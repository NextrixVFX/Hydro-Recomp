#pragma once

#include <workspace/core/boat/events.hxx>
#include <workspace/core/boat/fluid.hxx>
#include <workspace/core/boat/hull.hxx>
#include <workspace/core/boat/math.hxx>
#include <workspace/core/boat/state.hxx>
#include <workspace/core/assets/scene.hxx>
#include <workspace/core/world/pfxmgr.hxx>
#include <workspace/core/world/pfxsim.hxx>
#include <workspace/core/boat/camera.hxx>
#include <workspace/core/boat/fleet.hxx>
#include <workspace/core/world/surface.hxx>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <xmmintrin.h>

// VuBoatEntity's pfx controller (the object at boat +0x234; its +0x30 is the
// boat) and what the Build tick 0x41F330's step 5 runs on it: 0x419700 (dt).
//
// The controller keeps seven lists (std::vector: begin / end / capacity) of
// 0x60-byte records - +0x18 a VuPfxSystemInstance, +0x1C a bone index of the
// boat's model instance (< 0: none), +0x20 the record's local matrix - at
// +0x38 +0x44 +0x50 +0x5C +0x68 +0x74 +0x80 (list 0 .. 6 here: named by
// offset, the ctor that fills them is not read), the five "on" bytes +0x8C ..
// +0x90 (lists 2, 3, 4, 5, 6), one more system at +0x94 (0 = none) and an aabb
// +0x98 .. +0xB7 (min x y z w, max x y z w).
//
// Every call onto a system is a Vu virtual of VuPfxSystemInstance (vtable
// 0x74FF8C): slot 0 setPosition (+0), 2 setMatrix (+8), 3 the four words at
// +0x5C (+0xC: the velocity), 5 setColor (+0x14), 7 the aabb (+0x1C), 11 start
// (+0x2C), 12 stop(immediate) (+0x30), 13 tick(dt) (+0x34) - boat_pfx_ops_t.
// The water agent's recreated runtime implements them (world/pfxclass.hxx /
// pfxsim.hxx): boat_pfx_runtime_ops binds them.

struct boat_pfx_record_t
{
    std::string name;           // +0 (the effect's name: what the fill creates it by)
    std::uint32_t system = 0;   // +0x18
    std::int32_t bone = -1;     // +0x1C
    float local[ 16 ]{};        // +0x20
};

struct boat_pfx_t
{
    std::string fluids_asset;                     // +0x00 (HullEffect's "Fluids Mesh Asset")
    std::string hull_effect;                      // +0x18 (HullEffect's "Effect" / "CameraTargetEffect")
    std::uint32_t boat = 0;                       // +0x30 (the boat's address, written into bound processes)
    std::uint32_t fluids = 0;                     // +0x34 (the VuFluidsObject's address)
    std::vector< boat_pfx_record_t > list[ 7 ];   // +0x38 +0x44 +0x50 +0x5C +0x68 +0x74 +0x80
    std::uint8_t on[ 5 ]{};                       // +0x8C .. +0x90 (lists 2 .. 6)
    std::uint32_t extra = 0;                      // +0x94
    std::uint32_t aabb[ 8 ]{};                    // +0x98 .. +0xB7
    bool aabb_w_stale = false;   // a merge (0x41A790) left +0xA4 / +0xB4 = that function's stale stack: UNKNOWN
};

// What 0x419700 reads through the controller's +0x30 (the boat). `throttle`
// is the driver's (boat +0x220) vt+0x64 answer - retail's FIRST call, made
// before anything is written; `d570` is 0x41D570's answer (boat/tick.hxx
// boat_41D570 - it reads none of what this function or its callees write
// before it runs, so the caller may compute it first).
struct boat_pfx_boat_t
{
    float throttle = 0.0f;
    float d570 = 0.0f;
    const std::uint32_t* velocity = nullptr;   // body (+0x21C) +0x140, four words
    std::uint8_t b32c = 0;                     // boat +0x32C
    std::uint32_t f400 = 0, f404 = 0;          // boat +0x400 / +0x404 (words)
    float f2b4 = 0.0f;                         // boat +0x2B4 (the water colour blend, Build step 2)
    const std::uint8_t* colour = nullptr;      // boat +0x454 .. +0x45B
    const float* offset = nullptr;             // boat +0x49C (3)
    const float* world = nullptr;              // the transform (+0x50) +0x50: 16 floats (+0x80 = row 3)
    const float* bones = nullptr;              // the model instance (+0x2A4) +0x18: 16 floats each
    int bone_count = 0;
};

struct boat_pfx_ops_t
{
    void* user = nullptr;
    void ( *set_position )( void* user, std::uint32_t system, const float* p ) = nullptr;   // slot 0 (reads 3)
    void ( *set_matrix )( void* user, std::uint32_t system, const float* m16 ) = nullptr;   // 2
    void ( *set_velocity )( void* user, std::uint32_t system, const std::uint32_t* v4 ) = nullptr;   // 3
    void ( *set_color )( void* user, std::uint32_t system, const float* rgba ) = nullptr;   // 5
    const std::uint32_t* ( *aabb )( void* user, std::uint32_t system ) = nullptr;         // 7 (8 words)
    void ( *start )( void* user, std::uint32_t system ) = nullptr;                          // 11
    void ( *stop )( void* user, std::uint32_t system, bool immediate ) = nullptr;           // 12
    void ( *tick )( void* user, std::uint32_t system, float dt ) = nullptr;                 // 13
    float ( *duration )( void* user, std::uint32_t system ) = nullptr;                      // 10 (the splash discards it)
    // 0x41E2E0 (ecx the boat, xmm1 a, xmm2 b) = boat/state.hxx boat_driver_raise( driver, human, a, b ).
    void ( *raise )( void* user, float a, float b ) = nullptr;
};

// 0x41ABE0 (stdcall list): every record's system started (slot 11).
inline void boat_pfx_start_list( const std::vector< boat_pfx_record_t >& l, const boat_pfx_ops_t& o )
{
    for ( const boat_pfx_record_t& r : l )
        o.start( o.user, r.system );
}

// 0x41AC50 (stdcall list, immediate): every record's system stopped (slot 12).
inline void boat_pfx_stop_list( const std::vector< boat_pfx_record_t >& l, bool immediate, const boat_pfx_ops_t& o )
{
    for ( const boat_pfx_record_t& r : l )
        o.stop( o.user, r.system, immediate );
}

// 0x41AB70 (stdcall list, rgba*): every record's slot 5 with the same four floats.
inline void boat_pfx_color_list( const std::vector< boat_pfx_record_t >& l, const float* rgba, const boat_pfx_ops_t& o )
{
    for ( const boat_pfx_record_t& r : l )
        o.set_color( o.user, r.system, rgba );
}

// 0x41A790 (thiscall controller; list; xmm2 dt; retn 4): per record M =
// local (+0x20) [. bone (+0x1C) when >= 0] . the transform's world (+0x50) .
// translation( boat +0x49C ) (VuMatrix_multiply 0x4078C0, the translation's
// row 3 = (x, y, z, 1) written in place), slot 2 (M), slot 13 (dt); then slot
// 7, and unless its min x == FLT_MAX (ucomiss / jnp: NaN merges) slot 7 again
// and the controller's aabb takes minss( own, its ) on +0x98 .. +0xA0 and
// maxss on +0xA8 .. +0xB0. The merge's movq stores carry a stack word nothing
// in the function writes into +0xA4 / +0xB4 (UNKNOWN: aabb_w_stale).
inline void boat_pfx_place_list( boat_pfx_t& c, const std::vector< boat_pfx_record_t >& l, const boat_pfx_boat_t& b, float dt,
                                 const boat_pfx_ops_t& o )
{
    float m[ 16 ], t[ 16 ], x[ 16 ];
    for ( const boat_pfx_record_t& r : l )
    {
        std::memcpy( m, r.local, 64 );
        if ( r.bone >= 0 )
        {
            hull_multiply( m, x, b.bones + 16 * r.bone );
            std::memcpy( m, x, 64 );
        }
        hull_multiply( m, x, b.world );
        std::memcpy( m, x, 64 );
        hull_translation( t, b.offset );
        hull_multiply( m, x, t );
        std::memcpy( m, x, 64 );
        o.set_matrix( o.user, r.system, m );
        o.tick( o.user, r.system, dt );
        float lo;
        std::memcpy( &lo, o.aabb( o.user, r.system ), 4 );
        if ( lo == water_bits( 0x7F7FFFFFu ) )   // ordered equal only (jnp)
            continue;
        const std::uint32_t* a = o.aabb( o.user, r.system );
        float own[ 8 ], its[ 8 ];
        std::memcpy( own, c.aabb, 32 );
        std::memcpy( its, a, 32 );
        for ( int k = 0; k < 3; ++k )
        {
            own[ k ] = fluid_min( own[ k ], its[ k ] );
            own[ 4 + k ] = fluid_max( own[ 4 + k ], its[ 4 + k ] );
        }
        std::memcpy( c.aabb, own, 12 );
        std::memcpy( c.aabb + 4, own + 4, 12 );
        c.aabb_w_stale = true;
    }
}

// 0x419700 (__usercall: ecx the controller, xmm1 dt), in retail's order:
//   - the throttle t (driver vt+0x64, fstp). List 2 (+0x50, on +0x8C): off and
//     t > 0.75 -> on, started, 0x41E2E0( 0.75, 0.75 ); on and 0.25 > t -> off,
//     stopped (not immediate). Every compare is a comiss branch (NaN: neither).
//   - List 3 (+0x5C, +0x8D) on s = (vx vx + vy vy) + vz vz of the body's
//     +0x140: off, t > 0.75 and 17.8816^2 > s (dword 0x736284, its .data
//     value: nothing writes it) -> on, started; on and (0.25 > t or s >
//     35.7632^2 (0x736288)) -> off, stopped.
//   - List 4 (+0x68, +0x8E) follows the byte boat +0x32C: set -> started,
//     0x41E2E0( 1.0, 0.0 ); clear -> stopped IMMEDIATELY.
//   - Lists 5 / 6 (+0x74 / +0x80, +0x8F / +0x90) on boat +0x400 / +0x404: off
//     and > 0.1 -> on, started; on and 0 >= it -> off, stopped.
//   - 0x41AAD0 (thiscall; list +0x44; one ignored stack word; retn 8): each
//     list-1 record's slot 3 with the body's +0x140 .. +0x14F.
//   - The colour (r, g, b) lane k = (1 - v) x (byte(+0x454 + k) x c) +
//     (byte(+0x458 + k) x c) x v, v = boat +0x2B4, c = 0x3B808081 (movzx ->
//     cvtdq2ps -> mulss); 0x41AB70 with (1, 1, 1, maxss( 0x41D570, 0 )) on
//     list 0, (r, g, b, 1) on lists 1 and 3, (1, 1, 1, +0x400) on 5,
//     (1, 1, 1, +0x404) on 6.
//   - The aabb reset: +0x98 .. +0xA0 FLT_MAX, +0xA8 .. +0xB0 -FLT_MAX; +0xA4 and
//     +0xB4 take the stack word last written - the +0x404 the list-6 colour
//     carried.
//   - 0x41A790 on lists 2, 3, 4 (only while +0x8E), 0, 1, 5, 6.
//   - With +0x94: its slot 0 (the transform's +0x80), slot 5 (r, g, b, 1),
//     slot 13 (dt).
// False - nothing done - when a record's bone is outside the model's bones
// (retail would read past the palette: UNKNOWN).
inline bool boat_pfx_tick( boat_pfx_t& c, const boat_pfx_boat_t& b, float dt, const boat_pfx_ops_t& o )
{
    for ( const auto& l : c.list )
        for ( const boat_pfx_record_t& r : l )
            if ( r.bone >= 0 && ( b.bones == nullptr || r.bone >= b.bone_count ) )
                return false;
    // comiss branches: ordered compares (NaN false) - written as plain operators (see boat_debris_tick)
    auto gt = []( float a, float x ) { return a > x; };
    auto ge = []( float a, float x ) { return a >= x; };
    const float hi = water_bits( 0x3F400000u ), lo = water_bits( 0x3E800000u ), tenth = water_bits( 0x3DCCCCCDu );
    const float t = b.throttle;
    if ( c.on[ 0 ] == 0 )
    {
        if ( gt( t, hi ) )
        {
            c.on[ 0 ] = 1;
            boat_pfx_start_list( c.list[ 2 ], o );
            if ( o.raise )
                o.raise( o.user, hi, hi );
        }
    }
    else if ( gt( lo, t ) )
    {
        c.on[ 0 ] = 0;
        boat_pfx_stop_list( c.list[ 2 ], false, o );
    }
    float v[ 4 ];
    std::memcpy( v, b.velocity, 16 );
    auto speed2 = [ & ]( ) { return sse_add( sse_add( sse_mul( v[ 0 ], v[ 0 ] ), sse_mul( v[ 1 ], v[ 1 ] ) ), sse_mul( v[ 2 ], v[ 2 ] ) ); };
    if ( c.on[ 1 ] == 0 )
    {
        if ( gt( t, hi ) )
        {
            const float k = water_bits( 0x418F0D84u );
            if ( gt( k * k, speed2( ) ) )
            {
                c.on[ 1 ] = 1;
                boat_pfx_start_list( c.list[ 3 ], o );
            }
        }
    }
    else
    {
        const float k = water_bits( 0x420F0D84u );
        if ( gt( lo, t ) || gt( speed2( ), k * k ) )
        {
            c.on[ 1 ] = 0;
            boat_pfx_stop_list( c.list[ 3 ], false, o );
        }
    }
    if ( c.on[ 2 ] == 0 )
    {
        if ( b.b32c )
        {
            c.on[ 2 ] = 1;
            boat_pfx_start_list( c.list[ 4 ], o );
            if ( o.raise )
                o.raise( o.user, 1.0f, 0.0f );
        }
    }
    else if ( b.b32c == 0 )
    {
        c.on[ 2 ] = 0;
        boat_pfx_stop_list( c.list[ 4 ], true, o );
    }
    const std::uint32_t words[ 2 ] = { b.f400, b.f404 };
    for ( int k = 0; k < 2; ++k )
    {
        const float f = water_bits( words[ k ] );
        if ( c.on[ 3 + k ] == 0 )
        {
            if ( gt( f, tenth ) )
            {
                c.on[ 3 + k ] = 1;
                boat_pfx_start_list( c.list[ 5 + k ], o );
            }
        }
        else if ( ge( 0.0f, f ) )
        {
            c.on[ 3 + k ] = 0;
            boat_pfx_stop_list( c.list[ 5 + k ], false, o );
        }
    }
    for ( const boat_pfx_record_t& r : c.list[ 1 ] )
        o.set_velocity( o.user, r.system, b.velocity );
    const float cc = water_bits( 0x3B808081u ), u = sse_sub( 1.0f, b.f2b4 );
    float rgb[ 3 ];
    for ( int k = 0; k < 3; ++k )
    {
        const float a = sse_mul( static_cast< float >( static_cast< std::int32_t >( b.colour[ k ] ) ), cc );
        const float z = sse_mul( sse_mul( static_cast< float >( static_cast< std::int32_t >( b.colour[ 4 + k ] ) ), cc ), b.f2b4 );
        rgb[ k ] = sse_add( sse_mul( u, a ), z );
    }
    const float d = fluid_max( b.d570, 0.0f );
    const float white_d[ 4 ] = { 1.0f, 1.0f, 1.0f, d };
    const float tint[ 4 ] = { rgb[ 0 ], rgb[ 1 ], rgb[ 2 ], 1.0f };
    float white_400[ 4 ] = { 1.0f, 1.0f, 1.0f, 0.0f }, white_404[ 4 ] = { 1.0f, 1.0f, 1.0f, 0.0f };
    std::memcpy( white_400 + 3, &b.f400, 4 );
    std::memcpy( white_404 + 3, &b.f404, 4 );
    boat_pfx_color_list( c.list[ 0 ], white_d, o );
    boat_pfx_color_list( c.list[ 1 ], tint, o );
    boat_pfx_color_list( c.list[ 3 ], tint, o );
    boat_pfx_color_list( c.list[ 5 ], white_400, o );
    boat_pfx_color_list( c.list[ 6 ], white_404, o );
    const std::uint32_t reset[ 8 ] = { 0x7F7FFFFFu, 0x7F7FFFFFu, 0x7F7FFFFFu, b.f404, 0xFF7FFFFFu, 0xFF7FFFFFu, 0xFF7FFFFFu, b.f404 };
    std::memcpy( c.aabb, reset, 32 );
    c.aabb_w_stale = false;
    boat_pfx_place_list( c, c.list[ 2 ], b, dt, o );
    boat_pfx_place_list( c, c.list[ 3 ], b, dt, o );
    if ( c.on[ 2 ] )
        boat_pfx_place_list( c, c.list[ 4 ], b, dt, o );
    boat_pfx_place_list( c, c.list[ 0 ], b, dt, o );
    boat_pfx_place_list( c, c.list[ 1 ], b, dt, o );
    boat_pfx_place_list( c, c.list[ 5 ], b, dt, o );
    boat_pfx_place_list( c, c.list[ 6 ], b, dt, o );
    if ( c.extra != 0 )
    {
        o.set_position( o.user, c.extra, b.world + 12 );
        o.set_color( o.user, c.extra, tint );
        o.tick( o.user, c.extra, dt );
    }
    return true;
}

// The controller's DRAW, called by VuBoatEntity's draw 0x41EAE0 with its
// VuGfxDrawParams. A system is drawn through VuPfxSystemInstance slot 14
// (+0x38, the params pointer) - a render hook here: `draw( user, system,
// params )`, the params passed as the address retail hands on.
//
// 0x41ACC0 (stdcall list, params; retn 8): every record's system drawn. Retail
// recomputes the count ((end - begin) / 0x60) after each call; the draw does
// not touch the controller, so a plain walk visits the same records.
inline void boat_pfx_draw_list( const std::vector< boat_pfx_record_t >& l, std::uint32_t params,
                                void ( *draw )( void* user, std::uint32_t system, std::uint32_t params ), void* user )
{
    for ( const boat_pfx_record_t& r : l )
        draw( user, r.system, params );
}

// 0x419CE0 (thiscall controller; params; retn 4): nothing when the params'
// byte +0x18 is set. Else list 4 (+0x68) when +0x8E (on[ 2 ]); then, unless
// the boat's (+0x30) byte +0x46B is set, lists 2, 3, 0, 1, 5, 6 (+0x50 +0x5C
// +0x38 +0x44 +0x74 +0x80) - whatever their "on" bytes; then +0x94's slot 14
// when it is non-zero (whatever +0x46B).
inline void boat_pfx_draw( const boat_pfx_t& c, std::uint8_t params_18, std::uint8_t boat_46b, std::uint32_t params,
                           void ( *draw )( void* user, std::uint32_t system, std::uint32_t params ), void* user )
{
    if ( params_18 != 0 )
        return;
    if ( c.on[ 2 ] != 0 )
        boat_pfx_draw_list( c.list[ 4 ], params, draw, user );
    if ( boat_46b == 0 )
    {
        static const int order[ 6 ] = { 2, 3, 0, 1, 5, 6 };
        for ( int l : order )
            boat_pfx_draw_list( c.list[ l ], params, draw, user );
    }
    if ( c.extra != 0 )
        draw( user, c.extra, params );
}

// The binding onto the recreated VuPfx runtime: the water agent's instance
// exports (world/pfxsim.hxx pfx_instance_*, verified by its case pfx_calls; a
// system is its heap word) - slot 0 as world/pfxclass.hxx's setter (no
// instance export), 2 / 3 / 5 / 7 / 10 / 11 / 12 / 13 as pfx_instance_set_matrix /
// _set_velocity / _set_colour / _aabb / _duration / _start / _stop / _tick (with
// rt.hooks). `raise` stays the caller's (boat_driver_raise on the boat's
// driver). A piece's pfx release (boat_piece_hooks_t::pfx_release, 0x41A0E0 /
// g_vupfximpl vt+0x2C) is pfx_system_release( rt.heap, rt.resources, s ).
inline boat_pfx_ops_t boat_pfx_runtime_ops( pfx_runtime_t& rt )
{
    boat_pfx_ops_t o;
    o.user = &rt;
    o.set_position = []( void* u, std::uint32_t s, const float* p ) {
        std::uint32_t w[ 3 ];
        std::memcpy( w, p, sizeof( w ) );
        pfx_system_set_position( static_cast< pfx_runtime_t* >( u )->heap.find( s ), w );
    };
    o.set_matrix = []( void* u, std::uint32_t s, const float* m ) { pfx_instance_set_matrix( static_cast< pfx_runtime_t* >( u )->heap, s, m ); };
    o.set_velocity = []( void* u, std::uint32_t s, const std::uint32_t* v ) {
        float f[ 4 ];
        std::memcpy( f, v, 16 );
        pfx_instance_set_velocity( static_cast< pfx_runtime_t* >( u )->heap, s, f );
    };
    o.set_color = []( void* u, std::uint32_t s, const float* c ) { pfx_instance_set_colour( static_cast< pfx_runtime_t* >( u )->heap, s, c ); };
    o.aabb = []( void* u, std::uint32_t s ) -> const std::uint32_t* {
        return reinterpret_cast< const std::uint32_t* >( pfx_instance_aabb( static_cast< pfx_runtime_t* >( u )->heap, s ) );
    };
    o.duration = []( void* u, std::uint32_t s ) { return pfx_instance_duration( static_cast< pfx_runtime_t* >( u )->heap, s ); };
    o.start = []( void* u, std::uint32_t s ) { pfx_instance_start( static_cast< pfx_runtime_t* >( u )->heap, s ); };
    o.stop = []( void* u, std::uint32_t s, bool immediate ) {
        pfx_runtime_t& rt = *static_cast< pfx_runtime_t* >( u );
        pfx_instance_stop( rt.heap, rt.resources, s, immediate );
    };
    o.tick = []( void* u, std::uint32_t s, float dt ) {
        pfx_runtime_t& rt = *static_cast< pfx_runtime_t* >( u );
        pfx_instance_tick( rt.heap, s, dt, rt.hooks );
    };
    return o;
}

// ---------------------------------------------------------------------------
// The boat's debris object (boat +0x238; +0 the boat): its +4 sub-object's
// parts (0x51F1C0: +0x68 gravity, +0x6C count, +0x70 the 0xD8-byte parts),
// +0x94 "pieces out", +0x9C / +0xA0 the VuPiece pointers, the aabb +0xA8 ..
// +0xC7, +0xC8 the parts' timer (boat_decision_tick runs it down).

// One 0xD8-byte part (its image) and its model's local aabb (the model at
// part +0: its +0x70, 8 floats). Part +4 the base matrix, +0x44 the pivot,
// +0x54 the position, +0x64 the euler angles, +0x74 the velocity, +0x84 the
// angular velocity, +0x94 the world matrix it rebuilds, +0xD4 the damping.
struct boat_debris_part_t
{
    std::uint8_t image[ 0xD8 ]{};
    const float* model_aabb = nullptr;
    const float* model_matrix = nullptr;   // the model's +0x24 (16): 0x51E790's launch only
};

// 0x51F1C0 (thiscall the sub-object; aabb*; xmm1 dt; retn 4): with g = -(+0x68)
// (xorps), h = (dt x 0.5) x dt, h0 = h x 0, hz = h x g, c0 = dt x 0, cz = g x dt,
// per part (operand order as its mulss / addss):
//   k = minss( +0xD4 x dt, 1 );
//   +0x54 = +0x54 + (h0 + +0x74 dt), +0x58 = (h0 + +0x78 dt) + +0x58,
//   +0x5C = (hz + +0x7C dt) + +0x5C (the OLD velocity);
//   +0x74 = (c0 + +0x74)(1 - k), +0x78 = (c0 + +0x78)(1 - k), +0x7C = (+0x7C + cz)(1 - k);
//   +0x64.. += dt x +0x84 / +0x88 dt / +0x8C dt (dt x a for x, a x dt for y z);
//   +0x94 = identity . fromEuler( +0x64 ) (VuMatrix_fromEuler 0x435640), row 3 =
//   the position, then translation( -pivot ) . it, then +4 . it;
//   0x5175E0( aabb, model +0x70, +0x94 ).
inline void boat_debris_parts_tick( float gravity, std::vector< boat_debris_part_t >& parts, float* aabb, float dt )
{
    std::uint32_t gw;
    std::memcpy( &gw, &gravity, 4 );
    const float g = water_bits( gw ^ 0x80000000u );
    const float h = sse_mul( sse_mul( dt, 0.5f ), dt );
    const float c0 = sse_mul( dt, 0.0f ), h0 = sse_mul( h, 0.0f ), hz = sse_mul( h, g ), cz = sse_mul( g, dt );
    for ( boat_debris_part_t& p : parts )
    {
        std::uint8_t* e = p.image;
        const float k = fluid_min( sse_mul( state_f( e, 0xD4 ), dt ), 1.0f );
        const float vx = state_f( e, 0x74 ), vy = state_f( e, 0x78 ), vz = state_f( e, 0x7C );
        state_put_f( e, 0x54, sse_add( state_f( e, 0x54 ), sse_add( h0, sse_mul( vx, dt ) ) ) );
        state_put_f( e, 0x58, sse_add( sse_add( h0, sse_mul( vy, dt ) ), state_f( e, 0x58 ) ) );
        state_put_f( e, 0x5C, sse_add( sse_add( hz, sse_mul( vz, dt ) ), state_f( e, 0x5C ) ) );
        const float keep = sse_sub( 1.0f, k );
        state_put_f( e, 0x74, sse_mul( sse_add( c0, vx ), keep ) );
        state_put_f( e, 0x78, sse_mul( sse_add( c0, vy ), keep ) );
        state_put_f( e, 0x7C, sse_mul( sse_add( vz, cz ), keep ) );
        state_put_f( e, 0x64, sse_add( sse_mul( dt, state_f( e, 0x84 ) ), state_f( e, 0x64 ) ) );
        state_put_f( e, 0x68, sse_add( sse_mul( state_f( e, 0x88 ), dt ), state_f( e, 0x68 ) ) );
        state_put_f( e, 0x6C, sse_add( sse_mul( state_f( e, 0x8C ), dt ), state_f( e, 0x6C ) ) );
        float id[ 16 ], r[ 16 ], m[ 16 ], t[ 16 ], euler[ 3 ];
        vu_identity( id );
        std::memcpy( euler, e + 0x64, 12 );
        vu_matrix_from_euler( r, euler );
        hull_multiply( id, m, r );
        std::memcpy( m + 12, e + 0x54, 12 );
        float pivot[ 3 ];
        for ( int a = 0; a < 3; ++a )
        {
            std::uint32_t w;
            std::memcpy( &w, e + 0x44 + 4 * a, 4 );
            pivot[ a ] = water_bits( w ^ 0x80000000u );
        }
        hull_translation( t, pivot );
        hull_multiply( t, m, m );
        float base[ 16 ];
        std::memcpy( base, e + 4, 64 );
        hull_multiply( base, m, m );
        std::memcpy( e + 0x94, m, 64 );
        scene_aabb_grow( aabb, p.model_aabb, m );
    }
}

// One VuPiece as 0x411A50 reads it: +8 its matrix (16), +0x58 its model (the
// local aabb its +0x5C; none: the engine's zero aabb 0x7593F0), +0x1B0 the
// offset (3), +0x228 its pfx system (0 = none).
struct boat_debris_piece_t
{
    const float* matrix = nullptr;
    const float* model_aabb = nullptr;
    const float* offset = nullptr;
    std::uint32_t pfx = 0;
};

struct boat_debris_t
{
    float gravity = 0.0f;                        // +4 +0x68
    std::vector< boat_debris_part_t > parts;     // +4 +0x6C / +0x70
    std::uint8_t out = 0;                        // +0x94
    std::vector< boat_debris_piece_t > pieces;   // +0x9C / +0xA0
    float aabb[ 8 ]{};                           // +0xA8 .. +0xC7 (w lanes +0xB4 / +0xC4: never known, see below)
    float timer = 0.0f;                          // +0xC8
};

// 0x411A50 (__usercall: ecx the debris, xmm1 dt), Build step 5: the aabb reset
// (+0xA8 .. +0xB0 FLT_MAX, +0xB8 .. +0xC0 -FLT_MAX; +0xB4 / +0xC4 take a stack
// word the function never writes: UNKNOWN, not written here); when +0xC8 > 0
// (comiss) the parts (0x51F1C0); while +0x94, per piece: 0x5175E0( aabb, the
// model's aabb, piece +8 ), and with a pfx: setMatrix( translation( +0x1B0 ) .
// +8 ) (0x410AB0, VuMatrix_multiply), tick( dt ), and its aabb (slot 7: no
// empty test here) merged - minss( own, its ) / maxss.
inline void boat_debris_tick( boat_debris_t& d, float dt, const boat_pfx_ops_t& o )
{
    for ( int k = 0; k < 3; ++k )
    {
        d.aabb[ k ] = water_bits( 0x7F7FFFFFu );
        d.aabb[ 4 + k ] = water_bits( 0xFF7FFFFFu );
    }
    if ( d.timer > 0.0f )   // comiss / jbe: an ordered > (NaN and +-0 skip)
        boat_debris_parts_tick( d.gravity, d.parts, d.aabb, dt );
    if ( d.out == 0 )
        return;
    for ( const boat_debris_piece_t& p : d.pieces )
    {
        scene_aabb_grow( d.aabb, p.model_aabb != nullptr ? p.model_aabb : scene_zero_aabb( ), p.matrix );
        if ( p.pfx == 0 )
            continue;
        float t[ 16 ], m[ 16 ];
        hull_translation( t, p.offset );
        hull_multiply( t, m, p.matrix );
        o.set_matrix( o.user, p.pfx, m );
        o.tick( o.user, p.pfx, dt );
        float its[ 8 ];
        std::memcpy( its, o.aabb( o.user, p.pfx ), 32 );
        for ( int k = 0; k < 3; ++k )
        {
            d.aabb[ k ] = fluid_min( d.aabb[ k ], its[ k ] );
            d.aabb[ 4 + k ] = fluid_max( d.aabb[ 4 + k ], its[ 4 + k ] );
        }
    }
}
// 0x411950's per-piece body (thiscall the debris; the loop over +0x9C /
// +0xA0 after +0x94 = 0 and +0xC8 = 0 - boat/state.hxx boat_debris_reset,
// whose piece_remove hook this is): a piece in the world (+0x20C) leaves it -
// g_vudynamicsimpl (0x75900C) vt+0x40( its body +0x208 ) then vt+0x30( piece +
// 4, its step interface ), +0x20C = 0; a live pfx (+0x228) goes back to
// g_vupfximpl (0x75904C) vt+0x2C (release: world/pfxsim.hxx
// pfx_system_release); +0x228 = 0 either way. The dynamics calls are Vu
// virtuals: hooks.
struct boat_piece_hooks_t
{
    void* user = nullptr;
    void ( *remove_body )( void* user, std::uint32_t body ) = nullptr;           // vt+0x40
    void ( *remove_step )( void* user, std::uint8_t* step ) = nullptr;           // vt+0x30 (piece + 4)
    void ( *pfx_release )( void* user, std::uint32_t system ) = nullptr;         // g_vupfximpl vt+0x2C
};

inline void boat_piece_remove( std::uint8_t* piece, const boat_piece_hooks_t& h )
{
    if ( piece[ 0x20C ] )
    {
        std::uint32_t body;
        std::memcpy( &body, piece + 0x208, 4 );
        if ( h.remove_body )
            h.remove_body( h.user, body );
        if ( h.remove_step )
            h.remove_step( h.user, piece + 4 );
        piece[ 0x20C ] = 0;
    }
    std::uint32_t pfx;
    std::memcpy( &pfx, piece + 0x228, 4 );
    if ( pfx != 0 && h.pfx_release )
        h.pfx_release( h.user, pfx );
    image_put32( piece, 0x228, 0 );
}

// The Decision tick's destroyed branch (0x41EFF0, 0x41F103 .. 0x41F1A3: boat
// +0x46B set and the debris +0xA0 > 0) - boat_decision_hooks_t::piece_follow:
// the boat FOLLOWS the first piece. The transform's setWorldTransform 0x5344A0(
// piece +8, notify 0 ) (world/transform.hxx: the caller's), the boat body's
// +0x140 .. +0x15F (both velocities, four movq) = the piece body's (piece
// +0x208), then setCenterOfMassTransform 0x50E660( the boat body, the piece
// body's +0x10 transform ) (engine dynamics/world.hxx). Read from the
// disassembly. Verified: case debris_follow (the segment run on its own).
inline void boat_debris_follow( solver_rigid_t& body, const solver_rigid_t& piece_body, const float* piece_matrix,
                                void ( *set_world )( void* user, const float* m16 ), void* user )
{
    if ( set_world )
        set_world( user, piece_matrix );
    std::memcpy( body.linear_velocity, piece_body.linear_velocity, 16 );
    std::memcpy( body.angular_velocity, piece_body.angular_velocity, 16 );
    body_set_center_of_mass_transform( body, piece_body.basis, piece_body.origin );
}
// The hull tick 0x416630's splash (0x4167B8 .. 0x416921; boat/state.hxx
// boat_hull_tick calls hooks.splash with the name at hull +0x17C - this is
// what that hook stands for): createEffect 0x52BCD0 on the VuPfxManager g
// 0x75905C by the name (world/pfxmgr.hxx pfx_manager_create: `create` here,
// the caller's); none -> nothing. Else a handle (pfx_manager_handle: the
// entry's serial bumped, past the cap 1), the instance's slot 10 (the Duration
// getter, its x87 answer discarded - ops.duration when set), and when the handle is
// nonzero and looks up (pfx_manager_lookup) the looked-up entry's instance
// gets setMatrix( lookAtBasis 0x50CA70( (vx, vy, 0), (0, 0, 1) ) with row 3 =
// the transform's +0x80 .. +0x88 ), slot 3 ( vx, vy, 0, the body's +0x14C
// word ) and start (slot 11).
inline void boat_hull_splash( pfx_manager_t& m, const char* name, int ( *create )( void* user, const char* name ), void* user,
                              const std::uint32_t* velocity, const float* position, const boat_pfx_ops_t& o )
{
    const int e = create( user, name );
    if ( e < 0 )
        return;
    const std::uint32_t h = pfx_manager_handle( m, e );
    if ( o.duration )   // slot 10 on the created entry's instance: fld, then fstp st(0)
        o.duration( o.user, m.entries[ static_cast< std::size_t >( e ) ].instance );
    if ( h == 0u )
        return;
    const int x = pfx_manager_lookup( m, h );
    if ( x < 0 )
        return;
    std::uint32_t v[ 4 ] = { velocity[ 0 ], velocity[ 1 ], 0u, velocity[ 3 ] };
    float dir[ 4 ], mat[ 16 ];
    std::memcpy( dir, v, 16 );
    const float up[ 4 ] = { 0.0f, 0.0f, 1.0f, 0.0f };
    camera_look_at( dir, up, mat );
    mat[ 12 ] = position[ 0 ];
    mat[ 13 ] = position[ 1 ];
    mat[ 14 ] = position[ 2 ];
    const std::uint32_t inst = m.entries[ static_cast< std::size_t >( x ) ].instance;
    o.set_matrix( o.user, inst, mat );
    o.set_velocity( o.user, inst, v );
    o.start( o.user, inst );
}
// ---------------------------------------------------------------------------
// The launch: 0x410FF0 (__usercall: ecx the debris) - boat_decision_hooks_t's
// debris_410FF0, run by 0x41E010 when the boat is destroyed and by 0x422A10
// (the scripted path's flag 0x10).

// 0x51E790 (thiscall the debris +4; world*, velocity*; retn 8): L =
// lookAtBasis( velocity, (0, 0, 1) ) (0x50CA70; the up vector's fourth word a
// stack word nothing writes - not read); per part, M = model +0x24 . world
// (VuMatrix_multiply), +0x54 = the pivot +0x44 through M (((M0 p0 + M4 p1) +
// M8 p2) + M12 ...), +0x64 = M's euler (0x534AA0's three: world/surface.hxx
// water_world_euler, its CRT atan2 / asinf SUBSTITUTION); seven draws of the
// global VuRandom (inlined: boat/fleet.hxx fleet_random_t::draw) lerp the
// sub-object's ranges - l = (1 - r) lo + hi r: +0x20 / +0x30 .. +0x28 / +0x38
// the launch velocity in L's frame (+0x74 = ((L4 l2 + L0 l1) + L8 l3), ...),
// +0x40 / +0x50 .. +0x48 / +0x58 the spin in the world's frame (+0x84 =
// ((W0 a1 + a2 W4) + a3 W8), +0x88 = ((W5 a2 + W1 a1) + W9 a3), +0x8C =
// ((W6 a2 + W2 a1) + a3 W10)), +0x60 / +0x64 the damping +0xD4; +0x74 .. +=
// the velocity; +0x94 rebuilt as 0x51F1C0 does. The fourth words of the
// position / euler / velocity / spin stores (+0x60, +0x70, +0x80, +0x90) are
// stack words nothing writes: UNKNOWN, not written.
struct boat_debris_sub_t
{
    float range[ 18 ]{};   // +0x20 .. +0x67 of the sub-object (debris +0x24 ..)
};

inline void boat_debris_parts_launch( const boat_debris_sub_t& s, std::vector< boat_debris_part_t >& parts, const float* world,
                                      const float* velocity, fleet_random_t& rng )
{
    const float up[ 4 ] = { 0.0f, 0.0f, 1.0f, 0.0f };
    float L[ 16 ];
    camera_look_at( velocity, up, L );
    auto r = [ & ]( int lo, int hi, bool hi_first ) {
        const float t = rng.draw( );
        const float a = sse_mul( sse_sub( 1.0f, t ), s.range[ lo ] );
        return sse_add( a, hi_first ? sse_mul( s.range[ hi ], t ) : sse_mul( t, s.range[ hi ] ) );
    };
    for ( boat_debris_part_t& p : parts )
    {
        std::uint8_t* e = p.image;
        float M[ 16 ];
        hull_multiply( p.model_matrix, M, world );
        const float p0 = state_f( e, 0x44 ), p1 = state_f( e, 0x48 ), p2 = state_f( e, 0x4C );
        for ( int k = 0; k < 3; ++k )
            state_put_f( e, 0x54 + 4 * k, sse_add( sse_add( sse_add( sse_mul( M[ k ], p0 ), sse_mul( M[ 4 + k ], p1 ) ), sse_mul( M[ 8 + k ], p2 ) ), M[ 12 + k ] ) );
        float eul[ 3 ];
        water_world_euler( M, eul );
        std::memcpy( e + 0x64, eul, 12 );
        const float l1 = r( 0, 4, false ), l2 = r( 1, 5, true ), l3 = r( 2, 6, true );   // +0x20/+0x30 .. +0x28/+0x38
        for ( int k = 0; k < 3; ++k )
            state_put_f( e, 0x74 + 4 * k, sse_add( sse_add( sse_mul( L[ 4 + k ], l2 ), sse_mul( L[ k ], l1 ) ), sse_mul( L[ 8 + k ], l3 ) ) );
        const float a1 = r( 8, 12, true ), a2 = r( 9, 13, true ), a3 = r( 10, 14, true );   // +0x40/+0x50 .. +0x48/+0x58
        state_put_f( e, 0x84, sse_add( sse_add( sse_mul( world[ 0 ], a1 ), sse_mul( a2, world[ 4 ] ) ), sse_mul( a3, world[ 8 ] ) ) );
        state_put_f( e, 0x88, sse_add( sse_add( sse_mul( world[ 5 ], a2 ), sse_mul( world[ 1 ], a1 ) ), sse_mul( world[ 9 ], a3 ) ) );
        state_put_f( e, 0x8C, sse_add( sse_add( sse_mul( world[ 6 ], a2 ), sse_mul( world[ 2 ], a1 ) ), sse_mul( a3, world[ 10 ] ) ) );
        state_put_f( e, 0xD4, r( 16, 17, false ) );   // +0x60 / +0x64
        for ( int k = 0; k < 3; ++k )
            state_put_f( e, 0x74 + 4 * k, sse_add( state_f( e, 0x74 + 4 * k ), velocity[ k ] ) );
        float id[ 16 ], R[ 16 ], m[ 16 ], t[ 16 ], pivot[ 3 ];
        vu_identity( id );
        vu_matrix_from_euler( R, eul );
        hull_multiply( id, m, R );
        std::memcpy( m + 12, e + 0x54, 12 );
        for ( int a = 0; a < 3; ++a )
        {
            std::uint32_t w;
            std::memcpy( &w, e + 0x44 + 4 * a, 4 );
            pivot[ a ] = water_bits( w ^ 0x80000000u );
        }
        hull_translation( t, pivot );
        hull_multiply( t, m, m );
        float base[ 16 ];
        std::memcpy( base, e + 4, 64 );
        hull_multiply( base, m, m );
        std::memcpy( e + 0x94, m, 64 );
    }
}

// One VuPiece for the launch: its image (0x230: +8 matrix, +0x1B0 offset,
// +0x1C0 .. +0x1C8 / +0x1D0 .. +0x1D8 the velocity range, +0x1E0 .. / +0x1F0 ..
// the spin range, +0x208 its body, +0x20C in the world, +0x210 the pfx name,
// +0x228 the pfx), its rigid body and the name.
struct boat_launch_piece_t
{
    std::uint8_t* image = nullptr;
    solver_rigid_t* body = nullptr;
    std::string pfx_name;
};

struct boat_launch_hooks_t
{
    boat_piece_hooks_t remove;                                                   // 0x411950's and the loop's removals
    void ( *add_body )( void* user, std::uint32_t body ) = nullptr;              // g_vudynamicsimpl vt+0x3C
    void ( *add_step )( void* user, std::uint8_t* step ) = nullptr;              // vt+0x2C (piece + 4)
    std::uint32_t ( *pfx_create )( void* user, const std::string& name ) = nullptr;   // g_vupfximpl vt+0x28 (0: none)
    void ( *pfx_start )( void* user, std::uint32_t system ) = nullptr;           // the system's slot 11
};

// 0x410FF0: 0x411950 (boat_debris_reset with boat_piece_remove), +0x94 = 1,
// +0x98 = 10.0, +0xC8 = 1.0, the parts launched (0x51E790 on the transform's
// world and the boat body's +0x140 .. +0x14F); then per piece: removed again
// as 0x411950 does (no +0x94 test), +8 = the world, W = translation( +0x1B0 ) .
// it; the boat body's velocity at W's origin v = lin + ((dz wy - dy wz), (dx wz
// - dz wx), (dy wx - dx wy)), d = W row 3 - body origin; six draws: l = (1 - r)
// +0x1C0.. + +0x1D0.. r, a = (1 - r) +0x1E0.. + +0x1F0.. r; the piece body's
// +0x140 = v + ((W4 l2 + W0 l1) + W8 l3) per lane (+0x14C 0), +0x150 = the boat
// body's spin + ((W4 a2 + W0 a1) + W8 a3) (+0x15C 0); setCenterOfMassTransform
// 0x50E660( W as a btTransform: 0x410C30 ); into the world (vt+0x3C body, vt+0x2C
// piece + 4, +0x20C = 1) unless there; +0x228 = the pfx created by +0x210's name,
// started when made.
inline void boat_debris_launch( std::uint8_t* debris, boat_debris_sub_t& sub, std::vector< boat_debris_part_t >& parts,
                                std::vector< boat_launch_piece_t >& pieces, const float* world, const solver_rigid_t& body,
                                fleet_random_t& rng, const boat_launch_hooks_t& h )
{
    struct ctx_t
    {
        std::vector< boat_launch_piece_t >* pieces;
        const boat_piece_hooks_t* remove;
    } c{ &pieces, &h.remove };
    boat_decision_hooks_t dh;
    dh.user = &c;
    dh.piece_remove = []( void* u, int k ) {
        ctx_t& x = *static_cast< ctx_t* >( u );
        boat_piece_remove( ( *x.pieces )[ static_cast< std::size_t >( k ) ].image, *x.remove );
    };
    boat_debris_reset( debris, dh );
    debris[ 0x94 ] = 1;
    state_put_f( debris, 0x98, 10.0f );
    state_put_f( debris, 0xC8, 1.0f );
    boat_debris_parts_launch( sub, parts, world, body.linear_velocity, rng );
    std::int32_t count;
    std::memcpy( &count, debris + 0xA0, 4 );
    for ( std::int32_t k = 0; k < count; ++k )
    {
        boat_launch_piece_t& q = pieces[ static_cast< std::size_t >( k ) ];
        std::uint8_t* p = q.image;
        boat_piece_remove( p, h.remove );
        std::memcpy( p + 8, world, 64 );
        float T[ 16 ], W[ 16 ], m[ 16 ];
        hull_translation( T, reinterpret_cast< const float* >( p + 0x1B0 ) );
        std::memcpy( m, p + 8, 64 );
        hull_multiply( T, W, m );
        const float dx = sse_sub( W[ 12 ], body.origin[ 0 ] ), dy = sse_sub( W[ 13 ], body.origin[ 1 ] ), dz = sse_sub( W[ 14 ], body.origin[ 2 ] );
        const float* lv = body.linear_velocity;
        const float* av = body.angular_velocity;
        const float vx = sse_add( lv[ 0 ], sse_sub( sse_mul( dz, av[ 1 ] ), sse_mul( dy, av[ 2 ] ) ) );
        const float vy = sse_add( lv[ 1 ], sse_sub( sse_mul( dx, av[ 2 ] ), sse_mul( dz, av[ 0 ] ) ) );
        const float vz = sse_add( lv[ 2 ], sse_sub( sse_mul( dy, av[ 0 ] ), sse_mul( dx, av[ 1 ] ) ) );
        auto r = [ & ]( int lo ) {
            const float t = rng.draw( );
            return sse_add( sse_mul( sse_sub( 1.0f, t ), state_f( p, lo ) ), sse_mul( state_f( p, lo + 0x10 ), t ) );
        };
        const float l1 = r( 0x1C0 ), l2 = r( 0x1C4 ), l3 = r( 0x1C8 ), a1 = r( 0x1E0 ), a2 = r( 0x1E4 ), a3 = r( 0x1E8 );
        auto frame = [ & ]( int k2, float x1, float x2, float x3 ) {
            return sse_add( sse_add( sse_mul( W[ 4 + k2 ], x2 ), sse_mul( W[ k2 ], x1 ) ), sse_mul( W[ 8 + k2 ], x3 ) );
        };
        solver_rigid_t& pb = *q.body;
        pb.linear_velocity[ 0 ] = sse_add( vx, frame( 0, l1, l2, l3 ) );
        pb.linear_velocity[ 1 ] = sse_add( frame( 1, l1, l2, l3 ), vy );
        pb.linear_velocity[ 2 ] = sse_add( frame( 2, l1, l2, l3 ), vz );
        pb.linear_velocity[ 3 ] = 0.0f;
        pb.angular_velocity[ 0 ] = sse_add( av[ 0 ], frame( 0, a1, a2, a3 ) );
        pb.angular_velocity[ 1 ] = sse_add( av[ 1 ], frame( 1, a1, a2, a3 ) );
        pb.angular_velocity[ 2 ] = sse_add( av[ 2 ], frame( 2, a1, a2, a3 ) );
        pb.angular_velocity[ 3 ] = 0.0f;
        float basis[ 12 ], origin[ 4 ];
        for ( int row = 0; row < 3; ++row )
        {
            for ( int k2 = 0; k2 < 3; ++k2 )
                basis[ 4 * row + k2 ] = W[ row + 4 * k2 ];
            basis[ 4 * row + 3 ] = 0.0f;
        }
        origin[ 0 ] = W[ 12 ];
        origin[ 1 ] = W[ 13 ];
        origin[ 2 ] = W[ 14 ];
        origin[ 3 ] = 0.0f;
        body_set_center_of_mass_transform( pb, basis, origin );
        if ( p[ 0x20C ] == 0 )
        {
            std::uint32_t bw;
            std::memcpy( &bw, p + 0x208, 4 );
            if ( h.add_body )
                h.add_body( h.remove.user, bw );
            if ( h.add_step )
                h.add_step( h.remove.user, p + 4 );
            p[ 0x20C ] = 1;
        }
        const std::uint32_t s = h.pfx_create ? h.pfx_create( h.remove.user, q.pfx_name ) : 0u;
        image_put32( p, 0x228, s );
        if ( s != 0 && h.pfx_start )
            h.pfx_start( h.remove.user, s );
    }
}
// 0x41E240 (thiscall the boat) - boat_decision_hooks_t's boat_41E240, run
// after the launch: the FMOD event named by the VuString at boat +0x1EC
// (FMOD::EventSystem::getEvent on g_vuaudio +8, mode 1); found (FMOD_OK) ->
// set3DAttributes( the transform's +0x80 .. +0x88, no velocity, no
// orientation ) and start. All three are the platform's audio: hooks.
// Verified: case destroyed_event (0x41E240 run whole, FMOD logged).
struct boat_event_hooks_t
{
    void* user = nullptr;
    bool ( *get )( void* user, const std::string& name, std::uint32_t& event ) = nullptr;   // true: FMOD_OK
    void ( *attributes )( void* user, std::uint32_t event, const float* position ) = nullptr;
    void ( *start )( void* user, std::uint32_t event ) = nullptr;
};

inline void boat_destroyed_event( const std::string& name, const float* position, const boat_event_hooks_t& h )
{
    std::uint32_t ev = 0;
    if ( !h.get || !h.get( h.user, name, ev ) )
        return;
    if ( h.attributes )
        h.attributes( h.user, ev, position );
    if ( h.start )
        h.start( h.user, ev );
}
// ---------------------------------------------------------------------------
// THE FILL (postLoad's Pfx step, boat/load.hxx): what puts records into the
// seven lists. Recreated 1:1 from 0x419EF0 (clear), 0x419D70 (the reader,
// VuBoat_selectHullEffect, run with Default's Pfx section then the boat's),
// 0x41A1A0 (one list from a JSON array), 0x419FF0 (create) with 0x41A0E0 /
// 0x41A640. Hooks (not recreated - other subsystems' Vu code):
//   create / release  g_vupfx 0x75904C vt+0x28 (name) / vt+0x2C (system)
//   start / root      the system's vt+0x2C (slot 11) / vt+0x40
//   type_of           a process object's vt+4 (its type node; +4 = the parent)
//   find_bone         boat +0x2A4 (the model instance) +0x10 (its skeleton):
//                     VuSkeleton_findNodeByName 0x52F3C0 (render / model lane)
//   fluids_set        0x5243A0 on the VuFluidsObject +0x34 (the water agent's)
//   word / put        the pfx runtime's memory (patterns +0x10 / +4, processes
//                     +0x14 / +4, the object +0x10, type nodes +4) and the
//                     fluids object's word 0
// A missing hook answers false (UNKNOWN): nothing is guessed.
struct boat_pfx_fill_hooks_t
{
    void* user = nullptr;
    bool manager = true;   // g_vupfx non-null: only 0x41A0E0 tests it
    std::uint32_t ( *create )( void* user, const char* name ) = nullptr;
    void ( *release )( void* user, std::uint32_t system ) = nullptr;
    void ( *start )( void* user, std::uint32_t system ) = nullptr;
    std::uint32_t ( *root )( void* user, std::uint32_t system ) = nullptr;
    std::uint32_t ( *type_of )( void* user, std::uint32_t object ) = nullptr;
    std::int32_t ( *find_bone )( void* user, const char* name ) = nullptr;
    void ( *fluids_set )( void* user, const std::string& name ) = nullptr;
    std::uint32_t ( *word )( void* user, std::uint32_t address ) = nullptr;
    void ( *put )( void* user, std::uint32_t address, std::uint32_t value ) = nullptr;
};

inline bool boat_pfx_fill_ready( const boat_pfx_fill_hooks_t& h )
{
    return h.create && h.release && h.start && h.root && h.type_of && h.find_bone && h.fluids_set && h.word && h.put;
}

// 0x41A0E0 (stdcall list): with g_vupfx, every record's non-null system
// released (vt+0x2C); then every name destroyed and end = begin.
inline void boat_pfx_clear_list( std::vector< boat_pfx_record_t >& l, const boat_pfx_fill_hooks_t& h )
{
    if ( h.manager )
        for ( const boat_pfx_record_t& r : l )
            if ( r.system )
                h.release( h.user, r.system );
    l.clear( );
}

// 0x419EF0 (thiscall controller): 0x5243A0( "" ) on +0x34, the seven lists
// cleared (+0x38 .. +0x80 in order), then +0x94 released when set - retail
// does not zero it.
inline bool boat_pfx_clear( boat_pfx_t& c, const boat_pfx_fill_hooks_t& h )
{
    if ( !boat_pfx_fill_ready( h ) )
        return false;
    h.fluids_set( h.user, std::string( ) );
    for ( std::vector< boat_pfx_record_t >& l : c.list )
        boat_pfx_clear_list( l, h );
    if ( c.extra )
        h.release( h.user, c.extra );
    return true;
}

// The euler matrix 0x41A3BD .. 0x41A54C (angles = RotOffset x 0x3C8EFA35, the
// three x87 fsincos), in retail's operation order; its row 3 = (0, 0, 0, 1).
inline void boat_pfx_euler( const float* rot, float* e )
{
    const float k = support_bits( 0x3C8EFA35u );
    float cx, sx, cy, sy, cz, sz;
    x87_sincos( rot[ 0 ] * k, &cx, &sx );
    x87_sincos( rot[ 1 ] * k, &cy, &sy );
    x87_sincos( rot[ 2 ] * k, &cz, &sz );
    const float czcx = cz * cx, czsx = cz * sx;
    e[ 0 ] = cz * cy;
    e[ 1 ] = sz * cy;
    e[ 2 ] = -sy;
    e[ 3 ] = 0.0f;
    e[ 4 ] = sy * czsx - sz * cx;
    e[ 5 ] = ( sy * sx ) * sz + czcx;
    e[ 6 ] = cy * sx;
    e[ 7 ] = 0.0f;
    e[ 8 ] = sz * sx + sy * czcx;
    e[ 9 ] = ( sy * cx ) * sz - czsx;
    e[ 10 ] = cy * cx;
    for ( int i = 11; i < 15; ++i )
        e[ i ] = 0.0f;
    e[ 15 ] = 1.0f;
}

// 0x41A1A0 (thiscall controller; list, array; retn 8): per element (the
// array's i-th, or the null container past it / for a non-array): "Effect"
// (tag 4, else ""), "PosOffset" / "RotOffset" (vec3 into zeros), the local =
// identity x euler (VuMatrix_multiply 0x4078C0) with row 3 = PosOffset,
// "Bone" (tag 4, else "") through findNodeByName, system 0; pushed back
// (0x41AD70).
inline void boat_pfx_load_list( std::vector< boat_pfx_record_t >& l, const value_t& array, const boat_pfx_fill_hooks_t& h )
{
    static const value_t null{ };
    const std::size_t count = array.tag == 5 ? array.list.size( ) : 0u;
    for ( std::size_t i = 0; i < count; ++i )
    {
        const value_t& e = i < array.list.size( ) ? array.list[ i ] : null;
        boat_pfx_record_t r;
        const value_t& effect = member( e, "Effect" );
        if ( effect.tag == 4 )
            r.name = effect.text;
        float pos[ 3 ] = { 0.0f, 0.0f, 0.0f }, rot[ 3 ] = { 0.0f, 0.0f, 0.0f };
        vec3( member( e, "PosOffset" ), pos );
        vec3( member( e, "RotOffset" ), rot );
        float euler[ 16 ];
        boat_pfx_euler( rot, euler );
        const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        hull_multiply( identity, r.local, euler );
        r.local[ 12 ] = pos[ 0 ];
        r.local[ 13 ] = pos[ 1 ];
        r.local[ 14 ] = pos[ 2 ];
        const value_t& bone = member( e, "Bone" );
        r.bone = h.find_bone( h.user, bone.tag == 4 ? bone.text.c_str( ) : "" );
        l.push_back( std::move( r ) );
    }
}

// 0x419D70 (thiscall controller; section; retn 4). `driver` = boat +0x220's
// class: slot 12 (vt+0x30) and slot 14 (vt+0x38).
inline bool boat_pfx_read( boat_pfx_t& c, const value_t& section, const boat_driver_t& driver, const boat_pfx_fill_hooks_t& h )
{
    if ( !boat_pfx_fill_ready( h ) )
        return false;
    const value_t& hull = member( section, "HullEffect" );
    const value_t& mesh = member( hull, "Fluids Mesh Asset" );
    if ( mesh.tag == 4 )
        c.fluids_asset = mesh.text;
    const value_t& effect = member( hull, driver.slot[ 0 ] && driver.slot[ 2 ] ? "CameraTargetEffect" : "Effect" );
    if ( effect.tag == 4 )
        c.hull_effect = effect.text;
    boat_pfx_load_list( c.list[ 0 ], member( section, "ThrustAlphaEffects" ), h );
    boat_pfx_load_list( c.list[ 1 ], member( section, "ThrustColorEffects" ), h );
    boat_pfx_load_list( c.list[ 4 ], member( section, "BoostEffects" ), h );
    if ( driver.slot[ 0 ] )
    {
        boat_pfx_load_list( c.list[ 2 ], member( section, "BackfireEffects" ), h );
        boat_pfx_load_list( c.list[ 5 ], member( section, "SpeedEffects" ), h );
        if ( driver.slot[ 2 ] )
        {
            boat_pfx_load_list( c.list[ 3 ], member( section, "ChurnEffects" ), h );
            boat_pfx_load_list( c.list[ 6 ], member( section, "DraftEffects" ), h );
        }
    }
    return true;
}

// The process walk 0x41A69B / 0x41A08A: patterns from +0x10 (next +4), their
// processes from +0x14 (next +4); a process whose object's (+0x10) type chain
// (vt+4, then each node's +4) holds `type` is bound by `bind`.
template < typename F >
inline void boat_pfx_walk( std::uint32_t base, std::uint32_t type, const boat_pfx_fill_hooks_t& h, F bind )
{
    for ( std::uint32_t pat = h.word( h.user, base + 0x10 ); pat; pat = h.word( h.user, pat + 4 ) )
        for ( std::uint32_t proc = h.word( h.user, pat + 0x14 ); proc; proc = h.word( h.user, proc + 4 ) )
            for ( std::uint32_t t = h.type_of( h.user, h.word( h.user, proc + 0x10 ) ); t; t = h.word( h.user, t + 4 ) )
                if ( t == type )
                {
                    bind( proc );
                    break;
                }
}

// 0x41A640 (thiscall controller; list; retn 4): every record's system =
// vt+0x28( name ); a created one's VuPfxBoat* (0x7367D0) processes get +0x28 =
// the boat; then the records left null are erased in order (0x41B040).
inline void boat_pfx_create_list( boat_pfx_t& c, std::vector< boat_pfx_record_t >& l, const boat_pfx_fill_hooks_t& h )
{
    for ( boat_pfx_record_t& r : l )
    {
        r.system = h.create( h.user, r.name.c_str( ) );
        if ( r.system )
            boat_pfx_walk( r.system, 0x7367D0u, h, [ & ]( std::uint32_t proc ) { h.put( h.user, proc + 0x28, c.boat ); } );
    }
    for ( std::size_t i = 0; i < l.size( ); )
    {
        if ( l[ i ].system == 0 )
        {
            l.erase( l.begin( ) + static_cast< std::ptrdiff_t >( i ) );
            i = 0;
        }
        else
            ++i;
    }
}

// 0x419FF0 (thiscall controller; behind its owner's flag bit 2): the seven
// lists created, +0x38 / +0x44 started (0x41ABE0), 0x5243A0( +0 ) on +0x34;
// with the fluids object's word 0 set: +0x94 = vt+0x28( +0x18 ), its root's
// (vt+0x40) 0x7367D8 processes bound (+0x18 = the fluids object, +0x14 = the
// boat), then started.
inline bool boat_pfx_create( boat_pfx_t& c, const boat_pfx_fill_hooks_t& h )
{
    if ( !boat_pfx_fill_ready( h ) )
        return false;
    for ( std::vector< boat_pfx_record_t >& l : c.list )
        boat_pfx_create_list( c, l, h );
    for ( int k = 0; k < 2; ++k )
        for ( const boat_pfx_record_t& r : c.list[ k ] )
            h.start( h.user, r.system );
    h.fluids_set( h.user, c.fluids_asset );
    if ( h.word( h.user, c.fluids ) == 0 )
        return true;
    c.extra = h.create( h.user, c.hull_effect.c_str( ) );
    if ( c.extra == 0 )
        return true;
    boat_pfx_walk( h.root( h.user, c.extra ), 0x7367D8u, h, [ & ]( std::uint32_t proc )
    {
        h.put( h.user, proc + 0x18, c.fluids );
        h.put( h.user, proc + 0x14, c.boat );
    } );
    h.start( h.user, c.extra );
    return true;
}

// postLoad's whole Pfx step (boat/load.hxx): clear, Default's section, the
// boat's own, then (owner flag bit 2) create.
inline bool boat_pfx_fill( boat_pfx_t& c, const value_t& fallback, const value_t& own, const boat_driver_t& driver, bool create,
                           const boat_pfx_fill_hooks_t& h )
{
    if ( !boat_pfx_clear( c, h ) || !boat_pfx_read( c, fallback, driver, h ) || !boat_pfx_read( c, own, driver, h ) )
        return false;
    return !create || boat_pfx_create( c, h );
}