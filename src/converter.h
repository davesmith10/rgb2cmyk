#pragma once

#include "stats.h"

#include <cstdint>
#include <string>

struct Config {
    std::string input_path;
    std::string output_path;
    std::string srgb_profile_path;
    std::string cmyk_profile_path;
    bool strip_profile     = false;
    bool quality_specified = false;
    int  quality           = 85;
};

struct ConversionResult {
    uint32_t    width                      = 0;
    uint32_t    height                     = 0;
    uint64_t    output_file_size           = 0;
    std::string embedded_profile_desc;
    uint32_t    embedded_profile_size      = 0;
    double      max_total_ink_density      = 0.0;
    ChannelStats channel_stats;
};

ConversionResult run_conversion(const Config& config);
