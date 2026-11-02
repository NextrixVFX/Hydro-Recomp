#pragma once

#include <workspace/core/collision/world.hxx>
#include <workspace/core/dynamics/manager.hxx>
#include <workspace/core/dynamics/step.hxx>
#include <workspace/core/dynamics/writeback.hxx>
#include <workspace/core/world/transform.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>

// The race's dynamics world, the frame side: what VuDynamicsImpl (vtable
// 0x74C044, g_vudynamicsimpl 0x75900C) and its VuDynamicsWorldImpl (0x74F9BC)
// do around the step, recreated from retail's disassembly (world batch 15).
// The step itself is the engine's verified world_step_simulation 0x574C30
// (dynamics/step.hxx) over Bullet's recreated stages.

// ---- btDiscreteDynamicsWorld::synchronizeMotionStates 0x600B00 -------------
//
// A Bullet function the race reaches through DynamicsSync 0x4F8A20 (world slot
// 18) - absent from the engine and blocking (no body's motion reaches its
// entity without it), so recreated HERE and verified (case sync_motion_states).
// Its CProfileSample is timing only - not recreated.
//
// The body's motion state (+0x1F4, a Vu object: the boat's is VuBoatEntity's
// +0x5C, setWorldTransform 0x41E460) is the caller's: `has` answers whether a
// body has one (+0x1F4 != 0), `set` is its vt+8 setWorldTransform.
using motion_has_fn = bool ( * )( void* user, const solver_rigid_t& body );
using motion_set_fn = void ( * )( void* user, solver_rigid_t& body, const float* basis, const float* origin );

// synchronizeSingleMotionState 0x600600 (thiscall world; body; retn 4): a
// body with a motion state and flags +0xD8 & 3 == 0 (neither static nor
// kinematic) hands the motion state integrateTransform(its interpolation
// transform +0x50 / +0x80, the interpolation velocities +0x90 / +0xA0,
// localTime (world +0xF0) x hitFraction (+0xFC)) - the inlined integrate is
// writeback.hxx's integrate_transform, operation for operation (read
// 0x600639 .. 0x600AD7: the origin, the clamp 0x6F7684, the Taylor branch
// below 0.001, sinf((a x 0.5) x t) / a, cosf((a x t) x 0.5), getRotation
// 0x50F8E0, the product, the normalise, setRotation; w lanes 0).
inline void world_synchronize_single_motion_state( float local_time, solver_rigid_t& b, motion_has_fn has, motion_set_fn set, void* user )
{
    if ( !has || !has( user, b ) )
        return;
    if ( ( b.collision_flags & 3 ) != 0 )
        return;
    float basis[ 12 ], origin[ 4 ];
    integrate_transform( b.interpolation_basis, b.interpolation_origin, b.interpolation_linear, b.interpolation_angular,
                         b.hit_fraction * local_time, basis, origin );
    if ( set )
        set( user, b, basis, origin );
}

// synchronizeMotionStates 0x600B00 (thiscall world; ret): with the world's
// +0xF6 (m_synchronizeAllMotionStates - 0 from the ctor 0x5FFF40, no writer
// found) every collision object (+8 / +0x10) whose internal type +0xF4 & 2;
// otherwise every non-static rigid body (+0xCC / +0xD4) whose activation state
// +0xE4 is neither 2 nor 5.
inline void world_synchronize_motion_states( world_t& w, bool synchronize_all, motion_has_fn has, motion_set_fn set, void* user )
{
    if ( synchronize_all )
    {
        for ( solver_rigid_t* o : w.objects )
            if ( ( o->internal_type & 2 ) != 0 )
                world_synchronize_single_motion_state( w.local_time, *o, has, set, user );
        return;
    }
    for ( solver_rigid_t* b : w.bodies )
        if ( b->activation_state != 2 && b->activation_state != 5 )
            world_synchronize_single_motion_state( w.local_time, *b, has, set, user );
}

// ---- VuBoatEntity's btMotionState::setWorldTransform 0x41E460 --------------
//
// The boat body's motion state (boat +0x5C, vtable 0x7383C4 slot 2; thiscall;
// const btTransform&; retn 4) - VuBoatEntity code, recreated here because the
// race's synchronizeMotionStates reaches it (flagged for the boat agent): M =
// VuMatrix_multiply(this = translation(-(boat +0x290)) 0x410AB0, the body
// transform as a VuMatrix 0x410B40) - the centre of mass taken back out; the
// transform component's (boat +0x50) world = M and its world euler, then
// 0x534830 and 0x534C00(0): transform_set_world (world/transform.hxx) on the
// component's image (+0x10 local, +0x50 world, +0x90 / +0xA0 the eulers'
// lanes 0 - 2; their fourth lanes are stack words - left). The boat has no
// parent and no children in a race (0x481CE0's boats; the nuisance boats'
// owners are not their parents - 0x42FDC0): `has_parent` true is the
// parent path on an image, NOT modelled - false returned, nothing written.
inline bool boat_motion_set( const std::uint8_t* boat, std::uint8_t* transform, bool has_parent, const float* basis, const float* origin )
{
    if ( has_parent )
        return false;
    float bt[ 16 ], m[ 16 ], tr[ 16 ], out[ 16 ];
    std::memcpy( bt, basis, 48 );
    std::memcpy( bt + 12, origin, 16 );
    hull_vu_from_bt( m, bt );
    float c[ 3 ];
    for ( int k = 0; k < 3; ++k )
    {
        std::memcpy( &c[ k ], boat + 0x290 + 4 * k, 4 );
        c[ k ] = -c[ k ];   // xorps with the sign mask 0x6F7CD0
    }
    hull_translation( tr, c );
    hull_multiply( tr, out, m );
    transform_component_t t{};
    transform_set_world( t, out, nullptr );
    std::memcpy( transform + 0x10, t.local, 64 );
    std::memcpy( transform + 0x50, t.world, 64 );
    std::memcpy( transform + 0x90, t.rotation, 12 );
    std::memcpy( transform + 0xA0, t.world_rotation, 12 );
    return true;
}
// ---- VuDynamicsImpl's frame: DynamicsKick 0x4F8A90 / the thread 0x4F8950 /
//      DynamicsSync 0x4F8A20 / the per-substep callback 0x4F8BC0 / 0x4F8C00 ----
//
// READ from the disassembly (world batch 15; transliterated, NOT oracle-run -
// they are control flow over hooks). Init slot 5 0x4F8050 (from the engine
// init 0x566230) builds the collision configuration, dispatcher, btDbvt
// broadphase, solver, the btGhostPairCallback, the VuDynamicsWorldImpl world
// (+0x34; its +0x110 = this +4, the substep callback interface), the contact
// manager (+0x38, 0x574EC0) and the debug drawer (+0x3C); it registers
// "DynamicsSync" -> 0x4F8A20 and "DynamicsKick" -> 0x4F8A90 with the tick
// manager (g 0x759008 vt+0x20), +0x50 = maxSubSteps, +0x54 = fixedTimeStep,
// and starts the thread 0x4F7CA0 (g 0x759098 vt+0x20). Its arguments come
// from the config block 0x4D9900 builds on its stack: the ctor 0x4D9480's
// defaults (+0xEC = 6, +0xF0 = 0x3C888889 = 1/60) and ONE later write in
// 0x4D9900 (0x4DA484: word 0x101 at +0xE8 - dynamics on, +0xE9 async = 1);
// nothing else in it touches +0xEA .. +0xF3. So: async, 6 substeps, 1/60.
//   Kick(dt): +0x14 = clock - +0x14 (the frame interval: g_vumetrosys vt+0x28,
//     platform; read only by the dev profiler g 0x7590A0), +0x10 = dt, +0xD =
//     1, signal the thread (+0x18); not async -> own slot 25 (the wait).
//   The thread, per signal: the profiler hook (g 0x7590A0 vt+0x2C, dev); if
//     +0x10 > 0: world slot 11 stepSimulation(+0x10, +0x50, +0x54) - the
//     engine's world_step_simulation 0x574C30; the profiler's vt+0x30; signal
//     +0x1C.
//   Sync(dt): own slot 25 (wait on +0x1C while +0xD, then +0xD = 0); +0x14 =
//     the clock; the profiler's vt+0x34; if dt > 0: world slot 18
//     synchronizeMotionStates (above), then the debug drawer's mode (the OR of
//     its enabled +8 entries - dev, nothing enables one: 0) via its vt+0x28.
//   Substep slot 0 0x4F8BC0 (dt, bool) / slot 1 0x4F8C00 (dt): every step
//     callback on +0x48 (list order), its vt+0 (dt, bool) / vt+4 (dt).
// SUBSTITUTION (flagged): retail's step runs on a thread between the Kick and
// the next frame's Sync, concurrently with the phases after DynamicsKick
// (Build, Final, Audio - Build reads the body); here it runs AT the Kick, as
// the non-async configuration orders it. The clock and the profiler are not
// modelled (platform / dev).
//
// The boat's step callback (slot 1 tickThunk 0x41E5E0) is the boat agent's
// builder (boat/thunk.hxx), wired by session.hxx; a step it cannot run is
// logged there (UNKNOWN) and that boat gets no forces that substep.
struct race_step_callback_t
{
    std::function< void( float, bool ) > slot0;
    std::function< void( float ) > slot1;
};

struct race_dynamics_t
{
    vu_dynamics_t impl{};                        // +0xD pending, +0x40 listeners, +0x48 step callbacks
    world_t world;                               // +0x34 (VuDynamicsWorldImpl)
    collision_world_t collision;
    simplex_solver_t simplex{};                  // the collision configuration's (simplex_construct in race_dynamics_init)
    std::uint8_t async = 1;                      // +0xC
    float dt = 0.0f;                             // +0x10
    std::int32_t max_sub_steps = 6;              // +0x50
    float fixed_time_step = 0.0166666675f;       // +0x54 (0x3C888889)
    std::map< void*, race_step_callback_t > callbacks;   // by the +0x48 entries' identity
    std::map< const vu_rigid_body_t*, collision_object_t* > objects;   // the bodies' collision objects
    std::map< const solver_rigid_t*, std::function< void( const float*, const float* ) > > motion;   // +0x1F4's setWorldTransform
    std::string stopped;                         // non-empty: the frame is STOPPED (UNKNOWN)
    int steps = 0, syncs = 0;
};

// The world 0x4F8050 builds, then the game's start-up 0x4024E0's setGravity
// (VuDynamicsImpl slot 6 0x4F8610 -> world slot 16 0x601010) with (0, 0,
// 0xC19CE560 = -19.612) - before any body is added, so only the world's +0xE0
// changes. The contact-added callback (0x574EC0's gContactAddedCallback) is
// the caller's `contact`.
inline void race_dynamics_init( race_dynamics_t& d, const contact_callbacks_t* contact )
{
    collision_world_init( d.collision );
    simplex_construct( d.simplex );   // retail's simplex-solver setup (equal-vertex threshold 1e-4; boat batch 15)
    d.collision.dispatcher.simplex = &d.simplex;
    d.collision.dispatcher.callbacks = contact;
    world_attach_collision( d.world, d.collision );
    std::uint32_t gz = 0xC19CE560u;
    float z;
    std::memcpy( &z, &gz, 4 );
    d.world.gravity[ 0 ] = 0.0f;
    d.world.gravity[ 1 ] = 0.0f;
    d.world.gravity[ 2 ] = z;
    d.world.gravity[ 3 ] = 0.0f;
    d.world.hooks.user = &d;
    d.world.hooks.has_substep_callback = true;   // world +0x110 = this +4 (0x4F82A6)
    d.world.hooks.substep_begin = []( void* u, float dt, bool stepping ) {
        race_dynamics_t& r = *static_cast< race_dynamics_t* >( u );
        for ( void* c : r.impl.step_callbacks )
            if ( r.callbacks[ c ].slot0 )
                r.callbacks[ c ].slot0( dt, stepping );
    };
    d.world.hooks.substep = []( void* u, float dt ) {
        race_dynamics_t& r = *static_cast< race_dynamics_t* >( u );
        for ( void* c : r.impl.step_callbacks )
            if ( r.callbacks[ c ].slot1 )
                r.callbacks[ c ].slot1( dt );
    };
    d.impl.hooks.user = &d;
    d.impl.hooks.sync = []( void*, vu_dynamics_t& v ) { return vu_dynamics_sync( v ); };
    d.impl.hooks.wait = []( void*, std::uint32_t, std::int32_t ) { return true; };   // the step ran at the Kick
    d.impl.hooks.world_add = []( void* u, vu_rigid_body_t& b, std::int32_t group, std::int32_t mask ) {
        race_dynamics_t& r = *static_cast< race_dynamics_t* >( u );
        const auto it = r.objects.find( &b );
        // has_motion: the body's +0x1F4 motion state (the boat's +0x5C; a level prop's is 0).
        return it != r.objects.end( ) &&
               world_add_rigid_body( r.world, r.collision, *it->second, r.motion.count( &b.body ) != 0, static_cast< short >( group ),
                                     static_cast< short >( mask ) ) == 1;
    };
    d.impl.hooks.manifold_count = []( void* u ) { return static_cast< int >( static_cast< race_dynamics_t* >( u )->collision.dispatcher.manifolds.size( ) ); };
    d.impl.hooks.manifold_bodies = []( void* u, int i, vu_rigid_body_t*& b0, vu_rigid_body_t*& b1 ) {
        const solver_manifold_t* m = static_cast< race_dynamics_t* >( u )->collision.dispatcher.manifolds[ static_cast< std::size_t >( i ) ];
        b0 = reinterpret_cast< vu_rigid_body_t* >( m->body0 );   // solver_rigid_t is the image's first 672 bytes
        b1 = reinterpret_cast< vu_rigid_body_t* >( m->body1 );
        return true;
    };
    d.impl.hooks.world_remove = []( void* u, vu_rigid_body_t& b ) {
        race_dynamics_t& r = *static_cast< race_dynamics_t* >( u );
        const auto it = r.objects.find( &b );
        return it != r.objects.end( ) && world_remove_rigid_body( r.world, r.collision, *it->second ) == 1;
    };
}

// DynamicsKick 0x4F8A90 + the thread's step.
inline void race_dynamics_kick( race_dynamics_t& d, float dt )
{
    if ( !d.stopped.empty( ) )
        return;
    d.dt = dt;
    d.impl.pending = 1;
    if ( d.dt > 0.0f )
    {
        spikes::count( spikes::c_substeps, static_cast< std::uint32_t >( world_step_simulation( d.world, d.dt, d.max_sub_steps, d.fixed_time_step ) ) );
        ++d.steps;
        if ( d.world.refused )
            d.stopped = "the world step reached a Bullet path not recreated (world_t::refused)";
    }
    d.impl.pending = 0;   // the thread's signal +0x1C (here: done)
}

// DynamicsSync 0x4F8A20.
inline void race_dynamics_sync( race_dynamics_t& d, float dt )
{
    if ( !d.stopped.empty( ) || !vu_dynamics_sync( d.impl ) )
        return;
    if ( !( dt > 0.0f ) )   // comiss dt, 0; jbe
        return;
    world_synchronize_motion_states( d.world, false,
        []( void* u, const solver_rigid_t& b ) { return static_cast< race_dynamics_t* >( u )->motion.count( &b ) != 0; },
        []( void* u, solver_rigid_t& b, const float* basis, const float* origin ) { static_cast< race_dynamics_t* >( u )->motion[ &b ]( basis, origin ); },
        &d );
    ++d.syncs;
}