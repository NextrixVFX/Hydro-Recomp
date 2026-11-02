#pragma once

#include <impl/engine.hxx>

class c_composer : public c_draw
{
public:
    ~c_composer( );
    void tick( ) override;

private:
    void size_targets( );
    void sky( );
    void main_pass( );
    void blit( );
    void blit_screen( );
    void letterbox( );
};

#include <workspace/core/render/device.hxx>
#include <workspace/core/render/swap.hxx>
#include <workspace/core/render/target.hxx>
#include <workspace/core/render/shader.hxx>
#include <workspace/core/scene/viewport.hxx>
#include <workspace/core/scene/scene.hxx>
#include <workspace/core/render/mesh.hxx>
#include <workspace/core/scene/screen.hxx>

#if HYDRO_HOST_WINDOWS
namespace
{
    ID3D11Texture2D*          g_overlay_texture = nullptr;
    ID3D11ShaderResourceView* g_overlay_view = nullptr;
    ID3D11BlendState*         g_overlay_blend = nullptr;
    int                       g_overlay_width = 0;
    int                       g_overlay_height = 0;

    void release_overlay( )
    {
        if ( g_overlay_view )
            g_overlay_view->Release( );
        if ( g_overlay_texture )
            g_overlay_texture->Release( );
        g_overlay_view = nullptr;
        g_overlay_texture = nullptr;
        if ( g_overlay_blend )
            g_overlay_blend->Release( );
        g_overlay_blend = nullptr;
        g_overlay_width = 0;
        g_overlay_height = 0;
    }
}
#endif

c_composer::~c_composer( )
{
#if HYDRO_HOST_WINDOWS
    release_overlay( );
#endif
}

void c_composer::tick( )
{
    size_targets( );
    g_viewport( ).build( g_swap( ).width( ), g_swap( ).height( ) );
    main_pass( );
    blit( );

    if ( g_screen( ).pixels != nullptr && g_screen( ).width > 0 && g_screen( ).height > 0 )
        blit_screen( );
    else
        letterbox( );
}

void c_composer::size_targets( )
{
    int width = g_swap( ).width( );
    int height = g_swap( ).height( );
    if ( !g_target( ).matches( width, height ) )
        g_target( ).create( width, height );
}

void c_composer::sky( )
{
    const float* c = g_target( ).clear_colour;
    g_target( ).clear( c[ 0 ], c[ 1 ], c[ 2 ], c[ 3 ] );
}

void c_composer::main_pass( )
{
    g_target( ).bind( );
    sky( );
    g_geometry( ).draw( g_swap( ).width( ), g_swap( ).height( ), g_target( ).reflection_enabled );   // hydro world/stage.hxx: gfx_composer_t's whole frame
}

void c_composer::blit( )
{
#if HYDRO_HOST_WINDOWS
    ID3D11ShaderResourceView* unbound = nullptr;
    g_swap( ).bind( g_device( ) );
    g_device( ).context( )->RSSetState( nullptr );
    g_shader( ).bind_blit( );
    g_target( ).bind_color( );
    g_device( ).context( )->Draw( 3, 0 );
    g_device( ).context( )->PSSetShaderResources( 0, 1, &unbound );
#endif
}

void c_composer::blit_screen( )
{
#if HYDRO_HOST_WINDOWS
    const screen_t& screen = g_screen( );
    ID3D11Device* device = g_device( ).device( );
    ID3D11DeviceContext* context = g_device( ).context( );
    if ( g_overlay_width != screen.width || g_overlay_height != screen.height )
    {
        release_overlay( );

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast< UINT >( screen.width );
        description.Height = static_cast< UINT >( screen.height );
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if ( FAILED( device->CreateTexture2D( &description, nullptr, &g_overlay_texture ) ) )
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = description.Format;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        if ( FAILED( device->CreateShaderResourceView( g_overlay_texture, &view, &g_overlay_view ) ) )
        {
            release_overlay( );
            return;
        }

        g_overlay_width = screen.width;
        g_overlay_height = screen.height;
    }

    if ( g_overlay_texture == nullptr || g_overlay_view == nullptr )
        return;

    context->UpdateSubresource( g_overlay_texture, 0, nullptr, screen.pixels, static_cast< UINT >( screen.width * 4 ), 0 );

    if ( g_overlay_blend == nullptr )
    {
        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[ 0 ].BlendEnable = TRUE;
        blend.RenderTarget[ 0 ].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend.RenderTarget[ 0 ].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[ 0 ].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[ 0 ].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[ 0 ].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[ 0 ].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[ 0 ].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if ( FAILED( device->CreateBlendState( &blend, &g_overlay_blend ) ) )
            return;
    }

    ID3D11ShaderResourceView* unbound = nullptr;
    const float factor[ 4 ] = { 1.0f, 1.0f, 1.0f, 1.0f };
    g_swap( ).bind( g_device( ) );
    context->RSSetState( nullptr );
    g_shader( ).bind_blit( );
    context->OMSetBlendState( g_overlay_blend, factor, 0xffffffff );
    context->PSSetShaderResources( 0, 1, &g_overlay_view );
    context->Draw( 3, 0 );
    context->OMSetBlendState( nullptr, factor, 0xffffffff );
    context->PSSetShaderResources( 0, 1, &unbound );
#else
    ( void )this;
#endif
}

void c_composer::letterbox( )
{
    float height = static_cast< float >( g_swap( ).height( ) );
    if ( height <= 0.0f )
        return;

    float aspect = static_cast< float >( g_swap( ).width( ) ) / height;
    if ( aspect > 1.7f && aspect < 1.85f )
        return;
}
