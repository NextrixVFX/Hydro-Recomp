#pragma once

#include <workspace/core/ui/image.hxx>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// VuScreenFadeEntity (ctor 0x43E5D0, vtable 0x73B2A4; "Color" +0x5C, default 0xFF000000) and its two authored kinds: a volume in the level
// that tints the whole viewport while the camera is inside it. Levels/Area51 authors two boxes, Levels/Pack01Track01 two ellipsoids.
//   VuScreenFadeEllipsoidEntity (ctor 0x43E8F0, vtable 0x73B26C): "Inner Ratio %" +0x64
//   VuScreenFadeBoxEntity       (ctor 0x43ECE0, vtable 0x73B234): "Inner Ratio X % / Y % / Z %" +0x64 / +0x68 / +0x6C
// Slot 6 0x43E6B0 registers the tick 0x43E710 with the 3D draw manager g 0x759024 (vt+0x1C), slot 7 0x43E700 removes it: the world's.
struct ui_screen_fade_t
{
    std::uint8_t m_colour[ 4 ] = { 0u, 0u, 0u, 0xFFu };  // +0x5C
    bool m_box = false;
    float m_inner[ 3 ] = { 0.5f, 0.5f, 0.5f };           // +0x64 ..
    float m_axis[ 3 ][ 3 ] = { };                        // the transform component (+0x50): rows +0x50, +0x60, +0x70
    float m_position[ 3 ] = { };                         // +0x80
    float m_scale[ 3 ] = { 1.0f, 1.0f, 1.0f };           // +0xB0
};

// A row against ( eye - position ), retail's operand order: y + x, then + z.
inline float ui_fade_dot( const float row[ 3 ], const float d[ 3 ] )
{
    float sum = row[ 1 ] * d[ 1 ];
    sum = sum + row[ 0 ] * d[ 0 ];
    return sum + row[ 2 ] * d[ 2 ];
}

// Slot 12 0x43EB50: the ellipsoid's weight at `eye`. RETAIL'S OWN: the third term divides the SECOND row's dot by the z scale; row 2 is never
// read. Outside the unit volume (sum >= 1: comiss, unordered goes on) 0; inside the inner ratio 1; else the ramp ( r - 1 ) / ( inner - 1 ).
inline float ui_fade_ellipsoid( const ui_screen_fade_t& f, const float eye[ 3 ] )
{
    const float d[ 3 ] = { eye[ 0 ] - f.m_position[ 0 ], eye[ 1 ] - f.m_position[ 1 ], eye[ 2 ] - f.m_position[ 2 ] };
    float x = ui_fade_dot( f.m_axis[ 0 ], d ) / f.m_scale[ 0 ];
    x = x * x;
    const float dot = ui_fade_dot( f.m_axis[ 1 ], d );
    float y = dot / f.m_scale[ 1 ];
    float z = dot / f.m_scale[ 2 ];
    y = y * y;
    z = z * z;
    const float sum = y + x + z;
    if ( !( sum < 1.0f ) && !std::isnan( sum ) )
        return 0.0f;
    const float r = std::sqrt( sum );
    const float inner = f.m_inner[ 0 ];
    if ( inner > r )
        return 1.0f;
    return ( r - 1.0f ) / ( inner - 1.0f );
}

// maxss a, b: b unless a > b (either NaN: b).
inline float ui_fade_max( float a, float b )
{
    return a > b ? a : b;
}

// Slot 12 0x43F060: the box's weight at `eye`: each axis' |dot| / scale; beyond 1 on any: 0; else the product of the ramps of the axes past
// their inner ratio.
inline float ui_fade_box( const ui_screen_fade_t& f, const float eye[ 3 ] )
{
    const float d[ 3 ] = { eye[ 0 ] - f.m_position[ 0 ], eye[ 1 ] - f.m_position[ 1 ], eye[ 2 ] - f.m_position[ 2 ] };
    const float ax = std::fabs( ui_fade_dot( f.m_axis[ 0 ], d ) / f.m_scale[ 0 ] );
    const float ay = std::fabs( ui_fade_dot( f.m_axis[ 1 ], d ) / f.m_scale[ 1 ] );
    const float az = std::fabs( ui_fade_dot( f.m_axis[ 2 ], d ) / f.m_scale[ 2 ] );
    if ( ui_fade_max( ui_fade_max( ax, ay ), az ) > 1.0f )
        return 0.0f;
    float out = 1.0f;
    if ( ax > f.m_inner[ 0 ] )
        out = ( ax - 1.0f ) / ( f.m_inner[ 0 ] - 1.0f );
    if ( ay > f.m_inner[ 1 ] )
        out = ( ay - 1.0f ) / ( f.m_inner[ 1 ] - 1.0f ) * out;
    if ( az > f.m_inner[ 2 ] )
        out = ( az - 1.0f ) / ( f.m_inner[ 2 ] - 1.0f ) * out;
    return out;
}

// One viewport (g 0x759060 +8 + 0x274 x k: its rect at +0, the eye at +0x20).
struct ui_fade_view_t
{
    float m_rect[ 4 ] = { };
    float m_eye[ 3 ] = { };
};

// The tick 0x43E710: per viewport the weight (slot 12) scales each colour byte (fistp of byte x weight); a zero alpha draws nothing; else the
// key word's fields (bits 27.. 1, 24, 25 clear; 24.. = the viewport k; bit 23 1; bits 18 .. 21 = 9) and the solid rect 0x507830 over the
// viewport's rect at depth 1.0 (xmm1 0x6F76B8).
inline void ui_fade_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_screen_fade_t& f, const std::vector< ui_fade_view_t >& views )
{
    for ( std::size_t k = 0; k < views.size( ); ++k )
    {
        const float weight = f.m_box ? ui_fade_box( f, views[ k ].m_eye ) : ui_fade_ellipsoid( f, views[ k ].m_eye );
        std::uint8_t bytes[ 4 ];
        for ( int c = 0; c < 4; ++c )
            bytes[ c ] = static_cast< std::uint8_t >( ui_fistp32( static_cast< float >( f.m_colour[ c ] ) * weight ) & 0xFF );
        if ( bytes[ 3 ] == 0u )
            continue;
        std::uint32_t hi = sort.m_key_hi;
        hi = ( hi & 0x07FFFFFFu ) | 0x08000000u;
        hi &= 0xFCFFFFFFu;
        hi |= static_cast< std::uint32_t >( k ) << 24;
        hi = ( hi & 0xFF7FFFFFu ) | 0x00800000u;
        hi = ( hi & 0xFFC3FFFFu ) | 0x00240000u;
        sort.m_key_hi = hi;
        const std::uint32_t colour = static_cast< std::uint32_t >( bytes[ 0 ] ) | ( static_cast< std::uint32_t >( bytes[ 1 ] ) << 8 ) | ( static_cast< std::uint32_t >( bytes[ 2 ] ) << 16 ) |
                                     ( static_cast< std::uint32_t >( bytes[ 3 ] ) << 24 );
        ui_solid_draw( sort, env, colour, views[ k ].m_rect, 1.0f );
    }
}
