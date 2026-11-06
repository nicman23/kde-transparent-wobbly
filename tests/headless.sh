#!/bin/bash
# Run headless KWin (virtual backend) on a private D-Bus, then run "$@" inside that session.
# Usage: T=<scratch dir> headless.sh <cmd...>   (env QT_PLUGIN_PATH etc. pass through)
# KWin's log goes to $T/kwin.log.
set -u
export XDG_RUNTIME_DIR=$T/xdg; mkdir -p -m700 "$XDG_RUNTIME_DIR"
export QT_FORCE_STDERR_LOGGING=1
exec dbus-run-session -- bash -c '
  kwin_wayland --virtual --no-lockscreen --socket wl-test --width 1280 --height 800 > "$T"/kwin.log 2>&1 &
  K=$!
  for i in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/wl-test" ] && qdbus6 org.kde.KWin /KWin >/dev/null 2>&1 && break; sleep 0.2; done
  export WAYLAND_DISPLAY=wl-test QT_QPA_PLATFORM=wayland
  "$@"; rc=$?
  kill $K; wait $K 2>/dev/null; exit $rc' _ "$@"
