# Windows startup validation — 2026-09-23

These two scripts are archived unchanged from the visible validation campaign
for package `Win64-20260923-171119` (build 011). They run five targeted checks:
normal startup, first rendered frames, menu flow, starting sequence and frame
timing. The campaign passed 5/5; see `docs/WINDOWS.md` for measurements and the
report hash. This is not the full gameplay suite or the six-circuit soak.

Keep the PowerShell script and its Python analyzer together. The runner targets
the logged-in Windows desktop session **2**, an **RTX 5060**, **1920 × 1080**,
**Ultra**, **60 FPS**. Its default paths refer to the original test machine;
inspect its parameters before reuse. Python imports the normal verification
helpers from the explicitly supplied `--project-dir`.

Run only when no other Vroom game or validation process is active. The runner
refuses to overlap an existing game, verifies the immutable package before and
after testing, and writes diagnostics outside the package. Launch it from the
interactive desktop, not a headless session. Review the resulting screenshots
visually as well as the automated checks.

Raw logs, native packages and screenshots remain local. The maintained full
validation entry point is `Tools/verify_windows.ps1`; this folder preserves the
exact helpers used for the narrower startup campaign.
