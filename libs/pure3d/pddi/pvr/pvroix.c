//=============================================================================
// PDDI PVR -- operand cache window onto the TA FIFO
//=============================================================================

#include <pddi/pvr/pvroix.h>

#include <stdint.h>
#include <stdio.h>
#include <arch/cache.h>
#include <kos/irq.h>

unsigned char* pvrOixWindow = 0;

static volatile uint32_t* const kCCR = (volatile uint32_t*)0xFF00001C;

#define CCR_OIX (1u << 7)

// CCR must not change while the pipeline still holds cached accesses, and the
// code doing it must itself run uncached. Both routines are reached through
// their P2 alias; the nops cover the pipeline.
#define PIPELINE_SETTLE()                                                    \
    __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"        \
                         "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"        \
                         "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop")

__attribute__((noinline)) static void pvrOixEnter_( void )
{
    irq_mask_t mask = irq_disable();

    dcache_purge_all();

    *kCCR |= CCR_OIX;
    PIPELINE_SETTLE();

    // movca.l allocates a line without fetching it. The window's backing store
    // is a write-only FIFO, so a normal store missing the cache would try to
    // read-allocate from it; priming every line first means no store ever does.
    {
        uintptr_t a;
        for (a = PVR_OIX_ADDR; a < PVR_OIX_ADDR + PVR_OIX_BYTES; a += 32)
            dcache_alloc_block((void*)a, 0);
    }

    irq_restore(mask);
}

__attribute__((noinline)) static void pvrOixLeave_( void )
{
    irq_mask_t mask = irq_disable();

    // ocbi, not ocbp: whatever is left in the window is a partly built vertex
    // and must never reach the TA.
    dcache_inval_range(PVR_OIX_ADDR, PVR_OIX_BYTES);
    dcache_purge_all();

    *kCCR &= ~CCR_OIX;
    PIPELINE_SETTLE();

    irq_restore(mask);
}

static void (*s_enter)( void ) = 0;
static void (*s_leave)( void ) = 0;

static void* pvrOixP2( void* fn )
{
    return (void*)(((uintptr_t)fn & 0x1FFFFFFFu) | 0xA0000000u);
}

void pvrOixEnter( void )
{
    if (pvrOixWindow)
        s_enter();
}

void pvrOixLeave( void )
{
    if (pvrOixWindow)
        s_leave();
}

__attribute__((noinline)) static int pvrOixProbe_( void )
{
    volatile uint32_t* w = (volatile uint32_t*)PVR_OIX_ADDR;
    int ok;

    irq_mask_t mask = irq_disable();

    dcache_purge_all();

    *kCCR |= CCR_OIX;
    PIPELINE_SETTLE();

    // One line only. Where the mode is not really implemented movca.l
    // degenerates to a store, and the window's backing store is the TA FIFO --
    // priming all 8 KB before knowing that would push 2 KB of zeroes into it.
    dcache_alloc_block((void*)w, 0);
    w[0] = 0x5341524Du;
    w[7] = 0x32524453u;
    ok = (w[0] == 0x5341524Du) && (w[7] == 0x32524453u);

    dcache_inval_range(PVR_OIX_ADDR, 32);

    *kCCR &= ~CCR_OIX;
    PIPELINE_SETTLE();

    irq_restore(mask);
    return ok;
}

void pvrOixInit( void )
{
    s_enter = (void (*)(void))pvrOixP2((void*)&pvrOixEnter_);
    s_leave = (void (*)(void))pvrOixP2((void*)&pvrOixLeave_);

    {
        int (*probe)( void ) = (int (*)(void))pvrOixP2((void*)&pvrOixProbe_);
        if (probe())
            pvrOixWindow = (unsigned char*)PVR_OIX_ADDR;
    }

    printf( "[pvr] operand cache TA window: %s (%u vertices)\n",
            pvrOixWindow ? "on" : "unavailable",
            pvrOixWindow ? PVR_OIX_VERTS : 0u );
}
