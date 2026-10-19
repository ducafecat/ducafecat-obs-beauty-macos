/* Integration test: real Vision worker, blank input, busy/drop and in-flight teardown. */
#include "face-tracker.h"
#include <cassert>
#include <vector>
#include <cstdio>
#include <thread>
#include <chrono>
int main()
{
    std::vector<uint8_t> pixels(320*180*4,255);
    face_tracker *t=face_tracker_create(); assert(t);
    face_result r{};
    assert(!face_tracker_latest(t,&r));
    assert(!face_tracker_submit(t,pixels.data(),1,320,180,1280,720,1,1000000000));
    assert(!face_tracker_busy(t));
    assert(face_tracker_submit(t,pixels.data(),320*4,320,180,1280,720,1,1000000000));
    /* An immediate second submit either drops or follows an already completed job.
     * Do not assert the timing of an asynchronous worker. */
    face_tracker_submit(t,pixels.data(),320*4,320,180,1280,720,1,1000000000);
    for (int i=0;i<1000 && face_tracker_busy(t);++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(!face_tracker_busy(t));
    assert(!face_tracker_latest(t,&r)); /* Blank frame must never become a face. */
    assert(face_tracker_submit(t,pixels.data(),320*4,320,180,1280,720,2,2000000000));
    face_tracker_destroy(t); /* Waits for in-flight detection. */
    face_tracker_destroy(nullptr);
    puts("Vision blank-frame and in-flight teardown tests passed.");
}
