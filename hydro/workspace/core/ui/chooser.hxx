#pragma once

#include <workspace/core/ui/controls.hxx>
#include <workspace/core/ui/leave.hxx>
#include <workspace/core/ui/slider.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// VuUIComboBoxEntity (ctor 0x474590; the settings combos - VuUISubtitlesSetting
// Entity ctor 0x46D710, vtable 0x73DF3C, is the one the Screens tick) - its
// per-frame half, transliterated from the disassembly. Fields (offsets from the
// entity, the ctor's defaults): +0x68 Screen Scale (100, 100), +0x70 / +0x74
// Position X / Y, +0x78 the style block (ctor 0x4569F0: its +0x120 = 0.25 the
// scroll time, no property; +0x278 points at it), +0x258..+0x264 TouchLeft X /
// Y / Width / Height (0, 0, 100, 100), +0x268..+0x274 TouchRight (100, 0, 100,
// 100), +0x27C Heading String ID, +0x294 the choices (28-byte entries, filled by
// the subclass's slot 6 - an input here), +0x2A0 the focus the last tick saw,
// +0x2A1 / +0x2A2 the left / right arrow pressed, +0x2A4 the current choice,
// +0x2A8 the shown choice (float), +0x2AC focused, +0x2AD / +0x2AE / +0x2AF the
// touch down / held / up (the touch callbacks' - their dispatch is the device's),
// +0x2B0 the touch point, +0x2B8 the pressed arrow (0 none / 1 left / 2 right).
// All zero in the ctor.
//
// Handlers (ctor 0x474F1C..: VuMethod1 delegate vtable 0x73EE24 through 0x475E50):
// OnUISetFocus -> thunk 0x404AB2 = slot 14 0x475230, OnUITick -> 0x4077DE = slot
// 15 0x475280, OnUIEvent -> 0x4077E8 = slot 16 0x475640. Input SetFocus (0x475DB0)
// -> thunk 0x4077ED = slot 13 0x475190. Outputs NavUp, NavDown, SelectionChanged.
// The subclass's slot 12 applies the current choice: VuUISubtitlesSettingEntity
// 0x46D9B0 = VuSettingsManager g 0x758FE0 +0x5C (byte) = (+0x2A4 != 0), read
// without a null test.

inline constexpr std::uint32_t k_ui_subtitles_vtable = 0x73DF3Cu;

// The other five VuUIComboBoxEntity subclasses (their ctors install these
// vtables over 0x474590's; the base's handlers / slots 13..16 / 0x475B40 are the
// same - only slot 6 (the choices + the current choice) and slot 12 (apply)
// differ: initialize_settings / apply_settings below).
inline constexpr std::uint32_t k_ui_units_vtable = 0x73DFA4u;       // VuUIUnitsSettingEntity
inline constexpr std::uint32_t k_ui_fidelity_vtable = 0x73DED4u;    // VuUIFidelitySettingEntity
inline constexpr std::uint32_t k_ui_flip_vtable = 0x73DE6Cu;        // VuUIFlipIntervalSettingEntity
inline constexpr std::uint32_t k_ui_vibration_vtable = 0x73E0BCu;   // VuVibrationSettingsEntity
inline constexpr std::uint32_t k_ui_tilt_vtable = 0x73E15Cu;        // VuTiltSteeringSettingsEntity

// Ours, not retail: the Frame Rate Limit's steps; 0 Unlimited, the default.
inline constexpr std::int32_t k_ui_ours_limits[ 10 ] = { 30, 60, 75, 90, 120, 144, 165, 240, 360, 0 };

inline bool ui_is_settings_combo_vtable( std::uint32_t vt )
{
    return vt == k_ui_subtitles_vtable || vt == k_ui_units_vtable || vt == k_ui_fidelity_vtable || vt == k_ui_flip_vtable ||
           vt == k_ui_vibration_vtable || vt == k_ui_tilt_vtable;
}

// .data 0x7357EC {"Imperial", "Metric"} (the units) and 0x7357DC {"Low", "Medium",
// "High"} (the fidelity) - static pointers, no CRT initializer writes them. The
// slots 12 index them with +0x2A4 and NO bounds test (retail then reads the
// neighbouring .data words: 0x7357E8 = 2 after the fidelity's, 0x7357F4 "OFF" after
// the units') - here an index outside the table is a fault (false = UNKNOWN).
inline constexpr const char* k_ui_units_names[ 2 ] = { "Imperial", "Metric" };
inline constexpr const char* k_ui_fidelity_names[ 3 ] = { "Low", "Medium", "High" };

// What the settings combos reach outside VuSettingsManager g 0x758FE0. The
// platform calls are HOOKS (the device / the system): g_vumetrosys 0x759000
// vt+0x54 / vt+0x58 (two capability queries the tilt combo asks, thiscall, al),
// the D3D11 device g 0x75907C vt+0x28 (the flip interval, one int), the
// VuGfxSort g 0x759074 +0xC block (when the byte is set: g 0x759098 vt+0x3C(
// +0xA0, -1), the device's vt+0x30, the byte := 0, the device's vt+0xD0 - one
// hook `m_flush`, called with the byte already cleared), VuLensWaterManagerImpl g
// 0x759048 vt+0x18 (one bool). The game state 0x4B50A0 writes is data here:
// VuWater g 0x759058 +0x40 (float) / +0x44 / +0x45 / +0x46 (bytes) and the
// display object g 0x758F94 +0x11 (byte) - all written without a null test.
struct ui_settings_platform_t
{
    std::function< bool( ) > m_sys_54;                                        // g 0x759000 vt+0x54 (HOOK)
    std::function< bool( ) > m_sys_58;                                        // g 0x759000 vt+0x58 (HOOK)
    std::function< void( int ) > m_flip;                                      // g 0x75907C vt+0x28 (HOOK)
    std::function< void( ) > m_flush;                                         // the g 0x759074 +0xC block (HOOK)
    std::function< void( bool ) > m_lens;                                     // g 0x759048 vt+0x18 (HOOK)
    std::function< void( std::uint32_t, const ui_params_t& ) > m_broadcast;  // 0x505690
    bool m_gfx_pending = false;      // VuGfxSort g 0x759074 +0xC
    float m_water_scale = 0.0f;      // VuWater g 0x759058 +0x40
    std::uint8_t m_water_44 = 0;     // VuWater +0x44
    std::uint8_t m_water_45 = 0;     // VuWater +0x45
    std::uint8_t m_water_46 = 0;     // VuWater +0x46
    std::uint8_t m_display_11 = 0;   // g 0x758F94 +0x11
};

// VuSettingsManager's fidelity setter 0x4B50A0 (thiscall on g 0x758FE0, the new
// name by reference; ret 4), disassembled: nothing when the name equals +0x60
// (std::string compare 0x404AC0 + the lengths). Else the VuGfxSort block (above),
// +0x60 = the name (0x401DB0 unless the same object), then by name (VuString_equals
// 0x404990): "Low" -> VuWater +0x40 = 0.5, +0x44 = +0x45 = +0x46 = 0, display +0x11
// = 0, lens(0); "Medium" -> 1.0, 1, 1, 0, +0x11 = 0, lens(0); "High" -> 1.0, 1, 1,
// 1, +0x11 = 1, lens(1); any other name: none of those. Then (every changed name)
// broadcast FNV-1a "OnWaterSettingsChanged" with an empty VuParams.
inline void ui_settings_set_fidelity( ui_settings_t& s, const std::string& name, ui_settings_platform_t& p )
{
    if ( name == s.m_fidelity )
        return;
    if ( p.m_gfx_pending )
    {
        p.m_gfx_pending = false;
        if ( p.m_flush )
            p.m_flush( );
    }
    s.m_fidelity = name;
    const auto set = [ &p ]( float scale, std::uint8_t a, std::uint8_t b, std::uint8_t c, bool high )
    {
        p.m_water_scale = scale;   // 0x3F000000 / 0x3F800000
        p.m_water_44 = a;
        p.m_water_45 = b;
        p.m_water_46 = c;
        p.m_display_11 = high ? 1u : 0u;
        if ( p.m_lens )
            p.m_lens( high );
    };
    if ( name == "Low" )
        set( 0.5f, 0u, 0u, 0u, false );
    else if ( name == "Medium" )
        set( 1.0f, 1u, 1u, 0u, false );
    else if ( name == "High" )
        set( 1.0f, 1u, 1u, 1u, true );
    if ( p.m_broadcast )
        p.m_broadcast( fsm_hash( "OnWaterSettingsChanged" ), ui_params_t( ) );
}

struct ui_combo_box_hooks_t
{
    std::function< void( std::uint32_t, const ui_params_t& ) > m_broadcast;  // 0x505690
    std::function< void( const char*, const ui_params_t& ) > m_fire;         // findPlug 0x504770 + slot 2
    std::function< int( const char* ) > m_connections;                       // the plug's (+0x54 - +0x50) / 4
    std::function< void( const char* ) > m_sound;                            // FMOD getEvent + start (HOOK)
    std::function< float( ) > m_delta;                                       // the tick manager g 0x759008 slot 15 (HOOK)
    std::function< void( std::uint32_t vtable, int current ) > m_apply;      // the subclass's slot 12
};

class c_ui_combo_box
{
public:
    c_ui_combo_box( std::uint32_t id, std::uint32_t vtable, ui_combo_box_hooks_t& hooks ) : m_id( id ), m_vtable( vtable ), m_hooks( hooks ) { }

    // Slot 14 0x475230: focused = the id record (0x440FA0) names this entity (0x500D50).
    void on_set_focus( const ui_params_t& params )
    {
        ui_params_reader_t r( params );
        m_focused = ui_params_read( r, 1 ) == m_id;
    }

    // Slot 13 0x475190 (input SetFocus): broadcast "OnUISetFocus" with the id
    // record (0x440F20; every subscriber's handler runs inside), then focused.
    // Its VuRetVal: +0 = 0.
    void set_focus( )
    {
        ui_params_t p;
        ui_params_add( p, 1, m_id );
        if ( m_hooks.m_broadcast )
            m_hooks.m_broadcast( fsm_hash( "OnUISetFocus" ), p );
        m_focused = true;
    }

    // Slot 15 0x475280 (the params' float record is read into a local and
    // never used; dt = the tick manager's). The arrow rects in UI space: (1 /
    // scale) x (position + rect) for x / y, (1 / scale) x the size; a touch down
    // inside one (inclusive, comiss: unordered fails) calls SetFocus (slot 13)
    // and presses it; a press released steps the choice (0x475B40 -1 / +1),
    // and the press is dropped once the point leaves its rect. Then the up /
    // held bytes clear, the focus is latched, the down byte clears, and the
    // shown choice moves toward the current one by dt / the scroll time
    // (minss / maxss: the target wins an unordered compare).
    void on_tick( )
    {
        const float dt = m_hooks.m_delta ? m_hooks.m_delta( ) : 0.0f;
        const float ix = 1.0f / m_scale[ 0 ];   // 0x6F76B8
        const float iy = 1.0f / m_scale[ 1 ];
        const float lx = ix * ( m_position[ 0 ] + m_left[ 0 ] );
        const float ly = iy * ( m_left[ 1 ] + m_position[ 1 ] );
        const float lw = ix * m_left[ 2 ];
        const float lh = iy * m_left[ 3 ];
        const float rx = ix * ( m_position[ 0 ] + m_right[ 0 ] );
        const float ry = iy * ( m_right[ 1 ] + m_position[ 1 ] );
        const float rw = ix * m_right[ 2 ];
        const float rh = iy * m_right[ 3 ];
        const auto inside = [ this ]( float x, float y, float w, float h )
        {
            return m_touch[ 0 ] >= x && w + x >= m_touch[ 0 ] && m_touch[ 1 ] >= y && h + y >= m_touch[ 1 ];
        };
        if ( m_down )
        {
            if ( inside( lx, ly, lw, lh ) )
            {
                set_focus( );
                m_press = 1;
                m_left_pressed = 1;
            }
            else if ( inside( rx, ry, rw, rh ) )
            {
                set_focus( );
                m_press = 2;
                m_right_pressed = 1;
            }
        }
        if ( m_press == 1 )
        {
            if ( m_up )
            {
                change( -1 );
                m_press = 0;
                m_left_pressed = 0;
            }
            if ( !inside( lx, ly, lw, lh ) )
            {
                m_press = 0;
                m_left_pressed = 0;
            }
        }
        if ( m_press == 2 )
        {
            if ( m_up )
            {
                change( 1 );
                m_press = 0;
                m_right_pressed = 0;
            }
            if ( !inside( rx, ry, rw, rh ) )
            {
                m_press = 0;
                m_right_pressed = 0;
            }
        }
        if ( m_up )
        {
            m_held = 0;
            m_up = 0;
        }
        m_down = 0;
        m_latched = m_focused;
        const float step = dt / m_scroll_time;
        const float target = static_cast< float >( m_current );   // cvtdq2ps
        if ( target > m_shown )
        {
            const float sum = m_shown + step;
            m_shown = sum < target ? sum : target;
        }
        else
        {
            const float diff = m_shown - step;
            m_shown = diff > target ? diff : target;
        }
    }

    // Slot 16 0x475640: the input string read first; nothing unless the focus
    // the last tick saw (+0x2A0). InputUp / InputDown fire NavUp / NavDown with
    // one int record (0x4237B0 / 0x434E50) and, when that plug has a connection,
    // play menu_browse (inline for Up, 0x4D2D90 for Down - the same FMOD calls);
    // InputLeft / InputRight -> 0x475B40(-1 / +1).
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
            change( -1 );
        else if ( std::strcmp( input, "InputRight" ) == 0 )
            change( 1 );
    }

    // 0x475B40 (step): the new choice = min(current + step, count - 1), then 0 if
    // negative (signed); unchanged -> nothing. Else current = it, slot 12, fire
    // "SelectionChanged" with an empty VuParams, play menu_change.
    void change( int step )
    {
        int k = m_current + step;
        const int last = m_choices - 1;
        if ( last < k )
            k = last;
        if ( k < 0 )
            k = 0;
        if ( k == m_current )
            return;
        m_current = k;
        if ( m_hooks.m_apply )
            m_hooks.m_apply( m_vtable, m_current );
        if ( m_hooks.m_fire )
            m_hooks.m_fire( "SelectionChanged", ui_params_t( ) );
        if ( m_hooks.m_sound )
            m_hooks.m_sound( "Global/UI_and_Menu/menu_change" );
    }

    // VuUISubtitlesSettingEntity slot 6 0x46D7A0 (gameInitialize): the touch
    // callback (+0x5C, vtable 0x73DF24) registered through 0x53FB90 with priority 0;
    // "Settings_Off" then "Settings_On" pushed onto the choices +0x294 (0x457F50:
    // std::vector push_back of a VuString + a byte 0 - every run adds two more);
    // current +0x2A4 = VuSettingsManager g 0x758FE0 +0x5C (the subtitles byte, read
    // without a null test), shown +0x2A8 = (float) it.
    void initialize( ui_touch_device_t& touch, std::uint8_t subtitles )
    {
        ui_touch_add( touch, this, 0u );
        for ( const char* name : { "Settings_Off", "Settings_On" } )
            m_choice_texts.push_back( { name, 0u } );
        m_choices = static_cast< int >( m_choice_texts.size( ) );
        m_current = subtitles;
        m_shown = static_cast< float >( m_current );
    }

    // Slot 6 of every settings combo (gameInitialize; each registers the touch
    // callback +0x5C through 0x53FB90 with priority 0 first, then pushes its
    // choices with 0x457F50 - or 0x457830 on the record +0x278, the same push onto
    // +0x294 - every run adds them again). False: not a settings combo vtable.
    // - subtitles 0x46D7A0: initialize( ) above.
    // - units 0x46D3E0: k = 0, 1: push "Settings_Units_" + k_ui_units_names[k]
    //   (0x4366B0 = std::string operator+), then if settings +0x44 == that NAME
    //   (std::string compare): +0x2A4 = k, +0x2A8 = (float)k.
    // - fidelity 0x46DA70: the same over "Settings_Fidelity_" + k_ui_fidelity_names
    //   [k], k = 0..2, against settings +0x60.
    // - flip interval 0x46DE30: "Settings_FlipInterval_1", "Settings_FlipInterval_2";
    //   c = 2 - settings +0x78, then min( c, 1 ), then 0 if negative; +0x2A4 = c,
    //   +0x2A8 = (float)c.
    // - vibration 0x46CD70: "Settings_Off", "Settings_On"; +0x2A4 = settings +0x34
    //   (movzx; +0x2A8 NOT written).
    // - tilt steering 0x46C780: "Settings_Tilt_Off" if sys vt+0x54, then
    //   "Settings_Tilt_On" if sys vt+0x58; if the choices count > 1: a = vt+0x54,
    //   b = vt+0x58 (asked again), +0x2A4 = a ? ( b ? settings +0x2E : 0 ) : b
    //   (+0x2A8 NOT written).
    bool initialize_settings( ui_touch_device_t& touch, const ui_settings_t& s, ui_settings_platform_t& p )
    {
        const auto push = [ this ]( const std::string& name )
        {
            m_choice_texts.push_back( { name, 0u } );
            m_choices = static_cast< int >( m_choice_texts.size( ) );
        };
        const auto named = [ & ]( const char* prefix, const char* const* names, int count, const std::string& current )
        {
            for ( int k = 0; k < count; ++k )
            {
                push( std::string( prefix ) + names[ k ] );
                if ( current == names[ k ] )
                {
                    m_current = k;
                    m_shown = static_cast< float >( k );   // cvtdq2ps
                }
            }
        };
        if ( m_ours == ui_ours_frame_limit )   // ours, not retail
        {
            ui_touch_add( touch, this, 0u );
            for ( int k = 0; k < 10; ++k )
            {
                const std::int32_t hertz = k_ui_ours_limits[ k ];
                push( hertz != 0 ? "Settings_Ours_Limit_" + std::to_string( hertz ) : std::string( "Settings_Ours_Unlimited" ) );
                if ( hertz == s.m_ours.frame_limit )
                {
                    m_current = k;
                    m_shown = static_cast< float >( k );
                }
            }
            return true;
        }
        if ( m_ours == ui_ours_vsync )         // ours, not retail
        {
            ui_touch_add( touch, this, 0u );
            push( "Settings_Off" );
            push( "Settings_On" );
            m_current = s.m_ours.vsync ? 1 : 0;
            m_shown = static_cast< float >( m_current );
            return true;
        }
        switch ( m_vtable )
        {
        case k_ui_subtitles_vtable:
            initialize( touch, s.m_subtitles ? 1u : 0u );
            return true;
        case k_ui_units_vtable:
            ui_touch_add( touch, this, 0u );
            named( "Settings_Units_", k_ui_units_names, 2, s.m_units );
            return true;
        case k_ui_fidelity_vtable:
            ui_touch_add( touch, this, 0u );
            named( "Settings_Fidelity_", k_ui_fidelity_names, 3, s.m_fidelity );
            return true;
        case k_ui_flip_vtable:
        {
            ui_touch_add( touch, this, 0u );
            push( "Settings_FlipInterval_1" );
            push( "Settings_FlipInterval_2" );
            int c = 2 - s.m_flip_interval;
            if ( c > 1 )
                c = 1;
            if ( c < 0 )
                c = 0;
            m_current = c;
            m_shown = static_cast< float >( c );
            return true;
        }
        case k_ui_vibration_vtable:
            ui_touch_add( touch, this, 0u );
            push( "Settings_Off" );
            push( "Settings_On" );
            m_current = s.m_vibration ? 1 : 0;
            return true;
        case k_ui_tilt_vtable:
        {
            ui_touch_add( touch, this, 0u );
            const auto ask = []( const std::function< bool( ) >& f ) { return f ? f( ) : false; };
            if ( ask( p.m_sys_54 ) )
                push( "Settings_Tilt_Off" );
            if ( ask( p.m_sys_58 ) )
                push( "Settings_Tilt_On" );
            if ( static_cast< int >( m_choice_texts.size( ) ) > 1 )
            {
                const bool a = ask( p.m_sys_54 );
                const bool b = ask( p.m_sys_58 );
                m_current = a ? ( b ? ( s.m_tilt ? 1 : 0 ) : 0 ) : ( b ? 1 : 0 );
            }
            return true;
        }
        default:
            return false;
        }
    }

    // Slot 12 of every settings combo (the change 0x475B40 calls it) - the current
    // choice +0x2A4 into VuSettingsManager g 0x758FE0 (no null test). False: not a
    // settings combo, or a table index outside it (retail reads past - UNKNOWN).
    // - subtitles 0x46D9B0: +0x5C = current != 0.
    // - units 0x46D640: +0x44 = k_ui_units_names[current] (0x401DB0 unless the same object).
    // - fidelity 0x46DCD0: the setter 0x4B50A0 with k_ui_fidelity_names[current].
    // - flip interval 0x46E050: +0x78 = 2 - current, then the device's vt+0x28( it ).
    // - vibration 0x46CF70: +0x34 = current != 0.
    // - tilt steering 0x46C9B0: only when the choices count > 1: +0x2E = current != 0.
    bool apply_settings( ui_settings_t& s, ui_settings_platform_t& p ) const
    {
        if ( m_ours != ui_ours_none )          // ours, not retail
        {
            if ( m_ours == ui_ours_frame_limit )
            {
                if ( m_current < 0 || m_current >= 10 )
                    return false;
                s.m_ours.frame_limit = k_ui_ours_limits[ m_current ];
            }
            else
                s.m_ours.vsync = m_current != 0;
            if ( s.m_ours_changed )
                s.m_ours_changed( );
            return true;
        }
        switch ( m_vtable )
        {
        case k_ui_subtitles_vtable: s.m_subtitles = m_current != 0; return true;
        case k_ui_units_vtable:
            if ( m_current < 0 || m_current >= 2 )
                return false;
            s.m_units = k_ui_units_names[ m_current ];
            return true;
        case k_ui_fidelity_vtable:
            if ( m_current < 0 || m_current >= 3 )
                return false;
            ui_settings_set_fidelity( s, k_ui_fidelity_names[ m_current ], p );
            return true;
        case k_ui_flip_vtable:
            s.m_flip_interval = 2 - m_current;
            if ( p.m_flip )
                p.m_flip( s.m_flip_interval );
            return true;
        case k_ui_vibration_vtable: s.m_vibration = m_current != 0; return true;
        case k_ui_tilt_vtable:
            if ( static_cast< int >( m_choice_texts.size( ) ) > 1 )
                s.m_tilt = m_current != 0;
            return true;
        default: return false;
        }
    }

    // Slot 7 0x4253D0 (gameRelease): 0x53FA60 on the touch device with +0x5C.
    void release( ui_touch_device_t& touch ) { ui_touch_remove( touch, this ); }

    // The touch callbacks (vtable 0x73DF24, this = entity + 0x5C). Slot 0 0x474F80
    // (down, const VuVector2&): unless held (+0x2AE), +0x2AD = +0x2AE = 1 and the
    // point through VuUIImpl's inverse crop matrix 0x81A144 (m0 m1 m4 m5 m12 m13 -
    // an input) -> +0x2B0.
    void touch_down( const float inverse[ 16 ], const float point[ 2 ] )
    {
        if ( m_held )
            return;
        m_down = 1;
        m_held = 1;
        m_touch[ 0 ] = point[ 0 ] * inverse[ 0 ] + point[ 1 ] * inverse[ 4 ] + inverse[ 12 ];
        m_touch[ 1 ] = point[ 0 ] * inverse[ 1 ] + point[ 1 ] * inverse[ 5 ] + inverse[ 13 ];
    }

    // Slot 1 0x475080 (up): +0x2AF = 1.
    void touch_up( ) { m_up = 1; }

    // Slot 2 0x475090 (move): the device's touch 0 (g 0x759070 slot 7 vt+0x1C (0,
    // out) - a HOOK: `point`) mapped -> +0x2B0, always (retail's operand order).
    void touch_move( const float inverse[ 16 ], const float point[ 2 ] )
    {
        const float x = inverse[ 4 ] * point[ 1 ] + inverse[ 0 ] * point[ 0 ] + inverse[ 12 ];
        const float y = inverse[ 5 ] * point[ 1 ] + inverse[ 1 ] * point[ 0 ] + inverse[ 13 ];
        m_touch[ 0 ] = x;
        m_touch[ 1 ] = y;
    }

    std::uint32_t m_id;
    std::uint32_t m_vtable;
    int m_ours = ui_ours_none;       // ours, not retail: ui_ours_tag
    ui_combo_box_hooks_t& m_hooks;
    std::vector< std::pair< std::string, std::uint8_t > > m_choice_texts;   // +0x294 (28-byte entries: VuString, byte)
    // authored (OUR loaded properties)
    float m_scale[ 2 ] = { 100.0f, 100.0f };            // +0x68
    float m_position[ 2 ] = { 0.0f, 0.0f };             // +0x70
    float m_left[ 4 ] = { 0.0f, 0.0f, 100.0f, 100.0f };  // +0x258
    float m_right[ 4 ] = { 100.0f, 0.0f, 100.0f, 100.0f };  // +0x268
    float m_scroll_time = 0.25f;                         // block +0x120 (0x3E800000)
    // runtime
    int m_choices = 0;               // (+0x298 - +0x294) / 28
    int m_current = 0;               // +0x2A4
    float m_shown = 0.0f;            // +0x2A8
    std::uint8_t m_latched = 0;      // +0x2A0
    std::uint8_t m_left_pressed = 0;   // +0x2A1
    std::uint8_t m_right_pressed = 0;  // +0x2A2
    std::uint8_t m_focused = 0;      // +0x2AC
    std::uint8_t m_down = 0;         // +0x2AD
    std::uint8_t m_held = 0;         // +0x2AE
    std::uint8_t m_up = 0;           // +0x2AF
    float m_touch[ 2 ] = { 0.0f, 0.0f };  // +0x2B0
    int m_press = 0;                 // +0x2B8
};

// The combo box's authored fields from OUR loaded entity (0x474590's property names).
inline void ui_combo_box_fill( c_ui_combo_box& c, const entity_t& e )
{
    const auto take = [ &e ]( const char* name, float* out, std::size_t words )
    {
        if ( const ui_slot_t* s = ui_property_value( e, name ) )
            std::memcpy( out, s->m_bits, 4 * words );
    };
    take( "Screen Scale", c.m_scale, 2 );
    take( "Position X", &c.m_position[ 0 ], 1 );
    take( "Position Y", &c.m_position[ 1 ], 1 );
    const char* const rect[ 4 ] = { " X", " Y", " Width", " Height" };
    for ( int k = 0; k < 4; ++k )
    {
        take( ( std::string( "TouchLeft" ) + rect[ k ] ).c_str( ), &c.m_left[ k ], 1 );
        take( ( std::string( "TouchRight" ) + rect[ k ] ).c_str( ), &c.m_right[ k ], 1 );
    }
}
