#pragma once

#include <workspace/core/render/device.hxx>

class c_target
{
public:
    ~c_target( );

    bool create( int width, int height );
    bool matches( int width, int height ) const;
    void bind( );
    void clear( float red, float green, float blue, float alpha );
    void bind_color( );
    void snapshot( );
    void bind_scene( );
    void bind_mirror( );
    void clear_mirror( float red, float green, float blue, float alpha );
    ID3D11ShaderResourceView* mirror_view( ) const { return m_mirror_srv; }
    ID3D11ShaderResourceView* colour_view( ) const { return m_srv; }
    float reflection_plane = 0.0f;   // viewport +0x268
    bool reflection_enabled = true;  // VuWater +0x44: 0x558C00 draws nothing without it
    float clear_colour[ 4 ] = { 0.55f, 0.74f, 0.90f, 1.0f };   // the record's +4 once a world sets it (c_engine::clear_colour); no world: the old sky colour

private:
    void release( );

    int m_width{};
    int m_height{};
    int m_mirror_width{};
    int m_mirror_height{};
#if HYDRO_HOST_WINDOWS
    ID3D11Texture2D*          m_color{};
    ID3D11Texture2D*          m_depth{};
    ID3D11Texture2D*          m_color_copy{};
    ID3D11Texture2D*          m_depth_copy{};
    ID3D11RenderTargetView*   m_rtv{};
    ID3D11DepthStencilView*   m_dsv{};
    ID3D11ShaderResourceView* m_srv{};
    ID3D11ShaderResourceView* m_color_copy_srv{};
    ID3D11ShaderResourceView* m_depth_srv{};
    ID3D11Texture2D*          m_mirror{};
    ID3D11Texture2D*          m_mirror_depth{};
    ID3D11RenderTargetView*   m_mirror_rtv{};
    ID3D11DepthStencilView*   m_mirror_dsv{};
    ID3D11ShaderResourceView* m_mirror_srv{};
#endif
};

c_target& g_target( );

#include <workspace/utility/logger.hxx>

namespace
{
    c_target g_target_instance{};
}

c_target& g_target( )
{
    return g_target_instance;
}

c_target::~c_target( )
{
    release( );
}

void c_target::release( )
{
#if HYDRO_HOST_WINDOWS
    if ( m_mirror_srv ) m_mirror_srv->Release( );
    if ( m_mirror_dsv ) m_mirror_dsv->Release( );
    if ( m_mirror_rtv ) m_mirror_rtv->Release( );
    if ( m_mirror_depth ) m_mirror_depth->Release( );
    if ( m_mirror ) m_mirror->Release( );
    if ( m_depth_srv ) m_depth_srv->Release( );
    if ( m_color_copy_srv ) m_color_copy_srv->Release( );
    if ( m_srv ) m_srv->Release( );
    if ( m_dsv ) m_dsv->Release( );
    if ( m_rtv ) m_rtv->Release( );
    if ( m_depth_copy ) m_depth_copy->Release( );
    if ( m_color_copy ) m_color_copy->Release( );
    if ( m_depth ) m_depth->Release( );
    if ( m_color ) m_color->Release( );
    m_mirror_srv = nullptr;
    m_mirror_dsv = nullptr;
    m_mirror_rtv = nullptr;
    m_mirror_depth = nullptr;
    m_mirror = nullptr;
    m_depth_srv = nullptr;
    m_color_copy_srv = nullptr;
    m_srv = nullptr;
    m_dsv = nullptr;
    m_rtv = nullptr;
    m_depth_copy = nullptr;
    m_color_copy = nullptr;
    m_depth = nullptr;
    m_color = nullptr;
#endif
    m_width = 0;
    m_height = 0;
    m_mirror_width = 0;
    m_mirror_height = 0;
}

bool c_target::matches( int width, int height ) const
{
    return m_width == width && m_height == height && m_width > 0;
}

bool c_target::create( int width, int height )
{
#if HYDRO_HOST_WINDOWS
    release( );

    D3D11_TEXTURE2D_DESC color_desc{};
    color_desc.Width = static_cast< UINT >( width );
    color_desc.Height = static_cast< UINT >( height );
    color_desc.MipLevels = 1;
    color_desc.ArraySize = 1;
    color_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    color_desc.SampleDesc.Count = 1;
    color_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT result = g_device( ).device( )->CreateTexture2D( &color_desc, nullptr, &m_color );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "color target failed: 0x%08x", static_cast< unsigned >( result ) );
        return false;
    }

    D3D11_TEXTURE2D_DESC depth_desc = color_desc;
    depth_desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    result = g_device( ).device( )->CreateTexture2D( &depth_desc, nullptr, &m_depth );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "depth target failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }

    result = g_device( ).device( )->CreateRenderTargetView( m_color, nullptr, &m_rtv );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "render target view failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC depth_view{};
    depth_view.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    result = g_device( ).device( )->CreateDepthStencilView( m_depth, &depth_view, &m_dsv );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "depth view failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }

    result = g_device( ).device( )->CreateShaderResourceView( m_color, nullptr, &m_srv );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "color view failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }

    D3D11_TEXTURE2D_DESC color_copy = color_desc;
    color_copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    result = g_device( ).device( )->CreateTexture2D( &color_copy, nullptr, &m_color_copy );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "color copy failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC color_view{};
    color_view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    color_view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    color_view.Texture2D.MipLevels = 1;
    result = g_device( ).device( )->CreateShaderResourceView( m_color_copy, &color_view, &m_color_copy_srv );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "color copy view failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }

    D3D11_TEXTURE2D_DESC depth_copy = depth_desc;
    depth_copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    result = g_device( ).device( )->CreateTexture2D( &depth_copy, nullptr, &m_depth_copy );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "depth copy failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC depth_sample{};
    depth_sample.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    depth_sample.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    depth_sample.Texture2D.MipLevels = 1;
    result = g_device( ).device( )->CreateShaderResourceView( m_depth_copy, &depth_sample, &m_depth_srv );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "depth copy view failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }

    m_mirror_width = width > 4 ? width / 4 : 1;
    m_mirror_height = height > 4 ? height / 4 : 1;
    D3D11_TEXTURE2D_DESC mirror_desc = color_desc;
    mirror_desc.Width = static_cast< UINT >( m_mirror_width );
    mirror_desc.Height = static_cast< UINT >( m_mirror_height );
    result = g_device( ).device( )->CreateTexture2D( &mirror_desc, nullptr, &m_mirror );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "reflection target failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }
    result = g_device( ).device( )->CreateRenderTargetView( m_mirror, nullptr, &m_mirror_rtv );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "reflection view failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }
    result = g_device( ).device( )->CreateShaderResourceView( m_mirror, nullptr, &m_mirror_srv );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "reflection sample failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }
    D3D11_TEXTURE2D_DESC mirror_depth = mirror_desc;
    mirror_depth.Format = DXGI_FORMAT_R24G8_TYPELESS;
    mirror_depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    result = g_device( ).device( )->CreateTexture2D( &mirror_depth, nullptr, &m_mirror_depth );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "reflection depth failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }
    result = g_device( ).device( )->CreateDepthStencilView( m_mirror_depth, &depth_view, &m_mirror_dsv );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "reflection depth view failed: 0x%08x", static_cast< unsigned >( result ) );
        release( );
        return false;
    }

    m_width = width;
    m_height = height;
    LOG_SUCCESS( "target %dx%d", m_width, m_height );
    return true;
#else
    ( void )width;
    ( void )height;
    return false;
#endif
}

void c_target::bind( )
{
#if HYDRO_HOST_WINDOWS
    g_device( ).context( )->OMSetRenderTargets( 1, &m_rtv, m_dsv );
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast< float >( m_width );
    viewport.Height = static_cast< float >( m_height );
    viewport.MaxDepth = 1.0f;
    g_device( ).context( )->RSSetViewports( 1, &viewport );
#endif
}

void c_target::clear( float red, float green, float blue, float alpha )
{
#if HYDRO_HOST_WINDOWS
    const float color[ 4 ] = { red, green, blue, alpha };
    g_device( ).context( )->ClearRenderTargetView( m_rtv, color );
    g_device( ).context( )->ClearDepthStencilView( m_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0 );
#else
    ( void )red;
    ( void )green;
    ( void )blue;
    ( void )alpha;
#endif
}

void c_target::bind_color( )
{
#if HYDRO_HOST_WINDOWS
    g_device( ).context( )->PSSetShaderResources( 0, 1, &m_srv );
#endif
}

void c_target::snapshot( )
{
#if HYDRO_HOST_WINDOWS
    if ( m_color_copy == nullptr || m_depth_copy == nullptr )
        return;
    ID3D11DeviceContext* context = g_device( ).context( );
    context->OMSetRenderTargets( 0, nullptr, nullptr );
    context->CopyResource( m_color_copy, m_color );
    context->CopyResource( m_depth_copy, m_depth );
    bind( );
#endif
}

void c_target::bind_scene( )
{
#if HYDRO_HOST_WINDOWS
    ID3D11DeviceContext* context = g_device( ).context( );
    context->PSSetShaderResources( 2, 1, &m_color_copy_srv );
    context->PSSetShaderResources( 3, 1, &m_depth_srv );
#endif
}

void c_target::bind_mirror( )
{
#if HYDRO_HOST_WINDOWS
    ID3D11ShaderResourceView* unbound = nullptr;
    ID3D11DeviceContext* context = g_device( ).context( );
    context->PSSetShaderResources( 5, 1, &unbound );
    context->OMSetRenderTargets( 1, &m_mirror_rtv, m_mirror_dsv );
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast< float >( m_mirror_width );
    viewport.Height = static_cast< float >( m_mirror_height );
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports( 1, &viewport );
#endif
}

void c_target::clear_mirror( float red, float green, float blue, float alpha )
{
#if HYDRO_HOST_WINDOWS
    const float color[ 4 ] = { red, green, blue, alpha };
    g_device( ).context( )->ClearRenderTargetView( m_mirror_rtv, color );
    g_device( ).context( )->ClearDepthStencilView( m_mirror_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0 );
#else
    ( void )red;
    ( void )green;
    ( void )blue;
    ( void )alpha;
#endif
}

