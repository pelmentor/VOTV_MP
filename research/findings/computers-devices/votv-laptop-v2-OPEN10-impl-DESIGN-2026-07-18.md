# OPEN-10 laptop v2 — buffer quad + portable PC lid + floppyBox: impl design (2026-07-18)

> STATUS: DESIGN (converged /qf 11/15, critic "that holds"). Thread archived in the session
> scratchpad (`qf_thread_OPEN10-laptop-v2.md`); the round map is §7. Builds on
> `votv-laptop-pc-RE-2026-07-17.md` (RE base) + the v116 as-built `laptop_sync.cpp`.
> Confidence: [M] measured this pass (bytecode/code cite) / [P] shipped precedent.

## §0 Scope corrections vs TRACKER OPEN-10

- **MISATTRIBUTION RETIRED**: `floppyTypes/floppyData` belong to `prop_floppyBox_C`
  (prop_floppyBox.hpp:14-15), NOT `prop_portablePc_C`. The portable PC has NO floppy fields. [M]
- **Portable PC = remote terminal to the base laptop**: uber@15 `bindPC(gamemode.laptop.laptop)`;
  `usePC` = Enter Interface(the SHARED ui_laptop widget). Its screen state converges FOR FREE via
  v116 (launchedUpdate bound to laptop.pcLaunched, each peer's own delegate). Its ONLY unsynced
  axis = the LID `opened` @0x398 (runtime-only, not persisted). [M]
- **The TRACKER "claim discrimination" question DISSOLVES**: one shared widget per peer process,
  one underlying laptop actor -> the shared "laptop" claim key is CORRECT. [M device_screen.cpp:62-67]
- The claim deny/grant path re-verified end-to-end for the portable (device-class deny gate +
  activeInterface widget-edge claim). [M device_occupancy.cpp]

## §1 Measured fact base (the load-bearing set)

1. Buffer-quad mutation census (ALL widget-side, PE-INVISIBLE): erase = floppyData.Remove(index)+
   updFloppy [ui_floppyDatablock uber@347-441]; addFloppyBuffer = rw--, bufferSlots+=widget,
   floppyBufferUIDs+=uid, floppyData.Remove(index), updFloppy [@70-747]; transferBuffer = rw--,
   floppyData+=bufferSlot.data, updFloppy (buffer row KEPT) [@563-815]; removeBuffer =
   bufferSlots/floppyBufferUIDs/floppyBuffer.Remove(bid), NO updFloppy [@166-383]. decode-image =
   local texture only. [M]
2. `updFloppy` REGENERATES laptop.floppyBuffer FROM widget bufferSlots (clear@1700 + re-add@2007),
   clamps floppyData to 31 (@851), re-mints DISC-view uids (floppyUIDs) but NOT floppyBufferUIDs
   (writer census: addFloppyBuffer@490 + removeBuffer@311 only). genFloppyBuffer (only caller:
   laptop loadData@1755) builds bufferSlots from floppyBuffer + floppyBufferUIDs[i] (@387 operand);
   NOTHING native ever clears bufferSlots. [M]
3. Wire slot applies call updFloppy: ue_wrap WriteSlot/WriteSlotScalars/ClearSlot all end in
   CallWidgetUpdFloppy (laptop.cpp:257/271/285) -> a receiver with stale bufferSlots stomps
   wire-applied floppyBuffer. WriteSlot DOES write floppyData (WriteFStringArrayField). [M]
4. rw writer census CLOSED: insert copy (laptop uber@6411), loadData@1354, addFloppyBuffer@161,
   transferBuffer@654. No rw-only writer; rw is MONOTONE-DECREASING between inserts. [M]
5. Native order grammar: floppyData/floppyBuffer have NO move verb — only removeAt(index) +
   append-at-END. Every verb changes at least one of (fdNum, fbNum, uidNum, rw). [M census]
6. floppyBox: LIFO cap 15; addFloppy = arrays += (type, actorDataToString(disc)), caller destroys
   the held disc (uber@504->@581); getFloppy = LastIndex spawn AT PLAYER + loadData + Hold Object
   (into the hand), arrays.Remove(ind). Aprop_floppyBox_C : Aprop_C (hpp:4) — inside the prop
   universe (prop.cpp:37 lineage predicate), save-keyed, not a ChildActor. [M]
7. Session layer: SendReliableToSlot host-stamps header senderSlot (session.h:270); clients route
   by it (session.cpp:506-514) and the inbox stamps senderPeerSlot (795-802). Send-side: every
   reliable stamps m_idxLane = LaneForKind(kind) (session.cpp:292/315); GNS orders WITHIN a lane
   across kinds. Proto mismatch rejects at handshake (session.cpp:432-441). [M]
8. blob_chunks: Assembler keys (senderSlot, blobSeq); C-1 mismatch branch erases stale and
   restarts on chunkIdx==0 (blob_chunks.cpp:79-95); I-3 all-or-nothing sends. Doctrine: each
   module owns one Assembler per kind. [M]
9. v119 precedents adopted: SweepRacks first-sight silent prime + client ops vs shadow kept at
   SENT state + host organic -> canonical (drive_sync.cpp:463-528); drain-before-adopt (:674);
   taken-ring content correlation (:511-515); Registry::SnapshotActorsByType + IsLiveByIndex +
   class gate eid addressing (:162-172). [P/M]

## §2 Sub-lane A — the laptop buffer quad (NEW file `coop/interactables/laptop_buffer_sync.cpp`)

State: {floppyData[], floppyBuffer[], floppyBufferUIDs[], floppyReadwrites} on the ONE laptop.
Authority: HOST-CANONICAL + client value-ops (SET-state lesson; the v119 rack shape).

- **Detector** (4 Hz, GT): int pre-filter (fdNum, fbNum, uidNum, rw) — every native verb is
  int-visible (fact 5: rw monotone + nums strictly move; no combo nets all to zero); full
  SEQUENCE-hash of the four arrays only on int change. floppyType cached: on ANY slot-type change
  prime-and-skip (slot machinery owns slot transitions — v116 op=1/2 transport floppyData, fact 3
  WriteSlot). NO claim gating (change-edge = authority; the v116 eaten-edge lesson).
- **Client ops**: edit script under the native grammar: {removeAt(arrayId, idx, hash)} /
  {appendTail(arrayId, uid, STRING)} + rwDelta, derived prev->cur (cur = order-preserved
  subsequence of prev + appended tail; greedy on equal strings; derivation miss -> wholesale
  RESYNC REQUEST to host = ask for canonical, no local guess). ONE poll tick = ONE self-contained
  batch blob on **LaptopQuad** -> host only. Shadow kept at SENT state.
- **Host**: applies the batch index-anchored (hash-check at idx; mismatch -> content-search
  fallback; still-miss -> SKIP, errs toward KEEPING data), then **UNCONDITIONAL post-batch
  canonical** (the canonical IS the ack — an all-skip batch heals the author; R10-1). Host
  ORGANIC quad change (host is the UI driver) -> canonical on-change (no host derivation exists —
  structurally no derivation bug on the authority).
- **Canonical**: full quad blob on LaptopQuad, host-authored; receivers accept senderSlot==0
  ONLY. Adopt = drain-before-adopt (ship own pending diff as a batch FIRST), then if canonical
  hash == local hash -> prime-only (skip-rebuild-on-equal, kills the echo flash); else raw-write
  4 fields + **eager widget rebuild** + prime.
- **Widget rebuild** (the ONLY correct apply shape, facts 2/3): per-row teardown = native
  removeBuffer semantics (RemoveFromParent each bufferSlots row + array clear), then native
  loadData recipe (genFloppyBuffer + updFloppy). Invariant (stated as property, checked by audit):
  *every wire-driven laptop-state write path terminates in PrimeBaselines() before returning to
  the pump* — laptop_sync::PrimeBaselines() piggybacks laptop_buffer_sync::PrimeQuadBaseline().
- **Join**: canonical quad to the joiner in QueueConnectBroadcastForSlot (after op=3 + slot
  content), via SendBlobToSlot. Client sends gate on net_pump::HasAnnouncedWorldReady; first
  sight primes silently. STATE lane -> no seed-delta needed.
- **Selftest** `[dev] laptop_selftest`: client-inject phase (raw-write + rebuild -> the POLL'S
  OWN derivation must ship it -> host -> canonical -> adopt) + host-inject phase (exercises the
  client FULL canonical-rebuild path). Digest = sequence-hash over the 4 arrays PLUS the widget
  mirror (bufferSlots.Num + per-row 'data' prop hash) — a stale widget FAILS loudly. 0->1->0,
  digests logged per peer, smoke compares.

## §3 Sub-lane B — portable PC lid (in `laptop_sync.cpp`)

- op=6 on LaptopState {op, eid, isOpened} — ZERO struct growth (fields exist; protocol.h:3328). [M]
- 1 Hz element-snapshot walk (Registry SnapshotActorsByType(Prop) + IsLiveByIndex + POINTER
  class gate on Aprop_portablePc_C), per-eid prev map, change edge -> idempotent any-peer line
  (DriveSlotState shape). Apply: gate current!=wire, reflected `Open(opened)` (native re-applier:
  opened:=arg + collision + TL, uber@1801/@526), prime prev[eid] immediately (synchronous set —
  no latent window). Unresolvable eid -> stash {eid->opened} TTL 30 s (g_pendingDisc shape;
  PropSpawn rides another kind, cross-lane skew is real). Join: lid rows in QueueConnect.
  Host refans op=6 (manual SendOut, origin byte).

## §4 Sub-lane C — floppyBox (NEW file `coop/interactables/floppybox_sync.cpp`)

- **FloppyBoxState** kind, blob_chunks ENTIRELY (RackState shape verbatim): head {op, eid} inside
  the blob; op 0=push{type,dataString} / 1=pop{contentHash} / 2=deny / 3=canonical{types[],data[]}.
  KB-class dataString rides blobs — no fixed-line content (wire-caps lesson).
- 1 Hz walk (same shape as B); per-eid shadow (types+data); first-sight silent prime. Client
  derives push/pop vs shadow (LIFO tail ops), sends to host, shadow at SENT. Host: pop =
  tail-search by content hash (miss -> DENY + canonical); push = append (cap 15 enforced
  native-side); after every applied/denied op -> canonical for that eid; host organic -> canonical
  on-change. Clients adopt canonical (senderSlot==0) with drain-before-adopt; apply = raw-write
  arrays + reflected `gen()` + prime.
- **Pop DENY -> reap-if-alive, SKIP-if-consumed**: the author correlates its just-spawned in-hand
  disc via the adoption-eid binding + taken-ring content hash; if the disc actor still lives ->
  K2_DestroyActor (native-legal on held actors — insert does it); if already consumed (inserted)
  -> skip + INFO (content duplication is native-legal — transferBuffer copies; no PROP dupe
  survives). Window <1 RTT vs seconds-long human actions.
- Disc actors themselves cross on EXISTING lanes (held-disc destroy = prop destroy seam;
  getFloppy spawn = birth channels + hand-item sync). Box arrays are the only new wire state.
- Join: canonical per box at ready edge. Stash unresolved-eid canonicals (v119 pending shape).

## §5 Wire + migration (RULE 2)

- **Kinds**: LaptopState (SHRUNK: ops 0/1/2/3 + 6; chunk fields chunkSeq/Total/contentLen/
  contentKind/content[192] DIE -> ~16 B struct, asserts updated), **LaptopBlob** (slot/disc
  content, head byte {0 slot, 1 disc}, owner laptop_sync), **LaptopQuad** (op batches client->host
  + canonicals host->clients, owner laptop_buffer_sync), **FloppyBoxState** (owner floppybox_sync).
  Each owner: own Assembler + ONE monotonic seq counter (broadcast + to-slot share it).
- **All four pinned Lane::Normal** -> ONE FIFO with LaptopState (GNS in-order within a lane,
  measured) -> the v116 op=1-park/content-pairing contract survives; op=3-then-content ordering
  to the joiner holds on the SendReliableToSlot path too (same lane stamp, session.cpp:292/315).
- **Relay**: none of the new kinds relay-whitelisted. laptop_sync refans LaptopBlob per-chunk
  verbatim (origin byte); LaptopQuad/FloppyBoxState never refan (client->host consumed;
  host->clients authored). Client-forged canonicals die at the receiver senderSlot==0 check AND
  at the non-relay.
- **The v116 op=4 chunker RETIRES** (g_assembly + the hand-rolled chunk loops at laptop_sync.cpp:
  162-189, 462-488, 513-560 — all three consumers enumerated, incl. QueueConnectBroadcastForSlot)
  -> blob_chunks. kProtocolVersion 120 -> **121** (one bump covers all wire changes; handshake
  reject measured).
- THREE-places checklist per new kind: protocol.h + session_lanes.h (pin; whitelist untouched) +
  event_dispatch router.

## §6 SUPERSEDED IN-THREAD WORDING (do not build these)

- "op=5 quad edge line" (v3) — DEAD; the batch is a self-contained LaptopQuad blob.
- LaptopBlob head bytes "0/1/2/3" (R4.1) — now {0 slot, 1 disc} ONLY; no "box-none"; no kind=2
  on LaptopBlob (the quad rides LaptopQuad).
- R5.2 "host canonical on-change" for the BATCH path — superseded by the unconditional
  post-batch canonical (R10-1). Organic host changes keep on-change.
- v1's snapshot-LWW for A and C — dead (R1-3/R2-1).
- v1's "4 Hz walk" for lid/box — 1 Hz (rack cadence).

## §7 /qf round map (what moved where)

R1 claim-model + box LWW loss -> host-canonical; R2 A=value-ops (insert is claim-free), widget
construction measured, v119 drain/canonical precedents; R3 ORDER grammar (edit script, no move
verb), rw delta + closed census, join restored, kind/file split; R4 blob same-FIFO + atomic batch
carrying append STRINGS + reap policy + selftest axis; R5 change-edge authority + PrimeBaselines
piggyback invariant + host-no-derivation + full-circle selftest + teardown/cost; R6 WriteSlot
writes floppyData (insert transport complete) + stash+TTL + int pre-filter proof +
skip-rebuild-on-equal; R7 C-1 restart measured + widget-inclusive digest + forged-canonical
rejection + box all-blob; R8 kind split (one Assembler per module) + op=5 death + origin stamping
measured; R9 join-path migration + send-lane ordering measured + floppyBox lineage measured +
struct shrink; R10 unconditional post-batch canonical + kind=1 still load-bearing (ClearSlot
receiver path) + proto reject measured; R11 "that holds".

## §8 BUILD OUTCOME (2026-07-18, proto 121)

Built as designed with these deltas (all root-fixes from the two pre-commit audits):

- **Perf audit: 0 CRITICAL.** Folded flags: F1 floppybox 1 Hz per-box FString copies ->
  `FB::ReadDigest` alloc-free raw-buffer digest pre-filter (full ReadArrays only on digest
  change); F2 box Assembler::Sweep moved inside the 1 Hz gate (the blob_chunks contract);
  F3 box client shadow advances INCREMENTALLY per ACCEPTED send (a refused op re-derives
  next sweep -- no silent loss, no dupe resend); F5 WriteQuadAndRebuild checks the three
  field_io write results (partial-apply WARN). F6 noted: lid sweep sits behind the laptop
  resolve gate (seconds-window asymmetry, documented not fixed). The 4 Hz quad idle path
  verified int-only on both roles; no NameOf on warm paths; selftest OFF = one static bool.
- **Correctness audit: CRIT-1 + IMPORTANT-2, both root-fixed.**
  - CRIT-1: blob transport hard-caps at MaxBlobBytes() (=56,100 B, now exported from
    blob_chunks.h) and every canonical path ignored the send result -> silent permanent
    divergence on oversize/backpressure. Fix: PackCanonicalBounded (deterministic tail-drop
    below the cap + WARN, the OPEN-9-residual class) + send-result checks everywhere +
    retry arms (quad: g_canonRetry flag re-sent per poll; box: g_canonRetry eid set re-sent
    per sweep; join-refusals arm the broadcast retry which reaches the joiner too).
  - IMPORTANT-2: the single global op=1/3 park could cross-pair one sender's scalars with
    another's content stream (two peers at two terminals of the ONE laptop) -> per-sender
    park map `g_pendingSlots` keyed by senderSlot, consume matches the blob origin exactly.
- Audit-verified invariants: PrimeBaselines piggyback complete at every wire apply site;
  unconditional post-batch canonical incl. the malformed path; drain-before-adopt +
  skip-rebuild-on-equal; senderSlot==0 enforcement; LaptopStatePayload 16 B shrink clean
  (no stale field users tree-wide); lanes pinned, none relay-whitelisted; teardown complete.
- Accepted-residual notes: duplicate-content buffer rows may cross-pair their UIDs on
  removal (content-identical either way; canonical heals); host push past the native 15 cap
  logs WARN + applies (divergence-window artifact, canonical-visible).
- File sizes: laptop_sync 686 / laptop_buffer_sync ~600 / floppybox_sync ~500 /
  ue_wrap laptop 396 / portable_pc 73 / floppybox 104 / field_io 119 -- all under the caps.
  Perf audit's forward note: extract the lid axis (~120 LOC) before the NEXT laptop feature.
- **Smoke-found fix (run 1+2)**: canonical sends into a LOADING client's window spammed
  (AnyClientReady was the TRANSPORT predicate while SendReliable's B2 gate is WORLD-ready ->
  anySuccess=false -> no prime -> 4 Hz detector re-fire). Fix: AnyClientReady uses
  IsSlotWorldReady; zero world-ready clients -> prime silently (the joiner gets the connect
  canonical at its ready edge); refused-send WARN only on the arm transition.
- FINAL DLL `a451fce7cb674d04` x4 (host + 3 clients), kProtocolVersion 121.
- **Smoke evidence (3 runs, all PASS, RSS ~3.2 GB stable both peers):**
  - Run 1 (60e63516): host selftest circle 0->1->0 cross-peer, matching digests
    `763e681c6a3ee9cf` / widget `14650fb0739d0383` on BOTH peers; caught the load-window spam.
  - Run 2 (42715c65, 75 s): the CLIENT DERIVATION circle proven end-to-end -- client inject
    digest `10ee8c3f80fda3cb` -> "local edit -> batch (1 op)" -> host "batch from slot 1
    applied (1 op, 0 skipped)" -> canonical -> both peers converge to `763e681c6a3ee9cf`.
  - Run 3 (a451fce7 FINAL, 75 s): spam gone (exactly ONE organic line per change); both
    selftest circles again; the host's DIGEST tick shows the client-authored row
    `10ee8c3f80fda3cb widget={1,692b97476b2bdb5e}` = cross-peer raw+widget match; ZERO
    WARN/ERROR from the three lanes in either log; floppybox connect canonical shipped
    (1 box) + box class resolved on both peers.
  - NOT hands-on (autonomous evidence only). File sizes final: laptop_sync 698 /
    laptop_buffer_sync 595 / floppybox_sync 515 / ue_wrap laptop 383 -- all under caps.
  - Status: **AS-BUILT** (smoke x3 + e2e both-direction digest circles), awaiting hands-on.
