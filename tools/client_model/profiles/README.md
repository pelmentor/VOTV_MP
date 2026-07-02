# Repose profile LIBRARY — the "VOTV T-pose standard" collection

One json per learned example (user 2026-07-02: keep a base of profiles, not one file).
`repose.py apply <origDir> default <out.obj>` uses **DEFAULT_PROFILE** (repose.py, one
line to swap). A profile is learned from ONE manually posed PSK example and transfers
by bone NAME to any HL Bip01 model.

| file | format | learned from | look | status |
|---|---|---|---|---|
| `tpose_v1_narrow_2026-07-01.json` | 1 (rotation-only) | `hl_einstein_v1sc.psk` | narrow arm span 177.1 (the first manual example) | **DEFAULT** — in-game look preferred (hands-on 2026-07-02 evening) |
| `tpose_v2_wide_2026-07-02.json` | 2 (R+t local deltas) | `hl_einstein_v1sc_new_profile.psk` | wide arm span 209.5, matches the anthro template proportions (215) | library — in-game look REJECTED 2026-07-02 evening ("переделай обратно под v1"); template-matched span did not beat the narrow look

Format 1 = per-bone local ROTATION only (reproduces a pure re-pose; drops joint
translations). Format 2 = full rigid local delta (R + t in the bone's rest frame; carries
joint moves such as the v2 widened shoulders). `repose.py` loads BOTH (normalized to 4x4
at load). Self-reproduce residuals at learn time: v2 = 0.00005 max, v1 = 0.00009 max
(units ~190 = model height; float-zero).

The learn-time `up` axis is a CONSTANT of the target space (UE Z-up; the cook is a pure
Y-negation) — never inferred from the mesh bbox: a T-pose arm span wider than the height
flips the argmax and the model gets measured/grounded sideways (the 2026-07-02
17.58-residual bug).
