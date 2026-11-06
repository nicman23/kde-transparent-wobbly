#!/bin/bash
# Headless drag test of a window over a busy backdrop. KWin's log: tmp/wobbly/kwin.log
# Usage: run-wobbly.sh <gpuwobbly|wobblywindows> <outdir>
# env: KWINRC_EXTRA (appended to kwinrc), PLUGIN_PATH, Y0/Y1 and drive.py's DRAG_*/RELEASE_SHOTS
set -eu
EFFECT=$1 OUT=$2
HERE=$(dirname "$(realpath "$0")")
export T=$(dirname "$HERE")/tmp/wobbly
export XDG_CONFIG_HOME=$T/config
rm -rf "$XDG_CONFIG_HOME"; mkdir -p "$XDG_CONFIG_HOME" "$OUT"
cat > "$XDG_CONFIG_HOME/kwinrc" <<CFG
[Plugins]
gpuwobblyEnabled=$([ "$EFFECT" = gpuwobbly ] && echo true || echo false)
wobblywindowsEnabled=$([ "$EFFECT" = wobblywindows ] && echo true || echo false)
CFG
[ -n "${KWINRC_EXTRA:-}" ] && printf '%b\n' "$KWINRC_EXTRA" >> "$XDG_CONFIG_HOME/kwinrc"
export QT_PLUGIN_PATH=${PLUGIN_PATH:-$(dirname "$HERE")/build/bin}
export QT_LOGGING_RULES="kwin_effect_gpuwobbly.debug=true;kwin_scripting.debug=true;js.debug=true"
exec "$HERE/headless.sh" bash -c '
  /usr/lib/qt6/bin/qml '"$HERE"'/back.qml > "$T"/qml-back.log 2>&1 &
  sleep 1
  /usr/lib/qt6/bin/qml '"$HERE"'/front.qml > "$T"/qml-front.log 2>&1 &
  sleep 1.5
  id=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript '"$HERE"'/place.js place)
  qdbus6 org.kde.KWin /Scripting/Script$id org.kde.kwin.Script.run
  sleep 0.5
  qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadedEffects | tr "\n" " "; echo
  python3 '"$HERE"'/drive.py '"$OUT"' 550 '"${Y0:-215}"' 850 '"${Y1:-415}"'
'
