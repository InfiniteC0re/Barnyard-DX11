#pragma once
#include "Ref/AWorldShader/AWorldShader_DX8.h"

namespace remaster
{

void SetupRenderHooks_WorldShader();

class WorldShaderDX11
    : public AWorldShader
{
public:
	WorldShaderDX11();
	~WorldShaderDX11();

	//-----------------------------------------------------------------------------
	// Toshi::TShader
	//-----------------------------------------------------------------------------
	virtual void  Flush() override;
	virtual void  StartFlush() override;
	virtual void  EndFlush() override;
	virtual TBOOL Create() override;
	virtual TBOOL Validate() override;
	virtual void  Invalidate() override;
	virtual TBOOL TryInvalidate() override;
	virtual TBOOL TryValidate() override;
	virtual void  Render( Toshi::TRenderPacket* a_pRenderPacket ) override;

	//-----------------------------------------------------------------------------
	// AWorldShader
	//-----------------------------------------------------------------------------
	virtual void EnableRenderEnvMap( TBOOL a_bEnable ) override;

	//-----------------------------------------------------------------------------
	// Own methods
	//-----------------------------------------------------------------------------

	// Returns TTRUE if alpha blend material is enabled
	virtual TBOOL IsAlphaBlendMaterial() override;

	// Enabled alpha blend material
	virtual void SetAlphaBlendMaterial( TBOOL a_bIsAlphaBlendMaterial ) override;

	// Creates AWorldMaterial and returns it
	virtual AWorldMaterial* CreateMaterial( const TCHAR* a_szName ) override;

	// Creates AWorldMesh and returns it
	virtual AWorldMesh* CreateMesh( const TCHAR* a_szName ) override;

	// Returns TTRUE if shaders are supported
	virtual TBOOL IsHighEndMode();

	// Enables high end mode (compiled shader) if they are supported by hardware
	virtual void SetHighEndMode( TBOOL a_bEnable );

	// Returns TTRUE if shaders are supported by hardware
	virtual TBOOL IsCapableShaders();

	// Returns TTRUE if rendering of env map is enabled
	virtual TBOOL IsRenderEnvMapEnabled();

	// Probably used in debug mode but is stripped out in release
	virtual void* CreateUnknown( void*, void*, void*, void* );
};

}; // namespace remaster
