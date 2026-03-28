#pragma once
#include <d3d11.h>

namespace remaster
{

namespace dx11
{

ID3DBlob* CompileShader( const TCHAR* a_pchSrcData, LPCSTR a_pEntrypoint, LPCSTR a_pTarget, const D3D_SHADER_MACRO* a_pDefines );
ID3DBlob* CompileShaderFromFile( const TCHAR* a_pchFilepath, LPCSTR a_pEntrypoint, LPCSTR a_pTarget, const D3D_SHADER_MACRO* a_pDefines );
HRESULT   CreatePixelShader( const void* a_pShaderBytecode, SIZE_T a_uiBytecodeLength, ID3D11PixelShader** a_ppPixelShader );
HRESULT   CreateVertexShader( const void* a_pShaderBytecode, SIZE_T a_uiBytecodeLength, ID3D11VertexShader** a_ppVertexShader );

ID3D11Buffer* CreateBuffer(
    TUINT       a_uiFlags,
    TUINT       a_uiDataSize,
    const void* a_pData,
    D3D11_USAGE a_eUsage,
    TUINT       a_eCPUAccessFlags
);

using CreateTextureFlags = TUINT32;
enum : CreateTextureFlags
{
	CTF_RENDER_NONE   = 0,
	CTF_RENDER_TARGET = BITFLAG( 0 ),
	CTF_GEN_MIPMAPS   = BITFLAG( 1 ),
};

ID3D11ShaderResourceView* CreateTexture(
    TUINT              a_uiWidth,
    TUINT              a_uiHeight,
    DXGI_FORMAT        a_eFormat,
    const void*        a_pData,
    D3D11_USAGE        a_eUsage,
    TUINT32            a_eCPUAccessFlags,
    TUINT32            a_uiSampleDescCount,
    CreateTextureFlags a_eFlags = CTF_RENDER_NONE
);

TINT GetTextureRowPitch( DXGI_FORMAT a_eFormat, TUINT a_uiWidth );
TINT GetTextureDepthPitch( DXGI_FORMAT a_eFormat, TUINT a_uiWidth, TUINT a_uiHeight );

TBOOL IsColorEqual( const TFLOAT a_pColor1[ 4 ], const TFLOAT a_pColor2[ 4 ] );

}

}