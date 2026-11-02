#pragma once

#include <workspace/core/assets/scene.hxx>
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
