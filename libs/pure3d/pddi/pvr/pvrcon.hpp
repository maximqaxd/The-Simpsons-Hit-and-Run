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

class pvrDisplay;
class pvrDevice;

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

    unsigned contextID;

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

    bool CheckMemImageVersion(int version) { return false; }
    void* LockMemImage(unsigned) { return NULL; }
    void UnlockMemImage() { }
    unsigned GetMemImageLength() { return 0; }
    void SetMemImageParam(unsigned param, unsigned value) { (void)param; (void)value; }

    void Display(void);

protected:
    friend class pvrPrimBufferStream;
    pvrPrimBufferStream* stream;
    pvrContext* context;

    pddiPrimType primType;
    unsigned vertexType;

    int nStrips;
    int* strips;

    float* coord;
    float* normal;
    float* uv;
    unsigned char* colour;

    unsigned allocated;
    unsigned total;

    unsigned short* indices;
    unsigned indexCount;

    bool valid;
    // PVR: use 0 as placeholder or add pvr_dr_state_t later
    unsigned mem;
};

#endif /* _PVRCON_HPP_ */
