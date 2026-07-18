# net_pump Tick decomposition — design + as-built (2026-07-18 s23b)

> STATUS: BUILT (§5). net_pump.cpp 1237 -> 744; its 915-line Tick() decomposed into
> TWO concept TUs: coop/props/registry_reaper.cpp (401) + coop/player/puppet_drive.cpp
> (218). /qf pass: 6 rounds, "that holds" at R6 (thread continues the session's
> extraction discipline; the acceptance apparatus is the session_streams one).

## 1. The split (bodies verbatim; code-line-diff-proven)

| concept | new home | contents |
|---|---|---|
| prop-registry <-> world-lifecycle reconciler | `coop/props/registry_reaper.cpp`, `bool Tick(session)` | the ~4s scan block verbatim: dead-element reaper + escalation, purge-EPISODE detect + episode-end re-seed (+ key-index drain + save-identity re-bind), small-travel re-seed, steady-world high-water re-seed + DeliverLateRegisteredProps, host death-watch PropDestroy broadcast, and the gameplay->menu TRANSITION DETECTION (the world scan is this module's competency). `return true` = the menu guard fired -> net_pump aborts its Tick (the old inline `return`). `g_everInGameplayThisSession` moved in + `OnSessionStart()` reset hook. |
| per-slot puppet lifecycle + drive | `coop/player/puppet_drive.cpp` | owns the g_puppets array + pose-diag statics; `Puppet(slot)` (moved with its GT assert; 39 call sites swept, single-token verifier-proven), `DriveTick(session, displayOffsetX, worldReadyAnnounced)` = the old :1079-1218 block verbatim (pose spawn/apply incl. host self-slot-register, ragdoll drive, per-puppet interp Tick, wisp hold [it MOVES puppets], pose-diag emit), `bool DestroySlot(slot)` (= unconditional UnregisterPuppet + destroy-if-live; returns whether a live puppet died -- the disconnect edge logs on true, the teardown loop calls silently: log behavior byte-preserved). NO OnSessionStart (nothing puppet-side was ever reset -- an empty hook would be dead API), NO DestroyAll (the teardown loop composes DestroySlot + DisconnectSlot per slot, preserving the interleave, in net_pump). |

net_pump keeps (744): probes/HITCH, Flush, the connect/disconnect edges, the whole
world-ready ANNOUNCE AXIS (one owner: g_worldReadyAnnounced / g_reAnnounceWorldReady /
g_announcedWorld + the announce block), death policy + local-streams gate
(poseAuthoritative untouched), TickGameplay, event_feed tail — and THREE new publics:
- `IsFleeing()` — the flee latch read (the reaper's menu-guard predicate stays literal);
- `MaybeRequestReAnnounce(session, reapWorld)` — the old reaper-side maybeReAnnounce
  lambda BODY verbatim (role check, g_announcedWorld compare, ArmQuiesceProbe, the
  same-world log); the reaper's lambda now only forwards here;
- `FleeAfterNativeMenuTravel(session)` — the old inline TearDown+Flee(travel=false)
  pair behind the g_fleeing latch (detection reaper-side, action pump-side).

### Wrapper/substitution table (the ONLY non-verbatim lines)
1. net_pump: `if (registry_reaper::Tick(session)) return;` — exactly where the block sat
   (after Flush, before the edge loop); full-Tick abort preserved.
2. net_pump: `if (worldUp) { DriveTick(session, displayOffsetX, g_worldReadyAnnounced.load(...)); { PP::Scope; remote_prop::Tick; } }`
   — same worldUp local, remote_prop same-gate + post-drive (old order tick-loop -> wisp
   -> pose-diag -> remote_prop preserved; remote_prop is a PROP concern, stays pump-side).
   The load is IN the call expression: writers :510/:533 both run GT-earlier in the same
   Tick, none between -> observation-equivalent to the old per-iteration in-loop load.
3. reaper: `!g_fleeing` -> `!net_pump::IsFleeing()`; the TearDown+Flee pair ->
   `FleeAfterNativeMenuTravel`; `return` -> `return true`; maybeReAnnounce body -> one
   forward call; `:536 SetInPurgeEpisode(false)` verbatim reaper-side (detection
   bookkeeping, NOT a substitution).
4. drive: `if (isHost)` -> `if (session.role() == Host)` (one line); the in-loop atomic
   load -> the `worldReadyAnnounced` param; statics/aliases hoisted.
5. Caller sweep: 39 sites x single-token `net_pump::Puppet` -> `puppet_drive::Puppet`
   (+3 include inserts), verifier-asserted (zero unpaired, per-pair single-token).

## 2. /qf round map (6 rounds, fresh critic each)
R1 concept boundaries (detection-vs-action; wisp in / remote_prop out) + substitution
enumeration + flee idempotency + branch traversal. R2 :536 ownership, the full-Tick
abort row, the Puppet caller census (critic measured ~35), the observation point.
R3 announce one-owner, g_fleeing accessor, poseAuthoritative stays, teardown interleave
ownership (-> bool DestroySlot). R4 the worldUp brace row, OnSessionStart DROPPED (dead
API), the sweep verifier. R5 zero-unpaired assert, per-iteration load equivalence,
residual-vs-substitution overlap mapped. R6 "that holds".

## 3. Acceptance evidence (as-built, 2026-07-18 s23b)
- BASELINES on HEAD `1cd78d92` (DLL `667b49c26e11da07` x4): 2p 120s PASS — circles
  c4b0a701/2f57cabc -> cea1940d (client inject transient filled=2 = a join-window stale
  host row healed by canonical, both settle empty), cross=2, steady-world re-seed
  traversed 2x/peer; 4p PASS full CROSS-PEER matrix (scratchpad netpump_baseline_2p.txt
  + the streams_baseline_4p run on the same DLL).
- LITERAL DIFF (code lines; comment-only + brace-only stripped; substitutions excluded
  explicitly): reaper 99 / drive 58 / posediag 3+17 identical; KNOWN-POSITIVE: a
  ||->&& mutation in the death-watch eid guard flagged, exit 1.
- SWEEP VERIFIER: 39 pairs single-token-clean, 3 allowed include inserts, 0 unpaired.
- DECOMPOSITION (DLL `bc3dfe50bea249d1` -> final `ceb6cefeb0e18c3c` x4 after two MINOR
  hygiene fixes): 2p 120s PASS — digest circles EXACT frozen values both peers, cross=2
  == baseline, steady re-seed 2x/peer == baseline, pose-diag fresh=60/s trail=0,
  netPumpTick 0.19-0.34 ms/fr + reaper/puppets buckets alive, zero net/stream WARNs;
  4p PASS — verdict IDENTICAL to baseline (every client sees host + both peers via
  relay, 0 drops, 0 puppet fails). Final-DLL sanity smoke re-run after the hygiene
  fixes.
- AUDIT AGENT: PASS, 0 CRIT/IMPORTANT; the UNTRAVERSED menu-guard path traced line-by-
  line vs HEAD (ordering identical, no step lost/doubled); 2 MINOR hygiene items FIXED
  in-session (dead includes; stale g_puppets wording in a comment + the assert string).
- Honest residual (NOT dynamically exercised): the menu-guard flee, small-travel
  re-seed, death-watch broadcast, and maybeReAnnounce paths — evidence = the literal
  diff + the hand-verified table + the audit's line-by-line trace. NOT hands-on.
