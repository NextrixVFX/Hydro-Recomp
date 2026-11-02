#pragma once

#include <workspace/core/save/ghost.hxx>
#include <workspace/core/ui/config.hxx>
#include <workspace/core/world/ghost.hxx>
#include <workspace/core/world/rules.hxx>

#include <cfloat>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <emmintrin.h>

// VuGhostGame's state handlers past LoadGhosts (ctor 0x484A20; states: rules.hxx's table), the parts that are not the FSM's or a
// project's. Boat bytes as modes.hxx; the ghost list's entries (+0x75C) are save/ghost.hxx's ghost_entry_t. NOT WIRED: see
// ghost_load_all there and HANDOFF. Hooks are the service's (leaderboard, progress, voice, achievement) or the tick manager's.

// A ghost's record in the manager (+0x64): what the handlers read of it.
struct ghost_game_record_t
{
    int index = 0;                         // +0
    std::int32_t frames = 0;               // +0x50
    float last_time = 0.0f;                // the last frame's +0
    const std::uint8_t* boat = nullptr;    // +0x58's image
    race_results_entry_t entry;
};

struct ghost_game_t
{
    std::uint8_t circuit = 0;              // +0x44
    std::string track;                     // +0x68
    std::int32_t boats = 0;                // the boat manager's +0x1C
    const std::uint8_t* human = nullptr;   // its +0x24 list [0]
    race_results_entry_t human_entry;
    std::vector< ghost_entry_t > entries;
    std::vector< ghost_game_record_t > records;
};

// A record's time: the last frame's, 0 with no frames.
inline float ghost_game_time( const ghost_game_record_t& r )
{
    return r.frames != 0 ? r.last_time : 0.0f;
}

inline const ghost_game_record_t* ghost_game_find( const ghost_game_t& g, int index )
{
    for ( const ghost_game_record_t& r : g.records )
        if ( r.index == index )
            return &r;
    return nullptr;
}

// 0x486060's table (PreGame enter, past the project's start and the boats' hold): std::map< float, name > (0x487A00: a loaded ghost's
// time; an equal time overwrites the name), ascending, into gameutil's root["GhostTable"]: { Name, TotalTime }.
inline bool ghost_game_table( value_t& util_root, const ghost_game_t& g )
{
    std::map< float, std::string > order;
    for ( std::size_t k = 0; k < g.entries.size( ); ++k )
    {
        if ( !g.entries[ k ].loaded )
            continue;
        const ghost_game_record_t* r = ghost_game_find( g, static_cast< int >( k ) );
        if ( r == nullptr )
            return false;   // retail reads through the null: UNKNOWN
        order[ ghost_game_time( *r ) ] = g.entries[ k ].name;
    }
    value_t& table = ui_json_child( util_root, "GhostTable" );
    for ( const auto& [ time, name ] : order )
    {
        if ( table.tag != 5 )
        {
            ui_json_clear( table );
            table.tag = 5;
            table.kind = value_t::kind_t::list;
        }
        table.list.emplace_back( );
        value_t& row = table.list.back( );
        ui_json_set_string( ui_json_child( row, "Name" ), name.c_str( ) );
        race_results_set_float( ui_json_child( row, "TotalTime" ), time );
    }
    return true;
}

// 0x486660 (PreGame exit): the table erased (0x4FC140).
inline void ghost_game_table_erase( value_t& util_root )
{
    ui_json_erase( util_root, "GhostTable" );
}

// 0x486CE0 ( time in xmm2, boat ): the boat's row of GameResults.Table, at its place.
inline bool ghost_game_row( value_t& util_root, const std::uint8_t* b, const race_results_entry_t& e, float time )
{
    const std::int32_t place = rules_i32( b, 0x4CC );
    if ( place < 0 )
        return false;   // 0x4FBC60 on a negative index: UNKNOWN
    value_t& row = ui_json_at( ui_json_child( ui_json_child( util_root, "GameResults" ), "Table" ), static_cast< std::size_t >( place ) );
    ui_json_set_string( ui_json_child( row, "DriverName" ), e.driver_name.c_str( ) );
    race_results_set_float( ui_json_child( row, "BestLapTime" ), rules_f( b, 0x4B8 ) );
    race_results_set( ui_json_child( row, "LapsCompleted" ), 1, rules_i32( b, 0x4D4 ) );
    race_results_set_float( ui_json_child( row, "TotalTime" ), time );
    race_results_set( ui_json_child( row, "IsLocal" ), 3, e.local ? 1 : 0 );
    race_results_set( ui_json_child( row, "IsHuman" ), 3, e.human ? 1 : 0 );
    ui_json_set_string( ui_json_child( row, "BoatClass" ), e.boat_class.c_str( ) );
    race_results_set( ui_json_child( row, "IsFinished" ), 3, 1 );
    ui_json_set_string( ui_json_child( row, "Boat" ), e.boat_name.c_str( ) );
    race_results_set( ui_json_child( row, "TierAchieved" ), 1, place );
    return true;
}

// 0x486880 (Game exit), past the project's stop: a finished human writes GameResults (CircuitRace, a table of every boat, the human's
// row, a row per ghost found); +0x334 clear and some ghost past the first placed behind the human: the achievement "Frenemy". Then the
// caller unregisters the ghost tick (+0x28) and takes the last frame (0x4AFA30): always, finished or not.
inline bool ghost_game_end( value_t& util_root, const ghost_game_t& g, const std::function< void( const char* ) >& achievement )
{
    if ( !g.human[ 0x32F ] )
        return true;
    value_t& results = ui_json_child( util_root, "GameResults" );
    ui_json_clear( results );
    race_results_set( ui_json_child( results, "CircuitRace" ), 3, g.circuit );
    {
        value_t& table = ui_json_child( results, "Table" );
        if ( table.tag != 5 )
        {
            ui_json_clear( table );
            table.tag = 5;
            table.kind = value_t::kind_t::list;
        }
        table.list.resize( static_cast< std::size_t >( g.boats ) );
    }
    if ( !ghost_game_row( util_root, g.human, g.human_entry, rules_f( g.human, 0x4B4 ) ) )
        return false;
    for ( std::size_t k = 0; k < g.entries.size( ); ++k )
    {
        const ghost_game_record_t* r = ghost_game_find( g, static_cast< int >( k ) );
        if ( r != nullptr && !ghost_game_row( util_root, r->boat, r->entry, ghost_game_time( *r ) ) )
            return false;
    }
    if ( !g.human[ 0x334 ] )
    {
        std::int32_t worst = 0;
        for ( std::size_t k = 1; k < g.entries.size( ); ++k )
        {
            const ghost_game_record_t* r = ghost_game_find( g, static_cast< int >( k ) );
            if ( r != nullptr && rules_i32( r->boat, 0x4CC ) > worst )
                worst = rules_i32( r->boat, 0x4CC );
        }
        if ( worst > rules_i32( g.human, 0x4CC ) && achievement )
            achievement( "Frenemy" );
    }
    return true;
}

struct ghost_game_post_t
{
    std::function< void( const std::string& track, std::int32_t place, float time ) > progress;   // g 0x758FD8 vt+0x78
    std::function< void( const std::vector< std::uint8_t >& blob ) > store;                       // the PUT: ghost_store_best
    std::function< void( const std::string& name, std::int64_t score ) > leaderboard;             // g 0x758FA8 0x4B0650
    std::function< void( const char* line ) > voice;                                              // g 0x758FEC vt+0x20
};

// 0x486EC0 (PostGame enter), past the project's start and "OnSetAllowPause" false: with a human boat: progress( the track, its place,
// its time ); not disqualified: when the player's own ghost (index 0: FLT_MAX when there is none) is slower, the recording stored
// (0x4AFB90, 0x558300, title storage slot 7, "ghost/<track>"); the score -fistp( time * 1000 ) to the board "<track>Ghost". Then, placed behind
// with boost left (+0x328 > 0) and +0x32D clear, the voice line "DidNotUseBoost".
inline void ghost_game_post( const ghost_game_t& g, std::size_t humans, const ghost_data_t& recording, const ghost_game_post_t& h )
{
    if ( humans == 0 )
        return;
    const std::uint8_t* b = g.human;
    const float time = rules_f( b, 0x4B4 );
    const std::int32_t place = rules_i32( b, 0x4CC );
    if ( h.progress )
        h.progress( g.track, place, time );
    if ( !b[ 0x334 ] )
    {
        float best = FLT_MAX;
        if ( const ghost_game_record_t* own = ghost_game_find( g, 0 ) )
            best = ghost_game_time( *own );
        if ( best > time )
        {
            std::vector< std::uint8_t > blob;
            ghost_write( recording, blob );
            if ( h.store )
                h.store( blob );
        }
        const float ms = time * 1000.0f;
        const std::int64_t score = -static_cast< std::int64_t >( _mm_cvtss_si32( _mm_set_ss( ms ) ) );
        if ( h.leaderboard )
            h.leaderboard( g.track + "Ghost", score );
    }
    if ( place > 0 && rules_f( b, 0x328 ) > 0.0f && !b[ 0x32D ] && h.voice )
        h.voice( "DidNotUseBoost" );
}
