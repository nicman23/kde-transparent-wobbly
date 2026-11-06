#!/bin/bash
# Opens an effect's settings page in headless KWin and screenshots it.
# Usage: kcm-shot.sh <kwin_gpuwobbly_config|kwin_gpublur_config> <out.png>
set -eu
MODULE=$1 OUT=$(realpath "$2")
HERE=$(dirname "$(realpath "$0")")
export T=$(dirname "$HERE")/tmp/kcm
export XDG_CONFIG_HOME=$T/config QT_PLUGIN_PATH=${PLUGIN_PATH:-$(dirname "$HERE")/build/bin}
mkdir -p "$XDG_CONFIG_HOME"
exec "$HERE/headless.sh" bash -c '
  kcmshell6 kwin/effects/configs/'"$MODULE"' > "$T"/kcm.log 2>&1 &
  sleep 3
  python3 -I -c "
import dbus, os, sys
from PIL import Image
bus = dbus.SessionBus()
s = dbus.Interface(bus.get_object(\"org.kde.KWin\", \"/org/kde/KWin/ScreenShot2\"), \"org.kde.KWin.ScreenShot2\")
r, w = os.pipe(); m = s.CaptureWorkspace({}, dbus.types.UnixFd(w)); os.close(w)
d = b\"\"
while c := os.read(r, 1 << 20): d += c
raw = {4: \"BGRA\", 5: \"BGRA\", 6: \"BGRA\", 16: \"RGBA\", 17: \"RGBA\", 18: \"RGBA\"}[int(m[\"format\"])]
Image.frombuffer(\"RGBA\", (int(m[\"width\"]), int(m[\"height\"])), d, \"raw\", raw, int(m[\"stride\"]), 1).convert(\"RGB\").save(sys.argv[1])
" '"$OUT"'
'
