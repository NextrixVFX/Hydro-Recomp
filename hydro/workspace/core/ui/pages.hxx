#pragma once

#include <workspace/core/assets/tree.hxx>
#include <workspace/core/ui/draw.hxx>
#include <workspace/core/ui/gauge.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// The credits page: VuUIPageLayoutEntity (factory 0x62CA50, vtable 0x6F72C4;
// Screens/Credits). A vertical list of elements loaded from PageLayoutDB that
// scrolls through a rect. Transliterated from the disassembly (tools
// tmp/b26/d_62E210.txt, d_62FED0.txt, d_62FA90.txt; tmp/b27/d_630100.txt):
//   0x62E120  the UI draw member: Visible (+0x68) -> 0x62E210 (alpha)
//   0x62E140  the Vu2dLayoutComponent draw: (byte) the anchored rect white, then 0x62E210 at 1.0
//   0x62E210  the rect over the screen scale through the anchor (+0x84), the scroll, -> 0x62FED0
//   0x62FED0  the element list (+0xC4): per element its height (slot 1), its draw (slot 2) once it reaches the rect
//   0x62E3B0  the load (slot 2 0x62D990, and the "Page Layout" property's change callback): the entry ->
//             0x62FA90 -> the element loaders 0x630100 (Text) / 0x630610 (Image) / 0x630B40 (Space); +0xD0 = the heights' sum
// The three element types: Text (0x84 bytes, vtable 0x6F7534: height 0x6303B0, draw 0x6304D0), Image (0x20,
// 0x6F7554: 0x630800 / 0x630830), Space (8, 0x6F7564: 0x630BD0 / 0x4FB990 = ret 0x14, nothing).
// HOOKS: the font measure 0x5370F0 (the font engine's glyph metrics - NOT recreated), VuStringDB (g 0x759078)
// slot 7 (vt+0x1C, the text of a StringID: the string tables are not loaded), the asset factory's texture / font
// asset creates. The scroll +0xE4 is the tick's (0x62DA30) runtime state.

enum class ui_page_kind_t
{
    text,    // 0x630100
    image,   // 0x630610
    space    // 0x630B40
};

struct ui_page_element_t
{
    ui_page_kind_t m_kind = ui_page_kind_t::space;
    // Text: +4 its VuUIFont (ctor 0x626900).
    std::string m_font;                  // +0xC the VuUIFont's name (+8) - "Font"
    std::uint8_t m_params[ 60 ] = { };   // +0x24 its draw params (+0x20). UNKNOWN: bytes 41..59 (the ctor never writes them)
    bool m_reloaded = false;             // 0x626B00 reloaded it (a name other than the ctor's "")
    std::string m_font_asset;            // the reload's FontDB "FontAsset" (its factory requests)
    bool m_has_asset = false;            // +0x64 (the font's +0x60): the factory's create - a HOOK answer
    std::uint32_t m_asset_font = 0;      // that asset's +0x58
    std::string m_string_id;             // +0x68 "StringID"
    std::uint32_t m_flags = 0x80u;       // +0x80 the font flags (0x80, Align: Right 1 / Center 2 / RightEaLeft)
    // Image.
    std::string m_texture_name;          // "Texture"
    const ui_texture_t* m_texture = nullptr;   // +4 the asset (null: none); handle = its +0x58, size its +0xC / +0x10
    std::string m_align;                 // +8 "Align"
    // Space.
    float m_amount = 0.0f;               // +4 "Amount"
};

struct ui_page_layout_t
{
    std::uint8_t m_visible = 1;                          // +0x68 "Visible"
    float m_screen_scale[ 2 ] = { 1080.0f, 720.0f };    // +0x6C "Screen Scale"
    float m_rect[ 4 ] = { 0.0f, 0.0f, 100.0f, 100.0f }; // +0x74 X, Y, Width, Height
    ui_anchor_t m_anchor;                                // +0x84 / +0x88 anchors, +0x8C / +0x90 ratios
    std::string m_page;                                  // +0x94 "Page Layout" (the PageLayoutDB entry)
    std::uint8_t m_scroll_at_start = 0;                  // +0xAC
    float m_scroll_speed = 20.0f;                        // +0xB0
    float m_scroll_lag = 0.1f;                           // +0xB4
    float m_damping = ui_f32( 0x3F7D70A4u );             // +0xB8 "Scroll Residual Damping" (0.99)
    std::uint8_t m_touchable = 1;                        // +0xBC
    std::uint8_t m_scrolling = 1;                        // +0xBD
    std::vector< ui_page_element_t > m_elements;         // +0xC4
    float m_total = 0.0f;                                // +0xD0 the elements' heights (the load's)
    // runtime (the tick 0x62DA30, the plugs, the touch callbacks at +0x5C)
    std::uint8_t m_active = 0;                           // +0xD4 scrolling (StartScroll / StopScroll; Scroll at Start)
    std::uint8_t m_ended = 0;                            // +0xD5 OnReachedEnd fired
    std::uint8_t m_held = 0;                             // +0xD6 a touch holds the page
    float m_touch[ 2 ] = { 0.0f, 0.0f };                 // +0xD8 / +0xDC the touch-down point (VuUIImpl's inverse crop)
    float m_grab = 0.0f;                                 // +0xE0 +0xE8 at touch-down
    float m_scroll = 0.0f;                               // +0xE4 the drawn scroll (smoothed toward +0xE8)
    float m_target = 0.0f;                               // +0xE8
    float m_velocity = 0.0f;                             // +0xEC the smoothing's
    float m_residual = 0.0f;                             // +0xF0 the speed a release leaves (decays)
    float m_depth = 0.0f;                                // the draw component (+0x64) +0x14 "Depth"
};

// What the draws read besides the entity: the font draw's environment, VuUIImpl's
// crop matrix (the anchor) and its default font (0x81A18C -> +0x58: 0x626B80's
// fallback when the element's VuUIFont has no asset).
struct ui_page_env_t
{
    ui_font_draw_env_t m_draw;
    ui_crop_t m_crop;
    bool m_default = false;
    std::uint32_t m_default_font = 0;
};

struct ui_page_hooks_t
{
    ui_string_db_t m_db;   // g 0x759078 when a Text element loads (its "RightEaLeft" asks slot 12)
    // VuStringDB slot 7 (vt+0x1C) on a StringID: the text (copied by strlen - a NUL ends it).
    std::function< std::string( const std::string& id ) > m_string;
    // 0x5370F0 (fastcall out, font; text, params, width, flags, scale): out = the text's extent; false = UNKNOWN.
    std::function< bool( std::uint32_t font, const std::string& text, const std::uint8_t params[ 60 ], float width, std::uint32_t flags,
                         const float scale[ 2 ], float out[ 2 ] ) > m_measure;
    // The asset factory (g 0x759004) on an Image's Texture: exists 0x4269D0 then create 0x426A90. null: no asset.
    std::function< const ui_texture_t*( const std::string& name ) > m_texture;
    // The VuUIFont reload's factory exists / create on FontDB's FontAsset: true = an asset (+0x60), font = its +0x58.
    std::function< bool( const std::string& asset, std::uint32_t& font ) > m_font_asset;
};

// 0x626B80 on a Text element's VuUIFont: the asset's +0x58, else the default's, else none (0).
inline std::uint32_t ui_page_font( const ui_page_element_t& el, const ui_page_env_t& env )
{
    if ( el.m_has_asset )
        return el.m_asset_font;
    return env.m_default ? env.m_default_font : 0u;
}

inline std::string ui_page_text( const value_t& v )
{
    return v.tag == 4 ? v.text : std::string( );   // a string node, else the static empty string 0x819950
}

// The element loaders (thiscall on a fresh object; the element's container).
inline void ui_page_text_load( ui_page_element_t& el, const value_t& json, const value_t& fontdb, const ui_page_hooks_t& hooks )
{
    el.m_kind = ui_page_kind_t::text;
    // the ctor 0x626900's params (bytes 0..40); 41..59 left zero here (UNKNOWN in retail)
    static const value_t none{ };
    std::string unused;
    ui_font_params( none, std::string( ), el.m_params, unused );
    el.m_flags = 0x80u;
    // 0x626B00: a name other than the current one ("" from the ctor) is assigned and reloaded (0x626CF0)
    el.m_font = ui_page_text( member( json, "Font" ) );
    if ( el.m_font != std::string( ) )
    {
        el.m_reloaded = true;
        ui_font_params( fontdb, el.m_font, el.m_params, el.m_font_asset );
        el.m_has_asset = hooks.m_font_asset && hooks.m_font_asset( el.m_font_asset, el.m_asset_font );
        if ( !el.m_has_asset )
            el.m_asset_font = 0;
    }
    el.m_string_id = ui_page_text( member( json, "StringID" ) );
    const std::string align = ui_page_text( member( json, "Align" ) );
    if ( align == "Right" )
        el.m_flags |= 1u;
    else if ( align == "Center" )
        el.m_flags |= 2u;
    else if ( align == "RightEaLeft" && ( !hooks.m_db.m_present || !hooks.m_db.m_slot12 ) )
        el.m_flags |= 1u;
}

inline void ui_page_image_load( ui_page_element_t& el, const value_t& json, const ui_page_hooks_t& hooks )
{
    el.m_kind = ui_page_kind_t::image;
    el.m_texture_name = ui_page_text( member( json, "Texture" ) );
    el.m_texture = hooks.m_texture ? hooks.m_texture( el.m_texture_name ) : nullptr;
    el.m_align = ui_page_text( member( json, "Align" ) );
}

inline void ui_page_space_load( ui_page_element_t& el, const value_t& json )
{
    el.m_kind = ui_page_kind_t::space;
    const value_t& v = member( json, "Amount" );
    if ( v.tag == 1 )
        el.m_amount = static_cast< float >( v.integer );   // cvtdq2ps
    else if ( v.tag == 2 )
        el.m_amount = v.real;
    else if ( v.tag == 7 )
        el.m_amount = static_cast< float >( v.int64 );     // fild / fstp: one rounding
    else
        el.m_amount = 0.0f;
}

// 0x62FA90 (this = the list +0xC4; the entry): the old elements go (0x62FE90),
// then per item of entry["Elements"] (0x4FBC40: an array's size, else 0) by its
// "Type" string: exactly "Text", "Image" or "Space"; anything else is skipped.
inline void ui_page_elements_load( std::vector< ui_page_element_t >& out, const value_t& entry, const value_t& fontdb, const ui_page_hooks_t& hooks )
{
    out.clear( );
    const value_t& elements = member( entry, "Elements" );
    const int count = elements.tag == 5 ? static_cast< int >( elements.list.size( ) ) : 0;
    for ( int i = 0; i < count; ++i )
    {
        const value_t& json = elements.list[ static_cast< std::size_t >( i ) ];
        const std::string type = ui_page_text( member( json, "Type" ) );
        ui_page_element_t el;
        if ( type == "Text" )
            ui_page_text_load( el, json, fontdb, hooks );
        else if ( type == "Image" )
            ui_page_image_load( el, json, hooks );
        else if ( type == "Space" )
            ui_page_space_load( el, json );
        else
            continue;
        out.push_back( el );
    }
}

// Slot 1 (thiscall: width, scale*; the height on the x87 stack). false: a path
// not recreated (the measure hook unset) or a retail crash (an Image without its
// asset: 0x630800 reads the null asset's +0x58).
inline bool ui_page_element_height( const ui_page_element_t& el, float width, const float scale[ 2 ], const ui_page_env_t& env,
                                    const ui_page_hooks_t& hooks, float& height )
{
    if ( el.m_kind == ui_page_kind_t::space )
    {
        height = el.m_amount / scale[ 1 ];   // 0x630BD0
        return true;
    }
    if ( el.m_kind == ui_page_kind_t::image )
    {
        if ( !el.m_texture )
            return false;
        height = static_cast< float >( el.m_texture->m_height ) / scale[ 1 ];   // 0x630800: cvtdq2ps / scale.y
        return true;
    }
    // 0x6303B0: no font -> 0 (fldz); else the measure's y
    const std::uint32_t font = ui_page_font( el, env );
    if ( !font )
    {
        height = 0.0f;
        return true;
    }
    if ( !hooks.m_string || !hooks.m_measure )
        return false;
    const std::string text = hooks.m_string( el.m_string_id ).c_str( );   // VuString_assign( data, strlen )
    float out[ 2 ] = { 0.0f, 0.0f };
    if ( !hooks.m_measure( font, text, el.m_params, width, el.m_flags, scale, out ) )
        return false;
    height = out[ 1 ];
    return true;
}

// 0x630830 (thiscall: depth, rect*, y offset, alpha, scale*; ret 0x14): the
// texture at its size over the scale, placed by Align ("Right" / "Center", else
// the rect's left) at the rect's y + the offset, clipped to the rect (the uv
// shrunk alike), white with the alpha byte fistp( alpha x 255 ).
inline void ui_page_image_draw( ui_gfx_sort_t& sort, const ui_page_env_t& env, const ui_page_element_t& el, float depth, const float r[ 4 ],
                                float yoff, float alpha, const float scale[ 2 ] )
{
    if ( !el.m_texture )
        return;
    const float h = static_cast< float >( el.m_texture->m_height ) / scale[ 1 ];
    const float w = static_cast< float >( el.m_texture->m_width ) / scale[ 0 ];
    const float y = r[ 1 ] + yoff;
    const float half = ui_f32( 0x3F000000u );   // 0x6F765C
    float x;
    if ( el.m_align == "Right" )
        x = ( r[ 2 ] + r[ 0 ] ) - w;
    else if ( el.m_align == "Center" )
        x = ( r[ 2 ] * half + r[ 0 ] ) - w * half;
    else
        x = r[ 0 ];
    float left = 0.0f, top = 0.0f;
    if ( r[ 0 ] > x )   // comiss / jbe: unordered keeps 0
        left = r[ 0 ] - x;
    if ( r[ 1 ] > y )
        top = r[ 1 ] - y;
    const float xw = x + w;
    const float rr = r[ 2 ] + r[ 0 ];
    const float right = xw > rr ? rr - x : w;
    const float yh = y + h;
    const float rb = r[ 3 ] + r[ 1 ];
    const float bottom = yh > rb ? rb - y : h;
    // minss a, b = a < b ? a : b; maxss a, b = a > b ? a : b (the second operand on NaN / equal)
    const auto mins = []( float a, float b ) { return a < b ? a : b; };
    const auto maxs = []( float a, float b ) { return a > b ? a : b; };
    const float lo_x = mins( left, right ), hi_x = maxs( left, right );
    const float lo_y = mins( top, bottom ), hi_y = maxs( top, bottom );
    const float one = ui_f32( 0x3F800000u );   // 0x6F76B8
    const float iw = one / w;
    const float ih = one / h;
    const float uv[ 4 ] = { iw * lo_x, ih * lo_y, iw * ( hi_x - lo_x ), ih * ( hi_y - lo_y ) };
    const float dx = maxs( x, r[ 0 ] );
    const float dy = maxs( y, r[ 1 ] );
    const float dst[ 4 ] = { dx, dy, mins( xw, rr ) - dx, mins( yh, rb ) - dy };
    const std::uint32_t a = static_cast< std::uint32_t >( ui_fistp32( alpha * ui_f32( 0x437F0000u ) ) ) & 0xFFu;   // 0x6F7808: 255
    ui_texture_draw( sort, env.m_draw, el.m_texture->m_handle, 0x00FFFFFFu | ( a << 24 ), dst, uv, depth );   // 0x507360
}

// Slot 2 (thiscall: depth, rect*, y offset, alpha, scale*; ret 0x14). false: a
// path not recreated.
inline bool ui_page_element_draw( ui_gfx_sort_t& sort, const ui_page_env_t& env, const ui_page_hooks_t& hooks, const ui_page_element_t& el,
                                  float depth, const float rect[ 4 ], float yoff, float alpha, const float scale[ 2 ] )
{
    if ( el.m_kind == ui_page_kind_t::space )
        return true;   // 0x4FB990
    if ( el.m_kind == ui_page_kind_t::image )
    {
        ui_page_image_draw( sort, env, el, depth, rect, yoff, alpha, scale );
        return true;
    }
    // 0x6304D0: the font draw 0x536C10 of the StringID's text in the page rect at offset (0, yoff), the element's flags
    const std::uint32_t font = ui_page_font( el, env );
    if ( !font )
        return true;
    if ( !hooks.m_string )
        return false;
    const std::string text = hooks.m_string( el.m_string_id ).c_str( );
    const float offset[ 2 ] = { 0.0f, yoff };
    return ui_font_draw( sort, env.m_draw, font, text.c_str( ), el.m_params, rect, el.m_flags, alpha, offset, depth );
}

// 0x62FED0 (thiscall this = the list; rect*, alpha, scale*; the depth in xmm1,
// the scroll in xmm3; ret 0xC): y starts at scroll + rect.y; per element while
// the rect's bottom is not below y (comiss / jb: NaN stops), its height, and its
// draw when y + height has reached the rect's top (NaN skips it).
inline bool ui_page_list_draw( ui_gfx_sort_t& sort, const ui_page_env_t& env, const ui_page_hooks_t& hooks, const std::vector< ui_page_element_t >& list,
                               const float rect[ 4 ], float alpha, const float scale[ 2 ], float depth, float scroll )
{
    bool known = true;
    float y = scroll + rect[ 1 ];
    for ( const ui_page_element_t& el : list )
    {
        const float bottom = rect[ 3 ] + rect[ 1 ];
        if ( !( bottom >= y ) )
            break;
        float h = 0.0f;
        if ( !ui_page_element_height( el, rect[ 2 ], scale, env, hooks, h ) )
            return false;
        const float next = h + y;
        if ( next >= rect[ 1 ] )
            known = ui_page_element_draw( sort, env, hooks, el, depth, rect, y - rect[ 1 ], alpha, scale ) && known;
        y = next;
    }
    return known;
}

// 0x62E210 (thiscall; the alpha in xmm1): the rect over the screen scale through
// the anchor; with Scrolling the scroll = ((1 - t) x vis - total x t) / scale.y,
// t = +0xE4 / (scale.y x h + total), vis = scale.y x h, total = +0xD0 x scale.y
// (h from a second, identical anchored rect); without it 0 / scale.y.
inline bool ui_page_layout_contents_draw( ui_gfx_sort_t& sort, const ui_page_env_t& env, const ui_page_hooks_t& hooks, const ui_page_layout_t& p,
                                          float alpha )
{
    const float one = ui_f32( 0x3F800000u );   // 0x6F76B8
    const auto anchored = [ & ]( float out[ 4 ] )
    {
        const float ix = one / p.m_screen_scale[ 0 ];
        const float iy = one / p.m_screen_scale[ 1 ];
        const float src[ 4 ] = { ix * p.m_rect[ 0 ], p.m_rect[ 1 ] * iy, p.m_rect[ 2 ] * ix, p.m_rect[ 3 ] * iy };
        ui_anchor_transform( p.m_anchor, env.m_crop, src, out );   // 0x6282D0 (+0x84)
    };
    float rect[ 4 ];
    anchored( rect );
    const float sy = p.m_screen_scale[ 1 ];
    const float vis = sy * rect[ 3 ];
    float scroll = 0.0f;
    if ( p.m_scrolling )
    {
        float second[ 4 ];
        anchored( second );
        const float total = p.m_total * sy;
        const float t = p.m_scroll / ( sy * second[ 3 ] + total );
        scroll = ( one - t ) * vis - total * t;
    }
    scroll = scroll / sy;
    const float depth = p.m_depth * ui_f32( 0x3BA3D70Au ) + ui_f32( 0x3F000000u );   // 0x6F75C4, 0x6F765C
    return ui_page_list_draw( sort, env, hooks, p.m_elements, rect, alpha, p.m_screen_scale, depth, scroll );
}

// 0x62E120 (the UI draw member; alpha; ret 4).
inline bool ui_page_layout_draw( ui_gfx_sort_t& sort, const ui_page_env_t& env, const ui_page_hooks_t& hooks, const ui_page_layout_t& p, float alpha )
{
    if ( !p.m_visible )
        return true;
    return ui_page_layout_contents_draw( sort, env, hooks, p, alpha );
}

// 0x62E140 (the Vu2dLayoutComponent draw; byte; ret 4): with the byte the
// anchored rect white (0x507A00), then 0x62E210 at 1.0 - Visible is not tested.
inline bool ui_page_layout_layout_draw( ui_gfx_sort_t& sort, const ui_page_env_t& env, const ui_page_hooks_t& hooks, const ui_page_layout_t& p,
                                        std::uint8_t layout )
{
    const float one = ui_f32( 0x3F800000u );
    if ( layout )
    {
        const float ix = one / p.m_screen_scale[ 0 ];
        const float iy = one / p.m_screen_scale[ 1 ];
        const float src[ 4 ] = { p.m_rect[ 0 ] * ix, p.m_rect[ 1 ] * iy, p.m_rect[ 2 ] * ix, p.m_rect[ 3 ] * iy };
        float rect[ 4 ];
        ui_anchor_transform( p.m_anchor, env.m_crop, src, rect );
        ui_rect_draw( sort, env.m_draw, 0xFFFFFFFFu, rect, p.m_depth * ui_f32( 0x3BA3D70Au ) + ui_f32( 0x3F000000u ) );
    }
    return ui_page_layout_contents_draw( sort, env, hooks, p, one );
}

// 0x62E3B0 (slot 2 0x62D990): the "Page Layout" entry of PageLayoutDB's
// "VuDBAsset" map (the whole name; a missing DB asset or entry: the null
// container) -> 0x62FA90; then +0xD0 = the sum of every element's height at
// width Width / Screen Scale x (unanchored) - height + sum, in order. false: a
// height not recreated.
inline bool ui_page_layout_load( ui_page_layout_t& p, const value_t* pagedb, const value_t& fontdb, const ui_page_env_t& env, const ui_page_hooks_t& hooks )
{
    static const value_t none{ };
    const value_t& db = pagedb ? member( *pagedb, "VuDBAsset" ) : none;
    const value_t* entry = &none;
    if ( db.tag == 6 )
        for ( const auto& field : db.object )
            if ( field.first == p.m_page )
            {
                entry = &field.second;
                break;
            }
    ui_page_elements_load( p.m_elements, *entry, fontdb, hooks );
    const float width = p.m_rect[ 2 ] / p.m_screen_scale[ 0 ];
    float total = 0.0f;
    for ( const ui_page_element_t& el : p.m_elements )
    {
        float h = 0.0f;
        if ( !ui_page_element_height( el, width, p.m_screen_scale, env, hooks, h ) )
            return false;
        total = h + total;
    }
    p.m_total = total;
    return true;
}

// ---- the runtime half (batch 27): the tick, the plugs, the touch callbacks ----

// The CRT's __libm_sse2_powf (a DLL import - its code is not in the image):
// SUBSTITUTION, the float rounding of the double result - as the oracle's stub
// computes it and as engine dynamics world_powf; NOT proven bit-equal to the CRT.
inline float ui_page_powf( float x, float y ) { return static_cast< float >( std::pow( static_cast< double >( x ), static_cast< double >( y ) ) ); }

// The rect over the screen scale through the anchor, as the tick 0x62DA30 and
// the touch callbacks compute it (X x ix, Y x iy, W x ix, H x iy).
inline void ui_page_rect( const ui_page_layout_t& p, const ui_crop_t& crop, float out[ 4 ] )
{
    const float one = ui_f32( 0x3F800000u );
    const float ix = one / p.m_screen_scale[ 0 ];
    const float iy = one / p.m_screen_scale[ 1 ];
    const float src[ 4 ] = { p.m_rect[ 0 ] * ix, p.m_rect[ 1 ] * iy, p.m_rect[ 2 ] * ix, p.m_rect[ 3 ] * iy };
    ui_anchor_transform( p.m_anchor, crop, src, out );
}

// Slot 6 onGameInitialize 0x62D9A0: +0xE4..+0xF0 = 0; Scroll at Start sets
// +0xD4; Touchable registers the touch callbacks (this + 0x5C, priority 0) with
// the touch device g 0x759070 (0x53FB90) - `touch_add` (null: none).
inline void ui_page_layout_initialize( ui_page_layout_t& p, const std::function< void( ) >& touch_add )
{
    p.m_scroll = p.m_target = p.m_velocity = p.m_residual = 0.0f;
    if ( p.m_scroll_at_start )
        p.m_active = 1;
    if ( p.m_touchable && touch_add )
        touch_add( );
}

// Slot 7 onGameRelease 0x62DA10: Touchable -> the touch device's remove 0x53FA60 (this + 0x5C).
inline void ui_page_layout_release( const ui_page_layout_t& p, const std::function< void( ) >& touch_remove )
{
    if ( p.m_touchable && touch_remove )
        touch_remove( );
}

// The input plugs (each returns VuRetVal 0): StartScroll 0x62E0A0, StopScroll
// 0x62E0C0, ResetScroll 0x62E0E0.
inline void ui_page_layout_start( ui_page_layout_t& p ) { p.m_active = 1; }
inline void ui_page_layout_stop( ui_page_layout_t& p ) { p.m_active = 0; }
inline void ui_page_layout_reset( ui_page_layout_t& p )
{
    p.m_scroll = p.m_target = p.m_velocity = p.m_residual = 0.0f;
    p.m_ended = 0;
}

// The tick 0x62DA30 (its VuParams argument copied, never read): dt = the tick
// manager g 0x759008 slot 15 (an input). Nothing unless +0xD4 and not +0xD5.
// Not held: +0xF0 *= powf( 1 - damping, dt ), +0xE8 += (speed + +0xF0) x dt.
// Then +0xE4 follows +0xE8 by the critically damped spring (k = 2 / lag, x = k
// dt, 1 / (1 + x + 0.48 x^2 + 0.235 x^3)); past the end (+0xE4 > +0xD0 x sy + sy
// x h, ordered) +0xD5 = 1 and the "OnReachedEnd" output plug fires (`reached`).
inline void ui_page_layout_tick( ui_page_layout_t& p, const ui_crop_t& crop, float dt, const std::function< void( ) >& reached )
{
    if ( !p.m_active || p.m_ended )
        return;
    const float one = ui_f32( 0x3F800000u );   // 0x6F76B8
    if ( !p.m_held )
    {
        const float f = ui_page_powf( one - p.m_damping, dt ) * p.m_residual;
        p.m_residual = f;
        p.m_target = ( p.m_scroll_speed + f ) * dt + p.m_target;
    }
    const float k = ui_f32( 0x40000000u ) / p.m_scroll_lag;   // 0x6F76F8: 2
    const float diff = p.m_scroll - p.m_target;
    const float x = k * dt;
    const float den = ( x * ui_f32( 0x3EF5C28Fu ) ) * x + ( x + one ) + ( ( x * ui_f32( 0x3E70A3D7u ) ) * x ) * x;   // 0x6F7658 0.48, 0x6F7624 0.235
    const float inv = one / den;
    const float temp = ( diff * k + p.m_velocity ) * dt;
    p.m_velocity = ( p.m_velocity - temp * k ) * inv;
    p.m_scroll = ( temp + diff ) * inv + p.m_target;
    float rect[ 4 ];
    ui_page_rect( p, crop, rect );
    const float sy = p.m_screen_scale[ 1 ];
    if ( p.m_scroll > p.m_total * sy + sy * rect[ 3 ] )
    {
        p.m_ended = 1;
        if ( reached )
            reached( );
    }
}

// The touch callbacks (vtable at +0x5C; VuUIImpl's inverse crop matrix +0x4C
// 0x81A144 maps the device point: x' = x m0 + y m4 + m12, y' = x m1 + y m5 + m13).
// Down 0x62DD20 (point*): inside the anchored rect (ordered compares) -> +0xD8 /
// +0xDC = the point, +0xE0 = +0xE8, +0xD6 = 1.
inline void ui_page_layout_touch_down( ui_page_layout_t& p, const ui_crop_t& crop, const float inverse[ 16 ], const float point[ 2 ] )
{
    const float y = ( point[ 0 ] * inverse[ 1 ] + point[ 1 ] * inverse[ 5 ] ) + inverse[ 13 ];
    const float x = ( point[ 0 ] * inverse[ 0 ] + point[ 1 ] * inverse[ 4 ] ) + inverse[ 12 ];
    float r[ 4 ];
    ui_page_rect( p, crop, r );
    if ( !( x >= r[ 0 ] ) || !( r[ 2 ] + r[ 0 ] >= x ) || !( y >= r[ 1 ] ) || !( r[ 3 ] + r[ 1 ] >= y ) )
        return;
    p.m_touch[ 0 ] = x;
    p.m_touch[ 1 ] = y;
    p.m_grab = p.m_target;
    p.m_held = 1;
}

// Up 0x62DED0: +0xD6 = 0, +0xF0 = +0xEC (the spring's speed carries on).
inline void ui_page_layout_touch_up( ui_page_layout_t& p )
{
    p.m_held = 0;
    p.m_residual = p.m_velocity;
}

// Move 0x62DEF0: while held, the device's touch 0 position (g 0x759070 slot 7
// vt+0x1C( 0, out ): an input) -> +0xE8 = +0xE0 - (y' - +0xDC) x sy, clamped by
// minss / maxss to [0, sy x h + +0xD0 x sy].
inline void ui_page_layout_touch_move( ui_page_layout_t& p, const ui_crop_t& crop, const float inverse[ 16 ], const float point[ 2 ] )
{
    if ( !p.m_held )
        return;
    const float sy = p.m_screen_scale[ 1 ];
    const float y = ( point[ 0 ] * inverse[ 1 ] + point[ 1 ] * inverse[ 5 ] ) + inverse[ 13 ];
    p.m_target = p.m_grab - ( y - p.m_touch[ 1 ] ) * sy;
    float r[ 4 ];
    ui_page_rect( p, crop, r );
    const float end = sy * r[ 3 ] + p.m_total * sy;
    const float lo = end < p.m_target ? end : p.m_target;   // minss end, target
    p.m_target = 0.0f > lo ? 0.0f : lo;                     // maxss 0, that
}