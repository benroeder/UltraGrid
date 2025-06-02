#!/bin/bash
# Test script for I420 overlay functionality

echo "Testing I420 overlay support..."

# Create a simple PAM overlay file if it doesn't exist
if [ ! -f overlay.pam ]; then
    echo "Creating test overlay.pam..."
    python3 -c "
import struct
w, h = 200, 100
with open('overlay.pam', 'wb') as f:
    f.write(f'P7\\nWIDTH {w}\\nHEIGHT {h}\\nDEPTH 4\\nMAXVAL 255\\nTUPLTYPE RGB_ALPHA\\nENDHDR\\n'.encode())
    for y in range(h):
        for x in range(w):
            # Red with 50% transparency
            f.write(struct.pack('BBBB', 255, 0, 0, 128))
"
fi

# Test with I420 codec
echo "Running UltraGrid with I420 codec and overlay..."
./bin/uv -t testcard:codec=I420:size=1280x720 -p overlay:position=center:perf -d sdl

echo "Test complete."