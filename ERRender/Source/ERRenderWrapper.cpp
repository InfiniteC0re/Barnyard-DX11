#include "pch.h"
#include "RenderDX11.h"
#include "RenderParams.h"
#include "MaterialParams.h"
#include "Shader/GrassShader.h"
#include "Shader/SkinShader.h"
#include "Shader/SkinMesh.h"
#include "Shader/WorldShader.h"
#include "Shader/StaticInstanceShader.h"
#include "Shader/SysShader.h"
#include "Ref/AWorld.h"
#include "Ref/AWorldVIS.h"
#include "Resource/TextureResource.h"
#include "Resource/Viewport.h"
#include "Resource/OrderTable.h"
#include "Resource/ClassPatcher.h"
#include "Resource/VertexBlock.h"
#include "Resource/IndexBlock.h"
#include "UI/UIRenderer.h"
#include "UI/FontRenderer.h"
#include "LightData.h"

#include "Generated/SkyMaskShaderCombos.h"
#include "Generated/SunShaftsShaderCombos.h"
#include "Generated/CopyTextureShaderCombos.h"
#include "Generated/ResolveDepthShaderCombos.h"
#include "Generated/DownsampleDepthMinShaderCombos.h"
#include "Generated/DualKawaseDownShaderCombos.h"
#include "Generated/DualKawaseUpShaderCombos.h"
#include "Generated/GlowBloomCompositeShaderCombos.h"
#include "Generated/HDRBloomThresholdShaderCombos.h"
#include "Generated/HBAOPlusShaderCombos.h"
#include "Generated/XeGTAOShaderCombos.h"
#include "Generated/SSRShaderCombos.h"
#include "Generated/HBAOBlurShaderCombos.h"
#include "Generated/HBAOCompositeShaderCombos.h"
#include "Generated/VolumetricFogShaderCombos.h"
#include "Generated/VolumetricFogCompositeShaderCombos.h"
#include "Generated/CloudShadowShaderCombos.h"
#include "RenderContentDX11.h"
#include "LightManager.h"
#include "CubemapAnchors.h"
#include "StaticLightsFile.h"
#include "Editor.h"
#include "SkyCube.h"

#include <Toshi/TTask.h>
#include <Render/TTMDWin.h>

#include <AHooks.h>
#include <HookHelpers.h>
#include <BYardSDK/ACamera.h>
#include <BYardSDK/ARenderer.h>
#include <BYardSDK/AGlowViewport.h>
#include <BYardSDK/AModel.h>
#include <Render/TModel.h>

#include <Platform/DX8/TRenderInterface_DX8.h>
#include <Render/TVertexFactoryResourceInterface.h>
#include <Platform/DX8/TVertexPoolResource_DX8.h>
#include <Platform/DX8/TIndexPoolResource_DX8.h>
#include "Ref/AWorldShader/AWorldMesh.h"
#include "DirectXTex/DirectXTex.h"
#include <File/TTRB.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

namespace remaster
{

TBOOL  g_bSunShaftsEnabled       = TTRUE;
TFLOAT g_flSunShaftsAlpha        = 0.04f;
TFLOAT g_flSunShaftsRaysLength   = 0.3f;
TFLOAT g_flSunShaftsTint[ 3 ]    = { 1.0f, 0.91f, 0.8f };
TINT   g_iSunShaftsKawaseLevels  = 1;
TFLOAT g_flSunShaftsKawaseOffset = 1.0f;

TBOOL  g_bGlowBloomEnabled       = TTRUE;
TINT   g_iGlowBloomKawaseLevels  = 2;
TFLOAT g_flGlowBloomKawaseOffset = 1.7f;
TFLOAT g_flGlowBloomIntensity    = 0.715f;

TBOOL  g_bHDRBloomEnabled       = TTRUE;
TINT   g_iHDRBloomKawaseLevels  = 2;
TFLOAT g_flHDRBloomKawaseOffset = 1.7f;
TFLOAT g_flHDRBloomThreshold    = 1.05f;
TFLOAT g_flHDRBloomIntensity    = 4.0f;

TBOOL g_bHBAOEnabled = TTRUE;
TBOOL g_bHBAODebug   = TFALSE;

TBOOL  g_bSSREnabled       = TTRUE;
TBOOL  g_bSSRDebug         = TFALSE;
TBOOL  g_bSSRDebugNormals  = TFALSE;
TFLOAT g_flSSRIntensity    = 0.3f;
TFLOAT g_flSSRMaxDistance  = 70.0f;
TFLOAT g_flSSRThickness    = 0.3f;
TFLOAT g_flSSRStepSize     = 1.0f;
TINT   g_iSSRMaxSteps      = 50;
TFLOAT g_flSSRFresnelPower = 4.0f;
TFLOAT g_flSSREdgeFade     = 2.0f;

TBOOL  g_bSkyCubeEnabled    = TTRUE;
TBOOL  g_bSkyCubeDebugView  = TFALSE;
TFLOAT g_flSkyCubeIntensity = 1.0f;
TBOOL  g_bReflectTerrain    = TTRUE; // terrain in the probe is dynamic-lit, no shadows
// Parallax box half-extents for the camera-follow fallback (level has no authored anchor)
TFLOAT g_flSkyCubeParallaxHorizontal = 40.0f; // world units (X/Z)
TFLOAT g_flSkyCubeParallaxVertical   = 20.0f; // world units (Y)

TBOOL g_bDebugTangents = TFALSE;

// Blue vertex-color channel drives per-vertex strength; per-material opt-in via the XML "wind" flag
TBOOL  g_bWindEnabled                     = TTRUE;
TFLOAT g_flWindStrength                   = 0.15f;          // max world-unit sway at blue = 1
TFLOAT g_flWindSpeed                      = 1.5f;           // phase advance per second
TFLOAT g_flWindDir[ 2 ]                   = { 1.0f, 0.0f }; // world-space XZ sway direction
TFLOAT g_flWindTime                       = 0.0f;           // accumulated phase (updated per frame)
TINT   g_iAOAlgorithm                     = 0;
TFLOAT g_flHBAORadius                     = 0.7f;
TFLOAT g_flHBAOSceneScale                 = 1.0f;
TFLOAT g_flHBAOBias                       = 0.10f;
TFLOAT g_flHBAOIntensity                  = 1.0f;
TFLOAT g_flHBAOPower                      = 1.0f;
TFLOAT g_flHBAOBlurSharpness              = 4.0f;
TFLOAT g_flXeGTAORadiusMultiplier         = 1.457f;
TFLOAT g_flXeGTAOFalloffRange             = 0.9f;
TFLOAT g_flXeGTAOSampleDistributionPower  = 2.8f;
TFLOAT g_flXeGTAOThinOccluderCompensation = 0.5f;

TBOOL  g_bVolumetricFogEnabled       = TTRUE;
TINT   g_iVolumetricFogCompositeMode = 0;
TFLOAT g_flVolumetricFogDensity      = 0.019f;
TFLOAT g_flVolumetricFogG            = 0.0f;
TFLOAT g_flVolumetricFogMaxDist      = 44.0f;
TFLOAT g_flVolumetricFogIntensity    = 0.20f;
TFLOAT g_flVolumetricFogColor[ 3 ]   = { 0.937f, 0.8f, 0.5254f };
// Tint distance-fog colour by the authored colour so the volumetrics track the level's fog palette
TBOOL  g_bVolumetricFogUseSceneColor  = TFALSE;
TFLOAT g_flVolumetricFogNoiseScale    = 0.05f;
TFLOAT g_flVolumetricFogNoiseStrength = 1.25f;
TFLOAT g_flVolumetricFogWindDir[ 2 ]  = { -1.0f, -1.3f };
TFLOAT g_flVolumetricFogWindSpeed     = 1.0f;
TFLOAT g_flVolumetricFogHeight        = 0.0f; // bottom of the fog (full density at/below)
TFLOAT g_flVolumetricFogTopHeight     = 0.0f; // top of the fog (0 at/above; <= bottom disables the height band)

static Toshi::T2Map<TUINT32, void*, MaterialHashComparator> s_oTextureCache;

// Empties the SRV cache so the next LoadCachedTexture re-reads from disk (material hot-reload)
void ClearTextureCache()
{
	for ( auto it = s_oTextureCache.Begin(); it != s_oTextureCache.End(); it++ )
	{
		auto* pSRV = (ID3D11ShaderResourceView*)it.GetValue()->GetSecond();
		if ( pSRV ) pSRV->Release();
	}
	s_oTextureCache.Clear();
}

// Load Data\Textures\<name> into a D3D11 SRV, cached by name; TNULL on a missing/bad file
void* LoadCachedTexture( const TCHAR* a_szName )
{
	const TUINT32 uHash = HashMaterialName( a_szName );
	auto          it    = s_oTextureCache.Find( uHash );
	if ( s_oTextureCache.IsValid( it ) )
		return it.GetValue()->GetSecond();

	void* pResult = TNULL;

	Toshi::TString8 oPath = TEXTURE_DIR;
	oPath += a_szName;

	Toshi::TFile* pFile = Toshi::TFile::Create( oPath, Toshi::TFILEMODE_READ );
	if ( pFile )
	{
		const TUINT uiSize = pFile->GetSize();
		void*       pData  = TMalloc( uiSize );

		if ( pData && pFile->Read( pData, uiSize ) == uiSize )
		{
			DirectX::TexMetadata  oMeta;
			DirectX::ScratchImage oImage;

			const uint8_t* pBytes = static_cast<const uint8_t*>( pData );
			HRESULT        hRes   = DirectX::LoadFromDDSMemory( pBytes, uiSize, DirectX::DDS_FLAGS_NONE, &oMeta, oImage );
			if ( FAILED( hRes ) ) hRes = DirectX::LoadFromTGAMemory( pBytes, uiSize, DirectX::TGA_FLAGS_NONE, &oMeta, oImage );
			// IGNORE_SRGB: normal/roughness data is linear, never gamma-encoded
			if ( FAILED( hRes ) ) hRes = DirectX::LoadFromWICMemory( pBytes, uiSize, DirectX::WIC_FLAGS_IGNORE_SRGB, &oMeta, oImage );

			if ( SUCCEEDED( hRes ) )
			{
				ID3D11ShaderResourceView* pSRV = TNULL;
				DirectX::CreateShaderResourceView( g_pRender->GetD3D11Device(), oImage.GetImages(), oImage.GetImageCount(), oMeta, &pSRV );
				pResult = pSRV;
			}
		}

		if ( pData ) TFree( pData );
		pFile->Destroy();
	}

	s_oTextureCache.Insert( uHash, pResult ); // cache misses too, to avoid repeated disk hits
	return pResult;
}

} // namespace remaster

static TBOOL __stdcall LoadTRBModelCallback( TModel* a_pModel )
{
	TPROFILER_SCOPE();

	TTMDWin::TRBWinHeader*     pHeader    = a_pModel->CastSymbol<TTMDWin::TRBWinHeader>( "Header" );
	TTMDBase::MaterialsHeader* pMaterials = a_pModel->CastSymbol<TTMDBase::MaterialsHeader>( "Materials" );

	a_pModel->m_iLODCount       = pHeader->m_iNumLODs;
	a_pModel->m_fRenderDistance = pHeader->m_fLODDistance;

	TTMDBase::SHADERTYPE* pShaderTypes = TSTATICCAST( TTMDBase::SHADERTYPE, alloca( pHeader->m_iNumLODs * sizeof( TTMDBase::SHADERTYPE ) ) );

	for ( TINT i = 0; i < pHeader->m_iNumLODs; i++ )
	{
		auto pTRBLod = pHeader->GetLOD( i );

		pShaderTypes[ i ] = pTRBLod->m_eShader;

		// These shader types aren't supported on Windows; remap them so we can adjust material parameters
		switch ( pTRBLod->m_eShader )
		{
			case TTMDBase::SHADERTYPE_STATICINSTANCE:
				pTRBLod->m_eShader = TTMDBase::SHADERTYPE_SKIN;
				break;
		}
	}

	return TREINTERPRETCAST( TModel::t_ModelLoaderTRBCallback, 0x006114d0 )( a_pModel );
}

HOOK( 0x006c6d60, TRenderD3DInterface_CreateObject, TRenderInterface* )
{
	return new remaster::RenderDX11;
}

MEMBER_HOOK( 0x006c72a0, remaster::RenderDX11, TRenderD3DInterface_Create, TBOOL, const char* a_pchWindowTitle )
{
	TModel::SetLoaderTRBCallback( LoadTRBModelCallback );

	return Create( "Barnyard Remastered" );
}

MEMBER_HOOK( 0x006c58e0, remaster::RenderDX11, TRenderD3DInterface_BeginEndScene, void )
{
}

MEMBER_HOOK( 0x006be990, remaster::RenderDX11, TRenderD3DInterface_FlushShaders, void )
{
	TPROFILER_SCOPE();

	FlushOrderTables();

	for ( auto it = TShader::sm_oShaderList.GetRootShader(); it != TNULL; it = it->GetNextShader() )
	{
		it->Flush();
	}
}

MEMBER_HOOK( 0x006d68b0, TD3DAdapter, TD3DAdapter_Mode_Device_SupportsVSConstants, TBOOL )
{
	return TTRUE;
}

HOOK( 0x005e83e0, RenderCellMeshWin, void, CellMeshSphere* a_pMeshSphere, RenderData* a_pRenderData )
{
	TPROFILER_SCOPE();

	TVALIDPTR( a_pMeshSphere );

	const TBOOL bNormalPass = !remaster::g_pRender->GetCSMManager().IsRenderingShadowPass();

	ATerrainInterface* pTerrainInterface = ATerrainInterface::GetSingleton();
	TMesh*             pMesh             = a_pMeshSphere->m_pCellMesh->pMesh;

	TFLOAT fLightMag = 0.6f;
	for ( TUINT i = 0; i < 12; i++ )
	{
		if ( pTerrainInterface->m_apLightMagMaterials[ i ] == pMesh->GetMaterial() )
		{
			fLightMag = pTerrainInterface->m_afLightMags[ i ];

			if ( fLightMag <= 0.0f )
				return;

			break;
		}
	}

	auto pContext = TSTATICCAST( remaster::RenderContextD3D11, g_pRender->GetCurrentContext() );

	if ( bNormalPass )
	{
		TSphere oBounding(
		    a_pMeshSphere->m_BoundingSphere.AsVector4().x,
		    -a_pMeshSphere->m_BoundingSphere.AsVector4().z,
		    a_pMeshSphere->m_BoundingSphere.AsVector4().y,
		    a_pMeshSphere->m_BoundingSphere.GetRadius()
		);

		TLightIDList oLightIdList;
		AGlowViewport::GetSingleton()->GetInfluencingLightIDs( oBounding, oLightIdList );

		pContext->ClearLightIDs();
		if ( oLightIdList[ 0 ] >= 0 ) pContext->AddLight( oLightIdList[ 0 ] );
		if ( oLightIdList[ 1 ] >= 0 ) pContext->AddLight( oLightIdList[ 1 ] );
		if ( oLightIdList[ 2 ] >= 0 ) pContext->AddLight( oLightIdList[ 2 ] );
		if ( oLightIdList[ 3 ] >= 0 ) pContext->AddLight( oLightIdList[ 3 ] );

		TINT8 aStaticLightIds[ MAX_CELL_STATIC_LIGHTS ];
		if ( remaster::g_pLightManager )
			remaster::g_pLightManager->GetInfluencingStaticLightIDs( oBounding, aStaticLightIds );
		else
			for ( TINT i = 0; i < MAX_CELL_STATIC_LIGHTS; i++ ) aStaticLightIds[ i ] = -1;

		pContext->ClearStaticLightIDs();
		for ( TINT i = 0; i < MAX_CELL_STATIC_LIGHTS; i++ )
		{
			if ( aStaticLightIds[ i ] < 0 ) break;
			pContext->AddStaticLight( aStaticLightIds[ i ] );
		}
	}

	TVector4 vecColour{ 0.3f, 0.3f, 0.1952941f, 1.0f };
	vecColour *= fLightMag;

	TSTATICCAST( remaster::WorldShaderDX11, remaster::WorldShaderDX11::GetSingleton() )->SetColours( vecColour, vecColour );
	pMesh->Render();

	if ( bNormalPass )
	{
		pContext->ClearLightIDs();
		pContext->ClearStaticLightIDs();
	}
}

HOOK( 0x005e7d10, RenderCellMeshDefault, void, CellMeshSphere* a_pMeshSphere, RenderData* a_pRenderData )
{
	TPROFILER_SCOPE();

	TVALIDPTR( a_pMeshSphere );

	const TBOOL bNormalPass = !remaster::g_pRender->GetCSMManager().IsRenderingShadowPass();

	auto pContext = TSTATICCAST( remaster::RenderContextD3D11, g_pRender->GetCurrentContext() );

	if ( bNormalPass )
	{
		TSphere oBounding(
		    a_pMeshSphere->m_BoundingSphere.AsVector4().x,
		    -a_pMeshSphere->m_BoundingSphere.AsVector4().z,
		    a_pMeshSphere->m_BoundingSphere.AsVector4().y,
		    a_pMeshSphere->m_BoundingSphere.GetRadius()
		);

		TLightIDList oLightIdList;
		AGlowViewport::GetSingleton()->GetInfluencingLightIDs( oBounding, oLightIdList );

		pContext->ClearLightIDs();
		if ( oLightIdList[ 0 ] >= 0 ) pContext->AddLight( oLightIdList[ 0 ] );
		if ( oLightIdList[ 1 ] >= 0 ) pContext->AddLight( oLightIdList[ 1 ] );
		if ( oLightIdList[ 2 ] >= 0 ) pContext->AddLight( oLightIdList[ 2 ] );
		if ( oLightIdList[ 3 ] >= 0 ) pContext->AddLight( oLightIdList[ 3 ] );

		TINT8 aStaticLightIds[ MAX_CELL_STATIC_LIGHTS ];
		if ( remaster::g_pLightManager )
			remaster::g_pLightManager->GetInfluencingStaticLightIDs( oBounding, aStaticLightIds );
		else
			for ( TINT i = 0; i < MAX_CELL_STATIC_LIGHTS; i++ ) aStaticLightIds[ i ] = -1;

		pContext->ClearStaticLightIDs();
		for ( TINT i = 0; i < MAX_CELL_STATIC_LIGHTS; i++ )
		{
			if ( aStaticLightIds[ i ] < 0 ) break;
			pContext->AddStaticLight( aStaticLightIds[ i ] );
		}
	}

	a_pMeshSphere->m_pCellMesh->pMesh->Render();

	if ( bNormalPass )
	{
		pContext->ClearLightIDs();
		pContext->ClearStaticLightIDs();
	}
}

HOOK( 0x006108c0, AModelInstance_RenderInstanceCallback, void, Toshi::TModelInstance* a_pInstance, void* a_pUserData )
{
	const TBOOL bNormalPass = remaster::g_pLightManager && a_pInstance && a_pUserData &&
	    !remaster::g_pRender->GetCSMManager().IsRenderingShadowPass();

	if ( bNormalPass )
	{
		auto pContext = TSTATICCAST( remaster::RenderContextD3D11, remaster::g_pRender->GetCurrentContext() );
		pContext->ClearStaticLightIDs();

		AModelInstance* pGameModelInstance = TSTATICCAST( AModelInstance, a_pUserData );
		TModel*         pModel             = a_pInstance->GetModel();

		if ( pModel && pGameModelInstance->GetSceneObject() )
		{
			TModelLOD& rLOD = pModel->GetLOD( a_pInstance->GetLOD() );
			if ( rLOD.iNumMeshes > 0 && pGameModelInstance->ReceivesLight() )
			{
				TMatrix44 matTransform;
				pGameModelInstance->GetTransform().GetLocalMatrixImp( matTransform );

				TSphere oBounding = rLOD.BoundingSphere;
				TMatrix44::TransformVector( oBounding.GetOrigin(), matTransform, oBounding.GetOrigin() );

				TINT8 aStaticLightIds[ MAX_CELL_STATIC_LIGHTS ];
				remaster::g_pLightManager->GetInfluencingStaticLightIDs( oBounding, aStaticLightIds );
				for ( TINT i = 0; i < MAX_CELL_STATIC_LIGHTS; i++ )
				{
					if ( aStaticLightIds[ i ] < 0 ) break;
					pContext->AddStaticLight( aStaticLightIds[ i ] );
				}
			}
		}
	}

	CallOriginal( a_pInstance, a_pUserData );

	if ( bNormalPass )
	{
		auto pContext = TSTATICCAST( remaster::RenderContextD3D11, remaster::g_pRender->GetCurrentContext() );
		pContext->ClearStaticLightIDs();
	}
}

struct TangentAccum
{
	TFLOAT tx, ty, tz, bx, by, bz;
};

// Per-vertex tangents (Lengyel's method) for a triangle-strip world mesh: writes float4 per vertex
// (xyz = orthonormal tangent, w = handedness) into a_pOut. Winding-independent; skips degenerate/zero-UV tris
void GenerateWorldMeshTangents( const WorldVertex* a_pVerts, TUINT a_uiNumVerts, const TUINT16* a_pIndices, TUINT a_uiNumIndices, TFLOAT* a_pOut )
{
	TangentAccum* pAccum = new TangentAccum[ a_uiNumVerts ]();

	for ( TUINT i = 0; i + 2 < a_uiNumIndices; i++ )
	{
		const TUINT16 i0 = a_pIndices[ i ], i1 = a_pIndices[ i + 1 ], i2 = a_pIndices[ i + 2 ];
		if ( i0 == i1 || i1 == i2 || i0 == i2 ) continue; // degenerate strip stitch
		if ( i0 >= a_uiNumVerts || i1 >= a_uiNumVerts || i2 >= a_uiNumVerts ) continue;

		const WorldVertex& v0 = a_pVerts[ i0 ];
		const WorldVertex& v1 = a_pVerts[ i1 ];
		const WorldVertex& v2 = a_pVerts[ i2 ];

		const TFLOAT e1x = v1.Position.x - v0.Position.x, e1y = v1.Position.y - v0.Position.y, e1z = v1.Position.z - v0.Position.z;
		const TFLOAT e2x = v2.Position.x - v0.Position.x, e2y = v2.Position.y - v0.Position.y, e2z = v2.Position.z - v0.Position.z;

		const TFLOAT du1 = v1.UV.x - v0.UV.x, dv1 = v1.UV.y - v0.UV.y;
		const TFLOAT du2 = v2.UV.x - v0.UV.x, dv2 = v2.UV.y - v0.UV.y;

		const TFLOAT det = du1 * dv2 - du2 * dv1;
		if ( TMath::Abs( det ) < 1e-8f ) continue; // no UV area
		const TFLOAT r = 1.0f / det;

		const TFLOAT tx = ( e1x * dv2 - e2x * dv1 ) * r, ty = ( e1y * dv2 - e2y * dv1 ) * r, tz = ( e1z * dv2 - e2z * dv1 ) * r;
		const TFLOAT bx = ( e2x * du1 - e1x * du2 ) * r, by = ( e2y * du1 - e1y * du2 ) * r, bz = ( e2z * du1 - e1z * du2 ) * r;

		const TUINT16 tri[ 3 ] = { i0, i1, i2 };
		for ( TINT j = 0; j < 3; j++ )
		{
			TangentAccum& a = pAccum[ tri[ j ] ];
			a.tx += tx;
			a.ty += ty;
			a.tz += tz;
			a.bx += bx;
			a.by += by;
			a.bz += bz;
		}
	}

	for ( TUINT i = 0; i < a_uiNumVerts; i++ )
	{
		const TVector3&     N = a_pVerts[ i ].Normal;
		const TangentAccum& a = pAccum[ i ];

		// Gram-Schmidt orthonormalize against the normal
		const TFLOAT ndt = N.x * a.tx + N.y * a.ty + N.z * a.tz;
		TFLOAT       tx = a.tx - N.x * ndt, ty = a.ty - N.y * ndt, tz = a.tz - N.z * ndt;
		TFLOAT       len = TMath::Sqrt( tx * tx + ty * ty + tz * tz );

		if ( len > 1e-6f )
		{
			const TFLOAT inv = 1.0f / len;
			tx *= inv;
			ty *= inv;
			tz *= inv;
		}
		else
		{
			// No usable UV gradient: synthesise a tangent perpendicular to N via cross(ref, N)
			const TFLOAT rx  = ( TMath::Abs( N.y ) < 0.99f ) ? 0.0f : 1.0f;
			const TFLOAT ry  = ( TMath::Abs( N.y ) < 0.99f ) ? 1.0f : 0.0f;
			tx               = ry * N.z;
			ty               = -rx * N.z;
			tz               = rx * N.y - ry * N.x;
			len              = TMath::Sqrt( tx * tx + ty * ty + tz * tz );
			const TFLOAT inv = ( len > 1e-6f ) ? 1.0f / len : 0.0f;
			tx *= inv;
			ty *= inv;
			tz *= inv;
		}

		// Handedness = sign of dot(cross(N, T), accumulated bitangent)
		const TFLOAT cx         = N.y * tz - N.z * ty;
		const TFLOAT cy         = N.z * tx - N.x * tz;
		const TFLOAT cz         = N.x * ty - N.y * tx;
		const TFLOAT handedness = ( cx * a.bx + cy * a.by + cz * a.bz ) < 0.0f ? -1.0f : 1.0f;

		TFLOAT* pOut = a_pOut + i * 4;
		pOut[ 0 ]    = tx;
		pOut[ 1 ]    = ty;
		pOut[ 2 ]    = tz;
		pOut[ 3 ]    = handedness;
	}

	delete[] pAccum;
}

// Fill the tangent stream (managed vertex slot 1) for every world mesh in a loaded LOD.
// The managed->HAL upload is lazy (first render), so writing the managed array here needs no lock or re-upload
void GenerateWorldTangentsForLOD( TModelLOD* a_pLOD )
{
	for ( TINT k = 0; k < a_pLOD->iNumMeshes; k++ )
	{
		AWorldMesh* pMesh = TSTATICCAST( AWorldMesh, a_pLOD->ppMeshes[ k ] );
		if ( !pMesh ) continue;

		auto pVertexPool = TSTATICCAST( TVertexPoolResource, pMesh->GetVertexPool() );
		auto pIndexPool  = TSTATICCAST( TIndexPoolResource, pMesh->GetSubMesh( 0 )->pIndexPool );
		if ( !pVertexPool || !pIndexPool ) continue;

		const TUINT uiNumVerts   = pVertexPool->GetNumVertices();
		const TUINT uiNumIndices = pIndexPool->GetNumIndices();
		if ( uiNumVerts == 0 || uiNumIndices < 3 ) continue;

		const WorldVertex* pVerts    = TREINTERPRETCAST( const WorldVertex*, pVertexPool->GetManagedStream( 0 ) );
		TFLOAT*            pTangents = TREINTERPRETCAST( TFLOAT*, pVertexPool->GetManagedStream( 1 ) );
		const TUINT16*     pIndices  = pIndexPool->GetIndices();
		if ( !pVerts || !pTangents || !pIndices ) continue;

		GenerateWorldMeshTangents( pVerts, uiNumVerts, pIndices, uiNumIndices, pTangents );
	}
}

// After the original loads the LOD's meshes (every CellMesh then has a valid pMesh), fill the tangent stream
HOOK( 0x00613a40, AModelLoader_LoadWorldMeshTRB_Tangents, void, TModel* a_pModel, TINT a_iLODIndex, TModelLOD* a_pLOD, TTMDWin::TRBLODHeader* a_pLODHeader )
{
	CallOriginal( a_pModel, a_iLODIndex, a_pLOD, a_pLODHeader );

	GenerateWorldTangentsForLOD( a_pLOD );
}

static ID3D11Texture2D*          s_pSkyMaskTexture            = TNULL;
static ID3D11RenderTargetView*   s_pSkyMaskRenderTargetView   = TNULL;
static ID3D11ShaderResourceView* s_pSkyMaskShaderResourceView = TNULL;

static ID3D11Texture2D*          s_pSunshaftsTexture            = TNULL;
static ID3D11RenderTargetView*   s_pSunshaftsRenderTargetView   = TNULL;
static ID3D11ShaderResourceView* s_pSunshaftsShaderResourceView = TNULL;

static ID3D11Texture2D*          s_pResolvedColorTexture = TNULL;
static ID3D11ShaderResourceView* s_pResolvedColorSRV     = TNULL;

// Resolved (non-MSAA) main-pass G-buffer: rgb = world normal, a = reflectivity
static ID3D11Texture2D*          s_pResolvedGBufferTexture = TNULL;
static ID3D11ShaderResourceView* s_pResolvedGBufferSRV     = TNULL;
static ID3D11Texture2D*          s_pResolvedGlowTexture    = TNULL;
static ID3D11ShaderResourceView* s_pResolvedGlowSRV        = TNULL;

static ID3D11Texture2D*          s_pResolvedDepthTexture = TNULL;
static ID3D11RenderTargetView*   s_pResolvedDepthRTV     = TNULL;
static ID3D11ShaderResourceView* s_pResolvedDepthSRV     = TNULL;

// Half-res nearest-depth copy of the resolved depth, marched by SSR (see DownsampleDepthMin.hlsl)
static ID3D11Texture2D*          s_pHalfDepthTexture = TNULL;
static ID3D11RenderTargetView*   s_pHalfDepthRTV     = TNULL;
static ID3D11ShaderResourceView* s_pHalfDepthSRV     = TNULL;

static ID3D11Texture2D*          s_pHBAOTexture     = TNULL;
static ID3D11RenderTargetView*   s_pHBAORTV         = TNULL;
static ID3D11ShaderResourceView* s_pHBAOSRV         = TNULL;
static ID3D11Texture2D*          s_pHBAOBlurTexture = TNULL;
static ID3D11RenderTargetView*   s_pHBAOBlurRTV     = TNULL;
static ID3D11ShaderResourceView* s_pHBAOBlurSRV     = TNULL;

// SSR half-res RGBA16F: rgb = reflected colour, a = confidence
static ID3D11Texture2D*          s_pSSRTexture     = TNULL;
static ID3D11RenderTargetView*   s_pSSRRTV         = TNULL;
static ID3D11ShaderResourceView* s_pSSRSRV         = TNULL;
static ID3D11Texture2D*          s_pSSRBlurTexture = TNULL;
static ID3D11RenderTargetView*   s_pSSRBlurRTV     = TNULL;
static ID3D11ShaderResourceView* s_pSSRBlurSRV     = TNULL;

// Runtime sky cubemap: sky dome re-rendered into 6 mipped faces, sampled by SSR as fallback (roughness -> mip LOD)
static constexpr TUINT SKYCUBE_SIZE = 256;
// Two cubes ping-pong for the cross-fade (SkyCube.h): "to" renders fresh, "from" stays frozen while a switch blends
static ID3D11Texture2D*          s_pSkyCubeTexture[ 2 ]      = {};
static ID3D11ShaderResourceView* s_pSkyCubeSRV[ 2 ]          = {};
static ID3D11RenderTargetView*   s_pSkyCubeFaceRTV[ 2 ][ 6 ] = {};
// Shared depth for the probe capture (terrain occlusion), cleared per face
static ID3D11Texture2D*        s_pSkyCubeDepthTexture = TNULL;
static ID3D11DepthStencilView* s_pSkyCubeDepthDSV     = TNULL;

static TINT            s_iCubeTo         = 0;                                   // index (0/1) rendered this frame
static TFLOAT          s_flCubeBlend     = 1.0f;                                // 0..1, 1 = fully "to"
static const void*     s_pCubeToTarget   = TREINTERPRETCAST( const void*, -1 ); // anchor id the "to" cube holds (-1 = uninit)
static Toshi::TVector4 s_vCubeProbe[ 2 ] = {};                                  // per-cube probe centre captured
static Toshi::TVector4 s_vCubeBox[ 2 ]   = {};                                  // per-cube parallax box half-extents
static TBOOL           s_bCubeValid[ 2 ] = {};                                  // has each cube been fully captured at its probe yet
static TFLOAT          s_flCubeRRTimer   = 0.0f;                                // round-robin accumulator (steady-state face refresh)
static TINT            s_iCubeRRFace     = 0;                                   // next face to refresh in round-robin

// AO runs at half width x half height, upsampled with a linear sampler at composite
static TUINT s_uiHBAOWidth  = 0;
static TUINT s_uiHBAOHeight = 0;

// SSR runs at its own resolution (SSR_RESOLUTION_DIVISOR); 1 = full res
static constexpr TUINT SSR_RESOLUTION_DIVISOR = 1;
static TUINT           s_uiSSRWidth           = 0;
static TUINT           s_uiSSRHeight          = 0;

static ID3D11Texture2D*          s_pVolumetricFogTexture         = TNULL;
static ID3D11RenderTargetView*   s_pVolumetricFogRTV             = TNULL;
static ID3D11ShaderResourceView* s_pVolumetricFogSRV             = TNULL;
static ID3D11Texture2D*          s_pVolumetricFogTemporalTexture = TNULL;
static ID3D11RenderTargetView*   s_pVolumetricFogTemporalRTV     = TNULL;
static ID3D11ShaderResourceView* s_pVolumetricFogTemporalSRV     = TNULL;
static ID3D11Texture2D*          s_pVolumetricFogHistoryTexture  = TNULL;
static ID3D11RenderTargetView*   s_pVolumetricFogHistoryRTV      = TNULL;
static ID3D11ShaderResourceView* s_pVolumetricFogHistorySRV      = TNULL;
static TBOOL                     s_bVolumetricFogHistoryValid    = TFALSE;
static TUINT                     s_uiVolumetricFogFrameIndex     = 0;

// Baked tileable 3D fBm noise for the fog density (sampled per march step instead of live)
static ID3D11Texture3D*          s_pVolumetricFogNoiseTexture = TNULL;
static ID3D11ShaderResourceView* s_pVolumetricFogNoiseSRV     = TNULL;
static ID3D11SamplerState*       s_pVolumetricFogNoiseSampler = TNULL;

// 32-bit integer hash -> [0,1], used for the tileable value-noise lattice corners
static TFLOAT VolFogNoiseHash( TINT x, TINT y, TINT z )
{
	TUINT h = TUINT( x * 374761393 + y * 668265263 + z * 1274126177 );
	h       = ( h ^ ( h >> 13 ) ) * 1274126177u;
	h       = h ^ ( h >> 16 );
	return TFLOAT( h & 0xFFFFu ) * ( 1.0f / 65535.0f );
}

// Value noise whose lattice wraps at `period` so it tiles seamlessly (WRAP sampler at runtime)
static TFLOAT VolFogTileNoise( TFLOAT x, TFLOAT y, TFLOAT z, TINT period )
{
	const TINT   xi = TINT( x ), yi = TINT( y ), zi = TINT( z );
	const TFLOAT xf = x - xi, yf = y - yi, zf = z - zi;
	const TFLOAT ux = xf * xf * ( 3.0f - 2.0f * xf );
	const TFLOAT uy = yf * yf * ( 3.0f - 2.0f * yf );
	const TFLOAT uz = zf * zf * ( 3.0f - 2.0f * zf );

#define VOLFOG_H( dx, dy, dz ) VolFogNoiseHash( ( xi + dx ) % period, ( yi + dy ) % period, ( zi + dz ) % period )
	const TFLOAT c000 = VOLFOG_H( 0, 0, 0 ), c100 = VOLFOG_H( 1, 0, 0 ), c010 = VOLFOG_H( 0, 1, 0 ), c110 = VOLFOG_H( 1, 1, 0 );
	const TFLOAT c001 = VOLFOG_H( 0, 0, 1 ), c101 = VOLFOG_H( 1, 0, 1 ), c011 = VOLFOG_H( 0, 1, 1 ), c111 = VOLFOG_H( 1, 1, 1 );
#undef VOLFOG_H

	const TFLOAT x00 = c000 + ux * ( c100 - c000 );
	const TFLOAT x10 = c010 + ux * ( c110 - c010 );
	const TFLOAT x01 = c001 + ux * ( c101 - c001 );
	const TFLOAT x11 = c011 + ux * ( c111 - c011 );
	const TFLOAT y0  = x00 + uy * ( x10 - x00 );
	const TFLOAT y1  = x01 + uy * ( x11 - x01 );
	return y0 + uz * ( y1 - y0 );
}

// Bakes a 64^3 R8 tileable fBm volume (4 octaves), precomputed
static void CreateVolumetricFogNoiseVolume( ID3D11Device* a_pDevice )
{
	if ( s_pVolumetricFogNoiseTexture )
		return;

	static constexpr TINT N     = 64;
	TUINT8*               pData = new TUINT8[ N * N * N ];

	for ( TINT z = 0; z < N; z++ )
		for ( TINT y = 0; y < N; y++ )
			for ( TINT x = 0; x < N; x++ )
			{
				TFLOAT v = 0.0f, amp = 0.5f, total = 0.0f;
				TINT   period = 4; // base frequency; doubles each octave, all divide N

				for ( TINT octave = 0; octave < 4; octave++ )
				{
					const TFLOAT fx = ( TFLOAT( x ) / N ) * period;
					const TFLOAT fy = ( TFLOAT( y ) / N ) * period;
					const TFLOAT fz = ( TFLOAT( z ) / N ) * period;
					v += amp * VolFogTileNoise( fx, fy, fz, period );
					total += amp;
					amp *= 0.5f;
					period *= 2;
				}

				v                              = TMath::Min( TMath::Max( v / total, 0.0f ), 1.0f );
				pData[ x + y * N + z * N * N ] = TUINT8( v * 255.0f + 0.5f );
			}

	D3D11_TEXTURE3D_DESC desc = {};
	desc.Width                = N;
	desc.Height               = N;
	desc.Depth                = N;
	desc.MipLevels            = 1;
	desc.Format               = DXGI_FORMAT_R8_UNORM;
	desc.Usage                = D3D11_USAGE_IMMUTABLE;
	desc.BindFlags            = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA init = {};
	init.pSysMem                = pData;
	init.SysMemPitch            = N;
	init.SysMemSlicePitch       = N * N;

	DX11_API_VALIDATE( a_pDevice->CreateTexture3D( &desc, &init, &s_pVolumetricFogNoiseTexture ) );
	DX11_API_VALIDATE( a_pDevice->CreateShaderResourceView( s_pVolumetricFogNoiseTexture, TNULL, &s_pVolumetricFogNoiseSRV ) );

	delete[] pData;

	D3D11_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	samplerDesc.AddressU           = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV           = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW           = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.MaxLOD             = D3D11_FLOAT32_MAX;
	DX11_API_VALIDATE( a_pDevice->CreateSamplerState( &samplerDesc, &s_pVolumetricFogNoiseSampler ) );
}

static constexpr TINT KAWASE_MAX_LEVELS = 5;

static ID3D11Texture2D*          s_pKawaseTextures[ KAWASE_MAX_LEVELS ] = {};
static ID3D11RenderTargetView*   s_pKawaseRTVs[ KAWASE_MAX_LEVELS ]     = {};
static ID3D11ShaderResourceView* s_pKawaseSRVs[ KAWASE_MAX_LEVELS ]     = {};
static TUINT                     s_uiKawaseWidths[ KAWASE_MAX_LEVELS ]  = {};
static TUINT                     s_uiKawaseHeights[ KAWASE_MAX_LEVELS ] = {};

struct KawaseCBuffer
{
	TFLOAT texelSizeX;
	TFLOAT texelSizeY;
	TFLOAT offset;
	TFLOAT PADDING;
};

static ID3D11Buffer*       s_pKawaseCBuffer      = TNULL;
static ID3D11SamplerState* s_pPointClampSampler  = TNULL;
static ID3D11SamplerState* s_pLinearClampSampler = TNULL;

// Cloud shadow bake target (top-down sun-amount map) + cbuffer; SRV/sampler exposed via CSMManager.h externs
static ID3D11Texture2D*        s_pCloudShadowTexture = TNULL;
static ID3D11RenderTargetView* s_pCloudShadowRTV     = TNULL;
static ID3D11Buffer*           s_pCloudShadowCBuffer = TNULL;
static constexpr TUINT         s_uiCloudShadowRes    = 1024;

namespace remaster
{
ID3D11ShaderResourceView* g_pCloudShadowSRV     = TNULL;
ID3D11SamplerState*       g_pCloudShadowSampler = TNULL;

// Reflection cubemap, exposed for the world shader's environment specular
ID3D11ShaderResourceView* g_pSkyCubeSRV          = TNULL; // alias of the active ("to") cube SRV
TINT                      g_iSkyCubeMaxMip       = 0;
SkyCubeBlendState         g_oSkyCubeBlend        = {};
TFLOAT                    g_flSkyCubeBlendTime   = 0.35f;
TFLOAT                    g_flSkyCubeRefreshTime = 0.5f; // full-cube refresh cadence (s); faces amortized round-robin
} // namespace remaster

struct CloudShadowCBuffer
{
	TFLOAT region[ 4 ]; // xy = world region min (X,Z), z = region size, w = feature scale
	TFLOAT anim[ 4 ];   // x = time, yz = wind dir, w = coverage
	TFLOAT shape[ 4 ];  // x = density, y = contrast
};

struct HBAOCBuffer
{
	TFLOAT projection[ 4 ];
	TFLOAT depthParams[ 4 ];
	TFLOAT params[ 4 ];
	TFLOAT bufferSize[ 4 ];
};

struct XeGTAOCBuffer
{
	TFLOAT projection[ 4 ];
	TFLOAT depthParams[ 4 ];
	TFLOAT params[ 4 ];
	TFLOAT bufferSize[ 4 ];
	TFLOAT xeParams[ 4 ];
};

struct HBAOBlurCBuffer
{
	TFLOAT blurParams[ 4 ];
	TFLOAT depthParams[ 4 ];
};

struct SSRCBuffer
{
	TFLOAT    projection[ 4 ];
	TFLOAT    depthParams[ 4 ];
	TFLOAT    params[ 4 ]; // intensity, maxDistance, thickness, fresnelPower
	TFLOAT    bufferSize[ 4 ];
	TFLOAT    marchParams[ 4 ];      // maxSteps, pixelStride, edgeFadePower, unused
	TFLOAT    blurParams[ 4 ];       // invWidth, invHeight, dirX, dirY
	TFLOAT    blurDepth[ 4 ];        // near, far, sharpness, unused
	TMatrix44 worldToView;           // rotates G-buffer world normals into view space
	TFLOAT    skyHorizon[ 4 ];       // rgb = horizon colour avg(FORWARD,TRANSLATION), a = fallback intensity (0 = off)
	TFLOAT    skyZenith[ 4 ];        // rgb = zenith colour avg(RIGHT,UP)
	TFLOAT    skyCubeParams[ 4 ];    // maxMip, enable (0/1), intensity, unused
	TFLOAT    skyCubeParallax[ 4 ];  // "to" box half-extents xyz (world units), w = enable (0/1)
	TFLOAT    skyCubeOffset[ 4 ];    // xyz = camera - "to" probe (world); w = cross-fade blend (1 = fully "to")
	TFLOAT    skyCubeParallax2[ 4 ]; // "from" box half-extents xyz
	TFLOAT    skyCubeOffset2[ 4 ];   // xyz = camera - "from" probe (world)
};

static ID3D11Buffer* s_pSSRConstantBuffer = TNULL;

static ID3D11Buffer* s_pHBAOConstantBuffer     = TNULL;
static ID3D11Buffer* s_pXeGTAOConstantBuffer   = TNULL;
static ID3D11Buffer* s_pHBAOBlurConstantBuffer = TNULL;

struct SunShaftsCBuffer
{
	TFLOAT vSunPos[ 2 ];
	TFLOAT fSunAlpha;
	TFLOAT fRaysLength;
	TFLOAT vRaysTint[ 3 ];
	TFLOAT PADDING;
};

static ID3D11Buffer*       s_pSunShaftsConstantBuffer = TNULL;
static ID3D11SamplerState* s_pSkyMaskSampler          = TNULL;

struct VolumetricFogCBuffer
{
	TFLOAT    matLightVP[ 3 ][ 16 ];
	TFLOAT    cascadeSplits[ 4 ];
	TFLOAT    shadowBias[ 4 ];
	TMatrix44 matViewWorld;
	TFLOAT    projection[ 4 ];
	TFLOAT    depthParams[ 4 ];
	TFLOAT    lightDirVS[ 4 ];
	TFLOAT    fogColor[ 4 ];
	TFLOAT    fogParams[ 4 ];
	TFLOAT    frameParams[ 4 ];
	TFLOAT    cloudParams[ 4 ];
	TFLOAT    noiseParams[ 4 ];
	TFLOAT    heightParams[ 4 ];
};

struct VolumetricFogCompositeCBuffer
{
	TFLOAT depthParams[ 4 ];
	TFLOAT compositeParams[ 4 ];
};

static ID3D11Buffer* s_pVolumetricFogConstantBuffer          = TNULL;
static ID3D11Buffer* s_pVolumetricFogCompositeConstantBuffer = TNULL;

void remaster::RenderDX11::CreateRenderTargets()
{
	auto pSwapChainDesc = GetSwapChainDesc();

	// Sky mask
	{
		D3D11_TEXTURE2D_DESC skyMaskDesc = {};
		skyMaskDesc.ArraySize            = 1;
		skyMaskDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		skyMaskDesc.CPUAccessFlags       = 0;
		// R11G11B10_FLOAT not R8G8B8A8: the low-contrast shaft gradient bands in 8-bit; same 32bpp, alpha unused
		skyMaskDesc.Format    = DXGI_FORMAT_R11G11B10_FLOAT;
		skyMaskDesc.Height    = pSwapChainDesc->BufferDesc.Height >> 1;
		skyMaskDesc.Width     = pSwapChainDesc->BufferDesc.Width >> 1;
		skyMaskDesc.MipLevels = 1;
		skyMaskDesc.MiscFlags = 0;
		// Fullscreen post-process buffers -- no geometric aliasing, so 1-sample (not swap-chain MSAA) saves memory + bandwidth
		skyMaskDesc.SampleDesc.Count   = 1;
		skyMaskDesc.SampleDesc.Quality = 0;
		skyMaskDesc.Usage              = D3D11_USAGE_DEFAULT;

		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &skyMaskDesc, TNULL, &s_pSkyMaskTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pSkyMaskTexture, TNULL, &s_pSkyMaskRenderTargetView ) );

		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &skyMaskDesc, TNULL, &s_pSunshaftsTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pSunshaftsTexture, TNULL, &s_pSunshaftsRenderTargetView ) );

		D3D11_SHADER_RESOURCE_VIEW_DESC skyMaskSRVDesc = {};
		skyMaskSRVDesc.Format                          = skyMaskDesc.Format;
		skyMaskSRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2D;
		skyMaskSRVDesc.Texture2D.MipLevels             = 1;
		skyMaskSRVDesc.Texture2D.MostDetailedMip       = 0;

		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pSkyMaskTexture, &skyMaskSRVDesc, &s_pSkyMaskShaderResourceView ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pSunshaftsTexture, &skyMaskSRVDesc, &s_pSunshaftsShaderResourceView ) );

		D3D11_BUFFER_DESC sunShaftsCBDesc = {};
		sunShaftsCBDesc.ByteWidth         = sizeof( SunShaftsCBuffer );
		sunShaftsCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		sunShaftsCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		sunShaftsCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &sunShaftsCBDesc, TNULL, &s_pSunShaftsConstantBuffer ) );
	}

	// Resolved color/glow: non-MSAA destinations for ResolveSubresource
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width                = pSwapChainDesc->BufferDesc.Width;
		desc.Height               = pSwapChainDesc->BufferDesc.Height;
		desc.MipLevels            = 1;
		desc.ArraySize            = 1;
		desc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count     = 1;
		desc.SampleDesc.Quality   = 0;
		desc.Usage                = D3D11_USAGE_DEFAULT;
		desc.BindFlags            = D3D11_BIND_SHADER_RESOURCE;

		// Resolved main color matches the HDR main RT format (R11G11B10)
		D3D11_TEXTURE2D_DESC colorDesc = desc;
		colorDesc.Format               = DXGI_FORMAT_R11G11B10_FLOAT;
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &colorDesc, TNULL, &s_pResolvedColorTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pResolvedColorTexture, TNULL, &s_pResolvedColorSRV ) );

		// Resolved glow stays LDR to match the glow RT
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pResolvedGlowTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pResolvedGlowTexture, TNULL, &s_pResolvedGlowSRV ) );

		// Resolved G-buffer (matches the MSAA RGBA8 G-buffer target)
		D3D11_TEXTURE2D_DESC gbDesc = desc;
		gbDesc.Format               = DXGI_FORMAT_R8G8B8A8_UNORM;
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &gbDesc, TNULL, &s_pResolvedGBufferTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pResolvedGBufferTexture, TNULL, &s_pResolvedGBufferSRV ) );
	}

	// Resolved depth: R32_FLOAT render target written by the ResolveDepth shader
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width                = pSwapChainDesc->BufferDesc.Width;
		desc.Height               = pSwapChainDesc->BufferDesc.Height;
		desc.MipLevels            = 1;
		desc.ArraySize            = 1;
		desc.Format               = DXGI_FORMAT_R32_FLOAT;
		desc.SampleDesc.Count     = 1;
		desc.SampleDesc.Quality   = 0;
		desc.Usage                = D3D11_USAGE_DEFAULT;
		desc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pResolvedDepthTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pResolvedDepthTexture, TNULL, &s_pResolvedDepthRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pResolvedDepthTexture, TNULL, &s_pResolvedDepthSRV ) );
	}

	// AO buffers (half resolution)
	{
		s_uiHBAOWidth  = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Width >> 1, 1 );
		s_uiHBAOHeight = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Height >> 1, 1 );

		s_uiSSRWidth  = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Width / SSR_RESOLUTION_DIVISOR, 1 );
		s_uiSSRHeight = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Height / SSR_RESOLUTION_DIVISOR, 1 );

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width                = s_uiHBAOWidth;
		desc.Height               = s_uiHBAOHeight;
		desc.MipLevels            = 1;
		desc.ArraySize            = 1;
		desc.Format               = DXGI_FORMAT_R16_FLOAT;
		desc.SampleDesc.Count     = 1;
		desc.SampleDesc.Quality   = 0;
		desc.Usage                = D3D11_USAGE_DEFAULT;
		desc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pHBAOTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pHBAOTexture, TNULL, &s_pHBAORTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pHBAOTexture, TNULL, &s_pHBAOSRV ) );

		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pHBAOBlurTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pHBAOBlurTexture, TNULL, &s_pHBAOBlurRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pHBAOBlurTexture, TNULL, &s_pHBAOBlurSRV ) );

		// Half-res nearest-depth buffer SSR marches against; R32_FLOAT to preserve depth precision
		D3D11_TEXTURE2D_DESC halfDepthDesc = desc;
		halfDepthDesc.Format               = DXGI_FORMAT_R32_FLOAT;
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &halfDepthDesc, TNULL, &s_pHalfDepthTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pHalfDepthTexture, TNULL, &s_pHalfDepthRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pHalfDepthTexture, TNULL, &s_pHalfDepthSRV ) );

		// Cloud shadow bake target: single-channel sun-amount map, fixed resolution
		D3D11_TEXTURE2D_DESC cloudDesc = {};
		cloudDesc.Width                = s_uiCloudShadowRes;
		cloudDesc.Height               = s_uiCloudShadowRes;
		cloudDesc.MipLevels            = 1;
		cloudDesc.ArraySize            = 1;
		cloudDesc.Format               = DXGI_FORMAT_R8_UNORM;
		cloudDesc.SampleDesc.Count     = 1;
		cloudDesc.Usage                = D3D11_USAGE_DEFAULT;
		cloudDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &cloudDesc, TNULL, &s_pCloudShadowTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pCloudShadowTexture, TNULL, &s_pCloudShadowRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pCloudShadowTexture, TNULL, &remaster::g_pCloudShadowSRV ) );

		D3D11_BUFFER_DESC cloudCBDesc = {};
		cloudCBDesc.ByteWidth         = sizeof( CloudShadowCBuffer );
		cloudCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		cloudCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		cloudCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &cloudCBDesc, TNULL, &s_pCloudShadowCBuffer ) );

		D3D11_BUFFER_DESC hbaoCBDesc = {};
		hbaoCBDesc.ByteWidth         = sizeof( HBAOCBuffer );
		hbaoCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		hbaoCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		hbaoCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &hbaoCBDesc, TNULL, &s_pHBAOConstantBuffer ) );

		D3D11_BUFFER_DESC xeGTAOCBDesc = {};
		xeGTAOCBDesc.ByteWidth         = sizeof( XeGTAOCBuffer );
		xeGTAOCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		xeGTAOCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		xeGTAOCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &xeGTAOCBDesc, TNULL, &s_pXeGTAOConstantBuffer ) );

		D3D11_BUFFER_DESC hbaoBlurCBDesc = {};
		hbaoBlurCBDesc.ByteWidth         = sizeof( HBAOBlurCBuffer );
		hbaoBlurCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		hbaoBlurCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		hbaoBlurCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &hbaoBlurCBDesc, TNULL, &s_pHBAOBlurConstantBuffer ) );

		// SSR buffers (own resolution; see SSR_RESOLUTION_DIVISOR)
		D3D11_TEXTURE2D_DESC ssrDesc = {};
		ssrDesc.Width                = s_uiSSRWidth;
		ssrDesc.Height               = s_uiSSRHeight;
		ssrDesc.MipLevels            = 1;
		ssrDesc.ArraySize            = 1;
		ssrDesc.Format               = DXGI_FORMAT_R16G16B16A16_FLOAT;
		ssrDesc.SampleDesc.Count     = 1;
		ssrDesc.SampleDesc.Quality   = 0;
		ssrDesc.Usage                = D3D11_USAGE_DEFAULT;
		ssrDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &ssrDesc, TNULL, &s_pSSRTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pSSRTexture, TNULL, &s_pSSRRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pSSRTexture, TNULL, &s_pSSRSRV ) );

		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &ssrDesc, TNULL, &s_pSSRBlurTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pSSRBlurTexture, TNULL, &s_pSSRBlurRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pSSRBlurTexture, TNULL, &s_pSSRBlurSRV ) );

		D3D11_BUFFER_DESC ssrCBDesc = {};
		ssrCBDesc.ByteWidth         = sizeof( SSRCBuffer );
		ssrCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		ssrCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		ssrCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &ssrCBDesc, TNULL, &s_pSSRConstantBuffer ) );

		// Sky cubemap (fixed size, not swapchain-derived): 6-face RGBA16F with a full mip chain
		{
			D3D11_TEXTURE2D_DESC cubeDesc = {};
			cubeDesc.Width                = SKYCUBE_SIZE;
			cubeDesc.Height               = SKYCUBE_SIZE;
			cubeDesc.MipLevels            = 0; // 0 = full chain
			cubeDesc.ArraySize            = 6;
			cubeDesc.Format               = DXGI_FORMAT_R16G16B16A16_FLOAT;
			cubeDesc.SampleDesc.Count     = 1;
			cubeDesc.Usage                = D3D11_USAGE_DEFAULT;
			cubeDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			cubeDesc.MiscFlags            = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;

			for ( TINT cube = 0; cube < 2; cube++ )
			{
				DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &cubeDesc, TNULL, &s_pSkyCubeTexture[ cube ] ) );

				D3D11_SHADER_RESOURCE_VIEW_DESC cubeSRVDesc = {};
				cubeSRVDesc.Format                          = cubeDesc.Format;
				cubeSRVDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURECUBE;
				cubeSRVDesc.TextureCube.MostDetailedMip     = 0;
				cubeSRVDesc.TextureCube.MipLevels           = TUINT( -1 ); // all mips
				DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pSkyCubeTexture[ cube ], &cubeSRVDesc, &s_pSkyCubeSRV[ cube ] ) );

				for ( TINT face = 0; face < 6; face++ )
				{
					D3D11_RENDER_TARGET_VIEW_DESC cubeRTVDesc  = {};
					cubeRTVDesc.Format                         = cubeDesc.Format;
					cubeRTVDesc.ViewDimension                  = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
					cubeRTVDesc.Texture2DArray.MipSlice        = 0;
					cubeRTVDesc.Texture2DArray.FirstArraySlice = face;
					cubeRTVDesc.Texture2DArray.ArraySize       = 1;
					DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pSkyCubeTexture[ cube ], &cubeRTVDesc, &s_pSkyCubeFaceRTV[ cube ][ face ] ) );
				}
			}

			remaster::g_pSkyCubeSRV = s_pSkyCubeSRV[ 0 ]; // alias; updated to the active cube each frame

			// Highest mip index = log2(size), used to scale roughness -> LOD in SSR
			remaster::g_iSkyCubeMaxMip = 0;
			for ( TUINT uiSize = SKYCUBE_SIZE; uiSize > 1; uiSize >>= 1 )
				remaster::g_iSkyCubeMaxMip++;

			D3D11_TEXTURE2D_DESC cubeDepthDesc = {};
			cubeDepthDesc.Width                = SKYCUBE_SIZE;
			cubeDepthDesc.Height               = SKYCUBE_SIZE;
			cubeDepthDesc.MipLevels            = 1;
			cubeDepthDesc.ArraySize            = 1;
			cubeDepthDesc.Format               = DXGI_FORMAT_D32_FLOAT;
			cubeDepthDesc.SampleDesc.Count     = 1;
			cubeDepthDesc.Usage                = D3D11_USAGE_DEFAULT;
			cubeDepthDesc.BindFlags            = D3D11_BIND_DEPTH_STENCIL;
			DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &cubeDepthDesc, TNULL, &s_pSkyCubeDepthTexture ) );
			DX11_API_VALIDATE( GetD3D11Device()->CreateDepthStencilView( s_pSkyCubeDepthTexture, TNULL, &s_pSkyCubeDepthDSV ) );
		}
	}

	// Dual Kawase blur mip chain
	{
		for ( TINT i = 0; i < KAWASE_MAX_LEVELS; i++ )
		{
			s_uiKawaseWidths[ i ]  = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Width >> ( i + 2 ), 1 );
			s_uiKawaseHeights[ i ] = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Height >> ( i + 2 ), 1 );

			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width                = s_uiKawaseWidths[ i ];
			desc.Height               = s_uiKawaseHeights[ i ];
			desc.MipLevels            = 1;
			desc.ArraySize            = 1;
			desc.Format               = DXGI_FORMAT_R11G11B10_FLOAT;
			desc.SampleDesc.Count     = 1;
			desc.SampleDesc.Quality   = 0;
			desc.Usage                = D3D11_USAGE_DEFAULT;
			desc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pKawaseTextures[ i ] ) );
			DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pKawaseTextures[ i ], TNULL, &s_pKawaseRTVs[ i ] ) );
			DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pKawaseTextures[ i ], TNULL, &s_pKawaseSRVs[ i ] ) );
		}

		D3D11_BUFFER_DESC kawaseCBDesc = {};
		kawaseCBDesc.ByteWidth         = sizeof( KawaseCBuffer );
		kawaseCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		kawaseCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		kawaseCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &kawaseCBDesc, TNULL, &s_pKawaseCBuffer ) );
	}

	// Volumetric Fog (quarter-resolution R11G11B10_FLOAT)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width                = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Width >> 2, 1 );
		desc.Height               = TMath::Max<TUINT>( pSwapChainDesc->BufferDesc.Height >> 2, 1 );
		desc.MipLevels            = 1;
		desc.ArraySize            = 1;
		desc.Format               = DXGI_FORMAT_R11G11B10_FLOAT;
		desc.SampleDesc.Count     = 1;
		desc.SampleDesc.Quality   = 0;
		desc.Usage                = D3D11_USAGE_DEFAULT;
		desc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pVolumetricFogTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pVolumetricFogTexture, TNULL, &s_pVolumetricFogRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pVolumetricFogTexture, TNULL, &s_pVolumetricFogSRV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pVolumetricFogTemporalTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pVolumetricFogTemporalTexture, TNULL, &s_pVolumetricFogTemporalRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pVolumetricFogTemporalTexture, TNULL, &s_pVolumetricFogTemporalSRV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateTexture2D( &desc, TNULL, &s_pVolumetricFogHistoryTexture ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateRenderTargetView( s_pVolumetricFogHistoryTexture, TNULL, &s_pVolumetricFogHistoryRTV ) );
		DX11_API_VALIDATE( GetD3D11Device()->CreateShaderResourceView( s_pVolumetricFogHistoryTexture, TNULL, &s_pVolumetricFogHistorySRV ) );
		s_bVolumetricFogHistoryValid = TFALSE;

		D3D11_BUFFER_DESC fogCBDesc = {};
		fogCBDesc.ByteWidth         = sizeof( VolumetricFogCBuffer );
		fogCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		fogCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		fogCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &fogCBDesc, TNULL, &s_pVolumetricFogConstantBuffer ) );

		D3D11_BUFFER_DESC fogCompositeCBDesc = {};
		fogCompositeCBDesc.ByteWidth         = sizeof( VolumetricFogCompositeCBuffer );
		fogCompositeCBDesc.Usage             = D3D11_USAGE_DYNAMIC;
		fogCompositeCBDesc.BindFlags         = D3D11_BIND_CONSTANT_BUFFER;
		fogCompositeCBDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
		DX11_API_VALIDATE( GetD3D11Device()->CreateBuffer( &fogCompositeCBDesc, TNULL, &s_pVolumetricFogCompositeConstantBuffer ) );

		// Resolution-independent; the guard inside builds it only on the first call
		CreateVolumetricFogNoiseVolume( GetD3D11Device() );
	}

	s_pSkyMaskSampler = CreateSamplerState(
	    D3D11_FILTER_MIN_MAG_MIP_LINEAR,
	    D3D11_TEXTURE_ADDRESS_BORDER,
	    D3D11_TEXTURE_ADDRESS_BORDER,
	    D3D11_TEXTURE_ADDRESS_BORDER,
	    0.0f,
	    0x00000000,
	    0.0f,
	    D3D11_FLOAT32_MAX,
	    1
	);

	s_pPointClampSampler = CreateSamplerState(
	    D3D11_FILTER_MIN_MAG_MIP_POINT,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    0.0f,
	    0x00000000,
	    0.0f,
	    D3D11_FLOAT32_MAX,
	    1
	);

	s_pLinearClampSampler = CreateSamplerState(
	    D3D11_FILTER_MIN_MAG_MIP_LINEAR,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    D3D11_TEXTURE_ADDRESS_CLAMP,
	    0.0f,
	    0x00000000,
	    0.0f,
	    D3D11_FLOAT32_MAX,
	    1
	);

	// Cloud shadows reuse the linear-clamp sampler (clamp = full sun outside the region)
	remaster::g_pCloudShadowSampler = s_pLinearClampSampler;
}

void remaster::RenderDX11::ReleaseRenderTargets()
{
	auto fnRelease = []( auto*& a_rpObject ) {
		if ( a_rpObject )
		{
			a_rpObject->Release();
			a_rpObject = TNULL;
		}
	};

	fnRelease( s_pSkyMaskShaderResourceView );
	fnRelease( s_pSkyMaskRenderTargetView );
	fnRelease( s_pSkyMaskTexture );
	fnRelease( s_pSunshaftsShaderResourceView );
	fnRelease( s_pSunshaftsRenderTargetView );
	fnRelease( s_pSunshaftsTexture );
	fnRelease( s_pSunShaftsConstantBuffer );

	fnRelease( s_pResolvedColorSRV );
	fnRelease( s_pResolvedColorTexture );
	fnRelease( s_pResolvedGlowSRV );
	fnRelease( s_pResolvedGlowTexture );
	fnRelease( s_pResolvedGBufferSRV );
	fnRelease( s_pResolvedGBufferTexture );

	fnRelease( s_pResolvedDepthSRV );
	fnRelease( s_pResolvedDepthRTV );
	fnRelease( s_pResolvedDepthTexture );
	fnRelease( s_pHalfDepthSRV );
	fnRelease( s_pHalfDepthRTV );
	fnRelease( s_pHalfDepthTexture );

	fnRelease( s_pHBAOSRV );
	fnRelease( s_pHBAORTV );
	fnRelease( s_pHBAOTexture );
	fnRelease( s_pHBAOBlurSRV );
	fnRelease( s_pHBAOBlurRTV );
	fnRelease( s_pHBAOBlurTexture );
	fnRelease( s_pHBAOConstantBuffer );
	fnRelease( s_pXeGTAOConstantBuffer );
	fnRelease( s_pHBAOBlurConstantBuffer );

	// The cloud shadow sampler aliases s_pLinearClampSampler (released below), so it isn't freed here
	fnRelease( remaster::g_pCloudShadowSRV );
	fnRelease( s_pCloudShadowRTV );
	fnRelease( s_pCloudShadowTexture );
	fnRelease( s_pCloudShadowCBuffer );

	fnRelease( s_pSSRSRV );
	fnRelease( s_pSSRRTV );
	fnRelease( s_pSSRTexture );
	fnRelease( s_pSSRBlurSRV );
	fnRelease( s_pSSRBlurRTV );
	fnRelease( s_pSSRBlurTexture );
	fnRelease( s_pSSRConstantBuffer );

	remaster::g_pSkyCubeSRV = TNULL; // non-owning alias
	for ( TINT cube = 0; cube < 2; cube++ )
	{
		fnRelease( s_pSkyCubeSRV[ cube ] );
		for ( TINT i = 0; i < 6; i++ )
			fnRelease( s_pSkyCubeFaceRTV[ cube ][ i ] );
		fnRelease( s_pSkyCubeTexture[ cube ] );
	}
	fnRelease( s_pSkyCubeDepthDSV );
	fnRelease( s_pSkyCubeDepthTexture );

	// The blend state holds non-owning aliases of the cube SRVs just freed; clear it (and force a re-capture)
	// so a resolution/MSAA rebuild can't leave a dangling SRV bound before CaptureCubeMap repopulates it
	remaster::g_oSkyCubeBlend = {};
	s_bCubeValid[ 0 ]         = TFALSE;
	s_bCubeValid[ 1 ]         = TFALSE;
	s_pCubeToTarget           = TREINTERPRETCAST( const void*, -1 );
	s_flCubeBlend             = 1.0f;

	for ( TINT i = 0; i < KAWASE_MAX_LEVELS; i++ )
	{
		fnRelease( s_pKawaseSRVs[ i ] );
		fnRelease( s_pKawaseRTVs[ i ] );
		fnRelease( s_pKawaseTextures[ i ] );
	}
	fnRelease( s_pKawaseCBuffer );

	fnRelease( s_pVolumetricFogSRV );
	fnRelease( s_pVolumetricFogRTV );
	fnRelease( s_pVolumetricFogTexture );
	fnRelease( s_pVolumetricFogTemporalSRV );
	fnRelease( s_pVolumetricFogTemporalRTV );
	fnRelease( s_pVolumetricFogTemporalTexture );
	fnRelease( s_pVolumetricFogHistorySRV );
	fnRelease( s_pVolumetricFogHistoryRTV );
	fnRelease( s_pVolumetricFogHistoryTexture );
	fnRelease( s_pVolumetricFogConstantBuffer );
	fnRelease( s_pVolumetricFogCompositeConstantBuffer );
	fnRelease( s_pVolumetricFogNoiseSRV );
	fnRelease( s_pVolumetricFogNoiseTexture );
	fnRelease( s_pVolumetricFogNoiseSampler );
	s_bVolumetricFogHistoryValid = TFALSE;

	fnRelease( s_pSkyMaskSampler );
	fnRelease( s_pPointClampSampler );
	fnRelease( s_pLinearClampSampler );

	remaster::g_pCloudShadowSampler = TNULL;
}

TBOOL g_bHasGlowObjectsThisFrame = TFALSE;
TBOOL g_bEnableWaterReflections  = TFALSE;

static void CaptureCubeMap( TFLOAT a_flDeltaTime )
{
	if ( !remaster::g_bSkyCubeEnabled || !s_pSkyCubeTexture[ 0 ] )
		return;

	ASkyDome* pSky = ARenderer::GetSingleton()->m_pSkyDome;
	if ( !pSky || !pSky->m_pFancyDome )
		return;

	auto pCtx = remaster::g_pRender->GetCurrentContext();
	if ( !pCtx )
		return;

	TPROFILER_NAMED( "Sky Cube Capture" );
	TracyD3D11Zone( remaster::g_pRender->GetTracyGpuContext(), "Sky Cube Capture" );

	auto  pDeviceContext = remaster::g_pRender->GetD3D11DeviceContext();
	auto& rTransforms    = remaster::g_pRender->GetTransforms();

	const TMatrix44                               oOldWorldView  = pCtx->GetWorldViewMatrix();
	const TMatrix44                               oOldModelView  = pCtx->GetModelViewMatrix();
	const Toshi::TRenderContext::PROJECTIONPARAMS oOldProj       = pCtx->GetProjectionParams();
	const Toshi::TRenderContext::VIEWPORTPARAMS   oOldViewport   = pCtx->GetViewportParameters();
	const Toshi::TRenderContext::CameraMode       eOldCameraMode = pCtx->GetCameraMode();
	const TFLOAT                                  fOldStars      = pSky->m_fStarsOpacity;
	const TBOOL                                   bOldCSMEnabled = remaster::g_bCSMEnabled;

	D3D11_VIEWPORT oOldVP;
	TUINT          uiNumVP = 1;
	pDeviceContext->RSGetViewports( &uiNumVP, &oOldVP );

	// Probe sits at the camera world position; grab the ACamera and keep its matrix pointed at each face for cull paths
	ACamera* pCamera = TNULL;
	ACamera  oOldCamera;
	TVector4 vCamPos( 0.0f, 0.0f, 0.0f, 1.0f );
	if ( *(void**)0x007822e0 )
	{
		pCamera = CALL_THIS( 0x0045b870, void*, ACamera*, *(void**)0x007822e0 ); // ACameraManager::GetCurrentCamera
		if ( pCamera )
		{
			oOldCamera = *pCamera;
			vCamPos    = pCamera->m_Matrix.GetTranslation();
		}
	}

	// Probe + box come from the active anchor if the camera is inside one, else the live camera with the global box.
	// When the target identity (anchor pointer, null for the camera) changes, freeze the "to" cube as "from", flip, and restart the blend
	const remaster::CubemapAnchor* pAnchor    = remaster::CubemapAnchors_SelectActive( vCamPos );
	const void*                    pTargetId  = pAnchor;
	TVector4                       vTargetBox = pAnchor ? pAnchor->vHalfExtents : TVector4( remaster::g_flSkyCubeParallaxHorizontal, remaster::g_flSkyCubeParallaxVertical, remaster::g_flSkyCubeParallaxHorizontal, 0.0f );
	if ( pAnchor )
		vCamPos = pAnchor->vPosition;

	// Anchored = placed world-stable probe; unanchored = camera-follow fallback (sky + terrain only)
	const TBOOL bAnchored = ( pAnchor != TNULL );

	TBOOL bFlipped = TFALSE;
	if ( pTargetId != s_pCubeToTarget )
	{
		// New target: unless this is the very first capture, flip cubes and start a fresh fade
		if ( s_pCubeToTarget != TREINTERPRETCAST( const void*, -1 ) )
		{
			s_iCubeTo     = 1 - s_iCubeTo;
			s_flCubeBlend = 0.0f;
			bFlipped      = TTRUE;
		}
		s_pCubeToTarget = pTargetId;
	}

	// Full 6-face capture when the "to" cube is invalid, just flipped, or its probe moved (else faces would seam).
	// A stationary probe instead refreshes one face per (refreshTime / 6) to keep sky/lighting current
	const TVector4 vProbe    = TVector4( vCamPos.x, vCamPos.y, vCamPos.z, 0.0f );
	const TFLOAT   fdx       = vProbe.x - s_vCubeProbe[ s_iCubeTo ].x;
	const TFLOAT   fdy       = vProbe.y - s_vCubeProbe[ s_iCubeTo ].y;
	const TFLOAT   fdz       = vProbe.z - s_vCubeProbe[ s_iCubeTo ].z;
	const TBOOL    bNeedFull = !s_bCubeValid[ s_iCubeTo ] || bFlipped || ( fdx * fdx + fdy * fdy + fdz * fdz ) > 1e-4f;

	TINT aiFaces[ 6 ];
	TINT iNumFaces = 0;
	if ( bNeedFull )
	{
		for ( TINT i = 0; i < 6; i++ )
			aiFaces[ iNumFaces++ ] = i;

		// Record the probe + box the "to" cube is captured with (consumed by the shaders)
		s_vCubeProbe[ s_iCubeTo ] = vProbe;
		s_vCubeBox[ s_iCubeTo ]   = vTargetBox;
		s_bCubeValid[ s_iCubeTo ] = TTRUE;
		s_flCubeRRTimer           = 0.0f;
		s_iCubeRRFace             = 0;
	}
	else
	{
		const TFLOAT flInterval = TMath::Max( remaster::g_flSkyCubeRefreshTime, 0.06f ) / 6.0f;
		s_flCubeRRTimer += a_flDeltaTime;
		if ( s_flCubeRRTimer >= flInterval )
		{
			s_flCubeRRTimer -= flInterval;
			aiFaces[ iNumFaces++ ] = s_iCubeRRFace;
			s_iCubeRRFace          = ( s_iCubeRRFace + 1 ) % 6;
		}
	}

	// Terrain in the probe is dynamic-lit without shadows: g_bCSMEnabled=FALSE picks the World NO_CSM combo. Restored below
	const TBOOL bTerrain                 = remaster::g_bReflectTerrain && ( *(TINT*)0x00796300 ) != 0;
	remaster::g_bCSMEnabled              = TFALSE;
	remaster::g_bReflectionCaptureActive = TTRUE; // grass renders base layer only during the capture

	// ATerrainInterface::Render early-outs unless the context has a non-null camera object; point it at the game's main one
	Toshi::TCameraObject* pOldCamObj = pCtx->GetCameraObject();

	// Stars draw via an immediate path (not the order table); suppress them so they don't smear across faces
	pSky->m_fStarsOpacity = 0.0f;

	// Shared square 90-degree face projection (SetFromFOV takes the half-angle -> PI/4)
	Toshi::TRenderContext::PROJECTIONPARAMS oFaceProj = oOldProj;
	oFaceProj.SetFromFOV( TFLOAT( SKYCUBE_SIZE ), TFLOAT( SKYCUBE_SIZE ), TMath::HALF_PI * 0.5f, oOldProj.m_fNearClip, oOldProj.m_fFarClip );

	Toshi::TRenderContext::VIEWPORTPARAMS oFaceViewport = {};
	oFaceViewport.fWidth                                = TFLOAT( SKYCUBE_SIZE );
	oFaceViewport.fHeight                               = TFLOAT( SKYCUBE_SIZE );
	oFaceViewport.fMaxZ                                 = 1.0f;

	pCtx->SetCameraMode( Toshi::TRenderContext::CameraMode_Perspective );
	pCtx->SetProjectionParams( oFaceProj );
	pCtx->SetViewportParameters( oFaceViewport );
	pCtx->SetCameraObject( ARenderer::GetSingleton()->m_pCameraObject ); // terrain needs a non-null camera object

	D3D11_VIEWPORT oFaceVP = {};
	oFaceVP.Width          = TFLOAT( SKYCUBE_SIZE );
	oFaceVP.Height         = TFLOAT( SKYCUBE_SIZE );
	oFaceVP.MaxDepth       = 1.0f;
	pDeviceContext->RSSetViewports( 1, &oFaceVP );

	// Sky writes only the cube face; make sure no G-buffer stays on slot 1
	remaster::g_pRender->SetSecondaryRenderTargetView( TNULL );

	// D3D cube-face order (+X,-X,+Y,-Y,+Z,-Z) as (forward.xyz, up.xyz), world +Y up
	static const TFLOAT kFaces[ 6 ][ 6 ] = {
		{ 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },
		{ -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, -1.0f },
		{ 0.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f },
	};

	static const TFLOAT kClear[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };

	for ( TINT idx = 0; idx < iNumFaces; idx++ )
	{
		const TINT    face = aiFaces[ idx ];
		const TFLOAT* f    = kFaces[ face ];

		// right = up x forward
		const TFLOAT rx = f[ 4 ] * f[ 2 ] - f[ 5 ] * f[ 1 ];
		const TFLOAT ry = f[ 5 ] * f[ 0 ] - f[ 3 ] * f[ 2 ];
		const TFLOAT rz = f[ 3 ] * f[ 1 ] - f[ 4 ] * f[ 0 ];

		// Camera-world matrix (rows = right/up/forward, translation = camera pos); view = its inverse.
		// UP row negated to cancel the engine projection's built-in vertical flip (m_f22 < 0); reflected basis is fine since sky+terrain cull NONE
		TMatrix44 oCamWorld;
		oCamWorld.m_f11 = rx;
		oCamWorld.m_f12 = ry;
		oCamWorld.m_f13 = rz;
		oCamWorld.m_f14 = 0.0f;
		oCamWorld.m_f21 = -f[ 3 ];
		oCamWorld.m_f22 = -f[ 4 ];
		oCamWorld.m_f23 = -f[ 5 ];
		oCamWorld.m_f24 = 0.0f;
		oCamWorld.m_f31 = f[ 0 ];
		oCamWorld.m_f32 = f[ 1 ];
		oCamWorld.m_f33 = f[ 2 ];
		oCamWorld.m_f34 = 0.0f;
		oCamWorld.m_f41 = vCamPos.x;
		oCamWorld.m_f42 = vCamPos.y;
		oCamWorld.m_f43 = vCamPos.z;
		oCamWorld.m_f44 = 1.0f;

		TMatrix44 oFaceView;
		oFaceView.InvertOrthogonal( oCamWorld );

		pCtx->SetWorldViewMatrix( oFaceView );
		pCtx->SetModelViewMatrix( oFaceView );
		pCtx->Update();

		// Point the cull camera + transform stack at this face so terrain culls correctly
		if ( pCamera )
			pCamera->m_Matrix = oCamWorld;
		rTransforms.Reset();
		rTransforms.PushNull().Identity();
		rTransforms.Push( oFaceView );

		remaster::g_pRender->SetRenderTargetView( s_pSkyCubeFaceRTV[ s_iCubeTo ][ face ], s_pSkyCubeDepthDSV );
		remaster::g_pRender->ClearRenderTarget( s_pSkyCubeFaceRTV[ s_iCubeTo ][ face ], kClear );
		pDeviceContext->ClearDepthStencilView( s_pSkyCubeDepthDSV, D3D11_CLEAR_DEPTH, 1.0f, 0 );

		// Sky first (background); it manages its own depth state
		CALL_THIS( 0x0042c670, void*, void, pSky ); // ASkyDome::Render1 (queues the dome)
		remaster::g_pRender->FlushShaders();        // draws the queued sky into this face

		// Terrain over the sky; re-clear depth so the sky's writes don't occlude it (World shader sets its own state)
		if ( bTerrain )
		{
			pDeviceContext->ClearDepthStencilView( s_pSkyCubeDepthDSV, D3D11_CLEAR_DEPTH, 1.0f, 0 );
			remaster::g_pRender->SetDepthEnabled( TTRUE );
			remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );

			CALL_THIS( 0x005ea8b0, void*, void, *(void**)0x00796300 ); // ATerrain::Render

			// Gates, trees, and instances only go in for a placed anchor (the camera-follow fallback re-renders every frame)
			if ( bAnchored )
			{
				if ( *(TINT*)0x0078de44 )
					CALL_THIS( 0x005dd5c0, void*, void, *(void**)0x0078de44 ); // AGateManager::Render

				if ( *(void**)0x00796304 )
					CALL_THIS( 0x005ef3a0, void*, void, *(void**)0x00796304 ); // ATreeManager::Render

				if ( *(void**)0x0078deb0 )
					CALL_THIS( 0x005e17a0, void*, void, *(void**)0x0078deb0 ); // AInstanceManager::Render
			}

			remaster::g_pRender->FlushShaders();
		}
	}

	if ( iNumFaces > 0 )
		pDeviceContext->GenerateMips( s_pSkyCubeSRV[ s_iCubeTo ] );

	if ( s_flCubeBlend < 1.0f )
	{
		const TFLOAT flStep = ( remaster::g_flSkyCubeBlendTime > 1e-4f ) ? ( a_flDeltaTime / remaster::g_flSkyCubeBlendTime ) : 1.0f;
		s_flCubeBlend       = TMath::Min( 1.0f, s_flCubeBlend + flStep );
	}

	const TINT iFrom                     = 1 - s_iCubeTo;
	remaster::g_oSkyCubeBlend.pSRVTo     = s_pSkyCubeSRV[ s_iCubeTo ];
	remaster::g_oSkyCubeBlend.pSRVFrom   = s_pSkyCubeSRV[ iFrom ];
	remaster::g_oSkyCubeBlend.vProbeTo   = s_vCubeProbe[ s_iCubeTo ];
	remaster::g_oSkyCubeBlend.vProbeFrom = s_vCubeProbe[ iFrom ];
	remaster::g_oSkyCubeBlend.vBoxTo     = s_vCubeBox[ s_iCubeTo ];
	remaster::g_oSkyCubeBlend.vBoxFrom   = s_vCubeBox[ iFrom ];
	remaster::g_oSkyCubeBlend.flBlend    = s_flCubeBlend;
	remaster::g_pSkyCubeSRV              = s_pSkyCubeSRV[ s_iCubeTo ]; // alias for debug view + enable checks

	if ( pCamera )
		*pCamera = oOldCamera;
	pCtx->SetCameraObject( pOldCamObj );

	rTransforms.Reset();
	rTransforms.PushNull().Identity();
	rTransforms.Push( oOldWorldView );

	remaster::g_bCSMEnabled              = bOldCSMEnabled;
	remaster::g_bReflectionCaptureActive = TFALSE;

	pCtx->SetCameraMode( eOldCameraMode );
	pCtx->SetProjectionParams( oOldProj );
	pCtx->SetViewportParameters( oOldViewport );
	pCtx->SetWorldViewMatrix( oOldWorldView );
	pCtx->SetModelViewMatrix( oOldModelView );
	pCtx->Update();

	pSky->m_fStarsOpacity = fOldStars;

	pDeviceContext->RSSetViewports( 1, &oOldVP );
	remaster::g_pRender->ClearStateCache();
}

MEMBER_HOOK( 0x0060b370, ARenderer, ARenderer_RenderMainScene, void, TFLOAT a_flDeltaTime )
{
	TPROFILER_SCOPE();

	// Flag the world/reflection pass so the shared world/skin shaders apply CSM here but not the later UI pass; RAII clears it on every return
	struct MainScenePassScope
	{
		MainScenePassScope() { remaster::g_bInMainScenePass = TTRUE; }
		~MainScenePassScope() { remaster::g_bInMainScenePass = TFALSE; }
	} oMainScenePassScope;

	g_bHasGlowObjectsThisFrame = TFALSE;
	g_pLightDataPacketAllocator->Reset();

	// Advance the world-wind animation phase (consumed by the World_WIND shader permutation)
	if ( remaster::g_bWindEnabled )
		remaster::g_flWindTime += a_flDeltaTime * remaster::g_flWindSpeed;

	auto pSwapChainDesc = remaster::g_pRender->GetSwapChainDesc();

	auto& csmManager = remaster::g_pRender->GetCSMManager();

	csmManager.UpdateCascades( remaster::g_pRender->GetCurrentContext() );
	remaster::g_pRender->UpdateShadowCBuffer( csmManager.GetShadowCBufferData() );

	if ( remaster::g_bCSMEnabled )
		csmManager.RenderShadowMaps();

	remaster::g_pRender->GetLightManager().RenderDynamicLightShadowMaps();

	// Reload the per-level static lights on level change (the file is authoritative for each level's set)
	static TINT s_iLastStaticLightLevel = -1;
	if ( remaster::g_pLightManager )
	{
		const TINT iCurLevel = remaster::CubemapAnchors_GetCurrentLevel();
		if ( iCurLevel >= 0 && iCurLevel != s_iLastStaticLightLevel )
		{
			remaster::StaticLights_LoadForCurrentLevel( "Data\\StaticLights.xml" );
			// Editor dynamic lights load per level too. The old level's glow objects died with its viewport,
			// so pass "glow objects invalid" to detach the old entries instead of freeing them through stale pointers
			editor::DynamicLights_LoadForCurrentLevel( "Data\\DynamicLights.xml", TTRUE );
			// Per-level render settings snapshot (no-op when the level has none saved)
			editor::LevelSettings_ApplyForCurrentLevel( "Data\\LevelSettings.xml" );
			s_iLastStaticLightLevel = iCurLevel;
		}
	}

	// Static lights don't move, so upload them once per frame before the scene pass
	remaster::g_pRender->GetLightManager().UploadStaticLightsGlobalCBuffer();

	// Cloud shadow bake (animated top-down sun-amount map, sampled in SampleShadow)
	if ( remaster::g_bCloudShadowsEnabled && s_pCloudShadowRTV )
	{
		static TFLOAT s_flCloudTime = 0.0f;
		s_flCloudTime += a_flDeltaTime * remaster::g_flCloudShadowSpeed;

		const auto&  shadowData  = csmManager.GetShadowCBufferData();
		const TFLOAT fRegionSize = ( shadowData.cloudParams[ 2 ] > 0.0f ) ? ( 1.0f / shadowData.cloudParams[ 2 ] ) : remaster::g_flCloudShadowRegionSize;

		CloudShadowCBuffer cbData = {};
		cbData.region[ 0 ]        = shadowData.cloudParams[ 0 ];
		cbData.region[ 1 ]        = shadowData.cloudParams[ 1 ];
		cbData.region[ 2 ]        = fRegionSize;
		cbData.region[ 3 ]        = remaster::g_flCloudShadowFeatureScale;
		cbData.anim[ 0 ]          = s_flCloudTime;
		cbData.anim[ 1 ]          = remaster::g_flCloudShadowWindDir[ 0 ];
		cbData.anim[ 2 ]          = remaster::g_flCloudShadowWindDir[ 1 ];
		cbData.anim[ 3 ]          = remaster::g_flCloudShadowCoverage;
		cbData.shape[ 0 ]         = remaster::g_flCloudShadowDensity;
		cbData.shape[ 1 ]         = TMath::Max( remaster::g_flCloudShadowContrast, 0.01f );

		D3D11_MAPPED_SUBRESOURCE mapped;
		remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pCloudShadowCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
		TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
		remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pCloudShadowCBuffer, 0 );

		D3D11_VIEWPORT oOldVP;
		TUINT          uiNumVP = 1;
		remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiNumVP, &oOldVP );
		D3D11_VIEWPORT oCloudVP = oOldVP;
		oCloudVP.TopLeftX       = 0.0f;
		oCloudVP.TopLeftY       = 0.0f;
		oCloudVP.Width          = TFLOAT( s_uiCloudShadowRes );
		oCloudVP.Height         = TFLOAT( s_uiCloudShadowRes );
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oCloudVP );

		remaster::g_pRender->SetRenderTargetView( s_pCloudShadowRTV, TNULL );
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->PSSetConstantBuffer( 1, s_pCloudShadowCBuffer );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetCloudShadowPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::CloudShadow_NoCombos )
		);
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );

		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oOldVP );
	}

	static constexpr TFLOAT aflSkyMaskClearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// Bind the G-buffer on slot 1 for the whole scene pass (only when SSR needs it);
	// it persists across the glow path's colour-target swaps via the secondary RTV
	const TBOOL bGBufferActive = remaster::g_bSSREnabled;
	if ( bGBufferActive )
	{
		remaster::g_pRender->ClearRenderTarget( remaster::g_pRender->GetD3D11GBufferRTV(), aflSkyMaskClearColor );
		remaster::g_pRender->SetSecondaryRenderTargetView( remaster::g_pRender->GetD3D11GBufferRTV() );
	}

	remaster::g_pRender->SetRenderTargetView(
	    remaster::g_pRender->GetD3D11RenderTargetView(),
	    remaster::g_pRender->GetD3D11DepthStencilView()
	);

	remaster::g_pRender->GetD3D11DeviceContext()->ClearDepthStencilView( remaster::g_pRender->GetD3D11DepthStencilView(), D3D11_CLEAR_DEPTH, 1.0f, 0 );

	remaster::g_pRender->ClearRenderTarget( remaster::g_pRender->GetD3D11GlowRenderTargetView(), aflSkyMaskClearColor );

	// HACK: make sure depth is not cleared before skymask is generated
	remaster::g_bAllowClearingDepth = TFALSE;
	CallOriginal( a_flDeltaTime );
	remaster::g_bAllowClearingDepth = TTRUE;

	// Detach the G-buffer and resolve it (MSAA -> non-MSAA) for SSR
	if ( bGBufferActive )
	{
		// Drop slot 1 by rebinding the colour target alone (dirty flag forces the rebind) so the G-buffer isn't bound as an RTV while we resolve it
		remaster::g_pRender->SetSecondaryRenderTargetView( TNULL );
		remaster::g_pRender->SetRenderTargetView(
		    remaster::g_pRender->GetD3D11RenderTargetView(),
		    remaster::g_pRender->GetD3D11DepthStencilView()
		);
		remaster::g_pRender->GetD3D11DeviceContext()->ResolveSubresource(
		    s_pResolvedGBufferTexture, 0, remaster::g_pRender->GetD3D11GBufferTexture(), 0, DXGI_FORMAT_R8G8B8A8_UNORM
		);
	}

	// Resolve MSAA color -> non-MSAA (needed by sky mask shader)
	{
		TPROFILER_NAMED( "MSAA Resolve" );
		TracyD3D11Zone( remaster::g_pRender->GetTracyGpuContext(), "MSAA Resolve" );

		remaster::g_pRender->GetD3D11DeviceContext()->ResolveSubresource(
		    s_pResolvedColorTexture, 0, remaster::g_pRender->GetD3D11RenderTargetTexture(), 0, DXGI_FORMAT_R11G11B10_FLOAT
		);

		remaster::g_pRender->GetD3D11DeviceContext()->ResolveSubresource(
		    s_pResolvedGlowTexture, 0, remaster::g_pRender->GetD3D11GlowRenderTargetTexture(), 0, DXGI_FORMAT_R8G8B8A8_UNORM
		);
	}

	// Resolve MSAA depth -> R32_FLOAT via fullscreen pass (full-res viewport)
	{
		TPROFILER_NAMED( "MSAA Depth Resolve" );
		TracyD3D11Zone( remaster::g_pRender->GetTracyGpuContext(), "MSAA Depth Resolve" );

		remaster::g_pRender->DiscardView( s_pResolvedDepthRTV );
		remaster::g_pRender->SetRenderTargetView( s_pResolvedDepthRTV, TNULL );
		remaster::g_pRender->PSSetShaderResource( 0, remaster::g_pRender->GetD3D11DepthStencilSRV() );
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetResolveDepthPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::ResolveDepth_NoCombos )
		);
		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
	}

	// Half-res min-depth downsample, consumed by the SSR march and HBAO. Point-sampling full-res depth at half-res
	// picks an unstable texel per 2x2 (HBAO crawled on grazing ground); min gives one stable depth. XeGTAO and the fog keep full-res
	const TBOOL bHBAOWantsHalfDepth = remaster::g_bHBAOEnabled && remaster::g_iAOAlgorithm != 1;
	if ( ( remaster::g_bSSREnabled || bHBAOWantsHalfDepth ) && s_pHalfDepthRTV )
	{
		TPROFILER_NAMED( "Half-Depth Downsample" );
		TracyD3D11Zone( remaster::g_pRender->GetTracyGpuContext(), "Half-Depth Downsample" );

		D3D11_VIEWPORT oOldVP;
		TUINT          uiNumVP = 1;
		remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiNumVP, &oOldVP );
		D3D11_VIEWPORT oHalfVP = oOldVP;
		oHalfVP.Width          = TFLOAT( s_uiHBAOWidth );
		oHalfVP.Height         = TFLOAT( s_uiHBAOHeight );
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oHalfVP );

		remaster::g_pRender->DiscardView( s_pHalfDepthRTV );
		remaster::g_pRender->SetRenderTargetView( s_pHalfDepthRTV, TNULL );
		remaster::g_pRender->PSSetShaderResource( 0, s_pResolvedDepthSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pPointClampSampler );
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetDownsampleDepthMinPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::DownsampleDepthMin_NoCombos )
		);
		remaster::g_pRender->PSSetShaderResource( 0, TNULL );

		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oOldVP );
	}

	if ( remaster::g_bHBAOEnabled )
	{
		TPROFILER_NAMED( "HBAO" );
		TracyD3D11Zone( remaster::g_pRender->GetTracyGpuContext(), "HBAO" );

		auto             pContext = TSTATICCAST( remaster::RenderContextD3D11, m_pViewport->GetRenderContext() );
		const TMatrix44& proj     = pContext->GetProjectionMatrix();

		// AO + blur run at half res; shrink the viewport to match, restore before the full-res composite
		D3D11_VIEWPORT oHBAOOldVP;
		TUINT          uiHBAONumVP = 1;
		remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiHBAONumVP, &oHBAOOldVP );
		D3D11_VIEWPORT oHBAOHalfVP = oHBAOOldVP;
		oHBAOHalfVP.Width          = TFLOAT( s_uiHBAOWidth );
		oHBAOHalfVP.Height         = TFLOAT( s_uiHBAOHeight );
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oHBAOHalfVP );

		remaster::g_pRender->DiscardView( s_pHBAORTV );
		remaster::g_pRender->SetRenderTargetView( s_pHBAORTV, TNULL );
		// HBAO reads the half-res min-depth (matches its render target); XeGTAO takes full-res
		remaster::g_pRender->PSSetShaderResource( 0, remaster::g_iAOAlgorithm == 1 ? s_pResolvedDepthSRV : s_pHalfDepthSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pPointClampSampler );
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );

		if ( remaster::g_iAOAlgorithm == 1 )
		{
			XeGTAOCBuffer cbData;
			cbData.projection[ 0 ]  = proj.m_f11;
			cbData.projection[ 1 ]  = proj.m_f22;
			cbData.projection[ 2 ]  = proj.m_f31;
			cbData.projection[ 3 ]  = proj.m_f32;
			cbData.depthParams[ 0 ] = proj.m_f33;
			cbData.depthParams[ 1 ] = proj.m_f43;
			cbData.depthParams[ 2 ] = pContext->GetProjectionParams().m_fNearClip;
			cbData.depthParams[ 3 ] = pContext->GetProjectionParams().m_fFarClip;
			cbData.params[ 0 ]      = remaster::g_flHBAORadius * remaster::g_flHBAOSceneScale;
			cbData.params[ 1 ]      = remaster::g_flXeGTAOFalloffRange;
			cbData.params[ 2 ]      = remaster::g_flHBAOIntensity;
			cbData.params[ 3 ]      = remaster::g_flHBAOPower;
			cbData.bufferSize[ 0 ]  = TFLOAT( s_uiHBAOWidth );
			cbData.bufferSize[ 1 ]  = TFLOAT( s_uiHBAOHeight );
			cbData.bufferSize[ 2 ]  = 1.0f / cbData.bufferSize[ 0 ];
			cbData.bufferSize[ 3 ]  = 1.0f / cbData.bufferSize[ 1 ];
			cbData.xeParams[ 0 ]    = remaster::g_flXeGTAORadiusMultiplier;
			cbData.xeParams[ 1 ]    = remaster::g_flXeGTAOSampleDistributionPower;
			cbData.xeParams[ 2 ]    = remaster::g_flXeGTAOThinOccluderCompensation;
			cbData.xeParams[ 3 ]    = 0.0f;

			D3D11_MAPPED_SUBRESOURCE mapped;
			remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pXeGTAOConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
			remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pXeGTAOConstantBuffer, 0 );

			remaster::g_pRender->PSSetConstantBuffer( 1, s_pXeGTAOConstantBuffer );
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetXeGTAOPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::XeGTAO_NoCombos )
			);
		}
		else
		{
			HBAOCBuffer cbData;
			cbData.projection[ 0 ]  = proj.m_f11;
			cbData.projection[ 1 ]  = proj.m_f22;
			cbData.projection[ 2 ]  = proj.m_f31;
			cbData.projection[ 3 ]  = proj.m_f32;
			cbData.depthParams[ 0 ] = proj.m_f33;
			cbData.depthParams[ 1 ] = proj.m_f43;
			cbData.depthParams[ 2 ] = pContext->GetProjectionParams().m_fNearClip;
			cbData.depthParams[ 3 ] = pContext->GetProjectionParams().m_fFarClip;
			cbData.params[ 0 ]      = remaster::g_flHBAORadius * remaster::g_flHBAOSceneScale;
			cbData.params[ 1 ]      = remaster::g_flHBAOBias;
			cbData.params[ 2 ]      = remaster::g_flHBAOIntensity;
			cbData.params[ 3 ]      = remaster::g_flHBAOPower;
			cbData.bufferSize[ 0 ]  = TFLOAT( s_uiHBAOWidth );
			cbData.bufferSize[ 1 ]  = TFLOAT( s_uiHBAOHeight );
			cbData.bufferSize[ 2 ]  = 1.0f / cbData.bufferSize[ 0 ];
			cbData.bufferSize[ 3 ]  = 1.0f / cbData.bufferSize[ 1 ];

			D3D11_MAPPED_SUBRESOURCE mapped;
			remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pHBAOConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
			remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pHBAOConstantBuffer, 0 );

			remaster::g_pRender->PSSetConstantBuffer( 1, s_pHBAOConstantBuffer );
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetHBAOPlusPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::HBAOPlus_NoCombos )
			);
		}

		remaster::g_pRender->PSSetShaderResource( 0, TNULL );

		const TFLOAT fNearClip = pContext->GetProjectionParams().m_fNearClip;
		const TFLOAT fFarClip  = pContext->GetProjectionParams().m_fFarClip;

		auto fnBlurHBAO = [ fNearClip, fFarClip ]( ID3D11RenderTargetView* a_pRTV, ID3D11ShaderResourceView* a_pInputSRV, TFLOAT a_fDirX, TFLOAT a_fDirY ) {
			HBAOBlurCBuffer blurData;
			blurData.blurParams[ 0 ]  = 1.0f / TFLOAT( s_uiHBAOWidth );
			blurData.blurParams[ 1 ]  = 1.0f / TFLOAT( s_uiHBAOHeight );
			blurData.blurParams[ 2 ]  = a_fDirX;
			blurData.blurParams[ 3 ]  = a_fDirY;
			blurData.depthParams[ 0 ] = fNearClip;
			blurData.depthParams[ 1 ] = fFarClip;
			blurData.depthParams[ 2 ] = remaster::g_flHBAOBlurSharpness;
			blurData.depthParams[ 3 ] = 0.0f;

			D3D11_MAPPED_SUBRESOURCE mappedBlur;
			remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pHBAOBlurConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedBlur );
			TUtil::MemCopy( mappedBlur.pData, &blurData, sizeof( blurData ) );
			remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pHBAOBlurConstantBuffer, 0 );

			remaster::g_pRender->DiscardView( a_pRTV );
			remaster::g_pRender->SetRenderTargetView( a_pRTV, TNULL );
			remaster::g_pRender->PSSetShaderResource( 0, a_pInputSRV );
			remaster::g_pRender->PSSetShaderResource( 1, s_pResolvedDepthSRV );
			remaster::g_pRender->PSSetSamplerState( 0, s_pPointClampSampler );
			remaster::g_pRender->PSSetSamplerState( 1, s_pLinearClampSampler );
			remaster::g_pRender->PSSetConstantBuffer( 1, s_pHBAOBlurConstantBuffer );
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetHBAOBlurPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::HBAOBlur_NoCombos )
			);
			remaster::g_pRender->PSSetShaderResource( 0, TNULL );
			remaster::g_pRender->PSSetShaderResource( 1, TNULL );
		};

		fnBlurHBAO( s_pHBAOBlurRTV, s_pHBAOSRV, 1.0f, 0.0f );
		fnBlurHBAO( s_pHBAORTV, s_pHBAOBlurSRV, 0.0f, 1.0f );

		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oHBAOOldVP );

		remaster::g_pRender->SetRenderTargetView(
		    remaster::g_pRender->GetD3D11RenderTargetView(),
		    remaster::g_pRender->GetD3D11DepthStencilView()
		);
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->PSSetShaderResource( 0, s_pHBAOSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->SetBlendEnabled( TFALSE );

		if ( remaster::g_bHBAODebug )
		{
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetHBAOCompositePixelShaderCombo_ps_debug().GetPixelShader( remaster::shadercombos::HBAOComposite_NoCombos )
			);
		}
		else
		{
			remaster::g_pRender->PSSetShaderResource( 1, s_pResolvedColorSRV );
			remaster::g_pRender->PSSetSamplerState( 1, s_pLinearClampSampler );
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetHBAOCompositePixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::HBAOComposite_NoCombos )
			);
			remaster::g_pRender->PSSetShaderResource( 1, TNULL );
		}

		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
	}

	// Gate the capture on the sky cube, not SSR: the forward world/skin IBL samples it even with SSR off (gating on SSR left a stale cube)
	if ( remaster::g_bSkyCubeEnabled )
		CaptureCubeMap( a_flDeltaTime );

	// Sky cube debug: draw the cube as a skybox to verify face orientation (replaces SSR while enabled)
	if ( remaster::g_bSkyCubeEnabled && remaster::g_bSkyCubeDebugView && remaster::g_pSkyCubeSRV )
	{
		TPROFILER_NAMED( "Sky Cube Debug" );

		auto             pCtx = TSTATICCAST( remaster::RenderContextD3D11, m_pViewport->GetRenderContext() );
		const TMatrix44& proj = pCtx->GetProjectionMatrix();

		SSRCBuffer cbData      = {};
		cbData.projection[ 0 ] = proj.m_f11;
		cbData.projection[ 1 ] = proj.m_f22;
		cbData.projection[ 2 ] = proj.m_f31;
		cbData.projection[ 3 ] = proj.m_f32;
		cbData.worldToView.InvertOrthogonal( pCtx->GetViewWorldMatrix() );
		cbData.skyCubeParams[ 0 ] = TFLOAT( remaster::g_iSkyCubeMaxMip );
		cbData.skyCubeParams[ 1 ] = 1.0f;
		cbData.skyCubeParams[ 2 ] = remaster::g_flSkyCubeIntensity;

		D3D11_MAPPED_SUBRESOURCE mapped;
		remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pSSRConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
		TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
		remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pSSRConstantBuffer, 0 );

		D3D11_VIEWPORT oFullVP = {};
		oFullVP.Width          = remaster::g_pRender->GetSurfaceWidth();
		oFullVP.Height         = remaster::g_pRender->GetSurfaceHeight();
		oFullVP.MaxDepth       = 1.0f;
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oFullVP );

		remaster::g_pRender->SetRenderTargetView( remaster::g_pRender->GetD3D11RenderTargetView(), TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 1, s_pSSRConstantBuffer );
		remaster::g_pRender->PSSetSamplerState( 1, s_pLinearClampSampler );
		remaster::g_pRender->PSSetShaderResource( 4, remaster::g_pSkyCubeSRV );
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetSSRPixelShaderCombo_ps_debug_skycube().GetPixelShader( remaster::shadercombos::SSR_NoCombos )
		);
		remaster::g_pRender->PSSetShaderResource( 4, TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
	}

	if ( remaster::g_bSSREnabled && !remaster::g_bSkyCubeDebugView )
	{
		TPROFILER_NAMED( "SSR" );
		TracyD3D11Zone( remaster::g_pRender->GetTracyGpuContext(), "SSR" );

		auto             pCtx      = TSTATICCAST( remaster::RenderContextD3D11, m_pViewport->GetRenderContext() );
		const TMatrix44& proj      = pCtx->GetProjectionMatrix();
		const TFLOAT     fNearClip = pCtx->GetProjectionParams().m_fNearClip;
		const TFLOAT     fFarClip  = pCtx->GetProjectionParams().m_fFarClip;

		SSRCBuffer cbData       = {};
		cbData.projection[ 0 ]  = proj.m_f11;
		cbData.projection[ 1 ]  = proj.m_f22;
		cbData.projection[ 2 ]  = proj.m_f31;
		cbData.projection[ 3 ]  = proj.m_f32;
		cbData.depthParams[ 0 ] = proj.m_f33;
		cbData.depthParams[ 1 ] = proj.m_f43;
		cbData.depthParams[ 2 ] = fNearClip;
		cbData.depthParams[ 3 ] = fFarClip;
		cbData.params[ 0 ]      = remaster::g_flSSRIntensity;
		cbData.params[ 1 ]      = remaster::g_flSSRMaxDistance;
		cbData.params[ 2 ]      = remaster::g_flSSRThickness;
		cbData.params[ 3 ]      = remaster::g_flSSRFresnelPower;
		cbData.bufferSize[ 0 ]  = TFLOAT( s_uiSSRWidth );
		cbData.bufferSize[ 1 ]  = TFLOAT( s_uiSSRHeight );
		cbData.bufferSize[ 2 ]  = 1.0f / cbData.bufferSize[ 0 ];
		cbData.bufferSize[ 3 ]  = 1.0f / cbData.bufferSize[ 1 ];
		cbData.marchParams[ 0 ] = TFLOAT( remaster::g_iSSRMaxSteps );
		cbData.marchParams[ 1 ] = remaster::g_flSSRStepSize;
		cbData.marchParams[ 2 ] = remaster::g_flSSREdgeFade;
		cbData.marchParams[ 3 ] = 0.0f;
		cbData.blurDepth[ 0 ]   = fNearClip;
		cbData.blurDepth[ 1 ]   = fFarClip;
		cbData.blurDepth[ 2 ]   = remaster::g_flHBAOBlurSharpness;
		cbData.blurDepth[ 3 ]   = 0.0f;

		cbData.worldToView.InvertOrthogonal( pCtx->GetViewWorldMatrix() );

		// No sky-gradient ray-miss fallback now (forward pass draws the cube at full res, SSR only adds hits); slots stay for layout, carry zeros
		cbData.skyHorizon[ 0 ] = 0.0f;
		cbData.skyHorizon[ 1 ] = 0.0f;
		cbData.skyHorizon[ 2 ] = 0.0f;
		cbData.skyHorizon[ 3 ] = 0.0f;
		cbData.skyZenith[ 0 ]  = 0.0f;
		cbData.skyZenith[ 1 ]  = 0.0f;
		cbData.skyZenith[ 2 ]  = 0.0f;
		cbData.skyZenith[ 3 ]  = 0.0f;

		// Sky cubemap fallback: enabled only when the cube was captured this frame
		cbData.skyCubeParams[ 0 ] = TFLOAT( remaster::g_iSkyCubeMaxMip );
		cbData.skyCubeParams[ 1 ] = ( remaster::g_bSkyCubeEnabled && remaster::g_pSkyCubeSRV ) ? 1.0f : 0.0f;
		cbData.skyCubeParams[ 2 ] = remaster::g_flSkyCubeIntensity;
		cbData.skyCubeParams[ 3 ] = 0.0f;

		// Cross-fade parallax anchoring: each cube carries its own probe + box. The shader samples both with
		// offset = (camera - probe) so each stays pinned to its world point, then lerps by the blend (skyCubeOffset.w; 1 = fully "to")
		const remaster::SkyCubeBlendState& rBlend = remaster::g_oSkyCubeBlend;
		const TVector4&                    vCam   = pCtx->GetViewWorldMatrix().GetTranslation();

		cbData.skyCubeParallax[ 0 ] = TMath::Max( rBlend.vBoxTo.x, 0.01f );
		cbData.skyCubeParallax[ 1 ] = TMath::Max( rBlend.vBoxTo.y, 0.01f );
		cbData.skyCubeParallax[ 2 ] = TMath::Max( rBlend.vBoxTo.z, 0.01f );
		cbData.skyCubeParallax[ 3 ] = 1.0f;
		cbData.skyCubeOffset[ 0 ]   = vCam.x - rBlend.vProbeTo.x;
		cbData.skyCubeOffset[ 1 ]   = vCam.y - rBlend.vProbeTo.y;
		cbData.skyCubeOffset[ 2 ]   = vCam.z - rBlend.vProbeTo.z;
		cbData.skyCubeOffset[ 3 ]   = rBlend.flBlend;

		cbData.skyCubeParallax2[ 0 ] = TMath::Max( rBlend.vBoxFrom.x, 0.01f );
		cbData.skyCubeParallax2[ 1 ] = TMath::Max( rBlend.vBoxFrom.y, 0.01f );
		cbData.skyCubeParallax2[ 2 ] = TMath::Max( rBlend.vBoxFrom.z, 0.01f );
		cbData.skyCubeParallax2[ 3 ] = 0.0f;
		cbData.skyCubeOffset2[ 0 ]   = vCam.x - rBlend.vProbeFrom.x;
		cbData.skyCubeOffset2[ 1 ]   = vCam.y - rBlend.vProbeFrom.y;
		cbData.skyCubeOffset2[ 2 ]   = vCam.z - rBlend.vProbeFrom.z;
		cbData.skyCubeOffset2[ 3 ]   = 0.0f;

		auto fnUploadSSRCB = [ & ]() {
			D3D11_MAPPED_SUBRESOURCE mapped;
			remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pSSRConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
			remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pSSRConstantBuffer, 0 );
		};

		// SSR-resolution viewport for the gather + blur
		D3D11_VIEWPORT oOldVP;
		TUINT          uiNumVP = 1;
		remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiNumVP, &oOldVP );
		D3D11_VIEWPORT oHalfVP = oOldVP;
		oHalfVP.Width          = TFLOAT( s_uiSSRWidth );
		oHalfVP.Height         = TFLOAT( s_uiSSRHeight );
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oHalfVP );

		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->PSSetSamplerState( 0, s_pPointClampSampler );
		remaster::g_pRender->PSSetSamplerState( 1, s_pLinearClampSampler );
		remaster::g_pRender->PSSetConstantBuffer( 1, s_pSSRConstantBuffer );

		// Gather: depth (t0) + resolved scene colour (t1) -> reflection buffer
		fnUploadSSRCB();
		remaster::g_pRender->DiscardView( s_pSSRRTV );
		remaster::g_pRender->SetRenderTargetView( s_pSSRRTV, TNULL );
		remaster::g_pRender->PSSetShaderResource( 0, s_pResolvedDepthSRV ); // full-res depth
		remaster::g_pRender->PSSetShaderResource( 1, s_pResolvedColorSRV );
		remaster::g_pRender->PSSetShaderResource( 3, s_pResolvedGBufferSRV );
		remaster::g_pRender->PSSetShaderResource( 4, remaster::g_oSkyCubeBlend.pSRVTo );   // active cube fallback
		remaster::g_pRender->PSSetShaderResource( 5, remaster::g_oSkyCubeBlend.pSRVFrom ); // outgoing cube (cross-fade)
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetSSRPixelShaderCombo_ps_gather().GetPixelShader( remaster::shadercombos::SSR_NoCombos )
		);
		remaster::g_pRender->PSSetShaderResource( 1, TNULL );
		remaster::g_pRender->PSSetShaderResource( 3, TNULL );
		remaster::g_pRender->PSSetShaderResource( 4, TNULL );
		remaster::g_pRender->PSSetShaderResource( 5, TNULL );

		// Separable depth-aware bilateral blur: horizontal then vertical
		auto fnBlurSSR = [ & ]( ID3D11RenderTargetView* a_pRTV, ID3D11ShaderResourceView* a_pInput, TFLOAT a_fDirX, TFLOAT a_fDirY ) {
			cbData.blurParams[ 0 ] = 1.0f / TFLOAT( s_uiSSRWidth );
			cbData.blurParams[ 1 ] = 1.0f / TFLOAT( s_uiSSRHeight );
			cbData.blurParams[ 2 ] = a_fDirX;
			cbData.blurParams[ 3 ] = a_fDirY;
			fnUploadSSRCB();

			remaster::g_pRender->DiscardView( a_pRTV );
			remaster::g_pRender->SetRenderTargetView( a_pRTV, TNULL );
			remaster::g_pRender->PSSetShaderResource( 0, s_pResolvedDepthSRV ); // full-res depth
			remaster::g_pRender->PSSetShaderResource( 2, a_pInput );
			remaster::g_pRender->PSSetShaderResource( 3, s_pResolvedGBufferSRV ); // roughness for blur width
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetSSRPixelShaderCombo_ps_blur().GetPixelShader( remaster::shadercombos::SSR_NoCombos )
			);
			remaster::g_pRender->PSSetShaderResource( 2, TNULL );
			remaster::g_pRender->PSSetShaderResource( 3, TNULL );
		};

		fnBlurSSR( s_pSSRBlurRTV, s_pSSRSRV, 1.0f, 0.0f );
		fnBlurSSR( s_pSSRRTV, s_pSSRBlurSRV, 0.0f, 1.0f );

		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oOldVP );
		remaster::g_pRender->SetRenderTargetView(
		    remaster::g_pRender->GetD3D11RenderTargetView(),
		    remaster::g_pRender->GetD3D11DepthStencilView()
		);
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->PSSetShaderResource( 2, s_pSSRSRV );

		if ( remaster::g_bSSRDebugNormals )
		{
			// Full-screen view of the G-buffer world normals (diagnose world vs view space)
			remaster::g_pRender->PSSetShaderResource( 3, s_pResolvedGBufferSRV );
			remaster::g_pRender->SetBlendEnabled( TFALSE );
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetSSRPixelShaderCombo_ps_debug_normal().GetPixelShader( remaster::shadercombos::SSR_NoCombos )
			);
			remaster::g_pRender->PSSetShaderResource( 3, TNULL );
		}
		else if ( remaster::g_bSSRDebug )
		{
			remaster::g_pRender->SetBlendEnabled( TFALSE );
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetSSRPixelShaderCombo_ps_debug().GetPixelShader( remaster::shadercombos::SSR_NoCombos )
			);
		}
		else
		{
			// Premultiplied composite (gather bakes confidence into rgb): ONE / INV_SRC_ALPHA keeps the bilateral blur from bleeding reflections across confidence edges
			remaster::g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA );
			remaster::g_pRender->SetBlendEnabled( TTRUE );
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetSSRPixelShaderCombo_ps_composite().GetPixelShader( remaster::shadercombos::SSR_NoCombos )
			);
			remaster::g_pRender->SetBlendEnabled( TFALSE );
		}

		remaster::g_pRender->PSSetShaderResource( 2, TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
	}

	// Volumetric fog pass (half-res, raymarched, CSM-shadowed)
	if ( remaster::g_bCSMEnabled && remaster::g_bVolumetricFogEnabled )
	{
		TPROFILER_NAMED( "Volumetrics" );
		TracyD3D11Zone( remaster::g_pRender->GetTracyGpuContext(), "Volumetrics" );

		auto             pFogCtx = TSTATICCAST( remaster::RenderContextD3D11, m_pViewport->GetRenderContext() );
		const TMatrix44& proj    = pFogCtx->GetProjectionMatrix();

		TMatrix44 matViewWorld = pFogCtx->GetViewWorldMatrix();
		if ( *(void**)0x007822e0 )
		{
			ACamera* pCamera = CALL_THIS( 0x0045b870, void*, ACamera*, *(void**)0x007822e0 ); // ACameraManager::GetCurrentCamera
			if ( pCamera )
				matViewWorld = pCamera->m_Matrix;
		}

		TMatrix44 matWorldView;
		matWorldView.InvertOrthogonal( matViewWorld );

		// Light direction toward sun in view space; match the sun-shafts convention (-x, +y, -z) -- Y must NOT be negated.
		// Negating Y flips the phase vertically so forward scattering (g > 0) brightens under the sun (invisible while g = 0)
		TVector3 fogLightDir = csmManager.GetLightDirection();
		fogLightDir.Normalize();
		TVector4 fogLightDirWorld( -fogLightDir.x, fogLightDir.y, -fogLightDir.z, 0.0f );
		TVector4 fogLightDirView;
		TMatrix44::RotateVector( fogLightDirView, matWorldView, fogLightDirWorld );

		TFLOAT fogLightDirLen = TMath::Sqrt(
		    fogLightDirView.x * fogLightDirView.x +
		    fogLightDirView.y * fogLightDirView.y +
		    fogLightDirView.z * fogLightDirView.z
		);
		if ( fogLightDirLen > 0.0001f )
		{
			fogLightDirView.x /= fogLightDirLen;
			fogLightDirView.y /= fogLightDirLen;
			fogLightDirView.z /= fogLightDirLen;
		}

		const auto& shadowData = csmManager.GetShadowCBufferData();

		VolumetricFogCBuffer cbFog = {};
		for ( TINT i = 0; i < 3; i++ )
			TUtil::MemCopy( cbFog.matLightVP[ i ], &shadowData.matLightVP[ i ], sizeof( TMatrix44 ) );
		cbFog.cascadeSplits[ 0 ] = shadowData.cascadeSplits[ 0 ];
		cbFog.cascadeSplits[ 1 ] = shadowData.cascadeSplits[ 1 ];
		cbFog.cascadeSplits[ 2 ] = shadowData.cascadeSplits[ 2 ];
		cbFog.cascadeSplits[ 3 ] = shadowData.cascadeSplits[ 3 ];
		cbFog.shadowBias[ 0 ]    = shadowData.shadowParams[ 0 ];
		cbFog.shadowBias[ 1 ]    = shadowData.cascadeScales[ 0 ];
		cbFog.shadowBias[ 2 ]    = shadowData.cascadeScales[ 1 ];
		cbFog.shadowBias[ 3 ]    = shadowData.cascadeScales[ 2 ];
		cbFog.matViewWorld       = matViewWorld;
		cbFog.projection[ 0 ]    = proj.m_f11;
		cbFog.projection[ 1 ]    = proj.m_f22;
		cbFog.projection[ 2 ]    = proj.m_f31;
		cbFog.projection[ 3 ]    = proj.m_f32;
		cbFog.depthParams[ 0 ]   = 0.0f;
		cbFog.depthParams[ 1 ]   = 0.0f;
		cbFog.depthParams[ 2 ]   = pFogCtx->GetProjectionParams().m_fNearClip;
		cbFog.depthParams[ 3 ]   = pFogCtx->GetProjectionParams().m_fFarClip;
		cbFog.lightDirVS[ 0 ]    = fogLightDirView.x;
		cbFog.lightDirVS[ 1 ]    = fogLightDirView.y;
		cbFog.lightDirVS[ 2 ]    = fogLightDirView.z;
		cbFog.lightDirVS[ 3 ]    = 0.0f;
		if ( remaster::g_bVolumetricFogUseSceneColor )
		{
			// Scene distance-fog colour times the authored colour, so volumetrics take the level's fog palette with a tint
			cbFog.fogColor[ 0 ] = pFogCtx->m_FogColor.x * remaster::g_flVolumetricFogColor[ 0 ];
			cbFog.fogColor[ 1 ] = pFogCtx->m_FogColor.y * remaster::g_flVolumetricFogColor[ 1 ];
			cbFog.fogColor[ 2 ] = pFogCtx->m_FogColor.z * remaster::g_flVolumetricFogColor[ 2 ];
		}
		else
		{
			cbFog.fogColor[ 0 ] = remaster::g_flVolumetricFogColor[ 0 ];
			cbFog.fogColor[ 1 ] = remaster::g_flVolumetricFogColor[ 1 ];
			cbFog.fogColor[ 2 ] = remaster::g_flVolumetricFogColor[ 2 ];
		}
		cbFog.fogColor[ 3 ]  = 1.0f;
		cbFog.fogParams[ 0 ] = remaster::g_flVolumetricFogDensity;
		cbFog.fogParams[ 1 ] = remaster::g_flVolumetricFogG;
		cbFog.fogParams[ 2 ] = remaster::g_flVolumetricFogMaxDist;
		cbFog.fogParams[ 3 ] = remaster::g_flVolumetricFogIntensity;
		if ( remaster::g_iVolumetricFogCompositeMode == 1 )
			TMath::Clip( cbFog.fogParams[ 3 ], 0.0f, 1.0f );
		static TFLOAT s_flVolumetricFogWindTime = 0.0f;
		s_flVolumetricFogWindTime += a_flDeltaTime;

		cbFog.frameParams[ 0 ]  = TFLOAT( s_uiVolumetricFogFrameIndex & 7 );
		cbFog.frameParams[ 1 ]  = s_flVolumetricFogWindTime;
		cbFog.frameParams[ 2 ]  = 0.0f;
		cbFog.frameParams[ 3 ]  = 0.0f;
		cbFog.cloudParams[ 0 ]  = shadowData.cloudParams[ 0 ];
		cbFog.cloudParams[ 1 ]  = shadowData.cloudParams[ 1 ];
		cbFog.cloudParams[ 2 ]  = shadowData.cloudParams[ 2 ];
		cbFog.cloudParams[ 3 ]  = shadowData.cloudParams[ 3 ];
		cbFog.noiseParams[ 0 ]  = remaster::g_flVolumetricFogNoiseScale * 0.33f;
		cbFog.noiseParams[ 1 ]  = remaster::g_flVolumetricFogNoiseStrength * 0.5f;
		cbFog.noiseParams[ 2 ]  = remaster::g_flVolumetricFogWindDir[ 0 ] * remaster::g_flVolumetricFogWindSpeed;
		cbFog.noiseParams[ 3 ]  = remaster::g_flVolumetricFogWindDir[ 1 ] * remaster::g_flVolumetricFogWindSpeed;
		cbFog.heightParams[ 0 ] = remaster::g_flVolumetricFogHeight;
		cbFog.heightParams[ 1 ] = remaster::g_flVolumetricFogTopHeight;
		cbFog.heightParams[ 2 ] = 0.0f;
		cbFog.heightParams[ 3 ] = 0.0f;
		s_uiVolumetricFogFrameIndex++;

		D3D11_MAPPED_SUBRESOURCE fogMapped;
		remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pVolumetricFogConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &fogMapped );
		TUtil::MemCopy( fogMapped.pData, &cbFog, sizeof( cbFog ) );
		remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pVolumetricFogConstantBuffer, 0 );

		D3D11_VIEWPORT oFogOldVP;
		TUINT          uiFogNumVP = 1;
		remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiFogNumVP, &oFogOldVP );
		D3D11_VIEWPORT oFogQuarterVP = oFogOldVP;
		oFogQuarterVP.Width *= 0.25f;
		oFogQuarterVP.Height *= 0.25f;
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oFogQuarterVP );

		static constexpr TFLOAT aflFogClear[] = { 0.0f, 0.0f, 0.0f, 1.0f };
		remaster::g_pRender->DiscardView( s_pVolumetricFogRTV );
		remaster::g_pRender->ClearRenderTarget( s_pVolumetricFogRTV, aflFogClear );
		remaster::g_pRender->SetRenderTargetView( s_pVolumetricFogRTV, TNULL );
		// Full-res depth; half-res min-depth stabilised grazing ground but its nearest-bias haloed the fog at silhouettes
		remaster::g_pRender->PSSetShaderResource( 0, s_pResolvedDepthSRV );
		remaster::g_pRender->PSSetShaderResource( 1, csmManager.GetShadowSRV() );
		remaster::g_pRender->PSSetShaderResource( 3, s_pVolumetricFogNoiseSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pPointClampSampler );
		remaster::g_pRender->PSSetSamplerState( 1, csmManager.GetShadowSampler() );
		remaster::g_pRender->PSSetSamplerState( 3, s_pVolumetricFogNoiseSampler );
		remaster::g_pRender->PSSetConstantBuffer( 1, s_pVolumetricFogConstantBuffer );
		TUINT uiVolumetricFogComboFlags = 0;
		if ( !remaster::g_bDynamicLightEnabled ||
		     remaster::g_iVolumetricFogCompositeMode == 1
		)
		{
			uiVolumetricFogComboFlags |= remaster::shadercombos::VolumetricFog_NO_DYN_LIGHT;
		}
		else
		{
			remaster::g_pRender->GetLightManager().UploadVolumetricDynamicLightsCBuffer();
		}
		// Cloud shadows in the fog are an independent toggle (the per-step tap is the priciest consumer); bind + compile only when both are on
		const TBOOL bFogClouds = remaster::g_bCloudShadowsEnabled && remaster::g_bCloudShadowsVolumetrics;
		if ( bFogClouds )
		{
			uiVolumetricFogComboFlags |= remaster::shadercombos::VolumetricFog_CLOUD_SHADOWS;
			remaster::g_pRender->PSSetShaderResource( 2, remaster::g_pCloudShadowSRV );
			remaster::g_pRender->PSSetSamplerState( 2, remaster::g_pCloudShadowSampler );
		}
		const TUINT uiVolumetricFogComboIndex = remaster::shadercombos::GetVolumetricFogComboIndex( uiVolumetricFogComboFlags );
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		if ( remaster::g_iVolumetricFogCompositeMode == 1 )
		{
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetVolumetricFogPixelShaderCombo_ps_visibility().GetPixelShader( uiVolumetricFogComboIndex )
			);
		}
		else
		{
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetVolumetricFogPixelShaderCombo_ps_main().GetPixelShader( uiVolumetricFogComboIndex )
			);
		}
		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		remaster::g_pRender->PSSetShaderResource( 1, TNULL );
		remaster::g_pRender->PSSetShaderResource( 2, TNULL );
		remaster::g_pRender->PSSetShaderResource( 6, TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 2, TNULL );
		remaster::g_pRender->PSSetSamplerState( 6, TNULL );

		VolumetricFogCompositeCBuffer cbFogComposite = {};
		cbFogComposite.depthParams[ 0 ]              = 0.0f;
		cbFogComposite.depthParams[ 1 ]              = 0.0f;
		cbFogComposite.depthParams[ 2 ]              = pFogCtx->GetProjectionParams().m_fNearClip;
		cbFogComposite.depthParams[ 3 ]              = pFogCtx->GetProjectionParams().m_fFarClip;
		// Temporal blend weight; 1.0 = pure current (accumulation off). The history-heavy 0.12 blend denoised well but
		// lagged the camera without reprojection -- disabled until reprojection lands; lower to ~0.12 to re-enable
		cbFogComposite.compositeParams[ 0 ] = 1.0f;
		cbFogComposite.compositeParams[ 1 ] = s_bVolumetricFogHistoryValid ? 1.0f : 0.0f;
		cbFogComposite.compositeParams[ 2 ] = 24.0f;
		cbFogComposite.compositeParams[ 3 ] = 0.0f;

		D3D11_MAPPED_SUBRESOURCE fogCompositeMapped;
		remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pVolumetricFogCompositeConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &fogCompositeMapped );
		TUtil::MemCopy( fogCompositeMapped.pData, &cbFogComposite, sizeof( cbFogComposite ) );
		remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pVolumetricFogCompositeConstantBuffer, 0 );

		// Temporal resolve at half res before the full-res composite
		remaster::g_pRender->DiscardView( s_pVolumetricFogTemporalRTV );
		remaster::g_pRender->SetRenderTargetView( s_pVolumetricFogTemporalRTV, TNULL );
		if ( !s_bVolumetricFogHistoryValid )
		{
			remaster::g_pRender->GetD3D11DeviceContext()->CopyResource( s_pVolumetricFogHistoryTexture, s_pVolumetricFogTexture );
			s_bVolumetricFogHistoryValid = TTRUE;
		}
		remaster::g_pRender->PSSetShaderResource( 0, s_pVolumetricFogSRV );
		remaster::g_pRender->PSSetShaderResource( 3, s_pVolumetricFogHistorySRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->PSSetConstantBuffer( 1, s_pVolumetricFogCompositeConstantBuffer );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetVolumetricFogCompositePixelShaderCombo_ps_temporal().GetPixelShader( remaster::shadercombos::VolumetricFogComposite_NoCombos )
		);
		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		remaster::g_pRender->PSSetShaderResource( 3, TNULL );

		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oFogOldVP );
		remaster::g_pRender->SetRenderTargetView(
		    remaster::g_pRender->GetD3D11RenderTargetView(),
		    remaster::g_pRender->GetD3D11DepthStencilView()
		);
		// Re-resolve MSAA color so the fog composite sees the post-AO scene (the HBAO composite drew scene*ao after the first resolve)
		remaster::g_pRender->GetD3D11DeviceContext()->ResolveSubresource(
		    s_pResolvedColorTexture, 0, remaster::g_pRender->GetD3D11RenderTargetTexture(), 0, DXGI_FORMAT_R11G11B10_FLOAT
		);

		// Ping-pong temporal <-> history so next frame's temporal pass reads what we just wrote, no GPU copy
		{
			ID3D11Texture2D*          pTmpTex = s_pVolumetricFogTemporalTexture;
			ID3D11RenderTargetView*   pTmpRTV = s_pVolumetricFogTemporalRTV;
			ID3D11ShaderResourceView* pTmpSRV = s_pVolumetricFogTemporalSRV;
			s_pVolumetricFogTemporalTexture   = s_pVolumetricFogHistoryTexture;
			s_pVolumetricFogTemporalRTV       = s_pVolumetricFogHistoryRTV;
			s_pVolumetricFogTemporalSRV       = s_pVolumetricFogHistorySRV;
			s_pVolumetricFogHistoryTexture    = pTmpTex;
			s_pVolumetricFogHistoryRTV        = pTmpRTV;
			s_pVolumetricFogHistorySRV        = pTmpSRV;
		}
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->PSSetShaderResource( 0, s_pVolumetricFogTemporalSRV );
		remaster::g_pRender->PSSetShaderResource( 1, s_pResolvedColorSRV );
		remaster::g_pRender->PSSetShaderResource( 2, s_pResolvedDepthSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->PSSetSamplerState( 1, s_pLinearClampSampler );
		remaster::g_pRender->PSSetSamplerState( 2, s_pPointClampSampler );
		remaster::g_pRender->PSSetConstantBuffer( 1, s_pVolumetricFogCompositeConstantBuffer );

		if ( remaster::g_iVolumetricFogCompositeMode == 1 )
		{
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetVolumetricFogCompositePixelShaderCombo_ps_darken().GetPixelShader( remaster::shadercombos::VolumetricFogComposite_NoCombos )
			);
		}
		else
		{
			remaster::g_pRender->DrawScreenRectangle(
			    remaster::shadercombos::GetVolumetricFogCompositePixelShaderCombo_ps_additive().GetPixelShader( remaster::shadercombos::VolumetricFogComposite_NoCombos )
			);
		}

		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		remaster::g_pRender->PSSetShaderResource( 1, TNULL );
		remaster::g_pRender->PSSetShaderResource( 2, TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
		remaster::g_pRender->ClearStateCache();
	}

	remaster::g_pRender->SetRenderTargetView(
	    remaster::g_pRender->GetD3D11RenderTargetView(),
	    remaster::g_pRender->GetD3D11DepthStencilView()
	);

	// HDR bloom runs before the glow composite so the bright-pass doesn't re-bloom glow
	if ( remaster::g_bHDRBloomEnabled )
	{
		TPROFILER_NAMED( "HDR Bloom" );
		TracyD3D11Zone( remaster::g_pRender->GetTracyGpuContext(), "HDR Bloom" );

		// Re-resolve so the bright-pass sees the latest post-fog scene
		remaster::g_pRender->GetD3D11DeviceContext()->ResolveSubresource(
		    s_pResolvedColorTexture, 0, remaster::g_pRender->GetD3D11RenderTargetTexture(), 0, DXGI_FORMAT_R11G11B10_FLOAT
		);

		D3D11_VIEWPORT oHDRBloomOldViewport;
		TUINT          uiHDRBloomNumViewports = 1;
		remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiHDRBloomNumViewports, &oHDRBloomOldViewport );

		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );

		for ( TINT k = 0; k < KAWASE_MAX_LEVELS; k++ )
		{
			remaster::g_pRender->DiscardView( s_pKawaseRTVs[ k ] );
			remaster::g_pRender->ClearRenderTarget( s_pKawaseRTVs[ k ], aflSkyMaskClearColor );
		}

		auto fnUploadKawaseCB = [ & ]( TUINT uiSrcW, TUINT uiSrcH, TFLOAT flOffsetOrIntensity, TFLOAT flThresholdOrIntensity ) {
			KawaseCBuffer cbData;
			cbData.texelSizeX = 1.0f / (TFLOAT)uiSrcW;
			cbData.texelSizeY = 1.0f / (TFLOAT)uiSrcH;
			cbData.offset     = flOffsetOrIntensity;
			cbData.PADDING    = flThresholdOrIntensity;

			D3D11_MAPPED_SUBRESOURCE mapped;
			remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pKawaseCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
			remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pKawaseCBuffer, 0 );
		};

		auto fnSetVP = [ & ]( TUINT uiW, TUINT uiH ) {
			D3D11_VIEWPORT vp = oHDRBloomOldViewport;
			vp.Width          = (TFLOAT)uiW;
			vp.Height         = (TFLOAT)uiH;
			remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &vp );
		};

		TINT iNumLevels = remaster::g_iHDRBloomKawaseLevels;
		TMath::Clip( iNumLevels, 1, KAWASE_MAX_LEVELS );

		using namespace remaster::shadercombos;

		// k=0: thresholded downsample from full-res HDR scene into Kawase[0]
		fnUploadKawaseCB( pSwapChainDesc->BufferDesc.Width, pSwapChainDesc->BufferDesc.Height, remaster::g_flHDRBloomKawaseOffset, remaster::g_flHDRBloomThreshold );
		fnSetVP( s_uiKawaseWidths[ 0 ], s_uiKawaseHeights[ 0 ] );
		remaster::g_pRender->SetRenderTargetView( s_pKawaseRTVs[ 0 ], TNULL );
		remaster::g_pRender->PSSetShaderResource( 0, s_pResolvedColorSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->PSSetConstantBuffer( 1, s_pKawaseCBuffer );
		remaster::g_pRender->DrawScreenRectangle( GetHDRBloomThresholdPixelShaderCombo_ps_main().GetPixelShader( HDRBloomThreshold_NoCombos ) );
		remaster::g_pRender->PSSetShaderResource( 0, TNULL );

		for ( TINT k = 1; k < iNumLevels; k++ )
		{
			fnUploadKawaseCB( s_uiKawaseWidths[ k - 1 ], s_uiKawaseHeights[ k - 1 ], remaster::g_flHDRBloomKawaseOffset, 0.0f );
			fnSetVP( s_uiKawaseWidths[ k ], s_uiKawaseHeights[ k ] );
			remaster::g_pRender->SetRenderTargetView( s_pKawaseRTVs[ k ], TNULL );
			remaster::g_pRender->PSSetShaderResource( 0, s_pKawaseSRVs[ k - 1 ] );
			remaster::g_pRender->DrawScreenRectangle( GetDualKawaseDownPixelShaderCombo_ps_main().GetPixelShader( DualKawaseDown_NoCombos ) );
			remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		}

		// Upsample chain N-1 -> 0 (no blending; overwrite each level)
		for ( TINT k = iNumLevels - 1; k > 0; k-- )
		{
			fnUploadKawaseCB( s_uiKawaseWidths[ k ], s_uiKawaseHeights[ k ], remaster::g_flHDRBloomKawaseOffset, 0.0f );
			fnSetVP( s_uiKawaseWidths[ k - 1 ], s_uiKawaseHeights[ k - 1 ] );
			remaster::g_pRender->SetRenderTargetView( s_pKawaseRTVs[ k - 1 ], TNULL );
			remaster::g_pRender->PSSetShaderResource( 0, s_pKawaseSRVs[ k ] );
			remaster::g_pRender->DrawScreenRectangle( GetDualKawaseUpPixelShaderCombo_ps_main().GetPixelShader( DualKawaseUp_NoCombos ) );
			remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		}

		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oHDRBloomOldViewport );
		remaster::g_pRender->SetRenderTargetView(
		    remaster::g_pRender->GetD3D11RenderTargetView(),
		    remaster::g_pRender->GetD3D11DepthStencilView()
		);
		remaster::g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ONE );
		fnUploadKawaseCB( s_uiKawaseWidths[ 0 ], s_uiKawaseHeights[ 0 ], remaster::g_flHDRBloomKawaseOffset, remaster::g_flHDRBloomIntensity );
		remaster::g_pRender->PSSetShaderResource( 0, s_pKawaseSRVs[ 0 ] );
		remaster::g_pRender->DrawScreenRectangle( GetGlowBloomCompositePixelShaderCombo_ps_main().GetPixelShader( GlowBloomComposite_NoCombos ) );
		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );

		remaster::g_pRender->ClearStateCache();
	}

	// Overlay glow objects captured during the main pass, then bloom them
	if ( g_bHasGlowObjectsThisFrame )
	{
		TPROFILER_NAMED( "Glow" );
		TracyD3D11Zone( remaster::g_pRender->GetTracyGpuContext(), "Glow" );

		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ONE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->PSSetShaderResource( 0, s_pResolvedGlowSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetCopyTexturePixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::CopyTexture_NoCombos )
		);
		remaster::g_pRender->PSSetShaderResource( 0, TNULL );

		if ( remaster::g_bGlowBloomEnabled )
		{
			D3D11_VIEWPORT oGlowOldViewport;
			TUINT          uiGlowNumViewports = 1;
			remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiGlowNumViewports, &oGlowOldViewport );

			for ( TINT k = 0; k < KAWASE_MAX_LEVELS; k++ )
			{
				remaster::g_pRender->DiscardView( s_pKawaseRTVs[ k ] );
				remaster::g_pRender->ClearRenderTarget( s_pKawaseRTVs[ k ], aflSkyMaskClearColor );
			}

			auto fnKawasePass = [ & ]( ID3D11RenderTargetView* pDstRTV, ID3D11ShaderResourceView* pSrcSRV, TUINT uiSrcW, TUINT uiSrcH, TUINT uiDstW, TUINT uiDstH, TBOOL bDownsample, TBOOL bComposite ) {
				KawaseCBuffer cbData;
				cbData.texelSizeX = 1.0f / (TFLOAT)uiSrcW;
				cbData.texelSizeY = 1.0f / (TFLOAT)uiSrcH;
				cbData.offset     = remaster::g_flGlowBloomKawaseOffset;
				cbData.PADDING    = bComposite ? remaster::g_flGlowBloomIntensity : 0.0f;

				D3D11_MAPPED_SUBRESOURCE mapped;
				remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pKawaseCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
				TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
				remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pKawaseCBuffer, 0 );

				D3D11_VIEWPORT vp = oGlowOldViewport;
				vp.Width          = (TFLOAT)uiDstW;
				vp.Height         = (TFLOAT)uiDstH;
				remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &vp );

				remaster::g_pRender->SetRenderTargetView( pDstRTV, TNULL );
				remaster::g_pRender->PSSetShaderResource( 0, pSrcSRV );
				remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
				remaster::g_pRender->PSSetConstantBuffer( 1, s_pKawaseCBuffer );

				using namespace remaster::shadercombos;
				if ( bDownsample )
					remaster::g_pRender->DrawScreenRectangle( GetDualKawaseDownPixelShaderCombo_ps_main().GetPixelShader( DualKawaseDown_NoCombos ) );
				else if ( bComposite )
					remaster::g_pRender->DrawScreenRectangle( GetGlowBloomCompositePixelShaderCombo_ps_main().GetPixelShader( GlowBloomComposite_NoCombos ) );
				else
					remaster::g_pRender->DrawScreenRectangle( GetDualKawaseUpPixelShaderCombo_ps_main().GetPixelShader( DualKawaseUp_NoCombos ) );

				remaster::g_pRender->PSSetShaderResource( 0, TNULL );
			};

			TINT iNumLevels = remaster::g_iGlowBloomKawaseLevels;
			TMath::Clip( iNumLevels, 1, KAWASE_MAX_LEVELS );

			for ( TINT k = 0; k < iNumLevels; k++ )
			{
				ID3D11ShaderResourceView* pSrcSRV = ( k == 0 ) ? s_pResolvedGlowSRV : s_pKawaseSRVs[ k - 1 ];
				TUINT                     uiSrcW  = ( k == 0 ) ? pSwapChainDesc->BufferDesc.Width : s_uiKawaseWidths[ k - 1 ];
				TUINT                     uiSrcH  = ( k == 0 ) ? pSwapChainDesc->BufferDesc.Height : s_uiKawaseHeights[ k - 1 ];
				fnKawasePass( s_pKawaseRTVs[ k ], pSrcSRV, uiSrcW, uiSrcH, s_uiKawaseWidths[ k ], s_uiKawaseHeights[ k ], TTRUE, TFALSE );
			}

			remaster::g_pRender->SetBlendEnabled( TFALSE );
			for ( TINT k = iNumLevels - 1; k > 0; k-- )
			{
				fnKawasePass(
				    s_pKawaseRTVs[ k - 1 ],
				    s_pKawaseSRVs[ k ],
				    s_uiKawaseWidths[ k ],
				    s_uiKawaseHeights[ k ],
				    s_uiKawaseWidths[ k - 1 ],
				    s_uiKawaseHeights[ k - 1 ],
				    TFALSE,
				    TFALSE
				);
			}

			remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oGlowOldViewport );
			remaster::g_pRender->SetRenderTargetView(
			    remaster::g_pRender->GetD3D11RenderTargetView(),
			    remaster::g_pRender->GetD3D11DepthStencilView()
			);
			remaster::g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ONE );
			remaster::g_pRender->SetDepthEnabled( TFALSE );
			fnKawasePass(
			    remaster::g_pRender->GetD3D11RenderTargetView(),
			    s_pKawaseSRVs[ 0 ],
			    s_uiKawaseWidths[ 0 ],
			    s_uiKawaseHeights[ 0 ],
			    pSwapChainDesc->BufferDesc.Width,
			    pSwapChainDesc->BufferDesc.Height,
			    TFALSE,
			    TTRUE
			);
			remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
			remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oGlowOldViewport );
		}

		remaster::g_pRender->ClearStateCache();
	}

	// Skymask for the sunshafts; rendered at half resolution, so halve the viewport
	D3D11_VIEWPORT oOldViewport;
	D3D11_VIEWPORT oNewViewport;
	TUINT          uiNumViewports = 1;
	remaster::g_pRender->GetD3D11DeviceContext()->RSGetViewports( &uiNumViewports, &oOldViewport );

	oNewViewport = oOldViewport;
	oNewViewport.Width *= 0.5f;
	oNewViewport.Height *= 0.5f;
	remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oNewViewport );

	auto fnRestoreState = [ &oOldViewport ]() {
		remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &oOldViewport );

		remaster::g_pRender->SetRenderTargetView(
		    remaster::g_pRender->GetD3D11RenderTargetView(),
		    remaster::g_pRender->GetD3D11DepthStencilView()
		);
	};

	{
		TPROFILER_NAMED( "Sky mask" );
		TracyD3D11Zone( remaster::g_pRender->GetTracyGpuContext(), "Sky mask" );

		remaster::g_pRender->DiscardView( s_pSkyMaskRenderTargetView );
		remaster::g_pRender->ClearRenderTarget( s_pSkyMaskRenderTargetView, aflSkyMaskClearColor );
		remaster::g_pRender->SetRenderTargetView( s_pSkyMaskRenderTargetView, TNULL );
		remaster::g_pRender->PSSetShaderResource( 0, s_pResolvedDepthSRV );
		remaster::g_pRender->PSSetShaderResource( 1, s_pResolvedColorSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pSkyMaskSampler );
		remaster::g_pRender->PSSetSamplerState( 1, s_pSkyMaskSampler );
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendEnabled( TFALSE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetSkyMaskPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::SkyMask_NoCombos )
		);

		remaster::g_pRender->PSSetShaderResource( 0, TNULL );
		remaster::g_pRender->PSSetShaderResource( 1, TNULL );
	}

	{
		TPROFILER_NAMED( "Sunshafts" );
		TracyD3D11Zone( remaster::g_pRender->GetTracyGpuContext(), "Sunshafts" );

		remaster::g_pRender->DiscardView( s_pSunshaftsRenderTargetView );
		remaster::g_pRender->ClearRenderTarget( s_pSunshaftsRenderTargetView, aflSkyMaskClearColor );
		remaster::g_pRender->SetRenderTargetView(
		    s_pSunshaftsRenderTargetView,
		    TNULL
		);

		TVector3 lightDir = csmManager.GetLightDirection();
		lightDir.Normalize();

		auto             pContext     = TSTATICCAST( remaster::RenderContextD3D11, m_pViewport->GetRenderContext() );
		const TMatrix44& proj         = pContext->GetProjectionMatrix();
		TMatrix44        matViewWorld = pContext->GetViewWorldMatrix();
		if ( *(void**)0x007822e0 )
		{
			ACamera* pCamera = CALL_THIS( 0x0045b870, void*, ACamera*, *(void**)0x007822e0 ); // ACameraManager::GetCurrentCamera
			if ( pCamera )
				matViewWorld = pCamera->m_Matrix;
		}

		TMatrix44 matWorldView;
		matWorldView.InvertOrthogonal( matViewWorld );

		// Sun direction toward the sun in world space, then into the view space that produced the sky mask/depth
		TVector4 sunDirWorld( -lightDir.x, lightDir.y, -lightDir.z, 0.0f );
		TVector4 sunDirView;
		TMatrix44::RotateVector( sunDirView, matWorldView, sunDirWorld );

		TFLOAT vx = sunDirView.x;
		TFLOAT vy = sunDirView.y;
		TFLOAT vz = sunDirView.z;

		// Sun is behind the camera or effect is disabled - skip the draw entirely
		if ( vz <= 0.0f || !remaster::g_bSunShaftsEnabled )
		{
			fnRestoreState();
			return;
		}

		// D3D viewport convention: keep the signed projection Y scale, then invert NDC Y for texture UV.
		// Must mirror the UVToView/ViewToUV helpers used by screen-space effects, else camera pitch shifts the apparent sun direction
		const TFLOAT invZ = 1.0f / vz;
		const TFLOAT ndcX = ( vx * invZ ) * proj.m_f11 + proj.m_f31;
		const TFLOAT ndcY = ( vy * invZ ) * proj.m_f22 + proj.m_f32;
		TFLOAT       sunU = ndcX * 0.5f + 0.5f;
		TFLOAT       sunV = ( 1.0f - ndcY ) * 0.5f;

		TFLOAT angleFade = ( vz - 0.10f ) / 0.65f;
		TMath::Clip( angleFade, 0.0f, 1.0f );
		angleFade = angleFade * angleFade * ( 3.0f - 2.0f * angleFade );
		angleFade *= angleFade;

		{
			SunShaftsCBuffer cbData;
			cbData.vSunPos[ 0 ]   = sunU;
			cbData.vSunPos[ 1 ]   = sunV;
			cbData.fSunAlpha      = remaster::g_flSunShaftsAlpha * angleFade;
			cbData.fRaysLength    = remaster::g_flSunShaftsRaysLength;
			cbData.vRaysTint[ 0 ] = remaster::g_flSunShaftsTint[ 0 ];
			cbData.vRaysTint[ 1 ] = remaster::g_flSunShaftsTint[ 1 ];
			cbData.vRaysTint[ 2 ] = remaster::g_flSunShaftsTint[ 2 ];
			cbData.PADDING        = 0.0f;

			D3D11_MAPPED_SUBRESOURCE mapped;
			remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pSunShaftsConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
			TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
			remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pSunShaftsConstantBuffer, 0 );

			remaster::g_pRender->PSSetConstantBuffer( 1, s_pSunShaftsConstantBuffer );
		}

		remaster::g_pRender->PSSetShaderResource( 0, s_pSkyMaskShaderResourceView );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetSunShaftsPixelShaderCombo_ps_main().GetPixelShader( remaster::shadercombos::SunShafts_NoCombos )
		);
		remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );

		// Dual Kawase blur on the sunshafts texture
		{
			TUINT uiHalfW = pSwapChainDesc->BufferDesc.Width >> 1;
			TUINT uiHalfH = pSwapChainDesc->BufferDesc.Height >> 1;

			for ( TINT k = 0; k < KAWASE_MAX_LEVELS; k++ )
			{
				remaster::g_pRender->DiscardView( s_pKawaseRTVs[ k ] );
				remaster::g_pRender->ClearRenderTarget( s_pKawaseRTVs[ k ], aflSkyMaskClearColor );
			}

			auto fnKawasePass = [ & ]( ID3D11RenderTargetView* pDstRTV, ID3D11ShaderResourceView* pSrcSRV, TUINT uiSrcW, TUINT uiSrcH, TUINT uiDstW, TUINT uiDstH, TBOOL bDownsample ) {
				KawaseCBuffer cbData;
				cbData.texelSizeX = 1.0f / (TFLOAT)uiSrcW;
				cbData.texelSizeY = 1.0f / (TFLOAT)uiSrcH;
				cbData.offset     = remaster::g_flSunShaftsKawaseOffset;
				cbData.PADDING    = 0.0f;

				D3D11_MAPPED_SUBRESOURCE mapped;
				remaster::g_pRender->GetD3D11DeviceContext()->Map( s_pKawaseCBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped );
				TUtil::MemCopy( mapped.pData, &cbData, sizeof( cbData ) );
				remaster::g_pRender->GetD3D11DeviceContext()->Unmap( s_pKawaseCBuffer, 0 );

				D3D11_VIEWPORT vp = oOldViewport;
				vp.Width          = (TFLOAT)uiDstW;
				vp.Height         = (TFLOAT)uiDstH;
				remaster::g_pRender->GetD3D11DeviceContext()->RSSetViewports( 1, &vp );

				remaster::g_pRender->SetRenderTargetView( pDstRTV, TNULL );
				remaster::g_pRender->PSSetShaderResource( 0, pSrcSRV );
				remaster::g_pRender->PSSetConstantBuffer( 1, s_pKawaseCBuffer );

				using namespace remaster::shadercombos;
				if ( bDownsample )
					remaster::g_pRender->DrawScreenRectangle( GetDualKawaseDownPixelShaderCombo_ps_main().GetPixelShader( DualKawaseDown_NoCombos ) );
				else
					remaster::g_pRender->DrawScreenRectangle( GetDualKawaseUpPixelShaderCombo_ps_main().GetPixelShader( DualKawaseUp_NoCombos ) );

				remaster::g_pRender->PSSetShaderResource( 0, TNULL );
			};

			TINT iNumLevels = remaster::g_iSunShaftsKawaseLevels;
			TMath::Clip( iNumLevels, 1, KAWASE_MAX_LEVELS );

			for ( TINT k = 0; k < iNumLevels; k++ )
			{
				ID3D11ShaderResourceView* pSrcSRV = ( k == 0 ) ? s_pSunshaftsShaderResourceView : s_pKawaseSRVs[ k - 1 ];
				TUINT                     uiSrcW  = ( k == 0 ) ? uiHalfW : s_uiKawaseWidths[ k - 1 ];
				TUINT                     uiSrcH  = ( k == 0 ) ? uiHalfH : s_uiKawaseHeights[ k - 1 ];
				fnKawasePass( s_pKawaseRTVs[ k ], pSrcSRV, uiSrcW, uiSrcH, s_uiKawaseWidths[ k ], s_uiKawaseHeights[ k ], TTRUE );
			}

			for ( TINT k = iNumLevels - 1; k >= 0; k-- )
			{
				ID3D11RenderTargetView* pDstRTV = ( k == 0 ) ? s_pSunshaftsRenderTargetView : s_pKawaseRTVs[ k - 1 ];
				TUINT                   uiDstW  = ( k == 0 ) ? uiHalfW : s_uiKawaseWidths[ k - 1 ];
				TUINT                   uiDstH  = ( k == 0 ) ? uiHalfH : s_uiKawaseHeights[ k - 1 ];
				fnKawasePass( pDstRTV, s_pKawaseSRVs[ k ], s_uiKawaseWidths[ k ], s_uiKawaseHeights[ k ], uiDstW, uiDstH, TFALSE );
			}

			remaster::g_pRender->PSSetShaderResource( 0, TNULL );
			remaster::g_pRender->PSSetConstantBuffer( 1, TNULL );
		}

		fnRestoreState();

		// Additively composite the sunshafts. ps_composite also reads the resolved scene (t1) to dither the shaft sized to
		// the composited magnitude -- sizing to the shaft's own step would let the sum's coarser quantisation band it back
		remaster::g_pRender->SetCullMode( D3D11_CULL_NONE );
		remaster::g_pRender->SetBlendMode( TTRUE, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ONE );
		remaster::g_pRender->SetDepthEnabled( TFALSE );
		remaster::g_pRender->PSSetShaderResource( 0, s_pSunshaftsShaderResourceView );
		remaster::g_pRender->PSSetShaderResource( 1, s_pResolvedColorSRV );
		remaster::g_pRender->PSSetSamplerState( 0, s_pLinearClampSampler );
		remaster::g_pRender->DrawScreenRectangle(
		    remaster::shadercombos::GetCopyTexturePixelShaderCombo_ps_composite().GetPixelShader( remaster::shadercombos::CopyTexture_NoCombos )
		);
		remaster::g_pRender->PSSetShaderResource( 1, TNULL );

		remaster::g_pRender->ClearStateCache();
	}
}

MEMBER_HOOK( 0x00608540, AGlowViewport, AGlowViewport_AddGlowObject, AGlowViewport::GlowObject* )
{
	AGlowViewport::GlowObject* pGlowObject = CallOriginal();

	pGlowObject->m_bIsNightLight = TTRUE;
	return pGlowObject;
}

HOOK( 0x006119d0, AModelLoader_CreateMaterial, TMaterial*, TINT a_iOffset, const TCHAR* a_szMaterialName )
{
	TMaterial* pMaterial = CallOriginal( a_iOffset, a_szMaterialName );

	// Attach remaster material params authored in Data/MaterialParams.xml
	remaster::AttachMaterialParams( pMaterial, a_szMaterialName );

	return pMaterial;
}

// Tangent stream layout: float4 per vertex (xyz = tangent, w = handedness sign)
static constexpr TUINT16 TANGENT_STREAM_SIZE = sizeof( Toshi::TVector4 );

// Append a parallel tangent stream to the world/skin vertex factories after the original creates them. The engine's
// pool/block code is stream-driven (auto allocate/lock/manage); it's filled at mesh load and bound as vertex slot 1
MEMBER_HOOK( 0x006150e0, ARenderer, ARenderer_CreateTRenderResources, TBOOL )
{
	const TBOOL bResult = CallOriginal();

	auto pRender = Toshi::TRenderInterface::GetSingleton();
	if ( auto pWorldVF = pRender->GetSystemResource<Toshi::TVertexFactoryResourceInterface>( SYSRESOURCE_VFWORLD ) )
		pWorldVF->AddVertexStream( TANGENT_STREAM_SIZE );
	if ( auto pSkinVF = pRender->GetSystemResource<Toshi::TVertexFactoryResourceInterface>( SYSRESOURCE_VFSKIN ) )
		pSkinVF->AddVertexStream( TANGENT_STREAM_SIZE );

	return bResult;
}

void remaster::SetupRenderHooks()
{
	InstallHook<ARenderer_CreateTRenderResources>();
	InstallHook<TRenderD3DInterface_Create>();
	InstallHook<TRenderD3DInterface_CreateObject>();
	InstallHook<TRenderD3DInterface_BeginEndScene>();
	InstallHook<TRenderD3DInterface_FlushShaders>();
	InstallHook<TD3DAdapter_Mode_Device_SupportsVSConstants>();
	InstallHook<ARenderer_RenderMainScene>();
	InstallHook<RenderCellMeshWin>();
	InstallHook<RenderCellMeshDefault>();
	InstallHook<AModelInstance_RenderInstanceCallback>();
	InstallHook<AModelLoader_LoadWorldMeshTRB_Tangents>();
	InstallHook<AGlowViewport_AddGlowObject>();
	InstallHook<AModelLoader_CreateMaterial>();

	// Load per-material params (SSR reflectivity, etc.) before any material is created
	remaster::LoadMaterialParamsDB( "Data\\MaterialParams.xml" );

	// Load per-level sky-cube probe anchors (parallax anchoring for reflections)
	remaster::CubemapAnchors_Load( "Data\\CubemapAnchors.xml" );

	SetupRenderHooks_GrassShader();
	SetupRenderHooks_SkinShader();
	SetupRenderHooks_WorldShader();
	SetupRenderHooks_TextureResource();
	SetupRenderHooks_Viewport();
	SetupRenderHooks_StaticInstanceShader();
	SetupRenderHooks_SysShader();
	SetupRenderHooks_UIRenderer();
	SetupRenderHooks_FontRenderer();
	SetupRenderHooks_OrderTable();
	SetupRenderHooks_VertexBlock();
	SetupRenderHooks_IndexBlock();
}
