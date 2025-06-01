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
        // TODO: Implement proper NEON optimization
        // For now, use scalar implementation
        alpha_blend_rgba_scalar(dst, src, width);
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
 * Native UYVY alpha blending
 */
void alpha_blend_uyvy(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
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

/**
 * Native RGB alpha blending
 */
void alpha_blend_rgb(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
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