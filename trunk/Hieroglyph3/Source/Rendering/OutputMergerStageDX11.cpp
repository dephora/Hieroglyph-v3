//--------------------------------------------------------------------------------
// This file is a portion of the Hieroglyph 3 Rendering Engine.  It is distributed
// under the MIT License, available in the root of this distribution and 
// at the following URL:
//
// http://www.opensource.org/licenses/mit-license.php
//
// Copyright (c) 2003-2010 Jason Zink 
//--------------------------------------------------------------------------------

//--------------------------------------------------------------------------------
#include "OutputMergerStageDX11.h"
//--------------------------------------------------------------------------------
using namespace Glyph3;
//--------------------------------------------------------------------------------
OutputMergerStageDX11::OutputMergerStageDX11()
{
	for ( int i = 0; i < 8; i++ )
	{
		RenderTargetViews[i] = 0;
		APIRenderTargetViews[i] = 0;
	}

	DepthTargetViews = 0;
	for ( int i = 0; i < D3D11_PS_CS_UAV_REGISTER_COUNT; i++ ) UnorderedAccessViews[i] = 0;
}
//--------------------------------------------------------------------------------
OutputMergerStageDX11::~OutputMergerStageDX11()
{
	
}
//--------------------------------------------------------------------------------
void OutputMergerStageDX11::SetFeautureLevel( D3D_FEATURE_LEVEL level )
{
	m_FeatureLevel = level;
}
//--------------------------------------------------------------------------------
void OutputMergerStageDX11::SetRenderTargetView( int index, ID3D11RenderTargetView* pView )
{
	if ( ( index >= 0 ) && ( index < 8 ) )
	{
		RenderTargetViews[index] = pView;
	}
}
//--------------------------------------------------------------------------------
void OutputMergerStageDX11::SetDepthStencilView( ID3D11DepthStencilView* pView )
{
	DepthTargetViews = pView;
}
//--------------------------------------------------------------------------------
void OutputMergerStageDX11::SetUnorderedAccessView( int index, ID3D11UnorderedAccessView* pUAV )
{
	if ( ( index >= 0 ) && ( index < D3D11_PS_CS_UAV_REGISTER_COUNT ) )
	{
		UnorderedAccessViews[index] = pUAV;
	}
}
//--------------------------------------------------------------------------------
void OutputMergerStageDX11::BindResources( ID3D11DeviceContext* pContext )
{
	// Update all of the render targets and depth stencil targets
	//pContext->OMSetRenderTargetsAndUnorderedAccessViews( 8, RenderTargetViews, DepthTargetViews, 
	//	0, D3D11_PS_CS_UAV_REGISTER_COUNT, UnorderedAccessViews, (UINT*)&UnorderedAccessViews );
	
	// Find the highest index that doesn't match.
	int max = 0;
	for ( int i = 7; i >= 0; i-- )
	{
		if ( RenderTargetViews[i] != APIRenderTargetViews[i] )
		{
			max = i+1;
			break;
		}
	}

	// If any targets are different then copy them over.
	if ( max > 0 )
		pContext->OMSetRenderTargets( 7, RenderTargetViews, DepthTargetViews );
	
	// Update the API views to know what to update next time.
	for ( int i = 0; i < 8; i++ )
		APIRenderTargetViews[i] = RenderTargetViews[i];
}
//--------------------------------------------------------------------------------
void OutputMergerStageDX11::ClearResources( ID3D11DeviceContext* pContext )
{
	// Clear out all array elements in our cached arrays.  This will be used to 
	// write nulls into the context later on.

	memset( RenderTargetViews, 0, 8 * sizeof( RenderTargetViews[0] ) );
	DepthTargetViews = 0;
	memset( UnorderedAccessViews, 0, sizeof( UnorderedAccessViews[0] ) * D3D11_PS_CS_UAV_REGISTER_COUNT );
}
//--------------------------------------------------------------------------------
void OutputMergerStageDX11::UnbindResources( ID3D11DeviceContext* pContext )
{
	// Clear out the resource settings.

	ClearResources( pContext );

	// Bind the changes to the pipeline.

	BindResources( pContext );
}
//--------------------------------------------------------------------------------