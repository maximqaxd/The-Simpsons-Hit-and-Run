//=============================================================================
// PDDI PVR -- operand cache window onto the TA FIFO
//=============================================================================

#ifndef PVROIX_H
#define PVROIX_H

#ifdef __cplusplus
extern "C" {
#endif

// 0x92000000 is a mirror of the TA polygon FIFO reached through P1, so stores
// to it are cached write-back. With CCR.OIX set the cache index takes bit 25
// of the address, which puts this window in operand cache entries 256..511 --
// entries no main-RAM address can reach, so the window cannot be evicted by
// anything else the frame touches.
#define PVR_OIX_ADDR   0x92000000u
#define PVR_OIX_BYTES  8192u
#define PVR_OIX_VERTS  (PVR_OIX_BYTES / 32u)

// NULL until pvrOixInit() finds the mode usable.
extern unsigned char* pvrOixWindow;

void pvrOixInit ( void );
void pvrOixEnter( void );
void pvrOixLeave( void );

#ifdef __cplusplus
}
#endif

#endif
