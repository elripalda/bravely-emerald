#!/bin/sh
cd "$(dirname "$0")/../../build-deck/dist" || exit 1
timeout 20 ./pokeemerald >/tmp/deck_smoke.log 2>&1
code=$?
if [ "$code" -ne 124 ]; then
    echo "FAIL: exited with $code before 20 s"; tail -5 /tmp/deck_smoke.log; exit 1
fi
if grep -q "\[SHADER\].*FAILED" userdata/gl_errors.log 2>/dev/null; then
    echo "FAIL: shader build failed"; grep "\[SHADER\]" userdata/gl_errors.log | tail -5; exit 1
fi
grep -E "\[SHADER\] self-test|\[POST\] (ready|fallback)" userdata/gl_errors.log 2>/dev/null | tail -2
echo "PASS: ran 20 s without exiting"
