#include "converter.h"
#include "stats.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

#include <getopt.h>

// ---------------------------------------------------------------------------
// Verbose output (modeled on `magick identify -verbose`)
// ---------------------------------------------------------------------------

static std::string human_size(uint64_t bytes) {
    char buf[64];
    if (bytes >= 1024ULL * 1024 * 1024)
        std::snprintf(buf, sizeof(buf), "%.4gGiB", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ULL * 1024)
        std::snprintf(buf, sizeof(buf), "%.6gMiB", bytes / (1024.0 * 1024));
    else if (bytes >= 1024ULL)
        std::snprintf(buf, sizeof(buf), "%.4gKiB", bytes / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%llu bytes", (unsigned long long)bytes);
    return buf;
}

static void print_result(const ConversionResult& res, const Config& cfg) {
    const auto& cs = res.channel_stats;
    uint64_t    px = cs.total_pixels;

    std::printf("Image: %s\n", cfg.output_path.c_str());
    std::printf("  Format: JPEG (Joint Photographic Experts Group JFIF format)\n");
    std::printf("  Geometry: %ux%u+0+0\n", res.width, res.height);
    std::printf("  Colorspace: CMYK\n");
    std::printf("  Type: ColorSeparation\n");
    std::printf("  Depth: 8-bit\n");
    std::printf("  Channels: 4.0\n");
    std::printf("  Channel statistics:\n");
    std::printf("    Pixels: %llu\n", (unsigned long long)px);

    for (const auto& ch : cs.channels) {
        double min_n = ch.min_val / 255.0;
        double max_n = ch.max_val / 255.0;
        double mean_n = ch.mean   / 255.0;
        double med_n  = ch.median / 255.0;
        double std_n  = ch.std_dev / 255.0;

        std::printf("    %s:\n",        ch.name.c_str());
        std::printf("      min: %d  (%.7g)\n",  ch.min_val, min_n);
        std::printf("      max: %d  (%.7g)\n",  ch.max_val, max_n);
        std::printf("      mean: %.4g  (%.7g)\n", ch.mean, mean_n);
        std::printf("      median: %d  (%.7g)\n", ch.median, med_n);
        std::printf("      standard deviation: %.4g  (%.7g)\n", ch.std_dev, std_n);
    }

    // Total ink density: maximum per-pixel sum of ink percentages (print spec measure)
    std::printf("  Total ink density: %.3f%%\n", res.max_total_ink_density);

    std::printf("  Profiles:\n");
    std::printf("    Profile-icc: %u bytes\n", res.embedded_profile_size);
    if (!res.embedded_profile_desc.empty())
        std::printf("      icc:description: %s\n", res.embedded_profile_desc.c_str());

    std::printf("  Filesize: %s\n", human_size(res.output_file_size).c_str());
    std::printf("  Number pixels: %llu\n", (unsigned long long)px);
}

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------

static void print_usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s --strip-profile --sRGB <profile.icc> --CMYK <profile.icc>\n"
        "           [--quality <1-100>] <input.jpg> <output.jpg>\n"
        "\n"
        "Options:\n"
        "  --strip-profile       Strip embedded ICC profile from input before processing\n"
        "  --sRGB <path>         Source sRGB ICC profile to assign\n"
        "  --CMYK <path>         Destination CMYK ICC profile to convert into\n"
        "  --quality <1-100>     Output JPEG quality (default: preserve input quality)\n"
        "  --help                Show this help message\n",
        prog);
}

static Config parse_args(int argc, char* argv[]) {
    static const struct option long_opts[] = {
        {"strip-profile", no_argument,       nullptr, 's'},
        {"sRGB",          required_argument, nullptr, 'r'},
        {"CMYK",          required_argument, nullptr, 'c'},
        {"quality",       required_argument, nullptr, 'q'},
        {"help",          no_argument,       nullptr, 'h'},
        {nullptr,         0,                 nullptr,  0 }
    };

    Config cfg;
    int opt;
    while ((opt = getopt_long(argc, argv, "sr:c:q:h", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 's': cfg.strip_profile = true; break;
            case 'r': cfg.srgb_profile_path = optarg; break;
            case 'c': cfg.cmyk_profile_path = optarg; break;
            case 'q':
                cfg.quality_specified = true;
                cfg.quality = std::atoi(optarg);
                if (cfg.quality < 1 || cfg.quality > 100)
                    throw std::invalid_argument("--quality must be between 1 and 100");
                break;
            case 'h':
                print_usage(argv[0]);
                std::exit(0);
            default:
                throw std::invalid_argument("Unknown option");
        }
    }

    int remaining = argc - optind;
    if (remaining != 2)
        throw std::invalid_argument("Expected exactly two positional arguments: input.jpg output.jpg");

    cfg.input_path  = argv[optind];
    cfg.output_path = argv[optind + 1];

    if (cfg.srgb_profile_path.empty())
        throw std::invalid_argument("--sRGB profile path is required");
    if (cfg.cmyk_profile_path.empty())
        throw std::invalid_argument("--CMYK profile path is required");

    return cfg;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    Config cfg;
    try {
        cfg = parse_args(argc, argv);
    } catch (const std::invalid_argument& e) {
        std::fprintf(stderr, "Error: %s\n\n", e.what());
        print_usage(argv[0]);
        return 1;
    }

    try {
        ConversionResult result = run_conversion(cfg);
        print_result(result, cfg);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        // Attempt to remove partial output
        if (!cfg.output_path.empty())
            std::remove(cfg.output_path.c_str());
        return 1;
    }

    return 0;
}
