# VOTV — ALL events, classified for coop sync (2026-06-17)

User directive (RULE 1, no crutches): **sync/mirror ALL game events**. This doc is the
authoritative classification: every player-facing event -> its visible artifact(s) -> the
sync SCOPE (world-authoritative vs per-player-subjective) -> the sync CHANNEL (existing or
to-build) -> coop STATUS.

## Sources
- Player-facing enumeration: `voicesofthevoid.wiki.gg/wiki/Events/{Story_Mode,Random,
  Time-Related,Signals,Player-Activated,Dreams}` (Alpha 0.9.0; fetched 2026-06-17).
  (The `eternitydev-games.fandom.com` mirror the user linked is bot-blocked = HTTP 403, both
  via our RE agent and a direct fetch. The wiki.gg pages are the current, detailed source.)
- Engine dispatch reality: agent a2590baf (SDK CXXHeaderDump + `research/bp_reflection/` +
  `research/pak_re/extracted/`), folded into `votv-event-system-RE-2026-06-13.md`.

## The count, reconciled ("~200")
There is **no single event registry**. The "~200" the user means = the union of:
- **128 player-facing EVENTS** (Story 38 + Random 36 + Time-Related 23 + Signal-triggered 11
  + Player-Activated 9 + Dreams 11).
- **278 `list_signals` rows** (the lore "anomalies" — already host-rolled + mirrored, see
  channel S below).
- ~100 creature/entity classes (the spawn artifacts of the above events).
Engine-side these funnel through **TWO** FName dispatchers — `trigger_eventer.runEvent` (65
cases) + `trigger_eventer.runSpecialEvent` (42 cases, 31 net-new prank events) — PLUS the
daynightCycle weather/hour-rolls, mainGamemode ambient verbs, 28 ambient `ticker_*`, 17
standalone `*Spawner_C`, and 36 `trigger_*` volumes. (Our prior "65" counted ONE of these.)

---

## THE TWO AXES of classification

### Axis 1 — SCOPE (the RULE-1 / MTA decision: what SHOULD mirror)
Per the MTA model (reference/mtasa-blue) we mirror **shared WORLD state**, never a peer's
**subjective per-client experience**. Three scopes:
- **WORLD** — one shared truth all peers must see identically (a meteor shower, a spawned
  UFO, weather, a dropped prop). Host-authoritative; mirror to all. THE bulk of the work.
- **PLAYER-LOCAL (SELF)** — a per-viewer experience that is CORRECT to keep local: dreams
  (personal sleep minigames), jumpscares "when YOU open the locker / aren't looking",
  hallucinations, personal teleports to alt-dimensions. Forcing the host's instance onto a
  client would be WRONG, not missing. (This is not a crutch — it is the engine-faithful
  answer; SP authored these per-player and coop keeps them per-player. Each peer rolls/sees
  its own.)
- **DATE/SEASONAL (AUTO)** — driven by the real-world clock (April Fools, Halloween,
  Christmas, Easter cosmetics). Every peer's game computes them identically from the date;
  **no sync needed** (already consistent, like the ToD-derived night sky).

### Axis 2 — CHANNEL (how a WORLD event reaches clients)
Existing authoritative channels (DONE):
- **N** `npc_sync` — creature spawns whose leaf class is in `kNpcAllowlist`
  (`ue_wrap/sdk_profile.h`); subclass-aware, intercepts `BeginDeferredActorSpawnFromClass`.
- **P** `prop_lifecycle`/`remote_prop` — any `Aprop_C`-descendant **with a non-None Key**.
- **W** `weather_sync` — rain x3, lightning x2, fog, wind, snow, redSky.
- **T** `time_sync`/sky-stars — ToD, clock, the continuous `eff_shootingStar` night twinkle.
- **S** signal sync — `SkySignalState`(host-rolled set), `SkySignalCatch`(consume-replay),
  `SavedSignalAppend/Delete`. Covers the 278 signals + the signal-guaranteed story beats.
- **A** `atv_sync` — vehicle transforms. **Z** `sleep_sync` — shared sleep gate.
- **F** `firefly_sync` — the ONLY existing cosmetic-emitter-cue precedent (PSC-set diff ->
  reflected `SpawnEmitterAtLocation`).

Channels to BUILD (the GAPS), in priority order:
- **B1 cosmetic emitter-cue** (generalize `firefly_sync`, host-authoritative): host detects a
  new known cosmetic PSC (poll/diff, ~2-4 Hz, host-only, connected-gated) -> broadcast
  `{cueId, x,y,z}` -> peers reflected `SpawnEmitterAtLocation`. **The reported starfall bug
  lives here.** Covers: Meteor Shower / Shooting Star (`eff_shootingStar_rain` @ eventer
  @4709, EX_CallMath), Eye Moon, Pink Beam, TriFO flicker, Blinking Lights, Green-Fire glow.
- **B2 sound-cue**: host detects a new world AudioComponent (or taps the known trigger) ->
  broadcast `{soundId, x,y,z, is2D}` -> peers replay. Covers: Vent Knocker (banging), Vent
  Crawler (shuffling), Earth-Shaking Roar, Red Phone, Romeo Cries, Bridge Sob, Howling
  Grass/Hoofprints, Dropped-Gun yowl.
- **B3 spawn allowlist extension** (extend N/P): add the creature/prop classes the events
  spawn but `kNpcAllowlist` doesn't yet cover. MOST are ONE line each (subclass-walk does the
  rest). Covers ~20 creatures (pink `wisp_C`, geomOvoid, lakeMouth, possessed kerfur, sky UFO,
  space jellyfish, grave zombies, restocker, zombie deer, prying eyes, thunder wisp, the gray
  saucers/tanks/boars, the rozital family, soltomia, the mothership/peeping UFO, deer, etc.).
- **B4 weather/sky extension** (extend W): Black Hole Sun, Bad Sun, Black Fog, complete
  Super Fog (clear-only today), Meat-Rain sky.
- **B5 save/story-state delta**: a host->client reliable carrying the save-array writes
  (`forceObjects.Add`, treehouse build stages, `brokenServers`, power state, story flags,
  objective timers). ONE `SaveStateDelta` closes the forceObjects family (~14 events) at once.
- **B6 world-prop-state**: prop REMOVAL / MOVE / spawn-without-Key that prop_lifecycle's
  keyed-spawn path misses. Covers: Stolen Radio Tower (removal-when-unobserved), Satellites
  Move, Mannequins Move, Arirals Eat Shrimp (removal), Teleporter Logs, keyless event props.

---

## CLASSIFICATION — Story Mode (38)

| # | event | artifact(s) | scope | channel | status |
|---|---|---|---|---|---|
| 1 | Meteor Shower | `eff_shootingStar_rain` emitter | WORLD | **B1** | GAP (reported bug) |
| 2 | Power Outage | breaker power-off + extra fireflies | WORLD | **B5**(power)+F | GAP(power) |
| 3 | Vent Crawler | shuffling sound + `ventCrawler_C` + grate prop | WORLD | B2+N(done)+P | partial (creature mirrors; sound GAP) |
| 4 | Ariral "Peace" Signal | guaranteed signal | WORLD | S | DONE-ish |
| 5 | Ariral Picnic | warp-arrow NPCs + picnic props | WORLD | N/P | partial (keyed props mirror) |
| 6 | No More Birds | ambient (birds flee) | WORLD | cosmetic/ambient | minor (low value) |
| 7 | Wormhole Signal | guaranteed signal | WORLD | S | DONE-ish |
| 8 | Arirals "Talk" Signal | guaranteed signal | WORLD | S | DONE-ish |
| 9 | Arirals Eating Shrimp | shrimp-prop removal | WORLD | **B6** | GAP(removal) |
| 10 | Vent Knocker | banging sound | WORLD | **B2** | GAP |
| 11 | Ariral "Cringe Comp" Signal | object signal | WORLD | S | DONE-ish |
| 12 | Email Spam & Ariral Visit | warp-arrow NPC + email | WORLD/PLAYER | N + (email local) | partial |
| 13 | Pink Wisp Swarm | many `wisp_C` | WORLD | **B3**(1 line) | GAP(1 line) |
| 14 | Sushi Gift | sushi-box prop + letter | WORLD/PLAYER | P + (letter local) | partial |
| 15 | Flickering Lantern Prank | props + ariral NPC + door destruction | WORLD | N/P + door | partial |
| 16 | Ariral Treehouse | multi-day build structure | WORLD | **B5** + P | GAP(story) |
| 17 | Anti-Gravity Prank (`agrav`) | floating physics + cloaked NPC | WORLD | physics-local + N | GAP (physics by-design-local; see note) |
| 18 | Croissant Delivery | croissant prop + letter | WORLD/PLAYER | P + (letter local) | partial |
| 19 | Radio Tower Ariral Ship | `arirShip_C` | WORLD | **B3** | GAP |
| 20 | Dropped Ariral Gun | yowl sound + gun/shrimp props | WORLD | **B2** + P | partial |
| 21 | Brownie Trail | ariral NPC + brownie/note/doll props | WORLD | N/P | partial |
| 22 | Warning Obelisk | obelisk fall + prop + hologram + alarms | WORLD | **B5** + P + B1 | GAP |
| 23 | Ariral Satellite Signals | 3 signals | WORLD | S | DONE-ish |
| 24 | Black Hole Sun | black sky + red light + black-hole sun + bass + emails | WORLD | **B4** + (email local) | GAP |
| 25 | Looker Signals | live-feed signals + notification + crash mechanic | WORLD/PLAYER | S + B5 | partial |
| 26 | Piramid Signal | signal | WORLD | S | DONE-ish |
| 27 | Piramid Arrival | huge Rozital NPC collecting wisps | WORLD | **B3** | GAP |
| 28 | Rozital Scouts | tentacled rozital NPCs + server-down + kerfur destroy | WORLD | **B3** + B5 | GAP |
| 29 | Garage Soltomia | soltomia NPC + sponge prop + door jam | WORLD | **B3** + P + door | GAP |
| 30 | Peeping UFO | UFO NPC | WORLD | **B3** | GAP |
| 31 | Rozital Mothership | enormous ship NPC (8 min) | WORLD | **B3** | GAP |
| 32 | Hole Repair | beams + rozital NPCs + machinery props + teleporter | WORLD | **B3** + P + B1 | GAP |
| 33 | Gray Drops Corpse | saucer NPCs + corpse prop + lights flicker | WORLD | **B3** + P + B5(power) | GAP |
| 34 | Gray Drops Corpse (window) | same, different timing | WORLD | **B3** + P + B5 | GAP |
| 35 | Gray Drops Jeep | saucers + jeep prop | WORLD | **B3** + P | GAP |
| 36 | Grays at Transformer | TR-disable + gray NPCs | WORLD | **B5** + B3 | GAP |
| 37 | Gray Firetank | TR-disable + tank NPC + box prop | WORLD | **B3** + B5 + P | GAP |
| 38 | Gray Invasion | boars + firetanks (90 min) | WORLD | **B3** | GAP |

## CLASSIFICATION — Random (36)

| # | event | artifact(s) | scope | channel | status |
|---|---|---|---|---|---|
| 1 | Ariral Troll Drive | special drive prop | WORLD | P | minor |
| 2 | Bad Sun | bright/flicker sky + damage | WORLD | **B4** | GAP |
| 3 | Black Fog | fog + Prying Eyes creature + sound | WORLD | **B4** + B3 | GAP |
| 4 | Cursed Church | church structure prop | WORLD | **B6**/P | GAP |
| 5 | The Dialogue | notification | PLAYER | — | local |
| 6 | Earth-Shaking Roar | roar sound + shake | WORLD | **B2** | GAP |
| 7 | Empty Base | everyone-vanished illusion | PLAYER | — | SELF |
| 8 | Eye Moon | moon-replacement emitter | WORLD | **B1** | GAP |
| 9 | Fossilhound vs Boar War | grayboar + fossilhound creatures | WORLD | **B3** (fossilhound allowlisted) | GAP(grayboar line) |
| 10 | Gears | emitter (deprecated pre-0.7) | — | — | skip |
| 11 | Geom Ovoid Roaming | geomOvoid creature | WORLD | **B3** | GAP |
| 12 | Goat Skull Locker Jumpscare | locker prop + scare + sound | PLAYER | — | SELF |
| 13 | Gore Locker | flesh/gib props (per-viewer) | PLAYER | — | SELF (per opener) |
| 14 | Gray Cloaking Panel | prop + visual + scare + physics | WORLD/PLAYER | B3/P + (scare local) | partial |
| 15 | Kerfur-Omega Scary Face | face-swap when-not-looking | PLAYER | — | SELF |
| 16 | Lake Event | Lake Mouth creature + physics | WORLD | **B3** | GAP |
| 17 | Light Switches Off | power/environment | WORLD | **B5**(power) | GAP |
| 18 | Mannequins at Uniform | mannequin prop relocation | WORLD | **B6**(move) | GAP |
| 19 | "Meat Locker Thief" | flesh-gib props + sound | WORLD | **B6** + B2 | GAP |
| 20 | "Meat Locker Thumper" | shadow creature + sound (basement) | WORLD/PLAYER | B3 + B2 | partial |
| 21 | Meat Rain | meat-pile props falling + sound | WORLD | **B4**(sky) + P + B2 | GAP |
| 22 | Possessed Kerfur | Abandoned-Kerfur creature | WORLD | **B3** | GAP |
| 23 | "Press Shift to Run" | notification | PLAYER | — | local |
| 24 | Red Phone Rings | ring sound + notification | WORLD/PLAYER | **B2** + (note local) | GAP(sound) |
| 25 | Red Sky | red sky + emitter | WORLD | W (redSky) | DONE |
| 26 | Rubber Duck | duck prop + sound | WORLD | P + B2 | partial |
| 27 | "Salt Heart" | `saltpile_C` prop | WORLD | P/**B6** | GAP(if keyless) |
| 28 | Satellites Move On Their Own | dish transform + polarity save | WORLD | **B6**(transform) + B5 | GAP |
| 29 | Shooting Star | `eff_shootingStar_rain` (single) | WORLD | **B1** | GAP (same as #1 Meteor) |
| 30 | Skerfuro | creature + teleport + scare | WORLD/PLAYER | B3 + (scare local) | partial |
| 31 | Sky UFO | gray-UFO creature + visual | WORLD | **B3** | GAP |
| 32 | Space Jellyfish | jellyfish creatures + physics | WORLD | **B3** | GAP |
| 33 | Stolen Radio Tower | tower-prop removal-when-unobserved | WORLD | **B6**(removal) | GAP |
| 34 | Super Fog | fog + Thunder-Wisp + visual | WORLD | **B4**(complete) + B3 | GAP(partial today) |
| 35 | "Toyota Corolla" | car prop + physics | WORLD | P/**B6** | GAP |
| 36 | Tree Debris | sticks/pinecones/tubes props + physics | WORLD | P/**B6** | GAP |

## CLASSIFICATION — Time-Related (23)

| # | event | artifact(s) | scope | channel | status |
|---|---|---|---|---|---|
| 1 | Bad Sun | (= Random #2) | WORLD | **B4** | GAP |
| 2 | Teleporting Shadows | shadow creatures circling the player | PLAYER | — | SELF (around triggerer) |
| 3 | Screaming Corpses | corpse creatures around player + sound | PLAYER | — | SELF |
| 4 | April Fools | troll skins / square wheels / Maxwell face | DATE | — | AUTO (date-driven, each peer) |
| 5 | Halloween | seasonal decor | DATE | — | AUTO |
| 6 | Christmas | seasonal decor | DATE | — | AUTO |
| 7 | Easter | egg-hunt quest + props + well-teleport | WORLD/PLAYER | P + (quest local) | partial |
| 8 | Satellite Graffiti | graffiti decal prop (runSpecialEvent) | WORLD | **B6**/P | GAP |
| 9 | Green Fire | campfire prop + glow + vision-pull scare | WORLD/PLAYER | P + B1 + (vision local) | partial |
| 10 | Teleporter Logs | two teleport-log props | WORLD | P/**B6** | GAP |
| 11 | Abandoned Shack Portal | teleport to floating island | PLAYER | — | SELF |
| 12 | Bunker Elevator Camera | random-named camera feed | WORLD | B5 | minor |
| 13 | Furfur Toilet Jumpscare | Furfur creature + scare | PLAYER | — | SELF |
| 14 | Porta Potty Poop Dimension | teleport to alt map | PLAYER | — | SELF |
| 15 | Meat Locker Glows Red | red glow + swinging meat (basement) | PLAYER | — | SELF-ish |
| 16 | The White Door Opens | door prop | WORLD | P | minor |
| 17 | Foxtrot Depths | breakable wall + teleport chamber | WORLD/PLAYER | B6 + (teleport local) | partial |
| 18 | Romeo Cries | sobbing sound | WORLD | **B2** | GAP |
| 19 | Bridge Sob | sob sound + EMF | WORLD | **B2** | GAP |
| 20 | Blinking Lights | flickering posts + EMF | WORLD | B1/B5(power) | minor |
| 21 | Grave Zombies | zombie creatures | WORLD | **B3** | GAP |
| 22 | Server Critical Error | red-skull server screen + sound + explode | WORLD/PLAYER | B5 + (minigame local) | partial |
| 23 | Hallway White Door | door prop | WORLD | P | minor |

## CLASSIFICATION — Signal-triggered (11)

| # | event | artifact(s) | scope | channel | status |
|---|---|---|---|---|---|
| 1 | Kerfur-Omega Sleep Scare | creature + scare + sleep-teleport | PLAYER | — | SELF (sleeper) |
| 2 | Magnet Skeleton | skeleton + magnet props | WORLD | P/**B6** | GAP |
| 3 | Pink Beam & Rozital Engine | beam visual + weather + prop + explode | WORLD | **B1** + B4 + P | GAP |
| 4 | Shitting Duende | Duende creature + scare | PLAYER | — | SELF |
| 5 | Shorts Ariral Teleport | crash sound + visual + power + debris props | WORLD/PLAYER | B2 + B5 + P + (screen local) | partial |
| 6 | Tardis | `tardis`/`AskyFallingEvent_C`-family prop | WORLD | P/**B6** | GAP |
| 7 | TriFO | flicker + triangle-UFO + screen blur | WORLD/PLAYER | **B3** + B1 + (blur local) | partial |
| 8 | Virus | red-skull computer screens + power + servers | WORLD | **B5** + B1 | GAP |
| 9 | Zombie Deer | deer creature + corpse prop + scare | WORLD | **B3** + P | GAP |
| 10 | The Evil | timed objective + game-over | PLAYER/WORLD | B5(objective) | local-ish |
| 11 | SKELEO N APPEARS | skull-zoom visual (Halloween, old) | PLAYER | — | SELF |

## CLASSIFICATION — Player-Activated (9)

| # | event | artifact(s) | scope | channel | status |
|---|---|---|---|---|---|
| 1 | Howling Grass | distant howl sound @ location | WORLD | **B2** | GAP |
| 2 | Howling Hoofprints | distant howl sound | WORLD | **B2** | GAP |
| 3 | Invalid Email | blank email | PLAYER | — | local |
| 4 | Meat Locker Knocker | knock sound + knocked-over props | WORLD/PLAYER | B2 + B6 | partial |
| 5 | Mystery Footprints | footprint props | WORLD | P/B6 | minor |
| 6 | Restocker | black-humanoid creature + light flicker | WORLD | **B3** + B5 | GAP |
| 7 | Skeleton in Chair | skeleton prop in chair | WORLD | P | minor |
| 8 | Gay Baby Jail | teleport to cell (2 min) | PLAYER | — | SELF |
| 9 | I Know What You Are | UI meme image | PLAYER | — | local |

## CLASSIFICATION — Dreams (11) — ALL per-player

Every dream (Base, Boulder, Burger, Climbing, Flooding, Furfur, Hallway, Mannequin, Parkour,
Shed, Work) is a **personal sleep minigame in a separate dream level**. Scope = **PLAYER-LOCAL
(SELF)** for all 11. Coop-correct behavior: each sleeping peer enters its OWN dream (the
existing `sleep_sync` shared-sleep GATE already coordinates who's asleep; the dream CONTENT is
per-player and must NOT be mirrored). **Status: SELF — no sync to build.** (If two peers sleep
at once they each get their own dream — exactly like SP.)

---

## BUILD PLAN (priority order; each is a RULE-1 root-cause channel, no crutches)

1. **B1 cosmetic emitter-cue channel** — FIRST (fixes the reported starfall bug). Generalize
   `firefly_sync` into a host-authoritative cue relay; register `eff_shootingStar_rain` @
   (0,0,6000) as cue #1 (Meteor Shower + Shooting Star). Then Eye Moon, Pink Beam, TriFO,
   Blinking Lights, Green-Fire glow as further cues (one registration each).
2. **B3 spawn allowlist extension** — cheapest high-coverage: ~20 creature classes, most ONE
   `kNpcAllowlist` line each (subclass-walk + npc_sync does the rest). Closes the bulk of the
   Story/Random/Signal creature events at once.
3. **B2 sound-cue channel** — ~9 events; host detects a new world AudioComponent / taps the
   known trigger -> `{soundId,pos,is2D}` -> peers replay.
4. **B4 weather/sky extension** — Black Hole Sun, Bad Sun, Black Fog, complete Super Fog.
5. **B5 save/story-state delta** — `SaveStateDelta` reliable: forceObjects family (~14 at
   once) + treehouse builds + power + brokenServers + objective timers.
6. **B6 world-prop-state** — removal / move / keyless-spawn that prop_lifecycle's keyed-spawn
   path misses (Stolen Radio Tower, Satellites Move, Mannequins, Eat Shrimp, Teleporter Logs).

PLAYER-LOCAL (SELF) and DATE/AUTO events are CORRECT as-is — not gaps. Enumerated above so the
build does NOT accidentally mirror a per-viewer scare or a personal dream onto other peers.

## Tally
- WORLD events needing a new/extended channel (the build): ~75.
- Already mirror via existing channels (N/P/W/T/S/A/Z, fully or partially): ~30.
- PLAYER-LOCAL (SELF, correct, no sync): ~25.
- DATE/AUTO (consistent already): ~4.

---

# PART 2 — eternitydev fandom merge (the user's source, 2026-06-17)

The eternitydev-games.fandom.com Events page (user-supplied PDF; the site 403s automated
fetches) adds events the wiki.gg sub-pages omit AND — critically — confirms the SDK finding
that there are TWO FName dispatchers. Its **"Reputation Events"** category (~24 ariral pranks)
IS the engine's **`trigger_eventer.runSpecialEvent` / `summonArirPrank`** dispatcher (the 31
net-new cases agent a2590baf found). starfall is settled here too: the fandom Random list has
**"Falling star"** = "the same shooting star that appears in the story event" (= Meteor Shower
= `starRain`); and its **"Sky Falling"** (hum -> glowing object crashes -> white hexagon copies
the background) is the SEPARATE `AskyFallingEvent_C`, NOT starfall. Every new event still slots
into the SAME 6 channels — the build plan is unchanged, only the coverage rows grow.

## Reputation Events (= the `runSpecialEvent` dispatcher) — ariral rep-tiered pranks
Mostly prop-spawns (ride P/prop_lifecycle if keyed, else B6) + an "alien noise" sound (B2) +
a few ariral NPCs (B3). Scope WORLD unless noted.

| event | artifact(s) | channel | status |
|---|---|---|---|
| Alien Cart | cart prop rolls downhill + alien sound | P + B2 | GAP |
| Alien Catapult | catapult+alien prop (garage-triggered) + sound | P + B2 | GAP |
| Paper Alien Arrival | door sparks + 3 paper-alien statue props + sound + email | P + B2 + B5(door) | partial |
| Trash Piles | trash props at doorways | P (trash_collect_sync exists) | partial |
| Stalker Ariral | 2 ariral NPCs (knock over, despawn) | **B3** | GAP |
| Rock Thrower | 2 ariral NPCs throw rocks at window | **B3** + physics | GAP |
| Anti-Gravity (rep) | float physics + ariral ship | physics-local + B3 | GAP(physics local) |
| Explosive Cookies | cookie-box prop (explodes on open) | P | GAP(if keyed: mirrors) |
| Brownie Trail | running-footsteps sound + brownie props + note | B2 + P | partial |
| ATV Boobytrap | ATV moved to bridge + refilled 314 + explodes | A(atv_sync) + B5 | partial |
| Fake Drives | drive props (explode on load) | P | GAP |
| Bed Prank | bed+Kel teleported near Sierra dish | B6(prop-move) / SELF(player) | partial |
| Gascan Prank | 2 gascan props (explode) | P | GAP |
| Reorganizing | mannequin props placed + cup stack | P/B6 | GAP |
| Ding Dong Egg Ditch | knock sound + egg prop | B2 + P | GAP |
| Spiked Chocolate | chocolate prop (food-poison) | P + (poison=vitals SELF) | partial |
| Ariral Vaccine | vaccine-box prop + (infinite-hunger effect SELF) | P + SELF | partial |
| Yogurt Ariral | yogurt props + spoon (on sleep) | P | partial |
| Ariral Support | gift food/drive props (loyal tier) | P | GAP(if keyed) |
| Treehouse Kidnapping | sleep -> teleport into treehouse | SELF(player teleport) | local |
| Cookies (gift) | cookie-box prop (47-50 cookies) | P | GAP(if keyed) |
| Ariral ATV Repair | ariral NPC + toolbox prop | **B3** + P | GAP |
| Ariral ATV Refilling | ariral NPC + gascan prop | **B3** + P | GAP |
| Ariral Graffiti | graffiti decal prop on wall | **B6**/P | GAP |

## Additional Random events (fandom-only)
| event | artifact(s) | scope | channel | status |
|---|---|---|---|---|
| Password Attempt | keypad used by unknown force (front/garage door) | WORLD | keypad_sync(exists) / B5 | partial (keypad mirrors) |
| Security Breach | terminal anon-user typing commands | WORLD | B5 / comp | partial |
| Haunted old TV | TV prop plays error -> disintegrates to blood+meat | WORLD | P + B1(disintegrate FX) | GAP |
| Sky Falling | hum + glowing object crash + white-hexagon copier (`AskyFallingEvent_C`) | WORLD | **B3**/P + B1 + B2(hum) | GAP (distinct from starfall) |
| Phone Room | trash-covered phone booth in forest (radar) | WORLD | P/**B6** | GAP |
| Army | huge number of ships fly over base | WORLD | **B3**(swarm) | GAP |
| Witches' Circles | mushroom-ring props + diggable animal skull | WORLD | P | GAP |
| Automatic Signal Detection | dishes re-tune + polarity reset (= Satellites Move) | WORLD | **B6**(transform) + B5 | GAP |
| hi :) | paper text replaced with "hi :)" (incl. inventory) | PLAYER | — | local |
| Disappearing Meat | basement carcass removed -> blood pool | WORLD | **B6**(removal) + grime | GAP |
| Witches/Phone/Church/Gears | misc prop-spawn/sky | WORLD | P / B1 | GAP/minor |

## Additional Player-Activated events (fandom-only)
| event | artifact(s) | scope | channel | status |
|---|---|---|---|---|
| Growing Basalt Columns | basalt-pillar props grow from ground (dig X:465 Y:-85) | WORLD | P/**B3** | GAP |
| Madness Combat (console `madness.combat`) | grunt NPCs spawn + attack | WORLD | **B3** | GAP |
| Mad Kerfur (give "Skibidi toilet" paper) | Omega-Kerfur attacks Kel | WORLD | kerfur sync | GAP |
| Well Sleeping (sleeping bag in well) | teleport to untitled_129 | PLAYER | — | SELF |
| Cremation in a Dream (sleep in incinerator) | door closes + incinerate player | PLAYER | — | SELF |
| Strange Message (mail debug) | spam email | PLAYER | — | local |
| Keljoy Minigame (achievement icon) | PC keljoy minigame in world | PLAYER | — | local |

## Game Over events (fandom-only category)
| event | artifact(s) | scope | channel | status |
|---|---|---|---|---|
| THE END IS NEAR (`evil` signal) | red-skull displays + 12-min timer -> world destroyed -> game over | WORLD/PLAYER | B5(objective) + B1 | special |
| BAD SUN | blacked-out sun + damage + blood/flesh-falloff (= Story #24 / Random Bad Sun) | WORLD | **B4** + (damage SELF) | GAP |
| Rufus / Thiccfus (console `gooseworx.rufus`) | instakill NPC + "HIDE" text + fake explosions | WORLD/PLAYER | **B3** + (text SELF) | GAP |

## 3:33 Events (fandom category, table "in progress")
Player-action-at-03:33 triggers (already covered by the wiki.gg Time-Related table: Furfur
Toilet, Meat Locker Glows, Bridge Sob, Server Critical Error, Hallway White Door, etc.). Mix of
**SELF** (jumpscares, personal teleports) and WORLD (sound-cue/prop). No new channel.

## What Part 2 changes
- Total enumerated events rises from 128 -> ~165 (the ~24 Reputation pranks + ~13 fandom-only
  random/player/game-over). Still well under the union with the 278 signals (~200+ as the user
  said).
- **No new channel needed** — every addition maps to B1/B2/B3/B4/B5/B6 or is SELF/local.
- Confirms: (a) two dispatchers (runEvent + runSpecialEvent=Reputation Events); (b) starfall =
  starRain = "Falling star" / "Meteor Shower" (B1, the reported bug); (c) "Sky Falling" is a
  distinct event (`AskyFallingEvent_C`), also a B1/B3 GAP; (d) several pranks already ride
  existing channels (trash_collect_sync, keypad_sync, atv_sync) — partial coverage today.
