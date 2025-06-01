# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Initial setup (only needed once)
./autogen.sh

# Configure build
./configure

# Build UltraGrid
make -j$(sysctl -n hw.ncpu)    # macOS
make -j$(nproc)                # Linux

# Run tests
make tests
# or
make check

# Clean build artifacts
make clean

# Install (optional)
sudo make install
```

## Running UltraGrid

```bash
# Basic usage pattern
./bin/uv -t <capture_device> -c <compression> -d <display_device> hostname

# Common examples
./bin/uv -t testcard -d sdl              # Test pattern with SDL display
./bin/uv -t decklink -d gl               # Decklink capture with OpenGL display
./bin/uv -h                              # Basic help
./bin/uv -H                              # Full help
./bin/uv -t help                         # List capture devices
./bin/uv -d help                         # List display devices
./bin/uv -c help                         # List compression options
```

## Architecture Overview

UltraGrid is a modular, cross-platform multimedia streaming application with a plugin-based architecture:

### Core Subsystems

1. **Video Pipeline** (`src/video_*`)
   - Capture: Modular input sources (files, capture cards, test patterns)
   - Compression: Various codecs (libavcodec, JPEG, DXT, etc.)
   - Display: Multiple output options (SDL, OpenGL, Decklink, etc.)
   - Processing: Filters and postprocessors

2. **Audio Pipeline** (`src/audio/`)
   - Capture and playback modules
   - Codec support (PCM, Opus, AAC via libavcodec)
   - Filters for processing

3. **Network Transport** (`src/rtp/`)
   - RTP/RTSP implementation
   - Forward Error Correction (FEC)
   - Low-latency packet handling

4. **Hardware Abstraction** (`src/blackmagic_common.cpp`, etc.)
   - Support for professional capture cards
   - Platform-specific implementations

### Module System

Modules are discovered at runtime from:
- Built-in modules compiled into the binary
- External modules loaded as shared libraries
- Modules register themselves using macros like `REGISTER_MODULE`

### Platform Support

- **macOS**: Uses frameworks (CoreAudio, VideoToolbox, AVFoundation)
- **Linux**: Uses ALSA, V4L2, VA-API, VDPAU
- **Windows**: Uses DirectShow, WASAPI

## Code Style

Modified LLVM style with 8-space indentation (similar to Linux kernel):

```
BasedOnStyle: LLVM
IndentWidth: 8
BreakBeforeBraces: Linux
AlwaysBreakAfterReturnType: TopLevelDefinitions
```

Key conventions:
- Module names (`MOD_NAME`) should be lowercase
- Function-like macros should be UPPERCASE
- New files should use 3-clause BSD license

## Testing

Tests are in the `test/` directory and cover:
- Codec conversions
- Network protocols (RTP, UDP)
- Cryptographic functions
- Video capture/display modules

Run individual test:
```bash
./bin/run_tests [test_name]
```

## Dependencies

Major dependencies include:
- FFmpeg (libavcodec, libavformat, libavutil, libswscale)
- SDL2 (for display and UI)
- OpenGL/GLEW (for GPU acceleration)
- Various capture card SDKs (Blackmagic, AJA, etc.)

On macOS with Homebrew:
```bash
brew install automake autoconf libtool pkg-config
```

## Common Development Tasks

### Adding a New Video Capture Module
1. Create `src/video_capture/mymodule.c`
2. Implement the capture API functions
3. Register with `REGISTER_MODULE` macro
4. Add to Makefile.in if needed
5. See `doc/ADDING-MODULES.md` for details

### Adding a New Compression
1. Create `src/video_compress/mycodec.cpp`
2. Implement compress/decompress functions
3. Register the module
4. Handle codec-specific parameters

### Debugging
- Use `log_msg()` for logging (levels: LOG_LEVEL_ERROR, WARNING, NOTICE, INFO, DEBUG, VERBOSE)
- Enable verbose logging: `export UG_VERBOSE=9`
- Module-specific debug: `export UG_VERBOSE_mymodule=9`

## Important Files

- `src/main.cpp` - Entry point and command-line parsing
- `src/video_rxtx/ultragrid_rtp.cpp` - Main video RX/TX loop
- `src/module.c` - Module loading infrastructure
- `configure.ac` - Build configuration
- `test/run_tests.c` - Test runner

## Alpha Blending and Overlay Support

UltraGrid includes alpha blending utilities in `src/utils/alpha_blend.h`:
- **SIMD optimized blending**: AVX2, SSE2, ARM NEON implementations
- **Native format support**: UYVY, RGB blending without conversion
- **Automatic optimization**: Selects best implementation at runtime

### Native Format Processing
Several modules process video directly without RGB conversion:
- `src/capture_filter/grayscale.c` - Direct UYVY manipulation
- `src/capture_filter/matrix2.c` - YCbCr color space transforms
- Avoids quality loss from color space conversions

### Overlay Module (`src/vo_postprocess/overlay.c`)
- Overlays PAM images with alpha transparency
- Dynamic file reloading with nanosecond precision (macOS/Linux/Windows)
- Performance monitoring and SIMD optimization
- Currently converts to RGBA for blending (quality consideration)

## Our Findings
- Updated claude.md with latest repository insights