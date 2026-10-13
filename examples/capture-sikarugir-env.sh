#!/bin/zsh
# Capture the environment a Sikarugir/Kegworks/Wineskin wrapper gives Wine, so examples/reclaimer-sikarugir.sh
# can launch programs inside the same bottle with D3DMetal, esync, etc.
#   examples/capture-sikarugir-env.sh ~/Applications/Sikarugir/Steam10.app
# Writes ~/.config/wine-glcore/sikarugir_env.txt (override with SIKARUGIR_ENV). Session/credential variables are stripped.
set -e
APP="${1:?usage: $0 /path/to/Wrapper.app}"
OUT="${SIKARUGIR_ENV:-$HOME/.config/wine-glcore/sikarugir_env.txt}"; mkdir -p "${OUT:h}"
open "$APP"
for i in {1..30}; do sleep 2; PID=$(pgrep -f 'wineserver|wine64-preloader|\.exe' | head -1); [ -n "$PID" ] && break; done
[ -n "$PID" ] || { echo "no Wine process appeared"; exit 1; }
ps -wwE -p "$PID" -o command= | tr ' ' '\n' | grep -E '^[A-Z_][A-Z0-9_]*=' \
  | grep -vE '^(SHLVL|PWD|OLDPWD|_|TMPDIR|XPC_|SSH_|LANG|LC_|TERM|HOME|USER|LOGNAME|SHELL|__CF|CLAUDE|ANTHROPIC|GIT_|AWS_|.*TOKEN|.*SECRET|.*KEY)=' > "$OUT"
echo "wrote $(wc -l < "$OUT") variables to $OUT"
