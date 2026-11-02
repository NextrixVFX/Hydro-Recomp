#pragma once

#include <workspace/core/boat/state.hxx>
#include <workspace/core/world/query.hxx>

#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include <string>
#include <vector>

// VuGhostManager (g 0x758FC8, 0x70 bytes, ctor 0x4AF3C0, vtable 0x745668): the boat recording the human's run and the
// ghosts loaded for it (world batch 31). Its record at +4 and every ghost's at +4 share one shape, 0x4AF1C0 / 0x4AF220:
//   +0 / +0x18 / +0x30 three std::strings (the boat, the skin, the driver's name), +0x48 the frames (a 16-aligned array
//   of 24-byte frames), +0x4C their count, +0x50 its capacity.
// A frame: +0 the record clock, +4 .. +0xC the transform's position, +0x10 .. +0x14 its rotation x 0x6F7834 (int16s), +0x16 flags.
// The manager past its record: +0x58 the boat recorded, +0x5C the clock, +0x60 the frame accumulator, +0x64 the ghosts.
//
// 0x4AF640 (a ghost's VuGhostBoatEntity from its blob) and the entity: boat/ghost.hxx. NOT RECREATED (hooks / UNKNOWN):
// the tick registration's own phase is g_vutickmanager's (0x4AF9C0 is run by whoever registered it).
//
// The blob (the leaderboard / cloud payload - the service is a platform hook, the bytes are retail's): 16 bytes
// 0x56555242, 2, the payload size, FNV-1a (0x811C9DC5, 0x1000193) of the payload; the three strings NUL-terminated, the frame
// count (u32), the frames.

struct ghost_data_t
{
    std::string boat, skin, driver;
    std::vector< std::uint8_t > frames;   // 24 x count
    std::int32_t count = 0;               // +0x4C
    std::int32_t capacity = 0;            // +0x50
};

inline constexpr std::uint32_t k_ghost_magic = 0x56555242u;

// 0x4AFE10 (thiscall the array; count; retn 4): capacity grows to max( count, capacity + capacity / 2 ) when count exceeds it (aligned new,
// the old count's bytes copied); the count is set either way (a negative one too: nothing but the store).
inline void ghost_resize( ghost_data_t& d, std::int32_t count )
{
    if ( count > d.capacity )
    {
        const std::int32_t grown = d.capacity + d.capacity / 2;
        const std::int32_t want = count > grown ? count : grown;
        if ( want > d.capacity )
        {
            d.frames.resize( 24u * static_cast< std::size_t >( want ) );
            d.capacity = want;
        }
    }
    d.count = count;
}

// FNV-1a over [p, p + n) (0x4ABB40, seed on the stack).
inline std::uint32_t ghost_hash( const std::uint8_t* p, std::size_t n, std::uint32_t h = 0x811C9DC5u )
{
    for ( std::size_t k = 0; k < n; ++k )
        h = ( h ^ p[ k ] ) * 0x1000193u;
    return h;
}

// 0x4AFB90 (thiscall the data; buffer*; retn 4): appended to the buffer (0x4ABA90: each write grows it at its end) 16 zero
// bytes, the three strings with their NULs, the count, the frames; then the header over the buffer's FIRST 16 bytes, of the
// buffer's whole size - 16 (bytes before the call count).
inline void ghost_write( const ghost_data_t& d, std::vector< std::uint8_t >& out )
{
    auto put = [ & ]( const void* p, std::size_t n ) {
        const std::size_t at = out.size( );
        out.resize( at + n );
        if ( n != 0u )
            std::memcpy( out.data( ) + at, p, n );
    };
    const std::uint8_t zero[ 16 ] = { };
    put( zero, 16 );
    put( d.boat.c_str( ), d.boat.size( ) + 1u );
    put( d.skin.c_str( ), d.skin.size( ) + 1u );
    put( d.driver.c_str( ), d.driver.size( ) + 1u );
    put( &d.count, 4 );
    put( d.frames.data( ), 24u * static_cast< std::size_t >( d.count ) );
    const std::uint32_t size = static_cast< std::uint32_t >( out.size( ) ) - 16u;
    const std::uint32_t header[ 4 ] = { k_ghost_magic, 2u, size, ghost_hash( out.data( ) + 16, size ) };
    std::memcpy( out.data( ), header, 16 );
}

// 0x4AFCE0 (thiscall the data; const buffer*; retn 4): the boat string emptied, the frames freed (count = capacity = 0)
// first - the skin and driver strings stay as they were on a blob refused. Refused: under 16 bytes, the magic, version 2, the
// size, the hash. Then 0x4AF320 x 3 (a NUL-ended string at the cursor), 0x4AFE80 (the count), 0x4AFE10, the frames
// (0x4ABB10). Returns count > 0 (signed). Retail reads past a blob whose string has no NUL / whose count overruns it:
// `overrun` = that (UNKNOWN: nothing read, the same answer as refused) - a blob whose hash holds.
inline bool ghost_read( ghost_data_t& d, const std::uint8_t* blob, std::size_t size, bool* overrun = nullptr )
{
    d.boat.clear( );
    d.frames.clear( );
    d.count = 0;
    d.capacity = 0;
    if ( size < 16u )
        return false;
    std::uint32_t h[ 4 ];
    std::memcpy( h, blob, 16 );
    if ( h[ 0 ] != k_ghost_magic || h[ 1 ] != 2u || h[ 2 ] != static_cast< std::uint32_t >( size - 16u ) || h[ 3 ] != ghost_hash( blob + 16, size - 16u ) )
        return false;
    std::size_t at = 16u;
    std::string* strings[ 3 ] = { &d.boat, &d.skin, &d.driver };
    for ( std::string* s : strings )
    {
        const void* end = at < size ? std::memchr( blob + at, 0, size - at ) : nullptr;
        if ( end == nullptr )
        {
            if ( overrun )
                *overrun = true;
            return false;
        }
        s->assign( reinterpret_cast< const char* >( blob + at ), static_cast< const std::uint8_t* >( end ) - ( blob + at ) );
        at += s->size( ) + 1u;
    }
    if ( size - at < 4u )
    {
        if ( overrun )
            *overrun = true;
        return false;
    }
    std::int32_t count;
    std::memcpy( &count, blob + at, 4 );
    at += 4u;
    if ( count < 0 || static_cast< std::uint64_t >( count ) * 24u > size - at )
    {
        if ( overrun )
            *overrun = true;
        return false;
    }
    ghost_resize( d, count );
    if ( count != 0 )
        std::memcpy( d.frames.data( ), blob + at, 24u * static_cast< std::size_t >( count ) );
    return d.count > 0;
}

// What 0x4AFA30 reads of the boat (+0x58): the transform (+0x50) +0x80 / +0xA0, +0x310, +0x32C, the driver's vt+0x64 answer
// (its `throttle`), +0x464, the debris' (+0x238) +0x94.
struct ghost_boat_t
{
    const std::uint8_t* boat = nullptr;
    const std::uint8_t* transform = nullptr;
    const std::uint8_t* debris = nullptr;
    float throttle = 0.0f;
};

struct ghost_manager_t
{
    ghost_data_t record;       // +4
    bool recording = false;    // +0x58 set (the boat is the caller's)
    float clock = 0.0f;        // +0x5C
    float accumulator = 0.0f;  // +0x60
};

// 0x4AFA30 (thiscall the manager): one more frame.
inline void ghost_record_frame( ghost_manager_t& m, const ghost_boat_t& b )
{
    ghost_resize( m.record, m.record.count + 1 );
    std::uint8_t* f = m.record.frames.data( ) + 24u * ( static_cast< std::size_t >( m.record.count ) - 1u );
    std::uint16_t flags = 0;
    const float k310 = state_f( b.boat, 0x310 );
    if ( _mm_comigt_ss( _mm_set_ss( k310 ), _mm_set_ss( 0.0f ) ) )   // cmova on comiss 0x6F7570
        flags = 1;
    if ( b.boat[ 0x32C ] )
        flags |= 2u;
    if ( _mm_comigt_ss( _mm_set_ss( b.throttle ), _mm_set_ss( 0.25f ) ) )   // 0x6F762C
        flags |= 4u;
    if ( b.boat[ 0x464 ] )
        flags |= 8u;
    if ( b.debris[ 0x94 ] )
        flags |= 0x10u;
    std::memcpy( f, &m.clock, 4 );
    std::memcpy( f + 4, b.transform + 0x80, 12 );
    for ( int k = 0; k < 3; ++k )
    {
        // mulss 0x6F7834, then fld / fistp (round to nearest; out of range or NaN: 0x80000000) - the word's low half
        const float r = state_f( b.transform, 0xA0 + 4 * k ) * water_bits( 0x4622E298u );
        const std::int32_t v = _mm_cvtss_si32( _mm_set_ss( r ) );
        const std::uint16_t lo = static_cast< std::uint16_t >( v );
        std::memcpy( f + 0x10 + 2 * k, &lo, 2 );
    }
    std::memcpy( f + 0x16, &flags, 2 );
}

// 0x4AF9C0 (thiscall the manager; dt; retn 4), the tick 0x4AF520 registers: while 300 s (0x6F7810) > the clock, the clock and the
// accumulator gain dt and, the accumulator reaching 1/15 s (0x6F7604, comiss / jb), a frame; then the period off it until below.
inline void ghost_tick( ghost_manager_t& m, const ghost_boat_t& b, float dt )
{
    const float period = water_bits( 0x3D888889u );
    auto below = []( float a, float x ) { return _mm_comilt_ss( _mm_set_ss( a ), _mm_set_ss( x ) ) != 0; };   // jb: CF also set when unordered
    if ( !_mm_comigt_ss( _mm_set_ss( 300.0f ), _mm_set_ss( m.clock ) ) )
        return;
    m.clock = m.clock + dt;
    m.accumulator = m.accumulator + dt;
    if ( below( m.accumulator, period ) || m.accumulator != m.accumulator )
        return;
    ghost_record_frame( m, b );
    float x = m.accumulator;
    if ( below( x, period ) || x != x )
        return;
    do
        x = x - period;
    while ( !below( x, period ) && x == x );
    m.accumulator = x;
}

// 0x4AF520 (thiscall the manager; the boat; retn 4) on the record: +0x58 the boat, boat +0x8C / +0xA4 into the boat and skin
// strings, the boat's driver's (+0x220) +0x60 into the driver string; the frames' capacity 0x800 unless at least that already
// (aligned new, the count's frames copied); clock and accumulator 0. Then the tick's registration (g 0x759008 vt+0x20, a
// VuMethod1 0x745658 on 0x4AF9C0, phase string 0x6ABE24) - the caller's. The count stays.
inline void ghost_start( ghost_manager_t& m, const std::string& boat, const std::string& skin, const std::string& driver )
{
    m.recording = true;
    m.record.boat = boat;
    m.record.skin = skin;
    m.record.driver = driver;
    if ( m.record.capacity < 0x800 )
    {
        m.record.frames.resize( 24u * 0x800u );
        m.record.capacity = 0x800;
    }
    m.clock = 0.0f;
    m.accumulator = 0.0f;
}

// 0x4AF490 (thiscall the manager) on the record: the boat string emptied, the frames freed, count = capacity = 0. The ghosts
// (+0x64: each entity's 0x500B80 and refcount, its data, the record) are the caller's (boat/ghost.hxx ghost_record_t).
inline void ghost_clear( ghost_manager_t& m )
{
    m.record.boat.clear( );
    m.record.frames.clear( );
    m.record.count = 0;
    m.record.capacity = 0;
}
