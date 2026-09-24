#include "canvas/core/export/loudness.hpp"

#include "canvas/core/timeline/audio_mix.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace canvas::core::loudness {

namespace {
constexpr double kEnergyFloor = 1e-10;

const auto lu_of = [](const double mean_sq) {
    return 10.0 * std::log10(std::max(mean_sq, kEnergyFloor));
};

float gate_powers(std::vector<double> means) {
    if (means.empty()) return kSilenceLufs;

    std::vector<double> gated;
    for (const double m : means)
        if (lu_of(m) >= kSilenceLufs) gated.push_back(m);
    if (gated.empty()) return kSilenceLufs;

    double sum = 0.0;
    for (const double m : gated) sum += m;
    const double abs_mean = sum / static_cast<double>(gated.size());
    const double rel_threshold = lu_of(abs_mean) - 10.0;

    double kept_sum = 0.0;
    std::size_t kept = 0;
    for (const double m : gated) {
        if (lu_of(m) >= rel_threshold) {
            kept_sum += m;
            ++kept;
        }
    }
    if (kept == 0) return kSilenceLufs;
    return static_cast<float>(lu_of(kept_sum / static_cast<double>(kept)));
}

void block_powers(const float* data, const std::size_t samples, const int channels,
                  const std::size_t block, const std::size_t hop,
                  std::vector<double>* out) {
    if (channels <= 0 || block == 0) return;
    const std::size_t block_samples = block * static_cast<std::size_t>(channels);
    for (std::size_t start = 0; start + block_samples <= samples; start += hop *
                                                                          static_cast<std::size_t>(channels)) {
        double sum = 0.0;
        for (std::size_t i = start; i < start + block_samples; ++i) {
            const double s = data[i];
            sum += s * s;
        }
        out->push_back(sum / static_cast<double>(block_samples));
    }
}

std::size_t block_frames_of(const double sample_rate) {
    return sample_rate > 0.0
               ? static_cast<std::size_t>(std::max<int64_t>(1, std::llround(0.4 * sample_rate)))
               : 0;
}

std::size_t hop_frames_of(const double sample_rate) {
    return sample_rate > 0.0
               ? static_cast<std::size_t>(std::max<int64_t>(1, std::llround(0.1 * sample_rate)))
               : 0;
}

}

float normalization_gain_db(const float measured_lufs, const float target_lufs) noexcept {
    float gain = target_lufs - measured_lufs;
    if (!std::isfinite(gain)) gain = audio_mix::kMaxVolumeDb;
    return audio_mix::normalize_volume_db(gain);
}

float integrated_loudness_lufs(const std::span<const float> mono,
                               const double sample_rate) noexcept {
    if (mono.empty() || sample_rate <= 0.0) return kSilenceLufs;

    const std::size_t block = block_frames_of(sample_rate);
    const std::size_t hop = hop_frames_of(sample_rate);
    if (mono.size() < block) return kSilenceLufs;

    std::vector<double> means;
    block_powers(mono.data(), mono.size(), 1, block, hop, &means);
    return gate_powers(std::move(means));
}

float integrated_loudness_lufs_interleaved(const std::span<const float> interleaved,
                                           const int channels,
                                           const double sample_rate) noexcept {
    if (interleaved.empty() || channels <= 0 || sample_rate <= 0.0) return kSilenceLufs;

    const std::size_t block = block_frames_of(sample_rate);
    const std::size_t hop = hop_frames_of(sample_rate);
    const std::size_t frames = interleaved.size() / static_cast<std::size_t>(channels);
    if (frames < block) return kSilenceLufs;

    std::vector<double> means;
    block_powers(interleaved.data(), interleaved.size(), channels, block, hop, &means);
    return gate_powers(std::move(means));
}

void Accumulator::push(const std::span<const float> interleaved) {
    if (channels_ <= 0 || sample_rate_ <= 0.0) return;
    if (block_frames_ == 0) {
        block_frames_ = static_cast<int64_t>(block_frames_of(sample_rate_));
        hop_frames_ = static_cast<int64_t>(hop_frames_of(sample_rate_));
        if (block_frames_ <= 0 || hop_frames_ <= 0) return;
    }
    if (!interleaved.empty()) {
        pending_.insert(pending_.end(), interleaved.begin(), interleaved.end());
        frames_ += interleaved.size() / static_cast<std::size_t>(channels_);
    }
    drain();
}

void Accumulator::drain() {
    const std::size_t ch = static_cast<std::size_t>(channels_);
    const std::size_t block_samples = static_cast<std::size_t>(block_frames_) * ch;
    const std::size_t hop_samples = static_cast<std::size_t>(hop_frames_) * ch;
    while (pending_.size() >= block_samples) {
        double sum = 0.0;
        for (std::size_t i = 0; i < block_samples; ++i) {
            const double s = pending_[i];
            sum += s * s;
        }
        powers_.push_back(sum / static_cast<double>(block_samples));
        pending_.erase(pending_.begin(),
                       pending_.begin() + static_cast<std::ptrdiff_t>(hop_samples));
    }
}

float Accumulator::lufs() const noexcept {
    if (powers_.empty()) return kSilenceLufs;
    return gate_powers(powers_);
}

}
