#!/bin/sh
# Runs the build from its dist folder so relative paths (userdata/, app0/) resolve.
cd "$(dirname "$0")" || exit 1
exec ./pokeemerald "$@"
