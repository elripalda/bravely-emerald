#!/bin/sh
# usage: seed.sh <src> <dest>; copies only when dest is missing
[ -f "$2" ] || cp "$1" "$2"
