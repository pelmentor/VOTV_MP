# R::FindClass is a process-wide perf time bomb

**Date:** 2026-05-27
**File:** `src/votv-coop/src/ue_wrap/reflection.cpp:157`
**Status:** documented, not implemented — proposal for next refactor pass
**Severity:** latent (any hot-path caller exposes it; the 5G STAGE 2
non_prop_entity_sync::Install regression was one such case, 19 GB RSS in
~30 s)

## The hazard

`R::FindClass(const wchar_t* className)` walks the entire `GUObjectArray`
(~1M slots in a loaded VOTV world) and for **every single slot** does:

```cpp
if (ToString(NameOf(obj)) != className) continue;
```

`ToString(NameOf(obj))` (`reflection.cpp:123-136`) constructs a fresh
`std::wstring` on every comparison — it copies the per-thread `FString`
scratch into a brand-new heap allocation, then `operator!=` compares
against the input `const wchar_t*`, then the `std::wstring` is destroyed
on the next loop iteration. **One heap malloc + one heap free per
GUObjectArray entry, per FindClass call.** At ~1M entries that's ~1M
mallocs per call.

The same pattern is in `FindObject`, `FindFunction` (when comparing leaf
names of UFunction children), and `FindObjectByClass`.

Every caller that hits an UNcached path triggers ~1M allocations. The
codebase has the contract (`reflection.h:75-94`) that these are
"intended for one-time setup", but the contract is not enforced and the
2026-05-27 Phase 5G STAGE 2 regression proved a hot-path caller can ship
without anyone noticing — until working-set hits 19 GB.

## Why current callers don't (usually) explode

Sibling `Install()` modules (garbage_sync, weather_sync, item_activate,
npc_sync, grab_observer, prop_lifecycle) all gate `R::FindClass` behind
an installed-atomic so the search runs ~tens of times during the first
few hundred ms of session boot, then never again. Total per-session cost:
~tens of millions of allocs spread over a second — invisible.

The Phase 5G STAGE 2 ship broke that contract: `Install()` ran 9
FindClass calls per harness pump tick (~125 Hz). 9 × 1M × 125 =
~1.1 billion allocs/sec. The heap allocator can't keep up; commit charge
balloons.

## Proposed root-cause fix (one paragraph)

Change `FindClass` (and `FindObject` / `FindFunction` / `FindObjectByClass`)
to compare `FName.ComparisonIndex` against an interned search-name index,
NOT to construct a `std::wstring` per object. The flow:

1. **Intern the search name once per call.** Look up
   `className`'s `FName` via a name-pool lookup helper
   (`FindFName(const wchar_t*)`). UE4.27 exposes
   `FName::FName(const TCHAR*, EFindName=FNAME_Find)` as a native — AOB-
   resolve it the same way we resolve `FNameToString`. If the search name
   isn't in the name pool, no UObject can possibly have that name, so the
   loop is trivially empty and returns null in O(1).
2. **Compare integers, not strings.** Inside the loop, read
   `NameOf(obj).ComparisonIndex` (one int32 load) and compare against the
   interned index. Zero allocations. ~1M int compares cost a fraction of
   a millisecond.
3. **Suffix-meta check stays.** For `FindClass`'s `meta.compare(...,
   L"Class")` tail check, keep the current `ClassNameOf` path but it now
   only runs for objects whose name index matched — typically 0–2 hits
   across the whole array.
4. **Wrap the helper in `ue_wrap/reflection_fname_cache.cpp`** to keep
   the change behind the `ue_wrap` boundary. No caller-side API change:
   `FindClass(L"foo_C")` keeps its signature.

Result: every hot-path caller becomes cheap regardless of whether they
forget the install-latch. The 19-GB-RSS class of bug is structurally
impossible after the fix.

## Why not implement now

Out of scope for the immediate Phase 5G STAGE 2 hang fix (already shipped
with the install-latch). The FindClass refactor touches `ue_wrap`
substrate that the whole coop layer depends on — needs its own audit
cycle. Queued as a separate work item.

## Cross-refs

- `src/votv-coop/src/coop/non_prop_entity_sync.cpp:547-568` — the install
  loop that triggered the audit (now O(1) in steady state via
  `g_allInstalled` latch).
- `src/votv-coop/src/ue_wrap/reflection.cpp:123-136` — `ToString` (the
  per-call `std::wstring` alloc).
- `src/votv-coop/src/ue_wrap/reflection.cpp:157-176` — `FindClass`.
- `src/votv-coop/src/ue_wrap/reflection.cpp:178-189` — `FindFunction`
  (same hazard on the leaf-name comparison).
- `src/votv-coop/src/ue_wrap/reflection.cpp:144-155` — `FindObject`
  (same).
- `src/votv-coop/src/ue_wrap/reflection.cpp:191-` — `FindObjectByClass`
  (CDO skip via `ToString(NameOf(obj))` — same hazard, also CLAUDE.md-
  flagged historical 120→60 FPS regression).
- `[[feedback-install-idempotent-o1-steady-state]]` — the call-site rule
  that papers over this hazard one caller at a time.
- `[[feedback-codebase-familiarity-before-new-install]]` — the rule that
  would have prevented the 2026-05-27 regression by forcing the new
  Install() to mirror sibling patterns.

## Companion observations from the 2026-05-27 audit miss

Two parallel audit agents both saw `FindClass` in the diff. Both
flagged ONE occurrence (the connect-edge replay's ~50k-alloc walk) and
moved on. Neither agent grepped for ALL `R::Find*` occurrences in the
file. The audit-prompt fix is described in
`[[feedback-audit-prompt-hot-path-reentry]]` — but a structural fix
to `FindClass` itself would remove the need for the audit to catch
this pattern at all.
