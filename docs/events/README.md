# docs/events/ — the per-event coop knowledge base

One doc per VOTV event SCENARIO (user rule 2026-07-04): each event's native behavior, its
coop sync design, and its caveats live in that event's own file here. This folder answers
"what exactly happens during event X and how does coop deliver it to every peer" — the
cross-cutting CONTRACTS stay where they are:

- `docs/COOP_EVENT_JOIN.md` — the join-during-event contract (EventSnapshot design, the
  per-lane late-join table, phases). Per-event docs LINK to it; they do not restate it.
- `src/votv-coop/src/coop/world/event_fire_sync.cpp` — the replay/no-replay dupe matrix
  is AUTHORITY in code (kReplayRows/kNoReplayRows). A per-event doc cites its row's
  verdict; changing a verdict changes the CODE first, the doc second.
- `research/findings/votv-event-system-RE-2026-06-13.md` — the runEvent dispatcher RE
  (all 69 list_events rows' concrete outputs). Durable ground truth; per-event docs cite
  it instead of re-deriving.
- `research/findings/votv-active-events-registry-RE-2026-07-04.md` — the native
  activeEvents registry (lib_C::setEvent refcount + senders; the ~95-class census).

## Doc skeleton (copy for a new event)

```markdown
# <event name> — <one-line what the player sees>   (STATUS: DESIGN | AS-BUILT | VERIFIED)

## 1. Native behavior (ground truth)
   list_events row(s) / trigger; actor class(es); the full scenario timeline; every
   externally visible effect. Tag every claim [wiki] / [bytecode fn] / [live log] / [?].
## 2. Sync-axis table
   | axis | deterministic-from-start / host-random / per-viewer | carried by (lane/wire) |
## 3. Coop design
   What is mirrored, by which lane; what replays natively; what is host-local. Cite the
   dupe-matrix verdict + the late-join answer (COOP_EVENT_JOIN.md 3.4 row).
## 4. Caveats / known quirks
   Native bugs, save/reload interactions, timing races, what autonomy can't verify.
## 5. Verification
   What was proven, HOW (smoke line / hands-on / matching real log), what remains.
```

A "sync-axis" is one externally-visible state stream (position, anim phase, sound cue,
sub-spawn, decal...). The table forces the design decision per axis instead of a vague
"mirror the event".

## Index (grow as docs land; the ~95-class census is the backlog pool)

| Event (row / class) | Doc | Status |
|---|---|---|
| piramid (`piramid2_C`) — the walking pyramid; the devs'-gauntlet acceptance case | [piramid.md](piramid.md) | research in flight 2026-07-04 |
| obelisk (`obelisk_C`) — the Phase 0 registry-probe exemplar (BEGIN/END proven) | (todo) | — |
| wisps (`wisp_C` swarm + `killerwisp`) — first creature-event lane, shipped 07-03 | (todo — extract from memory/topic files) | AS-BUILT in code |
| starRain (`skyFallingEvent`) — event_cue lane; the known late-join gap | (todo) | gap documented in COOP_EVENT_JOIN.md |

Priority order = player impact x sync risk: story/scheduled spectacles first (piramid,
obelisk, arirShip, badSun/superFog sky events), then creature controllers (already
lane-covered — docs mostly record the verdicts), then pranks/ambience (mostly host-local
by design).
