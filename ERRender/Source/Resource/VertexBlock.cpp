#include "pch.h"
#include "VertexBlock.h"
#include "RenderDX11.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <Platform/DX8/TVertexBlockResource_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

MEMBER_HOOK( 0x006c0760, Toshi::TVertexBlockResource, TVertexBlockResource_DestroyHAL, void )
{
	for ( TUINT i = 0; i < m_HALBuffer.uiNumStreams; i++ )
	{
		if ( m_HALBuffer.apVertexBuffers[ i ] )
		{
			m_HALBuffer.apVertexBuffers[ i ]->Release();
			m_HALBuffer.apVertexBuffers[ i ] = TNULL;
		}
	}

	m_HALBuffer.uiNumStreams = 0;
}

MEMBER_HOOK( 0x006c0970, Toshi::TVertexBlockResource, TVertexBlockResource_CreateHAL, TBOOL )
{
	TPROFILER_SCOPE();

	CALL_THIS( 0x006c0760, Toshi::TVertexBlockResource*, void, this ); // DestroyHAL()

	remaster::RenderDX11*       pRenderer    = remaster::g_pRender;
	const TVertexFactoryFormat& vertexFormat = m_pFactory->GetVertexFormat();
	m_HALBuffer.uiNumStreams                 = vertexFormat.m_uiNumStreams;

	for ( TUINT i = 0; i < m_HALBuffer.uiNumStreams; i++ )
	{
		D3D11_USAGE usage = D3D11_USAGE_DEFAULT;

		if ( ISZERO( m_uiFlags & 1 ) )
		{
			usage      = D3D11_USAGE_DYNAMIC;
			m_uiOffset = 0;
		}

		D3D11_BUFFER_DESC bufferDesc;
		bufferDesc.ByteWidth           = vertexFormat.m_aStreamFormats[ i ].m_uiVertexSize * m_uiMaxVertices;
		bufferDesc.Usage               = usage;
		bufferDesc.BindFlags           = D3D11_BIND_VERTEX_BUFFER;
		bufferDesc.CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE;
		bufferDesc.MiscFlags           = 0;
		bufferDesc.StructureByteStride = vertexFormat.m_aStreamFormats[ i ].m_uiVertexSize;

		HRESULT hRes = pRenderer->GetD3D11Device()->CreateBuffer( &bufferDesc, TNULL, (ID3D11Buffer**)&m_HALBuffer.apVertexBuffers[ i ] );

		if ( FAILED( hRes ) )
		{
			TERROR( "Unable to create new vertex buffer!\n" );
			return TFALSE;
		}
	}

	return TTRUE;
}

MEMBER_HOOK( 0x006c0810, Toshi::TVertexBlockResource, TVertexBlockResource_Lock, TBOOL, TVertexPoolResourceInterface::LockBuffer& a_rLockBuffer, TUINT32 a_uiNumVertices )
{
	TPROFILER_SCOPE();
	remaster::RenderDX11* pRenderer = remaster::g_pRender;

	const TVertexFactoryFormat& vertexFormat = m_pFactory->GetVertexFormat();
	a_rLockBuffer.uiNumStreams               = vertexFormat.m_uiNumStreams;

	D3D11_MAP uiFlags;
	TUINT     uiNumVertices = 0;
	TUINT     uiUnk1        = m_uiFlags & 7;

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
			uiNumVertices = a_uiNumVertices;

			if ( m_uiMaxVertices < m_uiOffset + uiNumVertices )
			{
				uiFlags                = D3D11_MAP_WRITE_DISCARD;
				a_rLockBuffer.uiOffset = 0;
				m_uiOffset             = uiNumVertices;
			}
			else
			{
				uiFlags                = D3D11_MAP_WRITE_NO_OVERWRITE;
				a_rLockBuffer.uiOffset = m_uiOffset;
				m_uiOffset += uiNumVertices;
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
	for ( TUINT i = 0; i < a_rLockBuffer.uiNumStreams; i++ )
	{
		HRESULT hRes = pRenderer->GetD3D11DeviceContext()->Map(
		    (ID3D11Buffer*)m_HALBuffer.apVertexBuffers[ i ],
		    0,
		    uiFlags,
		    0,
		    &mappedResource
		);

		a_rLockBuffer.apStreams[ i ] = (TBYTE*)( mappedResource.pData ) + a_rLockBuffer.uiOffset * vertexFormat.m_aStreamFormats[ i ].m_uiVertexSize;

		if ( FAILED( hRes ) )
			TERROR( "Couldn\'t lock stream vertex buffer\n" );
	}

	m_uiLockCount += 1;
	return TTRUE;
}

MEMBER_HOOK( 0x006c0620, Toshi::TVertexBlockResource, TVertexBlockResource_Unlock, void )
{
	TPROFILER_SCOPE();
	TASSERT( 0 != m_uiLockCount );

	if ( m_uiLockCount > 0 )
	{
		remaster::RenderDX11* pRenderer = remaster::g_pRender;

		for ( TUINT i = 0; i < m_pFactory->GetVertexFormat().m_uiNumStreams; i++ )
		{
			pRenderer->GetD3D11DeviceContext()->Unmap(
			    (ID3D11Buffer*)m_HALBuffer.apVertexBuffers[ i ],
			    0
			);
		}

		m_uiLockCount -= 1;
	}
}

void remaster::SetupRenderHooks_VertexBlock()
{
	InstallHook<TVertexBlockResource_DestroyHAL>();
	InstallHook<TVertexBlockResource_CreateHAL>();
	InstallHook<TVertexBlockResource_Lock>();
	InstallHook<TVertexBlockResource_Unlock>();
}
