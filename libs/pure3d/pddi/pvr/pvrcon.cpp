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

#include <pddi/base/debug.hpp>
#include <math.h>
#include <string.h>

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
    pvr_wait_ready(); 
    pvr_scene_begin();
}

void pvrContext::EndFrame()
{
    pddiBaseContext::EndFrame();
    PVR: pvr_scene_finish();
}

void pvrContext::Clear(unsigned bufferMask)
{
    pddiBaseContext::Clear(bufferMask);

}

void pvrContext::SetScissor(pddiRect* rect)
{
    pddiBaseContext::SetScissor(rect);

}

pddiPrimStream* pvrContext::BeginPrims(pddiShader* material, pddiPrimType primType, unsigned vertexType, int vertexCount, unsigned pass)
{
    (void)material;
    (void)primType;
    (void)vertexType;
    (void)vertexCount;
    (void)pass;
    // PVR: pvr_list_begin(); pvr_dr_init(); return stream
    return NULL;
}

void pvrContext::EndPrims(pddiPrimStream* stream)
{
    (void)stream;
    // PVR: flush verts; pvr_dr_commit; pvr_list_finish()
}

void pvrContext::DrawPrimBuffer(pddiShader* material, pddiPrimBuffer* buffer)
{
    (void)material;
    (void)buffer;
    // PVR: submit prim buffer as poly header + vertices
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

void pvrContext::LoadHardwareMatrix(pddiMatrixType id)
{
    (void)id;
    //PVR: load matrix for vertex transform 
}

void pvrContext::SetupHardwareProjection(void)
{
    // PVR: set projection 
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

    pvrPrimBufferStream(pvrPrimBuffer* b) : buffer(b) {}

    void Position(float x, float y, float z) { (void)x; (void)y; (void)z; }
    void Normal(float x, float y, float z) { (void)x; (void)y; (void)z; }
    void Colour(pddiColour c, int channel = 0) { (void)c; (void)channel; }
    void TexCoord1(float s, int channel = 0) { (void)s; (void)channel; }
    void TexCoord2(float s, float t, int channel = 0) { (void)s; (void)t; (void)channel; }
    void TexCoord3(float s, float t, float u, int channel = 0) { (void)s; (void)t; (void)u; (void)channel; }
    void TexCoord4(float s, float t, float u, float v, int channel = 0) { (void)s; (void)t; (void)u; (void)v; (void)channel; }
    void Specular(pddiColour c) { (void)c; }
    void SkinIndices(unsigned a, unsigned b = 0, unsigned c = 0, unsigned d = 0) { (void)a; (void)b; (void)c; (void)d; }
    void SkinWeights(float a, float b = 0.0f, float c = 0.0f) { (void)a; (void)b; (void)c; }
    void Vertex(rmt::Vector* v, pddiColour c) { (void)v; (void)c; }
    void Vertex(rmt::Vector* v, rmt::Vector* n) { (void)v; (void)n; }
    void Vertex(rmt::Vector* v, rmt::Vector2* uv) { (void)v; (void)uv; }
    void Vertex(rmt::Vector* v, pddiColour c, rmt::Vector2* uv) { (void)v; (void)c; (void)uv; }
    void Vertex(rmt::Vector* v, rmt::Vector* n, rmt::Vector2* uv) { (void)v; (void)n; (void)uv; }
    void Next() { }
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
    , allocated(0)
    , total(nVertex)
    , indices(NULL)
    , indexCount(nIndex)
    , valid(false)
    , mem(0)
{
    context->AddRef();
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
    if (!stream)
        stream = new pvrPrimBufferStream(this);
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
    if (indices)
        delete[] indices;
    indexCount = count;
    indices = new unsigned short[count];
    memcpy(indices, idx, (size_t)count * sizeof(unsigned short));
}

void pvrPrimBuffer::Display(void)
{
    if (!valid || !context)
        return;
    // PVR: submit as poly header + vertices (OP or TR list)
}
