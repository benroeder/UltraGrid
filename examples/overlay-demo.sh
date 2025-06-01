#!/bin/bash
# UltraGrid Overlay Filter Demo Script
# This script demonstrates various overlay filter features

# Create example overlay images if they don't exist
create_sample_overlays() {
    echo "Creating sample overlay images..."
    
    # Create a semi-transparent red watermark
    python3 - <<EOF
import struct

# Watermark overlay (200x50)
width, height = 200, 50
header = f'''P7
WIDTH {width}
HEIGHT {height}
DEPTH 4
MAXVAL 255
TUPLTYPE RGB_ALPHA
ENDHDR
'''.encode('ascii')

pixels = bytearray()
for y in range(height):
    for x in range(width):
        r = 255
        g = 0
        b = 0
        a = 128  # 50% transparent
        pixels.extend([r, g, b, a])

with open('watermark.pam', 'wb') as f:
    f.write(header)
    f.write(pixels)
print("Created watermark.pam (200x50)")
EOF

    # Create a logo with gradient
    python3 - <<EOF
import struct

# Logo overlay with gradient (300x100)
width, height = 300, 100
header = f'''P7
WIDTH {width}
HEIGHT {height}
DEPTH 4
MAXVAL 255
TUPLTYPE RGB_ALPHA
ENDHDR
'''.encode('ascii')

pixels = bytearray()
for y in range(height):
    for x in range(width):
        # Create gradient effect
        r = int(255 * x / width)
        g = int(255 * y / height)
        b = 128
        a = int(200 * (1 - y / height))  # Fade from top to bottom
        pixels.extend([r, g, b, a])

with open('logo.pam', 'wb') as f:
    f.write(header)
    f.write(pixels)
print("Created logo.pam (300x100)")
EOF
}

# Function to run UltraGrid with overlay
run_demo() {
    local description=$1
    local params=$2
    local duration=${3:-10}
    
    echo
    echo "=== Demo: $description ==="
    echo "Command: uv -t testcard -p overlay$params -d sdl"
    echo "Running for $duration seconds..."
    
    timeout $duration ./bin/uv -t testcard:fps=30:size=1920x1080 -p overlay$params -d sdl 2>&1 | grep -E "\[overlay\]|\[SDL\]|FPS"
}

# Main demo
main() {
    cd "$(dirname "$0")/.." || exit 1
    
    # Check if UltraGrid is built
    if [ ! -x ./bin/uv ]; then
        echo "Error: UltraGrid binary not found. Please build UltraGrid first."
        exit 1
    fi
    
    # Create sample overlays
    create_sample_overlays
    
    echo "Starting UltraGrid Overlay Filter Demo"
    echo "======================================"
    
    # Demo 1: Basic overlay
    run_demo "Basic overlay in center" ":file=watermark.pam"
    
    # Demo 2: Position presets
    run_demo "Logo in top-right corner" ":file=logo.pam:position=topright"
    
    # Demo 3: Custom positioning
    run_demo "Custom position (100,50)" ":file=logo.pam:x=100:y=50"
    
    # Demo 4: No scaling
    run_demo "Overlay without scaling" ":file=watermark.pam:scale=none:position=bottomleft"
    
    # Demo 5: Performance monitoring
    run_demo "With performance monitoring" ":file=logo.pam:perf" 15
    
    # Demo 6: Negative positioning
    run_demo "Positioned from bottom-right (-50,-50)" ":file=watermark.pam:x=-50:y=-50"
    
    echo
    echo "Demo complete!"
    echo
    echo "Tips:"
    echo "- You can modify the overlay files while UltraGrid is running"
    echo "- Try: convert your_image.png -define pam:depth=4 overlay.pam"
    echo "- Check doc/OVERLAY-FILTER.md for full documentation"
    
    # Cleanup
    read -p "Remove demo overlay files? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -f watermark.pam logo.pam
        echo "Demo files removed."
    fi
}

main "$@"