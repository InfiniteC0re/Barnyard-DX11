#include "pch.h"
#include "RenderDX11Utils.h"
#include "RenderDX11.h"
#include "ShaderCache.h"

#include <d3dcompiler.h>
#include <windows.h>

#include <Toshi/TArray.h>
#include <Toshi/TString8.h>

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

class ShaderIncludeHandler : public ID3DInclude
{
public:
	ShaderIncludeHandler( const TCHAR* a_pchSourceFile )
	{
		m_DirectoryStack.Push( remaster::dx11::GetShaderFileDirectory( a_pchSourceFile ) );
	}

	HRESULT STDMETHODCALLTYPE Open(
	    D3D_INCLUDE_TYPE a_eIncludeType,
	    LPCSTR           a_pchFileName,
	    LPCVOID          a_pParentData,
	    LPCVOID*         a_ppData,
	    UINT*            a_puiBytes
	) override
	{
		remaster::dx11::LockShaderFileCache();

		TASSERT( a_ppData != TNULL );
		TASSERT( a_puiBytes != TNULL );

		const TString8 strCurrentDir = m_DirectoryStack.Size() == 0 ? TString8() : m_DirectoryStack[ m_DirectoryStack.Size() - 1 ];
		TString8       strFilepath   = remaster::dx11::JoinShaderFilePath( strCurrentDir, a_pchFileName );

		const TCHAR* pchData = TNULL;
		TUINT        uiSize  = 0;
		TBOOL        bFound  = remaster::dx11::GetCachedShaderFile( strFilepath, pchData, uiSize );

		if ( !bFound && a_eIncludeType == D3D_INCLUDE_SYSTEM && m_DirectoryStack.Size() != 0 )
		{
			strFilepath = remaster::dx11::JoinShaderFilePath( m_DirectoryStack[ 0 ], a_pchFileName );
			bFound      = remaster::dx11::GetCachedShaderFile( strFilepath, pchData, uiSize );
		}

		if ( !bFound )
		{
			remaster::dx11::UnlockShaderFileCache();
			return E_FAIL;
		}

		*a_ppData   = pchData;
		*a_puiBytes = uiSize;

		m_DirectoryStack.Push( remaster::dx11::GetShaderFileDirectory( strFilepath ) );
		remaster::dx11::UnlockShaderFileCache();
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Close( LPCVOID a_pData ) override
	{
		// Data is owned by the shader file cache
		remaster::dx11::LockShaderFileCache();

		if ( m_DirectoryStack.Size() > 1 )
			m_DirectoryStack.Pop();

		remaster::dx11::UnlockShaderFileCache();
		return S_OK;
	}

private:
	TArray<TString8> m_DirectoryStack;
};

static ID3DBlob* CompileShaderInternal(
    const TCHAR*             a_pchSrcData,
    const TCHAR*             a_pchSourceName,
    LPCSTR                   a_pEntrypoint,
    LPCSTR                   a_pTarget,
    const D3D_SHADER_MACRO*  a_pDefines,
    ID3DInclude*             a_pIncludeHandler
)
{
	TSIZE srcLength = T2String8::Length( a_pchSrcData );

	ID3DBlob* pShaderBlob = TNULL;
	ID3DBlob* pErrorBlob  = TNULL;

	HRESULT hRes = D3DCompile(
	    a_pchSrcData,
	    srcLength,
	    a_pchSourceName,
	    a_pDefines,
	    a_pIncludeHandler,
	    a_pEntrypoint,
	    a_pTarget,
	    D3DCOMPILE_PACK_MATRIX_ROW_MAJOR,
	    0,
	    &pShaderBlob,
	    &pErrorBlob
	);

	if ( !SUCCEEDED( hRes ) )
	{
		TERROR( "Shader compilation failed\n" );

		if ( pErrorBlob != TNULL )
		{
			TERROR( (const TCHAR*)pErrorBlob->GetBufferPointer() );
			OutputDebugStringA( (const TCHAR*)pErrorBlob->GetBufferPointer() );
			pErrorBlob->Release();
		}

		TASSERT( TFALSE );
	}

	return pShaderBlob;
}

static TBOOL ReadShaderSourceFile( const TCHAR* a_pchFilepath, TString8& a_rSource )
{
	remaster::dx11::LockShaderFileCache();

	const TCHAR* pchData = TNULL;
	TUINT        uiSize  = 0;
	const TBOOL  bFound  = remaster::dx11::GetCachedShaderFile( a_pchFilepath, pchData, uiSize );

	if ( bFound )
		a_rSource.Copy( pchData, TINT( uiSize ) );

	remaster::dx11::UnlockShaderFileCache();
	return bFound;
}

volatile LONG remaster::dx11::g_iCompiledShaderPermutations = 0;

struct ShaderCompileWorkerContext
{
	const TCHAR*                   pchFilepath;
	const TCHAR*                   pchSource;
	LPCSTR                         pEntrypoint;
	LPCSTR                         pTarget;
	const remaster::dx11::ShaderComboDefinition* pCombos;
	TUINT                          uiNumCombos;
	ID3DBlob**                     ppBlobs;
	const TUINT*                   pWorkIndices; // permutation indices to compile
	TUINT                          uiNumWorkItems;
	volatile LONG                  iNextIndex;
	volatile LONG                  iFailed;
};

static void CompileShaderComboPermutation( ShaderCompileWorkerContext* a_pContext, TUINT a_uiIndex )
{
	D3D_SHADER_MACRO* pDefines     = new D3D_SHADER_MACRO[ a_pContext->uiNumCombos + 1 ];
	TCHAR*            pDefinitions = new TCHAR[ TMath::Max<TUINT>( a_pContext->uiNumCombos, 1 ) * 16 ];

	for ( TUINT i = 0; i < a_pContext->uiNumCombos; i++ )
	{
		const remaster::dx11::ShaderComboDefinition& rCombo = a_pContext->pCombos[ i ];
		const TINT iValue = rCombo.iMinValue + TINT( ( a_uiIndex / rCombo.uiStride ) % TUINT( rCombo.iMaxValue - rCombo.iMinValue + 1 ) );

		TCHAR* pDefinition = pDefinitions + i * 16;
		_snprintf_s( pDefinition, 16, _TRUNCATE, "%d", iValue );

		pDefines[ i ].Name       = rCombo.pchName;
		pDefines[ i ].Definition = pDefinition;
	}

	pDefines[ a_pContext->uiNumCombos ].Name       = TNULL;
	pDefines[ a_pContext->uiNumCombos ].Definition = TNULL;

	ShaderIncludeHandler includeHandler( a_pContext->pchFilepath );
	a_pContext->ppBlobs[ a_uiIndex ] = CompileShaderInternal(
	    a_pContext->pchSource,
	    a_pContext->pchFilepath,
	    a_pContext->pEntrypoint,
	    a_pContext->pTarget,
	    pDefines,
	    &includeHandler
	);

	if ( !a_pContext->ppBlobs[ a_uiIndex ] )
		InterlockedExchange( &a_pContext->iFailed, 1 );

	InterlockedIncrement( &remaster::dx11::g_iCompiledShaderPermutations );

	delete[] pDefinitions;
	delete[] pDefines;
}

static DWORD WINAPI ShaderCompileWorkerProc( LPVOID a_pParameter )
{
	ShaderCompileWorkerContext* pContext = TREINTERPRETCAST( ShaderCompileWorkerContext*, a_pParameter );

	for ( ;; )
	{
		const LONG iSlot = InterlockedIncrement( &pContext->iNextIndex ) - 1;
		if ( iSlot >= LONG( pContext->uiNumWorkItems ) )
			break;

		CompileShaderComboPermutation( pContext, pContext->pWorkIndices[ iSlot ] );
	}

	return 0;
}

static TUINT GetShaderCompileThreadCount( TUINT a_uiNumPermutations )
{
	if ( a_uiNumPermutations <= 1 )
		return 1;

	// D3DCompile allocates a lot of temp memory per optimized shader; 8 at once relies on the LAA (4GB) exe for address space
	return TMath::Min<TUINT>( a_uiNumPermutations, 8 );
}

remaster::dx11::ShaderCombo::ShaderCombo()
{
}

remaster::dx11::ShaderCombo::~ShaderCombo()
{
	Clear();
}

void remaster::dx11::ShaderCombo::Clear()
{
	ReleasePixelShaders();
	ReleaseVertexShaders();
	ReleaseBlobs();

	m_vecPixelShaders.Clear();
	m_vecVertexShaders.Clear();
	m_vecBlobs.Clear();
}

void remaster::dx11::ShaderCombo::ReleaseBlobs()
{
	for ( TINT i = 0; i < m_vecBlobs.Size(); i++ )
	{
		ID3DBlob* pBlob = m_vecBlobs[ i ];
		if ( pBlob )
		{
			pBlob->Release();
			m_vecBlobs[ i ] = TNULL;
		}
	}
}

void remaster::dx11::ShaderCombo::ReleaseVertexShaders()
{
	for ( TINT i = 0; i < m_vecVertexShaders.Size(); i++ )
	{
		ID3D11VertexShader* pVertexShader = m_vecVertexShaders[ i ];
		if ( pVertexShader )
		{
			pVertexShader->Release();
			m_vecVertexShaders[ i ] = TNULL;
		}
	}
}

void remaster::dx11::ShaderCombo::ReleasePixelShaders()
{
	for ( TINT i = 0; i < m_vecPixelShaders.Size(); i++ )
	{
		ID3D11PixelShader* pPixelShader = m_vecPixelShaders[ i ];
		if ( pPixelShader )
		{
			pPixelShader->Release();
			m_vecPixelShaders[ i ] = TNULL;
		}
	}
}

TBOOL remaster::dx11::ShaderCombo::CompileFromFile( const TCHAR* a_pchFilepath, LPCSTR a_pEntrypoint, LPCSTR a_pTarget, const ShaderComboDefinition* a_pCombos, TUINT a_uiNumCombos, TUINT a_uiNumPermutations )
{
	ReleaseBlobs();
	m_vecBlobs.Clear();

	TString8 strSource;
	if ( !ReadShaderSourceFile( a_pchFilepath, strSource ) )
		return TFALSE;

	if ( a_uiNumPermutations == 0 )
		return TFALSE;

	m_vecBlobs.SetSize( a_uiNumPermutations, TNULL );

	const TUINT uiStage = ( a_pTarget && a_pTarget[ 0 ] == 'v' ) ? SHADERCOMBOSTAGE_VS : SHADERCOMBOSTAGE_PS;

	TUINT* pCanonical  = new TUINT[ a_uiNumPermutations ];
	TUINT* pUnique     = new TUINT[ a_uiNumPermutations ];
	TUINT  uiNumUnique = 0;

	for ( TUINT i = 0; i < a_uiNumPermutations; i++ )
	{
		TUINT uiCanonical = i;
		for ( TUINT c = 0; c < a_uiNumCombos; c++ )
		{
			const ShaderComboDefinition& rCombo = a_pCombos[ c ];
			if ( rCombo.uiStages & uiStage )
				continue;

			const TUINT uiValueOffset = ( i / rCombo.uiStride ) % TUINT( rCombo.iMaxValue - rCombo.iMinValue + 1 );
			uiCanonical -= uiValueOffset * rCombo.uiStride;
		}

		pCanonical[ i ] = uiCanonical;
		if ( uiCanonical == i )
			pUnique[ uiNumUnique++ ] = i;
	}

	const TUINT64  uiCacheHash  = ComputeShaderCacheHash( a_pchFilepath, a_pEntrypoint, a_pTarget, a_pCombos, a_uiNumCombos, a_uiNumPermutations );
	const TString8 strCachePath = GetShaderCachePath( a_pchFilepath, a_pEntrypoint, a_pTarget );

	const TBOOL bLoadedFromCache = LoadShaderCacheBlobs( strCachePath, uiCacheHash, &m_vecBlobs[ 0 ], a_uiNumPermutations );

	if ( bLoadedFromCache )
	{
		InterlockedExchangeAdd( &g_iCompiledShaderPermutations, LONG( a_uiNumPermutations ) );
	}
	else
	{
		ShaderCompileWorkerContext context;
		context.pchFilepath    = a_pchFilepath;
		context.pchSource      = strSource.GetString();
		context.pEntrypoint    = a_pEntrypoint;
		context.pTarget        = a_pTarget;
		context.pCombos        = a_pCombos;
		context.uiNumCombos    = a_uiNumCombos;
		context.ppBlobs        = &m_vecBlobs[ 0 ];
		context.pWorkIndices   = pUnique;
		context.uiNumWorkItems = uiNumUnique;
		context.iNextIndex     = 0;
		context.iFailed        = 0;

		const TUINT uiThreadCount = GetShaderCompileThreadCount( uiNumUnique );
		if ( uiThreadCount == 1 )
		{
			ShaderCompileWorkerProc( &context );
		}
		else
		{
			T2DynamicVector<HANDLE> vecThreads( GetGlobalAllocator(), uiThreadCount, uiThreadCount );
			vecThreads.SetSize( uiThreadCount, TNULL );

			TUINT uiCreatedThreads = 0;
			for ( TUINT i = 0; i < uiThreadCount; i++ )
			{
				vecThreads[ i ] = CreateThread( TNULL, 0, ShaderCompileWorkerProc, &context, 0, TNULL );
				if ( !vecThreads[ i ] )
					break;

				uiCreatedThreads++;
			}

			if ( uiCreatedThreads == 0 )
			{
				context.iNextIndex = 0;
				ShaderCompileWorkerProc( &context );
			}
			else
			{
				WaitForMultipleObjects( uiCreatedThreads, &vecThreads[ 0 ], TRUE, INFINITE );

				for ( TUINT i = 0; i < uiCreatedThreads; i++ )
					CloseHandle( vecThreads[ i ] );
			}
		}

		if ( context.iFailed != 0 )
		{
			delete[] pCanonical;
			delete[] pUnique;
			ReleaseBlobs();
			return TFALSE;
		}

		// Aliased permutations count toward the warm-up progress total
		InterlockedExchangeAdd( &g_iCompiledShaderPermutations, LONG( a_uiNumPermutations - uiNumUnique ) );
	}

	for ( TUINT i = 0; i < a_uiNumPermutations; i++ )
	{
		if ( pCanonical[ i ] == i )
			continue;

		if ( m_vecBlobs[ i ] )
			m_vecBlobs[ i ]->Release();

		m_vecBlobs[ i ] = m_vecBlobs[ pCanonical[ i ] ];
		if ( m_vecBlobs[ i ] )
			m_vecBlobs[ i ]->AddRef();
	}

	delete[] pCanonical;
	delete[] pUnique;

	for ( TUINT uiIndex = 0; uiIndex < a_uiNumPermutations; uiIndex++ )
	{
		if ( !m_vecBlobs[ uiIndex ] )
		{
			ReleaseBlobs();
			return TFALSE;
		}
	}

	if ( !bLoadedFromCache )
		SaveShaderCacheBlobs( strCachePath, uiCacheHash, &m_vecBlobs[ 0 ], a_uiNumPermutations );

	return TTRUE;
}

TBOOL remaster::dx11::ShaderCombo::CreateVertexShaders()
{
	ReleaseVertexShaders();

	if ( m_vecBlobs.IsEmpty() )
		return TFALSE;

	if ( m_vecVertexShaders.Size() != m_vecBlobs.Size() )
	{
		m_vecVertexShaders.Clear();
		m_vecVertexShaders.SetSize( m_vecBlobs.Size(), TNULL );
	}

	for ( TINT i = 0; i < m_vecBlobs.Size(); i++ )
	{
		ID3DBlob* pBlob = m_vecBlobs[ i ];
		TVALIDPTR( pBlob );
		if ( !pBlob )
			return TFALSE;

		// Aliased permutations share their canonical blob pointer; reuse its shader object
		TBOOL bReused = TFALSE;
		for ( TINT j = 0; j < i; j++ )
		{
			if ( m_vecBlobs[ j ] == pBlob )
			{
				m_vecVertexShaders[ i ] = m_vecVertexShaders[ j ];
				m_vecVertexShaders[ i ]->AddRef();
				bReused = TTRUE;
				break;
			}
		}

		if ( bReused )
			continue;

		DX11_API_VALIDATE_EXIT( CreateVertexShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), &m_vecVertexShaders[ i ] ) );
	}

	return TTRUE;
}

TBOOL remaster::dx11::ShaderCombo::CreatePixelShaders()
{
	ReleasePixelShaders();

	if ( m_vecBlobs.IsEmpty() )
		return TFALSE;

	if ( m_vecPixelShaders.Size() != m_vecBlobs.Size() )
	{
		m_vecPixelShaders.Clear();
		m_vecPixelShaders.SetSize( m_vecBlobs.Size(), TNULL );
	}

	for ( TINT i = 0; i < m_vecBlobs.Size(); i++ )
	{
		ID3DBlob* pBlob = m_vecBlobs[ i ];
		TVALIDPTR( pBlob );
		if ( !pBlob )
			return TFALSE;

		// Aliased permutations share their canonical blob pointer; reuse its shader object
		TBOOL bReused = TFALSE;
		for ( TINT j = 0; j < i; j++ )
		{
			if ( m_vecBlobs[ j ] == pBlob )
			{
				m_vecPixelShaders[ i ] = m_vecPixelShaders[ j ];
				m_vecPixelShaders[ i ]->AddRef();
				bReused = TTRUE;
				break;
			}
		}

		if ( bReused )
			continue;

		DX11_API_VALIDATE_EXIT( CreatePixelShader( pBlob->GetBufferPointer(), pBlob->GetBufferSize(), &m_vecPixelShaders[ i ] ) );
	}

	return TTRUE;
}

ID3DBlob* remaster::dx11::ShaderCombo::GetBlob( TUINT a_uiIndex ) const
{
	TASSERT( a_uiIndex < TUINT( m_vecBlobs.Size() ) );
	if ( a_uiIndex >= TUINT( m_vecBlobs.Size() ) )
		return TNULL;

	return m_vecBlobs[ a_uiIndex ];
}

ID3D11VertexShader* remaster::dx11::ShaderCombo::GetVertexShader( TUINT a_uiIndex ) const
{
	TASSERT( a_uiIndex < TUINT( m_vecVertexShaders.Size() ) );
	if ( a_uiIndex >= TUINT( m_vecVertexShaders.Size() ) )
		return TNULL;

	return m_vecVertexShaders[ a_uiIndex ];
}

ID3D11PixelShader* remaster::dx11::ShaderCombo::GetPixelShader( TUINT a_uiIndex ) const
{
	TASSERT( a_uiIndex < TUINT( m_vecPixelShaders.Size() ) );
	if ( a_uiIndex >= TUINT( m_vecPixelShaders.Size() ) )
		return TNULL;

	return m_vecPixelShaders[ a_uiIndex ];
}

ID3D11VertexShader** remaster::dx11::ShaderCombo::GetVertexShaderPtr( TUINT a_uiIndex )
{
	TASSERT( a_uiIndex < TUINT( m_vecVertexShaders.Size() ) );
	if ( a_uiIndex >= TUINT( m_vecVertexShaders.Size() ) )
		return TNULL;

	return &m_vecVertexShaders[ a_uiIndex ];
}

ID3D11PixelShader** remaster::dx11::ShaderCombo::GetPixelShaderPtr( TUINT a_uiIndex )
{
	TASSERT( a_uiIndex < TUINT( m_vecPixelShaders.Size() ) );
	if ( a_uiIndex >= TUINT( m_vecPixelShaders.Size() ) )
		return TNULL;

	return &m_vecPixelShaders[ a_uiIndex ];
}

ID3DBlob* remaster::dx11::CompileShader( const TCHAR* a_pchSrcData, LPCSTR a_pEntrypoint, LPCSTR a_pTarget, const D3D_SHADER_MACRO* a_pDefines )
{
	return CompileShaderInternal( a_pchSrcData, TNULL, a_pEntrypoint, a_pTarget, a_pDefines, TNULL );
}

ID3DBlob* remaster::dx11::CompileShaderFromFile( const TCHAR* a_pchFilepath, LPCSTR a_pEntrypoint, LPCSTR a_pTarget, const D3D_SHADER_MACRO* a_pDefines )
{
	TFile* pFile    = TFile::Create( a_pchFilepath );
	DWORD  fileSize = pFile->GetSize();
	TCHAR* srcData  = new TCHAR[ fileSize + 1 ];
	pFile->Read( srcData, fileSize );
	srcData[ fileSize ] = '\0';
	pFile->Destroy();

	ShaderIncludeHandler includeHandler( a_pchFilepath );
	ID3DBlob* shader = CompileShaderInternal( srcData, a_pchFilepath, a_pEntrypoint, a_pTarget, a_pDefines, &includeHandler );
	delete[] srcData;

	return shader;
}

HRESULT remaster::dx11::CreatePixelShader( const void* a_pShaderBytecode, SIZE_T a_uiBytecodeLength, ID3D11PixelShader** a_ppPixelShader )
{
	HRESULT hRes = g_pRender->GetD3D11Device()->CreatePixelShader( a_pShaderBytecode, a_uiBytecodeLength, NULL, a_ppPixelShader );
	TASSERT( SUCCEEDED( hRes ), "Couldnt Create Pixel Shader" );

	return hRes;
}

HRESULT remaster::dx11::CreateVertexShader( const void* a_pShaderBytecode, SIZE_T a_uiBytecodeLength, ID3D11VertexShader** a_ppVertexShader )
{
	HRESULT hRes = g_pRender->GetD3D11Device()->CreateVertexShader( a_pShaderBytecode, a_uiBytecodeLength, NULL, a_ppVertexShader );
	TASSERT( SUCCEEDED( hRes ), "Couldnt Create Vertex Shader" );

	return hRes;
}

ID3D11Buffer* remaster::dx11::CreateBuffer( TUINT a_uiFlags, TUINT a_uiDataSize, const void* a_pData, D3D11_USAGE a_eUsage, TUINT a_eCPUAccessFlags )
{
	D3D11_BUFFER_DESC bufferDesc;

	bufferDesc.ByteWidth           = a_uiDataSize;
	bufferDesc.Usage               = a_eUsage;
	bufferDesc.CPUAccessFlags      = a_eCPUAccessFlags;
	bufferDesc.MiscFlags           = 0;
	bufferDesc.StructureByteStride = 0;

	if ( a_uiFlags == 0 )
	{
		bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	}
	else
	{
		bufferDesc.BindFlags = ( a_uiFlags == 1 ) ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_CONSTANT_BUFFER;
	}

	ID3D11Buffer* pBuffer;

	if ( a_pData != TNULL )
	{
		D3D11_SUBRESOURCE_DATA subData;
		subData.pSysMem          = a_pData;
		subData.SysMemPitch      = 0;
		subData.SysMemSlicePitch = 0;

		g_pRender->GetD3D11Device()->CreateBuffer( &bufferDesc, &subData, &pBuffer );
	}
	else
	{
		g_pRender->GetD3D11Device()->CreateBuffer( &bufferDesc, TNULL, &pBuffer );
	}

	return pBuffer;
}

ID3D11ShaderResourceView* remaster::dx11::CreateTexture( TUINT a_uiWidth, TUINT a_uiHeight, DXGI_FORMAT a_eFormat, const void* a_pData, D3D11_USAGE a_eUsage, TUINT32 a_eCPUAccessFlags, TUINT32 a_uiSampleDescCount, CreateTextureFlags a_eFlags )
{
	D3D11_SUBRESOURCE_DATA subResourceData = {};
	D3D11_TEXTURE2D_DESC   textureDesc     = {};

	textureDesc.SampleDesc.Count   = a_uiSampleDescCount;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.ArraySize          = 1;
	textureDesc.Usage              = a_eUsage;
	textureDesc.Width              = a_uiWidth;
	textureDesc.Height             = a_uiHeight;
	textureDesc.Format             = a_eFormat;
	textureDesc.CPUAccessFlags     = a_eCPUAccessFlags;
	textureDesc.MipLevels          = ( a_eFlags & CTF_GEN_MIPMAPS ) ? 0 : 1;
	textureDesc.MiscFlags          = ( a_eFlags & CTF_GEN_MIPMAPS ) ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
	textureDesc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;

	if ( a_eFlags & CTF_RENDER_TARGET || a_eFlags & CTF_GEN_MIPMAPS )
		textureDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;

	ID3D11Texture2D* pTexture = TNULL;

	if ( a_pData == TNULL )
	{
		g_pRender->GetD3D11Device()->CreateTexture2D( &textureDesc, TNULL, &pTexture );
	}
	else
	{
		if ( a_eFlags & CTF_GEN_MIPMAPS )
		{
			// Load data after allocating the buffer
			g_pRender->GetD3D11Device()->CreateTexture2D( &textureDesc, TNULL, &pTexture );

			g_pRender->GetD3D11DeviceContext()->UpdateSubresource(
			    pTexture,
			    0,
			    TNULL,
			    a_pData,
			    GetTextureRowPitch( a_eFormat, a_uiWidth ),
			    0
			);
		}
		else
		{
			// Load texture data on create
			D3D11_SUBRESOURCE_DATA subresourceData;
			subresourceData.pSysMem          = a_pData;
			subresourceData.SysMemPitch      = GetTextureRowPitch( a_eFormat, a_uiWidth );
			subresourceData.SysMemSlicePitch = GetTextureDepthPitch( a_eFormat, a_uiWidth, a_uiHeight );

			DX11_API_VALIDATE( g_pRender->GetD3D11Device()->CreateTexture2D( &textureDesc, &subresourceData, &pTexture ) );
		}
	}

	if ( pTexture )
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc;
		shaderResourceViewDesc.Format                    = textureDesc.Format;
		shaderResourceViewDesc.ViewDimension             = ( a_uiSampleDescCount > 1 ) ? D3D_SRV_DIMENSION_TEXTURE2DMS : D3D_SRV_DIMENSION_TEXTURE2D;
		shaderResourceViewDesc.Texture2D.MipLevels       = -1;
		shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;

		ID3D11ShaderResourceView* pShaderResourceView = TNULL;
		DX11_API_VALIDATE( g_pRender->GetD3D11Device()->CreateShaderResourceView( pTexture, &shaderResourceViewDesc, &pShaderResourceView ) );

		if ( ( a_eFlags & CTF_GEN_MIPMAPS ) && pShaderResourceView != TNULL )
		{
			g_pRender->GetD3D11DeviceContext()->GenerateMips( pShaderResourceView );
		}

		pTexture->Release();
		return pShaderResourceView;
	}

	return TNULL;
}

TINT remaster::dx11::GetTextureRowPitch( DXGI_FORMAT a_eFormat, TUINT a_uiWidth )
{
	switch ( a_eFormat )
	{
		case DXGI_FORMAT_UNKNOWN: return 0;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return a_uiWidth << 4;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return a_uiWidth << 3;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_B8G8R8A8_UNORM: return a_uiWidth << 2;
		case DXGI_FORMAT_A8_UNORM:
		case DXGI_FORMAT_R8_UINT: return a_uiWidth;
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC4_UNORM: return ( ( a_uiWidth + 3U ) >> 2 ) << 3;
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC5_UNORM: return ( ( a_uiWidth + 3U ) >> 2 ) << 4;
		case DXGI_FORMAT_B8G8R8X8_UNORM: return a_uiWidth * 3;
		case DXGI_FORMAT_B4G4R4A4_UNORM: return a_uiWidth * 2;
	}

	TASSERT( TFALSE );
	return 0;
}

TINT remaster::dx11::GetTextureDepthPitch( DXGI_FORMAT a_eFormat, TUINT a_uiWidth, TUINT a_uiHeight )
{
	switch ( a_eFormat )
	{
		case DXGI_FORMAT_UNKNOWN: return 0;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return a_uiWidth * a_uiHeight * 16;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return a_uiWidth * a_uiHeight * 8;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_B8G8R8A8_UNORM: return a_uiWidth * a_uiHeight * 4;
		case DXGI_FORMAT_R8_UINT:;
		case DXGI_FORMAT_A8_UNORM: return a_uiWidth * a_uiHeight;
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC4_UNORM: return ( ( a_uiWidth + 3U ) >> 2 ) * ( ( a_uiHeight + 3U ) >> 2 ) * 8;
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC5_UNORM: return ( ( a_uiWidth + 3U ) >> 2 ) * ( ( a_uiHeight + 3U ) >> 2 ) * 16;
		case DXGI_FORMAT_B8G8R8X8_UNORM: return a_uiWidth * a_uiHeight * 3;
		case DXGI_FORMAT_B4G4R4A4_UNORM: return a_uiWidth * a_uiHeight * 2;
	}

	TASSERT( TFALSE );
	return 0;
}

TBOOL remaster::dx11::IsColorEqual( const TFLOAT a_pColor1[ 4 ], const TFLOAT a_pColor2[ 4 ] )
{
	return ( a_pColor1[ 0 ] == a_pColor2[ 0 ] ) && ( a_pColor1[ 1 ] == a_pColor2[ 1 ] ) && ( a_pColor1[ 2 ] == a_pColor2[ 2 ] ) && ( a_pColor1[ 3 ] == a_pColor2[ 3 ] );
}
