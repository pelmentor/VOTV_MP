# Hands-on runbook — variant-1 force-churn verify (2026-06-27)

> **SUPERSEDED 2026-06-27 (15:24/15:44 runs).** The synthetic `force_save_churn` probe is MOOT: the GATE
> diagnostic proved the REAL engine mass-purge (15:25:42 / 15:44:41, ~2215 reaped) ALREADY unbinds the chips
> BEFORE the synthetic probe runs (boundLive=0) -- the real purge IS the churn. New approach: `force_save_churn=0`,
> let the real save-transfer-join purge churn the natives, and verify the reconcile (now non-one-shot + valve-abort-
> safe + post-purge-triggered) re-binds them. **The probe + GATE diag stay in the tree (gated, RULE-2-exempt).**
> The variant-1 verify is now folded into the refactor arc -- see the AS-BUILT ledger in
> `research/findings/architecture-audits/sync-consolidation-refactor-PLAN-2026-06-27.md` (step 4b+/7) and
> [[project-sync-module-refactor-2026-06-27]]. 15:44 result: fix #1 (reconcile-on-valve-abort) VERIFIED in the
> log; world ended CLEAN (870 piles, 0 orphans); variant-1 N=0 = no churn left anything unbound = a pass.

GOAL (original, for reference): verify **variant-1** (`BindUnboundReCreatesByPosition`) re-binds GC-churned
save-natives by the host-wire save-position. The synthetic probe below cannot fire (the real purge pre-empts it).

Deployed DLL: build `425633d7` (the sync-refactor arc; D1+D2 closed, force-churn probe). Run
`tools/deploy-all.ps1` first; hash-verify host+client DLLs == `build\votv-coop\Release\votv-coop.dll`.

## [dev] ini — set on BOTH peers (host + client) in votv-coop.ini `[dev]`
```
save_identity_bind = 1      # the bind layer + variant-1 logging (already on for this arc)
save_identity_map_log = 1   # host builds + ships the sidecar v2 (eid->savePos)
force_save_churn = 1        # THE probe: unbind 3 bound save-natives right before the sweep (CLIENT side)
```
(force_save_churn only acts on the CLIENT -- it needs the received sidecar + armed bind. Harmless on the host.)

## Steps (a normal join is enough -- no special grab/move needed)
1. HOST: launch `mp_host_game.bat`, load a recent fresh save with chipPiles (the standard 6-kerfur/2-pile
   slot is fine -- ANY save with >=3 chipPiles works; the probe churns 3 of them).
2. CLIENT: launch `mp_client_connect.bat`, join. Let the world finish loading (the divergence sweep fires at
   load-tail quiescence, ~10-25 s after join).
3. That's it. Stand still; the probe + variant-1 run automatically at the quiescence sweep. No interaction.
4. Quit both. Tell Claude "logs ready" -- Claude greps (you don't read logs).

## What Claude verifies in the CLIENT log (the PASS criteria)
- `[force_save_churn] UNBOUND chip eid=... native=... @save-pos=(...)` x3 -- the synthetic churn fired (3
  natives unbound right before the sweep).
- `save_identity_bind: RE-BIND by position -- ... native=... -> host eid=... @save-pos=(...)` **x3** -- variant-1
  re-bound the 3 churned natives BY POSITION. The eid in each RE-BIND line must MATCH the eid in the
  corresponding UNBOUND line at the same save-pos (right native -> right eid, no cross-bind).
- `position re-bind pass -- 3 sparse GC-churned native(s) re-bound` -- the variant-1 summary, N=3.
- NO black/foreign pile, no dup, no ghost at those 3 positions (the re-bind restored them in place).
- Bulk (the other ~867 piles) untouched -- count stable, no mass churn.

PASS = 3 churned -> 3 RE-BIND by position, each eid matching its save-pos (deterministic correct re-bind).
FAIL = fewer RE-BIND than churned, OR an eid bound to the wrong save-pos (a cross-bind = a mis-match).

## After the verify
Set `force_save_churn = 0` again (it's a probe, not shipping behavior). The probe stays in the tree
(RULE-2-exempts-probes -- gated, reusable). On PASS, variant-1 is verified for its purpose.
