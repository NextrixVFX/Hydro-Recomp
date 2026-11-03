#pragma once

#include <impl/engine.hxx>

class c_scene
{
public:
    ~c_scene( );
    void add( c_node& node );
    void draw( );
    int count( ) const { return m_count; }

private:
    bool ensure( );
    static constexpr int capacity = 64;
    c_node* m_nodes[ capacity ]{};
    int m_count{};
    bool m_ready{};
};

c_scene& g_scene( );

#include <workspace/core/render/shader.hxx>
#include <workspace/core/scene/viewport.hxx>
#include <workspace/core/scene/light.hxx>
#include <workspace/utility/math.hxx>
#include <workspace/utility/logger.hxx>
#include <cstring>

#if HYDRO_HOST_WINDOWS
namespace
{
    struct vertex_t
    {
        float position[ 3 ];
        float normal[ 3 ];
    };

    struct constants_t
    {
        float world[ 16 ];
        float view[ 16 ];
        float projection[ 16 ];
        float light[ 4 ];
        float color[ 4 ];
    };

    ID3D11Buffer*             g_box = nullptr;
    ID3D11Buffer*             g_plane = nullptr;
    ID3D11DepthStencilState*  g_depth_on = nullptr;
    ID3D11DepthStencilState*  g_depth_off = nullptr;
    ID3D11RasterizerState*    g_raster = nullptr;

    int push_face( vertex_t* vertices, int count, const float* a, const float* b, const float* c, const float* d, const float* normal )
    {
        const float* corners[ 6 ] = { a, b, c, a, c, d };
        for ( int index = 0; index < 6; ++index )
        {
            vertices[ count ].position[ 0 ] = corners[ index ][ 0 ];
            vertices[ count ].position[ 1 ] = corners[ index ][ 1 ];
            vertices[ count ].position[ 2 ] = corners[ index ][ 2 ];
            vertices[ count ].normal[ 0 ] = normal[ 0 ];
            vertices[ count ].normal[ 1 ] = normal[ 1 ];
            vertices[ count ].normal[ 2 ] = normal[ 2 ];
            ++count;
        }
        return count;
    }

    ID3D11Buffer* vertex_buffer( const vertex_t* vertices, int count )
    {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = sizeof( vertex_t ) * static_cast< UINT >( count );
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = vertices;
        ID3D11Buffer* buffer = nullptr;
        g_device( ).device( )->CreateBuffer( &description, &data, &buffer );
        return buffer;
    }
}

c_scene::~c_scene( )
{
    if ( g_raster ) g_raster->Release( );
    if ( g_depth_off ) g_depth_off->Release( );
    if ( g_depth_on ) g_depth_on->Release( );
    if ( g_plane ) g_plane->Release( );
    if ( g_box ) g_box->Release( );
}
#else
c_scene::~c_scene( ) {}
#endif

namespace
{
    c_scene g_scene_instance{};
}

c_scene& g_scene( )
{
    return g_scene_instance;
}

void c_scene::add( c_node& node )
{
    if ( m_count >= capacity )
    {
        LOG_ERROR( "scene is full" );
        return;
    }
    m_nodes[ m_count++ ] = &node;
}

bool c_scene::ensure( )
{
    if ( m_ready )
        return true;

#if HYDRO_HOST_WINDOWS
    vertex_t box[ 36 ]{};
    int count = 0;
    const float p = 0.5f;
    const float n = -0.5f;
    const float px[ 3 ] = { 1, 0, 0 };
    const float nx[ 3 ] = { -1, 0, 0 };
    const float py[ 3 ] = { 0, 1, 0 };
    const float ny[ 3 ] = { 0, -1, 0 };
    const float pz[ 3 ] = { 0, 0, 1 };
    const float nz[ 3 ] = { 0, 0, -1 };
    const float a0[ 3 ] = { p, n, n }; const float b0[ 3 ] = { p, n, p }; const float c0[ 3 ] = { p, p, p }; const float d0[ 3 ] = { p, p, n };
    const float a1[ 3 ] = { n, p, n }; const float b1[ 3 ] = { n, p, p }; const float c1[ 3 ] = { n, n, p }; const float d1[ 3 ] = { n, n, n };
    const float a2[ 3 ] = { n, p, n }; const float b2[ 3 ] = { p, p, n }; const float c2[ 3 ] = { p, p, p }; const float d2[ 3 ] = { n, p, p };
    const float a3[ 3 ] = { p, n, n }; const float b3[ 3 ] = { n, n, n }; const float c3[ 3 ] = { n, n, p }; const float d3[ 3 ] = { p, n, p };
    const float a4[ 3 ] = { n, n, p }; const float b4[ 3 ] = { p, n, p }; const float c4[ 3 ] = { p, p, p }; const float d4[ 3 ] = { n, p, p };
    const float a5[ 3 ] = { n, p, n }; const float b5[ 3 ] = { p, p, n }; const float c5[ 3 ] = { p, n, n }; const float d5[ 3 ] = { n, n, n };
    count = push_face( box, count, a0, b0, c0, d0, px );
    count = push_face( box, count, a1, b1, c1, d1, nx );
    count = push_face( box, count, a2, b2, c2, d2, py );
    count = push_face( box, count, a3, b3, c3, d3, ny );
    count = push_face( box, count, a4, b4, c4, d4, pz );
    count = push_face( box, count, a5, b5, c5, d5, nz );
    g_box = vertex_buffer( box, count );

    vertex_t plane[ 6 ]{};
    const float pa[ 3 ] = { n, n, 0 }; const float pb[ 3 ] = { p, n, 0 }; const float pc[ 3 ] = { p, p, 0 }; const float pd[ 3 ] = { n, p, 0 };
    push_face( plane, 0, pa, pb, pc, pd, pz );
    g_plane = vertex_buffer( plane, 6 );

    D3D11_DEPTH_STENCIL_DESC depth{};
    depth.DepthEnable = TRUE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth.DepthFunc = D3D11_COMPARISON_LESS;
    g_device( ).device( )->CreateDepthStencilState( &depth, &g_depth_on );
    depth.DepthEnable = FALSE;
    depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    g_device( ).device( )->CreateDepthStencilState( &depth, &g_depth_off );

    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    g_device( ).device( )->CreateRasterizerState( &raster, &g_raster );

    m_ready = g_box && g_plane && g_depth_on && g_depth_off && g_raster;
    return m_ready;
#else
    return false;
#endif
}

void c_scene::draw( )
{
    if ( !ensure( ) )
        return;

#if HYDRO_HOST_WINDOWS
    ID3D11DeviceContext* context = g_device( ).context( );
    context->RSSetState( g_raster );
    context->OMSetDepthStencilState( g_depth_on, 0 );
    g_shader( ).bind_mesh( );

    UINT stride = sizeof( vertex_t );
    UINT offset = 0;
    constants_t constants{};
    memcpy( constants.view, g_viewport( ).view( ), sizeof( constants.view ) );
    memcpy( constants.projection, g_viewport( ).projection( ), sizeof( constants.projection ) );
    g_light( ).direction( constants.light );

    for ( int index = 0; index < m_count; ++index )
    {
        c_node* node = m_nodes[ index ];
        math_pose( constants.world, node->m_position, node->m_yaw, node->m_scale );
        constants.color[ 0 ] = node->m_color[ 0 ];
        constants.color[ 1 ] = node->m_color[ 1 ];
        constants.color[ 2 ] = node->m_color[ 2 ];
        constants.color[ 3 ] = 1.0f;
        g_shader( ).constants( &constants, sizeof( constants ) );

        ID3D11Buffer* buffer = node->m_shape == shape_t::plane ? g_plane : g_box;
        context->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );
        context->Draw( node->m_shape == shape_t::plane ? 6u : 36u, 0 );
    }

    context->OMSetDepthStencilState( g_depth_off, 0 );
#endif
}
