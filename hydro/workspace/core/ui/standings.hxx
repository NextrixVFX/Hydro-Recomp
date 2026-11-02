#pragma once

#include <workspace/core/ui/champ.hxx>
#include <workspace/core/ui/config.hxx>
#include <workspace/core/ui/flow.hxx>
#include <workspace/core/ui/select.hxx>
#include <workspace/core/world/keyframe.hxx>   // vc_qsort

#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

// VuChampionshipEntity's state (vtable 0x73C998; the draw is champ.hxx): the document +0x278 (the round's container, a
// VuJsonContainer the progress manager keeps) and the standings +0x288..+0x28C. Transliterated from the disassembly
// (tools tmp/b29/d_451370.txt, d_452890.txt, d_454410.txt, tmp/b30/d_*.txt):
//   slot 1 0x451370  the load: the progress manager's slot +0x70, 0x452E00 or 0x4540D0, 0x452890, 0x454410, the footer
//   slot 2 0x451540  the release: the manager's slot +0x68
//          0x451560  OnUITick: the award screen's tick and its end
//          0x451660  "is the award screen up" (a VuRetVal bool: type 3, +4)
//          0x451690  the next / back action: 0x454920 when the championship is done, else the Round 0 test
//          0x451720  the event's start: GameConfig, BoatConfig, "OnStartActionGame"
//   0x452890 the texts (0x452C00 "SetEventString", 0x452D30 "OnSetInfoStringID")
//   0x452E00 a new championship (0x453590 the AI field by class, 0x453F40 a skin already taken)
//   0x4540D0 a race's results (0x454EA0 the points), 0x454410 the standings (the import qsort, comparator 0x4548A0)
//   0x454920 the end: place, the managers' result, the second award screen on "Screens/AwardChampionship" (0x454C70)
//   0x454DD0 Name, 0x454E30 done, 0x455210 the event, 0x4552E0 the event count, 0x455360 the player's class,
//   0x455490 "OnSetScreenStack"
// SUBSTITUTION: the qsort import 0x64C178 (MSVCR110) is vc_qsort (keyframe.hxx): case championship_load runs the real
// DLL over ties in Points, whose order MSVC's sort does not keep.
// HOOKS (ui_champ_env_t): the progress manager g 0x758FD8 (saves), the game manager g 0x758FE8, 0x4ABD60 g 0x758FA0
// ("OnCloudSavePreSave" + its save step), the sign-in name, the string DB, the event manager's broadcast 0x505690, the
// entity's script plugs (0x504770 + vt+8), the award object.
// A boat class that is empty but asked for: retail reads a null vector (a fault): false, nothing further.

struct ui_champ_env_t
{
    value_t* m_root = nullptr;             // gameutil g 0x758FF8 +0x50
    const value_t* m_boat_db = nullptr;    // gameutil +8 -> +0x58
    const value_t* m_champ_db = nullptr;   // gameutil +0x14 -> +0x58
    fleet_random_t* m_random = nullptr;    // VuRandom's global 0x736810
    std::string m_player_name;             // g_vumetrosigninmanager 0x758FB0 +0x14

    std::function< std::string( const char* id ) > m_string;                                     // VuStringDB slot 6
    std::function< void( const char* event, const ui_params_t& params ) > m_broadcast;           // 0x505690
    std::function< void( const char* plug, const ui_params_t& params ) > m_plug;                 // 0x504770 + vt+8
    std::function< void( value_t& data ) > m_load;                                               // g 0x758FD8 vt+0x70
    std::function< void( const value_t& data ) > m_save;                                         // g 0x758FD8 vt+0x68
    std::function< void( ) > m_commit;                                                           // 0x4ABD60
    std::function< void( const std::string& name, std::int32_t place ) > m_result;               // g 0x758FD8 vt+0x5C
    std::function< std::int32_t( const std::string& name, std::int32_t place ) > m_value;        // g 0x758FE8 vt+0x1C
    std::function< bool( std::int32_t value, std::vector< std::string >& names ) > m_names;      // g 0x758FE8 vt+0x58
    bool m_paused = false;                                                                       // g 0x758FD4 +0x34

    // The award object +0x294. Open = 0x47ECC0 on the project, then 0x47F350 ( place, class, value, names ).
    std::function< void( const char* project, std::int32_t place, const std::string& boat_class, std::int32_t value, const std::vector< std::string >& names ) > m_award_open;
    std::function< void( float dt ) > m_award_tick;   // vt+0xC
    std::function< bool( ) > m_award_done;            // 0x47F720
    std::function< void( ) > m_award_exit;            // 0x47F4C0, then the scalar deleting dtor (the object freed)
    std::function< void( ) > m_award_draw;            // 0x451CA0's end: vt+0x10
};

struct ui_champ_state_t
{
    value_t m_data;     // +0x278
    value_t m_rows;     // +0x288..+0x28C, an array (ui_champ_inputs_t::m_rows)
    bool m_award = false;   // +0x294 set
};

// 0x50FD20 + the index both of 0x453590's skin and boat picks compute: one draw r, fistp( r x n - 0.5 ) through cvtdq2ps
// and fistp again (x87: round to nearest), min( i, n - 1 ), max( i, 0 ).
inline int ui_champ_pick( fleet_random_t& rng, int n )
{
    const float scaled = rng.draw( ) * static_cast< float >( n );
    int i = static_cast< int >( std::nearbyint( static_cast< double >( scaled ) - 0.5 ) );
    i = static_cast< int >( std::nearbyint( static_cast< double >( static_cast< float >( i ) ) ) );
    if ( n - 1 < i )
        i = n - 1;
    return i < 0 ? 0 : i;
}

// 0x454DD0: data["Name"]'s text.
inline const std::string& ui_champ_name( const ui_champ_state_t& s ) { return ui_json_text( member( s.m_data, "Name" ) ); }

// ChampDB "VuDBAsset" [ Name ] (0x4F22B0).
inline const value_t& ui_champ_entry( const ui_champ_state_t& s, const ui_champ_env_t& env )
{
    static const value_t null{ };
    return env.m_champ_db ? ui_json_find( member( *env.m_champ_db, "VuDBAsset" ), ui_champ_name( s ) ) : null;
}

// 0x455210: the entry's Events [ Round ], else the null container.
inline const value_t& ui_champ_event( const ui_champ_state_t& s, const ui_champ_env_t& env )
{
    static const value_t null{ };
    const int round = ui_json_as_int( member( s.m_data, "Round" ) );
    const value_t& events = member( ui_champ_entry( s, env ), "Events" );
    return events.tag == 5 && round >= 0 && static_cast< std::size_t >( round ) < events.list.size( ) ? events.list[ static_cast< std::size_t >( round ) ] : null;
}

// 0x454E30: Round >= 0x4552E0's event count (signed).
inline bool ui_champ_done( const ui_champ_state_t& s, const ui_champ_env_t& env )
{
    const value_t& events = member( ui_champ_entry( s, env ), "Events" );
    return ui_json_as_int( member( s.m_data, "Round" ) ) >= ( events.tag == 5 ? static_cast< int >( events.list.size( ) ) : 0 );
}

// 0x455360: BoatDB "VuDBAsset" [ PlayerBoats[0].Properties.Boat ] "Class" - the walk down the document is the writers'
// (0x4FC090, 0x4FBC60: it makes what it does not find).
inline std::string ui_champ_class( ui_champ_state_t& s, const ui_champ_env_t& env )
{
    static const value_t null{ };
    const std::string boat = ui_json_text( ui_json_child( ui_json_child( ui_json_at( ui_json_child( s.m_data, "PlayerBoats" ), 0 ), "Properties" ), "Boat" ) );
    const value_t& db = env.m_boat_db ? member( *env.m_boat_db, "VuDBAsset" ) : null;
    return ui_json_text( member( ui_json_find( db, boat ), "Class" ) );
}

// 0x453F40: any data["AIBoats"][i].Properties.Skin equal to `skin` (the element being filled counts: "" meets its absent one).
inline bool ui_champ_skin_taken( ui_champ_state_t& s, const std::string& skin )
{
    static const value_t null{ };
    const value_t& boats = ui_json_child( s.m_data, "AIBoats" );
    if ( boats.tag != 5 )
        return false;
    for ( const value_t& boat : boats.list )
        if ( ui_json_text( member( member( boat, "Properties" ), "Skin" ) ) == skin )
            return true;
    return false;
}

// 0x453590 (count, the class's boats; ret 8): `count` new data["AIBoats"] elements: the boat drawn; when the boat's BoatDB
// AISkins is a non-empty array: a skin drawn, redrawn up to 8 times while 0x453F40 finds it taken (the eighth redraw is
// not tested), Properties.Skin. No AISkins: no Skin member. False where retail reads the empty class's null vector.
inline bool ui_champ_ai_boats( ui_champ_state_t& s, const ui_champ_env_t& env, int count, const std::vector< std::string >& boats )
{
    static const value_t null{ };
    for ( int k = 0; k < count; ++k )
    {
        value_t& list = ui_json_child( s.m_data, "AIBoats" );
        const std::size_t at = list.tag == 5 ? list.list.size( ) : 0;
        ui_json_at( list, at );
        const int n = static_cast< int >( boats.size( ) );
        const int pick = ui_champ_pick( *env.m_random, n );
        if ( n == 0 )
            return false;
        const std::string boat = boats[ static_cast< std::size_t >( pick ) ];
        ui_json_set_string( ui_json_child( ui_json_child( ui_json_child( s.m_data, "AIBoats" ).list[ at ], "Properties" ), "Boat" ), boat.c_str( ) );
        const value_t& db = env.m_boat_db ? member( *env.m_boat_db, "VuDBAsset" ) : null;
        const value_t& skins = member( ui_json_find( db, boat ), "AISkins" );
        if ( skins.tag != 5 || skins.list.empty( ) )
            continue;
        const int size = static_cast< int >( skins.list.size( ) );
        int j = ui_champ_pick( *env.m_random, size );
        for ( int tries = 8; tries != 0; --tries )
            if ( ui_champ_skin_taken( s, ui_json_text( skins.list[ static_cast< std::size_t >( j ) ] ) ) )
                j = ui_champ_pick( *env.m_random, size );
        const std::string skin = ui_json_text( skins.list[ static_cast< std::size_t >( j ) ] );
        ui_json_set_string( ui_json_child( ui_json_child( ui_json_child( s.m_data, "AIBoats" ).list[ at ], "Properties" ), "Skin" ), skin.c_str( ) );
    }
    return true;
}

// 0x452E00: a new championship. data: Name = a copy of GameConfig.Championship, Round = 0, PlayerBoats[0].DriverName = the
// signed-in player, .Properties = a copy of BoatConfig.PlayerBoats[0].Properties; the AI field by the player's class: A /
// B / C counts 4 / 7 / 4 (class C: 0 / 5 / 10, class A: 10 / 5 / 0) in that order; the 15 names (one shuffle,
// "AI_Name%d" order[k] + 1, AIBoats[k].DriverName). False: a class asked for that has no boats.
inline bool ui_champ_new( ui_champ_state_t& s, const ui_champ_env_t& env )
{
    static const value_t null{ };
    const value_t& root = *env.m_root;
    ui_json_child( s.m_data, "Name" ) = member( member( root, "GameConfig" ), "Championship" );
    ui_json_set_int( ui_json_child( s.m_data, "Round" ), 0 );
    ui_json_set_string( ui_json_child( ui_json_at( ui_json_child( s.m_data, "PlayerBoats" ), 0 ), "DriverName" ), env.m_player_name.c_str( ) );
    const value_t& players = member( member( root, "BoatConfig" ), "PlayerBoats" );
    const value_t& first = players.tag == 5 && !players.list.empty( ) ? players.list[ 0 ] : null;
    ui_json_child( ui_json_at( ui_json_child( s.m_data, "PlayerBoats" ), 0 ), "Properties" ) = member( first, "Properties" );
    const std::string cls = ui_champ_class( s, env );
    int count_a = 4, count_b = 7, count_c = 4;
    if ( cls == "C" )
    {
        count_a = 0;
        count_b = 5;
        count_c = 10;
    }
    else if ( cls == "A" )
    {
        count_a = 10;
        count_b = 5;
        count_c = 0;
    }
    const value_t& db = env.m_boat_db ? member( *env.m_boat_db, "VuDBAsset" ) : null;
    std::map< std::string, std::vector< std::string > > by_class;  // 0x4560F0's std::map, filled by 0x455820
    for ( const std::string& key : ui_json_keys( db ) )
        by_class[ ui_json_text( member( ui_json_find( db, key ), "Class" ) ) ].push_back( key );
    if ( !ui_champ_ai_boats( s, env, count_a, by_class[ "A" ] ) || !ui_champ_ai_boats( s, env, count_b, by_class[ "B" ] ) ||
         !ui_champ_ai_boats( s, env, count_c, by_class[ "C" ] ) )
        return false;
    int names[ 15 ];
    ui_shuffle( *env.m_random, 15, names );
    for ( int k = 0; k < 15; ++k )
    {
        char id[ 256 ];
        std::snprintf( id, sizeof( id ), "AI_Name%d", names[ k ] + 1 );
        const std::string name = env.m_string ? env.m_string( id ) : std::string( );
        ui_json_set_string( ui_json_child( ui_json_at( ui_json_child( s.m_data, "AIBoats" ), static_cast< std::size_t >( k ) ), "DriverName" ), name.c_str( ) );
    }
    return true;
}

// 0x454EA0 (const std::string& name, bool human, int points; ret 0xC): every element of PlayerBoats (human) / AIBoats with
// that DriverName gets Points = its Points + points (an int). The walk is the writers': each element is made a map and
// its DriverName inserted.
inline void ui_champ_add_points( ui_champ_state_t& s, const std::string& name, bool human, std::int32_t points )
{
    value_t& list = ui_json_child( s.m_data, human ? "PlayerBoats" : "AIBoats" );
    for ( std::size_t i = 0; list.tag == 5 && i < list.list.size( ); ++i )
    {
        if ( ui_json_text( ui_json_child( list.list[ i ], "DriverName" ) ) != name )
            continue;
        value_t& held = ui_json_child( list.list[ i ], "Points" );
        const std::uint32_t sum = static_cast< std::uint32_t >( points ) + static_cast< std::uint32_t >( ui_json_as_int( held ) );
        ui_json_set_int( held, static_cast< std::int32_t >( sum ) );
    }
}

// 0x4540D0: the last race. GameResults.Table's rows in order: 0x454EA0( DriverName, IsHuman, the table 0x6F48C0 by row,
// 0 from the 16th ); Round + 1; root.GameResults cleared (the member stays, null).
inline void ui_champ_results( ui_champ_state_t& s, const ui_champ_env_t& env )
{
    static const std::int32_t points[ 15 ] = { 25, 20, 16, 13, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1 };  // 0x6F48C0
    const value_t& table = member( member( *env.m_root, "GameResults" ), "Table" );
    for ( std::size_t i = 0; table.tag == 5 && i < table.list.size( ); ++i )
    {
        const value_t& human = member( table.list[ i ], "IsHuman" );
        ui_champ_add_points( s, ui_json_text( member( table.list[ i ], "DriverName" ) ), human.tag == 3 && ( human.integer & 0xFF ) != 0, i < 15 ? points[ i ] : 0 );
    }
    const std::uint32_t round = static_cast< std::uint32_t >( ui_json_as_int( member( s.m_data, "Round" ) ) ) + 1u;
    ui_json_set_int( ui_json_child( s.m_data, "Round" ), static_cast< std::int32_t >( round ) );
    ui_json_clear( ui_json_child( *env.m_root, "GameResults" ) );
}

// 0x4548A0: the qsort comparator, asInt( b.Points ) - asInt( a.Points ) (wrapping).
inline std::int32_t ui_champ_compare( const value_t& a, const value_t& b )
{
    return static_cast< std::int32_t >( static_cast< std::uint32_t >( ui_json_as_int( member( b, "Points" ) ) ) - static_cast< std::uint32_t >( ui_json_as_int( member( a, "Points" ) ) ) );
}

// 0x454410: the standings: PlayerBoats' elements each with IsPlayer = true, then AIBoats' copies; the import qsort.
inline void ui_champ_standings( ui_champ_state_t& s )
{
    ui_json_clear( s.m_rows );
    s.m_rows.tag = 5;
    s.m_rows.kind = value_t::kind_t::list;
    const value_t& players = ui_json_child( s.m_data, "PlayerBoats" );
    for ( std::size_t i = 0; players.tag == 5 && i < players.list.size( ); ++i )
    {
        s.m_rows.list.push_back( players.list[ i ] );
        ui_json_set_bool( ui_json_child( s.m_rows.list.back( ), "IsPlayer" ), true );
    }
    const value_t& boats = ui_json_child( s.m_data, "AIBoats" );
    for ( std::size_t i = 0; boats.tag == 5 && i < boats.list.size( ); ++i )
        s.m_rows.list.push_back( boats.list[ i ] );
    std::vector< value_t >& rows = s.m_rows.list;
    vc_qsort( static_cast< int >( rows.size( ) ), [ & ]( int i, int j ) { return ui_champ_compare( rows[ static_cast< std::size_t >( i ) ], rows[ static_cast< std::size_t >( j ) ] ); },
              [ & ]( int i, int j ) { if ( i != j ) std::swap( rows[ static_cast< std::size_t >( i ) ], rows[ static_cast< std::size_t >( j ) ] ); } );
}

// 0x452890: the entity's texts. Always: "SetChampionshipString" ( the entry's StringID ). Done: 0x452C00 "SetEventString"
// ( "Champ_Completed" ), 0x452D30 "OnSetInfoStringID" ( DoneInfoStringID ). Else the event's StringID, InfoStringID, then
// "OnChangePoster" ( PosterModel ).
inline void ui_champ_texts( const ui_champ_state_t& s, const ui_champ_env_t& env )
{
    const value_t& entry = ui_champ_entry( s, env );
    const value_t& event = ui_champ_event( s, env );
    const auto one = [ ]( const value_t& v ) { ui_params_t p; p.add_string( ui_json_text( v ).c_str( ) ); return p; };
    const auto text = [ ]( const char* v ) { ui_params_t p; p.add_string( v ); return p; };
    if ( env.m_plug )
        env.m_plug( "SetChampionshipString", one( member( entry, "StringID" ) ) );
    if ( ui_champ_done( s, env ) )
    {
        if ( env.m_plug )
            env.m_plug( "SetEventString", text( "Champ_Completed" ) );
        if ( env.m_broadcast )
            env.m_broadcast( "OnSetInfoStringID", one( member( entry, "DoneInfoStringID" ) ) );
        return;
    }
    if ( env.m_plug )
        env.m_plug( "SetEventString", one( member( event, "StringID" ) ) );
    if ( env.m_broadcast )
    {
        env.m_broadcast( "OnSetInfoStringID", one( member( event, "InfoStringID" ) ) );
        env.m_broadcast( "OnChangePoster", one( member( event, "PosterModel" ) ) );
    }
}

// slot 1 0x451370. False: 0x452E00's false.
inline bool ui_champ_load( ui_champ_state_t& s, const ui_champ_env_t& env )
{
    if ( env.m_load )
        env.m_load( s.m_data );
    bool raced = false;
    if ( s.m_data.tag == 0 )
    {
        if ( !ui_champ_new( s, env ) )
            return false;
    }
    else if ( member( *env.m_root, "GameResults" ).tag != 0 )
    {
        ui_champ_results( s, env );
        raced = true;
    }
    ui_champ_texts( s, env );
    ui_champ_standings( s );
    if ( ui_champ_done( s, env ) && env.m_broadcast )
    {
        env.m_broadcast( "OnClearUIFooterOptions", ui_params_t{ } );
        ui_params_t p;
        p.add_string( "UI/ButtonAccept" );
        p.add_string( "Common_Accept" );
        env.m_broadcast( "OnAddUIFooterOption", p );
    }
    if ( raced )
    {
        if ( env.m_save )
            env.m_save( s.m_data );
        if ( env.m_commit )
            env.m_commit( );
    }
    return true;
}

// slot 2 0x451540.
inline void ui_champ_release( ui_champ_state_t& s, const ui_champ_env_t& env )
{
    if ( env.m_save )
        env.m_save( s.m_data );
}

// 0x455490: "OnSetScreenStack" ( "Screens/Start", "Screens/Main", "Screens/SinglePlayer" ).
inline void ui_champ_screen_stack( const ui_champ_env_t& env )
{
    ui_params_t p;
    p.add_string( "Screens/Start" );
    p.add_string( "Screens/Main" );
    p.add_string( "Screens/SinglePlayer" );
    if ( env.m_broadcast )
        env.m_broadcast( "OnSetScreenStack", p );
}

// 0x454920: the championship's end. place = the first row whose IsPlayer is true (0x7FFFFFFF: none; the walk is the
// writers': each row visited is made a map, IsPlayer inserted); the managers' result; a value above 0, or else a true
// answer from m_names, opens the award on "Screens/AwardChampionship" (0x454C70), +0x294 set; otherwise save, commit, 0x455490.
inline void ui_champ_end( ui_champ_state_t& s, const ui_champ_env_t& env )
{
    std::int32_t place = 0x7FFFFFFF;
    for ( std::size_t i = 0; i < s.m_rows.list.size( ); ++i )
    {
        const value_t& player = ui_json_child( s.m_rows.list[ i ], "IsPlayer" );
        if ( player.tag == 3 && ( player.integer & 0xFF ) != 0 )
        {
            place = static_cast< std::int32_t >( i );
            break;
        }
    }
    const std::string name = ui_champ_name( s );
    if ( env.m_result )
        env.m_result( name, place );
    const std::int32_t value = env.m_value ? env.m_value( name, place ) : 0;
    std::vector< std::string > names;
    if ( value <= 0 && !( env.m_names && env.m_names( value, names ) ) )
    {
        if ( env.m_save )
            env.m_save( s.m_data );
        if ( env.m_commit )
            env.m_commit( );
        ui_champ_screen_stack( env );
        return;
    }
    s.m_award = true;
    const std::string cls = ui_champ_class( s, env );
    if ( env.m_award_open )
        env.m_award_open( "Screens/AwardChampionship", place, cls, value, names );
}

// 0x451560 (OnUITick; the first record's float when its type is 2, else 0).
inline void ui_champ_tick( ui_champ_state_t& s, const ui_champ_env_t& env, float dt )
{
    if ( !s.m_award || env.m_paused )
        return;
    if ( env.m_award_tick )
        env.m_award_tick( dt );
    if ( !env.m_award_done || !env.m_award_done( ) )
        return;
    if ( env.m_award_exit )
        env.m_award_exit( );
    s.m_award = false;
    if ( env.m_save )
        env.m_save( s.m_data );
    if ( env.m_commit )
        env.m_commit( );
    ui_champ_screen_stack( env );
}

// 0x451690: done: 0x454920, the document cleared. Else Round (0x4FC090: inserted) 0: cleared; then 0x455490.
inline void ui_champ_next( ui_champ_state_t& s, const ui_champ_env_t& env )
{
    if ( ui_champ_done( s, env ) )
    {
        ui_champ_end( s, env );
        ui_json_clear( s.m_data );
        return;
    }
    if ( ui_json_as_int( ui_json_child( s.m_data, "Round" ) ) == 0 )
        ui_json_clear( s.m_data );
    ui_champ_screen_stack( env );
}

// 0x451720: the event's start: GameConfig GameType / Track / Course = copies of the event's Type / Track / Course;
// BoatConfig.CurPlayerIndex = 0, PlayerBoats[0].Properties = a copy of data's; "OnStartActionGame".
inline void ui_champ_start( ui_champ_state_t& s, const ui_champ_env_t& env )
{
    const value_t& event = ui_champ_event( s, env );
    value_t& root = *env.m_root;
    ui_json_child( ui_json_child( root, "GameConfig" ), "GameType" ) = member( event, "Type" );
    ui_json_child( ui_json_child( root, "GameConfig" ), "Track" ) = member( event, "Track" );
    ui_json_child( ui_json_child( root, "GameConfig" ), "Course" ) = member( event, "Course" );
    ui_json_set_int( ui_json_child( ui_json_child( root, "BoatConfig" ), "CurPlayerIndex" ), 0 );
    const value_t& source = ui_json_child( ui_json_at( ui_json_child( s.m_data, "PlayerBoats" ), 0 ), "Properties" );
    ui_json_child( ui_json_at( ui_json_child( ui_json_child( root, "BoatConfig" ), "PlayerBoats" ), 0 ), "Properties" ) = source;
    if ( env.m_broadcast )
        env.m_broadcast( "OnStartActionGame", ui_params_t{ } );
}
