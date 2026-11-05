#!/usr/bin/env bash
# Take an automated 1280x720 screenshot of a map in -game mode (see ANEGameMode -NEScreenshot).
#   Tools/NonEuclidean/shot.sh <map name> <output name> [extra engine args...]
# Example:
#   Tools/NonEuclidean/shot.sh L_NE_PortalSpike corridor -NEPose=5000,0,150,90,0 -ExecCmds="Portal.MaxDepth 4"
# Writes Saved/Screenshots/NE/<output name>.png and prints the run's [NE] log lines.
# Resolution: RESX/RESY environment variables (default 1280x720).
set -euo pipefail
cd "$(dirname "$0")/../.."
MAP="$1"; OUT="$2"; shift 2
UE="/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
P="$(cygpath -m "$PWD")"
LOG="$(cygpath -m "${TEMP:-/tmp}")/ne_shot_${OUT}.log"
SRC="Saved/Screenshots/WindowsEditor/NE_${MAP}.png"
rm -f "$SRC" Saved/Screenshots/WindowsEditor/NE_"${MAP}"_[0-9p]*.png
MSYS_NO_PATHCONV=1 timeout 900 "$UE" "$P/NonEuclideanTest.uproject" "/Game/NonEuclidean/Maps/$MAP" \
  -game -windowed -ResX=${RESX:-1280} -ResY=${RESY:-720} -unattended -nosplash -NEScreenshot=2 "$@" -log -abslog="$LOG" >/dev/null 2>&1 || true
mkdir -p Saved/Screenshots/NE
if [ -f "$SRC" ]; then mv "$SRC" "Saved/Screenshots/NE/$OUT.png"; echo "saved Saved/Screenshots/NE/$OUT.png"; else echo "NO SCREENSHOT"; fi
if ls Saved/Screenshots/WindowsEditor/NE_"${MAP}"_[0-9p]*.png >/dev/null 2>&1; then  # -NESequence frames
  rm -rf "Saved/Screenshots/NE/$OUT"; mkdir -p "Saved/Screenshots/NE/$OUT"
  mv Saved/Screenshots/WindowsEditor/NE_"${MAP}"_[0-9p]*.png "Saved/Screenshots/NE/$OUT/"
  echo "sequence: $(ls "Saved/Screenshots/NE/$OUT" | wc -l) frames in Saved/Screenshots/NE/$OUT/"
fi
grep -a -e "\[NE\]" -e "LogPortalWorlds" -e "Error:" "$LOG" | sed 's/^\[[^]]*\]\[[^]]*\]//' | grep -v -e "Requested channels" -e "screenshot requested" -e "\] pawn=" -e "captures last frame" || true
