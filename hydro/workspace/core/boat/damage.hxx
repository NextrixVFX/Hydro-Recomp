#pragma once

#include <workspace/core/boat/hull.hxx>
#include <workspace/core/boat/events.hxx>

#include <cstdint>
#include <cstring>

// VuBoatDamageController::VuPiece (the loader 0x412270 installs both
// vtables): a btMotionState at +0 (vtable 0x738088: 0 = the scalar-deleting
// dtor 0x412A70 -> 0x412B00, 1 = getWorldTransform 0x412CA0, 2 =
// setWorldTransform 0x412D90) and the fluid step interface at +4 (vtable
// 0x73807C: 0x410B30, 0x412E30, 0x6F9724). +8 is the piece's world matrix (Vu
// rows), +0x1B0 the offset of its body's origin in it. On the piece's image.

// getWorldTransform 0x412CA0 (thiscall(piece, btTransform* out), retn 4):
// translation(+0x1B0) (0x410AB0) . +8 (VuMatrix_multiply 0x4078C0), as a
// btTransform (0x410C30: the basis transposed into rows with w 0, the origin
// row 3 with w 0).
inline void piece_motion_get( const std::uint8_t* piece, float* out )
{
    float world[ 16 ], offset[ 16 ], product[ 16 ];
    std::memcpy( world, piece + 8, 64 );
    float v[ 3 ];
    std::memcpy( v, piece + 0x1B0, 12 );
    hull_translation( offset, v );
    hull_multiply( offset, product, world );
    const int column[ 3 ][ 3 ] = { { 0, 4, 8 }, { 1, 5, 9 }, { 2, 6, 10 } };
    for ( int row = 0; row < 3; ++row )
    {
        for ( int k = 0; k < 3; ++k )
            out[ 4 * row + k ] = product[ column[ row ][ k ] ];
        out[ 4 * row + 3 ] = 0.0f;
    }
    out[ 12 ] = product[ 12 ];
    out[ 13 ] = product[ 13 ];
    out[ 14 ] = product[ 14 ];
    out[ 15 ] = 0.0f;
}

// setWorldTransform 0x412D90 (thiscall(piece, btTransform const*), retn 4):
// +8 = 0x410B40(in), then +8 = translation(-(+0x1B0)) . +8.
inline void piece_motion_set( std::uint8_t* piece, const float* bt )
{
    float m[ 16 ], t[ 16 ], product[ 16 ];
    hull_vu_from_bt( m, bt );
    std::memcpy( piece + 8, m, 64 );
    float v[ 3 ];
    std::memcpy( v, piece + 0x1B0, 12 );
    v[ 0 ] = -v[ 0 ];
    v[ 1 ] = -v[ 1 ];
    v[ 2 ] = -v[ 2 ];
    hull_translation( t, v );
    hull_multiply( t, product, m );
    std::memcpy( piece + 8, product, 64 );
}

// The fluid step 0x412E30 (vtable 0x73807C slot 1; thiscall this = piece +
// 4; dt; retn 4), on the piece's image (this-relative offsets in brackets):
// M = translation(-(+0x1B0)) . 0x410B40(the body's btTransform) - the
// setWorldTransform product; VuFluidSolver_prepare on the piece's solver
// (piece +0x78 [+0x74]) with M, solve(the body, piece +0x208 [+0x204]) when
// the solver has a mesh. Then with F / T = the solver's force +0x60 / torque
// +0x70, d = its water normal +0xE0, u = its mean flow +0xF0 (piece +0xD8 /
// +0xE8 / +0x158 / +0x168): dF = (d0 F.x + d1 F.y) + d2 F.z, dv = (d0 (v.x -
// u0) + d1 (v.y - u1)) + d2 (v.z - u2) (v the body's +0x140); when dF > 0,
// 0 > dv, s = (1/m (+0x160) x dF) x dt > 12 (0x6F778C) and s > -dv (each
// `comiss; jbe`): F and T x= -(dv / s) - the force that would reverse the
// flow is capped; then the body's total force +0x1B0 += F x linear factor
// +0x170 and total torque +0x1C0 += T x angular factor +0x260 (each in the
// disassembly's operand order). The solver's inputs (+4 the offset, +0x14
// drag, +0x18 buoyancy, +0x1C velocity scale, +0x20 axis drag, +0x100 the
// floor) are read from the image; its outputs (+0x50, +0x54 .. +0xFF) are
// written back. The solver's excluded waves (+0x30) are the caller's (s.
// exclude, resolved as boat_thunk_in_t::solver_excluded); a count with no
// matching exclusions is UNKNOWN (false). False also when prepare / solve
// reach a path not recreated.
inline bool piece_fluid_step( fluid_solver_t& s, std::uint8_t* piece, std::uint8_t* body, const water_t& water, water_kernel_fn kernel,
                              void* kernel_user, fluid_gravity_fn gravity, void* gravity_user, float dt )
{
    std::uint8_t* sv = piece + 0x78;
    std::int32_t excluded;
    std::memcpy( &excluded, sv + 0x30, 4 );
    if ( excluded > 0 && s.exclude.size( ) != static_cast< std::size_t >( excluded ) )
        return false;
    float bt[ 16 ], m[ 16 ], t[ 16 ], M[ 16 ];
    std::memcpy( bt, body + 16, 64 );
    hull_vu_from_bt( m, bt );
    float c[ 3 ];
    std::memcpy( c, piece + 0x1B0, 12 );
    for ( float& x : c )
        x = support_negate( x );
    hull_translation( t, c );
    hull_multiply( t, M, m );
    std::memcpy( s.offset, sv + 4, 12 );
    std::memcpy( &s.drag, sv + 0x14, 4 );
    std::memcpy( &s.buoyancy, sv + 0x18, 4 );
    std::memcpy( &s.velocity_scale, sv + 0x1C, 4 );
    std::memcpy( s.axis_drag, sv + 0x20, 12 );
    std::memcpy( &s.floor_z, sv + 0x100, 4 );
    if ( !fluid_prepare( s, water, M, kernel, kernel_user ) )
        return false;
    if ( s.mesh )
    {
        fluid_body_t fb;
        std::memcpy( fb.transform, body + 16, 64 );
        std::memcpy( fb.linear, body + 320, 12 );
        std::memcpy( fb.angular, body + 336, 12 );
        std::memcpy( &fb.inverse_mass, body + 352, 4 );
        if ( !fluid_solve( s, fb, gravity, gravity_user ) )
            return false;
    }
    sv[ 0x50 ] = s.any_submerged ? 1 : 0;
    std::memcpy( sv + 0x54, &s.volume, 4 );
    std::memcpy( sv + 0x58, &s.area, 4 );
    std::memcpy( sv + 0x5C, &s.min_depth, 4 );
    std::memcpy( sv + 0x60, s.acc, sizeof( s.acc ) );
    float fx = s.acc[ 0 ], fy = s.acc[ 1 ], fz = s.acc[ 2 ], tx = s.acc[ 4 ], ty = s.acc[ 5 ], tz = s.acc[ 6 ];
    const float d0 = s.acc[ 32 ], d1 = s.acc[ 33 ], d2 = s.acc[ 34 ];
    const float df = sse_add( sse_add( sse_mul( d0, fx ), sse_mul( d1, fy ) ), sse_mul( d2, fz ) );
    const float dv = sse_add( sse_add( sse_mul( d0, sse_sub( state_f( body, 0x140 ), s.acc[ 36 ] ) ), sse_mul( d1, sse_sub( state_f( body, 0x144 ), s.acc[ 37 ] ) ) ),
                              sse_mul( d2, sse_sub( state_f( body, 0x148 ), s.acc[ 38 ] ) ) );
    if ( df > 0.0f && 0.0f > dv )
    {
        const float sc = sse_mul( sse_mul( state_f( body, 0x160 ), df ), dt );
        if ( sc > 12.0f && sc > support_negate( dv ) )
        {
            const float k = support_negate( dv / sc );
            fz = sse_mul( fz, k );
            fx = sse_mul( fx, k );
            fy = sse_mul( fy, k );
            tx = sse_mul( tx, k );
            ty = sse_mul( ty, k );
            tz = sse_mul( tz, k );
        }
    }
    state_put_f( body, 0x1B0, sse_add( sse_mul( fx, state_f( body, 0x170 ) ), state_f( body, 0x1B0 ) ) );
    state_put_f( body, 0x1B8, sse_add( sse_mul( state_f( body, 0x178 ), fz ), state_f( body, 0x1B8 ) ) );
    state_put_f( body, 0x1B4, sse_add( sse_mul( state_f( body, 0x174 ), fy ), state_f( body, 0x1B4 ) ) );
    state_put_f( body, 0x1C0, sse_add( sse_mul( tx, state_f( body, 0x260 ) ), state_f( body, 0x1C0 ) ) );
    state_put_f( body, 0x1C4, sse_add( sse_mul( state_f( body, 0x264 ), ty ), state_f( body, 0x1C4 ) ) );
    state_put_f( body, 0x1C8, sse_add( sse_mul( state_f( body, 0x268 ), tz ), state_f( body, 0x1C8 ) ) );
    return true;
}
// The dtor body 0x412B00 (thiscall piece; plain ret - reached through the
// scalar-deleting dtor 0x412A70), on the piece's image: the two VuPiece
// vtables (+0 0x738088, +4 0x73807C); when +0x20C: g_vudynamicsimpl vt+0x40
// (the body word +0x208) and vt+0x30 (piece + 4), +0x20C = 0; when +0x228:
// g_vupfximpl vt+0x2C (it); +0x228 = 0; g_vuassetfactoryimpl vt+0x24 (+0x1A0,
// even 0); +0x200's / +0x204's slot 0 (1) and +0x208's slot 1 (1) when set;
// the std::string +0x210 (operator delete of +0x210 when its capacity +0x224
// >= 16 - `jb`), +0x224 = 15, +0x220 = 0, byte +0x210 = 0; +0x180 =
// btStridingMeshInterface's vtable 0x738128; the solver's asset (+0x78) to the
// asset factory's vt+0x24 when set; operator delete[] (+0xAC); _aligned_free
// (+0xBC), (+0xB0); +0x68 = VuModelSkin's vtable 0x738068 and its dtor
// 0x521E40 (piece + 0x68); _aligned_free (+0x6C); +0x48 =
// VuStaticModelInstance's 0x74CDAC and its dtor 0x520000 (piece + 0x48), then
// VuModelInstance's 0x7505F8; +0 = btMotionState's 0x7380B4. Every callee is
// a hook (the dynamics, pfx and asset systems, the owned objects' dtors, the
// allocator, the model dtors - not recreated), told in order: code, word.
//   1 dynamics vt+0x40, 2 dynamics vt+0x30, 3 pfx vt+0x2C, 4 the asset factory's
//   vt+0x24 (twice), 5 / 6 slot 0 of +0x200 / +0x204, 7 slot 1 of +0x208, 8
//   operator delete, 10 operator delete[], 11 _aligned_free (three times), 13
//   0x521E40, 15 0x520000.
using piece_destroy_fn = void ( * )( void* user, int code, std::uint32_t word );

inline void piece_destroy( std::uint8_t* p, std::uint32_t self, piece_destroy_fn hook, void* user )
{
    auto w = [ & ]( int at ) { std::uint32_t x; std::memcpy( &x, p + at, 4 ); return x; };
    auto put = [ & ]( int at, std::uint32_t x ) { std::memcpy( p + at, &x, 4 ); };
    put( 0, 0x738088u );
    put( 4, 0x73807Cu );
    if ( p[ 0x20C ] )
    {
        hook( user, 1, w( 0x208 ) );
        hook( user, 2, self + 4 );
        p[ 0x20C ] = 0;
    }
    if ( w( 0x228 ) )
        hook( user, 3, w( 0x228 ) );
    put( 0x228, 0 );
    hook( user, 4, w( 0x1A0 ) );
    if ( w( 0x200 ) )
        hook( user, 5, w( 0x200 ) );
    if ( w( 0x204 ) )
        hook( user, 6, w( 0x204 ) );
    if ( w( 0x208 ) )
        hook( user, 7, w( 0x208 ) );
    if ( w( 0x224 ) >= 16u )
        hook( user, 8, w( 0x210 ) );
    put( 0x224, 15 );
    put( 0x220, 0 );
    p[ 0x210 ] = 0;
    put( 0x180, 0x738128u );
    if ( w( 0x78 ) )
        hook( user, 4, w( 0x78 ) );
    hook( user, 10, w( 0xAC ) );
    hook( user, 11, w( 0xBC ) );
    hook( user, 11, w( 0xB0 ) );
    put( 0x68, 0x738068u );
    hook( user, 13, self + 0x68 );
    hook( user, 11, w( 0x6C ) );
    put( 0x48, 0x74CDACu );
    hook( user, 15, self + 0x48 );
    put( 0x48, 0x7505F8u );
    put( 0, 0x7380B4u );
}

// 0x4120F0 (thiscall piece; one stack word, unused; retn 4), called by the breakable mesh load 0x411EF0: +0x50 .. +0x58,
// +0x6C, +0x78, +0xAC, +0xB0, +0xBC, +0x1A0, +0x200, +0x204, +0x208, +0x228 = 0.
inline void piece_clear( std::uint8_t* p )
{
    for ( const int at : { 0x50, 0x54, 0x58, 0x6C, 0x78, 0xAC, 0xB0, 0xBC, 0x1A0, 0x200, 0x204, 0x208, 0x228 } )
        std::memset( p + at, 0, 4 );
}
