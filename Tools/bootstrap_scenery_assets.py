"""Generate sign textures/materials and instanced start lights inside Unreal."""
import runpy
from pathlib import Path

if __name__ == "__main__":
    helpers = runpy.run_path(str(Path(__file__).with_name("bootstrap_assets.py")), run_name="vroom_scenery")
    helpers["create_scenery_materials"]()
