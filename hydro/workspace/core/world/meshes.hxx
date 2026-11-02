#pragma once

#include <workspace/core/assets/scene.hxx>
#include <workspace/core/entity/draw.hxx>
#include <workspace/core/world/instance.hxx>

#include <cstdint>
#include <vector>

// THE LOADED SCENE AS THE DRAW WALKS READ IT (water batch 32): assets/scene.hxx holds what the load read, world/instance.hxx what the walks read; this links the second to the first, the
// links 0x5C1560 makes (part +0xC = shaders[ +0x14 ], +0x10 = chunks[ +0x18 ]) and 0x5C2660 / 0x5C24D0 (node +0x64 +0x10 = meshes[ index ], the +0x68 list).
// Verified whole against retail's loaded scene, offset by offset: case gfx_real_model.
// The ids are ours, never retail words: a shader's three pipeline states (+0x24 .. +0x2C, device objects) are gfx_scene_state( shader, slot ), a chunk's binding (+0x18) its index + 1, a
// part 1 + its place in meshes order. The device side turns them into objects (hooks).
// HOOK: g_vugfxcomposer 0x7590BC's vt+0x10, the renderer setting that turns a shader mode of 2 or 3 into 1 at load, is not taken (assets/scene.hxx).
// The view points into the scene and the blob: both must outlive it. Holds pointers into itself: never copy it.
inline std::uint32_t gfx_scene_state( std::uint32_t shader, std::uint32_t slot )
{
    return 3u * shader + slot + 1u;
}

struct gfx_scene_view_t
{
    gfx_scene_view_t( ) = default;
    gfx_scene_view_t( const gfx_scene_view_t& ) = delete;
    gfx_scene_view_t& operator=( const gfx_scene_view_t& ) = delete;

    std::vector< gfx_scene_material_t > m_rows;
    std::vector< gfx_scene_chunk_t > m_chunks;
    std::vector< gfx_scene_part_t > m_parts;
    std::vector< gfx_scene_mesh_t > m_mesh_rows;
    std::vector< gfx_scene_node_t > m_nodes;
    std::vector< const gfx_scene_mesh_t* > m_meshes;     // +0x18
    std::vector< const gfx_scene_node_t* > m_roots;      // +0x44; empty in an animated scene
};

inline void gfx_scene_view_build( gfx_scene_view_t& v, const scene_t& scene, const std::uint8_t* blob )
{
    v.m_rows.resize( scene.shaders.size( ) );
    for ( std::size_t i = 0; i < scene.shaders.size( ); ++i )
    {
        const scene_shader_t& s = scene.shaders[ i ];
        gfx_scene_material_t& r = v.m_rows[ i ];
        r.m_name = s.index;
        r.m_skip = s.bytes[ 1 ];       // +0x1A
        r.m_casts = s.bytes[ 2 ];      // +0x1B
        r.m_flag_1d = s.bytes[ 4 ];    // +0x1D
        r.m_by_depth = s.bytes[ 5 ];   // +0x1E
        r.m_type = s.mode;
        const std::uint32_t shader = static_cast< std::uint32_t >( i );
        r.m_material = gfx_scene_state( shader, 0 );
        r.m_depth_material = gfx_scene_state( shader, 1 );
        r.m_shadow_material = gfx_scene_state( shader, 2 );
    }
    v.m_chunks.resize( scene.chunks.size( ) );
    for ( std::size_t i = 0; i < scene.chunks.size( ); ++i )
    {
        const scene_chunk_t& c = scene.chunks[ i ];
        v.m_chunks[ i ].m_stride = c.stride;
        v.m_chunks[ i ].m_vertices = blob + c.vertices.offset;
        v.m_chunks[ i ].m_indices = blob + c.index_bytes.offset;
    }
    std::size_t parts = 0;
    for ( const scene_mesh_t& mesh : scene.meshes )
        parts += mesh.parts.size( );
    v.m_parts.resize( parts );
    v.m_mesh_rows.resize( scene.meshes.size( ) );
    parts = 0;
    for ( std::size_t i = 0; i < scene.meshes.size( ); ++i )
    {
        const scene_mesh_t& mesh = scene.meshes[ i ];
        gfx_scene_mesh_t& row = v.m_mesh_rows[ i ];
        std::memcpy( row.m_min, mesh.aabb, 16 );
        std::memcpy( row.m_max, mesh.aabb + 4, 16 );
        for ( const scene_part_t& part : mesh.parts )
        {
            gfx_scene_part_t& p = v.m_parts[ parts++ ];
            p.m_id = static_cast< std::uint32_t >( parts );
            p.m_row = &v.m_rows[ part.shader ];
            p.m_chunk = part.chunk + 1u;
            p.m_arrays = &v.m_chunks[ part.chunk ];
            p.m_first_index = part.first_index;
            p.m_triangles = static_cast< std::int32_t >( part.triangles );
            std::memcpy( p.m_aabb, part.aabb, 32 );
            row.m_parts.push_back( &p );
        }
        v.m_meshes.push_back( &row );
    }
    v.m_nodes.resize( scene.nodes.size( ) );
    for ( std::size_t i = 0; i < scene.nodes.size( ); ++i )
    {
        const scene_node_t& node = scene.nodes[ i ];
        gfx_scene_node_t& n = v.m_nodes[ i ];
        std::memcpy( n.m_matrix, node.matrix, 64 );
        std::memcpy( n.m_aabb, node.aabb, 32 );
        n.m_mesh = node.mesh >= 0 ? &v.m_mesh_rows[ static_cast< std::size_t >( node.mesh ) ] : nullptr;
        for ( int child : node.children )
            n.m_children.push_back( &v.m_nodes[ static_cast< std::size_t >( child ) ] );
    }
    for ( int root : scene.roots )
        v.m_roots.push_back( &v.m_nodes[ static_cast< std::size_t >( root ) ] );
}

// VuGfxDrawInfoParams -> VuGfxDrawParams, the local 0x5B0600 and 0x61DAE0 build: the camera's eye (four words), the camera, everything else zero.
inline gfx_draw_params_t gfx_info_params( const gfx_camera_t& camera, float eye_w )
{
    gfx_draw_params_t p;
    p.m_eye[ 0 ] = camera.m_eye[ 0 ];
    p.m_eye[ 1 ] = camera.m_eye[ 1 ];
    p.m_eye[ 2 ] = camera.m_eye[ 2 ];
    p.m_eye[ 3 ] = eye_w;
    p.m_camera = &camera;
    return p;
}

// VU3DDRAWSTATICMODELCOMPONENT'S REST (water batch 32): its four instances (+0xBC .. +0x11C, 0x20 each), +0x13C the matrix, +0x24 the world aabb, +0x17C the fade.
struct gfx_static_component_t
{
    const model_draw_t* m_draw = nullptr;
    gfx_static_model_t m_lods[ 4 ];
    float m_min[ 4 ] = { }, m_max[ 4 ] = { };
    float m_matrix[ 16 ] = { };
    float m_fade = 1.0f;

    // 0x5B0730: the choice is entity/draw.hxx's; then 0x520130.
    void draw( gfx_sort_t& sort, const gfx_draw_params_t& p )
    {
        const static_draw_t s = static_model_select( *m_draw, m_min, m_max, p.m_eye, p.m_reflecting != 0, p.m_skip_models != 0, m_fade );
        put( s, sort, p );
    }

    // 0x5B0600 (info params: the camera, force).
    void draw_info( gfx_sort_t& sort, const gfx_camera_t& camera, float eye_w, bool force )
    {
        const static_draw_t s = static_model_select_info( *m_draw, m_min, m_max, camera.m_eye, force );
        const gfx_draw_params_t p = gfx_info_params( camera, eye_w );
        put( s, sort, p );
    }

    // 0x5B08D0.
    void draw_shadow( gfx_sort_t& sort, const gfx_shadow_params_t& p )
    {
        const int i = static_model_select_shadow( *m_draw, m_min, m_max, p.m_eye, p.m_clip_flag != 0, p.m_skip_models != 0 );
        if ( i < 0 )
            return;
        m_lods[ i ].m_lod = m_draw->rejection_scale_modifier;
        m_lods[ i ].draw_shadow( sort, m_matrix, p );
    }

    // 0x5B09B0.
    void prefetch( gfx_sort_t& sort ) const
    {
        for ( const gfx_static_model_t& lod : m_lods )
            lod.prefetch( sort );
    }

private:
    void put( const static_draw_t& s, gfx_sort_t& sort, const gfx_draw_params_t& p )
    {
        if ( s.instance < 0 )
            return;
        gfx_static_model_t& lod = m_lods[ s.instance ];
        lod.m_lod = s.rejection;
        lod.m_colour = s.color;
        lod.m_byte_1c = s.dynamic_lighting;
        lod.draw( sort, m_matrix, p );
    }
};

// VU3DDRAWANIMATEDMODELCOMPONENT'S REST (water batch 32): its instance (+0x68), +0x104 the matrix, +0x24 the world aabb. Draw 0x61DB60 is entity/draw.hxx's choice, then 0x532AD0.
struct gfx_animated_component_t
{
    const model_draw_t* m_draw = nullptr;
    gfx_animated_model_t m_model;
    float m_min[ 4 ] = { }, m_max[ 4 ] = { };
    float m_matrix[ 16 ] = { };

    // 0x61DAE0 (info params): always, no distance; the camera's eye.
    void draw_info( gfx_sort_t& sort, const gfx_camera_t& camera, float eye_w )
    {
        m_model.m_colour = m_draw->color;
        m_model.m_byte = m_draw->dynamic_lighting;
        const gfx_draw_params_t p = gfx_info_params( camera, eye_w );
        m_model.draw( sort, m_matrix, p );
    }

    // 0x61DC00.
    void draw_shadow( gfx_sort_t& sort, const gfx_shadow_params_t& p ) const
    {
        if ( animated_model_casts( *m_draw, m_min, m_max, p.m_eye ) )
            m_model.draw_shadow( sort, m_matrix, p );
    }
};

// VU3DDRAWBREAKABLEMODELCOMPONENT'S DRAW 0x543790 (water batch 32): the instance is at +0x70, a static model drawing pieces. Inside the draw distance the alpha of the colour (+0xEC, its byte
// +0xEF) is overwritten IN THE COMPONENT by minss( life +0xE8 / fade time +0x6C, 1 ) x 255 (0x6F7808), fistp, low byte; then the instance takes it, +0x60 and +0x64.
struct gfx_breakable_component_t
{
    std::uint32_t m_colour = 0;       // +0xEC
    float m_draw_distance = 0.0f;     // +0x5C
    float m_rejection = 0.0f;         // +0x60
    std::uint8_t m_lighting = 0;      // +0x64
    float m_fade_time = 0.0f;         // +0x6C
    float m_life = 0.0f;              // +0xE8
    float m_min[ 4 ] = { }, m_max[ 4 ] = { };     // +0x24 / +0x34
    gfx_static_model_t m_model;       // +0x70
    const gfx_scene_piece_t* m_pieces = nullptr;
    int m_count = 0;

    // The test and the writes; true: 0x51F540 follows.
    bool prepare( const float eye[ 3 ] )
    {
        const float dx = ( m_max[ 0 ] + m_min[ 0 ] ) * 0.5f - eye[ 0 ], dy = ( m_max[ 1 ] + m_min[ 1 ] ) * 0.5f - eye[ 1 ], dz = ( m_max[ 2 ] + m_min[ 2 ] ) * 0.5f - eye[ 2 ];
        if ( !( m_draw_distance * m_draw_distance > ( dy * dy + dx * dx ) + dz * dz ) )
            return false;
        const float t = m_life / m_fade_time;
        const float a = ( t < 1.0f ? t : 1.0f ) * 255.0f;     // minss( t, 1 ): 1 on NaN
        const std::uint32_t byte = static_cast< std::uint32_t >( _mm_cvtss_si32( _mm_set_ss( a ) ) ) & 0xFFu;   // fistp in the default rounding, the same answer for every float
        m_colour = ( m_colour & 0x00FFFFFFu ) | ( byte << 24 );
        m_model.m_colour = m_colour;
        m_model.m_lod = m_rejection;
        m_model.m_byte_1c = m_lighting;
        return true;
    }

    void draw( gfx_sort_t& sort, const gfx_draw_params_t& p )
    {
        if ( prepare( p.m_eye ) )
            m_model.draw_pieces( sort, m_pieces, m_count, p );
    }
};

// A model instance over a view: what 0x520130's instance holds at +0x10.
inline void gfx_scene_view_attach( gfx_static_model_t& model, const gfx_scene_view_t& v )
{
    model.m_scene = &v.m_roots;
    model.m_meshes = &v.m_meshes;
}

inline void gfx_scene_view_attach( gfx_animated_model_t& model, const gfx_scene_view_t& v )
{
    model.m_meshes = &v.m_meshes;
}
