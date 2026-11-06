#!/bin/bash
# Headless blur test: a translucent Konsole asking for blur, over a busy backdrop. KWin's log: tmp/blur/kwin.log
# Usage: run-blur.sh <outdir> <effect,...>   effects to enable, e.g. gpublur or blur or "gpublur,gpuwobbly"
# Without DRAG=1 it only screenshots; with DRAG=1 it drags the window, with RIGHT_CLICK=1 it opens a
# context menu. env: KWINRC_EXTRA (appended to kwinrc), CLIENT=dolphin, KVANTUM_NO_BLUR=1
set -eu
OUT=$1 EFFECTS=$2
HERE=$(dirname "$(realpath "$0")")
export T=$(dirname "$HERE")/tmp/blur
export XDG_CONFIG_HOME=$T/config XDG_DATA_HOME=$T/data
rm -rf "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"; mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME/konsole" "$OUT"
{
    echo "[Plugins]"
    for e in blur gpublur gpuwobbly wobblywindows; do
        [[ ",$EFFECTS," == *",$e,"* ]] && echo "${e}Enabled=true" || echo "${e}Enabled=false"
    done
} > "$XDG_CONFIG_HOME/kwinrc"
[ -n "${KWINRC_EXTRA:-}" ] && printf '%b\n' "$KWINRC_EXTRA" >> "$XDG_CONFIG_HOME/kwinrc"
cat > "$XDG_DATA_HOME/konsole/Test.colorscheme" <<CS
[Background]
Color=20,20,30
[Foreground]
Color=230,230,230
[General]
Blur=true
Opacity=0.35
CS
cat > "$XDG_DATA_HOME/konsole/Test.profile" <<PR
[Appearance]
ColorScheme=Test
[General]
Name=Test
PR
# CLIENT=dolphin: Dolphin with the translucent Kvantum theme KvRoughGlass, as in a Kvantum setup
CLIENT=${CLIENT:-konsole}
if [ "$CLIENT" = dolphin ]; then
    mkdir -p "$XDG_CONFIG_HOME/Kvantum"
    printf '[General]\ntheme=KvRoughGlass\n' > "$XDG_CONFIG_HOME/Kvantum/kvantum.kvconfig"
    if [ "${KVANTUM_NO_BLUR:-0}" = 1 ]; then
        # a translucent theme that does not ask KWin for blur
        mkdir -p "$XDG_CONFIG_HOME/Kvantum/KvRoughGlass"
        sed 's/^blurring=true/blurring=false/; s/^popup_blurring=true/popup_blurring=false/' \
            /usr/share/Kvantum/KvRoughGlass/KvRoughGlass.kvconfig > "$XDG_CONFIG_HOME/Kvantum/KvRoughGlass/KvRoughGlass.kvconfig"
    fi
    export QT_STYLE_OVERRIDE=kvantum
    CLIENT_CMD="dolphin --new-window $HOME"
else
    CLIENT_CMD="konsole --profile Test --nofork"
fi
export CLIENT_CMD
export QT_PLUGIN_PATH=${PLUGIN_PATH:-$(dirname "$HERE")/build/bin}
export QT_LOGGING_RULES="kwin_effect_gpublur.debug=true;kwin_effect_gpuwobbly.debug=true;js.debug=true"
exec "$HERE/headless.sh" bash -c '
  /usr/lib/qt6/bin/qml '"$HERE"'/back.qml > /dev/null 2>&1 &
  sleep 1
  $CLIENT_CMD > "$T"/client.log 2>&1 &
  sleep 2.5
  id=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript '"$HERE"'/place.js place)
  qdbus6 org.kde.KWin /Scripting/Script$id org.kde.kwin.Script.run
  sleep 0.7
  qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadedEffects | tr "\n" " "; echo
  if [ "${RIGHT_CLICK:-0}" = 1 ]; then
    python3 '"$HERE"'/drive.py '"$OUT"' 700 450 700 450
  elif [ "${DRAG:-0}" = 1 ]; then
    python3 '"$HERE"'/drive.py '"$OUT"' 600 215 750 315
  else
    DRAG_STEPS=0 RELEASE_SHOTS=0 python3 '"$HERE"'/drive.py '"$OUT"' 1270 790 1270 790
  fi
'
