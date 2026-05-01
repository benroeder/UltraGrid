/**
 * @file   utils/alpha_blend.h
 * @author Ben Roeder     <ben@sohonet.com>
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

#include <stdint.h>           // for uint8_t, uint16_t

#ifdef __cplusplus
extern "C" {
#endif

void alpha_blend_rgba(uint8_t *dst, const uint8_t *src, int width);
void alpha_blend_uyvy(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);
void alpha_blend_yuyv(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);
void alpha_blend_rgb(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);
void alpha_blend_v210(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);
void alpha_blend_r10k(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);
void alpha_blend_r12l(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width);
void alpha_blend_i420(uint8_t *dst_y, uint8_t *dst_u, uint8_t *dst_v,
                      const uint8_t *src_y, const uint8_t *src_u, const uint8_t *src_v,
                      const uint8_t *alpha, int width, int height);
void alpha_blend_y416(uint8_t *dst, const uint8_t *src, int width);
const char *alpha_blend_get_implementation(void);

#ifdef __cplusplus
}
#endif

#endif // UTILS_ALPHA_BLEND_H_