#pragma once

// VuGhostBoatEntity (vtable 0x738474, RTTI 0x736294; 0x5A0 bytes = VuBoatEntity's 0x520 + its own tail) and the making of
// one, VuGhostManager's 0x4AF640 (boat batch 33). Read from the disassembly. The tail: +0x520 the replayed data (the
// record's +4), +0x524 the replay clock, +0x528 the frame index, +0x52C a VuUIFont (ctor 0x626900, reloaded by 0x626B00 as
// "HUD_PlayerName"; its params at +0x20 = boat +0x54C), +0x590 / +0x594 the two GhostFadeDist values.
//
// The vtable against VuBoatEntity's 0x7383D4 (tmp/b33/vt2.py): slot 0 0x422520 (0x626A40 on the font, then boat_destroy
// 0x41CB10), slot 1 0x422300, slot 12 0x422590 (the draw, ghost_boat_fade + ghost_boat_label), slot 13 0x422A10 (the
// tick, ghost_boat_tick), slot 17 0x401560 (a plain ret: gameInitialize does NOT put the ghost's body into the world;
// the tick does, the first time the replay has ended). Every other slot is VuBoatEntity's, and so is the +0x5C / +0x60 /
// +0x64 interface's behaviour (only its slot 0 thunk 0x422FCB differs). The driver is VuGhostDriver (Driver Type "Ghost",
// boat/pilots.hxx).

#include <workspace/core/boat/events.hxx>
#include <workspace/core/boat/euler.hxx>
#include <workspace/core/boat/hull.hxx>
#include <workspace/core/boat/math.hxx>
#include <workspace/core/boat/state.hxx>
#include <workspace/core/dynamics/body.hxx>
#include <workspace/core/entity/json.hxx>
#include <workspace/core/world/ghost.hxx>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <emmintrin.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// divss (not commutative: no operand order to keep; the hardware's own NaN rule).
inline float ghost_div( float a, float b ) { return _mm_cvtss_f32( _mm_div_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); }

// One frame's rotation: the three int16s at +0x10 x 0x6F759C (the inverse of 0x6F7834): cvtdq2ps, mulss.
inline void ghost_frame_euler( const std::uint8_t* f, float* e )
{
    for ( int k = 0; k < 3; ++k )
    {
        std::int16_t v;
        std::memcpy( &v, f + 0x10 + 2 * k, 2 );
        e[ k ] = sse_mul( static_cast< float >( static_cast< std::int32_t >( v ) ), water_bits( 0x38C92C25u ) );
    }
}

// ---- Slot 13 0x422A10 (thiscall the boat; dt; retn 4), the ghost's Decision tick ----------------------------------
//
// The callees that are not this function's: 0x410FF0 boat_debris_launch and 0x41E240 (boat/effects.hxx), 0x5344A0( matrix,
// notify 0 ) on the transform (world/transform.hxx), VuFluidSolver_prepare 0x524450 on the hull's (+0x228) +0x1CC solver
// (boat/fluid.hxx), g_vudynamicsimpl's vt+0x3C (the body) and vt+0x2C (the boat's +0x60), the plain Decision tick 0x41EFF0.
struct ghost_replay_hooks_t
{
    void* user = nullptr;
    void ( *debris_launch )( void* user ) = nullptr;
    void ( *destroyed_event )( void* user ) = nullptr;
    void ( *set_world )( void* user, const float* m16 ) = nullptr;
    void ( *solver_prepare )( void* user, const float* m16 ) = nullptr;
    void ( *add_body )( void* user ) = nullptr;
    void ( *add_step )( void* user ) = nullptr;
    void ( *decision )( void* user, float dt ) = nullptr;
};

// With boat +0x32F clear: the clock gains dt ((dt + clock)); while the next frame exists and the clock is past it (comiss,
// jbe), the index moves on and that frame's flags are played: bit 1: +0x310 16.0 or 0; bit 2: +0x32C = +0x304; bit 4:
// +0x2FC 1.0 or 0; bit 8 on: +0x464 raised (+0x460 = 3.0 only on the raising) and +0x46B 0, off: +0x464 0, +0x460 0; bit
// 0x10: when the debris (+0x238) +0x94 is clear the launch, the event, +0x46B = 1; +0x46C = 0 either way.
// With a frame still ahead, the pose is the slerp between the two frames (position by (1 - t) a + b t, t = (clock - a.t) /
// (b.t - a.t), the euler by 0x531850), the velocity (b - a) / (b.t - a.t); a boost starting in this interval (bit 8 clear on
// a, set on b) snaps to b, velocity 0. The matrix identity x fromEuler with row 3 the position goes to 0x5344A0 and, as a
// btTransform (0x410C30) by 0x422FE0, to the body's +0x10 .. +0x4F; the velocity to the body's +0x140 (w 0); the matrix to
// the solver. Angular velocity is not touched.
// No frame ahead, or +0x32F set: with +0x2A0 clear the body's mask +0x2CC = 1 (and the broadphase proxy's +6, when there is
// one: `proxy`), into the world, +0x2A0 = 1. Then, always, 0x41EFF0 ( dt ).
inline void ghost_boat_tick( std::uint8_t* boat, std::uint8_t* debris, const ghost_data_t& d, vu_rigid_body_t& body, std::uint8_t* proxy, float dt,
                             const ghost_replay_hooks_t& h )
{
    const auto frame = [ & ]( std::int32_t k ) { return d.frames.data( ) + 24 * static_cast< std::ptrdiff_t >( k ); };
    bool ahead = false;
    if ( boat[ 0x32F ] == 0 )
    {
        state_put_f( boat, 0x524, sse_add( dt, state_f( boat, 0x524 ) ) );
        const auto index = [ & ]( ) {
            std::int32_t i;
            std::memcpy( &i, boat + 0x528, 4 );
            return i;
        };
        while ( index( ) + 1 < d.count )
        {
            const std::int32_t next = index( ) + 1;
            if ( !_mm_comigt_ss( _mm_set_ss( state_f( boat, 0x524 ) ), _mm_set_ss( state_f( frame( next ), 0 ) ) ) )
                break;
            std::memcpy( boat + 0x528, &next, 4 );
            std::uint16_t flags;
            std::memcpy( &flags, frame( next ) + 0x16, 2 );
            state_put_f( boat, 0x310, ( flags & 1u ) ? 16.0f : 0.0f );
            const std::uint8_t b2 = static_cast< std::uint8_t >( ( flags >> 1 ) & 1u );
            boat[ 0x32C ] = b2;
            boat[ 0x304 ] = b2;
            state_put_f( boat, 0x2FC, ( flags & 4u ) ? 1.0f : 0.0f );
            if ( flags & 8u )
            {
                if ( boat[ 0x464 ] == 0 )
                {
                    boat[ 0x464 ] = 1;
                    state_put_f( boat, 0x460, 3.0f );
                }
                boat[ 0x46B ] = 0;
            }
            else
            {
                boat[ 0x464 ] = 0;
                state_put_f( boat, 0x460, 0.0f );
            }
            if ( flags & 0x10u )
            {
                if ( debris[ 0x94 ] == 0 )
                {
                    if ( h.debris_launch )
                        h.debris_launch( h.user );
                    if ( h.destroyed_event )
                        h.destroyed_event( h.user );
                    boat[ 0x46B ] = 1;
                }
                state_put_f( boat, 0x46C, 0.0f );
            }
        }
        ahead = index( ) + 1 < d.count;
    }
    if ( ahead )
    {
        std::int32_t i;
        std::memcpy( &i, boat + 0x528, 4 );
        const std::uint8_t* a = frame( i );
        const std::uint8_t* b = frame( i + 1 );
        const float span = sse_sub( state_f( b, 0 ), state_f( a, 0 ) );
        const float t = ghost_div( sse_sub( state_f( boat, 0x524 ), state_f( a, 0 ) ), span );
        const float ax = state_f( a, 4 ), ay = state_f( a, 8 ), az = state_f( a, 0xC );
        const float bx = state_f( b, 4 ), by = state_f( b, 8 ), bz = state_f( b, 0xC );
        float ea[ 3 ], eb[ 3 ], e[ 4 ];
        ghost_frame_euler( a, ea );
        ghost_frame_euler( b, eb );
        const float u = sse_sub( 1.0f, t );
        float px = sse_add( sse_mul( u, ax ), sse_mul( bx, t ) );
        float py = sse_add( sse_mul( u, ay ), sse_mul( by, t ) );
        float pz = sse_add( sse_mul( u, az ), sse_mul( bz, t ) );
        euler_slerp( e, ea, eb, t );
        const float inv = ghost_div( 1.0f, span );
        float vx = sse_mul( inv, sse_sub( bx, ax ) ), vy = sse_mul( inv, sse_sub( by, ay ) ), vz = sse_mul( inv, sse_sub( bz, az ) );
        if ( ( a[ 0x16 ] & 8u ) == 0u && ( b[ 0x16 ] & 8u ) != 0u )
        {
            px = bx;
            py = by;
            pz = bz;
            std::memcpy( e, eb, 12 );
            vx = vy = vz = 0.0f;
        }
        float id[ 16 ], r[ 16 ], m[ 16 ];
        vu_identity( id );
        vu_matrix_from_euler( r, e );
        hull_multiply( id, m, r );
        m[ 12 ] = px;
        m[ 13 ] = py;
        m[ 14 ] = pz;
        if ( h.set_world )
            h.set_world( h.user, m );
        solver_rigid_t& rb = body.body;
        rb.linear_velocity[ 0 ] = vx;
        rb.linear_velocity[ 1 ] = vy;
        rb.linear_velocity[ 2 ] = vz;
        rb.linear_velocity[ 3 ] = 0.0f;
        for ( int row = 0; row < 3; ++row )   // 0x410C30 then 0x422FE0
        {
            for ( int c = 0; c < 3; ++c )
                rb.basis[ 4 * row + c ] = m[ row + 4 * c ];
            rb.basis[ 4 * row + 3 ] = 0.0f;
        }
        rb.origin[ 0 ] = m[ 12 ];
        rb.origin[ 1 ] = m[ 13 ];
        rb.origin[ 2 ] = m[ 14 ];
        rb.origin[ 3 ] = 0.0f;
        if ( h.solver_prepare )
            h.solver_prepare( h.user, m );
    }
    else if ( boat[ 0x2A0 ] == 0 )
    {
        const std::uint16_t one = 1;
        std::memcpy( reinterpret_cast< std::uint8_t* >( &body ) + 0x2CC, &one, 2 );
        if ( proxy )
            std::memcpy( proxy + 6, &one, 2 );
        if ( h.add_body )
            h.add_body( h.user );
        if ( h.add_step )
            h.add_step( h.user );
        boat[ 0x2A0 ] = 1;
    }
    if ( h.decision )
        h.decision( h.user, dt );
}

// ---- Slot 12 0x422590 (thiscall the boat; the draw params; retn 4), the ghost's draw --------------------------------
//
// With the boat manager (g 0x758FB8) +0x28 nonzero, its first boat's (+0x24 [0]) position `first`: +0x2C8 = 0 at or under
// +0x590 from it, 1.0 at or past +0x594, else (d - near) / (far - near); then 0x41EAE0 (VuBoatEntity's draw, which reads
// +0x2C8), then, when the params' +0x18 byte is clear, the driver's name over the boat.
inline void ghost_boat_fade( std::uint8_t* boat, const std::uint8_t* transform, const std::uint8_t* first_transform )
{
    if ( first_transform == nullptr )
        return;
    const float dx = sse_sub( state_f( first_transform, 0x80 ), state_f( transform, 0x80 ) );
    const float dy = sse_sub( state_f( first_transform, 0x84 ), state_f( transform, 0x84 ) );
    const float dz = sse_sub( state_f( first_transform, 0x88 ), state_f( transform, 0x88 ) );
    const float d = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( sse_add( sse_add( sse_mul( dx, dx ), sse_mul( dy, dy ) ), sse_mul( dz, dz ) ) ) ) );
    const float lo = state_f( boat, 0x590 ), hi = state_f( boat, 0x594 );
    float v;
    if ( _mm_comige_ss( _mm_set_ss( lo ), _mm_set_ss( d ) ) )   // comiss near, d; jb: CF also set when unordered
        v = 0.0f;
    else if ( _mm_comige_ss( _mm_set_ss( d ), _mm_set_ss( hi ) ) )
        v = 1.0f;
    else
        v = ghost_div( sse_sub( d, lo ), sse_sub( hi, lo ) );
    state_put_f( boat, 0x2C8, v );
}

// The label: P = the transform's world x translation( boat +0x49C ), the point (0, 0, 2) through it (retail's factored
// form: ((m0 + m4) 0 + m8 2) + m12); through the camera's (`cam`, the params' +0x10) matrices at +0x70 and +0xB8. Drawn only
// when (-w - cam +0x178) / (cam +0x17C - cam +0x178) > 0 and P is under 200 from the eye (+0). Out: the screen point
// ( 0.5 - cam +0x30 x / w 0.5, (cam +0x44 y / w + 1) 0.5 ), the depth, and the font's 60 params (+0x54C) with the alpha
// (1 - 0.005 dist) +0x2C8 255 (fistp, its low byte) in bytes 0xF and 0x1B.
//
// What the caller does with it: 0x536C10 (g 0x75902C's +0x14; xmm1 the depth)( the font's 0x626B80, the driver's name
// (+0x220's +0x60), the params, &(x, y, 0, 0), 0x66, 1.0, &(0, 0) ) - the text draw, the UI lane's (ui/board.hxx).
struct ghost_label_t
{
    float x = 0.0f, y = 0.0f, depth = 0.0f;
    std::uint8_t params[ 0x3C ]{};
};

inline bool ghost_boat_label( const std::uint8_t* boat, const std::uint8_t* transform, const std::uint8_t* cam, ghost_label_t& out )
{
    float t[ 16 ], w[ 16 ], m[ 16 ];
    std::memcpy( w, transform + 0x50, 64 );
    hull_translation( t, reinterpret_cast< const float* >( boat + 0x49C ) );
    hull_multiply( w, m, t );
    const float x = sse_add( sse_add( sse_mul( sse_add( m[ 0 ], m[ 4 ] ), 0.0f ), sse_mul( m[ 8 ], 2.0f ) ), m[ 12 ] );
    const float y = sse_add( sse_add( sse_mul( sse_add( m[ 1 ], m[ 5 ] ), 0.0f ), sse_mul( m[ 9 ], 2.0f ) ), m[ 13 ] );
    const float z = sse_add( sse_add( sse_mul( sse_add( m[ 2 ], m[ 6 ] ), 0.0f ), sse_mul( m[ 10 ], 2.0f ) ), m[ 14 ] );
    const auto c = [ & ]( int at ) { return state_f( cam, at ); };
    const auto through = [ & ]( int a, int b, int e, int o ) {   // ((cam a y + cam b x) + cam e z) + cam o
        return sse_add( sse_add( sse_add( sse_mul( c( a ), y ), sse_mul( c( b ), x ) ), sse_mul( c( e ), z ) ), c( o ) );
    };
    const float cx = through( 0x80, 0x70, 0x90, 0xA0 );
    const float cy = through( 0x84, 0x74, 0x94, 0xA4 );
    const float cw = through( 0x88, 0x78, 0x98, 0xA8 );
    const float inv = ghost_div( 1.0f, cw );
    const float sx = sse_sub( 0.5f, sse_mul( sse_mul( sse_mul( c( 0x30 ), cx ), inv ), 0.5f ) );
    const float sy = sse_mul( sse_add( sse_mul( sse_mul( c( 0x44 ), cy ), inv ), 1.0f ), 0.5f );
    const float near_ = c( 0x178 );
    std::uint32_t neg;
    std::memcpy( &neg, &cw, 4 );
    const float along = ghost_div( sse_sub( water_bits( neg ^ 0x80000000u ), near_ ), sse_sub( c( 0x17C ), near_ ) );
    if ( !_mm_comigt_ss( _mm_set_ss( along ), _mm_setzero_ps( ) ) )
        return false;
    const float ez = sse_sub( z, c( 8 ) ), ex = sse_sub( x, c( 0 ) ), ey = sse_sub( y, c( 4 ) );
    const float dist = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( sse_add( sse_add( sse_mul( ey, ey ), sse_mul( ex, ex ) ), sse_mul( ez, ez ) ) ) ) );
    if ( !_mm_comigt_ss( _mm_set_ss( 200.0f ), _mm_set_ss( dist ) ) )
        return false;
    const float dw = through( 0xCC, 0xBC, 0xDC, 0xEC );
    const float dz = through( 0xC8, 0xB8, 0xD8, 0xE8 );
    const float alpha = sse_mul( sse_mul( sse_sub( 1.0f, sse_mul( dist, water_bits( 0x3BA3D70Au ) ) ), state_f( boat, 0x2C8 ) ), 255.0f );
    out.x = sx;
    out.y = sy;
    out.depth = sse_mul( ghost_div( 1.0f, dw ), dz );
    std::memcpy( out.params, boat + 0x54C, 0x3C );
    const std::uint8_t al = static_cast< std::uint8_t >( _mm_cvtss_si32( _mm_set_ss( alpha ) ) );
    out.params[ 0xF ] = al;
    out.params[ 0x1B ] = al;
    return true;
}

// ---- The ctor's tail, 0x422310 after VuBoatEntity's ctor 0x41B850 -------------------------------------------------
//
// +0x520 .. +0x528 = 0 (0x4AF980 already zeroed +0x520 / +0x52C / +0x588 / +0x58C before the ctors), the font 0x626900 +
// 0x626B00( "HUD_PlayerName" ) (the UI lane's: the caller's); +0x590 / +0x594 = ConstantDB ["VuDBAsset"]["Boat"]
// ["GhostFadeDist"] elements 0 / 1: a list long enough, else the null container; int (cvtdq2ps), float, int64 (fild,
// fstp dword) or else 0; the hull's (+0x228) +0x1EC byte = 1.
inline float ghost_fade_value( const value_t& list, std::size_t at )
{
    if ( list.tag != 5 || list.list.size( ) <= at )
        return 0.0f;
    const value_t& v = list.list[ at ];
    if ( v.tag == 1 )
        return static_cast< float >( v.integer );
    if ( v.tag == 2 )
        return v.real;
    if ( v.tag == 7 )
        return static_cast< float >( v.int64 );
    return 0.0f;
}

inline void ghost_boat_construct( std::uint8_t* boat, std::uint8_t* hull, const value_t& constant_db )
{
    std::memset( boat + 0x520, 0, 12 );
    const value_t& list = member( member( member( constant_db, "VuDBAsset" ), "Boat" ), "GhostFadeDist" );
    state_put_f( boat, 0x590, ghost_fade_value( list, 0 ) );
    state_put_f( boat, 0x594, ghost_fade_value( list, 1 ) );
    hull[ 0x1EC ] = 1;
}

// ---- VuGhostManager 0x4AF640 (thiscall the manager; index, the blob buffer; retn 8), the ghost's boat ---------------
//
// The record (0x5C, ctor 0x4AF1C0, dtor 0x4AF220): +0 the index, +4 the data, +0x58 the boat. Past 8 ghosts (+0x64's size,
// unsigned) false, nothing made. The blob refused (0x4AFCE0): the record destroyed, false. Else the boat: new(0x5A0), 0x4AF980,
// the ctor 0x422310; the document { Properties { Boat, Skin, "Driver Type" "Ghost" }, Driver { Name } }; the entity's name
// "GhostBoat%d" (sprintf into 64 bytes); vt+0x2C load (the document), 0x500A60( 0x811C9DC5 ); the first frame's position
// straight into the transform's +0x80 .. with 0x534830, 0x534C00(1) and the +0xC0 watcher, then 0x5345C0( its euler );
// +0x520 = the record's data; the record pushed onto +0x64 (0x440990). True.
struct ghost_record_t
{
    std::int32_t index = 0;
    ghost_data_t data;
    void* boat = nullptr;
};

struct ghost_add_hooks_t
{
    // new(0x5A0) .. the name .. vt+0x2C .. 0x500A60: the entity world's (VuEntity::load, world/race.hxx's shape).
    std::function< void*( const std::string& name, const value_t& document ) > make_boat;
    // The transform's position stores + 0x534830 + 0x534C00(1) + the watcher, then 0x5345C0.
    std::function< void( void* boat, const float* position, const float* euler ) > place;
    // boat +0x520 = the entry's data.
    std::function< void( void* boat, const ghost_data_t& data ) > bind;
};

inline value_t ghost_document( const ghost_data_t& d )
{
    value_t doc;
    value_t& props = json_member( doc, "Properties" );
    json_set_string( json_member( props, "Boat" ), d.boat );
    json_set_string( json_member( props, "Skin" ), d.skin );
    json_set_string( json_member( props, "Driver Type" ), "Ghost" );
    json_set_string( json_member( json_member( doc, "Driver" ), "Name" ), d.driver );
    return doc;
}

inline bool ghost_add( std::vector< std::unique_ptr< ghost_record_t > >& ghosts, std::int32_t index, const std::uint8_t* blob, std::size_t size,
                       const ghost_add_hooks_t& h )
{
    if ( ghosts.size( ) >= 8u )
        return false;
    std::unique_ptr< ghost_record_t > e = std::make_unique< ghost_record_t >( );
    e->index = index;
    if ( !ghost_read( e->data, blob, size ) )
        return false;
    char name[ 64 ];
    std::snprintf( name, sizeof( name ), "GhostBoat%d", index );
    e->boat = h.make_boat( name, ghost_document( e->data ) );
    float euler[ 3 ];
    const std::uint8_t* first = e->data.frames.data( );
    ghost_frame_euler( first, euler );
    h.place( e->boat, reinterpret_cast< const float* >( first + 4 ), euler );
    h.bind( e->boat, e->data );
    ghosts.push_back( std::move( e ) );
    return true;
}
