#!/bin/bash

echo "=== Testing Ctrl+C Signal Handling ==="

# Start pennos in background
./bin/pennos &
PENNOS_PID=$!

# Give it time to start
sleep 1

echo "Started pennos with PID: $PENNOS_PID"

# Send SIGINT (Ctrl+C) to the process
echo "Sending SIGINT to pennos..."
kill -INT $PENNOS_PID

# Wait a bit to see if it handles the signal
sleep 2

# Check if the process is still running
if kill -0 $PENNOS_PID 2>/dev/null; then
    echo "SUCCESS: pennos is still running after SIGINT (shell handled it correctly)"
    # Clean up - send Ctrl+D (EOF) by connecting stdin and sending EOF
    echo "" | exec 0<&- 1>&2 2>&1 | exec ./bin/pennos
    sleep 1
    kill -TERM $PENNOS_PID 2>/dev/null
else
    echo "FAILED: pennos was killed by SIGINT (signal went to wrong handler)"
fi

wait $PENNOS_PID 2>/dev/null

echo "=== Test completed ===" 