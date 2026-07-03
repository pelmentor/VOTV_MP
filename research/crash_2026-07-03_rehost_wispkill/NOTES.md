# Crash 2026-07-03 12:53 — re-host after wisp-killer death (FOR FUTURE STUDY, user-filed)

**User sequence:** host killed in-game by wisp killer -> BOTH host and client dropped to main
menu (SP death flow) -> host pressed Host Game, picked save '1234' (coop slot 'zcoop_12196')
-> fatal crash during world load.

**Crash:** EXCEPTION_ACCESS_VIOLATION reading 0x0000000000000350 (null-base + 0x350 field),
`Crash in runnable thread TaskGraphThreadNP 0` — an async-loading / task-graph worker, NOT the
game thread. Stack all in VotV-Win64-Shipping.exe (no symbol; addrs 0x7ff60424d02d,
0x7ff60423ae0f, 0x7ff60423d8a7, 0x7ff60405d0e1, 0x7ff60405eebc, 0x7ff6041acd8b, 0x7ff6041a7121
— module-relative RVAs need the session base from the minidump; tools/maprva.py).

**Host coop log ends at:** `12:53:47 engine: LoadStorySave` (slot 'zcoop_12196') — the very
start of the second-session world load. DLL was `FCBC75823B023A22` (proto v95).

**Context that makes this interesting:** this is the SECOND session in one process — full world
teardown (death -> menu) then re-host. Known-fragile seam: any of our subsystems holding
world-lifetime pointers across the teardown, or a hook touching an object mid-async-load.
Wisp-killer death path additionally ran right before teardown (wisp_attack_sync involved?).

**Artifacts here:**
- `UE4CC-Windows-F5AE656C4BE65DC7D6DC90A59E1711F8_0000/` — 12:53 host crash (CrashContext.runtime-xml + UE4Minidump.dmp)
- `UE4CC-Windows-9EB66147407AF73408943DBF3001A28C_0000/` — 12:51 dump (earlier; possibly the client or the death itself)
- `host_votv-coop.log` (+ `.prev` = the session BEFORE, with the wisp death), `client_votv-coop.log`

**Next when picked up:** open UE4Minidump.dmp (WinDbg/`!analyze`), map the faulting RVA in IDA,
diff against our hook sites; grep host_votv-coop.log.prev for the death/teardown sequence
(wisp kill ~12:5x) + check which subsystems re-Install on second StartCoopSession.
