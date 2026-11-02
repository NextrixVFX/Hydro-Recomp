#pragma once

// Boat batch 33's save listeners against retail (case settings_profile, cases/profile.py): VuSettingsManager 0x4B4870 0x4B5240,
// VuCloudManager's 0x5C62C0 0x5C63B0, VuUnlockManagerImpl's script on the real UnlockDB. Included after checks/save.hxx.

#include <workspace/core/game/settings.hxx>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

inline bool verify_settings_profile( )
{
    save_words_t c{ g_golden_settings_profile_words };
    const std::string real_const = c.str( ), unlockdb = c.str( );
    const auto read = [ ]( const std::string& bytes, value_t& out ) {
        out = value_t{ };
        c_reader::read_at_start( std::vector< std::uint8_t >( bytes.begin( ), bytes.end( ) ), out );
    };
    int bad = 0;
    const auto fail = [ & ]( const char* what, unsigned v ) {
        if ( bad++ < 8 )
            LOG_ERROR( "settings_profile %s vector %u differs from retail", what, v );
    };
    // ---- VuSettingsManager
    const unsigned settings_count = c.next( );
    for ( unsigned v = 0; v < settings_count; ++v )
    {
        const std::string cdoc = c.str( ), pdoc = c.str( );
        const unsigned acc = c.next( ), tilt = c.next( ), disp = c.next( ), flip = c.next( );
        const std::string fallback = c.str( );
        unsigned bytes[ 5 ], words[ 5 ];
        for ( unsigned& b : bytes )
            b = c.next( );
        for ( unsigned& w : words )
            w = c.next( );
        const std::string units = c.str( ), fidelity = c.str( );
        const unsigned display_out = c.next( );
        std::vector< std::string > want( c.next( ) );
        for ( std::string& r : want )
            r = c.str( );
        const std::string dump = c.str( );
        value_t constant, profile;
        read( cdoc.empty( ) ? real_const : cdoc, constant );
        read( pdoc, profile );
        std::vector< std::string > log;
        settings_hooks_t h;
        h.category = [ & ]( const char* name, float volume ) {
            std::uint32_t u;
            std::memcpy( &u, &volume, 4 );
            char line[ 64 ];
            std::snprintf( line, sizeof( line ), "volume %08X", u );
            log.push_back( std::string( "category " ) + name );
            log.push_back( line );
        };
        h.flip_interval = [ & ]( std::int32_t n ) { log.push_back( "flip " + std::to_string( n ) ); };
        h.has_accelerometer = [ acc ] { return acc != 0u; };
        h.has_tilt = [ tilt ] { return tilt != 0u; };
        settings_t s;
        float fdisp;
        std::memcpy( &fdisp, &disp, 4 );
        settings_configure( s, fdisp, fallback, static_cast< std::int32_t >( flip ) );
        ui_settings_platform_t p;
        settings_load( s, constant, profile, h, p );
        unsigned got[ 5 ];
        std::memcpy( &got[ 0 ], &s.ui.m_steering, 4 );
        std::memcpy( &got[ 1 ], &s.ui.m_effect, 4 );
        std::memcpy( &got[ 2 ], &s.ui.m_music, 4 );
        std::memcpy( &got[ 3 ], &s.ui.m_display, 4 );
        got[ 4 ] = static_cast< unsigned >( s.ui.m_flip_interval );
        unsigned out;
        std::memcpy( &out, &s.ui.m_display_out, 4 );
        const unsigned got_bytes[ 5 ] = { s.auto_throttle_pad, s.auto_throttle_keys, s.ui.m_tilt, s.ui.m_vibration, s.ui.m_subtitles };
        std::string got_dump;
        settings_save( s, profile, h );
        vujb_dump( profile, got_dump );
        if ( std::memcmp( got, words, 20 ) != 0 || std::memcmp( got_bytes, bytes, 20 ) != 0 || s.ui.m_units != units || s.ui.m_fidelity != fidelity || out != display_out || log != want ||
             got_dump != dump )
            fail( "settings", v );
    }
    // ---- VuCloudManager
    const unsigned cloud_count = c.next( );
    for ( unsigned v = 0; v < cloud_count; ++v )
    {
        const std::string doc = c.str( );
        cloud_profile_t cp;
        for ( std::uint16_t& t : cp.news_time.parts )
            t = static_cast< std::uint16_t >( c.next( ) );
        for ( std::uint16_t& t : cp.tuning_time.parts )
            t = static_cast< std::uint16_t >( c.next( ) );
        cp.alert = c.next( ) != 0u;
        const bool want_id = c.next( ) != 0u, want_alert = c.next( ) != 0u;
        const std::string news = c.str( ), tuning = c.str( );
        unsigned times[ 12 ];
        for ( unsigned& t : times )
            t = c.next( );
        const std::string dump = c.str( );
        value_t profile;
        read( doc, profile );
        cloud_profile_load( cp, profile );
        std::string got_news, got_tuning, got_dump;
        vujb_dump( cp.news, got_news );
        vujb_dump( cp.tuning, got_tuning );
        bool same = cp.identity_saved == want_id && cp.alert == want_alert && got_news == news && got_tuning == tuning;
        for ( int k = 0; k < 6; ++k )
            same = same && cp.news_time.parts[ k ] == times[ k ] && cp.tuning_time.parts[ k ] == times[ 6 + k ];
        cloud_profile_save( cp, profile );
        vujb_dump( profile, got_dump );
        if ( !same || got_dump != dump )
            fail( "cloud", v );
    }
    // ---- VuUnlockManagerImpl
    static value_t db;
    read( unlockdb, db );
    const value_t& table = member( member( db, "VuDBAsset" ), "Unlockables" );
    const unsigned unlock_runs = c.next( );
    int ops = 0, founds = 0;
    for ( unsigned v = 0; v < unlock_runs; ++v )
    {
        const std::string doc = c.str( );
        const unsigned all = c.next( ), beta = c.next( );
        unsigned start[ 5 ];
        for ( unsigned& w : start )
            w = c.next( );
        value_t saved;
        read( doc, saved );
        c_unlock_manager u;
        u.db = &db;
        u.unlock_all = static_cast< std::uint8_t >( all );
        u.store_beta = [ beta ] { return beta != 0u; };
        u.load( saved );
        const auto state = [ & ]( unsigned* out ) {
            out[ 0 ] = static_cast< unsigned >( u.total );
            out[ 1 ] = static_cast< unsigned >( u.local );
            out[ 2 ] = static_cast< unsigned >( u.online );
            out[ 3 ] = static_cast< unsigned >( u.feats & 0xFFFFFFFFu );
            out[ 4 ] = static_cast< unsigned >( u.feats >> 32 );
        };
        unsigned now[ 5 ];
        state( now );
        if ( std::memcmp( now, start, 20 ) != 0 )
            fail( "unlock load", v );
        const unsigned n = c.next( );
        for ( unsigned k = 0; k < n; ++k, ++ops )
        {
            const unsigned op = c.next( ), r = c.next( );
            std::vector< unsigned > found;
            const c_unlock_manager::found_t note = [ & ]( const value_t& row ) { found.push_back( static_cast< unsigned >( &row - table.list.data( ) ) ); };
            unsigned got = r;
            bool out_same = true;
            switch ( op )
            {
            case 12: { const unsigned a = c.next( ), b = c.next( ), d = c.next( ); u.set_credits( static_cast< int >( a ), static_cast< int >( b ), static_cast< int >( d ) ); break; }
            case 13: { const unsigned a = c.next( ), b = c.next( ), d = c.next( ); u.add_credits( static_cast< int >( a ), static_cast< int >( b ), d ? &note : nullptr ); break; }
            case 17: u.refresh( ); break;
            case 18: { const std::string a = c.str( ); const unsigned d = c.next( ); u.add_feat( a, d ? &note : nullptr ); break; }
            case 19: { const std::string a = c.str( ); got = u.has_feat( a ) ? 1u : 0u; break; }
            case 22:
            {
                const unsigned credits = c.next( );
                std::vector< std::string > names( c.next( ) );
                for ( std::string& s : names )
                    s = c.str( );
                got = static_cast< unsigned >( u.unlock_count_for( static_cast< int >( credits ), names ) );
                break;
            }
            case 23: { const std::string a = c.str( ), b = c.str( ); got = u.is_unlocked( a, b ) ? 1u : 0u; break; }
            case 24: { const std::string a = c.str( ), b = c.str( ); got = u.in_trial( a, b ) ? 1u : 0u; break; }
            case 25: { const std::string a = c.str( ), b = c.str( ), d = c.str( ); got = u.is_track_unlocked( a, b, d ) ? 1u : 0u; break; }
            case 26: { const std::string a = c.str( ), b = c.str( ), d = c.str( ); got = u.track_in_trial( a, b, d ) ? 1u : 0u; break; }
            case 27:
            {
                const unsigned want = c.next( );
                std::int32_t o = 0xABCD;
                got = u.next_credits( o ) ? 1u : 0u;
                out_same = static_cast< unsigned >( o ) == want;
                break;
            }
            default: fail( "unlock op", v ); break;
            }
            std::vector< unsigned > want_found( c.next( ) );
            for ( unsigned& w : want_found )
                w = c.next( );
            founds += static_cast< int >( want_found.size( ) );
            unsigned want_state[ 5 ];
            for ( unsigned& w : want_state )
                w = c.next( );
            state( now );
            if ( got != r || !out_same || found != want_found || std::memcmp( now, want_state, 20 ) != 0 )
                fail( "unlock step", v * 1000u + k );
        }
        const std::string dump = c.str( );
        value_t out;
        u.save( out );
        std::string got_dump;
        vujb_dump( out, got_dump );
        if ( got_dump != dump )
            fail( "unlock save", v );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuSettingsManager 0x4B4870 0x4B5240 (+ 0x4B4C60 0x4B4E20 0x4B4F00 0x4B4FC0), VuCloudManager 0x5C62C0 0x5C63B0 and VuUnlockManagerImpl (0x4B6BA0 0x4B6AF0, 0x4B7250, slots 12 - 27) "
                     "match retail: %u settings, %u cloud, %u unlock runs, %d steps, %d rows found", settings_count, cloud_count, unlock_runs, ops, founds );
    else
        LOG_ERROR( "settings_profile FAILED %d", bad );
    return bad == 0;
}
