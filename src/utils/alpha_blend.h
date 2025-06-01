/**
 * @file   utils/alpha_blend.h
 * @author UltraGrid Team
 * @brief  Alpha blending utilities for various pixel formats
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

#ifndef UTILS_ALPHA_BLEND_H_
#define UTILS_ALPHA_BLEND_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Alpha blend RGBA pixels with embedded alpha channel
 * 
 * Blends source RGBA pixels onto destination RGBA pixels using the alpha channel
 * from the source. Formula: dst = src * alpha + dst * (1 - alpha)
 * 
 * The implementation automatically uses the best available SIMD instructions
 * (AVX2, SSE2, or NEON) for optimal performance.
 * 
 * @param dst   Destination RGBA buffer (modified in place)
 * @param src   Source RGBA buffer with alpha channel
 * @param width Number of pixels to blend
 */
void alpha_blend_rgba(uint8_t *dst, const uint8_t *src, int width);

/**
 * @brief Alpha blend UYVY pixels with separate alpha channel
 * 
 * Blends source UYVY pixels onto destination UYVY pixels using a separate
 * alpha channel. Handles chroma subsampling correctly by averaging alpha
 * values for shared chroma components.
 * 
 * @param dst   Destination UYVY buffer (modified in place)
 * @param src   Source UYVY buffer
 * @param alpha Alpha values (one per pixel, not per UYVY pair)
 * @param width Number of pixels (must be even for UYVY)
 */
void alpha_blend_uyvy(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);

/**
 * @brief Alpha blend RGB pixels with separate alpha channel
 * 
 * @param dst   Destination RGB buffer (modified in place)
 * @param src   Source RGB buffer  
 * @param alpha Alpha values (one per pixel)
 * @param width Number of pixels
 */
void alpha_blend_rgb(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);

/**
 * @brief Get the name of the alpha blending implementation being used
 * 
 * Useful for debugging and performance analysis.
 * 
 * @return String describing the implementation (e.g., "AVX2", "SSE2", "NEON", "scalar")
 */
const char *alpha_blend_get_implementation(void);

#ifdef __cplusplus
}
#endif

#endif // UTILS_ALPHA_BLEND_H_