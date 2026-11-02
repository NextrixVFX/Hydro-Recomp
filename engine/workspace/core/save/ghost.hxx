#pragma once

#include <workspace/core/save/seam.hxx>
#include <workspace/core/save/writer.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include <emmintrin.h>

// The ghost store, local (the user's Saves decision): VuGhostManager's recording (g 0x758FC8, vtable 0x745668), the ghost
// blob, the text form it travels in, and VuMetroTitleStorageManager's GET / PUT (slots 6 / 7 0x4A8850 / 0x4A8C40, HTTP to
// Xbox title storage in retail) answered from local files through the one seam.
//
// One sample, 24 bytes: +0 the time, +4 the position, +0x10 the rotation's three int16 (radians x 10424.648), +0x16 flags.
struct ghost_sample_t
{
    float time = 0.0f;
    float position[ 3 ]{};
    std::int16_t rotation[ 3 ]{};
    std::uint16_t flags = 0;
};
static_assert( sizeof( ghost_sample_t ) == 24 );

// The record (0x5C bytes, ctor 0x4AF1C0): +0 index; +4 / +0x1C / +0x34 boat, skin, driver; +0x4C the samples. +0x58 the
// VuBoatEntity retail makes in 0x4AF640: the caller's.
struct ghost_record_t
{
    std::string boat;
    std::string skin;
    std::string driver;
    std::vector< ghost_sample_t > samples;
};

// 0x4AFB90 (this = VuGhostManager + 4): 16 header bytes, the three texts each with its NUL, the sample count, the samples;
// then the header over the rest: 'BRUV' 0x56555242, version 2, the rest's size, its FNV-1a.
inline void ghost_write( const ghost_record_t& record, std::vector< std::uint8_t >& blob )
{
    blob.assign( 16, 0 );
    for ( const std::string* text : { &record.boat, &record.skin, &record.driver } )
        blob.insert( blob.end( ), text->c_str( ), text->c_str( ) + text->size( ) + 1 );
    const std::uint32_t count = static_cast< std::uint32_t >( record.samples.size( ) );
    const auto* at = reinterpret_cast< const std::uint8_t* >( &count );
    blob.insert( blob.end( ), at, at + 4 );
    at = reinterpret_cast< const std::uint8_t* >( record.samples.data( ) );
    blob.insert( blob.end( ), at, at + 24 * static_cast< std::size_t >( count ) );
    const std::uint32_t size = static_cast< std::uint32_t >( blob.size( ) ) - 16;
    const std::uint32_t head[ 4 ] = { 0x56555242u, 2, size, save_hash( blob.data( ) + 16, size ) };
    std::memcpy( blob.data( ), head, 16 );
}

// 0x4AFCE0: the boat's text and the samples go first (the skin and the driver stay until a good header replaces them); a
// good header replaces all of it. The answer: at least one sample. Retail reads past the end of a blob whose hash passes
// but whose texts or count overrun (strlen, memcpy_s of 24 * count): no defined result, read as a failed blob here.
inline bool ghost_read( ghost_record_t& record, const std::vector< std::uint8_t >& blob )
{
    record.boat.clear( );
    record.samples.clear( );
    if ( blob.size( ) < 16 )
        return false;
    std::uint32_t head[ 4 ];
    std::memcpy( head, blob.data( ), 16 );
    const std::uint32_t size = static_cast< std::uint32_t >( blob.size( ) ) - 16;
    if ( head[ 0 ] != 0x56555242u || head[ 1 ] != 2 || head[ 2 ] != size || head[ 3 ] != save_hash( blob.data( ) + 16, size ) )
        return false;
    std::size_t at = 16;
    const auto text = [ & ]( std::string& out ) {
        const void* end = at < blob.size( ) ? std::memchr( blob.data( ) + at, 0, blob.size( ) - at ) : nullptr;
        if ( end == nullptr )
            return false;
        const std::size_t length = static_cast< const std::uint8_t* >( end ) - ( blob.data( ) + at );
        out.assign( reinterpret_cast< const char* >( blob.data( ) ) + at, length );
        at += length + 1;
        return true;
    };
    if ( !text( record.boat ) || !text( record.skin ) || !text( record.driver ) || blob.size( ) < at + 4 )
        return false;
    std::uint32_t count = 0;
    std::memcpy( &count, blob.data( ) + at, 4 );
    at += 4;
    if ( ( blob.size( ) - at ) / 24 < count )
        return false;
    record.samples.resize( count );
    std::memcpy( record.samples.data( ), blob.data( ) + at, 24 * static_cast< std::size_t >( count ) );
    return count > 0;
}

// 0x558300: base64, table 0x6D2D78; 4 * ( ( size + 2 ) / 3 ) characters, '=' padded.
inline std::string save_base64( const std::uint8_t* bytes, std::uint32_t size )
{
    static constexpr char table[ ] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve( ( size + 2 ) / 3 * 4 );
    for ( std::uint32_t k = 0; k < size; k += 3 )
    {
        const std::uint32_t left = size - k;
        const std::uint8_t a = bytes[ k ], b = left > 1 ? bytes[ k + 1 ] : 0, c = left > 2 ? bytes[ k + 2 ] : 0;
        out += table[ a >> 2 ];
        out += table[ ( ( b >> 4 ) + ( a << 4 ) ) & 0x3F ];
        out += left > 1 ? table[ ( ( c >> 6 ) + ( b << 2 ) ) & 0x3F ] : '=';
        out += left > 2 ? table[ c & 0x3F ] : '=';
    }
    return out;
}

// 0x558440: 3 * ( length / 4 ) bytes, less one per trailing '=' (the last two characters tested). Four characters at a
// time from the left; a character outside the table ends the walk; 2 characters left end it after one byte, 3 after two;
// a '=' third character needs 4 left and a '=' fourth, a '=' fourth needs 4 left. Then `out` must hold exactly the size
// the first line reckoned: else it is emptied, false.
inline bool save_unbase64( const std::string& text, std::vector< std::uint8_t >& out )
{
    static const std::vector< std::int8_t > index = []( ) {
        std::vector< std::int8_t > t( 256, -1 );
        static constexpr char table[ ] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for ( int k = 0; k < 64; ++k )
            t[ static_cast< std::uint8_t >( table[ k ] ) ] = static_cast< std::int8_t >( k );
        return t;
    }( );
    std::int32_t left = static_cast< std::int32_t >( text.size( ) );
    std::int32_t size = 3 * ( left / 4 );
    if ( left != 0 && text[ left - 1 ] == '=' )
        --size;
    if ( static_cast< std::uint32_t >( left ) > 1 && text[ left - 2 ] == '=' )
        --size;
    out.assign( static_cast< std::size_t >( size > 0 ? size : 0 ), 0 );
    std::size_t put = 0;
    const auto emit = [ & ]( std::uint8_t byte ) {
        if ( put < out.size( ) )
            out[ put ] = byte;
        ++put;
    };
    const std::uint8_t* at = reinterpret_cast< const std::uint8_t* >( text.data( ) );
    const auto of = [ & ]( std::uint8_t c ) { return index[ c ]; };
    for ( ; left >= 2; at += 4, left -= 4 )
    {
        const std::int8_t a = of( at[ 0 ] );
        if ( a < 0 )
            break;
        const std::int8_t b = of( at[ 1 ] );
        if ( b < 0 )
            break;
        emit( static_cast< std::uint8_t >( ( a * 4 ) | ( b >> 4 ) ) );
        if ( left == 2 )
            break;
        if ( at[ 2 ] == '=' )
        {
            if ( left != 4 || at[ 3 ] != '=' )
                break;
            continue;
        }
        const std::int8_t c = of( at[ 2 ] );
        if ( c < 0 )
            break;
        emit( static_cast< std::uint8_t >( ( c >> 2 ) | ( b * 16 ) ) );
        if ( left == 3 )
            break;
        if ( at[ 3 ] == '=' )
        {
            if ( left != 4 )
                break;
            continue;
        }
        const std::int8_t d = of( at[ 3 ] );
        if ( d < 0 )
            break;
        emit( static_cast< std::uint8_t >( d | ( c << 6 ) ) );
    }
    if ( static_cast< std::int64_t >( put ) == size )
        return true;
    out.clear( );
    return false;
}

// What 0x4AFA30 reads of the boat each sample. position = body +0x80, rotation = body +0xA0; boost = boat +0x310;
// flag_2 = boat +0x32C; drive = boat[ +0x220 ] slot 25 ( +0x64 ); flag_8 = boat +0x464; flag_16 = boat[ +0x238 ] +0x94.
struct ghost_source_t
{
    float position[ 3 ]{};
    float rotation[ 3 ]{};
    float boost = 0.0f;
    bool flag_2 = false;
    float drive = 0.0f;
    bool flag_8 = false;
    bool flag_16 = false;
};

// VuGhostManager's recording (+4 the record, +0x5C the elapsed time, +0x60 the sample clock).
class c_ghost_recorder
{
public:
    ghost_record_t record;
    float elapsed = 0.0f;
    float clock = 0.0f;

    // 0x4AF520's part here: the names (boat +0x8C, boat +0xA4, the driver), 2048 samples reserved, both clocks zero. The tick
    // registration ("Final") is the caller's.
    void begin( const std::string& boat, const std::string& skin, const std::string& driver )
    {
        record.boat = boat;
        record.skin = skin;
        record.driver = driver;
        record.samples.reserve( 2048 );
        elapsed = 0.0f;
        clock = 0.0f;
    }

    // 0x4AFA30: one sample more. The rotation words: fistp (round to nearest) of the product, low 16 bits.
    void sample( const ghost_source_t& boat )
    {
        ghost_sample_t s;
        s.flags = boat.boost > 0.0f ? 1 : 0;
        if ( boat.flag_2 )
            s.flags |= 2;
        if ( boat.drive > 0.25f )
            s.flags |= 4;
        if ( boat.flag_8 )
            s.flags |= 8;
        if ( boat.flag_16 )
            s.flags |= 0x10;
        s.time = elapsed;
        std::memcpy( s.position, boat.position, sizeof( s.position ) );
        for ( int k = 0; k < 3; ++k )
            s.rotation[ k ] = static_cast< std::int16_t >( _mm_cvtss_si32( _mm_set_ss( boat.rotation[ k ] * 10424.6484f ) ) );
        record.samples.push_back( s );
    }

    // 0x4AF9C0 ( dt ): up to 300 s of 15 Hz; the clock keeps the remainder, by repeated subtraction.
    void tick( float dt, const ghost_source_t& boat )
    {
        if ( !( 300.0f > elapsed ) )
            return;
        elapsed = elapsed + dt;
        clock = clock + dt;
        const float step = 0.06666667f;
        if ( clock >= step )
        {
            sample( boat );
            float rest = clock;
            if ( rest >= step )
            {
                do
                    rest = rest - step;
                while ( rest >= step );
                clock = rest;
            }
        }
    }
};

// The title storage's answer to a GET (0x4A9140): 0 the body (HTTP 200), 1 no request / signed out, 2 another status. The
// local store: a file there 0, none 2 (a player with no ghost yet is retail's 404).
enum : int
{
    title_ok = 0,
    title_failed = 1,
    title_missing = 2,
};

// Where a title storage blob lives: <root>/titlestorage/<xuid>/<name>. The local player's xuid is 0.
inline std::string title_path( std::uint64_t xuid, const std::string& name )
{
    return "titlestorage/" + std::to_string( xuid ) + "/" + name;
}

// Slot 6 0x4A8850's answer, at once instead of on the next "Network" tick.
inline int title_load( std::uint64_t xuid, const std::string& name, std::string& text )
{
    text.clear( );
    save_seam::file_t* file = save_seam::open( title_path( xuid, name ), 0 );
    if ( file == nullptr )
        return title_missing;
    const std::uint32_t size = save_seam::size( file );
    text.resize( size );
    const std::uint32_t got = save_seam::read( file, text.data( ), size );
    save_seam::close( file );
    if ( got != size )
    {
        text.clear( );
        return title_failed;
    }
    return title_ok;
}

// Slot 7 0x4A8C40's body, the PUT; true on a whole write.
inline bool title_store( std::uint64_t xuid, const std::string& name, const std::string& text )
{
    save_seam::file_t* file = save_seam::open( title_path( xuid, name ), 1 );
    if ( file == nullptr )
        return false;
    const std::uint32_t size = static_cast< std::uint32_t >( text.size( ) );
    const bool ok = save_seam::write( file, text.data( ), size ) == size;
    save_seam::close( file );
    return ok;
}

// VuGhostGame's ghost list, +0x75C (0x28-byte entries, 0x487750): xuid, gamer tag, +0x20 loading, +0x21 failed, +0x22 loaded.
struct ghost_entry_t
{
    std::uint64_t xuid = 0;
    std::string name;
    bool loading = true;
    bool failed = false;
    bool loaded = false;
};

struct ghost_loads_t
{
    std::vector< ghost_entry_t > entries;
    std::string message;                                                       // VuGhostGame +0x740

    // 0x4AF640 ( index, blob ): make the ghost's VuBoatEntity (world lane). Its first step is ghost_read; false: no ghost.
    std::function< bool( int, const std::vector< std::uint8_t >& ) > add_ghost;
    // The FSM's "Success" / "Failure" condition (byte +0x1D of the transition with that name's hash).
    std::function< void( const char* ) > condition;
};

// 0x485680 ( status, body, index ), the title storage's LoadCallback: 1 marks the entry failed; 0 decodes and adds the
// ghost, marking it loaded; any other status does neither. The entry is no longer loading; once none is: "Success", unless
// every entry failed (or there are none): then "Ghost_NotConnected" and "Failure".
inline void ghost_on_load( ghost_loads_t& loads, int status, const std::string& body, int index )
{
    if ( status == title_failed )
        loads.entries[ index ].failed = true;
    else if ( status == title_ok )
    {
        std::vector< std::uint8_t > blob;
        if ( save_unbase64( body, blob ) && loads.add_ghost( index, blob ) )
            loads.entries[ index ].loaded = true;
    }
    loads.entries[ index ].loading = false;
    bool any_loading = false, all_failed = true;
    for ( const ghost_entry_t& entry : loads.entries )
    {
        any_loading = any_loading || entry.loading;
        all_failed = all_failed && entry.failed;
    }
    if ( any_loading )
        return;
    if ( all_failed )
    {
        loads.message = "Ghost_NotConnected";
        loads.condition( "Failure" );
    }
    else
    {
        loads.condition( "Success" );
    }
}

// THE LOCAL ANSWER TO LoadGhosts 0x485B50's request loop: per entry, in order, slot 6 on "ghost/<key>" (`key` = the game's
// +0x68 text). The world agent calls this where 0x485B50's GET loop stands, after its list (the local player first
// (0x487750), then the leaderboard rows that are not the player: none here).
inline void ghost_load_all( ghost_loads_t& loads, const std::string& key )
{
    const std::string name = "ghost/" + key;
    for ( std::size_t k = 0; k < loads.entries.size( ); ++k )
    {
        std::string text;
        const int status = title_load( loads.entries[ k ].xuid, name, text );
        ghost_on_load( loads, status, text, static_cast< int >( k ) );
    }
}

// 0x486EC0's PUT (post-game, a new best time): the record's blob as text, slot 7 on "ghost/<key>".
inline bool ghost_store_best( const ghost_record_t& record, std::uint64_t xuid, const std::string& key )
{
    std::vector< std::uint8_t > blob;
    ghost_write( record, blob );
    return title_store( xuid, "ghost/" + key, save_base64( blob.data( ), static_cast< std::uint32_t >( blob.size( ) ) ) );
}
