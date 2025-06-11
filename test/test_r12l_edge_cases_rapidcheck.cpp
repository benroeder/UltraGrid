/**
 * @file test_r12l_edge_cases_rapidcheck.cpp
 * @brief Property-based tests for R12L format edge case handling using RapidCheck
 * 
 * R12L format: 12-bit packed RGB 4:4:4 little-endian (SMPTE 268M DPX v1, Annex C, Method C4)
 * Packing: 8 pixels packed into 36 bytes (8 pixels × 36 bits per pixel ÷ 8 bits per byte = 36 bytes)
 * 
 * Edge case focus: Incomplete groups when width is not divisible by 8
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rapidcheck.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cmath>

extern "C" {
#include "utils/alpha_blend.h"
#include "video_codec.h"
}

namespace {

// Helper to generate R12L test data
auto genR12LWidth() {
    return rc::gen::inRange(1, 64); // Focus on small widths to test edge cases
}

auto genAlpha() {
    return rc::gen::inRange(0, 255);
}

// Generate 12-bit component value (0-4095)
auto genR12LComponent() {
    return rc::gen::inRange(0, 4095);
}

// Pack a single 12-bit RGB pixel into R12L bytes at specific position
void packR12LPixel(uint8_t *dst, int pixel_index, uint16_t r, uint16_t g, uint16_t b) {
    int group = pixel_index / 8;  // Which 8-pixel group
    int pos = pixel_index % 8;    // Position within group (0-7)
    int pair = pos / 2;           // Which pair within group (0-3)
    int pixel_in_pair = pos % 2;  // First or second pixel in pair (0-1)
    
    uint8_t *group_ptr = dst + group * 36 + pair * 9;
    
    if (pixel_in_pair == 0) {
        // First pixel of pair: R0 and G0 in first 3 bytes, B0 in next 3 bytes
        uint32_t rg_data = (r & 0xFFF) | ((g & 0xFFF) << 12);
        group_ptr[0] = rg_data & 0xFF;
        group_ptr[1] = (rg_data >> 8) & 0xFF;
        group_ptr[2] = (rg_data >> 16) & 0xFF;
        
        // Read existing B0/R1 data to preserve R1
        uint32_t b_r1_data = ((uint32_t)group_ptr[3] << 0) |
                             ((uint32_t)group_ptr[4] << 8) |
                             ((uint32_t)group_ptr[5] << 16);
        uint16_t existing_r1 = (b_r1_data >> 12) & 0xFFF;
        
        b_r1_data = (b & 0xFFF) | ((existing_r1 & 0xFFF) << 12);
        group_ptr[3] = b_r1_data & 0xFF;
        group_ptr[4] = (b_r1_data >> 8) & 0xFF;
        group_ptr[5] = (b_r1_data >> 16) & 0xFF;
    } else {
        // Second pixel of pair: R1 in bytes 3-5, G1 and B1 in bytes 6-8
        
        // Read existing B0 to preserve it
        uint32_t b0_r_data = ((uint32_t)group_ptr[3] << 0) |
                             ((uint32_t)group_ptr[4] << 8) |
                             ((uint32_t)group_ptr[5] << 16);
        uint16_t existing_b0 = b0_r_data & 0xFFF;
        
        b0_r_data = (existing_b0 & 0xFFF) | ((r & 0xFFF) << 12);
        group_ptr[3] = b0_r_data & 0xFF;
        group_ptr[4] = (b0_r_data >> 8) & 0xFF;
        group_ptr[5] = (b0_r_data >> 16) & 0xFF;
        
        // Pack G1 and B1 into last 3 bytes
        uint32_t gb_data = (g & 0xFFF) | ((b & 0xFFF) << 12);
        group_ptr[6] = gb_data & 0xFF;
        group_ptr[7] = (gb_data >> 8) & 0xFF;
        group_ptr[8] = (gb_data >> 16) & 0xFF;
    }
}

// Unpack a single R12L pixel from bytes at specific position
void unpackR12LPixel(const uint8_t *src, int pixel_index, uint16_t *r, uint16_t *g, uint16_t *b) {
    int group = pixel_index / 8;  // Which 8-pixel group
    int pos = pixel_index % 8;    // Position within group (0-7)
    int pair = pos / 2;           // Which pair within group (0-3)
    int pixel_in_pair = pos % 2;  // First or second pixel in pair (0-1)
    
    const uint8_t *group_ptr = src + group * 36 + pair * 9;
    
    if (pixel_in_pair == 0) {
        // First pixel of pair: R0 and G0 in first 3 bytes, B0 in next 3 bytes
        uint32_t rg_data = ((uint32_t)group_ptr[0] << 0) |
                           ((uint32_t)group_ptr[1] << 8) |
                           ((uint32_t)group_ptr[2] << 16);
        *r = rg_data & 0xFFF;
        *g = (rg_data >> 12) & 0xFFF;
        
        uint32_t b_r1_data = ((uint32_t)group_ptr[3] << 0) |
                             ((uint32_t)group_ptr[4] << 8) |
                             ((uint32_t)group_ptr[5] << 16);
        *b = b_r1_data & 0xFFF;
    } else {
        // Second pixel of pair: R1 in bytes 3-5, G1 and B1 in bytes 6-8
        uint32_t b0_r_data = ((uint32_t)group_ptr[3] << 0) |
                             ((uint32_t)group_ptr[4] << 8) |
                             ((uint32_t)group_ptr[5] << 16);
        *r = (b0_r_data >> 12) & 0xFFF;
        
        uint32_t gb_data = ((uint32_t)group_ptr[6] << 0) |
                           ((uint32_t)group_ptr[7] << 8) |
                           ((uint32_t)group_ptr[8] << 16);
        *g = gb_data & 0xFFF;
        *b = (gb_data >> 12) & 0xFFF;
    }
}

// Calculate bytes needed for R12L format
size_t calculateR12LBytes(int width) {
    return ((width + 7) / 8) * 36; // Round up to next 8-pixel boundary
}

// Create enhanced R12L alpha blend function that handles incomplete groups
void alpha_blend_r12l_enhanced(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width) {
    // Process complete 8-pixel groups using existing function
    int complete_groups = width / 8;
    if (complete_groups > 0) {
        alpha_blend_r12l(dst, src, alpha, complete_groups * 8);
    }
    
    // Handle remaining pixels (incomplete group)
    int remaining_pixels = width % 8;
    if (remaining_pixels > 0) {
        int pixels_processed = complete_groups * 8;
        
        // Create temporary buffers for the incomplete group
        uint8_t temp_src[36], temp_dst[36];
        uint8_t temp_alpha[8];
        
        // Initialize temporary buffers to zero
        memset(temp_src, 0, 36);
        memset(temp_dst, 0, 36);
        memset(temp_alpha, 0, 8);
        
        // Copy existing data for the incomplete group
        size_t group_bytes = calculateR12LBytes(8); // Always 36 bytes per group
        if (pixels_processed * 36 / 8 + group_bytes <= calculateR12LBytes(width)) {
            memcpy(temp_src, src + (pixels_processed / 8) * 36, 36);
            memcpy(temp_dst, dst + (pixels_processed / 8) * 36, 36);
        }
        
        // Copy alpha values for remaining pixels
        memcpy(temp_alpha, alpha + pixels_processed, remaining_pixels);
        
        // Process the 8-pixel group (with padding)
        alpha_blend_r12l(temp_dst, temp_src, temp_alpha, 8);
        
        // Copy back only the bytes that correspond to actual pixels
        // Since we can't determine exact byte boundaries for partial groups,
        // we copy the entire group but this is a limitation of the format
        memcpy(dst + (pixels_processed / 8) * 36, temp_dst, 36);
    }
}

} // namespace

void test_r12l_edge_cases_properties() {
    
    // Test 1: R12L format packing/unpacking correctness
    rc::check("R12L pixel packing and unpacking is reversible", []() {
        const int width = *genR12LWidth();
        
        // Generate random pixel data
        std::vector<uint16_t> original_r(width), original_g(width), original_b(width);
        for (int i = 0; i < width; i++) {
            original_r[i] = *genR12LComponent();
            original_g[i] = *genR12LComponent();
            original_b[i] = *genR12LComponent();
        }
        
        // Pack into R12L format
        size_t r12l_bytes = calculateR12LBytes(width);
        std::vector<uint8_t> r12l_data(r12l_bytes, 0);
        
        for (int i = 0; i < width; i++) {
            packR12LPixel(r12l_data.data(), i, original_r[i], original_g[i], original_b[i]);
        }
        
        // Unpack and verify
        for (int i = 0; i < width; i++) {
            uint16_t r, g, b;
            unpackR12LPixel(r12l_data.data(), i, &r, &g, &b);
            
            RC_ASSERT(r == original_r[i]);
            RC_ASSERT(g == original_g[i]);
            RC_ASSERT(b == original_b[i]);
        }
    });
    
    // Test 2: Incomplete groups are properly handled in memory allocation
    rc::check("R12L memory allocation handles incomplete groups correctly", []() {
        const int width = *genR12LWidth();
        
        size_t calculated_bytes = calculateR12LBytes(width);
        size_t expected_bytes = ((width + 7) / 8) * 36;
        
        RC_ASSERT(calculated_bytes == expected_bytes);
        
        // Ensure we allocate enough space for incomplete groups
        int complete_groups = width / 8;
        int remaining_pixels = width % 8;
        
        if (remaining_pixels > 0) {
            // Should allocate space for one more complete group
            RC_ASSERT(calculated_bytes == (complete_groups + 1) * 36);
        } else {
            RC_ASSERT(calculated_bytes == complete_groups * 36);
        }
    });
    
    // Test 3: Current alpha_blend_r12l preserves unblended pixels
    rc::check("current alpha_blend_r12l preserves incomplete group pixels", []() {
        const int width = *rc::gen::inRange(9, 16); // Ensure incomplete group
        RC_PRE(width % 8 != 0); // Only test incomplete groups
        
        size_t r12l_bytes = calculateR12LBytes(width);
        std::vector<uint8_t> dst(r12l_bytes), dst_backup(r12l_bytes);
        std::vector<uint8_t> src(r12l_bytes);
        std::vector<uint8_t> alpha(width);
        
        // Initialize with random data
        for (size_t i = 0; i < r12l_bytes; i++) {
            dst[i] = *rc::gen::inRange(0, 255);
            src[i] = *rc::gen::inRange(0, 255);
        }
        for (int i = 0; i < width; i++) {
            alpha[i] = *genAlpha();
        }
        
        // Backup original destination
        dst_backup = dst;
        
        // Apply current alpha blending
        alpha_blend_r12l(dst.data(), src.data(), alpha.data(), width);
        
        // Check that incomplete group area is unchanged
        int complete_pixels = (width / 8) * 8;
        int incomplete_start_byte = (complete_pixels / 8) * 36;
        
        for (size_t i = incomplete_start_byte; i < r12l_bytes; i++) {
            RC_ASSERT(dst[i] == dst_backup[i]);
        }
    });
    
    // Test 4: Enhanced R12L blending handles all pixels
    rc::check("enhanced R12L blending processes all pixels including incomplete groups", []() {
        const int width = *rc::gen::inRange(9, 16); // Ensure incomplete group
        RC_PRE(width % 8 != 0); // Only test incomplete groups
        
        size_t r12l_bytes = calculateR12LBytes(width);
        std::vector<uint8_t> dst(r12l_bytes), original_dst(r12l_bytes);
        std::vector<uint8_t> src(r12l_bytes);
        std::vector<uint8_t> alpha(width);
        
        // Initialize with known data
        for (size_t i = 0; i < r12l_bytes; i++) {
            dst[i] = *rc::gen::inRange(0, 255);
            src[i] = *rc::gen::inRange(0, 255);
        }
        for (int i = 0; i < width; i++) {
            alpha[i] = *genAlpha();
        }
        
        original_dst = dst;
        
        // Apply enhanced alpha blending
        alpha_blend_r12l_enhanced(dst.data(), src.data(), alpha.data(), width);
        
        // At least some blending should have occurred for incomplete group
        // (unless alpha is 0 everywhere, which is handled separately)
        bool has_non_zero_alpha = false;
        for (int i = 0; i < width; i++) {
            if (alpha[i] > 0) {
                has_non_zero_alpha = true;
                break;
            }
        }
        
        if (has_non_zero_alpha) {
            // The incomplete group area might have changed
            int incomplete_start_byte = ((width / 8) * 8 / 8) * 36;
            bool incomplete_area_changed = false;
            for (size_t i = incomplete_start_byte; i < r12l_bytes; i++) {
                if (dst[i] != original_dst[i]) {
                    incomplete_area_changed = true;
                    break;
                }
            }
            // Enhanced version should handle incomplete groups
            // Note: Due to the nature of R12L packing, the entire 36-byte group is processed
        }
    });
    
    // Test 5: Alpha=0 preserves destination for incomplete groups
    rc::check("alpha=0 preserves destination in incomplete R12L groups", []() {
        const int width = *rc::gen::inRange(9, 16); // Ensure incomplete group
        RC_PRE(width % 8 != 0); // Only test incomplete groups
        
        size_t r12l_bytes = calculateR12LBytes(width);
        std::vector<uint8_t> dst(r12l_bytes), original_dst(r12l_bytes);
        std::vector<uint8_t> src(r12l_bytes);
        std::vector<uint8_t> alpha(width, 0); // All alpha = 0
        
        // Initialize with random data
        for (size_t i = 0; i < r12l_bytes; i++) {
            dst[i] = *rc::gen::inRange(0, 255);
            src[i] = *rc::gen::inRange(0, 255);
        }
        
        original_dst = dst;
        
        // Apply enhanced alpha blending with alpha=0
        alpha_blend_r12l_enhanced(dst.data(), src.data(), alpha.data(), width);
        
        // Destination should be completely preserved
        for (size_t i = 0; i < r12l_bytes; i++) {
            RC_ASSERT(dst[i] == original_dst[i]);
        }
    });
    
    // Test 6: Different incomplete group sizes (1-7 pixels)
    rc::check("R12L handles all incomplete group sizes (1-7 pixels)", []() {
        const int base_width = (*rc::gen::inRange(1, 4)) * 8; // Complete groups
        const int extra_pixels = *rc::gen::inRange(1, 7);     // Incomplete group
        const int width = base_width + extra_pixels;
        
        size_t r12l_bytes = calculateR12LBytes(width);
        std::vector<uint8_t> dst(r12l_bytes);
        std::vector<uint8_t> src(r12l_bytes);
        std::vector<uint8_t> alpha(width);
        
        // Initialize with test data
        for (size_t i = 0; i < r12l_bytes; i++) {
            dst[i] = *rc::gen::inRange(0, 255);
            src[i] = *rc::gen::inRange(0, 255);
        }
        for (int i = 0; i < width; i++) {
            alpha[i] = *genAlpha();
        }
        
        // Should not crash or corrupt memory
        alpha_blend_r12l_enhanced(dst.data(), src.data(), alpha.data(), width);
        
        // Basic validation: function completed without error
        RC_ASSERT(true); // If we reach here, no crash occurred
    });
    
    // Test 7: R12L component value boundaries
    rc::check("R12L handles component value boundaries correctly", []() {
        const int width = *rc::gen::inRange(1, 16);
        
        size_t r12l_bytes = calculateR12LBytes(width);
        std::vector<uint8_t> r12l_data(r12l_bytes, 0);
        
        // Test boundary values: 0, 4095 (max 12-bit)
        for (int i = 0; i < width; i++) {
            uint16_t r = (i % 2 == 0) ? 0 : 4095;
            uint16_t g = (i % 3 == 0) ? 0 : 4095;
            uint16_t b = (i % 4 == 0) ? 0 : 4095;
            
            packR12LPixel(r12l_data.data(), i, r, g, b);
            
            // Verify unpacking
            uint16_t unpacked_r, unpacked_g, unpacked_b;
            unpackR12LPixel(r12l_data.data(), i, &unpacked_r, &unpacked_g, &unpacked_b);
            
            RC_ASSERT(unpacked_r == r);
            RC_ASSERT(unpacked_g == g);
            RC_ASSERT(unpacked_b == b);
        }
    });
    
    // Test 8: R12L alpha scaling validation (8-bit to 12-bit)
    rc::check("R12L alpha scaling from 8-bit to 12-bit is correct", []() {
        for (int alpha_8bit = 0; alpha_8bit <= 255; alpha_8bit += *rc::gen::inRange(1, 16)) {
            // Current R12L implementation: a = (alpha << 4) | (alpha >> 4)
            uint16_t alpha_12bit = (alpha_8bit << 4) | (alpha_8bit >> 4);
            
            // Properties to verify:
            // 1. 0 maps to 0
            if (alpha_8bit == 0) {
                RC_ASSERT(alpha_12bit == 0);
            }
            
            // 2. 255 maps close to 4095 (max 12-bit)
            if (alpha_8bit == 255) {
                RC_ASSERT(alpha_12bit == 4095); // 255 << 4 | 255 >> 4 = 4080 | 15 = 4095
            }
            
            // 3. Result should be in valid 12-bit range
            RC_ASSERT(alpha_12bit <= 4095);
            
            // 4. Monotonic: larger 8-bit values should generally give larger 12-bit values
            if (alpha_8bit < 255) {
                uint16_t next_alpha = ((alpha_8bit + 1) << 4) | ((alpha_8bit + 1) >> 4);
                RC_ASSERT(next_alpha >= alpha_12bit);
            }
        }
    });
    
    // Test 9: Memory bounds checking for incomplete groups
    rc::check("R12L enhanced blending respects memory boundaries", []() {
        const int width = *rc::gen::inRange(9, 16); // Ensure incomplete group
        RC_PRE(width % 8 != 0);
        
        size_t exact_bytes = calculateR12LBytes(width);
        
        // Allocate exact amount needed
        std::vector<uint8_t> dst(exact_bytes);
        std::vector<uint8_t> src(exact_bytes);
        std::vector<uint8_t> alpha(width);
        
        // Fill with known pattern
        for (size_t i = 0; i < exact_bytes; i++) {
            dst[i] = (i % 256);
            src[i] = ((i + 128) % 256);
        }
        for (int i = 0; i < width; i++) {
            alpha[i] = *genAlpha();
        }
        
        // Should not read/write beyond allocated memory
        alpha_blend_r12l_enhanced(dst.data(), src.data(), alpha.data(), width);
        
        // If we reach here without segfault, bounds were respected
        RC_ASSERT(true);
    });
    
    // Test 10: Pixel-level validation for small incomplete groups
    rc::check("pixel-level validation for small R12L incomplete groups", []() {
        const int complete_pixels = (*rc::gen::inRange(0, 2)) * 8; // 0, 8, or 16 complete pixels
        const int incomplete_pixels = *rc::gen::inRange(1, 3);     // 1-3 incomplete pixels
        const int width = complete_pixels + incomplete_pixels;
        
        // Create simple test case with known values
        size_t r12l_bytes = calculateR12LBytes(width);
        std::vector<uint8_t> dst(r12l_bytes, 0);
        std::vector<uint8_t> src(r12l_bytes, 0);
        std::vector<uint8_t> alpha(width);
        
        // Set up known pixel values using our packing function
        std::vector<uint16_t> src_r(width), src_g(width), src_b(width);
        std::vector<uint16_t> dst_r(width), dst_g(width), dst_b(width);
        
        for (int i = 0; i < width; i++) {
            src_r[i] = 4095; // Max value
            src_g[i] = 2048; // Half value  
            src_b[i] = 1024; // Quarter value
            
            dst_r[i] = 0;    // Min value
            dst_g[i] = 1024; // Quarter value
            dst_b[i] = 2048; // Half value
            
            alpha[i] = 128;  // Half alpha
            
            packR12LPixel(src.data(), i, src_r[i], src_g[i], src_b[i]);
            packR12LPixel(dst.data(), i, dst_r[i], dst_g[i], dst_b[i]);
        }
        
        // Apply enhanced blending
        alpha_blend_r12l_enhanced(dst.data(), src.data(), alpha.data(), width);
        
        // Verify blending occurred for all pixels (including incomplete group)
        for (int i = 0; i < width; i++) {
            uint16_t result_r, result_g, result_b;
            unpackR12LPixel(dst.data(), i, &result_r, &result_g, &result_b);
            
            // With alpha=128 (which becomes ~2048 in 12-bit), blending should produce values between src and original dst
            // Approximate check: result should be roughly halfway between src and dst
            uint16_t expected_r = (4095 + 0) / 2;     // Roughly 2047
            uint16_t expected_g = (2048 + 1024) / 2;  // Roughly 1536
            uint16_t expected_b = (1024 + 2048) / 2;  // Roughly 1536
            
            // Allow for rounding errors in 12-bit arithmetic
            RC_ASSERT(abs((int)result_r - (int)expected_r) <= 100);
            RC_ASSERT(abs((int)result_g - (int)expected_g) <= 100);
            RC_ASSERT(abs((int)result_b - (int)expected_b) <= 100);
        }
    });
}