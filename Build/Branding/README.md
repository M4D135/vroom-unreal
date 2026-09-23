# Shared application icon

`Vroom.png` is the source artwork for the desktop icon: a red 1990s open-wheel
car on a dark rounded tile, with transparent outer corners. No game screenshot,
third-party logo, or car badge is used.

The same artwork supplies every platform:

- Windows: `Build/Windows/Application.ico` (16, 24, 32, 48, 64, 128, 256 px).
  Unreal uses this path for newly packaged executable icons. Existing desktop
  shortcuts can also reference a separately installed copy of this ICO.
- Mac: `Build/Mac/Application.icns` (up to 1024 px). Unreal packages this icon;
  `Tools/install_mac.py` also embeds it in the desktop launcher bundle.
- Linux: `Build/Linux/Vroom.png` (512 px), ready for the future desktop launcher.
  A Linux installation has not yet been validated.

Regenerate the committed exports with Python and Pillow:

```sh
python3 Tools/export_app_icons.py
```

Pillow 12.3.0 was used for the initial exports. This command only resizes and
encodes the source image; building the game does not require Pillow.

## Artwork provenance

Created on 2026-09-23 with the built-in image generation tool, in **generate**
mode, with no reference images. Original output: 1254 × 1254 RGBA PNG, saved
unchanged as `Build/Branding/Vroom.png`.

Prompt:

> Use case: logo-brand. Create ONE finished desktop app icon for a vintage 1990s open-wheel racing game. Square 1024x1024 composition. Premium polished 3D miniature of a scarlet-red 1991-style single-seat formula racing car, no modern halo, open cockpit, four broad black slick tires, simple front and rear wings. Dynamic three-quarter view from above, pointed diagonally toward lower left. The red car is large and bold, fills about 75% of a dark charcoal / midnight-blue rounded-square enamel tile, with restrained ivory highlight and soft studio rim lighting; the car's silhouette must remain very clear at desktop icon sizes of 32 and 48 px. Tasteful dimensional shading and crisp edges, high contrast, purposeful professional racing-game identity rather than busy illustration. Rounded-square tile centered with about 5% genuinely transparent margin outside the tile and transparent rounded corners. No scenery, no text, no letters, no brand logos, no badges, no flags, no watermarks, no multiple variants, no icon sheet. Deliver a production icon, not a mockup of a desktop or device.
