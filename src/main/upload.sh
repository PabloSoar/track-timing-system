#!/usr/bin/env bash
set -euo pipefail

FQBN="esp32:esp32:esp32"
PORT="${ESP32_PORT:-/dev/ttyUSB0}"
BAUD="${ESP32_BAUD:-921600}"
MODE="all"

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SKETCH_DIR/build"
LITTLEFS_IMAGE="$BUILD_DIR/littlefs.bin"
ARDUINO_BUILD_DIR="$BUILD_DIR/arduino"
ARDUINO_OUTPUT_DIR="$BUILD_DIR/out"

MKLITTLEFS="$HOME/.arduino15/packages/esp32/tools/mklittlefs/4.0.2-db0513a/mklittlefs"
ESPTOOL="$HOME/.arduino15/packages/esp32/tools/esptool_py/5.2.0/esptool"

show_usage() {
  cat <<EOF
Usage: ./upload.sh [all|sketch|littlefs] [-p PORT] [-b BAUD]

Modes:
  all       Compile/upload sketch and upload LittleFS data (default)
  sketch    Compile/upload only main.ino
  littlefs  Build and upload only data/

Options:
  -p PORT   Serial port (default: /dev/ttyUSB0, or ESP32_PORT env var)
  -b BAUD   Upload baud rate (default: 921600, or ESP32_BAUD env var)
  -h        Show this help

Examples:
  ./upload.sh
  ./upload.sh sketch
  ./upload.sh littlefs -p /dev/ttyUSB0
  ESP32_PORT=/dev/ttyUSB1 ./upload.sh all
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    all|sketch|littlefs)
      MODE="$1"
      shift
      ;;
    -p|--port)
      PORT="${2:?Missing port after $1}"
      shift 2
      ;;
    -b|--baud)
      BAUD="${2:?Missing baud rate after $1}"
      shift 2
      ;;
    -h|--help)
      show_usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      show_usage >&2
      exit 1
      ;;
  esac
done

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing command: $1" >&2
    exit 1
  fi
}

require_file() {
  if [[ ! -x "$1" ]]; then
    echo "Missing executable: $1" >&2
    exit 1
  fi
}

upload_sketch() {
  require_command arduino-cli
  mkdir -p "$ARDUINO_BUILD_DIR" "$ARDUINO_OUTPUT_DIR"

  echo "Compiling and uploading sketch to $PORT..."
  arduino-cli compile \
    --upload \
    -p "$PORT" \
    --fqbn "$FQBN" \
    --build-path "$ARDUINO_BUILD_DIR" \
    --output-dir "$ARDUINO_OUTPUT_DIR" \
    "$SKETCH_DIR"
}

upload_littlefs() {
  require_file "$MKLITTLEFS"
  require_file "$ESPTOOL"

  if [[ ! -d "$SKETCH_DIR/data" ]]; then
    echo "Missing data directory: $SKETCH_DIR/data" >&2
    exit 1
  fi

  mkdir -p "$BUILD_DIR"

  echo "Building LittleFS image..."
  "$MKLITTLEFS" \
    -c "$SKETCH_DIR/data" \
    -b 4096 \
    -p 256 \
    -s 0x160000 \
    "$LITTLEFS_IMAGE"

  echo "Uploading LittleFS image to $PORT..."
  "$ESPTOOL" \
    --chip esp32 \
    --port "$PORT" \
    --baud "$BAUD" \
    --before default-reset \
    --after hard-reset \
    write-flash \
    -z \
    --flash-mode keep \
    --flash-freq keep \
    --flash-size keep \
    0x290000 "$LITTLEFS_IMAGE"
}

case "$MODE" in
  all)
    upload_sketch
    upload_littlefs
    ;;
  sketch)
    upload_sketch
    ;;
  littlefs)
    upload_littlefs
    ;;
esac

echo "Done."
