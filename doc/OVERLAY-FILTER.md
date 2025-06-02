# UltraGrid Overlay Filter

The overlay filter is a video postprocessor that allows you to overlay a semi-transparent image onto your video stream. It supports dynamic image reloading, flexible positioning, automatic scaling, and hardware-accelerated alpha blending.

## Features

- **PAM Image Format Support**: Uses PAM (Portable Arbitrary Map) format with alpha channel for transparency
- **Dynamic Reloading**: Automatically reloads the overlay image when the file is modified
- **Flexible Positioning**: Position overlay using presets or custom pixel coordinates
- **Automatic Scaling**: Option to scale overlay to match video resolution
- **Alpha Blending**: Full alpha channel support for smooth transparency effects
- **Optimized Scalar Implementation**: High-performance scalar blending with exact division
- **Performance Monitoring**: Optional real-time performance statistics
- **Error Recovery**: Graceful handling of missing or invalid overlay files

## Usage

Basic syntax:
```bash
uv -t <source> -p overlay[:options] -d <display>
```

### Parameters

- **`file=<path>`** - Path to PAM overlay image (default: `overlay.pam`)
- **`position=<pos>`** - Overlay position: `center`, `topleft`, `topright`, `bottomleft`, `bottomright` (default: `center`)
- **`x=<pixels>`** - Custom X position in pixels (overrides position parameter)
- **`y=<pixels>`** - Custom Y position in pixels (overrides position parameter)
- **`scale=<mode>`** - Scaling mode: `fit` or `none` (default: `fit`)
- **`perf`** - Enable performance monitoring (reports every 5 seconds)

### Examples

1. **Basic overlay in top-right corner:**
   ```bash
   uv -t testcard -p overlay:file=logo.pam:position=topright -d sdl
   ```

2. **Custom positioning with performance monitoring:**
   ```bash
   uv -t testcard -p overlay:file=watermark.pam:x=100:y=50:perf -d sdl
   ```

3. **Centered overlay without scaling:**
   ```bash
   uv -t decklink -p overlay:file=bug.pam:scale=none -d gl
   ```

4. **Overlay with negative positioning (from edges):**
   ```bash
   uv -t testcard -p overlay:x=-100:y=-50 -d sdl
   ```

## Creating Overlay Images

The overlay filter uses PAM format images with an alpha channel. You can create these using various tools:

### Using ImageMagick
```bash
# Convert PNG with transparency to PAM
convert logo.png -define pam:depth=4 overlay.pam
```

### Using Python
```python
import struct

# Create a 400x200 RGBA PAM image
width, height = 400, 200
header = f'''P7
WIDTH {width}
HEIGHT {height}
DEPTH 4
MAXVAL 255
TUPLTYPE RGB_ALPHA
ENDHDR
'''.encode('ascii')

# Generate RGBA pixel data
pixels = bytearray()
for y in range(height):
    for x in range(width):
        r = 255  # Red
        g = 0    # Green
        b = 0    # Blue
        a = 128  # Alpha (50% transparent)
        pixels.extend([r, g, b, a])

with open('overlay.pam', 'wb') as f:
    f.write(header)
    f.write(pixels)
```

## Performance Optimization

The overlay filter includes several optimizations:

1. **Optimized Scalar Alpha Blending**: 
   - Uses exact division by 255 for precise results without aliasing
   - Achieves ~2000 megapixels/second on modern hardware
   - Portable across all architectures without SIMD-specific code

2. **Cached Scaling**: Scaled overlays are cached to avoid repeated scaling operations

3. **Efficient File Monitoring**: Uses file modification timestamps to detect changes

### Performance Statistics

When `perf` is enabled, the filter reports:
- Average processing time per frame
- Maximum theoretical FPS
- Time breakdown by operation (load, scale, decode, blend, encode)
- Overlay dimensions and scaling status

Example output:
```
[overlay] Performance stats (150 frames in 5 seconds):
[overlay]   Average per frame: 5.3 ms (188.7 FPS max)
[overlay]   Load:   0.1 ms/op (1 ops, 0.0%)
[overlay]   Scale:  5.9 ms/op (1 ops, 0.4%)
[overlay]   Decode: 2.5 ms/frame (47.2%)
[overlay]   Blend:  5.2 ms/frame (98.1%)
[overlay]   Encode: 1.7 ms/frame (32.1%)
[overlay]   Overlay: 400x200 -> 1920x1080 (scaled)
```

## Tips and Best Practices

1. **Image Size**: Keep overlay images as small as necessary to reduce processing overhead
2. **Transparency**: Use alpha channel for smooth edges and partial transparency
3. **File Format**: Ensure PAM files have 4 channels (RGBA) and 8-bit depth
4. **Dynamic Updates**: The overlay file can be updated while UltraGrid is running
5. **Positioning**: Use negative X/Y values to position relative to right/bottom edges
6. **Performance**: Enable `perf` parameter during testing to monitor impact

## Troubleshooting

### Common Issues

1. **"Overlay file not found"**
   - Ensure the file path is correct
   - The filter will retry loading during processing

2. **"Unsupported channel count"**
   - PAM file must have 3 (RGB) or 4 (RGBA) channels
   - 4 channels recommended for transparency

3. **"Invalid image dimensions"**
   - Check that width and height are positive values
   - Very large overlays may impact performance

4. **Poor Performance**
   - Enable performance monitoring with `perf`
   - Consider reducing overlay size
   - The scalar implementation is optimized for performance

### Debug Output

Overlay loading messages are suppressed by default to reduce console noise. To enable verbose logging:

```bash
# Enable debug level logging for all modules
export UG_VERBOSE=7

# Enable debug logging only for overlay module
export UG_VERBOSE_overlay=7
```

The overlay module uses an optimized scalar implementation that provides excellent performance across all platforms.

With debug logging enabled, you'll see overlay loading messages:
```
[overlay] Loading overlay image: /path/to/overlay.pam
[overlay] Loaded overlay image: 1920x1080
```

## Technical Details

- **Color Space**: Overlay blending occurs in RGBA color space
- **Alpha Blending Formula**: `output = (overlay * alpha + video * (255 - alpha)) / 255`
- **File Monitoring**: Uses nanosecond precision on macOS/Linux/Windows for accurate change detection
- **Thread Safety**: Single-threaded processing per frame
- **Memory Usage**: Approximately `width * height * 4` bytes per overlay

## See Also

- UltraGrid postprocessor documentation
- PAM image format specification
- Performance tuning guide