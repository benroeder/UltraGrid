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

// Forward declaration of scalar functions for benchmarking
static void alpha_blend_rgba_scalar(uint8_t *dst, const uint8_t *src, int width);
static void alpha_blend_uyvy_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);
static void alpha_blend_yuyv_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);
static void alpha_blend_rgb_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);
static void alpha_blend_v210_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);
static void alpha_blend_r10k_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);
static void alpha_blend_i420_scalar(uint8_t *dst_y, uint8_t *dst_u, uint8_t *dst_v,
                                    const uint8_t *src_y, const uint8_t *src_u, const uint8_t *src_v,
                                    const uint8_t *alpha, int width, int height);
static void alpha_blend_y416_scalar(uint8_t *dst, const uint8_t *src, int width);

// Helper function to print frame rates for different resolutions
static void print_resolution_frame_rates(double fps)
{
    // Calculate frame rates for different resolutions based on 1920x1080 reference
    double hd_pixels = 1920.0 * 1080.0;           // HD (1920x1080)
    double uhd_2k_pixels = 2560.0 * 1440.0;       // 2K UHD (2560x1440)
    double uhd_4k_pixels = 3840.0 * 2160.0;       // UHD 4K (3840x2160)
    double cinema_4k_pixels = 4096.0 * 2160.0;    // Cinematic 4K (4096x2160)
    double uhd_8k_pixels = 7680.0 * 4320.0;       // 8K UHD (7680x4320)
    
    double fps_2k = fps * (hd_pixels / uhd_2k_pixels);
    double fps_uhd4k = fps * (hd_pixels / uhd_4k_pixels);
    double fps_cinema4k = fps * (hd_pixels / cinema_4k_pixels);
    double fps_8k = fps * (hd_pixels / uhd_8k_pixels);
    
    printf("  Frame rate: %.1f HD fps, %.1f 2K fps, %.1f UHD 4K fps, %.1f Cinema 4K fps, %.1f 8K fps\n", 
           fps, fps_2k, fps_uhd4k, fps_cinema4k, fps_8k);
}

// Scalar implementations for testing
static void alpha_blend_rgba_scalar(uint8_t *dst, const uint8_t *src, int width)
{
    for (int x = 0; x < width; x++) {
        uint8_t r = src[0];
        uint8_t g = src[1];
        uint8_t b = src[2];
        uint8_t a = src[3];
        
        dst[0] = (r * a + dst[0] * (255 - a)) / 255;
        dst[1] = (g * a + dst[1] * (255 - a)) / 255;
        dst[2] = (b * a + dst[2] * (255 - a)) / 255;
        dst[3] = 255;
        
        src += 4;
        dst += 4;
    }
}

static void alpha_blend_uyvy_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
    for (int x = 0; x < width; x += 2) {
        uint8_t u_src = src[0];
        uint8_t y0_src = src[1];
        uint8_t v_src = src[2];
        uint8_t y1_src = src[3];
        
        uint8_t u_dst = dst[0];
        uint8_t y0_dst = dst[1];
        uint8_t v_dst = dst[2];
        uint8_t y1_dst = dst[3];
        
        uint8_t a0 = alpha[x];
        uint8_t a1 = (x + 1 < width) ? alpha[x + 1] : a0;
        uint8_t avg_alpha = (a0 + a1) / 2;
        
        dst[0] = (u_src * avg_alpha + u_dst * (255 - avg_alpha)) / 255;
        dst[1] = (y0_src * a0 + y0_dst * (255 - a0)) / 255;
        dst[2] = (v_src * avg_alpha + v_dst * (255 - avg_alpha)) / 255;
        dst[3] = (y1_src * a1 + y1_dst * (255 - a1)) / 255;
        
        src += 4;
        dst += 4;
    }
}

static void alpha_blend_yuyv_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
    for (int x = 0; x < width; x += 2) {
        uint8_t y0_src = src[0];
        uint8_t u_src = src[1];
        uint8_t y1_src = src[2];
        uint8_t v_src = src[3];
        
        uint8_t y0_dst = dst[0];
        uint8_t u_dst = dst[1];
        uint8_t y1_dst = dst[2];
        uint8_t v_dst = dst[3];
        
        uint8_t a0 = alpha[x];
        uint8_t a1 = (x + 1 < width) ? alpha[x + 1] : a0;
        uint8_t avg_alpha = (a0 + a1) / 2;
        
        dst[0] = (y0_src * a0 + y0_dst * (255 - a0)) / 255;
        dst[1] = (u_src * avg_alpha + u_dst * (255 - avg_alpha)) / 255;
        dst[2] = (y1_src * a1 + y1_dst * (255 - a1)) / 255;
        dst[3] = (v_src * avg_alpha + v_dst * (255 - avg_alpha)) / 255;
        
        src += 4;
        dst += 4;
    }
}

static void alpha_blend_rgb_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
    for (int x = 0; x < width; x++) {
        uint8_t a = alpha[x];
        
        dst[0] = (src[0] * a + dst[0] * (255 - a)) / 255;
        dst[1] = (src[1] * a + dst[1] * (255 - a)) / 255;
        dst[2] = (src[2] * a + dst[2] * (255 - a)) / 255;
        
        dst += 3;
        src += 3;
    }
}

static void alpha_blend_v210_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
    // Process in groups of 6 pixels (16 bytes)
    for (int x = 0; x < width; x += 6) {
        uint32_t *dst_words = (uint32_t *)dst;
        const uint32_t *src_words = (const uint32_t *)src;
        
        // Extract 10-bit values from packed format
        uint32_t word0_dst = dst_words[0];
        uint32_t word0_src = src_words[0];
        
        uint16_t cb0_dst = (word0_dst >> 0) & 0x3FF;
        uint16_t y0_dst = (word0_dst >> 10) & 0x3FF;
        uint16_t cr0_dst = (word0_dst >> 20) & 0x3FF;
        
        uint16_t cb0_src = (word0_src >> 0) & 0x3FF;
        uint16_t y0_src = (word0_src >> 10) & 0x3FF;
        uint16_t cr0_src = (word0_src >> 20) & 0x3FF;
        
        uint8_t a0 = alpha[x];
        uint32_t temp = y0_src * a0 + y0_dst * (255 - a0);
        y0_dst = (temp + (temp >> 8)) >> 8;
        
        uint32_t word1_dst = dst_words[1];
        uint32_t word1_src = src_words[1];
        
        uint16_t y1_dst = (word1_dst >> 0) & 0x3FF;
        uint16_t cb2_dst = (word1_dst >> 10) & 0x3FF;
        uint16_t y2_dst = (word1_dst >> 20) & 0x3FF;
        
        uint16_t y1_src = (word1_src >> 0) & 0x3FF;
        uint16_t cb2_src = (word1_src >> 10) & 0x3FF;
        uint16_t y2_src = (word1_src >> 20) & 0x3FF;
        
        uint8_t a1 = (x + 1 < width) ? alpha[x + 1] : a0;
        uint8_t a2 = (x + 2 < width) ? alpha[x + 2] : a1;
        temp = y1_src * a1 + y1_dst * (255 - a1);
        y1_dst = (temp + (temp >> 8)) >> 8;
        temp = y2_src * a2 + y2_dst * (255 - a2);
        y2_dst = (temp + (temp >> 8)) >> 8;
        
        uint32_t word2_dst = dst_words[2];
        uint32_t word2_src = src_words[2];
        
        uint16_t cr2_dst = (word2_dst >> 0) & 0x3FF;
        uint16_t y3_dst = (word2_dst >> 10) & 0x3FF;
        uint16_t cb4_dst = (word2_dst >> 20) & 0x3FF;
        
        uint16_t cr2_src = (word2_src >> 0) & 0x3FF;
        uint16_t y3_src = (word2_src >> 10) & 0x3FF;
        uint16_t cb4_src = (word2_src >> 20) & 0x3FF;
        
        uint8_t a3 = (x + 3 < width) ? alpha[x + 3] : a2;
        temp = y3_src * a3 + y3_dst * (255 - a3);
        y3_dst = (temp + (temp >> 8)) >> 8;
        
        uint32_t word3_dst = dst_words[3];
        uint32_t word3_src = src_words[3];
        
        uint16_t y4_dst = (word3_dst >> 0) & 0x3FF;
        uint16_t cr4_dst = (word3_dst >> 10) & 0x3FF;
        uint16_t y5_dst = (word3_dst >> 20) & 0x3FF;
        
        uint16_t y4_src = (word3_src >> 0) & 0x3FF;
        uint16_t cr4_src = (word3_src >> 10) & 0x3FF;
        uint16_t y5_src = (word3_src >> 20) & 0x3FF;
        
        uint8_t a4 = (x + 4 < width) ? alpha[x + 4] : a3;
        uint8_t a5 = (x + 5 < width) ? alpha[x + 5] : a4;
        temp = y4_src * a4 + y4_dst * (255 - a4);
        y4_dst = (temp + (temp >> 8)) >> 8;
        temp = y5_src * a5 + y5_dst * (255 - a5);
        y5_dst = (temp + (temp >> 8)) >> 8;
        
        // Blend chroma using average alpha for pixel pairs
        uint8_t avg_alpha_01 = (a0 + a1) / 2;
        uint8_t avg_alpha_23 = (a2 + a3) / 2;
        uint8_t avg_alpha_45 = (a4 + a5) / 2;
        
        uint32_t temp32;
        temp32 = cb0_src * avg_alpha_01 + cb0_dst * (255 - avg_alpha_01);
        cb0_dst = (temp32 + (temp32 >> 8)) >> 8;
        temp32 = cr0_src * avg_alpha_01 + cr0_dst * (255 - avg_alpha_01);
        cr0_dst = (temp32 + (temp32 >> 8)) >> 8;
        temp32 = cb2_src * avg_alpha_23 + cb2_dst * (255 - avg_alpha_23);
        cb2_dst = (temp32 + (temp32 >> 8)) >> 8;
        temp32 = cr2_src * avg_alpha_23 + cr2_dst * (255 - avg_alpha_23);
        cr2_dst = (temp32 + (temp32 >> 8)) >> 8;
        temp32 = cb4_src * avg_alpha_45 + cb4_dst * (255 - avg_alpha_45);
        cb4_dst = (temp32 + (temp32 >> 8)) >> 8;
        temp32 = cr4_src * avg_alpha_45 + cr4_dst * (255 - avg_alpha_45);
        cr4_dst = (temp32 + (temp32 >> 8)) >> 8;
        
        // Pack back into v210 format
        dst_words[0] = (cb0_dst & 0x3FF) | ((y0_dst & 0x3FF) << 10) | ((cr0_dst & 0x3FF) << 20);
        dst_words[1] = (y1_dst & 0x3FF) | ((cb2_dst & 0x3FF) << 10) | ((y2_dst & 0x3FF) << 20);
        dst_words[2] = (cr2_dst & 0x3FF) | ((y3_dst & 0x3FF) << 10) | ((cb4_dst & 0x3FF) << 20);
        dst_words[3] = (y4_dst & 0x3FF) | ((cr4_dst & 0x3FF) << 10) | ((y5_dst & 0x3FF) << 20);
        
        dst += 16;
        src += 16;
    }
}

static void alpha_blend_r10k_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
    for (int x = 0; x < width; x++) {
        uint32_t dst_pixel = *(uint32_t *)dst;
        uint32_t src_pixel = *(uint32_t *)src;
        uint8_t a = alpha[x];
        
        // Extract 10-bit components
        uint16_t r_dst = (dst_pixel >> 20) & 0x3FF;
        uint16_t g_dst = (dst_pixel >> 10) & 0x3FF;
        uint16_t b_dst = (dst_pixel >> 0) & 0x3FF;
        
        uint16_t r_src = (src_pixel >> 20) & 0x3FF;
        uint16_t g_src = (src_pixel >> 10) & 0x3FF;
        uint16_t b_src = (src_pixel >> 0) & 0x3FF;
        
        // Blend with 10-bit precision using proper division
        uint32_t temp;
        temp = r_src * a + r_dst * (255 - a);
        r_dst = (temp + (temp >> 8)) >> 8;
        temp = g_src * a + g_dst * (255 - a);
        g_dst = (temp + (temp >> 8)) >> 8;
        temp = b_src * a + b_dst * (255 - a);
        b_dst = (temp + (temp >> 8)) >> 8;
        
        // Pack back with padding in bits 30-31
        *(uint32_t *)dst = ((r_dst & 0x3FF) << 20) | 
                           ((g_dst & 0x3FF) << 10) | 
                           (b_dst & 0x3FF);
        
        dst += 4;
        src += 4;
    }
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
    uint8_t *dst_copy = malloc(pixels * 4);
    
    if (!dst || !src || !dst_copy) {
        printf("Failed to allocate memory for benchmark\n");
        free(dst);
        free(src);
        free(dst_copy);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < pixels * 4; i++) {
        dst[i] = rand() & 0xFF;
        src[i] = rand() & 0xFF;
        dst_copy[i] = dst[i];
    }
    
    // Test scalar implementation
    printf("Scalar:\n");
    clock_t start = clock();
    
    for (int i = 0; i < iterations; i++) {
        alpha_blend_rgba_scalar(dst_copy, src, pixels);
    }
    
    clock_t end = clock();
    double elapsed_scalar = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_scalar = 1.0 / (elapsed_scalar / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_scalar);
    printf("  Average: %.3f ms per frame\n", elapsed_scalar * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (pixels * iterations) / (elapsed_scalar * 1000000.0));
    print_resolution_frame_rates(fps_scalar);
    
    // Reset data for optimized test
    for (int i = 0; i < pixels * 4; i++) {
        dst[i] = dst_copy[i];
    }
    
    // Test optimized implementation
    printf("Optimized (%s):\n", alpha_blend_get_implementation());
    start = clock();
    
    for (int i = 0; i < iterations; i++) {
        alpha_blend_rgba(dst, src, pixels);
    }
    
    end = clock();
    double elapsed_opt = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_opt = 1.0 / (elapsed_opt / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_opt);
    printf("  Average: %.3f ms per frame\n", elapsed_opt * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (pixels * iterations) / (elapsed_opt * 1000000.0));
    print_resolution_frame_rates(fps_opt);
    printf("  Speedup: %.1fx\n", elapsed_scalar / elapsed_opt);
    
    free(dst);
    free(src);
    free(dst_copy);
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
    uint8_t *dst_copy = malloc(uyvy_size);
    uint8_t *alpha = malloc(width * height);
    
    if (!dst || !src || !dst_copy || !alpha) {
        printf("Failed to allocate memory for benchmark\n");
        free(dst);
        free(src);
        free(dst_copy);
        free(alpha);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < uyvy_size; i++) {
        dst[i] = rand() & 0xFF;
        src[i] = rand() & 0xFF;
        dst_copy[i] = dst[i];
    }
    for (int i = 0; i < width * height; i++) {
        alpha[i] = rand() & 0xFF;
    }
    
    // Test scalar implementation
    printf("Scalar:\n");
    clock_t start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_uyvy_scalar(dst_copy + y * width * 2, src + y * width * 2, alpha + y * width, width);
        }
    }
    
    clock_t end = clock();
    double elapsed_scalar = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_scalar = 1.0 / (elapsed_scalar / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_scalar);
    printf("  Average: %.3f ms per frame\n", elapsed_scalar * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_scalar * 1000000.0));
    print_resolution_frame_rates(fps_scalar);
    
    // Reset data for optimized test
    for (int i = 0; i < uyvy_size; i++) {
        dst[i] = dst_copy[i];
    }
    
    // Test optimized implementation
    printf("Optimized (%s):\n", alpha_blend_get_implementation());
    start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_uyvy(dst + y * width * 2, src + y * width * 2, alpha + y * width, width);
        }
    }
    
    end = clock();
    double elapsed_opt = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_opt = 1.0 / (elapsed_opt / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_opt);
    printf("  Average: %.3f ms per frame\n", elapsed_opt * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_opt * 1000000.0));
    print_resolution_frame_rates(fps_opt);
    printf("  Speedup: %.1fx\n", elapsed_scalar / elapsed_opt);
    
    free(dst);
    free(src);
    free(dst_copy);
    free(alpha);
}

static void test_yuyv_blending()
{
    printf("\n=== Testing YUYV Blending ===\n");
    
    // YUYV format: Y0 U0 Y1 V0 (covers 2 pixels)
    uint8_t dst[4] = {235, 128, 235, 128}; // Bright luma, neutral chroma
    uint8_t src[4] = {16, 128, 16, 128};   // Dark luma, neutral chroma
    uint8_t alpha[2] = {128, 128};         // 50% alpha for both pixels
    
    printf("Before blend - dst: Y0=%d U=%d Y1=%d V=%d\n", dst[0], dst[1], dst[2], dst[3]);
    printf("Before blend - src: Y0=%d U=%d Y1=%d V=%d\n", src[0], src[1], src[2], src[3]);
    printf("Alpha values: %d, %d\n", alpha[0], alpha[1]);
    
    alpha_blend_yuyv(dst, src, alpha, 2);
    
    printf("After blend  - dst: Y0=%d U=%d Y1=%d V=%d\n", dst[0], dst[1], dst[2], dst[3]);
}

static void test_yuyv_optimized()
{
    printf("\n=== Testing YUYV Optimized Blending ===\n");
    printf("Implementation: %s\n", alpha_blend_get_implementation());
    
    // Test various widths to ensure SIMD and scalar paths work
    int test_widths[] = {8, 10, 16, 20, 1920};
    int num_widths = sizeof(test_widths) / sizeof(test_widths[0]);
    
    for (int w = 0; w < num_widths; w++) {
        int width = test_widths[w];
        int size = width * 2; // YUYV is 2 bytes per pixel
        
        uint8_t *dst = malloc(size);
        uint8_t *src = malloc(size);
        uint8_t *alpha = malloc(width);
        
        if (!dst || !src || !alpha) {
            printf("Memory allocation failed\n");
            free(dst);
            free(src);
            free(alpha);
            continue;
        }
        
        // Initialize YUYV data with known pattern
        for (int x = 0; x < width; x += 2) {
            // Destination: bright pixels
            dst[x*2 + 0] = 235;  // Y0
            dst[x*2 + 1] = 128;  // U
            dst[x*2 + 2] = 235;  // Y1  
            dst[x*2 + 3] = 128;  // V
            
            // Source: dark pixels
            src[x*2 + 0] = 16;   // Y0
            src[x*2 + 1] = 128;  // U
            src[x*2 + 2] = 16;   // Y1
            src[x*2 + 3] = 128;  // V
            
            // Alpha: 50% blend
            alpha[x] = 128;
            if (x + 1 < width) alpha[x + 1] = 128;
        }
        
        alpha_blend_yuyv(dst, src, alpha, width);
        
        // Check first pixel results
        uint8_t y0 = dst[0];
        uint8_t u = dst[1];
        uint8_t y1 = dst[2]; 
        uint8_t v = dst[3];
        
        // Expected: (16 * 128 + 235 * 127) / 255 ≈ 125
        int expected_y = 125;
        int diff_y = abs(y0 - expected_y);
        
        printf("Width %d: Y0=%d U=%d Y1=%d V=%d (Y expected ~%d, got %d, diff=%d) %s\n", 
               width, y0, u, y1, v, expected_y, y0, diff_y, (diff_y <= 2) ? "✓" : "✗");
        
        free(dst);
        free(src);
        free(alpha);
    }
}

static void benchmark_yuyv_blending()
{
    printf("\n=== Benchmarking YUYV Blending ===\n");
    
    const int width = 1920;
    const int height = 1080;
    const int iterations = 100;
    const int yuyv_size = width * height * 2; // 2 bytes per pixel
    
    uint8_t *dst = malloc(yuyv_size);
    uint8_t *src = malloc(yuyv_size);
    uint8_t *dst_copy = malloc(yuyv_size);
    uint8_t *alpha = malloc(width * height);
    
    if (!dst || !src || !dst_copy || !alpha) {
        printf("Failed to allocate memory for benchmark\n");
        free(dst);
        free(src);
        free(dst_copy);
        free(alpha);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < yuyv_size; i++) {
        dst[i] = rand() & 0xFF;
        src[i] = rand() & 0xFF;
        dst_copy[i] = dst[i];
    }
    for (int i = 0; i < width * height; i++) {
        alpha[i] = rand() & 0xFF;
    }
    
    // Test scalar implementation
    printf("Scalar:\n");
    clock_t start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_yuyv_scalar(dst_copy + y * width * 2, src + y * width * 2, alpha + y * width, width);
        }
    }
    
    clock_t end = clock();
    double elapsed_scalar = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_scalar = 1.0 / (elapsed_scalar / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_scalar);
    printf("  Average: %.3f ms per frame\n", elapsed_scalar * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_scalar * 1000000.0));
    print_resolution_frame_rates(fps_scalar);
    
    // Reset data for optimized test
    for (int i = 0; i < yuyv_size; i++) {
        dst[i] = dst_copy[i];
    }
    
    // Test optimized implementation
    printf("Optimized (%s):\n", alpha_blend_get_implementation());
    start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_yuyv(dst + y * width * 2, src + y * width * 2, alpha + y * width, width);
        }
    }
    
    end = clock();
    double elapsed_opt = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_opt = 1.0 / (elapsed_opt / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_opt);
    printf("  Average: %.3f ms per frame\n", elapsed_opt * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_opt * 1000000.0));
    print_resolution_frame_rates(fps_opt);
    printf("  Speedup: %.1fx\n", elapsed_scalar / elapsed_opt);
    
    free(dst);
    free(src);
    free(dst_copy);
    free(alpha);
}

static void test_rgb_blending()
{
    printf("\n=== Testing RGB Blending ===\n");
    
    // RGB format: R G B (3 bytes per pixel)
    uint8_t dst[6] = {200, 100, 50, 200, 100, 50}; // Two bright pixels
    uint8_t src[6] = {50, 150, 200, 50, 150, 200}; // Two dark/different pixels
    uint8_t alpha[2] = {128, 192};                  // 50% and 75% alpha
    
    printf("Before blend - dst: R=%d G=%d B=%d | R=%d G=%d B=%d\n", 
           dst[0], dst[1], dst[2], dst[3], dst[4], dst[5]);
    printf("Before blend - src: R=%d G=%d B=%d | R=%d G=%d B=%d\n", 
           src[0], src[1], src[2], src[3], src[4], src[5]);
    printf("Alpha values: %d, %d\n", alpha[0], alpha[1]);
    
    alpha_blend_rgb(dst, src, alpha, 2);
    
    printf("After blend  - dst: R=%d G=%d B=%d | R=%d G=%d B=%d\n", 
           dst[0], dst[1], dst[2], dst[3], dst[4], dst[5]);
}

static void test_rgb_optimized()
{
    printf("\n=== Testing RGB Optimized Blending ===\n");
    printf("Implementation: %s\n", alpha_blend_get_implementation());
    
    // Test various widths to ensure SIMD and scalar paths work
    int test_widths[] = {4, 6, 8, 12, 1920};
    int num_widths = sizeof(test_widths) / sizeof(test_widths[0]);
    
    for (int w = 0; w < num_widths; w++) {
        int width = test_widths[w];
        int size = width * 3; // RGB is 3 bytes per pixel
        
        uint8_t *dst = malloc(size);
        uint8_t *src = malloc(size);
        uint8_t *alpha = malloc(width);
        
        if (!dst || !src || !alpha) {
            printf("Memory allocation failed\n");
            free(dst);
            free(src);
            free(alpha);
            continue;
        }
        
        // Initialize RGB data with known pattern
        for (int x = 0; x < width; x++) {
            // Destination: bright pixel (200, 100, 50)
            dst[x*3 + 0] = 200;  // R
            dst[x*3 + 1] = 100;  // G
            dst[x*3 + 2] = 50;   // B
            
            // Source: different pixel (50, 150, 200)
            src[x*3 + 0] = 50;   // R
            src[x*3 + 1] = 150;  // G
            src[x*3 + 2] = 200;  // B
            
            // Alpha: 50% blend
            alpha[x] = 128;
        }
        
        alpha_blend_rgb(dst, src, alpha, width);
        
        // Check first pixel results
        uint8_t r = dst[0];
        uint8_t g = dst[1];
        uint8_t b = dst[2];
        
        // Expected for R: (50 * 128 + 200 * 127) / 255 ≈ 125
        int expected_r = 125;
        int diff_r = abs(r - expected_r);
        
        printf("Width %d: R=%d G=%d B=%d (R expected ~%d, got %d, diff=%d) %s\n", 
               width, r, g, b, expected_r, r, diff_r, (diff_r <= 2) ? "✓" : "✗");
        
        free(dst);
        free(src);
        free(alpha);
    }
}

static void benchmark_rgb_blending()
{
    printf("\n=== Benchmarking RGB Blending ===\n");
    
    const int width = 1920;
    const int height = 1080;
    const int iterations = 100;
    const int rgb_size = width * height * 3; // 3 bytes per pixel
    
    uint8_t *dst = malloc(rgb_size);
    uint8_t *src = malloc(rgb_size);
    uint8_t *dst_copy = malloc(rgb_size);
    uint8_t *alpha = malloc(width * height);
    
    if (!dst || !src || !dst_copy || !alpha) {
        printf("Failed to allocate memory for benchmark\n");
        free(dst);
        free(src);
        free(dst_copy);
        free(alpha);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < rgb_size; i++) {
        dst[i] = rand() & 0xFF;
        src[i] = rand() & 0xFF;
        dst_copy[i] = dst[i];
    }
    for (int i = 0; i < width * height; i++) {
        alpha[i] = rand() & 0xFF;
    }
    
    // Test scalar implementation
    printf("Scalar:\n");
    clock_t start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_rgb_scalar(dst_copy + y * width * 3, src + y * width * 3, alpha + y * width, width);
        }
    }
    
    clock_t end = clock();
    double elapsed_scalar = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_scalar = 1.0 / (elapsed_scalar / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_scalar);
    printf("  Average: %.3f ms per frame\n", elapsed_scalar * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_scalar * 1000000.0));
    print_resolution_frame_rates(fps_scalar);
    
    // Reset data for optimized test
    for (int i = 0; i < rgb_size; i++) {
        dst[i] = dst_copy[i];
    }
    
    // Test optimized implementation
    printf("Optimized (%s):\n", alpha_blend_get_implementation());
    start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_rgb(dst + y * width * 3, src + y * width * 3, alpha + y * width, width);
        }
    }
    
    end = clock();
    double elapsed_opt = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_opt = 1.0 / (elapsed_opt / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_opt);
    printf("  Average: %.3f ms per frame\n", elapsed_opt * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_opt * 1000000.0));
    print_resolution_frame_rates(fps_opt);
    printf("  Speedup: %.1fx\n", elapsed_scalar / elapsed_opt);
    
    free(dst);
    free(src);
    free(dst_copy);
    free(alpha);
}

static void test_v210_blending()
{
    printf("\n=== Testing v210 Blending ===\n");
    
    // v210 format: 6 pixels in 16 bytes
    // Test with minimal data - 6 pixels
    uint8_t dst[16] = {0};
    uint8_t src[16] = {0};
    uint8_t alpha[6] = {128, 128, 128, 128, 128, 128}; // 50% alpha for all
    
    // Initialize v210 data: create simple pattern
    // Word 0: Cb0(128) Y0(235) Cr0(128) - bright luma, neutral chroma
    uint32_t *dst_words = (uint32_t*)dst;
    uint32_t *src_words = (uint32_t*)src;
    
    // Destination: bright pixels (Y=940 in 10-bit scale, chroma=512)
    dst_words[0] = (512 << 0) | (940 << 10) | (512 << 20);  // Cb0 Y0 Cr0
    dst_words[1] = (940 << 0) | (512 << 10) | (940 << 20);  // Y1 Cb2 Y2
    dst_words[2] = (512 << 0) | (940 << 10) | (512 << 20);  // Cr2 Y3 Cb4
    dst_words[3] = (940 << 0) | (512 << 10) | (940 << 20);  // Y4 Cr4 Y5
    
    // Source: dark pixels (Y=64 in 10-bit scale, chroma=512)
    src_words[0] = (512 << 0) | (64 << 10) | (512 << 20);   // Cb0 Y0 Cr0
    src_words[1] = (64 << 0) | (512 << 10) | (64 << 20);    // Y1 Cb2 Y2
    src_words[2] = (512 << 0) | (64 << 10) | (512 << 20);   // Cr2 Y3 Cb4
    src_words[3] = (64 << 0) | (512 << 10) | (64 << 20);    // Y4 Cr4 Y5
    
    printf("Before blend - dst Y0=%d (10-bit)\n", (dst_words[0] >> 10) & 0x3FF);
    printf("Before blend - src Y0=%d (10-bit)\n", (src_words[0] >> 10) & 0x3FF);
    
    alpha_blend_v210(dst, src, alpha, 6);
    
    // Check result
    dst_words = (uint32_t*)dst;
    uint16_t y0_result = (dst_words[0] >> 10) & 0x3FF;
    printf("After blend  - dst Y0=%d (10-bit)\n", y0_result);
    
    // Calculate expected manually step by step
    uint32_t manual_calc = 64 * 128 + 940 * 127;  // = 8192 + 119380 = 127572
    uint16_t manual_result = (manual_calc + (manual_calc >> 8)) >> 8;  // proper division by 255
    printf("Manual calculation: (64*128 + 940*127) = %u, proper division = %d\n", manual_calc, manual_result);
    
    // Expected: (64 * 128 + 940 * 127) / 255 ≈ 500 in 10-bit scale
    int expected = manual_result;
    int diff = abs(y0_result - expected);
    printf("Expected Y0 ~%d, got %d, diff=%d %s\n", expected, y0_result, diff, (diff <= 8) ? "✓" : "✗");
}

static void benchmark_v210_blending()
{
    printf("\n=== Benchmarking v210 Blending ===\n");
    
    const int width = 1920;
    const int height = 1080;
    const int iterations = 100;
    // v210: 6 pixels in 16 bytes, so width pixels need (width/6)*16 bytes
    const int v210_size = (width / 6) * 16 * height;
    
    uint8_t *dst = malloc(v210_size);
    uint8_t *src = malloc(v210_size);
    uint8_t *dst_copy = malloc(v210_size);
    uint8_t *alpha = malloc(width * height);
    
    if (!dst || !src || !dst_copy || !alpha) {
        printf("Failed to allocate memory for benchmark\n");
        free(dst);
        free(src);
        free(dst_copy);
        free(alpha);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < v210_size; i++) {
        dst[i] = rand() & 0xFF;
        src[i] = rand() & 0xFF;
        dst_copy[i] = dst[i];
    }
    for (int i = 0; i < width * height; i++) {
        alpha[i] = rand() & 0xFF;
    }
    
    // Test scalar implementation
    printf("Scalar:\n");
    clock_t start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_v210_scalar(dst_copy + y * (width / 6) * 16, 
                                  src + y * (width / 6) * 16, 
                                  alpha + y * width, width);
        }
    }
    
    clock_t end = clock();
    double elapsed_scalar = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_scalar = 1.0 / (elapsed_scalar / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_scalar);
    printf("  Average: %.3f ms per frame\n", elapsed_scalar * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_scalar * 1000000.0));
    print_resolution_frame_rates(fps_scalar);
    
    // Reset data for optimized test
    for (int i = 0; i < v210_size; i++) {
        dst[i] = dst_copy[i];
    }
    
    // Test optimized implementation
    printf("Optimized (%s):\n", alpha_blend_get_implementation());
    start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_v210(dst + y * (width / 6) * 16, 
                           src + y * (width / 6) * 16, 
                           alpha + y * width, width);
        }
    }
    
    end = clock();
    double elapsed_opt = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_opt = 1.0 / (elapsed_opt / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_opt);
    printf("  Average: %.3f ms per frame\n", elapsed_opt * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_opt * 1000000.0));
    print_resolution_frame_rates(fps_opt);
    printf("  Speedup: %.1fx\n", elapsed_scalar / elapsed_opt);
    
    free(dst);
    free(src);
    free(dst_copy);
    free(alpha);
}

static void test_r10k_blending()
{
    printf("\n=== Testing R10k Blending ===\n");
    
    // R10k format: 4 bytes per pixel, 10 bits per RGB component
    uint8_t dst[8] = {0}; // 2 pixels
    uint8_t src[8] = {0};
    uint8_t alpha[2] = {128, 192}; // 50% and 75% alpha
    
    // Initialize R10k data
    uint32_t *dst_pixels = (uint32_t*)dst;
    uint32_t *src_pixels = (uint32_t*)src;
    
    // Destination: bright pixel (RGB = 800, 600, 400 in 10-bit scale)
    dst_pixels[0] = (800 << 20) | (600 << 10) | 400;  // R G B
    dst_pixels[1] = (800 << 20) | (600 << 10) | 400;
    
    // Source: dark pixel (RGB = 100, 200, 300 in 10-bit scale)  
    src_pixels[0] = (100 << 20) | (200 << 10) | 300;  // R G B
    src_pixels[1] = (100 << 20) | (200 << 10) | 300;
    
    printf("Before blend - dst R=%d G=%d B=%d (10-bit)\n", 
           (dst_pixels[0] >> 20) & 0x3FF,
           (dst_pixels[0] >> 10) & 0x3FF,
           dst_pixels[0] & 0x3FF);
    printf("Before blend - src R=%d G=%d B=%d (10-bit)\n",
           (src_pixels[0] >> 20) & 0x3FF,
           (src_pixels[0] >> 10) & 0x3FF,
           src_pixels[0] & 0x3FF);
    
    alpha_blend_r10k(dst, src, alpha, 2);
    
    // Check result
    dst_pixels = (uint32_t*)dst;
    uint16_t r_result = (dst_pixels[0] >> 20) & 0x3FF;
    uint16_t g_result = (dst_pixels[0] >> 10) & 0x3FF;
    uint16_t b_result = dst_pixels[0] & 0x3FF;
    
    printf("After blend  - dst R=%d G=%d B=%d (10-bit)\n", r_result, g_result, b_result);
    
    // Calculate expected manually for R component: (100 * 128 + 800 * 127) / 255
    uint32_t expected_r_calc = 100 * 128 + 800 * 127;
    uint16_t expected_r = (expected_r_calc + (expected_r_calc >> 8)) >> 8;
    int diff = abs(r_result - expected_r);
    
    printf("Expected R ~%d, got %d, diff=%d %s\n", expected_r, r_result, diff, (diff <= 4) ? "✓" : "✗");
}

static void benchmark_r10k_blending()
{
    printf("\n=== Benchmarking R10k Blending ===\n");
    
    const int width = 1920;
    const int height = 1080;
    const int iterations = 100;
    const int r10k_size = width * height * 4; // 4 bytes per pixel
    
    uint8_t *dst = malloc(r10k_size);
    uint8_t *src = malloc(r10k_size);
    uint8_t *dst_copy = malloc(r10k_size);
    uint8_t *alpha = malloc(width * height);
    
    if (!dst || !src || !dst_copy || !alpha) {
        printf("Failed to allocate memory for benchmark\n");
        free(dst);
        free(src);
        free(dst_copy);
        free(alpha);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < r10k_size; i++) {
        dst[i] = rand() & 0xFF;
        src[i] = rand() & 0xFF;
        dst_copy[i] = dst[i];
    }
    for (int i = 0; i < width * height; i++) {
        alpha[i] = rand() & 0xFF;
    }
    
    // Test scalar implementation
    printf("Scalar:\n");
    clock_t start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_r10k_scalar(dst_copy + y * width * 4, 
                                  src + y * width * 4, 
                                  alpha + y * width, width);
        }
    }
    
    clock_t end = clock();
    double elapsed_scalar = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_scalar = 1.0 / (elapsed_scalar / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_scalar);
    printf("  Average: %.3f ms per frame\n", elapsed_scalar * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_scalar * 1000000.0));
    print_resolution_frame_rates(fps_scalar);
    
    // Reset data for optimized test
    for (int i = 0; i < r10k_size; i++) {
        dst[i] = dst_copy[i];
    }
    
    // Test optimized implementation
    printf("Optimized (%s):\n", alpha_blend_get_implementation());
    start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_r10k(dst + y * width * 4, 
                           src + y * width * 4, 
                           alpha + y * width, width);
        }
    }
    
    end = clock();
    double elapsed_opt = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_opt = 1.0 / (elapsed_opt / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_opt);
    printf("  Average: %.3f ms per frame\n", elapsed_opt * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_opt * 1000000.0));
    print_resolution_frame_rates(fps_opt);
    printf("  Speedup: %.1fx\n", elapsed_scalar / elapsed_opt);
    
    free(dst);
    free(src);
    free(dst_copy);
    free(alpha);
}

static void test_r12l_blending()
{
    printf("\n=== Testing R12L Blending ===\n");
    
    // R12L format: 36 bytes for 8 pixels (12 bits per component)
    // 8 pixels * 36 bits per pixel = 288 bits = 36 bytes
    // Every 9 bytes contains 2 pixels (72 bits = 2 * 36 bits)
    
    uint8_t dst[36] = {0}; // 8 pixels
    uint8_t src[36] = {0};
    uint8_t alpha[8] = {128, 128, 128, 128, 128, 128, 128, 128}; // 50% alpha for all
    
    // Pack test values into R12L format using the same logic as alpha_blend_r12l
    uint16_t dst_r = 3000, dst_g = 2000, dst_b = 1000; // 12-bit values
    uint16_t src_r = 300, src_g = 200, src_b = 100;    // 12-bit values
    
    // Pack all 8 pixels with the same values for simplicity
    for (int pair = 0; pair < 4; pair++) {
        int byte_offset = pair * 9;
        
        // Pack destination pixels
        // R0 and G0 in first 3 bytes
        uint32_t data = (dst_r & 0xFFF) | ((dst_g & 0xFFF) << 12);
        dst[byte_offset] = data & 0xFF;
        dst[byte_offset + 1] = (data >> 8) & 0xFF;
        dst[byte_offset + 2] = (data >> 16) & 0xFF;
        
        // B0 and R1 in next 3 bytes
        data = (dst_b & 0xFFF) | ((dst_r & 0xFFF) << 12);
        dst[byte_offset + 3] = data & 0xFF;
        dst[byte_offset + 4] = (data >> 8) & 0xFF;
        dst[byte_offset + 5] = (data >> 16) & 0xFF;
        
        // G1 and B1 in last 3 bytes
        data = (dst_g & 0xFFF) | ((dst_b & 0xFFF) << 12);
        dst[byte_offset + 6] = data & 0xFF;
        dst[byte_offset + 7] = (data >> 8) & 0xFF;
        dst[byte_offset + 8] = (data >> 16) & 0xFF;
        
        // Pack source pixels
        // R0 and G0 in first 3 bytes
        data = (src_r & 0xFFF) | ((src_g & 0xFFF) << 12);
        src[byte_offset] = data & 0xFF;
        src[byte_offset + 1] = (data >> 8) & 0xFF;
        src[byte_offset + 2] = (data >> 16) & 0xFF;
        
        // B0 and R1 in next 3 bytes
        data = (src_b & 0xFFF) | ((src_r & 0xFFF) << 12);
        src[byte_offset + 3] = data & 0xFF;
        src[byte_offset + 4] = (data >> 8) & 0xFF;
        src[byte_offset + 5] = (data >> 16) & 0xFF;
        
        // G1 and B1 in last 3 bytes
        data = (src_g & 0xFFF) | ((src_b & 0xFFF) << 12);
        src[byte_offset + 6] = data & 0xFF;
        src[byte_offset + 7] = (data >> 8) & 0xFF;
        src[byte_offset + 8] = (data >> 16) & 0xFF;
    }
    
    printf("Before blend - dst R=%d G=%d B=%d (12-bit)\n", dst_r, dst_g, dst_b);
    printf("Before blend - src R=%d G=%d B=%d (12-bit)\n", src_r, src_g, src_b);
    
    // Debug: Print raw bytes before blend
    printf("Raw dst bytes before: ");
    for (int i = 0; i < 9; i++) printf("%02x ", dst[i]);
    printf("\n");
    
    // Test alpha blending
    alpha_blend_r12l(dst, src, alpha, 8);
    
    // Debug: Print raw bytes after blend
    printf("Raw dst bytes after:  ");
    for (int i = 0; i < 9; i++) printf("%02x ", dst[i]);
    printf("\n");
    
    // Unpack first pixel to check result
    uint32_t data = ((uint32_t)dst[0] << 0) |
                   ((uint32_t)dst[1] << 8) |
                   ((uint32_t)dst[2] << 16);
    uint16_t result_r = (data >> 0) & 0xFFF;
    uint16_t result_g = (data >> 12) & 0xFFF;
    
    data = ((uint32_t)dst[3] << 0) |
           ((uint32_t)dst[4] << 8) |
           ((uint32_t)dst[5] << 16);
    uint16_t result_b = (data >> 0) & 0xFFF;
    
    printf("After blend  - dst R=%d G=%d B=%d (12-bit)\n", result_r, result_g, result_b);
    
    // Expected: ~50% blend
    // Alpha 128 is scaled to 12-bit: (128 << 4) | (128 >> 4) = 2048 | 8 = 2056
    // Blend: (src * 2056 + dst * (4095 - 2056)) / 4095
    uint16_t scaled_alpha = (128 << 4) | (128 >> 4);  // 2056
    uint16_t expected_r = ((uint32_t)src_r * scaled_alpha + (uint32_t)dst_r * (4095 - scaled_alpha)) / 4095;
    uint16_t expected_g = ((uint32_t)src_g * scaled_alpha + (uint32_t)dst_g * (4095 - scaled_alpha)) / 4095;
    uint16_t expected_b = ((uint32_t)src_b * scaled_alpha + (uint32_t)dst_b * (4095 - scaled_alpha)) / 4095;
    
    printf("Scaled alpha: %d (from 8-bit %d)\n", scaled_alpha, 128);
    printf("Expected R ~%d, got %d, diff=%d %s\n", expected_r, result_r, 
           abs(expected_r - result_r), (abs(expected_r - result_r) <= 2) ? "✓" : "✗");
    printf("\n");
}

static void benchmark_r12l_blending()
{
    printf("\n=== Benchmarking R12L Blending ===\n");
    
    const int width = 1920;
    const int height = 1080;
    const int iterations = 100;
    // R12L: 36 bytes for 8 pixels, so width pixels need (width/8)*36 bytes
    const int r12l_size = (width / 8) * 36 * height;
    
    uint8_t *dst = malloc(r12l_size);
    uint8_t *src = malloc(r12l_size);
    uint8_t *dst_copy = malloc(r12l_size);
    uint8_t *alpha = malloc(width * height);
    
    if (!dst || !src || !dst_copy || !alpha) {
        printf("Failed to allocate memory for benchmark\n");
        free(dst);
        free(src);
        free(dst_copy);
        free(alpha);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < r12l_size; i++) {
        dst[i] = rand() & 0xFF;
        src[i] = rand() & 0xFF;
        dst_copy[i] = dst[i];
    }
    for (int i = 0; i < width * height; i++) {
        alpha[i] = rand() & 0xFF;
    }
    
    // R12L doesn't have a separate scalar implementation in our test,
    // so we'll just test the main implementation twice for consistency
    
    // Test implementation
    printf("Scalar:\n");
    clock_t start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_r12l(dst_copy + y * (width / 8) * 36, 
                           src + y * (width / 8) * 36, 
                           alpha + y * width, width);
        }
    }
    
    clock_t end = clock();
    double elapsed_scalar = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_scalar = 1.0 / (elapsed_scalar / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_scalar);
    printf("  Average: %.3f ms per frame\n", elapsed_scalar * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_scalar * 1000000.0));
    print_resolution_frame_rates(fps_scalar);
    
    // Reset data for optimized test
    for (int i = 0; i < r12l_size; i++) {
        dst[i] = dst_copy[i];
    }
    
    // Test optimized implementation (same as scalar for now)
    printf("Optimized (%s):\n", alpha_blend_get_implementation());
    start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            alpha_blend_r12l(dst + y * (width / 8) * 36, 
                           src + y * (width / 8) * 36, 
                           alpha + y * width, width);
        }
    }
    
    end = clock();
    double elapsed_opt = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_opt = 1.0 / (elapsed_opt / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_opt);
    printf("  Average: %.3f ms per frame\n", elapsed_opt * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_opt * 1000000.0));
    print_resolution_frame_rates(fps_opt);
    printf("  Speedup: %.1fx\n", elapsed_scalar / elapsed_opt);
    
    free(dst);
    free(src);
    free(dst_copy);
    free(alpha);
}

static void alpha_blend_i420_scalar(uint8_t *dst_y, uint8_t *dst_u, uint8_t *dst_v,
                                    const uint8_t *src_y, const uint8_t *src_u, const uint8_t *src_v,
                                    const uint8_t *alpha, int width, int height)
{
        // Blend Y plane (full resolution)
        for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                        uint8_t a = alpha[y * width + x];
                        dst_y[y * width + x] = (src_y[y * width + x] * a + dst_y[y * width + x] * (255 - a)) / 255;
                }
        }
        
        // Blend U and V planes (half resolution - 4:2:0)
        int chroma_width = width / 2;
        int chroma_height = height / 2;
        
        for (int y = 0; y < chroma_height; y++) {
                for (int x = 0; x < chroma_width; x++) {
                        // Average alpha values for the 2x2 block of pixels
                        int y2 = y * 2;
                        int x2 = x * 2;
                        uint16_t a00 = alpha[y2 * width + x2];
                        uint16_t a01 = (x2 + 1 < width) ? alpha[y2 * width + x2 + 1] : a00;
                        uint16_t a10 = (y2 + 1 < height) ? alpha[(y2 + 1) * width + x2] : a00;
                        uint16_t a11 = ((x2 + 1 < width) && (y2 + 1 < height)) ? 
                                       alpha[(y2 + 1) * width + x2 + 1] : a00;
                        
                        // Calculate average alpha for this chroma sample
                        uint8_t avg_alpha = (a00 + a01 + a10 + a11) / 4;
                        
                        // Blend chroma
                        int idx = y * chroma_width + x;
                        dst_u[idx] = (src_u[idx] * avg_alpha + dst_u[idx] * (255 - avg_alpha)) / 255;
                        dst_v[idx] = (src_v[idx] * avg_alpha + dst_v[idx] * (255 - avg_alpha)) / 255;
                }
        }
}

static void test_i420_blending()
{
    printf("\n=== Testing I420 Blending ===\n");
    
    // I420 format: Planar YUV 4:2:0
    const int width = 4;
    const int height = 4;
    const int y_size = width * height;
    const int chroma_size = (width / 2) * (height / 2);
    
    uint8_t *dst_y = malloc(y_size);
    uint8_t *dst_u = malloc(chroma_size);
    uint8_t *dst_v = malloc(chroma_size);
    uint8_t *src_y = malloc(y_size);
    uint8_t *src_u = malloc(chroma_size);
    uint8_t *src_v = malloc(chroma_size);
    uint8_t *alpha = malloc(y_size);
    
    if (!dst_y || !dst_u || !dst_v || !src_y || !src_u || !src_v || !alpha) {
        printf("Memory allocation failed\n");
        free(dst_y); free(dst_u); free(dst_v);
        free(src_y); free(src_u); free(src_v);
        free(alpha);
        return;
    }
    
    // Initialize I420 data
    // Destination: bright Y=235, neutral chroma U=V=128
    for (int i = 0; i < y_size; i++) {
        dst_y[i] = 235;  // Bright luma
        alpha[i] = 128;  // 50% alpha
    }
    for (int i = 0; i < chroma_size; i++) {
        dst_u[i] = 128;  // Neutral chroma
        dst_v[i] = 128;
    }
    
    // Source: dark Y=16, neutral chroma U=V=128
    for (int i = 0; i < y_size; i++) {
        src_y[i] = 16;   // Dark luma
    }
    for (int i = 0; i < chroma_size; i++) {
        src_u[i] = 128;  // Neutral chroma
        src_v[i] = 128;
    }
    
    printf("Before blend - dst Y[0]=%d U[0]=%d V[0]=%d\n", dst_y[0], dst_u[0], dst_v[0]);
    printf("Before blend - src Y[0]=%d U[0]=%d V[0]=%d\n", src_y[0], src_u[0], src_v[0]);
    printf("Alpha[0] = %d\n", alpha[0]);
    
    alpha_blend_i420(dst_y, dst_u, dst_v, src_y, src_u, src_v, alpha, width, height);
    
    printf("After blend  - dst Y[0]=%d U[0]=%d V[0]=%d\n", dst_y[0], dst_u[0], dst_v[0]);
    
    // Expected Y: (16 * 128 + 235 * 127) / 255 ≈ 125
    int expected_y = 125;
    int diff_y = abs(dst_y[0] - expected_y);
    printf("Expected Y ~%d, got %d, diff=%d %s\n", expected_y, dst_y[0], diff_y, (diff_y <= 2) ? "✓" : "✗");
    
    free(dst_y); free(dst_u); free(dst_v);
    free(src_y); free(src_u); free(src_v);
    free(alpha);
}

static void benchmark_i420_blending()
{
    printf("\n=== Benchmarking I420 Blending ===\n");
    
    const int width = 1920;
    const int height = 1080;
    const int iterations = 100;
    const int y_size = width * height;
    const int chroma_size = (width / 2) * (height / 2);
    
    uint8_t *dst_y = malloc(y_size);
    uint8_t *dst_u = malloc(chroma_size);
    uint8_t *dst_v = malloc(chroma_size);
    uint8_t *dst_y_copy = malloc(y_size);
    uint8_t *dst_u_copy = malloc(chroma_size);
    uint8_t *dst_v_copy = malloc(chroma_size);
    uint8_t *src_y = malloc(y_size);
    uint8_t *src_u = malloc(chroma_size);
    uint8_t *src_v = malloc(chroma_size);
    uint8_t *alpha = malloc(y_size);
    
    if (!dst_y || !dst_u || !dst_v || !dst_y_copy || !dst_u_copy || !dst_v_copy ||
        !src_y || !src_u || !src_v || !alpha) {
        printf("Failed to allocate memory for benchmark\n");
        free(dst_y); free(dst_u); free(dst_v);
        free(dst_y_copy); free(dst_u_copy); free(dst_v_copy);
        free(src_y); free(src_u); free(src_v);
        free(alpha);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < y_size; i++) {
        dst_y[i] = rand() & 0xFF;
        src_y[i] = rand() & 0xFF;
        alpha[i] = rand() & 0xFF;
        dst_y_copy[i] = dst_y[i];
    }
    for (int i = 0; i < chroma_size; i++) {
        dst_u[i] = rand() & 0xFF;
        dst_v[i] = rand() & 0xFF;
        src_u[i] = rand() & 0xFF;
        src_v[i] = rand() & 0xFF;
        dst_u_copy[i] = dst_u[i];
        dst_v_copy[i] = dst_v[i];
    }
    
    // Test scalar implementation
    printf("Scalar:\n");
    clock_t start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        alpha_blend_i420_scalar(dst_y_copy, dst_u_copy, dst_v_copy, 
                               src_y, src_u, src_v, alpha, width, height);
    }
    
    clock_t end = clock();
    double elapsed_scalar = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_scalar = 1.0 / (elapsed_scalar / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_scalar);
    printf("  Average: %.3f ms per frame\n", elapsed_scalar * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_scalar * 1000000.0));
    print_resolution_frame_rates(fps_scalar);
    
    // Reset data for optimized test
    for (int i = 0; i < y_size; i++) {
        dst_y[i] = dst_y_copy[i];
    }
    for (int i = 0; i < chroma_size; i++) {
        dst_u[i] = dst_u_copy[i];
        dst_v[i] = dst_v_copy[i];
    }
    
    // Test optimized implementation
    printf("Optimized (%s):\n", alpha_blend_get_implementation());
    start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        alpha_blend_i420(dst_y, dst_u, dst_v, src_y, src_u, src_v, alpha, width, height);
    }
    
    end = clock();
    double elapsed_opt = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_opt = 1.0 / (elapsed_opt / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_opt);
    printf("  Average: %.3f ms per frame\n", elapsed_opt * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_opt * 1000000.0));
    print_resolution_frame_rates(fps_opt);
    printf("  Speedup: %.1fx\n", elapsed_scalar / elapsed_opt);
    
    free(dst_y); free(dst_u); free(dst_v);
    free(dst_y_copy); free(dst_u_copy); free(dst_v_copy);
    free(src_y); free(src_u); free(src_v);
    free(alpha);
}

static void alpha_blend_y416_scalar(uint8_t *dst, const uint8_t *src, int width)
{
        uint16_t *dst16 = (uint16_t *)dst;
        const uint16_t *src16 = (const uint16_t *)src;
        
        for (int x = 0; x < width; x++) {
                // Extract components (little-endian 16-bit)
                uint16_t u_src = src16[0];
                uint16_t y_src = src16[1];
                uint16_t v_src = src16[2];
                uint16_t a_src = src16[3];
                
                uint16_t u_dst = dst16[0];
                uint16_t y_dst = dst16[1];
                uint16_t v_dst = dst16[2];
                
                // Convert 16-bit alpha to 0-65535 range for blending
                uint32_t inv_alpha = 65535 - a_src;
                
                // Blend components
                dst16[0] = (uint16_t)(((uint32_t)u_src * a_src + (uint32_t)u_dst * inv_alpha) / 65535);
                dst16[1] = (uint16_t)(((uint32_t)y_src * a_src + (uint32_t)y_dst * inv_alpha) / 65535);
                dst16[2] = (uint16_t)(((uint32_t)v_src * a_src + (uint32_t)v_dst * inv_alpha) / 65535);
                dst16[3] = 65535;
                
                dst16 += 4;
                src16 += 4;
        }
}

static void test_y416_blending()
{
    printf("\n=== Testing Y416 Blending ===\n");
    
    // Y416 format: 8 bytes per pixel (4 × 16-bit components)
    uint8_t dst[16] = {0}; // 2 pixels
    uint8_t src[16] = {0};
    
    // Initialize Y416 data (little-endian)
    uint16_t *dst16 = (uint16_t*)dst;
    uint16_t *src16 = (uint16_t*)src;
    
    // Destination: bright pixel (Y=60000, U=V=32768 neutral, A=65535)
    dst16[0] = 32768;  // U
    dst16[1] = 60000;  // Y (bright)
    dst16[2] = 32768;  // V  
    dst16[3] = 65535;  // A (unused in blending)
    
    dst16[4] = 32768;  // U
    dst16[5] = 60000;  // Y
    dst16[6] = 32768;  // V
    dst16[7] = 65535;  // A
    
    // Source: dark pixel with 50% alpha (Y=4000, U=V=32768, A=32768)
    src16[0] = 32768;  // U
    src16[1] = 4000;   // Y (dark)
    src16[2] = 32768;  // V
    src16[3] = 32768;  // A (50% in 16-bit = ~32768)
    
    src16[4] = 32768;  // U
    src16[5] = 4000;   // Y
    src16[6] = 32768;  // V
    src16[7] = 49152;  // A (75% in 16-bit = ~49152)
    
    printf("Before blend - dst Y[0]=%d A[0]=%d\n", dst16[1], dst16[3]);
    printf("Before blend - src Y[0]=%d A[0]=%d\n", src16[1], src16[3]);
    
    alpha_blend_y416(dst, src, 2);
    
    printf("After blend  - dst Y[0]=%d A[0]=%d\n", dst16[1], dst16[3]);
    
    // Expected Y: (4000 * 32768 + 60000 * 32767) / 65535 ≈ 32000
    uint32_t expected_calc = 4000 * 32768 + 60000 * 32767;
    uint16_t expected_y = expected_calc / 65535;
    int diff_y = abs(dst16[1] - expected_y);
    
    printf("Expected Y ~%d, got %d, diff=%d %s\n", expected_y, dst16[1], diff_y, (diff_y <= 100) ? "✓" : "✗");
    printf("Expected A=65535, got %d %s\n", dst16[3], (dst16[3] == 65535) ? "✓" : "✗");
}

static void benchmark_y416_blending()
{
    printf("\n=== Benchmarking Y416 Blending ===\n");
    
    const int width = 1920;
    const int height = 1080;
    const int iterations = 100;
    const int y416_size = width * height * 8; // 8 bytes per pixel
    
    uint8_t *dst = malloc(y416_size);
    uint8_t *src = malloc(y416_size);
    uint8_t *dst_copy = malloc(y416_size);
    
    if (!dst || !src || !dst_copy) {
        printf("Failed to allocate memory for benchmark\n");
        free(dst);
        free(src);
        free(dst_copy);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < y416_size; i++) {
        dst[i] = rand() & 0xFF;
        src[i] = rand() & 0xFF;
        dst_copy[i] = dst[i];
    }
    
    // Test scalar implementation
    printf("Scalar:\n");
    clock_t start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        alpha_blend_y416_scalar(dst_copy, src, width * height);
    }
    
    clock_t end = clock();
    double elapsed_scalar = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_scalar = 1.0 / (elapsed_scalar / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_scalar);
    printf("  Average: %.3f ms per frame\n", elapsed_scalar * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_scalar * 1000000.0));
    print_resolution_frame_rates(fps_scalar);
    
    // Reset data for optimized test
    for (int i = 0; i < y416_size; i++) {
        dst[i] = dst_copy[i];
    }
    
    // Test optimized implementation
    printf("Optimized (%s):\n", alpha_blend_get_implementation());
    start = clock();
    
    for (int iter = 0; iter < iterations; iter++) {
        alpha_blend_y416(dst, src, width * height);
    }
    
    end = clock();
    double elapsed_opt = (double)(end - start) / CLOCKS_PER_SEC;
    double fps_opt = 1.0 / (elapsed_opt / iterations);
    
    printf("  Blended %d frames of %dx%d in %.3f seconds\n", iterations, width, height, elapsed_opt);
    printf("  Average: %.3f ms per frame\n", elapsed_opt * 1000.0 / iterations);
    printf("  Throughput: %.1f megapixels/second\n", (width * height * iterations) / (elapsed_opt * 1000000.0));
    print_resolution_frame_rates(fps_opt);
    printf("  Speedup: %.1fx\n", elapsed_scalar / elapsed_opt);
    
    free(dst);
    free(src);
    free(dst_copy);
}

// Multi-format benchmark at 1920x1080
static void benchmark_multi_format()
{
    printf("\n=== Multi-Format Benchmark (1920x1080) ===\n");
    printf("Format       Pixels/Format  Time/Frame   Throughput   Frame Rate\n");
    printf("----------------------------------------------------------------------\n");
    
    const int width = 1920;
    const int height = 1080;
    const int pixels = width * height;
    const int iterations = 100;
    
    // Test RGBA
    {
        uint8_t *dst = malloc(pixels * 4);
        uint8_t *src = malloc(pixels * 4);
        
        if (dst && src) {
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
            
            double ms_per_frame = (elapsed * 1000.0) / iterations;
            double fps = 1000.0 / ms_per_frame;
            double megapixels_per_sec = (pixels * iterations) / (elapsed * 1e6);
            
            printf("%-12s %dx%d RGBA  %6.2f ms    %7.1f MP/s  %7.1f fps\n", 
                   "RGBA", width, height, ms_per_frame, megapixels_per_sec, fps);
        }
        free(dst);
        free(src);
    }
    
    // Test UYVY
    {
        int uyvy_size = pixels * 2;
        uint8_t *dst = malloc(uyvy_size);
        uint8_t *src = malloc(uyvy_size);
        uint8_t *alpha = malloc(pixels);
        
        if (dst && src && alpha) {
            for (int i = 0; i < uyvy_size; i++) {
                dst[i] = rand() & 0xFF;
                src[i] = rand() & 0xFF;
            }
            for (int i = 0; i < pixels; i++) {
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
            
            double ms_per_frame = (elapsed * 1000.0) / iterations;
            double fps = 1000.0 / ms_per_frame;
            double megapixels_per_sec = (pixels * iterations) / (elapsed * 1e6);
            
            printf("%-12s %dx%d UYVY  %6.2f ms    %7.1f MP/s  %7.1f fps\n", 
                   "UYVY", width, height, ms_per_frame, megapixels_per_sec, fps);
        }
        free(dst);
        free(src);
        free(alpha);
    }
    
    // Test RGB
    {
        int rgb_size = pixels * 3;
        uint8_t *dst = malloc(rgb_size);
        uint8_t *src = malloc(rgb_size);
        uint8_t *alpha = malloc(pixels);
        
        if (dst && src && alpha) {
            for (int i = 0; i < rgb_size; i++) {
                dst[i] = rand() & 0xFF;
                src[i] = rand() & 0xFF;
            }
            for (int i = 0; i < pixels; i++) {
                alpha[i] = rand() & 0xFF;
            }
            
            clock_t start = clock();
            for (int iter = 0; iter < iterations; iter++) {
                for (int y = 0; y < height; y++) {
                    alpha_blend_rgb(dst + y * width * 3, src + y * width * 3, alpha + y * width, width);
                }
            }
            clock_t end = clock();
            double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
            
            double ms_per_frame = (elapsed * 1000.0) / iterations;
            double fps = 1000.0 / ms_per_frame;
            double megapixels_per_sec = (pixels * iterations) / (elapsed * 1e6);
            
            printf("%-12s %dx%d RGB   %6.2f ms    %7.1f MP/s  %7.1f fps\n", 
                   "RGB", width, height, ms_per_frame, megapixels_per_sec, fps);
        }
        free(dst);
        free(src);
        free(alpha);
    }
    
    // Test I420
    {
        int y_size = pixels;
        int chroma_size = pixels / 4;
        uint8_t *dst_y = malloc(y_size);
        uint8_t *dst_u = malloc(chroma_size);
        uint8_t *dst_v = malloc(chroma_size);
        uint8_t *src_y = malloc(y_size);
        uint8_t *src_u = malloc(chroma_size);
        uint8_t *src_v = malloc(chroma_size);
        uint8_t *alpha = malloc(y_size);
        
        if (dst_y && dst_u && dst_v && src_y && src_u && src_v && alpha) {
            for (int i = 0; i < y_size; i++) {
                dst_y[i] = rand() & 0xFF;
                src_y[i] = rand() & 0xFF;
                alpha[i] = rand() & 0xFF;
            }
            for (int i = 0; i < chroma_size; i++) {
                dst_u[i] = rand() & 0xFF;
                dst_v[i] = rand() & 0xFF;
                src_u[i] = rand() & 0xFF;
                src_v[i] = rand() & 0xFF;
            }
            
            clock_t start = clock();
            for (int i = 0; i < iterations; i++) {
                alpha_blend_i420(dst_y, dst_u, dst_v, src_y, src_u, src_v, alpha, width, height);
            }
            clock_t end = clock();
            double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
            
            double ms_per_frame = (elapsed * 1000.0) / iterations;
            double fps = 1000.0 / ms_per_frame;
            double megapixels_per_sec = (pixels * iterations) / (elapsed * 1e6);
            
            printf("%-12s %dx%d I420  %6.2f ms    %7.1f MP/s  %7.1f fps\n", 
                   "I420", width, height, ms_per_frame, megapixels_per_sec, fps);
        }
        free(dst_y); free(dst_u); free(dst_v);
        free(src_y); free(src_u); free(src_v);
        free(alpha);
    }
    
    // Test v210
    {
        int v210_size = (width / 6) * 16 * height;
        uint8_t *dst = malloc(v210_size);
        uint8_t *src = malloc(v210_size);
        uint8_t *alpha = malloc(pixels);
        
        if (dst && src && alpha) {
            for (int i = 0; i < v210_size; i++) {
                dst[i] = rand() & 0xFF;
                src[i] = rand() & 0xFF;
            }
            for (int i = 0; i < pixels; i++) {
                alpha[i] = rand() & 0xFF;
            }
            
            clock_t start = clock();
            for (int iter = 0; iter < iterations; iter++) {
                for (int y = 0; y < height; y++) {
                    alpha_blend_v210(dst + y * (width / 6) * 16, 
                                   src + y * (width / 6) * 16, 
                                   alpha + y * width, width);
                }
            }
            clock_t end = clock();
            double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
            
            double ms_per_frame = (elapsed * 1000.0) / iterations;
            double fps = 1000.0 / ms_per_frame;
            double megapixels_per_sec = (pixels * iterations) / (elapsed * 1e6);
            
            printf("%-12s %dx%d v210  %6.2f ms    %7.1f MP/s  %7.1f fps\n", 
                   "v210", width, height, ms_per_frame, megapixels_per_sec, fps);
        }
        free(dst);
        free(src);
        free(alpha);
    }
    
    printf("\nNote: I420 has lower bandwidth requirements due to 4:2:0 chroma subsampling\n");
}

// Multi-resolution benchmark
static void benchmark_multi_resolution()
{
    typedef struct {
        const char *name;
        int width;
        int height;
    } resolution_t;
    
    const resolution_t resolutions[] = {
        {"HD 720p", 1280, 720},
        {"HD 1080p", 1920, 1080},
        {"UHD 4K", 3840, 2160},
        {"8K", 7680, 4320}
    };
    
    printf("\n=== Multi-Resolution Benchmark (RGBA) ===\n");
    printf("Resolution    Pixels       Time/Frame   Throughput   Frame Rate             Bandwidth\n");
    printf("----------------------------------------------------------------------------------------------------\n");
    
    for (size_t i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); i++) {
        const int width = resolutions[i].width;
        const int height = resolutions[i].height;
        const int pixels = width * height;
        const int iterations = (pixels > 10000000) ? 10 : 50; // Fewer iterations for 4K/8K
        
        uint8_t *dst = malloc(pixels * 4);
        uint8_t *src = malloc(pixels * 4);
        
        if (!dst || !src) {
            printf("%-12s  Memory allocation failed\n", resolutions[i].name);
            free(dst);
            free(src);
            continue;
        }
        
        // Initialize with random data
        for (int j = 0; j < pixels * 4; j++) {
            dst[j] = rand() & 0xFF;
            src[j] = rand() & 0xFF;
        }
        
        // Warm up
        alpha_blend_rgba(dst, src, pixels);
        
        // Benchmark
        clock_t start = clock();
        for (int j = 0; j < iterations; j++) {
            alpha_blend_rgba(dst, src, pixels);
        }
        clock_t end = clock();
        double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
        
        double ms_per_frame = (elapsed * 1000.0) / iterations;
        double fps = 1000.0 / ms_per_frame;
        double megapixels_per_sec = (pixels * iterations) / (elapsed * 1e6);
        double bandwidth_gb_per_sec = (pixels * iterations * 8.0) / (elapsed * 1e9);
        
        printf("%-12s  %4dx%-4d   %6.2f ms    %7.1f MP/s  %7.1f fps            %5.1f GB/s\n", 
               resolutions[i].name, width, height, ms_per_frame, 
               megapixels_per_sec, fps, bandwidth_gb_per_sec);
        
        free(dst);
        free(src);
    }
}

int main()
{
    printf("Alpha Blending Test Program\n");
    printf("===========================\n");
    
    // Run all correctness tests
    test_rgba_blending();
    test_uyvy_blending();
    test_uyvy_optimized();
    test_yuyv_blending();
    test_yuyv_optimized();
    test_rgb_blending();
    test_rgb_optimized();
    test_v210_blending();
    test_r10k_blending();
    test_r12l_blending();
    test_i420_blending();
    test_y416_blending();
    
    // Run all benchmarks at 1920x1080
    benchmark_rgba_blending();
    benchmark_uyvy_blending();
    benchmark_yuyv_blending();
    benchmark_rgb_blending();
    benchmark_v210_blending();
    benchmark_r10k_blending();
    benchmark_r12l_blending();
    benchmark_i420_blending();
    benchmark_y416_blending();
    
    // Run multi-resolution benchmark
    benchmark_multi_resolution();
    
    // Run multi-format benchmark
    benchmark_multi_format();
    
    return 0;
}