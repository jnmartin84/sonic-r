#!/usr/bin/env bash
set -euo pipefail

# KOS wav2adpcm tool
WAV2ADPCM="/content/wav2adpcm"

# Directory to process (default = current dir)
DIR="${1:-.}"

if [[ ! -x "$WAV2ADPCM" ]]; then
    echo "Error: wav2adpcm not found or not executable at:"
    echo "  $WAV2ADPCM"
    exit 1
fi

shopt -s nullglob nocaseglob

for wav in "$DIR"/*.wav; do
    filename="$(basename "$wav")"
    tmp_out="$DIR/$filename.adpcm"

    echo "Converting in-place filename: $filename"

    "$WAV2ADPCM" -t "$wav" "$tmp_out"

    # Replace original WAV with ADPCM data using same filename
    mv -f "$tmp_out" "$wav"
done

echo "Done."
