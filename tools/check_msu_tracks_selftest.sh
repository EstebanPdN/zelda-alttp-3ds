#!/usr/bin/env bash
# Self-test suite for the MSU-1 track validator.
#
# It fabricates various .pcm / .opuz files in a temporary directory and checks
# tools/check_msu_tracks.py item by item: exit codes, report contents, and the
# JSON structure:
#   normal pack / bad tag / header under 8 bytes / missing numbers / optional
#   track 0 / duplicate numbers / non-numeric file names / too short and too
#   long durations / repeat_position out of range / unaligned data / empty
#   directory / missing directory / --quiet / --json / --ext opuz
#
# Usage: bash tools/check_msu_tracks_selftest.sh
# Exit code: 0 if everything passes, 1 if any check fails.
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CHECKER="$SCRIPT_DIR/check_msu_tracks.py"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/msu-selftest.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT

OUT="$WORK/stdout.txt"
ERR="$WORK/stderr.txt"
TOTAL=0
FAILED=0

ok() {
  TOTAL=$((TOTAL + 1))
  printf '  \033[32mPASS\033[0m %s\n' "$1"
}

ng() {
  TOTAL=$((TOTAL + 1))
  FAILED=$((FAILED + 1))
  printf '  \033[31mFAIL\033[0m %s\n' "$1"
  [ -s "$OUT" ] && sed 's/^/       | /' "$OUT"
  [ -s "$ERR" ] && sed 's/^/       ! /' "$ERR"
}

# expect <expected exit code> <description> <command...>
# Output is saved to $OUT / $ERR for the later contains/not_contains checks.
expect() {
  local want="$1" label="$2"
  shift 2
  "$@" >"$OUT" 2>"$ERR"
  local rc=$?
  if [ "$rc" -eq "$want" ]; then
    ok "$label (exit code $rc)"
  else
    ng "$label: expected exit code $want, got $rc"
  fi
}

# Search both stdout and stderr: directory-level errors go to stderr, while
# per-file results go to stdout.
contains() {
  if grep -qF -- "$2" "$OUT" || grep -qF -- "$2" "$ERR"; then
    ok "$1"
  else
    ng "$1: output does not contain \"$2\""
  fi
}

not_contains() {
  if grep -qF -- "$2" "$OUT" || grep -qF -- "$2" "$ERR"; then
    ng "$1: output must not contain \"$2\""
  else
    ok "$1"
  fi
}

# make_pcm <path> <seconds> [tag] [repeat_position]
# 8-byte header + silent PCM (sparse file, so it barely uses any disk space).
make_pcm() {
  python3 - "$@" <<'PY'
import struct
import sys

path, seconds = sys.argv[1], float(sys.argv[2])
tag = sys.argv[3].encode("ascii") if len(sys.argv) > 3 else b"MSU1"
repeat = int(sys.argv[4]) if len(sys.argv) > 4 else 0
assert len(tag) == 4, "tag must be 4 bytes"
size = 8 + int(seconds * 44100) * 4
with open(path, "wb") as f:
    f.write(tag + struct.pack("<I", repeat))
    f.truncate(size)          # sparse extension, contents are silent zeros
PY
}

# make_opuz <path> <packet count>
make_opuz() {
  python3 - "$@" <<'PY'
import struct
import sys

path, count = sys.argv[1], int(sys.argv[2])
with open(path, "wb") as f:
    f.write(b"OPUZ" + struct.pack("<I", 0))
    for i in range(count):
        payload = bytes([i & 0xFF]) * 10
        f.write(struct.pack("<H", len(payload)) + payload)
PY
}

# ---------------------------------------------------------- case 1: normal pack
GOOD="$WORK/good"
mkdir -p "$GOOD"
make_pcm "$GOOD/1.pcm" 6
make_pcm "$GOOD/2.pcm" 5.5
make_pcm "$GOOD/3.pcm" 10
expect 0 "normal track pack" python3 "$CHECKER" "$GOOD"
contains "normal pack summary is 3/3" "passed 3/3"
contains "normal pack has no gaps" "no missing numbers"
contains "table has the number column" "No."
contains "table duration is formatted correctly" "0:10.0"
if ! grep -q "⚠" "$OUT"; then ok "normal pack has no warnings"
else ng "normal pack must not have warnings"; fi

# ----------------------------------------------------------------- case 2: bad tag
BADTAG="$WORK/badtag"
mkdir -p "$BADTAG"
make_pcm "$BADTAG/1.pcm" 6
make_pcm "$BADTAG/2.pcm" 6 RIFF
expect 1 "bad tag pack fails" python3 "$CHECKER" "$BADTAG"
contains "bad tag reports the actual value" "found b'RIFF'"
contains "bad tag explains it is not MSU-1 PCM" "not an MSU-1 PCM file"
contains "bad tag summary includes the failure count" "failed 1"

# --------------------------------------------------------------- case 3: too short
SHORT="$WORK/short"
mkdir -p "$SHORT"
make_pcm "$SHORT/1.pcm" 6
printf 'MSU1' >"$SHORT/2.pcm"      # only 4 bytes
: >"$SHORT/3.pcm"                  # an empty file is also "under 8 bytes"
expect 1 "header under 8 bytes fails" python3 "$CHECKER" "$SHORT"
contains "4-byte file reports its actual size" \
  "File is only 4 bytes, shorter than the 8-byte header"
contains "0-byte file reports its actual size" \
  "File is only 0 bytes, shorter than the 8-byte header"
contains "short-file summary includes the failure count" "failed 2"

# ------------------------------------------------------------- case 4: missing numbers
GAP="$WORK/gap"
mkdir -p "$GAP"
make_pcm "$GAP/1.pcm" 6
make_pcm "$GAP/2.pcm" 6
make_pcm "$GAP/4.pcm" 6
expect 0 "a missing number is only a warning (exit code 0)" python3 "$CHECKER" "$GAP"
contains "lists missing number 3" "missing numbers (1): 3"
contains "still counts passed files when numbers are missing" "passed 3/3"

# ------------------------------------------------------------ case 5: optional track 0
ZERO="$WORK/zero"
mkdir -p "$ZERO"
make_pcm "$ZERO/0.pcm" 4        # 4-second menu track: short but no warning
make_pcm "$ZERO/1.pcm" 6
make_pcm "$ZERO/2.pcm" 6
expect 0 "track 0 is optional and excluded from gap detection" python3 "$CHECKER" "$ZERO"
contains "no gaps when track 0 exists" "no missing numbers"
contains "track 0 is listed" "0.pcm"
not_contains "short track 0 does not trigger the 5-second warning" "under 5 seconds"

# ------------------------------------------------------ case 6: naming and duplicates
NAMES="$WORK/names"
mkdir -p "$NAMES"
make_pcm "$NAMES/1.pcm" 6
make_pcm "$NAMES/02.pcm" 6
make_pcm "$NAMES/2.pcm" 6       # duplicate of 02
make_pcm "$NAMES/intro.pcm" 6   # non-numeric file name
make_pcm "$NAMES/7.PCM" 6      # upper-case extensions are recognised too
expect 0 "naming problems are only warnings (exit code 0)" python3 "$CHECKER" "$NAMES"
contains "duplicate number is reported" "Number 2 is duplicated"
contains "non-canonical file name is reported" "is not a canonical name"
contains "non-numeric file name is reported" "is not a numeric track number"
contains "upper-case extension is recognised" "7.PCM"

# ------------------------------------------------------ case 7: duration / loop point
EDGE="$WORK/edge"
mkdir -p "$EDGE"
make_pcm "$EDGE/1.pcm" 6
make_pcm "$EDGE/5.pcm" 2          # a too-short track that is not track 0
make_pcm "$EDGE/2.pcm" 1801 MSU1  # 30 minutes 1 second, sparse file
make_pcm "$EDGE/3.pcm" 6 MSU1 999999999   # repeat_position out of range
python3 - "$EDGE/4.pcm" <<'PY'
import struct
import sys
with open(sys.argv[1], "wb") as f:
    f.write(b"MSU1" + struct.pack("<I", 0) + b"\x00" * 101)
PY
expect 0 "overlong/loop point/unaligned are all warnings" python3 "$CHECKER" "$EDGE"
contains "warns when a track exceeds 30 minutes" "exceeds 30 minutes"
contains "warns when a non-zero track is too short" "under 5 seconds"
contains "warns when repeat_position is out of range" "repeat_position=999999999"
contains "warns when PCM data is unaligned" "not a multiple of 4"

# --------------------------------------------------------------- case 8: bad directories
EMPTY="$WORK/empty"
mkdir -p "$EMPTY"
expect 1 "empty directory fails" python3 "$CHECKER" "$EMPTY"
contains "empty directory explains why" "No *.pcm files found"
expect 1 "missing directory fails" python3 "$CHECKER" "$WORK/no-such-dir"
contains "missing directory explains why" "Directory does not exist"
expect 1 "a file path instead of a directory fails" python3 "$CHECKER" "$GOOD/1.pcm"

# ------------------------------------------------------------------- case 9: --quiet
expect 1 "quiet mode still reports failures through the exit code" \
  python3 "$CHECKER" "$BADTAG" --quiet
contains "quiet mode keeps errors" "Header tag is not MSU1"
contains "quiet mode keeps the summary" "failed 1"
not_contains "quiet mode hides per-file warnings" "⚠"
if ! grep -q "1.pcm" "$OUT"; then ok "quiet mode does not print lines for valid files"
else ng "quiet mode must not print the table"; fi

# -------------------------------------------------------------------- case 10: --json
expect 1 "json mode returns 1 for a bad pack" python3 "$CHECKER" "$BADTAG" --json
if python3 - "$OUT" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as f:
    data = json.load(f)
assert data["ext"] == "pcm"
assert data["expected_tag"] == "MSU1"
assert data["summary"]["total"] == 2, data["summary"]
assert data["summary"]["failed"] == 1
assert data["summary"]["ok"] is False
files = {item["name"]: item for item in data["files"]}
assert files["2.pcm"]["ok"] is False
assert files["2.pcm"]["errors"], files["2.pcm"]
assert files["1.pcm"]["repeat_position"] == 0
assert files["1.pcm"]["duration_seconds"] > 5
PY
then ok "json output structure is correct"; else ng "json output structure is incorrect"; fi

expect 0 "json mode returns 0 for a normal pack" python3 "$CHECKER" "$GAP" --json
if python3 - "$OUT" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as f:
    data = json.load(f)
assert data["summary"]["ok"] is True
assert data["summary"]["missing_numbers"] == [3]
assert data["summary"]["total_duration_seconds"] >= 18
assert [item["number"] for item in data["files"]] == [1, 2, 4]
PY
then ok "json summary and missing numbers are correct"
else ng "json summary or missing numbers are incorrect"; fi

# ---------------------------------------------------------------- case 11: --ext opuz
OPUS="$WORK/opus"
mkdir -p "$OPUS"
make_opuz "$OPUS/1.opuz" 3
make_opuz "$OPUS/2.opuz" 5
make_pcm "$OPUS/3.pcm" 6          # unrelated to --ext opuz and must be ignored
expect 0 "opuz track pack is valid" python3 "$CHECKER" "$OPUS" --ext opuz
contains "opuz mentions the container check" "Opus"
contains "opuz summary is 2/2" "passed 2/2"
not_contains "opuz mode ignores .pcm" "3.pcm"

BADOPUS="$WORK/badopus"
mkdir -p "$BADOPUS"
make_opuz "$BADOPUS/1.opuz" 2
make_pcm "$BADOPUS/2.opuz" 6      # MSU1 header, not OPUZ
expect 1 "opuz mode rejects a PCM header" python3 "$CHECKER" "$BADOPUS" --ext opuz
contains "opuz reports the actual tag" "found b'MSU1'"

# --------------------------------------------------------------- case 12: symlinks
LINK="$WORK/link"
mkdir -p "$LINK"
make_pcm "$LINK/1.pcm" 6
make_pcm "$LINK/target.pcm" 6
ln -s target.pcm "$LINK/2.pcm"      # symlink to a file inside the pack: fine
ln -s nowhere.pcm "$LINK/3.pcm"     # broken link: an error, not a "missing number"
expect 1 "dangling symlink is an error" python3 "$CHECKER" "$LINK"
contains "symlinked track validates normally" "2.pcm"
contains "broken link reports the missing target" "Symbolic link target does not exist"
contains "broken link counts as a failure" "failed 1"

# --------------------------------------------------------- case 13: corrupt opuz
BROKEN="$WORK/brokenopus"
mkdir -p "$BROKEN"
make_opuz "$BROKEN/1.opuz" 2
python3 - "$BROKEN" <<'PY'
import os
import struct
import sys

directory = sys.argv[1]


def write(name, *chunks):
    with open(os.path.join(directory, name), "wb") as f:
        f.write(b"OPUZ" + struct.pack("<I", 0))
        for chunk in chunks:
            f.write(chunk)


write("2.opuz", struct.pack("<H", 5000), b"\x00" * 5000)   # above the 1275 limit
write("3.opuz")                                            # header only
write("4.opuz", struct.pack("<H", 100), b"\x00" * 10)      # declared length out of range
PY
expect 1 "opuz oversize/no packets/out of range all fail" \
  python3 "$CHECKER" "$BROKEN" --ext opuz
contains "opuz packet over the limit is reported" "exceeds the 1275-byte limit"
contains "opuz header-only file is reported" "no Opus packets"
contains "opuz packet running past the end is reported" "runs past the end of the file"

# --------------------------------------------------------- case 14: directory errors
expect 1 "json mode reports directory errors" python3 "$CHECKER" "$WORK/no-such-dir" --json
if python3 - "$OUT" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as f:
    data = json.load(f)
assert data["ok"] is False
assert "Directory does not exist" in data["error"], data
PY
then ok "json directory error structure is correct"
else ng "json directory error structure is incorrect"; fi

# ------------------------------------------------------------------- case 15: help
expect 0 "help prints normally" python3 "$CHECKER" --help
contains "help includes usage" "usage:"

echo
if [ "$FAILED" -eq 0 ]; then
  printf '\033[32mself-test passed\033[0m: %d/%d checks passed\n' "$TOTAL" "$TOTAL"
  exit 0
else
  printf '\033[31mself-test failed\033[0m: %d/%d checks failed\n' "$FAILED" "$TOTAL"
  exit 1
fi
