#pragma once

#include <workspace/core/ui/champ.hxx>
#include <workspace/core/ui/frame.hxx>
#include <workspace/core/ui/pages.hxx>
#include <workspace/core/ui/remap.hxx>

#include <cstdint>
#include <cstring>
#include <string>

// The loaded fields of the three leaves wired into the frame in batch 28: the
// property names verify_ui_page_layout / verify_ui_remap_draw / verify_ui_
// championship_draw compare with retail's bytes. The runtime fields (the page's
// elements and scroll, the row's selection and textures, the standings) are the
// owner's: c_ui_binding, or a check's replay.

inline ui_page_layout_t ui_page_layout_of( const entity_t& e )
{
    ui_page_layout_t p;
    ui_frame_take( e, "Visible", &p.m_visible, 1 );
    ui_frame_take( e, "Screen Scale", p.m_screen_scale, 8 );
    ui_frame_take( e, "X", &p.m_rect[ 0 ], 4 );
    ui_frame_take( e, "Y", &p.m_rect[ 1 ], 4 );
    ui_frame_take( e, "Width", &p.m_rect[ 2 ], 4 );
    ui_frame_take( e, "Height", &p.m_rect[ 3 ], 4 );
    ui_frame_take( e, "Horizontal Anchor", &p.m_anchor.m_horizontal, 4 );
    ui_frame_take( e, "Vertical Anchor", &p.m_anchor.m_vertical, 4 );
    ui_frame_take( e, "Horizontal Ratio", &p.m_anchor.m_h_ratio, 4 );
    ui_frame_take( e, "Vertical Ratio", &p.m_anchor.m_v_ratio, 4 );
    ui_frame_take( e, "Scroll at Start", &p.m_scroll_at_start, 1 );
    ui_frame_take( e, "Scroll Speed", &p.m_scroll_speed, 4 );
    ui_frame_take( e, "Scroll Lag", &p.m_scroll_lag, 4 );
    ui_frame_take( e, "Scroll Residual Damping", &p.m_damping, 4 );
    ui_frame_take( e, "Touchable", &p.m_touchable, 1 );
    ui_frame_take( e, "Scrolling", &p.m_scrolling, 1 );
    if ( const ui_slot_t* s = ui_property_value( e, "Page Layout" ) )
        p.m_page = s->m_text;
    p.m_depth = ui_frame_depth( e );
    return p;
}

// The params' UNKNOWN bytes 41..59 are the caller's (0 by default).
inline ui_remap_t ui_remap_of( const entity_t& e, const value_t* fontdb )
{
    ui_remap_t r;
    ui_frame_take( e, "Visible", &r.m_visible, 1 );
    ui_frame_take( e, "Screen Scale", r.m_scale, 8 );
    ui_frame_take( e, "Pos", r.m_pos, 8 );
    ui_frame_take( e, "Label Offset", r.m_label_offset, 8 );
    ui_frame_take( e, "Control Offset", r.m_control_offset, 8 );
    ui_frame_take( e, "Control Selected Text Color", &r.m_selected_colour, 4 );
    ui_frame_take( e, "Config", &r.m_config, 4 );
    static const char* const k_rects[ 3 ] = { "Control Image", "Touch", "Image" };
    static const char* const k_xywh[ 4 ] = { " X", " Y", " Width", " Height" };
    float* const rects[ 3 ] = { r.m_control_image, r.m_touch, r.m_image };
    for ( int k = 0; k < 3; ++k )
        for ( int c = 0; c < 4; ++c )
            ui_frame_take( e, ( std::string( k_rects[ k ] ) + k_xywh[ c ] ).c_str( ), &rects[ k ][ c ], 4 );
    const auto text = [ &e ]( const char* name ) { const ui_slot_t* s = ui_property_value( e, name ); return s ? s->m_text : std::string( ); };
    r.m_type = text( "Type" );
    r.m_channel = text( "Channel Name" );
    r.m_label = text( "Label String ID" );
    if ( fontdb )
    {
        std::string asset;
        ui_font_params( *fontdb, text( "Label Font" ), r.m_label_params, asset );
        ui_font_params( *fontdb, text( "Control Font" ), r.m_control_params, asset );
    }
    r.m_depth = ui_frame_depth( e );
    return r;
}

// `font_asset`: FontDB's "FontAsset" -> the asset's +88 (null: the fonts stay 0, as before).
inline ui_champ_t ui_champ_of( const entity_t& e, const value_t* fontdb, const std::function< std::uint32_t( const std::string& ) >* font_asset = nullptr )
{
    ui_champ_t c;
    ui_results_table_t& t = c.m_table;
    ui_frame_take( e, "Screen Scale", t.m_scale, 8 );
    ui_frame_take( e, "Position X", &t.m_pos[ 0 ], 4 );
    ui_frame_take( e, "Position Y", &t.m_pos[ 1 ], 4 );
    ui_frame_take( e, "Text Highlight Color", &t.m_highlight, 4 );
    static const char* const k_cells[ 4 ] = { "PlaceText", "PointsText", "NameText", "BoatText" };
    static const char* const k_fonts[ 4 ] = { "PlaceFont", "PointsFont", "NameFont", "BoatFont" };
    static const char* const k_xywh[ 4 ] = { "X", "Y", "Width", "Height" };
    float* const cells[ 4 ] = { c.m_place, c.m_points, c.m_name, c.m_boat };
    ui_results_font_t* const fonts[ 4 ] = { &c.m_place_font, &c.m_points_font, &c.m_name_font, &c.m_boat_font };
    for ( int k = 0; k < 4; ++k )
    {
        for ( int j = 0; j < 4; ++j )
            ui_frame_take( e, ( std::string( k_cells[ k ] ) + k_xywh[ j ] ).c_str( ), &cells[ k ][ j ], 4 );
        const ui_slot_t* s = ui_property_value( e, k_fonts[ k ] );
        std::string asset;
        if ( fontdb )
            ui_font_params( *fontdb, s ? s->m_text : std::string( ), fonts[ k ]->m_params, asset );
        if ( font_asset && *font_asset )
            fonts[ k ]->m_font = ( *font_asset )( asset );
    }
    ui_frame_take( e, "Width", &t.m_width, 4 );
    ui_frame_take( e, "VerticalSpacing", &t.m_spacing, 4 );
    ui_frame_take( e, "HeaderColor", &t.m_header, 4 );
    ui_frame_take( e, "InactiveColor", &t.m_inactive, 4 );
    ui_frame_take( e, "ActiveColor", &t.m_active, 4 );
    ui_frame_take( e, "PlayerColor", &t.m_player, 4 );
    t.m_depth = ui_frame_depth( e );
    return c;
}
