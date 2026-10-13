#!/bin/zsh
# Launch Project Reclaimer (https://projectreclaimer.dev) inside a Sikarugir Steam bottle with the interposer.
# Prereqs: project-reclaimer.exe copied to <prefix>/drive_c/ProjectReclaimer/, Halo 3 installed in the bottle's Steam MCC,
#          examples/capture-sikarugir-env.sh run once, `make` run in the repo root.
# Detached from the calling shell; log goes to ~/Library/Logs/ProjectReclaimer/. Set WRAPPER to your .app if not Steam10.
set -e
HERE="${0:A:h}/.."
W="${WRAPPER:-$HOME/Applications/Sikarugir/Steam10.app}"
LOGDIR="$HOME/Library/Logs/ProjectReclaimer"; mkdir -p "$LOGDIR"
LOG="$LOGDIR/reclaimer-$(date +%Y%m%d-%H%M%S).log"
set -a; while IFS= read -r line; do export "$line"; done < "${SIKARUGIR_ENV:-$HOME/.config/wine-glcore/sikarugir_env.txt}"; set +a
export GLCORE_INTERPOSE=1
export WINEDEBUG="${WINEDEBUG_OVERRIDE:-err+all}"
cd "$W/Contents/SharedSupport/prefix/drive_c/ProjectReclaimer"
echo "launch $(date)" > "$LOG"
# Own session so a terminal/tool timeout cannot kill the game. macOS strips DYLD_* from the
# system python's environment, so the interposer path is passed as an argument and re-added here.
/usr/bin/python3 - "$W/Contents/SharedSupport/wine/bin/wine" "$LOG" "$HERE/libglcore_interpose.dylib" <<'PY' &
import os, sys, subprocess
wine, log, dylib = sys.argv[1:4]
os.setsid()
env = dict(os.environ); env["DYLD_INSERT_LIBRARIES"] = dylib
with open(log, "ab") as f:
    p = subprocess.Popen([wine, r"C:\ProjectReclaimer\project-reclaimer.exe", "game-client"], stdout=f, stderr=subprocess.STDOUT, env=env)
    sys.exit(p.wait())
PY
disown
echo "started; log: $LOG"
# A process started from a shell is not allowed to take focus, so the game window would open
# behind Steam. Wait for the engine worker (owner of the "Project Reclaimer - Player" window) and raise it.
for i in {1..45}; do
  sleep 2
  PID=$(pgrep -f 'project-reclaimer.exe probe-engine-worker' | head -1)
  if [ -n "$PID" ]; then
    osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $PID) to true" >/dev/null 2>&1 && echo "raised game window (pid $PID)" && break
  fi
done
