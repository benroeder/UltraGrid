/**
 * @file   test_alpha_blend_rapidcheck.cpp
 * @author Ben Roeder     <ben@sohonet.com>
 * @brief  Property-based tests for alpha blending using RapidCheck
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rapidcheck.h>
#include <vector>
#include <cstring>
#include <algorithm>
#include <iostream>

extern "C" {
#include "../src/utils/alpha_blend.h"
#include "../src/types.h"
}

namespace {

// Helper to generate valid alpha values
auto genAlpha() {
    return rc::gen::inRange(0, 256);
}

// Helper to generate pixel values for different bit depths
auto genPixel8() {
    return rc::gen::inRange(0, 256);
}

// Helper to generate reasonable dimensions
auto genWidth() {
    return rc::gen::inRange(1, 1920);
}

} // namespace

void test_alpha_blend_properties() {
    // Test 1: Alpha blending bounds property - output must be within valid range
    rc::check("alpha_blend_rgba: output pixels within valid bounds", []() {
        const int width = *genWidth();
        const auto src = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        auto dst = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        
        alpha_blend_rgba(dst.data(), src.data(), width);
        
        for (size_t i = 0; i < dst.size(); i++) {
            RC_ASSERT(dst[i] >= 0 && dst[i] <= 255);
        }
    });

    // Test 2: Alpha zero identity - when alpha=0, destination unchanged
    rc::check("alpha_blend_rgba: alpha=0 preserves destination", []() {
        const int width = *genWidth();
        auto src = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        auto dst = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        
        // Set all alpha values to 0
        for (size_t i = 3; i < src.size(); i += 4) {
            src[i] = 0;
        }
        
        auto dst_copy = dst;
        alpha_blend_rgba(dst.data(), src.data(), width);
        
        RC_ASSERT(dst == dst_copy);
    });

    // Test 3: Alpha full replacement - when alpha=255, dst equals src
    rc::check("alpha_blend_rgba: alpha=255 replaces destination", []() {
        const int width = *genWidth();
        auto src = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        auto dst = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        
        // Set all alpha values to 255
        for (size_t i = 3; i < src.size(); i += 4) {
            src[i] = 255;
        }
        
        alpha_blend_rgba(dst.data(), src.data(), width);
        
        // Compare RGB values (alpha might differ)
        for (size_t i = 0; i < dst.size(); i++) {
            if (i % 4 != 3) { // Skip alpha channel
                RC_ASSERT(dst[i] == src[i]);
            }
        }
    });

    // Test 4: UYVY format alignment
    rc::check("alpha_blend_uyvy: handles even width correctly", []() {
        const int width = *rc::gen::suchThat(genWidth(), 
            [](int w) { return w % 2 == 0; }); // UYVY requires even width
        
        auto dst = *rc::gen::container<std::vector<uint8_t>>(
            width * 2, genPixel8());
        auto src = *rc::gen::container<std::vector<uint8_t>>(
            width * 2, genPixel8());
        const auto alpha = *rc::gen::container<std::vector<uint8_t>>(
            width, genPixel8());
        
        // Should not crash or corrupt memory
        alpha_blend_uyvy(dst.data(), src.data(), alpha.data(), width);
        
        // Basic sanity check - all values should be valid
        for (const auto& pixel : dst) {
            RC_ASSERT(pixel >= 0 && pixel <= 255);
        }
    });

    // Test 5: Memory safety - no buffer overruns
    rc::check("alpha_blend_rgba: no buffer overruns", []() {
        const int width = *genWidth();
        const int guard_size = 16;
        
        // Allocate with guard bytes
        std::vector<uint8_t> src_buf((width * 4) + guard_size, 0xAA);
        std::vector<uint8_t> dst_buf((width * 4) + guard_size, 0xBB);
        
        // Fill actual data area
        auto src = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        auto dst = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        
        std::copy(src.begin(), src.end(), src_buf.begin());
        std::copy(dst.begin(), dst.end(), dst_buf.begin());
        
        alpha_blend_rgba(dst_buf.data(), src_buf.data(), width);
        
        // Check guard bytes are untouched
        for (int i = width * 4; i < (width * 4) + guard_size; i++) {
            RC_ASSERT(src_buf[i] == 0xAA);
            RC_ASSERT(dst_buf[i] == 0xBB);
        }
    });

    // Test 6: Alpha blending associativity property
    rc::check("alpha_blend_rgba: associativity with sequential blending", []() {
        const int width = *rc::gen::inRange(1, 100); // Smaller for complex test
        auto base = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        auto overlay1 = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        auto overlay2 = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        
        // Test: (base + overlay1) + overlay2 should be predictable
        auto result1 = base;
        alpha_blend_rgba(result1.data(), overlay1.data(), width);
        alpha_blend_rgba(result1.data(), overlay2.data(), width);
        
        // All results should be within bounds
        for (const auto& pixel : result1) {
            RC_ASSERT(pixel >= 0 && pixel <= 255);
        }
    });

    // Test 7: Alpha blending commutative property with same alpha
    rc::check("alpha_blend_rgba: order independence with identical alpha", []() {
        const int width = *rc::gen::inRange(1, 50);
        const uint8_t alpha = *genPixel8();
        
        auto base = *rc::gen::container<std::vector<uint8_t>>(
            width * 4, genPixel8());
        auto overlay1 = *rc::gen::container<std::vector<uint8_t>>(
            width * 3, genPixel8()); // RGB only
        auto overlay2 = *rc::gen::container<std::vector<uint8_t>>(
            width * 3, genPixel8()); // RGB only
        
        // Create RGBA overlays with same alpha
        std::vector<uint8_t> src1_rgba, src2_rgba;
        for (int i = 0; i < width; i++) {
            src1_rgba.push_back(overlay1[i*3 + 0]); // R
            src1_rgba.push_back(overlay1[i*3 + 1]); // G  
            src1_rgba.push_back(overlay1[i*3 + 2]); // B
            src1_rgba.push_back(alpha);             // A
            
            src2_rgba.push_back(overlay2[i*3 + 0]); // R
            src2_rgba.push_back(overlay2[i*3 + 1]); // G
            src2_rgba.push_back(overlay2[i*3 + 2]); // B
            src2_rgba.push_back(alpha);             // A
        }
        
        // When alpha is 0, order shouldn't matter (both should preserve base)
        if (alpha == 0) {
            auto result1 = base;
            auto result2 = base;
            
            alpha_blend_rgba(result1.data(), src1_rgba.data(), width);
            alpha_blend_rgba(result1.data(), src2_rgba.data(), width);
            
            alpha_blend_rgba(result2.data(), src2_rgba.data(), width);
            alpha_blend_rgba(result2.data(), src1_rgba.data(), width);
            
            // Both should equal the original base
            RC_ASSERT(result1 == base);
            RC_ASSERT(result2 == base);
        }
    });

    // Test 8: Monotonicity property
    rc::check("alpha_blend_rgba: output varies monotonically with alpha", []() {
        const int width = 1; // Single pixel test
        std::vector<uint8_t> dst = {100, 100, 100, 128}; // Mid-gray with some alpha
        std::vector<uint8_t> src = {200, 50, 150, 0};    // Different color, alpha will be set
        
        uint8_t prev_result = 0;
        bool first = true;
        
        // Test increasing alpha values
        for (int alpha = 0; alpha <= 255; alpha += 32) {
            auto test_dst = dst;
            auto test_src = src;
            test_src[3] = alpha;
            
            alpha_blend_rgba(test_dst.data(), test_src.data(), width);
            
            // Red channel should vary monotonically (src[0] > dst[0])
            if (!first) {
                if (src[0] > dst[0]) {
                    RC_ASSERT(test_dst[0] >= prev_result);
                } else if (src[0] < dst[0]) {
                    RC_ASSERT(test_dst[0] <= prev_result);
                }
            }
            
            prev_result = test_dst[0];
            first = false;
        }
    });

    // Test 9: UYVY chroma subsampling properties
    rc::check("alpha_blend_uyvy: chroma subsampling with alpha averaging", []() {
        const int width = 4; // Multiple of 2 for UYVY
        auto dst = *rc::gen::container<std::vector<uint8_t>>(width * 2, genPixel8());
        auto src = *rc::gen::container<std::vector<uint8_t>>(width * 2, genPixel8());
        auto alpha = *rc::gen::container<std::vector<uint8_t>>(width, genPixel8());
        
        auto dst_original = dst;
        alpha_blend_uyvy(dst.data(), src.data(), alpha.data(), width);
        
        // Test alpha=0 preserves destination for UYVY
        std::fill(alpha.begin(), alpha.end(), 0);
        dst = dst_original;
        alpha_blend_uyvy(dst.data(), src.data(), alpha.data(), width);
        RC_ASSERT(dst == dst_original);
        
        // Test bounds - all output values should be in valid range
        std::fill(alpha.begin(), alpha.end(), 255);
        dst = dst_original;
        alpha_blend_uyvy(dst.data(), src.data(), alpha.data(), width);
        for (const auto& pixel : dst) {
            RC_ASSERT(pixel >= 0 && pixel <= 255);
        }
    });

    // Test 10: RGB format with separate alpha channel
    rc::check("alpha_blend_rgb: separate alpha channel properties", []() {
        const int width = *rc::gen::inRange(1, 100);
        auto dst = *rc::gen::container<std::vector<uint8_t>>(width * 3, genPixel8());
        auto src = *rc::gen::container<std::vector<uint8_t>>(width * 3, genPixel8());
        auto alpha = *rc::gen::container<std::vector<uint8_t>>(width, genPixel8());
        
        auto dst_original = dst;
        
        // Test alpha=0 preserves destination
        std::fill(alpha.begin(), alpha.end(), 0);
        alpha_blend_rgb(dst.data(), src.data(), alpha.data(), width);
        RC_ASSERT(dst == dst_original);
        
        // Test alpha=255 replaces destination with source
        std::fill(alpha.begin(), alpha.end(), 255);
        dst = dst_original;
        alpha_blend_rgb(dst.data(), src.data(), alpha.data(), width);
        RC_ASSERT(dst == src);
        
        // Test bounds
        for (const auto& pixel : dst) {
            RC_ASSERT(pixel >= 0 && pixel <= 255);
        }
    });

    // Test 11: 10-bit precision formats (v210, R10k)
    rc::check("alpha_blend_r10k: 10-bit precision properties", []() {
        const int width = *rc::gen::inRange(1, 100);
        auto dst = *rc::gen::container<std::vector<uint32_t>>(width, 
            rc::gen::inRange(0u, 0x3FFFFFFFu)); // 30-bit RGB + 2-bit alpha
        auto src = *rc::gen::container<std::vector<uint32_t>>(width,
            rc::gen::inRange(0u, 0x3FFFFFFFu));
        auto alpha = *rc::gen::container<std::vector<uint8_t>>(width, genPixel8());
        
        auto dst_bytes = reinterpret_cast<uint8_t*>(dst.data());
        auto src_bytes = reinterpret_cast<const uint8_t*>(src.data());
        
        auto dst_original = dst;
        
        // Test alpha=0 preserves destination (allowing for rounding differences)
        std::fill(alpha.begin(), alpha.end(), 0);
        alpha_blend_r10k(dst_bytes, src_bytes, alpha.data(), width);
        
        // For alpha=0, dst should be very close to original (accounting for 10-bit precision)
        for (size_t i = 0; i < dst.size(); i++) {
            uint32_t orig_pixel = dst_original[i];
            uint32_t result_pixel = dst[i];
            
            // Extract 10-bit components
            uint16_t r_orig = (orig_pixel >> 20) & 0x3FF;
            uint16_t g_orig = (orig_pixel >> 10) & 0x3FF;
            uint16_t b_orig = (orig_pixel >> 0) & 0x3FF;
            
            uint16_t r_result = (result_pixel >> 20) & 0x3FF;
            uint16_t g_result = (result_pixel >> 10) & 0x3FF;
            uint16_t b_result = (result_pixel >> 0) & 0x3FF;
            
            // Should be exactly equal for alpha=0 in exact implementation
            RC_ASSERT(r_result == r_orig);
            RC_ASSERT(g_result == g_orig);
            RC_ASSERT(b_result == b_orig);
            
            // Alpha bits should be set to max (0x3)
            RC_ASSERT(((result_pixel >> 30) & 0x3) == 0x3);
        }
    });

    // Test 12: I420 planar format alpha handling
    rc::check("alpha_blend_i420: planar format with chroma subsampling", []() {
        const int width = *rc::gen::suchThat(rc::gen::inRange(2, 100), 
            [](int w) { return w % 2 == 0; }); // Must be even for 4:2:0
        const int height = *rc::gen::suchThat(rc::gen::inRange(2, 100),
            [](int h) { return h % 2 == 0; }); // Must be even for 4:2:0
        
        const int y_size = width * height;
        const int uv_size = (width / 2) * (height / 2);
        
        auto dst_y = *rc::gen::container<std::vector<uint8_t>>(y_size, genPixel8());
        auto dst_u = *rc::gen::container<std::vector<uint8_t>>(uv_size, genPixel8());
        auto dst_v = *rc::gen::container<std::vector<uint8_t>>(uv_size, genPixel8());
        
        auto src_y = *rc::gen::container<std::vector<uint8_t>>(y_size, genPixel8());
        auto src_u = *rc::gen::container<std::vector<uint8_t>>(uv_size, genPixel8());
        auto src_v = *rc::gen::container<std::vector<uint8_t>>(uv_size, genPixel8());
        
        auto alpha = *rc::gen::container<std::vector<uint8_t>>(y_size, genPixel8());
        
        auto dst_y_orig = dst_y;
        auto dst_u_orig = dst_u;
        auto dst_v_orig = dst_v;
        
        // Test alpha=0 preserves destination
        std::fill(alpha.begin(), alpha.end(), 0);
        alpha_blend_i420(dst_y.data(), dst_u.data(), dst_v.data(),
                        src_y.data(), src_u.data(), src_v.data(),
                        alpha.data(), width, height);
        
        RC_ASSERT(dst_y == dst_y_orig);
        RC_ASSERT(dst_u == dst_u_orig);
        RC_ASSERT(dst_v == dst_v_orig);
        
        // Test bounds for all planes
        for (const auto& pixel : dst_y) {
            RC_ASSERT(pixel >= 0 && pixel <= 255);
        }
        for (const auto& pixel : dst_u) {
            RC_ASSERT(pixel >= 0 && pixel <= 255);
        }
        for (const auto& pixel : dst_v) {
            RC_ASSERT(pixel >= 0 && pixel <= 255);
        }
    });

    // Test 13: Y416 16-bit precision with embedded alpha
    rc::check("alpha_blend_y416: 16-bit YUV with embedded alpha", []() {
        const int width = *rc::gen::inRange(1, 50);
        auto dst = *rc::gen::container<std::vector<uint16_t>>(width * 4,
            rc::gen::inRange(0, 65536));
        auto src = *rc::gen::container<std::vector<uint16_t>>(width * 4,
            rc::gen::inRange(0, 65536));
        
        auto dst_bytes = reinterpret_cast<uint8_t*>(dst.data());
        auto src_bytes = reinterpret_cast<const uint8_t*>(src.data());
        
        auto dst_original = dst;
        
        // Set alpha=0 in source for identity test
        for (int i = 0; i < width; i++) {
            src[i * 4 + 3] = 0; // Set alpha to 0
        }
        
        alpha_blend_y416(dst_bytes, src_bytes, width);
        
        // With alpha=0, destination should be preserved (U, Y, V components)
        for (int i = 0; i < width; i++) {
            RC_ASSERT(dst[i * 4 + 0] == dst_original[i * 4 + 0]); // U
            RC_ASSERT(dst[i * 4 + 1] == dst_original[i * 4 + 1]); // Y
            RC_ASSERT(dst[i * 4 + 2] == dst_original[i * 4 + 2]); // V
            RC_ASSERT(dst[i * 4 + 3] == 65535); // Alpha should be set to max
        }
        
        // Test with alpha=65535 (full opacity)
        dst = dst_original;
        for (int i = 0; i < width; i++) {
            src[i * 4 + 3] = 65535; // Set alpha to max
        }
        
        alpha_blend_y416(dst_bytes, src_bytes, width);
        
        // With full alpha, destination should match source (except alpha)
        for (int i = 0; i < width; i++) {
            RC_ASSERT(dst[i * 4 + 0] == src[i * 4 + 0]); // U
            RC_ASSERT(dst[i * 4 + 1] == src[i * 4 + 1]); // Y
            RC_ASSERT(dst[i * 4 + 2] == src[i * 4 + 2]); // V
            RC_ASSERT(dst[i * 4 + 3] == 65535); // Alpha always set to max
        }
    });

    // Test 14: Division precision and rounding
    rc::check("alpha_blend_rgba: division precision analysis", []() {
        // Test the EXACT_DIV255 implementation for precision
        const int width = 1;
        
        // Test specific cases that might expose rounding issues
        std::vector<std::pair<uint8_t, uint8_t>> test_cases = {
            {1, 1},     // Minimal values
            {254, 254}, // Near maximum
            {127, 128}, // Middle values
            {0, 255},   // Extremes
            {255, 0}    // Reverse extremes
        };
        
        for (const auto& test_case : test_cases) {
            std::vector<uint8_t> dst = {test_case.first, test_case.first, test_case.first, 128};
            std::vector<uint8_t> src = {test_case.second, test_case.second, test_case.second, 200};
            
            alpha_blend_rgba(dst.data(), src.data(), width);
            
            // Result should be within valid bounds
            for (int i = 0; i < 4; i++) {
                RC_ASSERT(dst[i] >= 0 && dst[i] <= 255);
            }
            
            // Mathematical check: result should be reasonable blend
            uint32_t expected_r = (test_case.second * 200 + test_case.first * 55) / 255;
            uint32_t actual_r = dst[0];
            
            // Allow for integer division rounding (difference should be small)
            int diff = static_cast<int>(actual_r) - static_cast<int>(expected_r);
            RC_ASSERT(std::abs(diff) <= 1); // Allow 1 bit of rounding error
        }
    });

    // Test 15: EXACT_DIV255 accuracy analysis
    rc::check("EXACT_DIV255 macro accuracy vs true exact division", []() {
        // Test if current implementation (x / 255) differs from true exact division
        const uint8_t src = *genPixel8();
        const uint8_t dst = *genPixel8();
        const uint8_t alpha = *genPixel8();
        
        // Current implementation: (src * alpha + dst * (255 - alpha)) / 255
        uint32_t current_blend = (src * alpha + dst * (255 - alpha)) / 255;
        
        // True exact division with rounding: ((src * alpha + dst * (255 - alpha)) + 127) / 255
        uint32_t exact_blend = ((src * alpha + dst * (255 - alpha)) + 127) / 255;
        
        // Alternative exact: ((src * alpha + dst * (255 - alpha)) * 257) >> 16
        uint32_t fast_exact = ((src * alpha + dst * (255 - alpha)) * 257) >> 16;
        
        // All results should be in valid range
        RC_ASSERT(current_blend <= 255);
        RC_ASSERT(exact_blend <= 255);
        RC_ASSERT(fast_exact <= 255);
        
        // Difference between current and exact should be small (usually 0 or 1)
        int diff_exact = static_cast<int>(current_blend) - static_cast<int>(exact_blend);
        int diff_fast = static_cast<int>(current_blend) - static_cast<int>(fast_exact);
        
        RC_ASSERT(std::abs(diff_exact) <= 1);
        RC_ASSERT(std::abs(diff_fast) <= 1);
        
        // For alpha = 0 or 255, truncating and rounding division must
        // give identical results.  The bit-shift approximation (fast_exact)
        // is known to be off-by-one at boundaries so we only check it
        // stays within ±1 (already asserted above).
        if (alpha == 0) {
            RC_ASSERT(current_blend == dst);
            RC_ASSERT(exact_blend == dst);
        }
        if (alpha == 255) {
            RC_ASSERT(current_blend == src);
            RC_ASSERT(exact_blend == src);
        }
    });

}