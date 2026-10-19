/*
 * OBS Beauty Filter (macOS) - GPU-based camera beautification
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This is a non-ML approximate skin-smoothing filter intended for SDR cameras.
 */
#include <obs-module.h>
#include <graphics/vec2.h>
#include <stdint.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-beauty-macos", "en-US")

#define FILTER_ID "ducafecat_beauty_filter"
#define SET_SMOOTH "smooth"
#define SET_RADIUS "radius"
#define SET_WHITEN "whiten"
#define SET_ROSY "rosy"
#define SET_SHARPEN "sharpen"
#define SET_SKIN_ONLY "skin_only"

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
}

static void beauty_update(void *data, obs_data_t *settings)
{
    struct beauty_filter *filter = data;
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
    return props;
}

static void beauty_destroy(void *data)
{
    struct beauty_filter *filter = data;
    if (!filter)
        return;
    if (filter->effect) {
        obs_enter_graphics();
        gs_effect_destroy(filter->effect);
        obs_leave_graphics();
    }
    bfree(filter);
}

static void *beauty_create(obs_data_t *settings, obs_source_t *source)
{
    struct beauty_filter *filter = bzalloc(sizeof(*filter));
    if (!filter)
        return NULL;
    filter->source = source;

    char *effect_path = obs_module_file("beauty.effect");
    if (!effect_path) {
        blog(LOG_ERROR, "[obs-beauty-macos] Cannot locate beauty.effect");
        beauty_destroy(filter);
        return NULL;
    }

    obs_enter_graphics();
    filter->effect = gs_effect_create_from_file(effect_path, NULL);
    if (filter->effect) {
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

    if (!filter->effect || !filter->texel_size_param || !filter->smooth_param ||
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

static void beauty_render(void *data, gs_effect_t *unused)
{
    UNUSED_PARAMETER(unused);
    struct beauty_filter *filter = data;
    obs_source_t *target = obs_filter_get_target(filter->source);
    if (!target) {
        obs_source_skip_video_filter(filter->source);
        return;
    }

    uint32_t width = obs_source_get_base_width(target);
    uint32_t height = obs_source_get_base_height(target);
    if (!width || !height) {
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
        obs_source_skip_video_filter(filter->source);
        return;
    }
    enum gs_color_format format = gs_get_format_from_space(source_space);
    if (!obs_source_process_filter_begin_with_color_space(
            filter->source, format, source_space, OBS_ALLOW_DIRECT_RENDERING))
        return;

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
    blog(LOG_INFO, "[obs-beauty-macos] GPU beauty filter registered");
    return true;
}

const char *obs_module_description(void)
{
    return "Camera beauty filter: skin smoothing, brightening, rosy tone and sharpening";
}
