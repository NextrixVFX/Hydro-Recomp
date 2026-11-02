#pragma once

#include <workspace/core/boat/hull.hxx>
#include <workspace/core/world/drawer.hxx>
#include <workspace/core/world/primitive.hxx>

#include <cstdint>
#include <cstring>
#include <vector>

// VUSTATICMODELINSTANCE'S DRAW (render layer, platform independent), recreated 1:1 from the disassembly (water batch 31): draw 0x520130 and its two node walks 0x5215A0 (the opaque
// colour) / 0x521960, the visibility tests 0x535220 / 0x5354B0 / 0x535740 / 0x5A8580, the shadow 0x5201B0 / 0x520670, the prefetch 0x520200. The layout draw 0x5203B0 / 0x520820 and its
// VuGfxSceneInfo text 0x592120 .. 0x592510 are dev (STOPPED). What reaches the sort is the same 0x50-byte record per part; the records' draws are hooks named by retail address.
// The scene is held as plain structs of what these functions read, each field with its retail offset; a pointer word in a record is the object's own id here.
//   The instance: +4 a word the draw hands on, +8 the material map, +0x10 the scene, +0x14 the lod, +0x18 the colour (alpha +0x1B), +0x1C a byte, +0x1D the two-pass flag.

constexpr std::uint32_t k_gfx_cb_model = 0x521220u, k_gfx_cb_model_back = 0x5212B0u, k_gfx_cb_model_front = 0x521380u, k_gfx_cb_model_depth = 0x521430u, k_gfx_cb_model_shadow = 0x521490u;
constexpr float k_gfx_model_bias = 0.001f;   // 0x6F75B0, 0x3A83126F

// A part's material row (part +0xC).
struct gfx_scene_material_t
{
    std::uint32_t m_name = 0;         // +0xC what the map is asked
    std::uint8_t m_skip = 0;          // +0x1A
    std::uint8_t m_casts = 0;         // +0x1B
    std::uint8_t m_flag_1d = 0;       // +0x1D
    std::uint8_t m_by_depth = 0;      // +0x1E the opaque submit: set = 0x429010, clear = 0x50B7B0
    std::uint32_t m_type = 0;         // +0x20
    std::uint32_t m_material = 0;     // +0x24
    std::uint32_t m_depth_material = 0;   // +0x28
    std::uint32_t m_shadow_material = 0;  // +0x2C
};

struct gfx_scene_part_t
{
    std::uint32_t m_id = 0;                          // what a record holds
    const gfx_scene_material_t* m_row = nullptr;     // +0xC
    std::uint32_t m_chunk = 0;                       // [+0x10] +0x18
    float m_aabb[ 8 ] = { };                         // +0x30
};

struct gfx_scene_mesh_t
{
    std::vector< const gfx_scene_part_t* > m_parts;  // +0x24
    float m_min[ 4 ] = { }, m_max[ 4 ] = { };        // +0x2C / +0x3C
};

struct gfx_scene_node_t
{
    float m_matrix[ 16 ] = { };                      // +0x24
    const gfx_scene_mesh_t* m_mesh = nullptr;        // [+0x64] +0x10; a node without an instance has none
    std::vector< const gfx_scene_node_t* > m_children;   // +0x68
    float m_aabb[ 8 ] = { };                         // +0x70
};

struct gfx_material_map_t
{
    virtual ~gfx_material_map_t( ) = default;
    virtual std::uint32_t map( std::uint32_t name ) = 0;     // vt+0
};

// 0x5A8580 (ecx the volume, centre, xmm2 the radius): 0x50C250 over the volume's own count of planes, in its own order. No planes: true.
inline bool gfx_volume_visible( const gfx_shadow_volume_t& v, const float c[ 3 ], float radius )
{
    std::uint32_t signs = 0;
    for ( int i = 0; i < v.m_count; ++i )
    {
        const float* p = v.m_planes + 4 * i;
        const float d = ( ( p[ 0 ] * c[ 0 ] + p[ 1 ] * c[ 1 ] ) + c[ 2 ] * p[ 2 ] ) + p[ 3 ] + radius;
        std::uint32_t bits;
        std::memcpy( &bits, &d, 4 );
        signs |= bits & 0x80000000u;
    }
    return signs == 0;
}

// The first half of the three box tests: the box through the matrix, its centre and half diagonal; false when (lod x size)^2 > r^2 / d^2 (too small to see, NaN passes) or when wholly
// behind the plane.
inline bool gfx_box_near( const float eye[ 3 ], float lod, const float clip[ 4 ], const float a[ 8 ], const float m[ 16 ], float size, float c[ 3 ], float& radius_out )
{
    const float x0 = ( ( a[ 0 ] * m[ 0 ] + a[ 1 ] * m[ 4 ] ) + a[ 2 ] * m[ 8 ] ) + m[ 12 ];
    const float y0 = ( ( a[ 0 ] * m[ 1 ] + a[ 1 ] * m[ 5 ] ) + a[ 2 ] * m[ 9 ] ) + m[ 13 ];
    const float z0 = ( ( a[ 0 ] * m[ 2 ] + a[ 1 ] * m[ 6 ] ) + a[ 2 ] * m[ 10 ] ) + m[ 14 ];
    const float x1 = ( ( a[ 4 ] * m[ 0 ] + a[ 5 ] * m[ 4 ] ) + a[ 6 ] * m[ 8 ] ) + m[ 12 ];
    const float y1 = ( ( a[ 4 ] * m[ 1 ] + a[ 5 ] * m[ 5 ] ) + a[ 6 ] * m[ 9 ] ) + m[ 13 ];
    const float z1 = ( ( a[ 4 ] * m[ 2 ] + a[ 5 ] * m[ 6 ] ) + a[ 6 ] * m[ 10 ] ) + m[ 14 ];
    c[ 0 ] = ( x1 + x0 ) * 0.5f;
    c[ 1 ] = ( y1 + y0 ) * 0.5f;
    c[ 2 ] = ( z1 + z0 ) * 0.5f;
    const float dy = y1 - y0, dx = x1 - x0, dz = z1 - z0;
    const float radius = hull_sqrt( ( dy * dy + dx * dx ) + dz * dz ) * 0.5f;
    radius_out = radius;
    const float ex = eye[ 0 ] - c[ 0 ], ey = eye[ 1 ] - c[ 1 ], ez = eye[ 2 ] - c[ 2 ];
    const float seen = lod * size;
    if ( seen * seen > ( radius * radius ) / ( ( ey * ey + ex * ex ) + ez * ez ) )
        return false;
    const float side = ( ( clip[ 1 ] * c[ 1 ] + c[ 0 ] * clip[ 0 ] ) + clip[ 2 ] * c[ 2 ] ) + clip[ 3 ] + radius;
    return !( 0.0f >= side );
}

// 0x535220 (ecx the params, aabb, matrix, xmm3 the size): then the camera's six planes.
inline bool gfx_box_visible( const gfx_draw_params_t& p, const float a[ 8 ], const float m[ 16 ], float size )
{
    float c[ 3 ], radius;
    return gfx_box_near( p.m_eye, p.m_lod, p.m_clip, a, m, size, c, radius ) && gfx_sphere_visible( p.m_camera->m_planes, c, radius );
}

// 0x5354B0 (ecx the params, aabb, matrix, xmm3 the size): the same, then the light's volume.
inline bool gfx_box_in_light( const gfx_shadow_params_t& p, const float a[ 8 ], const float m[ 16 ], float size )
{
    float c[ 3 ], radius;
    return gfx_box_near( p.m_eye, p.m_lod, p.m_clip, a, m, size, c, radius ) && gfx_volume_visible( *p.m_volume, c, radius );
}

// 0x535740 (ecx the params, the cascade, aabb, matrix, size): the same, then that cascade's volume.
inline bool gfx_box_in_cascade( const gfx_shadow_params_t& p, int cascade, const float a[ 8 ], const float m[ 16 ], float size )
{
    float c[ 3 ], radius;
    return gfx_box_near( p.m_eye, p.m_lod, p.m_clip, a, m, size, c, radius ) && gfx_volume_visible( p.m_cascade[ cascade ].m_volume, c, radius );
}

// The sort depth of a mesh's centre under the matrix: its distance from the camera over the far plane; minss with 1 second, so NaN gives 1.
inline float gfx_mesh_depth( const gfx_scene_mesh_t& mesh, const float m[ 16 ], const gfx_camera_t& camera )
{
    const float cx = ( mesh.m_min[ 0 ] + mesh.m_max[ 0 ] ) * 0.5f, cy = ( mesh.m_min[ 1 ] + mesh.m_max[ 1 ] ) * 0.5f, cz = ( mesh.m_min[ 2 ] + mesh.m_max[ 2 ] ) * 0.5f;
    const float wx = ( ( cy * m[ 4 ] + cx * m[ 0 ] ) + cz * m[ 8 ] ) + m[ 12 ] - camera.m_eye[ 0 ];
    const float wy = ( ( cy * m[ 5 ] + cx * m[ 1 ] ) + cz * m[ 9 ] ) + m[ 13 ] - camera.m_eye[ 1 ];
    const float wz = ( ( cy * m[ 6 ] + cx * m[ 2 ] ) + cz * m[ 10 ] ) + m[ 14 ] - camera.m_eye[ 2 ];
    const float scaled = hull_sqrt( ( wy * wy + wx * wx ) + wz * wz ) / camera.m_far;
    return scaled < 1.0f ? scaled : 1.0f;
}

struct gfx_static_model_t
{
    // 0x520130 (matrix, params): nothing without a scene.
    void draw( gfx_sort_t& sort, const float matrix[ 16 ], const gfx_draw_params_t& p ) const
    {
        if ( !m_scene )
            return;
        const bool opaque = ( m_colour >> 24 ) == 0xFFu;
        for ( const gfx_scene_node_t* root : *m_scene )
            draw_node( sort, *root, matrix, p, opaque );
    }

    // 0x5201B0 (matrix, shadow params).
    void draw_shadow( gfx_sort_t& sort, const float matrix[ 16 ], const gfx_shadow_params_t& p ) const
    {
        if ( !m_scene )
            return;
        for ( const gfx_scene_node_t* root : *m_scene )
            shadow_node( sort, *root, matrix, p );
    }

    // 0x520670 (node, parent matrix, params): per casting part, per cascade whose volume sees it, a 0x44-byte record (the part's matrix x the cascade's, the part) sorted with the
    // screen field = cascade + 1, left set.
    void shadow_node( gfx_sort_t& sort, const gfx_scene_node_t& node, const float parent[ 16 ], const gfx_shadow_params_t& p ) const
    {
        if ( !gfx_box_in_light( p, node.m_aabb, parent, m_lod ) )
            return;
        float m[ 16 ];
        hull_multiply( node.m_matrix, m, parent );
        if ( node.m_mesh )
        {
            for ( const gfx_scene_part_t* pp : node.m_mesh->m_parts )
            {
                if ( !pp->m_row->m_casts )
                    continue;
                for ( int i = 0; i < p.m_cascades; ++i )
                {
                    if ( !gfx_box_in_cascade( p, i, pp->m_aabb, m, m_lod ) )
                        continue;
                    std::uint8_t* r = sort.allocate( 0x44 );
                    float product[ 16 ];
                    hull_multiply( m, product, p.m_cascade[ i ].m_matrix );
                    std::memcpy( r, product, 64 );
                    std::memcpy( r + 0x40, &pp->m_id, 4 );
                    sort.set_screen( static_cast< std::uint32_t >( i ) + 1u );
                    sort.submit_keyed( 2, pp->m_row->m_shadow_material, pp->m_chunk, k_gfx_cb_model_shadow, 0.0f );
                }
            }
        }
        for ( const gfx_scene_node_t* child : node.m_children )
            shadow_node( sort, *child, m, p );
    }

    // 0x520200: every mesh of the scene (+0x18), each part not skipped, under the identity 0x7593B0 at depth 0.5.
    void prefetch( gfx_sort_t& sort ) const
    {
        static constexpr float k_identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        if ( !m_meshes )
            return;
        for ( const gfx_scene_mesh_t* mesh : *m_meshes )
        {
            for ( const gfx_scene_part_t* pp : mesh->m_parts )
            {
                const gfx_scene_material_t& row = *pp->m_row;
                if ( row.m_skip )
                    continue;
                std::uint32_t material = row.m_material;
                if ( m_map )
                    material = m_map->map( row.m_name );
                record( sort, k_identity, *pp );
                sort.submit_keyed( 2, material, pp->m_chunk, k_gfx_cb_model, 0.5f );
            }
        }
    }

    // The two parts of one record's use in 0x5215A0 and the first branch of 0x521960.
    void submit_opaque( gfx_sort_t& sort, const gfx_scene_part_t& part, std::uint32_t type, std::uint32_t material, float depth ) const
    {
        if ( part.m_row->m_by_depth )
            sort.submit( type, material, part.m_chunk, k_gfx_cb_model, depth );
        else
            sort.submit_keyed( type, material, part.m_chunk, k_gfx_cb_model, depth );
    }

    std::uint8_t* record( gfx_sort_t& sort, const float m[ 16 ], const gfx_scene_part_t& part ) const
    {
        std::uint8_t* r = sort.allocate( 0x50 );
        std::memcpy( r, m, 64 );
        std::memcpy( r + 0x40, &m_colour, 4 );
        std::memcpy( r + 0x44, &m_word, 4 );
        std::memcpy( r + 0x48, &part.m_id, 4 );
        r[ 0x4C ] = m_byte_1c;
        return r;
    }

    // 0x5215A0 (opaque) / 0x521960 (node, parent matrix, params; identical but for the blended branch).
    void draw_node( gfx_sort_t& sort, const gfx_scene_node_t& node, const float parent[ 16 ], const gfx_draw_params_t& p, bool opaque ) const
    {
        if ( !gfx_box_visible( p, node.m_aabb, parent, m_lod ) )
            return;
        float m[ 16 ];
        hull_multiply( node.m_matrix, m, parent );
        if ( node.m_mesh )
        {
            const gfx_scene_mesh_t& mesh = *node.m_mesh;
            const float depth = gfx_mesh_depth( mesh, m, *p.m_camera );
            for ( const gfx_scene_part_t* pp : mesh.m_parts )
            {
                const gfx_scene_part_t& part = *pp;
                const gfx_scene_material_t& row = *part.m_row;
                if ( row.m_skip || !gfx_box_visible( p, part.m_aabb, m, m_lod ) )
                    continue;
                std::uint32_t material = row.m_material;
                if ( m_map )
                    material = m_map->map( row.m_name );
                const std::uint32_t type = row.m_type;
                if ( !opaque && static_cast< std::int32_t >( type ) <= 3 )
                {
                    record( sort, m, part );
                    if ( m_two_pass )
                    {
                        sort.submit( 11, material, part.m_chunk, k_gfx_cb_model_back, depth + k_gfx_model_bias );
                        sort.submit( 11, material, part.m_chunk, k_gfx_cb_model_front, depth );
                    }
                    else
                        sort.submit( 11, material, part.m_chunk, k_gfx_cb_model, depth );
                    continue;
                }
                record( sort, m, part );
                submit_opaque( sort, part, type, material, depth );
                if ( type != 1 )
                    continue;
                // The depth pre-pass: screen 6, again screen 5 under params +0x2C and the row's +0x1D, then the screen field put back.
                const std::uint32_t screen = ( sort.m_key_hi >> 18 ) & 0xFu;
                std::uint8_t* r = sort.allocate( 0x44 );
                std::memcpy( r, m, 64 );
                std::memcpy( r + 0x40, &part.m_id, 4 );
                sort.set_screen( 6 );
                sort.submit_keyed( 2, row.m_depth_material, part.m_chunk, k_gfx_cb_model_depth, depth );
                if ( p.m_second_depth && row.m_flag_1d )
                {
                    sort.set_screen( 5 );
                    sort.submit_keyed( 2, row.m_depth_material, part.m_chunk, k_gfx_cb_model_depth, depth );
                }
                sort.set_screen( screen );
            }
        }
        for ( const gfx_scene_node_t* child : node.m_children )
            draw_node( sort, *child, m, p, opaque );
    }

    std::uint32_t m_word = 0;                 // +4
    gfx_material_map_t* m_map = nullptr;      // +8
    const std::vector< const gfx_scene_node_t* >* m_scene = nullptr;   // [+0x10] +0x44
    const std::vector< const gfx_scene_mesh_t* >* m_meshes = nullptr;  // [+0x10] +0x18
    float m_lod = 1.0f;                       // +0x14
    std::uint32_t m_colour = 0;               // +0x18
    std::uint8_t m_byte_1c = 0;               // +0x1C
    std::uint8_t m_two_pass = 0;              // +0x1D
};

constexpr std::uint32_t k_gfx_cb_skin = 0x533C90u, k_gfx_cb_skin_back = 0x533D40u, k_gfx_cb_skin_front = 0x533E30u, k_gfx_cb_skin_depth = 0x533F00u, k_gfx_cb_skin_shadow = 0x533F80u;

// VUANIMATEDMODELINSTANCE'S DRAW (water batch 31): 0x532AD0, shadow 0x533060, prefetch 0x5333A0. No culling of its own: every mesh, every part. Instance: +4 a word, +8 the material
// map, [+0x10] +0xC the model's word, +0x14 the meshes, +0x1C + 4 x [+0x28] the current bone buffer (world/skeleton.hxx), +0x90 the colour (alpha +0x93), +0x94 a byte, +0x95 two-pass.
struct gfx_animated_model_t
{
    std::uint8_t* record( gfx_sort_t& sort, const float m[ 16 ], const gfx_scene_part_t& part ) const
    {
        std::uint8_t* r = sort.allocate( 0x58 );
        std::memcpy( r, m, 64 );
        std::memcpy( r + 0x40, &m_colour, 4 );
        std::memcpy( r + 0x44, &m_word, 4 );
        std::memcpy( r + 0x48, &part.m_id, 4 );
        std::memcpy( r + 0x4C, &m_model_word, 4 );
        std::memcpy( r + 0x50, &m_bones, 4 );
        r[ 0x54 ] = m_byte;
        return r;
    }

    std::uint8_t* short_record( gfx_sort_t& sort, const float m[ 16 ], const gfx_scene_part_t& part ) const
    {
        std::uint8_t* r = sort.allocate( 0x4C );
        std::memcpy( r, m, 64 );
        std::memcpy( r + 0x40, &part.m_id, 4 );
        std::memcpy( r + 0x44, &m_model_word, 4 );
        std::memcpy( r + 0x48, &m_bones, 4 );
        return r;
    }

    // 0x532AD0 (matrix, params). Unlike the static walk the blended path takes every type.
    void draw( gfx_sort_t& sort, const float m[ 16 ], const gfx_draw_params_t& p ) const
    {
        if ( !m_meshes )
            return;
        const bool opaque = ( m_colour >> 24 ) == 0xFFu;
        for ( const gfx_scene_mesh_t* mesh : *m_meshes )
        {
            const float depth = gfx_mesh_depth( *mesh, m, *p.m_camera );
            for ( const gfx_scene_part_t* pp : mesh->m_parts )
            {
                const gfx_scene_part_t& part = *pp;
                const gfx_scene_material_t& row = *part.m_row;
                if ( row.m_skip )
                    continue;
                std::uint32_t material = row.m_material;
                if ( m_map )
                    material = m_map->map( row.m_name );
                record( sort, m, part );
                if ( !opaque )
                {
                    if ( m_two_pass )
                    {
                        sort.submit( 11, material, part.m_chunk, k_gfx_cb_skin_back, depth + k_gfx_model_bias );
                        sort.submit( 11, material, part.m_chunk, k_gfx_cb_skin_front, depth );
                    }
                    else
                        sort.submit( 11, material, part.m_chunk, k_gfx_cb_skin, depth );
                    continue;
                }
                const std::uint32_t type = row.m_type;
                if ( row.m_by_depth )
                    sort.submit( type, material, part.m_chunk, k_gfx_cb_skin, depth );
                else
                    sort.submit_keyed( type, material, part.m_chunk, k_gfx_cb_skin, depth );
                if ( type != 1 )
                    continue;
                const std::uint32_t screen = ( sort.m_key_hi >> 18 ) & 0xFu;
                short_record( sort, m, part );
                sort.set_screen( 6 );
                sort.submit_keyed( 2, row.m_depth_material, part.m_chunk, k_gfx_cb_skin_depth, depth );
                if ( p.m_second_depth && row.m_flag_1d )
                {
                    sort.set_screen( 5 );
                    sort.submit_keyed( 2, row.m_depth_material, part.m_chunk, k_gfx_cb_skin_depth, depth );
                }
                sort.set_screen( screen );
            }
        }
    }

    // 0x533060 (matrix, shadow params): each casting part into each cascade; the screen field = cascade + 1, left set. Retail works out the mesh's depth here too and never uses it.
    void draw_shadow( gfx_sort_t& sort, const float m[ 16 ], const gfx_shadow_params_t& p ) const
    {
        if ( !m_meshes )
            return;
        for ( const gfx_scene_mesh_t* mesh : *m_meshes )
        {
            for ( const gfx_scene_part_t* pp : mesh->m_parts )
            {
                if ( !pp->m_row->m_casts )
                    continue;
                for ( int i = 0; i < p.m_cascades; ++i )
                {
                    float product[ 16 ];
                    hull_multiply( m, product, p.m_cascade[ i ].m_matrix );
                    short_record( sort, product, *pp );
                    sort.set_screen( static_cast< std::uint32_t >( i ) + 1u );
                    sort.submit_keyed( 2, pp->m_row->m_shadow_material, pp->m_chunk, k_gfx_cb_skin_shadow, 0.0f );
                }
            }
        }
    }

    // 0x5333A0: every mesh, each part not skipped, under the identity 0x7593B0 at depth 0.5.
    void prefetch( gfx_sort_t& sort ) const
    {
        static constexpr float k_identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        if ( !m_meshes )
            return;
        for ( const gfx_scene_mesh_t* mesh : *m_meshes )
        {
            for ( const gfx_scene_part_t* pp : mesh->m_parts )
            {
                const gfx_scene_material_t& row = *pp->m_row;
                if ( row.m_skip )
                    continue;
                std::uint32_t material = row.m_material;
                if ( m_map )
                    material = m_map->map( row.m_name );
                record( sort, k_identity, *pp );
                sort.submit_keyed( 2, material, pp->m_chunk, k_gfx_cb_skin, 0.5f );
            }
        }
    }

    std::uint32_t m_word = 0;                 // +4
    gfx_material_map_t* m_map = nullptr;      // +8
    std::uint32_t m_model_word = 0;           // [+0x10] +0xC
    const std::vector< const gfx_scene_mesh_t* >* m_meshes = nullptr;  // [+0x14] +0x18
    std::uint32_t m_bones = 0;                // +0x1C + 4 x [+0x28]
    std::uint32_t m_colour = 0;               // +0x90
    std::uint8_t m_byte = 0;                  // +0x94
    std::uint8_t m_two_pass = 0;              // +0x95
};
