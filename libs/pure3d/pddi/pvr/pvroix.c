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
// code doing it must itself run uncached. Every routine here is reached
// through its P2 alias; the nops cover the pipeline.
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

// One line only. Where the mode is not really implemented movca.l degenerates
// to a store, and this window's backing store is the TA FIFO, so priming all
// 8 KB before knowing that would push 2 KB of zeroes into it.
__attribute__((noinline)) static int pvrOixProbe_( void )
{
    volatile uint32_t* w = (volatile uint32_t*)PVR_OIX_ADDR;
    int ok;

    irq_mask_t mask = irq_disable();

    dcache_purge_all();

    *kCCR |= CCR_OIX;
    PIPELINE_SETTLE();

    dcache_alloc_block((void*)w, 0);
    w[0] = 0x5341524Du;
    w[7] = 0x32524453u;
    ok = (w[0] == 0x5341524Du) && (w[7] == 0x32524453u);

    // ocbi, not ocbp: this line must never reach the TA.
    dcache_inval_range(PVR_OIX_ADDR, 32);
    dcache_purge_all();

    *kCCR &= ~CCR_OIX;
    PIPELINE_SETTLE();

    irq_restore(mask);
    return ok;
}

static void* pvrOixP2( void* fn )
{
    return (void*)(((uintptr_t)fn & 0x1FFFFFFFu) | 0xA0000000u);
}

// Entered once at startup and never left.
//
// Flipping CCR per frame was the dangerous part. The operand cache index takes
// address bit 25 while OIX is set and bit 13 while it is clear, so a line still
// resident across the change is indexed one way and interpreted the other. On
// hardware that surfaced as the leave routine's own epilogue reading back a
// corrupted return address and returning into the stack.
//
// Nothing forces us to leave. With OIX set, cache entries 256..511 are
// reachable only by addresses carrying bit 25, and this window is the only
// such address in the machine -- main RAM sits at 0x8Cxxxxxx and the store
// queues at 0xE0000000, both with bit 25 clear. No other access can evict the
// primed lines, so they stay valid for the life of the process. The cost is
// that ordinary data gets 8 KB of operand cache instead of 16 KB.
void pvrOixEnter( void )
{
}

void pvrOixLeave( void )
{
}

void pvrOixInit( void )
{
    void (*enter)( void ) = (void (*)(void))pvrOixP2((void*)&pvrOixEnter_);
    int  (*probe)( void ) = (int  (*)(void))pvrOixP2((void*)&pvrOixProbe_);

#ifndef SRR_DC_OIX
    (void)enter; (void)probe;
    printf( "[pvr] operand cache TA window: disabled at build time\n" );
    return;
#else
    if (probe())
    {
        pvrOixWindow = (unsigned char*)PVR_OIX_ADDR;
        enter();
    }

    printf( "[pvr] operand cache TA window: %s (%u vertices)\n",
            pvrOixWindow ? "on" : "unavailable",
            pvrOixWindow ? PVR_OIX_VERTS : 0u );
#endif
}
