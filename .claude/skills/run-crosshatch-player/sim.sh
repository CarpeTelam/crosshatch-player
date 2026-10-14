#!/usr/bin/env bash
# Build, launch, and drive the CrossPoint desktop simulator headlessly.
# Run from the repo root. State (pids, log, screenshots) lives in build/sim/.
# SIM_INSTANCE=1..9 runs a numbered instance beside the others, so two windows can play a nearby round: its own
# display, state directory (build/sim/inst-N), SD root, web server port, and inbox. See `where` and `inbox`.
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

# A numbered instance (SIM_INSTANCE unset or empty: the default instance, as before). INSTANCE stays empty for the
# default. SD is the instance's SD root (absolute: the binary runs in the repo root), INBOX its games inbox.
INSTANCE="${SIM_INSTANCE:-}"
SD="$ROOT/fs_"
HTTP_PORT=8080
if [ -n "$INSTANCE" ]; then
  case "$INSTANCE" in
    [1-9]) ;;
    *) die "SIM_INSTANCE must be 1..9 (or unset), got '$INSTANCE'" ;;
  esac
  STATE=build/sim/inst-$INSTANCE
  SHOTS=$STATE/shots
  DISP="${SIM_DISPLAY:-:$((77 + INSTANCE))}"
  SD="$ROOT/$STATE/fs_"
  HTTP_PORT=$((8080 + 10 * INSTANCE))
fi
INBOX="$SD/games"

# The UDP port base the instances of this worktree share (UdpLink: CROSSHATCH_UDP_PORT_BASE). The caller's wins; else it
# comes from the repo root, so worktrees running simulators at once do not hear each other: 21000 + (cksum % 1000) * 8,
# in 21000..28992, clear of the link's default (45700).
udp_base() {
  if [ -n "${CROSSHATCH_UDP_PORT_BASE:-}" ]; then
    echo "$CROSSHATCH_UDP_PORT_BASE"
    return
  fi
  local sum
  sum=$(printf '%s' "$ROOT" | cksum | cut -d' ' -f1)
  echo $((21000 + (sum % 1000) * 8))
}

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

# A marker line is matched with trailing blanks and CRs ignored, so a platformio.local.ini saved with CRLF endings
# (an editor on Windows, core.autocrlf) still has its markers, and none of the checks below reads it as marker-free:
# setup would then append a second block beside the first (retro R9 c, cross-story finding 12). Each awk program below
# starts with this rule, which sets `line` to the record without them.
TRIM='{ line = $0; sub(/[ \t\r]+$/, "", line) }'

# Exit 0 when platformio.local.ini is absent, has neither marker, or has exactly one begin marker followed by one end
# marker; else exit 1. setup's block replace drops every line from a begin marker to the next end marker, so any other
# shape (no end marker, an end marker first, a second begin marker, a second block) would lose the user's lines.
markers_ok() {
  [ -f platformio.local.ini ] || return 0
  awk -v b="$MARK_BEGIN" -v e="$MARK_END" "$TRIM"'
    line==b { if (begins || ends) bad=1; begins++ }
    line==e { if (!begins || ends) bad=1; ends++ }
    END { exit (bad || begins != ends) }' platformio.local.ini
}

# Exit 0 when platformio.local.ini has a begin marker line.
has_begin() {
  awk -v b="$MARK_BEGIN" "$TRIM"' line==b { found=1 } END { exit !found }' platformio.local.ini
}

MARKERS_BAD="markers are not one '$MARK_BEGIN' line followed by one '$MARK_END' line"

cmd_setup() {
  local local_ini=platformio.local.ini tmp
  touch "$local_ini"
  # Any other marker shape would make the block replace below drop the user's lines.
  markers_ok || die "$local_ini's simulator $MARKERS_BAD; restore it by hand"
  tmp=$(mktemp)
  # Replace any previous managed block, keep the user's own settings.
  awk -v b="$MARK_BEGIN" -v e="$MARK_END" "$TRIM"' line==b{skip=1;next} line==e{skip=0;next} !skip' "$local_ini" > "$tmp"
  { cat "$tmp"; echo "$MARK_BEGIN"; cat "$SKILL_DIR/simulator.ini"; echo "$MARK_END"; } > "$local_ini"
  rm -f "$tmp"
  mkdir -p fs_/books "$SHOTS"
  if [ -z "$(ls -A fs_/books)" ]; then
    cp test/epubs/test_kerning_ligature.epub fs_/books/
    echo "sim: seeded fs_/books/ with test_kerning_ligature.epub"
  fi
  echo "sim: simulator envs installed into $local_ini"
}

managed_block() {
  [ -f platformio.local.ini ] || return 0
  awk -v b="$MARK_BEGIN" -v e="$MARK_END" "$TRIM"' line==b{on=1;next} line==e{on=0;next} on' platformio.local.ini
}

# Exit 0 when platformio.local.ini's managed block equals simulator.ini, else say why and exit 1.
cmd_check() {
  local local_ini=platformio.local.ini
  if ! markers_ok; then
    echo "sim: $local_ini's simulator $MARKERS_BAD; restore it by hand (setup will not replace such a block)" >&2
    return 1
  fi
  # markers_ok passed, so a begin marker here has its one end marker after it.
  if [ ! -f "$local_ini" ] || ! has_begin; then
    echo "sim: $local_ini has no managed simulator block; run '$0 setup'" >&2
    return 1
  fi
  if [ "$(managed_block)" != "$(cat "$SKILL_DIR/simulator.ini")" ]; then
    echo "sim: $local_ini's managed simulator block differs from simulator.ini (stale); run '$0 setup'" >&2
    return 1
  fi
  echo "sim: $local_ini's managed simulator block is current"
}

cmd_build() {
  local env
  env=$(env_for "${1:-}")
  # A missing or outdated copy (simulator.ini edited since setup) would build with the wrong flags.
  if ! cmd_check; then
    echo "sim: running setup"
    cmd_setup
  fi
  pio run -e "$env" -j "$(nproc)"
}

cmd_start() {
  local env bin
  env=$(env_for "${1:-}")
  bin=.pio/build/$env/program
  [ -x "$bin" ] || die "$bin missing; run '$0 build ${1:-x4pro}'"
  cmd_stop >/dev/null 2>&1 || true
  mkdir -p "$STATE" "$SHOTS"
  # The UDP base is the one thing every simulator of this worktree shares, the default instance included.
  export CROSSHATCH_UDP_PORT_BASE
  CROSSHATCH_UDP_PORT_BASE=$(udp_base)
  if [ -n "$INSTANCE" ]; then
    # Its own SD root and web server port.
    mkdir -p "$INBOX"
    export CROSSPOINT_SIM_SD="$SD" CROSSPOINT_SIM_HTTP_PORT="$HTTP_PORT"
  fi
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

# What this instance uses, as key=value lines. Reads nothing and creates nothing.
cmd_where() {
  echo "instance=${INSTANCE:-default}"
  echo "display=$DISP"
  echo "state=$STATE"
  echo "shots=$SHOTS"
  echo "sd=$SD"
  echo "inbox=$INBOX"
  echo "http_port=$HTTP_PORT"
  echo "udp_base=$(udp_base)"
}

# Packs a game folder into this instance's inbox; the Games screen installs it on open.
cmd_inbox() {
  [ $# -eq 1 ] || die "usage: inbox GAME_DIR"
  [ -d "$1" ] || die "no game folder '$1'"
  python3 scripts/pack_game.py "$1" "$INBOX"
  echo "sim: packed $1 into $INBOX"
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
  check) shift; cmd_check "$@" ;;    # exit 1 if the managed block is missing or stale
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
  where) shift; cmd_where "$@" ;;    # key=value lines: display, state, SD root, inbox, ports (SIM_INSTANCE=1..9)
  inbox) shift; cmd_inbox "$@" ;;    # GAME_DIR pack a game into the instance's fs_/games
  script) shift; cmd_script "$@" ;;  # DEVICE INPUT SHOTS one-shot run
  *) usage; exit 1 ;;
esac
