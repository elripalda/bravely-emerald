# Sourced by build.sh (and by tools/builder/tests): decides whether an old CMake build folder can be reused.
# Must stay safe under "set -eo pipefail": a first build has no cache file at all.

# drop_foreign_build <build dir> <source dir, MSYS form>
# Removes <build dir> when its CMakeCache.txt was made for another source folder. No cache = nothing to do.
drop_foreign_build() {
    local build="$1" root="$2" cache="$1/CMakeCache.txt" mixed cached
    [ -f "$cache" ] || return 0
    # CMake stores the source folder as a Windows path (F:/x/y) while $root is an MSYS path (/f/x/y): compare the mixed
    # form, ignoring case, or every run would throw the build folder away and compile everything again
    mixed="$(cygpath -m "$root" 2>/dev/null || echo "$root")"
    cached="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache" 2>/dev/null | tr 'A-Z' 'a-z' || true)"
    if [ "$cached" != "$(echo "$mixed" | tr 'A-Z' 'a-z')" ] && [ "$cached" != "$(echo "$root" | tr 'A-Z' 'a-z')" ]; then
        rm -rf "$build"
    fi
    return 0
}
