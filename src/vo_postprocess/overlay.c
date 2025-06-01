/**
 * @file   vo_postprocess/overlay.c
 * @author UltraGrid Team
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

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <libswscale/swscale.h>

#include "compat/strings.h"
#include "debug.h"
#include "lib_common.h"
#include "pixfmt_conv.h"
#include "types.h"
#include "utils/alpha_blend.h"
#include "utils/color_out.h"
#include "utils/macros.h"
#include "utils/pam.h"
#include "utils/misc.h"  // for get_time_in_ns()
#include "video.h"
#include "video_codec.h"
#include "video_display.h"
#include "video_frame.h"
#include "vo_postprocess.h"

#define MOD_NAME "[overlay] "
#define DEFAULT_OVERLAY_PATH "overlay.pam"
#define OVERLAY_WIDTH 1920
#define OVERLAY_HEIGHT 1080

enum position {
        POS_CENTER,
        POS_TOP_LEFT,
        POS_TOP_RIGHT,
        POS_BOTTOM_LEFT,
        POS_BOTTOM_RIGHT
};

struct state_overlay {
        struct video_desc saved_desc;
        struct video_frame *in;
        
        // Overlay image data
        unsigned char *overlay_data;     // RGBA format
        unsigned int overlay_width;
        unsigned int overlay_height;
        
        // Overlay settings
        char *overlay_path;
        enum position position;
        bool scale_to_fit;
        bool use_custom_pos;    // Use x,y instead of position enum
        int custom_x;
        int custom_y;
        
        // File monitoring
        time_t last_modified_sec;
        long last_modified_nsec;
        
        // Error rate limiting
        time_t last_error_time;
        int error_count;
        
        // Performance optimization
        unsigned char *scaled_overlay;   // Cached scaled overlay
        unsigned int scaled_width;
        unsigned int scaled_height;
        codec_t last_codec;             // Last processed codec
        struct SwsContext *sws_ctx;     // libswscale context for scaling
        unsigned int sws_src_width;     // Last source width used for sws context
        unsigned int sws_src_height;    // Last source height used for sws context
        
        // Performance monitoring
        struct {
                long long total_time_ns;        // Total processing time
                long long load_time_ns;         // Time spent loading images
                long long scale_time_ns;        // Time spent scaling
                long long blend_time_ns;        // Time spent blending
                long long decode_time_ns;       // Time spent decoding to RGBA
                long long encode_time_ns;       // Time spent encoding from RGBA
                long frame_count;               // Number of frames processed
                long overlay_reloads;           // Number of times overlay was reloaded
                long scale_operations;          // Number of scaling operations
                time_t last_report_time;        // Last time we reported stats
                bool enabled;                   // Whether monitoring is enabled
        } perf;
};

static bool overlay_get_property(void *state, int property, void *val, size_t *len)
{
        UNUSED(state);
        UNUSED(property);
        UNUSED(val);
        UNUSED(len);
        return false;
}


static void print_help() {
        color_printf("Overlay postprocessor overlays a PAM image onto video frames.\n");
        color_printf("\nUsage:\n");
        color_printf(TERM_BOLD TERM_FG_RED "\t-p overlay" TERM_FG_RESET "[:file=<path>][:position=<pos>][:x=<x>][:y=<y>][:scale=<mode>][:perf]\n" TERM_RESET);
        color_printf("\nParameters:\n");
        color_printf(TERM_BOLD "\tfile=<path>" TERM_RESET " - Path to PAM overlay image (default: overlay.pam)\n");
        color_printf(TERM_BOLD "\tposition=<pos>" TERM_RESET " - Overlay position: center, topleft, topright, bottomleft, bottomright (default: center)\n");
        color_printf(TERM_BOLD "\tx=<x>" TERM_RESET " - Custom X position in pixels (overrides position parameter)\n");
        color_printf(TERM_BOLD "\ty=<y>" TERM_RESET " - Custom Y position in pixels (overrides position parameter)\n");
        color_printf(TERM_BOLD "\tscale=<mode>" TERM_RESET " - Scaling mode: fit, none (default: fit)\n");
        color_printf(TERM_BOLD "\tperf" TERM_RESET " - Enable performance monitoring (reports every 5 seconds)\n");
        color_printf("\nExamples:\n");
        color_printf(TERM_BOLD "\tuv -t testcard -p overlay:file=logo.pam:position=topright -d sdl\n" TERM_RESET);
        color_printf(TERM_BOLD "\tuv -t testcard -p overlay:file=logo.pam:x=100:y=50:perf -d sdl\n" TERM_RESET);
        color_printf("\nNotes:\n");
        color_printf(" - Overlay image should be in PAM format with alpha channel\n");
        color_printf(" - Image is reloaded automatically when file is modified\n");
        color_printf(" - Uses alpha channel for transparency\n");
        color_printf(" - Negative X/Y values position from right/bottom edges\n");
}

static bool load_overlay_image(struct state_overlay *s) {
        long long start_time = 0;
        if (s->perf.enabled) {
                start_time = get_time_in_ns();
        }
        
#ifdef _WIN32
        // Windows-specific file time handling
        WIN32_FILE_ATTRIBUTE_DATA fileInfo;
        if (!GetFileAttributesExA(s->overlay_path, GetFileExInfoStandard, &fileInfo)) {
#else
        struct stat st;
        if (stat(s->overlay_path, &st) != 0) {
#endif
                // Rate limit error messages - only log every 5 seconds
                time_t now = time(NULL);
                if (now - s->last_error_time >= 5) {
                        if (errno == ENOENT) {
                                log_msg(LOG_LEVEL_WARNING, MOD_NAME "Overlay file not found: %s\n", s->overlay_path);
                        } else {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Cannot stat overlay file '%s': %s\n", 
                                        s->overlay_path, strerror(errno));
                        }
                        s->last_error_time = now;
                        s->error_count++;
                        
                        if (s->error_count == 10) {
                                log_msg(LOG_LEVEL_WARNING, MOD_NAME "Suppressing further overlay loading errors\n");
                        }
                } else if (s->error_count < 10) {
                        // Still log debug messages for debugging
                        log_msg(LOG_LEVEL_DEBUG, MOD_NAME "Overlay file stat failed (suppressed)\n");
                }
                return false;
        }
        
        // Reset error count on success
        s->error_count = 0;
        
#ifdef _WIN32
        // Check if it's a regular file (not a directory)
        if (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Overlay path is a directory, not a file: %s\n", s->overlay_path);
                return false;
        }
#else
        // Check if it's a regular file
        if (!S_ISREG(st.st_mode)) {
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Overlay path is not a regular file: %s\n", s->overlay_path);
                return false;
        }
        
        // Check if file is readable
        if (access(s->overlay_path, R_OK) != 0) {
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Overlay file is not readable: %s\n", s->overlay_path);
                return false;
        }
#endif
        
        // Check if file has been modified (using nanosecond precision where available)
#ifdef _WIN32
        // Windows FILETIME is in 100-nanosecond intervals since 1601
        // Convert to Unix epoch (seconds since 1970) and nanoseconds
        ULARGE_INTEGER uli;
        uli.LowPart = fileInfo.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = fileInfo.ftLastWriteTime.dwHighDateTime;
        
        // Convert from Windows epoch to Unix epoch
        // Windows epoch starts at 1601-01-01, Unix epoch at 1970-01-01
        // Difference is 11644473600 seconds
        const ULONGLONG WINDOWS_TICK = 10000000; // 100ns intervals per second
        const ULONGLONG SEC_TO_UNIX_EPOCH = 11644473600ULL;
        
        time_t win_sec = (time_t)(uli.QuadPart / WINDOWS_TICK - SEC_TO_UNIX_EPOCH);
        long win_nsec = (long)((uli.QuadPart % WINDOWS_TICK) * 100); // Convert 100ns to ns
        
        if (win_sec == s->last_modified_sec && 
            win_nsec == s->last_modified_nsec && 
            s->overlay_data != NULL) {
                return true; // No change needed
        }
#elif defined(__APPLE__)
        if (st.st_mtimespec.tv_sec == s->last_modified_sec && 
            st.st_mtimespec.tv_nsec == s->last_modified_nsec && 
            s->overlay_data != NULL) {
                return true; // No change needed
        }
#elif defined(__linux__)
        // Linux uses st_mtim (not st_mtimespec)
        if (st.st_mtim.tv_sec == s->last_modified_sec && 
            st.st_mtim.tv_nsec == s->last_modified_nsec && 
            s->overlay_data != NULL) {
                return true; // No change needed
        }
#else
        // Fall back to second precision on other systems
        if (st.st_mtime == s->last_modified_sec && s->overlay_data != NULL) {
                return true; // No change needed
        }
#endif
        
        log_msg(LOG_LEVEL_INFO, MOD_NAME "Loading overlay image: %s\n", s->overlay_path);
        
        // Free old data
        free(s->overlay_data);
        s->overlay_data = NULL;
        
        // Load PAM file
        struct pam_metadata info;
        unsigned char *data = NULL;
        
        if (!pam_read(s->overlay_path, &info, &data, malloc)) {
                // This could be a temporary failure if file is being written
                log_msg(LOG_LEVEL_DEBUG, MOD_NAME "Failed to read PAM file: %s (may be temporary)\n", s->overlay_path);
                // Keep existing overlay if we have one
                if (s->overlay_data) {
                        return true;
                }
                return false;
        }
        
        // Validate format
        if (info.ch_count != 3 && info.ch_count != 4) {
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Unsupported channel count %d in PAM file '%s' (need 3 or 4 for RGB/RGBA)\n", 
                        info.ch_count, s->overlay_path);
                free(data);
                return false;
        }
        
        // Validate dimensions
        if (info.width <= 0 || info.height <= 0) {
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Invalid image dimensions %dx%d in PAM file '%s'\n", 
                        info.width, info.height, s->overlay_path);
                free(data);
                return false;
        }
        
        // Validate maxval
        if (info.maxval != 255) {
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Unsupported maxval %d in PAM file '%s' (need 255 for 8-bit)\n", 
                        info.maxval, s->overlay_path);
                free(data);
                return false;
        }
        
        // Warn if overlay is very large
        if ((size_t)info.width * info.height > 8192 * 8192) {
                log_msg(LOG_LEVEL_WARNING, MOD_NAME "Overlay image is very large (%dx%d), this may impact performance\n", 
                        info.width, info.height);
        }
        
        s->overlay_width = info.width;
        s->overlay_height = info.height;
        
        // Convert to RGBA if needed
        if (info.ch_count == 3) {
                // RGB to RGBA
                size_t rgba_size = (size_t)info.width * info.height * 4;
                // Check for overflow
                if (rgba_size / 4 / info.height != info.width) {
                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Overlay image too large (would overflow)\n");
                        free(data);
                        return false;
                }
                s->overlay_data = malloc(rgba_size);
                if (!s->overlay_data) {
                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate memory for RGBA conversion\n");
                        free(data);
                        return false;
                }
                vc_copylineRGBtoRGBA(s->overlay_data, data, rgba_size, 0, 8, 16);
                free(data);
        } else {
                // Already RGBA
                s->overlay_data = data;
        }
        
#ifdef _WIN32
        // Save Windows file time converted to Unix epoch
        ULARGE_INTEGER uli;
        uli.LowPart = fileInfo.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = fileInfo.ftLastWriteTime.dwHighDateTime;
        
        const ULONGLONG WINDOWS_TICK = 10000000; // 100ns intervals per second
        const ULONGLONG SEC_TO_UNIX_EPOCH = 11644473600ULL;
        
        s->last_modified_sec = (time_t)(uli.QuadPart / WINDOWS_TICK - SEC_TO_UNIX_EPOCH);
        s->last_modified_nsec = (long)((uli.QuadPart % WINDOWS_TICK) * 100);
#elif defined(__APPLE__)
        s->last_modified_sec = st.st_mtimespec.tv_sec;
        s->last_modified_nsec = st.st_mtimespec.tv_nsec;
#elif defined(__linux__)
        // Linux uses st_mtim (not st_mtimespec)
        s->last_modified_sec = st.st_mtim.tv_sec;
        s->last_modified_nsec = st.st_mtim.tv_nsec;
#else
        s->last_modified_sec = st.st_mtime;
        s->last_modified_nsec = 0;
#endif
        
        // Invalidate scaled cache
        free(s->scaled_overlay);
        s->scaled_overlay = NULL;
        
        log_msg(LOG_LEVEL_INFO, MOD_NAME "Loaded overlay image: %dx%d\n", 
                s->overlay_width, s->overlay_height);
        
        if (s->perf.enabled) {
                s->perf.load_time_ns += get_time_in_ns() - start_time;
                s->perf.overlay_reloads++;
        }
        
        return true;
}

static void *overlay_init(const char *config) {
        struct state_overlay *s = calloc(1, sizeof(struct state_overlay));
        if (!s) {
                return NULL;
        }
        
        // Set defaults
        s->overlay_path = strdup(DEFAULT_OVERLAY_PATH);
        if (!s->overlay_path) {
                free(s);
                return NULL;
        }
        s->position = POS_CENTER;
        s->scale_to_fit = true;
        s->use_custom_pos = false;
        s->custom_x = 0;
        s->custom_y = 0;
        s->last_codec = VIDEO_CODEC_NONE;
        s->last_modified_sec = 0;
        s->last_modified_nsec = 0;
        s->last_error_time = 0;
        s->error_count = 0;
        s->sws_src_width = 0;
        s->sws_src_height = 0;
        
        // Parse help
        if (strlen(config) > 0 && strcmp(config, "help") == 0) {
                print_help();
                free(s->overlay_path);
                free(s);
                return NULL;
        }
        
        // Parse configuration
        if (strlen(config) > 0) {
                char *tmp = strdup(config);
                if (!tmp) {
                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Memory allocation failed\n");
                        free(s->overlay_path);
                        free(s);
                        return NULL;
                }
                char *config_copy = tmp;
                char *item, *save_ptr;
                
                while ((item = strtok_r(config_copy, ":", &save_ptr))) {
                        if (strncasecmp(item, "file=", 5) == 0) {
                                free(s->overlay_path);
                                s->overlay_path = strdup(item + 5);
                                if (!s->overlay_path) {
                                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Memory allocation failed\n");
                                        free(tmp);
                                        free(s);
                                        return NULL;
                                }
                        } else if (strncasecmp(item, "position=", 9) == 0) {
                                const char *pos = item + 9;
                                if (strcasecmp(pos, "center") == 0) {
                                        s->position = POS_CENTER;
                                } else if (strcasecmp(pos, "topleft") == 0) {
                                        s->position = POS_TOP_LEFT;
                                } else if (strcasecmp(pos, "topright") == 0) {
                                        s->position = POS_TOP_RIGHT;
                                } else if (strcasecmp(pos, "bottomleft") == 0) {
                                        s->position = POS_BOTTOM_LEFT;
                                } else if (strcasecmp(pos, "bottomright") == 0) {
                                        s->position = POS_BOTTOM_RIGHT;
                                } else {
                                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Unknown position: %s\n", pos);
                                        free(tmp);
                                        free(s->overlay_path);
                                        free(s);
                                        return NULL;
                                }
                        } else if (strncasecmp(item, "scale=", 6) == 0) {
                                const char *scale = item + 6;
                                if (strcasecmp(scale, "fit") == 0) {
                                        s->scale_to_fit = true;
                                } else if (strcasecmp(scale, "none") == 0) {
                                        s->scale_to_fit = false;
                                } else {
                                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Unknown scale mode: %s\n", scale);
                                        free(tmp);
                                        free(s->overlay_path);
                                        free(s);
                                        return NULL;
                                }
                        } else if (strncasecmp(item, "x=", 2) == 0) {
                                s->custom_x = atoi(item + 2);
                                s->use_custom_pos = true;
                        } else if (strncasecmp(item, "y=", 2) == 0) {
                                s->custom_y = atoi(item + 2);
                                s->use_custom_pos = true;
                        } else if (strcasecmp(item, "perf") == 0) {
                                s->perf.enabled = true;
                                s->perf.last_report_time = time(NULL);
                                log_msg(LOG_LEVEL_INFO, MOD_NAME "Performance monitoring enabled\n");
                        } else {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Unknown option: %s\n", item);
                                free(tmp);
                                free(s->overlay_path);
                                free(s);
                                return NULL;
                        }
                        config_copy = NULL;
                }
                free(tmp);
        }
        
        // Load initial overlay image
        if (!load_overlay_image(s)) {
                if (errno == ENOENT) {
                        log_msg(LOG_LEVEL_INFO, MOD_NAME "Overlay file not found at startup, will retry during processing\n");
                } else {
                        log_msg(LOG_LEVEL_WARNING, MOD_NAME "Failed to load initial overlay image, will retry during processing\n");
                }
                // Don't fail init - overlay can be added later
        } else {
                log_msg(LOG_LEVEL_INFO, MOD_NAME "Successfully loaded initial overlay image: %dx%d\n", 
                        s->overlay_width, s->overlay_height);
        }
        
        // Report which SIMD optimization is available
        log_msg(LOG_LEVEL_INFO, MOD_NAME "Using %s SIMD optimization for alpha blending\n", 
                alpha_blend_get_implementation());
        
        return s;
}

static bool overlay_postprocess_reconfigure(void *state, struct video_desc desc)
{
        struct state_overlay *s = (struct state_overlay *) state;
        s->saved_desc = desc;
        
        vf_free(s->in);
        s->in = vf_alloc_desc_data(desc);
        
        // Invalidate scaled cache if resolution changed
        if (s->scaled_overlay && (s->scaled_width != desc.width || s->scaled_height != desc.height)) {
                free(s->scaled_overlay);
                s->scaled_overlay = NULL;
        }
        
        return true;
}

static struct video_frame *overlay_getf(void *state)
{
        struct state_overlay *s = (struct state_overlay *) state;
        return s->in;
}

#ifdef __SSE2__
// SSE2 alpha blending - process 4 pixels at once
static inline void blend_line_sse2(unsigned char * __restrict rgba_line, 
                                  const unsigned char * __restrict overlay_line,
                                  int width)
{
        // Constants for blending
        const __m128i zero = _mm_setzero_si128();
        const __m128i alpha_mask = _mm_set1_epi32(0xFF000000);
        const __m128i ones = _mm_set1_epi16(1);
        
        int x = 0;
        
        // Process 4 pixels at a time
        for (; x <= width - 4; x += 4) {
                // Load 4 pixels from overlay and video (16 bytes each)
                __m128i overlay = _mm_loadu_si128((const __m128i*)(overlay_line + x * 4));
                __m128i video = _mm_loadu_si128((const __m128i*)(rgba_line + x * 4));
                
                // Separate into two 8x16bit vectors for processing
                __m128i overlay_lo = _mm_unpacklo_epi8(overlay, zero);
                __m128i overlay_hi = _mm_unpackhi_epi8(overlay, zero);
                __m128i video_lo = _mm_unpacklo_epi8(video, zero);
                __m128i video_hi = _mm_unpackhi_epi8(video, zero);
                
                // Extract alpha values (every 4th byte) and expand to 16-bit
                __m128i alpha_bytes = _mm_srli_epi32(overlay, 24);
                __m128i alpha_lo = _mm_unpacklo_epi16(alpha_bytes, alpha_bytes);
                __m128i alpha_hi = _mm_unpackhi_epi16(alpha_bytes, alpha_bytes);
                alpha_lo = _mm_unpacklo_epi16(alpha_lo, alpha_lo);
                alpha_hi = _mm_unpacklo_epi16(alpha_hi, alpha_hi);
                
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
                _mm_storeu_si128((__m128i*)(rgba_line + x * 4), result);
        }
        
        // Handle remaining pixels
        for (; x < width; x++) {
                const unsigned char *overlay_pixel = overlay_line + x * 4;
                unsigned char *rgba_pixel = rgba_line + x * 4;
                
                unsigned char r = overlay_pixel[0];
                unsigned char g = overlay_pixel[1];
                unsigned char b = overlay_pixel[2];
                unsigned char a = overlay_pixel[3];
                
                rgba_pixel[0] = (r * a + rgba_pixel[0] * (255 - a)) / 255;
                rgba_pixel[1] = (g * a + rgba_pixel[1] * (255 - a)) / 255;
                rgba_pixel[2] = (b * a + rgba_pixel[2] * (255 - a)) / 255;
                rgba_pixel[3] = 255;
        }
}
#endif

#ifdef __AVX2__
// AVX2 alpha blending - process 8 pixels at once
static inline void blend_line_avx2(unsigned char * __restrict rgba_line, 
                                  const unsigned char * __restrict overlay_line,
                                  int width)
{
        // Constants for blending
        const __m256i zero = _mm256_setzero_si256();
        const __m256i alpha_mask = _mm256_set1_epi32(0xFF000000);
        
        int x = 0;
        
        // Process 8 pixels at a time
        for (; x <= width - 8; x += 8) {
                // Load 8 pixels from overlay and video (32 bytes each)
                __m256i overlay = _mm256_loadu_si256((const __m256i*)(overlay_line + x * 4));
                __m256i video = _mm256_loadu_si256((const __m256i*)(rgba_line + x * 4));
                
                // Separate into 16-bit values for processing
                __m256i overlay_lo = _mm256_unpacklo_epi8(overlay, zero);
                __m256i overlay_hi = _mm256_unpackhi_epi8(overlay, zero);
                __m256i video_lo = _mm256_unpacklo_epi8(video, zero);
                __m256i video_hi = _mm256_unpackhi_epi8(video, zero);
                
                // Extract and broadcast alpha values
                __m256i alpha_bytes = _mm256_srli_epi32(overlay, 24);
                __m256i alpha_lo = _mm256_unpacklo_epi16(alpha_bytes, alpha_bytes);
                __m256i alpha_hi = _mm256_unpackhi_epi16(alpha_bytes, alpha_bytes);
                alpha_lo = _mm256_unpacklo_epi16(alpha_lo, alpha_lo);
                alpha_hi = _mm256_unpacklo_epi16(alpha_hi, alpha_hi);
                
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
                _mm256_storeu_si256((__m256i*)(rgba_line + x * 4), result);
        }
        
        // Handle remaining pixels with SSE2 or scalar
#ifdef __SSE2__
        blend_line_sse2(rgba_line + x * 4, overlay_line + x * 4, width - x);
#else
        for (; x < width; x++) {
                const unsigned char *overlay_pixel = overlay_line + x * 4;
                unsigned char *rgba_pixel = rgba_line + x * 4;
                
                unsigned char r = overlay_pixel[0];
                unsigned char g = overlay_pixel[1];
                unsigned char b = overlay_pixel[2];
                unsigned char a = overlay_pixel[3];
                
                rgba_pixel[0] = (r * a + rgba_pixel[0] * (255 - a)) / 255;
                rgba_pixel[1] = (g * a + rgba_pixel[1] * (255 - a)) / 255;
                rgba_pixel[2] = (b * a + rgba_pixel[2] * (255 - a)) / 255;
                rgba_pixel[3] = 255;
        }
#endif
}
#endif

#ifdef __ARM_NEON
// ARM NEON alpha blending - simplified scalar implementation for now
// TODO: Optimize with proper NEON vectorization
static inline void blend_line_neon(unsigned char * __restrict rgba_line, 
                                  const unsigned char * __restrict overlay_line,
                                  int width)
{
        // For now, use scalar implementation to ensure correctness
        // This will still be called when ARM NEON is available, allowing future optimization
        for (int x = 0; x < width; x++) {
                const unsigned char *overlay_pixel = overlay_line + x * 4;
                unsigned char *rgba_pixel = rgba_line + x * 4;
                
                unsigned char r = overlay_pixel[0];
                unsigned char g = overlay_pixel[1];
                unsigned char b = overlay_pixel[2];
                unsigned char a = overlay_pixel[3];
                
                rgba_pixel[0] = (r * a + rgba_pixel[0] * (255 - a)) / 255;
                rgba_pixel[1] = (g * a + rgba_pixel[1] * (255 - a)) / 255;
                rgba_pixel[2] = (b * a + rgba_pixel[2] * (255 - a)) / 255;
                rgba_pixel[3] = 255;
        }
}
#endif

static bool overlay_postprocess(void *state, struct video_frame *in, struct video_frame *out, int req_pitch)
{
        struct state_overlay *s = (struct state_overlay *) state;
        
        assert(req_pitch == vc_get_linesize(in->tiles[0].width, in->color_spec));
        assert(in->tile_count == 1);
        assert(out->tile_count == 1);
        
        long long frame_start_time = 0;
        if (s->perf.enabled) {
                frame_start_time = get_time_in_ns();
        }
        
        // Try to reload overlay if needed (do this FIRST before any caching checks)
        // Don't fail processing if reload fails - continue with existing overlay
        if (!load_overlay_image(s) && s->overlay_data != NULL) {
                // Failed to reload but we have existing data, continue with it
                log_msg(LOG_LEVEL_DEBUG, MOD_NAME "Failed to reload overlay, using cached version\n");
        }
        
        // Copy input to output
        memcpy(out->tiles[0].data, in->tiles[0].data, in->tiles[0].data_len);
        
        // Skip if no overlay loaded
        if (!s->overlay_data) {
                return true;
        }
        
        // Get color space converters
        decoder_t decoder = get_decoder_from_to(out->color_spec, RGBA);
        decoder_t coder = get_decoder_from_to(RGBA, out->color_spec);
        
        if (!decoder || !coder) {
                log_msg(LOG_LEVEL_WARNING, MOD_NAME "Cannot find color space converters for %s\n",
                        get_codec_name(out->color_spec));
                return true;
        }
        
        // Determine overlay dimensions
        unsigned int overlay_width = s->overlay_width;
        unsigned int overlay_height = s->overlay_height;
        unsigned int overlay_stride_width = s->overlay_width;  // Width to use for line stride calculation
        unsigned char *overlay_to_use = s->overlay_data;
        
        if (s->scale_to_fit && (overlay_width != out->tiles[0].width || overlay_height != out->tiles[0].height)) {
                // Note: We always rescale because the overlay might have changed even if dimensions are the same
                // The scaled_overlay is invalidated when we reload the image
                if (s->scaled_overlay && s->scaled_width == out->tiles[0].width && 
                    s->scaled_height == out->tiles[0].height && s->last_codec == out->color_spec) {
                        // Use cached version - this is only valid if the overlay hasn't been reloaded
                        overlay_to_use = s->scaled_overlay;
                        overlay_width = s->scaled_width;
                        overlay_height = s->scaled_height;
                        overlay_stride_width = s->scaled_width;  // Update stride for scaled overlay
                } else {
                        // Need to scale - create or update swscale context
                        // Recreate if output dimensions changed OR source dimensions changed
                        if (s->sws_ctx == NULL || s->scaled_width != out->tiles[0].width || 
                            s->scaled_height != out->tiles[0].height ||
                            s->sws_src_width != s->overlay_width || s->sws_src_height != s->overlay_height) {
                                sws_freeContext(s->sws_ctx);
                                s->sws_ctx = sws_getContext(
                                        s->overlay_width, s->overlay_height, AV_PIX_FMT_RGBA,
                                        out->tiles[0].width, out->tiles[0].height, AV_PIX_FMT_RGBA,
                                        SWS_BILINEAR, NULL, NULL, NULL);
                                s->sws_src_width = s->overlay_width;
                                s->sws_src_height = s->overlay_height;
                                if (!s->sws_ctx) {
                                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to create swscale context\n");
                                        return true;
                                }
                        }
                        
                        // Allocate buffer for scaled overlay
                        size_t scaled_size = (size_t)out->tiles[0].width * out->tiles[0].height * 4;
                        // Check for overflow
                        if (scaled_size / 4 / out->tiles[0].height != out->tiles[0].width) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Scaled overlay size would overflow\n");
                                return true;
                        }
                        if (!s->scaled_overlay || s->scaled_width != out->tiles[0].width || 
                            s->scaled_height != out->tiles[0].height) {
                                free(s->scaled_overlay);
                                s->scaled_overlay = malloc(scaled_size);
                                if (!s->scaled_overlay) {
                                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate memory for scaled overlay - using original size\n");
                                        // Continue without scaling - use original overlay
                                        overlay_to_use = s->overlay_data;
                                        overlay_width = s->overlay_width;
                                        overlay_height = s->overlay_height;
                                        goto skip_scaling;
                                }
                        }
                        
                        // Clear the scaled overlay buffer to ensure no remnants from previous scaling
                        memset(s->scaled_overlay, 0, scaled_size);
                        
                        // Scale the overlay
                        long long scale_start = 0;
                        if (s->perf.enabled) {
                                scale_start = get_time_in_ns();
                        }
                        
                        int src_stride = s->overlay_width * 4;
                        int dst_stride = out->tiles[0].width * 4;
                        sws_scale(s->sws_ctx, 
                                  (const uint8_t * const *)&s->overlay_data, &src_stride, 0, s->overlay_height,
                                  (uint8_t **)&s->scaled_overlay, &dst_stride);
                        
                        if (s->perf.enabled) {
                                s->perf.scale_time_ns += get_time_in_ns() - scale_start;
                                s->perf.scale_operations++;
                        }
                        
                        // Update cache info
                        s->scaled_width = out->tiles[0].width;
                        s->scaled_height = out->tiles[0].height;
                        s->last_codec = out->color_spec;
                        
                        overlay_to_use = s->scaled_overlay;
                        overlay_width = out->tiles[0].width;
                        overlay_height = out->tiles[0].height;
                        overlay_stride_width = out->tiles[0].width;  // Update stride for scaled overlay
                }
        }
        
skip_scaling:
        ; // Empty statement to avoid C23 warning
        // Calculate position
        int pos_x, pos_y;
        
        if (s->use_custom_pos) {
                // Use custom X,Y positioning
                pos_x = s->custom_x;
                pos_y = s->custom_y;
                
                // Handle negative values (position from right/bottom)
                if (pos_x < 0) {
                        pos_x = out->tiles[0].width + pos_x - overlay_width;
                }
                if (pos_y < 0) {
                        pos_y = out->tiles[0].height + pos_y - overlay_height;
                }
        } else {
                // Use preset positions
                switch (s->position) {
                case POS_CENTER:
                        pos_x = (out->tiles[0].width - overlay_width) / 2;
                        pos_y = (out->tiles[0].height - overlay_height) / 2;
                        break;
                case POS_TOP_LEFT:
                        pos_x = 0;
                        pos_y = 0;
                        break;
                case POS_TOP_RIGHT:
                        pos_x = out->tiles[0].width - overlay_width;
                        pos_y = 0;
                        break;
                case POS_BOTTOM_LEFT:
                        pos_x = 0;
                        pos_y = out->tiles[0].height - overlay_height;
                        break;
                case POS_BOTTOM_RIGHT:
                        pos_x = out->tiles[0].width - overlay_width;
                        pos_y = out->tiles[0].height - overlay_height;
                        break;
                default:
                        pos_x = (out->tiles[0].width - overlay_width) / 2;
                        pos_y = (out->tiles[0].height - overlay_height) / 2;
                        break;
                }
        }
        
        // Ensure alignment for pixel formats
        int pf_block = get_pf_block_bytes(out->color_spec);
        if (pf_block > 0) {
                pos_x = (pos_x / pf_block) * pf_block;
        }
        
        // Clamp positions to ensure overlay stays within video bounds
        if (pos_x < 0) pos_x = 0;
        if (pos_y < 0) pos_y = 0;
        if (pos_x > (int)(out->tiles[0].width - 1)) pos_x = out->tiles[0].width - 1;
        if (pos_y > (int)(out->tiles[0].height - 1)) pos_y = out->tiles[0].height - 1;
        
        // Calculate actual overlay dimensions that fit within the video
        int actual_overlay_width = MIN(overlay_width, out->tiles[0].width - pos_x);
        int actual_overlay_height = MIN(overlay_height, out->tiles[0].height - pos_y);
        
        // Ensure overlay width is aligned to pixel format blocks
        if (pf_block > 0 && actual_overlay_width % pf_block != 0) {
                actual_overlay_width = (actual_overlay_width / pf_block) * pf_block;
        }
        
        // Skip if overlay is completely outside the video
        if (actual_overlay_width <= 0 || actual_overlay_height <= 0) {
                return true;
        }
        
        // Process line by line
        size_t rgba_linesize = (size_t)actual_overlay_width * 4;
        // Check for overflow
        if (rgba_linesize / 4 != actual_overlay_width) {
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "RGBA line buffer size would overflow\n");
                return true;
        }
        unsigned char *rgba_line = malloc(rgba_linesize);
        if (!rgba_line) {
                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate memory for RGBA line buffer - continuing without overlay\n");
                return true;  // Continue without overlay
        }
        
        long long blend_start = 0;
        if (s->perf.enabled) {
                blend_start = get_time_in_ns();
        }
        
        for (int y = 0; y < actual_overlay_height; ++y) {
                
                // Decode video line to RGBA
                unsigned char *video_line = (unsigned char *)(out->tiles[0].data + 
                        (y + pos_y) * vc_get_linesize(out->tiles[0].width, out->color_spec) +
                        vc_get_linesize(pos_x, out->color_spec));
                
                long long decode_start = 0;
                if (s->perf.enabled) {
                        decode_start = get_time_in_ns();
                }
                
                decoder(rgba_line, video_line, rgba_linesize, 0, 8, 16);
                
                if (s->perf.enabled) {
                        s->perf.decode_time_ns += get_time_in_ns() - decode_start;
                }
                
                // Blend overlay line
                const unsigned char *overlay_line = overlay_to_use + y * overlay_stride_width * 4;
                
#ifdef __AVX2__
                // Use AVX2 for fastest blending (processes 8 pixels at once)
                blend_line_avx2(rgba_line, overlay_line, actual_overlay_width);
#elif defined(__SSE2__)
                // Use SSE2 for faster blending (processes 4 pixels at once)
                blend_line_sse2(rgba_line, overlay_line, actual_overlay_width);
#elif defined(__ARM_NEON)
                // Use ARM NEON for faster blending (processes 4 pixels at once)
                blend_line_neon(rgba_line, overlay_line, actual_overlay_width);
#else
                // Fallback to scalar blending
                unsigned char *rgba_pixel = rgba_line;
                
                for (int x = 0; x < actual_overlay_width; ++x) {
                        
                        // Get RGBA values from overlay
                        unsigned char r = overlay_line[0];
                        unsigned char g = overlay_line[1];
                        unsigned char b = overlay_line[2];
                        unsigned char a = overlay_line[3];
                        
                        // Alpha blend: out = overlay * alpha + video * (1 - alpha)
                        rgba_pixel[0] = (r * a + rgba_pixel[0] * (255 - a)) / 255;
                        rgba_pixel[1] = (g * a + rgba_pixel[1] * (255 - a)) / 255;
                        rgba_pixel[2] = (b * a + rgba_pixel[2] * (255 - a)) / 255;
                        rgba_pixel[3] = 255;  // Keep output fully opaque
                        
                        overlay_line += 4;  // Move to next RGBA pixel
                        rgba_pixel += 4;    // Move to next RGBA pixel
                }
#endif
                
                // Encode back to video format
                long long encode_start = 0;
                if (s->perf.enabled) {
                        encode_start = get_time_in_ns();
                }
                
                coder(video_line, rgba_line, 
                      vc_get_linesize(actual_overlay_width, out->color_spec), 0, 8, 16);
                
                if (s->perf.enabled) {
                        s->perf.encode_time_ns += get_time_in_ns() - encode_start;
                }
        }
        
        if (s->perf.enabled) {
                s->perf.blend_time_ns += get_time_in_ns() - blend_start;
        }
        
        free(rgba_line);
        
        // Update performance stats
        if (s->perf.enabled) {
                s->perf.total_time_ns += get_time_in_ns() - frame_start_time;
                s->perf.frame_count++;
                
                // Report every 5 seconds
                time_t now = time(NULL);
                if (now - s->perf.last_report_time >= 5 && s->perf.frame_count > 0) {
                        // Calculate averages
                        double avg_total_ms = (double)s->perf.total_time_ns / s->perf.frame_count / 1000000.0;
                        double avg_load_ms = (double)s->perf.load_time_ns / (s->perf.overlay_reloads ? s->perf.overlay_reloads : 1) / 1000000.0;
                        double avg_scale_ms = (double)s->perf.scale_time_ns / (s->perf.scale_operations ? s->perf.scale_operations : 1) / 1000000.0;
                        double avg_blend_ms = (double)s->perf.blend_time_ns / s->perf.frame_count / 1000000.0;
                        double avg_decode_ms = (double)s->perf.decode_time_ns / s->perf.frame_count / 1000000.0;
                        double avg_encode_ms = (double)s->perf.encode_time_ns / s->perf.frame_count / 1000000.0;
                        
                        // Calculate percentage of time spent in each operation
                        double decode_pct = (s->perf.decode_time_ns * 100.0) / s->perf.total_time_ns;
                        double encode_pct = (s->perf.encode_time_ns * 100.0) / s->perf.total_time_ns;
                        double blend_pct = (s->perf.blend_time_ns * 100.0) / s->perf.total_time_ns;
                        double scale_pct = (s->perf.scale_time_ns * 100.0) / s->perf.total_time_ns;
                        double load_pct = (s->perf.load_time_ns * 100.0) / s->perf.total_time_ns;
                        
                        log_msg(LOG_LEVEL_INFO, MOD_NAME "Performance stats (%ld frames in %ld seconds):\n", 
                                s->perf.frame_count, now - s->perf.last_report_time);
                        log_msg(LOG_LEVEL_INFO, MOD_NAME "  Average per frame: %.3f ms (%.1f FPS max)\n", 
                                avg_total_ms, 1000.0 / avg_total_ms);
                        log_msg(LOG_LEVEL_INFO, MOD_NAME "  Load:   %.3f ms/op (%ld ops, %.1f%%)\n", 
                                avg_load_ms, s->perf.overlay_reloads, load_pct);
                        log_msg(LOG_LEVEL_INFO, MOD_NAME "  Scale:  %.3f ms/op (%ld ops, %.1f%%)\n", 
                                avg_scale_ms, s->perf.scale_operations, scale_pct);
                        log_msg(LOG_LEVEL_INFO, MOD_NAME "  Decode: %.3f ms/frame (%.1f%%)\n", 
                                avg_decode_ms, decode_pct);
                        log_msg(LOG_LEVEL_INFO, MOD_NAME "  Blend:  %.3f ms/frame (%.1f%%)\n", 
                                avg_blend_ms, blend_pct);
                        log_msg(LOG_LEVEL_INFO, MOD_NAME "  Encode: %.3f ms/frame (%.1f%%)\n", 
                                avg_encode_ms, encode_pct);
                        
                        // Check if overlay is present
                        if (s->overlay_data) {
                                log_msg(LOG_LEVEL_INFO, MOD_NAME "  Overlay: %dx%d -> %dx%d (%s)\n",
                                        s->overlay_width, s->overlay_height,
                                        s->scaled_width ? s->scaled_width : s->overlay_width,
                                        s->scaled_height ? s->scaled_height : s->overlay_height,
                                        s->scale_to_fit ? "scaled" : "original");
                        } else {
                                log_msg(LOG_LEVEL_INFO, MOD_NAME "  Overlay: not loaded\n");
                        }
                        
                        s->perf.last_report_time = now;
                }
        }
        
        return true;
}

static void overlay_done(void *state)
{
        struct state_overlay *s = (struct state_overlay *) state;
        
        // Print final performance report if enabled
        if (s->perf.enabled && s->perf.frame_count > 0) {
                log_msg(LOG_LEVEL_INFO, MOD_NAME "Final performance summary:\n");
                log_msg(LOG_LEVEL_INFO, MOD_NAME "  Total frames processed: %ld\n", s->perf.frame_count);
                log_msg(LOG_LEVEL_INFO, MOD_NAME "  Total processing time: %.3f seconds\n", 
                        (double)s->perf.total_time_ns / 1000000000.0);
                log_msg(LOG_LEVEL_INFO, MOD_NAME "  Average FPS capacity: %.1f\n", 
                        (double)s->perf.frame_count * 1000000000.0 / s->perf.total_time_ns);
        }
        
        vf_free(s->in);
        free(s->overlay_data);
        free(s->scaled_overlay);
        free(s->overlay_path);
        sws_freeContext(s->sws_ctx);
        free(s);
}

static void overlay_get_out_desc(void *state, struct video_desc *out, int *in_display_mode, int *out_frames)
{
        struct state_overlay *s = (struct state_overlay *) state;
        
        *out = s->saved_desc;
        *in_display_mode = DISPLAY_PROPERTY_VIDEO_MERGED;
        *out_frames = 1;
}

static const struct vo_postprocess_info vo_pp_overlay_info = {
        overlay_init,
        overlay_postprocess_reconfigure,
        overlay_getf,
        overlay_get_out_desc,
        overlay_get_property,
        overlay_postprocess,
        overlay_done,
};

REGISTER_MODULE(overlay, &vo_pp_overlay_info, LIBRARY_CLASS_VIDEO_POSTPROCESS, VO_PP_ABI_VERSION);