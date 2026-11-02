#pragma once

#include <workspace/core/ui/award.hxx>
#include <workspace/core/ui/bind.hxx>
#include <workspace/core/ui/champ.hxx>
#include <workspace/core/ui/common.hxx>
#include <workspace/core/ui/controls.hxx>
#include <workspace/core/ui/fade.hxx>
#include <workspace/core/ui/glyphs.hxx>
#include <workspace/core/ui/message.hxx>
#include <workspace/core/ui/pages.hxx>
#include <workspace/core/ui/paused.hxx>
#include <workspace/core/ui/remap.hxx>
#include <workspace/core/ui/screens.hxx>
#include <workspace/core/ui/slider.hxx>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// The UI area's widget draw checks (batch 15; included by checks/leaves.hxx before
// verify_ui_leaves_all, which runs verify_ui_widgets_all). Everything ui.hxx /
// menu.hxx / leaves.hxx define above is usable.

// verify_ui_slider_draw: retail's VuSliderEntity draws 0x4700E0 (alpha) and
// 0x470010 (the layout bool) on every slider of the loaded Settings* / Controls*
// screens (oracle case slider_draw). The fields from OUR loaded entity
// (c_ui_binding::ui_bind_slider; each compared with retail's bytes first), the
// font params 0..40 from OUR load over FontDB (41..59 retail's - UNKNOWN), slot
// 12's value from OUR getter over the run's settings manager; the textures, the
// font answer and VuStringDB are the run's (inputs). Commands and records byte for
// byte, one sort per buffer through all runs, and the string ids in call order.
inline bool verify_ui_slider_draw( )
{
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    int failed = 0, runs = 0, commands = 0, strings = 0, fields = 0;
    for ( std::size_t k = 0; k < g_golden_sld_entities_count; ++k )
    {
        const golden_sld_entity_t& ge = g_golden_sld_entities[ k ];
        const entity_t* e = ui_lv_entity( ge.m_project, ge.m_index );
        const std::vector< std::uint8_t > raw = ui_unhex( ge.m_fields );
        bool same = e && e->m_ctor && e->m_ctor->m_vtable == ge.m_vtable && raw.size( ) == 0x150;
        ui_slider_t s;
        std::uint8_t params[ 60 ] = { };
        if ( same )
        {
            s = c_ui_binding::ui_bind_slider( *e, ge.m_vtable );
            const auto against = [ & ]( const void* ours, int offset, std::size_t bytes )
            {
                if ( std::memcmp( ours, raw.data( ) + ( offset - 0x60 ), bytes ) != 0 )
                    same = false;
                ++fields;
            };
            const std::uint8_t visible = s.m_visible ? 1 : 0;
            against( &visible, 0x64, 1 );
            against( s.m_scale, 0x68, 8 );
            against( s.m_frame, 0xA0, 16 );
            against( s.m_text, 0xB0, 16 );
            against( &s.m_h_align, 0x124, 4 );
            against( &s.m_v_align, 0x128, 4 );
            against( &s.m_no_clip, 0x12C, 1 );
            against( &s.m_single_line, 0x12D, 1 );
            against( &s.m_wordbreak, 0x12E, 1 );
            against( s.m_dst, 0x160, 16 );
            against( s.m_dst_delta, 0x170, 8 );
            against( s.m_src, 0x178, 16 );
            against( s.m_src_delta, 0x188, 8 );
            against( s.m_src_on_delta, 0x190, 8 );
            against( &s.m_count, 0x198, 4 );
            same = same && s.m_string_id == ge.m_string_id;
            s.m_depth = ui_frame_depth( *e );
            same = same && ui_lv_bits( s.m_depth ) == ge.m_depth;
            const std::vector< std::uint8_t > want = ui_unhex( ge.m_params );
            const ui_slot_t* name = ui_property_value( *e, "Font" );
            same = same && name && want.size( ) == 60;
            if ( same )
            {
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
                LOG_ERROR( "ui slider draw: %s entity %d: OUR loaded fields differ from retail's", ge.m_project, ge.m_index );
            continue;
        }
        ui_texture_t textures[ 2 ];
        for ( int t = 0; t < 2; ++t )
            textures[ t ] = { g_golden_sld_textures[ t ].m_handle, g_golden_sld_textures[ t ].m_width, g_golden_sld_textures[ t ].m_height };
        for ( int r = 0; r < ge.m_runs; ++r )
        {
            const golden_sld_run_t& run = g_golden_sld_runs[ ge.m_first + r ];
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
            env.m_texture_material = g_golden_sld_material[ 0 ];
            env.m_texture_material_key = g_golden_sld_material[ 1 ];
            env.m_rect_material = g_golden_sld_rect_material[ 0 ];
            env.m_rect_material_key = g_golden_sld_rect_material[ 1 ];
            ui_settings_t settings;
            std::memcpy( &settings.m_steering, &run.m_settings[ 0 ], 4 );
            std::memcpy( &settings.m_effect, &run.m_settings[ 1 ], 4 );
            std::memcpy( &settings.m_music, &run.m_settings[ 2 ], 4 );
            std::memcpy( &settings.m_display, &run.m_settings[ 3 ], 4 );
            ui_slider_hooks_t hooks;
            hooks.m_settings = run.m_has_settings ? &settings : nullptr;
            const c_ui_slider slider( 0u, s, hooks );
            ui_slider_draw_inputs_t in;
            const ui_texture_t* const* choice = nullptr;
            const ui_texture_t* by[ 4 ] = { &textures[ 0 ], &textures[ 1 ], nullptr, nullptr };   // 2: an asset without a texture, 3: none
            choice = by;
            in.m_frame = choice[ run.m_choice[ 0 ] ];
            in.m_selected = choice[ run.m_choice[ 1 ] ];
            in.m_bar = choice[ run.m_choice[ 2 ] ];
            ui_text_font_t font;
            font.m_asset = run.m_font != 0;
            font.m_asset_font = g_golden_sld_fonts[ 0 ];
            font.m_default = te.m_default != 0;
            font.m_default_font = g_golden_sld_fonts[ 1 ];
            in.m_font = font.m_asset ? font.m_asset_font : font.m_default ? font.m_default_font : 0u;   // 0x626B80
            std::memcpy( in.m_params, params, 60 );
            std::vector< std::string > asked;
            in.m_string = [ & ]( const std::string& id ) { asked.push_back( id ); return run.m_empty ? std::string( ) : id + "~"; };
            in.m_db.m_present = true;   // the case always serves VuStringDB
            in.m_db.m_slot12 = te.m_sdb_answer != 0;
            in.m_value = slider.value( );
            sort.m_index = te.m_index;
            sort.m_key_lo = te.m_key_lo;
            sort.m_key_hi = te.m_key_hi;
            std::vector< ui_gfx_command_t >& list = sort.m_commands[ te.m_index ];
            const std::size_t before = list.size( );
            float alpha;
            std::memcpy( &alpha, &run.m_arg, 4 );
            bool ok = run.m_fn ? ui_slider_layout_draw( sort, env, s, run.m_visible != 0, run.m_latched != 0, in, run.m_arg != 0 )
                               : ui_slider_draw( sort, env, s, run.m_visible != 0, run.m_latched != 0, in, alpha );
            ok = ok && list.size( ) - before == static_cast< std::size_t >( run.m_commands ) && static_cast< int >( asked.size( ) ) == run.m_strings;
            for ( int q = 0; ok && q < run.m_strings; ++q )
            {
                ok = asked[ static_cast< std::size_t >( q ) ] == g_golden_sld_strings[ run.m_first_string + q ];
                ++strings;
            }
            for ( int c = 0; ok && c < run.m_commands; ++c )
            {
                const golden_txt_command_t& want = g_golden_sld_commands[ run.m_first + c ];
                const ui_gfx_command_t& h = list[ before + static_cast< std::size_t >( c ) ];
                const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
                const auto& memory = sort.m_buffers[ te.m_index ].m_memory;
                ok = std::memcmp( h.m_words, want.m_words, sizeof( h.m_words ) ) == 0 && h.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                     std::memcmp( memory.data( ) + h.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
                ++commands;
            }
            ++runs;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui slider draw: %s entity %d run %d: %d commands (retail %d), %d string ids (retail %d) - or a word / record differs",
                    ge.m_project, ge.m_index, r, static_cast< int >( list.size( ) - before ), run.m_commands, static_cast< int >( asked.size( ) ),
                    run.m_strings );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_slider_draw / ui_slider_layout_draw match retail's VuSliderEntity draws 0x4700E0 / 0x470010 (the four subclasses' slot 12 "
                     "getters) on %d sliders x the runs (%d runs; %d of OUR loaded fields): %d commands byte for byte, %d string ids",
            static_cast< int >( g_golden_sld_entities_count ), runs, fields, commands, strings );
    else
        LOG_ERROR( "the UI slider draw FAILED %d checks against retail's", failed );
    return failed == 0;
}

// verify_ui_hud_controls: retail's HUD control draws - pause 0x4257A0 (slot 15
// 0x426330), recover 0x425F70 (0x425F50), the on-screen axis / button 0x42A0C0 -
// on every such entity of the loaded race HUD projects (oracle case
// hud_controls). The fields from OUR loaded properties (compared with retail's
// bytes first); the textures, the touch state, the boing scale and the two
// globals are the run's (inputs). Commands and records byte for byte, one sort
// per buffer through all runs.
inline bool ui_hud_controls_check( const golden_hc_entity_t* entities, std::size_t count, const golden_hc_run_t* all_runs, const golden_txt_command_t* all_commands, bool layout )
{
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    int failed = 0, runs = 0, commands = 0, fields = 0;
    for ( std::size_t k = 0; k < count; ++k )
    {
        const golden_hc_entity_t& ge = entities[ k ];
        const entity_t* e = ui_lv_entity( ge.m_project, ge.m_index );
        const std::vector< std::uint8_t > raw = ui_unhex( ge.m_fields );
        bool same = e && e->m_ctor && e->m_ctor->m_vtable == ge.m_vtable && raw.size( ) == 0x9C;
        ui_hud_control_t c;
        float touch[ 4 ] = { };
        if ( same )
        {
            const bool onscreen = ge.m_kind == 2;
            const int scale = onscreen ? 0x60 : 0x64, image = onscreen ? 0x68 : 0x6C, anchor = onscreen ? 0xB8 : 0xBC;
            const auto take = [ & ]( const char* name, void* out, std::size_t bytes, int offset )
            {
                same = ui_frame_take( *e, name, out, bytes ) && std::memcmp( out, raw.data( ) + ( offset - 0x5C ), bytes ) == 0 && same;
                ++fields;
            };
            take( "Screen Scale", c.m_scale, 8, scale );
            take( "Image X", &c.m_image[ 0 ], 4, image );
            take( "Image Y", &c.m_image[ 1 ], 4, image + 4 );
            take( "Image Width", &c.m_image[ 2 ], 4, image + 8 );
            take( "Image Height", &c.m_image[ 3 ], 4, image + 12 );
            take( "Horizontal Anchor", &c.m_anchor.m_horizontal, 4, anchor );
            take( "Vertical Anchor", &c.m_anchor.m_vertical, 4, anchor + 4 );
            take( "Horizontal Ratio", &c.m_anchor.m_h_ratio, 4, anchor + 8 );
            take( "Vertical Ratio", &c.m_anchor.m_v_ratio, 4, anchor + 12 );
            if ( onscreen )
            {
                const char* const names[ 4 ] = { "Enable for Tilt", "Enable for Touch", "Enable for GamePad", "Enable for Keyboard" };
                for ( int f = 0; f < 4; ++f )
                    take( names[ f ], &c.m_enable[ f ], 1, 0xC8 + f );
            }
            if ( layout )
            {
                const int at = onscreen ? 0x78 : 0x7C;
                take( "Touch X", &touch[ 0 ], 4, at );
                take( "Touch Y", &touch[ 1 ], 4, at + 4 );
                take( "Touch Width", &touch[ 2 ], 4, at + 8 );
                take( "Touch Height", &touch[ 3 ], 4, at + 12 );
            }
            c.m_depth = ui_frame_depth( *e );
            same = same && ui_lv_bits( c.m_depth ) == ge.m_depth;
        }
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui hud controls: %s entity %d: OUR loaded fields differ from retail's", ge.m_project, ge.m_index );
            continue;
        }
        ui_texture_t textures[ 2 ];
        for ( int t = 0; t < 2; ++t )
            textures[ t ] = { g_golden_hc_textures[ t ].m_handle, g_golden_hc_textures[ t ].m_width, g_golden_hc_textures[ t ].m_height };
        const ui_texture_t* by[ 4 ] = { &textures[ 0 ], &textures[ 1 ], nullptr, nullptr };   // 2: an asset without a texture, 3: none
        for ( int r = 0; r < ge.m_runs; ++r )
        {
            const golden_hc_run_t& run = all_runs[ ge.m_first + r ];
            const golden_txt_env_t& te = g_golden_txt_envs[ run.m_env ];
            ui_font_draw_env_t env;
            const std::vector< std::uint8_t > matrix = ui_unhex( run.m_matrix );
            std::memcpy( env.m_matrix, matrix.data( ), 64 );
            env.m_texture_material = g_golden_hc_material[ 0 ];
            env.m_texture_material_key = g_golden_hc_material[ 1 ];
            if ( layout )
            {
                env.m_rect_material = g_golden_hld_rect_material[ 0 ];
                env.m_rect_material_key = g_golden_hld_rect_material[ 1 ];
            }
            ui_crop_t crop;
            std::memcpy( &crop.m_matrix[ 0 ], &te.m_crop[ 0 ], 4 );
            std::memcpy( &crop.m_matrix[ 5 ], &te.m_crop[ 1 ], 4 );
            std::memcpy( &crop.m_matrix[ 12 ], &te.m_crop[ 2 ], 4 );
            std::memcpy( &crop.m_matrix[ 13 ], &te.m_crop[ 3 ], 4 );
            ui_hud_control_inputs_t in;
            in.m_texture = by[ run.m_choice[ 0 ] ];
            in.m_pressed_texture = by[ run.m_choice[ 1 ] ];
            in.m_state = run.m_state;
            std::memcpy( &in.m_boing, &run.m_boing, 4 );
            in.m_race_present = run.m_race >= 0;
            in.m_race_28 = static_cast< std::uint32_t >( run.m_race );
            in.m_method_present = run.m_method >= 0;
            in.m_method = run.m_method;
            float alpha;
            std::memcpy( &alpha, &run.m_alpha, 4 );
            sort.m_index = te.m_index;
            sort.m_key_lo = te.m_key_lo;
            sort.m_key_hi = te.m_key_hi;
            std::vector< ui_gfx_command_t >& list = sort.m_commands[ te.m_index ];
            const std::size_t before = list.size( );
            if ( layout )
                ui_hud_layout_draw( sort, env, crop, c, touch, in, ge.m_kind, static_cast< std::uint8_t >( run.m_alpha ) );
            else if ( ge.m_kind == 2 )
                ui_hud_onscreen_draw( sort, env, crop, c, in, alpha );
            else
                ui_hud_button_draw( sort, env, crop, c, in, ge.m_kind == 1, alpha );
            bool ok = list.size( ) - before == static_cast< std::size_t >( run.m_commands );
            for ( int q = 0; ok && q < run.m_commands; ++q )
            {
                const golden_txt_command_t& want = all_commands[ run.m_first + q ];
                const ui_gfx_command_t& h = list[ before + static_cast< std::size_t >( q ) ];
                const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
                const auto& memory = sort.m_buffers[ te.m_index ].m_memory;
                ok = std::memcmp( h.m_words, want.m_words, sizeof( h.m_words ) ) == 0 && h.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                     std::memcmp( memory.data( ) + h.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
                ++commands;
            }
            ++runs;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui hud controls: %s entity %d run %d: %d commands (retail %d) - or a word / record differs", ge.m_project, ge.m_index, r,
                    static_cast< int >( list.size( ) - before ), run.m_commands );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_hud_button_draw / ui_hud_onscreen_draw match retail's HUD control draws (pause 0x4257A0 / 0x426330, recover 0x425F70 / "
                     "0x425F50, on-screen 0x42A0C0) on %d entities x the runs (%d runs; %d of OUR loaded fields): %d commands byte for byte",
            static_cast< int >( count ), runs, fields, commands );
    else
        LOG_ERROR( "the UI HUD control draws FAILED %d checks against retail's", failed );
    return failed == 0;
}

inline bool verify_ui_hud_controls( ) { return ui_hud_controls_check( g_golden_hc_entities, g_golden_hc_entities_count, g_golden_hc_runs, g_golden_hc_commands, false ); }

// verify_ui_hud_layout_draw: the same entities through the layout draws 0x4258E0 / 0x42A240 (oracle case hud_layout_draw).
inline bool verify_ui_hud_layout_draw( ) { return ui_hud_controls_check( g_golden_hld_entities, g_golden_hld_entities_count, g_golden_hld_runs, g_golden_hld_commands, true ); }

// verify_ui_hud_toast: retail's VuHUDHiddenPackageToastEntity OnPackageFound
// 0x424600 / OnUITick 0x4248E0 through the scripted sequence on every toast of the
// loaded HUD projects (oracle case hud_toast). The toast starts from OUR loaded
// Total / Fade Time (compared with retail's state at load); VuStringDB slot 6 is
// the step's answer (a HOOK). After each step: Visible, alpha, the three times and
// the text, plus the keys slot 6 was asked.
inline bool verify_ui_hud_toast( )
{
    int failed = 0, steps = 0;
    const auto state_text = []( const ui_hud_toast_t& t )
    {
        const float f[ 4 ] = { t.m_alpha, t.m_total, t.m_fade, t.m_left };
        char text[ 64 ];
        std::snprintf( text, sizeof( text ), "%02x", t.m_visible ? 1 : 0 );
        std::string out = text;
        for ( float x : f )
        {
            std::snprintf( text, sizeof( text ), ".0x%08X", ui_lv_bits( x ) );
            out += text;
        }
        return out + "." + t.m_text;
    };
    for ( std::size_t k = 0; k < g_golden_ts_toasts_count; ++k )
    {
        const golden_ts_toast_t& gt = g_golden_ts_toasts[ k ];
        const entity_t* e = ui_lv_entity( gt.m_project, gt.m_index );
        ui_hud_toast_t t;
        bool same = e && e->m_type == "VuHUDHiddenPackageToastEntity";
        if ( same )
        {
            same = ui_frame_take( *e, "Total Time", &t.m_total, 4 ) && ui_frame_take( *e, "Fade Time", &t.m_fade, 4 );
            std::uint8_t visible = 1;
            if ( ui_frame_take( *e, "Visible", &visible, 1 ) )
                t.m_visible = visible != 0;
            same = same && state_text( t ) == gt.m_initial;
        }
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui hud toast: %s entity %d: OUR loaded state differs from retail's", gt.m_project, gt.m_index );
            continue;
        }
        for ( int s = 0; s < gt.m_steps; ++s )
        {
            const golden_ts_step_t& st = g_golden_ts_steps[ gt.m_first + s ];
            const std::vector< std::uint8_t > raw = ui_unhex( st.m_params );
            ui_params_t p;
            std::memcpy( p.m_bytes, raw.data( ), raw.size( ) < 128 ? raw.size( ) : 128 );
            p.m_size = static_cast< int >( raw.size( ) );
            std::string keys;
            if ( st.m_op == 0 )
                ui_hud_toast_tick( t, p );
            else if ( st.m_op == 1 )
                ui_hud_toast_found( t, p, [ & ]( const char* key )
                {
                    keys += keys.empty( ) ? key : std::string( "," ) + key;
                    return std::string( st.m_template );
                } );
            else
            {
                std::memcpy( &t.m_total, raw.data( ), 4 );
                std::memcpy( &t.m_fade, raw.data( ) + 4, 4 );
            }
            ++steps;
            if ( ( state_text( t ) != st.m_state || keys != st.m_keys ) && ++failed <= 8 )
                LOG_ERROR( "ui hud toast: %s entity %d step %d: %s (retail %s)", gt.m_project, gt.m_index, s, state_text( t ).c_str( ), st.m_state );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_hud_toast_found / ui_hud_toast_tick match retail's VuHUDHiddenPackageToastEntity OnPackageFound 0x424600 / OnUITick "
                     "0x4248E0 on %d toasts from OUR loaded Total / Fade Time: %d steps (visible, alpha, times, text, the string-DB keys)",
            static_cast< int >( g_golden_ts_toasts_count ), steps );
    else
        LOG_ERROR( "the UI HUD toast FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_message_box_draw / ui_message_box_layout_draw (core/ui message.hxx) against
// retail's VuMessageBoxScreenEntity draw 0x4675A0 and layout draw 0x467710 ->
// 0x467990 (oracle case message_box_draw) on Screens/MessageBox's box: runtime
// state per run (enabled blocks, texts, fonts, +0x588, textures, spinner angle,
// image flip / fractions), then a sweep with the fields written raw. Each new
// command's words and record byte for byte in ONE sort through all runs, the
// child-draw calls (child, component, alpha, both stack tops and sizes, key
// words), the key after. The real runs take the property fields from OUR loaded
// box (compared with retail's bytes first) and font params 0..40 from OUR five
// "* Font" names over the real FontDB (41..59: retail's indeterminate stack).
inline bool verify_ui_message_box_draw( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    static const char* const k_blocks[ 5 ] = { "Heading", "Body", "BodyImage", "ButtonA", "ButtonB" };
    int failed = 0, commands = 0, calls = 0, fields = 0, fonts = 0, real = 0;
    for ( std::size_t k = 0; k < g_golden_mbx_runs_count; ++k )
    {
        const golden_mbx_run_t& v = g_golden_mbx_runs[ k ];
        const std::vector< std::uint8_t > raw = ui_unhex( v.m_fields );
        const std::vector< std::uint8_t > block = ui_unhex( v.m_block );
        const auto at = [ &raw ]( int offset ) { return raw.data( ) + ( offset - 0x60 ); };
        ui_message_box_t b;
        std::memcpy( b.m_screen_scale, at( 0x60 ), 8 );
        std::memcpy( b.m_image, at( 0x68 ), 16 );
        for ( int j = 0; j < 5; ++j )
        {
            ui_message_block_t& t = b.m_blocks[ j ];
            const int o = 0x78 + 0x9C * j;
            std::memcpy( t.m_rect, at( o ), 16 );
            std::memcpy( t.m_params, at( o + 0x30 ), 60 );
            std::memcpy( &t.m_h_align, at( o + 0x74 ), 4 );
            std::memcpy( &t.m_v_align, at( o + 0x78 ), 4 );
            t.m_no_clip = *at( o + 0x7C );
            t.m_single_line = *at( o + 0x7D );
            t.m_wordbreak = *at( o + 0x7E );
            t.m_enabled = static_cast< std::uint8_t >( ( v.m_mask >> j ) & 1 );
            const std::vector< std::uint8_t > text = ui_unhex( v.m_texts[ j ] );
            t.m_text.assign( text.begin( ), text.end( ) );
            t.m_font.m_asset = v.m_fonts[ j ] == 0;
            t.m_font.m_asset_font = g_golden_mbx_fonts[ 0 ];
            std::uint32_t def;
            std::memcpy( &def, block.data( ) + 0x9C, 4 );
            t.m_font.m_default = def != 0;
            t.m_font.m_default_font = g_golden_mbx_fonts[ 1 ];
        }
        std::memcpy( b.m_button_rect[ 0 ], at( 0x384 ), 16 );
        std::memcpy( b.m_button_rect[ 1 ], at( 0x3E0 ), 16 );
        b.m_button_flip[ 0 ] = *at( 0x3DC );
        b.m_button_flip[ 1 ] = *at( 0x438 );
        std::memcpy( &b.m_selected_colour, at( 0x439 ), 4 );
        std::memcpy( b.m_spinner_rect, at( 0x440 ), 16 );
        std::memcpy( &b.m_spinner_colour, at( 0x468 ), 4 );
        std::memcpy( b.m_touch[ 0 ], at( 0x470 ), 16 );
        std::memcpy( b.m_touch[ 1 ], at( 0x480 ), 16 );
        b.m_image_flip = *at( 0x528 );
        std::memcpy( b.m_progress, at( 0x52C ), 8 );
        std::memcpy( &b.m_rotation, at( 0x578 ), 4 );
        std::memcpy( &b.m_button, at( 0x588 ), 4 );
        std::memcpy( &b.m_depth, &v.m_depth, 4 );
        const entity_t* have = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count && !have; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, v.m_project ) == 0 && v.m_index < static_cast< int >( loaded.trees[ p ].size( ) ) )
                have = loaded.trees[ p ][ static_cast< std::size_t >( v.m_index ) ].second;
        if ( !have )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui message box draw: run %d: %s entity %d is not loaded", static_cast< int >( k ), v.m_project, v.m_index );
            continue;
        }
        if ( v.m_sweep == 0 )
        {
            const ui_state_t* st = ui_is_state( *have ) ? static_cast< const ui_state_t* >( have->m_state.get( ) ) : nullptr;
            const ui_state_t* draw = nullptr;
            for ( const entity_component_t& c : have->m_components )
                if ( c.m_vtable == k_ui_draw_component_vtable && c.m_state )
                    draw = static_cast< const ui_state_t* >( c.m_state.get( ) );
            bool same = st && draw && have->m_ctor && have->m_ctor->m_vtable == 0x73D8E4u;
            const auto take = [ & ]( const std::string& name, void* target, int bytes, const ui_state_t* from )
            {
                const ui_slot_t* slot = from ? ui_slot_named( *from, name.c_str( ) ) : nullptr;
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
            static const char* const k_xywh[ 4 ] = { " X", " Y", " Width", " Height" };
            if ( same )
            {
                take( "Screen Scale", b.m_screen_scale, 8, st );
                for ( int j = 0; j < 4; ++j )
                {
                    take( std::string( "Image" ) + k_xywh[ j ], &b.m_image[ j ], 4, st );
                    take( std::string( "ButtonA Texture" ) + k_xywh[ j ], &b.m_button_rect[ 0 ][ j ], 4, st );
                    take( std::string( "ButtonB Texture" ) + k_xywh[ j ], &b.m_button_rect[ 1 ][ j ], 4, st );
                    take( std::string( "Animated Texture" ) + k_xywh[ j ], &b.m_spinner_rect[ j ], 4, st );
                    take( std::string( "ButtonA Touch" ) + k_xywh[ j ], &b.m_touch[ 0 ][ j ], 4, st );
                    take( std::string( "ButtonB Touch" ) + k_xywh[ j ], &b.m_touch[ 1 ][ j ], 4, st );
                }
                for ( int j = 0; j < 5; ++j )
                {
                    ui_message_block_t& t = b.m_blocks[ j ];
                    const std::string p = k_blocks[ j ];
                    for ( int c = 0; c < 4; ++c )
                        take( p + k_xywh[ c ], &t.m_rect[ c ], 4, st );
                    take( p + " Horizontal Alignment", &t.m_h_align, 4, st );
                    take( p + " Vertical Alignment", &t.m_v_align, 4, st );
                    take( p + " No Clip", &t.m_no_clip, 1, st );
                    take( p + " Single Line", &t.m_single_line, 1, st );
                    take( p + " Wordbreak", &t.m_wordbreak, 1, st );
                    const ui_slot_t* font_name = ui_slot_named( *st, ( p + " Font" ).c_str( ) );
                    if ( !font_name )
                    {
                        same = false;
                        continue;
                    }
                    std::uint8_t params[ 60 ];
                    std::memcpy( params, t.m_params, 60 );
                    std::string asset;
                    ui_font_params( ui_font_db( 0 ), font_name->m_text, params, asset );
                    if ( std::memcmp( params, t.m_params, 41 ) != 0 )
                        same = false;
                    std::memcpy( t.m_params, params, 60 );
                    ++fonts;
                }
                take( "ButtonA Texture FlipX", &b.m_button_flip[ 0 ], 1, st );
                take( "ButtonB Texture FlipX", &b.m_button_flip[ 1 ], 1, st );
                take( "Selected Text Color", &b.m_selected_colour, 4, st );
                take( "Animated Texture Color", &b.m_spinner_colour, 4, st );
                take( "Depth", &b.m_depth, 4, draw );
            }
            if ( !same && ++failed <= 8 )
                LOG_ERROR( "ui message box draw: run %d: our loaded fields differ from retail's", static_cast< int >( k ) );
            ++real;
        }
        ui_message_box_inputs_t in;
        for ( int j = 0; j < 6; ++j )
            in.m_buttons[ j ] = ( v.m_textures >> j ) & 1u ? g_golden_mbx_textures + 0x20u * static_cast< unsigned >( j ) : 0u;
        in.m_spinner = ( v.m_textures >> 6 ) & 1u ? g_golden_mbx_textures + 0xC0u : 0u;
        in.m_image = ( v.m_textures >> 7 ) & 1u ? g_golden_mbx_textures + 0xE0u : 0u;
        ui_impl_t impl;
        std::memcpy( impl.m_crop.m_matrix, block.data( ) + 0x14, 64 );
        std::memcpy( &impl.m_float, block.data( ) + 0x94, 4 );
        std::memcpy( &impl.m_aspect, block.data( ) + 0x98, 4 );
        ui_string_db_t db;
        db.m_present = v.m_sdb >= 0;
        db.m_slot12 = v.m_sdb == 1;
        ui_font_draw_env_t env;
        const std::vector< std::uint8_t > matrix = ui_unhex( v.m_matrix );
        std::memcpy( env.m_matrix, matrix.data( ), 64 );
        std::memcpy( &env.m_top, &v.m_top, 4 );
        for ( int m = 0; m < 2; ++m )
        {
            env.m_material[ m ] = g_golden_mbx_font_materials[ m ];
            env.m_material_key[ m ] = g_golden_mbx_font_materials[ 2 + m ];
        }
        env.m_texture_material = g_golden_mbx_materials[ 0 ];
        env.m_texture_material_key = g_golden_mbx_materials[ 1 ];
        env.m_rect_material = g_golden_mbx_materials[ 2 ];
        env.m_rect_material_key = g_golden_mbx_materials[ 3 ];
        sort.m_index = v.m_buffer;
        sort.m_key_lo = v.m_key_in[ 0 ];
        sort.m_key_hi = v.m_key_in[ 1 ];
        std::vector< ui_gfx_command_t >& list = sort.m_commands[ v.m_buffer ];
        const std::size_t before = list.size( );
        ui_gfx_stacks_t stacks;
        stacks.m_matrices.resize( static_cast< std::size_t >( v.m_stacks[ 0 ] ) );
        stacks.m_floats.resize( static_cast< std::size_t >( v.m_stacks[ 1 ] ) );
        struct seen_t
        {
            int m_child, m_component;
            float m_alpha;
            std::array< float, 16 > m_matrix;
            int m_matrices, m_floats;
            float m_float;
            std::uint32_t m_key[ 2 ];
        };
        std::vector< seen_t > seen;
        float alpha;
        std::memcpy( &alpha, &v.m_alpha, 4 );
        bool known;
        if ( v.m_layout )
            known = ui_message_box_layout_draw( sort, env, db, impl.m_aspect, b, in, static_cast< std::uint8_t >( v.m_arg ) );
        else
            known = ui_message_box_draw( sort, stacks, impl, env, db, *have, b, in, alpha, [ & ]( const entity_t& child, int component, float a )
            {
                int index = -1;
                for ( std::size_t c = 0; c < have->m_children.size( ); ++c )
                    if ( have->m_children[ c ].get( ) == &child )
                        index = static_cast< int >( c );
                seen.push_back( { index, component, a, stacks.m_matrices.back( ), static_cast< int >( stacks.m_matrices.size( ) ),
                                  static_cast< int >( stacks.m_floats.size( ) ), stacks.m_floats.back( ), { sort.m_key_lo, sort.m_key_hi } } );
            } );
        bool same = known && list.size( ) - before == static_cast< std::size_t >( v.m_commands ) && seen.size( ) == static_cast< std::size_t >( v.m_calls ) &&
                    sort.m_key_lo == v.m_key[ 0 ] && sort.m_key_hi == v.m_key[ 1 ] && static_cast< int >( stacks.m_matrices.size( ) ) == v.m_stacks[ 2 ] &&
                    static_cast< int >( stacks.m_floats.size( ) ) == v.m_stacks[ 3 ];
        for ( int c = 0; same && c < v.m_commands; ++c )
        {
            const golden_txt_command_t& want = g_golden_mbx_commands[ v.m_first + c ];
            const ui_gfx_command_t& got = list[ before + static_cast< std::size_t >( c ) ];
            const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
            const auto& memory = sort.m_buffers[ v.m_buffer ].m_memory;
            same = std::memcmp( got.m_words, want.m_words, sizeof( got.m_words ) ) == 0 && got.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                   std::memcmp( memory.data( ) + got.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
            ++commands;
        }
        for ( int c = 0; same && c < v.m_calls; ++c )
        {
            const golden_mbx_call_t& want = g_golden_mbx_calls[ v.m_first_call + c ];
            const seen_t& got = seen[ static_cast< std::size_t >( c ) ];
            const std::vector< std::uint8_t > m = ui_unhex( want.m_matrix );
            same = got.m_child == want.m_child && got.m_component == want.m_component && std::memcmp( &got.m_alpha, &want.m_alpha, 4 ) == 0 &&
                   m.size( ) == 64 && std::memcmp( got.m_matrix.data( ), m.data( ), 64 ) == 0 && got.m_matrices == want.m_matrices &&
                   got.m_floats == want.m_floats && std::memcmp( &got.m_float, &want.m_float, 4 ) == 0 && got.m_key[ 0 ] == want.m_key[ 0 ] &&
                   got.m_key[ 1 ] == want.m_key[ 1 ];
            ++calls;
        }
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "ui message box draw: run %d (sweep %d, %s) gave %d commands / %d child calls, retail %d / %d - or a word, record byte, call "
                       "or key differs", static_cast< int >( k ), v.m_sweep, v.m_layout ? "layout" : "draw", static_cast< int >( list.size( ) - before ),
                static_cast< int >( seen.size( ) ), v.m_commands, v.m_calls );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_message_box_draw / _layout_draw match retail's VuMessageBoxScreenEntity draw 0x4675A0 and layout draw 0x467710 -> 0x467990 "
                     "(0x4681A0, font draw 0x536C10, texture draw 0x507360, rect draw 0x507A00) on %d runs (%d real, %d loaded fields, %d fonts): "
                     "%d commands and records byte for byte, %d child calls",
            static_cast< int >( g_golden_mbx_runs_count ), real, fields, fonts, commands, calls );
    else
        LOG_ERROR( "the UI message box draw FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_game_config_release (core/ui expand.hxx) over the touch device's removal
// (controls.hxx ui_touch_remove) against retail's VuGameConfigEntity slot 7
// 0x45D080 with 0x53FA60 live (oracle case game_config_release) on the five
// GameConfig* screens' selectors: the device's entries after, +0x1C, the draw
// object's +0x1E and the 3D draw manager removes.
inline bool verify_ui_game_config_release( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    int failed = 0, runs = 0;
    for ( std::size_t k = 0; k < g_golden_gcr_entities_count; ++k )
    {
        const golden_gcr_entity_t& want = g_golden_gcr_entities[ k ];
        const entity_t* have = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, want.m_project ) == 0 && want.m_index >= 0 &&
                 static_cast< std::size_t >( want.m_index ) < loaded.trees[ p ].size( ) )
                have = loaded.trees[ p ][ static_cast< std::size_t >( want.m_index ) ].second;
        if ( !have || have->m_type != "VuGameConfigEntity" || have->m_name != want.m_name )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui game config release: %s entity %d is not the authored selector", want.m_project, want.m_index );
            continue;
        }
        for ( int r = 0; r < want.m_runs; ++r )
        {
            const golden_gcr_run_t& run = g_golden_gcr_runs[ want.m_first + r ];
            ui_config_nav_t self;
            int others[ 2 ] = { };
            const std::map< std::string, const void* > tags = { { "self", &self }, { "o1", &others[ 0 ] }, { "o2", &others[ 1 ] } };
            ui_touch_device_t device;
            for ( const char* at = run.m_before; *at; )
            {
                const char* comma = std::strchr( at, ',' );
                const char* semi = comma ? std::strchr( comma, ';' ) : nullptr;
                if ( !semi )
                    break;
                device.m_callbacks.push_back( { tags.at( std::string( at, comma ) ), static_cast< std::uint32_t >( std::strtoul( comma + 1, nullptr, 10 ) ) } );
                at = semi + 1;
            }
            device.m_priority = 7u;
            ui_game_config_t2 g;
            g.m_drawn = run.m_drawn != 0;
            int draws = 0;
            ui_config_hooks_t h;
            h.touch_remove = [ & ] { ui_touch_remove( device, &self ); };
            h.draw_remove = [ & ] { ++draws; };
            ui_game_config_release( g, h );
            std::string after;
            for ( const auto& c : device.m_callbacks )
                for ( const auto& t : tags )
                    if ( t.second == c.first )
                        after += t.first + "," + std::to_string( c.second ) + ";";
            ++runs;
            if ( ( after != run.m_after || device.m_priority != run.m_priority || ( g.m_drawn ? 1 : 0 ) != run.m_byte || draws != run.m_draws ) && ++failed <= 8 )
                LOG_ERROR( "ui game config release: %s run %d: entries %s +0x1C %u +0x1E %d draws %d (retail %s %u %d %d)", want.m_project, r, after.c_str( ),
                           device.m_priority, g.m_drawn ? 1 : 0, draws, run.m_after, run.m_priority, run.m_byte, run.m_draws );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_game_config_release matches retail's VuGameConfigEntity onGameRelease 0x45D080 (touch removal 0x53FA60, 3D draw manager "
                     "slot +0x2C) on %d selectors: %d runs (the device's entries and +0x1C, +0x1E, the draw removes)",
            static_cast< int >( g_golden_gcr_entities_count ), runs );
    else
        LOG_ERROR( "the game config release FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_text_layout_draw (core/ui draw.hxx) against retail's VuUITextBaseEntity
// layout draw slot 13 0x627B30 (oracle case text_layout_draw) on the text-base
// entities of four projects under TxtWorld's environments: each new command's
// words and record (the rect and the text) byte for byte, one sort per buffer
// through all vectors. The fields are retail's bytes (verify_ui_text_draw
// compares OUR loaded ones with them for the same entities).
inline bool verify_ui_text_layout_draw( )
{
    constexpr std::uint32_t k_material[ 2 ] = { 0x5D001000u, 0x5D001800u };   // TxtWorld's font materials
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    int failed = 0, commands = 0, rects = 0;
    for ( std::size_t k = 0; k < g_golden_tld_vectors_count; ++k )
    {
        const golden_tld_vector_t& v = g_golden_tld_vectors[ k ];
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
            env.m_material[ m ] = k_material[ m ];
            env.m_material_key[ m ] = g_golden_txt_material_keys[ m ];
        }
        env.m_rect_material = g_golden_tld_rect_material[ 0 ];
        env.m_rect_material_key = g_golden_tld_rect_material[ 1 ];
        ui_string_db_t db;
        db.m_present = e.m_sdb != 0;
        db.m_slot12 = e.m_sdb_answer != 0;
        ui_text_font_t font;
        font.m_asset = true;
        font.m_asset_font = 0x0F0E0D0Cu;
        font.m_default = e.m_default != 0;
        font.m_default_font = 0x0B0A0908u;
        const std::vector< std::uint8_t > raw = ui_unhex( v.m_text );
        const std::string text( raw.begin( ), raw.end( ) );
        const ui_text_base_t t = ui_text_base_of( ui_unhex( v.m_fields ), v.m_depth );
        sort.m_index = e.m_index;
        sort.m_key_lo = e.m_key_lo;
        sort.m_key_hi = e.m_key_hi;
        std::vector< ui_gfx_command_t >& list = sort.m_commands[ e.m_index ];
        const std::size_t before = list.size( );
        const bool known = ui_text_layout_draw( sort, env, crop, db, t, font, text, static_cast< std::uint8_t >( v.m_layout ) );
        bool same = known && list.size( ) - before == static_cast< std::size_t >( v.m_commands );
        for ( int c = 0; same && c < v.m_commands; ++c )
        {
            const golden_txt_command_t& want = g_golden_tld_commands[ v.m_first + c ];
            const ui_gfx_command_t& got = list[ before + static_cast< std::size_t >( c ) ];
            const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
            const auto& memory = sort.m_buffers[ e.m_index ].m_memory;
            same = std::memcmp( got.m_words, want.m_words, sizeof( got.m_words ) ) == 0 && got.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                   std::memcmp( memory.data( ) + got.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
            rects += got.m_words[ 2 ] == k_ui_rect_callback ? 1 : 0;
            ++commands;
        }
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "ui text layout draw: vector %d (%s %d, byte %d) gave %d commands, retail %d - or a word / record byte differs", static_cast< int >( k ),
                       v.m_project, v.m_index, v.m_layout, static_cast< int >( list.size( ) - before ), v.m_commands );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_text_layout_draw matches retail's VuUITextBaseEntity layout draw slot 13 0x627B30 (rect draw 0x507A00, slot 14 0x627C60) "
                     "on %d vectors: %d commands (%d rects) and records byte for byte",
            static_cast< int >( g_golden_tld_vectors_count ), commands, rects );
    else
        LOG_ERROR( "the UI text layout draw FAILED %d checks against retail's", failed );
    return failed == 0;
}

// The page layout's fields from retail's bytes +0x60..+0xF8 (the case's "fields").
inline ui_page_layout_t ui_pl_of( const std::vector< std::uint8_t >& raw, std::uint32_t depth )
{
    ui_page_layout_t p;
    const auto at = [ &raw ]( int offset ) { return raw.data( ) + ( offset - 0x60 ); };
    if ( raw.size( ) < 0x98 )
        return p;
    p.m_visible = *at( 0x68 );
    std::memcpy( p.m_screen_scale, at( 0x6C ), 8 );
    std::memcpy( p.m_rect, at( 0x74 ), 16 );
    std::memcpy( &p.m_anchor.m_horizontal, at( 0x84 ), 4 );
    std::memcpy( &p.m_anchor.m_vertical, at( 0x88 ), 4 );
    std::memcpy( &p.m_anchor.m_h_ratio, at( 0x8C ), 4 );
    std::memcpy( &p.m_anchor.m_v_ratio, at( 0x90 ), 4 );
    p.m_scroll_at_start = *at( 0xAC );
    std::memcpy( &p.m_scroll_speed, at( 0xB0 ), 4 );
    std::memcpy( &p.m_scroll_lag, at( 0xB4 ), 4 );
    std::memcpy( &p.m_damping, at( 0xB8 ), 4 );
    p.m_touchable = *at( 0xBC );
    p.m_scrolling = *at( 0xBD );
    std::memcpy( &p.m_total, at( 0xD0 ), 4 );
    std::memcpy( &p.m_scroll, at( 0xE4 ), 4 );
    std::memcpy( &p.m_depth, &depth, 4 );
    return p;
}

// One element against retail's dump (params bytes 0..40 only: 41..59 UNKNOWN).
inline bool ui_pl_same( const ui_page_element_t& el, const golden_pl_element_t& g )
{
    const int kind = el.m_kind == ui_page_kind_t::text ? 0 : el.m_kind == ui_page_kind_t::image ? 1 : 2;
    if ( kind != g.m_kind )
        return false;
    const auto text = []( const char* hexed ) { const std::vector< std::uint8_t > r = ui_unhex( hexed ); return std::string( r.begin( ), r.end( ) ); };
    if ( kind == 0 )
    {
        const std::vector< std::uint8_t > params = ui_unhex( g.m_params );
        return el.m_font == text( g.m_font ) && params.size( ) == 60 && std::memcmp( el.m_params, params.data( ), 41 ) == 0 &&
               ( el.m_has_asset ? 1u : 0u ) == ( g.m_asset ? 1u : 0u ) && el.m_string_id == text( g.m_id ) && el.m_flags == g.m_flags;
    }
    if ( kind == 1 )
        return ( el.m_texture ? 1u : 0u ) == ( g.m_asset ? 1u : 0u ) && el.m_align == text( g.m_align );
    std::uint32_t bits;
    std::memcpy( &bits, &el.m_amount, 4 );
    return bits == g.m_amount;
}

// ui_page_layout_* (core/ui pages.hxx) against retail's VuUIPageLayoutEntity
// (oracle case page_layout) on Screens/Credits: OUR loaded properties against
// retail's bytes; OUR load of the real PageLayoutDB entry against the list the
// project load built; the draws 0x62E120 / 0x62E140 (-> 0x62E210 -> 0x62FED0 over
// the three element types) on that list and on written lists - every command's
// words and record byte for byte in ONE sort, each measure / StringDB call's
// inputs (the HOOKS replayed with retail's answers); then the loads 0x62E3B0 on
// the real entry and hand-built documents: elements, +0xD0, the hook calls, the
// factory's exists names.
inline bool verify_ui_page_layout( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    static const value_t pagedb = c_reader( std::vector< std::uint8_t >( reinterpret_cast< const std::uint8_t* >( g_golden_pl_pagedb ),
                                                                        reinterpret_cast< const std::uint8_t* >( g_golden_pl_pagedb ) + g_golden_pl_pagedb_bytes ) ).parse( );
    int failed = 0, commands = 0, measures = 0, strings = 0, runs = 0, loads = 0, elements = 0;
    // the hooks replay retail's recorded calls in order; a differing input fails the run
    struct replay_t
    {
        int m_first_measure = 0, m_measures = 0, m_first_string = 0, m_strings = 0, m_m = 0, m_s = 0;
        bool m_same = true;
    } rp;
    std::string exists;
    ui_page_hooks_t hooks;
    hooks.m_string = [ & ]( const std::string& id )
    {
        if ( rp.m_s >= rp.m_strings )
        {
            rp.m_same = false;
            return std::string( );
        }
        const golden_pl_string_t& g = g_golden_pl_strings[ rp.m_first_string + rp.m_s++ ];
        const std::vector< std::uint8_t > want = ui_unhex( g.m_id ), text = ui_unhex( g.m_text );
        if ( std::string( want.begin( ), want.end( ) ) != id )
            rp.m_same = false;
        ++strings;
        return std::string( text.begin( ), text.end( ) );
    };
    hooks.m_measure = [ & ]( std::uint32_t font, const std::string& text, const std::uint8_t params[ 60 ], float width, std::uint32_t flags,
                             const float scale[ 2 ], float out[ 2 ] )
    {
        if ( rp.m_m >= rp.m_measures )
        {
            rp.m_same = false;
            return false;
        }
        const golden_pl_measure_t& g = g_golden_pl_measures[ rp.m_first_measure + rp.m_m++ ];
        const std::vector< std::uint8_t > t = ui_unhex( g.m_text ), p = ui_unhex( g.m_params );
        std::uint32_t w, s[ 2 ];
        std::memcpy( &w, &width, 4 );
        std::memcpy( s, scale, 8 );
        if ( font != g.m_font || std::string( t.begin( ), t.end( ) ) != text || p.size( ) != 60 || std::memcmp( p.data( ), params, 41 ) != 0 /* 41..59 UNKNOWN */ || w != g.m_width ||
             flags != g.m_flags || s[ 0 ] != g.m_scale[ 0 ] || s[ 1 ] != g.m_scale[ 1 ] )
            rp.m_same = false;
        out[ 0 ] = 0.25f;
        std::memcpy( &out[ 1 ], &g.m_answer, 4 );
        ++measures;
        return true;
    };
    static const ui_texture_t k_load_texture = { 1u, 256, 128 };
    hooks.m_texture = [ & ]( const std::string& name ) -> const ui_texture_t*
    {
        exists += ( exists.empty( ) ? "" : ";" ) + std::string( "VuTextureAsset/" ) + name;
        return &k_load_texture;   // the case's fake asset (a real name, 256 x 128)
    };
    hooks.m_font_asset = [ & ]( const std::string& asset, std::uint32_t& )
    {
        exists += ( exists.empty( ) ? "" : ";" ) + std::string( "VuFontAsset/" ) + asset;
        return false;
    };
    const auto env_of = [ & ]( const char* matrix, const char* block_hex, unsigned top, int default_font ) -> ui_page_env_t
    {
        ui_page_env_t env;
        const std::vector< std::uint8_t > m = ui_unhex( matrix ), block = ui_unhex( block_hex );
        std::memcpy( env.m_draw.m_matrix, m.data( ), 64 );
        std::memcpy( &env.m_draw.m_top, &top, 4 );
        for ( int k = 0; k < 2; ++k )
        {
            env.m_draw.m_material[ k ] = g_golden_pl_font_materials[ k ];
            env.m_draw.m_material_key[ k ] = g_golden_pl_font_materials[ 2 + k ];
        }
        env.m_draw.m_texture_material = g_golden_pl_materials[ 0 ];
        env.m_draw.m_texture_material_key = g_golden_pl_materials[ 1 ];
        env.m_draw.m_rect_material = g_golden_pl_materials[ 2 ];
        env.m_draw.m_rect_material_key = g_golden_pl_materials[ 3 ];
        if ( block.size( ) >= 0xA0 )
        {
            std::memcpy( env.m_crop.m_matrix, block.data( ) + 0x14, 64 );
            std::uint32_t def;
            std::memcpy( &def, block.data( ) + 0x9C, 4 );
            env.m_default = def != 0;
        }
        if ( default_font >= 0 )
            env.m_default = default_font != 0;
        env.m_default_font = g_golden_pl_fonts[ 1 ];
        return env;
    };
    std::vector< ui_texture_t > textures( 16 );
    for ( std::size_t k = 0; k < g_golden_pl_pages_count; ++k )
    {
        const golden_pl_page_t& gp = g_golden_pl_pages[ k ];
        const entity_t* have = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, gp.m_project ) == 0 && gp.m_index >= 0 && static_cast< std::size_t >( gp.m_index ) < loaded.trees[ p ].size( ) )
                have = loaded.trees[ p ][ static_cast< std::size_t >( gp.m_index ) ].second;
        if ( !have || have->m_type != "VuUIPageLayoutEntity" )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui page layout: %s entity %d is not loaded", gp.m_project, gp.m_index );
            continue;
        }
        // OUR loaded properties against retail's bytes
        const ui_page_layout_t retail = ui_pl_of( ui_unhex( gp.m_fields ), gp.m_depth );
        ui_page_layout_t ours = retail;
        bool same = true;
        const auto take = [ & ]( const char* name, void* target, std::size_t bytes )
        {
            std::uint8_t was[ 16 ];
            std::memcpy( was, target, bytes );
            if ( !ui_frame_take( *have, name, target, bytes ) || std::memcmp( was, target, bytes ) != 0 )
                same = false;
        };
        take( "Visible", &ours.m_visible, 1 );
        take( "Screen Scale", ours.m_screen_scale, 8 );
        take( "X", &ours.m_rect[ 0 ], 4 );
        take( "Y", &ours.m_rect[ 1 ], 4 );
        take( "Width", &ours.m_rect[ 2 ], 4 );
        take( "Height", &ours.m_rect[ 3 ], 4 );
        take( "Horizontal Anchor", &ours.m_anchor.m_horizontal, 4 );
        take( "Vertical Anchor", &ours.m_anchor.m_vertical, 4 );
        take( "Horizontal Ratio", &ours.m_anchor.m_h_ratio, 4 );
        take( "Vertical Ratio", &ours.m_anchor.m_v_ratio, 4 );
        take( "Scroll at Start", &ours.m_scroll_at_start, 1 );
        take( "Scroll Speed", &ours.m_scroll_speed, 4 );
        take( "Scroll Lag", &ours.m_scroll_lag, 4 );
        take( "Scroll Residual Damping", &ours.m_damping, 4 );
        take( "Touchable", &ours.m_touchable, 1 );
        take( "Scrolling", &ours.m_scrolling, 1 );
        ours.m_depth = ui_frame_depth( *have );
        same = same && std::memcmp( &ours.m_depth, &gp.m_depth, 4 ) == 0;
        if ( const ui_slot_t* page = ui_property_value( *have, "Page Layout" ) )
            ours.m_page = page->m_text;
        same = same && ours.m_page == gp.m_page;
        // OUR load of the real entry (the project load's environment: no StringDB, no default font, null assets)
        ui_page_env_t none;
        rp = replay_t( );
        exists.clear( );
        same = ui_page_layout_load( ours, &pagedb, ui_font_db( 0 ), none, hooks ) && same && ours.m_elements.size( ) == static_cast< std::size_t >( gp.m_elements );
        for ( int j = 0; same && j < gp.m_elements; ++j )
            same = ui_pl_same( ours.m_elements[ static_cast< std::size_t >( j ) ], g_golden_pl_elements[ gp.m_first_element + j ] );
        same = same && std::memcmp( &ours.m_total, &gp.m_total, 4 ) == 0;
        elements += gp.m_elements;
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui page layout: %s entity %d: OUR loaded fields / elements (%d, retail %d) differ from retail's", gp.m_project, gp.m_index,
                           static_cast< int >( ours.m_elements.size( ) ), gp.m_elements );
            continue;
        }
        for ( int r = 0; r < gp.m_runs; ++r )
        {
            const golden_pl_run_t& v = g_golden_pl_runs[ gp.m_first_run + r ];
            ui_page_layout_t p = ui_pl_of( ui_unhex( v.m_fields ), v.m_depth );
            ui_page_env_t env = env_of( v.m_matrix, v.m_block, v.m_top, -1 );
            if ( v.m_sweep == 0 )
            {
                // the loaded list and properties; the run's runtime bytes (Visible, Scrolling, +0xD0, +0xE4) and fonts
                const ui_page_layout_t run = p;
                p = ours;
                // params bytes 41..59: retail's indeterminate stack (UNKNOWN); 0..40 were compared with OUR load above
                for ( int e = 0; e < gp.m_elements; ++e )
                    if ( p.m_elements[ static_cast< std::size_t >( e ) ].m_kind == ui_page_kind_t::text )
                    {
                        const std::vector< std::uint8_t > params = ui_unhex( g_golden_pl_elements[ gp.m_first_element + e ].m_params );
                        if ( params.size( ) == 60 )
                            std::memcpy( p.m_elements[ static_cast< std::size_t >( e ) ].m_params, params.data( ), 60 );
                    }
                p.m_visible = run.m_visible;
                p.m_scrolling = run.m_scrolling;
                p.m_total = run.m_total;
                p.m_scroll = run.m_scroll;
                int j = 0;
                for ( ui_page_element_t& el : p.m_elements )
                    if ( el.m_kind == ui_page_kind_t::text )
                    {
                        const char mode = v.m_fonts[ 0 ] == 'm' ? ( j % 2 == 0 ? 'a' : 'd' ) : v.m_fonts[ 0 ];
                        el.m_has_asset = mode == 'a';
                        el.m_asset_font = el.m_has_asset ? g_golden_pl_fonts[ 0 ] : 0u;
                        ++j;
                    }
            }
            else
            {
                for ( int j = 0; j < v.m_elements; ++j )
                {
                    const golden_pl_element_t& g = g_golden_pl_elements[ v.m_first_element + j ];
                    ui_page_element_t el;
                    const std::vector< std::uint8_t > id = ui_unhex( g.m_id ), align = ui_unhex( g.m_align ), params = ui_unhex( g.m_params );
                    if ( g.m_kind == 0 )
                    {
                        el.m_kind = ui_page_kind_t::text;
                        if ( params.size( ) == 60 )
                            std::memcpy( el.m_params, params.data( ), 60 );
                        el.m_has_asset = g.m_asset != 0;
                        el.m_asset_font = el.m_has_asset ? g_golden_pl_fonts[ 0 ] : 0u;
                        el.m_string_id.assign( id.begin( ), id.end( ) );
                        el.m_flags = g.m_flags;
                    }
                    else if ( g.m_kind == 1 )
                    {
                        el.m_kind = ui_page_kind_t::image;
                        el.m_align.assign( align.begin( ), align.end( ) );
                        if ( g.m_width >= 0 )
                        {
                            ui_texture_t& t = textures[ static_cast< std::size_t >( j % 16 ) ];
                            t.m_handle = g_golden_pl_textures + 0x20u * static_cast< unsigned >( j % 16 );
                            t.m_width = g.m_width;
                            t.m_height = g.m_height;
                            el.m_texture = &t;
                        }
                    }
                    else
                    {
                        el.m_kind = ui_page_kind_t::space;
                        std::memcpy( &el.m_amount, &g.m_amount, 4 );
                    }
                    p.m_elements.push_back( el );
                }
                env.m_default = v.m_default != 0;
            }
            rp = replay_t( );
            rp.m_first_measure = v.m_first_measure;
            rp.m_measures = v.m_measures;
            rp.m_first_string = v.m_first_string;
            rp.m_strings = v.m_strings;
            hooks.m_db = ui_string_db_t( );
            sort.m_index = v.m_buffer;
            sort.m_key_lo = v.m_key_in[ 0 ];
            sort.m_key_hi = v.m_key_in[ 1 ];
            std::vector< ui_gfx_command_t >& list = sort.m_commands[ v.m_buffer ];
            const std::size_t before = list.size( );
            float alpha;
            std::memcpy( &alpha, &v.m_alpha, 4 );
            bool known;
            if ( v.m_layout )
                known = ui_page_layout_layout_draw( sort, env, hooks, p, static_cast< std::uint8_t >( v.m_arg ) );
            else
                known = ui_page_layout_draw( sort, env, hooks, p, alpha );
            bool ok = known && rp.m_same && rp.m_m == rp.m_measures && rp.m_s == rp.m_strings && list.size( ) - before == static_cast< std::size_t >( v.m_commands ) &&
                      sort.m_key_lo == v.m_key[ 0 ] && sort.m_key_hi == v.m_key[ 1 ];
            for ( int c = 0; ok && c < v.m_commands; ++c )
            {
                const golden_txt_command_t& want = g_golden_pl_commands[ v.m_first + c ];
                const ui_gfx_command_t& got = list[ before + static_cast< std::size_t >( c ) ];
                const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
                const auto& memory = sort.m_buffers[ v.m_buffer ].m_memory;
                ok = std::memcmp( got.m_words, want.m_words, sizeof( got.m_words ) ) == 0 && got.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                     std::memcmp( memory.data( ) + got.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
                ++commands;
            }
            ++runs;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui page layout: run %d (sweep %d, %s) gave %d commands / %d measures / %d strings, retail %d / %d / %d - or a word, record "
                           "byte or hook input differs", r, v.m_sweep, v.m_layout ? "layout" : "draw", static_cast< int >( list.size( ) - before ), rp.m_m,
                           rp.m_s, v.m_commands, v.m_measures, v.m_strings );
        }
        for ( int l = 0; l < gp.m_loads; ++l )
        {
            const golden_pl_load_t& g = g_golden_pl_loads[ gp.m_first_load + l ];
            ui_page_layout_t p = ours;
            value_t doc;
            const value_t* db = &pagedb;
            if ( g.m_doc )
            {
                doc = c_reader( std::vector< std::uint8_t >( reinterpret_cast< const std::uint8_t* >( g.m_doc ),
                                                             reinterpret_cast< const std::uint8_t* >( g.m_doc ) + g.m_doc_bytes ) ).parse( );
                db = &doc;
                p.m_page = g.m_page;
            }
            ui_page_env_t env;
            env.m_default = g.m_default != 0;
            env.m_default_font = g_golden_pl_fonts[ 1 ];
            rp = replay_t( );
            rp.m_first_measure = g.m_first_measure;
            rp.m_measures = g.m_measures;
            rp.m_first_string = g.m_first_string;
            rp.m_strings = g.m_strings;
            hooks.m_db.m_present = g.m_sdb >= 0;
            hooks.m_db.m_slot12 = g.m_sdb == 1;
            exists.clear( );
            bool ok = ui_page_layout_load( p, db, ui_font_db( 0 ), env, hooks ) && rp.m_same && rp.m_m == rp.m_measures && rp.m_s == rp.m_strings &&
                      p.m_elements.size( ) == static_cast< std::size_t >( g.m_elements ) && std::memcmp( &p.m_total, &g.m_total, 4 ) == 0 && exists == g.m_exists;
            for ( int j = 0; ok && j < g.m_elements; ++j )
                ok = ui_pl_same( p.m_elements[ static_cast< std::size_t >( j ) ], g_golden_pl_elements[ g.m_first_element + j ] );
            elements += g.m_elements;
            ++loads;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui page layout: load %d (%s) gave %d elements / %d measures / %d strings, exists %s (retail %d / %d / %d, %s) - or an element "
                           "or +0xD0 differs", l, g.m_page, static_cast< int >( p.m_elements.size( ) ), rp.m_m, rp.m_s, exists.c_str( ), g.m_elements,
                           g.m_measures, g.m_strings, g.m_exists );
        }
    }
    hooks.m_db = ui_string_db_t( );
    if ( failed == 0 )
        LOG_SUCCESS( "ui_page_layout_* match retail's VuUIPageLayoutEntity (draw 0x62E120, layout draw 0x62E140, 0x62E210, list 0x62FED0, the Text / "
                     "Image / Space heights and draws, load 0x62E3B0 -> 0x62FA90) on %d entity: %d draw runs, %d loads, %d elements, %d commands and "
                     "records byte for byte, %d measure / %d StringDB hook calls",
            static_cast< int >( g_golden_pl_pages_count ), runs, loads, elements, commands, measures, strings );
    else
        LOG_ERROR( "the UI page layout FAILED %d checks against retail's", failed );
    return failed == 0;
}

// The page layout's runtime bytes +0xD0..+0xF4 (byte +0xD7 carried from `pad`).
inline std::string ui_plt_state( const ui_page_layout_t& p, std::uint8_t pad )
{
    std::uint8_t b[ 0x24 ];
    std::memcpy( b + 0x00, &p.m_total, 4 );
    b[ 4 ] = p.m_active;
    b[ 5 ] = p.m_ended;
    b[ 6 ] = p.m_held;
    b[ 7 ] = pad;
    std::memcpy( b + 0x08, p.m_touch, 8 );
    std::memcpy( b + 0x10, &p.m_grab, 4 );
    std::memcpy( b + 0x14, &p.m_scroll, 4 );
    std::memcpy( b + 0x18, &p.m_target, 4 );
    std::memcpy( b + 0x1C, &p.m_velocity, 4 );
    std::memcpy( b + 0x20, &p.m_residual, 4 );
    std::string out;
    char h[ 3 ];
    for ( std::uint8_t c : b )
    {
        std::snprintf( h, sizeof( h ), "%02x", c );
        out += h;
    }
    return out;
}

// ui_page_layout_initialize / _tick / _start / _stop / _reset / _touch_* /
// _release (core/ui pages.hxx) against retail's VuUIPageLayoutEntity 0x62D9A0 /
// 0x62DA30 / 0x62E0A0 / 0x62E0C0 / 0x62E0E0 / 0x62DD20 / 0x62DED0 / 0x62DEF0 /
// 0x62DA10 (oracle case page_layout_tick) on Screens/Credits' page through a
// scripted sequence under three crops: after each step the bytes +0xD0..+0xF4,
// the touch device's entries and +0x1C, the hook calls (the device position,
// OnReachedEnd's findPlug + execute). Starts from retail's bytes; the property
// fields are the ones verify_ui_page_layout compares with OUR loaded entity.
inline bool verify_ui_page_layout_tick( )
{
    int failed = 0, steps = 0, ends = 0;
    for ( std::size_t k = 0; k < g_golden_plt_runs_count; ++k )
    {
        const golden_plt_run_t& run = g_golden_plt_runs[ k ];
        const std::vector< std::uint8_t > raw = ui_unhex( run.m_fields ), block = ui_unhex( run.m_block );
        if ( raw.size( ) < 0x98 || block.size( ) < 0xA0 )
        {
            ++failed;
            continue;
        }
        ui_page_layout_t p = ui_pl_of( raw, 0u );
        const auto at = [ &raw ]( int offset ) { return raw.data( ) + ( offset - 0x60 ); };
        p.m_active = *at( 0xD4 );
        p.m_ended = *at( 0xD5 );
        p.m_held = *at( 0xD6 );
        const std::uint8_t pad = *at( 0xD7 );
        std::memcpy( p.m_touch, at( 0xD8 ), 8 );
        std::memcpy( &p.m_grab, at( 0xE0 ), 4 );
        std::memcpy( &p.m_target, at( 0xE8 ), 4 );
        std::memcpy( &p.m_velocity, at( 0xEC ), 4 );
        std::memcpy( &p.m_residual, at( 0xF0 ), 4 );
        ui_crop_t crop;
        std::memcpy( crop.m_matrix, block.data( ) + 0x14, 64 );
        float inverse[ 16 ];
        std::memcpy( inverse, block.data( ) + 0x54, 64 );
        ui_touch_device_t device;
        const int self = 0;
        for ( int s = 0; s < run.m_steps; ++s )
        {
            const golden_plt_step_t& g = g_golden_plt_steps[ run.m_first + s ];
            float a[ 2 ];
            std::memcpy( a, g.m_arg, 8 );
            std::string fired;
            switch ( g.m_op )
            {
            case 0: ui_page_layout_initialize( p, [ & ] { ui_touch_add( device, &self, 0u ); } ); break;
            case 1: ui_page_layout_release( p, [ & ] { ui_touch_remove( device, &self ); } ); break;
            case 2: ui_page_layout_start( p ); break;
            case 3: ui_page_layout_stop( p ); break;
            case 4: ui_page_layout_reset( p ); break;
            case 5:
                ui_page_layout_tick( p, crop, a[ 0 ], [ & ]
                {
                    fired = "find OnReachedEnd;execute 0";
                    ++ends;
                } );
                break;
            case 6: ui_page_layout_touch_down( p, crop, inverse, a ); break;
            case 7: ui_page_layout_touch_up( p ); break;
            case 8:
                if ( p.m_held )
                    fired = "position 0";
                ui_page_layout_touch_move( p, crop, inverse, a );
                break;
            default:
                if ( g.m_at == 0xD0 )
                    p.m_total = a[ 0 ];
                else if ( g.m_at == 0xB0 )
                    p.m_scroll_speed = a[ 0 ];
                else if ( g.m_at == 0xB4 )
                    p.m_scroll_lag = a[ 0 ];
                else if ( g.m_at == 0xB8 )
                    p.m_damping = a[ 0 ];
                else if ( g.m_at == 0xE4 )
                    p.m_scroll = a[ 0 ];
                break;
            }
            std::string entries;
            for ( const auto& c : device.m_callbacks )
                entries += ( c.first == &self ? std::string( "self" ) : std::string( "other" ) ) + "," + std::to_string( c.second ) + ";";
            ++steps;
            const std::string state = ui_plt_state( p, pad );
            if ( ( state != g.m_state || entries != g.m_entries || fired != g.m_fired ) && ++failed <= 8 )
                LOG_ERROR( "ui page layout tick: run %d (env %d) step %d (op %d): %s [%s] {%s} (retail %s [%s] {%s})", static_cast< int >( k ), run.m_env, s, g.m_op,
                           state.c_str( ), entries.c_str( ), fired.c_str( ), g.m_state, g.m_entries, g.m_fired );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_page_layout_tick / _initialize / plugs / touch callbacks / _release match retail's VuUIPageLayoutEntity 0x62DA30 / 0x62D9A0 / "
                     "0x62E0A0 0x62E0C0 0x62E0E0 / 0x62DD20 0x62DED0 0x62DEF0 / 0x62DA10 on %d runs: %d steps (+0xD0..+0xF4, the touch entries, the hook "
                     "calls; %d OnReachedEnd fires)", static_cast< int >( g_golden_plt_runs_count ), steps, ends );
    else
        LOG_ERROR( "the UI page layout tick FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_control_methods_init / _release (core/ui controls.hxx) against retail's
// VuControlMethodManager slot 5 0x4AE890 / slot 3 0x4AE980 (oracle case
// control_methods: 0x53FB90 / 0x4F6700 / 0x539020 live on fake devices): per
// seeded run init, release, release - both lists, the key size and +0x150, the
// touch +0x1C, the platform hooks' log.
inline bool verify_ui_control_methods( )
{
    int failed = 0, steps = 0;
    const int self4 = 0, self8 = 0, o1 = 0, o2 = 0;
    const auto parse = [ & ]( const char* text, const void* self, std::vector< std::pair< const void*, std::uint32_t > >& out )
    {
        for ( const char* at = text; *at; )
        {
            const char* comma = std::strchr( at, ',' );
            const char* semi = comma ? std::strchr( comma, ';' ) : nullptr;
            if ( !semi )
                break;
            const std::string t( at, comma );
            out.push_back( { t == "self" ? self : t == "o1" ? static_cast< const void* >( &o1 ) : static_cast< const void* >( &o2 ),
                             static_cast< std::uint32_t >( std::strtoul( comma + 1, nullptr, 10 ) ) } );
            at = semi + 1;
        }
    };
    const auto print = [ & ]( const std::vector< std::pair< const void*, std::uint32_t > >& list, const void* self )
    {
        std::string s;
        for ( const auto& c : list )
            s += std::string( c.first == self ? "self" : c.first == &o1 ? "o1" : "o2" ) + "," + std::to_string( c.second ) + ";";
        return s;
    };
    for ( std::size_t k = 0; k < g_golden_cmm_runs_count; ++k )
    {
        const golden_cmm_run_t& run = g_golden_cmm_runs[ k ];
        ui_touch_device_t touch;
        touch.m_priority = 3u;
        ui_key_device_t keys;
        parse( run.m_touch, &self4, touch.m_low_level );
        parse( run.m_keys, &self8, keys.m_callbacks );
        keys.m_priority = run.m_top;
        for ( int s = 0; s < run.m_steps; ++s )
        {
            const golden_cmm_step_t& g = g_golden_cmm_steps[ run.m_first + s ];
            std::string log;
            ui_control_methods_hooks_t h;
            h.m_tick_add = [ & ] { log += std::string( log.empty( ) ? "" : ";" ) + "tick_add 0x7454F0 0x4AEC40 self Input"; };
            h.m_tick_remove = [ & ] { log += std::string( log.empty( ) ? "" : ";" ) + "tick_remove self"; };
            h.m_input = [ & ]( std::uint8_t v ) { log += std::string( log.empty( ) ? "" : ";" ) + "input " + std::to_string( v ); };
            int result = -1;
            if ( g.m_op == 0 )
                result = ui_control_methods_init( touch, keys, &self4, &self8, h ) ? 1 : 0;
            else
                ui_control_methods_release( touch, keys, &self4, &self8, h );
            ++steps;
            if ( ( result != g.m_result || print( touch.m_low_level, &self4 ) != g.m_second || print( keys.m_callbacks, &self8 ) != g.m_keys ||
                   static_cast< int >( keys.m_callbacks.size( ) ) != g.m_size || keys.m_priority != g.m_top || touch.m_priority != g.m_priority || log != g.m_log ) &&
                 ++failed <= 8 )
                LOG_ERROR( "ui control methods: run %d step %d: %s | %s | %u | %s (retail %s | %s | %u | %s)", static_cast< int >( k ), s,
                           print( touch.m_low_level, &self4 ).c_str( ), print( keys.m_callbacks, &self8 ).c_str( ), keys.m_priority, log.c_str( ), g.m_second,
                           g.m_keys, g.m_top, g.m_log );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_control_methods_init / _release match retail's VuControlMethodManager slot 5 0x4AE890 / slot 3 0x4AE980 on %d runs: %d steps "
                     "(the touch device's second list, the key device's list / size / +0x150, the tick / input hooks)",
            static_cast< int >( g_golden_cmm_runs_count ), steps );
    else
        LOG_ERROR( "the UI control methods FAILED %d checks against retail's", failed );
    return failed == 0;
}

// THE FRAME WIRING of the message box: ui_root_frame (core/ui frame.hxx) on OUR
// loaded Screens/MessageBox root with the m_message_box input against retail's
// 0x4675A0 with its children's draws LIVE (oracle case message_box_frame): the
// box's records (fields from OUR properties via ui_message_box_of, the runtime
// state from the run) and the two image children's (their textures null), every
// command byte for byte in one sort.
inline bool verify_ui_message_box_frame( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    int failed = 0, commands = 0, leaves = 0;
    for ( std::size_t k = 0; k < g_golden_mbf_runs_count; ++k )
    {
        const golden_mbf_run_t& v = g_golden_mbf_runs[ k ];
        const entity_t* root = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count && !root; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, v.m_project ) == 0 && !loaded.trees[ p ].empty( ) )
                root = loaded.trees[ p ][ 0 ].second;
        if ( !root )
        {
            ++failed;
            continue;
        }
        const std::vector< std::uint8_t > raw = ui_unhex( v.m_fields ), block = ui_unhex( v.m_block ), matrix = ui_unhex( v.m_matrix );
        const auto at = [ &raw ]( int offset ) { return raw.data( ) + ( offset - 0x60 ); };
        std::uint32_t def;
        std::memcpy( &def, block.data( ) + 0x9C, 4 );
        bool params_same = true;
        ui_frame_inputs_t in;
        for ( int m = 0; m < 2; ++m )
        {
            in.m_env.m_material[ m ] = g_golden_mbx_font_materials[ m ];
            in.m_env.m_material_key[ m ] = g_golden_mbx_font_materials[ 2 + m ];
        }
        in.m_env.m_texture_material = g_golden_mbx_materials[ 0 ];
        in.m_env.m_texture_material_key = g_golden_mbx_materials[ 1 ];
        in.m_env.m_rect_material = g_golden_mbx_materials[ 2 ];
        in.m_env.m_rect_material_key = g_golden_mbx_materials[ 3 ];
        std::memcpy( in.m_env.m_matrix, matrix.data( ), 64 );
        in.m_db.m_present = v.m_sdb >= 0;
        in.m_db.m_slot12 = v.m_sdb == 1;
        in.m_fontdb = &ui_font_db( 0 );
        in.m_message_box = [ & ]( const entity_t&, ui_message_box_t& b, ui_message_box_inputs_t& mi )
        {
            for ( int j = 0; j < 5; ++j )
            {
                ui_message_block_t& t = b.m_blocks[ j ];
                const int o = 0x78 + 0x9C * j;
                if ( std::memcmp( t.m_params, at( o + 0x30 ), 41 ) != 0 )
                    params_same = false;
                std::memcpy( t.m_params, at( o + 0x30 ), 60 );   // 41..59: retail's indeterminate stack (UNKNOWN)
                t.m_enabled = static_cast< std::uint8_t >( ( v.m_mask >> j ) & 1 );
                const std::vector< std::uint8_t > text = ui_unhex( v.m_texts[ j ] );
                t.m_text.assign( text.begin( ), text.end( ) );
                t.m_font.m_asset = v.m_fonts[ j ] == 0;
                t.m_font.m_asset_font = g_golden_mbx_fonts[ 0 ];
                t.m_font.m_default = def != 0;
                t.m_font.m_default_font = g_golden_mbx_fonts[ 1 ];
            }
            b.m_image_flip = *at( 0x528 );
            std::memcpy( b.m_progress, at( 0x52C ), 8 );
            std::memcpy( &b.m_rotation, at( 0x578 ), 4 );
            std::memcpy( &b.m_button, at( 0x588 ), 4 );
            for ( int j = 0; j < 6; ++j )
                mi.m_buttons[ j ] = ( v.m_textures >> j ) & 1u ? g_golden_mbx_textures + 0x20u * static_cast< unsigned >( j ) : 0u;
            mi.m_spinner = ( v.m_textures >> 6 ) & 1u ? g_golden_mbx_textures + 0xC0u : 0u;
            mi.m_image = ( v.m_textures >> 7 ) & 1u ? g_golden_mbx_textures + 0xE0u : 0u;
            return true;
        };
        ui_impl_t impl;
        std::memcpy( impl.m_crop.m_matrix, block.data( ) + 0x14, 64 );
        std::memcpy( &impl.m_float, block.data( ) + 0x94, 4 );
        std::memcpy( &impl.m_aspect, block.data( ) + 0x98, 4 );
        sort.m_index = v.m_buffer;
        sort.m_key_lo = v.m_key_in[ 0 ];
        sort.m_key_hi = v.m_key_in[ 1 ];
        std::vector< ui_gfx_command_t >& list = sort.m_commands[ v.m_buffer ];
        const std::size_t before = list.size( );
        float alpha;
        std::memcpy( &alpha, &v.m_alpha, 4 );
        const ui_frame_t frame = ui_root_frame( *root, impl, sort, alpha, &in );
        bool same = params_same && frame.m_unknown == 0 && frame.m_unrecreated == 0 && list.size( ) - before == static_cast< std::size_t >( v.m_commands ) &&
                    sort.m_key_lo == v.m_key[ 0 ] && sort.m_key_hi == v.m_key[ 1 ];
        for ( int c = 0; same && c < v.m_commands; ++c )
        {
            const golden_txt_command_t& want = g_golden_mbf_commands[ v.m_first + c ];
            const ui_gfx_command_t& got = list[ before + static_cast< std::size_t >( c ) ];
            const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
            const auto& memory = sort.m_buffers[ v.m_buffer ].m_memory;
            same = std::memcmp( got.m_words, want.m_words, sizeof( got.m_words ) ) == 0 && got.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                   std::memcmp( memory.data( ) + got.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
            ++commands;
        }
        leaves += static_cast< int >( frame.m_draws.size( ) );
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "ui message box frame: run %d gave %d commands (%d leaves, %d unrecreated, %d unknown), retail %d - or a word / record byte / key differs",
                       static_cast< int >( k ), static_cast< int >( list.size( ) - before ), static_cast< int >( frame.m_draws.size( ) ), frame.m_unrecreated,
                       frame.m_unknown, v.m_commands );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_root_frame with the message box kind matches retail's VuMessageBoxScreenEntity 0x4675A0 with its children's draws live on %d runs: "
                     "%d leaves, %d commands and records byte for byte", static_cast< int >( g_golden_mbf_runs_count ), leaves, commands );
    else
        LOG_ERROR( "the UI message box frame FAILED %d checks against retail's", failed );
    return failed == 0;
}

// The row's fields from retail's bytes +0x60..+0x230, its Type / Channel / Label and Depth.
inline ui_remap_t ui_rm_of( const std::vector< std::uint8_t >& raw, const golden_rm_row_t& row, std::uint32_t depth )
{
    ui_remap_t r;
    const auto at = [ &raw ]( int offset ) { return raw.data( ) + ( offset - 0x60 ); };
    r.m_visible = *at( 0x6C );
    std::memcpy( r.m_scale, at( 0x70 ), 8 );
    std::memcpy( r.m_pos, at( 0x78 ), 8 );
    std::memcpy( r.m_label_offset, at( 0x80 ), 8 );
    std::memcpy( r.m_label_params, at( 0xA8 ), 60 );
    std::memcpy( r.m_control_offset, at( 0x104 ), 8 );
    std::memcpy( r.m_control_image, at( 0x10C ), 16 );
    std::memcpy( r.m_control_params, at( 0x13C ), 60 );
    std::memcpy( &r.m_selected_colour, at( 0x180 ), 4 );
    std::memcpy( r.m_touch, at( 0x184 ), 16 );
    std::memcpy( r.m_image, at( 0x194 ), 16 );
    r.m_label = row.m_label;
    r.m_type = row.m_type;
    r.m_channel = row.m_channel;
    r.m_config = row.m_config;
    std::memcpy( &r.m_depth, &depth, 4 );
    return r;
}

// ui_remap_draw / ui_remap_layout_draw (core/ui remap.hxx) against retail's
// 0x45FB90 / 0x461A30 -> 0x461B00 (oracle case remap_draw) on every remapping row
// of Screens/ControlsGamepad: OUR loaded properties against retail's bytes; the
// runs (every binding type, auto, no input manager, selected, hover, no textures
// / fonts, Visible off, a sweep of the fields): every command's words and record
// byte for byte in ONE sort, every input-manager / StringDB hook call's inputs
// (answered with retail's).
inline bool verify_ui_remap_draw( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    const auto text = []( const char* hexed ) { const std::vector< std::uint8_t > v = ui_unhex( hexed ); return std::string( v.begin( ), v.end( ) ); };
    int failed = 0, commands = 0, calls = 0, runs = 0;
    for ( std::size_t k = 0; k < g_golden_rm_rows_count; ++k )
    {
        const golden_rm_row_t& row = g_golden_rm_rows[ k ];
        const std::vector< std::uint8_t > raw = ui_unhex( row.m_fields );
        const entity_t* have = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, row.m_project ) == 0 && row.m_index >= 0 && static_cast< std::size_t >( row.m_index ) < loaded.trees[ p ].size( ) )
                have = loaded.trees[ p ][ static_cast< std::size_t >( row.m_index ) ].second;
        const ui_remap_t retail = ui_rm_of( raw, row, row.m_depth );
        ui_remap_t ours = retail;
        bool same = have != nullptr;
        if ( have )
        {
            const auto take = [ & ]( const char* name, void* target, std::size_t bytes )
            {
                std::uint8_t was[ 16 ];
                std::memcpy( was, target, bytes );
                ui_frame_take( *have, name, target, bytes );
                if ( std::memcmp( was, target, bytes ) != 0 )
                    same = false;
            };
            static const char* const k_rects[ 3 ] = { "Control Image", "Touch", "Image" };
            float* const rects[ 3 ] = { ours.m_control_image, ours.m_touch, ours.m_image };
            static const char* const k_xywh[ 4 ] = { " X", " Y", " Width", " Height" };
            take( "Visible", &ours.m_visible, 1 );
            take( "Screen Scale", ours.m_scale, 8 );
            take( "Pos", ours.m_pos, 8 );
            take( "Label Offset", ours.m_label_offset, 8 );
            take( "Control Offset", ours.m_control_offset, 8 );
            take( "Control Selected Text Color", &ours.m_selected_colour, 4 );
            take( "Config", &ours.m_config, 4 );
            for ( int r = 0; r < 3; ++r )
                for ( int c = 0; c < 4; ++c )
                    take( ( std::string( k_rects[ r ] ) + k_xywh[ c ] ).c_str( ), &rects[ r ][ c ], 4 );
            const auto slot_text = [ & ]( const char* name ) { const ui_slot_t* s = ui_property_value( *have, name ); return s ? s->m_text : std::string( ); };
            same = same && slot_text( "Type" ) == row.m_type && slot_text( "Channel Name" ) == row.m_channel && slot_text( "Label String ID" ) == row.m_label;
            std::uint8_t params[ 60 ] = { };
            std::string asset;
            ui_font_params( ui_font_db( 0 ), slot_text( "Label Font" ), params, asset );
            same = same && std::memcmp( params, retail.m_label_params, 41 ) == 0;
            ui_font_params( ui_font_db( 0 ), slot_text( "Control Font" ), params, asset );
            same = same && std::memcmp( params, retail.m_control_params, 41 ) == 0;
            const float depth = ui_frame_depth( *have );
            same = same && std::memcmp( &depth, &row.m_depth, 4 ) == 0;
        }
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui remap: row %d (%s %s): OUR loaded properties differ from retail's", row.m_index, row.m_type, row.m_channel );
            continue;
        }
        for ( int r = 0; r < row.m_runs; ++r )
        {
            const golden_rm_run_t& v = g_golden_rm_runs[ row.m_first + r ];
            ui_remap_t p = ui_rm_of( ui_unhex( v.m_fields ), row, v.m_depth );
            p.m_selected = static_cast< std::uint8_t >( v.m_selected );
            p.m_hover = v.m_hover;
            for ( int j = 0; j < 6; ++j )
                p.m_textures[ j ] = ( v.m_textures >> j ) & 1u ? g_golden_rm_textures + 0x20u * static_cast< unsigned >( j ) : 0u;
            const std::vector< std::uint8_t > block = ui_unhex( v.m_block ), matrix = ui_unhex( v.m_matrix );
            std::uint32_t def;
            std::memcpy( &def, block.data( ) + 0x9C, 4 );
            for ( ui_text_font_t* f : { &p.m_label_font, &p.m_control_font } )
            {
                const int which = f == &p.m_label_font ? v.m_fonts[ 0 ] : v.m_fonts[ 1 ];
                f->m_asset = which == 0;
                f->m_asset_font = g_golden_rm_fonts[ 0 ];
                f->m_default = def != 0;
                f->m_default_font = g_golden_rm_fonts[ 1 ];
            }
            ui_font_draw_env_t env;
            std::memcpy( env.m_matrix, matrix.data( ), 64 );
            std::memcpy( &env.m_top, &v.m_top, 4 );
            for ( int m = 0; m < 2; ++m )
            {
                env.m_material[ m ] = g_golden_rm_font_materials[ m ];
                env.m_material_key[ m ] = g_golden_rm_font_materials[ 2 + m ];
            }
            env.m_texture_material = g_golden_rm_materials[ 0 ];
            env.m_texture_material_key = g_golden_rm_materials[ 1 ];
            env.m_rect_material = g_golden_rm_materials[ 2 ];
            env.m_rect_material_key = g_golden_rm_materials[ 3 ];
            ui_string_db_t db;
            db.m_present = true;
            db.m_slot12 = v.m_sdb == 1;
            int at = 0;
            bool hooks_same = true;
            const auto next = [ & ]( int kind ) -> const golden_rm_call_t*
            {
                if ( at >= v.m_calls || g_golden_rm_calls[ v.m_first_call + at ].m_kind != kind )
                {
                    hooks_same = false;
                    return nullptr;
                }
                ++calls;
                return &g_golden_rm_calls[ v.m_first_call + at++ ];
            };
            ui_remap_inputs_t in;
            in.m_input = v.m_input != 0;
            in.m_throttle = row.m_vtable == 0x73D45Cu;
            in.m_auto[ 0 ] = static_cast< std::uint8_t >( v.m_auto[ 0 ] );
            in.m_auto[ 1 ] = static_cast< std::uint8_t >( v.m_auto[ 1 ] );
            in.m_lookup = [ & ]( int slot, std::int32_t config, const std::string& channel, std::int32_t& type, std::int32_t& index )
            {
                const golden_rm_call_t* c = next( slot );
                if ( !c || text( c->m_id ) != channel || c->m_a != 0u || c->m_b != static_cast< unsigned >( config ) )
                    hooks_same = false;
                type = v.m_type;
                index = v.m_index;
            };
            in.m_axis_name = []( std::int32_t index ) { return std::string( g_golden_rm_axes[ index ] ); };
            in.m_button_name = []( std::int32_t index ) { return std::string( g_golden_rm_buttons[ index ] ); };
            in.m_string = [ & ]( const std::string& id )
            {
                const golden_rm_call_t* c = next( 7 );
                if ( !c || text( c->m_id ) != id )
                    hooks_same = false;
                return c ? text( c->m_text ) : std::string( );
            };
            in.m_string_literal = [ & ]( const char* id )
            {
                const golden_rm_call_t* c = next( 6 );
                if ( !c || text( c->m_id ) != id )
                    hooks_same = false;
                return c ? text( c->m_text ) : std::string( );
            };
            in.m_missing = [ & ]
            {
                const golden_rm_call_t* c = next( 9 );
                return c ? text( c->m_text ) : std::string( );
            };
            sort.m_index = v.m_buffer;
            sort.m_key_lo = v.m_key_in[ 0 ];
            sort.m_key_hi = v.m_key_in[ 1 ];
            std::vector< ui_gfx_command_t >& list = sort.m_commands[ v.m_buffer ];
            const std::size_t before = list.size( );
            float alpha;
            std::memcpy( &alpha, &v.m_alpha, 4 );
            const bool known = v.m_layout ? ui_remap_layout_draw( sort, env, db, p, in, static_cast< std::uint8_t >( v.m_arg ) ) : ui_remap_draw( sort, env, db, p, in, alpha );
            bool ok = known && hooks_same && at == v.m_calls && list.size( ) - before == static_cast< std::size_t >( v.m_commands ) && sort.m_key_lo == v.m_key[ 0 ] && sort.m_key_hi == v.m_key[ 1 ];
            for ( int c = 0; ok && c < v.m_commands; ++c )
            {
                const golden_txt_command_t& want = g_golden_rm_commands[ v.m_first + c ];
                const ui_gfx_command_t& got = list[ before + static_cast< std::size_t >( c ) ];
                const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
                const auto& memory = sort.m_buffers[ v.m_buffer ].m_memory;
                ok = std::memcmp( got.m_words, want.m_words, sizeof( got.m_words ) ) == 0 && got.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                     std::memcmp( memory.data( ) + got.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
                ++commands;
            }
            ++runs;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui remap: row %d run %d (type %d index %d, %s) gave %d commands / %d hook calls, retail %d / %d - or a word, record byte or hook input differs",
                           row.m_index, r, v.m_type, v.m_index, v.m_layout ? "layout" : "draw", static_cast< int >( list.size( ) - before ), at, v.m_commands, v.m_calls );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_remap_draw / _layout_draw match retail's remapping rows 0x45FB90 / 0x461A30 -> 0x461B00 (0x4626D0, 0x4629A0) on %d rows: %d runs, %d commands and "
                     "records byte for byte, %d hook calls", static_cast< int >( g_golden_rm_rows_count ), runs, commands, calls );
    else
        LOG_ERROR( "the UI remapping row FAILED %d checks against retail's", failed );
    return failed == 0;
}

// The binding's glue for the batch 28 leaves, ROUTING and not a retail comparison
// (the functions it reaches are verify_ui_page_layout_tick / verify_ui_remap_logic's):
// on OUR Screens/Credits and Screens/ControlsGamepad trees the broadcast OnUITick
// reaches the page's tick (the scroll against the same ticks run directly, bit for
// bit), and OnUISetFocus / OnUIEvent reach a row: focus by id, InputUp only while
// selected fires its NavUp sound path through ui_remap_event.
inline bool verify_ui_wired_bind( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    const auto root_of = [ & ]( const char* asset ) -> entity_t*
    {
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, asset ) == 0 && !loaded.trees[ p ].empty( ) )
                return const_cast< entity_t* >( loaded.trees[ p ][ 0 ].second );
        return nullptr;
    };
    int failed = 0, ticks = 0, pages = 0, rows = 0;
    float dt = 0.1f;
    {
        entity_t* root = root_of( "Screens/Credits" );
        ui_bind_hooks_t hooks;
        hooks.m_delta = [ &dt ] { return dt; };
        c_ui_binding binding( *root, hooks );
        const auto visit = [ & ]( const auto& self, const entity_t& e ) -> void
        {
            if ( ui_page_layout_t* page = binding.page( e ) )
            {
                ui_page_layout_t direct = *page;
                ui_crop_t crop;
                page->m_active = direct.m_active = 1;
                page->m_total = direct.m_total = 0.05f;
                for ( int k = 0; k < 40; ++k )
                {
                    binding.post( fsm_hash( "OnUITick" ), ui_params_t( ) );
                    ui_page_layout_tick( direct, crop, dt, nullptr );
                    ++ticks;
                    if ( std::memcmp( &direct.m_scroll, &page->m_scroll, 4 ) != 0 || direct.m_ended != page->m_ended )
                        ++failed;
                }
                ++pages;
            }
            for ( const auto& child : e.m_children )
                self( self, *child );
        };
        visit( visit, *root );
    }
    {
        entity_t* root = root_of( "Screens/ControlsGamepad" );
        ui_bind_hooks_t hooks;
        hooks.m_font_db = &ui_font_db( 0 );
        c_ui_binding binding( *root, hooks );
        const auto visit = [ & ]( const auto& self, const entity_t& e ) -> void
        {
            if ( ui_remap_t* row = binding.remap( e ) )
            {
                ui_params_t focus;
                ui_params_add( focus, 1, ui_entity_id( ui_long_name( e ) ) );
                binding.post( fsm_hash( "OnUISetFocus" ), focus );
                if ( binding.remap_state( e )->m_focus != 1u )
                    ++failed;
                ui_params_t other;
                ui_params_add( other, 1, 0x12345678u );
                binding.post( fsm_hash( "OnUISetFocus" ), other );
                if ( binding.remap_state( e )->m_focus != 0u )
                    ++failed;
                ui_params_t select;
                select.add_string( "InputSelect" );
                row->m_selected = 0;
                binding.post( fsm_hash( "OnUIEvent" ), select );
                if ( binding.remap_state( e )->m_select != 0u )
                    ++failed;
                row->m_selected = 1;
                binding.post( fsm_hash( "OnUIEvent" ), select );
                if ( binding.remap_state( e )->m_select != 1u )
                    ++failed;
                ++rows;
            }
            for ( const auto& child : e.m_children )
                self( self, *child );
        };
        visit( visit, *root );
    }
    if ( pages == 0 || rows == 0 )
        ++failed;
    if ( failed == 0 )
        LOG_SUCCESS( "c_ui_binding routes the page layout tick on %d pages (%d ticks, the scroll bit for bit against the direct tick) and the remapping rows' focus / event on %d rows",
                     pages, ticks, rows );
    else
        LOG_ERROR( "the batch 28 binding glue FAILED %d checks", failed );
    return failed == 0;
}

// c_ui_pause_menu (core/ui paused.hxx) against retail's pause menu (oracle case
// pause_menu: ctor 0x4BAE80, then the handlers, tick 0x4BB540 and draw 0x4BB5E0 on a
// script): each step's calls in order, the FSM state, every field after.
inline bool verify_ui_pause_menu( )
{
    int failed = 0, calls = 0;
    pause_menu_t gate;
    const golden_pmn_step_t* step = nullptr;
    std::string log;
    bool game = false;
    std::uint32_t key = 0;
    const auto say = [ &log, &calls ]( const std::string& what ) { log += ( log.empty( ) ? "" : ";" ) + what; ++calls; };
    const auto flag = [ &step ]( char c ) { return std::strchr( step->m_flags, c ) != nullptr; };
    const auto word = []( float f ) { std::uint32_t b; std::memcpy( &b, &f, 4 ); return b; };
    ui_pause_hooks_t hooks;
    hooks.m_play = [ & ]( const char* event ) { say( std::string( "play " ) + event ); };
    hooks.m_tick_paused = [ & ]( bool paused ) { say( paused ? "tick_pause" : "tick_resume" ); };
    hooks.m_category_paused = [ & ]( bool paused ) { say( paused ? "category 1" : "category 0" ); };
    hooks.m_game_paused = [ & ]( bool paused ) { game = paused; };
    hooks.m_analytics = [ & ]( const char* name, const std::string& screen ) { say( std::string( "analytics " ) + name + " " + screen ); };
    hooks.m_delta = [ & ] { float dt; std::memcpy( &dt, &step->m_dt, 4 ); return dt; };
    hooks.m_popup = [ & ] { return flag( 'p' ); };
    hooks.m_busy = [ & ] { return flag( 'b' ); };
    hooks.m_input = [ & ]( const char* name ) { say( std::string( "input " ) + name ); return flag( 'i' ); };
    hooks.m_touches = [ & ] { return flag( 't' ) ? 1 : 0; };
    hooks.m_input_type = [ & ] { return flag( 'y' ) ? 2 : 0; };
    hooks.m_open = [ & ]( const std::string& name )
    {
        say( "load " + name );
        if ( flag( 'n' ) )
            return false;
        say( "add" );
        say( "init" );
        say( "event OnProjectInitialized" );
        return true;
    };
    hooks.m_close = [ & ] { say( "release" ); say( "remove" ); say( "delete" ); };
    hooks.m_screen = [ ] { return true; };
    hooks.m_screen_tick = [ & ]( float dt, bool input )
    {
        char text[ 48 ];
        std::snprintf( text, sizeof( text ), "screen_tick 0x%08X %d", word( dt ), input ? 1 : 0 );
        say( text );
    };
    hooks.m_screen_draw = [ & ] { say( "screen_draw 0x3F800000" ); };
    hooks.m_background = [ & ]( std::uint32_t colour )
    {
        char text[ 96 ];
        std::snprintf( text, sizeof( text ), "solid 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X 0x%08X", colour, word( k_ui_pause_depth ), word( k_ui_pause_rect[ 0 ] ), word( k_ui_pause_rect[ 1 ] ),
                       word( k_ui_pause_rect[ 2 ] ), word( k_ui_pause_rect[ 3 ] ) );
        say( text );
    };
    hooks.m_sort_key = [ & ] { key = ui_pause_key( key ); };
    c_ui_pause_menu menu( gate, hooks, g_golden_pmn_colour );
    for ( std::size_t k = 0; k < g_golden_pmn_steps_count; ++k )
    {
        const golden_pmn_step_t& g = g_golden_pmn_steps[ k ];
        step = &g;
        log.clear( );
        const std::string op = g.m_op;
        if ( op == "tick" )
            menu.tick( );
        else if ( op == "draw" )
            menu.draw( );
        else if ( op == "pause" )
            pause_on_pause( gate );
        else if ( op == "resume" )
            menu.on_resume( );
        else if ( op == "set" )
            menu.on_set_screen( g.m_arg );
        else if ( op == "push" )
            menu.on_push_screen( g.m_arg );
        else if ( op == "pop" )
            menu.on_pop_screen( );
        else if ( op == "reload" )
            menu.on_reload_screen( );
        else if ( op == "settings" )
            menu.on_settings_command( g.m_arg );
        else if ( op == "allow" )
        {
            ui_params_t p;
            const std::uint32_t words[ 2 ] = { 3u, static_cast< std::uint32_t >( std::atoi( g.m_arg ) ) };
            std::memcpy( p.m_bytes, words, 8 );
            p.m_size = 8;
            pause_on_set_allow( gate, p );
        }
        const c_fsm& f = menu.fsm( );
        const bool same = log == g.m_log && f.m_current == g.m_state && gate.paused == ( g.m_paused != 0 ) && gate.allow == ( g.m_allow != 0 ) && gate.next == g.m_next && menu.loaded( ) == g.m_loaded &&
                          static_cast< int >( menu.stack( ).size( ) ) == g.m_stack && menu.project( ) == ( g.m_project != 0 ) && menu.depth( ) == g.m_depth && word( f.m_time ) == g.m_time &&
                          game == ( g.m_game != 0 ) && key == g.m_key;
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "ui pause menu: step %d %s %s: [%s] state %d depth %d next '%s' loaded '%s' time 0x%08X key 0x%08X; retail [%s] state %d depth %d next '%s' loaded '%s' time 0x%08X key 0x%08X",
                       static_cast< int >( k ), g.m_op, g.m_arg, log.c_str( ), f.m_current, menu.depth( ), gate.next.c_str( ), menu.loaded( ).c_str( ), word( f.m_time ), key, g.m_log, g.m_state, g.m_depth, g.m_next,
                       g.m_loaded, g.m_time, g.m_key );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "c_ui_pause_menu matches retail's pause menu 0x4BAE80 / 0x4BB540 / 0x4BB5E0 and its handlers on %d script steps: %d hook calls in order, the FSM and every field after each",
                     static_cast< int >( g_golden_pmn_steps_count ), calls );
    else
        LOG_ERROR( "the pause menu FAILED %d steps against retail's", failed );
    return failed == 0;
}

// c_ui_common_screen (core/ui common.hxx) against retail's VuCommonScreen slots 1..4
// 0x480A50 / 0x480AB0 / 0x480AE0 / 0x480B40 (oracle case common_screen) on every
// Screens/* and HUDs/* root: both states of the root's bit 2 and no project: the
// recorded calls of each slot in order, and the root's vtable.
inline bool verify_ui_common_screen( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    int failed = 0, slots = 0;
    float dt;
    std::memcpy( &dt, &g_golden_cmn_dt, 4 );
    for ( std::size_t k = 0; k < g_golden_cmn_runs_count; ++k )
    {
        const golden_cmn_run_t& g = g_golden_cmn_runs[ k ];
        const entity_t* root = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count && !root; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, g.m_project ) == 0 && !loaded.trees[ p ].empty( ) )
                root = loaded.trees[ p ][ 0 ].second;
        if ( !root || !root->m_ctor || root->m_ctor->m_vtable != g.m_vtable )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui common screen: %s's root is not retail's 0x%X", g.m_project, g.m_vtable );
            continue;
        }
        std::string log, slot;
        const auto say = [ &slot ]( const std::string& what ) { slot += ( slot.empty( ) ? "" : ";" ) + what; };
        ui_common_ops_t ops;
        ops.m_repository_add = [ & ] { say( "add" ); };
        ops.m_repository_remove = [ & ] { say( "remove" ); };
        ops.m_initialize = [ & ] { say( "init" ); say( "event OnProjectInitialized" ); };
        ops.m_release = [ & ] { say( "release" ); };
        ops.m_tick = [ & ]( float d )
        {
            std::uint32_t bits;
            std::memcpy( &bits, &d, 4 );
            char text[ 40 ];
            std::snprintf( text, sizeof( text ), "tick 0x%08X 15", bits );   // the 0xF: input allowed
            say( text );
        };
        ops.m_draw = [ & ] { say( "draw 0x3F800000" ); };
        c_ui_common_screen s( g.m_has_project ? root : nullptr, ops, g.m_bit != 0 );
        const auto end_slot = [ & ]( bool last ) { log += slot + ( last ? "" : "|" ); slot.clear( ); ++slots; };
        s.open( );
        end_slot( false );
        s.exit( );
        end_slot( false );
        s.tick( dt );
        end_slot( false );
        s.draw( );
        end_slot( true );
        if ( log != g.m_log && ++failed <= 8 )
            LOG_ERROR( "ui common screen: %s bit %d project %d: %s, retail %s", g.m_project, g.m_bit, g.m_has_project, log.c_str( ), g.m_log );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "c_ui_common_screen matches retail's VuCommonScreen 0x480A50 / 0x480AB0 / 0x480AE0 / 0x480B40 on %d runs over the Screens/* and HUDs/* roots: %d slots' calls "
                     "in order", static_cast< int >( g_golden_cmn_runs_count ), slots );
    else
        LOG_ERROR( "the VuCommonScreen slots FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_champ_draw (core/ui champ.hxx) against retail's VuChampionshipEntity 0x451CA0
// (oracle case championship_draw) on Screens/Championship: OUR loaded properties
// against retail's bytes, then hand-built standings through the real entity: every
// command's words and record byte for byte in ONE sort, the block's array after,
// each StringDB call's id.
inline bool verify_ui_championship_draw( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    const auto parse = []( const unsigned* words, int bytes )
    {
        return c_reader( std::vector< std::uint8_t >( reinterpret_cast< const std::uint8_t* >( words ), reinterpret_cast< const std::uint8_t* >( words ) + bytes ) ).parse( );
    };
    const auto text = []( const char* hexed ) { const std::vector< std::uint8_t > v = ui_unhex( hexed ); return std::string( v.begin( ), v.end( ) ); };
    int failed = 0, commands = 0, calls = 0, runs = 0;
    for ( std::size_t k = 0; k < g_golden_ch_tables_count; ++k )
    {
        const golden_ch_table_t& g = g_golden_ch_tables[ k ];
        const std::vector< std::uint8_t > raw = ui_unhex( g.m_fields );
        const auto at = [ &raw ]( int offset ) { return raw.data( ) + ( offset - 0x60 ); };
        const entity_t* have = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, g.m_project ) == 0 && g.m_index >= 0 && static_cast< std::size_t >( g.m_index ) < loaded.trees[ p ].size( ) )
                have = loaded.trees[ p ][ static_cast< std::size_t >( g.m_index ) ].second;
        ui_champ_t base;
        ui_results_table_t& t = base.m_table;
        std::memcpy( t.m_scale, at( 0x64 ), 8 );
        std::memcpy( t.m_pos, at( 0x6C ), 8 );
        std::memcpy( &t.m_highlight, at( 0x74 ), 4 );
        std::memcpy( base.m_place, at( 0x78 ), 16 );
        std::memcpy( base.m_place_font.m_params, at( 0xA8 ), 60 );
        std::memcpy( base.m_points, at( 0xEC ), 16 );
        std::memcpy( base.m_points_font.m_params, at( 0x11C ), 60 );
        std::memcpy( base.m_name, at( 0x160 ), 16 );
        std::memcpy( base.m_name_font.m_params, at( 0x190 ), 60 );
        std::memcpy( base.m_boat, at( 0x1D4 ), 16 );
        std::memcpy( base.m_boat_font.m_params, at( 0x204 ), 60 );
        std::memcpy( &t.m_width, at( 0x248 ), 4 );
        std::memcpy( &t.m_spacing, at( 0x24C ), 4 );
        std::memcpy( &t.m_header, at( 0x250 ), 4 );
        std::memcpy( &t.m_inactive, at( 0x254 ), 4 );
        std::memcpy( &t.m_active, at( 0x258 ), 4 );
        std::memcpy( &t.m_player, at( 0x25C ), 4 );
        std::memcpy( &t.m_depth, &g.m_depth, 4 );
        ui_champ_t ours = base;
        bool same = have != nullptr;
        if ( have )
        {
            const auto take = [ & ]( const char* name, void* target, std::size_t bytes )
            {
                std::uint8_t was[ 16 ];
                std::memcpy( was, target, bytes );
                ui_frame_take( *have, name, target, bytes );
                if ( std::memcmp( was, target, bytes ) != 0 )
                    same = false;
            };
            ui_results_table_t& o = ours.m_table;
            take( "Screen Scale", o.m_scale, 8 );
            take( "Position X", &o.m_pos[ 0 ], 4 );
            take( "Position Y", &o.m_pos[ 1 ], 4 );
            take( "Text Highlight Color", &o.m_highlight, 4 );
            static const char* const k_cells[ 4 ] = { "PlaceText", "PointsText", "NameText", "BoatText" };
            static const char* const k_fonts[ 4 ] = { "PlaceFont", "PointsFont", "NameFont", "BoatFont" };
            float* const cells[ 4 ] = { ours.m_place, ours.m_points, ours.m_name, ours.m_boat };
            const ui_results_font_t* const fonts[ 4 ] = { &base.m_place_font, &base.m_points_font, &base.m_name_font, &base.m_boat_font };
            static const char* const k_xywh[ 4 ] = { "X", "Y", "Width", "Height" };
            for ( int c = 0; c < 4; ++c )
            {
                for ( int j = 0; j < 4; ++j )
                    take( ( std::string( k_cells[ c ] ) + k_xywh[ j ] ).c_str( ), &cells[ c ][ j ], 4 );
                std::uint8_t params[ 60 ] = { };
                std::string asset;
                const ui_slot_t* s = ui_property_value( *have, k_fonts[ c ] );
                ui_font_params( ui_font_db( 0 ), s ? s->m_text : std::string( ), params, asset );
                same = same && std::memcmp( params, fonts[ c ]->m_params, 41 ) == 0;
            }
            take( "Width", &o.m_width, 4 );
            take( "VerticalSpacing", &o.m_spacing, 4 );
            take( "HeaderColor", &o.m_header, 4 );
            take( "InactiveColor", &o.m_inactive, 4 );
            take( "ActiveColor", &o.m_active, 4 );
            take( "PlayerColor", &o.m_player, 4 );
            const float depth = ui_frame_depth( *have );
            same = same && std::memcmp( &depth, &g.m_depth, 4 ) == 0;
        }
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui championship: entity %d: OUR loaded properties differ from retail's", g.m_index );
            continue;
        }
        const value_t boatdb = parse( g.m_boatdb, g.m_boatdb_bytes );
        for ( int r = 0; r < g.m_runs; ++r )
        {
            const golden_ch_run_t& v = g_golden_ch_runs[ g.m_first + r ];
            const value_t doc = parse( v.m_doc, v.m_doc_bytes );
            ui_champ_t c = base;
            ui_results_table_t& o = c.m_table;
            o.m_active_rows = v.m_active;
            o.m_size = v.m_size;
            o.m_capacity = 16;
            o.m_pads.assign( 16, v.m_seed );
            const std::vector< std::uint8_t > block = ui_unhex( v.m_block ), matrix = ui_unhex( v.m_matrix );
            std::uint32_t def;
            std::memcpy( &def, block.data( ) + 0x9C, 4 );
            ui_results_font_t* const fonts[ 4 ] = { &c.m_place_font, &c.m_points_font, &c.m_name_font, &c.m_boat_font };
            for ( int j = 0; j < 4; ++j )
                fonts[ j ]->m_font = v.m_fonts[ j ] == 'a' ? g_golden_ch_fonts[ 0 ] : def ? g_golden_ch_fonts[ 1 ] : 0u;
            ui_font_draw_env_t env;
            std::memcpy( env.m_matrix, matrix.data( ), 64 );
            std::memcpy( &env.m_top, &v.m_top, 4 );
            for ( int m = 0; m < 2; ++m )
            {
                env.m_material[ m ] = g_golden_ch_font_materials[ m ];
                env.m_material_key[ m ] = g_golden_ch_font_materials[ 2 + m ];
            }
            env.m_texture_material = g_golden_ch_materials[ 0 ];
            env.m_texture_material_key = g_golden_ch_materials[ 1 ];
            env.m_rect_material = g_golden_ch_materials[ 2 ];
            env.m_rect_material_key = g_golden_ch_materials[ 3 ];
            int at_call = 0;
            bool hooks_same = true;
            ui_champ_inputs_t in;
            in.m_rows = &member( doc, "rows" );
            in.m_boatdb = &boatdb;
            in.m_string = [ & ]( const std::string& id )
            {
                if ( at_call >= v.m_calls )
                {
                    hooks_same = false;
                    return std::string( );
                }
                const golden_ch_call_t& call = g_golden_ch_calls[ v.m_first_call + at_call++ ];
                ++calls;
                if ( text( call.m_id ) != id )
                    hooks_same = false;
                return text( call.m_text );
            };
            sort.m_index = v.m_buffer;
            sort.m_key_lo = v.m_key_in[ 0 ];
            sort.m_key_hi = v.m_key_in[ 1 ];
            std::vector< ui_gfx_command_t >& list = sort.m_commands[ v.m_buffer ];
            const std::size_t before = list.size( );
            const bool known = ui_champ_draw( sort, env, c, in );
            bool ok = known && hooks_same && at_call == v.m_calls && list.size( ) - before == static_cast< std::size_t >( v.m_commands ) && sort.m_key_lo == v.m_key[ 0 ] &&
                      sort.m_key_hi == v.m_key[ 1 ];
            for ( int j = 0; ok && j < 16; ++j )
                ok = static_cast< unsigned >( o.m_pads[ static_cast< std::size_t >( j ) ] ) == v.m_pads[ j ];
            for ( int n = 0; ok && n < v.m_commands; ++n )
            {
                const golden_txt_command_t& want = g_golden_ch_commands[ v.m_first + n ];
                const ui_gfx_command_t& got = list[ before + static_cast< std::size_t >( n ) ];
                const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
                const auto& memory = sort.m_buffers[ v.m_buffer ].m_memory;
                ok = std::memcmp( got.m_words, want.m_words, sizeof( got.m_words ) ) == 0 && got.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                     std::memcmp( memory.data( ) + got.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
                ++commands;
            }
            ++runs;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui championship: run %d gave %d commands / %d string calls, retail %d / %d - or a word, record byte, array entry or id differs", r,
                           static_cast< int >( list.size( ) - before ), at_call, v.m_commands, v.m_calls );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_champ_draw matches retail's VuChampionshipEntity 0x451CA0 (0x4527A0, 0x443C90) on %d table: %d runs, %d commands and records byte for byte, %d StringDB calls",
                     static_cast< int >( g_golden_ch_tables_count ), runs, commands, calls );
    else
        LOG_ERROR( "the UI championship table FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_minimap_layout_draw (core/ui map.hxx) against retail's 0x427FB0 (oracle case
// minimap_layout): OUR loaded scale / rect against retail's bytes, the rect's
// command and record, the alpha the draw is run at.
inline bool verify_ui_minimap_layout( )
{
    ui_gfx_sort_t sort;
    sort.m_buffers[ 0 ].m_capacity = 0x300000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    sort.m_buffers[ 1 ].m_capacity = 0x100000;
    for ( auto& b : sort.m_buffers )
        b.m_memory.resize( static_cast< std::size_t >( b.m_capacity ) );
    int failed = 0, runs = 0, commands = 0;
    for ( std::size_t k = 0; k < g_golden_mlo_entities_count; ++k )
    {
        const golden_mlo_entity_t& g = g_golden_mlo_entities[ k ];
        const entity_t* e = ui_lv_entity( g.m_project, g.m_index );
        const std::vector< std::uint8_t > raw = ui_unhex( g.m_fields );
        ui_minimap_t loaded;
        bool same = e && raw.size( ) == 0x18;
        if ( same )
        {
            same = ui_frame_take( *e, "Screen Scale", loaded.m_screen_scale, 8 ) && same;
            static const char* const k_names[ 4 ] = { "X", "Y", "Width", "Height" };
            for ( int j = 0; j < 4; ++j )
                ui_frame_take( *e, k_names[ j ], &loaded.m_rect[ j ], 4 );
            same = same && std::memcmp( loaded.m_screen_scale, raw.data( ), 8 ) == 0 && std::memcmp( loaded.m_rect, raw.data( ) + 8, 16 ) == 0;
            loaded.m_depth = ui_frame_depth( *e );
            same = same && ui_lv_bits( loaded.m_depth ) == g.m_depth;
        }
        if ( !same )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "ui minimap layout: %s entity %d: OUR loaded fields differ from retail's", g.m_project, g.m_index );
            continue;
        }
        for ( int r = 0; r < g.m_runs; ++r )
        {
            const golden_mlo_run_t& v = g_golden_mlo_runs[ g.m_first + r ];
            const std::vector< std::uint8_t > fields = ui_unhex( v.m_fields ), matrix = ui_unhex( v.m_matrix );
            ui_minimap_t m;
            std::memcpy( m.m_screen_scale, fields.data( ), 8 );
            std::memcpy( m.m_rect, fields.data( ) + 8, 16 );
            std::memcpy( &m.m_depth, &v.m_depth, 4 );
            ui_font_draw_env_t env;
            std::memcpy( env.m_matrix, matrix.data( ), 64 );
            env.m_rect_material = g_golden_mlo_rect_material[ 0 ];
            env.m_rect_material_key = g_golden_mlo_rect_material[ 1 ];
            sort.m_index = v.m_buffer;
            sort.m_key_lo = v.m_key_lo;
            sort.m_key_hi = v.m_key_hi;
            std::vector< ui_gfx_command_t >& list = sort.m_commands[ v.m_buffer ];
            const std::size_t before = list.size( );
            std::string calls;
            ui_minimap_layout_draw( sort, env, m, static_cast< std::uint8_t >( v.m_byte ), [ & ]( float a )
            {
                char t[ 16 ];
                std::snprintf( t, sizeof t, "0x%08X", ui_lv_bits( a ) );
                calls += t;
            } );
            bool ok = calls == v.m_calls && list.size( ) - before == static_cast< std::size_t >( v.m_commands );
            for ( int c = 0; ok && c < v.m_commands; ++c )
            {
                const golden_txt_command_t& want = g_golden_mlo_commands[ v.m_first + c ];
                const ui_gfx_command_t& got = list[ before + static_cast< std::size_t >( c ) ];
                const std::vector< std::uint8_t > record = ui_unhex( want.m_record );
                const auto& memory = sort.m_buffers[ v.m_buffer ].m_memory;
                ok = std::memcmp( got.m_words, want.m_words, sizeof( got.m_words ) ) == 0 && got.m_words[ 3 ] + record.size( ) <= memory.size( ) &&
                     std::memcmp( memory.data( ) + got.m_words[ 3 ], record.data( ), record.size( ) ) == 0;
                ++commands;
            }
            ++runs;
            if ( !ok && ++failed <= 8 )
                LOG_ERROR( "ui minimap layout: entity %d run %d: %d commands (retail %d), draw alpha %s (retail %s)", g.m_index, r, static_cast< int >( list.size( ) - before ), v.m_commands,
                           calls.c_str( ), v.m_calls );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_minimap_layout_draw matches retail's 0x427FB0 on %d entities: %d runs, %d commands and records byte for byte", static_cast< int >( g_golden_mlo_entities_count ), runs, commands );
    else
        LOG_ERROR( "the UI minimap layout draw FAILED %d checks against retail's", failed );
    return failed == 0;
}

// ui_remap_on_focus / _set_focus / _hide / _tick / _event / _touch_* / _decision /
// _box_result (core/ui remap.hxx) against retail's row runtime (oracle case
// remap_logic): one script per row kind on the real entity; after each step the row's
// bytes, the point, hover, select, box, +0x204, the axes, the two auto settings
// and the log of the hook calls with their arguments.
inline bool verify_ui_remap_logic( )
{
    int failed = 0, steps = 0, logged = 0;
    for ( std::size_t k = 0; k < g_golden_rl_rows_count; ++k )
    {
        const golden_rl_row_t& g = g_golden_rl_rows[ k ];
        const std::vector< std::uint8_t > raw = ui_unhex( g.m_fields ), block = ui_unhex( g.m_block );
        golden_rm_row_t row = { };
        row.m_type = g.m_type;
        row.m_channel = g.m_channel;
        row.m_label = g.m_label;
        row.m_config = g.m_config;
        ui_remap_t r = ui_rm_of( raw, row, 0u );
        r.m_selected = raw[ 0x1F0 - 0x60 ];
        std::memcpy( &r.m_hover, raw.data( ) + ( 0x22C - 0x60 ), 4 );
        float inverse[ 16 ];
        std::memcpy( inverse, block.data( ) + 0x54, 64 );
        ui_remap_runtime_t s;
        s.m_axes.assign( 3, 0.0f );   // retail's array exists before the first opening
        float axes[ 3 ] = { 0.0f, 0.0f, 0.0f };
        std::uint32_t bits = 0;
        std::vector< int > keys;
        std::uint8_t automatic[ 2 ] = { 1, 1 };
        std::string log;
        const auto note = [ & ]( const std::string& t )
        {
            log += ( log.empty( ) ? "" : ";" ) + t;
            ++logged;
        };
        ui_remap_hooks_t h;
        h.m_set_focus = [ & ]
        {
            std::uint32_t hash = 0x811C9DC5u;
            for ( const char* c = "OnUISetFocus"; *c; ++c )
                hash = ( hash ^ static_cast< std::uint8_t >( *c ) ) * 0x01000193u;
            char t[ 32 ];
            std::snprintf( t, sizeof t, "broadcast 0x%08X", hash );
            note( t );
        };
        h.m_nav = [ & ]( const char* plug, std::int32_t arg )
        {
            note( std::string( "find " ) + plug );
            note( "nav " + std::to_string( arg ) );
            note( "sound Global/UI_and_Menu/menu_browse" );
        };
        h.m_sound = [ & ] { note( std::string( "sound " ) + g.m_sfx ); };
        h.m_open_box = [ & ]( bool can_auto )
        {
            note( std::string( "box " ) + g.m_label + "|Control_Mapping_MB_Body|" + ( can_auto ? "MessageBox_Auto" : "" ) + "|MessageBox_Cancel" );
        };
        h.m_channels = [ ] { return 3; };
        h.m_axis = [ & ]( int i ) { return axes[ i ]; };
        h.m_button_bits = [ & ] { return bits; };
        h.m_key = [ & ]( int key )
        {
            for ( int d : keys )
                if ( d == key )
                    return true;
            return false;
        };
        h.m_bind = [ & ]( int slot, std::int32_t config, const std::string& channel, int kind, int index )
        {
            note( "bind " + std::to_string( slot ) + " " + std::to_string( config ) + " " + channel + " " + std::to_string( kind ) + " " + std::to_string( index ) );
        };
        if ( g.m_vtable == 0x73D45Cu )
            h.m_set_auto = [ & ]( std::uint8_t v )
            {
                if ( g.m_config == 0 || g.m_config == 1 )
                    automatic[ g.m_config ] = v;
            };
        for ( int n = 0; n < g.m_steps; ++n )
        {
            const golden_rl_step_t& t = g_golden_rl_steps[ g.m_first + n ];
            log.clear( );
            float f[ 3 ];
            std::memcpy( f, t.m_f, 12 );
            switch ( t.m_op )
            {
            case 0: ui_remap_set_focus( s, h ); break;
            case 1: ui_remap_tick( r, s, h ); break;
            case 2: ui_remap_on_focus( s, t.m_i != 0 ); break;
            case 3: ui_remap_event( r, s, t.m_text, t.m_i, h ); break;
            case 4: ui_remap_decision( r, s, h ); break;
            case 5:
            {
                std::memcpy( axes, f, 12 );
                bits = t.m_bits;
                keys.clear( );
                for ( const char* c = t.m_keys; *c; )
                {
                    char* end = nullptr;
                    keys.push_back( static_cast< int >( std::strtol( c, &end, 10 ) ) );
                    c = *end ? end + 1 : end;
                }
                break;
            }
            case 6: ui_remap_touch_down( r, s, inverse, f ); break;
            case 7: ui_remap_touch_point( s, inverse, f ); break;
            case 8: ui_remap_touch_up( r, s ); break;
            case 9: ui_remap_box_result( s, t.m_i, h ); break;
            default: ui_remap_hide( r, s ); break;
            }
            ++steps;
            char state[ 16 ];
            std::snprintf( state, sizeof state, "%02x%02x%02x%02x%02x%02x", r.m_visible, r.m_selected, s.m_focus, s.m_pressed, s.m_down, s.m_released );
            std::string axes_text;
            if ( !s.m_axes.empty( ) )
                for ( int a = 0; a < 3; ++a )
                {
                    char v[ 16 ];
                    std::snprintf( v, sizeof v, "0x%08X", ui_lv_bits( s.m_axes[ static_cast< std::size_t >( a ) ] ) );
                    axes_text += ( a ? ";" : "" ) + std::string( v );
                }
            const bool same = t.m_state == std::string( state ) && ui_lv_bits( s.m_point[ 0 ] ) == t.m_point[ 0 ] && ui_lv_bits( s.m_point[ 1 ] ) == t.m_point[ 1 ] && r.m_hover == t.m_hover && s.m_select == t.m_select &&
                              ( s.m_box ? 1 : 0 ) == t.m_box && s.m_buttons == t.m_buttons && axes_text == t.m_axes && automatic[ 0 ] == t.m_auto[ 0 ] && automatic[ 1 ] == t.m_auto[ 1 ] && log == t.m_log;
            if ( !same && ++failed <= 8 )
                LOG_ERROR( "ui remap logic: row %d step %d (op %d): %s hover %d select %d box %d buttons 0x%08X [%s] {%s} (retail %s %d %d %d 0x%08X [%s] {%s})", static_cast< int >( k ), n, t.m_op, state, r.m_hover,
                           s.m_select, s.m_box ? 1 : 0, s.m_buttons, axes_text.c_str( ), log.c_str( ), t.m_state, t.m_hover, t.m_select, t.m_box, t.m_buttons, t.m_axes, t.m_log );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "the remapping row's runtime matches retail's (0x4613A0, 0x4613F0, 0x4615A0, 0x4612E0, 0x461380, 0x461760, 0x4617F0, 0x461800, 0x4623D0, 0x4625C0, 0x4627F0, 0x461900) on %d rows: %d steps, %d hook calls",
                     static_cast< int >( g_golden_rl_rows_count ), steps, logged );
    else
        LOG_ERROR( "the UI remapping logic FAILED %d checks against retail's", failed );
    return failed == 0;
}

// Batch 30, ROUTING only (the slots are compared with retail by verify_ui_championship_load, the award by verify_ui_award_screen): a bound
// Screens/Championship reaches 0x451370 / 0x451540 through the game slots, its OnUITick and the inputs IsBusy / Exit / PlayNext reach
// standings.hxx, the frame's m_champ draws the standings the load built; ui_game_screens_t::award_open builds the second VuAwardScreen
// on Screens/AwardChampionship and its slots run through ui_champ_award_bind.
inline bool verify_ui_champ_bind( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    entity_t* root = nullptr, * award_root = nullptr, * table = nullptr;
    for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
    {
        if ( loaded.trees[ p ].empty( ) )
            continue;
        if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, "Screens/Championship" ) == 0 )
        {
            root = const_cast< entity_t* >( loaded.trees[ p ][ 0 ].second );
            for ( const auto& leaf : loaded.trees[ p ] )
                if ( leaf.second->m_ctor && leaf.second->m_ctor->m_vtable == k_ui_champ_vtable )
                    table = const_cast< entity_t* >( leaf.second );
        }
        if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, "Screens/Award" ) == 0 )
            award_root = const_cast< entity_t* >( loaded.trees[ p ][ 0 ].second );
    }
    if ( !root || !table )
    {
        LOG_ERROR( "ui champ bind: Screens/Championship is not in the loaded projects" );
        return false;
    }
    value_t data;
    ui_json_set_string( ui_json_child( ui_json_child( data, "GameConfig" ), "Championship" ), "Nothing" );
    const value_t boat_db = ui_gd_document( 0 );
    value_t champ_db;
    value_t& entry = ui_json_child( ui_json_child( champ_db, "VuDBAsset" ), "Nothing" );
    ui_json_at( ui_json_child( entry, "Events" ), 0 );
    fleet_random_t rng;
    std::string log;
    ui_champ_env_t env;
    env.m_root = &data;
    env.m_boat_db = &boat_db;
    env.m_champ_db = &champ_db;
    env.m_random = &rng;
    env.m_string = [ ]( const char* id ) { return std::string( id ); };
    env.m_load = [ & ]( value_t& ) { log += "load;"; };
    env.m_save = [ & ]( const value_t& ) { log += "save;"; };
    env.m_broadcast = [ & ]( const char* e, const ui_params_t& ) { log += e; log += ";"; };
    ui_game_screens_t screens;
    screens.m_load = [ & ]( const std::string& project ) -> std::unique_ptr< entity_t > { log += project + ";"; (void) award_root; return nullptr; };
    ui_champ_award_bind( env, screens );
    int ticks = 0;
    env.m_value = [ ]( const std::string&, std::int32_t ) { return 7; };
    ui_bind_hooks_t hooks;
    hooks.m_root = &data;
    hooks.m_champ = &env;
    int failed = 0;
    {
        c_ui_binding binding( *root, hooks );
        binding.project_initialize( );
        ui_champ_state_t* state = binding.champ( *table );
        failed += state && state->m_rows.list.size( ) == 16u ? 0 : 1;
        const ui_ret_t busy = binding.input( *table, "IsBusy", ui_params_t( ) );
        failed += busy.m_words[ 0 ] == 3u && busy.m_words[ 1 ] == 0u ? 0 : 1;
        ui_json_set_int( ui_json_child( state->m_data, "Round" ), 1 );
        binding.input( *table, "Exit", ui_params_t( ) );
        failed += state->m_award && screens.m_award ? 0 : 1;
        const ui_ret_t busy2 = binding.input( *table, "IsBusy", ui_params_t( ) );
        failed += busy2.m_words[ 1 ] == 1u ? 0 : 1;
        ui_params_t tick;
        ui_params_add( tick, 2, 0x3D000000u );
        binding.post( fsm_hash( "OnUITick" ), tick );
        ++ticks;
        failed += log.find( "Screens/AwardChampionship" ) != std::string::npos ? 0 : 1;
        binding.project_release( );
        failed += screens.m_award ? 0 : 1;
    }
    failed += log.find( "load;" ) == 0 && log.find( "save;" ) != std::string::npos ? 0 : 1;
    if ( failed == 0 )
        LOG_SUCCESS( "VuChampionshipEntity's binding: slots 6 / 7, OnUITick, IsBusy / Exit / PlayNext reach standings.hxx; ui_game_screens_t::award_open builds the second VuAwardScreen; ROUTING only (%d tick)", ticks );
    else
        LOG_ERROR( "ui champ bind: %d routing tests failed (log %s)", failed, log.c_str( ) );
    return failed == 0;
}

// c_ui_award_screen (core/ui award.hxx) against retail's VuAwardScreen (oracle case award_screen: ctor 0x47ECC0, then per
// run 0x47F350 and a script of ticks, draws, the listeners and the exit): each step's calls in order (the events' VuParams
// bytes included), the state, every field after.
inline bool verify_ui_award_screen( )
{
    int failed = 0, compared = 0;
    const auto hex = []( const ui_params_t& p )
    {
        std::string out;
        char two[ 4 ];
        for ( int k = 0; k < p.m_size; ++k )
        {
            std::snprintf( two, sizeof( two ), "%02X", p.m_bytes[ k ] );
            out += two;
        }
        return out;
    };
    const ui_loaded_t& loaded = ui_loaded( );
    const entity_t* root = nullptr;
    for ( std::size_t p = 0; p < g_golden_ui_projects_count && !root; ++p )
        if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, "Screens/Award" ) == 0 && !loaded.trees[ p ].empty( ) )
            root = loaded.trees[ p ][ 0 ].second;
    if ( !root || !ui_common_is_screen( *root ) )
    {
        LOG_ERROR( "VuAwardScreen: Screens/Award's root is not a screen" );
        return false;
    }
    std::size_t unlock_next = 0;
    for ( std::size_t r = 0; r < g_golden_aws_runs_count; ++r )
    {
        const golden_aws_run_t& run = g_golden_aws_runs[ r ];
        const golden_aws_step_t* step = nullptr;
        std::string log;
        bool game = false;
        int queue = -1, index = 0;
        const auto say = [ &log ]( const std::string& what ) { log += ( log.empty( ) ? "" : ";" ) + what; };
        const auto flag = [ &step ]( char c ) { return step && std::strchr( step->m_flags, c ) != nullptr; };
        ui_common_ops_t ops;
        ops.m_repository_add = [ & ] { say( "add" ); };
        ops.m_repository_remove = [ & ] { say( "remove" ); };
        ops.m_initialize = [ & ] { say( "init" ); say( "event OnProjectInitialized " ); };
        ops.m_release = [ & ] { say( "release" ); };
        ops.m_tick = [ & ]( float dt )
        {
            std::uint32_t b;
            std::memcpy( &b, &dt, 4 );
            char text[ 48 ];
            std::snprintf( text, sizeof( text ), "screen_tick 0x%08X 15", b );
            say( text );
        };
        ops.m_draw = [ & ] { say( "screen_draw 0x3F800000" ); };
        ui_award_hooks_t hooks;
        hooks.m_event = [ & ]( const char* name, const ui_params_t& p ) { say( std::string( "event " ) + name + " " + hex( p ) ); };
        hooks.m_screen_busy = [ & ]( bool busy )
        {
            char text[ 48 ];
            std::snprintf( text, sizeof( text ), "broadcast 0x%08X 03000000%s", fsm_hash( "OnAwardScreenBusy" ), busy ? "01" : "00" );
            say( text );
        };
        hooks.m_play = [ & ]( const char* event ) { say( std::string( "play " ) + event ); };
        hooks.m_input = [ & ]( const char* name ) { say( std::string( "input " ) + name ); return flag( 'i' ); };
        hooks.m_touches = [ & ] { return flag( 't' ) ? 0x7FFF : 1; };
        hooks.m_credits_add = [ & ]( std::int32_t delta, std::int32_t f ) { say( "credits_add " + std::to_string( delta ) + " " + std::to_string( f ) ); };
        hooks.m_credits = [ & ] { say( "credits" ); return 1000 + index; };
        hooks.m_credits_room = [ & ]( std::int32_t& room )
        {
            say( "room" );
            if ( !flag( 'c' ) )
                return false;
            room = 37 + 11 * index;
            return true;
        };
        hooks.m_unlock = [ & ]( const std::string& name )
        {
            say( "unlock " + name );
            if ( flag( 'u' ) )
                queue = static_cast< int >( unlock_next++ % g_golden_aws_unlock_count );
        };
        hooks.m_analytics = [ & ]( const char* name, std::int64_t value )
        {
            say( std::string( "analytics " ) + name + " " + std::to_string( static_cast< std::uint32_t >( value ) ) + " " + std::to_string( static_cast< std::uint32_t >( value >> 32 ) ) );
        };
        hooks.m_game_busy = [ & ]( bool busy ) { game = busy; };
        c_ui_award_screen award( root, ops, hooks );
        const auto unlock = [ & ]( int k )
        {
            const golden_aws_unlock_t& u = g_golden_aws_unlocks[ k ];
            award.on_unlock( { u.m_string_id, u.m_image, u.m_type, u.m_mode } );
        };
        std::vector< std::string > names;
        for ( const char* at = run.m_names; *at; )
        {
            const char* end = std::strchr( at, '|' );
            names.emplace_back( at, end ? static_cast< std::size_t >( end - at ) : std::strlen( at ) );
            at = end ? end + 1 : at + names.back( ).size( );
        }
        for ( int k = 0; k < run.m_steps; ++k )
        {
            const golden_aws_step_t& g = g_golden_aws_steps[ run.m_first + k ];
            step = &g;
            index = k - 1;
            log.clear( );
            queue = -1;
            const std::string op = g.m_op;
            float dt;
            std::memcpy( &dt, &g.m_dt, 4 );
            int ret = 0;
            if ( op == "start" )
                award.start( run.m_tier, run.m_class, run.m_value, names );
            else if ( op == "tick" )
            {
                award.tick( dt );
                if ( queue >= 0 )
                    unlock( queue );
            }
            else if ( op == "draw" )
                award.draw( );
            else if ( op == "done" )
                ret = award.done( ) ? 1 : 0;
            else if ( op == "next" )
                award.on_flow_next( );
            else if ( op == "queue" )
                unlock( g.m_arg );
            else if ( op == "team" )
                award.trophy_team( g.m_arg );
            else if ( op == "exit" )
                award.exit( );
            std::uint32_t elapsed, time;
            const float e = award.elapsed( ), t = award.fsm( ).m_time;
            std::memcpy( &elapsed, &e, 4 );
            std::memcpy( &time, &t, 4 );
            ++compared;
            if ( log != g.m_log || award.fsm( ).m_current != g.m_state || ret != g.m_ret || award.earned( ) != g.m_earned || elapsed != g.m_elapsed || award.feat( ) != g.m_feat ||
                 static_cast< int >( award.queued( ) ) != g.m_queued || ( award.select( ) ? 1 : 0 ) != g.m_select || time != g.m_time || ( game ? 1 : 0 ) != g.m_busy )
            {
                if ( ++failed <= 4 )
                    LOG_ERROR( "award_screen run %d step %d %s: state %d (%d) earned %d (%d)\n  ours   %s\n  retail %s", static_cast< int >( r ), k, g.m_op, award.fsm( ).m_current, g.m_state,
                               award.earned( ), g.m_earned, log.c_str( ), g.m_log );
            }
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuAwardScreen (ctor 0x47ECC0, 0x47F350, 0x47F580, 0x47F6D0, 0x47F720, 0x47F820, 0x47FCD0, 0x47FE40, 0x47FE90, 0x47F4C0) matches retail on %d steps of %d runs", compared,
                     static_cast< int >( g_golden_aws_runs_count ) );
    else
        LOG_ERROR( "VuAwardScreen FAILED on %d of %d steps", failed, compared );
    return failed == 0;
}

// ui_font_glyph_width / ui_font_wrap (core/ui glyphs.hxx, over water's wrap_line) against retail's 0x537EF0 / 0x537DB0 (oracle
// case font_wrap; 0x559D80 / 0x559A40 / 0x559B50 / 0x5A88F0 live, only the code map's find hooked): the width per code, the wrapped
// text byte for byte per text x width x flags. Hex strings: bad UTF-8 stays exact.
inline bool verify_ui_font_wrap( )
{
    ui_font_glyphs_t f;
    for ( std::size_t k = 0; k < g_golden_fwr_glyphs_count; ++k )
    {
        const golden_fwr_glyph_t& g = g_golden_fwr_glyphs[ k ];
        float advance;
        std::memcpy( &advance, &g.m_advance, 4 );
        f.add( g.m_code, static_cast< std::uint32_t >( k ), advance );
        if ( g.m_index == 0xFFFFu )
            f.m_index[ g.m_code ] = 0xFFFFu;
    }
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_fwr_widths_count; ++k )
    {
        const golden_fwr_width_t& g = g_golden_fwr_widths[ k ];
        if ( ui_font_glyph_width( f, static_cast< std::uint16_t >( g.m_code ) ) != g.m_width && ++failed <= 4 )
            LOG_ERROR( "font width 0x%X: %u, retail %u", g.m_code, ui_font_glyph_width( f, static_cast< std::uint16_t >( g.m_code ) ), g.m_width );
    }
    for ( std::size_t k = 0; k < g_golden_fwr_wraps_count; ++k )
    {
        const golden_fwr_wrap_t& g = g_golden_fwr_wraps[ k ];
        const std::vector< std::uint8_t > text = ui_unhex( g.m_text ), want = ui_unhex( g.m_out );
        std::string input( text.begin( ), text.end( ) );
        float width;
        std::memcpy( &width, &g.m_width, 4 );
        const std::string out = ui_font_wrap( f, input.c_str( ), width, g.m_flags );
        if ( out != std::string( want.begin( ), want.end( ) ) && ++failed <= 4 )
            LOG_ERROR( "font wrap run %d: [%s], retail [%s]", static_cast< int >( k ), out.c_str( ), std::string( want.begin( ), want.end( ) ).c_str( ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "The font's width 0x537EF0 and wrap 0x537DB0 match retail on %d codes, %d wraps", static_cast< int >( g_golden_fwr_widths_count ), static_cast< int >( g_golden_fwr_wraps_count ) );
    else
        LOG_ERROR( "The font's wrap FAILED on %d", failed );
    return failed == 0;
}

// ROUTING, no retail frame vector (case slider_draw holds the draw itself): the four sliders' frame kind reaches the bound c_ui_slider's value, flags and fields, on the
// real Screens/SettingsAudio tree; the commands equal the direct draw's.
inline bool verify_ui_slider_frame( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    entity_t* root = nullptr;
    for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
        if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, "Screens/SettingsAudio" ) == 0 && !loaded.trees[ p ].empty( ) )
            root = const_cast< entity_t* >( loaded.trees[ p ][ 0 ].second );
    if ( !root )
    {
        LOG_ERROR( "slider frame: no Screens/SettingsAudio" );
        return false;
    }
    ui_settings_t settings;
    settings.m_effect = 0.625f;
    settings.m_music = 0.25f;
    ui_touch_device_t device;
    static const float k_inverse[ 16 ] = { 1.0f, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    ui_bind_hooks_t hooks;
    hooks.m_touch_device = &device;
    hooks.m_inverse = k_inverse;
    hooks.m_font_db = &ui_font_db( 0 );
    hooks.m_settings = &settings;
    c_ui_binding binding( *root, hooks );
    const ui_texture_t frame_texture{ 0x1111u, 256, 128 }, selected_texture{ 0x2222u, 128, 64 }, bar_texture{ 0x3333u, 64, 32 };
    ui_screen_env_t env;
    env.m_fontdb = &ui_font_db( 0 );
    env.m_font_asset = []( const std::string& ) { return 0x0F0E0D0Cu; };
    env.m_string6 = []( const std::string& id ) { return id + "~"; };
    env.m_slider = [ & ]( const entity_t& )
    {
        ui_slider_draw_inputs_t i;
        i.m_frame = &frame_texture;
        i.m_selected = &selected_texture;
        i.m_bar = &bar_texture;
        return i;
    };
    ui_frame_inputs_t in = ui_screen_inputs( binding, env );
    ui_gfx_sort_t sort, direct;
    ui_lv_sort( sort );
    ui_lv_sort( direct );
    int calls = 0, failed = 0, commands = 0;
    std::vector< int > sizes;
    const auto route = in.m_slider;
    in.m_slider = [ & ]( const entity_t& e, ui_gfx_sort_t& into, const ui_font_draw_env_t& draw, float alpha )
    {
        c_ui_slider* slider = binding.slider( e );
        if ( !slider )
            return false;
        ui_slider_draw_inputs_t i = env.m_slider( e );
        i.m_value = slider->value( );
        direct.m_key_lo = into.m_key_lo;
        direct.m_key_hi = into.m_key_hi;
        const std::size_t before = direct.m_commands[ direct.m_index ].size( );
        ui_slider_draw( direct, draw, slider->m_s, slider->m_visible, slider->m_latched, i, alpha );
        sizes.push_back( static_cast< int >( direct.m_commands[ direct.m_index ].size( ) - before ) );
        ++calls;
        return route( e, into, draw, alpha );
    };
    ui_impl_t impl;
    const ui_frame_t frame = ui_screen_frame( *root, impl, sort, 1.0f, 4, &in );
    const auto& a = sort.m_commands[ sort.m_index ];
    const auto& b = direct.m_commands[ direct.m_index ];
    int leaves = 0, at = 0;
    for ( const ui_frame_draw_t& d : frame.m_draws )
        if ( d.m_kind == ui_frame_kind_t::slider )
        {
            const int size = leaves < static_cast< int >( sizes.size( ) ) ? sizes[ static_cast< std::size_t >( leaves ) ] : -1;
            ++leaves;
            if ( !d.m_emitted || d.m_commands != size )
                ++failed;
            for ( int c = 0; c < d.m_commands && c < size; ++c, ++commands )
            {
                const ui_gfx_command_t& x = a[ static_cast< std::size_t >( d.m_first + c ) ];
                const ui_gfx_command_t& y = b[ static_cast< std::size_t >( at + c ) ];
                // the record itself: words 4 .. 7, then the payload up to the next command's offset (the sorts' earlier offsets differ)
                const auto payload = [ ]( const ui_gfx_sort_t& q, const std::vector< ui_gfx_command_t >& all, std::size_t index )
                {
                    const auto& buffer = q.m_buffers[ q.m_index ];
                    const std::size_t from = all[ index ].m_words[ 3 ];
                    const std::size_t to = index + 1 < all.size( ) ? all[ index + 1 ].m_words[ 3 ] : static_cast< std::size_t >( buffer.m_used );
                    return std::string( buffer.m_memory.begin( ) + static_cast< std::ptrdiff_t >( from ), buffer.m_memory.begin( ) + static_cast< std::ptrdiff_t >( to ) );
                };
                const std::string left = payload( sort, a, static_cast< std::size_t >( d.m_first + c ) ), right = payload( direct, b, static_cast< std::size_t >( at + c ) );
                const std::size_t common = left.size( ) < right.size( ) ? left.size( ) : right.size( );   // a later leaf's 16-byte alignment can pad the framed record
                if ( std::memcmp( x.m_words + 4, y.m_words + 4, 16 ) != 0 || common == 0 || left.compare( 0, common, right, 0, common ) != 0 )
                    ++failed;
            }
            at += size;
        }
    if ( leaves != 2 || calls != 2 || commands == 0 )
        ++failed;
    if ( failed == 0 )
        LOG_SUCCESS( "the settings sliders' frame kind routes to ui_slider_draw 0x4700E0: Screens/SettingsAudio, %d leaves, %d commands equal the direct draw (ROUTING)", leaves, commands );
    else
        LOG_ERROR( "slider frame: %d differ (%d leaves, %d calls, %d commands)", failed, leaves, calls, commands );
    return failed == 0;
}

// Batch 32: the actions a menu reaches on its way to a race against retail's (case menu_actions): VuRetVal, the broadcast, the game-data root
// after: VuStartActionGameUIAction 0x470A80, VuClearGameResultsUIAction 0x4721A0, VuSetMenuPresenceUIAction 0x4050B0 through c_ui_action,
// VuSetBoatConfigPlayerEntity's 0x44EF10 / 0x44EEA0 through ui_set_single_player / ui_boat_player_index (run_input's arms).
inline bool verify_ui_menu_actions( )
{
    int failed = 0, broadcasts = 0;
    for ( std::size_t k = 0; k < g_golden_mac_rows_count; ++k )
    {
        const golden_mac_row_t& g = g_golden_mac_rows[ k ];
        const std::vector< std::uint8_t > blob = ui_unhex( g.m_doc );
        value_t root = c_reader( blob ).parse( );
        std::string before, after;
        vujb_dump( root, before );
        ui_params_t params;
        const std::vector< std::uint8_t > records = ui_unhex( g.m_records );
        if ( !records.empty( ) )
            std::memcpy( params.m_bytes, records.data( ), records.size( ) );
        params.m_size = static_cast< int >( records.size( ) );
        std::uint32_t ret = 0;
        int logs = 0;
        std::uint32_t hash = 0;
        std::string sent;
        ui_action_kind_t kind;
        if ( ui_action_kind_of( g.m_class, kind ) )
        {
            c_ui_action action( kind, g.m_text );
            action.m_root = &root;
            action.m_broadcast = [ & ]( std::uint32_t h, const ui_params_t& p ) { ++logs; hash = h; sent = ui_hex( p ); };
            ret = action.trigger( params );
        }
        else if ( std::strcmp( g.m_plug, "SetSinglePlayer" ) == 0 )
            ret = ui_set_single_player( root );
        else
            ui_boat_player_index( root, params );
        vujb_dump( root, after );
        broadcasts += logs;
        const bool same = before == g.m_before && after == g.m_after && ret == g.m_ret && logs == g.m_logs && ( !logs || ( hash == g.m_hash && sent == g.m_params ) );
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "menu action %s . %s row %d differs: after '%s', retail '%s'", g.m_class, g.m_plug, static_cast< int >( k ), after.c_str( ), g.m_after );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "the menu's actions match retail's (%d runs, %d broadcasts): OnStartActionGame 0x470A80, GameResults clear 0x4721A0, presence 0x4050B0, 0x44EF10, 0x44EEA0: VuRetVal, broadcast, root",
                     static_cast< int >( g_golden_mac_rows_count ), broadcasts );
    else
        LOG_ERROR( "the menu's actions FAILED %d of %d", failed, static_cast< int >( g_golden_mac_rows_count ) );
    return failed == 0;
}

// A record's bytes (the words 4 .. 7, then the payload up to the next command's offset - the sorts' earlier offsets differ).
inline std::string ui_record_bytes( const ui_gfx_sort_t& q, std::size_t index )
{
    const auto& all = q.m_commands[ q.m_index ];
    const auto& buffer = q.m_buffers[ q.m_index ];
    const std::size_t from = all[ index ].m_words[ 3 ];
    const std::size_t to = index + 1 < all.size( ) ? all[ index + 1 ].m_words[ 3 ] : static_cast< std::size_t >( buffer.m_used );
    std::string out( reinterpret_cast< const char* >( all[ index ].m_words + 4 ), 16 );
    out.append( buffer.m_memory.begin( ) + static_cast< std::ptrdiff_t >( from ), buffer.m_memory.begin( ) + static_cast< std::ptrdiff_t >( to ) );
    return out;
}

// Batch 29, ROUTING only (each draw / callback is compared with retail by its own check): ui_screen_inputs over a bound Screens/RaceResults
// reaches 0x46A500 through ui_screen_frame (the same commands as the direct draw of that leaf, on the golden's document); the page's and
// the rows' touch callbacks arrive through the device's dispatch (0x62D9A0 / 0x461200 register them, the slot 7s remove them).
inline bool verify_ui_screen_inputs( )
{
    const ui_loaded_t& loaded = ui_loaded( );
    const auto root_of = [ & ]( const char* asset ) -> entity_t*
    {
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, asset ) == 0 && !loaded.trees[ p ].empty( ) )
                return const_cast< entity_t* >( loaded.trees[ p ][ 0 ].second );
        return nullptr;
    };
    int failed = 0, commands = 0, tables = 0, touches = 0, unknown = 0;
    const golden_rr_run_t* run = nullptr;
    for ( int k = 0; k < g_golden_rr_entities[ 0 ].m_runs && !run; ++k )
    {
        const golden_rr_run_t& r = g_golden_rr_runs[ g_golden_rr_entities[ 0 ].m_first + k ];
        if ( r.m_op == 1 && r.m_blob[ 0 ] && r.m_commands > 8 )
            run = &r;
    }
    if ( !run )
        ++failed;
    else
    {
        entity_t* root = root_of( "Screens/RaceResults" );
        const std::vector< std::uint8_t > blob = ui_unhex( run->m_blob );
        value_t data = c_reader( blob ).parse( );
        ui_bind_hooks_t hooks;
        hooks.m_root = &data;
        c_ui_binding binding( *root, hooks );
        ui_screen_env_t env;
        env.m_fontdb = &ui_font_db( 0 );
        env.m_root = &data;
        env.m_font_asset = []( const std::string& ) { return 0x0F0E0D0Cu; };
        env.m_string6 = []( const std::string& id ) { return id + "~"; };
        ui_frame_inputs_t in = ui_screen_inputs( binding, env );
        ui_gfx_sort_t sort, direct;
        ui_lv_sort( sort );
        ui_lv_sort( direct );
        const auto route = in.m_results;
        in.m_results = [ & ]( const entity_t& e, ui_gfx_sort_t& into, const ui_font_draw_env_t& draw, float alpha )
        {
            ui_results_table_t* table = binding.results( e );
            if ( !table )
                return false;
            ui_results_table_t copy = *table;
            direct.m_key_lo = into.m_key_lo;
            direct.m_key_hi = into.m_key_hi;
            ui_results_draw( direct, draw, copy, ui_results_inputs_of( e, env ), data );
            ++tables;
            return route( e, into, draw, alpha );
        };
        ui_impl_t impl;
        const ui_frame_t frame = ui_screen_frame( *root, impl, sort, 1.0f, 4, &in );
        const auto& a = sort.m_commands[ sort.m_index ];
        const auto& b = direct.m_commands[ direct.m_index ];
        int results = 0;
        for ( const ui_frame_draw_t& d : frame.m_draws )
            if ( d.m_kind == ui_frame_kind_t::results )
            {
                ++results;
                if ( !d.m_emitted || d.m_commands != static_cast< int >( b.size( ) ) )
                    ++failed;
                for ( int c = 0; c < d.m_commands && c < static_cast< int >( b.size( ) ); ++c )
                {
                    ++commands;
                    if ( ui_record_bytes( sort, static_cast< std::size_t >( d.m_first + c ) ) != ui_record_bytes( direct, static_cast< std::size_t >( c ) ) )
                        ++failed;
                }
            }
        if ( results != 1 || tables != 1 || commands == 0 )
        {
            LOG_ERROR( "screen inputs: %d results leaves, %d tables, %d commands (direct %d)", results, tables, commands, static_cast< int >( b.size( ) ) );
            ++failed;
        }
    }
    const auto dispatch = [ & ]( const char* asset, const auto& one )
    {
        entity_t* root = root_of( asset );
        ui_touch_device_t device;
        static const float k_inverse[ 16 ] = { 1.0f, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        ui_bind_hooks_t hooks;
        hooks.m_touch_device = &device;
        hooks.m_inverse = k_inverse;
        hooks.m_font_db = &ui_font_db( 0 );
        c_ui_binding binding( *root, hooks );
        // Credits authors Touchable off: on here, so the path a touchable page takes runs.
        const auto touchable = [ & ]( const auto& self, const entity_t& e ) -> void
        {
            if ( ui_page_layout_t* page = binding.page( e ) )
                page->m_touchable = 1;
            for ( const auto& child : e.m_children )
                self( self, *child );
        };
        touchable( touchable, *root );
        binding.project_initialize( );
        const auto visit = [ & ]( const auto& self, const entity_t& e ) -> void
        {
            one( binding, device, e );
            for ( const auto& child : e.m_children )
                self( self, *child );
        };
        visit( visit, *root );
        binding.project_release( );
        unknown += binding.m_unknown_slots;
        if ( !device.m_callbacks.empty( ) || !device.m_handlers.empty( ) )
        {
            LOG_ERROR( "screen inputs: %s: %d callbacks, %d handlers left", asset, static_cast< int >( device.m_callbacks.size( ) ), static_cast< int >( device.m_handlers.size( ) ) );
            ++failed;
        }
    };
    dispatch( "Screens/Credits", [ & ]( c_ui_binding& binding, ui_touch_device_t& device, const entity_t& e )
    {
        ui_page_layout_t* page = binding.page( e );
        if ( !page )
            return;
        ui_crop_t crop;
        float r[ 4 ];
        ui_page_rect( *page, crop, r );
        ui_touch_dispatch_down( device, 0u, r[ 0 ] + r[ 2 ] * 0.5f, r[ 1 ] + r[ 3 ] * 0.5f );
        if ( page->m_held != 1u )
            ++failed;
        ui_touch_dispatch_up( device, 0u, 0.0f, 0.0f );
        if ( page->m_held != 0u )
            ++failed;
        ++touches;
    } );
    dispatch( "Screens/ControlsGamepad", [ & ]( c_ui_binding& binding, ui_touch_device_t& device, const entity_t& e )
    {
        ui_remap_t* row = binding.remap( e );
        if ( !row || !row->m_visible )
            return;
        ui_touch_dispatch_down( device, 0u, 0.25f, 0.125f );
        ui_remap_runtime_t* s = binding.remap_state( e );
        if ( s->m_pressed != 1u || s->m_down != 1u )
            ++failed;
        s->m_pressed = s->m_down = 0;
        ++touches;
    } );
    if ( touches == 0 )
        ++failed;
    if ( failed == 0 )
        LOG_SUCCESS( "ui_screen_inputs routes the results table (%d commands = the direct draw's) and c_ui_binding the page's / rows' touch callbacks (%d touches), registered and removed; %d other slots unknown", commands, touches, unknown );
    else
        LOG_ERROR( "the screen inputs FAILED %d checks", failed );
    return failed == 0;
}

// Batch 32, ROUTING only (0x4578D0 / 0x448F50 / 0x44FB60 are checked against retail by their own cases): ui_platform_of's keys reach the right
// profile answer; ui_screen_inputs over bound Screens/GameConfigRace and BoatConfig, built by config_setup + project_initialize as the menu
// does, draws each selector row: its label and shown choice go through VuStringDB, its seven textures by their property names, the boat
// stat's two by theirs, the two fonts by HeadingFont / ValueFont.
inline bool verify_ui_selector_frame( )
{
    int failed = 0, rows = 0, stats = 0, keys = 0;
    {
        std::vector< std::string > calls;
        const auto note = [ &calls ]( const std::string& what ) { calls.push_back( what ); };
        ui_profile_t p;
        p.unlock = [ note ]( int slot, const std::string& a, const std::string& b, const std::string& c ) { note( "unlock " + std::to_string( slot ) + " " + a + " " + b + " " + c ); return true; };
        p.license = [ note ] { note( "license" ); return true; };
        p.store_beta = [ note ] { note( "beta" ); return false; };
        p.progress = [ note ]( const std::string& v ) { note( "progress " + v ); return true; };
        p.package = [ note ]( const std::string& a ) { note( "package " + a ); return false; };
        p.champ_done = [ note ]( const std::string& a ) { note( "champ " + a ); return true; };
        p.champ_rank = [ ]( const std::string& ) { return 3; };
        p.race_done = [ note ]( const std::string& a, const std::string& b, const std::string& c ) { note( "race " + a + " " + b + " " + c ); return false; };
        p.race_rank = [ ]( const std::string&, const std::string&, const std::string& ) { return 5; };
        p.ghost_done = [ note ]( const std::string& a ) { note( "ghost " + a ); return true; };
        const auto platform = ui_platform_of( p );
        struct row_t { const char* key; const char* call; bool first; int second; };
        static const row_t k_rows[] = { { "92|n|v", "unlock 92 n v ", true, 0 }, { "100|m|t|c", "unlock 100 m t c", true, 0 }, { "license", "license", true, 0 }, { "4D3170", "beta", false, 0 },
                                        { "132|v", "progress v", true, 0 }, { "144|a", "package a", false, 0 }, { "88|c", "champ c", true, 3 }, { "48|m|t|c", "race m t c", false, 5 }, { "124|t", "ghost t", true, 0 } };
        for ( const row_t& r : k_rows )
        {
            calls.clear( );
            const std::pair< bool, int > got = platform( r.key );
            ++keys;
            if ( calls.size( ) != 1u || calls[ 0 ] != r.call || got.first != r.first || got.second != r.second )
            {
                LOG_ERROR( "ui_platform_of( %s ): '%s' %d %d", r.key, calls.empty( ) ? "" : calls[ 0 ].c_str( ), static_cast< int >( got.first ), got.second );
                ++failed;
            }
        }
    }
    const ui_loaded_t& loaded = ui_loaded( );
    const value_t option_db = ui_cx_document( 4 ), track_db = ui_cx_document( 2 ), boat_db = ui_cx_document( 0 ), skin_db = ui_cx_document( 1 ), champ_db = ui_cx_document( 3 );
    for ( const char* asset : { "Screens/GameConfigRace", "Screens/BoatConfig" } )
    {
        entity_t* root = nullptr;
        for ( std::size_t p = 0; p < g_golden_ui_projects_count; ++p )
            if ( std::strcmp( g_golden_ui_projects[ p ].m_asset, asset ) == 0 && !loaded.trees[ p ].empty( ) )
                root = const_cast< entity_t* >( loaded.trees[ p ][ 0 ].second );
        if ( !root )
        {
            LOG_ERROR( "selector frame: %s not loaded", asset );
            ++failed;
            continue;
        }
        value_t data;
        ui_bind_hooks_t hooks;
        hooks.m_root = &data;
        hooks.m_option_db = &option_db;
        hooks.m_track_db = &track_db;
        hooks.m_boat_db = &boat_db;
        hooks.m_skin_db = &skin_db;
        hooks.m_champ_db = &champ_db;
        hooks.m_font_db = &ui_font_db( 0 );
        hooks.m_platform = ui_platform_of( ui_profile_all_unlocked( ) );
        c_ui_binding binding( *root, hooks );
        binding.config_setup( );
        binding.project_initialize( );
        std::vector< std::string > strings, textures, fonts;
        const ui_texture_t texture{ 0x1111u, 64, 32 };
        ui_screen_env_t env;
        env.m_fontdb = &ui_font_db( 0 );
        env.m_font_asset = [ &fonts ]( const std::string& a ) { fonts.push_back( a ); return 0x0F0E0D0Cu; };
        env.m_string7 = [ &strings ]( const std::string& id ) { strings.push_back( id ); return id + "~"; };
        env.m_string6 = env.m_string7;
        env.m_asset_texture = [ &textures, &texture ]( const std::string& a ) -> const ui_texture_t* { textures.push_back( a ); return &texture; };
        const ui_frame_inputs_t in = ui_screen_inputs( binding, env );
        ui_gfx_sort_t sort;
        ui_lv_sort( sort );
        ui_impl_t impl;
        const ui_frame_t frame = ui_screen_frame( *root, impl, sort, 1.0f, 4, &in );
        const auto has = [ ]( const std::vector< std::string >& list, const std::string& what ) { return std::find( list.begin( ), list.end( ), what ) != list.end( ); };
        const auto text_of = [ ]( const entity_t& e, const char* name ) { const ui_slot_t* v = ui_property_value( e, name ); return v ? v->m_text : std::string( ); };
        for ( const ui_frame_draw_t& d : frame.m_draws )
        {
            const entity_t& e = *d.m_entity;
            if ( d.m_kind == ui_frame_kind_t::stat )
            {
                ++stats;
                if ( !d.m_emitted || !binding.stat( e ) || !has( textures, text_of( e, "Tick Off Texture" ) ) || !has( textures, text_of( e, "Tick On Texture" ) ) )
                    ++failed;
            }
            if ( d.m_kind != ui_frame_kind_t::config )
                continue;
            const ui_config_entity_t* config = binding.config( e );
            if ( !config || !d.m_emitted || d.m_commands <= 0 || config->m_options.empty( ) )
            {
                LOG_ERROR( "selector frame: %s: bound %d, emitted %d, %d commands", asset, config ? 1 : 0, static_cast< int >( d.m_emitted ), d.m_commands );
                ++failed;
                continue;
            }
            std::string heading, value;
            std::uint8_t params[ 60 ];
            ui_font_params( ui_font_db( 0 ), text_of( e, "HeadingFont" ), params, heading );
            ui_font_params( ui_font_db( 0 ), text_of( e, "ValueFont" ), params, value );
            if ( !has( fonts, heading ) || !has( fonts, value ) || !has( textures, text_of( e, "ValueTexture" ) ) )
                ++failed;
            for ( const value_t* option : config->m_options )
            {
                const ui_config_combo_t& c = config->m_combos[ static_cast< std::size_t >( ui_json_as_int( member( *option, "ComboBoxID" ) ) ) ];
                const int shown = ui_fistp32( c.m_shown );
                ++rows;
                if ( !has( strings, c.m_string_id ) || ( !c.m_choice_ids.empty( ) && !has( strings, c.m_choice_ids[ static_cast< std::size_t >( shown ) ] ) ) )
                    ++failed;
                if ( shown > 0 && !has( textures, text_of( e, "LeftArrowTexture" ) ) && !has( textures, text_of( e, "SelectedLeftArrowTexture" ) ) )
                    ++failed;
                if ( shown < static_cast< int >( c.m_choice_ids.size( ) ) - 1 && !has( textures, text_of( e, "RightArrowTexture" ) ) && !has( textures, text_of( e, "SelectedRightArrowTexture" ) ) )
                    ++failed;
            }
        }
        binding.project_release( );
    }
    if ( rows == 0 || stats == 0 )
        ++failed;
    if ( failed == 0 )
        LOG_SUCCESS( "ui_screen_inputs draws the selectors (ROUTING): %d rows on GameConfigRace + BoatConfig, %d boat stats, %d platform keys", rows, stats, keys );
    else
        LOG_ERROR( "the selector frame FAILED %d checks (%d rows, %d stats)", failed, rows, stats );
    return failed == 0;
}

// Batch 32: VuScreenFade* against retail (case screen_fade): the weight bits of slot 12 0x43EB50 / 0x43F060, then the tick 0x43E710's calls of
// 0x507830: colour, rect, the key word left at each call, the depth.
// footer_text: VuGamerTagUITextEntity 0x445DE0, VuCreditsUITextEntity 0x445F00, VuLicenseEntity's 0x42DE10 / 0x42DE40 run by retail: the text bytes,
// the bool word. String ids asked of the DB are compared too (the ids the C++ asks must answer what retail's stub answered).
inline bool verify_ui_footer_text( )
{
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_ftx_rows_count; ++k )
    {
        const golden_ftx_row_t& g = g_golden_ftx_rows[ k ];
        const auto db = [ &g ]( const char* id ) {
            if ( std::strcmp( id, "SignIn_InProgress" ) == 0 ) return std::string( g.m_busy );
            if ( std::strcmp( id, "Common_Numeric_Separator" ) == 0 ) return std::string( g.m_sep );
            if ( std::strcmp( id, "CreditsSuffix" ) == 0 ) return std::string( g.m_suffix );
            return std::string( "?" );
        };
        std::string got;
        if ( g.m_kind == 0 )
        {
            ui_signin_t s;
            s.m_present = ( g.m_a & 1 ) != 0;
            s.m_signed_in = ( g.m_a & 2 ) != 0;
            s.m_busy = ( g.m_a & 4 ) != 0;
            s.m_name = g.m_name;
            got = ui_gamertag_text( s, db );
        }
        else
            got = ui_credits_text( g.m_a != 0, g.m_b, db );
        if ( got != g.m_want )
        {
            ++failed;
            if ( failed <= 3 )
                LOG_ERROR( "footer text row %d: '%s', retail '%s'", static_cast< int >( k ), got.c_str( ), g.m_want );
        }
    }
    for ( std::size_t k = 0; k < g_golden_ftx_license_count; ++k )
    {
        const golden_ftx_license_t& g = g_golden_ftx_license[ k ];
        failed += g.m_type != 3u || ( ui_license_answer( g.m_trial != 0, g.m_full != 0 ) ? 1u : 0u ) != g.m_value ? 1 : 0;
    }
    if ( failed )
    {
        LOG_ERROR( "footer texts: %d rows differ", failed );
        return false;
    }
    LOG_SUCCESS( "footer texts 0x445DE0 / 0x445F00 and the license plugs equal retail on %d + %d vectors", static_cast< int >( g_golden_ftx_rows_count ), static_cast< int >( g_golden_ftx_license_count ) );
    return true;
}

// completion_text: retail's four slot 6s over a synthetic TrackDB / ChampDB (real reader, real VuJsonContainer) on the record, open and package answers each row
// lists: the text, +0x124, +0x125, the package / record / open calls' count.
inline bool verify_ui_completion_text( )
{
    int failed = 0;
    value_t tracks, champs;
    const auto rows_of = [ ]( value_t& db, const char* const* keys, std::size_t count ) {
        value_t& rows = ui_json_child( db, "VuDBAsset" );
        for ( std::size_t k = 0; k < count; ++k )
            ui_json_set_string( ui_json_child( ui_json_child( rows, keys[ k ] ), "AssetDB" ), keys[ k ] );
    };
    static const char* const track_keys[ 5 ] = { "T0", "T1", "T2", "T3", "T4" };
    static const char* const champ_keys[ 3 ] = { "T0", "T1", "T2" };
    rows_of( tracks, track_keys, 5 );
    rows_of( champs, champ_keys, 3 );
    const auto index = [ ]( const std::string& key, const std::string& course ) {
        const int c = course == "CourseB" ? 1 : course == "CourseC" ? 2 : 0;
        return ( key[ 1 ] - '0' ) * 3 + c;
    };
    for ( std::size_t k = 0; k < g_golden_cpl_rows_count; ++k )
    {
        const golden_cpl_row_t& g = g_golden_cpl_rows[ k ];
        ui_completion_t c;
        c.m_kind = static_cast< ui_completion_kind_t >( g.m_kind );
        ui_completion_env_t env;
        env.m_track_db = &tracks;
        env.m_champ_db = &champs;
        env.m_package = [ &g ]( const std::string& asset ) { return ( g.m_package >> ( asset[ 1 ] - '0' ) & 1u ) != 0u; };
        int calls = 0;
        env.m_record = [ & ]( ui_completion_kind_t, const std::string& track, const std::string& course, std::int32_t& place ) {
            ++calls;
            const int i = index( track, course );
            if ( ( g.m_record >> i & 1u ) == 0u )
                return false;
            place = g.m_place[ i ];
            return true;
        };
        env.m_open = [ & ]( ui_completion_kind_t, const std::string& track, const std::string& course ) { ++calls; return ( g.m_open >> index( track, course ) & 1u ) != 0u; };
        env.m_string6 = [ ]( const char* id ) { return std::string( std::strcmp( id, "SinglePlayer_020" ) == 0 ? "Complete" : "?" ); };
        ui_completion_initialize( c, env );
        if ( c.m_text != g.m_text || c.m_unused != ( g.m_unused != 0 ) || c.m_locked != ( g.m_locked != 0 ) || calls != g.m_calls )
        {
            ++failed;
            if ( failed <= 3 )
                LOG_ERROR( "completion row %d: '%s' %d %d calls %d, retail '%s' %d %d calls %d", static_cast< int >( k ), c.m_text.c_str( ), c.m_unused, c.m_locked, calls, g.m_text, g.m_unused, g.m_locked, g.m_calls );
        }
    }
    if ( failed )
    {
        LOG_ERROR( "completion texts: %d rows differ", failed );
        return false;
    }
    LOG_SUCCESS( "completion texts 0x458500 / 0x458A40 / 0x459060 / 0x4595A0 equal retail on %d vectors", static_cast< int >( g_golden_cpl_rows_count ) );
    return true;
}

// achievement_text: 0x4458F0 / 0x445C50 by retail over a synthetic AchievementDB: one row per key, the DB built from the row.
inline bool verify_ui_achievement_text( )
{
    int failed = 0;
    value_t db;
    value_t& rows = ui_json_child( db, "VuDBAsset" );
    for ( std::size_t k = 0; k < g_golden_ach_rows_count; ++k )
    {
        const golden_ach_row_t& g = g_golden_ach_rows[ k ];
        if ( g.m_absent )
            continue;
        value_t& row = ui_json_child( rows, g.m_key );
        if ( g.m_id )
            ui_json_set_string( ui_json_child( row, "StringID" ), g.m_id );
        value_t& value = ui_json_child( row, "Value" );
        value.tag = g.m_tag;
        value.kind = g.m_tag == 2u ? value_t::kind_t::real : g.m_tag == 7u ? value_t::kind_t::int64 : g.m_tag == 4u ? value_t::kind_t::text : value_t::kind_t::integer;
        value.integer = static_cast< int >( g.m_low );
        std::memcpy( &value.real, &g.m_low, 4 );
        value.int64 = static_cast< std::int64_t >( static_cast< std::uint64_t >( g.m_high ) << 32 | g.m_low );
    }
    const auto string7 = [ ]( const std::string& id ) { return id + "|t"; };
    for ( std::size_t k = 0; k < g_golden_ach_rows_count; ++k )
    {
        const golden_ach_row_t& g = g_golden_ach_rows[ k ];
        const std::string name = ui_achievement_name( &db, g.m_key, string7 ), value = ui_achievement_value( &db, g.m_key );
        if ( name != g.m_name || value != g.m_value )
        {
            ++failed;
            if ( failed <= 3 )
                LOG_ERROR( "achievement row %d: '%s' '%s', retail '%s' '%s'", static_cast< int >( k ), name.c_str( ), value.c_str( ), g.m_name, g.m_value );
        }
    }
    if ( failed )
    {
        LOG_ERROR( "achievement texts: %d rows differ", failed );
        return false;
    }
    LOG_SUCCESS( "achievement texts 0x4458F0 / 0x445C50 equal retail on %d keys", static_cast< int >( g_golden_ach_rows_count ) );
    return true;
}

inline bool verify_ui_screen_fade( )
{
    int failed = 0, drawn = 0, ones = 0, zeros = 0, ramps = 0;
    ui_gfx_sort_t sort;
    ui_lv_sort( sort );
    const auto bits = [ ]( float v ) { std::uint32_t u; std::memcpy( &u, &v, 4 ); return u; };
    const auto real = [ ]( unsigned u ) { float v; std::memcpy( &v, &u, 4 ); return v; };
    for ( std::size_t k = 0; k < g_golden_sfd_rows_count; ++k )
    {
        const golden_sfd_row_t& g = g_golden_sfd_rows[ k ];
        ui_screen_fade_t f;
        f.m_box = g.m_box != 0;
        std::memcpy( f.m_colour, &g.m_colour, 4 );
        for ( int a = 0; a < 3; ++a )
        {
            f.m_inner[ a ] = real( g.m_inner[ a ] );
            f.m_position[ a ] = real( g.m_pos[ a ] );
            f.m_scale[ a ] = real( g.m_scale[ a ] );
            for ( int b = 0; b < 3; ++b )
                f.m_axis[ a ][ b ] = real( g.m_axis[ a * 3 + b ] );
        }
        const float eye[ 3 ] = { real( g.m_eye[ 0 ] ), real( g.m_eye[ 1 ] ), real( g.m_eye[ 2 ] ) };
        const float weight = f.m_box ? ui_fade_box( f, eye ) : ui_fade_ellipsoid( f, eye );
        ones += weight == 1.0f ? 1 : 0;
        zeros += weight == 0.0f ? 1 : 0;
        ramps += weight != 1.0f && weight != 0.0f ? 1 : 0;
        bool same = bits( weight ) == g.m_weight;
        std::vector< ui_fade_view_t > views;
        for ( int v = 0; v < g.m_views; ++v )
        {
            ui_fade_view_t view;
            view.m_rect[ 0 ] = 0.0f;
            view.m_rect[ 1 ] = 0.25f * static_cast< float >( v );
            view.m_rect[ 2 ] = 1.0f;
            view.m_rect[ 3 ] = 0.5f;
            for ( int a = 0; a < 3; ++a )
                view.m_eye[ a ] = v == 0 ? eye[ a ] : eye[ a ] + static_cast< float >( v );
            views.push_back( view );
        }
        // Each call's key: the prefix of the viewports up to it.
        int at = 0;
        std::vector< ui_fade_view_t > prefix;
        ui_font_draw_env_t env;
        std::uint32_t key = g.m_key0;
        for ( const ui_fade_view_t& view : views )
        {
            prefix.assign( 1u, view );
            prefix[ 0 ] = view;
            sort.m_key_hi = key;
            sort.m_commands[ sort.m_index ].clear( );
            sort.m_buffers[ sort.m_index ].m_used = 0;
            // the viewport index in the key: the k-th draw of the whole list
            std::vector< ui_fade_view_t > upto( views.begin( ), views.begin( ) + ( &view - views.data( ) ) + 1 );
            std::vector< ui_fade_view_t > mute = upto;
            for ( std::size_t m = 0; m + 1 < mute.size( ); ++m )
                mute[ m ].m_eye[ 0 ] = mute[ m ].m_eye[ 1 ] = mute[ m ].m_eye[ 2 ] = 1e30f;   // outside: no draw before the last
            ui_fade_draw( sort, env, f, mute );
            const auto& commands = sort.m_commands[ sort.m_index ];
            if ( commands.empty( ) )
                continue;
            key = sort.m_key_hi;
            ++drawn;
            if ( at >= g.m_calls || commands.size( ) != 1u )
            {
                same = false;
                break;
            }
            const golden_sfd_call_t& c = g_golden_sfd_calls[ g.m_first + at++ ];
            const std::uint8_t* record = sort.m_buffers[ sort.m_index ].m_memory.data( ) + commands[ 0 ].m_words[ 3 ];
            std::uint32_t colour, rect[ 4 ];
            std::memcpy( &colour, record + 0x40, 4 );
            std::memcpy( rect, record + 0x44, 16 );
            if ( colour != c.m_colour || std::memcmp( rect, c.m_rect, 16 ) != 0 || key != c.m_key || c.m_depth != 0x3F800000u )
                same = false;
        }
        if ( at != g.m_calls )
            same = false;
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "screen fade row %d (box %d): weight 0x%08X, retail 0x%08X; %d calls of %d", static_cast< int >( k ), g.m_box, bits( weight ), g.m_weight, at, g.m_calls );
    }
    if ( ones == 0 || zeros == 0 || ramps == 0 || drawn == 0 )
        ++failed;
    if ( failed == 0 )
        LOG_SUCCESS( "VuScreenFade matches retail: %d volumes (slot 12 0x43EB50 / 0x43F060: %d full, %d out, %d on the ramp), the tick 0x43E710's %d draws: colour, rect, key, depth",
                     static_cast< int >( g_golden_sfd_rows_count ), ones, zeros, ramps, drawn );
    else
        LOG_ERROR( "the screen fade FAILED %d of %d (%d full, %d out, %d ramp, %d drawn)", failed, static_cast< int >( g_golden_sfd_rows_count ), ones, zeros, ramps, drawn );
    return failed == 0;
}

inline bool verify_ui_widgets_all( )
{
    bool ok = true;
    ok = verify_ui_championship_draw( ) && ok;
    ok = verify_ui_remap_draw( ) && ok;
    ok = verify_ui_remap_logic( ) && ok;
    ok = verify_ui_wired_bind( ) && ok;
    ok = verify_ui_pause_menu( ) && ok;
    ok = verify_ui_award_screen( ) && ok;
    ok = verify_ui_champ_bind( ) && ok;
    ok = verify_ui_font_wrap( ) && ok;
    ok = verify_ui_menu_actions( ) && ok;
    ok = verify_ui_selector_frame( ) && ok;
    ok = verify_ui_screen_fade( ) && ok;
    ok = verify_ui_footer_text( ) && ok;
    ok = verify_ui_completion_text( ) && ok;
    ok = verify_ui_achievement_text( ) && ok;
    ok = verify_ui_screen_inputs( ) && ok;
    ok = verify_ui_slider_frame( ) && ok;
    ok = verify_ui_common_screen( ) && ok;
    ok = verify_ui_message_box_frame( ) && ok;
    ok = verify_ui_control_methods( ) && ok;
    ok = verify_ui_page_layout_tick( ) && ok;
    ok = verify_ui_page_layout( ) && ok;
    ok = verify_ui_text_layout_draw( ) && ok;
    ok = verify_ui_game_config_release( ) && ok;
    ok = verify_ui_message_box_draw( ) && ok;
    ok = verify_ui_slider_draw( ) && ok;
    ok = verify_ui_hud_controls( ) && ok;
    ok = verify_ui_hud_layout_draw( ) && ok;
    ok = verify_ui_minimap_layout( ) && ok;
    ok = verify_ui_hud_toast( ) && ok;
    return ok;
}
