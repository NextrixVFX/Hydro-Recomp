#pragma once

#include <workspace/core/ui/bind.hxx>
#include <workspace/core/ui/board.hxx>
#include <workspace/core/ui/frame.hxx>
#include <workspace/core/ui/ladder.hxx>
#include <workspace/core/ui/navigate.hxx>
#include <workspace/core/ui/ticker.hxx>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The UI area's menu checks (included by checks/ui.hxx before verify_ui_all,
// which runs verify_ui_menu_all): the config selectors' navigation and the
// screens' frame. Everything ui.hxx defines above its verify_ui_all is usable.

// The 19 nav bytes +0x2CD..+0x2DF as config_nav prints them.
inline std::string ui_cn_nav_hex( const ui_config_nav_t& n )
{
    std::uint8_t raw[ 19 ];
    raw[ 0 ] = n.m_down;
    raw[ 1 ] = n.m_held;
    raw[ 2 ] = n.m_up;
    std::memcpy( raw + 3, n.m_touch, 8 );
    std::memcpy( raw + 11, &n.m_press, 4 );
    std::memcpy( raw + 15, &n.m_press_index, 4 );
    std::string out;
    char buf[ 4 ];
    for ( std::uint8_t b : raw )
    {
        std::snprintf( buf, sizeof( buf ), "%02x", b );
        out += buf;
    }
    return out;
}

inline float ui_cn_float( std::uint32_t bits )
{
    float f;
    std::memcpy( &f, &bits, 4 );
    return f;
}

// ui_config_input / ui_config_touch_* / ui_config_tick / ui_config_focus /
// ui_config_change (hydro core/ui/navigate.hxx: slot 14 0x448A50, the touch
// callbacks 0x4481C0 / 0x4482C0 / 0x4482D0, slot 13 0x448470, 0x4496E0,
// 0x449830) against retail on each GameConfig* screen's VuGameConfigEntity
// (oracle case config_nav): the nav properties read from OUR loaded entity and
// compared with retail's floats; slot 4 + 0x45D050 (ui_game_config_build /
// _initialize) then 47 scripted pad / touch / tick steps per run, after each:
// the whole root, every combo record, the focus, the 19 nav bytes, the trophy
// byte and every hook call in order with its arguments.
inline bool verify_ui_config_nav( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    int failed = 0, runs = 0, steps = 0, calls = 0, failed_runs = 0;
    auto bad = [ &failed ]( const char* what, int k )
    {
        if ( ++failed <= 8 )
            LOG_ERROR( "ui config nav: %s differs from retail (step %d)", what, k );
    };
    const value_t track_db = ui_cx_document( 2 ), option_db = ui_cx_document( 4 );
    static const float inverse[ 16 ] = { 1.25f, 0.03125f, 0, 0, -0.0625f, 0.875f, 0, 0, 0, 0, 1, 0, -0.1f, 0.05f, 0, 1 };
    for ( std::size_t k = 0; k < g_golden_cn_entities_count; ++k )
    {
        const golden_cn_entity_t& want = g_golden_cn_entities[ k ];
        const entity_t* have = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, want.m_project ) == 0 && want.m_index >= 0 &&
                 static_cast< std::size_t >( want.m_index ) < loaded.trees[ p ].size( ) )
                have = loaded.trees[ p ][ static_cast< std::size_t >( want.m_index ) ].second;
        if ( !have || have->m_type != "VuGameConfigEntity" || have->m_name != want.m_name )
        {
            bad( "the authored entity", static_cast< int >( k ) );
            continue;
        }
        // The nav fields from OUR loaded entity, then compared with retail's.
        ui_config_nav_t nav0;
        std::uint32_t bits[ 14 ] = { };
        auto take = [ & ]( const char* name, int words, int at )
        {
            if ( const ui_slot_t* v = ui_property_value( *have, name ) )
                for ( int w = 0; w < words; ++w )
                    bits[ at + w ] = v->m_bits[ w ];
            else
                bad( "a nav property (missing)", static_cast< int >( k ) );
        };
        take( "Screen Scale", 2, 0 );
        take( "Position X", 1, 2 );
        take( "Position Y", 1, 3 );
        take( "Spacing", 1, 4 );
        std::memcpy( &bits[ 5 ], &nav0.m_scroll_time, 4 );
        const char* const rects[ ] = { "TouchLeft X", "TouchLeft Y", "TouchLeft Width", "TouchLeft Height",
                                       "TouchRight X", "TouchRight Y", "TouchRight Width", "TouchRight Height" };
        for ( int r = 0; r < 8; ++r )
            take( rects[ r ], 1, 6 + r );
        for ( int w = 0; w < 14; ++w )
            if ( bits[ w ] != want.m_floats[ w ] )
                bad( "a nav float", static_cast< int >( k ) );
        nav0.m_scale[ 0 ] = ui_cn_float( bits[ 0 ] );
        nav0.m_scale[ 1 ] = ui_cn_float( bits[ 1 ] );
        nav0.m_position[ 0 ] = ui_cn_float( bits[ 2 ] );
        nav0.m_position[ 1 ] = ui_cn_float( bits[ 3 ] );
        nav0.m_spacing = ui_cn_float( bits[ 4 ] );
        for ( int r = 0; r < 4; ++r )
        {
            nav0.m_left[ r ] = ui_cn_float( bits[ 6 + r ] );
            nav0.m_right[ r ] = ui_cn_float( bits[ 10 + r ] );
        }
        if ( ui_cn_nav_hex( nav0 ) != want.m_initial )
            bad( "the ctor's nav bytes", static_cast< int >( k ) );
        const ui_front_end_screen_t screen = ui_front_end_screen( *have, want.m_flags );
        if ( screen.m_option_type != want.m_option_type )
            bad( "the OptionType", static_cast< int >( k ) );
        for ( int r = 0; r < want.m_runs; ++r )
        {
            const golden_cn_run_t& run = g_golden_cn_runs[ want.m_first + r ];
            const std::vector< std::uint8_t > rdoc = ui_unhex( run.m_root );
            value_t root = c_reader( rdoc ).parse( );
            ui_config_env_t env;
            env.m_root = &root;
            const golden_cn_step_t* step = nullptr;
            int at = 0;
            bool hooks_same = true;
            auto next = [ & ]( int kind ) -> const golden_bci_log_t*
            {
                if ( !step || at >= step->m_logs || g_golden_cn_logs[ step->m_first_log + at ].m_kind != kind )
                {
                    hooks_same = false;
                    return nullptr;
                }
                ++calls;
                return &g_golden_cn_logs[ step->m_first_log + at++ ];
            };
            ui_profile_t p;
            p.unlock = [ & ]( int slot, const std::string& a, const std::string& b, const std::string& c )
            {
                const golden_bci_log_t* g = next( 0 );
                hooks_same = hooks_same && g && g->m_slot == slot && a == g->m_a && b == g->m_b && c == g->m_c;
                return g && g->m_answer != 0;
            };
            p.license = [ & ]( ) { const golden_bci_log_t* g = next( 1 ); return g && g->m_answer != 0; };
            p.progress = [ & ]( const std::string& value )
            {
                const golden_bci_log_t* g = next( 2 );
                hooks_same = hooks_same && g && value == g->m_a;
                return g && g->m_answer != 0;
            };
            p.store_beta = [ & ]( ) { const golden_bci_log_t* g = next( 3 ); return g && g->m_answer != 0; };
            p.package = [ & ]( const std::string& asset )
            {
                const golden_bci_log_t* g = next( 8 );
                hooks_same = hooks_same && g && asset == g->m_a;
                return g && g->m_answer != 0;
            };
            p.next_choice = [ & ]( const value_t&, std::string& ) { next( 9 ); return false; };
            p.champ_rank = [ & ]( const std::string& champ )
            {
                const golden_bci_log_t* g = next( 10 );
                hooks_same = hooks_same && g && champ == g->m_a;
                return g ? g->m_answer : 0;
            };
            p.race_rank = [ & ]( const std::string& mode, const std::string& track, const std::string& course )
            {
                const golden_bci_log_t* g = next( 11 );
                hooks_same = hooks_same && g && mode == g->m_a && track == g->m_b && course == g->m_c;
                return g ? g->m_answer : 0;
            };
            ui_config_hooks_t h;
            h.touch = [ & ]( ) { next( 4 ); };
            h.draw_add = [ & ]( ) { next( 13 ); };
            h.fire = [ & ]( const std::string& plug, const ui_params_t& params )
            {
                const golden_bci_log_t* g = next( 5 );
                hooks_same = hooks_same && g && plug == g->m_a && ui_bci_hex( params ) == g->m_b;
                return g != nullptr;
            };
            h.broadcast = [ & ]( std::uint32_t hash, const ui_params_t& params )
            {
                const golden_bci_log_t* g = next( 6 );
                char text[ 16 ];
                std::snprintf( text, sizeof( text ), "0x%08X", hash );
                hooks_same = hooks_same && g && std::strcmp( g->m_a, text ) == 0 && ui_bci_hex( params ) == g->m_b;
            };
            h.sound = [ & ]( const char* event )
            {
                const golden_bci_log_t* g = next( 7 );
                hooks_same = hooks_same && g && std::strcmp( g->m_a, event ) == 0;
            };
            h.trophy = [ & ]( const char* model )
            {
                const golden_bci_log_t* g = next( 12 );
                hooks_same = hooks_same && g && std::strcmp( g->m_a, model ) == 0;
            };
            ui_config_entity_t e;
            e.m_single_player = screen.m_single_player;
            e.m_force_online = screen.m_force_online;
            ui_game_config_t2 g;
            ui_config_nav_t n = nav0;
            // batch 26: setup's 0x53FB90 registers the selector's callbacks in a touch device and every touch op below goes
            // through the device's dispatch (controls.hxx) to them (navigate.hxx ui_config_touch_handlers) - the binding's glue
            ui_touch_device_t device;
            int touch_faults = 0;
            h.touch = [ & ]( )
            {
                next( 4 );
                ui_touch_add( device, &n, 0u );
                ui_config_touch_handlers( device, &n, [ ] { return inverse; }, touch_faults );
            };
            value_t doc;
            ui_config_class_t cls;
            ui_game_config_class_full( cls, e, g, root, p, h );
            ++runs;
            const int failed_before = failed;
            for ( int s = 0; s < run.m_steps; ++s )
            {
                step = &g_golden_cn_steps[ run.m_first + s ];
                const int row = run.m_first + s;
                at = 0;
                hooks_same = true;
                bool done = true;
                switch ( step->m_op )
                {
                case 5:
                    done = ui_game_config_build( e, env, doc, &option_db, screen.m_option_type, &track_db, screen.m_flags, p, h ) &&
                           ui_game_config_initialize( e, g, env, doc, p, h );
                    break;
                case 0:
                {
                    ui_params_t params;
                    if ( step->m_has_text )
                        params.add_string( step->m_text );
                    done = ui_config_input( e, env, doc, params, cls, p, h );
                    break;
                }
                case 1:
                    done = ui_config_tick( e, n, env, doc, ui_cn_float( step->m_a ), cls, p, h );
                    break;
                case 2:
                    ui_touch_dispatch_down( device, 1u, ui_cn_float( step->m_a ), ui_cn_float( step->m_b ) );   // 0x5393E0
                    break;
                case 3:
                    // the device's touch 0 answers the case's point (retail's hooked slot 7), then 0x53FB40
                    next( 15 );
                    if ( device.m_touches.empty( ) )
                        device.m_touches.resize( 1 );
                    device.m_touches[ 0 ].m_point[ 0 ] = ui_cn_float( step->m_a );
                    device.m_touches[ 0 ].m_point[ 1 ] = ui_cn_float( step->m_b );
                    ui_touch_dispatch_move( device );
                    break;
                default:
                    ui_touch_dispatch_up( device, 1u, 0.0f, 0.0f );   // 0x5394A0
                    break;
                }
                if ( touch_faults || device.m_unhandled )
                    bad( "the touch dispatch (an unhandled callback / indeterminate touch)", row );
                ++steps;
                std::string after;
                vujb_dump( root, after );
                if ( !done )
                    bad( "the step (a path not recreated)", row );
                if ( after != step->m_after )
                    bad( "the root after", row );
                if ( ui_cx_combo_text( e.m_combos ) != step->m_combos )
                    bad( "the combo records", row );
                if ( e.m_focus != step->m_focus )
                    bad( "the focus", row );
                if ( ui_cn_nav_hex( n ) != step->m_nav )
                    bad( "the nav bytes", row );
                if ( ( g.m_trophy ? 1 : 0 ) != step->m_trophy )
                    bad( "the trophy byte", row );
                if ( !hooks_same || at != step->m_logs )
                    bad( "the hook calls", row );
            }
            if ( failed > failed_before )
                ++failed_runs;
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_config_input / touch / tick match retail's VuBaseConfigEntity navigation (slot 14 0x448A50, touch 0x4481C0 / "
                     "0x4482C0 / 0x4482D0, slot 13 0x448470, focus 0x4496E0, change 0x449830) on %d GameConfig entities from our loaded "
                     "properties: %d runs, %d steps (root, combos, focus, nav bytes, trophy; the touch ops through the device's dispatch 0x5393E0 / "
                     "0x5394A0 / 0x53FB40 to the callbacks setup's 0x53FB90 registered), %d hook calls",
            static_cast< int >( g_golden_cn_entities_count ), runs, steps, calls );
    else
        LOG_ERROR( "the config navigation FAILED %d checks against retail's (%d runs)", failed, failed_runs );
    return failed == 0;
}

// THE BINDING'S SELECTOR GLUE, batch 27 (C++ only: golden config_nav). c_ui_binding
// on each GameConfig* project with a touch device: bind_config's fields from OUR
// loaded properties, config_setup( )'s build + initialize, h.touch's registration
// in the device and handlers - the lambdas verify_ui_config_nav mirrors, here the
// real ones. Each run's steps through the bound selector (input / tick through its
// members, touch through the device), against retail's root / combos / focus / nav
// bytes after each step. Platform answers: the step's logged calls by their
// arguments. The plugs and broadcasts are stubbed, as in the case.
inline bool verify_ui_config_glue( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    int failed = 0, runs = 0, steps = 0, answers = 0;
    auto bad = [ &failed ]( const char* what, int k )
    {
        if ( ++failed <= 8 )
            LOG_ERROR( "ui config glue: %s differs from retail (step %d)", what, k );
    };
    const value_t track_db = ui_cx_document( 2 ), option_db = ui_cx_document( 4 );
    static const float inverse[ 16 ] = { 1.25f, 0.03125f, 0, 0, -0.0625f, 0.875f, 0, 0, 0, 0, 1, 0, -0.1f, 0.05f, 0, 1 };
    for ( std::size_t k = 0; k < g_golden_cn_entities_count; ++k )
    {
        const golden_cn_entity_t& want = g_golden_cn_entities[ k ];
        entity_t* root = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, want.m_project ) == 0 && !loaded.trees[ p ].empty( ) )
                root = const_cast< entity_t* >( loaded.trees[ p ][ 0 ].second );
        if ( !root )
        {
            bad( "the project", static_cast< int >( k ) );
            continue;
        }
        for ( int r = 0; r < want.m_runs; ++r )
        {
            const golden_cn_run_t& run = g_golden_cn_runs[ want.m_first + r ];
            const std::vector< std::uint8_t > rdoc = ui_unhex( run.m_root );
            value_t doc_root = c_reader( rdoc ).parse( );
            const golden_cn_step_t* step = nullptr;
            int at = 0;
            ui_touch_device_t device;
            ui_bind_hooks_t hooks;
            hooks.m_root = &doc_root;
            hooks.m_touch_device = &device;
            hooks.m_inverse = inverse;
            hooks.m_option_db = &option_db;
            hooks.m_track_db = &track_db;
            hooks.m_config_flags = want.m_flags;
            hooks.m_platform = [ & ]( const std::string& name ) -> std::pair< bool, int >
            {
                // the step's logged call with these arguments (the verify_ui_config_nav profile logs); a call that check's profile never makes answers false
                if ( !step )
                    return { false, 0 };
                for ( int l = 0; l < step->m_logs; ++l )
                {
                    const golden_bci_log_t& g = g_golden_cn_logs[ step->m_first_log + l ];
                    std::string key;
                    if ( g.m_kind == 0 )
                    {
                        const std::string tag = std::to_string( g.m_slot );
                        key = g.m_slot >= 100 ? tag + "|" + g.m_a + "|" + g.m_b + "|" + g.m_c : tag + "|" + g.m_a + "|" + g.m_b;
                    }
                    else if ( g.m_kind == 1 )
                        key = "license";
                    else if ( g.m_kind == 2 )
                        key = std::string( "132|" ) + g.m_a;
                    else if ( g.m_kind == 3 )
                        key = "4D3170";
                    else if ( g.m_kind == 8 )
                        key = std::string( "144|" ) + g.m_a;
                    else if ( g.m_kind == 10 )
                        key = std::string( "88|" ) + g.m_a;
                    else if ( g.m_kind == 11 )
                        key = std::string( "48|" ) + g.m_a + "|" + g.m_b + "|" + g.m_c;
                    if ( key == name )
                    {
                        ++answers;
                        return { g.m_answer != 0, g.m_answer };
                    }
                }
                return { false, 0 };
            };
            c_ui_binding bind( *root, hooks );
            if ( static_cast< std::size_t >( want.m_index ) >= bind.m_bound.size( ) || !bind.m_bound[ static_cast< std::size_t >( want.m_index ) ]->m_config )
            {
                bad( "the bound selector", static_cast< int >( k ) );
                continue;
            }
            ui_bound_config_t& c = *bind.m_bound[ static_cast< std::size_t >( want.m_index ) ]->m_config;
            // retail's run stubs these (the case's hooks): the plugs and broadcasts must not reach this binding's live graph, which would re-enter the selector
            c.m_hooks.fire = [ ]( const std::string&, const ui_params_t& ) { return true; };
            c.m_hooks.broadcast = [ ]( std::uint32_t, const ui_params_t& ) { };
            ++runs;
            for ( int s = 0; s < run.m_steps; ++s )
            {
                step = &g_golden_cn_steps[ run.m_first + s ];
                const int row = run.m_first + s;
                at = 0;
                switch ( step->m_op )
                {
                case 5: bind.config_setup( ); break;
                case 0:
                {
                    ui_params_t params;
                    if ( step->m_has_text )
                        params.add_string( step->m_text );
                    ui_config_input( c.base( ), c.m_env, c.m_doc, params, c.m_class, c.m_profile, c.m_hooks );
                    break;
                }
                case 1: ui_config_tick( c.base( ), c.m_nav, c.m_env, c.m_doc, ui_cn_float( step->m_a ), c.m_class, c.m_profile, c.m_hooks ); break;
                case 2: ui_touch_dispatch_down( device, 1u, ui_cn_float( step->m_a ), ui_cn_float( step->m_b ) ); break;
                case 3:
                    if ( device.m_touches.empty( ) )
                        device.m_touches.resize( 1 );
                    device.m_touches[ 0 ].m_point[ 0 ] = ui_cn_float( step->m_a );
                    device.m_touches[ 0 ].m_point[ 1 ] = ui_cn_float( step->m_b );
                    ui_touch_dispatch_move( device );
                    break;
                default: ui_touch_dispatch_up( device, 1u, 0.0f, 0.0f ); break;
                }
                ++steps;
                std::string after;
                vujb_dump( doc_root, after );
                // one count per failing step
                const char* what = device.m_unhandled ? "the touch dispatch (an unhandled callback)" : bind.m_config_faults ? "a selector path not recreated" :
                                   after != step->m_after ? "the root after" : ui_cx_combo_text( c.base( ).m_combos ) != step->m_combos ? "the combo records" :
                                   c.base( ).m_focus != step->m_focus ? "the focus" : ui_cn_nav_hex( c.m_nav ) != step->m_nav ? "the nav bytes" : nullptr;
                if ( what )
                    bad( what, row );
            }
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "c_ui_binding's selector glue (bind_config, config_setup, h.touch's registration, the device's dispatch) matches retail's navigation on %d GameConfig entities: "
                     "%d runs, %d steps (root, combos, focus, nav bytes), %d platform answers", static_cast< int >( g_golden_cn_entities_count ), runs, steps, answers );
    else
        LOG_ERROR( "the selector glue FAILED %d checks against retail's", failed );
    return failed == 0;
}

// The same navigation on each BoatConfig* screen's VuBoatConfigEntity (oracle
// case boat_nav): the base functions over the BOAT class virtuals (dock.hxx
// ui_dock_class: slots 17..26 and slot 12 0x44E2B0 in the change path 0x449830),
// setup = OUR slot 4 (ui_dock_build) + slot 6 0x44CD10 (ui_dock_initialize).
inline bool verify_ui_boat_nav( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    int failed = 0, runs = 0, steps = 0, calls = 0;
    auto bad = [ &failed ]( const char* what, int k )
    {
        if ( ++failed <= 8 )
            LOG_ERROR( "ui boat nav: %s differs from retail (step %d)", what, k );
    };
    const value_t boat_db = ui_cx_document( 0 ), skin_db = ui_cx_document( 1 ), champ_db = ui_cx_document( 3 ), option_db = ui_cx_document( 4 );
    static const float inverse[ 16 ] = { 1.25f, 0.03125f, 0, 0, -0.0625f, 0.875f, 0, 0, 0, 0, 1, 0, -0.1f, 0.05f, 0, 1 };
    for ( std::size_t k = 0; k < g_golden_bn_entities_count; ++k )
    {
        const golden_cn_entity_t& want = g_golden_bn_entities[ k ];
        const entity_t* have = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, want.m_project ) == 0 && want.m_index >= 0 &&
                 static_cast< std::size_t >( want.m_index ) < loaded.trees[ p ].size( ) )
                have = loaded.trees[ p ][ static_cast< std::size_t >( want.m_index ) ].second;
        if ( !have || have->m_type != "VuBoatConfigEntity" || have->m_name != want.m_name )
        {
            bad( "the authored entity", static_cast< int >( k ) );
            continue;
        }
        ui_config_nav_t nav0;
        std::uint32_t bits[ 14 ] = { };
        auto take = [ & ]( const char* name, int words, int at )
        {
            if ( const ui_slot_t* v = ui_property_value( *have, name ) )
                for ( int w = 0; w < words; ++w )
                    bits[ at + w ] = v->m_bits[ w ];
            else
                bad( "a nav property (missing)", static_cast< int >( k ) );
        };
        take( "Screen Scale", 2, 0 );
        take( "Position X", 1, 2 );
        take( "Position Y", 1, 3 );
        take( "Spacing", 1, 4 );
        std::memcpy( &bits[ 5 ], &nav0.m_scroll_time, 4 );
        const char* const rects[ ] = { "TouchLeft X", "TouchLeft Y", "TouchLeft Width", "TouchLeft Height",
                                       "TouchRight X", "TouchRight Y", "TouchRight Width", "TouchRight Height" };
        for ( int r = 0; r < 8; ++r )
            take( rects[ r ], 1, 6 + r );
        for ( int w = 0; w < 14; ++w )
            if ( bits[ w ] != want.m_floats[ w ] )
                bad( "a nav float", static_cast< int >( k ) );
        nav0.m_scale[ 0 ] = ui_cn_float( bits[ 0 ] );
        nav0.m_scale[ 1 ] = ui_cn_float( bits[ 1 ] );
        nav0.m_position[ 0 ] = ui_cn_float( bits[ 2 ] );
        nav0.m_position[ 1 ] = ui_cn_float( bits[ 3 ] );
        nav0.m_spacing = ui_cn_float( bits[ 4 ] );
        for ( int r = 0; r < 4; ++r )
        {
            nav0.m_left[ r ] = ui_cn_float( bits[ 6 + r ] );
            nav0.m_right[ r ] = ui_cn_float( bits[ 10 + r ] );
        }
        if ( ui_cn_nav_hex( nav0 ) != want.m_initial )
            bad( "the ctor's nav bytes", static_cast< int >( k ) );
        const ui_front_end_screen_t screen = ui_front_end_screen( *have, want.m_flags );
        const std::vector< std::uint8_t > authored = ui_unhex( want.m_authored );
        if ( screen.m_option_type != want.m_option_type || authored.size( ) < 3 || screen.m_single_player != ( authored[ 0 ] != 0 ) ||
             screen.m_force_online != ( authored[ 1 ] != 0 ) || screen.m_allow_special != ( authored[ 2 ] != 0 ) )
            bad( "the authored properties", static_cast< int >( k ) );
        for ( int r = 0; r < want.m_runs; ++r )
        {
            const golden_cn_run_t& run = g_golden_bn_runs[ want.m_first + r ];
            value_t root = c_reader( ui_unhex( run.m_root ) ).parse( );
            ui_config_env_t env;
            env.m_root = &root;
            const golden_cn_step_t* step = nullptr;
            int at = 0;
            bool hooks_same = true;
            auto next = [ & ]( int kind ) -> const golden_bci_log_t*
            {
                if ( !step || at >= step->m_logs || g_golden_bn_logs[ step->m_first_log + at ].m_kind != kind )
                {
                    hooks_same = false;
                    return nullptr;
                }
                ++calls;
                return &g_golden_bn_logs[ step->m_first_log + at++ ];
            };
            ui_profile_t p;
            p.unlock = [ & ]( int slot, const std::string& a, const std::string& b, const std::string& c )
            {
                const golden_bci_log_t* g = next( 0 );
                hooks_same = hooks_same && g && g->m_slot == slot && a == g->m_a && b == g->m_b && c == g->m_c;
                return g && g->m_answer != 0;
            };
            p.license = [ & ]( ) { const golden_bci_log_t* g = next( 1 ); return g && g->m_answer != 0; };
            p.progress = [ & ]( const std::string& value )
            {
                const golden_bci_log_t* g = next( 2 );
                hooks_same = hooks_same && g && value == g->m_a;
                return g && g->m_answer != 0;
            };
            p.store_beta = [ & ]( ) { const golden_bci_log_t* g = next( 3 ); return g && g->m_answer != 0; };
            p.package = [ & ]( const std::string& asset )
            {
                const golden_bci_log_t* g = next( 8 );
                hooks_same = hooks_same && g && asset == g->m_a;
                return g && g->m_answer != 0;
            };
            ui_config_hooks_t h;
            h.touch = [ & ]( ) { next( 4 ); };
            h.draw_add = [ & ]( ) { next( 13 ); };
            h.fire = [ & ]( const std::string& plug, const ui_params_t& params )
            {
                const golden_bci_log_t* g = next( 5 );
                hooks_same = hooks_same && g && plug == g->m_a && ui_bci_hex( params ) == g->m_b;
                return g != nullptr;
            };
            h.broadcast = [ & ]( std::uint32_t hash, const ui_params_t& params )
            {
                const golden_bci_log_t* g = next( 6 );
                char text[ 16 ];
                std::snprintf( text, sizeof( text ), "0x%08X", hash );
                hooks_same = hooks_same && g && std::strcmp( g->m_a, text ) == 0 && ui_bci_hex( params ) == g->m_b;
            };
            h.sound = [ & ]( const char* event )
            {
                const golden_bci_log_t* g = next( 7 );
                hooks_same = hooks_same && g && std::strcmp( g->m_a, event ) == 0;
            };
            ui_dock_t d;
            d.m_base.m_single_player = screen.m_single_player;
            d.m_base.m_force_online = screen.m_force_online;
            d.m_allow_special = screen.m_allow_special;
            d.m_boat_db = &boat_db;
            d.m_skin_db = &skin_db;
            d.m_champ_db = &champ_db;
            ui_config_nav_t n = nav0;
            value_t doc;
            ui_config_class_t cls;
            ui_dock_class( cls, d, env, p, h );
            ++runs;
            for ( int s = 0; s < run.m_steps; ++s )
            {
                step = &g_golden_bn_steps[ run.m_first + s ];
                const int row = run.m_first + s;
                at = 0;
                hooks_same = true;
                bool done = true;
                switch ( step->m_op )
                {
                case 5:
                    done = ui_dock_build( d, env, doc, &option_db, screen.m_option_type, screen.m_flags, p, h ) && ui_dock_initialize( d, env, doc, p, h );
                    break;
                case 0:
                {
                    ui_params_t params;
                    if ( step->m_has_text )
                        params.add_string( step->m_text );
                    done = ui_config_input( d.m_base, env, doc, params, cls, p, h );
                    break;
                }
                case 1:
                    done = ui_config_tick( d.m_base, n, env, doc, ui_cn_float( step->m_a ), cls, p, h );
                    break;
                case 2:
                {
                    const float point[ 2 ] = { ui_cn_float( step->m_a ), ui_cn_float( step->m_b ) };
                    ui_config_touch_down( n, inverse, point );
                    break;
                }
                case 3:
                {
                    next( 15 );
                    const float point[ 2 ] = { ui_cn_float( step->m_a ), ui_cn_float( step->m_b ) };
                    ui_config_touch_move( n, inverse, point );
                    break;
                }
                default:
                    ui_config_touch_up( n );
                    break;
                }
                ++steps;
                std::string after;
                vujb_dump( root, after );
                if ( !done )
                    bad( "the step (a path not recreated)", row );
                if ( after != step->m_after )
                    bad( "the root after", row );
                if ( ui_cx_combo_text( d.m_base.m_combos ) != step->m_combos )
                    bad( "the combo records", row );
                if ( d.m_base.m_focus != step->m_focus )
                    bad( "the focus", row );
                if ( ui_cn_nav_hex( n ) != step->m_nav )
                    bad( "the nav bytes", row );
                if ( !hooks_same || at != step->m_logs )
                    bad( "the hook calls", row );
            }
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_config_input / touch / tick match retail's VuBaseConfigEntity navigation over the BOAT class virtuals (slot 12 "
                     "0x44E2B0 in the change path) on %d BoatConfig entities from our loaded properties: %d runs, %d steps (root, combos, "
                     "focus, nav bytes), %d hook calls",
            static_cast< int >( g_golden_bn_entities_count ), runs, steps, calls );
    else
        LOG_ERROR( "the boat config navigation FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_game_config_next / ui_game_config_done (hydro core/ui/select.hxx: slot 26
// 0x45E300 over slot 17 0x45D720 and slot 18 0x45D0C0) against retail's slot
// 26 called directly on every option of each GameConfig* screen's entity
// (oracle case config_next: two roots, six answer seeds): the result, the out
// text and every progress / unlock call in order with its arguments. Force
// Online from OUR loaded entity, compared with retail's authored byte.
inline bool verify_ui_config_next( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    int failed = 0, runs = 0, calls = 0;
    auto bad = [ &failed ]( const char* what, int k )
    {
        if ( ++failed <= 8 )
            LOG_ERROR( "ui config next: %s differs from retail (call %d)", what, k );
    };
    for ( std::size_t k = 0; k < g_golden_nx_entities_count; ++k )
    {
        const golden_nx_entity_t& want = g_golden_nx_entities[ k ];
        const entity_t* have = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, want.m_project ) == 0 && want.m_index >= 0 &&
                 static_cast< std::size_t >( want.m_index ) < loaded.trees[ p ].size( ) )
                have = loaded.trees[ p ][ static_cast< std::size_t >( want.m_index ) ].second;
        if ( !have || have->m_type != "VuGameConfigEntity" || have->m_name != want.m_name )
        {
            bad( "the authored entity", static_cast< int >( k ) );
            continue;
        }
        const ui_front_end_screen_t screen = ui_front_end_screen( *have );
        const std::vector< std::uint8_t > authored = ui_unhex( want.m_authored );
        if ( authored.size( ) < 2 || screen.m_single_player != ( authored[ 0 ] != 0 ) || screen.m_force_online != ( authored[ 1 ] != 0 ) )
            bad( "the authored bytes", static_cast< int >( k ) );
        ui_config_entity_t e;
        e.m_single_player = screen.m_single_player;
        e.m_force_online = screen.m_force_online;
        for ( int c = 0; c < want.m_calls; ++c )
        {
            const golden_nx_call_t& call = g_golden_nx_calls[ want.m_first + c ];
            const int row = want.m_first + c;
            const value_t root = c_reader( ui_unhex( call.m_root ) ).parse( );
            const value_t option = c_reader( ui_unhex( call.m_option ) ).parse( );
            int at = 0;
            bool hooks_same = true;
            auto next = [ & ]( int kind ) -> const golden_bci_log_t*
            {
                if ( at >= call.m_logs || g_golden_nx_logs[ call.m_first_log + at ].m_kind != kind )
                {
                    hooks_same = false;
                    return nullptr;
                }
                ++calls;
                return &g_golden_nx_logs[ call.m_first_log + at++ ];
            };
            ui_profile_t p;
            p.has_progress = std::strcmp( call.m_seed, "noprog" ) != 0;
            p.unlock = [ & ]( int slot, const std::string& a, const std::string& b, const std::string& cc )
            {
                const golden_bci_log_t* g = next( 0 );
                hooks_same = hooks_same && g && g->m_slot == slot && a == g->m_a && b == g->m_b && cc == g->m_c;
                return g && g->m_answer != 0;
            };
            p.race_done = [ & ]( const std::string& mode, const std::string& track, const std::string& course )
            {
                const golden_bci_log_t* g = next( 11 );
                hooks_same = hooks_same && g && mode == g->m_a && track == g->m_b && course == g->m_c;
                return g && g->m_answer != 0;
            };
            p.champ_done = [ & ]( const std::string& champ )
            {
                const golden_bci_log_t* g = next( 10 );
                hooks_same = hooks_same && g && champ == g->m_a;
                return g && g->m_answer != 0;
            };
            p.ghost_done = [ & ]( const std::string& track )
            {
                const golden_bci_log_t* g = next( 16 );
                hooks_same = hooks_same && g && track == g->m_a;
                return g && g->m_answer != 0;
            };
            std::string out;
            const bool result = ui_game_config_next( e, root, option, p, out );
            ++runs;
            if ( result != ( call.m_result != 0 ) )
                bad( "the result", row );
            if ( ui_cx_hex( out ) != call.m_out )
                bad( "the out text", row );
            if ( !hooks_same || at != call.m_logs )
                bad( "the hook calls", row );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_game_config_next matches retail's VuGameConfigEntity slot 26 0x45E300 (slot 17 0x45D720, slot 18 0x45D0C0) on %d "
                     "GameConfig entities: %d calls, the result, the out text, %d progress / unlock calls",
            static_cast< int >( g_golden_nx_entities_count ), runs, calls );
    else
        LOG_ERROR( "the game selector's next choice FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_screen_draw (core/ui compose.hxx) against retail's VuUIScreenEntity draw
// slot 13 0x629D60 (oracle case screen_draw) on every Screens/* root under three
// environments (alpha, 0x81A184, the sort's +0x74, the screen's +0x5C): every
// draw reached in order (the screen's own component = child -1, then the
// direct children: component, alpha bits, both stacks' tops and sizes, the sort
// key), then the sizes and the key after. OUR trees decide which entities have
// a UI draw component; the ctor's +0x5C (4) is compared with retail's.
inline bool verify_ui_screen_draw( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    int failed = 0, calls = 0;
    for ( std::size_t r = 0; r < g_golden_scr_runs_count; ++r )
    {
        const golden_scr_run_t& run = g_golden_scr_runs[ r ];
        const golden_scr_env_t& se = g_golden_scr_envs[ run.m_env ];
        const golden_txt_env_t& e = g_golden_txt_envs[ se.m_txt ];
        const entity_t* root = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count && !root; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, run.m_project ) == 0 )
                root = loaded.roots[ p ].get( );
        if ( !root || root->m_type != "VuUIScreenEntity" || root->m_children.size( ) != static_cast< std::size_t >( run.m_children ) ||
             run.m_layer0 != 4u )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui screen draw: %s has no loaded screen root, a different child count or +0x5C", run.m_project );
            continue;
        }
        ui_gfx_sort_t sort;
        sort.m_index = e.m_index;
        sort.m_key_lo = e.m_key_lo;
        sort.m_key_hi = se.m_key_hi;
        ui_gfx_stacks_t stacks;
        stacks.m_matrices.resize( static_cast< std::size_t >( run.m_matrices_before ) );
        stacks.m_floats.resize( static_cast< std::size_t >( run.m_floats_before ) );
        ui_impl_t impl;
        const std::vector< std::uint8_t > crop = ui_unhex( run.m_crop );
        std::memcpy( impl.m_crop.m_matrix, crop.data( ), 64 );
        std::memcpy( &impl.m_float, &se.m_float, 4 );
        float alpha;
        std::memcpy( &alpha, &se.m_alpha, 4 );
        bool same = true;
        int at = 0;
        ui_screen_draw( sort, stacks, impl, *root, se.m_layer, alpha, [ & ]( const entity_t& child, int component, float a )
        {
            if ( at >= run.m_calls )
            {
                same = false;
                return;
            }
            const golden_hud_call_t& want = g_golden_scr_calls[ run.m_first + at++ ];
            int index = -2;
            if ( &child == root )
                index = -1;
            for ( std::size_t k = 0; k < root->m_children.size( ); ++k )
                if ( root->m_children[ k ].get( ) == &child )
                    index = static_cast< int >( k );
            std::uint32_t bits, top;
            std::memcpy( &bits, &a, 4 );
            std::memcpy( &top, &stacks.m_floats.back( ), 4 );
            const std::vector< std::uint8_t > matrix = ui_unhex( want.m_matrix );
            same = same && index == want.m_child && component == want.m_component && bits == want.m_alpha &&
                   stacks.m_matrices.size( ) == static_cast< std::size_t >( want.m_matrices ) &&
                   std::memcmp( stacks.m_matrices.back( ).data( ), matrix.data( ), 64 ) == 0 &&
                   stacks.m_floats.size( ) == static_cast< std::size_t >( want.m_floats ) && top == want.m_float &&
                   sort.m_key_lo == want.m_key[ 0 ] && sort.m_key_hi == want.m_key[ 1 ];
            ++calls;
        } );
        same = same && at == run.m_calls && stacks.m_matrices.size( ) == static_cast< std::size_t >( run.m_matrices_after ) &&
               stacks.m_floats.size( ) == static_cast< std::size_t >( run.m_floats_after ) && sort.m_key_lo == run.m_key[ 0 ] && sort.m_key_hi == run.m_key[ 1 ];
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "ui screen draw: %s env %d reached %d draws, retail %d - or a call / stack / key differs", run.m_project, run.m_env, at, run.m_calls );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_screen_draw matches retail's VuUIScreenEntity draw slot 13 0x629D60 (0x50B4B0, 0x50B530, 0x628070) on %d runs over the "
                     "loaded Screens/* roots: %d draws - entity, component, alpha, both stacks' tops and sizes, the sort key - and the state after",
            static_cast< int >( g_golden_scr_runs_count ), calls );
    else
        LOG_ERROR( "the screen draw FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_screen_frame (core/ui frame.hxx, THE EXPORT) on every Screens/* root
// under screen_draw's first environment: its depth-0 leaves must be exactly
// retail's screen-level draws minus the containers (which the export walks
// into), in order, with the same component, alpha bits and sort key. The
// containers' own walks are verify_ui_container_draw's.
inline bool verify_ui_screen_frame( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    int failed = 0, screens = 0, leaves = 0, texts = 0, unrecreated = 0, containers = 0;
    for ( std::size_t r = 0; r < g_golden_scr_runs_count; ++r )
    {
        const golden_scr_run_t& run = g_golden_scr_runs[ r ];
        if ( run.m_env != 0 )
            continue;
        const golden_scr_env_t& se = g_golden_scr_envs[ 0 ];
        const golden_txt_env_t& e = g_golden_txt_envs[ se.m_txt ];
        const entity_t* root = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count && !root; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, run.m_project ) == 0 )
                root = loaded.roots[ p ].get( );
        if ( !root )
        {
            ++failed;
            continue;
        }
        ui_gfx_sort_t sort;
        sort.m_index = e.m_index;
        sort.m_key_lo = e.m_key_lo;
        sort.m_key_hi = se.m_key_hi;
        ui_impl_t impl;
        const std::vector< std::uint8_t > crop = ui_unhex( run.m_crop );
        std::memcpy( impl.m_crop.m_matrix, crop.data( ), 64 );
        std::memcpy( &impl.m_float, &se.m_float, 4 );
        float alpha;
        std::memcpy( &alpha, &se.m_alpha, 4 );
        const ui_frame_t frame = ui_screen_frame( *root, impl, sort, alpha, se.m_layer );
        std::vector< const ui_frame_draw_t* > top;
        for ( const ui_frame_draw_t& d : frame.m_draws )
        {
            if ( d.m_depth == 0 )
                top.push_back( &d );
            texts += d.m_kind == ui_frame_kind_t::text ? 1 : 0;        }
        std::size_t at = 0;
        bool same = true;
        for ( int c = 0; c < run.m_calls; ++c )
        {
            const golden_hud_call_t& want = g_golden_scr_calls[ run.m_first + c ];
            if ( want.m_child >= 0 && root->m_children[ static_cast< std::size_t >( want.m_child ) ]->m_type == "VuUIContainerEntity" )
                continue;
            if ( at >= top.size( ) )
            {
                same = false;
                break;
            }
            const ui_frame_draw_t& d = *top[ at++ ];
            const entity_t* who = want.m_child < 0 ? root : root->m_children[ static_cast< std::size_t >( want.m_child ) ].get( );
            std::uint32_t bits, ftop;
            std::memcpy( &bits, &d.m_alpha, 4 );
            std::memcpy( &ftop, &d.m_top, 4 );
            const std::vector< std::uint8_t > matrix = ui_unhex( want.m_matrix );
            same = same && d.m_entity == who && d.m_component == want.m_component && bits == want.m_alpha && ftop == want.m_float &&
                   std::memcmp( d.m_matrix.data( ), matrix.data( ), 64 ) == 0 && d.m_key_lo == want.m_key[ 0 ] && d.m_key_hi == want.m_key[ 1 ];
        }
        same = same && at == top.size( );
        ++screens;
        leaves += static_cast< int >( frame.m_draws.size( ) );
        unrecreated += frame.m_unrecreated;
        containers += frame.m_containers;
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "ui screen frame: %s's screen-level leaves differ from retail's draws", run.m_project );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_screen_frame (the export) reproduces retail's screen-level draws on %d Screens/* roots: %d leaf draws through %d "
                     "container walks - %d text (ui_text_draw), %d image / button (image.hxx), %d whose draw is NOT recreated (other types)",
            screens, leaves, containers, texts, leaves - texts - unrecreated, unrecreated );
    else
        LOG_ERROR( "the screen frame export FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_image_component_draw / ui_image_draw (core/ui image.hxx) against retail's
// VuUIImageEntity slot 15 0x4401C0 -> slot 13 0x629300 (oracle case image_draw)
// on every entity whose slot 13 is 0x629300 in the loaded projects, then a
// sweep: each new command's words and record byte for byte, in ONE sort per
// buffer through all vectors. For the real entities the fields are OUR loaded
// properties (compared with retail's bytes first); +0xB4 is retail's.
inline bool verify_ui_image_draw( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    ui_gfx_sort_t sort;   // TxtWorld's buffers: 0x300000 / 0x100000 bytes, buffer 1 with 0x35 used
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    int failed = 0, commands = 0, fields = 0, real = 0;
    for ( std::size_t k = 0; k < g_golden_img_vectors_count; ++k )
    {
        const golden_img_vector_t& v = g_golden_img_vectors[ k ];
        const golden_txt_env_t& e = g_golden_txt_envs[ v.m_env ];
        ui_crop_t crop;
        std::memcpy( &crop.m_matrix[ 0 ], &e.m_crop[ 0 ], 4 );
        std::memcpy( &crop.m_matrix[ 5 ], &e.m_crop[ 1 ], 4 );
        std::memcpy( &crop.m_matrix[ 12 ], &e.m_crop[ 2 ], 4 );
        std::memcpy( &crop.m_matrix[ 13 ], &e.m_crop[ 3 ], 4 );
        ui_font_draw_env_t env;
        const std::vector< std::uint8_t > matrix = ui_unhex( v.m_matrix );
        std::memcpy( env.m_matrix, matrix.data( ), 64 );
        env.m_texture_material = g_golden_img_materials[ 0 ][ 0 ];
        env.m_texture_material_key = g_golden_img_materials[ 0 ][ 1 ];
        env.m_rect_material = g_golden_img_materials[ 1 ][ 0 ];
        env.m_rect_material_key = g_golden_img_materials[ 1 ][ 1 ];
        const std::vector< std::uint8_t > b = ui_unhex( v.m_fields );
        const auto at = [ &b ]( int offset ) { return b.data( ) + ( offset - 0x6C ); };
        ui_image_t im;
        im.m_visible = *at( 0x6C );
        std::memcpy( &im.m_colour, at( 0x6D ), 4 );
        std::memcpy( im.m_screen_scale, at( 0x74 ), 8 );
        std::memcpy( im.m_dst, at( 0x7C ), 16 );
        std::memcpy( im.m_src, at( 0x8C ), 16 );
        std::memcpy( &im.m_rotation, at( 0x9C ), 4 );
        im.m_flip_x = *at( 0xA0 );
        im.m_flip_y = *at( 0xA1 );
        im.m_absolute = *at( 0xA2 );
        std::memcpy( &im.m_anchor.m_horizontal, at( 0xA4 ), 4 );
        std::memcpy( &im.m_anchor.m_vertical, at( 0xA8 ), 4 );
        std::memcpy( &im.m_anchor.m_h_ratio, at( 0xAC ), 4 );
        std::memcpy( &im.m_anchor.m_v_ratio, at( 0xB0 ), 4 );
        std::memcpy( &im.m_alpha, at( 0xB4 ), 4 );
        std::memcpy( &im.m_depth, &v.m_depth, 4 );
        if ( v.m_sweep == 0 )
        {
            const entity_t* have = nullptr;
            for ( std::size_t p = 0; p < g_golden_ui_projects_count && !have; ++p )
                if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, v.m_project ) == 0 && v.m_index < static_cast< int >( loaded.trees[ p ].size( ) ) )
                    have = loaded.trees[ p ][ static_cast< std::size_t >( v.m_index ) ].second;
            const ui_state_t* st = have && ui_is_state( *have ) ? static_cast< const ui_state_t* >( have->m_state.get( ) ) : nullptr;
            const ui_state_t* draw = nullptr;
            for ( const entity_component_t& c : have ? have->m_components : std::vector< entity_component_t >( ) )
                if ( c.m_vtable == k_ui_draw_component_vtable && c.m_state )
                    draw = static_cast< const ui_state_t* >( c.m_state.get( ) );
            bool same = st && draw && have->m_ctor && have->m_ctor->m_vtable == v.m_vtable;
            const auto take = [ & ]( const char* name, void* target, int bytes, const ui_state_t* from )
            {
                const ui_slot_t* slot = from ? ui_slot_named( *from, name ) : nullptr;
                if ( !slot )
                {
                    same = false;
                    return;
                }
                if ( std::memcmp( target, slot->m_bits, static_cast< std::size_t >( bytes ) ) != 0 )
                    same = false;
                std::memcpy( target, slot->m_bits, static_cast< std::size_t >( bytes ) );
                ++fields;
            };
            if ( same )
            {
                take( "Visible", &im.m_visible, 1, st );
                take( "Color", &im.m_colour, 4, st );
                take( "Screen Scale", im.m_screen_scale, 8, st );
                take( "DstX", &im.m_dst[ 0 ], 4, st );
                take( "DstY", &im.m_dst[ 1 ], 4, st );
                take( "DstWidth", &im.m_dst[ 2 ], 4, st );
                take( "DstHeight", &im.m_dst[ 3 ], 4, st );
                take( "SrcX", &im.m_src[ 0 ], 4, st );
                take( "SrcY", &im.m_src[ 1 ], 4, st );
                take( "SrcWidth", &im.m_src[ 2 ], 4, st );
                take( "SrcHeight", &im.m_src[ 3 ], 4, st );
                take( "Rotation", &im.m_rotation, 4, st );
                take( "FlipX", &im.m_flip_x, 1, st );
                take( "FlipY", &im.m_flip_y, 1, st );
                take( "Absolute Coords", &im.m_absolute, 1, st );
                take( "Horizontal Anchor", &im.m_anchor.m_horizontal, 4, st );
                take( "Horizontal Ratio", &im.m_anchor.m_h_ratio, 4, st );
                take( "Vertical Anchor", &im.m_anchor.m_vertical, 4, st );
                take( "Vertical Ratio", &im.m_anchor.m_v_ratio, 4, st );
                take( "Depth", &im.m_depth, 4, draw );
            }
            if ( !same && ++failed <= 8 )
                LOG_ERROR( "ui image draw: %s entity %d: our loaded fields differ from retail's", v.m_project, v.m_index );
            ++real;
        }
        const golden_img_texture_t& gt = g_golden_img_textures[ v.m_texture ];
        const ui_texture_t texture = { gt.m_handle, gt.m_width, gt.m_height };
        float alpha, aspect;
        std::memcpy( &alpha, &v.m_alpha, 4 );
        std::memcpy( &aspect, &v.m_aspect, 4 );
        sort.m_index = e.m_index;
        sort.m_key_lo = e.m_key_lo;
        sort.m_key_hi = e.m_key_hi;
        std::vector< ui_gfx_command_t >& list = sort.m_commands[ e.m_index ];
        const std::size_t before = list.size( );
        ui_image_component_draw( sort, env, crop, aspect, im, gt.m_handle ? &texture : nullptr, alpha );
        bool same = list.size( ) - before == static_cast< std::size_t >( v.m_commands ) && v.m_stack[ 0 ] == v.m_stack[ 2 ] && v.m_stack[ 1 ] == v.m_stack[ 3 ];
        for ( int c = 0; same && c < v.m_commands; ++c )
        {
            const golden_txt_command_t& want = g_golden_img_commands[ v.m_first + c ];
            const ui_gfx_command_t& have = list[ before + static_cast< std::size_t >( c ) ];
            const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
            const auto& memory = sort.m_buffers[ e.m_index ].m_memory;
            same = std::memcmp( have.m_words, want.m_words, sizeof( have.m_words ) ) == 0 && have.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                   std::memcmp( memory.data( ) + have.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
            ++commands;
        }
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "ui image draw: vector %d (sweep %d, %s %d) gave %d commands, retail %d - or a word / record byte differs", static_cast< int >( k ),
                v.m_sweep, v.m_project, v.m_index, static_cast< int >( list.size( ) - before ), v.m_commands );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_image_draw matches retail's VuUIImageEntity draw (slot 15 0x4401C0 -> slot 13 0x629300: anchor 0x6282D0, 0x410AB0 / "
                     "0x407DB0 / 0x40B5F0 / 0x4078C0, 0x50B4B0, texture draw 0x507360 / 0x507830) on %d vectors (%d on real images, %d of their "
                     "loaded fields): %d commands and records byte for byte",
            static_cast< int >( g_golden_img_vectors_count ), real, fields, commands );
    else
        LOG_ERROR( "the UI image draw FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_button_draw (core/ui image.hxx) against retail's VuBarracudaUIButtonEntity
// draw 0x443280 (oracle case button_draw) on every button in the loaded projects
// (focus / press / textures / font / text per run), then a sweep: each new
// command's words and record byte for byte, in ONE sort through all vectors. For
// the real buttons the property-backed fields are OUR loaded ones (compared with
// retail's bytes first) and the font params bytes 0..40 come from OUR "Font"
// over the real FontDB (41..59 are retail's indeterminate stack: its bytes).
inline bool verify_ui_button_draw( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    constexpr std::uint32_t k_material[ 2 ] = { 0x5D001000u, 0x5D001800u };   // TxtWorld's font materials
    int failed = 0, commands = 0, fields = 0, real = 0, fonts = 0;
    for ( std::size_t k = 0; k < g_golden_btn_vectors_count; ++k )
    {
        const golden_btn_vector_t& v = g_golden_btn_vectors[ k ];
        const golden_txt_env_t& e = g_golden_txt_envs[ v.m_env ];
        ui_crop_t crop;
        std::memcpy( &crop.m_matrix[ 0 ], &e.m_crop[ 0 ], 4 );
        std::memcpy( &crop.m_matrix[ 5 ], &e.m_crop[ 1 ], 4 );
        std::memcpy( &crop.m_matrix[ 12 ], &e.m_crop[ 2 ], 4 );
        std::memcpy( &crop.m_matrix[ 13 ], &e.m_crop[ 3 ], 4 );
        ui_font_draw_env_t env;
        const std::vector< std::uint8_t > matrix = ui_unhex( v.m_matrix );
        std::memcpy( env.m_matrix, matrix.data( ), 64 );
        std::memcpy( &env.m_top, &e.m_top, 4 );
        for ( int m = 0; m < 2; ++m )
        {
            env.m_material[ m ] = k_material[ m ];
            env.m_material_key[ m ] = g_golden_txt_material_keys[ m ];
        }
        env.m_texture_material = g_golden_btn_material[ 0 ];
        env.m_texture_material_key = g_golden_btn_material[ 1 ];
        const std::vector< std::uint8_t > raw = ui_unhex( v.m_fields );
        const auto at = [ &raw ]( int offset ) { return raw.data( ) + ( offset - 0x64 ); };
        ui_button_draw_t b;
        b.m_enabled = *at( 0x68 );
        b.m_label = *at( 0x69 );
        std::memcpy( &b.m_selected_colour, at( 0x84 ), 4 );
        std::memcpy( b.m_screen_scale, at( 0x88 ), 8 );
        std::memcpy( b.m_rect, at( 0x90 ), 16 );
        std::memcpy( b.m_params, at( 0xC0 ), 60 );
        std::memcpy( &b.m_h_align, at( 0x104 ), 4 );
        std::memcpy( &b.m_v_align, at( 0x108 ), 4 );
        b.m_no_clip = *at( 0x10C );
        b.m_single_line = *at( 0x10D );
        b.m_wordbreak = *at( 0x10E );
        std::memcpy( &b.m_anchor.m_horizontal, at( 0x168 ), 4 );
        std::memcpy( &b.m_anchor.m_vertical, at( 0x16C ), 4 );
        std::memcpy( &b.m_anchor.m_h_ratio, at( 0x170 ), 4 );
        std::memcpy( &b.m_anchor.m_v_ratio, at( 0x174 ), 4 );
        b.m_focused = static_cast< std::uint8_t >( v.m_focused );
        b.m_press = static_cast< std::uint32_t >( v.m_press );
        std::memcpy( &b.m_depth, &v.m_depth, 4 );
        if ( v.m_sweep == 0 )
        {
            const entity_t* have = nullptr;
            for ( std::size_t p = 0; p < g_golden_ui_projects_count && !have; ++p )
                if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, v.m_project ) == 0 && v.m_index < static_cast< int >( loaded.trees[ p ].size( ) ) )
                    have = loaded.trees[ p ][ static_cast< std::size_t >( v.m_index ) ].second;
            const ui_state_t* st = have && ui_is_state( *have ) ? static_cast< const ui_state_t* >( have->m_state.get( ) ) : nullptr;
            const ui_state_t* draw = nullptr;
            for ( const entity_component_t& c : have ? have->m_components : std::vector< entity_component_t >( ) )
                if ( c.m_vtable == k_ui_draw_component_vtable && c.m_state )
                    draw = static_cast< const ui_state_t* >( c.m_state.get( ) );
            bool same = st && draw && have->m_ctor && have->m_ctor->m_vtable == v.m_vtable;
            const auto take = [ & ]( const char* name, void* target, int bytes, const ui_state_t* from )
            {
                const ui_slot_t* slot = from ? ui_slot_named( *from, name ) : nullptr;
                if ( !slot )
                {
                    same = false;
                    return;
                }
                if ( std::memcmp( target, slot->m_bits, static_cast< std::size_t >( bytes ) ) != 0 )
                    same = false;
                std::memcpy( target, slot->m_bits, static_cast< std::size_t >( bytes ) );
                ++fields;
            };
            if ( same )
            {
                take( "Enabled", &b.m_enabled, 1, st );
                take( "Label", &b.m_label, 1, st );
                take( "Selected Text Color", &b.m_selected_colour, 4, st );
                take( "Screen Scale", b.m_screen_scale, 8, st );
                take( "X", &b.m_rect[ 0 ], 4, st );
                take( "Y", &b.m_rect[ 1 ], 4, st );
                take( "Width", &b.m_rect[ 2 ], 4, st );
                take( "Height", &b.m_rect[ 3 ], 4, st );
                take( "Horizontal Alignment", &b.m_h_align, 4, st );
                take( "Vertical Alignment", &b.m_v_align, 4, st );
                take( "No Clip", &b.m_no_clip, 1, st );
                take( "Single Line", &b.m_single_line, 1, st );
                take( "Wordbreak", &b.m_wordbreak, 1, st );
                take( "Horizontal Anchor", &b.m_anchor.m_horizontal, 4, st );
                take( "Horizontal Ratio", &b.m_anchor.m_h_ratio, 4, st );
                take( "Vertical Anchor", &b.m_anchor.m_vertical, 4, st );
                take( "Vertical Ratio", &b.m_anchor.m_v_ratio, 4, st );
                take( "Depth", &b.m_depth, 4, draw );
                const ui_slot_t* font_name = ui_slot_named( *st, "Font" );
                if ( !font_name )
                    same = false;
                else
                {
                    std::uint8_t params[ 60 ];
                    std::memcpy( params, b.m_params, 60 );
                    std::string asset;
                    ui_font_params( ui_font_db( 0 ), font_name->m_text, params, asset );
                    if ( std::memcmp( params, b.m_params, 41 ) != 0 )
                        same = false;
                    std::memcpy( b.m_params, params, 60 );
                    ++fonts;
                }
            }
            if ( !same && ++failed <= 8 )
                LOG_ERROR( "ui button draw: %s entity %d: our loaded fields differ from retail's", v.m_project, v.m_index );
            ++real;
        }
        ui_texture_t textures[ 3 ];
        const ui_texture_t* chosen[ 3 ] = { };
        for ( int t = 0; t < 3; ++t )
        {
            const golden_img_texture_t& g = g_golden_btn_textures[ v.m_textures[ t ] ];
            textures[ t ] = { g.m_handle, g.m_width, g.m_height };
            chosen[ t ] = g.m_handle ? &textures[ t ] : nullptr;
        }
        ui_button_inputs_t in;
        const std::string mode = v.m_font;
        in.m_font = mode == "asset" ? g_golden_btn_fonts[ 0 ] : v.m_default ? g_golden_btn_fonts[ 1 ] : 0u;
        const std::vector< std::uint8_t > text = ui_unhex( v.m_text );
        in.m_text.assign( text.begin( ), text.end( ) );
        in.m_selected = chosen[ 0 ];
        in.m_unselected = chosen[ 1 ];
        in.m_pressed = chosen[ 2 ];
        ui_string_db_t db;
        db.m_present = true;   // the case always serves a StringDB (slot 7 is called unconditionally with a font)
        db.m_slot12 = e.m_sdb_answer != 0;
        float alpha;
        std::memcpy( &alpha, &v.m_alpha, 4 );
        sort.m_index = e.m_index;
        sort.m_key_lo = e.m_key_lo;
        sort.m_key_hi = e.m_key_hi;
        std::vector< ui_gfx_command_t >& list = sort.m_commands[ e.m_index ];
        const std::size_t before = list.size( );
        const bool known = ui_button_draw( sort, env, crop, db, b, in, alpha );
        bool same = known && list.size( ) - before == static_cast< std::size_t >( v.m_commands );
        for ( int c = 0; same && c < v.m_commands; ++c )
        {
            const golden_txt_command_t& want = g_golden_btn_commands[ v.m_first + c ];
            const ui_gfx_command_t& have = list[ before + static_cast< std::size_t >( c ) ];
            const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
            const auto& memory = sort.m_buffers[ e.m_index ].m_memory;
            same = std::memcmp( have.m_words, want.m_words, sizeof( have.m_words ) ) == 0 && have.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                   std::memcmp( memory.data( ) + have.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
            ++commands;
        }
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "ui button draw: vector %d (sweep %d, %s %d) gave %d commands, retail %d - or a word / record byte differs", static_cast< int >( k ),
                v.m_sweep, v.m_project, v.m_index, static_cast< int >( list.size( ) - before ), v.m_commands );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_button_draw matches retail's VuBarracudaUIButtonEntity draw 0x443280 (anchor 0x6282D0, flags 0x5546B0, font draw "
                     "0x536C10, texture draw 0x507360) on %d vectors (%d on real buttons, %d of their loaded fields, %d font params from FontDB): "
                     "%d commands and records byte for byte",
            static_cast< int >( g_golden_btn_vectors_count ), real, fields, fonts, commands );
    else
        LOG_ERROR( "the UI button draw FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_screen_frame WITH inputs (the export's records) against retail's screen
// draw 0x629D60 run with the text / image / button leaf draws live (oracle case
// screen_frame) on every Screens/* root under two environments: the sort's new
// commands (words + records, one sort per buffer through all runs), the leaves
// left `other` (retail's stubbed draws: entity, alpha) and the key after. The
// inputs mirror the case's hooks: a text's text = its name, a button's String
// ID answer = its name, the asset font everywhere, an image's texture A when its
// name length is odd, a button's A / B / none; params 41..59 retail's.
// Batch 13: the Info / config selector / settings combo / boat stat / progress
// bar leaves run live in retail too (case screen_frame EXTENDED) and are emitted
// here: Info / stat / bar from OUR ctor state (compared with retail's loaded
// bits), the selectors' option ComboBoxIDs + records (after retail's slot 4)
// and the settings combos' record are retail's; the combos' asset properties A /
// B by parity, both fonts the asset, VuStringDB "<id>~".
inline bool verify_ui_screen_frame_records( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    const ui_texture_t texture_a = { g_golden_frm_textures[ 0 ].m_handle, g_golden_frm_textures[ 0 ].m_width, g_golden_frm_textures[ 0 ].m_height };
    const ui_texture_t texture_b = { g_golden_frm_textures[ 1 ].m_handle, g_golden_frm_textures[ 1 ].m_width, g_golden_frm_textures[ 1 ].m_height };
    int failed = 0, runs = 0, commands = 0, stubs = 0, emitted = 0, live = 0, boards = 0;
    const value_t option_db = ui_cx_document( 4 );
    for ( std::size_t r = 0; r < g_golden_frm_runs_count; ++r )
    {
        const golden_frm_run_t& run = g_golden_frm_runs[ r ];
        const golden_frm_env_t& fe = g_golden_frm_envs[ run.m_env ];
        const golden_txt_env_t& e = g_golden_txt_envs[ fe.m_txt ];
        std::size_t project = g_golden_ui_projects_count;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, run.m_project ) == 0 )
                project = p;
        if ( project == g_golden_ui_projects_count || loaded.trees[ project ].empty( ) )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui screen frame records: %s not loaded", run.m_project );
            continue;
        }
        const auto& tree = loaded.trees[ project ];
        const auto index_of = [ &tree ]( const entity_t* who )
        {
            for ( std::size_t k = 0; k < tree.size( ); ++k )
                if ( tree[ k ].second == who )
                    return static_cast< int >( k );
            return -1;
        };
        ui_frame_inputs_t in;
        for ( int m = 0; m < 2; ++m )
        {
            in.m_env.m_material[ m ] = 0x5D001000u + 0x800u * static_cast< std::uint32_t >( m );   // TxtWorld's font materials
            in.m_env.m_material_key[ m ] = g_golden_txt_material_keys[ m ];
        }
        in.m_env.m_texture_material = g_golden_img_materials[ 0 ][ 0 ];
        in.m_env.m_texture_material_key = g_golden_img_materials[ 0 ][ 1 ];
        in.m_env.m_rect_material = g_golden_img_materials[ 1 ][ 0 ];
        in.m_env.m_rect_material_key = g_golden_img_materials[ 1 ][ 1 ];
        in.m_db.m_present = true;
        in.m_db.m_slot12 = e.m_sdb_answer != 0;
        in.m_fontdb = &ui_font_db( 0 );
        in.m_text = []( const entity_t& who ) { return who.m_name; };
        in.m_font = []( const entity_t& ) { ui_text_font_t f; f.m_asset = true; f.m_asset_font = 0x0F0E0D0Cu; return f; };
        in.m_texture = [ & ]( const entity_t& who, int which ) -> const ui_texture_t*
        {
            if ( ui_frame_kind_of( who ) == ui_frame_kind_t::image )
                return who.m_name.size( ) % 2 ? &texture_a : nullptr;
            return which == 0 ? &texture_a : which == 1 ? &texture_b : nullptr;
        };
        const auto tail_of = [ & ]( const entity_t& who, int kind, std::uint8_t params[ 60 ] )
        {
            const int at = index_of( &who );
            for ( int t = 0; t < run.m_tails; ++t )
            {
                const golden_frm_tail_t& g = g_golden_frm_tails[ run.m_first_tail + t ];
                if ( g.m_index == at && ( kind < 0 ? g.m_kind <= 1 : g.m_kind == kind ) )
                {
                    const std::vector< std::uint8_t > tail = ui_unhex( g.m_tail );
                    std::memcpy( params + 41, tail.data( ), 19 );
                }
            }
        };
        in.m_params_tail = [ & ]( const entity_t& who, std::uint8_t params[ 60 ] ) { tail_of( who, -1, params ); };
        in.m_combo_params_tail = [ & ]( const entity_t& who, int which, std::uint8_t params[ 60 ] ) { tail_of( who, 2 + which, params ); };
        // The live leaves (batch 13): Info / stat / bar from OUR ctor state (compared with retail's bits as loaded);
        // the config selectors' option vector + records and the settings combos' record are retail's (slot 4 and
        // the combo's own code build them - verified by verify_ui_config_expand / verify_ui_combo_draw).
        bool state_same = true;
        const auto bits = []( float f )
        {
            std::uint32_t b;
            std::memcpy( &b, &f, 4 );
            return b;
        };
        std::vector< const golden_frm_leaf_t* > leaf_of( tree.size( ), nullptr );
        std::vector< ui_config_entity_t > configs( tree.size( ) );
        std::vector< std::vector< value_t > > options( tree.size( ) );
        for ( int l = 0; l < run.m_leaves; ++l )
        {
            const golden_frm_leaf_t& g = g_golden_frm_leaves[ run.m_first_leaf + l ];
            if ( g.m_index < 0 || g.m_index >= static_cast< int >( tree.size( ) ) )
            {
                state_same = false;
                continue;
            }
            const std::size_t k = static_cast< std::size_t >( g.m_index );
            const entity_t& who = *tree[ k ].second;
            leaf_of[ k ] = &g;
            if ( g.m_kind == 0 )
            {
                const ui_info_ticker_t t = ui_info_ticker_of( who );
                const ui_text_base_t base;
                const std::uint32_t ours[ 5 ] = { bits( base.m_alpha ), bits( t.m_scroll.m_extent ), bits( t.m_scroll.m_gap ), bits( t.m_scroll.m_x ),
                                                  bits( t.m_scroll.m_y ) };
                state_same = state_same && who.m_type == "VuInfoUITextEntity" && std::memcmp( ours, g.m_words, sizeof( ours ) ) == 0;
            }
            else if ( g.m_kind == 3 )
                state_same = state_same && who.m_type == "VuBoatStatEntity" && static_cast< std::uint32_t >( ui_boat_stat_t( ).m_value ) == g.m_words[ 0 ];
            else if ( g.m_kind == 4 )
            {
                const ui_progress_bar_t b;
                state_same = state_same && who.m_type == "VuLoadingProgressBarEntity" && bits( b.m_progress ) == g.m_words[ 0 ] && b.m_visible == g.m_words[ 1 ];
            }
            else if ( g.m_kind == 5 )
            {
                // batch 25: the full leaderboard - OUR ctor records (ui_leaderboard_combos over the real OptionDB)
                // against the ones retail's fresh ctor 0x477FD0 built for the case
                ui_config_combo_t combos[ 2 ];
                ui_leaderboard_combos( &option_db, combos );
                state_same = state_same && ui_frame_kind_of( who ) == ui_frame_kind_t::leaderboard && g.m_records == 2;
                for ( int r = 0; state_same && r < 2; ++r )
                {
                    const golden_frm_record_t& gr = g_golden_frm_records[ g.m_first_record + r ];
                    state_same = combos[ r ].m_string_id == gr.m_id && static_cast< int >( combos[ r ].m_choice_ids.size( ) ) == gr.m_choices;
                    for ( int j = 0; state_same && j < gr.m_choices; ++j )
                        state_same = combos[ r ].m_choice_ids[ static_cast< std::size_t >( j ) ] == g_golden_frm_choices[ gr.m_first + j ].m_id &&
                                     combos[ r ].m_locked[ static_cast< std::size_t >( j ) ] == g_golden_frm_choices[ gr.m_first + j ].m_locked;
                }
            }
            else if ( g.m_kind == 6 )
                state_same = state_same && ui_frame_kind_of( who ) == ui_frame_kind_t::board;
            else
            {
                ui_config_entity_t& ce = configs[ k ];
                for ( int r = 0; r < g.m_records; ++r )
                {
                    const golden_frm_record_t& gr = g_golden_frm_records[ g.m_first_record + r ];
                    const std::vector< std::uint8_t > head = ui_unhex( gr.m_head );
                    ui_config_combo_t c;
                    c.m_focused = head[ 0 ];
                    c.m_b41 = head[ 1 ];
                    c.m_b42 = head[ 2 ];
                    std::memcpy( &c.m_index, head.data( ) + 4, 4 );
                    std::memcpy( &c.m_shown, head.data( ) + 8, 4 );
                    c.m_string_id = gr.m_id;
                    for ( int j = 0; j < gr.m_choices; ++j )
                    {
                        c.m_choice_ids.push_back( g_golden_frm_choices[ gr.m_first + j ].m_id );
                        c.m_locked.push_back( static_cast< std::uint8_t >( g_golden_frm_choices[ gr.m_first + j ].m_locked ) );
                    }
                    ce.m_combos.push_back( c );
                }
                options[ k ].resize( static_cast< std::size_t >( g.m_ids ) );
                for ( int i = 0; i < g.m_ids; ++i )
                {
                    value_t id;
                    id.kind = value_t::kind_t::integer;
                    id.tag = 1;
                    id.integer = g_golden_frm_ids[ g.m_first_id + i ];
                    value_t& o = options[ k ][ static_cast< std::size_t >( i ) ];
                    o.kind = value_t::kind_t::object;
                    o.tag = 6;
                    o.object.push_back( { "ComboBoxID", id } );
                }
                for ( const value_t& o : options[ k ] )
                    ce.m_options.push_back( &o );
                state_same = state_same && ( g.m_kind == 1 ? ui_frame_kind_of( who ) == ui_frame_kind_t::config : ui_frame_kind_of( who ) == ui_frame_kind_t::combo_box ) &&
                             ( g.m_kind == 1 || g.m_records == 1 );
            }
        }
        const auto leaf = [ & ]( const entity_t& who ) -> const golden_frm_leaf_t*
        {
            const int at = index_of( &who );
            return at >= 0 ? leaf_of[ static_cast< std::size_t >( at ) ] : nullptr;
        };
        in.m_info = [ & ]( const entity_t& who ) { return ui_info_ticker_of( who ).m_scroll; };
        in.m_stat = []( const entity_t& ) { return ui_boat_stat_t( ); };
        in.m_bar = []( const entity_t& ) { return ui_progress_bar_t( ); };
        in.m_config = [ & ]( const entity_t& who ) -> const ui_config_entity_t*
        {
            const int at = index_of( &who );
            return at >= 0 && leaf( who ) ? &configs[ static_cast< std::size_t >( at ) ] : nullptr;
        };
        in.m_combo = [ & ]( const entity_t& who ) -> const ui_config_combo_t*
        {
            const int at = index_of( &who );
            return at >= 0 && leaf( who ) && !configs[ static_cast< std::size_t >( at ) ].m_combos.empty( ) ? &configs[ static_cast< std::size_t >( at ) ].m_combos[ 0 ]
                                                                                                           : nullptr;
        };
        in.m_combo_inputs = [ & ]( const entity_t& who )
        {
            ui_combo_inputs_t ci;
            ci.m_label_font = ci.m_value_font = 0x0F0E0D0Cu;   // TXT_FONT, the stand-in asset font
            ci.m_string = []( const std::string& id ) { return id + "~"; };   // the case's VuStringDB answer
            const golden_frm_leaf_t* g = leaf( who );
            ci.m_texture = [ g, &texture_a, &texture_b ]( int which ) -> const ui_texture_t*
            {
                if ( !g || which < 0 || which >= 7 || g->m_textures[ which ] < 0 )
                    return nullptr;
                return g->m_textures[ which ] ? &texture_b : &texture_a;
            };
            return ci;
        };
        // The difficulty leaves (batch 13): the case's gameutil root over the real BoatDB / ChampDB, no dev
        // config, the round an empty container; Texture Asset C -> B, B / A -> A; every Font the asset.
        const std::vector< std::uint8_t > groot = ui_unhex( run.m_groot );
        const value_t diff_root = groot.empty( ) ? value_t( ) : c_reader( groot ).parse( );
        static const value_t boat_db = ui_fr_document( 0 ), champ_db = ui_fr_document( 3 ), empty_round{ };
        ui_difficulty_env_t de;
        de.m_root = &diff_root;
        de.m_boat_db = &boat_db;
        de.m_champ_db = &champ_db;
        de.m_round = []( ) -> const value_t& { return empty_round; };
        if ( !groot.empty( ) )
            in.m_difficulty = &de;
        in.m_class_texture = [ & ]( const entity_t&, int k ) -> const ui_texture_t* { return k == 0 ? &texture_b : &texture_a; };
        in.m_class_font = []( const entity_t&, int ) { ui_text_font_t f; f.m_asset = true; f.m_asset_font = 0x0F0E0D0Cu; return f; };
        in.m_class_params_tail = [ & ]( const entity_t& who, int k, std::uint8_t params[ 60 ] ) { tail_of( who, 4 + k, params ); };
        in.m_string = []( const std::string& id ) { return id + "~"; };   // the case's VuStringDB answer
        ui_impl_t impl;
        const std::vector< std::uint8_t > crop = ui_unhex( run.m_crop );
        std::memcpy( impl.m_crop.m_matrix, crop.data( ), 64 );
        std::memcpy( &impl.m_float, &fe.m_float, 4 );
        std::memcpy( &impl.m_aspect, &fe.m_aspect, 4 );
        // Batch 25: the leaderboards live (retail's 0x47A360 / 0x47DB40 in the case). OUR loaded fields / block /
        // font params 0..40 (41..59 retail's tails 7..11 / 12..14 - UNKNOWN), OUR ctor records; the state (focus,
        // press, FSM, the records' head bytes) and the manager's rows are the case's inputs. A record's focused
        // byte after the draw is compared too.
        const auto font_params = [ & ]( const entity_t& who, const char* name, int kind, ui_results_font_t& font )
        {
            std::memset( font.m_params, 0, 60 );
            tail_of( who, kind, font.m_params );
            const ui_slot_t* v = ui_property_value( who, name );
            std::string asset;
            ui_font_params( ui_font_db( 0 ), v ? v->m_text : std::string( ), font.m_params, asset );
            font.m_font = 0x0F0E0D0Cu;   // TXT_FONT: the case gives every font the stand-in asset
        };
        const auto row_of = []( int k, std::int32_t& rank, std::string& name, std::int32_t& score )
        {
            if ( k >= 0 && static_cast< std::size_t >( k ) < g_golden_frm_lb_rows_count )
            {
                rank = g_golden_frm_lb_rows[ k ].m_rank;
                name = g_golden_frm_lb_rows[ k ].m_name;
                score = g_golden_frm_lb_rows[ k ].m_score;
            }
        };
        in.m_leaderboard = [ & ]( const entity_t& who, ui_gfx_sort_t& into, const ui_font_draw_env_t& env ) -> bool
        {
            const golden_frm_leaf_t* g = leaf( who );
            if ( !g || g->m_kind != 5 || g->m_records != 2 )
                return false;
            const ui_leaderboard_t f = ui_leaderboard_of( who );
            ui_combo_block_t b = ui_combo_block_of( who );
            for ( int c = 0; c < 2; ++c )
            {
                std::uint8_t* params = c ? b.m_value_params : b.m_label_params;
                std::memset( params, 0, 60 );
                tail_of( who, 2 + c, params );
                const ui_slot_t* v = ui_property_value( who, c ? "ValueFont" : "HeadingFont" );
                std::string asset;
                ui_font_params( ui_font_db( 0 ), v ? v->m_text : std::string( ), params, asset );
            }
            ui_leaderboard_state_t s;
            ui_leaderboard_combos( &option_db, s.m_combos );
            std::vector< std::uint8_t > heads[ 2 ];
            for ( int c = 0; c < 2; ++c )
            {
                heads[ c ] = ui_unhex( g_golden_frm_records[ g->m_first_record + c ].m_head );
                s.m_combos[ c ].m_b41 = heads[ c ][ 1 ];
                s.m_combos[ c ].m_b42 = heads[ c ][ 2 ];
                std::memcpy( &s.m_combos[ c ].m_index, heads[ c ].data( ) + 4, 4 );
                std::memcpy( &s.m_combos[ c ].m_shown, heads[ c ].data( ) + 8, 4 );
            }
            s.m_focus = static_cast< std::int32_t >( g->m_words[ 0 ] );
            s.m_press = static_cast< std::int32_t >( g->m_words[ 1 ] );
            s.m_state = static_cast< std::int32_t >( g->m_words[ 2 ] );
            ui_leaderboard_draw_t d;
            const char* const names[ 5 ] = { "RankFont", "NameFont", "ScoreFont", "InfoFont", "ButtonFont" };
            for ( int c = 0; c < 5; ++c )
                font_params( who, names[ c ], 7 + c, d.m_fonts[ c ] );
            for ( int t = 0; t < 3; ++t )   // the three button images' holders: A / B / A
                d.m_buttons[ t ] = t % 2 ? &texture_b : &texture_a;
            d.m_combo.m_label_font = d.m_combo.m_value_font = 0x0F0E0D0Cu;
            d.m_combo.m_string = []( const std::string& id ) { return id + "~"; };
            d.m_combo.m_texture = [ g, &texture_a, &texture_b ]( int which ) -> const ui_texture_t*
            {
                if ( which < 0 || which >= 7 || g->m_textures[ which ] < 0 )
                    return nullptr;
                return g->m_textures[ which ] ? &texture_b : &texture_a;
            };
            d.m_string = []( const std::string& id ) { return id + "~"; };   // VuStringDB slot 6
            d.m_count = [ ] { return static_cast< int >( g_golden_frm_lb_rows_count ); };
            d.m_row = row_of;
            d.m_db = in.m_db;
            d.m_sort = &into;
            d.m_env = &env;
            const bool known = ui_leaderboard_draw( f, b, s, d );
            ++boards;
            return known && s.m_combos[ 0 ].m_focused == heads[ 0 ][ 0 ] && s.m_combos[ 1 ].m_focused == heads[ 1 ][ 0 ];
        };
        in.m_board = [ & ]( const entity_t& who, ui_gfx_sort_t& into, const ui_font_draw_env_t& env ) -> bool
        {
            const golden_frm_leaf_t* g = leaf( who );
            if ( !g || g->m_kind != 6 )
                return false;
            ui_mini_board_hooks_t hooks;
            hooks.m_count = [ ] { return static_cast< int >( g_golden_frm_lb_rows_count ); };
            hooks.m_row = [ &row_of ]( int k, std::string& name, std::int32_t& score )
            {
                std::int32_t rank = 0;
                row_of( k, rank, name, score );
            };
            c_ui_mini_board board( ui_mini_board_of( who ), hooks );
            board.m_fsm.m_current = static_cast< int >( g->m_words[ 0 ] );
            ui_mini_board_draw_t d;
            const char* const names[ 3 ] = { "NameFont", "ScoreFont", "InfoFont" };
            for ( int c = 0; c < 3; ++c )
                font_params( who, names[ c ], 12 + c, d.m_fonts[ c ] );
            d.m_crop = impl.m_crop;
            d.m_db = in.m_db;
            d.m_string = []( const std::string& id ) { return id + "~"; };   // VuStringDB slot 6
            d.m_sort = &into;
            d.m_env = &env;
            board.draw( d );
            ++boards;
            return true;
        };
        float alpha;
        std::memcpy( &alpha, &fe.m_alpha, 4 );
        sort.m_index = e.m_index;
        sort.m_key_lo = e.m_key_lo;
        sort.m_key_hi = fe.m_key_hi;
        std::vector< ui_gfx_command_t >& list = sort.m_commands[ e.m_index ];
        const std::size_t before = list.size( );
        const ui_frame_t frame = ui_screen_frame( *tree[ 0 ].second, impl, sort, alpha, 4, &in );
        live += run.m_leaves;
        bool same = state_same && frame.m_unknown == 0 && list.size( ) - before == static_cast< std::size_t >( run.m_commands ) &&
                    sort.m_key_lo == run.m_key[ 0 ] && sort.m_key_hi == run.m_key[ 1 ];
        for ( int c = 0; same && c < run.m_commands; ++c )
        {
            const golden_txt_command_t& want = g_golden_frm_commands[ run.m_first + c ];
            const ui_gfx_command_t& have = list[ before + static_cast< std::size_t >( c ) ];
            const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
            const auto& memory = sort.m_buffers[ e.m_index ].m_memory;
            same = std::memcmp( have.m_words, want.m_words, sizeof( have.m_words ) ) == 0 && have.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                   std::memcmp( memory.data( ) + have.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
            ++commands;
        }
        int s = 0;
        for ( const ui_frame_draw_t& d : frame.m_draws )
        {
            emitted += d.m_emitted ? 1 : 0;
            if ( d.m_emitted )   // the leaves these inputs do not cover (Info without m_info, ...) stay unrecreated
                continue;
            std::uint32_t bits;
            std::memcpy( &bits, &d.m_alpha, 4 );
            same = same && s < run.m_stubs && g_golden_frm_stubs[ run.m_first_stub + s ].m_index == index_of( d.m_entity ) &&
                   g_golden_frm_stubs[ run.m_first_stub + s ].m_alpha == bits;
            ++s;
        }
        same = same && s == run.m_stubs;
        stubs += s;
        ++runs;
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "ui screen frame records: %s env %d: %d commands (retail %d), %d other leaves (retail %d), unknown %d - or a word / record / "
                       "leaf / key differs", run.m_project, run.m_env, static_cast< int >( list.size( ) - before ), run.m_commands, s, run.m_stubs, frame.m_unknown );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_screen_frame's records (the export with inputs) match retail's screen draw 0x629D60 with the text / image / button / Info / "
                     "config / combo box / stat / bar draws live on %d runs over the Screens/* roots: %d leaf draws emitted (%d of them Info / config / "
                     "combo / stat / bar / leaderboard, their state checked; %d full / mini leaderboard draws 0x47A360 / 0x47DB40), %d commands and "
                     "records byte for byte, %d unrecreated leaves in order",
            runs, emitted, live, boards, commands, stubs );
    else
        LOG_ERROR( "the screen frame's records FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_info_text_draw (core/ui draw.hxx) against retail's VuInfoUITextEntity slot
// 14 0x446670 (oracle case info_draw) on every Info text in the loaded projects
// x 6 runs (text, ticker state, +0x108 written per run): the property-backed
// fields from OUR loaded entity (compared with retail's bytes; the params 0..40
// from OUR "Font" over FontDB), the commands and records byte for byte, ONE sort
// per buffer through all vectors.
inline bool verify_ui_info_draw( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    int failed = 0, commands = 0, fields = 0;
    for ( std::size_t k = 0; k < g_golden_info_vectors_count; ++k )
    {
        const golden_info_vector_t& v = g_golden_info_vectors[ k ];
        const golden_txt_env_t& e = g_golden_txt_envs[ v.m_env ];
        ui_crop_t crop;
        std::memcpy( &crop.m_matrix[ 0 ], &e.m_crop[ 0 ], 4 );
        std::memcpy( &crop.m_matrix[ 5 ], &e.m_crop[ 1 ], 4 );
        std::memcpy( &crop.m_matrix[ 12 ], &e.m_crop[ 2 ], 4 );
        std::memcpy( &crop.m_matrix[ 13 ], &e.m_crop[ 3 ], 4 );
        ui_font_draw_env_t env;
        std::memcpy( &env.m_matrix[ 0 ], &e.m_matrix[ 0 ], 4 );
        std::memcpy( &env.m_matrix[ 5 ], &e.m_matrix[ 1 ], 4 );
        std::memcpy( &env.m_matrix[ 12 ], &e.m_matrix[ 2 ], 4 );
        std::memcpy( &env.m_matrix[ 13 ], &e.m_matrix[ 3 ], 4 );
        std::memcpy( &env.m_top, &e.m_top, 4 );
        for ( int m = 0; m < 2; ++m )
        {
            env.m_material[ m ] = 0x5D001000u + 0x800u * static_cast< std::uint32_t >( m );   // TxtWorld's font materials
            env.m_material_key[ m ] = g_golden_txt_material_keys[ m ];
        }
        ui_string_db_t db;
        db.m_present = e.m_sdb != 0;
        db.m_slot12 = e.m_sdb_answer != 0;
        ui_text_font_t font;
        font.m_asset = true;
        font.m_asset_font = 0x0F0E0D0Cu;
        ui_text_base_t t = ui_text_base_of( ui_unhex( v.m_fields ), v.m_depth );
        const entity_t* have = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count && !have; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, v.m_project ) == 0 && v.m_index < static_cast< int >( loaded.trees[ p ].size( ) ) )
                have = loaded.trees[ p ][ static_cast< std::size_t >( v.m_index ) ].second;
        bool same = have && have->m_type == "VuInfoUITextEntity";
        const auto take = [ & ]( const char* name, void* target, int bytes )
        {
            const ui_slot_t* slot = have ? ui_property_value( *have, name ) : nullptr;
            if ( !slot )
            {
                same = false;
                return;
            }
            if ( std::memcmp( target, slot->m_bits, static_cast< std::size_t >( bytes ) ) != 0 )
                same = false;
            std::memcpy( target, slot->m_bits, static_cast< std::size_t >( bytes ) );
            ++fields;
        };
        if ( same )
        {
            take( "Screen Scale", t.m_screen_scale, 8 );
            take( "X", &t.m_rect[ 0 ], 4 );
            take( "Y", &t.m_rect[ 1 ], 4 );
            take( "Width", &t.m_rect[ 2 ], 4 );
            take( "Height", &t.m_rect[ 3 ], 4 );
            take( "Horizontal Alignment", &t.m_h_align, 4 );
            take( "Vertical Alignment", &t.m_v_align, 4 );
            take( "No Clip", &t.m_no_clip, 1 );
            take( "Single Line", &t.m_single_line, 1 );
            take( "Wordbreak", &t.m_wordbreak, 1 );
            take( "Horizontal Anchor", &t.m_anchor.m_horizontal, 4 );
            take( "Horizontal Ratio", &t.m_anchor.m_h_ratio, 4 );
            take( "Vertical Anchor", &t.m_anchor.m_vertical, 4 );
            take( "Vertical Ratio", &t.m_anchor.m_v_ratio, 4 );
            float depth = ui_frame_depth( *have );
            if ( std::memcmp( &depth, &t.m_depth, 4 ) != 0 )
                same = false;
            std::uint8_t params[ 60 ];
            std::memcpy( params, t.m_params, 60 );
            const ui_slot_t* name = ui_property_value( *have, "Font" );
            std::string asset;
            ui_font_params( ui_font_db( 0 ), name ? name->m_text : std::string( ), params, asset );
            if ( !name || std::memcmp( params, t.m_params, 41 ) != 0 )
                same = false;
        }
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "ui info draw: %s entity %d: our loaded fields differ from retail's", v.m_project, v.m_index );
        ui_info_scroll_t s;
        std::memcpy( &s, v.m_scroll, 16 );
        const std::vector< std::uint8_t > raw = ui_unhex( v.m_text );
        const std::string text( raw.begin( ), raw.end( ) );
        float alpha;
        std::memcpy( &alpha, &v.m_alpha, 4 );
        sort.m_index = e.m_index;
        sort.m_key_lo = e.m_key_lo;
        sort.m_key_hi = e.m_key_hi;
        std::vector< ui_gfx_command_t >& list = sort.m_commands[ e.m_index ];
        const std::size_t before = list.size( );
        const bool known = ui_info_text_draw( sort, env, crop, db, t, font, text, s, alpha );
        same = known && list.size( ) - before == static_cast< std::size_t >( v.m_commands );
        for ( int c = 0; same && c < v.m_commands; ++c )
        {
            const golden_txt_command_t& want = g_golden_info_commands[ v.m_first + c ];
            const ui_gfx_command_t& h = list[ before + static_cast< std::size_t >( c ) ];
            const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
            const auto& memory = sort.m_buffers[ e.m_index ].m_memory;
            same = std::memcmp( h.m_words, want.m_words, sizeof( h.m_words ) ) == 0 && h.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                   std::memcmp( memory.data( ) + h.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
            ++commands;
        }
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "ui info draw: vector %d (%s %d) gave %d commands, retail %d - or a word / record byte differs", static_cast< int >( k ), v.m_project,
                v.m_index, static_cast< int >( list.size( ) - before ), v.m_commands );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_info_text_draw matches retail's VuInfoUITextEntity draw slot 14 0x446670 (anchor 0x6282D0, flags 0x5546B0, font draw "
                     "0x536C10 once or twice) on %d vectors (%d of OUR loaded fields): %d commands and records byte for byte",
            static_cast< int >( g_golden_info_vectors_count ), fields, commands );
    else
        LOG_ERROR( "the UI info draw FAILED %d checks against retail's", failed );
    return failed == 0;
}

#include <workspace/utility/checks/leaves.hxx>

inline bool verify_ui_menu_all( )
{
    bool ok = true;
    ok = verify_ui_leaves_all( ) && ok;
    ok = verify_ui_info_draw( ) && ok;
    ok = verify_ui_screen_frame_records( ) && ok;
    ok = verify_ui_button_draw( ) && ok;
    ok = verify_ui_image_draw( ) && ok;
    ok = verify_ui_screen_frame( ) && ok;
    ok = verify_ui_screen_draw( ) && ok;
    ok = verify_ui_config_next( ) && ok;
    ok = verify_ui_config_nav( ) && ok;
    ok = verify_ui_config_glue( ) && ok;
    ok = verify_ui_boat_nav( ) && ok;
    return ok;
}
