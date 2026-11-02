#pragma once

#include <workspace/core/ui/config.hxx>
#include <workspace/core/ui/select.hxx>
#include <workspace/core/ui/split.hxx>

#include <algorithm>
#include <bit>
#include <cfloat>
#include <climits>
#include <cstdint>
#include <functional>
#include <string>

// VuProgressManagerImpl's race records (vtable 0x74590C, g 0x758FD8, slots 5 .. 21, 30, 31) and VuHiddenPackageManagerImpl (vtable
// 0x7456D8, g 0x758FD0, slots 6 .. 12) on the save document, VuCloudSaveManager +0x20 (world batch 38). Disassembled; verified by
// case progress_race. The championship slots 22 .. 29 and 32, 33 are the UI's (core/ui/progress.hxx).
//   Progress [ "Race" | "TimeTrial" | "Slalom" ] [ track ] [ "None" | course ] [ table ] [ key ] = { Place, TotalTime, BestLap }
// `key` = the boat's class (boat +0x160). `table` = the static table 0x736718 (initialised at 0x64B3C0's function) by the index every
// caller passes, 0 today.

inline const char* record_table( std::int32_t index )
{
    static const char* const table[ 3 ] = { "SinglePlayer", "LocalMultiplayer", "OnlineMultiplayer" };
    return table[ index ];
}

// A number's Place: an int, a float (cvttss2si), a tag 7's low word. `other` for any other tag.
inline std::int32_t record_int( const value_t& v, std::int32_t other )
{
    return v.tag == 1 || v.tag == 2 || v.tag == 7 ? ui_json_as_int( v ) : other;
}

inline bool record_float( const value_t& v, float& out )
{
    return ui_json_float( v, out );
}

// 0x4B4100's first four levels.
inline const value_t& record_node( const value_t& document, const char* type, const std::string& track, const std::string& course, std::int32_t index )
{
    return ui_json_find( ui_json_find( ui_json_find( ui_json_find( ui_json_find( document, "Progress" ), type ), track ), course ), record_table( index ) );
}

// Slots 5 0x4B1930 ("Race"), 7 0x4B1C70 ("TimeTrial"), 9 0x4B1FB0 ("Slalom"): 0x4B4100's record; false when absent, the outs untouched.
// Absent numbers answer INT_MAX / FLT_MAX. All three read under "None": slot 9 takes a course and never uses it, so a slalom record
// (written under its course by slot 15) is not found here: the writer's old-record step below never sees one.
inline bool record_get( const value_t& document, const char* type, const std::string& track, const std::string& key, std::int32_t index,
                        std::int32_t& place, float& total, float& lap )
{
    const value_t& node = ui_json_find( record_node( document, type, track, "None", index ), key );
    if ( node.tag == 0 )
        return false;
    place = record_int( ui_json_find( node, "Place" ), INT_MAX );
    if ( !record_float( ui_json_find( node, "TotalTime" ), total ) )
        total = FLT_MAX;
    if ( !record_float( ui_json_find( node, "BestLap" ), lap ) )
        lap = FLT_MAX;
    return true;
}

// Slot 12 0x4B22A0 ( type, track, course, index, .. ): the bests over every key of that node, each number on its own; true and the outs
// when any number beat its start (FLT_MAX, FLT_MAX, INT_MAX). Slots 6 0x4B1B70, 8 0x4B1EB0, 10 0x4B21F0 pass their type.
inline bool record_best( const value_t& document, const char* type, const std::string& track, const std::string& course, std::int32_t index,
                         std::int32_t& place, float& total, float& lap )
{
    const value_t& node = record_node( document, type, track, course, index );
    float best_total = FLT_MAX, best_lap = FLT_MAX;
    std::int32_t best_place = INT_MAX;
    bool any = false;
    for ( const std::string& key : ui_json_keys( node ) )
    {
        const value_t& entry = ui_json_find( node, key );
        float v = 0.0f;
        if ( record_float( ui_json_find( entry, "TotalTime" ), v ) && best_total > v )
        {
            best_total = v;
            any = true;
        }
        if ( record_float( ui_json_find( entry, "BestLap" ), v ) && best_lap > v )
        {
            best_lap = v;
            any = true;
        }
        const value_t& number = ui_json_find( entry, "Place" );
        if ( ( number.tag == 1 || number.tag == 2 || number.tag == 7 ) && ui_json_as_int( number ) < best_place )
        {
            best_place = ui_json_as_int( number );
            any = true;
        }
    }
    if ( !any )
        return false;
    total = best_total;
    lap = best_lap;
    place = best_place;
    return true;
}

// Slot 11 0x4B2540 ( type, track, course, .. ): slot 12 on tables 0, 1, 2 from (INT_MAX, FLT_MAX, FLT_MAX), the smallest of each.
inline bool record_best_all( const value_t& document, const char* type, const std::string& track, const std::string& course,
                             std::int32_t& place, float& total, float& lap )
{
    std::int32_t p = INT_MAX;
    float t = FLT_MAX, l = FLT_MAX;
    bool any = false;
    for ( std::int32_t k = 0; k < 3; ++k )
    {
        std::int32_t one_place = INT_MAX;
        float one_total = FLT_MAX, one_lap = FLT_MAX;
        any = record_best( document, type, track, course, k, one_place, one_total, one_lap ) || any;
        p = one_place < p ? one_place : p;
        t = one_total < t ? one_total : t;
        l = one_lap < l ? one_lap : l;
    }
    if ( !any )
        return false;
    place = p;
    total = t;
    lap = l;
    return true;
}

// Progress [ name ] [ table ] + 1 (0x4FC090 and 0x4FCAB0: both made, whatever they held replaced; asInt; a tag 1 written).
inline void record_bump( value_t& progress, const char* name, const char* table )
{
    value_t& slot = json_member( ui_json_child( progress, name ), table );
    const std::uint32_t held = static_cast< std::uint32_t >( ui_json_as_int( slot ) );
    ui_json_set_int( slot, static_cast< std::int32_t >( held + 1u ) );
}

// Slots 13 0x4B2710 ("Race"), 14 0x4B2AF0 ("TimeTrial"), 15 0x4B2ED0 ("Slalom", its course): the count; a place of 2 / 1 / 0 its medal;
// the old record through slot 5 / 7 / 9 keeps the smaller of each number (cmovl, minss old, new: an equal or unordered pair takes the new).
inline void record_put( value_t& document, const char* type, const std::string& track, const std::string& course, const std::string& key, std::int32_t index,
                        std::int32_t place, float total, float lap )
{
    value_t& progress = ui_json_child( document, "Progress" );
    const char* table = record_table( index );
    const std::string all = std::string( "AllTime" ) + type;
    record_bump( progress, ( all + "Count" ).c_str( ), table );
    if ( place == 2 )
        record_bump( progress, ( all + "Bronzes" ).c_str( ), table );
    else if ( place == 1 )
        record_bump( progress, ( all + "Silvers" ).c_str( ), table );
    else if ( place == 0 )
        record_bump( progress, ( all + "Golds" ).c_str( ), table );
    std::int32_t old_place = 0;
    float old_total = 0.0f, old_lap = 0.0f;
    if ( record_get( document, type, track, key, index, old_place, old_total, old_lap ) )
    {
        place = old_place < place ? old_place : place;
        total = old_total < total ? old_total : total;
        lap = old_lap < lap ? old_lap : lap;
    }
    value_t& record = json_member( json_member( json_member( json_member( ui_json_child( progress, type ), track ), course ), table ), key );
    ui_json_set_int( ui_json_child( record, "Place" ), place );
    json_set_float( ui_json_child( record, "TotalTime" ), total );
    json_set_float( ui_json_child( record, "BestLap" ), lap );
}

// Slots 16 0x4B32C0, 17 0x4B3370, 18 0x4B3420: AllTime<type>Count over the three tables.
inline std::int32_t record_count( const value_t& document, const char* type )
{
    const value_t& node = ui_json_find( ui_json_find( document, "Progress" ), ( std::string( "AllTime" ) + type + "Count" ).c_str( ) );
    std::uint32_t sum = 0;
    for ( std::int32_t k = 0; k < 3; ++k )
        sum += static_cast< std::uint32_t >( ui_json_as_int( ui_json_find( node, record_table( k ) ) ) );
    return static_cast< std::int32_t >( sum );
}

// Slots 19 0x4B34D0, 20 0x4B3660, 21 0x4B37F0.
inline void record_medals( const value_t& document, const char* type, std::int32_t index, std::int32_t& gold, std::int32_t& silver, std::int32_t& bronze )
{
    const value_t& progress = ui_json_find( document, "Progress" );
    const std::string all = std::string( "AllTime" ) + type;
    gold = ui_json_as_int( ui_json_find( ui_json_find( progress, all + "Golds" ), record_table( index ) ) );
    silver = ui_json_as_int( ui_json_find( ui_json_find( progress, all + "Silvers" ), record_table( index ) ) );
    bronze = ui_json_as_int( ui_json_find( ui_json_find( progress, all + "Bronzes" ), record_table( index ) ) );
}

// Slot 31 0x4B3F90: Progress.Ghost [ track ] a number: true and `time`.
inline bool record_ghost_get( const value_t& document, const std::string& track, float& time )
{
    return record_float( ui_json_find( ui_json_find( ui_json_find( document, "Progress" ), "Ghost" ), track ), time );
}

// Slot 30 0x4B3E10 ( track, place, time ): AllTimeGhostCount + 1, a place of 0 AllTimeGhostWins + 1, the smaller time kept (minss old, new).
inline void record_ghost_put( value_t& document, const std::string& track, std::int32_t place, float time )
{
    value_t& progress = ui_json_child( document, "Progress" );
    const auto bump = [ &progress ]( const char* key ) {
        const std::uint32_t held = static_cast< std::uint32_t >( ui_json_as_int( ui_json_child( progress, key ) ) );
        ui_json_set_int( ui_json_child( progress, key ), static_cast< std::int32_t >( held + 1u ) );
    };
    bump( "AllTimeGhostCount" );
    if ( place == 0 )
        bump( "AllTimeGhostWins" );
    float old = 0.0f;
    if ( record_ghost_get( document, track, old ) )
        time = old < time ? old : time;
    json_set_float( json_member( ui_json_child( progress, "Ghost" ), track ), time );
}

// ---- VuHiddenPackageManagerImpl: root [ "PackagesFound" ] [ name ] = true; HiddenPackagesDB's VuDBAsset.PackagesInLevel [ level ] -------

// Slot 7 0x4AFFA0.
inline bool package_is_found( const value_t& document, const std::string& name )
{
    const value_t& v = ui_json_find( ui_json_find( document, "PackagesFound" ), name );
    return v.tag == 3 && v.integer != 0;
}

// Slot 8 0x4B0000 ( level, out ): out = 0, then the number: true.
inline bool package_in_level( const value_t& db, const std::string& level, std::int32_t& out )
{
    out = 0;
    const value_t& v = ui_json_find( ui_json_find( ui_json_find( db, "VuDBAsset" ), "PackagesInLevel" ), level );
    if ( v.tag != 1 && v.tag != 2 && v.tag != 7 )
        return false;
    out = ui_json_as_int( v );
    return true;
}

// Slot 9 0x4B0090 ( level, out ): the found packages whose name starts with `level` (0x43E030: its first find is at 0).
inline void package_found_in_level( const value_t& document, const std::string& level, std::int32_t& out )
{
    out = 0;
    const value_t& found = ui_json_find( document, "PackagesFound" );
    for ( const std::string& key : ui_json_keys( found ) )
        if ( key.find( level ) == 0 && package_is_found( document, key ) )
            ++out;
}

// Slot 10 0x4B0140 ( out ).
inline void package_found_total( const value_t& document, std::int32_t& out )
{
    out = 0;
    const value_t& found = ui_json_find( document, "PackagesFound" );
    for ( const std::string& key : ui_json_keys( found ) )
        if ( package_is_found( document, key ) )
            ++out;
}

// Slot 11 0x4B01D0 ( level ): every package of the level found (signed >=); a level with no count: false.
inline bool package_all_found( const value_t& document, const value_t& db, const std::string& level )
{
    std::int32_t count = 0, found = 0;
    if ( !package_in_level( db, level, count ) )
        return false;
    package_found_in_level( document, level, found );
    return found >= count;
}

// Slot 12 0x4B0250.
inline void package_clear( value_t& document )
{
    ui_json_erase( document, "PackagesFound" );
}

// Slot 6 0x4AFEF0 ( name ): marked, the game saved (0x4ABD60), then the achievement manager's progress on "Collector": 0x4AA1A0 with
// total x 100 x the float 0x6F75D0 (0x3C14F209) in xmm2.
inline void package_set_found( value_t& document, const std::string& name, const std::function< void( ) >& commit,
                               const std::function< void( const char*, float ) >& achievement )
{
    ui_json_set_bool( json_member( ui_json_child( document, "PackagesFound" ), name ), true );
    if ( commit )
        commit( );
    std::int32_t total = 0;
    package_found_total( document, total );
    if ( achievement )
        achievement( "Collector", static_cast< float >( static_cast< std::int32_t >( static_cast< std::uint32_t >( total ) * 0x64u ) ) * std::bit_cast< float >( 0x3C14F209u ) );
}
