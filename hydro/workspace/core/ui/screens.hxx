#pragma once

#include <workspace/core/ui/bind.hxx>
#include <workspace/core/ui/frame.hxx>
#include <workspace/core/ui/pages.hxx>
#include <workspace/core/ui/remap.hxx>
#include <workspace/core/ui/results.hxx>

#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// The frame inputs for one screen in the game: what ui_screen_frame (frame.hxx) needs to emit a loaded screen's leaves, built over
// that screen's c_ui_binding (the runtime objects) and the owner's environment (the materials, FontDB, the asset factory, VuStringDB).
// Nothing here is a retail function: it is the wiring between OUR objects. The world's call:
//
//     ui_screen_env_t env = ...;                       // once, from the render pass
//     ui_frame_inputs_t in = ui_screen_inputs( binding, env );
//     ui_screen_frame( root, sort, stacks, impl, ..., &in );
//
// and ui_game_screens_t::m_frame is the natural place for it (common.hxx).
//
// Batch 32: m_info, m_config, m_combo, m_combo_inputs, m_stat, m_bar come from the binding; m_combo_inputs' and the slider's textures, the
// buttons' and the boat stat's need m_asset_texture. What stays out, each an m_* the caller may set after the call: m_message_box,
// m_board / m_leaderboard (their fonts and the platform leaderboard rows).
struct ui_screen_env_t
{
    ui_font_draw_env_t m_draw;                                                     // the materials
    ui_string_db_t m_db;                                                           // 0x5546B0's "right-ea-left" answer
    const value_t* m_fontdb = nullptr;                                             // FontDB's document
    const ui_crop_t* m_crop = nullptr;                                             // VuUIImpl's crop
    std::function< std::string( const std::string& id ) > m_string6;               // VuStringDB slot 6 (world/strings.hxx)
    std::function< std::string( const std::string& id ) > m_string7;               // VuStringDB slot 7
    std::function< std::string( const entity_t& ) > m_text;                        // a text leaf's slot 12 (per type)
    ui_signin_t m_signin;                                                          // the gamer tag's 0x445DE0 (footer.hxx)
    std::function< std::int32_t( ) > m_credits;                                    // g 0x758FE8 vt+0x38; null: no manager (0x445F00's 1234567)
    const value_t* m_achievement_db = nullptr;                                     // gameutil +0x24
    std::function< ui_text_font_t( const entity_t& ) > m_font;                     // 0x626B80
    std::function< const ui_texture_t*( const entity_t&, int which ) > m_texture;  // an image's slot 14, a button's three
    std::function< std::uint32_t( const std::string& asset ) > m_font_asset;       // FontDB's "FontAsset" -> the asset's +88, 0 none
    ui_page_hooks_t m_page;                                                        // the page draw's hooks
    ui_page_env_t m_page_env;                                                      // m_draw is replaced by each leaf's
    std::function< ui_remap_inputs_t( const entity_t& ) > m_remap;                 // a row's textures and fonts
    std::function< ui_slider_draw_inputs_t( const entity_t& ) > m_slider;          // a slider's textures, font, strings; m_value is filled here
    const value_t* m_root = nullptr;                                               // gameutil +0x50 (GameResults)
    const ui_champ_env_t* m_champ = nullptr;                                       // the championship's (m_boat_db, the award's draw)
    std::function< const ui_texture_t*( const std::string& asset ) > m_asset_texture;  // a VuTextureAsset by the loaded property's name (+0x58)
    const ui_difficulty_env_t* m_difficulty = nullptr;                             // the class picker 0x459D70's
};

// A loaded asset property's texture (the property's +0x4C asset, its +0x58). Null: no name or no answer: the draws draw nothing.
inline const ui_texture_t* ui_screen_texture( const entity_t& e, const char* property, const ui_screen_env_t& env )
{
    const ui_slot_t* s = ui_property_value( e, property );
    return s && !s->m_text.empty( ) && env.m_asset_texture ? env.m_asset_texture( s->m_text ) : nullptr;
}

// 0x626B80 on a named VuUIFont property: its asset's font, else VuUIImpl +0x94's "Dev".
inline std::uint32_t ui_screen_font( const entity_t& e, const char* property, const ui_screen_env_t& env )
{
    if ( !env.m_font_asset || !env.m_fontdb )
        return 0u;
    const ui_slot_t* name = ui_property_value( e, property );
    std::uint8_t params[ 60 ] = { };
    std::string asset;
    ui_font_params( *env.m_fontdb, name ? name->m_text : std::string( ), params, asset );
    const std::uint32_t font = asset.empty( ) ? 0u : env.m_font_asset( asset );
    return font ? font : env.m_font_asset( "Dev" );
}

// The block's seven asset properties, in ui_combo_texture_t's order.
inline const char* const k_ui_combo_textures[ 7 ] = { "ValueTexture", "LeftArrowTexture", "RightArrowTexture", "SelectedLeftArrowTexture",
                                                      "SelectedRightArrowTexture", "PressedLeftArrowTexture", "PressedRightArrowTexture" };

inline ui_combo_inputs_t ui_combo_inputs_of( const entity_t& e, const ui_screen_env_t& env )
{
    ui_combo_inputs_t ci;
    ci.m_label_font = ui_screen_font( e, "HeadingFont", env );
    ci.m_value_font = ui_screen_font( e, "ValueFont", env );
    ci.m_string = env.m_string7;
    const entity_t* who = &e;
    ci.m_texture = [ who, env ]( int which ) -> const ui_texture_t*
    {
        return which >= 0 && which < 7 ? ui_screen_texture( *who, k_ui_combo_textures[ which ], env ) : nullptr;
    };
    return ci;
}

// A table's fonts from FontDB: ui_font_params on each column's VuUIFont name (verify_ui_race_results compares the same path with
// retail's bytes), the asset through the owner. Bytes 41..59 of a param block stay 0: retail's are indeterminate stack (UNKNOWN).
inline ui_results_inputs_t ui_results_inputs_of( const entity_t& e, const ui_screen_env_t& env )
{
    ui_results_inputs_t in;
    in.m_string = env.m_string6;
    for ( int c = 0; c < 6; ++c )
    {
        const ui_slot_t* name = ui_property_value( e, k_ui_results_font_names[ c ] );
        if ( !name || !env.m_fontdb )
            continue;
        std::string asset;
        ui_font_params( *env.m_fontdb, name->m_text, in.m_fonts[ c ].m_params, asset );
        in.m_fonts[ c ].m_font = env.m_font_asset ? env.m_font_asset( asset ) : 0u;
    }
    return in;
}

// A slider's Frame Texture +0x1A0, Selected Frame Texture +0x1A4, Bar Texture +0x1A8 and its font, from the loaded properties.
inline ui_slider_draw_inputs_t ui_slider_inputs_of( const entity_t& e, const ui_screen_env_t& env )
{
    ui_slider_draw_inputs_t i;
    i.m_frame = ui_screen_texture( e, "Frame Texture", env );
    i.m_selected = ui_screen_texture( e, "Selected Frame Texture", env );
    i.m_bar = ui_screen_texture( e, "Bar Texture", env );
    i.m_font = ui_screen_font( e, "Font", env );
    i.m_string = env.m_string7;
    i.m_db = env.m_db;
    if ( env.m_fontdb )
    {
        const ui_slot_t* font = ui_property_value( e, "Font" );
        std::string asset;
        ui_font_params( *env.m_fontdb, font ? font->m_text : std::string( ), i.m_params, asset );
    }
    return i;
}

// The platform answers the selectors ask (c_ui_binding::ask's keys, bind.hxx ui_bound_config_t) from a ui_profile_t: "92|name|value",
// "96|...", "100|mode|track|course", "104|...", "license", "4D3170", "132|value", "144|asset", "88|championship", "48|mode|track|course",
// "124|track".
inline std::function< std::pair< bool, int >( const std::string& ) > ui_platform_of( ui_profile_t p )
{
    return [ p ]( const std::string& key ) -> std::pair< bool, int >
    {
        std::vector< std::string > part;
        std::size_t from = 0;
        for ( ;; )
        {
            const std::size_t bar = key.find( '|', from );
            part.push_back( key.substr( from, bar == std::string::npos ? bar : bar - from ) );
            if ( bar == std::string::npos )
                break;
            from = bar + 1;
        }
        const auto at = [ &part ]( std::size_t k ) { return k < part.size( ) ? part[ k ] : std::string( ); };
        if ( key == "license" )
            return { p.license && p.license( ), 0 };
        if ( key == "4D3170" )
            return { p.store_beta && p.store_beta( ), 0 };
        const int tag = std::atoi( part[ 0 ].c_str( ) );
        switch ( tag )
        {
        case 132: return { p.progress && p.progress( at( 1 ) ), 0 };
        case 144: return { p.package && p.package( at( 1 ) ), 0 };
        case 88: return { p.champ_done && p.champ_done( at( 1 ) ), p.champ_rank ? p.champ_rank( at( 1 ) ) : 0x7FFFFFFF };
        case 48: return { p.race_done && p.race_done( at( 1 ), at( 2 ), at( 3 ) ), p.race_rank ? p.race_rank( at( 1 ), at( 2 ), at( 3 ) ) : 0x7FFFFFFF };
        case 124: return { p.ghost_done && p.ghost_done( at( 1 ) ), 0 };
        default: return { p.unlock && p.unlock( tag, at( 1 ), at( 2 ), at( 3 ) ), 0 };
        }
    };
}

inline ui_frame_inputs_t ui_screen_inputs( c_ui_binding& binding, ui_screen_env_t env )
{
    ui_frame_inputs_t in;
    in.m_env = env.m_draw;
    in.m_db = env.m_db;
    in.m_fontdb = env.m_fontdb;
    in.m_font = env.m_font;
    in.m_string = env.m_string7;
    in.m_difficulty = env.m_difficulty;
    c_ui_binding* b = &binding;
    // Slot 12 of VuInfoUITextEntity 0x446860: the ticker's text +0x140, not a String ID.
    in.m_text = [ b, env ]( const entity_t& e )
    {
        if ( const ui_info_ticker_t* t = b->ticker( e ) )
        {
            ui_info_hooks_t hooks;
            hooks.m_string = env.m_string7;
            return ui_info_text( const_cast< ui_info_ticker_t& >( *t ), hooks );
        }
        if ( const ui_completion_t* c = b->completion( e ) )
            return c->m_text;
        const std::string* id = env.m_string7 ? b->runtime_string( e ) : nullptr;
        if ( id )
            return env.m_string7( *id );
        const std::uint32_t vt = e.m_ctor ? e.m_ctor->m_vtable : 0u;
        if ( const std::string* award = b->award_state( e ) )   // slot 12: 0x443920 +0x10C as it is; 0x446DE0 VuStringDB slot 7 on it
            return vt == k_ui_award_credit_vtable || !env.m_string7 ? *award : env.m_string7( *award );
        if ( vt == k_ui_achievement_name_vtable || vt == k_ui_achievement_value_vtable )
        {
            const ui_slot_t* key = ui_property_value( e, "Achievement" );
            const std::string name = key ? key->m_text : std::string( );
            if ( vt == k_ui_achievement_value_vtable )
                return ui_achievement_value( env.m_achievement_db, name );
            return env.m_string7 ? ui_achievement_name( env.m_achievement_db, name, env.m_string7 ) : std::string( );
        }
        if ( vt == k_ui_gamertag_vtable )
            return ui_gamertag_text( env.m_signin, [ & ]( const char* s ) { return env.m_string6 ? env.m_string6( s ) : std::string( ); } );
        if ( vt == k_ui_credits_vtable )
            return ui_credits_text( env.m_credits != nullptr, env.m_credits ? env.m_credits( ) : 0, [ & ]( const char* s ) { return env.m_string6 ? env.m_string6( s ) : std::string( ); } );
        return env.m_text ? env.m_text( e ) : std::string( );
    };
    // The button's three (0x443280: Selected / Unselected / Pressed Texture), the boat stat's two (Tick Off / On Texture); else the image's.
    in.m_texture = [ env, b ]( const entity_t& e, int which ) -> const ui_texture_t*
    {
        const ui_frame_kind_t kind = ui_frame_kind_of( e );
        if ( const std::string* award = b->award_state( e ) )   // slot 18 0x440EA0: +0xB8's handle
            return award->empty( ) || !env.m_asset_texture ? nullptr : env.m_asset_texture( *award );
        if ( kind == ui_frame_kind_t::button && env.m_asset_texture )
        {
            static const char* const names[ 3 ] = { "Selected Texture", "Unselected Texture", "Pressed Texture" };
            return which >= 0 && which < 3 ? ui_screen_texture( e, names[ which ], env ) : nullptr;
        }
        if ( kind == ui_frame_kind_t::stat )
            return ui_screen_texture( e, which ? "Tick On Texture" : "Tick Off Texture", env );
        return env.m_texture ? env.m_texture( e, which ) : nullptr;
    };
    in.m_info = [ b ]( const entity_t& e )
    {
        const ui_info_ticker_t* t = b->ticker( e );
        return t ? t->m_scroll : ui_info_ticker_of( e ).m_scroll;
    };
    in.m_config = [ b ]( const entity_t& e ) { return b->config( e ); };
    in.m_combo = [ b ]( const entity_t& e ) { return b->combo( e ); };
    in.m_combo_inputs = [ env ]( const entity_t& e ) { return ui_combo_inputs_of( e, env ); };
    in.m_stat = [ b ]( const entity_t& e )
    {
        const ui_boat_stat_t* s = b->stat( e );
        return s ? *s : ui_boat_stat_t( );
    };
    in.m_bar = [ b ]( const entity_t& e )
    {
        const ui_progress_bar_t* bar = b->bar( e );
        return bar ? *bar : ui_progress_bar_t( );
    };
    // The class leaves' Texture Asset C / B / A, Font C / B / A ("Font C " as authored).
    in.m_class_texture = [ env ]( const entity_t& e, int k ) -> const ui_texture_t*
    {
        static const char* const names[ 3 ] = { "Texture Asset C", "Texture Asset B", "Texture Asset A" };
        return k >= 0 && k < 3 ? ui_screen_texture( e, names[ k ], env ) : nullptr;
    };
    in.m_class_font = [ env ]( const entity_t& e, int k )
    {
        static const char* const names[ 3 ] = { "Font C ", "Font B ", "Font A " };
        ui_text_font_t f;
        f.m_asset_font = k >= 0 && k < 3 ? ui_screen_font( e, names[ k ], env ) : 0u;
        f.m_asset = f.m_asset_font != 0u;
        return f;
    };
    in.m_button = [ b ]( const entity_t& e, std::uint8_t& focused, std::uint32_t& press ) { b->button_state( e, focused, press ); };
    in.m_shown = [ b ]( const entity_t& e, std::uint8_t& visible, float& alpha ) { return b->shown( e, visible, alpha ); };
    in.m_page = [ b, env ]( const entity_t& e, ui_gfx_sort_t& sort, const ui_font_draw_env_t& draw, float alpha )
    {
        const ui_page_layout_t* page = b->page( e );
        if ( !page )
            return false;
        ui_page_env_t pe = env.m_page_env;
        pe.m_draw = draw;
        return ui_page_layout_draw( sort, pe, env.m_page, *page, alpha );
    };
    in.m_remap = [ b, env ]( const entity_t& e, ui_gfx_sort_t& sort, const ui_font_draw_env_t& draw, float alpha )
    {
        const ui_remap_t* row = b->remap( e );
        if ( !row || !env.m_remap )
            return false;
        // The row's six textures: 0x460E26 .. 0x460FD5 ask the asset factory 0x426A90 for fixed names, each handle = the asset's +0x58. The two VuUIFonts (0x626B80 on each).
        ui_remap_t r = *row;
        static const char* const k_textures[ 6 ] = { "UI/Gamepad_Axis", "UI/Gamepad_Button", "UI/Gamepad_Key", "UI/Gamepad_InputMapping", "UI/Gamepad_InputMapping_Selected", "UI/Gamepad_InputMapping_Pressed" };
        if ( env.m_asset_texture )
            for ( int k = 0; k < 6; ++k )
                if ( const ui_texture_t* t = env.m_asset_texture( k_textures[ k ] ) )
                    r.m_textures[ k ] = t->m_handle;
        r.m_label_font.m_asset_font = ui_screen_font( e, "Label Font", env );
        r.m_label_font.m_asset = r.m_label_font.m_asset_font != 0u;
        r.m_control_font.m_asset_font = ui_screen_font( e, "Control Font", env );
        r.m_control_font.m_asset = r.m_control_font.m_asset_font != 0u;
        return ui_remap_draw( sort, draw, env.m_db, r, env.m_remap( e ), alpha );
    };
    in.m_slider = [ b, env ]( const entity_t& e, ui_gfx_sort_t& sort, const ui_font_draw_env_t& draw, float alpha )
    {
        const c_ui_slider* slider = b->slider( e );
        if ( !slider )
            return false;
        ui_slider_draw_inputs_t inputs = env.m_slider ? env.m_slider( e ) : ui_slider_inputs_of( e, env );
        inputs.m_value = slider->value( );
        return ui_slider_draw( sort, draw, slider->m_s, slider->m_visible, slider->m_latched, inputs, alpha );
    };
    in.m_results = [ b, env ]( const entity_t& e, ui_gfx_sort_t& sort, const ui_font_draw_env_t& draw, float )
    {
        ui_results_table_t* table = b->results( e );
        if ( !table || !env.m_root )
            return false;
        ui_results_draw( sort, draw, *table, ui_results_inputs_of( e, env ), *env.m_root );
        return true;
    };
    // 0x451CA0 over the state 0x451370 built; with +0x294 set its end runs the award's vt+0x10.
    in.m_champ = [ b, env ]( const entity_t& e, ui_gfx_sort_t& sort, const ui_font_draw_env_t& draw, float )
    {
        ui_champ_state_t* state = b->champ( e );
        if ( !state || !env.m_champ )
            return false;
        ui_champ_t c = ui_champ_of( e, env.m_fontdb, &env.m_font_asset );
        ui_champ_inputs_t inputs;
        inputs.m_rows = &state->m_rows;
        inputs.m_boatdb = env.m_champ->m_boat_db;
        inputs.m_string = env.m_string7;
        ui_champ_draw( sort, draw, c, inputs );
        if ( state->m_award && env.m_champ->m_award_draw )
            env.m_champ->m_award_draw( );
        return true;
    };
    return in;
}
