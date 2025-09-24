#pragma once
#include <dwrite_3.h>

namespace remaster
{

void SetupRenderHooks_FontRenderer();

namespace fontrenderer
{

void Create();
void Destroy();
void Update();

} // namespace fontrenderer

} // namespace remaster
