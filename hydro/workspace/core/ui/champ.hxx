#pragma once

#include <workspace/core/assets/tree.hxx>
#include <workspace/core/ui/results.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// The championship standings table: VuChampionshipEntity (vtable 0x73C998, ctor
// 0x450070; Screens/Championship), its draw member 0x451CA0 and the cell draw
// 0x4527A0, transliterated from the disassembly (tools tmp/b27/d_451CA0.txt).
// The header / row blocks are 0x443C90 (results.hxx ui_results_rows_draw) - the
// same class of block as the race results table, here at +0x248; the highlighted
// cell has the shape of results.hxx's 0x46BAA0.
//
// INPUTS: the standings (+0x288..+0x28C, 16-byte containers), the row block's
// +0x260 / +0x264 / +0x26C - their writers (the load 0x451370 -> 0x454410..) are
// NOT recreated. +0x294 the award screen (0x47F...): not recreated - while it is
// set the draw answers UNKNOWN after the table (slot 4 vt+0x10 not run).
// Retail's operator[] 0x4FC970 INSERTS a key a row lacks and turns a non-object
// row into an object; the values drawn are the same as this const walk's, the
// document's later bytes are not.

struct ui_champ_t
{
    ui_results_table_t m_table;       // +0x60.. the block +0x248..+0x26C, Depth (component +0x60), Position, Screen Scale, Text Highlight Color
    float m_place[ 4 ] = { };         // +0x78 PlaceText X, Y, Width, Height
    ui_results_font_t m_place_font;   // +0x88 (params +0xA8, asset +0xE8)
    float m_points[ 4 ] = { };        // +0xEC PointsText
    ui_results_font_t m_points_font;  // +0xFC
    float m_name[ 4 ] = { };          // +0x160 NameText
    ui_results_font_t m_name_font;    // +0x170
    float m_boat[ 4 ] = { };          // +0x1D4 BoatText
    ui_results_font_t m_boat_font;    // +0x1E4
    bool m_award = false;             // +0x294 set
};

struct ui_champ_inputs_t
{
    const value_t* m_rows = nullptr;  // the standings, an array
    const value_t* m_boatdb = nullptr;   // gameutil (+8) +0x58: the BoatDB document
    // VuStringDB slot 7 (vt+0x1C) of a StringID.
    std::function< std::string( const std::string& id ) > m_string;
};

// 0x451CA0 (thiscall; alpha unread; ret 4): per row the place (index + 1), the
// points, the driver and the boat cells, each row VerticalSpacing lower; then the
// IsPlayer flags into the block's array (0, else -1) and the block. false: a path
// not recreated.
inline bool ui_champ_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, ui_champ_t& c, const ui_champ_inputs_t& in )
{
    static const value_t none{ };
    ui_results_table_t& t = c.m_table;
    const std::size_t rows = in.m_rows && in.m_rows->tag == 5 ? in.m_rows->list.size( ) : 0u;
    if ( rows > static_cast< std::size_t >( t.m_capacity ) )
        return false;   // retail writes past its array
    if ( rows && !in.m_string )
        return false;
    const float x = t.m_pos[ 0 ];
    float y = t.m_pos[ 1 ];
    for ( std::size_t i = 0; i < rows; ++i )
    {
        const value_t& row = in.m_rows->list[ i ];
        const value_t& properties = member( row, "Properties" );
        // the boat's StringID: BoatDB "VuDBAsset" [ Boat ] (0x4F22B0, the exact name) "StringID"
        const value_t& boat = member( properties, "Boat" );
        const std::string boat_name = boat.tag == 4 ? boat.text : std::string( );
        const value_t* entry = &none;
        if ( in.m_boatdb )
        {
            const value_t& db = member( *in.m_boatdb, "VuDBAsset" );
            if ( db.tag == 6 )
                for ( const auto& field : db.object )
                    if ( field.first == boat_name )
                    {
                        entry = &field.second;
                        break;
                    }
        }
        const value_t& id = member( *entry, "StringID" );
        const std::string boat_text = in.m_string( id.tag == 4 ? id.text : std::string( ) );
        const value_t& player = member( row, "IsPlayer" );
        const bool highlight = player.tag == 3 && ( player.integer & 0xFF ) != 0;
        const std::string place = ui_results_itoa( static_cast< std::int32_t >( i ) + 1 );
        ui_results_plain_draw( sort, env, t, c.m_place_font, place.c_str( ), c.m_place[ 0 ] + x, y + c.m_place[ 1 ], c.m_place[ 2 ], c.m_place[ 3 ] );
        const value_t& points = member( row, "Points" );
        std::int32_t value = 0;
        if ( points.tag == 1 )
            value = points.integer;
        else if ( points.tag == 2 )
            value = ui_cvtt( points.real );
        else if ( points.tag == 7 )
            value = static_cast< std::int32_t >( points.int64 );
        const std::string score = ui_results_itoa( value );
        const float pr[ 4 ] = { x + c.m_points[ 0 ], c.m_points[ 1 ] + y, c.m_points[ 2 ], c.m_points[ 3 ] };
        ui_results_text_draw( sort, env, t, c.m_points_font, pr, score.c_str( ), highlight );
        const value_t& driver = member( row, "DriverName" );
        const float nr[ 4 ] = { c.m_name[ 0 ] + x, c.m_name[ 1 ] + y, c.m_name[ 2 ], c.m_name[ 3 ] };
        ui_results_text_draw( sort, env, t, c.m_name_font, nr, driver.tag == 4 ? driver.text.c_str( ) : "", highlight );
        const float br[ 4 ] = { c.m_boat[ 0 ] + x, c.m_boat[ 1 ] + y, c.m_boat[ 2 ], c.m_boat[ 3 ] };
        ui_results_text_draw( sort, env, t, c.m_boat_font, br, boat_text.c_str( ), highlight );
        y = t.m_spacing + y;
    }
    for ( std::size_t i = 0; i < rows; ++i )
    {
        const value_t& player = member( in.m_rows->list[ i ], "IsPlayer" );
        t.m_pads[ i ] = player.tag == 3 && ( player.integer & 0xFF ) != 0 ? 0 : -1;
    }
    ui_results_rows_draw( sort, env, t, ui_results_depth( t ) );
    return !c.m_award;
}
