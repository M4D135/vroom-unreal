# Valve glove texture sources

Copyright (c), Valve Corporation. The attached [LICENSE](LICENSE) applies.

Source: [ValveSoftware/openxr_engine_plugins — valve_hand_models](https://github.com/ValveSoftware/openxr_engine_plugins/tree/main/assets/valve_hand_models), file `openxr_glove_left_model_slim.glb`, retrieved 2026-09-06.

GLB SHA-256: `135189e6a2caab7d5fa0182feda4c2c7b1a9485bc5a7b27b0fe7b5b5ed7e33eb`.

These are the untouched embedded albedo JPEG and tangent-space normal PNG; the full GLB is deliberately not duplicated in the project.

- `ValveGloveColor.jpg`: SHA-256 `a424f719450ca9bce29ba28e90712dab4e40b56e67642150379b5359475eb868`; 4096 × 4096, original embedded bytes.
- `ValveGloveNormal.png`: SHA-256 `bde8ee08ffd7b3437d16a23206a826b6504f8e91587093a02a84d710e2994767`; 4096 × 4096, original embedded bytes.

`Tools/bootstrap_hand_assets.py` imports `T_RacingGloveColor` (sRGB) and `T_RacingGloveNormal` (linear normal compression, green channel inverted for Unreal). Both retain their 4K source and use a 2048 maximum runtime texture size. `M_RacingGlove` recolors red panels to ivory in its shader, retaining the original dark panels, stitches and texture detail. Metallic 0, roughness 0.8. No source pixels were repainted.
