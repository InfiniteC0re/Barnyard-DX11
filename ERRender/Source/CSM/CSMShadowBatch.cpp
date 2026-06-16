#include "pch.h"
#include "CSM/CSMShadowBatch.h"
#include "Ref/AWorld.h"
#include "RenderDX11.h"
#include "RenderDX11Utils.h"
#include "Shader/WorldShader.h"

#include <Render/TMaterial.h>

#include <vector>
#include <utility>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

namespace remaster
{

//-----------------------------------------------------------------------------
// Accumulates merged geometry for a single material while a section is built.
//-----------------------------------------------------------------------------
struct MaterialAccumulator
{
	TMaterial*               pMaterial = TNULL;
	std::vector<WorldVertex> vecVertices;
	std::vector<TUINT32>     vecIndices;
	std::vector<TMesh*>      vecMeshes; // source meshes contributing to this group
};

// Converts one triangle-strip submesh (16-bit indices) into appended 32-bit
// triangle-list indices, offsetting by the current vertex base. Degenerate
// triangles (repeated indices, used to stitch strips together) are dropped.
static void AppendStripAsList(
    std::vector<TUINT32>& a_rOutIndices,
    const TUINT16*        a_pStripIndices,
    TUINT                 a_uiNumStripIndices,
    TUINT32               a_uiVertexBase
)
{
	if ( a_uiNumStripIndices < 3 ) return;

	for ( TUINT i = 0; i + 2 < a_uiNumStripIndices; i++ )
	{
		TUINT32 i0 = a_uiVertexBase + a_pStripIndices[ i ];
		TUINT32 i1 = a_uiVertexBase + a_pStripIndices[ i + 1 ];
		TUINT32 i2 = a_uiVertexBase + a_pStripIndices[ i + 2 ];

		// Skip degenerate stitching triangles
		if ( i0 == i1 || i1 == i2 || i0 == i2 )
			continue;

		// Triangle strips alternate winding every other triangle. The shadow
		// pass renders with CULL_NONE so winding is cosmetic, but keep it
		// correct anyway in case culling is enabled later.
		if ( ( i & 1 ) == 0 )
		{
			a_rOutIndices.push_back( i0 );
			a_rOutIndices.push_back( i1 );
			a_rOutIndices.push_back( i2 );
		}
		else
		{
			a_rOutIndices.push_back( i0 );
			a_rOutIndices.push_back( i2 );
			a_rOutIndices.push_back( i1 );
		}
	}
}

CSMShadowBatch::~CSMShadowBatch()
{
	for ( auto it = m_Sections.Begin(); it != m_Sections.End(); it++ )
	{
		SectionBatch& rBatch = it.GetValue()->GetSecond();
		for ( TINT i = 0; i < rBatch.vecGroups.Size(); i++ )
			ReleaseGroup( rBatch.vecGroups[ i ] );
	}

	m_Sections.Clear();
	m_MeshToSection.Clear();
}

void CSMShadowBatch::ReleaseGroup( MergedGroup& a_rGroup )
{
	if ( a_rGroup.pVertexBuffer )
	{
		a_rGroup.pVertexBuffer->Release();
		a_rGroup.pVertexBuffer = TNULL;
	}

	if ( a_rGroup.pIndexBuffer )
	{
		a_rGroup.pIndexBuffer->Release();
		a_rGroup.pIndexBuffer = TNULL;
	}

	a_rGroup.uiIndexCount = 0;
}

void CSMShadowBatch::BuildSection( TModel* a_pModel, TModelLOD* a_pLOD )
{
	if ( !a_pModel || !a_pLOD ) return;

	// Rebuild safety: if this LOD was somehow already batched, drop it first.
	if ( m_Sections.IsValid( m_Sections.Find( a_pLOD ) ) )
		DestroySection( a_pLOD );

	auto pDatabase = a_pModel->CastSymbol<WorldDatabase>( "Database" );
	if ( !pDatabase ) return;

	// Group all CellMeshes by their material pointer, accumulating merged
	// world-space vertices and 32-bit list indices.
	std::vector<MaterialAccumulator> vecAccumulators;

	auto FindAccumulator = [ & ]( TMaterial* a_pMaterial ) -> MaterialAccumulator& {
		for ( auto& rAcc : vecAccumulators )
		{
			if ( rAcc.pMaterial == a_pMaterial )
				return rAcc;
		}
		vecAccumulators.emplace_back();
		vecAccumulators.back().pMaterial = a_pMaterial;
		return vecAccumulators.back();
	};

	for ( TUINT w = 0; w < pDatabase->m_uiNumWorlds; w++ )
	{
		World* pWorld = pDatabase->m_ppWorlds[ w ];
		if ( !pWorld ) continue;

		for ( TINT c = 0; c < pWorld->m_iNumCells; c++ )
		{
			Cell* pCell = pWorld->m_ppCells[ c ];
			if ( !pCell ) continue;

			for ( TUINT m = 0; m < pCell->uiNumMeshes; m++ )
			{
				CellMeshSphere* pMeshSphere = pCell->ppCellMeshSpheres[ m ];
				if ( !pMeshSphere || !pMeshSphere->m_pCellMesh ) continue;

				CellMesh* pCellMesh = pMeshSphere->m_pCellMesh;
				if ( !pCellMesh->pMesh || pCellMesh->uiNumVertices1 == 0 || pCellMesh->uiNumIndices == 0 )
					continue;

				TMaterial*           pMaterial = TSTATICCAST( remaster::WorldShaderDX11, remaster::WorldShaderDX11::GetSingleton() )->GetShadowMaterial();
				MaterialAccumulator& rAcc      = FindAccumulator( pMaterial );

				const TUINT32 uiVertexBase = (TUINT32)rAcc.vecVertices.size();
				rAcc.vecVertices.insert(
				    rAcc.vecVertices.end(),
				    pCellMesh->pVertices,
				    pCellMesh->pVertices + pCellMesh->uiNumVertices1
				);

				AppendStripAsList( rAcc.vecIndices, pCellMesh->pIndices, pCellMesh->uiNumIndices, uiVertexBase );

				rAcc.vecMeshes.push_back( pCellMesh->pMesh );
			}
		}
	}

	if ( vecAccumulators.empty() ) return;

	// Create GPU buffers per material group and store them directly in the map
	// entry. The entry is created empty and groups are pushed in place, because
	// T2DynamicVector has no move constructor -- moving a populated batch would
	// shallow-copy its buffer pointer and double-free on teardown.
	ID3D11Device* pDevice  = g_pRender->GetD3D11Device();
	SectionBatch* pStored  = TNULL;

	for ( auto& rAcc : vecAccumulators )
	{
		if ( rAcc.vecVertices.empty() || rAcc.vecIndices.empty() )
			continue;

		MergedGroup oGroup;
		oGroup.pMaterial     = rAcc.pMaterial;
		oGroup.pVertexBuffer = TNULL;
		oGroup.pIndexBuffer  = TNULL;
		oGroup.uiIndexCount  = (TUINT)rAcc.vecIndices.size();

		// Immutable vertex buffer (static world-space geometry).
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth      = (UINT)( rAcc.vecVertices.size() * sizeof( WorldVertex ) );
			desc.Usage          = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
			desc.CPUAccessFlags = 0;

			D3D11_SUBRESOURCE_DATA init = {};
			init.pSysMem = rAcc.vecVertices.data();

			if ( FAILED( pDevice->CreateBuffer( &desc, &init, &oGroup.pVertexBuffer ) ) )
				continue;
		}

		// Immutable 32-bit index buffer.
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth      = (UINT)( rAcc.vecIndices.size() * sizeof( TUINT32 ) );
			desc.Usage          = D3D11_USAGE_IMMUTABLE;
			desc.BindFlags      = D3D11_BIND_INDEX_BUFFER;
			desc.CPUAccessFlags = 0;

			D3D11_SUBRESOURCE_DATA init = {};
			init.pSysMem = rAcc.vecIndices.data();

			if ( FAILED( pDevice->CreateBuffer( &desc, &init, &oGroup.pIndexBuffer ) ) )
			{
				oGroup.pVertexBuffer->Release();
				continue;
			}
		}

		if ( !pStored )
		{
			pStored = m_Sections.Emplace( a_pLOD );
			pStored->bModelViewValid = TFALSE;
		}

		pStored->vecGroups.PushBack( oGroup );

		// Only suppress per-mesh shadow draws for meshes whose group was built.
		for ( TMesh* pMesh : rAcc.vecMeshes )
			m_MeshToSection.Insert( pMesh, a_pLOD );
	}
}

void CSMShadowBatch::DestroySection( TModelLOD* a_pLOD )
{
	auto it = m_Sections.Find( a_pLOD );
	if ( !m_Sections.IsValid( it ) ) return;

	SectionBatch& rBatch = it.GetValue()->GetSecond();
	for ( TINT i = 0; i < rBatch.vecGroups.Size(); i++ )
		ReleaseGroup( rBatch.vecGroups[ i ] );

	// Drop reverse-lookup entries that point at this section.
	for ( auto mit = m_MeshToSection.Begin(); mit != m_MeshToSection.End(); )
	{
		if ( mit.GetValue()->GetSecond() == a_pLOD )
		{
			auto toErase = mit;
			mit++;
			m_MeshToSection.Remove( toErase );
		}
		else
		{
			mit++;
		}
	}

	m_Sections.Remove( it );
}

void CSMShadowBatch::NotifyMeshDestroyed( TMesh* a_pMesh )
{
	auto it = m_MeshToSection.Find( a_pMesh );
	if ( !m_MeshToSection.IsValid( it ) ) return;

	// Free the whole owning section; sibling meshes' later notifications become
	// no-ops once their reverse-lookup entries are erased by DestroySection.
	DestroySection( it.GetValue()->GetSecond() );
}

TBOOL CSMShadowBatch::CaptureSectionModelView( TMesh* a_pMesh, const TMatrix44& a_rModelView )
{
	auto it = m_MeshToSection.Find( a_pMesh );
	if ( !m_MeshToSection.IsValid( it ) ) return TFALSE;

	auto sectionIt = m_Sections.Find( it.GetValue()->GetSecond() );
	if ( m_Sections.IsValid( sectionIt ) )
	{
		SectionBatch& rSection   = sectionIt.GetValue()->GetSecond();
		rSection.matModelView    = a_rModelView;
		rSection.bModelViewValid = TTRUE;
	}

	return TTRUE;
}

void CSMShadowBatch::BeginShadowPass()
{
	for ( auto it = m_Sections.Begin(); it != m_Sections.End(); it++ )
		it.GetValue()->GetSecond().bModelViewValid = TFALSE;
}

} // namespace remaster
