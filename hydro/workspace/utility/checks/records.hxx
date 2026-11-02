#pragma once

#include <workspace/core/game/packages.hxx>
#include <workspace/core/ui/progress.hxx>

// ui/progress.hxx and game/packages.hxx, a second witness beside verify_ui_progress_records and hidden_packages, against retail's
// VuProgressManagerImpl and VuHiddenPackageManagerImpl on their own vtables (oracle case
// progress_race): a seeded op script over five documents x 5: answers, the out words, the commit / achievement log, the document's hash.
inline bool verify_progress_race( )
{
    int failed = 0, ops = 0;
    const auto hash = [ ]( const value_t& v ) { std::string d; vujb_dump( v, d ); return ui_chl_hash( d ); };
    static const char* const types[ 3 ] = { "Race", "TimeTrial", "Slalom" };
    for ( int k = 0; k < 3; ++k )
        if ( std::string( k_ui_progress_modes[ k ] ) != g_golden_prr_strings[ k ] )
            ++failed;
    const value_t db = c_reader( ui_fnd_bytes( g_golden_prr_db ) ).parse( );
    c_hidden_packages packages;
    packages.init( &db );
    for ( std::size_t k = 0; k < g_golden_prr_runs_count; ++k )
    {
        const golden_prr_run_t& run = g_golden_prr_runs[ k ];
        value_t document;
        if ( run.m_doc[ 0 ] )
            document = c_reader( ui_fnd_bytes( run.m_doc ) ).parse( );
        for ( int t = 0; t < run.m_steps; ++t )
        {
            const golden_prr_step_t& g = g_golden_prr_steps[ run.m_first + t ];
            const std::string op = g.m_op;
            const char* type = types[ g.m_ty ];
            const std::string course = g.m_ty == 2 ? std::string( g.m_course ) : std::string( "None" );
            const float t1 = std::bit_cast< float >( g.m_t1 ), t2 = std::bit_cast< float >( g.m_t2 );
            std::uint32_t ret = 0;
            std::int32_t a = 0x11111111, b = 0x22222222, c = 0x33333333;
            float fb = std::bit_cast< float >( 0x22222222u ), fc = std::bit_cast< float >( 0x33333333u );
            std::string log;
            bool floats = false;
            const auto commit = [ & ] { log += log.empty( ) ? "commit" : ";commit"; };
            const auto achievement = [ & ]( const char* name, float v ) {
                char text[ 64 ];
                const std::uint32_t bits = std::bit_cast< std::uint32_t >( v );
                std::snprintf( text, sizeof( text ), "%sach %s %02x%02x%02x%02x", log.empty( ) ? "" : ";", name, bits & 255u, ( bits >> 8 ) & 255u, ( bits >> 16 ) & 255u, bits >> 24 );
                log += text;
            };
            if ( op == "put" )
                ui_progress_add( document, type, g.m_track, course, g.m_key, g.m_idx, g.m_place, t1, t2 );
            else if ( op == "get" )
            {
                ret = ui_progress_record( document, type, g.m_track, g.m_key, g.m_idx, a, fb, fc );
                floats = true;
            }
            else if ( op == "best" )
            {
                ret = ui_progress_best( document, type, g.m_track, course, g.m_idx, a, fb, fc );
                floats = true;
            }
            else if ( op == "bestall" )
            {
                ret = ui_progress_best_any( document, type, g.m_track, g.m_course, a, fb, fc );
                floats = true;
            }
            else if ( op == "count" )
                ret = static_cast< std::uint32_t >( ui_progress_count( document, type ) );
            else if ( op == "medal" )
                ui_progress_medals( document, type, g.m_idx, a, b, c );
            else if ( op == "gput" )
                ui_progress_ghost_add( document, g.m_track, g.m_place, t1 );
            else if ( op == "gget" )
            {
                float out = std::bit_cast< float >( 0x11111111u );
                ret = ui_progress_ghost_time( document, g.m_track, out );
                a = std::bit_cast< std::int32_t >( out );
            }
            else if ( op == "found" )
                {
                packages.save = commit;
                packages.progress = achievement;
                packages.set_found( document, g.m_name );
            }
            else if ( op == "isfound" )
                ret = c_hidden_packages::is_found( document, g.m_name );
            else if ( op == "inlevel" )
                ret = packages.level_total( g.m_track, a );
            else if ( op == "inlevelfound" )
                c_hidden_packages::level_found( document, g.m_track, a );
            else if ( op == "total" )
                a = c_hidden_packages::found_total( document );
            else if ( op == "all" )
                ret = packages.level_complete( document, g.m_track );
            else if ( op == "clear" )
                c_hidden_packages::clear( document );
            if ( floats )
            {
                b = std::bit_cast< std::int32_t >( fb );
                c = std::bit_cast< std::int32_t >( fc );
            }
            ++ops;
            if ( ( ret != g.m_ret || static_cast< std::uint32_t >( a ) != g.m_a || static_cast< std::uint32_t >( b ) != g.m_b || static_cast< std::uint32_t >( c ) != g.m_c ||
                   log != g.m_log || hash( document ) != g.m_doc ) &&
                 ++failed <= 6 )
            {
                std::string d;
                vujb_dump( document, d );
                LOG_ERROR( "progress_race run %zu op %d %s: ret %X / %X, a %X / %X, b %X / %X, c %X / %X, log '%s' / '%s', doc %s", k, t, g.m_op, ret, g.m_ret, a, g.m_a, b, g.m_b, c, g.m_c,
                           log.c_str( ), g.m_log, hash( document ) == g.m_doc ? "same" : d.c_str( ) );
            }
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui/progress.hxx + game/packages.hxx match retail's VuProgressManagerImpl slots 5 .. 21, 30, 31 and VuHiddenPackageManagerImpl slots 7 .. 12, 6: %d ops on %zu runs", ops, g_golden_prr_runs_count );
    else
        LOG_ERROR( "progress_race FAILED %d of %d", failed, ops );
    return failed == 0;
}
