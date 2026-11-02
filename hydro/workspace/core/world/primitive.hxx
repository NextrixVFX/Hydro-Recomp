#pragma once

#include <workspace/core/world/gfxsort.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>

// VUGFXUTIL'S RECORD BUILDERS (render layer, platform independent), recreated 1:1 from the disassembly (water batch 31): each writes its record into the sort's
// memory and queues one command. The record's draw is the callback named here by its retail address: geometry + device, the platform's, not recreated.
// The texture / rect / font builders are the UI agent's (core/ui/draw.hxx); this file is the rest of g 0x75902C's. The shader set is **(VuGfxUtil +4).
// Retail inlines 0x50B7B0 ( 2, material, 0, callback, 0 ) in eight of them; the keys are the same, so here all go through submit_keyed.

constexpr std::uint32_t k_gfx_cb_ellipse = 0x507C90u, k_gfx_cb_line2d = 0x507F30u, k_gfx_cb_line3d = 0x5080F0u, k_gfx_cb_line3d2 = 0x5082B0u, k_gfx_cb_box = 0x508520u;
constexpr std::uint32_t k_gfx_cb_aabb = 0x5086D0u, k_gfx_cb_508870 = 0x508870u, k_gfx_cb_508B20 = 0x508B20u, k_gfx_cb_508F20 = 0x508F20u, k_gfx_cb_509180 = 0x509180u;
constexpr std::uint32_t k_gfx_cb_509790 = 0x509790u, k_gfx_cb_5098E0 = 0x5098E0u, k_gfx_cb_509A40 = 0x509A40u, k_gfx_cb_509F50 = 0x509F50u, k_gfx_cb_50A420 = 0x50A420u;
constexpr std::uint32_t k_gfx_cb_50A890 = 0x50A890u, k_gfx_cb_50B2F0 = 0x50B2F0u;
constexpr std::uint32_t k_gfx_two_pi_bits = 0x40C90FDBu;

struct gfx_util_t
{
    gfx_sort_t& m_sort;
    const float* m_top = nullptr;      // the matrix stack's top (+0x20, a deque: 0x4BC130 / 0x4BC2E0)
    std::uint32_t m_flat = 0;          // shader set +8
    std::uint32_t m_lines = 0;         // +0x38
    std::uint32_t m_lines2 = 0;        // +0x50
    std::uint32_t m_lit = 0;           // +0x5C

    static void put( std::uint8_t* r, int at, const void* from, std::size_t size ) { std::memcpy( r + at, from, size ); }
    static void put_word( std::uint8_t* r, int at, std::uint32_t v ) { std::memcpy( r + at, &v, 4 ); }
    static void put_float( std::uint8_t* r, int at, float v ) { std::memcpy( r + at, &v, 4 ); }

    // The opaque test of 0x5095D0 .. 0x50A7C0: alpha 0xFF sorts as type 2 by material, else type 11 by depth 0.
    void queue( std::uint32_t colour, std::uint32_t material, std::uint32_t callback )
    {
        if ( ( colour >> 24 ) == 0xFFu )
            m_sort.submit_keyed( 2, material, 0, callback, 0.0f );
        else
            m_sort.submit( 11, material, 0, callback, 0.0f );
    }

    // 0x507BE0 (colour, 4 floats, an int).
    void ellipse( std::uint32_t colour, const float rect[ 4 ], std::uint32_t segments )
    {
        std::uint8_t* r = m_sort.allocate( 0x58 );
        put( r, 0, m_top, 64 );
        put_word( r, 0x40, colour );
        put( r, 0x44, rect, 16 );
        put_word( r, 0x54, segments );
        m_sort.submit( 0x11, m_flat, 0, k_gfx_cb_ellipse, 0.0f );
    }

    // 0x507E80 (colour, two 2-float points).
    void line_2d( std::uint32_t colour, const float a[ 2 ], const float b[ 2 ] )
    {
        std::uint8_t* r = m_sort.allocate( 0x54 );
        put( r, 0, m_top, 64 );
        put_word( r, 0x40, colour );
        put( r, 0x44, a, 8 );
        put( r, 0x4C, b, 8 );
        m_sort.submit( 0x11, m_flat, 0, k_gfx_cb_line2d, 0.0f );
    }

    // 0x508000 (colour, two 16-byte points, matrix).
    void line_3d( std::uint32_t colour, const float a[ 4 ], const float b[ 4 ], const float matrix[ 16 ] )
    {
        std::uint8_t* r = m_sort.allocate( 0x64 );
        put( r, 0, matrix, 64 );
        put_word( r, 0x40, colour );
        put( r, 0x44, a, 16 );
        put( r, 0x54, b, 16 );
        m_sort.submit_keyed( 2, m_lines, 0, k_gfx_cb_line3d, 0.0f );
    }

    // 0x5081C0 (a, colour a, b, colour b, matrix).
    void line_3d_two( const float a[ 4 ], std::uint32_t colour_a, const float b[ 4 ], std::uint32_t colour_b, const float matrix[ 16 ] )
    {
        std::uint8_t* r = m_sort.allocate( 0x68 );
        put( r, 0, matrix, 64 );
        put( r, 0x40, a, 16 );
        put( r, 0x50, b, 16 );
        put_word( r, 0x60, colour_a );
        put_word( r, 0x64, colour_b );
        m_sort.submit_keyed( 2, m_lines2, 0, k_gfx_cb_line3d2, 0.0f );
    }

    // 0x508370 (unread, colour, five 16-byte points, unread, matrix): a strip of five, the points' x, y, z only.
    void strip_lines( std::uint32_t colour, const float points[ 20 ], const float matrix[ 16 ] )
    {
        std::uint8_t* r = m_sort.allocate( 0x88 );
        put( r, 0, matrix, 64 );
        put_word( r, 0x40, 2 );
        put_word( r, 0x44, colour );
        put_word( r, 0x48, 5 );
        for ( int i = 0; i < 5; ++i )
            put( r, 0x4C + 12 * i, points + 4 * i, 12 );
        m_sort.submit_keyed( 2, m_lines, 0, k_gfx_cb_box, 0.0f );
    }

    // 0x5085E0 (colour, aabb, matrix).
    void aabb_lines( std::uint32_t colour, const float aabb[ 8 ], const float matrix[ 16 ] )
    {
        std::uint8_t* r = m_sort.allocate( 0x64 );
        put( r, 0, matrix, 64 );
        put_word( r, 0x40, colour );
        put( r, 0x44, aabb, 32 );
        m_sort.submit_keyed( 2, m_lines, 0, k_gfx_cb_aabb, 0.0f );
    }

    // 0x508780 (colour, an int, matrix; xmm2, xmm3).
    void shape_508780( std::uint32_t colour, std::uint32_t a, const float matrix[ 16 ], float f0, float f1 )
    {
        std::uint8_t* r = m_sort.allocate( 0x50 );
        put( r, 0, matrix, 64 );
        put_word( r, 0x40, colour );
        put_float( r, 0x44, f0 );
        put_float( r, 0x48, f1 );
        put_word( r, 0x4C, a );
        m_sort.submit_keyed( 2, m_lines, 0, k_gfx_cb_508870, 0.0f );
    }

    // 0x508A40 (colour, two ints, matrix; xmm2).
    void shape_508A40( std::uint32_t colour, std::uint32_t a, std::uint32_t b, const float matrix[ 16 ], float f0 )
    {
        std::uint8_t* r = m_sort.allocate( 0x50 );
        put( r, 0, matrix, 64 );
        put_word( r, 0x40, colour );
        put_float( r, 0x44, f0 );
        put_word( r, 0x48, a );
        put_word( r, 0x4C, b );
        m_sort.submit_keyed( 2, m_lines, 0, k_gfx_cb_508B20, 0.0f );
    }

    // 0x508E30 (colour, a float, matrix; xmm2, xmm3).
    void shape_508E30( std::uint32_t colour, float f2, const float matrix[ 16 ], float f0, float f1 )
    {
        std::uint8_t* r = m_sort.allocate( 0x50 );
        put( r, 0, matrix, 64 );
        put_word( r, 0x40, colour );
        put_float( r, 0x44, f0 );
        put_float( r, 0x48, f1 );
        put_float( r, 0x4C, f2 );
        m_sort.submit_keyed( 2, m_lines, 0, k_gfx_cb_508F20, 0.0f );
    }

    // 0x509050 (colour, three 16-byte vectors, six words of which only one float is read, matrix; ret 0x28): the sweep 0 .. 2 pi, 16 segments.
    void shape_509050( std::uint32_t colour, const float a[ 4 ], const float b[ 4 ], const float c[ 4 ], float f, const float matrix[ 16 ] )
    {
        std::uint8_t* r = m_sort.allocate( 0x88 );
        put( r, 0, matrix, 64 );
        put_word( r, 0x40, colour );
        put( r, 0x44, a, 16 );
        put( r, 0x54, b, 16 );
        put( r, 0x64, c, 16 );
        put_word( r, 0x74, 0 );
        put_word( r, 0x78, k_gfx_two_pi_bits );
        put_float( r, 0x7C, f );
        put_word( r, 0x80, 0x10 );
        r[ 0x84 ] = 0;
        m_sort.submit_keyed( 2, m_lines, 0, k_gfx_cb_509180, 0.0f );
    }

    // 0x5095D0 (colour, four 16-byte corners, unread, matrix).
    void quad_fill( std::uint32_t colour, const float corners[ 16 ], const float matrix[ 16 ] )
    {
        std::uint8_t* r = m_sort.allocate( 0x78 );
        put( r, 0, matrix, 64 );
        put_word( r, 0x40, colour );
        put_word( r, 0x44, 4 );
        for ( int i = 0; i < 4; ++i )
            put( r, 0x48 + 12 * i, corners + 4 * i, 12 );
        queue( colour, m_lines, k_gfx_cb_509790 );
    }

    // 0x509810 (colour, aabb, matrix, matrix, 16 bytes).
    void lit_509810( std::uint32_t colour, const float aabb[ 8 ], const float m0[ 16 ], const float m1[ 16 ], const float v[ 4 ] )
    {
        std::uint8_t* r = m_sort.allocate( 0xB4 );
        put( r, 0, m0, 64 );
        put( r, 0x40, m1, 64 );
        put_word( r, 0x80, colour );
        put( r, 0x84, aabb, 32 );
        put( r, 0xA4, v, 16 );
        queue( colour, m_lit, k_gfx_cb_5098E0 );
    }

    // 0x509970 / 0x50A350 / 0x50A7C0, the same record (colour, an int, matrix, matrix, 16 bytes; xmm2, xmm3), three callbacks.
    void lit_shape( std::uint32_t callback, std::uint32_t colour, std::uint32_t a, const float m0[ 16 ], const float m1[ 16 ], const float v[ 4 ], float f0, float f1 )
    {
        std::uint8_t* r = m_sort.allocate( 0xA0 );
        put( r, 0, m0, 64 );
        put( r, 0x40, m1, 64 );
        put_word( r, 0x80, colour );
        put_float( r, 0x84, f0 );
        put_float( r, 0x88, f1 );
        put_word( r, 0x8C, a );
        put( r, 0x90, v, 16 );
        queue( colour, m_lit, callback );
    }

    // 0x509E80 (colour, two ints, matrix, matrix, 16 bytes; xmm2).
    void lit_509E80( std::uint32_t colour, std::uint32_t a, std::uint32_t b, const float m0[ 16 ], const float m1[ 16 ], const float v[ 4 ], float f0 )
    {
        std::uint8_t* r = m_sort.allocate( 0xA0 );
        put( r, 0, m0, 64 );
        put( r, 0x40, m1, 64 );
        put_word( r, 0x80, colour );
        put_float( r, 0x84, f0 );
        put_word( r, 0x88, a );
        put_word( r, 0x8C, b );
        put( r, 0x90, v, 16 );
        queue( colour, m_lit, k_gfx_cb_509F50 );
    }

    // 0x50B250 (a word, a pointed word): a command of the key's bits alone, no material, type 0. The callback 0x50B2F0 hands +0, +4 and the 1.0 at +8 to the device's +0x68.
    void key_command( std::uint32_t a, std::uint32_t b )
    {
        std::uint8_t* r = m_sort.allocate( 0xC );
        put_word( r, 0, a );
        put_word( r, 4, b );
        put_float( r, 8, 1.0f );
        m_sort.submit_bare( k_gfx_cb_50B2F0 );
    }
};

// VuCamera 0x50C250 (ecx the camera, centre, the radius in xmm2): true unless the box's signed distance + radius has its sign bit set for any of the six planes
// (camera +0x1E0, here the planes). A sign-bit OR, so -0 and a negative NaN both fail. Each row's additions in retail's own order.
inline bool gfx_sphere_visible( const float planes[ 24 ], const float centre[ 3 ], float radius )
{
    std::uint32_t signs = 0;
    for ( int i = 0; i < 6; ++i )
    {
        const float* p = planes + 4 * i;
        const float d = ( ( p[ 1 ] * centre[ 1 ] + centre[ 0 ] * p[ 0 ] ) + p[ 2 ] * centre[ 2 ] ) + p[ 3 ] + radius;
        std::uint32_t bits;
        std::memcpy( &bits, &d, 4 );
        signs |= bits & 0x80000000u;
    }
    return signs == 0;
}

// 0x50C6B0 (ecx v0, edx v1, v2, segment a, segment b, out x y z; caller cleans): the segment's midpoint form against the triangle, two-sided. Out's fourth word is a stack
// leftover in retail, not written here.
inline bool gfx_segment_triangle( const float v0[ 3 ], const float v1[ 3 ], const float v2[ 3 ], const float a[ 3 ], const float b[ 3 ], float out[ 3 ] )
{
    const float mz = ( b[ 2 ] + a[ 2 ] ) * 0.5f, mx = ( b[ 0 ] + a[ 0 ] ) * 0.5f, my = ( b[ 1 ] + a[ 1 ] ) * 0.5f;
    float dx = b[ 0 ] - a[ 0 ], dy = b[ 1 ] - a[ 1 ], dz = b[ 2 ] - a[ 2 ];
    const float length = std::sqrt( ( dy * dy + dx * dx ) + dz * dz );
    const float inverse = 1.0f / length;
    const float half = length * 0.5f;
    dx = dx * inverse;
    dy = dy * inverse;
    dz = dz * inverse;
    const float qx = mx - v0[ 0 ], qy = my - v0[ 1 ], qz = mz - v0[ 2 ];
    const float e1x = v1[ 0 ] - v0[ 0 ], e1y = v1[ 1 ] - v0[ 1 ], e1z = v1[ 2 ] - v0[ 2 ];
    const float e2x = v2[ 0 ] - v0[ 0 ], e2y = v2[ 1 ] - v0[ 1 ], e2z = v2[ 2 ] - v0[ 2 ];
    const float nx = e2z * e1y - e2y * e1z, ny = e2x * e1z - e2z * e1x, nz = e2y * e1x - e2x * e1y;
    float det = ( ny * dy + nx * dx ) + nz * dz;
    float sign;
    if ( det > 1.1920929e-07f )     // 0x6F7584
        sign = 1.0f;
    else if ( -1.1920929e-07f > det )
    {
        sign = -1.0f;
        det = -det;
    }
    else
        return false;
    const float u = ( ( ( ( e2x * qz - e2z * qx ) * dy + ( e2z * qy - e2y * qz ) * dx ) + ( e2y * qx - e2x * qy ) * dz ) * sign );
    if ( !( u >= 0.0f ) )
        return false;
    const float w = ( ( ( e1z * qx - e1x * qz ) * dy + ( e1y * qz - e1z * qy ) * dx ) + ( e1x * qy - e1y * qx ) * dz ) * sign;
    if ( !( w >= 0.0f ) )
        return false;
    if ( !( det >= w + u ) )
        return false;
    const float t = -( ( ( ny * qy + nx * qx ) + nz * qz ) * sign );
    const float limit = det * half;
    if ( !( t >= -limit ) || !( limit >= t ) )
        return false;
    const float s = ( 1.0f / det ) * t;
    out[ 0 ] = dx * s + mx;
    out[ 1 ] = dy * s + my;
    out[ 2 ] = dz * s + mz;
    return true;
}
