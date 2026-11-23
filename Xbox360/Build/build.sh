#!/bin/bash
# Strike Trilogy X360: builds DesertStrike.xex, JungleStrike.xex and UrbanStrike.xex in WSL.
#   bash Xbox360/Build/build.sh            all three
#   bash Xbox360/Build/build.sh jungle     one game
# Needs the recompiled C first (Tools\gen_all.bat on Windows, or the same python commands).
# make cannot take paths with spaces: the project is reached through ~/strike-trilogy.
set -e
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
LINK="$HOME/strike-trilogy"
ln -sfn "$SRC" "$LINK"          # always this copy of the project
cd "$LINK/Xbox360/Build"
GAMES="${*:-desert jungle urban}"
[ "$GAMES" = "all" ] && GAMES="desert jungle urban"
declare -A DIR=([desert]=DesertStrike [jungle]=JungleStrike [urban]=UrbanStrike)
for g in $GAMES; do
    d=${DIR[$g]}
    if ! ls "$LINK/Reference/StrikeHDCollection/generated/$g/recomp_"*.c >/dev/null 2>&1; then
        echo "$g: generated/$g is empty, run Tools\\gen_all.bat first"; exit 1
    fi
    echo "== $d"
    make -f Makefile.x360 objs -j"$(nproc)" GAME=$g ROOT="$LINK" 2>&1 | grep -E "error|warning: implicit" || true
    make -f Makefile.x360 xex GAME=$g ROOT="$LINK" 2>&1 | grep -v "^    -T\|^    --" | tail -6
    out="$LINK/Xbox360/Build/out/xex/$d.xex"
    [ -f "$out" ] || { echo "$d: no XEX"; exit 1; }
    mkdir -p "$LINK/Builds/$d/Deploy/$d/rom"
    cp "$out" "$LINK/Builds/$d/$d.xex"
    cp "$out" "$LINK/Builds/$d/Deploy/$d/default.xex"
    ls -la "$LINK/Builds/$d/$d.xex"
done
