# RE: kerfurOmega variant EFFECT identity — face machinery, SCS rigs, sentient semantics, all-28-variant census (2026-07-03)

Durable RE (survives implementation changes). Source: cooked-BP bytecode + SCS dumps via
kismet-analyzer (research/pak_re/dump_precise.py, dump_scs.py, census_variants.py over
kerfurOmega.json, kerfusFace.json, kerfurOmega_mynet.json + 25 more variant jsons —
gitignored, regenerable per research/pak_re/drone_dust_notes.md workflow). Confidence: [RD]
throughout unless marked [V].

Consumer: `ue_wrap/scs_rig.cpp` + `coop/player/skin_effects.cpp` (commits `23704962` take-1,
`8c813c44` take-2) — the player-skin effect rig.

## 1. The face pipeline (kerfusFace_C) [RD, self-consistent with votv-kerfurOmega-coop-double-RE-2026-06-14 §face]

- `kerfurOmega_C.makeFace()`: `BeginDeferredActorSpawnFromClass(kerfusFace_C, T(0,0,10))` ->
  `SetIntPropertyByName(actor, 'type', this.Type)` -> `FinishSpawningActor` -> `this.face =
  actor` -> if `!skipFaceAssign` -> `setFace()`. EVERY kerfur parks its face actor at the same
  world spot (0,0,10); overlap is fine (see gen()).
- `kerfusFace_C.ReceiveBeginPlay -> gen(out dynmat)`:
  - `SceneCaptureComponent2D->ShowOnlyComponent(face)` — the capture renders ONLY its own
    `face` skeletal mesh (AnimBP `kefrus_face_Skeleton_AnimBlueprint` = the blink/idle anim);
  - `rt = KismetRenderingLibrary::CreateRenderTarget2D(256, 256, fmt=2, clear=(0,0,0,1))`;
  - `dynmat = Plane->CreateDynamicMaterialInstance(0, switch(type))` with the per-type parent:
    `0->Inst_kerfusFace_blue, 1->pink, 2->green, 3->None, 4->red`;
  - `SceneCaptureComponent2D.TextureTarget = rt`; `dynmat->SetTextureParameterValue('tex', rt)`;
  - `dynmatStare = KismetMaterialLibrary::CreateDynamicMaterialInstance(Inst_kerfusFace_stare)`;
    on `mainGamemode.april1st` the returned dynmat = dynmatStare.
- `kerfurOmega_C.setFace()`: `if faceMaterialIndex >= 0`: spooky -> `Mesh.SetMaterial(fmi,
  Inst_kerfusFace_)` (static stare) + `anim.isFace=true`; else `Mesh.SetMaterial(fmi,
  rendered ? face.dynmat : Inst_kerfusFace_)` — offscreen kerfurs (`rendered` = the
  mat_invisRender `vis` cube probe) wear the static material, the RT dynmat only when seen.

## 2. Sentient semantics — the effects are DORMANT by default [RD; take-1/take-2 lesson]

- Base `kerfurOmega_C` SCS authors 14x `eff_life*` ParticleSystemComponents
  (Template=`eff_kerfurJointLife`, bones upperarm/forearm/hand/thigh/lowerLeg/foot L+R +
  head + belly, rel scales 0.3–1.25) with **bAutoActivate=FALSE**, and `lifeLight`
  (PointLight @bone `belly`: Intensity=100, LightColor=(255,156,255), AttenuationRadius=500,
  SourceRadius=50, SourceLength=106, CastShadows=false) with **bVisible=FALSE**.
- `makeSentient()` is the ONLY enabler: `dynmat.SetTextureParameterValue('ag',
  tex_kerfurOmega256_glow1)` + all 14 `eff_life*.Activate(true)` + `lifeLight.SetVisibility
  (true)` + `sentient=true` + meow cue swap. Called from loadData (save restore) + one
  ubergraph site. A DEFAULT crafted kerfur shows NONE of this — its visible glow bits are
  base-material emissive. **[V hands-on 2026-07-03: force-enabling this set on player skins
  produced the screen-flooding pink light blast the user rejected.]**
- `setStyle(contrcut)`: `Mesh.SetMaterial(0, null)` -> `Mesh.SetSkeletalMesh(skinMesh)` ->
  `dynmat = Mesh.CreateDynamicMaterialInstance(0, null)` -> optional setFace -> meow cue by
  `sentient`.

## 3. Variant census (28 classes; CDO fields: skinMesh / Type / faceMaterialIndex / footstepSound)

Face (fmi=1) ONLY on the 4 omega-body classes; everything else fmi=-1 (their meshes have no
screen slot; KerfurO_maid measured single-material `inst_kerfurMaid`):

| class | skinMesh | Type (face) | extras |
|---|---|---|---|
| kerfurOmega (base) | kerfurOmegaV1 | 0 blue | base SCS above |
| kerfurOmega_0 | kerfurOmegaV1_nc | 0 blue | — |
| kerfurOmega_1 | kerfurOmegaV1_m | 1 pink | — |
| kerfurOmega_2 | kerfurOmegaV1_h | 2 green | — |
| kerfurOmega_mynet | kerfurOmega_mynetSkin | -1 | SCS: 9x eff_mynetEmitterLimb on limb bones (autoActivate default TRUE), 8x eff_pofinStatic under foot billboards, 11x decal_digitalGrid (100^3, Pitch=90, scene root — a STATIC pool, zero runtime decal moves in bytecode), 3x eff_zapp AudioComponents (spark_pofinStatic_Cue, vol 0.4). step(loc) = lib_C::step + SpawnEmitterAtLocation(eff_mynetEmitterStep, loc) + PlaySoundAtLocation(boltrix_mediumHit, att_default). footstepSound=boltrix_mediumHit |
| kerfurOmega_keljoy | keljoySkin | -1 | footstepSound=keljoyFoot_Cue (played by stepped()) |
| kerfurOmega_mannequin | mannequinSkin | -1 | setStyle/makeFace overrides CLEAR slot 1 (doll face is painted on) |
| kerfurOmega_asmodena | skin_asmodena | -1 | eff_burning hands + eat/ignite audio (NOT in the player skin list) |
| kerfurOmega_col / _col_gamer | (none) | — | texturePicker-paintable; per-instance color, no skinMesh |
| ariral, ariral1, keith(6), antibreather, argpl, alien, bonerman(fleshly), bonerman1(skeleton1), vargman(vargskeleton1), maxwell, erie(4), erieV4, igetis, monique, skerfuro, furfur, kel, fleshman, zombiefur | per-name skins | -1 | CDO-only overrides, no extra SCS, no step FX |

- No variant class wears KerfurO_maid or kerfurOmega_krampusSkin (mesh-only assets).
- Step chain: kerfur footsteps are anim-notify -> `step(loc)` -> `lib_C::step(Character,...)`
  which fires `stepped(Volume, Loc)` back on the kerfur; base `stepped` plays `footstepSound`
  when set. mainPlayer_C does NOT implement these BP events — a puppet stride must dispatch
  its own FX (skin_effects::OnStep at the puppet_footsteps::Stride::StepDue verdict).

## 4. Engine facts proven along the way

- Cooked UE4.27 BPGCs keep `SimpleConstructionScript` -> `RootNodes` -> `SCS_Node`
  (ComponentTemplate / InternalVariableName / AttachToName / ParentComponentOrVariableName /
  ChildNodes) readable via reflected offsets at runtime [V — live rig builds logged 14:14].
- Template objects hold EFFECTIVE values for every field (CDO-inherited included) — raw reads
  need no serialized-or-not logic. Bitfield bools: template-byte XOR class-CDO-byte isolates
  the single overridden flag (cooked templates serialize only overrides) — scs_rig::
  ReadTemplateFlag [[lesson-template-faithful-scs-dormancy]].
- `UGameplayStatics::SpawnEmitterAttached/SpawnSoundAttached/SpawnDecalAttached` accept a bone
  FName + relative transform directly — one reflected call per cosmetic component, no manual
  registration [V — 31-comp mynet rig live].
- `FindPropertyOffset` DOES climb SuperStruct [V — reflection.cpp read 2026-07-03: 32-hop
  SuperStruct loop, audit fix ~09c003b 2026-05-25; the stale local-only header comment
  corrected same day] — AttenuationRadius/IntensityUnits resolve on a UPointLightComponent
  query despite being declared on ULocalLightComponent (0x328/0x330).
