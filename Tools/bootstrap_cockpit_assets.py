"""Generate the cockpit materials without regenerating the scene assets.

Run inside Unreal Editor 5.8:
  UnrealEditor-Cmd Vroom.uproject -run=pythonscript -script=/absolute/path/to/this.py
"""

import runpy
from pathlib import Path


if __name__ == "__main__":
    # A path-based load also works when Unreal's Python path omits Tools. The
    # distinct run name leaves bootstrap_assets.main (and the map) untouched.
    generator = runpy.run_path(
        str(Path(__file__).resolve().with_name("bootstrap_assets.py")),
        run_name="vroom_cockpit_asset_generator",
    )
    generator["create_cockpit_materials"]()
