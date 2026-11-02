#pragma once

#include <workspace/core/ui/bind.hxx>
#include <workspace/core/ui/board.hxx>
#include <workspace/core/ui/chooser.hxx>
#include <workspace/core/ui/ladder.hxx>
#include <workspace/core/ui/results.hxx>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// The UI area's race results checks (batch 20; included by checks/leaves.hxx before
// verify_ui_leaves_all, which runs verify_ui_results_all). Everything ui.hxx / menu.hxx
// / leaves.hxx / widgets.hxx define above is usable.

// One recorded runtime state (+0x330, +0x34C, the pad array's size / capacity / entries).
inline bool ui_rr_state_same( const ui_results_table_t& t, const golden_rr_state_t& s )
{
    if ( t.m_championship != s.m_champ || t.m_active_rows != s.m_active || t.m_size != s.m_size || t.m_capacity != s.m_capacity ||
         s.m_pads != ( s.m_size < 64 ? s.m_size : 64 ) )
        return false;
    for ( int k = 0; k < s.m_pads; ++k )
        if ( static_cast< std::uint32_t >( t.m_pads[ static_cast< std::size_t >( k ) ] ) != g_golden_rr_pads[ s.m_first_pad + k ] )
            return false;
    return true;
}

// verify_ui_race_results: retail's VuRaceResultsTableEntity slot 6 0x469B10, draw
// 0x46A500, layout draw 0x469BD0 and its VuRetVal plugs 0x469B70 / 0x469BB0 in one
// sequence per authored table (oracle case race_results), and VuUserNextUIAction slot 12
// 0x470E50. The fields from OUR loaded entity (ui_results_table_of; each compared with
// retail's bytes first), the six fonts' params 0..40 from OUR load over FontDB (41..59
// retail's - UNKNOWN), the state after the load = OUR ctor state; the game-data root =
// OUR reader (c_reader) on the run's VUJB; the font answers, the track manager and
// VuStringDB slot 6 are the run's (inputs). Per run: commands and records byte for byte
// (one sort per buffer through all runs), the string keys in order, the VuRetVal, the
// state after.
inline bool verify_ui_race_results( )
{
    ui_gfx_sort_t sort;
    ui_lv_sort( sort );
    int failed = 0, runs = 0, commands = 0, strings = 0, fields = 0;
    for ( std::size_t k = 0; k < g_golden_rr_entities_count; ++k )
    {
        const golden_rr_entity_t& ge = g_golden_rr_entities[ k ];
        const entity_t* e = ui_lv_entity( ge.m_project, ge.m_index );
        const std::vector< std::uint8_t > raw = ui_unhex( ge.m_fields );
        bool same = e && e->m_ctor && e->m_ctor->m_vtable == ge.m_vtable && raw.size( ) == 0x300;
        ui_results_table_t t;
        ui_results_inputs_t in;
        if ( same )
        {
            t = ui_results_table_of( *e );
            const auto against = [ & ]( const void* ours, int offset, std::size_t bytes )
            {
                if ( std::memcmp( ours, raw.data( ) + ( offset - 0x60 ), bytes ) != 0 )
                    same = false;
                ++fields;
            };
            against( t.m_scale, 0x64, 8 );
            against( t.m_pos, 0x6C, 8 );
            against( &t.m_highlight, 0x74, 4 );
            const int rects[ 6 ] = { 0x78, 0xEC, 0x160, 0x1D4, 0x248, 0x2BC };
            for ( int c = 0; c < 6; ++c )
                against( t.m_rect[ c ], rects[ c ], 16 );
            against( &t.m_width, 0x334, 4 );
            against( &t.m_spacing, 0x338, 4 );
            against( &t.m_header, 0x33C, 4 );
            against( &t.m_inactive, 0x340, 4 );
            against( &t.m_active, 0x344, 4 );
            against( &t.m_player, 0x348, 4 );
            same = same && ui_lv_bits( t.m_depth ) == ge.m_depth && ui_rr_state_same( t, ge.m_initial );
            for ( int c = 0; same && c < 6; ++c )
            {
                const golden_rr_font_t& gf = g_golden_rr_font_rows[ ge.m_first_font + c ];
                const ui_slot_t* name = ui_property_value( *e, k_ui_results_font_names[ c ] );
                const std::vector< std::uint8_t > want = ui_unhex( gf.m_params );
                same = name && name->m_text == gf.m_name && want.size( ) == 60;
                if ( !same )
                    break;
                std::uint8_t* params = in.m_fonts[ c ].m_params;
                std::memcpy( params, want.data( ), 60 );   // 41..59: retail's (UNKNOWN)
                std::memset( params, 0, 41 );
                std::string asset;
                ui_font_params( ui_font_db( 0 ), name->m_text, params, asset );
                same = std::memcmp( params, want.data( ), 41 ) == 0;
                ++fields;
            }
        }
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui race results: %s entity %d: OUR loaded fields / fonts / ctor state differ from retail's", ge.m_project, ge.m_index );
            continue;
        }
        std::vector< std::string > asked;
        bool flag_answer[ 2 ] = { false, false };
        in.m_string = [ & ]( const std::string& id ) { asked.push_back( id ); return id + "~"; };
        for ( int r = 0; r < ge.m_runs; ++r )
        {
            const golden_rr_run_t& run = g_golden_rr_runs[ ge.m_first + r ];
            const golden_txt_env_t& te = g_golden_txt_envs[ run.m_env ];
            ui_font_draw_env_t env;
            const std::vector< std::uint8_t > matrix = ui_unhex( run.m_matrix );
            std::memcpy( env.m_matrix, matrix.data( ), 64 );
            std::memcpy( &env.m_top, &te.m_top, 4 );
            for ( int m = 0; m < 2; ++m )
            {
                env.m_material[ m ] = 0x5D001000u + 0x800u * static_cast< std::uint32_t >( m );   // TxtWorld's font materials
                env.m_material_key[ m ] = g_golden_txt_material_keys[ m ];
            }
            env.m_texture_material = g_golden_rr_material[ 0 ];
            env.m_texture_material_key = g_golden_rr_material[ 1 ];
            env.m_rect_material = g_golden_rr_rect_material[ 0 ];
            env.m_rect_material_key = g_golden_rr_rect_material[ 1 ];
            for ( int c = 0; c < 6; ++c )   // 0x626B80: the asset's font, else VuUIImpl's default where the environment has one
                in.m_fonts[ c ].m_font = run.m_fonts[ c ] ? g_golden_rr_fonts[ 0 ] : te.m_default ? g_golden_rr_fonts[ 1 ] : 0u;
            const std::vector< std::uint8_t > blob = ui_unhex( run.m_blob );
            const value_t root = blob.empty( ) ? value_t( ) : c_reader( blob ).parse( );
            asked.clear( );
            sort.m_index = te.m_index;
            sort.m_key_lo = te.m_key_lo;
            sort.m_key_hi = te.m_key_hi;
            std::vector< ui_gfx_command_t >& list = sort.m_commands[ te.m_index ];
            const std::size_t before = list.size( );
            bool ok = true;
            if ( run.m_op == 0 )
                ui_results_initialize( t, root, nullptr );   // dev config g 0x75901C null: the shipping game
            else if ( run.m_op == 1 )
                ui_results_draw( sort, env, t, in, root );
            else if ( run.m_op == 2 )
                ui_results_layout_draw( sort, env, t, in );
            else
            {
                flag_answer[ 0 ] = run.m_track[ 0 ] != 0;
                flag_answer[ 1 ] = run.m_track[ 1 ] != 0;
                const bool v = run.m_op == 3 ? ui_results_is_circuit( flag_answer[ 0 ], flag_answer[ 1 ] ) : ui_results_points_shown( t );
                ok = run.m_ret[ 0 ] == 3u && ( run.m_ret[ 1 ] & 0xFFu ) == ( v ? 1u : 0u );
            }
            ok = ok && list.size( ) - before == static_cast< std::size_t >( run.m_commands ) && static_cast< int >( asked.size( ) ) == run.m_strings;
            for ( int q = 0; ok && q < run.m_strings; ++q )
            {
                ok = asked[ static_cast< std::size_t >( q ) ] == g_golden_rr_strings[ run.m_first_string + q ];
                ++strings;
            }
            for ( int c = 0; ok && c < run.m_commands; ++c )
            {
                const golden_txt_command_t& want = g_golden_rr_commands[ run.m_first + c ];
                const ui_gfx_command_t& h = list[ before + static_cast< std::size_t >( c ) ];
                const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
                const auto& memory = sort.m_buffers[ te.m_index ].m_memory;
                ok = std::memcmp( h.m_words, want.m_words, sizeof( h.m_words ) ) == 0 && h.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                     std::memcmp( memory.data( ) + h.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
                ++commands;
            }
            ok = ok && ui_rr_state_same( t, run.m_state );
            ++runs;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui race results: %s entity %d run %d (op %d): %d commands (retail %d), %d string keys (retail %d) - or a word / record / "
                           "the state differs", ge.m_project, ge.m_index, r, run.m_op, static_cast< int >( list.size( ) - before ), run.m_commands,
                           static_cast< int >( asked.size( ) ), run.m_strings );
        }
    }
    int nexts = 0;
    for ( std::size_t k = 0; k < g_golden_rr_nexts_count; ++k )
    {
        const golden_rr_next_t& gn = g_golden_rr_nexts[ k ];
        std::vector< std::pair< std::uint32_t, int > > sent;
        const std::uint32_t ret = ui_user_next( [ &sent ]( std::uint32_t hash, const ui_params_t& p ) { sent.push_back( { hash, p.m_size } ); } );
        const bool ok = gn.m_slot12 == 0x470E50u && gn.m_broadcasts == 1 && sent.size( ) == 1 && sent[ 0 ].first == gn.m_hash &&
                        sent[ 0 ].second == gn.m_params_size && ret == gn.m_ret[ 0 ];
        ++nexts;
        if ( !ok && ++failed <= 8 )
            LOG_ERROR( "ui race results: %s VuUserNextUIAction %d: the broadcast / VuRetVal differs from retail's 0x470E50", gn.m_project, gn.m_index );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_results_initialize / ui_results_draw / ui_results_layout_draw / the plugs match retail's VuRaceResultsTableEntity 0x469B10 / "
                     "0x46A500 / 0x469BD0 / 0x469B70 / 0x469BB0 on %d tables (%d runs; %d of OUR loaded fields / fonts): %d commands byte for byte, "
                     "%d string keys, the state after each; VuUserNextUIAction 0x470E50 on %d actions",
            static_cast< int >( g_golden_rr_entities_count ), runs, fields, commands, strings, nexts );
    else
        LOG_ERROR( "the UI race results table FAILED %d checks against retail's", failed );
    return failed == 0;
}

// verify_ui_subtitles_combo: retail's VuUISubtitlesSettingEntity slot 6 0x46D7A0, its
// touch callbacks 0x474F80 / 0x475080 / 0x475090, slots 13 / 15 / 16 (with the two
// choices: the change 0x475B40 and slot 12 0x46D9B0 run) and slot 7 0x4253D0 through
// one script per authored instance (oracle case subtitles_combo). The fields from OUR
// loaded entity (ui_combo_box_fill; compared with retail's bytes first); the touch
// device, the settings byte, the tick manager's delta, VuUIImpl's inverse crop matrix
// and the NavUp / NavDown connection counts are the case's (inputs). Per step: the
// state +0x2A0..+0x2BB, the choices, the device's entries and +0x1C, the settings
// byte, the VuRetVal of SetFocus, the hook log in order.
inline bool verify_ui_subtitles_combo( )
{
    int failed = 0, steps = 0, logs = 0, dispatched = 0;
    // Batch 25: pass 1 again with the touch ops through the device's dispatch (controls.hxx
    // ui_touch_dispatch_down / _up / _move; the move's point in the device's touch 0, as the
    // platform's pointer-moved 0x4DB100 leaves it) reaching the combo through OUR handlers
    // (ui_touch_handlers - the binding's glue): every step must come out the same.
    for ( int pass = 0; pass < 2; ++pass )
    for ( std::size_t k = 0; k < g_golden_sc_entities_count; ++k )
    {
        const golden_sc_entity_t& ge = g_golden_sc_entities[ k ];
        const entity_t* e = ui_lv_entity( ge.m_project, ge.m_index );
        const std::vector< std::uint8_t > raw = ui_unhex( ge.m_fields );
        std::vector< std::string > log;
        ui_settings_t settings;
        ui_touch_device_t device;
        float inverse_now[ 16 ] = { };
        int faults = 0;
        ui_combo_box_hooks_t hooks;
        char line[ 160 ];
        hooks.m_broadcast = [ & ]( std::uint32_t hash, const ui_params_t& p )
        {
            std::snprintf( line, sizeof line, "broadcast|0x%08X|%d", hash, p.m_size );
            log.push_back( line );
        };
        hooks.m_fire = [ & ]( const char* plug, const ui_params_t& p )
        {
            log.push_back( std::string( "find|" ) + plug );
            log.push_back( "fire|" + std::to_string( p.m_size ) );
        };
        hooks.m_connections = [ & ]( const char* plug )
        {
            log.push_back( std::string( "find|" ) + plug );
            return std::strcmp( plug, "NavUp" ) == 0 ? ge.m_links[ 0 ] : std::strcmp( plug, "NavDown" ) == 0 ? ge.m_links[ 1 ] : 0;
        };
        hooks.m_sound = [ & ]( const char* name ) { log.push_back( std::string( "sound|" ) + name ); };
        hooks.m_delta = [ ] { return ui_lv_float( g_golden_sc_delta ); };
        hooks.m_apply = [ & ]( std::uint32_t, int current ) { settings.m_subtitles = current != 0; };
        c_ui_combo_box c( 0u, k_ui_subtitles_vtable, hooks );
        if ( pass )
            ui_touch_handlers( device, &c, [ &inverse_now ] { return static_cast< const float* >( inverse_now ); }, faults );
        bool same = e && e->m_ctor && e->m_ctor->m_vtable == k_ui_subtitles_vtable && raw.size( ) == 0x260;
        if ( same )
        {
            ui_combo_box_fill( c, *e );
            same = std::memcmp( c.m_scale, raw.data( ) + 0x08, 8 ) == 0 && std::memcmp( c.m_position, raw.data( ) + 0x10, 8 ) == 0 &&
                   std::memcmp( c.m_left, raw.data( ) + 0x1F8, 16 ) == 0 && std::memcmp( c.m_right, raw.data( ) + 0x208, 16 ) == 0 &&
                   std::memcmp( &c.m_scroll_time, raw.data( ) + 0x138, 4 ) == 0;   // +0x68, +0x70, +0x258, +0x268, block +0x120 = +0x198
        }
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui subtitles combo: %s entity %d: OUR loaded fields differ from retail's", ge.m_project, ge.m_index );
            continue;
        }
        for ( int s = 0; s < ge.m_steps; ++s )
        {
            const golden_sc_step_t& st = g_golden_sc_steps[ ge.m_first + s ];
            const std::vector< std::uint8_t > inverse_raw = ui_unhex( g_golden_sc_inverses[ st.m_env ] );
            float inverse[ 16 ];
            std::memcpy( inverse, inverse_raw.data( ), 64 );
            std::memcpy( inverse_now, inverse, 64 );
            const float point[ 2 ] = { ui_lv_float( st.m_point[ 0 ] ), ui_lv_float( st.m_point[ 1 ] ) };
            log.clear( );
            std::uint32_t ret[ 2 ] = { 0xCCCCCCCCu, 0xCCCCCCCCu };
            const int touch_op = pass ? st.m_op : -1;
            if ( touch_op == 4 )
                ui_touch_dispatch_down( device, 1u, point[ 0 ], point[ 1 ] );
            else if ( touch_op == 5 )
                ui_touch_dispatch_up( device, 1u, 0.0f, 0.0f );
            else if ( touch_op == 6 )
            {
                log.push_back( "touch|0" );   // the device's slot 7 (0, out)
                if ( device.m_touches.empty( ) )
                    device.m_touches.resize( 1 );
                device.m_touches[ 0 ].m_point[ 0 ] = point[ 0 ];
                device.m_touches[ 0 ].m_point[ 1 ] = point[ 1 ];
                ui_touch_dispatch_move( device );
            }
            if ( touch_op >= 4 && touch_op <= 6 )
                ++dispatched;
            else
            switch ( st.m_op )
            {
            case 0:
                settings.m_subtitles = st.m_byte != 0;
                c.initialize( device, settings.m_subtitles ? 1u : 0u );
                break;
            case 1:
                c.on_tick( );
                break;
            case 2:
                c.set_focus( );
                ret[ 0 ] = 0u;
                break;
            case 3:
            {
                ui_params_t p;
                p.add_string( st.m_event );
                if ( std::strcmp( st.m_event, "InputUp" ) == 0 || std::strcmp( st.m_event, "InputDown" ) == 0 )
                    ui_params_add( p, 1, 0u );
                c.on_event( p );
                break;
            }
            case 4:
                c.touch_down( inverse, point );
                break;
            case 5:
                c.touch_up( );
                break;
            case 6:
                log.push_back( "touch|0" );   // the device's slot 7 (0, out)
                c.touch_move( inverse, point );
                break;
            default:
                c.release( device );
                break;
            }
            std::uint8_t state[ 0x1C ] = { };
            state[ 0 ] = c.m_latched;
            state[ 1 ] = c.m_left_pressed;
            state[ 2 ] = c.m_right_pressed;
            std::memcpy( state + 4, &c.m_current, 4 );
            std::memcpy( state + 8, &c.m_shown, 4 );
            state[ 0xC ] = c.m_focused;
            state[ 0xD ] = c.m_down;
            state[ 0xE ] = c.m_held;
            state[ 0xF ] = c.m_up;
            std::memcpy( state + 0x10, c.m_touch, 8 );
            std::memcpy( state + 0x18, &c.m_press, 4 );
            const std::vector< std::uint8_t > want = ui_unhex( st.m_state );
            bool ok = want.size( ) == sizeof state && std::memcmp( state, want.data( ), sizeof state ) == 0;
            ok = ok && static_cast< int >( c.m_choice_texts.size( ) ) == st.m_choices && c.m_choices == st.m_choices;
            for ( int q = 0; ok && q < st.m_choices; ++q )
            {
                const golden_sc_choice_t& gc = g_golden_sc_choices[ st.m_first_choice + q ];
                ok = c.m_choice_texts[ static_cast< std::size_t >( q ) ].first == gc.m_text &&
                     c.m_choice_texts[ static_cast< std::size_t >( q ) ].second == gc.m_byte;
            }
            ok = ok && static_cast< int >( device.m_callbacks.size( ) ) == st.m_entries && device.m_priority == st.m_priority;
            if ( ok && st.m_entries )
                ok = device.m_callbacks[ 0 ].first == &c && st.m_entry_offset == 0x5C && device.m_callbacks[ 0 ].second == st.m_entry_priority;
            ok = ok && device.m_unhandled == 0 && faults == 0;
            ok = ok && ( settings.m_subtitles ? 1 : 0 ) == st.m_settings && ret[ 0 ] == st.m_ret[ 0 ] && static_cast< int >( log.size( ) ) == st.m_logs;
            for ( int q = 0; ok && q < st.m_logs; ++q )
            {
                ok = log[ static_cast< std::size_t >( q ) ] == g_golden_sc_logs[ st.m_first_log + q ];
                ++logs;
            }
            ++steps;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui subtitles combo: %s entity %d step %d (op %d, pass %d): the state / choices / touch entries / settings / log differ (log %d, retail %d)",
                    ge.m_project, ge.m_index, s, st.m_op, pass, static_cast< int >( log.size( ) ), st.m_logs );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "c_ui_combo_box initialize / touch_down / touch_up / touch_move / release (+ the tick, SetFocus, events, change) match retail's "
                     "VuUISubtitlesSettingEntity 0x46D7A0 / 0x474F80 / 0x475080 / 0x475090 / 0x4253D0 on %d entities: %d steps (both passes; the "
                     "second's %d touch ops through the device's dispatch 0x5393E0 / 0x5394A0 / 0x53FB40 and OUR handlers), %d hook calls in order",
            static_cast< int >( g_golden_sc_entities_count ), steps, dispatched, logs );
    else
        LOG_ERROR( "the UI subtitles combo box FAILED %d checks against retail's", failed );
    return failed == 0;
}

// verify_ui_settings_combos (batch 24): the other five settings combos - units
// 0x46D3E0 / 0x46D640, fidelity 0x46DA70 / 0x46DCD0 + the setter 0x4B50A0, flip
// interval 0x46DE30 / 0x46E050, vibration 0x46CD70 / 0x46CF70, tilt steering
// 0x46C780 / 0x46C9B0 - through one script per authored instance (oracle case
// settings_combos): c_ui_combo_box::initialize_settings / apply_settings with the
// base's tick / SetFocus / events / change / release. The fields from OUR loaded
// entity (compared with retail's bytes first); the fake world per slot 6 (the
// settings, g_vumetrosys's two answers, VuGfxSort +0xC) is the case's. Per step:
// the state +0x2A0..+0x2BB, the choices, the touch entries and +0x1C, the settings
// fields, VuWater +0x40..+0x47, display +0x11, VuGfxSort +0xC, the hook log.
inline bool verify_ui_settings_combos( )
{
    int failed = 0, steps = 0, logs = 0;
    for ( std::size_t k = 0; k < g_golden_stc_entities_count; ++k )
    {
        const golden_stc_entity_t& ge = g_golden_stc_entities[ k ];
        const entity_t* e = ui_lv_entity( ge.m_project, ge.m_index );
        const std::vector< std::uint8_t > raw = ui_unhex( ge.m_fields );
        std::vector< std::string > log;
        ui_settings_t settings;
        ui_settings_platform_t platform;
        ui_touch_device_t device;
        ui_combo_box_hooks_t hooks;
        char line[ 160 ];
        int sys[ 2 ] = { 0, 0 };
        const auto broadcast = [ & ]( std::uint32_t hash, const ui_params_t& p )
        {
            std::snprintf( line, sizeof line, "broadcast|0x%08X|%d", hash, p.m_size );
            log.push_back( line );
        };
        hooks.m_broadcast = broadcast;
        platform.m_broadcast = broadcast;
        platform.m_sys_54 = [ & ] { log.push_back( "sys54" ); return sys[ 0 ] != 0; };
        platform.m_sys_58 = [ & ] { log.push_back( "sys58" ); return sys[ 1 ] != 0; };
        platform.m_flip = [ & ]( int v ) { log.push_back( "flip|" + std::to_string( v ) ); };
        platform.m_flush = [ & ]
        {
            log.push_back( "gfx3C|0x5A5A0001|0xFFFFFFFF" );   // the case's +0xA0 handle, -1
            log.push_back( "d3d30" );
            log.push_back( "d3dD0|0" );                        // the byte already cleared
        };
        platform.m_lens = [ & ]( bool high ) { log.push_back( std::string( "lens|" ) + ( high ? "1" : "0" ) ); };
        hooks.m_fire = [ & ]( const char* plug, const ui_params_t& p )
        {
            log.push_back( std::string( "find|" ) + plug );
            log.push_back( "fire|" + std::to_string( p.m_size ) );
        };
        hooks.m_connections = [ & ]( const char* plug )
        {
            log.push_back( std::string( "find|" ) + plug );
            return std::strcmp( plug, "NavUp" ) == 0 ? ge.m_links[ 0 ] : std::strcmp( plug, "NavDown" ) == 0 ? ge.m_links[ 1 ] : 0;
        };
        hooks.m_sound = [ & ]( const char* name ) { log.push_back( std::string( "sound|" ) + name ); };
        hooks.m_delta = [ ] { return ui_lv_float( g_golden_stc_delta ); };
        c_ui_combo_box c( 0u, ge.m_vtable, hooks );
        bool faults = false;
        hooks.m_apply = [ & ]( std::uint32_t, int ) { faults |= !c.apply_settings( settings, platform ); };
        bool same = e && e->m_ctor && e->m_ctor->m_vtable == ge.m_vtable && raw.size( ) == 0x260;
        if ( same )
        {
            ui_combo_box_fill( c, *e );
            same = std::memcmp( c.m_scale, raw.data( ) + 0x08, 8 ) == 0 && std::memcmp( c.m_position, raw.data( ) + 0x10, 8 ) == 0 &&
                   std::memcmp( c.m_left, raw.data( ) + 0x1F8, 16 ) == 0 && std::memcmp( c.m_right, raw.data( ) + 0x208, 16 ) == 0 &&
                   std::memcmp( &c.m_scroll_time, raw.data( ) + 0x138, 4 ) == 0;
        }
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui settings combos: %s entity %d: OUR loaded fields differ from retail's", ge.m_project, ge.m_index );
            continue;
        }
        for ( int s = 0; s < ge.m_steps; ++s )
        {
            const golden_stc_step_t& st = g_golden_stc_steps[ ge.m_first + s ];
            log.clear( );
            std::uint32_t ret[ 2 ] = { 0xCCCCCCCCu, 0xCCCCCCCCu };
            bool known = true;
            switch ( st.m_op )
            {
            case 0:
            {
                const golden_stc_state_t& w = g_golden_stc_states[ st.m_state ];
                settings.m_tilt = w.m_tilt != 0;
                settings.m_vibration = w.m_vibration != 0;
                settings.m_units = w.m_units;
                settings.m_fidelity = w.m_fidelity;
                settings.m_flip_interval = w.m_flip;
                sys[ 0 ] = w.m_sys[ 0 ];
                sys[ 1 ] = w.m_sys[ 1 ];
                platform.m_gfx_pending = w.m_gfx != 0;
                known = c.initialize_settings( device, settings, platform );
                break;
            }
            case 1: c.on_tick( ); break;
            case 2:
                c.set_focus( );
                ret[ 0 ] = 0u;
                break;
            case 3:
            {
                ui_params_t p;
                p.add_string( st.m_event );
                c.on_event( p );
                break;
            }
            default: c.release( device ); break;
            }
            std::uint8_t state[ 0x1C ] = { };
            state[ 0 ] = c.m_latched;
            state[ 1 ] = c.m_left_pressed;
            state[ 2 ] = c.m_right_pressed;
            std::memcpy( state + 4, &c.m_current, 4 );
            std::memcpy( state + 8, &c.m_shown, 4 );
            state[ 0xC ] = c.m_focused;
            state[ 0xD ] = c.m_down;
            state[ 0xE ] = c.m_held;
            state[ 0xF ] = c.m_up;
            std::memcpy( state + 0x10, c.m_touch, 8 );
            std::memcpy( state + 0x18, &c.m_press, 4 );
            std::uint8_t water[ 8 ] = { };
            std::memcpy( water, &platform.m_water_scale, 4 );
            water[ 4 ] = platform.m_water_44;
            water[ 5 ] = platform.m_water_45;
            water[ 6 ] = platform.m_water_46;
            const std::vector< std::uint8_t > want = ui_unhex( st.m_bytes );
            const std::vector< std::uint8_t > want_water = ui_unhex( st.m_water );
            bool ok = known && !faults && want.size( ) == sizeof state && std::memcmp( state, want.data( ), sizeof state ) == 0;
            ok = ok && want_water.size( ) == sizeof water && std::memcmp( water, want_water.data( ), sizeof water ) == 0;
            ok = ok && platform.m_display_11 == st.m_display && ( platform.m_gfx_pending ? 1 : 0 ) == st.m_gfx;
            const golden_stc_settings_t& gs = st.m_settings;
            ok = ok && ( settings.m_tilt ? 1 : 0 ) == gs.m_tilt && ( settings.m_vibration ? 1 : 0 ) == gs.m_vibration && settings.m_units == gs.m_units &&
                 settings.m_fidelity == gs.m_fidelity && settings.m_flip_interval == gs.m_flip && ( settings.m_subtitles ? 1 : 0 ) == gs.m_subtitles;
            ok = ok && static_cast< int >( c.m_choice_texts.size( ) ) == st.m_choices && c.m_choices == st.m_choices;
            for ( int q = 0; ok && q < st.m_choices; ++q )
            {
                const golden_sc_choice_t& gc = g_golden_stc_choices[ st.m_first_choice + q ];
                ok = c.m_choice_texts[ static_cast< std::size_t >( q ) ].first == gc.m_text &&
                     c.m_choice_texts[ static_cast< std::size_t >( q ) ].second == gc.m_byte;
            }
            ok = ok && static_cast< int >( device.m_callbacks.size( ) ) == st.m_entries && device.m_priority == st.m_priority;
            if ( ok && st.m_entries )
                ok = device.m_callbacks[ 0 ].first == &c && st.m_entry_offset == 0x5C && device.m_callbacks[ 0 ].second == st.m_entry_priority;
            ok = ok && ret[ 0 ] == st.m_ret[ 0 ] && static_cast< int >( log.size( ) ) == st.m_logs;
            for ( int q = 0; ok && q < st.m_logs; ++q )
            {
                ok = log[ static_cast< std::size_t >( q ) ] == g_golden_stc_logs[ st.m_first_log + q ];
                ++logs;
            }
            ++steps;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui settings combos: %s entity %d (vtable 0x%X) step %d (op %d): the state / choices / settings / world / log differ (log %d, retail %d)",
                    ge.m_project, ge.m_index, ge.m_vtable, s, st.m_op, static_cast< int >( log.size( ) ), st.m_logs );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "c_ui_combo_box initialize_settings / apply_settings (+ ui_settings_set_fidelity) match retail's settings combos (units 0x46D3E0 / "
                     "0x46D640, fidelity 0x46DA70 / 0x46DCD0 / 0x4B50A0, flip 0x46DE30 / 0x46E050, vibration 0x46CD70 / 0x46CF70, tilt 0x46C780 / "
                     "0x46C9B0) on %d entities: %d steps, %d hook calls in order",
            static_cast< int >( g_golden_stc_entities_count ), steps, logs );
    else
        LOG_ERROR( "the UI settings combos FAILED %d checks against retail's", failed );
    return failed == 0;
}

// verify_ui_touch_dispatch (batch 24): controls.hxx's touch device - ui_touch_dispatch_
// down / up / move, ui_touch_count, ui_touch_point with ui_touch_add / set_priority /
// remove - against retail's VuMetroTouch 0x5393E0 / 0x5394A0, VuTouch 0x53FB40, slots
// 6 0x539210 / 7 0x539230, 0x53FB90 / 0x53FAE0 / 0x53FA60 (oracle case touch_dispatch)
// on one script: the callbacks' slot calls in order (callback, slot, the point bits),
// the first list, +0x1C, the count, the touches' first 8 entries, the answers.
inline bool verify_ui_touch_dispatch( )
{
    ui_touch_device_t d;
    std::vector< int > tokens( static_cast< std::size_t >( g_golden_tdc_callbacks ) );
    const auto cb = [ &tokens ]( int k ) -> const void* { return &tokens[ static_cast< std::size_t >( k ) ]; };
    const auto index_of = [ & ]( const void* p )
    {
        for ( int k = 0; k < g_golden_tdc_callbacks; ++k )
            if ( cb( k ) == p )
                return k;
        return -1;
    };
    std::vector< std::string > log;
    char line[ 96 ];
    for ( int k = 0; k < g_golden_tdc_callbacks; ++k )
    {
        ui_touch_handler_t h;
        const auto point_call = [ &log, &line, k ]( int slot, const float p[ 2 ] )
        {
            std::uint32_t x, y;
            std::memcpy( &x, &p[ 0 ], 4 );
            std::memcpy( &y, &p[ 1 ], 4 );
            std::snprintf( line, sizeof line, "cb|%d|%d|0x%08X|0x%08X", k, slot, x, y );
            log.push_back( line );
        };
        h.m_down = [ point_call ]( const float p[ 2 ] ) { point_call( 0, p ); };
        h.m_up = [ point_call ]( const float p[ 2 ] ) { point_call( 1, p ); };
        h.m_move = [ &log, k ] { log.push_back( "cb|" + std::to_string( k ) + "|2" ); };
        d.m_handlers[ cb( k ) ] = h;
    }
    for ( std::size_t k = 0; k < g_golden_tdc_low_count; ++k )
        d.m_low_level.push_back( { cb( g_golden_tdc_low[ k ].m_callback ), g_golden_tdc_low[ k ].m_priority } );
    int failed = 0, logs = 0;
    for ( std::size_t s = 0; s < g_golden_tdc_steps_count; ++s )
    {
        const golden_tdc_step_t& st = g_golden_tdc_steps[ s ];
        log.clear( );
        std::uint32_t answer[ 2 ] = { 0u, 0u };
        int answers = 0;
        bool known = true;
        const float x = ui_lv_float( st.m_point[ 0 ] ), y = ui_lv_float( st.m_point[ 1 ] );
        switch ( st.m_op )
        {
        case 0: ui_touch_add( d, cb( st.m_a ), static_cast< std::uint32_t >( st.m_b ) ); break;
        case 1: ui_touch_set_priority( d, cb( st.m_a ), static_cast< std::uint32_t >( st.m_b ) ); break;
        case 2: ui_touch_remove( d, cb( st.m_a ) ); break;
        case 3: ui_touch_dispatch_down( d, static_cast< std::uint32_t >( st.m_a ), x, y ); break;
        case 4: ui_touch_dispatch_up( d, static_cast< std::uint32_t >( st.m_a ), x, y ); break;
        case 5: ui_touch_dispatch_move( d ); break;
        case 6:
            answer[ 0 ] = static_cast< std::uint32_t >( ui_touch_count( d, static_cast< std::uint32_t >( st.m_a ) ) );
            answers = 1;
            break;
        default:
        {
            float p[ 2 ];
            known = ui_touch_point( d, static_cast< std::uint32_t >( st.m_a ), p );
            std::memcpy( answer, p, 8 );
            answers = 2;
            break;
        }
        }
        // the array's first 8 entries as retail's bytes (entries never written: the case's zeroed storage)
        std::vector< std::uint8_t > array( 12 * 8, 0u );
        for ( std::size_t k = 0; k < d.m_touches.size( ) && k < 8; ++k )
        {
            std::memcpy( array.data( ) + 12 * k, &d.m_touches[ k ].m_id, 4 );
            std::memcpy( array.data( ) + 12 * k + 4, d.m_touches[ k ].m_point, 8 );
        }
        bool ok = known && d.m_unhandled == 0 && array == ui_unhex( st.m_array ) && d.m_priority == st.m_priority && d.m_count == st.m_count &&
                  answers == st.m_answers && ( answers < 1 || answer[ 0 ] == st.m_answer[ 0 ] ) && ( answers < 2 || answer[ 1 ] == st.m_answer[ 1 ] );
        ok = ok && static_cast< int >( d.m_callbacks.size( ) ) == st.m_pairs;
        for ( int k = 0; ok && k < st.m_pairs; ++k )
            ok = index_of( d.m_callbacks[ static_cast< std::size_t >( k ) ].first ) == g_golden_tdc_pairs[ st.m_first_pair + k ].m_callback &&
                 d.m_callbacks[ static_cast< std::size_t >( k ) ].second == g_golden_tdc_pairs[ st.m_first_pair + k ].m_priority;
        ok = ok && static_cast< int >( log.size( ) ) == st.m_logs;
        for ( int k = 0; ok && k < st.m_logs; ++k )
        {
            ok = log[ static_cast< std::size_t >( k ) ] == g_golden_tdc_logs[ st.m_first_log + k ];
            ++logs;
        }
        if ( !ok && ++failed <= 8 )
            LOG_ERROR( "ui touch dispatch: step %d (op %d) differs from retail's (log %d, retail %d)", static_cast< int >( s ), st.m_op,
                static_cast< int >( log.size( ) ), st.m_logs );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_touch_dispatch_down / up / move, ui_touch_count / ui_touch_point (+ add / priority / remove) match retail's VuMetroTouch "
                     "0x5393E0 / 0x5394A0, 0x53FB40, slots 6 0x539210 / 7 0x539230 on %d steps: %d callback calls in order, the lists / touches after each",
            static_cast< int >( g_golden_tdc_steps_count ), logs );
    else
        LOG_ERROR( "the UI touch dispatch FAILED %d checks against retail's", failed );
    return failed == 0;
}

// verify_ui_screen_game_init (batch 21): c_ui_binding::project_initialize /
// project_release on OUR loaded Screens projects (the scripted world, as
// verify_ui_frames) against retail's 0x500B10 + 0x501260 / 0x500B80 (oracle case
// screen_game_init): the repository adds / removes and every non-nullsub slot 6 /
// 7 entered (owner, address) in order, the OnProjectInitialized handlers entered,
// then the touch device's entries (owner, priority) and +0x1C, the subtitles
// combos' choices / +0x2A4 / +0x2A8 and every entity's flags & 4 after each
// phase; and each VuUserNextUIAction's Trigger input through the binding's graph
// (c_ui_wiring::input): the broadcasts and the VuRetVal. Retail stubbed the screen's
// 0x629A70 / 0x629B00 (case ui_frames runs them): OUR enter / exit run, so the
// graph's own traces in the init / release phases are not compared (only the
// kinds above; the events only 0x5EFA60).
inline bool verify_ui_screen_game_init( )
{
    const ui_loaded_t& base = ui_loaded( );
    std::vector< std::string > names( g_golden_entity_templates, g_golden_entity_templates + g_golden_entity_templates_count );
    static const entity_registry_t registry = build_entity_registry( names );
    const std::map< std::string, value_t >* templates = &base.templates;
    const entity_world_t world = ui_scripted_world( registry, [ templates ]( const std::string& name ) -> const value_t*
    {
        const auto found = templates->find( "VuTemplateAsset/" + name );
        return found == templates->end( ) ? nullptr : &found->second;
    } );
    int failed = 0, phases = 0, logs = 0, slots = 0, triggers = 0;
    const auto bad = [ &failed ]( const char* project, int phase, const char* what )
    {
        if ( ++failed <= 8 )
            LOG_ERROR( "ui screen game init: %s phase %d: %s differs from retail's", project, phase, what );
    };
    for ( std::size_t w = 0; w < g_golden_sgi_projects_count; ++w )
    {
        const golden_sgi_project_t& gp = g_golden_sgi_projects[ w ];
        const golden_ui_project_t* project = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, gp.m_asset ) == 0 )
                project = &g_golden_ui_projects[ p ];
        if ( !project )
        {
            bad( gp.m_asset, -1, "the project's presence in the loaded set" );
            continue;
        }
        const unsigned char* raw = reinterpret_cast< const unsigned char* >( project->m_words );
        const value_t document = c_reader( std::vector< std::uint8_t >( raw, raw + project->m_bytes ) ).parse( );
        const std::unique_ptr< entity_t > root = entity_load_project( member( document, "VuProject" ), project->m_path, world );
        if ( !root )
        {
            bad( gp.m_asset, -1, "the load" );
            continue;
        }
        ui_touch_device_t device;
        ui_settings_t settings;
        settings.m_subtitles = gp.m_subtitles != 0;
        ui_bind_hooks_t hooks;
        hooks.m_touch_device = &device;
        hooks.m_settings = &settings;
        // batch 22: a project the case gave a game-data root holds a mini leaderboard - its slot 6 / 7 run live
        // (the leaderboard manager's read and the tick manager's register come out as the binding's traces)
        ui_mini_board_hooks_t leaderboard;
        value_t board_root;
        if ( gp.m_root[ 0 ] )
        {
            board_root = c_reader( ui_unhex( gp.m_root ) ).parse( );
            leaderboard.m_root = &board_root;
            hooks.m_leaderboard = &leaderboard;
        }
        // batch 23: the full leaderboard's slot 6 / 7 run live (the manager's read / count answered 0 as the case's)
        const value_t option_db = ui_cx_document( 4 );
        ui_leaderboard_hooks_t ladder;
        ladder.m_option_db = &option_db;
        ladder.m_total = [ ] { return 0; };
        hooks.m_ladder = &ladder;
        // batch 24: the other five settings combos' slot 6 run live over the case's settings / g_vumetrosys answers
        settings.m_tilt = g_golden_sgi_settings.m_tilt != 0;
        settings.m_vibration = g_golden_sgi_settings.m_vibration != 0;
        settings.m_units = g_golden_sgi_settings.m_units;
        settings.m_fidelity = g_golden_sgi_settings.m_fidelity;
        settings.m_flip_interval = g_golden_sgi_settings.m_flip;
        ui_settings_platform_t platform;
        platform.m_sys_54 = [ ] { return g_golden_sgi_settings.m_sys[ 0 ] != 0; };
        platform.m_sys_58 = [ ] { return g_golden_sgi_settings.m_sys[ 1 ] != 0; };
        hooks.m_settings_platform = &platform;
        c_ui_binding bind( *root, hooks );
        if ( static_cast< int >( bind.m_bound.size( ) ) != gp.m_count )
        {
            bad( gp.m_asset, -1, "the entity count" );
            continue;
        }
        // the case's Focus Priority pokes "index,value;" into OUR loaded values
        for ( const char* at = gp.m_pokes; *at; )
        {
            char* end = nullptr;
            const int index = static_cast< int >( std::strtol( at, &end, 10 ) );
            if ( !end || *end != ',' )
                break;
            const int value = static_cast< int >( std::strtol( end + 1, &end, 10 ) );
            if ( !end || *end != ';' )
                break;
            at = end + 1;
            if ( const ui_slot_t* s = ui_bind_slot( *bind.m_bound[ static_cast< std::size_t >( index ) ]->m_entity, "Focus Priority" ) )
                const_cast< ui_slot_t* >( s )->m_bits[ 0 ] = static_cast< std::uint32_t >( value );
            else
                bad( gp.m_asset, -1, "a poked Focus Priority's presence" );
        }
        const auto owner_of = [ &bind ]( const void* callback )
        {
            for ( const auto& b : bind.m_bound )
                if ( ( b->m_button && b->m_button.get( ) == callback ) || ( b->m_combo && b->m_combo.get( ) == callback ) ||
                     ( b->m_ladder && b->m_ladder.get( ) == callback ) )
                    return b->m_index;
            return -1;
        };
        const auto state = [ & ]
        {
            char word[ 64 ];
            std::string t = "E";
            for ( const auto& c : device.m_callbacks )
            {
                // Batch 29: the page's / rows' entries: the case stubs their slot 6 (0x62D9A0 / 0x461200), so retail's list has none.
                bool stubbed = false;
                for ( const auto& b : bind.m_bound )
                    stubbed = stubbed || ( b->m_page && b->m_page.get( ) == c.first ) || ( b->m_remap && b->m_remap.get( ) == c.first );
                if ( stubbed )
                    continue;
                std::snprintf( word, sizeof word, "%d,0,%u;", owner_of( c.first ), c.second );
                t += word;
            }
            std::snprintf( word, sizeof word, "|P%u|C", device.m_priority );
            t += word;
            for ( const auto& b : bind.m_bound )
                if ( b->m_combo )
                {
                    std::uint32_t bits;
                    std::memcpy( &bits, &b->m_combo->m_shown, 4 );
                    std::snprintf( word, sizeof word, "%d,%d,0x%08X,", b->m_index, b->m_combo->m_current, bits );
                    t += word;
                    for ( const auto& c : b->m_combo->m_choice_texts )
                        t += c.first + ":" + std::to_string( c.second ) + "/";
                    t += ";";
                }
            t += "|F";
            for ( const auto& b : bind.m_bound )
                t += ( b->m_entity->m_flags & 4u ) ? "1" : "0";
            std::string boards;
            for ( const auto& b : bind.m_bound )
                if ( b->m_board )
                {
                    std::snprintf( word, sizeof word, "%d,%d,", b->m_index, b->m_board->m_fsm.m_current );
                    boards += word + b->m_board->m_key + ";";
                }
            if ( !boards.empty( ) )
                t += "|B" + boards;
            std::string ladders;
            for ( const auto& b : bind.m_bound )
                if ( b->m_ladder )
                {
                    std::snprintf( word, sizeof word, "%d,%d,%d;", b->m_index, b->m_ladder->m_fsm.m_current, b->m_ladder->m_first );
                    ladders += word;
                }
            if ( !ladders.empty( ) )
                t += "|L" + ladders;
            return t;
        };
        // the trace from `first` as retail's log lines (init / release: repository, slots, 0x5EFA60; trigger: broadcasts)
        const auto lines = [ &bind ]( std::size_t first, bool trigger )
        {
            std::vector< std::string > out;
            char line[ 96 ];
            for ( std::size_t k = first; k < bind.m_trace.size( ); ++k )
            {
                const ui_trace_t& t = bind.m_trace[ k ];
                line[ 0 ] = 0;
                if ( trigger )
                {
                    if ( t.m_kind == ui_trace_kind_t::broadcast )
                        std::snprintf( line, sizeof line, "broadcast|0x%08X|%d", t.m_word, t.m_params.m_size );
                }
                else if ( t.m_kind == ui_trace_kind_t::hook && ( t.m_name == "repository_add" || t.m_name == "repository_remove" ) )
                    std::snprintf( line, sizeof line, "%s|%d", t.m_name == "repository_add" ? "repo_add" : "repo_remove", t.m_index );
                else if ( t.m_kind == ui_trace_kind_t::game )
                    std::snprintf( line, sizeof line, "slot|%d|0x%06X", t.m_index, t.m_word );
                else if ( t.m_kind == ui_trace_kind_t::hook && ( t.m_name.compare( 0, 6, "board_" ) == 0 || t.m_name.compare( 0, 7, "ladder_" ) == 0 ) )
                    std::snprintf( line, sizeof line, "%s", t.m_name.c_str( ) );
                else if ( t.m_kind == ui_trace_kind_t::event && t.m_word == 0x5EFA60u )
                    std::snprintf( line, sizeof line, "event|%d|0x%06X", t.m_index, t.m_word );
                if ( line[ 0 ] )
                    out.push_back( line );
            }
            return out;
        };
        const auto same_log = [ & ]( const std::vector< std::string >& have, int first, int count )
        {
            if ( static_cast< int >( have.size( ) ) != count )
                return false;
            for ( int q = 0; q < count; ++q )
            {
                if ( have[ static_cast< std::size_t >( q ) ] != g_golden_sgi_logs[ first + q ] )
                    return false;
                ++logs;
                slots += have[ static_cast< std::size_t >( q ) ].compare( 0, 5, "slot|" ) == 0 ? 1 : 0;
            }
            return true;
        };
        std::size_t mark = bind.m_trace.size( );
        bind.project_initialize( );
        // retail's phases 0 (0x500B10) and 1 (0x501260) as one: OUR project_initialize runs both
        int first = -1, count = 0;
        const golden_sgi_phase_t* after = nullptr;
        for ( int p = 0; p < gp.m_phases; ++p )
        {
            const golden_sgi_phase_t& ph = g_golden_sgi_phases[ gp.m_first + p ];
            if ( ph.m_phase > 1 )
                continue;
            first = first < 0 ? ph.m_first_log : first;
            count += ph.m_logs;
            after = &ph;   // phase 1's state (0x501260 last)
            ++phases;
        }
        if ( !same_log( lines( mark, false ), first, count ) )
            bad( gp.m_asset, 0, "the gameInitialize / OnProjectInitialized log" );
        if ( !after || state( ) != after->m_state )
            bad( gp.m_asset, 1, "the state after gameInitialize" );
        for ( int p = 0; p < gp.m_phases; ++p )
        {
            const golden_sgi_phase_t& ph = g_golden_sgi_phases[ gp.m_first + p ];
            if ( ph.m_phase == 2 )
            {
                entity_t& e = *bind.m_bound[ static_cast< std::size_t >( ph.m_entity ) ]->m_entity;
                script_link_t link;
                link.m_entity = &e;
                const script_component_t* sc = script_of( e );
                for ( std::size_t k = 0; sc && k < sc->m_plugs.size( ); ++k )
                    if ( std::strcmp( sc->m_plugs[ k ].m_row->m_name, "Trigger" ) == 0 )
                        link.m_plug = static_cast< int >( k );
                mark = bind.m_trace.size( );
                const ui_ret_t ret = link.m_plug >= 0 ? bind.m_wiring.input( link, ui_params_t( ) ) : ui_ret_t( );
                ++triggers;
                ++phases;
                if ( ( link.m_plug >= 0 ? 1 : 0 ) != ph.m_found || !same_log( lines( mark, true ), ph.m_first_log, ph.m_logs ) || ret.m_words[ 0 ] != ph.m_ret[ 0 ] )
                    bad( gp.m_asset, p, "a VuUserNextUIAction Trigger's broadcasts / VuRetVal" );
            }
            else if ( ph.m_phase == 3 )
            {
                mark = bind.m_trace.size( );
                bind.project_release( );
                ++phases;
                if ( !same_log( lines( mark, false ), ph.m_first_log, ph.m_logs ) )
                    bad( gp.m_asset, p, "the gameRelease log" );
                if ( state( ) != ph.m_state )
                    bad( gp.m_asset, p, "the state after gameRelease" );
            }
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "c_ui_binding project_initialize / project_release (+ VuUserNextUIAction's Trigger) match retail's 0x500B10 / 0x501260 / "
                     "0x500B80 (button 0x4426E0 + 0x53FAE0, subtitles 0x46D7A0, the five settings combos' slot 6, 0x4253D0, mini leaderboard 0x47DAB0 / 0x47DB10, leaderboard 0x479C50 / 0x479CF0; 0x470E50) on %d Screens projects: %d phases, %d log "
                     "lines in order (%d slot 6 / 7 entries), %d triggers, the touch device / combos / flags after each",
            static_cast< int >( g_golden_sgi_projects_count ), phases, logs, slots, triggers );
    else
        LOG_ERROR( "the UI screens' gameInitialize / gameRelease FAILED %d checks against retail's", failed );
    return failed == 0;
}

// verify_ui_mini_board (batch 21): c_ui_mini_board (core/ui board.hxx) against retail's
// VuUIMiniLeaderboardEntity slot 6 0x47DAB0, Decision 0x47E170, draw 0x47DB40 (Read
// 0x47E560 / Show 0x47E600 / NotConnected 0x47EA20) and slot 7 0x47DB10 through one script
// per authored instance (oracle case mini_board). The fields from OUR loaded entity
// (ui_mini_board_of; compared with retail's bytes first), the three fonts' params 0..40
// from OUR load over FontDB (41..59 retail's - UNKNOWN); the leaderboard manager (status,
// rows), the tick manager's delta, VuStringDB slot 6 and the font answers are the case's
// (inputs); the game-data root = OUR reader on the step's VUJB. Per step: the hook log in
// order, the commands and records byte for byte (one sort per buffer), the FSM after.
inline bool verify_ui_mini_board( )
{
    ui_gfx_sort_t sort;
    ui_lv_sort( sort );
    int failed = 0, steps = 0, commands = 0, logs = 0, fields = 0;
    const auto fsm_text = []( const c_ui_mini_board& b )
    {
        char word[ 48 ];
        std::snprintf( word, sizeof word, "%d,%d,%d|", b.m_fsm.m_current, b.m_fsm.m_previous, b.m_fsm.m_next );
        std::string t = word;
        for ( const fsm_condition_t& c : b.m_fsm.m_conditions )
        {
            std::snprintf( word, sizeof word, "%d,%d;", c.m_value, c.m_pulse );
            t += word;
        }
        std::snprintf( word, sizeof word, "|0x%08X|", ui_lv_bits( b.m_fsm.m_time ) );
        return t + word + b.m_key;
    };
    for ( std::size_t k = 0; k < g_golden_mb_entities_count; ++k )
    {
        const golden_mb_entity_t& ge = g_golden_mb_entities[ k ];
        const entity_t* e = ui_lv_entity( ge.m_project, ge.m_index );
        const std::vector< std::uint8_t > raw = ui_unhex( ge.m_fields );
        bool same = e && e->m_ctor && e->m_ctor->m_vtable == k_ui_mini_board_vtable && raw.size( ) == 0x190;
        ui_mini_board_t f;
        ui_mini_board_draw_t d;
        if ( same )
        {
            f = ui_mini_board_of( *e );
            const auto against = [ & ]( const void* ours, int offset, std::size_t bytes )
            {
                if ( std::memcmp( ours, raw.data( ) + ( offset - 0x60 ), bytes ) != 0 )
                    same = false;
                ++fields;
            };
            against( f.m_scale, 0x64, 8 );
            against( f.m_pos, 0x6C, 8 );
            against( &f.m_rows, 0x74, 4 );
            against( f.m_row, 0x78, 16 );
            against( &f.m_spacing, 0x88, 4 );
            against( &f.m_name_x, 0x8C, 4 );
            against( &f.m_score_x, 0x90, 4 );
            against( f.m_info, 0x15C, 16 );
            against( &f.m_h_align, 0x1D0, 4 );
            against( &f.m_v_align, 0x1D4, 4 );
            against( &f.m_no_clip, 0x1D8, 1 );
            against( &f.m_single_line, 0x1D9, 1 );
            against( &f.m_wordbreak, 0x1DA, 1 );
            against( &f.m_anchor, 0x1DC, 16 );
            same = same && ui_lv_bits( f.m_depth ) == ge.m_depth;
            const char* const font_names[ 3 ] = { "NameFont", "ScoreFont", "InfoFont" };
            for ( int c = 0; same && c < 3; ++c )
            {
                const golden_rr_font_t& gf = g_golden_mb_font_rows[ ge.m_first_font + c ];
                const ui_slot_t* name = ui_property_value( *e, font_names[ c ] );
                const std::vector< std::uint8_t > want = ui_unhex( gf.m_params );
                same = name && name->m_text == gf.m_name && want.size( ) == 60;
                if ( !same )
                    break;
                std::uint8_t* params = d.m_fonts[ c ].m_params;
                std::memcpy( params, want.data( ), 60 );   // 41..59: retail's (UNKNOWN)
                std::memset( params, 0, 41 );
                std::string asset;
                ui_font_params( ui_font_db( 0 ), name->m_text, params, asset );
                same = std::memcmp( params, want.data( ), 41 ) == 0;
                d.m_fonts[ c ].m_font = g_golden_mb_fonts[ 0 ];   // the case gives every block TxtWorld's font asset
                ++fields;
            }
        }
        std::vector< std::string > log;
        ui_mini_board_hooks_t hooks;
        std::string set;
        int status = 0;
        value_t root;
        const auto rows_of = [ & ]( int k_row, std::string& name, std::int32_t& score )
        {
            for ( std::size_t r = 0; r < g_golden_mb_rows_count; ++r )
                if ( set == g_golden_mb_rows[ r ].m_set && g_golden_mb_rows[ r ].m_k == k_row )
                {
                    name = g_golden_mb_rows[ r ].m_name;
                    score = g_golden_mb_rows[ r ].m_score;
                }
        };
        const auto row_count = [ & ]
        {
            int n = 0;
            for ( std::size_t r = 0; r < g_golden_mb_rows_count; ++r )
                n += set == g_golden_mb_rows[ r ].m_set ? 1 : 0;
            return n;
        };
        hooks.m_read = [ & ]( const std::string& key, int a, int b ) { log.push_back( "read|" + key + "|" + std::to_string( a ) + "|" + std::to_string( b ) ); };
        hooks.m_status = [ & ] { log.push_back( "status" ); return status; };
        hooks.m_count = [ & ] { log.push_back( "count" ); return row_count( ); };
        hooks.m_row = [ & ]( int k_row, std::string& name, std::int32_t& score )
        {
            log.push_back( "row|" + std::to_string( k_row ) );
            rows_of( k_row, name, score );
        };
        hooks.m_tick_register = [ & ]( bool on ) { log.push_back( on ? "register|Decision" : "unregister|Decision" ); };
        hooks.m_delta = [ ] { return ui_lv_float( g_golden_mb_delta ); };
        hooks.m_root = &root;
        d.m_string = [ & ]( const std::string& id ) { log.push_back( "string|" + id ); return id + "~"; };
        c_ui_mini_board board( f, hooks );
        same = same && fsm_text( board ) == ge.m_initial;
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui mini board: %s entity %d: OUR loaded fields / fonts / FSM differ from retail's", ge.m_project, ge.m_index );
            continue;
        }
        for ( int s = 0; s < ge.m_steps; ++s )
        {
            const golden_mb_step_t& st = g_golden_mb_steps[ ge.m_first + s ];
            const golden_txt_env_t& te = g_golden_txt_envs[ st.m_env ];
            ui_font_draw_env_t env;
            const std::vector< std::uint8_t > matrix = ui_unhex( st.m_matrix );
            std::memcpy( env.m_matrix, matrix.data( ), 64 );
            std::memcpy( &env.m_top, &te.m_top, 4 );
            for ( int m = 0; m < 2; ++m )
            {
                env.m_material[ m ] = 0x5D001000u + 0x800u * static_cast< std::uint32_t >( m );   // TxtWorld's font materials
                env.m_material_key[ m ] = g_golden_txt_material_keys[ m ];
            }
            std::memcpy( &d.m_crop.m_matrix[ 0 ], &te.m_crop[ 0 ], 4 );
            std::memcpy( &d.m_crop.m_matrix[ 5 ], &te.m_crop[ 1 ], 4 );
            std::memcpy( &d.m_crop.m_matrix[ 12 ], &te.m_crop[ 2 ], 4 );
            std::memcpy( &d.m_crop.m_matrix[ 13 ], &te.m_crop[ 3 ], 4 );
            d.m_db.m_present = true;   // the case always serves VuStringDB
            d.m_db.m_slot12 = te.m_sdb_answer != 0;
            d.m_sort = &sort;
            d.m_env = &env;
            sort.m_index = te.m_index;
            sort.m_key_lo = te.m_key_lo;
            sort.m_key_hi = te.m_key_hi;
            std::vector< ui_gfx_command_t >& list = sort.m_commands[ te.m_index ];
            const std::size_t before = list.size( );
            log.clear( );
            switch ( st.m_op )
            {
            case 0:
            {
                const std::vector< std::uint8_t > blob = ui_unhex( st.m_blob );
                root = c_reader( blob ).parse( );
                break;
            }
            case 1:
                set = st.m_name;
                break;
            case 2:
                status = st.m_int;
                break;
            case 3:
                board.draw( d );
                break;
            case 4:
                board.initialize( );
                break;
            case 5:
                board.decision( );
                break;
            default:
                board.release( );
                break;
            }
            bool ok = list.size( ) - before == static_cast< std::size_t >( st.m_commands ) && static_cast< int >( log.size( ) ) == st.m_logs;
            for ( int q = 0; ok && q < st.m_logs; ++q )
            {
                ok = log[ static_cast< std::size_t >( q ) ] == g_golden_mb_logs[ st.m_first_log + q ];
                ++logs;
            }
            for ( int c = 0; ok && c < st.m_commands; ++c )
            {
                const golden_txt_command_t& want = g_golden_mb_commands[ st.m_first + c ];
                const ui_gfx_command_t& h = list[ before + static_cast< std::size_t >( c ) ];
                const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
                const auto& memory = sort.m_buffers[ te.m_index ].m_memory;
                ok = std::memcmp( h.m_words, want.m_words, sizeof( h.m_words ) ) == 0 && h.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                     std::memcmp( memory.data( ) + h.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
                ++commands;
            }
            ok = ok && fsm_text( board ) == st.m_fsm;
            ++steps;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui mini board: %s entity %d step %d (op %d): %d commands (retail %d), %d log lines (retail %d) - or a word / record / "
                           "line / the FSM differs", ge.m_project, ge.m_index, s, st.m_op, static_cast< int >( list.size( ) - before ), st.m_commands,
                           static_cast< int >( log.size( ) ), st.m_logs );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "c_ui_mini_board initialize / decision / draw / release match retail's VuUIMiniLeaderboardEntity 0x47DAB0 / 0x47E170 / "
                     "0x47DB40 (0x47E560 / 0x47E600 / 0x47EA20, 0x47E090, the key 0x47E2B0) / 0x47DB10 on %d entities (%d of OUR loaded fields / "
                     "fonts): %d steps, %d commands byte for byte, %d hook calls in order, the FSM after each",
            static_cast< int >( g_golden_mb_entities_count ), fields, steps, commands, logs );
    else
        LOG_ERROR( "the UI mini leaderboard FAILED %d checks against retail's", failed );
    return failed == 0;
}

// verify_ui_leaderboard_draw (batch 22): ui_leaderboard_draw (core/ui ladder.hxx) against
// retail's VuUILeaderboardEntity draw 0x47A360 (the combo box draw 0x4578D0, the button
// images 0x507360 / the inline solid rect, the button texts, the FSM's state draw: Read
// 0x47BEF0 / Show 0x47BF90 / NotConnected 0x47C720 through 0x47B570) on Screens/Leaderboard
// x the case's variants (oracle case leaderboard_draw). The fields from OUR loaded entity
// (ui_leaderboard_of; compared with retail's bytes first), the combo block from OUR load
// (ui_combo_block_of), the six fonts' params 0..40 from OUR load over FontDB (41..59
// retail's - UNKNOWN), the two combo records from OUR ui_leaderboard_combos over the real
// OptionDB (compared with retail's ctor 0x477FD0's); the head bytes (shown, arrows), focus,
// press, the FSM state, the manager's rows, VuStringDB slot 6 / 7 and the font / texture
// answers are the case's (inputs). Per run: the hook log in order, the commands and
// records byte for byte (one sort per buffer), the records' focused bytes after.
inline bool verify_ui_leaderboard_draw( )
{
    ui_gfx_sort_t sort;
    ui_lv_sort( sort );
    int failed = 0, runs = 0, commands = 0, logs = 0, fields = 0, choices = 0;
    const value_t option_db = ui_cx_document( 4 );
    for ( std::size_t k = 0; k < g_golden_lb_entities_count; ++k )
    {
        const golden_lb_entity_t& ge = g_golden_lb_entities[ k ];
        const entity_t* e = ui_lv_entity( ge.m_project, ge.m_index );
        const std::vector< std::uint8_t > raw = ui_unhex( ge.m_fields );
        const std::vector< std::uint8_t > block_raw = ui_unhex( ge.m_block );
        bool same = e && e->m_ctor && e->m_ctor->m_vtable == k_ui_leaderboard_vtable && raw.size( ) == 0x584 && block_raw.size( ) == 0x1E0;
        ui_leaderboard_t f;
        ui_combo_block_t b;
        ui_results_font_t fonts[ 5 ];
        ui_config_combo_t combos[ 2 ];
        if ( same )
        {
            f = ui_leaderboard_of( *e );
            const auto against = [ & ]( const void* ours, int offset, std::size_t bytes )
            {
                if ( std::memcmp( ours, raw.data( ) + ( offset - 0x60 ), bytes ) != 0 )
                    same = false;
                ++fields;
            };
            against( f.m_scale, 0x68, 8 );
            against( f.m_pos, 0x70, 8 );
            against( &f.m_combo_spacing, 0x78, 4 );
            against( &f.m_rows, 0x27C, 4 );
            against( f.m_row, 0x280, 16 );
            against( &f.m_spacing, 0x290, 4 );
            against( &f.m_rank_x, 0x294, 12 );
            against( f.m_info, 0x3CC, 16 );
            against( &f.m_h_align, 0x440, 8 );
            against( &f.m_no_clip, 0x448, 3 );
            against( f.m_button_touch, 0x4B0, 16 );
            against( f.m_button_image, 0x4C0, 16 );
            against( f.m_button_text, 0x4D0, 16 );
            against( &f.m_button_spacing, 0x4E0, 4 );
            same = same && ui_lv_bits( f.m_depth ) == ge.m_depth;
            // the block (as verify_ui_combo_draw)
            b = ui_combo_block_of( *e );
            const auto block_against = [ & ]( const void* ours, int offset, std::size_t bytes )
            {
                if ( std::memcmp( ours, block_raw.data( ) + offset, bytes ) != 0 )
                    same = false;
                ++fields;
            };
            block_against( b.m_label_rect, 0x08, 16 );
            block_against( &b.m_label_selected, 0x7C, 4 );
            block_against( b.m_value_offset, 0x98, 8 );
            block_against( b.m_value_rect, 0xA0, 16 );
            block_against( &b.m_value_selected, 0x114, 12 );
            block_against( b.m_left, 0x1B4, 8 );
            block_against( b.m_right, 0x1BC, 8 );
            for ( int c = 0; c < 2; ++c )
            {
                std::uint8_t* params = c ? b.m_value_params : b.m_label_params;
                const int at = c ? 0xD0 : 0x38;
                std::memcpy( params, block_raw.data( ) + at, 60 );   // 41..59: retail's (UNKNOWN)
                std::memset( params, 0, 41 );
                const ui_slot_t* name = ui_property_value( *e, c ? "ValueFont" : "HeadingFont" );
                std::string asset;
                if ( !name )
                    same = false;
                else
                    ui_font_params( ui_font_db( 0 ), name->m_text, params, asset );
                block_against( params, at, 41 );
            }
            const char* const font_names[ 5 ] = { "RankFont", "NameFont", "ScoreFont", "InfoFont", "ButtonFont" };
            for ( int c = 0; same && c < 5; ++c )
            {
                const golden_rr_font_t& gf = g_golden_lb_font_rows[ ge.m_first_font + c ];
                const ui_slot_t* name = ui_property_value( *e, font_names[ c ] );
                const std::vector< std::uint8_t > want = ui_unhex( gf.m_params );
                same = name && name->m_text == gf.m_name && want.size( ) == 60;
                if ( !same )
                    break;
                std::memcpy( fonts[ c ].m_params, want.data( ), 60 );   // 41..59: retail's (UNKNOWN)
                std::memset( fonts[ c ].m_params, 0, 41 );
                std::string asset;
                ui_font_params( ui_font_db( 0 ), name->m_text, fonts[ c ].m_params, asset );
                same = std::memcmp( fonts[ c ].m_params, want.data( ), 41 ) == 0;
                ++fields;
            }
            // the ctor's records from OUR builder over the real OptionDB
            ui_leaderboard_combos( &option_db, combos );
            same = same && combos[ 0 ].m_string_id == ge.m_id0 && combos[ 1 ].m_string_id == ge.m_id1 &&
                   static_cast< int >( combos[ 0 ].m_choice_ids.size( ) ) == ge.m_choices0 &&
                   static_cast< int >( combos[ 1 ].m_choice_ids.size( ) ) == ge.m_choices1;
            for ( int c = 0; same && c < ge.m_choices0 + ge.m_choices1; ++c )
            {
                const ui_config_combo_t& cb = combos[ c < ge.m_choices0 ? 0 : 1 ];
                const std::size_t j = static_cast< std::size_t >( c < ge.m_choices0 ? c : c - ge.m_choices0 );
                const golden_lb_choice_t& want = g_golden_lb_choices[ ge.m_first_id + c ];
                same = cb.m_choice_ids[ j ] == want.m_id && cb.m_locked[ j ] == want.m_locked;
                ++choices;
            }
        }
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui leaderboard draw: %s entity %d: OUR loaded fields / block / fonts / ctor records differ from retail's", ge.m_project, ge.m_index );
            continue;
        }
        for ( int r = 0; r < ge.m_runs + ge.m_layouts; ++r )   // batch 24: then the layout draw 0x47A910 runs
        {
            const golden_lb_run_t& run = g_golden_lb_runs[ ge.m_first + r ];
            const golden_txt_env_t& te = g_golden_txt_envs[ run.m_env ];
            std::vector< std::string > log;
            ui_leaderboard_state_t s;
            for ( int c = 0; c < 2; ++c )
            {
                s.m_combos[ c ] = combos[ c ];
                const std::vector< std::uint8_t > head = ui_unhex( c ? run.m_head1 : run.m_head0 );
                s.m_combos[ c ].m_b41 = head[ 1 ];
                s.m_combos[ c ].m_b42 = head[ 2 ];
                std::memcpy( &s.m_combos[ c ].m_index, head.data( ) + 4, 4 );
                std::memcpy( &s.m_combos[ c ].m_shown, head.data( ) + 8, 4 );
            }
            s.m_focus = run.m_focus;
            s.m_press = run.m_press;
            s.m_state = run.m_state;
            ui_font_draw_env_t env;
            const std::vector< std::uint8_t > matrix = ui_unhex( run.m_matrix );
            ui_leaderboard_draw_t d;
            ui_texture_t textures[ 7 ], buttons[ 3 ];
            for ( int t = 0; t < 7; ++t )
                if ( run.m_textures[ t ] >= 0 )
                {
                    const golden_img_texture_t& g = g_golden_lb_textures[ run.m_textures[ t ] ];
                    textures[ t ] = { g.m_handle, g.m_width, g.m_height };
                }
            for ( int t = 0; t < 3; ++t )
            {
                d.m_buttons[ t ] = nullptr;
                if ( run.m_buttons[ t ] >= 0 )
                {
                    const golden_img_texture_t& g = g_golden_lb_textures[ run.m_buttons[ t ] ];
                    buttons[ t ] = { g.m_handle, g.m_width, g.m_height };
                    d.m_buttons[ t ] = &buttons[ t ];
                }
            }
            const auto font_of = [ & ]( int bit ) { return ( run.m_fonts_mask >> bit & 1 ) ? g_golden_lb_fonts[ 0 ] : ( te.m_default ? g_golden_lb_fonts[ 1 ] : 0u ); };
            for ( int c = 0; c < 5; ++c )
            {
                d.m_fonts[ c ] = fonts[ c ];
                d.m_fonts[ c ].m_font = font_of( c );
            }
            d.m_combo.m_label_font = font_of( 5 );
            d.m_combo.m_value_font = font_of( 6 );
            d.m_combo.m_string = [ & ]( const std::string& id ) { log.push_back( "id|" + id ); return id + "~"; };
            d.m_combo.m_texture = [ & ]( int which ) -> const ui_texture_t* { return run.m_textures[ which ] >= 0 ? &textures[ which ] : nullptr; };
            d.m_string = [ & ]( const std::string& id ) { log.push_back( "string|" + id ); return id + "~"; };
            int count = 0;
            for ( std::size_t q = 0; q < g_golden_lb_rows_count; ++q )
                count += std::strcmp( g_golden_lb_rows[ q ].m_set, run.m_rows ) == 0 ? 1 : 0;
            d.m_count = [ & ] { log.push_back( "count" ); return count; };
            d.m_row = [ & ]( int k_row, std::int32_t& rank, std::string& name, std::int32_t& score )
            {
                log.push_back( "row|" + std::to_string( k_row ) );
                for ( std::size_t q = 0; q < g_golden_lb_rows_count; ++q )
                    if ( std::strcmp( g_golden_lb_rows[ q ].m_set, run.m_rows ) == 0 && g_golden_lb_rows[ q ].m_k == k_row )
                    {
                        rank = g_golden_lb_rows[ q ].m_rank;
                        name = g_golden_lb_rows[ q ].m_name;
                        score = g_golden_lb_rows[ q ].m_score;
                    }
            };
            d.m_db.m_present = true;   // the case always serves VuStringDB
            d.m_db.m_slot12 = te.m_sdb_answer != 0;
            std::memcpy( env.m_matrix, matrix.data( ), 64 );
            std::memcpy( &env.m_top, &te.m_top, 4 );
            for ( int m = 0; m < 2; ++m )
            {
                env.m_material[ m ] = 0x5D001000u + 0x800u * static_cast< std::uint32_t >( m );   // TxtWorld's font materials
                env.m_material_key[ m ] = g_golden_txt_material_keys[ m ];
            }
            env.m_texture_material = g_golden_lb_material[ 0 ];
            env.m_texture_material_key = g_golden_lb_material[ 1 ];
            env.m_rect_material = g_golden_lb_rect_material[ 0 ];
            env.m_rect_material_key = g_golden_lb_rect_material[ 1 ];
            d.m_sort = &sort;
            d.m_env = &env;
            sort.m_index = te.m_index;
            sort.m_key_lo = te.m_key_lo;
            sort.m_key_hi = te.m_key_hi;
            std::vector< ui_gfx_command_t >& list = sort.m_commands[ te.m_index ];
            const std::size_t before = list.size( );
            const bool known = run.m_layout < 0 ? ui_leaderboard_draw( f, b, s, d ) : ui_leaderboard_layout_draw( f, b, s, d, run.m_layout != 0 );
            bool ok = known && list.size( ) - before == static_cast< std::size_t >( run.m_commands ) && static_cast< int >( log.size( ) ) == run.m_logs;
            for ( int q = 0; ok && q < run.m_logs; ++q )
            {
                ok = log[ static_cast< std::size_t >( q ) ] == g_golden_lb_logs[ run.m_first_log + q ];
                ++logs;
            }
            for ( int c = 0; ok && c < run.m_commands; ++c )
            {
                const golden_txt_command_t& want = g_golden_lb_commands[ run.m_first + c ];
                const ui_gfx_command_t& h = list[ before + static_cast< std::size_t >( c ) ];
                const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
                const auto& memory = sort.m_buffers[ te.m_index ].m_memory;
                ok = std::memcmp( h.m_words, want.m_words, sizeof( h.m_words ) ) == 0 && h.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                     std::memcmp( memory.data( ) + h.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
                ++commands;
            }
            for ( int c = 0; ok && c < 2; ++c )
                ok = s.m_combos[ c ].m_focused == ui_unhex( c ? run.m_head1 : run.m_head0 )[ 0 ];
            ++runs;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui leaderboard draw: %s entity %d run %d (variant %d): %d commands (retail %d), %d log lines (retail %d) - or a word / "
                           "record / line / focused byte differs", ge.m_project, ge.m_index, r, run.m_variant, static_cast< int >( list.size( ) - before ),
                           run.m_commands, static_cast< int >( log.size( ) ), run.m_logs );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_leaderboard_draw / ui_leaderboard_layout_draw match retail's VuUILeaderboardEntity draw 0x47A360 / layout draw 0x47A910 (0x4578D0, 0x507360 / the solid rect, 0x47B570, "
                     "Read 0x47BEF0 / Show 0x47BF90 / NotConnected 0x47C720) on %d entities (%d of OUR loaded fields / block / fonts, %d ctor "
                     "0x477FD0 choices from OUR builder): %d runs, %d commands byte for byte, %d hook calls in order",
            static_cast< int >( g_golden_lb_entities_count ), fields, choices, runs, commands, logs );
    else
        LOG_ERROR( "the UI leaderboard draw FAILED %d checks against retail's", failed );
    return failed == 0;
}

// verify_ui_leaderboard_tick (batch 23): c_ui_leaderboard (core/ui ladder.hxx) against retail's
// VuUILeaderboardEntity per-frame half - slot 6 0x479C50, Decision 0x47B640, Read's enter 0x47BC90 /
// exit 0x47BE70 / tick 0x47BEA0, 0x47A1B0, 0x47A280, the touch callbacks 0x479D30 / 0x479E30 /
// 0x479E40, slot 7 0x479CF0 - over one script on the authored instance (oracle case
// leaderboard_tick). The fields from OUR loaded entity (compared with retail's bytes first), the
// records from OUR ui_leaderboard_combos over the real OptionDB; the platform leaderboard manager
// (read / status / count), the tick manager, the touch device and FMOD are the script's hooks,
// logged as retail's were. Per step: the hook log in order and the whole state string.
inline bool verify_ui_leaderboard_tick( )
{
    int failed = 0, steps = 0, logs = 0, fields = 0;
    const value_t option_db = ui_cx_document( 4 );
    float delta, inverse[ 16 ];
    std::memcpy( &delta, &g_golden_lt_delta, 4 );
    std::memcpy( inverse, g_golden_lt_inverse, 64 );
    const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    const auto snapshot = [ &bits ]( const c_ui_leaderboard& lb )
    {
        char word[ 160 ];
        std::snprintf( word, sizeof word, "%d,%d,%d,%d,%d,%d,%d,0x%08X,0x%08X|", lb.m_s.m_focus, lb.m_first, lb.m_s.m_press, lb.m_entry, lb.m_down, lb.m_held,
                       lb.m_up, bits( lb.m_point[ 0 ] ), bits( lb.m_point[ 1 ] ) );
        std::string s = word;
        for ( const ui_config_combo_t& c : lb.m_s.m_combos )
        {
            std::snprintf( word, sizeof word, "%d,%d,%d,%d,0x%08X;", c.m_focused, c.m_b41, c.m_b42, c.m_index, bits( c.m_shown ) );
            s += word;
        }
        std::snprintf( word, sizeof word, "|%d,%d,%d|", lb.m_fsm.m_current, lb.m_fsm.m_previous, lb.m_fsm.m_next );
        s += word;
        for ( const fsm_condition_t& c : lb.m_fsm.m_conditions )
        {
            std::snprintf( word, sizeof word, "%d,%d;", c.m_value, c.m_pulse );
            s += word;
        }
        std::snprintf( word, sizeof word, "|0x%08X", bits( lb.m_fsm.m_time ) );
        return s + word;
    };
    for ( std::size_t k = 0; k < g_golden_lt_entities_count; ++k )
    {
        const golden_lt_entity_t& ge = g_golden_lt_entities[ k ];
        const entity_t* e = ui_lv_entity( ge.m_project, ge.m_index );
        const std::vector< std::uint8_t > raw = ui_unhex( ge.m_fields );
        bool same = e && e->m_ctor && e->m_ctor->m_vtable == k_ui_leaderboard_vtable && raw.size( ) == 0x584;
        ui_leaderboard_t f;
        if ( same )
        {
            f = ui_leaderboard_of( *e );
            const auto against = [ & ]( const void* ours, int offset, std::size_t n )
            {
                if ( std::memcmp( ours, raw.data( ) + ( offset - 0x60 ), n ) != 0 )
                    same = false;
                ++fields;
            };
            against( f.m_scale, 0x68, 8 );
            against( f.m_pos, 0x70, 8 );
            against( &f.m_combo_spacing, 0x78, 4 );
            against( f.m_left, 0x25C, 16 );
            against( f.m_right, 0x26C, 16 );
            against( &f.m_rows, 0x27C, 4 );
            against( f.m_button_touch, 0x4B0, 16 );
            against( &f.m_button_spacing, 0x4E0, 4 );
        }
        std::vector< std::string > log;
        int status = 0, total = 0;
        float point[ 2 ] = { 0.0f, 0.0f };
        ui_leaderboard_hooks_t hooks;
        hooks.m_option_db = &option_db;
        hooks.m_read = [ & ]( const std::string& key, int first, int rows ) { log.push_back( "read|" + key + "|" + std::to_string( first ) + "|" + std::to_string( rows ) ); };
        hooks.m_status = [ & ] { log.push_back( "status" ); return status; };
        hooks.m_total = [ & ] { log.push_back( "total" ); return total; };
        hooks.m_tick_register = [ & ]( bool on ) { log.push_back( on ? "register|Decision" : "unregister|Decision" ); };
        hooks.m_delta = [ & ] { return delta; };
        hooks.m_touch = [ & ]( bool on ) { log.push_back( on ? "touch_add|0x5C|0" : "touch_remove|0x5C" ); };
        hooks.m_sound = [ & ]( const char* name ) { log.push_back( std::string( "sound|" ) + name ); };
        c_ui_leaderboard lb( f, hooks );
        same = same && static_cast< int >( lb.m_s.m_combos[ 0 ].m_choice_ids.size( ) ) == ge.m_choices0 &&
               static_cast< int >( lb.m_s.m_combos[ 1 ].m_choice_ids.size( ) ) == ge.m_choices1 && snapshot( lb ) == ge.m_initial;
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui leaderboard tick: %s entity %d: OUR loaded fields / ctor records / initial state differ from retail's", ge.m_project, ge.m_index );
            continue;
        }
        for ( int s = 0; s < ge.m_steps; ++s )
        {
            const golden_lt_step_t& gs = g_golden_lt_steps[ ge.m_first + s ];
            log.clear( );
            float p[ 2 ];
            std::memcpy( p, gs.m_point, 8 );
            bool known = true;
            switch ( gs.m_op )
            {
            case 0: known = lb.initialize( ); break;
            case 1: known = lb.decision( ); break;
            case 2: status = gs.m_args[ 0 ]; break;
            case 3: total = gs.m_args[ 0 ]; break;
            case 4: lb.touch_down( inverse, p ); break;
            case 5: lb.touch_up( ); break;
            case 6:
                log.push_back( "point|0" );   // the device's vt+0x1C (0, out)
                point[ 0 ] = p[ 0 ];
                point[ 1 ] = p[ 1 ];
                lb.touch_move( inverse, point );
                break;
            case 7: known = lb.change( gs.m_args[ 0 ], gs.m_args[ 1 ] ); break;
            case 8: known = lb.page( gs.m_args[ 0 ] ); break;
            default: lb.release( ); break;
            }
            bool ok = known && static_cast< int >( log.size( ) ) == gs.m_logs;
            for ( int q = 0; ok && q < gs.m_logs; ++q )
            {
                ok = log[ static_cast< std::size_t >( q ) ] == g_golden_lt_logs[ gs.m_first_log + q ];
                ++logs;
            }
            const std::string state = snapshot( lb );
            ok = ok && state == gs.m_state;
            ++steps;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui leaderboard tick: %s step %d (op %d): %d log lines (retail %d), state %s (retail %s)", ge.m_project, s, gs.m_op,
                           static_cast< int >( log.size( ) ), gs.m_logs, state.c_str( ), gs.m_state );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "c_ui_leaderboard matches retail's VuUILeaderboardEntity slot 6 0x479C50 / Decision 0x47B640 / Read 0x47BC90 0x47BE70 0x47BEA0 / "
                     "0x47A1B0 / 0x47A280 / touch 0x479D30 0x479E30 0x479E40 / slot 7 0x479CF0 on %d entities (%d of OUR loaded fields): %d steps, "
                     "%d hook calls in order, the state after each",
            static_cast< int >( g_golden_lt_entities_count ), fields, steps, logs );
    else
        LOG_ERROR( "the UI leaderboard tick FAILED %d checks against retail's", failed );
    return failed == 0;
}

// verify_ui_button_touch (batch 25): c_ui_button's touch half (core/ui button.hxx: the callbacks
// 0x442740 / 0x4427D0 / 0x4427E0, OnUITick 0x442A90's hit test) against retail's on every authored
// button of Screens/Main / SinglePlayer / Settings (oracle case button_touch) under two VuUIImpl
// blocks. The touch fields from OUR loaded entity (bind.hxx ui_button_touch_of; compared with
// retail's bytes first), the entity id from OUR long name; the device point, the crop / inverse and
// the hooks (broadcast, the plugs' fire) are the case's. Per step: +0x68, +0x188..+0x18C, the
// point, the press state and the hook log in order.
inline bool verify_ui_button_touch( )
{
    int failed = 0, steps = 0, logs = 0, fields = 0;
    const auto hex = []( const std::uint8_t* p, std::size_t n )
    {
        static const char* const digits = "0123456789abcdef";
        std::string t;
        for ( std::size_t k = 0; k < n; ++k )
        {
            t += digits[ p[ k ] >> 4 ];
            t += digits[ p[ k ] & 15 ];
        }
        return t;
    };
    for ( std::size_t k = 0; k < g_golden_btt_entities_count; ++k )
    {
        const golden_btt_entity_t& ge = g_golden_btt_entities[ k ];
        const entity_t* e = ui_lv_entity( ge.m_project, ge.m_index );
        const std::vector< std::uint8_t > raw = ui_unhex( ge.m_fields );
        bool same = e && e->m_ctor && e->m_ctor->m_vtable == k_ui_button_vtable && raw.size( ) == 0x140;
        ui_button_touch_t t;
        std::uint8_t flags[ 3 ] = { 1, 1, 1 };
        if ( same )
        {
            t = ui_button_touch_of( *e );
            const auto against = [ & ]( const void* ours, int offset, std::size_t bytes )
            {
                if ( std::memcmp( ours, raw.data( ) + ( offset - 0x60 ), bytes ) != 0 )
                    same = false;
                ++fields;
            };
            against( t.m_scale, 0x88, 8 );
            against( t.m_xy, 0x90, 8 );
            against( t.m_touch, 0x110, 16 );
            against( &t.m_anchor, 0x168, 16 );
            const char* const names[ 3 ] = { "Enabled", "Label", "Can Have Focus" };
            for ( int f = 0; f < 3; ++f )
                if ( const ui_slot_t* s = ui_bind_slot( *e, names[ f ] ) )
                    flags[ f ] = static_cast< std::uint8_t >( s->m_bits[ 0 ] & 0xFFu );
            against( flags, 0x68, 3 );
            const std::uint8_t zero[ 0x14 ] = { };
            against( zero, 0x188, 5 );   // focus / touch bytes, +0x190 / +0x194, +0x198 as the ctor leaves them
            against( zero, 0x190, 12 );
            same = same && ui_entity_id( ui_long_name( *e ) ) == ge.m_id;
        }
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui button touch: %s entity %d: OUR loaded fields / id differ from retail's", ge.m_project, ge.m_index );
            continue;
        }
        for ( int r = 0; r < ge.m_runs; ++r )
        {
            const golden_btt_run_t& run = g_golden_btt_runs[ ge.m_first + r ];
            ui_crop_t crop;
            float inverse[ 16 ];
            const std::vector< std::uint8_t > c = ui_unhex( run.m_crop ), inv = ui_unhex( run.m_inverse );
            std::memcpy( crop.m_matrix, c.data( ), 64 );
            std::memcpy( inverse, inv.data( ), 64 );
            std::vector< std::string > log;
            ui_button_hooks_t hooks;
            hooks.m_broadcast = [ & ]( std::uint32_t hash, const ui_params_t& p )
            {
                char word[ 24 ];
                std::snprintf( word, sizeof word, "0x%08X", hash );
                log.push_back( std::string( "broadcast|" ) + word + "|" + hex( p.m_bytes, static_cast< std::size_t >( p.m_size ) ) );
            };
            hooks.m_fire = [ & ]( const char* plug, const ui_params_t& p )
            {
                log.push_back( std::string( "fire|" ) + plug + "|" + hex( p.m_bytes, static_cast< std::size_t >( p.m_size ) ) );
            };
            c_ui_button b( ge.m_id, hooks );
            b.m_enabled = flags[ 0 ] != 0;
            b.m_label = flags[ 1 ] != 0;
            b.m_can_focus = flags[ 2 ] != 0;
            b.m_touch_fields = t;
            b.m_crop = &crop;
            for ( int s = 0; s < run.m_steps; ++s )
            {
                const golden_btt_step_t& st = g_golden_btt_steps[ run.m_first + s ];
                log.clear( );
                float device[ 2 ];
                std::memcpy( device, st.m_device, 8 );
                switch ( st.m_op )
                {
                case 0:
                    b.on_tick( );
                    break;
                case 1:
                    b.touch_down( inverse, device );
                    break;
                case 2:
                    b.touch_up( );
                    break;
                case 3:
                    log.push_back( "point|0" );   // the device's slot 7 (index 0)
                    b.touch_move( inverse, device );
                    break;
                default:
                    b.m_enabled = st.m_arg != 0;
                    break;
                }
                const std::uint8_t bytes[ 6 ] = { static_cast< std::uint8_t >( b.m_enabled ), static_cast< std::uint8_t >( b.m_focused ),
                                                  static_cast< std::uint8_t >( b.m_want_focus ), static_cast< std::uint8_t >( b.m_touched ),
                                                  static_cast< std::uint8_t >( b.m_touch_press ), static_cast< std::uint8_t >( b.m_touch_release ) };
                const std::string state = hex( bytes, 1 ) + "." + hex( bytes + 1, 5 );
                bool ok = state == st.m_state && std::memcmp( b.m_touch_point, st.m_point, 8 ) == 0 && b.m_press == st.m_press &&
                          static_cast< int >( log.size( ) ) == st.m_logs;
                for ( int q = 0; ok && q < st.m_logs; ++q )
                {
                    ok = log[ static_cast< std::size_t >( q ) ] == g_golden_btt_logs[ st.m_first_log + q ];
                    ++logs;
                }
                ++steps;
                if ( !ok && ++failed <= 8 )
                    LOG_ERROR( "ui button touch: %s entity %d run %d step %d (op %d): state %s (retail %s), press %u (retail %u), %d log lines (retail %d)",
                               ge.m_project, ge.m_index, r, s, st.m_op, state.c_str( ), st.m_state, b.m_press, st.m_press, static_cast< int >( log.size( ) ),
                               st.m_logs );
            }
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "c_ui_button's touch half matches retail's VuBarracudaUIButtonEntity callbacks 0x442740 / 0x4427D0 / 0x4427E0 and OnUITick "
                     "0x442A90 (the rect through the anchor 0x6282D0, SetFocus 0x4428E0, Pressed / OnFocus) on %d authored buttons (%d of OUR loaded "
                     "fields): %d steps, %d hook calls in order, the state after each",
            static_cast< int >( g_golden_btt_entities_count ), fields, steps, logs );
    else
        LOG_ERROR( "the UI button touch half FAILED %d checks against retail's", failed );
    return failed == 0;
}

inline bool verify_ui_results_all( )
{
    bool ok = true;
    ok = verify_ui_button_touch( ) && ok;
    ok = verify_ui_leaderboard_draw( ) && ok;
    ok = verify_ui_leaderboard_tick( ) && ok;
    ok = verify_ui_race_results( ) && ok;
    ok = verify_ui_subtitles_combo( ) && ok;
    ok = verify_ui_settings_combos( ) && ok;
    ok = verify_ui_touch_dispatch( ) && ok;
    ok = verify_ui_screen_game_init( ) && ok;
    ok = verify_ui_mini_board( ) && ok;
    return ok;
}
