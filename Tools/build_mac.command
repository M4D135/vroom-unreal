#!/bin/bash
set -euo pipefail

VROOM_PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VROOM_ENGINE_DIR="${VROOM_UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
VROOM_MODE=editor
VROOM_ARCHIVE_DIR="$VROOM_PROJECT_DIR/Saved/Packages"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --engine|--archive)
            if [[ $# -lt 2 ]]; then
                printf 'Erreur : %s demande un chemin.\n' "$1" >&2
                exit 2
            fi
            if [[ "$1" == --engine ]]; then VROOM_ENGINE_DIR="$2"; else VROOM_ARCHIVE_DIR="$2"; fi
            shift 2
            ;;
        --editor) VROOM_MODE=editor; shift ;;
        --assets) VROOM_MODE=assets; shift ;;
        --package) VROOM_MODE=package; shift ;;
        --help|-h)
            printf '%s\n' \
                'Usage : Tools/build_mac.command [--editor|--assets|--package] [--engine /chemin/UE_5.8]' \
                '  --editor  Compile VroomEditor puis génère les matériaux, textures et la carte (défaut).' \
                '  --assets  Génère seulement les assets ; le module Editor doit être déjà compilé.' \
                '  --package Compile, génère les assets, puis crée une application Mac Development.' \
                '  --archive /chemin  Destination du package, défaut Saved/Packages.' \
                'VROOM_UE_ROOT peut aussi indiquer le moteur. Fermer le jeu et l’éditeur avant ce script.'
            exit 0
            ;;
        *) printf 'Argument inconnu : %s\nUtiliser --help.\n' "$1" >&2; exit 2 ;;
    esac
done

VROOM_PROJECT="$VROOM_PROJECT_DIR/Vroom.uproject"
VROOM_BUILD="$VROOM_ENGINE_DIR/Engine/Build/BatchFiles/Mac/Build.sh"
VROOM_EDITOR_CMD="$VROOM_ENGINE_DIR/Engine/Binaries/Mac/UnrealEditor-Cmd"
VROOM_UAT="$VROOM_ENGINE_DIR/Engine/Build/BatchFiles/RunUAT.sh"
for VROOM_TOOL in "$VROOM_BUILD" "$VROOM_EDITOR_CMD" "$VROOM_UAT"; do
    if [[ ! -x "$VROOM_TOOL" ]]; then
        printf 'Outil Unreal introuvable : %s\nIndiquer le moteur avec --engine ou VROOM_UE_ROOT.\n' "$VROOM_TOOL" >&2
        exit 1
    fi
done
if [[ ! -f "$VROOM_PROJECT" || ! -f "$VROOM_PROJECT_DIR/Tools/bootstrap_assets.py" ]]; then
    printf '%s\n' 'Le projet ou Tools/bootstrap_assets.py est introuvable.' >&2
    exit 1
fi

cd "$VROOM_PROJECT_DIR"
mkdir -p "$VROOM_PROJECT_DIR/Saved/Logs"
if [[ "$VROOM_MODE" != assets ]]; then
    printf '%s\n' 'Compilation de VroomEditor (Mac Development)…'
    "$VROOM_BUILD" VroomEditor Mac Development "$VROOM_PROJECT" -WaitMutex -NoHotReloadFromIDE -NoUBA
elif [[ ! -f "$VROOM_PROJECT_DIR/Binaries/Mac/libUnrealEditor-Vroom.dylib" ]]; then
    printf '%s\n' 'Compiler le module avec Tools/build_mac.command avant --assets.' >&2
    exit 1
fi

printf '%s\n' 'Génération des matériaux et de la carte…'
"$VROOM_EDITOR_CMD" "$VROOM_PROJECT" -run=pythonscript \
    "-script=$VROOM_PROJECT_DIR/Tools/bootstrap_assets.py" -unattended -nop4 -nosplash -NullRHI -NoSound \
    "-abslog=$VROOM_PROJECT_DIR/Saved/Logs/VroomBootstrap.log"
if [[ ! -f "$VROOM_PROJECT_DIR/Content/Maps/Circuit.umap" || ! -f "$VROOM_PROJECT_DIR/Content/Materials/M_Asphalt.uasset" ]]; then
    printf '%s\n' 'Génération incomplète. Consulter Saved/Logs/VroomBootstrap.log.' >&2
    exit 1
fi

if [[ "$VROOM_MODE" == package ]]; then
    printf '%s\n' 'Compilation et préparation de l’application Mac…'
    "$VROOM_UAT" BuildCookRun "-project=$VROOM_PROJECT" -nop4 -unattended -WaitForUATMutex \
        -platform=Mac -clientconfig=Development -build -cook -stage -pak -iostore -package -archive \
        "-archivedirectory=$VROOM_ARCHIVE_DIR" -utf8output | tee "$VROOM_PROJECT_DIR/Saved/Logs/VroomPackage-UAT.log"
    if /usr/bin/grep -q 'Failed to compile Material' "$VROOM_PROJECT_DIR/Saved/Logs/VroomPackage-UAT.log"; then
        printf '%s\n' 'Échec : un matériau du package ne compile pas. Consulter Saved/Logs/VroomPackage-UAT.log.' >&2
        exit 1
    fi
    printf 'Package créé dans : %s\n' "$VROOM_ARCHIVE_DIR"
    printf '%s\n' 'La version installée reste active. Valider avec Tools/verify_mac.py --visible --graphics-preset performance --profile --soak, puis Tools/install_mac.py --validation CHEMIN/results.json.'

else
    printf '%s\n' 'Prêt. Double-cliquer Tools/run_mac.command ou Desktop/Vroom.command pour jouer.'
fi
