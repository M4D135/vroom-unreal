# Valve glove sources and driving pose

Copyright (c), Valve Corporation. The attached [LICENSE](LICENSE) applies.

Source: [ValveSoftware/openxr_engine_plugins — valve_hand_models](https://github.com/ValveSoftware/openxr_engine_plugins/tree/main/assets/valve_hand_models), file `openxr_glove_left_model_slim.glb`, retrieved 2026-09-06.

GLB SHA-256: `135189e6a2caab7d5fa0182feda4c2c7b1a9485bc5a7b27b0fe7b5b5ed7e33eb`.

These are the untouched embedded albedo JPEG and tangent-space normal PNG; the full GLB is deliberately not duplicated in the project.

- `ValveGloveColor.jpg`: SHA-256 `a424f719450ca9bce29ba28e90712dab4e40b56e67642150379b5359475eb868`; 4096 × 4096, original embedded bytes.
- `ValveGloveNormal.png`: SHA-256 `bde8ee08ffd7b3437d16a23206a826b6504f8e91587093a02a84d710e2994767`; 4096 × 4096, original embedded bytes.

`Tools/bootstrap_hand_assets.py` imports `T_RacingGloveColor` (sRGB) and `T_RacingGloveNormal` (linear normal compression, green channel inverted for Unreal). Both retain their 4K source and use a 2048 maximum runtime texture size. `M_RacingGlove` recolors red panels to ivory in its shader, retaining the original dark panels, stitches and texture detail. Metallic 0, roughness 0.8. No source pixels were repainted.

The approved circular-wheel pose preserves the four fingers and changes only
the thumb and its weighted palm transition. `Tools/repose_glove_thumb.py` uses
the verified Valve GLB, discards the inherited thumb vertex fit, and applies
local opposition and flexion to put the palm-side thumb surface against the
inner rim. The thumb remains upright/oblique, as in the supplied references.
The exact local joint angles are recorded in the command below; positive
proximal/distal flexion means rotation about local -X, while metacarpal X/Y/Z
and proximal Y/Z use the corresponding positive local axes. This leaves the white glove
material, UVs, triangle order, and 2,633 non-thumb vertex positions unchanged.
All 1,671 vertices with any weight on another finger's phalanges retain their
positions and normals exactly. Adjacent, unprotected normals follow the local
surface-frame change.

To reproduce the input and selected candidate with Python 3 and NumPy, set
`VROOM_GLOVE_SOURCE` to the local copy of the GLB identified above, then run:

```sh
python3 Tools/bake_valve_gloves.py --source "$VROOM_GLOVE_SOURCE" \
  --output Saved/Validation/gloves-historical.json
python3 Tools/repose_glove_thumb.py --source "$VROOM_GLOVE_SOURCE" \
  --input Saved/Validation/gloves-historical.json \
  --output Saved/Validation/gloves-circle-reproduced.json \
  --metacarpal-x -69.99999999999436 \
  --metacarpal-y 7.665489778964648 \
  --metacarpal-z 79.99999999999089 \
  --proximal 84.99999999999885 \
  --proximal-y -51.14584084162949 \
  --proximal-z 63.86821426598626 \
  --distal 22.216574922888743
```

The first command reconstructs the historical input exactly: SHA-256
`f21033656a44456f680e943dfd0395c0211b702f7af2af14d86856b2c2ad91a3`.
The selected generated JSON, including provenance metadata, has SHA-256
`f342faa13e3c64a1883f2b2683ccc6b3e43edf2a565dc2a1cde2c7b06ea68908`.
The command reproduces this selected file byte for byte; no subsequent trial
refinement is included in that hash.
The generator verifies the GLB checksum and matching input topology/UVs. Its
output preserves historical fit records but marks them inapplicable to the
new thumb; they were vertex-only checks, not triangle-clearance guarantees.
The generator itself reports `NOT_EVALUATED_BY_GENERATOR` for rim clearance.

Run `Tools/audit_cockpit_grip.py` separately against the adopted data and inspect
neutral and steering views. That audit checks the rim against both glove
surfaces; it does not certify glove self-intersections. The selected data have
about 0.667 mm minimum rim clearance. Fixed palm-side landmarks 51 and 405 sit
about 1.86 mm and 1.35 mm from the rim, with normals pointing toward it (dot
products about 0.996 and 0.999). This checks the identified palm surface rather
than selecting a nearby point on the back of the thumb.

The separate thumb/index check finds zero transverse intersecting face pairs
for this exact SHA. It excludes shared-index neighbours and contacts limited
to triangle edges; it is not a watertight-volume or perfect-anatomy certificate.
Reproduction and this check are recorded in
`Saved/Validation/v034/thumb-wrap/selected-final-integrity.json`, with the
validation-only script `Saved/Validation/v034/thumb-wrap/audit_self.py`.
The four fingers and circular rim remain unchanged.
