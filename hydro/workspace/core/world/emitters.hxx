#pragma once

// The world's state for VuAudioEmitterEntity (the water agent's world/sound.hxx audio_emitter_t - verified against retail by
// case audio_emitter): its two property nodes from the ctor 0x5F3660 - "Event Name" +0x6C (VuAudioEventNameProperty, vtable
// 0x73A18C: its load slot 2 is 0x406C20, property_string's contract - the same node class the breakable's Sfx Name uses,
// verified by case breakable_properties) and "Initially Active" +0x84 (bool, 0).

#include <workspace/core/entity/property.hxx>
#include <workspace/core/world/force.hxx>
#include <workspace/core/world/keyframe.hxx>
#include <workspace/core/world/keymotion.hxx>
#include <workspace/core/world/sound.hxx>

#include <memory>
#include <string>

struct audio_emitter_state_t
{
    audio_emitter_t value;
    std::string name_default;
    std::uint8_t active_default = 0;
};

// VuKeyframeMotionEntity (world/keymotion.hxx key_motion_t, the water agent's - case key_motion): VuMotionEntity's ctor
// 0x61A000 nodes "Initially Active" / "One Shot" and the ctor 0x5CE400's "Loop" / "Ease In/Out" / "Affect Position" /
// "Affect Rotation" are VuBasicProperty<bool> (0x737450: property_bool); "Curve Type" is a VuStaticIntEnumProperty
// (0x74C354: property_enum over the table 0x752C18 - world batch 28; before it an authored one was UNKNOWN, not run).
// VuKeyframeEntity (ctor 0x5CE110): "Time" +0x60, VuBasicProperty<float> (0x737484: property_float).
struct key_motion_state_t
{
    key_motion_t value;
    std::uint8_t d_active = 0, d_one_shot = 0, d_loop = 0, d_ease = 1, d_position = 1, d_rotation = 1;
    std::int32_t d_curve = 0;
};

struct keyframe_state_t
{
    float time = 0.0f, time_default = 0.0f;
};

// VuSubstituteAssetEntity (create 0x5F47A0, ctor 0x5F48A0 over 0x500710(0), vtable 0x7570C4): "Asset Type" +0x60 - the
// ctor's one property (0x4C bytes, vtable 0x7570F8: the string node, its load slot 2 0x406C20 = property_string's
// contract; +0x48 the asset factory's vt+0x2C type list); +0x78 / +0x90 the two names. Its postLoad (slot 2 0x5F4B70)
// first runs 0x5F4C10 - the +0xA8 / +0xAC properties unlinked from the list and deleted, +0x78 / +0x90 = "", two new
// asset-name properties (0x53FC70: 0x68 bytes, vtable 0x74D6CC, the type = +0x60's text, "Asset Name" over +0x78 and
// "Subst Asset Name" over +0x90, +0x64 the asset 0) appended - then each one's vt+8 (0x406C20) on data["Properties"]
// (0x4FBDF0). Their notify (slot 11 0x53FDD0): a held asset released (factory vt+0x24), then when the factory's vt+0x28
// (doesAssetExist(type, name)) answers true +0x64 = its vt+0x1C (createAsset(type, name)). The postLoad runs once per
// load with the data being loaded: ours resets the two names on every property load and reads them from that load's node.
struct substitute_asset_state_t
{
    std::string type, type_default;   // +0x60 "Asset Type"
    std::string name, subst;          // +0x78 "Asset Name" / +0x90 "Subst Asset Name" (0x5F4C10 resets them: no default)
    bool post_loaded = false;
};

// VuControlEntity (world/keyframe.hxx control_entity_t, the water agent's - case control_entity): the ctor 0x5CCBA0's six
// VuBasicProperty<bool> (0x737450: property_bool) "Initially Active" +0x64, "One Shot" +0x65, "Network Synchronized" +0x66,
// "Loop" +0x67, "Ease In/Out" +0x68 (default 1), "Real Time" +0x69. Its VuControlKeyframeEntity children have no property
// of their own (ctor 0x5CC780): their list is made by the bind 0x5CC820 in slot 4 (control_post_load).
struct control_entity_state_t
{
    control_entity_t value;
    std::uint8_t d[ 6 ] = { 0, 0, 0, 0, 1, 0 };
};

inline std::uint8_t* control_entity_bools( control_entity_t& v, int k )
{
    std::uint8_t* const b[ 6 ] = { &v.initially_active, &v.one_shot, &v.network_sync, &v.loop, &v.ease, &v.real_time };
    return b[ k ];
}

inline const char* const k_control_entity_bools[ 6 ] = { "Initially Active", "One Shot", "Network Synchronized", "Loop", "Ease In/Out",
                                                         "Real Time" };

inline void control_entity_load_properties( entity_t& e, const value_t& p )
{
    if ( e.m_type != "VuControlEntity" || !e.m_state )
        return;
    control_entity_state_t& s = *static_cast< control_entity_state_t* >( e.m_state.get( ) );
    for ( int k = 0; k < 6; ++k )
        property_bool( p, k_control_entity_bools[ k ], *control_entity_bools( s.value, k ), s.d[ k ] );
}

inline void control_entity_mark_properties( entity_t& e )
{
    if ( e.m_type != "VuControlEntity" || !e.m_state )
        return;
    control_entity_state_t& s = *static_cast< control_entity_state_t* >( e.m_state.get( ) );
    for ( int k = 0; k < 6; ++k )
        s.d[ k ] = *control_entity_bools( s.value, k );
}

// VuEntity::load 0x5009D0's vt+8 (a normal load) / 0x5015D0's vt+0xC (a template override) on a VuControlEntity: its slots
// 2 / 3 0x5CD2C0 / 0x5CD2E0 keep data["ChildEntities"] (+0x90 / +0x94) for slot 4.
inline void control_entity_post_load( entity_t& e, const value_t& data, bool override )
{
    if ( e.m_type != "VuControlEntity" || !e.m_state )
        return;
    control_entity_t& v = static_cast< control_entity_state_t* >( e.m_state.get( ) )->value;
    if ( override )
        control_load_template( v, data );
    else
        control_load( v, data );
}

// VuLensWaterEmitterEntity (create 0x5E64C0, world/lens.hxx): its two VuBasicProperty<float> (0x737484: property_float)
// "Radius" +0x60 (25.0) and "Drops Per Second" +0x64 (64.0) - the ctor's defaults as the schema (engine entity/schema.hxx)
// holds them.
struct lens_emitter_state_t
{
    float radius = 25.0f, radius_default = 25.0f;
    float drops = 64.0f, drops_default = 64.0f;
};

// VuForceAffectorEntity (world/force.hxx, ctor 0x439300): "Initially Active" +0x68 (bool, 1), "Acceleration" +0x6C (98.06), "Max Speed"
// +0x70 (20), both VuBasicProperty<float> (0x737484: property_float).
struct force_affector_state_t
{
    force_affector_t value;
    std::uint8_t d_active = 1;
    float d_acceleration = value.acceleration, d_speed = value.max_speed;
};

inline std::shared_ptr< void > audio_emitter_state_for( const std::string& cls )
{
    if ( cls == "VuForceAffectorEntity" )
        return std::make_shared< force_affector_state_t >( );
    if ( cls == "VuLensWaterEmitterEntity" )
        return std::make_shared< lens_emitter_state_t >( );
    if ( cls == "VuControlEntity" )
        return std::make_shared< control_entity_state_t >( );
    if ( cls == "VuSubstituteAssetEntity" )
        return std::make_shared< substitute_asset_state_t >( );
    if ( cls == "VuKeyframeMotionEntity" )
        return std::make_shared< key_motion_state_t >( );
    if ( cls == "VuKeyframeEntity" )
        return std::make_shared< keyframe_state_t >( );
    return cls == "VuAudioEmitterEntity" ? std::make_shared< audio_emitter_state_t >( ) : nullptr;
}

inline void key_motion_load_properties( entity_t& e, const value_t& p )
{
    if ( e.m_type == "VuKeyframeEntity" && e.m_state )
    {
        keyframe_state_t& s = *static_cast< keyframe_state_t* >( e.m_state.get( ) );
        property_float( p, "Time", s.time, s.time_default, false );
        return;
    }
    if ( e.m_type != "VuKeyframeMotionEntity" || !e.m_state )
        return;
    key_motion_state_t& s = *static_cast< key_motion_state_t* >( e.m_state.get( ) );
    key_motion_t& v = s.value;
    property_bool( p, "Initially Active", v.initially_active, s.d_active );
    property_bool( p, "One Shot", v.one_shot, s.d_one_shot );
    property_bool( p, "Loop", v.loop, s.d_loop );
    property_bool( p, "Ease In/Out", v.ease, s.d_ease );
    property_bool( p, "Affect Position", v.affect_position, s.d_position );
    property_bool( p, "Affect Rotation", v.affect_rotation, s.d_rotation );
    // "Curve Type" +0x70: VuStaticIntEnumProperty (vtable 0x74C354) - the node class VuSwitchEntity's "Initial State" uses
    // (property_enum, verified there), over the table 0x752C18 {Linear 0, Cubic 1, Cubic Euler 2, Spline 3} (read from the
    // image: {char*, int} pairs, a null name ends it).
    static const property_choice_t k_curves[ 4 ] = { { "Linear", 0 }, { "Cubic", 1 }, { "Cubic Euler", 2 }, { "Spline", 3 } };
    property_enum( p, "Curve Type", v.curve, s.d_curve, k_curves, 4 );
}

inline void key_motion_mark_properties( entity_t& e )
{
    if ( e.m_type == "VuKeyframeEntity" && e.m_state )
    {
        keyframe_state_t& s = *static_cast< keyframe_state_t* >( e.m_state.get( ) );
        s.time_default = s.time;
    }
    if ( e.m_type == "VuKeyframeMotionEntity" && e.m_state )
    {
        key_motion_state_t& s = *static_cast< key_motion_state_t* >( e.m_state.get( ) );
        const key_motion_t& v = s.value;
        s.d_active = v.initially_active;
        s.d_one_shot = v.one_shot;
        s.d_loop = v.loop;
        s.d_ease = v.ease;
        s.d_position = v.affect_position;
        s.d_rotation = v.affect_rotation;
        s.d_curve = v.curve;
    }
}

inline void audio_emitter_load_properties( entity_t& e, const value_t& p )
{
    if ( e.m_type == "VuForceAffectorEntity" && e.m_state )
    {
        force_affector_state_t& s = *static_cast< force_affector_state_t* >( e.m_state.get( ) );
        property_bool( p, "Initially Active", s.value.initially_active, s.d_active );
        property_float( p, "Acceleration", s.value.acceleration, s.d_acceleration, false );
        property_float( p, "Max Speed", s.value.max_speed, s.d_speed, false );
        return;
    }
    if ( e.m_type == "VuSubstituteAssetEntity" && e.m_state )
    {
        substitute_asset_state_t& s = *static_cast< substitute_asset_state_t* >( e.m_state.get( ) );
        property_string( p, "Asset Type", s.type, s.type_default );
        if ( !s.post_loaded )
        {
            // VuEntity::load 0x5009D0's postLoad (slot 2): the names reset, then read from this load's node.
            s.post_loaded = true;
            s.name.clear( );
            s.subst.clear( );
        }
        // A template override (0x5015D0) loads every listed property (the two made by the postLoad above included) through
        // its vt+8 and calls vt+0xC (nullsub 0x4021A0), not the postLoad: an absent key keeps the value.
        const std::string name = s.name, subst = s.subst;
        property_string( p, "Asset Name", s.name, name );
        property_string( p, "Subst Asset Name", s.subst, subst );
        return;
    }
    if ( e.m_type == "VuLensWaterEmitterEntity" && e.m_state )
    {
        lens_emitter_state_t& s = *static_cast< lens_emitter_state_t* >( e.m_state.get( ) );
        property_float( p, "Radius", s.radius, s.radius_default, false );
        property_float( p, "Drops Per Second", s.drops, s.drops_default, false );
        return;
    }
    if ( e.m_type != "VuAudioEmitterEntity" || !e.m_state )
        return;
    audio_emitter_state_t& s = *static_cast< audio_emitter_state_t* >( e.m_state.get( ) );
    property_string( p, "Event Name", s.value.event_name, s.name_default );
    property_bool( p, "Initially Active", s.value.initially_active, s.active_default );
}

inline void audio_emitter_mark_properties( entity_t& e )
{
    if ( e.m_type == "VuForceAffectorEntity" && e.m_state )
    {
        force_affector_state_t& s = *static_cast< force_affector_state_t* >( e.m_state.get( ) );
        s.d_active = s.value.initially_active;
        s.d_acceleration = s.value.acceleration;
        s.d_speed = s.value.max_speed;
    }
    if ( e.m_type == "VuLensWaterEmitterEntity" && e.m_state )
    {
        lens_emitter_state_t& s = *static_cast< lens_emitter_state_t* >( e.m_state.get( ) );
        s.radius_default = s.radius;
        s.drops_default = s.drops;
    }
    if ( e.m_type == "VuSubstituteAssetEntity" && e.m_state )
        static_cast< substitute_asset_state_t* >( e.m_state.get( ) )->type_default = static_cast< substitute_asset_state_t* >( e.m_state.get( ) )->type;
    if ( e.m_type == "VuAudioEmitterEntity" && e.m_state )
    {
        audio_emitter_state_t& s = *static_cast< audio_emitter_state_t* >( e.m_state.get( ) );
        s.name_default = s.value.event_name;
        s.active_default = s.value.initially_active;
    }
}
