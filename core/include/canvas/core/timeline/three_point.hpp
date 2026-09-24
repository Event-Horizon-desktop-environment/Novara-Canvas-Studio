#pragma once

#include "canvas/core/timeline/model.hpp"

#include <cmath>
#include <cstdint>

namespace canvas::core::three_point {

struct Marks {
    int64_t src_in = 0;
    int64_t src_out = 0;
    int64_t tl_in = 0;

    [[nodiscard]] int64_t src_span() const noexcept {
        return src_out > src_in ? src_out - src_in : 0;
    }
};

[[nodiscard]] inline int64_t timeline_duration(const Marks& m, const double seq_fps,
                                               const double media_fps) noexcept {
    const double ratio = seq_fps > 0.0 && media_fps > 0.0 ? seq_fps / media_fps : 1.0;
    return static_cast<int64_t>(
        std::llround(static_cast<double>(m.src_span()) * ratio));
}

[[nodiscard]] inline Clip make_clip(const MediaId media, const Marks& m, const double seq_fps,
                                    const double media_fps) {
    Clip c;
    c.media = media;
    c.src_in = m.src_in;
    c.src_out = m.src_in + m.src_span();
    c.tl_in = m.tl_in;
    c.tl_out = m.tl_in + timeline_duration(m, seq_fps, media_fps);
    return c;
}

struct ResolveInput {
    int64_t src_in = -1;
    int64_t src_out = -1;
    int64_t tl_in = -1;
    int64_t tl_out = -1;
    int64_t playhead = 0;
    int64_t media_frames = 0;
    double seq_fps = 0.0;
    double media_fps = 0.0;
};

[[nodiscard]] inline Marks resolve(const ResolveInput& in) noexcept {
    Marks m;
    const int64_t media = in.media_frames > 0 ? in.media_frames : 0;

    int64_t src_in = in.src_in >= 0 ? in.src_in : 0;
    if (src_in < 0) src_in = 0;
    if (media > 0 && src_in >= media) src_in = media - 1;
    m.src_in = src_in;

    int64_t tl_in = in.tl_in >= 0 ? in.tl_in : in.playhead;
    if (tl_in < 0) tl_in = 0;

    int64_t span = 0;
    if (in.src_out >= 0) {
        span = in.src_out > src_in ? in.src_out - src_in : 1;
    } else if (in.tl_out > tl_in) {
        const double ratio =
            in.media_fps > 0.0 && in.seq_fps > 0.0 ? in.media_fps / in.seq_fps : 1.0;
        span = static_cast<int64_t>(std::llround(static_cast<double>(in.tl_out - tl_in) * ratio));
    } else if (media > 0) {
        span = media - src_in;
    }
    if (span < 1) span = 1;
    if (media > 0 && src_in + span > media) span = media - src_in;
    if (span < 1) span = 1;
    m.src_out = src_in + span;
    m.tl_in = tl_in;
    return m;
}
}
