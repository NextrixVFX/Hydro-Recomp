#pragma once

#include <workspace/core/world/drawer.hxx>
#include <workspace/core/world/gfxsort.hxx>

#include <emmintrin.h>
#include <xmmintrin.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <list>
#include <vector>

// VUFOLIAGEMANAGER and VUFOLIAGEENTITY, the CPU side recreated 1:1 from the disassembly (boat batch 31, case foliage). The
// user's visible item 4: Hydroelectric places 115 VuFoliageEntity, each one a camera-facing quad; retail batches the quads
// per texture and fog flag. g_vufoliagemanager 0x7590DC (make 0x5BB150, vtable 0x751E58): two lists, [0] fog off, [1] on
// (+4 the shader asset, +8 the material, +0xC the bucket list, +0x10 its size; stride 0x10). A bucket VuBucket (0x20, ctor
// 0x5BB1F0, vtable 0x751E74): +4 the reference count, +0xC the texture, +0x10 the fog byte, +0x14 / +0x18 / +0x1C the quads.
//
// HOOKS / NOT HERE: slot 5 0x5BB3A0 (READ, device: the assets "Foliage/Simple" and "Foliage/Fog", the vertex declaration
// { 0xC0000 1 6 0 } { 0x140000 7 2 0 } plus the first element made by 0x428C60, 0x540E00, one material per asset through
// 0x53A440: the caller hands the two material ids to foliage_manager_t), slot 3 0x5BB6E0 its release (0x541BE0, device), the
// command's draw 0x5BBDD0 (texture bound on vt+0xB4, then the indexed 3 / 2 draw of count x 4 vertices, device; the WATER
// agent's side), the draw-component registrations ([0x7590B8] vt+0x28 / +0x2C / +0x30), the layout draw 0x5BB870 + entity
// 0x5E70C0 (the Vu3dLayoutComponent path: READ; layout = the tool's preview, no race caller was traced, NOT recreated).
// A record's float operands stay in retail's order; no NaN reaches a case here (the transform and the camera are finite).
// Wired: world/stage.hxx makes the materials and draws the command, c_world's frame walk calls draw and flush.

constexpr std::uint32_t k_gfx_cb_foliage = 0x5BBDD0u;

// The 0x34 byte quad the bucket holds: +0 the position (4 words, as the transform holds them), +0x10 half width, +0x14 height,
// +0x18 the colour (RGBA bytes), +0x1C u0, +0x20 v0, +0x24 u1, +0x28 v1, +0x2C / +0x30 the quad's horizontal direction.
struct foliage_quad_t
{
    std::uint32_t m_position[ 4 ] = { };
    float m_width = 0.0f;
    float m_height = 0.0f;
    std::uint32_t m_color = 0;
    std::uint32_t m_uv[ 4 ] = { };
    float m_direction[ 2 ] = { };
};

struct foliage_bucket_t
{
    int m_references = 1;
    std::uint32_t m_texture = 0;
    std::uint8_t m_fog = 0;
    std::vector< foliage_quad_t > m_quads;
};

// 0x5BBE30: the four vertices (0x18 each: x y z u v colour); the colour's bytes scaled by `tint` and rounded (fistp), alpha kept.
inline void foliage_vertices( const foliage_quad_t& q, const float tint[ 3 ], std::uint8_t* out )
{
    const auto f = []( const std::uint32_t& w ) {
        float v;
        std::memcpy( &v, &w, 4 );
        return v;
    };
    const auto scale = []( std::uint32_t byte, float t ) {
        const float v = static_cast< float >( static_cast< std::int32_t >( byte ) ) * t;
        return static_cast< std::uint32_t >( _mm_cvtss_si32( _mm_set_ss( v ) ) ) & 0xFFu;
    };
    const std::uint32_t color = scale( q.m_color & 0xFFu, tint[ 0 ] ) | ( scale( ( q.m_color >> 8 ) & 0xFFu, tint[ 1 ] ) << 8 ) |
                                ( scale( ( q.m_color >> 16 ) & 0xFFu, tint[ 2 ] ) << 16 ) | ( q.m_color & 0xFF000000u );
    const float x = f( q.m_position[ 0 ] ), y = f( q.m_position[ 1 ] );
    const float dx = q.m_direction[ 0 ] * q.m_width;
    const float dy = q.m_direction[ 1 ] * q.m_width;
    const float top = q.m_height * 2.0f + f( q.m_position[ 2 ] );   // 0x6F76F8
    const auto put = [ & ]( int vertex, float px, float py, std::uint32_t z, std::uint32_t u, std::uint32_t v ) {
        std::uint8_t* p = out + vertex * 0x18;
        std::memcpy( p, &px, 4 );
        std::memcpy( p + 4, &py, 4 );
        std::memcpy( p + 8, &z, 4 );
        std::memcpy( p + 12, &u, 4 );
        std::memcpy( p + 16, &v, 4 );
        std::memcpy( p + 20, &color, 4 );
    };
    std::uint32_t top_word;
    std::memcpy( &top_word, &top, 4 );
    put( 0, x - dx, y - dy, q.m_position[ 2 ], q.m_uv[ 0 ], q.m_uv[ 1 ] );
    put( 1, dx + x, dy + y, q.m_position[ 2 ], q.m_uv[ 2 ], q.m_uv[ 1 ] );
    put( 2, dx + x, dy + y, top_word, q.m_uv[ 2 ], q.m_uv[ 3 ] );
    put( 3, x - dx, y - dy, top_word, q.m_uv[ 0 ], q.m_uv[ 3 ] );
}

struct foliage_manager_t
{
    std::list< foliage_bucket_t > m_lists[ 2 ];
    std::uint32_t m_material[ 2 ] = { };

    // 0x5BB7B0 ( texture, fog ): the list's first bucket of this texture, one more reference; else a new one on the list's back.
    foliage_bucket_t* acquire( std::uint32_t texture, std::uint8_t fog )
    {
        std::list< foliage_bucket_t >& list = m_lists[ fog ];
        for ( foliage_bucket_t& b : list )
        {
            if ( b.m_texture == texture )
            {
                ++b.m_references;
                return &b;
            }
        }
        list.emplace_back( );
        list.back( ).m_texture = texture;
        list.back( ).m_fog = fog;
        return &list.back( );
    }

    // 0x5E7420's body (and 0x5E7050's inlined copy): the last reference goes and the bucket leaves its list (0x4A44A0).
    void release( foliage_bucket_t* bucket )
    {
        if ( --bucket->m_references != 0 )
            return;
        std::list< foliage_bucket_t >& list = m_lists[ bucket->m_fog ];
        for ( auto it = list.begin( ); it != list.end( ); ++it )
        {
            if ( &*it == bucket )
            {
                list.erase( it );
                return;
            }
        }
    }

    // 0x5BBAA0 ( rgb bytes ), the frame's flush from 0x558AC0 / 0x558C00: for each list, each bucket holding quads: the block
    // { texture, count, count x 4 vertices } in the sort's memory, one command (callback 0x5BBDD0, type 4), the bucket emptied.
    // Retail's shrink at the end needs a negative capacity: dead.
    void flush( gfx_sort_t& sort, const std::uint8_t color[ 3 ] )
    {
        const std::uint32_t reciprocal = 0x3B808081u;   // 0x6F75BC, 1 / 255
        float k;
        std::memcpy( &k, &reciprocal, 4 );
        const float tint[ 3 ] = { static_cast< float >( static_cast< std::int32_t >( color[ 0 ] ) ) * k,
                                  static_cast< float >( static_cast< std::int32_t >( color[ 1 ] ) ) * k,
                                  static_cast< float >( static_cast< std::int32_t >( color[ 2 ] ) ) * k };
        for ( int l = 0; l < 2; ++l )
        {
            for ( foliage_bucket_t& b : m_lists[ l ] )
            {
                const std::int32_t count = static_cast< std::int32_t >( b.m_quads.size( ) );
                if ( count == 0 )
                    continue;
                std::uint8_t* block = sort.allocate( count * 0x60 + 8 );
                std::memcpy( block, &b.m_texture, 4 );
                std::memcpy( block + 4, &count, 4 );
                std::uint8_t* out = block + 8;
                for ( const foliage_quad_t& q : b.m_quads )
                {
                    foliage_vertices( q, tint, out );
                    out += 0x60;
                }
                const std::uint32_t hi = ( sort.m_hooks.material( m_material[ l ] )->m_key << 3 ) | sort.m_key_hi | 0x12000u;
                sort.command( sort.m_key_lo, hi, k_gfx_cb_foliage, m_material[ l ], 0u, 4u );
                b.m_quads.clear( );
            }
        }
    }
};

// 0x5BB9C0 ( bucket, params, camera ): the quad into the bucket, its direction square to the view on the plane.
inline void foliage_add( foliage_bucket_t& bucket, const foliage_quad_t& quad, const float camera[ 3 ] )
{
    float px, py;
    std::memcpy( &px, &quad.m_position[ 0 ], 4 );
    std::memcpy( &py, &quad.m_position[ 1 ], 4 );
    const std::uint32_t sign = 0x80000000u;
    float a = px - camera[ 0 ];
    std::uint32_t bits;
    std::memcpy( &bits, &a, 4 );
    bits ^= sign;
    std::memcpy( &a, &bits, 4 );
    const float b = py - camera[ 1 ];
    const float inverse = 1.0f / std::sqrt( a * a + b * b );
    foliage_quad_t q = quad;
    q.m_direction[ 0 ] = inverse * b;
    q.m_direction[ 1 ] = inverse * a;
    bucket.m_quads.push_back( q );
}

// VuFoliageEntity (0xA0, ctor 0x5E69A0, vtable 0x7556CC). Its properties, by the ctor: "Texture Asset" (VuAssetProperty on +0x64,
// the asset +0x98's +0x4C, the texture its +0x58), "Fog Enabled" +0x7C (default 0: the ctor's first word 0xFFFFFF00 puts the
// byte 0 there and 0xFF in the colour's first three), "Color" +0x7D, "U0" +0x84 (0), "V0" +0x88 (0), "U1" +0x8C (1), "V1" +0x90
// (1), "Draw Distance" +0x94 (0x7F7FFFFF). The transform: +0x80 the position, +0xB0 / +0xB8 the scale's x / z.
struct foliage_entity_t
{
    std::uint32_t m_position[ 4 ] = { };
    float m_scale_x = 0.0f;
    float m_scale_z = 0.0f;
    std::uint8_t m_fog = 0;
    std::uint32_t m_color = 0x00FFFFFFu;
    std::uint32_t m_uv[ 4 ] = { 0u, 0u, 0x3F800000u, 0x3F800000u };
    float m_draw_distance = 3.4028235e38f;
    std::uint32_t m_texture = 0;        // 0: the property holds no asset, no bucket, nothing is drawn
    foliage_bucket_t* m_bucket = nullptr;
    bool m_registered = false;          // the draw component's +0x1E
    std::uint8_t m_reflect = 0;         // the draw component's +0x1C, the draw manager's mask
    std::uint32_t m_aabb[ 8 ] = { };    // the draw component's +0x24 words, +0x30 / +0x40 are stack words in retail: left 0 here
    std::uint32_t m_layout[ 8 ] = { };  // the layout component's +0x18 words, +0x24 / +0x34 likewise

    // 0x5E72E0 (the transform's change method, ctor +0xC0): the draw aabb (x, y +- the width, z .. z + height x 2), the layout aabb.
    void transform_changed( )
    {
        float x, y, z;
        std::memcpy( &x, &m_position[ 0 ], 4 );
        std::memcpy( &y, &m_position[ 1 ], 4 );
        std::memcpy( &z, &m_position[ 2 ], 4 );
        const auto word = []( float v ) {
            std::uint32_t w;
            std::memcpy( &w, &v, 4 );
            return w;
        };
        m_aabb[ 0 ] = word( x - m_scale_x );
        m_aabb[ 1 ] = word( y - m_scale_x );
        m_aabb[ 2 ] = m_position[ 2 ];
        m_aabb[ 3 ] = 0u;
        m_aabb[ 4 ] = word( m_scale_x + x );
        m_aabb[ 5 ] = word( y + m_scale_x );
        m_aabb[ 6 ] = word( m_scale_z * 2.0f + z );
        m_aabb[ 7 ] = 0u;
        m_layout[ 0 ] = 0xBF800000u;
        m_layout[ 1 ] = word( m_scale_x ) ^ 0x80000000u;
        m_layout[ 2 ] = 0u;
        m_layout[ 3 ] = 0u;
        m_layout[ 4 ] = 0x3F800000u;
        m_layout[ 5 ] = word( m_scale_x );
        m_layout[ 6 ] = 0x40000000u;
        m_layout[ 7 ] = 0u;
    }

    // Slot 6 0x5E7000 (gameInitialize): registered once; the bucket of the texture, when there is one.
    void game_initialize( foliage_manager_t& manager )
    {
        m_registered = true;
        if ( m_texture != 0 )
            m_bucket = manager.acquire( m_texture, m_fog );
    }

    // Slot 7 0x5E7050 (gameRelease).
    void game_release( foliage_manager_t& manager )
    {
        m_registered = false;
        if ( m_bucket != nullptr )
        {
            manager.release( m_bucket );
            m_bucket = nullptr;
        }
    }

    // Slots 2 / 3 0x5E6FB0: the change above; `live` = the +0x14 flags' bit 2 (the entity is in the game): the bucket afresh.
    void changed( foliage_manager_t& manager, bool live )
    {
        transform_changed( );
        if ( !live )
            return;
        if ( m_bucket != nullptr )
        {
            manager.release( m_bucket );
            m_bucket = nullptr;
        }
        if ( m_texture != 0 )
            m_bucket = manager.acquire( m_texture, m_fog );
    }

    // 0x5E71E0 (the draw component's method, VuGfxDrawParams): past the draw distance (ordered, NaN draws) or with no bucket:
    // nothing. Else the quad.
    void draw( const float eye[ 3 ], const float camera[ 3 ] )
    {
        float x, y, z;
        std::memcpy( &x, &m_position[ 0 ], 4 );
        std::memcpy( &y, &m_position[ 1 ], 4 );
        std::memcpy( &z, &m_position[ 2 ], 4 );
        const float ex = x - eye[ 0 ], ey = y - eye[ 1 ], ez = z - eye[ 2 ];
        const float d2 = ( ey * ey + ex * ex ) + ez * ez;
        const float limit = m_draw_distance * m_draw_distance;
        if ( _mm_comigt_ss( _mm_set_ss( d2 ), _mm_set_ss( limit ) ) != 0 || m_bucket == nullptr )
            return;
        foliage_quad_t q;
        std::memcpy( q.m_position, m_position, 16 );
        q.m_width = m_scale_x;
        q.m_height = m_scale_z;
        q.m_color = m_color;
        std::memcpy( q.m_uv, m_uv, 16 );
        foliage_add( *m_bucket, q, camera );
    }
};
