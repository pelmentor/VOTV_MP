#!/usr/bin/env python3
"""ue_pkg -- UE4.27 cooked package (.uasset) summary (de)serializer + round-trip.

Step 1 of the pure-Python SkeletalMesh cooker (docs/COOP_CLIENT_MODEL.md): prove we
can parse the FPackageFileSummary and re-emit it BYTE-IDENTICAL, so we can later edit
offsets (rename the asset, resize the export) and re-serialize correctly. No UE editor.

Fields whose values we will later EDIT (offsets/counts/sizes) are stored structured;
everything else is stored as the exact bytes consumed, so re-emit == original when
unedited. Assumes the 4.27 summary layout (all fields present).

Usage: python tools/ue_pkg.py roundtrip <file.uasset>
Dev/RE tool (RULE 3).
"""
import struct
import sys


class Cur:
    def __init__(self, b):
        self.b = b; self.o = 0
        self.tok = []          # ordered (kind, key, raw_bytes, value)

    def raw(self, n, kind="raw"):
        v = self.b[self.o:self.o + n]; self.o += n
        self.tok.append([kind, None, v, None]); return v

    def i32(self, key):
        raw = self.b[self.o:self.o + 4]; v = struct.unpack_from("<i", self.b, self.o)[0]; self.o += 4
        self.tok.append(["i32", key, raw, v]); return v

    def i64(self, key):
        raw = self.b[self.o:self.o + 8]; v = struct.unpack_from("<q", self.b, self.o)[0]; self.o += 8
        self.tok.append(["i64", key, raw, v]); return v

    def fstring(self):
        start = self.o
        ln = struct.unpack_from("<i", self.b, self.o)[0]; self.o += 4
        if ln > 0:
            self.o += ln
        elif ln < 0:
            self.o += (-ln) * 2
        self.tok.append(["raw", None, self.b[start:self.o], None])

    def array_raw(self, elem_size, key=None):
        """count i32 + count*elem_size bytes, kept as one raw blob (we don't edit these)."""
        start = self.o
        n = struct.unpack_from("<i", self.b, self.o)[0]; self.o += 4 + n * elem_size
        self.tok.append(["raw", key, self.b[start:self.o], None])
        return n


def engine_version(c):
    c.raw(2 + 2 + 2 + 4)   # u16 major, u16 minor, u16 patch, u32 changelist
    c.fstring()            # branch


def parse_summary(b):
    c = Cur(b)
    c.raw(4, "tag")                       # 0x9E2A83C1
    legacy = c.i32("legacy")              # -7
    if legacy != -4:
        c.i32("legacyUE3")
    c.i32("fileVerUE4")
    c.i32("fileVerLicensee")
    ncv = c.array_raw(20, "customVersions")   # count + count*(FGuid16+i32)=20
    c.i32("totalHeaderSize")
    c.fstring()                           # FolderName
    c.raw(4, "packageFlags")
    c.i32("nameCount"); c.i32("nameOffset")
    # NOTE: VOTV's cooked summary has NO LocalizationId (its version gating < 516).
    c.i32("gatherableCount"); c.i32("gatherableOffset")
    c.i32("exportCount"); c.i32("exportOffset")
    c.i32("importCount"); c.i32("importOffset")
    c.i32("dependsOffset")
    c.i32("softRefCount"); c.i32("softRefOffset")
    c.i32("searchableNamesOffset")
    c.i32("thumbnailTableOffset")
    c.raw(16, "guid")
    # Generations: count + count*(exportCount i32, nameCount i32)
    c.array_raw(8, "generations")
    engine_version(c)                     # SavedByEngineVersion
    engine_version(c)                     # CompatibleWithEngineVersion
    c.raw(4, "compressionFlags")
    c.array_raw(0, "compressedChunks")    # expect 0; elem size 0 (count only) -- FCompressedChunk if any would break; kel has none
    c.raw(4, "packageSource")
    # AdditionalPackagesToCook: FString array
    apc = struct.unpack_from("<i", b, c.o)[0]
    c.raw(4, "additionalPackagesToCookCount")
    for _ in range(apc):
        c.fstring()
    c.i32("assetRegistryDataOffset")
    c.i64("bulkDataStartOffset")
    c.i32("worldTileInfoDataOffset")
    c.array_raw(4, "chunkIDs")            # count + count*i32
    c.i32("preloadDependencyCount"); c.i32("preloadDependencyOffset")
    return c


def reserialize(c):
    return b"".join(t[2] for t in c.tok)


def roundtrip(path):
    b = open(path, "rb").read()
    c = parse_summary(b)
    out = reserialize(c)
    same = out == b[:c.o]
    print(f"summary parsed: {c.o} bytes of {len(b)}  round-trip identical: {same}")
    if not same:
        for i in range(min(len(out), c.o)):
            if out[i] != b[i]:
                print(f"  first diff at byte {i}: got {out[i]:#04x} want {b[i]:#04x}"); break
    # dump the editable fields
    keys = {t[1]: t[3] for t in c.tok if t[1] and t[3] is not None}
    for k in ("totalHeaderSize", "nameCount", "nameOffset", "exportCount", "exportOffset",
              "importCount", "importOffset", "dependsOffset", "assetRegistryDataOffset",
              "preloadDependencyCount", "preloadDependencyOffset"):
        print(f"    {k} = {keys.get(k)}")
    print(f"    bulkDataStartOffset = {next((t[3] for t in c.tok if t[1]=='bulkDataStartOffset'), None)}")


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "roundtrip":
        roundtrip(sys.argv[2])
    else:
        print(__doc__)
