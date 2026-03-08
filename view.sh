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
TREE_PATH_NEW="init_fp_bstar_tree_sample_${SAMPLE_ID}.txt"
TREE_PATH_OLD="init_fp_bstar_tree.txt"

TREE_PATH=""
if [[ -f "${TREE_PATH_NEW}" ]]; then
  TREE_PATH="${TREE_PATH_NEW}"
elif [[ -f "${TREE_PATH_OLD}" ]]; then
  TREE_PATH="${TREE_PATH_OLD}"
fi

if [[ ! -f "${INPUT_PATH}" ]]; then
  echo "Error: input file not found: ${INPUT_PATH}" >&2
  exit 1
fi

if [[ ! -f "${SOL_PATH}" ]]; then
  echo "Error: solution file not found: ${SOL_PATH}" >&2
  exit 1
fi

if [[ -z "${TREE_PATH}" ]]; then
  echo "Error: B*-tree file not found. Tried '${TREE_PATH_NEW}' and '${TREE_PATH_OLD}'." >&2
  echo "Hint: run 'make run INPUT=${INPUT_PATH} T=1' first to generate the tree file." >&2
  exit 1
fi

make visualize \
  "INPUT=${INPUT_PATH}" \
  "SOL=${SOL_PATH}" \
  "SHOW_PINS=1" \
  "PIN_LABELS=1" &
PID_FP=$!

python bstar.py --bstar "${TREE_PATH}" --sample "${INPUT_PATH}" --show &
PID_TREE=$!

STATUS=0

if ! wait "${PID_FP}"; then
  STATUS=1
fi
if ! wait "${PID_TREE}"; then
  STATUS=1
fi

exit "${STATUS}"
