#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

if [[ $# -ne 1 ]]; then
  echo "Usage: ./view.sh <sample_id>"
  echo "Example: ./view.sh 4"
  exit 1
fi

SAMPLE_ID="$1"
if ! [[ "${SAMPLE_ID}" =~ ^[0-9]+$ ]]; then
  echo "Error: sample_id must be a number, got '${SAMPLE_ID}'" >&2
  exit 1
fi

INPUT_PATH="samples/sample_${SAMPLE_ID}.txt"
SOL_PATH="sample_${SAMPLE_ID}_solution.txt"

if [[ ! -f "${INPUT_PATH}" ]]; then
  echo "Error: input file not found: ${INPUT_PATH}" >&2
  exit 1
fi

if [[ ! -f "${SOL_PATH}" ]]; then
  echo "Error: solution file not found: ${SOL_PATH}" >&2
  exit 1
fi

exec make visualize \
  "INPUT=${INPUT_PATH}" \
  "SOL=${SOL_PATH}" \
  "SHOW_PINS=1" \
  "PIN_LABELS=1"
