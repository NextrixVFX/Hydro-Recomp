#pragma once

#include <workspace/core/boat/hull.hxx>
#include <workspace/core/world/drawer.hxx>
#include <workspace/core/world/primitive.hxx>

#include <cstdint>
#include <cstring>
#include <vector>

// VUSTATICMODELINSTANCE'S DRAW (render layer, platform independent), recreated 1:1 from the disassembly (water batch 31): draw 0x520130 and its two node walks 0x5215A0 (the opaque
// colour) / 0x521960, the visibility test 0x535220. What reaches the sort is the same 0x50-byte record per part; the records' draws are hooks named by retail address.
// The scene is held as plain structs of what these functions read, each field with its retail offset; a pointer word in a record is the object's own id here.
//   The instance: +4 a word the draw hands on, +8 the material map, +0x10 the scene, +0x14 the lod, +0x18 the colour (alpha +0x1B), +0x1C a byte, +0x1D the two-pass flag.

constexpr std::uint32_t k_gfx_cb_model = 0x521220u, k_gfx_cb_model_back = 0x5212B0u, k_gfx_cb_model_front = 0x521380u, k_gfx_cb_model_depth = 0x521430u;
constexpr float k_gfx_model_bias = 0.001f;   // 0x6F75B0, 0x3A83126F

// A part's material row (part +0xC).
struct gfx_scene_material_t
{
    std::uint32_t m_name = 0;         // +0xC what the map is asked
    std::uint8_t m_skip = 0;          // +0x1A
    std::uint8_t m_flag_1d = 0;       // +0x1D
    std::uint8_t m_by_depth = 0;      // +0x1E the opaque submit: set = 0x429010, clear = 0x50B7B0
    std::uint32_t m_type = 0;         // +0x20
    std::uint32_t m_material = 0;     // +0x24
    std::uint32_t m_depth_material = 0;   // +0x28
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

// 0x535220 (ecx the params, aabb, matrix, xmm3 the size): the box through the matrix, its centre and half diagonal; false when (lod x size)^2 > r^2 / d^2 (too small to see, NaN
// passes), when wholly behind the clip plane, else the camera's sphere test.
inline bool gfx_box_visible( const gfx_draw_params_t& p, const float a[ 8 ], const float m[ 16 ], float size )
{
    const float x0 = ( ( a[ 0 ] * m[ 0 ] + a[ 1 ] * m[ 4 ] ) + a[ 2 ] * m[ 8 ] ) + m[ 12 ];
    const float y0 = ( ( a[ 0 ] * m[ 1 ] + a[ 1 ] * m[ 5 ] ) + a[ 2 ] * m[ 9 ] ) + m[ 13 ];
    const float z0 = ( ( a[ 0 ] * m[ 2 ] + a[ 1 ] * m[ 6 ] ) + a[ 2 ] * m[ 10 ] ) + m[ 14 ];
    const float x1 = ( ( a[ 4 ] * m[ 0 ] + a[ 5 ] * m[ 4 ] ) + a[ 6 ] * m[ 8 ] ) + m[ 12 ];
    const float y1 = ( ( a[ 4 ] * m[ 1 ] + a[ 5 ] * m[ 5 ] ) + a[ 6 ] * m[ 9 ] ) + m[ 13 ];
    const float z1 = ( ( a[ 4 ] * m[ 2 ] + a[ 5 ] * m[ 6 ] ) + a[ 6 ] * m[ 10 ] ) + m[ 14 ];
    const float c[ 3 ] = { ( x1 + x0 ) * 0.5f, ( y1 + y0 ) * 0.5f, ( z1 + z0 ) * 0.5f };
    const float dy = y1 - y0, dx = x1 - x0, dz = z1 - z0;
    const float radius = hull_sqrt( ( dy * dy + dx * dx ) + dz * dz ) * 0.5f;
    const float ex = p.m_eye[ 0 ] - c[ 0 ], ey = p.m_eye[ 1 ] - c[ 1 ], ez = p.m_eye[ 2 ] - c[ 2 ];
    const float seen = p.m_lod * size;
    if ( seen * seen > ( radius * radius ) / ( ( ey * ey + ex * ex ) + ez * ez ) )
        return false;
    const float side = ( ( p.m_clip[ 1 ] * c[ 1 ] + c[ 0 ] * p.m_clip[ 0 ] ) + p.m_clip[ 2 ] * c[ 2 ] ) + p.m_clip[ 3 ] + radius;
    if ( 0.0f >= side )
        return false;
    return gfx_sphere_visible( p.m_camera->m_planes, c, radius );
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
            const float cx = ( mesh.m_min[ 0 ] + mesh.m_max[ 0 ] ) * 0.5f, cy = ( mesh.m_min[ 1 ] + mesh.m_max[ 1 ] ) * 0.5f, cz = ( mesh.m_min[ 2 ] + mesh.m_max[ 2 ] ) * 0.5f;
            const float wx = ( ( cy * m[ 4 ] + cx * m[ 0 ] ) + cz * m[ 8 ] ) + m[ 12 ] - p.m_camera->m_eye[ 0 ];
            const float wy = ( ( cy * m[ 5 ] + cx * m[ 1 ] ) + cz * m[ 9 ] ) + m[ 13 ] - p.m_camera->m_eye[ 1 ];
            const float wz = ( ( cy * m[ 6 ] + cx * m[ 2 ] ) + cz * m[ 10 ] ) + m[ 14 ] - p.m_camera->m_eye[ 2 ];
            const float scaled = hull_sqrt( ( wy * wy + wx * wx ) + wz * wz ) / p.m_camera->m_far;
            const float depth = scaled < 1.0f ? scaled : 1.0f;   // minss with 1 second: NaN gives 1
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
    float m_lod = 1.0f;                       // +0x14
    std::uint32_t m_colour = 0;               // +0x18
    std::uint8_t m_byte_1c = 0;               // +0x1C
    std::uint8_t m_two_pass = 0;              // +0x1D
};
