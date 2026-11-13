#!/bin/sh
# usage: setkey.sh <settings.txt> <key> <value>
# Sets key=value in a settings.txt; appends the line when the key is absent. Prints the result.
f="$1"; k="$2"; v="$3"
[ -f "$f" ] || { echo "no $f"; exit 1; }
if grep -q "^$k=" "$f"; then
    sed -i "s/^$k=.*/$k=$v/" "$f"
else
    echo "$k=$v" >> "$f"
fi
grep "^$k=" "$f"
