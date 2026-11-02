#pragma once

#include <workspace/core/ui/bind.hxx>
#include <workspace/core/ui/difficulty.hxx>
#include <workspace/core/ui/hud.hxx>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

// The Screens/Loading* leaves: each computes its text once, in its slot 6 (VuEntity's gameInitialize), from GameData and the DBs, and slot 12 hands it over as it is.
//   VuBarracudaEventUITextEntity 0x443EB0, VuBarracudaSlalomPenaltyUITextEntity 0x444160, VuBarracudaTrackUITextEntity 0x4445F0,
//   VuBarracudaTuningUITextEntity 0x4448C0 (the time to beat of "Place"), VuBarracudaUnlockUITextEntity 0x444D80 (the credits of "Place").
// HOOKS: VuStringDB slots 6 / 7; the unlock manager g 0x758FE8 vt+0x18 .. +0x2C (game/unlock.hxx); the progress manager's round is env.d.m_round.
struct ui_loading_env_t
{
    ui_difficulty_env_t d;
    const value_t* m_constant_db = nullptr;  // gameutil +4
    const value_t* m_track_db = nullptr;     // gameutil +0x10
    std::function< std::string( const std::string& ) > m_string6, m_string7;
    // vt+0x18 "Race", +0x20 "Slalom", +0x24 "TimeTrial", +0x2C "Ghost" ( class, place ); vt+0x1C "Championship" ( the round's name, place ); vt+0x28 "RubberDucky" ( "", place ).
    std::function< std::int32_t( const char* mode, const std::string& key, std::int32_t place ) > m_reward;
};

inline std::string ui_loading_data( const ui_loading_env_t& env, const char* key )
{
    static const value_t null{ };
    return race_game_data_text( env.d.m_root ? *env.d.m_root : null, env.d.m_dev, key );
}

// 0x4D3410: Level past its last '/' (0x564AE0).
inline std::string ui_loading_level( const ui_loading_env_t& env )
{
    const std::string level = ui_loading_data( env, "Level" );
    const std::size_t slash = level.rfind( '/' );
    return slash == std::string::npos ? level : level.substr( slash + 1 );
}

// 0x443EB0.
inline std::string ui_loading_event( const ui_loading_env_t& env )
{
    const std::string type = ui_loading_data( env, "GameType" ), course = ui_loading_data( env, "Course" );
    std::string out = env.m_string7( "Option_Mode_" + type );
    if ( type == "Slalom" )
    {
        std::string id;
        if ( course == "CourseA" )
            id = "Option_Course_SlalomA";
        else if ( course == "CourseB" )
            id = "Option_Course_SlalomB";
        else if ( course == "CourseC" )
            id = "Option_Course_SlalomC";
        out += " ";   // 0x691D70
        out += env.m_string7( id );
    }
    return out;
}

// 0x4445F0.
inline std::string ui_loading_track( const ui_loading_env_t& env ) { return env.m_string7( "Option_Track_" + ui_loading_level( env ) ); }

// 0x444160: ConstantDB Slalom [ the boat's class ] GateMissPenalty: int / float / int64 as a float, else 0; rounded (fistp) into "Loading_Slalom_Penalty%d", slot 6.
inline std::string ui_loading_penalty( const ui_loading_env_t& env )
{
    const value_t& v = member( ui_db_entry( env.m_constant_db, "Slalom" ), ui_human_boat_class( env.d ).c_str( ) );
    const value_t& g = member( v, "GateMissPenalty" );
    float x = 0.0f;
    if ( g.tag == 1 )
        x = static_cast< float >( g.integer );
    else if ( g.tag == 2 )
        x = g.real;
    else if ( g.tag == 7 )
        x = static_cast< float >( g.int64 );
    char text[ 0x100 ];
    std::snprintf( text, sizeof( text ), "Loading_Slalom_Penalty%d", static_cast< int >( ui_fistp32( x ) ) );
    return env.m_string6( text );
}

// 0x4FBC10 then 0x4FC450.
inline std::int32_t ui_loading_int_at( const value_t& array, std::int32_t i )
{
    if ( array.tag != 5 || i < 0 || i >= static_cast< std::int32_t >( array.list.size( ) ) )
        return 0;
    const value_t& v = array.list[ static_cast< std::size_t >( i ) ];
    return v.tag == 1 ? v.integer : v.tag == 2 ? ui_cvtt( v.real ) : v.tag == 7 ? static_cast< std::int32_t >( v.int64 ) : 0;
}

// 0x4448C0: Race / Ghost / RubberDucky "Place_%03d", slot 6. TimeTrial TrackDB [ level ][ TimeTrial ][ class ][ place ], Slalom [ level ][ Slalom ][ class ].Times[ place ]: seconds, 0x53F5E0
// format 1. Any other game: empty (+0x110 stays). A negative place reads as 0.
inline std::string ui_loading_tuning( const ui_loading_env_t& env, std::int32_t place )
{
    if ( place < 0 )
        place = 0;
    const std::string type = ui_loading_data( env, "GameType" );
    if ( type == "Race" || type == "Ghost" || type == "RubberDucky" )
    {
        char text[ 0x10 ];
        std::snprintf( text, sizeof( text ), "Place_%03d", place );
        return env.m_string6( text );
    }
    const value_t& node = member( ui_db_entry( env.m_track_db, ui_loading_level( env ) ), type.c_str( ) );
    const value_t& by_class = member( node, ui_human_boat_class( env.d ).c_str( ) );
    if ( type == "TimeTrial" )
        return ui_format_time( 1, static_cast< float >( ui_loading_int_at( by_class, place ) ) );
    if ( type == "Slalom" )
        return ui_format_time( 1, static_cast< float >( ui_loading_int_at( member( by_class, "Times" ), place ) ) );
    return std::string( );
}

// 0x444D80: GameData.IsChampionship: the round's "Name"; else by GameType; another type: 0. _itoa_s radix 10.
inline std::string ui_loading_credits( const ui_loading_env_t& env, std::int32_t place )
{
    static const value_t null{ };
    const value_t& champ = member( race_action_game_data( env.d.m_root ? *env.d.m_root : null, env.d.m_dev ), "IsChampionship" );
    const std::string type = ui_loading_data( env, "GameType" ), cls = ui_human_boat_class( env.d );
    std::int32_t v = 0;
    if ( champ.tag == 3 && champ.integer != 0 )
        v = env.m_reward( "Championship", env.d.m_round ? ui_json_text( member( env.d.m_round( ), "Name" ) ) : std::string( ), place );
    else if ( type == "Race" || type == "TimeTrial" || type == "Slalom" || type == "Ghost" )
        v = env.m_reward( type.c_str( ), cls, place );
    else if ( type == "RubberDucky" )
        v = env.m_reward( "RubberDucky", std::string( ), place );
    return std::to_string( v );
}

// The leaf's text by its vtable; false: not one of the five.
inline bool ui_loading_text( const ui_loading_env_t& env, std::uint32_t vt, std::int32_t place, std::string& out )
{
    if ( vt == k_ui_event_text_vtable )
        out = ui_loading_event( env );
    else if ( vt == k_ui_penalty_text_vtable )
        out = ui_loading_penalty( env );
    else if ( vt == k_ui_track_text_vtable )
        out = ui_loading_track( env );
    else if ( vt == k_ui_tuning_text_vtable )
        out = ui_loading_tuning( env, place );
    else if ( vt == k_ui_unlock_text_vtable )
        out = ui_loading_credits( env, place );
    else
        return false;
    return true;
}
