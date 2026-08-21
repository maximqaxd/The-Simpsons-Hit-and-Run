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
extern "C" unsigned pvrLastBoxCulled( void )  { return s_lastBoxCulled; }
extern "C" unsigned pvrLastFusedDraws( void ) { return s_lastFusedDraws; }
#define PH_BEGIN()  uint64_t phMark_ = timer_us_gettime64()
#define PH_MARK(b)  do { const uint64_t n_ = timer_us_gettime64(); \
                         (b) += n_ - phMark_; phMark_ = n_; } while (0)
#else
#define PH_BEGIN()  do {} while (0)
#define PH_MARK(b)  do {} while (0)
#endif
static unsigned s_lastVtxBytes = 0;

extern "C" unsigned pvrLastDrawCount( void )   { return s_lastDraws; }
extern "C" unsigned pvrLastVertexCount( void ) { return s_lastVtxBytes / 32u; }

void pvrContext::BeginFrame()
{
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
    pddiBaseContext::EndFrame();
    FlushDeferredLists();
#if defined SRR2_DC_PVR_TRACE
    const uint64_t finishStart = timer_us_gettime64();
#endif
    pvr_scene_finish();
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
};

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

static pvrBoxClass pvrClassifyBox(const pvrViewportMap& vp, const float* lo,
                                  const float* hi, float* areaOut)
{
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
    // Not implemented yet; keep state tracking in base.
    pddiBaseContext::SetScissor(rect);
    (void)rect;
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
    unsigned char   pad;
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
    cxt.gen.fog_type = PVR_FOG_DISABLE;

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
        if (reserveVerts > 0)
            s_immVerts.reserve(s_immVerts.size() + (size_t)reserveVerts);

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
        if (slot < 0)
        {
            s_immVerts.resize(immFirst);
            return;
        }

        pvrDrawCmd cmd;
        cmd.hdr = hdr;
        cmd.xform = ctx->GetViewProj();
        cmd.vp = ctx->GetViewportMap();
        cmd.buffer = NULL;
        cmd.immFirst = immFirst;
        cmd.immCount = immCount;
        cmd.immPrim = primType;
        cmd.argb = 0xffffffffu;
        cmd.uScale = GetUStrideScale(env.texture);
        cmd.cull = PVR_CULLING_NONE;
        pvrFoldViewport(cmd.xform, cmd.vp);

        s_drawCmds[slot].push_back(cmd);
    }

    static void SubmitDeferred(const pvrDrawCmd& cmd)
    {
        {
            pvr_poly_hdr_t* h = (pvr_poly_hdr_t*)pvr_dr_target();
            *h = cmd.hdr;
            pvr_dr_commit(h);
        }

        shz_xmtrx_load_4x4(&cmd.xform);

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

static void pvrResetDeferredLists()
{
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
    s_lastVtxPerFrame = s_vtxPerFrame;
    s_lastVtxEmitted = s_vtxEmitted;
    s_vtxEmitted = 0;
    s_lastSetup = s_phSetup; s_phSetup = 0;
    s_lastXform = s_phXform; s_phXform = 0;
    s_lastEmit  = s_phEmit;  s_phEmit  = 0;
    s_lastClip  = s_phClip;  s_phClip  = 0;
    s_lastImm   = s_phImm;   s_phImm   = 0;
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
    s_boxCulled = 0;
    s_fusedDraws = 0;
    s_vtxPerFrame = 0;
#endif
#if defined SRR2_DC_PVR_TRACE
    const uint64_t replayStart = timer_us_gettime64();
#endif

    pvrOixEnter();

    for (int i = 0; i < 3; i++)
    {
        const std::vector<pvrDrawCmd>& cmds = s_drawCmds[i];
        if (cmds.empty())
            continue;

        if (pvr_list_begin(kListOrder[i]) < 0)
            continue;

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

void pvrContext::SetColourWrite(bool red, bool green, bool blue, bool alpha)
{
    pddiBaseContext::SetColourWrite(red, green, blue, alpha);
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
    // PVR: cxt.gen.fog_type
}

void pvrContext::SetFog(pddiColour colour, float start, float end)
{
    pddiBaseContext::SetFog(colour, start, end);
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

void pvrContext::SetupHardwareLight(int)
{
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
        if (!buffer->normal) return;
        buffer->normal[cur * 3 + 0] = x;
        buffer->normal[cur * 3 + 1] = y;
        buffer->normal[cur * 3 + 2] = z;
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
    , coordQCount(0)
    , bbValid(false)

    , uvQ(NULL)
    , uvWritten(false)
    , uvQCount(0)
    , normal(NULL)
    , uv(NULL)
    , colour(NULL)
    , runs(NULL)
    , runCount(0)
    , inter(NULL)
    , interCount(0)
    , interUV(false)
    , interColour(false)
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

    // Normals are never read back: this backend has no hardware lighting, so
    // the stream's Normal() writes went into 12 bytes a vertex of dead weight.
    (void)normal;

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
    if (coordQ)
        delete[] coordQ;
    if (normal)
        delete[] normal;
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

    for (int a = 0; a < 3; a++)
    {
        const float half = 0.5f * (mx[a] - mn[a]);
        qBias[a] = 0.5f * (mx[a] + mn[a]);
        qScale[a] = (half > 0.0f) ? (half / 32767.0f) : 1.0f;

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

    for (unsigned v = 0; v < total; v++)
    {
        for (int a = 0; a < 3; a++)
        {
            float q = (coord[v * 3 + a] - qBias[a]) / qScale[a];
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

    for (int a = 0; a < 2; a++)
    {
        const float half = 0.5f * (mx[a] - mn[a]);
        uvBias[a] = 0.5f * (mx[a] + mn[a]);
        uvScale[a] = (half > 0.0f) ? (half / 32767.0f) : 1.0f;
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
            float q = (uv[v * 2 + a] - uvBias[a]) / uvScale[a];
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

void pvrPrimBuffer::BuildInterleaved()
{
    if (inter || !coordQ || coordQCount == 0)
        return;

    const unsigned n = coordQCount;
    pvrInterVert* p = new pvrInterVert[n];
    if (!p)
        return;

    const bool haveUV  = uvQ && uvQCount >= n;
    const bool haveCol = colour != NULL;

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
        else
        {
            p[i].argb = 0xFFFFFFFFu;
        }
    }

    inter = p;
    interCount = n;
    interUV = haveUV;
    interColour = haveCol;

    delete[] coordQ; coordQ = NULL;
    delete[] uvQ;    uvQ = NULL;
    delete[] colour; colour = NULL;
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

void pvrPrimBuffer::SetRunList(const unsigned short* src, int count)
{
    delete[] runs;
    runs = NULL;
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
    if (slot < 0)
        return;

    pvrDrawCmd cmd;
    cmd.hdr = hdr;
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
template <bool InFront, bool HaveCol>
static void FillInterleaved(pvr_vertex_t* pkt, unsigned char* vis,
                            const pvrInterVert* src, unsigned n,
                            unsigned* visAll, unsigned argbDefault,
                            float uMul, float uAdd, float vMul, float vAdd)
{
    unsigned all = 1;

    for (unsigned i = 0; i < n; i++)
    {
        // One line covers two records, so this lands a fetch every other pass.
        dcache_pref_block(&src[i + 2]);

        const shz_vec4_t p = shz_xmtrx_transform_vec4(
            shz_vec4_init((float)src[i].x, (float)src[i].y, (float)src[i].z, 1.0f));

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
        v.argb = HaveCol ? src[i].argb : argbDefault;
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

    {
        pvr_poly_hdr_t* h = (pvr_poly_hdr_t*)pvr_dr_target();
        *h = cmd.hdr;
        pvr_dr_commit(h);
    }

    shz_xmtrx_load_4x4(&cmd.xform);

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
        const pvrBoxClass box = pvrClassifyBox(vp, lo, hi, &boxArea);

        if (box == kBoxCulled || boxArea < s_minScreenArea)
        {
#if defined SRR2_DC_PROFILER
            s_boxCulled++;
#endif
            PH_MARK(s_phSetup);
            return;
        }

        // One pass over the unique vertices building finished TA packets, then
        // emission by index. A vertex referenced by several indices is
        // transformed once, not once per reference.
        if (primType == PDDI_PRIM_TRISTRIP && indexCount && indices && indexCount >= 3
            && vcount > 0 && pvrReserveVtxCache(vcount) && s_vtxPkt && s_vtxVis)
        {
            const bool oix = (pvrOixWindow != NULL) && ((unsigned)vcount <= PVR_OIX_VERTS);
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
                if (inFront)
                {
                    if (interColour)
                        FillInterleaved<true, true>(pkt, s_vtxVis, inter, vcount,
                                                    &visAll, cmd.argb, uMul, uAdd, vMul, vAdd);
                    else
                        FillInterleaved<true, false>(pkt, s_vtxVis, inter, vcount,
                                                     &visAll, cmd.argb, uMul, uAdd, vMul, vAdd);
                }
                else
                {
                    if (interColour)
                        FillInterleaved<false, true>(pkt, s_vtxVis, inter, vcount,
                                                     &visAll, cmd.argb, uMul, uAdd, vMul, vAdd);
                    else
                        FillInterleaved<false, false>(pkt, s_vtxVis, inter, vcount,
                                                      &visAll, cmd.argb, uMul, uAdd, vMul, vAdd);
                }
            }
            else
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

                if (inter)
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

            PH_MARK(s_phXform);

            if (visAll)
            {
                if (runs)
                    EmitRunList<false>(oix, pkt, s_vtxVis, indices, runs, runCount, vp, clipPos);
                else
                    EmitStripRuns<false, true>(oix, pkt, s_vtxVis, indices, indexCount, vp, clipPos);
                PH_MARK(s_phEmit);
                return;
            }

            // Some vertices sit behind the near plane. Still emit from the
            // packets already built -- falling through to the generic path
            // would transform this meshlet a second time.
#if defined SRR2_DC_PROFILER
            s_clipDrawsFast++;
#endif
            if (runs)
                EmitRunList<true>(oix, pkt, s_vtxVis, indices, runs, runCount, vp, clipPos);
            else
                EmitStripRuns<true, true>(oix, pkt, s_vtxVis, indices, indexCount, vp, clipPos);
            PH_MARK(s_phClip);
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
        const unsigned genBefore_ = s_clipIters;
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
        s_genIters  += s_clipIters - genBefore_;
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
