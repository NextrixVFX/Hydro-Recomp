#pragma once

#include <memory>
#include <vector>

#include <workspace/core/collision/concave.hxx>
#include <workspace/core/collision/sphere.hxx>
#include <workspace/core/collision/boxbox.hxx>

// btCompoundShape, btCompoundCollisionAlgorithm and the dispatcher pieces
// they reach (Bullet 2.79), recreated from retail's disassembly
// (memory/audit.md "Batch 8g"). Every compound in the binary gets ONE child
// (the boat loader 0x412270, VuBoatDB 0x420A70, VuRigidBodyComponent
// 0x544100), and the ctor 0x5269C0 always builds the btDbvt, so the tree is
// one leaf and the Compound algorithm always takes the tree path; a second
// child (a btDbvt insert past the root) returns -1 here.

// gNumManifold 0x7590A4: getNewManifold ++, releaseManifold --.
inline int g_num_manifolds = 0;

// The btCollisionDispatcher pieces the collision algorithms reach: +4 the
// flags (ctor 0x56D1E8 = 2, a CTOR DEFAULT - measure live), the manifold
// array +12 / +16 / +20 (each manifold's +1196 is its slot), the config's
// simplex / pd solvers and the ConvexConvex CreateFunc's +16 / +20 (0 / 3).
// The pools (+0xD4 algorithms, +0xD8 manifolds) are allocation bookkeeping:
// manifolds are owned here.
struct collision_dispatcher_t
{
    int flags = 2;
    std::vector< solver_manifold_t* > manifolds;
    std::vector< std::unique_ptr< solver_manifold_t > > storage;
    simplex_solver_t* simplex = nullptr;
    bool pd_solver = true;
    int perturbation_iterations = 0;
    int minimum_points_threshold = 3;
    const contact_callbacks_t* callbacks = nullptr;
};

// getBoundingSphere @0x554270 (the shape's getAabb under the IDENTITY
// transform 0x413280: radius = sqrt((dy^2 + dx^2) + dz^2) * 0.5, centre =
// (min + max) * 0.5) -> getAngularMotionDisc @0x5543B0 = sqrt((cy^2 + cx^2)
// + cz^2) + radius -> getContactBreakingThreshold @0x554380 = disc * the
// global threshold (one float rounding of an exact double product). -1 when
// the shape's getAabb is not recreated.
inline void compound_get_aabb( const compound_shape_t& c, const float* t, float* aabb_min, float* aabb_max );

inline bool body_get_aabb( const concave_body_t& b, const float* t, float* aabb_min, float* aabb_max )
{
    if ( b.convex )
        return shape_get_aabb( *b.convex, t, aabb_min, aabb_max );
    if ( b.concave )
    {
        bvh_mesh_get_aabb( *b.concave, t, aabb_min, aabb_max );
        return true;
    }
    if ( b.compound )
    {
        compound_get_aabb( *b.compound, t, aabb_min, aabb_max );
        return true;
    }
    return false;
}

inline bool body_contact_breaking_threshold( const concave_body_t& b, float global, float& out )
{
    static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
    float mn[ 4 ], mx[ 4 ];
    if ( !body_get_aabb( b, identity, mn, mx ) )
        return false;
    const float dx = mx[ 0 ] - mn[ 0 ];
    const float dy = mx[ 1 ] - mn[ 1 ];
    const float dz = mx[ 2 ] - mn[ 2 ];
    const float radius = std::sqrt( ( dy * dy + dx * dx ) + dz * dz ) * 0.5f;
    const float cx = ( mn[ 0 ] + mx[ 0 ] ) * 0.5f;
    const float cy = ( mn[ 1 ] + mx[ 1 ] ) * 0.5f;
    const float cz = ( mn[ 2 ] + mx[ 2 ] ) * 0.5f;
    const float disc = std::sqrt( ( cy * cy + cx * cx ) + cz * cz ) + radius;
    out = disc * global;
    return true;
}

// gContactBreakingThreshold 0x735C8C.
inline float contact_breaking_global( )
{
    return support_bits( 0x3CA3D70Au );
}

// getNewManifold @0x56D380 (dispatcher vt+8; body0, body1): ++gNumManifold;
// breaking = flags & 2 ? the two shapes' thresholds, body1's FIRST, picked by
// `comiss t1, t0; cmovbe` -> (t1 > t0) ? t0 : t1 : the global; processing the
// same pick on the bodies' +196 (body1 vs body0); the manifold ctor 0x56DA40
// (bodies, count 0, both thresholds; the per-point fields it zeroes are zero
// in a fresh manifold here); +1196 = the array count; pushed.
inline solver_manifold_t* dispatcher_get_new_manifold( collision_dispatcher_t& d, const concave_body_t& body0, const concave_body_t& body1 )
{
    ++g_num_manifolds;
    float breaking = contact_breaking_global( );
    if ( d.flags & 2 )
    {
        float t1, t0;
        if ( !body_contact_breaking_threshold( body1, contact_breaking_global( ), t1 ) ||
             !body_contact_breaking_threshold( body0, contact_breaking_global( ), t0 ) )
            return nullptr;
        breaking = ( t1 > t0 ) ? t0 : t1;
    }
    const float p0 = body0.processing_threshold;
    const float p1 = body1.processing_threshold;
    const float processing = ( p1 > p0 ) ? p0 : p1;
    d.storage.push_back( std::make_unique< solver_manifold_t >( ) );
    solver_manifold_t* m = d.storage.back( ).get( );
    m->body0 = body0.object;
    m->body1 = body1.object;
    m->num_contacts = 0;
    m->contact_breaking_threshold = breaking;
    m->contact_processing_threshold = processing;
    m->dispatcher_index = static_cast< int >( d.manifolds.size( ) );
    d.manifolds.push_back( m );
    return m;
}

// clearManifold @0x56D560 (vt+16, stdcall): every point's user data (+108)
// goes to gContactDestroyedCallback and is zeroed - ONLY when the callback is
// set; count = 0.
inline void dispatcher_clear_manifold( const collision_dispatcher_t& d, solver_manifold_t& m )
{
    if ( m.num_contacts > 0 )
    {
        for ( int i = 0; i < m.num_contacts; ++i )
        {
            unsigned& user = m.points[ i ].user_persistent_data;
            if ( user && d.callbacks && d.callbacks->destroyed )
            {
                d.callbacks->destroyed( d.callbacks->user, user );
                user = 0;
            }
        }
    }
    m.num_contacts = 0;
}

// releaseManifold @0x56D5D0 (vt+12): --gNumManifold, clearManifold, the
// manifold swapped with the last (the moved one's +1196 = the slot), count--,
// freed.
inline void dispatcher_release_manifold( collision_dispatcher_t& d, solver_manifold_t* m )
{
    --g_num_manifolds;
    dispatcher_clear_manifold( d, *m );
    const int slot = m->dispatcher_index;
    const int last = static_cast< int >( d.manifolds.size( ) ) - 1;
    solver_manifold_t* moved = d.manifolds[ last ];
    d.manifolds[ slot ] = moved;
    d.manifolds[ last ] = m;
    d.manifolds[ slot ]->dispatcher_index = slot;
    d.manifolds.pop_back( );
    for ( auto it = d.storage.begin( ); it != d.storage.end( ); ++it )
        if ( it->get( ) == m )
        {
            d.storage.erase( it );
            break;
        }
}

// A child collision algorithm as findAlgorithm 0x56D660 makes it through the
// matrix for the pairs the compound reaches: convex vs the BVH mesh ->
// ConvexConcave (CreateFunc 0x56C2B0: the manifold from getNewManifold, then
// clearManifold); convex vs convex -> ConvexConvex (CreateFunc 0x5FBCD0: the
// shared manifold, ownManifold 0 - NULL from nearCallback, so path 1 makes it
// on the first processCollision); sphere vs sphere (8, 8) -> SphereSphere
// (sphere.hxx: getNewManifold(body0, body1), ownManifold 1). The matrix's
// sphere-triangle (8, 1) and box-box (0, 0) are not recreated: -1.
// A convex child against another COMPOUND (boat vs boat: both bodies are
// compounds) -> the matrix's (convex, 31) SwappedCreateFunc 0x56C4F0 (vt
// 0x74F610): a nested btCompoundCollisionAlgorithm (vt 0x757578), +0x1C
// isSwapped 1, +0x20 the shared manifold = ci.m_manifold (the outer's, NULL
// from the nearCallback), +0x24 ownsManifold 0, +0x28 body1's shape (+0xCC)
// revision (+0x44), then preallocateChildAlgorithms 0x5F7A60 (all NULL with
// the tree).
struct compound_algorithm_t;
struct child_algorithm_t
{
    int kind = 0;   // 1 ConvexConvex, 2 ConvexConcave, 3 Compound (swapped), 4 SphereSphere, 5 BoxBox
    convex_convex_t convex{};
    convex_concave_t concave{};
    sphere_sphere_t sphere{};
    box_box_t box{};
    std::unique_ptr< compound_algorithm_t > compound;
    child_algorithm_t( );
    ~child_algorithm_t( );
};

struct compound_algorithm_t
{
    bool swapped = false;
    solver_manifold_t* shared_manifold = nullptr;
    int revision = 0;
    std::vector< std::unique_ptr< child_algorithm_t > > children;
};

inline void compound_create( compound_algorithm_t& alg, const compound_shape_t& shape, bool swapped, solver_manifold_t* shared );
inline void compound_destroy_children( collision_dispatcher_t& d, compound_algorithm_t& alg );
inline int compound_process_collision( collision_dispatcher_t& d, compound_algorithm_t& alg, const concave_body_t& body0,
    const concave_body_t& body1, manifold_result_t& result, void* debug_draw, void* stack_alloc );
inline void compound_contact_manifolds( const compound_algorithm_t& alg, std::vector< solver_manifold_t* >& out );

// The BoxBox CreateFunc (vt 0x74F5E0, slot 1 0x56C700) and ctor 0x5FB6F0: 16
// bytes; manifold 0 / own 0, then IF the dispatcher's needsCollision(b0, b1)
// (vt+0x14) getNewManifold(b0, b1) and own 1. Every path that reaches it
// (the nearCallback, the compound's children) has just passed needsCollision
// for the same two objects, so the manifold is always made here.
inline int dispatcher_make_box_box( collision_dispatcher_t& d, const concave_body_t& body0, const concave_body_t& body1, child_algorithm_t& a )
{
    a.kind = 5;
    a.box.manifold = dispatcher_get_new_manifold( d, body0, body1 );
    if ( !a.box.manifold )
        return -1;
    a.box.own_manifold = true;
    return 1;
}

inline int dispatcher_make_sphere_sphere( collision_dispatcher_t& d, const concave_body_t& body0, const concave_body_t& body1,
    child_algorithm_t& a )
{
    a.kind = 4;
    a.sphere.own_manifold = false;
    a.sphere.manifold = dispatcher_get_new_manifold( d, body0, body1 );
    if ( !a.sphere.manifold )
        return -1;
    a.sphere.own_manifold = true;
    return 1;
}

inline int dispatcher_find_algorithm( collision_dispatcher_t& d, const concave_body_t& body0, const concave_body_t& body1,
    solver_manifold_t* shared, std::unique_ptr< child_algorithm_t >& out )
{
    const int t0 = body_shape_type( body0 );
    const int t1 = body_shape_type( body1 );
    auto a = std::make_unique< child_algorithm_t >( );
    if ( t0 < 20 && t1 == 21 )
    {
        a->kind = 2;
        a->concave.swapped = false;
        a->concave.simplex = d.simplex;
        a->concave.pd_solver = d.pd_solver;
        a->concave.perturbation_iterations = d.perturbation_iterations;
        a->concave.minimum_points_threshold = d.minimum_points_threshold;
        a->concave.manifold = dispatcher_get_new_manifold( d, body0, body1 );
        if ( !a->concave.manifold )
            return -1;
        dispatcher_clear_manifold( d, *a->concave.manifold );
    }
    else if ( t0 == 8 && t1 == 8 )
    {
        if ( dispatcher_make_sphere_sphere( d, body0, body1, *a ) < 0 )
            return -1;
    }
    else if ( t0 == 0 && t1 == 0 )
    {
        if ( dispatcher_make_box_box( d, body0, body1, *a ) < 0 )
            return -1;
    }
    else if ( t0 < 20 && t1 < 20 )
    {
        if ( ( t0 == 8 && t1 == 1 ) || ( t0 == 1 && t1 == 8 ) )
            return -1;   // SphereTriangle at the top of a compound child: not reached
        a->kind = 1;
        a->convex.simplex = d.simplex;
        a->convex.pd_solver = d.pd_solver;
        a->convex.own_manifold = false;
        a->convex.manifold = shared;
        a->convex.perturbation_iterations = d.perturbation_iterations;
        a->convex.minimum_points_threshold = d.minimum_points_threshold;
    }
    else if ( t0 < 20 && t1 == 31 && body1.compound )
    {
        a->kind = 3;
        a->compound = std::make_unique< compound_algorithm_t >( );
        compound_create( *a->compound, *body1.compound, true, shared );
    }
    else
        return -1;   // another pair of the matrix
    out = std::move( a );
    return 1;
}

// The algorithm's dtor (vt+0) + freeCollisionAlgorithm: ConvexConvex 0x5FBD50
// releases its manifold only when owned; ConvexConcave 0x5F65A0 clears then
// releases the callback's.
inline void dispatcher_destroy_algorithm( collision_dispatcher_t& d, std::unique_ptr< child_algorithm_t >& a )
{
    if ( !a )
        return;
    if ( a->kind == 1 && a->convex.own_manifold && a->convex.manifold )
        dispatcher_release_manifold( d, a->convex.manifold );
    if ( a->kind == 4 && a->sphere.own_manifold && a->sphere.manifold )   // 0x5FEC00
        dispatcher_release_manifold( d, a->sphere.manifold );
    if ( a->kind == 5 && a->box.own_manifold && a->box.manifold )   // 0x5FB670
        dispatcher_release_manifold( d, a->box.manifold );
    if ( a->kind == 2 && a->concave.manifold )
    {
        dispatcher_clear_manifold( d, *a->concave.manifold );
        dispatcher_release_manifold( d, a->concave.manifold );
    }
    if ( a->kind == 3 && a->compound )   // the Compound dtor 0x5F7A30 -> removeChildAlgorithms 0x5F7BD0 (ownsManifold 0)
        compound_destroy_children( d, *a->compound );
    a.reset( );
}

// processCollision through vt+4. ConvexConvex path 1 (0x5FC96C): no manifold
// -> getNewManifold(body0, body1), ownManifold 1, before anything else.
inline int child_process_collision( collision_dispatcher_t& d, child_algorithm_t& a, const concave_body_t& body0,
    const concave_body_t& body1, manifold_result_t& result, void* debug_draw, void* stack_alloc )
{
    const contact_callbacks_t empty{};
    const contact_callbacks_t& cb = d.callbacks ? *d.callbacks : empty;
    if ( a.kind == 1 )
    {
        if ( !a.convex.manifold )
        {
            a.convex.manifold = dispatcher_get_new_manifold( d, body0, body1 );
            if ( !a.convex.manifold )
                return -1;
            a.convex.own_manifold = true;
        }
        if ( !body0.convex || !body1.convex )
            return -1;
        return convex_convex_process( a.convex, *body0.convex, body0.transform, *body1.convex, body1.transform, result, cb, debug_draw,
            stack_alloc );
    }
    if ( a.kind == 4 )
        return sphere_sphere_process( a.sphere, body0, body1, result, cb );
    if ( a.kind == 5 )
    {
        if ( !body0.convex || !body1.convex )
            return -1;
        return box_box_process( a.box, *body0.convex, body0.transform, *body1.convex, body1.transform, result, cb );
    }
    if ( a.kind == 3 )
        return compound_process_collision( d, *a.compound, body0, body1, result, debug_draw, stack_alloc );
    return convex_concave_process( a.concave, body0, body1, result, cb, debug_draw, stack_alloc );
}

// getAllContactManifolds (vt+12): ConvexConvex 0x5FBBF0 and SphereSphere
// 0x5F9BB0 only when they own a manifold; ConvexConcave 0x5F6630 its
// callback's when non-null; a nested Compound 0x5F7A00 each non-null child's
// in index order.
inline solver_manifold_t* child_contact_manifold( const child_algorithm_t& a )
{
    if ( a.kind == 4 )
        return ( a.sphere.manifold && a.sphere.own_manifold ) ? a.sphere.manifold : nullptr;
    if ( a.kind == 5 )
        return ( a.box.manifold && a.box.own_manifold ) ? a.box.manifold : nullptr;
    if ( a.kind == 1 )
        return ( a.convex.manifold && a.convex.own_manifold ) ? a.convex.manifold : nullptr;
    if ( a.kind == 3 )
        return nullptr;   // several: child_contact_manifolds
    return a.concave.manifold;
}

// The manifold array getAllContactManifolds appends to.
inline void child_contact_manifolds( const child_algorithm_t& a, std::vector< solver_manifold_t* >& out )
{
    if ( a.kind == 3 )
    {
        compound_contact_manifolds( *a.compound, out );
        return;
    }
    if ( solver_manifold_t* m = child_contact_manifold( a ) )
        out.push_back( m );
}

// btCompoundShape: the ctor 0x5269C0 (type 31, local aabb 1e18 / -1e18,
// revision +68 = 1, the btDbvt) and addChildShape 0x526BF0: ++revision; the
// 80-byte record (transform, +64 shape, +68 type, +72 the child's getMargin,
// +76 the leaf); the child's getAabb under the child transform widens the
// local aabb (min `cur > new`, max `new > cur`, x x y y z z); the FIRST
// btDbvt insert 0x5948D0 makes the leaf the root (volume = that aabb, data =
// the child index).
struct compound_child_t
{
    float transform[ 16 ];
    const convex_shape_t* shape;
    int type;
    float margin;
};

struct compound_shape_t
{
    int revision = 1;
    std::vector< compound_child_t > children;
    float local_aabb_min[ 4 ] = { support_bits( 0x5D5E0B6Bu ), support_bits( 0x5D5E0B6Bu ), support_bits( 0x5D5E0B6Bu ), 0.0f };
    float local_aabb_max[ 4 ] = { support_bits( 0xDD5E0B6Bu ), support_bits( 0xDD5E0B6Bu ), support_bits( 0xDD5E0B6Bu ), 0.0f };
    bool has_root = false;
    float root_min[ 4 ] = {};
    float root_max[ 4 ] = {};
    int root_data = 0;
    float margin = 0.0f;   // +0x48 (ctor 0; getMargin 0x527B40 `fld [ecx+48h]`)
    float local_scaling[ 4 ] = { 1.0f, 1.0f, 1.0f, 0.0f };   // +0x50
};

// btCompoundShape::getAabb @0x527280 (slot 1; thiscall transform, min, max;
// retn 0Ch): half = (hi - lo) * 0.5 and centre = (lo + hi) * 0.5 from the
// local aabb (+0x20 / +0x30) - both ZERO when the child count (+0x10) is 0 -
// half += getMargin (vt+0x28, three calls); then the centre through the
// transform and the half through |basis|, min = c - e, max = e + c, lane 3
// 0. Every sum and product is aabb_transform_local's up to commutation.
inline void compound_get_aabb( const compound_shape_t& c, const float* t, float* aabb_min, float* aabb_max )
{
    static const float zero[ 4 ] = {};
    if ( c.children.empty( ) )
        aabb_transform_local( zero, zero, c.margin, t, aabb_min, aabb_max );
    else
        aabb_transform_local( c.local_aabb_min, c.local_aabb_max, c.margin, t, aabb_min, aabb_max );
}

inline int compound_add_child( compound_shape_t& c, const float* transform, const convex_shape_t& child )
{
    ++c.revision;
    compound_child_t r{};
    std::memcpy( r.transform, transform, 64 );
    r.shape = &child;
    r.type = child.type;
    if ( !shape_get_margin( child, r.margin ) )
        return -1;
    float mn[ 4 ], mx[ 4 ];
    if ( !shape_get_aabb( child, transform, mn, mx ) )
        return -1;
    for ( int k = 0; k < 3; ++k )
    {
        if ( c.local_aabb_min[ k ] > mn[ k ] )
            c.local_aabb_min[ k ] = mn[ k ];
        if ( mx[ k ] > c.local_aabb_max[ k ] )
            c.local_aabb_max[ k ] = mx[ k ];
    }
    if ( c.has_root )
        return -1;   // insertleaf 0x5942E0 past the root: not recreated
    c.has_root = true;
    std::memcpy( c.root_min, mn, 16 );
    std::memcpy( c.root_max, mx, 16 );
    c.root_data = static_cast< int >( c.children.size( ) );
    c.children.push_back( r );
    return 1;
}

// removeChildShapeByIndex @0x527030 (thiscall; index; retn 4 - 2.79: NO
// local-aabb recalculation): ++revision (+0x44); with the btDbvt (+0x40) the
// child's leaf (+0x4C) removed (removeleaf 0x594480) and freed (deletenode: the
// old m_free released, the leaf kept as m_free, --m_leaves); the LAST child
// copied over this one (0x527A80, through a stack copy) and, with the tree, its
// leaf's data (+0x24) = index; --count. Recreated for the one-leaf model only:
// removing the only child (the root leaf empties the tree; the copy is a
// self-copy). Any other case moves another child's leaf - not recreated (-1).
inline int compound_remove_child( compound_shape_t& c, int index )
{
    if ( index != 0 || c.children.size( ) != 1 || !c.has_root )
        return -1;
    ++c.revision;
    c.has_root = false;
    c.children.pop_back( );
    return 1;
}

// btCompoundShape::recalculateLocalAabb 0x527160 (slot 15) on the typed
// model - arena.hxx bt_compound_recalculate_local_aabb is the same function on
// retail memory: the local aabb reset to (1e18 x3, 0) / (-1e18 x3, 0)
// (0x6F7B10 / 0x6F7C90), then per child its getAabb (vt+4) under the child
// transform, per lane x / y / z min = the child's when min > it, max = the
// child's when it > max (`comiss`, ordered). False when a child's getAabb is
// not recreated.
inline bool compound_recalculate_local_aabb( compound_shape_t& c )
{
    for ( int k = 0; k < 3; ++k )
    {
        c.local_aabb_min[ k ] = support_bits( 0x5D5E0B6Bu );
        c.local_aabb_max[ k ] = support_bits( 0xDD5E0B6Bu );
    }
    c.local_aabb_min[ 3 ] = 0.0f;
    c.local_aabb_max[ 3 ] = 0.0f;
    for ( const compound_child_t& r : c.children )
    {
        float mn[ 4 ], mx[ 4 ];
        if ( !shape_get_aabb( *r.shape, r.transform, mn, mx ) )
            return false;
        for ( int k = 0; k < 3; ++k )
        {
            if ( c.local_aabb_min[ k ] > mn[ k ] )
                c.local_aabb_min[ k ] = mn[ k ];
            if ( mx[ k ] > c.local_aabb_max[ k ] )
                c.local_aabb_max[ k ] = mx[ k ];
        }
    }
    return true;
}

// btCompoundShape::setLocalScaling @0x5276D0 (slot 5) on the typed model - arena.hxx bt_compound_set_local_scaling is the
// same function on retail memory: per child the child scale ((s.x * c.x) / ls.x, ..., 0) goes to the child's own
// setLocalScaling (`child_scaling` gets its current scale and the new one: the shape is the caller's), the origin
// (s.x * o.x, o.y * s.y, o.z * s.z, 0) into updateChildTransform 0x526F40 (with the tree, the child's aabb under it into
// btDbvt::update 0x594960: the one-leaf model's root). Then +0x50 and recalculateLocalAabb.
template < typename child_scaling_fn >
inline bool compound_set_local_scaling( compound_shape_t& c, const float* s, child_scaling_fn child_scaling )
{
    for ( compound_child_t& r : c.children )
    {
        const float* now = child_scaling( r, nullptr );
        const float v[ 4 ] = { ( s[ 0 ] * now[ 0 ] ) / c.local_scaling[ 0 ], ( s[ 1 ] * now[ 1 ] ) / c.local_scaling[ 1 ],
                               ( s[ 2 ] * now[ 2 ] ) / c.local_scaling[ 2 ], 0.0f };
        child_scaling( r, v );
        const float ox = s[ 0 ] * r.transform[ 12 ], oy = r.transform[ 13 ] * s[ 1 ], oz = r.transform[ 14 ] * s[ 2 ];
        r.transform[ 12 ] = ox;
        r.transform[ 13 ] = oy;
        r.transform[ 14 ] = oz;
        r.transform[ 15 ] = 0.0f;
        if ( c.has_root )
        {
            if ( c.children.size( ) != 1 )
                return false;   // btDbvt::update past a one-leaf tree: not recreated
            if ( !shape_get_aabb( *r.shape, r.transform, c.root_min, c.root_max ) )
                return false;
        }
    }
    std::memcpy( c.local_scaling, s, 16 );
    return compound_recalculate_local_aabb( c );
}

// btCompoundShape::removeChildShape 0x527110 (slot 14; thiscall shape; retn
// 4): ++revision (+0x44); from the last child (count - 1, read once) down to 0
// every child whose shape (+0x40 of the 0x50-byte record, the data re-read)
// is `shape` -> removeChildShapeByIndex 0x527030 (compound_remove_child: the
// one-leaf model, else -1); then recalculateLocalAabb (vt+0x3C).
inline int compound_remove_child_shape( compound_shape_t& c, const convex_shape_t* shape )
{
    ++c.revision;
    for ( int i = static_cast< int >( c.children.size( ) ) - 1; i >= 0; --i )
        if ( c.children[ static_cast< std::size_t >( i ) ].shape == shape && compound_remove_child( c, i ) < 0 )
            return -1;
    return compound_recalculate_local_aabb( c ) ? 1 : -1;
}

inline void compound_create( compound_algorithm_t& alg, const compound_shape_t& shape, bool swapped, solver_manifold_t* shared )
{
    alg.swapped = swapped;
    alg.shared_manifold = shared;
    alg.revision = shape.revision;
    alg.children.clear( );
    alg.children.resize( shape.children.size( ) );
}

inline child_algorithm_t::child_algorithm_t( ) = default;
inline child_algorithm_t::~child_algorithm_t( ) = default;

// removeChildAlgorithms 0x5F7BD0: each child in index order (its dtor +
// freeCollisionAlgorithm).
inline void compound_destroy_children( collision_dispatcher_t& d, compound_algorithm_t& alg )
{
    for ( auto& a : alg.children )
        dispatcher_destroy_algorithm( d, a );
}

// getAllContactManifolds 0x5F7A00 (slot 3): each non-null child's, in index
// order.
inline void compound_contact_manifolds( const compound_algorithm_t& alg, std::vector< solver_manifold_t* >& out )
{
    for ( const auto& a : alg.children )
        if ( a )
            child_contact_manifolds( *a, out );
}

// The two-box test both ProcessChildShape 0x5F7CB0 and the removal loop make:
// x then z then y, each `comiss a, b; ja` (NaN = overlap).
inline bool compound_aabb_overlap( const float* min1, const float* max1, const float* min2, const float* max2 )
{
    bool ok = true;
    if ( min1[ 0 ] > max2[ 0 ] || min2[ 0 ] > max1[ 0 ] )
        ok = false;
    if ( min1[ 2 ] > max2[ 2 ] || min2[ 2 ] > max1[ 2 ] )
        ok = false;
    if ( min1[ 1 ] > max2[ 1 ] || min2[ 1 ] > max1[ 1 ] )
        return false;
    return ok;
}

// orgTrans * childTrans as ProcessChildShape and the removal loop compose it:
// M[r][c] = (O[4r] C[c] + O[4r+1] C[4+c]) + O[4r+2] C[8+c], origin[r] =
// ((C12 O[4r] + C13 O[4r+1]) + C14 O[4r+2]) + O[12+r], w 0.
inline void compound_child_world( const float* o, const float* c, float* out )
{
    for ( int r = 0; r < 3; ++r )
    {
        for ( int k = 0; k < 3; ++k )
            out[ 4 * r + k ] = ( o[ 4 * r ] * c[ k ] + o[ 4 * r + 1 ] * c[ 4 + k ] ) + o[ 4 * r + 2 ] * c[ 8 + k ];
        out[ 4 * r + 3 ] = 0.0f;
        out[ 12 + r ] = ( ( c[ 12 ] * o[ 4 * r ] + c[ 13 ] * o[ 4 * r + 1 ] ) + c[ 14 ] * o[ 4 * r + 2 ] ) + o[ 12 + r ];
    }
    out[ 15 ] = 0.0f;
}

// inverse(a) * b - the same expression, in the same order, as
// setTimeStepAndCounters (concave.hxx) and processCollision's tree path.
inline void transform_inverse_times( const float* t, const float* c, float* x )
{
    const float n0 = support_negate( t[ 12 ] );
    const float n1 = support_negate( t[ 13 ] );
    const float n2 = support_negate( t[ 14 ] );
    float i[ 3 ];
    for ( int k = 0; k < 3; ++k )
        i[ k ] = ( t[ 4 + k ] * n1 + t[ k ] * n0 ) + t[ 8 + k ] * n2;
    for ( int k = 0; k < 3; ++k )
        x[ 12 + k ] = ( ( c[ 12 ] * t[ k ] + c[ 13 ] * t[ 4 + k ] ) + c[ 14 ] * t[ 8 + k ] ) + i[ k ];
    x[ 15 ] = 0.0f;
    for ( int r = 0; r < 3; ++r )
    {
        x[ 4 * r + 0 ] = ( t[ r ] * c[ 0 ] + t[ 4 + r ] * c[ 4 ] ) + t[ 8 + r ] * c[ 8 ];
        x[ 4 * r + 1 ] = ( t[ r ] * c[ 1 ] + t[ 4 + r ] * c[ 5 ] ) + t[ 8 + r ] * c[ 9 ];
        x[ 4 * r + 2 ] = ( t[ r ] * c[ 2 ] + c[ 6 ] * t[ 4 + r ] ) + t[ 8 + r ] * c[ 10 ];
        x[ 4 * r + 3 ] = 0.0f;
    }
}

// ProcessChildShape @0x5F7CB0 (transliterated from the disassembly): the
// child's world transform, the child's and the other's aabbs, the test; on
// overlap the compound body carries newChildWorld (+16 and, through 0x5A8520,
// +80) and the child shape (+204) for the call - here a child body - the
// child algorithm is made on first use (findAlgorithm with the shared
// manifold), the result's ids are A(-1, index) when its object0 is the
// compound object, else B(-1, index), and the child's processCollision runs.
// The debug drawer's two drawAabb (dispatchInfo+20, mode & 2) are a hook not
// called here.
inline int compound_process_child( collision_dispatcher_t& d, compound_algorithm_t& alg, const concave_body_t& compound,
    const concave_body_t& other, int index, manifold_result_t& result, void* debug_draw, void* stack_alloc )
{
    const compound_child_t& c = compound.compound->children[ index ];
    float world[ 16 ];
    compound_child_world( compound.transform, c.transform, world );
    float min1[ 4 ], max1[ 4 ], min2[ 4 ], max2[ 4 ];
    if ( !shape_get_aabb( *c.shape, world, min1, max1 ) || !body_get_aabb( other, other.transform, min2, max2 ) )
        return -1;
    if ( !compound_aabb_overlap( min1, max1, min2, max2 ) )
        return 1;
    concave_body_t child{ world, c.shape, nullptr, compound.object, nullptr, compound.processing_threshold };
    if ( !alg.children[ index ] )
        if ( dispatcher_find_algorithm( d, child, other, alg.shared_manifold, alg.children[ index ] ) < 0 )
            return -1;
    if ( result.object0 == compound.object )
    {
        result.part_id0 = -1;
        result.index0 = index;
    }
    else
    {
        result.part_id1 = -1;
        result.index1 = index;
    }
    return child_process_collision( d, *alg.children[ index ], child, other, result, debug_draw, stack_alloc );
}

// processCollision @0x5F8750 (thiscall alg; body0, body1, dispatchInfo,
// resultOut; retn 10h): compound / other by isSwapped; a revision mismatch
// removes (dtor + free each) and re-preallocates (all NULL with the tree);
// every child algorithm's manifolds with points are refreshed (result+4 set,
// refresh 0x5B6C10 with (result+16, +80) if the manifold's body0 is the
// result's object0 else swapped, result+4 = 0); the tree path: the other's
// aabb under inverse(compound) * other, the btDbvt volume, collideTV 0x5F9910
// from the root (one leaf: cmpltps lanes 0..2, NaN = overlap) -> the leaf
// Process 0x5F8360 -> ProcessChildShape; then every non-null child algorithm
// whose child aabb (compound world * child) no longer overlaps the other's is
// destroyed and freed. Returns 1, or -1 where code not recreated is reached.
inline int compound_process_collision( collision_dispatcher_t& d, compound_algorithm_t& alg, const concave_body_t& body0,
    const concave_body_t& body1, manifold_result_t& result, void* debug_draw, void* stack_alloc )
{
    const concave_body_t& compound = alg.swapped ? body1 : body0;
    const concave_body_t& other = alg.swapped ? body0 : body1;
    if ( !compound.compound )
        return -1;
    const compound_shape_t& shape = *compound.compound;
    const contact_callbacks_t empty{};
    const contact_callbacks_t& cb = d.callbacks ? *d.callbacks : empty;
    if ( shape.revision != alg.revision )
    {
        for ( auto& a : alg.children )
            dispatcher_destroy_algorithm( d, a );
        alg.children.clear( );
        alg.children.resize( shape.children.size( ) );
    }
    std::vector< solver_manifold_t* > array;
    for ( auto& a : alg.children )
    {
        if ( !a )
            continue;
        child_contact_manifolds( *a, array );
        for ( solver_manifold_t* m : array )
        {
            if ( m->num_contacts == 0 )
                continue;
            result.manifold = m;
            if ( m->body0 == result.object0 )
                manifold_refresh( *m, result.basis0, result.origin0, result.basis1, result.origin1, cb );
            else
                manifold_refresh( *m, result.basis1, result.origin1, result.basis0, result.origin0, cb );
            result.manifold = nullptr;
        }
        array.clear( );
    }
    if ( !shape.has_root )
        return -1;   // no tree root: retail skips collideTV; with no child - not reached by content
    float local[ 16 ];
    transform_inverse_times( compound.transform, other.transform, local );
    float qmin[ 4 ], qmax[ 4 ];
    if ( !body_get_aabb( other, local, qmin, qmax ) )
        return -1;
    bool hit = true;
    for ( int k = 0; k < 3; ++k )
        if ( qmax[ k ] < shape.root_min[ k ] || shape.root_max[ k ] < qmin[ k ] )
            hit = false;
    if ( hit && compound_process_child( d, alg, compound, other, shape.root_data, result, debug_draw, stack_alloc ) < 0 )
        return -1;
    for ( std::size_t i = 0; i < alg.children.size( ); ++i )
    {
        if ( !alg.children[ i ] )
            continue;
        const compound_child_t& c = shape.children[ i ];
        float world[ 16 ];
        compound_child_world( compound.transform, c.transform, world );
        float min1[ 4 ], max1[ 4 ], min2[ 4 ], max2[ 4 ];
        if ( !shape_get_aabb( *c.shape, world, min1, max1 ) || !body_get_aabb( other, other.transform, min2, max2 ) )
            return -1;
        if ( !compound_aabb_overlap( min1, max1, min2, max2 ) )
            dispatcher_destroy_algorithm( d, alg.children[ i ] );
    }
    return 1;
}

// btCompoundShape::calculateLocalInertia @0x5275F0 (slot 7; thiscall mass,
// inertia&; retn 8): the identity 0x413280 (origin 0), the compound's own
// getAabb (slot 1, compound_get_aabb) through it, then the box formula on
// the aabb: l = ((hi - lo) x 0.5) x 2 per lane, k = mass x 1/12 (0x6F760C),
// I = ((y^2 + z^2) k, (x^2 + z^2) k, (x^2 + y^2) k) - the w lane is NOT
// written. setMargin @0x527B00 (slot 9) is `margin` (+0x48) = the argument.
inline void compound_local_inertia( const compound_shape_t& c, float mass, float* inertia )
{
    float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
    float lo[ 4 ], hi[ 4 ];
    compound_get_aabb( c, identity, lo, hi );
    const float half = 0.5f, two = 2.0f;
    std::uint32_t twelfth_bits = 0x3DAAAAABu;
    float twelfth;
    std::memcpy( &twelfth, &twelfth_bits, 4 );
    const float k = mass * twelfth;
    float ly = ( ( hi[ 1 ] - lo[ 1 ] ) * half ) * two;
    float lz = ( ( hi[ 2 ] - lo[ 2 ] ) * half ) * two;
    float lx = ( ( hi[ 0 ] - lo[ 0 ] ) * half ) * two;
    ly = ly * ly;
    lz = lz * lz;
    lx = lx * lx;
    inertia[ 0 ] = ( ly + lz ) * k;
    inertia[ 1 ] = ( lx + lz ) * k;
    inertia[ 2 ] = ( lx + ly ) * k;
}