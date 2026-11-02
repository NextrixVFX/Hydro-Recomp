#pragma once

#include <workspace/core/boat/meshes.hxx>
#include <workspace/core/boat/parts.hxx>
#include <workspace/core/collision/box.hxx>
#include <workspace/core/collision/compound.hxx>
#include <workspace/core/collision/concave.hxx>
#include <workspace/core/collision/cylinder.hxx>
#include <workspace/core/collision/rebuild.hxx>
#include <workspace/core/collision/sphere.hxx>
#include <workspace/core/collision/support.hxx>
#include <workspace/core/dynamics/body.hxx>
#include <workspace/core/collision/world.hxx>
#include <workspace/core/entity/rigid.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <xmmintrin.h>

// The level's static collision bodies (world batch 16): VuPropEntity's
// gameInitialize (vt slot 6 0x546910) and VuRigidBodyComponent's
// createRigidBody 0x544100, recreated from the disassembly and checked
// against retail on every VuPropEntity of the Hydroelectric level (oracle
// case prop_rigid_bodies). Shape paths: 6 convex hull (btConvexTriangleMeshShape
// 0x526380) and 7 triangle mesh (btBvhTriangleMeshShape 0x5AEB60 + Vu's
// setOptimizedBvh 0x5AF400 on the asset's VuOptimizedBvh); world batch 23 adds
// 3 - 5 (the cylinders, ctor 0x5AE160 - the dynamic game props' buoys), the
// centre-of-mass compound (0x5269C0 / 0x526BF0) and a mass's inertia through the
// shape's vt+0x1C (compound 0x5275F0, cylinder 0x5AE2D0, convex hull 0x54B4C0),
// checked by the case dynamic_prop_bodies. 1 / 2 (sphere 0x43D700, box
// 0x547D80), a non-unit scale on a compound (setLocalScaling vt+0x14, the
// compound's) and the triangle mesh's inertia are NOT recreated: such a body is
// UNKNOWN (refused, never stood in for). A BVH mesh's non-unit scale runs 0x5AF300
// with its rebuild (boat batch 23's collision/rebuild.hxx), one btOptimizedBvh per body.

// One built body: the component's shape (+0xF8), the VuRigidBody (+0x100), the
// VuStridingMesh (+0xA0) its shape reads, and the body's collision object in
// the race's world.
struct level_body_t
{
    std::string name;
    std::int32_t type = 0;
    const collision_mesh_asset_t* asset = nullptr;   // +0xC0
    striding_mesh_t mesh{ };                         // +0xA0 (scaling +0xB0 = (1, 1, 1, 0) from the ctor)
    quantized_bvh_t bvh{ };                          // the asset's +0xA8, as the walk reads it
    bvh_mesh_shape_t concave{ };
    optimized_bvh_t rebuilt{ };                      // the btOptimizedBvh setLocalScaling 0x5AF300 builds (concave.bvh then points at it)
    convex_shape_t convex{ };
    convex_shape_t cylinder{ };                      // types 3 - 5 (0x5AE160)
    compound_shape_t compound{ };                    // +0xF8 when the offset differs from the centre (+0xFC the child)
    bool has_compound = false;
    vu_rigid_body_t body{ };
    collision_object_t collision{ };
    std::uint32_t flags_d8 = 0;                      // component +0xD8 after the build
    bool in_world = false;                           // component +0x104
    const void* owner = nullptr;                     // the owning entity (the session's: its Show / Hide plugs)
    bool built = false;                              // a body exists (component +0x100 non-null)
    level_body_t( ) = default;
    level_body_t( const level_body_t& ) = delete;    // the compound holds a pointer to its own child
    level_body_t& operator=( const level_body_t& ) = delete;
};

// The motion state createRigidBody hands 0x4131E0 (component +0xF0): null for a
// VuPropEntity; VuDynamicPropEntity's 0x545A80 sets it to the entity's +0x5C,
// whose getWorldTransform (vt+4) the VuRigidBody ctor's setupRigidBody calls.
struct bodies_motion_t
{
    body_motion_get_fn get = nullptr;
    void* user = nullptr;
};

// btCylinderShape's ctor 0x5AE160 (thiscall; half extents; retn 4): type 0x23
// first, +8 0, scaling (1, 1, 1, 0), margin 0x3D23D70A (0.04), the Y vtable
// and up axis 1; the margin becomes 0.1 (0x6F7610) x the smallest half extent
// (y > x: z > x ? x : z; else z <= y ? z : y - comiss / cmova / setbe) through
// setMargin (slot 9 0x548900, box_set_margin) when the margin (getMargin, x87)
// is above it; implicit = scaling x half - margin (w 0), type 13. 0x544100
// then re-tags X (type 3: vt 0x7514F4, up 0) or Z (type 5: vt 0x751434, up 2).
// The half extents (createRigidBody): x (h/2 sx, r sy, r sz), y (r sx, h/2 sy,
// r sz), z (r sx, r sy, h/2 sz) - h/2 = height x 0.5 (0x6F765C) - and the scale
// is then (1, 1, 1): no setLocalScaling follows.
inline void bodies_cylinder( convex_shape_t& s, const float* half, int up )
{
    s = convex_shape_t{ };
    s.local_scaling[ 0 ] = s.local_scaling[ 1 ] = s.local_scaling[ 2 ] = 1.0f;
    s.margin = support_bits( 0x3D23D70Au );
    const float x = half[ 0 ], y = half[ 1 ], z = half[ 2 ];
    int axis;
    if ( _mm_comigt_ss( _mm_set_ss( y ), _mm_set_ss( x ) ) )
        axis = _mm_comigt_ss( _mm_set_ss( z ), _mm_set_ss( x ) ) ? 0 : 2;
    else
        axis = !_mm_comigt_ss( _mm_set_ss( z ), _mm_set_ss( y ) ) ? 2 : 1;   // setbe: unordered counts
    const float least = half[ axis ] * support_bits( 0x3DCCCCCDu );
    if ( _mm_comigt_ss( _mm_set_ss( s.margin ), _mm_set_ss( least ) ) )
        s.margin = least;   // box_set_margin's implicit rewrite is overwritten below
    for ( int k = 0; k < 3; ++k )
        s.implicit[ k ] = s.local_scaling[ k ] * half[ k ] - s.margin;
    s.implicit[ 3 ] = 0.0f;
    s.type = 13;
    s.up_axis = up;
}

// btBoxShape's ctor 0x547D80: the same margin rule as the cylinders' 0x5AE160 (the least half extent x 0.1 when the margin
// passes it), type 0. createRigidBody passes Collision Size (+0x30) x 0.5 (0x54421C), unscaled: the scale follows through
// setLocalScaling 0x548A00.
inline void bodies_box( convex_shape_t& s, const float* half )
{
    bodies_cylinder( s, half, 0 );
    s.type = 0;
}

// btSphereShape's ctor 0x43D700 (createRigidBody 0x5441BD): type 8, Collision Radius (+0x2C) at +0x20 and +0x30. Scale
// (1, 1, 1); the other implicit lanes are not written, and nothing reads them (0x43D650 / 0x43D630 read +0x20).
inline void bodies_sphere( convex_shape_t& s, float radius )
{
    s = convex_shape_t{ };
    s.local_scaling[ 0 ] = s.local_scaling[ 1 ] = s.local_scaling[ 2 ] = 1.0f;
    s.implicit[ 0 ] = radius;
    s.margin = radius;
    s.type = 8;
}

// The cylinder as cylinder.hxx's byte views read it (box.hxx's layout + the up
// axis at +0x40).
inline void bodies_cylinder_bytes( const convex_shape_t& s, std::uint8_t* out )
{
    std::memset( out, 0, 0x50 );
    std::memcpy( out + 4, &s.type, 4 );
    std::memcpy( out + 0x10, s.local_scaling, 16 );
    std::memcpy( out + 0x20, s.implicit, 16 );
    std::memcpy( out + 0x30, &s.margin, 4 );
    std::memcpy( out + 0x40, &s.up_axis, 4 );
}

// btPolyhedralConvexShape::calculateLocalInertia 0x54B4C0 (slot 7 of the
// convex hull's vtable 0x74CEBC; thiscall mass, inertia&; retn 8): the margin
// (getMargin, x87), getAabb (slot 1 0x54B6D0: the cached local aabb + that
// margin) under the identity 0x413280, then l = (((hi - lo) x 0.5) + margin) x 2
// (0x6F765C / 0x6F76F8) per lane, k = mass x 0x3DAAAAAA (0x6F7608 - NOT the
// 0x3DAAAAAB the box and compound use), I = ((z^2 + y^2) k, (z^2 + x^2) k,
// (y^2 + x^2) k, 0). (No engine/collision file had it: kept here, world-local.)
inline bool bodies_hull_local_inertia( const convex_shape_t& s, float mass, float* inertia )
{
    float m;
    if ( !shape_get_margin( s, m ) )
        return false;
    static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
    float lo[ 4 ], hi[ 4 ];
    if ( !shape_get_aabb( s, identity, lo, hi ) )
        return false;
    float z = ( hi[ 2 ] - lo[ 2 ] ) * 0.5f, x = ( hi[ 0 ] - lo[ 0 ] ) * 0.5f, y = ( hi[ 1 ] - lo[ 1 ] ) * 0.5f;
    const float k = mass * support_bits( 0x3DAAAAAAu );
    z = ( z + m ) * 2.0f;
    y = ( y + m ) * 2.0f;
    x = ( x + m ) * 2.0f;
    z = z * z;
    y = y * y;
    x = x * x;
    inertia[ 0 ] = ( z + y ) * k;
    inertia[ 1 ] = ( z + x ) * k;
    inertia[ 2 ] = ( y + x ) * k;
    inertia[ 3 ] = 0.0f;
    return true;
}

// VuMatrix -> btTransform as 0x410C30 does it (the basis transposed into rows
// with w 0, then the origin, w 0) - the form boat construct.hxx's motion state
// inlines.
inline void bodies_bt_from_vu( const float* m, float* out )
{
    for ( int row = 0; row < 3; ++row )
    {
        for ( int k = 0; k < 3; ++k )
            out[ 4 * row + k ] = m[ 4 * k + row ];
        out[ 4 * row + 3 ] = 0.0f;
    }
    out[ 12 ] = m[ 12 ];
    out[ 13 ] = m[ 13 ];
    out[ 14 ] = m[ 14 ];
    out[ 15 ] = 0.0f;
}

// The quantized BVH the walk reads, over the asset's loaded VuOptimizedBvh.
inline void bodies_bvh_of( const collision_mesh_asset_t& a, quantized_bvh_t& out )
{
    out = quantized_bvh_t{ };
    std::memcpy( out.aabb_min, a.bvh.header + 0, 16 );
    std::memcpy( out.aabb_max, a.bvh.header + 4, 16 );
    std::memcpy( out.quantization, a.bvh.header + 8, 16 );
    out.cur_node_index = static_cast< int >( a.bvh.header[ 13 ] );
    out.use_quantization = a.bvh.use_quantization;
    out.nodes = reinterpret_cast< const bvh_node_t* >( a.bvh.quantized.bytes.data( ) );
    out.traversal_mode = a.bvh.traversal;
}

struct bodies_hooks_t
{
    void* user = nullptr;
    // g_vudynamicsimpl vt+0x54 (slot 21 0x4F88A0): the surface type's id.
    std::uint8_t ( *surface )( void* user, const std::string& name ) = nullptr;
    // g_vudynamicsimpl vt+0x1C: the world's gravity (12 bytes).
    void ( *gravity )( void* user, float* out ) = nullptr;
};

// setLocalScaling vt+0x14 on the box 0x548A00 / the sphere 0x552F30 (btConvexInternalShape's: |s|, w 0).
inline void bodies_scale_primitive( convex_shape_t& shape, const float* s )
{
    if ( shape.type == 0 )
    {
        std::uint8_t bytes[ 0x50 ];
        bodies_cylinder_bytes( shape, bytes );
        box_set_local_scaling( bytes, s );
        std::memcpy( shape.local_scaling, bytes + 0x10, 16 );
        std::memcpy( shape.implicit, bytes + 0x20, 16 );
        return;
    }
    for ( int k = 0; k < 3; ++k )
        shape.local_scaling[ k ] = std::fabs( s[ k ] );
    shape.local_scaling[ 3 ] = 0.0f;
}

// createRigidBody 0x544100 on a component whose group / mask are set. False:
// a path not recreated (b.name says nothing; `why` says which) - nothing built.
// True with no body (type 0 / no asset): retail builds none either.
// `unique_id` is the global 0x759030 the VuRigidBody ctor takes and bumps.
inline bool bodies_create( level_body_t& b, rigid_component_t& c, const collision_mesh_asset_t* asset, const float* world,
                           const float* scale_in, std::uint32_t& unique_id, const bodies_hooks_t& hooks, std::string& why, bool& built,
                           const bodies_motion_t* motion = nullptr )
{
    float scale[ 3 ] = { scale_in[ 0 ], scale_in[ 1 ], scale_in[ 2 ] };   // the transform's +0xB0 (reset to 1 by the cylinders)
    built = false;
    b.type = c.collision_type;
    if ( c.collision_type == 6 || c.collision_type == 7 )
    {
        b.asset = asset;   // +0xC0 = the asset property's +0x4C
        if ( !asset )
            return true;
    }
    c.flags_d8 &= ~4u;
    if ( c.collision_type == 0 )
        return true;
    b.mesh = striding_mesh_t{ };
    b.mesh.scaling[ 0 ] = b.mesh.scaling[ 1 ] = b.mesh.scaling[ 2 ] = 1.0f;
    if ( asset )
        collision_mesh_striding( *asset, b.mesh );
    bool convex = false;
    if ( c.collision_type == 6 )
    {
        shape_init_triangle_mesh_hull( b.convex, &b.mesh );
        convex = true;
    }
    else if ( c.collision_type == 7 )
    {
        if ( bvh_mesh_shape_init( b.concave, &b.mesh ) < 0 )
        {
            why = "the BVH mesh shape's recalcLocalAabb refused";
            return false;
        }
        bodies_bvh_of( *asset, b.bvh );
        const float unit[ 4 ] = { 1.0f, 1.0f, 1.0f, 0.0f };   // 0x6F7B00
        if ( bvh_mesh_set_optimized_bvh( b.concave, b.mesh, &b.bvh, unit ) < 0 )
        {
            why = "setOptimizedBvh's recalcLocalAabb refused";
            return false;
        }
        if ( asset->flag )
            c.flags_d8 |= 4;
    }
    else if ( c.collision_type >= 3 && c.collision_type <= 5 )
    {
        // 0x5442BD / 0x544352 / 0x544405: the half extents from Collision Radius (+0x2C), Collision Height (+0x40)
        // x 0.5 and the transform's scale; then the scale is (1, 1, 1) (0x544474).
        const float h = c.height * 0.5f;
        float half[ 3 ];
        int up = 1;
        if ( c.collision_type == 3 )
        {
            half[ 0 ] = h * scale[ 0 ];
            half[ 1 ] = c.radius * scale[ 1 ];
            half[ 2 ] = c.radius * scale[ 2 ];
            up = 0;
        }
        else if ( c.collision_type == 4 )
        {
            half[ 0 ] = c.radius * scale[ 0 ];
            half[ 1 ] = h * scale[ 1 ];
            half[ 2 ] = c.radius * scale[ 2 ];
        }
        else
        {
            half[ 0 ] = c.radius * scale[ 0 ];
            half[ 1 ] = c.radius * scale[ 1 ];
            half[ 2 ] = h * scale[ 2 ];
            up = 2;
        }
        bodies_cylinder( b.cylinder, half, up );
        scale[ 0 ] = scale[ 1 ] = scale[ 2 ] = 1.0f;
    }
    else if ( c.collision_type == 1 )
        bodies_sphere( b.cylinder, c.radius );
    else if ( c.collision_type == 2 )
    {
        const float half[ 3 ] = { c.size[ 0 ] * 0.5f, c.size[ 1 ] * 0.5f, c.size[ 2 ] * 0.5f };   // 0x6F765C
        bodies_box( b.cylinder, half );
    }
    else
    {
        why = "createRigidBody 0x544100: collision type " + std::to_string( c.collision_type ) + " is no branch of retail's";
        return false;
    }
    const bool cylinder = c.collision_type >= 3 && c.collision_type <= 5;
    const bool primitive = c.collision_type == 1 || c.collision_type == 2;   // held in b.cylinder too
    const convex_shape_t* single = cylinder || primitive ? &b.cylinder : convex ? &b.convex : nullptr;
    // The offset (+0x5C) against the centre of mass (+0xDC): (dx^2 + dy^2) + dz^2 > 0 (0x6F7570, comiss) -> +0xFC =
    // the shape, +0xF8 = a new btCompoundShape (0x5269C0) holding it under the identity (0x413280) with origin
    // (dx, dy, dz, 0) (addChildShape 0x526BF0).
    const float dx = c.offset[ 0 ] - c.centre[ 0 ], dy = c.offset[ 1 ] - c.centre[ 1 ], dz = c.offset[ 2 ] - c.centre[ 2 ];
    b.has_compound = false;
    if ( _mm_comigt_ss( _mm_set_ss( ( dx * dx + dy * dy ) + dz * dz ), _mm_setzero_ps( ) ) )
    {
        if ( !single )
        {
            why = "createRigidBody 0x544100: a compound over a BVH mesh (addChildShape's insertleaf of a concave child) is not recreated";
            return false;
        }
        b.compound = compound_shape_t{ };
        const float t[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, dx, dy, dz, 0 };
        if ( compound_add_child( b.compound, t, *single ) != 1 )
        {
            why = "addChildShape 0x526BF0 refused";
            return false;
        }
        b.has_compound = true;
    }
    // The transform's scale: each lane `ucomiss` against 1.0 - not equal or unordered -> setLocalScaling (vt+0x14).
    if ( !( scale[ 0 ] == 1.0f && scale[ 1 ] == 1.0f && scale[ 2 ] == 1.0f ) )
    {
        const float s[ 4 ] = { scale[ 0 ], scale[ 1 ], scale[ 2 ], 0.0f };
        if ( b.has_compound )
        {
            // btCompoundShape::setLocalScaling 0x5276D0 over the one child; getLocalScaling vt+0x18 answers the hull's
            // mesh scaling, the primitive's own (the cylinders never come here: their scale is (1, 1, 1) by now).
            const bool ok = compound_set_local_scaling( b.compound, s, [ & ]( compound_child_t&, const float* v ) -> const float* {
                if ( !v )
                    return convex ? b.mesh.scaling : b.cylinder.local_scaling;
                if ( convex )
                    shape_set_triangle_mesh_scaling( b.convex, b.mesh, v );
                else
                    bodies_scale_primitive( b.cylinder, v );
                return nullptr;
            } );
            if ( !ok )
            {
                why = "createRigidBody 0x544100: the compound's setLocalScaling 0x5276D0 refused";
                return false;
            }
        }
        else if ( primitive )
            bodies_scale_primitive( b.cylinder, s );
        else if ( convex )
            shape_set_triangle_mesh_scaling( b.convex, b.mesh, s );
        // btBvhTriangleMeshShape::setLocalScaling 0x5AF300 with its rebuild branch (engine collision/rebuild.hxx, the
        // boat agent's verified overload - case bvh_build): a new btOptimizedBvh over the rescaled mesh, one per body.
        else if ( bvh_mesh_set_local_scaling( b.concave, b.mesh, s, b.rebuilt ) < 0 )
        {
            why = "createRigidBody 0x544100: setLocalScaling 0x5AF300's BVH rebuild refused";
            return false;
        }
    }
    // The mass (+0xEC) > 0 (comiss) with a shape: its calculateLocalInertia (vt+0x1C) into a zeroed vector.
    float inertia[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if ( _mm_comigt_ss( _mm_set_ss( c.mass ), _mm_setzero_ps( ) ) )
    {
        if ( b.has_compound )
            compound_local_inertia( b.compound, c.mass, inertia );
        else if ( cylinder || primitive )
        {
            std::uint8_t bytes[ 0x50 ];
            bodies_cylinder_bytes( b.cylinder, bytes );
            if ( c.collision_type == 1 )
                sphere_local_inertia( bytes, c.mass, inertia );   // slot 7 0x5535A0
            else if ( c.collision_type == 2 )
                box_local_inertia( bytes, c.mass, inertia );      // slot 7 0x5480C0
            else
                cylinder_local_inertia( bytes, c.mass, inertia );
        }
        else if ( convex )
        {
            if ( !bodies_hull_local_inertia( b.convex, c.mass, inertia ) )
            {
                why = "the convex hull's calculateLocalInertia 0x54B4C0 refused";
                return false;
            }
        }
        else
        {
            why = "createRigidBody 0x544100: the triangle mesh's calculateLocalInertia is not recreated";
            return false;
        }
    }
    // 0x4131E0(mass, motion state +0xF0, shape, inertia) then the start transform = 0x410C30(the owner's world).
    boat_4131e0_t cinfo;
    std::uint32_t mass;
    std::memcpy( &mass, &c.mass, 4 );
    // The motion state (+0xF0: 0 for a VuPropEntity - VuDynamicPropEntity's 0x545A80 sets it) and the shape are
    // pointer words (setCollisionShape: held by level_body_t); a motion state's own word is a marker here (1) so
    // setupRigidBody takes its getWorldTransform over the start transform, as retail does.
    boat_4131e0( cinfo, mass, motion && motion->get ? 1u : 0u, 0, reinterpret_cast< const std::uint8_t* >( inertia ) );
    float bt[ 16 ];
    bodies_bt_from_vu( world, bt );
    std::memcpy( cinfo.image + 0x10, bt, 64 );
    if ( !vu_rigid_body_construct( b.body, cinfo.image, static_cast< short >( c.group ), static_cast< short >( c.mask ), unique_id,
                                   motion ? motion->get : nullptr, motion ? motion->user : nullptr ) )
    {
        why = "the VuRigidBody ctor 0x527B80 refused";
        return false;
    }
    body_word( b.body, 0xD8 ) = c.flags_d0;
    body_word( b.body, 0x2D0 ) = c.flags_d8;
    body_word( b.body, 0x2C4 ) = c.word_f4;
    if ( !hooks.surface || !hooks.gravity )
    {
        why = "createRigidBody 0x544100: g_vudynamicsimpl's surface / gravity not wired";
        return false;
    }
    body_byte( b.body, 0x2C8 ) = hooks.surface( hooks.user, c.surface );
    float g[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
    hooks.gravity( hooks.user, g );
    rigid_set_gravity( b.body.body, g );
    const float ld = body_clamp01( c.linear_damping ), ad = body_clamp01( c.angular_damping );
    std::memcpy( &body_word( b.body, 0x1D0 ), &ld, 4 );
    std::memcpy( &body_word( b.body, 0x1D4 ), &ad, 4 );
    std::memcpy( &body_word( b.body, 0x1EC ), &c.linear_sleep, 4 );
    std::memcpy( &body_word( b.body, 0x1F0 ), &c.angular_sleep, 4 );
    if ( !c.initially_active )
        body_word( b.body, 0xE8 ) = 0x41200000u;   // 10.0
    b.flags_d8 = c.flags_d8;
    b.collision = collision_object_t{ };
    if ( b.has_compound )
        b.collision.body = concave_body_t{ b.body.body.basis, nullptr, nullptr, &b.body.body, &b.compound, rigid_bits( body_word( b.body, 0xC4 ) ) };
    else
        b.collision.body = concave_body_t{ b.body.body.basis, single, single ? nullptr : &b.concave, &b.body.body, nullptr,
                                           rigid_bits( body_word( b.body, 0xC4 ) ) };
    built = true;
    return true;
}

// VuPropEntity gameInitialize 0x546910: the component's group 1 / mask 0xFFFE
// (+0xD4 / +0xD6; the body's +0x2CA / +0x2CC and its proxy's +4 / +6 when one
// exists - none before the build), createRigidBody, then - Initially Visible
// (+0x78) - the draw component's registration with g 0x7590B8 vt+0x28 (the
// renderer: `draw`, a hook) and, with a body not yet in the world (+0x104),
// g_vudynamicsimpl vt+0x3C addRigidBody (`add`) and +0x104 = 1.
template < typename draw_fn, typename add_fn >
inline bool bodies_prop_game_initialize( level_body_t& b, rigid_component_t& c, std::uint8_t visible, const collision_mesh_asset_t* asset,
                                         const float* world, const float* scale, std::uint32_t& unique_id, const bodies_hooks_t& hooks,
                                         draw_fn draw, add_fn add, std::string& why )
{
    c.group = 1;
    c.mask = 0xFFFE;
    bool built = false;
    if ( !bodies_create( b, c, asset, world, scale, unique_id, hooks, why, built ) )
        return false;
    b.built = built;
    if ( visible )
    {
        draw( );
        if ( !b.in_world && built )
        {
            if ( !add( b ) )
            {
                why = "addRigidBody (g_vudynamicsimpl vt+0x3C) refused";
                return false;
            }
            b.in_world = true;
        }
    }
    return true;
}

// VuRigidBody's dtor 0x50FAC0 -> 0x50FAF0 (case rigid_body_dtor), on the body's retail image (0x2D4 bytes): +0 =
// 0x74C644; when the constraint-ref array (+0x204) is non-null - with +0x208 (the byte: owns) set, gNumAlignedFree
// 0x759050 += 1 and btAlignedFree (the function pointer at 0x7358AC) on it - +0x204 = 0; then +0x208 = 1, +0x204 =
// +0x1FC (the count) = +0x200 (the capacity) = 0, +0 = 0x74DEBC (btCollisionObject's, whose own dtor is empty here).
// The delete (flags & 1) is the allocator's. `aligned_free` = the CRT's / the platform's (a hook).
inline void rigid_body_destroy( std::uint8_t* body, std::uint32_t& aligned_frees, const std::function< void( std::uint32_t ) >& aligned_free )
{
    std::uint32_t w = 0x74C644u;
    std::memcpy( body, &w, 4 );
    std::uint32_t refs;
    std::memcpy( &refs, body + 0x204, 4 );
    if ( refs )
    {
        if ( body[ 0x208 ] )
        {
            ++aligned_frees;
            if ( aligned_free )
                aligned_free( refs );
        }
        std::memset( body + 0x204, 0, 4 );
    }
    body[ 0x208 ] = 1;
    std::memset( body + 0x1FC, 0, 12 );
    w = 0x74DEBCu;
    std::memcpy( body, &w, 4 );
}