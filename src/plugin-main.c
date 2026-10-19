/*
 * OBS Beauty Filter (macOS) - GPU-based camera beautification
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SDR shader beauty with optional asynchronous Apple Vision face landmarks.
 */
#include <obs-module.h>
#include <graphics/vec2.h>
#include <stdint.h>
#include <util/platform.h>
#include <graphics/vec4.h>
#include <math.h>
#include "face-tracker.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-beauty-macos", "en-US")

#define FILTER_ID "ducafecat_beauty_filter"
#define SET_SMOOTH "smooth"
#define SET_RADIUS "radius"
#define SET_WHITEN "whiten"
#define SET_ROSY "rosy"
#define SET_SHARPEN "sharpen"
#define SET_SKIN_ONLY "skin_only"
#define SET_FACE_ONLY "face_only"
#define SET_PROTECT "feature_protection"
#define SET_DEBUG "mask_debug"

struct beauty_filter {
    obs_source_t *source;
    gs_effect_t *effect;
    gs_eparam_t *texel_size_param;
    gs_eparam_t *smooth_param;
    gs_eparam_t *radius_param;
    gs_eparam_t *whiten_param;
    gs_eparam_t *rosy_param;
    gs_eparam_t *sharpen_param;
    gs_eparam_t *skin_only_param;

    struct face_tracker *tracker;
    gs_texrender_t *detect_render;
    gs_stagesurf_t *stage;
    gs_texture_t *mask_texture;
    uint8_t *mask_pixels;
    uint32_t input_width, input_height, detect_width, detect_height;
    uint32_t mask_width, mask_height;
    uint64_t generation, last_submit, mask_timestamp;
    bool face_only, protect, debug;
    bool last_face_only;
    gs_eparam_t *mask_param, *face_only_param, *protect_param, *freshness_param, *debug_param;

    float smooth;
    float radius;
    float whiten;
    float rosy;
    float sharpen;
    float skin_only;
};

static const char *beauty_name(void *unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("FilterName");
}

static void beauty_defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, SET_SMOOTH, 40);
    obs_data_set_default_int(settings, SET_RADIUS, 3);
    obs_data_set_default_int(settings, SET_WHITEN, 12);
    obs_data_set_default_int(settings, SET_ROSY, 8);
    obs_data_set_default_int(settings, SET_SHARPEN, 10);
    obs_data_set_default_bool(settings, SET_SKIN_ONLY, true);
    obs_data_set_default_bool(settings, SET_FACE_ONLY, true);
    obs_data_set_default_bool(settings, SET_PROTECT, true);
    obs_data_set_default_bool(settings, SET_DEBUG, false);
}

static void beauty_update(void *data, obs_data_t *settings)
{
    struct beauty_filter *filter = data;
    filter->face_only = obs_data_get_bool(settings, SET_FACE_ONLY);
    filter->protect = obs_data_get_bool(settings, SET_PROTECT);
    filter->debug = obs_data_get_bool(settings, SET_DEBUG);
    filter->smooth = (float)obs_data_get_int(settings, SET_SMOOTH) / 100.0f;
    filter->radius = (float)obs_data_get_int(settings, SET_RADIUS);
    filter->whiten = (float)obs_data_get_int(settings, SET_WHITEN) / 100.0f;
    filter->rosy = (float)obs_data_get_int(settings, SET_ROSY) / 100.0f;
    filter->sharpen = (float)obs_data_get_int(settings, SET_SHARPEN) / 100.0f;
    filter->skin_only = obs_data_get_bool(settings, SET_SKIN_ONLY) ? 1.0f : 0.0f;
}

static obs_properties_t *beauty_properties(void *unused)
{
    UNUSED_PARAMETER(unused);
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_text(props, "info", obs_module_text("Info"), OBS_TEXT_INFO);
    obs_properties_add_int_slider(props, SET_SMOOTH, obs_module_text("Smooth"), 0, 100, 1);
    obs_properties_add_int_slider(props, SET_RADIUS, obs_module_text("Radius"), 1, 6, 1);
    obs_properties_add_int_slider(props, SET_WHITEN, obs_module_text("Whiten"), 0, 100, 1);
    obs_properties_add_int_slider(props, SET_ROSY, obs_module_text("Rosy"), 0, 100, 1);
    obs_properties_add_int_slider(props, SET_SHARPEN, obs_module_text("Sharpen"), 0, 100, 1);
    obs_properties_add_bool(props, SET_SKIN_ONLY, obs_module_text("SkinOnly"));
    obs_properties_add_bool(props, SET_FACE_ONLY, obs_module_text("FaceOnly"));
    obs_properties_add_bool(props, SET_PROTECT, obs_module_text("FeatureProtection"));
    obs_properties_add_bool(props, SET_DEBUG, obs_module_text("MaskDebug"));
    return props;
}

static void beauty_destroy(void *data)
{
    struct beauty_filter *filter = data;
    if (!filter)
        return;
    face_tracker_destroy(filter->tracker);
    obs_enter_graphics();
    gs_effect_destroy(filter->effect);
    gs_texrender_destroy(filter->detect_render);
    gs_stagesurface_destroy(filter->stage);
    gs_texture_destroy(filter->mask_texture);
    obs_leave_graphics();
    bfree(filter->mask_pixels);
    bfree(filter);
}

static void *beauty_create(obs_data_t *settings, obs_source_t *source)
{
    struct beauty_filter *filter = bzalloc(sizeof(*filter));
    if (!filter)
        return NULL;
    filter->source = source;
    /* Defaults are not user values: distinguish a new filter from a saved v0.1 filter. */
    if (!obs_data_has_user_value(settings, SET_FACE_ONLY)) {
        const char *old_keys[] = {SET_SMOOTH, SET_RADIUS, SET_WHITEN, SET_ROSY, SET_SHARPEN, SET_SKIN_ONLY};
        bool legacy = false;
        for (size_t i = 0; i < OBS_COUNTOF(old_keys); ++i)
            legacy |= obs_data_has_user_value(settings, old_keys[i]);
        obs_data_set_bool(settings, SET_FACE_ONLY, !legacy);
    }
    filter->tracker = face_tracker_create();
    if (!filter->tracker) {
        beauty_destroy(filter);
        return NULL;
    }

    char *effect_path = obs_module_file("beauty.effect");
    if (!effect_path) {
        blog(LOG_ERROR, "[obs-beauty-macos] Cannot locate beauty.effect");
        beauty_destroy(filter);
        return NULL;
    }

    obs_enter_graphics();
    filter->effect = gs_effect_create_from_file(effect_path, NULL);
    /* Bind a valid texture even in legacy mode: some drivers validate all
     * samplers regardless of the face_only uniform branch. */
    const uint8_t empty_mask[] = {0, 0, 0, 255};
    const uint8_t *empty_pixels = empty_mask;
    filter->mask_texture = gs_texture_create(1, 1, GS_RGBA, 1, &empty_pixels, GS_DYNAMIC);
    if (filter->effect) {
        filter->mask_param = gs_effect_get_param_by_name(filter->effect, "face_mask");
        filter->face_only_param = gs_effect_get_param_by_name(filter->effect, "face_only");
        filter->protect_param = gs_effect_get_param_by_name(filter->effect, "feature_protection");
        filter->freshness_param = gs_effect_get_param_by_name(filter->effect, "face_freshness");
        filter->debug_param = gs_effect_get_param_by_name(filter->effect, "mask_debug");
        filter->texel_size_param = gs_effect_get_param_by_name(filter->effect, "texel_size");
        filter->smooth_param = gs_effect_get_param_by_name(filter->effect, "smooth_amount");
        filter->radius_param = gs_effect_get_param_by_name(filter->effect, "blur_radius");
        filter->whiten_param = gs_effect_get_param_by_name(filter->effect, "whiten_amount");
        filter->rosy_param = gs_effect_get_param_by_name(filter->effect, "rosy_amount");
        filter->sharpen_param = gs_effect_get_param_by_name(filter->effect, "sharpen_amount");
        filter->skin_only_param = gs_effect_get_param_by_name(filter->effect, "skin_only");
    }
    obs_leave_graphics();
    bfree(effect_path);

    if (!filter->effect || !filter->mask_texture || !filter->mask_param || !filter->face_only_param ||
        !filter->protect_param || !filter->freshness_param || !filter->debug_param || !filter->texel_size_param || !filter->smooth_param ||
        !filter->radius_param || !filter->whiten_param || !filter->rosy_param ||
        !filter->sharpen_param || !filter->skin_only_param) {
        blog(LOG_ERROR, "[obs-beauty-macos] Shader load/parameter lookup failed");
        beauty_destroy(filter);
        return NULL;
    }
    beauty_update(filter, settings);
    return filter;
}

static enum gs_color_space beauty_color_space(void *data, size_t count,
                                             const enum gs_color_space *preferred_spaces)
{
    UNUSED_PARAMETER(count);
    UNUSED_PARAMETER(preferred_spaces);
    const enum gs_color_space spaces[] = {
        GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED,
    };
    struct beauty_filter *filter = data;
    return obs_source_get_color_space(obs_filter_get_target(filter->source),
                                      OBS_COUNTOF(spaces), spaces);
}

/* Called only on the OBS graphics thread. Invalidate on input changes or re-enable. */
static bool beauty_face_resources(struct beauty_filter *f, uint32_t width, uint32_t height)
{
    if (width != f->input_width || height != f->input_height || !f->last_face_only) {
        ++f->generation;
        f->input_width = width;
        f->input_height = height;
        f->mask_timestamp = f->last_submit = 0;
        gs_texrender_destroy(f->detect_render);
        gs_stagesurface_destroy(f->stage);
        gs_texture_destroy(f->mask_texture);
        bfree(f->mask_pixels);
        f->detect_render = NULL;
        f->stage = NULL;
        f->mask_texture = NULL;
        f->mask_pixels = NULL;
        float scale = fminf(1.0f, 640.0f / (float)(width > height ? width : height));
        f->detect_width = (uint32_t)fmaxf(1, roundf(width * scale));
        f->detect_height = (uint32_t)fmaxf(1, roundf(height * scale));
        scale = fminf(1.0f, 320.0f / (float)(width > height ? width : height));
        f->mask_width = (uint32_t)fmaxf(1, roundf(width * scale));
        f->mask_height = (uint32_t)fmaxf(1, roundf(height * scale));
    }
    f->last_face_only = true;
    if (!f->detect_render) f->detect_render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
    if (!f->stage) f->stage = gs_stagesurface_create(f->detect_width, f->detect_height, GS_RGBA);
    if (!f->mask_pixels) {
        f->mask_pixels = bzalloc((size_t)f->mask_width * f->mask_height * 4);
        if (f->mask_pixels) {
            struct face_result empty = {0};
            face_mask_build(&empty, f->mask_pixels, f->mask_width, f->mask_height);
        }
    }
    if (!f->mask_texture && f->mask_pixels) {
        const uint8_t *pixels = f->mask_pixels;
        f->mask_texture = gs_texture_create(f->mask_width, f->mask_height, GS_RGBA, 1, &pixels, GS_DYNAMIC);
    }
    return f->detect_render && f->stage && f->mask_texture;
}

static float beauty_detect(struct beauty_filter *f, uint64_t now)
{
    if (now - f->last_submit >= 100000000ULL && !face_tracker_busy(f->tracker)) {
        f->last_submit = now;
        gs_texrender_reset(f->detect_render);
        if (gs_texrender_begin_with_color_space(f->detect_render, f->detect_width,
                                               f->detect_height, GS_CS_SRGB)) {
            struct vec4 clear = {0};
            gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
            gs_ortho(0, (float)f->detect_width, 0, (float)f->detect_height, -100, 100);
            gs_blend_state_push();
            gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
            /* begin(NO_DIRECT_RENDERING) has already captured the filter input.
             * end draws that cached input, not the source. Drawing it once here
             * and again below is safe with the bypass path explicitly disabled. */
            obs_source_process_filter_end(f->source, obs_get_base_effect(OBS_EFFECT_DEFAULT),
                                          f->detect_width, f->detect_height);
            gs_blend_state_pop();
            gs_texrender_end(f->detect_render);
            gs_stage_texture(f->stage, gs_texrender_get_texture(f->detect_render));
            uint8_t *pixels;
            uint32_t stride;
            if (gs_stagesurface_map(f->stage, &pixels, &stride)) {
                face_tracker_submit(f->tracker, pixels, stride, f->detect_width, f->detect_height,
                                    f->input_width, f->input_height, f->generation, now);
                gs_stagesurface_unmap(f->stage);
            }
        }
    }
    struct face_result result;
    if (face_tracker_latest(f->tracker, &result) &&
        face_result_matches(&result, f->generation, f->input_width, f->input_height)) {
        if (result.timestamp_ns != f->mask_timestamp && face_freshness(now, result.timestamp_ns) > 0) {
            face_mask_build(&result, f->mask_pixels, f->mask_width, f->mask_height);
            gs_texture_set_image(f->mask_texture, f->mask_pixels, f->mask_width * 4, false);
            f->mask_timestamp = result.timestamp_ns;
        }
    }
    return face_freshness(os_gettime_ns(), f->mask_timestamp);
}

static void beauty_render(void *data, gs_effect_t *unused)
{
    UNUSED_PARAMETER(unused);
    struct beauty_filter *filter = data;
    obs_source_t *target = obs_filter_get_target(filter->source);
    if (!target) {
        filter->last_face_only = false;
        obs_source_skip_video_filter(filter->source);
        return;
    }

    uint32_t width = obs_source_get_base_width(target);
    uint32_t height = obs_source_get_base_height(target);
    if (!width || !height) {
        filter->last_face_only = false;
        obs_source_skip_video_filter(filter->source);
        return;
    }

    const enum gs_color_space spaces[] = {
        GS_CS_SRGB, GS_CS_SRGB_16F, GS_CS_709_EXTENDED,
    };
    const enum gs_color_space source_space =
        obs_source_get_color_space(target, OBS_COUNTOF(spaces), spaces);

    /* SDR-only color operations. Do not damage HDR/extended-range sources. */
    if (source_space == GS_CS_709_EXTENDED) {
        filter->last_face_only = false;
        obs_source_skip_video_filter(filter->source);
        return;
    }
    if (!filter->face_only) filter->last_face_only = false;
    if (filter->face_only && !beauty_face_resources(filter, width, height)) {
        obs_source_skip_video_filter(filter->source);
        return;
    }
    enum gs_color_format format = gs_get_format_from_space(source_space);
    if (!obs_source_process_filter_begin_with_color_space(
            filter->source, format, source_space,
            filter->face_only ? OBS_NO_DIRECT_RENDERING : OBS_ALLOW_DIRECT_RENDERING))
        return;

    float freshness = filter->face_only ? beauty_detect(filter, os_gettime_ns()) : 0;
    gs_effect_set_texture(filter->mask_param, filter->mask_texture);
    gs_effect_set_float(filter->face_only_param, filter->face_only ? 1 : 0);
    gs_effect_set_float(filter->protect_param, filter->protect ? 1 : 0);
    gs_effect_set_float(filter->freshness_param, freshness);
    gs_effect_set_float(filter->debug_param, filter->debug && filter->face_only ? 1 : 0);
    struct vec2 texel_size;
    vec2_set(&texel_size, 1.0f / (float)width, 1.0f / (float)height);
    gs_effect_set_vec2(filter->texel_size_param, &texel_size);
    gs_effect_set_float(filter->smooth_param, filter->smooth);
    gs_effect_set_float(filter->radius_param, filter->radius);
    gs_effect_set_float(filter->whiten_param, filter->whiten);
    gs_effect_set_float(filter->rosy_param, filter->rosy);
    gs_effect_set_float(filter->sharpen_param, filter->sharpen);
    gs_effect_set_float(filter->skin_only_param, filter->skin_only);

    gs_blend_state_push();
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
    obs_source_process_filter_end(filter->source, filter->effect, 0, 0);
    gs_blend_state_pop();
}

static struct obs_source_info beauty_info = {
    .id = FILTER_ID,
    .type = OBS_SOURCE_TYPE_FILTER,
    .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
    .get_name = beauty_name,
    .create = beauty_create,
    .destroy = beauty_destroy,
    .update = beauty_update,
    .get_properties = beauty_properties,
    .get_defaults = beauty_defaults,
    .video_render = beauty_render,
    .video_get_color_space = beauty_color_space,
};

bool obs_module_load(void)
{
    obs_register_source(&beauty_info);
    blog(LOG_INFO, "[obs-beauty-macos] GPU beauty filter with Apple Vision registered");
    return true;
}

const char *obs_module_description(void)
{
    return "Camera beauty filter: Apple Vision face masks, feature protection and GPU beauty";
}
