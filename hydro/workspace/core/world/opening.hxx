#pragma once

// The race session, second half (split from session.hxx, its include): race_session_start, the frame, the boat draw choice.

inline bool race_session_start( race_session_t& s, const std::string& mode, const value_t& game_data, const race_session_dbs_t& db,
                                const c_archive& archive, entity_t& tree, const track_manager_t* track, fleet_random_t& random,
                                const std::function< void( ) >& project_initialized = { } )
{
    race_session_end( s );   // a session still running ends first (its VuGame release, its bodies' dtors)
    race_world_t lent = std::move( s.world );   // the world's services, set by c_world before the start (the HUD projects)
    s = race_session_t{ };
    s.world = std::move( lent );
    s.dynamics = std::make_unique< race_dynamics_t >( );   // the engine init's VuDynamicsImpl (0x4F8050) + 0x4024E0's gravity
    race_dynamics_init( *s.dynamics, &s.contact );
    s.random = &random;
    s.game_data = game_data;
    race_game_construct( s.game );   // the VuRaceGame ctor 0x489030 (its base 0x480BA0's values)
    race_session_game_events( s, mode );   // the ctor's 0x5156E0 registrations (before the boat load, as the ctor runs)
    {
        std::vector< std::uint8_t > bytes;
        if ( archive.read( "VuDBAsset/SurfaceDB", bytes ) )
        {
            const value_t surface_db = c_reader( bytes ).parse( );
            s.surfaces_built = surface_table_build( s.surfaces, &surface_db );
        }
        if ( !s.surfaces_built )
            LOG_WARNING( "race: VuDBAsset/SurfaceDB did not build the surface-type table - the sections load will stop at it" );
        else
            LOG_INFO( "race: surface-type table built from SurfaceDB: %d types", static_cast< int >( surface_count( s.surfaces ) ) );
    }
    s.mode = mode;
    s.track = track;
    s.tree = &tree;

    // VuGame's boat load 0x481CE0 + each boat's postLoad 0x41CFF0.
    value_t& util = s.util;
    ui_json_child( util, "GameData" ) = s.game_data;
    const entity_registry_t registry = build_entity_registry( { } );
    race_load_hooks_t hooks;
    bool made = true;
    hooks.post_load = [ & ]( entity_t& e, const value_t& entry )
    {
        auto b = std::make_unique< race_session_boat_t >( );
        b->owner = &s;
        b->index = static_cast< int >( s.boats.size( ) );
        const race_boat_state_t& st = *static_cast< const race_boat_state_t* >( e.m_state.get( ) );
        b->name = e.m_name;
        b->boat = st.boat;
        b->skin = st.skin;
        b->graph = boat_construct( *db.constant_db );
        std::uint8_t* image = b->graph.objects[ 0 ].image.data( );
        // The Boat text into +0x8C (0x420A70 reads it there); Driver Type (+0x74)
        // and Skin (+0xA4) stay in the entity's state - their readers here are
        // postLoad's compare (given the state's text) and the skin pass (a hook).
        if ( !race_session_text( image, 0x8C, st.boat ) )
        {
            LOG_WARNING( "race: %s's Boat '%s' is 16+ characters (a heap std::string in +0x8C - not modelled); no race", e.m_name.c_str( ),
                         st.boat.c_str( ) );
            made = false;
        }
        race_session_boat_t& r = *b;
        race_post_load_hooks_t ph;
        ph.load_sections = [ &r, &db, &archive, &s ] { r.sections = race_session_sections( r, *db.boat_db, archive, s.surfaces_built ? &s.surfaces : nullptr ); };
        ph.transform_changed = [ &r, &s ]
        {
            std::uint8_t* t = race_session_image( r, 0, 0x50 );
            if ( !r.sections || !t )
            {
                ++s.stopped_transform;
                return;
            }
            boat_transform_changed( r.graph.objects[ 0 ].image.data( ), reinterpret_cast< const float* >( t + 0x50 ), r.body, r.io.camera,
                                    r.io.instigator, race_session_transform_hooks( r ) );
        };
        const bool known = race_boat_post_load( image, st.driver_type, entry, *db.constant_db, r.driver, ph );
        if ( !known )
        {
            LOG_WARNING( "race: %s's driver '%s' is UNKNOWN (no modelled class); no race", e.m_name.c_str( ), st.driver_type.c_str( ) );
            made = false;
        }
        s.boats.push_back( std::move( b ) );
    };
    if ( !race_load_boats( s.game, s.game_data, util, *db.track_db, registry, hooks ) || !made || s.boats.empty( ) )
    {
        LOG_WARNING( "race: the boat load 0x481CE0 made no usable boat; no race" );
        return false;
    }
    for ( const auto& b : s.boats )
    {
        {
            int records = 0, systems = 0;
            for ( const auto& l : b->pfx.list )
                for ( const boat_pfx_record_t& r : l )
                    records += 1, systems += r.system ? 1 : 0;
            LOG_INFO( "race: %s postLoad Pfx fill %s - %d records (lists %d/%d/%d/%d/%d/%d/%d), %d systems, hull effect '%s' system 0x%X, fluids '%s', "
                      "%d bone lookups UNKNOWN", b->name.c_str( ), b->pfx_filled ? "ran" : "NOT RUN", records,
                      static_cast< int >( b->pfx.list[ 0 ].size( ) ), static_cast< int >( b->pfx.list[ 1 ].size( ) ), static_cast< int >( b->pfx.list[ 2 ].size( ) ),
                      static_cast< int >( b->pfx.list[ 3 ].size( ) ), static_cast< int >( b->pfx.list[ 4 ].size( ) ), static_cast< int >( b->pfx.list[ 5 ].size( ) ),
                      static_cast< int >( b->pfx.list[ 6 ].size( ) ), systems, b->pfx.hull_effect.c_str( ), b->pfx.extra, b->pfx.fluids_asset.c_str( ),
                      b->pfx_unknown_bones );
        }
        if ( b->sections )
            LOG_SUCCESS( "race: %s (%s, skin %s): sections load 0x420A70 complete", b->name.c_str( ), b->boat.c_str( ), b->skin.c_str( ) );
        else
            LOG_WARNING( "race: %s (%s, skin %s): sections load 0x420A70 STOPPED (UNKNOWN) at %s - its physics, ticks and camera stop; the "
                         "hull is placed at its start transform", b->name.c_str( ), b->boat.c_str( ), b->skin.c_str( ), b->stop.c_str( ) );
    }
    if ( s.stopped_transform )
        LOG_WARNING( "race: the transform-changed delegate 0x421670 STOPPED (UNKNOWN) on %d boat(s) - it reads the sections load's +0x290 centre",
                     s.stopped_transform );
    // The tick manager (ctor 0x4F6F60, slot 5 0x4F7050) and the AI manager's tick.
    tick_manager_init( s.ticks );
    {
        std::vector< std::uint8_t > bytes;
        if ( archive.read( "VuDBAsset/InputDB", bytes ) )
        {
            s.input_db = c_reader( bytes ).parse( );
            s.input = std::make_shared< platform_input_t >( );
            platform_input_init( *s.input, s.input_db );
            if ( s.world.input_profile )
                s.world.input_profile( *s.input );   // 0x510E10: the manager's mappings from the profile
            tick_register( s.ticks, { s.input.get( ), race_input_tick, &s }, "Input" );
        }
        else
            LOG_WARNING( "race: VuDBAsset/InputDB not in the pack - the human's input answers 0 (UNKNOWN)" );
    }
    // VuEventManager's init slot 5 0x5055C0: its Decision tick 0x505950 - the engine init 0x566230 makes and inits it
    // (0x5663DD) before every system with a Decision tick (world/bus.hxx).
    tick_register( s.ticks, { &s.bus, race_bus_tick, &s.bus }, "Decision" );
    tick_register( s.ticks, { s.dynamics.get( ), race_session_dynamics_sync, &s }, "DynamicsSync" );   // 0x4F8050's registrations
    tick_register( s.ticks, { s.dynamics.get( ), race_session_dynamics_kick, &s }, "DynamicsKick" );
    // VuLensWaterManagerImpl's tick 0x514500 in "Build" (its init 0x513B90 at 0x513C0A - an engine system, made before the
    // game's; its order against the other engine systems' Build ticks is not read).
    s.lens = lens_water_manager_t{ };
    tick_register( s.ticks, { &s.lens, race_session_lens_tick, &s }, "Build" );
    // VuCollisionManager (g 0x758FBC): the game init 0x4024E0 creates it (0x4ACD30 at 0x4026B9: the ctor 0x4ACDF0) and calls
    // its init 0x4AD110 at 0x4026D4 - before the AI manager's (0x402828), so its Decision tick precedes the AI's. The init's
    // hooks: addContactListener (g_vudynamicsimpl slot 13) = this session's contact-added path hands every record to it
    // (world/drive.hxx race_contact_added); the asset manager's "SurfaceTableDB" = VuDBAsset/SurfaceTableDB's document from
    // the pack; the Decision registration. Its +0x14 = surface_count of the session's surface table (built above).
    collision_manager_construct( s.collision, s.collision_tables );
    {
        std::vector< std::uint8_t > bytes;
        if ( archive.read( "VuDBAsset/SurfaceTableDB", bytes ) )
            s.collision_db = c_reader( bytes ).parse( );
        else
            LOG_WARNING( "race: VuDBAsset/SurfaceTableDB not in the pack - the collision manager's tables load from the null container" );
        collision_init_hooks_t ch;
        ch.user = &s;
        ch.add_listener = []( void* u ) { static_cast< race_session_t* >( u )->collision_listening = true; };
        ch.asset = []( void* u, const char* name ) -> const value_t* {
            race_session_t& x = *static_cast< race_session_t* >( u );
            return std::strcmp( name, "SurfaceTableDB" ) == 0 && x.collision_db.tag == 6 ? &x.collision_db : nullptr;
        };
        ch.register_tick = []( void* u, const char* phase ) {
            race_session_t& x = *static_cast< race_session_t* >( u );
            tick_register( x.ticks, { &x.collision, race_session_collision, &x }, phase );
        };
        collision_manager_init( s.collision_tables, s.surfaces, ch );
        // VuTireTrackManager's init 0x5C40C0 (the engine's, 0x566CD4): the pools and skins; its "Final" tick on the session's ticks.
        tire_track_init( s.tire_tracks, s.surfaces, s.collision_db );
        tick_register( s.ticks, { &s.tire_tracks, []( void* u, float dt ) { tire_track_tick( static_cast< race_session_t* >( u )->tire_tracks, dt ); }, &s }, "Final" );
        LOG_INFO( "race: VuCollisionManager init 0x4AD110 - %d surface types, the four SurfaceTableDB tables loaded, its Decision tick registered",
                  static_cast< int >( s.collision_tables.types ) );
    }
    tick_register( s.ticks, { &s.fleet, race_session_fleet, &s }, "Decision" );
    tick_register( s.ticks, { &s.world, race_session_vo_tick, &s }, "Decision" );   // VuVoiceOverManagerImpl 0x4B8000
    // The game mode manager's Decision tick 0x4CBFD0 (its init 0x4CBCD0, slot 5): the action mode's Run state 0x4BAB50 ->
    // VuGame's tick 0x482F10 (race_session_game_mode). The order is read (world batch 21): the game init 0x4024E0 creates
    // each system, pushes it on the back of its list (0x403120) and calls its init vt+0x14 AT ONCE - the AI manager (g
    // 0x758F8C, 0x403310 at 0x402808, init 0x4042E0 at 0x402828: 'Decision' -> 0x404331's handler) long before the game
    // mode manager (g 0x758FF0, 0x4CBA30 at 0x402A6C, init 0x4CBCD0 at 0x402A90: 'Decision' at 0x4CBE4C); the list's
    // later walk (0x402AB2) calls each one's vt+4 - 0x401560 (nothing) for both. So the AI's Decision tick runs first.
    tick_register( s.ticks, { &s.rules, race_session_mode_tick, &s }, "Decision" );
    tick_register( s.ticks, { &s.manager, race_session_viewports, &s }, "Viewports" );
    // VuWater's tick 0x529E30 in "Water" (VuWater made by the engine init, before the race: c_world's
    // water_tick - surface.hxx water_built_tick and the queries' view).
    tick_register( s.ticks, { &s.world, race_session_water, &s }, "Water" );
    // The trigger manager's tick 0x536460 in "Triggers" (its init 0x536110, the engine's start).
    tick_register( s.ticks, { &s.triggers, race_session_triggers, &s }, "Triggers" );

    // The level's gameInitialize walk (after the tick manager: the gfx-settings entities register their Build ticks),
    // on the blender as the engine's init 0x52F620 leaves it.
    gfx_blender_init( s.blender );
    race_session_level_bodies( s, tree, archive );

    // The grid: every VuGridPositionEntity's transform world, through 0x43A740,
    // in the tree's pre-order (the project's gameInitialize walk; the list is
    // sorted by name after each add, so only equal names would depend on it).
    const auto walk = [ & ]( const auto& self, entity_t& e ) -> void
    {
        if ( e.m_type == "VuGridPositionEntity" && !e.m_world_unknown )
            if ( const transform_component_t* t = entity_transform( e ) )
            {
                race_grid_entry_t g;
                g.name = e.m_name;
                std::memcpy( g.world, t->world, 64 );
                race_grid_add( s.manager, g );
            }
        for ( auto& child : e.m_children )
            self( self, *child );
    };
    walk( walk, tree );

    // 0x4BA060 after the project's gameInitialize 0x500B10: 0x501260(root, "OnProjectInitialized") - the event every
    // VuOnProjectInitializedEntity answers by firing its "Trigger" (the world's graph) - then VuGame's begin.
    if ( project_initialized )
        project_initialized( );

    // VuGame's begin 0x4829A0.
    s.start.laps = s.game.laps;
    s.start.difficulty = s.game.difficulty;
    s.start.root_name = tree.m_name;
    // The delegates' users (stable addresses: the tick manager holds them).
    static std::vector< std::unique_ptr< race_session_tick_t > > tick_users;
    tick_users.clear( );
    for ( std::size_t k = 0; k < s.boats.size( ); ++k )
    {
        race_session_boat_t& b = *s.boats[ k ];
        tick_users.push_back( std::make_unique< race_session_tick_t >( race_session_tick_t{ &s, &b, k } ) );
        race_start_boat_t sb;
        sb.boat = b.graph.objects[ 0 ].image.data( );
        sb.transform = race_session_image( b, 0, 0x50 );
        sb.engine = b.engine.object.image;
        sb.driver = b.driver->graph[ 0 ].image.data( );
        const std::uint32_t vt = load_word( sb.driver, 0 );
        sb.cls = boat_driver_of( vt );
        boat_driver_kind( vt, sb.kind );
        s.start_boats.push_back( sb );
        s.fleet_boats.push_back( { reinterpret_cast< const float* >( sb.transform + 0x50 ), 0.0f, 0.0f } );
    }
    race_session_boat_events( s );
    if ( !track )
    {
        LOG_WARNING( "race: the world's track did not build; begin 0x4829A0 not run" );
        return false;
    }
    race_start_hooks_t sh;
    // The placement's notify: the transform's watcher +0xC0 = the transform-changed delegate 0x421670
    // (boat_transform_changed: the body to the new world), as the nuisance boats' place_child runs it.
    sh.transform_changed = [ &s ]( int k )
    {
        race_session_boat_t& r = *s.boats[ static_cast< std::size_t >( k ) ];
        std::uint8_t* t = race_session_image( r, 0, 0x50 );
        if ( !r.sections || !t )
        {
            ++s.stopped_transform;
            return;
        }
        boat_transform_changed( r.graph.objects[ 0 ].image.data( ), reinterpret_cast< const float* >( t + 0x50 ), r.body, r.io.camera,
                                r.io.instigator, race_session_transform_hooks( r ) );
    };
    sh.game_initialize = [ &s ]( int k )
    {
        race_session_boat_t& b = *s.boats[ static_cast< std::size_t >( k ) ];
        race_boat_init_hooks_t ih;
        ih.manager = &s.manager.lists;
        ih.entry = s.start_boats[ static_cast< std::size_t >( k ) ].boat;
        ih.kind = s.start_boats[ static_cast< std::size_t >( k ) ].kind;
        race_session_tick_t* user = tick_users[ static_cast< std::size_t >( k ) ].get( );
        ih.slot17 = [ &s, &b ] { race_session_enable( s, b, true ); };
        ih.instigator = [ &s, &b ] { race_session_register_instigator( s, b ); };
        ih.anim_create = [ &b ] { race_session_create_rig( b ); };
        // 0x419FF0 on +0x234 (boat effects.hxx boat_pfx_create, the boat agent's) when the Pfx fill was bound and ran.
        ih.pfx_create = [ &s, &b ] {
            if ( !b.pfx_filled )
                return;
            if ( !boat_pfx_create( b.pfx, race_session_pfx_hooks( b ) ) )
                race_unwired( s, "gameInitialize 0x419FF0: a pfx hook missing - UNKNOWN" );
            int systems = 0, records = 0;
            for ( const auto& l : b.pfx.list )
                for ( const boat_pfx_record_t& r : l )
                    ++records, systems += r.system ? 1 : 0;
            LOG_INFO( "race: %s gameInitialize 0x419FF0 - %d of %d records kept with a system, hull effect system 0x%X", b.name.c_str( ), systems, records, b.pfx.extra );
        };
        race_boat_game_initialize( &b, s.ticks, { &b, race_session_decision, user }, { &b, race_session_build, user }, b.draw_added, ih );
    };
    sh.add_driver = [ &s ]( int k )
    {
        // Slot 11 0x403590 (boat, 0, 0) for an AI boat's driver.
        fleet_pilot_t p;
        p.driver = s.start_boats[ static_cast< std::size_t >( k ) ].driver;
        p.boat = k;
        p.sector = s.start_boats[ static_cast< std::size_t >( k ) ].sector;
        s.pilots.push_back( p );
        fleet_boat_t& fb = s.fleet_boats[ static_cast< std::size_t >( k ) ];
        fb.engine_4 = state_f( s.start_boats[ static_cast< std::size_t >( k ) ].engine, 4 );
        fb.engine_c = state_f( s.start_boats[ static_cast< std::size_t >( k ) ].engine, 0xC );
        fleet_add_driver( s.fleet, s.fleet_boats, static_cast< int >( s.pilots.size( ) ) - 1, k, 0.0f, 0.0f );
    };
    sh.fleet_start = [ &s ]
    {
        fleet_start( s.fleet, *s.track, s.pilots, s.fleet_boats, *s.random );
    };
    sh.fleet_run = [ &s ] { fleet_run( s.fleet ); };
    // VuRaceGame's own part (world/rules.hxx): the ctor 0x489030's state machine, built here (after the base fields above -
    // the ctor ran before the boat load); its first state's enter is slot 5 0x4896A0's "state enter" step below, and its
    // tick the game's slot 7, reached from VuGame's tick 0x482F10 in the Decision phase (race_session_game_mode).
    // GameType "TimeTrial": VuTimeTrialGame's machine (rules.hxx timetrial_rules_construct, case timetrial_game_fsm), its
    // slots 13 / 5 in race_begin (start.hxx), its Game exit's TrackDB and the human's boat class (+0x160).
    s.start.timetrial = mode == "TimeTrial";
    s.start.slalom = mode == "Slalom";
    s.start.ghost = mode == "Ghost";
    if ( s.start.slalom )
        race_session_slalom( s, tree, db );
    else if ( s.start.ghost )
    {
        // VuGhostGame 0x484A20: VuGame's base machines (verified), then ITS machine +0x700 from the ctor's table (game_mode_defs,
        // case game_ctors) - GetLeaderboard runs; LoadGhosts 0x485B50 and behind it log UNKNOWN (rules.hxx game_mode_construct), so the
        // machine stops there. Retail's own slots that ARE read: 5 0x485340 (race_rules_start), 13
        // 0x487570 (start.hxx race_place_timetrial "Ghost"), 7 0x485380 (race_rules_tick, then the ghosts' matrix copy below),
        // 12 = nullsub 0x4021A0 (no results written), 15 0x487640 (drive.hxx: ui_race_checkpoint).
        vugame_fsms_construct( s.rules );
        s.rules.buoys = 0;
        game_mode_construct( s.rules.fsm, game_mode_defs( )[ 2 ], [ &s ]( std::uint32_t address, const char* what ) {
            if ( s.rules.unrecreated )
                s.rules.unrecreated( address, what );
        } );
        // GetLeaderboard, read whole. Enter 0x485890: the tick manager's vt+0x2C, every game boat's +0x308 += 1, the leaderboard
        // g 0x758FA8's vt+0x18 (the request: "Ghost" and the track), a "Ghost_Leaderboard" VuMessageBox +0x758 (UI: a hook). Exit
        // 0x485AA0: vt+0x30, +0x308 -= 1, the box released. Tick 0x485AF0: vt+0x1C's status 0 waits, 1 "Success", else +0x740 =
        // "Ghost_NotConnected" and the "Failure" pulse (ShowError).
        // PLATFORM (a hook, FLAGGED): the online leaderboard is not in this build, so vt+0x1C answers 2 - retail's answer with no
        // connection, the one path retail itself has for it. LoadGhosts 0x485B50 and on need the service: not reached.
        for ( auto& st : s.rules.fsm.m_states )
            if ( st->m_name == "GetLeaderboard" )
            {
                st->m_enter = [ &s ] {
                    race_rules_hold( s.rules, 1 );
                    race_unwired( s, "VuGhostGame GetLeaderboard: the tick manager's vt+0x2C and the \"Ghost_Leaderboard\" VuMessageBox +0x758 (UI, not recreated)" );
                };
                st->m_exit = [ &s ] { race_rules_hold( s.rules, -1 ); };
                st->m_tick = [ &s ]( float ) {
                    s.ghost_error = "Ghost_NotConnected";
                    s.rules.fsm.pulse_condition( "Failure" );
                };
            }
        // 0x485380 past the time: for i < the +0x75C list's count, the ghost manager g 0x758FC8's +0x64 record whose [0] == i:
        // its +0x58 entity's +0x4DC = its transform's world (+0x50 +0x50, 16 words). The list is LoadGhosts' / PreGame's
        // (0x485B50 / 0x486060, not recreated): empty here, so the loop runs over none - logged.
        s.rules.hooks.gates = [ &s ] {
            race_unwired( s, "VuGhostGame slot 7 0x485380: the +0x75C ghost list is empty (LoadGhosts 0x485B50 / PreGame 0x486060 not recreated) - "
                             "the per-ghost +0x4DC copy ran over none (UNKNOWN)" );
        };
        LOG_WARNING( "race: VuGhostGame - GetLeaderboard's handlers run; LoadGhosts 0x485B50 and every state behind it are not recreated (UNKNOWN)" );
    }
    else if ( s.start.timetrial )
    {
        timetrial_rules_construct( s.rules );
        s.rules.track_db = db.track_db;
        s.boat_db = db.boat_db;
        for ( std::size_t k = 0; k < s.start_boats.size( ); ++k )
            if ( s.start_boats[ k ].cls.slot[ 0 ] )
            {
                const auto it = s.boats[ k ]->io.boat_strings.find( 0x160 );
                s.rules.human_boat = it == s.boats[ k ]->io.boat_strings.end( ) ? std::string( ) : it->second;
                break;
            }
        LOG_INFO( "race: VuTimeTrialGame - the human's +0x160 '%s' keys TrackDB [%s][TimeTrial]", s.rules.human_boat.c_str( ), s.game.track.c_str( ) );
    }
    else
        race_rules_construct( s.rules );
    s.rules.hooks.hud = [ &s ]( int project, race_hud_verb_t verb, float dt )
    {
        if ( s.world.hud )
            s.world.hud( project, verb, dt );
        else
            race_unwired( s, "the race HUD projects (VuRaceGame +0x738 / +0x73C / +0x740) with no world to load them - not run (UNKNOWN)" );
    };
    s.rules.hooks.broadcast = [ &s ]( const char* name, bool value )
    {
        // The VuParams 0x48AB70 builds: one bool (0x4051A0: type 3, the byte - 5 bytes).
        ui_params_t p;
        ui_params_add_bool( p, value );
        race_session_post( s, name, &p );
    };
    s.rules.hooks.platform = [ &s ]( const std::string& call ) {
        // The voice-over lines (vt+0x1C "vo", vt+0x20 "vo20") on the human (+0x24 [0]) go to the manager (world/voice.hxx).
        const bool now = call.compare( 0, 3, "vo " ) == 0, queued = call.compare( 0, 5, "vo20 " ) == 0;
        if ( now || queued )
        {
            race_session_vo_say( s, s.rules.humans.empty( ) ? nullptr : s.rules.humans[ 0 ], call.c_str( ) + ( now ? 3 : 5 ), queued );
            return;
        }
        race_unwired( s, "platform services: " + call + " (saves / online / achievements - hooks)" );
    };
    s.rules.hooks.record = [ &s ]( const char* type, const std::string& course, std::int32_t place, float total, float lap ) {
        if ( !s.world.save_doc )
            return;
        std::string boat_class;
        for ( std::size_t k = 0; k < s.rules_boats.size( ) && k < s.boats.size( ); ++k )
            if ( s.rules_boats[ k ].human )
            {
                const auto it = s.boats[ k ]->io.boat_strings.find( 0x160u );
                boat_class = it == s.boats[ k ]->io.boat_strings.end( ) ? std::string( ) : it->second;
                break;
            }
        record_put( *s.world.save_doc, type, s.game.track, course, boat_class, 0, place, total, lap );
        LOG_INFO( "saves: %s record, %s class %s: place %d, %.3f, lap %.3f", type, s.game.track.c_str( ), boat_class.c_str( ), place, total, lap );
    };
    s.rules.track = s.game.track;
    {
        // VuGame's ctor 0x481630: +0x674 = ConstantDB VuDBAsset / HUD / ChallengeTextDuration - an int (1, cvtdq2ps), a float (2), an int64
        // (7, fild / fstp), else 0.
        const value_t& v = member( member( member( *db.constant_db, "VuDBAsset" ), "HUD" ), "ChallengeTextDuration" );
        s.rules.split_duration = v.tag == 1 ? static_cast< float >( v.integer ) : v.tag == 2 ? v.real : v.tag == 7 ? static_cast< float >( v.int64 ) : 0.0f;
    }
    s.rules.unrecreated = [ &s ]( std::uint32_t address, const char* what )
    {
        char text[ 160 ];
        std::snprintf( text, sizeof( text ), "%s 0x%X - not recreated, not run (UNKNOWN)", what, address );
        race_unwired( s, text );
    };
    // VuGame's Results tick 0x484280: the game's vt+0x30 = VuRaceGame slot 12 0x489C60 (rules.hxx race_game_write_results)
    // into gameutil +0x50, then that root's "GameResults" type (0x4FBDF0: a non-map root or an absent name = the static
    // null container, type 0).
    // Awards' enter 0x484320: the services are saves-side (the user's scope: re-made, not copied; none exists yet). No hidden package
    // found, no unlock (so no award screen and "AwardsDone" on the first tick) - logged once each state entry.
    s.rules.game_mode = s.start.slalom ? "Slalom" : s.start.timetrial ? "TimeTrial" : s.start.ghost ? "Ghost" : "Race";
    s.rules.screen_hooks.award.hidden_found = [ &s ]( const std::string& track ) {
        return s.world.save_doc && s.world.hidden_db && package_all_found( *s.world.save_doc, *s.world.hidden_db, track );
    };
    s.rules.screen_hooks.award.progress = [ &s ]( const std::string& boat ) {
        if ( s.world.save_doc )
            ui_progress_boat_used( *s.world.save_doc, boat );
    };
    s.rules.screen_hooks.award.unlock_value = [ ]( std::uint32_t, const std::string&, std::int32_t ) { return 0; };
    s.rules.screen_hooks.award.unlock_show = [ ]( std::int32_t, const std::vector< std::string >& ) { return false; };
    s.rules.screen_hooks.award.award_open = [ &s ]( const char*, std::int32_t, const std::string&, std::int32_t, const std::vector< std::string >& ) {
        race_unwired( s, "the award screen 0x47ECC0 (UI) - not built" );
        return false;
    };
    s.rules.championship = s.game.championship;
    s.rules.screen_hooks.award_boat = [ &s ] {
        vugame_award_boat_t b;
        for ( std::size_t k = 0; k < s.rules_boats.size( ) && k < s.boats.size( ); ++k )
            if ( s.rules_boats[ k ].human )
            {
                const race_session_boat_t& rb = *s.boats[ k ];
                b.name = rb.boat;
                const auto cls = rb.io.boat_strings.find( 0x160u );
                b.boat_class = cls == rb.io.boat_strings.end( ) ? std::string( ) : cls->second;
                b.tier = rules_i32( s.rules_boats[ k ].boat, 0x4CC );
                break;
            }
        return b;
    };
    s.rules.screen_hooks.game_results_tick = [ &s ]( float dt )
    {
        const auto entry_of = [ &s ]( std::size_t k ) {
            race_results_entry_t e;
            if ( k < s.boats.size( ) )
            {
                const race_session_boat_t& b = *s.boats[ k ];
                if ( b.driver )
                {
                    const auto it = b.driver->strings.find( 0x60 );
                    e.driver_name = it == b.driver->strings.end( ) ? std::string( ) : it->second;
                }
                const auto cls = b.io.boat_strings.find( 0x160 ), id = b.io.boat_strings.find( 0x178 );
                e.boat_class = cls == b.io.boat_strings.end( ) ? std::string( ) : cls->second;
                e.boat_name = id == b.io.boat_strings.end( ) ? std::string( ) : id->second;
            }
            if ( k < s.start_boats.size( ) )
            {
                e.human = s.start_boats[ k ].cls.slot[ 0 ];   // vt+0x30
                e.local = s.start_boats[ k ].cls.slot[ 2 ];   // vt+0x38
            }
            return e;
        };
        if ( s.start.ghost )
            return;   // VuGhostGame slot 12 = nullsub 0x4021A0: nothing written
        if ( s.start.timetrial || s.start.slalom )
        {
            // VuSlalomGame slot 12 0x48CAF0 is the same writer (in.slalom: case slalom_results).
            // VuTimeTrialGame slot 12 0x48F560 (world/modes.hxx timetrial_write_results, case timetrial_results): the boat
            // manager's +0x18 / +0x24 lists' first boats (by image) as the racer / the human.
            const auto first_of = [ &s, &entry_of ]( const boat_manager_list_t& l ) {
                timetrial_results_boat_t b;
                for ( std::size_t k = 0; !l.boats.empty( ) && k < s.boats.size( ); ++k )
                    if ( l.boats[ 0 ] == s.boats[ k ]->graph.objects[ 0 ].image.data( ) )
                        b = { s.boats[ k ]->graph.objects[ 0 ].image.data( ), entry_of( k ) };
                return b;
            };
            timetrial_results_in_t in;
            in.championship = s.game.championship;
            in.circuit = s.rules_game.circuit;
            in.laps = s.start.laps;
            in.track = s.game.track;
            in.racer = first_of( s.manager.lists.racers );
            in.human = first_of( s.manager.lists.humans );
            in.track_db = s.rules.track_db;
            in.boat_db = s.boat_db;
            in.string_db = s.world.string_db;
            in.random = s.random;
            in.slalom = s.start.slalom;
            if ( !in.racer.boat || !in.human.boat )
            {
                race_unwired( s, "0x48F560: the boat manager's +0x18 / +0x24 list has no first boat (retail reads [0] of an empty list: UNKNOWN) - not run" );
                return;
            }
            if ( in.championship )
            {
                race_unwired( s, "0x48F560 in a championship: the progress manager's vt+0x6C (the profile's round, a saves-side hook) - not run (UNKNOWN)" );
                return;
            }
            if ( !timetrial_write_results( s.util, s.timetrial_written, in ) )
                race_unwired( s, "0x48F560: a path not recreated (no VuStringDB / VuRandom bound, a negative place) - logged UNKNOWN" );
        }
        else
        {
            std::vector< race_results_entry_t > entries( s.rules_boats.size( ) );
            for ( std::size_t k = 0; k < entries.size( ); ++k )
                entries[ k ] = entry_of( k );
            if ( !race_game_write_results( s.util, s.rules_game, s.rules_boats, entries, dt ) )
                race_unwired( s, "0x489C60: the places over more than 32 boats or a negative place (UNKNOWN) - that row not written" );
        }
        if ( !s.results_logged )
        {
            s.results_logged = true;
            const value_t& results = ui_json_find( s.util, "GameResults" );
            const value_t& table = ui_json_find( results, "Table" );
            LOG_INFO( "race: GameResults written (%s) - CircuitRace %d, %d rows",
                      s.start.slalom ? "VuSlalomGame slot 12 0x48CAF0" : s.start.timetrial ? "VuTimeTrialGame slot 12 0x48F560" : "VuRaceGame slot 12 0x489C60",
                      ui_json_find( results, "CircuitRace" ).integer,
                      table.tag == 5 ? static_cast< int >( table.list.size( ) ) : -1 );
            for ( std::size_t k = 0; table.tag == 5 && k < table.list.size( ); ++k )
            {
                const value_t& row = table.list[ k ];
                LOG_INFO( "race: results row %d: DriverName '%s' Boat '%s' BoatClass '%s' TotalTime %.3f BestLapTime %.3f LapsCompleted %d IsFinished %d "
                          "IsHuman %d IsLocal %d PlayerUID %d%s",
                          static_cast< int >( k ) + 1, ui_json_text( ui_json_find( row, "DriverName" ) ).c_str( ), ui_json_text( ui_json_find( row, "Boat" ) ).c_str( ),
                          ui_json_text( ui_json_find( row, "BoatClass" ) ).c_str( ), ui_json_find( row, "TotalTime" ).real,
                          ui_json_find( row, "BestLapTime" ).real, ui_json_find( row, "LapsCompleted" ).integer, ui_json_find( row, "IsFinished" ).integer,
                          ui_json_find( row, "IsHuman" ).integer, ui_json_find( row, "IsLocal" ).integer, ui_json_find( row, "PlayerUID" ).integer,
                          ui_json_find( row, "PadIndex" ).tag == 1 ? " PadIndex 0" : "" );
                if ( ui_json_find( row, "TierEntry" ).tag == 3 || ui_json_find( row, "TierAchieved" ).tag == 1 )
                    LOG_INFO( "race: results row %d: Tier %d TierName '%s' TierTime %.3f TierAchieved %d", static_cast< int >( k ) + 1,
                              ui_json_find( row, "Tier" ).integer, ui_json_text( ui_json_find( row, "TierName" ) ).c_str( ),
                              ui_json_find( row, "TierTime" ).real, ui_json_find( row, "TierAchieved" ).integer );
            }
        }
    };
    s.rules.screen_hooks.json_type = [ &s ]( const char* name ) { return static_cast< int >( ui_json_find( s.util, name ).tag ); };
    for ( std::size_t k = 0; k < s.game.boats.size( ) && k < s.start_boats.size( ); ++k )
    {
        race_rules_boat_t rb;
        rb.boat = s.start_boats[ k ].boat;
        rb.transform = s.start_boats[ k ].transform;
        rb.driver = s.start_boats[ k ].driver;
        rb.human = s.start_boats[ k ].cls.slot[ 0 ];
        s.rules_boats.push_back( rb );
    }
    s.rules.boats = &s.rules_boats;
    s.rules.game = &s.rules_game;
    sh.call = [ &s, &db ]( const char* row )
    {
        // Slot 5 0x4896A0: "(whatever the track) the game's +0x6F8 state machine enters its first state" - PreGame's enter
        // 0x48A7F0, before the +0x4CC loop (which it does not read).
        // VuGame's begin 0x4829A0: its two machines (+0x0C from +0x18, +0x67C from +0x688) enter their first states.
        if ( std::strcmp( row, "state enter fsm+0x18" ) == 0 )
            vugame_fsm_begin( s.rules.event );
        if ( std::strcmp( row, "state enter fsm+0x688" ) == 0 )
            vugame_fsm_begin( s.rules.split );
        if ( std::strcmp( row, "slalom slot 5" ) == 0 )
        {
            s.rules_game.circuit = s.start.circuit;
            s.rules_game.length = s.start.length;
            s.rules_game.laps = s.start.laps;
            race_session_slalom_start( s, db );
        }
        if ( std::strcmp( row, "state enter fsm+0x704" ) == 0 )
        {
            s.rules_game.circuit = s.start.circuit;
            s.rules_game.length = s.start.length;
            s.rules_game.laps = s.start.laps;
            race_rules_start( s.rules );
        }
    };
    race_begin( s.start, s.start_boats, s.manager, *track, *db.track_db, *db.constant_db, sh );
    // The game's +0x720 order as slot 5 left it, and each boat's +0x4D8 (race_start_boat_t::checkpoint) as begin set it.
    s.rules_game.order = s.start.order;
    s.rules_game.order.resize( s.rules_boats.size( ) );
    for ( std::size_t k = 0; k < s.rules_boats.size( ); ++k )
        s.rules_boats[ k ].checkpoint = s.start_boats[ k ].checkpoint;
    s.rules_running = true;

    // VuAiNuisanceDriverStartEntity's gameInitialize 0x42FDC0 (boat nuisance.hxx) on each one in the tree, in
    // pre-order: its child VuBoatEntity (operator new(0x520) + 0x41B710 + ctor 0x41B850 - the object the
    // registry's create 0x41B690 makes) named name + "Boat", loaded from the slot's document (VuEntity::load
    // 0x5009D0, then the postLoad 0x41CFF0 above: a VuAiNuisanceDriver named name + "Driver"), hashed from
    // 0x811C9DC5, put at the start entity's world by 0x5344A0(notify 1), then initialised (0x500B10 -> the
    // boat's 0x41D350). ORDER NOT READ: where the project's gameInitialize walk falls against VuGame's begin -
    // the children are made after begin here (flagged).
    // 0x5344A0 (thiscall transform; matrix*, notify; retn 8): world +0x50 = the matrix; +0xA0 = (atan2(m6,
    // m10), asinf(clamp(-m2)), atan2(m1, m0)) (respawn_euler), +0xAC a stack word (UNKNOWN - 0 here); 0x534830
    // with no parent (local +0x10 = world, +0x90 = the euler); 0x534C00(notify) - the boat has no children;
    // with notify the watcher +0xC0 = the transform-changed delegate 0x421670.
    {
        entity_world_t bw = race_boat_world( registry, hooks );
        std::vector< entity_t* > starts;
        const auto find = [ & ]( const auto& self, entity_t& e ) -> void
        {
            if ( e.m_type == k_nuisance_start_class && e.m_state )
                starts.push_back( &e );
            for ( auto& child : e.m_children )
                self( self, *child );
        };
        find( find, tree );
        for ( entity_t* start : starts )
        {
            const transform_component_t* st = entity_transform( *start );
            std::unique_ptr< entity_t > child = entity_create( "VuBoatEntity", bw );
            if ( !child || !st || start->m_world_unknown )
            {
                LOG_WARNING( "race: %s's nuisance boat 0x42FDC0 STOPPED (UNKNOWN) - %s", start->m_name.c_str( ),
                             child ? "the start entity's world is unknown" : "VuBoatEntity could not be created" );
                continue;
            }
            const std::size_t before = s.boats.size( );
            nuisance_start_hooks_t nh;
            nh.name_child = [ & ]( const std::string& n ) { child->m_name = n; };
            nh.load_child = [ & ]( const value_t& doc ) {
                entity_load( *child, &doc, bw );
                if ( hooks.post_load )
                    hooks.post_load( *child, doc );
            };
            nh.hash_child = [ & ]( std::uint32_t seed ) { race_entity_hash( *child, seed ); };
            nh.place_child = [ & ]( bool notify ) {
                if ( s.boats.size( ) != before + 1 )
                    return;
                race_session_boat_t& r = *s.boats[ before ];
                std::uint8_t* t = race_session_image( r, 0, 0x50 );
                if ( !t )
                    return;
                std::memcpy( t + 0x50, st->world, 64 );
                float e[ 3 ];
                respawn_euler( st->world, e );
                std::memcpy( t + 0xA0, e, 12 );
                image_put32( t, 0xAC, 0 );   // a stack word in retail - UNKNOWN
                std::memmove( t + 0x10, t + 0x50, 64 );
                std::memmove( t + 0x90, t + 0xA0, 16 );
                if ( notify && r.sections )
                    boat_transform_changed( r.graph.objects[ 0 ].image.data( ), reinterpret_cast< const float* >( t + 0x50 ), r.body, r.io.camera,
                                            r.io.instigator, race_session_transform_hooks( r ) );
                else if ( notify )
                    ++s.stopped_transform;
            };
            nh.add_child = [ & ] {
                if ( s.boats.size( ) != before + 1 || !s.boats[ before ]->driver || s.boats[ before ]->driver->graph.empty( ) )
                    return;
                race_session_boat_t& b = *s.boats[ before ];
                tick_users.push_back( std::make_unique< race_session_tick_t >( race_session_tick_t{ &s, &b, before } ) );
                race_start_boat_t sb;
                sb.boat = b.graph.objects[ 0 ].image.data( );
                sb.transform = race_session_image( b, 0, 0x50 );
                sb.engine = b.engine.object.image;
                sb.driver = b.driver->graph[ 0 ].image.data( );
                const std::uint32_t vt = load_word( sb.driver, 0 );
                sb.cls = boat_driver_of( vt );
                boat_driver_kind( vt, sb.kind );
                s.start_boats.push_back( sb );
                s.fleet_boats.push_back( { reinterpret_cast< const float* >( sb.transform + 0x50 ), 0.0f, 0.0f } );
                race_boat_init_hooks_t ih;
                ih.manager = &s.manager.lists;
                ih.entry = sb.boat;
                ih.kind = sb.kind;
                race_session_tick_t* user = tick_users.back( ).get( );
                ih.slot17 = [ &s, &b ] { race_session_enable( s, b, true ); };
                ih.instigator = [ &s, &b ] { race_session_register_instigator( s, b ); };
                ih.anim_create = [ &b ] { race_session_create_rig( b ); };
                // 0x419FF0 on +0x234 (boat effects.hxx boat_pfx_create, the boat agent's) when the Pfx fill was bound and ran.
                ih.pfx_create = [ &s, &b ] {
                    if ( !b.pfx_filled )
                        return;
                    if ( !boat_pfx_create( b.pfx, race_session_pfx_hooks( b ) ) )
                        race_unwired( s, "gameInitialize 0x419FF0: a pfx hook missing - UNKNOWN" );
                };
                race_boat_game_initialize( &b, s.ticks, { &b, race_session_decision, user }, { &b, race_session_build, user }, b.draw_added, ih );
            };
            nuisance_start_game_initialize( start->m_name, *static_cast< const nuisance_start_t* >( start->m_state.get( ) ), nh );
            s.nuisance_by_path[ race_entity_path( start ) ] = { static_cast< nuisance_start_t* >( start->m_state.get( ) ),
                                                                s.boats.size( ) == before + 1 ? static_cast< int >( before ) : -1 };
            if ( s.boats.size( ) == before + 1 )
                LOG_INFO( "race: nuisance boat %s (%s) from %s (0x42FDC0)", child->m_name.c_str( ), s.boats[ before ]->boat.c_str( ),
                          start->m_name.c_str( ) );
            s.nuisance.push_back( std::move( child ) );
        }
        if ( !starts.empty( ) )
            LOG_INFO( "race: %d VuAiNuisanceDriverStartEntity, %d nuisance boat(s) made", static_cast< int >( starts.size( ) ),
                      static_cast< int >( s.nuisance.size( ) ) );
    }
    // The model's words for the weak references (race_session_t::words): each boat's +0x220 = its driver, the
    // driver's +0 = its vtable.
    for ( std::size_t k = 0; k < s.boats.size( ); ++k )
        if ( s.boats[ k ]->driver && !s.boats[ k ]->driver->graph.empty( ) )
        {
            const std::uint32_t e = race_entity_token( static_cast< int >( k ) );
            s.words[ e + 0x220 ] = e + 0x1000u;
            s.words[ e + 0x1000u ] = load_word( s.boats[ k ]->driver->graph[ 0 ].image.data( ), 0 );
        }
    race_drive_setup( s, db.constant_db );
    LOG_INFO( "race: the dynamics world holds %d rigid bod(ies) / %d collision object(s) / %d step callback(s) after the boats' slot 17 0x421980; gravity z 0x%08X",
              static_cast< int >( s.dynamics->world.bodies.size( ) ), static_cast< int >( s.dynamics->collision.objects.size( ) ),
              static_cast< int >( s.dynamics->impl.step_callbacks.size( ) ), [ & ] { std::uint32_t u; std::memcpy( &u, &s.dynamics->world.gravity[ 2 ], 4 ); return u; }( ) );
    int placed = 0;
    for ( const race_start_boat_t& b : s.start_boats )
        placed += b.transform ? 1 : 0;
    LOG_SUCCESS( "race: %s on %s - %d boat(s) from GameData, %d grid position(s), %d lap(s), difficulty %s, begin 0x4829A0 run "
                 "(humans %d, AI %d)", mode.c_str( ), s.game.track.c_str( ), static_cast< int >( s.boats.size( ) ),
                 static_cast< int >( s.manager.grid.size( ) ), s.game.laps, s.game.difficulty.c_str( ),
                 static_cast< int >( s.manager.lists.humans.boats.size( ) ), static_cast< int >( s.manager.lists.ais.boats.size( ) ) );
    s.active = true;
    return true;
}

// Per frame: VuTickManagerImpl's tick 0x4F72F0 with the clock advanced by our
// frame time (the clock g_vumetrosys vt+0x28 is the platform layer).
inline void race_session_frame( race_session_t& s, float dt )
{
    if ( !s.active )
        return;
    s.clock += static_cast< double >( dt );
    tick_run( s.ticks, s.clock );
    ++s.frames;
}

// VuBoatEntity's draw 0x41EAE0 (slot 12, behind the draw delegate 0x4077E3),
// its model choice on the normal pass: nothing when the pass's +0x2D is set,
// the debug draw 0x41ED20 when g 0x758F92 is (dev only - not modelled), else
// 0x4121C0 (the damage pieces - a hook), nothing when +0x464 && +0x465, no
// hull when +0x46B; the matrix = the transform's world (+0x50) . 0x410AB0
// (translation by boat +0x49C) - 0x4078C0 with the world as `this`; d = the
// squared distance of the transform's +0x80 origin from the eye ((y + x) + z
// order); the reflection pass (+0x18) takes instance 2 unless +0x466; else
// d > (+0x110)^2 instance 2 (+0x2AC, the static LOD 2), d > (+0x10C)^2
// instance 1 (+0x2A8), else instance 0 (+0x2A4). -1: no hull drawn. The
// instance colour (0x41B310: +0x114 / +0x118 lerped by +0x2B4) and the alpha
// byte (+0x2C8 x 0x6F7808, 0x80 with +0x466) are not consumed by our renderer.
inline int race_boat_draw_instance( const std::uint8_t* boat, const std::uint8_t* transform, const float* eye )
{
    if ( ( boat[ 0x464 ] && boat[ 0x465 ] ) || boat[ 0x46B ] )
        return -1;
    const float dx = state_f( transform, 0x80 ) - eye[ 0 ];
    const float dy = state_f( transform, 0x84 ) - eye[ 1 ];
    const float dz = state_f( transform, 0x88 ) - eye[ 2 ];
    const float d = ( dy * dy + dx * dx ) + dz * dz;
    const float lod2 = state_f( boat, 0x110 ), lod1 = state_f( boat, 0x10C );
    if ( _mm_comigt_ss( _mm_set_ss( d ), _mm_set_ss( lod2 * lod2 ) ) )
        return 2;
    if ( _mm_comigt_ss( _mm_set_ss( d ), _mm_set_ss( lod1 * lod1 ) ) )
        return 1;
    return 0;
}

// 0x41EAE0's tail: the controller's draw 0x419CE0 on the normal pass, reached unless (+0x464 && +0x465) (its +0x46B branch
// lands on it too). What it draws, boat by boat in the race's order.
inline void race_session_pfx_draw( const race_session_t& s, std::vector< std::uint32_t >& out )
{
    out.clear( );
    if ( !s.active )
        return;
    for ( const auto& b : s.boats )
    {
        const std::uint8_t* boat = b->graph.objects[ 0 ].image.data( );
        if ( !boat[ 0x464 ] || !boat[ 0x465 ] )
            boat_pfx_draw_systems( b->pfx, boat[ 0x46B ], false, out );
    }
}

inline void race_boat_draw_matrix( const std::uint8_t* boat, const std::uint8_t* transform, float* out )
{
    float world[ 16 ], tr[ 16 ];
    std::memcpy( world, transform + 0x50, 64 );
    hull_translation( tr, reinterpret_cast< const float* >( boat + 0x49C ) );
    hull_multiply( world, out, tr );
}
