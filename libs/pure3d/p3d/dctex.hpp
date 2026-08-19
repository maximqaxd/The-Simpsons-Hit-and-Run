//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// Loader for pre-encoded Dreamcast textures (pvrtex .DT container).
//=============================================================================

#ifndef _DCTEX_HPP
#define _DCTEX_HPP

#include <p3d/imagefactory.hpp>

// Header of a .DT file, as produced by tools/p3dconv. Everything the PVR needs
// is already baked in: the pixels are twiddled and VQ compressed, and pvrType
// holds the texture control bits in their hardware layout.
struct tDCTexHeader
{
    char     fourcc[4];      // "DcTx"
    P3D_U32  chunkSize;      // whole file, header included, multiple of 32
    P3D_U8   version;
    P3D_U8   headerSize;     // in 32-byte units, minus one
    P3D_U8   codebookSize;   // entries minus one; only valid when compressed
    P3D_U8   coloursUsed;
    P3D_U16  width;
    P3D_U16  height;
    P3D_U32  pvrType;
    P3D_U32  pad[3];
};

class tDCTexHandler : public tImageHandler
{
public:
    const char* GetExtension() { return "dt"; }
    bool CanLoad()       { return true; }
    bool CanSave()       { return false; }

    bool CheckFormat(Format);
    void CreateImage(tFile* data, tImageHandler::Builder* builder);
};

#endif // _DCTEX_HPP
