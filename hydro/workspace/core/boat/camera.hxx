#pragma once

#include <workspace/core/boat/ai.hxx>
#include <workspace/core/boat/hull.hxx>
#include <workspace/core/boat/math.hxx>
#include <workspace/core/boat/state.hxx>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <xmmintrin.h>

// VuBoatCamera_tick 0x409900 and its closure, on the camera's image (0x308
// bytes; ctor 0x409750 = boat_409750, parts.hxx; its authored fields from
// boat_read_camera, sections.hxx). The VuCamera base (0..0x23F) after a tick:
// +0x00 eye, +0x10 target (16 b each), +0x20 the boat's linear velocity, +0x30
// the projection, +0x70 the view (the inverse of +0xF0), +0xB0 view x
// projection, +0xF0 the eye's basis (right, up, -forward, eye), +0x130 the
// camera's transform (right, forward, up, eye), +0x170 vertical fov, +0x174
// aspect, +0x178 near, +0x17C far, +0x180 tan(fov / 2), +0x184 the frustum's
// inputs, +0x1E0 its six planes. VuBoatCamera's own (decimal = the ctor's):
// +0x244 target local offset, +0x254 target world z, +0x258 distance, +0x25C
// pitch (degrees), +0x264 lag distance, +0x270 / +0x280 / +0x290 the three
// fixed views' offsets, +0x2A0 the mode, +0x2A4 the ideal eye, +0x2B4 the last
// eye, +0x2C4 the snap byte, +0x2C8 yaw, +0x2CC roll, +0x2D0 the look-back
// byte, +0x2D4 the smoothed velocity, +0x2E4 shake amplitude, +0x2E8 its timer,
// +0x2EC its duration, +0x2F0 its frequency, +0x2F4 its direction, +0x304 the
// chase ramp.
//
// Lanes retail leaves to the stack (NOT recreated - written 0 here): the w of
// the pivot / eye / target each mode builds (the eye's is the body's +0x14C
// only when the chase or variant mode blends the velocity; after the shake both
// are a slot the mode function's frame left - measured 1.0, UNKNOWN here), so
// +0x0C, +0x1C, +0x2B0 and +0x2C0 can hold stale stack words in retail.
//
// For the world: boat_camera_tick on the boat's camera image (boat +0x224's
// object) is what boat_decision_tick's camera_tick hook stands for (0x41EFF0
// calls 0x409900 there); then read the eye +0x00, target +0x10, view +0x70,
// projection +0x30.

// OURS, NOT RETAIL: the user's Video settings, defaults exact; what each scales is told at the shake below.
inline float g_camera_ours_shake = 1.0f;
inline float g_camera_ours_zoom = 1.0f;
inline float g_camera_ours_fov = 0.0f;       // degrees, 0: ConstantDB's
inline float g_camera_ours_shake_peak = 0.0f;   // the largest eye move since a --camera-log line read it

// What the tick reads through its boat (camera +0x240), each read by the
// caller where retail reads it: world = *(boat +0x50) +0x50 (the transform
// component's world, 16 floats); destroyed = boat +0x46B; debris_count = the
// debris object's (boat +0x238) +0xA0 and piece = its +0x9C array's first
// entry (read only when destroyed and the count > 0: +0x08..+0x47 a matrix,
// +0x1B0 a point); velocity = *(boat +0x21C) +0x140 (16 b); engine = *(boat
// +0x22C) (+0x04, +0x0C read) and engine_owner_470 = *(*(boat +0x22C)) +0x470;
// far_plane = boat +0x408.
struct camera_boat_t
{
    const float* world = nullptr;
    bool destroyed = false;
    std::int32_t debris_count = 0;
    const std::uint8_t* piece = nullptr;
    const float* velocity = nullptr;
    const std::uint8_t* engine = nullptr;
    float engine_owner_470 = 1.0f;
    float far_plane = 0.0f;
};

// VuDB's "VuDBAsset" / "Camera" / "VerticalFOV" json value (0x4FBDF0's
// lookups - the DB is a hook): +0 the type, +8 the payload.
struct camera_db_value_t
{
    std::int32_t type = 0;
    std::uint64_t payload = 0;
};

// What 0x409900 calls that is not recreated: the driver's (boat +0x220) vt+0x34
// (true selects the variant mode), g_vucontrolmethodmanager (0x758FC0) +0x18,
// g_vumetrod3d11gfx (0x75907C) vt+0x40 - the display size (a platform hook,
// read twice, answered the same both times) - and the dynamics world's ray
// test (ai_ray_hit_t's hook: g_vudynamicsimpl vt+0x6C's collision world
// vt+0x18, Bullet's traversal).
struct camera_env_t
{
    bool driver_34 = false;
    std::int32_t control_method = 0;
    std::int32_t display_w = 1280;
    std::int32_t display_h = 720;
    camera_db_value_t vertical_fov;
    void* user = nullptr;
    int ( *ray )( void* user, const float* from, const float* to, const ai_ray_hit_t** hits ) = nullptr;
};

inline float cam_minss( float a, float b ) { return _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); }

// The CRT DLL's __libm_sse2_atan2 0x631CD0 (doubles in, double out) and
// tanf / atanf. SUBSTITUTION: served by the host's double functions rounded
// once to float, as the oracle serves them.
inline float cam_atan2( float y, float x )
{
    return static_cast< float >( std::atan2( static_cast< double >( y ), static_cast< double >( x ) ) );
}
inline float cam_tanf( float x ) { return static_cast< float >( std::tan( static_cast< double >( x ) ) ); }
inline float cam_atanf( float x ) { return static_cast< float >( std::atan( static_cast< double >( x ) ) ); }

// `fld dword t; fsub dword 0.5; fistp dword` - the difference is exact in the
// x87's precision; fistp rounds to nearest even and stores the integer
// indefinite 0x80000000 out of range.
inline std::int32_t cam_fistp_half( float t )
{
    const double d = std::nearbyint( static_cast< double >( t ) - 0.5 );
    if ( !( d >= -2147483648.0 && d <= 2147483647.0 ) )
        return INT_MIN;
    return static_cast< std::int32_t >( d );
}

// One ray through VuDynamicsRayTest 0x50CFB0 with a VuBoatCameraRayTestResult
// (vtable 0x737B14) on the caller's stack: for each hit, needsCollision
// 0x50CDF0 (internal type +0xF4 == 2) then the result's slot 0 0x409730 (the
// body's +0x2CA byte & 1), and addSingleResult 0x50CE20 (a local normal turned
// by the body's basis) into slot 1 0x404B20 (kept when its fraction is ordered
// <= the kept one; hit = 1 either way). The caller resets hit / body /
// fraction per ray; the normal is NOT reset (only read after a hit).
inline void camera_ray( ai_ray_result_t& r, const float* from, const float* to, const camera_env_t& env )
{
    const ai_ray_hit_t* hits = nullptr;
    const int n = env.ray ? env.ray( env.user, from, to, &hits ) : 0;
    for ( int k = 0; k < n; ++k )
    {
        const ai_ray_hit_t& h = hits[ k ];
        std::int32_t type;
        std::memcpy( &type, h.body + 0xF4, 4 );
        if ( type != 2 || ( h.body[ 0x2CA ] & 1 ) == 0 )
            continue;
        float n4[ 4 ];
        std::memcpy( n4, h.normal, 16 );
        if ( !h.world )
        {
            const float nx = h.normal[ 0 ], ny = h.normal[ 1 ], nz = h.normal[ 2 ];
            for ( int row = 0; row < 3; ++row )
            {
                const std::uint8_t* b = h.body + 0x10 + 0x10 * row;
                n4[ row ] = ( state_f( b, 4 ) * ny + state_f( b, 0 ) * nx ) + state_f( b, 8 ) * nz;
            }
            n4[ 3 ] = 0.0f;
        }
        if ( _mm_comige_ss( _mm_set_ss( r.fraction ), _mm_set_ss( h.fraction ) ) )
        {
            r.body = h.body;
            r.fraction = h.fraction;
            std::memcpy( r.normal, n4, 16 );
        }
        r.hit = true;
    }
}

// VuMatrix_rotationY 0x409660 (ecx out, xmm1 angle): x87 fsincos; rows
// (c, 0, -s, 0) (0, 1, 0, 0) (s, 0, c, 0) (0, 0, 0, 1).
inline void camera_rotation_y( float* m, float angle )
{
    float c, s;
    x87_sincos( angle, &c, &s );
    const float r[ 16 ] = { c, 0.0f, -s, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, s, 0.0f, c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    std::memcpy( m, r, 64 );
}

// VuMatrix_lookAtBasis 0x50CA70 (fastcall: ecx dir, edx up, out on the stack):
// r = dir x up, u = r x dir; when FLT_MIN (0x00800000) is ordered > |dir|^2, |r|^2
// or |u|^2, r = (1,0,0), dir = (0,1,0), u = (0,0,1). Out = the identity with
// rows 0 / 1 / 2 = r, dir, u, each times 1 / VuMath_sqrt of its square.
inline void camera_look_at( const float* dir, const float* up, float* out )
{
    float dx = dir[ 0 ], dy = dir[ 1 ], dz = dir[ 2 ];
    float rx = up[ 2 ] * dy - up[ 1 ] * dz;
    float ry = up[ 0 ] * dz - up[ 2 ] * dx;
    float rz = up[ 1 ] * dx - up[ 0 ] * dy;
    float ux = ry * dz - rz * dy;
    float uy = rz * dx - rx * dz;
    float uz = rx * dy - ry * dx;
    if ( FLT_MIN > ( dx * dx + dy * dy ) + dz * dz || FLT_MIN > ( ry * ry + rx * rx ) + rz * rz
         || FLT_MIN > ( uy * uy + ux * ux ) + uz * uz )
    {
        ry = 0.0f; rx = 1.0f; rz = 0.0f;
        dx = 0.0f; dy = 1.0f; dz = 0.0f;
        uz = 1.0f; uy = 0.0f; ux = 0.0f;
    }
    const float lr = std::sqrt( ( ry * ry + rx * rx ) + rz * rz );
    const float ld = std::sqrt( ( dx * dx + dy * dy ) + dz * dz );
    const float lu = std::sqrt( ( uy * uy + ux * ux ) + uz * uz );
    vu_identity( out );
    out[ 0 ] = ( 1.0f / lr ) * rx;
    out[ 1 ] = ( 1.0f / lr ) * ry;
    out[ 2 ] = ( 1.0f / lr ) * rz;
    out[ 4 ] = ( 1.0f / ld ) * dx;
    out[ 5 ] = ( 1.0f / ld ) * dy;
    out[ 6 ] = ( 1.0f / ld ) * dz;
    out[ 8 ] = ( 1.0f / lu ) * ux;
    out[ 9 ] = ( 1.0f / lu ) * uy;
    out[ 10 ] = ( 1.0f / lu ) * uz;
}

// The orientation every orbiting mode ends with: lookAtBasis(pivot - eye,
// (0,0,1)), then rotationY(angle) x it (0x4078C0, rotY = A); up = row 2.
inline void camera_orient( const float* eye, const float* pivot, float angle, float* up )
{
    const float dir[ 3 ] = { pivot[ 0 ] - eye[ 0 ], pivot[ 1 ] - eye[ 1 ], pivot[ 2 ] - eye[ 2 ] };
    const float z[ 3 ] = { 0.0f, 0.0f, 1.0f };
    float look[ 16 ], rot[ 16 ], out[ 16 ];
    camera_look_at( dir, z, look );
    camera_rotation_y( rot, angle );
    hull_multiply( rot, out, look );
    std::memcpy( up, out + 8, 16 );
}

// The pivot both orbiting modes start from (0x409D00 / 0x40A4D0's first
// block): destroyed with debris pieces, the first piece's +0x1B0 point placed by
// its matrix (+0x08 / +0x18 / +0x28 / +0x38 rows); else the target local
// offset (+0x244) placed by the boat's world. Then z += +0x254.
inline void camera_pivot( const std::uint8_t* cam, const camera_boat_t& boat, const float* m, float* p )
{
    if ( !boat.destroyed || boat.debris_count <= 0 )
    {
        const float x = state_f( cam, 0x244 ), y = state_f( cam, 0x248 ), z = state_f( cam, 0x24C );
        p[ 0 ] = ( ( x * m[ 0 ] + y * m[ 4 ] ) + z * m[ 8 ] ) + m[ 12 ];
        p[ 1 ] = ( ( x * m[ 1 ] + y * m[ 5 ] ) + z * m[ 9 ] ) + m[ 13 ];
        p[ 2 ] = ( ( x * m[ 2 ] + y * m[ 6 ] ) + z * m[ 10 ] ) + m[ 14 ];
    }
    else
    {
        const std::uint8_t* q = boat.piece;
        const float x = state_f( q, 0x1B0 ), y = state_f( q, 0x1B4 ), z = state_f( q, 0x1B8 );
        p[ 0 ] = ( ( state_f( q, 0x18 ) * y + state_f( q, 0x08 ) * x ) + state_f( q, 0x28 ) * z ) + state_f( q, 0x38 );
        p[ 1 ] = ( ( state_f( q, 0x0C ) * x + state_f( q, 0x1C ) * y ) + state_f( q, 0x2C ) * z ) + state_f( q, 0x3C );
        p[ 2 ] = ( ( state_f( q, 0x10 ) * x + state_f( q, 0x20 ) * y ) + state_f( q, 0x30 ) * z ) + state_f( q, 0x40 );
    }
    p[ 3 ] = 0.0f;   // stale stack in retail
    p[ 2 ] = state_f( cam, 0x254 ) + p[ 2 ];
}

// The smoothed velocity (+0x2D4): snapped (+0x2C4) to the body's +0x140, else
// b = minss(dt x 4, 1) and v = (1 - b) v + body x b per lane; w = the body's.
inline void camera_velocity( std::uint8_t* cam, const camera_boat_t& boat, float dt )
{
    const float* bv = boat.velocity;
    if ( cam[ 0x2C4 ] )
    {
        std::memcpy( cam + 0x2D4, bv, 16 );
        return;
    }
    const float b = cam_minss( dt * 4.0f, 1.0f );
    const float k = 1.0f - b;
    const float x = k * state_f( cam, 0x2D4 ) + bv[ 0 ] * b;
    const float y = k * state_f( cam, 0x2D8 ) + bv[ 1 ] * b;
    const float z = state_f( cam, 0x2DC ) * k + bv[ 2 ] * b;
    state_put_f( cam, 0x2D4, x );
    state_put_f( cam, 0x2D8, y );
    state_put_f( cam, 0x2DC, z );
    state_put_f( cam, 0x2E0, bv[ 3 ] );
}

// (engine owner +0x470 x (engine +0x0C x engine +0x04)) - the top speed the
// lag divides by (x -0.44704 / 0.44704: mph to m/s).
inline float camera_top( const camera_boat_t& boat )
{
    return boat.engine_owner_470 * ( state_f( boat.engine, 0x0C ) * state_f( boat.engine, 0x04 ) );
}

// 0x409D00 (__userpurge: ecx the camera, xmm1 dt; eye*, pivot*, up*, near*;
// retn 10h), VuBoatCamera_update - the chase mode.
inline void camera_chase( std::uint8_t* cam, const camera_boat_t& boat, float dt, float* eye, float* pivot, float* up, float& near_plane )
{
    float m[ 16 ];
    std::memcpy( m, boat.world, 64 );
    camera_pivot( cam, boat, m, pivot );
    const float dot = ( m[ 5 ] * state_f( cam, 0x2D8 ) + m[ 4 ] * state_f( cam, 0x2D4 ) ) + state_f( cam, 0x2DC ) * m[ 6 ];
    if ( -4.4703999f > dot || 0.5f > m[ 10 ] )
        state_put_f( cam, 0x304, 0.0f );
    else
    {
        const float r = state_f( cam, 0x304 );
        state_put_f( cam, 0x304, cam_minss( ( 1.0f - r ) * dt + r, 1.0f ) );
    }
    // (retail clamps -m[2] to [-1, 1] into a stack slot the pitch's fsincos overwrites - dead)
    const float ideal = cam_atan2( m[ 1 ], m[ 0 ] ) - 1.5707964f;
    const float cur = cam_atan2( state_f( cam, 0x2A8 ) - pivot[ 1 ], state_f( cam, 0x2A4 ) - pivot[ 0 ] );
    if ( cam[ 0x2C4 ] )
        state_put_f( cam, 0x2C8, ideal );
    else
    {
        const float t = ( ( ideal - cur ) + 3.1415927f ) * 0.15915494f;
        const float k = static_cast< float >( cam_fistp_half( t ) );
        state_put_f( cam, 0x2C8, ( ( ( t - k ) * 6.2831855f - 3.1415927f ) * state_f( cam, 0x304 ) ) + cur );
    }
    float cy, sy, cp, sp;
    x87_sincos( state_f( cam, 0x2C8 ), &cy, &sy );
    x87_sincos( state_f( cam, 0x25C ) * 0.017453292f + 1.5707964f, &cp, &sp );
    const float ux = sp * cy, uy = sp * sy;
    const float dist = state_f( cam, 0x258 );
    state_put_f( cam, 0x2A4, pivot[ 0 ] + ux * dist );
    state_put_f( cam, 0x2A8, pivot[ 1 ] + uy * dist );
    state_put_f( cam, 0x2AC, pivot[ 2 ] + cp * dist );
    state_put_f( cam, 0x2B0, 0.0f );   // stale stack in retail
    const bool snap = cam[ 0x2C4 ] != 0;
    camera_velocity( cam, boat, dt );
    const float top = camera_top( boat );
    const float lag = state_f( cam, 0x264 ) * g_camera_ours_zoom;
    const float scale = lag / ( top * -0.44703999f );
    const float vx = state_f( cam, 0x2D4 ), vy = state_f( cam, 0x2D8 ), vz = state_f( cam, 0x2DC );
    const float speed = std::sqrt( ( vx * vx + vy * vy ) + vz * vz );
    const float mag = ( speed * lag ) / ( top * 0.44703999f );
    const float r = state_f( cam, 0x304 );
    const float k = 1.0f - r;
    const float lz = k * ( vz * scale ) + r * ( vz * scale );
    eye[ 0 ] = ( ( vx * scale ) * k + ( mag * ux ) * r ) + state_f( cam, 0x2A4 );
    eye[ 1 ] = state_f( cam, 0x2A8 ) + ( ( vy * scale ) * k + ( uy * mag ) * r );
    eye[ 2 ] = state_f( cam, 0x2AC ) + lz;
    eye[ 3 ] = snap ? 0.0f : boat.velocity[ 3 ];   // snapped: stale stack in retail
    eye[ 2 ] = eye[ 2 ] + lz;
    camera_orient( eye, pivot, state_f( cam, 0x2CC ), up );
    near_plane = 0.5f;
}

// 0x40A4D0 (as 0x409D00), VuBoatCamera_updateVariant: no ramp; the yaw snaps
// to atan2(m1, m0) - pi/2 or follows the last ideal eye (atan2 of it less the
// pivot); the eye is the ideal eye plus the velocity x the lag scale (its z
// added twice); the roll is +0x2CC x pi/2.
inline void camera_variant( std::uint8_t* cam, const camera_boat_t& boat, float dt, float* eye, float* pivot, float* up, float& near_plane )
{
    float m[ 16 ];
    std::memcpy( m, boat.world, 64 );
    camera_pivot( cam, boat, m, pivot );
    const float yaw = cam[ 0x2C4 ] ? cam_atan2( m[ 1 ], m[ 0 ] ) - 1.5707964f
                                   : cam_atan2( state_f( cam, 0x2A8 ) - pivot[ 1 ], state_f( cam, 0x2A4 ) - pivot[ 0 ] );
    const float pitch = state_f( cam, 0x25C ) * 0.017453292f + 1.5707964f;
    const float dist = state_f( cam, 0x258 );
    state_put_f( cam, 0x2C8, yaw );
    float cy, sy, cp, sp;
    x87_sincos( yaw, &cy, &sy );
    x87_sincos( pitch, &cp, &sp );
    state_put_f( cam, 0x2A4, pivot[ 0 ] + ( dist * cy ) * sp );
    state_put_f( cam, 0x2A8, ( dist * sy ) * sp + pivot[ 1 ] );
    state_put_f( cam, 0x2AC, pivot[ 2 ] + dist * cp );
    state_put_f( cam, 0x2B0, 0.0f );   // stale stack in retail
    const bool snap = cam[ 0x2C4 ] != 0;
    camera_velocity( cam, boat, dt );
    const float scale = ( state_f( cam, 0x264 ) * g_camera_ours_zoom ) / ( camera_top( boat ) * -0.44703999f );
    const float lz = state_f( cam, 0x2DC ) * scale;
    eye[ 0 ] = scale * state_f( cam, 0x2D4 ) + state_f( cam, 0x2A4 );
    eye[ 1 ] = state_f( cam, 0x2A8 ) + state_f( cam, 0x2D8 ) * scale;
    eye[ 2 ] = state_f( cam, 0x2AC ) + lz;
    eye[ 3 ] = snap ? 0.0f : boat.velocity[ 3 ];   // snapped: stale stack in retail
    eye[ 2 ] = lz + eye[ 2 ];
    camera_orient( eye, pivot, state_f( cam, 0x2CC ) * 1.5707964f, up );
    near_plane = 0.5f;
}

// 0x40AAC0 (VuBoatCamera_readCamOffset, offset +0x270, +, near 0.5), 0x40AC10
// (+0x280, +, 0.25) and 0x40AD60 (+0x290, -, 0.5) (thiscall camera; eye*,
// target*, up*, near*; retn 10h): the eye = the offset placed by the boat's
// world, the target = the eye +/- its row 1, up = its row 2 (16 b).
inline void camera_fixed( const std::uint8_t* cam, const camera_boat_t& boat, int at, bool minus, float near_value, float* eye, float* target, float* up, float& near_plane )
{
    const float* m = boat.world;
    const float x = state_f( cam, at ), y = state_f( cam, at + 4 ), z = state_f( cam, at + 8 );
    eye[ 0 ] = ( ( m[ 4 ] * y + x * m[ 0 ] ) + m[ 8 ] * z ) + m[ 12 ];
    eye[ 1 ] = ( ( m[ 1 ] * x + m[ 5 ] * y ) + m[ 9 ] * z ) + m[ 13 ];
    eye[ 2 ] = ( ( m[ 2 ] * x + m[ 6 ] * y ) + m[ 10 ] * z ) + m[ 14 ];
    eye[ 3 ] = 0.0f;   // stale stack in retail
    for ( int k = 0; k < 3; ++k )
        target[ k ] = minus ? eye[ k ] - m[ 4 + k ] : eye[ k ] + m[ 4 + k ];
    target[ 3 ] = 0.0f;   // stale stack in retail
    std::memcpy( up, m + 8, 16 );
    near_plane = near_value;
}

// OURS, NOT RETAIL: the user's Video settings; at their defaults every product below is exact, and the camera is retail's.
// Shake: scales the amplitude where 0x40AEB0 reads it, so every source that writes +0x2E4 (OnCameraShake, the big splash, the
// event's) follows. Zoom: scales the lag distance +0x264, the only input of the speed and falling offsets of 0x409D00 and 0x40A4D0.
// Fov: replaces VerticalFOV in the race camera (drive.hxx); the reflection mirrors that camera, the front end keeps ConstantDB's.
// 0x40AEB0 (VuBoatCamera_helper; __userpurge ecx camera, xmm1 dt; eye*,
// target*; retn 8), the shake: s = sin(+0x2F0 x +0x2E8) (x87 - SUBSTITUTION:
// fsin served by x87_sincos's sine on this FPU), v = (+0x2F4.. x +0x2E4) x s,
// scaled by timer / duration while the duration is ordered > the timer; the
// eye moves by v / 2, the target is the eye + the unit view direction - v / 2;
// the timer runs down by dt.
inline void camera_shake( std::uint8_t* cam, float dt, float* eye, float* target )
{
    float c, s;
    x87_sincos( state_f( cam, 0x2F0 ) * state_f( cam, 0x2E8 ), &c, &s );
    const float amp = state_f( cam, 0x2E4 ) * g_camera_ours_shake, duration = state_f( cam, 0x2EC ), timer = state_f( cam, 0x2E8 );
    float vx = ( state_f( cam, 0x2F4 ) * amp ) * s;
    float vy = ( state_f( cam, 0x2F8 ) * amp ) * s;
    float vz = ( state_f( cam, 0x2FC ) * amp ) * s;
    if ( duration > timer )
    {
        const float k = timer / duration;
        vx = vx * k;
        vy = vy * k;
        vz = vz * k;
    }
    g_camera_ours_shake_peak = ( std::max )( g_camera_ours_shake_peak, std::sqrt( vx * vx + vy * vy + vz * vz ) * 0.5f );
    const float dx = target[ 0 ] - eye[ 0 ], dz = target[ 2 ] - eye[ 2 ], dy = target[ 1 ] - eye[ 1 ];
    const float len = std::sqrt( ( dy * dy + dx * dx ) + dz * dz );
    eye[ 0 ] = vx * 0.5f + eye[ 0 ];
    eye[ 1 ] = vy * 0.5f + eye[ 1 ];
    eye[ 2 ] = vz * 0.5f + eye[ 2 ];
    eye[ 3 ] = 0.0f;   // stale stack in retail
    target[ 2 ] = ( eye[ 2 ] + ( 1.0f / len ) * dz ) - vz * 0.5f;
    target[ 1 ] = ( ( 1.0f / len ) * dy + eye[ 1 ] ) - vy * 0.5f;
    target[ 0 ] = ( eye[ 0 ] + ( 1.0f / len ) * dx ) - vx * 0.5f;
    target[ 3 ] = 0.0f;   // stale stack in retail
    state_put_f( cam, 0x2E8, state_f( cam, 0x2E8 ) - dt );
}

// 0x40B090 (stdcall eye*, target*, and a third word never read; retn 0Ch), the
// camera's collision: d = target - eye, side = d x (0,0,1) (both fall back to
// (0,1,0) / (1,0,0) when FLT_MIN is ordered > a square), each normalised; a
// ray from the target to the eye pushed back 1 m: hit with n.d > 0 puts the eye
// at the hit plus d; a ray to eye + side: hit with n.side < 0 pulls the eye in
// by side x (1 - f); a ray to eye - side: hit with n.side > 0 pushes it by the
// same. The eye's w after a first-ray hit is stale stack in retail (written 0).
inline void camera_collide( float* eye, const float* target, const camera_env_t& env )
{
    float dx = target[ 0 ] - eye[ 0 ], dy = target[ 1 ] - eye[ 1 ], dz = target[ 2 ] - eye[ 2 ];
    const float e0 = eye[ 0 ], e1 = eye[ 1 ], e2 = eye[ 2 ];
    float sz = dx * 0.0f - dy * 0.0f;
    float sx = dy - dz * 0.0f;
    float sy = dz * 0.0f - dx;
    if ( FLT_MIN > ( dy * dy + dx * dx ) + dz * dz || FLT_MIN > ( sy * sy + sx * sx ) + sz * sz )
    {
        sy = 0.0f; sx = 1.0f; sz = 0.0f;
        dz = 0.0f; dy = 1.0f; dx = 0.0f;
    }
    const float ls = std::sqrt( ( sy * sy + sx * sx ) + sz * sz );
    const float s0 = ( 1.0f / ls ) * sx, s1 = ( 1.0f / ls ) * sy, s2 = ( 1.0f / ls ) * sz;
    const float ld = std::sqrt( ( dy * dy + dx * dx ) + dz * dz );
    const float n0 = ( 1.0f / ld ) * dx, n1 = ( 1.0f / ld ) * dy, n2 = ( 1.0f / ld ) * dz;
    const float p0 = e0 - n0, p1 = e1 - n1, p2 = e2 - n2;
    ai_ray_result_t r;
    std::memset( r.normal, 0, sizeof( r.normal ) );
    {
        const float to[ 3 ] = { p0, p1, p2 };
        r.hit = false; r.body = nullptr; r.fraction = 1.0f;
        camera_ray( r, target, to, env );
        if ( r.hit && ( r.normal[ 1 ] * n1 + r.normal[ 0 ] * n0 ) + r.normal[ 2 ] * n2 > 0.0f )
        {
            const float f = r.fraction;
            const float x = ( ( 1.0f - f ) * target[ 0 ] + p0 * f ) + n0;
            const float y = ( target[ 1 ] * ( 1.0f - f ) + p1 * f ) + n1;
            const float z = ( ( 1.0f - f ) * target[ 2 ] + p2 * f ) + n2;
            eye[ 0 ] = x; eye[ 1 ] = y; eye[ 2 ] = z; eye[ 3 ] = 0.0f;
        }
    }
    {
        const float from[ 3 ] = { eye[ 0 ], eye[ 1 ], eye[ 2 ] };
        const float to[ 3 ] = { s0 + eye[ 0 ], eye[ 1 ] + s1, s2 + eye[ 2 ] };
        r.hit = false; r.body = nullptr; r.fraction = 1.0f;
        camera_ray( r, from, to, env );
        if ( r.hit && ( r.normal[ 1 ] * s1 + r.normal[ 0 ] * s0 ) + r.normal[ 2 ] * s2 < 0.0f )
        {
            const float g = 1.0f - r.fraction;
            const float a1 = s1 * g, a2 = s2 * g;
            eye[ 0 ] = eye[ 0 ] - s0 * g;
            eye[ 1 ] = eye[ 1 ] - a1;
            eye[ 2 ] = eye[ 2 ] - a2;
        }
    }
    {
        const float from[ 3 ] = { eye[ 0 ], eye[ 1 ], eye[ 2 ] };
        const float to[ 3 ] = { eye[ 0 ] - s0, eye[ 1 ] - s1, eye[ 2 ] - s2 };
        r.hit = false; r.body = nullptr; r.fraction = 1.0f;
        camera_ray( r, from, to, env );
        if ( r.hit && ( r.normal[ 1 ] * s1 + r.normal[ 0 ] * s0 ) + r.normal[ 2 ] * s2 > 0.0f )
        {
            const float g = 1.0f - r.fraction;
            const float z = s2 * g + eye[ 2 ];
            const float y = eye[ 1 ] + s1 * g;
            eye[ 0 ] = s0 * g + eye[ 0 ];
            eye[ 2 ] = z;
            eye[ 1 ] = y;
        }
    }
}

// 0x57CBA0 (thiscall: the frustum inputs at camera +0x184 - eye, forward, up,
// right, near, far, half width / height at near, far / near; out = six planes
// at +0x1E0): near, far, then four side planes from the far corners.
inline void camera_frustum( const float* t, float* out )
{
    const float v3 = t[ 17 ];
    const float v4 = t[ 6 ] * v3, v5 = t[ 4 ] * v3, v6 = t[ 5 ] * v3;
    const float v7 = t[ 18 ] * t[ 20 ];
    const float v8 = t[ 0 ] + v5;
    const float v89 = t[ 8 ] * v7, v81 = t[ 9 ] * v7, v83 = t[ 10 ] * v7;
    const float v10 = t[ 19 ] * t[ 20 ];
    const float v72 = t[ 14 ] * v10;
    const float v11 = t[ 1 ] + v6;
    const float v12 = t[ 12 ] * v10;
    const float v13 = t[ 2 ] + v4;
    const float v75 = t[ 13 ] * v10;
    const float v68 = ( v8 - v12 ) - v89;
    const float v80 = ( v11 - v75 ) - v81;
    const float v67 = ( v13 - v72 ) - v83;
    const float v86 = ( v8 + v12 ) - v89;
    const float v66 = ( v11 + v75 ) - v81;
    const float v78 = ( v13 + v72 ) - v83;
    const float v71 = v89 + ( v8 + v12 );
    const float v70 = v81 + ( v11 + v75 );
    const float v69 = v83 + ( v13 + v72 );
    const float v90 = v89 + ( v8 - v12 );
    const float v82 = v81 + ( v11 - v75 );
    const float v84 = v83 + ( v13 - v72 );
    {
        const float v22 = t[ 16 ];
        const float v23 = t[ 0 ] + t[ 4 ] * v22, v24 = t[ 1 ] + t[ 5 ] * v22, v25 = t[ 2 ] + t[ 6 ] * v22;
        out[ 0 ] = t[ 4 ]; out[ 1 ] = t[ 5 ]; out[ 2 ] = t[ 6 ];
        out[ 3 ] = -( ( t[ 5 ] * v24 + t[ 4 ] * v23 ) + t[ 6 ] * v25 );
    }
    {
        const float v26 = t[ 17 ];
        const float ny = -t[ 5 ], nx = -t[ 4 ], nz = -t[ 6 ];
        const float v27 = ny * ( t[ 5 ] * v26 + t[ 1 ] );
        const float v28 = nx * ( t[ 4 ] * v26 + t[ 0 ] );
        const float v29 = nz * ( t[ 6 ] * v26 + t[ 2 ] );
        out[ 4 ] = nx; out[ 5 ] = ny; out[ 6 ] = nz;
        out[ 7 ] = -( ( v27 + v28 ) + v29 );
    }
    {
        const float v30 = v90 - t[ 0 ], v31 = v82 - t[ 1 ], v32 = v84 - t[ 2 ];
        const float v33 = v68 - t[ 0 ], v34 = v80 - t[ 1 ];
        const float v93 = v34 * v32 - ( v67 - t[ 2 ] ) * v31;
        const float v73 = ( v67 - t[ 2 ] ) * v30 - v33 * v32;
        const float v76 = v33 * v31 - v34 * v30;
        const float v35 = 1.0f / std::sqrt( ( v73 * v73 + v93 * v93 ) + v76 * v76 );
        const float nx = v93 * v35, ny = v73 * v35, nz = v76 * v35;
        out[ 8 ] = nx; out[ 9 ] = ny; out[ 10 ] = nz;
        out[ 11 ] = -( ( t[ 1 ] * ny + t[ 0 ] * nx ) + t[ 2 ] * nz );
    }
    {
        const float v39 = v69 - t[ 2 ], v40 = v70 - t[ 1 ], v41 = v71 - t[ 0 ];
        const float v42 = v66 - t[ 1 ], v43 = v78 - t[ 2 ], v44 = v86 - t[ 0 ];
        const float v94 = v40 * v43 - v39 * v42;
        const float v74 = v39 * v44 - v41 * v43;
        const float v77 = v41 * v42 - v40 * v44;
        const float v45 = 1.0f / std::sqrt( ( v74 * v74 + v94 * v94 ) + v77 * v77 );
        const float nx = v94 * v45, ny = v74 * v45, nz = v77 * v45;
        out[ 12 ] = nx; out[ 13 ] = ny; out[ 14 ] = nz;
        out[ 15 ] = -( ( ny * t[ 1 ] + nx * t[ 0 ] ) + nz * t[ 2 ] );
    }
    {
        const float v49 = v84 - t[ 2 ], v50 = v69 - t[ 2 ], v51 = v70 - t[ 1 ], v52 = v71 - t[ 0 ];
        const float v91 = v90 - t[ 0 ], v53 = v82 - t[ 1 ];
        const float v95 = v53 * v50 - v49 * v51;
        const float v85 = v49 * v52 - v91 * v50;
        const float v92 = v91 * v51 - v53 * v52;
        const float v54 = std::sqrt( ( v85 * v85 + v95 * v95 ) + v92 * v92 );
        const float nx = v95 * ( 1.0f / v54 ), ny = v85 * ( 1.0f / v54 ), nz = v92 * ( 1.0f / v54 );
        out[ 16 ] = nx; out[ 17 ] = ny; out[ 18 ] = nz;
        out[ 19 ] = -( ( ny * t[ 1 ] + nx * t[ 0 ] ) + nz * t[ 2 ] );
    }
    {
        const float v56 = v78 - t[ 2 ], v57 = v67 - t[ 2 ], v58 = v80 - t[ 1 ], v59 = v68 - t[ 0 ];
        const float v87 = v86 - t[ 0 ], v60 = v66 - t[ 1 ];
        const float v96 = v60 * v57 - v56 * v58;
        const float v79 = v56 * v59 - v87 * v57;
        const float v88 = v87 * v58 - v60 * v59;
        const float v61 = std::sqrt( ( v79 * v79 + v96 * v96 ) + v88 * v88 );
        const float nx = v96 * ( 1.0f / v61 ), ny = v79 * ( 1.0f / v61 ), nz = v88 * ( 1.0f / v61 );
        out[ 20 ] = nx; out[ 21 ] = ny; out[ 22 ] = nz;
        out[ 23 ] = -( ( ny * t[ 1 ] + nx * t[ 0 ] ) + nz * t[ 2 ] );
    }
}

// 0x50C550 (thiscall camera): the frustum inputs at +0x184 (eye +0x160, forward
// +0x140, up +0x150, right +0x130, near, far, tan x near / aspect, tan x near,
// far / near, and both half sizes x -2 x far), then 0x57CBA0 into +0x1E0.
inline void camera_frustum_update( std::uint8_t* cam )
{
    std::memmove( cam + 0x184, cam + 0x160, 16 );
    std::memmove( cam + 0x194, cam + 0x140, 16 );
    std::memmove( cam + 0x1A4, cam + 0x150, 16 );
    std::memmove( cam + 0x1B4, cam + 0x130, 16 );
    const float near_plane = state_f( cam, 0x178 );
    const float h = state_f( cam, 0x180 ) * near_plane;
    state_put_f( cam, 0x1C4, near_plane );
    std::memcpy( cam + 0x1C8, cam + 0x17C, 4 );
    state_put_f( cam, 0x1D0, h );
    state_put_f( cam, 0x1CC, h / state_f( cam, 0x174 ) );
    const float far_plane = state_f( cam, 0x1C8 );
    state_put_f( cam, 0x1D4, far_plane / state_f( cam, 0x1C4 ) );
    state_put_f( cam, 0x1D8, ( state_f( cam, 0x1CC ) * -2.0f ) * far_plane );
    state_put_f( cam, 0x1DC, ( state_f( cam, 0x1D0 ) * -2.0f ) * far_plane );
    float t[ 23 ], planes[ 24 ];
    std::memcpy( t, cam + 0x184, sizeof( t ) );
    camera_frustum( t, planes );
    std::memcpy( cam + 0x1E0, planes, sizeof( planes ) );
}

// 0x50BBE0 (__userpurge: ecx camera, xmm1 fov, xmm2 aspect, xmm3 near, far on
// the stack; retn 8 - the second word unread): +0x170.. stored, t = tanf(fov x
// 0.5) at +0x180; +0x30 = (1/t, 0, 0, 0) (0, 1/t x aspect, 0, 0) (0, 0, q, -1)
// (0, 0, q x near, 0), q = far / (near - far); +0xB0 = +0x70 x +0x30; 0x50C550.
inline void camera_projection( std::uint8_t* cam, float fov, float aspect, float near_plane, float far_plane )
{
    state_put_f( cam, 0x170, fov );
    state_put_f( cam, 0x178, near_plane );
    state_put_f( cam, 0x17C, far_plane );
    state_put_f( cam, 0x174, aspect );
    const float t = cam_tanf( fov * 0.5f );
    state_put_f( cam, 0x180, t );
    const float inv = 1.0f / t;
    const float q = far_plane / ( near_plane - far_plane );
    const float p[ 16 ] = { inv, 0.0f, 0.0f, 0.0f, 0.0f, inv * aspect, 0.0f, 0.0f, 0.0f, 0.0f, q, -1.0f, 0.0f, 0.0f, q * near_plane, 0.0f };
    std::memcpy( cam + 0x30, p, 64 );
    float a[ 16 ], b[ 16 ], out[ 16 ];
    std::memcpy( a, cam + 0x70, 64 );
    std::memcpy( b, cam + 0x30, 64 );
    hull_multiply( a, out, b );
    std::memcpy( cam + 0xB0, out, 64 );
    camera_frustum_update( cam );
}

// 0x50BDA0 (thiscall camera; eye*, target*, up*; retn 0Ch): d = target - eye,
// r = up x d ... in retail's lanes (r = (u.z dy - u.y dz, u.x dz - u.z dx, u.y dx
// - u.x dy)), u' = r x d; when 2^-23 is ordered > |d|^2, |r|^2 or |u'|^2: r =
// (1,0,0), d = (0,1,0), u' = (0,0,1). Normalised. +0xF0 = the identity with
// (r, u', -d, eye); +0x70 = it inverted (0x4157C0); +0xB0 = +0x70 x +0x30; +0x00
// = eye, +0x10 = target (16 b each); +0x130 = the identity with (r, d, u', eye);
// 0x50C550.
inline void camera_view( std::uint8_t* cam, const float* eye, const float* target, const float* up )
{
    const float dy = target[ 1 ] - eye[ 1 ], dz = target[ 2 ] - eye[ 2 ], dx = target[ 0 ] - eye[ 0 ];
    float rx = up[ 2 ] * dy - up[ 1 ] * dz;
    float ry = up[ 0 ] * dz - up[ 2 ] * dx;
    float rz = up[ 1 ] * dx - up[ 0 ] * dy;
    float ux = ry * dz - rz * dy;
    float uy = rz * dx - rx * dz;
    float uz = rx * dy - ry * dx;
    float fx = dx, fy = dy, fz = dz;
    const float eps = 1.1920929e-07f;
    if ( eps > ( dy * dy + dx * dx ) + dz * dz || eps > ( ry * ry + rx * rx ) + rz * rz || eps > ( uy * uy + ux * ux ) + uz * uz )
    {
        rx = 1.0f; ry = 0.0f; rz = 0.0f;
        fx = 0.0f; fy = 1.0f; fz = 0.0f;
        ux = 0.0f; uy = 0.0f; uz = 1.0f;
    }
    const float lr = std::sqrt( ( ry * ry + rx * rx ) + rz * rz );
    rx = ( 1.0f / lr ) * rx; ry = ( 1.0f / lr ) * ry; rz = ( 1.0f / lr ) * rz;
    const float lf = std::sqrt( ( fy * fy + fx * fx ) + fz * fz );
    fx = ( 1.0f / lf ) * fx; fy = ( 1.0f / lf ) * fy; fz = ( 1.0f / lf ) * fz;
    const float lu = std::sqrt( ( uy * uy + ux * ux ) + uz * uz );
    ux = ( 1.0f / lu ) * ux; uy = ( 1.0f / lu ) * uy; uz = ( 1.0f / lu ) * uz;
    float m[ 16 ];
    vu_identity( m );
    m[ 0 ] = rx; m[ 1 ] = ry; m[ 2 ] = rz;
    m[ 4 ] = ux; m[ 5 ] = uy; m[ 6 ] = uz;
    m[ 8 ] = -fx; m[ 9 ] = -fy; m[ 10 ] = -fz;
    m[ 12 ] = eye[ 0 ]; m[ 13 ] = eye[ 1 ]; m[ 14 ] = eye[ 2 ];
    std::memcpy( cam + 0xF0, m, 64 );
    hull_invert( m );
    std::memcpy( cam + 0x70, m, 64 );
    float p[ 16 ], out[ 16 ];
    std::memcpy( p, cam + 0x30, 64 );
    hull_multiply( m, out, p );
    std::memcpy( cam + 0xB0, out, 64 );
    std::memcpy( cam + 0x00, eye, 16 );
    std::memcpy( cam + 0x10, target, 16 );
    vu_identity( m );
    m[ 0 ] = rx; m[ 1 ] = ry; m[ 2 ] = rz;
    m[ 4 ] = fx; m[ 5 ] = fy; m[ 6 ] = fz;
    m[ 8 ] = ux; m[ 9 ] = uy; m[ 10 ] = uz;
    m[ 12 ] = eye[ 0 ]; m[ 13 ] = eye[ 1 ]; m[ 14 ] = eye[ 2 ];
    std::memcpy( cam + 0x130, m, 64 );
    camera_frustum_update( cam );
}

// VuDB_readVerticalFOV 0x4D2C60 (ecx g_vugameutil 0x758FF8; xmm0 out): the
// display's w / h (cvtdq2ps, divss); the value is an int (type 1, cvtdq2ps),
// a float (2), an int64 (7, fild / fstp) or else 0; 2 x atanf(tanf(v x
// 0.017453292 x 0.5) x aspect).
inline float camera_vertical_fov( const camera_env_t& env )
{
    const float aspect = static_cast< float >( env.display_w ) / static_cast< float >( env.display_h );
    const camera_db_value_t& v = env.vertical_fov;
    float x = 0.0f;
    if ( v.type == 1 )
        x = static_cast< float >( static_cast< std::int32_t >( static_cast< std::uint32_t >( v.payload ) ) );
    else if ( v.type == 2 )
    {
        const std::uint32_t lo = static_cast< std::uint32_t >( v.payload );
        std::memcpy( &x, &lo, 4 );
    }
    else if ( v.type == 7 )
        x = static_cast< float >( static_cast< std::int64_t >( v.payload ) );
    return cam_atanf( cam_tanf( ( x * 0.017453292f ) * 0.5f ) * aspect ) * 2.0f;
}

// VuBoatCamera_tick 0x409900 (__usercall: ecx the camera, xmm1 dt). The mode:
// the driver's vt+0x34 -> the variant 0x40A4D0; else the look-back byte +0x2D0
// -> 0x40AD60; else with the control method 2, +0x2A0 picks 0 the chase
// 0x409D00, 1 0x40AAC0, 2 0x40AC10 (any other leaves the four outputs unset -
// UNKNOWN: false, nothing written); else the variant. The shake 0x40AEB0 while
// +0x2E8 > 0 (ordered), the collision 0x40B090, the projection 0x50BBE0
// (readVerticalFOV, the display's w / h, the mode's near, the boat's +0x408),
// the view 0x50BDA0; then +0x20 = the body's velocity (16 b), +0x2B4 = the eye
// (16 b), +0x2C4 = 0.
inline bool boat_camera_tick( std::uint8_t* cam, const camera_boat_t& boat, const camera_env_t& env, float dt )
{
    float eye[ 4 ] = { }, target[ 4 ] = { }, up[ 4 ] = { };
    float near_plane = 0.0f;
    std::int32_t mode;
    std::memcpy( &mode, cam + 0x2A0, 4 );
    if ( env.driver_34 )
        camera_variant( cam, boat, dt, eye, target, up, near_plane );
    else if ( cam[ 0x2D0 ] )
        camera_fixed( cam, boat, 0x290, true, 0.5f, eye, target, up, near_plane );
    else if ( env.control_method == 2 )
    {
        if ( mode == 0 )
            camera_chase( cam, boat, dt, eye, target, up, near_plane );
        else if ( mode == 1 )
            camera_fixed( cam, boat, 0x270, false, 0.5f, eye, target, up, near_plane );
        else if ( mode == 2 )
            camera_fixed( cam, boat, 0x280, false, 0.25f, eye, target, up, near_plane );
        else
            return false;
    }
    else
        camera_variant( cam, boat, dt, eye, target, up, near_plane );
    if ( state_f( cam, 0x2E8 ) > 0.0f )
        camera_shake( cam, dt, eye, target );
    camera_collide( eye, target, env );
    const float aspect = static_cast< float >( env.display_w ) / static_cast< float >( env.display_h );
    const float fov = camera_vertical_fov( env );
    camera_projection( cam, fov, aspect, near_plane, boat.far_plane );
    camera_view( cam, eye, target, up );
    std::memcpy( cam + 0x20, boat.velocity, 16 );
    std::memcpy( cam + 0x2B4, eye, 16 );
    cam[ 0x2C4 ] = 0;
    return true;
}
