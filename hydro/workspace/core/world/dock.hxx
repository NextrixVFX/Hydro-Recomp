#pragma once

// VuDockedBoatEntity and VuPosterEntity's model half, live in the front end (game/docked.hxx holds their verified document logic).
//
// What the menu runs, read from retail: Screens/Background authors one VuDockedBoatEntity (Position X -8, Position Y 20, Rotation Z 180:
// its three nodes are VuBasicProperty<float> 0x737484, so the 180 reaches 0x5345C0 as it stands) and one VuPosterEntity. Slot 6 0x435B00 ->
// 0x436230: a whole VuBoatEntity (new 0x520, 0x41B710, 0x41B850, the document with Driver.Type "UI", the name 0x683E40) at (x, y, 0) (0x534830,
// 0x534C00( 1 ), the watcher +0xC0), rotation (0, 0, z) (0x5345C0), +0x308 += 1, gameInitialize 0x500B10: the boat runs its own Decision
// and Build ticks and the engine's dynamics and water exactly as in a race; the dock's "Build" tick 0x436450 then pins x, y and heading.
//
// Ours: the boat lives in m_race, a session marked `dock` (never `active`), holding only what the boat's ticks read: the dynamics world, the
// surfaces, the tick manager's DynamicsSync / DynamicsKick, the boat. A changed boat rebuilds that session, as 0x435CC0's drop and 0x436230 rebuild the boat.
// FLAGGED, not recreated: VuAiUiDriver's tick 0x40E5C0 (the boat's throttle and steering stay 0); the Background's static bodies (the dock
// boat is pinned, nothing else in the world moves it); set_skin rebuilds rather than 0x421490 on the live boat; the "YoureCrazy" voice line (no
// audio device), and the 6 VuAIUIDriverStartEntity boats that sail behind the menu (0x430520).

inline entity_t* race_entity_find( entity_t* e, const char* type )
{
    if ( !e )
        return nullptr;
    if ( e->m_type == type )
        return e;
    for ( auto& c : e->m_children )
        if ( entity_t* x = race_entity_find( c.get( ), type ) )
            return x;
    return nullptr;
}

// The dock's "Build" tick 0x436450.
inline void c_world::dock_tick( void* user, float )
{
    c_world& w = *static_cast< c_world* >( user );
    if ( w.m_race.boats.empty( ) )
        return;
    race_session_boat_t& b = *w.m_race.boats[ 0 ];
    float out[ 16 ];
    docked_build_tick( b.body.body.basis, reinterpret_cast< const float* >( b.graph.objects[ 0 ].image.data( ) + 0x290 ), w.m_dock_spot, out );
    body_set_center_of_mass_transform( b.body.body, out, out + 12 );
}

// 0x436230 on a fresh dock session.
inline bool c_world::dock_make( const value_t& document )
{
    race_world_t lent = std::move( m_race.world );
    m_retail_boats.clear( );
    m_race = race_session_t{ };
    m_race.world = std::move( lent );
    race_session_t& s = m_race;
    const race_session_dbs_t dbs = race_dbs( );
    if ( !dbs.constant_db || !dbs.boat_db )
        return false;
    s.dock = true;
    s.dynamics = std::make_unique< race_dynamics_t >( );
    race_dynamics_init( *s.dynamics, &s.contact );
    s.constant_db = dbs.constant_db;
    s.random = &m_vurandom;
    s.track = m_track_built ? &m_track : nullptr;
    s.tree = m_tree.get( );
    {
        std::vector< std::uint8_t > bytes;
        if ( m_archive.read( "VuDBAsset/SurfaceDB", bytes ) )
        {
            const value_t surface_db = c_reader( bytes ).parse( );
            s.surfaces_built = surface_table_build( s.surfaces, &surface_db );
        }
    }
    tick_manager_init( s.ticks );
    tick_register( s.ticks, { s.dynamics.get( ), race_session_dynamics_sync, &s }, "DynamicsSync" );
    tick_register( s.ticks, { s.dynamics.get( ), race_session_dynamics_kick, &s }, "DynamicsKick" );
    // The ghost's path (0x4AF640), the same load.
    static const entity_registry_t registry = build_entity_registry( { } );
    bool ok = true;
    race_load_hooks_t h;
    h.post_load = [ & ]( entity_t& e, const value_t& entry ) { race_session_post_load( s, dbs, m_archive, ok, e, entry ); };
    entity_world_t bw = race_boat_world( registry, h );
    std::unique_ptr< entity_t > child = entity_create( "VuBoatEntity", bw );
    if ( !child )
        return false;
    child->m_name = "DockedBoat";
    entity_load( *child, &document, bw );
    h.post_load( *child, document );
    race_entity_hash( *child, 0x811C9DC5u );
    s.ghost_entities.push_back( std::move( child ) );
    if ( !ok || s.boats.size( ) != 1u || !s.boats[ 0 ]->sections || !s.boats[ 0 ]->driver || s.boats[ 0 ]->driver->graph.empty( ) )
        return false;
    race_session_boat_t& b = *s.boats[ 0 ];
    std::uint8_t* image = b.graph.objects[ 0 ].image.data( );
    // The placement, 0x436230's tail.
    const float position[ 3 ] = { m_dock_spot[ 0 ], m_dock_spot[ 1 ], 0.0f };
    const float euler[ 3 ] = { 0.0f, 0.0f, m_dock_spot[ 2 ] };
    race_ghost_place( s, b, position, euler );
    std::uint32_t lock;
    std::memcpy( &lock, image + 0x308, 4 );
    ++lock;
    std::memcpy( image + 0x308, &lock, 4 );
    // 0x500B10 on the boat, as race_ghost_initialize's, with the boat's slot 17 (a ghost's is a nullsub).
    auto user = std::make_shared< race_session_tick_t >( race_session_tick_t{ &s, &b, 0 } );
    s.ghost_ticks.push_back( user );
    race_start_boat_t sb;
    sb.boat = image;
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
    ih.slot17 = [ &s, &b ] { race_session_enable( s, b, true ); };
    ih.anim_create = [ &b ] { race_session_create_rig( b ); };
    ih.pfx_create = [ &s, &b ] {
        if ( b.pfx_filled && !boat_pfx_create( b.pfx, race_session_pfx_hooks( b ) ) )
            race_unwired( s, "gameInitialize 0x419FF0: a pfx hook missing - UNKNOWN" );
    };
    race_boat_game_initialize( &b, s.ticks, { &b, race_session_decision, user.get( ) }, { &b, race_session_build, user.get( ) }, b.draw_added, ih );
    const std::uint32_t e = race_entity_token( 0 );
    s.words[ e + 0x220 ] = e + 0x1000u;
    s.words[ e + 0x1000u ] = vt;
    race_drive_setup( s, s.constant_db, 0 );
    tick_register( s.ticks, { this, &c_world::dock_tick, this }, "Build" );
    race_boat_models( 0 );
    LOG_INFO( "front end: dock boat %s (%s) at %.1f %.1f, heading word %.3f", b.boat.c_str( ), b.skin.c_str( ), m_dock_spot[ 0 ], m_dock_spot[ 1 ], m_dock_spot[ 2 ] );
    return true;
}

inline docked_hooks_t c_world::dock_hooks( )
{
    static const value_t none;
    const race_session_dbs_t dbs = race_dbs( );
    docked_hooks_t h;
    h.game_data = &m_menu_util;
    h.boat_db = dbs.constant_db;   // gameutil +4
    h.make = [ this ]( const value_t& document ) {
        if ( !dock_make( document ) )
            LOG_WARNING( "front end: the dock boat did not build (UNKNOWN)" );
        return 1u;
    };
    h.drop = [ ]( std::uint32_t ) { };   // dock_make rebuilds the session
    h.db = [ ]( std::uint32_t ) -> const value_t& { return none; };   // read only for the voice line: no device
    h.set_skin = [ this ]( std::uint32_t, const value_t& ) {
        const value_t dock = member( member( m_menu_util, "DockedBoat" ), "Properties" );
        dock_make( docked_document( dock ) );
    };
    h.set_locked = [ this ]( std::uint32_t, std::uint8_t locked ) {
        if ( !m_race.boats.empty( ) )
            m_race.boats[ 0 ]->graph.objects[ 0 ].image[ 0x466 ] = locked;
    };
    h.voice = [ ]( const char* ) { };
    h.tick_on = [ ] { };   // registered by dock_make
    h.tick_off = [ ] { };
    return h;
}

// The front end's load: both entities out of Screens/Background.
inline void c_world::dock_start( )
{
    entity_t* poster = race_entity_find( m_tree.get( ), "VuPosterEntity" );
    m_poster_on = false;
    m_poster_entity = poster;
    poster_reload( );
    entity_t* dock = race_entity_find( m_tree.get( ), "VuDockedBoatEntity" );
    if ( !dock || !dock->m_state )
        return;
    const docked_entity_state_t& st = *static_cast< const docked_entity_state_t* >( dock->m_state.get( ) );
    m_dock_spot[ 0 ] = st.x;
    m_dock_spot[ 1 ] = st.y;
    m_dock_spot[ 2 ] = st.rotation;
    docked_boat_t e;
    docked_initialize( e, dock_hooks( ) );
}

// The two listeners (ctor 0x4358E0): "OnChangeDockedBoat" 0x435CC0, "OnResetDockedBoat" 0x436060.
inline bool c_world::menu_dock( std::uint32_t hash, const ui_params_t& p )
{
    static const std::uint32_t change = fsm_hash( "OnChangeDockedBoat" ), reset = fsm_hash( "OnResetDockedBoat" );
    if ( hash != change && hash != reset )
        return false;
    if ( !race_entity_find( m_tree.get( ), "VuDockedBoatEntity" ) )
        return true;
    docked_boat_t e;
    e.boat = 1u;
    if ( hash == change )
        docked_change( e, p, dock_hooks( ) );
    else
        docked_reset( e, dock_hooks( ) );
    return true;
}

// The reload 0x43AF50: the instance +0x64 takes Poster.ModelAssetName (0x520080; nothing or an unknown name: no scene), the colour by Locked, drawn by 0x43AEA0 at the transform's +0x50.
inline void c_world::poster_reload( )
{
    m_poster_on = false;
    if ( !m_poster_entity || !m_retail )
        return;
    const transform_component_t* t = entity_transform( *m_poster_entity );
    const value_t& name = member( member( m_menu_util, "Poster" ), "ModelAssetName" );
    if ( !t || name.tag != 4u || name.text.empty( ) )
        return;
    const gfx_scene_view_t* v = m_retail->model( name.text );
    if ( v == nullptr )
        return;
    m_poster_model = gfx_static_model_t{ };
    gfx_scene_view_attach( m_poster_model, *v );
    std::memcpy( m_poster_matrix, t->world, 64 );
    m_poster_colour = poster_color( m_menu_util );
    m_poster_on = true;
    LOG_INFO( "front end: poster %s", name.text.c_str( ) );
}

// The matrix of 0x45F860 / 0x45F900: identity (0x410A40) x fromEuler( rotation ) (0x435640, 0x4078C0), then x the translation (0x410AB0, 0x4078C0).
inline void ui_game_config_trophy_matrix( const float position[ 3 ], const float rotation[ 3 ], float out[ 16 ] )
{
    const float identity[ 16 ] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    float translation[ 16 ], euler[ 16 ], m[ 16 ];
    std::memcpy( translation, identity, 64 );
    std::memcpy( translation + 12, position, 12 );
    vu_matrix_from_euler( euler, rotation );
    hull_multiply( identity, m, euler );
    hull_multiply( m, out, translation );
}

// VuGameConfigEntity's trophy (slot 12 0x45EE20 picks the model of table 0x7357CC by the best placing, ui_game_config_changed): 0x520080 on the instance +0x304 (colour +0x31C 0xFFFFFFFF
// from the ctor), the matrix 0x45F860 builds: identity x fromEuler( Trophy Rotation +0x2F4 ) x translation( Trophy Position +0x2E4 ) (0x410A40, 0x435640, 0x4078C0, 0x410AB0, 0x4078C0).
inline void c_world::menu_trophy_model( int id, const entity_t& e, const char* model )
{
    const auto it = m_menu_projects.find( id );
    if ( it == m_menu_projects.end( ) || !m_retail )
        return;
    menu_project_t& p = it->second;
    p.trophy = gfx_static_model_t{ };
    if ( const gfx_scene_view_t* v = m_retail->model( model ) )
        gfx_scene_view_attach( p.trophy, *v );
    p.trophy.m_colour = 0xFFFFFFFFu;
    float position[ 3 ] = { }, rotation[ 3 ] = { };
    if ( const ui_slot_t* s = ui_property_value( e, "Trophy Position" ) )
        std::memcpy( position, s->m_bits, 12 );
    if ( const ui_slot_t* s = ui_property_value( e, "Trophy Rotation" ) )
        std::memcpy( rotation, s->m_bits, 12 );
    ui_game_config_trophy_matrix( position, rotation, p.trophy_matrix );
    LOG_INFO( "front end: trophy %s", model );
}

// 0x45F860 per added draw object: +0x324 clear draws nothing; the model's 0x520130 draws nothing with no scene. FLAGGED: its shadow half 0x45F900 is not wired (the front end has no model shadow hook).
inline void c_world::menu_trophy_draw( gfx_sort_t& sort, const gfx_draw_params_t& p )
{
    for ( auto& project : m_menu_projects )
        if ( project.second.trophy_shown && *project.second.trophy_shown )
            project.second.trophy.draw( sort, project.second.trophy_matrix, p );
}

// The frame's m_board: 0x47DB40 over the bound entity, with the three fonts of its loaded names.
inline void c_world::menu_board_frame( ui_frame_inputs_t& in, c_ui_binding& binding, const ui_screen_env_t& env )
{
    in.m_board = [ this, &binding, env, db = in.m_db ]( const entity_t& e, ui_gfx_sort_t& sort, const ui_font_draw_env_t& draw ) -> bool {
        c_ui_mini_board* board = binding.mini_board( e );
        if ( !board || !env.m_fontdb )
            return false;
        ui_mini_board_draw_t d;
        const char* const names[ 3 ] = { "NameFont", "ScoreFont", "InfoFont" };
        for ( int k = 0; k < 3; ++k )
        {
            const ui_slot_t* name = ui_property_value( e, names[ k ] );
            std::string asset;
            ui_font_params( *env.m_fontdb, name ? name->m_text : std::string( ), d.m_fonts[ k ].m_params, asset );
            d.m_fonts[ k ].m_font = ui_screen_font( e, names[ k ], env );
        }
        d.m_sort = &sort;
        d.m_env = &draw;
        d.m_crop = *env.m_crop;
        d.m_db = db;
        d.m_string = env.m_string6;
        board->draw( d );
        return true;
    };
}
