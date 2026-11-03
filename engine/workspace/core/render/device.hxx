#pragma once

#include <impl/host.hxx>

#if HYDRO_HOST_WINDOWS
#  include <d3d11.h>
#elif HYDRO_HOST_WASM
#  include <emscripten/html5.h>
#endif

class c_device
{
public:
    c_device( ) = default;
    ~c_device( );

    bool create( );

#if HYDRO_HOST_WINDOWS
    ID3D11Device* device( ) const { return m_device; }
    ID3D11DeviceContext* context( ) const { return m_context; }
#endif

private:
#if HYDRO_HOST_WINDOWS
    ID3D11Device*        m_device{};
    ID3D11DeviceContext* m_context{};
#elif HYDRO_HOST_WASM
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE m_webgl{};
#endif
};

c_device& g_device( );

#include <workspace/utility/logger.hxx>

#if HYDRO_HOST_WINDOWS
#  pragma comment( lib, "d3d11.lib" )
namespace
{
    c_device* g_device_instance = nullptr;
}
#endif

c_device& g_device( )
{
#if HYDRO_HOST_WINDOWS
    return *g_device_instance;
#else
    static c_device missing{};
    return missing;
#endif
}

c_device::~c_device( )
{
#if HYDRO_HOST_WINDOWS
    if ( g_device_instance == this )
        g_device_instance = nullptr;
    if ( m_context )
        m_context->Release( );
    if ( m_device )
        m_device->Release( );
#endif
}

bool c_device::create( )
{
#if HYDRO_HOST_WINDOWS
    UINT flags = 0;
#if defined( _DEBUG )
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL level{};
    HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &m_device,
        &level,
        &m_context
    );

    if ( FAILED( result ) && ( flags & D3D11_CREATE_DEVICE_DEBUG ) )
    {
        result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &m_device,
            &level,
            &m_context
        );
    }

    if ( FAILED( result ) )
    {
        LOG_ERROR( "D3D11CreateDevice failed: 0x%08x", static_cast< unsigned >( result ) );
        return false;
    }

    g_device_instance = this;
    LOG_SUCCESS( "device feature level 0x%x", static_cast< unsigned >( level ) );
    return true;
#elif HYDRO_HOST_WASM
    EmscriptenWebGLContextAttributes attributes{};
    emscripten_webgl_init_context_attributes( &attributes );
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;
    attributes.alpha = EM_FALSE;
    attributes.depth = EM_TRUE;
    attributes.stencil = EM_TRUE;
    attributes.antialias = EM_FALSE;
    attributes.premultipliedAlpha = EM_FALSE;
    attributes.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    m_webgl = emscripten_webgl_create_context( "#canvas", &attributes );
    if ( m_webgl <= 0 )
    {
        LOG_ERROR( "WebGL2 context failed (%d)", m_webgl );
        return false;
    }
    emscripten_webgl_make_context_current( m_webgl );
    // Retail colour is BC1/BC3. WebGL2 leaves those enums invalid until the extension is enabled.
    if ( !emscripten_webgl_enable_extension( m_webgl, "WEBGL_compressed_texture_s3tc" ) )
        LOG_WARNING( "WebGL: S3TC textures are not available" );
    emscripten_webgl_enable_extension( m_webgl, "WEBGL_compressed_texture_s3tc_srgb" );
    LOG_SUCCESS( "WebGL2 context" );
    return true;
#else
    LOG_ERROR( "device create is unimplemented on %s", host::name( ) );
    return false;
#endif
}
