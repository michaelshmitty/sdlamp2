#!/usr/bin/env bash
#
# concat_cassette.sh — Losslessly concatenate m4a files into a single file
# with chapter markers derived from input filenames.
#
# Usage:
#   ./tools/concat_cassette.sh -o "Cassette 1.m4a" story1.m4a story2.m4a story3.m4a
#
# Requires: ffmpeg, ffprobe

set -euo pipefail

usage() {
  echo "Usage: $0 -o OUTPUT INPUT1 INPUT2 [INPUT3 ...]"
  echo ""
  echo "Losslessly concatenate m4a files into a single file with chapter markers."
  echo "Album art is preserved from the first input file."
  echo ""
  echo "Options:"
  echo "  -o OUTPUT   Output filename (must not already exist)"
  exit 1
}

die() {
  echo "Error: $1" >&2
  exit 1
}

# --- Parse arguments ---

output=""
while getopts "o:h" opt; do
  case "$opt" in
    o) output="$OPTARG" ;;
    h) usage ;;
    *) usage ;;
  esac
done
shift $((OPTIND - 1))

[ -z "$output" ] && die "Missing -o OUTPUT"
[ $# -lt 2 ] && die "Need at least 2 input files"
[ -e "$output" ] && die "Output file already exists: $output"

for cmd in ffmpeg ffprobe; do
  command -v "$cmd" >/dev/null 2>&1 || die "$cmd not found"
done

inputs=("$@")
for f in "${inputs[@]}"; do
  [ -f "$f" ] || die "Input file not found: $f"
done

# --- Set up temp dir ---

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

concat_list="$tmpdir/concat.txt"
metadata="$tmpdir/metadata.txt"

# --- Build concat list and probe durations ---

echo ";FFMETADATA1" > "$metadata"

cumulative_ms=0
for f in "${inputs[@]}"; do
  # Concat list entry (absolute path for -safe 0)
  abs=$(cd "$(dirname "$f")" && pwd)/$(basename "$f")
  printf "file '%s'\n" "$abs" >> "$concat_list"

  # Probe duration
  dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f")
  [ -z "$dur" ] && die "Could not read duration of: $f"
  dur_ms=$(awk "BEGIN { printf \"%d\", $dur * 1000 }")

  # Chapter title from filename (strip path and extension)
  title=$(basename "$f")
  title="${title%.*}"

  end_ms=$((cumulative_ms + dur_ms))
  cat >> "$metadata" <<EOF

[CHAPTER]
TIMEBASE=1/1000
START=$cumulative_ms
END=$end_ms
title=$title
EOF
  cumulative_ms=$end_ms
done

# --- Concatenate ---

ffmpeg -y -f concat -safe 0 -i "$concat_list" -i "$metadata" \
  -map_metadata 1 -c copy -movflags +faststart "$output" 2>&1

# --- Summary ---

total_secs=$((cumulative_ms / 1000))
mins=$((total_secs / 60))
secs=$((total_secs % 60))

echo ""
echo "Done: $output"
echo "  Chapters: ${#inputs[@]}"
echo "  Duration: ${mins}m ${secs}s"
