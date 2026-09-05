#!/bin/bash
set -euo pipefail

VROOM_PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VROOM_ENGINE_DIR="${VROOM_UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
VROOM_EXTRA_ARGS=(-NoSplash)
VROOM_WIDTH=1440
VROOM_HEIGHT=900

while [[ $# -gt 0 ]]; do
    case "$1" in
        --engine)
            if [[ $# -lt 2 ]]; then
                printf '%s\n' 'Erreur : --engine demande le chemin du moteur.' >&2
                exit 2
            fi
            VROOM_ENGINE_DIR="$2"
            shift 2
            ;;
        -ResX=*|-resx=*) VROOM_WIDTH="${1#*=}"; shift ;;
        -ResY=*|-resy=*) VROOM_HEIGHT="${1#*=}"; shift ;;
        --help|-h)
            printf '%s\n' \
                'Usage : Tools/run_mac.command [--engine /chemin/UE_5.8] [arguments Unreal]' \
                'Lance le jeu dans Unreal Editor, en fenêtre 1440 × 900, sans compilation.' \
                'VROOM_UE_ROOT peut aussi indiquer le dossier du moteur.' \
                'Exemple 4K : Tools/run_mac.command -ResX=3840 -ResY=2160'
            exit 0
            ;;
        *) VROOM_EXTRA_ARGS+=("$1"); shift ;;
    esac
done

if [[ ! "$VROOM_WIDTH" =~ ^[1-9][0-9]*$ || ! "$VROOM_HEIGHT" =~ ^[1-9][0-9]*$ ]]; then
    printf '%s\n' 'La largeur et la hauteur doivent être des nombres entiers positifs.' >&2
    exit 2
fi

VROOM_EDITOR="$VROOM_ENGINE_DIR/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
if [[ ! -x "$VROOM_EDITOR" ]]; then
    printf 'Unreal Editor introuvable : %s\nIndiquer le moteur avec --engine ou VROOM_UE_ROOT.\n' "$VROOM_EDITOR" >&2
    exit 1
fi
if [[ ! -f "$VROOM_PROJECT_DIR/Binaries/Mac/libUnrealEditor-Vroom.dylib" ]]; then
    printf '%s\n' 'Le module VroomEditor manque. Lancer Tools/build_mac.command une première fois.' >&2
    exit 1
fi
if [[ ! -f "$VROOM_PROJECT_DIR/Content/Maps/Circuit.umap" || ! -f "$VROOM_PROJECT_DIR/Content/Materials/M_Asphalt.uasset" ]]; then
    printf '%s\n' 'La carte ou les matériaux manquent. Lancer Tools/build_mac.command --assets.' >&2
    exit 1
fi

cd "$VROOM_PROJECT_DIR"
printf '%s\n' 'Vroom : Entrée pour démarrer, flèches ou ZQSD pour conduire, Échap pour le menu.'
exec "$VROOM_EDITOR" "$VROOM_PROJECT_DIR/Vroom.uproject" \
    -game -windowed "-ResX=$VROOM_WIDTH" "-ResY=$VROOM_HEIGHT" "${VROOM_EXTRA_ARGS[@]}"
