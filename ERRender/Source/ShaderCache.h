#pragma once
#include "RenderDX11Utils.h"

#include <Toshi/TString8.h>

namespace remaster
{

namespace dx11
{

Toshi::TString8 GetShaderFileDirectory( const Toshi::TString8& a_rcFilepath );
Toshi::TString8 JoinShaderFilePath( const Toshi::TString8& a_rcDirectory, const Toshi::TString8& a_rcFilepath );

// Shader source files are read once and served from a RAM cache for every entry point
// and permutation; freed after the warm-up via ClearShaderFileCache
void  LockShaderFileCache();
void  UnlockShaderFileCache();
TBOOL GetCachedShaderFile( const Toshi::TString8& a_rcFilepath, const TCHAR*& a_rpchData, TUINT& a_ruiSize ); // call locked
void  ClearShaderFileCache();

// Compiled blob disk cache (Data\ShaderCache): skips D3DCompile when the source tree
// (incl. all #includes), entry point, target and combo layout are unchanged
TUINT64         ComputeShaderCacheHash( const TCHAR* a_pchFilepath, LPCSTR a_pEntrypoint, LPCSTR a_pTarget, const ShaderComboDefinition* a_pCombos, TUINT a_uiNumCombos, TUINT a_uiNumPermutations );
Toshi::TString8 GetShaderCachePath( const TCHAR* a_pchFilepath, LPCSTR a_pEntrypoint, LPCSTR a_pTarget );
TBOOL           LoadShaderCacheBlobs( const Toshi::TString8& a_rcCachePath, TUINT64 a_uiHash, ID3DBlob** a_ppBlobs, TUINT a_uiNumBlobs );
void            SaveShaderCacheBlobs( const Toshi::TString8& a_rcCachePath, TUINT64 a_uiHash, ID3DBlob* const* a_ppBlobs, TUINT a_uiNumBlobs );

} // namespace dx11

} // namespace remaster
