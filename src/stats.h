#pragma once

#include <array>
#include <cstdint>
#include <string>

struct ChannelStats {
    struct Channel {
        std::string name;
        uint8_t  min_val   = 0;
        uint8_t  max_val   = 0;
        double   mean      = 0.0;
        double   std_dev   = 0.0;
        uint8_t  median    = 0;
        uint64_t histogram[256] = {};
    };
    std::array<Channel, 4> channels;
    uint64_t total_pixels = 0;
};

class StatsAccumulator {
public:
    StatsAccumulator();

    // cmyk_row: width*4 bytes, interleaved CMYK (Adobe convention — 255=no ink).
    // Statistics are accumulated in IM display convention (inverted: 0=no ink).
    void accumulate(const uint8_t* cmyk_row, uint32_t width);

    ChannelStats finalize() const;

    // Maximum total ink density seen across all pixels (0–400 range).
    double max_total_ink_density() const;

private:
    uint64_t histograms_[4][256] = {};
    uint64_t total_pixels_       = 0;
    uint32_t max_ink_sum_        = 0;  // max per-pixel ink sum (0-1020)
};
