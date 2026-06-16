#pragma once

#include <Render/TModel.h>
#include <Toshi/T2Map.h>
#include <ToshiTools/T2DynamicVector.h>

#include <d3d11.h>

namespace remaster
{

class CSMShadowBatch
{
public:
	// One merged buffer per distinct material within a section.
	struct MergedGroup
	{
		Toshi::TMaterial* pMaterial;   // for alpha-test texture bind
		ID3D11Buffer*     pVertexBuffer;
		ID3D11Buffer*     pIndexBuffer;
		TUINT             uiIndexCount; // 32-bit triangle list
	};

	struct SectionBatch
	{
		Toshi::T2DynamicVector<MergedGroup> vecGroups;

		// The model-view matrix the per-mesh path would use for this section,
		// captured from a suppressed per-mesh draw during the shadow flush. The
		// merged vertices are in section-local (Y-up art) space, so the batch MVP
		// must include this transform, not just lightProj * lightView. Constant
		// across cascades within a frame; re-captured each shadow pass.
		Toshi::TMatrix44 matModelView;
		TBOOL            bModelViewValid;
	};

public:
	static CSMShadowBatch& GetSingleton()
	{
		static CSMShadowBatch s_oInstance;
		return s_oInstance;
	}

	// Build merged buffers for a freshly loaded world section. Walks the model's
	// WorldDatabase, groups CellMeshes by material, concatenates their
	// world-space vertices and converts the 16-bit triangle-strip indices into a
	// single 32-bit triangle list per material. Keyed by a_pLOD for teardown.
	// No-op if the model has no world database.
	void BuildSection( Toshi::TModel* a_pModel, Toshi::TModelLOD* a_pLOD );

	// Release the merged buffers owned by a section. Called when the section's
	// meshes are torn down. Safe to call for an unknown LOD (no-op).
	void DestroySection( Toshi::TModelLOD* a_pLOD );

	// Called from AWorldMesh::OnDestroy. Finds the section that owns this mesh
	// (if any) and frees it. The first contained-mesh destroy frees the whole
	// section batch; subsequent sibling destroys are no-ops.
	void NotifyMeshDestroyed( Toshi::TMesh* a_pMesh );

	// Iterate all currently-loaded section batches (for the shadow pass).
	Toshi::T2Map<Toshi::TModelLOD*, SectionBatch>& GetSections() { return m_Sections; }

	TBOOL HasSections() const { return !m_Sections.IsEmpty(); }

	// Diagnostics: number of loaded section batches and total merged draw groups.
	TINT GetSectionCount() const { return (TINT)m_Sections.Size(); }
	TINT GetGroupCount()
	{
		TINT iTotal = 0;
		for ( auto it = m_Sections.Begin(); it != m_Sections.End(); it++ )
			iTotal += it.GetValue()->GetSecond().vecGroups.Size();
		return iTotal;
	}
	// Number of sections whose per-mesh model-view was captured this shadow pass.
	TINT GetCapturedCount()
	{
		TINT iTotal = 0;
		for ( auto it = m_Sections.Begin(); it != m_Sections.End(); it++ )
			if ( it.GetValue()->GetSecond().bModelViewValid )
				iTotal++;
		return iTotal;
	}

	// If this mesh is covered by a merged section batch, records the section's
	// model-view matrix (so the batch can reproduce the per-mesh transform) and
	// returns TTRUE so the caller suppresses the per-mesh shadow draw. Returns
	// TFALSE for non-batched meshes, which keep the per-mesh path.
	TBOOL CaptureSectionModelView( Toshi::TMesh* a_pMesh, const Toshi::TMatrix44& a_rModelView );

	// Invalidate captured model-view matrices at the start of a shadow pass so
	// sections no longer visible to the light are not drawn with stale
	// transforms.
	void BeginShadowPass();

private:
	CSMShadowBatch() = default;
	~CSMShadowBatch();

	void ReleaseGroup( MergedGroup& a_rGroup );

private:
	Toshi::T2Map<Toshi::TModelLOD*, SectionBatch> m_Sections;

	// Reverse lookup so a single destroyed mesh can find its owning section
	// without scanning every group.
	Toshi::T2Map<Toshi::TMesh*, Toshi::TModelLOD*> m_MeshToSection;
};

} // namespace remaster
