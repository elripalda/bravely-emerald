#!/bin/sh
# usage: shots.sh <label>   (Deck host, not the container). Full-screen PNG into SD512/emerald-deck/shots.
dir=/run/media/deck/SD512/emerald-deck/shots
mkdir -p "$dir" || exit 1
out="$dir/$1-$(date +%Y%m%d-%H%M%S).png"
DISPLAY=:0 XAUTHORITY=$(ls /run/user/1000/xauth_* | head -1) spectacle -b -n -f -o "$out" || exit 1
echo "$out"
