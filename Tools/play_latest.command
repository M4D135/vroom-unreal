#!/bin/bash
# Lance le Vroom.app le plus récent, quel que soit le dossier du dernier package.
set -euo pipefail

VROOM_WIDTH="${VROOM_WIDTH:-1440}"
VROOM_HEIGHT="${VROOM_HEIGHT:-900}"

vroom_mtime() {
    local exe="$1/Contents/MacOS/Vroom"
    if [[ -x "$exe" ]]; then
        stat -f %m "$exe" 2>/dev/null || echo 0
    else
        echo 0
    fi
}

declare -a CANDIDATES=(
    "$HOME/Documents/Codex/2026-09-05/su/work/vroom-unreal/Saved/Packages/Mac/Vroom.app"
    "$HOME/Documents/Codex/2026-09-05/su/work/vroom-unreal/Binaries/Mac/Vroom.app"
    "$HOME/Documents/Codex/2026-09-05/su/outputs/Vroom-Mac/Mac/Vroom.app"
    "$HOME/Documents/Codex/2026-09-05/su/work/package-vroom-v02/Mac/Vroom.app"
    "$HOME/Desktop/Vroom.app"
)

while IFS= read -r -d '' app; do
    CANDIDATES+=("$app")
done < <(find "$HOME/Documents/Codex/2026-09-05/su" -maxdepth 6 -name Vroom.app -type d -print0 2>/dev/null)

BEST=""
BEST_M=0
SEEN=""
for app in "${CANDIDATES[@]}"; do
    [[ -n "$app" && -d "$app" ]] || continue
    resolved="$(cd "$app" 2>/dev/null && pwd -P)" || continue
    case "$SEEN" in
        *"|$resolved|"*) continue ;;
    esac
    SEEN="$SEEN|$resolved|"
    m="$(vroom_mtime "$resolved")"
    if [[ "$m" =~ ^[0-9]+$ ]] && (( m > BEST_M )); then
        BEST_M=$m
        BEST=$resolved
    fi
done

if [[ -z "$BEST" ]]; then
    /usr/bin/osascript -e 'display dialog "Aucun Vroom.app trouvé.\nCompiler avec Tools/build_mac.command --package." buttons {"OK"} default button 1 with title "Vroom"' >/dev/null
    exit 1
fi

# Un vieux process (souvent un package d’hier) ne doit pas rester au premier plan.
/usr/bin/pkill -x Vroom 2>/dev/null || true
sleep 0.25

STAMP="$(/bin/date -r "$BEST_M" '+%Y-%m-%d %H:%M' 2>/dev/null || echo "?")"
printf 'Vroom : %s (%s)\n' "$BEST" "$STAMP"

# UE 5.8 Development: retain the memory tracker while Cocoa starts up.
# Disabling it during an outstanding allocator scope can assert in PopTag.
exec /usr/bin/open -n "$BEST" --args -LLM -windowed "-ResX=$VROOM_WIDTH" "-ResY=$VROOM_HEIGHT" "$@"
