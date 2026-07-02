#!/usr/bin/env python3
"""repose -- auto scale+repose a GoldSrc A-pose model to the VOTV T-pose standard.

LEARNED from a single manual example (the user posed+scaled one HL scientist onto
dr_kel's anthro T-pose in Blender). Because GoldSrc skinning is RIGID (1 bone/vertex),
the manual repose is EXACTLY a per-bone rigid transform, and -- since bone head offsets
are rest-invariant under posing -- it reduces to ONE transferable quantity per bone: a
LOCAL POSE ROTATION (in that bone's own frame). That set of local rotations + a target
height IS the "VOTV T-pose standard" and generalizes to any model on the same HL Bip01
skeleton. Validated: applying the learned profile back to the source reproduces the
manual PSK to floating-point zero.

  learn <origDir> <posed.psk> <profile.json>   # extract the standard from the example
  apply <origDir> <profile.json> <out.obj>     # auto-repose a NEW model's A-pose
        [--validate <psk>]                      #   ...and check vs a ground-truth psk
  apply <origDir> default <out.obj>            # ...with the DEFAULT library profile

Profiles live in the LIBRARY tools/client_model/profiles/ (one json per learned
example, provenance in profiles/README.md); DEFAULT_PROFILE below names the default
(user 2026-07-02: keep a base of profiles, new one as default).

<origDir> = mdl_extract output (model.obj + model.bones.json with bone world matrices).
Pipeline: mdl_extract -> repose.apply -> ue_cook. Dev/RE tool (RULE 3).
"""
import json
import os
import struct
import sys

import numpy as np

# The library default (the "VOTV T-pose standard" every new model gets unless a
# profile is named explicitly). Swap by editing this one line; the library keeps
# every learned profile side by side (profiles/README.md). v1 narrow is the default
# by IN-GAME VERDICT (2026-07-02 evening: the v2 wide look was rejected hands-on;
# v2 stays in the library).
DEFAULT_PROFILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "profiles", "tpose_v1_narrow_2026-07-01.json")


# ---------- IO ----------
def load_apose(d):
    V = []
    for line in open(os.path.join(d, "model.obj")):
        if line.startswith("v "):
            V.append([float(x) for x in line.split()[1:4]])
    V = np.array(V, float)
    m = json.load(open(os.path.join(d, "model.bones.json")))
    vbone = np.array(m["vert_bone"])
    W = np.array(m["world"]).reshape(-1, 3, 4)     # 3x4 (R|t) world, column-vector convention
    parent = [b["parent"] for b in m["bones"]]
    names = [b["name"] for b in m["bones"]]
    return V, vbone, W, parent, names


def psk_points(path):
    b = open(path, "rb").read(); o = 0; ch = {}
    while o + 32 <= len(b):
        c = b[o:o + 20].split(b"\0")[0].decode("latin1")
        _, ds, dc = struct.unpack_from("<iii", b, o + 20)
        if ds < 0 or dc < 0:
            break
        ch[c] = (o + 32, ds, dc); o += 32 + ds * dc
    o, ds, dc = ch["PNTS0000"]; P = np.frombuffer(b, "<f4", dc * 3, o).reshape(dc, 3).astype(float)
    o, ds, dc = ch["RAWWEIGHTS"]; pb = np.full(len(P), -1, int)
    for i in range(dc):
        w, pi, bi = struct.unpack_from("<fii", b, o + i * 12); pb[pi] = bi
    return P, pb


# ---------- math ----------
def to4(W3):
    M = np.eye(4); M[:3, :] = W3; return M


def orthonormal(R):
    U, _, Vt = np.linalg.svd(R[:3, :3])
    Rr = U @ Vt
    if np.linalg.det(Rr) < 0:
        U[:, -1] *= -1; Rr = U @ Vt
    return Rr


def rot4(R3):
    M = np.eye(4); M[:3, :3] = R3; return M


def umeyama(X, Y):
    """least-squares similarity mapping X->Y (Nx3): returns s, R(3x3), t(3)."""
    mx, my = X.mean(0), Y.mean(0); Xc, Yc = X - mx, Y - my
    S = Xc.T @ Yc / len(X); U, D, Vt = np.linalg.svd(S)
    d = np.sign(np.linalg.det(U @ Vt)); Dd = np.array([1, 1, d])
    R = U @ np.diag(Dd) @ Vt; R = R.T
    var = (Xc ** 2).sum() / len(X); s = (D * Dd).sum() / var
    t = my - s * R @ mx
    return s, R, t


# ---------- learn ----------
def learn(orig_dir, posed_psk, prof_out):
    V, vbone, W3, parent, names = load_apose(orig_dir)
    P, pb = psk_points(posed_psk)
    nb = len(names)
    assert len(V) == len(P) and (vbone == pb).all(), \
        "point order/bone mismatch between mdl parse and PSK -- cannot learn"
    WA = np.array([to4(W3[i]) for i in range(nb)])

    # per-bone measured similarity A->T (only bones that carry vertices)
    Sb = [None] * nb; scales = []
    for b in range(nb):
        m = vbone == b
        if m.sum() >= 3:
            s, R, t = umeyama(V[m], P[m])
            M = np.eye(4); M[:3, :3] = s * R; M[:3, 3] = t; Sb[b] = M; scales.append(s)
    scale = float(np.median(scales))

    # measured UNIT-scale T-world frame per vertex-bone: W_Tu = (S/scale) @ WA
    WTu = [None] * nb
    for b in range(nb):
        if Sb[b] is not None:
            Su = Sb[b].copy(); Su[:3, :] /= scale
            WTu[b] = Su @ WA[b]

    # fill vertless connector bones (root, neck, ...) so the hierarchy is complete.
    # their pose rotation is unconstrained by verts; choose a natural default:
    #   root  -> orientation of its first posed child (rigid with pelvis)
    #   inner -> identity local pose (stays aligned with its parent)
    order = sorted(range(nb), key=lambda b: (0 if parent[b] < 0 else 1, b))
    for b in order:
        if WTu[b] is not None:
            continue
        pa = parent[b]
        if pa < 0:  # root: borrow a child's world rotation
            ch = next((c for c in range(nb) if parent[c] == b and WTu[c] is not None), None)
            R = orthonormal(WTu[ch]) if ch is not None else np.eye(3)
            M = np.eye(4); M[:3, :3] = R; WTu[b] = M          # at origin
        else:       # inner: identity local pose relative to parent
            RestL = np.linalg.inv(WA[pa]) @ WA[b]
            WTu[b] = WTu[pa] @ RestL

    # transferable = per-bone LOCAL POSE DELTA: a FULL rigid (R + t) in the bone's own
    # rest frame -- pose[b] = RestL_A[b]^-1 @ L_Tu[b]. Format 1 stored only the rotation
    # part; that reproduced a pure RE-POSE (the v1 narrow example) to ~0 but silently
    # dropped JOINT TRANSLATIONS -- the v2 wide example moves shoulder/arm joints
    # outward, and rotation-only left a 16-unit residual (2026-07-02). The translation
    # rides in the rest-local frame, so it transfers by bone NAME like the rotation.
    pose_local = {}
    for b in range(nb):
        pa = parent[b]
        if pa < 0:
            M = np.eye(4); M[:3, :3] = orthonormal(WTu[b])   # root: world rotation @ origin
            pose_local[b] = M.tolist()
        else:
            RestL = np.linalg.inv(WA[pa]) @ WA[b]
            LTu = np.linalg.inv(WTu[pa]) @ WTu[b]
            D = np.linalg.inv(RestL) @ LTu
            M = np.eye(4); M[:3, :3] = orthonormal(D); M[:3, 3] = D[:3, 3]
            pose_local[b] = M.tolist()

    # UP is a CONSTANT of the target space, never inferred from the mesh: the PSK/cook
    # space is UE Z-up (the cook is a pure Y-negation, SPEC.md). The old argmax(bbox)
    # heuristic broke the moment a T-pose ARM SPAN (X) grew wider than the height --
    # learn then measured "height" along the arms and grounded the model sideways
    # (2026-07-02: the new-profile example read up=axis0, self-reproduce residual 17.58).
    up = 2
    prof = {
        "format": 2, "skeleton": "HL_Bip01", "source": os.path.basename(posed_psk),
        "bones": names, "parent": parent,
        "pose_local": [pose_local[b] for b in range(nb)],
        "target_height": float(P[:, up].max() - P[:, up].min()),
        "up_axis": up,
        "foot": float(P[:, up].min()),
        "center": [float((P[:, i].max() + P[:, i].min()) / 2) for i in range(3)],
    }
    json.dump(prof, open(prof_out, "w"), indent=1)
    print(f"[learn] {nb} bones, scale={scale:.4f}, target_height={prof['target_height']:.2f}, "
          f"up=axis{up}  -> {prof_out}")

    # self-validate: rebuild from the profile and compare to the ground-truth PSK
    Vt = _apply(V, vbone, WA, parent, prof, names)
    err = np.linalg.norm(Vt - P, axis=1)
    print(f"[learn] self-reproduce residual: max={err.max():.5f} mean={err.mean():.5f} "
          f"(should be ~0; units ~{prof['target_height']:.0f})")


# ---------- apply ----------
def _apply(V, vbone, WA, parent, prof, names):
    nb = len(parent)
    # Match pose deltas by bone NAME -- a new model's skeleton can differ from the
    # profile's (e.g. this one adds fingers/toes). Bones absent from the profile get an
    # identity local pose (keep their rest orientation); they still inherit their parent's
    # repose through the hierarchy. Root ("Bip01") stores a WORLD rotation (see learn).
    # Profile formats: 1 = rotation-only (pose_rot, 3x3; pre-2026-07-02 library entries),
    # 2 = full rigid local delta (pose_local, 4x4; carries JOINT TRANSLATIONS -- the wide
    # T-pose moves shoulder/arm joints, which rotation-only silently dropped). Both load;
    # normalization to 4x4 happens here so ONE apply path serves the whole library.
    if prof.get("format", 1) >= 2:
        pmap = {n: np.array(m, float) for n, m in zip(prof["bones"], prof["pose_local"])}
    else:
        pmap = {n: rot4(np.array(r, float)) for n, r in zip(prof["bones"], prof["pose_rot"])}
    I4 = np.eye(4)
    pose = [pmap.get(names[b], I4) for b in range(nb)]
    order = sorted(range(nb), key=lambda b: (0 if parent[b] < 0 else 1, b))
    WTu = [None] * nb
    for b in order:
        pa = parent[b]
        if pa < 0:
            WTu[b] = pose[b]                                  # root world rot at origin
        else:
            RestL = np.linalg.inv(WA[pa]) @ WA[b]
            LT = RestL @ pose[b]                              # keep new bone offset, apply std delta
            WTu[b] = WTu[pa] @ LT
    # rigid per-bone repose into unit T-space
    Vu = np.empty_like(V)
    invWA = [np.linalg.inv(WA[b]) for b in range(nb)]
    for i in range(len(V)):
        b = int(vbone[i])
        vh = np.array([V[i, 0], V[i, 1], V[i, 2], 1.0])
        Vu[i] = (WTu[b] @ (invWA[b] @ vh))[:3]
    # scale to target height, then place (feet on ground, centered) per profile
    up = prof["up_axis"]
    h = Vu[:, up].max() - Vu[:, up].min()
    s = prof["target_height"] / h
    Vs = Vu * s
    out = np.empty_like(Vs)
    for i in range(3):
        if i == up:
            out[:, i] = Vs[:, i] - Vs[:, i].min() + prof["foot"]
        else:
            out[:, i] = Vs[:, i] - (Vs[:, i].max() + Vs[:, i].min()) / 2 + prof["center"][i]
    return out


def apply(orig_dir, prof_path, out_obj, validate=None):
    V, vbone, W3, parent, names = load_apose(orig_dir)
    prof = json.load(open(prof_path))
    if names != prof["bones"]:
        print("[apply] WARNING: bone list differs from profile -- generalizing by index/name; "
              "verify the result.")
    WA = np.array([to4(W3[i]) for i in range(len(names))])
    Vt = _apply(V, vbone, WA, parent, prof, names)

    # write posed OBJ: reposed verts, original vt/f copied verbatim (topology unchanged)
    with open(os.path.join(orig_dir, "model.obj")) as f:
        lines = f.readlines()
    vi = 0
    with open(out_obj, "w") as o:
        o.write("# repose.py -> VOTV T-pose\n")
        for ln in lines:
            if ln.startswith("v "):
                o.write(f"v {Vt[vi,0]:.5f} {Vt[vi,1]:.5f} {Vt[vi,2]:.5f}\n"); vi += 1
            elif ln.startswith(("vt ", "f ", "usemtl", "g ", "o ")):
                o.write(ln)
    print(f"[apply] reposed {vi} verts -> {out_obj}  bbox={ (Vt.max(0)-Vt.min(0)).round(1) }")

    if validate:
        P, pb = psk_points(validate)
        if len(P) == len(Vt):
            err = np.linalg.norm(Vt - P, axis=1)
            print(f"[apply] validate vs {os.path.basename(validate)}: "
                  f"max={err.max():.5f} mean={err.mean():.5f}")
        else:
            print(f"[apply] validate: point count {len(Vt)} != {len(P)} (different model) -- skipped")


def main():
    a = sys.argv[1:]
    if len(a) >= 4 and a[0] == "learn":
        learn(a[1], a[2], a[3])
    elif len(a) >= 4 and a[0] == "apply":
        val = a[a.index("--validate") + 1] if "--validate" in a else None
        prof = DEFAULT_PROFILE if a[2] == "default" else a[2]
        apply(a[1], prof, a[3], val)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
