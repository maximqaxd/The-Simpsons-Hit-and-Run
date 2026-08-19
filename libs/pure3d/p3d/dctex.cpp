//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
// Loader for pre-encoded Dreamcast textures (pvrtex .DT container).
//=============================================================================

#include <p3d/dctex.hpp>
#include <p3d/file.hpp>
#include <p3d/error.hpp>
#include <pddi/pdditype.hpp>

#include <string.h>

bool tDCTexHandler::CheckFormat(Format f)
{
    return f == IMG_DC_DT;
}

void tDCTexHandler::CreateImage(tFile* file, tImageHandler::Builder* builder)
{
    tDCTexHeader header;
    file->GetData(&header, sizeof(header), tFile::BYTE);

    if (memcmp(header.fourcc, "DcTx", 4) != 0)
    {
        P3DVERIFY(false, "Dreamcast texture has a bad fourcc");
        return;
    }

    const int headerBytes = (header.headerSize + 1) * 32;
    if (header.chunkSize < (P3D_U32)headerBytes)
    {
        P3DVERIFY(false, "Dreamcast texture is truncated");
        return;
    }

    // The texture is uploaded exactly as it sits on disc, so the whole file --
    // header and all -- is handed to the pddi texture, which reads pvrType out
    // of it at upload time.
    builder->SetTextureType(PDDI_TEXTYPE_DC_DT);
    if (!builder->BeginImage(header.width, header.height, 16,
                             tImageHandler::Builder::TOP, NULL))
    {
        return;
    }

    unsigned char* dest = (unsigned char*)builder->GetMemoryImagePtr();
    if (dest == NULL)
    {
        return;
    }

    memcpy(dest, &header, sizeof(header));
    const int remaining = (int)header.chunkSize - (int)sizeof(header);
    if (remaining > 0)
    {
        file->GetData(dest + sizeof(header), remaining, tFile::BYTE);
    }

    builder->EndImage();
}
