#pragma once

// gfx_api_t on real D3D11: render/gfx.hxx's seam, one forwarding line per call, no decisions. A handle is the COM pointer. The
// description structs hold D3D11's layouts, asserted below. WebGL will be a sibling of this file, not a change to gfx.hxx.

#include <workspace/core/render/gfx.hxx>
#include <workspace/utility/drawstats.hxx>
#include <workspace/utility/spikes.hxx>

#include <impl/host.hxx>

#if HYDRO_HOST_WINDOWS
#  include <d3d11.h>
#  include <dxgi.h>

#  include <cstring>

static_assert( sizeof( gfx_buffer_desc_t ) == sizeof( D3D11_BUFFER_DESC ) );
static_assert( sizeof( gfx_texture_desc_t ) == sizeof( D3D11_TEXTURE2D_DESC ) );
static_assert( sizeof( gfx_depth_desc_t ) == sizeof( D3D11_DEPTH_STENCIL_DESC ) );
static_assert( sizeof( gfx_blend_desc_t ) == sizeof( D3D11_BLEND_DESC ) );
static_assert( sizeof( gfx_raster_desc_t ) == sizeof( D3D11_RASTERIZER_DESC ) );
static_assert( sizeof( gfx_viewport_t ) == sizeof( D3D11_VIEWPORT ) );
static_assert( sizeof( gfx_rect_t ) == sizeof( D3D11_RECT ) );
static_assert( sizeof( gfx_layout_element_t ) == sizeof( D3D11_INPUT_ELEMENT_DESC ) );
static_assert( sizeof( gfx_sampler_desc_t ) == sizeof( D3D11_SAMPLER_DESC ) );

struct gfx_direct_t final : gfx_api_t
{
    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    IDXGISwapChain* m_chain = nullptr;

    template < typename T >
    static T* as( gfx_handle_t h ) { return reinterpret_cast< T* >( h ); }
    template < typename T >
    static gfx_handle_t out( T* p ) { return reinterpret_cast< gfx_handle_t >( p ); }

    gfx_handle_t create_buffer( const gfx_buffer_desc_t& d ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11Buffer* o = nullptr;
        m_device->CreateBuffer( reinterpret_cast< const D3D11_BUFFER_DESC* >( &d ), nullptr, &o );
        return out( o );
    }
    gfx_handle_t create_texture( const gfx_texture_desc_t& d ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11Texture2D* o = nullptr;
        m_device->CreateTexture2D( reinterpret_cast< const D3D11_TEXTURE2D_DESC* >( &d ), nullptr, &o );
        return out( o );
    }
    gfx_handle_t create_view( gfx_handle_t t ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11ShaderResourceView* o = nullptr;
        m_device->CreateShaderResourceView( as< ID3D11Resource >( t ), nullptr, &o );
        return out( o );
    }
    gfx_handle_t create_layout( const gfx_layout_element_t* e, std::uint32_t n, const void* code, std::uint32_t size ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11InputLayout* o = nullptr;
        m_device->CreateInputLayout( reinterpret_cast< const D3D11_INPUT_ELEMENT_DESC* >( e ), n, code, size, &o );
        return out( o );
    }
    gfx_handle_t create_sampler( const gfx_sampler_desc_t& d ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11SamplerState* o = nullptr;
        m_device->CreateSamplerState( reinterpret_cast< const D3D11_SAMPLER_DESC* >( &d ), &o );
        return out( o );
    }
    gfx_handle_t create_target_view( gfx_handle_t t ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11RenderTargetView* o = nullptr;
        m_device->CreateRenderTargetView( as< ID3D11Resource >( t ), nullptr, &o );
        return out( o );
    }
    gfx_handle_t create_depth_view( gfx_handle_t t ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11DepthStencilView* o = nullptr;
        m_device->CreateDepthStencilView( as< ID3D11Resource >( t ), nullptr, &o );
        return out( o );
    }
    gfx_handle_t create_vertex_shader( const void* code, std::uint32_t size ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11VertexShader* o = nullptr;
        m_device->CreateVertexShader( code, size, nullptr, &o );
        return out( o );
    }
    gfx_handle_t create_pixel_shader( const void* code, std::uint32_t size ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11PixelShader* o = nullptr;
        m_device->CreatePixelShader( code, size, nullptr, &o );
        return out( o );
    }
    gfx_handle_t create_blend( const gfx_blend_desc_t& d ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11BlendState* o = nullptr;
        m_device->CreateBlendState( reinterpret_cast< const D3D11_BLEND_DESC* >( &d ), &o );
        return out( o );
    }
    gfx_handle_t create_depth( const gfx_depth_desc_t& d ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11DepthStencilState* o = nullptr;
        m_device->CreateDepthStencilState( reinterpret_cast< const D3D11_DEPTH_STENCIL_DESC* >( &d ), &o );
        return out( o );
    }
    gfx_handle_t create_raster( const gfx_raster_desc_t& d ) override
    {
        const spikes::scope_t spike( spikes::k_create ); spikes::count( spikes::c_creates );
        ID3D11RasterizerState* o = nullptr;
        m_device->CreateRasterizerState( reinterpret_cast< const D3D11_RASTERIZER_DESC* >( &d ), &o );
        return out( o );
    }
    void release( gfx_handle_t o ) override { as< IUnknown >( o )->Release( ); }
    void release_all( gfx_handle_t o ) override
    {
        while ( as< IUnknown >( o )->Release( ) != 0 )
        {
        }
    }

    void vs_constants( gfx_handle_t b ) override { ID3D11Buffer* p = as< ID3D11Buffer >( b ); m_context->VSSetConstantBuffers( 0, 1, &p ); }
    // A start slot past D3D11's 128 is 0x540B50's null branch (gfx.hxx, its TRAP): OURS, the call is dropped, the live runtime stomps state otherwise. Samplers below the same.
    void ps_views( std::uint32_t s, gfx_handle_t v ) override { if ( s >= 128u ) return; ++drawstats::at( ).textures; ID3D11ShaderResourceView* p = as< ID3D11ShaderResourceView >( v ); m_context->PSSetShaderResources( s, 1, &p ); }
    void ps_shader( gfx_handle_t s ) override { ++drawstats::at( ).shaders; m_context->PSSetShader( as< ID3D11PixelShader >( s ), nullptr, 0 ); }
    void ps_samplers( std::uint32_t s, gfx_handle_t v ) override { if ( s >= 16u ) return; ID3D11SamplerState* p = as< ID3D11SamplerState >( v ); m_context->PSSetSamplers( s, 1, &p ); }
    void vs_shader( gfx_handle_t s ) override { ++drawstats::at( ).shaders; m_context->VSSetShader( as< ID3D11VertexShader >( s ), nullptr, 0 ); }
    void draw_indexed( std::uint32_t c, std::uint32_t s, std::int32_t b ) override { drawstats::counts_t& n = drawstats::at( ); ++n.draws; n.indices += c; m_context->DrawIndexed( c, s, b ); }
    void draw( std::uint32_t c, std::uint32_t s ) override { drawstats::counts_t& n = drawstats::at( ); ++n.draws; n.indices += c; m_context->Draw( c, s ); }
    void* map( gfx_handle_t r, std::uint32_t t ) override
    {
        ++drawstats::at( ).constants;
        D3D11_MAPPED_SUBRESOURCE m{ };
        m_context->Map( as< ID3D11Resource >( r ), 0, static_cast< D3D11_MAP >( t ), 0, &m );
        return m.pData;
    }
    void unmap( gfx_handle_t r ) override { m_context->Unmap( as< ID3D11Resource >( r ), 0 ); }
    void ps_constants( gfx_handle_t b ) override { ID3D11Buffer* p = as< ID3D11Buffer >( b ); m_context->PSSetConstantBuffers( 0, 1, &p ); }
    void input_layout( gfx_handle_t l ) override { m_context->IASetInputLayout( as< ID3D11InputLayout >( l ) ); }
    void vertex_buffer( gfx_handle_t b, std::uint32_t s, std::uint32_t o ) override
    {
        ID3D11Buffer* p = as< ID3D11Buffer >( b );
        UINT stride = s, offset = o;
        m_context->IASetVertexBuffers( 0, 1, &p, &stride, &offset );
    }
    void index_buffer( gfx_handle_t b, std::uint32_t f, std::uint32_t o ) override { m_context->IASetIndexBuffer( as< ID3D11Buffer >( b ), static_cast< DXGI_FORMAT >( f ), o ); }
    void topology( std::uint32_t v ) override { m_context->IASetPrimitiveTopology( static_cast< D3D11_PRIMITIVE_TOPOLOGY >( v ) ); }
    void set_targets( std::uint32_t n, gfx_handle_t v, gfx_handle_t d ) override
    {
        ++drawstats::at( ).targets;
        ID3D11RenderTargetView* p = as< ID3D11RenderTargetView >( v );
        m_context->OMSetRenderTargets( n, n ? &p : nullptr, as< ID3D11DepthStencilView >( d ) );
    }
    void set_blend( gfx_handle_t s ) override { ++drawstats::at( ).states; m_context->OMSetBlendState( as< ID3D11BlendState >( s ), nullptr, 0xFFFFFFFFu ); }
    void set_depth( gfx_handle_t s ) override { ++drawstats::at( ).states; m_context->OMSetDepthStencilState( as< ID3D11DepthStencilState >( s ), 0 ); }
    void set_raster( gfx_handle_t s ) override { ++drawstats::at( ).states; m_context->RSSetState( as< ID3D11RasterizerState >( s ) ); }
    void set_viewport( const gfx_viewport_t& v ) override { m_context->RSSetViewports( 1, reinterpret_cast< const D3D11_VIEWPORT* >( &v ) ); }
    void set_scissor( const gfx_rect_t& r ) override { m_context->RSSetScissorRects( 1, reinterpret_cast< const D3D11_RECT* >( &r ) ); }
    void update_texture( gfx_handle_t t, std::uint32_t l, const void* d, std::uint32_t r, std::uint32_t s ) override { ++drawstats::at( ).constants; m_context->UpdateSubresource( as< ID3D11Resource >( t ), l, nullptr, d, r, s ); }
    void clear_target( gfx_handle_t v, const float* c ) override { m_context->ClearRenderTargetView( as< ID3D11RenderTargetView >( v ), c ); }
    void clear_depth( gfx_handle_t v, std::uint32_t f, float d, std::uint8_t s ) override { m_context->ClearDepthStencilView( as< ID3D11DepthStencilView >( v ), f, d, s ); }
    void get_targets( gfx_handle_t& v, gfx_handle_t& d ) override
    {
        ID3D11RenderTargetView* a = nullptr;
        ID3D11DepthStencilView* b = nullptr;
        m_context->OMGetRenderTargets( 1, &a, &b );
        v = out( a );
        d = out( b );
        if ( a )
            a->Release( );   // gfx.hxx's SUBSTITUTION: retail keeps these references
        if ( b )
            b->Release( );
    }
    void present( std::uint32_t s, std::uint32_t f ) override { m_chain->Present( s, f ); }
    gfx_handle_t back_buffer( ) override
    {
        ID3D11Texture2D* o = nullptr;
        m_chain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), reinterpret_cast< void** >( &o ) );
        return out( o );
    }
    void resize( std::uint32_t c, std::uint32_t w, std::uint32_t h, std::uint32_t f, std::uint32_t l ) override { m_chain->ResizeBuffers( c, w, h, static_cast< DXGI_FORMAT >( f ), l ); }
    void copy( void* to, std::uint32_t room, const void* from, std::uint32_t count ) override { memcpy_s( to, room, from, count ); }
};

#endif

#if HYDRO_HOST_WASM
#  include <workspace/core/render/webgl.hxx>
using gfx_platform_api_t = gfx_webgl_t;
#elif HYDRO_HOST_WINDOWS
using gfx_platform_api_t = gfx_direct_t;
#endif
