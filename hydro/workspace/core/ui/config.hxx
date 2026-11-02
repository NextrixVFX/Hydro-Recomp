#pragma once

#include <workspace/core/assets/tree.hxx>
#include <workspace/core/entity/json.hxx>
#include <workspace/core/boat/fleet.hxx>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <xmmintrin.h>

// The front end's GameData: what VuFrontEndGameMode's exit (vtable 0x746204
// slot 3, 0x4BF9B0) writes into gameutil +80 ["GameData"] for the action game
// mode (VuActionGameMode's load 0x4BA060 reads it; VuGame's boat load 0x481CE0
// makes one boat per GameData["Boats"][k]). Transliterated from the
// disassembly:
//   0x4BF9B0  the exit's data step (its teardown half is c_front_end::exit)
//   0x4C0A50  the race builder ("Race"), 0x4C18D0 the slalom's ("Slalom"),
//             0x4C1E30 the time trial's ("TimeTrial"), 0x4C2300 the
//             championship's ("Championship"), 0x4C2C50 the ghost's ("Ghost")
//   0x4C3380  an AI pick by class, over the shuffle 0x50FF40
//   0x4C31D0  an AI boat's entry (its skin drawn from BoatDB's AISkins)
//   0x4C3120  the human player's entry
// and the VuJsonContainer calls they make (0x4FBB10 clear, 0x4FC6B0 make a
// map, 0x4FC090 member-or-insert, 0x4FC640 set a string, 0x4FBC60 element-or-
// grow, 0x4FC140 erase, 0x4FBDF0 / 0x4F22B0 lookups, 0x4FBE90 / 0x4FC000 the
// map's size and its i-th key in std::map order).

// ---------------------------------------------------------------------------
// VuJsonContainer mutation over value_t (+0 tag, +8 payload in retail).

// 0x4FBB10: the payload freed, tag 0 (+8 / +0xC zeroed). Batch 25: one copy -
// forwards to engine entity/json.hxx (verified there on retail's containers).
inline void ui_json_clear( value_t& v ) { json_clear( v ); }

// 0x4FC6B0: anything that is not a map is cleared and becomes an empty one.
inline value_t& ui_json_make_map( value_t& v )
{
    if ( v.tag != 6 )
    {
        ui_json_clear( v );
        v.tag = 6;
        v.kind = value_t::kind_t::object;
    }
    return v;
}

// 0x4FC090 (the key through strlen) -> 0x4FC6B0, then the map's operator[]
// 0x4FC970: the member, a fresh null one inserted when absent. The reference
// lives until the parent map's next insertion.
// Forwards to engine entity/json.hxx json_member (batch 26: its full-key-scan fix
// from water batch 27; the batch-25 duplicate-key defect is gone).
inline value_t& ui_json_child( value_t& v, const char* key ) { return json_member( v, std::string( key ) ); }

// 0x4FC640: cleared, tag 4, the text up to its first NUL (strlen).
inline void ui_json_set_string( value_t& v, const char* text )
{
    ui_json_clear( v );
    v.tag = 4;
    v.kind = value_t::kind_t::text;
    v.text = text;
}

// 0x4FBC60(i): anything that is not an array is cleared and becomes an empty
// one; i >= size grows it to i + 1 (0x4FC880: null elements); element i.
inline value_t& ui_json_at( value_t& v, std::size_t i )
{
    if ( v.tag != 5 )
    {
        ui_json_clear( v );
        v.tag = 5;
        v.kind = value_t::kind_t::list;
    }
    if ( i >= v.list.size( ) )
        v.list.resize( i + 1 );
    return v.list[ i ];
}

// 0x4FC140: on a map, every member with that key erased; else nothing.
inline void ui_json_erase( value_t& v, const char* key )
{
    if ( v.tag != 6 )
        return;
    const std::string k( key );
    for ( std::size_t i = 0; i < v.object.size( ); )
    {
        if ( v.object[ i ].first == k )
            v.object.erase( v.object.begin( ) + static_cast< std::ptrdiff_t >( i ) );
        else
            ++i;
    }
}

// std::map::find 0x4F22B0 with a VuString key (no strlen: the whole string),
// node +40, else the static null container 0x7593A0 - also for a non-map.
inline const value_t& ui_json_find( const value_t& v, const std::string& key )
{
    static const value_t null{ };
    if ( v.tag != 6 )
        return null;
    for ( const auto& field : v.object )
        if ( field.first == key )
            return field.second;
    return null;
}

// A string member's text: tag 4, else the function-local static "" 0x819950.
inline const std::string& ui_json_text( const value_t& v )
{
    static const std::string empty;
    return v.tag == 4 ? v.text : empty;
}

// 0x4FBE90 / 0x4FC000: a map's keys in its std::map order (0 for a non-map).
inline std::vector< std::string > ui_json_keys( const value_t& v )
{
    std::vector< std::string > keys;
    if ( v.tag != 6 )
        return keys;
    for ( const auto& field : v.object )
        keys.push_back( field.first );
    std::sort( keys.begin( ), keys.end( ) );
    return keys;
}

// ---------------------------------------------------------------------------
// What the builders read besides the containers they write.
struct ui_game_env_t
{
    // [g_vugameutil 0x758FF8 + 8] + 0x58 and [... + 16] + 0x58: BoatDB's and
    // TrackDB's document roots (gameutil's DB order, 0x4D3EB0's table 0x746D14).
    const value_t* boat_db = nullptr;
    const value_t* track_db = nullptr;
    // VuRandom's global 0x736810 (the draw 0x50FD20 = fleet_random_t::draw).
    fleet_random_t* random = nullptr;
    // HOOK (profile side): g_vuprogressmanagerimpl 0x758FD8 slot +108, the
    // current championship round's container (0x4C2300 only); null = the
    // static null container.
    const value_t* championship = nullptr;
    std::function< const value_t*( ) > championship_now;   // the same answer asked at 0x4C2300's own moment (the screen's unload writes it)
    // HOOK: VuStringDBImpl getString (g 0x759078 slot 6, thiscall(this, const
    // char* id) -> const std::string&) - the string tables are not recreated.
    std::function< std::string( const char* ) > string_db;
    // HOOK (platform sign-in): g_vumetrosigninmanager 0x758FB0 +20, the
    // signed-in player's name.
    std::string player_name;   // platform_gamer_tag( )
};

// 0x50FF40 (thiscall on the generator, n, int* out; ret 8): out = 0 .. n-1
// (its SSE fill writes the same values), then for k = 0 .. n-2 one inlined
// draw r (= 0x50FD20's) and j = k + cvttss2si( (float)n x r ) % (n - k)
// (signed idiv; (float)n is computed once), out[k] and out[j] swapped.
inline void ui_shuffle( fleet_random_t& rng, int n, int* out )
{
    for ( int i = 0; i < n; ++i )
        out[ i ] = i;
    if ( n - 1 <= 0 )
        return;
    const float count = static_cast< float >( n );
    int left = n;
    for ( int k = 0; k < n - 1; ++k )
    {
        const float r = rng.draw( );
        const int t = _mm_cvtt_ss2si( _mm_set_ss( count * r ) );
        const int j = k + t % left;
        const int held = out[ k ];
        out[ k ] = out[ j ];
        out[ j ] = held;
        --left;
    }
}

// 0x4C3380 (stdcall count, const std::vector<VuString>* boats, std::vector<
// VuString>* picked; ret 0xC): a shuffle of boats.size() (ALWAYS run - its
// draws happen even for count 0), then picked gets boats[order[i % size]] for
// i < count. Retail divides by zero (a fault) when count > 0 and the class has
// no boats: false then, nothing picked.
inline bool ui_pick( fleet_random_t& rng, int count, const std::vector< std::string >& boats, std::vector< std::string >& picked )
{
    const int n = static_cast< int >( boats.size( ) );
    std::vector< int > order( static_cast< std::size_t >( n ) );
    ui_shuffle( rng, n, order.data( ) );
    if ( count > 0 && n == 0 )
        return false;
    for ( int i = 0; i < count; ++i )
        picked.push_back( boats[ static_cast< std::size_t >( order[ static_cast< std::size_t >( i % n ) ] ) ] );
    return true;
}

// 0x4C31D0 (entry, const char* boat): BoatDB "VuDBAsset" / boat / "AISkins"
// (lookups by strlen key), n = its size when an array (else 0); one draw r
// (always); i = fistp( fld( r x (float)n ) - 0.5f ) (x87, round to nearest),
// through cvtdq2ps + fistp again, then min( i, n - 1 ), max( i, 0 ); the skin =
// AISkins[i]'s text when that element exists, else "". Then Properties: Boat,
// Skin, "Driver Type" = "AI"; Driver: Name = "Ai Driver".
inline void ui_ai_entry( value_t& entry, const std::string& boat, const ui_game_env_t& env )
{
    static const value_t null{ };
    const value_t& db = env.boat_db ? member( *env.boat_db, "VuDBAsset" ) : null;
    const value_t& skins = member( member( db, boat.c_str( ) ), "AISkins" );
    const int n = skins.tag == 5 ? static_cast< int >( skins.list.size( ) ) : 0;
    const float r = env.random->draw( );
    const float scaled = r * static_cast< float >( n );
    int i = static_cast< int >( std::nearbyint( static_cast< double >( scaled ) - 0.5 ) );
    i = static_cast< int >( std::nearbyint( static_cast< double >( static_cast< float >( i ) ) ) );
    if ( n - 1 < i )
        i = n - 1;
    if ( i < 0 )
        i = 0;
    const std::string skin = skins.tag == 5 && i < static_cast< int >( skins.list.size( ) ) ? ui_json_text( skins.list[ static_cast< std::size_t >( i ) ] ) : std::string( );
    ui_json_set_string( ui_json_child( ui_json_child( entry, "Properties" ), "Boat" ), boat.c_str( ) );
    ui_json_set_string( ui_json_child( ui_json_child( entry, "Properties" ), "Skin" ), skin.c_str( ) );
    ui_json_set_string( ui_json_child( ui_json_child( entry, "Properties" ), "Driver Type" ), "AI" );
    ui_json_set_string( ui_json_child( ui_json_child( entry, "Driver" ), "Name" ), "Ai Driver" );
}

// 0x4C3120 (stdcall entry, const char* boat, const char* skin): Properties:
// Boat, Skin, "Driver Type" = "Human"; Driver: Name = the signed-in player's.
inline void ui_player_entry( value_t& entry, const char* boat, const char* skin, const ui_game_env_t& env )
{
    ui_json_set_string( ui_json_child( ui_json_child( entry, "Properties" ), "Boat" ), boat );
    ui_json_set_string( ui_json_child( ui_json_child( entry, "Properties" ), "Skin" ), skin );
    ui_json_set_string( ui_json_child( ui_json_child( entry, "Properties" ), "Driver Type" ), "Human" );
    ui_json_set_string( ui_json_child( ui_json_child( entry, "Driver" ), "Name" ), env.player_name.c_str( ) );
}

// 0x4C0A50 (stdcall VuJsonContainer* game_data; ret 4), the race. Reads, from
// gameutil +80 (`root`): GameConfig.Track; BoatConfig.PlayerBoats[0] (an array
// with an element, else the null container) .Properties.Boat / .Skin; the
// boat's BoatDB Class; the track's TrackDB ProjectAsset. Writes GameData:
// GameType "Race", Track, Level (= ProjectAsset), Difficulty ("Normal"; class
// exactly "A" -> "Hard", "C" -> "Easy"); then the AI field: every BoatDB boat
// filed under its Class (std::map order), C / B / A counts 4 / 7 / 4 (class C:
// 10 / 5 / 0, class A: 0 / 5 / 10) picked in that order (0x4C3380); Boats[k]
// = each pick's entry (0x4C31D0); one shuffle of 15 and Boats[k].Driver.Name =
// the string DB's "AI_Name%d" (order[k] + 1); Boats[picks] = the player's
// entry (0x4C3120); root.InitialScreen = "Screens/SinglePlayer". `game_data`
// must not live inside `root`'s top level unless it is root["GameData"] taken
// fresh by the caller (the InitialScreen insertion may move it; it is not
// touched after). False where retail faults (a class with no boats asked for
// some; more than 15 picks reads past its 15 names - unreachable with these
// counts).
inline bool ui_race_game_data( value_t& game_data, value_t& root, const ui_game_env_t& env )
{
    static const value_t null{ };
    const value_t& boats_db = env.boat_db ? member( *env.boat_db, "VuDBAsset" ) : null;
    const value_t& tracks_db = env.track_db ? member( *env.track_db, "VuDBAsset" ) : null;
    const std::string track = ui_json_text( member( member( root, "GameConfig" ), "Track" ) );
    const value_t& players = member( member( root, "BoatConfig" ), "PlayerBoats" );
    const value_t& player = players.tag == 5 && !players.list.empty( ) ? players.list[ 0 ] : null;
    const std::string boat = ui_json_text( member( member( player, "Properties" ), "Boat" ) );
    const std::string skin = ui_json_text( member( member( player, "Properties" ), "Skin" ) );
    const std::string cls = ui_json_text( member( ui_json_find( boats_db, boat ), "Class" ) );
    const std::string level = ui_json_text( member( ui_json_find( tracks_db, track ), "ProjectAsset" ) );
    std::string difficulty = "Normal";
    if ( cls == "A" )
        difficulty = "Hard";
    if ( cls == "C" )
        difficulty = "Easy";
    ui_json_set_string( ui_json_child( game_data, "GameType" ), "Race" );
    ui_json_set_string( ui_json_child( game_data, "Track" ), track.c_str( ) );
    ui_json_set_string( ui_json_child( game_data, "Level" ), level.c_str( ) );
    ui_json_set_string( ui_json_child( game_data, "Difficulty" ), difficulty.c_str( ) );
    int count_a = 4, count_c = 4, count_b = 7;
    if ( cls == "C" )
    {
        count_a = 0;
        count_c = 10;
        count_b = 5;
    }
    else if ( cls == "A" )
    {
        count_a = 10;
        count_c = 0;
        count_b = 5;
    }
    std::map< std::string, std::vector< std::string > > by_class;  // 0x4560F0's std::map, filled by 0x455820
    for ( const std::string& key : ui_json_keys( boats_db ) )
        by_class[ ui_json_text( member( ui_json_find( boats_db, key ), "Class" ) ) ].push_back( key );
    std::vector< std::string > picked;
    if ( !ui_pick( *env.random, count_c, by_class[ "C" ], picked ) || !ui_pick( *env.random, count_b, by_class[ "B" ], picked ) ||
         !ui_pick( *env.random, count_a, by_class[ "A" ], picked ) )
        return false;
    for ( std::size_t k = 0; k < picked.size( ); ++k )
        ui_ai_entry( ui_json_at( ui_json_child( game_data, "Boats" ), k ), picked[ k ], env );
    int names[ 15 ];
    ui_shuffle( *env.random, 15, names );
    if ( picked.size( ) > 15 )
        return false;
    for ( std::size_t k = 0; k < picked.size( ); ++k )
    {
        char id[ 256 ];
        std::snprintf( id, sizeof( id ), "AI_Name%d", names[ k ] + 1 );
        const std::string name = env.string_db ? env.string_db( id ) : std::string( );
        value_t& entry = ui_json_at( ui_json_child( game_data, "Boats" ), k );
        ui_json_set_string( ui_json_child( ui_json_child( entry, "Driver" ), "Name" ), name.c_str( ) );
    }
    ui_player_entry( ui_json_at( ui_json_child( game_data, "Boats" ), picked.size( ) ), boat.c_str( ), skin.c_str( ), env );
    ui_json_set_string( ui_json_child( root, "InitialScreen" ), "Screens/SinglePlayer" );
    return true;
}

// 0x4C18D0 (stdcall VuJsonContainer* game_data; ret 4), the slalom. Reads
// GameConfig.Track and .Course, PlayerBoats[0].Properties.Boat / .Skin, the
// boat's BoatDB Class and the track's TrackDB ProjectAsset (as the race); no
// AI and no draw. Writes GameData: GameType "Slalom", Track, Course, Level,
// Difficulty (the race's rule), Boats[0] = the player's entry (0x4C3120);
// root.InitialScreen = "Screens/SinglePlayer". 0x4C1E30, the time trial, is
// the same without the Course read and write, GameType "TimeTrial" (`slalom`
// false).
inline bool ui_slalom_game_data( value_t& game_data, value_t& root, const ui_game_env_t& env, bool slalom = true )
{
    static const value_t null{ };
    const value_t& boats_db = env.boat_db ? member( *env.boat_db, "VuDBAsset" ) : null;
    const value_t& tracks_db = env.track_db ? member( *env.track_db, "VuDBAsset" ) : null;
    const std::string track = ui_json_text( member( member( root, "GameConfig" ), "Track" ) );
    const std::string course = slalom ? ui_json_text( member( member( root, "GameConfig" ), "Course" ) ) : std::string( );
    const value_t& players = member( member( root, "BoatConfig" ), "PlayerBoats" );
    const value_t& player = players.tag == 5 && !players.list.empty( ) ? players.list[ 0 ] : null;
    const std::string boat = ui_json_text( member( member( player, "Properties" ), "Boat" ) );
    const std::string skin = ui_json_text( member( member( player, "Properties" ), "Skin" ) );
    const std::string cls = ui_json_text( member( ui_json_find( boats_db, boat ), "Class" ) );
    const std::string level = ui_json_text( member( ui_json_find( tracks_db, track ), "ProjectAsset" ) );
    std::string difficulty = "Normal";
    if ( cls == "A" )
        difficulty = "Hard";
    if ( cls == "C" )
        difficulty = "Easy";
    ui_json_set_string( ui_json_child( game_data, "GameType" ), slalom ? "Slalom" : "TimeTrial" );
    ui_json_set_string( ui_json_child( game_data, "Track" ), track.c_str( ) );
    if ( slalom )
        ui_json_set_string( ui_json_child( game_data, "Course" ), course.c_str( ) );
    ui_json_set_string( ui_json_child( game_data, "Level" ), level.c_str( ) );
    ui_json_set_string( ui_json_child( game_data, "Difficulty" ), difficulty.c_str( ) );
    ui_player_entry( ui_json_at( ui_json_child( game_data, "Boats" ), 0 ), boat.c_str( ), skin.c_str( ), env );
    ui_json_set_string( ui_json_child( root, "InitialScreen" ), "Screens/SinglePlayer" );
    return true;
}

// 0x4C2C50 (stdcall VuJsonContainer* game_data; ret 4), the ghost: the time
// trial's reads (GameConfig.Track, PlayerBoats[0].Properties.Boat / .Skin,
// BoatDB Class, TrackDB ProjectAsset, the same Difficulty rule) and writes
// with GameType "Ghost", then root.InitialScreen = "Screens/Main".
inline bool ui_ghost_game_data( value_t& game_data, value_t& root, const ui_game_env_t& env )
{
    static const value_t null{ };
    const value_t& boats_db = env.boat_db ? member( *env.boat_db, "VuDBAsset" ) : null;
    const value_t& tracks_db = env.track_db ? member( *env.track_db, "VuDBAsset" ) : null;
    const std::string track = ui_json_text( member( member( root, "GameConfig" ), "Track" ) );
    const value_t& players = member( member( root, "BoatConfig" ), "PlayerBoats" );
    const value_t& player = players.tag == 5 && !players.list.empty( ) ? players.list[ 0 ] : null;
    const std::string boat = ui_json_text( member( member( player, "Properties" ), "Boat" ) );
    const std::string skin = ui_json_text( member( member( player, "Properties" ), "Skin" ) );
    const std::string cls = ui_json_text( member( ui_json_find( boats_db, boat ), "Class" ) );
    const std::string level = ui_json_text( member( ui_json_find( tracks_db, track ), "ProjectAsset" ) );
    std::string difficulty = "Normal";
    if ( cls == "A" )
        difficulty = "Hard";
    if ( cls == "C" )
        difficulty = "Easy";
    ui_json_set_string( ui_json_child( game_data, "GameType" ), "Ghost" );
    ui_json_set_string( ui_json_child( game_data, "Track" ), track.c_str( ) );
    ui_json_set_string( ui_json_child( game_data, "Level" ), level.c_str( ) );
    ui_json_set_string( ui_json_child( game_data, "Difficulty" ), difficulty.c_str( ) );
    ui_player_entry( ui_json_at( ui_json_child( game_data, "Boats" ), 0 ), boat.c_str( ), skin.c_str( ), env );
    ui_json_set_string( ui_json_child( root, "InitialScreen" ), "Screens/Main" );
    return true;
}

// 0x4C2300 (stdcall VuJsonContainer* game_data; ret 4), the championship.
// The round's container is the progress manager's (g 0x758FD8 slot +108 -
// HOOK, profile side: env.championship). Reads that container's Name,
// PlayerBoats[0] (array with an element, else null) and AIBoats; from root
// GameConfig.GameType (NOT Mode), .Track, .Course, PlayerBoats[0].Properties
// .Boat (its Skin is looked up and dropped); BoatDB Class -> the Difficulty
// rule; TrackDB ProjectAsset. Writes GameData: IsChampionship = bool true,
// GameType, Track, Course, Level, Difficulty, Championship = the Name;
// Boats[0].Properties = a copy of the round's player Properties (0x4FB9B0),
// its "Driver Type" = "Human", Boats[0].Driver.Name = a copy of its
// DriverName; only for GameType exactly "Race": every AIBoats element
// appended (Boats made an array, element size()) the same way with "Driver
// Type" "AI". Then root.InitialScreen = "Screens/Championship".
inline bool ui_championship_game_data( value_t& game_data, value_t& root, const ui_game_env_t& env )
{
    static const value_t null{ };
    const value_t* const round = env.championship_now ? env.championship_now( ) : env.championship;
    const value_t& champ = round ? *round : null;
    const value_t& boats_db = env.boat_db ? member( *env.boat_db, "VuDBAsset" ) : null;
    const value_t& tracks_db = env.track_db ? member( *env.track_db, "VuDBAsset" ) : null;
    const std::string name = ui_json_text( member( champ, "Name" ) );
    const std::string type = ui_json_text( member( member( root, "GameConfig" ), "GameType" ) );
    const std::string track = ui_json_text( member( member( root, "GameConfig" ), "Track" ) );
    const std::string course = ui_json_text( member( member( root, "GameConfig" ), "Course" ) );
    const value_t& players = member( member( root, "BoatConfig" ), "PlayerBoats" );
    const value_t& player = players.tag == 5 && !players.list.empty( ) ? players.list[ 0 ] : null;
    const std::string boat = ui_json_text( member( member( player, "Properties" ), "Boat" ) );
    const std::string cls = ui_json_text( member( ui_json_find( boats_db, boat ), "Class" ) );
    const std::string level = ui_json_text( member( ui_json_find( tracks_db, track ), "ProjectAsset" ) );
    std::string difficulty = "Normal";
    if ( cls == "A" )
        difficulty = "Hard";
    if ( cls == "C" )
        difficulty = "Easy";
    value_t& is_champ = ui_json_child( game_data, "IsChampionship" );
    ui_json_clear( is_champ );
    is_champ.tag = 3;
    is_champ.kind = value_t::kind_t::integer;
    is_champ.integer = 1;
    ui_json_set_string( ui_json_child( game_data, "GameType" ), type.c_str( ) );
    ui_json_set_string( ui_json_child( game_data, "Track" ), track.c_str( ) );
    ui_json_set_string( ui_json_child( game_data, "Course" ), course.c_str( ) );
    ui_json_set_string( ui_json_child( game_data, "Level" ), level.c_str( ) );
    ui_json_set_string( ui_json_child( game_data, "Difficulty" ), difficulty.c_str( ) );
    ui_json_set_string( ui_json_child( game_data, "Championship" ), name.c_str( ) );
    const value_t& champ_players = member( champ, "PlayerBoats" );
    const value_t& first = champ_players.tag == 5 && !champ_players.list.empty( ) ? champ_players.list[ 0 ] : null;
    {
        value_t& entry = ui_json_at( ui_json_child( game_data, "Boats" ), 0 );
        ui_json_child( entry, "Properties" ) = member( first, "Properties" );
        ui_json_set_string( ui_json_child( ui_json_child( entry, "Properties" ), "Driver Type" ), "Human" );
        ui_json_child( ui_json_child( entry, "Driver" ), "Name" ) = member( first, "DriverName" );
    }
    if ( type == "Race" )
    {
        const value_t& ai = member( champ, "AIBoats" );
        for ( std::size_t i = 0; ai.tag == 5 && i < ai.list.size( ); ++i )
        {
            value_t& boats = ui_json_child( game_data, "Boats" );
            const std::size_t n = boats.tag == 5 ? boats.list.size( ) : 0;
            value_t& entry = ui_json_at( boats, n );
            ui_json_child( entry, "Properties" ) = member( ai.list[ i ], "Properties" );
            ui_json_set_string( ui_json_child( ui_json_child( entry, "Properties" ), "Driver Type" ), "AI" );
            ui_json_child( ui_json_child( entry, "Driver" ), "Name" ) = member( ai.list[ i ], "DriverName" );
        }
    }
    ui_json_set_string( ui_json_child( root, "InitialScreen" ), "Screens/Championship" );
    return true;
}

// Which builder 0x4BF9B0 handed GameData to (0 = none ran: the next mode is
// not "Action", or the event type is none of the five) and whether it is
// recreated and completed (the race, the slalom and the time trial are).
struct ui_game_config_t
{
    std::uint32_t builder = 0;
    bool built = false;
};

// 0x4BF9B0's data step, after its teardown: root's "Results" erased; then
// only for the next game mode exactly "Action": GameData = root["GameData"],
// cleared; GameData.EventType = GameConfig.Mode's text; the builder by that
// text: "Race" 0x4C0A50, "Slalom" 0x4C18D0, "TimeTrial" 0x4C1E30,
// "Championship" 0x4C2300, "Ghost" 0x4C2C50.
inline ui_game_config_t ui_game_config( const std::string& next_mode, value_t& root, const ui_game_env_t& env )
{
    ui_game_config_t out;
    ui_json_erase( root, "Results" );
    if ( next_mode != "Action" )
        return out;
    value_t& game_data = ui_json_child( root, "GameData" );
    ui_json_clear( game_data );
    const std::string mode = ui_json_text( member( member( root, "GameConfig" ), "Mode" ) );
    ui_json_set_string( ui_json_child( game_data, "EventType" ), mode.c_str( ) );
    if ( mode == "Race" )
    {
        out.builder = 0x4C0A50u;
        out.built = ui_race_game_data( ui_json_child( root, "GameData" ), root, env );
    }
    else if ( mode == "Slalom" )
    {
        out.builder = 0x4C18D0u;
        out.built = ui_slalom_game_data( ui_json_child( root, "GameData" ), root, env );
    }
    else if ( mode == "TimeTrial" )
    {
        out.builder = 0x4C1E30u;
        out.built = ui_slalom_game_data( ui_json_child( root, "GameData" ), root, env, false );
    }
    else if ( mode == "Championship" )
    {
        out.builder = 0x4C2300u;
        out.built = ui_championship_game_data( ui_json_child( root, "GameData" ), root, env );
    }
    else if ( mode == "Ghost" )
    {
        out.builder = 0x4C2C50u;
        out.built = ui_ghost_game_data( ui_json_child( root, "GameData" ), root, env );
    }
    return out;
}

// THE EXPORT (for the race start: VuActionGameMode's load 0x4BA060 takes
// gameutil +80 ["GameData"], VuGame's boat load 0x481CE0 its "Boats"). The
// GameData the front end's exit 0x4BF9B0 leaves when the next mode is
// "Action" and the event was chosen with these values - GameConfig.Mode,
// GameConfig.Track, GameConfig.Course (the slalom's and the championship's),
// BoatConfig.PlayerBoats[0].Properties.Boat / .Skin as the screens leave them
// (VuSetGameModeUIAction writes Mode; the selectors in select.hxx write the
// rest - the caller passes their values). Exact for ANY
// front-end state holding those strings: 0x4BF9B0 erases Results and clears
// GameData before building, and 0x4BF9B0 / 0x4C0A50 / 0x4C18D0 read nothing
// else from gameutil +80 (0x4C1E30 / 0x4C2C50 neither; 0x4C2300 also reads
// GameConfig.GameType, which this export leaves absent: "" - so the
// championship's GameData carries GameType "" and no AI boats; its round is
// env.championship). `env` carries BoatDB / TrackDB, VuRandom's state (drawn:
// the caller's generator advances exactly as retail's global does) and the two
// hooks (the string DB's AI names, the signed-in player's name). `result`
// reports the builder reached.
// THE EXPORT (batch 16) for a whole front-end root (ui_front_end_root's -
// e.g. under the test profile ui_profile_all_unlocked): 0x4BF9B0's data step
// (ui_game_config "Action") on `root` itself, so every member the builders
// read (GameConfig.Championship / GameType / Course included) comes from it.
// Returns root["GameData"].
inline value_t ui_front_end_game_data_root( value_t& root, const ui_game_env_t& env, ui_game_config_t* result = nullptr )
{
    const ui_game_config_t r = ui_game_config( "Action", root, env );
    if ( result )
        *result = r;
    return ui_json_find( root, "GameData" );
}

inline value_t ui_front_end_game_data( const std::string& mode, const std::string& track, const std::string& course, const std::string& boat,
                                       const std::string& skin, const ui_game_env_t& env, ui_game_config_t* result = nullptr )
{
    value_t root;
    ui_json_set_string( ui_json_child( ui_json_child( root, "GameConfig" ), "Mode" ), mode.c_str( ) );
    ui_json_set_string( ui_json_child( ui_json_child( root, "GameConfig" ), "Track" ), track.c_str( ) );
    ui_json_set_string( ui_json_child( ui_json_child( root, "GameConfig" ), "Course" ), course.c_str( ) );
    value_t& properties = ui_json_child( ui_json_at( ui_json_child( ui_json_child( root, "BoatConfig" ), "PlayerBoats" ), 0 ), "Properties" );
    ui_json_set_string( ui_json_child( properties, "Boat" ), boat.c_str( ) );
    ui_json_set_string( ui_json_child( properties, "Skin" ), skin.c_str( ) );
    const ui_game_config_t r = ui_game_config( "Action", root, env );
    if ( result )
        *result = r;
    return ui_json_find( root, "GameData" );
}
