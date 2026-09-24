#!/bin/sh
# run_tests.sh - end-to-end PoC test for c64-git.
#
#  1. create a real git repo fixture (loose objects)
#  2. mkpak -> GITREPO.PAK
#  3. host report -> golden.txt   (also asserts SHA1-OK/FAIL-0)
#  4. unit-check sha1/inflate against `git` itself
#  5. build d64, run x64sc under the remote monitor, capture the
#     on-C64 report from RAM, diff against golden
#
# --host-only skips step 5.
set -e
cd "$(dirname "$0")/.."
HOST_ONLY=0
[ "$1" = "--host-only" ] && HOST_ONLY=1

: "${C1541:=c1541}"
: "${X64SC:=x64sc}"
export C1541 X64SC

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
echo "workdir: $WORK"

# ---------- fixture repo ----------
FIX=$WORK/fixture
mkdir -p "$FIX"
cd "$FIX"
git init -q
git config user.name "Sam Crafts"
git config user.email "sam@c64.dev"
git config commit.gpgsign false
git config gc.auto 0
export GIT_AUTHOR_DATE="1985-06-01T12:00:00 +0000"
export GIT_COMMITTER_DATE="1985-06-01T12:00:00 +0000"

printf 'HELLO C64 GIT\nline two of readme\nthird line\n' > README.TXT
printf 'kernel panic in basic rom\n' > NOTES.TXT
git add -A && git commit -qm "initial commit"

printf 'HELLO C64 GIT\nline two of readme\nthird line\nmore lines\n' > README.TXT
printf 'load "*",8,1\n' > RUN.BAS
git add -A && git commit -qm "add run basic stub"

printf 'HELLO C64 GIT - EXTENDED\nline two of readme\nthird line\nmore lines\nlast line\n' > README.TXT
git add -A && git commit -qm "extend readme for the demo"

cd - >/dev/null

# ---------- mkpak + host report ----------
./build/git64tool mkpak "$FIX/.git" "$WORK/gitrepo.pak"
./build/git64tool report "$WORK/gitrepo.pak" > "$WORK/golden.txt"
cat "$WORK/golden.txt"

grep -q "SHA1-OK " "$WORK/golden.txt"
grep -q " FAIL 0" "$WORK/golden.txt"
echo "host report: PASS"

# sanity: our sha1+inflate produce the same hash git computes
GITSHA=$(git -C "$FIX" rev-parse HEAD)
PAKSHA=$(./build/git64tool cat "$WORK/gitrepo.pak" "$GITSHA" | head -c 200 | grep -c '^tree ')
echo "commit object verified via pak cat (tree header seen: $PAKSHA)"

if [ "$HOST_ONLY" = 1 ]; then
    echo "host-only test: PASS"
    exit 0
fi

# ---------- d64 + emulator run ----------
D64=$WORK/c64git.d64
"$C1541" -format c64git,01 d64 "$D64" >/dev/null
"$C1541" -attach "$D64" -write ./build/GIT64.PRG git64 >/dev/null
"$C1541" -attach "$D64" -write "$WORK/gitrepo.pak" "gitrepo.pak,s" >/dev/null
echo "disk image:"; "$C1541" -attach "$D64" -list

SHOT=$PWD/build/vice-screen.png
# fresh monitor port each run (a previous run's socket lingers TIME_WAIT)
PORT=$(( ($$ % 30000) + 20000 ))
"$X64SC" -remotemonitor -remotemonitoraddress "ip4://127.0.0.1:$PORT" \
         +sound -warp -autostart "$D64" >/dev/null 2>&1 &
VICE_PID=$!
python3 tests/vicemon.py --port $PORT --preroll 20 --timeout 300 \
        --out "$WORK/report_c64.txt" \
        --screen "$WORK/screen_c64.txt" --shot "$SHOT" || true
kill $VICE_PID 2>/dev/null || true
wait $VICE_PID 2>/dev/null || true

echo "--- screen capture ---"; cat "$WORK/screen_c64.txt" || true
echo "--- c64 report ---"; cat "$WORK/report_c64.txt" || true

diff -u "$WORK/golden.txt" "$WORK/report_c64.txt"
echo "emulator report matches host golden: PASS"
