#include "pch.h"
#include "IndexBlock.h"
#include "RenderDX11.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <Platform/DX8/TIndexPoolResource_DX8.h>
#include <Platform/DX8/TIndexBlockResource_DX8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

constexpr TINT INDEX_SIZE = 2;

MEMBER_HOOK( 0x006c0280, Toshi::TIndexBlockResource, TIndexBlockResource_DestroyHAL, void )
{
	if ( m_HALBuffer.pIndexBuffer )
	{
		m_HALBuffer.pIndexBuffer->Release();
		m_HALBuffer.pIndexBuffer = TNULL;
	}
}

MEMBER_HOOK( 0x006c0350, Toshi::TIndexBlockResource, TIndexBlockResource_CreateHAL, TBOOL )
{
	if ( m_HALBuffer.pIndexBuffer )
	{
		m_HALBuffer.pIndexBuffer->Release();
		m_HALBuffer.pIndexBuffer = TNULL;
	}

	remaster::RenderDX11* pRenderer = remaster::g_pRender;
	D3D11_USAGE           usage     = D3D11_USAGE_DYNAMIC;

	if ( ISZERO( m_uiFlags & 1 ) )
	{
		//usage      = D3D11_USAGE_DYNAMIC;
		m_uiOffset = 0;
	}

	D3D11_BUFFER_DESC bufferDesc;
	bufferDesc.ByteWidth           = m_uiMaxIndices * sizeof( TIndexType );
	bufferDesc.Usage               = usage;
	bufferDesc.BindFlags           = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_INDEX_BUFFER;
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
		uiFlags                = D3D11_MAP_WRITE_DISCARD;
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
				m_uiOffset             = TAlignNumUp( uiNumIndices, INDEX_SIZE );
			}
			else
			{
				uiFlags                = D3D11_MAP_WRITE_NO_OVERWRITE;
				a_rLockBuffer.uiOffset = m_uiOffset;
				m_uiOffset             = TAlignNumUp( uiNumIndices, INDEX_SIZE );
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

MEMBER_HOOK( 0x006c0730, Toshi::TIndexBlockResource, TIndexBlockResource_AttachPool, TBOOL, TIndexPoolResource* a_pPool )
{
	TVALIDPTR( a_pPool );

	if ( a_pPool->GetClass() == (TClass*)0x007d3138 )
	{
		// IndexPoolResource
		m_uiIndicesUsed += TAlignNumUp( a_pPool->GetNumIndices(), INDEX_SIZE );
	}
	else
	{
		// VertexPoolResource
		m_uiIndicesUsed += a_pPool->GetNumIndices();
	}

	a_pPool->SetParent( this );

	if ( m_uiFlags & 1 )
		Invalidate();

	return TTRUE;
}

MEMBER_HOOK( 0x006c0150, Toshi::TIndexBlockResource, TIndexBlockResource_CanFit, TBOOL, TIndexPoolResource* a_pPool )
{
	TVALIDPTR( a_pPool );

	if ( !HASANYFLAG( m_uiFlags, 1 ) )
	{
		if ( HASANYFLAG( m_uiFlags, 4 ) && HASANYFLAG( a_pPool->GetFlags(), 4 ) )
		{
			return TTRUE;
		}
	}
	else if ( HASANYFLAG( a_pPool->GetFlags(), 1 ) )
	{
		return m_uiMaxIndices > TAlignNumUp( a_pPool->GetNumIndices(), INDEX_SIZE ) + m_uiIndicesUsed;
	}

	return TFALSE;
}

MEMBER_HOOK( 0x006d6040, Toshi::TIndexPoolResource, TIndexPoolResource_Lock, TBOOL, Toshi::TIndexPoolResource::LockBuffer* a_pLockBuffer )
{
	TVALIDPTR( a_pLockBuffer );

	auto uiOldLockCount = m_uiLockCount;
	m_uiNumLocksAllTime += 1;
	m_uiLockCount += 1;

	if ( uiOldLockCount == 0 )
	{
		TUINT8 uiUnk1 = m_uiFlags & 7;

		if ( uiUnk1 == 1 )
		{
			a_pLockBuffer->pBuffer = m_pIndices;
			return TTRUE;
		}
		else if ( uiUnk1 == 2 )
		{
			Validate();

			if ( CALL_THIS( 0x006d5fa0, TIndexPoolResource*, TIndexBlockResource_Lock::_hook_obj*, this )->_hook_func( *a_pLockBuffer, 0 ) )
			{
				m_uiIndexOffset = a_pLockBuffer->uiOffset;
				return TTRUE;
			}
		}
		else if ( uiUnk1 == 4 )
		{
			Validate();

			if ( CALL_THIS( 0x006d5fa0, TIndexPoolResource*, TIndexBlockResource_Lock::_hook_obj*, this )->_hook_func( *a_pLockBuffer, GetMaxIndices() ) )
			{
				m_uiIndexOffset = a_pLockBuffer->uiOffset;
				return TTRUE;
			}
		}
	}

	return TFALSE;
}

using BlockPoolPair = T2Pair<TIndexBlockResource*, TIndexPoolResourceInterface::LockBuffer>;

HOOK( 0x006bfff0, TIndexBlockResource_Validate_Recurse, TBOOL, TResource* a_pResource, BlockPoolPair* a_pUserData )
{
	if ( a_pResource->IsExactly( (TClass*)0x007d3138 ) )
	{
		auto pPool = TSTATICCAST( TIndexPoolResource, a_pResource );
		auto pPair = TSTATICCAST( BlockPoolPair, a_pUserData );

		if ( pPool->GetFlags() & 1 )
		{
			pPool->m_uiIndexOffset = pPair->second.uiOffset;
			pPair->second.uiOffset += TAlignNumUp( pPool->GetNumIndices(), INDEX_SIZE );

			TUtil::MemCopy(
			    pPair->second.pBuffer + pPool->m_uiIndexOffset,
			    pPool->GetIndices(),
			    pPool->GetNumIndices() * sizeof( TIndexType )
			);
		}
	}

	return TTRUE;
}

void remaster::SetupRenderHooks_IndexBlock()
{
	InstallHook<TIndexBlockResource_DestroyHAL>();
	InstallHook<TIndexBlockResource_CreateHAL>();
	InstallHook<TIndexBlockResource_Lock>();
	InstallHook<TIndexBlockResource_Unlock>();
	InstallHook<TIndexBlockResource_AttachPool>();
	InstallHook<TIndexBlockResource_CanFit>();
	InstallHook<TIndexPoolResource_Lock>();
	InstallHook<TIndexBlockResource_Validate_Recurse>();
}
