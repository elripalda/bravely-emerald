#!/bin/sh
# usage (on the Deck host, from the repo root): run_autoplay.sh <script> <seconds> <outdir>
# env SETKEY="voxel_fps=0 voxel_blur=1" : settings applied to the test copy only; EXTRA_ENV="vblank_mode=1": extra game env.
# Runs the game from build-deck/dist-test (copy of dist, saves included) so the user's dist/userdata stays untouched,
# collects the perf files and in-engine screenshots (shot_*.png) into <outdir>, then relaunches one normal instance from dist.
script="$(readlink -f "$1")"; secs="$2"; out="$3"
root="$(pwd)"; dist="$root/build-deck/dist"; test="$root/build-deck/dist-test"
[ -f "$script" ] && [ -n "$secs" ] && [ -n "$out" ] || { echo "usage: $0 <script> <seconds> <outdir>"; exit 1; }
export DISPLAY=:0 XAUTHORITY="$(ls /run/user/1000/xauth_* | head -1)" XDG_RUNTIME_DIR=/run/user/1000

killgame() {
    for p in $(ps -eo pid,args | grep '[.]/pokeemerald' | awk '{print $1}'); do kill "$p"; done
    sleep 3
    for p in $(ps -eo pid,args | grep '[.]/pokeemerald' | awk '{print $1}'); do kill -9 "$p"; done
    sleep 1
}
launch() { # <dir> <env...>
    d="$1"; shift
    ( cd "$d" && setsid nohup env "$@" distrobox enter emerald-build -- bash -lc \
        "cd $d && env $* DISPLAY=:0 XAUTHORITY=$XAUTHORITY XDG_RUNTIME_DIR=/run/user/1000 ./run.sh" \
        >/tmp/game.out 2>&1 </dev/null & )
}

killgame
rm -rf "$test"; cp -a "$dist" "$test"
rm -f "$test"/userdata/perf_frames.csv "$test"/userdata/perf_summary.txt "$test"/userdata/debug.log "$test"/userdata/gl_errors.log
for kv in $SETKEY; do sh "$root/tools/deck/setkey.sh" "$test/userdata/settings.txt" "${kv%%=*}" "${kv#*=}"; done
mkdir -p "$out"
# In-engine screenshots (DECK_SHOT, see deck_shot.c): spectacle cannot capture the GL window. Times are ms since process start.
shots="15000:$out/shot_15s.png"
[ "$secs" -gt 65 ] && shots="$shots,60000:$out/shot_60s.png"
shots="$shots,$(( (secs - 5) * 1000 )):$out/shot_end.png"
launch "$test" DECK_AUTOPLAY="$script" DECK_GL_DEBUG=1 DECK_SHOT="$shots" $EXTRA_ENV
sleep "$secs"
# optional: SPECTACLE=1 also grabs a desktop shot (does not show the GL window, steals focus)
[ -n "$SPECTACLE" ] && spectacle -b -n -f -o "$out/desktop_end.png"
ps -eo pid,args | grep '[.]/pokeemerald'
killgame
for f in perf_frames.csv perf_summary.txt gl_errors.log debug.log; do cp "$test/userdata/$f" "$out/" 2>/dev/null; done
sh "$root/tools/deck/perf_report.sh" "$out" > "$out/report.txt"; cat "$out/report.txt"
launch "$dist"
sleep 5; ps -eo pid,args | grep '[.]/pokeemerald'
