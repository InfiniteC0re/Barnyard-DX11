#include "pch.h"
#include "OrderTable.h"
#include "RenderDX11.h"

#include <AHooks.h>
#include <HookHelpers.h>

#include <Render/TOrderTable.h>
#include <Render/TShader.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

HOOK( 0x006d5a60, TOrderTable_CreateStaticData, void, TUINT a_uiMaxMaterials, TUINT a_uiMaxRenderPackets )
{
	Toshi::TOrderTable::CreateStaticData( a_uiMaxMaterials, a_uiMaxRenderPackets * 2 );
}

MEMBER_HOOK( 0x006d58e0, TOrderTable, TOrderTable_Create, void, Toshi::TShader* a_pShader, TINT a_iPriority )
{
	m_pLastRegMat = TNULL;
	m_pShader     = a_pShader;
	remaster::g_pRender->GetOrderTables().Insert( this, a_iPriority );
}

MEMBER_HOOK( 0x006d5be0, TOrderTable, TOrderTable_RegisterMaterial, void, Toshi::TMaterial* a_pMaterial )
{
	RegisterMaterial( a_pMaterial );
}

MEMBER_HOOK( 0x006d5c60, TOrderTable, TOrderTable_DeregisterMaterial, void, Toshi::TRegMaterial* a_pRegMat )
{
	DeregisterMaterial( a_pRegMat );
}

MEMBER_HOOK( 0x006d5970, TOrderTable, TOrderTable_Flush, void )
{
	TPROFILER_SCOPE();

	if ( s_uiMaxNumRenderPackets < s_uiNumRenderPackets )
	{
		s_uiMaxNumRenderPackets = s_uiNumRenderPackets;
	}

	if ( m_pLastRegMat != TNULL )
	{
		m_pShader->StartFlush();

		for ( auto it = m_pLastRegMat; it != TNULL; it = it->GetNextRegMat() )
		{
			it->Render();
		}

		m_pShader->EndFlush();
	}

	s_uiNumRenderPackets = 0;
	m_pLastRegMat        = TNULL;
}

MEMBER_HOOK( 0x006d5910, TOrderTable, TOrderTable_Render, void )
{
	TPROFILER_SCOPE();

	if ( m_pLastRegMat != TNULL )
	{
		for ( auto it = m_pLastRegMat; it != TNULL; it = it->GetNextRegMat() )
		{
			it->Render();
		}
	}

	s_uiNumRenderPackets = 0;
	m_pLastRegMat        = TNULL;
}

MEMBER_HOOK( 0x006d5d60, TOrderTable, TOrderTable_Destructor, void )
{
	( (TOrderTable*)( this ) )->~TOrderTable();
}

void remaster::SetupRenderHooks_OrderTable()
{
	InstallHook<TOrderTable_CreateStaticData>();
	InstallHook<TOrderTable_Create>();
	InstallHook<TOrderTable_RegisterMaterial>();
	InstallHook<TOrderTable_DeregisterMaterial>();
	InstallHook<TOrderTable_Flush>();
	InstallHook<TOrderTable_Render>();
	InstallHook<TOrderTable_Destructor>();
}
