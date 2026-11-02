#pragma once

#include <workspace/core/ui/action.hxx>
#include <workspace/core/ui/dock.hxx>
#include <workspace/core/ui/entity.hxx>

#include <cstdint>
#include <string>

// THE FRONT END'S ROOT: the game-data root (gameutil +80) the single-player
// path leaves for the GameData builders (config.hxx) - GameConfig.Mode /
// .Track / .Course / .Championship and BoatConfig.PlayerBoats[0].Properties
// .Boat / .Skin - built by the recreated retail steps in retail's order:
//   VuSetGameModeUIAction 0x472740   GameConfig.Mode
//   SetSinglePlayer 0x44EF10         CurPlayerIndex 0, PlayerBoats[k].Active
//   the GameConfig<Mode> screen's VuGameConfigEntity: slot 4 0x448120 (the
//     options from OptionDB, the walk remembering each unlocked current
//     choice) then onGameInitialize 0x45D050 (the refresh walk: each option's
//     LastGameConfigSelection, else its first choice)
//   the BoatConfig* screen's VuBoatConfigEntity: slot 4, onGameInitialize
//     0x44CD10, onGameRelease 0x44D090 (PlayerBoats[player].Properties).
// The player's own picks enter where retail keeps them: the root's
// LastGameConfigSelection [option name] and LastBoatConfigSelection [player]
// [option name] (the profile's - the caller seeds them). The navigation that
// changes a choice on screen is navigate.hxx (0x448470 -> 0x449830).
// The game selector's slot 26 0x45E300 (asked for a single-player "push") is
// select.hxx's ui_game_config_next over the progress answers p.race_done /
// champ_done / ghost_done (leave p.next_choice empty to use it); its slot 23
// 0x45DBF0 (the locked option's info text) is expand.hxx.

// What a config screen's entity contributes, read from OUR loaded entity: its
// "OptionType" (the OptionDB entry), "Single Player", "Force Online", and for
// the boat selector "Allow Special Boats"; `flags` = VuEntity +0x14 (2 on every
// config entity of the real screens as retail's loader leaves them - the
// loader does not recreate that word: the caller passes it).
struct ui_front_end_screen_t
{
    std::string m_option_type;
    bool m_single_player = false;
    bool m_force_online = false;
    bool m_allow_special = false;
    std::uint32_t m_flags = 2;
};

// A loaded entity's property value by name (the rows' list order).
inline const ui_slot_t* ui_property_value( const entity_t& entity, const char* name )
{
    if ( !ui_is_state( entity ) )
        return nullptr;
    const ui_state_t* s = static_cast< const ui_state_t* >( entity.m_state.get( ) );
    for ( int k = 0; k < s->m_count; ++k )
        if ( std::strcmp( s->m_rows[ k ]->m_name, name ) == 0 )
            return &s->m_value[ k ];
    return nullptr;
}

inline ui_front_end_screen_t ui_front_end_screen( const entity_t& entity, std::uint32_t flags = 2 )
{
    ui_front_end_screen_t s;
    s.m_flags = flags;
    if ( const ui_slot_t* v = ui_property_value( entity, "OptionType" ) )
        s.m_option_type = v->m_text;
    if ( const ui_slot_t* v = ui_property_value( entity, "Single Player" ) )
        s.m_single_player = ( v->m_bits[ 0 ] & 0xFF ) != 0;
    if ( const ui_slot_t* v = ui_property_value( entity, "Force Online" ) )
        s.m_force_online = ( v->m_bits[ 0 ] & 0xFF ) != 0;
    if ( const ui_slot_t* v = ui_property_value( entity, "Allow Special Boats" ) )
        s.m_allow_special = ( v->m_bits[ 0 ] & 0xFF ) != 0;
    return s;
}

// The DB documents the selectors read (gameutil +8 BoatDB, +12 SkinDB, +16
// TrackDB, +20 ChampDB, +52 OptionDB - and the entities' OptionType asset).
struct ui_front_end_dbs_t
{
    const value_t* m_option_db = nullptr;
    const value_t* m_boat_db = nullptr;
    const value_t* m_skin_db = nullptr;
    const value_t* m_track_db = nullptr;
    const value_t* m_champ_db = nullptr;
};

// THE TEST PROFILE (the user's decision, batch 16: "--front-end-defaults can
// exist as a profile with everything unlocked for testing"). Saves / profiles
// are re-made for the platform, NOT 1:1: this is a DECLARED platform answer
// set for the hooks the recreated selectors call - not a stand-in for retail
// logic. Run through ui_front_end_root it yields retail's own defaults for an
// everything-unlocked profile (each option's current / first choice, through
// the recreated walks). Each answer, next to the hook it fills:
//   unlock        the unlock manager g 0x758FE8 slots +92 / +96 / +100 / +104
//                 (0x45D0C0 / 0x45D4C0 / 0x44D150 / 0x44D2C0 ...): true = unlocked.
//   license       g 0x758FAC slot +0x18: true = the full game is owned.
//   progress      the progress manager g 0x758FD8 slot +132 (the boat selector's
//                 slot 17 0x44D410): true = complete.
//   race_done / champ_done / ghost_done   slots +48 / +88 / +124 (slot 17
//                 0x45D720's): true = complete; so slot 26 0x45E300 (a "push")
//                 finds no not-done choice and keeps the current one.
//   race_rank / champ_rank   the best placing those slots write: 0 (first).
//   store_beta    0x4D3170: false (not the store beta build).
//   package       the asset factory g 0x759004 slot +0x90: true = every
//                 package / DLC asset present.
//   next_choice   unset: the recreated slot 26 (select.hxx ui_game_config_next).
inline ui_profile_t ui_profile_all_unlocked( )
{
    ui_profile_t p;
    p.unlock = []( int, const std::string&, const std::string&, const std::string& ) { return true; };
    p.license = [] { return false; };   // the license manager's vt+0x18 "is trial" (0x42DE10): a full game
    p.progress = []( const std::string& ) { return true; };
    p.store_beta = [] { return false; };
    p.package = []( const std::string& ) { return true; };
    p.champ_rank = []( const std::string& ) { return 0; };
    p.race_rank = []( const std::string&, const std::string&, const std::string& ) { return 0; };
    p.race_done = []( const std::string&, const std::string&, const std::string& ) { return true; };
    p.champ_done = []( const std::string& ) { return true; };
    p.ghost_done = []( const std::string& ) { return true; };
    p.has_progress = true;
    p.has_unlock = true;
    return p;
}

// THE EXPORT: `root` in (the profile's Last*Selection, or nothing) and out (the
// root the front end leaves after the boat screen); `op` = gameutil +0x64 when
// the screens initialise ("push" when pushed). False on any path not
// recreated (the slot 23 info text excepted: see above).
inline bool ui_front_end_root( value_t& root, const std::string& mode, const ui_front_end_screen_t& game, const ui_front_end_screen_t& boat,
                               const ui_front_end_dbs_t& dbs, const std::string& op, const ui_profile_t& p, const ui_config_hooks_t& h )
{
    ui_set_game_mode( root, mode );
    ui_set_single_player( root );
    ui_config_env_t env;
    env.m_root = &root;
    env.m_op = op;
    ui_config_entity_t e;
    e.m_single_player = game.m_single_player;
    e.m_force_online = game.m_force_online;
    ui_game_config_t2 g;
    value_t game_doc;
    if ( !ui_game_config_build( e, env, game_doc, dbs.m_option_db, game.m_option_type, dbs.m_track_db, game.m_flags, p, h ) )
        return false;
    if ( !ui_game_config_initialize( e, g, env, game_doc, p, h ) )
        return false;
    ui_dock_t d;
    d.m_base.m_single_player = boat.m_single_player;
    d.m_base.m_force_online = boat.m_force_online;
    d.m_allow_special = boat.m_allow_special;
    d.m_boat_db = dbs.m_boat_db;
    d.m_skin_db = dbs.m_skin_db;
    d.m_champ_db = dbs.m_champ_db;
    value_t boat_doc;
    if ( !ui_dock_build( d, env, boat_doc, dbs.m_option_db, boat.m_option_type, boat.m_flags, p, h ) )
        return false;
    if ( !ui_dock_initialize( d, env, boat_doc, p, h ) )
        return false;
    return ui_boat_config_release( root, d.m_player, d.m_base.m_options.size( ) );
}
