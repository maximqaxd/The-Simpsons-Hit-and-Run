//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// PDDI PVR context
//=============================================================================

#ifndef _PVRCON_HPP_
#define _PVRCON_HPP_

#define PVR_BUFFERED_VERTS 1024

#include <pddi/pddi.hpp>
#include <pddi/pddipc.hpp>
#include <pddi/base/basecontext.hpp>
#include <pddi/pvr/pvr.hpp>
#include <sh4zam/shz_sh4zam.h>

class pvrDisplay;
class pvrDevice;
class pvrMat;
struct pvrTextureEnv;

// stub primStream
class pvrPrimStream : public pddiPrimStream
{
public:
    void Coord(float x, float y, float z) { (void)x;(void)y;(void)z; }
    void Normal(float x, float y, float z) { (void)x;(void)y;(void)z; }
    void Colour(pddiColour colour, int channel = 0) { (void)colour;(void)channel; }
    void UV(float u, float v, int channel = 0) { (void)u;(void)v;(void)channel; }
    void Specular(pddiColour colour) { (void)colour; }
    void Vertex(pddiVector* v, pddiColour c) { (void)v;(void)c; }
    void Vertex(pddiVector* v, pddiVector* n) { (void)v;(void)n; }
    void Vertex(pddiVector* v, pddiVector2* uv) { (void)v;(void)uv; }
    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv) { (void)v;(void)c;(void)uv; }
    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv) { (void)v;(void)n;(void)uv; }
};

//--------------------------------------------------------------
class pvrContext : public pddiBaseContext
{
public:
    pvrContext(pvrDevice* dev, pvrDisplay* disp);
    ~pvrContext();

    // frame synchronisation
    void BeginFrame();
    void EndFrame();

    // buffer clearing
    void Clear(unsigned bufferMask);

    // viewport clipping
    void SetScissor(pddiRect* rect);

    // immediate mode prim rendering
    pddiPrimStream* BeginPrims(pddiShader* material, pddiPrimType primType, unsigned vertexType, int vertexCount = 0, unsigned pass = 0);
    void EndPrims(pddiPrimStream* stream);

    // retained mode prim rendering
    void DrawPrimBuffer(pddiShader* material, pddiPrimBuffer* buffer);

    // lighting
    int GetMaxLights();
    void SetAmbientLight(pddiColour col);

    // backface culling
    void SetCullMode(pddiCullMode mode);

    // colour buffer control
    void SetColourWrite(bool red, bool green, bool blue, bool alpha);

    // z-buffer control
    void EnableZBuffer(bool enable);
    void SetZCompare(pddiCompareMode compareMode);
    void SetZWrite(bool);
    void SetZBias(float bias);
    void SetZRange(float n, float f);

    // stencil buffer control
    void EnableStencilBuffer(bool enable);
    void SetStencilCompare(pddiCompareMode compare);
    void SetStencilRef(int ref);
    void SetStencilMask(unsigned mask);
    void SetStencilWriteMask(unsigned mask);
    void SetStencilOp(pddiStencilOp failOp, pddiStencilOp zFailOp, pddiStencilOp zPassOp);

    // polygon fill
    void SetFillMode(pddiFillMode mode);

    // fog
    void EnableFog(bool enable);
    void SetFog(pddiColour colour, float start, float end);

    // utility
    int GetMaxTextureDimension(void);

    // extensions
    pddiExtension* GetExtension(unsigned extID);
    bool VerifyExtension(unsigned extID);

    pvrDisplay* GetDisplay(void) { return display; }
    void LoadTransformToXmtrx() const { shz_xmtrx_load_4x4(&viewProjM); }
    void LoadTransformToXmtrx(const float* scale, const float* bias) const;
    void BuildTransform(const float* scale, const float* bias, shz_mat4x4_t* out) const;
    const shz_mat4x4_t& GetViewProj() const { return viewProjM; }
    void FlushDeferredLists();
    pvr_cull_mode_t GetCurrentCull() const;
    struct pvrViewportMap GetViewportMap() const;
    pddiShader* GetDefaultShader(void) { return defaultShader; }

    unsigned contextID;

    // Helper to build PVR poly header from texture environment
    void BuildPolyHeader(const pvrTextureEnv& env, pvr_poly_hdr_t& outHdr, pvr_list_t& outList) const;

    friend bool TransformToScreen(const pvrContext* ctx, float x, float y, float z, float& sx, float& sy, float& sz);
    friend void EnsureList(pvrContext* ctx, pvr_list_t list);
    class pvrImmediatePrimStream;
    friend class pvrImmediatePrimStream;
    friend class pvrPrimBuffer;  

protected:
    void LoadHardwareMatrix(pddiMatrixType id);
    void SetupHardwareProjection(void);
    void SetupHardwareLight(int);
    void BeginTiming(void);
    float EndTiming(void);

    void SetVertexArray(unsigned descr, void* data, int count);

    pvrDevice* device;
    pvrDisplay* display;

    pddiShader* defaultShader;

    int maxTexSize;

    // PVR scene/list state
    pvr_list_t currentList;
    unsigned begunMask;

    // Cached projection/viewport state 
    shz_mat4x4_t modelViewM;
    shz_mat4x4_t projectionM;
    shz_mat4x4_t viewProjM;
    int viewportX, viewportY, viewportW, viewportH; 
    int displayW, displayH;
};

// One record per vertex instead of three parallel arrays. The submit loop
// reads position, uv and colour for the same vertex back to back, so keeping
// them together turns three streams into one and halves a cache line per two
// vertices. 16 bytes against the 14 the split arrays cost.
struct pvrInterVert
{
    short    x, y, z;
    short    u, v;
    short    pad;
    unsigned argb;
};

class pvrPrimBufferStream;

class pvrPrimBuffer : public pddiPrimBuffer
{
public:
    pvrPrimBuffer(pvrContext* context, pddiPrimType type, unsigned vertexFormat, int nVertex, int nIndex);
    ~pvrPrimBuffer();

    pddiPrimBufferStream* Lock();
    void Unlock(pddiPrimBufferStream* stream);

    unsigned char* LockIndexBuffer();
    void UnlockIndexBuffer(int count);

    void SetIndices(unsigned short* indices, int count);
    void SetRunList(const unsigned short* runs, int count);

    bool CheckMemImageVersion(int version) { return false; }
    void* LockMemImage(unsigned) { return NULL; }
    void UnlockMemImage() { }
    unsigned GetMemImageLength() { return 0; }
    void SetMemImageParam(unsigned param, unsigned value) { (void)param; (void)value; }

    void Display(void);
    void DisplayWithMaterial(pvrMat* mat, unsigned pass);
    void SubmitDeferred(const struct pvrDrawCmd& cmd);

protected:
    friend class pvrPrimBufferStream;
    pvrPrimBufferStream* stream;
    pvrContext* context;

    pddiPrimType primType;
    unsigned vertexType;

    int nStrips;
    int* strips;

    void BuildInterleaved();
    void DropInterleaved();

    void QuantiseCoords();
    void ComputeBounds();
    void RestoreCoords();
    void QuantiseUVs();
    void RestoreUVs();

    float* coord;
    short* coordQ;
    float qScale[3];
    float qBias[3];

    // Model-space bounds, kept for every buffer so the backend can cull and
    // fast-path draws whether or not the positions ended up quantised.
    float bbMin[3];
    float bbMax[3];
    bool  bbValid;
    bool coordWritten;
    unsigned coordQCount;

    float* normal;
    float* uv;
    short* uvQ;
    float uvScale[2];
    float uvBias[2];
    bool uvWritten;
    unsigned uvQCount;
    unsigned char* colour;

    // Built on first draw, once every chunk has been written; the three
    // source arrays are released to it. NULL means float coords were kept.
    pvrInterVert* inter;
    unsigned interCount;
    bool interUV;
    bool interColour;


    unsigned allocated;
    unsigned total;

    unsigned short* indices;
    unsigned indexCount;

    // Pairs of (first index, index count). NULL means the strips have to be
    // found the hard way.
    unsigned short* runs;
    unsigned runCount;

    bool valid;
    unsigned mem;
};

#endif /* _PVRCON_HPP_ */
