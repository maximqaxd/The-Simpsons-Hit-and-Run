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
    memset(&drState, 0, sizeof(drState));

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

void pvrContext::BeginFrame()
{
    pddiBaseContext::BeginFrame();
    // Start a new PVR scene.
    pvr_wait_ready();
    pvr_scene_begin();

    // Reset per-frame list state. 
    currentList = (pvr_list_t)-1;
    begunMask = 0;
}

void pvrContext::EndFrame()
{
    pddiBaseContext::EndFrame();
    if (currentList != (pvr_list_t)-1)
    {
        pvr_list_finish();
        currentList = (pvr_list_t)-1;
    }
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

static inline int MapFilter(pddiFilterMode m)
{
    switch (m)
    {
        case PDDI_FILTER_NONE: return PVR_FILTER_NONE;
        default: return PVR_FILTER_BILINEAR;
    }
}

static inline int MapCull(pddiCullMode m)
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

static inline int MapDepthCompareInvW(pddiCompareMode m)
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

// Copy rmt::Matrix (column-major m[col][row]) into shz_mat4x4_t with GL->PVR Z flip.
static inline void CopyRmtToShzWithFlip(shz_mat4x4_t* out, const rmt::Matrix& in)
{
    // rmt is column-major in memory; load as-is then negate Z column via scale.
    shz_xmtrx_load_unaligned_4x4(&in.m[0][0]);
    shz_xmtrx_apply_scale(1.0f, 1.0f, -1.0f);
    shz_xmtrx_store_4x4(out);
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
            BuildPerspective(projectionM, fov_rad, aspect, nearPlane);
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

inline bool TransformToScreen(const pvrContext* ctx, float x, float y, float z,
                              float& sx, float& sy, float& sz)
{
    shz_xmtrx_load_4x4(&ctx->viewProjM);
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

static inline pvr_list_t ChooseList(const pvrTextureEnv& env)
{
    if (env.alphaTest)
        return PVR_LIST_PT_POLY;
    if (env.alphaBlendMode != PDDI_BLEND_NONE)
        return PVR_LIST_TR_POLY;
    return PVR_LIST_OP_POLY;
}

static inline void MapBlend(const pvrTextureEnv& env, int& src, int& dst, bool& enable)
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

inline void EnsureList(pvrContext* ctx, pvr_list_t list)
{
    if (ctx->currentList == list)
        return;

    // Close any currently open list.
    if (ctx->currentList != (pvr_list_t)-1)
    {
        pvr_list_finish();
        ctx->currentList = (pvr_list_t)-1;
    }

    const unsigned bit = ListBit(list);
    if (!bit)
        return;

    const bool canReopen = (list == PVR_LIST_PT_POLY);
    if ((ctx->begunMask & bit) && !canReopen)
        return;

    if (!(ctx->begunMask & bit) || canReopen)
    {
        if (pvr_list_begin(list) < 0)
            return;
        pvr_dr_init(&ctx->drState);
        ctx->currentList = list;
        ctx->begunMask |= bit;
    }
}

void pvrContext::BuildPolyHeader(const pvrTextureEnv& env, pvr_poly_hdr_t& outHdr, pvr_list_t& outList) const
{
    outList = ChooseList(env);

    pvr_poly_cxt_t cxt;
    if (env.enabled && env.texture && env.texture->GetVramPtr())
    {
        const int clamp = (env.uvMode == PDDI_UV_CLAMP) ? PVR_UVCLAMP_UV : PVR_UVCLAMP_NONE;
        pvr_poly_cxt_txr(&cxt, outList,
                         env.texture->GetPvrTxrFormat(),
                         env.texture->GetWidth(), env.texture->GetHeight(),
                         env.texture->GetVramPtr(),
                         MapFilter(env.filterMode));
        cxt.txr.uv_clamp = clamp;
        // Pure3D content/UVs are authored with GL-style V (origin bottom-left).
        // The PVR hardware treats V origin differently, so flip V in the texture context.
        cxt.txr.uv_flip = PVR_UVFLIP_NONE;
        cxt.txr.env = (outList == PVR_LIST_TR_POLY) ? PVR_TXRENV_MODULATEALPHA : PVR_TXRENV_MODULATE;
        cxt.txr.alpha = PVR_TXRALPHA_ENABLE;
    }
    else
    {
        pvr_poly_cxt_col(&cxt, outList);
    }

    cxt.gen.culling = MapCull(state.renderState->cullMode);
    cxt.gen.fog_type = PVR_FOG_DISABLE;

    // Depth.
    if (state.renderState->zEnabled)
    {
        cxt.depth.comparison = MapDepthCompareInvW(state.renderState->zCompare);
        cxt.depth.write = (outList == PVR_LIST_TR_POLY) ? PVR_DEPTHWRITE_DISABLE :
                          (state.renderState->zWrite ? PVR_DEPTHWRITE_ENABLE : PVR_DEPTHWRITE_DISABLE);
    }
    else
    {
        cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;
        cxt.depth.write = PVR_DEPTHWRITE_DISABLE;
    }

    // Blend.
    bool blendEnable = false;
    int src = PVR_BLEND_ONE, dst = PVR_BLEND_ZERO;
    MapBlend(env, src, dst, blendEnable);
    if (outList == PVR_LIST_TR_POLY && blendEnable)
    {
        cxt.blend.src = src;
        cxt.blend.dst = dst;
    }
    else
    {
        cxt.blend.src = PVR_BLEND_ONE;
        cxt.blend.dst = PVR_BLEND_ZERO;
    }

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
        EnsureList(ctx, list);

        pvr_dr_state_t& drStateRef = ctx->GetDrState();
        const float uScale = GetUStrideScale(env.texture);
        const bool flipV = true;


        {
            pvr_poly_hdr_t* h = (pvr_poly_hdr_t*)pvr_dr_target(drStateRef);
            *h = hdr;
            pvr_dr_commit(h);
        }
        auto submitTri = [&](int i0, int i1, int i2)
        {
            float sx0, sy0, sz0;
            float sx1, sy1, sz1;
            float sx2, sy2, sz2;
            if (!TransformToScreen(ctx, coords[i0].x, coords[i0].y, coords[i0].z, sx0, sy0, sz0)) return;
            if (!TransformToScreen(ctx, coords[i1].x, coords[i1].y, coords[i1].z, sx1, sy1, sz1)) return;
            if (!TransformToScreen(ctx, coords[i2].x, coords[i2].y, coords[i2].z, sx2, sy2, sz2)) return;

            pvr_vertex_t* v = pvr_dr_target(drStateRef);
            v->flags = PVR_CMD_VERTEX;
            v->x = sx0; v->y = sy0; v->z = sz0;
            v->u = uvs[i0].u * uScale;
            v->v = flipV ? (1.0f - uvs[i0].v) : uvs[i0].v;
            v->argb = (uint32_t)(unsigned)colours[i0];
            v->oargb = 0;
            pvr_dr_commit(v);

            v = pvr_dr_target(drStateRef);
            v->flags = PVR_CMD_VERTEX;
            v->x = sx1; v->y = sy1; v->z = sz1;
            v->u = uvs[i1].u * uScale;
            v->v = flipV ? (1.0f - uvs[i1].v) : uvs[i1].v;
            v->argb = (uint32_t)(unsigned)colours[i1];
            v->oargb = 0;
            pvr_dr_commit(v);

            v = pvr_dr_target(drStateRef);
            v->flags = PVR_CMD_VERTEX_EOL;
            v->x = sx2; v->y = sy2; v->z = sz2;
            v->u = uvs[i2].u * uScale;
            v->v = flipV ? (1.0f - uvs[i2].v) : uvs[i2].v;
            v->argb = (uint32_t)(unsigned)colours[i2];
            v->oargb = 0;
            pvr_dr_commit(v);
        };

        if (primType == PDDI_PRIM_TRIANGLES)
        {
            for (size_t i = 0; i + 2 < coords.size(); i += 3)
                submitTri((int)i, (int)i + 1, (int)i + 2);
        }
        else if (primType == PDDI_PRIM_TRISTRIP)
        {
            for (size_t i = 0; i + 2 < coords.size(); ++i)
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

        pvr_dr_finish();
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
        buffer->coord[cur * 3 + 0] = x;
        buffer->coord[cur * 3 + 1] = y;
        buffer->coord[cur * 3 + 2] = z;
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
        if (!buffer->uv) return;
        if (channel != 0) return;
        buffer->uv[cur * 2 + 0] = s;
        buffer->uv[cur * 2 + 1] = t;
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
    mem += 12;

    if (vertexFormat & PDDI_V_NORMAL)
    {
        normal = new float[3 * (size_t)allocated];
        mem += 12;
    }

    if (vertexFormat & 0xf)
    {
        uv = new float[2 * (size_t)allocated];
        mem += 8;
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
    if (normal)
        delete[] normal;
    if (uv)
        delete[] uv;
    if (colour)
        delete[] colour;
    if (indices)
        delete[] indices;
    if (stream)
        delete stream;
    context->Release();
}

pddiPrimBufferStream* pvrPrimBuffer::Lock()
{
    total = 0;
    if (stream) stream->cur = 0;
    return stream;
}

void pvrPrimBuffer::Unlock(pddiPrimBufferStream* s)
{
    (void)s;
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
    EnsureList(context, list);
    pvr_dr_state_t& drStateRef = context->GetDrState();
    const float uScale = GetUStrideScale(env.texture);
    const bool flipV = true;

    {
        pvr_poly_hdr_t* h = (pvr_poly_hdr_t*)pvr_dr_target(drStateRef);
        *h = hdr;
        pvr_dr_commit(h);
    }

    auto submitTriIdx = [&](int i0, int i1, int i2)
    {
        float sx[3], sy[3], sz[3];
        const int idxsPre[3] = { i0, i1, i2 };
        for (int k = 0; k < 3; k++)
        {
            const int vi = idxsPre[k];
            const float px = coord[vi * 3 + 0];
            const float py = coord[vi * 3 + 1];
            const float pz = coord[vi * 3 + 2];
            if (!TransformToScreen(context, px, py, pz, sx[k], sy[k], sz[k]))
                return;
        }

        const int idxs[3] = { i0, i1, i2 };
        for (int k = 0; k < 3; k++)
        {
            const int vi = idxs[k];
            pvr_vertex_t* v = pvr_dr_target(drStateRef);
            v->flags = (k == 2) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
            v->x = sx[k]; v->y = sy[k]; v->z = sz[k];
            if (uv)
            {
                v->u = uv[vi * 2 + 0] * uScale;
                v->v = flipV ? (1.0f - uv[vi * 2 + 1]) : uv[vi * 2 + 1];
            }
            else
            {
                v->u = 0.0f;
                v->v = 0.0f;
            }

            if (colour)
            {
                const unsigned char* c = &colour[vi * 4];
                v->argb = ((uint32_t)c[3] << 24) | ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | (uint32_t)c[2];
            }
            else
            {
                v->argb = (uint32_t)(unsigned)env.diffuse;
            }
            v->oargb = 0;
            pvr_dr_commit(v);
        }
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

    pvr_dr_finish();
}
