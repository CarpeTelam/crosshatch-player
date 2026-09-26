#!/usr/bin/env bash
# Build, launch, and drive the CrossPoint desktop simulator headlessly.
# Run from the repo root. State (pids, log, screenshots) lives in build/sim/.
set -euo pipefail

SKILL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "$SKILL_DIR" rev-parse --show-toplevel)"
cd "$ROOT"

STATE=build/sim
SHOTS=$STATE/shots
DISP="${SIM_DISPLAY:-:77}"
MARK_BEGIN="; >>> crosshatch simulator (managed by .claude/skills/run-crosshatch-player/sim.sh setup)"
MARK_END="; <<< crosshatch simulator"

die() { echo "sim: $*" >&2; exit 1; }

env_for() {
  case "${1:-x4pro}" in
    x4pro) echo simulator_x4pro ;;
    sticky) echo simulator_sticky ;;
    x4) echo simulator ;;
    *) die "unknown device '$1' (x4pro | sticky | x4)" ;;
  esac
}

window() {
  DISPLAY=$DISP xdotool search --name '^Simulator' 2>/dev/null | head -1
}

need_window() {
  local w
  w=$(window) || true
  [ -n "$w" ] || die "no simulator window on $DISP; run '$0 start' first"
  echo "$w"
}

cmd_setup() {
  local local_ini=platformio.local.ini tmp
  touch "$local_ini"
  tmp=$(mktemp)
  # Replace any previous managed block, keep the user's own settings.
  awk -v b="$MARK_BEGIN" -v e="$MARK_END" '$0==b{skip=1;next} $0==e{skip=0;next} !skip' "$local_ini" > "$tmp"
  { cat "$tmp"; echo "$MARK_BEGIN"; cat "$SKILL_DIR/simulator.ini"; echo "$MARK_END"; } > "$local_ini"
  rm -f "$tmp"
  mkdir -p fs_/books "$SHOTS"
  if [ -z "$(ls -A fs_/books)" ]; then
    cp test/epubs/test_kerning_ligature.epub fs_/books/
    echo "sim: seeded fs_/books/ with test_kerning_ligature.epub"
  fi
  echo "sim: simulator envs installed into $local_ini"
}

cmd_build() {
  pio run -e "$(env_for "${1:-}")" -j "$(nproc)"
}

cmd_start() {
  local env bin
  env=$(env_for "${1:-}")
  bin=.pio/build/$env/program
  [ -x "$bin" ] || die "$bin missing; run '$0 build ${1:-x4pro}'"
  cmd_stop >/dev/null 2>&1 || true
  mkdir -p "$STATE" "$SHOTS"
  Xvfb "$DISP" -screen 0 1280x1024x24 >"$STATE/xvfb.log" 2>&1 &
  echo $! > "$STATE/xvfb.pid"
  sleep 0.5
  DISPLAY=$DISP "$bin" >"$STATE/sim.log" 2>&1 &
  echo $! > "$STATE/sim.pid"
  for _ in $(seq 1 50); do
    if [ -n "$(window)" ]; then
      sleep 1 # let Boot hand over to Home before the first input
      echo "sim: $env running on $DISP ($(cmd_geom))"
      return 0
    fi
    sleep 0.2
  done
  tail -20 "$STATE/sim.log" >&2
  die "window never appeared"
}

cmd_stop() {
  local f
  for f in sim xvfb; do
    if [ -f "$STATE/$f.pid" ]; then
      kill "$(cat "$STATE/$f.pid")" 2>/dev/null || true
      rm -f "$STATE/$f.pid"
    fi
  done
  echo "sim: stopped"
}

cmd_geom() {
  local w
  w=$(need_window)
  DISPLAY=$DISP xdotool getwindowname "$w" | tr '\n' ' '
  DISPLAY=$DISP xdotool getwindowgeometry "$w" | sed -n 's/.*Geometry: //p'
}

# Coordinates are window pixels, which equal the firmware's logical pixels
# (480x800 in portrait). Re-check with `geom` after an orientation change.
cmd_tap() {
  [ $# -ge 2 ] || die "usage: tap X Y"
  local w
  w=$(need_window)
  DISPLAY=$DISP xdotool mousemove --window "$w" "$1" "$2" mousedown 1
  sleep 0.08
  DISPLAY=$DISP xdotool mouseup 1
  sleep "${SIM_SETTLE:-1}"
}

cmd_hold() {
  [ $# -ge 2 ] || die "usage: hold X Y [MS]"
  local w
  w=$(need_window)
  DISPLAY=$DISP xdotool mousemove --window "$w" "$1" "$2" mousedown 1
  sleep "$(awk "BEGIN{print ${3:-800}/1000}")"
  DISPLAY=$DISP xdotool mouseup 1
  sleep "${SIM_SETTLE:-1}"
}

cmd_swipe() {
  [ $# -ge 4 ] || die "usage: swipe X1 Y1 X2 Y2"
  local w i x y steps=8
  w=$(need_window)
  DISPLAY=$DISP xdotool mousemove --window "$w" "$1" "$2" mousedown 1
  for i in $(seq 1 $steps); do
    x=$(($1 + ($3 - $1) * i / steps))
    y=$(($2 + ($4 - $2) * i / steps))
    DISPLAY=$DISP xdotool mousemove --window "$w" "$x" "$y"
    sleep 0.02
  done
  DISPLAY=$DISP xdotool mouseup 1
  sleep "${SIM_SETTLE:-1}"
}

cmd_key() {
  [ $# -ge 1 ] || die "usage: key back|enter|left|right|up|down|power|home|sleep [HOLD_MS]"
  local w k
  w=$(need_window)
  case "$1" in
    back) k=Escape ;;
    enter | confirm) k=Return ;;
    left) k=Left ;;
    right) k=Right ;;
    up | prev) k=Up ;;
    down | next) k=Down ;;
    power) k=p ;;
    home) k=h ;;
    sleep) k=s ;;
    *) die "unknown key '$1'" ;;
  esac
  DISPLAY=$DISP xdotool windowfocus "$w"
  DISPLAY=$DISP xdotool keydown --window "$w" "$k"
  sleep "$(awk "BEGIN{print ${2:-80}/1000}")"
  DISPLAY=$DISP xdotool keyup --window "$w" "$k"
  sleep "${SIM_SETTLE:-1}"
}

cmd_ss() {
  local w out
  w=$(need_window)
  mkdir -p "$SHOTS"
  out="$SHOTS/${1:-shot-$(date +%H%M%S)}.png"
  DISPLAY=$DISP import -window "$w" "$out"
  echo "$out"
}

cmd_log() {
  tail -n "${1:-20}" "$STATE/sim.log"
}

# Deterministic one-shot run using the simulator's own timed input schedule.
# Screenshots are "<ms>:<name>" pairs; PNGs land in build/sim/shots/.
cmd_script() {
  [ $# -ge 3 ] || die "usage: script DEVICE 'MS:ACTION;...' 'MS:NAME;...'"
  local env bin shots="" pair ms name
  env=$(env_for "$1")
  bin=.pio/build/$env/program
  [ -x "$bin" ] || die "$bin missing; run '$0 build $1'"
  mkdir -p "$SHOTS"
  IFS=';' read -ra pairs <<< "$3"
  for pair in "${pairs[@]}"; do
    ms=${pair%%:*}
    name=${pair#*:}
    shots+="$ms:$SHOTS/$name.bmp;"
  done
  CROSSPOINT_SIM_INPUT_SCRIPT="$2" CROSSPOINT_SIM_SCREENSHOTS="${shots%;}" \
    timeout "${SIM_TIMEOUT:-60}" xvfb-run -a -s "-screen 0 1280x1024x24" "$bin" > "$STATE/script.log" 2>&1 \
    || { tail -20 "$STATE/script.log" >&2; die "run failed (log: $STATE/script.log)"; }
  for pair in "${pairs[@]}"; do
    name=${pair#*:}
    convert "$SHOTS/$name.bmp" "$SHOTS/$name.png" && rm -f "$SHOTS/$name.bmp"
    echo "$SHOTS/$name.png"
  done
}

usage() {
  sed -n 's/^  \([a-z]*\)) .*# \(.*\)/  \1 \2/p' "$0"
}

case "${1:-}" in
  setup) shift; cmd_setup "$@" ;;    # install sim envs + seed fs_/books
  build) shift; cmd_build "$@" ;;    # [x4pro|sticky|x4]
  start) shift; cmd_start "$@" ;;    # [x4pro|sticky|x4] launch on Xvfb
  stop) shift; cmd_stop "$@" ;;      # kill simulator + Xvfb
  geom) shift; cmd_geom "$@" ;;      # window title and size
  tap) shift; cmd_tap "$@" ;;        # X Y
  hold) shift; cmd_hold "$@" ;;      # X Y [MS] long-press
  swipe) shift; cmd_swipe "$@" ;;    # X1 Y1 X2 Y2
  key) shift; cmd_key "$@" ;;        # NAME [HOLD_MS]
  ss) shift; cmd_ss "$@" ;;          # [NAME] -> prints PNG path
  log) shift; cmd_log "$@" ;;        # [LINES] tail firmware log
  script) shift; cmd_script "$@" ;;  # DEVICE INPUT SHOTS one-shot run
  *) usage; exit 1 ;;
esac
