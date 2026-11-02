#pragma once

// VuPfx's two renderers on the sort (water batch 40): the quad renderer's submit 0x58BD60 and the trail renderer's 0x58D030, CPU side, the patterns' slot 2 (0x5920B0 / 0x58F9C0) reach them while the pattern
// holds particles. The vertex fills are pfxclass.hxx's (0x58C340, 0x58D3F0); the device half of those two callbacks and the renderers' programs (0x58BAC0, 0x58CCC0, 0x58CE40) are world/stage.hxx's.
// A record's retail pointers: +0 the renderer is 0 here, the node is the runtime heap's address; the particles' own two list words are copied with them, as retail's 0x5C memcpy does.

#include <workspace/core/world/drawer.hxx>
#include <workspace/core/world/gfxsort.hxx>
#include <workspace/core/world/pfxclass.hxx>
#include <workspace/core/world/pfxsim.hxx>

#include <emmintrin.h>
#include <xmmintrin.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

constexpr std::uint32_t k_gfx_cb_pfx_quad = 0x58BA20u, k_gfx_cb_pfx_trail = 0x58CE30u;

// The box centre's distance from the eye over the far plane, at most 1: the key's depth (both submits, operand order 0x58BD74).
inline float pfx_box_depth( pfx_heap_t& h, std::uint32_t pattern, const gfx_camera_t& camera )
{
    const float y = ( h.f( pattern + 0x30 ) + h.f( pattern + 0x40 ) ) * 0.5f - camera.m_eye[ 1 ];
    const float x = ( h.f( pattern + 0x2C ) + h.f( pattern + 0x3C ) ) * 0.5f - camera.m_eye[ 0 ];
    const float z = ( h.f( pattern + 0x34 ) + h.f( pattern + 0x44 ) ) * 0.5f - camera.m_eye[ 2 ];
    const float d = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( ( y * y + x * x ) + z * z ) ) ) / camera.m_far;
    return _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( d ), _mm_set_ss( 1.0f ) ) );
}

// The system matrix (+0x1C) when the pattern's Space +0x34 is 1, else the identity 0x7593B0.
inline void pfx_record_matrix( pfx_heap_t& h, std::uint32_t pattern, std::uint32_t node, std::uint32_t* out )
{
    static const std::uint32_t identity[ 16 ] = { 0x3F800000u, 0, 0, 0, 0, 0x3F800000u, 0, 0, 0, 0, 0x3F800000u, 0, 0, 0, 0, 0x3F800000u };
    if ( h[ node + 0x34 ] == 1u )
        std::memcpy( out, h.find( h[ pattern + 0x0C ] + 0x1C ), 64 );
    else
        std::memcpy( out, identity, 64 );
}

// Additive (word 0): the key's bits and the layer in the high word, no depth. Modulate: 0x429010's depth sorted key.
inline void pfx_command( gfx_sort_t& sort, std::uint32_t blend, std::uint32_t layer, std::uint32_t material, std::uint32_t callback, float depth )
{
    if ( blend != 0u )
    {
        sort.submit( layer, material, 0u, callback, depth );
        return;
    }
    const std::uint32_t hi = ( ( ( layer << 11 ) | sort.m_hooks.material( material )->m_key ) << 3 ) | sort.m_key_hi | 0x2000u;
    sort.command( sort.m_key_lo, hi, callback, material, 0u, layer );
}

// 0x58BD60 ( camera, pattern ): `materials` = the eight programs' materials, `tile` = the node's Tile Texture asset exists. Each particle past Near Fade Min keeps its 0x5C bytes with alpha x min( fade, 1 );
// the block is cut to those kept (none: no command). The program index: bit 0 Fog +0x94, bit 1 the tile, bit 2 Clip Threshold +0x74 > 0. At most 0x800 draw.
inline bool pfx_quad_submit( gfx_sort_t& sort, pfx_heap_t& h, std::uint32_t pattern, const gfx_camera_t& camera, const std::uint32_t* materials, bool tile )
{
    const std::uint32_t node = h[ pattern + 0x10 ], system = h[ pattern + 0x0C ];
    const std::uint32_t layer = pfx_sort_layer( h[ node + 0x6C ], h[ node + 0x70 ] );
    const float depth = pfx_box_depth( h, pattern, camera );
    const float near_min = h.f( node + 0x78 );
    const float fade_scale = 1.0f / ( h.f( node + 0x7C ) - near_min );
    std::uint32_t matrix[ 16 ];
    pfx_record_matrix( h, pattern, node, matrix );
    float m[ 16 ];
    std::memcpy( m, matrix, 64 );
    std::uint8_t* record = sort.allocate( static_cast< std::int32_t >( h[ pattern + 0x28 ] ) * 0x5C + 0x84 );
    std::uint8_t* out = record + 0x84;
    std::int32_t kept = 0;
    for ( std::uint32_t p = h[ pattern + 0x20 ]; p != 0u; p = h[ p ] )
    {
        const float x = h.f( p + 8 ), y = h.f( p + 0xC ), z = h.f( p + 0x10 );
        const float dx = ( ( m[ 0 ] * x + m[ 4 ] * y ) + z * m[ 8 ] ) + m[ 12 ] - camera.m_eye[ 0 ];
        const float dy = ( ( m[ 1 ] * x + m[ 5 ] * y ) + z * m[ 9 ] ) + m[ 13 ] - camera.m_eye[ 1 ];
        const float dz = ( ( m[ 2 ] * x + m[ 6 ] * y ) + m[ 10 ] * z ) + m[ 14 ] - camera.m_eye[ 2 ];
        const float along = ( ( camera.m_direction[ 0 ] * dx + camera.m_direction[ 1 ] * dy ) + camera.m_direction[ 2 ] * dz );
        const float fade = ( along - near_min ) * fade_scale;
        if ( !( fade > 0.0f ) )
            continue;
        std::memcpy( out, h.find( p ), 0x5C );
        float alpha;
        std::memcpy( &alpha, out + 0x34, 4 );
        alpha *= _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( fade ), _mm_set_ss( 1.0f ) ) );
        std::memcpy( out + 0x34, &alpha, 4 );
        out += 0x5C;
        ++kept;
    }
    ui_gfx_sort_t::buffer_t& buffer = sort.m_buffers[ sort.m_index ];
    if ( kept <= 0 )
    {
        buffer.m_used = sort.m_offset;
        return false;
    }
    buffer.m_used = sort.m_offset + 0x84 + kept * 0x5C;
    const float clip = h.f( node + 0x74 );
    const std::uint32_t shader = static_cast< std::uint32_t >( h[ node + 0x94 ] & 0xFFu ) + 2u * ( ( tile ? 1u : 0u ) + 2u * ( clip > 0.0f ? 1u : 0u ) );
    const std::uint32_t none = 0u;
    std::memcpy( record, &none, 4 );
    std::memcpy( record + 4, &shader, 4 );
    std::memcpy( record + 8, &node, 4 );
    std::memcpy( record + 0xC, matrix, 64 );
    std::memcpy( record + 0x4C, h.find( pattern + 0x2C ), 32 );
    const std::int32_t drawn = ( std::min )( kept, 0x800 );
    std::memcpy( record + 0x6C, &drawn, 4 );
    std::memcpy( record + 0x70, h.find( system + 0xA8 ), 20 );
    pfx_command( sort, h[ node + 0x6C ], layer, materials[ shader ], k_gfx_cb_pfx_quad, depth );
    return true;
}

// 0x58D030 ( camera, pattern ): nothing below two particles. Each 0x2C bytes: the position, +0x44's 16 bytes, +0x54, the colour (tint x colour x 255, fistp, low bytes), the scale x 0.5. At most 0x1555 draw.
inline bool pfx_trail_submit( gfx_sort_t& sort, pfx_heap_t& h, std::uint32_t pattern, const gfx_camera_t& camera, std::uint32_t material )
{
    const std::int32_t count = static_cast< std::int32_t >( h[ pattern + 0x28 ] );
    if ( count < 2 )
        return false;
    const std::uint32_t node = h[ pattern + 0x10 ], system = h[ pattern + 0x0C ];
    const std::uint32_t layer = pfx_sort_layer( h[ node + 0x84 ], h[ node + 0x88 ] );
    const float depth = pfx_box_depth( h, pattern, camera );
    std::uint8_t* record = sort.allocate( count * 0x2C + 0x6C );
    std::uint8_t* out = record + 0x6C;
    const float tint[ 4 ] = { h.f( system + 0xAC ), h.f( system + 0xB0 ), h.f( system + 0xB4 ), h.f( system + 0xB8 ) };
    for ( std::uint32_t p = h[ pattern + 0x20 ]; p != 0u; p = h[ p ], out += 0x2C )
    {
        std::memcpy( out, h.find( p + 8 ), 16 );
        std::memcpy( out + 0x10, h.find( p + 0x44 ), 16 );
        std::memcpy( out + 0x20, h.find( p + 0x54 ), 4 );
        for ( std::uint32_t k = 0; k < 4u; ++k )
        {
            const float v = ( tint[ k ] * h.f( p + 0x28 + 4u * k ) ) * 255.0f;
            out[ 0x24 + k ] = static_cast< std::uint8_t >( _mm_cvtsd_si32( _mm_set_sd( static_cast< double >( v ) ) ) );
        }
        const float half = h.f( p + 0x38 ) * 0.5f;
        std::memcpy( out + 0x28, &half, 4 );
    }
    const std::uint32_t none = 0u;
    std::memcpy( record, &none, 4 );
    std::memcpy( record + 4, &node, 4 );
    std::uint32_t matrix[ 16 ];
    pfx_record_matrix( h, pattern, node, matrix );
    std::memcpy( record + 8, matrix, 64 );
    std::memcpy( record + 0x48, h.find( pattern + 0x2C ), 32 );
    const std::int32_t drawn = ( std::min )( count, 0x1555 );
    std::memcpy( record + 0x68, &drawn, 4 );
    pfx_command( sort, h[ node + 0x84 ], layer, material, k_gfx_cb_pfx_trail, depth );
    return true;
}
