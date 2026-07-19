#include "pch.h"
#include "StaticLightsFile.h"
#include "LightManager.h"
#include "CubemapAnchors.h"

#include <ToshiTools/tinyxml2.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

static TBOOL NamesEqual( const TCHAR* a, const TCHAR* b )
{
	if ( !a || !b )
		return TFALSE;

	for ( ;; ++a, ++b )
	{
		TCHAR ca = *a, cb = *b;
		if ( ca >= 'A' && ca <= 'Z' ) ca += ( 'a' - 'A' );
		if ( cb >= 'A' && cb <= 'Z' ) cb += ( 'a' - 'A' );
		if ( ca != cb )
			return TFALSE;
		if ( ca == '\0' )
			return TTRUE;
	}
}

void remaster::StaticLights_LoadForCurrentLevel( const TCHAR* a_szPath )
{
	if ( !g_pLightManager )
		return;

	g_pLightManager->ClearStaticPointLights();

	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	if ( iLevel < 0 )
		return;
	const TCHAR* szLevelName = CubemapAnchors_GetLevelName( iLevel );

	tinyxml2::XMLDocument oDoc;
	if ( oDoc.LoadFile( a_szPath ) != tinyxml2::XML_SUCCESS )
		return;

	const tinyxml2::XMLElement* pRoot = oDoc.FirstChildElement( "staticLights" );
	if ( !pRoot )
		return;

	for ( const tinyxml2::XMLElement* pLevel = pRoot->FirstChildElement( "level" );
	      pLevel;
	      pLevel = pLevel->NextSiblingElement( "level" ) )
	{
		const TCHAR* szName = pLevel->Attribute( "name" );
		if ( !szName || !NamesEqual( szName, szLevelName ) )
			continue;

		for ( const tinyxml2::XMLElement* pElem = pLevel->FirstChildElement( "light" );
		      pElem;
		      pElem = pElem->NextSiblingElement( "light" ) )
		{
			StaticPointLight oLight = {};
			oLight.vPosition = TVector4(
			    pElem->FloatAttribute( "x", 0.0f ),
			    pElem->FloatAttribute( "y", 0.0f ),
			    pElem->FloatAttribute( "z", 0.0f ),
			    pElem->FloatAttribute( "radius", 10.0f ) ); // w = radius
			oLight.vColor = TVector4(
			    pElem->FloatAttribute( "r", 1.0f ),
			    pElem->FloatAttribute( "g", 1.0f ),
			    pElem->FloatAttribute( "b", 1.0f ),
			    pElem->FloatAttribute( "intensity", 1.0f ) ); // w = intensity

			oLight.uiFlags = 0;
			if ( pElem->BoolAttribute( "enabled", true ) )
				oLight.uiFlags |= STATIC_LIGHT_ENABLED;
			if ( pElem->BoolAttribute( "nightOnly", false ) )
				oLight.uiFlags |= STATIC_LIGHT_NIGHT_ONLY;

			// Building-light group that gates this light (-1 = always on); see LightManager
			oLight.iLightMag = TINT8( pElem->IntAttribute( "lightMag", STATIC_LIGHT_NO_MAG ) );

			if ( g_pLightManager->AddStaticPointLight( oLight ) < 0 )
				break;
		}

		break;
	}
}

void remaster::StaticLights_SaveCurrentLevel( const TCHAR* a_szPath )
{
	if ( !g_pLightManager )
		return;

	const TINT iLevel = CubemapAnchors_GetCurrentLevel();
	if ( iLevel < 0 )
		return;
	const TCHAR* szLevelName = CubemapAnchors_GetLevelName( iLevel );

	// Load the existing file first so other levels' lights survive the write
	tinyxml2::XMLDocument oDoc;
	oDoc.LoadFile( a_szPath ); // ignore failure: a missing file just means a fresh document

	tinyxml2::XMLElement* pRoot = oDoc.FirstChildElement( "staticLights" );
	if ( !pRoot )
	{
		pRoot = oDoc.NewElement( "staticLights" );
		oDoc.InsertFirstChild( pRoot );
	}

	for ( tinyxml2::XMLElement* pLevel = pRoot->FirstChildElement( "level" ); pLevel; )
	{
		tinyxml2::XMLElement* pNext = pLevel->NextSiblingElement( "level" );
		const TCHAR*          szName = pLevel->Attribute( "name" );
		if ( szName && NamesEqual( szName, szLevelName ) )
			pRoot->DeleteChild( pLevel );
		pLevel = pNext;
	}

	// Skip the <level> element entirely when empty, so an emptied level leaves no stray entry
	const TINT iCount = g_pLightManager->GetStaticPointLightCount();
	if ( iCount > 0 )
	{
		tinyxml2::XMLElement* pLevel = oDoc.NewElement( "level" );
		pLevel->SetAttribute( "name", szLevelName );

		for ( TINT i = 0; i < iCount; i++ )
		{
			const StaticPointLight& rLight = g_pLightManager->GetStaticPointLight( i );
			tinyxml2::XMLElement*   pElem   = oDoc.NewElement( "light" );
			pElem->SetAttribute( "x", rLight.vPosition.x );
			pElem->SetAttribute( "y", rLight.vPosition.y );
			pElem->SetAttribute( "z", rLight.vPosition.z );
			pElem->SetAttribute( "radius", rLight.vPosition.w );
			pElem->SetAttribute( "r", rLight.vColor.x );
			pElem->SetAttribute( "g", rLight.vColor.y );
			pElem->SetAttribute( "b", rLight.vColor.z );
			pElem->SetAttribute( "intensity", rLight.vColor.w );
			pElem->SetAttribute( "enabled", ( rLight.uiFlags & STATIC_LIGHT_ENABLED ) != 0 );
			pElem->SetAttribute( "nightOnly", ( rLight.uiFlags & STATIC_LIGHT_NIGHT_ONLY ) != 0 );
			pElem->SetAttribute( "lightMag", TINT( rLight.iLightMag ) );
			pLevel->InsertEndChild( pElem );
		}

		pRoot->InsertEndChild( pLevel );
	}

	oDoc.SaveFile( a_szPath );
}
