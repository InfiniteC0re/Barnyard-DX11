#pragma once

namespace editor
{

extern TBOOL g_bEnabled;

void Render();
void SetupHooks();

// Pass a_bGlowObjectsInvalid = TTRUE after a level change: the old glow objects are gone, so
// detach the old entries without dereferencing their stale pointers
void DynamicLights_LoadForCurrentLevel( const TCHAR* a_szPath, TBOOL a_bGlowObjectsInvalid );
void DynamicLights_SaveCurrentLevel( const TCHAR* a_szPath );

void LevelSettings_ApplyForCurrentLevel( const TCHAR* a_szPath );
void LevelSettings_SaveCurrentLevel( const TCHAR* a_szPath );

// Driven by the AGameTimeFXManager::OnUpdate hook
void FXSettings_Update( TFLOAT a_fDeltaTime );

} // namespace editor

