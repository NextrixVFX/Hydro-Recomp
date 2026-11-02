#pragma once

#include <workspace/core/ui/button.hxx>
#include <workspace/core/ui/draw.hxx>
#include <workspace/core/ui/gauge.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

// VuSliderEntity (ctor 0x46E300, vtable 0x73E334 - abstract: slots 12 / 13 the
// value get / set are _purecall) and its four settings subclasses, transliterated
// from the disassembly. Fields (offsets from the entity, the ctor's defaults):
// +0x64 Visible (1), +0x68 Screen Scale (1080, 720), +0xA0..+0xAC Frame X / Y /
// Width / Height, +0xB0..+0xBC Text X / Y / Width / Height, +0xC0 the Font
// (VuUIFont, its params at +0xE0), +0x124 Horizontal / +0x128 Vertical Alignment
// (0 / 2), +0x12C No Clip, +0x12D Single Line, +0x12E Wordbreak, +0x130 String
// ID, +0x160..+0x16C Bar Dst X / Y / Width / Height, +0x170 Bar Dst Delta,
// +0x178..+0x184 Bar Src X / Y / Width / Height, +0x188 Bar Src Delta, +0x190
// Bar Src On Delta, +0x198 Bar Count (0), +0x19C the "OnSetValue" output, +0x1A0
// / +0x1A4 / +0x1A8 the Frame / Selected Frame / Bar texture assets, +0x1AC
// focused, +0x1AD the focus the last tick saw (the ctor's WORD 0).
//
// Handlers (VuEventMap, delegate vtable 0x73E304): OnUISetFocus 0x46F8F0,
// OnUITick 0x46F940, OnUIEvent 0x46FCC0. Inputs (0x4706A0, VuMethod1 vtable
// 0x73E2F4): Show 0x446C80 / Hide 0x446FB0 (Visible = 1 / 0) and SetFocus
// 0x46F850 - each writes only +0 = 0 of its VuRetVal. Outputs: OnSetValue, NavUp,
// NavDown, SelectionChanged (fired by none of these).
//
// The value (slots 12 / 13) lives in VuSettingsManager g 0x758FE0 (null: the
// getters' defaults): VuEffectVolumeSettingsEntity (0x73E06C) +0x38 - get
// 0x46CFA0, set 0x46CFE0 (+ the FMOD categories "game" and "ui" setVolume);
// VuMusicVolumeSettingsEntity (0x73E030) +0x3C - 0x46D110 / 0x46D140 ("music");
// VuDisplayScaleSettingsEntity (0x73DFF4) +0x40 - get 0x46D240 (+0x40 - 0.5) x 2
// (null 0.75), set 0x46D280 (v + 1) x 0.5 into +0x40 and g 0x758F94 +0xC;
// VuSteeringSensitivitySettingsEntity (0x73E10C) +0x30 - 0x46CA00 / 0x46CA30.

constexpr std::uint32_t k_ui_slider_vtable = 0x73E334u;
constexpr std::uint32_t k_ui_effect_volume_vtable = 0x73E06Cu;
constexpr std::uint32_t k_ui_music_volume_vtable = 0x73E030u;
constexpr std::uint32_t k_ui_display_scale_vtable = 0x73DFF4u;
constexpr std::uint32_t k_ui_steering_vtable = 0x73E10Cu;

inline bool ui_is_slider_vtable( std::uint32_t vt )
{
    return vt == k_ui_effect_volume_vtable || vt == k_ui_music_volume_vtable || vt == k_ui_display_scale_vtable || vt == k_ui_steering_vtable;
}

// OURS, NOT RETAIL: the user's Video settings (world/video.hxx adds them to the screen, settings_ours_load / _save persist
// them). Each entity is a retail slider or combo told apart by its name, which no retail entity has. The defaults are
// retail's behaviour: no limit, vsync off, ConstantDB's VerticalFOV, 100% of the shake and of the speed / falling zoom out.
enum ui_ours_t : int
{
    ui_ours_none = 0,
    ui_ours_frame_limit,
    ui_ours_vsync,
    ui_ours_fov,
    ui_ours_shake,
    ui_ours_zoom,
};

inline int ui_ours_tag( const std::string& name )
{
    if ( name == "OursFrameLimit" ) return ui_ours_frame_limit;
    if ( name == "OursVSync" ) return ui_ours_vsync;
    if ( name == "OursFieldOfView" ) return ui_ours_fov;
    if ( name == "OursCameraShake" ) return ui_ours_shake;
    if ( name == "OursCameraZoomOut" ) return ui_ours_zoom;
    return ui_ours_none;
}

inline constexpr float k_ui_ours_fov_low = 50.0f, k_ui_ours_fov_high = 110.0f;

struct ui_settings_ours_t
{
    std::int32_t frame_limit = 0;   // hertz, 0 unlimited
    bool vsync = false;
    float fov = 70.0f;              // degrees
    float fov_default = 70.0f;      // ConstantDB's VerticalFOV
    float shake = 1.0f;             // 0 .. 2
    float zoom = 1.0f;              // 0 .. 2
};

// VuSettingsManager g 0x758FE0 (the fields the sliders read and write) and the
// display object g 0x758F94 +0xC (written by the display-scale setter).
struct ui_settings_t
{
    bool m_present = true;
    float m_steering = 0.0f;   // +0x30
    float m_effect = 0.0f;     // +0x38
    float m_music = 0.0f;      // +0x3C
    float m_display = 0.0f;    // +0x40
    float m_display_out = 0.0f;  // g 0x758F94 +0xC
    bool m_subtitles = false;    // +0x5C (VuUISubtitlesSettingEntity slot 12 0x46D9B0)
    // the other settings combos (chooser.hxx): slots 6 read them, slots 12 write them
    bool m_tilt = false;               // +0x2E (VuTiltSteeringSettingsEntity 0x46C780 / 0x46C9B0)
    bool m_vibration = false;          // +0x34 (VuVibrationSettingsEntity 0x46CD70 / 0x46CF70)
    std::string m_units;               // +0x44 std::string (VuUIUnitsSettingEntity 0x46D3E0 / 0x46D640)
    std::string m_fidelity;            // +0x60 std::string (VuUIFidelitySettingEntity 0x46DA70 / setter 0x4B50A0)
    std::int32_t m_flip_interval = 0;  // +0x78 (VuUIFlipIntervalSettingEntity 0x46DE30 / 0x46E050)
    ui_settings_ours_t m_ours;                 // ours, not retail
    std::function< void( ) > m_ours_changed;   // ours: puts m_ours into the engine and the camera
};

struct ui_slider_hooks_t
{
    std::function< void( std::uint32_t, const ui_params_t& ) > m_broadcast;  // 0x505690
    std::function< void( const char*, const ui_params_t& ) > m_fire;         // findPlug 0x504770 + slot 2
    std::function< int( const char* ) > m_connections;                       // the plug's (+0x54 - +0x50) / 4
    std::function< void( const char* ) > m_sound;                            // FMOD getEvent + start (HOOK)
    std::function< void( const char*, float ) > m_category;                  // FMOD getCategory + setVolume (HOOK)
    std::function< int( ) > m_touch_count;                                   // HOOK: the touch device g 0x759070 slot 6 (-1)
    std::function< void( int, float[ 2 ] ) > m_touch_point;                  // HOOK: its slot 7 (index, out)
    const float* m_inverse = nullptr;  // VuUIImpl +76 0x81A144: the inverse crop matrix (an input; 0x4157C0 not recreated)
    ui_settings_t* m_settings = nullptr;   // g 0x758FE0 (null = no manager)
};

// The authored fields the handlers and the draw read.
struct ui_slider_t
{
    std::uint32_t m_vtable = 0;
    bool m_visible = true;                             // +0x64
    float m_scale[ 2 ] = { 1080.0f, 720.0f };          // +0x68
    float m_frame[ 4 ] = { };                          // +0xA0
    float m_text[ 4 ] = { };                           // +0xB0
    std::int32_t m_h_align = 0;                        // +0x124
    std::int32_t m_v_align = 2;                        // +0x128
    std::uint8_t m_no_clip = 0;                        // +0x12C
    std::uint8_t m_single_line = 0;                    // +0x12D
    std::uint8_t m_wordbreak = 0;                      // +0x12E
    std::string m_string_id;                           // +0x130
    float m_dst[ 4 ] = { };                            // +0x160
    float m_dst_delta[ 2 ] = { };                      // +0x170
    float m_src[ 4 ] = { };                            // +0x178
    float m_src_delta[ 2 ] = { };                      // +0x188
    float m_src_on_delta[ 2 ] = { };                   // +0x190
    std::int32_t m_count = 0;                          // +0x198
    float m_depth = 0.0f;                              // the draw component (+0x60) +0x14 "Depth"
    int m_ours = ui_ours_none;                         // ours, not retail: ui_ours_tag
};

class c_ui_slider
{
public:
    c_ui_slider( std::uint32_t id, const ui_slider_t& s, ui_slider_hooks_t& hooks ) : m_id( id ), m_s( s ), m_hooks( hooks ), m_visible( s.m_visible ) { }

    // Slot 12 of the subclass (x87 return of a float).
    float value( ) const
    {
        const ui_settings_t* m = m_hooks.m_settings;
        if ( m && m_s.m_ours == ui_ours_fov )
            return ( m->m_ours.fov - k_ui_ours_fov_low ) / ( k_ui_ours_fov_high - k_ui_ours_fov_low );
        if ( m && m_s.m_ours == ui_ours_shake )
            return m->m_ours.shake * 0.5f;
        if ( m && m_s.m_ours == ui_ours_zoom )
            return m->m_ours.zoom * 0.5f;
        switch ( m_s.m_vtable )
        {
        case k_ui_effect_volume_vtable: return m ? m->m_effect : 0.5f;
        case k_ui_music_volume_vtable: return m ? m->m_music : 0.5f;
        case k_ui_display_scale_vtable: return m ? ( m->m_display - 0.5f ) * 2.0f : 0.75f;   // 0x6F765C, 0x6F76F8
        case k_ui_steering_vtable: return m ? m->m_steering : 0.5f;
        default: return 0.0f;   // VuSliderEntity itself: _purecall
        }
    }

    // Slot 13 of the subclass.
    void set_value( float v )
    {
        ui_settings_t* m = m_hooks.m_settings;
        if ( !m )
            return;
        if ( m_s.m_ours != ui_ours_none )
        {
            if ( m_s.m_ours == ui_ours_fov )
                m->m_ours.fov = k_ui_ours_fov_low + v * ( k_ui_ours_fov_high - k_ui_ours_fov_low );
            else if ( m_s.m_ours == ui_ours_shake )
                m->m_ours.shake = v * 2.0f;
            else if ( m_s.m_ours == ui_ours_zoom )
                m->m_ours.zoom = v * 2.0f;
            if ( m->m_ours_changed )
                m->m_ours_changed( );
            return;
        }
        switch ( m_s.m_vtable )
        {
        case k_ui_effect_volume_vtable:
            m->m_effect = v;
            category( "game", v );
            category( "ui", v );
            break;
        case k_ui_music_volume_vtable:
            m->m_music = v;
            category( "music", v );
            break;
        case k_ui_display_scale_vtable:
            m->m_display = ( v + 1.0f ) * 0.5f;   // 0x6F76B8, 0x6F765C
            m->m_display_out = m->m_display;
            break;
        case k_ui_steering_vtable: m->m_steering = v; break;
        default: break;
        }
    }

    // 0x46F730: fistp( value x (float)Bar Count ) - the product rounded to a float first.
    std::int32_t current( ) const
    {
        const float product = value( ) * static_cast< float >( m_s.m_count );
        return ui_fistp32( product );
    }

    // 0x46F770 (k): 0 <= k <= Bar Count and not the current step -> the tick
    // sound, slot 13 ( k / count ), then OnSetValue with an empty VuParams.
    void set( std::int32_t k )
    {
        if ( k < 0 || k > m_s.m_count )
            return;
        if ( k == current( ) )
            return;
        if ( m_hooks.m_sound )
            m_hooks.m_sound( "Global/UI_and_Menu/credits_tick_2" );   // [0x746D54]
        set_value( static_cast< float >( k ) / static_cast< float >( m_s.m_count ) );
        if ( m_hooks.m_fire )
            m_hooks.m_fire( "OnSetValue", ui_params_t( ) );
    }

    // 0x46F8F0: focused = the id record names this slider.
    void on_set_focus( const ui_params_t& params )
    {
        ui_params_reader_t r( params );
        m_focused = ui_params_read( r, 1 ) == m_id;
    }

    // 0x46F850 (input SetFocus): broadcast "OnUISetFocus" with the id record (every
    // subscriber's handler runs inside), then focused.
    void set_focus( )
    {
        ui_params_t p;
        ui_params_add( p, 1, m_id );
        if ( m_hooks.m_broadcast )
            m_hooks.m_broadcast( fsm_hash( "OnUISetFocus" ), p );
        m_focused = true;
    }

    // 0x46F940: a visible slider latches its focus, then hit-tests every touch
    // against the bars k = -1 .. Bar Count - 1 (the bar rect shrunk to the delta
    // along its major axis); the last hit k sets step k + 1.
    void on_tick( )
    {
        if ( !m_visible )
            return;
        m_latched = m_focused;
        const float px = m_s.m_dst[ 0 ], py = m_s.m_dst[ 1 ], sx = m_s.m_dst[ 2 ], sy = m_s.m_dst[ 3 ];
        const float ax = std::fabs( m_s.m_dst_delta[ 0 ] );   // VuVector_abs 0x4089C0 (andps)
        const float ay = std::fabs( m_s.m_dst_delta[ 1 ] );
        float x0, y0, w, h;
        if ( ax > ay )   // comiss / jbe: unordered takes the other axis
        {
            const float d = sx - ax;
            w = sx;
            x0 = px;
            if ( d > 0.0f )
            {
                x0 = d * 0.5f + px;
                w = sx - d;
            }
            h = sy;
            y0 = py;
        }
        else
        {
            const float d = sy - ay;
            h = sy;
            y0 = py;
            if ( d > 0.0f )
            {
                y0 = d * 0.5f + py;
                h = sy - d;
            }
            w = sx;
            x0 = px;
        }
        std::int32_t hit = -1;
        if ( !m_hooks.m_touch_count )
            return;
        static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        const float* m = m_hooks.m_inverse ? m_hooks.m_inverse : identity;
        for ( int i = 0; i < m_hooks.m_touch_count( ); ++i )
        {
            float point[ 2 ] = { 0.0f, 0.0f };
            if ( m_hooks.m_touch_point )
                m_hooks.m_touch_point( i, point );
            const float x = point[ 0 ] * m[ 0 ] + point[ 1 ] * m[ 4 ] + m[ 12 ];
            const float y = point[ 0 ] * m[ 1 ] + point[ 1 ] * m[ 5 ] + m[ 13 ];
            const std::int32_t count = m_s.m_count;
            if ( count <= -1 )
                continue;
            const float bx = m_s.m_frame[ 0 ] + x0;
            const float by = m_s.m_frame[ 1 ] + y0;
            const float ix = 1.0f / m_s.m_scale[ 0 ];
            const float iy = 1.0f / m_s.m_scale[ 1 ];
            const float wp = ix * w;
            const float hp = iy * h;
            for ( std::int32_t k = -1; k < count; ++k )
            {
                const float kf = static_cast< float >( k );
                const float cx = ix * ( bx + m_s.m_dst_delta[ 0 ] * kf );
                const float cy = iy * ( by + m_s.m_dst_delta[ 1 ] * kf );
                if ( x >= cx && wp + cx >= x && y >= cy && hp + cy >= y )
                    hit = k + 1;
            }
        }
        if ( hit >= 0 )
            set( hit );
    }

    // 0x46FCC0: with the latched focus, InputUp / InputDown fire NavUp / NavDown
    // with one int record (menu_browse when the plug has a connection),
    // InputLeft / InputRight step the value by -1 / +1.
    void on_event( const ui_params_t& params )
    {
        ui_params_reader_t r( params );
        const char* input = r.read_string( );
        if ( !m_latched )
            return;
        if ( std::strcmp( input, "InputUp" ) == 0 || std::strcmp( input, "InputDown" ) == 0 )
        {
            const char* plug = input[ 5 ] == 'U' ? "NavUp" : "NavDown";
            ui_params_t out;
            ui_params_add( out, 0, ui_params_read( r, 0 ) );
            if ( m_hooks.m_fire )
                m_hooks.m_fire( plug, out );
            if ( m_hooks.m_connections && m_hooks.m_connections( plug ) && m_hooks.m_sound )
                m_hooks.m_sound( "Global/UI_and_Menu/menu_browse" );
        }
        else if ( std::strcmp( input, "InputLeft" ) == 0 )
            set( current( ) - 1 );
        else if ( std::strcmp( input, "InputRight" ) == 0 )
            set( current( ) + 1 );
    }

    std::uint32_t m_id;
    ui_slider_t m_s;
    ui_slider_hooks_t& m_hooks;
    bool m_visible;           // +0x64 (Show / Hide)
    bool m_focused = false;   // +0x1AC
    bool m_latched = false;   // +0x1AD

private:
    void category( const char* name, float v )
    {
        if ( m_hooks.m_category )
            m_hooks.m_category( name, v );
    }
};

// What the slider's draw reaches outside its own fields - each an input: the
// three textures (asset property +0x4C -> +0x58; null = no asset or no texture:
// that draw is skipped), the font (0x626B80 on +0xC0: the asset's, else VuUIImpl's
// default; 0 = none), its params (+0xE0, the VuUIFont's 60 bytes), VuStringDB slot
// 7 on String ID, the string DB for the font flags, and slot 12's value.
struct ui_slider_draw_inputs_t
{
    const ui_texture_t* m_frame = nullptr;      // +0x1A0 Frame Texture
    const ui_texture_t* m_selected = nullptr;   // +0x1A4 Selected Frame Texture
    const ui_texture_t* m_bar = nullptr;        // +0x1A8 Bar Texture
    std::uint32_t m_font = 0;
    std::uint8_t m_params[ 60 ] = { };
    std::function< std::string( const std::string& ) > m_string;
    ui_string_db_t m_db;
    float m_value = 0.0f;
};

// 0x4700E0 (the VuUIDrawComponent's member via delegate 0x470600 / vtable
// 0x73E314; alpha; ret 4), disassembled. Nothing unless Visible. The colour
// 0x??FFFFFF with alpha byte fistp( alpha x 255 ); the frame (selected when the
// tick's latched focus +0x1AD) over the Frame rect / screen scale, uv (0, 0, 1,
// 1), depth Depth x 0.005 + 0.5 + 3.576e-7; then Bar Count bars of the bar
// texture at Frame + Bar Dst + k x Dst Delta (Bar Dst size), uv (Bar Src + k x Src
// Delta, + Src On Delta when fistp( value x count ) >= k + 1, Bar Src size) over
// the texture's own +0xC / +0x10 ints, depth + 1.788e-7; then the label - with
// a font and a non-empty String ID text - in (Text + Frame, Text size) over the
// scale, offset 0, the font flags of +0x124, the alpha raw, depth + 0.
inline bool ui_slider_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_slider_t& s, bool visible, bool latched,
                            const ui_slider_draw_inputs_t& in, float alpha )
{
    if ( !visible )
        return true;
    const std::uint32_t a = static_cast< std::uint32_t >( ui_fistp32( alpha * 255.0f ) ) & 0xFFu;   // 0x6F7808
    const std::uint32_t colour = ( a << 24 ) | 0x00FFFFFFu;
    const float base = s.m_depth * 0.005f + 0.5f;   // 0x6F75C4, 0x6F765C
    if ( const ui_texture_t* frame = latched ? in.m_selected : in.m_frame )
    {
        const float ix = 1.0f / s.m_scale[ 0 ];
        const float iy = 1.0f / s.m_scale[ 1 ];
        const float rect[ 4 ] = { ix * s.m_frame[ 0 ], s.m_frame[ 1 ] * iy, s.m_frame[ 2 ] * ix, s.m_frame[ 3 ] * iy };
        const float uv[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
        ui_texture_draw( sort, env, frame->m_handle, colour, rect, uv, base + 3.57627869e-07f );   // 0x6F758C
    }
    if ( const ui_texture_t* bar = in.m_bar )
        for ( std::int32_t k = 0; k < s.m_count; )
        {
            const float kf = static_cast< float >( k );
            const float x = ( s.m_frame[ 0 ] + s.m_dst[ 0 ] ) + kf * s.m_dst_delta[ 0 ];
            const float y = ( s.m_frame[ 1 ] + s.m_dst[ 1 ] ) + s.m_dst_delta[ 1 ] * kf;
            float u = kf * s.m_src_delta[ 0 ] + s.m_src[ 0 ];
            float v = kf * s.m_src_delta[ 1 ] + s.m_src[ 1 ];
            const std::int32_t filled = ui_fistp32( in.m_value * static_cast< float >( s.m_count ) );   // slot 12 per bar
            ++k;
            if ( filled >= k )
            {
                u = s.m_src_on_delta[ 0 ] + u;
                v = s.m_src_on_delta[ 1 ] + v;
            }
            const float iw = 1.0f / static_cast< float >( bar->m_width );
            const float ih = 1.0f / static_cast< float >( bar->m_height );
            const float ix = 1.0f / s.m_scale[ 0 ];
            const float iy = 1.0f / s.m_scale[ 1 ];
            const float uv[ 4 ] = { iw * u, ih * v, iw * s.m_src[ 2 ], ih * s.m_src[ 3 ] };
            const float rect[ 4 ] = { ix * x, iy * y, ix * s.m_dst[ 2 ], iy * s.m_dst[ 3 ] };
            ui_texture_draw( sort, env, bar->m_handle, colour, rect, uv, base + 1.78813934e-07f );   // 0x6F7588
        }
    if ( !in.m_font )
        return true;
    const std::string text = in.m_string ? in.m_string( s.m_string_id ) : std::string( );
    if ( text.empty( ) )
        return true;
    const float ix = 1.0f / s.m_scale[ 0 ];
    const float iy = 1.0f / s.m_scale[ 1 ];
    const float rect[ 4 ] = { ix * ( s.m_text[ 0 ] + s.m_frame[ 0 ] ), iy * ( s.m_text[ 1 ] + s.m_frame[ 1 ] ), ix * s.m_text[ 2 ], iy * s.m_text[ 3 ] };
    const float offset[ 2 ] = { 0.0f, 0.0f };
    const std::uint32_t flags = ui_font_flags( s.m_h_align, s.m_v_align, s.m_no_clip, s.m_single_line, s.m_wordbreak, in.m_db );
    return ui_font_draw( sort, env, in.m_font, text.c_str( ), in.m_params, rect, flags, alpha, offset, base );
}

// 0x470010 (the Vu2dLayoutComponent's member via delegate 0x470570 / vtable
// 0x73E324; bool; ret 4): when set, the text rect (as the label's) as a rect draw
// 0x507A00 in the params' own colour (+0xEC = params +12) at Depth x 0.005 + 0.5;
// then always the draw above with alpha 1.
inline bool ui_slider_layout_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_slider_t& s, bool visible, bool latched,
                                   const ui_slider_draw_inputs_t& in, bool layout )
{
    if ( layout )
    {
        const float ix = 1.0f / s.m_scale[ 0 ];
        const float iy = 1.0f / s.m_scale[ 1 ];
        const float rect[ 4 ] = { ix * ( s.m_text[ 0 ] + s.m_frame[ 0 ] ), iy * ( s.m_text[ 1 ] + s.m_frame[ 1 ] ), ix * s.m_text[ 2 ], iy * s.m_text[ 3 ] };
        std::uint32_t colour;
        std::memcpy( &colour, in.m_params + 12, 4 );
        ui_rect_draw( sort, env, colour, rect, s.m_depth * 0.005f + 0.5f );
    }
    return ui_slider_draw( sort, env, s, visible, latched, in, 1.0f );
}
