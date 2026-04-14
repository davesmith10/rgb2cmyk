#include "converter.h"

#include <lcms2.h>
#include <jpeglib.h>
#include <jerror.h>

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// libjpeg error handler — safe for use with C++ objects above setjmp frame
// ---------------------------------------------------------------------------

struct JpegErrorHandler {
    jpeg_error_mgr base;               // MUST be first member
    jmp_buf        jump_buffer;
    char           message[JMSG_LENGTH_MAX];
};

static void jpeg_error_exit_fn(j_common_ptr cinfo) {
    auto* err = reinterpret_cast<JpegErrorHandler*>(cinfo->err);
    (cinfo->err->format_message)(cinfo, err->message);
    longjmp(err->jump_buffer, 1);
}

// ---------------------------------------------------------------------------
// RAII wrappers — trivial constructors so they can safely straddle setjmp
// ---------------------------------------------------------------------------

struct DecompressGuard {
    jpeg_decompress_struct cinfo{};
    bool created = false;
    ~DecompressGuard() { if (created) jpeg_destroy_decompress(&cinfo); }
};

struct CompressGuard {
    jpeg_compress_struct cinfo{};
    bool created = false;
    ~CompressGuard() { if (created) jpeg_destroy_compress(&cinfo); }
};

struct CmsProfileGuard {
    cmsHPROFILE handle = nullptr;
    ~CmsProfileGuard() { if (handle) cmsCloseProfile(handle); }
};

struct CmsTransformGuard {
    cmsHTRANSFORM handle = nullptr;
    ~CmsTransformGuard() { if (handle) cmsDeleteTransform(handle); }
};

struct FileGuard {
    FILE* fp = nullptr;
    explicit FileGuard(FILE* f) : fp(f) {}
    ~FileGuard() { if (fp) std::fclose(fp); }
};

// ---------------------------------------------------------------------------
// lcms2 error handler
// ---------------------------------------------------------------------------

static void lcms_error_fn(cmsContext /*ctx*/, cmsUInt32Number /*code*/,
                           const char* text) {
    throw std::runtime_error(std::string("lcms2: ") + text);
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

static std::vector<uint8_t> load_file_bytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open file: " + path);
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>()
    );
}

static std::string get_profile_description(cmsHPROFILE profile) {
    auto* mlu = static_cast<cmsMLU*>(
        cmsReadTag(profile, cmsSigProfileDescriptionTag));
    if (!mlu) return std::string();

    cmsUInt32Number len = cmsMLUgetASCII(mlu, "en", "US", nullptr, 0);
    if (len == 0) return std::string();

    std::string desc(len, '\0');
    cmsMLUgetASCII(mlu, "en", "US", &desc[0], len);
    // Strip trailing null(s) that lcms2 may include in the count
    while (!desc.empty() && desc.back() == '\0') desc.pop_back();
    return desc;
}

// ---------------------------------------------------------------------------
// ICC profile extraction from APP2 marker list
// ---------------------------------------------------------------------------

static const char ICC_SIGNATURE[] = "ICC_PROFILE\0";  // 12 bytes incl. \0
static const size_t ICC_SIG_LEN   = 12;
static const size_t ICC_HDR_LEN   = 14;  // signature + seq + count

static std::vector<uint8_t> extract_icc_profile(jpeg_saved_marker_ptr marker_list) {
    std::map<uint8_t, std::vector<uint8_t>> chunks;
    uint8_t total_count = 0;

    for (auto* m = marker_list; m != nullptr; m = m->next) {
        if (m->marker != JPEG_APP0 + 2) continue;
        if (m->data_length < ICC_HDR_LEN) continue;
        if (memcmp(m->data, ICC_SIGNATURE, ICC_SIG_LEN) != 0) continue;

        uint8_t seq_no = m->data[ICC_SIG_LEN];
        uint8_t count  = m->data[ICC_SIG_LEN + 1];
        total_count    = count;

        chunks[seq_no] = std::vector<uint8_t>(
            m->data + ICC_HDR_LEN,
            m->data + m->data_length
        );
    }

    if (chunks.empty()) return {};

    std::vector<uint8_t> profile;
    for (uint8_t i = 1; i <= total_count; i++) {
        auto it = chunks.find(i);
        if (it == chunks.end())
            throw std::runtime_error(
                "Incomplete ICC profile: missing chunk " + std::to_string(i));
        profile.insert(profile.end(), it->second.begin(), it->second.end());
    }
    return profile;
}

// ---------------------------------------------------------------------------
// ICC profile embedding into APP2 markers
// ---------------------------------------------------------------------------

static const size_t MAX_CHUNK_DATA = 65519;  // 65535 - 2 (len) - 14 (hdr)

static void write_icc_markers(jpeg_compress_struct* cinfo,
                               const std::vector<uint8_t>& profile) {
    size_t profile_size = profile.size();
    size_t num_chunks   = (profile_size + MAX_CHUNK_DATA - 1) / MAX_CHUNK_DATA;

    if (num_chunks > 255)
        throw std::runtime_error("ICC profile too large to embed (>255 APP2 chunks)");

    std::vector<uint8_t> buf(ICC_HDR_LEN + MAX_CHUNK_DATA);

    for (size_t chunk = 0; chunk < num_chunks; chunk++) {
        size_t offset         = chunk * MAX_CHUNK_DATA;
        size_t chunk_data_len = std::min(MAX_CHUNK_DATA, profile_size - offset);

        memcpy(buf.data(), ICC_SIGNATURE, ICC_SIG_LEN);
        buf[ICC_SIG_LEN]     = static_cast<uint8_t>(chunk + 1);  // 1-based
        buf[ICC_SIG_LEN + 1] = static_cast<uint8_t>(num_chunks);

        memcpy(buf.data() + ICC_HDR_LEN, profile.data() + offset, chunk_data_len);

        jpeg_write_marker(cinfo, JPEG_APP0 + 2,
                          buf.data(),
                          static_cast<unsigned int>(ICC_HDR_LEN + chunk_data_len));
    }
}

// ---------------------------------------------------------------------------
// Quantization table copying (preserves input quality without re-estimating)
// ---------------------------------------------------------------------------

static void copy_quant_tables(jpeg_decompress_struct* src,
                               jpeg_compress_struct*   dst) {
    for (int i = 0; i < NUM_QUANT_TBLS; i++) {
        if (!src->quant_tbl_ptrs[i]) continue;

        if (!dst->quant_tbl_ptrs[i])
            dst->quant_tbl_ptrs[i] =
                jpeg_alloc_quant_table(reinterpret_cast<j_common_ptr>(dst));

        memcpy(dst->quant_tbl_ptrs[i]->quantval,
               src->quant_tbl_ptrs[i]->quantval,
               sizeof(UINT16) * DCTSIZE2);
        dst->quant_tbl_ptrs[i]->sent_table = FALSE;
    }
}

// ---------------------------------------------------------------------------
// Main conversion
// ---------------------------------------------------------------------------

ConversionResult run_conversion(const Config& config) {
    cmsSetLogErrorHandler(lcms_error_fn);

    // --- Open files early to catch path errors before doing work ---
    FileGuard in_fg(std::fopen(config.input_path.c_str(), "rb"));
    if (!in_fg.fp)
        throw std::runtime_error("Cannot open input: " + config.input_path);

    FileGuard out_fg(std::fopen(config.output_path.c_str(), "wb"));
    if (!out_fg.fp)
        throw std::runtime_error("Cannot open output: " + config.output_path);

    // --- Load ICC profiles ---
    CmsProfileGuard srgb_prof, cmyk_prof;
    srgb_prof.handle = cmsOpenProfileFromFile(config.srgb_profile_path.c_str(), "r");
    if (!srgb_prof.handle)
        throw std::runtime_error("Cannot open sRGB profile: " + config.srgb_profile_path);

    cmyk_prof.handle = cmsOpenProfileFromFile(config.cmyk_profile_path.c_str(), "r");
    if (!cmyk_prof.handle)
        throw std::runtime_error("Cannot open CMYK profile: " + config.cmyk_profile_path);

    if (cmsGetColorSpace(srgb_prof.handle) != cmsSigRgbData)
        throw std::runtime_error("sRGB profile does not describe an RGB color space");
    if (cmsGetColorSpace(cmyk_prof.handle) != cmsSigCmykData)
        throw std::runtime_error("CMYK profile does not describe a CMYK color space");

    // Build the transform at 16-bit to match ImageMagick's Q16-HDRI internal
    // precision. TYPE_CMYK_16_REV outputs Adobe-inverted values (65535=no ink)
    // matching libjpeg's JCS_CMYK convention after downsample to 8-bit.
    CmsTransformGuard xform;
    xform.handle = cmsCreateTransform(
        srgb_prof.handle, TYPE_RGB_16,
        cmyk_prof.handle, TYPE_CMYK_16_REV,
        INTENT_PERCEPTUAL, 0);
    if (!xform.handle)
        throw std::runtime_error("Failed to create lcms2 color transform");

    // Load CMYK ICC bytes for embedding
    std::vector<uint8_t> cmyk_icc_bytes = load_file_bytes(config.cmyk_profile_path);

    // Get profile description for reporting
    std::string cmyk_desc = get_profile_description(cmyk_prof.handle);

    // --- Set up libjpeg error handlers (declared before setjmp) ---
    JpegErrorHandler jerr_d, jerr_c;
    DecompressGuard  dctx;
    CompressGuard    cctx;

    // Decompressor error handler
    dctx.cinfo.err = jpeg_std_error(&jerr_d.base);
    jerr_d.base.error_exit = jpeg_error_exit_fn;

    if (setjmp(jerr_d.jump_buffer))
        throw std::runtime_error(std::string("JPEG read error: ") + jerr_d.message);

    jpeg_create_decompress(&dctx.cinfo);
    dctx.created = true;
    jpeg_stdio_src(&dctx.cinfo, in_fg.fp);

    // Must request marker saving BEFORE jpeg_read_header
    jpeg_save_markers(&dctx.cinfo, JPEG_APP0 + 2, 0xFFFF);

    jpeg_read_header(&dctx.cinfo, TRUE);

    // Extract embedded ICC profile (between read_header and start_decompress)
    std::vector<uint8_t> input_icc = extract_icc_profile(dctx.cinfo.marker_list);
    // (input_icc is captured for info; --strip-profile means we just don't use it)

    dctx.cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&dctx.cinfo);

    uint32_t width  = dctx.cinfo.output_width;
    uint32_t height = dctx.cinfo.output_height;

    // --- Set up compressor ---
    cctx.cinfo.err = jpeg_std_error(&jerr_c.base);
    jerr_c.base.error_exit = jpeg_error_exit_fn;

    if (setjmp(jerr_c.jump_buffer))
        throw std::runtime_error(std::string("JPEG write error: ") + jerr_c.message);

    jpeg_create_compress(&cctx.cinfo);
    cctx.created = true;
    jpeg_stdio_dest(&cctx.cinfo, out_fg.fp);

    // Set colorspace BEFORE jpeg_set_defaults so defaults initialise 4 components
    cctx.cinfo.image_width      = width;
    cctx.cinfo.image_height     = height;
    cctx.cinfo.input_components = 4;
    cctx.cinfo.in_color_space   = JCS_CMYK;

    jpeg_set_defaults(&cctx.cinfo);
    jpeg_set_colorspace(&cctx.cinfo, JCS_CMYK);

    if (config.quality_specified) {
        jpeg_set_quality(&cctx.cinfo, config.quality, TRUE);
    } else {
        copy_quant_tables(&dctx.cinfo, &cctx.cinfo);
        // Map 4 CMYK components to the 2 quant tables from the source:
        // C,K → luma table [0]; M,Y → chroma table [1]
        cctx.cinfo.comp_info[0].quant_tbl_no = 0;  // Cyan
        cctx.cinfo.comp_info[1].quant_tbl_no = 1;  // Magenta
        cctx.cinfo.comp_info[2].quant_tbl_no = 1;  // Yellow
        cctx.cinfo.comp_info[3].quant_tbl_no = 0;  // Black
    }

    // Optimize Huffman tables for the actual image data (matches IM's default behaviour)
    cctx.cinfo.optimize_coding = TRUE;

    jpeg_start_compress(&cctx.cinfo, TRUE);

    // Embed CMYK ICC profile in APP2 markers (after start_compress,
    // before first scanline)
    write_icc_markers(&cctx.cinfo, cmyk_icc_bytes);

    // --- Row-by-row streaming pipeline ---
    // 8-bit buffers for libjpeg I/O; 16-bit intermediate for lcms2 (Q16 precision)
    StatsAccumulator stats;
    std::vector<uint8_t>  rgb_row(width * 3);
    std::vector<uint16_t> rgb_row_16(width * 3);
    std::vector<uint16_t> cmyk_row_16(width * 4);
    std::vector<uint8_t>  cmyk_row(width * 4);
    JSAMPROW rgb_ptr  = rgb_row.data();
    JSAMPROW cmyk_ptr = cmyk_row.data();

    while (dctx.cinfo.output_scanline < dctx.cinfo.output_height) {
        if (jpeg_read_scanlines(&dctx.cinfo, &rgb_ptr, 1) != 1)
            throw std::runtime_error("jpeg_read_scanlines failed");

        // Upsample 8-bit → 16-bit (0-255 → 0-65535, factor 257 = 65535/255)
        for (uint32_t i = 0; i < width * 3; i++)
            rgb_row_16[i] = static_cast<uint16_t>(rgb_row[i] * 257u);

        // Transform at 16-bit precision
        cmsDoTransform(xform.handle, rgb_row_16.data(), cmyk_row_16.data(), width);

        // Downsample 16-bit → 8-bit
        for (uint32_t i = 0; i < width * 4; i++)
            cmyk_row[i] = static_cast<uint8_t>(cmyk_row_16[i] >> 8);

        stats.accumulate(cmyk_row.data(), width);

        jpeg_write_scanlines(&cctx.cinfo, &cmyk_ptr, 1);
    }

    jpeg_finish_compress(&cctx.cinfo);
    jpeg_finish_decompress(&dctx.cinfo);

    // Flush output file so we can stat it
    std::fflush(out_fg.fp);

    // Get output file size
    long out_size = std::ftell(out_fg.fp);

    // Close files explicitly before returning
    std::fclose(out_fg.fp);
    out_fg.fp = nullptr;
    std::fclose(in_fg.fp);
    in_fg.fp = nullptr;

    ConversionResult result;
    result.width                 = width;
    result.height                = height;
    result.output_file_size      = static_cast<uint64_t>(out_size > 0 ? out_size : 0);
    result.embedded_profile_desc  = cmyk_desc;
    result.embedded_profile_size  = static_cast<uint32_t>(cmyk_icc_bytes.size());
    result.max_total_ink_density  = stats.max_total_ink_density();
    result.channel_stats          = stats.finalize();

    return result;
}
