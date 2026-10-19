/* Run against an installed OBS; loads only this module, no camera or user scenes. */
#import <AppKit/AppKit.h>
#include <obs.h>
#include <graphics/vec4.h>
#include <graphics/vec2.h>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>

static uint32_t input_width=320,input_height=180;
struct pattern { gs_texture_t *texture=nullptr; };
static const char *pattern_name(void *) { return "Test pattern"; }
static void *pattern_create(obs_data_t *,obs_source_t *) { return new pattern; }
static void pattern_destroy(void *data)
{
    auto *p=(pattern *)data;
    obs_enter_graphics(); gs_texture_destroy(p->texture); obs_leave_graphics(); delete p;
}
static uint32_t pattern_width(void *) { return input_width; }
static uint32_t pattern_height(void *) { return input_height; }
static void pattern_render(void *data,gs_effect_t *)
{
    auto *p=(pattern *)data;
    if (!p->texture) {
        const uint8_t pixels[]={210,90,60,255, 30,120,210,255,
                                60,180,80,255, 180,60,170,255};
        const uint8_t *ptr=pixels;
        p->texture=gs_texture_create(2,2,GS_RGBA,1,&ptr,0);
    }
    gs_effect_t *effect=obs_get_base_effect(OBS_EFFECT_DEFAULT);
    auto *image=gs_effect_get_param_by_name(effect,"image");
    if (gs_get_linear_srgb()) gs_effect_set_texture_srgb(image,p->texture);
    else gs_effect_set_texture(image,p->texture);
    bool before=gs_framebuffer_srgb_enabled();
    gs_enable_framebuffer_srgb(gs_get_linear_srgb());
    while (gs_effect_loop(effect,"Draw")) gs_draw_sprite(p->texture,0,input_width,input_height);
    gs_enable_framebuffer_srgb(before);
}
static std::vector<uint8_t> render_frame(obs_source_t *source)
{
    /* OBS resets its per-source texrenders once per video tick, not per manual
     * render call. Wait for a fresh tick before rendering a changed input. */
    uint64_t tick=obs_get_video_frame_time();
    for (unsigned i=0;i<200 && obs_get_video_frame_time()==tick;++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(obs_get_video_frame_time()!=tick);
    obs_enter_graphics();
    auto *render=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    assert(gs_texrender_begin(render,320,180));
    vec4 clear{};
    gs_clear(GS_CLEAR_COLOR,&clear,0,0);
    gs_ortho(0,(float)input_width,0,(float)input_height,-100,100);
    gs_blend_state_push(); gs_blend_function(GS_BLEND_ONE,GS_BLEND_INVSRCALPHA);
    obs_source_video_render(source);
    gs_blend_state_pop(); gs_texrender_end(render);
    auto *stage=gs_stagesurface_create(320,180,GS_RGBA);
    gs_stage_texture(stage,gs_texrender_get_texture(render));
    uint8_t *pixels; uint32_t stride;
    assert(gs_stagesurface_map(stage,&pixels,&stride));
    std::vector<uint8_t> out(320*180*4);
    for (unsigned y=0;y<180;++y) memcpy(out.data()+y*320*4,pixels+y*stride,320*4);
    gs_stagesurface_unmap(stage); gs_stagesurface_destroy(stage); gs_texrender_destroy(render);
    obs_leave_graphics(); return out;
}
static void compare(const std::vector<uint8_t> &a,const std::vector<uint8_t> &b)
{
    assert(a.size()==b.size());
    for (size_t i=0;i<a.size();++i) if (abs((int)a[i]-(int)b[i])>3) {
        fprintf(stderr,"Pixel mismatch at %zu: %u vs %u; corners RGBA: %u,%u,%u,%u / %u,%u,%u,%u\n",i,a[i],b[i],
            b[0],b[1],b[2],b[3],b[b.size()-4],b[b.size()-3],b[b.size()-2],b[b.size()-1]); abort();
    }
}

/* Exercise actual GPU mixing with deterministic masks, independent of Vision. */
static std::vector<uint8_t> render_mask_case(const char *path, uint8_t face, uint8_t feature,
    uint8_t lips, float freshness, float smooth, float whiten)
{
    obs_enter_graphics();
    auto *effect=gs_effect_create_from_file(path,nullptr); assert(effect);
    const uint8_t image_bytes[]={210,90,60,255,30,120,210,255,60,180,80,255,180,60,170,255};
    const uint8_t mask_bytes[]={face,feature,lips,255};
    const uint8_t *ptr=image_bytes;
    auto *image=gs_texture_create(2,2,GS_RGBA,1,&ptr,0);
    ptr=mask_bytes;
    auto *mask=gs_texture_create(1,1,GS_RGBA,1,&ptr,0);
    auto *render=gs_texrender_create(GS_RGBA,GS_ZS_NONE);
    assert(gs_texrender_begin(render,320,180));
    vec4 clear{}; gs_clear(GS_CLEAR_COLOR,&clear,0,0);
    gs_ortho(0,320,0,180,-100,100);
    auto set=[&](const char *name,float value) { gs_effect_set_float(gs_effect_get_param_by_name(effect,name),value); };
    vec2 texel; vec2_set(&texel,.5f,.5f);
    gs_effect_set_vec2(gs_effect_get_param_by_name(effect,"texel_size"),&texel);
    set("smooth_amount",smooth); set("blur_radius",1); set("whiten_amount",whiten);
    set("rosy_amount",0); set("sharpen_amount",0); set("skin_only",0);
    set("face_only",1); set("feature_protection",1); set("face_freshness",freshness); set("mask_debug",0);
    gs_effect_set_texture_srgb(gs_effect_get_param_by_name(effect,"image"),image);
    gs_effect_set_texture(gs_effect_get_param_by_name(effect,"face_mask"),mask);
    bool srgb=gs_framebuffer_srgb_enabled(); gs_enable_framebuffer_srgb(true);
    gs_blend_state_push(); gs_blend_function(GS_BLEND_ONE,GS_BLEND_ZERO);
    while (gs_effect_loop(effect,"Draw")) gs_draw_sprite(image,0,320,180);
    gs_blend_state_pop(); gs_enable_framebuffer_srgb(srgb); gs_texrender_end(render);
    auto *stage=gs_stagesurface_create(320,180,GS_RGBA);
    gs_stage_texture(stage,gs_texrender_get_texture(render));
    uint8_t *pixels; uint32_t stride;
    assert(gs_stagesurface_map(stage,&pixels,&stride));
    std::vector<uint8_t> out(320*180*4);
    for (unsigned y=0;y<180;++y) memcpy(out.data()+y*320*4,pixels+y*stride,320*4);
    gs_stagesurface_unmap(stage); gs_stagesurface_destroy(stage); gs_texrender_destroy(render);
    gs_texture_destroy(image); gs_texture_destroy(mask); gs_effect_destroy(effect);
    obs_leave_graphics(); return out;
}

int main(int argc, char **argv)
{
    if (argc!=3) { fprintf(stderr,"usage: obs-runtime-smoke plugin-binary plugin-data\n"); return 2; }
    @autoreleasepool {
        [NSApplication sharedApplication];
        assert(obs_startup("en-US",nullptr,nullptr));
        obs_video_info video{};
        const char *backend=getenv("BEAUTY_TEST_GRAPHICS");
        std::string graphics="/Applications/OBS.app/Contents/Frameworks/libobs-";
        graphics += backend ? backend : "opengl";
        graphics += ".dylib";
        video.graphics_module=graphics.c_str();
        video.fps_num=30; video.fps_den=1;
        video.base_width=video.output_width=320;
        video.base_height=video.output_height=180;
        video.output_format=VIDEO_FORMAT_RGBA;
        video.colorspace=VIDEO_CS_709; video.range=VIDEO_RANGE_FULL;
        video.scale_type=OBS_SCALE_BILINEAR;
        int status=obs_reset_video(&video);
        if (status!=OBS_VIDEO_SUCCESS) { fprintf(stderr,"OBS graphics init failed: %d\n",status); obs_shutdown(); return 1; }
        obs_module_t *module=nullptr;
        assert(obs_open_module(&module,argv[1],argv[2])==MODULE_SUCCESS);
        assert(obs_init_module(module));
        obs_source_t *fresh=obs_source_create_private("ducafecat_beauty_filter","new",nullptr);
        assert(fresh);
        obs_data_t *settings=obs_source_get_settings(fresh);
        assert(obs_data_get_bool(settings,"face_only"));
        assert(obs_data_get_bool(settings,"feature_protection"));
        assert(!obs_data_get_bool(settings,"mask_debug"));
        obs_data_release(settings);
        obs_source_info info{};
        info.id="beauty_test_pattern"; info.type=OBS_SOURCE_TYPE_INPUT;
        info.output_flags=OBS_SOURCE_VIDEO|OBS_SOURCE_CUSTOM_DRAW|OBS_SOURCE_SRGB;
        info.get_name=pattern_name; info.create=pattern_create; info.destroy=pattern_destroy;
        info.get_width=pattern_width; info.get_height=pattern_height; info.video_render=pattern_render;
        obs_register_source(&info);
        obs_source_t *parent=obs_source_create_private(info.id,"pattern",nullptr); assert(parent);
        auto baseline=render_frame(parent);
        assert(baseline[(20*320+20)*4]>baseline[(150*320+20)*4]); /* Asymmetric top/bottom. */
        std::string shader=std::string(argv[2])+"/beauty.effect";
        puts("Testing GPU masks: face limits, lip tone protection, eye smoothing protection and expiry");
        auto shader_baseline=render_mask_case(shader.c_str(),255,0,0,1,0,0);
        compare(shader_baseline,render_mask_case(shader.c_str(),0,0,0,1,1,1));
        compare(shader_baseline,render_mask_case(shader.c_str(),255,0,0,0,1,1));
        compare(shader_baseline,render_mask_case(shader.c_str(),255,255,255,1,1,1));
        auto bright=render_mask_case(shader.c_str(),255,0,0,1,0,1);
        assert(bright[(20*320+20)*4+1]>shader_baseline[(20*320+20)*4+1]+5);
        auto smoothed=render_mask_case(shader.c_str(),255,0,0,1,1,0);
        unsigned changed=0;
        for (size_t i=0;i<shader_baseline.size();++i) changed+=abs((int)shader_baseline[i]-(int)smoothed[i])>3;
        assert(changed>100);
        obs_source_filter_add(parent,fresh);
        settings=obs_data_create();
        obs_data_set_bool(settings,"face_only",false);
        obs_data_set_int(settings,"smooth",0); obs_data_set_int(settings,"whiten",0);
        obs_data_set_int(settings,"rosy",0); obs_data_set_int(settings,"sharpen",0);
        obs_source_update(fresh,settings);
        puts("Testing legacy identity render"); fflush(stdout);
        compare(baseline,render_frame(parent));
        obs_data_set_bool(settings,"face_only",true);
        obs_data_set_int(settings,"smooth",40); obs_data_set_int(settings,"whiten",12);
        obs_data_set_int(settings,"rosy",8); obs_data_set_int(settings,"sharpen",10);
        obs_source_update(fresh,settings); obs_data_release(settings);
        puts("Testing no-face render"); fflush(stdout);
        compare(baseline,render_frame(parent)); /* No-face path, including detection downsample/readback. */
        input_width=640; input_height=360;
        puts("Testing resized render"); fflush(stdout);
        compare(baseline,render_frame(parent)); /* Size change invalidates prior generation. */
        obs_source_filter_remove(parent,fresh);
        obs_source_release(fresh); obs_source_release(parent);
        settings=obs_data_create();
        obs_data_set_int(settings,"smooth",40);
        obs_source_t *legacy=obs_source_create_private("ducafecat_beauty_filter","old",settings);
        assert(legacy);
        obs_data_t *saved=obs_source_get_settings(legacy);
        assert(!obs_data_get_bool(saved,"face_only"));
        obs_data_release(saved); obs_data_release(settings); obs_source_release(legacy);
        obs_shutdown();
        puts("OBS module, Shader, no-face render/readback, resize and settings migration passed.");
    }
}
