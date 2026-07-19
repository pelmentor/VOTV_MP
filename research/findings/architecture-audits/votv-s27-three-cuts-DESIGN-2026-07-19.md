# s27 three cuts — kerfur_convert / autotest_vitals / harness under the cap (2026-07-19, AS-BUILT)

The "go next" queue (user order: kerfur, vitals, harness; autonomous tests batched at the END).
Three /qf design passes (5 + 4 + 7 rounds), the frozen-instrument equivalence recipe per cut.

## Cut 1 — kerfur_convert.cpp 1259 -> 633 (+client 395, +host 390)

/qf 5 rounds, "that holds" R5. Axis: the conversion feature's three lanes = DETECTION
(residual: Install + actionName interceptor + death-watch poll + both first-refusal seams),
CLIENT half (`kerfur_convert_client.{h,cpp}`: conversion-ghost custody Claim/Cleanup/
TakeParkedGhostByEid + CollectMirrorActors + the KerfurConvert wire apply MaterializeKerfurMirror/
OnKerfurConvert — fused per the R1 measurement that both Materialize branches consume Take),
HOST executor (`kerfur_convert_host.{h,cpp}`: FindNewFormKerfurActor/ExpressConversionFloppies/
ReleaseHostPropSilent/ConvergeAfterConversion/PickDropPropFn/OnConvertRequest + the request-verb
bracket with ActiveRequestVerbEid + the NEW one-way `RecordSeamConvergedInBracket` for the
residual destroy seam — predicate lives WITH the state, the s21b owner-API shape).

Key /qf finds (all measured):
- **Two-layer handoff** (R2-R3): `client::SetSession` per Install call (top, mirrors the
  per-call g_session.store); `client::SetClasses` + `host::SetClasses` EVERY attempt after the
  opportunistic resolve block (disabled-state claims/converges keep working exactly as the
  single-TU statics did); `host::SetVerbs` + the request latch ONLY at the success
  `g_installed.store` site — the gate flips at the identical instant.
- **ONE documented fail-closed deviation** (R2-R4): in the DISABLED install state
  (signature-changed verbs, :845 latch-off) the pre-cut OnConvertRequest gate PASSED a request
  — which would CallFunction a signature-changed verb over a zeroed 16-byte frame, the exact
  over-read the disable guard exists to prevent (a latent pre-existing bug). Post-cut the host
  latch stays down there -> requests DROP. Unreachable on the current game build (kismet-proven
  zero-param verbs). Mid-join (unresolved window) behavior is UNCHANGED (drops both before and
  after — principle 8 unaffected).
- Bracket safety: the seam record is conditional (impossible outside a bracket); single consume
  caller; no stale value can persist (same-GT-frame consume; OnDisconnect belt).

Evidence: `kerfur_body_diff_c2.py` PASS (18 spans verbatim; residual sequence-equal with
enumerated edits; exact-literal scaffold) + 5 mutate controls FAIL (incl. the gate-line
control); Release build+link clean per commit. Commits `bcd7b44b` (client) + `bd82c596` (host).

## Cut 2 — autotest_vitals.cpp 1013 -> ragdoll 373 + damage 238 + dmghazard 252 + playerdmg 158 + puppetframe 175

/qf 4 rounds, "that holds" R4 (the critic independently re-measured the partition). The s26
island recipe verbatim: commit 0 = the local ReadEnv retire alone (`ba317803`, the 89ce6602
twin); commit 1 = verbatim extraction of FOUR one-feature TUs (`247a7037`; three damage TUs
because grep proved ZERO shared code across the families — "damage" is a shared word, not a
shared subsystem); commit 2 = pure `git mv` -> autotest_ragdoll.cpp (`c43d8878`, 100% rename,
--follow intact; + `d7899730` doc-header fix). The s26 ledger flag (PuppetFrame rig is not a
vitals test) resolved: it owns autotest_puppetframe.cpp.

Key /qf finds: the island's measured convention is PER-TU wait helpers (RunGT in chippile:101,
a second WaitDone in ragdoll_spawn_probe:73) -> each new TU carries its own WaitDone copy, no
umbrella-header inline; negative grep island-wide with a known-positive control (29 WaitDone
hits) proved all family helpers file-local; the only external "InvokeAddPlayerDamage" hits are
a namespace-distinct ue_wrap::engine symbol (pre-existing benign shadow, kept under verbatim
discipline). Evidence: `vitals_body_diff.py` PASS (8 spans; residual sequence-equality;
enumerated include prunes) + 5 mutate controls FAIL; build clean per commit.

## Cut 3 — harness.cpp 1223 -> 526 (+session_runtime 709)

/qf 7 rounds (R6 caught the displayOffsetX chain; R7 items closed by measurement). Three commits:
- **A `8a9c509c`**: the nick-color ini-hex parse (22 lines) -> `coop::nick_color::
  SetInitialLocalFromIniHex` (the MODULARIZATION_PLAN Tier-C row; semantics verbatim).
- **B `e6f8576e`** (RULE 2): the netloopback scenario RETIRED — a stub since PR-2 2026-05-28
  (its own comment: GNS topology, one process cannot self-peer; the branch merely started a
  host, a worse duplicate of the play host path; the AUTONOMOUS_TESTING.md row described a
  capability REMOVED months ago — rule2-exempts-diagnostics protects working diagnostics, not
  stubs). Swept per ENUMERATE-CALL-SITES: the branch + storyBoot/wantGameplay name tests +
  3 in-file comments + docs rows (ROADMAP:280, tools/README:44, AUTONOMOUS_TESTING x3,
  net_pump.h:34) + **the displayOffsetX chain it alone fed nonzero** (net_pump::Tick param ->
  DriveTick param -> the one puppet_drive shift line; caught by the R6 ALIAS-vocabulary census
  — the name-grep was blind to it). A stale `scenario=netloopback` string lands in the loud
  final else. Closing negative grep on both vocabularies; lan-test.ps1 contrast-prose kept.
- **C `a48b21d8`**: `harness/session_runtime.{h,cpp}` — the coop-session LIFECYCLE DRIVER
  (owns g_session + per-session wiring + world boot + the 60 Hz RunPlayLoop + scenario-UX
  orchestration; ui:: touches are why it stays harness-side, not coop/session). harness.cpp
  keeps PROCESS boot (Start; 3 accessor sites take `&session_runtime::Session()`) + the
  scenario timeline. The pump-composite TEMPLATE moved verbatim-internal (possible ONLY
  because commit B killed its last external consumer — no GT::Task signature change, no extra
  std::function hop). 26 dead residual includes pruned by a measured census (only roster:: had
  a live use). Static-dtor order: no new hazard class (the audit-P2 teardown contract — the
  single Stop lives in DoShutdown pre-static-exit; nothing relies on cross-TU dtor order).

Evidence: `harness_body_diff.py` PASS (12 spans, 607 nb-lines; residual sequence-equal incl.
the enumerated prune) + 4 mutate controls FAIL (moved-span / residual-TimelineThread /
accessor-line / scaffold); build+link clean.

## Landings (live wc -l)

kerfur_convert 633 / _client 395 / _host 390; autotest_ragdoll 373 / _damage 238 /
_dmghazard 252 / _playerdmg 158 / _puppetframe 175; harness 526 / session_runtime 709.
All under the 800 cap. Proto 121 unchanged (no wire change).

## Batched differential verification (the user's "autonomous tests only at the end")

DLL points: pre-session `b62c6426...` (saved), post-vitals-commit-0 `e352122a...` (saved),
final `61f56942...` (post-`de304643`; the interim `fd07cfe6` pre-audit-fix build was superseded). Pairs: kerfurtoggle (pre-session vs final; the mp.py verdict block);
V1 ragdoll / V2 damage-flash / V3 dmghazard+playerdmg (one-writer-per-axis census: ragdoll +
flash contend on host position -> separate; hazard/relay health axes distinct) / V4 puppetshot
(gate-literal class) — post-commit-0 vs final, s_1234 restored byte-identically before every
run. The harness cut rides the boot+menu-join+RunPlayLoop path of EVERY pair. Smoke blind
spots documented: kerfur reject/restore, floppies, legacy search, non-initiator materialize;
netloopback/show/skin branches (link + literal-diff only).

RESULTS: **the batch was CANCELLED mid-run by the user** ("Оффай тесты") after ONE completed
run: kt.base = the FULL mp.py kerfurtoggle on the pre-session baseline bytes — verdict **PASS**
(toggled=2, turn_off=1, turn_on=1, claim_prop=1, claim_npc=1, prop_adopt(eid)=1, npc_adopt=1,
orphan=0, cascade=0, out_of_range=0). No post-side ran -> NO completed runtime differential.
All artifacts (batch.sh, gate_s27.sh, the three frozen DLLs b62c6426/e352122a/61f56942, the
s_1234 snapshot) are preserved in scratchpad/s27 — the batch is re-runnable verbatim later.
After the abort: FINAL bytes 61f56942 re-deployed + hash-verified x4 (HOST/CLIENT_1/CLIENT_2/
CLIENT_3), s_1234 restored byte-identically from the snapshot.

## Audits

Two background code-reviewer agents on 6d6a62c0..HEAD, both landed:
- **Correctness: PASS on all three areas**, zero behavioral findings >= 80. Verified: the
  two-layer handoff gate equivalence (host g_ready flips in lockstep with the residual
  success latch), the fail-closed DISABLED deviation "a strict improvement, not a regression",
  the bracket single-writer/single-reader pair, the OnDisconnect fanout == the pre-cut clear
  set, all caller migrations, teardown single-Stop preserved (same object via Session()).
  Two COSMETIC findings (conf 90/85): a stray 2-line comment fragment the vitals splitter's
  scaffold span dragged into the 4 non-ragdoll TUs, and 5 autotest.h doc comments still citing
  the dissolved autotest_vitals.cpp — both fixed in `de304643` (instrument re-run PASS).
- **Perf/hot-path: PASS, no CRITICAL** — the only new per-tick work is the SetSession/
  SetClasses handoffs (O(1) atomic/pointer stores, resolve work still stops at the latch);
  poll cross-TU calls are plain function calls; the pump template moved verbatim (no new
  std::function hop); displayOffsetX removal behavior-preserving (only netloopback fed it
  nonzero). File-size table: all ten landings exact + under cap. Finding (conf 90): the
  project's >800 residue line under-listed 6 live files — folded into the ledger-3 residue
  rewrite (remote_prop 1180, npc_sync 989, puppet.cpp 972, save_transfer 925, meadow_db_sync
  884, player_handshake 828 + chippile 877).

## Honest status

Behavior preservation is literal-diff + mutate-control + build proven per commit, plus two
clean audits. Runtime evidence: kt.base kerfurtoggle PASS (baseline side only — the batch was
user-cancelled; no completed baseline-vs-post differential ran). NOT hands-on — all s27
changes ride the standing take-4 runbook; deployed DLL 61f56942 x4, proto 121.
