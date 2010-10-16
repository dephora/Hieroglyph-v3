//--------------------------------------------------------------------------------
#include "App.h"
#include "Log.h"

#include <sstream>

#include "EventManager.h"
#include "EvtFrameStart.h"
#include "EvtChar.h"
#include "EvtKeyUp.h"
#include "EvtKeyDown.h"

#include "ScriptManager.h"

#include "SwapChainConfigDX11.h"
#include "Texture2dConfigDX11.h"

#include "RasterizerStateConfigDX11.h"
#include "ParameterManagerDX11.h"
#include "SamplerParameterDX11.h"
#include "ShaderResourceParameterDX11.h"

#define clamp(value,minimum,maximum) (max(min((value),(maximum)),(minimum)))

using namespace Glyph3;
//--------------------------------------------------------------------------------
App AppInstance; // Provides an instance of the application
//--------------------------------------------------------------------------------


//--------------------------------------------------------------------------------
App::App()
{
	m_bSaveScreenshot = false;
	m_bViewPointInAutoMode = true;
	m_bSolidRender = false;
}
//--------------------------------------------------------------------------------
bool App::ConfigureEngineComponents()
{
	// The application currently supplies the 
	int width = 400;
	int height = 300;
	bool windowed = true;

	// Set the render window parameters and initialize the window
	m_pWindow = new Win32RenderWindow();
	m_pWindow->SetPosition( 25, 25 );
	m_pWindow->SetSize( width, height );
	m_pWindow->SetCaption( GetName( ) );
	m_pWindow->Initialize();

	
	// Create the renderer and initialize it for the desired device
	// type and feature level.

	m_pRenderer11 = new RendererDX11();

	if ( !m_pRenderer11->Initialize( D3D_DRIVER_TYPE_HARDWARE, D3D_FEATURE_LEVEL_11_0 ) )
	{
		Log::Get().Write( L"Could not create hardware device, trying to create the reference device..." );

		if ( !m_pRenderer11->Initialize( D3D_DRIVER_TYPE_REFERENCE, D3D_FEATURE_LEVEL_11_0 ) )
		{
			ShowWindow( m_pWindow->GetHandle(), SW_HIDE );
			MessageBox( m_pWindow->GetHandle(), L"Could not create a hardware or software Direct3D 11 device - the program will now abort!", L"Hieroglyph 3 Rendering", MB_ICONEXCLAMATION | MB_SYSTEMMODAL );
			RequestTermination();			
			return( false );
		}

		// If using the reference device, utilize a fixed time step for any animations.
		m_pTimer->SetFixedTimeStep( 1.0f / 10.0f );
	}


	// Create a swap chain for the window that we started out with.  This
	// demonstrates using a configuration object for fast and concise object
	// creation.

	SwapChainConfigDX11 Config;
	Config.SetWidth( m_pWindow->GetWidth() );
	Config.SetHeight( m_pWindow->GetHeight() );
	Config.SetOutputWindow( m_pWindow->GetHandle() );
	m_iSwapChain = m_pRenderer11->CreateSwapChain( &Config );
	m_pWindow->SetSwapChain( m_iSwapChain );

	// We'll keep a copy of the render target index to use in later examples.

	m_RenderTarget = m_pRenderer11->GetSwapChainResource( m_iSwapChain );

	// Next we create a depth buffer for use in the traditional rendering
	// pipeline.

	Texture2dConfigDX11 DepthConfig;
	DepthConfig.SetDepthBuffer( width, height );
	m_DepthTarget = m_pRenderer11->CreateTexture2D( &DepthConfig, 0 );
	
	// Bind the swap chain render target and the depth buffer for use in 
	// rendering.  

	m_pRenderer11->pImmPipeline->ClearRenderTargets();
	m_pRenderer11->pImmPipeline->BindRenderTargets( 0, m_RenderTarget );
	m_pRenderer11->pImmPipeline->BindDepthTarget( m_DepthTarget );
	m_pRenderer11->pImmPipeline->ApplyRenderTargets();


	// Create a view port to use on the scene.  This basically selects the 
	// entire floating point area of the render target.

	D3D11_VIEWPORT viewport;
	viewport.Width = static_cast< float >( width );
	viewport.Height = static_cast< float >( height );
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;

	int ViewPort = m_pRenderer11->CreateViewPort( viewport );
	m_pRenderer11->pImmPipeline->SetViewPort( ViewPort );
	
	return( true );
}
//--------------------------------------------------------------------------------
void App::ShutdownEngineComponents()
{
	if ( m_pRenderer11 )
	{
		m_pRenderer11->Shutdown();
		delete m_pRenderer11;
	}

	if ( m_pWindow )
	{
		m_pWindow->Shutdown();
		delete m_pWindow;
	}
}
//--------------------------------------------------------------------------------
void App::Initialize()
{
	// Basic event handling is supported with the EventManager class.  This is a 
	// singleton class that allows an EventListener to register which events it
	// wants to receive.

	EventManager* pEventManager = EventManager::Get( );

	// The application object wants to know about these three events, so it 
	// registers itself with the appropriate event IDs.

	pEventManager->AddEventListener( SYSTEM_KEYBOARD_KEYUP, this );
	pEventManager->AddEventListener( SYSTEM_KEYBOARD_KEYDOWN, this );
	pEventManager->AddEventListener( SYSTEM_KEYBOARD_CHAR, this );

	// Create the necessary resources
	CreateTerrainGeometry();
	CreateTerrainShaders();
	CreateTerrainTextures();

	// Set initial shader values
	Vector4f vMinMaxDist = Vector4f( 0.25f, 8.0f, /* unused */ 0.0f, /* unused */ 0.0f );
	m_pRenderer11->m_pParamMgr->SetVectorParameter( L"minMaxDistance", &vMinMaxDist );

	Vector4f vMinMaxLod = Vector4f( 1.0f, 8.0f, /* unused */ 0.0f, /* unused */ 0.0f );
	m_pRenderer11->m_pParamMgr->SetVectorParameter( L"minMaxLOD", &vMinMaxLod );

	// Create the text rendering
	m_pFont = new SpriteFontDX11();
	m_pFont->Initialize( L"Consolas", 12.0f, 0, true );
	
	m_pSpriteRenderer = new SpriteRendererDX11();
	m_pSpriteRenderer->Initialize();
}
//--------------------------------------------------------------------------------
void App::Update()
{
	// Update the timer to determine the elapsed time since last frame.  This can 
	// then used for animation during the frame.

	m_pTimer->Update();

	// Process any new events
	EventManager::Get()->ProcessEvent( new EvtFrameStart() );

	// Update any animation/camera config
	UpdateViewState();

	// Clear the window to white
	m_pRenderer11->pImmPipeline->ClearBuffers( Vector4f( 1.0f, 1.0f, 1.0f, 1.0f ), 1.0f );

	// Draw the main geometry
	m_pTerrainEffect->ConfigurePipeline( m_pRenderer11->pImmPipeline, m_pRenderer11->m_pParamMgr );
	m_pRenderer11->pImmPipeline->Draw( *m_pTerrainEffect, *m_pTerrainGeometry, m_pRenderer11->m_pParamMgr );

	// Draw the UI text
	if( !m_bSaveScreenshot )
	{
		// Don't draw text if we're taking a screenshot - don't want the images
		// cluttered with UI text!
		std::wstringstream out;
		out << L"Hieroglyph 3 : Interlocking Terrain Tiles\nFPS: " << m_pTimer->Framerate();

		// Screenshot
		out << L"\n S : Take Screenshot";

		// Rasterizer State
		out << L"\n W : Toggle Wireframe Display";

		// Advanced LoD
		out << L"\n L : Toggle LoD Complexity";

		// Automatic Rotation
		out << L"\n A : Toggle Automated Camera";

		m_pSpriteRenderer->RenderText( m_pRenderer11->pImmPipeline, m_pRenderer11->m_pParamMgr, *m_pFont, out.str().c_str(), Matrix4f::Identity(), Vector4f( 1.f, 0.f, 0.f, 1.f ) );
	}

	// Present the final image to the screen
	m_pRenderer11->Present( m_pWindow->GetHandle(), m_pWindow->GetSwapChain() );

	// Save a screenshot if desired.  This is done by pressing the 's' key, which
	// demonstrates how an event is sent and handled by an event listener (which
	// in this case is the application object itself).

	if ( m_bSaveScreenshot  )
	{
		m_bSaveScreenshot = false;
		m_pRenderer11->pImmPipeline->SaveTextureScreenShot( 0, std::wstring( L"TessParamsDemo_" ), D3DX11_IFF_BMP );
	}
}
//--------------------------------------------------------------------------------
void App::Shutdown()
{
	// Safely dispose of our rendering resource
	SAFE_RELEASE( m_pTerrainGeometry );
	SAFE_DELETE( m_pTerrainEffect );

	// Print the framerate out for the log before shutting down.
	std::wstringstream out;
	out << L"Max FPS: " << m_pTimer->MaxFramerate();
	Log::Get().Write( out.str() );
}
//--------------------------------------------------------------------------------
bool App::HandleEvent( IEvent* pEvent )
{
	eEVENT e = pEvent->GetEventType();

	if ( e == SYSTEM_KEYBOARD_KEYDOWN )
	{
		EvtKeyDown* pKeyDown = (EvtKeyDown*)pEvent;

		unsigned int key = pKeyDown->GetCharacterCode();

		return( true );
	}
	else if ( e == SYSTEM_KEYBOARD_KEYUP )
	{
		EvtKeyUp* pKeyUp = (EvtKeyUp*)pEvent;

		unsigned int key = pKeyUp->GetCharacterCode();

		if ( key == VK_ESCAPE ) // 'Esc' Key - Exit the application
		{
			this->RequestTermination();
			return( true );
		}
		else if ( key == 0x53 ) // 'S' Key - Save a screen shot for the next frame
		{
			m_bSaveScreenshot = true;
			return( true );
		}
		else if ( 'W' == key )
		{
			// Toggle Wireframe
			m_bSolidRender = !m_bSolidRender;
			m_pTerrainEffect->m_iRasterizerState = m_bSolidRender ? m_rsSolid : m_rsWireframe;
		}
		else if ( 'L' == key )
		{
			// Toggle between simple and CS-based LOD
		}
		else if ( 'A' == key )
		{
			// Toggle automated camera
			m_bViewPointInAutoMode = !m_bViewPointInAutoMode;
		}
		else
		{
			return( false );
		}
	}

	
	return( false );
}
//--------------------------------------------------------------------------------
std::wstring App::GetName( )
{
	return( std::wstring( L"Direct3D 11 Interlocking Terrain Tiles Demo" ) );
}
//--------------------------------------------------------------------------------
void App::CreateTerrainGeometry()
{
	Log::Get().Write( L"Creating terrain geometry" );

	// Setup actual resource
	SAFE_RELEASE( m_pTerrainGeometry );
	m_pTerrainGeometry = new GeometryDX11( );

	// Create vertex data
	VertexElementDX11 *pPositions = new VertexElementDX11( 3, (TERRAIN_X_LEN + 1) * (TERRAIN_Z_LEN + 1) );
		pPositions->m_SemanticName = "CONTROL_POINT_POSITION";
		pPositions->m_uiSemanticIndex = 0;
		pPositions->m_Format = DXGI_FORMAT_R32G32B32_FLOAT;
		pPositions->m_uiInputSlot = 0;
		pPositions->m_uiAlignedByteOffset = 0;
		pPositions->m_InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		pPositions->m_uiInstanceDataStepRate = 0;

	VertexElementDX11 *pTexCoords = new VertexElementDX11( 2, (TERRAIN_X_LEN + 1) * (TERRAIN_Z_LEN + 1) );
		pTexCoords->m_SemanticName = "CONTROL_POINT_TEXCOORD";
		pTexCoords->m_uiSemanticIndex = 0;
		pTexCoords->m_Format = DXGI_FORMAT_R32G32_FLOAT;
		pTexCoords->m_uiInputSlot = 0;
		pTexCoords->m_uiAlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		pTexCoords->m_InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		pTexCoords->m_uiInstanceDataStepRate = 0;

	Vector3f *pPosData = pPositions->Get3f( 0 );
	Vector2f *pTCData = pTexCoords->Get2f( 0 );

	float fWidth = static_cast< float >( TERRAIN_X_LEN );
	float fHeight = static_cast< float >( TERRAIN_Z_LEN );

	for( int x = 0; x < TERRAIN_X_LEN + 1; ++x )
	{
		for( int z = 0; z < TERRAIN_Z_LEN + 1; ++z )
		{
			float fX = static_cast<float>(x) / fWidth - 0.5f;
			float fZ = static_cast<float>(z) / fHeight - 0.5f;
			pPosData[ x + z * (TERRAIN_X_LEN + 1) ] = Vector3f( fX, 0.0f, fZ );
			pTCData[ x + z * (TERRAIN_X_LEN + 1) ] = Vector2f( fX + 0.5f, fZ + 0.5f );
		}
	}

	m_pTerrainGeometry->AddElement( pPositions );
	m_pTerrainGeometry->AddElement( pTexCoords );

	// Create index data
	for( int x = 0; x < TERRAIN_X_LEN; ++x )
	{
		for( int z = 0; z < TERRAIN_Z_LEN; ++z )
		{
			// Define 12 control points per terrain quad

			// 0-3 are the actual quad vertices
			m_pTerrainGeometry->AddIndex( (z + 0) + (x + 0) * (TERRAIN_X_LEN + 1) );
			m_pTerrainGeometry->AddIndex( (z + 1) + (x + 0) * (TERRAIN_X_LEN + 1) );
			m_pTerrainGeometry->AddIndex( (z + 0) + (x + 1) * (TERRAIN_X_LEN + 1) );
			m_pTerrainGeometry->AddIndex( (z + 1) + (x + 1) * (TERRAIN_X_LEN + 1) );
	
			// 4-5 are +x
			m_pTerrainGeometry->AddIndex( clamp(z + 0, 0, TERRAIN_Z_LEN) + clamp(x + 2, 0, TERRAIN_X_LEN) * (TERRAIN_X_LEN + 1) );
			m_pTerrainGeometry->AddIndex( clamp(z + 1, 0, TERRAIN_Z_LEN) + clamp(x + 2, 0, TERRAIN_X_LEN) * (TERRAIN_X_LEN + 1) );

			// 6-7 are +z
			m_pTerrainGeometry->AddIndex( clamp(z + 2, 0, TERRAIN_Z_LEN) + clamp(x + 0, 0, TERRAIN_X_LEN) * (TERRAIN_X_LEN + 1) );
			m_pTerrainGeometry->AddIndex( clamp(z + 2, 0, TERRAIN_Z_LEN) + clamp(x + 1, 0, TERRAIN_X_LEN) * (TERRAIN_X_LEN + 1) );

			// 8-9 are -x
			m_pTerrainGeometry->AddIndex( clamp(z + 0, 0, TERRAIN_Z_LEN) + clamp(x - 1, 0, TERRAIN_X_LEN) * (TERRAIN_X_LEN + 1) );
			m_pTerrainGeometry->AddIndex( clamp(z + 1, 0, TERRAIN_Z_LEN) + clamp(x - 1, 0, TERRAIN_X_LEN) * (TERRAIN_X_LEN + 1) );

			// 10-11 are -z			
			m_pTerrainGeometry->AddIndex( clamp(z - 1, 0, TERRAIN_Z_LEN) + clamp(x + 0, 0, TERRAIN_X_LEN) * (TERRAIN_X_LEN + 1) );
			m_pTerrainGeometry->AddIndex( clamp(z - 1, 0, TERRAIN_Z_LEN) + clamp(x + 1, 0, TERRAIN_X_LEN) * (TERRAIN_X_LEN + 1) );
		}
	}

	// Move the in-memory geometry to be 
	// an actual renderable resource
	m_pTerrainGeometry->LoadToBuffers();
	m_pTerrainGeometry->SetPrimitiveType( D3D11_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST );

	Log::Get().Write( L"Created terrain geometry" );
}
//--------------------------------------------------------------------------------
void App::CreateTerrainShaders()
{
	Log::Get().Write( L"Creating shaders" );

	SAFE_DELETE( m_pTerrainEffect );
	m_pTerrainEffect = new RenderEffectDX11();

	// Create the vertex shader
	m_pTerrainEffect->m_iVertexShader = 
		m_pRenderer11->LoadShader( VERTEX_SHADER,
		std::wstring( L"../Data/Shaders/InterlockingTerrainTiles.hlsl" ),
		std::wstring( L"vsMain" ),
		std::wstring( L"vs_5_0" ) );
	_ASSERT( -1 != m_pTerrainEffect->m_iVertexShader );
	
	Log::Get().Write( L"... vertex shader created" );

	// Create the hull shader
	m_pTerrainEffect->m_iHullShader = 
		m_pRenderer11->LoadShader( HULL_SHADER,
		std::wstring( L"../Data/Shaders/InterlockingTerrainTiles.hlsl" ),
		std::wstring( L"hsMain" ),
		std::wstring( L"hs_5_0" ) );
	_ASSERT( -1 != m_pTerrainEffect->m_iHullShader );

	Log::Get().Write( L"... hull shader created" );

	// Create the domain shader
	m_pTerrainEffect->m_iDomainShader = 
		m_pRenderer11->LoadShader( DOMAIN_SHADER,
		std::wstring( L"../Data/Shaders/InterlockingTerrainTiles.hlsl" ),
		std::wstring( L"dsMain" ),
		std::wstring( L"ds_5_0" ) );
	_ASSERT( -1 != m_pTerrainEffect->m_iDomainShader );

	Log::Get().Write( L"... domain shader created" );

	// Create the geometry shader
	m_pTerrainEffect->m_iGeometryShader = 
		m_pRenderer11->LoadShader( GEOMETRY_SHADER,
		std::wstring( L"../Data/Shaders/InterlockingTerrainTiles.hlsl" ),
		std::wstring( L"gsMain" ),
		std::wstring( L"gs_5_0" ) );
	_ASSERT( -1 != m_pTerrainEffect->m_iGeometryShader );

	Log::Get().Write( L"... geometry shader created" );

	// Create the pixel shader
	m_pTerrainEffect->m_iPixelShader = 
		m_pRenderer11->LoadShader( PIXEL_SHADER,
		std::wstring( L"../Data/Shaders/InterlockingTerrainTiles.hlsl" ),
		std::wstring( L"psMain" ),
		std::wstring( L"ps_5_0" ) );
	_ASSERT( -1 != m_pTerrainEffect->m_iPixelShader );

	Log::Get().Write( L"... pixel shader created" );

	// Create rasterizer states
	RasterizerStateConfigDX11 RS;
	
	RS.FillMode = D3D11_FILL_WIREFRAME;
	RS.CullMode = D3D11_CULL_FRONT;
	m_rsWireframe = m_pRenderer11->CreateRasterizerState( &RS );

	RS.FillMode = D3D11_FILL_SOLID;
	RS.CullMode = D3D11_CULL_FRONT;
	m_rsSolid = m_pRenderer11->CreateRasterizerState( &RS );

	// Assign default state
	m_pTerrainEffect->m_iRasterizerState = m_bSolidRender ? m_rsSolid : m_rsWireframe;

	Log::Get().Write( L"Created all shaders" );
}
//--------------------------------------------------------------------------------
void App::CreateTerrainTextures()
{
	Log::Get().Write( L"Creating textures" );

	// Load the texture
	m_pHeightMapTexture = m_pRenderer11->LoadTexture( std::wstring( L"../Data/Textures/TerrainHeightMap.png" ) );

	// Create the SRV
	ShaderResourceParameterDX11* pHeightMapTexParam = new ShaderResourceParameterDX11();
    pHeightMapTexParam->SetParameterData( &m_pHeightMapTexture->m_iResourceSRV );
    pHeightMapTexParam->SetName( std::wstring( L"texHeightMap" ) );
	
	// Map it to the param manager
	m_pRenderer11->m_pParamMgr->SetShaderResourceParameter( L"texHeightMap", m_pHeightMapTexture );

	// Create a sampler
	D3D11_SAMPLER_DESC sampDesc;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.BorderColor[0] = sampDesc.BorderColor[1] = sampDesc.BorderColor[2] = sampDesc.BorderColor[3] = 0;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    sampDesc.MinLOD = 0.0f;
    sampDesc.MipLODBias = 0.0f;
    int samplerState = m_pRenderer11->CreateSamplerState( &sampDesc );

	// Set it to the param manager
	m_pRenderer11->m_pParamMgr->SetSamplerParameter( L"smpHeightMap", &samplerState );

	Log::Get().Write( L"Created textures" );
}
//--------------------------------------------------------------------------------
void App::UpdateViewState()
{
	if( m_bViewPointInAutoMode )
	{
		// If in 'auto' mode then simply keep rotating
		float time = m_pTimer->Runtime();

		// Create the world matrix
		Matrix4f mWorld, mWorldScale, mWorldRotation;
		
		D3DXMatrixIdentity( reinterpret_cast<D3DXMATRIX*>(&mWorld) );
		
		// Geometry is defined in the [-0.5,+0.5] range on the XZ plane

		D3DXMatrixScaling( reinterpret_cast<D3DXMATRIX*>(&mWorldScale), 15.0f, 1.0f, 15.0f ); // Domain Shader controls Y scale, not here!
		D3DXMatrixMultiply( reinterpret_cast<D3DXMATRIX*>(&mWorld), reinterpret_cast<D3DXMATRIX*>(&mWorld), reinterpret_cast<D3DXMATRIX*>(&mWorldScale) );

		// Create the inverse transpose world matrix
		Matrix4f mInvTPoseWorld;

		D3DXMatrixInverse( reinterpret_cast<D3DXMATRIX*>(&mInvTPoseWorld), NULL, reinterpret_cast<D3DXMATRIX*>(&mWorld) );
		D3DXMatrixTranspose( reinterpret_cast<D3DXMATRIX*>(&mInvTPoseWorld), reinterpret_cast<D3DXMATRIX*>(&mInvTPoseWorld) );

		
		D3DXVECTOR3 vLookAt = D3DXVECTOR3( 0.0f, 0.0f, 0.0f );
		D3DXVECTOR3 vLookFrom = D3DXVECTOR3( 0.0f, 0.0f, 0.0f );
		D3DXVECTOR3 vLookUp = D3DXVECTOR3( 0.0f, 1.0f, 0.0f );

		// based on time, determine where the camera is at and where it should look to
		float distance = time / 30.0f; // 30 seconds to do a single circuit
		float fromAngle = fmodf( distance * 2.0f * D3DX_PI, 2.0f * D3DX_PI);
		float toAngle = fmodf( (distance + 0.08f) * 2.0f * D3DX_PI, 2.0f * D3DX_PI); // ~30 degrees in front

		vLookFrom.x = sinf(fromAngle) * 10.0f;
		vLookFrom.y = 3.f;
		vLookFrom.z = cosf(fromAngle) * 10.0f;

		vLookAt.x = sinf(toAngle) * 3.0f;
		vLookAt.y = 0.3f;
		vLookAt.z = cosf(toAngle) * 3.0f;

		// Create the view matrix
		Matrix4f mView;
		D3DXMatrixLookAtLH( reinterpret_cast<D3DXMATRIX*>(&mView), &vLookFrom, &vLookAt, &vLookUp );

		// Create the projection matrix
		Matrix4f mProj;
		D3DXMatrixPerspectiveFovLH( reinterpret_cast<D3DXMATRIX*>(&mProj), static_cast< float >(D3DX_PI) / 3.0f, static_cast<float>(m_pWindow->GetWidth()) /  static_cast<float>(m_pWindow->GetHeight()), 0.1f, 50.0f );

		// Composite together for the final transform
		Matrix4f mViewProj = mView * mProj;

		// Set the various values to the parameter manager
		m_pRenderer11->m_pParamMgr->SetMatrixParameter( L"mWorld", &mWorld );
		m_pRenderer11->m_pParamMgr->SetMatrixParameter( L"mViewProj", &mViewProj );
		m_pRenderer11->m_pParamMgr->SetMatrixParameter( L"mInvTposeWorld", &mInvTPoseWorld );

		Vector4f vCam = Vector4f( vLookFrom.x, vLookFrom.y, vLookFrom.z, /* unused */ 0.0f );
		m_pRenderer11->m_pParamMgr->SetVectorParameter( L"cameraPosition", &vCam );
	}
	else
	{
		// Else, if in 'manual' mode then update according to the
		// current user's input
	}
}
//--------------------------------------------------------------------------------