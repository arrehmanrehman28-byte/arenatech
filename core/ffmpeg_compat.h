#pragma once
// FFmpeg version-compatibility helpers shared by the media and render
// libraries. Header-only; include it only from translation units that link
// FFmpeg (this header pulls in the FFmpeg C headers).

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace arena::ff {

// Pick the sample format an audio encoder wants, preferring
// AV_SAMPLE_FMT_FLTP (what the native AAC encoder prefers).
//
// FFmpeg >= 7.1 (libavcodec >= 61.19.100) exposes the supported-config API;
// older builds (e.g. FFmpeg 6.1 in the Ubuntu 24.04 repositories) still
// carry the static encoder table, so use that as the fallback.
inline AVSampleFormat pickEncoderSampleFormat(const AVCodec *enc) {
    AVSampleFormat first = AV_SAMPLE_FMT_NONE;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 19, 100)
    const void *cfg = nullptr;
    int cfgCount = 0;
    if (avcodec_get_supported_config(nullptr, enc, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0,
                                     &cfg, &cfgCount) == 0 && cfg) {
        const auto *fmts = static_cast<const AVSampleFormat *>(cfg);
        for (int i = 0; fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
            if (fmts[i] == AV_SAMPLE_FMT_FLTP) return AV_SAMPLE_FMT_FLTP;
            if (first == AV_SAMPLE_FMT_NONE) first = fmts[i];
        }
        return first;
    }
#else
    if (enc->sample_fmts) {
        const auto *fmts = enc->sample_fmts;
        for (int i = 0; fmts[i] != AV_SAMPLE_FMT_NONE; ++i) {
            if (fmts[i] == AV_SAMPLE_FMT_FLTP) return AV_SAMPLE_FMT_FLTP;
            if (first == AV_SAMPLE_FMT_NONE) first = fmts[i];
        }
    }
#endif
    return first;
}

} // namespace arena::ff
