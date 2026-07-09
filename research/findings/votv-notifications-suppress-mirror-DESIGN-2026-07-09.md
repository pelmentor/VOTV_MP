# Notifications (toast/email/console) suppress+mirror — DESIGN /qf convergence 2026-07-09

STATUS: **DESIGN DIRECTION converged (ROOT), BUILD BLOCKED on 2 gates.** RE docs: `docs/notifications/`.
Not built. `/qf` design pass (2 rounds → converged on the verdict below; thread in scratchpad).

## The premise CORRECTION (why this is bigger than "suppress a toast")
The user asked to "suppress the client's false `SERVER "X" is down` toast + mirror the host's." The RE
proved that notice is **NOT a toast** — it is an **email** (`panel_SATconsole.Server Alert → addEmail`) +
a **console** `writeToLog` line + the **serverDown alarm**. And it is the tip of a **host-authoritative
server SIMULATION** (`ticker_serverBreaker` break/fix, `serverEfficiency`, per-`serverBox` state), none of
which is UE-replicated. So the real feature is **server-state sync**, not toast suppression.

## The /qf verdict (converged)
- **ROOT beats NARROW (RULE-1).** NARROW = per-channel suppress the client's self-authored notices +
  mirror the host's. ROOT = the **HOST owns server state; clients MIRROR + render and do NOT run the
  server sim** (neutralize the client `ticker_serverBreaker`). Under ROOT the client never fires
  `serverBroke` locally → never self-authors → **the entire suppress mechanism (poll-remove
  `ui_hints_C.verT_hinTs` / a `ProcessLocalScriptFunction` hook / hide-widget) is dead code** — a crutch
  NARROW only needs because it leaves the client running the sim.
- **Invariant, not a site-list:** "the client never runs the host-world server sim." NARROW leaves the
  SAT-console query lane (`sv.ping`/`sv.check`/`tw.check`…) reading the diverged local `servers[]` →
  permanently wrong; ROOT fixes it by construction. Counting that lane, ROOT is the *smaller* net blast
  radius.
- **State AND edge (likely two lanes, refuse-unification):** the server *condition* is STATE (poll
  `brokenServers` + per-serverBox struct — alarm_sync shape); the *down/damaged notice* is an EDGE
  (one-shot email/console/alarm author — event_fire shape). Probably a state-mirror lane + an
  edge-forward lane, not one.

## THE 2 GATES (before ANY build — do NOT skip, /qf-confirmed)
1. **GATE-1 — the disasm AUTHORING CENSUS (a measurement, NOT YET RUN).** serverBox / panel_SATconsole /
   ui_console / ticker_serverBreaker are **absent from the bytecode dataset** (`research/pak_re/cfg/` has
   only mainGamemode/daynightCycle/directionalWind). Disassemble them (bp_reflect.py) to MEASURE, not infer:
   - Is `ticker_serverBreaker` the **SOLE** break/fix author, or do console-use / drone / damage / recovery
     ALSO author a server notice on the client? (If they do, neutralizing the breaker is insufficient —
     ROOT's key claim fails and a residual suppressor returns.)
   - Does writing the client's REAL `serverBox.{isBroken,upgrades,health}` **re-enter** the break/recovery
     sim (delegates, minigame spawn, `calcServerEff`)? → decides **drive-real-serverBox vs read-only-shadow**.
     (The alarm lane earned poll-a-bool only because `runTrigger` was first RE'd as idempotent; the
     equivalent property for server state is unmeasured.)
   - Does the down/damaged notice fire off the STATE or off the `serverBroke` delegate EDGE? → decides one
     lane vs two.
   - The exact channel per notice (which of serverBox's 4 addHint sites vs the Server Alert email/console/
     alarm fires on `active_downl`) + the exact localized string.
2. **GATE-2 — user COOP_SCOPE ratification.** Server-state sync is **not in `docs/COOP_SCOPE.md`** today —
   it's a NEW lane (the signal-server sim is core VOTV gameplay, unreplicated). ROOT is the right shape but
   it materially exceeds "suppress a toast." Needs the user's scope decision + a COOP_SCOPE amendment
   before the build investment.

## Valid Inc-1 (once the gates clear) — must exercise the RISKY half
NOT "mirror brokenServers + one isBroken so sv.ping reads true" (that proves only the cheap query half —
the F2-dupe trap of mapping one half). A valid first slice mirrors ONE server's break EDGE end-to-end: host
breaks a server → client's console/email/alarm shows the TRUE notice (mirrored, not self-authored) AND its
`sv.check` reads the true health — proving the state lane + the edge lane + the no-self-author invariant
together. Exact shape is designed AFTER the census (a MECHANISM /qf on the measured facts).

## Next steps (in order)
1. Run GATE-1 (the disasm census) — read-only RE, ready to do.
2. Present GATE-2 (scope) to the user — done in this session's handoff.
3. MECHANISM /qf on the measured facts → the valid Inc-1 → build → implementation /qf.

Related: `docs/notifications/`, `docs/COOP_DISPATCH_VISIBILITY.md:126` (addHint invisibility),
`src/votv-coop/src/coop/world/alarm_sync.*` (state-poll lane precedent),
`coop/world/event_fire_sync` (edge-forward lane precedent), `reference/mtasa-blue/` (host-auth world state).
