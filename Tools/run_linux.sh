#!/usr/bin/env bash
set -euo pipefail

VROOM_PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VROOM_ENGINE_DIR="${VROOM_UE_ROOT:-}"
VROOM_ARCHIVE_DIR=""
VROOM_EDITOR_MODE=false
VROOM_WIDTH=3840
VROOM_HEIGHT=2160
VROOM_WINDOW_MODE=-fullscreen
VROOM_GAME_ARGS=(-vulkan -NoSplash)

while [[ $# -gt 0 ]]; do
    case "$1" in
        --engine|--project-dir|--archive|--width|--height)
            [[ $# -ge 2 ]] || { printf 'Valeur manquante : %s\n' "$1" >&2; exit 2; }
            case "$1" in
                --engine) VROOM_ENGINE_DIR="$2" ;;
                --project-dir) VROOM_PROJECT_DIR="$2" ;;
                --archive) VROOM_ARCHIVE_DIR="$2" ;;
                --width) VROOM_WIDTH="$2" ;;
                --height) VROOM_HEIGHT="$2" ;;
            esac
            shift 2 ;;
        --editor) VROOM_EDITOR_MODE=true; shift ;;
        --windowed) VROOM_WINDOW_MODE=-windowed; shift ;;
        --)
            shift
            while [[ $# -gt 0 ]]; do
                case "$1" in
                    -ResX=*|-ResY=*|-resx=*|-resy=*|-windowed|-fullscreen|-d3d11|-d3d12|-dx11|-dx12|-opengl|-vulkan)
                        printf '%s\n' 'Utiliser --width/--height/--windowed ; ce lanceur Linux impose Vulkan.' >&2; exit 2 ;;
                esac
                VROOM_GAME_ARGS+=("$1"); shift
            done ;;
        --help|-h)
            printf '%s\n' \
                'Usage: Tools/run_linux.sh [--archive /path/package] [options] [-- arguments Unreal]' \
                '  --editor --engine /path/UE_5.8   lancer le module Editor existant, sans rebuild' \
                '  --project-dir /path/Vroom       racine du script par defaut' \
                '  --width 3840 --height 2160      resolution par defaut, Vulkan' \
                '  --windowed                     fenetre au lieu du plein ecran' \
                'Package par defaut : Saved/Packages/Linux. VROOM_UE_ROOT remplace --engine.'
            exit 0 ;;
        *) printf 'Option inconnue : %s. Placer les arguments Unreal apres --.\n' "$1" >&2; exit 2 ;;
    esac
done

[[ "$VROOM_WIDTH" =~ ^[1-9][0-9]*$ && "$VROOM_HEIGHT" =~ ^[1-9][0-9]*$ ]] || { printf '%s\n' 'Resolution invalide.' >&2; exit 2; }
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || { printf '%s\n' 'Ce lanceur demande Linux x86_64.' >&2; exit 1; }
VROOM_PROJECT_DIR="$(cd "$VROOM_PROJECT_DIR" && pwd)"
if [[ "$VROOM_EDITOR_MODE" == true ]]; then
    [[ -n "$VROOM_ENGINE_DIR" ]] || { printf '%s\n' 'Indiquer --engine ou VROOM_UE_ROOT.' >&2; exit 2; }
    VROOM_ENGINE_DIR="$(cd "$VROOM_ENGINE_DIR" && pwd)"
    VROOM_EXECUTABLE="$VROOM_ENGINE_DIR/Engine/Binaries/Linux/UnrealEditor"
    for VROOM_REQUIRED in Vroom.uproject Binaries/Linux/libUnrealEditor-Vroom.so Content/Maps/Circuit.umap; do
        [[ -f "$VROOM_PROJECT_DIR/$VROOM_REQUIRED" ]] || { printf 'Fichier absent : %s. Preparer le projet avec build_linux.sh.\n' "$VROOM_REQUIRED" >&2; exit 1; }
    done
    [[ -x "$VROOM_EXECUTABLE" ]] || { printf 'Executable absent : %s\n' "$VROOM_EXECUTABLE" >&2; exit 1; }
    cd "$VROOM_PROJECT_DIR"
    exec "$VROOM_EXECUTABLE" "$VROOM_PROJECT_DIR/Vroom.uproject" -game \
        "-ResX=$VROOM_WIDTH" "-ResY=$VROOM_HEIGHT" "$VROOM_WINDOW_MODE" "${VROOM_GAME_ARGS[@]}"
fi
if [[ -z "$VROOM_ARCHIVE_DIR" ]]; then VROOM_ARCHIVE_DIR="$VROOM_PROJECT_DIR/Saved/Packages/Linux";
elif [[ "$VROOM_ARCHIVE_DIR" != /* ]]; then VROOM_ARCHIVE_DIR="$VROOM_PROJECT_DIR/$VROOM_ARCHIVE_DIR"; fi
VROOM_EXECUTABLE=""
for VROOM_CANDIDATE in "$VROOM_ARCHIVE_DIR/Linux/Vroom.sh" "$VROOM_ARCHIVE_DIR/LinuxNoEditor/Vroom.sh" "$VROOM_ARCHIVE_DIR/Vroom.sh"; do
    if [[ -f "$VROOM_CANDIDATE" ]]; then VROOM_EXECUTABLE="$VROOM_CANDIDATE"; break; fi
done
[[ -n "$VROOM_EXECUTABLE" ]] || { printf 'Package absent dans %s. Lancer build_linux.sh ou utiliser --editor.\n' "$VROOM_ARCHIVE_DIR" >&2; exit 1; }
cd "$(dirname "$VROOM_EXECUTABLE")"
exec bash "$VROOM_EXECUTABLE" "-ResX=$VROOM_WIDTH" "-ResY=$VROOM_HEIGHT" "$VROOM_WINDOW_MODE" "${VROOM_GAME_ARGS[@]}"
