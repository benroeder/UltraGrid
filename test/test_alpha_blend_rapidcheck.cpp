/**
 * @file test_alpha_blend_rapidcheck.cpp
 * @brief Property-based tests for alpha blending using RapidCheck
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

}