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
 * Uses optimized scalar implementation with exact division for precise results
 * without aliasing artifacts.
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
 * @brief Alpha blend YUYV pixels with separate alpha channel
 * 
 * Blends source YUYV pixels onto destination YUYV pixels using a separate
 * alpha channel. Handles chroma subsampling correctly by averaging alpha
 * values for shared chroma components.
 * 
 * @param dst   Destination YUYV buffer (modified in place)
 * @param src   Source YUYV buffer
 * @param alpha Alpha values (one per pixel, not per YUYV pair)
 * @param width Number of pixels (must be even for YUYV)
 */
void alpha_blend_yuyv(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);

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
 * @brief Alpha blend v210 pixels (10-bit YUV 4:2:2) with separate alpha channel
 * 
 * Handles the complex v210 packing format where 6 pixels are packed into 16 bytes.
 * 
 * @param dst   Destination v210 buffer (modified in place)
 * @param src   Source v210 buffer
 * @param alpha Alpha values (one per pixel)
 * @param width Number of pixels (should be multiple of 6 for v210)
 */
void alpha_blend_v210(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);

/**
 * @brief Alpha blend R10k pixels (10-bit RGB) with separate alpha channel
 * 
 * Handles R10k format where RGB components are packed into 4 bytes with 2 bits padding.
 * 
 * @param dst   Destination R10k buffer (modified in place)
 * @param src   Source R10k buffer
 * @param alpha Alpha values (one per pixel)
 * @param width Number of pixels
 */
void alpha_blend_r10k(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);

/**
 * @brief Alpha blend R12L pixels (12-bit RGB little-endian) with separate alpha channel
 * 
 * Handles R12L format where 8 pixels are packed into 36 bytes.
 * 
 * @param dst   Destination R12L buffer (modified in place)
 * @param src   Source R12L buffer
 * @param alpha Alpha values (one per pixel)
 * @param width Number of pixels (should be multiple of 8 for R12L)
 */
void alpha_blend_r12l(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);

/**
 * @brief Alpha blend I420 pixels (YUV 4:2:0 planar) with separate alpha channel
 * 
 * Handles I420 planar format where Y plane is full resolution and U/V planes
 * are half resolution (4:2:0 subsampling).
 * 
 * @param dst_y  Destination Y plane (modified in place)
 * @param dst_u  Destination U plane (modified in place)
 * @param dst_v  Destination V plane (modified in place)
 * @param src_y  Source Y plane
 * @param src_u  Source U plane
 * @param src_v  Source V plane
 * @param alpha  Alpha values (full resolution, one per Y pixel)
 * @param width  Width in pixels
 * @param height Height in pixels
 */
void alpha_blend_i420(uint8_t *dst_y, uint8_t *dst_u, uint8_t *dst_v,
                      const uint8_t *src_y, const uint8_t *src_u, const uint8_t *src_v,
                      const uint8_t *alpha, int width, int height);

/**
 * @brief Alpha blend Y416 pixels (16-bit YUV with alpha) 
 * 
 * Y416 format already contains alpha channel, so this function uses the
 * embedded alpha from the source for blending.
 * Format: U16 Y16 V16 A16 (little-endian)
 * 
 * @param dst   Destination Y416 buffer (modified in place)
 * @param src   Source Y416 buffer with embedded alpha
 * @param width Number of pixels
 */
void alpha_blend_y416(uint8_t *dst, const uint8_t *src, int width);

/**
 * @brief Get the name of the alpha blending implementation being used
 * 
 * Useful for debugging and performance analysis.
 * 
 * @return String describing the implementation (currently "Scalar with exact division")
 */
const char *alpha_blend_get_implementation(void);

#ifdef __cplusplus
}
#endif

#endif // UTILS_ALPHA_BLEND_H_