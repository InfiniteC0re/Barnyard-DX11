#pragma once
#include "Ref/ASysShader/ASysShader_DX8.h"
#include "RenderDX11.h"

#include <d3d11.h>

namespace remaster
{

void SetupRenderHooks_SysShader();

class SysShaderDX11
    : public ASysShader
{
public:
	TDECLARE_CLASS( SysShaderDX11, ASysShader );

public:
	static constexpr TUINT NUM_ORDER_TABLES = 4;

public:
	SysShaderDX11();
	~SysShaderDX11();

	//-----------------------------------------------------------------------------
	// Toshi::TShader
	//-----------------------------------------------------------------------------
	virtual void          Flush() OVERRIDE;
	virtual void          StartFlush() OVERRIDE;
	virtual void          EndFlush() OVERRIDE;
	virtual TBOOL         Create() OVERRIDE;
	virtual TBOOL         Validate() OVERRIDE;
	virtual void          Invalidate() OVERRIDE;
	virtual TBOOL         TryInvalidate() OVERRIDE;
	virtual TBOOL         TryValidate() OVERRIDE;
	virtual void          Render( Toshi::TRenderPacket* pPacket ) OVERRIDE;

	//-----------------------------------------------------------------------------
	// ASysShader
	//-----------------------------------------------------------------------------
	virtual ASysMaterial* CreateMaterial( const TCHAR* a_szName ) OVERRIDE;
	virtual ASysMesh*     CreateMesh( const TCHAR* a_szName ) OVERRIDE;

	Toshi::TOrderTable* GetOrderTable( TUINT a_uiIndex )
	{
		TASSERT( a_uiIndex < NUM_ORDER_TABLES );
		return &m_aOrderTables[ a_uiIndex ];
	}

public:
	inline static TBOOL ms_bRenderEnabled = TTRUE;

private:
	Toshi::TOrderTable m_aOrderTables[ NUM_ORDER_TABLES ];

	ID3DBlob* m_pVSShaderBlob;
	ID3DBlob* m_pPSShaderBlob_Textured;
	ID3DBlob* m_pPSShaderBlob_Solid;

	RenderDX11::ShaderPipelineState m_oShaderPipeline_Textured;
	RenderDX11::ShaderPipelineState m_oShaderPipeline_Solid;
};

TSINGLETON_DECLARE_INHERITED_ALIAS( ASysShader, SysShaderDX11, SysShader );

}; // namespace remaster
