#!/bin/bash
# Same launcher as Desktop/Vroom.app; never select a build by its modification time.
set -euo pipefail
VROOM_LAUNCHER="$HOME/Applications/Vroom.app/Contents/MacOS/VroomLauncher"
if [[ ! -x "$VROOM_LAUNCHER" ]]; then
    printf '%s\n' 'Aucune version validée installée. Lancer Tools/install_mac.py --validation CHEMIN/results.json.' >&2
    exit 1
fi
exec "$VROOM_LAUNCHER" "$@"
