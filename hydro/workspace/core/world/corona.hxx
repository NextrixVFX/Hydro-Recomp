#pragma once

#include <workspace/core/world/drawer.hxx>
#include <workspace/core/world/gfxsort.hxx>
#include <workspace/core/collision/raytest.hxx>

#include <cmath>
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

struct gfx_corona_entity_ray_t
{
    virtual ~gfx_corona_entity_ray_t( ) = default;
    virtual bool blocked( const float from[ 3 ], const float to[ 3 ], std::uint32_t mask ) = 0;   // 0x50CFB0 with a VuCoronaRayTestResult (+0x24 the mask), its +4
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

// VuCoronaRayTestResult's slot 0 0x61CE30 (vtable 0x7580E8; its slot 1 is the same add 0x5E7650): the flag byte +0x2D0 bit 3 clear, then the body's sign-extended group +0x2CA against the
// entity's mask (result +0x24).
inline bool gfx_corona_ray_wants_mask( std::uint8_t flags, std::int16_t group, std::uint32_t mask )
{
    if ( flags & 8u )
        return false;
    return ( static_cast< std::uint32_t >( static_cast< std::int32_t >( group ) ) & mask ) != 0u;
}

// RayTestWrapper's addSingleResult 0x50CE20, the normal: already in world space it passes through; otherwise it goes through the body's basis rows +0x10 / +0x20 / +0x30. Lanes 0 - 2 only: lane 3 is stack.
inline void gfx_ray_wrapper_normal( const float* basis, const float* normal, bool in_world, float* out )
{
    if ( in_world )
    {
        out[ 0 ] = normal[ 0 ];
        out[ 1 ] = normal[ 1 ];
        out[ 2 ] = normal[ 2 ];
        return;
    }
    const float x = normal[ 0 ], y = normal[ 1 ], z = normal[ 2 ];
    out[ 0 ] = ( basis[ 1 ] * y + basis[ 0 ] * x ) + basis[ 2 ] * z;
    out[ 1 ] = ( basis[ 5 ] * y + basis[ 4 ] * x ) + basis[ 6 ] * z;
    out[ 2 ] = ( basis[ 9 ] * y + basis[ 8 ] * x ) + basis[ 10 ] * z;
}

// VuDynamicsRayTest 0x50CFB0 on the race's collision world with either corona's result: RayTestWrapper's needsCollision 0x50CDF0 (internal type +0xF4 == 2, then the result's slot 0) and addSingleResult
// 0x50CE20 (the triangle = shape info +4, -1 with no info; a refused hit answers 1.0, a taken one sets the result's +4 and answers the hit's own fraction). image / surface are the caller's: the body's
// VuRigidBody bytes (+0x10 the basis, +0x2CA, +0x2D0) and the triangle's material byte +0x29 through 0x5E7650's chain, -1 when the body's shape +0xD4 +8 is null. A ray reaching a path the collision
// code has not recreated sets m_unknown: its answer holds only up to there.
struct gfx_corona_collision_ray_t : gfx_corona_ray_t, gfx_corona_entity_ray_t
{
    collision_world_t* m_world = nullptr;
    void* m_user = nullptr;
    const std::uint8_t* ( *m_image )( void* user, const collision_object_t& o ) = nullptr;
    int ( *m_surface )( void* user, const collision_object_t& o, int triangle ) = nullptr;
    bool m_unknown = false;

    bool m_use_mask = false;
    std::uint32_t m_mask = 0;
    bool m_hit = false;              // the result's +4
    gfx_corona_result_t m_result;

    bool wants( const collision_object_t& o ) const
    {
        if ( o.internal_type != 2 )
            return false;
        const std::uint8_t* b = m_image( m_user, o );
        if ( !m_use_mask )
            return gfx_corona_ray_wants( b[ 0x2D0 ] );
        std::int16_t group;
        std::memcpy( &group, b + 0x2CA, 2 );
        return gfx_corona_ray_wants_mask( b[ 0x2D0 ], group, m_mask );
    }

    float add( const collision_object_t& o, int triangle, float fraction, const float* normal, bool in_world )
    {
        const std::uint8_t* b = m_image( m_user, o );
        float basis[ 12 ];
        std::memcpy( basis, b + 0x10, sizeof( basis ) );
        float n[ 3 ];
        gfx_ray_wrapper_normal( basis, normal, in_world, n );
        std::uint32_t words[ 4 ] = { };
        std::memcpy( words, n, 12 );
        const int surface = m_surface( m_user, o, triangle );
        const bool shape = surface >= 0;
        const std::uint32_t body = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( &o ) );
        if ( !m_result.add( body, fraction, static_cast< std::uint32_t >( triangle ), words, shape, static_cast< std::uint8_t >( surface ) ) )
            return 1.0f;
        m_hit = true;
        return fraction;
    }

    bool cast( const float from[ 3 ], const float to[ 3 ] )
    {
        m_hit = false;
        m_result = gfx_corona_result_t( );
        if ( !m_world || !m_image || !m_surface )
        {
            m_unknown = true;
            return false;
        }
        ray_callback_t cb;
        cb.user = this;
        cb.needs_collision = []( void* u, ray_callback_t&, const broadphase_proxy_t& proxy ) {
            return static_cast< gfx_corona_collision_ray_t* >( u )->wants( *static_cast< const collision_object_t* >( proxy.client ) );
        };
        cb.add_single_result = []( void* u, ray_callback_t&, ray_local_result_t& r, bool in_world ) {
            return static_cast< gfx_corona_collision_ray_t* >( u )->add( *r.object, r.info ? r.info->triangle : -1, r.fraction, r.normal, in_world );
        };
        const float f[ 4 ] = { from[ 0 ], from[ 1 ], from[ 2 ], 0.0f };
        const float t[ 4 ] = { to[ 0 ], to[ 1 ], to[ 2 ], 0.0f };
        if ( collision_world_ray_test( *m_world, f, t, cb ) < 0 )
            m_unknown = true;
        return m_hit;
    }

    bool blocked( const float from[ 3 ], const float to[ 3 ] ) override
    {
        m_use_mask = false;
        return cast( from, to );
    }

    bool blocked( const float from[ 3 ], const float to[ 3 ], std::uint32_t mask ) override
    {
        m_use_mask = true;
        m_mask = mask;
        return cast( from, to );
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

// VUCORONAENTITY (a placed light's corona), CPU side: the tick 0x5E9B60 and its occlusion half 0x61D000 (the component at entity +0xA0), and the aabb writer 0x5E9BD0.
struct gfx_corona_entity_t
{
    // 0x61D000: per view: visible = 0; a request casts eye -> position pulled back toward the eye by the radius, and an open ray makes it 1. The request is cleared either way.
    void occlusion( const float position[ 3 ], const gfx_sun_view_t* views, int count, gfx_corona_entity_ray_t& ray )
    {
        for ( int i = 0; i < count; ++i )
        {
            m_visible[ i ] = 0.0f;
            if ( m_request[ i ] )
            {
                const float* eye = views[ i ].m_camera.m_eye;
                const float dx = position[ 0 ] - eye[ 0 ], dy = position[ 1 ] - eye[ 1 ], dz = position[ 2 ] - eye[ 2 ];
                const float scale = 1.0f / std::sqrt( ( dy * dy + dx * dx ) + dz * dz );
                const float to[ 3 ] = { position[ 0 ] - ( scale * dx ) * m_radius, position[ 1 ] - ( scale * dy ) * m_radius, position[ 2 ] - ( scale * dz ) * m_radius };
                if ( !ray.blocked( eye, to, m_mask ) )
                    m_visible[ i ] = 1.0f;
            }
            m_request[ i ] = 0;
        }
    }

    // 0x5E9B60: the occlusion, then the phase: speed x dt x 0x3E22F983 (1 / 2pi) + phase, less its truncation.
    void tick( const float position[ 3 ], const gfx_sun_view_t* views, int count, gfx_corona_entity_ray_t& ray, float dt )
    {
        occlusion( position, views, count, ray );
        const float k = 0.15915494f;
        const float phase = m_speed * dt * k + m_phase;
        m_phase = phase - static_cast< float >( static_cast< int >( phase ) );
    }

    // 0x5E9BD0: the draw component's aabb (+0x60 +0x18 .. +0x34) from the radius. Lanes 3 of both corners are stack in retail: not written here.
    void aabb( float aabb_min[ 3 ], float aabb_max[ 3 ] ) const
    {
        const std::uint32_t sign = 0x80000000u;
        std::uint32_t bits;
        std::memcpy( &bits, &m_radius, 4 );
        bits ^= sign;
        float negative;
        std::memcpy( &negative, &bits, 4 );
        for ( int i = 0; i < 3; ++i )
        {
            aabb_min[ i ] = negative;
            aabb_max[ i ] = m_radius;
        }
    }

    float m_speed = 0.0f;              // +0x8C
    float m_radius = 0.0f;             // +0xA8 (component +8)
    std::uint32_t m_mask = 0;          // +0xC8 (component +0x28)
    std::uint8_t m_request[ 4 ] = { }; // +0xD0 (component +0x30), stride 8
    float m_visible[ 4 ] = { };        // +0xD4 (component +0x34), stride 8
    float m_phase = 0.0f;              // +0xF4
};
