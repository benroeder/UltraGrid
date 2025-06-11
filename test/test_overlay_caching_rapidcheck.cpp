/**
 * @file test_overlay_caching_rapidcheck.cpp
 * @brief Property-based tests for overlay module caching behavior using RapidCheck
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <rapidcheck.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstddef>
#include <climits>
#include <cstring>
#include <unordered_set>

// Include UltraGrid headers for codec types
#include "video_codec.h"
#include "types.h"

namespace {

// Mock overlay state structure to test caching logic
struct MockOverlayState {
    // Overlay image data
    unsigned char *overlay_data;
    unsigned int overlay_width;
    unsigned int overlay_height;
    
    // Cached scaled overlay
    unsigned char *scaled_overlay;
    unsigned int scaled_width;
    unsigned int scaled_height;
    codec_t last_codec;
    
    // SwsContext tracking (simplified)
    bool sws_ctx_valid;
    unsigned int sws_src_width;
    unsigned int sws_src_height;
    unsigned int sws_dst_width;
    unsigned int sws_dst_height;
    
    // Performance counters
    long scale_operations;
    long cache_hits;
    long cache_misses;
    long sws_recreations;
    
    MockOverlayState() : overlay_data(nullptr), overlay_width(0), overlay_height(0),
                        scaled_overlay(nullptr), scaled_width(0), scaled_height(0), 
                        last_codec(VIDEO_CODEC_NONE), sws_ctx_valid(false),
                        sws_src_width(0), sws_src_height(0), sws_dst_width(0), sws_dst_height(0),
                        scale_operations(0), cache_hits(0), cache_misses(0), sws_recreations(0) {}
    
    ~MockOverlayState() {
        free(overlay_data);
        free(scaled_overlay);
    }
};

// Helper to generate frame dimensions
auto genFrameWidth() {
    return rc::gen::inRange(100, 1920);
}

auto genFrameHeight() {
    return rc::gen::inRange(100, 1080);
}

auto genOverlayWidth() {
    return rc::gen::inRange(10, 500);
}

auto genOverlayHeight() {
    return rc::gen::inRange(10, 500);
}

auto genCodec() {
    return rc::gen::element(RGBA, RGB, UYVY, YUYV, v210, R10k, R12L, Y416, I420);
}

// Simulate overlay loading (cache invalidation)
void simulateOverlayLoad(MockOverlayState& state, int width, int height) {
    // Free old overlay data
    free(state.overlay_data);
    state.overlay_data = (unsigned char*)malloc(width * height * 4);
    state.overlay_width = width;
    state.overlay_height = height;
    
    // Invalidate scaled cache (this is what the real overlay.c does)
    free(state.scaled_overlay);
    state.scaled_overlay = nullptr;
    state.scaled_width = 0;
    state.scaled_height = 0;
}

// Simulate processing a frame with caching logic
bool simulateFrameProcess(MockOverlayState& state, int frame_width, int frame_height, codec_t codec) {
    if (!state.overlay_data) {
        return false; // No overlay loaded
    }
    
    bool needs_scaling = (state.overlay_width != frame_width || state.overlay_height != frame_height);
    
    if (needs_scaling) {
        // Check if we can use cached scaled overlay
        bool cache_valid = (state.scaled_overlay != nullptr &&
                           state.scaled_width == frame_width &&
                           state.scaled_height == frame_height &&
                           state.last_codec == codec);
        
        if (cache_valid) {
            // Cache hit
            state.cache_hits++;
        } else {
            // Cache miss - need to scale
            state.cache_misses++;
            state.scale_operations++;
            
            // Check if SwsContext needs recreation
            bool sws_needs_recreation = (!state.sws_ctx_valid ||
                                       state.sws_src_width != state.overlay_width ||
                                       state.sws_src_height != state.overlay_height ||
                                       state.sws_dst_width != frame_width ||
                                       state.sws_dst_height != frame_height);
            
            if (sws_needs_recreation) {
                state.sws_recreations++;
                state.sws_ctx_valid = true;
                state.sws_src_width = state.overlay_width;
                state.sws_src_height = state.overlay_height;
                state.sws_dst_width = frame_width;
                state.sws_dst_height = frame_height;
            }
            
            // Allocate/update scaled overlay cache
            free(state.scaled_overlay);
            state.scaled_overlay = (unsigned char*)malloc(frame_width * frame_height * 4);
            state.scaled_width = frame_width;
            state.scaled_height = frame_height;
            state.last_codec = codec;
        }
    }
    
    return true;
}

// Simulate resolution change (cache invalidation)
void simulateResolutionChange(MockOverlayState& state, int new_width, int new_height) {
    // Invalidate cache if resolution changed (this is what overlay.c does)
    if (state.scaled_overlay && 
        (state.scaled_width != new_width || state.scaled_height != new_height)) {
        free(state.scaled_overlay);
        state.scaled_overlay = nullptr;
        state.scaled_width = 0;
        state.scaled_height = 0;
    }
}

} // namespace

void test_overlay_caching_properties() {
    
    // Test 1: Cache hit rate optimization
    rc::check("scaled overlay cache reduces scaling operations", []() {
        MockOverlayState state;
        
        const int overlay_width = *genOverlayWidth();
        const int overlay_height = *genOverlayHeight();
        const int frame_width = *genFrameWidth();
        const int frame_height = *genFrameHeight();
        const codec_t codec = *genCodec();
        
        // Load overlay once
        simulateOverlayLoad(state, overlay_width, overlay_height);
        
        // Process multiple frames with same dimensions
        const int num_frames = *rc::gen::inRange(5, 20);
        for (int i = 0; i < num_frames; i++) {
            simulateFrameProcess(state, frame_width, frame_height, codec);
        }
        
        // Properties that must hold:
        
        // 1. Only one scaling operation should occur (first frame)
        RC_ASSERT(state.scale_operations == 1);
        
        // 2. Cache hits should be num_frames - 1 (all frames after first)
        RC_ASSERT(state.cache_hits == num_frames - 1);
        
        // 3. Only one cache miss (first frame)
        RC_ASSERT(state.cache_misses == 1);
        
        // 4. SwsContext should be created only once
        RC_ASSERT(state.sws_recreations == 1);
    });
    
    // Test 2: Cache invalidation on overlay reload
    rc::check("overlay reload invalidates scaled cache", []() {
        MockOverlayState state;
        
        const int overlay_width1 = *genOverlayWidth();
        const int overlay_height1 = *genOverlayHeight();
        const int overlay_width2 = *genOverlayWidth();
        const int overlay_height2 = *genOverlayHeight();
        const int frame_width = *genFrameWidth();
        const int frame_height = *genFrameHeight();
        const codec_t codec = *genCodec();
        
        // Load overlay and process frame (creates cache)
        simulateOverlayLoad(state, overlay_width1, overlay_height1);
        simulateFrameProcess(state, frame_width, frame_height, codec);
        
        // Store cache state
        long initial_scale_ops = state.scale_operations;
        bool had_cached_overlay = (state.scaled_overlay != nullptr);
        
        // Reload overlay (should invalidate cache)
        simulateOverlayLoad(state, overlay_width2, overlay_height2);
        
        // Process frame again
        simulateFrameProcess(state, frame_width, frame_height, codec);
        
        // Properties:
        
        // 1. Should have had a cached overlay after first processing
        RC_ASSERT(had_cached_overlay);
        
        // 2. Should have performed at least one more scaling operation after reload
        RC_ASSERT(state.scale_operations > initial_scale_ops);
        
        // 3. Should have had a cache miss after reload
        RC_ASSERT(state.cache_misses >= 1);
    });
    
    // Test 3: Cache invalidation on resolution change
    rc::check("resolution change invalidates appropriate caches", []() {
        MockOverlayState state;
        
        const int overlay_width = *genOverlayWidth();
        const int overlay_height = *genOverlayHeight();
        const int frame_width1 = *genFrameWidth();
        const int frame_height1 = *genFrameHeight();
        const int frame_width2 = *rc::gen::inRange(frame_width1 + 1, frame_width1 + 500);
        const int frame_height2 = *rc::gen::inRange(frame_height1 + 1, frame_height1 + 500);
        const codec_t codec = *genCodec();
        
        // Load overlay and process frame (creates cache)
        simulateOverlayLoad(state, overlay_width, overlay_height);
        simulateFrameProcess(state, frame_width1, frame_height1, codec);
        
        // Simulate resolution change
        simulateResolutionChange(state, frame_width2, frame_height2);
        
        // Process frame with new resolution
        long scale_ops_before = state.scale_operations;
        simulateFrameProcess(state, frame_width2, frame_height2, codec);
        
        // Properties:
        
        // 1. Should perform additional scaling operation for new resolution
        RC_ASSERT(state.scale_operations > scale_ops_before);
        
        // 2. SwsContext should be recreated for new dimensions
        RC_ASSERT(state.sws_recreations >= 2);
    });
    
    // Test 4: Codec change invalidates cache
    rc::check("codec change invalidates scaled overlay cache", []() {
        MockOverlayState state;
        
        const int overlay_width = *genOverlayWidth();
        const int overlay_height = *genOverlayHeight();
        const int frame_width = *genFrameWidth();
        const int frame_height = *genFrameHeight();
        const codec_t codec1 = *genCodec();
        
        // Generate different codec
        codec_t codec2;
        do {
            codec2 = *genCodec();
        } while (codec2 == codec1);
        
        // Load overlay and process frame with first codec
        simulateOverlayLoad(state, overlay_width, overlay_height);
        simulateFrameProcess(state, frame_width, frame_height, codec1);
        
        long initial_scale_ops = state.scale_operations;
        
        // Process frame with different codec (should invalidate cache)
        simulateFrameProcess(state, frame_width, frame_height, codec2);
        
        // Properties:
        
        // 1. Should perform additional scaling operation for different codec
        RC_ASSERT(state.scale_operations > initial_scale_ops);
        
        // 2. Should have cache miss due to codec change
        RC_ASSERT(state.cache_misses >= 1);
    });
    
    // Test 5: Cache efficiency under mixed workloads
    rc::check("cache efficiency under realistic workload patterns", []() {
        MockOverlayState state;
        
        const int overlay_width = *genOverlayWidth();
        const int overlay_height = *genOverlayHeight();
        
        // Create a simple test case with known configs to ensure we get cache hits
        const int frame_width = *genFrameWidth();
        const int frame_height = *genFrameHeight();
        const codec_t codec1 = *genCodec();
        codec_t codec2;
        do {
            codec2 = *genCodec();
        } while (codec2 == codec1);
        
        // Load overlay
        simulateOverlayLoad(state, overlay_width, overlay_height);
        
        // Simple pattern: process same config multiple times, then different config
        const int num_frames = *rc::gen::inRange(6, 12);
        
        std::unordered_set<std::string> unique_configs_processed;
        int frames_needing_scaling = 0;
        
        for (int i = 0; i < num_frames; i++) {
            codec_t codec = (i < num_frames/2) ? codec1 : codec2;
            
            bool needs_scaling = (state.overlay_width != frame_width || state.overlay_height != frame_height);
            if (needs_scaling) {
                frames_needing_scaling++;
                // Track unique configurations that need scaling
                std::string config_key = std::to_string(frame_width) + "x" + std::to_string(frame_height) + "_" + std::to_string(codec);
                unique_configs_processed.insert(config_key);
            }
            
            simulateFrameProcess(state, frame_width, frame_height, codec);
        }
        
        // Properties:
        
        // 1. Number of scaling operations should not exceed number of unique configurations
        RC_ASSERT(state.scale_operations <= (long)unique_configs_processed.size());
        
        // 2. Cache hit rate should improve with repeated configurations
        if (frames_needing_scaling > unique_configs_processed.size()) {
            double cache_hit_rate = (double)state.cache_hits / (state.cache_hits + state.cache_misses);
            RC_ASSERT(cache_hit_rate > 0.0);
        }
        
        // 3. Total operations should match frames needing scaling
        RC_ASSERT(state.scale_operations + state.cache_hits == frames_needing_scaling);
    });
    
    // Test 6: No scaling optimization for same-size overlays
    rc::check("no scaling needed when overlay matches frame size", []() {
        MockOverlayState state;
        
        const int width = *genFrameWidth();
        const int height = *genFrameHeight();
        const codec_t codec = *genCodec();
        
        // Load overlay with same size as frame
        simulateOverlayLoad(state, width, height);
        
        // Process multiple frames
        const int num_frames = *rc::gen::inRange(5, 15);
        for (int i = 0; i < num_frames; i++) {
            simulateFrameProcess(state, width, height, codec);
        }
        
        // Properties:
        
        // 1. No scaling operations should occur when sizes match
        RC_ASSERT(state.scale_operations == 0);
        
        // 2. No cache hits/misses since no scaling cache is used
        RC_ASSERT(state.cache_hits == 0);
        RC_ASSERT(state.cache_misses == 0);
        
        // 3. No SwsContext creation needed
        RC_ASSERT(state.sws_recreations == 0);
    });
    
    // Test 7: Memory consistency in cache operations
    rc::check("cache operations maintain memory consistency", []() {
        MockOverlayState state;
        
        const int overlay_width = *genOverlayWidth();
        const int overlay_height = *genOverlayHeight();
        const int frame_width = *genFrameWidth();
        const int frame_height = *genFrameHeight();
        const codec_t codec = *genCodec();
        
        // Load overlay
        simulateOverlayLoad(state, overlay_width, overlay_height);
        RC_ASSERT(state.overlay_data != nullptr);
        RC_ASSERT(state.scaled_overlay == nullptr); // Should be invalidated
        
        // Process frame (creates cache)
        simulateFrameProcess(state, frame_width, frame_height, codec);
        
        if (overlay_width != frame_width || overlay_height != frame_height) {
            // Should have scaled overlay cached
            RC_ASSERT(state.scaled_overlay != nullptr);
            RC_ASSERT(state.scaled_width == frame_width);
            RC_ASSERT(state.scaled_height == frame_height);
            RC_ASSERT(state.last_codec == codec);
        }
        
        // Reload overlay (should invalidate cache)
        simulateOverlayLoad(state, overlay_width, overlay_height);
        RC_ASSERT(state.overlay_data != nullptr);
        RC_ASSERT(state.scaled_overlay == nullptr); // Should be invalidated again
        
        // Resolution change
        simulateResolutionChange(state, frame_width + 100, frame_height + 100);
        // After resolution change, cache should still be null since it was already invalidated
        RC_ASSERT(state.scaled_overlay == nullptr);
    });
    
    // Test 8: SwsContext caching efficiency
    rc::check("SwsContext caching avoids unnecessary recreations", []() {
        MockOverlayState state;
        
        const int overlay_width = *genOverlayWidth();
        const int overlay_height = *genOverlayHeight();
        const int frame_width = *genFrameWidth();
        const int frame_height = *genFrameHeight();
        const codec_t codec = *genCodec();
        
        // Load overlay
        simulateOverlayLoad(state, overlay_width, overlay_height);
        
        // Process same configuration multiple times
        const int num_frames = *rc::gen::inRange(5, 15);
        for (int i = 0; i < num_frames; i++) {
            simulateFrameProcess(state, frame_width, frame_height, codec);
        }
        
        // Properties:
        
        // 1. SwsContext should be created only once for same dimensions
        if (overlay_width != frame_width || overlay_height != frame_height) {
            RC_ASSERT(state.sws_recreations == 1);
        } else {
            RC_ASSERT(state.sws_recreations == 0); // No scaling needed
        }
        
        // 2. Context should be valid after first creation
        if (overlay_width != frame_width || overlay_height != frame_height) {
            RC_ASSERT(state.sws_ctx_valid);
            RC_ASSERT(state.sws_src_width == overlay_width);
            RC_ASSERT(state.sws_src_height == overlay_height);
            RC_ASSERT(state.sws_dst_width == frame_width);
            RC_ASSERT(state.sws_dst_height == frame_height);
        }
    });
}