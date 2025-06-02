/**
 * Test program for soft edge gradient functions
 * Generates test images showing different soft edge effects
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))

enum edge_type {
    EDGE_LINEAR,
    EDGE_GAUSSIAN,
    EDGE_COSINE
};

enum edge_side {
    EDGE_LEFT   = 1 << 0,
    EDGE_RIGHT  = 1 << 1,
    EDGE_TOP    = 1 << 2,
    EDGE_BOTTOM = 1 << 3,
    EDGE_ALL    = EDGE_LEFT | EDGE_RIGHT | EDGE_TOP | EDGE_BOTTOM
};

static float apply_gradient(float t, enum edge_type type) {
    switch (type) {
    case EDGE_LINEAR:
        return t;
    case EDGE_GAUSSIAN:
        // Gaussian curve with adjustable steepness
        return expf(-powf(1.0f - t, 2) / (2 * 0.3f * 0.3f));
    case EDGE_COSINE:
        // Smooth S-curve
        return 0.5f * (1.0f + cosf(M_PI * (1.0f - t)));
    default:
        return t;
    }
}

static void apply_soft_edges(unsigned char *rgba_data, int width, int height,
                            int edge_width, enum edge_type type, int edge_sides) {
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

static void save_pam(const char *filename, unsigned char *rgba_data, int width, int height) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("fopen");
        return;
    }
    
    fprintf(f, "P7\n");
    fprintf(f, "WIDTH %d\n", width);
    fprintf(f, "HEIGHT %d\n", height);
    fprintf(f, "DEPTH 4\n");
    fprintf(f, "MAXVAL 255\n");
    fprintf(f, "TUPLTYPE RGB_ALPHA\n");
    fprintf(f, "ENDHDR\n");
    
    fwrite(rgba_data, 4, width * height, f);
    fclose(f);
}

static void create_test_image(unsigned char *rgba_data, int width, int height) {
    // Create a gradient test pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 4;
            rgba_data[idx + 0] = (x * 255) / width;     // R: horizontal gradient
            rgba_data[idx + 1] = (y * 255) / height;    // G: vertical gradient
            rgba_data[idx + 2] = 128;                   // B: constant
            rgba_data[idx + 3] = 255;                   // A: fully opaque
        }
    }
}

int main() {
    const int width = 800;
    const int height = 600;
    const int edge_widths[] = {0, 50, 100, 150};
    const char *type_names[] = {"linear", "gaussian", "cosine"};
    
    unsigned char *rgba_data = malloc(width * height * 4);
    if (!rgba_data) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }
    
    // Test 1: No soft edges (baseline)
    printf("Creating baseline image without soft edges...\n");
    create_test_image(rgba_data, width, height);
    save_pam("soft_edge_none.pam", rgba_data, width, height);
    
    // Test 2: Different edge widths with linear gradient
    for (int i = 1; i < 4; i++) {
        printf("Creating linear soft edge with width %d...\n", edge_widths[i]);
        create_test_image(rgba_data, width, height);
        apply_soft_edges(rgba_data, width, height, edge_widths[i], EDGE_LINEAR, EDGE_ALL);
        char filename[64];
        snprintf(filename, sizeof(filename), "soft_edge_linear_%d.pam", edge_widths[i]);
        save_pam(filename, rgba_data, width, height);
    }
    
    // Test 3: Different gradient types with 100px edge
    for (int i = 0; i < 3; i++) {
        printf("Creating %s soft edge with width 100...\n", type_names[i]);
        create_test_image(rgba_data, width, height);
        apply_soft_edges(rgba_data, width, height, 100, i, EDGE_ALL);
        char filename[64];
        snprintf(filename, sizeof(filename), "soft_edge_%s_100.pam", type_names[i]);
        save_pam(filename, rgba_data, width, height);
    }
    
    // Test 4: Different edge combinations
    printf("Creating soft edge on left and right only...\n");
    create_test_image(rgba_data, width, height);
    apply_soft_edges(rgba_data, width, height, 100, EDGE_GAUSSIAN, EDGE_LEFT | EDGE_RIGHT);
    save_pam("soft_edge_leftright.pam", rgba_data, width, height);
    
    printf("Creating soft edge on top and bottom only...\n");
    create_test_image(rgba_data, width, height);
    apply_soft_edges(rgba_data, width, height, 100, EDGE_GAUSSIAN, EDGE_TOP | EDGE_BOTTOM);
    save_pam("soft_edge_topbottom.pam", rgba_data, width, height);
    
    free(rgba_data);
    printf("\nTest images created successfully!\n");
    printf("View them with: display *.pam (ImageMagick) or any PAM-compatible viewer\n");
    
    return 0;
}