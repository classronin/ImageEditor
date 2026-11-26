#ifndef CLASSRONIN_QRDATA_H
#define CLASSRONIN_QRDATA_H

/*
 * ClassRonin_QRData
 * -----------------
 * 200 x 200 QR code, embedded as exact 24-bit BGR pixel data.
 *
 * This module is deliberately library-agnostic:
 *   - Pure C++11, no Win32, GDI, D2D, OpenGL, Qt, SDL or image library.
 *   - No external image file at runtime.
 *   - Caller chooses output pixel format and row order.
 *   - The caller creates its own texture/bitmap/surface from the pixels.
 *
 * Source image:
 *   E:\ClassRonin\scan-classronin\scan-classronin-200x200.png
 *
 * RLE storage format:
 *   [count][B][G][R] per run, top row first, count is 1..Width.
 *   This is the compressed storage, not the decoded pixel buffer.
 *
 * Example:
 *   std::vector<std::uint8_t> rgba;
 *   if (ClassRonin::QRCode::Decode(rgba, ClassRonin::QRCode::PixelFormat::Rgba32)) {
 *       // rgba is Width * Height * 4 bytes, top-down, row stride =
 *       // ClassRonin::QRCode::StrideFor(PixelFormat::Rgba32).
 *   }
 *
 * For a bottom-up DIB or engine buffer:
 *   Decode(destination, stride, format, RowOrder::BottomUp);
 *
 * This module does not own destination memory and never calls a graphics API.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ClassRonin
{
namespace QRCode
{
constexpr int Width = 200;
constexpr int Height = 200;

enum class PixelFormat
{
    Bgr24,
    Rgb24,
    Bgra32,
    Rgba32
};

enum class RowOrder
{
    TopDown,
    BottomUp
};

constexpr int BytesPerPixel(PixelFormat format) noexcept
{
    return (format == PixelFormat::Bgr24 || format == PixelFormat::Rgb24) ? 3 : 4;
}

constexpr int StrideFor(PixelFormat format) noexcept
{
    return Width * BytesPerPixel(format);
}

constexpr int PixelBytes(PixelFormat format) noexcept
{
    return StrideFor(format) * Height;
}

/* Read-only access to the embedded RLE bytes.
   Format: [count][B][G][R] per run, top row first.
   The pointer remains valid for the lifetime of the program; do not free it. */
const std::uint8_t* RleData() noexcept;
std::uint32_t RleSize() noexcept;

/* Decode into a tightly packed, top-down vector.
   Default output is RGBA32 because it is the most portable upload format. */
bool Decode(std::vector<std::uint8_t>& pixels,
            PixelFormat format = PixelFormat::Rgba32);

/* Decode into caller-owned memory.
   destinationStride must be at least StrideFor(format).
   The module writes only pixels; it never creates a bitmap or texture. */
bool Decode(std::uint8_t* destination, int destinationStride,
            PixelFormat format, RowOrder rowOrder = RowOrder::TopDown);

} // namespace QRCode
} // namespace ClassRonin

#endif // CLASSRONIN_QRDATA_H
