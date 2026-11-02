#pragma once

// VuForceAffectorEntity (vtable 0x73A898, getType 0x4391B0 -> 0x7363D0; ctor 0x439300; deleting dtor 0x439750) - a
// cylinder volume that pushes rigid bodies along its z axis. Not authored by Hydroelectric (game reach).
// Verified against retail: hydro checks/logic.hxx verify_force_affector (case force_affector).
//
// 0x439DD0 is this class's transform watcher (the ctor's only reference, 0x439498): force_affector_watch.
// The ctor: +0x5C a VuDynamicsStepCallback (vtable 0x73A88C: slot 1 0x439970 = the step), +0x60 the script component,
// +0x64 a Vu3dLayoutComponent (0x50B8D0), +0x68 "Initially Active" (1), +0x6C "Acceleration" (98.06, 0x42C41EB8), +0x70
// "Max Speed" (20); a btBoxShape at +0x80 (0x547D80, half extents from 0x6F7B00), a btPairCachingGhostObject at +0xD0
// (0x548E00; its world transform +0xE0 .. +0x11F, its overlapping objects +0x1E4 count / +0x1EC data), +0x210 the active
// byte (0). Inputs "Activate" 0x439C40 (-> 0x439CB0) / "Deactivate" 0x439C60.
// Hooks (the dynamics: Bullet's world and Vu's step-callback list are the physics layer's, not this file's):
//   g_vudynamicsimpl 0x75900C vt+0x6C -> the collision world: vt+0x1C addCollisionObject( ghost, 512, 466 ), vt+0x20
//   removeCollisionObject( ghost ); g_vudynamicsimpl vt+0x2C / vt+0x30 add / remove the step callback (entity + 0x5C);
//   the box's vt+0x14 setLocalScaling( transform +0xB0 xyz, 0 ); g_vutickmanager 0x759008 vt+0x20 ( {0x73A84C, this,
//   0x439ED0, 0}, "Build" ) / vt+0x28. The Build tick 0x439ED0 runs 0x43A0A0 only while active and the dev byte
//   0x758F93 is set (a dev draw: NOT RECREATED, counted).

#include <workspace/core/boat/hull.hxx>
#include <workspace/core/world/wave.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>

// One overlapping object of the ghost, as the step reads and writes it (a btCollisionObject / btRigidBody image).
struct force_body_t
{
    std::uint32_t type = 0;          // +0xF4: 2 = a rigid body
    float position[ 3 ] = { };       // +0x40: the world transform's origin
    float velocity[ 3 ] = { };       // +0x140: the linear velocity
    float inverse_mass = 0.0f;       // +0x160
    float factor[ 3 ] = { };         // +0x170: the linear factor
    float force[ 3 ] = { };          // +0x1B0: the total force (accumulated)
};

struct force_affector_t
{
    std::uint8_t initially_active = 1;   // +0x68
    float acceleration = 98.06f;         // +0x6C (0x42C41EB8)
    float max_speed = 20.0f;             // +0x70
    // +0xE0: the ghost's btTransform (rows = the Vu matrix's columns); the ghost ctor 0x548E00 leaves the identity basis.
    float ghost[ 16 ] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    std::uint8_t active = 0;             // +0x210
};

struct force_affector_hooks_t
{
    void* user = nullptr;
    void ( *set_scaling )( void* user, const float* scale ) = nullptr;            // the box's setLocalScaling( xyz, w 0 )
    void ( *add_object )( void* user, std::uint32_t group, std::uint32_t mask ) = nullptr;   // world vt+0x1C ( ghost, .. )
    void ( *remove_object )( void* user ) = nullptr;                              // world vt+0x20 ( ghost )
    void ( *step_callback )( void* user, bool add ) = nullptr;                    // dynamics vt+0x2C / vt+0x30
    void ( *tick_register )( void* user, bool on ) = nullptr;                     // "Build" vt+0x20 / vt+0x28
    void ( *update_aabb )( void* user ) = nullptr;                                // updateSingleAabb 0x54C370 ( the world vt+0x6C, the ghost )
    int* unrecreated = nullptr;
};

// 0x410C30 (VuMatrix -> btTransform): rows (m0 m4 m8 0), (m1 m5 m9 0), (m2 m6 m10 0), origin (m12 m13 m14 0) - each w lane
// zeroed on the stack just before its 8-byte copy.
inline void force_bt_from_vu( float* bt, const float* m )
{
    for ( int r = 0; r < 3; ++r )
    {
        bt[ 4 * r + 0 ] = m[ r ];
        bt[ 4 * r + 1 ] = m[ 4 + r ];
        bt[ 4 * r + 2 ] = m[ 8 + r ];
        bt[ 4 * r + 3 ] = 0.0f;
    }
    bt[ 12 ] = m[ 12 ];
    bt[ 13 ] = m[ 13 ];
    bt[ 14 ] = m[ 14 ];
    bt[ 15 ] = 0.0f;
}

// 0x410B40 (btTransform -> VuMatrix): row i = (bt[i], bt[4 + i], bt[8 + i], 0), row 3 = (origin, 1).
inline void force_vu_from_bt( float* m, const float* bt )
{
    for ( int r = 0; r < 3; ++r )
    {
        m[ 4 * r + 0 ] = bt[ r ];
        m[ 4 * r + 1 ] = bt[ 4 + r ];
        m[ 4 * r + 2 ] = bt[ 8 + r ];
        m[ 4 * r + 3 ] = 0.0f;
    }
    m[ 12 ] = bt[ 12 ];
    m[ 13 ] = bt[ 13 ];
    m[ 14 ] = bt[ 14 ];
    m[ 15 ] = 1.0f;
}

// "Activate" 0x439C40 -> 0x439CB0 (VuRetVal void): when inactive: the box scaled by the transform's +0xB0 (w 0), the
// ghost's transform = 0x410C30( transform +0x50 ), the ghost added (group 512, mask 466), the step callback added, active.
inline void force_affector_activate( force_affector_t& e, const float* transform, const float* scale, const force_affector_hooks_t& h )
{
    if ( e.active )
        return;
    const float s[ 4 ] = { scale[ 0 ], scale[ 1 ], scale[ 2 ], 0.0f };
    if ( h.set_scaling )
        h.set_scaling( h.user, s );
    force_bt_from_vu( e.ghost, transform );
    if ( h.add_object )
        h.add_object( h.user, 512u, 466u );
    if ( h.step_callback )
        h.step_callback( h.user, true );
    e.active = 1;
}

// "Deactivate" 0x439C60 (and slot 7's tail): when active: the ghost removed, the step callback removed, inactive.
// The transform watcher 0x439DD0: active, the ghost follows the world (0x410C30), the box the scale, then the ghost's aabb.
inline void force_affector_watch( force_affector_t& e, const float* transform, const float* scale, const force_affector_hooks_t& h )
{
    if ( !e.active )
        return;
    force_bt_from_vu( e.ghost, transform );
    const float s[ 4 ] = { scale[ 0 ], scale[ 1 ], scale[ 2 ], 0.0f };
    h.set_scaling( h.user, s );
    h.update_aabb( h.user );
}

inline void force_affector_deactivate( force_affector_t& e, const force_affector_hooks_t& h )
{
    if ( !e.active )
        return;
    if ( h.remove_object )
        h.remove_object( h.user );
    if ( h.step_callback )
        h.step_callback( h.user, false );
    e.active = 0;
}

// Slot 6 0x4398C0: the "Build" tick registered; Initially Active -> 0x439CB0.
inline void force_affector_game_init( force_affector_t& e, const float* transform, const float* scale, const force_affector_hooks_t& h )
{
    if ( h.tick_register )
        h.tick_register( h.user, true );
    if ( e.initially_active )
        force_affector_activate( e, transform, scale, h );
}

// Slot 7 0x439920: the tick unregistered, then Deactivate's body.
inline void force_affector_game_release( force_affector_t& e, const force_affector_hooks_t& h )
{
    if ( h.tick_register )
        h.tick_register( h.user, false );
    force_affector_deactivate( e, h );
}

// The "Build" tick 0x439ED0.
inline void force_affector_build( const force_affector_t& e, bool dev_draw, const force_affector_hooks_t& h )
{
    if ( e.active && dev_draw && h.unrecreated )
        ++*h.unrecreated;   // 0x43A0A0
}

// The step 0x439970 (the callback's slot 1; dt unused): with overlapping objects: W = fromScale( the box's local scaling,
// `scaling` = its vt+0x18 answer ) x 0x410B40( ghost ) (0x407DB0 / 0x4078C0), inverted in place (0x4157C0); for each
// object of type 2: p = W^-1 . origin; when 1 >= |p.z| (comiss: NaN fails), 1 > p.y^2 + p.x^2 and Max Speed > v . a (a =
// the UNSCALED transform's row 2; (v.y a.y + a.x v.x) + v.z a.z): k = Acceleration / inverse mass, force += factor x (a k).
inline void force_affector_step( const force_affector_t& e, const float* scaling, force_body_t* bodies, int count )
{
    if ( count == 0 )
        return;
    float m[ 16 ], s[ 16 ], w[ 16 ];
    force_vu_from_bt( m, e.ghost );
    wave_matrix_scale( s, scaling );
    hull_multiply( s, w, m );
    hull_invert( w );
    const float ax = m[ 8 ], ay = m[ 9 ], az = m[ 10 ];
    for ( int i = 0; i < count; ++i )
    {
        force_body_t& b = bodies[ i ];
        if ( b.type != 2u )
            continue;
        const float px = b.position[ 0 ], py = b.position[ 1 ], pz = b.position[ 2 ];
        const float x = ( ( w[ 4 ] * py + w[ 0 ] * px ) + w[ 8 ] * pz ) + w[ 12 ];
        const float y = ( ( w[ 5 ] * py + w[ 1 ] * px ) + w[ 9 ] * pz ) + w[ 13 ];
        const float z = ( ( w[ 6 ] * py + w[ 2 ] * px ) + w[ 10 ] * pz ) + w[ 14 ];
        if ( !( 1.0f >= std::fabs( z ) ) )
            continue;
        if ( !( 1.0f > y * y + x * x ) )
            continue;
        const float dot = ( b.velocity[ 1 ] * ay + ax * b.velocity[ 0 ] ) + b.velocity[ 2 ] * az;
        if ( !( e.max_speed > dot ) )
            continue;
        const float k = e.acceleration / b.inverse_mass;
        const float fy = ay * k, fx = ax * k, fz = az * k;
        b.force[ 0 ] = fx * b.factor[ 0 ] + b.force[ 0 ];
        b.force[ 1 ] = b.factor[ 1 ] * fy + b.force[ 1 ];
        b.force[ 2 ] = b.factor[ 2 ] * fz + b.force[ 2 ];
    }
}
