#pragma once

// The selectors' profile on the saved state (world batch 40; bound by c_world::menu_profile): `unlock` g 0x758FE8, `save` the game save's document g 0x758FA0 +0x20.
// The progress slots read the document live: the answers follow every race. HOOKS, declared: the license manager g 0x758FAC vt+0x18 (the game is owned), the asset
// factory's package test 0x4EF7C0 (every package ships in the one pack), the store beta 0x4D3170 (no).

#include <workspace/core/game/unlock.hxx>
#include <workspace/core/ui/progress.hxx>

inline ui_profile_t ui_profile_saved( const c_unlock_manager& unlock, const value_t& save )
{
    ui_profile_t p;
    p.unlock = [ &unlock, &save ]( int slot, const std::string& a, const std::string& b, const std::string& c ) {
        switch ( slot )
        {
        case 92: return unlock.is_unlocked( a, b );             // vt+0x5C 0x4B6800
        case 96: return unlock.in_trial( a, b );                // vt+0x60 0x4B6880
        case 100: return unlock.is_track_unlocked( a, b, c );   // vt+0x64 0x4B68C0
        case 104: return unlock.track_in_trial( a, b, c );      // vt+0x68 0x4B6940
        default: return false;
        }
    };
    p.license = [ ] { return true; };
    p.store_beta = [ ] { return false; };
    p.package = [ ]( const std::string& ) { return true; };
    p.progress = [ &save ]( const std::string& boat ) { return ui_progress_boat_was_used( save, boat ); };     // vt+0x84 0x4B4090
    p.champ_done = [ &save ]( const std::string& name ) {                                                               // vt+0x58 0x4B3980
        std::int32_t place = 0;
        return ui_progress_champ_place( save, name, place );
    };
    p.champ_rank = [ &save ]( const std::string& name ) {
        std::int32_t place = 0x7FFFFFFF;
        ui_progress_champ_place( save, name, place );
        return place;
    };
    // vt+0x30 0x4B22A0 ( game, track, group, mode 0, the outs ): 0x45D86D pushes the 0. A rank's out starts at 0x7FFFFFFF.
    p.race_done = [ &save ]( const std::string& game, const std::string& track, const std::string& group ) {
        std::int32_t place = 0;
        float total = 0.0f, lap = 0.0f;
        return ui_progress_best( save, game, track, group, 0, place, total, lap );
    };
    p.race_rank = [ &save ]( const std::string& game, const std::string& track, const std::string& group ) {
        std::int32_t place = 0x7FFFFFFF;
        float total = 0.0f, lap = 0.0f;
        ui_progress_best( save, game, track, group, 0, place, total, lap );
        return place;
    };
    p.ghost_done = [ &save ]( const std::string& track ) {                                                              // vt+0x7C 0x4B3F90
        float time = 0.0f;
        return ui_progress_ghost_time( save, track, time );
    };
    p.has_progress = true;
    p.has_unlock = true;
    return p;
}

