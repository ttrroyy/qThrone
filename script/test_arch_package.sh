#!/bin/bash
set -euo pipefail
logdir="${1:?Log directory required}"
export XDG_RUNTIME_DIR="$(mktemp -d)"
chmod 700 "$XDG_RUNTIME_DIR"
trap 'rm -rf "$XDG_RUNTIME_DIR"' EXIT

# Verify a real visible X11 window, not just a surviving background process.
xvfb-run -a dbus-run-session -- bash -c '
  set -euo pipefail
  rawline > "$1/x11-launch.log" 2>&1 &
  app_pid=$!
  trap "kill $app_pid 2>/dev/null || true" EXIT
  for attempt in {1..30}; do
    kill -0 "$app_pid"
    if xdotool search --onlyvisible --class Rawline > "$1/x11-windows.log"; then
      sleep 3
      kill -0 "$app_pid"
      exit 0
    fi
    sleep 1
  done
  cat "$1/x11-launch.log"
  exit 1
' _ "$logdir"

# Test the bundled Wayland plugin against a native headless compositor.
weston --backend=headless --renderer=pixman --socket=wayland-test --idle-time=0 \
  > "$logdir/wayland-compositor.log" 2>&1 &
compositor_pid=$!
app_pid=''
cleanup() {
  if [[ -n "$app_pid" ]]; then kill "$app_pid" 2>/dev/null || true; fi
  kill "$compositor_pid" 2>/dev/null || true
  rm -rf "$XDG_RUNTIME_DIR"
}
trap cleanup EXIT
for attempt in {1..30}; do
  kill -0 "$compositor_pid"
  [[ -S "$XDG_RUNTIME_DIR/wayland-test" ]] && break
  sleep 1
done
test -S "$XDG_RUNTIME_DIR/wayland-test"
WAYLAND_DISPLAY=wayland-test QT_QPA_PLATFORM=wayland dbus-run-session -- rawline \
  > "$logdir/wayland-launch.log" 2>&1 &
app_pid=$!
sleep 10
kill -0 "$app_pid"
if grep -E 'Could not load the Qt platform plugin|Failed to create.*display|Cannot load library' "$logdir/wayland-launch.log"; then
  exit 1
fi
