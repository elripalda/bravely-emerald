#!/bin/sh
cd "$(dirname "$0")/../../build-deck/dist" || exit 1
timeout 20 ./pokeemerald >/tmp/deck_smoke.log 2>&1
code=$?
if [ "$code" -ne 124 ]; then
    echo "FAIL: exited with $code before 20 s"; tail -5 /tmp/deck_smoke.log; exit 1
fi
echo "PASS: ran 20 s without exiting"
