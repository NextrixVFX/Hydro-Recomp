#pragma once

#include <workspace/core/ui/draw.hxx>
#include <workspace/core/ui/gauge.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

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
