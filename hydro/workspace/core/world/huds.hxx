#pragma once

// c_world's race HUD projects - member definitions (one class, split by concern).
//
// VuRaceGame's ctor 0x489030 loads three projects through 0x4B4360 (the same
// VuProject load the race project takes: AssetData "File", VuProject::load
// 0x556390): +0x738 HUDs/RacePreGameHUD, +0x73C HUDs/RaceGameHUD, +0x740
// HUDs/RacePostGameHUD. Its states initialise / release / tick them (world/
// rules.hxx). Here they are loaded AS DATA - the entity trees through the
// verified loader, their event graphs merged into m_graph (each path prefixed
// "<project>:" so no HUD path can meet a level path) - and run as far as the
// world's graph runs: the PreGame HUD's #HUD/Countdown is a VuMultiDelayEntity
// (Delays 1 / 2 / 3 / 4 s) started by OnProjectInitialized, whose output "4"
// triggers VuStartLightsOutAction -> the "OnStartLightsOut" broadcast. Every
// HUD element (VuUITextEntity / VuHUDElementEntity Show / Hide, the HUD's
// VuHUDEntity tick vt+0x30 / draw vt+0x34, VuAudioEventEntity Start) is the UI
// agent's / the audio layer's: counted unhandled or logged, never drawn here.

inline void c_world::load_huds( const char* const* k_names )   // the game ctor's three loads (world/rules.hxx)
{
    m_multi_delays.clear( );
    m_start_lights.clear( );
    for ( int p = 0; p < 3; ++p )
    {
        hud_project_t& h = m_huds[ p ];
        h = hud_project_t{ };
        h.name = k_names[ p ];
        const std::string asset = std::string( "VuProjectAsset/" ) + k_names[ p ];
        std::vector< std::uint8_t > bytes;
        if ( !m_archive.read( asset.c_str( ), bytes ) )
        {
            LOG_WARNING( "race HUD: %s not in the pack - not loaded (UNKNOWN)", asset.c_str( ) );
            continue;
        }
        const value_t root = c_reader( bytes ).parse( );
        const value_t* project = find( root, "VuProject" );
        if ( project )
            h.tree = race_load_tree( m_archive, asset.c_str( ), *project, [ this ]( const std::string& name ) { return template_document( name ); } );
        if ( !h.tree )
        {
            LOG_WARNING( "race HUD: VuProject::load failed on %s - not loaded (UNKNOWN)", asset.c_str( ) );
            continue;
        }
        const std::string prefix = h.name + ":";
        // The UI agent's driver (core/ui overlay.hxx): the same document through the UI's scripted world.
        {
            const entity_registry_t registry = build_entity_registry( m_archive.names( "VuTemplateAsset/" ) );
            h.ui_tree = ui_hud_load( *project, race_project_file( m_archive, asset.c_str( ) ), registry,
                                     [ this ]( const std::string& name ) { return template_document( name ); } );
            if ( h.ui_tree )
            {
                const auto trails = [ & ]( const auto& self, entity_t& e, const std::string& trail ) -> void
                {
                    h.ui_trail[ &e ] = trail;
                    for ( auto& child : e.m_children )
                        self( self, *child, trail.empty( ) ? child->m_name : trail + "/" + child->m_name );
                };
                trails( trails, *h.ui_tree, std::string( ) );
                ui_bind_hooks_t hooks;
                const int index = p;
                // The world-owned inputs: VuMultiDelayEntity "In" 0x5E0660 (the world's clock); VuAudioEventEntity Start /
                // Stop (the audio device - a platform hook).
                hooks.m_external = [ this, index, prefix ]( entity_t& e, const char* plug, const ui_params_t& params ) {
                    hud_project_t& hp = m_huds[ index ];
                    const auto at = hp.ui_trail.find( &e );
                    const auto clock = at != hp.ui_trail.end( ) ? m_multi_delays.find( prefix + at->second ) : m_multi_delays.end( );
                    if ( clock != m_multi_delays.end( ) && std::strcmp( plug, "In" ) == 0 )
                        multi_delay_in( clock->second.state );
                    else
                        return screen_external( m_game_screen_state, hp.ui->binding( ), e, plug, params );
                    return ui_ret_t{ };
                };
                hooks.m_broadcast_out = [ this, index ]( std::uint32_t hash, const ui_params_t& bp ) {
                    if ( m_hud_posting )
                        return;   // the world's own post into this HUD (hud_post) coming back out: already delivered
                    race_broadcast_hash( hash, hash == fsm_hash( "OnStartLightsOut" ) ? "OnStartLightsOut" : nullptr, bp, index );
                };
                // m_string_db: VuStringDB's getString (slot 6 0x53B830, world/strings.hxx) on the world's string DB.
                string_db_ready( );
                hooks.m_string_db = [ this ]( const char* key ) { return string_db_get( m_string_db, key ? key : "" ); };
                hooks.m_input = [ this ]( int pad, const char* name ) { return m_race.input && m_race.input->manager.pressed( pad, name ); };
                hooks.m_touch_device = &m_touch;
                h.ui = std::make_unique< c_ui_race_hud >( *h.ui_tree, hooks );
            }
            else
                LOG_WARNING( "race HUD: %s did not load through the UI's world - the world graph runs it (fallback)", h.name.c_str( ) );
        }
        std::map< const entity_t*, std::string > path_of;
        const auto name_all = [ & ]( const auto& self, entity_t& e, const std::string& trail ) -> void
        {
            path_of[ &e ] = trail;
            for ( auto& child : e.m_children )
                self( self, *child, trail.empty( ) ? child->m_name : trail + "/" + child->m_name );
        };
        name_all( name_all, *h.tree, std::string( ) );
        int entities = 0, unknown = 0, links = 0;
        const auto walk = [ & ]( const auto& self, entity_t& e, const std::string& trail ) -> void
        {
            const std::string at = prefix + trail;
            if ( !trail.empty( ) )
            {
                ++entities;
                unknown += e.m_ctor ? 0 : 1;
                m_entity_type[ at ] = e.m_type;
                if ( const script_component_t* sc = h.ui ? nullptr : script_of( e ) )   // with the UI driver its graph runs the HUD
                    for ( const script_plug_t& plug : sc->m_plugs )
                    {
                        if ( plug.m_links.empty( ) )
                            continue;
                        std::vector< edge_t >& edges = m_graph[ std::make_pair( at, std::string( plug.m_row->m_name ) ) ];
                        for ( const script_link_t& link : plug.m_links )
                            edges.push_back( edge_t{ prefix + path_of[ link.m_entity ], script_of( *link.m_entity )->m_plugs[ link.m_plug ].m_row->m_name } );
                        links += static_cast< int >( plug.m_links.size( ) );
                    }
                if ( e.m_ctor && e.m_type == "VuOnProjectInitializedEntity" )
                    h.initialized.push_back( at );
                if ( e.m_ctor && e.m_type == "VuStartLightsOutAction" )
                    m_start_lights.insert( at );
                if ( e.m_type == k_multi_delay_class && e.m_state )
                {
                    multi_delay_node_t& n = m_multi_delays[ at ];
                    std::memcpy( n.state.delay, static_cast< const multi_delay_state_t* >( e.m_state.get( ) )->value, sizeof( n.state.delay ) );
                    n.world = this;
                    n.path = at;
                    h.delays.push_back( at );
                }
            }
            for ( auto& child : e.m_children )
                self( self, *child, trail.empty( ) ? child->m_name : trail + "/" + child->m_name );
        };
        walk( walk, *h.tree, std::string( ) );
        LOG_INFO( "race HUD: %s loaded as data - %d entities (%d UNKNOWN types), %d graph links, %d OnProjectInitialized, %d multi delays",
                  h.name.c_str( ), entities, unknown, links, static_cast< int >( h.initialized.size( ) ), static_cast< int >( h.delays.size( ) ) );
    }
}

// VuMultiDelayEntity's Decision tick 0x5E06B0 (registered by its gameInitialize 0x5E0610).
inline void c_world::multi_delay_ticked( void* user, float dt )
{
    multi_delay_node_t& n = *static_cast< multi_delay_node_t* >( user );
    multi_delay_tick( n.state, dt, [ & ]( int output, const std::uint8_t* params ) {
        LOG_INFO( "race HUD: %s output %d fired (Delay %.2f)", n.path.c_str( ), output + 1, n.state.delay[ output ] );
        for ( hud_project_t& h : n.world->m_huds )
            if ( h.ui && n.path.compare( 0, h.name.size( ) + 1, h.name + ":" ) == 0 )
            {
                ui_params_t p;
                if ( params )
                {
                    std::memcpy( p.m_bytes, params, 128 );
                    std::memcpy( &p.m_size, params + 128, 4 );
                }
                h.ui->fire( h.ui_tree->m_name + "/" + n.path.substr( h.name.size( ) + 1 ), std::to_string( output + 1 ).c_str( ), p );
                return;
            }
        n.world->fire( n.path, std::to_string( output + 1 ), 0, params );
    } );
}

// The state handlers' HUD verbs (world/rules.hxx race_rules_hooks_t::hud).
// initialize: when the root's +0x14 bit 2 is clear (not yet live) - the
// repository add (g_vu_entity_repository vt+0x18: not recreated, nothing it
// holds is read here), the project's gameInitialize 0x500B10 (of its types,
// VuMultiDelayEntity's 0x5E0610 - its Decision tick - is recreated; the
// others' are not: the HUD elements' are the UI's), then 0x501260
// "OnProjectInitialized" (each VuOnProjectInitializedEntity's Trigger, tree
// pre-order). release: when live - gameRelease 0x500B80 (the multi delays'
// ticks unregistered: slot 7 0x43A420), the repository remove. tick: the
// VuHUDEntity root's vt+0x30 - the UI agent's (logged).
inline void c_world::hud( int project, race_hud_verb_t verb, float dt )
{
    if ( project < 0 || project > 2 || !m_huds[ project ].tree )
    {
        race_unwired( m_race, "a race HUD project that did not load (UNKNOWN) - its initialise / tick / release not run" );
        return;
    }
    hud_project_t& h = m_huds[ project ];
    if ( verb == race_hud_verb_t::initialize )
    {
        if ( h.live )
            return;
        h.live = true;
        for ( const std::string& at : h.delays )
        {
            multi_delay_node_t& n = m_multi_delays[ at ];
            if ( !n.registered && tick_register( m_race.ticks, { &n, &c_world::multi_delay_ticked, &n }, "Decision" ) )
                n.registered = true;
        }
        if ( !h.ui )
            race_unwired( m_race, "the race HUD projects' gameInitialize 0x500B10 of every type but VuMultiDelayEntity (the HUD elements - the UI's) - not run" );
        LOG_INFO( "race HUD: %s initialised (gameInitialize, OnProjectInitialized x%d)", h.name.c_str( ), static_cast< int >( h.initialized.size( ) ) );
        if ( h.ui )
        {
            hud_env( h );
            h.ui->initialize( );   // gameInitialize 0x500B10 + 0x501260 "OnProjectInitialized" through the UI's binding
            return;
        }
        for ( const std::string& at : h.initialized )
            fire( at, "Trigger" );
        return;
    }
    if ( verb == race_hud_verb_t::release )
    {
        if ( !h.live )
            return;
        h.live = false;
        for ( const std::string& at : h.delays )
        {
            multi_delay_node_t& n = m_multi_delays[ at ];
            tick_unregister_all( m_race.ticks, &n );
            n.registered = false;
        }
        if ( h.ui )
            h.ui->release( );
        LOG_INFO( "race HUD: %s released", h.name.c_str( ) );
        return;
    }
    if ( verb == race_hud_verb_t::draw )   // the states' draws 0x486C00 ..: the root is a VuHUDEntity -> its vt+0x34( 1.0 )
    {
#if HYDRO_UI_PASS
        if ( h.ui && m_ui_pass )
        {
            hud_env( h );
            const ui_frame_t frame = h.ui->draw( m_ui_pass->m_sort, m_ui_pass->m_impl, ui_inputs( ), 1.0f );
            m_hud_unrecreated = frame.m_unrecreated;
        }
#else
        race_unwired( m_race, "the race HUD's draw 0x423A70: compiled out until agent/water's render/program.hxx + resource.hxx are on the tree (world/hudpass.hxx)" );
#endif
        return;
    }
    if ( h.ui )
    {
        hud_env( h );
        h.ui->tick( dt );   // VuHUDEntity slot 12 0x423A20
        return;
    }
    race_unwired( m_race, "the race HUD's VuHUDEntity tick vt+0x30 / draw vt+0x34 (the HUD's update and drawing - the UI agent's) - not run here" );
}

// VuEventManager_broadcast 0x505690. The race's code subscribers are the session's bus (session.hxx
// race_session_game_events / race_session_boat_events): e.g. "OnStartLightsOut" (VuStartLightsOutAction's 0x405470, an
// empty VuParams) reaches the game's 0x48A780 (the condition StartLightsOut) and every VuHumanDriver's 0x40F680 (driver
// +0xC0 = 0.25) in the bus's order - the two writes are independent, so that order (a SUBSTITUTION, world/bus.hxx) is not
// observable there.
//
// The race HUDs are subscribers too (their entities' handlers, registered by their gameInitialize - so only a LIVE HUD
// holds them): hud_post delivers the broadcast to every live HUD but the one it came from (whose binding already ran its
// own handlers, c_ui_binding::broadcast). The order between the race's subscribers and the HUDs' follows the
// registration order the UI binding states (the race's registered before the HUDs loaded: they run first).
inline void c_world::hud_post( std::uint32_t hash, const ui_params_t& p, int origin )
{
    for ( int k = 0; k < 3; ++k )
    {
        hud_project_t& h = m_huds[ k ];
        if ( k == origin || !h.ui || !h.live )
            continue;
        m_hud_posting = true;
        h.ui->binding( ).post( hash, p );
        m_hud_posting = false;
    }
}

inline void c_world::race_broadcast( const char* name, const ui_params_t* params, int origin )
{
    const ui_params_t none{ };
    race_broadcast_hash( fsm_hash( name ), name, params ? *params : none, origin );
}

// The bus (world/bus.hxx: the session's VuEventManager - every subscriber the race's code registered, in the bus's order),
// then the live HUDs. A HUD's own broadcast (m_broadcast_out, name unknown: null) comes here by its hash too, so the race's
// subscribers hear the HUDs' events (OnUserNext ...). The pause menu's gate is on the bus (world/pause.hxx).
inline void c_world::race_broadcast_hash( std::uint32_t hash, const char* name, const ui_params_t& p, int origin )
{
    const int ran = race_bus_broadcast( m_race.bus, hash, p );
    if ( ran == 0 && name )
        LOG_VERBOSE( "race: broadcast %s - no subscriber in the race's code (the HUDs' next)", name );
    if ( name && std::strcmp( name, "OnStartLightsOut" ) == 0 )
        LOG_INFO( "race: OnStartLightsOut broadcast (sync %d) - %d subscribers ran", m_race.dynamics ? m_race.dynamics->syncs : -1, ran );
    hud_post( hash, p, origin );
}

// VuGame's Results enter 0x484110 / exit 0x484250 / tick 0x484280 / draw 0x4842F0 on the UI agent's VuCommonScreen. The
// project load is 0x4B4360 through the UI's world (ui_hud_load, as the HUDs). A screen's broadcasts reach the race's bus
// ("OnUserNext" 0x483170) and the live HUDs; no HUD is its origin (-1). m_frame stays null: the results leaf 0x46A500 has
// no render pass yet. g 0x758FD4 +0x34 (the game's paused word): nothing writes it, so false.
inline void c_world::bind_game_screens( )
{
    ui_game_screens_t& g = m_race.screens;
    g.m_load = [ this ]( const std::string& name ) -> std::unique_ptr< entity_t > {
        const std::string asset = "VuProjectAsset/" + name;
        std::vector< std::uint8_t > bytes;
        if ( !m_archive.read( asset.c_str( ), bytes ) )
        {
            race_unwired( m_race, asset + " not in the pack - the common screen holds no project (0x4B4360's null +0xC)" );
            return nullptr;
        }
        const value_t root = c_reader( bytes ).parse( );
        const value_t* project = find( root, "VuProject" );
        if ( !project )
            return nullptr;
        const entity_registry_t registry = build_entity_registry( m_archive.names( "VuTemplateAsset/" ) );
        return ui_hud_load( *project, race_project_file( m_archive, asset.c_str( ) ), registry,
                            [ this ]( const std::string& n ) { return template_document( n ); } );
    };
    ui_bind_hooks_t hooks;
    hooks.m_root = &m_race.util;
    hooks.m_broadcast_out = [ this ]( std::uint32_t hash, const ui_params_t& p ) { race_broadcast_hash( hash, nullptr, p, -1 ); };
    string_db_ready( );
    hooks.m_string_db = [ this ]( const char* key ) { return string_db_get( m_string_db, key ? key : "" ); };
    hooks.m_external = [ this ]( entity_t& e, const char* plug, const ui_params_t& p ) {
        ui_game_award_t* award = m_race.screens.m_award.get( );
        c_ui_binding* binding = award && award->m_binding ? award->m_binding.get( ) : m_race.screens.m_binding.get( );
        return binding ? screen_external( m_game_screen_state, *binding, e, plug, p ) : ui_ret_t{ };
    };
    // g 0x759038 slot 9( pad, name ): the manager's "pressed" (Results' Select).
    hooks.m_input = [ this ]( int pad, const char* name ) { return m_race.input && m_race.input->manager.pressed( pad, name ); };
    hooks.m_touch_device = &m_touch;
    g.m_hooks = hooks;
    m_race.rules.screen_hooks.results_open = [ this ]( const char* project ) { return m_race.screens.results_open( project ); };
    m_race.rules.screen_hooks.screen = [ this ]( int screen, int verb, float dt ) {
        if ( screen == 0 )
            m_race.screens.screen( verb, dt );
        else
            m_race.screens.award_screen( verb, dt );
    };
    m_race.rules.screen_hooks.award_done = [ this ] { return m_race.screens.award_done( ); };
    m_race.rules.screen_hooks.award.award_open = [ this ]( const char* project, std::int32_t tier, const std::string& boat_class, std::int32_t value, const std::vector< std::string >& names ) {
        return m_race.screens.award_open( project, tier, boat_class, value, names );
    };
    m_race.rules.screen_hooks.paused = [ ] { return false; };
    pause_bind( );
#if HYDRO_UI_PASS
    g.m_frame = [ this ]( const entity_t& root, c_ui_binding& binding ) {
        if ( !m_ui_pass )
            return;
        const ui_frame_inputs_t in = ui_screen_inputs( binding, ui_screen_environment( ) );
        ui_screen_frame( root, m_ui_pass->m_impl, m_ui_pass->m_sort, 1.0f, 4, &in );
    };
#endif
}

// The pause menu: ui/paused.hxx's object over the race. Its project is a screen as Results' (g.m_load, a c_ui_binding); its broadcasts reach the bus, where
// 0x4BAE80's event map stands. Hooks: FMOD's "game" category (no audio device), the analytics (a platform service), the touch device (none), g 0x758FD4 +0x34 (nothing sets it).
inline void c_world::pause_bind( )
{
    ui_pause_hooks_t h;
    h.m_play = [ this ]( const char* event ) {
        std::uint32_t id = 0;
        if ( m_sound.get_event && m_sound.get_event( m_sound.user, event, 1u, &id, nullptr ) == 0 && m_sound.start )
            m_sound.start( m_sound.user, id );
    };
    h.m_tick_paused = [ this ]( bool paused ) { m_race.ticks.pause_count += paused ? 1 : -1; };   // slots 11 / 12
    h.m_delta = [ this ] { return m_race.ticks.dt; };
    h.m_input = [ this ]( const char* name ) { return m_race.input && m_race.input->manager.pressed( 0, name ); };
    h.m_touches = [ ] { return 0; };
    h.m_input_type = [ this ] { return m_race.world.control_method; };
    h.m_open = [ this ]( const std::string& name ) {
        ui_game_screens_t& g = m_race.screens;
        m_pause_root = g.m_load ? g.m_load( name ) : nullptr;
        if ( !m_pause_root )
            return false;
        m_pause_binding = std::make_unique< c_ui_binding >( *m_pause_root, g.m_hooks );
        m_pause_binding->project_initialize( );
        return true;
    };
    h.m_close = [ this ] {
        if ( m_pause_binding )
            m_pause_binding->project_release( );
        m_pause_binding.reset( );
        m_pause_root.reset( );
    };
    h.m_screen = [ this ] { return m_pause_root && ui_common_is_screen( *m_pause_root ); };
    h.m_screen_tick = [ this ]( float dt, bool input ) { m_pause_binding->tick( dt, input ); };
#if HYDRO_UI_PASS
#if HYDRO_UI_PASS
    h.m_sort_key = [ this ] {
        if ( m_ui_pass )
            m_ui_pass->m_sort.key_high( ) = ui_pause_key( m_ui_pass->m_sort.key_high( ) );
    };
#endif
    h.m_screen_draw = [ this ] {
        if ( m_ui_pass )
        {
            const ui_frame_inputs_t in = ui_screen_inputs( *m_pause_binding, ui_screen_environment( ) );
            ui_screen_frame( *m_pause_root, m_ui_pass->m_impl, m_ui_pass->m_sort, 1.0f, 4, &in );
        }
    };
    h.m_background = [ this ]( std::uint32_t colour ) {
        if ( m_ui_pass )
            ui_pause_background( m_ui_pass->m_sort, ui_inputs( ).m_env, colour );
    };
#endif
    std::uint32_t colour = 0xFF000000u;
    ui_json_colour( member( member( member( m_constant_db, "VuDBAsset" ), "UI" ), "PauseMenuBackgroundColor" ), colour );
    m_pause_menu = std::make_unique< c_ui_pause_menu >( m_race.pause, std::move( h ), colour );
    const auto text = [ ]( const ui_params_t& p ) {
        ui_params_reader_t r( p );
        return std::string( r.read_string( ) );
    };
    race_bus_subscribe( m_race.bus, "OnResumeActionGame", 0x4BB680u, "VuPauseMenu", [ this ]( const ui_params_t& ) { m_pause_menu->on_resume( ); } );
    race_bus_subscribe( m_race.bus, "OnSetScreen", 0x4BB6F0u, "VuPauseMenu", [ this, text ]( const ui_params_t& p ) { m_pause_menu->on_set_screen( text( p ) ); } );
    race_bus_subscribe( m_race.bus, "OnPushScreen", 0x4BB780u, "VuPauseMenu", [ this, text ]( const ui_params_t& p ) { m_pause_menu->on_push_screen( text( p ) ); } );
    race_bus_subscribe( m_race.bus, "OnPopScreen", 0x4BB830u, "VuPauseMenu", [ this ]( const ui_params_t& ) { m_pause_menu->on_pop_screen( ); } );
    race_bus_subscribe( m_race.bus, "OnReloadScreen", 0x4BB8E0u, "VuPauseMenu", [ this ]( const ui_params_t& ) { m_pause_menu->on_reload_screen( ); } );
    race_bus_subscribe( m_race.bus, "OnSettingsCommand", 0x4BB980u, "VuPauseMenu", [ this, text ]( const ui_params_t& p ) { m_pause_menu->on_settings_command( text( p ) ); } );
    race_bus_subscribe( m_race.bus, "OnEndActionGame", 0x4B9B70u, "VuActionGameMode", [ this ]( const ui_params_t& ) { if ( m_action ) m_action->on_end_action_game( ); } );
    race_bus_subscribe( m_race.bus, "OnRestartActionGame", 0x4B9B90u, "VuActionGameMode", [ this ]( const ui_params_t& ) { if ( m_action ) m_action->on_restart_action_game( ); } );
}

#if HYDRO_UI_PASS
// The render pass's answers to the frame's inputs (world/hudpass.hxx): the materials, FontDB (g VuUIFont's document), VuStringDB, the
// assets by the loaded property's name. 0x626B80's default font (VuUIImpl +0x94 "Dev").
inline ui_frame_inputs_t c_world::ui_inputs( )
{
    if ( !m_font_db_ready )
    {
        m_font_db_ready = true;
        std::vector< std::uint8_t > bytes;
        if ( m_archive.read( "VuDBAsset/FontDB", bytes ) )
            m_font_db = c_reader( bytes ).parse( );
    }
    ui_frame_inputs_t in;
    in.m_env = m_ui_pass->env( );
    in.m_fontdb = &m_font_db;
    in.m_string = [ this ]( const std::string& id ) { return string_db_get( m_string_db, id ); };
    in.m_texture = [ this ]( const entity_t& e, int which ) -> const ui_texture_t* {
        static const char* const names[ 3 ] = { "Texture Asset", "Selected Texture Asset", "Pressed Texture Asset" };
        const ui_slot_t* s = ui_property_value( e, names[ which < 3 ? which : 0 ] );
        return s && !s->m_text.empty( ) ? m_ui_pass->texture_asset( s->m_text ) : nullptr;
    };
    in.m_control_texture = [ this ]( const entity_t& e, int which ) -> const ui_texture_t* {
        const ui_slot_t* s = ui_property_value( e, which == 0 ? "Texture Asset" : "Pressed Texture Asset" );
        return s && !s->m_text.empty( ) ? m_ui_pass->texture_asset( s->m_text ) : nullptr;
    };
#if HYDRO_UI_TEXT
    in.m_font = [ this ]( const entity_t& e ) {
        ui_text_font_t f;
        const ui_slot_t* font = ui_property_value( e, "Font" );
        std::uint8_t params[ 60 ] = { };
        std::string asset;
        ui_font_params( m_font_db, font ? font->m_text : std::string( ), params, asset );
        f.m_asset_font = asset.empty( ) ? 0u : m_ui_pass->font_asset( asset );
        f.m_asset = f.m_asset_font != 0u;
        f.m_default_font = m_ui_pass->font_asset( "Dev" );
        f.m_default = f.m_default_font != 0u;
        return f;
    };
#endif
    return in;
}

inline ui_screen_env_t c_world::ui_screen_environment( )
{
    const ui_frame_inputs_t in = ui_inputs( );
    ui_screen_env_t env;
    env.m_draw = in.m_env;
    env.m_fontdb = in.m_fontdb;
    env.m_crop = &m_ui_pass->m_impl.m_crop;
    env.m_string6 = [ this ]( const std::string& id ) { return string_db_get( m_string_db, id ); };
    env.m_string7 = env.m_string6;
    env.m_font = in.m_font;
    // A text leaf's slot 12: VuUITextEntity's 0x446DE0 is VuStringDB slot 7 on its String ID; the screen's bound text has no runtime
    // String ID object here, so the loaded one (SUBSTITUTION until a screen's SetText path needs it).
    env.m_text = [ this ]( const entity_t& e ) {
        if ( e.m_type == "VuGamerTagUITextEntity" )   // slot 12 0x445DE0
            return platform_gamer_tag( );
        const ui_slot_t* id = ui_property_value( e, "String ID" );
        return id && !id->m_text.empty( ) ? string_db_get( m_string_db, id->m_text ) : std::string( );
    };
    env.m_texture = in.m_texture;
    env.m_asset_texture = [ this ]( const std::string& asset ) { return m_ui_pass->texture_asset( asset ); };   // UI batch 32: selectors, buttons, boat stats, sliders
#if HYDRO_UI_TEXT
    env.m_font_asset = [ this ]( const std::string& asset ) { return m_ui_pass->font_asset( asset ); };
#endif
    env.m_root = m_menu ? &m_menu_util : &m_race.util;
    m_difficulty_env.m_root = env.m_root;
    env.m_difficulty = &m_difficulty_env;
    return env;
}
#endif

// The frame's UI (world/hudpass.hxx): after the composer, before the present. The action mode's draw slot 5 0x4B9B60: its current state's
// draw, which for Begin / Run / End is 0x4B9C10 (hook `draw`), then the sort's flip.
inline void c_world::ui_frame( )
{
#if HYDRO_UI_PASS
    if ( !m_ui_pass || !m_modes.m_mode )
        return;
    m_ui_pass->begin( m_race.world.display_w, m_race.world.display_h );
    m_modes.draw( );
    m_ui_pass->end( );
#endif
}

// ui_hud_env_fill before each HUD call: the rules' boats (their 0x520 images), the human's index, the count, the
// settings' units (below), the track's built / circuit answers; the control method g 0x758FC0 +0x18 = the session's
// (race_world_t::control_method - c_input_device's, set each "Input" phase).
inline void c_world::hud_env( hud_project_t& h )
{
    std::vector< const std::uint8_t* > images;
    std::int32_t local = -1;
    for ( std::size_t k = 0; k < m_race.rules_boats.size( ); ++k )
    {
        images.push_back( m_race.rules_boats[ k ].boat );
        for ( const void* b : m_race.manager.lists.humans.boats )
            if ( b == m_race.rules_boats[ k ].boat && local < 0 )
                local = static_cast< std::int32_t >( k );
    }
    ui_hud_env_t& env = h.ui->env( );
    // The units: VuSettingsManager g 0x758FE0 +0x44 (ctor 0x4B4650: ""), which its slot 1 0x4B4870 fills first from the
    // defaults - VuDBAsset_load_Units 0x4B4FC0: gameutil +4 (ConstantDB) ["VuDBAsset"]["DefaultSettings"]["Metro"]
    // ["Units"], a string's text else the static "" 0x819950 - then from the profile (g 0x759084: saves, re-made, NOT
    // 1:1 - the test profile holds no saved settings, so the defaults stand).
    const value_t& units = member( member( member( member( m_constant_db, "VuDBAsset" ), "DefaultSettings" ), "Metro" ), "Units" );
    const std::string units_text = units.tag == 4 ? units.text : std::string( );
    ui_hud_env_fill( env, h.boats, images, local, static_cast< std::int32_t >( images.size( ) ), units_text.c_str( ) );
    env.m_track_built = m_race.track && track_is_built( *m_race.track );
    env.m_track_circuit = env.m_track_built && track_is_circuit( *m_race.track );
    env.m_control_method = m_race.world.control_method;
    env.m_manager_28 = static_cast< std::uint32_t >( m_race.manager.lists.humans.boats.size( ) );   // g 0x758FB8 +0x28: the human list's size (0x426330: the pause button)
    // The gauges' inputs (core/ui overlay.hxx ui_hud_gauges_t): the speedometer 0x42BA00's local boat - its body
    // (+0x21C) linear velocity +0x140 / +0x144, +0x31C, +0x320, +0x32C, +0x310, the animation object's (+0x230) +0x78,
    // +0x32D - and the minimap 0x427BD0's boats: the transform's (+0x50) world row 3 (+0x80 / +0x84) and row 1
    // (+0x50 / +0x54), the camera's (+0x224) +0x130 / +0x134, the driver slot 12 (the rules' human answer), +0x490.
    // The display size is g 0x75907C slot 16 (the D3D11 layer - the session's display_w / _h, a platform hook); the
    // textures / materials (m_speedometer / m_minimap) are the asset factory's - not set here. The draw needs the
    // HUD render pass (not scheduled).
    ui_hud_gauges_t& g = h.ui->gauges( );
    g.m_live = true;
#if HYDRO_UI_PASS
    // The speedometer ctor 0x42AB60's textures by name (the asset factory 0x426A90) and its "BoostMeter" material.
    if ( m_ui_pass && !g.m_speedometer )
        g.m_speedometer = [ this ]( const entity_t&, ui_speedometer_t& s ) {
            const auto at = [ this ]( const std::string& name ) {
                const ui_texture_t* t = m_ui_pass->texture_asset( name );
                return t ? *t : ui_texture_t{ };
            };
            s.m_back_imperial = at( "UI/HUD_Speed_Back_Imperial" );
            s.m_back_metric = at( "UI/HUD_Speed_Back_Metric" );
            s.m_needle = at( "UI/HUD_Speed_Needle" );
            for ( int k = 0; k < 8; ++k )
                s.m_boost[ k ] = at( "UI/HUD_Boost" + std::to_string( k + 1 ) + "x" );
            s.m_active = at( "UI/HUD_Speed_BoostActive" );
            s.m_inactive = at( "UI/HUD_Speed_BoostInactive" );
            s.m_hold = at( "UI/HUD_Speed_BoostHold" );
            s.m_mask = at( "UI/HUD_Speed_BoostMask" );
            s.m_flash = at( "UI/HUD_Speed_BoostInactiveFlash" );
            m_ui_pass->boost_material( s.m_material, s.m_material_key );
            s.m_self = m_ui_pass->boost_add( s );
        };
    // The minimap ctor 0x426D70: its textures by name, TrackDB [ track ].MiniMap. No "Texture": VuGfxUtil +0x1C's white (0x4280B6), none on this tree: 0.
    if ( m_ui_pass && !g.m_minimap )
        g.m_minimap = [ this ]( const entity_t&, ui_minimap_t& m ) {
            const auto at = [ this ]( const std::string& name ) {
                const ui_texture_t* t = m_ui_pass->texture_asset( name );
                return t ? *t : ui_texture_t{ };
            };
            const value_t& mini = member( member( member( m_track_db, "VuDBAsset" ), m_race.game.track.c_str( ) ), "MiniMap" );
            const value_t& origin = member( mini, "Origin" );
            ui_json_float( member( origin, "X" ), m.m_origin[ 0 ] );
            ui_json_float( member( origin, "Y" ), m.m_origin[ 1 ] );
            ui_json_float( member( mini, "Scale" ), m.m_scale );
            ui_json_float( member( mini, "ViewDist" ), m.m_view );
            ui_json_float( member( mini, "Softness" ), m.m_softness );
            std::uint32_t water = 0;
            if ( ui_json_colour( member( mini, "WaterColor" ), water ) )
                std::memcpy( m.m_water, &water, 4 );
            const value_t& texture = member( mini, "Texture" );
            const ui_texture_t map = texture.tag == 4 ? at( texture.text ) : ui_texture_t{ };
            m.m_map = map.m_handle;
            m.m_map_size[ 0 ] = map.m_width;
            m.m_map_size[ 1 ] = map.m_height;
            const ui_texture_t frame = at( "UI/HUD_MapFrame" );
            m.m_frame = frame.m_handle;
            m.m_frame_size[ 0 ] = frame.m_width;
            m.m_frame_size[ 1 ] = frame.m_height;
            m.m_mask = at( "UI/HUD_MapMask" ).m_handle;
            m.m_icon = at( "UI/HUD_MapPlayer" ).m_handle;
            m_ui_pass->map_material( m.m_material, m.m_material_key );
            m.m_self = m_ui_pass->map_add( m );
        };
#endif
    g.m_display[ 0 ] = m_race.world.display_w;
    g.m_display[ 1 ] = m_race.world.display_h;
    g.m_boat = ui_speedometer_boat_t{ };
    g.m_map = ui_map_world_t{ };
    g.m_map.m_manager = true;
    const auto f32 = []( const std::uint8_t* p, int at ) { float v; std::memcpy( &v, p + at, 4 ); return v; };
    const auto i32 = []( const std::uint8_t* p, int at ) { std::int32_t v; std::memcpy( &v, p + at, 4 ); return v; };
    for ( std::size_t k = 0; k < m_race.rules_boats.size( ) && k < m_race.boats.size( ); ++k )
    {
        const race_rules_boat_t& rb = m_race.rules_boats[ k ];
        const race_session_boat_t& sb = *m_race.boats[ k ];
        if ( !rb.boat )
            continue;
        ui_map_boat_t mb;
        if ( rb.transform )
        {
            mb.m_position[ 0 ] = f32( rb.transform, 0x80 );
            mb.m_position[ 1 ] = f32( rb.transform, 0x84 );
            mb.m_direction[ 0 ] = f32( rb.transform, 0x50 );
            mb.m_direction[ 1 ] = f32( rb.transform, 0x54 );
        }
        if ( sb.io.camera )
        {
            mb.m_heading[ 0 ] = f32( sb.io.camera, 0x130 );
            mb.m_heading[ 1 ] = f32( sb.io.camera, 0x134 );
        }
        mb.m_answer = rb.human;
        mb.m_kind = i32( rb.boat, 0x490 );
        g.m_map.m_boats.push_back( mb );
        if ( static_cast< std::int32_t >( k ) == local )
        {
            g.m_map.m_local_present = true;
            g.m_map.m_local = mb;
            const std::uint8_t* body = reinterpret_cast< const std::uint8_t* >( &sb.body );
            g.m_boat.m_present = true;
            g.m_boat.m_velocity[ 0 ] = f32( body, 0x140 );
            g.m_boat.m_velocity[ 1 ] = f32( body, 0x144 );
            g.m_boat.m_boost_count = i32( rb.boat, 0x31C );
            g.m_boat.m_digit_scale = f32( rb.boat, 0x320 );
            g.m_boat.m_byte_32c = rb.boat[ 0x32C ];
            g.m_boat.m_boost = f32( rb.boat, 0x310 );
            g.m_boat.m_mode = sb.io.animation ? i32( sb.io.animation, 0x78 ) : 0;
            g.m_boat.m_byte_32d = rb.boat[ 0x32D ];
        }
    }
}