#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_JAR="${1:-$ROOT_DIR/target/j2me-poc-0.1.0.jar}"
WIDTH="${2:-240}"
HEIGHT="${3:-320}"
SCALE="${4:-2}"
FREEJ2ME_JAR="$ROOT_DIR/tools/freej2me/build/freej2me.jar"

if [[ "$APP_JAR" != /* ]]; then
  APP_JAR="$ROOT_DIR/$APP_JAR"
fi

if [[ ! -f "$FREEJ2ME_JAR" ]]; then
  echo "Missing $FREEJ2ME_JAR"
  echo "Build it with: ant -f tools/freej2me/build.xml"
  exit 1
fi

if [[ ! -f "$APP_JAR" ]]; then
  echo "Missing $APP_JAR"
  echo "Build your MIDlet with: mvn package"
  exit 1
fi

cd "$ROOT_DIR"
java -jar "$FREEJ2ME_JAR" "file://$APP_JAR" "$WIDTH" "$HEIGHT" "$SCALE"
