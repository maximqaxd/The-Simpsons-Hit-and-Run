//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR context 
//=============================================================================

#include <pddi/pvr/pvr.hpp>
#include <pddi/pvr/pvrcon.hpp>
#include <pddi/pvr/pvrdev.hpp>
#include <pddi/pvr/pvrdisplay.hpp>
#include <pddi/pvr/pvrtex.hpp>
#include <pddi/pvr/pvrmat.hpp>
#include <pddi/pvr/pvrutil.hpp>
#include <pddi/pvr/pvroix.h>
#include <sh4zam/shz_sh4zam.h>

#include <pddi/base/debug.hpp>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>

#include <vector>
#include <algorithm>
#include <kos/timer.h>
#include <arch/cache.h>
#if defined SRR2_DC_PVR_TRACE
#include <kos/timer.h>
#include <stdio.h>
static uint64_t s_replayUs = 0;
static uint64_t s_finishUs = 0;
static uint64_t s_waitUs = 0;
static uint64_t s_frameUs = 0;
static unsigned s_vtxXform = 0;
static unsigned s_vtxEmit = 0;
static unsigned s_vtxEmitIM = 0;
static unsigned s_boxCulled = 0;
static unsigned s_fusedDraws = 0;
static unsigned s_rollDraws = 0;

static uint64_t s_replayImUs = 0;
static unsigned s_tris = 0;
#endif


#ifdef RAD_DC_TRACE_VERTS
#include <stdio.h>
static int s_traceFrame = 0;
static int s_traceBudget = 0;
static void TraceTri(const char* tag, const shz_vec4_t* clip,
                     const float* u, const float* v, float vpOy, float vpHh)
{
    if (s_traceBudget <= 0)
        return;
    s_traceBudget--;
    printf("   [tri] %s\n", tag);
    for (int k = 0; k < 3; k++)
    {
        const float invw = (clip[k].w != 0.0f) ? (1.0f / clip[k].w) : 0.0f;
        const float ndcY = clip[k].y * invw;
        printf("         v%d clipY=%7.4f w=%6.3f ndcY=%7.4f scrY=%6.1f  uv=(%5.3f,%5.3f)\n",
               k, clip[k].y, clip[k].w, ndcY, vpOy + vpHh * (1.0f - ndcY), u[k], v[k]);
    }
}
#endif


//--------------------------------------------------------------
pvrContext::pvrContext(pvrDevice* dev, pvrDisplay* disp)
    : pddiBaseContext((pddiDisplay*)disp, (pddiDevice*)dev)
{
    device = dev;
    display = disp;

    device->AddRef();
    display->AddRef();
    disp->SetContext(this);

    maxTexSize = 1024;

    contextID = 0;
    colourWriteOff = false;

    // Driven from the draw distance rather than the game, which disables fog
    // at view setup. Start partway in so the band is a haze, not an edge.
    fogRGB   = (unsigned)SRR_DC_FOG_RGB;
    fogEnd   = (SRR_DC_DEPTH_CULL > 0.0f) ? SRR_DC_DEPTH_CULL : 0.0f;
    fogStart = fogEnd * 0.55f;
    fogOn    = (fogEnd > 0.0f);
    // Not applied here: this runs before pvr_init, which would reset the
    // registers straight back. Programmed at the first frame instead.
    fogDirty = true;
    scissorOn = false;
    DefaultState();

    defaultShader = new pvrMat(this);
    defaultShader->AddRef();

    currentList = (pvr_list_t)-1;
    begunMask = 0;


    shz_mat4x4_init_identity(&modelViewM);
    shz_mat4x4_init_identity(&projectionM);
    shz_mat4x4_init_identity(&viewProjM);
    viewportX = viewportY = 0;
    viewportW = display->GetWidth();
    viewportH = display->GetHeight();
    displayW = display->GetWidth();
    displayH = display->GetHeight();
}

pvrContext::~pvrContext()
{
    defaultShader->Release();

    display->SetContext(NULL);
    display->Release();
    device->Release();
}

static void pvrResetDeferredLists();
static void pvrRunDeferredLists();
static void pvrInvalidateHeaderCache();

static unsigned s_lastDraws = 0;
#if defined SRR2_DC_PROFILER && !defined SRR2_DC_PVR_TRACE
static unsigned s_boxCulled = 0;
static unsigned s_fusedDraws = 0;
#endif
#if defined SRR2_DC_PROFILER
static unsigned s_lastBoxCulled = 0;
static unsigned s_lastFusedDraws = 0;
// The window holds 256 vertices; anything larger falls back to a RAM buffer
// and the store queues. Split by where the packets actually went.
// Why a draw misses the fast path: it wants an indexed tristrip, and
// anything else is transformed per triangle with no vertex reuse.
static unsigned s_missPrim = 0, s_missIdx = 0, s_missOther = 0;
static unsigned s_lastMissPrim = 0, s_lastMissIdx = 0, s_lastMissOther = 0;
static unsigned s_oixDraws = 0, s_oixVerts = 0;
static unsigned s_sqDraws = 0,  s_sqVerts = 0;
static unsigned s_lastOixDraws = 0, s_lastOixVerts = 0;
static unsigned s_lastSqDraws = 0,  s_lastSqVerts = 0;
// Counted once per draw, not per vertex, so it costs nothing measurable.
static unsigned s_vtxPerFrame = 0;
static unsigned s_lastVtxPerFrame = 0;
extern "C" unsigned pvrLastVertexEstimate( void ) { return s_lastVtxPerFrame; }
// Every packet that actually reaches the TA, whichever path emitted it. The
// hardware counter behind pvr_get_stats() is not emulated, so this is the only
// figure that holds on both.
static unsigned s_vtxEmitted = 0;
static unsigned s_lastVtxEmitted = 0;
extern "C" unsigned pvrLastVertexEmitted( void ) { return s_lastVtxEmitted; }

// Submission split by phase. Marked per draw, never per vertex -- reading the
// timer costs about as much as transforming one vertex, so bracketing the
// loops is affordable and bracketing their bodies is not.
static uint64_t s_phSetup = 0, s_phXform = 0, s_phEmit = 0, s_phClip = 0, s_phImm = 0;
static uint64_t s_lastSetup = 0, s_lastXform = 0, s_lastEmit = 0, s_lastClip = 0, s_lastImm = 0;
extern "C" unsigned pvrPhaseSetupUs( void ) { return (unsigned)s_lastSetup; }
extern "C" unsigned pvrPhaseXformUs( void ) { return (unsigned)s_lastXform; }
extern "C" unsigned pvrPhaseEmitUs ( void ) { return (unsigned)s_lastEmit;  }
extern "C" unsigned pvrPhaseClipUs ( void ) { return (unsigned)s_lastClip;  }
extern "C" unsigned pvrPhaseImmUs  ( void ) { return (unsigned)s_lastImm;   }
// clip was lumping fast-path draws that straddle the near plane together with
// draws that never reached the fast path at all. Count them apart.
static unsigned s_clipDrawsFast = 0, s_clipDrawsGeneric = 0;
static unsigned s_lastClipFast = 0, s_lastClipGeneric = 0;
extern "C" unsigned pvrClipDrawsFast( void )    { return s_lastClipFast; }
extern "C" unsigned pvrClipDrawsGeneric( void ) { return s_lastClipGeneric; }
// The denominator for xform: vertices actually put through the transform,
// which is not the same as the indices the strips reference.
static unsigned s_vtxXformed = 0, s_lastVtxXformed = 0;
extern "C" unsigned pvrLastVertexXformed( void ) { return s_lastVtxXformed; }
// Inside a clipped draw, how much is strip work and how much actually reaches
// the clipper. Decides whether keeping the strip alive through a partial
// triangle is worth a state machine.
static unsigned s_stripTris = 0, s_clipTris = 0, s_deadTris = 0;
static unsigned s_lastStripTris = 0, s_lastClipTris = 0, s_lastDeadTris = 0;
extern "C" unsigned pvrLastStripTris( void ) { return s_lastStripTris; }
extern "C" unsigned pvrLastClipTris( void )  { return s_lastClipTris;  }
extern "C" unsigned pvrLastDeadTris( void )  { return s_lastDeadTris;  }
// The counted triangles do not account for the time, so measure the walk
// itself: how many times round the loop, and how long the clipper alone takes.
static unsigned s_clipIters = 0, s_lastClipIters = 0;
static uint64_t s_phClipTri = 0, s_lastClipTriUs = 0;
extern "C" unsigned pvrLastClipIters( void )  { return s_lastClipIters; }
extern "C" unsigned pvrClipTriUs( void )      { return (unsigned)s_lastClipTriUs; }
// Split the walk by which path fed it. The world meshlets are capped at 256
// vertices; the draws that never reach the fast path are whole skinned meshes
// and can be far larger, so an even spread across draws was never a safe
// assumption.
static unsigned s_genIters = 0, s_lastGenIters = 0;
static uint64_t s_phGenWalk = 0, s_lastGenWalkUs = 0;
extern "C" unsigned pvrLastGenIters( void )  { return s_lastGenIters; }
extern "C" unsigned pvrGenWalkUs( void )     { return (unsigned)s_lastGenWalkUs; }
// How much of the per-draw header and matrix traffic is actually redundant.
static unsigned s_hdrSubmitted = 0, s_hdrSkipped = 0;
static unsigned s_xformLoaded = 0, s_xformSkipped = 0;
static unsigned s_lastHdrSubmitted = 0, s_lastHdrSkipped = 0;
static unsigned s_lastXformLoaded = 0, s_lastXformSkipped = 0;
extern "C" unsigned pvrHdrSubmitted( void )  { return s_lastHdrSubmitted; }
extern "C" unsigned pvrHdrSkipped( void )    { return s_lastHdrSkipped; }
extern "C" unsigned pvrXformLoaded( void )   { return s_lastXformLoaded; }
extern "C" unsigned pvrXformSkipped( void )  { return s_lastXformSkipped; }
// Whether the light pass is running at all, and on how much.
static unsigned s_litVerts = 0, s_litDraws = 0, s_lightCount = 0;
static unsigned s_lastLitVerts = 0, s_lastLitDraws = 0, s_lastLightCount = 0;
extern "C" unsigned pvrLitVerts( void )   { return s_lastLitVerts; }
extern "C" unsigned pvrLitDraws( void )   { return s_lastLitDraws; }
extern "C" unsigned pvrLightCount( void ) { return s_lastLightCount; }
// Recorded, as opposed to consumed: how many draws arrived carrying normals
// and how many distinct light sets got pooled for them.
static unsigned s_recNormalDraws = 0, s_lastRecNormalDraws = 0;
static unsigned s_lastPoolSize = 0, s_lastPoolCount = 0;
extern "C" unsigned pvrRecNormalDraws( void ) { return s_lastRecNormalDraws; }
extern "C" unsigned pvrLightPoolSize( void )  { return s_lastPoolSize; }
// Fullbright has two causes that look identical on screen: the inputs are so
// bright every vertex clamps to white, or the normals are absent so nothing
// varies. Count both rather than reason about which.
static unsigned s_litSat = 0, s_litZeroN = 0;
static unsigned s_lastLitSat = 0, s_lastLitZeroN = 0;
static unsigned s_lastFlushUs = 0, s_lastFinishUs = 0;
extern "C" unsigned pvrFlushUs( void )  { return s_lastFlushUs; }
extern "C" unsigned pvrFinishUs( void ) { return s_lastFinishUs; }
static unsigned s_distCulled = 0, s_lastDistCulled = 0;
extern "C" unsigned pvrDistCulled( void ) { return s_lastDistCulled; }
#define SRR_DEPTH_BINS 8
static unsigned s_depthVerts[SRR_DEPTH_BINS] = { 0 };
static unsigned s_depthDraws[SRR_DEPTH_BINS] = { 0 };
static unsigned s_lastDepthVerts[SRR_DEPTH_BINS] = { 0 };
static unsigned s_lastDepthDraws[SRR_DEPTH_BINS] = { 0 };
extern "C" unsigned pvrDepthVerts( unsigned b )
{
    return (b < SRR_DEPTH_BINS) ? s_lastDepthVerts[b] : 0u;
}
extern "C" unsigned pvrDepthDraws( unsigned b )
{
    return (b < SRR_DEPTH_BINS) ? s_lastDepthDraws[b] : 0u;
}
static unsigned s_ambRGB = 0, s_l0RGB = 0, s_matRGB = 0;
// The light directions as the shader actually sees them, and how hard they hit.
// If object space is not where the skinned normals live, the directions come
// out wrong and every normal reads as facing a light -- which looks exactly
// like a light rig that is simply too bright.
static float s_lDir[4][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
static float s_dotSum = 0.0f;
static unsigned s_dotCount = 0;
static int s_lastDotAvg = 0, s_lastDirI[4][3] = { { 0, 0, 0 }, { 0, 0, 0 },
                                                  { 0, 0, 0 }, { 0, 0, 0 } };
extern "C" int pvrLightDirI( unsigned l, unsigned a )
{
    return (l < 4u && a < 3u) ? s_lastDirI[l][a] : 0;
}
extern "C" int pvrLightDotAvg( void ) { return s_lastDotAvg; }
// Draws arriving with normals, split by whether their material actually asked
// for lighting.
// How the visibility walk classifies the runs it is handed. The prescan costs
// the same for all three, so this says whether the fast path is firing and
// what the scan itself is worth.
static unsigned s_runSkip = 0, s_runSkipV = 0;
static unsigned s_lastRunSkip = 0, s_lastRunSkipV = 0;
extern "C" unsigned pvrRunSkip( void )  { return s_lastRunSkip; }
extern "C" unsigned pvrRunSkipV( void ) { return s_lastRunSkipV; }
static unsigned s_runDead = 0, s_runAll = 0, s_runMixed = 0;
static unsigned s_runAllV = 0, s_runMixedV = 0;
static unsigned s_lastRunDead = 0, s_lastRunAll = 0, s_lastRunMixed = 0;
static unsigned s_lastRunAllV = 0, s_lastRunMixedV = 0;
extern "C" unsigned pvrRunDead( void )   { return s_lastRunDead; }
extern "C" unsigned pvrRunAll( void )    { return s_lastRunAll; }
extern "C" unsigned pvrRunMixed( void )  { return s_lastRunMixed; }
extern "C" unsigned pvrRunAllV( void )   { return s_lastRunAllV; }
extern "C" unsigned pvrRunMixedV( void ) { return s_lastRunMixedV; }
static unsigned s_litFlagOn = 0, s_litFlagOff = 0;
static unsigned s_lastLitFlagOn = 0, s_lastLitFlagOff = 0;
extern "C" unsigned pvrLitFlagOn( void )  { return s_lastLitFlagOn; }
extern "C" unsigned pvrLitFlagOff( void ) { return s_lastLitFlagOff; }
extern "C" unsigned pvrLightMaterial( void ) { return s_matRGB; }
extern "C" unsigned pvrLitSaturated( void )  { return s_lastLitSat; }
extern "C" unsigned pvrLitZeroNormal( void ) { return s_lastLitZeroN; }
extern "C" unsigned pvrLightAmbient( void )  { return s_ambRGB; }
extern "C" unsigned pvrLightZeroCol( void )  { return s_l0RGB; }
extern "C" unsigned pvrLightPoolCount( void ) { return s_lastPoolCount; }
extern "C" unsigned pvrLastBoxCulled( void )  { return s_lastBoxCulled; }
extern "C" unsigned pvrLastFusedDraws( void ) { return s_lastFusedDraws; }
extern "C" unsigned pvrLastMissPrim( void )  { return s_lastMissPrim; }
extern "C" unsigned pvrLastMissIdx( void )   { return s_lastMissIdx; }
extern "C" unsigned pvrLastMissOther( void ) { return s_lastMissOther; }
extern "C" unsigned pvrLastOixDraws( void ) { return s_lastOixDraws; }
extern "C" unsigned pvrLastOixVerts( void ) { return s_lastOixVerts; }
extern "C" unsigned pvrLastSqDraws( void )  { return s_lastSqDraws; }
extern "C" unsigned pvrLastSqVerts( void )  { return s_lastSqVerts; }
// Emit, split by whether the run walk tests visibility. A draw wholly in front
// walks its runs and stores; a straddling one loads a byte per index and
// breaks strips around what it finds. If both cost the same per vertex the
// time is in the store to the TA, and if they do not it is in the walk.
static uint64_t s_phEmitPlain = 0, s_phEmitVis = 0;
static uint64_t s_lastEmitPlain = 0, s_lastEmitVis = 0;
static unsigned s_emitVertsPlain = 0, s_emitVertsVis = 0;
static unsigned s_lastEmitVertsPlain = 0, s_lastEmitVertsVis = 0;
extern "C" unsigned pvrEmitPlainUs( void )    { return (unsigned)s_lastEmitPlain; }
extern "C" unsigned pvrEmitVisUs( void )      { return (unsigned)s_lastEmitVis; }
extern "C" unsigned pvrEmitPlainVerts( void ) { return s_lastEmitVertsPlain; }
extern "C" unsigned pvrEmitVisVerts( void )   { return s_lastEmitVertsVis; }
// Transform, split the same way: the straddling variant carries a per-vertex
// visibility test and store that the in-front one compiles out.
static uint64_t s_phXformFront = 0, s_phXformStraddle = 0;
static uint64_t s_lastXformFront = 0, s_lastXformStraddle = 0;
static unsigned s_xformVertsFront = 0, s_xformVertsStraddle = 0;
static unsigned s_lastXformVertsFront = 0, s_lastXformVertsStraddle = 0;
extern "C" unsigned pvrXformFrontUs( void )       { return (unsigned)s_lastXformFront; }
extern "C" unsigned pvrXformStraddleUs( void )    { return (unsigned)s_lastXformStraddle; }
extern "C" unsigned pvrXformFrontVerts( void )    { return s_lastXformVertsFront; }
extern "C" unsigned pvrXformStraddleVerts( void ) { return s_lastXformVertsStraddle; }
#define PH_BEGIN()  uint64_t phMark_ = timer_us_gettime64()
#define PH_MARK(b)  do { const uint64_t n_ = timer_us_gettime64(); \
                         (b) += n_ - phMark_; phMark_ = n_; } while (0)
// One timer read charged to two accumulators: the existing total and the
// split beside it. Reading it twice would put the split's own cost in the
// total.
#define PH_MARK2(a,b) do { const uint64_t n_ = timer_us_gettime64(); \
                           const uint64_t d_ = n_ - phMark_;         \
                           (a) += d_; (b) += d_; phMark_ = n_; } while (0)
#else
#define PH_BEGIN()  do {} while (0)
#define PH_MARK(b)  do {} while (0)
#define PH_MARK2(a,b) do {} while (0)
#endif
static unsigned s_lastVtxBytes = 0;

extern "C" unsigned pvrLastDrawCount( void )   { return s_lastDraws; }
extern "C" unsigned pvrLastVertexCount( void ) { return s_lastVtxBytes / 32u; }

void pvrContext::BeginFrame()
{
    if (fogDirty)
        ApplyFog();

    pddiBaseContext::BeginFrame();
#if defined SRR2_DC_PVR_TRACE
    const uint64_t waitStart = timer_us_gettime64();
#endif
    pvr_wait_ready();
#if defined SRR2_DC_PVR_TRACE
    s_waitUs = timer_us_gettime64() - waitStart;
    s_vtxXform = 0;
    s_vtxEmit = 0;
    s_vtxEmitIM = 0;
    s_rollDraws = 0;
    s_replayImUs = 0;
    s_tris = 0;
#endif
    pvr_scene_begin();

    currentList = (pvr_list_t)-1;
    begunMask = 0;

    // Textures are freed between frames during zone loads, so a cached header
    // must not outlive the frame that built it.
    pvrInvalidateHeaderCache();

    pvrResetDeferredLists();
#ifdef RAD_DC_TRACE_VERTS
    s_traceFrame++;
    if ((s_traceFrame % 100) == 0)
        s_traceBudget = 2;
#endif
}

void pvrContext::FlushDeferredLists()
{
    pvrRunDeferredLists();
    currentList = (pvr_list_t)-1;
}

void pvrContext::EndFrame()
{
#if defined SRR2_DC_PROFILER
    const uint64_t flushStart_ = timer_us_gettime64();
#endif
    pddiBaseContext::EndFrame();
    FlushDeferredLists();
#if defined SRR2_DC_PROFILER
    // Everything above is this frame's submission. pvr_scene_finish below
    // blocks until the hardware has taken it, so the two apart say whether the
    // frame is ours to shorten or the PVR's.
    const uint64_t finishStart_ = timer_us_gettime64();
    s_lastFlushUs = (unsigned)(finishStart_ - flushStart_);
#endif
#if defined SRR2_DC_PVR_TRACE
    const uint64_t finishStart = timer_us_gettime64();
#endif
    pvr_scene_finish();
#if defined SRR2_DC_PROFILER
    s_lastFinishUs = (unsigned)(timer_us_gettime64() - finishStart_);
#endif
#if defined SRR2_DC_PVR_TRACE
    {
        const uint64_t now = timer_us_gettime64();
        s_finishUs = now - finishStart;

        printf("[perf] %u ms | replay %u (ov %u) | draws %u (fus %u roll %u cull %u)"
               " | xform %u emit %u (ov %u) tris %u\n",
               (unsigned)((now - s_frameUs) / 1000u),
               (unsigned)s_replayUs,
               (unsigned)s_replayImUs,
               s_lastDraws,
               s_fusedDraws,
               s_rollDraws,
               s_boxCulled,
               s_vtxXform,
               s_vtxEmit,
               s_vtxEmitIM,
               s_tris);

        s_frameUs = now;
    }
#endif
}

void pvrContext::Clear(unsigned bufferMask)
{
    pddiBaseContext::Clear(bufferMask);
    if (bufferMask & PDDI_BUFFER_COLOUR)
    {
        const float r = float(state.viewState->clearColour.Red()) / 255.0f;
        const float g = float(state.viewState->clearColour.Green()) / 255.0f;
        const float b = float(state.viewState->clearColour.Blue()) / 255.0f;
        pvr_set_bg_color(r, g, b);
    }
}

static inline unsigned ListBit(pvr_list_t list)
{
    switch (list)
    {
        case PVR_LIST_OP_POLY: return 1u;
        case PVR_LIST_PT_POLY: return 2u;
        case PVR_LIST_TR_POLY: return 4u;
        default: return 0u;
    }
}

static inline pvr_filter_mode_t MapFilter(pddiFilterMode m)
{
    switch (m)
    {
        case PDDI_FILTER_NONE: return PVR_FILTER_NEAREST;
        default: return PVR_FILTER_BILINEAR;
    }
}

static inline pvr_cull_mode_t MapCull(pddiCullMode m)
{
    switch (m)
    {
        default:
        case PDDI_CULL_NONE: return PVR_CULLING_NONE;
        case PDDI_CULL_NORMAL: return PVR_CULLING_CCW;
        case PDDI_CULL_INVERTED: return PVR_CULLING_CW;
        case PDDI_CULL_SHADOW_BACKFACE: return PVR_CULLING_CCW;
        case PDDI_CULL_SHADOW_FRONTFACE: return PVR_CULLING_CW;
    }
}

static inline float GetUStrideScale(pvrTexture* tex)
{
    if (!tex)
        return 1.0f;
    const int w = tex->GetWidth();
    const int s = tex->GetStridePixels();
    if (s > 0 && s != w)
        return (float)w / (float)s;
    return 1.0f;
}

static inline pvr_depthcmp_mode_t MapDepthCompareInvW(pddiCompareMode m)
{
    // We submit Z as invW (i.e. 1/clip.w). This flips depth ordering compared
    // to regular "smaller Z = closer" conventions.
    switch (m)
    {
        case PDDI_COMPARE_NONE: return PVR_DEPTHCMP_ALWAYS;
        case PDDI_COMPARE_ALWAYS: return PVR_DEPTHCMP_ALWAYS;
        case PDDI_COMPARE_LESS: return PVR_DEPTHCMP_GREATER;
        case PDDI_COMPARE_LESSEQUAL: return PVR_DEPTHCMP_GEQUAL;
        case PDDI_COMPARE_GREATER: return PVR_DEPTHCMP_LESS;
        case PDDI_COMPARE_GREATEREQUAL: return PVR_DEPTHCMP_LEQUAL;
        case PDDI_COMPARE_EQUAL: return PVR_DEPTHCMP_EQUAL;
        case PDDI_COMPARE_NOTEQUAL: return PVR_DEPTHCMP_NOTEQUAL;
        default: return PVR_DEPTHCMP_GEQUAL;
    }
}

static inline void CopyRmtToShzWithFlip(shz_mat4x4_t* out, const rmt::Matrix& in)
{
    for (int col = 0; col < 4; col++)
    {
        out->elem2D[col][0] =  in.m[col][0];
        out->elem2D[col][1] =  in.m[col][1];
        out->elem2D[col][2] = -in.m[col][2];
        out->elem2D[col][3] =  in.m[col][3];
    }
}

void pvrContext::LoadHardwareMatrix(pddiMatrixType id)
{
    if (id != PDDI_MATRIX_MODELVIEW)
        return;

    // Copy to sh4zam matrix and apply GL->PVR handedness flip on Z column.
    CopyRmtToShzWithFlip(&modelViewM, *state.matrixStack[id]->Top());

    // Keep combined view-projection up to date.
    shz_mat4x4_mult(&viewProjM, &projectionM, &modelViewM);
}

// fov_rad = vertical FOV in radians, aspect = width/height, near_z = near plane.
static inline void BuildPerspective(shz_mat4x4_t& out, float fov_rad, float aspect, float near_z)
{
    shz_xmtrx_init_identity();
    shz_xmtrx_apply_perspective(fov_rad, aspect, near_z);
    shz_xmtrx_store_4x4(&out);
}

// Build orthographic projection (scale then translation).
static inline void BuildOrtho(shz_mat4x4_t& out, float l, float r, float b, float t, float n, float f)
{
    const float sx = 2.0f / (r - l);
    const float sy = 2.0f / (t - b);
    const float sz = -2.0f / (f - n);
    const float tx = -(r + l) / (r - l);
    const float ty = -(t + b) / (t - b);
    const float tz = -(f + n) / (f - n);

    shz_xmtrx_init_identity();
    shz_xmtrx_apply_scale(sx, sy, sz);
    shz_xmtrx_apply_translation(tx, ty, tz);
    shz_xmtrx_store_4x4(&out);
}

void pvrContext::SetupHardwareProjection(void)
{
    displayW = display->GetWidth();
    displayH = display->GetHeight();

    switch (state.viewState->projectionMode)
    {
        case PDDI_PROJECTION_DEVICE:
            BuildOrtho(projectionM,
                       0.0f, float(displayW),
                       float(displayH), 0.0f,
                       state.viewState->camera.nearPlane, state.viewState->camera.farPlane);
            viewportX = 0; viewportY = 0; viewportW = displayW; viewportH = displayH;
            break;

        case PDDI_PROJECTION_ORTHOGRAPHIC:
            BuildOrtho(projectionM,
                       -0.5f, 0.5f,
                       -((1.0f / state.viewState->camera.aspect) / 2.0f),
                       +((1.0f / state.viewState->camera.aspect) / 2.0f),
                       state.viewState->camera.nearPlane, state.viewState->camera.farPlane);
            viewportX = int(state.viewState->viewWindow.left * displayW);
            viewportY = int((1.0f - state.viewState->viewWindow.bottom) * displayH);
            viewportW = int((state.viewState->viewWindow.right - state.viewState->viewWindow.left) * displayW);
            viewportH = int((state.viewState->viewWindow.bottom - state.viewState->viewWindow.top) * displayH);
            break;

        case PDDI_PROJECTION_PERSPECTIVE:
        default:
        {
            // fov in radians, aspect w/h, near.
            const float fov_rad = state.viewState->camera.fov;
            const float aspect = state.viewState->camera.aspect;
            const float nearPlane = state.viewState->camera.nearPlane;
            const float fov_vertical = 2.0f * atanf(tanf(fov_rad * 0.5f) / aspect);
            BuildPerspective(projectionM, fov_vertical, aspect, nearPlane);
            viewportX = int(state.viewState->viewWindow.left * displayW);
            viewportY = int((1.0f - state.viewState->viewWindow.bottom) * displayH);
            viewportW = int((state.viewState->viewWindow.right - state.viewState->viewWindow.left) * displayW);
            viewportH = int((state.viewState->viewWindow.bottom - state.viewState->viewWindow.top) * displayH);
        }
        break;
    }

    // Keep combined view-projection up to date.
    shz_mat4x4_mult(&viewProjM, &projectionM, &modelViewM);
}

struct pvrClipVert
{
    shz_vec4_t pos;
    float u, v;
    uint32_t argb;
};

static const float PVR_NEAR_CLIP_EPSILON = 1e-4f;

static inline bool ClipVertVisible(const pvrClipVert& v)
{
    return v.pos.w >= v.pos.z + PVR_NEAR_CLIP_EPSILON;
}

static inline float LerpF(float a, float b, float t) { return a + (b - a) * t; }

static inline uint32_t LerpARGB(uint32_t c1, uint32_t c2, int ti)
{
    const uint32_t rb = ((((c2 & 0x00FF00FFu) - (c1 & 0x00FF00FFu)) * ti) >> 8) + (c1 & 0x00FF00FFu);
    const uint32_t g  = ((((c2 & 0x0000FF00u) - (c1 & 0x0000FF00u)) * ti) >> 8) + (c1 & 0x0000FF00u);
    const uint32_t a  = ((((c2 >> 24) - (c1 >> 24)) * ti) >> 8) + (c1 >> 24);
    return (a << 24) | (rb & 0x00FF00FFu) | (g & 0x0000FF00u);
}

static inline void ClipEdge(const pvrClipVert& v1, const pvrClipVert& v2, pvrClipVert& out)
{
    const float d0 = v1.pos.w - v1.pos.z - PVR_NEAR_CLIP_EPSILON;
    const float d1 = v2.pos.w - v2.pos.z - PVR_NEAR_CLIP_EPSILON;
    const float denom = d0 - d1;

    float t = (fabsf(denom) < 1e-8f) ? 0.0f : (d0 / denom);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    out.pos.x = LerpF(v1.pos.x, v2.pos.x, t);
    out.pos.y = LerpF(v1.pos.y, v2.pos.y, t);
    out.pos.z = LerpF(v1.pos.z, v2.pos.z, t);
    out.pos.w = LerpF(v1.pos.w, v2.pos.w, t);
    out.u = LerpF(v1.u, v2.u, t);
    out.v = LerpF(v1.v, v2.v, t);

    int ti = (int)(t * 255.0f);
    if (ti < 0) ti = 0;
    if (ti > 255) ti = 255;
    out.argb = LerpARGB(v1.argb, v2.argb, ti);
}

inline bool TransformToScreen(const pvrContext* ctx, float x, float y, float z,
                              float& sx, float& sy, float& sz)
{
    shz_vec4_t v = shz_vec4_init(x, y, z, 1.0f);
    shz_vec4_t out = shz_xmtrx_transform_vec4(v);
    if (out.w == 0.0f)
        return false;

    const float invW = 1.0f / out.w;
    const float ndcX = out.x * invW;
    const float ndcY = out.y * invW;

    // GL viewport mapping (origin bottom-left), then convert to PVR (origin top-left).
    const float winX = float(ctx->viewportX) + (ndcX + 1.0f) * 0.5f * float(ctx->viewportW);
    const float winY = float(ctx->viewportY) + (ndcY + 1.0f) * 0.5f * float(ctx->viewportH);

    sx = winX;
    sy = float(ctx->displayH) - winY;
    sz = invW;
    return true;
}

struct pvrViewportMap
{
    float ox, oy;   // viewport origin in pixels
    float hw, hh;   // half extents, for the NDC -> pixel scale
};

static inline void SubmitClipVert(const pvrViewportMap& vp, const pvrClipVert& cv, unsigned flags)
{
    (void)vp;
#if defined SRR2_DC_PVR_TRACE
    s_vtxEmit++;
#endif
#if defined SRR2_DC_PROFILER
    s_vtxEmitted++;
#endif
    const float invw = 1.0f / cv.pos.w;
    pvr_vertex_t* vert = (pvr_vertex_t*)pvr_dr_target();

    vert->flags = flags;
    vert->x = cv.pos.x * invw;
    vert->y = cv.pos.y * invw;
    vert->z = invw;
    vert->u = cv.u;
    vert->v = cv.v;
    vert->argb = cv.argb;
    vert->oargb = 0;
    pvr_dr_commit(vert);
}

static void ClipAndSubmitTriangle(const pvrViewportMap& vp, pvrClipVert* verts)
{
    unsigned mask = 0;
    if (ClipVertVisible(verts[0])) mask |= 1;
    if (ClipVertVisible(verts[1])) mask |= 2;
    if (ClipVertVisible(verts[2])) mask |= 4;

    if (mask == 0)
        return;

    if (mask == 7)
    {
        SubmitClipVert(vp, verts[0], PVR_CMD_VERTEX);
        SubmitClipVert(vp, verts[1], PVR_CMD_VERTEX);
        SubmitClipVert(vp, verts[2], PVR_CMD_VERTEX_EOL);
        return;
    }

    pvrClipVert v[4];
    v[0] = verts[0]; v[1] = verts[1]; v[2] = verts[2];
    unsigned count = 3;

    switch (mask)
    {
        case 1:
            ClipEdge(v[0], v[1], v[1]);
            ClipEdge(v[0], v[2], v[2]);
            break;
        case 2:
            ClipEdge(v[1], v[0], v[0]);
            ClipEdge(v[1], v[2], v[2]);
            break;
        case 3:
            count = 4;
            ClipEdge(v[1], v[2], v[3]);
            ClipEdge(v[0], v[2], v[2]);
            break;
        case 4:
            ClipEdge(v[2], v[0], v[0]);
            ClipEdge(v[2], v[1], v[1]);
            break;
        case 5:
            count = 4;
            ClipEdge(v[1], v[2], v[3]);
            ClipEdge(v[0], v[1], v[1]);
            break;
        case 6:
            count = 4;
            v[3] = v[2];
            ClipEdge(v[0], v[2], v[2]);
            ClipEdge(v[0], v[1], v[0]);
            break;
        default:
            return;
    }

    for (unsigned i = 0; i < count; i++)
        SubmitClipVert(vp, v[i], (i == count - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
}

static void pvrFoldViewport(shz_mat4x4_t& m, const pvrViewportMap& vp)
{
    const float ax = vp.ox + vp.hw;
    const float ay = vp.oy + vp.hh;

    for (int c = 0; c < 4; c++)
    {
        const float r0 = m.elem2D[c][0];
        const float r1 = m.elem2D[c][1];
        const float r3 = m.elem2D[c][3];

        m.elem2D[c][0] =  vp.hw * r0 + ax * r3;
        m.elem2D[c][1] = -vp.hh * r1 + ay * r3;
    }
}

pvrViewportMap pvrContext::GetViewportMap() const
{
    pvrViewportMap vp;
    vp.ox = float(viewportX);
    vp.oy = float(viewportY);
    vp.hw = float(viewportW) * 0.5f;
    vp.hh = float(viewportH) * 0.5f;
    return vp;
}

struct pvrDrawCmd
{
    pvr_poly_hdr_t  hdr;
    shz_mat4x4_t    xform;
    pvrViewportMap  vp;
    pvrPrimBuffer*  buffer;
    unsigned        immFirst;
    unsigned        immCount;
    pddiPrimType    immPrim;
    unsigned        argb;
    float           uScale;
    pvr_cull_mode_t cull;
    // Sort key only. Collisions cost a redundant header, never a wrong one:
    // the skip below compares the header words themselves.
    unsigned        hdrKey;
    unsigned        lightSet;
    unsigned char   clipOn;
    unsigned char   clipX0, clipY0, clipX1, clipY1;
};

static inline unsigned pvrHdrSortKey(const pvr_poly_hdr_t& h)
{
    return h.cmd ^ (h.mode1 * 3u) ^ (h.mode2 * 5u) ^ (h.mode3 * 7u);
}

// State carried across the draws of one list, reset when a list opens.
static pvr_poly_hdr_t s_lastHdr;
static bool           s_haveLastHdr = false;
static shz_mat4x4_t   s_loadedXform;
static bool           s_haveLastXform = false;

static unsigned char  s_clipRect[4] = { 0, 0, 0, 0 };
static bool           s_lastClipOn = false;
static bool           s_haveLastClip = false;

static inline void pvrResetSubmitState( void )
{
    s_haveLastHdr = false;
    s_haveLastXform = false;
    s_haveLastClip = false;
}

// The clip rectangle is list state, not per-poly state, so it has to reach the
// TA ahead of the polys that reference it. Coordinates are in 32-pixel tiles
// and the far edge is inclusive.
static inline void pvrSubmitClip(const pvrDrawCmd& cmd)
{
    if (s_haveLastClip && s_lastClipOn == (cmd.clipOn != 0)
        && (!cmd.clipOn
            || (s_clipRect[0] == cmd.clipX0 && s_clipRect[1] == cmd.clipY0
                && s_clipRect[2] == cmd.clipX1 && s_clipRect[3] == cmd.clipY1)))
        return;

    unsigned* dst = (unsigned*)pvr_dr_target();
    dst[0] = PVR_CMD_USERCLIP;
    dst[1] = 0;
    dst[2] = 0;
    dst[3] = 0;
    dst[4] = cmd.clipOn ? cmd.clipX0 : 0u;
    dst[5] = cmd.clipOn ? cmd.clipY0 : 0u;
    dst[6] = cmd.clipOn ? cmd.clipX1 : 19u;
    dst[7] = cmd.clipOn ? cmd.clipY1 : 14u;
    pvr_dr_commit(dst);

    s_clipRect[0] = cmd.clipX0; s_clipRect[1] = cmd.clipY0;
    s_clipRect[2] = cmd.clipX1; s_clipRect[3] = cmd.clipY1;
    s_lastClipOn = (cmd.clipOn != 0);
    s_haveLastClip = true;
}

static inline void pvrFillClip(pvrDrawCmd& cmd, const pvrContext* ctx)
{
    if (!ctx->scissorOn)
    {
        cmd.clipOn = 0;
        cmd.clipX0 = cmd.clipY0 = cmd.clipX1 = cmd.clipY1 = 0;
        return;
    }

    const pddiRect& r = ctx->scissorRect;
    cmd.clipOn = 1;
    cmd.clipX0 = (unsigned char)(r.left / 32);
    cmd.clipY0 = (unsigned char)(r.top / 32);
    cmd.clipX1 = (unsigned char)((r.right  > 0 ? r.right  - 1 : 0) / 32);
    cmd.clipY1 = (unsigned char)((r.bottom > 0 ? r.bottom - 1 : 0) / 32);
}

static inline void pvrInvalidateXform( void )
{
    s_haveLastXform = false;
}

static inline void pvrSubmitHeader(const pvr_poly_hdr_t& h)
{
    if (s_haveLastHdr
        && s_lastHdr.cmd == h.cmd && s_lastHdr.mode1 == h.mode1
        && s_lastHdr.mode2 == h.mode2 && s_lastHdr.mode3 == h.mode3)
    {
#if defined SRR2_DC_PROFILER
        s_hdrSkipped++;
#endif
        return;
    }

    pvr_poly_hdr_t* dst = (pvr_poly_hdr_t*)pvr_dr_target();
    *dst = h;
    pvr_dr_commit(dst);

    s_lastHdr = h;
    s_haveLastHdr = true;
#if defined SRR2_DC_PROFILER
    s_hdrSubmitted++;
#endif
}

// No skip on an unchanged matrix. radmath drives XMTRX for every matrix
// multiply in the engine, so between two draws the register file has almost
// certainly been overwritten by whatever the display callback did. Only the
// eight fmov.d pairs prove what is in there.
static inline void pvrLoadXform(const shz_mat4x4_t& m)
{
    shz_xmtrx_load_4x4(&m);
    s_loadedXform = m;
    s_haveLastXform = true;
#if defined SRR2_DC_PROFILER
    s_xformLoaded++;
#endif
}

static_assert(alignof(pvrDrawCmd) >= alignof(shz_mat4x4_t),
              "pvrDrawCmd must meet shz_mat4x4_t alignment for fmov.d");
static_assert((sizeof(pvrDrawCmd) % alignof(shz_mat4x4_t)) == 0,
              "pvrDrawCmd stride must keep every xform aligned");

// The cache holds finished TA packets. Emitting a vertex is then a 32-byte
// copy plus the strip flag, with no per-emit uv dequant or colour assembly --
// each unique vertex pays that once, however many indices reference it.
struct pvrCacheVert
{
    float      sx, sy, sz;
    float      u, v;
    unsigned   argb;
    unsigned   vis;
};

static pvr_vertex_t* s_vtxPkt = NULL;
static unsigned char* s_vtxVis = NULL;

static const float kBackfaceSign = 1.0f;
static unsigned      s_vtxCacheMax = 0;

enum pvrBoxClass { kBoxCulled, kBoxClipped, kBoxInFront };

// Classify the quantised AABB (corners at +/-32767) against the near plane and
// the screen edges, using the caller's already-loaded transform. The half-space
// tests are affine in model space, so a box corner attains each extremum.
//
// Reject draws whose projected box covers less than this many pixels. Culling
// and the submit path are both as tight as they go, so the only lever left on
// frame time is submitting fewer vertices -- this trades distant detail for it
// directly. Raise for speed, lower for fidelity.
//
#ifndef SRR_DC_MIN_DRAW_AREA
#define SRR_DC_MIN_DRAW_AREA 6.0f
#endif
static float s_minScreenArea = SRR_DC_MIN_DRAW_AREA;
// Compared against the nearest corner of the box, so anything straddling the
// boundary is kept whole rather than half-drawn.
static float s_maxDrawDepth = SRR_DC_DEPTH_CULL;

static pvrBoxClass pvrClassifyBox(const pvrViewportMap& vp, const float* lo,
                                  const float* hi, float* areaOut, float* nearOut = NULL)
{
    float nearW = 1e30f;
    const float x0 = vp.ox;
    const float x1 = vp.ox + vp.hw * 2.0f;
    const float y0 = vp.oy;
    const float y1 = vp.oy + vp.hh * 2.0f;

    unsigned behind = 0, left = 0, right = 0, above = 0, below = 0;

    float minX =  1e30f, maxX = -1e30f;
    float minY =  1e30f, maxY = -1e30f;

    for (int c = 0; c < 8; c++)
    {
        const shz_vec4_t p = shz_xmtrx_transform_vec4(
            shz_vec4_init((c & 1) ? hi[0] : lo[0],
                          (c & 2) ? hi[1] : lo[1],
                          (c & 4) ? hi[2] : lo[2], 1.0f));

        if (p.w < p.z + PVR_NEAR_CLIP_EPSILON)
        {
            behind++;
            continue;
        }

        if (p.w < nearW) nearW = p.w;

        if (p.x < x0 * p.w) left++;
        if (p.x > x1 * p.w) right++;
        if (p.y < y0 * p.w) above++;
        if (p.y > y1 * p.w) below++;

        const float iw = shz_invf_fsrra(p.w);
        const float sx = p.x * iw;
        const float sy = p.y * iw;
        if (sx < minX) minX = sx;
        if (sx > maxX) maxX = sx;
        if (sy < minY) minY = sy;
        if (sy > maxY) maxY = sy;
    }

    *areaOut = (behind == 0) ? ((maxX - minX) * (maxY - minY)) : 1e30f;

    // View depth of the nearest corner still in front. What a draw distance
    // would actually be compared against.
    if (nearOut)
        *nearOut = (nearW < 1e29f) ? nearW : 0.0f;

    if (behind == 8)
        return kBoxCulled;

    if (behind == 0 && (left == 8 || right == 8 || above == 8 || below == 8))
        return kBoxCulled;

    return behind ? kBoxClipped : kBoxInFront;
}

static bool pvrReserveVtxCache(unsigned count)
{
    if (count <= s_vtxCacheMax)
        return true;

    unsigned want = s_vtxCacheMax ? s_vtxCacheMax : 256;
    while (want < count)
        want *= 2;

    // 32-byte aligned so each packet owns a cache line and the copy into the
    // store queue is four fmov.d pairs rather than a straddle.
    pvr_vertex_t* pkt = (pvr_vertex_t*)memalign(32, want * sizeof(pvr_vertex_t));
    if (!pkt)
        return false;

    unsigned char* vis = (unsigned char*)realloc(s_vtxVis, want);
    if (!vis)
    {
        free(pkt);
        return false;
    }

    // Only publish the new capacity once every buffer behind it has grown --
    // a partial success here would let the fill loop run off the end.
    free(s_vtxPkt);
    s_vtxPkt = pkt;
    s_vtxVis = vis;
    s_vtxCacheMax = want;
    return true;
}

static __attribute__((always_inline)) inline void SubmitPacket(const pvr_vertex_t& src, unsigned flags)
{
#if defined SRR2_DC_PVR_TRACE
    s_vtxEmit++;
#endif
#if defined SRR2_DC_PROFILER
    s_vtxEmitted++;
#endif
    pvr_vertex_t* v = (pvr_vertex_t*)pvr_dr_target();
    *v = src;
    v->flags = flags;
    pvr_dr_commit(v);
}

// Writing the flag word re-dirties the line, so a vertex referenced by
// several indices is written back once per reference, as the TA needs.
static __attribute__((always_inline)) inline void SubmitOix(pvr_vertex_t* v, unsigned flags)
{
#if defined SRR2_DC_PVR_TRACE
    s_vtxEmit++;
#endif
#if defined SRR2_DC_PROFILER
    s_vtxEmitted++;
#endif
    v->flags = flags;
    __asm__ __volatile__("ocbwb @%0" : : "r" (v) : "memory");
}

static __attribute__((always_inline)) inline void SubmitVert(bool oix, pvr_vertex_t* v, unsigned flags)
{
    if (oix)
        SubmitOix(v, flags);
    else
        SubmitPacket(*v, flags);
}

static __attribute__((always_inline)) inline void SubmitScreenVert(const pvrCacheVert& cv, unsigned flags)
{
#if defined SRR2_DC_PVR_TRACE
    s_vtxEmit++;
#endif
#if defined SRR2_DC_PROFILER
    s_vtxEmitted++;
#endif
    pvr_vertex_t* vert = (pvr_vertex_t*)pvr_dr_target();

    vert->flags = flags;
    vert->x = cv.sx;
    vert->y = cv.sy;
    vert->z = cv.sz;
    vert->u = cv.u;
    vert->v = cv.v;
    vert->argb = cv.argb;
    vert->oargb = 0;
    pvr_dr_commit(vert);
}

struct pvrImmVert
{
    float    x, y, z;
    float    u, v;
    unsigned argb;
};

static std::vector<pvrDrawCmd>  s_drawCmds[3];
// Most draws share a light set, so they are pooled and referenced by index
// rather than copied into every command.
static std::vector<pvrLightSet> s_lightSets;
static std::vector<pvrImmVert>  s_immVerts;


static inline int ListSlot(pvr_list_t list)
{
    switch (list)
    {
        case PVR_LIST_OP_POLY: return 0;
        case PVR_LIST_PT_POLY: return 1;
        case PVR_LIST_TR_POLY: return 2;
        default: return -1;
    }
}

static const pvr_list_t kListOrder[3] =
{
    PVR_LIST_OP_POLY, PVR_LIST_PT_POLY, PVR_LIST_TR_POLY
};

static const pvr_list_t kSubmitList = PVR_LIST_TR_POLY;

static inline pvr_list_t ChooseList(const pvrTextureEnv& env)
{
    if (env.alphaTest)
        return PVR_LIST_PT_POLY;
    if (env.alphaBlendMode != PDDI_BLEND_NONE)
        return PVR_LIST_TR_POLY;
    return PVR_LIST_OP_POLY;
}

static inline void MapBlend(const pvrTextureEnv& env, pvr_blend_mode_t& src, pvr_blend_mode_t& dst, bool& enable)
{
    enable = false;
    src = PVR_BLEND_ONE;
    dst = PVR_BLEND_ZERO;

    switch (env.alphaBlendMode)
    {
        default:
        case PDDI_BLEND_NONE:
            enable = false;
            src = PVR_BLEND_ONE;
            dst = PVR_BLEND_ZERO;
            break;
        case PDDI_BLEND_ALPHA:
            enable = true;
            src = PVR_BLEND_SRCALPHA;
            dst = PVR_BLEND_INVSRCALPHA;
            break;
        case PDDI_BLEND_ADD:
            enable = true;
            src = PVR_BLEND_ONE;
            dst = PVR_BLEND_ONE;
            break;
        case PDDI_BLEND_MODULATE:
            enable = true;
            src = PVR_BLEND_DESTCOLOR;
            dst = PVR_BLEND_ZERO;
            break;
        case PDDI_BLEND_SUBTRACT:
            // Approximation: subtract isn't directly supported; treat as modulate for now.
            enable = true;
            src = PVR_BLEND_DESTCOLOR;
            dst = PVR_BLEND_ZERO;
            break;
    }
}

void pvrContext::SetScissor(pddiRect* rect)
{
    if (!rect)
    {
        scissorOn = false;
        return;
    }

    pddiBaseContext::SetScissor(rect);

    const int w = display ? display->GetWidth()  : 640;
    const int hgt = display ? display->GetHeight() : 480;

    int x0 = rect->left  < rect->right  ? rect->left  : rect->right;
    int x1 = rect->left  < rect->right  ? rect->right : rect->left;
    int y0 = rect->top   < rect->bottom ? rect->top   : rect->bottom;
    int y1 = rect->top   < rect->bottom ? rect->bottom: rect->top;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w)   x1 = w;
    if (y1 > hgt) y1 = hgt;

    scissorRect.Set(x0, y0, x1, y1);
    scissorOn = (x0 > 0) || (y0 > 0) || (x1 < w) || (y1 < hgt);
}

// Compiling a header costs a context fill plus pvr_poly_compile, and a mesh's
// meshlets all share one shader -- so consecutive draws ask for the same header
// over and over. Memoise on everything the header is derived from.
struct pvrHdrKey
{
    const void*     texture;
    const void*     vram;
    unsigned        fmt;
    unsigned short  w, h;
    unsigned char   mipmap;
    unsigned char   filter;
    unsigned char   uvMode;
    unsigned char   cull;
    unsigned char   zCmp;
    unsigned char   zWrite;
    unsigned char   zEnabled;
    unsigned char   blendSrc;
    unsigned char   blendDst;
    unsigned char   list;
    unsigned char   enabled;
    unsigned char   clip;
    unsigned char   fog;
};

enum { kHdrCacheSize = 8 };
static pvrHdrKey       s_hdrKey[kHdrCacheSize];
static pvr_poly_hdr_t  s_hdrVal[kHdrCacheSize];
static pvr_list_t      s_hdrList[kHdrCacheSize];
static bool            s_hdrUsed[kHdrCacheSize] = { false };
static unsigned        s_hdrNext = 0;

static void pvrInvalidateHeaderCache()
{
    for (int i = 0; i < kHdrCacheSize; i++)
        s_hdrUsed[i] = false;
    s_hdrNext = 0;
}

void pvrContext::BuildPolyHeader(const pvrTextureEnv& env, pvr_poly_hdr_t& outHdr, pvr_list_t& outList) const
{
    const pvr_list_t logical = ChooseList(env);
    outList = logical;

    const bool textured = env.enabled && env.texture && env.texture->GetVramPtr();

    bool keyBlend = false;
    pvr_blend_mode_t keySrc = PVR_BLEND_ONE, keyDst = PVR_BLEND_ZERO;
    MapBlend(env, keySrc, keyDst, keyBlend);
    if (!(logical == PVR_LIST_TR_POLY && keyBlend))
    {
        keySrc = PVR_BLEND_ONE;
        keyDst = PVR_BLEND_ZERO;
    }

    pvrHdrKey key;
    memset(&key, 0, sizeof(key));
    key.texture  = textured ? (const void*)env.texture : NULL;
    key.vram     = textured ? (const void*)env.texture->GetVramPtr() : NULL;
    key.fmt      = textured ? env.texture->GetPvrTxrFormat() : 0u;
    key.w        = textured ? (unsigned short)env.texture->GetWidth() : 0;
    key.h        = textured ? (unsigned short)env.texture->GetHeight() : 0;
    key.mipmap   = textured ? (unsigned char)env.texture->HasMipMaps() : 0;
    key.filter   = (unsigned char)MapFilter(env.filterMode);
    key.uvMode   = (unsigned char)env.uvMode;
    key.cull     = (unsigned char)MapCull(state.renderState->cullMode);
    key.zCmp     = (unsigned char)MapDepthCompareInvW(state.renderState->zCompare);
    key.zWrite   = (unsigned char)(state.renderState->zWrite ? 1 : 0);
    key.zEnabled = (unsigned char)(state.renderState->zEnabled ? 1 : 0);
    key.blendSrc = (unsigned char)keySrc;
    key.blendDst = (unsigned char)keyDst;
    key.list     = (unsigned char)logical;
    key.enabled  = (unsigned char)(textured ? 1 : 0);
    key.clip     = (unsigned char)(scissorOn ? 1 : 0);
    key.fog      = (unsigned char)(fogOn ? 1 : 0);

    for (int i = 0; i < kHdrCacheSize; i++)
    {
        if (s_hdrUsed[i] && memcmp(&s_hdrKey[i], &key, sizeof(key)) == 0)
        {
            outHdr = s_hdrVal[i];
            outList = s_hdrList[i];
            return;
        }
    }

    pvr_poly_cxt_t cxt;
    if (textured)
    {
        const pvr_uv_clamp_t clamp = (env.uvMode == PDDI_UV_CLAMP) ? PVR_UVCLAMP_UV : PVR_UVCLAMP_NONE;
        pvr_poly_cxt_txr(&cxt, outList,
                         env.texture->GetPvrTxrFormat(),
                         env.texture->GetWidth(), env.texture->GetHeight(),
                         env.texture->GetVramPtr(),
                         MapFilter(env.filterMode));
        cxt.txr.uv_clamp = clamp;
        cxt.txr.uv_flip = PVR_UVFLIP_NONE;
        cxt.txr.mipmap = env.texture->HasMipMaps();
        cxt.txr.env = (logical == PVR_LIST_TR_POLY) ? PVR_TXRENV_MODULATEALPHA : PVR_TXRENV_MODULATE;
        cxt.txr.alpha = false;
    }
    else
    {
        pvr_poly_cxt_col(&cxt, outList);
    }

    cxt.gen.culling = MapCull(state.renderState->cullMode);
    cxt.gen.clip_mode = scissorOn ? PVR_USERCLIP_INSIDE : PVR_USERCLIP_DISABLE;
    // Table fog: the hardware blends per pixel from the vertex 1/w against a
    // 128-entry table, so the cost of hiding the draw distance is zero on our
    // side. Vertex fog would need a per-vertex offset colour, and KOS asserts
    // in pvr_fog_vertex_color anyway.
    cxt.gen.fog_type = fogOn ? PVR_FOG_TABLE : PVR_FOG_DISABLE;

    if (state.renderState->zEnabled)
    {
        cxt.depth.comparison = MapDepthCompareInvW(state.renderState->zCompare);
        cxt.depth.write = (logical != PVR_LIST_TR_POLY && state.renderState->zWrite)
                              ? PVR_DEPTHWRITE_ENABLE
                              : PVR_DEPTHWRITE_DISABLE;
    }
    else
    {
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    }

    bool blendEnable = false;
    pvr_blend_mode_t src = PVR_BLEND_ONE, dst = PVR_BLEND_ZERO;
    MapBlend(env, src, dst, blendEnable);
    if (logical == PVR_LIST_TR_POLY && blendEnable)
    {
        cxt.blend.src = src;
        cxt.blend.dst = dst;
    }
    else
    {
        cxt.blend.src = PVR_BLEND_ONE;
        cxt.blend.dst = PVR_BLEND_ZERO;
    }
    cxt.blend.src_enable = false;
    cxt.blend.dst_enable = false;

    pvr_poly_compile(&outHdr, &cxt);

    const unsigned slot = s_hdrNext++ % kHdrCacheSize;
    s_hdrKey[slot]  = key;
    s_hdrVal[slot]  = outHdr;
    s_hdrList[slot] = logical;
    s_hdrUsed[slot] = true;
}

class pvrImmediatePrimStream : public pddiPrimStream
{
public:
    void Begin(pvrContext* c, pddiShader* m, pddiPrimType t, unsigned vf, int reserveVerts, unsigned pass)
    {
        ctx = c;
        mat = m;
        primType = t;
        vertexFormat = vf;
        passIndex = pass;

        // Write straight into the shared vertex array; no per-stream staging
        // vectors and no second copy at flush time.
        immFirst = (unsigned)s_immVerts.size();
        // Reserving the exact figure defeats the geometric growth and makes
        // every stream in the frame reallocate and copy the whole buffer.
        if (reserveVerts > 0)
        {
            const size_t need = s_immVerts.size() + (size_t)reserveVerts;
            if (need > s_immVerts.capacity())
            {
                size_t cap = s_immVerts.capacity() ? s_immVerts.capacity() : 2048;
                while (cap < need)
                    cap *= 2;
                s_immVerts.reserve(cap);
            }
        }

        curColour = pddiColour(255, 255, 255, 255);
        curUV.u = 0.0f; curUV.v = 0.0f;
    }

    void Coord(float x, float y, float z) override
    {
        pvrImmVert iv;
        iv.x = x; iv.y = y; iv.z = z;
        iv.u = curUV.u; iv.v = curUV.v;
        iv.argb = (uint32_t)(unsigned)curColour;
        s_immVerts.push_back(iv);
    }

    void Normal(float x, float y, float z) override { (void)x; (void)y; (void)z; }

    void Colour(pddiColour colour, int channel = 0) override
    {
        (void)channel; // Multiple CBVs not handled yet.
        curColour = colour;
    }

    void UV(float u, float v, int channel = 0) override
    {
        if (channel == 0)
        {
            curUV.u = u;
            curUV.v = v;
        }
    }

    void Specular(pddiColour colour) override { (void)colour; }

    void Vertex(pddiVector* v, pddiColour c) override { Colour(c); Coord(v->x, v->y, v->z); }
    void Vertex(pddiVector* v, pddiVector* n) override { (void)n; Coord(v->x, v->y, v->z); }
    void Vertex(pddiVector* v, pddiVector2* uv) override { UV(uv->u, uv->v); Coord(v->x, v->y, v->z); }
    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv) override { Colour(c); UV(uv->u, uv->v); Coord(v->x, v->y, v->z); }
    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv) override { (void)n; UV(uv->u, uv->v); Coord(v->x, v->y, v->z); }

    void Flush()
    {
        const unsigned immCount = (unsigned)s_immVerts.size() - immFirst;

        if (!ctx || immCount == 0)
            return;

        pddiShader* useMat = mat ? mat : ctx->GetDefaultShader();
        pddiBaseShader* material = (pddiBaseShader*)useMat;
        material->SetMaterial((int)passIndex);

        pvrTextureEnv env = ((pvrMat*)material)->GetTextureEnv((int)passIndex);

        pvr_poly_hdr_t hdr;
        pvr_list_t list;
        ctx->BuildPolyHeader(env, hdr, list);

        const int slot = ListSlot(list);
        if (slot < 0 || ctx->colourWriteOff)
        {
            s_immVerts.resize(immFirst);
            return;
        }

        pvrDrawCmd cmd;
        cmd.hdr = hdr;
        cmd.hdrKey = pvrHdrSortKey(hdr);
        cmd.lightSet = ~0u;
        cmd.xform = ctx->GetViewProj();
        cmd.vp = ctx->GetViewportMap();
        cmd.buffer = NULL;
        cmd.immFirst = immFirst;
        cmd.immCount = immCount;
        cmd.immPrim = primType;
        cmd.argb = 0xffffffffu;
        cmd.uScale = GetUStrideScale(env.texture);
        cmd.cull = PVR_CULLING_NONE;
        pvrFillClip(cmd, ctx);
        pvrFoldViewport(cmd.xform, cmd.vp);

        s_drawCmds[slot].push_back(cmd);
    }

    static void SubmitDeferred(const pvrDrawCmd& cmd)
    {
        pvrSubmitClip(cmd);
        pvrSubmitHeader(cmd.hdr);

        pvrLoadXform(cmd.xform);

        const pvrViewportMap vp = cmd.vp;
        const pddiPrimType primType = cmd.immPrim;
        const pvrImmVert* verts = &s_immVerts[cmd.immFirst];
        const size_t count = cmd.immCount;

        auto fetch = [&](int i, pvrClipVert& out)
        {
            out.pos = shz_xmtrx_transform_vec4(
                shz_vec4_init(verts[i].x, verts[i].y, verts[i].z, 1.0f));
            out.u = verts[i].u * cmd.uScale;
            out.v = 1.0f - verts[i].v;
            out.argb = verts[i].argb;
        };

        auto submitTri = [&](int i0, int i1, int i2)
        {
            pvrClipVert tri[3];
            fetch(i0, tri[0]);
            fetch(i1, tri[1]);
            fetch(i2, tri[2]);
#ifdef RAD_DC_TRACE_VERTS
            {
                const shz_vec4_t c[3] = { tri[0].pos, tri[1].pos, tri[2].pos };
                const float uu[3] = { tri[0].u, tri[1].u, tri[2].u };
                const float vv[3] = { tri[0].v, tri[1].v, tri[2].v };
                TraceTri("IM", c, uu, vv, vp.oy, vp.hh);
            }
#endif
            ClipAndSubmitTriangle(vp, tri);
        };

        // The HUD and front end are device-projection 2D, so nothing can cross
        // the near plane: transform once, then emit native strips instead of
        // routing every triangle through the clipper at three vertices each.
        if (count >= 3
            && (primType == PDDI_PRIM_TRIANGLES || primType == PDDI_PRIM_TRISTRIP)
            && pvrReserveVtxCache((unsigned)count) && s_vtxPkt && s_vtxVis)
        {
            const bool oix = (pvrOixWindow != NULL) && ((unsigned)count <= PVR_OIX_VERTS);
#if defined SRR2_DC_PROFILER
            if (oix) { s_oixDraws++; s_oixVerts += (unsigned)count; }
            else     { s_sqDraws++;  s_sqVerts  += (unsigned)count; }
#endif
            pvr_vertex_t* const pkt = oix ? (pvr_vertex_t*)pvrOixWindow : s_vtxPkt;
            unsigned allVis = 1;

            for (size_t i = 0; i < count; i++)
            {
                pvr_vertex_t& v = pkt[i];

                const shz_vec4_t p = shz_xmtrx_transform_vec4(
                    shz_vec4_init(verts[i].x, verts[i].y, verts[i].z, 1.0f));

                const unsigned vis = (p.w >= p.z + PVR_NEAR_CLIP_EPSILON) ? 1u : 0u;
                s_vtxVis[i] = (unsigned char)vis;
                allVis &= vis;

                v.flags = PVR_CMD_VERTEX;

                if (vis)
                {
                    const float iw = shz_invf_fsrra(p.w);
                    v.x = p.x * iw;
                    v.y = p.y * iw;
                    v.z = iw;
                }

                v.u = verts[i].u * cmd.uScale;
                v.v = 1.0f - verts[i].v;
                v.argb = verts[i].argb;
                v.oargb = 0;
            }

            if (allVis)
            {
                if (primType == PDDI_PRIM_TRISTRIP)
                {
                    for (size_t i = 0; i < count; i++)
                    {
                        SubmitVert(oix, &pkt[i], (i == count - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
                    }
                }
                else
                {
                    for (size_t i = 0; i + 2 < count; i += 3)
                    {
                        SubmitVert(oix, &pkt[i], PVR_CMD_VERTEX);
                        SubmitVert(oix, &pkt[i + 1], PVR_CMD_VERTEX);
                        SubmitVert(oix, &pkt[i + 2], PVR_CMD_VERTEX_EOL);
                    }
                }
                return;
            }
        }

        if (primType == PDDI_PRIM_TRIANGLES)
        {
            for (size_t i = 0; i + 2 < count; i += 3)
                submitTri((int)i, (int)i + 1, (int)i + 2);
        }
        else if (primType == PDDI_PRIM_TRISTRIP)
        {
            for (size_t i = 0; i + 2 < count; ++i)
            {
                const bool odd = (i & 1u) != 0;
                const int i0 = (int)i;
                const int i1 = (int)i + 1;
                const int i2 = (int)i + 2;
                submitTri(i0, odd ? i2 : i1, odd ? i1 : i2);
            }
        }
        else if (primType == PDDI_PRIM_LINES || primType == PDDI_PRIM_LINESTRIP)
        {
            auto submitLine = [&](int i0, int i1)
            {
                pvrClipVert a, b;
                fetch(i0, a);
                fetch(i1, b);

                const bool visA = ClipVertVisible(a);
                const bool visB = ClipVertVisible(b);
                if (!visA && !visB)
                    return;
                if (!visA)
                    ClipEdge(b, a, a);
                else if (!visB)
                    ClipEdge(a, b, b);

                const float invwA = 1.0f / a.pos.w;
                const float invwB = 1.0f / b.pos.w;
                const float ax = a.pos.x * invwA;
                const float ay = a.pos.y * invwA;
                const float bx = b.pos.x * invwB;
                const float by = b.pos.y * invwB;

                const float dx = bx - ax;
                const float dy = by - ay;
                const float len = sqrtf(dx * dx + dy * dy);
                if (len < 1e-3f)
                    return;

                const float nx = (-dy / len) * 0.5f;
                const float ny = ( dx / len) * 0.5f;

                pvrCacheVert q[4];
                q[0].sx = ax + nx; q[0].sy = ay + ny; q[0].sz = invwA; q[0].argb = a.argb;
                q[1].sx = ax - nx; q[1].sy = ay - ny; q[1].sz = invwA; q[1].argb = a.argb;
                q[2].sx = bx + nx; q[2].sy = by + ny; q[2].sz = invwB; q[2].argb = b.argb;
                q[3].sx = bx - nx; q[3].sy = by - ny; q[3].sz = invwB; q[3].argb = b.argb;

                for (int k = 0; k < 4; k++)
                {
                    q[k].u = 0.0f;
                    q[k].v = 0.0f;
                    SubmitScreenVert(q[k], (k == 3) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
                }
            };

            if (primType == PDDI_PRIM_LINES)
            {
                for (size_t i = 0; i + 1 < count; i += 2)
                    submitLine((int)i, (int)i + 1);
            }
            else
            {
                for (size_t i = 0; i + 1 < count; ++i)
                    submitLine((int)i, (int)i + 1);
            }
        }


    }

private:
    pvrContext* ctx = nullptr;
    pddiShader* mat = nullptr;
    pddiPrimType primType = PDDI_PRIM_TRIANGLES;
    unsigned vertexFormat = 0;
    unsigned passIndex = 0;

    pddiColour curColour;
    pddiVector2 curUV;

    unsigned immFirst = 0;
};

static pvrImmediatePrimStream g_imStream;

// Only groups that carry normals are lit, and consecutive draws almost always
// share their lights, so the pool usually ends a frame one or two entries long.
static unsigned pvrPoolLightSet(const pvrContext* ctx)
{
    pvrLightSet ls;
    const_cast<pvrContext*>(ctx)->BuildLightSet(&ls);

    if (!s_lightSets.empty()
        && memcmp(&s_lightSets.back(), &ls, sizeof(pvrLightSet)) == 0)
    {
        return (unsigned)(s_lightSets.size() - 1);
    }

    s_lightSets.push_back(ls);
    return (unsigned)(s_lightSets.size() - 1);
}

static void pvrResetDeferredLists()
{
    s_lightSets.clear();

    for (int i = 0; i < 3; i++)
    {
        for (size_t c = 0; c < s_drawCmds[i].size(); c++)
        {
            if (s_drawCmds[i][c].buffer)
                s_drawCmds[i][c].buffer->Release();
        }
        s_drawCmds[i].clear();
    }
    s_immVerts.clear();
}

static void pvrRunDeferredLists()
{
    s_lastDraws = (unsigned)(s_drawCmds[0].size() + s_drawCmds[1].size() + s_drawCmds[2].size());
#if defined SRR2_DC_PROFILER
    s_lastBoxCulled = s_boxCulled;
    s_lastFusedDraws = s_fusedDraws;
    s_lastMissPrim = s_missPrim;   s_missPrim = 0;
    s_lastMissIdx = s_missIdx;     s_missIdx = 0;
    s_lastMissOther = s_missOther; s_missOther = 0;
    s_lastOixDraws = s_oixDraws;  s_lastOixVerts = s_oixVerts;
    s_lastSqDraws  = s_sqDraws;   s_lastSqVerts  = s_sqVerts;
    s_oixDraws = 0; s_oixVerts = 0;
    s_sqDraws  = 0; s_sqVerts  = 0;
    s_lastVtxPerFrame = s_vtxPerFrame;
    s_lastVtxEmitted = s_vtxEmitted;
    s_vtxEmitted = 0;
    s_lastSetup = s_phSetup; s_phSetup = 0;
    s_lastXform = s_phXform; s_phXform = 0;
    s_lastEmit  = s_phEmit;  s_phEmit  = 0;
    s_lastClip  = s_phClip;  s_phClip  = 0;
    s_lastImm   = s_phImm;   s_phImm   = 0;
    s_lastEmitPlain = s_phEmitPlain; s_phEmitPlain = 0;
    s_lastEmitVis   = s_phEmitVis;   s_phEmitVis   = 0;
    s_lastEmitVertsPlain = s_emitVertsPlain; s_emitVertsPlain = 0;
    s_lastEmitVertsVis   = s_emitVertsVis;   s_emitVertsVis   = 0;
    s_lastXformFront    = s_phXformFront;    s_phXformFront    = 0;
    s_lastXformStraddle = s_phXformStraddle; s_phXformStraddle = 0;
    s_lastXformVertsFront    = s_xformVertsFront;    s_xformVertsFront    = 0;
    s_lastXformVertsStraddle = s_xformVertsStraddle; s_xformVertsStraddle = 0;
    s_lastDistCulled = s_distCulled; s_distCulled = 0;
    for (unsigned b = 0; b < SRR_DEPTH_BINS; b++)
    {
        s_lastDepthVerts[b] = s_depthVerts[b]; s_depthVerts[b] = 0;
        s_lastDepthDraws[b] = s_depthDraws[b]; s_depthDraws[b] = 0;
    }
    s_lastDotAvg = s_dotCount ? (int)((s_dotSum / (float)s_dotCount) * 100.0f) : 0;
    s_dotSum = 0.0f; s_dotCount = 0;
    for (unsigned li = 0; li < 4u; li++)
        for (unsigned ax = 0; ax < 3u; ax++)
            s_lastDirI[li][ax] = (int)(s_lDir[li][ax] * 100.0f);
    s_lastLitSat   = s_litSat;   s_litSat   = 0;
    s_lastLitZeroN = s_litZeroN; s_litZeroN = 0;
    s_lastRunSkip  = s_runSkip;  s_runSkip  = 0;
    s_lastRunSkipV = s_runSkipV; s_runSkipV = 0;
    s_lastRunDead  = s_runDead;  s_runDead  = 0;
    s_lastRunAll   = s_runAll;   s_runAll   = 0;
    s_lastRunMixed = s_runMixed; s_runMixed = 0;
    s_lastRunAllV   = s_runAllV;   s_runAllV   = 0;
    s_lastRunMixedV = s_runMixedV; s_runMixedV = 0;
    s_lastLitFlagOn  = s_litFlagOn;  s_litFlagOn  = 0;
    s_lastLitFlagOff = s_litFlagOff; s_litFlagOff = 0;
    s_lastClipFast = s_clipDrawsFast;       s_clipDrawsFast = 0;
    s_lastClipGeneric = s_clipDrawsGeneric; s_clipDrawsGeneric = 0;
    s_lastVtxXformed = s_vtxXformed;        s_vtxXformed = 0;
    s_lastStripTris = s_stripTris; s_stripTris = 0;
    s_lastClipTris  = s_clipTris;  s_clipTris  = 0;
    s_lastDeadTris  = s_deadTris;  s_deadTris  = 0;
    s_lastClipIters = s_clipIters; s_clipIters = 0;
    s_lastClipTriUs = s_phClipTri; s_phClipTri = 0;
    s_lastGenIters  = s_genIters;  s_genIters  = 0;
    s_lastGenWalkUs = s_phGenWalk; s_phGenWalk = 0;
    s_lastHdrSubmitted = s_hdrSubmitted; s_hdrSubmitted = 0;
    s_lastHdrSkipped   = s_hdrSkipped;   s_hdrSkipped   = 0;
    s_lastXformLoaded  = s_xformLoaded;  s_xformLoaded  = 0;
    s_lastXformSkipped = s_xformSkipped; s_xformSkipped = 0;
    s_lastLitVerts = s_litVerts;   s_litVerts = 0;
    s_lastLitDraws = s_litDraws;   s_litDraws = 0;
    s_lastLightCount = s_lightCount;       s_lightCount = 0;
    s_lastRecNormalDraws = s_recNormalDraws; s_recNormalDraws = 0;
    s_lastPoolSize = (unsigned)s_lightSets.size();
    s_lastPoolCount = s_lightSets.empty() ? 0u : s_lightSets.back().count;
    s_boxCulled = 0;
    s_fusedDraws = 0;
    s_vtxPerFrame = 0;
#endif
#if defined SRR2_DC_PVR_TRACE
    const uint64_t replayStart = timer_us_gettime64();
#endif

    const bool anyWork = !s_drawCmds[0].empty()
                      || !s_drawCmds[1].empty()
                      || !s_drawCmds[2].empty();

    if (anyWork)
        pvrOixEnter();

    for (int i = 0; i < 3; i++)
    {
        const std::vector<pvrDrawCmd>& cmds = s_drawCmds[i];
        if (cmds.empty())
            continue;

        // Opaque and punch-through are depth tested, so grouping them by
        // material only changes how many headers the TA sees. Translucent is
        // depth sorted by the engine and must keep the order it was given.
        if (i != 2)
        {
            std::stable_sort(s_drawCmds[i].begin(), s_drawCmds[i].end(),
                             [](const pvrDrawCmd& a, const pvrDrawCmd& b)
                             { return a.hdrKey < b.hdrKey; });
        }

        if (pvr_list_begin(kListOrder[i]) < 0)
            continue;

        pvrResetSubmitState();

        for (size_t c = 0; c < cmds.size(); c++)
        {
            const pvrDrawCmd& cmd = cmds[c];
            if (cmd.buffer)
            {
                cmd.buffer->SubmitDeferred(cmd);
            }
            else
            {
#if defined SRR2_DC_PVR_TRACE
                const unsigned emitBefore = s_vtxEmit;
                const uint64_t imStart = timer_us_gettime64();
#endif
#if defined SRR2_DC_PROFILER
                const uint64_t phImm_ = timer_us_gettime64();
#endif
                pvrImmediatePrimStream::SubmitDeferred(cmd);
#if defined SRR2_DC_PROFILER
                s_phImm += timer_us_gettime64() - phImm_;
#endif
#if defined SRR2_DC_PVR_TRACE
                s_replayImUs += timer_us_gettime64() - imStart;
                s_vtxEmitIM += s_vtxEmit - emitBefore;
#endif
            }
        }

        pvr_list_finish();
    }
    if (anyWork)
        pvrOixLeave();

#if defined SRR2_DC_PVR_TRACE
    s_replayUs = timer_us_gettime64() - replayStart;
#endif
}

pddiPrimStream* pvrContext::BeginPrims(pddiShader* material, pddiPrimType primType, unsigned vertexType, int vertexCount, unsigned pass)
{
    if (!material)
        material = defaultShader;

    pddiBaseContext::BeginPrims(material, primType, vertexType, vertexCount, pass);
    g_imStream.Begin(this, material, primType, vertexType, vertexCount, pass);
    return &g_imStream;
}

void pvrContext::EndPrims(pddiPrimStream* stream)
{
    pddiBaseContext::EndPrims(stream);
    if (stream == &g_imStream)
        g_imStream.Flush();
}

void pvrContext::DrawPrimBuffer(pddiShader* material, pddiPrimBuffer* buffer)
{
    if (!buffer)
        return;
    if (!material)
        material = defaultShader;

    pddiBaseShader* m = (pddiBaseShader*)material;
    m->SetMaterial(0);
    ((pvrPrimBuffer*)buffer)->DisplayWithMaterial((pvrMat*)m, 0);
}

int pvrContext::GetMaxLights()
{
    return 8;
}

void pvrContext::SetAmbientLight(pddiColour col)
{
    pddiBaseContext::SetAmbientLight(col);
}

void pvrContext::SetCullMode(pddiCullMode mode)
{
    pddiBaseContext::SetCullMode(mode);
    // PVR: cxt.gen.culling
}

// PVR has no colour write mask. Callers use one to punch depth without
// touching the frame buffer -- the hud map masks itself to a circle that way
// -- and drawing the geometry anyway paints it solid. Nothing is lost by
// dropping it: these overlays land in the translucent list, where depth writes
// are off, so the draw only ever contributed colour.
void pvrContext::SetColourWrite(bool red, bool green, bool blue, bool alpha)
{
    pddiBaseContext::SetColourWrite(red, green, blue, alpha);
    colourWriteOff = !(red || green || blue);
}

void pvrContext::EnableZBuffer(bool enable)
{
    pddiBaseContext::EnableZBuffer(enable);
}

void pvrContext::SetZCompare(pddiCompareMode compareMode)
{
    pddiBaseContext::SetZCompare(compareMode);
    // PVR: cxt.depth.comparison
}

void pvrContext::SetZWrite(bool b)
{
    pddiBaseContext::SetZWrite(b);
    // PVR: cxt.depth.write
}

void pvrContext::SetZBias(float bias)
{
    pddiBaseContext::SetZBias(bias);
}

void pvrContext::SetZRange(float n, float f)
{
    pddiBaseContext::SetZRange(n, f);
}

void pvrContext::EnableStencilBuffer(bool enable)
{
    pddiBaseContext::EnableStencilBuffer(enable);
}

void pvrContext::SetStencilCompare(pddiCompareMode compare)
{
    pddiBaseContext::SetStencilCompare(compare);
}

void pvrContext::SetStencilRef(int ref)
{
    pddiBaseContext::SetStencilRef(ref);
}

void pvrContext::SetStencilMask(unsigned mask)
{
    pddiBaseContext::SetStencilMask(mask);
}

void pvrContext::SetStencilWriteMask(unsigned mask)
{
    pddiBaseContext::SetStencilWriteMask(mask);
}

void pvrContext::SetStencilOp(pddiStencilOp failOp, pddiStencilOp zFailOp, pddiStencilOp zPassOp)
{
    pddiBaseContext::SetStencilOp(failOp, zFailOp, zPassOp);
}

void pvrContext::SetFillMode(pddiFillMode mode)
{
    pddiBaseContext::SetFillMode(mode);
}

void pvrContext::EnableFog(bool enable)
{
    pddiBaseContext::EnableFog(enable);

    // The game turns fog off at view setup and never turns it back on, so a
    // configured draw distance keeps it: the whole point is to hide where the
    // world stops.
    if (!enable && SRR_DC_DEPTH_CULL > 0.0f)
        return;

    fogOn = enable;
    fogDirty = true;
}

void pvrContext::SetFog(pddiColour colour, float start, float end)
{
    pddiBaseContext::SetFog(colour, start, end);

    fogRGB = ((unsigned)colour.Red() << 16) | ((unsigned)colour.Green() << 8)
           | (unsigned)colour.Blue();
    fogStart = start;
    fogEnd = end;
    fogDirty = true;
}

void pvrContext::ApplyFog()
{
    fogDirty = false;

    if (!fogOn)
        return;

    pvr_fog_table_color(1.0f,
                        (float)((fogRGB >> 16) & 0xFFu) * (1.0f / 255.0f),
                        (float)((fogRGB >>  8) & 0xFFu) * (1.0f / 255.0f),
                        (float)( fogRGB        & 0xFFu) * (1.0f / 255.0f));

    // Sets the density register for the end value itself and builds a
    // perspective correct ramp, so these are plain world distances.
    pvr_fog_table_linear(fogStart, fogEnd);
}

int pvrContext::GetMaxTextureDimension(void)
{
    return maxTexSize;
}

pddiExtension* pvrContext::GetExtension(unsigned extID)
{
    (void)extID;
    return NULL;
}

bool pvrContext::VerifyExtension(unsigned extID)
{
    (void)extID;
    return false;
}

void pvrContext::SetupHardwareLight(int handle)
{
    if (handle < 0 || handle >= PDDI_MAX_LIGHTS)
        return;

    // Lights are set before any model matrix is pushed, so the stack top here
    // is the view matrix. Keeping it per light means a light set while a
    // different camera was current still resolves correctly.
    lightView[handle] = *state.matrixStack[PDDI_MATRIX_MODELVIEW]->Top();
}

// Rows of out->dir become the object-space directions of up to four enabled
// directional lights, ready for a single ftrv per normal.
unsigned pvrContext::BuildLightSet(pvrLightSet* out) const
{
    const pddiColour amb = state.lightingState->ambient;
    out->ambR = (float)amb.Red();
    out->ambG = (float)amb.Green();
    out->ambB = (float)amb.Blue();

    rmt::Matrix inv;
    inv.Transpose(*state.matrixStack[PDDI_MATRIX_MODELVIEW]->Top());

    unsigned n = 0;
    for (int i = 0; i < PDDI_MAX_LIGHTS && n < 4; i++)
    {
        const pddiLight& l = state.lightingState->light[i];
        if (!l.enabled || l.type != PDDI_LIGHT_DIRECTIONAL)
            continue;

        rmt::Vector v;
        v.Rotate(l.worldDirection, lightView[i]);   // world -> view
        rmt::Vector o;
        o.Rotate(v, inv);                           // view -> object

        // Stored negated: the diffuse term wants the direction towards the
        // light, and worldDirection points the way the light travels.
        out->dir.elem2D[0][n] = -o.x;
        out->dir.elem2D[1][n] = -o.y;
        out->dir.elem2D[2][n] = -o.z;
        out->dir.elem2D[3][n] = 0.0f;

        const float ls_ = (float)SRR_DC_LIGHT_SCALE * 0.01f;
        out->r[n] = (float)l.colour.Red()   * l.intensity * ls_;
        out->g[n] = (float)l.colour.Green() * l.intensity * ls_;
        out->b[n] = (float)l.colour.Blue()  * l.intensity * ls_;
        n++;
    }

    for (unsigned i = n; i < 4; i++)
    {
        out->dir.elem2D[0][i] = 0.0f;
        out->dir.elem2D[1][i] = 0.0f;
        out->dir.elem2D[2][i] = 0.0f;
        out->dir.elem2D[3][i] = 0.0f;
        out->r[i] = out->g[i] = out->b[i] = 0.0f;
    }

    out->count = n;
#if defined SRR2_DC_PROFILER
    for (unsigned li = 0; li < 4u; li++)
    {
        s_lDir[li][0] = out->dir.elem2D[0][li];
        s_lDir[li][1] = out->dir.elem2D[1][li];
        s_lDir[li][2] = out->dir.elem2D[2][li];
    }
    s_ambRGB = ((unsigned)amb.Red() << 16) | ((unsigned)amb.Green() << 8) | amb.Blue();
    if (n)
    {
        int lr = (int)out->r[0], lg = (int)out->g[0], lb = (int)out->b[0];
        if (lr > 255) lr = 255;
        if (lg > 255) lg = 255;
        if (lb > 255) lb = 255;
        s_l0RGB = ((unsigned)lr << 16) | ((unsigned)lg << 8) | (unsigned)lb;
    }
    else
    {
        s_l0RGB = 0u;
    }
#endif
    return n;
}

void pvrContext::BeginTiming(void)
{
}

float pvrContext::EndTiming(void)
{
    return 0.0f;
}

void pvrContext::SetVertexArray(unsigned descr, void* data, int count)
{
    (void)descr;
    (void)data;
    (void)count;
}

//---------- pvrPrimBuffer ----------
#include <pddi/base/basecontext.hpp>

class pvrPrimBufferStream : public pddiPrimBufferStream
{
public:
    pvrPrimBuffer* buffer;
    unsigned cur;

    pvrPrimBufferStream(pvrPrimBuffer* b) : buffer(b), cur(0) {}

    void Next(void) override
    {
        cur++;
        if (cur > buffer->total)
            buffer->total = cur;
        PDDIASSERT(cur <= buffer->allocated);
    }

    void Position(float x, float y, float z) override
    {
        if (!buffer->coord)
            buffer->RestoreCoords();
        if (!buffer->coord)
            return;
        buffer->coord[cur * 3 + 0] = x;
        buffer->coord[cur * 3 + 1] = y;
        buffer->coord[cur * 3 + 2] = z;
        buffer->coordWritten = true;
        Next();
    }

    void Normal(float x, float y, float z) override
    {
        // BuildInterleaved frees normalQ and nothing restores it, so on a
        // dynamic buffer the packed normals live in the interleaved array and
        // that is where the skinned ones have to land. Writing them anywhere
        // else lights an animated character in its bind pose. Normal() runs
        // immediately before Position() for each vertex, so cur is the index
        // about to be filled.
        signed char* d;

        if (buffer->normalQ)
        {
            d = &buffer->normalQ[cur * 3];
        }
        else if (buffer->dynamic && buffer->inter && buffer->interNormal
                 && cur < buffer->interCount)
        {
            d = buffer->inter[cur].n;
        }
        else
        {
            return;
        }

        const float lsq = x * x + y * y + z * z;
        if (lsq > 1e-12f)
        {
            const float inv = shz_inv_sqrtf_fsrra(lsq);
            x *= inv; y *= inv; z *= inv;
        }
        else
        {
            x = 0.0f; y = 0.0f; z = 1.0f;
        }

        d[0] = (signed char)(x * 127.0f);
        d[1] = (signed char)(y * 127.0f);
        d[2] = (signed char)(z * 127.0f);
    }

    void Colour(pddiColour c, int channel = 0) override
    {
        (void)channel;
        if (!buffer->colour) return;
        buffer->colour[cur * 4 + 0] = (unsigned char)c.Red();
        buffer->colour[cur * 4 + 1] = (unsigned char)c.Green();
        buffer->colour[cur * 4 + 2] = (unsigned char)c.Blue();
        buffer->colour[cur * 4 + 3] = (unsigned char)c.Alpha();
    }

    void TexCoord1(float s, int channel = 0) override { (void)s; (void)channel; }
    void TexCoord2(float s, float t, int channel = 0) override
    {
        if (channel != 0) return;
        if (!buffer->uv)
            buffer->RestoreUVs();
        if (!buffer->uv) return;
        buffer->uv[cur * 2 + 0] = s;
        buffer->uv[cur * 2 + 1] = t;
        buffer->uvWritten = true;
    }

    void TexCoord3(float s, float t, float u, int channel = 0) override { (void)s; (void)t; (void)u; (void)channel; }
    void TexCoord4(float s, float t, float u, float v, int channel = 0) override { (void)s; (void)t; (void)u; (void)v; (void)channel; }
    void Specular(pddiColour c) { (void)c; }
    void SkinIndices(unsigned a, unsigned b = 0, unsigned c = 0, unsigned d = 0) { (void)a; (void)b; (void)c; (void)d; }
    void SkinWeights(float a, float b = 0.0f, float c = 0.0f) { (void)a; (void)b; (void)c; }
    void Vertex(rmt::Vector* v, pddiColour c) { (void)v; (void)c; }
    void Vertex(rmt::Vector* v, rmt::Vector* n) { (void)v; (void)n; }
    void Vertex(rmt::Vector* v, rmt::Vector2* uv) { (void)v; (void)uv; }
    void Vertex(rmt::Vector* v, pddiColour c, rmt::Vector2* uv) { (void)v; (void)c; (void)uv; }
    void Vertex(rmt::Vector* v, rmt::Vector* n, rmt::Vector2* uv) { (void)v; (void)n; (void)uv; }
};

pvrPrimBuffer::pvrPrimBuffer(pvrContext* c, pddiPrimType type, unsigned vertexFormat, int nVertex, int nIndex)
    : context(c)
    , primType(type)
    , vertexType(vertexFormat)
    , stream(NULL)
    , nStrips(0)
    , strips(NULL)
    , coord(NULL)
    , coordQ(NULL)
    , coordWritten(false)
    , dynamic(false)
    , coordQCount(0)
    , bbValid(false)

    , uvQ(NULL)
    , uvWritten(false)
    , uvQCount(0)
    , normal(NULL)
    , normalQ(NULL)
    , uv(NULL)
    , colour(NULL)
    , runs(NULL)
    , runCount(0)
    , runLo(NULL)
    , runHi(NULL)
    , inter(NULL)
    , interCount(0)
    , interUV(false)
    , interColour(false)
    , interNormal(false)
    , allocated((unsigned)nVertex)
    , total(0)
    , indices(NULL)
    , indexCount((unsigned)nIndex)
    , valid(false)
    , mem(0)
{
    context->AddRef();

    stream = new pvrPrimBufferStream(this);

    mem = 0;

    coord = new float[3 * (size_t)allocated];
    qScale[0] = qScale[1] = qScale[2] = 1.0f;
    qBias[0] = qBias[1] = qBias[2] = 0.0f;
    mem += 6;

    // Kept as three signed bytes rather than three floats. A group that has
    // normals has no baked colours, so this array and the colour array are
    // never both live.
    (void)normal;
    if (vertexFormat & PDDI_V_NORMAL)
    {
        normalQ = new signed char[3 * (size_t)allocated];
        mem += 3;
    }

    if (vertexFormat & 0xf)
    {
        uv = new float[2 * (size_t)allocated];
        uvScale[0] = uvScale[1] = 1.0f;
        uvBias[0] = uvBias[1] = 0.0f;
        mem += 4;
    }

    if (vertexFormat & PDDI_V_COLOUR)
    {
        colour = new unsigned char[4 * (size_t)allocated];
        mem += 4;
    }

    mem *= allocated;

    if (indexCount)
        indices = new unsigned short[indexCount];
}

pvrPrimBuffer::~pvrPrimBuffer()
{
    if (strips)
        delete[] strips;
    if (coord)
        delete[] coord;
    delete[] runLo; runLo = NULL;
    delete[] runHi; runHi = NULL;
    if (coordQ)
        delete[] coordQ;
    if (normal)
        delete[] normal;
    if (normalQ)
        delete[] normalQ;
    if (uv)
        delete[] uv;
    if (uvQ)
        delete[] uvQ;
    if (colour)
        delete[] colour;
    if (inter)
        delete[] inter;
    if (runs)
        delete[] runs;
    if (indices)
        delete[] indices;
    if (stream)
        delete stream;
    context->Release();
}

pvr_cull_mode_t pvrContext::GetCurrentCull() const
{
    return MapCull(state.renderState->cullMode);
}

void pvrContext::BuildTransform(const float* scale, const float* bias, shz_mat4x4_t* out) const
{
    shz_mat4x4_t& m = *out;

    for (int row = 0; row < 4; row++)
    {
        const float c0 = viewProjM.elem2D[0][row];
        const float c1 = viewProjM.elem2D[1][row];
        const float c2 = viewProjM.elem2D[2][row];

        m.elem2D[0][row] = c0 * scale[0];
        m.elem2D[1][row] = c1 * scale[1];
        m.elem2D[2][row] = c2 * scale[2];
        m.elem2D[3][row] = c0 * bias[0] + c1 * bias[1] + c2 * bias[2]
                         + viewProjM.elem2D[3][row];
    }
}

void pvrContext::LoadTransformToXmtrx(const float* scale, const float* bias) const
{
    shz_mat4x4_t m;
    BuildTransform(scale, bias, &m);
    shz_xmtrx_load_4x4(&m);
}

void pvrPrimBuffer::QuantiseCoords()
{
    if (!coord || !coordWritten || total == 0)
        return;

    float mn[3], mx[3];
    for (int a = 0; a < 3; a++)
    {
        mn[a] = mx[a] = coord[a];
    }
    for (unsigned v = 1; v < total; v++)
    {
        for (int a = 0; a < 3; a++)
        {
            const float f = coord[v * 3 + a];
            if (f < mn[a]) mn[a] = f;
            if (f > mx[a]) mx[a] = f;
        }
    }

    float inv[3];
    for (int a = 0; a < 3; a++)
    {
        const float half = 0.5f * (mx[a] - mn[a]);
        qBias[a] = 0.5f * (mx[a] + mn[a]);
        qScale[a] = (half > 0.0f) ? (half / 32767.0f) : 1.0f;
        inv[a] = 1.0f / qScale[a];

        bbMin[a] = mn[a];
        bbMax[a] = mx[a];
    }
    bbValid = true;

    if (!coordQ)
    {
        coordQ = new short[3 * (size_t)allocated];
        if (!coordQ)
            return;
    }

    // The scale is fixed for the whole pass, so this is a reciprocal and a
    // multiply rather than three fdiv a vertex.
    for (unsigned v = 0; v < total; v++)
    {
        for (int a = 0; a < 3; a++)
        {
            float q = (coord[v * 3 + a] - qBias[a]) * inv[a];
            if (q > 32767.0f) q = 32767.0f;
            if (q < -32767.0f) q = -32767.0f;
            coordQ[v * 3 + a] = (short)(q >= 0.0f ? (q + 0.5f) : (q - 0.5f));
        }
    }

    coordQCount = total;
    delete[] coord;
    coord = NULL;
}

void pvrPrimBuffer::QuantiseUVs()
{
    if (!uv || !uvWritten || total == 0)
        return;

    float mn[2], mx[2];
    mn[0] = mx[0] = uv[0];
    mn[1] = mx[1] = uv[1];

    for (unsigned v = 1; v < total; v++)
    {
        for (int a = 0; a < 2; a++)
        {
            const float f = uv[v * 2 + a];
            if (f < mn[a]) mn[a] = f;
            if (f > mx[a]) mx[a] = f;
        }
    }

    float inv[2];
    for (int a = 0; a < 2; a++)
    {
        const float half = 0.5f * (mx[a] - mn[a]);
        uvBias[a] = 0.5f * (mx[a] + mn[a]);
        uvScale[a] = (half > 0.0f) ? (half / 32767.0f) : 1.0f;
        inv[a] = 1.0f / uvScale[a];
    }

    if (!uvQ)
    {
        uvQ = new short[2 * (size_t)allocated];
        if (!uvQ)
            return;
    }

    for (unsigned v = 0; v < total; v++)
    {
        for (int a = 0; a < 2; a++)
        {
            float q = (uv[v * 2 + a] - uvBias[a]) * inv[a];
            if (q > 32767.0f) q = 32767.0f;
            if (q < -32767.0f) q = -32767.0f;
            uvQ[v * 2 + a] = (short)(q >= 0.0f ? (q + 0.5f) : (q - 0.5f));
        }
    }

    uvQCount = total;
    delete[] uv;
    uv = NULL;
}

void pvrPrimBuffer::RestoreUVs()
{
    DropInterleaved();
    if (uv || !uvQ)
        return;

    uv = new float[2 * (size_t)allocated];
    if (!uv)
        return;

    for (unsigned v = 0; v < uvQCount; v++)
    {
        for (int a = 0; a < 2; a++)
            uv[v * 2 + a] = (float)uvQ[v * 2 + a] * uvScale[a] + uvBias[a];
    }
}

void pvrPrimBuffer::RestoreCoords()
{
    // A dynamic buffer is about to have every position overwritten, so there
    // is nothing to restore: hand back a staging array and leave the
    // interleaved data alone for RequantiseDynamic to write into.
    if (dynamic && inter)
    {
        if (!coord)
            coord = new float[3 * (size_t)allocated];
        return;
    }

    DropInterleaved();
    if (coord || !coordQ)
        return;

    coord = new float[3 * (size_t)allocated];
    if (!coord)
        return;

    for (unsigned v = 0; v < coordQCount; v++)
    {
        for (int a = 0; a < 3; a++)
            coord[v * 3 + a] = (float)coordQ[v * 3 + a] * qScale[a] + qBias[a];
    }
}

// Rewrite the positions inside the interleaved array that is already built,
// leaving its uvs and colours where they are.
//
// The alternative is what a buffer written once at load time does: tear the
// interleaved array back apart into three separate arrays, dequantise the
// positions to float, quantise them again, then rebuild. That is five
// allocations and seven passes over the vertices, every frame, for data that
// is overwritten before anything reads it twice.
void pvrPrimBuffer::RequantiseDynamic()
{
    const unsigned n = total;
    if (!coord || !inter || n == 0 || n != interCount)
        return;

    float mn[3], mx[3];
    for (int a = 0; a < 3; a++)
        mn[a] = mx[a] = coord[a];

    for (unsigned v = 1; v < n; v++)
    {
        for (int a = 0; a < 3; a++)
        {
            const float f = coord[v * 3 + a];
            if (f < mn[a]) mn[a] = f;
            if (f > mx[a]) mx[a] = f;
        }
    }

    float inv[3];
    for (int a = 0; a < 3; a++)
    {
        const float half = 0.5f * (mx[a] - mn[a]);
        qBias[a] = 0.5f * (mx[a] + mn[a]);
        qScale[a] = (half > 0.0f) ? (half / 32767.0f) : 1.0f;
        inv[a] = 1.0f / qScale[a];

        bbMin[a] = mn[a];
        bbMax[a] = mx[a];
    }
    bbValid = true;

    for (unsigned v = 0; v < n; v++)
    {
        short* d = &inter[v].x;
        for (int a = 0; a < 3; a++)
        {
            float q = (coord[v * 3 + a] - qBias[a]) * inv[a];
            if (q > 32767.0f) q = 32767.0f;
            if (q < -32767.0f) q = -32767.0f;
            d[a] = (short)(q >= 0.0f ? (q + 0.5f) : (q - 0.5f));
        }
    }

    // The array carries one extra vertex so the strip walker can read past the
    // end; keep it a copy of the last.
    inter[n] = inter[n - 1];

    delete[] coord;
    coord = NULL;
}

void pvrPrimBuffer::BuildInterleaved()
{
    if (inter || !coordQ || coordQCount == 0)
        return;

    const unsigned n = coordQCount;
    pvrInterVert* p = new pvrInterVert[n + 1];
    if (!p)
        return;

    const bool haveUV  = uvQ && uvQCount >= n;
    const bool haveCol = colour != NULL;
    const bool haveNrm = !haveCol && normalQ != NULL;

    for (unsigned i = 0; i < n; i++)
    {
        p[i].x = coordQ[i * 3 + 0];
        p[i].y = coordQ[i * 3 + 1];
        p[i].z = coordQ[i * 3 + 2];
        p[i].u = haveUV ? uvQ[i * 2 + 0] : (short)0;
        p[i].v = haveUV ? uvQ[i * 2 + 1] : (short)0;
        p[i].pad = 0;

        if (haveCol)
        {
            const unsigned char* cc = &colour[i * 4];
            p[i].argb = ((unsigned)cc[3] << 24) | ((unsigned)cc[0] << 16)
                      | ((unsigned)cc[1] << 8) | (unsigned)cc[2];
        }
        else if (haveNrm)
        {
            const signed char* nn = &normalQ[i * 3];
            p[i].n[0] = nn[0];
            p[i].n[1] = nn[1];
            p[i].n[2] = nn[2];
            p[i].n[3] = 0;
        }
        else
        {
            p[i].argb = 0xFFFFFFFFu;
        }
    }

    p[n] = p[n - 1];

    inter = p;
    interCount = n;
    interUV = haveUV;
    interColour = haveCol;
    interNormal = haveNrm;

    delete[] coordQ;  coordQ = NULL;
    delete[] uvQ;     uvQ = NULL;
    delete[] colour;  colour = NULL;
    delete[] normalQ; normalQ = NULL;
}

// Hand the split arrays back. The stream writers call the Restore paths when
// the engine rewrites a buffer, and those want the layout they were built for.
void pvrPrimBuffer::DropInterleaved()
{
    if (!inter)
        return;

    if (!coordQ)
    {
        coordQ = new short[3 * (size_t)allocated];
        if (coordQ)
        {
            for (unsigned i = 0; i < interCount; i++)
            {
                coordQ[i * 3 + 0] = inter[i].x;
                coordQ[i * 3 + 1] = inter[i].y;
                coordQ[i * 3 + 2] = inter[i].z;
            }
            coordQCount = interCount;
        }
    }

    if (!uvQ && interUV)
    {
        uvQ = new short[2 * (size_t)allocated];
        if (uvQ)
        {
            for (unsigned i = 0; i < interCount; i++)
            {
                uvQ[i * 2 + 0] = inter[i].u;
                uvQ[i * 2 + 1] = inter[i].v;
            }
            uvQCount = interCount;
        }
    }

    if (!colour && interColour)
    {
        colour = new unsigned char[4 * (size_t)allocated];
        if (colour)
        {
            for (unsigned i = 0; i < interCount; i++)
            {
                const unsigned a = inter[i].argb;
                colour[i * 4 + 0] = (unsigned char)(a >> 16);
                colour[i * 4 + 1] = (unsigned char)(a >> 8);
                colour[i * 4 + 2] = (unsigned char)a;
                colour[i * 4 + 3] = (unsigned char)(a >> 24);
            }
        }
    }

    delete[] inter;
    inter = NULL;
    interCount = 0;
    interUV = false;
    interColour = false;
}

pddiPrimBufferStream* pvrPrimBuffer::Lock()
{
    // Locking a buffer that has already been built means its contents are
    // being replaced, which for anything drawn every frame makes the teardown
    // below pure loss.
    if (valid && inter)
        dynamic = true;

    if (!(dynamic && inter))
        DropInterleaved();

    total = 0;
    coordWritten = false;
    uvWritten = false;
    if (stream) stream->cur = 0;
    return stream;
}

void pvrPrimBuffer::ComputeBounds()
{
    if (!coord || total == 0)
        return;

    for (int a = 0; a < 3; a++)
    {
        bbMin[a] = bbMax[a] = coord[a];
    }

    for (unsigned v = 1; v < total; v++)
    {
        for (int a = 0; a < 3; a++)
        {
            const float f = coord[v * 3 + a];
            if (f < bbMin[a]) bbMin[a] = f;
            if (f > bbMax[a]) bbMax[a] = f;
        }
    }

    bbValid = true;
}

void pvrPrimBuffer::Unlock(pddiPrimBufferStream* s)
{
    (void)s;

    // Writing uvs takes RestoreUVs through DropInterleaved, so inter is gone
    // by then and this does not fire. Anything else that leaves the write
    // short has to tear the interleaved array down before the general path
    // rebuilds the positions, or the draw would keep reading the old ones.
    if (dynamic && inter)
    {
        if (coordWritten && !uvWritten && total == interCount)
        {
            RequantiseDynamic();
            valid = true;
            return;
        }

        DropInterleaved();
    }

    if (coordWritten)
        QuantiseCoords();

    // Quantising records the bounds itself; anything left in floats needs its
    // own pass so it can still be culled.
    if (coord && coordWritten)
        ComputeBounds();
    if (uvWritten)
        QuantiseUVs();
    valid = true;
}

unsigned char* pvrPrimBuffer::LockIndexBuffer()
{
    return (unsigned char*)indices;
}

void pvrPrimBuffer::UnlockIndexBuffer(int count)
{
    indexCount = count;
}

void pvrPrimBuffer::BuildRunRanges()
{
    if (runLo || !runs || !runCount || !indices)
        return;

    runLo = new unsigned short[runCount];
    runHi = new unsigned short[runCount];
    if (!runLo || !runHi)
    {
        delete[] runLo; runLo = NULL;
        delete[] runHi; runHi = NULL;
        return;
    }

    for (unsigned r = 0; r < runCount; r++)
    {
        const unsigned first = runs[r * 2 + 0];
        const unsigned count = runs[r * 2 + 1];

        unsigned lo = 0xFFFFu, hi = 0;
        for (unsigned k = first; k < first + count; k++)
        {
            const unsigned v = indices[k];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }

        runLo[r] = (unsigned short)(count ? lo : 0xFFFFu);
        runHi[r] = (unsigned short)(count ? hi : 0);
    }
}

void pvrPrimBuffer::SetRunList(const unsigned short* src, int count)
{
    delete[] runs;
    delete[] runLo;
    delete[] runHi;
    runs = NULL;
    runLo = NULL;
    runHi = NULL;
    runCount = 0;

    if (!src || count <= 0)
        return;

    runs = new unsigned short[2 * (size_t)count];
    if (!runs)
        return;

    memcpy(runs, src, (size_t)count * 2 * sizeof(unsigned short));
    runCount = (unsigned)count;
}

void pvrPrimBuffer::SetIndices(unsigned short* idx, int count)
{
    PDDIASSERT(count <= (int)indexCount);
    memcpy(indices, idx, (size_t)count * sizeof(unsigned short));
    valid = false;
}

void pvrPrimBuffer::Display(void)
{
    if (!valid || !context)
        return;
    // Default (no material info) = untextured white opaque.
    DisplayWithMaterial(NULL, 0);
}

void pvrPrimBuffer::DisplayWithMaterial(pvrMat* mat, unsigned pass)
{
    if (!valid || !context)
        return;

    pvrTextureEnv env{};
    if (mat)
        env = mat->GetTextureEnv((int)pass);
    else
        env = ((pvrMat*)context->GetDefaultShader())->GetTextureEnv(0);

    pvr_poly_hdr_t hdr;
    pvr_list_t list;
    context->BuildPolyHeader(env, hdr, list);

    const int slot = ListSlot(list);
    if (slot < 0 || context->colourWriteOff)
        return;

    pvrDrawCmd cmd;
    cmd.hdr = hdr;
    cmd.hdrKey = pvrHdrSortKey(hdr);
    // env.lit is what the shader asked for. Carrying normals is not consent to
    // be lit: the flag defaults off and only a material that turns it on wants
    // the pass.
    const bool wantsLight = env.lit && (normalQ || interNormal);

    cmd.lightSet = wantsLight ? pvrPoolLightSet(context) : ~0u;
#if defined SRR2_DC_PROFILER
    if (normalQ || interNormal)
    {
        s_recNormalDraws++;
        s_litFlagOn += env.lit ? 1u : 0u;
        s_litFlagOff += env.lit ? 0u : 1u;
    }
#endif
    if ((coordQ || inter) && !coord)
        context->BuildTransform(qScale, qBias, &cmd.xform);
    else
        cmd.xform = context->GetViewProj();
    cmd.vp = context->GetViewportMap();
    cmd.buffer = this;
    AddRef();
    cmd.immFirst = 0;
    cmd.immCount = 0;
    cmd.immPrim = primType;
    cmd.argb = (unsigned)env.diffuse;
    cmd.uScale = GetUStrideScale(env.texture);
    cmd.cull = context->GetCurrentCull();
    pvrFillClip(cmd, context);
    pvrFoldViewport(cmd.xform, cmd.vp);

    s_drawCmds[slot].push_back(cmd);
}

// Walk an index list emitting native strips, breaking a run only where it has
// to: at a degenerate join, or -- when CheckVis is set -- at a triangle that
// meets the near plane. Terminating every triangle with EOL instead costs
// three packets where a continued strip costs one, which is what made the
// clipped paths dominate submission.
//
// Backface rejection is left to the hardware; the poly header already carries
// the cull mode, so testing it here only duplicated it.
// The interleaved layout is the common case, and everything that selected
// between layouts inside the old loop -- which stream holds the uvs, whether
// there are per-vertex colours, whether the box is wholly in front -- is
// constant for the whole draw. Lifting all of it into template parameters
// leaves a loop whose body is only the work.
//
// XMTRX already holds the draw transform, and nothing here disturbs it: ftrv
// reads the back bank, and fsrra and the multiplies use the front one.
// Diffuse for up to four directional lights, one pass over the vertices with
// XMTRX holding the light directions instead of the view-projection. That is
// the whole reason this is its own pass: ftrv gives all four dot products for
// the price of one instruction, but only while the matrix is loaded.
//
// Runs before the transform pass, which reloads XMTRX for itself.
template <unsigned Lights>
static void LightInterleaved(pvr_vertex_t* pkt, const pvrInterVert* src,
                             unsigned n, const pvrLightSet& ls, unsigned argb)
{
    const float sc = 1.0f / 127.0f;

    // The material diffuse belongs in the product. Dropping it left the result
    // as ambient plus the raw light colours, which with three lights near full
    // white clamps at 255 for any normal facing any of them -- a textured mesh
    // then draws exactly as if it were never lit.
    const unsigned alpha = argb & 0xFF000000u;
    const float mr = (float)((argb >> 16) & 0xFFu) * (1.0f / 255.0f);
    const float mg = (float)((argb >>  8) & 0xFFu) * (1.0f / 255.0f);
    const float mb = (float)( argb        & 0xFFu) * (1.0f / 255.0f);

    for (unsigned i = 0; i < n; i++)
    {
        dcache_pref_block(&src[i + 2]);

        const shz_vec4_t d = shz_xmtrx_transform_vec4(
            shz_vec4_init((float)src[i].n[0] * sc,
                          (float)src[i].n[1] * sc,
                          (float)src[i].n[2] * sc, 0.0f));

        float r = ls.ambR, g = ls.ambG, b = ls.ambB;

        if (Lights > 0 && d.x > 0.0f) { r += d.x * ls.r[0]; g += d.x * ls.g[0]; b += d.x * ls.b[0]; }
        if (Lights > 1 && d.y > 0.0f) { r += d.y * ls.r[1]; g += d.y * ls.g[1]; b += d.y * ls.b[1]; }
        if (Lights > 2 && d.z > 0.0f) { r += d.z * ls.r[2]; g += d.z * ls.g[2]; b += d.z * ls.b[2]; }
        if (Lights > 3 && d.w > 0.0f) { r += d.w * ls.r[3]; g += d.w * ls.g[3]; b += d.w * ls.b[3]; }

        r *= mr; g *= mg; b *= mb;

#if defined SRR2_DC_PROFILER
        if (r >= 255.0f && g >= 255.0f && b >= 255.0f) s_litSat++;
        if (!src[i].n[0] && !src[i].n[1] && !src[i].n[2]) s_litZeroN++;
        {
            float best = 0.0f;
            if (Lights > 0 && d.x > best) best = d.x;
            if (Lights > 1 && d.y > best) best = d.y;
            if (Lights > 2 && d.z > best) best = d.z;
            if (Lights > 3 && d.w > best) best = d.w;
            s_dotSum += best;
            s_dotCount++;
        }
#endif
        if (r > 255.0f) r = 255.0f;
        if (g > 255.0f) g = 255.0f;
        if (b > 255.0f) b = 255.0f;

        pkt[i].argb = alpha
                    | ((unsigned)(int)r << 16)
                    | ((unsigned)(int)g << 8)
                    |  (unsigned)(int)b;
    }
}

template <unsigned Lights>
static void LightPacked(pvr_vertex_t* pkt, const signed char* nrm,
                        unsigned n, const pvrLightSet& ls, unsigned argb)
{
    const float sc = 1.0f / 127.0f;

    const unsigned alpha = argb & 0xFF000000u;
    const float mr = (float)((argb >> 16) & 0xFFu) * (1.0f / 255.0f);
    const float mg = (float)((argb >>  8) & 0xFFu) * (1.0f / 255.0f);
    const float mb = (float)( argb        & 0xFFu) * (1.0f / 255.0f);

    for (unsigned i = 0; i < n; i++)
    {
        const shz_vec4_t d = shz_xmtrx_transform_vec4(
            shz_vec4_init((float)nrm[i * 3 + 0] * sc,
                          (float)nrm[i * 3 + 1] * sc,
                          (float)nrm[i * 3 + 2] * sc, 0.0f));

        float r = ls.ambR, g = ls.ambG, b = ls.ambB;

        if (Lights > 0 && d.x > 0.0f) { r += d.x * ls.r[0]; g += d.x * ls.g[0]; b += d.x * ls.b[0]; }
        if (Lights > 1 && d.y > 0.0f) { r += d.y * ls.r[1]; g += d.y * ls.g[1]; b += d.y * ls.b[1]; }
        if (Lights > 2 && d.z > 0.0f) { r += d.z * ls.r[2]; g += d.z * ls.g[2]; b += d.z * ls.b[2]; }
        if (Lights > 3 && d.w > 0.0f) { r += d.w * ls.r[3]; g += d.w * ls.g[3]; b += d.w * ls.b[3]; }

        r *= mr; g *= mg; b *= mb;

        if (r > 255.0f) r = 255.0f;
        if (g > 255.0f) g = 255.0f;
        if (b > 255.0f) b = 255.0f;

        pkt[i].argb = alpha
                    | ((unsigned)(int)r << 16)
                    | ((unsigned)(int)g << 8)
                    |  (unsigned)(int)b;
    }
}

static void RunLightPassPacked(pvr_vertex_t* pkt, const signed char* nrm, unsigned n,
                               const pvrLightSet& ls, unsigned argb)
{
#if defined SRR2_DC_PROFILER
    s_matRGB = argb & 0x00FFFFFFu;
#endif
    const unsigned alpha = argb;

    shz_xmtrx_load_4x4(&ls.dir);

    switch (ls.count)
    {
        case 0:  LightPacked<0>(pkt, nrm, n, ls, alpha); break;
        case 1:  LightPacked<1>(pkt, nrm, n, ls, alpha); break;
        case 2:  LightPacked<2>(pkt, nrm, n, ls, alpha); break;
        case 3:  LightPacked<3>(pkt, nrm, n, ls, alpha); break;
        default: LightPacked<4>(pkt, nrm, n, ls, alpha); break;
    }
}

static void RunLightPass(pvr_vertex_t* pkt, const pvrInterVert* src, unsigned n,
                         const pvrLightSet& ls, unsigned argb)
{
#if defined SRR2_DC_PROFILER
    s_matRGB = argb & 0x00FFFFFFu;
#endif
    const unsigned alpha = argb;

    shz_xmtrx_load_4x4(&ls.dir);

    switch (ls.count)
    {
        case 0:  LightInterleaved<0>(pkt, src, n, ls, alpha); break;
        case 1:  LightInterleaved<1>(pkt, src, n, ls, alpha); break;
        case 2:  LightInterleaved<2>(pkt, src, n, ls, alpha); break;
        case 3:  LightInterleaved<3>(pkt, src, n, ls, alpha); break;
        default: LightInterleaved<4>(pkt, src, n, ls, alpha); break;
    }
}

template <bool InFront, int ArgbMode>   // 0 default, 1 from record, 2 leave alone
static void FillInterleaved(pvr_vertex_t* pkt, unsigned char* vis,
                            const pvrInterVert* src, unsigned n,
                            unsigned* visAll, unsigned argbDefault,
                            float uMul, float uAdd, float vMul, float vAdd)
{
    unsigned all = 1;

    if (n == 0)
    {
        *visAll = 1;
        return;
    }

    float ax = (float)src[0].x;
    float ay = (float)src[0].y;
    float az = (float)src[0].z;

    for (unsigned i = 0; i < n; i++)
    {
        dcache_pref_block(&src[i + 2]);

        const shz_vec4_t p = shz_xmtrx_transform_vec4(
            shz_vec4_init(ax, ay, az, 1.0f));

        const pvrInterVert& nx = src[i + 1];
        ax = (float)nx.x;
        ay = (float)nx.y;
        az = (float)nx.z;

        pvr_vertex_t& v = pkt[i];
        v.flags = PVR_CMD_VERTEX;

        if (InFront)
        {
            const float iw = shz_invf_fsrra(p.w);
            v.x = p.x * iw;
            v.y = p.y * iw;
            v.z = iw;
        }
        else
        {
            const unsigned ok = (p.w >= p.z + PVR_NEAR_CLIP_EPSILON) ? 1u : 0u;
            vis[i] = (unsigned char)ok;
            all &= ok;

            if (ok)
            {
                const float iw = shz_invf_fsrra(p.w);
                v.x = p.x * iw;
                v.y = p.y * iw;
                v.z = iw;
            }
        }

        v.u = (float)src[i].u * uMul + uAdd;
        v.v = (float)src[i].v * vMul + vAdd;

        if (ArgbMode == 0)      v.argb = argbDefault;
        else if (ArgbMode == 1) v.argb = src[i].argb;

        v.oargb = 0;
    }

    if (InFront)
    {
        memset(vis, 1, n);
    }

    *visAll = all;
}

// The converter knows where each strip begins and ends, so when it has told
// us there is nothing to hunt for: no degenerate tests, and the last index of
// a run is known rather than discovered.
template <bool CheckVis, typename ClipPosFn>
static void EmitRunList(bool oix, pvr_vertex_t* pkt, const unsigned char* vis,
                        const unsigned short* indices, const unsigned short* runs,
                        unsigned runCount, const pvrViewportMap& vp, ClipPosFn clipPos)
{
    for (unsigned r = 0; r < runCount; r++)
    {
        const unsigned first = runs[r * 2 + 0];
        const unsigned count = runs[r * 2 + 1];

        if (count < 3)
            continue;

        const unsigned end = first + count - 2;   // one past the last triangle


        // Classify the run before walking it. A strip wholly behind the near
        // plane costs one byte load per index to reject, against assembling
        // and winding every triangle in it -- and on a draw whose box
        // straddles, most of what it contains is behind the camera.
        if (CheckVis)
        {
            unsigned anyVis = 0, allVis = 1;

            for (unsigned k = first; k < first + count; k++)
            {
                const unsigned v = vis[indices[k]];
                anyVis |= v;
                allVis &= v;
            }

            if (!anyVis)
            {
#if defined SRR2_DC_PROFILER
                s_deadTris += count - 2;
                s_runDead++;
#endif
                continue;
            }
#if defined SRR2_DC_PROFILER
            if (allVis) { s_runAll++;   s_runAllV   += count; }
            else        { s_runMixed++; s_runMixedV += count; }
#endif

            // The scan already knows whether anything in the run is hidden.
            // Walking the triangles again to rediscover that costs three index
            // loads and three visibility loads apiece, and then the run
            // extension walks them a third time -- about four times the loads
            // of a straight submit, on runs that are almost always wholly
            // visible. Take the plain path when the scan says nothing is cut.
            if (allVis)
            {
                const unsigned last = end + 1;

                if (first & 1u)
                    SubmitVert(oix, &pkt[indices[first]], PVR_CMD_VERTEX);

                for (unsigned k = first; k <= last; k++)
                    SubmitVert(oix, &pkt[indices[k]],
                               (k == last) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
#if defined SRR2_DC_PROFILER
                s_stripTris += count - 2;
#endif
                continue;
            }
        }

        unsigned i = first;
        while (i < end)
        {
            if (CheckVis)
            {
                const unsigned a = indices[i], b = indices[i + 1], c = indices[i + 2];

                if (!(vis[a] && vis[b] && vis[c]))
                {
                    if (vis[a] || vis[b] || vis[c])
                    {
#if defined SRR2_DC_PROFILER
                        s_clipTris++;
#endif
                        const unsigned odd = i & 1u;
                        const unsigned ix[3] = { a, odd ? c : b, odd ? b : c };

                        pvrClipVert tri[3];
                        for (int k = 0; k < 3; k++)
                        {
                            tri[k].pos  = clipPos(ix[k]);
                            tri[k].u    = pkt[ix[k]].u;
                            tri[k].v    = pkt[ix[k]].v;
                            tri[k].argb = pkt[ix[k]].argb;
                        }
                        ClipAndSubmitTriangle(vp, tri);
                    }
#if defined SRR2_DC_PROFILER
                    else
                    {
                        s_deadTris++;
                    }
#endif
                    i++;
                    continue;
                }
            }

            unsigned j = i + 1;
            if (CheckVis)
            {
                while (j < end)
                {
                    const unsigned x = indices[j], y = indices[j + 1], z = indices[j + 2];
                    if (!(vis[x] && vis[y] && vis[z]))
                        break;
                    j++;
                }
            }
            else
            {
                j = end;
            }

            const unsigned last = j + 1;

            if (i & 1u)
                SubmitVert(oix, &pkt[indices[i]], PVR_CMD_VERTEX);

            for (unsigned k = i; k <= last; k++)
            {
                SubmitVert(oix, &pkt[indices[k]],
                           (k == last) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
            }

#if defined SRR2_DC_PROFILER
            if (CheckVis)
                s_stripTris += j - i;
#endif
            i = j;
        }
    }
}

template <bool CheckVis, bool HasIdx, typename ClipPosFn>
static void EmitStripRuns(bool oix, pvr_vertex_t* pkt, const unsigned char* vis,
                          const unsigned short* indices, unsigned n,
                          const pvrViewportMap& vp, ClipPosFn clipPos)
{
    unsigned i = 0;

    while (i + 2 < n)
    {
#if defined SRR2_DC_PROFILER
        if (CheckVis)
            s_clipIters++;
#endif
        const unsigned a = HasIdx ? indices[i]     : i;
        const unsigned b = HasIdx ? indices[i + 1] : i + 1;
        const unsigned c = HasIdx ? indices[i + 2] : i + 2;

        if (a == b || b == c || a == c)
        {
            i++;
            continue;
        }

        if (CheckVis && !(vis[a] && vis[b] && vis[c]))
        {
            // Wholly behind the plane contributes nothing; straddling goes to
            // the clipper on its own, and the strip picks up after it.
            if (vis[a] || vis[b] || vis[c])
            {
#if defined SRR2_DC_PROFILER
                s_clipTris++;
#endif
                const unsigned odd = i & 1u;
                const unsigned ix[3] = { a, odd ? c : b, odd ? b : c };

                pvrClipVert tri[3];
                for (int k = 0; k < 3; k++)
                {
                    tri[k].pos  = clipPos(ix[k]);
                    tri[k].u    = pkt[ix[k]].u;
                    tri[k].v    = pkt[ix[k]].v;
                    tri[k].argb = pkt[ix[k]].argb;
                }
#if defined SRR2_DC_PROFILER
                const uint64_t ct_ = timer_us_gettime64();
#endif
                ClipAndSubmitTriangle(vp, tri);
#if defined SRR2_DC_PROFILER
                s_phClipTri += timer_us_gettime64() - ct_;
#endif
            }
#if defined SRR2_DC_PROFILER
            else
            {
                s_deadTris++;
            }
#endif
            i++;
            continue;
        }

        unsigned j = i + 1;
        while (j + 2 < n)
        {
            const unsigned x = HasIdx ? indices[j]     : j;
            const unsigned y = HasIdx ? indices[j + 1] : j + 1;
            const unsigned z = HasIdx ? indices[j + 2] : j + 2;

            if (x == y || y == z || x == z)
                break;
            if (CheckVis && !(vis[x] && vis[y] && vis[z]))
                break;
            j++;
        }

        const unsigned last = j + 1;

        if (i & 1u)
            SubmitVert(oix, &pkt[HasIdx ? indices[i] : i], PVR_CMD_VERTEX);

        for (unsigned k = i; k <= last; k++)
        {
            SubmitVert(oix, &pkt[HasIdx ? indices[k] : k],
                       (k == last) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX);
        }

#if defined SRR2_DC_PVR_TRACE
        s_tris += j - i;
#endif
#if defined SRR2_DC_PROFILER
        if (CheckVis)
            s_stripTris += j - i;
#endif
        i = j;
    }
}

void pvrPrimBuffer::SubmitDeferred(const pvrDrawCmd& cmd)
{
    PH_BEGIN();

    BuildInterleaved();

    const float uScale = cmd.uScale;
    const bool flipV = true;

    pvrSubmitClip(cmd);
    pvrSubmitHeader(cmd.hdr);

    pvrLoadXform(cmd.xform);

    const float uMul = uvScale[0] * uScale;
    const float uAdd = uvBias[0] * uScale;
    const float vMul = flipV ? -uvScale[1] : uvScale[1];
    const float vAdd = flipV ? (1.0f - uvBias[1]) : uvBias[1];

    const pvrViewportMap vp = cmd.vp;

    unsigned vcount = inter ? interCount : (coordQ ? coordQCount : total);
    if (vcount == 0)
        vcount = allocated;

    auto clipPos = [&](unsigned i) -> shz_vec4_t
    {
        if (inter)
            return shz_xmtrx_transform_vec4(
                shz_vec4_init((float)inter[i].x, (float)inter[i].y,
                              (float)inter[i].z, 1.0f));
        return coord
            ? shz_xmtrx_transform_vec4(
                  shz_vec4_init(coord[i * 3 + 0], coord[i * 3 + 1], coord[i * 3 + 2], 1.0f))
            : shz_xmtrx_transform_vec4(
                  shz_vec4_init((float)coordQ[i * 3 + 0], (float)coordQ[i * 3 + 1],
                                (float)coordQ[i * 3 + 2], 1.0f));
    };

    // Culling and the packet path only need positions and a bounding box --
    // neither cares whether the positions were quantised. Skinned characters
    // and vehicles keep float coords, and used to get neither.
    if (bbValid && (inter || coordQ || coord))
    {
        float lo[3], hi[3];

        if ((coordQ || inter) && !coord)
        {
            // BuildTransform folded the dequantisation into cmd.xform, so in
            // that space the box corners are the int16 limits.
            lo[0] = lo[1] = lo[2] = -32767.0f;
            hi[0] = hi[1] = hi[2] =  32767.0f;
        }
        else
        {
            for (int a = 0; a < 3; a++)
            {
                lo[a] = bbMin[a];
                hi[a] = bbMax[a];
            }
        }

        float boxArea = 0.0f;
        float boxNear = 0.0f;
        const pvrBoxClass box = pvrClassifyBox(vp, lo, hi, &boxArea, &boxNear);

        if (box == kBoxCulled || boxArea < s_minScreenArea
            || (s_maxDrawDepth > 0.0f && boxNear > s_maxDrawDepth))
        {
#if defined SRR2_DC_PROFILER
            s_boxCulled++;
            if (box != kBoxCulled && boxArea >= s_minScreenArea) s_distCulled++;
#endif
            PH_MARK(s_phSetup);
            return;
        }

#if defined SRR2_DC_PROFILER
        // Vertices by how far away the draw is. Decides whether a draw distance
        // would reach the geometry that actually costs, or whether the cost is
        // near the camera where only lower detail could help.
        {
            static const float kEdge[SRR_DEPTH_BINS - 1] =
                { 25.0f, 50.0f, 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f };
            unsigned b = SRR_DEPTH_BINS - 1;
            for (unsigned e = 0; e < SRR_DEPTH_BINS - 1; e++)
            {
                if (boxNear < kEdge[e]) { b = e; break; }
            }
            s_depthVerts[b] += (unsigned)vcount;
            s_depthDraws[b]++;
        }
#endif

        // One pass over the unique vertices building finished TA packets, then
        // emission by index. A vertex referenced by several indices is
        // transformed once, not once per reference.
#if defined SRR2_DC_PROFILER
        if (primType != PDDI_PRIM_TRISTRIP)                    s_missPrim++;
        else if (!indexCount || !indices || indexCount < 3)    s_missIdx++;
        else if (!(vcount > 0 && s_vtxPkt && s_vtxVis))        s_missOther++;
#endif
        if (primType == PDDI_PRIM_TRISTRIP && indexCount && indices && indexCount >= 3
            && vcount > 0 && pvrReserveVtxCache(vcount) && s_vtxPkt && s_vtxVis)
        {
            const bool oix = (pvrOixWindow != NULL) && ((unsigned)vcount <= PVR_OIX_VERTS);
#if defined SRR2_DC_PROFILER
            if (oix) { s_oixDraws++; s_oixVerts += (unsigned)vcount; }
            else     { s_sqDraws++;  s_sqVerts  += (unsigned)vcount; }
#endif
            pvr_vertex_t* const pkt = oix ? (pvr_vertex_t*)pvrOixWindow : s_vtxPkt;
            const bool inFront = (box == kBoxInFront);
            unsigned visAll = 1;
#if defined SRR2_DC_PROFILER
            s_vtxPerFrame += indexCount;
#endif
#if defined SRR2_DC_PVR_TRACE
            s_vtxXform += vcount;
#endif
#if defined SRR2_DC_PROFILER
            s_fusedDraws++;
#endif
            PH_MARK(s_phSetup);

#if defined SRR2_DC_PROFILER
            s_vtxXformed += vcount;
#endif
            if (inter)
            {
                int argbMode = interColour ? 1 : 0;

                if (interNormal && cmd.lightSet < s_lightSets.size()
                    && s_lightSets[cmd.lightSet].count > 0)
                {
#if defined SRR2_DC_PROFILER
                    s_litVerts += vcount;
                    s_litDraws++;
                    s_lightCount = s_lightSets[cmd.lightSet].count;
#endif
                    RunLightPass(pkt, inter, vcount, s_lightSets[cmd.lightSet], cmd.argb);
                    pvrInvalidateXform();
                    pvrLoadXform(cmd.xform);
                    argbMode = 2;
                }

                if (inFront)
                {
                    if (argbMode == 1)      FillInterleaved<true, 1>(pkt, s_vtxVis, inter, vcount, &visAll, cmd.argb, uMul, uAdd, vMul, vAdd);
                    else if (argbMode == 2) FillInterleaved<true, 2>(pkt, s_vtxVis, inter, vcount, &visAll, cmd.argb, uMul, uAdd, vMul, vAdd);
                    else                    FillInterleaved<true, 0>(pkt, s_vtxVis, inter, vcount, &visAll, cmd.argb, uMul, uAdd, vMul, vAdd);
                }
                else
                {
                    if (argbMode == 1)      FillInterleaved<false, 1>(pkt, s_vtxVis, inter, vcount, &visAll, cmd.argb, uMul, uAdd, vMul, vAdd);
                    else if (argbMode == 2) FillInterleaved<false, 2>(pkt, s_vtxVis, inter, vcount, &visAll, cmd.argb, uMul, uAdd, vMul, vAdd);
                    else                    FillInterleaved<false, 0>(pkt, s_vtxVis, inter, vcount, &visAll, cmd.argb, uMul, uAdd, vMul, vAdd);
                }
            }
            else
            {
            bool lit = false;
            if (!inter && normalQ && cmd.lightSet < s_lightSets.size()
                && s_lightSets[cmd.lightSet].count > 0)
            {
#if defined SRR2_DC_PROFILER
                s_litVerts += vcount;
                s_litDraws++;
                s_lightCount = s_lightSets[cmd.lightSet].count;
#endif
                RunLightPassPacked(pkt, normalQ, vcount, s_lightSets[cmd.lightSet], cmd.argb);
                pvrInvalidateXform();
                pvrLoadXform(cmd.xform);
                lit = true;
            }

            for (unsigned i = 0; i < vcount; i++)
            {
                const shz_vec4_t p = clipPos(i);

                const unsigned vis = inFront
                    ? 1u
                    : ((p.w >= p.z + PVR_NEAR_CLIP_EPSILON) ? 1u : 0u);

                s_vtxVis[i] = (unsigned char)vis;
                visAll &= vis;

                pvr_vertex_t& v = pkt[i];
                v.flags = PVR_CMD_VERTEX;

                if (vis)
                {
                    const float iw = shz_invf_fsrra(p.w);
                    v.x = p.x * iw;
                    v.y = p.y * iw;
                    v.z = iw;
                }

                if (inter)
                {
                    v.u = (float)inter[i].u * uMul + uAdd;
                    v.v = (float)inter[i].v * vMul + vAdd;
                }
                else if (uvQ)
                {
                    v.u = (float)uvQ[i * 2 + 0] * uMul + uAdd;
                    v.v = (float)uvQ[i * 2 + 1] * vMul + vAdd;
                }
                else if (uv)
                {
                    v.u = uv[i * 2 + 0] * uScale;
                    v.v = flipV ? (1.0f - uv[i * 2 + 1]) : uv[i * 2 + 1];
                }
                else
                {
                    v.u = 0.0f;
                    v.v = 0.0f;
                }

                if (lit)
                {
                    // already written by the light pass
                }
                else if (inter)
                {
                    v.argb = interColour ? inter[i].argb : cmd.argb;
                }
                else if (colour)
                {
                    const unsigned char* cc = &colour[i * 4];
                    v.argb = ((uint32_t)cc[3] << 24) | ((uint32_t)cc[0] << 16)
                           | ((uint32_t)cc[1] << 8) | (uint32_t)cc[2];
                }
                else
                {
                    v.argb = cmd.argb;
                }

                v.oargb = 0;
            }
            }

#if defined SRR2_DC_PROFILER
            if (inFront) s_xformVertsFront    += (unsigned)vcount;
            else         s_xformVertsStraddle += (unsigned)vcount;
#endif
            PH_MARK2(s_phXform, inFront ? s_phXformFront : s_phXformStraddle);

            if (visAll)
            {
#if defined SRR2_DC_PROFILER
                const unsigned e0_ = s_vtxEmitted;
#endif
                if (runs)
                    EmitRunList<false>(oix, pkt, s_vtxVis, indices, runs, runCount, vp, clipPos);
                else
                    EmitStripRuns<false, true>(oix, pkt, s_vtxVis, indices, indexCount, vp, clipPos);
#if defined SRR2_DC_PROFILER
                s_emitVertsPlain += s_vtxEmitted - e0_;
#endif
                PH_MARK2(s_phEmit, s_phEmitPlain);
                return;
            }

            // Some vertices sit behind the near plane. Still emit from the
            // packets already built -- falling through to the generic path
            // would transform this meshlet a second time.
#if defined SRR2_DC_PROFILER
            s_clipDrawsFast++;
#endif
            // The box straddles, so the whole draw can still be behind the
            // camera. One byte load per vertex settles that, against walking
            // and winding every triangle only to discard it.
            {
                unsigned anyVis = 0;

                for (int vi = 0; vi < vcount; vi++)
                    anyVis |= s_vtxVis[vi];

                if (!anyVis)
                {
                    PH_MARK(s_phClip);
                    return;
                }
            }

#if defined SRR2_DC_PROFILER
            const unsigned e0_ = s_vtxEmitted;
#endif
            if (runs)
                EmitRunList<true>(oix, pkt, s_vtxVis, indices, runs, runCount, vp, clipPos);
            else
                EmitStripRuns<true, true>(oix, pkt, s_vtxVis, indices, indexCount, vp, clipPos);
#if defined SRR2_DC_PROFILER
            s_emitVertsVis += s_vtxEmitted - e0_;
#endif

            // This walk is emission, not clipping: 288 triangles a frame
            // reach the clipper while this pushes tens of thousands of
            // vertices. Charging it to clip hid where the time really goes.
            PH_MARK2(s_phEmit, s_phEmitVis);
            return;
        }

        PH_MARK(s_phSetup);
    }

    if (vcount > 0 && (coord || coordQ || inter) && pvrReserveVtxCache(vcount))
    {
        unsigned visAll = 1;
#if defined SRR2_DC_PVR_TRACE
        s_vtxXform += vcount;
#endif

        for (unsigned i = 0; i < vcount; i++)
        {
            pvr_vertex_t& c = s_vtxPkt[i];

            const shz_vec4_t pos = clipPos(i);

            const unsigned vis = (pos.w >= pos.z + PVR_NEAR_CLIP_EPSILON) ? 1u : 0u;
            s_vtxVis[i] = (unsigned char)vis;
            visAll &= vis;

            c.flags = PVR_CMD_VERTEX;
            c.oargb = 0;

            if (vis)
            {
                const float iw = shz_invf_fsrra(pos.w);
                c.x = pos.x * iw;
                c.y = pos.y * iw;
                c.z = iw;
            }

            if (uv)
            {
                c.u = uv[i * 2 + 0] * uScale;
                c.v = flipV ? (1.0f - uv[i * 2 + 1]) : uv[i * 2 + 1];
            }
            else if (inter)
            {
                c.u = (float)inter[i].u * uMul + uAdd;
                c.v = (float)inter[i].v * vMul + vAdd;
            }
            else if (uvQ)
            {
                c.u = (float)uvQ[i * 2 + 0] * uMul + uAdd;
                c.v = (float)uvQ[i * 2 + 1] * vMul + vAdd;
            }
            else
            {
                c.u = 0.0f;
                c.v = 0.0f;
            }

            if (inter)
            {
                c.argb = interColour ? inter[i].argb : cmd.argb;
            }
            else if (colour)
            {
                const unsigned char* cc = &colour[i * 4];
                c.argb = ((uint32_t)cc[3] << 24) | ((uint32_t)cc[0] << 16)
                       | ((uint32_t)cc[1] << 8) | (uint32_t)cc[2];
            }
            else
            {
                c.argb = cmd.argb;
            }
        }

        PH_MARK(s_phXform);

        const unsigned short* idx = (indexCount && indices) ? indices : NULL;
        const unsigned n = idx ? indexCount : vcount;

        const unsigned allVis = visAll;

        if (primType == PDDI_PRIM_TRISTRIP && allVis && n >= 3)
        {
            // Same EOL run-splitting as the fused path: drop the exporter's
            // degenerate joins rather than feeding them to the TA.
            unsigned i = 0;
            if (idx)
                EmitStripRuns<false, true>(false, s_vtxPkt, s_vtxVis, idx, n, vp, clipPos);
            else
                EmitStripRuns<false, false>(false, s_vtxPkt, s_vtxVis, idx, n, vp, clipPos);
            PH_MARK(s_phEmit);
            return;
        }

        auto emitTri = [&](unsigned a, unsigned b, unsigned c)
        {
            const pvr_vertex_t& va = s_vtxPkt[a];
            const pvr_vertex_t& vb = s_vtxPkt[b];
            const pvr_vertex_t& vc = s_vtxPkt[c];
#if defined SRR2_DC_PVR_TRACE
            s_tris++;
#endif

            if (s_vtxVis[a] && s_vtxVis[b] && s_vtxVis[c])
            {
                SubmitPacket(va, PVR_CMD_VERTEX);
                SubmitPacket(vb, PVR_CMD_VERTEX);
                SubmitPacket(vc, PVR_CMD_VERTEX_EOL);
                return;
            }

            pvrClipVert tri[3];
            tri[0].pos = clipPos(a); tri[0].u = va.u; tri[0].v = va.v; tri[0].argb = va.argb;
            tri[1].pos = clipPos(b); tri[1].u = vb.u; tri[1].v = vb.v; tri[1].argb = vb.argb;
            tri[2].pos = clipPos(c); tri[2].u = vc.u; tri[2].v = vc.v; tri[2].argb = vc.argb;
            ClipAndSubmitTriangle(vp, tri);
        };

#if defined SRR2_DC_PROFILER
        s_clipDrawsGeneric++;

        const uint64_t genStart_ = timer_us_gettime64();
#endif
        if (primType == PDDI_PRIM_TRIANGLES)
        {
            for (unsigned i = 0; i + 2 < n; i += 3)
                emitTri(idx ? idx[i] : i, idx ? idx[i + 1] : i + 1, idx ? idx[i + 2] : i + 2);
        }
        else if (primType == PDDI_PRIM_TRISTRIP)
        {
            if (idx)
                EmitStripRuns<true, true>(false, s_vtxPkt, s_vtxVis, idx, n, vp, clipPos);
            else
                EmitStripRuns<true, false>(false, s_vtxPkt, s_vtxVis, idx, n, vp, clipPos);
        }
#if defined SRR2_DC_PROFILER
        s_genIters  += n;
        s_phGenWalk += timer_us_gettime64() - genStart_;
#endif
        PH_MARK(s_phClip);
        return;
    }

    auto submitTriIdx = [&](int i0, int i1, int i2)
    {
        const int idxs[3] = { i0, i1, i2 };
        pvrClipVert tri[3];

        for (int k = 0; k < 3; k++)
        {
            const int vi = idxs[k];
            tri[k].pos = coord
                ? shz_xmtrx_transform_vec4(
                      shz_vec4_init(coord[vi * 3 + 0], coord[vi * 3 + 1], coord[vi * 3 + 2], 1.0f))
                : inter
                ? shz_xmtrx_transform_vec4(
                      shz_vec4_init((float)inter[vi].x, (float)inter[vi].y,
                                    (float)inter[vi].z, 1.0f))
                : shz_xmtrx_transform_vec4(
                      shz_vec4_init((float)coordQ[vi * 3 + 0], (float)coordQ[vi * 3 + 1],
                                    (float)coordQ[vi * 3 + 2], 1.0f));

            if (uv)
            {
                tri[k].u = uv[vi * 2 + 0] * uScale;
                tri[k].v = flipV ? (1.0f - uv[vi * 2 + 1]) : uv[vi * 2 + 1];
            }
            else if (inter)
            {
                tri[k].u = (float)inter[vi].u * uMul + uAdd;
                tri[k].v = (float)inter[vi].v * vMul + vAdd;
            }
            else if (uvQ)
            {
                tri[k].u = (float)uvQ[vi * 2 + 0] * uMul + uAdd;
                tri[k].v = (float)uvQ[vi * 2 + 1] * vMul + vAdd;
            }
            else
            {
                tri[k].u = 0.0f;
                tri[k].v = 0.0f;
            }

            if (inter)
            {
                tri[k].argb = interColour ? inter[vi].argb : cmd.argb;
            }
            else if (colour)
            {
                const unsigned char* c = &colour[vi * 4];
                tri[k].argb = ((uint32_t)c[3] << 24) | ((uint32_t)c[0] << 16)
                            | ((uint32_t)c[1] << 8) | (uint32_t)c[2];
            }
            else
            {
                tri[k].argb = cmd.argb;
            }
        }

#ifdef RAD_DC_TRACE_VERTS
        {
            const shz_vec4_t c[3] = { tri[0].pos, tri[1].pos, tri[2].pos };
            const float uu[3] = { tri[0].u, tri[1].u, tri[2].u };
            const float vv[3] = { tri[0].v, tri[1].v, tri[2].v };
            TraceTri("PB", c, uu, vv, vp.oy, vp.hh);
        }
#endif
        ClipAndSubmitTriangle(vp, tri);
    };

    if (indexCount && indices)
    {
        if (primType == PDDI_PRIM_TRIANGLES)
        {
            for (unsigned i = 0; i + 2 < indexCount; i += 3)
                submitTriIdx(indices[i], indices[i + 1], indices[i + 2]);
        }
        else if (primType == PDDI_PRIM_TRISTRIP)
        {
            for (unsigned i = 0; i + 2 < indexCount; ++i)
            {
                const bool odd = (i & 1u) != 0;
                const int a = indices[i];
                const int b = indices[i + 1];
                const int c = indices[i + 2];
                submitTriIdx(a, odd ? c : b, odd ? b : c);
            }
        }
    }
    else
    {
        if (primType == PDDI_PRIM_TRIANGLES)
        {
            for (unsigned i = 0; i + 2 < total; i += 3)
                submitTriIdx((int)i, (int)i + 1, (int)i + 2);
        }
        else if (primType == PDDI_PRIM_TRISTRIP)
        {
            for (unsigned i = 0; i + 2 < total; ++i)
            {
                const bool odd = (i & 1u) != 0;
                submitTriIdx((int)i, odd ? (int)i + 2 : (int)i + 1, odd ? (int)i + 1 : (int)i + 2);
            }
        }
    }

    PH_MARK(s_phClip);
}
