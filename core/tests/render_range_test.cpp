#include "canvas/core/export/deliver_preset.hpp"
#include "canvas/core/export/exporter.hpp"

#include <cstdio>

using namespace canvas::core;

namespace {

int failures = 0;

void check(const bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    } else {
        std::printf("ok:   %s\n", what);
    }
}

void test_scope_flags() {
    check(!scope_is_range(RenderScope::SingleClip), "single clip is not a range");
    check(!scope_is_range(RenderScope::Still), "still is not a range");
    check(!scope_is_range(RenderScope::FrameSequence), "sequence is not a range");
    check(scope_is_range(RenderScope::Range), "range flag");
    check(!scope_is_image(RenderScope::Range), "range is a video scope, not an image one");
    check(scope_is_image(RenderScope::Still) && scope_is_image(RenderScope::FrameSequence),
          "still/sequence stay image scopes");
    check(static_cast<int>(RenderScope::SingleClip) == 0 &&
              static_cast<int>(RenderScope::IndividualClips) == 1 &&
              static_cast<int>(RenderScope::Still) == 2 &&
              static_cast<int>(RenderScope::FrameSequence) == 3 &&
              static_cast<int>(RenderScope::Range) == 4,
          "existing scope ordinals are stable; Range appends");
}

void test_no_marks_whole_timeline() {
    const RenderRange w = render_range_window(-1, -1, 25, 100);
    check(w.start == 0 && w.count == 100, "no marks -> the whole timeline");
    const RenderRange z = render_range_window(-1, -1, 0, 0);
    check(z.start == 0 && z.count == 0, "empty timeline -> empty window");
}

void test_both_marks() {
    const RenderRange w = render_range_window(10, 40, 7, 100);
    check(w.start == 10 && w.count == 30, "in/out marks -> the marked window");
    const RenderRange clamped = render_range_window(0, 500, 0, 100);
    check(clamped.start == 0 && clamped.count == 100, "out clamps to the timeline end");
    const RenderRange inverted = render_range_window(40, 10, 0, 100);
    check(inverted.start == 40 && inverted.count == 1, "inverted marks never invert the window");
    const RenderRange past_end = render_range_window(150, 200, 0, 100);
    check(past_end.start == 99 && past_end.count == 1, "in past the end clamps inside");
}

void test_partial_marks() {
    const RenderRange in_only = render_range_window(10, -1, 0, 100);
    check(in_only.start == 10 && in_only.count == 90, "in only -> in to the end");
    const RenderRange out_only = render_range_window(-1, 40, 20, 100);
    check(out_only.start == 20 && out_only.count == 20, "out only -> playhead to out");
    const RenderRange out_head = render_range_window(-1, 40, -5, 100);
    check(out_head.start == 0 && out_head.count == 40, "negative playhead clamps to zero");
}

void test_round_trip_window() {
    const RenderRange w = render_range_window(30, 60, 0, 90);
    check(w.start == 30 && w.count == 30, "a named range of 30 frames keeps its length");
}

}

int main() {
    test_scope_flags();
    test_no_marks_whole_timeline();
    test_both_marks();
    test_partial_marks();
    test_round_trip_window();

    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::fprintf(stderr, "%d FAILURES\n", failures);
    return 1;
}
