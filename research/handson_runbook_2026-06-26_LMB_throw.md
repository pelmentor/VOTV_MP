# Hands-on runbook — LMB hard-throw (native camera-driven throw for client)

**Date:** 2026-06-26
**Deployed DLL:** SHA256 head `B4AA1A373E93` — hash-verified MATCH on HOST + CLIENT + DEV.
**Build:** clean (Release). **Audit:** SHIP (correctness/wire/perf/thread/regression all clean).
**push:** HELD. **Bind:** 874/874 unchanged. **#3 (E-drop):** closed + untouched.

## What changed (Variant B — native fidelity)
The throw is a SEPARATE native input from E. E = grab + **E-drop** (soft release, host derives velocity
from puppet hand motion, capped 650 cm/s — this is #3, works). LMB = the **native throw**: a camera-DIRECTED
launch at the native mass-scaled speed. The client held only a proxy so native LMB did nothing; now the
client routes LMB→a `hardThrow` intent carrying its instantaneous camera-forward, and the **host applies the
exact native formula** with the real clump mass:

    velocity = cameraForward * (15000 / max(clumpMass, 10)) + puppetVelocity     [NO cap]

(RE-pinned from `throwHoldingProp → traceThrow → throwShit` bytecode.) So a light clump flies ~1500 cm/s
straight where you look — much harder than the E-drop, exactly like single-player.

## What to do
1. Launch `mp_host_game.bat` (host) — your usual pile save. Launch `mp_client_connect.bat` (client); let it join (~10s).
2. On the **client**: aim at a chipPile, press **E** to grab it (it becomes a clump in hand).
3. Aim where you want to throw, press **LMB** (left mouse) — it should **fly forward fast** toward your aim, then re-pile where it lands.
4. Repeat: grab another pile (E), this time press **E** again instead of LMB → it should **drop soft** (the E-drop, gentle). This confirms the two mechanics coexist and differ.
5. Try throwing while running / looking up / looking down — it should fly where the camera points.

## Acceptance — checks (read both logs)
- CLIENT: `Game_0.9.0n_copy\...\votv-coop.log`   HOST: `Game_0.9.0n\...\votv-coop.log`

| # | What | Where | PASS = |
|---|------|-------|--------|
| 0 | Observer installed | CLIENT log, at join | `LMB hard-throw observer installed on N/2 InpActEvt_fire handler(s)` with **N>=1** (if 0/2, the fire UFunction names are wrong — tell me) |
| 1 | LMB routes a hard-throw | CLIENT log, on LMB while carrying | `[THROW-INTENT] CLIENT LMB(fire) while carrying eid=<N> -> requesting NATIVE hard-throw ... (camFwd=...)` |
| 2 | Sent as hardThrow | CLIENT log | `CLIENT SENT eid=<N> mode=hardThrow(LMB)` |
| 3 | Host applies native velocity | HOST log | `RECEIVED eid=<N> ... mode=1` then `THROW-INTENT SUCCESS ... mode=hardThrow(LMB) ... vel=(...)` — the vel magnitude should be STRONG (hundreds–~1500 cm/s, camera-directed), clearly bigger than an E-drop |
| 4 | E-drop still works | both logs | E-while-carrying still logs `requesting release(E-drop)` + `mode=release(E)`, soft (capped 650) |
| 5 | Visual (your eyes) | game | LMB = pile flies forward where you aim + lands + re-piles; E = soft drop. Two distinct feels. |

If 0–3 hold and the throw flies where you look at native-ish speed → **LMB-throw verified**.

## FAIL signatures
- `installed on 0/2 ... handler(s)` → fire UFunction name mismatch (the `_58/_59` ordinals; tell me, I'll re-resolve).
- LMB still does nothing + no `CLIENT LMB(fire)` line → the fire observer isn't firing (wrong handler / not the press edge).
- Throw too weak/soft (like E-drop) → velocity/mass issue (tell me the host `vel=(...)` numbers).
- Throw flies wrong direction (not where you look) → camFwd/aim issue (this is exactly why we chose Variant B = instantaneous client camera; report it).

## Honest note
Autonomous smoke is rendering/interaction-blind, so this (throw feel + direction + the native-fire-no-op
assumption) is your hands-on call. The velocity formula + wiring are RE-pinned + audited SHIP, but
"feels like base" is an eyeball check only you can make.
