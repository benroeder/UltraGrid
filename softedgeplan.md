# Soft Edge Implementation Plan for UltraGrid Overlay Module

## Overview
This document outlines the plan for adding soft edge (feathering) support to the UltraGrid overlay module. Soft edges create a gradual transparency transition at the borders of overlay images, providing a more professional blending effect.

**Important**: The implementation must maintain backward compatibility - overlays without soft edges should continue to work exactly as they do currently.

## 1. Alpha Gradient Generation
Create a feathering effect by modifying the alpha channel at the edges of the overlay:
- **Linear gradient**: Alpha decreases linearly from 255 to 0 over a specified distance
- **Gaussian gradient**: Smoother falloff using a Gaussian curve
- **Custom curves**: Support for different falloff profiles (cosine, exponential, etc.)

## 2. Implementation Approach

### Option A: Pre-process the overlay image (Recommended)
Modify the alpha channel after loading the PAM file:
```c
// After loading overlay in load_overlay_image()
apply_soft_edges(s->overlay_data, s->overlay_width, s->overlay_height, edge_width);
```

**Advantages:**
- Minimal performance impact (only done once per image load)
- Works with all existing alpha blending code
- Can be cached along with scaled overlays
- No changes needed to blending functions

### Option B: Apply during blending
Modify alpha values on-the-fly during the blending operation:
```c
// Calculate edge alpha modifier based on pixel position
float edge_alpha = calculate_edge_alpha(x, y, width, height, edge_width);
uint8_t modified_alpha = (uint8_t)(original_alpha * edge_alpha);
```

**Disadvantages:**
- Performance overhead on every frame
- Requires modifying all blending functions
- More complex implementation

## 3. Configuration Parameters
Add new parameters to the overlay module:
- `edge=<pixels>` - Width of the soft edge in pixels (default: 0 = disabled)
- `edge_type=<linear|gaussian|cosine>` - Type of edge falloff (default: linear)
- `edge_sides=<all|top|bottom|left|right>` - Which edges to soften (default: all)
  - Multiple sides can be specified: `edge_sides=top,bottom`

Example usage:
```bash
# With soft edges (new feature)
uv -t testcard -p overlay:file=logo.pam:edge=50:edge_type=gaussian -d gl

# Without soft edges (current behavior - must continue to work)
uv -t testcard -p overlay:file=logo.pam -d gl
```

**Backward Compatibility**: When `edge` parameter is not specified or is 0, the overlay module must function exactly as it does currently, with no soft edge processing.

## 4. Algorithm Implementation

### Basic Edge Alpha Calculation
```c
float calculate_edge_alpha(int x, int y, int width, int height, int edge_width) {
    float alpha = 1.0f;
    
    // Distance from edges
    int dist_left = x;
    int dist_right = width - 1 - x;
    int dist_top = y;
    int dist_bottom = height - 1 - y;
    
    // Find minimum distance to any edge
    int min_dist = MIN(MIN(dist_left, dist_right), MIN(dist_top, dist_bottom));
    
    // Apply gradient if within edge zone
    if (min_dist < edge_width) {
        alpha = (float)min_dist / edge_width;
    }
    
    return alpha;
}
```

### Gradient Type Implementations
```c
// Linear gradient (default)
float linear_gradient(float t) {
    return t;
}

// Gaussian gradient
float gaussian_gradient(float t) {
    // Gaussian curve with adjustable steepness
    float sigma = 0.3f;
    return expf(-powf(1.0f - t, 2) / (2 * sigma * sigma));
}

// Cosine gradient (smooth S-curve)
float cosine_gradient(float t) {
    return 0.5f * (1.0f + cosf(M_PI * (1.0f - t)));
}
```

### Full Soft Edge Application Function
```c
void apply_soft_edges(unsigned char *rgba_data, int width, int height, 
                      int edge_width, enum edge_type type, int edge_sides) {
    // No processing if edge width is 0 or negative (maintains current behavior)
    if (edge_width <= 0) return;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float alpha_modifier = 1.0f;
            
            // Calculate distances from edges
            int dist_left = x;
            int dist_right = width - 1 - x;
            int dist_top = y;
            int dist_bottom = height - 1 - y;
            
            // Apply edge gradients based on selected sides
            if (edge_sides & EDGE_LEFT && dist_left < edge_width) {
                float t = (float)dist_left / edge_width;
                alpha_modifier = MIN(alpha_modifier, apply_gradient(t, type));
            }
            if (edge_sides & EDGE_RIGHT && dist_right < edge_width) {
                float t = (float)dist_right / edge_width;
                alpha_modifier = MIN(alpha_modifier, apply_gradient(t, type));
            }
            if (edge_sides & EDGE_TOP && dist_top < edge_width) {
                float t = (float)dist_top / edge_width;
                alpha_modifier = MIN(alpha_modifier, apply_gradient(t, type));
            }
            if (edge_sides & EDGE_BOTTOM && dist_bottom < edge_width) {
                float t = (float)dist_bottom / edge_width;
                alpha_modifier = MIN(alpha_modifier, apply_gradient(t, type));
            }
            
            // Apply alpha modification
            int pixel_offset = (y * width + x) * 4;
            rgba_data[pixel_offset + 3] = (unsigned char)(rgba_data[pixel_offset + 3] * alpha_modifier);
        }
    }
}
```

## 5. Integration Points

### State Structure Updates
```c
struct state_overlay {
    // ... existing fields ...
    
    // Soft edge settings (default values maintain current behavior)
    int edge_width;           // Width of soft edge in pixels (default: 0)
    enum edge_type edge_type; // Type of gradient (default: EDGE_LINEAR)
    int edge_sides;          // Bitmask of which edges to soften (default: EDGE_ALL)
};
```

### Configuration Parsing
Add parsing for new parameters in `overlay_init()`:
```c
} else if (strncasecmp(item, "edge=", 5) == 0) {
    s->edge_width = atoi(item + 5);
} else if (strncasecmp(item, "edge_type=", 10) == 0) {
    const char *type = item + 10;
    if (strcasecmp(type, "linear") == 0) {
        s->edge_type = EDGE_LINEAR;
    } else if (strcasecmp(type, "gaussian") == 0) {
        s->edge_type = EDGE_GAUSSIAN;
    } else if (strcasecmp(type, "cosine") == 0) {
        s->edge_type = EDGE_COSINE;
    }
} else if (strncasecmp(item, "edge_sides=", 11) == 0) {
    // Parse comma-separated list of sides
}
```

### Apply During Load
In `load_overlay_image()`, after converting to RGBA:
```c
// Apply soft edges if configured
// Only processes if edge_width > 0, maintaining current behavior otherwise
if (s->edge_width > 0) {
    apply_soft_edges(s->overlay_data, s->overlay_width, s->overlay_height,
                     s->edge_width, s->edge_type, s->edge_sides);
    log_msg(LOG_LEVEL_INFO, MOD_NAME "Applied soft edges: width=%d, type=%s\n", 
            s->edge_width, edge_type_to_string(s->edge_type));
}
// No soft edge processing when edge_width is 0 (default)
```

## 6. Performance Considerations
- Soft edge processing only happens when overlay is loaded/reloaded
- No runtime performance impact during frame processing
- Edge processing is cache-friendly (sequential memory access)
- For typical overlay sizes (e.g., 1920x1080), processing time is negligible
- **Zero performance impact when soft edges are not used** (default behavior)

## 7. Testing Plan
1. **Backward compatibility testing**:
   - Verify overlays without edge parameters work exactly as before
   - Test existing overlay configurations continue to function unchanged
   - Ensure no performance regression for non-soft-edge overlays
2. Test with various edge widths (1-200 pixels)
3. Compare visual quality of different gradient types
4. Verify correct behavior with partial edge selection
5. Test with overlays at different positions
6. Ensure soft edges work correctly with scaled overlays
7. Performance testing with large overlays (4K, 8K)
8. Test edge=0 explicitly to ensure it behaves the same as no edge parameter

## 8. Future Enhancements
- Per-edge width control (e.g., `edge_top=50:edge_bottom=20`)
- Custom gradient curves via lookup tables
- Animated fade-in/fade-out effects
- Mask-based soft edges (using a separate alpha mask file)