#!/usr/bin/env bash
set -u

CAMERA_IP=""
PORT="8080"
APP_ID=""
OUTPUT_DIR="test-results"
CHANNEL_COUNT="4"
TIMEOUT="15"

usage() {
  cat <<'USAGE'
Usage:
  bash scripts/test-all-channels.sh \
    --camera-ip <address> --app-id <app-id> \
    [--port 8080] [--channels 4] [--timeout 15] [--output-dir test-results]

Runs Start Server once, then requests /image/jpg?channel=N sequentially.
USAGE
}

while (($#)); do
  case "$1" in
    --camera-ip) CAMERA_IP="${2:-}"; shift 2 ;;
    --port) PORT="${2:-}"; shift 2 ;;
    --app-id) APP_ID="${2:-}"; shift 2 ;;
    --channels) CHANNEL_COUNT="${2:-}"; shift 2 ;;
    --timeout) TIMEOUT="${2:-}"; shift 2 ;;
    --output-dir) OUTPUT_DIR="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ -n "$CAMERA_IP" ]] || { echo "--camera-ip is required" >&2; exit 2; }
[[ -n "$APP_ID" ]] || { echo "--app-id is required" >&2; exit 2; }
[[ "$PORT" =~ ^[0-9]+$ ]] || { echo "--port must be numeric" >&2; exit 2; }
[[ "$CHANNEL_COUNT" =~ ^[1-9][0-9]*$ ]] || { echo "--channels must be positive" >&2; exit 2; }
[[ "$TIMEOUT" =~ ^[1-9][0-9]*$ ]] || { echo "--timeout must be positive" >&2; exit 2; }

mkdir -p "$OUTPUT_DIR"
OPEN_API_BASE="http://${CAMERA_IP}/opensdk/${APP_ID}"
IMAGE_BASE="http://${CAMERA_IP}:${PORT}/image/jpg"
START_BODY="${OUTPUT_DIR}/startserver.json"

start_meta=$(curl --silent --show-error --connect-timeout "$TIMEOUT" \
  --max-time "$TIMEOUT" --output "$START_BODY" \
  --write-out '%{http_code} %{time_total}' \
  --request POST --header 'Content-Type: application/json' \
  --data "{\"app_id\":\"${APP_ID}\",\"port\":\"${PORT}\"}" \
  "${OPEN_API_BASE}/startserver")
start_curl=$?
read -r start_http start_time <<<"${start_meta:-000 0}"
if ((start_curl != 0)) || [[ "$start_http" != "200" && "$start_http" != "202" ]]; then
  echo "[SERVER] FAIL HTTP ${start_http:-000} curl=${start_curl}" >&2
  exit 1
fi
printf '[SERVER] PASS HTTP %s time=%ss\n' "$start_http" "$start_time"

passed=0
failed=0
results_file="${OUTPUT_DIR}/result.json"
printf '{\n  "channel_count": %s,\n  "channels": [\n' \
  "$CHANNEL_COUNT" >"$results_file"

for ((channel=0; channel<CHANNEL_COUNT; channel++)); do
  output_file="${OUTPUT_DIR}/full_ch${channel}.jpg"
  error_file="${OUTPUT_DIR}/full_ch${channel}.error.json"
  meta=$(curl --silent --show-error --connect-timeout "$TIMEOUT" \
    --max-time "$TIMEOUT" --output "$output_file" \
    --write-out '%{http_code} %{time_total} %{size_download}' \
    "${IMAGE_BASE}?channel=${channel}")
  curl_status=$?
  read -r http_status elapsed size <<<"${meta:-000 0 0}"
  magic=""
  if [[ -f "$output_file" ]]; then
    magic=$(od -An -tx1 -N3 "$output_file" | tr -d ' \n')
  fi

  if ((curl_status == 0)) && [[ "$http_status" == "200" && "$magic" == "ffd8ff" ]]; then
    status="success"
    error_code=""
    ((passed+=1))
    printf '[CH %d/API %d] PASS HTTP %s time=%ss bytes=%s\n' \
      "$((channel + 1))" "$channel" "$http_status" "$elapsed" "$size"
  else
    status="error"
    error_code=$([[ "$http_status" != "200" ]] && echo "HTTP_ERROR" || echo "INVALID_JPEG_RESPONSE")
    ((failed+=1))
    if [[ -f "$output_file" ]]; then
      mv "$output_file" "$error_file"
    fi
    printf '[CH %d/API %d] FAIL HTTP %s curl=%s code=%s\n' \
      "$((channel + 1))" "$channel" "${http_status:-000}" "$curl_status" "$error_code" >&2
  fi

  json_http=$((10#${http_status:-0}))
  comma=','
  if ((channel == CHANNEL_COUNT - 1)); then comma=''; fi
  printf '    {"channel": %d, "label": "CH %d", "status": "%s", "http": %s, "seconds": %s, "bytes": %s, "error_code": "%s"}%s\n' \
    "$channel" "$((channel + 1))" "$status" "$json_http" "${elapsed:-0}" \
    "${size:-0}" "$error_code" "$comma" >>"$results_file"
done

overall="success"
if ((failed > 0)); then overall="failed"; fi
printf '  ],\n  "passed": %d,\n  "failed": %d,\n  "status": "%s"\n}\n' \
  "$passed" "$failed" "$overall" >>"$results_file"
printf 'RESULT: %d PASS / %d FAIL · %s\n' "$passed" "$failed" "$results_file"

((failed == 0))