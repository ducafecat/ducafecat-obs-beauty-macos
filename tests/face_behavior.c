/* Behavioral tests of production coordinate, selection, expiry and mask code. */
#include "face-tracker.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static void near(float a,float b) { assert(fabsf(a-b)<.0001f); }
static struct face_region rect(float x,float y,float w,float h)
{
    return (struct face_region){4,{{x,y},{x+w,y},{x+w,y+h},{x,y+h}}};
}
static unsigned channel(uint8_t *p,unsigned w,unsigned x,unsigned y,unsigned c)
{ return p[(y*w+x)*4+c]; }
int main(void)
{
    struct face_box b={.2f,.1f,.4f,.6f};
    struct face_point p=face_from_vision(b,0,0); near(p.x,.2f); near(p.y,.9f);
    p=face_from_vision(b,1,1); near(p.x,.6f); near(p.y,.3f);
    p=face_from_vision(b,.5f,.5f); near(p.x,.4f); near(p.y,.6f);
    struct face_box candidates[]={{0,0,.5f,.5f},{.6f,.3f,.2f,.2f}};
    assert(face_select(candidates,2,NULL)==0);
    struct face_box previous={.61f,.31f,.2f,.2f};
    assert(face_select(candidates,2,&previous)==1);
    previous=(struct face_box){.9f,.9f,.05f,.05f};
    assert(face_select(candidates,2,&previous)==-1);
    assert(face_select(candidates,0,NULL)==-1);
    near(face_box_iou(b,b),1);
    uint64_t stamp=1000000000ULL;
    near(face_freshness(stamp+200000000,stamp),1);
    near(face_freshness(stamp+350000000,stamp),.5f);
    near(face_freshness(stamp+500000000,stamp),0);
    near(face_freshness(stamp-1,stamp),0);
    near(face_freshness(stamp,0),0);
    struct face_result r={.timestamp_ns=stamp,.generation=7,.width=1280,.height=720,.valid=true,
        .box={.2f,.1f,.6f,.8f}};
    assert(face_result_matches(&r,7,1280,720));
    assert(!face_result_matches(&r,6,1280,720));
    assert(!face_result_matches(&r,7,720,1280));
    r.regions[FACE_OUTLINE]=rect(.2f,.1f,.6f,.8f);
    r.regions[FACE_LEFT_EYE]=rect(.3f,.3f,.1f,.06f);
    r.regions[FACE_LIPS]=rect(.42f,.65f,.16f,.08f);
    /* Non-square mask preserves source aspect ratio; top-down feature alignment. */
    unsigned w=320,h=180;
    uint8_t *pixels=calloc(w*h,4); assert(pixels);
    face_mask_build(&r,pixels,w,h);
    assert(channel(pixels,w,160,90,0)==255);
    assert(channel(pixels,w,20,90,0)==0);
    assert(channel(pixels,w,64,90,0)<channel(pixels,w,66,90,0));
    assert(channel(pixels,w,66,90,0)<channel(pixels,w,72,90,0));
    assert(channel(pixels,w,112,59,1)==255);
    assert(channel(pixels,w,112,140,1)==0);
    assert(channel(pixels,w,160,124,2)==255);
    assert(channel(pixels,w,160,90,2)==0);
    r.valid=false; face_mask_build(&r,pixels,w,h);
    for (unsigned i=0;i<w*h;++i) {
        assert(pixels[i*4]==0 && pixels[i*4+1]==0 && pixels[i*4+2]==0 && pixels[i*4+3]==255);
    }
    free(pixels);
    puts("Face behavior tests passed.");
}
