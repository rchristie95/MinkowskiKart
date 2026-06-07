#!/bin/bash
# MinkowskiKart - Smoke Test for macOS
# (C) 2026 MinkowskiKart Team

set -e

APP_PATH="$1"

if [ -z "${APP_PATH}" ]; then
    echo "Usage: $0 <path_to_MinkowskiKart.app>"
    exit 1
fi

EXECUTABLE="${APP_PATH}/Contents/MacOS/MinkowskiKart"

if [ ! -x "${EXECUTABLE}" ]; then
    echo "ERROR: Executable not found or not executable: ${EXECUTABLE}"
    exit 1
fi

echo "================================================"
echo "  MinkowskiKart - Smoke Test (macOS)"
echo "================================================"
echo "Testing: ${APP_PATH}"
echo "================================================"

# Run the game for 10 seconds in the background
# Use flags to skip intro and start a race immediately
LOG_FILE="smoke_test.log"
echo "Launching game for initialization check..."
"${EXECUTABLE}" --no-start-screen --track=sandtrack --kart=minkowski --laps=1 --numkarts=2 > "${LOG_FILE}" 2>&1 &
GAME_PID=$!

sleep 10

if ps -p $GAME_PID > /dev/null; then
    echo "Game is still running after 10s. Success!"
    kill $GAME_PID
else
    echo "ERROR: Game process exited prematurely."
    echo "Last 20 lines of log:"
    tail -n 20 "${LOG_FILE}"
    exit 1
fi

echo "================================================"
echo "  SMOKE TEST PASSED"
echo "================================================"
