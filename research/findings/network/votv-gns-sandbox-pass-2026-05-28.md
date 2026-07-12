# GNS sandbox §12 gate — PASS report

**Date:** 2026-05-28
**Status:** §12 gate of `votv-gns-integration-plan-2026-05-27.md` PASSED.
**Unblocks:** PR-1 (vendor + link) of the 4-PR GNS migration.

## Result

`GameNetworkingSockets_s.lib` = **10,559,088 bytes (~10.07 MB)**, comfortably
above the >5 MB threshold the plan set as the PASS signal. Built standalone
at `d:\Projects\Programming\gns-sandbox\` (outside the project tree, per
§12 of the plan).

Host: Windows 10 Pro 19045, MSVC 14.29.30133 (VS 2019 BuildTools), CMake
4.3.0-rc3, vcpkg 2026-04-08 commit e0612b42, protobuf 33.4.0 via vcpkg
`x64-windows-static` triplet.

Build path: `d:\Projects\Programming\gns-sandbox\build\src\Release\GameNetworkingSockets_s.lib`.

## Two corrections to the original integration plan

The plan (`votv-gns-integration-plan-2026-05-27.md` §8.1) said to pin
**v1.4.1** "or whatever HEAD is stable at the time." That choice doesn't
work in 2026; PR-1 must inherit two corrections:

### Correction 1 — pin tag must be v1.5.1, not v1.4.1

GNS **v1.4.1** (Sep 2022, commit `505c697d`) calls `.c_str()` on
protobuf-returned strings. Protobuf 22+ (Mar 2023) changed those
return types from `const std::string&` to `std::string_view`, and
`string_view` has no `.c_str()` member (only `.data()`). Modern
protobuf (33.4 in 2026) makes v1.4.1 source-incompatible.

Concrete compile errors observed:
```
csteamnetworkingsockets.cpp(653,3): error C2039: "c_str": is not
  a member of "std::basic_string_view<char,std::char_traits<char>>"
steamnetworkingsockets_connections.cpp(1298,2): error C2039 ...
steamnetworkingsockets_udp.cpp(1482,3): error C2039 ...
steamnetworkingsockets_udp.cpp(1735,3): error C2039 ...
```

**GNS v1.5.1** (Sep 2023, commit `fa489fd`) is the next tagged release
after v1.4.1, and includes the modern-protobuf compatibility fixes.
Verified to compile clean against protobuf 33.4.

### Correction 2 — `-DVCPKG_MANIFEST_MODE=OFF` is required

GNS v1.5.1's `vcpkg.json` pins a `builtin-baseline` commit
(`c3867e714dd3a51c272826eea77267876517ed99`). vcpkg's toolchain
auto-enters manifest mode when it sees that file, and tries to fetch
the baseline commit. A shallow-cloned vcpkg (`--depth 1`) doesn't
have it, and the configure fails with:

```
fatal: path 'versions/baseline.json' exists on disk, but not in
  'c3867e714dd3a51c272826eea77267876517ed99'
while loading baseline version for openssl
```

Two fixes:
- Unshallow vcpkg (`cd C:\vcpkg && git fetch --unshallow`) — works
  but ~hundreds of MB of history.
- Add **`-DVCPKG_MANIFEST_MODE=OFF`** to the configure line, and pre-
  install needed deps via `vcpkg install protobuf:x64-windows-static`.

The second option is the chosen path. Why: PR-1 needs deterministic
deps anyway, and pinning the vcpkg install to classic mode keeps the
build reproducible without depending on vcpkg's baseline-resolution
machinery. The project's `BUILDING.md` (to be written in PR-1) will
say:

```
# One-time vcpkg setup:
vcpkg install protobuf:x64-windows-static

# Configure:
cmake -S . -B build -G "Visual Studio 16 2019" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
    -DVCPKG_MANIFEST_MODE=OFF
```

## The verbatim verified configure command (for PR-1)

```
cmake -S GameNetworkingSockets -B build ^
      -G "Visual Studio 16 2019" -A x64 ^
      -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
      -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
      -DVCPKG_MANIFEST_MODE=OFF ^
      -DUSE_CRYPTO=BCrypt ^
      -DUSE_CRYPTO25519=Reference ^
      -DBUILD_SHARED_LIB=OFF ^
      -DBUILD_STATIC_LIB=ON ^
      -DBUILD_TESTS=OFF ^
      -DBUILD_EXAMPLES=OFF ^
      -DProtobuf_USE_STATIC_LIBS=ON ^
      -DMSVC_CRT_STATIC=ON
```

Then:
```
cmake --build build --config Release --target GameNetworkingSockets_s
```

Configure took ~6.5 min (most of it the openssl bootstrap that GNS
pulls transitively via its vcpkg.json default-feature even though we
disabled OpenSSL — openssl libs still get installed; we just don't
link them). Build itself took ~13 min on a single-job MSVC compile.

## API symbols sanity-checked in v1.5.1

All §5 plan symbols are present in v1.5.1's
`include/steam/isteamnetworkingsockets.h`:
- `CreateListenSocketIP` (line 65)
- `ConnectByIPAddress` (line 89)
- `SendMessageToConnection` (line 268)
- `ReceiveMessagesOnConnection` (line 334)
- `ConfigureConnectionLanes` (line 461)
- `RunCallbacks` (line 800)
- `k_ESteamNetworkingConnectionState_*` enum values
- `k_nSteamNetworkingSend_*` flag values

No version-skew surprises ahead of PR-2.

## Encryption-cannot-be-disabled re-verified

GNS v1.5.1's `CMakeLists.txt` still has no `USE_CRYPTO=none` option;
the three valid values remain OpenSSL / libsodium / BCrypt
(line 63-65). Per the plan §3, this is acceptable — handshake adds
~10 ms one-time at connect, AES-GCM adds ~16 B per packet. AES-NI
makes the steady-state cost sub-microsecond.

## Open risks NOT explored by the §12 gate

The gate verifies GNS compiles + links standalone. It does NOT
verify:

1. **GNS + our existing CMake additions** — MinHook target conflicts,
   `/W4 /permissive-` flag inheritance issues. PR-1's job to verify.
2. **Resulting `votv-coop.dll` size** — plan §4 estimate is ~2.2 MB.
   PR-1 verifies actual size.
3. **DllMain thread-creation safety** — plan §11.2 risk: must defer
   `GameNetworkingSockets_Init()` to first-Session-start, never call
   from DllAttach. Already our existing pattern.
4. **GNS internal thread + game-thread interactions** — plan §7
   discussed; PR-2 verifies in actual gameplay.

## Final size table (for the BUILDING.md TODO list)

| Component | Disk size | Notes |
| --- | --- | --- |
| `libprotobuf.lib` (vcpkg, x64-windows-static) | 217 MB | Trimmed heavily at link by `/OPT:REF /OPT:ICF` |
| `libprotobuf-lite.lib` | 34 MB | Subset, may not be needed |
| `GameNetworkingSockets_s.lib` | 10.07 MB | Final static lib, gate-PASS |
| abseil libs (transitive via protobuf 33) | ~80 MB total | Trimmed at link; our DLL only pulls what GNS uses |

Final DLL size estimate after PR-1 link: still expected ~2-3 MB per
plan §4 (the ~217 MB of protobuf is mostly debug info + unused
descriptors that `/OPT:REF` will discard). PR-1 measures the actual
number.
