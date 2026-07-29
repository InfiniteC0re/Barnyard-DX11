// nanosvg is a header-only SVG parser + rasteriser. This TU pulls in the single
// implementation (built without the PCH, see premake); the render interface includes
// the same headers for the declarations only
#pragma warning( push )
#pragma warning( disable : 4244 ) // double/float narrowing inside the vendored library
#pragma warning( disable : 4996 ) // POSIX name / unchecked CRT calls in nanosvg

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg.h>
#include <nanosvgrast.h>

#pragma warning( pop )
