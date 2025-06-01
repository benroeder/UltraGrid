/**
 * @file   examples/test_alpha_blend.c
 * @brief  Test program demonstrating alpha blending utilities
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/utils/alpha_blend.h"

static void print_rgba_pixels(const char *label, uint8_t *data, int count)
{
    printf("%s: ", label);
    for (int i = 0; i < count; i++) {
        printf("(%3d,%3d,%3d,%3d) ", data[i*4], data[i*4+1], data[i*4+2], data[i*4+3]);
    }
    printf("\n");
}

static void test_rgba_blending()
{
    printf("\n=== Testing RGBA Blending ===\n");
    printf("Implementation: %s\n", alpha_blend_get_implementation());
    
    // Test data: 4 pixels
    uint8_t dst[16] = {
        255, 0, 0, 255,    // Red
        0, 255, 0, 255,    // Green
        0, 0, 255, 255,    // Blue
        255, 255, 255, 255 // White
    };
    
    uint8_t src[16] = {
        0, 0, 0, 128,      // Black with 50% alpha
        255, 255, 255, 64, // White with 25% alpha
        255, 0, 255, 192,  // Magenta with 75% alpha
        0, 255, 255, 255   // Cyan with 100% alpha
    };
    
    print_rgba_pixels("Before blend - dst", dst, 4);
    print_rgba_pixels("Before blend - src", src, 4);
    
    alpha_blend_rgba(dst, src, 4);
    
    print_rgba_pixels("After blend  - dst", dst, 4);
}

static void test_uyvy_blending()
{
    printf("\n=== Testing UYVY Blending ===\n");
    
    // UYVY format: U0 Y0 V0 Y1 (covers 2 pixels)
    uint8_t dst[8] = {
        128, 235, 128, 235  // Two white pixels in YUV
    };
    
    uint8_t src[8] = {
        128, 16, 128, 16    // Two black pixels in YUV
    };
    
    uint8_t alpha[4] = { 128, 128, 255, 0 }; // 50%, 50%, 100%, 0%
    
    printf("Before blend - dst: U=%d Y0=%d V=%d Y1=%d\n", dst[0], dst[1], dst[2], dst[3]);
    printf("Before blend - src: U=%d Y0=%d V=%d Y1=%d\n", src[0], src[1], src[2], src[3]);
    printf("Alpha values: %d, %d\n", alpha[0], alpha[1]);
    
    alpha_blend_uyvy(dst, src, alpha, 2);
    
    printf("After blend  - dst: U=%d Y0=%d V=%d Y1=%d\n", dst[0], dst[1], dst[2], dst[3]);
}

static void benchmark_rgba_blending()
{
    printf("\n=== Benchmarking RGBA Blending ===\n");
    
    const int width = 1920;
    const int height = 1080;
    const int pixels = width * height;
    const int iterations = 100;
    
    uint8_t *dst = malloc(pixels * 4);
    uint8_t *src = malloc(pixels * 4);
    
    if (!dst || !src) {
        printf("Failed to allocate memory for benchmark\n");
        free(dst);
        free(src);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < pixels * 4; i++) {
        dst[i] = rand() & 0xFF;
        src[i] = rand() & 0xFF;
    }
    
    clock_t start = clock();
    
    for (int i = 0; i < iterations; i++) {
        alpha_blend_rgba(dst, src, pixels);
    }
    
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed);
    printf("Average: %.3f ms per frame\n", elapsed * 1000.0 / iterations);
    printf("Throughput: %.1f megapixels/second\n", (pixels * iterations) / (elapsed * 1000000.0));
    
    free(dst);
    free(src);
}

static void test_uyvy_optimized()
{
    printf("\n=== Testing UYVY Optimized Blending ===\n");
    printf("Implementation: %s\n", alpha_blend_get_implementation());
    
    // Test with different widths to verify scalar fallback
    int test_widths[] = {8, 10, 16, 20, 1920};
    
    for (int w = 0; w < 5; w++) {
        int width = test_widths[w];
        int uyvy_size = width * 2; // 2 bytes per pixel
        
        uint8_t *dst = malloc(uyvy_size);
        uint8_t *src = malloc(uyvy_size);
        uint8_t *alpha = malloc(width);
        
        if (!dst || !src || !alpha) {
            printf("Memory allocation failed\n");
            free(dst);
            free(src);
            free(alpha);
            continue;
        }
        
        // Initialize test data
        for (int i = 0; i < uyvy_size; i++) {
            dst[i] = (i % 2 == 0) ? 128 : 235; // U/V=128, Y=235 (white in YUV)
            src[i] = (i % 2 == 0) ? 128 : 16;  // U/V=128, Y=16 (black in YUV)
        }
        for (int i = 0; i < width; i++) {
            alpha[i] = 128; // 50% blend
        }
        
        // Perform blending
        alpha_blend_uyvy(dst, src, alpha, width);
        
        // Check first few pixels
        printf("Width %d: ", width);
        if (width >= 2) {
            printf("U=%d Y0=%d V=%d Y1=%d ", dst[0], dst[1], dst[2], dst[3]);
            // Y values should be around 125 (50% blend of 235 and 16)
            int expected_y = (235 * 128 + 16 * 127) / 255;
            int diff = abs(dst[1] - expected_y);
            printf("(Y expected ~%d, got %d, diff=%d) ", expected_y, dst[1], diff);
            if (diff <= 2) {
                printf("✓");
            } else {
                printf("✗");
            }
        }
        printf("\n");
        
        free(dst);
        free(src);
        free(alpha);
    }
}

static void benchmark_uyvy_blending()
{
    printf("\n=== Benchmarking UYVY Blending ===\n");
    
    const int width = 1920;
    const int height = 1080;
    const int iterations = 100;
    const int uyvy_size = width * height * 2; // 2 bytes per pixel
    
    uint8_t *dst = malloc(uyvy_size);
    uint8_t *src = malloc(uyvy_size);
    uint8_t *alpha = malloc(width * height);
    
    if (!dst || !src || !alpha) {
        printf("Failed to allocate memory for benchmark\n");
        free(dst);
        free(src);
        free(alpha);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < uyvy_size; i++) {
        dst[i] = rand() & 0xFF;
        src[i] = rand() & 0xFF;
    }
    for (int i = 0; i < width * height; i++) {
        alpha[i] = rand() & 0xFF;
    }
    
    clock_t start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_uyvy(dst + y * width * 2, src + y * width * 2, alpha + y * width, width);
        }
    }
    
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed);
    printf("Average: %.3f ms per frame\n", elapsed * 1000.0 / iterations);
    printf("Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed * 1000000.0));
    
    free(dst);
    free(src);
    free(alpha);
}

int main()
{
    printf("Alpha Blending Test Program\n");
    printf("===========================\n");
    
    test_rgba_blending();
    test_uyvy_blending();
    test_uyvy_optimized();
    
    benchmark_rgba_blending();
    benchmark_uyvy_blending();
    
    return 0;
}