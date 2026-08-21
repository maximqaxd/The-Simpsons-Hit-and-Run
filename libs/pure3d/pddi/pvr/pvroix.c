//=============================================================================
// PDDI PVR -- operand cache window onto the TA FIFO
//=============================================================================

#include <pddi/pvr/pvroix.h>

#include <stdint.h>
#include <stdio.h>
#include <arch/cache.h>
#include <kos/irq.h>

unsigned char* pvrOixWindow = 0;

static int s_oixOk = 0;

static volatile uint32_t* const kCCR = (volatile uint32_t*)0xFF00001C;

#define CCR_OIX (1u << 7)

// The operand cache index takes address bit 25 while OIX is set and bit 13
// while it is clear. Main RAM always carries bit 25 clear, so with OIX set it
// reaches entries 0..255 only, and with OIX clear it reaches all 512. A line
// left dirty across the flip is therefore parked at an index the new mode can
// no longer look up, and the value silently reverts to what RAM last saw.
//
// dcache_purge_all() handles the bulk, but the compiler re-dirties this
// function's own frame between that call and the store to CCR -- including the
// slot the epilogue reads the return address from. So the sweep over the live
// stack and the flip itself have to be one uninterrupted sequence with nothing
// in between, which means writing them together.
//
// CCR must also be stored from uncached memory; every routine here is reached
// through its P2 alias, and the nops cover the pipeline.

#define OIX_STACK_SWEEP                                                      \
        "mov     r15, r0\n\t"                                                \
        "and     %1, r0\n\t"                                                 \
        "add     #-64, r0\n\t"                                               \
        "mov     #24, r1\n\t"                                                \
        "0:\n\t"                                                             \
        "ocbp    @r0\n\t"                                                    \
        "add     #32, r0\n\t"                                                \
        "dt      r1\n\t"                                                     \
        "bf      0b\n\t"

// Not dcache_inval_range: KOS falls back to a whole-cache purge past a size
// threshold, and a purge writes dirty lines back. These lines are dirty by
// construction -- movca.l marks them so -- and their backing store is the TA.
static void pvrOixInvalWindow( void )
{
    uintptr_t a;
    for (a = PVR_OIX_ADDR; a < PVR_OIX_ADDR + PVR_OIX_BYTES; a += 32)
        __asm__ __volatile__("ocbi @%0" :: "r"(a) : "memory");
}

#define OIX_SETTLE                                                           \
        "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"    \
        "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\t"

static void pvrOixFlipOn( void )
{
    __asm__ __volatile__(
        OIX_STACK_SWEEP
        "mov.l   @%0, r0\n\t"
        "or      %2, r0\n\t"
        "mov.l   r0, @%0\n\t"
        OIX_SETTLE
        :
        : "r"(kCCR), "r"(~31), "r"(CCR_OIX)
        : "r0", "r1", "memory");
}

static void pvrOixFlipOff( void )
{
    __asm__ __volatile__(
        OIX_STACK_SWEEP
        "mov.l   @%0, r0\n\t"
        "and     %2, r0\n\t"
        "mov.l   r0, @%0\n\t"
        OIX_SETTLE
        :
        : "r"(kCCR), "r"(~31), "r"((uint32_t)~CCR_OIX)
        : "r0", "r1", "memory");
}

__attribute__((noinline)) static void pvrOixEnter_( void )
{
    irq_mask_t mask = irq_disable();

    dcache_purge_all();
    pvrOixFlipOn();

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

    pvrOixInvalWindow();
    dcache_purge_all();
    pvrOixFlipOff();

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
    pvrOixFlipOn();

    dcache_alloc_block((void*)w, 0);
    w[0] = 0x5341524Du;
    w[7] = 0x32524453u;
    ok = (w[0] == 0x5341524Du) && (w[7] == 0x32524453u);

    __asm__ __volatile__("ocbi @%0" :: "r"((uintptr_t)PVR_OIX_ADDR) : "memory");
    dcache_purge_all();
    pvrOixFlipOff();

    irq_restore(mask);
    return ok;
}

static void* pvrOixP2( void* fn )
{
    return (void*)(((uintptr_t)fn & 0x1FFFFFFFu) | 0xA0000000u);
}

void pvrOixEnter( void )
{
    if (!s_oixOk)
        return;

    ((void (*)(void))pvrOixP2((void*)&pvrOixEnter_))();
    pvrOixWindow = (unsigned char*)PVR_OIX_ADDR;
}

// The window is only primed between enter and leave, and a store into an
// unprimed line read-allocates from a write-only FIFO. Dropping the pointer
// here is what keeps submission outside the bracket -- the front end loads
// through one -- on the plain RAM path instead of faulting.
void pvrOixLeave( void )
{
    if (!pvrOixWindow)
        return;

    pvrOixWindow = 0;
    ((void (*)(void))pvrOixP2((void*)&pvrOixLeave_))();
}

void pvrOixInit( void )
{
#ifndef SRR_DC_OIX
    printf( "[pvr] operand cache TA window: disabled at build time\n" );
#else
    s_oixOk = ((int (*)(void))pvrOixP2((void*)&pvrOixProbe_))();

    printf( "[pvr] operand cache TA window: %s (%u vertices)\n",
            s_oixOk ? "on" : "unavailable",
            s_oixOk ? PVR_OIX_VERTS : 0u );
#endif
}
