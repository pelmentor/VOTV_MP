#!/usr/bin/env python3
"""builtin_skin_census -- verify which game meshes are safe player-body skins.

Emits the kBuiltinSkins table for src/votv-coop/src/coop/player/skin_registry.cpp.
Re-run at EVERY game-version re-target; the table is trusted only as far as this
census (born 2026-07-02: sk/assbreather shares the player skeleton asset but its
root bone is 'rootKerfur_010' -- a Blender-duplicate leftover absent from the
skeleton; applying it poisoned the player AnimInstance and locomotion never
recovered across later swaps. The game itself never loads that mesh).

A mesh is a safe builtin ONLY if all four hold:
  (1) it imports Skeleton kerfurOmegaV1_Skeleton (the player-body rig);
  (2) its refskel bone 0 is 'rootKerfur';
  (3) EVERY refskel bone name exists in the skeleton asset's name map;
  (4) it has exactly one SkeletalMesh export named == the package stem
      (that is the LoadObject path shape skin_registry assumes).

Usage:
  python builtin_skin_census.py [<extracted_content_root>] [--paklist <file>]

  extracted_content_root  defaults to research/pak_re/extracted/VotV/Content
  --paklist               output of `repak list VotV-WindowsNoEditor.pak`; used to
                          warn about kerfurOmega_*Skin pak entries NOT present in
                          the extracted tree (extract them first, then re-run --
                          the 2026-07-02 census found 14 such skins the old
                          partial extraction was blind to).
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue_cook
import ue_pkg
import ue_skelmesh as sm

PLAYER_SKELETON = "kerfurOmegaV1_Skeleton"
PLAYER_ROOT_BONE = "rootKerfur"
SKELETON_REL = r"meshes\kerfurAnthro\sk\kerfurOmegaV1_Skeleton.uasset"
# The kel look is dr_kel (the pristine native mesh) -- never offered as a builtin.
EXCLUDE_STEMS = {"kerfurOmega_KelSkin"}

# Stable curated skin names (stem -> name shown in the F1 browser / persisted in
# the ini / sent on the wire). A stem without an override gets a derived name.
NAME_OVERRIDES = {
    "kerfurOmegaV1": "kerfur_omega",
    "kerfurOmegaV1_h": "kerfur_omega_h",
    "kerfurOmegaV1_m": "kerfur_omega_m",
    "kerfurOmegaV1_nc": "kerfur_omega_nc",
    "KerfurO_maid": "kerfur_maid",
    "kerfurOmega_ariralSkin": "kerfur_ariral",
    "kerfurOmega_ariralSuitSkin": "kerfur_ariral_suit",
    "kerfurOmega_keljoySkin": "kerfur_keljoy",
    "kerfurOmega_mannequinSkin": "kerfur_mannequin",
    "skerfuro": "skerfuro",
    "ScrappyKeith": "scrappy_keith",
    "kerfurOmega_antibreatherSkin": "kerfur_antibreather",
    "kerfurOmega_argplushSkin": "kerfur_argplush",
    "kerfurOmega_alienSkin": "kerfur_alien",
    "kerfurOmega_fleshlySkin": "kerfur_fleshly",
    "kerfurOmega_skeletonSkin": "kerfur_skeleton",
    "kerfurOmega_vargskeletonSkin": "kerfur_vargskeleton",
    "kerfurOmega_maxwellskin": "kerfur_maxwell",
    "kerfurOmega_erieSkin": "kerfur_erie",
    "kerfurOmega_erieV4Skin": "kerfur_erie_v4",
    "kerfurOmega_igetisSkin": "kerfur_igetis",
    "kerfurOmega_moniqueSkin": "kerfur_monique",
    "kerfurOmega_krampusSkin": "kerfur_krampus",
    "kerfurOmega_mynetSkin": "kerfur_mynet",
    "kerfurOmega_furfurSkin": "kerfur_furfur",
}


def derived_name(stem):
    n = stem
    for pre in ("kerfurOmega_", "kerfurOmegaV1_", "KerfurO_"):
        if n.startswith(pre):
            n = n[len(pre):]
            break
    n = re.sub(r"[Ss]kin$", "", n)
    n = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", n).lower().strip("_")
    return "kerfur_" + n if not n.startswith("kerfur") else n


def parse_pkg(b):
    c = ue_pkg.parse_summary(b)
    kv = {t[1]: t[3] for t in c.tok if t[1]}
    o = kv["nameOffset"]
    names = []
    for _ in range(kv["nameCount"]):
        ln = struct.unpack_from("<i", b, o)[0]
        o += 4
        if ln > 0:
            names.append(b[o:o + ln - 1].decode("latin1", "replace"))
            o += ln
        else:
            names.append(b[o:o + (-ln) * 2 - 2].decode("utf-16-le", "replace"))
            o += (-ln) * 2
        o += 4
    def fname(off):
        i, n = struct.unpack_from("<ii", b, off)
        s = names[i] if 0 <= i < len(names) else f"<bad:{i}>"
        return s + (f"_{n - 1}" if n else "")
    imps = []
    o = kv["importOffset"]
    for _ in range(kv["importCount"]):
        imps.append((fname(o + 8), fname(o + 20)))
        o += 28
    exps = []
    o = kv["exportOffset"]
    for _ in range(kv["exportCount"]):
        ci = struct.unpack_from("<i", b, o)[0]
        obj = fname(o + 16)
        cls = imps[-ci - 1][1] if ci < 0 else f"<exp:{ci}>"
        exps.append((cls, obj))
        o += 104
    return names, imps, exps


def main():
    args = [a for a in sys.argv[1:]]
    paklist = None
    if "--paklist" in args:
        i = args.index("--paklist")
        paklist = args[i + 1]
        del args[i:i + 2]
    root = args[0] if args else os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        "research", "pak_re", "extracted", "VotV", "Content")
    if not os.path.isdir(root):
        sys.exit(f"extracted content root not found: {root}")

    # Skeleton name map = the bone-name universe.
    skel_b = open(os.path.join(root, SKELETON_REL), "rb").read()
    skel_names = set(parse_pkg(skel_b)[0])

    # Completeness cross-check vs the pak listing (partial extraction is BLIND).
    if paklist:
        listed = set()
        for line in open(paklist, encoding="utf-8", errors="replace"):
            m = re.search(r"VotV/Content/(.+?\.uasset)\s*$", line.strip())
            if m and re.search(r"kerfurOmega.*[Ss]kin\.uasset$", m.group(1)):
                listed.add(m.group(1).replace("/", os.sep))
        missing = [p for p in sorted(listed)
                   if not os.path.isfile(os.path.join(root, p))]
        if missing:
            print(f"WARNING: {len(missing)} kerfurOmega_*Skin pak entries NOT extracted "
                  f"(census is blind to them -- extract with repak, then re-run):")
            for p in missing:
                print(f"  {p}")

    needle = PLAYER_SKELETON.encode()
    rows, bad = [], []
    scanned = 0
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if not fn.endswith(".uasset"):
                continue
            p = os.path.join(dirpath, fn)
            with open(p, "rb") as fh:
                b = fh.read()
            scanned += 1
            if needle not in b:
                continue
            try:
                names, imps, exps = parse_pkg(b)
            except Exception as e:
                print(f"  PARSE FAIL {os.path.relpath(p, root)}: {e}")
                continue
            stem = fn[:-7]
            skmesh = [obj for cls, obj in exps if cls == "SkeletalMesh"]
            if not skmesh:
                continue
            rel = os.path.relpath(p, root)
            if stem in EXCLUDE_STEMS:
                continue
            ok_skel = ("Skeleton", PLAYER_SKELETON) in imps
            ok_exp = skmesh == [stem]
            uexp = open(p[:-7] + ".uexp", "rb").read()
            payload = uexp[:-4]
            rstart, _hc = sm.property_block_end(payload, names)
            rs, _nb = sm.find_refskel(payload, names, rstart + 2 + 28)
            bones = ue_cook.refskel_bone_names(payload, rs, names)
            ok_root = bones[0] == PLAYER_ROOT_BONE
            foreign = [bn for bn in bones if bn not in skel_names]
            if ok_skel and ok_exp and ok_root and not foreign:
                rows.append((NAME_OVERRIDES.get(stem, derived_name(stem)), rel, stem))
            else:
                why = []
                if not ok_skel: why.append("skeleton import mismatch")
                if not ok_root: why.append(f"bone0={bones[0]!r}")
                if foreign: why.append(f"bones not in skeleton: {foreign[:4]}")
                if not ok_exp: why.append(f"exports={skmesh}")
                bad.append((rel, "; ".join(why)))

    print(f"\nscanned {scanned} uassets under {root}")
    print(f"REJECTED ({len(bad)}):")
    for rel, why in bad:
        print(f"  {rel:66} {why}")
    rows.sort(key=lambda r: r[0])
    print(f"\nVERIFIED builtin skins ({len(rows)}) -- kBuiltinSkins table:")
    for skin, rel, stem in rows:
        pkg = "/Game/" + rel[:-7].replace(os.sep, "/")
        pad = " " * max(1, 22 - len(skin))
        print(f'    {{ "{skin}",{pad}L"{pkg}.{stem}" }},')


if __name__ == "__main__":
    main()
