#pragma once

#include <workspace/core/entity/property.hxx>

#include <cstdint>
#include <cstring>
#include <string>

// The property sets of the logic / action / motion / settings entities the
// world consumes, each read from its ctor (node order, class, target, default).
// Every class is a verified load: bool 0x737450, float 0x737484, angle
// 0x73A490 (float + the 0x3C8EFA35 filter), VuVector3 0x737A84 (load 0x409280 /
// value read 0x409330), the string family (VuStringProperty 0x7374B8 and the
// VuAssetNameProperty 0x74D690 subclasses: load 0x406C20 / value read 0x406CD0 /
// filter 0x406AB0). One new class: VuRotation3dProperty-style 0x73D24C, the
// vector load with slot 9 = 0x45CB50 (each lane x [0x6F75DC] = 0x3C8EFA35).

inline float actions_bits( std::uint32_t b )
{
    float f;
    std::memcpy( &f, &b, sizeof( f ) );
    return f;
}

// 0x73D24C: the vector load 0x409280 / 0x409330 with the filter 0x45CB50
// (lanes x 0x3C8EFA35) before the store-if-differs.
inline void property_rotation3( const value_t& properties, const char* name, float* target, const float* fallback )
{
    for ( int lane = 0; lane < 3; ++lane )
        target[ lane ] = fallback[ lane ];
    float parsed[ 3 ];
    if ( !vec3( member( properties, name ), parsed ) )
        return;
    const float k = actions_bits( 0x3C8EFA35u );
    for ( float& lane : parsed )
        lane *= k;
    if ( target[ 0 ] != parsed[ 0 ] || target[ 1 ] != parsed[ 1 ] || target[ 2 ] != parsed[ 2 ] )
        for ( int lane = 0; lane < 3; ++lane )
            target[ lane ] = parsed[ lane ];
}

// VuDelayEntity (create 0x5DF7B0, ctor 0x5DF830): "Delay" float +0x60 = 1.0,
// "Random Window" float +0x64 = 0.
struct delay_properties_t
{
    float delay;
    float random_window;
};

inline void delay_construct( delay_properties_t& p )
{
    p.delay = actions_bits( 0x3F800000u );
    p.random_window = 0.0f;
}

inline void delay_load( delay_properties_t& p, const value_t& v, const delay_properties_t& d )
{
    property_float( v, "Delay", p.delay, d.delay, false );
    property_float( v, "Random Window", p.random_window, d.random_window, false );
}

// VuPowerUpEntity (ctor 0x43B180): "Initially Enabled" bool +0x70 = 1, "Type" +0x74, "Reflect" +0x8C = 0, "Handle Wave
// Height" +0x8D = 0, "Height Above Water" float +0x90 = 0, "Voice Over Entry" +0x94; the boost's (ctor 0x43D860)
// "Randomize for Multiplayer" +0x138 = 1. The two DB entries load as the names they hold.
struct powerup_properties_t
{
    std::uint8_t initially_enabled;
    std::string type;
    std::uint8_t reflect;
    std::uint8_t handle_wave_height;
    float height_above_water;
    std::string voice_over;
    std::uint8_t randomize;
};

inline void powerup_construct( powerup_properties_t& p )
{
    p.initially_enabled = 1;
    p.type.clear( );
    p.reflect = 0;
    p.handle_wave_height = 0;
    p.height_above_water = 0.0f;
    p.voice_over.clear( );
    p.randomize = 1;
}

inline void powerup_load( powerup_properties_t& p, const value_t& v, const powerup_properties_t& d )
{
    property_bool( v, "Initially Enabled", p.initially_enabled, d.initially_enabled );
    property_string( v, "Type", p.type, d.type );
    property_bool( v, "Reflect", p.reflect, d.reflect );
    property_bool( v, "Handle Wave Height", p.handle_wave_height, d.handle_wave_height );
    property_float( v, "Height Above Water", p.height_above_water, d.height_above_water, false );
    property_string( v, "Voice Over Entry", p.voice_over, d.voice_over );
    property_bool( v, "Randomize for Multiplayer", p.randomize, d.randomize );
}

// VuBooleanEntity (create 0x5E5810, ctor 0x5E5880): "Value" bool +0x60 = 0.
struct boolean_properties_t
{
    std::uint8_t value;
};

inline void boolean_construct( boolean_properties_t& p )
{
    p.value = 0;
}

inline void boolean_load( boolean_properties_t& p, const value_t& v, const boolean_properties_t& d )
{
    property_bool( v, "Value", p.value, d.value );
}

// VuGenericEventEntity (create 0x5F0000, ctor 0x5F00E0): "Event Name"
// VuStringProperty (0x406B70) on the std::string +0x5C = "".
struct generic_event_properties_t
{
    std::string event_name;
};

inline void generic_event_construct( generic_event_properties_t& p )
{
    p.event_name.clear( );
}

inline void generic_event_load( generic_event_properties_t& p, const value_t& v, const generic_event_properties_t& d )
{
    property_string( v, "Event Name", p.event_name, d.event_name );
}

// VuRotateMotionEntity (create 0x5D0D40, ctor 0x5D0DC0 over VuMotionEntity's
// 0x61A000): the base's "Initially Active" +0x64 = 0 and "One Shot" +0x65 = 0
// (bools), then "Axis" VuVector3 +0x70 = (0, 0, 1), "Speed" angle (re-tagged
// 0x73A490) +0x80 = 0x3FC90FDB, "Local" bool +0x84 = 1.
struct rotate_motion_properties_t
{
    std::uint8_t initially_active;
    std::uint8_t one_shot;
    float axis[ 3 ];
    float speed;
    std::uint8_t local;
};

inline void rotate_motion_construct( rotate_motion_properties_t& p )
{
    p.initially_active = 0;
    p.one_shot = 0;
    p.axis[ 0 ] = 0.0f;
    p.axis[ 1 ] = 0.0f;
    p.axis[ 2 ] = actions_bits( 0x3F800000u );
    p.speed = actions_bits( 0x3FC90FDBu );
    p.local = 1;
}

inline void rotate_motion_load( rotate_motion_properties_t& p, const value_t& v, const rotate_motion_properties_t& d )
{
    property_bool( v, "Initially Active", p.initially_active, d.initially_active );
    property_bool( v, "One Shot", p.one_shot, d.one_shot );
    property_vector3( v, "Axis", p.axis, d.axis );
    property_float( v, "Speed", p.speed, d.speed, true );
    property_bool( v, "Local", p.local, d.local );
}

// VuAttachEntity (create 0x5F4EA0, ctor 0x5F4FA0): "Attach at Start" bool +0x60
// = 0, "Relative Position" VuVector3 +0x64, "Relative Rotation" (re-tagged
// 0x73D24C: stored in RADIANS) +0x74, "Node Name" VuStringProperty on the
// std::string +0x84; then the refs Parent / Child (VuScriptRef 0x74D8EC - the
// script component's, not properties) and the Attach / Detach plugs.
struct attach_properties_t
{
    std::uint8_t attach_at_start;
    float relative_position[ 3 ];
    float relative_rotation[ 3 ];
    std::string node_name;
};

inline void attach_construct( attach_properties_t& p )
{
    p.attach_at_start = 0;
    for ( int k = 0; k < 3; ++k )
        p.relative_position[ k ] = p.relative_rotation[ k ] = 0.0f;
    p.node_name.clear( );
}

inline void attach_load( attach_properties_t& p, const value_t& v, const attach_properties_t& d )
{
    property_bool( v, "Attach at Start", p.attach_at_start, d.attach_at_start );
    property_vector3( v, "Relative Position", p.relative_position, d.relative_position );
    property_rotation3( v, "Relative Rotation", p.relative_rotation, d.relative_rotation );
    property_string( v, "Node Name", p.node_name, d.node_name );
}

// VuPlayAnimationEntity (create 0x5F5850, ctor 0x5F5950): "Animation Asset"
// (0x5F5FE0: a VuAssetNameProperty, vtable 0x757304) and "Timed Event Asset"
// (0x5F6110: vtable 0x7572C8), both the string load; then "Start Time" +0x90 =
// 0, "Blend Time" +0x94 = 0, "Time Factor" +0x98 = 1.0 (floats), "Looping"
// +0x9C = 1, "One Shot" +0x9D = 0 (bools); the PlayAnimation output plug.
struct play_animation_properties_t
{
    std::string animation_asset;
    std::string timed_event_asset;
    float start_time;
    float blend_time;
    float time_factor;
    std::uint8_t looping;
    std::uint8_t one_shot;
};

inline void play_animation_construct( play_animation_properties_t& p )
{
    p.animation_asset.clear( );
    p.timed_event_asset.clear( );
    p.start_time = 0.0f;
    p.blend_time = 0.0f;
    p.time_factor = actions_bits( 0x3F800000u );
    p.looping = 1;
    p.one_shot = 0;
}

inline void play_animation_load( play_animation_properties_t& p, const value_t& v, const play_animation_properties_t& d )
{
    property_string( v, "Animation Asset", p.animation_asset, d.animation_asset );
    property_string( v, "Timed Event Asset", p.timed_event_asset, d.timed_event_asset );
    property_float( v, "Start Time", p.start_time, d.start_time, false );
    property_float( v, "Blend Time", p.blend_time, d.blend_time, false );
    property_float( v, "Time Factor", p.time_factor, d.time_factor, false );
    property_bool( v, "Looping", p.looping, d.looping );
    property_bool( v, "One Shot", p.one_shot, d.one_shot );
}

// VuSkyBoxEntity (create 0x5EB790, ctor 0x5EB810): "Model Asset" (0x5EC030: a
// VuAssetNameProperty, vtable 0x756018) on the std::string +0x64.
struct sky_box_properties_t
{
    std::string model_asset;
    std::uint8_t reflect = 1;   // its Vu3dDrawComponent's "Reflect" +0x1C: 0x5EB8F0 passes 1 to the ctor 0x531A40, the draw manager's mask
};

inline void sky_box_construct( sky_box_properties_t& p )
{
    p.model_asset.clear( );
    p.reflect = 1;
}

inline void sky_box_load( sky_box_properties_t& p, const value_t& v, const sky_box_properties_t& d )
{
    property_string( v, "Model Asset", p.model_asset, d.model_asset );
}

// VuWaterTextureSettingsEntity (create 0x5D2B50, ctor 0x5D2BD0): eight floats
// +0x60 .. +0x7C, then "Use" (nop load).
struct water_texture_properties_t
{
    float gravity, world_size, wind_speed, directional_power, suppression_wave_length, height_factor, time_factor,
        normal_texture_scale;
};

inline void water_texture_construct( water_texture_properties_t& p )
{
    p.gravity = actions_bits( 0x411CD0E5u );
    p.world_size = actions_bits( 0x41200000u );
    p.wind_speed = actions_bits( 0x41200000u );
    p.directional_power = 0.0f;
    p.suppression_wave_length = 0.0f;
    p.height_factor = actions_bits( 0x3F800000u );
    p.time_factor = actions_bits( 0x3F800000u );
    p.normal_texture_scale = actions_bits( 0x3F000000u );
}

inline void water_texture_load( water_texture_properties_t& p, const value_t& v, const water_texture_properties_t& d )
{
    property_float( v, "Gravity", p.gravity, d.gravity, false );
    property_float( v, "WorldSize", p.world_size, d.world_size, false );
    property_float( v, "WindSpeed", p.wind_speed, d.wind_speed, false );
    property_float( v, "DirectionalPower", p.directional_power, d.directional_power, false );
    property_float( v, "SuppressionWaveLength", p.suppression_wave_length, d.suppression_wave_length, false );
    property_float( v, "HeightFactor", p.height_factor, d.height_factor, false );
    property_float( v, "TimeFactor", p.time_factor, d.time_factor, false );
    property_float( v, "NormalTextureScale", p.normal_texture_scale, d.normal_texture_scale, false );
}

// VuSetFrontEndCameraEntity (create 0x408A80, ctor 0x408B00): "Interpolation
// Time" float +0x60 = 2.0, three bools +0x64 / +0x65 / +0x66 = 0, "Eye Position"
// / "Target Position" VuVector3 +0x68 / +0x78, "Far Plane" float +0x88 = 1000.
struct front_end_camera_properties_t
{
    float interpolation_time;
    std::uint8_t modify_eye, modify_target, modify_far;
    float eye[ 3 ];
    float target[ 3 ];
    float far_plane;
};

inline void front_end_camera_construct( front_end_camera_properties_t& p )
{
    p.interpolation_time = actions_bits( 0x40000000u );
    p.modify_eye = p.modify_target = p.modify_far = 0;
    for ( int k = 0; k < 3; ++k )
        p.eye[ k ] = p.target[ k ] = 0.0f;
    p.far_plane = actions_bits( 0x447A0000u );
}

inline void front_end_camera_load( front_end_camera_properties_t& p, const value_t& v, const front_end_camera_properties_t& d )
{
    property_float( v, "Interpolation Time", p.interpolation_time, d.interpolation_time, false );
    property_bool( v, "Modify Eye Position", p.modify_eye, d.modify_eye );
    property_bool( v, "Modify Target Position", p.modify_target, d.modify_target );
    property_bool( v, "Modify Far Plane", p.modify_far, d.modify_far );
    property_vector3( v, "Eye Position", p.eye, d.eye );
    property_vector3( v, "Target Position", p.target, d.target );
    property_float( v, "Far Plane", p.far_plane, d.far_plane, false );
}

// VuPointWaveEntity (create 0x5D42B0, ctor 0x5D43B0; schema.hxx g_schema_VuPointWaveEntity): seven floats +0x80 ..
// +0x98 - Falloff Time 5, Magnitude 1, Range Start 0, Range End 20, Speed 10, Frequency 5, Foaminess 1 - in the
// order VuPointWaveEntity_tick 0x5D4C50 copies them into the wave's descriptor (surface.hxx water_point_wave_emit).
struct point_wave_properties_t
{
    float word[ 7 ];   // +0x80 .. +0x98
};

inline void point_wave_construct( point_wave_properties_t& p )
{
    const std::uint32_t d[ 7 ] = { 0x40A00000u, 0x3F800000u, 0x00000000u, 0x41A00000u, 0x41200000u, 0x40A00000u, 0x3F800000u };
    for ( int k = 0; k < 7; ++k )
        p.word[ k ] = actions_bits( d[ k ] );
}

inline void point_wave_load( point_wave_properties_t& p, const value_t& v, const point_wave_properties_t& d )
{
    static const char* const names[ 7 ] = { "Falloff Time", "Magnitude", "Range Start", "Range End", "Speed", "Frequency", "Foaminess" };
    for ( int k = 0; k < 7; ++k )
        property_float( v, names[ k ], p.word[ k ], d.word[ k ], false );
}