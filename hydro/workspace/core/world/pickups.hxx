#pragma once

#include <workspace/core/boat/powerup.hxx>
#include <workspace/core/world/session.hxx>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// The level's power-ups made live (world batch 35): VuPowerUpEntity's gameInitialize 0x43B9D0, the body 0x43C700, the
// Decision tick 0x43BDB0, the contact part +0x5C (0x43C8C0) and slots 12 / 13 / 15, all boat/powerup.hxx's on OUR session.
// Boosts (37 on Hydroelectric), triggers (3), hidden packages (10; slots 6 0x43DD20, 13 0x43DE20, 15 0x43DE80, on world/records.hxx's
// manager: with the saves off no document answers, so they are not made live). Hooks answered here:
//   an entity id (0x500D50, FNV-1a of the long name) is the boat's index + 1: SUBSTITUTION, the map's key only;
//   the ApplyPfx (0x52BAC0 + the spring constraint: read to 0x43C02D, tools tmp/b38/d_43BDB0.txt) and the StaticPfx's scale (vt+0x10: no slot
//   in boat_pfx_ops_t) are logged, not run; gameRelease 0x43BCA0 is not run (the session's teardown drops the bodies).

// 0x43C700: a btSphereShape (vtable 0x73ADD4: type 8, +0x10 scaling (1, 1, 1, 0), +0x20 and +0x30 the radius), mass 0, the
// entity's world, group 0x20, mask 0x10 (the boats), +0xD8 |= 4, +0x2C4 the +0x5C part (a marker word here).
inline bool race_powerup_body( race_session_t& s, race_session_t::powerup_live_t& u, std::uint32_t& id )
{
    transform_component_t* t = entity_transform( *u.entity );
    if ( !t || u.entity->m_world_unknown )
        return false;
    auto b = std::make_unique< level_body_t >( );
    b->name = u.entity->m_name;
    b->convex = convex_shape_t{ };
    b->convex.type = 8;
    b->convex.local_scaling[ 0 ] = b->convex.local_scaling[ 1 ] = b->convex.local_scaling[ 2 ] = 1.0f;
    b->convex.implicit[ 0 ] = u.p.radius;
    b->convex.margin = u.p.radius;
    const float inertia[ 4 ] = { };
    boat_4131e0_t cinfo;
    boat_4131e0( cinfo, 0.0f, 0u, 0, reinterpret_cast< const std::uint8_t* >( inertia ) );
    float bt[ 16 ];
    bodies_bt_from_vu( t->world, bt );
    std::memcpy( cinfo.image + 0x10, bt, 64 );
    if ( !vu_rigid_body_construct( b->body, cinfo.image, 0x20, 0x10, id, nullptr, nullptr ) )
        return false;
    body_word( b->body, 0xD8 ) |= 4u;
    body_word( b->body, 0x2C4 ) = 1u;
    b->collision = collision_object_t{ };
    b->collision.body = concave_body_t{ b->body.body.basis, &b->convex, nullptr, &b->body.body, nullptr, rigid_bits( body_word( b->body, 0xC4 ) ) };
    u.body = b.get( );
    s.dynamics->objects[ &b->body ] = &b->collision;
    s.level_bodies.push_back( std::move( b ) );
    return true;
}

// slots 13 / 12's hooks. The StaticPfx 0x43C5E0: the manager's create 0x52BCD0, placed at the transform (vt+8), started.
inline powerup_world_hooks_t race_powerup_world_hooks( race_session_t& s, race_session_t::powerup_live_t& u )
{
    powerup_world_hooks_t h;
    h.create_pfx = [ &s, &u ]( const std::string& name, std::uint8_t, float ) -> std::uint32_t {
        if ( name.empty( ) || !s.world.pfx || !s.world.pfx_manager || !s.world.pfx_ops )
            return 0u;
        pfx_manager_t& m = *s.world.pfx_manager;
        const int e = pfx_manager_create( m, name.c_str( ), *s.world.pfx_ops );
        if ( e < 0 )
            return 0u;
        const std::uint32_t handle = pfx_manager_handle( m, e );
        const int x = handle ? pfx_manager_lookup( m, handle ) : -1;
        const transform_component_t* t = entity_transform( *u.entity );
        if ( x >= 0 && t )
        {
            const boat_pfx_ops_t o = boat_pfx_runtime_ops( *s.world.pfx );
            const std::uint32_t inst = m.entries[ static_cast< std::size_t >( x ) ].instance;
            o.set_matrix( o.user, inst, t->world );
            o.start( o.user, inst );
        }
        return handle;
    };
    h.release_pfx = [ &s ]( std::uint32_t handle ) {
        if ( !s.world.pfx_manager || !s.world.pfx_ops )
            return;
        const int x = pfx_manager_lookup( *s.world.pfx_manager, handle );
        if ( x >= 0 )
            pfx_manager_release( *s.world.pfx_manager, x, *s.world.pfx_ops );
    };
    h.add_body = [ &s, &u ] {
        if ( u.body && !vu_dynamics_add_rigid_body( s.dynamics->impl, u.body->body ) )
            race_unwired( s, "addRigidBody (g_vudynamicsimpl vt+0x3C) refused a power-up's body" );
    };
    h.remove_body = [ &s, &u ] {
        if ( u.body )
            vu_dynamics_remove_rigid_body( s.dynamics->impl, u.body->body );
    };
    return h;
}

// 0x4D3410: the file name of GameData.Level (0x564AE0: past the last '/').
inline std::string race_level_name( const race_session_t& s )
{
    const std::string level = race_game_data_text( s.util, nullptr, "Level" );
    const std::size_t slash = level.rfind( '/' );
    return slash == std::string::npos ? level : level.substr( slash + 1 );
}

// The manager's slots on the game save (world/records.hxx).
inline powerup_package_hooks_t race_powerup_package_hooks( race_session_t& s, race_session_t::powerup_live_t& u )
{
    powerup_package_hooks_t p;
    p.is_found = [ &s ]( const std::string& name ) { return package_is_found( *s.world.save_doc, name ); };
    p.set_found = [ &s ]( const std::string& name ) {
        package_set_found( *s.world.save_doc, name, s.world.save_game, [ &s ]( const char*, float ) { race_unwired( s, "the \"Collector\" achievement 0x4AA1A0 (a platform service) - not run" ); } );
    };
    p.level = [ &s ] { return race_level_name( s ); };
    p.first = [ &s ]( const std::string& level, std::int32_t& value ) { return package_in_level( *s.world.hidden_db, level, value ); };
    p.second = [ &s ]( const std::string& level, std::int32_t& value ) { package_found_in_level( *s.world.save_doc, level, value ); };
    p.broadcast = [ &s ]( const std::vector< std::uint32_t >& w ) {
        ui_params_t params;
        ui_params_add( params, w[ 0 ], w[ 1 ] );
        ui_params_add( params, w[ 2 ], w[ 3 ] );
        race_session_post( s, "OnPackageFound", &params );
    };
    p.remove_from_world = [ &s, &u ] { powerup_remove_from_world( u.p, race_powerup_world_hooks( s, u ) ); };
    return p;
}

// slot 13: the boost's 0x43D960 (the "MPBoost" entry with more than one human), every other type's 0x43BD40.
inline void race_powerup_enable( race_session_t& s, race_session_t::powerup_live_t& u )
{
    const powerup_world_hooks_t h = race_powerup_world_hooks( s, u );
    if ( u.boost )
        powerup_boost_enable( u.p, u.randomize, u.image[ 0x140 ], static_cast< std::int32_t >( s.manager.lists.humans.boats.size( ) ),
                              powerup_db_entry( s.powerup_db, "MPBoost" ), h );
    else if ( u.package )
        powerup_package_enable( u.p, u.package_name, race_powerup_package_hooks( s, u ), h );
    else
        powerup_add_to_world( u.p, h );
}

inline bool race_session_is_human( const race_session_t& s, const std::uint8_t* boat )
{
    for ( const void* b : s.manager.lists.humans.boats )
        if ( b == boat )
            return true;
    return false;
}

// The Decision tick 0x43BDB0.
inline void race_powerup_tick( void* user, float dt )
{
    race_session_t::powerup_live_t& u = *static_cast< race_session_t::powerup_live_t* >( user );
    race_session_t& s = *u.session;
    transform_component_t* t = entity_transform( *u.entity );
    if ( !t )
        return;
    powerup_tick_hooks_t h;
    h.water_height = [ &s ]( const float* p ) -> float {
        water_vertex_t o{ };
        if ( !s.world.view || !water_point_height( s.world.view->water, p, water_view_kernel, const_cast< water_view_t* >( s.world.view ), o ) )
        {
            race_unwired( s, "a power-up's wave height 0x529140 unanswered - not moved" );
            return -3.4028235e+38f;
        }
        return o.height;
    };
    h.find = [ &s ]( std::uint32_t id ) -> void* {
        return id >= 1u && id <= s.boats.size( ) ? s.boats[ id - 1u ].get( ) : nullptr;
    };
    h.is_boat = [ ]( void* ) { return true; };   // powerup_contact only takes boats' ids here
    h.apply = [ &s, &u ]( void* v ) -> bool {
        race_session_boat_t& b = *static_cast< race_session_boat_t* >( v );
        std::uint8_t* boat = b.graph.objects[ 0 ].image.data( );
        if ( u.package )
            return powerup_package_apply( true, race_session_is_human( s, boat ), u.package_name, race_powerup_package_hooks( s, u ) );
        if ( u.boost )
        {
            ++s.powerups_applied;
            return powerup_boost_apply( u.image.data( ), boat, *s.random );
        }
        return powerup_trigger_apply( race_session_is_human( s, boat ), true, [ &s, &u ] {
            if ( s.world.fire )
                s.world.fire( *u.entity, "OnTrigger" );
        } );
    };
    h.apply_pfx = [ &s ]( void* ) { race_unwired( s, "a power-up's ApplyPfx (0x52BAC0 + VuPfxBoatSpringConstraint) - not run" ); };
    h.driver_38 = [ ]( void* ) { return true; };   // 0x404BD0 in all five driver vtables
    h.driver_30 = [ &s ]( void* v ) { return race_session_is_human( s, static_cast< race_session_boat_t* >( v )->graph.objects[ 0 ].image.data( ) ); };
    h.sound = [ &s, &u ]( const float* position ) {
        // FMOD getEvent ( AudioEvent, 1 ); on success set3DAttributes ( the position, 0, 0 ) and start.
        const sound_hooks_t* a = s.world.sound;
        std::uint32_t event = 0;
        if ( !a || !a->get_event || a->get_event( a->user, u.p.audio_event.c_str( ), 1u, &event, nullptr ) != 0 )
            return;
        if ( a->attributes )
            a->attributes( a->user, event, position, nullptr, nullptr );
        if ( a->start )
            a->start( a->user, event );
    };
    h.voice = [ &s, &u ]( void* v ) {
        race_session_vo_say( s, static_cast< race_session_boat_t* >( v )->graph.objects[ 0 ].image.data( ), u.p.voice_over.c_str( ), false );
    };
    if ( !powerup_decision_tick( u.p, t->world + 12, dt, h ) )
        race_unwired( s, "a power-up's pickup named no boat - UNKNOWN" );
}

// VuPowerUpEntity's slot 6 0x43B9D0 + the DB load 0x43C1F0 on a boost / trigger. False: not one of those.
inline bool race_session_powerup( race_session_t& s, entity_t& e, std::uint32_t& id, const c_archive& archive )
{
    const bool boost = e.m_type == "VuPowerUpBoostEntity";
    const bool package = e.m_type == "VuPowerUpHiddenPackageEntity";
    if ( package && ( !s.world.save_doc || !s.world.hidden_db ) )
    {
        race_unwired( s, "VuPowerUpHiddenPackageEntity's gameInitialize with the saves off - not made live" );
        return true;
    }
    if ( !boost && !package && e.m_type != "VuPowerUpTriggerEntity" )
        return false;
    const auto* props = entity_action< powerup_properties_t >( e );
    if ( !props || !entity_transform( e ) )
    {
        race_unwired( s, e.m_type + " without its properties / transform - gameInitialize 0x43B9D0 not run" );
        return true;
    }
    if ( s.powerup_db.tag != 6 )
    {
        std::vector< std::uint8_t > bytes;
        if ( archive.read( "VuDBAsset/PowerUpDB", bytes ) )
            s.powerup_db = c_reader( bytes ).parse( );
    }
    auto u = std::make_unique< race_session_t::powerup_live_t >( );
    u->session = &s;
    u->entity = &e;
    u->boost = boost;
    u->package = package;
    if ( package )   // slot 6 0x43DD20: +0x138, before the base gameInitialize
    {
        u->package_name = race_entity_path( &e );   // 0x500BF0's long name holds the root's name first: powerup_package_name drops just that, which this never had
        LOG_INFO( "saves: hidden package %s (%s)", u->package_name.c_str( ), package_is_found( *s.world.save_doc, u->package_name ) ? "found" : "not found" );
    }
    u->randomize = props->randomize;
    u->image.assign( 0x144, 0 );
    powerup_t& p = u->p;
    p.initially_enabled = props->initially_enabled;
    p.type = props->type;
    p.reflect = props->reflect;
    p.handle_wave_height = props->handle_wave_height;
    p.height_above_water = props->height_above_water;
    p.voice_over = props->voice_over;
    const value_t& entry = powerup_db_entry( s.powerup_db, p.type );
    powerup_load_db( p, entry );
    if ( boost )
    {
        float energy = 0.0f;
        powerup_boost_load_energy( energy, entry );
        std::memcpy( u->image.data( ) + 0x13C, &energy, 4 );
    }
    std::vector< std::uint32_t > racers;
    for ( std::size_t k = 0; k < s.boats.size( ); ++k )
        racers.push_back( static_cast< std::uint32_t >( k + 1 ) );
    race_session_t::powerup_live_t& live = *u;
    powerup_init_hooks_t h;
    h.create_body = [ & ] {
        if ( !race_powerup_body( s, live, id ) )
            race_unwired( s, "a power-up's body 0x43C700: no known world / the ctor refused - no body" );
    };
    h.register_tick = [ & ] { tick_register( s.ticks, { &live, race_powerup_tick, &live }, "Decision" ); };
    h.enable = [ & ] { race_powerup_enable( s, live ); };
    const std::string mode = s.start.slalom ? "Slalom" : s.start.timetrial ? "TimeTrial" : s.start.ghost ? "Ghost" : "Race";
    powerup_game_initialize( p, mode, powerup_disabled_in_games( entry ), racers, h );
    s.powerups.push_back( std::move( u ) );
    return true;
}

// "Enable" 0x43B140 / "Disable" 0x43B160 -> vt+0x34 / vt+0x30. `path` the entity's path as the graph names it.
inline bool race_powerup_plug( race_session_t& s, const std::string& entity, const std::string& plug )
{
    if ( plug != "Enable" && plug != "Disable" )
        return false;
    for ( const auto& u : s.powerups )
    {
        if ( race_entity_path( u->entity ) != entity )
            continue;
        if ( plug == "Enable" )
            race_powerup_enable( s, *u );
        else
            powerup_remove_from_world( u->p, race_powerup_world_hooks( s, *u ) );
        return true;
    }
    return false;
}

// The +0x5C part's slot 0 0x43C8C0 on a contact record: `other` a session boat, else nothing.
inline bool race_powerup_contact( race_session_t& s, const solver_rigid_t* self, const solver_rigid_t* other )
{
    for ( const auto& u : s.powerups )
    {
        if ( !u->body || &u->body->body.body != self )
            continue;
        for ( std::size_t k = 0; k < s.boats.size( ); ++k )
            if ( &s.boats[ k ]->body.body == other )
                powerup_contact( u->p, true, static_cast< std::uint32_t >( k + 1 ) );
        return true;
    }
    return false;
}
