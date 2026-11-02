#pragma once

#include <cstring>
#include <functional>
#include <vector>

#include <impl/engine.hxx>
#include <workspace/core/render/device.hxx>
#include <workspace/core/render/target.hxx>

class c_geometry
{
public:
    int geometry( const mesh_vertex_t* vertices, int vertex_count, const unsigned short* indices, int index_count );
    int texture( const unsigned char* rgba, int width, int height );
    int cube( const unsigned char* const* faces, int face );
    int place( int geometry, int texture, int index_start, int index_count, const float* world, const float* color, int alpha_test, const float* surface, int detail, float scroll, const float* bias );
    // Attach the material's authored shading constants to a placement already
    // made. Kept separate from place() so its twenty-odd call sites stay
    // untouched and only the materials that actually declare a cube pay for it.
    bool shade( int id, int cube, const float* specular, float specular_amount, float environment_amount, const float* diffuse = nullptr );
    void drop( int alpha_test );
    void lift( int alpha_test, float extra );
    bool move( int id, const float* world );
    bool show( int id, bool visible );
    bool layer( int id, int type, int shading, float diffuse_alpha, const float* box );
    void sorted( void ( *pass )( void*, const sorted_pass_t& ), void* user ) { m_sorted = pass; m_sorted_user = user; }
    mesh_vertex_t* edit( int geometry, int& count );
    void refresh( int geometry );
    void stream( int geometry );
    void revise( int texture, const unsigned char* rgba, int width, int height );
    void clock( float seconds );
    void fog( float start, float end, float red, float green, float blue );
    // The water surfaces' pass (hydro world/waterpass.hxx): runs inside draw( ), the scene bound, the transparent pass's blend and depth set up.
    void water_pass( void ( *pass )( void* ), void* user ) { m_water_pass = pass; m_water_user = user; }
    // The sort's layer 0xA reached: the water now (retail's order: below-water pfx, water, above-water pfx and alpha), not after the stage.
    void water_now( )
    {
        if ( m_water_pass != nullptr )
            m_water_pass( m_water_user );
    }
    void stage_pass( void ( *pass )( void*, int, int, bool ), void* user ) { m_stage_pass = pass; m_stage_user = user; }
    void* view_of( int texture ) const;
    void draw( int width, int height, bool reflection );
    void release( );

private:
    struct geometry_t
    {
        std::vector< mesh_vertex_t > vertices;
        std::vector< unsigned short > indices;
#if HYDRO_HOST_WINDOWS
        ID3D11Buffer* vertex_buffer = nullptr;
        ID3D11Buffer* index_buffer = nullptr;
        bool live = false;
#endif
    };

    struct texture_t
    {
        std::vector< unsigned char > rgba;
        int width = 0;
        int height = 0;
#if HYDRO_HOST_WINDOWS
        ID3D11ShaderResourceView* view = nullptr;
#endif
    };

    // An environment cube: six square BC1-decoded faces. The authored material
    // reflects one at EnvironmentAmount, 0.70 on the boat hull, and 64 of
    // Hydroelectric's materials declare one - so this is a scene-wide term,
    // not a boat detail.
    struct cube_t
    {
        std::vector< unsigned char > rgba[ 6 ];
        int face = 0;
#if HYDRO_HOST_WINDOWS
        ID3D11ShaderResourceView* view = nullptr;
#endif
    };

    struct placement_t
    {
        int geometry = -1;
        int texture = -1;
        int detail = -1;
        int index_start = 0;
        int index_count = 0;
        float world[ 16 ]{};
        float color[ 4 ]{ 1.0f, 1.0f, 1.0f, 1.0f };
        float surface[ 4 ]{ 2.0f, 1.0f, 15.0f, 12.0f };
        float scroll = 0.0f;
        float bias[ 2 ]{};
        float base_z = 0.0f;
        int alpha_test = 0;
        // Authored shading constants from the model's material table. env <= 0
        // means the material declares no reflection, which is the case for
        // foliage and ground; those keep the plain lit path.
        int cube = -1;
        float specular[ 3 ]{ 1.0f, 1.0f, 1.0f };
        // The material table's DiffuseColor. The retail pixel shader folds it
        // into the per-vertex diffuse term, so it tints every lit material -
        // 1,1,1 on the boat hull but 0.80/0.86/0.92 on Car_SUVa.
        float diffuse[ 3 ]{ 1.0f, 1.0f, 1.0f };
        float specular_amount = 0.0f;
        float environment_amount = 0.0f;
        // A stable handle for move(). An index into m_placements would not
        // do: drop() compacts the vector and shifts everything after the
        // hole, so any stored index would silently address the wrong item.
        int id = -1;
        // show(): a hidden placement is kept (its handle, matrix and state) but not submitted - the static model
        // component's LOD instances, one shown at a time by the world's draw selection.
        bool hidden = false;
        // layer( ): retail's sort type (-1: none set), the shading, DiffuseAlpha, the mesh's aabb.
        int layer = -1;
        int shading = 0;
        float diffuse_alpha = 1.0f;
        bool has_box = false;
        float box[ 8 ]{};
    };

    // The sort type of a placement the blend pass takes, or -1 for the opaque pass. Retail's own when the material gave one:
    // types past 4 blend or skip the depth write (the table 0x735A00). The old tags without a retail type keep their pass:
    // 7 the waterfall, type 11; 3 type 8; 10 the Blend shader, type 2.
    static int sorted_type( const placement_t& item )
    {
        if ( item.alpha_test == 2 || item.alpha_test == 5 || item.alpha_test == 6 || item.alpha_test == 11 )
            return -1;
        if ( item.layer >= 5 )
            return item.layer;
        if ( item.alpha_test == 7 )
            return 11;
        if ( item.alpha_test == 3 )
            return 8;
        if ( item.alpha_test == 10 )
            return item.layer >= 0 ? item.layer : 2;
        return -1;
    }

    bool ensure( );

    std::vector< geometry_t > m_geometries;
    std::vector< texture_t > m_textures;
    std::vector< cube_t > m_cubes;
    std::vector< placement_t > m_placements;
    int m_next_id = 0;
    int m_white = -1;
    float m_time = 0.0f;
    float m_fog_start = 0.0f;
    float m_fog_end = 900.0f;
    float m_fog_color[ 3 ] = { 156.0f / 255.0f, 196.0f / 255.0f, 215.0f / 255.0f };
    void ( *m_water_pass )( void* ) = nullptr;
    void* m_water_user = nullptr;
    void ( *m_stage_pass )( void*, int, int, bool ) = nullptr;
    void* m_stage_user = nullptr;
    bool m_ready = false;
    void ( *m_sorted )( void*, const sorted_pass_t& ) = nullptr;
    void* m_sorted_user = nullptr;
#if HYDRO_HOST_WINDOWS
    ID3D11BlendState* m_blend = nullptr;
    ID3D11DepthStencilState* m_depth = nullptr;
    ID3D11DepthStencilState* m_depth_sky = nullptr;
    ID3D11DepthStencilState* m_depth_fade = nullptr;
    ID3D11DepthStencilState* m_depth_spray = nullptr;
    ID3D11RasterizerState* m_hide_back = nullptr;
    ID3D11RasterizerState* m_show_both = nullptr;
    ID3D11Query* m_gpu_disjoint = nullptr;
    ID3D11Query* m_gpu_begin = nullptr;
    ID3D11Query* m_gpu_end = nullptr;
    bool m_gpu_open = false;
#endif
};

c_geometry& g_geometry( );

#include <workspace/utility/trace.hxx>
#include <workspace/core/render/shader.hxx>
#include <workspace/core/scene/viewport.hxx>
#include <workspace/core/scene/light.hxx>
#include <workspace/utility/math.hxx>
#include <vector>
#include <cstring>

namespace
{
    c_geometry g_geometry_instance{};

    struct textured_constants_t
    {
        float world[ 16 ];
        float view[ 16 ];
        float projection[ 16 ];
        float light[ 4 ];
        float color[ 4 ];
        float flags[ 4 ];
        float eye[ 4 ];
        float surface[ 4 ];
        float ambient[ 4 ];
        float front[ 4 ];
        float back[ 4 ];
        // x holds EnvironmentAmount.
        float material[ 4 ];
        // The whole specular product the retail vertex shader builds:
        // gDirLightSpecularColor * SpecularColor * SpecularAmount. Folded on
        // the CPU because all three are constant per draw.
        float specular[ 4 ];
    };

    // The HLSL cbuffer must agree with this struct byte for byte. A silent
    // mismatch here already cost a whole measurement once, when the constant
    // buffer truncated at 288 bytes and dropped every float4 past it without
    // erroring, so the layout is asserted rather than trusted.
    static_assert( sizeof( textured_constants_t ) == 352, "textured cbuffer size moved" );
    static_assert( offsetof( textured_constants_t, specular ) == 336, "specular moved" );
    static_assert( offsetof( textured_constants_t, ambient ) == 272, "ambient moved" );
    static_assert( offsetof( textured_constants_t, front ) == 288, "front moved" );
    static_assert( offsetof( textured_constants_t, back ) == 304, "back moved" );
    static_assert( offsetof( textured_constants_t, material ) == 320, "material moved" );
}

c_geometry& g_geometry( )
{
    return g_geometry_instance;
}

int c_geometry::geometry( const mesh_vertex_t* vertices, int vertex_count, const unsigned short* indices, int index_count )
{
    if ( vertices == nullptr || indices == nullptr || vertex_count <= 0 || index_count < 3 || vertex_count > 200000 || index_count > 700000 )
        return -1;

    geometry_t item{};
    item.vertices.assign( vertices, vertices + vertex_count );
    item.indices.assign( indices, indices + index_count );
    m_geometries.push_back( std::move( item ) );
    m_ready = false;
    return static_cast< int >( m_geometries.size( ) ) - 1;
}

int c_geometry::texture( const unsigned char* rgba, int width, int height )
{
    if ( rgba == nullptr || width <= 0 || height <= 0 || width > 8192 || height > 8192 )
        return -1;

    texture_t item{};
    item.width = width;
    item.height = height;
    item.rgba.assign( rgba, rgba + static_cast< std::size_t >( width ) * static_cast< std::size_t >( height ) * 4 );
    m_textures.push_back( std::move( item ) );
    m_ready = false;
    return static_cast< int >( m_textures.size( ) ) - 1;
}

int c_geometry::cube( const unsigned char* const* faces, int face )
{
    if ( faces == nullptr || face <= 0 || face > 2048 )
        return -1;
    cube_t item{};
    item.face = face;
    const std::size_t bytes = static_cast< std::size_t >( face ) * face * 4;
    for ( int index = 0; index < 6; ++index )
    {
        if ( faces[ index ] == nullptr )
            return -1;
        item.rgba[ index ].assign( faces[ index ], faces[ index ] + bytes );
    }
    m_cubes.push_back( std::move( item ) );
    m_ready = false;
    return static_cast< int >( m_cubes.size( ) ) - 1;
}

int c_geometry::place( int geometry, int texture, int index_start, int index_count, const float* world, const float* color, int alpha_test, const float* surface, int detail, float scroll, const float* bias )
{
    if ( geometry < 0 || geometry >= static_cast< int >( m_geometries.size( ) ) || world == nullptr || index_count < 3 )
        return -1;

    placement_t item{};
    item.id = m_next_id++;
    item.geometry = geometry;
    item.texture = texture;
    item.detail = detail;
    item.index_start = index_start;
    item.index_count = index_count;
    item.scroll = scroll;
    item.base_z = world[ 14 ];
    if ( bias != nullptr )
    {
        item.bias[ 0 ] = bias[ 0 ];
        item.bias[ 1 ] = bias[ 1 ];
    }
    std::memcpy( item.world, world, sizeof( item.world ) );
    if ( color != nullptr )
    {
        item.color[ 0 ] = color[ 0 ];
        item.color[ 1 ] = color[ 1 ];
        item.color[ 2 ] = color[ 2 ];
        if ( alpha_test == 11 || alpha_test == 14 || surface != nullptr )
            item.color[ 3 ] = color[ 3 ];
    }
    if ( surface != nullptr )
    {
        std::memcpy( item.surface, surface, sizeof( item.surface ) );
        item.alpha_test = alpha_test == 11 ? 11 : 2;
    }
    else
        item.alpha_test = alpha_test;
    m_placements.push_back( item );
    return item.id;
}

bool c_geometry::layer( int id, int type, int shading, float diffuse_alpha, const float* box )
{
    for ( placement_t& item : m_placements )
    {
        if ( item.id != id )
            continue;
        item.layer = type;
        item.shading = shading;
        item.diffuse_alpha = diffuse_alpha;
        item.has_box = box != nullptr;
        if ( box != nullptr )
            std::memcpy( item.box, box, sizeof( item.box ) );
        return true;
    }
    return false;
}

bool c_geometry::shade( int id, int cube, const float* specular, float specular_amount, float environment_amount, const float* diffuse )
{
    if ( id < 0 )
        return false;
    for ( placement_t& item : m_placements )
    {
        if ( item.id != id )
            continue;
        if ( diffuse != nullptr )
            std::memcpy( item.diffuse, diffuse, sizeof( item.diffuse ) );
        item.cube = cube;
        item.specular_amount = specular_amount;
        item.environment_amount = environment_amount;
        if ( specular != nullptr )
            std::memcpy( item.specular, specular, sizeof( item.specular ) );
        return true;
    }
    return false;
}

// Repoint an existing placement at a new world matrix. This is how a rigidly
// animated object moves: one matrix write instead of transforming every vertex
// on the CPU and re-uploading the buffer. Linear in the placement count, which
// is a few thousand, and only a handful of objects use it per frame.
bool c_geometry::move( int id, const float* world )
{
    if ( id < 0 || world == nullptr )
        return false;
    for ( placement_t& item : m_placements )
    {
        if ( item.id != id )
            continue;
        std::memcpy( item.world, world, sizeof( item.world ) );
        // base_z is what lift() restores from, so it has to track the matrix
        // or a later lift would snap the object back to its original height.
        item.base_z = world[ 14 ];
        return true;
    }
    return false;
}

bool c_geometry::show( int id, bool visible )
{
    if ( id < 0 )
        return false;
    for ( placement_t& item : m_placements )
    {
        if ( item.id != id )
            continue;
        item.hidden = !visible;
        return true;
    }
    return false;
}

void c_geometry::drop( int alpha_test )
{
    std::size_t kept = 0;
    for ( std::size_t index = 0; index < m_placements.size( ); ++index )
    {
        if ( m_placements[ index ].alpha_test == alpha_test )
            continue;
        if ( kept != index )
            m_placements[ kept ] = m_placements[ index ];
        ++kept;
    }
    m_placements.resize( kept );
}

void c_geometry::lift( int alpha_test, float extra )
{
    for ( placement_t& item : m_placements )
    {
        if ( item.alpha_test == alpha_test )
            item.world[ 14 ] = item.base_z + extra;
    }
}

mesh_vertex_t* c_geometry::edit( int geometry, int& count )
{
    if ( geometry < 0 || geometry >= static_cast< int >( m_geometries.size( ) ) )
    {
        count = 0;
        return nullptr;
    }
    geometry_t& item = m_geometries[ static_cast< std::size_t >( geometry ) ];
    count = static_cast< int >( item.vertices.size( ) );
    return item.vertices.empty( ) ? nullptr : item.vertices.data( );
}

void c_geometry::revise( int texture, const unsigned char* rgba, int width, int height )
{
    if ( texture < 0 || texture >= static_cast< int >( m_textures.size( ) ) || rgba == nullptr || width <= 0 || height <= 0 )
        return;
    texture_t& item = m_textures[ static_cast< std::size_t >( texture ) ];
    item.width = width;
    item.height = height;
    item.rgba.assign( rgba, rgba + static_cast< std::size_t >( width ) * static_cast< std::size_t >( height ) * 4 );
#if HYDRO_HOST_WINDOWS
    if ( item.view != nullptr )
    {
        item.view->Release( );
        item.view = nullptr;
    }
    m_ready = false;
#else
    ( void )texture;
#endif
}

void c_geometry::stream( int geometry )
{
    if ( geometry < 0 || geometry >= static_cast< int >( m_geometries.size( ) ) )
        return;
#if HYDRO_HOST_WINDOWS
    geometry_t& item = m_geometries[ static_cast< std::size_t >( geometry ) ];
    if ( item.vertices.empty( ) )
        return;
    ID3D11Device* device = g_device( ).device( );
    ID3D11DeviceContext* context = g_device( ).context( );
    const UINT bytes = static_cast< UINT >( sizeof( mesh_vertex_t ) * item.vertices.size( ) );
    if ( item.vertex_buffer == nullptr || !item.live )
    {
        if ( item.vertex_buffer != nullptr )
        {
            item.vertex_buffer->Release( );
            item.vertex_buffer = nullptr;
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = bytes;
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = item.vertices.data( );
        if ( FAILED( device->CreateBuffer( &description, &data, &item.vertex_buffer ) ) )
            return;
        item.live = true;
    }
    else
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if ( SUCCEEDED( context->Map( item.vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped ) ) )
        {
            std::memcpy( mapped.pData, item.vertices.data( ), bytes );
            context->Unmap( item.vertex_buffer, 0 );
        }
    }
    if ( item.index_buffer == nullptr && !item.indices.empty( ) )
    {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast< UINT >( sizeof( unsigned short ) * item.indices.size( ) );
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = item.indices.data( );
        device->CreateBuffer( &description, &data, &item.index_buffer );
    }
#else
    ( void )geometry;
#endif
}

void c_geometry::refresh( int geometry )
{
    if ( geometry < 0 || geometry >= static_cast< int >( m_geometries.size( ) ) )
        return;
#if HYDRO_HOST_WINDOWS
    geometry_t& item = m_geometries[ static_cast< std::size_t >( geometry ) ];
    if ( item.vertex_buffer != nullptr )
    {
        item.vertex_buffer->Release( );
        item.vertex_buffer = nullptr;
    }
    if ( item.index_buffer != nullptr )
    {
        item.index_buffer->Release( );
        item.index_buffer = nullptr;
    }
    m_ready = false;
#else
    ( void )geometry;
#endif
}

void c_geometry::clock( float seconds )
{
    m_time = seconds;
}

void c_geometry::fog( float start, float end, float red, float green, float blue )
{
    m_fog_start = start;
    m_fog_end = end;
    m_fog_color[ 0 ] = red;
    m_fog_color[ 1 ] = green;
    m_fog_color[ 2 ] = blue;
}

bool c_geometry::ensure( )
{
    if ( m_ready )
        return true;

#if HYDRO_HOST_WINDOWS
    ID3D11Device* device = g_device( ).device( );
    if ( m_white < 0 )
    {
        const unsigned char white[ 4 ] = { 255, 255, 255, 255 };
        m_white = texture( white, 1, 1 );
    }

    for ( geometry_t& item : m_geometries )
    {
        if ( item.vertex_buffer != nullptr )
            continue;

        D3D11_BUFFER_DESC vertices{};
        vertices.ByteWidth = static_cast< UINT >( sizeof( mesh_vertex_t ) * item.vertices.size( ) );
        vertices.Usage = D3D11_USAGE_IMMUTABLE;
        vertices.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertex_data{};
        vertex_data.pSysMem = item.vertices.data( );
        if ( FAILED( device->CreateBuffer( &vertices, &vertex_data, &item.vertex_buffer ) ) )
            return false;

        D3D11_BUFFER_DESC indices{};
        indices.ByteWidth = static_cast< UINT >( sizeof( unsigned short ) * item.indices.size( ) );
        indices.Usage = D3D11_USAGE_IMMUTABLE;
        indices.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA index_data{};
        index_data.pSysMem = item.indices.data( );
        if ( FAILED( device->CreateBuffer( &indices, &index_data, &item.index_buffer ) ) )
            return false;
    }

    for ( texture_t& item : m_textures )
    {
        if ( item.view != nullptr )
            continue;

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast< UINT >( item.width );
        description.Height = static_cast< UINT >( item.height );
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = item.rgba.data( );
        data.SysMemPitch = static_cast< UINT >( item.width * 4 );
        ID3D11Texture2D* texture = nullptr;
        if ( FAILED( device->CreateTexture2D( &description, &data, &texture ) ) )
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = description.Format;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        const HRESULT created = device->CreateShaderResourceView( texture, &view, &item.view );
        texture->Release( );
        ++trace::frame( ).textures;
        if ( FAILED( created ) )
            return false;
    }

    // Cube faces upload as a 6-slice texture array with the TEXTURECUBE
    // misc flag, one subresource per face in the order the asset stores them.
    for ( cube_t& item : m_cubes )
    {
        if ( item.view != nullptr )
            continue;

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast< UINT >( item.face );
        description.Height = static_cast< UINT >( item.face );
        description.MipLevels = 1;
        description.ArraySize = 6;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        D3D11_SUBRESOURCE_DATA data[ 6 ]{};
        for ( int face = 0; face < 6; ++face )
        {
            data[ face ].pSysMem = item.rgba[ face ].data( );
            data[ face ].SysMemPitch = static_cast< UINT >( item.face * 4 );
        }
        ID3D11Texture2D* texture = nullptr;
        if ( FAILED( device->CreateTexture2D( &description, data, &texture ) ) )
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = description.Format;
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        view.TextureCube.MipLevels = 1;
        const HRESULT created = device->CreateShaderResourceView( texture, &view, &item.view );
        texture->Release( );
        ++trace::frame( ).textures;
        if ( FAILED( created ) )
            return false;
    }

    if ( m_blend == nullptr )
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
        device->CreateBlendState( &blend, &m_blend );

        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D11_COMPARISON_LESS;
        device->CreateDepthStencilState( &depth, &m_depth );
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        device->CreateDepthStencilState( &depth, &m_depth_sky );
        depth.DepthFunc = D3D11_COMPARISON_LESS;
        device->CreateDepthStencilState( &depth, &m_depth_fade );
        depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        device->CreateDepthStencilState( &depth, &m_depth_spray );

        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.FrontCounterClockwise = FALSE;
        raster.DepthClipEnable = TRUE;
        device->CreateRasterizerState( &raster, &m_hide_back );
        raster.CullMode = D3D11_CULL_NONE;
        device->CreateRasterizerState( &raster, &m_show_both );
    }

    m_ready = true;
    return true;
#else
    return false;
#endif
}

void* c_geometry::view_of( int texture ) const
{
#if HYDRO_HOST_WINDOWS
    if ( texture >= 0 && texture < static_cast< int >( m_textures.size( ) ) )
        return m_textures[ static_cast< std::size_t >( texture ) ].view;
#endif
    return nullptr;
}

void c_geometry::draw( int width, int height, bool reflection )
{
    const double ensure_start = trace::now( );
    if ( ( m_placements.empty( ) && m_stage_pass == nullptr ) || !ensure( ) )   // the retail stage draws with no engine placement at all
        return;
    trace::frame( ).ensure += trace::now( ) - ensure_start;

#if HYDRO_HOST_WINDOWS
    ID3D11DeviceContext* context = g_device( ).context( );
    const double draw_start = trace::now( );
    if ( m_gpu_open )
    {
        UINT64 begin = 0;
        UINT64 end = 0;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
        if ( context->GetData( m_gpu_disjoint, &disjoint, sizeof( disjoint ), 0 ) == S_OK
            && context->GetData( m_gpu_begin, &begin, sizeof( begin ), 0 ) == S_OK
            && context->GetData( m_gpu_end, &end, sizeof( end ), 0 ) == S_OK
            && !disjoint.Disjoint
            && disjoint.Frequency > 0 )
            trace::frame( ).gpu = static_cast< double >( end - begin ) * 1000.0 / static_cast< double >( disjoint.Frequency );
        m_gpu_open = false;
    }
    if ( m_gpu_disjoint == nullptr )
    {
        D3D11_QUERY_DESC description{};
        description.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        g_device( ).device( )->CreateQuery( &description, &m_gpu_disjoint );
        description.Query = D3D11_QUERY_TIMESTAMP;
        g_device( ).device( )->CreateQuery( &description, &m_gpu_begin );
        g_device( ).device( )->CreateQuery( &description, &m_gpu_end );
    }
    if ( m_gpu_disjoint != nullptr )
        context->Begin( m_gpu_disjoint );
    if ( m_gpu_begin != nullptr )
        context->End( m_gpu_begin );
    // The retail stage (world/stage.hxx) draws everything: nothing is hand-placed any more (water batch 43).
    if ( m_stage_pass != nullptr )
    {
        m_stage_pass( m_stage_user, width, height, reflection );
        const FLOAT factor[ 4 ] = { 1, 1, 1, 1 };
        context->OMSetBlendState( nullptr, factor, 0xFFFFFFFF );
        context->RSSetState( nullptr );
        ID3D11ShaderResourceView* clear_stage[ 8 ] = { };
        context->PSSetShaderResources( 0, 8, clear_stage );
    }

    if ( m_gpu_end != nullptr )
        context->End( m_gpu_end );
    if ( m_gpu_disjoint != nullptr )
    {
        context->End( m_gpu_disjoint );
        m_gpu_open = true;
    }
    trace::frame( ).draw += trace::now( ) - draw_start;
    trace::frame( ).placements = static_cast< int >( m_placements.size( ) );
    ID3D11ShaderResourceView* unbound = nullptr;
    context->PSSetShaderResources( 0, 1, &unbound );
#endif
}

void c_geometry::release( )
{
#if HYDRO_HOST_WINDOWS
    for ( geometry_t& item : m_geometries )
    {
        if ( item.index_buffer ) item.index_buffer->Release( );
        if ( item.vertex_buffer ) item.vertex_buffer->Release( );
        item.index_buffer = nullptr;
        item.vertex_buffer = nullptr;
    }
    for ( texture_t& item : m_textures )
    {
        if ( item.view ) item.view->Release( );
        item.view = nullptr;
    }
    if ( m_blend ) m_blend->Release( );
    if ( m_depth ) m_depth->Release( );
    if ( m_depth_sky ) m_depth_sky->Release( );
    if ( m_depth_fade ) m_depth_fade->Release( );
    if ( m_depth_spray ) m_depth_spray->Release( );
    if ( m_hide_back ) m_hide_back->Release( );
    if ( m_show_both ) m_show_both->Release( );
    if ( m_gpu_disjoint ) m_gpu_disjoint->Release( );
    if ( m_gpu_begin ) m_gpu_begin->Release( );
    if ( m_gpu_end ) m_gpu_end->Release( );
    m_gpu_disjoint = nullptr;
    m_gpu_begin = nullptr;
    m_gpu_end = nullptr;
    m_gpu_open = false;
    m_blend = nullptr;
    m_depth = nullptr;
    m_depth_sky = nullptr;
    m_depth_fade = nullptr;
    m_depth_spray = nullptr;
    m_hide_back = nullptr;
    m_show_both = nullptr;
    m_ready = false;
#endif
}

int c_engine::geometry( const mesh_vertex_t* vertices, int vertex_count, const unsigned short* indices, int index_count )
{
    return g_geometry( ).geometry( vertices, vertex_count, indices, index_count );
}

int c_engine::cube( const unsigned char* const* faces, int face )
{
    return g_geometry( ).cube( faces, face );
}

bool c_engine::shade( int placement, int cube, const float* specular, float specular_amount, float environment_amount, const float* diffuse )
{
    return g_geometry( ).shade( placement, cube, specular, specular_amount, environment_amount, diffuse );
}

int c_engine::texture( const unsigned char* rgba, int width, int height )
{
    return g_geometry( ).texture( rgba, width, height );
}

void c_engine::api( void*& device, void*& context )
{
#if HYDRO_HOST_WINDOWS
    device = g_device( ).device( );
    context = g_device( ).context( );
#else
    device = context = nullptr;
#endif
}

void c_engine::sorted( void ( *pass )( void*, const sorted_pass_t& ), void* user )
{
    g_geometry( ).sorted( pass, user );
}

bool c_engine::layer( int placement, int type, int shading, float diffuse_alpha, const float* box )
{
    return g_geometry( ).layer( placement, type, shading, diffuse_alpha, box );
}

void c_engine::stage_pass( void ( *pass )( void*, int, int, bool ), void* user )
{
    g_geometry( ).stage_pass( pass, user );
}

// What 0x5409F0's null means here: the engine's colour and depth, its blit the only way to the swap chain.
void c_engine::chain( void*& view, void*& depth, int& width, int& height )
{
#if HYDRO_HOST_WINDOWS
    view = g_target( ).colour_target( );
    depth = g_target( ).depth_target( );
    width = g_target( ).width( );
    height = g_target( ).height( );
#endif
}

void c_engine::look_target( float* target )
{
    std::memcpy( target, g_viewport( ).target( ), 12 );
}

void c_engine::water_pass( void ( *pass )( void* ), void* user )
{
    g_geometry( ).water_pass( pass, user );
}

void c_engine::water_now( )
{
    g_geometry( ).water_now( );
}

void* c_engine::texture_view( int texture )
{
    return g_geometry( ).view_of( texture );
}

void c_engine::device( void*& device, void*& context, void*& mirror )
{
#if HYDRO_HOST_WINDOWS
    device = g_device( ).device( );
    context = g_device( ).context( );
    mirror = g_target( ).mirror_view( );
#endif
}

void c_engine::reflection_enabled( bool on )
{
    g_target( ).reflection_enabled = on;
}

void c_engine::reflection_plane( float z )
{
    g_target( ).reflection_plane = z;
}

// 0x5BC330's colour: the bytes of 0x7596E4 over 255 (0x6F75BC), r g b a.
void c_engine::clear_colour( const void* rgba )
{
    for ( int i = 0; i < 4; ++i )
        g_target( ).clear_colour[ i ] = static_cast< float >( static_cast< const unsigned char* >( rgba )[ i ] ) * 0.0039215689f;
}

void c_engine::matrices( float* view, float* projection, float* eye )
{
    std::memcpy( view, g_viewport( ).view( ), 64 );
    std::memcpy( projection, g_viewport( ).projection( ), 64 );
    std::memcpy( eye, g_viewport( ).eye( ), 12 );
}

int c_engine::place( int geometry, int texture, int index_start, int index_count, const float* world, const float* color, int alpha_test, const float* surface, int detail, float scroll, const float* bias )
{
    return g_geometry( ).place( geometry, texture, index_start, index_count, world, color, alpha_test, surface, detail, scroll, bias );
}

bool c_engine::move( int placement, const float* world )
{
    return g_geometry( ).move( placement, world );
}

bool c_engine::show( int placement, bool visible )
{
    return g_geometry( ).show( placement, visible );
}

void c_engine::drop( int alpha_test )
{
    g_geometry( ).drop( alpha_test );
}

void c_engine::lift( int alpha_test, float extra )
{
    g_geometry( ).lift( alpha_test, extra );
}

mesh_vertex_t* c_engine::edit( int geometry, int& count )
{
    return g_geometry( ).edit( geometry, count );
}

void c_engine::refresh( int geometry )
{
    g_geometry( ).refresh( geometry );
}

void c_engine::stream( int geometry )
{
    g_geometry( ).stream( geometry );
}

void c_engine::revise( int texture, const unsigned char* rgba, int width, int height )
{
    g_geometry( ).revise( texture, rgba, width, height );
}
