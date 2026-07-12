#!/usr/bin/env bash
set -euo pipefail

: "${GITHUB_WORKSPACE:?GITHUB_WORKSPACE is required}"
: "${GITHUB_ENV:?GITHUB_ENV is required}"
: "${GITHUB_RUN_ID:?GITHUB_RUN_ID is required}"
: "${SEARCH_DEPTH:?SEARCH_DEPTH is required}"
: "${VALIDATION_POSITIONS:?VALIDATION_POSITIONS is required}"
: "${SELFPLAY_CHUNK_POSITIONS:?SELFPLAY_CHUNK_POSITIONS is required}"
: "${SELFPLAY_DEADLINE:?SELFPLAY_DEADLINE is required}"
: "${DATA_CAP_BYTES:?DATA_CAP_BYTES is required}"

# The upstream generator does not impose a depth-12 ceiling. The wall-clock
# budget bounds total compute, so only require a positive integer.
if ! [[ "$SEARCH_DEPTH" =~ ^[1-9][0-9]*$ ]]; then
  echo "::error::SEARCH_DEPTH must be a positive integer"
  exit 1
fi

# Higher fixed depths make each generated position much more expensive. Reduce
# only the chunk granularity (not the total wall-clock budget) so the timer is
# checked frequently enough and a single chunk cannot consume the entire job.
EFFECTIVE_CHUNK_POSITIONS="$SELFPLAY_CHUNK_POSITIONS"
EFFECTIVE_VALIDATION_POSITIONS="$VALIDATION_POSITIONS"

if (( SEARCH_DEPTH >= 17 )); then
  EFFECTIVE_CHUNK_POSITIONS=250
  EFFECTIVE_VALIDATION_POSITIONS=2000
elif (( SEARCH_DEPTH >= 13 )); then
  EFFECTIVE_CHUNK_POSITIONS=1000
  EFFECTIVE_VALIDATION_POSITIONS=5000
elif (( SEARCH_DEPTH >= 9 )); then
  EFFECTIVE_CHUNK_POSITIONS=5000
  EFFECTIVE_VALIDATION_POSITIONS=10000
fi

echo "Self-play search depth: $SEARCH_DEPTH"
echo "Self-play chunk positions: $EFFECTIVE_CHUNK_POSITIONS"
echo "Validation positions: $EFFECTIVE_VALIDATION_POSITIONS"

mkdir -p data logs
: > data/fresh-train.bin

write_generator_commands() {
  local output_file="$1"
  local position_count="$2"
  local seed_value="$3"
  local command_file="$4"

  {
    printf '%s\n' 'uci'
    printf '%s\n' 'setoption name UCI_Variant value mini7xiangqi'
    printf '%s\n' 'setoption name Use NNUE value false'
    printf '%s\n' 'setoption name Threads value 4'
    printf '%s\n' 'setoption name Hash value 1024'
    printf '%s\n' 'isready'
    printf '%s\n' \
      "generate_training_data depth ${SEARCH_DEPTH} count ${position_count} output_file_name ${output_file} random_move_min_ply 1 random_move_max_ply 18 random_move_count 5 random_multi_pv 4 random_multi_pv_diff 120 random_multi_pv_depth ${SEARCH_DEPTH} write_min_ply 4 write_max_ply 240 eval_limit 6000 keep_draws 0.40 filter_captures true filter_checks false filter_promotions false seed ${seed_value} set_recommended_uci_options data_format bin"
    printf '%s\n' 'quit'
  } > "$command_file"
}

SELFPLAY_START="$(date +%s)"

write_generator_commands \
  "$GITHUB_WORKSPACE/data/fresh-validation.bin" \
  "$EFFECTIVE_VALIDATION_POSITIONS" \
  "${GITHUB_RUN_ID}-validation" \
  "commands-validation.txt"

tools/src/stockfish < commands-validation.txt \
  2>&1 | tee logs/validation-generator.log
test -s data/fresh-validation.bin
rm -f commands-validation.txt

CHUNK_INDEX=0
GENERATED_POSITIONS=0

while true; do
  NOW="$(date +%s)"
  CURRENT_BYTES="$(stat -c%s data/fresh-train.bin)"

  if (( CHUNK_INDEX > 0 && NOW >= SELFPLAY_DEADLINE - 20 )); then
    echo "Self-play timer reached."
    break
  fi
  if (( CURRENT_BYTES >= DATA_CAP_BYTES )); then
    echo "Self-play data cap reached: ${CURRENT_BYTES} bytes."
    break
  fi

  CHUNK_INDEX=$((CHUNK_INDEX + 1))
  CHUNK_FILE="$GITHUB_WORKSPACE/data/chunk-${CHUNK_INDEX}.bin"
  COMMAND_FILE="commands-chunk-${CHUNK_INDEX}.txt"

  write_generator_commands \
    "$CHUNK_FILE" \
    "$EFFECTIVE_CHUNK_POSITIONS" \
    "${GITHUB_RUN_ID}-${CHUNK_INDEX}" \
    "$COMMAND_FILE"

  tools/src/stockfish < "$COMMAND_FILE" \
    >> logs/train-generator.log 2>&1

  test -s "$CHUNK_FILE"
  cat "$CHUNK_FILE" >> data/fresh-train.bin
  rm -f "$CHUNK_FILE" "$COMMAND_FILE"

  GENERATED_POSITIONS=$((GENERATED_POSITIONS + EFFECTIVE_CHUNK_POSITIONS))

  if (( CHUNK_INDEX % 10 == 0 )); then
    CURRENT_BYTES="$(stat -c%s data/fresh-train.bin)"
    echo "Self-play chunks=${CHUNK_INDEX}, positions=${GENERATED_POSITIONS}, bytes=${CURRENT_BYTES}"
  fi
done

test -s data/fresh-train.bin

SELFPLAY_END="$(date +%s)"
SELFPLAY_ELAPSED=$((SELFPLAY_END - SELFPLAY_START))
FRESH_DATA_BYTES="$(stat -c%s data/fresh-train.bin)"

{
  echo "GENERATED_POSITIONS=$GENERATED_POSITIONS"
  echo "SELFPLAY_ELAPSED_SECONDS=$SELFPLAY_ELAPSED"
  echo "FRESH_DATA_BYTES=$FRESH_DATA_BYTES"
  echo "VALIDATION_POSITIONS=$EFFECTIVE_VALIDATION_POSITIONS"
  echo "EFFECTIVE_SELFPLAY_CHUNK_POSITIONS=$EFFECTIVE_CHUNK_POSITIONS"
} >> "$GITHUB_ENV"

echo "Fresh self-play positions: $GENERATED_POSITIONS"
echo "Fresh self-play bytes: $FRESH_DATA_BYTES"
echo "Self-play elapsed seconds: $SELFPLAY_ELAPSED"
