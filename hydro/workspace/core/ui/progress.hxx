#pragma once

#include <workspace/core/save/cloud.hxx>
#include <workspace/core/ui/select.hxx>
#include <workspace/core/ui/standings.hxx>

#include <emmintrin.h>

#include <cstdint>
#include <string>

// VuProgressManagerImpl (vtable 0x74590C, g 0x758FD8) on the save document (VuCloudSaveManager +0x20 ["Progress"]): the
// championship's, the boats', the race / time trial / slalom records' and the ghost's slots, from the disassembly (tools tmp/b30/d_4B3980.txt,
// tmp/u31/d1.txt, d2.txt). A record is Progress [ game ] [ track ] [ group ] [ mode name ] [ boat ] { Place, TotalTime, BestLap }.

// Slot 22 0x4B3980: Championship [ name ] .Place an int: true and `out`; else false, `out` kept.
inline bool ui_progress_champ_place( const value_t& document, const std::string& name, std::int32_t& out )
{
    const value_t& place = member( ui_json_find( member( member( document, "Progress" ), "Championship" ), name ), "Place" );
    if ( place.tag != 1 && place.tag != 2 && place.tag != 7 )
        return false;
    out = ui_json_as_int( place );
    return true;
}

// Slot 23 0x4B3A30 (vt+0x5C; name, place): AllTimeChampionshipCount + 1; place 2 / 1 / 0: Bronzes / Silvers / Golds + 1; the best
// place kept (signed) into Championship [ name ] .Place.
inline void ui_progress_champ_result( value_t& document, const std::string& name, std::int32_t place )
{
    value_t& progress = ui_json_child( document, "Progress" );
    const auto bump = [ &progress ]( const char* key )
    {
        const std::uint32_t held = static_cast< std::uint32_t >( ui_json_as_int( ui_json_child( progress, key ) ) );
        ui_json_set_int( ui_json_child( progress, key ), static_cast< std::int32_t >( held + 1u ) );
    };
    bump( "AllTimeChampionshipCount" );
    if ( place == 2 )
        bump( "AllTimeChampionshipBronzes" );
    if ( place == 1 )
        bump( "AllTimeChampionshipSilvers" );
    if ( place == 0 )
        bump( "AllTimeChampionshipGolds" );
    std::int32_t best = place;
    std::int32_t held = 0;
    if ( ui_progress_champ_place( document, name, held ) && held < best )
        best = held;
    ui_json_set_int( ui_json_child( json_member( ui_json_child( progress, "Championship" ), name ), "Place" ), best );
}

// Slot 24 0x4B3C40.
inline std::int32_t ui_progress_champ_count( const value_t& document )
{
    return ui_json_as_int( member( member( document, "Progress" ), "AllTimeChampionshipCount" ) );
}

// Slot 25 0x4B3C80 ( unread, gold, silver, bronze; ret 0x10 ): each 0 unless an int.
inline void ui_progress_champ_medals( const value_t& document, std::int32_t& gold, std::int32_t& silver, std::int32_t& bronze )
{
    const value_t& progress = member( document, "Progress" );
    gold = ui_json_as_int( member( progress, "AllTimeChampionshipGolds" ) );
    silver = ui_json_as_int( member( progress, "AllTimeChampionshipSilvers" ) );
    bronze = ui_json_as_int( member( progress, "AllTimeChampionshipBronzes" ) );
}

// Slot 26 0x4B3D60 (vt+0x68): Progress.ChampionshipState = a copy.
inline void ui_progress_champ_save( value_t& document, const value_t& state )
{
    ui_json_child( ui_json_child( document, "Progress" ), "ChampionshipState" ) = state;
}

// Slot 27 0x4B3DC0 (vt+0x6C): the state itself (what 0x4C2300 reads: ui_game_env_t::championship).
inline const value_t& ui_progress_champ_state( const value_t& document ) { return member( member( document, "Progress" ), "ChampionshipState" ); }

// Slot 28 0x4B3D90 (vt+0x70): a copy out.
inline void ui_progress_champ_load( const value_t& document, value_t& out ) { out = ui_progress_champ_state( document ); }

// Slot 29 0x4B3DE0: a state exists.
inline bool ui_progress_champ_active( const value_t& document ) { return ui_progress_champ_state( document ).tag != 0; }

// Slot 32 0x4B4040 (vt+0x80): BoatsUsed [ boat ] = true.
inline void ui_progress_boat_used( value_t& document, const std::string& boat )
{
    ui_json_set_bool( json_member( ui_json_child( ui_json_child( document, "Progress" ), "BoatsUsed" ), boat ), true );
}

// Slot 33 0x4B4090.
inline bool ui_progress_boat_was_used( const value_t& document, const std::string& boat )
{
    const value_t& used = ui_json_find( member( member( document, "Progress" ), "BoatsUsed" ), boat );
    return used.tag == 3 && ( used.integer & 0xFF ) != 0;
}

// The game modes of the records' third level: the static 0x736718 (three VuStrings, CRT initializer 0x64B3B0). The keys below are by this
// index; retail reads the statics past it unchecked: callers pass 0 .. 2 only.
inline constexpr const char* k_ui_progress_modes[ 3 ] = { "SinglePlayer", "LocalMultiplayer", "OnlineMultiplayer" };

inline constexpr float k_ui_progress_flt_max = 0x1.fffffep+127f;   // 0x7F7FFFFF, 0x6F7864

// 0x4B4100 ( game, track, group, mode, boat ): Progress [ game ] [ track ] [ group ] [ mode name ] [ boat ], each step the static null when the
// parent is not a map or has no such member.
inline const value_t& ui_progress_node( const value_t& document, const std::string& game, const std::string& track, const std::string& group, int mode )
{
    return ui_json_find( ui_json_find( ui_json_find( ui_json_find( member( document, "Progress" ), game ), track ), group ), k_ui_progress_modes[ mode ] );
}

// A number member as the record slots read it: int cvtdq2ps, float, int64 through fild / fstp; else `missing`.
inline float ui_progress_float( const value_t& v, float missing )
{
    if ( v.tag == 1 )
        return static_cast< float >( v.integer );
    if ( v.tag == 2 )
        return v.real;
    if ( v.tag == 7 )
        return static_cast< float >( v.int64 );
    return missing;
}

// Slots 5 0x4B1930 ( game "Race" ), 7 0x4B1C70 ( "TimeTrial" ), 9 0x4B1FB0 ( "Slalom" ): false, the outs kept, when the record is null. Else true
// with Place (0x7FFFFFFF when not a number), TotalTime, BestLap (FLT_MAX). Group "None" in all three: slot 9's second argument is unread, while
// its writer slot 15 stores under it, so a slalom record under another group is only seen by slots 10 / 11.
inline bool ui_progress_record( const value_t& document, const std::string& game, const std::string& track, const std::string& boat, int mode, std::int32_t& place, float& total, float& lap )
{
    const value_t& record = ui_json_find( ui_progress_node( document, game, track, "None", mode ), boat );
    if ( record.tag == 0 )
        return false;
    const value_t& p = member( record, "Place" );
    place = p.tag == 1 || p.tag == 2 || p.tag == 7 ? ui_json_as_int( p ) : 0x7FFFFFFF;
    total = ui_progress_float( member( record, "TotalTime" ), k_ui_progress_flt_max );
    lap = ui_progress_float( member( record, "BestLap" ), k_ui_progress_flt_max );
    return true;
}

// Slot 12 0x4B22A0 (slot 6 0x4B1B70 "Race" / "None", slot 8 0x4B1EB0 "TimeTrial" / "None", slot 10 0x4B21F0 "Slalom" / its argument): the best of every
// boat's record for one mode. Each of the three is the least NUMBER met, starting at 0x7FFFFFFF / FLT_MAX; a member that improves nothing changes
// nothing. False, the outs kept, when no member improved one.
inline bool ui_progress_best( const value_t& document, const std::string& game, const std::string& track, const std::string& group, int mode, std::int32_t& place, float& total, float& lap )
{
    const value_t& boats = ui_progress_node( document, game, track, group, mode );
    if ( boats.tag != 6 )
        return false;
    bool any = false;
    std::int32_t best_place = 0x7FFFFFFF;
    float best_total = k_ui_progress_flt_max, best_lap = best_total;
    const auto lower = [ & ]( const value_t& v, float& best )
    {
        if ( v.tag != 1 && v.tag != 2 && v.tag != 7 )
            return;
        const float f = ui_progress_float( v, 0.0f );
        if ( best > f )   // comiss: a NaN improves nothing
        {
            best = f;
            any = true;
        }
    };
    for ( const auto& field : boats.object )
    {
        lower( member( field.second, "TotalTime" ), best_total );
        lower( member( field.second, "BestLap" ), best_lap );
        const value_t& p = member( field.second, "Place" );
        if ( ( p.tag == 1 || p.tag == 2 || p.tag == 7 ) && ui_json_as_int( p ) < best_place )
        {
            best_place = ui_json_as_int( p );
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

// Slot 11 0x4B2540: slot 12 over the three modes. The outs take the least of what each answered when any did.
inline bool ui_progress_best_any( const value_t& document, const std::string& game, const std::string& track, const std::string& group, std::int32_t& place, float& total, float& lap )
{
    std::int32_t best_place = 0x7FFFFFFF;
    float best_total = k_ui_progress_flt_max, best_lap = best_total;
    bool any = false;
    for ( int mode = 0; mode < 3; ++mode )
    {
        std::int32_t p = 0x7FFFFFFF;
        float t = k_ui_progress_flt_max, l = t;
        any = ui_progress_best( document, game, track, group, mode, p, t, l ) || any;
        best_place = p < best_place ? p : best_place;
        best_total = t < best_total ? t : best_total;
        best_lap = l < best_lap ? l : best_lap;
    }
    if ( !any )
        return false;
    place = best_place;
    total = best_total;
    lap = best_lap;
    return true;
}

// Slots 13 0x4B2710, 14 0x4B2AF0, 15 0x4B2ED0: AllTime<game>Count [ mode name ] + 1; place 2 / 1 / 0: AllTime<game>Bronzes / Silvers / Golds [ mode name ]
// + 1; the record read back (ui_progress_record: Race / TimeTrial always group "None"; slot 15 passes its group to slot 9, which ignores it); when there
// was one the least of each pair kept (signed, minss( held, new )); stored at Progress [ game ] [ track ] [ group ] [ mode name ] [ boat ].
inline void ui_progress_add( value_t& document, const std::string& game, const std::string& track, const std::string& group, const std::string& boat, int mode, std::int32_t place, float total, float lap )
{
    value_t& progress = ui_json_child( document, "Progress" );
    const std::string name = k_ui_progress_modes[ mode ];
    const auto bump = [ & ]( const std::string& key )
    {
        value_t& held = json_member( json_member( progress, key ), name );
        ui_json_set_int( held, static_cast< std::int32_t >( static_cast< std::uint32_t >( ui_json_as_int( held ) ) + 1u ) );
    };
    bump( "AllTime" + game + "Count" );
    if ( place == 2 )
        bump( "AllTime" + game + "Bronzes" );
    else if ( place == 1 )
        bump( "AllTime" + game + "Silvers" );
    else if ( place == 0 )
        bump( "AllTime" + game + "Golds" );
    std::int32_t held_place = 0;
    float held_total = 0.0f, held_lap = 0.0f;
    if ( ui_progress_record( document, game, track, boat, mode, held_place, held_total, held_lap ) )
    {
        place = held_place < place ? held_place : place;
        total = held_total < total ? held_total : total;
        lap = held_lap < lap ? held_lap : lap;
    }
    value_t& record = json_member( json_member( json_member( json_member( json_member( progress, game ), track ), group ), name ), boat );
    ui_json_set_int( json_member( record, "Place" ), place );
    json_set_float( json_member( record, "TotalTime" ), total );
    json_set_float( json_member( record, "BestLap" ), lap );
}

// The score the three games post (0x48ACC9, 0x48E20D, 0x490B08): the human's +0x4B4 x 1000 (0x6F7828), fistp (nearest, ties to even), negated.
inline std::int32_t ui_progress_board_score( float total )
{
    return static_cast< std::int32_t >( 0u - static_cast< std::uint32_t >( _mm_cvtss_si32( _mm_set_ss( total * 1000.0f ) ) ) );
}

// OURS, not retail (the user's decision: no online service, so the mini leaderboard's one row is the local best): the leaderboard manager g 0x758FA8's read answered from the save.
// Mode / Track / Course are what 0x47E2B0 keys on and the three posts name: Race and TimeTrial group "None", Slalom its course, the time the best of the mode's records
// (slot 12 0x4B22A0, mode 0 as the record hook stores). "Ghost": retail posts no ghost score here, so the player's own ghost time (slot 31 0x4B3F90) - ANOTHER OF OUR CHOICES. False: no record.
inline bool ui_progress_ghost_time( const value_t& document, const std::string& boat, float& time );
inline bool ui_progress_board_local( const value_t& document, const value_t& root, std::int32_t& score )
{
    const value_t& config = member( root, "GameConfig" );
    const std::string& mode = ui_json_text( member( config, "Mode" ) );
    const std::string& track = ui_json_text( member( config, "Track" ) );
    float total = k_ui_progress_flt_max, lap = total;
    if ( mode == "Ghost" )
    {
        if ( !ui_progress_ghost_time( document, track, total ) )
            return false;
    }
    else
    {
        std::int32_t place = 0;
        const std::string group = mode == "Slalom" ? ui_json_text( member( config, "Course" ) ) : std::string( "None" );
        if ( !ui_progress_best( document, mode, track, group, 0, place, total, lap ) || !( total < k_ui_progress_flt_max ) )
            return false;
    }
    score = ui_progress_board_score( total );
    return true;
}

// Slots 16 0x4B32C0, 17 0x4B3370, 18 0x4B3420: AllTime<game>Count summed over the three modes.
inline std::int32_t ui_progress_count( const value_t& document, const std::string& game )
{
    const value_t& counts = member( member( document, "Progress" ), ( "AllTime" + game + "Count" ).c_str( ) );
    std::uint32_t sum = 0;
    for ( const char* name : k_ui_progress_modes )
        sum += static_cast< std::uint32_t >( ui_json_as_int( ui_json_find( counts, name ) ) );
    return static_cast< std::int32_t >( sum );
}

// Slots 19 0x4B34D0, 20 0x4B3660, 21 0x4B37F0 ( mode, gold, silver, bronze ): each 0 unless a number.
inline void ui_progress_medals( const value_t& document, const std::string& game, int mode, std::int32_t& gold, std::int32_t& silver, std::int32_t& bronze )
{
    const value_t& progress = member( document, "Progress" );
    const auto of = [ & ]( const char* medal ) { return ui_json_as_int( ui_json_find( member( progress, ( "AllTime" + game + medal ).c_str( ) ), k_ui_progress_modes[ mode ] ) ); };
    gold = of( "Golds" );
    silver = of( "Silvers" );
    bronze = of( "Bronzes" );
}

// Slot 31 0x4B3F90: Ghost [ boat ] a number: true and `time`; else false, `time` kept.
inline bool ui_progress_ghost_time( const value_t& document, const std::string& boat, float& time )
{
    const value_t& held = ui_json_find( member( member( document, "Progress" ), "Ghost" ), boat );
    if ( held.tag != 1 && held.tag != 2 && held.tag != 7 )
        return false;
    time = ui_progress_float( held, 0.0f );
    return true;
}

// Slot 30 0x4B3E10 ( boat, place, time ): AllTimeGhostCount + 1, place 0 AllTimeGhostWins + 1, Ghost [ boat ] = minss( held, time ) when one was held.
inline void ui_progress_ghost_add( value_t& document, const std::string& boat, std::int32_t place, float time )
{
    value_t& progress = ui_json_child( document, "Progress" );
    const auto bump = [ & ]( const char* key )
    {
        value_t& held = ui_json_child( progress, key );
        ui_json_set_int( held, static_cast< std::int32_t >( static_cast< std::uint32_t >( ui_json_as_int( held ) ) + 1u ) );
    };
    bump( "AllTimeGhostCount" );
    if ( place == 0 )
        bump( "AllTimeGhostWins" );
    float held = 0.0f;
    if ( ui_progress_ghost_time( document, boat, held ) )
        time = held < time ? held : time;
    json_set_float( json_member( ui_json_child( progress, "Ghost" ), boat ), time );
}

// The three strings of the corrupt save's box (0x4B1430, 0x4AC480): what the owner of cloud_hooks_t::corrupt_box shows.
inline constexpr const char* k_ui_save_corrupt_box[ 3 ] = { "SaveGame_Corrupt_Heading", "SaveGame_Corrupt_Body", "MessageBox_OK" };

// The championship entity's save slots over the game save (ui_champ_env_t, standings.hxx): 0x451370's vt+0x70, 0x451540's vt+0x68, the
// result vt+0x5C and 0x4ABD60 = c_cloud_save::save.
inline void ui_champ_save_bind( ui_champ_env_t& env, c_cloud_save& cloud )
{
    c_cloud_save* m = &cloud;
    env.m_load = [ m ]( value_t& data ) { ui_progress_champ_load( m->data, data ); };
    env.m_save = [ m ]( const value_t& data ) { ui_progress_champ_save( m->data, data ); };
    env.m_result = [ m ]( const std::string& name, std::int32_t place ) { ui_progress_champ_result( m->data, name, place ); };
    env.m_commit = [ m ] { m->save( ); };
}
