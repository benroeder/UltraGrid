UltraGrid Overlay Filter Quick Reference
========================================

BASIC USAGE
-----------
uv -t <source> -p overlay[:options] -d <display>

PARAMETERS
----------
file=<path>         Path to PAM image (default: overlay.pam)
position=<pos>      center, topleft, topright, bottomleft, bottomright
x=<pixels>          Custom X position (negative = from right edge)
y=<pixels>          Custom Y position (negative = from bottom edge)
scale=<mode>        fit (default) or none
perf                Enable performance stats

EXAMPLES
--------
# Basic overlay
uv -t testcard -p overlay -d sdl

# Logo in corner
uv -t testcard -p overlay:file=logo.pam:position=topright -d sdl

# Custom position
uv -t testcard -p overlay:x=100:y=50 -d sdl

# From bottom-right
uv -t testcard -p overlay:x=-100:y=-50 -d sdl

# Performance monitoring
uv -t testcard -p overlay:perf -d sdl

CREATE PAM FILES
----------------
# From PNG with ImageMagick
convert image.png -define pam:depth=4 overlay.pam

# From Python
python3 -c "
import struct
w, h = 200, 100
with open('overlay.pam', 'wb') as f:
    f.write(f'P7\\nWIDTH {w}\\nHEIGHT {h}\\nDEPTH 4\\nMAXVAL 255\\nTUPLTYPE RGB_ALPHA\\nENDHDR\\n'.encode())
    for y in range(h):
        for x in range(w):
            f.write(struct.pack('BBBB', 255, 0, 0, 128))  # RGBA
"

FEATURES
--------
✓ Dynamic reloading (file changes detected automatically)
✓ Alpha transparency support
✓ Optimized scalar implementation with exact division
✓ Automatic scaling to video resolution
✓ Flexible positioning
✓ Performance monitoring

TIPS
----
• Keep overlays small for best performance
• Use alpha channel for smooth edges
• Files can be updated while running
• Enable 'perf' to check impact

DEBUG LOGGING
-------------
# Show overlay loading messages and verbose output
export UG_VERBOSE=7

# Module-specific debug logging
export UG_VERBOSE_overlay=7

See doc/OVERLAY-FILTER.md for full documentation