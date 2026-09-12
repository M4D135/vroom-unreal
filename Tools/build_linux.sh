#!/usr/bin/env bash
set -euo pipefail

VROOM_PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VROOM_ENGINE_DIR="${VROOM_UE_ROOT:-}"
VROOM_ARCHIVE_DIR=""
VROOM_MODE=package
VROOM_CONFIGURATION=Development

while [[ $# -gt 0 ]]; do
    case "$1" in
        --engine|--project-dir|--archive|--configuration|--mode)
            [[ $# -ge 2 ]] || { printf 'Valeur manquante : %s\n' "$1" >&2; exit 2; }
            case "$1" in
                --engine) VROOM_ENGINE_DIR="$2" ;;
                --project-dir) VROOM_PROJECT_DIR="$2" ;;
                --archive) VROOM_ARCHIVE_DIR="$2" ;;
                --configuration) VROOM_CONFIGURATION="$2" ;;
                --mode) VROOM_MODE="$2" ;;
            esac
            shift 2 ;;
        --help|-h)
            printf '%s\n' \
                'Usage: Tools/build_linux.sh --engine /path/UE_5.8 [options]' \
                '  --mode package|editor|assets     package par defaut' \
                '  --configuration Development|Shipping' \
                '  --project-dir /path/Vroom       racine du script par defaut' \
                '  --archive /path/output          Saved/Packages/Linux par defaut' \
                'VROOM_UE_ROOT remplace --engine. Aucun outil ne sera installe.' \
                'package conserve les assets versionnes ; assets les regenere explicitement.'
            exit 0 ;;
        *) printf 'Option inconnue : %s\n' "$1" >&2; exit 2 ;;
    esac
done

[[ "$VROOM_MODE" == package || "$VROOM_MODE" == editor || "$VROOM_MODE" == assets ]] || { printf '%s\n' 'Mode invalide.' >&2; exit 2; }
[[ "$VROOM_CONFIGURATION" == Development || "$VROOM_CONFIGURATION" == Shipping ]] || { printf '%s\n' 'Configuration invalide.' >&2; exit 2; }
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || { printf '%s\n' 'Ce build demande Linux x86_64.' >&2; exit 1; }
[[ -n "$VROOM_ENGINE_DIR" ]] || { printf '%s\n' 'Indiquer --engine ou VROOM_UE_ROOT (racine Unreal Engine 5.8).' >&2; exit 2; }
VROOM_PROJECT_DIR="$(cd "$VROOM_PROJECT_DIR" && pwd)"
VROOM_ENGINE_DIR="$(cd "$VROOM_ENGINE_DIR" && pwd)"
VROOM_PROJECT="$VROOM_PROJECT_DIR/Vroom.uproject"
VROOM_BUILD="$VROOM_ENGINE_DIR/Engine/Build/BatchFiles/Linux/Build.sh"
VROOM_UAT="$VROOM_ENGINE_DIR/Engine/Build/BatchFiles/RunUAT.sh"
VROOM_EDITOR="$VROOM_ENGINE_DIR/Engine/Binaries/Linux/UnrealEditor"
for VROOM_TOOL in "$VROOM_BUILD" "$VROOM_UAT" "$VROOM_EDITOR"; do
    [[ -x "$VROOM_TOOL" ]] || { printf 'Outil Linux Unreal absent : %s\n' "$VROOM_TOOL" >&2; exit 1; }
done
[[ -f "$VROOM_PROJECT" ]] || { printf 'Projet absent : %s\n' "$VROOM_PROJECT" >&2; exit 1; }
VROOM_VERSION="$VROOM_ENGINE_DIR/Engine/Build/Build.version"
if ! grep -Eq '"MajorVersion"[[:space:]]*:[[:space:]]*5[[:space:]]*,' "$VROOM_VERSION" || \
   ! grep -Eq '"MinorVersion"[[:space:]]*:[[:space:]]*8[[:space:]]*,' "$VROOM_VERSION"; then
    printf '%s\n' 'Le chemin moteur doit designer Unreal Engine 5.8.' >&2; exit 1
fi
if [[ -z "$VROOM_ARCHIVE_DIR" ]]; then VROOM_ARCHIVE_DIR="$VROOM_PROJECT_DIR/Saved/Packages/Linux";
elif [[ "$VROOM_ARCHIVE_DIR" != /* ]]; then VROOM_ARCHIVE_DIR="$VROOM_PROJECT_DIR/$VROOM_ARCHIVE_DIR"; fi
if [[ "$VROOM_MODE" == package ]]; then
    for VROOM_ASSET in Content/Maps/Circuit.umap Content/Materials/M_Asphalt.uasset Content/Data/tracks.json; do
        [[ -f "$VROOM_PROJECT_DIR/$VROOM_ASSET" ]] || { printf 'Asset absent : %s. Recuperer les assets ou lancer --mode assets.\n' "$VROOM_ASSET" >&2; exit 1; }
    done
fi

cd "$VROOM_PROJECT_DIR"
"$VROOM_BUILD" VroomEditor Linux Development "-Project=$VROOM_PROJECT" -WaitMutex -NoHotReloadFromIDE
if [[ "$VROOM_MODE" == assets ]]; then
    [[ -f Tools/bootstrap_assets.py ]] || { printf '%s\n' 'Generateur bootstrap absent.' >&2; exit 1; }
    mkdir -p Saved/Logs
    "$VROOM_EDITOR" "$VROOM_PROJECT" -run=pythonscript \
        "-script=$VROOM_PROJECT_DIR/Tools/bootstrap_assets.py" -unattended -nop4 -nosplash \
        "-abslog=$VROOM_PROJECT_DIR/Saved/Logs/VroomBootstrap-Linux.log"
    [[ -f Content/Maps/Circuit.umap ]] || { printf '%s\n' 'Generation de la carte incomplete.' >&2; exit 1; }
elif [[ "$VROOM_MODE" == package ]]; then
    mkdir -p "$VROOM_ARCHIVE_DIR"
    "$VROOM_UAT" BuildCookRun "-project=$VROOM_PROJECT" -nop4 -unattended -WaitForUATMutex \
        -platform=Linux "-clientconfig=$VROOM_CONFIGURATION" -build -cook -stage -pak -iostore \
        -package -archive "-archivedirectory=$VROOM_ARCHIVE_DIR" -utf8output
    VROOM_PACKAGED_LAUNCHER=""
    for VROOM_CANDIDATE in "$VROOM_ARCHIVE_DIR/Linux/Vroom.sh" "$VROOM_ARCHIVE_DIR/LinuxNoEditor/Vroom.sh" "$VROOM_ARCHIVE_DIR/Vroom.sh"; do
        if [[ -f "$VROOM_CANDIDATE" ]]; then VROOM_PACKAGED_LAUNCHER="$VROOM_CANDIDATE"; break; fi
    done
    [[ -n "$VROOM_PACKAGED_LAUNCHER" ]] || { printf 'UAT termine mais lanceur Vroom.sh absent dans %s.\n' "$VROOM_ARCHIVE_DIR" >&2; exit 1; }
    printf 'Package %s cree : %s\n' "$VROOM_CONFIGURATION" "$VROOM_PACKAGED_LAUNCHER"
else
    printf '%s\n' 'Module Editor construit. Utiliser run_linux.sh --editor pour jouer.'
fi
