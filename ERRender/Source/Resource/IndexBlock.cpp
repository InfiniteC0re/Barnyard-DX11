#include "pch.h"
#include "IndexBlock.h"
#include "ERRender.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <Platform/DX8/TIndexBlockResource_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

MEMBER_HOOK( 0x006c0280, Toshi::TIndexBlockResource, TIndexBlockResource_DestroyHAL, void )
{
	if ( m_HALBuffer.pIndexBuffer )
	{
		TASSERT( TFALSE );
		//m_HALBuffer.pIndexBuffer->Release();
		m_HALBuffer.pIndexBuffer = TNULL;
	}
}

MEMBER_HOOK( 0x006c0350, Toshi::TIndexBlockResource, TIndexBlockResource_CreateHAL, TBOOL )
{
	if ( m_HALBuffer.pIndexBuffer )
	{
		TASSERT( TFALSE );
		//m_HALBuffer.pIndexBuffer->Release();
		m_HALBuffer.pIndexBuffer = TNULL;
	}

	remaster::RenderDX11* pRenderer = remaster::g_pRender;
	D3D11_USAGE           usage     = D3D11_USAGE_DEFAULT;

	if ( ISZERO( m_uiFlags & 1 ) )
	{
		usage      = D3D11_USAGE_DYNAMIC;
		m_uiOffset = 0;
	}

	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.ByteWidth           = m_uiMaxIndices * sizeof( TIndexType );
	bufferDesc.Usage               = usage;
	bufferDesc.BindFlags           = D3D11_BIND_INDEX_BUFFER;
	bufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.MiscFlags           = 0;
	bufferDesc.StructureByteStride = sizeof( TIndexType );

	HRESULT hRes = pRenderer->GetD3D11Device()->CreateBuffer( &bufferDesc, TNULL, (ID3D11Buffer**)&m_HALBuffer.pIndexBuffer );

	if ( FAILED( hRes ) )
	{
		TERROR( "Unable to create a new index buffer!\n" );
		return TFALSE;
	}

	return TTRUE;
}

MEMBER_HOOK( 0x006c0060, Toshi::TIndexBlockResource, TIndexBlockResource_Lock, TBOOL, TIndexPoolResourceInterface::LockBuffer& a_rLockBuffer, TUINT16 a_uiNumIndices )
{
	remaster::RenderDX11* pRenderer = remaster::g_pRender;

	D3D11_MAP uiFlags;
	TUINT     uiNumIndices = 0;
	TUINT     uiUnk1       = m_uiFlags & 7;

	if ( uiUnk1 == 1 )
	{
		uiFlags                = D3D11_MAP_WRITE;
		a_rLockBuffer.uiOffset = 0;
	}
	else
	{
		if ( uiUnk1 != 2 )
		{
			if ( uiUnk1 != 4 )
			{
				return TFALSE;
			}

			Validate();
			uiNumIndices = a_uiNumIndices;

			if ( m_uiMaxIndices < m_uiOffset + uiNumIndices )
			{
				uiFlags                = D3D11_MAP_WRITE_DISCARD;
				a_rLockBuffer.uiOffset = 0;
				m_uiOffset             = uiNumIndices;
			}
			else
			{
				uiFlags                = D3D11_MAP_WRITE_NO_OVERWRITE;
				a_rLockBuffer.uiOffset = m_uiOffset;
				m_uiOffset += uiNumIndices;
			}
		}
		else
		{
			Validate();
			uiFlags                = D3D11_MAP_WRITE_DISCARD;
			a_rLockBuffer.uiOffset = 0;
		}
	}

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT                  hRes = pRenderer->GetD3D11DeviceContext()->Map(
        (ID3D11Buffer*)m_HALBuffer.pIndexBuffer,
        0,
        uiFlags,
        0,
        &mappedResource
    );

	a_rLockBuffer.pBuffer = (TIndexType*)( mappedResource.pData ) + a_rLockBuffer.uiOffset;

	if ( FAILED( hRes ) )
		TERROR( "Couldn\'t lock stream index buffer\n" );

	m_uiLockCount += 1;
	return TTRUE;
}

MEMBER_HOOK( 0x006c0120, Toshi::TIndexBlockResource, TIndexBlockResource_Unlock, void )
{
	remaster::RenderDX11* pRenderer = remaster::g_pRender;

	TASSERT( 0 != m_uiLockCount );

	if ( m_uiLockCount > 0 )
	{
		pRenderer->GetD3D11DeviceContext()->Unmap(
		    (ID3D11Buffer*)m_HALBuffer.pIndexBuffer,
		    0
		);

		m_uiLockCount -= 1;
	}
}

void remaster::SetupRenderHooks_IndexBlock()
{
	InstallHook<TIndexBlockResource_DestroyHAL>();
	InstallHook<TIndexBlockResource_CreateHAL>();
	InstallHook<TIndexBlockResource_Lock>();
	InstallHook<TIndexBlockResource_Unlock>();
}
