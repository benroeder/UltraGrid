/**
 * Performance test for soft edge gradient functions
 * Measures the time taken to apply soft edges to images of various sizes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

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

// Get time in microseconds
static long long get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

static float apply_gradient(float t, enum edge_type type) {
    switch (type) {
    case EDGE_LINEAR:
        return t;
    case EDGE_GAUSSIAN:
        return expf(-powf(1.0f - t, 2) / (2 * 0.3f * 0.3f));
    case EDGE_COSINE:
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
            
            int dist_left = x;
            int dist_right = width - 1 - x;
            int dist_top = y;
            int dist_bottom = height - 1 - y;
            
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
            
            int pixel_offset = (y * width + x) * 4;
            rgba_data[pixel_offset + 3] = (unsigned char)(rgba_data[pixel_offset + 3] * alpha_modifier);
        }
    }
}

static void run_performance_test(const char *test_name, int width, int height, 
                                int edge_width, enum edge_type type, int edge_sides) {
    size_t data_size = (size_t)width * height * 4;
    unsigned char *rgba_data = malloc(data_size);
    if (!rgba_data) {
        printf("Failed to allocate %zu bytes for %dx%d image\n", data_size, width, height);
        return;
    }
    
    // Initialize with dummy data
    memset(rgba_data, 255, data_size);
    
    // Warm up
    apply_soft_edges(rgba_data, width, height, edge_width, type, edge_sides);
    
    // Run multiple iterations
    const int iterations = 100;
    long long start_time = get_time_us();
    
    for (int i = 0; i < iterations; i++) {
        // Reset alpha channel
        for (size_t j = 3; j < data_size; j += 4) {
            rgba_data[j] = 255;
        }
        apply_soft_edges(rgba_data, width, height, edge_width, type, edge_sides);
    }
    
    long long end_time = get_time_us();
    long long total_time_us = end_time - start_time;
    double avg_time_ms = (double)total_time_us / iterations / 1000.0;
    double pixels_per_sec = (double)width * height * iterations / (total_time_us / 1000000.0);
    double megapixels_per_sec = pixels_per_sec / 1000000.0;
    double fps = 1000.0 / avg_time_ms;
    
    printf("%-35s %4dx%-4d  %7.3f ms  %8.2f MP/s  %8.1f fps\n", 
           test_name, width, height, avg_time_ms, megapixels_per_sec, fps);
    
    free(rgba_data);
}

int main() {
    printf("Soft Edge Performance Test\n");
    printf("==========================\n\n");
    
    // Test different resolutions
    struct {
        int width;
        int height;
        const char *name;
    } resolutions[] = {
        {1280, 720, "720p HD"},
        {1920, 1080, "1080p Full HD"},
        {3840, 2160, "4K UHD"},
        {7680, 4320, "8K UHD"},
        {800, 600, "Small overlay"},
        {1920, 300, "Wide banner"},
        {300, 1080, "Tall sidebar"}
    };
    
    // Test parameters
    const int edge_widths[] = {50, 100, 200};
    const char *type_names[] = {"Linear", "Gaussian", "Cosine"};
    const char *side_names[] = {"All sides", "Left/Right", "Top/Bottom", "Single side"};
    int side_masks[] = {EDGE_ALL, EDGE_LEFT | EDGE_RIGHT, EDGE_TOP | EDGE_BOTTOM, EDGE_LEFT};
    
    printf("Test Configuration | Resolution | Time | Throughput | Max FPS\n");
    printf("-------------------|------------|------|------------|----------\n");
    
    // Baseline: No soft edges
    printf("\nBASELINE (No soft edges):\n");
    for (int i = 0; i < 7; i++) {
        run_performance_test("No soft edges", 
                           resolutions[i].width, resolutions[i].height, 
                           0, EDGE_LINEAR, EDGE_ALL);
    }
    
    // Test different edge widths with linear gradient
    printf("\nEDGE WIDTH COMPARISON (Linear, All sides):\n");
    for (int w = 0; w < 3; w++) {
        for (int i = 0; i < 7; i++) {
            char test_name[64];
            snprintf(test_name, sizeof(test_name), "Edge width %dpx", edge_widths[w]);
            run_performance_test(test_name, 
                               resolutions[i].width, resolutions[i].height, 
                               edge_widths[w], EDGE_LINEAR, EDGE_ALL);
        }
        if (w < 2) printf("\n");
    }
    
    // Test different gradient types
    printf("\nGRADIENT TYPE COMPARISON (100px, All sides):\n");
    for (int t = 0; t < 3; t++) {
        for (int i = 0; i < 4; i++) {  // Test main resolutions: 720p, 1080p, 4K, 8K
            char test_name[64];
            snprintf(test_name, sizeof(test_name), "%s gradient", type_names[t]);
            run_performance_test(test_name, 
                               resolutions[i].width, resolutions[i].height, 
                               100, t, EDGE_ALL);
        }
        if (t < 2) printf("\n");
    }
    
    // Test different side combinations
    printf("\nSIDE COMBINATION COMPARISON (100px, Gaussian):\n");
    for (int s = 0; s < 4; s++) {
        for (int i = 0; i < 4; i++) {  // Test main resolutions: 720p, 1080p, 4K, 8K
            run_performance_test(side_names[s], 
                               resolutions[i].width, resolutions[i].height, 
                               100, EDGE_GAUSSIAN, side_masks[s]);
        }
        if (s < 3) printf("\n");
    }
    
    // Memory usage estimate
    printf("\nMEMORY USAGE:\n");
    printf("Resolution    | Image Size  | With Soft Edges\n");
    printf("--------------|-------------|----------------\n");
    for (int i = 0; i < 7; i++) {
        size_t base_size = (size_t)resolutions[i].width * resolutions[i].height * 4;
        printf("%-13s | %7.2f MB  | No additional memory needed\n", 
               resolutions[i].name, base_size / (1024.0 * 1024.0));
    }
    
    printf("\nNOTE: Soft edges are applied in-place during loading, so there's no\n");
    printf("      additional memory overhead or runtime performance impact during\n");
    printf("      video processing.\n");
    
    return 0;
}