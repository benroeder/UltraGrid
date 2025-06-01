/**
 * @file   utils/alpha_blend.c
 * @author UltraGrid Team
 * @brief  Alpha blending utilities implementation
 */
/*
 * Copyright (c) 2025 CESNET
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of CESNET nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "utils/alpha_blend.h"

#ifdef __SSE2__
#include <emmintrin.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

/**
 * Scalar implementation of RGBA alpha blending
 */
static void alpha_blend_rgba_scalar(uint8_t *dst, const uint8_t *src, int width)
{
        for (int x = 0; x < width; x++) {
                uint8_t r = src[0];
                uint8_t g = src[1];
                uint8_t b = src[2];
                uint8_t a = src[3];
                
                // Alpha blend: out = overlay * alpha + video * (1 - alpha)
                dst[0] = (r * a + dst[0] * (255 - a)) / 255;
                dst[1] = (g * a + dst[1] * (255 - a)) / 255;
                dst[2] = (b * a + dst[2] * (255 - a)) / 255;
                dst[3] = 255;  // Keep output fully opaque
                
                src += 4;
                dst += 4;
        }
}

#ifdef __SSE2__
/**
 * SSE2 optimized RGBA alpha blending - processes 4 pixels at once
 */
static void alpha_blend_rgba_sse2(uint8_t *dst, const uint8_t *src, int width)
{
        const __m128i zero = _mm_setzero_si128();
        const __m128i alpha_mask = _mm_set1_epi32(0xFF000000);
        
        int x = 0;
        
        // Process 4 pixels at a time
        for (; x <= width - 4; x += 4) {
                // Load 4 pixels from src and dst (16 bytes each)
                __m128i overlay = _mm_loadu_si128((const __m128i*)(src + x * 4));
                __m128i video = _mm_loadu_si128((const __m128i*)(dst + x * 4));
                
                // Separate into two 8x16bit vectors for processing
                __m128i overlay_lo = _mm_unpacklo_epi8(overlay, zero);
                __m128i overlay_hi = _mm_unpackhi_epi8(overlay, zero);
                __m128i video_lo = _mm_unpacklo_epi8(video, zero);
                __m128i video_hi = _mm_unpackhi_epi8(video, zero);
                
                // Extract alpha values and broadcast to all channels
                // Alpha is in bytes 3, 7, 11, 15
                __m128i shuffle_mask = _mm_set_epi8(15, 15, 15, 15, 11, 11, 11, 11, 7, 7, 7, 7, 3, 3, 3, 3);
                __m128i alpha = _mm_shuffle_epi8(overlay, shuffle_mask);
                __m128i alpha_lo = _mm_unpacklo_epi8(alpha, zero);
                __m128i alpha_hi = _mm_unpackhi_epi8(alpha, zero);
                
                // Calculate inverse alpha (255 - alpha)
                __m128i inv_alpha_lo = _mm_sub_epi16(_mm_set1_epi16(255), alpha_lo);
                __m128i inv_alpha_hi = _mm_sub_epi16(_mm_set1_epi16(255), alpha_hi);
                
                // Blend: result = (overlay * alpha + video * inv_alpha + 128) / 255
                // Adding 128 for rounding
                __m128i result_lo = _mm_mullo_epi16(overlay_lo, alpha_lo);
                result_lo = _mm_add_epi16(result_lo, _mm_mullo_epi16(video_lo, inv_alpha_lo));
                result_lo = _mm_add_epi16(result_lo, _mm_set1_epi16(128));
                result_lo = _mm_mulhi_epu16(result_lo, _mm_set1_epi16(0x8081)); // Divide by 255
                
                __m128i result_hi = _mm_mullo_epi16(overlay_hi, alpha_hi);
                result_hi = _mm_add_epi16(result_hi, _mm_mullo_epi16(video_hi, inv_alpha_hi));
                result_hi = _mm_add_epi16(result_hi, _mm_set1_epi16(128));
                result_hi = _mm_mulhi_epu16(result_hi, _mm_set1_epi16(0x8081)); // Divide by 255
                
                // Pack back to 8-bit
                __m128i result = _mm_packus_epi16(result_lo, result_hi);
                
                // Force alpha to 255
                result = _mm_or_si128(result, alpha_mask);
                
                // Store result
                _mm_storeu_si128((__m128i*)(dst + x * 4), result);
        }
        
        // Handle remaining pixels with scalar code
        alpha_blend_rgba_scalar(dst + x * 4, src + x * 4, width - x);
}
#endif

#ifdef __AVX2__
/**
 * AVX2 optimized RGBA alpha blending - processes 8 pixels at once
 */
static void alpha_blend_rgba_avx2(uint8_t *dst, const uint8_t *src, int width)
{
        const __m256i zero = _mm256_setzero_si256();
        const __m256i alpha_mask = _mm256_set1_epi32(0xFF000000);
        
        int x = 0;
        
        // Process 8 pixels at a time
        for (; x <= width - 8; x += 8) {
                // Load 8 pixels from src and dst (32 bytes each)
                __m256i overlay = _mm256_loadu_si256((const __m256i*)(src + x * 4));
                __m256i video = _mm256_loadu_si256((const __m256i*)(dst + x * 4));
                
                // Separate into 16-bit values for processing
                __m256i overlay_lo = _mm256_unpacklo_epi8(overlay, zero);
                __m256i overlay_hi = _mm256_unpackhi_epi8(overlay, zero);
                __m256i video_lo = _mm256_unpacklo_epi8(video, zero);
                __m256i video_hi = _mm256_unpackhi_epi8(video, zero);
                
                // Extract and broadcast alpha values
                // Alpha is in bytes 3, 7, 11, 15, 19, 23, 27, 31
                __m256i shuffle_mask = _mm256_set_epi8(
                    31, 31, 31, 31, 27, 27, 27, 27, 23, 23, 23, 23, 19, 19, 19, 19,
                    15, 15, 15, 15, 11, 11, 11, 11, 7, 7, 7, 7, 3, 3, 3, 3);
                __m256i alpha = _mm256_shuffle_epi8(overlay, shuffle_mask);
                __m256i alpha_lo = _mm256_unpacklo_epi8(alpha, zero);
                __m256i alpha_hi = _mm256_unpackhi_epi8(alpha, zero);
                
                // Calculate inverse alpha
                __m256i inv_alpha_lo = _mm256_sub_epi16(_mm256_set1_epi16(255), alpha_lo);
                __m256i inv_alpha_hi = _mm256_sub_epi16(_mm256_set1_epi16(255), alpha_hi);
                
                // Blend with rounding
                __m256i result_lo = _mm256_mullo_epi16(overlay_lo, alpha_lo);
                result_lo = _mm256_add_epi16(result_lo, _mm256_mullo_epi16(video_lo, inv_alpha_lo));
                result_lo = _mm256_add_epi16(result_lo, _mm256_set1_epi16(128));
                result_lo = _mm256_mulhi_epu16(result_lo, _mm256_set1_epi16(0x8081));
                
                __m256i result_hi = _mm256_mullo_epi16(overlay_hi, alpha_hi);
                result_hi = _mm256_add_epi16(result_hi, _mm256_mullo_epi16(video_hi, inv_alpha_hi));
                result_hi = _mm256_add_epi16(result_hi, _mm256_set1_epi16(128));
                result_hi = _mm256_mulhi_epu16(result_hi, _mm256_set1_epi16(0x8081));
                
                // Pack back to 8-bit
                __m256i result = _mm256_packus_epi16(result_lo, result_hi);
                
                // Force alpha to 255
                result = _mm256_or_si256(result, alpha_mask);
                
                // Store result
                _mm256_storeu_si256((__m256i*)(dst + x * 4), result);
        }
        
        // Handle remaining pixels
#ifdef __SSE2__
        alpha_blend_rgba_sse2(dst + x * 4, src + x * 4, width - x);
#else
        alpha_blend_rgba_scalar(dst + x * 4, src + x * 4, width - x);
#endif
}
#endif

#ifdef __ARM_NEON
/**
 * ARM NEON optimized RGBA alpha blending
 */
static void alpha_blend_rgba_neon(uint8_t *dst, const uint8_t *src, int width)
{
        int x = 0;
        
        // Process 4 pixels at a time (16 bytes)
        for (; x <= width - 4; x += 4) {
                // Load 4 RGBA pixels from source and destination
                uint8x16_t vsrc = vld1q_u8(src + x * 4);
                uint8x16_t vdst = vld1q_u8(dst + x * 4);
                
                // Extract alpha values and replicate to all channels
                // Alpha is in bytes 3, 7, 11, 15
                uint8x16_t valpha = vsrc;
                valpha = vextq_u8(valpha, valpha, 3);  // Shift to get alpha in position 0
                
                // Create alpha mask for all 4 pixels
                uint8x16_t alpha_mask = vdupq_n_u8(0);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 3), alpha_mask, 0);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 3), alpha_mask, 1);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 3), alpha_mask, 2);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 3), alpha_mask, 3);
                
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 7), alpha_mask, 4);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 7), alpha_mask, 5);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 7), alpha_mask, 6);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 7), alpha_mask, 7);
                
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 11), alpha_mask, 8);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 11), alpha_mask, 9);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 11), alpha_mask, 10);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 11), alpha_mask, 11);
                
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 15), alpha_mask, 12);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 15), alpha_mask, 13);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 15), alpha_mask, 14);
                alpha_mask = vsetq_lane_u8(vgetq_lane_u8(vsrc, 15), alpha_mask, 15);
                
                // Calculate inverse alpha (255 - alpha)
                uint8x16_t inv_alpha = vsubq_u8(vdupq_n_u8(255), alpha_mask);
                
                // Convert to 16-bit for multiplication
                uint16x8_t src_lo = vmovl_u8(vget_low_u8(vsrc));
                uint16x8_t src_hi = vmovl_u8(vget_high_u8(vsrc));
                uint16x8_t dst_lo = vmovl_u8(vget_low_u8(vdst));
                uint16x8_t dst_hi = vmovl_u8(vget_high_u8(vdst));
                uint16x8_t alpha_lo = vmovl_u8(vget_low_u8(alpha_mask));
                uint16x8_t alpha_hi = vmovl_u8(vget_high_u8(alpha_mask));
                uint16x8_t inv_alpha_lo = vmovl_u8(vget_low_u8(inv_alpha));
                uint16x8_t inv_alpha_hi = vmovl_u8(vget_high_u8(inv_alpha));
                
                // Blend: result = (src * alpha + dst * inv_alpha + 128) / 255
                uint16x8_t result_lo = vmulq_u16(src_lo, alpha_lo);
                result_lo = vmlaq_u16(result_lo, dst_lo, inv_alpha_lo);
                result_lo = vaddq_u16(result_lo, vdupq_n_u16(128));  // Rounding
                result_lo = vshrq_n_u16(vmulq_u16(result_lo, vdupq_n_u16(0x8081)), 15);
                
                uint16x8_t result_hi = vmulq_u16(src_hi, alpha_hi);
                result_hi = vmlaq_u16(result_hi, dst_hi, inv_alpha_hi);
                result_hi = vaddq_u16(result_hi, vdupq_n_u16(128));  // Rounding
                result_hi = vshrq_n_u16(vmulq_u16(result_hi, vdupq_n_u16(0x8081)), 15);
                
                // Pack back to 8-bit
                uint8x16_t result = vcombine_u8(vqmovn_u16(result_lo), vqmovn_u16(result_hi));
                
                // Force alpha to 255
                result = vsetq_lane_u8(255, result, 3);
                result = vsetq_lane_u8(255, result, 7);
                result = vsetq_lane_u8(255, result, 11);
                result = vsetq_lane_u8(255, result, 15);
                
                // Store result
                vst1q_u8(dst + x * 4, result);
        }
        
        // Handle remaining pixels with scalar code
        alpha_blend_rgba_scalar(dst + x * 4, src + x * 4, width - x);
}
#endif

/**
 * Main RGBA blending function - automatically selects best implementation
 */
void alpha_blend_rgba(uint8_t *dst, const uint8_t *src, int width)
{
#ifdef __AVX2__
        alpha_blend_rgba_avx2(dst, src, width);
#elif defined(__SSE2__)
        alpha_blend_rgba_sse2(dst, src, width);
#elif defined(__ARM_NEON)
        alpha_blend_rgba_neon(dst, src, width);
#else
        alpha_blend_rgba_scalar(dst, src, width);
#endif
}

/**
 * Scalar implementation of UYVY alpha blending
 */
static void alpha_blend_uyvy_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        // UYVY format: U0 Y0 V0 Y1 (covers 2 pixels)
        for (int x = 0; x < width; x += 2) {
                uint8_t alpha0 = alpha[x];
                uint8_t alpha1 = (x + 1 < width) ? alpha[x + 1] : alpha[x];
                
                // Get video components
                uint8_t u_vid = dst[0];
                uint8_t y0_vid = dst[1];
                uint8_t v_vid = dst[2];
                uint8_t y1_vid = dst[3];
                
                // Get overlay components
                uint8_t u_ovr = src[0];
                uint8_t y0_ovr = src[1];
                uint8_t v_ovr = src[2];
                uint8_t y1_ovr = src[3];
                
                // Blend luma (simple per-pixel)
                dst[1] = (y0_ovr * alpha0 + y0_vid * (255 - alpha0)) / 255;
                dst[3] = (y1_ovr * alpha1 + y1_vid * (255 - alpha1)) / 255;
                
                // Blend chroma (shared between 2 pixels - use average alpha)
                uint8_t avg_alpha = (alpha0 + alpha1) / 2;
                dst[0] = (u_ovr * avg_alpha + u_vid * (255 - avg_alpha)) / 255;
                dst[2] = (v_ovr * avg_alpha + v_vid * (255 - avg_alpha)) / 255;
                
                dst += 4;
                src += 4;
        }
}

#ifdef __SSE2__
/**
 * SSE2 optimized UYVY alpha blending - processes 8 pixels at once
 */
static void alpha_blend_uyvy_sse2(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        const __m128i zero = _mm_setzero_si128();
        const __m128i c255 = _mm_set1_epi16(255);
        const __m128i round = _mm_set1_epi16(128);
        const __m128i div255 = _mm_set1_epi16(0x8081);
        
        int x = 0;
        
        // Process 8 pixels at a time (16 bytes = 4 UYVY groups)
        for (; x <= width - 8; x += 8) {
                // Load 16 bytes (8 pixels) of UYVY data
                __m128i vdst = _mm_loadu_si128((const __m128i*)(dst + x * 2));
                __m128i vsrc = _mm_loadu_si128((const __m128i*)(src + x * 2));
                
                // Load 8 alpha values
                __m128i valpha = _mm_loadl_epi64((const __m128i*)(alpha + x));
                valpha = _mm_unpacklo_epi8(valpha, zero);
                
                // Extract Y components (positions 1,3,5,7,9,11,13,15)
                __m128i y_mask = _mm_set_epi8(0,15,0,13,0,11,0,9,0,7,0,5,0,3,0,1);
                __m128i y_vid = _mm_shuffle_epi8(vdst, y_mask);
                __m128i y_ovr = _mm_shuffle_epi8(vsrc, y_mask);
                
                // Extract U components (positions 0,4,8,12)
                __m128i u_mask = _mm_set_epi8(0,0,0,0,0,0,0,0,0,12,0,8,0,4,0,0);
                __m128i u_vid = _mm_shuffle_epi8(vdst, u_mask);
                __m128i u_ovr = _mm_shuffle_epi8(vsrc, u_mask);
                
                // Extract V components (positions 2,6,10,14)
                __m128i v_mask = _mm_set_epi8(0,0,0,0,0,0,0,0,0,14,0,10,0,6,0,2);
                __m128i v_vid = _mm_shuffle_epi8(vdst, v_mask);
                __m128i v_ovr = _mm_shuffle_epi8(vsrc, v_mask);
                
                // Expand alpha values to match Y positions (duplicate each alpha for two Y values)
                __m128i alpha_y = _mm_unpacklo_epi16(valpha, valpha);
                __m128i inv_alpha_y = _mm_sub_epi16(c255, alpha_y);
                
                // Blend Y components
                y_vid = _mm_unpacklo_epi8(y_vid, zero);
                y_ovr = _mm_unpacklo_epi8(y_ovr, zero);
                __m128i y_result = _mm_mullo_epi16(y_ovr, alpha_y);
                y_result = _mm_add_epi16(y_result, _mm_mullo_epi16(y_vid, inv_alpha_y));
                y_result = _mm_add_epi16(y_result, round);
                y_result = _mm_mulhi_epu16(y_result, div255);
                
                // Calculate average alpha for U/V components (average pairs)
                __m128i alpha_even = _mm_and_si128(valpha, _mm_set_epi16(0,0xFF,0,0xFF,0,0xFF,0,0xFF));
                __m128i alpha_odd = _mm_srli_epi16(valpha, 8);
                __m128i alpha_avg = _mm_avg_epu16(alpha_even, alpha_odd);
                alpha_avg = _mm_shufflelo_epi16(alpha_avg, 0xD8); // 0,2,1,3
                alpha_avg = _mm_shufflehi_epi16(alpha_avg, 0xD8);
                alpha_avg = _mm_packs_epi32(alpha_avg, alpha_avg);
                __m128i inv_alpha_avg = _mm_sub_epi16(c255, alpha_avg);
                
                // Blend U components
                u_vid = _mm_unpacklo_epi8(u_vid, zero);
                u_ovr = _mm_unpacklo_epi8(u_ovr, zero);
                __m128i u_result = _mm_mullo_epi16(u_ovr, alpha_avg);
                u_result = _mm_add_epi16(u_result, _mm_mullo_epi16(u_vid, inv_alpha_avg));
                u_result = _mm_add_epi16(u_result, round);
                u_result = _mm_mulhi_epu16(u_result, div255);
                
                // Blend V components
                v_vid = _mm_unpacklo_epi8(v_vid, zero);
                v_ovr = _mm_unpacklo_epi8(v_ovr, zero);
                __m128i v_result = _mm_mullo_epi16(v_ovr, alpha_avg);
                v_result = _mm_add_epi16(v_result, _mm_mullo_epi16(v_vid, inv_alpha_avg));
                v_result = _mm_add_epi16(v_result, round);
                v_result = _mm_mulhi_epu16(v_result, div255);
                
                // Pack results back to 8-bit
                y_result = _mm_packus_epi16(y_result, zero);
                u_result = _mm_packus_epi16(u_result, zero);
                v_result = _mm_packus_epi16(v_result, zero);
                
                // Reconstruct UYVY data
                // First merge U and Y0 (U in even positions, Y in odd)
                __m128i uy = _mm_unpacklo_epi8(u_result, y_result);
                
                // Then merge V and Y1 (we need to extract odd Y values)
                __m128i y_odd = _mm_srli_epi16(y_result, 8);
                __m128i vy = _mm_unpacklo_epi8(v_result, y_odd);
                
                // Final UYVY is interleaved UY and VY pairs
                __m128i result_lo = _mm_unpacklo_epi16(uy, vy);
                __m128i result_hi = _mm_unpackhi_epi16(uy, vy);
                
                // Store results
                _mm_storeu_si128((__m128i*)(dst + x * 2), result_lo);
                if (x + 4 < width) {
                        _mm_storel_epi64((__m128i*)(dst + (x + 4) * 2), result_hi);
                }
        }
        
        // Handle remaining pixels with scalar code
        alpha_blend_uyvy_scalar(dst + x * 2, src + x * 2, alpha + x, width - x);
}
#endif

#ifdef __AVX2__
/**
 * AVX2 optimized UYVY alpha blending - processes 16 pixels at once
 */
static void alpha_blend_uyvy_avx2(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        const __m256i zero = _mm256_setzero_si256();
        const __m256i c255 = _mm256_set1_epi16(255);
        const __m256i round = _mm256_set1_epi16(128);
        const __m256i div255 = _mm256_set1_epi16(0x8081);
        
        int x = 0;
        
        // Process 16 pixels at a time (32 bytes = 8 UYVY groups)
        for (; x <= width - 16; x += 16) {
                // Load 32 bytes (16 pixels) of UYVY data
                __m256i vdst = _mm256_loadu_si256((const __m256i*)(dst + x * 2));
                __m256i vsrc = _mm256_loadu_si256((const __m256i*)(src + x * 2));
                
                // Load 16 alpha values
                __m128i alpha128 = _mm_loadu_si128((const __m128i*)(alpha + x));
                __m256i valpha = _mm256_cvtepu8_epi16(alpha128);
                
                // Extract Y components using shuffle
                __m256i y_indices = _mm256_set_epi8(
                    31,29,27,25,23,21,19,17,15,13,11,9,7,5,3,1,
                    31,29,27,25,23,21,19,17,15,13,11,9,7,5,3,1);
                __m256i y_vid_packed = _mm256_shuffle_epi8(vdst, y_indices);
                __m256i y_ovr_packed = _mm256_shuffle_epi8(vsrc, y_indices);
                
                // Combine Y values from both lanes
                __m128i y_vid_lo = _mm256_extracti128_si256(y_vid_packed, 0);
                __m128i y_vid_hi = _mm256_extracti128_si256(y_vid_packed, 1);
                __m128i y_vid_bytes = _mm_unpacklo_epi64(y_vid_lo, y_vid_hi);
                __m256i y_vid = _mm256_cvtepu8_epi16(y_vid_bytes);
                
                __m128i y_ovr_lo = _mm256_extracti128_si256(y_ovr_packed, 0);
                __m128i y_ovr_hi = _mm256_extracti128_si256(y_ovr_packed, 1);
                __m128i y_ovr_bytes = _mm_unpacklo_epi64(y_ovr_lo, y_ovr_hi);
                __m256i y_ovr = _mm256_cvtepu8_epi16(y_ovr_bytes);
                
                // Calculate inverse alpha for Y
                __m256i inv_alpha_y = _mm256_sub_epi16(c255, valpha);
                
                // Blend Y components
                __m256i y_result = _mm256_mullo_epi16(y_ovr, valpha);
                y_result = _mm256_add_epi16(y_result, _mm256_mullo_epi16(y_vid, inv_alpha_y));
                y_result = _mm256_add_epi16(y_result, round);
                y_result = _mm256_mulhi_epu16(y_result, div255);
                
                // Extract U and V components - they're at positions 0,4,8,12... and 2,6,10,14...
                __m256i uv_indices_u = _mm256_set_epi8(
                    -1,-1,-1,-1,-1,-1,-1,-1,28,24,20,16,12,8,4,0,
                    -1,-1,-1,-1,-1,-1,-1,-1,28,24,20,16,12,8,4,0);
                __m256i uv_indices_v = _mm256_set_epi8(
                    -1,-1,-1,-1,-1,-1,-1,-1,30,26,22,18,14,10,6,2,
                    -1,-1,-1,-1,-1,-1,-1,-1,30,26,22,18,14,10,6,2);
                
                __m256i u_vid_packed = _mm256_shuffle_epi8(vdst, uv_indices_u);
                __m256i u_ovr_packed = _mm256_shuffle_epi8(vsrc, uv_indices_u);
                __m256i v_vid_packed = _mm256_shuffle_epi8(vdst, uv_indices_v);
                __m256i v_ovr_packed = _mm256_shuffle_epi8(vsrc, uv_indices_v);
                
                // Combine U/V values from both lanes
                __m128i u_vid_bytes = _mm_unpacklo_epi64(
                    _mm256_extracti128_si256(u_vid_packed, 0),
                    _mm256_extracti128_si256(u_vid_packed, 1));
                __m128i u_ovr_bytes = _mm_unpacklo_epi64(
                    _mm256_extracti128_si256(u_ovr_packed, 0),
                    _mm256_extracti128_si256(u_ovr_packed, 1));
                __m128i v_vid_bytes = _mm_unpacklo_epi64(
                    _mm256_extracti128_si256(v_vid_packed, 0),
                    _mm256_extracti128_si256(v_vid_packed, 1));
                __m128i v_ovr_bytes = _mm_unpacklo_epi64(
                    _mm256_extracti128_si256(v_ovr_packed, 0),
                    _mm256_extracti128_si256(v_ovr_packed, 1));
                
                // Convert to 16-bit for processing
                __m256i u_vid = _mm256_cvtepu8_epi16(u_vid_bytes);
                __m256i u_ovr = _mm256_cvtepu8_epi16(u_ovr_bytes);
                __m256i v_vid = _mm256_cvtepu8_epi16(v_vid_bytes);
                __m256i v_ovr = _mm256_cvtepu8_epi16(v_ovr_bytes);
                
                // Calculate average alpha for U/V (average adjacent pairs)
                __m256i alpha_even = _mm256_and_si256(valpha, _mm256_set_epi16(0,0xFF,0,0xFF,0,0xFF,0,0xFF,0,0xFF,0,0xFF,0,0xFF,0,0xFF));
                __m256i alpha_odd = _mm256_srli_epi16(valpha, 8);
                __m256i alpha_avg = _mm256_avg_epu16(alpha_even, alpha_odd);
                // Compress to get one alpha per U/V pair
                alpha_avg = _mm256_permute4x64_epi64(alpha_avg, 0xD8);
                alpha_avg = _mm256_shuffle_epi8(alpha_avg, 
                    _mm256_set_epi8(
                        -1,-1,-1,-1,-1,-1,-1,-1,14,14,10,10,6,6,2,2,
                        -1,-1,-1,-1,-1,-1,-1,-1,14,14,10,10,6,6,2,2));
                __m256i inv_alpha_avg = _mm256_sub_epi16(c255, alpha_avg);
                
                // Blend U components
                __m256i u_result = _mm256_mullo_epi16(u_ovr, alpha_avg);
                u_result = _mm256_add_epi16(u_result, _mm256_mullo_epi16(u_vid, inv_alpha_avg));
                u_result = _mm256_add_epi16(u_result, round);
                u_result = _mm256_mulhi_epu16(u_result, div255);
                
                // Blend V components
                __m256i v_result = _mm256_mullo_epi16(v_ovr, alpha_avg);
                v_result = _mm256_add_epi16(v_result, _mm256_mullo_epi16(v_vid, inv_alpha_avg));
                v_result = _mm256_add_epi16(v_result, round);
                v_result = _mm256_mulhi_epu16(v_result, div255);
                
                // Pack results back to 8-bit and reconstruct UYVY
                __m128i y_result_packed = _mm256_cvtepi16_epi8(y_result);
                __m128i u_result_packed = _mm256_cvtepi16_epi8(u_result);
                __m128i v_result_packed = _mm256_cvtepi16_epi8(v_result);
                
                // Interleave to create UYVY format
                __m256i result = _mm256_setzero_si256();
                
                // Place U values at positions 0,4,8,12...
                for (int i = 0; i < 8; i++) {
                        ((uint8_t*)&result)[i*4] = ((uint8_t*)&u_result_packed)[i];
                        ((uint8_t*)&result)[i*4+1] = ((uint8_t*)&y_result_packed)[i*2];
                        ((uint8_t*)&result)[i*4+2] = ((uint8_t*)&v_result_packed)[i];
                        ((uint8_t*)&result)[i*4+3] = ((uint8_t*)&y_result_packed)[i*2+1];
                }
                
                _mm256_storeu_si256((__m256i*)(dst + x * 2), result);
        }
        
        // Handle remaining pixels
#ifdef __SSE2__
        alpha_blend_uyvy_sse2(dst + x * 2, src + x * 2, alpha + x, width - x);
#else
        alpha_blend_uyvy_scalar(dst + x * 2, src + x * 2, alpha + x, width - x);
#endif
}
#endif

#ifdef __ARM_NEON
/**
 * ARM NEON optimized UYVY alpha blending
 */
static void alpha_blend_uyvy_neon(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        int x = 0;
        
        // Process 8 pixels at a time (16 bytes = 4 UYVY groups)
        for (; x <= width - 8; x += 8) {
                // Load 16 bytes (8 pixels) of UYVY data
                uint8x16_t vdst = vld1q_u8(dst + x * 2);
                uint8x16_t vsrc = vld1q_u8(src + x * 2);
                
                // Load 8 alpha values
                uint8x8_t alpha8 = vld1_u8(alpha + x);
                uint16x8_t valpha = vmovl_u8(alpha8);
                
                // Extract Y components (at positions 1,3,5,7,9,11,13,15)
                uint8x8_t y_indices = {1,3,5,7,9,11,13,15};
                uint8x8_t y_vid = vqtbl1_u8(vdst, y_indices);
                uint8x8_t y_ovr = vqtbl1_u8(vsrc, y_indices);
                
                // Extract U components (at positions 0,4,8,12)
                uint8x8_t u_indices = {0,4,8,12,0,0,0,0};
                uint8x8_t u_vid_temp = vqtbl1_u8(vdst, u_indices);
                uint8x8_t u_ovr_temp = vqtbl1_u8(vsrc, u_indices);
                
                // Extract V components (at positions 2,6,10,14)
                uint8x8_t v_indices = {2,6,10,14,0,0,0,0};
                uint8x8_t v_vid_temp = vqtbl1_u8(vdst, v_indices);
                uint8x8_t v_ovr_temp = vqtbl1_u8(vsrc, v_indices);
                
                // Convert to 16-bit for blending
                uint16x8_t y_vid16 = vmovl_u8(y_vid);
                uint16x8_t y_ovr16 = vmovl_u8(y_ovr);
                uint16x8_t inv_alpha = vsubq_u16(vdupq_n_u16(255), valpha);
                
                // Blend Y components using simple division by 255
                uint16x8_t y_temp = vmulq_u16(y_ovr16, valpha);
                y_temp = vmlaq_u16(y_temp, y_vid16, inv_alpha);
                
                // Simple division by 255: (x + (x >> 8)) >> 8
                uint16x8_t y_div = vaddq_u16(y_temp, vshrq_n_u16(y_temp, 8));
                uint16x8_t y_result = vshrq_n_u16(y_div, 8);
                
                // Calculate average alpha for U/V components (pairs of pixels)
                // Extract even/odd alpha values and average them
                uint16x4_t alpha_lo = vget_low_u16(valpha);
                uint16x4_t alpha_hi = vget_high_u16(valpha);
                // Get alpha for pixels 0,2,4,6
                uint16x4_t alpha_even = {vget_lane_u16(alpha_lo, 0), vget_lane_u16(alpha_lo, 2),
                                         vget_lane_u16(alpha_hi, 0), vget_lane_u16(alpha_hi, 2)};
                // Get alpha for pixels 1,3,5,7
                uint16x4_t alpha_odd = {vget_lane_u16(alpha_lo, 1), vget_lane_u16(alpha_lo, 3),
                                        vget_lane_u16(alpha_hi, 1), vget_lane_u16(alpha_hi, 3)};
                uint16x4_t alpha_avg = vhadd_u16(alpha_even, alpha_odd);
                uint16x4_t inv_alpha_avg = vsub_u16(vdup_n_u16(255), alpha_avg);
                
                // Convert U/V to 16-bit (only first 4 values are valid)
                uint16x8_t u_vid16 = vmovl_u8(u_vid_temp);
                uint16x8_t u_ovr16 = vmovl_u8(u_ovr_temp);
                uint16x8_t v_vid16 = vmovl_u8(v_vid_temp);
                uint16x8_t v_ovr16 = vmovl_u8(v_ovr_temp);
                
                // Blend U components (only first 4)
                uint16x4_t u_vid16_4 = vget_low_u16(u_vid16);
                uint16x4_t u_ovr16_4 = vget_low_u16(u_ovr16);
                uint16x4_t u_temp = vmul_u16(u_ovr16_4, alpha_avg);
                u_temp = vmla_u16(u_temp, u_vid16_4, inv_alpha_avg);
                
                // Simple division by 255: (x + (x >> 8)) >> 8
                uint16x4_t u_div = vadd_u16(u_temp, vshr_n_u16(u_temp, 8));
                uint16x4_t u_result = vshr_n_u16(u_div, 8);
                
                // Blend V components (only first 4)
                uint16x4_t v_vid16_4 = vget_low_u16(v_vid16);
                uint16x4_t v_ovr16_4 = vget_low_u16(v_ovr16);
                uint16x4_t v_temp = vmul_u16(v_ovr16_4, alpha_avg);
                v_temp = vmla_u16(v_temp, v_vid16_4, inv_alpha_avg);
                
                // Simple division by 255: (x + (x >> 8)) >> 8
                uint16x4_t v_div = vadd_u16(v_temp, vshr_n_u16(v_temp, 8));
                uint16x4_t v_result = vshr_n_u16(v_div, 8);
                
                // Pack results back to 8-bit
                uint8x8_t y_result8 = vqmovn_u16(y_result);
                uint8x8_t u_result8 = vqmovn_u16(vcombine_u16(u_result, u_result));
                uint8x8_t v_result8 = vqmovn_u16(vcombine_u16(v_result, v_result));
                
                // Reconstruct UYVY format by manually interleaving components
                uint8_t temp[16];
                temp[0] = vget_lane_u8(u_result8, 0);  // U0
                temp[1] = vget_lane_u8(y_result8, 0);  // Y0
                temp[2] = vget_lane_u8(v_result8, 0);  // V0
                temp[3] = vget_lane_u8(y_result8, 1);  // Y1
                temp[4] = vget_lane_u8(u_result8, 1);  // U1
                temp[5] = vget_lane_u8(y_result8, 2);  // Y2
                temp[6] = vget_lane_u8(v_result8, 1);  // V1
                temp[7] = vget_lane_u8(y_result8, 3);  // Y3
                temp[8] = vget_lane_u8(u_result8, 2);  // U2
                temp[9] = vget_lane_u8(y_result8, 4);  // Y4
                temp[10] = vget_lane_u8(v_result8, 2); // V2
                temp[11] = vget_lane_u8(y_result8, 5); // Y5
                temp[12] = vget_lane_u8(u_result8, 3); // U3
                temp[13] = vget_lane_u8(y_result8, 6); // Y6
                temp[14] = vget_lane_u8(v_result8, 3); // V3
                temp[15] = vget_lane_u8(y_result8, 7); // Y7
                
                vst1q_u8(dst + x * 2, vld1q_u8(temp));
        }
        
        // Handle remaining pixels with scalar code
        alpha_blend_uyvy_scalar(dst + x * 2, src + x * 2, alpha + x, width - x);
}
#endif

/**
 * Native UYVY alpha blending
 */
void alpha_blend_uyvy(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
#ifdef __AVX2__
        alpha_blend_uyvy_avx2(dst, src, alpha, width);
#elif defined(__SSE2__)
        alpha_blend_uyvy_sse2(dst, src, alpha, width);
#elif defined(__ARM_NEON)
        alpha_blend_uyvy_neon(dst, src, alpha, width);
#else
        alpha_blend_uyvy_scalar(dst, src, alpha, width);
#endif
}

/**
 * Scalar YUYV alpha blending
 */
static void alpha_blend_yuyv_scalar(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        // YUYV format: Y0 U0 Y1 V0 (covers 2 pixels)
        for (int x = 0; x < width; x += 2) {
                uint8_t alpha0 = alpha[x];
                uint8_t alpha1 = (x + 1 < width) ? alpha[x + 1] : alpha[x];
                
                // Get video components
                uint8_t y0_vid = dst[0];
                uint8_t u_vid = dst[1];
                uint8_t y1_vid = dst[2];
                uint8_t v_vid = dst[3];
                
                // Get overlay components
                uint8_t y0_ovr = src[0];
                uint8_t u_ovr = src[1];
                uint8_t y1_ovr = src[2];
                uint8_t v_ovr = src[3];
                
                // Blend luma (simple per-pixel)
                dst[0] = (y0_ovr * alpha0 + y0_vid * (255 - alpha0)) / 255;
                dst[2] = (y1_ovr * alpha1 + y1_vid * (255 - alpha1)) / 255;
                
                // Blend chroma (shared between 2 pixels - use average alpha)
                uint8_t avg_alpha = (alpha0 + alpha1) / 2;
                dst[1] = (u_ovr * avg_alpha + u_vid * (255 - avg_alpha)) / 255;
                dst[3] = (v_ovr * avg_alpha + v_vid * (255 - avg_alpha)) / 255;
                
                dst += 4;
                src += 4;
        }
}

#ifdef __SSE2__
/**
 * SSE2 optimized YUYV alpha blending - processes 8 pixels at once
 */
static void alpha_blend_yuyv_sse2(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        const __m128i zero = _mm_setzero_si128();
        const __m128i c255 = _mm_set1_epi16(255);
        
        int x = 0;
        
        // Process 8 pixels at a time (16 bytes = 4 YUYV groups)
        for (; x <= width - 8; x += 8) {
                // Load 16 bytes (8 pixels) of YUYV data
                __m128i vdst = _mm_loadu_si128((const __m128i*)(dst + x * 2));
                __m128i vsrc = _mm_loadu_si128((const __m128i*)(src + x * 2));
                
                // Load 8 alpha values and expand to 16-bit
                __m128i alpha8 = _mm_loadl_epi64((const __m128i*)(alpha + x));
                __m128i valpha = _mm_unpacklo_epi8(alpha8, zero);
                
                // Extract Y components (at positions 0,2,4,6,8,10,12,14)
                __m128i y_mask = _mm_set_epi8(0,14,0,12,0,10,0,8,0,6,0,4,0,2,0,0);
                __m128i y_vid = _mm_shuffle_epi8(vdst, y_mask);
                __m128i y_ovr = _mm_shuffle_epi8(vsrc, y_mask);
                
                // Extract U components (at positions 1,5,9,13)
                __m128i u_mask = _mm_set_epi8(0,0,0,0,0,13,0,9,0,5,0,1,0,0,0,0);
                __m128i u_vid = _mm_shuffle_epi8(vdst, u_mask);
                __m128i u_ovr = _mm_shuffle_epi8(vsrc, u_mask);
                
                // Extract V components (at positions 3,7,11,15)
                __m128i v_mask = _mm_set_epi8(0,0,0,0,0,15,0,11,0,7,0,3,0,0,0,0);
                __m128i v_vid = _mm_shuffle_epi8(vdst, v_mask);
                __m128i v_ovr = _mm_shuffle_epi8(vsrc, v_mask);
                
                // Convert to 16-bit for blending
                __m128i y_vid16 = _mm_unpacklo_epi8(y_vid, zero);
                __m128i y_ovr16 = _mm_unpacklo_epi8(y_ovr, zero);
                __m128i inv_alpha = _mm_sub_epi16(c255, valpha);
                
                // Blend Y components
                __m128i y_temp = _mm_mullo_epi16(y_ovr16, valpha);
                y_temp = _mm_add_epi16(y_temp, _mm_mullo_epi16(y_vid16, inv_alpha));
                // Simple division by 255: (x + (x >> 8)) >> 8
                __m128i y_div = _mm_add_epi16(y_temp, _mm_srli_epi16(y_temp, 8));
                __m128i y_result = _mm_srli_epi16(y_div, 8);
                
                // Calculate average alpha for U/V components (pairs of pixels)
                __m128i alpha_even = _mm_shufflelo_epi16(valpha, _MM_SHUFFLE(3,2,1,0));
                alpha_even = _mm_shufflehi_epi16(alpha_even, _MM_SHUFFLE(3,2,1,0));
                alpha_even = _mm_shuffle_epi32(alpha_even, _MM_SHUFFLE(3,1,2,0));
                
                __m128i alpha_odd = _mm_shufflelo_epi16(valpha, _MM_SHUFFLE(3,2,1,0));
                alpha_odd = _mm_shufflehi_epi16(alpha_odd, _MM_SHUFFLE(3,2,1,0));
                alpha_odd = _mm_shuffle_epi32(alpha_odd, _MM_SHUFFLE(2,0,3,1));
                
                __m128i alpha_avg = _mm_avg_epu16(alpha_even, alpha_odd);
                __m128i inv_alpha_avg = _mm_sub_epi16(c255, alpha_avg);
                
                // Blend U components
                __m128i u_vid16 = _mm_unpacklo_epi8(u_vid, zero);
                __m128i u_ovr16 = _mm_unpacklo_epi8(u_ovr, zero);
                __m128i u_temp = _mm_mullo_epi16(u_ovr16, alpha_avg);
                u_temp = _mm_add_epi16(u_temp, _mm_mullo_epi16(u_vid16, inv_alpha_avg));
                __m128i u_div = _mm_add_epi16(u_temp, _mm_srli_epi16(u_temp, 8));
                __m128i u_result = _mm_srli_epi16(u_div, 8);
                
                // Blend V components
                __m128i v_vid16 = _mm_unpacklo_epi8(v_vid, zero);
                __m128i v_ovr16 = _mm_unpacklo_epi8(v_ovr, zero);
                __m128i v_temp = _mm_mullo_epi16(v_ovr16, alpha_avg);
                v_temp = _mm_add_epi16(v_temp, _mm_mullo_epi16(v_vid16, inv_alpha_avg));
                __m128i v_div = _mm_add_epi16(v_temp, _mm_srli_epi16(v_temp, 8));
                __m128i v_result = _mm_srli_epi16(v_div, 8);
                
                // Pack results back to 8-bit
                __m128i y_result8 = _mm_packus_epi16(y_result, zero);
                __m128i u_result8 = _mm_packus_epi16(u_result, zero);
                __m128i v_result8 = _mm_packus_epi16(v_result, zero);
                
                // Reconstruct YUYV format by interleaving components
                // YUYV: Y0 U0 Y1 V0 Y2 U1 Y3 V1...
                __m128i result = _mm_setzero_si128();
                
                // Interleave Y, U, V values into YUYV pattern manually
                for (int i = 0; i < 4; i++) {
                        ((uint8_t*)&result)[i*4+0] = ((uint8_t*)&y_result8)[i*2];     // Y0,Y2,Y4,Y6
                        ((uint8_t*)&result)[i*4+1] = ((uint8_t*)&u_result8)[i];       // U0,U1,U2,U3
                        ((uint8_t*)&result)[i*4+2] = ((uint8_t*)&y_result8)[i*2+1];   // Y1,Y3,Y5,Y7
                        ((uint8_t*)&result)[i*4+3] = ((uint8_t*)&v_result8)[i];       // V0,V1,V2,V3
                }
                
                _mm_storeu_si128((__m128i*)(dst + x * 2), result);
        }
        
        // Handle remaining pixels with scalar code
        alpha_blend_yuyv_scalar(dst + x * 2, src + x * 2, alpha + x, width - x);
}
#endif

#ifdef __AVX2__
/**
 * AVX2 optimized YUYV alpha blending - processes 16 pixels at once
 */
static void alpha_blend_yuyv_avx2(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        const __m256i zero = _mm256_setzero_si256();
        const __m256i c255 = _mm256_set1_epi16(255);
        
        int x = 0;
        
        // Process 16 pixels at a time (32 bytes = 8 YUYV groups)
        for (; x <= width - 16; x += 16) {
                // Load 32 bytes (16 pixels) of YUYV data
                __m256i vdst = _mm256_loadu_si256((const __m256i*)(dst + x * 2));
                __m256i vsrc = _mm256_loadu_si256((const __m256i*)(src + x * 2));
                
                // Load 16 alpha values
                __m128i alpha8 = _mm_loadu_si128((const __m128i*)(alpha + x));
                __m256i valpha = _mm256_unpacklo_epi8(_mm256_castsi128_si256(alpha8), zero);
                valpha = _mm256_permute4x64_epi64(valpha, _MM_SHUFFLE(1,1,0,0));
                
                // Extract Y components using similar approach as SSE2 but with AVX2
                // This is complex - for now fall back to SSE2 for remaining pixels
                alpha_blend_yuyv_sse2(dst + x * 2, src + x * 2, alpha + x, width - x);
                break;
        }
        
        // Handle remaining pixels
#ifdef __SSE2__
        alpha_blend_yuyv_sse2(dst + x * 2, src + x * 2, alpha + x, width - x);
#else
        alpha_blend_yuyv_scalar(dst + x * 2, src + x * 2, alpha + x, width - x);
#endif
}
#endif

#ifdef __ARM_NEON
/**
 * ARM NEON optimized YUYV alpha blending
 */
static void alpha_blend_yuyv_neon(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        int x = 0;
        
        // Process 8 pixels at a time (16 bytes = 4 YUYV groups)
        for (; x <= width - 8; x += 8) {
                // Load 16 bytes (8 pixels) of YUYV data
                uint8x16_t vdst = vld1q_u8(dst + x * 2);
                uint8x16_t vsrc = vld1q_u8(src + x * 2);
                
                // Load 8 alpha values
                uint8x8_t alpha8 = vld1_u8(alpha + x);
                uint16x8_t valpha = vmovl_u8(alpha8);
                
                // Extract Y components (at positions 0,2,4,6,8,10,12,14)
                uint8x8_t y_indices = {0,2,4,6,8,10,12,14};
                uint8x8_t y_vid = vqtbl1_u8(vdst, y_indices);
                uint8x8_t y_ovr = vqtbl1_u8(vsrc, y_indices);
                
                // Extract U components (at positions 1,5,9,13)
                uint8x8_t u_indices = {1,5,9,13,0,0,0,0};
                uint8x8_t u_vid_temp = vqtbl1_u8(vdst, u_indices);
                uint8x8_t u_ovr_temp = vqtbl1_u8(vsrc, u_indices);
                
                // Extract V components (at positions 3,7,11,15)
                uint8x8_t v_indices = {3,7,11,15,0,0,0,0};
                uint8x8_t v_vid_temp = vqtbl1_u8(vdst, v_indices);
                uint8x8_t v_ovr_temp = vqtbl1_u8(vsrc, v_indices);
                
                // Convert to 16-bit for blending
                uint16x8_t y_vid16 = vmovl_u8(y_vid);
                uint16x8_t y_ovr16 = vmovl_u8(y_ovr);
                uint16x8_t inv_alpha = vsubq_u16(vdupq_n_u16(255), valpha);
                
                // Blend Y components using simple division by 255
                uint16x8_t y_temp = vmulq_u16(y_ovr16, valpha);
                y_temp = vmlaq_u16(y_temp, y_vid16, inv_alpha);
                
                // Simple division by 255: (x + (x >> 8)) >> 8
                uint16x8_t y_div = vaddq_u16(y_temp, vshrq_n_u16(y_temp, 8));
                uint16x8_t y_result = vshrq_n_u16(y_div, 8);
                
                // Calculate average alpha for U/V components (pairs of pixels)
                uint16x4_t alpha_lo = vget_low_u16(valpha);
                uint16x4_t alpha_hi = vget_high_u16(valpha);
                // Get alpha for pixels 0,2,4,6
                uint16x4_t alpha_even = {vget_lane_u16(alpha_lo, 0), vget_lane_u16(alpha_lo, 2),
                                         vget_lane_u16(alpha_hi, 0), vget_lane_u16(alpha_hi, 2)};
                // Get alpha for pixels 1,3,5,7
                uint16x4_t alpha_odd = {vget_lane_u16(alpha_lo, 1), vget_lane_u16(alpha_lo, 3),
                                        vget_lane_u16(alpha_hi, 1), vget_lane_u16(alpha_hi, 3)};
                uint16x4_t alpha_avg = vhadd_u16(alpha_even, alpha_odd);
                uint16x4_t inv_alpha_avg = vsub_u16(vdup_n_u16(255), alpha_avg);
                
                // Convert U/V to 16-bit (only first 4 values are valid)
                uint16x8_t u_vid16 = vmovl_u8(u_vid_temp);
                uint16x8_t u_ovr16 = vmovl_u8(u_ovr_temp);
                uint16x8_t v_vid16 = vmovl_u8(v_vid_temp);
                uint16x8_t v_ovr16 = vmovl_u8(v_ovr_temp);
                
                // Blend U components (only first 4)
                uint16x4_t u_vid16_4 = vget_low_u16(u_vid16);
                uint16x4_t u_ovr16_4 = vget_low_u16(u_ovr16);
                uint16x4_t u_temp = vmul_u16(u_ovr16_4, alpha_avg);
                u_temp = vmla_u16(u_temp, u_vid16_4, inv_alpha_avg);
                
                // Simple division by 255: (x + (x >> 8)) >> 8
                uint16x4_t u_div = vadd_u16(u_temp, vshr_n_u16(u_temp, 8));
                uint16x4_t u_result = vshr_n_u16(u_div, 8);
                
                // Blend V components (only first 4)
                uint16x4_t v_vid16_4 = vget_low_u16(v_vid16);
                uint16x4_t v_ovr16_4 = vget_low_u16(v_ovr16);
                uint16x4_t v_temp = vmul_u16(v_ovr16_4, alpha_avg);
                v_temp = vmla_u16(v_temp, v_vid16_4, inv_alpha_avg);
                
                // Simple division by 255: (x + (x >> 8)) >> 8
                uint16x4_t v_div = vadd_u16(v_temp, vshr_n_u16(v_temp, 8));
                uint16x4_t v_result = vshr_n_u16(v_div, 8);
                
                // Pack results back to 8-bit
                uint8x8_t y_result8 = vqmovn_u16(y_result);
                uint8x8_t u_result8 = vqmovn_u16(vcombine_u16(u_result, u_result));
                uint8x8_t v_result8 = vqmovn_u16(vcombine_u16(v_result, v_result));
                
                // Reconstruct YUYV format by manually interleaving components
                uint8_t temp[16];
                temp[0] = vget_lane_u8(y_result8, 0);  // Y0
                temp[1] = vget_lane_u8(u_result8, 0);  // U0
                temp[2] = vget_lane_u8(y_result8, 1);  // Y1
                temp[3] = vget_lane_u8(v_result8, 0);  // V0
                temp[4] = vget_lane_u8(y_result8, 2);  // Y2
                temp[5] = vget_lane_u8(u_result8, 1);  // U1
                temp[6] = vget_lane_u8(y_result8, 3);  // Y3
                temp[7] = vget_lane_u8(v_result8, 1);  // V1
                temp[8] = vget_lane_u8(y_result8, 4);  // Y4
                temp[9] = vget_lane_u8(u_result8, 2);  // U2
                temp[10] = vget_lane_u8(y_result8, 5); // Y5
                temp[11] = vget_lane_u8(v_result8, 2); // V2
                temp[12] = vget_lane_u8(y_result8, 6); // Y6
                temp[13] = vget_lane_u8(u_result8, 3); // U3
                temp[14] = vget_lane_u8(y_result8, 7); // Y7
                temp[15] = vget_lane_u8(v_result8, 3); // V3
                
                vst1q_u8(dst + x * 2, vld1q_u8(temp));
        }
        
        // Handle remaining pixels with scalar code
        alpha_blend_yuyv_scalar(dst + x * 2, src + x * 2, alpha + x, width - x);
}
#endif

/**
 * Native YUYV alpha blending
 */
void alpha_blend_yuyv(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
#ifdef __AVX2__
        alpha_blend_yuyv_avx2(dst, src, alpha, width);
#elif defined(__SSE2__)
        alpha_blend_yuyv_sse2(dst, src, alpha, width);
#elif defined(__ARM_NEON)
        alpha_blend_yuyv_neon(dst, src, alpha, width);
#else
        alpha_blend_yuyv_scalar(dst, src, alpha, width);
#endif
}

/**
 * Scalar RGB alpha blending
 */
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

#ifdef __SSE2__
/**
 * SSE2 optimized RGB alpha blending - processes 4 pixels at once
 * RGB is 3 bytes per pixel, so 4 pixels = 12 bytes
 */
static void alpha_blend_rgb_sse2(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        const __m128i zero = _mm_setzero_si128();
        const __m128i c255 = _mm_set1_epi16(255);
        
        int x = 0;
        
        // Process 4 pixels at a time (12 bytes each)
        for (; x <= width - 4; x += 4) {
                // Load 12 bytes (4 RGB pixels)
                __m128i src_rgb = _mm_loadu_si128((const __m128i*)(src + x * 3));
                __m128i dst_rgb = _mm_loadu_si128((const __m128i*)(dst + x * 3));
                
                // Load 4 alpha values and expand to 16-bit
                uint32_t alpha4 = *(uint32_t*)(alpha + x);
                __m128i alpha_8 = _mm_cvtsi32_si128(alpha4);
                alpha_8 = _mm_unpacklo_epi8(alpha_8, zero);
                
                // Duplicate each alpha for RGB components: a0 a0 a0 a1 a1 a1 a2 a2
                __m128i alpha_dup = _mm_shufflelo_epi16(alpha_8, _MM_SHUFFLE(1,1,0,0));
                alpha_dup = _mm_shufflehi_epi16(alpha_dup, _MM_SHUFFLE(3,2,2,1));
                alpha_dup = _mm_shuffle_epi32(alpha_dup, _MM_SHUFFLE(2,1,0,0));
                
                // Another shuffle to get: a0 a0 a0 a1 a1 a1 a2 a2
                __m128i alpha_rgb = _mm_set_epi16(
                        ((alpha4 >> 8) & 0xFF), ((alpha4 >> 8) & 0xFF), ((alpha4 >> 8) & 0xFF), // a1 a1 a1
                        (alpha4 & 0xFF), (alpha4 & 0xFF), (alpha4 & 0xFF),                      // a0 a0 a0
                        ((alpha4 >> 16) & 0xFF), ((alpha4 >> 16) & 0xFF)                       // a2 a2
                );
                
                // Split RGB data into two 8x16 vectors for processing
                __m128i src_lo = _mm_unpacklo_epi8(src_rgb, zero);
                __m128i src_hi = _mm_unpackhi_epi8(src_rgb, zero);
                __m128i dst_lo = _mm_unpacklo_epi8(dst_rgb, zero);
                __m128i dst_hi = _mm_unpackhi_epi8(dst_rgb, zero);
                
                // Calculate inverse alpha
                __m128i inv_alpha_rgb = _mm_sub_epi16(c255, alpha_rgb);
                
                // Blend low part
                __m128i temp_lo = _mm_mullo_epi16(src_lo, alpha_rgb);
                temp_lo = _mm_add_epi16(temp_lo, _mm_mullo_epi16(dst_lo, inv_alpha_rgb));
                __m128i div_lo = _mm_add_epi16(temp_lo, _mm_srli_epi16(temp_lo, 8));
                __m128i result_lo = _mm_srli_epi16(div_lo, 8);
                
                // For high part, we need the remaining alphas
                __m128i alpha_hi = _mm_set_epi16(
                        ((alpha4 >> 24) & 0xFF), ((alpha4 >> 24) & 0xFF), // a3 a3
                        ((alpha4 >> 16) & 0xFF), ((alpha4 >> 16) & 0xFF), // a2 a2
                        0, 0, 0, 0
                );
                __m128i inv_alpha_hi = _mm_sub_epi16(c255, alpha_hi);
                
                // Blend high part  
                __m128i temp_hi = _mm_mullo_epi16(src_hi, alpha_hi);
                temp_hi = _mm_add_epi16(temp_hi, _mm_mullo_epi16(dst_hi, inv_alpha_hi));
                __m128i div_hi = _mm_add_epi16(temp_hi, _mm_srli_epi16(temp_hi, 8));
                __m128i result_hi = _mm_srli_epi16(div_hi, 8);
                
                // Pack back to 8-bit and store
                __m128i result = _mm_packus_epi16(result_lo, result_hi);
                _mm_storeu_si128((__m128i*)(dst + x * 3), result);
        }
        
        // Handle remaining pixels with scalar code
        alpha_blend_rgb_scalar(dst + x * 3, src + x * 3, alpha + x, width - x);
}
#endif

#ifdef __AVX2__
/**
 * AVX2 optimized RGB alpha blending - processes 8 pixels at once  
 * RGB is 3 bytes per pixel, so 8 pixels = 24 bytes
 */
static void alpha_blend_rgb_avx2(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        // For now, fall back to SSE2 since RGB packing is complex with AVX2
        int x = 0;
        
        // Process larger chunks with SSE2
        for (; x <= width - 8; x += 8) {
#ifdef __SSE2__
                alpha_blend_rgb_sse2(dst + x * 3, src + x * 3, alpha + x, (width - x > 8) ? 8 : (width - x));
#else
                alpha_blend_rgb_scalar(dst + x * 3, src + x * 3, alpha + x, (width - x > 8) ? 8 : (width - x));
#endif
        }
        
        // Handle remaining pixels
#ifdef __SSE2__
        alpha_blend_rgb_sse2(dst + x * 3, src + x * 3, alpha + x, width - x);
#else
        alpha_blend_rgb_scalar(dst + x * 3, src + x * 3, alpha + x, width - x);
#endif
}
#endif

#ifdef __ARM_NEON
/**
 * ARM NEON optimized RGB alpha blending
 */
static void alpha_blend_rgb_neon(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        int x = 0;
        
        // Process 4 pixels at a time (12 bytes)
        for (; x <= width - 4; x += 4) {
                // Load 12 bytes (4 RGB pixels) into two 8-byte vectors
                uint8x8_t src_low = vld1_u8(src + x * 3);
                uint8x8_t src_high = vld1_u8(src + x * 3 + 8);
                uint8x8_t dst_low = vld1_u8(dst + x * 3);
                uint8x8_t dst_high = vld1_u8(dst + x * 3 + 8);
                
                // Load 4 alpha values
                uint32_t alpha4 = *(uint32_t*)(alpha + x);
                uint8x8_t alpha_values = vdup_n_u8(0);
                alpha_values = vset_lane_u8(alpha4 & 0xFF, alpha_values, 0);
                alpha_values = vset_lane_u8((alpha4 >> 8) & 0xFF, alpha_values, 1);
                alpha_values = vset_lane_u8((alpha4 >> 16) & 0xFF, alpha_values, 2);
                alpha_values = vset_lane_u8((alpha4 >> 24) & 0xFF, alpha_values, 3);
                
                // Duplicate alphas for RGB: a0 a0 a0 a1 a1 a1 a2 a2
                uint8x8_t alpha_rgb_low = vdup_n_u8(0);
                alpha_rgb_low = vset_lane_u8(vget_lane_u8(alpha_values, 0), alpha_rgb_low, 0); // R0
                alpha_rgb_low = vset_lane_u8(vget_lane_u8(alpha_values, 0), alpha_rgb_low, 1); // G0
                alpha_rgb_low = vset_lane_u8(vget_lane_u8(alpha_values, 0), alpha_rgb_low, 2); // B0
                alpha_rgb_low = vset_lane_u8(vget_lane_u8(alpha_values, 1), alpha_rgb_low, 3); // R1
                alpha_rgb_low = vset_lane_u8(vget_lane_u8(alpha_values, 1), alpha_rgb_low, 4); // G1
                alpha_rgb_low = vset_lane_u8(vget_lane_u8(alpha_values, 1), alpha_rgb_low, 5); // B1
                alpha_rgb_low = vset_lane_u8(vget_lane_u8(alpha_values, 2), alpha_rgb_low, 6); // R2
                alpha_rgb_low = vset_lane_u8(vget_lane_u8(alpha_values, 2), alpha_rgb_low, 7); // G2
                
                // For high part: B2 R3 G3 B3 (4 remaining bytes)
                uint8x8_t alpha_rgb_high = vdup_n_u8(0);
                alpha_rgb_high = vset_lane_u8(vget_lane_u8(alpha_values, 2), alpha_rgb_high, 0); // B2
                alpha_rgb_high = vset_lane_u8(vget_lane_u8(alpha_values, 3), alpha_rgb_high, 1); // R3
                alpha_rgb_high = vset_lane_u8(vget_lane_u8(alpha_values, 3), alpha_rgb_high, 2); // G3
                alpha_rgb_high = vset_lane_u8(vget_lane_u8(alpha_values, 3), alpha_rgb_high, 3); // B3
                
                // Convert to 16-bit for blending
                uint16x8_t src_low16 = vmovl_u8(src_low);
                uint16x8_t dst_low16 = vmovl_u8(dst_low);
                uint16x8_t alpha_low16 = vmovl_u8(alpha_rgb_low);
                uint16x8_t inv_alpha_low16 = vsubq_u16(vdupq_n_u16(255), alpha_low16);
                
                // Blend low part
                uint16x8_t temp_low = vmulq_u16(src_low16, alpha_low16);
                temp_low = vmlaq_u16(temp_low, dst_low16, inv_alpha_low16);
                uint16x8_t div_low = vaddq_u16(temp_low, vshrq_n_u16(temp_low, 8));
                uint16x8_t result_low16 = vshrq_n_u16(div_low, 8);
                
                // Convert high part (only first 4 bytes are valid)
                uint16x8_t src_high16 = vmovl_u8(src_high);
                uint16x8_t dst_high16 = vmovl_u8(dst_high);
                uint16x8_t alpha_high16 = vmovl_u8(alpha_rgb_high);
                uint16x8_t inv_alpha_high16 = vsubq_u16(vdupq_n_u16(255), alpha_high16);
                
                // Blend high part
                uint16x8_t temp_high = vmulq_u16(src_high16, alpha_high16);
                temp_high = vmlaq_u16(temp_high, dst_high16, inv_alpha_high16);
                uint16x8_t div_high = vaddq_u16(temp_high, vshrq_n_u16(temp_high, 8));
                uint16x8_t result_high16 = vshrq_n_u16(div_high, 8);
                
                // Pack back to 8-bit and store
                uint8x8_t result_low = vqmovn_u16(result_low16);
                uint8x8_t result_high = vqmovn_u16(result_high16);
                
                vst1_u8(dst + x * 3, result_low);
                vst1_u8(dst + x * 3 + 8, result_high);
        }
        
        // Handle remaining pixels with scalar code
        alpha_blend_rgb_scalar(dst + x * 3, src + x * 3, alpha + x, width - x);
}
#endif

/**
 * Native RGB alpha blending
 */
void alpha_blend_rgb(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
#ifdef __AVX2__
        alpha_blend_rgb_avx2(dst, src, alpha, width);
#elif defined(__SSE2__)
        alpha_blend_rgb_sse2(dst, src, alpha, width);
#elif defined(__ARM_NEON)
        alpha_blend_rgb_neon(dst, src, alpha, width);
#else
        alpha_blend_rgb_scalar(dst, src, alpha, width);
#endif
}

/**
 * Native v210 alpha blending (10-bit YUV 4:2:2)
 * v210 packs 6 pixels (12 samples) into 16 bytes
 */
void alpha_blend_v210(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        // Process in groups of 6 pixels (16 bytes)
        for (int x = 0; x < width; x += 6) {
                uint32_t *dst_words = (uint32_t *)dst;
                const uint32_t *src_words = (const uint32_t *)src;
                
                // Extract 10-bit values from packed format
                // Word 0: Cb0 Y0 Cr0 (each 10 bits + 2 padding)
                uint32_t word0_dst = dst_words[0];
                uint32_t word0_src = src_words[0];
                
                uint16_t cb0_dst = (word0_dst >> 0) & 0x3FF;
                uint16_t y0_dst = (word0_dst >> 10) & 0x3FF;
                uint16_t cr0_dst = (word0_dst >> 20) & 0x3FF;
                
                uint16_t cb0_src = (word0_src >> 0) & 0x3FF;
                uint16_t y0_src = (word0_src >> 10) & 0x3FF;
                uint16_t cr0_src = (word0_src >> 20) & 0x3FF;
                
                // Blend Y0 with its alpha
                uint8_t a0 = alpha[x];
                y0_dst = (y0_src * a0 + y0_dst * (255 - a0)) / 255;
                
                // Word 1: Y1 Cb2 Y2 (each 10 bits + 2 padding)
                uint32_t word1_dst = dst_words[1];
                uint32_t word1_src = src_words[1];
                
                uint16_t y1_dst = (word1_dst >> 0) & 0x3FF;
                uint16_t cb2_dst = (word1_dst >> 10) & 0x3FF;
                uint16_t y2_dst = (word1_dst >> 20) & 0x3FF;
                
                uint16_t y1_src = (word1_src >> 0) & 0x3FF;
                uint16_t cb2_src = (word1_src >> 10) & 0x3FF;
                uint16_t y2_src = (word1_src >> 20) & 0x3FF;
                
                // Blend Y1 and Y2
                uint8_t a1 = (x + 1 < width) ? alpha[x + 1] : a0;
                uint8_t a2 = (x + 2 < width) ? alpha[x + 2] : a1;
                y1_dst = (y1_src * a1 + y1_dst * (255 - a1)) / 255;
                y2_dst = (y2_src * a2 + y2_dst * (255 - a2)) / 255;
                
                // Word 2: Cr2 Y3 Cb4 (each 10 bits + 2 padding)
                uint32_t word2_dst = dst_words[2];
                uint32_t word2_src = src_words[2];
                
                uint16_t cr2_dst = (word2_dst >> 0) & 0x3FF;
                uint16_t y3_dst = (word2_dst >> 10) & 0x3FF;
                uint16_t cb4_dst = (word2_dst >> 20) & 0x3FF;
                
                uint16_t cr2_src = (word2_src >> 0) & 0x3FF;
                uint16_t y3_src = (word2_src >> 10) & 0x3FF;
                uint16_t cb4_src = (word2_src >> 20) & 0x3FF;
                
                // Blend Y3
                uint8_t a3 = (x + 3 < width) ? alpha[x + 3] : a2;
                y3_dst = (y3_src * a3 + y3_dst * (255 - a3)) / 255;
                
                // Word 3: Y4 Cr4 Y5 (each 10 bits + 2 padding)
                uint32_t word3_dst = dst_words[3];
                uint32_t word3_src = src_words[3];
                
                uint16_t y4_dst = (word3_dst >> 0) & 0x3FF;
                uint16_t cr4_dst = (word3_dst >> 10) & 0x3FF;
                uint16_t y5_dst = (word3_dst >> 20) & 0x3FF;
                
                uint16_t y4_src = (word3_src >> 0) & 0x3FF;
                uint16_t cr4_src = (word3_src >> 10) & 0x3FF;
                uint16_t y5_src = (word3_src >> 20) & 0x3FF;
                
                // Blend Y4 and Y5
                uint8_t a4 = (x + 4 < width) ? alpha[x + 4] : a3;
                uint8_t a5 = (x + 5 < width) ? alpha[x + 5] : a4;
                y4_dst = (y4_src * a4 + y4_dst * (255 - a4)) / 255;
                y5_dst = (y5_src * a5 + y5_dst * (255 - a5)) / 255;
                
                // Blend chroma using average alpha for pixel pairs
                uint8_t avg_alpha_01 = (a0 + a1) / 2;
                uint8_t avg_alpha_23 = (a2 + a3) / 2;
                uint8_t avg_alpha_45 = (a4 + a5) / 2;
                
                cb0_dst = (cb0_src * avg_alpha_01 + cb0_dst * (255 - avg_alpha_01)) / 255;
                cr0_dst = (cr0_src * avg_alpha_01 + cr0_dst * (255 - avg_alpha_01)) / 255;
                cb2_dst = (cb2_src * avg_alpha_23 + cb2_dst * (255 - avg_alpha_23)) / 255;
                cr2_dst = (cr2_src * avg_alpha_23 + cr2_dst * (255 - avg_alpha_23)) / 255;
                cb4_dst = (cb4_src * avg_alpha_45 + cb4_dst * (255 - avg_alpha_45)) / 255;
                cr4_dst = (cr4_src * avg_alpha_45 + cr4_dst * (255 - avg_alpha_45)) / 255;
                
                // Pack back into v210 format
                dst_words[0] = (cb0_dst & 0x3FF) | ((y0_dst & 0x3FF) << 10) | ((cr0_dst & 0x3FF) << 20);
                dst_words[1] = (y1_dst & 0x3FF) | ((cb2_dst & 0x3FF) << 10) | ((y2_dst & 0x3FF) << 20);
                dst_words[2] = (cr2_dst & 0x3FF) | ((y3_dst & 0x3FF) << 10) | ((cb4_dst & 0x3FF) << 20);
                dst_words[3] = (y4_dst & 0x3FF) | ((cr4_dst & 0x3FF) << 10) | ((y5_dst & 0x3FF) << 20);
                
                dst += 16;
                src += 16;
        }
}

/**
 * Native R10k alpha blending (10-bit RGB)
 * R10k uses 4 bytes per pixel with 10 bits per component
 */
void alpha_blend_r10k(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
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
                
                // Blend with 10-bit precision
                r_dst = (r_src * a + r_dst * (255 - a)) / 255;
                g_dst = (g_src * a + g_dst * (255 - a)) / 255;
                b_dst = (b_src * a + b_dst * (255 - a)) / 255;
                
                // Pack back with padding in bits 30-31
                *(uint32_t *)dst = ((r_dst & 0x3FF) << 20) | 
                                   ((g_dst & 0x3FF) << 10) | 
                                   (b_dst & 0x3FF);
                
                dst += 4;
                src += 4;
        }
}

/**
 * Native R12L alpha blending (12-bit RGB little-endian)
 * R12L packs 8 pixels (24 RGB values) into 36 bytes
 */
void alpha_blend_r12l(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        // Define BYTE_SWAP based on endianness
#ifdef WORDS_BIGENDIAN
#define BYTE_SWAP(x) (3 - x)
#else
#define BYTE_SWAP(x) x
#endif

        // Process in groups of 8 pixels (36 bytes)
        for (int x = 0; x < width; x += 8) {
                int pixels_to_process = (width - x) > 8 ? 8 : (width - x);
                
                // Extract all 8 pixels first
                uint16_t r[8], g[8], b[8];
                uint16_t r_src[8], g_src[8], b_src[8];
                
                // Extract 12-bit values from destination
                // Pixel 0
                r[0] = (dst[BYTE_SWAP(1)] & 0xF) << 8 | dst[BYTE_SWAP(0)];
                g[0] = dst[BYTE_SWAP(2)] << 4 | (dst[BYTE_SWAP(1)] >> 4);
                b[0] = (dst[4 + BYTE_SWAP(0)] & 0xF) << 8 | dst[BYTE_SWAP(3)];
                
                r_src[0] = (src[BYTE_SWAP(1)] & 0xF) << 8 | src[BYTE_SWAP(0)];
                g_src[0] = src[BYTE_SWAP(2)] << 4 | (src[BYTE_SWAP(1)] >> 4);
                b_src[0] = (src[4 + BYTE_SWAP(0)] & 0xF) << 8 | src[BYTE_SWAP(3)];
                
                // Pixel 1
                r[1] = dst[4 + BYTE_SWAP(1)] << 4 | (dst[4 + BYTE_SWAP(0)] >> 4);
                g[1] = (dst[4 + BYTE_SWAP(3)] & 0xF) << 8 | dst[4 + BYTE_SWAP(2)];
                b[1] = dst[8 + BYTE_SWAP(0)] << 4 | (dst[4 + BYTE_SWAP(3)] >> 4);
                
                r_src[1] = src[4 + BYTE_SWAP(1)] << 4 | (src[4 + BYTE_SWAP(0)] >> 4);
                g_src[1] = (src[4 + BYTE_SWAP(3)] & 0xF) << 8 | src[4 + BYTE_SWAP(2)];
                b_src[1] = src[8 + BYTE_SWAP(0)] << 4 | (src[4 + BYTE_SWAP(3)] >> 4);
                
                // Pixel 2
                r[2] = (dst[8 + BYTE_SWAP(2)] & 0xF) << 8 | dst[8 + BYTE_SWAP(1)];
                g[2] = dst[8 + BYTE_SWAP(3)] << 4 | (dst[8 + BYTE_SWAP(2)] >> 4);
                b[2] = (dst[12 + BYTE_SWAP(1)] & 0xF) << 8 | dst[12 + BYTE_SWAP(0)];
                
                r_src[2] = (src[8 + BYTE_SWAP(2)] & 0xF) << 8 | src[8 + BYTE_SWAP(1)];
                g_src[2] = src[8 + BYTE_SWAP(3)] << 4 | (src[8 + BYTE_SWAP(2)] >> 4);
                b_src[2] = (src[12 + BYTE_SWAP(1)] & 0xF) << 8 | src[12 + BYTE_SWAP(0)];
                
                // Pixel 3
                r[3] = dst[12 + BYTE_SWAP(2)] << 4 | (dst[12 + BYTE_SWAP(1)] >> 4);
                g[3] = (dst[16 + BYTE_SWAP(0)] & 0xF) << 8 | dst[12 + BYTE_SWAP(3)];
                b[3] = dst[16 + BYTE_SWAP(1)] << 4 | (dst[16 + BYTE_SWAP(0)] >> 4);
                
                r_src[3] = src[12 + BYTE_SWAP(2)] << 4 | (src[12 + BYTE_SWAP(1)] >> 4);
                g_src[3] = (src[16 + BYTE_SWAP(0)] & 0xF) << 8 | src[12 + BYTE_SWAP(3)];
                b_src[3] = src[16 + BYTE_SWAP(1)] << 4 | (src[16 + BYTE_SWAP(0)] >> 4);
                
                // Pixel 4
                r[4] = (dst[16 + BYTE_SWAP(3)] & 0xF) << 8 | dst[16 + BYTE_SWAP(2)];
                g[4] = dst[20 + BYTE_SWAP(0)] << 4 | (dst[16 + BYTE_SWAP(3)] >> 4);
                b[4] = (dst[20 + BYTE_SWAP(2)] & 0xF) << 8 | dst[20 + BYTE_SWAP(1)];
                
                r_src[4] = (src[16 + BYTE_SWAP(3)] & 0xF) << 8 | src[16 + BYTE_SWAP(2)];
                g_src[4] = src[20 + BYTE_SWAP(0)] << 4 | (src[16 + BYTE_SWAP(3)] >> 4);
                b_src[4] = (src[20 + BYTE_SWAP(2)] & 0xF) << 8 | src[20 + BYTE_SWAP(1)];
                
                // Pixel 5
                r[5] = dst[20 + BYTE_SWAP(3)] << 4 | (dst[20 + BYTE_SWAP(2)] >> 4);
                g[5] = (dst[24 + BYTE_SWAP(1)] & 0xF) << 8 | dst[24 + BYTE_SWAP(0)];
                b[5] = dst[24 + BYTE_SWAP(2)] << 4 | (dst[24 + BYTE_SWAP(1)] >> 4);
                
                r_src[5] = src[20 + BYTE_SWAP(3)] << 4 | (src[20 + BYTE_SWAP(2)] >> 4);
                g_src[5] = (src[24 + BYTE_SWAP(1)] & 0xF) << 8 | src[24 + BYTE_SWAP(0)];
                b_src[5] = src[24 + BYTE_SWAP(2)] << 4 | (src[24 + BYTE_SWAP(1)] >> 4);
                
                // Pixel 6
                r[6] = (dst[28 + BYTE_SWAP(0)] & 0xF) << 8 | dst[24 + BYTE_SWAP(3)];
                g[6] = dst[28 + BYTE_SWAP(1)] << 4 | (dst[28 + BYTE_SWAP(0)] >> 4);
                b[6] = (dst[28 + BYTE_SWAP(3)] & 0xF) << 8 | dst[28 + BYTE_SWAP(2)];
                
                r_src[6] = (src[28 + BYTE_SWAP(0)] & 0xF) << 8 | src[24 + BYTE_SWAP(3)];
                g_src[6] = src[28 + BYTE_SWAP(1)] << 4 | (src[28 + BYTE_SWAP(0)] >> 4);
                b_src[6] = (src[28 + BYTE_SWAP(3)] & 0xF) << 8 | src[28 + BYTE_SWAP(2)];
                
                // Pixel 7
                r[7] = dst[32 + BYTE_SWAP(0)] << 4 | (dst[28 + BYTE_SWAP(3)] >> 4);
                g[7] = (dst[32 + BYTE_SWAP(2)] & 0xF) << 8 | dst[32 + BYTE_SWAP(1)];
                b[7] = dst[32 + BYTE_SWAP(3)] << 4 | (dst[32 + BYTE_SWAP(2)] >> 4);
                
                r_src[7] = src[32 + BYTE_SWAP(0)] << 4 | (src[28 + BYTE_SWAP(3)] >> 4);
                g_src[7] = (src[32 + BYTE_SWAP(2)] & 0xF) << 8 | src[32 + BYTE_SWAP(1)];
                b_src[7] = src[32 + BYTE_SWAP(3)] << 4 | (src[32 + BYTE_SWAP(2)] >> 4);
                
                // Blend all pixels
                for (int i = 0; i < pixels_to_process; i++) {
                        uint8_t a = alpha[x + i];
                        
                        // Blend with 12-bit precision (scale alpha to 12-bit range)
                        uint16_t a12 = (a << 4) | (a >> 4);  // Replicate to 12 bits
                        uint16_t inv_a12 = 0xFFF - a12;
                        
                        r[i] = ((uint32_t)r_src[i] * a12 + (uint32_t)r[i] * inv_a12) / 0xFFF;
                        g[i] = ((uint32_t)g_src[i] * a12 + (uint32_t)g[i] * inv_a12) / 0xFFF;
                        b[i] = ((uint32_t)b_src[i] * a12 + (uint32_t)b[i] * inv_a12) / 0xFFF;
                        
                        // Clamp to 12-bit range
                        r[i] = r[i] > 0xFFF ? 0xFFF : r[i];
                        g[i] = g[i] > 0xFFF ? 0xFFF : g[i];
                        b[i] = b[i] > 0xFFF ? 0xFFF : b[i];
                }
                
                // Pack pixels back into R12L format
                // Pixel 0
                dst[BYTE_SWAP(0)] = r[0] & 0xFF;
                dst[BYTE_SWAP(1)] = ((r[0] >> 8) & 0xF) | ((g[0] << 4) & 0xF0);
                dst[BYTE_SWAP(2)] = g[0] >> 4;
                dst[BYTE_SWAP(3)] = b[0] & 0xFF;
                dst[4 + BYTE_SWAP(0)] = ((b[0] >> 8) & 0xF) | ((r[1] << 4) & 0xF0);
                
                // Pixel 1
                dst[4 + BYTE_SWAP(1)] = r[1] >> 4;
                dst[4 + BYTE_SWAP(2)] = g[1] & 0xFF;
                dst[4 + BYTE_SWAP(3)] = ((g[1] >> 8) & 0xF) | ((b[1] << 4) & 0xF0);
                dst[8 + BYTE_SWAP(0)] = b[1] >> 4;
                
                // Pixel 2
                dst[8 + BYTE_SWAP(1)] = r[2] & 0xFF;
                dst[8 + BYTE_SWAP(2)] = ((r[2] >> 8) & 0xF) | ((g[2] << 4) & 0xF0);
                dst[8 + BYTE_SWAP(3)] = g[2] >> 4;
                dst[12 + BYTE_SWAP(0)] = b[2] & 0xFF;
                dst[12 + BYTE_SWAP(1)] = ((b[2] >> 8) & 0xF) | ((r[3] << 4) & 0xF0);
                
                // Pixel 3
                dst[12 + BYTE_SWAP(2)] = r[3] >> 4;
                dst[12 + BYTE_SWAP(3)] = g[3] & 0xFF;
                dst[16 + BYTE_SWAP(0)] = ((g[3] >> 8) & 0xF) | ((b[3] << 4) & 0xF0);
                dst[16 + BYTE_SWAP(1)] = b[3] >> 4;
                
                // Pixel 4
                dst[16 + BYTE_SWAP(2)] = r[4] & 0xFF;
                dst[16 + BYTE_SWAP(3)] = ((r[4] >> 8) & 0xF) | ((g[4] << 4) & 0xF0);
                dst[20 + BYTE_SWAP(0)] = g[4] >> 4;
                dst[20 + BYTE_SWAP(1)] = b[4] & 0xFF;
                dst[20 + BYTE_SWAP(2)] = ((b[4] >> 8) & 0xF) | ((r[5] << 4) & 0xF0);
                
                // Pixel 5
                dst[20 + BYTE_SWAP(3)] = r[5] >> 4;
                dst[24 + BYTE_SWAP(0)] = g[5] & 0xFF;
                dst[24 + BYTE_SWAP(1)] = ((g[5] >> 8) & 0xF) | ((b[5] << 4) & 0xF0);
                dst[24 + BYTE_SWAP(2)] = b[5] >> 4;
                
                // Pixel 6
                dst[24 + BYTE_SWAP(3)] = r[6] & 0xFF;
                dst[28 + BYTE_SWAP(0)] = ((r[6] >> 8) & 0xF) | ((g[6] << 4) & 0xF0);
                dst[28 + BYTE_SWAP(1)] = g[6] >> 4;
                dst[28 + BYTE_SWAP(2)] = b[6] & 0xFF;
                dst[28 + BYTE_SWAP(3)] = ((b[6] >> 8) & 0xF) | ((r[7] << 4) & 0xF0);
                
                // Pixel 7
                dst[32 + BYTE_SWAP(0)] = r[7] >> 4;
                dst[32 + BYTE_SWAP(1)] = g[7] & 0xFF;
                dst[32 + BYTE_SWAP(2)] = ((g[7] >> 8) & 0xF) | ((b[7] << 4) & 0xF0);
                dst[32 + BYTE_SWAP(3)] = b[7] >> 4;
                
                dst += 36;
                src += 36;
        }
        
#undef BYTE_SWAP
}

/**
 * Native I420 alpha blending (YUV 4:2:0 planar)
 */
void alpha_blend_i420(uint8_t *dst_y, uint8_t *dst_u, uint8_t *dst_v,
                      const uint8_t *src_y, const uint8_t *src_u, const uint8_t *src_v,
                      const uint8_t *alpha, int width, int height)
{
        // Blend Y plane (full resolution)
        for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                        uint8_t a = alpha[y * width + x];
                        dst_y[y * width + x] = (src_y[y * width + x] * a + 
                                                dst_y[y * width + x] * (255 - a)) / 255;
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

/**
 * Native Y416 alpha blending (16-bit YUV with alpha)
 * Y416 format: U16 Y16 V16 A16 (little-endian)
 */
void alpha_blend_y416(uint8_t *dst, const uint8_t *src, int width)
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
                // uint16_t a_dst = dst16[3]; // unused for now
                
                // Convert 16-bit alpha to 0-65535 range for blending
                // Blend with 16-bit precision
                uint32_t inv_alpha = 65535 - a_src;
                
                // Blend components
                dst16[0] = (uint16_t)(((uint32_t)u_src * a_src + (uint32_t)u_dst * inv_alpha) / 65535);
                dst16[1] = (uint16_t)(((uint32_t)y_src * a_src + (uint32_t)y_dst * inv_alpha) / 65535);
                dst16[2] = (uint16_t)(((uint32_t)v_src * a_src + (uint32_t)v_dst * inv_alpha) / 65535);
                
                // For alpha channel, we could either:
                // 1. Keep destination alpha (current behavior)
                // 2. Blend alphas: a_result = a_src + a_dst * (1 - a_src)
                // Let's keep destination alpha at full opacity
                dst16[3] = 65535;
                
                dst16 += 4;
                src16 += 4;
        }
}

/**
 * Get implementation name
 */
const char *alpha_blend_get_implementation(void)
{
#ifdef __AVX2__
        return "AVX2";
#elif defined(__SSE2__)
        return "SSE2";
#elif defined(__ARM_NEON)
        return "ARM NEON";
#else
        return "scalar";
#endif
}