#pragma once

// c_world's entities - member definitions moved out of world.hxx (one class, split by concern).

// A VuTemplateAsset's whole document, what factory slot 7 0x555690 loads,
// or null when the pack has none of that name.
inline const value_t* c_world::template_document( const std::string& name )
{
    template_root( name );
    const auto found = m_templates.find( name );
    return found != m_templates.end( ) && found->second.tag == 6 ? &found->second : nullptr;
}

// The project as retail loads it (the race game 0x4B4360 -> VuProject::load
// 0x556390): the registry as start-up leaves it, the asset's AssetData
// "File" path (0x55FBE0), and the entity tree through every recreated load.
// The coronas are fed from it (each VuCoronaEntity's transform world and its
// property targets), and so are the props: every model-draw component's
// Model Asset and Color at its owner's draw matrix. Its Draw Distance is
// carried on the animated props; no draw distance or LOD switch (the
// static draw 0x5B0730's +0xA8 / +0xAC / +0xB0 tests) is applied by the
// renderer yet - a gap, not a choice. Anything below an UNKNOWN entity is
// never created, and anything whose world is unknown is not placed - both
// counted, neither stood in for. The rotators still come from visit() (the
// old walk) and adopt the tree's placements here.
inline void c_world::load_entity_tree( const char* asset, const value_t& project )
{
    // race.hxx: the AssetData path, the registry and the callbacks (with the AI
    // waypoint's property set the track manager reads).
    m_track_built = false;
    m_track = track_manager_t{ };
    const std::string path = race_project_file( m_archive, asset );
    if ( path.empty( ) )
    {
        LOG_ERROR( "entity tree: %s has no AssetData \"File\" - the project is not loaded the retail way", asset );
        return;
    }
    m_tree = race_load_tree( m_archive, asset, project, [ this ]( const std::string& name ) { return template_document( name ); } );
    if ( !m_tree )
    {
        LOG_ERROR( "entity tree: VuProject::load failed on %s (%s)", asset, path.c_str( ) );
        return;
    }
    // VuTrackManagerImpl from the tree's AI waypoints (boat track.hxx): retail's
    // project gameInitialize runs every waypoint's slot 6, then the race's begin
    // 0x4829A0 the manager's slot 7. A project without waypoints (or one the
    // build refuses) has no track.
    m_track_built = track_build_from_tree( *m_tree, m_track );
    if ( m_track_built )
        LOG_INFO( "track: %d waypoints, %d sectors, %d checkpoints, start %d end %d, length %.1f",
            static_cast< int >( m_track.waypoints.size( ) ), static_cast< int >( m_track.sectors.size( ) ),
            static_cast< int >( m_track.checkpoints.size( ) ), m_track.start, m_track.end, m_track.length );
    else
        LOG_INFO( "track: none (no AI waypoints, or the build refused)" );
    int entities = 0;
    int unknown = 0;
    int unplaced = 0;
    int placed = 0;
    std::map< std::string, int > unknown_types;
    int models = 0;
    int models_unplaced = 0;
    std::vector< trigger_t > fresh_triggers;
    int pfx_placed = 0;
    int pfx_unplaced = 0;
    std::map< std::string, std::uint32_t > fresh_static_pfx;
    std::map< std::string, entity_t* > fresh_entities;   // every entity by its graph path (the audio receivers read their state)
    int triggers_unplaced = 0;
    // The logic / action / motion / settings consumers (engine entity/actions.hxx).
    std::vector< rotator_t > fresh_rotators;
    std::map< std::string, play_t > fresh_play;
    std::map< std::string, play_t > fresh_plays;   // by the sender's own path: what its Trigger packs
    std::map< std::string, action_t > fresh_actions;
    std::map< std::string, delay_t > fresh_delays;
    std::map< std::string, std::string > fresh_events;
    std::map< std::string, bool > fresh_booleans;
    std::map< std::string, front_end_camera_t > fresh_front_end;
    std::map< std::string, filter_t > fresh_filters;
    std::vector< std::string > fresh_initialized;
    std::set< std::string > fresh_ifs;
    std::map< std::string, entity_t* > fresh_logic;
    std::string fresh_sky;
    bool fresh_sky_reflect = false;
    // The event graph addresses an entity by its names below the project
    // root, '/'-joined.
    std::map< std::string, std::string > fresh_types;
    std::map< std::string, int > fresh_census;
    // THE EVENT GRAPH, as the script components' loads resolved it (engine
    // entity/script.hxx, verify_script_graph): every plug's +0x50 list in link
    // order - outputs and inputs alike, as retail holds them - and every bound
    // ref's target, each end named by its path. A connection or ref that did
    // not resolve is not in the tree, so it is not here either.
    std::map< const entity_t*, std::string > path_of;
    const auto name_all = [ & ]( const auto& self, entity_t& e, const std::string& trail ) -> void
    {
        path_of[ &e ] = trail;
        for ( auto& child : e.m_children )
            self( self, *child, trail.empty( ) ? child->m_name : trail + "/" + child->m_name );
    };
    name_all( name_all, *m_tree, std::string( ) );
    std::map< std::pair< std::string, std::string >, std::vector< edge_t > > fresh_graph;
    std::map< std::pair< std::string, std::string >, std::string > fresh_refs;
    int graph_links = 0;
    const auto walk = [ & ]( const auto& self, entity_t& e, const std::string& trail ) -> void
    {
        if ( const script_component_t* sc = script_of( e ); sc && !trail.empty( ) )
        {
            for ( const script_plug_t& plug : sc->m_plugs )
            {
                if ( plug.m_links.empty( ) )
                    continue;
                std::vector< edge_t >& edges = fresh_graph[ std::make_pair( trail, std::string( plug.m_row->m_name ) ) ];
                for ( const script_link_t& link : plug.m_links )
                    edges.push_back( edge_t{ path_of[ link.m_entity ], script_of( *link.m_entity )->m_plugs[ link.m_plug ].m_row->m_name } );
                graph_links += static_cast< int >( plug.m_links.size( ) );
            }
            for ( const script_ref_t& ref : sc->m_refs )
                if ( ref.m_target )
                    fresh_refs[ std::make_pair( trail, std::string( ref.m_row->m_name ) ) ] = path_of[ ref.m_target ];
        }
        // The class behind each path (what the graph audit and ref_target read)
        // and the per-class census, from the tree.
        if ( !trail.empty( ) )
        {
            fresh_types[ trail ] = e.m_type;
            ++fresh_census[ e.m_type ];
        }
        // VuRotateMotionEntity (world/rotate.hxx): its properties and the "Entity" ref's target (script component), whose
        // motion component slot 6 finds.
        if ( const auto* p = entity_action< rotate_motion_properties_t >( e ); p && e.m_type == "VuRotateMotionEntity" )
        {
            rotator_t motion{};
            motion.path = trail;
            motion.entity = &e;
            vu_identity( motion.value.base );
            std::memcpy( motion.value.axis, p->axis, sizeof( motion.value.axis ) );
            motion.value.speed = p->speed;
            motion.value.local = p->local != 0;
            motion.initially_active = p->initially_active != 0;
            motion.one_shot = p->one_shot != 0;
            if ( const script_component_t* sc = script_of( e ) )
                for ( const script_ref_t& ref : sc->m_refs )
                    if ( std::string( ref.m_row->m_name ) == "Entity" && ref.m_target )
                        motion.target = ref.m_target;
            fresh_rotators.push_back( std::move( motion ) );
        }
        // VuAttachEntity (world/joints.hxx): its properties, the Parent / Child refs' entities (script component).
        if ( const auto* p = entity_action< attach_properties_t >( e ); p && e.m_type == "VuAttachEntity" )
        {
            entity_t* ends[ 2 ] = { nullptr, nullptr };
            if ( const script_component_t* sc = script_of( e ) )
                for ( const script_ref_t& ref : sc->m_refs )
                {
                    const std::string name( ref.m_row->m_name );
                    if ( name == "Parent" )
                        ends[ 0 ] = ref.m_target;
                    else if ( name == "Child" )
                        ends[ 1 ] = ref.m_target;
                }
            m_joints.add( e, *p, ends[ 0 ], ends[ 1 ] );
        }
        // VuPlayAnimationEntity: the clip and its params from the loaded
        // targets; which props it drives and what triggers it are its script
        // component's links - its "PlayAnimation" output's ends, and the first
        // end of its "Trigger" input.
        if ( const auto* p = entity_action< play_animation_properties_t >( e ); p && e.m_type == "VuPlayAnimationEntity" &&
             !p->animation_asset.empty( ) )
        {
            play_t play{};
            play.clip = p->animation_asset;
            play.timed = p->timed_event_asset;
            play.path = trail;
            play.start = p->start_time;
            play.looping = p->looping != 0;
            play.one_shot = p->one_shot != 0;
            play.factor = p->time_factor;
            play.blend = p->blend_time;
            if ( const script_component_t* sc = script_of( e ) )
            {
                const int trigger = script_find_plug( *sc, "Trigger" );
                const int drives = script_find_plug( *sc, "PlayAnimation" );
                if ( trigger >= 0 && !sc->m_plugs[ trigger ].m_links.empty( ) )
                    play.trigger = path_of[ sc->m_plugs[ trigger ].m_links.front( ).m_entity ];
                if ( drives >= 0 )
                    for ( const script_link_t& link : sc->m_plugs[ drives ].m_links )
                    {
                        fresh_play.emplace( path_of[ link.m_entity ], play );
                        play.props.push_back( path_of[ link.m_entity ] );
                    }
            }
            fresh_plays.emplace( trail, play );
            if ( !trail.empty( ) )
            {
                action_t act{};
                act.one_shot = play.one_shot;
                fresh_actions.emplace( trail, act );
            }
        }
        if ( const auto* p = entity_action< delay_properties_t >( e ); p && e.m_type == "VuDelayEntity" )
        {
            delay_t wait{};
            wait.delay = p->delay;
            wait.window = p->random_window;
            fresh_delays.emplace( trail, wait );
        }
        if ( const auto* p = entity_action< generic_event_properties_t >( e ); p && e.m_type == "VuGenericEventEntity" )
            fresh_events.emplace( trail, p->event_name );
        if ( const auto* p = entity_action< boolean_properties_t >( e ); p && e.m_type == "VuBooleanEntity" )
            fresh_booleans.emplace( trail, p->value != 0 );
        if ( const auto* p = entity_action< front_end_camera_properties_t >( e ); p && e.m_type == "VuSetFrontEndCameraEntity" && !trail.empty( ) )
        {
            front_end_camera_t shot{};
            shot.interpolation_time = p->interpolation_time;
            shot.modify_eye = p->modify_eye != 0;
            shot.modify_target = p->modify_target != 0;
            shot.modify_far = p->modify_far != 0;
            std::memcpy( shot.eye, p->eye, sizeof( shot.eye ) );
            std::memcpy( shot.target, p->target, sizeof( shot.target ) );
            shot.far_plane = p->far_plane;
            fresh_front_end.emplace( trail, shot );
        }
        // VuWaterTextureSettingsEntity (ctor 0x5D2BD0): its words reach the water renderer through its game-start Trigger
        // 0x5D2B00 (graph.hxx), not at load.
        if ( const auto* p = entity_action< sky_box_properties_t >( e ); p && e.m_type == "VuSkyBoxEntity" && !p->model_asset.empty( ) )
        {
            fresh_sky = p->model_asset;
            fresh_sky_reflect = p->reflect != 0u;
        }
        // The three graph nodes that carry no property: their path is the state.
        if ( e.m_ctor && e.m_type == "VuOneShotFilterEntity" )
            fresh_filters.emplace( trail, filter_t{ } );
        if ( e.m_ctor && e.m_type == "VuOnProjectInitializedEntity" )
            fresh_initialized.push_back( trail );
        if ( e.m_ctor && e.m_type == "VuIfEntity" )
            fresh_ifs.insert( trail );
        // The logic entities (world/logic.hxx): their state is the tree's (race.hxx logic_state_t).
        if ( !trail.empty( ) )
            fresh_entities[ trail ] = &e;
        if ( logic_state_of( e ) && !trail.empty( ) )
            fresh_logic[ trail ] = &e;
        // VuTriggerPlaneEntity / VuTriggerBoxEntity (engine entity/trigger.hxx):
        // the volume is the transform - VuTriggerPlaneEntity_bounds 0x5D18B0
        // composes VuMatrix_multiply(fromScale(scale +0xB0), out, world
        // +0x50), the same product as transform_draw_matrix, and the crossing
        // test 0x5D1570 compares against the Scale (+0xB0) itself as the half
        // extent. Initially Active and Trigger Type are the loaded targets.
        if ( entity_is_trigger( e.m_type ) && e.m_state )
        {
            const transform_component_t* t = entity_transform( e );
            if ( !t || e.m_world_unknown )
                ++triggers_unplaced;
            else
            {
                const trigger_properties_t& p = static_cast< const trigger_state_t* >( e.m_state.get( ) )->value;
                trigger_t gate{};
                gate.box = e.m_type == "VuTriggerBoxEntity";
                gate.path = trail;
                transform_draw_matrix( *t, gate.world );
                for ( int axis = 0; axis < 3; ++axis )
                    gate.scale[ axis ] = t->scale[ axis ];
                if ( !model_invert( gate.inverse, gate.world ) )
                    gate.inverse[ 0 ] = 0.0f;
                gate.kind = p.trigger_type;
                gate.active = p.initially_active != 0;
                std::memcpy( gate.frame, t->world, sizeof( gate.frame ) );
                for ( int axis = 0; axis < 3; ++axis )
                    gate.half[ axis ] = t->scale[ axis ];
                gate.mask = trigger_type_mask( p.trigger_type );
                fresh_triggers.push_back( std::move( gate ) );
            }
        }
        // VuStaticPfxEntity (engine entity/pfx.hxx): what its onGameInitialize
        // 0x5EB060 hands the VuPfxSystem the pfx manager (0x75904C vt+0x28)
        // creates from its Effect Name - setScale(Pfx Scale), setColor
        // (static_pfx_color), setMatrix(the transform WORLD: scale kept out,
        // as its transform watcher 0x5EB4A0 hands it too) and start() iff
        // Initially Active. The manager's lookup is the platform side: an
        // empty name is skipped here (what retail's manager returns for it
        // is unread); the system is the verified runtime's (world/effects.hxx).
        if ( e.m_type == "VuStaticPfxEntity" && e.m_state )
        {
            const static_pfx_t& p = static_cast< const static_pfx_state_t* >( e.m_state.get( ) )->value;
            const transform_component_t* t = entity_transform( e );
            if ( !t || e.m_world_unknown )
                ++pfx_unplaced;
            else if ( !p.effect_name.empty( ) )
            {
                float color[ 4 ];
                static_pfx_color( p, color );
                // +0x94 for its Start / Stop / Kill plugs (graph.hxx): 0 when the create answered none - then
                // each plug's guard does nothing, as retail's.
                fresh_static_pfx[ trail ] = m_pfx.add_static( m_archive, p.effect_name, t->world, p.pfx_scale, color, p.initially_active != 0 );
                ++pfx_placed;
            }
        }
        // VuGlobalGfxSettingsEntity (engine entity/gfx.hxx): the far plane is
        // its Camera Far Plane - neither VuBoatCamera_update nor the front-end
        // camera sets one - and the fog its Fog Start / End and Fog Color's R,
        // G, B bytes / 255. The other 22 values are loaded and not consumed
        // yet (Contrast / Tint feed PostProcess/Effect and the Water * ones the
        // water shader's constants - memory water.md - neither wired), and
        // how retail applies the settings (Always Active, the ramp times, the
        // Rectangular / Elliptical volumes that blend over it) is unread: the
        // last one in tree order is applied at load, ours and flagged.
        if ( e.m_type == "VuGlobalGfxSettingsEntity" && e.m_state )
        {
            const gfx_settings_t& g = static_cast< const gfx_settings_state_t* >( e.m_state.get( ) )->value;
            m_authored_far = g.camera_far_plane;
            m_foliage_tint = g.foliage_tint;           // models.hxx foliage_frame
            {
                std::uint32_t words[ 26 ];
                gfx_settings_words( g, words );
                std::memcpy( m_gfx_record, words + 4, sizeof( m_gfx_record ) );   // world.hxx water_stage_frame
            }
            const float red = static_cast< float >( g.fog_color & 0xFF ) / 255.0f;
            const float green = static_cast< float >( ( g.fog_color >> 8 ) & 0xFF ) / 255.0f;
            const float blue = static_cast< float >( ( g.fog_color >> 16 ) & 0xFF ) / 255.0f;
            LOG_VERBOSE( "gfx settings %s: far %.0f  fog %.0f..%.0f  colour %.3f %.3f %.3f", trail.c_str( ), g.camera_far_plane,
                g.fog_start, g.fog_end, red, green, blue );
            // Hydroelectric authors Fog End 1800 and no Camera Far Plane, so
            // the far plane is the ctor's 500 and geometry clips long before
            // the fog reaches full density - one of the two comes from
            // somewhere not yet found. Reported, not reconciled.
            if ( g.fog_end > m_authored_far )
                LOG_WARNING( "gfx settings: Fog End %.0f is beyond the far plane %.0f - the far plane's real source has not been found",
                    g.fog_end, m_authored_far );
            c_engine::fog( g.fog_start, g.fog_end, red, green, blue );
        }
        // The rig is no longer read at load: the lights' game-start "Trigger" handlers write g_vulightmanager (world/settings.hxx,
        // the water agent's - case settings_triggers) and the renderer takes its words from there (graph.hxx).
        // The model-draw components, drawn at the matrix their owner leaves
        // in them (transform_draw_matrix: fromScale x world). An empty
        // Model Asset draws nothing.
        for ( const entity_component_t& c : e.m_components )
        {
            if ( ( c.m_vtable != k_static_model_component_vtable && c.m_vtable != k_animated_model_component_vtable ) || !c.m_state )
                continue;
            const model_draw_t& m = static_cast< const model_draw_state_t* >( c.m_state.get( ) )->value;
            // A static component naming only LOD 1 / LOD 2 still places those (its LOD 0 holds no scene).
            if ( m.model_asset.empty( ) && ( m.animated || ( m.lod1_model_asset.empty( ) && m.lod2_model_asset.empty( ) ) ) )
                continue;
            const transform_component_t* t = entity_transform( e );
            if ( !t || e.m_world_unknown )
            {
                ++models_unplaced;
                continue;
            }
            ++models;
            float matrix[ 16 ];
            transform_draw_matrix( *t, matrix );
            // The VuColor's R, G, B bytes.
            const float tint[ 3 ] = { static_cast< float >( m.color & 0xFF ) / 255.0f,
                static_cast< float >( ( m.color >> 8 ) & 0xFF ) / 255.0f, static_cast< float >( ( m.color >> 16 ) & 0xFF ) / 255.0f };
            if ( !m.animated )
            {
                // The Model Asset watcher's rebuild 0x5B09E0: the LOD 0 instance (+0xBC) takes the Model Asset - or
                // the LOD 1 asset when g_vumetrod3d11gfx's vt+0x1C (0x5135E0: its +4 byte) is set and LOD 1 is
                // named; +4 is 0 from the ctor 0x5AC8C0 and its setter vt+0x18 (0x5135D0) has no caller found, so
                // the Model Asset - then LOD 1 (+0xDC) and LOD 2 (+0xFC). Every instance shares the draw matrix,
                // so each is collected for the rotators. An instance naming nothing holds no scene and draws
                // nothing (0x520130: +0x10 null -> return).
                static_draw_item_t item{};
                item.draw = m;
                if ( !m_retail )
                    retail_attach( );
                const std::string* names[ 4 ] = { &m.model_asset, &m.lod1_model_asset, &m.lod2_model_asset, &m.reflection_model_asset };
                item.retail = std::make_unique< gfx_static_component_t >( );
                std::memcpy( item.retail->m_matrix, matrix, 64 );
                for ( int instance = 0; instance < 4; ++instance )
                    if ( !names[ instance ]->empty( ) )
                    {
                        model_for( *names[ instance ] );   // m_scene_aabbs; the placements are gone
                        if ( m_retail )
                            if ( const gfx_scene_view_t* v = m_retail->model( *names[ instance ] ) )
                                gfx_scene_view_attach( item.retail->m_lods[ instance ], *v );
                    }
                item.owner = &e;
                // 0x531D10 with the LOD 0 instance's scene aabb (its +0x10 scene's +0x5C), or the zero aabb
                // 0x7593F0 when it holds no scene; +0x1E's draw-manager hand-off is the renderer's (a hook).
                const auto box = m_scene_aabbs.find( m.model_asset );
                scene_component_aabb( item.aabb, box != m_scene_aabbs.end( ) ? box->second.data( ) : scene_zero_aabb( ), matrix, false );
                std::memcpy( item.retail->m_min, item.aabb, 32 );
                m_static_draws.push_back( std::move( item ) );
                continue;
            }
            // The clip arrives from a VuPlayAnimationEntity; bind_animations
            // joins them.
            animated_t item{};
            item.entity = &e;
            item.asset = m.model_asset;
            item.path = trail;
            std::memcpy( item.world, matrix, sizeof( item.world ) );
            std::memcpy( item.tint, tint, sizeof( item.tint ) );
            item.distance = m.draw_distance;
            item.draw = m;
            if ( e.m_type == "VuAnimatedPropEntity" && e.m_state )
                item.visible = static_cast< const prop_entity_state_t* >( e.m_state.get( ) )->visible != 0;
            {
                const auto box = m_scene_aabbs.find( m.model_asset );
                scene_component_aabb( item.aabb, box != m_scene_aabbs.end( ) ? box->second.data( ) : scene_zero_aabb( ), matrix, false );
            }
            m_animated.push_back( std::move( item ) );
        }
        ++entities;
        unknown += e.m_ctor ? 0 : 1;
        if ( !e.m_ctor )
            ++unknown_types[ e.m_type ];
        if ( e.m_type == "VuCoronaEntity" && e.m_state )
        {
            const transform_component_t* t = entity_transform( e );
            if ( !t || e.m_world_unknown )
                ++unplaced;
            else
            {
                m_coronas.add( corona_from( trail, *t, static_cast< const corona_state_t* >( e.m_state.get( ) )->value ) );
                ++placed;
            }
        }
        for ( auto& child : e.m_children )
            self( self, *child, trail.empty( ) ? child->m_name : trail + "/" + child->m_name );
    };
    walk( walk, *m_tree, std::string( ) );
    m_graph = std::move( fresh_graph );
    m_refs = std::move( fresh_refs );
    LOG_INFO( "event graph: %u linked plugs, %d link entries (each connection on both ends), %u bound refs - from the tree",
        static_cast< unsigned >( m_graph.size( ) ), graph_links, static_cast< unsigned >( m_refs.size( ) ) );
    if ( m_tree )
        foliage_build( *m_tree );
    m_entity_type = std::move( fresh_types );
    m_type_census = std::move( fresh_census );
    m_rotators = std::move( fresh_rotators );
    m_play = std::move( fresh_play );
    m_plays = std::move( fresh_plays );
    m_actions = std::move( fresh_actions );
    m_delays = std::move( fresh_delays );
    m_events = std::move( fresh_events );
    m_booleans = std::move( fresh_booleans );
    m_front_end = std::move( fresh_front_end );
    m_filters = std::move( fresh_filters );
    m_initialized = std::move( fresh_initialized );
    m_ifs = std::move( fresh_ifs );
    m_logic = std::move( fresh_logic );
    m_static_pfx = std::move( fresh_static_pfx );
    m_entities = std::move( fresh_entities );
    // Dev trace: each logic entity's links (both ends are recorded, so its inputs' senders are here too).
    for ( const auto& pair : m_graph )
        if ( m_logic.count( pair.first.first ) != 0 )
            for ( const edge_t& edge : pair.second )
                LOG_VERBOSE( "logic link: %s . %s <-> %s . %s", pair.first.first.c_str( ), pair.first.second.c_str( ), edge.entity.c_str( ),
                             edge.plug.c_str( ) );
    if ( !fresh_sky.empty( ) )
    {
        m_sky = fresh_sky;
        if ( !m_retail )
            retail_attach( );
        if ( m_retail )
        {
            m_retail->m_sky = m_retail->model( fresh_sky );
            m_retail->m_sky_reflect = fresh_sky_reflect;
        }
    }
    for ( trigger_t& gate : fresh_triggers )
        m_triggers.add( std::move( gate ) );
    LOG_INFO( "entity tree: %s (%s) - %d entities, %d UNKNOWN (nothing below one is loaded); %d coronas placed, %d with an "
              "unknown world; %d models placed, %d with an unknown world; %d triggers, %d with an unknown world; %d static pfx placed, %d "
              "with an unknown world", asset, path.c_str( ), entities, unknown, placed, unplaced, models, models_unplaced,
        static_cast< int >( m_triggers.size( ) ), triggers_unplaced, pfx_placed, pfx_unplaced );
    // VuWater, built from the tree: each water entity's slot 6 in VuEntity::
    // gameInitialize 0x500B10's order (children first), from the project root.
    // ASSUMED, not read: that the race calls 0x500B10 on the project root -
    // which caller does is unread. Point waves are made later by their tick.
    // An entity it cannot build is counted, never stood in for.
    m_water = water_built_t{ };
    m_wave_owner.clear( );
    m_water_missing = 0;
    water_build_world( *m_tree, m_water, []( entity_t& e ) { return entity_transform( e ); } );
    // Destroyed wake records are reused (water agent: retail's list / pool / trees / refcounts untouched -
    // verify_wake_reuse); the built water's default (off) keeps verify_race_hull_wake's creation-order indices.
    m_water.wake_reuse = true;
    water_fidelity( );
    LOG_INFO( "water: %u surfaces, %u waves built from the tree; %d water entities not built", static_cast< unsigned >( m_water.surfaces.size( ) ),
        static_cast< unsigned >( m_water.waves.size( ) ), m_water.skipped );
    // Each surface's VuWaterMapAsset (+0x100) by its WaterMap name; the fetch
    // is the asset manager hook (the pack read). A name the pack lacks keeps a
    // null map, counted.
    const int maps_missing = water_build_maps(
        m_water,
        []( void* user, const std::string& asset, std::vector< std::uint8_t >& blob ) {
            return static_cast< c_archive* >( user )->read( asset.c_str( ), blob );
        },
        &m_archive );
    int maps_named = 0;
    for ( const std::string& name : m_water.map_names )
        maps_named += name.empty( ) ? 0 : 1;
    LOG_INFO( "water: %d of %u surfaces name a water map, %d named but not loaded", maps_named,
        static_cast< unsigned >( m_water.map_names.size( ) ), maps_missing );
    water_view_build( m_water, m_water_view );
    // The water renderer's templates (its ctor's 0x595D40). The new level's surfaces take their shaders at the next water_draw; last level's frame is not drawn again.
    water_templates( m_grid.templates );
    m_grid_frame = water_grid_frame_t{ };
    m_stage_bound = false;
    // The point waves' descriptors (VuPointWaveEntity_tick 0x5D4C50: transform +0x80, then the entity's seven
    // properties), emitted when the graph reaches their Emit plug. An entity without a known world is skipped.
    m_point_waves.clear( );
    for ( const auto& named : path_of )
    {
        entity_t& e = *const_cast< entity_t* >( named.first );
        const point_wave_properties_t* p = entity_action< point_wave_properties_t >( e );
        const transform_component_t* t = e.m_type == "VuPointWaveEntity" ? entity_transform( e ) : nullptr;
        if ( !p || !t || e.m_world_unknown )
            continue;
        point_wave_source_t source{};
        std::memcpy( source.position, t->world + 12, 16 );
        std::memcpy( source.properties, p->word, 28 );
        m_point_waves.emplace( named.second, source );
    }
    // Which types' construction is not captured - what to recreate next.
    for ( const auto& pair : unknown_types )
        LOG_VERBOSE( "entity tree: UNKNOWN %-40s x%d", pair.first.c_str( ), pair.second );
}

// VuCoronaEntity's members as c_corona_entity reads them. The colour is
// the VuColor the property load packed (R, G, B, A bytes).
inline c_corona_entity::corona_t c_world::corona_from( const std::string& path, const transform_component_t& t, const corona_properties_t& c )
{
    c_corona_entity::corona_t light{};
    light.path = path;
    std::memcpy( light.world, t.world, sizeof( light.world ) );
    light.visible = c.initially_visible != 0;
    light.texture = c.texture_name;
    light.draw_distance = c.draw_distance;
    light.fade_distance = c.fade_distance;
    light.query_radius = c.query_radius;
    light.cone = c.cone;
    light.penumbra = c.penumbra;
    light.back_light = c.back_light != 0;
    for ( int k = 0; k < 4; ++k )
        light.color[ k ] = static_cast< float >( ( c.texture_color >> ( 8 * k ) ) & 0xFF ) / 255.0f;
    light.screen_space = c.screen_space != 0;
    light.size = c.texture_size;
    light.rotation_offset = c.rotation_offset;
    light.rotation_amount = c.rotation_amount;
    light.rotation_speed = c.rotation_speed;
    std::memcpy( light.axis, c.rotation_axis, sizeof( light.axis ) );
    return light;
}

inline void c_world::load_project( const char* asset )
{
    std::vector< std::uint8_t > project;
    if ( asset == nullptr || !m_archive.read( asset, project ) )
    {
        LOG_ERROR( "project %s", asset != nullptr ? asset : "" );
        return;
    }
    const value_t root = c_reader( project ).parse( );
    const value_t* project_node = find( root, "VuProject" );
    const value_t* entity = project_node != nullptr ? find( *project_node, "RootEntity" ) : nullptr;
    if ( entity == nullptr )
    {
        LOG_ERROR( "project %s has no root", asset );
        return;
    }
    float identity[ 16 ]{};
    math_identity( identity );
    visit( *entity, identity );
    load_entity_tree( asset, *project_node );
    for ( const auto& pair : m_type_census )
        LOG_VERBOSE( "census %-40s %d", pair.first.c_str( ), pair.second );
    // Hand the authored rig to the renderer. Until this runs the engine
    // keeps the old hardcoded lambert's constants in the rig's form, so a
    // scene with no light entities looks exactly as it did before.
    // --no-rig holds it back on purpose: it is the A/B switch that makes
    // the rig's contribution measurable against the old lambert instead
    // of merely plausible.
    if ( m_rig.lit( ) && c_engine::option( "no-rig" ) == nullptr )
        c_engine::rig( m_rig.ambient( ), m_rig.front( ), m_rig.back( ) );
    bind_animations( );
    audit_authored( );
    // The water surfaces and the sky box are counted but not built: the
    // readers that used to run here were partial and the geometry they fed
    // was invented. The count comes from the census, so the load log still
    // states exactly what the level authors and nothing draws.
    const auto water = m_type_census.find( "VuWaterSurfaceEntity" );
    LOG_SUCCESS( "project %s  draws %d  water surfaces %d (none built)  sky %s", asset, m_draws,
        water != m_type_census.end( ) ? water->second : 0,
        m_sky.empty( ) ? "-" : m_sky.c_str( ) );
}
