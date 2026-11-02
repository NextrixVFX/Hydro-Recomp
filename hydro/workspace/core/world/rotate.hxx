#pragma once

#include <workspace/core/world/keymotion.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>

// VuRotateMotionEntity (vtable 0x752CAC, ctor 0x5D0DC0 over VuMotionEntity's; the head of keymotion.hxx describes the base): slot 12
// 0x5D0FE0 at activation, slot 14 0x5D1090 the "Motion" tick while active. Properties: engine entity/actions.hxx
// rotate_motion_properties_t. Verified: checks/rig.hxx verify_rotate_motion (case rotate_motion).
// SUBSTITUTION: __libm_sse2_atan2 / asinf in the angular velocity (transform.hxx transform_euler's CRT double narrowed).
struct rotate_motion_t
{
    float axis[ 3 ]{ };      // +0x70
    float speed = 0.0f;      // +0x80
    bool local = true;       // +0x84
    float base[ 16 ]{ };     // +0x88
    float angle = 0.0f;      // +0xC8
};

// Slot 12 0x5D0FE0: the target's transform world (+0x68 -> +8 -> +0x50: `world`, null without one: +0x88 keeps its words) into
// the base, the axis normalised (1 / sqrt( x2 + y2 + z2 ), each lane multiplied), angle 0.
inline void rotate_motion_enable( rotate_motion_t& r, const float* world )
{
    if ( world )
        std::memcpy( r.base, world, 64 );
    const float x = r.axis[ 0 ], y = r.axis[ 1 ], z = r.axis[ 2 ];
    const float k = 1.0f / std::sqrt( ( x * x + y * y ) + z * z );
    r.axis[ 0 ] = x * k;
    r.axis[ 1 ] = y * k;
    r.axis[ 2 ] = z * k;
    r.angle = 0.0f;
}

// Slot 14 0x5D1090 (the tick, retn 4): the angle advanced and wrapped into [ -pi, pi ] (cvttss2si, so a magnitude beyond int32 reads
// 0x80000000); M1 = the base rotated by it, Local through 0x55A350 else 0x5D0BE0, M2 = by speed x 0.001 + angle; angular = (euler(
// M2 ) - euler( M1 )) x 999.99994, its w a stack word retail never wrote (UNKNOWN, 0 here); the target's motion gets M1, linear 0
// (all four words), angular, then its IF slot 0 (the caller's).
inline void rotate_motion_tick( rotate_motion_t& r, float dt, float* matrix, float* angular )
{
    const float x = dt * r.speed + r.angle + control_f( 0x40490FDBu );
    const float a = std::fabs( x );
    const float turns = a * control_f( 0x3E22F983u );
    const std::int32_t whole = ( turns >= -2147483648.0f && turns < 2147483648.0f ) ? static_cast< std::int32_t >( turns ) : static_cast< std::int32_t >( 0x80000000u );
    float wrapped = ( a - static_cast< float >( whole ) * control_f( 0x40C90FDBu ) ) - control_f( 0x40490FDBu );
    if ( 0.0f > x )
    {
        std::uint32_t u;
        std::memcpy( &u, &wrapped, 4 );
        u ^= 0x80000000u;
        std::memcpy( &wrapped, &u, 4 );
    }
    r.angle = wrapped;
    float m2[ 16 ];
    std::memcpy( matrix, r.base, 64 );
    std::memcpy( m2, r.base, 64 );
    const float ahead = r.speed * control_f( 0x3A83126Fu ) + r.angle;
    if ( r.local )
    {
        vu_matrix_rotate_axis_angle_pre( matrix, r.axis, r.angle );
        vu_matrix_rotate_axis_angle_pre( m2, r.axis, ahead );
    }
    else
    {
        vu_matrix_rotate_axis_angle_post( matrix, r.axis, r.angle );
        vu_matrix_rotate_axis_angle_post( m2, r.axis, ahead );
    }
    float e1[ 3 ], e2[ 3 ];
    transform_euler( matrix, e1 );
    transform_euler( m2, e2 );
    const float k = control_f( 0x4479FFFFu );
    angular[ 0 ] = ( e2[ 0 ] - e1[ 0 ] ) * k;
    angular[ 1 ] = ( e2[ 1 ] - e1[ 1 ] ) * k;
    angular[ 2 ] = ( e2[ 2 ] - e1[ 2 ] ) * k;
    angular[ 3 ] = 0.0f;
}
