#include "pch.h"

#define STB_IMAGE_IMPLEMENTATION
#include "SOIL2/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "SOIL2/stb_image_write.h"
#include "SOIL2/image_helper.h"
#include "SOIL2/image_DXT.h"
#include "SOIL2/pvr_helper.h"
#include "SOIL2/pkm_helper.h"
#include "SOIL2/jo_jpeg.h"


//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING