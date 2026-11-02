#pragma once

#include <workspace/core/ui/expand.hxx>

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

// The Single Player screen's four completion texts, slot 6 (gameInitialize) of each, case completion_text:
//   VuRaceCompletionUITextEntity         ctor 0x458300, vtable 0x73CCF4, slot 6 0x458500
//   VuSlalomCompletionUITextEntity       ctor 0x458840, vtable 0x73CCB4, slot 6 0x458A40
//   VuTimeTrialCompletionUITextEntity    ctor 0x458E60, vtable 0x73CC74, slot 6 0x459060
//   VuChampionshipCompletionUITextEntity ctor 0x4593A0, vtable 0x73CC34, slot 6 0x4595A0
// Fields: +0x10C the text (slot 12 0x443920 returns it); the ctor's word +0x124 = 0x100: +0x124 "UnusedContent" = 0 (plug 0x4582C0), +0x125 "Locked" = 1
// (plug 0x4582E0).
// Over each key of TrackDB (ChampDB for the championship) in the document that passes its package test (0x4D3AA0 / 0x4D3C60), for each course
// (Slalom: CourseC, CourseB, CourseA; else one): counted in the total; a record: a place under 3 counts as done, and Locked goes to 0. No record: the
// unlock manager's "is it open" (vt+0x64 mode, track, course; the championship's vt+0x5C "Championship", name): open writes the word 1 (UnusedContent 1,
// Locked 0). The text: "%d/%d " done, total, then VuStringDB slot 6 "SinglePlayer_020".
// HOOKS, the saves' / the platform's: the record (the progress manager's slots vt+0x18, +0x20, +0x28, +0x58), the open test, the package test.
enum class ui_completion_kind_t { race, slalom, time_trial, championship };

struct ui_completion_t
{
    ui_completion_kind_t m_kind = ui_completion_kind_t::race;
    std::string m_text;      // +0x10C
    bool m_unused = false;   // +0x124
    bool m_locked = true;    // +0x125
};

struct ui_completion_env_t
{
    const value_t* m_track_db = nullptr;
    const value_t* m_champ_db = nullptr;
    std::function< bool( const std::string& asset ) > m_package;
    std::function< bool( ui_completion_kind_t, const std::string& track, const std::string& course, std::int32_t& place ) > m_record;
    std::function< bool( ui_completion_kind_t, const std::string& track, const std::string& course ) > m_open;
    std::function< std::string( const char* ) > m_string6;
};

inline constexpr std::uint32_t k_ui_completion_vtables[ 4 ] = { 0x73CCF4u, 0x73CCB4u, 0x73CC74u, 0x73CC34u };

inline int ui_completion_of_vtable( std::uint32_t vt )
{
    for ( int k = 0; k < 4; ++k )
        if ( k_ui_completion_vtables[ k ] == vt )
            return k;
    return -1;
}

inline void ui_completion_initialize( ui_completion_t& c, const ui_completion_env_t& env )
{
    const bool champ = c.m_kind == ui_completion_kind_t::championship;
    const value_t* db = champ ? env.m_champ_db : env.m_track_db;
    const value_t& rows = db ? member( *db, "VuDBAsset" ) : value_t( );
    std::int32_t done = 0, total = 0;
    static const char* const slalom[ 3 ] = { "CourseC", "CourseB", "CourseA" };
    static const char* const none[ 1 ] = { "" };
    const char* const* courses = c.m_kind == ui_completion_kind_t::slalom ? slalom : none;
    const int count = c.m_kind == ui_completion_kind_t::slalom ? 3 : 1;
    for ( const auto& row : rows.object )
    {
        if ( !ui_db_package_ok( db, row.first, env.m_package ) )
            continue;
        total += count;
        for ( int k = 0; k < count; ++k )
        {
            std::int32_t place = 0;
            if ( env.m_record( c.m_kind, row.first, courses[ k ], place ) )
            {
                if ( place < 3 )
                    ++done;
                c.m_locked = false;
            }
            else if ( env.m_open( c.m_kind, row.first, courses[ k ] ) )
            {
                c.m_unused = true;
                c.m_locked = false;
            }
        }
    }
    char buffer[ 0x40 ];
    std::snprintf( buffer, sizeof( buffer ), "%d/%d ", done, total );
    c.m_text = std::string( buffer ) + env.m_string6( "SinglePlayer_020" );
}
