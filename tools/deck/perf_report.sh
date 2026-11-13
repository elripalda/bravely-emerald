#!/bin/sh
# usage: perf_report.sh [userdata-dir]   (default: userdata, relative to the dist folder)
# Reads perf_frames.csv (columns by name, see docs/DEBUGGING.md) and perf_summary.txt.
# Only 3D frames (render3d == 1) are counted.
# "1% low FPS" = 1000 / mean frame ms of the slowest 1% of frames (0.1% likewise).
dir="${1:-userdata}"
csv="$dir/perf_frames.csv"
sum="$dir/perf_summary.txt"
[ -f "$csv" ] || { echo "no $csv"; exit 1; }
tmp=$(mktemp) || exit 1
trap 'rm -f "$tmp"' EXIT

# frame_ms of 3D frames, sorted descending (worst first)
awk -F, 'NR==1 {for (i=1;i<=NF;i++) c[$i]=i; next}
         $c["render3d"]==1 {print $c["frame_ms"]}' "$csv" | sort -rn > "$tmp"

awk '{v[NR]=$1; s+=$1; if (NR==1||$1<mn) mn=$1}
     END {
       n=NR; if (n==0) {print "no 3D frames"; exit}
       k1=int(n*0.01); if (k1<1) k1=1
       k01=int(n*0.001); if (k01<1) k01=1
       for (i=1;i<=k1;i++) a+=v[i]
       for (i=1;i<=k01;i++) b+=v[i]
       for (i=1;i<=n;i++) {if (v[i]>16.7) o16++; if (v[i]>33.3) o33++}
       printf "3D frames: %d\n", n
       printf "frame ms: avg %.2f  min %.2f  max %.2f\n", s/n, mn, v[1]
       printf "FPS: avg %.1f  1%% low %.1f  0.1%% low %.1f\n", 1000/(s/n), 1000/(a/k1), 1000/(b/k01)
       printf "over 16.7 ms: %d (%.2f%%)  over 33.3 ms: %d (%.2f%%)\n", o16, 100*o16/n, o33, 100*o33/n
     }' "$tmp"

# chunk build: Core 0 builds (build_ms over frames with chunks_built>0) and worker compute
# (worker_compute_ms over frames with worker_built>0); draw3D over all 3D frames.
awk -F, 'NR==1 {for (i=1;i<=NF;i++) c[$i]=i; next}
         $c["render3d"]==1 {
           d+=$c["draw3d_ms"]; nd++; if ($c["draw3d_ms"]>dm) dm=$c["draw3d_ms"]
           if ($c["chunks_built"]>0) {b+=$c["build_ms"]; nb+=$c["chunks_built"]; nbf++}
           if ($c["worker_built"]>0) {w+=$c["worker_compute_ms"]; nw+=$c["worker_built"]}
         }
         END {
           if (nd==0) exit
           printf "draw3D ms: avg %.2f  max %.2f\n", d/nd, dm
           if (nb>0) printf "chunk build (Core 0): %d chunks in %d frames, %.2f ms/chunk, %.2f ms/frame-with-build\n", nb, nbf, b/nb, b/nbf
           else print "chunk build (Core 0): none"
           if (nw>0) printf "chunk build (worker): %d chunks, %.2f ms/chunk\n", nw, w/nw
           else print "chunk build (worker): none"
         }' "$csv"

if [ -f "$sum" ]; then
    echo "--- last summary lines ---"
    grep -E "FRAME \(3D\)|DECK:|MEMORY|SESSION \(3D\)" "$sum" | tail -4
fi
