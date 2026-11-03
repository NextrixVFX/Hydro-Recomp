#pragma once

#include <workspace/core/render/device.hxx>
#include <workspace/utility/trace.hxx>
#include <workspace/core/render/window.hxx>

#if HYDRO_HOST_WINDOWS
#  include <dxgi1_5.h>
#elif HYDRO_HOST_WASM
#  include <GLES3/gl3.h>
#  include <emscripten/html5.h>
#endif

class c_swap
{
public:
    c_swap( ) = default;
    ~c_swap( );

    bool create( c_device& device, c_window& window );
    // The platform side of 0x540890 (gfx.hxx gfx_device_t::resize): keeps our tearing flag, and the chain has no depth of its own here (the scene's is the target's).
    bool resize( c_device& device, int width, int height );
    void bind( c_device& device );
    void clear( c_device& device, float red, float green, float blue, float alpha );
    void present( );
    int width( ) const { return m_width; }
    int height( ) const { return m_height; }

#if HYDRO_HOST_WINDOWS
    IDXGISwapChain1* chain( ) const { return m_swap; }
#endif

private:
    int m_width{};
    int m_height{};
    // Ours, not retail: always unthrottled, so a frame time is the game's own cost and not the display cadence.
    // Tearing where the display allows it, or the compositor can still hold a flip-model window to the refresh.
    bool m_tearing = false;
#if HYDRO_HOST_WINDOWS
    IDXGISwapChain1*         m_swap{};
    ID3D11RenderTargetView*  m_target{};
#endif
};

c_swap& g_swap( );

// The flip interval g 0x75907C vt+0x28 writes (retail's device +8, the Present's first argument). Held, NOT applied: the user asked for vsync off unless they turn it on, so retail's interval is ignored here.
inline std::uint32_t g_swap_interval = 1u;
// Ours, not retail: the user's VSync setting, default off. On: Present interval 1, and no tearing flag (DXGI refuses it with an interval).
inline bool g_swap_vsync = false;

#include <workspace/utility/logger.hxx>

#if HYDRO_HOST_WINDOWS
#  pragma comment( lib, "dxgi.lib" )
namespace
{
    c_swap* g_swap_instance = nullptr;
}
#elif HYDRO_HOST_WASM
inline c_swap* g_swap_live = nullptr;
#endif

c_swap& g_swap( )
{
#if HYDRO_HOST_WINDOWS
    return *g_swap_instance;
#elif HYDRO_HOST_WASM
    return *g_swap_live;
#else
    static c_swap missing{};
    return missing;
#endif
}

c_swap::~c_swap( )
{
#if HYDRO_HOST_WINDOWS
    if ( g_swap_instance == this )
        g_swap_instance = nullptr;
    if ( m_target )
        m_target->Release( );
    if ( m_swap )
        m_swap->Release( );
#endif
}

bool c_swap::create( c_device& device, c_window& window )
{
#if HYDRO_HOST_WINDOWS
    IDXGIDevice* dxgi_device = nullptr;
    HRESULT result = device.device( )->QueryInterface( __uuidof( IDXGIDevice ), reinterpret_cast< void** >( &dxgi_device ) );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "QueryInterface IDXGIDevice failed: 0x%08x", static_cast< unsigned >( result ) );
        return false;
    }

    IDXGIAdapter* adapter = nullptr;
    result = dxgi_device->GetAdapter( &adapter );
    dxgi_device->Release( );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "GetAdapter failed: 0x%08x", static_cast< unsigned >( result ) );
        return false;
    }

    IDXGIFactory2* factory = nullptr;
    result = adapter->GetParent( __uuidof( IDXGIFactory2 ), reinterpret_cast< void** >( &factory ) );
    adapter->Release( );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "GetParent IDXGIFactory2 failed: 0x%08x", static_cast< unsigned >( result ) );
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = static_cast< UINT >( window.width( ) );
    description.Height = static_cast< UINT >( window.height( ) );
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    IDXGIFactory5* factory5 = nullptr;
    if ( SUCCEEDED( factory->QueryInterface( __uuidof( IDXGIFactory5 ), reinterpret_cast< void** >( &factory5 ) ) ) )
    {
        BOOL tearing = FALSE;
        if ( SUCCEEDED( factory5->CheckFeatureSupport( DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof( tearing ) ) ) && tearing )
        {
            m_tearing = true;
            description.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }
        factory5->Release( );
    }

    result = factory->CreateSwapChainForHwnd(
        device.device( ),
        window.handle( ),
        &description,
        nullptr,
        nullptr,
        &m_swap
    );
    factory->Release( );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "CreateSwapChainForHwnd failed: 0x%08x", static_cast< unsigned >( result ) );
        return false;
    }

    ID3D11Texture2D* backbuffer = nullptr;
    result = m_swap->GetBuffer( 0, __uuidof( ID3D11Texture2D ), reinterpret_cast< void** >( &backbuffer ) );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "GetBuffer failed: 0x%08x", static_cast< unsigned >( result ) );
        return false;
    }

    result = device.device( )->CreateRenderTargetView( backbuffer, nullptr, &m_target );
    backbuffer->Release( );
    if ( FAILED( result ) )
    {
        LOG_ERROR( "CreateRenderTargetView failed: 0x%08x", static_cast< unsigned >( result ) );
        return false;
    }

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast< float >( window.width( ) );
    viewport.Height = static_cast< float >( window.height( ) );
    viewport.MaxDepth = 1.0f;
    device.context( )->RSSetViewports( 1, &viewport );

    m_width = window.width( );
    m_height = window.height( );
    g_swap_instance = this;
    LOG_SUCCESS( "swap %dx%d", window.width( ), window.height( ) );
    return true;
#elif HYDRO_HOST_WASM
    ( void )device;
    m_width = window.width( );
    m_height = window.height( );
    g_swap_live = this;
    LOG_SUCCESS( "swap %dx%d", m_width, m_height );
    return true;
#else
    ( void )device;
    ( void )window;
    LOG_ERROR( "swap create is unimplemented on %s", host::name( ) );
    return false;
#endif
}

bool c_swap::resize( c_device& device, int width, int height )
{
#if HYDRO_HOST_WINDOWS
    if ( width <= 0 || height <= 0 || ( width == m_width && height == m_height ) )
        return true;

    // ResizeBuffers refuses while anything holds the back buffer: the bound view and the view itself, and the release must reach the driver first.
    device.context( )->OMSetRenderTargets( 0, nullptr, nullptr );
    m_target->Release( );
    m_target = nullptr;
    device.context( )->Flush( );

    HRESULT result = m_swap->ResizeBuffers( 2u, static_cast< UINT >( width ), static_cast< UINT >( height ), DXGI_FORMAT_R8G8B8A8_UNORM,
        m_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u );
    if ( FAILED( result ) )
        LOG_ERROR( "ResizeBuffers %dx%d failed: 0x%08x", width, height, static_cast< unsigned >( result ) );
    else
    {
        m_width = width;
        m_height = height;
    }

    // On a failure the old buffers stand, at the old size: the view comes back either way.
    ID3D11Texture2D* backbuffer = nullptr;
    const HRESULT buffer = m_swap->GetBuffer( 0, __uuidof( ID3D11Texture2D ), reinterpret_cast< void** >( &backbuffer ) );
    if ( FAILED( buffer ) )
    {
        LOG_ERROR( "GetBuffer failed: 0x%08x", static_cast< unsigned >( buffer ) );
        return false;
    }
    const HRESULT view = device.device( )->CreateRenderTargetView( backbuffer, nullptr, &m_target );
    backbuffer->Release( );
    if ( FAILED( view ) )
    {
        LOG_ERROR( "CreateRenderTargetView failed: 0x%08x", static_cast< unsigned >( view ) );
        return false;
    }
    LOG_SUCCESS( "swap %dx%d", m_width, m_height );
    return SUCCEEDED( result );
#elif HYDRO_HOST_WASM
    ( void )device;
    if ( width > 0 && height > 0 )
    {
        m_width = width;
        m_height = height;
        emscripten_set_canvas_element_size( "#canvas", width, height );
    }
    return true;
#else
    ( void )device;
    ( void )width;
    ( void )height;
    return false;
#endif
}

void c_swap::bind( c_device& device )
{
#if HYDRO_HOST_WINDOWS
    device.context( )->OMSetRenderTargets( 1, &m_target, nullptr );
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast< float >( m_width );
    viewport.Height = static_cast< float >( m_height );
    viewport.MaxDepth = 1.0f;
    device.context( )->RSSetViewports( 1, &viewport );
#else
    ( void )device;
#endif
}

void c_swap::clear( c_device& device, float red, float green, float blue, float alpha )
{
#if HYDRO_HOST_WINDOWS
    const float color[ 4 ] = { red, green, blue, alpha };
    device.context( )->OMSetRenderTargets( 1, &m_target, nullptr );
    device.context( )->ClearRenderTargetView( m_target, color );
#else
    ( void )device;
    ( void )red;
    ( void )green;
    ( void )blue;
    ( void )alpha;
#endif
}

void c_swap::present( )
{
#if HYDRO_HOST_WINDOWS
    if ( m_swap )
    {
        const double present_start = trace::now( );
        m_swap->Present( g_swap_vsync ? 1u : 0u, m_tearing && !g_swap_vsync ? DXGI_PRESENT_ALLOW_TEARING : 0u );
        trace::frame( ).present += trace::now( ) - present_start;
    }
    trace::report( );
#elif HYDRO_HOST_WASM
    glFlush( );
    trace::report( );
#endif
}
