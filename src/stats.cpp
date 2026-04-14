#include "stats.h"

#include <cmath>
#include <cstring>

StatsAccumulator::StatsAccumulator() {
    memset(histograms_, 0, sizeof(histograms_));
    total_pixels_ = 0;
    max_ink_sum_  = 0;
}

void StatsAccumulator::accumulate(const uint8_t* cmyk_row, uint32_t width) {
    // cmyk_row bytes are in Adobe convention (TYPE_CMYK_*_REV): 255=no ink, 0=full ink.
    // Invert to IM display convention (0=no ink, 255=full ink) before accumulating.
    for (uint32_t x = 0; x < width; x++) {
        uint8_t c = 255u - cmyk_row[x * 4 + 0];
        uint8_t m = 255u - cmyk_row[x * 4 + 1];
        uint8_t y = 255u - cmyk_row[x * 4 + 2];
        uint8_t k = 255u - cmyk_row[x * 4 + 3];

        histograms_[0][c]++;
        histograms_[1][m]++;
        histograms_[2][y]++;
        histograms_[3][k]++;

        // Per-pixel total ink (sum of ink percentages, raw 0-1020)
        uint32_t ink_sum = static_cast<uint32_t>(c) + m + y + k;
        if (ink_sum > max_ink_sum_) max_ink_sum_ = ink_sum;
    }
    total_pixels_ += width;
}

double StatsAccumulator::max_total_ink_density() const {
    return max_ink_sum_ / 255.0 * 100.0;
}

ChannelStats StatsAccumulator::finalize() const {
    static const char* names[4] = {"Cyan", "Magenta", "Yellow", "Black"};

    ChannelStats result;
    result.total_pixels = total_pixels_;

    for (int ch = 0; ch < 4; ch++) {
        auto& out       = result.channels[ch];
        const auto* hist = histograms_[ch];
        out.name = names[ch];
        memcpy(out.histogram, hist, sizeof(uint64_t) * 256);

        // Min: first non-zero bin
        out.min_val = 0;
        for (int v = 0; v < 256; v++) {
            if (hist[v] > 0) { out.min_val = static_cast<uint8_t>(v); break; }
        }

        // Max: last non-zero bin
        out.max_val = 255;
        for (int v = 255; v >= 0; v--) {
            if (hist[v] > 0) { out.max_val = static_cast<uint8_t>(v); break; }
        }

        // Mean
        double sum = 0.0;
        for (int v = 0; v < 256; v++)
            sum += static_cast<double>(v) * static_cast<double>(hist[v]);
        out.mean = sum / static_cast<double>(total_pixels_);

        // Std dev via E[x^2] - E[x]^2
        double sum_sq = 0.0;
        for (int v = 0; v < 256; v++)
            sum_sq += static_cast<double>(v) * static_cast<double>(v) * static_cast<double>(hist[v]);
        double variance = sum_sq / static_cast<double>(total_pixels_) - out.mean * out.mean;
        out.std_dev = std::sqrt(std::max(0.0, variance));

        // Median: cumulative sum to 50th percentile
        uint64_t half       = total_pixels_ / 2;
        uint64_t cumulative = 0;
        out.median = 0;
        for (int v = 0; v < 256; v++) {
            cumulative += hist[v];
            if (cumulative > half) { out.median = static_cast<uint8_t>(v); break; }
        }
    }

    return result;
}
