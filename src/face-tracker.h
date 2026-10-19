/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FACE_MAX_POINTS 64
#define FACE_REGIONS 7
/* All coordinates are top-left-origin normalized filter-input coordinates. */
struct face_point { float x, y; };
struct face_region { uint32_t count; struct face_point points[FACE_MAX_POINTS]; };
struct face_box { float x, y, w, h; };
enum face_region_id { FACE_OUTLINE, FACE_LEFT_EYE, FACE_RIGHT_EYE,
    FACE_LEFT_BROW, FACE_RIGHT_BROW, FACE_NOSE, FACE_LIPS };
struct face_result {
    uint64_t timestamp_ns, generation;
    uint32_t width, height;
    bool valid;
    struct face_box box;
    struct face_region regions[FACE_REGIONS];
};
struct face_tracker;
struct face_tracker *face_tracker_create(void);
/* Joins the worker before returning. Never call from its worker. */
void face_tracker_destroy(struct face_tracker *tracker);
bool face_tracker_busy(struct face_tracker *tracker);
/* Copies top-down RGBA8 input; false when busy or allocation fails. */
bool face_tracker_submit(struct face_tracker *tracker, const uint8_t *rgba,
    uint32_t stride, uint32_t width, uint32_t height, uint32_t source_width,
    uint32_t source_height, uint64_t generation, uint64_t timestamp_ns);
bool face_tracker_latest(struct face_tracker *tracker, struct face_result *result);
/* Pure helpers shared with behavioral tests. */
struct face_point face_from_vision(struct face_box bottom_left_box, float x, float y);
float face_box_iou(struct face_box a, struct face_box b);
int face_select(const struct face_box *boxes, unsigned count, const struct face_box *previous);
float face_freshness(uint64_t now, uint64_t timestamp);
bool face_result_matches(const struct face_result *r, uint64_t generation,
    uint32_t width, uint32_t height);
/* Close a raw Vision jaw contour using a roll-aware brow/chin forehead estimate. */
bool face_complete_forehead(struct face_result *result);
/* RGBA8: R=face, G=feature protection, B=lip protection, A=255. */
void face_mask_build(const struct face_result *result, uint8_t *rgba, unsigned width, unsigned height);
#ifdef __cplusplus
}
#endif
