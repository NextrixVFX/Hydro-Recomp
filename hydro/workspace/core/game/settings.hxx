#pragma once

// VuSettingsManager (g 0x758FE0, ctor 0x4B4650, vtable 0x745A68) and the part of VuCloudManager (g 0x7590FC, ctor 0x5C5D00,
// vtable 0x752420) that lives in the profile (boat batch 33). Read from the decompilation.
//
// VuSettingsManager: +0xC the platform's default fidelity, +0x24 its default display scale, +0x28 its default flip
// interval (all three from slot 5 0x4B4800, which also registers the "OnSaveProfile" listener 0x4B5240: the caller's); +0x2C /
// +0x2D auto throttle (gamepad, keyboard); the rest is ui/slider.hxx ui_settings_t, the fields the settings entities read.
// Slot 1 0x4B4870 = settings_load, the listener = settings_save.
// HOOKS: FMOD's category volume (the audio device), g_vubarracudametrogfxcomposer +0xC (`m_display_out`, written), the
// fidelity's side effects (ui_settings_platform_t), g_vumetrod3d11gfx's vt+0x28 (flip interval), g_vumetrosys's vt+0x54 /
// +0x58 (the tilt device).
//
// VuCloudManager: everything but its profile members is the online side (identity.php, tuning, news, an FSM over
// g_vumetrohttpclient): NOT RECREATED, by the Saves decision. Its load 0x5C62C0 and "OnSaveProfile" listener 0x5C63B0 (thunk
// 0x5C5C20) keep the profile's "Cloud" member round-tripping the same bytes.

#include <workspace/core/entity/json.hxx>
#include <workspace/core/game/unlock.hxx>
#include <workspace/core/ui/chooser.hxx>
#include <workspace/core/ui/slider.hxx>

#include <cstdint>
#include <functional>
#include <string>

struct settings_hooks_t
{
    std::function< void( const char* category, float volume ) > category;   // FMOD getCategory, on success vt+0x20 setVolume
    std::function< void( std::int32_t ) > flip_interval;                    // g 0x75907C vt+0x28
    std::function< bool( ) > has_accelerometer;                             // g 0x759000 vt+0x54
    std::function< bool( ) > has_tilt;                                      // g 0x759000 vt+0x58
};

struct settings_t
{
    ui_settings_t ui;
    std::string default_fidelity;      // +0xC
    float default_display = 0.0f;      // +0x24
    std::int32_t default_flip = 0;     // +0x28
    bool auto_throttle_pad = false;    // +0x2C
    bool auto_throttle_keys = false;   // +0x2D
};

// Slot 5 0x4B4800 (float, const VuString&, int; retn 0xC): the listener's registration is the caller's.
inline void settings_configure( settings_t& s, float display, const std::string& fidelity, std::int32_t flip )
{
    s.default_fidelity = fidelity;
    s.default_display = display;
    s.default_flip = flip;
}

// A float read: int (cvtdq2ps), float, int64 (fild, fstp). False leaves `out` alone.
inline bool settings_float( const value_t& v, float& out )
{
    if ( v.tag == 1 )
        out = static_cast< float >( v.integer );
    else if ( v.tag == 2 )
        out = v.real;
    else if ( v.tag == 7 )
        out = static_cast< float >( v.int64 );
    else
        return false;
    return true;
}

inline bool settings_bool( const value_t& v )
{
    return v.tag == 3 && v.integer != 0;
}

// 0x4B4C60: the volumes (FMOD: "game" and "ui" share the effect volume, "music" its own) and the subtitles.
inline void settings_defaults_audio( settings_t& s, const value_t& metro, const settings_hooks_t& h )
{
    float effect = 0.0f, music = 0.0f;
    settings_float( member( metro, "EffectVolume" ), effect );
    s.ui.m_effect = effect;
    h.category( "game", effect );
    h.category( "ui", effect );
    settings_float( member( metro, "MusicVolume" ), music );
    s.ui.m_music = music;
    h.category( "music", music );
    s.ui.m_subtitles = settings_bool( member( metro, "Subtitles" ) );
}

// 0x4B4FC0: the platform's fidelity, display scale and flip interval (slot 5's), and "Units".
inline void settings_defaults_display( settings_t& s, const value_t& metro, const settings_hooks_t& h, ui_settings_platform_t& p )
{
    ui_settings_set_fidelity( s.ui, s.default_fidelity, p );
    s.ui.m_display = s.default_display;
    s.ui.m_display_out = s.default_display;
    s.ui.m_flip_interval = s.default_flip;
    h.flip_interval( s.default_flip );
    const value_t& units = member( metro, "Units" );
    s.ui.m_units = units.tag == 4 ? units.text : std::string( );
}

// Slot 1 0x4B4870: ConstantDB's VuDBAsset / DefaultSettings / Metro, then the profile's Settings over it, member by member.
inline void settings_load( settings_t& s, const value_t& constant_db, const value_t& profile, const settings_hooks_t& h, ui_settings_platform_t& p )
{
    const value_t& metro = member( member( member( constant_db, "VuDBAsset" ), "DefaultSettings" ), "Metro" );
    settings_defaults_audio( s, metro, h );
    s.auto_throttle_pad = settings_bool( member( metro, "AutoThrottleGamePad" ) );    // 0x4B4E20
    s.ui.m_vibration = settings_bool( member( metro, "Vibration" ) );
    s.auto_throttle_keys = settings_bool( member( metro, "AutoThrottleKeyboard" ) );
    s.ui.m_tilt = settings_bool( member( metro, "TiltSteering" ) );                   // 0x4B4F00
    s.ui.m_steering = 0.0f;
    settings_float( member( metro, "SteeringSensitivity" ), s.ui.m_steering );
    settings_defaults_display( s, metro, h, p );
    const value_t& saved = member( profile, "Settings" );
    const value_t& controls = member( saved, "Controls" );
    const value_t& pad = member( controls, "AutoThrottleGamePad" );
    if ( pad.tag == 3 )
        s.auto_throttle_pad = pad.integer != 0;
    const value_t& keys = member( controls, "AutoThrottleKeyboard" );
    if ( keys.tag == 3 )
        s.auto_throttle_keys = keys.integer != 0;
    const value_t& tilt = member( controls, "TiltSteering" );
    if ( tilt.tag == 3 )
        s.ui.m_tilt = tilt.integer != 0;
    settings_float( member( controls, "SteeringSensitivity" ), s.ui.m_steering );
    const value_t& vibration = member( controls, "Vibration" );
    if ( vibration.tag == 3 )
        s.ui.m_vibration = vibration.integer != 0;
    const value_t& audio = member( saved, "Audio" );
    float v = 0.0f;
    if ( settings_float( member( audio, "EffectVolume" ), v ) )
    {
        s.ui.m_effect = v;
        h.category( "game", v );
        h.category( "ui", v );
    }
    if ( settings_float( member( audio, "MusicVolume" ), v ) )
    {
        s.ui.m_music = v;
        h.category( "music", v );
    }
    const value_t& subtitles = member( audio, "Subtitles" );
    if ( subtitles.tag == 3 )
        s.ui.m_subtitles = subtitles.integer != 0;
    const value_t& graphics = member( saved, "Graphics" );
    const value_t& fidelity = member( graphics, "Fidelity" );
    if ( fidelity.tag == 4 )
        ui_settings_set_fidelity( s.ui, fidelity.text, p );
    if ( settings_float( member( graphics, "DisplayScale" ), v ) )
    {
        s.ui.m_display = v;
        s.ui.m_display_out = v;
    }
    std::int32_t flip = 0;
    if ( unlock_int( member( graphics, "FlipInterval" ), flip ) )
    {
        s.ui.m_flip_interval = flip;
        h.flip_interval( flip );
    }
    const value_t& units = member( graphics, "Units" );
    if ( units.tag == 4 )
        s.ui.m_units = units.text;
}

// 0x4B5240, the "OnSaveProfile" listener. TiltSteering: with both device answers true the setting, else the second answer.
inline void settings_save( const settings_t& s, value_t& profile, const settings_hooks_t& h )
{
    const auto set_bool = [ ]( value_t& j, bool v ) {
        json_clear( j );
        j.tag = 3;
        j.kind = value_t::kind_t::integer;
        j.integer = v ? 1 : 0;
    };
    value_t& saved = json_member( profile, "Settings" );
    value_t& controls = json_member( saved, "Controls" );
    set_bool( json_member( controls, "AutoThrottleGamePad" ), s.auto_throttle_pad );
    set_bool( json_member( controls, "AutoThrottleKeyboard" ), s.auto_throttle_keys );
    const bool accel = h.has_accelerometer( );
    const bool tilt = h.has_tilt( );
    set_bool( json_member( controls, "TiltSteering" ), accel && tilt ? s.ui.m_tilt : tilt );
    json_set_float( json_member( controls, "SteeringSensitivity" ), s.ui.m_steering );
    set_bool( json_member( controls, "Vibration" ), s.ui.m_vibration );
    value_t& audio = json_member( saved, "Audio" );
    json_set_float( json_member( audio, "EffectVolume" ), s.ui.m_effect );
    json_set_float( json_member( audio, "MusicVolume" ), s.ui.m_music );
    set_bool( json_member( audio, "Subtitles" ), s.ui.m_subtitles );
    value_t& graphics = json_member( saved, "Graphics" );
    json_set_string( json_member( graphics, "Fidelity" ), s.ui.m_fidelity );
    json_set_float( json_member( graphics, "DisplayScale" ), s.ui.m_display );
    json_set_int( json_member( graphics, "FlipInterval" ), s.ui.m_flip_interval );
    json_set_string( json_member( graphics, "Units" ), s.ui.m_units );
}

// ---- OURS, NOT RETAIL: the user's Video settings (ui/slider.hxx). Their own "Settings/Ours" member, so 0x4B4870 / 0x4B5240 stay 1:1 ------
// `fov_default` is ConstantDB's VerticalFOV; a value the profile lacks keeps its default.
inline void settings_ours_defaults( ui_settings_ours_t& o )
{
    const float fov = o.fov_default;
    o = ui_settings_ours_t{ };
    o.fov_default = fov;
    o.fov = fov;
}

inline void settings_ours_load( ui_settings_ours_t& o, const value_t& profile )
{
    const value_t& ours = member( member( profile, "Settings" ), "Ours" );
    unlock_int( member( ours, "FrameLimit" ), o.frame_limit );
    const value_t& vsync = member( ours, "VSync" );
    if ( vsync.tag == 3 )
        o.vsync = vsync.integer != 0;
    settings_float( member( ours, "FieldOfView" ), o.fov );
    settings_float( member( ours, "CameraShake" ), o.shake );
    settings_float( member( ours, "CameraZoomOut" ), o.zoom );
}

inline void settings_ours_save( const ui_settings_ours_t& o, value_t& profile )
{
    value_t& ours = json_member( json_member( profile, "Settings" ), "Ours" );
    json_set_int( json_member( ours, "FrameLimit" ), o.frame_limit );
    value_t& vsync = json_member( ours, "VSync" );
    json_clear( vsync );
    vsync.tag = 3;
    vsync.kind = value_t::kind_t::integer;
    vsync.integer = o.vsync ? 1 : 0;
    json_set_float( json_member( ours, "FieldOfView" ), o.fov );
    json_set_float( json_member( ours, "CameraShake" ), o.shake );
    json_set_float( json_member( ours, "CameraZoomOut" ), o.zoom );
}

// ---- VuCloudManager, the profile's "Cloud" member ------------------------------------------------------------------

// A VuDateTime, six u16s: year, month, day, hour, minute, second.
struct cloud_time_t
{
    std::uint16_t parts[ 6 ]{};
};

inline const char* const k_cloud_time_keys[ 6 ] = { "Year", "Month", "Day", "Hour", "Minute", "Second" };

// 0x4FE740: all six or none (each read 0x4FDB90's); the low 16 bits are stored.
inline bool cloud_time_read( const value_t& in, cloud_time_t& out )
{
    std::int32_t v[ 6 ];
    for ( int k = 0; k < 6; ++k )
        if ( !unlock_int( member( in, k_cloud_time_keys[ k ] ), v[ k ] ) )
            return false;
    for ( int k = 0; k < 6; ++k )
        out.parts[ k ] = static_cast< std::uint16_t >( v[ k ] );
    return true;
}

// 0x4FEA10.
inline void cloud_time_write( const cloud_time_t& t, value_t& out )
{
    for ( int k = 0; k < 6; ++k )
        json_set_int( json_member( out, k_cloud_time_keys[ k ] ), t.parts[ k ] );
}

struct cloud_profile_t
{
    bool identity_saved = false;   // +0x68
    value_t news;                  // +0x38
    cloud_time_t news_time;        // +0x48
    bool alert = false;            // +0x54
    value_t tuning;                // +0x70
    cloud_time_t tuning_time;      // +0x80
};

// 0x5C62C0. The times keep their last value on a failed read; Alert keeps its own when not a bool.
inline void cloud_profile_load( cloud_profile_t& c, const value_t& profile )
{
    const value_t& cloud = member( profile, "Cloud" );
    c.identity_saved = settings_bool( member( cloud, "IdentitySaved" ) );
    const value_t& news = member( cloud, "News" );
    c.news = member( news, "Data" );
    cloud_time_read( member( news, "LastCheckTime" ), c.news_time );
    const value_t& alert = member( news, "Alert" );
    if ( alert.tag == 3 )
        c.alert = alert.integer != 0;
    const value_t& tuning = member( cloud, "Tuning" );
    c.tuning = member( tuning, "Data" );
    cloud_time_read( member( tuning, "LastCheckTime" ), c.tuning_time );
}

// 0x5C63B0, the "OnSaveProfile" listener: "Cloud" is cleared first.
inline void cloud_profile_save( const cloud_profile_t& c, value_t& profile )
{
    value_t& cloud = json_member( profile, "Cloud" );
    json_clear( cloud );
    const auto set_bool = [ ]( value_t& j, bool v ) {
        json_clear( j );
        j.tag = 3;
        j.kind = value_t::kind_t::integer;
        j.integer = v ? 1 : 0;
    };
    set_bool( json_member( cloud, "IdentitySaved" ), c.identity_saved );
    value_t& news = json_member( cloud, "News" );
    json_member( news, "Data" ) = c.news;
    cloud_time_write( c.news_time, json_member( news, "LastCheckTime" ) );
    set_bool( json_member( news, "Alert" ), c.alert );
    value_t& tuning = json_member( cloud, "Tuning" );
    json_member( tuning, "Data" ) = c.tuning;
    cloud_time_write( c.tuning_time, json_member( tuning, "LastCheckTime" ) );
}
