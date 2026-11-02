#pragma once

#include <workspace/core/save/seam.hxx>
#include <workspace/core/save/writer.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// The ghost store, local (the user's Saves decision): the text form a ghost blob travels in, VuMetroTitleStorageManager's GET /
// PUT (slots 6 / 7 0x4A8850 / 0x4A8C40, HTTP to Xbox title storage in retail) answered from local files through the one seam,
// and VuGhostGame's load callback. The blob itself and the recorder are hydro's world/ghost.hxx (world batch 31): ghost_write
// 0x4AFB90, ghost_read 0x4AFCE0, ghost_tick 0x4AF9C0.

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

// 0x486EC0's PUT (post-game, a new best time): `blob` = ghost_write of the record, as text, slot 7 on "ghost/<key>".
inline bool ghost_store_best( const std::vector< std::uint8_t >& blob, std::uint64_t xuid, const std::string& key )
{
    return title_store( xuid, "ghost/" + key, save_base64( blob.data( ), static_cast< std::uint32_t >( blob.size( ) ) ) );
}
