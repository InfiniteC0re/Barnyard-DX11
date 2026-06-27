#pragma once

namespace settings
{

extern bool g_bEnabled;

// Draws the graphics settings window (call from OnImGuiRenderOverlay). The Alt+G toggle
// is handled by the editor input hook (see Editor.cpp) so there is a single game hook.
void Render();

} // namespace settings
