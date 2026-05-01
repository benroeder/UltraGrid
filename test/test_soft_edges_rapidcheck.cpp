/**
 * @file   test_soft_edges_rapidcheck.cpp
 * @author Ben Roeder     <ben@sohonet.com>
 * @brief  Property-based tests for overlay soft edge functionality using RapidCheck
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rapidcheck.h>
#include <vector>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <cmath>

// Test the soft edge functions directly by including overlay source
// We'll need to expose these functions for testing
extern "C" {
    // Mock the overlay soft edge functions for testing
    // These are copied from overlay.c for direct testing
    
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
        // No processing if edge width is 0 or negative
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
                    alpha_modifier = std::min(alpha_modifier, apply_gradient(t, type));
                }
                if (edge_sides & EDGE_RIGHT && dist_right < edge_width) {
                    float t = (float)dist_right / edge_width;
                    alpha_modifier = std::min(alpha_modifier, apply_gradient(t, type));
                }
                if (edge_sides & EDGE_TOP && dist_top < edge_width) {
                    float t = (float)dist_top / edge_width;
                    alpha_modifier = std::min(alpha_modifier, apply_gradient(t, type));
                }
                if (edge_sides & EDGE_BOTTOM && dist_bottom < edge_width) {
                    float t = (float)dist_bottom / edge_width;
                    alpha_modifier = std::min(alpha_modifier, apply_gradient(t, type));
                }
                
                // Apply alpha modification
                int pixel_offset = (y * width + x) * 4;
                rgba_data[pixel_offset + 3] = (unsigned char)(rgba_data[pixel_offset + 3] * alpha_modifier);
            }
        }
    }
}

namespace {

// Helper to generate dimensions
auto genImageWidth() {
    return rc::gen::inRange(10, 200);
}

auto genImageHeight() {
    return rc::gen::inRange(10, 200);
}

auto genEdgeWidth() {
    return rc::gen::inRange(0, 50);
}

auto genEdgeType() {
    return rc::gen::element(EDGE_LINEAR, EDGE_GAUSSIAN, EDGE_COSINE);
}

auto genEdgeSides() {
    return rc::gen::inRange(1, 16); // 1-15 covers all combinations
}

auto genPixel() {
    return rc::gen::inRange(0, 256);
}

} // namespace

void test_soft_edge_properties() {
    
    // Test 1: Gradient function properties
    rc::check("apply_gradient: output range validation", []() {
        const int t_int = *rc::gen::inRange(0, 101); // 0-100, convert to 0.0-1.0
        const float t = t_int / 100.0f;
        const enum edge_type type = *genEdgeType();
        
        float result = apply_gradient(t, type);
        
        // All gradient functions should return values in [0, 1] range
        RC_ASSERT(result >= 0.0f);
        RC_ASSERT(result <= 1.0f);
        
        // Test boundary conditions
        if (t == 0.0f) {
            // At t=0, all gradients should return 0 or very close to 0
            RC_ASSERT(result <= 0.1f);
        }
        if (t == 1.0f) {
            // At t=1, all gradients should return 1 or very close to 1
            RC_ASSERT(result >= 0.9f);
        }
    });
    
    // Test 2: Gradient monotonicity
    rc::check("apply_gradient: monotonically increasing", []() {
        const enum edge_type type = *genEdgeType();
        
        float prev_result = apply_gradient(0.0f, type);
        
        // Test that gradient is monotonically increasing
        for (int i = 1; i <= 10; i++) {
            float t = i / 10.0f; // 0.1, 0.2, ..., 1.0
            float current_result = apply_gradient(t, type);
            RC_ASSERT(current_result >= prev_result - 0.01f); // Allow small floating point error
            prev_result = current_result;
        }
    });
    
    // Test 3: Edge width of 0 disables processing
    rc::check("apply_soft_edges: edge_width=0 preserves original", []() {
        const int width = *genImageWidth();
        const int height = *genImageHeight();
        const enum edge_type type = *genEdgeType();
        const int edge_sides = *genEdgeSides();
        
        auto original = *rc::gen::container<std::vector<uint8_t>>(
            width * height * 4, genPixel());
        auto modified = original;
        
        apply_soft_edges(modified.data(), width, height, 0, type, edge_sides);
        
        // Should be unchanged
        RC_ASSERT(modified == original);
    });
    
    // Test 4: Center pixels unaffected by small edge widths
    rc::check("apply_soft_edges: center pixels unaffected by small edges", []() {
        const int width = *rc::gen::inRange(50, 100);
        const int height = *rc::gen::inRange(50, 100);
        const int edge_width = *rc::gen::inRange(1, 10);
        const enum edge_type type = *genEdgeType();
        
        auto original = *rc::gen::container<std::vector<uint8_t>>(
            width * height * 4, genPixel());
        auto modified = original;
        
        apply_soft_edges(modified.data(), width, height, edge_width, type, EDGE_ALL);
        
        // Check center region (well inside edge_width) is unmodified
        int center_x = width / 2;
        int center_y = height / 2;
        
        if (center_x >= edge_width && center_y >= edge_width &&
            center_x < width - edge_width && center_y < height - edge_width) {
            int center_offset = (center_y * width + center_x) * 4;
            
            // RGB components should be unchanged
            RC_ASSERT(modified[center_offset + 0] == original[center_offset + 0]);
            RC_ASSERT(modified[center_offset + 1] == original[center_offset + 1]); 
            RC_ASSERT(modified[center_offset + 2] == original[center_offset + 2]);
            
            // Alpha should be unchanged (no edge effect at center)
            RC_ASSERT(modified[center_offset + 3] == original[center_offset + 3]);
        }
    });
    
    // Test 5: Edge pixels are modified (alpha reduced)
    rc::check("apply_soft_edges: edge pixels have reduced alpha", []() {
        const int width = *rc::gen::inRange(20, 50);
        const int height = *rc::gen::inRange(20, 50);
        const int edge_width = *rc::gen::inRange(5, 15);
        const enum edge_type type = *genEdgeType();
        
        // Create image with full alpha
        std::vector<uint8_t> image(width * height * 4);
        for (size_t i = 0; i < image.size(); i += 4) {
            image[i + 0] = 128; // R
            image[i + 1] = 128; // G
            image[i + 2] = 128; // B
            image[i + 3] = 255; // A (full opacity)
        }
        
        apply_soft_edges(image.data(), width, height, edge_width, type, EDGE_ALL);
        
        // Check corner pixel (should have maximum edge effect)
        int corner_offset = 0; // Top-left corner
        RC_ASSERT(image[corner_offset + 3] < 255); // Alpha should be reduced
        
        // RGB should be unchanged
        RC_ASSERT(image[corner_offset + 0] == 128);
        RC_ASSERT(image[corner_offset + 1] == 128);
        RC_ASSERT(image[corner_offset + 2] == 128);
    });
    
    // Test 6: Selective edge processing
    rc::check("apply_soft_edges: selective edge sides work correctly", []() {
        const int width = 40;
        const int height = 40;
        const int edge_width = 10;
        const enum edge_type type = EDGE_LINEAR;
        
        // Create image with full alpha
        std::vector<uint8_t> image(width * height * 4, 128);
        for (size_t i = 3; i < image.size(); i += 4) {
            image[i] = 255; // Full alpha
        }
        
        auto original = image;
        
        // Apply only left edge
        apply_soft_edges(image.data(), width, height, edge_width, type, EDGE_LEFT);
        
        // Left edge pixels should be modified
        int left_edge_offset = (height / 2) * width * 4 + 0 * 4; // Middle row, leftmost pixel
        RC_ASSERT(image[left_edge_offset + 3] < original[left_edge_offset + 3]);
        
        // Right edge pixels should be unchanged
        int right_edge_offset = (height / 2) * width * 4 + (width - 1) * 4; // Middle row, rightmost pixel
        RC_ASSERT(image[right_edge_offset + 3] == original[right_edge_offset + 3]);
    });
    
    // Test 7: Alpha never increases (soft edges only reduce)
    rc::check("apply_soft_edges: alpha never increases", []() {
        const int width = *genImageWidth();
        const int height = *genImageHeight();
        const int edge_width = *genEdgeWidth();
        const enum edge_type type = *genEdgeType();
        const int edge_sides = *genEdgeSides();
        
        auto original = *rc::gen::container<std::vector<uint8_t>>(
            width * height * 4, genPixel());
        auto modified = original;
        
        apply_soft_edges(modified.data(), width, height, edge_width, type, edge_sides);
        
        // Check that alpha never increases
        for (int i = 0; i < width * height; i++) {
            int offset = i * 4;
            RC_ASSERT(modified[offset + 3] <= original[offset + 3]);
            
            // RGB should be unchanged
            RC_ASSERT(modified[offset + 0] == original[offset + 0]);
            RC_ASSERT(modified[offset + 1] == original[offset + 1]);
            RC_ASSERT(modified[offset + 2] == original[offset + 2]);
        }
    });
    
    // Test 8: Large edge width behavior
    rc::check("apply_soft_edges: large edge width behavior", []() {
        const int width = *rc::gen::inRange(10, 30);
        const int height = *rc::gen::inRange(10, 30);
        const int edge_width = width + height; // Larger than image dimensions
        const enum edge_type type = *genEdgeType();
        
        // Create image with full alpha
        std::vector<uint8_t> image(width * height * 4);
        for (size_t i = 0; i < image.size(); i += 4) {
            image[i + 0] = 100; // R
            image[i + 1] = 150; // G
            image[i + 2] = 200; // B
            image[i + 3] = 255; // A
        }
        
        apply_soft_edges(image.data(), width, height, edge_width, type, EDGE_ALL);
        
        // When edge width is larger than image, all pixels should be affected
        // Center should still have some alpha (not zero due to distance)
        int center_offset = ((height / 2) * width + (width / 2)) * 4;
        RC_ASSERT(image[center_offset + 3] > 0); // Should not be completely transparent
        RC_ASSERT(image[center_offset + 3] < 255); // Should be somewhat transparent
        
        // RGB should still be unchanged
        RC_ASSERT(image[center_offset + 0] == 100);
        RC_ASSERT(image[center_offset + 1] == 150);
        RC_ASSERT(image[center_offset + 2] == 200);
    });
    
    // Test 9: Gradient type differences
    rc::check("apply_soft_edges: different gradient types produce different results", []() {
        const int width = 50;
        const int height = 50;
        const int edge_width = 20;
        
        // Create identical test image
        std::vector<uint8_t> image_linear(width * height * 4);
        std::vector<uint8_t> image_gaussian(width * height * 4);
        std::vector<uint8_t> image_cosine(width * height * 4);
        
        for (size_t i = 0; i < image_linear.size(); i += 4) {
            image_linear[i + 0] = image_gaussian[i + 0] = image_cosine[i + 0] = 128;
            image_linear[i + 1] = image_gaussian[i + 1] = image_cosine[i + 1] = 128;
            image_linear[i + 2] = image_gaussian[i + 2] = image_cosine[i + 2] = 128;
            image_linear[i + 3] = image_gaussian[i + 3] = image_cosine[i + 3] = 255;
        }
        
        apply_soft_edges(image_linear.data(), width, height, edge_width, EDGE_LINEAR, EDGE_ALL);
        apply_soft_edges(image_gaussian.data(), width, height, edge_width, EDGE_GAUSSIAN, EDGE_ALL);
        apply_soft_edges(image_cosine.data(), width, height, edge_width, EDGE_COSINE, EDGE_ALL);
        
        // Check that different gradient types produce different results
        // Test a pixel in the edge region
        int test_offset = (height / 4) * width * 4 + (width / 4) * 4; // Quarter way in
        
        uint8_t alpha_linear = image_linear[test_offset + 3];
        uint8_t alpha_gaussian = image_gaussian[test_offset + 3];
        uint8_t alpha_cosine = image_cosine[test_offset + 3];
        
        // At least one should be different (usually gaussian differs significantly)
        bool has_difference = (alpha_linear != alpha_gaussian) || 
                             (alpha_linear != alpha_cosine) || 
                             (alpha_gaussian != alpha_cosine);
        RC_ASSERT(has_difference);
    });
}