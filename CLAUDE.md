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

UltraGrid includes comprehensive alpha blending utilities in `src/utils/alpha_blend.h`:
- **SIMD optimized blending**: AVX2, SSE2, ARM NEON implementations
- **Native format support**: Prevents quality loss from color space conversions
- **Smart optimization**: Uses SIMD only when faster than scalar, keeping all implementations

### Supported Native Formats
The alpha blending utilities support direct blending in these formats:
- **RGBA** - 32-bit RGBA (direct alpha channel)
- **RGB** - 24-bit RGB
- **UYVY** - YUV 4:2:2 (U0 Y0 V0 Y1)
- **YUYV** - YUV 4:2:2 (Y0 U0 Y1 V0)
- **v210** - 10-bit YUV 4:2:2 (6 pixels in 16 bytes)
- **R10k** - 10-bit RGB with padding
- **R12L** - 12-bit RGB little-endian (8 pixels in 36 bytes)
- **Y416** - 16-bit YUV with alpha channel
- **I420** - YUV 4:2:0 planar format

### Native Format Processing
Several modules process video directly without RGB conversion:
- `src/capture_filter/grayscale.c` - Direct UYVY manipulation
- `src/capture_filter/matrix2.c` - YCbCr color space transforms
- Avoids quality loss from color space conversions

### Overlay Module (`src/vo_postprocess/overlay.c`)
- Overlays PAM images with alpha transparency
- Dynamic file reloading with nanosecond precision (macOS/Linux/Windows)
- Performance monitoring and SIMD optimization
- Native format blending for all supported formats (no RGB conversion)
- Tracks native vs. converted blending in performance stats

### Testing
Example test program available in `examples/test_alpha_blend.c`:
```bash
cd examples
make test_alpha_blend
./test_alpha_blend
```

### Usage Example
```bash
# Overlay with native UYVY blending (no conversion)
./bin/uv -t testcard:codec=UYVY -p overlay:file=logo.pam:perf -d gl

# Overlay with 10-bit v210 (native blending)
./bin/uv -t testcard:codec=v210 -p overlay:file=logo.pam -d gl
```

## Recent Improvements
- Fixed overlay module bugs (side-by-side duplication, buffer overflows)
- Implemented comprehensive native format alpha blending with SIMD optimizations
- Added cross-platform nanosecond file monitoring
- Created reusable alpha blending utilities with smart dispatch
- Performance: >2000 megapixels/second on modern hardware
- Smart SIMD selection: Uses optimizations only when beneficial (e.g., RGBA 2.3x faster, but RGB uses scalar when SIMD is 0.3x slower)

## Alpha Blending Performance Plan

### Current SIMD Performance Results:
- **Use SIMD (faster)**: RGBA (2.3x), I420 (1.4x), Y416 (1.6x)
- **Use Scalar (SIMD slower)**: UYVY (0.9x), YUYV (0.9x), RGB (0.3x), v210 (0.5x), R10k (0.7x)

### Implementation Strategy:
1. Keep all SIMD implementations available for future hardware/compiler improvements
2. Dispatch functions select best performing implementation at runtime
3. Fall back to scalar when SIMD overhead exceeds computation benefits
4. Comprehensive benchmarking across HD/2K/4K/Cinema 4K/8K resolutions

### Next Steps:
- Modify dispatch functions to use scalar for slower SIMD formats
- Monitor performance across different ARM/x86 architectures
- Consider format-specific optimizations (alignment, memory access patterns)

## Advanced Alpha Blending Optimization Research

### Industry Research Findings

Based on analysis of Blender, FFmpeg, Krita, and other open-source compositors, several advanced optimization techniques have been identified:

#### Memory Bandwidth Bottlenecks (Primary Issue)
Research indicates that **memory bandwidth, not computational power, is often the limiting factor** in SIMD alpha blending operations:

1. **Cache-First Design**: RAM bandwidth bottlenecks prevent AVX improvements on desktop systems (laptops showed better gains)
2. **Cache Blocking**: Processing data in cache-sized blocks provides 1.58x performance improvements
3. **Memory Footprint Reduction**: If working set exceeds cache, performance degrades significantly
4. **Alignment Requirements**: SIMD requires proper memory alignment - padding to boundaries is critical

#### Format-Specific SIMD Challenges

**Why Some Formats Perform Worse with SIMD:**

1. **YUV Formats (UYVY, v210, R10k)**:
   - **Shuffle Overhead**: Require extensive data reorganization before/after calculations
   - **Lane-Crossing Penalties**: AVX/AVX2 lane-crossing shuffles are expensive operations
   - **Subsampling Complexity**: 4:2:2 subsampling complicates vectorization
   - **Unaligned Access**: v210's 10-bit packing (6 pixels in 16 bytes) creates alignment issues

2. **RGB Format**:
   - **3-Component Packing**: RGB lacks 4th component for efficient SIMD packing
   - **Shuffle Operations**: Converting RGB to RGBA requires expensive shuffle instructions
   - **Memory Layout**: Non-power-of-2 stride (3 bytes) hurts cache efficiency

#### Proven Optimization Techniques from Industry

**Blender VSE Optimizations:**
- Focus on `straight_uchar_to_premul_float` + blending + `premul_float_to_straight_uchar` pipeline
- 6.82ms → 4.93ms improvement at 4K (27% faster)
- **Key Insight**: "Memory bandwidth limited, not compute limited"

**FFmpeg SIMD Alpha Blending:**
- Uses `FAST_DIV255` macro: `((x + 128) * 257) >> 16` to avoid division
- Processes 4 pixels simultaneously with SSE2
- 8-16x speedup over scalar with proper implementation
- **Critical**: Eliminates branches in loops, uses streaming instructions

**Krita XSIMD Approach:**
- Batch processing with `xsimd::batch<float, _impl>` (8 values parallel)
- Mask-based conditionals for selective blending
- Row-stride processing methods
- **Performance**: ARM NEON achieves 1.4-3.5x speedup depending on image size

**ARM NEON Compositor:**
- 3.5x faster for small images, 1.4x for large images
- **Key Finding**: Performance scales differently based on working set size
- Memory alignment and data reordering are crucial

#### Advanced Optimization Strategies

**Cache-Aware Processing:**
1. **Tiled Processing**: Process image in cache-friendly blocks
2. **Prefetching**: Use software prefetch for streaming access patterns
3. **Interleaved Layout**: Consider RGBA over RGB for better vectorization

**SIMD-Specific Optimizations:**
1. **Minimize Shuffles**: Keep data in same 128-bit lanes when possible
2. **Fixed Fanout**: Use power-of-2 processing chunks matching SIMD width
3. **Streaming Stores**: Use non-temporal stores for large datasets
4. **Mixed Approaches**: Combine SIMD with scalar for optimal performance

**Format-Specific Solutions:**
1. **YUV Processing**: Consider converting entire lines vs. per-pixel conversion
2. **Alignment Padding**: Pad scanlines to SIMD-friendly boundaries
3. **Format Detection**: Runtime selection of optimized paths per format
4. **Lookup Tables**: Pre-compute expensive operations where possible

#### Implementation Recommendations

**Immediate Improvements:**
1. **Smart Dispatch**: Use scalar for formats where SIMD adds overhead
2. **Cache Blocking**: Implement tile-based processing for large images
3. **Memory Layout**: Ensure proper alignment and consider format-specific optimizations

**Advanced Techniques:**
1. **Multi-Level Optimization**: Different strategies for small vs. large images
2. **Hybrid Processing**: SIMD for computation-heavy parts, scalar for memory-bound parts
3. **Architecture Detection**: Optimize differently for desktop vs. mobile/embedded

**Performance Targets:**
- Target 2-3x improvement for formats currently slower with SIMD
- Focus on memory bandwidth optimization over computational optimization
- Benchmark across working set sizes (HD/2K/4K/8K)

## Optimization Results (Applied)

Based on the research findings, the following optimizations have been implemented:

### 1. Smart Dispatch Implementation
Formats now use their optimal implementation path:
- **RGBA**: Continues using SIMD (2.2x performance)
- **UYVY**: Switched to scalar (was 0.9x with SIMD, now optimal)
- **YUYV**: Switched to scalar (was 0.9x with SIMD, now optimal)
- **RGB**: Switched to scalar (was 0.3x with SIMD, now 1.2x with FAST_DIV255)
- **v210**: Switched to scalar (was 0.5x with SIMD, now optimal)
- **R10k**: Switched to scalar (was 0.7x with SIMD, now optimal)
- **I420**: Continues using SIMD (1.4x performance)
- **Y416**: Continues using SIMD (1.5x performance)

### 2. FFmpeg's FAST_DIV255 Optimization
All scalar implementations now use `((x + 128) * 257) >> 16` instead of division by 255:
- More accurate than simple bit shifting
- Avoids expensive division operations
- Applied to RGBA, UYVY, YUYV, and RGB scalar implementations

### 3. Memory Prefetching
Added prefetch instructions to SSE2 and AVX2 implementations:
```c
_mm_prefetch((const char*)(src + (x + 16) * 4), _MM_HINT_T0);
_mm_prefetch((const char*)(dst + (x + 16) * 4), _MM_HINT_T0);
```

### Performance Summary
- Formats that benefit from SIMD show 1.4x-2.2x speedup
- Formats with complex packing now use optimized scalar implementations
- Memory bandwidth utilization improved through prefetching
- Overall system performance optimized based on actual benchmarks rather than assumptions

#### OpenImageIO SIMD Implementation Insights

**Architecture Design:**
OpenImageIO provides a sophisticated SIMD library specifically designed for high-performance image processing:

1. **Flexible Vector Types**:
   - Multiple vector widths: `vfloat4`, `vfloat8`, `vfloat16` (4, 8, 16 elements)
   - Integer variants: `vint4`, `vint8`, `vint16`
   - Boolean mask types: `vbool4`, `vbool8`, `vbool16`
   - Hardware-specific implementations for SSE, AVX, NEON

2. **Optimized Blend Operations**:
   ```cpp
   // Masked blend between two vectors
   vfloat4 blend(vfloat4 a, vfloat4 b, vbool4 mask)
   
   // Zero-preserving blends
   vfloat4 blend0(vfloat4 a, vbool4 mask)      // mask ? a : 0
   vfloat4 blend0not(vfloat4 a, vbool4 mask)   // mask ? 0 : a
   
   // Symmetric select
   vfloat4 select(vbool4 mask, vfloat4 a, vfloat4 b)  // mask ? a : b
   ```

3. **Alpha Compositing Implementation**:
   - **Fast Path**: Special SIMD path for 4-channel RGBA float images
   - **Porter-Duff Over**: Optimized "A over B" compositing
   - **Key Pattern**:
     ```cpp
     vfloat4 alpha = broadcast_element<3>(a_simd);
     vfloat4 one_minus_alpha = one - clamp(alpha, zero, one);
     vfloat4 result = a_simd + one_minus_alpha * b_simd;
     ```

4. **Performance Techniques**:
   - **Parallel Processing**: Uses `parallel_image` for multi-threading
   - **Type Specialization**: Template-based implementations for different data types
   - **Memory Alignment**: Alignment macros for optimal access
   - **Iterator Bypass**: Avoids iterators in hot paths for maximum performance
   - **In-Place Support**: Handles both in-place and out-of-place operations

5. **High Bit-Depth Support**:
   - Native support for `half`, `float`, and integer types
   - Flexible type system allows 8, 16, 32-bit operations
   - Automatic format conversions with SIMD acceleration

**Key Takeaways for UltraGrid**:
1. **Templated SIMD Design**: Use templates for different vector widths and types
2. **Masked Operations**: Implement boolean mask-based blending for conditional processing
3. **Broadcast Pattern**: Use `broadcast_element` for efficient alpha channel extraction
4. **Parallel Framework**: Consider parallel processing for large images
5. **Type Flexibility**: Support multiple bit depths through templated implementations