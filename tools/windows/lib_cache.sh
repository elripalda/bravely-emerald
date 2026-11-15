# Sourced by build.sh, build_deps.sh, package.sh (and by tools/builder/tests): helpers that must stay safe under
# "set -eo pipefail" (a first build has no cache file, no deps folder and no log yet).

# --- diagnostics: a script that stops must say where, never end silently ------------------------------------------
# bravely_trap <script name>: call right after "set -eEo pipefail"
bravely_trap() {
    BRAVELY_SCRIPT="$1"
    trap 'rc=$?; echo "${BRAVELY_SCRIPT:-script} stopped at line $LINENO: $BASH_COMMAND (exit code $rc)" >&2' ERR
}

# die <message>: print the message, where it came from, and the last 30 lines of the log the script keeps (if any)
die() {
    echo "ERROR: $*" >&2
    echo "  (${BRAVELY_SCRIPT:-script}, line $(caller 0 | cut -d' ' -f1))" >&2
    if [ -n "${BRAVELY_LOG:-}" ] && [ -f "$BRAVELY_LOG" ]; then
        echo "--- last 30 lines of $BRAVELY_LOG ---" >&2
        tail -n 30 "$BRAVELY_LOG" >&2 || true
    fi
    exit 1
}

# run_logged <log file> <command...>: run, show and keep the output; on failure repeat the last 30 lines and return the code
run_logged() {
    local log="$1" rc had_e=0
    shift
    mkdir -p "$(dirname "$log")"
    BRAVELY_LOG="$log"
    case "$-" in *e*) had_e=1 ;; esac
    set +e                                  # a failing command must reach the lines below, not end the script inside the pipe
    "$@" 2>&1 | tee "$log"
    rc=${PIPESTATUS[0]}
    [ "$had_e" -eq 1 ] && set -e
    if [ "$rc" -ne 0 ]; then
        echo
        echo "--- $1 failed (exit code $rc); last 30 lines of its output ---"
        tail -n 30 "$log" || true
    fi
    return "$rc"
}

# --- CMake build folder ------------------------------------------------------------------------------------------------
# drop_foreign_build <build dir> <source dir, MSYS form>
# Removes <build dir> when its CMakeCache.txt was made for another source folder. No cache = nothing to do.
# Prints one line saying which of the three cases it was.
drop_foreign_build() {
    local build="$1" root="$2" cache="$1/CMakeCache.txt" mixed cached
    if [ ! -f "$cache" ]; then
        echo "no CMake build folder yet: full compile"
        return 0
    fi
    # CMake stores the source folder as a Windows path (F:/x/y) while $root is an MSYS path (/f/x/y): compare the mixed
    # form, ignoring case, or every run would throw the build folder away and compile everything again
    mixed="$(cygpath -m "$root" 2>/dev/null || echo "$root")"
    cached="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache" 2>/dev/null | tr 'A-Z' 'a-z' || true)"
    if [ "$cached" != "$(echo "$mixed" | tr 'A-Z' 'a-z')" ] && [ "$cached" != "$(echo "$root" | tr 'A-Z' 'a-z')" ]; then
        echo "the build folder was made for another source folder: removing it"
        rm -rf "$build"
    else
        echo "already configured for this folder: reusing it (only changed files are compiled)"
    fi
    return 0
}

# --- SDL2 deps ---------------------------------------------------------------------------------------------------------
SDL2_VERSION=2.32.10
SDL2_IMAGE_VERSION=2.8.12

# pc_version <file.pc>: the Version line, empty when the file is missing
pc_version() {
    [ -f "$1" ] || return 0
    sed -n 's/^Version:[[:space:]]*//p' "$1" 2>/dev/null | head -n 1 | tr -d '\r' || true
}

# deps_state <prefix>: prints present | missing | outdated (mirror of deps_state in tools/builder/core/probe.py)
# present = both libraries and both versions match; outdated = a half-built prefix or other versions; missing = nothing.
deps_state() {
    local p="$1" n=0
    [ -f "$p/lib/libSDL2.a" ] && n=$((n + 1))
    [ -f "$p/lib/libSDL2_image.a" ] && n=$((n + 1))
    if [ "$n" -eq 0 ]; then
        echo missing
    elif [ "$n" -eq 2 ] && [ "$(pc_version "$p/lib/pkgconfig/sdl2.pc")" = "$SDL2_VERSION" ] &&
         [ "$(pc_version "$p/lib/pkgconfig/SDL2_image.pc")" = "$SDL2_IMAGE_VERSION" ]; then
        echo present
    else
        echo outdated
    fi
}
