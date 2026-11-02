#pragma once

#include <workspace/core/world/build.hxx>
#include <workspace/core/world/drive.hxx>
#include <workspace/core/world/livery.hxx>
#include <workspace/core/world/race.hxx>
#include <workspace/core/world/rules.hxx>
#include <workspace/core/world/start.hxx>
#include <workspace/core/world/tick.hxx>
#include <workspace/utility/checks/game.hxx>

// The world area's oracle-backed checks (one file per parallel agent; included by check.hxx after every
// shared check, so anything above is usable here). verify_world_all runs them all.

// transform_draw_matrix (hydro world/transform.hxx) against the draw matrix
// retail's prop owners leave in their model-draw component (oracle case
// prop_draw_matrix): every level / template prop of each class that carries a
// Vu3dDrawStaticModelComponent or Vu3dDrawAnimatedModelComponent, loaded alone,
// plus a hand-built nested set whose parents move the children after their own
// postLoad (the transform watcher's path). Inputs are retail's transform world
// and scale; all 16 words compared, one failure per prop.
inline bool verify_prop_draw_matrix( )
{
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_prop_matrices_count; ++k )
    {
        const golden_prop_matrix_t& want = g_golden_prop_matrices[ k ];
        transform_component_t c;
        transform_construct( c );
        std::memcpy( c.world, want.m_world, sizeof( c.world ) );
        std::memcpy( c.scale, want.m_scale, 3 * sizeof( float ) );
        float ours[ 16 ];
        transform_draw_matrix( c, ours );
        if ( std::memcmp( ours, want.m_matrix, sizeof( ours ) ) != 0 && ++failed <= 8 )
        {
            int w = 0;
            while ( w < 15 && std::memcmp( &ours[ w ], &want.m_matrix[ w ], 4 ) == 0 )
                ++w;
            std::uint32_t got;
            std::memcpy( &got, &ours[ w ], 4 );
            LOG_ERROR( "prop draw matrix: '%s' word %d is 0x%08X, retail 0x%08X", want.m_path, w, got, want.m_matrix[ w ] );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "transform_draw_matrix matches the prop owners' draw matrix (0x546AE0 / 0x545FF0 / 0x5ED420: fromScale x world) "
                     "on %d props (%d hand-built nested), 16 words each, bit for bit",
            static_cast< int >( g_golden_prop_matrices_count ), static_cast< int >( g_golden_prop_matrices_synthetic ) );
    else
        LOG_ERROR( "transform_draw_matrix FAILED on %d props", failed );
    return failed == 0;
}

// A golden entity-targets document (the hand-built project of real entities an
// entity_targets_case loaded through retail's VuEntity::load) loaded through
// hydro's tree: VuProject::load (entity_load_project) with the verified
// callbacks, as world.hxx's load_entity_tree runs it. Each child by name.
inline std::unique_ptr< entity_t > world_check_tree( const unsigned* words, std::size_t bytes, std::map< std::string, entity_t* >& by_name )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( words );
    static std::vector< value_t > documents;
    documents.push_back( c_reader( std::vector< std::uint8_t >( raw, raw + bytes ) ).parse( ) );
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    world.m_template = []( const std::string& ) -> const value_t* { return nullptr; };
    world.m_components_for = entity_components_scripted;
    world.m_component_load = entity_load_component;
    world.m_component_post = entity_post_load_component;
    world.m_component_mark = entity_mark_component;
    world.m_state_for = entity_state_for;
    world.m_properties_load = entity_load_properties;
    world.m_properties_mark = entity_mark_properties;
    std::unique_ptr< entity_t > tree = entity_load_project( member( documents.back( ), "VuProject" ), "Data/Projects/Check.vuprj", world );
    if ( tree )
        for ( auto& child : tree->m_children )
            by_name[ child->m_name ] = child.get( );
    return tree;
}

// Compare one child's modelled words against the golden row; logs and counts.
inline void world_check_words( const char* what, const golden_entity_targets_t& want, const std::vector< std::uint32_t >& ours, int& failed,
    int& compared )
{
    if ( static_cast< int >( ours.size( ) ) != want.m_count )
    {
        LOG_ERROR( "%s: '%s' (%s) has %d targets, retail %d", what, want.m_source, want.m_child, static_cast< int >( ours.size( ) ), want.m_count );
        ++failed;
        return;
    }
    for ( int k = 0; k < want.m_count; ++k )
    {
        ++compared;
        if ( ours[ k ] != want.m_values[ k ] && ++failed <= 8 )
            LOG_ERROR( "%s: '%s' target %d is 0x%08X, retail 0x%08X", what, want.m_source, k, ours[ k ], want.m_values[ k ] );
    }
}

// VuAmbientLightEntity / VuDirectionalLightEntity's property loads (engine
// entity/rig.hxx through the tree's entity_load_properties) against retail's
// VuEntity::load on every such entity in the level templates plus hand-built
// ones (oracle case load_light_rig), bit for bit.
inline bool verify_light_rig( )
{
    std::map< std::string, entity_t* > by_name;
    const std::unique_ptr< entity_t > tree = world_check_tree( g_golden_light_rig_document, g_golden_light_rig_document_bytes, by_name );
    int failed = 0;
    int compared = 0;
    for ( std::size_t k = 0; k < g_golden_light_rig_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_light_rig[ k ];
        const auto found = by_name.find( want.m_child );
        if ( found == by_name.end( ) || !found->second->m_state || found->second->m_type != want.m_type )
        {
            LOG_ERROR( "light rig: '%s' (%s) missing or without state", want.m_source, want.m_child );
            ++failed;
            continue;
        }
        std::vector< std::uint32_t > ours;
        if ( found->second->m_type == "VuAmbientLightEntity" )
            ours = { static_cast< const ambient_light_state_t* >( found->second->m_state.get( ) )->value.color };
        else
        {
            const directional_light_t& l = static_cast< const directional_light_state_t* >( found->second->m_state.get( ) )->value;
            ours = { l.front_color, l.back_color, l.specular_color };
        }
        world_check_words( "light rig", want, ours, failed, compared );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ambient_light_load / directional_light_load match VuAmbientLightEntity / VuDirectionalLightEntity's loads (ctors "
                     "0x5EF580 / 0x5EEA90) on %d lights: %d values, bit for bit", static_cast< int >( g_golden_light_rig_count ), compared );
    else
        LOG_ERROR( "the light rig's property loads FAILED %d checks", failed );
    return failed == 0;
}

// VuGlobalGfxSettingsEntity's 26 property loads (engine entity/gfx.hxx through
// the tree's entity_load_properties) against retail's VuEntity::load on every
// such entity in the level templates and projects plus two hand-built ones
// (oracle case load_gfx_settings), bit for bit.
inline bool verify_gfx_settings( )
{
    std::map< std::string, entity_t* > by_name;
    const std::unique_ptr< entity_t > tree = world_check_tree( g_golden_gfx_settings_document, g_golden_gfx_settings_document_bytes, by_name );
    int failed = 0;
    int compared = 0;
    for ( std::size_t k = 0; k < g_golden_gfx_settings_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_gfx_settings[ k ];
        const auto found = by_name.find( want.m_child );
        if ( found == by_name.end( ) || !found->second->m_state || found->second->m_type != want.m_type )
        {
            LOG_ERROR( "gfx settings: '%s' (%s) missing or without state", want.m_source, want.m_child );
            ++failed;
            continue;
        }
        std::vector< std::uint32_t > ours( 26 );
        gfx_settings_words( static_cast< const gfx_settings_state_t* >( found->second->m_state.get( ) )->value, ours.data( ) );
        world_check_words( "gfx settings", want, ours, failed, compared );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "gfx_settings_load matches VuGlobalGfxSettingsEntity's 26 loads (base ctor 0x5A6520) on %d entities: %d values, bit for bit",
            static_cast< int >( g_golden_gfx_settings_count ), compared );
    else
        LOG_ERROR( "the gfx settings' property loads FAILED %d checks", failed );
    return failed == 0;
}

// The gfx-settings zones' blend views (world/blend.hxx gfx_zone_view on hydro's game/types.hxx zone state: the
// base set + the VuPercentageProperty inner radii) against retail's VuEntity::load on every
// VuRectangularGfxSettingsEntity / VuEllipticalGfxSettingsEntity of the level templates and projects plus four
// hand-built ones (oracle case load_gfx_zones): +0x60 Always Active, the times, the +0x70 record, the +0xC8 timer,
// the inner radii - every word the blend 0x52F690 and the weights read - bit for bit.
inline bool verify_gfx_zones( )
{
    std::map< std::string, entity_t* > by_name;
    const std::unique_ptr< entity_t > tree = world_check_tree( g_golden_gfx_zones_document, g_golden_gfx_zones_document_bytes, by_name );
    int failed = 0;
    int compared = 0;
    for ( std::size_t k = 0; k < g_golden_gfx_zones_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_gfx_zones[ k ];
        const auto found = by_name.find( want.m_child );
        if ( found == by_name.end( ) || !found->second->m_state || found->second->m_type != want.m_type )
        {
            LOG_ERROR( "gfx zones: '%s' (%s) missing or without state", want.m_source, want.m_child );
            ++failed;
            continue;
        }
        const bool rect = found->second->m_type == "VuRectangularGfxSettingsEntity";
        const gfx_zone_state_t& st = *static_cast< const gfx_zone_state_t* >( found->second->m_state.get( ) );
        gfx_zone_t z;
        gfx_zone_view( z, st.value, rect ? gfx_zone_kind_t::rectangular : gfx_zone_kind_t::elliptical, st.inner, nullptr );
        std::vector< std::uint32_t > ours = { z.always_active, gfx_w( z.ramp_up ), gfx_w( z.duration ), gfx_w( z.ramp_down ) };
        ours.insert( ours.end( ), z.record, z.record + 22 );
        ours.push_back( gfx_w( z.timer ) );
        ours.push_back( gfx_w( z.inner[ 0 ] ) );
        if ( rect )
            ours.push_back( gfx_w( z.inner[ 1 ] ) );
        world_check_words( "gfx zones", want, ours, failed, compared );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "The gfx-settings zones' blend views match retail's loads (ctors 0x5D1FB0 / 0x5D2580 on the base 0x5A6520) on %d "
                     "entities: %d values, bit for bit",
                     static_cast< int >( g_golden_gfx_zones_count ), compared );
    else
        LOG_ERROR( "the gfx zones' loads FAILED %d checks", failed );
    return failed == 0;
}

// VuTriggerEntity's two property loads (engine entity/trigger.hxx through the
// tree's entity_load_properties) against retail's VuEntity::load on the
// VuTriggerPlaneEntity / VuTriggerBoxEntity of the templates plus five
// hand-built ones (oracle case load_triggers, start-up's 0x4D9260 run first so
// the Trigger Type default is the enum's first value), bit for bit.
inline bool verify_triggers( )
{
    std::map< std::string, entity_t* > by_name;
    const std::unique_ptr< entity_t > tree = world_check_tree( g_golden_triggers_document, g_golden_triggers_document_bytes, by_name );
    int failed = 0;
    int compared = 0;
    for ( std::size_t k = 0; k < g_golden_triggers_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_triggers[ k ];
        const auto found = by_name.find( want.m_child );
        if ( found == by_name.end( ) || !found->second->m_state || found->second->m_type != want.m_type )
        {
            LOG_ERROR( "triggers: '%s' (%s) missing or without state", want.m_source, want.m_child );
            ++failed;
            continue;
        }
        const trigger_properties_t& t = static_cast< const trigger_state_t* >( found->second->m_state.get( ) )->value;
        world_check_words( "triggers", want, { t.initially_active, trigger_type_mask( t.trigger_type ) }, failed, compared );
        ++compared;
        if ( t.trigger_type != want.m_texts[ 0 ] && ++failed <= 8 )
            LOG_ERROR( "triggers: '%s' Trigger Type is '%s', retail '%s'", want.m_source, t.trigger_type.c_str( ), want.m_texts[ 0 ] );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "trigger_load matches VuTriggerEntity's loads (ctor 0x61C6A0) on %d trigger planes / boxes: %d values, bit for bit",
            static_cast< int >( g_golden_triggers_count ), compared );
    else
        LOG_ERROR( "the triggers' property loads FAILED %d checks", failed );
    return failed == 0;
}

// VuStaticPfxEntity's four property loads (engine entity/pfx.hxx through the
// tree's entity_load_properties) and its onGameInitialize 0x5EB060 hand-off to
// the particle system (oracle case load_static_pfx: the template entities plus
// three hand-built): the targets bit for bit, then what hydro hands c_pfx -
// setScale = Pfx Scale, setColor = static_pfx_color, setMatrix = the transform
// world (scale kept out), start iff Initially Active.
inline bool verify_static_pfx( )
{
    std::map< std::string, entity_t* > by_name;
    const std::unique_ptr< entity_t > tree = world_check_tree( g_golden_static_pfx_document, g_golden_static_pfx_document_bytes, by_name );
    int failed = 0;
    int compared = 0;
    const auto word = []( float f )
    {
        std::uint32_t b;
        std::memcpy( &b, &f, sizeof( b ) );
        return b;
    };
    for ( std::size_t k = 0; k < g_golden_static_pfx_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_static_pfx[ k ];
        const golden_pfx_handoff_t& hand = g_golden_static_pfx_handoff[ k ];
        const auto found = by_name.find( want.m_child );
        const transform_component_t* t = found != by_name.end( ) ? entity_transform( *found->second ) : nullptr;
        if ( found == by_name.end( ) || !found->second->m_state || found->second->m_type != want.m_type || !t )
        {
            LOG_ERROR( "static pfx: '%s' (%s) missing, without state or without a transform", want.m_source, want.m_child );
            ++failed;
            continue;
        }
        const static_pfx_t& p = static_cast< const static_pfx_state_t* >( found->second->m_state.get( ) )->value;
        world_check_words( "static pfx", want, { p.initially_active, word( p.pfx_scale ), p.pfx_color }, failed, compared );
        ++compared;
        if ( p.effect_name != want.m_texts[ 0 ] && ++failed <= 8 )
            LOG_ERROR( "static pfx: '%s' Effect Name is '%s', retail '%s'", want.m_source, p.effect_name.c_str( ), want.m_texts[ 0 ] );
        float color[ 4 ];
        static_pfx_color( p, color );
        std::vector< std::uint32_t > ours = { static_cast< std::uint32_t >( p.initially_active != 0 ), word( p.pfx_scale ) };
        std::vector< std::uint32_t > theirs = { static_cast< std::uint32_t >( hand.m_started ), hand.m_scale };
        for ( int c = 0; c < 4; ++c )
        {
            ours.push_back( word( color[ c ] ) );
            theirs.push_back( hand.m_color[ c ] );
        }
        for ( int w = 0; w < 16; ++w )
        {
            ours.push_back( word( t->world[ w ] ) );
            theirs.push_back( hand.m_matrix[ w ] );
        }
        for ( std::size_t w = 0; w < ours.size( ); ++w )
        {
            ++compared;
            if ( ours[ w ] != theirs[ w ] && ++failed <= 8 )
                LOG_ERROR( "static pfx: '%s' hand-off word %d is 0x%08X, retail 0x%08X", want.m_source, static_cast< int >( w ), ours[ w ], theirs[ w ] );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "static_pfx_load and the hand-off to the particle system match VuStaticPfxEntity (ctor 0x5EAA40, onGameInitialize "
                     "0x5EB060) on %d entities: %d values, bit for bit", static_cast< int >( g_golden_static_pfx_count ), compared );
    else
        LOG_ERROR( "the static pfx loads / hand-off FAILED %d checks", failed );
    return failed == 0;
}

// An action entity's loaded targets as the oracle dumps them (load_actions):
// non-text targets as words in node order (a VuVector3 as three), texts apart.
inline bool world_action_words( const entity_t& e, std::vector< std::uint32_t >& w, std::vector< std::string >& texts )
{
    const auto f = []( float v )
    {
        std::uint32_t b;
        std::memcpy( &b, &v, sizeof( b ) );
        return b;
    };
    const auto vec = [ & ]( const float* v )
    {
        for ( int k = 0; k < 3; ++k )
            w.push_back( f( v[ k ] ) );
    };
    if ( const auto* p = entity_action< delay_properties_t >( e ); p && e.m_type == "VuDelayEntity" )
        w = { f( p->delay ), f( p->random_window ) };
    else if ( const auto* p = entity_action< boolean_properties_t >( e ); p && e.m_type == "VuBooleanEntity" )
        w = { p->value };
    else if ( const auto* p = entity_action< generic_event_properties_t >( e ); p && e.m_type == "VuGenericEventEntity" )
        texts = { p->event_name };
    else if ( const auto* p = entity_action< rotate_motion_properties_t >( e ); p && e.m_type == "VuRotateMotionEntity" )
    {
        w = { p->initially_active, p->one_shot };
        vec( p->axis );
        w.push_back( f( p->speed ) );
        w.push_back( p->local );
    }
    else if ( const auto* p = entity_action< attach_properties_t >( e ); p && e.m_type == "VuAttachEntity" )
    {
        w = { p->attach_at_start };
        vec( p->relative_position );
        vec( p->relative_rotation );
        texts = { p->node_name };
    }
    else if ( const auto* p = entity_action< play_animation_properties_t >( e ); p && e.m_type == "VuPlayAnimationEntity" )
    {
        texts = { p->animation_asset, p->timed_event_asset };
        w = { f( p->start_time ), f( p->blend_time ), f( p->time_factor ), p->looping, p->one_shot };
    }
    else if ( const auto* p = entity_action< sky_box_properties_t >( e ); p && e.m_type == "VuSkyBoxEntity" )
        texts = { p->model_asset };
    else if ( const auto* p = entity_action< water_texture_properties_t >( e ); p && e.m_type == "VuWaterTextureSettingsEntity" )
        w = { f( p->gravity ), f( p->world_size ), f( p->wind_speed ), f( p->directional_power ), f( p->suppression_wave_length ),
            f( p->height_factor ), f( p->time_factor ), f( p->normal_texture_scale ) };
    else if ( const auto* p = entity_action< front_end_camera_properties_t >( e ); p && e.m_type == "VuSetFrontEndCameraEntity" )
    {
        w = { f( p->interpolation_time ), p->modify_eye, p->modify_target, p->modify_far };
        vec( p->eye );
        vec( p->target );
        w.push_back( f( p->far_plane ) );
    }
    else if ( const auto* p = entity_action< point_wave_properties_t >( e ); p && e.m_type == "VuPointWaveEntity" )
        for ( float x : p->word )
            w.push_back( f( x ) );
    else
        return false;
    return true;
}

// The action / logic / motion / settings entities' property loads (engine
// entity/actions.hxx, through types.hxx's action table and the tree's
// entity_load_properties) against retail's VuEntity::load (oracle case
// load_actions: ten types from the templates and projects plus hand-built).
inline bool verify_actions( )
{
    std::map< std::string, entity_t* > by_name;
    const std::unique_ptr< entity_t > tree = world_check_tree( g_golden_actions_document, g_golden_actions_document_bytes, by_name );
    int failed = 0;
    int compared = 0;
    for ( std::size_t k = 0; k < g_golden_actions_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_actions[ k ];
        const auto found = by_name.find( want.m_child );
        std::vector< std::uint32_t > ours;
        std::vector< std::string > texts;
        if ( found == by_name.end( ) || found->second->m_type != want.m_type || !world_action_words( *found->second, ours, texts ) )
        {
            LOG_ERROR( "actions: '%s' (%s) missing or without state", want.m_source, want.m_child );
            ++failed;
            continue;
        }
        world_check_words( "actions", want, ours, failed, compared );
        for ( std::size_t t = 0; t < texts.size( ); ++t )
        {
            ++compared;
            if ( texts[ t ] != want.m_texts[ t ] && ++failed <= 8 )
                LOG_ERROR( "actions: '%s' text %d is '%s', retail '%s'", want.m_source, static_cast< int >( t ), texts[ t ].c_str( ), want.m_texts[ t ] );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "the action / logic / settings loads match retail's VuEntity::load on %d entities of 10 types: %d values, bit for bit",
            static_cast< int >( g_golden_actions_count ), compared );
    else
        LOG_ERROR( "the action / logic / settings loads FAILED %d checks", failed );
    return failed == 0;
}

// trigger_plane_crossing (hydro trigger/volume.hxx) against
// VuTriggerPlaneEntity_testCrossings 0x5D1570 run on seeded planes and
// instigators (oracle case trigger_plane_test): per (plane, instigator) whether
// it fires and which plug, one failure per pair.
inline bool verify_trigger_plane( )
{
    int failed = 0;
    int pairs = 0;
    int fires = 0;
    for ( std::size_t p = 0; p < g_golden_trigger_planes_count; ++p )
    {
        const golden_trigger_plane_t& plane = g_golden_trigger_planes[ p ];
        float world[ 16 ], scale[ 3 ];
        std::memcpy( world, plane.m_world, sizeof( world ) );
        std::memcpy( scale, plane.m_scale, sizeof( scale ) );
        for ( int k = 0; k < plane.m_count; ++k )
        {
            const golden_trigger_instigator_t& g = g_golden_trigger_instigators[ plane.m_first + k ];
            trigger_instigator_t who{};
            who.mask = g.m_mask;
            std::memcpy( who.prev, g.m_prev, sizeof( who.prev ) );
            std::memcpy( &who.radius, &g.m_radius, 4 );
            std::memcpy( who.cur, g.m_cur, sizeof( who.cur ) );
            std::memcpy( &who.rate, &g.m_rate, 4 );
            const int ours = trigger_plane_crossing( world, scale, plane.m_mask, who );
            ++pairs;
            fires += g.m_fired >= 0;
            if ( ours != g.m_fired && ++failed <= 8 )
                LOG_ERROR( "trigger plane: plane %d instigator %d gives %d, retail %d", static_cast< int >( p ), k, ours, g.m_fired );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "trigger_plane_crossing matches VuTriggerPlaneEntity_testCrossings 0x5D1570 on %d planes: %d plane / instigator pairs, %d "
                     "crossings fired", static_cast< int >( g_golden_trigger_planes_count ), pairs, fires );
    else
        LOG_ERROR( "trigger_plane_crossing FAILED %d of %d pairs", failed, pairs );
    return failed == 0;
}

// The race rules' bookkeeping (world/rules.hxx: race_rules_distances 0x483230,
// race_rules_checkpoints 0x483540 with race_rules_next_checkpoint 0x4D0820 and
// the boat agent's track_checkpoint_index 0x4D0900, race_rules_clocks
// 0x4831D0, race_rules_places 0x48AB20) against retail's run frame by frame
// (oracle case race_progress): one failure per (frame, boat) whose words /
// finished / checkpoint / driver +0x78 differ, per frame whose order or calls
// differ.
inline bool verify_race_progress( )
{
    int failed = 0, rows = 0, crossings = 0, finishes = 0;
    const auto u2f = []( unsigned u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    for ( std::size_t ri = 0; ri < g_golden_rp_runs_count; ++ri )
    {
        const golden_rp_run_t& run = g_golden_rp_runs[ ri ];
        const int nb = run.m_boats;
        track_manager_t tm;
        tm.sectors.resize( 1 );
        tm.end = 0;
        if ( run.m_circuit )
            tm.sectors[ 0 ].next.push_back( 0 );
        for ( int w = 0; w < 4; ++w )
            tm.checkpoints.push_back( w );
        std::vector< std::array< std::uint8_t, 0x100 > > sectors( static_cast< std::size_t >( run.m_sec_count ) );
        for ( int s = 0; s < run.m_sec_count; ++s )
        {
            std::uint8_t* q = sectors[ static_cast< std::size_t >( s ) ].data( );
            std::memset( q, 0, 0x100 );
            const unsigned* w = g_golden_rp_sectors[ run.m_sec + s ];
            std::memcpy( q + 0x40, w, 16 );
            std::memcpy( q + 0x70, w + 4, 16 );
            std::memcpy( q + 0xB0, w + 8, 4 );
            std::memcpy( q + 0xBC, w + 9, 4 );
        }
        std::vector< std::array< std::uint8_t, 0x520 > > images( static_cast< std::size_t >( nb ) );
        std::vector< std::array< std::uint8_t, 0xC8 > > transforms( static_cast< std::size_t >( nb ) );
        std::vector< std::array< std::uint8_t, 0xD0 > > drivers( static_cast< std::size_t >( nb ) );
        std::vector< race_rules_boat_t > boats( static_cast< std::size_t >( nb ) );
        race_rules_game_t game;
        game.circuit = static_cast< std::uint8_t >( run.m_circuit );
        game.length = u2f( run.m_length );
        game.laps = run.m_laps;
        for ( int k = 0; k < nb; ++k )
        {
            const golden_rp_start_t& st = g_golden_rp_start[ run.m_start + k ];
            std::uint8_t* b = images[ static_cast< std::size_t >( k ) ].data( );
            std::memset( b, 0, 0x520 );
            std::memset( transforms[ static_cast< std::size_t >( k ) ].data( ), 0, 0xC8 );
            std::memset( drivers[ static_cast< std::size_t >( k ) ].data( ), 0, 0xD0 );
            rules_put_i32( b, 0x4AC, st.m_lap );
            std::memcpy( b + 0x4B8, &st.m_best, 4 );
            std::memcpy( b + 0x50C, st.m_prev, 12 );
            boats[ static_cast< std::size_t >( k ) ] = { b, transforms[ static_cast< std::size_t >( k ) ].data( ), drivers[ static_cast< std::size_t >( k ) ].data( ),
                                                         nullptr, st.m_cp, false };
            game.order.push_back( k );
        }
        std::vector< std::array< int, 3 > > calls;
        race_checkpoint_hooks_t h;
        h.view = [ & ]( int w, race_checkpoint_view_t& v ) {
            const golden_rp_wp_t& g = g_golden_rp_waypoints[ run.m_wp + w ];
            for ( int i = 0; i < 3; ++i )
            {
                v.normal[ i ] = u2f( g.m_normal[ i ] );
                v.origin[ i ] = u2f( g.m_origin[ i ] );
            }
            v.finish = static_cast< std::uint8_t >( g.m_finish );
            v.width = u2f( g.m_width );
            v.height = u2f( g.m_height );
            return true;
        };
        h.index = [ & ]( int w ) { return track_checkpoint_index( tm, w ); };
        h.next = [ & ]( int w ) { return race_rules_next_checkpoint( tm, w ); };
        h.crossed = [ & ]( int boat, int w ) { calls.push_back( { 0, boat, w } ); };
        h.finished = [ & ]( int boat ) { calls.push_back( { 1, boat, -1 } ); };
        for ( int f = 0; f < run.m_frames; ++f )
        {
            const golden_rp_frame_t& gf = g_golden_rp_frames[ run.m_frame + f ];
            for ( int k = 0; k < nb; ++k )
            {
                const golden_rp_row_t& row = g_golden_rp_rows[ ( run.m_frame + f ) * nb + k ];
                std::memcpy( transforms[ static_cast< std::size_t >( k ) ].data( ) + 0x80, row.m_pos, 12 );
                boats[ static_cast< std::size_t >( k ) ].sector = row.m_sector < 0 ? nullptr : sectors[ static_cast< std::size_t >( row.m_sector ) ].data( );
            }
            calls.clear( );
            race_rules_distances( game, boats );
            race_rules_checkpoints( game, boats, h );
            race_rules_clocks( boats, u2f( run.m_dt ) );
            race_rules_places( game, boats );
            for ( int k = 0; k < nb; ++k )
            {
                const golden_rp_row_t& row = g_golden_rp_rows[ ( run.m_frame + f ) * nb + k ];
                const race_rules_boat_t& r = boats[ static_cast< std::size_t >( k ) ];
                static const int offs[ 10 ] = { 0x308, 0x4AC, 0x4B0, 0x4B4, 0x4B8, 0x4BC, 0x4C0, 0x4C4, 0x4C8, 0x4CC };
                bool same = r.boat[ 0x32F ] == row.m_finished && r.checkpoint == row.m_cp && rules_i32( r.driver, 0x78 ) == row.m_driver78;
                for ( int i = 0; i < 10; ++i )
                    same = same && static_cast< unsigned >( rules_i32( r.boat, offs[ i ] ) ) == row.m_words[ i ];
                ++rows;
                if ( !same && ++failed <= 8 )
                    LOG_ERROR( "race progress: run %d frame %d boat %d differs (lap %d / %u, place %d / %u, cp %d / %d)", static_cast< int >( ri ), f, k,
                               rules_i32( r.boat, 0x4AC ), row.m_words[ 1 ], rules_i32( r.boat, 0x4CC ), row.m_words[ 9 ], r.checkpoint, row.m_cp );
                std::memcpy( r.boat + 0x50C, r.transform + 0x80, 12 );   // 0x482F10's +0x4DC copy (its row 3)
            }
            bool same = static_cast< int >( calls.size( ) ) == gf.m_calls;
            for ( int c = 0; same && c < gf.m_calls; ++c )
            {
                const golden_rp_call_t& gc = g_golden_rp_calls[ gf.m_call + c ];
                same = calls[ static_cast< std::size_t >( c ) ] == std::array< int, 3 >{ gc.m_kind, gc.m_boat, gc.m_wp };
                crossings += gc.m_kind == 0;
                finishes += gc.m_kind == 1;
            }
            for ( int k = 0; same && k < nb; ++k )
                same = game.order[ static_cast< std::size_t >( k ) ] == gf.m_order[ k ];
            if ( !same && ++failed <= 8 )
                LOG_ERROR( "race progress: run %d frame %d order / calls differ (%d calls, retail %d)", static_cast< int >( ri ), f,
                           static_cast< int >( calls.size( ) ), gf.m_calls );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "the race rules' bookkeeping matches retail's 0x483230 / 0x483540 (0x4D0820 / 0x4D0900) / 0x4831D0 / 0x48AB20 on %d runs: %d "
                     "boat-frames (13 values each), %d checkpoint crossings, %d finishes, the place order every frame",
                     static_cast< int >( g_golden_rp_runs_count ), rows, crossings, finishes );
    else
        LOG_ERROR( "the race rules' bookkeeping FAILED %d checks", failed );
    return failed == 0;
}

// multi_delay_in / multi_delay_tick (world/rules.hxx) against
// VuMultiDelayEntity's In 0x5E0660 and tick 0x5E06B0 (oracle case
// multi_delay): one failure per tick whose outputs (count and order), +0xA0 or
// +0xA4 bits differ.
inline bool verify_multi_delay( )
{
    int failed = 0, ticks = 0, fires = 0;
    for ( std::size_t r = 0; r < g_golden_md_runs_count; ++r )
    {
        const golden_md_run_t& run = g_golden_md_runs[ r ];
        multi_delay_t m;
        std::memcpy( m.delay, run.m_delays, sizeof( m.delay ) );
        for ( int t = 0; t < run.m_count; ++t )
        {
            const golden_md_tick_t& g = g_golden_md_ticks[ run.m_first + t ];
            if ( g.m_in )
                multi_delay_in( m );
            float dt;
            std::memcpy( &dt, &g.m_dt, 4 );
            int count = 0;
            unsigned order = 0;
            multi_delay_tick( m, dt, [ & ]( int k, const std::uint8_t* ) { order |= static_cast< unsigned >( k ) << ( 4 * count++ ); } );
            unsigned time;
            std::memcpy( &time, &m.time, 4 );
            ++ticks;
            fires += g.m_fired;
            if ( ( count != g.m_fired || order != g.m_order || m.active != g.m_active || time != g.m_time ) && ++failed <= 8 )
                LOG_ERROR( "multi delay: run %d tick %d fired %d (0x%X) active %d time 0x%08X, retail %d (0x%X) %d 0x%08X", static_cast< int >( r ), t,
                           count, order, m.active, time, g.m_fired, g.m_order, g.m_active, g.m_time );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "multi_delay_in / multi_delay_tick match VuMultiDelayEntity 0x5E0660 / 0x5E06B0 on %d runs: %d ticks, %d outputs fired in order",
                     static_cast< int >( g_golden_md_runs_count ), ticks, fires );
    else
        LOG_ERROR( "multi_delay FAILED %d of %d ticks", failed, ticks );
    return failed == 0;
}

// trigger_box_crossing (hydro trigger/volume.hxx) against
// VuTriggerBoxEntity_testCrossings 0x5D1AB0 run on seeded boxes and
// instigators (oracle case trigger_box_test): one failure per (box, instigator)
// pair whose answer (none / OnEnter / OnExit) differs.
inline bool verify_trigger_box( )
{
    int failed = 0, pairs = 0, fires = 0;
    for ( std::size_t p = 0; p < g_golden_trigger_boxes_count; ++p )
    {
        const golden_trigger_plane_t& box = g_golden_trigger_boxes[ p ];
        float world[ 16 ], scale[ 3 ];
        std::memcpy( world, box.m_world, sizeof( world ) );
        std::memcpy( scale, box.m_scale, sizeof( scale ) );
        for ( int k = 0; k < box.m_count; ++k )
        {
            const golden_trigger_instigator_t& g = g_golden_trigger_box_instigators[ box.m_first + k ];
            trigger_instigator_t who{};
            who.mask = g.m_mask;
            std::memcpy( who.prev, g.m_prev, sizeof( who.prev ) );
            std::memcpy( &who.radius, &g.m_radius, 4 );
            std::memcpy( who.cur, g.m_cur, sizeof( who.cur ) );
            std::memcpy( &who.rate, &g.m_rate, 4 );
            const int ours = trigger_box_crossing( world, scale, box.m_mask, who );
            ++pairs;
            fires += g.m_fired >= 0;
            if ( ours != g.m_fired && ++failed <= 8 )
                LOG_ERROR( "trigger box: box %d instigator %d gives %d, retail %d", static_cast< int >( p ), k, ours, g.m_fired );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "trigger_box_crossing matches VuTriggerBoxEntity_testCrossings 0x5D1AB0 on %d boxes: %d box / instigator pairs, %d "
                     "crossings fired", static_cast< int >( g_golden_trigger_boxes_count ), pairs, fires );
    else
        LOG_ERROR( "trigger_box_crossing FAILED %d of %d pairs", failed, pairs );
    return failed == 0;
}

// static_model_select (engine entity/draw.hxx) against
// Vu3dDrawStaticModelComponent's draw 0x5B0730 on seeded inputs (oracle case
// static_model_draw): which model instance is drawn (or none) and the three
// words written into it, one failure per vector.
inline bool verify_static_model_draw( )
{
    const auto f = []( unsigned w )
    {
        float v;
        std::memcpy( &v, &w, sizeof( v ) );
        return v;
    };
    int failed = 0;
    int drawn = 0;
    for ( std::size_t k = 0; k < g_golden_static_draws_count; ++k )
    {
        const golden_static_draw_t& g = g_golden_static_draws[ k ];
        model_draw_t c;
        model_draw_construct( c, false );
        c.color = g.m_color;
        c.draw_distance = f( g.m_draw );
        c.lod0_draw_distance = f( g.m_lod0 );
        c.lod1_draw_distance = f( g.m_lod1 );
        c.rejection_scale_modifier = f( g.m_reject );
        c.dynamic_lighting = static_cast< std::uint8_t >( g.m_dynamic );
        const float lo[ 3 ] = { f( g.m_min[ 0 ] ), f( g.m_min[ 1 ] ), f( g.m_min[ 2 ] ) };
        const float hi[ 3 ] = { f( g.m_max[ 0 ] ), f( g.m_max[ 1 ] ), f( g.m_max[ 2 ] ) };
        const float eye[ 3 ] = { f( g.m_eye[ 0 ] ), f( g.m_eye[ 1 ] ), f( g.m_eye[ 2 ] ) };
        const static_draw_t ours = static_model_select( c, lo, hi, eye, g.m_reflection != 0, g.m_skip != 0, f( g.m_fade ) );
        std::uint32_t rejection;
        std::memcpy( &rejection, &ours.rejection, 4 );
        const bool same = ours.instance == g.m_instance &&
                          ( ours.instance < 0 || ( rejection == g.m_w14 && ours.color == g.m_w18 && ours.dynamic_lighting == g.m_b1c ) );
        drawn += g.m_instance >= 0;
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "static model draw: vector %d draws %d (0x%08X 0x%08X %d), retail %d (0x%08X 0x%08X %d)", static_cast< int >( k ), ours.instance,
                rejection, ours.color, ours.dynamic_lighting, g.m_instance, g.m_w14, g.m_w18, g.m_b1c );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "static_model_select matches Vu3dDrawStaticModelComponent's draw 0x5B0730 on %d vectors (%d drawn): instance, rejection, "
                     "colour and lighting bit for bit", static_cast< int >( g_golden_static_draws_count ), drawn );
    else
        LOG_ERROR( "static_model_select FAILED %d vectors", failed );
    return failed == 0;
}

// scene_component_aabb (hydro assets/scene.hxx) against the static model
// component's world aabb 0x531D10 on seeded inputs (oracle case
// static_world_aabb): the six x y z lanes bit for bit and whether the draw
// manager was handed the component, one failure per vector.
inline bool verify_static_world_aabb( )
{
    int failed = 0;
    int registered = 0;
    for ( std::size_t k = 0; k < g_golden_static_world_aabbs_count; ++k )
    {
        const golden_static_world_aabb_t& g = g_golden_static_world_aabbs[ k ];
        float source[ 8 ], matrix[ 16 ], aabb[ 8 ];
        std::memcpy( source, g.m_source, sizeof( source ) );
        std::memcpy( matrix, g.m_matrix_a, 32 );
        std::memcpy( matrix + 8, g.m_matrix_b, 32 );
        const bool handed = scene_component_aabb( aabb, source, matrix, g.m_register != 0 );
        const float lanes[ 6 ] = { aabb[ 0 ], aabb[ 1 ], aabb[ 2 ], aabb[ 4 ], aabb[ 5 ], aabb[ 6 ] };
        const bool same = std::memcmp( lanes, g.m_out, sizeof( lanes ) ) == 0 && handed == ( g.m_registered != 0 );
        registered += g.m_registered != 0;
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "static world aabb: vector %d differs (handed %d, retail %d)", static_cast< int >( k ), handed ? 1 : 0, g.m_registered );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "scene_component_aabb matches the static model component's world aabb 0x531D10 (0x41B2A0 / 0x5175E0) on %d vectors "
                     "(%d handed to the draw manager) bit for bit", static_cast< int >( g_golden_static_world_aabbs_count ), registered );
    else
        LOG_ERROR( "scene_component_aabb FAILED %d vectors", failed );
    return failed == 0;
}

// The event graph as VuScriptComponent's loads resolve it (engine
// entity/script.hxx: 0x503D70 / 0x504320, 0x502430, 0x542830, 0x5049E0)
// against retail's VuProject::load on Games/HydroelectricRace (its level's
// templates served) and a hand-built project (oracle case script_graph): the
// tree pre-order (parent, name) and, per script component, every plug's links
// in +0x50 order, every ref's target and the +0x28 referrers. One failure per
// entity that differs in any of it.
inline bool verify_script_graph( )
{
    static std::map< std::string, value_t > templates;
    if ( templates.empty( ) )
        for ( std::size_t k = 0; k < g_golden_script_templates_count; ++k )
        {
            const golden_script_document_t& d = g_golden_script_templates[ k ];
            const unsigned char* raw = reinterpret_cast< const unsigned char* >( d.m_words );
            templates[ d.m_name ] = c_reader( std::vector< std::uint8_t >( raw, raw + d.m_bytes ) ).parse( );
        }
    std::vector< std::string > names;
    for ( const auto& pair : templates )
        names.push_back( pair.first );
    const entity_registry_t registry = build_entity_registry( names );
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    world.m_template = []( const std::string& name ) -> const value_t*
    {
        const auto found = templates.find( name );
        return found != templates.end( ) ? &found->second : nullptr;
    };
    world.m_components_for = entity_components_scripted;
    world.m_component_load = entity_load_component;
    world.m_component_post = entity_post_load_component;
    world.m_component_mark = entity_mark_component;
    world.m_state_for = entity_state_for;
    world.m_properties_load = entity_load_properties;
    world.m_properties_mark = entity_mark_properties;
    int failed = 0;
    int entities = 0;
    int links = 0;
    int bound = 0;
    for ( std::size_t p = 0; p < g_golden_script_projects_count; ++p )
    {
        const golden_script_project_t& want = g_golden_script_projects[ p ];
        const unsigned char* raw = reinterpret_cast< const unsigned char* >( want.m_words );
        const value_t document = c_reader( std::vector< std::uint8_t >( raw, raw + want.m_bytes ) ).parse( );
        const std::unique_ptr< entity_t > tree = entity_load_project( member( document, "VuProject" ), want.m_path, world );
        std::vector< entity_t* > order;
        std::vector< int > parent_of;
        std::map< const entity_t*, int > index_of;
        const auto walk = [ & ]( const auto& self, entity_t& e, int parent ) -> void
        {
            index_of[ &e ] = static_cast< int >( order.size( ) );
            order.push_back( &e );
            parent_of.push_back( parent );
            const int here = index_of[ &e ];
            for ( auto& child : e.m_children )
                self( self, *child, here );
        };
        if ( tree )
            walk( walk, *tree, -1 );
        if ( static_cast< int >( order.size( ) ) != want.m_count )
        {
            LOG_ERROR( "script graph: %s has %d entities, retail %d", want.m_asset, static_cast< int >( order.size( ) ), want.m_count );
            ++failed;
            continue;
        }
        const auto index = [ & ]( const entity_t* e ) { const auto it = index_of.find( e ); return it != index_of.end( ) ? it->second : -1; };
        for ( int k = 0; k < want.m_count; ++k )
        {
            const golden_script_entity_t& g = g_golden_script_entities[ want.m_first + k ];
            entity_t& e = *order[ k ];
            ++entities;
            const script_component_t* sc = script_of( e );
            bool same = e.m_name == g.m_name && parent_of[ k ] == g.m_parent && ( sc != nullptr ) == ( g.m_script != 0 );
            if ( same && sc )
            {
                same = static_cast< int >( sc->m_plugs.size( ) ) == g.m_plugs && static_cast< int >( sc->m_refs.size( ) ) == g.m_refs &&
                       static_cast< int >( sc->m_referrers.size( ) ) == g.m_referrers;
                for ( int i = 0; same && i < g.m_plugs; ++i )
                {
                    const golden_script_plug_t& gp = g_golden_script_plugs[ g.m_first_plug + i ];
                    const script_plug_t& ours = sc->m_plugs[ i ];
                    same = ours.m_row->m_name == std::string( gp.m_name ) && static_cast< int >( ours.m_links.size( ) ) == gp.m_count;
                    for ( int l = 0; same && l < gp.m_count; ++l )
                    {
                        const golden_script_end_t& gl = g_golden_script_links[ gp.m_first + l ];
                        const script_link_t& link = ours.m_links[ l ];
                        same = index( link.m_entity ) == gl.m_entity && script_of( *link.m_entity )->m_plugs[ link.m_plug ].m_row->m_name == std::string( gl.m_name );
                        ++links;
                    }
                }
                for ( int i = 0; same && i < g.m_refs; ++i )
                {
                    const golden_script_ref_t& gr = g_golden_script_refs[ g.m_first_ref + i ];
                    same = sc->m_refs[ i ].m_row->m_name == std::string( gr.m_name ) && index( sc->m_refs[ i ].m_target ) == gr.m_target;
                    bound += gr.m_target >= 0;
                }
                for ( int i = 0; same && i < g.m_referrers; ++i )
                {
                    const golden_script_end_t& gr = g_golden_script_referrers[ g.m_first_referrer + i ];
                    const script_referrer_t& r = sc->m_referrers[ i ];
                    same = index( r.m_entity ) == gr.m_entity && script_of( *r.m_entity )->m_refs[ r.m_ref ].m_row->m_name == std::string( gr.m_name );
                }
            }
            if ( !same && ++failed <= 8 )
                LOG_ERROR( "script graph: %s entity %d '%s' differs from retail ('%s', parent %d, script %d)", want.m_asset, k, e.m_name.c_str( ),
                    g.m_name, g.m_parent, g.m_script );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuScriptComponent's loads (0x503D70 / 0x504320: connections 0x502430, refs 0x542830, ref connections 0x5049E0) match "
                     "retail on %d entities of %d projects: %d link entries (each connection on both ends) and %d bound refs, in order", entities,
            static_cast< int >( g_golden_script_projects_count ), links, bound );
    else
        LOG_ERROR( "script graph FAILED on %d entities", failed );
    return failed == 0;
}

// animated_model_select against Vu3dDrawAnimatedModelComponent's draw 0x61DB60
// (oracle case animated_model_draw): whether the instance is drawn and the
// colour / lighting written first, one failure per vector.
inline bool verify_animated_model_draw( )
{
    int failed = 0;
    int drawn = 0;
    for ( std::size_t k = 0; k < g_golden_animated_draws_count; ++k )
    {
        const golden_animated_draw_t& g = g_golden_animated_draws[ k ];
        model_draw_t c;
        model_draw_construct( c, true );
        c.color = g.m_color;
        c.draw_distance = draw_bits( g.m_draw );
        c.dynamic_lighting = static_cast< std::uint8_t >( g.m_dynamic );
        float lo[ 3 ], hi[ 3 ], eye[ 3 ];
        for ( int a = 0; a < 3; ++a )
        {
            lo[ a ] = draw_bits( g.m_min[ a ] );
            hi[ a ] = draw_bits( g.m_max[ a ] );
            eye[ a ] = draw_bits( g.m_eye[ a ] );
        }
        const animated_draw_t ours = animated_model_select( c, lo, hi, eye, g.m_skip != 0 );
        const bool same = ours.drawn == ( g.m_drawn != 0 ) && ( !ours.drawn || ( ours.color == g.m_wf8 && ours.dynamic_lighting == g.m_bfc ) );
        drawn += g.m_drawn;
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "animated model draw: vector %d drawn %d (0x%08X %d), retail %d (0x%08X %d)", static_cast< int >( k ), ours.drawn ? 1 : 0,
                ours.color, ours.dynamic_lighting, g.m_drawn, g.m_wf8, g.m_bfc );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "animated_model_select matches Vu3dDrawAnimatedModelComponent's draw 0x61DB60 on %d vectors (%d drawn): drawn, colour "
                     "and lighting bit for bit", static_cast< int >( g_golden_animated_draws_count ), drawn );
    else
        LOG_ERROR( "animated_model_select FAILED %d vectors", failed );
    return failed == 0;
}

// The digests case_static_scene_load takes of retail's VuGfxStaticScene (FNV-1a 32 over little-endian words and
// NUL-ended strings), built here from hydro's scene_t in the same order.
struct scene_digest_t
{
    std::uint32_t h = 0x811C9DC5u;
    void bytes( const void* data, std::size_t count )
    {
        const std::uint8_t* p = static_cast< const std::uint8_t* >( data );
        for ( std::size_t k = 0; k < count; ++k )
            h = ( h ^ p[ k ] ) * 0x01000193u;
    }
    void word( std::uint32_t w ) { bytes( &w, 4 ); }
    void floats( const float* f, int count ) { bytes( f, 4 * static_cast< std::size_t >( count ) ); }
    void text( const std::string& s ) { bytes( s.c_str( ), s.size( ) + 1 ); }
};

// Base.apf next to the exe or up to eight folders above it (c_world::pack_path's walk).
inline std::string world_check_pack( )
{
#if defined( _WIN32 )
    char module_path[ MAX_PATH ]{};
    GetModuleFileNameA( nullptr, module_path, MAX_PATH );
    std::string directory = module_path;
    for ( int step = 0; step < 9; ++step )
    {
        const auto slash = directory.find_last_of( "\\/" );
        if ( slash == std::string::npos )
            break;
        directory.resize( slash );
        for ( const char* leaf : { "\\HydroThunderUWP\\Base.apf", "\\Base.apf" } )
            if ( GetFileAttributesA( ( directory + leaf ).c_str( ) ) != INVALID_FILE_ATTRIBUTES )
                return directory + leaf;
    }
#endif
    return {};
}

// scene_load_static (hydro assets/scene.hxx) against retail's VuGfxStaticScene load 0x560C70 (oracle case
// static_scene_load: VuStaticModelAsset's load 0x542C30 run on real blobs). Each blob is read from Base.apf here;
// compared: the nine totals, the scene aabb (min / max x y z), and digests of the shaders (index, flag, the six
// bytes, mode, declaration, the three material tables), the D3D11 stages retail created (its stage cache: a key
// already made is not made again), the meshes + parts, the chunks + their buffer bytes, and the node tree (name,
// matrix, mesh, children, aabb). One failure per asset.
inline bool verify_static_scene_load( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "verify_static_scene_load: Base.apf not found" );
        return false;
    }
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_static_scenes_count; ++k )
    {
        const golden_static_scene_t& want = g_golden_static_scenes[ k ];
        std::vector< std::uint8_t > blob;
        scene_t scene;
        const std::string name = std::string( "VuStaticModelAsset/" ) + want.m_asset;
        if ( !archive.read( name.c_str( ), blob ) || !scene_load_static( blob.data( ), blob.size( ), scene ) )
        {
            LOG_ERROR( "static scene '%s': scene_load_static failed", want.m_asset );
            ++failed;
            continue;
        }
        const std::uint32_t totals[ 9 ] = { scene.mesh_count, scene.part_count, scene.vertex_count, scene.triangle_count,
            scene.shader_count, scene.node_total, scene.instance_total, scene.vertex_total, scene.triangle_total };
        std::uint32_t aabb[ 6 ];
        std::memcpy( aabb, scene.aabb, 12 );
        std::memcpy( aabb + 3, scene.aabb + 4, 12 );
        scene_digest_t shaders, stages, meshes, chunks, tree;
        std::vector< std::uint32_t > made;
        for ( const scene_shader_t& s : scene.shaders )
        {
            shaders.word( s.index );
            shaders.word( s.flag );
            shaders.bytes( s.bytes, 6 );
            shaders.word( s.mode );
            shaders.bytes( s.elements.data( ), 16 * s.elements.size( ) );
            for ( const scene_table_t& t : s.tables )
            {
                shaders.word( static_cast< std::uint32_t >( t.constants.size( ) ) );
                for ( const scene_constant_t& c : t.constants )
                {
                    shaders.text( c.name );
                    shaders.word( c.type );
                    for ( std::uint32_t w : c.value )
                        shaders.word( w );
                }
                shaders.word( static_cast< std::uint32_t >( t.textures.size( ) ) );
                for ( const scene_texture_t& x : t.textures )
                {
                    shaders.text( x.name );
                    shaders.text( x.slot );
                    shaders.word( x.value );
                    shaders.text( x.asset );
                }
            }
            const scene_stage_t* both[ 2 ] = { &s.program.vertex, &s.program.pixel };
            for ( std::uint32_t type = 0; type < 2; ++type )
            {
                const scene_stage_t& st = *both[ type ];
                if ( std::find( made.begin( ), made.end( ), st.key ) != made.end( ) )
                    continue;
                made.push_back( st.key );
                scene_digest_t code;
                code.bytes( blob.data( ) + st.bytecode.offset, st.bytecode.size );
                stages.word( type );
                stages.word( st.bytecode.size );
                stages.word( code.h );
                stages.word( st.constants );
            }
        }
        for ( const scene_mesh_t& m : scene.meshes )
        {
            meshes.text( m.name );
            meshes.word( static_cast< std::uint32_t >( m.parts.size( ) ) );
            meshes.floats( m.aabb, 8 );
            for ( const scene_part_t& p : m.parts )
            {
                for ( std::uint32_t w : { p.chunk, p.shader, p.first_vertex, p.last_vertex, p.vertices, p.first_index, p.triangles } )
                    meshes.word( w );
                meshes.floats( p.aabb, 8 );
            }
        }
        for ( const scene_chunk_t& c : scene.chunks )
        {
            chunks.word( c.stride );
            chunks.word( c.vertices.size );
            chunks.word( c.indices );
            chunks.bytes( blob.data( ) + c.vertices.offset, c.vertices.size );
            chunks.bytes( blob.data( ) + c.index_bytes.offset, c.index_bytes.size );
        }
        for ( const scene_node_t& n : scene.nodes )
        {
            tree.text( n.name );
            tree.floats( n.matrix, 16 );
            tree.word( n.mesh >= 0 ? static_cast< std::uint32_t >( n.mesh ) : 0xFFFFFFFFu );
            tree.word( static_cast< std::uint32_t >( n.children.size( ) ) );
            tree.floats( n.aabb, 3 );
            tree.floats( n.aabb + 4, 3 );
        }
        const int counts[ 4 ] = { static_cast< int >( scene.shaders.size( ) ), static_cast< int >( scene.meshes.size( ) ),
            static_cast< int >( scene.chunks.size( ) ), static_cast< int >( made.size( ) ) };
        const char* what = nullptr;
        if ( std::memcmp( totals, want.m_totals, sizeof( totals ) ) != 0 )
            what = "totals";
        else if ( std::memcmp( counts, want.m_counts, sizeof( counts ) ) != 0 )
            what = "counts";
        else if ( std::memcmp( aabb, want.m_aabb, sizeof( aabb ) ) != 0 )
            what = "scene aabb";
        else if ( shaders.h != want.m_shaders )
            what = "shaders";
        else if ( stages.h != want.m_stages )
            what = "stages";
        else if ( meshes.h != want.m_meshes )
            what = "meshes";
        else if ( chunks.h != want.m_chunks )
            what = "chunks";
        else if ( tree.h != want.m_tree )
            what = "node tree";
        if ( what != nullptr && ++failed <= 8 )
            LOG_ERROR( "static scene '%s': %s differ from retail", want.m_asset, what );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "scene_load_static matches VuGfxStaticScene's load (0x542C30 -> 0x560C70 / 0x5C1560 / 0x5C24D0 / 0x5610F0) "
                     "on %d real VuStaticModelAssets: totals, scene aabb, shaders, stages, meshes, chunks, node tree",
            static_cast< int >( g_golden_static_scenes_count ) );
    else
        LOG_ERROR( "scene_load_static FAILED on %d of %d assets", failed, static_cast< int >( g_golden_static_scenes_count ) );
    return failed == 0;
}

// The water renderer's CPU side (world/grid.hxx) against retail (oracle case
// water_grid): the sixteen templates 0x595D40, the frustum OBB test 0x57D280
// on seeded cameras / boxes / matrices, and whole surfaces through 0x597B50 -
// patches, records and indices word for word (qsort faked under emulation, so
// ours runs with the sort off: the order the quadtree emits).
inline bool verify_water_templates( )
{
    water_template_t t[ 16 ];
    water_templates( t );
    int failed = 0;
    std::size_t xy = 0, ix = 0;
    for ( int k = 0; k < 16; ++k )
    {
        const int nv = g_golden_water_template_counts[ k ][ 0 ];
        const int ni = g_golden_water_template_counts[ k ][ 1 ];
        bool same = static_cast< int >( t[ k ].xy.size( ) ) == 2 * nv && static_cast< int >( t[ k ].index.size( ) ) == ni;
        same = same && std::memcmp( t[ k ].xy.data( ), g_golden_water_template_xy + xy, 8 * static_cast< std::size_t >( nv ) ) == 0;
        same = same && std::memcmp( t[ k ].index.data( ), g_golden_water_template_index + ix, 2 * static_cast< std::size_t >( ni ) ) == 0;
        xy += 2 * static_cast< std::size_t >( nv );
        ix += static_cast< std::size_t >( ni );
        if ( !same && ++failed <= 4 )
            LOG_ERROR( "water template %d differs", k );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "water_templates matches the water renderer's sixteen patch templates 0x595D40 (0x5970B0) vertex and index for index" );
    else
        LOG_ERROR( "water_templates FAILED %d of 16", failed );
    return failed == 0;
}

inline bool verify_water_frustum( )
{
    int failed = 0, visible = 0;
    for ( std::size_t k = 0; k < g_golden_water_frustums_count; ++k )
    {
        const golden_water_frustum_t& g = g_golden_water_frustums[ k ];
        water_frustum_t fr{};
        std::memcpy( fr.f, g.m_frustum, sizeof( fr.f ) );
        float box[ 8 ]{}, m[ 16 ];
        std::memcpy( box, g.m_box, 12 );
        std::memcpy( box + 4, g.m_box + 3, 12 );
        std::memcpy( m, g.m_matrix, sizeof( m ) );
        const bool v = water_frustum_test( fr, box, m );
        visible += g.m_visible != 0;
        if ( v != ( g.m_visible != 0 ) && ++failed <= 8 )
            LOG_ERROR( "water frustum: vector %d ours %d retail %d", static_cast< int >( k ), v ? 1 : 0, g.m_visible );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "water_frustum_test matches the camera frustum OBB test 0x57D280 on %d vectors (%d visible)",
                     static_cast< int >( g_golden_water_frustums_count ), visible );
    else
        LOG_ERROR( "water_frustum_test FAILED %d of %d vectors", failed, static_cast< int >( g_golden_water_frustums_count ) );
    return failed == 0;
}

inline bool verify_water_grid( )
{
    int failed = 0, patches = 0, vertices = 0;
    water_grid_t g{};
    water_templates( g.templates );
    for ( std::size_t k = 0; k < g_golden_water_grids_count; ++k )
    {
        const golden_water_grid_t& r = g_golden_water_grids[ k ];
        water_frustum_t fr{};
        std::memcpy( fr.f, r.m_frustum, sizeof( fr.f ) );
        std::memcpy( fr.position, r.m_eye, sizeof( fr.position ) );
        water_words_t s{};
        for ( int w = 0; w < 68; ++w )
            s.set( 4 * w, r.m_surface[ w ] );
        water_view_t view{};
        water_surface_t q{};
        q.height = s.f( 20 );
        q.wave_root = -1;
        view.water.surfaces.push_back( q );
        water_grid_frame_t out{};
        water_grid_build( g, fr, s, 0, &view, out );   // the sort on: the harness runs the real qsort (boat batch 27)
        bool same = ( out.draws.size( ) == 1 ) == ( r.m_built != 0 ) && out.failed == 0;
        if ( same && r.m_built )
        {
            const water_grid_draw_t& d = out.draws[ 0 ];
            const unsigned n = r.m_record[ 10 ], nv = r.m_record[ 12 ], ni = r.m_record[ 14 ];
            same = static_cast< unsigned >( d.patches ) == n && static_cast< unsigned >( d.vertex_count ) == nv &&
                   static_cast< unsigned >( d.index_count ) == ni && std::memcmp( d.position, &r.m_record[ 1 ], 16 ) == 0 &&
                   std::memcmp( &d.rotation, &r.m_record[ 5 ], 4 ) == 0 && std::memcmp( d.size, &r.m_record[ 6 ], 8 ) == 0 &&
                   std::memcmp( &d.below, &r.m_record[ 8 ], 4 ) == 0 && std::memcmp( &d.above, &r.m_record[ 9 ], 4 ) == 0;
            for ( unsigned p = 0; same && p < n; ++p )
                same = std::memcmp( &g.patches[ p ], g_golden_water_grid_patches + r.m_patch_at + 5 * p, 20 ) == 0;
            same = same && std::memcmp( out.vertices.data( ), g_golden_water_grid_vertices + r.m_vertex_at, 32 * static_cast< std::size_t >( nv ) ) == 0;
            same = same && std::memcmp( out.indices.data( ), g_golden_water_grid_indices + r.m_index_at, 2 * static_cast< std::size_t >( ni ) ) == 0;
            patches += static_cast< int >( n );
            vertices += static_cast< int >( nv );
        }
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "water grid: surface %d differs (ours %d patches %d vertices)", static_cast< int >( k ),
                       static_cast< int >( g.patches.size( ) ), static_cast< int >( out.vertices.size( ) ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "water_grid_build matches the water renderer's surface build 0x597B50 (0x597110 / 0x597260 / 0x597700 / 0x597D70, the "
                     "forced +96 query) on %d surfaces: %d patches, %d vertex records and their indices bit for bit",
                     static_cast< int >( g_golden_water_grids_count ), patches, vertices );
    else
        LOG_ERROR( "water_grid_build FAILED %d of %d surfaces", failed, static_cast< int >( g_golden_water_grids_count ) );
    return failed == 0;
}

// The surface build with a water map (world/grid.hxx water_map_clip /
// water_grid_map_terms) against retail (case water_grid_maps): Hydroelectric's
// real VuWaterMapAssets read from Base.apf through water_map_load (verified by
// water_point), the case's declared synthetic maps rebuilt from the golden
// bytes, one scenario with ClipMaps off and a null-map control; per scenario
// the record, the patches, each patch's vertex block (FNV-1a 32 of its 32-byte
// records) and the indices. One failure per scenario.
inline std::uint32_t world_check_fnv32( const void* data, std::size_t n )
{
    std::uint32_t x = 0x811C9DC5u;
    for ( std::size_t i = 0; i < n; ++i )
        x = ( x ^ static_cast< const std::uint8_t* >( data )[ i ] ) * 0x01000193u;
    return x;
}

inline bool verify_water_grid_maps( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "water grid maps: Base.apf did not open" );
        return false;
    }
    int failed = 0, patches = 0, vertices = 0, mapped = 0;
    water_grid_t g{};
    water_templates( g.templates );
    for ( std::size_t k = 0; k < g_golden_water_grid_maps_count; ++k )
    {
        const golden_water_grid_map_t& r = g_golden_water_grid_maps[ k ];
        water_map_t map{};
        bool have = false;
        const char* what = nullptr;
        if ( r.m_map[ 0 ] )
        {
            std::vector< std::uint8_t > blob;
            have = archive.read( ( std::string( "VuWaterMapAsset/" ) + r.m_map ).c_str( ), blob ) && water_map_load( blob.data( ), blob.size( ), map );
            if ( !have )
                what = "the map did not load";
        }
        else if ( r.m_synth_at >= 0 )
        {
            const unsigned* w = g_golden_wgm_synth + r.m_synth_at;
            map.width = w[ 0 ];
            map.height = w[ 1 ];
            const unsigned char* b = g_golden_wgm_bytes + w[ 2 ];
            std::vector< std::uint8_t >* arrays[ 3 ] = { &map.map, &map.second, &map.third };
            for ( int a = 0; a < 3; ++a )
            {
                arrays[ a ]->assign( b, b + w[ 3 + a ] );
                b += w[ 3 + a ];
            }
            for ( unsigned j = 0; j < w[ 6 ]; ++j )
            {
                water_map_t::entry_t e{};
                e.a = w[ 7 + 3 * j ];
                e.b = w[ 8 + 3 * j ];
                e.bytes.assign( b, b + w[ 9 + 3 * j ] );
                b += w[ 9 + 3 * j ];
                map.entries.push_back( std::move( e ) );
            }
            have = true;
        }
        mapped += have ? 1 : 0;
        water_frustum_t fr{};
        std::memcpy( fr.f, r.m_frustum, sizeof( fr.f ) );
        std::memcpy( fr.position, r.m_eye, sizeof( fr.position ) );
        water_words_t s{};
        for ( int w = 0; w < 68; ++w )
            s.set( 4 * w, r.m_surface[ w ] );
        water_view_t view{};
        water_surface_t q{};
        q.height = s.f( 20 );
        q.wave_root = -1;
        q.map = have ? &map : nullptr;
        view.water.surfaces.push_back( q );
        g.clip_maps = r.m_clip != 0;
        water_grid_frame_t out{};
        water_grid_build( g, fr, s, 0, &view, out );   // the sort on: the harness runs the real qsort (boat batch 27)
        if ( !what && ( ( out.draws.size( ) == 1 ) != ( r.m_built != 0 ) || out.failed != 0 ) )
            what = "built or not / a failed patch";
        if ( !what && r.m_built )
        {
            const water_grid_draw_t& d = out.draws[ 0 ];
            const unsigned n = r.m_record[ 10 ];
            if ( static_cast< unsigned >( d.patches ) != n || static_cast< unsigned >( d.vertex_count ) != r.m_record[ 12 ] ||
                 static_cast< unsigned >( d.index_count ) != r.m_record[ 14 ] )
                what = "the record's counts";
            for ( unsigned p = 0; !what && p < n; ++p )
                if ( std::memcmp( &g.patches[ p ], g_golden_wgm_patches + r.m_patch_at + 5 * p, 20 ) != 0 )
                    what = "a patch";
            std::size_t at = 0;
            for ( unsigned p = 0; !what && p < n; ++p )
            {
                const std::size_t c = g.templates[ g.patches[ p ].flags ].xy.size( ) / 2;
                if ( world_check_fnv32( out.vertices.data( ) + at, 32 * c ) != g_golden_wgm_vertex_fnv[ r.m_fnv_at + p ] )
                    what = "a patch's vertex records";
                at += c;
            }
            if ( !what && world_check_fnv32( out.indices.data( ), 2 * out.indices.size( ) ) != r.m_index_fnv )
                what = "the indices";
            patches += static_cast< int >( n );
            vertices += d.vertex_count;
        }
        if ( what && ++failed <= 6 )
            LOG_ERROR( "water grid maps: scenario %d (%s, ClipMaps %d): %s", static_cast< int >( k ), r.m_map[ 0 ] ? r.m_map : "synthetic / none",
                r.m_clip, what );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "water_grid_build with water maps matches retail's 0x597B50 (clip test 0x5C7E80 in 0x597260; 0x597D70's byte maps "
                     "0x598590 / 0x5986E0 / 0x598810 / inline / 0x598940 / 0x598A70 / 0x598B70) on %d scenarios (%d mapped): %d patches, "
                     "%d vertex records bit for bit",
            static_cast< int >( g_golden_water_grid_maps_count ), mapped, patches, vertices );
    else
        LOG_ERROR( "water grid maps FAILED %d of %d scenarios", failed, static_cast< int >( g_golden_water_grid_maps_count ) );
    return failed == 0;
}

// The tick manager (world/tick.hxx) against retail's VuTickManagerImpl (case
// tick_manager): the same op script replayed - registrations (with their
// return), unregistrations (the handlers destroyed, in order), the dt cap, the
// paused / slow / step bytes, the pause count slots, and every tick: the
// handlers called in order with their dt bits and the manager's +0x18 .. +0x30
// after. Then the final phase list. One failure per op that differs.
inline bool verify_tick_manager( )
{
    struct call_t
    {
        int handler;
        std::uint32_t dt;
    };
    struct user_t
    {
        int id;
        std::vector< call_t >* log;
    };
    std::vector< call_t > log;
    user_t users[ 64 ];
    int owner_of[ 64 ];
    for ( int h = 0; h < 64; ++h )
    {
        users[ h ] = { h, &log };
        owner_of[ h ] = -1;
    }
    static const int owner_keys[ 8 ] = {};
    const auto fn = []( void* u, float dt ) {
        user_t* x = static_cast< user_t* >( u );
        std::uint32_t b;
        std::memcpy( &b, &dt, 4 );
        x->log->push_back( { x->id, b } );
    };
    const auto handler_id = []( const tick_handler_t& h ) { return static_cast< const user_t* >( h.user )->id; };
    tick_manager_t tm{};
    tick_manager_init( tm );
    int failed = 0, ticks = 0, calls = 0, ops = 0;
    const unsigned* w = g_golden_tick_ops;
    const unsigned* end = g_golden_tick_ops + g_golden_tick_ops_count;
    while ( w < end )
    {
        bool same = true;
        const unsigned op = *w++;
        ++ops;
        if ( op == 1 )
        {
            const int h = static_cast< int >( w[ 0 ] );
            owner_of[ h ] = static_cast< int >( w[ 1 ] );
            const tick_handler_t th{ &owner_keys[ w[ 1 ] ], fn, &users[ h ] };
            same = tick_register( tm, th, g_golden_tick_phase_names[ w[ 2 ] ] ) == ( w[ 3 ] != 0 );
            w += 4;
        }
        else if ( op == 2 || op == 3 )
        {
            const int o = static_cast< int >( *w++ );
            const char* phase = op == 2 ? g_golden_tick_phase_names[ *w++ ] : nullptr;
            const unsigned n = *w++;
            std::vector< int > destroyed;   // the handlers 0x4F7890 destroys, in list order
            for ( tick_phase_t& p : tm.phases )
                if ( !phase || p.name == phase )
                {
                    for ( const tick_handler_t& h : p.handlers )
                        if ( h.owner == &owner_keys[ o ] )
                            destroyed.push_back( handler_id( h ) );
                    if ( phase )
                        break;
                }
            if ( phase )
                tick_unregister( tm, &owner_keys[ o ], phase );
            else
                tick_unregister_all( tm, &owner_keys[ o ] );
            same = destroyed.size( ) == n;
            for ( unsigned k = 0; same && k < n; ++k )
                same = destroyed[ k ] == static_cast< int >( w[ k ] );
            w += n;
        }
        else if ( op == 4 )
            std::memcpy( &tm.cap, w++, 4 );
        else if ( op == 5 )
        {
            tm.paused = w[ 0 ] != 0;
            tm.slow = w[ 1 ] != 0;
            tm.step = w[ 2 ] != 0;
            w += 3;
        }
        else if ( op == 6 )
        {
            const unsigned which = *w++;
            tm.pause_count = which == 0 ? tm.pause_count + 1 : which == 1 ? tm.pause_count - 1 : 0;
        }
        else if ( op == 7 )
        {
            double now;
            std::memcpy( &now, w, 8 );
            log.clear( );
            tick_run( tm, now );
            std::uint32_t st[ 7 ];
            std::memcpy( st, &tm.clock, 8 );
            st[ 2 ] = static_cast< std::uint32_t >( tm.paused ) | ( static_cast< std::uint32_t >( tm.slow ) << 8 ) | ( static_cast< std::uint32_t >( tm.step ) << 16 );
            st[ 3 ] = static_cast< std::uint32_t >( tm.pause_count );
            std::memcpy( &st[ 4 ], &tm.dt, 4 );
            std::memcpy( &st[ 5 ], &tm.raw, 4 );
            std::memcpy( &st[ 6 ], &tm.previous, 4 );
            same = std::memcmp( st, w + 2, 28 ) == 0;
            const unsigned n = w[ 9 ];
            same = same && log.size( ) == n;
            for ( unsigned k = 0; same && k < n; ++k )
                same = log[ k ].handler == static_cast< int >( w[ 10 + 2 * k ] ) && log[ k ].dt == w[ 11 + 2 * k ];
            w += 10 + 2 * n;
            ++ticks;
            calls += static_cast< int >( n );
        }
        else
        {
            LOG_ERROR( "tick manager: unknown op %u in the golden", op );
            return false;
        }
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "tick manager: op %d (kind %u) differs from retail", ops, op );
    }
    // the final phase list
    const unsigned* p = g_golden_tick_phases;
    bool lists = tm.phases.size( ) == p[ 0 ];
    ++p;
    for ( const tick_phase_t& ph : tm.phases )
    {
        if ( !lists )
            break;
        lists = ph.name == g_golden_tick_phase_names[ p[ 0 ] ] && ph.last_dt == ( p[ 1 ] != 0 ) && ph.enabled == ( p[ 2 ] != 0 ) && ph.handlers.size( ) == p[ 3 ];
        const unsigned n = p[ 3 ];
        auto it = ph.handlers.begin( );
        for ( unsigned k = 0; lists && k < n; ++k, ++it )
            lists = handler_id( *it ) == static_cast< int >( p[ 4 + k ] );
        p += 4 + n;
    }
    if ( !lists && ++failed )
        LOG_ERROR( "tick manager: the final phase lists differ from retail" );
    if ( failed == 0 )
        LOG_SUCCESS( "tick_manager matches retail's VuTickManagerImpl (0x4F6F60 / 0x4F7050 / 0x4F7550 / 0x4F7570 / 0x4F7590 / 0x4F72F0) on %d "
                     "ops: %d ticks, %d handler calls with their dt bits, the %d phases in retail's order",
            ops, ticks, calls, static_cast< int >( tm.phases.size( ) ) );
    else
        LOG_ERROR( "tick manager FAILED: %d items differ", failed );
    return failed == 0;
}

// THE RACE'S TRACK AS THE RUNNING WORLD BUILDS IT (world/race.hxx +
// entities.hxx): VuProjectAsset/Games/HydroelectricRace loaded from Base.apf
// through race_load_tree (the callbacks c_world::load_entity_tree uses, the
// templates read from the pack), then track_build_from_tree - against retail's
// VuTrackManagerImpl (oracle case track_build, boat area: the level's "AI"
// child under a bare root, VuEntity::load + every waypoint's slot 6 + slot 7).
// The same fields verify_track_build compares (stale lanes / pointers
// skipped). One failure per waypoint, per sector, for the checkpoint list, for
// the summary, and for a refused load / build.
inline int race_track_failures( const track_manager_t& tm )
{
    int failed = 0;
    const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    if ( tm.waypoints.size( ) != g_golden_track_waypoint_count || tm.sectors.size( ) != g_golden_track_sector_count )
    {
        LOG_ERROR( "race track: %d waypoints / %d sectors, retail %d / %d", static_cast< int >( tm.waypoints.size( ) ),
            static_cast< int >( tm.sectors.size( ) ), static_cast< int >( g_golden_track_waypoint_count ), static_cast< int >( g_golden_track_sector_count ) );
        return 1 + static_cast< int >( g_golden_track_waypoint_count + g_golden_track_sector_count );
    }
    for ( std::size_t i = 0; i < tm.waypoints.size( ); ++i )
    {
        const track_waypoint_t& w = tm.waypoints[ i ];
        const unsigned* g = g_golden_track_waypoints + 20 * i;
        bool same = w.entity->m_name == g_golden_track_waypoint_names[ i ];
        for ( int k = 0; k < 3; ++k )
            same = same && bits( w.right[ k ] ) == g[ k ] && bits( w.left[ k ] ) == g[ 4 + k ];
        same = same && bits( w.p.speed_hint ) == g[ 10 ] &&
               static_cast< std::uint32_t >( w.p.start | ( w.p.checkpoint << 8 ) | ( w.p.finish << 16 ) | ( w.p.allow_recovery << 24 ) ) == g[ 11 ] &&
               w.p.show_wrong_way == ( g[ 12 ] & 0xFF ) && bits( w.p.width ) == g[ 13 ] && bits( w.p.height ) == g[ 14 ] &&
               static_cast< std::uint32_t >( w.sector_a ) == g[ 15 ] && static_cast< std::uint32_t >( w.sector_b ) == g[ 16 ] &&
               bits( w.p.branch_a ) == g[ 17 ] && bits( w.p.branch_b ) == g[ 18 ];
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "race track: waypoint %d (%s) differs from retail", static_cast< int >( i ), w.entity->m_name.c_str( ) );
    }
    const unsigned* g = g_golden_track_sectors;
    for ( std::size_t i = 0; i < tm.sectors.size( ); ++i )
    {
        const track_sector_t& s = tm.sectors[ i ];
        const std::uint64_t stale = g[ 60 ] | ( static_cast< std::uint64_t >( g[ 61 ] ) << 32 );
        bool same = true;
        for ( int k = 0; k < 60; ++k )
        {
            const int at = 4 * k;
            if ( at == 0x0C || at == 0x1C || at == 0x2C || at == 0x3C || at == 0xD4 || at == 0xD8 || at == 0xE0 || at == 0xE4 || at == 0xEC ||
                 ( stale >> k & 1 ) )
                continue;
            same = same && track_u32( s.image, at ) == g[ k ];
        }
        const unsigned nn = g[ 63 ], np = g[ 64 ];
        same = same && static_cast< std::uint32_t >( s.waypoint ) == g[ 62 ] && s.next.size( ) == nn && s.prev.size( ) == np;
        for ( unsigned k = 0; same && k < nn; ++k )
            same = static_cast< std::uint32_t >( s.next[ k ] ) == g[ 65 + k ];
        for ( unsigned k = 0; same && k < np; ++k )
            same = static_cast< std::uint32_t >( s.prev[ k ] ) == g[ 65 + nn + k ];
        if ( !same && ++failed <= 12 )
            LOG_ERROR( "race track: sector %d differs from retail", static_cast< int >( i ) );
        g += 65 + nn + np;
    }
    bool checkpoints = tm.checkpoints.size( ) == g_golden_track_checkpoint_count;
    for ( std::size_t k = 0; checkpoints && k < tm.checkpoints.size( ); ++k )
        checkpoints = static_cast< std::uint32_t >( tm.checkpoints[ k ] ) == static_cast< std::uint32_t >( g_golden_track_checkpoints[ k ] );
    if ( !checkpoints && ++failed )
        LOG_ERROR( "race track: the checkpoint list differs from retail" );
    const bool summary = static_cast< std::uint32_t >( tm.start ) == static_cast< std::uint32_t >( g_golden_track_summary[ 0 ] ) &&
                         static_cast< std::uint32_t >( tm.end ) == static_cast< std::uint32_t >( g_golden_track_summary[ 1 ] ) &&
                         bits( tm.length ) == g_golden_track_manager[ 7 ] && static_cast< std::uint32_t >( tm.state ) == g_golden_track_manager[ 12 ];
    if ( !summary && ++failed )
        LOG_ERROR( "race track: start / end / length / state differ from retail" );
    return failed;
}

// The race project's tree the way c_world loads it (race_load_tree with a
// template reader equal to c_world::template_document: the parsed
// VuTemplateAsset document when it is a map). Null when the pack is missing.
inline std::unique_ptr< entity_t > race_check_tree( c_archive& archive, std::map< std::string, value_t >& templates, const char* asset )
{
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
        return nullptr;
    std::vector< std::uint8_t > bytes;
    if ( !archive.read( asset, bytes ) )
        return nullptr;
    const value_t root = c_reader( bytes ).parse( );
    const value_t* project = find( root, "VuProject" );
    if ( !project )
        return nullptr;
    const auto read = [ &archive, &templates ]( const std::string& name ) -> const value_t*
    {
        auto it = templates.find( name );
        if ( it == templates.end( ) )
        {
            std::vector< std::uint8_t > b;
            value_t parsed{};
            if ( archive.read( ( "VuTemplateAsset/" + name ).c_str( ), b ) )
                parsed = c_reader( b ).parse( );
            it = templates.emplace( name, std::move( parsed ) ).first;
        }
        return it->second.tag == 6 ? &it->second : nullptr;
    };
    return race_load_tree( archive, asset, *project, read );
}

inline bool verify_race_track( )
{
    c_archive archive;
    std::map< std::string, value_t > templates;
    const std::unique_ptr< entity_t > tree = race_check_tree( archive, templates, "VuProjectAsset/Games/HydroelectricRace" );
    track_manager_t tm;
    if ( !tree || !track_build_from_tree( *tree, tm ) )
    {
        LOG_ERROR( "race track FAILED: HydroelectricRace %s", tree ? "built no track (refused)" : "did not load (Base.apf?)" );
        return false;
    }
    const int failed = race_track_failures( tm );
    if ( failed == 0 )
        LOG_SUCCESS( "the running world's track (HydroelectricRace's tree via race_load_tree -> track_build_from_tree) matches retail's "
                     "VuTrackManagerImpl: %d waypoints, %d sectors, %d checkpoints, bit for bit",
            static_cast< int >( tm.waypoints.size( ) ), static_cast< int >( tm.sectors.size( ) ), static_cast< int >( tm.checkpoints.size( ) ) );
    else
        LOG_ERROR( "race track FAILED: %d items differ from retail", failed );
    return failed == 0;
}

// race_track_start (world/race.hxx) against retail's 0x4D1E10 (oracle case
// race_track_start) on the running world's own track (HydroelectricRace via
// race_load_tree, as verify_race_track): per vector the return, the sector
// out (or none for a null out) and all 16 matrix words (unwritten words keep
// the sentinel on both sides). One failure per vector.
inline bool verify_race_track_start( )
{
    c_archive archive;
    std::map< std::string, value_t > templates;
    const std::unique_ptr< entity_t > tree = race_check_tree( archive, templates, "VuProjectAsset/Games/HydroelectricRace" );
    track_manager_t tm;
    if ( !tree || !track_build_from_tree( *tree, tm ) )
    {
        LOG_ERROR( "race track start FAILED: HydroelectricRace's track did not build" );
        return false;
    }
    int failed = 0, found = 0;
    for ( std::size_t k = 0; k < g_golden_race_track_start_count; ++k )
    {
        const unsigned* g = g_golden_race_track_start + 20 * k;
        float distance;
        std::memcpy( &distance, &g[ 0 ], 4 );
        std::uint32_t words[ 16 ];
        for ( auto& w : words )
            w = 0xA5A5A5A5u;
        float m[ 16 ];
        std::memcpy( m, words, 64 );
        int sector = -1;
        const bool ret = race_track_start( tm, distance, m, g[ 1 ] ? nullptr : &sector );
        std::memcpy( words, m, 64 );
        found += ret ? 1 : 0;
        const bool same = ( ret ? 1u : 0u ) == g[ 2 ] && static_cast< std::uint32_t >( ret && !g[ 1 ] ? sector : -1 ) == g[ 3 ] &&
                          std::memcmp( words, g + 4, 64 ) == 0;
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "race track start vector %d (distance 0x%08X) differs from retail: ret %d sector %d word0 0x%08X (retail %u %d 0x%08X)",
                static_cast< int >( k ), g[ 0 ], ret ? 1 : 0, sector, words[ 0 ], g[ 2 ], static_cast< int >( g[ 3 ] ), g[ 4 ] );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "race_track_start matches retail's 0x4D1E10 (VuRaceGame 0x48AF90's start down the track) on the running world's "
                     "track: %d distances (%d placed), 16 matrix words + sector each, bit for bit",
            static_cast< int >( g_golden_race_track_start_count ), found );
    else
        LOG_ERROR( "race track start FAILED %d of %d vectors", failed, static_cast< int >( g_golden_race_track_start_count ) );
    return failed == 0;
}

// race_load_boats (world/race.hxx) against retail's VuGame boat load 0x481CE0
// (oracle case game_boat_load) on hand-built GameData documents - a declared
// INPUT until the front end's GameData builder is recreated - with the real
// TrackDB or a hand-built one: per vector the boat list (count, capacity; per
// boat name, vtable, +0x14, +0x30 hash, children, the three property values
// and the property list), the game's +0x4C / +0x50 / +0x68 / +0x80 and the 14
// font names, and the hooks in call order (postLoad on which entry, the skin
// pass with which skin, each font reload). One failure per vector.
inline bool verify_game_boat_load( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    std::vector< std::uint8_t > bytes;
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) || !archive.read( "VuDBAsset/TrackDB", bytes ) )
    {
        LOG_ERROR( "game boat load FAILED: the pack's TrackDB is not readable" );
        return false;
    }
    const value_t real_trackdb = c_reader( bytes ).parse( );
    const entity_registry_t registry = build_entity_registry( { } );
    const unsigned* w = g_golden_game_boat_load_words;
    const char* const* t = g_golden_game_boat_load_texts;
    const auto parse = []( unsigned k ) {
        const unsigned char* raw = g_golden_game_boat_load_blobs[ k ];
        return c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_game_boat_load_blob_sizes[ k ] ) ).parse( );
    };
    int failed = 0, boats_total = 0, calls_total = 0;
    for ( std::size_t v = 0; v < g_golden_game_boat_load_vectors; ++v )
    {
        const value_t root = parse( w[ 0 ] );
        const value_t trackdb = w[ 1 ] == 0xFFFFFFFFu ? value_t{ } : parse( w[ 1 ] );
        const value_t& game_data = member( root, "GameData" );
        race_game_t game;
        game.laps = static_cast< std::int32_t >( w[ 2 ] );
        game.difficulty.clear( );
        const char* difficulty = t[ 0 ];
        const char* track = t[ 1 ];
        for ( int k = 0; k < 14; ++k )
            game.fonts[ k ] = t[ 2 + k ];
        const char* const* fonts_after = t + 16;
        const unsigned count = w[ 3 ], capacity = w[ 4 ], laps = w[ 5 ], championship = w[ 6 ], nboats = w[ 7 ], ncalls = w[ 8 ];
        w += 9;
        t += 30;
        std::vector< std::string > calls;
        race_load_hooks_t hooks;
        hooks.post_load = [ & ]( entity_t&, const value_t& data ) {
            const value_t& list = member( game_data, "Boats" );
            int at = -3;
            for ( std::size_t k = 0; k < list.list.size( ); ++k )
                if ( &list.list[ k ] == &data )
                    at = static_cast< int >( k );
            calls.push_back( "0 " + std::to_string( game.boats.size( ) ) + " " + std::to_string( at ) );
        };
        hooks.skin = [ & ]( entity_t& e ) {
            calls.push_back( "1 " + std::to_string( game.boats.size( ) ) + " " + static_cast< race_boat_state_t* >( e.m_state.get( ) )->skin );
        };
        hooks.font_reload = [ & ]( int offset, const std::string& name ) { calls.push_back( "2 " + std::to_string( offset ) + " " + name ); };
        bool same = race_load_boats( game, game_data, root, w[ -8 ] == 0xFFFFFFFFu ? real_trackdb : trackdb, registry, hooks );
        same = same && game.boats.size( ) == count && static_cast< unsigned >( game.capacity ) == capacity &&
               static_cast< unsigned >( game.laps ) == laps && game.championship == championship && game.difficulty == difficulty &&
               game.track == track && nboats == count;
        for ( int k = 0; k < 14; ++k )
            same = same && game.fonts[ k ] == fonts_after[ k ];
        for ( unsigned b = 0; b < nboats; ++b )
        {
            const unsigned nprops = w[ 4 ];
            if ( same && b < game.boats.size( ) )
            {
                const entity_t& e = *game.boats[ b ];
                const race_boat_state_t& s = *static_cast< const race_boat_state_t* >( e.m_state.get( ) );
                same = e.m_ctor && e.m_ctor->m_vtable == w[ 0 ] && e.m_flags == w[ 1 ] && s.hash == w[ 2 ] && e.m_children.size( ) == w[ 3 ] &&
                       nprops == 3 && e.m_name == t[ 0 ] && s.driver_type == t[ 1 ] && s.boat == t[ 2 ] && s.skin == t[ 3 ] &&
                       std::string( t[ 4 ] ) == "Driver Type" && std::string( t[ 5 ] ) == "Boat" && std::string( t[ 6 ] ) == "Skin";
            }
            w += 5;
            t += 4 + nprops;
        }
        boats_total += static_cast< int >( nboats );
        same = same && calls.size( ) == ncalls;
        for ( unsigned c = 0; c < ncalls; ++c )
        {
            std::string want = std::to_string( w[ 0 ] ) + " " + std::to_string( static_cast< int >( w[ 1 ] ) ) + " ";
            if ( w[ 0 ] == 0 )
                want += std::to_string( static_cast< int >( w[ 2 ] ) );
            else
                want += *t++;
            same = same && c < calls.size( ) && calls[ c ] == want;
            w += 3;
        }
        calls_total += static_cast< int >( ncalls );
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "game boat load vector %d differs from retail: %d boats (cap %d) laps %d track '%s' difficulty '%s' %d calls "
                       "(retail %u / %u / %d / '%s' / '%s' / %u)",
                static_cast< int >( v ), static_cast< int >( game.boats.size( ) ), game.capacity, game.laps, game.track.c_str( ),
                game.difficulty.c_str( ), static_cast< int >( calls.size( ) ), count, capacity, static_cast< int >( laps ), track, difficulty, ncalls );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "race_load_boats matches retail's VuGame boat load 0x481CE0 on %d hand-built GameData documents: %d boats "
                     "(name, hash, the three properties), list growth, laps / difficulty / track / championship, 14 fonts, %d hook calls in order",
            static_cast< int >( g_golden_game_boat_load_vectors ), boats_total, calls_total );
    else
        LOG_ERROR( "game boat load FAILED %d of %d vectors", failed, static_cast< int >( g_golden_game_boat_load_vectors ) );
    return failed == 0;
}

// race_boat_game_initialize / race_boat_game_release (world/race.hxx) against
// retail's 0x41D350 / 0x41D470 (oracle case boat_game_init) in a world of
// recording fakes: per vector every call row in order (the two tick
// registrations with the delegate's words and phase), the draw component's
// +0x1E and the instigator's +0x28 after; and the hydro tick manager's own
// state - after an initialize the boat's one handler in "Decision" and one in
// "Build" (another owner's kept), after a release none of its handlers left.
inline bool verify_boat_game_init( )
{
    const unsigned* w = g_golden_boat_game_init_words;
    const char* const* rows = g_golden_boat_game_init_rows;
    int failed = 0, total = 0;
    for ( std::size_t v = 0; v < g_golden_boat_game_init_vectors; ++v, w += 6 )
    {
        const bool init = w[ 0 ] == 0x41D350u;
        std::uint8_t draw = static_cast< std::uint8_t >( w[ 1 ] ), inst = static_cast< std::uint8_t >( w[ 2 ] );
        int boat = 0, other = 0;
        tick_manager_t tm;
        tick_manager_init( tm );
        const auto nop = []( void*, float ) {};
        tick_register( tm, { &other, nop, nullptr }, "Decision" );
        if ( !init )
        {
            tick_register( tm, { &boat, nop, nullptr }, "Decision" );
            tick_register( tm, { &boat, nop, nullptr }, "Build" );
        }
        std::vector< std::string > got;
        race_boat_init_hooks_t hooks;
        hooks.call = [ &got ]( const std::string& r ) { got.push_back( r ); };
        hooks.engine_ready = [ w ] { return w[ 2 ] != 0; };
        if ( init )
            race_boat_game_initialize( &boat, tm, { &boat, nop, nullptr }, { &boat, nop, nullptr }, draw, hooks );
        else
            race_boat_game_release( &boat, tm, draw, inst, hooks );
        bool same = draw == w[ 3 ] && inst == w[ 4 ] && got.size( ) == w[ 5 ];
        for ( unsigned k = 0; k < w[ 5 ]; ++k )
            same = same && k < got.size( ) && got[ k ] == rows[ k ];
        rows += w[ 5 ];
        total += static_cast< int >( w[ 5 ] );
        int mine = 0, others = 0, decision = 0, build = 0;
        for ( const tick_phase_t& p : tm.phases )
            for ( const tick_handler_t& h : p.handlers )
            {
                mine += h.owner == &boat ? 1 : 0;
                others += h.owner == &other ? 1 : 0;
                decision += h.owner == &boat && p.name == "Decision" ? 1 : 0;
                build += h.owner == &boat && p.name == "Build" ? 1 : 0;
            }
        same = same && others == 1 && ( init ? ( mine == 2 && decision == 1 && build == 1 ) : mine == 0 );
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "boat game init vector %d (0x%X) differs from retail: %d calls (retail %u), draw %d inst %d (retail %u %u), "
                       "boat handlers %d", static_cast< int >( v ), w[ 0 ], static_cast< int >( got.size( ) ), w[ 5 ], draw, inst, w[ 3 ], w[ 4 ], mine );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "race_boat_game_initialize / _release match retail's VuBoatEntity 0x41D350 / 0x41D470: %d vectors, %d calls in order, "
                     "the Decision / Build registrations in the tick manager", static_cast< int >( g_golden_boat_game_init_vectors ), total );
    else
        LOG_ERROR( "boat game init FAILED %d of %d vectors", failed, static_cast< int >( g_golden_boat_game_init_vectors ) );
    return failed == 0;
}

// race_boat_post_load (world/race.hxx) against retail's VuBoatEntity postLoad
// 0x41CFF0 run whole (oracle case boat_post_load; the unrecreated ctors and
// the two composed units 0x420A70 / 0x421670 recorded): per vector the call
// rows in order, what +0x220 holds (a new block of retail's size / the
// previous driver kept), and for an AI / UI driver its 0xD4 bytes on retail's
// two-fill mask (the +0x60 string field compared as its text), its pointer
// refs (the +0x60 string's heap block excepted) and the name; every class's
// block by its size and bytes (the ctors are boat driver.hxx's).
inline bool verify_boat_post_load( )
{
    const unsigned* cdb = g_golden_vujb_words + g_golden_vujb_offsets[ 0 ];   // ConstantDB (the human ctor reads it)
    const unsigned char* cdb_raw = reinterpret_cast< const unsigned char* >( cdb + 2 );
    const value_t constant_db = c_reader( std::vector< std::uint8_t >( cdb_raw, cdb_raw + cdb[ 1 ] ) ).parse( );
    const value_t doc = c_reader( std::vector< std::uint8_t >( g_golden_boat_post_load_doc,
                                                               g_golden_boat_post_load_doc + g_golden_boat_post_load_doc_size ) ).parse( );
    const unsigned* w = g_golden_boat_post_load_words;
    const char* const* t = g_golden_boat_post_load_texts;
    int failed = 0, rows_total = 0, drivers = 0;
    for ( std::size_t v = 0; v < g_golden_boat_post_load_vectors; ++v )
    {
        const unsigned length = *w++;
        const std::string type( *t++, length );
        const std::string key = *t++;
        std::vector< std::uint8_t > boat( 0x520, 0 );
        std::memcpy( boat.data( ) + 0x270, w, 8 );
        std::memcpy( boat.data( ) + 0x280, w + 2, 8 );
        w += 4;
        const unsigned held = *w++, size = *w++, calls = *w++, has = *w++;
        const value_t& entry = member( doc, key.c_str( ) );
        const value_t& driver_node = member( entry, "Driver" );
        // The previous driver the case presets at +0x220: an object of no recreated class.
        auto previous = std::make_unique< race_boat_driver_t >( );
        race_boat_driver_t* const before = previous.get( );
        std::unique_ptr< race_boat_driver_t > driver = std::move( previous );
        std::vector< std::string > got;
        race_post_load_hooks_t hooks;
        hooks.unknown_load = [ &got, &driver_node ]( race_boat_driver_t&, const value_t& c )
        { got.push_back( &c == &driver_node && c.tag != 0 ? "driver load Driver" : "driver load null" ); };
        hooks.load_sections = [ &got ] { got.push_back( "sections boat" ); };
        hooks.transform_changed = [ &got ] { got.push_back( "transform boat" ); };
        race_boat_post_load( boat.data( ), type, entry, constant_db, driver, hooks );
        int bad = got.size( ) != calls ? 1 : 0;
        for ( unsigned k = 0; k < calls; ++k )
            bad = bad ? bad : ( k < got.size( ) && got[ k ] == t[ k ] ? 0 : 2 );
        t += calls;
        rows_total += static_cast< int >( calls );
        if ( !bad && held == 1 && driver.get( ) != before )
            bad = 3;
        if ( !bad && held == 0 && ( driver.get( ) == before || !driver || driver->size != size ) )
            bad = 4;
        if ( !bad && has != ( driver && driver.get( ) != before && driver->known ? 1u : 0u ) )
            bad = 5;
        if ( has )
        {
            const unsigned bytes = *w++;
            const std::uint8_t* want = reinterpret_cast< const std::uint8_t* >( w );
            const std::uint8_t* mask = reinterpret_cast< const std::uint8_t* >( w + bytes / 4 );
            w += 2 * ( bytes / 4 );
            std::vector< image_ref_t > refs;
            const unsigned n = *w++;
            for ( unsigned k = 0; k < n; ++k, w += 4 )
                if ( w[ 0 ] != 0x60 )
                    refs.push_back( { static_cast< int >( w[ 0 ] ), w[ 1 ] == 1 ? AI_DRIVER_REF_BOAT : static_cast< int >( w[ 2 ] ),
                                      static_cast< int >( w[ 3 ] ) } );
            const std::string name = *t++;
            ++drivers;
            if ( !bad )
            {
                const std::vector< std::uint8_t >& img = driver->graph[ 0 ].image;
                if ( img.size( ) != bytes )
                    bad = 8;
                for ( int at = 0; at < static_cast< int >( bytes ) && !bad; ++at )
                    if ( mask[ at ] && !( at >= 0x60 && at < 0x78 ) && img[ static_cast< std::size_t >( at ) ] != want[ at ] )
                        bad = 0x100 + at;
                std::vector< image_ref_t > ours = driver->graph[ 0 ].refs;
                auto by_slot = []( const image_ref_t& x, const image_ref_t& y ) { return x.slot < y.slot; };
                std::sort( ours.begin( ), ours.end( ), by_slot );
                std::sort( refs.begin( ), refs.end( ), by_slot );
                bool same = ours.size( ) == refs.size( );
                for ( std::size_t k = 0; same && k < refs.size( ); ++k )
                    same = ours[ k ].slot == refs[ k ].slot && ours[ k ].object == refs[ k ].object && ours[ k ].offset == refs[ k ].offset;
                // The AI / UI pointer refs (the other classes' graphs carry method / event nodes the boat agent's
                // verify_boat_drivers checks by what they hold).
                const bool ai = driver->cls == race_driver_class_t::ai || driver->cls == race_driver_class_t::ui;
                if ( !bad && ai && !same )
                    bad = 6;
                const auto it = driver->strings.find( 0x60 );
                if ( !bad && ( it == driver->strings.end( ) ? std::string( ) : it->second ) != name )
                    bad = 7;
            }
        }
        if ( bad && ++failed <= 6 )
            LOG_ERROR( "boat post load vector %d (type '%s', %s) differs from retail: %d (1/2 calls, 3/4 +0x220, 5 recreated, 6 refs, 7 name, "
                       "8 size, 0x1xx the driver's byte)", static_cast< int >( v ), type.c_str( ), key.c_str( ), bad );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "race_boat_post_load matches retail's VuBoatEntity postLoad 0x41CFF0: %d vectors, %d call rows, %d drivers of "
                     "all five classes (bytes, AI / UI refs, name)", static_cast< int >( g_golden_boat_post_load_vectors ),
                     rows_total, drivers );
    else
        LOG_ERROR( "boat post load FAILED %d of %d vectors", failed, static_cast< int >( g_golden_boat_post_load_vectors ) );
    return failed == 0;
}

// race_begin + race_place_boats + race_start_fleet + the boat manager
// (world/start.hxx) against retail's VuGame begin 0x4829A0 run whole with
// VuRaceGame's slots 13 / 5 (oracle case race_begin) on the running world's
// track (HydroelectricRace's tree): the grid list's order after retail's
// 0x43A740 adds, the five boat lists + focus after 0x4AAF60 and after 0x4AB040
// (boat 1), every recorded call row in order, each boat's transform (the
// owner +0x08 / delegate +0xC0 pointers and the stale +0x9C / +0xAC words
// skipped), +0x4AC / +0x4B0 / +0x4CC / +0x4D4 / +0x4D8 / +0x4DC, engine +4,
// driver +0x78 / +0x7C / +0x80, game +0x44 / +0x48 and the +0x720 order.
inline bool verify_race_begin( )
{
    c_archive archive;
    std::map< std::string, value_t > templates;
    const std::unique_ptr< entity_t > tree = race_check_tree( archive, templates, "VuProjectAsset/Games/HydroelectricRace" );
    track_manager_t tm;
    if ( !tree || !track_build_from_tree( *tree, tm ) )
    {
        LOG_ERROR( "race begin FAILED: HydroelectricRace's track did not build" );
        return false;
    }
    const unsigned* w = g_golden_race_begin_words;
    const char* const* t = g_golden_race_begin_texts;
    auto doc = []( unsigned k )
    {
        const unsigned char* b = g_golden_race_begin_blobs[ k ];
        return c_reader( std::vector< std::uint8_t >( b, b + g_golden_race_begin_blob_sizes[ k ] ) ).parse( );
    };
    int failed = 0, rows_total = 0, boats_total = 0;
    for ( std::size_t v = 0; v < g_golden_race_begin_vectors; ++v )
    {
        const value_t cdb = doc( w[ 0 ] ), tdb = doc( w[ 1 ] );
        race_start_game_t game;
        game.laps = static_cast< std::int32_t >( w[ 2 ] );
        const unsigned nb = w[ 3 ], ng = w[ 4 ];
        w += 5;
        game.difficulty = *t++;
        game.root_name = *t++;
        struct img_t { std::vector< std::uint8_t > boat, xf, eng, drv; };
        std::vector< img_t > imgs( nb );
        std::vector< race_start_boat_t > boats( nb );
        for ( unsigned k = 0; k < nb; ++k, w += 5 )
        {
            img_t& i = imgs[ k ];
            i.boat.assign( 0x520, 0 );
            i.xf.assign( 0xC8, 0 );
            i.eng.assign( 0x20, 0 );
            i.drv.assign( 0xD4, 0 );
            image_put32( i.boat.data( ), 0x470, w[ 1 ] );
            image_put32( i.eng.data( ), 4, w[ 2 ] );
            image_put32( i.eng.data( ), 0xC, w[ 3 ] );
            image_put32( i.drv.data( ), 0x80, 1 );
            boats[ k ] = { i.boat.data( ), i.xf.data( ), i.eng.data( ), i.drv.data( ), boat_driver_of( w[ 0 ] ), {}, -1, static_cast< int >( w[ 4 ] ) };
            boat_driver_kind( w[ 0 ], boats[ k ].kind );
        }
        race_boat_manager_t m;
        for ( unsigned j = 0; j < ng; ++j, w += 16 )
        {
            race_grid_entry_t e;
            e.name = *t++;
            std::memcpy( e.world, w, 64 );
            race_grid_add( m, e );
        }
        int bad = 0;
        const unsigned order_n = *w++;
        bad = order_n != m.grid.size( ) ? 1 : 0;
        for ( unsigned j = 0; j < order_n; ++j, ++t )
            bad = bad ? bad : ( m.grid[ j ].name == *t ? 0 : 1 );
        for ( unsigned k = 0; k < nb; ++k )
            boat_manager_add( m.lists, boats[ k ].boat, boats[ k ].kind );
        auto lists = [ & ]( int code )
        {
            const boat_manager_list_t* l[ 5 ] = { &m.lists.all, &m.lists.racers, &m.lists.humans, &m.lists.ais, &m.lists.ghosts };
            auto name_of = [ & ]( const void* p )
            {
                for ( unsigned k = 0; k < nb; ++k )
                    if ( p == imgs[ k ].boat.data( ) )
                        return "boat" + std::to_string( k );
                return std::string( "?" );
            };
            for ( auto* list : l )
            {
                const unsigned n = *w++;
                bad = bad ? bad : ( n == list->boats.size( ) ? 0 : code );
                for ( unsigned j = 0; j < n; ++j, ++t )
                    bad = bad ? bad : ( j < list->boats.size( ) && name_of( list->boats[ j ] ) == *t ? 0 : code );
            }
            bad = bad ? bad : ( static_cast< unsigned >( m.lists.viewport ) == *w ? 0 : code + 1 );
            ++w;
        };
        lists( 2 );
        std::vector< std::string > got;
        race_start_hooks_t hooks;
        hooks.transform_changed = [ &got ]( int k ) { got.push_back( "transform boat" + std::to_string( k ) ); };
        hooks.game_initialize = [ &got ]( int k ) { got.push_back( "gameInitialize boat" + std::to_string( k ) ); };
        hooks.call = [ &got ]( const char* row ) { if ( std::strncmp( row, "GameResults", 11 ) ) got.push_back( row ); };
        hooks.add_driver = [ &got ]( int k ) { got.push_back( "addDriver aim boat" + std::to_string( k ) + " 0 0" ); };
        hooks.fleet_start = [ &got ] { got.push_back( "fleet start aim" ); };
        hooks.fleet_run = [ &got ] { got.push_back( "fleet run aim" ); };
        race_begin( game, boats, m, tm, tdb, cdb, hooks );
        const unsigned calls = *w++;
        bad = bad ? bad : ( calls == got.size( ) ? 0 : 4 );
        for ( unsigned j = 0; j < calls; ++j, ++t )
            bad = bad ? bad : ( j < got.size( ) && got[ j ] == *t ? 0 : 5 );
        rows_total += static_cast< int >( calls );
        for ( unsigned k = 0; k < nb; ++k )
        {
            const img_t& i = imgs[ k ];
            for ( int at = 0; at < 50; ++at, ++w )
                if ( at != 2 && at != 48 && at != 39 && at != 43 && !bad && load_word( i.xf.data( ), 4 * at ) != *w )
                    bad = 0x10000 + 0x100 * static_cast< int >( k ) + 4 * at;   // boat k's transform byte
            const int offs[ 4 ] = { 0x4AC, 0x4B0, 0x4CC, 0x4D4 };
            for ( int q = 0; q < 4; ++q, ++w )
                bad = bad ? bad : ( load_word( i.boat.data( ), offs[ q ] ) == *w ? 0 : 0x200 + offs[ q ] );
            bad = bad ? bad : ( static_cast< unsigned >( boats[ k ].checkpoint ) == *w ? 0 : 0x4D8 );
            ++w;
            for ( int at = 0; at < 16; ++at, ++w )
                bad = bad ? bad : ( load_word( i.boat.data( ), 0x4DC + 4 * at ) == *w ? 0 : 0x4DC );
            bad = bad ? bad : ( load_word( i.eng.data( ), 4 ) == *w ? 0 : 0x304 );
            ++w;
            bad = bad ? bad : ( load_word( i.drv.data( ), 0x78 ) == *w ? 0 : 0x378 );
            ++w;
            bad = bad ? bad : ( static_cast< unsigned >( boats[ k ].sector ) == *w ? 0 : 0x37C );
            ++w;
            bad = bad ? bad : ( load_word( i.drv.data( ), 0x80 ) == *w ? 0 : 0x380 );
            ++w;
            ++boats_total;
        }
        std::uint32_t length;
        std::memcpy( &length, &game.length, 4 );
        bad = bad ? bad : ( game.circuit == w[ 0 ] && length == w[ 1 ] ? 0 : 6 );
        w += 2;
        const unsigned on = *w++;
        bad = bad ? bad : ( on == game.order.size( ) ? 0 : 7 );
        for ( unsigned j = 0; j < on; ++j, ++w )
            bad = bad ? bad : ( j < game.order.size( ) && static_cast< unsigned >( game.order[ j ] ) == *w ? 0 : 7 );
        boat_manager_remove( m.lists, boats[ 1 ].boat );
        lists( 8 );
        if ( bad && ++failed <= 6 )
            LOG_ERROR( "race begin vector %d differs from retail: 0x%X (1 grid, 2/3 lists, 4/5 calls, 0x1kkxx transform, "
                       "0x2xx/0x3xx words, 6 game, 7 order, 8/9 removed)", static_cast< int >( v ), bad );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "race_begin / race_place_boats / race_start_fleet / boat_manager_add / _remove / race_grid_add match retail's VuGame begin 0x4829A0 (VuRaceGame "
                     "0x48AF90 / 0x4896A0, 0x4AAF60 / 0x4AB040 / 0x43A740): %d vectors, %d boats, %d call rows",
                     static_cast< int >( g_golden_race_begin_vectors ), boats_total, rows_total );
    else
        LOG_ERROR( "race begin FAILED %d of %d vectors", failed, static_cast< int >( g_golden_race_begin_vectors ) );
    return failed == 0;
}

// race_boat_draw_instance / race_boat_draw_matrix (world/session.hxx) against
// retail's VuBoatEntity draw 0x41EAE0 run whole on the normal pass (oracle case
// boat_draw; the instance draws, damage and pfx calls intercepted): per vector
// the instance drawn (model 0 / 1 / 2 or none) and, when one is, the 16 words of
// the matrix it was handed. The colour / alpha words the draw writes into the
// instance are not compared (our renderer does not take them).
inline bool verify_boat_draw( )
{
    int failed = 0, drawn = 0;
    for ( std::size_t v = 0; v < g_golden_boat_draw_count; ++v )
    {
        const unsigned* g = g_golden_boat_draw + 46 * v;
        std::vector< std::uint8_t > boat( 0x520, 0 ), xf( 0xC8, 0 );
        std::memcpy( xf.data( ) + 0x50, g, 64 );
        std::memcpy( boat.data( ) + 0x49C, g + 16, 16 );
        std::memcpy( boat.data( ) + 0x10C, g + 20, 8 );
        const int flag_at[ 4 ] = { 0x464, 0x465, 0x466, 0x46B };
        for ( int k = 0; k < 4; ++k )
            boat[ static_cast< std::size_t >( flag_at[ k ] ) ] = static_cast< std::uint8_t >( g[ 22 + k ] );
        float eye[ 3 ];
        std::memcpy( eye, g + 26, 12 );
        const int pick = race_boat_draw_instance( boat.data( ), xf.data( ), eye );
        int bad = static_cast< unsigned >( pick ) == g[ 29 ] ? 0 : 1;
        if ( !bad && pick >= 0 )
        {
            ++drawn;
            float m[ 16 ];
            race_boat_draw_matrix( boat.data( ), xf.data( ), m );
            bad = std::memcmp( m, g + 30, 64 ) == 0 ? 0 : 2;
        }
        if ( bad && ++failed <= 6 )
            LOG_ERROR( "boat draw vector %d differs from retail: %d (1 instance: ours %d retail %d, 2 matrix)", static_cast< int >( v ), bad, pick,
                       static_cast< int >( g[ 29 ] ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "race_boat_draw_instance / race_boat_draw_matrix match retail's VuBoatEntity draw 0x41EAE0: %d vectors, %d instance draws "
                     "(matrix bit for bit), the rest drawing none", static_cast< int >( g_golden_boat_draw_count ), drawn );
    else
        LOG_ERROR( "boat draw FAILED %d of %d vectors", failed, static_cast< int >( g_golden_boat_draw_count ) );
    return failed == 0;
}

// livery_pass (world/livery.hxx) against retail's skin pass 0x421490 run whole (oracle case boat_skin: the Skin
// property's SkinDB document read by retail's 0x55B560 from hand-built VUJB holding real SkinDB entries and
// synthetic ones; three instance scenes and 0-2 damage pieces over a pool of 24 material descs; the material
// cache's create 0x53A440 / release 0x53A710 intercepted): per pass, every create in order - the program and
// layout words and the FNV-1a 32 of the 0x6C8-byte desc it was handed - and each skin array's count (the creates
// land in the arrays in order). Some vectors run a second pass on the same boat (the rebuild). The releases are the
// platform cache's (not compared). `rewritten` counts the creates whose desc is not a pool desc as it was.
inline bool verify_boat_skin( )
{
    const auto fnv = [ ]( const std::uint8_t* p, std::size_t n )
    {
        std::uint32_t h = 0x811C9DC5u;
        for ( std::size_t i = 0; i < n; ++i )
            h = ( h ^ p[ i ] ) * 0x01000193u;
        return h;
    };
    std::vector< value_t > docs;
    for ( std::size_t k = 0; k < sizeof( g_golden_boat_skin_doc_sizes ) / sizeof( g_golden_boat_skin_doc_sizes[ 0 ] ); ++k )
    {
        const std::vector< std::uint8_t > bytes( g_golden_boat_skin_docs[ k ], g_golden_boat_skin_docs[ k ] + g_golden_boat_skin_doc_sizes[ k ] );
        docs.push_back( c_reader( bytes ).parse( ) );
    }
    std::vector< livery_material_t > pool( g_golden_boat_skin_pool_count );
    std::vector< std::uint32_t > own;
    for ( std::size_t k = 0; k < pool.size( ); ++k )
    {
        std::memcpy( pool[ k ].desc.data( ), g_golden_boat_skin_pool + k_livery_desc_size * k, k_livery_desc_size );
        pool[ k ].program = g_golden_boat_skin_pool_words[ 2 * k ];
        pool[ k ].layout = g_golden_boat_skin_pool_words[ 2 * k + 1 ];
        own.push_back( fnv( pool[ k ].desc.data( ), k_livery_desc_size ) );
    }
    const unsigned* w = g_golden_boat_skin_words;
    int failed = 0, passes = 0, creates = 0, rewritten = 0;
    for ( std::size_t v = 0; v < g_golden_boat_skin_vectors; ++v )
    {
        const value_t* doc = &docs[ *w++ ];
        const bool asset = *w++ != 0;
        const unsigned scene_count = *w++;
        std::vector< livery_scene_t > scenes( scene_count );
        std::vector< bool > present( scene_count, false );
        for ( unsigned s = 0; s < scene_count; ++s )
        {
            const unsigned n = *w++;
            if ( n == 0xFFFFFFFFu )
                continue;
            present[ s ] = true;
            for ( unsigned i = 0; i < n; ++i )
                scenes[ s ].push_back( pool[ *w++ ] );
        }
        const livery_scene_t* three[ 3 ];
        for ( int i = 0; i < 3; ++i )
            three[ i ] = present[ static_cast< std::size_t >( i ) ] ? &scenes[ static_cast< std::size_t >( i ) ] : nullptr;
        std::vector< const livery_scene_t* > pieces;
        for ( unsigned s = 3; s < scene_count; ++s )
            pieces.push_back( present[ s ] ? &scenes[ s ] : nullptr );
        const unsigned pass_count = *w++;
        for ( unsigned p = 0; p < pass_count; ++p )
        {
            ++passes;
            const std::string name = g_golden_boat_skin_texts[ *w++ ];
            std::vector< std::vector< livery_material_t > > skins;
            const bool ok = livery_pass( asset ? doc : nullptr, name, three, pieces, skins );
            std::vector< const livery_material_t* > made;
            for ( const auto& skin : skins )
                for ( const livery_material_t& m : skin )
                    made.push_back( &m );
            const unsigned n = *w++;
            int bad = ok ? 0 : 1;
            if ( !bad && made.size( ) != n )
                bad = 2;
            for ( unsigned i = 0; i < n; ++i, w += 3 )
            {
                ++creates;
                bool pooled = false;
                for ( std::uint32_t h : own )
                    pooled = pooled || h == w[ 2 ];
                rewritten += pooled ? 0 : 1;
                if ( bad || i >= made.size( ) )
                    continue;
                const std::uint32_t h = fnv( made[ i ]->desc.data( ), k_livery_desc_size );
                if ( made[ i ]->program != w[ 0 ] || made[ i ]->layout != w[ 1 ] || h != w[ 2 ] )
                    bad = 3;
            }
            const unsigned arrays = *w++;
            if ( !bad && arrays != skins.size( ) )
                bad = 4;
            unsigned ordinal = 0;
            for ( unsigned a = 0; a < arrays; ++a )
            {
                const unsigned count = *w++;
                if ( !bad && ( a >= skins.size( ) || skins[ a ].size( ) != count ) )
                    bad = 5;
                for ( unsigned i = 0; i < count; ++i, ++ordinal )
                    if ( *w++ != ordinal && !bad )
                        bad = 6;
            }
            if ( bad && ++failed <= 6 )
                LOG_ERROR( "boat skin vector %d pass %u ('%s') differs from retail: %d (1 failed, 2 create count ours %d retail %u, "
                           "3 a desc / program / layout, 4 / 5 arrays, 6 order)", static_cast< int >( v ), p, name.c_str( ), bad,
                           static_cast< int >( made.size( ) ), n );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "livery_pass matches retail's skin pass 0x421490 (0x521F20 x3 + 0x4119F0): %d vectors, %d passes, %d material "
                     "creates (program, layout, desc FNV; %d rewritten) and every skin array's count",
                     static_cast< int >( g_golden_boat_skin_vectors ), passes, creates, rewritten );
    else
        LOG_ERROR( "boat skin FAILED %d passes", failed );
    return failed == 0;
}

// race_project_name (core/world race.hxx) against retail's 0x4D3480 run whole (oracle case game_project_name):
// per vector the util root and the dev config's ActionGameData (retail's VUJB reader on both), the asset
// factory's doesAssetExist answered as retail was answered (from Base.apf's directory). Compared: the project
// name out, and every doesAssetExist request (type / name) in order.
inline std::vector< std::uint8_t > world_unhex( const char* hex )
{
    std::vector< std::uint8_t > out;
    for ( const char* p = hex; p[ 0 ] && p[ 1 ]; p += 2 )
    {
        const auto nib = []( char c ) { return c <= '9' ? c - '0' : ( c | 0x20 ) - 'a' + 10; };
        out.push_back( static_cast< std::uint8_t >( nib( p[ 0 ] ) * 16 + nib( p[ 1 ] ) ) );
    }
    return out;
}

inline bool verify_game_project_name( )
{
    int failed = 0, asked = 0;
    for ( std::size_t v = 0; v < g_golden_game_project_name_count; ++v )
    {
        const golden_game_project_name_t& g = g_golden_game_project_name[ v ];
        const value_t root = c_reader( world_unhex( g.m_document ) ).parse( );
        value_t dev;
        const bool has_dev = g.m_devconfig[ 0 ] != '\0';
        if ( has_dev )
            dev = c_reader( world_unhex( g.m_devconfig ) ).parse( );
        // The requests retail made, "VuProjectAsset/<name> = <answer>" joined by ';'.
        std::vector< std::string > want;
        for ( std::string s = g.m_exists; !s.empty( ); )
        {
            const std::size_t semi = s.find( ';' );
            want.push_back( s.substr( 0, semi ) );
            s = semi == std::string::npos ? std::string( ) : s.substr( semi + 1 );
        }
        std::size_t at = 0;
        bool same = true;
        const std::string name = race_project_name( root, has_dev ? &dev : nullptr, [ & ]( const std::string& type, const std::string& n )
        {
            ++asked;
            if ( at >= want.size( ) )
            {
                same = false;
                return false;
            }
            const std::string& w = want[ at++ ];
            const std::size_t eq = w.rfind( " = " );
            same = same && w.substr( 0, eq ) == type + "/" + n;
            return eq != std::string::npos && w.substr( eq + 3 ) == "1";
        } );
        if ( name != g.m_name || !same || at != want.size( ) )
        {
            if ( ++failed <= 6 )
                LOG_ERROR( "game project name '%s': ours '%s' retail '%s'%s", g.m_label, name.c_str( ), g.m_name,
                           same && at == want.size( ) ? "" : " (doesAssetExist requests differ)" );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "race_project_name matches retail's project choice 0x4D3480 (0x4D3260 / 0x4D31D0 / 0x4D32F0, 0x564AE0, 0x4D3FF0, "
                     "0x436700, 0x430420): %d vectors, %d doesAssetExist requests",
                     static_cast< int >( g_golden_game_project_name_count ), asked );
    else
        LOG_ERROR( "game project name FAILED %d of %d vectors", failed, static_cast< int >( g_golden_game_project_name_count ) );
    return failed == 0;
}

// race_game_construct (core/world race.hxx) against retail's VuGame ctor 0x480BA0 run whole (oracle case
// race_game_ctor): count, capacity, laps, championship, difficulty, track and the 14 font names.
inline bool verify_race_game_ctor( )
{
    race_game_t game;
    race_game_construct( game );
    const unsigned* w = g_golden_race_game_ctor_words;
    const char* const* t = g_golden_race_game_ctor_texts;
    int failed = 0;
    const auto one = [ &failed ]( bool same, const char* what ) {
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "race game ctor: %s differs from retail's 0x480BA0", what );
    };
    one( game.boats.size( ) == w[ 0 ], "the boat count" );
    one( static_cast< unsigned >( game.capacity ) == w[ 1 ], "the capacity +0x40" );
    one( static_cast< unsigned >( game.laps ) == w[ 2 ], "the laps +0x4C" );
    one( game.championship == w[ 3 ], "the championship +0x80" );
    one( game.difficulty == t[ 0 ], "the difficulty +0x50" );
    one( game.track == t[ 1 ], "the track +0x68" );
    for ( int k = 0; k < 14; ++k )
        one( game.fonts[ k ] == t[ 2 + k ], "a font name" );
    if ( failed == 0 )
        LOG_SUCCESS( "race_game_construct matches retail's VuGame ctor 0x480BA0 (VuRaceGame 0x489030's base): 20 values" );
    return failed == 0;
}

// race_drive_floor (core/world drive.hxx) against VuWater's surface list as
// retail keeps it (oracle case water_surface_list: each add's push_back then
// std::list::sort 0x52A670 with the predicate 0x527F50, run whole) and the
// Decision tick's floor read [[[water +0x20]] +8] +0xD8: per vector the floor
// word, bit for bit. Ties of the least height (6 of 48 vectors) and a lone
// surface are among them.
inline bool verify_water_surface_list( )
{
    int failed = 0;
    for ( std::size_t index = 0; index < g_golden_water_surface_list_count; ++index )
    {
        const unsigned* s = g_golden_water_surface_list_words + g_golden_water_surface_list_offsets[ index ];
        const unsigned n = *s++;
        water_built_t water;
        for ( unsigned k = 0; k < n; ++k )
        {
            water_words_t w{};
            w.set( 20, *s++ );
            w.set( 216, *s++ );
            water.surfaces.push_back( w );
        }
        bool unknown = false;
        const float* floor = race_drive_floor( &water, unknown );
        std::uint32_t got = 0;
        if ( floor )
            std::memcpy( &got, floor, 4 );
        if ( !floor || unknown || got != *s )
            if ( ++failed <= 4 )
                LOG_ERROR( "water surface list vector %d: our floor 0x%08X, retail's 0x%08X", static_cast< int >( index ), got, *s );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "race_drive_floor matches VuWater's surface list order (0x5284C0's push_back + std::list::sort 0x52A670 / 0x527F50) "
                     "and the Decision tick's floor read: %d vectors", static_cast< int >( g_golden_water_surface_list_count ) );
    else
        LOG_ERROR( "water surface list FAILED %d of %d vectors", failed, static_cast< int >( g_golden_water_surface_list_count ) );
    return failed == 0;
}

// boat_build_audio (core/world build.hxx) against retail's 0x4200E0 run whole
// (oracle case boat_build_audio; 0x50FBF0 and the FMOD imports intercepted):
// +0x3F8..+0x407 bit for bit, +0x484, +0x48C and the calls in order, over 200
// vectors (10 NaN probes).
struct build_audio_log_t
{
    std::vector< unsigned > got;
    unsigned ok = 0;
};

inline bool verify_boat_build_audio( )
{
    int failed = 0;
    for ( std::size_t index = 0; index < g_golden_boat_build_audio_count; ++index )
    {
        const unsigned* s = g_golden_boat_build_audio_words + g_golden_boat_build_audio_offsets[ index ];
        std::vector< std::uint8_t > boat( 0x520, 0 ), body( 0x2D4, 0 ), engine( 0x104, 0 ), solver( 0x104, 0 ), camera( 0x308, 0 );
        float dt;
        std::memcpy( &dt, s++, 4 );
        std::memcpy( body.data( ) + 0x140, s, 16 );
        s += 4;
        std::memcpy( camera.data( ) + 0x20, s, 12 );
        s += 3;
        const struct { std::uint8_t* b; int at; } in[ 11 ] = { { boat.data( ), 0x3F8 }, { boat.data( ), 0x3FC }, { boat.data( ), 0x400 },
            { boat.data( ), 0x404 }, { boat.data( ), 0x30C }, { boat.data( ), 0x470 }, { engine.data( ), 4 }, { engine.data( ), 0xC },
            { engine.data( ), 0x28 }, { solver.data( ), 0x54 }, { solver.data( ), 0x5C } };
        for ( const auto& i : in )
            std::memcpy( i.b + i.at, s++, 4 );
        const unsigned n = *s++;
        const std::string name( reinterpret_cast< const char* >( s ), n );
        s += ( n + 3 ) / 4;
        build_audio_log_t log;
        log.ok = *s++;
        boat[ 0x484 ] = static_cast< std::uint8_t >( *s++ );
        image_put32( boat.data( ), 0x48C, *s++ );
        const unsigned* after = s;
        s += 4;
        const unsigned flag_after = *s++, handle_after = *s++;
        const unsigned calls = *s++;
        std::vector< unsigned > want( s, s + 2 * calls );
        boat_build_audio_hooks_t h;
        h.user = &log;
        h.slot = []( void* u, std::uint8_t* b, const std::string& ) {
            build_audio_log_t& l = *static_cast< build_audio_log_t* >( u );
            l.got.push_back( 1 );
            l.got.push_back( 0 );
            if ( l.ok )
                image_put32( b, 0x48C, 0xE700u + static_cast< unsigned >( l.got.size( ) / 2 ) );
            return l.ok != 0;
        };
        h.start = []( void* u, std::uint32_t x ) { auto& g = static_cast< build_audio_log_t* >( u )->got; g.push_back( 2 ); g.push_back( x ); };
        h.callback = []( void* u, std::uint32_t x ) { auto& g = static_cast< build_audio_log_t* >( u )->got; g.push_back( 3 ); g.push_back( x ); };
        h.release = []( void* u, std::uint32_t x ) { auto& g = static_cast< build_audio_log_t* >( u )->got; g.push_back( 4 ); g.push_back( x ); };
        boat_build_audio( boat.data( ), body.data( ), engine.data( ), solver.data( ), camera.data( ), name, dt, h );
        const bool same = std::memcmp( boat.data( ) + 0x3F8, after, 16 ) == 0 && boat[ 0x484 ] == flag_after && load_word( boat.data( ), 0x48C ) == handle_after &&
                          log.got == want;
        if ( !same && ++failed <= 4 )
            LOG_ERROR( "boat build audio vector %d differs from retail's 0x4200E0 (+0x3F8 0x%08X / 0x%08X, +0x404 0x%08X / 0x%08X, %d / %d calls)",
                       static_cast< int >( index ), load_word( boat.data( ), 0x3F8 ), after[ 0 ], load_word( boat.data( ), 0x404 ), after[ 3 ],
                       static_cast< int >( log.got.size( ) / 2 ), static_cast< int >( calls ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "boat_build_audio matches the Build tick's 0x4200E0 (0x50FBF0 / FMOD hooked): %d vectors",
                     static_cast< int >( g_golden_boat_build_audio_count ) );
    else
        LOG_ERROR( "boat build audio FAILED %d of %d vectors", failed, static_cast< int >( g_golden_boat_build_audio_count ) );
    return failed == 0;
}

// entity_set_world (world/race.hxx) = transform_set_world (world/transform.hxx)
// + the child walk + the watcher, against retail's 0x5344A0 with 0x534830 run
// whole (oracle case transform_set_world; 0x534C00 and the watcher's vt+8
// intercepted): per vector, half under a parent (its transform's world the
// hand-built parent's), the component's local +0x10, world +0x50, rotation
// +0x90 and world euler +0xA0 (lanes 0 - 2; each fourth lane is a stack word
// retail never writes) bit for bit, and the watcher fired iff notify (its low
// byte - `cmp byte`) and a watcher.
inline bool verify_transform_set_world( )
{
    int failed = 0, parents = 0, fired = 0;
    for ( std::size_t index = 0; index < g_golden_transform_set_world_count; ++index )
    {
        const unsigned* s = g_golden_transform_set_world_words + g_golden_transform_set_world_offsets[ index ];
        const bool has_parent = *s++ != 0;
        float matrix[ 16 ], parent_world[ 16 ];
        std::memcpy( matrix, s, 64 );
        s += 16;
        if ( has_parent )
        {
            std::memcpy( parent_world, s, 64 );
            s += 16;
        }
        const unsigned notify = *s++, watcher = *s++;
        const unsigned* after = s;
        s += 40;
        const unsigned ncalls = *s++;
        int want_fired = 0;
        for ( unsigned k = 0; k < ncalls; ++k )
            want_fired += s[ 3 * k ] == 2 ? 1 : 0;
        entity_t parent, owner;
        auto component = []( entity_t& e ) {
            auto st = std::make_shared< transform_state_t >( );
            std::memset( &st->c, 0x77, sizeof( st->c ) );
            e.m_components.push_back( entity_component_t{ "VuTransformComponent", k_transform_component_vtable, st } );
            return st;
        };
        auto ps = component( parent );
        auto os = component( owner );
        if ( has_parent )
        {
            std::memcpy( ps->c.world, parent_world, 64 );
            owner.m_parent = &parent;
            ++parents;
        }
        int got_fired = 0;
        const std::function< void( ) > hook = [ &got_fired ] { ++got_fired; };
        const bool ran = entity_set_world( owner, matrix, ( notify & 0xFFu ) != 0, watcher ? hook : std::function< void( ) >( ) );
        fired += got_fired;
        const transform_component_t& c = os->c;
        int bad = -1;
        for ( int w = 0; w < 40 && ran; ++w )
        {
            if ( w == 35 || w == 39 )
                continue;
            const float* f = w < 16 ? &c.local[ w ] : w < 32 ? &c.world[ w - 16 ] : w < 36 ? &c.rotation[ w - 32 ] : &c.world_rotation[ w - 36 ];
            if ( std::memcmp( f, &after[ w ], 4 ) != 0 )
            {
                bad = w;
                break;
            }
        }
        if ( !ran || bad >= 0 || got_fired != want_fired )
            if ( ++failed <= 4 )
                LOG_ERROR( "transform set world vector %d (%s): %s word +0x%X (watcher %d / %d)", static_cast< int >( index ),
                           has_parent ? "parent" : "no parent", ran ? "differs at" : "did not run", 0x10 + 4 * bad, got_fired, want_fired );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "entity_set_world matches VuTransformComponent's setWorldTransform 0x5344A0 + 0x534830 over %d vectors (%d under a parent, "
                     "%d watcher calls): local, world, both eulers bit for bit", static_cast< int >( g_golden_transform_set_world_count ), parents, fired );
    else
        LOG_ERROR( "transform set world FAILED %d of %d vectors", failed, static_cast< int >( g_golden_transform_set_world_count ) );
    return failed == 0;
}

// world_synchronize_motion_states (world/dynamics.hxx) against retail's
// synchronizeMotionStates 0x600B00 + 0x600600 run whole (case
// sync_motion_states; the motion states' setWorldTransform intercepted): per
// vector the bodies whose motion state is called, in order, and the 16
// transform words each gets, bit for bit - both the synchronize-all and the
// non-static paths, every flag / state / type gate, the Taylor and sinf
// branches and the angular clamp.
struct sync_motion_log_t
{
    const solver_rigid_t* first = nullptr;
    const std::vector< unsigned >* motion = nullptr;
    std::vector< unsigned > got;
};

inline bool verify_sync_motion_states( )
{
    int failed = 0, calls = 0;
    static solver_rigid_t bodies[ 8 ];
    for ( std::size_t index = 0; index < g_golden_sync_motion_states_count; ++index )
    {
        const unsigned* s = g_golden_sync_motion_states_words + g_golden_sync_motion_states_offsets[ index ];
        auto w = std::make_unique< world_t >( );
        std::memcpy( &w->local_time, s++, 4 );
        const bool all = *s++ != 0;
        const unsigned n = *s++, ns = *s++;
        std::vector< unsigned > nonstatic( s, s + ns );
        s += ns;
        std::vector< unsigned > motion( n );
        for ( unsigned i = 0; i < n; ++i )
        {
            solver_rigid_t& b = bodies[ i ];
            std::memset( &b, 0, sizeof( b ) );
            b.collision_flags = static_cast< int >( *s++ );
            b.activation_state = static_cast< int >( *s++ );
            b.internal_type = static_cast< int >( *s++ );
            std::memcpy( &b.hit_fraction, s++, 4 );
            motion[ i ] = *s++;
            std::memcpy( b.interpolation_basis, s, 48 );
            std::memcpy( b.interpolation_origin, s + 12, 16 );
            std::memcpy( b.interpolation_linear, s + 16, 16 );
            std::memcpy( b.interpolation_angular, s + 20, 16 );
            s += 24;
            w->objects.push_back( &b );
        }
        for ( unsigned i : nonstatic )
            w->bodies.push_back( &bodies[ i ] );
        const unsigned ncalls = *s++;
        std::vector< unsigned > want( s, s + 17 * ncalls );
        sync_motion_log_t log;
        log.first = bodies;
        log.motion = &motion;
        world_synchronize_motion_states( *w, all,
            []( void* u, const solver_rigid_t& b ) {
                const sync_motion_log_t& l = *static_cast< const sync_motion_log_t* >( u );
                return ( *l.motion )[ static_cast< std::size_t >( &b - l.first ) ] != 0;
            },
            []( void* u, solver_rigid_t& b, const float* basis, const float* origin ) {
                sync_motion_log_t& l = *static_cast< sync_motion_log_t* >( u );
                l.got.push_back( static_cast< unsigned >( &b - l.first ) );
                unsigned words[ 16 ];
                std::memcpy( words, basis, 48 );
                std::memcpy( words + 12, origin, 16 );
                l.got.insert( l.got.end( ), words, words + 16 );
            },
            &log );
        calls += static_cast< int >( ncalls );
        if ( log.got != want )
        {
            int bad = -1;
            for ( std::size_t k = 0; k < want.size( ) && k < log.got.size( ); ++k )
                if ( want[ k ] != log.got[ k ] )
                {
                    bad = static_cast< int >( k );
                    break;
                }
            if ( ++failed <= 4 )
                LOG_ERROR( "sync motion states vector %d: %d / %d calls, first difference at word %d", static_cast< int >( index ),
                           static_cast< int >( log.got.size( ) / 17 ), static_cast< int >( ncalls ), bad );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "world_synchronize_motion_states matches btDiscreteDynamicsWorld::synchronizeMotionStates 0x600B00 + 0x600600: %d "
                     "vectors, %d setWorldTransform calls, the transforms bit for bit", static_cast< int >( g_golden_sync_motion_states_count ), calls );
    else
        LOG_ERROR( "sync motion states FAILED %d of %d vectors", failed, static_cast< int >( g_golden_sync_motion_states_count ) );
    return failed == 0;
}

// boat_motion_set (world/dynamics.hxx) against retail's VuBoatEntity
// setWorldTransform 0x41E460 run whole with 0x534830 (case boat_motion_set;
// 0x534C00 intercepted - called once with 0 on the component): the component's
// local, world and both eulers' lanes 0 - 2 bit for bit over 64 vectors (8 of
// them a non-orthonormal basis with odd w lanes), and the walk's call.
inline bool verify_boat_motion_set( )
{
    int failed = 0;
    for ( std::size_t index = 0; index < g_golden_boat_motion_set_count; ++index )
    {
        const unsigned* s = g_golden_boat_motion_set_words + g_golden_boat_motion_set_offsets[ index ];
        float basis[ 12 ], origin[ 4 ];
        std::memcpy( basis, s, 48 );
        std::memcpy( origin, s + 12, 16 );
        s += 16;
        std::vector< std::uint8_t > boat( 0x520, 0 ), transform( 0xC0, 0x77 );
        std::memcpy( boat.data( ) + 0x290, s, 12 );
        s += 3;
        const unsigned* after = s;
        s += 40;
        const unsigned ncalls = *s++;
        const bool walk_ok = ncalls == 1 && s[ 0 ] == 1 && s[ 1 ] == 0;
        const bool ran = boat_motion_set( boat.data( ), transform.data( ), false, basis, origin );
        int bad = -1;
        for ( int w = 0; w < 40 && ran; ++w )
            if ( w != 35 && w != 39 && std::memcmp( transform.data( ) + 0x10 + 4 * w, &after[ w ], 4 ) != 0 )
            {
                bad = w;
                break;
            }
        if ( !ran || bad >= 0 || !walk_ok )
            if ( ++failed <= 4 )
                LOG_ERROR( "boat motion set vector %d: %s +0x%X (walk %s)", static_cast< int >( index ), ran ? "differs at" : "did not run",
                           0x10 + 4 * bad, walk_ok ? "0x534C00(0)" : "NOT as recreated" );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "boat_motion_set matches VuBoatEntity's setWorldTransform 0x41E460 + 0x534830 over %d vectors: local, world, both "
                     "eulers bit for bit, 0x534C00(0)", static_cast< int >( g_golden_boat_motion_set_count ) );
    else
        LOG_ERROR( "boat motion set FAILED %d of %d vectors", failed, static_cast< int >( g_golden_boat_motion_set_count ) );
    return failed == 0;
}

// water_surface_under (world/query.hxx) against retail's 0x52ACB0 run whole
// (case water_surface_under): 160 hand-built surface trees (1 - 7 surfaces,
// rotated / arbitrary inverses, shrunk node rects, points inside / on the
// edge / outside, below the floor, a -1e30 height), the surface found (or
// none) per vector. Each surface's 'above' is set to -FLT_MAX here: 0x52ACB0
// has no above test, so it must not matter.
inline bool verify_water_surface_under( )
{
    int failed = 0, hits = 0;
    for ( std::size_t index = 0; index < g_golden_water_surface_under_count; ++index )
    {
        const unsigned* s = g_golden_water_surface_under_words + g_golden_water_surface_under_offsets[ index ];
        float p[ 4 ];
        std::memcpy( p, s, 16 );
        s += 4;
        water_t w;
        w.skip_waves = true;
        const unsigned ns = *s++;
        for ( unsigned k = 0; k < ns; ++k )
        {
            water_surface_t sf{};
            std::memcpy( &sf.height, s, 4 );
            std::memcpy( &sf.below, s + 1, 4 );
            std::memcpy( sf.inverse, s + 2, 64 );
            std::memcpy( &sf.half_x, s + 18, 4 );
            std::memcpy( &sf.half_y, s + 19, 4 );
            sf.above = -3.4028235e38f;
            sf.wave_root = -1;
            s += 20;
            w.surfaces.push_back( sf );
        }
        const unsigned nn = *s++;
        for ( unsigned k = 0; k < nn; ++k )
        {
            water_node_t n{};
            std::memcpy( n.rect, s, 16 );
            n.first = static_cast< int >( s[ 4 ] );
            n.second = static_cast< int >( s[ 5 ] );
            s += 6;
            w.nodes.push_back( n );
        }
        w.surface_root = 0;
        const int want = static_cast< int >( *s );
        const int got = water_surface_under( w, p, water_bits( 0xFEFFFFFFu ) );
        hits += want >= 0 ? 1 : 0;
        if ( got != want && ++failed <= 4 )
            LOG_ERROR( "water surface under vector %d: ours %d, retail %d", static_cast< int >( index ), got, want );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "water_surface_under matches VuWater's 0x52ACB0 over %d trees (%d with a surface found)",
                     static_cast< int >( g_golden_water_surface_under_count ), hits );
    else
        LOG_ERROR( "water surface under FAILED %d of %d", failed, static_cast< int >( g_golden_water_surface_under_count ) );
    return failed == 0;
}

// The level's static collision bodies (world/bodies.hxx, engine entity/rigid.hxx) against retail's VuPropEntity
// create + VuEntity::load + gameInitialize 0x546910 (oracle case prop_rigid_bodies) on every VuPropEntity of
// VuTemplateAsset/Levels/Hydroelectric, loaded here through hydro's tree from Base.apf, its collision meshes from
// the pack (meshes.hxx). Compared: the component's 13 load words + Initially Active, its +0xD0 / +0xD4 / +0xD8
// after, whether a body was built, the shape's type / margin / local aabb, the body's 0x2D4 bytes (pointer words
// masked), whether it was drawn / added, the prop's Initially Visible. The surface hook answers as the case's
// fake did (1 + the order a name was first asked); the VuRigidBody id seed is the body's own (the global 0x759030
// is restored per call in the emulator).
inline bool verify_prop_rigid_bodies( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    std::vector< std::uint8_t > bytes;
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) || !archive.read( "VuTemplateAsset/Levels/Hydroelectric", bytes ) )
    {
        LOG_ERROR( "verify_prop_rigid_bodies: Base.apf / the Hydroelectric template not readable" );
        return false;
    }
    static std::vector< value_t > documents;
    documents.push_back( c_reader( bytes ).parse( ) );
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    world.m_template = []( const std::string& ) -> const value_t* { return nullptr; };
    world.m_components_for = entity_components_scripted;
    world.m_component_load = entity_load_component;
    world.m_component_post = entity_post_load_component;
    world.m_component_mark = entity_mark_component;
    world.m_state_for = entity_state_for;
    world.m_properties_load = entity_load_properties;
    world.m_properties_mark = entity_mark_properties;
    std::unique_ptr< entity_t > tree = entity_load_project( member( documents.back( ), "VuProject" ), "Data/Projects/Check.vuprj", world );
    boat_mesh_library_t lib;
    lib.read_user = &archive;
    lib.read = []( void* u, const std::string& key, std::vector< std::uint8_t >& blob ) { return static_cast< c_archive* >( u )->read( key.c_str( ), blob ); };
    bodies_hooks_t hooks;
    hooks.surface = []( void*, const std::string& n ) -> std::uint8_t {
        for ( std::size_t k = 0; k < g_golden_prop_bodies_surface_count; ++k )
            if ( n == g_golden_prop_bodies_surfaces[ k ] )
                return static_cast< std::uint8_t >( 1 + k );
        return 0xEE;
    };
    hooks.gravity = []( void*, float* g ) {
        const std::uint32_t z = 0xC19CE560u;
        g[ 0 ] = g[ 1 ] = 0.0f;
        std::memcpy( &g[ 2 ], &z, 4 );
    };
    int failed = 0, bodies = 0, words = 0, contacts = 0;
    for ( std::size_t k = 0; k < g_golden_prop_bodies_count && tree; ++k )
    {
        const golden_prop_body_t& g = g_golden_prop_bodies[ k ];
        const unsigned* w = g_golden_prop_bodies_words + g.m_at;
        entity_t* e = tree.get( );
        std::string path = g.m_path;
        std::size_t at = 0;
        while ( e && at <= path.size( ) )
        {
            const std::size_t slash = path.find( '/', at );
            const std::string part = path.substr( at, slash == std::string::npos ? std::string::npos : slash - at );
            entity_t* next = nullptr;
            for ( auto& c : e->m_children )
                if ( c->m_name == part )
                    next = c.get( );
            e = next;
            if ( slash == std::string::npos )
                break;
            at = slash + 1;
        }
        rigid_component_state_t* rs = nullptr;
        if ( e )
            for ( entity_component_t& c : e->m_components )
                if ( c.m_vtable == k_rigid_body_component_vtable )
                    rs = static_cast< rigid_component_state_t* >( c.m_state.get( ) );
        transform_component_t* t = e ? entity_transform( *e ) : nullptr;
        if ( !e || !rs || !t || !e->m_state )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "prop bodies: '%s' not found as a VuPropEntity with its components in hydro's tree", g.m_path );
            continue;
        }
        rigid_component_t& c = rs->value;
        auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
        const std::uint32_t loaded[ 14 ] = { static_cast< std::uint32_t >( c.collision_type ), bits( c.radius ), bits( c.size[ 0 ] ), bits( c.size[ 1 ] ),
            bits( c.size[ 2 ] ), bits( c.height ), bits( c.offset[ 0 ] ), bits( c.offset[ 1 ] ), bits( c.offset[ 2 ] ), bits( c.linear_damping ),
            bits( c.angular_damping ), bits( c.linear_sleep ), bits( c.angular_sleep ), c.initially_active };
        int bad = -1;
        for ( int i = 0; i < 14 && bad < 0; ++i )
            if ( loaded[ i ] != w[ i ] )
                bad = i;
        if ( c.surface != g.m_surface )
            bad = 100;
        const std::uint8_t visible = static_cast< prop_entity_state_t* >( e->m_state.get( ) )->visible;
        level_body_t b;
        std::string why;
        std::uint32_t id = 0;
        const unsigned* p = w + 18;
        if ( w[ 17 ] )
        {
            const unsigned* body_words = p + 8 + 181;
            id = body_words[ 0x210 / 4 ];
        }
        const collision_mesh_asset_t* asset = nullptr;
        if ( !c.mesh_asset.empty( ) && boat_mesh_exists( &lib, "VuCollisionMeshAsset", c.mesh_asset ) )
            asset = lib.collision_of( boat_mesh_create( &lib, "VuCollisionMeshAsset", c.mesh_asset ) );
        if ( ( asset ? c.mesh_asset : std::string( ) ) != g.m_mesh && bad < 0 )
            bad = 101;
        int drawn = 0, added = 0;
        const bool ok = bodies_prop_game_initialize( b, c, visible, asset, t->local, t->scale, id, hooks, [ & ] { ++drawn; },
                                                     [ & ]( level_body_t& ) { ++added; return true; }, why );
        if ( !ok && bad < 0 )
            bad = 102;
        const std::uint32_t after[ 3 ] = { c.flags_d0, static_cast< std::uint32_t >( c.group ) | static_cast< std::uint32_t >( c.mask ) << 16, c.flags_d8 };
        for ( int i = 0; i < 3 && bad < 0; ++i )
            if ( after[ i ] != w[ 14 + i ] )
                bad = 14 + i;
        const bool built = b.body.body.internal_type == 2;
        if ( bad < 0 && built != ( w[ 17 ] != 0 ) )
            bad = 17;
        if ( w[ 17 ] && bad < 0 )
        {
            ++bodies;
            std::uint32_t shape[ 8 ];
            if ( c.collision_type == 7 )
            {
                shape[ 0 ] = static_cast< std::uint32_t >( b.concave.type );
                shape[ 1 ] = bits( b.concave.margin );
                for ( int i = 0; i < 3; ++i )
                {
                    shape[ 2 + i ] = bits( b.concave.local_aabb_min[ i ] );
                    shape[ 5 + i ] = bits( b.concave.local_aabb_max[ i ] );
                }
            }
            else
            {
                shape[ 0 ] = static_cast< std::uint32_t >( b.convex.type );
                shape[ 1 ] = bits( b.convex.margin );
                for ( int i = 0; i < 3; ++i )
                {
                    shape[ 2 + i ] = bits( b.convex.local_aabb_min[ i ] );
                    shape[ 5 + i ] = bits( b.convex.local_aabb_max[ i ] );
                }
            }
            for ( int i = 0; i < 8 && bad < 0; ++i )
                if ( shape[ i ] != p[ i ] )
                    bad = 200 + i;
            // The contact-added callback's collision mesh (world/drive.hxx race_contact_level_mesh) against retail's
            // shape +8 (1 null, 2 the asset) with the body's +0xD4 = the shape.
            const collision_mesh_asset_t* user = race_contact_level_mesh( b );
            const unsigned char want_user = g_golden_prop_bodies_shape_user[ k ];
            if ( bad < 0 && ( g_golden_prop_bodies_d4_shape[ k ] != 1 || !( want_user == 1 ? user == nullptr : want_user == 2 && user && user == asset ) ) )
                bad = 103;
            ++contacts;
            const unsigned* mask = p + 8;
            const unsigned* want = p + 8 + 181;
            for ( int i = 0; i < 181 && bad < 0; ++i )
            {
                ++words;
                if ( !mask[ i ] && body_word( b.body, 4 * i ) != want[ i ] )
                {
                    bad = 1000 + 4 * i;
                    if ( failed < 8 )
                        LOG_ERROR( "prop bodies: '%s' body +0x%X = 0x%08X, retail 0x%08X", g.m_path, 4 * i, body_word( b.body, 4 * i ), want[ i ] );
                }
            }
            p += 8 + 2 * 181;
        }
        if ( bad < 0 && ( added != static_cast< int >( p[ 0 ] ) || drawn != static_cast< int >( p[ 1 ] ) || visible != p[ 2 ] ) )
            bad = 300;
        if ( bad >= 0 && ++failed <= 8 )
            LOG_ERROR( "prop bodies: '%s' differs at item %d (%s)", g.m_path, bad, why.c_str( ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuPropEntity gameInitialize 0x546910 / createRigidBody 0x544100 match retail on all %d Hydroelectric props (%d bodies: "
                     "the component loads, shape aabbs and %d body words bit for bit; the contact callback's shape +8 mesh on %d)",
                     static_cast< int >( g_golden_prop_bodies_count ), bodies, words, contacts );
    else
        LOG_ERROR( "prop rigid bodies FAILED on %d of %d props", failed, static_cast< int >( g_golden_prop_bodies_count ) );
    return failed == 0;
}

// The race's hull wake composition (world/drive.hxx race_hull_wake_apply: 0x417D80 after 0x417F80 - the +0x1EC
// dispatch, the hull's slot words, the plain wake's solver +0x30 and +0x34 array writes) against retail's own 0x417D80
// run in the water agent's case water_wake (its golden g_golden_wwk_*: 0x417F80 handed the planned records): two hull
// images (+0x1D0 the case's descriptors, +0x1EC 0 / 1), a solver image and its 8-byte array each, on Hydroelectric's
// built water, the Water tick 0x529E30 between frames. Compared per frame: each hull's four slot words (as wave
// indices) and the solver's +0x30 / array [0] / [1].
inline std::unique_ptr< entity_t > water_build_document_tree( );   // checks/water.hxx (included after this file)
inline bool verify_race_hull_wake( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "race hull wake: the document did not load" );
        return false;
    }
    water_built_t b{};
    water_build_world( *tree, b, []( entity_t& e ) { return entity_transform( e ); } );
    int failed = 0, compared = 0;
    auto cmp = [ & ]( const char* what, int a, int c, std::uint32_t ours, std::uint32_t theirs ) {
        ++compared;
        if ( ours != theirs && ++failed <= 12 )
            LOG_ERROR( "race hull wake %s %d / %d: ours 0x%08X retail 0x%08X", what, a, c, ours, theirs );
    };
    auto ix = [ & ]( std::uint32_t word ) { return static_cast< std::uint32_t >( race_wave_of( &b, word ) ); };
    const unsigned* s = g_golden_wwk_words;
    const unsigned frames = *s++;
    ++s;   // the built wave count
    const unsigned n0 = *s++, n1 = *s++;
    s += n0 + n1;   // the type chains
    std::uint8_t hull[ 2 ][ 0x200 ] = { }, solver[ 2 ][ 0x80 ] = { }, array[ 2 ][ 8 ] = { };
    const float desc[ 2 ][ 3 ] = { { 0.35f, 0.8f, 1.25f }, { 0.2f, 0.6f, 0.9f } };
    for ( int k = 0; k < 2; ++k )
    {
        std::memcpy( hull[ k ] + 0x1D0, desc[ k ], 12 );
        hull[ k ][ 0x1EC ] = static_cast< std::uint8_t >( k );
    }
    water_built_hooks_t hooks{};
    hooks.render = []( void* ) {};
    const float dt = 1.0f / 30.0f;
    for ( unsigned f = 0; f < frames; ++f )
    {
        const int fi = static_cast< int >( f );
        for ( int k = 0; k < 2; ++k )
        {
            std::string why;
            if ( !race_hull_wake_apply( b, hull[ k ], solver[ k ], array[ k ], s + 13 * k, why ) )
                cmp( why.c_str( ), fi, k, 0u, 1u );
        }
        s += 26;
        water_built_tick( b, dt, hooks );
        for ( int k = 0; k < 2; ++k )
            for ( int j = 0; j < 4; ++j )
                cmp( "slot", fi, 4 * k + j, ix( load_word( hull[ k ], 0x1DC + 4 * j ) ), *s++ );
        for ( int k = 0; k < 2; ++k )
        {
            cmp( "solver +0x30", fi, k, load_word( solver[ k ], 0x30 ), *s++ );
            cmp( "solver array [0]", fi, k, ix( load_word( array[ k ], 0 ) ), *s++ );
            cmp( "solver array [1]", fi, k, ix( load_word( array[ k ], 4 ) ), *s++ );
        }
        // the rest of the frame (the water agent's check compares it): dtors, listed wakes and words, the list, the pool, entries
        const unsigned nd = *s++;
        s += nd;
        const unsigned nl = *s++;
        s += nl * ( 1 + 48 );
        const unsigned n_list = *s++;
        s += 2 * n_list;
        s += 2;
        for ( unsigned i = 0; i < nl; ++i )
        {
            const unsigned n = *s++;
            s += 2 * n;
        }
    }
    cmp( "golden consumed", 0, 0, s == g_golden_wwk_words + g_golden_wwk_size ? 1u : 0u, 1u );
    if ( failed == 0 )
        LOG_SUCCESS( "The race's hull wake composition (0x417D80 after 0x417F80: the +0x1EC dispatch, the hull's slot words, the "
                     "solver +0x30 / +0x34 writes) matches retail on Hydroelectric's built water: %u frames, %d values",
                     frames, compared );
    else
        LOG_ERROR( "race hull wake FAILED %d of %d values", failed, compared );
    return failed == 0;
}

// The gfx-settings blend (world/blend.hxx gfx_blend: 0x52F690 with 0x52F7E0 / 0x52FFE0 / 0x530450 and the zone weights
// 0x5D2380 / 0x5D2850) against retail's 0x52F690 (case gfx_blend): per vector the blender's record, 0..4 zones (their
// kind from the vtable, transform rows / origin / scale, Always Active, times, timer, inner radii, record) and the
// position; compared: the 22 words written.
inline bool verify_gfx_blend( )
{
    const unsigned* s = g_golden_gfx_blend_words;
    int failed = 0, words = 0, blended = 0;
    for ( std::size_t v = 0; v < g_golden_gfx_blend_count; ++v )
    {
        gfx_blender_t b;
        std::memcpy( b.record, s, 88 );
        s += 22;
        float position[ 4 ];
        std::memcpy( position, s, 16 );
        s += 4;
        const unsigned n = *s++;
        std::vector< gfx_zone_t > zones( n );
        std::vector< transform_component_t > transforms( n );
        for ( unsigned k = 0; k < n; ++k )
        {
            gfx_zone_t& z = zones[ k ];
            transform_component_t& t = transforms[ k ];
            transform_construct( t );
            z.kind = s[ 0 ] == 0x752F4Cu ? gfx_zone_kind_t::elliptical : gfx_zone_kind_t::rectangular;
            std::memcpy( t.world, s + 1, 32 );
            std::memcpy( t.world + 12, s + 9, 12 );
            std::memcpy( t.scale, s + 12, 8 );
            z.transform = &t;
            z.always_active = static_cast< std::uint8_t >( s[ 14 ] );
            z.ramp_up = gfx_f( s[ 15 ] );
            z.duration = gfx_f( s[ 16 ] );
            z.ramp_down = gfx_f( s[ 17 ] );
            z.timer = gfx_f( s[ 18 ] );
            z.inner[ 0 ] = gfx_f( s[ 19 ] );
            z.inner[ 1 ] = gfx_f( s[ 20 ] );
            std::memcpy( z.record, s + 21, 88 );
            s += 43;
            b.zones.push_back( &z );
        }
        std::uint32_t out[ 22 ];
        gfx_blend( b, position, out );
        bool same_as_base = true;
        for ( int k = 0; k < 22; ++k )
        {
            ++words;
            same_as_base = same_as_base && s[ k ] == b.record[ k ];
            if ( out[ k ] != s[ k ] && ++failed <= 8 )
                LOG_ERROR( "gfx blend vector %d word %d: ours 0x%08X retail 0x%08X", static_cast< int >( v ), k, out[ k ], s[ k ] );
        }
        blended += same_as_base ? 0 : 1;
        s += 22;
    }
    if ( s != g_golden_gfx_blend_words + g_golden_gfx_blend_size )
    {
        LOG_ERROR( "gfx blend: the golden stream was not consumed exactly" );
        ++failed;
    }
    if ( failed == 0 )
        LOG_SUCCESS( "The gfx-settings blend 0x52F690 (0x52F7E0 / 0x52FFE0 / 0x530450, weights 0x5D2380 / 0x5D2850) matches retail: %d vectors "
                     "(%d with a zone blended in), %d words bit for bit",
                     static_cast< int >( g_golden_gfx_blend_count ), blended, words );
    else
        LOG_ERROR( "gfx blend FAILED %d of %d words", failed, words );
    return failed == 0;
}

// The trigger manager's instigators (trigger/volume.hxx: 0x531F50 trigger_instigator_initialize, 0x536460's step
// trigger_instigators_step, 0x536350 trigger_instigator_update, 0x5362A0 trigger_instigator_release) against retail
// (case trigger_instigators): per vector 1..6 components (world, mask, offset, radius, registered) and a sequence of
// registers / ticks (new worlds and radii) / updates / releases; compared after every op: the record count, each
// record's 13 words (component / transform as indices, the two never-read stack words 0) and each component's +0x28.
inline bool verify_trigger_instigators( )
{
    const unsigned* s = g_golden_trigger_instigators_words;
    int failed = 0, words = 0, ops = 0;
    for ( std::size_t v = 0; v < g_golden_trigger_instigators_count; ++v )
    {
        const unsigned n = *s++;
        std::vector< std::vector< std::uint8_t > > comps( n, std::vector< std::uint8_t >( 0x40, 0 ) ), transforms( n, std::vector< std::uint8_t >( 0x100, 0 ) );
        for ( unsigned k = 0; k < n; ++k )
        {
            std::memcpy( transforms[ k ].data( ) + 0x50, s, 64 );
            std::memcpy( comps[ k ].data( ) + 0x10, s + 16, 16 );
            std::memcpy( comps[ k ].data( ) + 0x24, s + 20, 4 );
            comps[ k ][ 0x28 ] = static_cast< std::uint8_t >( s[ 21 ] );
            s += 22;
        }
        trigger_manager_t m;
        const unsigned count = *s++;
        for ( unsigned o = 0; o < count; ++o, ++ops )
        {
            const unsigned kind = s[ 0 ], k = s[ 1 ];
            s += 2;
            if ( kind == 0 )
                trigger_instigator_initialize( m, comps[ k ].data( ), transforms[ k ].data( ), static_cast< int >( k ) );
            else if ( kind == 1 )
            {
                ++s;   // dt: 0x536460 reads no dt in its instigator step
                for ( unsigned c = 0; c < n; ++c, s += 17 )
                {
                    std::memcpy( transforms[ c ].data( ) + 0x50, s, 64 );
                    std::memcpy( comps[ c ].data( ) + 0x24, s + 16, 4 );
                }
                trigger_instigators_step( m );
            }
            else if ( kind == 2 )
            {
                std::memcpy( transforms[ k ].data( ) + 0x50, s, 64 );
                s += 16;
                trigger_instigator_update( m, comps[ k ].data( ) );
            }
            else
                trigger_instigator_release( m, comps[ k ].data( ) );
            std::vector< unsigned > ours;
            ours.push_back( static_cast< unsigned >( m.instigators.size( ) ) );
            for ( const trigger_record_t& r : m.instigators )
            {
                unsigned ci = 0xFFFFFFFFu, ti = 0xFFFFFFFFu;
                for ( unsigned c = 0; c < n; ++c )
                {
                    ci = r.component == comps[ c ].data( ) ? c : ci;
                    ti = r.transform == transforms[ c ].data( ) ? c : ti;
                }
                unsigned w[ 13 ] = { ci, ti, r.at.mask };
                std::memcpy( w + 3, r.at.prev, 12 );
                std::memcpy( w + 7, &r.at.radius, 4 );
                std::memcpy( w + 8, r.at.cur, 12 );
                std::memcpy( w + 12, &r.at.rate, 4 );
                ours.insert( ours.end( ), w, w + 13 );
            }
            for ( unsigned c = 0; c < n; ++c )
                ours.push_back( comps[ c ][ 0x28 ] );
            const unsigned len = *s++;
            for ( unsigned i = 0; i < len; ++i )
            {
                ++words;
                const unsigned mine = i < ours.size( ) ? ours[ i ] : 0xDEADBEEFu;
                if ( mine != s[ i ] && ++failed <= 8 )
                    LOG_ERROR( "trigger instigators vector %d op %u (kind %u) word %u: ours 0x%08X retail 0x%08X", static_cast< int >( v ), o, kind, i,
                               mine, s[ i ] );
            }
            if ( ours.size( ) != len && ++failed <= 8 )
                LOG_ERROR( "trigger instigators vector %d op %u: %u words ours, %u retail", static_cast< int >( v ), o,
                           static_cast< unsigned >( ours.size( ) ), len );
            s += len;
        }
    }
    if ( s != g_golden_trigger_instigators_words + g_golden_trigger_instigators_size )
    {
        LOG_ERROR( "trigger instigators: the golden stream was not consumed exactly" );
        ++failed;
    }
    if ( failed == 0 )
        LOG_SUCCESS( "The trigger manager's instigators (0x531F50 / 0x536460's step / 0x536350 / 0x5362A0) match retail: %d vectors, %d ops, "
                     "%d words bit for bit", static_cast< int >( g_golden_trigger_instigators_count ), ops, words );
    else
        LOG_ERROR( "trigger instigators FAILED %d of %d words", failed, words );
    return failed == 0;
}

// The level's dynamic game props (world/dynprop.hxx, world/bodies.hxx) against retail's create + VuEntity::load +
// gameInitialize 0x437580 (oracle case dynamic_prop_bodies) on every VuCheckpointBuoyEntity / VuDynamicGamePropEntity
// of VuTemplateAsset/Levels/Hydroelectric - plain or a template instance - loaded here through hydro's tree with the
// pack's templates. Compared: the component's load words + Initially Active and its +0xD0 / +0xDC .. +0xE4 / +0xEC after
// postLoad (0x5459F0), its +0xD0 / +0xD4 / +0xD8 / +0xEC after gameInitialize, the shape's (compound: type, count,
// local aabb, revision, margin; cylinder: type, scaling, implicit, margin, up axis; convex hull: type, scaling, margin,
// cached aabb min) and the child's modelled words, the body's 0x2D4 bytes (pointer words masked), the ticks registered
// (Decision / Build in order), the body / step-callback / draw adds. VuBreakableDynamicGamePropEntity and
// VuFloatingMineEntity are not recreated: counted, not compared (their tree state is UNKNOWN).
inline bool verify_dynamic_prop_bodies( )
{
    static c_archive archive;
    const std::string pack = world_check_pack( );
    std::vector< std::uint8_t > bytes;
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) || !archive.read( "VuTemplateAsset/Levels/Hydroelectric", bytes ) )
    {
        LOG_ERROR( "verify_dynamic_prop_bodies: Base.apf / the Hydroelectric template not readable" );
        return false;
    }
    static std::map< std::string, value_t > templates;
    const value_t document = c_reader( bytes ).parse( );
    const entity_registry_t registry = build_entity_registry( archive.names( "VuTemplateAsset/" ) );
    entity_world_t world = race_entity_world( registry, []( const std::string& name ) -> const value_t* {
        auto at = templates.find( name );
        if ( at == templates.end( ) )
        {
            std::vector< std::uint8_t > raw;
            value_t parsed{ };
            if ( archive.read( ( "VuTemplateAsset/" + name ).c_str( ), raw ) )
                parsed = c_reader( raw ).parse( );
            at = templates.emplace( name, std::move( parsed ) ).first;
        }
        return at->second.tag == 6 ? &at->second : nullptr;
    } );
    std::unique_ptr< entity_t > tree = entity_load_project( member( document, "VuProject" ), "Data/Projects/Check.vuprj", world );
    boat_mesh_library_t lib;
    lib.read_user = &archive;
    lib.read = []( void* u, const std::string& key, std::vector< std::uint8_t >& blob ) { return static_cast< c_archive* >( u )->read( key.c_str( ), blob ); };
    bodies_hooks_t hooks;
    hooks.surface = []( void*, const std::string& n ) -> std::uint8_t {
        for ( std::size_t k = 0; k < g_golden_dynamic_props_surface_count; ++k )
            if ( n == g_golden_dynamic_props_surfaces[ k ] )
                return static_cast< std::uint8_t >( 1 + k );
        return 0xEE;
    };
    hooks.gravity = []( void*, float* g ) {
        const std::uint32_t z = 0xC19CE560u;
        g[ 0 ] = g[ 1 ] = 0.0f;
        std::memcpy( &g[ 2 ], &z, 4 );
    };
    auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    int failed = 0, compared = 0, skipped = 0, words = 0, buoys = 0;
    for ( std::size_t k = 0; k < g_golden_dynamic_props_count && tree; ++k )
    {
        const golden_dynamic_prop_t& g = g_golden_dynamic_props[ k ];
        const std::string cls = g.m_class;
        if ( cls != "VuCheckpointBuoyEntity" && cls != "VuDynamicGamePropEntity" )
        {
            ++skipped;
            continue;
        }
        const unsigned* w = g_golden_dynamic_props_words + g.m_at;
        const unsigned* loaded = w;
        const unsigned* after = w + 66;
        const unsigned* shape_w = w + 132;
        const unsigned* child_w = w + 156;
        const unsigned* body_w = w + 176;
        const unsigned* shape_m = w + 357;
        const unsigned* child_m = w + 381;
        const unsigned* body_m = w + 401;
        const unsigned* tail = w + 582;
        entity_t* e = tree.get( );
        const std::string path = g.m_path;
        std::size_t at = 0;
        while ( e )
        {
            const std::size_t slash = path.find( '/', at );
            const std::string part = path.substr( at, slash == std::string::npos ? std::string::npos : slash - at );
            entity_t* next = nullptr;
            for ( auto& c : e->m_children )
                if ( c->m_name == part )
                    next = c.get( );
            e = next;
            if ( slash == std::string::npos )
                break;
            at = slash + 1;
        }
        rigid_component_state_t* rs = nullptr;
        if ( e )
            for ( entity_component_t& c : e->m_components )
                if ( c.m_vtable == k_rigid_body_component_vtable )
                    rs = static_cast< rigid_component_state_t* >( c.m_state.get( ) );
        transform_component_t* t = e ? entity_transform( *e ) : nullptr;
        if ( !e || !rs || !t || !e->m_state || !dynamic_prop_class( e->m_type ) )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "dynamic props: '%s' not found as a %s with its components in hydro's tree", g.m_path, g.m_class );
            continue;
        }
        ++compared;
        buoys += cls == "VuCheckpointBuoyEntity" ? 1 : 0;
        const dynamic_prop_t& p = static_cast< dynamic_prop_state_t* >( e->m_state.get( ) )->value;
        rigid_component_t& c = rs->value;
        dynamic_prop_post_load( p, c );
        int bad = -1;
        const std::uint32_t mine_loaded[ 19 ] = { static_cast< std::uint32_t >( c.collision_type ), bits( c.radius ), bits( c.size[ 0 ] ), bits( c.size[ 1 ] ),
            bits( c.size[ 2 ] ), bits( c.height ), bits( c.offset[ 0 ] ), bits( c.offset[ 1 ] ), bits( c.offset[ 2 ] ), bits( c.linear_damping ),
            bits( c.angular_damping ), bits( c.linear_sleep ), bits( c.angular_sleep ), c.flags_d0, bits( c.centre[ 0 ] ), bits( c.centre[ 1 ] ),
            bits( c.centre[ 2 ] ), bits( c.mass ), c.initially_active };
        const int at_loaded[ 18 ] = { 0x10, 0x2C, 0x30, 0x34, 0x38, 0x40, 0x5C, 0x60, 0x64, 0x6C, 0x70, 0x74, 0x78, 0xD0, 0xDC, 0xE0, 0xE4, 0xEC };
        for ( int i = 0; i < 18 && bad < 0; ++i )
            if ( mine_loaded[ i ] != loaded[ at_loaded[ i ] / 4 ] )
                bad = at_loaded[ i ];
        if ( bad < 0 && mine_loaded[ 18 ] != ( loaded[ 0x7C / 4 ] & 0xFFu ) )
            bad = 0x7C;
        if ( bad < 0 && c.surface != g.m_surface )
            bad = 100;
        const collision_mesh_asset_t* asset = nullptr;
        if ( ( c.collision_type == 6 || c.collision_type == 7 ) && !c.mesh_asset.empty( ) && boat_mesh_exists( &lib, "VuCollisionMeshAsset", c.mesh_asset ) )
            asset = lib.collision_of( boat_mesh_create( &lib, "VuCollisionMeshAsset", c.mesh_asset ) );
        if ( bad < 0 && ( asset ? c.mesh_asset : std::string( ) ) != g.m_mesh )
            bad = 101;
        auto b = std::make_unique< level_body_t >( );
        std::string why;
        std::uint32_t id = tail[ 2 ] ? body_w[ 0x210 / 4 ] : 0;
        dynamic_prop_motion_t motion;
        dynamic_prop_init_t init;
        const bool ok = dynamic_prop_game_initialize( *b, c, p, *t, asset, id, hooks, motion, init, why );
        if ( bad < 0 && !ok )
            bad = 102;
        if ( bad < 0 && ok )
        {
            const std::uint32_t mine_after[ 4 ] = { c.flags_d0, static_cast< std::uint32_t >( c.group ) | static_cast< std::uint32_t >( c.mask ) << 16, c.flags_d8,
                                                    bits( c.mass ) };
            const int at_after[ 4 ] = { 0xD0, 0xD4, 0xD8, 0xEC };
            for ( int i = 0; i < 4 && bad < 0; ++i )
                if ( mine_after[ i ] != after[ at_after[ i ] / 4 ] )
                    bad = 0x1000 + at_after[ i ];
            if ( bad < 0 && ( tail[ 0 ] != 1 || tail[ 2 ] != 1 || ( tail[ 1 ] != 0 ) != b->has_compound ) )
                bad = 103;
        }
        // The modelled words of one convex / cylinder shape image against `want` (its mask skips nothing modelled).
        auto convex_words = [ & ]( const convex_shape_t& s, const unsigned* want, int size ) -> int {
            std::uint32_t m[ 24 ] = { };
            std::vector< int > offs = { 4, 0x10, 0x14, 0x18, 0x30 };
            m[ 1 ] = static_cast< std::uint32_t >( s.type );
            for ( int i = 0; i < 3; ++i )
                m[ 4 + i ] = bits( s.local_scaling[ i ] );
            m[ 12 ] = bits( s.margin );
            if ( s.type == 13 )
            {
                for ( int i = 0; i < 4; ++i )
                    m[ 8 + i ] = bits( s.implicit[ i ] );
                m[ 16 ] = static_cast< std::uint32_t >( s.up_axis );
                offs.insert( offs.end( ), { 0x20, 0x24, 0x28, 0x2C, 0x40 } );
            }
            else if ( size >= 0x60 )
            {
                for ( int i = 0; i < 3; ++i )
                    m[ 20 + i ] = bits( s.local_aabb_min[ i ] );
                offs.insert( offs.end( ), { 0x50, 0x54, 0x58 } );
            }
            for ( int o : offs )
            {
                ++words;
                if ( m[ o / 4 ] != want[ o / 4 ] )
                    return o;
            }
            return -1;
        };
        if ( bad < 0 && ok )
        {
            if ( b->has_compound )
            {
                const compound_shape_t& cs = b->compound;
                const std::uint32_t m[ 11 ] = { 31u, static_cast< std::uint32_t >( cs.children.size( ) ), bits( cs.local_aabb_min[ 0 ] ), bits( cs.local_aabb_min[ 1 ] ),
                    bits( cs.local_aabb_min[ 2 ] ), bits( cs.local_aabb_max[ 0 ] ), bits( cs.local_aabb_max[ 1 ] ), bits( cs.local_aabb_max[ 2 ] ),
                    static_cast< std::uint32_t >( cs.revision ), bits( cs.margin ), 0 };
                const int o[ 10 ] = { 4, 0x10, 0x20, 0x24, 0x28, 0x30, 0x34, 0x38, 0x44, 0x48 };
                for ( int i = 0; i < 10 && bad < 0; ++i )
                {
                    ++words;
                    if ( m[ i ] != shape_w[ o[ i ] / 4 ] )
                        bad = 0x2000 + o[ i ];
                }
                const convex_shape_t& child = c.collision_type == 6 ? b->convex : b->cylinder;
                const int r = bad < 0 ? convex_words( child, child_w, 0x50 ) : -1;
                if ( r >= 0 )
                    bad = 0x3000 + r;
            }
            else
            {
                const convex_shape_t& single = c.collision_type == 6 ? b->convex : b->cylinder;
                const int r = convex_words( single, shape_w, 0x60 );
                if ( r >= 0 )
                    bad = 0x2000 + r;
            }
        }
        for ( int i = 0; i < 181 && bad < 0 && ok; ++i )
        {
            if ( body_m[ i ] )
                continue;
            ++words;
            if ( body_word( b->body, 4 * i ) != body_w[ i ] )
            {
                bad = 0x4000 + 4 * i;
                if ( failed < 8 )
                    LOG_ERROR( "dynamic props: '%s' body +0x%X = 0x%08X, retail 0x%08X", g.m_path, 4 * i, body_word( b->body, 4 * i ), body_w[ i ] );
            }
        }
        if ( bad < 0 && ok )
        {
            std::vector< unsigned > ticks;
            if ( init.decision_tick )
                ticks.push_back( 1 );
            if ( init.build_tick )
                ticks.push_back( 2 );
            const unsigned n = tail[ 3 ];
            bool same = n == ticks.size( );
            for ( unsigned i = 0; same && i < n; ++i )
                same = tail[ 4 + i ] == ticks[ i ];
            const unsigned* adds = tail + 4 + n;
            if ( !same )
                bad = 300;
            else if ( adds[ 0 ] != ( init.added ? 1u : 0u ) || adds[ 1 ] != ( init.step_callback ? 1u : 0u ) || adds[ 2 ] != ( init.drawn ? 1u : 0u ) )
                bad = 301;
        }
        if ( bad >= 0 && ++failed <= 8 )
            LOG_ERROR( "dynamic props: '%s' differs at item 0x%X (%s)", g.m_path, bad, why.c_str( ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuDynamicGamePropEntity gameInitialize 0x437580 / createRigidBody 0x544100 (cylinder, compound, inertia, motion state) match "
                     "retail on %d dynamic game props (%d checkpoint buoys; %d words bit for bit); %d breakable / mine props not recreated",
                     compared, buoys, words, skipped );
    else
        LOG_ERROR( "dynamic prop bodies FAILED on %d of %d props", failed, compared );
    return failed == 0;
}

// The dynamic game prop's step callback (world/dynprop.hxx dynamic_prop_step: 0x437730 with the sphere buoyancy
// 0x5474A0 and applyForce 0x4133B0) against retail's own run (oracle case dynamic_prop_step): 240 seeded entity /
// component / body images, the water query answered as the case's hook did (the point back, the vector's disp and
// height). Compared: the water points asked (count and words), the body's force / torque +0x1B0 .. +0x1CC (the
// w lanes too - untouched) and the entity's +0x180 / +0x181.
inline bool verify_dynamic_prop_step( )
{
    int failed = 0, words = 0, wet = 0;
    for ( std::size_t index = 0; index < g_golden_dynamic_prop_step_count; ++index )
    {
        const unsigned* s = g_golden_dynamic_prop_step_words + g_golden_dynamic_prop_step_offsets[ index ];
        std::uint8_t ent[ 0x190 ], comp[ 0x108 ], body[ 0x2D4 ];
        std::memcpy( ent, s, 0x190 );
        std::memcpy( comp, s + 100, 0x108 );
        std::memcpy( body, s + 166, 0x2D4 );
        const unsigned* w = s + 347;
        float disp[ 3 ], height;
        std::memcpy( disp, w, 12 );
        std::memcpy( &height, w + 3, 4 );
        const unsigned n = w[ 4 ];
        const unsigned* points = w + 5;
        const unsigned* want = points + 3 * n;
        dynamic_prop_t p;
        dynamic_prop_construct( p );
        std::memcpy( &p.mass, ent + 0x78, 4 );
        std::memcpy( &p.fluids_radius, ent + 0xB8, 4 );
        std::memcpy( &p.fluids_density, ent + 0xBC, 4 );
        std::memcpy( &p.fluids_drag, ent + 0xC0, 4 );
        std::memcpy( &p.fluids_lift, ent + 0xC4, 4 );
        std::memcpy( p.hydro_centre, ent + 0xC8, 12 );
        std::memcpy( &p.splash_speed, ent + 0xF0, 4 );
        p.linear_anchor = ent[ 0xFC ];
        p.angular_anchor = ent[ 0xFD ];
        std::memcpy( &p.anchor_spring, ent + 0x100, 4 );
        std::memcpy( &p.anchor_damping, ent + 0x104, 4 );
        dynamic_prop_live_t live;
        std::memcpy( live.anchor, ent + 0x144, 12 );
        std::memcpy( live.anchor_q, ent + 0x154, 16 );
        live.in_water = ent[ 0x180 ];
        live.splash = ent[ 0x181 ];
        float centre[ 3 ];
        std::memcpy( centre, comp + 0xDC, 12 );
        std::vector< std::uint32_t > asked;
        const dynamic_prop_water_fn water = [ & ]( const float* point, water_vertex_t& out ) {
            out = water_vertex_t{ };
            std::memcpy( out.pos, point, 12 );
            std::memcpy( out.disp, disp, 12 );
            out.height = height;
            for ( int k = 0; k < 3; ++k )
            {
                std::uint32_t u;
                std::memcpy( &u, point + k, 4 );
                asked.push_back( u );
            }
            return true;
        };
        bool bad = !dynamic_prop_step( p, live, centre, body, water );
        bad = bad || asked.size( ) != 3 * n;
        for ( std::size_t k = 0; !bad && k < asked.size( ); ++k )
            bad = asked[ k ] != points[ k ];
        for ( int k = 0; !bad && k < 8; ++k )
        {
            ++words;
            std::uint32_t u;
            std::memcpy( &u, body + 0x1B0 + 4 * k, 4 );
            if ( u != want[ k ] )
            {
                bad = true;
                if ( failed < 6 )
                    LOG_ERROR( "dynamic prop step vector %d: body +0x%X = 0x%08X, retail 0x%08X", static_cast< int >( index ), 0x1B0 + 4 * k, u, want[ k ] );
            }
        }
        bad = bad || live.in_water != want[ 8 ] || live.splash != want[ 9 ];
        wet += live.in_water ? 1 : 0;
        if ( bad && ++failed <= 6 )
            LOG_ERROR( "dynamic prop step vector %d differs", static_cast< int >( index ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuDynamicGamePropEntity's step callback 0x437730 (buoyancy 0x5474A0, applyForce 0x4133B0, the anchors) matches retail "
                     "over %d vectors (%d in water; %d force / torque words bit for bit, the water points, +0x180 / +0x181)",
                     static_cast< int >( g_golden_dynamic_prop_step_count ), wet, words );
    else
        LOG_ERROR( "dynamic prop step FAILED %d of %d", failed, static_cast< int >( g_golden_dynamic_prop_step_count ) );
    return failed == 0;
}

#include <workspace/utility/checks/rig.hxx>
#include <workspace/utility/checks/session.hxx>

inline bool verify_world_all( )
{
    bool ok = true;
    ok = verify_dynamic_prop_step( ) && ok;
    ok = verify_dynamic_prop_bodies( ) && ok;
    ok = verify_trigger_instigators( ) && ok;
    ok = verify_gfx_blend( ) && ok;
    ok = verify_gfx_zones( ) && ok;
    ok = verify_race_hull_wake( ) && ok;
    ok = verify_prop_rigid_bodies( ) && ok;
    ok = verify_water_surface_under( ) && ok;
    ok = verify_boat_motion_set( ) && ok;
    ok = verify_sync_motion_states( ) && ok;
    ok = verify_transform_set_world( ) && ok;
    ok = verify_boat_build_audio( ) && ok;
    ok = verify_water_surface_list( ) && ok;
    ok = verify_race_game_ctor( ) && ok;
    ok = verify_game_project_name( ) && ok;
    ok = verify_boat_skin( ) && ok;
    ok = verify_boat_draw( ) && ok;
    ok = verify_race_begin( ) && ok;
    ok = verify_boat_post_load( ) && ok;
    ok = verify_boat_game_init( ) && ok;
    ok = verify_game_boat_load( ) && ok;
    ok = verify_race_track( ) && ok;
    ok = verify_race_track_start( ) && ok;
    ok = verify_water_templates( ) && ok;
    ok = verify_water_frustum( ) && ok;
    ok = verify_water_grid( ) && ok;
    ok = verify_water_grid_maps( ) && ok;
    ok = verify_tick_manager( ) && ok;
    ok = verify_static_scene_load( ) && ok;
    ok = verify_animated_model_draw( ) && ok;
    ok = verify_script_graph( ) && ok;
    ok = verify_prop_draw_matrix( ) && ok;
    ok = verify_light_rig( ) && ok;
    ok = verify_gfx_settings( ) && ok;
    ok = verify_triggers( ) && ok;
    ok = verify_static_pfx( ) && ok;
    ok = verify_actions( ) && ok;
    ok = verify_trigger_plane( ) && ok;
    ok = verify_trigger_box( ) && ok;
    ok = verify_race_progress( ) && ok;
    ok = verify_multi_delay( ) && ok;
    ok = verify_race_game_fsm( ) && ok;
    ok = verify_timetrial_game_fsm( ) && ok;
    ok = verify_slalom_game_fsm( ) && ok;
    ok = verify_buoy_hit( ) && ok;
    ok = verify_tick_same_pass( ) && ok;
    ok = verify_game_ctors( ) && ok;
    ok = verify_vugame_handlers( ) && ok;
    ok = verify_split_handlers( ) && ok;
    ok = verify_static_model_draw( ) && ok;
    ok = verify_static_world_aabb( ) && ok;
    ok = verify_session_all( ) && ok;
    ok = verify_rig_all( ) && ok;
    return ok;
}
