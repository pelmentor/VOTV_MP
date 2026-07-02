#!/usr/bin/env python3
"""skin_transfer -- auto-rig (weight transfer) for the VOTV rigs + a validator.

Goal: skin a NEW mesh (the user's Dr-Kel-style or anthro model) to a game rig
automatically by TRANSFERRING per-vertex bone weights from an already-skinned
game mesh -- no manual weight painting. Two target rigs:
  kel    : kel_lmao_Skeleton  (6 bones, low-poly, the 99% case)
  anthro : kerfurOmegaV1_Skeleton (102 bones, arms/fingers)

Algorithm (transfer): for each target vertex, take the k nearest source
vertices, gate by surface-normal agreement, blend their bone weights by inverse
distance, then Laplacian-smooth the result over the target mesh. Tuned against
ground truth via leave-one-out on the game's own skins.

CLI:
  validate SRC.glb TGT.glb [k smooth_iters alpha normal_min]
      Transfer SRC->TGT geometry, compare to TGT's REAL weights. Prints
      top-1 bone match % + total-variation weight error. The tuning metric.

Dev/RE tool only (RULE 3); reads the gitignored .glb under
research/pak_re/mesh_out (from tools/mesh_extract).
"""
import json
import os
import struct
import sys

import numpy as np

_CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
_NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
_CT_MAX = {5121: 255.0, 5123: 65535.0}


def load_glb(path):
    d = open(path, "rb").read()
    magic, ver, _ = struct.unpack_from("<III", d, 0)
    assert magic == 0x46546C67, "not a glb"
    jlen, _ = struct.unpack_from("<II", d, 12)
    gltf = json.loads(d[20:20 + jlen].decode("utf-8"))
    off = 20 + jlen
    blen, btype = struct.unpack_from("<II", d, off)
    assert btype == 0x004E4942, "expected BIN chunk"
    return gltf, d[off + 8: off + 8 + blen]


def accessor(gltf, buf, idx):
    a = gltf["accessors"][idx]
    bv = gltf["bufferViews"][a["bufferView"]]
    dt = np.dtype(_CT[a["componentType"]])
    nc = _NC[a["type"]]
    base = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride") or (dt.itemsize * nc)
    count = a["count"]
    if stride == dt.itemsize * nc:  # tightly packed -> one read
        out = np.frombuffer(buf, dtype=dt, count=count * nc, offset=base).reshape(count, nc).astype(np.float32)
    else:
        out = np.empty((count, nc), np.float32)
        for i in range(count):
            out[i] = np.frombuffer(buf, dtype=dt, count=nc, offset=base + i * stride)
    if a.get("normalized") and a["componentType"] in _CT_MAX:
        out = out / _CT_MAX[a["componentType"]]
    return out


def joint_names(gltf):
    return [gltf["nodes"][jn].get("name", f"joint{jn}") for jn in gltf["skins"][0]["joints"]]


def mesh_skin(gltf, buf):
    """(pos[N,3], nrm[N,3], weights[N,B] over this skin's joints, names[B], tris[T,3])."""
    names = joint_names(gltf)
    B = len(names)
    P, Nr, W, TRI = [], [], [], []
    voff = 0
    for prim in gltf["meshes"][0]["primitives"]:
        at = prim["attributes"]
        pos = accessor(gltf, buf, at["POSITION"])
        nrm = accessor(gltf, buf, at["NORMAL"]) if "NORMAL" in at else np.zeros_like(pos)
        ji = accessor(gltf, buf, at["JOINTS_0"]).astype(np.int32)
        wt = accessor(gltf, buf, at["WEIGHTS_0"])
        n = pos.shape[0]
        dense = np.zeros((n, B), np.float32)
        for k in range(4):
            np.add.at(dense, (np.arange(n), ji[:, k]), wt[:, k])
        s = dense.sum(1, keepdims=True)
        dense /= np.where(s > 1e-8, s, 1.0)
        if "indices" in prim:
            tri = accessor(gltf, buf, prim["indices"]).astype(np.int64).reshape(-1, 3) + voff
            TRI.append(tri)
        P.append(pos); Nr.append(nrm); W.append(dense); voff += n
    tris = np.concatenate(TRI) if TRI else np.zeros((0, 3), np.int64)
    return np.concatenate(P), np.concatenate(Nr), np.concatenate(W), names, tris


def knn_transfer(sp, sn, sw, tp, tn, k=8, normal_min=0.0, chunk=256):
    """k-nearest, normal-gated, inverse-distance-weighted weight blend."""
    B = sw.shape[1]
    out = np.zeros((tp.shape[0], B), np.float32)
    has_n = sn.any() and tn.any()
    for i in range(0, tp.shape[0], chunk):
        blk = tp[i:i + chunk]
        d2 = ((blk[:, None, :] - sp[None, :, :]) ** 2).sum(-1)          # [c,Ns]
        nn = np.argpartition(d2, min(k, sp.shape[0] - 1), axis=1)[:, :k]  # [c,k]
        rows = np.arange(blk.shape[0])[:, None]
        dk = d2[rows, nn]                                                # [c,k]
        wk = 1.0 / (dk + 1e-6)
        if has_n:
            dot = (tn[i:i + chunk][:, None, :] * sn[nn]).sum(-1)         # [c,k]
            wk = wk * (dot > normal_min)
        ws = wk.sum(1, keepdims=True)
        wk = np.where(ws > 1e-12, wk / np.where(ws > 1e-12, ws, 1.0), 1.0 / k)
        out[i:i + chunk] = np.einsum("ck,ckb->cb", wk.astype(np.float32), sw[nn])
    s = out.sum(1, keepdims=True)
    return out / np.where(s > 1e-8, s, 1.0)


def smooth(w, tris, iters=4, alpha=0.5):
    if iters <= 0 or tris.shape[0] == 0:
        return w
    e = np.concatenate([tris[:, [0, 1]], tris[:, [1, 2]], tris[:, [2, 0]]])
    e = np.concatenate([e, e[:, ::-1]])
    i, j = e[:, 0], e[:, 1]
    n = w.shape[0]
    for _ in range(iters):
        acc = np.zeros_like(w); cnt = np.zeros((n, 1), np.float32)
        np.add.at(acc, i, w[j]); np.add.at(cnt, i, 1.0)
        mean = acc / np.where(cnt > 0, cnt, 1.0)
        w = (1 - alpha) * w + alpha * mean
    s = w.sum(1, keepdims=True)
    return w / np.where(s > 1e-8, s, 1.0)


def align_common(sn_names, sw, tn_names, tw):
    union = sorted(set(sn_names) | set(tn_names))
    ui = {n: k for k, n in enumerate(union)}
    def remap(names, w):
        m = np.zeros((w.shape[0], len(union)), np.float32)
        for j, nm in enumerate(names):
            m[:, ui[nm]] += w[:, j]
        return m
    return remap(sn_names, sw), remap(tn_names, tw), union


def validate(src, tgt, k=8, iters=4, alpha=0.5, normal_min=0.0):
    sg, sb = load_glb(src); tg, tb = load_glb(tgt)
    sp, sn, sw, snm, _ = mesh_skin(sg, sb)
    tp, tn, tw, tnm, ttri = mesh_skin(tg, tb)
    pred = knn_transfer(sp, sn, sw, tp, tn, k=k, normal_min=normal_min)
    pred = smooth(pred, ttri, iters=iters, alpha=alpha)
    predC, trueC, _ = align_common(snm, pred, tnm, tw)
    top1 = float((predC.argmax(1) == trueC.argmax(1)).mean()) * 100
    tv = 0.5 * np.abs(predC - trueC).sum(1)
    print(f"  {os.path.basename(src)} -> {os.path.basename(tgt)} "
          f"[k={k} smooth={iters}/{alpha} nmin={normal_min}]")
    print(f"    top-1 {top1:5.1f}% | TV mean {tv.mean():.3f} med {np.median(tv):.3f} p90 {np.percentile(tv,90):.3f} "
          f"| TV<=.1 {float((tv<=.1).mean())*100:4.1f}% <=.25 {float((tv<=.25).mean())*100:4.1f}%")


def main():
    a = sys.argv
    if len(a) >= 4 and a[1] == "validate":
        kw = dict(k=int(a[4]) if len(a) > 4 else 8,
                  iters=int(a[5]) if len(a) > 5 else 4,
                  alpha=float(a[6]) if len(a) > 6 else 0.5,
                  normal_min=float(a[7]) if len(a) > 7 else 0.0)
        validate(a[2], a[3], **kw)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
