//=============================================================================
// PDDI PVR backend utilities (pixel packing, lock-info helpers, math).
//=============================================================================

#ifndef PDDI_PVR_UTIL_HPP
#define PDDI_PVR_UTIL_HPP

#include <pddi/pddi.hpp>

#include <cstdint>

namespace pvrutil {

inline uint16_t PackRGB565(pddiColour c) {
    const uint32_t r = (uint32_t)c.Red();
    const uint32_t g = (uint32_t)c.Green();
    const uint32_t b = (uint32_t)c.Blue();
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

inline uint16_t PackARGB1555(pddiColour c) {
    const uint32_t a = (uint32_t)c.Alpha();
    const uint32_t r = (uint32_t)c.Red();
    const uint32_t g = (uint32_t)c.Green();
    const uint32_t b = (uint32_t)c.Blue();
    return (uint16_t)(((a >= 128 ? 1u : 0u) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

inline uint16_t PackARGB4444(pddiColour c) {
    const uint32_t a = (uint32_t)c.Alpha();
    const uint32_t r = (uint32_t)c.Red();
    const uint32_t g = (uint32_t)c.Green();
    const uint32_t b = (uint32_t)c.Blue();
    return (uint16_t)(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
}

static inline int BitCount(uint32_t x) {
    return __builtin_popcount(x);
}

static inline int FirstSetBit(uint32_t x) {
    return x ? __builtin_ctz(x) : 0;
}

inline void FillLockInfoForFormat(pddiPixelFormat format, pddiLockInfo* lock) {
    uint32_t rMask = 0, gMask = 0, bMask = 0, aMask = 0;

    switch (format) {
        case PDDI_PIXEL_RGB565:
            rMask = 0xF800; gMask = 0x07E0; bMask = 0x001F; aMask = 0x0000;
            lock->format = PDDI_PIXEL_RGB565;
            break;
        case PDDI_PIXEL_ARGB1555:
            aMask = 0x8000; rMask = 0x7C00; gMask = 0x03E0; bMask = 0x001F;
            lock->format = PDDI_PIXEL_ARGB1555;
            break;
        case PDDI_PIXEL_ARGB4444:
            aMask = 0xF000; rMask = 0x0F00; gMask = 0x00F0; bMask = 0x000F;
            lock->format = PDDI_PIXEL_ARGB4444;
            break;
        default:
            // Keep unknown format; caller should handle.
            lock->format = format;
            break;
    }

    lock->rgbaMask[0] = (unsigned)rMask;
    lock->rgbaMask[1] = (unsigned)gMask;
    lock->rgbaMask[2] = (unsigned)bMask;
    lock->rgbaMask[3] = (unsigned)aMask;

    lock->rgbaRShift[0] = (8 - BitCount(rMask)) + 16;
    lock->rgbaRShift[1] = (8 - BitCount(gMask)) + 8;
    lock->rgbaRShift[2] = (8 - BitCount(bMask));
    lock->rgbaRShift[3] = (8 - BitCount(aMask)) + 24;

    lock->rgbaLShift[0] = FirstSetBit(rMask);
    lock->rgbaLShift[1] = FirstSetBit(gMask);
    lock->rgbaLShift[2] = FirstSetBit(bMask);
    lock->rgbaLShift[3] = FirstSetBit(aMask);

    lock->native = false;
}

inline size_t AlignUp32(size_t v) { return (v + 31u) & ~size_t(31u); }

} // namespace pvrutil

#endif // PDDI_PVR_UTIL_HPP

