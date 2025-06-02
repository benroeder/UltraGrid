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
#include <math.h>
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
        
        // Soft edge settings
        int edge_width;                 // Width of soft edge in pixels (0 = disabled)
        enum edge_type edge_type;       // Type of gradient (default: linear)
        int edge_sides;                 // Bitmask of which edges to soften (default: all)
        
        // Performance monitoring
        struct {
                long long total_time_ns;        // Total processing time
                long long load_time_ns;         // Time spent loading images
                long long scale_time_ns;        // Time spent scaling
                long long blend_time_ns;        // Time spent blending
                long long decode_time_ns;       // Time spent decoding to RGBA
                long long encode_time_ns;       // Time spent encoding from RGBA
                long frame_count;               // Number of frames processed
                long native_blend_count;        // Number of frames using native blending
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
        color_printf(TERM_BOLD TERM_FG_RED "\t-p overlay" TERM_FG_RESET "[:file=<path>][:position=<pos>][:x=<x>][:y=<y>][:scale=<mode>][:edge=<width>][:edge_type=<type>][:edge_sides=<sides>][:perf]\n" TERM_RESET);
        color_printf("\nParameters:\n");
        color_printf(TERM_BOLD "\tfile=<path>" TERM_RESET " - Path to PAM overlay image (default: overlay.pam)\n");
        color_printf(TERM_BOLD "\tposition=<pos>" TERM_RESET " - Overlay position: center, topleft, topright, bottomleft, bottomright (default: center)\n");
        color_printf(TERM_BOLD "\tx=<x>" TERM_RESET " - Custom X position in pixels (overrides position parameter)\n");
        color_printf(TERM_BOLD "\ty=<y>" TERM_RESET " - Custom Y position in pixels (overrides position parameter)\n");
        color_printf(TERM_BOLD "\tscale=<mode>" TERM_RESET " - Scaling mode: fit, none (default: fit)\n");
        color_printf(TERM_BOLD "\tedge=<width>" TERM_RESET " - Soft edge width in pixels (default: 0 = disabled)\n");
        color_printf(TERM_BOLD "\tedge_type=<type>" TERM_RESET " - Edge gradient type: linear, gaussian, cosine (default: linear)\n");
        color_printf(TERM_BOLD "\tedge_sides=<sides>" TERM_RESET " - Which edges to soften: all, left, right, top, bottom (default: all)\n");
        color_printf("                                Can combine multiple sides with commas: left,right\n");
        color_printf(TERM_BOLD "\tperf" TERM_RESET " - Enable performance monitoring (reports every 5 seconds)\n");
        color_printf("\nExamples:\n");
        color_printf(TERM_BOLD "\tuv -t testcard -p overlay:file=logo.pam:position=topright -d sdl\n" TERM_RESET);
        color_printf(TERM_BOLD "\tuv -t testcard -p overlay:file=logo.pam:x=100:y=50:perf -d sdl\n" TERM_RESET);
        color_printf(TERM_BOLD "\tuv -t testcard -p overlay:file=logo.pam:edge=50:edge_type=gaussian -d sdl\n" TERM_RESET);
        color_printf("\nNotes:\n");
        color_printf(" - Overlay image should be in PAM format with alpha channel\n");
        color_printf(" - Image is reloaded automatically when file is modified\n");
        color_printf(" - Uses alpha channel for transparency\n");
        color_printf(" - Negative X/Y values position from right/bottom edges\n");
        color_printf(" - Soft edges create a gradual transparency transition at overlay borders\n");
        color_printf(" - Soft edges are applied to the overlay image edges, not recommended for logos with scale=fit\n");
}

static const char *edge_type_to_string(enum edge_type type) {
        switch (type) {
        case EDGE_LINEAR:
                return "linear";
        case EDGE_GAUSSIAN:
                return "gaussian";
        case EDGE_COSINE:
                return "cosine";
        default:
                return "unknown";
        }
}

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
        // No processing if edge width is 0 or negative (maintains current behavior)
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
                                alpha_modifier = MIN(alpha_modifier, apply_gradient(t, type));
                        }
                        if (edge_sides & EDGE_RIGHT && dist_right < edge_width) {
                                float t = (float)dist_right / edge_width;
                                alpha_modifier = MIN(alpha_modifier, apply_gradient(t, type));
                        }
                        if (edge_sides & EDGE_TOP && dist_top < edge_width) {
                                float t = (float)dist_top / edge_width;
                                alpha_modifier = MIN(alpha_modifier, apply_gradient(t, type));
                        }
                        if (edge_sides & EDGE_BOTTOM && dist_bottom < edge_width) {
                                float t = (float)dist_bottom / edge_width;
                                alpha_modifier = MIN(alpha_modifier, apply_gradient(t, type));
                        }
                        
                        // Apply alpha modification
                        int pixel_offset = (y * width + x) * 4;
                        rgba_data[pixel_offset + 3] = (unsigned char)(rgba_data[pixel_offset + 3] * alpha_modifier);
                }
        }
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
        
        // Apply soft edges if configured
        // Only processes if edge_width > 0, maintaining current behavior otherwise
        if (s->edge_width > 0) {
                apply_soft_edges(s->overlay_data, s->overlay_width, s->overlay_height,
                                 s->edge_width, s->edge_type, s->edge_sides);
                log_msg(LOG_LEVEL_INFO, MOD_NAME "Applied soft edges: width=%d, type=%s\n", 
                        s->edge_width, edge_type_to_string(s->edge_type));
        }
        
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
        
        // Soft edge defaults (maintain current behavior when not used)
        s->edge_width = 0;              // Disabled by default
        s->edge_type = EDGE_LINEAR;     // Default gradient type
        s->edge_sides = EDGE_ALL;       // All sides by default
        
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
                        } else if (strncasecmp(item, "edge=", 5) == 0) {
                                s->edge_width = atoi(item + 5);
                                if (s->edge_width < 0) {
                                        log_msg(LOG_LEVEL_WARNING, MOD_NAME "Edge width cannot be negative, setting to 0\n");
                                        s->edge_width = 0;
                                }
                        } else if (strncasecmp(item, "edge_type=", 10) == 0) {
                                const char *type = item + 10;
                                if (strcasecmp(type, "linear") == 0) {
                                        s->edge_type = EDGE_LINEAR;
                                } else if (strcasecmp(type, "gaussian") == 0) {
                                        s->edge_type = EDGE_GAUSSIAN;
                                } else if (strcasecmp(type, "cosine") == 0) {
                                        s->edge_type = EDGE_COSINE;
                                } else {
                                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Unknown edge type: %s\n", type);
                                        free(tmp);
                                        free(s->overlay_path);
                                        free(s);
                                        return NULL;
                                }
                        } else if (strncasecmp(item, "edge_sides=", 11) == 0) {
                                const char *sides_str = item + 11;
                                s->edge_sides = 0;
                                
                                // Parse comma-separated list of sides
                                char *sides_copy = strdup(sides_str);
                                if (!sides_copy) {
                                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Memory allocation failed\n");
                                        free(tmp);
                                        free(s->overlay_path);
                                        free(s);
                                        return NULL;
                                }
                                
                                char *side_token, *side_save_ptr;
                                side_token = strtok_r(sides_copy, ",", &side_save_ptr);
                                while (side_token) {
                                        if (strcasecmp(side_token, "all") == 0) {
                                                s->edge_sides = EDGE_ALL;
                                                break;  // All overrides individual sides
                                        } else if (strcasecmp(side_token, "left") == 0) {
                                                s->edge_sides |= EDGE_LEFT;
                                        } else if (strcasecmp(side_token, "right") == 0) {
                                                s->edge_sides |= EDGE_RIGHT;
                                        } else if (strcasecmp(side_token, "top") == 0) {
                                                s->edge_sides |= EDGE_TOP;
                                        } else if (strcasecmp(side_token, "bottom") == 0) {
                                                s->edge_sides |= EDGE_BOTTOM;
                                        } else {
                                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Unknown edge side: %s\n", side_token);
                                                free(sides_copy);
                                                free(tmp);
                                                free(s->overlay_path);
                                                free(s);
                                                return NULL;
                                        }
                                        side_token = strtok_r(NULL, ",", &side_save_ptr);
                                }
                                free(sides_copy);
                                
                                // Default to all sides if none specified
                                if (s->edge_sides == 0) {
                                        s->edge_sides = EDGE_ALL;
                                }
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
        
        // Report which implementation is being used
        log_msg(LOG_LEVEL_INFO, MOD_NAME "Using %s for alpha blending\n", 
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
        
        // Check if we can do native format blending (before the loop)
        bool used_native_blend = false;
        if (out->color_spec == UYVY || out->color_spec == YUYV || out->color_spec == RGB || 
            out->color_spec == v210 || out->color_spec == R10k || 
            out->color_spec == R12L || out->color_spec == RGBA || out->color_spec == Y416 ||
            out->color_spec == I420) {
                used_native_blend = true;
        }
        
        // Handle I420 separately since it's planar
        if (out->color_spec == I420) {
                // I420 is planar: Y plane (full res), U plane (half res), V plane (half res)
                int y_width = out->tiles[0].width;
                int y_height = out->tiles[0].height;
                int uv_width = y_width / 2;
                int uv_height = y_height / 2;
                
                // Calculate plane offsets
                unsigned char *y_plane = (unsigned char *)out->tiles[0].data;
                unsigned char *u_plane = y_plane + (y_width * y_height);
                unsigned char *v_plane = u_plane + (uv_width * uv_height);
                
                // Allocate buffers for converted overlay
                size_t overlay_y_size = actual_overlay_width * actual_overlay_height;
                size_t overlay_uv_size = (actual_overlay_width / 2) * (actual_overlay_height / 2);
                unsigned char *overlay_y = malloc(overlay_y_size);
                unsigned char *overlay_u = malloc(overlay_uv_size);
                unsigned char *overlay_v = malloc(overlay_uv_size);
                unsigned char *alpha_full = malloc(overlay_y_size);
                
                if (!overlay_y || !overlay_u || !overlay_v || !alpha_full) {
                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate I420 buffers\n");
                        free(overlay_y);
                        free(overlay_u);
                        free(overlay_v);
                        free(alpha_full);
                        goto skip_i420;
                }
                
                // Convert RGBA overlay to I420 and extract alpha
                decoder_t rgba_to_i420 = get_decoder_from_to(RGBA, I420);
                if (!rgba_to_i420) {
                        log_msg(LOG_LEVEL_ERROR, MOD_NAME "No RGBA to I420 converter available\n");
                        free(overlay_y);
                        free(overlay_u);
                        free(overlay_v);
                        free(alpha_full);
                        goto skip_i420;
                }
                
                // Extract alpha channel to full resolution buffer
                for (int y = 0; y < actual_overlay_height; y++) {
                        const unsigned char *overlay_line = overlay_to_use + y * overlay_stride_width * 4;
                        for (int x = 0; x < actual_overlay_width; x++) {
                                alpha_full[y * actual_overlay_width + x] = overlay_line[x * 4 + 3];
                        }
                }
                
                // Convert overlay to I420
                struct video_frame src_frame, dst_frame;
                src_frame.tiles[0].data = (char *)overlay_to_use;
                src_frame.tiles[0].width = actual_overlay_width;
                src_frame.tiles[0].height = actual_overlay_height;
                src_frame.color_spec = RGBA;
                
                dst_frame.tiles[0].data = (char *)overlay_y;
                dst_frame.tiles[0].width = actual_overlay_width;
                dst_frame.tiles[0].height = actual_overlay_height;
                dst_frame.color_spec = I420;
                
                rgba_to_i420((unsigned char *)dst_frame.tiles[0].data, 
                            (unsigned char *)src_frame.tiles[0].data,
                            vc_get_datalen(actual_overlay_width, actual_overlay_height, I420),
                            0, 0, 0);
                
                // Set up plane pointers for converted overlay
                unsigned char *conv_y_plane = overlay_y;
                unsigned char *conv_u_plane = overlay_y + overlay_y_size;
                unsigned char *conv_v_plane = conv_u_plane + overlay_uv_size;
                
                // We need to blend the overlay into the video frame at the correct position
                // For I420, we need to handle the fact that it's planar with different resolutions
                
                // Blend Y plane line by line
                for (int y = 0; y < actual_overlay_height; y++) {
                        if (pos_y + y >= 0 && pos_y + y < y_height) {
                                unsigned char *dst_y_line = y_plane + (pos_y + y) * y_width + pos_x;
                                unsigned char *src_y_line = conv_y_plane + y * actual_overlay_width;
                                unsigned char *alpha_line = alpha_full + y * actual_overlay_width;
                                
                                // Blend this line
                                for (int x = 0; x < actual_overlay_width; x++) {
                                        if (pos_x + x >= 0 && pos_x + x < y_width) {
                                                uint8_t a = alpha_line[x];
                                                dst_y_line[x] = (src_y_line[x] * a + dst_y_line[x] * (255 - a)) / 255;
                                        }
                                }
                        }
                }
                
                // Blend U and V planes (half resolution)
                int uv_pos_x = pos_x / 2;
                int uv_pos_y = pos_y / 2;
                int uv_overlay_width = actual_overlay_width / 2;
                int uv_overlay_height = actual_overlay_height / 2;
                
                for (int y = 0; y < uv_overlay_height; y++) {
                        if (uv_pos_y + y >= 0 && uv_pos_y + y < uv_height) {
                                unsigned char *dst_u_line = u_plane + (uv_pos_y + y) * uv_width + uv_pos_x;
                                unsigned char *dst_v_line = v_plane + (uv_pos_y + y) * uv_width + uv_pos_x;
                                unsigned char *src_u_line = conv_u_plane + y * uv_overlay_width;
                                unsigned char *src_v_line = conv_v_plane + y * uv_overlay_width;
                                
                                // For chroma, we need to average the alpha values from the corresponding 2x2 block
                                for (int x = 0; x < uv_overlay_width; x++) {
                                        if (uv_pos_x + x >= 0 && uv_pos_x + x < uv_width) {
                                                // Average alpha from 2x2 block
                                                int y2 = y * 2;
                                                int x2 = x * 2;
                                                uint16_t a00 = alpha_full[y2 * actual_overlay_width + x2];
                                                uint16_t a01 = (x2 + 1 < actual_overlay_width) ? 
                                                               alpha_full[y2 * actual_overlay_width + x2 + 1] : a00;
                                                uint16_t a10 = (y2 + 1 < actual_overlay_height) ? 
                                                               alpha_full[(y2 + 1) * actual_overlay_width + x2] : a00;
                                                uint16_t a11 = ((x2 + 1 < actual_overlay_width) && (y2 + 1 < actual_overlay_height)) ? 
                                                               alpha_full[(y2 + 1) * actual_overlay_width + x2 + 1] : a00;
                                                
                                                uint8_t avg_alpha = (a00 + a01 + a10 + a11) / 4;
                                                
                                                // Blend chroma
                                                dst_u_line[x] = (src_u_line[x] * avg_alpha + dst_u_line[x] * (255 - avg_alpha)) / 255;
                                                dst_v_line[x] = (src_v_line[x] * avg_alpha + dst_v_line[x] * (255 - avg_alpha)) / 255;
                                        }
                                }
                        }
                }
                
                free(overlay_y);
                free(overlay_u);
                free(overlay_v);
                free(alpha_full);
                
                goto skip_line_processing;
        }
        
skip_i420:
        for (int y = 0; y < actual_overlay_height; ++y) {
                unsigned char *video_line = (unsigned char *)(out->tiles[0].data + 
                        (y + pos_y) * vc_get_linesize(out->tiles[0].width, out->color_spec) +
                        vc_get_linesize(pos_x, out->color_spec));
                
                const unsigned char *overlay_line = overlay_to_use + y * overlay_stride_width * 4;
                
                // Check if we can do native format blending
                if (out->color_spec == UYVY) {
                        // Native UYVY blending
                        // First extract alpha channel from RGBA overlay
                        unsigned char *alpha_line = malloc(actual_overlay_width);
                        if (!alpha_line) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate alpha buffer\n");
                                continue;
                        }
                        
                        // Extract alpha channel
                        for (int x = 0; x < actual_overlay_width; x++) {
                                alpha_line[x] = overlay_line[x * 4 + 3];
                        }
                        
                        // Convert overlay from RGBA to UYVY
                        size_t uyvy_linesize = vc_get_linesize(actual_overlay_width, UYVY);
                        unsigned char *overlay_uyvy = malloc(uyvy_linesize);
                        if (!overlay_uyvy) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate UYVY buffer\n");
                                free(alpha_line);
                                continue;
                        }
                        
                        // Get converter from RGBA to UYVY
                        decoder_t rgba_to_uyvy = get_decoder_from_to(RGBA, UYVY);
                        if (rgba_to_uyvy) {
                                rgba_to_uyvy(overlay_uyvy, overlay_line, uyvy_linesize, 0, 8, 16);
                        } else {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "No RGBA to UYVY converter available\n");
                                free(overlay_uyvy);
                                free(alpha_line);
                                continue;
                        }
                        
                        // Do native UYVY blending
                        alpha_blend_uyvy(video_line, overlay_uyvy, alpha_line, actual_overlay_width);
                        
                        free(overlay_uyvy);
                        free(alpha_line);
                        
                } else if (out->color_spec == YUYV) {
                        // Native YUYV blending
                        // First extract alpha channel from RGBA overlay
                        unsigned char *alpha_line = malloc(actual_overlay_width);
                        if (!alpha_line) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate alpha buffer\n");
                                continue;
                        }
                        
                        // Extract alpha channel
                        for (int x = 0; x < actual_overlay_width; x++) {
                                alpha_line[x] = overlay_line[x * 4 + 3];
                        }
                        
                        // Convert overlay from RGBA to YUYV
                        size_t yuyv_linesize = vc_get_linesize(actual_overlay_width, YUYV);
                        unsigned char *overlay_yuyv = malloc(yuyv_linesize);
                        if (!overlay_yuyv) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate YUYV buffer\n");
                                free(alpha_line);
                                continue;
                        }
                        
                        // Get converter from RGBA to YUYV
                        decoder_t rgba_to_yuyv = get_decoder_from_to(RGBA, YUYV);
                        if (rgba_to_yuyv) {
                                rgba_to_yuyv(overlay_yuyv, overlay_line, yuyv_linesize, 0, 8, 16);
                        } else {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "No RGBA to YUYV converter available\n");
                                free(overlay_yuyv);
                                free(alpha_line);
                                continue;
                        }
                        
                        // Do native YUYV blending
                        alpha_blend_yuyv(video_line, overlay_yuyv, alpha_line, actual_overlay_width);
                        
                        free(overlay_yuyv);
                        free(alpha_line);
                        
                } else if (out->color_spec == RGB) {
                        // Native RGB blending
                        // Extract alpha channel
                        unsigned char *alpha_line = malloc(actual_overlay_width);
                        if (!alpha_line) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate alpha buffer\n");
                                continue;
                        }
                        
                        for (int x = 0; x < actual_overlay_width; x++) {
                                alpha_line[x] = overlay_line[x * 4 + 3];
                        }
                        
                        // Convert overlay from RGBA to RGB
                        size_t rgb_linesize = actual_overlay_width * 3;
                        unsigned char *overlay_rgb = malloc(rgb_linesize);
                        if (!overlay_rgb) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate RGB buffer\n");
                                free(alpha_line);
                                continue;
                        }
                        
                        // Get converter from RGBA to RGB
                        decoder_t rgba_to_rgb = get_decoder_from_to(RGBA, RGB);
                        if (rgba_to_rgb) {
                                rgba_to_rgb(overlay_rgb, overlay_line, rgb_linesize, 0, 8, 16);
                        } else {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "No RGBA to RGB converter available\n");
                                free(overlay_rgb);
                                free(alpha_line);
                                continue;
                        }
                        
                        // Do native RGB blending
                        alpha_blend_rgb(video_line, overlay_rgb, alpha_line, actual_overlay_width);
                        
                        free(overlay_rgb);
                        free(alpha_line);
                        
                } else if (out->color_spec == v210) {
                        // Native v210 blending
                        unsigned char *alpha_line = malloc(actual_overlay_width);
                        if (!alpha_line) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate alpha buffer\n");
                                continue;
                        }
                        
                        for (int x = 0; x < actual_overlay_width; x++) {
                                alpha_line[x] = overlay_line[x * 4 + 3];
                        }
                        
                        // Convert overlay from RGBA to v210
                        size_t v210_linesize = vc_get_linesize(actual_overlay_width, v210);
                        unsigned char *overlay_v210 = malloc(v210_linesize);
                        if (!overlay_v210) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate v210 buffer\n");
                                free(alpha_line);
                                continue;
                        }
                        
                        // Get converter from RGBA to v210
                        decoder_t rgba_to_v210 = get_decoder_from_to(RGBA, v210);
                        if (rgba_to_v210) {
                                rgba_to_v210(overlay_v210, overlay_line, v210_linesize, 0, 8, 16);
                        } else {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "No RGBA to v210 converter available\n");
                                free(overlay_v210);
                                free(alpha_line);
                                continue;
                        }
                        
                        // Do native v210 blending
                        alpha_blend_v210(video_line, overlay_v210, alpha_line, actual_overlay_width);
                        
                        free(overlay_v210);
                        free(alpha_line);
                        
                } else if (out->color_spec == R10k) {
                        // Native R10k blending
                        unsigned char *alpha_line = malloc(actual_overlay_width);
                        if (!alpha_line) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate alpha buffer\n");
                                continue;
                        }
                        
                        for (int x = 0; x < actual_overlay_width; x++) {
                                alpha_line[x] = overlay_line[x * 4 + 3];
                        }
                        
                        // Convert overlay from RGBA to R10k
                        size_t r10k_linesize = vc_get_linesize(actual_overlay_width, R10k);
                        unsigned char *overlay_r10k = malloc(r10k_linesize);
                        if (!overlay_r10k) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate R10k buffer\n");
                                free(alpha_line);
                                continue;
                        }
                        
                        // Get converter from RGBA to R10k
                        decoder_t rgba_to_r10k = get_decoder_from_to(RGBA, R10k);
                        if (rgba_to_r10k) {
                                rgba_to_r10k(overlay_r10k, overlay_line, r10k_linesize, 0, 8, 16);
                        } else {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "No RGBA to R10k converter available\n");
                                free(overlay_r10k);
                                free(alpha_line);
                                continue;
                        }
                        
                        // Do native R10k blending
                        alpha_blend_r10k(video_line, overlay_r10k, alpha_line, actual_overlay_width);
                        
                        free(overlay_r10k);
                        free(alpha_line);
                        
                } else if (out->color_spec == R12L) {
                        // Native R12L blending
                        unsigned char *alpha_line = malloc(actual_overlay_width);
                        if (!alpha_line) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate alpha buffer\n");
                                continue;
                        }
                        
                        for (int x = 0; x < actual_overlay_width; x++) {
                                alpha_line[x] = overlay_line[x * 4 + 3];
                        }
                        
                        // Convert overlay from RGBA to R12L
                        size_t r12l_linesize = vc_get_linesize(actual_overlay_width, R12L);
                        unsigned char *overlay_r12l = malloc(r12l_linesize);
                        if (!overlay_r12l) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate R12L buffer\n");
                                free(alpha_line);
                                continue;
                        }
                        
                        // Get converter from RGBA to R12L
                        decoder_t rgba_to_r12l = get_decoder_from_to(RGBA, R12L);
                        if (rgba_to_r12l) {
                                rgba_to_r12l(overlay_r12l, overlay_line, r12l_linesize, 0, 8, 16);
                        } else {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "No RGBA to R12L converter available\n");
                                free(overlay_r12l);
                                free(alpha_line);
                                continue;
                        }
                        
                        // Do native R12L blending
                        alpha_blend_r12l(video_line, overlay_r12l, alpha_line, actual_overlay_width);
                        
                        free(overlay_r12l);
                        free(alpha_line);
                        
                } else if (out->color_spec == RGBA) {
                        // Direct RGBA blending - no conversion needed
                        alpha_blend_rgba(video_line, overlay_line, actual_overlay_width);
                        
                } else if (out->color_spec == Y416) {
                        // Native Y416 blending - Y416 has its own alpha channel
                        // Convert overlay from RGBA to Y416
                        size_t y416_linesize = vc_get_linesize(actual_overlay_width, Y416);
                        unsigned char *overlay_y416 = malloc(y416_linesize);
                        if (!overlay_y416) {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "Failed to allocate Y416 buffer\n");
                                continue;
                        }
                        
                        // Get converter from RGBA to Y416
                        decoder_t rgba_to_y416 = get_decoder_from_to(RGBA, Y416);
                        if (rgba_to_y416) {
                                rgba_to_y416(overlay_y416, overlay_line, y416_linesize, 0, 8, 16);
                        } else {
                                log_msg(LOG_LEVEL_ERROR, MOD_NAME "No RGBA to Y416 converter available\n");
                                free(overlay_y416);
                                continue;
                        }
                        
                        // Do native Y416 blending (Y416 contains alpha in the format)
                        alpha_blend_y416(video_line, overlay_y416, actual_overlay_width);
                        
                        free(overlay_y416);
                        
                } else {
                        // Fallback: Convert to RGBA, blend, convert back
                        long long decode_start = 0;
                        if (s->perf.enabled) {
                                decode_start = get_time_in_ns();
                        }
                        
                        decoder(rgba_line, video_line, rgba_linesize, 0, 8, 16);
                        
                        if (s->perf.enabled) {
                                s->perf.decode_time_ns += get_time_in_ns() - decode_start;
                        }
                        
                        // Use optimized alpha blending from utils
                        alpha_blend_rgba(rgba_line, overlay_line, actual_overlay_width);
                        
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
        }
        
skip_line_processing:
        if (s->perf.enabled) {
                s->perf.blend_time_ns += get_time_in_ns() - blend_start;
                if (used_native_blend) {
                        s->perf.native_blend_count++;
                }
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
                        log_msg(LOG_LEVEL_INFO, MOD_NAME "  Blend:  %.3f ms/frame (%.1f%%, %ld/%ld native)\n", 
                                avg_blend_ms, blend_pct, s->perf.native_blend_count, s->perf.frame_count);
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