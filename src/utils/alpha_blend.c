/**
 * @file   utils/alpha_blend.c
 * @author Ben Roeder     <ben@sohonet.com>
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

#include <string.h>            // for memcpy

#include "utils/alpha_blend.h"

/// exact integer division by 255
#define EXACT_DIV255(x) ((x) / 255)

/// RGBA alpha blending with embedded alpha
void alpha_blend_rgba(uint8_t *dst, const uint8_t *src, int width)
{
        for (int x = 0; x < width; x++) {
                uint8_t r = src[0];
                uint8_t g = src[1];
                uint8_t b = src[2];
                uint8_t a = src[3];
                
                dst[0] = EXACT_DIV255(r * a + dst[0] * (255 - a));
                dst[1] = EXACT_DIV255(g * a + dst[1] * (255 - a));
                dst[2] = EXACT_DIV255(b * a + dst[2] * (255 - a));
                dst[3] = a + EXACT_DIV255(dst[3] * (255 - a));  // Porter-Duff 'over'
                
                src += 4;
                dst += 4;
        }
}

/// UYVY alpha blending with separate alpha channel
void alpha_blend_uyvy(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        for (int x = 0; x < width; x += 2) {
                // Get alpha values for both pixels
                uint8_t a0 = alpha[0];
                uint8_t a1 = alpha[1];

                // Components from source
                uint8_t u_src = src[0];
                uint8_t y0_src = src[1];
                uint8_t v_src = src[2];
                uint8_t y1_src = src[3];

                // Components from destination
                uint8_t u_dst = dst[0];
                uint8_t y0_dst = dst[1];
                uint8_t v_dst = dst[2];
                uint8_t y1_dst = dst[3];

                // Blend Y components with their respective alphas
                dst[1] = EXACT_DIV255(y0_src * a0 + y0_dst * (255 - a0));
                dst[3] = EXACT_DIV255(y1_src * a1 + y1_dst * (255 - a1));

                // For U and V, use average of both alphas
                uint16_t avg_alpha = (a0 + a1 + 1) >> 1;
                dst[0] = EXACT_DIV255(u_src * avg_alpha + u_dst * (255 - avg_alpha));
                dst[2] = EXACT_DIV255(v_src * avg_alpha + v_dst * (255 - avg_alpha));

                src += 4;
                dst += 4;
                alpha += 2;
        }
}

/// YUYV alpha blending with separate alpha channel
void alpha_blend_yuyv(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        for (int x = 0; x < width; x += 2) {
                // Get alpha values for both pixels
                uint8_t a0 = alpha[0];
                uint8_t a1 = alpha[1];

                // Components from source
                uint8_t y0_src = src[0];
                uint8_t u_src = src[1];
                uint8_t y1_src = src[2];
                uint8_t v_src = src[3];

                // Components from destination
                uint8_t y0_dst = dst[0];
                uint8_t u_dst = dst[1];
                uint8_t y1_dst = dst[2];
                uint8_t v_dst = dst[3];

                // Blend Y components with their respective alphas
                dst[0] = EXACT_DIV255(y0_src * a0 + y0_dst * (255 - a0));
                dst[2] = EXACT_DIV255(y1_src * a1 + y1_dst * (255 - a1));

                // For U and V, use average of both alphas
                uint16_t avg_alpha = (a0 + a1 + 1) >> 1;
                dst[1] = EXACT_DIV255(u_src * avg_alpha + u_dst * (255 - avg_alpha));
                dst[3] = EXACT_DIV255(v_src * avg_alpha + v_dst * (255 - avg_alpha));

                src += 4;
                dst += 4;
                alpha += 2;
        }
}

/// RGB alpha blending with separate alpha channel
void alpha_blend_rgb(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        for (int x = 0; x < width; x++) {
                uint8_t a = alpha[x];
                
                // Use exact division for accurate alpha blending
                dst[0] = EXACT_DIV255(src[0] * a + dst[0] * (255 - a));
                dst[1] = EXACT_DIV255(src[1] * a + dst[1] * (255 - a));
                dst[2] = EXACT_DIV255(src[2] * a + dst[2] * (255 - a));
                
                src += 3;
                dst += 3;
        }
}

/// v210 alpha blending (10-bit YUV 4:2:2, 6 pixels per 16 bytes)
void alpha_blend_v210(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        // Process 6 pixels at a time (width must be divisible by 6)
        for (int x = 0; x < width; x += 6) {
                // Calculate byte offset for this group of 6 pixels (16 bytes)
                size_t offset = (x / 6) * 16;

                // Unpack source values (10-bit) via memcpy to avoid alignment UB
                uint32_t s0, s1, s2, s3;
                memcpy(&s0, src + offset + 0, sizeof(uint32_t));
                memcpy(&s1, src + offset + 4, sizeof(uint32_t));
                memcpy(&s2, src + offset + 8, sizeof(uint32_t));
                memcpy(&s3, src + offset + 12, sizeof(uint32_t));
                
                // Extract 10-bit components from source
                // DWORD 0: Cb0 Y0 Cr0
                uint16_t cb0_src = (s0 >> 0) & 0x3FF;
                uint16_t y0_src = (s0 >> 10) & 0x3FF;
                uint16_t cr0_src = (s0 >> 20) & 0x3FF;
                
                // DWORD 1: Y1 Cb1 Y2
                uint16_t y1_src = (s1 >> 0) & 0x3FF;
                uint16_t cb1_src = (s1 >> 10) & 0x3FF;
                uint16_t y2_src = (s1 >> 20) & 0x3FF;
                
                // DWORD 2: Cr1 Y3 Cb2
                uint16_t cr1_src = (s2 >> 0) & 0x3FF;
                uint16_t y3_src = (s2 >> 10) & 0x3FF;
                uint16_t cb2_src = (s2 >> 20) & 0x3FF;
                
                // DWORD 3: Y4 Cr2 Y5
                uint16_t y4_src = (s3 >> 0) & 0x3FF;
                uint16_t cr2_src = (s3 >> 10) & 0x3FF;
                uint16_t y5_src = (s3 >> 20) & 0x3FF;
                
                // Unpack destination values
                uint32_t d0, d1, d2, d3;
                memcpy(&d0, dst + offset + 0, sizeof(uint32_t));
                memcpy(&d1, dst + offset + 4, sizeof(uint32_t));
                memcpy(&d2, dst + offset + 8, sizeof(uint32_t));
                memcpy(&d3, dst + offset + 12, sizeof(uint32_t));
                
                uint16_t cb0_dst = (d0 >> 0) & 0x3FF;
                uint16_t y0_dst = (d0 >> 10) & 0x3FF;
                uint16_t cr0_dst = (d0 >> 20) & 0x3FF;
                
                uint16_t y1_dst = (d1 >> 0) & 0x3FF;
                uint16_t cb1_dst = (d1 >> 10) & 0x3FF;
                uint16_t y2_dst = (d1 >> 20) & 0x3FF;
                
                uint16_t cr1_dst = (d2 >> 0) & 0x3FF;
                uint16_t y3_dst = (d2 >> 10) & 0x3FF;
                uint16_t cb2_dst = (d2 >> 20) & 0x3FF;
                
                uint16_t y4_dst = (d3 >> 0) & 0x3FF;
                uint16_t cr2_dst = (d3 >> 10) & 0x3FF;
                uint16_t y5_dst = (d3 >> 20) & 0x3FF;
                
                // Get alpha values (8-bit, need to scale to 10-bit)
                uint16_t a0 = (alpha[0] << 2) | (alpha[0] >> 6);  // Scale 8-bit to 10-bit
                uint16_t a1 = (alpha[1] << 2) | (alpha[1] >> 6);
                uint16_t a2 = (alpha[2] << 2) | (alpha[2] >> 6);
                uint16_t a3 = (alpha[3] << 2) | (alpha[3] >> 6);
                uint16_t a4 = (alpha[4] << 2) | (alpha[4] >> 6);
                uint16_t a5 = (alpha[5] << 2) | (alpha[5] >> 6);
                
                // Blend Y values with their respective alphas
                y0_dst = ((uint32_t)y0_src * a0 + (uint32_t)y0_dst * (1023 - a0)) / 1023;
                y1_dst = ((uint32_t)y1_src * a1 + (uint32_t)y1_dst * (1023 - a1)) / 1023;
                y2_dst = ((uint32_t)y2_src * a2 + (uint32_t)y2_dst * (1023 - a2)) / 1023;
                y3_dst = ((uint32_t)y3_src * a3 + (uint32_t)y3_dst * (1023 - a3)) / 1023;
                y4_dst = ((uint32_t)y4_src * a4 + (uint32_t)y4_dst * (1023 - a4)) / 1023;
                y5_dst = ((uint32_t)y5_src * a5 + (uint32_t)y5_dst * (1023 - a5)) / 1023;
                
                // Blend chroma with average alpha
                uint16_t avg_a01 = (a0 + a1 + 1) >> 1;
                uint16_t avg_a23 = (a2 + a3 + 1) >> 1;
                uint16_t avg_a45 = (a4 + a5 + 1) >> 1;
                
                cb0_dst = ((uint32_t)cb0_src * avg_a01 + (uint32_t)cb0_dst * (1023 - avg_a01)) / 1023;
                cr0_dst = ((uint32_t)cr0_src * avg_a01 + (uint32_t)cr0_dst * (1023 - avg_a01)) / 1023;
                cb1_dst = ((uint32_t)cb1_src * avg_a23 + (uint32_t)cb1_dst * (1023 - avg_a23)) / 1023;
                cr1_dst = ((uint32_t)cr1_src * avg_a23 + (uint32_t)cr1_dst * (1023 - avg_a23)) / 1023;
                cb2_dst = ((uint32_t)cb2_src * avg_a45 + (uint32_t)cb2_dst * (1023 - avg_a45)) / 1023;
                cr2_dst = ((uint32_t)cr2_src * avg_a45 + (uint32_t)cr2_dst * (1023 - avg_a45)) / 1023;
                
                // Pack back
                d0 = (cb0_dst & 0x3FF) | ((y0_dst & 0x3FF) << 10) | ((cr0_dst & 0x3FF) << 20);
                d1 = (y1_dst & 0x3FF) | ((cb1_dst & 0x3FF) << 10) | ((y2_dst & 0x3FF) << 20);
                d2 = (cr1_dst & 0x3FF) | ((y3_dst & 0x3FF) << 10) | ((cb2_dst & 0x3FF) << 20);
                d3 = (y4_dst & 0x3FF) | ((cr2_dst & 0x3FF) << 10) | ((y5_dst & 0x3FF) << 20);

                memcpy(dst + offset + 0, &d0, sizeof(uint32_t));
                memcpy(dst + offset + 4, &d1, sizeof(uint32_t));
                memcpy(dst + offset + 8, &d2, sizeof(uint32_t));
                memcpy(dst + offset + 12, &d3, sizeof(uint32_t));

                alpha += 6;
        }
}

/// R10k alpha blending (10-bit RGB, 2:10:10:10 packed)
void alpha_blend_r10k(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        for (int x = 0; x < width; x++) {
                // 4 bytes per pixel
                size_t offset = x * 4;

                uint32_t src_pixel, dst_pixel;
                memcpy(&src_pixel, src + offset, sizeof(uint32_t));
                memcpy(&dst_pixel, dst + offset, sizeof(uint32_t));
                
                // Extract 10-bit components
                uint16_t r_src = (src_pixel >> 20) & 0x3FF;
                uint16_t g_src = (src_pixel >> 10) & 0x3FF;
                uint16_t b_src = (src_pixel >> 0) & 0x3FF;
                
                uint16_t r_dst = (dst_pixel >> 20) & 0x3FF;
                uint16_t g_dst = (dst_pixel >> 10) & 0x3FF;
                uint16_t b_dst = (dst_pixel >> 0) & 0x3FF;
                
                // Scale 8-bit alpha to 10-bit
                uint16_t a = (alpha[x] << 2) | (alpha[x] >> 6);
                
                // Blend (10-bit precision)
                r_dst = ((uint32_t)r_src * a + (uint32_t)r_dst * (1023 - a)) / 1023;
                g_dst = ((uint32_t)g_src * a + (uint32_t)g_dst * (1023 - a)) / 1023;
                b_dst = ((uint32_t)b_src * a + (uint32_t)b_dst * (1023 - a)) / 1023;
                
                // Pack back with alpha set to max (0x3)
                uint32_t result = (0x3 << 30) | ((r_dst & 0x3FF) << 20) |
                                 ((g_dst & 0x3FF) << 10) | (b_dst & 0x3FF);
                memcpy(dst + offset, &result, sizeof(uint32_t));
        }
}

/// R12L alpha blending (12-bit RGB LE, 8 pixels per 36 bytes)
void alpha_blend_r12l(uint8_t *dst, const uint8_t *src, const uint8_t *alpha, int width)
{
        // Process pixels in groups of 8 (which pack into 36 bytes)
        int pixels_processed = 0;
        
        while (pixels_processed + 8 <= width) {
                // Point to the start of this 8-pixel group
                const uint8_t *src_ptr = src + (pixels_processed / 8) * 36;
                uint8_t *dst_ptr = dst + (pixels_processed / 8) * 36;
                
                // Extract all 8 pixels first
                uint16_t src_pixels[8][3]; // [pixel][component: R,G,B]
                uint16_t dst_pixels[8][3];
                
                // Unpack pixels - every 9 bytes contains 2 pixels
                for (int pair = 0; pair < 4; pair++) {
                        int byte_offset = pair * 9;
                        
                        // First pixel of pair: R0 and G0 in first 3 bytes
                        uint32_t data = ((uint32_t)src_ptr[byte_offset] << 0) |
                                       ((uint32_t)src_ptr[byte_offset + 1] << 8) |
                                       ((uint32_t)src_ptr[byte_offset + 2] << 16);
                        src_pixels[pair * 2][0] = (data >> 0) & 0xFFF;  // R0
                        src_pixels[pair * 2][1] = (data >> 12) & 0xFFF; // G0
                        
                        data = ((uint32_t)dst_ptr[byte_offset] << 0) |
                               ((uint32_t)dst_ptr[byte_offset + 1] << 8) |
                               ((uint32_t)dst_ptr[byte_offset + 2] << 16);
                        dst_pixels[pair * 2][0] = (data >> 0) & 0xFFF;  // R0
                        dst_pixels[pair * 2][1] = (data >> 12) & 0xFFF; // G0
                        
                        // B0 and R1 in next 3 bytes
                        data = ((uint32_t)src_ptr[byte_offset + 3] << 0) |
                               ((uint32_t)src_ptr[byte_offset + 4] << 8) |
                               ((uint32_t)src_ptr[byte_offset + 5] << 16);
                        src_pixels[pair * 2][2] = (data >> 0) & 0xFFF;      // B0
                        src_pixels[pair * 2 + 1][0] = (data >> 12) & 0xFFF; // R1
                        
                        data = ((uint32_t)dst_ptr[byte_offset + 3] << 0) |
                               ((uint32_t)dst_ptr[byte_offset + 4] << 8) |
                               ((uint32_t)dst_ptr[byte_offset + 5] << 16);
                        dst_pixels[pair * 2][2] = (data >> 0) & 0xFFF;      // B0
                        dst_pixels[pair * 2 + 1][0] = (data >> 12) & 0xFFF; // R1
                        
                        // G1 and B1 in last 3 bytes
                        data = ((uint32_t)src_ptr[byte_offset + 6] << 0) |
                               ((uint32_t)src_ptr[byte_offset + 7] << 8) |
                               ((uint32_t)src_ptr[byte_offset + 8] << 16);
                        src_pixels[pair * 2 + 1][1] = (data >> 0) & 0xFFF;  // G1
                        src_pixels[pair * 2 + 1][2] = (data >> 12) & 0xFFF; // B1
                        
                        data = ((uint32_t)dst_ptr[byte_offset + 6] << 0) |
                               ((uint32_t)dst_ptr[byte_offset + 7] << 8) |
                               ((uint32_t)dst_ptr[byte_offset + 8] << 16);
                        dst_pixels[pair * 2 + 1][1] = (data >> 0) & 0xFFF;  // G1
                        dst_pixels[pair * 2 + 1][2] = (data >> 12) & 0xFFF; // B1
                }
                
                // Blend all 8 pixels
                for (int i = 0; i < 8; i++) {
                        // Scale 8-bit alpha to 12-bit
                        uint16_t a = (alpha[pixels_processed + i] << 4) | (alpha[pixels_processed + i] >> 4);
                        
                        // Blend each component
                        for (int c = 0; c < 3; c++) {
                                dst_pixels[i][c] = ((uint32_t)src_pixels[i][c] * a + 
                                                   (uint32_t)dst_pixels[i][c] * (4095 - a)) / 4095;
                        }
                }
                
                // Pack pixels back - every 9 bytes contains 2 pixels
                for (int pair = 0; pair < 4; pair++) {
                        int byte_offset = pair * 9;
                        
                        // Pack R0 and G0 into first 3 bytes
                        uint32_t data = (dst_pixels[pair * 2][0] & 0xFFF) |        // R0
                                       ((dst_pixels[pair * 2][1] & 0xFFF) << 12);  // G0
                        dst_ptr[byte_offset] = data & 0xFF;
                        dst_ptr[byte_offset + 1] = (data >> 8) & 0xFF;
                        dst_ptr[byte_offset + 2] = (data >> 16) & 0xFF;
                        
                        // Pack B0 and R1 into next 3 bytes
                        data = (dst_pixels[pair * 2][2] & 0xFFF) |             // B0
                               ((dst_pixels[pair * 2 + 1][0] & 0xFFF) << 12);  // R1
                        dst_ptr[byte_offset + 3] = data & 0xFF;
                        dst_ptr[byte_offset + 4] = (data >> 8) & 0xFF;
                        dst_ptr[byte_offset + 5] = (data >> 16) & 0xFF;
                        
                        // Pack G1 and B1 into last 3 bytes
                        data = (dst_pixels[pair * 2 + 1][1] & 0xFFF) |         // G1
                               ((dst_pixels[pair * 2 + 1][2] & 0xFFF) << 12);  // B1
                        dst_ptr[byte_offset + 6] = data & 0xFF;
                        dst_ptr[byte_offset + 7] = (data >> 8) & 0xFF;
                        dst_ptr[byte_offset + 8] = (data >> 16) & 0xFF;
                }
                
                pixels_processed += 8;
        }
        
        // Handle remaining pixels (less than 8) — process in pairs of 2
        int remaining = width - pixels_processed;
        if (remaining > 0) {
                const uint8_t *src_ptr = src + (pixels_processed / 8) * 36;
                uint8_t *dst_ptr = dst + (pixels_processed / 8) * 36;
                int num_pairs = (remaining + 1) / 2;

                // Unpack, blend, repack each pair (9 bytes = 2 pixels)
                for (int pair = 0; pair < num_pairs; pair++) {
                        int byte_offset = pair * 9;
                        int px0 = pixels_processed + pair * 2;
                        int px1 = px0 + 1;

                        // Unpack first pixel: R0 and G0
                        uint32_t sdata = ((uint32_t)src_ptr[byte_offset] << 0) |
                                        ((uint32_t)src_ptr[byte_offset + 1] << 8) |
                                        ((uint32_t)src_ptr[byte_offset + 2] << 16);
                        uint16_t r0_src = (sdata >> 0) & 0xFFF;
                        uint16_t g0_src = (sdata >> 12) & 0xFFF;

                        uint32_t ddata = ((uint32_t)dst_ptr[byte_offset] << 0) |
                                        ((uint32_t)dst_ptr[byte_offset + 1] << 8) |
                                        ((uint32_t)dst_ptr[byte_offset + 2] << 16);
                        uint16_t r0_dst = (ddata >> 0) & 0xFFF;
                        uint16_t g0_dst = (ddata >> 12) & 0xFFF;

                        // Unpack B0 and R1
                        sdata = ((uint32_t)src_ptr[byte_offset + 3] << 0) |
                               ((uint32_t)src_ptr[byte_offset + 4] << 8) |
                               ((uint32_t)src_ptr[byte_offset + 5] << 16);
                        uint16_t b0_src = (sdata >> 0) & 0xFFF;
                        uint16_t r1_src = (sdata >> 12) & 0xFFF;

                        ddata = ((uint32_t)dst_ptr[byte_offset + 3] << 0) |
                               ((uint32_t)dst_ptr[byte_offset + 4] << 8) |
                               ((uint32_t)dst_ptr[byte_offset + 5] << 16);
                        uint16_t b0_dst = (ddata >> 0) & 0xFFF;
                        uint16_t r1_dst = (ddata >> 12) & 0xFFF;

                        // Blend first pixel
                        uint16_t a0 = (alpha[px0] << 4) | (alpha[px0] >> 4);
                        r0_dst = ((uint32_t)r0_src * a0 + (uint32_t)r0_dst * (4095 - a0)) / 4095;
                        g0_dst = ((uint32_t)g0_src * a0 + (uint32_t)g0_dst * (4095 - a0)) / 4095;
                        b0_dst = ((uint32_t)b0_src * a0 + (uint32_t)b0_dst * (4095 - a0)) / 4095;

                        // Blend second pixel if it exists
                        if (px1 < width) {
                                sdata = ((uint32_t)src_ptr[byte_offset + 6] << 0) |
                                       ((uint32_t)src_ptr[byte_offset + 7] << 8) |
                                       ((uint32_t)src_ptr[byte_offset + 8] << 16);
                                uint16_t g1_src = (sdata >> 0) & 0xFFF;
                                uint16_t b1_src = (sdata >> 12) & 0xFFF;

                                ddata = ((uint32_t)dst_ptr[byte_offset + 6] << 0) |
                                       ((uint32_t)dst_ptr[byte_offset + 7] << 8) |
                                       ((uint32_t)dst_ptr[byte_offset + 8] << 16);
                                uint16_t g1_dst = (ddata >> 0) & 0xFFF;
                                uint16_t b1_dst = (ddata >> 12) & 0xFFF;

                                uint16_t a1 = (alpha[px1] << 4) | (alpha[px1] >> 4);
                                r1_dst = ((uint32_t)r1_src * a1 + (uint32_t)r1_dst * (4095 - a1)) / 4095;
                                uint16_t g1_out = ((uint32_t)g1_src * a1 + (uint32_t)g1_dst * (4095 - a1)) / 4095;
                                uint16_t b1_out = ((uint32_t)b1_src * a1 + (uint32_t)b1_dst * (4095 - a1)) / 4095;

                                // Pack G1 and B1
                                uint32_t packed = (g1_out & 0xFFF) | ((b1_out & 0xFFF) << 12);
                                dst_ptr[byte_offset + 6] = packed & 0xFF;
                                dst_ptr[byte_offset + 7] = (packed >> 8) & 0xFF;
                                dst_ptr[byte_offset + 8] = (packed >> 16) & 0xFF;
                        }

                        // Pack R0 and G0
                        uint32_t packed = (r0_dst & 0xFFF) | ((g0_dst & 0xFFF) << 12);
                        dst_ptr[byte_offset] = packed & 0xFF;
                        dst_ptr[byte_offset + 1] = (packed >> 8) & 0xFF;
                        dst_ptr[byte_offset + 2] = (packed >> 16) & 0xFF;

                        // Pack B0 and R1
                        packed = (b0_dst & 0xFFF) | ((r1_dst & 0xFFF) << 12);
                        dst_ptr[byte_offset + 3] = packed & 0xFF;
                        dst_ptr[byte_offset + 4] = (packed >> 8) & 0xFF;
                        dst_ptr[byte_offset + 5] = (packed >> 16) & 0xFF;
                }
        }
}

/// I420 alpha blending (YUV 4:2:0 planar)
void alpha_blend_i420(uint8_t *dst_y, uint8_t *dst_u, uint8_t *dst_v,
                     const uint8_t *src_y, const uint8_t *src_u, const uint8_t *src_v,
                     const uint8_t *alpha, int width, int height)
{
        // Blend Y plane (full resolution)
        for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                        uint8_t a = alpha[y * width + x];
                        int idx = y * width + x;
                        dst_y[idx] = EXACT_DIV255(src_y[idx] * a + dst_y[idx] * (255 - a));
                }
        }
        
        // Blend U and V planes (half resolution)
        int uv_width = width / 2;
        int uv_height = height / 2;
        
        for (int y = 0; y < uv_height; y++) {
                for (int x = 0; x < uv_width; x++) {
                        // Average alpha from corresponding 2x2 block in Y plane
                        int y_base = y * 2;
                        int x_base = x * 2;
                        uint16_t a_sum = alpha[y_base * width + x_base] +
                                        alpha[y_base * width + x_base + 1] +
                                        alpha[(y_base + 1) * width + x_base] +
                                        alpha[(y_base + 1) * width + x_base + 1];
                        uint8_t a = (a_sum + 2) >> 2;  // Average with rounding
                        
                        int idx = y * uv_width + x;
                        dst_u[idx] = EXACT_DIV255(src_u[idx] * a + dst_u[idx] * (255 - a));
                        dst_v[idx] = EXACT_DIV255(src_v[idx] * a + dst_v[idx] * (255 - a));
                }
        }
}

/// Y416 alpha blending (16-bit YUV with embedded alpha)
void alpha_blend_y416(uint8_t *dst, const uint8_t *src, int width)
{
        for (int x = 0; x < width; x++) {
                // 8 bytes per pixel: 4 components x 2 bytes
                size_t offset = x * 8;

                // Extract components (little-endian 16-bit)
                uint16_t u_src, y_src, v_src, a_src;
                memcpy(&u_src, src + offset + 0, sizeof(uint16_t));
                memcpy(&y_src, src + offset + 2, sizeof(uint16_t));
                memcpy(&v_src, src + offset + 4, sizeof(uint16_t));
                memcpy(&a_src, src + offset + 6, sizeof(uint16_t));

                uint16_t u_dst, y_dst, v_dst;
                memcpy(&u_dst, dst + offset + 0, sizeof(uint16_t));
                memcpy(&y_dst, dst + offset + 2, sizeof(uint16_t));
                memcpy(&v_dst, dst + offset + 4, sizeof(uint16_t));

                // Blend with 16-bit precision
                uint32_t inv_alpha = 65535 - a_src;

                uint16_t u_out = (uint16_t)(((uint32_t)u_src * a_src + (uint32_t)u_dst * inv_alpha) / 65535);
                uint16_t y_out = (uint16_t)(((uint32_t)y_src * a_src + (uint32_t)y_dst * inv_alpha) / 65535);
                uint16_t v_out = (uint16_t)(((uint32_t)v_src * a_src + (uint32_t)v_dst * inv_alpha) / 65535);
                uint16_t a_out = 65535; // Keep destination alpha at full opacity

                memcpy(dst + offset + 0, &u_out, sizeof(uint16_t));
                memcpy(dst + offset + 2, &y_out, sizeof(uint16_t));
                memcpy(dst + offset + 4, &v_out, sizeof(uint16_t));
                memcpy(dst + offset + 6, &a_out, sizeof(uint16_t));
        }
}

const char *alpha_blend_get_implementation(void)
{
        return "Scalar with exact division";
}