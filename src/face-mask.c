/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "face-tracker.h"
#include <math.h>
#include <stddef.h>

struct face_point face_from_vision(struct face_box b, float x, float y)
{
    return (struct face_point){b.x + x * b.w, 1.0f - (b.y + y * b.h)};
}
float face_box_iou(struct face_box a, struct face_box b)
{
    float w = fmaxf(0, fminf(a.x+a.w,b.x+b.w)-fmaxf(a.x,b.x));
    float h = fmaxf(0, fminf(a.y+a.h,b.y+b.h)-fmaxf(a.y,b.y));
    float intersection = w*h, total = a.w*a.h+b.w*b.h-intersection;
    return total > 0 ? intersection/total : 0;
}
int face_select(const struct face_box *boxes, unsigned count, const struct face_box *previous)
{
    int largest = -1, match = -1;
    float area = 0, overlap = 0.1f;
    for (unsigned i=0; i<count; ++i) {
        float a = boxes[i].w*boxes[i].h;
        if (a>area) { area=a; largest=(int)i; }
        float v = previous ? face_box_iou(boxes[i],*previous) : 0;
        if (v>overlap) { overlap=v; match=(int)i; }
    }
    /* Do not switch to a bystander while a recent primary face is lost. */
    return previous ? match : largest;
}
float face_freshness(uint64_t now, uint64_t timestamp)
{
    if (!timestamp || now < timestamp) return 0;
    double age = (double)(now-timestamp)/1e6;
    return age<=200 ? 1 : age>=500 ? 0 : (float)((500-age)/300);
}
bool face_result_matches(const struct face_result *r, uint64_t generation, uint32_t w, uint32_t h)
{
    return r->valid && r->generation==generation && r->width==w && r->height==h;
}
static float clamp01(float v) { return fminf(1,fmaxf(0,v)); }
static struct face_point region_center_pixels(const struct face_region *region, float w, float h)
{
    struct face_point center = {0};
    for (unsigned i=0;i<region->count;++i) {
        center.x += region->points[i].x*w;
        center.y += region->points[i].y*h;
    }
    center.x /= region->count;
    center.y /= region->count;
    return center;
}
bool face_complete_forehead(struct face_result *r)
{
    struct face_region *jaw=&r->regions[FACE_OUTLINE];
    const struct face_region *left=&r->regions[FACE_LEFT_BROW];
    const struct face_region *right=&r->regions[FACE_RIGHT_BROW];
    const struct face_region *lips=&r->regions[FACE_LIPS];
    if (!r->width || !r->height || jaw->count<3 || jaw->count>FACE_MAX_POINTS-7 ||
        left->count<2 || right->count<2 || lips->count<3) return false;
    float w=(float)r->width,h=(float)r->height;
    struct face_point a=region_center_pixels(left,w,h);
    struct face_point b=region_center_pixels(right,w,h);
    struct face_point mouth=region_center_pixels(lips,w,h);
    struct face_point brow={(a.x+b.x)*.5f,(a.y+b.y)*.5f};
    float dx=b.x-a.x,dy=b.y-a.y,span=hypotf(dx,dy);
    if (span<1) return false;
    /* Work in pixel coordinates so head roll is independent of frame aspect. */
    struct face_point down={-dy/span,dx/span};
    if ((mouth.x-brow.x)*down.x+(mouth.y-brow.y)*down.y<0) {
        down.x=-down.x; down.y=-down.y;
    }
    float chin=0;
    for (unsigned i=0;i<jaw->count;++i) {
        struct face_point p=jaw->points[i];
        chin=fmaxf(chin,(p.x*w-brow.x)*down.x+(p.y*h-brow.y)*down.y);
    }
    if (chin<1) return false;
    struct face_point first={jaw->points[0].x*w,jaw->points[0].y*h};
    struct face_point last={jaw->points[jaw->count-1].x*w,jaw->points[jaw->count-1].y*h};
    float baseline=((first.x+last.x)*.5f-brow.x)*down.x+
                   ((first.y+last.y)*.5f-brow.y)*down.y;
    /* Vision boxes can start near the brows: estimate forehead height from the
     * brow-to-chin distance instead of clipping it to the detector's box top.
     * Cap extension relative to temple width to stay conservative in profile. */
    float temple_width=hypotf(last.x-first.x,last.y-first.y);
    float height=fminf(chin*.42f,temple_width*.65f);
    float amplitude=fmaxf(0,baseline+height);
    for (unsigned i=1;i<8;++i) {
        float angle=(float)i*3.14159265358979323846f/8;
        float t=(1-cosf(angle))*.5f;
        float lift=amplitude*sinf(angle);
        struct face_point p={last.x+(first.x-last.x)*t-down.x*lift,
                             last.y+(first.y-last.y)*t-down.y*lift};
        jaw->points[jaw->count++]=(struct face_point){clamp01(p.x/w),clamp01(p.y/h)};
    }
    return true;
}
struct pixel_region {
    unsigned count;
    struct face_point points[FACE_MAX_POINTS];
    float left, top, right, bottom;
};
static struct pixel_region pixel_region(const struct face_region *r, unsigned w, unsigned h)
{
    struct pixel_region out = {.count=r->count,.left=1e6f,.top=1e6f,.right=-1e6f,.bottom=-1e6f};
    for (unsigned i=0;i<r->count;++i) {
        struct face_point p={r->points[i].x*w,r->points[i].y*h};
        out.points[i]=p;
        out.left=fminf(out.left,p.x); out.right=fmaxf(out.right,p.x);
        out.top=fminf(out.top,p.y); out.bottom=fmaxf(out.bottom,p.y);
    }
    return out;
}
/* Signed pixel distance. Bounds reject pixels outside the feather footprint;
 * compare squared segment distances and take only one square root per region. */
static float distance_to(const struct pixel_region *r, float x, float y, float expansion)
{
    if (r->count<2 || x<r->left-expansion || x>r->right+expansion ||
        y<r->top-expansion || y>r->bottom+expansion) return -1e6f;
    bool inside=false;
    float distance=1e12f;
    for (unsigned i=0,j=r->count-1; i<r->count; j=i++) {
        float ax=r->points[j].x, ay=r->points[j].y;
        float bx=r->points[i].x, by=r->points[i].y;
        if ((ay>y)!=(by>y) && x<(bx-ax)*(y-ay)/(by-ay)+ax) inside=!inside;
        float dx=bx-ax, dy=by-ay;
        float t=clamp01(((x-ax)*dx+(y-ay)*dy)/fmaxf(dx*dx+dy*dy,1e-8f));
        float px=x-ax-t*dx, py=y-ay-t*dy;
        distance=fminf(distance,px*px+py*py);
    }
    distance=sqrtf(distance);
    return inside ? distance : -distance;
}
void face_mask_build(const struct face_result *r, uint8_t *rgba, unsigned w, unsigned h)
{
    struct pixel_region regions[FACE_REGIONS];
    for (unsigned i=0;i<FACE_REGIONS;++i) regions[i]=pixel_region(&r->regions[i],w,h);
    float face_width=r->box.w*w;
    float feather=fmaxf(2,face_width*0.025f);
    float expansion=fmaxf(2,face_width*0.025f);
    for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
        float face=0,protect=0,lip=0;
        if (r->valid) {
            /* Feather inward so pixels outside the predicted face stay untouched. */
            face=clamp01(distance_to(&regions[FACE_OUTLINE],x+.5f,y+.5f,0)/feather);
            for (unsigned i=FACE_LEFT_EYE;face>0 && i<FACE_REGIONS;++i) {
                float v=clamp01((distance_to(&regions[i],x+.5f,y+.5f,expansion)+expansion)/feather);
                if (i==FACE_LIPS) lip=v;
                protect=fmaxf(protect,v*(i==FACE_NOSE ? .65f : 1));
            }
        }
        size_t p=((size_t)y*w+x)*4;
        rgba[p]=(uint8_t)lroundf(face*255);
        rgba[p+1]=(uint8_t)lroundf(protect*255);
        rgba[p+2]=(uint8_t)lroundf(lip*255);
        rgba[p+3]=255;
    }
}
