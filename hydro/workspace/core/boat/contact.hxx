#pragma once

#include <cmath>

#include <workspace/core/boat/hull.hxx>
#include <workspace/core/boat/meshes.hxx>
#include <workspace/core/boat/surface.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include <xmmintrin.h>

// Bullet's gContactAddedCallback (0x7590D8, installed by the contact
// manager's ctor 0x574EC0 and cleared by its dtor 0x575150): 0x574E90
// (cdecl(cp, obj0, part0, index0, obj1, part1, index1) -> false) hands the
// manifold point, both objects and index1 to g_vudynamicsimpl vt+0x70's
// object - the surface-type table VuDynamicsContactManagerImpl (+0x38) -
// at 0x5751F0 (thiscall; retn 0x1C; part0 / index0 / part1 unread). This is
// where boat-vs-world contacts get their friction and where the collision
// mesh's edges are smoothed.
//
// The manifold point is Bullet's btManifoldPoint as retail lays it out (an
// image): +0x10 the local point on B, +0x20 the world point on B, +0x30 the
// world point on A, +0x40 the normal on B (w +0x4C), +0x54 the combined
// friction.

// An object as 0x5751F0 / 0x519720 read it.
struct contact_body_t
{
    // The btCollisionObject image: +0x10 / +0x20 / +0x30 the world basis
    // rows, +0xF4 the internal type (2: a rigid body), +0x2C4 the contact
    // callback (VuRigidBodyContactCallback*; nonzero = present), byte +0x2C8
    // the surface id.
    const std::uint8_t* image = nullptr;
    // The shape's (+0xD4) +8: a collision mesh (VuCollisionMeshAsset), or none.
    const collision_mesh_asset_t* mesh = nullptr;
    std::uint32_t handle = 0;   // the object as the record names it
};

// The record 0x5751F0 builds on its stack and hands every listener and both
// callbacks (by pointer: what they write back is what follows reads).
struct contact_record_t
{
    std::uint32_t body0 = 0, body1 = 0;   // +0x00 / +0x04
    std::uint32_t other = 0;              // +0x08 (0; the other body for a callback)
    float pos[ 4 ] = { };                 // +0x0C (w +0x18: stale stack - left 0)
    float normal[ 4 ] = { };              // +0x1C (w +0x28: stale stack - left 0)
    std::uint8_t surface0 = 0;            // +0x2C
    std::uint8_t surface1 = 0;            // +0x2D (+0x2E / +0x2F stale)
    float friction = 0.0f;                // +0x30
};

// 0x43E420 VuRampEntity's contact callback, the boat's branch (the other's owner +0x2C0 a VuBoatEntity): a normal with z below -0.9 (the record as
// given: its sign depends on which object the ramp is) compares d = n.v with p = v + n d (retail adds; not the tangential part): |p| above |d|
// sets the friction to minus |d| / |p|. True: the hull's byte +0x174 = 1 (0x43E420's last store, boat +0x228).
inline bool ramp_contact( contact_record_t& r, const float* v )
{
    const float nx = r.normal[ 0 ], ny = r.normal[ 1 ], nz = r.normal[ 2 ];
    if ( !( nz < -0.89999998f ) )
        return false;
    const float d = ( nx * v[ 0 ] + ny * v[ 1 ] ) + nz * v[ 2 ];
    const float py = ny * d + v[ 1 ], px = nx * d + v[ 0 ], pz = nz * d + v[ 2 ];
    const float a = ( ( v[ 1 ] - py ) * ( v[ 1 ] - py ) + ( v[ 0 ] - px ) * ( v[ 0 ] - px ) ) + ( v[ 2 ] - pz ) * ( v[ 2 ] - pz );
    const float b = ( py * py + px * px ) + pz * pz;
    if ( b > a )
        r.friction = -( std::sqrt( a ) / std::sqrt( b ) );
    return true;
}

struct contact_hooks_t
{
    // The manager's +4 std::list, in order: each listener's vt+0(&record).
    std::vector< std::function< void( contact_record_t& ) > > listeners;
    // obj0's / obj1's +0x2C4 vt+0(&record) (called only when the word is set).
    std::function< void( contact_record_t& ) > callback0, callback1;
};

inline float contact_f( const std::uint8_t* p, int at )
{
    float v;
    std::memcpy( &v, p + at, 4 );
    return v;
}

inline void contact_put( std::uint8_t* p, int at, float v )
{
    std::memcpy( p + at, &v, 4 );
}

inline std::uint32_t contact_u32( const std::uint8_t* p, int at )
{
    std::uint32_t v;
    std::memcpy( &v, p + at, 4 );
    return v;
}

// minss(1.0, t) then maxss(0.0, that): a NaN t passes through.
inline float contact_clamp01( float t )
{
    return _mm_cvtss_f32( _mm_max_ss( _mm_setzero_ps( ), _mm_min_ss( _mm_set_ss( 1.0f ), _mm_set_ss( t ) ) ) );
}

// One edge test of 0x519720: the point p's distance to the segment a -> b
// (the parameter clamped by contact_clamp01) is ordered below 0.01
// (0x6F75D4).
inline bool contact_near_edge( const float* a, const float* b, const float* p )
{
    const float ex = b[ 0 ] - a[ 0 ], ey = b[ 1 ] - a[ 1 ], ez = b[ 2 ] - a[ 2 ];
    const float dx = p[ 0 ] - a[ 0 ], dy = p[ 1 ] - a[ 1 ], dz = p[ 2 ] - a[ 2 ];
    const float num = ( ey * dy + ex * dx ) + ez * dz;
    const float den = ( ex * ex + ey * ey ) + ez * ez;
    const float t = contact_clamp01( num / den );
    const float qy = ( ey * t + a[ 1 ] ) - p[ 1 ];
    const float qx = ( ex * t + a[ 0 ] ) - p[ 0 ];
    const float qz = ( ez * t + a[ 2 ] ) - p[ 2 ];
    const float d = hull_sqrt( ( qy * qy + qx * qx ) + qz * qz );
    return _mm_comigt_ss( _mm_set_ss( 0.01f ), _mm_set_ss( d ) ) != 0;
}

// 0x519720 (thiscall, the collision mesh; cp, obj1, the triangle; retn 0xC).
// The triangle's word (+0x70; low byte: bits 0-4 the material, 0x20 / 0x40 /
// 0x80 flag the edges v0-v1 / v1-v2 / v2-v0). Unless the local point on B is
// near a flagged edge, the normal on B becomes the triangle's face normal
// (cross(v1 - v0, v2 - v0) x (1 / VuMath_sqrt(|c|^2))) through obj1's basis,
// w 0. False (UNKNOWN, nothing written) for a triangle or vertex past the
// arrays (retail reads them unchecked).
inline bool contact_mesh_edges( const collision_mesh_asset_t& m, std::uint8_t* cp, const std::uint8_t* body1, std::int32_t tri )
{
    const std::size_t t = static_cast< std::size_t >( static_cast< std::uint32_t >( tri ) );
    if ( 6 * t + 6 > m.indices.bytes.size( ) || 4 * t + 4 > m.triangles.bytes.size( ) )
        return false;
    const float* v[ 3 ];
    for ( int k = 0; k < 3; ++k )
    {
        std::uint16_t i;
        std::memcpy( &i, m.indices.bytes.data( ) + 6 * t + 2 * k, 2 );
        if ( 16u * i + 12u > m.vertices.bytes.size( ) )
            return false;
        v[ k ] = reinterpret_cast< const float* >( m.vertices.bytes.data( ) + 16u * i );
    }
    const std::uint8_t flags = m.triangles.bytes[ 4 * t ];
    const float p[ 3 ] = { contact_f( cp, 0x10 ), contact_f( cp, 0x14 ), contact_f( cp, 0x18 ) };
    bool on_edge = false;
    if ( flags & 0x20 )
        on_edge = contact_near_edge( v[ 0 ], v[ 1 ], p );
    if ( flags & 0x40 )
        on_edge = contact_near_edge( v[ 1 ], v[ 2 ], p ) || on_edge;
    if ( ( flags & 0x80 ) && contact_near_edge( v[ 2 ], v[ 0 ], p ) )
        return true;
    if ( on_edge )
        return true;
    const float e1x = v[ 1 ][ 0 ] - v[ 0 ][ 0 ], e1y = v[ 1 ][ 1 ] - v[ 0 ][ 1 ], e1z = v[ 1 ][ 2 ] - v[ 0 ][ 2 ];
    const float e2x = v[ 2 ][ 0 ] - v[ 0 ][ 0 ], e2y = v[ 2 ][ 1 ] - v[ 0 ][ 1 ], e2z = v[ 2 ][ 2 ] - v[ 0 ][ 2 ];
    const float cx = e1y * e2z - e1z * e2y;
    const float cy = e1z * e2x - e1x * e2z;
    const float cz = e1x * e2y - e1y * e2x;
    const float inv = 1.0f / hull_sqrt( ( cy * cy + cx * cx ) + cz * cz );
    const float nx = inv * cx, ny = inv * cy, nz = inv * cz;
    const std::uint8_t* b = body1;
    contact_put( cp, 0x40, ( contact_f( b, 0x14 ) * ny + nx * contact_f( b, 0x10 ) ) + contact_f( b, 0x18 ) * nz );
    contact_put( cp, 0x44, ( contact_f( b, 0x24 ) * ny + nx * contact_f( b, 0x20 ) ) + contact_f( b, 0x28 ) * nz );
    contact_put( cp, 0x48, ( contact_f( b, 0x34 ) * ny + nx * contact_f( b, 0x30 ) ) + contact_f( b, 0x38 ) * nz );
    contact_put( cp, 0x4C, 0.0f );
    return true;
}

// 0x5751F0 (from 0x574E90, which always answers false). Only when both
// objects' +0xF4 is 2: the surfaces (obj0's +0x2C8; obj1's - or, with a
// mesh, its triangle's material's byte +40 - then 0x519720); the record:
// pos = (world B + world A) x 0.5 per lane, the normal on B, friction =
// type(s1).friction x type(s0).friction (slot 3 0x574E70); every listener;
// obj0's callback (other = obj1); the record's normal negated (x, y, z);
// obj1's callback (other = the record's body0); cp +0x54 = the record's
// friction. False (UNKNOWN: retail reads past them) for a surface id past
// the table or a material past the mesh - after whatever came first.
inline bool contact_added( const surface_table_t& table, std::uint8_t* cp, const contact_body_t& o0, const contact_body_t& o1, std::int32_t index1,
                           const contact_hooks_t& h )
{
    if ( contact_u32( o0.image, 0xF4 ) != 2 || contact_u32( o1.image, 0xF4 ) != 2 )
        return true;
    contact_record_t r;
    r.body0 = o0.handle;
    r.body1 = o1.handle;
    r.other = 0;
    r.surface0 = o0.image[ 0x2C8 ];
    r.surface1 = o1.image[ 0x2C8 ];
    if ( o1.mesh )
    {
        const collision_mesh_asset_t& m = *o1.mesh;
        const std::size_t t = static_cast< std::size_t >( static_cast< std::uint32_t >( index1 ) );
        if ( 4 * t + 4 > m.triangles.bytes.size( ) )
            return false;
        const std::size_t at = 44u * ( m.triangles.bytes[ 4 * t ] & 0x1Fu ) + 40u;
        if ( at >= m.materials.bytes.size( ) )
            return false;
        r.surface1 = m.materials.bytes[ at ];
        if ( !contact_mesh_edges( m, cp, o1.image, index1 ) )
            return false;
    }
    for ( int k = 0; k < 3; ++k )
        r.pos[ k ] = ( contact_f( cp, 0x30 + 4 * k ) + contact_f( cp, 0x20 + 4 * k ) ) * 0.5f;
    for ( int k = 0; k < 3; ++k )
        r.normal[ k ] = contact_f( cp, 0x40 + 4 * k );
    const surface_type_t* t1 = surface_type( table, r.surface1 );
    const surface_type_t* t0 = surface_type( table, r.surface0 );
    if ( !t1 || !t0 )
        return false;
    r.friction = t1->friction * t0->friction;
    for ( const auto& l : h.listeners )
        if ( l )
            l( r );
    if ( contact_u32( o0.image, 0x2C4 ) )
    {
        r.other = r.body1;
        if ( h.callback0 )
            h.callback0( r );
    }
    for ( int k = 0; k < 3; ++k )
        r.normal[ k ] = -r.normal[ k ];
    if ( contact_u32( o1.image, 0x2C4 ) )
    {
        r.other = r.body0;
        if ( h.callback1 )
            h.callback1( r );
    }
    contact_put( cp, 0x54, r.friction );
    return true;
}
