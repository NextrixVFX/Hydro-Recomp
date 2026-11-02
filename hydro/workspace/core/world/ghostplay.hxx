#pragma once

// VuGhostGame's machine on the session (world batch 38): the states' handlers of game_mode_defs( )[ 2 ], over world/ghostgame.hxx and the
// local ghost store (save/ghost.hxx). Hooks: the leaderboard g 0x758FA8 answers 1 with zero rows (a local Ghost race; a local ghost is
// the player's own, per the Saves decision); the sign-in's gamer tag is the human's driver name.
// The ghost's boat: boat/ghost.hxx ghost_add on the session's boats, made in LoadGhosts, initialised in Game's enter, ticked by race_ghost_decision.

// Boat k's row for the results, as race_session_start's entry_of.
inline race_results_entry_t race_ghost_entry( const race_session_t& s, std::size_t k )
{
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
        e.human = s.start_boats[ k ].cls.slot[ 0 ];
        e.local = s.start_boats[ k ].cls.slot[ 2 ];
    }
    return e;
}

inline std::size_t race_ghost_human( const race_session_t& s )
{
    for ( std::size_t k = 0; k < s.rules_boats.size( ); ++k )
        if ( s.rules_boats[ k ].human )
            return k;
    return s.rules_boats.size( );
}

inline float race_ghost_throttle( race_session_t& s, std::size_t k );   // world/drive.hxx

// The tick 0x4AF520 registers, "Final": 0x4AF9C0 on the human.
inline void race_ghost_tick( void* user, float dt )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    const std::size_t k = race_ghost_human( s );
    if ( k >= s.boats.size( ) )
        return;
    race_session_boat_t& b = *s.boats[ k ];
    ghost_boat_t g;
    g.boat = b.graph.objects[ 0 ].image.data( );
    g.transform = race_session_image( b, 0, 0x50 );
    g.debris = race_session_image( b, 0, 0x238 );
    g.throttle = race_ghost_throttle( s, k );
    if ( !g.transform || !g.debris )
    {
        race_unwired( s, "the ghost recorder 0x4AF9C0: the human's transform / debris image is missing - not recorded" );
        return;
    }
    ghost_tick( s.ghost, g, dt );
}

inline void race_ghost_last_frame( race_session_t& s )
{
    const std::size_t k = race_ghost_human( s );
    if ( k >= s.boats.size( ) )
        return;
    race_session_boat_t& b = *s.boats[ k ];
    ghost_boat_t g;
    g.boat = b.graph.objects[ 0 ].image.data( );
    g.transform = race_session_image( b, 0, 0x50 );
    g.debris = race_session_image( b, 0, 0x238 );
    g.throttle = race_ghost_throttle( s, k );
    if ( g.transform && g.debris )
        ghost_record_frame( s.ghost, g );
}

inline void race_ghost_debris( race_session_t& s, std::size_t k );      // world/drive.hxx
inline void race_ghost_destroyed( race_session_t& s, std::size_t k );

// 0x5344A0 on the boat's transform (as the nuisance boat's placement).
inline void race_ghost_set_world( race_session_t& s, race_session_boat_t& r, const float* m, bool notify )
{
    std::uint8_t* t = race_session_image( r, 0, 0x50 );
    if ( !t )
        return;
    std::memcpy( t + 0x50, m, 64 );
    float e[ 3 ];
    respawn_euler( m, e );
    std::memcpy( t + 0xA0, e, 12 );
    image_put32( t, 0xAC, 0 );
    std::memmove( t + 0x10, t + 0x50, 64 );
    std::memmove( t + 0x90, t + 0xA0, 16 );
    if ( notify && r.sections )
        boat_transform_changed( r.graph.objects[ 0 ].image.data( ), reinterpret_cast< const float* >( t + 0x50 ), r.body, r.io.camera, r.io.instigator, race_session_transform_hooks( r ) );
    ( void ) s;
}

// 0x4AF640's placement: the first frame's position and euler.
inline void race_ghost_place( race_session_t& s, race_session_boat_t& r, const float* position, const float* euler )
{
    float id[ 16 ], e[ 16 ], m[ 16 ];
    vu_identity( id );
    vu_matrix_from_euler( e, euler );
    hull_multiply( id, m, e );
    std::memcpy( m + 12, position, 12 );
    race_ghost_set_world( s, r, m, true );
}

// Slot 13 0x422A10 on "Decision".
inline void race_ghost_decision( void* user, float dt )
{
    race_session_tick_t& t = *static_cast< race_session_tick_t* >( user );
    race_session_t& s = *t.session;
    race_session_boat_t& b = *t.boat;
    if ( !b.ghost )
        return;
    ghost_replay_hooks_t h;
    h.user = &t;
    h.debris_launch = [ ]( void* u ) {
        race_session_tick_t& x = *static_cast< race_session_tick_t* >( u );
        race_ghost_debris( *x.session, x.index );
    };
    h.destroyed_event = [ ]( void* u ) {
        race_session_tick_t& x = *static_cast< race_session_tick_t* >( u );
        race_ghost_destroyed( *x.session, x.index );
    };
    h.set_world = [ ]( void* u, const float* m ) {
        race_session_tick_t& x = *static_cast< race_session_tick_t* >( u );
        race_ghost_set_world( *x.session, *x.boat, m, false );
    };
    // The hull solver's 0x524450. Our solver lives in the step thunk, which exists once the body is in the world: before that nothing reads it here (FLAGGED).
    h.solver_prepare = [ ]( void* u, const float* m ) {
        race_session_tick_t& x = *static_cast< race_session_tick_t* >( u );
        if ( x.boat->thunk && x.session->world.view )
            fluid_prepare( x.boat->thunk->solver, x.session->world.view->water, m, water_view_kernel, const_cast< water_view_t* >( x.session->world.view ) );
    };
    h.add_body = [ ]( void* u ) {
        race_session_tick_t& x = *static_cast< race_session_tick_t* >( u );
        race_session_enable( *x.session, *x.boat, true );   // vt+0x3C and vt+0x2C: both in boat_enable
    };
    h.decision = [ ]( void* u, float step ) { race_session_decision( u, step ); };
    std::uint8_t* debris = race_session_image( b, 0, 0x238 );
    if ( !debris )
    {
        race_unwired( s, "the ghost's tick 0x422A10 with no debris image - not run" );
        return;
    }
    ghost_boat_tick( b.graph.objects[ 0 ].image.data( ), debris, *b.ghost, b.body, nullptr, dt, h );
}

// Game enter 0x4867E5: 0x500B10 on each ghost's boat (0x41D350, slot 17 the nullsub 0x401560); 0x486820: +0x4D4, +0x4D8, +0x4DC.
inline void race_ghost_initialize( race_session_t& s )
{
    const std::size_t human = race_ghost_human( s );
    for ( const std::unique_ptr< ghost_record_t >& g : s.ghost_records )
    {
        race_session_boat_t& b = *static_cast< race_session_boat_t* >( g->boat );
        std::size_t k = 0;
        while ( k < s.boats.size( ) && s.boats[ k ].get( ) != &b )
            ++k;
        if ( k == s.boats.size( ) || !b.driver || b.driver->graph.empty( ) )
        {
            race_unwired( s, "a ghost's boat with no driver - not initialised (UNKNOWN)" );
            continue;
        }
        std::uint8_t* image = b.graph.objects[ 0 ].image.data( );
        if ( std::uint8_t* hull = b.io.hull )
            ghost_boat_construct( image, hull, *s.constant_db );
        auto user = std::make_shared< race_session_tick_t >( race_session_tick_t{ &s, &b, k } );
        s.ghost_ticks.push_back( user );
        race_start_boat_t sb;
        sb.boat = image;
        sb.transform = race_session_image( b, 0, 0x50 );
        sb.engine = b.engine.object.image;
        sb.driver = b.driver->graph[ 0 ].image.data( );
        const std::uint32_t vt = load_word( sb.driver, 0 );
        sb.cls = boat_driver_of( vt );
        boat_driver_kind( vt, sb.kind );
        s.start_boats.resize( k );
        s.start_boats.push_back( sb );
        s.fleet_boats.resize( k );
        s.fleet_boats.push_back( { reinterpret_cast< const float* >( sb.transform + 0x50 ), 0.0f, 0.0f } );
        race_boat_init_hooks_t ih;
        ih.manager = &s.manager.lists;
        ih.entry = sb.boat;
        ih.kind = sb.kind;
        ih.instigator = [ &s, &b ] { race_session_register_instigator( s, b ); };
        ih.anim_create = [ &b ] { race_session_create_rig( b ); };
        ih.pfx_create = [ &s, &b ] {
            if ( b.pfx_filled && !boat_pfx_create( b.pfx, race_session_pfx_hooks( b ) ) )
                race_unwired( s, "gameInitialize 0x419FF0: a pfx hook missing - UNKNOWN" );
        };
        race_boat_game_initialize( &b, s.ticks, { &b, race_ghost_decision, user.get( ) }, { &b, race_session_build, user.get( ) }, b.draw_added, ih );
        const std::uint32_t e = race_entity_token( static_cast< int >( k ) );
        s.words[ e + 0x220 ] = e + 0x1000u;
        s.words[ e + 0x1000u ] = vt;
        race_drive_setup( s, s.constant_db, k );
        race_rules_boat_t rb;
        rb.boat = image;
        rb.transform = sb.transform;
        rb.driver = sb.driver;
        rb.drive = static_cast< int >( k );
        // +0x4D8: the track's vt+0x38, the answer begin gave the human.
        rb.checkpoint = human < s.rules_boats.size( ) ? s.rules_boats[ human ].checkpoint : -1;
        rules_put_i32( image, 0x4D4, s.rules_game.laps );
        std::memcpy( image + 0x4DC, sb.transform + 0x50, 64 );
        s.rules_boats.push_back( rb );
        for ( ghost_game_record_t& r : s.ghost_game.records )
            if ( r.index == g->index )
                r.entry = race_ghost_entry( s, k );
        if ( s.boat_models )
            s.boat_models( k );
    }
}

// The game's fields the handlers read.
inline void race_ghost_fill( race_session_t& s )
{
    ghost_game_t& g = s.ghost_game;
    g.circuit = s.rules_game.circuit;
    g.track = s.game.track;
    g.boats = static_cast< std::int32_t >( s.manager.lists.racers.boats.size( ) );
    const std::size_t k = race_ghost_human( s );
    g.human = k < s.rules_boats.size( ) ? s.rules_boats[ k ].boat : nullptr;
    g.human_entry = race_ghost_entry( s, k );
    g.entries = s.ghost_loads.entries;
}

inline void race_session_ghost_bind( race_session_t& s )
{
    race_rules_t& r = s.rules;
    s.ghost_loads.add_ghost = [ &s ]( int index, const std::vector< std::uint8_t >& blob ) {
        race_session_boat_t* made = nullptr;
        ghost_add_hooks_t h;
        h.make_boat = [ &s, &made ]( const std::string& name, const value_t& document ) -> void* {
            made = s.ghost_make ? s.ghost_make( name, document ) : nullptr;
            return made;
        };
        h.place = [ &s ]( void* boat, const float* position, const float* euler ) {
            if ( boat )
                race_ghost_place( s, *static_cast< race_session_boat_t* >( boat ), position, euler );
        };
        h.bind = [ ]( void* boat, const ghost_data_t& d ) {
            if ( boat )
                static_cast< race_session_boat_t* >( boat )->ghost = &d;
        };
        if ( !ghost_add( s.ghost_records, index, blob.data( ), blob.size( ), h ) || made == nullptr )
            return false;
        const ghost_data_t& d = *made->ghost;
        ghost_game_record_t rec;
        rec.index = index;
        rec.frames = d.count;
        rec.boat = made->graph.objects[ 0 ].image.data( );
        if ( d.count > 0 )
            std::memcpy( &rec.last_time, d.frames.data( ) + 24u * ( static_cast< std::size_t >( d.count ) - 1u ), 4 );
        s.ghost_game.records.push_back( rec );
        LOG_INFO( "ghost: %s's ghost read, %d frames, %.3f s: GhostBoat%d made", s.ghost_loads.entries[ index ].name.c_str( ), d.count, rec.last_time, index );
        return true;
    };
    s.ghost_loads.condition = [ &s ]( const char* name ) { s.rules.fsm.pulse_condition( name ); };
    for ( auto& st : r.fsm.m_states )
    {
        fsm_state_t* state = st.get( );
        if ( state->m_name == "GetLeaderboard" )
        {
            // Enter 0x485890, exit 0x485AA0, tick 0x485AF0: vt+0x1C's answer is 1, "Success"; the VuMessageBox "Ghost_Leaderboard" +0x758 is the UI's.
            state->m_enter = [ &s ] { race_rules_hold( s.rules, 1 ); };
            state->m_exit = [ &s ] { race_rules_hold( s.rules, -1 ); };
            state->m_tick = [ &s ]( float ) { s.rules.fsm.pulse_condition( "Success" ); };
        }
        else if ( state->m_name == "LoadGhosts" )
        {
            // Enter 0x485B50: hold, the list (the local player, 0x487750; no leaderboard row), the GET loop. Exit 0x485AA0.
            state->m_enter = [ &s ] {
                race_rules_hold( s.rules, 1 );
                s.ghost_loads.entries.clear( );
                ghost_entry_t me;
                me.name = race_ghost_entry( s, race_ghost_human( s ) ).driver_name;
                s.ghost_loads.entries.push_back( me );
                ghost_load_all( s.ghost_loads, s.game.track );
            };
            state->m_exit = [ &s ] { race_rules_hold( s.rules, -1 ); };
        }
        else if ( state->m_name == "PreGame" )
        {
            // 0x486060, 0x486660, 0x4866C0, 0x486720.
            state->m_enter = [ &s ] {
                race_rules_hold( s.rules, 1 );
                race_rules_hud( s.rules, 0, race_hud_verb_t::initialize );
                race_ghost_fill( s );
                if ( !ghost_game_table( s.util, s.ghost_game ) )
                    race_unwired( s, "VuGhostGame PreGame: a loaded ghost with no record - UNKNOWN" );
            };
            state->m_exit = [ &s ] {
                race_rules_hold( s.rules, -1 );
                race_rules_hud( s.rules, 0, race_hud_verb_t::release );
                ghost_game_table_erase( s.util );
            };
            state->m_tick = [ &s ]( float dt ) { race_rules_hud( s.rules, 0, race_hud_verb_t::tick, dt ); };
            state->m_draw = [ &s ] { race_rules_hud( s.rules, 0, race_hud_verb_t::draw ); };
        }
        else if ( state->m_name == "Game" )
        {
            // 0x486780: the HUD; the point above; 0x4AF520 on the human, its "Final" tick.
            state->m_enter = [ &s ] {
                race_rules_hud( s.rules, 1, race_hud_verb_t::initialize );
                race_ghost_initialize( s );
                const std::size_t k = race_ghost_human( s );
                if ( k >= s.boats.size( ) )
                    return;
                const race_results_entry_t e = race_ghost_entry( s, k );
                ghost_start( s.ghost, s.boats[ k ]->boat, s.boats[ k ]->skin, e.driver_name );
                tick_register( s.ticks, { &s.ghost, race_ghost_tick, &s }, "Final" );
            };
            // 0x486880: the HUD; ghost_game_end; the tick off; the last frame.
            state->m_exit = [ &s ] {
                race_rules_hud( s.rules, 1, race_hud_verb_t::release );
                race_ghost_fill( s );
                if ( !s.ghost_game.human || !ghost_game_end( s.util, s.ghost_game, [ &s ]( const char* name ) { race_unwired( s, std::string( "the achievement " ) + name + " (a platform service) - not run" ); } ) )
                    race_unwired( s, "VuGhostGame Game exit: a negative place or no human - UNKNOWN" );
                tick_unregister( s.ticks, &s.ghost, "Final" );
                race_ghost_last_frame( s );
            };
            // 0x486A70: the ghosts' 0x4833C0 / 0x483830 are the rules boats' distances and checkpoints (a ghost is a rules boat), the clocks 0x4831D0, the places 0x486C60 over +0x18 listed 0 .. n - 1,
            // "Finished" = the human's +0x32F, the HUD.
            state->m_tick = [ &s ]( float dt ) {
                race_rules_clocks( s.rules_boats, dt );
                std::vector< int >& order = s.rules_game.order;
                order.resize( s.rules_boats.size( ) );
                for ( std::size_t k = 0; k < order.size( ); ++k )
                    order[ k ] = static_cast< int >( k );
                race_rules_places( s.rules_game, s.rules_boats );
                if ( !s.rules.humans.empty( ) )
                    s.rules.fsm.set_condition( "Finished", s.rules.humans[ 0 ][ 0x32F ] != 0 );
                race_rules_hud( s.rules, 1, race_hud_verb_t::tick, dt );
            };
            state->m_draw = [ &s ] { race_rules_hud( s.rules, 1, race_hud_verb_t::draw ); };
        }
        else if ( state->m_name == "PostGame" )
        {
            // 0x486EC0, 0x4872A0, 0x4872D0 ( +0x724 >= 4 ), 0x487370.
            state->m_enter = [ &s ] {
                race_rules_hud( s.rules, 2, race_hud_verb_t::initialize );
                if ( s.rules.hooks.broadcast )
                    s.rules.hooks.broadcast( "OnSetAllowPause", false );
                race_ghost_fill( s );
                ghost_game_post_t h;
                h.progress = [ &s ]( const std::string& track, std::int32_t place, float time ) {
                    if ( s.world.save_doc )
                        ui_progress_ghost_add( *s.world.save_doc, track, place, time );
                };
                h.store = [ &s ]( const std::vector< std::uint8_t >& blob ) {
                    if ( s.world.save_doc && !ghost_store_best( blob, 0, s.game.track ) )
                        race_unwired( s, "the ghost's PUT: the file did not write" );
                    LOG_INFO( "ghost: a new best: %zu bytes stored for %s", blob.size( ), s.game.track.c_str( ) );
                };
                h.leaderboard = [ &s ]( const std::string& name, std::int64_t ) { race_unwired( s, "the leaderboard 0x4B0650 (online - a hook): " + name ); };
                h.voice = [ &s ]( const char* line ) { race_session_vo_say( s, s.rules.humans.empty( ) ? nullptr : s.rules.humans[ 0 ], line, true ); };
                if ( s.ghost_game.human )
                    ghost_game_post( s.ghost_game, s.rules.humans.size( ), s.ghost.record, h );
            };
            state->m_exit = [ &s ] { race_rules_hud( s.rules, 2, race_hud_verb_t::release ); };
            state->m_tick = [ &s ]( float dt ) {
                s.rules.fsm.set_condition( "Exit", s.rules.fsm.m_time >= 4.0f );
                race_rules_hud( s.rules, 2, race_hud_verb_t::tick, dt );
            };
            state->m_draw = [ &s ] { race_rules_hud( s.rules, 2, race_hud_verb_t::draw ); };
        }
        else if ( state->m_name == "ShowError" )
        {
            // 0x4873D0: the tick manager's vt+0x2C, every game boat's +0x308 += 1, the "Ghost_Heading" box (the UI's), exit 0x487560 undoes it.
            state->m_enter = [ &s ] {
                race_rules_hold( s.rules, 1 );
                race_unwired( s, "VuGhostGame ShowError: the \"Ghost_Heading\" VuMessageBox (UI, not recreated): " + s.ghost_loads.message );
            };
            state->m_exit = [ &s ] { race_rules_hold( s.rules, -1 ); };
        }
    }
    s.rules.hooks.gates = nullptr;   // slot 7 0x485380's copy over the +0x75C list's boats: NOT DONE, the ghost's own Game enter copy stands
}
