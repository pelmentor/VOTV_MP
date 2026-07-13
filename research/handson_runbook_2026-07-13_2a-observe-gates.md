# Hands-on runbook — 2a-OBSERVE gates (VM-dispatch substrate, kerfur form-flip)

**Date:** 2026-07-13 (nite) · **Take:** 2a-observe (increment 1 + gate instrumentation)
**Deployed DLL SHA256:** `1802B5A3A0635FB3681679540543E99837A4192D54BE6C0D0238B01620AAE5EF`
(byte-identical on HOST, CLIENT_1, CLIENT_2, DEV — verified)
**Wire protocol:** UNCHANGED — this is observe-only; no packets added, no behavior changed.
The kerfur conversion verbs run entirely unmodified. NO capture, NO repoint, NO converge.
**Flags:** `vm_dispatch_log=1` set in HOST + CLIENT_1 inis (`gnatives_probe=0`, the retired probe).
**Committed?** the DLL is built from the 2a-observe instrumentation edit to
`kerfur_form_assembler.cpp` (commit pending / see git state below).

---

## WHY this run exists

The converged 2a design (repoint-at-birth, `docs/COOP_VM_DISPATCH_PLAN.md` §3) rests on four
facts that increment 1 did NOT yet measure. This run measures all four on ONE deliberate hands-on
pass, then HALTS before any capture code is written. GREEN on all four = the capture code is safe
to build; any RED = the design halts and is re-examined.

Everything mechanical is already done (build, deploy, flags). Your part is ONLY the in-game
actions below. After the run, I read the log; you don't have to interpret it.

---

## THE FOUR GATES (what each in-game action proves)

| Gate | In-game action that exercises it | GREEN means |
|------|----------------------------------|-------------|
| **1** one-capture-per-A-eid | Toggle a kerfur OFF and ON — on BOTH host and client | every verb-entry Context A is registry-bound (`eidUnbound=0`), and no same-eid re-entry (`reentrySameEid=0`) |
| **2** floppy class-separation | Turn OFF a kerfur **that has a floppy disc inserted** | the floppy is counted as `floppyIn`, NOT mis-attributed as a form successor (`formIn` counts only the prop/NPC body) |
| **3b** per-bracket seam ORDER (load-bearing) | any toggle (same actions) | B's form-spawn fires BEFORE A's self-destroy inside one bracket (`spawn<destroy` > 0, `DESTROY_NO_SPAWN=0`) |
| **3c** B-index live at capture | any toggle (same actions) | the successor B is a live, index-assigned object at spawn (`bIndex live` > 0, `DEAD=0`) |

The **client's own local verb-death husk** falls out of the same counters under the `[CLIENT]`
role tag (SetEnabled is both roles). The **pending-eid pose-tick misroute** is NOT observable in
observe-only (there is no pending flag yet) — it is deferred to 2a-capture's own verification and
is NOT part of this run's GREEN criteria.

---

## PRECONDITIONS

1. No VOTV instance running. (If one is, close it first.)
2. Use the **HOST** copy and the **CLIENT_1** copy (both have `vm_dispatch_log=1`).
3. **Fresh save — New Game** on the host (never a stale 2023-2024 slot).
4. Have at least **one kerfur** and **one floppy disc** available in the world for the actions
   below. (Spawn a kerfur via the usual means if the fresh world has none; a floppy disc is the
   item you insert into a kerfur.)

---

## STEPS (do them deliberately, one at a time — this is a COUNTED run)

**A. Host, both openers**
1. Host: turn a kerfur **OFF** (it converts NPC -> prop). Wait ~2s.
2. Host: turn that kerfur **ON** (prop -> NPC). Wait ~2s.

**B. Client, both openers** (so both roles are measured)
3. Client: turn a kerfur **OFF**. Wait ~2s.
4. Client: turn that kerfur **ON**. Wait ~2s.

**C. Floppy-carrying toggle (gate 2)**
5. Insert a **floppy disc** into a kerfur (so `hasFloppy` is set on it).
6. Turn that kerfur **OFF** — the conversion should spawn the floppy alongside the prop body.
   Do this on the host once, and (if convenient) on the client once.

**D. Contested toggle (gate 1 stress — optional but valuable)**
7. If you can, have host and client each toggle the SAME kerfur near-simultaneously (or in quick
   succession). This is the two-openers-race the dedup must survive; it stresses `reentrySameEid`.

Keep a rough tally of how many OFF and how many ON toggles you did on each peer — I cross-check it
against `catch{off,on}` so a missed catch (a coverage hole) is caught.

**E. End the session cleanly** — disconnect the client, then quit both. The disconnect is what
dumps the two SUMMARY lines (the counters are also live in the log per-catch, but the summary is
the verdict line).

---

## WHAT I READ AFTER (you can just hand me the logs)

Both peer logs are at:
- HOST:    `Game_0.9.0n_HOST\WindowsNoEditor\VotV\Saved\Logs\votv-coop.log`
- CLIENT_1:`Game_0.9.0n_CLIENT_1\WindowsNoEditor\VotV\Saved\Logs\votv-coop.log`

(NOTE: `votv-coop.log` is TRUNCATED at each boot — copy both logs to the scratchpad BEFORE
relaunching anything, or just leave the games closed and tell me; I'll copy them.)

The verdict lines (one per role, at disconnect):
```
[kerfur_asm][HOST] 2a-OBSERVE GATES (session-end): g1_entry{bound=.. UNBOUND=.. reentrySameEid=..}
    g3b_order{spawn<destroy=.. DESTROY_NO_SPAWN=..} g3c_Bindex{live=.. DEAD=..}
    -- GREEN iff UNBOUND=0 & reentrySameEid=0 & DESTROY_NO_SPAWN=0 & DEAD=0
[kerfur_asm][HOST] CONTAINMENT SUMMARY (session-end): catch{off=.. on=..}
    spawn{formIn=.. formOut=.. floppyIn=..} destroy{selfIn=.. otherIn=.. kerfurOut=..}
```
plus the same two lines role-tagged `[CLIENT]`.

**GREEN (capture code is safe to build):** on BOTH roles — `UNBOUND=0`, `reentrySameEid=0`,
`DESTROY_NO_SPAWN=0`, `DEAD=0`; `formIn` == your toggle count, `formOut=0`, `otherIn=0`,
`kerfurOut=0`; and `floppyIn>0` from step 6 (turning the null result into a real pass).

**RED (HALT — design re-examined, no capture code):** any `UNBOUND>0` (A had no eid to repoint),
any `DESTROY_NO_SPAWN>0` (self-destroy raced ahead of the spawn — the eid would strand), any
`DEAD>0` (B not real at capture), any `formOut>0` / `kerfurOut>0` (a conversion escaped the
bracket — a coverage hole), or `catch` counts short of your tally.

---

## AFTER THE RUN

- I revert both inis to `vm_dispatch_log=0` (ship-silent).
- If GREEN: proceed to **2a-capture** (repoint-at-birth, split `BindFormActor`, transition flag).
- If RED: the plan halts at §3; I root-cause the specific gate before any capture code.

## Honest status of what this run does / does not prove

- **Proves (if GREEN):** the four measured premises the repoint-at-birth design is built on, on
  real toggles, both roles.
- **Does NOT prove:** the pending-eid pose-tick misroute (no pending state exists yet — deferred to
  2a-capture), and the client wire-path suppression retirement (that end-to-end trace is owed on
  the capture increment, not here). Neither is a blocker for building capture; both are on the
  2a-capture verification list in the plan.
