#pragma once

#include <workspace/core/ui/draw.hxx>
#include <workspace/core/ui/gauge.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// The control remapping row: VuInputRemappingEntity (vtable 0x73D4F4, ctor
// 0x45FD30) and VuThrottleRemappingEntity (0x73D45C, ctor 0x462910: only slots
// 15..17 differ; Screens/ControlsGamepad). Transliterated from the disassembly
// (tools tmp/b27/d_461200.txt, d_461B00.txt):
//   0x45FB90  the UI draw member: Visible (+0x6C) -> 0x461B00 (alpha in xmm1)
//   0x461A30  the Vu2dLayoutComponent draw: (byte) the touch rect white, then 0x461B00 at 1.0
//   0x461B00  the row: background image, label, the binding's text and icon
//   0x4626D0  the binding: Type picks the input manager's slot, Channel Name + Config its arguments
// HOOKS: the input manager g 0x759038 (slots 10 / 11 / 12), the input channel
// tables g 0x759040, VuStringDB g 0x759078 slots 6 / 7 / 9 (the string tables are
// not loaded). The textures +0x208.. +0x21C are the ctor's asset creates.

// 0x5390A0's table 0x7358B8 (0x51 names, static .data), past it "" 0x64D303.
inline const char* ui_key_name( std::uint32_t key )
{
    static const char* const k_names[ 0x51 ] = {
        "NONE", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P",
        "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "BACK", "ENTER", "SPACE", "TAB", "COMMA", "PERIOD", "MINUS", "PLUS", "APOSTROPHE",
        "SEMICOLON", "SLASH", "GRAVE", "BACKSLASH", "LEFT BRACKET", "RIGHT BRACKET", "LEFT", "RIGHT", "UP", "DOWN", "CENTER", "SHIFT", "ALT",
        "CONTROL", "ESCAPE", "SCROLL", "PAUSE", "INSERT", "DELETE", "HOME", "END", "PRIOR", "NEXT", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8",
        "F9", "F10", "F11", "F12" };
    return key < 0x51u ? k_names[ key ] : "";
}

struct ui_remap_t
{
    std::uint8_t m_visible = 1;                          // +0x6C "Visible"
    float m_scale[ 2 ] = { 1080.0f, 720.0f };            // +0x70 "Screen Scale"
    float m_pos[ 2 ] = { 0.0f, 0.0f };                   // +0x78 "Pos"
    float m_label_offset[ 2 ] = { 0.0f, 0.0f };          // +0x80 "Label Offset"
    ui_text_font_t m_label_font;                         // +0x88 the VuUIFont "Label Font"
    std::uint8_t m_label_params[ 60 ] = { };             // +0xA8 its draw params
    std::string m_label;                                 // +0xEC "Label String ID"
    float m_control_offset[ 2 ] = { 0.0f, 0.0f };        // +0x104 "Control Offset"
    float m_control_image[ 4 ] = { };                    // +0x10C "Control Image" X, Y, Width, Height
    ui_text_font_t m_control_font;                       // +0x11C the VuUIFont "Control Font"
    std::uint8_t m_control_params[ 60 ] = { };           // +0x13C its draw params
    std::uint32_t m_selected_colour = 0xFFFFFFFFu;       // +0x180 "Control Selected Text Color"
    float m_touch[ 4 ] = { };                            // +0x184 "Touch" X, Y, Width, Height
    float m_image[ 4 ] = { };                            // +0x194 "Image" X, Y, Width, Height
    std::string m_type;                                  // +0x1A4 "Type"
    std::string m_channel;                               // +0x1BC "Channel Name"
    std::int32_t m_config = 0;                           // +0x1EC "Config"
    std::uint8_t m_selected = 0;                         // +0x1F0 runtime (INPUT here)
    std::int32_t m_hover = 0;                            // +0x22C runtime (INPUT here)
    std::uint32_t m_textures[ 6 ] = { };                 // +0x208 axis, +0x20C button, +0x210 key icons; +0x214 normal, +0x218 selected, +0x21C hover background (handle = the asset's +0x58; 0: none)
    float m_depth = 0.0f;                                // the draw component (+0x68) +0x14 "Depth"
};

struct ui_remap_inputs_t
{
    bool m_input = false;                                // g 0x759038 present
    bool m_throttle = false;                             // VuThrottleRemappingEntity's vtable
    std::uint8_t m_auto[ 2 ] = { 0, 0 };                 // g 0x758FE0 +0x2C / +0x2D (0x4629A0: by Config 0 / 1)
    // The input manager's slot 10 (Axis+) / 11 (Axis-) / 12 (Button): (0, config, channel, &type, &index).
    std::function< void( int slot, std::int32_t config, const std::string& channel, std::int32_t& type, std::int32_t& index ) > m_lookup;
    // g 0x759040: the axis name +4 [ index x 16 ], the button name +0x10 [ index x 8 ].
    std::function< std::string( std::int32_t index ) > m_axis_name;
    std::function< std::string( std::int32_t index ) > m_button_name;
    // VuStringDB slot 7 (vt+0x1C, a std::string id), slot 6 (vt+0x18, a literal id), slot 9 (vt+0x24: the "missing" text).
    std::function< std::string( const std::string& id ) > m_string;
    std::function< std::string( const char* id ) > m_string_literal;
    std::function< std::string( ) > m_missing;
};

// 0x626B80 on a VuUIFont: its asset's +0x58, else VuUIImpl's default's, else none.
inline std::uint32_t ui_remap_font( const ui_text_font_t& f )
{
    return f.m_asset ? f.m_asset_font : f.m_default ? f.m_default_font : 0u;
}

// 0x4626D0 (thiscall: type*, index*): both 0; Type "Button" / "Axis+" / "Axis-"
// -> the input manager's slot 12 / 10 / 11; anything else leaves the zeros.
inline void ui_remap_binding( const ui_remap_t& r, const ui_remap_inputs_t& in, std::int32_t& type, std::int32_t& index )
{
    type = 0;
    index = 0;
    const int slot = r.m_type == "Button" ? 12 : r.m_type == "Axis+" ? 10 : r.m_type == "Axis-" ? 11 : -1;
    if ( slot >= 0 && in.m_lookup )
        in.m_lookup( slot, r.m_config, r.m_channel, type, index );
}

// Slot 16 (vt+0x40): 0x4052D0 false; the throttle's 0x4629A0 the setting of Config 0 / 1, else false.
inline bool ui_remap_auto( const ui_remap_t& r, const ui_remap_inputs_t& in )
{
    if ( !in.m_throttle )
        return false;
    return r.m_config == 0 ? in.m_auto[ 0 ] != 0 : r.m_config == 1 ? in.m_auto[ 1 ] != 0 : false;
}

// One image of the row: white with the alpha byte, the full texture.
inline void ui_remap_image( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, std::uint32_t handle, const float dst[ 4 ], float alpha, float depth )
{
    static const float k_uv[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
    const std::uint32_t a = static_cast< std::uint32_t >( ui_fistp32( alpha * ui_f32( 0x437F0000u ) ) ) & 0xFFu;   // 0x6F7808
    ui_texture_draw( sort, env, handle, 0x00FFFFFFu | ( a << 24 ), dst, k_uv, depth );   // 0x507360
}

// 0x461B00 (thiscall; the alpha in xmm1). false: a path not recreated.
inline bool ui_remap_contents_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_string_db_t& db, const ui_remap_t& r,
                                    const ui_remap_inputs_t& in, float alpha )
{
    const float one = ui_f32( 0x3F800000u );            // 0x6F76B8
    const float ix = one / r.m_scale[ 0 ];
    const float iy = one / r.m_scale[ 1 ];
    const float base = r.m_depth * ui_f32( 0x3BA3D70Au ) + ui_f32( 0x3F000000u );   // 0x6F75C4, 0x6F765C
    const std::uint32_t flags = ui_font_flags( 0, 4, 1, 0, 0, db );                  // 0x5546B0 on the local block { 0, 4, 1, 0, 0 }
    const float offset[ 2 ] = { 0.0f, 0.0f };
    bool known = true;
    // the background: +0x214, selected +0x218, hover +0x21C
    std::uint32_t background = r.m_selected ? r.m_textures[ 4 ] : r.m_textures[ 3 ];
    if ( r.m_hover == 1 )
        background = r.m_textures[ 5 ];
    if ( background )
    {
        const float dst[ 4 ] = { ix * ( r.m_image[ 0 ] + r.m_pos[ 0 ] ), iy * ( r.m_image[ 1 ] + r.m_pos[ 1 ] ), ix * r.m_image[ 2 ], iy * r.m_image[ 3 ] };
        ui_remap_image( sort, env, background, dst, alpha, base + ui_f32( 0x34C00000u ) );   // 0x6F758C
    }
    // the label
    if ( const std::uint32_t font = ui_remap_font( r.m_label_font ) )
    {
        if ( !in.m_string )
            return false;
        const std::string text = in.m_string( r.m_label );
        const float zero = 0.0f;
        const float rect[ 4 ] = { ix * ( r.m_pos[ 0 ] + r.m_label_offset[ 0 ] ), iy * ( r.m_pos[ 1 ] + r.m_label_offset[ 1 ] ), ix * zero, iy * zero };
        known = ui_font_draw( sort, env, font, text.c_str( ), r.m_label_params, rect, flags, alpha, offset, base ) && known;   // 0x536C10
    }
    // the binding's text and icon
    std::string text;
    if ( in.m_input )
    {
        std::int32_t type = 0, index = 0;
        ui_remap_binding( r, in, type, index );
        std::uint32_t icon = 0;
        if ( ui_remap_auto( r, in ) )
        {
            if ( !in.m_string_literal )
                return false;
            text = in.m_string_literal( "Control_Label_Auto" ).c_str( );
        }
        else if ( type == 1 || type == 2 )
        {
            if ( !in.m_axis_name || !in.m_string )
                return false;
            text = in.m_string( "Control_Axis_" + in.m_axis_name( index ) ) + ( type == 1 ? "+" : "-" );   // 0x4366B0, slot 7, 0x430160
            icon = r.m_textures[ 0 ];
        }
        else if ( type == 3 )
        {
            if ( !in.m_button_name || !in.m_string )
                return false;
            text = in.m_string( "Control_Button_" + in.m_button_name( index ) );
            icon = r.m_textures[ 1 ];
        }
        else if ( type == 4 )
        {
            if ( !in.m_string || !in.m_missing )
                return false;
            text = in.m_string( std::string( "Control_Key_" ) + ui_key_name( static_cast< std::uint32_t >( index ) ) );
            if ( text == in.m_missing( ) )                 // 0x407790: the table has no text: the key's own name (0x426350)
                text = ui_key_name( static_cast< std::uint32_t >( index ) );
            icon = r.m_textures[ 2 ];
        }
        else
        {
            if ( !in.m_string_literal )
                return false;
            text = in.m_string_literal( "Control_Label_Invalid" ).c_str( );
        }
        if ( icon )
        {
            const float dst[ 4 ] = { ix * ( r.m_control_image[ 0 ] + r.m_pos[ 0 ] ), iy * ( r.m_pos[ 1 ] + r.m_control_image[ 1 ] ), ix * r.m_control_image[ 2 ],
                                     iy * r.m_control_image[ 3 ] };
            ui_remap_image( sort, env, icon, dst, alpha, base + ui_f32( 0x34400000u ) );   // 0x6F7588
        }
    }
    if ( const std::uint32_t font = ui_remap_font( r.m_control_font ) )
    {
        std::uint8_t params[ 60 ];
        std::memcpy( params, r.m_control_params, 60 );
        if ( r.m_selected )
            std::memcpy( params + 12, &r.m_selected_colour, 4 );
        const float zero = 0.0f;
        const float rect[ 4 ] = { ix * ( r.m_control_offset[ 0 ] + r.m_pos[ 0 ] ), iy * ( r.m_pos[ 1 ] + r.m_control_offset[ 1 ] ), ix * zero, iy * zero };
        known = ui_font_draw( sort, env, font, text.c_str( ), params, rect, flags, alpha, offset, base ) && known;
    }
    return known;
}

// 0x45FB90 (the UI draw member; alpha; ret 4).
inline bool ui_remap_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_string_db_t& db, const ui_remap_t& r, const ui_remap_inputs_t& in, float alpha )
{
    if ( !r.m_visible )
        return true;
    return ui_remap_contents_draw( sort, env, db, r, in, alpha );
}

// 0x461A30 (the Vu2dLayoutComponent draw; byte; ret 4): with the byte the touch
// rect white (0x507A00), then 0x461B00 at 1.0 - Visible is not tested.
inline bool ui_remap_layout_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_string_db_t& db, const ui_remap_t& r, const ui_remap_inputs_t& in,
                                  std::uint8_t layout )
{
    const float one = ui_f32( 0x3F800000u );
    if ( layout )
    {
        const float ix = one / r.m_scale[ 0 ];
        const float iy = one / r.m_scale[ 1 ];
        const float rect[ 4 ] = { ix * ( r.m_touch[ 0 ] + r.m_pos[ 0 ] ), iy * ( r.m_touch[ 1 ] + r.m_pos[ 1 ] ), ix * r.m_touch[ 2 ], iy * r.m_touch[ 3 ] };
        ui_rect_draw( sort, env, 0xFFFFFFFFu, rect, r.m_depth * ui_f32( 0x3BA3D70Au ) + ui_f32( 0x3F000000u ) );
    }
    return ui_remap_contents_draw( sort, env, db, r, in, one );
}

// ---- the row's runtime (batch 27): events, plugs, touch, the capture of a new binding ----

// What the row writes besides its drawn fields.
struct ui_remap_runtime_t
{
    std::uint8_t m_focus = 0;                 // +0x1F1
    std::uint8_t m_pressed = 0;               // +0x220 a touch went down this tick
    std::uint8_t m_down = 0;                  // +0x221
    std::uint8_t m_released = 0;              // +0x222
    float m_point[ 2 ] = { 0.0f, 0.0f };      // +0x224 / +0x228 the touch point
    std::uint8_t m_select = 0;                // +0x230 InputSelect arrived
    bool m_box = false;                       // +0x1F4 the message box is open
    std::vector< float > m_axes;              // +0x1F8 the channels' values when the box opened (VuArray: size kept)
    std::uint32_t m_buttons = 0;              // +0x204 the button bits seen so far
};

struct ui_remap_hooks_t
{
    // 0x505690 "OnUISetFocus" with the row's id (0x4612E0).
    std::function< void( ) > m_set_focus;
    // 0x462420 (the output plug's name, the event's first int): findPlug + execute, the browse sound when it has a connection.
    std::function< void( const char* plug, std::int32_t arg ) > m_nav;
    // The Sfx +0x1D4 played at the press (FMOD: 0x64C348 / 0x64C354).
    std::function< void( ) > m_sound;
    // 0x461940: the message box (heading = Label String ID, "Control_Mapping_MB_Body", "MessageBox_Cancel", with slot 15 true "MessageBox_Auto").
    // `done`: the box's callback 0x461900 with its result (+0xC8).
    std::function< void( const std::string& heading, bool can_auto, std::function< void( std::int32_t ) > done ) > m_open_box;
    // The box's release (+4 - 1, +0xCC = 1).
    std::function< void( ) > m_close_box;
    // g 0x759040: its +8 channel count; pad 0's (slot 6) +0x10 values [ i ] and +0xC button bits.
    std::function< int( ) > m_channels;
    std::function< float( int ) > m_axis;
    std::function< std::uint32_t( ) > m_button_bits;
    // g 0x75906C +0xFF [ key ] down (0..0x50).
    std::function< bool( int ) > m_key;
    // 0x4627F0: the input manager's slot 15 (Button) / 13 (Axis+) / 14 (Axis-): (0, config, channel, kind, index).
    std::function< void( int slot, std::int32_t config, const std::string& channel, int kind, int index ) > m_bind;
    // Slot 17 vt+0x44 (byte): 0x4021A0 nothing / the throttle's 0x4629D0 (the setting of Config 0 / 1).
    std::function< void( std::uint8_t ) > m_set_auto;
};

// 0x461900 (the box's callback at +0x60; ret 4): result 1 (its +0xC8) -> slot 17(1); the box released.
inline void ui_remap_box_result( ui_remap_runtime_t& s, std::int32_t result, const ui_remap_hooks_t& h )
{
    if ( result == 1 && h.m_set_auto )
        h.m_set_auto( 1 );
    if ( h.m_close_box )
        h.m_close_box( );
    s.m_box = false;
}

// Slot 12 0x4613A0 (OnUISetFocus): +0x1F1 = the event's id is this row's.
inline void ui_remap_on_focus( ui_remap_runtime_t& s, bool mine ) { s.m_focus = mine ? 1u : 0u; }

// The input plugs: SetFocus 0x4612E0 (the broadcast, then +0x1F1 = 1), Hide 0x461380.
inline void ui_remap_set_focus( ui_remap_runtime_t& s, const ui_remap_hooks_t& h )
{
    if ( h.m_set_focus )
        h.m_set_focus( );
    s.m_focus = 1u;
}

inline void ui_remap_hide( ui_remap_t& r, ui_remap_runtime_t& s )
{
    r.m_visible = 0;
    r.m_selected = 0;   // the word +0x1F0
    s.m_focus = 0;
}

// The touch callbacks (this + 0x5C). Down 0x461760 (point*): Visible and not yet
// down: +0x220 = +0x221 = 1, the point through VuUIImpl's inverse crop.
inline void ui_remap_touch_point( ui_remap_runtime_t& s, const float inverse[ 16 ], const float point[ 2 ] )
{
    const float x = ( inverse[ 4 ] * point[ 1 ] + inverse[ 0 ] * point[ 0 ] ) + inverse[ 12 ];
    const float y = ( inverse[ 1 ] * point[ 0 ] + inverse[ 5 ] * point[ 1 ] ) + inverse[ 13 ];
    s.m_point[ 0 ] = x;
    s.m_point[ 1 ] = y;
}

inline void ui_remap_touch_down( const ui_remap_t& r, ui_remap_runtime_t& s, const float inverse[ 16 ], const float point[ 2 ] )
{
    if ( !r.m_visible || s.m_down )
        return;
    s.m_pressed = 1;
    s.m_down = 1;
    ui_remap_touch_point( s, inverse, point );
}

// Up 0x4617F0: Visible -> +0x222 = 1. Move 0x461800: the device's touch 0 position, no test.
inline void ui_remap_touch_up( const ui_remap_t& r, ui_remap_runtime_t& s )
{
    if ( r.m_visible )
        s.m_released = 1;
}

// 0x462540: the baseline: the array sized to the channels, each value, +0x204 = -1.
inline void ui_remap_baseline( ui_remap_runtime_t& s, const ui_remap_hooks_t& h )
{
    const int n = h.m_channels ? h.m_channels( ) : 0;
    if ( static_cast< int >( s.m_axes.size( ) ) < n )
        s.m_axes.resize( static_cast< std::size_t >( n ), 0.0f );
    for ( int i = 0; i < n; ++i )
        s.m_axes[ static_cast< std::size_t >( i ) ] = h.m_axis( i );
    s.m_buttons = 0xFFFFFFFFu;
}

// Slot 13 0x4613F0 (OnUITick; its VuParams unread).
inline void ui_remap_tick( ui_remap_t& r, ui_remap_runtime_t& s, const ui_remap_hooks_t& h )
{
    if ( !r.m_visible )
        return;
    bool press = false;
    if ( s.m_focus != r.m_selected )
        r.m_selected = s.m_focus;
    const float one = ui_f32( 0x3F800000u );
    const float ix = one / r.m_scale[ 0 ];
    const float iy = one / r.m_scale[ 1 ];
    const float x = ix * ( r.m_pos[ 0 ] + r.m_touch[ 0 ] );
    const float y = iy * ( r.m_touch[ 1 ] + r.m_pos[ 1 ] );
    const float w = ix * r.m_touch[ 2 ];
    const float h2 = iy * r.m_touch[ 3 ];
    const bool inside = s.m_point[ 0 ] >= x && w + x >= s.m_point[ 0 ] && s.m_point[ 1 ] >= y && h2 + y >= s.m_point[ 1 ];
    if ( s.m_pressed && inside )
        r.m_hover = 1;
    if ( r.m_hover == 1 )
    {
        if ( s.m_released )
        {
            press = true;
            r.m_hover = 0;
        }
        if ( !inside )
            r.m_hover = 0;
    }
    if ( s.m_released )
    {
        s.m_down = 0;
        s.m_released = 0;
    }
    const bool go = s.m_select != 0 || press;
    s.m_pressed = 0;
    s.m_select = 0;
    if ( go )
    {
        if ( h.m_sound )
            h.m_sound( );
        if ( h.m_open_box )
            h.m_open_box( r.m_label, h.m_set_auto != nullptr, [ &r, &s, &h ]( std::int32_t result ) { ui_remap_box_result( s, result, h ); } );
        s.m_box = true;
        ui_remap_baseline( s, h );
    }
}

// Slot 14 0x4615A0 (OnUIEvent; the event's string and first int): only while
// selected: InputUp / Down / Left / Right -> the plug Nav*; InputSelect -> +0x230.
inline void ui_remap_event( const ui_remap_t& r, ui_remap_runtime_t& s, const std::string& text, std::int32_t arg, const ui_remap_hooks_t& h )
{
    if ( !r.m_selected )
        return;
    const char* plug = text == "InputUp" ? "NavUp" : text == "InputDown" ? "NavDown" : text == "InputLeft" ? "NavLeft" : text == "InputRight" ? "NavRight" : nullptr;
    if ( plug )
    {
        if ( h.m_nav )
            h.m_nav( plug, arg );
    }
    else if ( text == "InputSelect" )
        s.m_select = 1;
}

// 0x4627F0 (kind, index): Type picks the manager's slot.
inline void ui_remap_bind( const ui_remap_t& r, int kind, int index, const ui_remap_hooks_t& h )
{
    const int slot = r.m_type == "Button" ? 15 : r.m_type == "Axis+" ? 13 : r.m_type == "Axis-" ? 14 : -1;
    if ( slot >= 0 && h.m_bind )
        h.m_bind( slot, r.m_config, r.m_channel, kind, index );
}

// 0x4625C0: true = a new input was bound: an axis moved past +-0.5 (kinds 1 / 2); a
// button bit new since +0x204 (kind 3, the HIGHEST such bit); else +0x204 = the bits
// and the first key down (kind 4).
inline bool ui_remap_capture( const ui_remap_t& r, ui_remap_runtime_t& s, const ui_remap_hooks_t& h )
{
    const int n = h.m_channels( );
    for ( int i = 0; i < n; ++i )
    {
        const float d = h.m_axis( i ) - s.m_axes[ static_cast< std::size_t >( i ) ];
        if ( d > ui_f32( 0x3F000000u ) )
        {
            ui_remap_bind( r, 1, i, h );
            return true;
        }
        if ( ui_f32( 0xBF000000u ) > d )
        {
            ui_remap_bind( r, 2, i, h );
            return true;
        }
    }
    const std::uint32_t bits = h.m_button_bits( );
    std::uint32_t fresh = ~s.m_buttons & bits;
    if ( fresh )
    {
        int index = 0;
        while ( fresh >>= 1 )
            ++index;
        ui_remap_bind( r, 3, index, h );
        return true;
    }
    s.m_buttons = bits;
    for ( int k = 0; k < 0x51; ++k )
        if ( h.m_key( k ) )
        {
            ui_remap_bind( r, 4, k, h );
            return true;
        }
    return false;
}

// 0x4623D0 ("Decision" tick; dt unread): with the box open and a capture: slot 17(0), the box released.
inline void ui_remap_decision( const ui_remap_t& r, ui_remap_runtime_t& s, const ui_remap_hooks_t& h )
{
    if ( !s.m_box || !ui_remap_capture( r, s, h ) )
        return;
    if ( h.m_set_auto )
        h.m_set_auto( 0 );
    if ( h.m_close_box )
        h.m_close_box( );
    s.m_box = false;
}

