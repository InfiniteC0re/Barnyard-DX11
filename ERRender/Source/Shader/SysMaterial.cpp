#include "pch.h"
#include "SysMaterial.h"
#include "Resource/ClassPatcher.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

TDEFINE_CLASS_PATCHED( remaster::SysMaterial, 0x0079a498 );

remaster::SysMaterial::SysMaterial()
    : m_pAssignedOrderTable( TNULL )
{
}

remaster::SysMaterial::~SysMaterial()
{
}

void remaster::SysMaterial::PreRender()
{
}

void remaster::SysMaterial::PostRender()
{
}

TBOOL remaster::SysMaterial::Create( BLENDMODE a_eBlendMode )
{
	SetBlendMode( a_eBlendMode );
	ASysMaterial::SetBlendMode( a_eBlendMode );

	return TMaterial::Create();
}

void remaster::SysMaterial::SetBlendMode( BLENDMODE a_eBlendMode )
{
}

void remaster::SysMaterial::SetOrderTable( Toshi::TOrderTable* a_pOrderTable )
{
	if ( a_pOrderTable != m_pAssignedOrderTable )
	{
		if ( m_pAssignedOrderTable )
		{
			TOrderTable::DeregisterMaterial( GetRegMaterial() );
		}

		if ( a_pOrderTable )
		{
			a_pOrderTable->RegisterMaterial( this );
		}

		m_pAssignedOrderTable = a_pOrderTable;
	}
}
