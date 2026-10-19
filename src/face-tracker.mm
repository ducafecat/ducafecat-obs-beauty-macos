/* SPDX-License-Identifier: GPL-2.0-or-later */
#import <Vision/Vision.h>
#import <CoreVideo/CoreVideo.h>
#include "face-tracker.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <cmath>
#include <new>

struct face_tracker {
    dispatch_queue_t queue;
    std::atomic<bool> busy{false};
    std::mutex mutex;
    face_result result{};
    face_result previous{}; /* Worker-owned tracking state. */
};
static face_box vision_box(VNFaceObservation *face)
{
    CGRect b=face.boundingBox;
    return {(float)b.origin.x,(float)b.origin.y,(float)b.size.width,(float)b.size.height};
}
static void region(face_region &out, VNFaceLandmarkRegion2D *input, face_box box)
{
    out.count=(uint32_t)MIN(input.pointCount,FACE_MAX_POINTS-4);
    for (unsigned i=0;i<out.count;++i)
        out.points[i]=face_from_vision(box,input.normalizedPoints[i].x,input.normalizedPoints[i].y);
}
static void detect(face_tracker *t, CVPixelBufferRef buffer, face_result next)
{
    @autoreleasepool {
        VNDetectFaceLandmarksRequest *request=[[VNDetectFaceLandmarksRequest alloc] init];
        VNImageRequestHandler *handler=[[VNImageRequestHandler alloc] initWithCVPixelBuffer:buffer
            orientation:kCGImagePropertyOrientationUp options:@{}];
        NSError *error=nil;
        bool success=[handler performRequests:@[request] error:&error];
        NSArray<VNFaceObservation *> *faces=success ? request.results : @[];
        std::vector<face_box> boxes;
        for (VNFaceObservation *face in faces) boxes.push_back(vision_box(face));
        bool recent=face_result_matches(&t->previous,next.generation,next.width,next.height) &&
            next.timestamp_ns>=t->previous.timestamp_ns && next.timestamp_ns-t->previous.timestamp_ns<500000000ULL;
        face_box previous_vision=t->previous.box;
        previous_vision.y=1-previous_vision.y-previous_vision.h;
        int selected=face_select(boxes.data(),(unsigned)boxes.size(),recent ? &previous_vision : nullptr);
        if (selected>=0) {
            VNFaceObservation *face=faces[(NSUInteger)selected];
            VNFaceLandmarks2D *l=face.landmarks;
            face_box box=boxes[(size_t)selected];
            next.box={box.x,1-box.y-box.h,box.w,box.h};
            region(next.regions[FACE_OUTLINE],l.faceContour,box);
            region(next.regions[FACE_LEFT_EYE],l.leftEye,box);
            region(next.regions[FACE_RIGHT_EYE],l.rightEye,box);
            region(next.regions[FACE_LEFT_BROW],l.leftEyebrow,box);
            region(next.regions[FACE_RIGHT_BROW],l.rightEyebrow,box);
            region(next.regions[FACE_NOSE],l.nose,box);
            region(next.regions[FACE_LIPS],l.outerLips,box);
            next.valid=face.confidence>=.5f;
            for (unsigned i=0;i<FACE_REGIONS;++i) next.valid &= next.regions[i].count>=3;
            if (next.valid) {
                /* Vision's open jaw contour runs temple -> chin -> opposite temple.
                   Close it across a conservative forehead arch inside the face box. */
                face_region &outline=next.regions[FACE_OUTLINE];
                face_point first=outline.points[0],last=outline.points[outline.count-1];
                float top=next.box.y+next.box.h*.08f;
                outline.points[outline.count++]={last.x,top+next.box.h*.06f};
                outline.points[outline.count++]={(last.x+first.x)*.5f,top};
                outline.points[outline.count++]={first.x,top+next.box.h*.06f};
                if (recent) {
                    double dt=(double)(next.timestamp_ns-t->previous.timestamp_ns)/1e9;
                    float alpha=(float)(1-std::exp(-dt/.08));
                    for (unsigned k=0;k<FACE_REGIONS;++k) {
                        face_region &a=next.regions[k];
                        const face_region &b=t->previous.regions[k];
                        if (a.count==b.count) for (unsigned i=0;i<a.count;++i) {
                            a.points[i].x=b.points[i].x+alpha*(a.points[i].x-b.points[i].x);
                            a.points[i].y=b.points[i].y+alpha*(a.points[i].y-b.points[i].y);
                        }
                    }
                }
                t->previous=next;
            }
        }
        /* Keep the last valid result on misses so the render-side age fades it out. */
        if (next.valid) {
            std::lock_guard<std::mutex> guard(t->mutex);
            t->result=next;
        }
    }
}
face_tracker *face_tracker_create(void)
{
    auto *t=new(std::nothrow) face_tracker;
    if (t) t->queue=dispatch_queue_create("com.ducafecat.beauty.vision",DISPATCH_QUEUE_SERIAL);
    return t;
}
void face_tracker_destroy(face_tracker *t)
{
    if (!t) return;
    dispatch_sync(t->queue, ^{});
    delete t;
}
bool face_tracker_busy(face_tracker *t) { return !t || t->busy.load(); }
bool face_tracker_latest(face_tracker *t, face_result *out)
{
    if (!t) return false;
    std::lock_guard<std::mutex> guard(t->mutex);
    *out=t->result;
    return out->valid;
}
bool face_tracker_submit(face_tracker *t,const uint8_t *rgba,uint32_t stride,uint32_t w,uint32_t h,
    uint32_t sw,uint32_t sh,uint64_t generation,uint64_t timestamp)
{
    if (!t || !rgba || !w || !h || w>640 || h>640 || stride<w*4 || t->busy.exchange(true)) return false;
    CVPixelBufferRef buffer=nullptr;
    if (CVPixelBufferCreate(kCFAllocatorDefault,w,h,kCVPixelFormatType_32BGRA,nullptr,&buffer)!=kCVReturnSuccess) {
        t->busy=false; return false;
    }
    if (CVPixelBufferLockBaseAddress(buffer,0)!=kCVReturnSuccess) {
        CVPixelBufferRelease(buffer); t->busy=false; return false;
    }
    auto *dst=(uint8_t *)CVPixelBufferGetBaseAddress(buffer);
    size_t pitch=CVPixelBufferGetBytesPerRow(buffer);
    for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
        const uint8_t *p=rgba+y*stride+x*4;
        uint8_t *q=dst+y*pitch+x*4;
        q[0]=p[2]; q[1]=p[1]; q[2]=p[0]; q[3]=p[3];
    }
    CVPixelBufferUnlockBaseAddress(buffer,0);
    face_result next{};
    next.timestamp_ns=timestamp; next.generation=generation; next.width=sw; next.height=sh;
    dispatch_async(t->queue, ^{
        detect(t,buffer,next);
        CVPixelBufferRelease(buffer);
        t->busy=false;
    });
    return true;
}
