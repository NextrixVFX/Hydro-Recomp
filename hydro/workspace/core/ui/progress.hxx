#pragma once

#include <workspace/core/ui/savefile.hxx>
#include <workspace/core/ui/select.hxx>
#include <workspace/core/ui/standings.hxx>

#include <cstdint>
#include <string>

// VuProgressManagerImpl (vtable 0x74590C, g 0x758FD8) on the save document (VuCloudSaveManager +0x20 ["Progress"]): the
// championship's and the boats' slots, from the disassembly (tools tmp/b30/d_4B3980.txt). Slots 5 .. 21 (the race, time trial and
// slalom records, 0x4B1930 .. 0x4B37F0) and the ghost's 0x4B3E10 / 0x4B3F90 are NOT recreated: named in HANDOFF.md.

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

// The championship entity's save slots over the manager (ui_champ_env_t, standings.hxx): 0x451370's vt+0x70, 0x451540's vt+0x68, the
// result vt+0x5C and 0x4ABD60.
inline void ui_champ_save_bind( ui_champ_env_t& env, c_save_manager& manager )
{
    c_save_manager* m = &manager;
    env.m_load = [ m ]( value_t& data ) { ui_progress_champ_load( m->document( ), data ); };
    env.m_save = [ m ]( const value_t& data ) { ui_progress_champ_save( m->document( ), data ); };
    env.m_result = [ m ]( const std::string& name, std::int32_t place ) { ui_progress_champ_result( m->document( ), name, place ); };
    env.m_commit = [ m ] { m->request( ); };
}
