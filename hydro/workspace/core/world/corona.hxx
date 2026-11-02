#pragma once

#include <workspace/core/world/drawer.hxx>
#include <workspace/core/world/gfxsort.hxx>

#include <cstdint>
#include <cstring>

// VUDIRECTIONALCORONAENTITY (the sun's disc), its CPU side recreated 1:1 from the disassembly (water batch 32): the per-viewport occlusion tick 0x5E8080, the draw 0x5E82D0 and the ray result's
// two slots 0x5E7630 / 0x5E7650. The transform's +0x60 row is the direction away from the sun; the disc sits one unit inside the camera's far plane along it. HOOKS: the collision ray 0x50CFB0
// (gfx_corona_ray_t), the material made by slot 6 0x5E7ED0 (+0xA0, device), the record's draw 0x5E8410, the draw manager's add / remove 0x5E7490 / 0x5E74E0 / 0x5E8030.
// 0x5E81E0 is the entity's dev gizmo: STOPPED. Not wired: no caller in the running game yet.

constexpr std::uint32_t k_gfx_cb_sun = 0x5E8410u;

struct gfx_corona_ray_t
{
    virtual ~gfx_corona_ray_t( ) = default;
    virtual bool blocked( const float from[ 3 ], const float to[ 3 ] ) = 0;   // 0x50CFB0 with a VuDirectionalCoronaRayTestResult, its +4
};

// The result's slot 1 0x5E7630: the body's flag byte +0x2D0, bit 3 clear.
inline bool gfx_corona_ray_wants( std::uint8_t flags )
{
    return ( flags & 8u ) == 0u;
}

// The result's add 0x5E7650 (+8 the body, +0xC the fraction 1, +0x10 the triangle, +0x14 the normal). `shape`: the body's shape +0xD4 +8 is set; `surface` its triangle's material byte +0x29,
// reached by +0x70[ tri x 4 ] & 0x1F x 0x2C into +0x7C. False: this triangle is passed through. Equal fractions replace: comiss / jb.
struct gfx_corona_result_t
{
    std::uint32_t m_body = 0;
    float m_fraction = 1.0f;
    std::uint32_t m_triangle = 0;
    std::uint32_t m_normal[ 4 ] = { };

    bool add( std::uint32_t body, float fraction, std::uint32_t triangle, const std::uint32_t normal[ 4 ], bool shape, std::uint8_t surface )
    {
        if ( shape && surface == 0 )
            return false;
        if ( m_fraction >= fraction )
        {
            m_body = body;
            m_fraction = fraction;
            m_triangle = triangle;
            std::memcpy( m_normal, normal, 16 );
        }
        return true;
    }
};

struct gfx_sun_view_t
{
    gfx_camera_t m_camera;             // the view's +0x28 (stride 0x274, g 0x759060)
};

struct gfx_sun_corona_t
{
    // 0x5E8080: per view: the request (+0xA4 + 8i) taken, visible (+0xA8 + 8i) = 0; a request casts eye -> eye - forward x (far - 1) and an open ray makes it 1.
    void tick( const gfx_sun_view_t* views, int count, gfx_corona_ray_t& ray )
    {
        for ( int i = 0; i < count; ++i )
        {
            m_visible[ i ] = 0.0f;
            if ( m_request[ i ] )
            {
                const gfx_camera_t& c = views[ i ].m_camera;
                const float length = c.m_far - 1.0f;
                const float to[ 3 ] = { -m_forward[ 0 ] * length + c.m_eye[ 0 ], -m_forward[ 1 ] * length + c.m_eye[ 1 ], -m_forward[ 2 ] * length + c.m_eye[ 2 ] };
                if ( !ray.blocked( c.m_eye, to ) )
                    m_visible[ i ] = 1.0f;
            }
            m_request[ i ] = 0;
        }
    }

    // 0x5E82D0 (params): the view = the key's viewport bits (+0x77 & 3); its request set; if visible a 0x20-byte record (position, colour +0x8C, size, +0x94, +0x98), type 0x12, no chunk.
    // The position's fourth word is stack: not written. Retail's size: ( ( +0x90 x 0.01 ) x 2 x length ) / camera +0x44.
    void draw( gfx_sort_t& sort, const gfx_draw_params_t& p, float camera_44 )
    {
        const std::uint32_t view = ( sort.m_key_hi >> 24 ) & 3u;
        m_request[ view ] = 1;
        if ( !( m_visible[ view ] > 0.0f ) )
            return;
        const gfx_camera_t& c = *p.m_camera;
        const float length = c.m_far - 1.0f;
        const float size = ( ( ( m_size * 0.01f ) * 2.0f ) * length ) / camera_44;
        const float position[ 3 ] = { c.m_eye[ 0 ] + -m_forward[ 0 ] * length, c.m_eye[ 1 ] + -m_forward[ 1 ] * length, c.m_eye[ 2 ] + -m_forward[ 2 ] * length };
        std::uint8_t* r = sort.allocate( 0x20 );
        std::memcpy( r, position, 12 );
        std::memcpy( r + 0x10, &m_colour, 4 );
        std::memcpy( r + 0x14, &size, 4 );
        std::memcpy( r + 0x18, &m_word_94, 4 );
        std::memcpy( r + 0x1C, &m_word_98, 4 );
        sort.submit_keyed( 0x12, m_material, 0, k_gfx_cb_sun, 0.0f );
    }

    float m_forward[ 3 ] = { };        // [+0x50] +0x60
    std::uint32_t m_colour = 0;        // +0x8C
    float m_size = 0.0f;               // +0x90
    std::uint32_t m_word_94 = 0, m_word_98 = 0;
    std::uint32_t m_material = 0;      // +0xA0
    std::uint8_t m_request[ 4 ] = { }; // +0xA4, stride 8
    float m_visible[ 4 ] = { };        // +0xA8, stride 8
};
