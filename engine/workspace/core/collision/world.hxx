#pragma once

#include <memory>
#include <vector>

#include <workspace/core/collision/broadphase.hxx>
#include <workspace/core/collision/dispatch.hxx>
#include <workspace/core/dynamics/step.hxx>

// The collision side of btCollisionWorld / btDiscreteDynamicsWorld (Bullet
// 2.79), recreated from retail's disassembly (memory/audit.md "Batch 8h" /
// "Batch 8j"): addCollisionObject 0x54C1D0 (world slot 7 is a jump to it),
// performDiscreteCollisionDetection 0x54C710 (world slot 9; no Vu override)
// over the pieces verified separately - updateAabbs 0x54C5F0, the
// btDbvtBroadphase, the hashed pair cache with the btGhostPairCallback Vu
// installs, and the dispatcher's nearCallback.

// The world: +8 / +16 the object array, +0x18 the dispatcher, +0x1C the
// dispatchInfo, +0x2C useContinuous, +0x4C the broadphase, +0x50 the drawer,
// +0x54 forceUpdateAllAabbs.
struct collision_world_t
{
    std::vector< collision_object_t* > objects;
    std::unique_ptr< dbvt_broadphase_t > broadphase;
    collision_dispatcher_t dispatcher;
    pair_algorithm_store_t algorithms;
    aabb_world_t aabb;
    dispatch_info_t info;
    int status = 1;
};

inline int collision_world_set_aabb( void* user, unsigned proxy, const float* mn, const float* mx )
{
    collision_world_t& w = *static_cast< collision_world_t* >( user );
    return broadphase_set_aabb( *w.broadphase, w.broadphase->proxies[ proxy ].get( ), mn, mx );
}

inline void collision_world_clean_algorithm( void* user, void* algorithm )
{
    collision_world_t& w = *static_cast< collision_world_t* >( user );
    dispatch_destroy_pair_algorithm( w.dispatcher, w.algorithms, static_cast< pair_algorithm_t* >( algorithm ) );
}

// btPairCachingGhostObject (vtable 0x74DFB4, ctor 0x548E00: internal type 4, the overlapping objects +0x114 count / +0x11C data).
// Slot 6 addOverlappingObjectInternal 0x548FE0: a linear search of the array, push_back when absent, then the same pair into the
// ghost's OWN hashed pair cache (+0x130). Slot 7 removeOverlappingObjectInternal 0x549120: the match swapped with the last. The
// ghost's own cache is read by no recreated path: not modelled.
struct ghost_object_t : collision_object_t
{
    std::vector< collision_object_t* > overlapping;
};

inline void collision_world_ghost_add( void*, void* ghost, broadphase_proxy_t* other, broadphase_proxy_t* )
{
    ghost_object_t& g = *static_cast< ghost_object_t* >( static_cast< collision_object_t* >( ghost ) );
    collision_object_t* o = static_cast< collision_object_t* >( other->client );
    for ( const collision_object_t* x : g.overlapping )
        if ( x == o )
            return;
    g.overlapping.push_back( o );
}

inline void collision_world_ghost_remove( void*, void* ghost, broadphase_proxy_t* other, broadphase_proxy_t* )
{
    ghost_object_t& g = *static_cast< ghost_object_t* >( static_cast< collision_object_t* >( ghost ) );
    const collision_object_t* o = static_cast< collision_object_t* >( other->client );
    for ( std::size_t i = 0; i < g.overlapping.size( ); ++i )
        if ( g.overlapping[ i ] == o )
        {
            g.overlapping[ i ] = g.overlapping.back( );
            g.overlapping.pop_back( );
            return;
        }
}

// The world as Vu 0x4F8050 builds it for the collision stage: the
// btDbvtBroadphase (own pair cache) with the btGhostPairCallback installed;
// the cache's algorithm teardown and the broadphase's setAabb wired here.
inline void collision_world_init( collision_world_t& w )
{
    w.broadphase = std::make_unique< dbvt_broadphase_t >( );
    broadphase_init( *w.broadphase );
    w.broadphase->cache.has_ghost = true;
    w.broadphase->cache.hooks.user = &w;
    w.broadphase->cache.hooks.clean_algorithm = collision_world_clean_algorithm;
    w.broadphase->cache.hooks.ghost_add = collision_world_ghost_add;
    w.broadphase->cache.hooks.ghost_remove = collision_world_ghost_remove;
    w.aabb.user = &w;
    w.aabb.set_aabb = collision_world_set_aabb;
}

// addCollisionObject @0x54C1D0 (thiscall world; object, group, mask; retn
// 0Ch): the object appended (the array doubling), its shape's getAabb (vt+4)
// on the world transform - NO breaking-threshold widening here - and the
// broadphase's createProxy (vt+4: min, max, the shape type, the object,
// group, mask, the dispatcher, 0) stored at +200.
inline int collision_world_add_object( collision_world_t& w, collision_object_t& o, short group, short mask )
{
    w.objects.push_back( &o );
    float mn[ 4 ], mx[ 4 ];
    if ( !body_get_aabb( o.body, o.body.transform, mn, mx ) )
        return -1;
    int status = 1;
    dbvt_proxy_t* p = broadphase_create_proxy( *w.broadphase, mn, mx, &o, o.internal_type, group, mask, status );
    o.proxy = static_cast< unsigned >( p->index );
    o.proxied = true;
    return status;
}

// btAlignedObjectArray::remove @0x50F200 (thiscall array; T const&): the FIRST
// match by a linear search, swapped with the last element, the size
// decremented; no match -> nothing. Parallel arrays get the same swap.
template < typename T > inline int array_remove_index( const std::vector< T >& a, const T& value )
{
    for ( std::size_t i = 0; i < a.size( ); ++i )
        if ( a[ i ] == value )
            return static_cast< int >( i );
    return -1;
}

template < typename T > inline void array_remove_at( std::vector< T >& a, int i )
{
    if ( i < 0 )
        return;
    std::swap( a[ static_cast< std::size_t >( i ) ], a.back( ) );
    a.pop_back( );
}

// btCollisionWorld::removeCollisionObject as 0x601170 / 0x6011E0 inline it:
// with a proxy (+200): the broadphase's pair cache (vt+0x24) cleanProxyFromPairs
// (vt+0x24: proxy, dispatcher - each pair's algorithm torn down), the
// broadphase's destroyProxy (vt+8: proxy, dispatcher), +200 = 0; then the
// object out of the world's array (+4, remove 0x50F200).
inline int collision_world_remove_object( collision_world_t& w, collision_object_t& o )
{
    if ( o.proxied )
    {
        dbvt_proxy_t* p = w.broadphase->proxies[ o.proxy ].get( );
        if ( pair_cache_clean_proxy( w.broadphase->cache, p ) < 0 )
            return -1;
        if ( broadphase_destroy_proxy( *w.broadphase, p ) < 0 )
            return -1;
        o.proxied = false;
    }
    array_remove_at( w.objects, array_remove_index( w.objects, &o ) );
    return 1;
}

// The cache walk dispatchAllCollisionPairs 0x56D7D0 makes: processAll
// OverlappingPairs with the btCollisionPairCallback - each pair's clients'
// nearCallback, never a removal.
inline int collision_world_dispatch_pairs( collision_world_t& w )
{
    return pair_cache_process_all( w.broadphase->cache, [ &w ]( broadphase_pair_t& p ) {
        collision_object_t& o0 = *static_cast< collision_object_t* >( p.proxy0->client );
        collision_object_t& o1 = *static_cast< collision_object_t* >( p.proxy1->client );
        pair_algorithm_t* slot = static_cast< pair_algorithm_t* >( p.algorithm );
        const int r = dispatch_near( w.dispatcher, w.algorithms, o0, o1, slot, w.info );
        p.algorithm = slot;
        return r < 0 ? -1 : 0;
    } );
}

// performDiscreteCollisionDetection @0x54C710 (profiled): updateAabbs
// (world vt+4), the broadphase's calculateOverlappingPairs (vt+28,
// dispatcher), and - with a dispatcher - dispatchAllCollisionPairs (vt+28)
// over the broadphase's pair cache (vt+36) with the dispatchInfo.
inline int collision_world_perform_discrete( collision_world_t& w )
{
    if ( update_aabbs( w.aabb, w.objects ) < 0 )
        return -1;
    if ( broadphase_calculate_overlapping_pairs( *w.broadphase ) < 0 )
        return -1;
    return collision_world_dispatch_pairs( w );
}

// --- The dynamics world's side -------------------------------------------
// Retail has ONE object per body (the btRigidBody is the btCollisionObject);
// here the dynamics reads solver_rigid_t and the collision code reads
// collision_object_t (body.object = the solver_rigid_t, body.transform = its
// +16, interpolation_transform = its +80). The fields both read - +216 flags,
// +228 activation state, +244 internal type - are copied into the collision
// object before the collision stage, and +228 (the only one the stage writes:
// updateSingleAabb's state 5) back after it.
inline void collision_object_sync_in( collision_object_t& o )
{
    const solver_rigid_t& b = *o.body.object;
    o.activation_state = b.activation_state;
    o.collision_flags = b.collision_flags;
    o.internal_type = b.internal_type;
}

// btRigidBody::setGravity @0x50DC40 (thiscall acceleration*; retn 4):
// `ucomiss invMass, 0; lahf; test ah, 44h; jnp` - invMass (+352) non-zero OR
// NaN -> +384 = acceleration * (1 / invMass), w 0; then +400 = the 16 bytes.
inline void rigid_set_gravity( solver_rigid_t& b, const float* acceleration )
{
    const float im = b.inverse_mass;
    if ( !( im == 0.0f ) )
    {
        const float r = 1.0f / im;
        b.gravity[ 0 ] = acceleration[ 0 ] * r;
        b.gravity[ 1 ] = acceleration[ 1 ] * r;
        b.gravity[ 2 ] = acceleration[ 2 ] * r;
        b.gravity[ 3 ] = 0.0f;
    }
    std::memcpy( b.gravity_acceleration, acceleration, 16 );
}

// btDiscreteDynamicsWorld::addRigidBody(body, group, mask) @0x6013E0 (slot 19;
// retn 0Ch) - the variant Vu's addRigidBody (VuDynamicsImpl slot 15 0x4F8720)
// calls, with the body's +714 / +716 shorts, after body+216 |= 8: setGravity
// (world +224) unless static / kinematic (+216 & 3) or +524 & 1; a body
// without a shape (+204 = 0) stops there; a non-static one is appended to
// the non-static list (+204 / +212), a static one goes to state 2 unless 4 /
// 5; then addCollisionObject (vt+0x1C) with group / mask. The world's object
// array (+8) is appended by addCollisionObject.
inline int world_add_rigid_body( world_t& w, collision_world_t& cw, collision_object_t& o, bool has_motion, short group, short mask )
{
    solver_rigid_t& b = *o.body.object;
    if ( !( b.collision_flags & 3 ) && !( b.rigidbody_flags & 1 ) )
        rigid_set_gravity( b, w.gravity );
    if ( !o.body.convex && !o.body.concave && !o.body.compound )
        return 1;
    if ( !( b.collision_flags & 1 ) )
    {
        w.bodies.push_back( &b );
        w.shape_types.push_back( body_shape_type( o.body ) );
    }
    else if ( b.activation_state != 4 && b.activation_state != 5 )
        b.activation_state = 2;
    o.body.transform = b.basis;
    o.interpolation_transform = b.interpolation_basis;
    collision_object_sync_in( o );
    w.objects.push_back( &b );
    w.has_motion.push_back( has_motion ? 1 : 0 );
    return collision_world_add_object( cw, o, group, mask );
}

// addCollisionObject (world vt+0x1C = 0x54C1D0) on an object that is no rigid body: the world's one object array, which ours
// mirrors in w.objects / has_motion, then the proxy. The ghost's collision object reads a solver_rigid_t image too (see
// collision_object_sync_in), so `o.body.object` must be set.
inline int world_add_collision_object( world_t& w, collision_world_t& cw, collision_object_t& o, short group, short mask )
{
    collision_object_sync_in( o );
    w.objects.push_back( o.body.object );
    w.has_motion.push_back( 0 );
    return collision_world_add_object( cw, o, group, mask );
}

// removeRigidBody @0x6011E0 (slot 21; retn 4): the body out of the non-static
// list (+200 / +212, remove 0x50F200), then removeCollisionObject inlined. The
// world's removeCollisionObject @0x601170 (slot 8) sends an object with
// internal type & 2 (+0xF4) here through vt+0x54, else straight to the
// inlined btCollisionWorld part.
inline int world_remove_rigid_body( world_t& w, collision_world_t& cw, collision_object_t& o )
{
    solver_rigid_t* b = o.body.object;
    const int bi = array_remove_index( w.bodies, b );
    array_remove_at( w.bodies, bi );
    array_remove_at( w.shape_types, bi );
    const int oi = array_remove_index( w.objects, b );
    if ( collision_world_remove_object( cw, o ) < 0 )
        return -1;
    array_remove_at( w.objects, oi );
    array_remove_at( w.has_motion, oi );
    return 1;
}

inline int world_remove_collision_object( world_t& w, collision_world_t& cw, collision_object_t& o )
{
    if ( o.internal_type & 2 )
        return world_remove_rigid_body( w, cw, o );
    const int oi = array_remove_index( w.objects, o.body.object );
    if ( collision_world_remove_object( cw, o ) < 0 )
        return -1;
    array_remove_at( w.objects, oi );
    array_remove_at( w.has_motion, oi );
    return 1;
}

// The world's collision stage (slot 9 -> 0x54C710) on the attached collision
// world: world +44 / +84 into updateSingleAabb / updateAabbs, then the
// dispatcher's manifold array and the broadphase's pair array (in cache
// order, as calculateSimulationIslands reads it) handed to the islands and
// the solver.
inline void world_stage_collide_attached( world_t& w )
{
    collision_world_t& cw = *w.collision;
    cw.aabb.use_continuous = w.use_continuous;
    cw.aabb.force_update_all = w.force_update_all_aabbs;
    for ( collision_object_t* o : cw.objects )
        collision_object_sync_in( *o );
    const int r = collision_world_perform_discrete( cw );
    for ( collision_object_t* o : cw.objects )
        o->body.object->activation_state = o->activation_state;
    w.manifolds = cw.dispatcher.manifolds;
    w.pairs.clear( );
    const pair_cache_t& c = cw.broadphase->cache;
    for ( int k = 0; k < c.pairs.size; ++k )
    {
        const broadphase_pair_t& p = c.pairs.data[ k ];
        w.pairs.push_back( island_pair_t{ static_cast< collision_object_t* >( p.proxy0->client )->body.object,
            static_cast< collision_object_t* >( p.proxy1->client )->body.object } );
    }
    if ( r != 1 )
        w.refused = true;
}

inline void world_attach_collision( world_t& w, collision_world_t& cw )
{
    w.collision = &cw;
    w.stages.collide = world_stage_collide_attached;
}
