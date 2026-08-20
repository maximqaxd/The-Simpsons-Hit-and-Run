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
#include <sh4zam/shz_sh4zam.h>

#include <pddi/base/debug.hpp>
#include <math.h>
#include <string.h>

#include <vector>

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

void pvrContext::BeginFrame()
{
    pddiBaseContext::BeginFrame();
    // Start a new PVR scene.
    pvr_wait_ready();
    pvr_scene_begin();

    currentList = (pvr_list_t)-1;
    begunMask = 0;
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
    pvr_scene_finish();
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
    const float invw = 1.0f / cv.pos.w;
    pvr_vertex_t* vert = (pvr_vertex_t*)pvr_dr_target();

    vert->flags = flags;
    vert->x = vp.ox + vp.hw * (1.0f + cv.pos.x * invw);
    vert->y = vp.oy + vp.hh * (1.0f - cv.pos.y * invw);
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
};

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

void pvrContext::BuildPolyHeader(const pvrTextureEnv& env, pvr_poly_hdr_t& outHdr, pvr_list_t& outList) const
{
    const pvr_list_t logical = ChooseList(env);
    outList = logical;

    pvr_poly_cxt_t cxt;
    if (env.enabled && env.texture && env.texture->GetVramPtr())
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

        coords.clear();
        colours.clear();
        uvs.clear();

        coords.reserve((size_t)(reserveVerts > 0 ? reserveVerts : 0));
        colours.reserve((size_t)(reserveVerts > 0 ? reserveVerts : 0));
        uvs.reserve((size_t)(reserveVerts > 0 ? reserveVerts : 0));

        curColour = pddiColour(255, 255, 255, 255);
        curUV.u = 0.0f; curUV.v = 0.0f;
    }

    void Coord(float x, float y, float z) override
    {
        pddiVector v;
        v.x = x; v.y = y; v.z = z;
        coords.push_back(v);
        colours.push_back(curColour);
        uvs.push_back(curUV);
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
        if (!ctx || coords.empty())
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
            return;

        const float uScale = GetUStrideScale(env.texture);
        const bool flipV = true;

        pvrDrawCmd cmd;
        cmd.hdr = hdr;
        cmd.xform = ctx->GetViewProj();
        cmd.vp = ctx->GetViewportMap();
        cmd.buffer = NULL;
        cmd.immFirst = (unsigned)s_immVerts.size();
        cmd.immCount = (unsigned)coords.size();
        cmd.immPrim = primType;
        cmd.argb = 0xffffffffu;
        cmd.uScale = uScale;

        for (size_t i = 0; i < coords.size(); i++)
        {
            pvrImmVert iv;
            iv.x = coords[i].x;
            iv.y = coords[i].y;
            iv.z = coords[i].z;
            iv.u = uvs[i].u * uScale;
            iv.v = flipV ? (1.0f - uvs[i].v) : uvs[i].v;
            iv.argb = (uint32_t)(unsigned)colours[i];
            s_immVerts.push_back(iv);
        }

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
            out.u = verts[i].u;
            out.v = verts[i].v;
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
        else
        {
            // Lines/points not supported by PVR backend directly yet.
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

    std::vector<pddiVector> coords;
    std::vector<pddiColour> colours;
    std::vector<pddiVector2> uvs;
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
#if defined RAD_DC_TRACE_BIG_ALLOCS
    {
        static unsigned frame = 0;
        if ((frame++ % 60) == 0)
        {
            printf("[lists] op %u tr %u pt %u, imm verts %u\n",
                   (unsigned)s_drawCmds[0].size(),
                   (unsigned)s_drawCmds[1].size(),
                   (unsigned)s_drawCmds[2].size(),
                   (unsigned)s_immVerts.size());
        }
    }
#endif

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
                cmd.buffer->SubmitDeferred(cmd);
            else
                pvrImmediatePrimStream::SubmitDeferred(cmd);
        }

        pvr_list_finish();
    }
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
    , uvQ(NULL)
    , uvWritten(false)
    , uvQCount(0)
    , normal(NULL)
    , uv(NULL)
    , colour(NULL)
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

    if (vertexFormat & PDDI_V_NORMAL)
    {
        normal = new float[3 * (size_t)allocated];
        mem += 12;
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
    if (indices)
        delete[] indices;
    if (stream)
        delete stream;
    context->Release();
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
    }

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

pddiPrimBufferStream* pvrPrimBuffer::Lock()
{
    total = 0;
    coordWritten = false;
    uvWritten = false;
    if (stream) stream->cur = 0;
    return stream;
}

void pvrPrimBuffer::Unlock(pddiPrimBufferStream* s)
{
    (void)s;
    if (coordWritten)
        QuantiseCoords();
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
    if (coordQ && !coord)
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

    s_drawCmds[slot].push_back(cmd);
}

void pvrPrimBuffer::SubmitDeferred(const pvrDrawCmd& cmd)
{
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
                : shz_xmtrx_transform_vec4(
                      shz_vec4_init((float)coordQ[vi * 3 + 0], (float)coordQ[vi * 3 + 1],
                                    (float)coordQ[vi * 3 + 2], 1.0f));

            if (uv)
            {
                tri[k].u = uv[vi * 2 + 0] * uScale;
                tri[k].v = flipV ? (1.0f - uv[vi * 2 + 1]) : uv[vi * 2 + 1];
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

            if (colour)
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


}
