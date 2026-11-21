#include "sobel_simd.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace sobel {

namespace {

constexpr int kSobelX[3][3] = {
    {-1, 0, 1},
    {-2, 0, 2},
    {-1, 0, 1},
};

constexpr int kSobelY[3][3] = {
    {-1, -2, -1},
    { 0,  0,  0},
    { 1,  2,  1},
};

// Calcula Sobel 3x3 escalar para un único píxel (x, y), aplicando zero-padding en los bordes.
inline void sobel_scalar_pixel(const ImageRGBA& input, ImageRGBA& output, int x, int y) noexcept {
    const int width = input.width;
    const int height = input.height;

    int gx[3] = {0, 0, 0};
    int gy[3] = {0, 0, 0};

    for (int i = -1; i <= 1; ++i) {
        const int sy = y + i;
        if (sy < 0 || sy >= height) {
            continue;
        }
        const int ky_row = i + 1;

        for (int j = -1; j <= 1; ++j) {
            const int sx = x + j;
            if (sx < 0 || sx >= width) {
                continue;
            }
            const int kx_col = j + 1;
            const int wx = kSobelX[ky_row][kx_col];
            const int wy = kSobelY[ky_row][kx_col];

            const std::size_t src_off = input.pixel_offset(sx, sy);
            for (int c = 0; c < 3; ++c) {
                const int sample = static_cast<int>(input.data[src_off + static_cast<std::size_t>(c)]);
                gx[c] += sample * wx;
                gy[c] += sample * wy;
            }
        }
    }

    const std::size_t dst_off = output.pixel_offset(x, y);
    for (int c = 0; c < 3; ++c) {
        const int mag = std::abs(gx[c]) + std::abs(gy[c]);
        output.data[dst_off + static_cast<std::size_t>(c)] =
            static_cast<uint8_t>(std::min(255, mag));
    }
    output.data[dst_off + 3] = 255;
}

inline void sobel_scalar_row(const ImageRGBA& input, ImageRGBA& output, int y) noexcept {
    for (int x = 0; x < input.width; ++x) {
        sobel_scalar_pixel(input, output, x, y);
    }
}

#if defined(__AVX2__)

// Calcula Sobel 3x3 sobre 4 píxeles RGBA32 (16 canales uint8_t -> 16 carriles int16_t).
// Emplea la factorización algebraica:
//   d1 = bR - tL,  d2 = tR - bL,  dH = mR - mL,  dV = bC - tC
//   Gx = (d1 + d2) + 2*dH
//   Gy = (d1 - d2) + 2*dV
inline __m256i sobel_4px_avx2(__m128i top_l, __m128i top_c, __m128i top_r,
                              __m128i mid_l,                __m128i mid_r,
                              __m128i bot_l, __m128i bot_c, __m128i bot_r) noexcept {
    const __m256i tL = _mm256_cvtepu8_epi16(top_l);
    const __m256i tC = _mm256_cvtepu8_epi16(top_c);
    const __m256i tR = _mm256_cvtepu8_epi16(top_r);

    const __m256i mL = _mm256_cvtepu8_epi16(mid_l);
    const __m256i mR = _mm256_cvtepu8_epi16(mid_r);

    const __m256i bL = _mm256_cvtepu8_epi16(bot_l);
    const __m256i bC = _mm256_cvtepu8_epi16(bot_c);
    const __m256i bR = _mm256_cvtepu8_epi16(bot_r);

    const __m256i d1 = _mm256_sub_epi16(bR, tL);
    const __m256i d2 = _mm256_sub_epi16(tR, bL);
    const __m256i dH = _mm256_sub_epi16(mR, mL);
    const __m256i dV = _mm256_sub_epi16(bC, tC);

    const __m256i gx = _mm256_add_epi16(_mm256_add_epi16(d1, d2), _mm256_slli_epi16(dH, 1));
    const __m256i gy = _mm256_add_epi16(_mm256_sub_epi16(d1, d2), _mm256_slli_epi16(dV, 1));

    return _mm256_add_epi16(_mm256_abs_epi16(gx), _mm256_abs_epi16(gy));
}

void sobel_avx2_into(const ImageRGBA& input, ImageRGBA& output) noexcept {
    const int width = input.width;
    const int height = input.height;

    if (width < 12 || height < 3) {
        for (int y = 0; y < height; ++y) {
            sobel_scalar_row(input, output, y);
        }
        return;
    }

    // Bordes superior e inferior completos con zero-padding.
    sobel_scalar_row(input, output, 0);
    sobel_scalar_row(input, output, height - 1);

    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
    const __m256i alpha_mask = _mm256_set1_epi32(static_cast<int>(0xFF000000U));

    for (int y = 1; y < height - 1; ++y) {
        const uint8_t* top = input.data.data() + static_cast<std::size_t>(y - 1) * row_bytes;
        const uint8_t* mid = input.data.data() + static_cast<std::size_t>(y)     * row_bytes;
        const uint8_t* bot = input.data.data() + static_cast<std::size_t>(y + 1) * row_bytes;
        uint8_t* dst = output.data.data() + static_cast<std::size_t>(y) * row_bytes;

        // Píxel izquierdo (x = 0) con zero-padding.
        sobel_scalar_pixel(input, output, 0, y);

        // Ventana deslizante: v0 inicia con los píxeles [0, 1, 2, 3].
        __m128i top_v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(top));
        __m128i mid_v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(mid));
        __m128i bot_v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bot));

        int x = 1;
        // Cada iteración procesa 8 píxeles [x .. x+7] leyendo hasta el píxel x+10.
        for (; x + 11 <= width; x += 8) {
            const std::size_t off1 = static_cast<std::size_t>(x + 3) * 4U;
            const std::size_t off2 = static_cast<std::size_t>(x + 7) * 4U;

            const __m128i top_v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(top + off1));
            const __m128i top_v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(top + off2));

            const __m128i mid_v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(mid + off1));
            const __m128i mid_v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(mid + off2));

            const __m128i bot_v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bot + off1));
            const __m128i bot_v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bot + off2));

            // Primer grupo de 4 píxeles: [x, x+1, x+2, x+3].
            const __m128i top_lo_c = _mm_alignr_epi8(top_v1, top_v0, 4);
            const __m128i top_lo_r = _mm_alignr_epi8(top_v1, top_v0, 8);
            const __m128i mid_lo_r = _mm_alignr_epi8(mid_v1, mid_v0, 8);
            const __m128i bot_lo_c = _mm_alignr_epi8(bot_v1, bot_v0, 4);
            const __m128i bot_lo_r = _mm_alignr_epi8(bot_v1, bot_v0, 8);

            const __m256i mag_lo = sobel_4px_avx2(
                top_v0, top_lo_c, top_lo_r,
                mid_v0,           mid_lo_r,
                bot_v0, bot_lo_c, bot_lo_r);

            // Segundo grupo de 4 píxeles: [x+4, x+5, x+6, x+7].
            const __m128i top_hi_c = _mm_alignr_epi8(top_v2, top_v1, 4);
            const __m128i top_hi_r = _mm_alignr_epi8(top_v2, top_v1, 8);
            const __m128i mid_hi_r = _mm_alignr_epi8(mid_v2, mid_v1, 8);
            const __m128i bot_hi_c = _mm_alignr_epi8(bot_v2, bot_v1, 4);
            const __m128i bot_hi_r = _mm_alignr_epi8(bot_v2, bot_v1, 8);

            const __m256i mag_hi = sobel_4px_avx2(
                top_v1, top_hi_c, top_hi_r,
                mid_v1,           mid_hi_r,
                bot_v1, bot_hi_c, bot_hi_r);

            // Empaqueta 2x16 int16 -> 32 uint8 con saturación a [0, 255] y reordena carriles de 64 bits.
            const __m256i packed = _mm256_packus_epi16(mag_lo, mag_hi);
            const __m256i ordered = _mm256_permute4x64_epi64(packed, 0xD8);
            const __m256i result = _mm256_or_si256(ordered, alpha_mask);

            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + static_cast<std::size_t>(x) * 4U), result);

            // Reutiliza v2 ([x+7..x+10]) como v0 para los siguientes 8 píxeles.
            top_v0 = top_v2;
            mid_v0 = mid_v2;
            bot_v0 = bot_v2;
        }

        // Cola residual y borde derecho (x = width - 1).
        for (; x < width; ++x) {
            sobel_scalar_pixel(input, output, x, y);
        }
    }
}

#elif defined(__ARM_NEON) || defined(__aarch64__)

inline int16x8_t sobel_2px_neon(uint8x8_t top_l, uint8x8_t top_c, uint8x8_t top_r,
                                uint8x8_t mid_l,                  uint8x8_t mid_r,
                                uint8x8_t bot_l, uint8x8_t bot_c, uint8x8_t bot_r) noexcept {
    const int16x8_t tL = vreinterpretq_s16_u16(vmovl_u8(top_l));
    const int16x8_t tC = vreinterpretq_s16_u16(vmovl_u8(top_c));
    const int16x8_t tR = vreinterpretq_s16_u16(vmovl_u8(top_r));

    const int16x8_t mL = vreinterpretq_s16_u16(vmovl_u8(mid_l));
    const int16x8_t mR = vreinterpretq_s16_u16(vmovl_u8(mid_r));

    const int16x8_t bL = vreinterpretq_s16_u16(vmovl_u8(bot_l));
    const int16x8_t bC = vreinterpretq_s16_u16(vmovl_u8(bot_c));
    const int16x8_t bR = vreinterpretq_s16_u16(vmovl_u8(bot_r));

    const int16x8_t d1 = vsubq_s16(bR, tL);
    const int16x8_t d2 = vsubq_s16(tR, bL);
    const int16x8_t dH = vsubq_s16(mR, mL);
    const int16x8_t dV = vsubq_s16(bC, tC);

    const int16x8_t gx = vaddq_s16(vaddq_s16(d1, d2), vshlq_n_s16(dH, 1));
    const int16x8_t gy = vaddq_s16(vsubq_s16(d1, d2), vshlq_n_s16(dV, 1));

    return vaddq_s16(vabsq_s16(gx), vabsq_s16(gy));
}

void sobel_neon_into(const ImageRGBA& input, ImageRGBA& output) noexcept {
    const int width = input.width;
    const int height = input.height;

    if (width < 6 || height < 3) {
        for (int y = 0; y < height; ++y) {
            sobel_scalar_row(input, output, y);
        }
        return;
    }

    sobel_scalar_row(input, output, 0);
    sobel_scalar_row(input, output, height - 1);

    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
    const uint8x16_t alpha_mask = vreinterpretq_u8_u32(vdupq_n_u32(0xFF000000U));

    for (int y = 1; y < height - 1; ++y) {
        const uint8_t* top = input.data.data() + static_cast<std::size_t>(y - 1) * row_bytes;
        const uint8_t* mid = input.data.data() + static_cast<std::size_t>(y)     * row_bytes;
        const uint8_t* bot = input.data.data() + static_cast<std::size_t>(y + 1) * row_bytes;
        uint8_t* dst = output.data.data() + static_cast<std::size_t>(y) * row_bytes;

        sobel_scalar_pixel(input, output, 0, y);

        int x = 1;
        for (; x + 5 <= width; x += 4) {
            const std::size_t off_l = static_cast<std::size_t>(x - 1) * 4U;
            const std::size_t off_c = static_cast<std::size_t>(x)     * 4U;
            const std::size_t off_r = static_cast<std::size_t>(x + 1) * 4U;

            const uint8x16_t tL = vld1q_u8(top + off_l);
            const uint8x16_t tC = vld1q_u8(top + off_c);
            const uint8x16_t tR = vld1q_u8(top + off_r);

            const uint8x16_t mL = vld1q_u8(mid + off_l);
            const uint8x16_t mR = vld1q_u8(mid + off_r);

            const uint8x16_t bL = vld1q_u8(bot + off_l);
            const uint8x16_t bC = vld1q_u8(bot + off_c);
            const uint8x16_t bR = vld1q_u8(bot + off_r);

            const int16x8_t mag_lo = sobel_2px_neon(
                vget_low_u8(tL), vget_low_u8(tC), vget_low_u8(tR),
                vget_low_u8(mL),                  vget_low_u8(mR),
                vget_low_u8(bL), vget_low_u8(bC), vget_low_u8(bR));

            const int16x8_t mag_hi = sobel_2px_neon(
                vget_high_u8(tL), vget_high_u8(tC), vget_high_u8(tR),
                vget_high_u8(mL),                   vget_high_u8(mR),
                vget_high_u8(bL), vget_high_u8(bC), vget_high_u8(bR));

            const uint8x16_t packed = vcombine_u8(vqmovun_s16(mag_lo), vqmovun_s16(mag_hi));
            const uint8x16_t result = vorrq_u8(packed, alpha_mask);
            vst1q_u8(dst + off_c, result);
        }

        for (; x < width; ++x) {
            sobel_scalar_pixel(input, output, x, y);
        }
    }
}

#endif

}  // namespace

const char* sobel_simd_backend() noexcept {
#if defined(__AVX2__)
    return "AVX2 (256-bit)";
#elif defined(__ARM_NEON) || defined(__aarch64__)
    return "ARM NEON (128-bit)";
#else
    return "Scalar fallback";
#endif
}

void sobel_simd_into(const ImageRGBA& input, ImageRGBA& output) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    if (output.width != input.width || output.height != input.height || !output.is_valid()) {
        throw std::invalid_argument("Output image dimensions must match input image");
    }

#if defined(__AVX2__)
    sobel_avx2_into(input, output);
#elif defined(__ARM_NEON) || defined(__aarch64__)
    sobel_neon_into(input, output);
#else
    for (int y = 0; y < input.height; ++y) {
        sobel_scalar_row(input, output, y);
    }
#endif
}

ImageRGBA sobel_simd(const ImageRGBA& input) {
    if (!input.is_valid()) {
        throw std::invalid_argument("Input image is not valid");
    }
    ImageRGBA output(input.width, input.height);
    sobel_simd_into(input, output);
    return output;
}

}  // namespace sobel
