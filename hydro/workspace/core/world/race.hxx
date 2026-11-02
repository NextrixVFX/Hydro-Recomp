#pragma once

#include <workspace/core/assets/archive.hxx>
#include <workspace/core/assets/reader.hxx>
#include <workspace/core/assets/tree.hxx>
#include <workspace/core/boat/driver.hxx>
#include <workspace/core/boat/hull.hxx>
#include <workspace/core/boat/manager.hxx>
#include <workspace/core/boat/nuisance.hxx>
#include <workspace/core/boat/track.hxx>
#include <workspace/core/entity/node.hxx>
#include <workspace/core/entity/property.hxx>
#include <workspace/core/game/types.hxx>
#include <workspace/core/entity/values.hxx>
#include <workspace/core/world/breakable.hxx>
#include <workspace/core/world/emitters.hxx>
#include <workspace/core/world/dynprop.hxx>
#include <workspace/core/world/logic.hxx>
#include <workspace/core/world/rules.hxx>
#include <workspace/core/world/slalom.hxx>
#include <workspace/core/world/tick.hxx>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <xmmintrin.h>

// The race game's start, as far as it is recreated. Retail's chain (read from
// the disassembly / Hex-Rays, world agent batch 7):
//
//   VuActionGameMode's load 0x4BA060: GameData = gameutil +80 ["GameData"]
//     (the dev config's "ActionGameData" when absent - compiled out), the race
//     project loaded, VuRaceGame ctor 0x489030 (VuGame ctor 0x480BA0), then
//   VuGame's boat load 0x481CE0: for each GameData["Boats"][k] a boat made by
//     0x484940 (the entity factory, by name), named "Boat%02d", its vt+0x2C
//     load 0x5009D0 on that entry (Properties: Boat / Skin / Driver Type ...,
//     Driver: Name), 0x500A60, pushed onto the game's +0x38 list; then the
//     project's gameInitialize (every VuAiWaypointEntity's slot 6: the track
//     manager's sectors) and "OnProjectInitialized"; then
//   VuGame's begin 0x4829A0: the track manager's slot 7 (the build) / 8 / 9 /
//     13, the game's slot 13 (VuRaceGame 0x48AF90: a HUMAN-driven boat -
//     driver slot 12 - takes the next VuGridPositionEntity's world (the boat
//     manager's +0x50 list, filled by the grid entities' gameInitialize
//     0x43A740 and sorted by 0x4AAA40); every other boat goes AiSpacing x k
//     metres down the track - race_track_start below - with +0x4AC = 1,
//     +0x4B0 = that distance / (top speed x 0.44704), driver +0x7C the
//     sector), each boat's driver +0x78 = track slot 19 (slot 14) and its
//     gameInitialize, then +0x4D4 = the lap count, +0x4D8, +0x4DC = its world.
//   VuRaceGame slot 5 0x4896A0 then hands every boat to the AI manager
//     (vt+0x2C addDriver, top speed lerp over the boat list from
//     Race.TopSpeed) and runs its slots 6 / 7 (fleet_start / fleet_run).
//
// THE GAP (UNKNOWN - nothing is stood in): GameData["Boats"] is written by the
// front end - VuFrontEndGameMode's GameConfig step 0x4BF9B0 -> 0x4C0A50 (race;
// 0x4C18D0 slalom, 0x4C1E30 time trial, 0x4C2C50 ghost), from GameConfig.Track
// and BoatConfig.PlayerBoats[0] (the player's chosen boat / skin, written by
// the boat-select screens' VuSetBoatConfigPlayerEntity / VuBoatConfigEntity)
// plus an AI roster picked by class (0x4560F0, 0x455700, 0x455820, 0x4C3380,
// 0x455BC0) and the player entry 0x4C3120 - none recreated. With no GameData
// there are no boats, so the AI fleet (fleet_add_driver / fleet_start /
// fleet_run), the per-frame race and the hull draw are not composed.

// The engine's logic entities (world/logic.hxx) as tree state: the handlers'
// logic_entity_t (words from +0x60, the VuString) and the node defaults the
// template mark copies. Each class's property set, read from its ctor (node
// name / class / target): VuAddIntegerConstantEntity 0x5DC910 "C" int
// (VuBasicProperty<int> 0x73A218) +0x60; VuIntegerEntity 0x5DD710 "Value" int
// +0x60; VuSetBooleanEntity 0x5E2F40 "Value" bool (0x737450) +0x60;
// VuSetIntegerEntity 0x5E32C0 "Value" int +0x60; VuSetStringEntity 0x5E3640 /
// VuStringEntity 0x5E5240 "Value" VuStringProperty (0x406B70, vt 0x7374B8:
// the string load 0x406C20) on the VuString +0x60; VuRandomTriggerEntity
// 0x5E10E0 "Weight 1" .. "Weight 8" float (0x737484) +0x60 .. +0x7C;
// VuSwitchEntity 0x5E21B0 "Initial State" VuStaticIntEnumProperty (0x74C354,
// table 0x754A0C: Undefined 0, Off 1, On 2) +0x60; VuCounterEntity 0x5E4C00
// "Initial Count" / "Target Count" int +0x60 / +0x64. The rest (the integer
// math, the compares, sequencer, trigger array, platform / build filters)
// declare no property. Every ctor default is 0 / "" (the ctors' stores).
struct logic_state_t
{
    logic_entity_t value;
    logic_entity_t defaults;
};

inline std::shared_ptr< void > logic_state_for( const std::string& cls )
{
    const std::uint32_t vt = logic_vtable_of( cls.c_str( ) );
    if ( vt == 0u )
        return nullptr;
    auto s = std::make_shared< logic_state_t >( );
    s->value = logic_construct( vt );
    s->defaults = s->value;
    return s;
}

inline logic_state_t* logic_state_of( entity_t& e )
{
    return e.m_state && logic_vtable_of( e.m_type.c_str( ) ) != 0u ? static_cast< logic_state_t* >( e.m_state.get( ) ) : nullptr;
}

inline void logic_load_properties( entity_t& e, const value_t& p )
{
    logic_state_t* s = logic_state_of( e );
    if ( !s )
        return;
    logic_entity_t& v = s->value;
    const logic_entity_t& d = s->defaults;
    const auto int_at = [ & ]( const char* name, int k ) {
        std::int32_t t = 0;
        property_int( p, name, t, static_cast< std::int32_t >( d.words[ k ] ) );
        v.words[ k ] = static_cast< std::uint32_t >( t );
    };
    const std::string& c = e.m_type;
    if ( c == "VuAddIntegerConstantEntity" )
        int_at( "C", 0 );
    else if ( c == "VuIntegerEntity" || c == "VuSetIntegerEntity" )
        int_at( "Value", 0 );
    else if ( c == "VuCounterEntity" )
    {
        int_at( "Initial Count", 0 );
        int_at( "Target Count", 1 );
    }
    else if ( c == "VuSetBooleanEntity" )
    {
        std::uint8_t b = 0;
        property_bool( p, "Value", b, static_cast< std::uint8_t >( d.words[ 0 ] & 0xFFu ) );
        v.words[ 0 ] = ( v.words[ 0 ] & 0xFFFFFF00u ) | b;
    }
    else if ( c == "VuSetStringEntity" || c == "VuStringEntity" )
        property_string( p, "Value", v.text, d.text );
    else if ( c == "VuIsSkuEntity" )   // ctor 0x42CD60: "SKU" VuStringProperty (0x406B70) on the VuString +0x60
        property_string( p, "SKU", v.text, d.text );
    else if ( c == "VuIsEventTypeEntity" )   // ctor 0x42C850: "Type" (vt 0x74C394: the string loads 0x406C20 / 0x406CD0 / 0x406AB0), "AttractEvent"
        property_string( p, "Type", v.text, d.text );
    else if ( c == "VuRandomTriggerEntity" )
    {
        static const char* names[ 8 ] = { "Weight 1", "Weight 2", "Weight 3", "Weight 4", "Weight 5", "Weight 6", "Weight 7", "Weight 8" };
        for ( int k = 0; k < 8; ++k )
        {
            float f, fd;
            std::memcpy( &fd, &d.words[ k ], 4 );
            property_float( p, names[ k ], f, fd, false );
            std::memcpy( &v.words[ k ], &f, 4 );
        }
    }
    else if ( c == "VuSwitchEntity" )
    {
        static const property_choice_t choices[ 3 ] = { { "Undefined", 0 }, { "Off", 1 }, { "On", 2 } };
        std::int32_t t = 0;
        property_enum( p, "Initial State", t, static_cast< std::int32_t >( d.words[ 0 ] ), choices, 3 );
        v.words[ 0 ] = static_cast< std::uint32_t >( t );
    }
}

inline void logic_mark_properties( entity_t& e )
{
    if ( logic_state_t* s = logic_state_of( e ) )
        s->defaults = s->value;
}

// The callbacks the race project's tree is loaded with: the verified entity
// loads (engine entity/node.hxx, hydro game/types.hxx) plus the AI waypoint's
// property set (boat track.hxx, ctor 0x431770) - the track manager reads it -
// and VuAiNuisanceDriverStartEntity's (boat nuisance.hxx, ctor 0x42FA20: Boat).
inline entity_world_t race_entity_world( const entity_registry_t& registry, std::function< const value_t*( const std::string& ) > templates )
{
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    world.m_template = std::move( templates );
    world.m_components_for = entity_components_scripted;
    world.m_component_load = entity_load_component;
    world.m_component_post = entity_post_load_component;
    world.m_component_mark = entity_mark_component;
    world.m_state_for = []( const std::string& cls ) {
        auto s = waypoint_state_for( cls );
        if ( !s )
            s = nuisance_start_state_for( cls );
        if ( !s )
            s = multi_delay_state_for( cls );   // world/rules.hxx (ctor 0x5DFD80)
        if ( !s )
            s = dynamic_prop_state_for( cls );   // world/dynprop.hxx (ctors 0x4367E0 / 0x434F80)
        if ( !s )
            s = breakable_state_for( cls );   // world/breakable.hxx (ctor 0x433F80)
        if ( !s )
            s = audio_emitter_state_for( cls );   // world/emitters.hxx (ctor 0x5F3660)
        if ( !s )
            s = logic_state_for( cls );   // world/logic.hxx's classes (above)
        if ( !s )
            s = slalom_gate_state_for( cls );   // world/slalom.hxx (ctor 0x43F390)
        if ( !s )
            s = explosion_entity_state_for( cls );   // world/dynprop.hxx (ctor 0x5E6150)
        return s ? s : entity_state_for( cls );
    };
    world.m_properties_load = []( entity_t& e, const value_t& p ) { waypoint_load_properties( e, p ); nuisance_start_load_properties( e, p ); multi_delay_load_properties( e, p ); dynamic_prop_load_properties( e, p ); breakable_load_properties( e, p ); audio_emitter_load_properties( e, p ); key_motion_load_properties( e, p ); control_entity_load_properties( e, p ); logic_load_properties( e, p ); slalom_gate_load_properties( e, p ); explosion_entity_load_properties( e, p ); entity_load_properties( e, p ); };
    world.m_properties_mark = []( entity_t& e ) { waypoint_mark_properties( e ); nuisance_start_mark_properties( e ); multi_delay_mark_properties( e ); dynamic_prop_mark_properties( e ); breakable_mark_properties( e ); audio_emitter_mark_properties( e ); key_motion_mark_properties( e ); control_entity_mark_properties( e ); logic_mark_properties( e ); slalom_gate_mark_properties( e ); explosion_entity_mark_properties( e ); entity_mark_properties( e ); };
    world.m_post_load = []( entity_t& e, const value_t& data, bool override ) { control_entity_post_load( e, data, override ); };
    return world;
}

// 0x5CCA10 on an entity: the first component of its +0x48 list whose type chain holds VuControlComponent (0x736E48) - only
// VuControlComponent's own vtable 0x757EA0 answers it (g_logic_types). Its records are the owner ctor's 0x5AD8C0 push_backs
// {name, value, value} of the properties' ctor values; for VuDirectionalWaveEntity (ctor 0x5D97F0, 0x5D9C5A .. 0x5D9D0E) five:
// "Max Height" +0x6C, "Speed" +0x70, "Period" +0x74, "Longitudinal Decay Ratio" +0x78, "Lateral Decay Ratio" +0x7C (their
// defaults: surface.hxx k_water_directional_props). Kept in that component's m_state, made on first use. Null: no such
// component; `unknown`: it has one but its owner class's records are not read here (UNKNOWN).
inline control_component_t* control_component_of( entity_t& e, bool& unknown )
{
    unknown = false;
    for ( entity_component_t& c : e.m_components )
    {
        if ( c.m_vtable != 0x757EA0u )
            continue;
        if ( !c.m_state )
        {
            if ( e.m_type != "VuDirectionalWaveEntity" )
            {
                unknown = true;
                return nullptr;
            }
            static const char* const names[ 5 ] = { "Max Height", "Speed", "Period", "Longitudinal Decay Ratio", "Lateral Decay Ratio" };
            auto comp = std::make_shared< control_component_t >( );
            for ( int k = 0; k < 5; ++k )
            {
                const std::uint32_t v = k_water_directional_props[ k ].fallback;
                comp->names.push_back( names[ k ] );
                comp->records.push_back( 0u );
                comp->records.push_back( v );
                comp->records.push_back( v );
            }
            c.m_state = comp;
        }
        return static_cast< control_component_t* >( c.m_state.get( ) );
    }
    return nullptr;
}

// The "Entity" ref's target of an entity's script component (null: unlinked).
inline entity_t* control_ref_target( entity_t& e )
{
    if ( const script_component_t* sc = script_of( e ) )
        for ( const script_ref_t& ref : sc->m_refs )
            if ( std::string( ref.m_row->m_name ) == "Entity" )
                return ref.m_target;
    return nullptr;
}

// 0x500A60's vt+0x10 (slot 4) after the project load, on every VuControlEntity of the tree (children first, as 0x500A60
// recurses before its own call): 0x5CD300 = control_post_load with its children (+0x38 in order: a keyframe is a
// VuControlKeyframeEntity), the ref's entity's VuControlComponent and 0x5012E0 (an ANCESTOR up the +0x34 chain has a +0x4C).
// Returns the count of controls whose target's component records are UNKNOWN (bound to none).
inline int control_entities_post_load( entity_t& root )
{
    int unknown = 0;
    const auto walk = [ & ]( const auto& self, entity_t& e ) -> void {
        for ( auto& c : e.m_children )
            self( self, *c );
        if ( e.m_type != "VuControlEntity" || !e.m_state )
            return;
        control_entity_t& v = static_cast< control_entity_state_t* >( e.m_state.get( ) )->value;
        v.children.clear( );
        for ( auto& c : e.m_children )
        {
            control_child_t ch;
            ch.keyframe = c->m_type == "VuControlKeyframeEntity";
            ch.name = c->m_name;
            v.children.push_back( ch );
        }
        bool templated = false;
        for ( const entity_t* a = e.m_parent; a && !templated; a = a->m_parent )
            templated = !a->m_template.empty( );
        control_component_t* comp = nullptr;
        if ( entity_t* t = control_ref_target( e ) )
        {
            bool u = false;
            comp = control_component_of( *t, u );
            unknown += u ? 1 : 0;
        }
        control_post_load( v, comp, templated );
    };
    walk( walk, root );
    return unknown;
}

// VuTransformComponent::setWorldTransform 0x5344A0 on an entity of the tree,
// whole: transform_set_world (world/transform.hxx: the world, its euler and
// 0x534830's local against the parent's transform world - the owner's +0x34,
// the entity's m_parent), then 0x534C00(notify) on the owner's children
// (game/types.hxx transform_walk_children - its children's watchers are hooks
// it does not fire), then, with notify, the component's watcher +0xC0 vt+8 =
// `watcher` (null: none installed). False, nothing written: the entity has no
// modelled transform, or its parent has none (its world is UNKNOWN).
inline bool entity_set_world( entity_t& owner, const float* matrix, bool notify, const std::function< void( ) >& watcher )
{
    transform_component_t* t = entity_transform( owner );
    if ( !t )
        return false;
    const transform_component_t* parent = nullptr;
    if ( owner.m_parent )
    {
        parent = entity_transform( *owner.m_parent );
        if ( !parent || owner.m_parent->m_world_unknown )
            return false;
    }
    transform_set_world( *t, matrix, parent ? parent->world : nullptr );
    owner.m_world_unknown = false;
    transform_walk_children( owner, t->world );
    if ( notify && watcher )
        watcher( );
    return true;
}

// ---- VuGame's boat load 0x481CE0 ------------------------------------------
//
// The VuBoatEntity as the entity tree sees it (create 0x41B690 = operator
// new(0x520) + 0x41B710 + ctor 0x41B850; VuEntity ctor 0x500710(0): +0x14 0,
// vtable 0x7383D4, no ctor-made children). Its three string properties (the
// +0x44 list in ctor order, every one VuAssetProperty-shaped: load 0x406C20,
// value read 0x406CD0) - "Driver Type" -> +0x74 (default "AI"), "Boat" ->
// +0x8C ("Default"), "Skin" -> +0xA4 ("", notify byte 1: its watcher, the skin
// pass 0x421490, fires when a string value differs from the reset target).
// The boat's components (transform / layout / draw / instigator) are the boat
// image's (boat construct.hxx), not modelled on this node.
constexpr entity_ctor_t k_race_boat_ctor{ "VuBoatEntity", 0x007383D4u, 0u };

struct race_boat_state_t
{
    std::string driver_type = "AI", boat = "Default", skin;   // +0x74 / +0x8C / +0xA4
    std::uint32_t hash = 0;                                   // +0x30, 0x500A60
};

inline const entity_ctor_t* race_ctor_for( const std::string& cls )
{
    return cls == "VuBoatEntity" ? &k_race_boat_ctor : entity_ctor_for( cls );
}

struct race_load_hooks_t
{
    // The boat's vt+8 (0x41CFF0, the postLoad: driver, sections, transform) on
    // the boat's GameData entry - its own unit.
    std::function< void( entity_t& boat, const value_t& data ) > post_load;
    // The Skin watcher 0x421490 (the skin pass - the rendering side).
    std::function< void( entity_t& boat ) > skin;
    // VuUIFont's reload 0x626CF0 (ui draw.hxx's ui_font_params) for the font
    // object at this game offset, after its name changed.
    std::function< void( int offset, const std::string& name ) > font_reload;
};

// ---- VuActionGameMode's load 0x4BA060: the project and the game object -----
//
// The GameData every reader below takes: gameutil +0x50 ["GameData"] when it is
// an object (type 6), else g_vudevconfig 0x75901C's getParam("ActionGameData")
// (slot 6 - in this build 0x4FDB70, which answers the static null container
// 0x7593A0: `dev` null = that), else (no dev config) the null container.
// 0x4D3260 / 0x4D31D0 / 0x4D32F0 read its Level / GameType / Course: a string
// (type 4), else the static empty 0x819950.
inline const value_t& race_action_game_data( const value_t& util_root, const value_t* dev )
{
    static const value_t null{};
    const value_t& gd = member( util_root, "GameData" );
    if ( gd.tag == 6 )
        return gd;
    return dev ? *dev : null;
}

inline std::string race_game_data_text( const value_t& util_root, const value_t* dev, const char* key )
{
    const value_t& v = member( race_action_game_data( util_root, dev ), key );
    return v.tag == 4 ? v.text : std::string( );
}

// 0x4D3480 (thiscall gameutil, VuString* out): out = Level (0x4D3260); when
// the asset factory's doesAssetExist("VuProjectAsset", out) (vt+0x28) is false:
// out = "Games/" + 0x564AE0(Level) (the part after the LAST '/' - 0x4ECA50 is
// an rfind; no '/' -> all of it; operator+ 0x4D3FF0) + GameType (0x4D31D0,
// operator+ 0x436700); then for GameType "Slalom" the Course (0x4D32F0) is
// appended (0x430420). An existing Level names the project as is, unappended.
// `exists` answers doesAssetExist (the asset factory's: the caller's).
inline std::string race_project_name( const value_t& util_root, const value_t* dev,
                                      const std::function< bool( const std::string& type, const std::string& name ) >& exists )
{
    std::string out = race_game_data_text( util_root, dev, "Level" );
    if ( exists( "VuProjectAsset", out ) )
        return out;
    const std::size_t slash = out.rfind( '/' );
    const std::string tail = slash == std::string::npos ? out : out.substr( slash + 1 );
    out = "Games/" + tail + race_game_data_text( util_root, dev, "GameType" );
    if ( race_game_data_text( util_root, dev, "GameType" ) == "Slalom" )
        out += race_game_data_text( util_root, dev, "Course" );
    return out;
}

// 0x4BA060's game object by GameType (0x4D31D0): exactly "Race" (0x404AC0
// over min(size, 4) bytes and size 4) -> VuRaceGame (new 0x748, 0x4BA430, the
// ctor 0x489030); else VuString_equals "Slalom" -> 0x770 / 0x48BEC0,
// "TimeTrial" -> 0x740 / 0x48ED30, "Ghost" -> 0x778 / 0x484A20; anything else
// -> none (+0x44 left as it was: 0x481CE0 and 0x4829A0 are skipped, the
// project's OnProjectInitialized / OnGameStart still run).
enum class race_game_kind_t { none, race, slalom, time_trial, ghost };

inline race_game_kind_t race_game_kind( const std::string& type )
{
    if ( type == "Race" )
        return race_game_kind_t::race;
    if ( type == "Slalom" )
        return race_game_kind_t::slalom;
    if ( type == "TimeTrial" )
        return race_game_kind_t::time_trial;
    if ( type == "Ghost" )
        return race_game_kind_t::ghost;
    return race_game_kind_t::none;
}

// The game fields 0x481CE0 writes, with the values the VuRaceGame ctor 0x489030
// leaves in them (race_game_construct).
struct race_game_t
{
    std::vector< std::unique_ptr< entity_t > > boats;   // +0x38 data, +0x3C count
    std::int32_t capacity = 0;                           // +0x40
    std::int32_t laps = 0;                               // +0x4C
    std::string difficulty;                              // +0x50
    std::string track;                                   // +0x68
    std::uint8_t championship = 0;                       // +0x80
    std::string fonts[ 14 ];                             // each font object's +8
};

// The VuRaceGame ctor 0x489030's values in race_game_t: all from its base, VuGame's ctor 0x480BA0 - the boat
// vector's storage aligned_malloc(0x20, 0x10) with +0x3C count 0 and +0x40 capacity 8, +0x4C laps -1, the
// +0x50 difficulty / +0x68 track strings empty, +0x80 championship 0, and each HUD font's ctor 0x626900 leaves
// its name (+8) empty. (0x489030's own part - +0x6F8 .. +0x744 zeroed, the PreGame / Game / PostGame FSM states
// and the three HUD projects HUDs/RacePreGameHUD / RaceGameHUD / RacePostGameHUD - holds nothing race_game_t
// models: the HUD is the UI's, the FSM is not recreated here.)
inline void race_game_construct( race_game_t& game )
{
    game = race_game_t{ };
    game.capacity = 8;
    game.laps = -1;
}

// The 14 HUD font objects (game offset, name) in 0x481CE0's call order.
inline const std::pair< int, const char* >* race_game_fonts( )
{
    static const std::pair< int, const char* > fonts[ 14 ] = {
        { 0x8C, "HUD_PlaceLabelFont" }, { 0xF0, "HUD_PlaceDataFont" }, { 0x154, "HUD_TimeLabelFont" },
        { 0x1B8, "HUD_TimeDataFont" }, { 0x21C, "HUD_BestLabelFont" }, { 0x280, "HUD_BestDataFont" },
        { 0x2E4, "HUD_LapLabelFont" }, { 0x348, "HUD_LapDataFont" }, { 0x3CC, "HUD_ChallengeNameFont" },
        { 0x430, "HUD_ChallengeTimeFont" }, { 0x4F8, "HUD_ChallengeTimeBehindFont" },
        { 0x494, "HUD_ChallengeTimeAheadFont" }, { 0x55C, "HUD_ChallengeResultTextFont" },
        { 0x5C0, "HUD_ChallengeResultTimeFont" } };
    return fonts;
}

// The boat's three property loads (0x406C20 each, list order): the target is
// reset to the default, a string value (tag 4) that differs is stored, and the
// Skin's notify fires its watcher then.
inline void race_boat_load_properties( entity_t& e, const value_t& properties, const race_load_hooks_t& hooks )
{
    race_boat_state_t& s = *static_cast< race_boat_state_t* >( e.m_state.get( ) );
    property_string( properties, "Driver Type", s.driver_type, "AI" );
    property_string( properties, "Boat", s.boat, "Default" );
    s.skin.clear( );
    const value_t& skin = member( properties, "Skin" );
    if ( skin.tag == 4 && skin.text != s.skin )
    {
        s.skin = skin.text;
        if ( hooks.skin )
            hooks.skin( e );
    }
}

// 0x500A60(seed) (thiscall; retn 4): +0x30 = FNV-1a of the name from `seed`;
// the hash then runs on over "/" (0x6CB0C4) and seeds every child's; each
// component's slot 5 (a hook) and the entity's vt+0x10 (VuBoatEntity: 0x401560,
// a nop) follow. Only a boat's hash has a place here.
inline std::uint32_t race_entity_hash( entity_t& e, std::uint32_t seed )
{
    std::uint32_t h = seed;
    for ( unsigned char c : e.m_name )
        h = ( h ^ c ) * 0x1000193u;
    if ( e.m_type == "VuBoatEntity" && e.m_state )
        static_cast< race_boat_state_t* >( e.m_state.get( ) )->hash = h;
    std::uint32_t path = ( h ^ static_cast< unsigned char >( '/' ) ) * 0x1000193u;
    for ( auto& child : e.m_children )
        race_entity_hash( *child, path );
    return h;
}

// 0x564AE0's file part: after the LAST '/', else the whole name.
inline std::string race_file_part( const std::string& name )
{
    const std::size_t slash = name.rfind( '/' );
    return slash == std::string::npos ? name : name.substr( slash + 1 );
}

// 0x481CE0 itself (this = the game, `game_data` = GameData). `util_data` is
// g_vugameutil's +0x50 container (0x4D3260 reads ["GameData"]["Level"] there -
// when that is not an object, g_vudevconfig's "ActionGameData", a hook: null
// here), `trackdb` TrackDB's document (g_vugameutil +0x10's +0x58). Per entry
// of GameData["Boats"] (0x4FBC40: an array's count, else 0): the registry's
// create for "VuBoatEntity" (0x484940 - [0x73628C]), the name "Boat%02d"
// (k + 1), vt+0x2C = VuEntity::load 0x5009D0 on the entry (out of range / not
// an array -> the null container), 0x500A60(0x811C9DC5), push_back (capacity
// max(n, c + c / 2) when full). Then "Difficulty" (tag 4) -> +0x50,
// "IsChampionship" (tag 3) -> +0x80, the track = the Level's file part ->
// +0x68, TrackDB["VuDBAsset"][track]["LapCount"] (1 int, 2 float truncated, 7
// the int64's low dword; anything else keeps +0x4C), each font object renamed
// and reloaded when its name differs (0x626B00), then the game's vt+0x10 on
// GameData (VuRaceGame slot 4 = 0x4021A0, ret 4: nothing). False when the
// registry cannot create the boat type (retail would push null - UNKNOWN).
// The entity world a VuBoatEntity is made and loaded in (its ctor facts and
// its three string properties on top of race_entity_world) - 0x481CE0's boats
// and VuAiNuisanceDriverStartEntity's child (0x42FDC0) alike.
inline entity_world_t race_boat_world( const entity_registry_t& registry, const race_load_hooks_t& hooks )
{
    entity_world_t world = race_entity_world( registry, nullptr );
    world.m_ctor_for = race_ctor_for;
    const auto state_for = world.m_state_for;
    world.m_state_for = [ state_for ]( const std::string& cls ) -> std::shared_ptr< void > {
        return cls == "VuBoatEntity" ? std::make_shared< race_boat_state_t >( ) : state_for( cls );
    };
    const auto properties_load = world.m_properties_load;
    world.m_properties_load = [ properties_load, &hooks ]( entity_t& e, const value_t& p ) {
        if ( e.m_type == "VuBoatEntity" )
            race_boat_load_properties( e, p, hooks );
        else
            properties_load( e, p );
    };
    return world;
}

inline bool race_load_boats( race_game_t& game, const value_t& game_data, const value_t& util_data, const value_t& trackdb,
                             const entity_registry_t& registry, const race_load_hooks_t& hooks )
{
    entity_world_t world = race_boat_world( registry, hooks );
    static const value_t null{};
    const value_t& boats = member( game_data, "Boats" );
    const int count = boats.tag == 5 ? static_cast< int >( boats.list.size( ) ) : 0;
    for ( int k = 0; k < count; ++k )
    {
        std::unique_ptr< entity_t > boat = entity_create( "VuBoatEntity", world );
        if ( !boat )
            return false;
        char name[ 16 ];
        std::snprintf( name, sizeof( name ), "Boat%02d", k + 1 );
        boat->m_name = name;
        const value_t& entry = k < static_cast< int >( boats.list.size( ) ) ? boats.list[ static_cast< std::size_t >( k ) ] : null;
        entity_load( *boat, &entry, world );
        if ( hooks.post_load )
            hooks.post_load( *boat, entry );
        race_entity_hash( *boat, 0x811C9DC5u );
        const std::int32_t n = static_cast< std::int32_t >( game.boats.size( ) ) + 1;
        if ( n > game.capacity )
        {
            std::int32_t grown = game.capacity + game.capacity / 2;
            if ( n > grown )
                grown = n;
            if ( grown > game.capacity )
                game.capacity = grown;
        }
        game.boats.push_back( std::move( boat ) );
    }
    const value_t& difficulty = member( game_data, "Difficulty" );
    if ( difficulty.tag == 4 )
        game.difficulty = difficulty.text;
    const value_t& championship = member( game_data, "IsChampionship" );
    if ( championship.tag == 3 )
        game.championship = static_cast< std::uint8_t >( championship.integer );
    const value_t& gd = member( util_data, "GameData" );
    const value_t& level = member( gd.tag == 6 ? gd : null, "Level" );
    game.track = race_file_part( level.tag == 4 ? level.text : std::string( ) );
    const value_t& laps = member( member( member( trackdb, "VuDBAsset" ), game.track.c_str( ) ), "LapCount" );
    if ( laps.tag == 1 )
        game.laps = laps.integer;
    else if ( laps.tag == 2 )
        game.laps = _mm_cvtt_ss2si( _mm_set_ss( laps.real ) );
    else if ( laps.tag == 7 )
        game.laps = static_cast< std::int32_t >( static_cast< std::uint64_t >( laps.int64 ) & 0xFFFFFFFFu );
    const auto* fonts = race_game_fonts( );
    for ( int k = 0; k < 14; ++k )
        if ( game.fonts[ k ] != fonts[ k ].second )
        {
            game.fonts[ k ] = fonts[ k ].second;
            if ( hooks.font_reload )
                hooks.font_reload( fonts[ k ].first, game.fonts[ k ] );
        }
    return true;
}

// ---- VuBoatEntity gameInitialize 0x41D350 / gameRelease 0x41D470 ---------
//
// Slot 6 (0x500B10's vt+0x18): two VuMethod1<VuBoatEntity, void, float>
// delegates (operator new(0x10): vtable 0x738348, the boat, the member, 0)
// registered through g_vutickmanagerimpl 0x759008's slot 8 - 0x4077ED (a
// thunk: vt+0x34, slot 13 = the Decision tick 0x41EFF0) in "Decision", then
// 0x404AB2 (vt+0x38, slot 14 = 0x41F330, the Build tick) in "Build". Then, in
// order, the draw component (+0x6C) handed to g_vu3ddrawmanager 0x7590B8's
// slot 10 when its +0x1E is 0 (+0x1E = 1), the boat's slot 17 (0x421980),
// the driver's (+0x220) slot 6, 0x4191F0 on the hull (+0x228), the engine
// (+0x22C) event start - 0x50FBF0 on its +0xC4 with the name +0x4C, true ->
// FMOD::Event::start(+0xC8) - 0x410610 on +0x230, 0x419FF0 on +0x234, the
// boat manager g 0x758FB8's 0x4AAF60(boat), 0x531F50 on the instigator +0x70.
// Slot 7 (gameRelease): the instigator manager g 0x759064's 0x5362A0 when the
// instigator's +0x28 is set, then +0x28 = 0; 0x4AB040(boat) on the boat
// manager; slot 10 unregister(boat) (every phase); the draw manager's slot 11
// when +0x1E (then 0); the boat's slot 18 (0x421940); the driver's slot 7;
// 0x419260 hull, 0x415330 engine, 0x4108C0 +0x230, a tail jmp 0x411950 on
// +0x238. Every callee but the tick manager's is a HOOK here (`call` gets
// its name and object, as the case names them); `engine_ready` is 0x50FBF0
// (the audio layer, a hook).
struct race_boat_init_hooks_t
{
    std::function< void( const std::string& row ) > call;
    std::function< bool( ) > engine_ready;
    // The boat manager g 0x758FB8 and this boat's entry (its 0x520 image) and
    // driver kind: when set, 0x4AAF60 / 0x4AB040 run as boat manager.hxx's
    // boat_manager_add / _remove (the row is still recorded - the case
    // intercepts the call to see it).
    boat_manager_t* manager = nullptr;
    const void* entry = nullptr;
    boat_driver_kind_t kind{};
    // The boat's slot 17 0x421980 (boat_enable on the race's dynamics world - world/session.hxx race_session_enable);
    // null: the row only.
    std::function< void( ) > slot17;
    // VuInstigatorComponent's gameInitialize 0x531F50 on +0x70 (trigger/volume.hxx, the session's trigger manager); null: the row only.
    std::function< void( ) > instigator;
    // 0x419FF0 on the pfx controller +0x234 (boat effects.hxx boat_pfx_create); null: the row only.
    std::function< void( ) > pfx_create;
    // 0x410610 on the animation +0x230 (world/rig.hxx boat_rig_create); null: the row only.
    std::function< void( ) > anim_create;
};

inline void race_boat_game_initialize( const void* boat, tick_manager_t& tm, const tick_handler_t& decision, const tick_handler_t& build,
                                       std::uint8_t& draw_added, const race_boat_init_hooks_t& hooks )
{
    const auto call = [ & ]( const std::string& row ) { if ( hooks.call ) hooks.call( row ); };
    // The delegates' words as retail stores them (the owner stands for the boat).
    if ( tick_register( tm, decision, "Decision" ) )
        call( "tick register tick 0x738348 boat 0x4077ED 0 Decision" );
    if ( tick_register( tm, build, "Build" ) )
        call( "tick register tick 0x738348 boat 0x404AB2 0 Build" );
    ( void )boat;
    if ( !draw_added )
    {
        call( "draw add drawm draw" );
        draw_added = 1;
    }
    call( "slot17 boat" );
    if ( hooks.slot17 )
        hooks.slot17( );
    call( "driver slot6 driver" );
    call( "0x4191F0 hull" );
    call( "0x50FBF0 engine+0xC4 Engine/Ev" );
    if ( hooks.engine_ready && hooks.engine_ready( ) )
        call( "FMOD start engine+0xC4 0xE7E7" );
    call( "0x410610 anim" );
    if ( hooks.anim_create )
        hooks.anim_create( );
    call( "0x419FF0 part234" );
    if ( hooks.pfx_create )
        hooks.pfx_create( );
    call( "0x4AAF60 boatm boat" );
    if ( hooks.manager )
        boat_manager_add( *hooks.manager, hooks.entry, hooks.kind );
    call( "0x531F50 inst" );
    if ( hooks.instigator )
        hooks.instigator( );
}

inline void race_boat_game_release( const void* boat, tick_manager_t& tm, std::uint8_t& draw_added, std::uint8_t& instigator_registered,
                                    const race_boat_init_hooks_t& hooks )
{
    const auto call = [ & ]( const std::string& row ) { if ( hooks.call ) hooks.call( row ); };
    if ( instigator_registered )
        call( "0x5362A0 instm inst" );
    instigator_registered = 0;
    call( "0x4AB040 boatm boat" );
    if ( hooks.manager )
        boat_manager_remove( *hooks.manager, hooks.entry );
    tick_unregister_all( tm, boat );
    call( "tick unregister tick boat" );
    if ( draw_added )
    {
        call( "draw remove drawm draw" );
        draw_added = 0;
    }
    call( "slot18 boat" );
    call( "driver slot7 driver" );
    call( "0x419260 hull" );
    call( "0x415330 engine" );
    call( "0x4108C0 anim" );
    call( "0x411950 damage" );
}

// ---- VuBoatEntity postLoad 0x41CFF0 -----------------------------------------
//
// The boat's vt+8 (VuEntity::load's trailing call; thiscall boat, the boat's
// data container; retn 4). The Driver Type text (+0x74) is compared EXACTLY
// (0x404990 / the 5-byte "Human" compare: same length, same bytes) with, in
// order: "Human" -> operator new(0xD0), 0x41D2E0, +0x84 = 0, the Human ctor
// 0x40ED50(boat); "AI" -> new(0xD4), 0x41D2E0, +0x8C = +0xBC = 0, VuAiDriver's
// ctor 0x40B690(boat); "UI" -> the same then vtable 0x737CDC (VuAiUiDriver);
// "Nuisance" -> new(0x104), 0x41D280, the ctor 0x40D880(boat); "Ghost" ->
// new(0x84), 0x41D2E0, +0x80 = 0, the ctor 0x40EB50(boat). The block goes to
// +0x220 (operator new's null too). ANY OTHER TEXT LEAVES +0x220 AS IT IS
// (the oracle shows the previous driver kept and loaded). Then, with +0x220
// set, the driver's vt+8 load on data["Driver"] (0x4FBDF0), then 0x420A70
// (boat_load_sections, boat load.hxx) and 0x421670 (boat_transform_changed).
// The four ctors are boat driver.hxx's (human_driver_construct -
// ConstantDB's - ui / nuisance / ghost_driver_construct, ai_driver_construct);
// the loads are VuAiDriver's 0x40B8A0 (AI / UI / Nuisance: ai_driver_load) and
// VuDriverEntity's 0x40EAC0 (Human / Ghost: driver_entity_load). UNKNOWN:
// operator new's failure (never under the oracle), and a kept driver of a class
// not modelled here (unknown_load).
enum class race_driver_class_t { ai, ui, human, nuisance, ghost };

struct race_boat_driver_t
{
    race_driver_class_t cls = race_driver_class_t::ai;
    std::uint32_t size = 0;                 // operator new's request
    bool known = false;                     // false: an object of no modelled class (UNKNOWN)
    std::vector< image_object_t > graph;    // the ctor's objects (boat driver.hxx)
    std::vector< std::pair< std::uint32_t, int > > methods;   // the human's method registrations
    boat_strings_t strings;                 // +0x60, the Name
};

struct race_post_load_hooks_t
{
    // vt+8 on a kept driver of no modelled class, with data["Driver"].
    std::function< void( race_boat_driver_t& driver, const value_t& container ) > unknown_load;
    // 0x420A70 and 0x421670, the composed units (the running game passes
    // boat_load_sections / boat_transform_changed on the boat's objects).
    std::function< void( ) > load_sections;
    std::function< void( ) > transform_changed;
};

// `boat` the boat's 0x520 image (the AI loads read its +0x270 / +0x280 aabb,
// still the ctor's zeros on a fresh boat); `constant_db` ConstantDB's root
// (the human ctor reads it). True when the driver at +0x220 is a modelled
// object; false when it is null or UNKNOWN - every step still runs, in
// retail's order.
inline bool race_boat_post_load( std::uint8_t* boat, const std::string& driver_type, const value_t& data, const value_t& constant_db,
                                 std::unique_ptr< race_boat_driver_t >& driver, const race_post_load_hooks_t& hooks )
{
    auto make = [ & ]( race_driver_class_t cls, std::uint32_t size )
    {
        driver = std::make_unique< race_boat_driver_t >( );
        driver->cls = cls;
        driver->size = size;
        driver->known = true;
        switch ( cls )
        {
        case race_driver_class_t::human:
        {
            human_driver_graph_t h = human_driver_construct( constant_db );   // 0x40ED50
            driver->graph = std::move( h.objects );
            driver->methods = std::move( h.methods );
            break;
        }
        case race_driver_class_t::ai: driver->graph = ai_driver_construct( ); break;              // 0x40B690
        case race_driver_class_t::ui: driver->graph = ui_driver_construct( ); break;              // 0x40B690 + 0x737CDC
        case race_driver_class_t::nuisance: driver->graph = nuisance_driver_construct( ); break;  // 0x41D280 + 0x40D880
        case race_driver_class_t::ghost: driver->graph = ghost_driver_construct( ); break;        // 0x40EB50
        }
    };
    if ( driver_type == "Human" )
        make( race_driver_class_t::human, 0xD0 );
    else if ( driver_type == "AI" )
        make( race_driver_class_t::ai, 0xD4 );
    else if ( driver_type == "UI" )
        make( race_driver_class_t::ui, 0xD4 );
    else if ( driver_type == "Nuisance" )
        make( race_driver_class_t::nuisance, 0x104 );
    else if ( driver_type == "Ghost" )
        make( race_driver_class_t::ghost, 0x84 );
    bool known = driver != nullptr;
    if ( driver )
    {
        const value_t& container = member( data, "Driver" );
        if ( !driver->known )
        {
            known = false;
            if ( hooks.unknown_load )
                hooks.unknown_load( *driver, container );
        }
        else if ( driver->cls == race_driver_class_t::human || driver->cls == race_driver_class_t::ghost )
            driver_entity_load( driver->strings, container );   // 0x40EAC0
        else
            ai_driver_load( driver->graph[ 0 ].image.data( ), driver->strings, container, boat );   // 0x40B8A0
    }
    if ( hooks.load_sections )
        hooks.load_sections( );
    if ( hooks.transform_changed )
        hooks.transform_changed( );
    return known;
}
// The project asset's AssetData "File" path (0x55FBE0), or empty.
inline std::string race_project_file( c_archive& archive, const char* asset )
{
    std::vector< std::uint8_t > bytes;
    const std::string prefix = "VuProjectAsset/";
    if ( std::strncmp( asset, prefix.c_str( ), prefix.size( ) ) != 0 || !archive.read( "Assets/AssetData", bytes ) )
        return {};
    const value_t database = c_reader( bytes ).parse( );
    const value_t& file = member( member( member( database, "VuProjectAsset" ), asset + prefix.size( ) ), "File" );
    return file.tag == 4 ? file.text : std::string( );
}

// VuProject::load 0x556390 on the race project: the registry as start-up
// leaves it and the tree through race_entity_world. Null when not loadable.
inline std::unique_ptr< entity_t > race_load_tree( c_archive& archive, const char* asset, const value_t& project,
                                                   std::function< const value_t*( const std::string& ) > templates )
{
    const std::string path = race_project_file( archive, asset );
    if ( path.empty( ) )
        return nullptr;
    const entity_registry_t registry = build_entity_registry( archive.names( "VuTemplateAsset/" ) );
    entity_world_t world = race_entity_world( registry, std::move( templates ) );
    std::unique_ptr< entity_t > tree = entity_load_project( project, path, world );
    // 0x500A60 follows the load: its slot 4 step for VuControlEntity (the rest of that walk - the +0x30 hash, the
    // components' vt+0x14, every other class's slot 4 - is not run here).
    if ( tree )
        if ( const int unknown = control_entities_post_load( *tree ) )
            LOG_WARNING( "race tree: %d VuControlEntity target(s) with a VuControlComponent whose owner's records are not read - "
                         "bound to none (UNKNOWN)", unknown );
    return tree;
}

// 0x4D1E10 (__usercall: ecx the 64-byte matrix out, xmm0 the distance; the
// sector out on the stack, may be null; ret), VuRaceGame 0x48AF90's start
// position for a boat not driven by a human: from the track's start sector
// (slot 11) along next[0] to the end sector (slot 12) inclusive, sectors whose
// waypoint Allows Recovery (+0x8B) use up their length (+0xB0) until one is
// longer than what is left (ordered compare, `ja`); there f = left / length,
// the point lerps +0x80 -> +0x90 (a x (1 - f) + b x f per lane) and the matrix
// is the identity with row 1 = the direction's xy over VuMath_sqrt(y y + x x)
// (z = that inverse x 0), row 0 = (fy - fz 0, fz 0 - fx, fx 0 - fy 0), row 2
// (0, 0, 1), row 3 the point. False (nothing written) when no sector is long
// enough or the track has no start.
inline bool race_track_start( const track_manager_t& tm, float distance, float* out, int* sector )
{
    const int end = track_end_sector( tm );
    int s = track_start_sector( tm );
    float left = distance;
    while ( s >= 0 )
    {
        const track_sector_t& t = tm.sectors[ static_cast< std::size_t >( s ) ];
        if ( tm.waypoints[ static_cast< std::size_t >( t.waypoint ) ].p.allow_recovery )
        {
            const float length = state_f( t.image, 0xB0 );
            if ( _mm_comigt_ss( _mm_set_ss( length ), _mm_set_ss( left ) ) )
            {
                const float f = left / length;
                const float g = 1.0f - f;
                const float x = state_f( t.image, 0x80 ) * g + state_f( t.image, 0x90 ) * f;
                const float y = state_f( t.image, 0x84 ) * g + state_f( t.image, 0x94 ) * f;
                const float z = state_f( t.image, 0x88 ) * g + state_f( t.image, 0x98 ) * f;
                const float dx = state_f( t.image, 0xA0 ), dy = state_f( t.image, 0xA4 );
                const float inv = 1.0f / hull_sqrt( dy * dy + dx * dx );
                const float zero = 0.0f;
                const float fx = inv * dx, fy = inv * dy, fz = inv * zero;
                const float fz0 = fz * zero;
                for ( int k = 0; k < 16; ++k )
                    out[ k ] = k % 5 == 0 ? 1.0f : 0.0f;   // VuMatrix_identity
                out[ 0 ] = fy - fz0;
                out[ 1 ] = fz0 - fx;
                out[ 2 ] = fx * zero - fy * zero;
                out[ 4 ] = fx;
                out[ 5 ] = fy;
                out[ 6 ] = fz;
                out[ 12 ] = x;
                out[ 13 ] = y;
                out[ 8 ] = 0.0f;
                out[ 9 ] = 0.0f;
                out[ 10 ] = 1.0f;
                out[ 14 ] = z;
                if ( sector )
                    *sector = s;
                return true;
            }
            left = left - length;
        }
        if ( s == end )
            break;
        // Retail reads the next list's first word whatever its count; an empty
        // list before the end sector (not on a built track) is UNKNOWN - stop.
        s = t.next.empty( ) ? -1 : t.next[ 0 ];
    }
    return false;
}
