#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace canvas::core::loudness {

inline constexpr float kSilenceLufs = -70.0f;

[[nodiscard]] float normalization_gain_db(float measured_lufs, float target_lufs) noexcept;

[[nodiscard]] float integrated_loudness_lufs(std::span<const float> mono,
                                             double sample_rate) noexcept;

[[nodiscard]] float integrated_loudness_lufs_interleaved(std::span<const float> interleaved,
                                                         int channels,
                                                         double sample_rate) noexcept;

class Accumulator {
public:
    Accumulator() = default;
    Accumulator(double sample_rate, int channels) : sample_rate_(sample_rate),
                                                    channels_(channels) {}

    void set(double sample_rate, int channels) {
        sample_rate_ = sample_rate;
        channels_ = channels;
    }

    void push(std::span<const float> interleaved);

    [[nodiscard]] float lufs() const noexcept;

    [[nodiscard]] bool empty() const noexcept { return powers_.empty(); }

    [[nodiscard]] int channels() const noexcept { return channels_; }

    [[nodiscard]] std::size_t frames_seen() const noexcept { return frames_; }

private:
    void drain();

    double sample_rate_ = 0.0;
    int channels_ = 0;
    int64_t block_frames_ = 0;
    int64_t hop_frames_ = 0;
    std::vector<float> pending_;
    std::vector<double> powers_;
    std::size_t frames_ = 0;
};

}
