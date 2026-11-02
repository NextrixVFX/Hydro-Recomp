#pragma once

#include <workspace/core/ui/bind.hxx>
#include <workspace/core/ui/frame.hxx>
#include <workspace/core/ui/pages.hxx>
#include <workspace/core/ui/remap.hxx>
#include <workspace/core/ui/results.hxx>

#include <functional>
#include <string>

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
// What stays out, each an m_* the caller may set after the call: m_info (the ticker), m_config / m_combo (the selector runtime),
// m_stat, m_bar, m_difficulty, m_message_box, m_board / m_leaderboard (their fonts and the platform leaderboard rows).
struct ui_screen_env_t
{
    ui_font_draw_env_t m_draw;                                                     // the materials
    ui_string_db_t m_db;                                                           // 0x5546B0's "right-ea-left" answer
    const value_t* m_fontdb = nullptr;                                             // FontDB's document
    const ui_crop_t* m_crop = nullptr;                                             // VuUIImpl's crop
    std::function< std::string( const std::string& id ) > m_string6;               // VuStringDB slot 6 (world/strings.hxx)
    std::function< std::string( const std::string& id ) > m_string7;               // VuStringDB slot 7
    std::function< std::string( const entity_t& ) > m_text;                        // a text leaf's slot 12 (per type)
    std::function< ui_text_font_t( const entity_t& ) > m_font;                     // 0x626B80
    std::function< const ui_texture_t*( const entity_t&, int which ) > m_texture;  // an image's slot 14, a button's three
    std::function< std::uint32_t( const std::string& asset ) > m_font_asset;       // FontDB's "FontAsset" -> the asset's +88, 0 none
    ui_page_hooks_t m_page;                                                        // the page draw's hooks
    ui_page_env_t m_page_env;                                                      // m_draw is replaced by each leaf's
    std::function< ui_remap_inputs_t( const entity_t& ) > m_remap;                 // a row's textures and fonts
    const value_t* m_root = nullptr;                                               // gameutil +0x50 (GameResults)
    const ui_champ_env_t* m_champ = nullptr;                                       // the championship's (m_boat_db, the award's draw)
};

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

inline ui_frame_inputs_t ui_screen_inputs( c_ui_binding& binding, ui_screen_env_t env )
{
    ui_frame_inputs_t in;
    in.m_env = env.m_draw;
    in.m_db = env.m_db;
    in.m_fontdb = env.m_fontdb;
    in.m_text = env.m_text;
    in.m_font = env.m_font;
    in.m_texture = env.m_texture;
    in.m_string = env.m_string7;
    c_ui_binding* b = &binding;
    in.m_button = [ b ]( const entity_t& e, std::uint8_t& focused, std::uint32_t& press ) { b->button_state( e, focused, press ); };
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
        return row && env.m_remap && ui_remap_draw( sort, draw, env.m_db, *row, env.m_remap( e ), alpha );
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
