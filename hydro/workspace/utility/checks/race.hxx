#pragma once

#include <workspace/core/boat/camera.hxx>
#include <workspace/core/boat/respawn.hxx>
#include <workspace/core/boat/anim.hxx>
#include <workspace/core/boat/human.hxx>
#include <workspace/core/boat/pilots.hxx>
#include <workspace/core/boat/manager.hxx>
#include <workspace/core/boat/powerup.hxx>
#include <workspace/core/boat/meshes.hxx>
#include <workspace/core/boat/surface.hxx>
#include <workspace/core/boat/nuisance.hxx>
#include <workspace/core/boat/contact.hxx>
#include <workspace/core/boat/events.hxx>
#include <workspace/core/boat/damage.hxx>
#include <workspace/core/collision/leaves.hxx>
#include <workspace/utility/checks/rays.hxx>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

// The boat area's race-time checks (the camera, the respawn, the animation tick, the drivers): included by
// check.hxx after checks/boat.hxx. verify_race_all runs them all.

// VuBoatCamera_tick 0x409900 against retail's own runs (oracle case boat_camera): sequences of ticks on one
// camera image (retail's ctor 0x409750 plus authored fields), each tick's boat / env / scripted ray hits fed
// to boat_camera_tick and the camera image after compared word for word (but the stale-stack words the
// case's two stack fills disagree on), plus every ray's from / to.
inline bool verify_boat_camera( )
{
    const unsigned* g = g_golden_boat_camera_words;
    struct ray_script_t
    {
        std::vector< std::vector< ai_ray_hit_t > > rays;
        std::vector< std::array< float, 8 > > seen;
        std::size_t next = 0;
    } script;
    auto ray = []( void* user, const float* from, const float* to, const ai_ray_hit_t** hits ) -> int
    {
        ray_script_t& s = *static_cast< ray_script_t* >( user );
        std::array< float, 8 > rec{ from[ 0 ], from[ 1 ], from[ 2 ], 0.0f, to[ 0 ], to[ 1 ], to[ 2 ], 0.0f };
        s.seen.push_back( rec );
        if ( s.next >= s.rays.size( ) )
            return 0;
        std::vector< ai_ray_hit_t >& h = s.rays[ s.next++ ];
        *hits = h.data( );
        return static_cast< int >( h.size( ) );
    };
    auto u = []( float v ) { std::uint32_t w; std::memcpy( &w, &v, 4 ); return w; };
    auto fl = []( unsigned w ) { float v; std::memcpy( &v, &w, 4 ); return v; };
    int compared = 0, failed = 0, ticks = 0, bad_ticks = 0;
    const unsigned ns = *g++;
    for ( unsigned s = 0; s < ns; ++s )
    {
        const unsigned nt = *g++;
        std::uint8_t cam[ 0x308 ];
        std::memcpy( cam, g, 0x308 );
        g += 0x308 / 4;
        for ( unsigned t = 0; t < nt; ++t, ++ticks )
        {
            const float dt = fl( *g++ );
            float world[ 16 ];
            std::memcpy( world, g, 64 );
            g += 16;
            camera_boat_t boat;
            boat.world = world;
            boat.destroyed = *g++ != 0;
            boat.debris_count = static_cast< std::int32_t >( *g++ );
            std::uint8_t piece[ 0x1C0 ];
            std::memcpy( piece, g, 0x1C0 );
            g += 0x1C0 / 4;
            boat.piece = piece;
            float vel[ 4 ];
            std::memcpy( vel, g, 16 );
            g += 4;
            boat.velocity = vel;
            std::uint8_t engine[ 0x10 ] = { };
            std::memcpy( engine + 4, g, 4 );
            std::memcpy( engine + 0xC, g + 1, 4 );
            boat.engine = engine;
            boat.engine_owner_470 = fl( g[ 2 ] );
            boat.far_plane = fl( g[ 3 ] );
            g += 4;
            camera_env_t env;
            env.driver_34 = *g++ != 0;
            env.control_method = static_cast< std::int32_t >( *g++ );
            env.display_w = static_cast< std::int32_t >( *g++ );
            env.display_h = static_cast< std::int32_t >( *g++ );
            env.vertical_fov.type = static_cast< std::int32_t >( *g++ );
            env.vertical_fov.payload = g[ 0 ] | ( static_cast< std::uint64_t >( g[ 1 ] ) << 32 );
            g += 2;
            const unsigned nb = *g++;
            std::vector< std::vector< std::uint8_t > > bodies( nb );
            for ( unsigned k = 0; k < nb; ++k, g += 0x300 / 4 )
                bodies[ k ].assign( reinterpret_cast< const std::uint8_t* >( g ), reinterpret_cast< const std::uint8_t* >( g ) + 0x300 );
            script.rays.assign( 3, { } );
            script.seen.clear( );
            script.next = 0;
            for ( int r = 0; r < 3; ++r )
            {
                const unsigned nh = *g++;
                for ( unsigned j = 0; j < nh; ++j, g += 7 )
                {
                    ai_ray_hit_t h;
                    h.body = bodies[ g[ 0 ] ].data( );
                    h.fraction = fl( g[ 1 ] );
                    std::memcpy( h.normal, g + 2, 16 );
                    h.world = g[ 6 ] != 0;
                    script.rays[ r ].push_back( h );
                }
            }
            env.user = &script;
            env.ray = ray;
            // the shake's eye / target w lanes are a stack slot the mode function's frame left (UNKNOWN here)
            const bool shook = state_f( cam, 0x2E8 ) > 0.0f;
            const int failed_before = failed;
            const bool ran = boat_camera_tick( cam, boat, env, dt );
            const unsigned* after = g;
            g += 0x308 / 4;
            const unsigned* mask = g;
            g += 7;
            ++compared;
            if ( !ran )
            {
                ++failed;
                if ( failed <= 8 )
                    LOG_ERROR( "boat_camera seq %u tick %u: the tick refused (mode UNKNOWN)", s, t );
            }
            for ( int w = 0; w < 0x308 / 4; ++w )
            {
                if ( mask[ w / 32 ] >> ( w % 32 ) & 1 )
                    continue;
                if ( shook && ( 4 * w == 0x0C || 4 * w == 0x1C || 4 * w == 0x2C0 ) )
                    continue;
                std::uint32_t mine;
                std::memcpy( &mine, cam + 4 * w, 4 );
                ++compared;
                if ( mine != after[ w ] )
                {
                    ++failed;
                    if ( failed <= 8 )
                        LOG_ERROR( "boat_camera seq %u tick %u +0x%X: 0x%08X, retail 0x%08X", s, t, 4 * w, mine, after[ w ] );
                }
            }
            // carry retail's state (the stale words included) into the next tick, as the case does
            std::memcpy( cam, after, 0x308 );
            const unsigned nc = *g++;
            ++compared;
            if ( nc != script.seen.size( ) )
            {
                ++failed;
                if ( failed <= 8 )
                    LOG_ERROR( "boat_camera seq %u tick %u: %zu rays, retail %u", s, t, script.seen.size( ), nc );
            }
            for ( unsigned c = 0; c < nc; ++c, g += 8 )
                for ( int k = 0; k < 8 && c < script.seen.size( ); ++k )
                {
                    if ( k == 3 || k == 7 )
                        continue;
                    ++compared;
                    if ( u( script.seen[ c ][ k ] ) != g[ k ] )
                    {
                        ++failed;
                        if ( failed <= 8 )
                            LOG_ERROR( "boat_camera seq %u tick %u ray %u lane %d: 0x%08X, retail 0x%08X", s, t, c, k, u( script.seen[ c ][ k ] ), g[ k ] );
                    }
                }
            bad_ticks += failed != failed_before ? 1 : 0;
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuBoatCamera_tick 0x409900 (the five modes 0x409D00 / 0x40A4D0 / 0x40AAC0 / 0x40AC10 / 0x40AD60, the "
                     "shake 0x40AEB0, the collision 0x40B090, the projection 0x50BBE0 / view 0x50BDA0 / frustum "
                     "0x50C550 + 0x57CBA0) matches retail: %d ticks in %u sequences, %d values, bit for bit",
            ticks, ns, compared );
        return true;
    }
    LOG_ERROR( "boat_camera FAILED %d of %d values (%d of %d ticks)", failed, compared, bad_ticks, ticks );
    return false;
}

// The respawn 0x41D790 / 0x41DA90 against retail's own runs on Hydroelectric's track (oracle case
// boat_respawn): the track built by track_build_from_tree (as verify_track_build), each vector's blocks fed to
// boat_respawn with the scripted water answers; every word of the six blocks after (but the stale ones) and the
// hook log (order, the water points, the event hash) compared.
inline bool verify_boat_respawn( )
{
    const std::unique_ptr< entity_t > tree = track_document_tree( );
    track_manager_t tm;
    if ( !tree || !track_build_from_tree( *tree, tm ) )
    {
        LOG_ERROR( "boat_respawn: the track did not build" );
        return false;
    }
    static const int sizes[ 6 ] = { 0x520, 0xC8, 0x2D4, 0x10, 0x308, 0x2C };
    struct run_t
    {
        const unsigned* heights = nullptr;
        unsigned next = 0;
        std::vector< unsigned > log;
    } run;
    boat_respawn_hooks_t hooks;
    hooks.user = &run;
    hooks.water = []( void* u, const float* p ) {
        run_t& r = *static_cast< run_t* >( u );
        r.log.push_back( 1 );
        for ( int k = 0; k < 3; ++k )
        {
            std::uint32_t w;
            std::memcpy( &w, p + k, 4 );
            r.log.push_back( w );
        }
        const unsigned bits = r.next < 8 ? r.heights[ r.next ] : 0xFF7FFFFFu;
        ++r.next;
        return respawn_bits( bits );
    };
    hooks.transform_children = []( void* u ) { static_cast< run_t* >( u )->log.push_back( 2 ); };
    hooks.instigator = []( void* u ) { static_cast< run_t* >( u )->log.push_back( 3 ); };
    hooks.enable = []( void* u ) { static_cast< run_t* >( u )->log.push_back( 6 ); };
    hooks.driver_50 = []( void* u ) { static_cast< run_t* >( u )->log.push_back( 7 ); };
    hooks.recovered = []( void* u, std::uint32_t hash ) {
        run_t& r = *static_cast< run_t* >( u );
        r.log.push_back( 4 );
        r.log.push_back( 5 );
        r.log.push_back( hash );
    };
    const unsigned* g = g_golden_boat_respawn_words;
    int compared = 0, failed = 0, bad = 0;
    const unsigned nv = *g++;
    for ( unsigned v = 0; v < nv; ++v )
    {
        std::vector< std::uint8_t > blocks[ 6 ];
        for ( int k = 0; k < 6; ++k )
        {
            blocks[ k ].assign( reinterpret_cast< const std::uint8_t* >( g ), reinterpret_cast< const std::uint8_t* >( g ) + sizes[ k ] );
            g += sizes[ k ] / 4;
        }
        const bool has_proxy = *g++ != 0;
        const std::int32_t checkpoint = static_cast< std::int32_t >( *g++ );
        run.heights = g;
        run.next = 0;
        run.log.clear( );
        g += 8;
        const unsigned used = *g++;
        vu_rigid_body_t body;
        std::memcpy( &body, blocks[ 2 ].data( ), 0x2D4 );
        boat_respawn_t r;
        r.boat = blocks[ 0 ].data( );
        r.transform = blocks[ 1 ].data( );
        r.body = &body;
        r.proxy = has_proxy ? blocks[ 3 ].data( ) : nullptr;
        r.camera = blocks[ 4 ].data( );
        r.instigator = blocks[ 5 ].data( );
        r.checkpoint = checkpoint;
        boat_respawn( tm, r, hooks );
        std::memcpy( blocks[ 2 ].data( ), &body, 0x2D4 );
        const int failed_before = failed;
        const unsigned* after[ 6 ];
        for ( int k = 0; k < 6; ++k )
        {
            after[ k ] = g;
            g += sizes[ k ] / 4;
        }
        for ( int k = 0; k < 6; ++k )
        {
            std::vector< bool > stale( sizes[ k ] / 4, false );
            const unsigned ns = *g++;
            for ( unsigned j = 0; j < ns; ++j )
                stale[ *g++ ] = true;
            for ( int w = 0; w < sizes[ k ] / 4; ++w )
            {
                if ( stale[ w ] )
                    continue;
                std::uint32_t mine;
                std::memcpy( &mine, blocks[ k ].data( ) + 4 * w, 4 );
                ++compared;
                if ( mine != after[ k ][ w ] && ++failed <= 8 )
                    LOG_ERROR( "boat_respawn vector %u block %d +0x%X: 0x%08X, retail 0x%08X", v, k, 4 * w, mine, after[ k ][ w ] );
            }
        }
        std::vector< unsigned > expect;
        const unsigned nl = *g++;
        for ( unsigned j = 0; j < nl; ++j )
        {
            const unsigned code = *g++;
            expect.push_back( code );
            if ( code == 1 )
                for ( int k = 0; k < 3; ++k )
                    expect.push_back( *g++ );
            else if ( code == 5 )
                expect.push_back( *g++ );
        }
        ++compared;
        if ( ( expect != run.log || used != run.next ) && ++failed <= 8 )
            LOG_ERROR( "boat_respawn vector %u: the hook log differs (%zu words vs retail %zu; water %u vs %u)", v, run.log.size( ), expect.size( ), run.next, used );
        bad += failed != failed_before ? 1 : 0;
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "The respawn 0x41D790 (slot 22, the sector walk, 0x4D29F0) and the reset 0x41DA90 match retail on "
                     "Hydroelectric's track: %u vectors, %d values, bit for bit", nv, compared );
        return true;
    }
    LOG_ERROR( "boat_respawn FAILED %d of %d values (%d of %u vectors)", failed, compared, bad, nv );
    return false;
}

// The animation tick 0x40FFF0 (sequences on one object, retail's state carried) and the wake parameters
// 0x417F80 against retail (oracle case boat_anim): the object's and its controls' words after, the hook and
// FMOD streams; the 0x417F80 out block but its stale words.
inline bool verify_boat_anim( )
{
    const unsigned* g = g_golden_boat_anim_words;
    auto fl = []( unsigned w ) { float v; std::memcpy( &v, &w, 4 ); return v; };
    struct log_t
    {
        float s64 = 0.0f, s68 = 0.0f;
        const unsigned* create = nullptr;
        std::vector< unsigned > a, b;
    } lg;
    boat_anim_hooks_t hooks;
    hooks.user = &lg;
    hooks.driver_64 = []( void* u ) { return static_cast< log_t* >( u )->s64; };
    hooks.driver_68 = []( void* u ) { return static_cast< log_t* >( u )->s68; };
    hooks.control_516300 = []( void* u ) { static_cast< log_t* >( u )->a.push_back( 1 ); };
    hooks.event_create = []( void* u, int slot, const char* ) {
        log_t& l = *static_cast< log_t* >( u );
        const unsigned ok = l.create[ slot ];
        l.a.insert( l.a.end( ), { 2u, static_cast< unsigned >( slot ), ok } );
        return ok != 0;
    };
    hooks.event_start = []( void* u, int ) { static_cast< log_t* >( u )->b.push_back( 3 ); };   // retail starts slot k's event even when it is null
    hooks.voice = []( void* u, const char* ) { static_cast< log_t* >( u )->a.push_back( 4 ); };
    hooks.event_update = []( void* u, int slot, const float* p, const float* v, const float* values ) {
        log_t& l = *static_cast< log_t* >( u );
        l.b.push_back( 5 );
        l.b.push_back( static_cast< unsigned >( slot ) );
        for ( int k = 0; k < 6; ++k )
        {
            std::uint32_t w;
            std::memcpy( &w, k < 3 ? p + k : v + k - 3, 4 );
            l.b.push_back( w );
        }
        for ( int k = 0; k < 3; ++k )
        {
            std::uint32_t w;
            std::memcpy( &w, values + k, 4 );
            l.b.push_back( 6 );
            l.b.push_back( w );
        }
    };
    int compared = 0, failed = 0, ticks = 0, bad = 0;
    auto cmp = [ & ]( const char* what, int a, int b, std::uint32_t ours, std::uint32_t theirs ) {
        ++compared;
        if ( ours != theirs && ++failed <= 8 )
            LOG_ERROR( "boat_anim %s %d +0x%X: 0x%08X, retail 0x%08X", what, a, b, ours, theirs );
    };
    const unsigned ns = *g++;
    for ( unsigned s = 0; s < ns; ++s )
    {
        const unsigned nt = *g++;
        bool events[ 3 ];
        for ( int k = 0; k < 3; ++k )
            events[ k ] = *g++ != 0;
        g += 3;   // the names' lengths (the emitter checked retail's names)
        for ( unsigned t = 0; t < nt; ++t, ++ticks )
        {
            const int failed_before = failed;
            const float dt = fl( *g++ );
            lg.s64 = fl( *g++ );
            lg.s68 = fl( *g++ );
            bool present[ 3 ];
            for ( int k = 0; k < 3; ++k )
                present[ k ] = *g++ != 0;
            std::uint8_t assets[ 3 ][ 0x20 ] = { };
            for ( int k = 0; k < 3; ++k )
                std::memcpy( assets[ k ] + 0x18, g++, 4 );
            std::uint8_t boat[ 0x520 ] = { };
            std::memcpy( boat + 0x310, g++, 4 );
            boat[ 0x32F ] = static_cast< std::uint8_t >( *g++ );
            lg.create = g;
            g += 3;
            float vel[ 4 ], pos[ 4 ];
            std::memcpy( vel, g, 16 );
            g += 4;
            std::memcpy( pos, g, 16 );
            g += 4;
            std::uint8_t engine[ 0x100 ] = { };
            std::memcpy( engine + 0xEC, g, 12 );
            g += 3;
            std::uint8_t obj[ 0xE0 ], ctl[ 3 ][ 0x20 ];
            std::memcpy( obj, g, 0xE0 );
            g += 0xE0 / 4;
            for ( int k = 0; k < 3; ++k, g += 8 )
                std::memcpy( ctl[ k ], g, 0x20 );
            boat_anim_t a;
            a.anim = obj;
            a.boat = boat;
            for ( int k = 0; k < 3; ++k )
            {
                a.control[ k ] = present[ k ] ? ctl[ k ] : nullptr;
                a.asset[ k ] = assets[ k ];
                a.event[ k ] = events[ k ];
            }
            a.position = pos;
            a.velocity = vel;
            a.engine = engine;
            lg.a.clear( );
            lg.b.clear( );
            boat_anim_tick( a, dt, hooks );
            for ( int w = 0; w < 0xE0 / 4; ++w, ++g )
            {
                std::uint32_t mine;
                std::memcpy( &mine, obj + 4 * w, 4 );
                cmp( "object", ticks, 4 * w, mine, *g );
            }
            for ( int k = 0; k < 3; ++k )
                for ( int w = 0; w < 8; ++w, ++g )
                {
                    std::uint32_t mine;
                    std::memcpy( &mine, ctl[ k ] + 4 * w, 4 );
                    cmp( "control", ticks * 10 + k, 4 * w, mine, *g );
                }
            const unsigned na = *g++;
            const std::vector< unsigned > ea( g, g + na );
            g += na;
            const unsigned nb = *g++;
            const std::vector< unsigned > eb( g, g + nb );
            g += nb;
            cmp( "hook stream", ticks, 0, ea == lg.a ? 0u : 1u, 0u );
            cmp( "fmod stream", ticks, 0, eb == lg.b ? 0u : 1u, 0u );
            bad += failed != failed_before ? 1 : 0;
        }
    }
    const unsigned nw = *g++;
    for ( unsigned v = 0; v < nw; ++v )
    {
        float m[ 16 ];
        std::memcpy( m, g, 64 );
        g += 16;
        std::uint8_t hull[ 0xA8 ] = { };
        std::memcpy( hull + 0x98, g, 16 );
        g += 4;
        std::uint8_t body[ 0x160 ] = { };
        std::memcpy( body + 0x140, g, 32 );
        g += 8;
        std::uint8_t solver[ 0x60 ] = { };
        std::memcpy( solver + 0x5C, g++, 4 );
        std::uint8_t out[ 0x30 ] = { };
        boat_wake_params( hull, m, body, solver, out );
        const unsigned* theirs = g;
        g += 12;
        const unsigned stale = *g++;
        for ( int w = 0; w < 12; ++w )
            if ( !( stale >> w & 1 ) )
            {
                std::uint32_t mine;
                std::memcpy( &mine, out + 4 * w, 4 );
                cmp( "wake", static_cast< int >( v ), 4 * w, mine, theirs[ w ] );
            }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "The animation tick 0x40FFF0 (%d ticks in %u sequences) and the wake parameters 0x417F80 (%u vectors) "
                     "match retail: %d values, bit for bit", ticks, ns, nw, compared );
        return true;
    }
    LOG_ERROR( "boat_anim FAILED %d of %d values (%d of %d ticks)", failed, compared, bad, ticks );
    return false;
}

// VuHumanDriver's tick 0x40F270 against retail's own runs on Hydroelectric's track (oracle case human_tick):
// each tick's blocks fed to human_tick with the scripted input, accelerometer and settings; the driver / boat /
// body / camera words after (but the stale ones; the driver's +0x84 as the sector index) and the hook stream
// compared.
inline bool verify_human_tick( )
{
    const std::unique_ptr< entity_t > tree = track_document_tree( );
    track_manager_t tm;
    if ( !tree || !track_build_from_tree( *tree, tm ) )
    {
        LOG_ERROR( "human_tick: the track did not build" );
        return false;
    }
    static const int sizes[ 8 ] = { 0xD0, 0x520, 0xC8, 0x2D4, 0x308, 0x80, 0x1D0, 0x60 };
    static const int after_of[ 4 ] = { 0, 1, 3, 4 };
    struct run_t
    {
        float axes[ 3 ] = { };
        std::uint8_t held = 0, jump = 0, recover = 0;
        float acc[ 4 ] = { };
        bool acc_ok = false;
        std::vector< unsigned > log;
    } run;
    auto code = []( const char* n ) -> unsigned {
        static const char* names[ 6 ] = { "Brake", "Throttle", "Steering", "Boost", "Jump", "Recover" };
        for ( unsigned k = 0; k < 6; ++k )
            if ( std::strcmp( n, names[ k ] ) == 0 )
                return k + 1;
        return 99;
    };
    static unsigned ( *s_code )( const char* ) = nullptr;
    s_code = code;
    human_hooks_t hooks;
    hooks.user = &run;
    hooks.axis = []( void* u, std::int32_t pad, const char* n ) {
        run_t& r = *static_cast< run_t* >( u );
        const unsigned c = s_code( n );
        r.log.insert( r.log.end( ), { 1u, static_cast< unsigned >( pad ), c } );
        return c >= 1 && c <= 3 ? r.axes[ c - 1 ] : 0.0f;
    };
    hooks.held = []( void* u, std::int32_t pad, const char* n ) -> std::uint8_t {
        run_t& r = *static_cast< run_t* >( u );
        const unsigned c = s_code( n );
        r.log.insert( r.log.end( ), { 2u, static_cast< unsigned >( pad ), c } );
        return c == 4 ? r.held : 0;
    };
    hooks.pressed = []( void* u, std::int32_t pad, const char* n ) -> std::uint8_t {
        run_t& r = *static_cast< run_t* >( u );
        const unsigned c = s_code( n );
        r.log.insert( r.log.end( ), { 3u, static_cast< unsigned >( pad ), c } );
        return c == 5 ? r.jump : c == 6 ? r.recover : 0;
    };
    hooks.accel = []( void* u, float* out ) {
        run_t& r = *static_cast< run_t* >( u );
        r.log.push_back( 4 );
        std::memcpy( out, r.acc, 16 );
        return r.acc_ok;
    };
    hooks.rumble = []( void* u, float lo, float hi ) {
        run_t& r = *static_cast< run_t* >( u );
        std::uint32_t a, b;
        std::memcpy( &a, &lo, 4 );
        std::memcpy( &b, &hi, 4 );
        r.log.insert( r.log.end( ), { 5u, 0u, 6u, a, b } );
    };
    hooks.voice = []( void* u, const char* line ) {
        run_t& r = *static_cast< run_t* >( u );
        r.log.insert( r.log.end( ), { 7u, std::strcmp( line, "NeedBoostBeforeJump" ) == 0 ? 1u : std::strcmp( line, "NeedPickUpPowerBoost" ) == 0 ? 2u : 99u } );
    };
    hooks.misfire = []( void* u ) { static_cast< run_t* >( u )->log.push_back( 8 ); };
    hooks.jump_refused = []( void* u ) { static_cast< run_t* >( u )->log.push_back( 9 ); };
    hooks.jump_sound = []( void* u ) { static_cast< run_t* >( u )->log.push_back( 10 ); };
    hooks.menu_change = []( void* u ) {
        run_t& r = *static_cast< run_t* >( u );
        r.log.insert( r.log.end( ), { 11u, 12u } );
    };
    const unsigned* g = g_golden_human_tick_words;
    int compared = 0, failed = 0, bad = 0, ticks = 0;
    const unsigned ns = *g++;
    for ( unsigned s = 0; s < ns; ++s )
    {
        const unsigned nt = *g++;
        for ( unsigned t = 0; t < nt; ++t, ++ticks )
        {
            float dt;
            std::memcpy( &dt, g++, 4 );
            human_env_t env;
            env.control_method = static_cast< std::int32_t >( *g++ );
            env.brake_only_0 = static_cast< std::uint8_t >( *g++ );
            env.brake_only_1 = static_cast< std::uint8_t >( *g++ );
            std::memcpy( &env.tilt_blend, g++, 4 );
            env.rumble = static_cast< std::uint8_t >( *g++ );
            std::memcpy( run.axes, g, 12 );
            g += 3;
            run.held = static_cast< std::uint8_t >( *g++ );
            run.jump = static_cast< std::uint8_t >( *g++ );
            run.recover = static_cast< std::uint8_t >( *g++ );
            std::memcpy( run.acc, g, 16 );
            g += 4;
            run.acc_ok = *g++ != 0;
            int sector = static_cast< std::int32_t >( *g++ );
            const int sector_after = static_cast< std::int32_t >( *g++ );
            std::vector< std::uint8_t > blocks[ 8 ];
            for ( int k = 0; k < 8; ++k )
            {
                blocks[ k ].assign( reinterpret_cast< const std::uint8_t* >( g ), reinterpret_cast< const std::uint8_t* >( g ) + sizes[ k ] );
                g += sizes[ k ] / 4;
            }
            human_pilot_t pilot;
            pilot.driver = blocks[ 0 ].data( );
            pilot.sector = sector;
            human_tick_t tick;
            tick.boat = blocks[ 1 ].data( );
            tick.transform = blocks[ 2 ].data( );
            tick.body = blocks[ 3 ].data( );
            tick.camera = blocks[ 4 ].data( );
            tick.animation = blocks[ 5 ].data( );
            tick.solver = blocks[ 7 ].data( );
            run.log.clear( );
            human_driver_tick( tm, pilot, tick, dt, env, hooks );
            const int failed_before = failed;
            const unsigned* after[ 4 ];
            for ( int k = 0; k < 4; ++k )
            {
                after[ k ] = g;
                g += sizes[ after_of[ k ] ] / 4;
            }
            for ( int k = 0; k < 4; ++k )
            {
                const int n = sizes[ after_of[ k ] ] / 4;
                std::vector< bool > stale( n, false );
                const unsigned nst = *g++;
                for ( unsigned j = 0; j < nst; ++j )
                    stale[ *g++ ] = true;
                if ( k == 0 )
                    stale[ 0x84 / 4 ] = true;   // the sector pointer: compared as the index below
                for ( int w = 0; w < n; ++w )
                {
                    if ( stale[ w ] )
                        continue;
                    std::uint32_t mine;
                    std::memcpy( &mine, blocks[ after_of[ k ] ].data( ) + 4 * w, 4 );
                    ++compared;
                    if ( mine != after[ k ][ w ] && ++failed <= 8 )
                        LOG_ERROR( "human_tick %u/%u block %d +0x%X: 0x%08X, retail 0x%08X", s, t, after_of[ k ], 4 * w, mine, after[ k ][ w ] );
                }
            }
            ++compared;
            if ( pilot.sector != sector_after && ++failed <= 8 )
                LOG_ERROR( "human_tick %u/%u: sector %d, retail %d", s, t, pilot.sector, sector_after );
            std::vector< unsigned > expect;
            const unsigned nl = *g++;
            expect.assign( g, g + nl );
            g += nl;
            ++compared;
            if ( expect != run.log && ++failed <= 8 )
                LOG_ERROR( "human_tick %u/%u: the hook stream differs (%zu words vs retail %zu)", s, t, run.log.size( ), expect.size( ) );
            bad += failed != failed_before ? 1 : 0;
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuHumanDriver's tick 0x40F270 (0x40F690 input, 0x40FA90 wrong way, 0x40FC60 + 0x41DEC0, 0x41D6B0, 0x40FD30, "
                     "slot 21) matches retail on Hydroelectric's track: %d ticks, %d values, bit for bit", ticks, compared );
        return true;
    }
    LOG_ERROR( "human_tick FAILED %d of %d values (%d of %d ticks)", failed, compared, bad, ticks );
    return false;
}

// VuGhostDriver's tick 0x40EBA0 against retail on Hydroelectric's track (oracle case ghost_tick): the +0x80
// sector after, for each position and sector before.
inline bool verify_ghost_tick( )
{
    const std::unique_ptr< entity_t > tree = track_document_tree( );
    track_manager_t tm;
    if ( !tree || !track_build_from_tree( *tree, tm ) )
    {
        LOG_ERROR( "ghost_tick: the track did not build" );
        return false;
    }
    const unsigned* g = g_golden_ghost_tick_words;
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        std::uint8_t transform[ 0xC8 ] = { };
        std::memcpy( transform + 0x80, g, 16 );
        g += 4;
        ghost_pilot_t p;
        p.sector = static_cast< std::int32_t >( *g++ );
        const int expect = static_cast< std::int32_t >( *g++ );
        ghost_driver_tick( tm, p, transform );
        if ( p.sector != expect && ++failed <= 8 )
            LOG_ERROR( "ghost_tick vector %u: sector %d, retail %d", v, p.sector, expect );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuGhostDriver's tick 0x40EBA0 matches retail on Hydroelectric's track: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "ghost_tick FAILED %d of %u vectors", failed, nv );
    return false;
}

// VuAiUiDriver's tick 0x40E5C0 against retail on Hydroelectric's track (oracle case ui_tick): each tick's blocks,
// plan and VuRandom fed to ui_driver_tick; the driver (but its boat / plan pointers) and boat words after, the plan,
// VuRandom and the hook calls compared.
inline bool verify_ui_tick( )
{
    const std::unique_ptr< entity_t > tree = track_document_tree( );
    track_manager_t tm;
    if ( !tree || !track_build_from_tree( *tree, tm ) )
    {
        LOG_ERROR( "ui_tick: the track did not build" );
        return false;
    }
    static const int sizes[ 7 ] = { 0xD4, 0x520, 0xC8, 0x2D4, 0x40, 0x80, 0x60 };
    static std::vector< unsigned > s_log;
    ai_tick_hooks_t hooks;
    hooks.controls.misfire = []( void* ) { s_log.push_back( 1 ); };
    hooks.controls.voice = []( void*, const char* line ) { s_log.push_back( std::strcmp( line, "NeedPickUpPowerBoost" ) == 0 ? 2u : 99u ); };
    const unsigned* g = g_golden_ui_tick_words;
    int compared = 0, failed = 0, bad = 0, ticks = 0;
    const unsigned ns = *g++;
    for ( unsigned s = 0; s < ns; ++s )
    {
        const unsigned nt = *g++;
        for ( unsigned t = 0; t < nt; ++t, ++ticks )
        {
            float dt;
            std::memcpy( &dt, g++, 4 );
            fleet_random_t rng;
            std::memcpy( &rng, g, 136 );
            g += 34;
            ui_pilot_t p;
            const unsigned np = *g++;
            for ( unsigned j = 0; j < np; ++j )
                p.plan.sectors.push_back( static_cast< std::int32_t >( *g++ ) );
            std::vector< std::uint8_t > blocks[ 7 ];
            for ( int k = 0; k < 7; ++k )
            {
                blocks[ k ].assign( reinterpret_cast< const std::uint8_t* >( g ), reinterpret_cast< const std::uint8_t* >( g ) + sizes[ k ] );
                g += sizes[ k ] / 4;
            }
            p.driver = blocks[ 0 ].data( );
            const std::uint8_t* boats[ 1 ] = { blocks[ 1 ].data( ) };
            ai_tick_t tick;
            tick.boat = blocks[ 1 ].data( );
            tick.transform = blocks[ 2 ].data( );
            tick.body = blocks[ 3 ].data( );
            tick.engine = blocks[ 4 ].data( );
            tick.owner = blocks[ 1 ].data( );
            tick.animation = blocks[ 5 ].data( );
            tick.solver = blocks[ 6 ].data( );
            tick.boats = boats;
            tick.boat_count = 1;
            s_log.clear( );
            ui_driver_tick( tm, p, tick, rng, dt, hooks );
            const int failed_before = failed;
            const unsigned* after[ 2 ] = { g, g + sizes[ 0 ] / 4 };
            g += ( sizes[ 0 ] + sizes[ 1 ] ) / 4;
            for ( int k = 0; k < 2; ++k )
            {
                const int n = sizes[ k ] / 4;
                std::vector< bool > stale( n, false );
                const unsigned nst = *g++;
                for ( unsigned j = 0; j < nst; ++j )
                    stale[ *g++ ] = true;   // the stale stack words (e.g. the path target's w lane, ai.hxx)
                for ( int w = 0; w < n; ++w )
                {
                    // and the boat and the plan's own words (its sectors are compared below), and +0xB4: the path
                    // target's w lane, which 0x40BEC0's first branch copies from a dead stack word (ai.hxx: UNKNOWN) -
                    // here the inner 0x40D5A0's k = dt x 10 left in the same tick's frame, so no stack fill shows it
                    if ( stale[ w ] || ( k == 0 && ( w == 0x5C / 4 || w == 0xB4 / 4 || ( w >= 0x84 / 4 && w <= 0x94 / 4 ) ) ) )
                        continue;
                    std::uint32_t mine;
                    std::memcpy( &mine, blocks[ k ].data( ) + 4 * w, 4 );
                    ++compared;
                    if ( mine != after[ k ][ w ] && ++failed <= 8 )
                        LOG_ERROR( "ui_tick %u/%u block %d +0x%X: 0x%08X, retail 0x%08X", s, t, k, 4 * w, mine, after[ k ][ w ] );
                }
            }
            std::vector< int > plan;
            const unsigned na = *g++;
            for ( unsigned j = 0; j < na; ++j )
                plan.push_back( static_cast< std::int32_t >( *g++ ) );
            const std::int32_t depth = static_cast< std::int32_t >( *g++ );
            ++compared;
            if ( ( plan != p.plan.sectors || depth != p.plan.depth ) && ++failed <= 8 )
                LOG_ERROR( "ui_tick %u/%u: the plan differs (%zu sectors, retail %zu)", s, t, p.plan.sectors.size( ), plan.size( ) );
            ++compared;
            if ( std::memcmp( &rng, g, 136 ) != 0 && ++failed <= 8 )
                LOG_ERROR( "ui_tick %u/%u: VuRandom differs", s, t );
            g += 34;
            const unsigned nl = *g++;
            ++compared;
            if ( std::vector< unsigned >( g, g + nl ) != s_log && ++failed <= 8 )
                LOG_ERROR( "ui_tick %u/%u: the hook calls differ", s, t );
            g += nl;
            bad += failed != failed_before ? 1 : 0;
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuAiUiDriver's tick 0x40E5C0 (0x40B940, the plan update 0x40E800, VuRandomTrackPlan) matches retail on "
                     "Hydroelectric's track: %d ticks, %d values, bit for bit", ticks, compared );
        return true;
    }
    LOG_ERROR( "ui_tick FAILED %d of %d values (%d of %d ticks)", failed, compared, bad, ticks );
    return false;
}

// The Human / UI / Nuisance / Ghost driver constructions (boat/driver.hxx) against retail's postLoad branches
// (oracle case boat_drivers): every object's size, every written byte, every pointer by what it points at. The
// human's method-map / event-tree nodes and the DB key temporaries (retail's objects 7..) are not objects of ours:
// its map head (object 1) is compared by bytes only, and the registration by the map node holding the name's hash
// and the delegate, and an event-tree node holding the hash.
inline bool verify_boat_drivers( )
{
    const unsigned* cdb = g_golden_vujb_words + g_golden_vujb_offsets[ 0 ];
    const unsigned char* cdb_raw = reinterpret_cast< const unsigned char* >( cdb + 2 );
    const value_t constant_db = c_reader( std::vector< std::uint8_t >( cdb_raw, cdb_raw + cdb[ 1 ] ) ).parse( );
    const human_driver_graph_t human = human_driver_construct( constant_db );
    const std::vector< image_object_t > mine[ 4 ] = { human.objects, ui_driver_construct( ), nuisance_driver_construct( ), ghost_driver_construct( ) };
    static const char* names[ 4 ] = { "Human", "UI", "Nuisance", "Ghost" };
    const unsigned* s = g_golden_boat_drivers_words;
    const unsigned kinds = *s++;
    int failed = 0, bytes = 0, objects = 0;
    auto fail = [ &failed ]( const char* kind, const char* what, int object, int at ) {
        if ( ++failed <= 8 )
            LOG_ERROR( "boat_drivers %s: %s at object %d, byte %d", kind, what, object, at );
    };
    for ( unsigned kind = 0; kind < kinds && kind < 4; ++kind )
    {
        const std::vector< image_object_t >& g = mine[ kind ];
        const unsigned count = *s++;
        const bool partial = kind == 0;
        if ( partial ? count < g.size( ) : count != g.size( ) )
            fail( names[ kind ], "object count", static_cast< int >( count ), static_cast< int >( g.size( ) ) );
        bool map_node = false, tree_node = false;
        for ( unsigned k = 0; k < count; ++k )
        {
            const unsigned size = *s++;
            const unsigned char* want = reinterpret_cast< const unsigned char* >( s );
            s += ( size + 3 ) / 4;
            const unsigned char* mask = reinterpret_cast< const unsigned char* >( s );
            s += ( size + 3 ) / 4;
            std::vector< image_ref_t > refs( *s++ );
            for ( auto& r : refs )
            {
                r = { static_cast< int >( s[ 0 ] ), s[ 1 ] == 1 ? AI_DRIVER_REF_BOAT : static_cast< int >( s[ 2 ] ), static_cast< int >( s[ 3 ] ) };
                s += 4;
            }
            if ( partial && k >= g.size( ) )
            {
                std::uint32_t w[ 6 ] = { };
                std::memcpy( w, want, size < 24 ? size : 24 );
                const std::uint32_t hash = human.methods[ 0 ].first;
                if ( size == 24 && w[ 4 ] == hash )
                    for ( const auto& r : refs )
                        map_node = map_node || ( r.slot == 20 && r.object == human.methods[ 0 ].second && r.offset == 0 );
                if ( size == 20 && w[ 2 ] == hash )
                    tree_node = true;
                continue;
            }
            ++objects;
            if ( size != g[ k ].image.size( ) )
            {
                fail( names[ kind ], "size", static_cast< int >( k ), static_cast< int >( g[ k ].image.size( ) ) );
                continue;
            }
            for ( unsigned at = 0; at < size; ++at )
            {
                if ( !mask[ at ] )
                    continue;
                ++bytes;
                if ( g[ k ].image[ at ] != want[ at ] )
                    fail( names[ kind ], "byte", static_cast< int >( k ), static_cast< int >( at ) );
            }
            if ( partial && k == 1 )
                continue;   // the map head: its links go to the nodes
            std::vector< image_ref_t > ours = g[ k ].refs;
            auto by_slot = []( const image_ref_t& x, const image_ref_t& y ) { return x.slot < y.slot; };
            std::sort( ours.begin( ), ours.end( ), by_slot );
            std::sort( refs.begin( ), refs.end( ), by_slot );
            if ( !( ours == refs ) )
                fail( names[ kind ], "pointers", static_cast< int >( k ), 0 );
        }
        if ( partial && !( map_node && tree_node ) )
            fail( names[ kind ], "the OnStartLightsOut registration", -1, 0 );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "postLoad's Human 0x40ED50 / UI 0x40B690 + 0x737CDC / Nuisance 0x41D280 + 0x40D880 / Ghost 0x40EB50 drivers match "
                     "retail: %d objects, %d bytes, every pointer, the human's method registration", objects, bytes );
        return true;
    }
    LOG_ERROR( "boat_drivers FAILED %d checks", failed );
    return false;
}

// VuAiNuisanceDriver's tick 0x40DA90 against retail on Hydroelectric's track (oracle case nuisance_tick): each
// tick's blocks, plan, target sector and VuRandom fed to nuisance_driver_tick with the scripted line list; the
// driver (but its pointer words: the boat, the plan's, the weak reference's - those by being cleared or not - and
// the target's sector, compared as an index; +0xE0 the aim's w lane, a dead stack word) and boat words, the plan,
// VuRandom and the hook stream compared.
inline bool verify_nuisance_tick( )
{
    const std::unique_ptr< entity_t > tree = track_document_tree( );
    track_manager_t tm;
    if ( !tree || !track_build_from_tree( *tree, tm ) )
    {
        LOG_ERROR( "nuisance_tick: the track did not build" );
        return false;
    }
    static const int sizes[ 9 ] = { 0x104, 0x520, 0xC8, 0x2D4, 0x40, 0x80, 0x60, 0xC8, 0x2D4 };
    struct run_t
    {
        int kind = 0, lines = 0;
        std::vector< unsigned > log;
        std::vector< unsigned > handles;
        std::size_t next = 0;
        char names[ 8 ][ 8 ] = { };
    };
    static run_t run;
    nuisance_hooks_t hooks;
    hooks.user = &run;
    hooks.ai.controls.misfire = []( void* ) { run.log.push_back( 1 ); };
    hooks.ai.controls.voice = []( void*, const char* ) { run.log.push_back( 2 ); };
    hooks.release_target = []( void* ) { run.log.push_back( 9 ); };
    hooks.line_count = []( void* ) {
        run.log.push_back( 3 );
        return run.kind == 5 ? run.lines : -1;
    };
    // the case's list: "Line<k>", element 2 not a string, element 4 an empty one
    hooks.line = []( void*, int k ) -> const char* { return k == 2 || k == 4 ? "" : run.names[ k ]; };
    hooks.line_event = []( void*, const char* name ) -> std::uint32_t {
        const std::uint32_t h = run.next < run.handles.size( ) ? run.handles[ run.next++ ] : 0u;
        run.log.insert( run.log.end( ), { 4u, static_cast< unsigned >( name[ 4 ] - '0' ), h } );
        return h;
    };
    hooks.start = []( void*, std::uint32_t e ) { run.log.insert( run.log.end( ), { 5u, e } ); };
    hooks.attributes = []( void*, std::uint32_t e, const float* pos, const float* vel ) {
        run.log.insert( run.log.end( ), { 6u, e } );
        for ( const float* v : { pos, vel } )
            for ( int k = 0; k < 3; ++k )
            {
                std::uint32_t w;
                std::memcpy( &w, v + k, 4 );
                run.log.push_back( w );
            }
    };
    hooks.stop = []( void*, std::uint32_t e ) { run.log.insert( run.log.end( ), { 8u, e } ); };
    for ( int k = 0; k < 8; ++k )
        std::snprintf( run.names[ k ], 8, "Line%d", k );
    const unsigned* g = g_golden_nuisance_tick_words;
    int compared = 0, failed = 0, bad = 0, ticks = 0;
    const unsigned ns = *g++;
    for ( unsigned s = 0; s < ns; ++s )
    {
        const unsigned nt = *g++;
        run.kind = static_cast< int >( *g++ );
        run.lines = static_cast< int >( *g++ );
        for ( unsigned t = 0; t < nt; ++t, ++ticks )
        {
            float dt;
            std::memcpy( &dt, g++, 4 );
            fleet_random_t rng;
            std::memcpy( &rng, g, 136 );
            g += 34;
            nuisance_pilot_t p;
            const unsigned np = *g++;
            for ( unsigned j = 0; j < np; ++j )
                p.plan.sectors.push_back( static_cast< std::int32_t >( *g++ ) );
            p.target_sector = static_cast< std::int32_t >( *g++ );
            std::vector< std::uint8_t > blocks[ 9 ];
            for ( int k = 0; k < 9; ++k )
            {
                blocks[ k ].assign( reinterpret_cast< const std::uint8_t* >( g ), reinterpret_cast< const std::uint8_t* >( g ) + sizes[ k ] );
                g += sizes[ k ] / 4;
            }
            p.driver = blocks[ 0 ].data( );
            p.target = human_i32( p.driver, 0xF0 ) != 0;
            const std::uint8_t* boats[ 1 ] = { blocks[ 1 ].data( ) };
            ai_tick_t tick;
            tick.boat = blocks[ 1 ].data( );
            tick.transform = blocks[ 2 ].data( );
            tick.body = blocks[ 3 ].data( );
            tick.engine = blocks[ 4 ].data( );
            tick.owner = blocks[ 1 ].data( );
            tick.animation = blocks[ 5 ].data( );
            tick.solver = blocks[ 6 ].data( );
            tick.boats = boats;
            tick.boat_count = 1;
            nuisance_target_t target;
            target.transform = blocks[ 7 ].data( );
            target.body = blocks[ 8 ].data( );
            const unsigned* after[ 2 ] = { g, g + sizes[ 0 ] / 4 };
            g += ( sizes[ 0 ] + sizes[ 1 ] ) / 4;
            std::vector< int > plan;
            const unsigned na = *g++;
            for ( unsigned j = 0; j < na; ++j )
                plan.push_back( static_cast< std::int32_t >( *g++ ) );
            const int target_sector = static_cast< std::int32_t >( *g++ );
            const unsigned* rng_after = g;
            g += 34;
            const unsigned nl = *g++;
            const std::vector< unsigned > expect( g, g + nl );
            g += nl;
            run.log.clear( );
            run.handles.clear( );
            run.next = 0;
            for ( std::size_t j = 0; j < expect.size( ); )
            {
                const unsigned c = expect[ j ];
                if ( c == 4 )
                    run.handles.push_back( expect[ j + 2 ] );
                j += c == 4 ? 3 : c == 5 || c == 8 ? 2 : c == 6 ? 8 : 1;
            }
            nuisance_driver_tick( tm, p, tick, target, rng, dt, hooks );
            const int failed_before = failed;
            for ( int k = 0; k < 2; ++k )
            {
                for ( int w = 0; w < sizes[ k ] / 4; ++w )
                {
                    const int at = 4 * w;
                    if ( k == 0 && ( at == 0x5C || ( at >= 0x84 && at <= 0x94 ) || at == 0xE0 || at == 0xF4 ||
                                     ( ( at == 0xE8 || at == 0xEC || at == 0xF0 ) && after[ 0 ][ w ] != 0 ) ) )
                        continue;
                    std::uint32_t mine;
                    std::memcpy( &mine, blocks[ k ].data( ) + at, 4 );
                    ++compared;
                    if ( mine != after[ k ][ w ] && ++failed <= 8 )
                        LOG_ERROR( "nuisance_tick %u/%u block %d +0x%X: 0x%08X, retail 0x%08X", s, t, k, at, mine, after[ k ][ w ] );
                }
            }
            compared += 4;
            if ( ( plan != p.plan.sectors || target_sector != p.target_sector || p.target != ( after[ 0 ][ 0xF0 / 4 ] != 0 ) ) && ++failed <= 8 )
                LOG_ERROR( "nuisance_tick %u/%u: plan / target sector %d (retail %d) / target differ", s, t, p.target_sector, target_sector );
            if ( std::memcmp( &rng, rng_after, 136 ) != 0 && ++failed <= 8 )
                LOG_ERROR( "nuisance_tick %u/%u: VuRandom differs", s, t );
            if ( expect != run.log && ++failed <= 8 )
                LOG_ERROR( "nuisance_tick %u/%u: the hook stream differs (%zu words vs retail %zu)", s, t, run.log.size( ), expect.size( ) );
            bad += failed != failed_before ? 1 : 0;
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuAiNuisanceDriver's tick 0x40DA90 (0x40E060, 0x40E280, the line pick) matches retail on Hydroelectric's track: "
                     "%d ticks, %d values, bit for bit", ticks, compared );
        return true;
    }
    LOG_ERROR( "nuisance_tick FAILED %d of %d values (%d of %d ticks)", failed, compared, bad, ticks );
    return false;
}

// The drivers' slots 23 / 24 / 25 / 26 against retail (oracle case driver_slots): the x87 answers bit for bit, the
// sector answers by what they point at.
inline bool verify_driver_slots( )
{
    const unsigned* g = g_golden_driver_slots_words;
    const unsigned nv = *g++;
    static const int sizes[ 5 ] = { 0x104, 0x520, 0xC8, 0x2D4, 0x40 };
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        std::vector< std::uint8_t > blocks[ 5 ];
        for ( int k = 0; k < 5; ++k )
        {
            blocks[ k ].assign( reinterpret_cast< const std::uint8_t* >( g ), reinterpret_cast< const std::uint8_t* >( g ) + sizes[ k ] );
            g += sizes[ k ] / 4;
        }
        const unsigned n = *g++, first = *g++;
        const std::uint8_t* boat = blocks[ 1 ].data( );
        auto bits = []( float x ) { std::uint32_t u; std::memcpy( &u, &x, 4 ); return u; };
        fleet_plan_t plan;
        for ( unsigned k = 0; k < n; ++k )
            plan.sectors.push_back( k == 0 && first ? 7 : k == 0 ? -1 : 8 );
        human_pilot_t hp;
        hp.sector = 5;
        ghost_pilot_t gp;
        gp.sector = 6;
        const float ai = ai_driver_throttle( blocks[ 0 ].data( ), boat, blocks[ 4 ].data( ), boat );
        const unsigned mine[ 7 ] = { bits( driver_steer_value( boat ) ), bits( driver_entity_throttle( boat, blocks[ 2 ].data( ), blocks[ 4 ].data( ), blocks[ 3 ].data( ) ) ),
                                     bits( ai ), bits( ai ), ai_driver_sector( plan ) >= 0 ? 1u : 0u, human_driver_sector( hp ) == 5 ? 1u : 0u,
                                     ghost_driver_sector( gp ) == 6 ? 1u : 0u };
        static const char* names[ 7 ] = { "slot 26", "slot 24 0x40EB00", "AI slot 24", "AI slot 25", "AI slot 23", "human slot 23", "ghost slot 23" };
        for ( int k = 0; k < 7; ++k, ++g )
            if ( mine[ k ] != *g && ++failed <= 8 )
                LOG_ERROR( "driver_slots vector %u %s: 0x%08X, retail 0x%08X", v, names[ k ], mine[ k ], *g );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "The drivers' slots 26 0x40EB20, 24 / 25 0x40EB00, AI 24 0x40BD40 / 25 0x40BDD0, 23 0x40BD20 / 0x40ED30 / 0x40EB40 "
                     "match retail: %u vectors x 7", nv );
        return true;
    }
    LOG_ERROR( "driver_slots FAILED %d of %u values", failed, nv * 7 );
    return false;
}

// VuBoatManager's add 0x4AAF60 / remove 0x4AB040 against retail (oracle case boat_manager): the driver's kind
// from its vtable (boat_driver_kind), every operation replayed, then +0x58 and the five lists - sizes, the
// capacities retail's growth keeps, the entries in order.
inline bool verify_boat_manager( )
{
    const unsigned* g = g_golden_boat_manager_words;
    const unsigned ns = *g++;
    int failed = 0, compared = 0, ops = 0;
    for ( unsigned s = 0; s < ns; ++s )
    {
        const unsigned nb = *g++;
        std::vector< boat_driver_kind_t > kinds( nb );
        std::vector< int > ids( nb );
        for ( unsigned i = 0; i < nb; ++i, ++g )
        {
            ids[ i ] = static_cast< int >( i );
            if ( !boat_driver_kind( *g, kinds[ i ] ) && ++failed <= 8 )
                LOG_ERROR( "boat_manager sequence %u: no kind for the vtable 0x%X", s, *g );
        }
        boat_manager_t m;
        const unsigned no = *g++;
        for ( unsigned o = 0; o < no; ++o, ++ops )
        {
            const unsigned op = *g++, b = *g++, has_pre = *g++;
            const std::int32_t pre = static_cast< std::int32_t >( *g++ );
            if ( has_pre )
                m.viewport = pre;
            if ( op == 0 )
                boat_manager_add( m, &ids[ b ], kinds[ b ] );
            else
                boat_manager_remove( m, &ids[ b ] );
            ++compared;
            if ( static_cast< unsigned >( m.viewport ) != *g && ++failed <= 8 )
                LOG_ERROR( "boat_manager sequence %u op %u: viewport %d, retail %d", s, o, m.viewport, static_cast< int >( *g ) );
            ++g;
            const boat_manager_list_t* lists[ 5 ] = { &m.all, &m.racers, &m.humans, &m.ais, &m.ghosts };
            for ( int k = 0; k < 5; ++k )
            {
                const unsigned size = *g++, cap = *g++;
                compared += 2;
                if ( lists[ k ]->boats.size( ) != size && ++failed <= 8 )
                    LOG_ERROR( "boat_manager sequence %u op %u list %d: size %zu, retail %u", s, o, k, lists[ k ]->boats.size( ), size );
                if ( static_cast< unsigned >( lists[ k ]->capacity ) != cap && ++failed <= 8 )
                    LOG_ERROR( "boat_manager sequence %u op %u list %d: capacity %d, retail %u", s, o, k, lists[ k ]->capacity, cap );
                for ( unsigned j = 0; j < size; ++j, ++g )
                {
                    ++compared;
                    const void* want = *g < nb ? &ids[ *g ] : nullptr;
                    if ( ( j >= lists[ k ]->boats.size( ) || lists[ k ]->boats[ j ] != want ) && ++failed <= 8 )
                        LOG_ERROR( "boat_manager sequence %u op %u list %d entry %u differs (retail boat %u)", s, o, k, j, *g );
                }
            }
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuBoatManager add 0x4AAF60 / remove 0x4AB040 (+0x415690, 0x4AB7D0) match retail: %d operations, %d values", ops, compared );
        return true;
    }
    LOG_ERROR( "boat_manager FAILED %d of %d values", failed, compared );
    return false;
}

// The "Viewports" tick 0x4AB0A0 against retail (oracle case viewport_tick): which camera reaches the viewport,
// viewport +0x278, every boat's +0x467, the hook calls in order. The hooks stand for retail's callees as the case
// recorded them (the listener = FMOD + 0x534830 + 0x534C00(0); the release branch = 0x5362A0 when the instigator's
// +0x28 is set, then 0).
inline bool verify_viewport_tick( )
{
    const unsigned* g = g_golden_viewport_tick_words;
    const unsigned nv = *g++;
    int failed = 0, compared = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned n = *g++, has_override = *g++, view = *g++, b2b9 = *g++, i28 = *g++;
        std::vector< std::vector< std::uint8_t > > boats( n, std::vector< std::uint8_t >( 0x520, 0xCD ) );
        for ( unsigned i = 0; i < n; ++i, g += 2 )
            std::memcpy( boats[ i ].data( ) + 0x3F8, g, 8 );
        static const std::uint8_t override_image[ 0x240 ] = { };
        boat_manager_t m;
        for ( unsigned i = 0; i < n; ++i )
            m.all.boats.push_back( boats[ i ].data( ) );
        m.viewport = static_cast< std::int32_t >( view );
        m.b2b9 = static_cast< std::uint8_t >( b2b9 );
        m.override_camera = has_override ? override_image : nullptr;
        int source = -1;
        std::uint32_t scale = 0xCDCDCDCDu;
        std::uint8_t inst28 = static_cast< std::uint8_t >( i28 );
        std::vector< std::pair< unsigned, unsigned > > log;
        std::vector< unsigned > local( n, 0xCD );
        auto bits = []( float x ) { std::uint32_t u; std::memcpy( &u, &x, 4 ); return u; };
        auto index = [ & ]( const void* b ) { for ( unsigned i = 0; i < n; ++i ) if ( boats[ i ].data( ) == b ) return static_cast< int >( i ); return -2; };
        boat_viewport_hooks_t hooks;
        hooks.viewport_camera = [ & ]( const std::uint8_t* c ) { source = c == override_image ? 100 : -2; };
        hooks.boat_camera = [ & ]( const void* b ) { source = index( b ); };
        hooks.lens_scale = [ & ]( float s ) { scale = bits( s ); };
        hooks.lens_water = [ & ]( float a ) { log.push_back( { 3u, bits( a ) } ); };
        hooks.local = [ & ]( const void* b, std::uint8_t x ) { const int i = index( b ); if ( i >= 0 ) local[ i ] = x; };
        hooks.listener = [ & ]( ) { log.push_back( { 5u, 1u } ); log.push_back( { 6u, 1u } ); log.push_back( { 7u, 1u } ); };
        hooks.trigger = [ & ]( bool registered ) {
            if ( registered )
                log.push_back( { 8u, 1u } );
            else
            {
                if ( inst28 )
                    log.push_back( { 9u, 1u } );
                inst28 = 0;
            }
        };
        boat_viewport_t out;
        const bool ok = boat_manager_viewport_tick( m, out, hooks );
        const unsigned mine_head[ 2 ] = { static_cast< unsigned >( source ), scale };
        for ( int k = 0; k < 2; ++k, ++g )
        {
            ++compared;
            if ( ( !ok || mine_head[ k ] != *g ) && ++failed <= 8 )
                LOG_ERROR( "viewport_tick vector %u %s: 0x%X, retail 0x%X", v, k ? "+0x278" : "source", mine_head[ k ], *g );
        }
        for ( unsigned i = 0; i < n; ++i, ++g )
            if ( ++compared, local[ i ] != *g && ++failed <= 8 )
                LOG_ERROR( "viewport_tick vector %u boat %u +0x467: %u, retail %u", v, i, local[ i ], *g );
        const unsigned position = *g++, inst_after = *g++, nl = *g++;
        compared += 2;
        if ( position != 1 && ++failed <= 8 )
            LOG_ERROR( "viewport_tick vector %u: retail did not copy the listener position", v );
        if ( inst28 != inst_after && ++failed <= 8 )
            LOG_ERROR( "viewport_tick vector %u: instigator +0x28 %u, retail %u", v, inst28, inst_after );
        ++compared;
        if ( log.size( ) != nl && ++failed <= 8 )
            LOG_ERROR( "viewport_tick vector %u: %zu calls, retail %u", v, log.size( ), nl );
        for ( unsigned j = 0; j < nl; ++j, g += 2 )
        {
            ++compared;
            if ( ( j >= log.size( ) || log[ j ].first != g[ 0 ] || log[ j ].second != g[ 1 ] ) && ++failed <= 8 )
                LOG_ERROR( "viewport_tick vector %u call %u: retail (%u, 0x%X)", v, j, g[ 0 ], g[ 1 ] );
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuBoatManager's Viewports tick 0x4AB0A0 matches retail: %u vectors, %d values", nv, compared );
        return true;
    }
    LOG_ERROR( "viewport_tick FAILED %d of %d values", failed, compared );
    return false;
}

// VuPowerUpBoostEntity slot 15 0x43DB40 against retail (oracle case boost_apply): the boat's +0x314 / +0x318, the
// return value and VuRandom's state after, bit for bit.
inline bool verify_boost_apply( )
{
    const unsigned* g = g_golden_boost_apply_words;
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        std::uint8_t pu[ 0x144 ] = { }, boat[ 0x520 ] = { };
        std::memcpy( pu + 0x13C, g, 8 );
        g += 2;
        std::memcpy( boat + 0x474, g++, 4 );
        fleet_random_t rng;
        rng.seed = static_cast< std::int32_t >( *g++ );
        rng.last = static_cast< std::int32_t >( *g++ );
        for ( int k = 0; k < 32; ++k )
            rng.table[ k ] = static_cast< std::int32_t >( *g++ );
        const unsigned ret = powerup_boost_apply( pu, boat, rng ) ? 1u : 0u;
        unsigned mine[ 37 ] = { ret };
        std::memcpy( mine + 1, boat + 0x314, 8 );
        mine[ 3 ] = static_cast< unsigned >( rng.seed );
        mine[ 4 ] = static_cast< unsigned >( rng.last );
        for ( int k = 0; k < 32; ++k )
            mine[ 5 + k ] = static_cast< unsigned >( rng.table[ k ] );
        for ( int k = 0; k < 37; ++k, ++g )
            if ( mine[ k ] != *g && ++failed <= 8 )
                LOG_ERROR( "boost_apply vector %u word %d: 0x%08X, retail 0x%08X", v, k, mine[ k ], *g );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuPowerUpBoostEntity slot 15 0x43DB40 (the boost a pickup gives) matches retail: %u vectors x 37 words", nv );
        return true;
    }
    LOG_ERROR( "boost_apply FAILED %d of %u values", failed, nv * 37 );
    return false;
}

// VuPowerUpEntity's Decision tick 0x43BDB0 against retail (oracle case powerup_tick): the time, +0x12C, the scale
// spring (+0x130 / +0x134), the bobbed position and every hook call in order, bit for bit. The hooks answer as the
// case's recorders did (the driver's vt+0x38 / +0x30 ran retail's own 0x404BD0 / 0x4052D0 there: boat_driver_kind).
inline bool verify_powerup_tick( )
{
    const unsigned* g = g_golden_powerup_tick_words;
    const unsigned ns = *g++;
    int failed = 0, compared = 0, ticks = 0;
    auto fl = []( unsigned u ) { float x; std::memcpy( &x, &u, 4 ); return x; };
    auto bits = []( float x ) { std::uint32_t u; std::memcpy( &u, &x, 4 ); return u; };
    for ( unsigned s = 0; s < ns; ++s )
    {
        const unsigned nt = *g++;
        for ( unsigned t = 0; t < nt; ++t, ++ticks )
        {
            const float dt = fl( *g++ );
            powerup_t p;
            std::memcpy( &p.time, g, 8 );
            g += 2;
            p.in_world = static_cast< std::uint8_t >( *g++ );
            p.handle_wave_height = static_cast< std::uint8_t >( *g++ );
            p.height_above_water = fl( *g++ );
            p.pickup = *g++;
            p.scale_velocity = fl( *g++ );
            p.scale = fl( *g++ );
            p.spring = fl( *g++ );
            p.damping = fl( *g++ );
            p.scale_initial = fl( *g++ );
            p.scale_initial_velocity = fl( *g++ );
            p.pfx = *g++;
            float position[ 4 ];
            std::memcpy( position, g, 16 );
            g += 4;
            const unsigned boat = *g++, kind = *g++, rtti = *g++, c0 = *g++, valid = *g++;
            const unsigned water = *g++, apply = *g++, bac0 = *g++, getevent = *g++;
            std::uint8_t boats[ 4 ] = { };
            std::vector< std::vector< unsigned > > log;
            auto index = [ & ]( void* b ) { return static_cast< unsigned >( static_cast< std::uint8_t* >( b ) - boats ); };
            powerup_tick_hooks_t hooks;
            hooks.water_height = [ & ]( const float* pos ) {
                log.push_back( { 1u, bits( pos[ 0 ] ), bits( pos[ 1 ] ), bits( pos[ 2 ] ), bits( pos[ 3 ] ) } );
                return fl( water );
            };
            hooks.moved = [ & ]( ) {
                log.push_back( { 2u, 1u } );
                log.push_back( { 3u, 1u } );
                if ( c0 )
                    log.push_back( { 4u, 1u } );
            };
            hooks.find = [ & ]( std::uint32_t id ) { log.push_back( { 5u, id } ); return static_cast< void* >( boats + boat ); };
            hooks.is_boat = [ & ]( void* b ) { log.push_back( { 6u, index( b ) } ); return rtti == 1 || rtti == 2; };
            hooks.apply = [ & ]( void* b ) { log.push_back( { 7u, index( b ) } ); return ( apply & 0xFF ) != 0; };
            hooks.apply_pfx = [ & ]( void* ) {
                log.push_back( { 8u, 1u } );
                if ( bac0 )
                    log.push_back( { 9u } );
            };
            hooks.driver_38 = [ & ]( void* ) { return true; };
            hooks.driver_30 = [ & ]( void* ) { boat_driver_kind_t k; return boat_driver_kind( kind, k ) && k.human; };
            hooks.sound = [ & ]( const float* pos ) {
                log.push_back( { 11u, 1u } );
                if ( getevent == 0 )
                {
                    log.push_back( { 12u, 1u, bits( pos[ 0 ] ), bits( pos[ 1 ] ), bits( pos[ 2 ] ) } );
                    log.push_back( { 13u, 1u } );
                }
            };
            hooks.voice = [ & ]( void* b ) { log.push_back( { 14u, index( b ), 1u } ); };
            hooks.pfx_scale = [ & ]( std::uint32_t, float sc ) {
                if ( valid )
                    log.push_back( { 15u, bits( sc ) } );
            };
            const bool ok = powerup_decision_tick( p, position, dt, hooks );
            unsigned mine[ 9 ];
            std::memcpy( mine, &p.time, 8 );
            mine[ 2 ] = p.pickup;
            mine[ 3 ] = bits( p.scale_velocity );
            mine[ 4 ] = bits( p.scale );
            std::memcpy( mine + 5, position, 16 );
            static const char* const names[ 9 ] = { "time lo", "time hi", "+0x12C", "+0x130", "+0x134", "x", "y", "z", "w" };
            for ( int k = 0; k < 9; ++k, ++g )
                if ( ++compared, ( !ok || mine[ k ] != *g ) && ++failed <= 8 )
                    LOG_ERROR( "powerup_tick sequence %u tick %u %s: 0x%08X, retail 0x%08X", s, t, names[ k ], mine[ k ], *g );
            const unsigned nl = *g++;
            ++compared;
            if ( log.size( ) != nl && ++failed <= 8 )
                LOG_ERROR( "powerup_tick sequence %u tick %u: %zu calls, retail %u", s, t, log.size( ), nl );
            for ( unsigned j = 0; j < nl; ++j )
            {
                const unsigned len = *g++;
                ++compared;
                bool same = j < log.size( ) && log[ j ].size( ) == len;
                for ( unsigned k = 0; k < len; ++k )
                    same = same && log[ j ][ k ] == g[ k ];
                if ( !same && ++failed <= 8 )
                    LOG_ERROR( "powerup_tick sequence %u tick %u call %u: retail code %u", s, t, j, g[ 0 ] );
                g += len;
            }
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuPowerUpEntity's Decision tick 0x43BDB0 matches retail: %u sequences, %d ticks, %d values", ns, ticks, compared );
        return true;
    }
    LOG_ERROR( "powerup_tick FAILED %d of %d values", failed, compared );
    return false;
}

// The two mesh-asset loads against retail's (oracle case mesh_assets): every collision mesh BoatDB names and every
// fluids mesh in Base.apf, read from the pack here and loaded by collision_mesh_load / fluids_mesh_load; compared:
// the return, the stream's end, the surface names asked, every array's size / capacity (/ owns) and FNV-1a of its
// bytes (the materials' after the surface ids went in), the aabbs, the flag, the bvh header and words. One failure
// per asset.
inline bool verify_mesh_assets( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "verify_mesh_assets: Base.apf not found" );
        return false;
    }
    const unsigned* g = g_golden_mesh_assets_words;
    auto text = [ & ]( ) {
        const unsigned n = *g++;
        std::string s( reinterpret_cast< const char* >( g ), n );
        g += ( n + 3 ) / 4;
        return s;
    };
    auto fnv = []( const std::vector< std::uint8_t >& b, std::int32_t n ) {
        if ( n == 0 )
            return 0u;
        std::uint32_t h = 0x811C9DC5u;
        for ( std::uint8_t c : b )
            h = ( h ^ c ) * 0x01000193u;
        return h;
    };
    int failed = 0, collisions = 0, fluids = 0;
    std::vector< std::uint32_t > mine, want;
    auto vu = [ & ]( const mesh_vu_array_t& a ) {
        mine.push_back( static_cast< std::uint32_t >( a.size ) );
        mine.push_back( static_cast< std::uint32_t >( a.capacity ) );
        mine.push_back( fnv( a.bytes, a.size ) );
    };
    auto bt = [ & ]( const mesh_bt_array_t& a ) {
        mine.push_back( static_cast< std::uint32_t >( a.size ) );
        mine.push_back( static_cast< std::uint32_t >( a.capacity ) );
        mine.push_back( a.owns );
        mine.push_back( fnv( a.bytes, a.size ) );
    };
    auto words = [ & ]( const void* p, int n ) {
        const std::uint32_t* w = static_cast< const std::uint32_t* >( p );
        mine.insert( mine.end( ), w, w + n );
    };
    for ( int kind = 0; kind < 2; ++kind )
    {
        const unsigned count = *g++;
        for ( unsigned k = 0; k < count; ++k )
        {
            const std::string name = text( );
            const unsigned size = *g++, ret = *g++, end = *g++;
            std::vector< std::string > want_names;
            if ( kind == 0 )
            {
                want_names.resize( *g++ );
                for ( auto& n : want_names )
                    n = text( );
            }
            const unsigned nwords = kind == 0 ? 12 + 8 + 1 + 14 + 1 + 20 + 2 : 9 + 8 + 2;
            want.assign( g, g + nwords );
            g += nwords;
            mine.clear( );
            std::vector< std::uint8_t > blob;
            const std::string key = std::string( kind == 0 ? "VuCollisionMeshAsset/" : "VuFluidsMeshAsset/" ) + name;
            const bool read = archive.read( key.c_str( ), blob );
            mesh_stream_t s{ blob.data( ), blob.size( ), 0 };
            std::vector< std::string > names;
            bool ok = false;
            if ( kind == 0 )
            {
                collision_mesh_asset_t a;
                ok = read && collision_mesh_load( s, a, [ & ]( const std::string& n ) -> std::uint8_t {
                    names.push_back( n );
                    return n == "Metal" ? 3 : n == "Wood" ? 5 : n == "Duck" ? 9 : 1;
                } );
                vu( a.vertices ), vu( a.indices ), vu( a.triangles ), vu( a.materials );
                words( a.aabb, 8 );
                mine.push_back( a.flag );
                words( a.bvh.header, 14 );
                mine.push_back( a.bvh.use_quantization );
                bt( a.bvh.leaf ), bt( a.bvh.contiguous ), bt( a.bvh.quantized_leaf ), bt( a.bvh.quantized ), bt( a.bvh.subtrees );
                mine.push_back( static_cast< std::uint32_t >( a.bvh.traversal ) );
                mine.push_back( static_cast< std::uint32_t >( a.bvh.subtree_count ) );
                ++collisions;
            }
            else
            {
                fluids_mesh_asset_t a;
                ok = read && fluids_mesh_load( s, a );
                vu( a.vertices ), vu( a.edges ), vu( a.records );
                words( a.aabb, 8 );
                mine.push_back( a.w9c );
                mine.push_back( a.wa0 );
                ++fluids;
            }
            const bool same = ok && ret == 1 && blob.size( ) == size && s.at == end && names == want_names && mine == want;
            if ( !same && ++failed <= 8 )
            {
                std::size_t at = 0;
                while ( at < mine.size( ) && at < want.size( ) && mine[ at ] == want[ at ] )
                    ++at;
                LOG_ERROR( "mesh_assets %s: load %d, end %zu (retail %u), %zu names (retail %zu), first differing word %zu",
                           key.c_str( ), ok ? 1 : 0, s.at, end, names.size( ), want_names.size( ), at );
            }
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuCollisionMeshAsset 0x51AF20 / VuFluidsMeshAsset 0x523770 loads match retail on Base.apf: %d collision, %d fluids meshes",
                     collisions, fluids );
        return true;
    }
    LOG_ERROR( "mesh_assets FAILED %d of %d assets", failed, collisions + fluids );
    return false;
}

// VuRandom's ctor 0x50FC70 / seed 0x50FCA0 against retail (oracle case random_seed): seed, last, the table, +0x88
// = 0 and +0x8C untouched, and whether the clock (VuMetroSys slot 12) was asked.
inline bool verify_random_seed( )
{
    const unsigned* g = g_golden_random_seed_words;
    const unsigned nv = *g++;
    int failed = 0;
    static std::uint64_t clock_value = 0;
    static int asked = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const std::int32_t seed = static_cast< std::int32_t >( *g++ );
        clock_value = static_cast< std::uint64_t >( g[ 0 ] ) | ( static_cast< std::uint64_t >( g[ 1 ] ) << 32 );
        g += 2;
        const unsigned want_asked = *g++;
        asked = 0;
        fleet_random_t r;
        r.seed = static_cast< std::int32_t >( 0xCDCDCDCDu );
        r.last = r.seed;
        for ( auto& t : r.table )
            t = r.seed;
        fleet_random_seed( r, seed, [ ]( ) -> std::int64_t { ++asked; return static_cast< std::int64_t >( clock_value ); } );
        unsigned mine[ 36 ];
        mine[ 0 ] = static_cast< unsigned >( r.seed );
        mine[ 1 ] = static_cast< unsigned >( r.last );
        for ( int k = 0; k < 32; ++k )
            mine[ 2 + k ] = static_cast< unsigned >( r.table[ k ] );
        mine[ 34 ] = 0;             // the ctor's +0x88
        mine[ 35 ] = 0xCDCDCDCDu;   // +0x8C: neither writes it
        if ( static_cast< unsigned >( asked ) != want_asked && ++failed <= 8 )
            LOG_ERROR( "random_seed vector %u: the clock asked %d times, retail %u", v, asked, want_asked );
        for ( int k = 0; k < 36; ++k, ++g )
            if ( mine[ k ] != *g && ++failed <= 8 )
                LOG_ERROR( "random_seed vector %u word %d: 0x%08X, retail 0x%08X", v, k, mine[ k ], *g );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuRandom's seed 0x50FC70 / 0x50FCA0 (the engine init's reseed of 0x736810) matches retail: %u vectors x 37 values", nv );
        return true;
    }
    LOG_ERROR( "random_seed FAILED %d of %u values", failed, nv * 37 );
    return false;
}

// VuPowerUpEntity's contact callback 0x43C8C0 with its map 0x43C980 against retail (oracle case powerup_contact):
// +0x12C and the whole per-boat map after every contact; the seeding is powerup_game_initialize's (no disabled
// games, no hooks).
inline bool verify_powerup_contact( )
{
    const unsigned* g = g_golden_powerup_contact_words;
    const unsigned ns = *g++;
    int failed = 0, contacts = 0;
    for ( unsigned s = 0; s < ns; ++s )
    {
        powerup_t p;
        std::memcpy( &p.recover_time, g++, 4 );
        std::uint32_t ids[ 6 ];
        std::memcpy( ids, g, 24 );
        g += 6;
        std::vector< std::uint32_t > seeded( *g++ );
        for ( auto& i : seeded )
            i = *g++;
        p.initially_enabled = 0;
        powerup_game_initialize( p, "", { }, seeded, { } );
        const unsigned nc = *g++;
        for ( unsigned c = 0; c < nc; ++c, ++contacts )
        {
            std::memcpy( &p.time, g, 8 );
            g += 2;
            const unsigned owner = *g++, rtti = *g++, before = *g++, after = *g++;
            p.pickup = before;
            if ( owner != 6 )
                powerup_contact( p, rtti == 1 || rtti == 2, ids[ owner ] );
            const unsigned nm = *g++;
            bool same = p.pickup == after && p.last_pickup.size( ) == nm;
            auto it = p.last_pickup.begin( );
            for ( unsigned m = 0; m < nm; ++m, g += 3 )
            {
                if ( !same || it == p.last_pickup.end( ) )
                {
                    same = false;
                    continue;
                }
                std::uint32_t v[ 2 ];
                std::memcpy( v, &it->second, 8 );
                same = it->first == g[ 0 ] && v[ 0 ] == g[ 1 ] && v[ 1 ] == g[ 2 ];
                ++it;
            }
            if ( !same && ++failed <= 8 )
                LOG_ERROR( "powerup_contact sequence %u contact %u: +0x12C 0x%X (retail 0x%X), %zu map entries (retail %u)", s, c,
                           p.pickup, after, p.last_pickup.size( ), nm );
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuPowerUpEntity's contact callback 0x43C8C0 + its per-boat map 0x43C980 match retail: %u sequences, %d contacts", ns, contacts );
        return true;
    }
    LOG_ERROR( "powerup_contact FAILED %d of %d contacts", failed, contacts );
    return false;
}

// VuPowerUpEntity slots 13 / 12 / 7 against retail (oracle case powerup_world): +0x128, +0x110, the body, +0x130 /
// +0x134 and the call order. The pfx hooks stand for the pfx manager's internals as the case recorded them (which
// effect calls a live handle made); what is compared of them is when they run and the scale the power-up hands over.
inline bool verify_powerup_world( )
{
    const unsigned* g = g_golden_powerup_world_words;
    const unsigned ns = *g++;
    int failed = 0, calls = 0;
    auto bits = []( float x ) { std::uint32_t u; std::memcpy( &u, &x, 4 ); return u; };
    for ( unsigned s = 0; s < ns; ++s )
    {
        const unsigned nc = *g++;
        for ( unsigned c = 0; c < nc; ++c, ++calls )
        {
            const unsigned slot = *g++;
            powerup_t p;
            p.in_world = static_cast< std::uint8_t >( *g++ );
            p.active = static_cast< std::uint8_t >( *g++ );
            p.pfx = *g++;
            const bool has_body = *g++ != 0;
            std::memcpy( &p.scale_velocity, g++, 4 );
            std::memcpy( &p.scale, g++, 4 );
            p.reflect = static_cast< std::uint8_t >( *g++ );
            const unsigned want[ 6 ] = { g[ 0 ], g[ 1 ], g[ 2 ], g[ 3 ], g[ 4 ], g[ 5 ] };
            g += 6;
            std::vector< std::vector< unsigned > > glog( *g++ );
            for ( auto& e : glog )
            {
                e.assign( g + 1, g + 1 + *g );
                g += 1 + e.size( );
            }
            auto golden_of = [ & ]( std::initializer_list< unsigned > codes ) {
                std::vector< std::vector< unsigned > > out;
                for ( const auto& e : glog )
                    for ( unsigned k : codes )
                        if ( e[ 0 ] == k )
                            out.push_back( e );
                return out;
            };
            std::vector< std::vector< unsigned > > log;
            unsigned eff64 = 0xCD;
            bool body = has_body;
            powerup_world_hooks_t hooks;
            hooks.create_pfx = [ & ]( const std::string&, std::uint8_t reflect, float scale ) {
                log.push_back( { 1u, 1u } );
                for ( auto& e : golden_of( { 3u, 4u, 5u } ) )
                {
                    if ( e[ 0 ] == 4u )
                        e[ 1 ] = bits( scale );
                    else if ( e[ 0 ] == 5u )
                        eff64 = reflect;
                    log.push_back( e );
                }
                return want[ 1 ];
            };
            hooks.release_pfx = [ & ]( std::uint32_t ) { for ( auto& e : golden_of( { 6u, 2u } ) ) log.push_back( e ); };
            // The dynamics world is handed +0x114 whatever it holds (0 without a body).
            hooks.add_body = [ & ]( ) { log.push_back( { 7u, has_body ? 1u : 0u } ); };
            hooks.remove_body = [ & ]( ) { log.push_back( { 8u, has_body ? 1u : 0u } ); };
            hooks.destroy_body = [ & ]( ) {
                for ( auto& e : golden_of( { 10u } ) )
                    log.push_back( e );
                log.push_back( { 11u, 1u } );
                body = false;
            };
            hooks.unregister_tick = [ & ]( ) { log.push_back( { 9u, 1u } ); };
            if ( slot == 13 )
                powerup_add_to_world( p, hooks );
            else if ( slot == 12 )
                powerup_remove_from_world( p, hooks );
            else
                powerup_game_release( p, has_body, hooks );
            const unsigned mine[ 6 ] = { p.in_world, p.pfx, body ? 1u : 0u, bits( p.scale_velocity ), bits( p.scale ), eff64 };
            bool same = log == glog;
            for ( int k = 0; k < 6; ++k )
                same = same && mine[ k ] == want[ k ];
            if ( !same && ++failed <= 8 )
                LOG_ERROR( "powerup_world sequence %u call %u (slot %u): +0x128 %u (retail %u), +0x110 0x%X (0x%X), %zu calls (%zu)", s, c, slot,
                           mine[ 0 ], want[ 0 ], mine[ 1 ], want[ 1 ], log.size( ), glog.size( ) );
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuPowerUpEntity slots 13 0x43BD40 / 12 0x43BD00 / 7 0x43BCA0 match retail: %u sequences, %d calls", ns, calls );
        return true;
    }
    LOG_ERROR( "powerup_world FAILED %d of %d calls", failed, calls );
    return false;
}

// VuPowerUpTriggerEntity slot 15 0x43E240 against retail (oracle case trigger_apply): the return and whether the
// OnTrigger plug fired, per driver class (vt+0x30 = boat_driver_kind's human; vt+0x38 0x404BD0 = true).
inline bool verify_trigger_apply( )
{
    const unsigned* g = g_golden_trigger_apply_words;
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned kind = *g++, ret = *g++, nl = *g++;
        boat_driver_kind_t k;
        const bool human = boat_driver_kind( kind, k ) && k.human;
        unsigned fired = 0;
        const unsigned mine = powerup_trigger_apply( human, true, [ & ]( ) { ++fired; } ) ? 1u : 0u;
        // Retail's log for a firing: 1 VuParams(boat), 2 findPlug "OnTrigger", 3 the plug's vt+8 with them.
        bool same = mine == ret && nl == ( fired ? 3u : 0u );
        for ( unsigned j = 0; j < nl; ++j, g += 2 )
            same = same && g[ 0 ] == j + 1 && g[ 1 ] == 1;
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "trigger_apply vector %u (vtable 0x%X): %u, retail %u; %u calls", v, kind, mine, ret, nl );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuPowerUpTriggerEntity slot 15 0x43E240 matches retail: %u vectors (every driver class)", nv );
        return true;
    }
    LOG_ERROR( "trigger_apply FAILED %d of %u vectors", failed, nv );
    return false;
}

// The PowerUpDB reads against retail (oracle case powerup_db), on the real VuDBAsset/PowerUpDB and three synthetic
// documents parsed here by c_reader: 0x43C1F0 (powerup_load_db with its tail), 0x43DA90 (powerup_boost_load_energy),
// 0x43D960 (powerup_boost_enable) and 0x43B9D0 (powerup_disabled_in_games + powerup_game_initialize). Per run: the
// three strings, the seven floats' bits, +0x128 / +0x129 / +0x140, the body, +0x130 / +0x134, the call log; the
// layout bounds' lanes 0-2 / 4-6 (3 and 7 are stale stack in retail); the map after gameInitialize.
inline bool verify_powerup_db( )
{
    const unsigned* g = g_golden_powerup_db_words;
    auto text = [ &g ]( ) {
        const unsigned n = *g++;
        std::string s( reinterpret_cast< const char* >( g ), n );
        g += ( n + 3 ) / 4;
        return s;
    };
    auto bits = []( float x ) { std::uint32_t u; std::memcpy( &u, &x, 4 ); return u; };
    auto real = []( unsigned u ) { float x; std::memcpy( &x, &u, 4 ); return x; };
    std::vector< value_t > docs( *g++ );
    for ( value_t& d : docs )
    {
        const unsigned n = *g++;
        const std::uint8_t* raw = reinterpret_cast< const std::uint8_t* >( g );
        d = c_reader( std::vector< std::uint8_t >( raw, raw + n ) ).parse( );
        g += ( n + 3 ) / 4;
    }
    const unsigned nv = *g++;
    static const value_t null{};
    int failed = 0, runs = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned doc = *g++;
        const std::string type = text( );
        const bool asset = *g++ != 0;
        const unsigned flags = *g++, enabled = *g++;
        unsigned floats[ 7 ];
        for ( unsigned& f : floats )
            f = *g++;
        const bool has_body = g[ 0 ] != 0, has_shape = g[ 1 ] != 0;
        g += 2;
        const unsigned b128 = *g++, b129 = *g++, b138 = *g++, b140 = *g++;
        std::string pre[ 3 ];
        for ( std::string& s : pre )
            s = text( );
        const std::int32_t humans = static_cast< std::int32_t >( *g++ );
        std::vector< std::uint32_t > ids( *g++ );
        for ( std::uint32_t& id : ids )
            id = respawn_fnv( text( ).c_str( ) );
        const std::string mode = text( );
        const value_t& root = doc < docs.size( ) ? docs[ doc ] : null;
        const value_t& entry = asset ? powerup_db_entry( root, type ) : null;
        const value_t& mp = asset ? powerup_db_entry( root, "MPBoost" ) : null;
        for ( int run = 0; run < 4; ++run, ++runs )
        {
            std::string want_s[ 3 ];
            for ( std::string& s : want_s )
                s = text( );
            unsigned want[ 13 ];
            for ( unsigned& w : want )
                w = *g++;
            std::vector< std::vector< unsigned > > glog( *g++ );
            for ( auto& e : glog )
            {
                e.assign( g + 1, g + 1 + *g );
                g += 1 + e.size( );
            }
            unsigned want_bounds[ 8 ] = { };
            std::vector< unsigned > want_map;
            if ( run == 0 )
                for ( unsigned& b : want_bounds )
                    b = *g++;
            if ( run == 3 )
            {
                want_map.assign( g + 1, g + 1 + 3 * *g );
                g += 1 + want_map.size( );
            }
            powerup_t p;
            p.static_pfx = pre[ 0 ];
            p.apply_pfx = pre[ 1 ];
            p.audio_event = pre[ 2 ];
            float* fields[ 6 ] = { &p.radius, &p.recover_time, &p.spring, &p.damping, &p.scale_initial, &p.scale_initial_velocity };
            for ( int k = 0; k < 6; ++k )
                *fields[ k ] = real( floats[ k ] );
            float energy = real( floats[ 6 ] );
            p.initially_enabled = static_cast< std::uint8_t >( enabled );
            p.in_world = static_cast< std::uint8_t >( b128 );
            p.active = static_cast< std::uint8_t >( b129 );
            p.scale = p.scale_velocity = real( 0xCDCDCDCDu );
            std::uint8_t multiplayer = static_cast< std::uint8_t >( b140 );
            bool body = has_body;
            float bounds[ 8 ];
            for ( float& b : bounds )
                b = real( 0xCDCDCDCDu );
            std::vector< std::vector< unsigned > > log;
            if ( run == 0 )
            {
                powerup_db_hooks_t h;
                h.remove_from_world = [ & ]( ) { log.push_back( { 4u } ); };
                h.destroy_body = [ & ]( ) {
                    if ( has_shape )
                        log.push_back( { 6u, 1u } );
                    log.push_back( { 7u, 1u } );
                    body = false;
                };
                h.create_body = [ & ]( ) { log.push_back( { 1u } ); };
                h.add_to_world = [ & ]( ) { log.push_back( { 3u } ); };
                h.load_extra = [ & ]( ) { log.push_back( { 5u } ); };
                powerup_load_db( p, entry, bounds, flags, has_body, h );
            }
            else if ( run == 1 )
                powerup_boost_load_energy( energy, entry );
            else if ( run == 2 )
            {
                powerup_world_hooks_t h;
                h.create_pfx = [ & ]( const std::string& name, std::uint8_t, float ) {
                    log.push_back( { 8u, respawn_fnv( name.c_str( ) ) } );
                    return 0u;
                };
                h.add_body = [ & ]( ) { log.push_back( { 9u, 1u } ); };
                powerup_boost_enable( p, static_cast< std::uint8_t >( b138 ), multiplayer, humans, mp, h );
            }
            else
            {
                powerup_init_hooks_t h;
                h.create_body = [ & ]( ) { log.push_back( { 1u } ); };
                h.register_tick = [ & ]( ) { log.push_back( { 2u, 1u } ); };
                h.enable = [ & ]( ) { log.push_back( { 3u } ); };
                powerup_game_initialize( p, mode, powerup_disabled_in_games( entry ), ids, h );
            }
            const unsigned mine[ 13 ] = { bits( p.radius ), bits( p.recover_time ), bits( p.spring ), bits( p.damping ), bits( p.scale_initial ),
                bits( p.scale_initial_velocity ), bits( energy ), p.in_world, p.active, multiplayer, body ? 1u : 0u, bits( p.scale_velocity ),
                bits( p.scale ) };
            bool same = log == glog && p.static_pfx == want_s[ 0 ] && p.apply_pfx == want_s[ 1 ] && p.audio_event == want_s[ 2 ];
            for ( int k = 0; k < 13; ++k )
                same = same && mine[ k ] == want[ k ];
            if ( run == 0 )
                for ( int k : { 0, 1, 2, 4, 5, 6 } )
                    same = same && bits( bounds[ k ] ) == want_bounds[ k ];
            if ( run == 3 )
            {
                std::vector< unsigned > map;
                for ( const auto& e : p.last_pickup )
                {
                    std::uint32_t w[ 2 ];
                    std::memcpy( w, &e.second, 8 );
                    map.insert( map.end( ), { e.first, w[ 0 ], w[ 1 ] } );
                }
                same = same && map == want_map;
            }
            if ( !same && ++failed <= 8 )
                LOG_ERROR( "powerup_db vector %u (document %u, \"%s\") run %d: radius 0x%X (retail 0x%X), +0x129 %u (%u), %zu calls (%zu)", v,
                           doc, type.c_str( ), run, mine[ 0 ], want[ 0 ], mine[ 8 ], want[ 8 ], log.size( ), glog.size( ) );
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "The PowerUpDB reads 0x43C1F0 / 0x43DA90 / 0x43D960 / 0x43B9D0 match retail on the real PowerUpDB and 3 synthetic "
                     "documents: %u vectors, %d runs", nv, runs );
        return true;
    }
    LOG_ERROR( "powerup_db FAILED %d of %d runs", failed, runs );
    return false;
}

// The surface-type table against retail (oracle case surface_table): surface_table_build on the documents parsed
// here (the real SurfaceDB, synthetic ones, an empty / non-object VuDBAsset, none) - the map in order (name, id), the
// types (friction bits, colour) - then surface_count, surface_name for n = -1 .. count + 1 and surface_id on every name
// and some misses; the factory's create / release only when there is an asset.
inline bool verify_surface_table( )
{
    const unsigned* g = g_golden_surface_table_words;
    auto text = [ &g ]( ) {
        const unsigned n = *g++;
        std::string s( reinterpret_cast< const char* >( g ), n );
        g += ( n + 3 ) / 4;
        return s;
    };
    std::vector< value_t > docs( *g++ );
    for ( value_t& d : docs )
    {
        const unsigned n = *g++;
        const std::uint8_t* raw = reinterpret_cast< const std::uint8_t* >( g );
        d = c_reader( std::vector< std::uint8_t >( raw, raw + n ) ).parse( );
        g += ( n + 3 ) / 4;
    }
    const unsigned nv = *g++;
    int failed = 0, compared = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const std::int32_t doc = static_cast< std::int32_t >( *g++ );
        const unsigned vtable = *g++;
        surface_table_t t;
        const value_t* root = doc >= 0 && static_cast< std::size_t >( doc ) < docs.size( ) ? &docs[ doc ] : nullptr;
        bool same = surface_table_build( t, root ) && vtable == 0x74FA94u;
        const unsigned nm = *g++;
        same = same && nm == t.ids.size( );
        for ( unsigned k = 0; k < nm; ++k, ++compared )
        {
            const std::string name = text( );
            const unsigned id = *g++;
            same = same && k < t.ids.size( ) && t.ids[ k ].first == name && t.ids[ k ].second == id;
        }
        const unsigned nt = *g++;
        same = same && nt == t.types.size( );
        for ( unsigned k = 0; k < nt; ++k, g += 2, ++compared )
        {
            std::uint32_t f = 0;
            if ( k < t.types.size( ) )
                std::memcpy( &f, &t.types[ k ].friction, 4 );
            same = same && k < t.types.size( ) && f == g[ 0 ] && t.types[ k ].color == g[ 1 ];
        }
        const unsigned want_count = *g++;
        same = same && static_cast< unsigned >( surface_count( t ) ) == want_count;
        const unsigned nn = *g++;
        for ( unsigned k = 0; k < nn; ++k, ++compared )
        {
            const std::string* mine = surface_name( t, static_cast< std::int32_t >( k ) - 1 );
            if ( *g++ )
            {
                const std::string want = text( );
                same = same && mine && *mine == want;
            }
            else
                same = same && !mine;
        }
        const unsigned np = *g++;
        for ( unsigned k = 0; k < np; ++k, ++compared )
        {
            const std::string name = text( );
            const unsigned want = *g++;
            same = same && surface_id( t, name.c_str( ) ) == want;
        }
        const unsigned nl = *g++;
        same = same && nl == ( root ? 2u : 0u );
        for ( unsigned k = 0; k < nl; ++k, g += 2 )
            same = same && g[ 0 ] == k + 1 && g[ 1 ] == 1;
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "surface_table vector %u (document %d): %zu names (retail %u), %zu types (%u)", v, doc, t.ids.size( ), nm, t.types.size( ), nt );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "The surface-type table (VuDynamicsContactManagerImpl 0x574EC0 + slots 0 / 1 / 2) matches retail on the real SurfaceDB "
                     "and synthetic documents: %u vectors, %d values", nv, compared );
        return true;
    }
    LOG_ERROR( "surface_table FAILED %d of %u vectors", failed, nv );
    return false;
}

// VuPowerUpHiddenPackageEntity slots 6 / 13 / 15 against retail (oracle case hidden_package): the name from the
// long name, the enable gate, the apply (driver answers RUN in retail; boat_driver_kind's here) - +0x138 / +0x128,
// the return and the hooks' order and arguments, the OnPackageFound params word for word.
inline bool verify_hidden_package( )
{
    const unsigned* g = g_golden_hidden_package_words;
    auto text = [ &g ]( ) {
        const unsigned n = *g++;
        std::string s( reinterpret_cast< const char* >( g ), n );
        g += ( n + 3 ) / 4;
        return s;
    };
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned slot = *g++, kind = *g++;
        const std::string name = text( ), long_name = text( ), level = text( );
        const unsigned found = *g++, first = *g++, a = *g++, b = *g++, b128 = *g++, b129 = *g++, ret = *g++;
        const std::string after_name = text( );
        const unsigned after128 = *g++;
        std::vector< std::vector< unsigned > > glog( *g++ );
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        powerup_t p;
        p.in_world = static_cast< std::uint8_t >( b128 );
        p.active = static_cast< std::uint8_t >( b129 );
        std::string package = name;
        std::vector< std::vector< unsigned > > log;
        unsigned mine_ret = 0;
        powerup_package_hooks_t h;
        h.is_found = [ & ]( const std::string& n ) { log.push_back( { 3u, n == name ? 1u : 0u } ); return found != 0; };
        h.set_found = [ & ]( const std::string& n ) { log.push_back( { 2u, n == name ? 1u : 0u } ); };
        h.level = [ & ]( ) { return level; };
        h.first = [ & ]( const std::string& l, std::int32_t& x ) {
            log.push_back( { 4u, l == level ? 1u : 0u, static_cast< unsigned >( x ) } );
            x = static_cast< std::int32_t >( a );
            return first != 0;
        };
        h.second = [ & ]( const std::string& l, std::int32_t& x ) {
            log.push_back( { 5u, l == level ? 1u : 0u } );
            x = static_cast< std::int32_t >( b );
        };
        h.broadcast = [ & ]( const std::vector< std::uint32_t >& params ) {
            std::vector< unsigned > e = { 6u, 1u, static_cast< unsigned >( 4 * params.size( ) ) };
            e.insert( e.end( ), params.begin( ), params.end( ) );
            log.push_back( e );
        };
        h.remove_from_world = [ & ]( ) { log.push_back( { 8u } ); };
        if ( slot == 6 )
        {
            package = powerup_package_name( long_name );
            log.push_back( { 1u } );
        }
        else if ( slot == 13 )
        {
            powerup_world_hooks_t w;
            w.create_pfx = [ & ]( const std::string&, std::uint8_t, float ) { log.push_back( { 7u } ); return 0u; };
            w.add_body = [ & ]( ) { log.push_back( { 9u } ); };
            powerup_package_enable( p, package, h, w );
        }
        else
        {
            boat_driver_kind_t k;
            const bool human = boat_driver_kind( kind, k ) && k.human;
            mine_ret = powerup_package_apply( true, human, package, h ) ? 1u : 0u;
        }
        const bool same = log == glog && package == after_name && p.in_world == after128 && mine_ret == ret;
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "hidden_package vector %u (slot %u): \"%s\" (retail \"%s\"), %zu calls (%zu), return %u (%u)", v, slot, package.c_str( ),
                       after_name.c_str( ), log.size( ), glog.size( ), mine_ret, ret );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuPowerUpHiddenPackageEntity slots 6 0x43DD20 / 13 0x43DE20 / 15 0x43DE80 match retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "hidden_package FAILED %d of %u vectors", failed, nv );
    return false;
}

// VuBoatManager's draw 0x4AB300 / slot 5 0x4AAD70 / slot 3 0x4AAF10 against retail (oracle case boat_manager_game):
// the sort word, the driver draw's boat, the hook order and arguments, the trigger's name and reference count.
inline bool verify_boat_manager_game( )
{
    const unsigned* g = g_golden_boat_manager_game_words;
    const unsigned nv = *g++;
    int failed = 0;
    std::vector< std::uint8_t > images( 8 * 0x400, 0 );
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned fn = *g++, n = *g++, b5c = *g++, b2b8 = *g++, vp = *g++;
        std::vector< unsigned > flags( *g++ );
        for ( unsigned& f : flags )
            f = *g++;
        const unsigned w70 = *g++, w74 = *g++, refs = *g++, ret = *g++, a70 = *g++, a74 = *g++, refs_after = *g++;
        const unsigned nn = *g++;
        const std::string name( reinterpret_cast< const char* >( g ), nn );
        g += ( nn + 3 ) / 4;
        std::vector< std::vector< unsigned > > glog( *g++ );
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        std::vector< std::vector< unsigned > > log;
        std::uint32_t sort = w74;
        std::int32_t references = static_cast< std::int32_t >( refs );
        std::string trigger_name;
        unsigned mine_ret = 0;
        if ( fn == 0 )
        {
            boat_manager_t m;
            for ( unsigned j = 0; j < n && j < 8; ++j )
            {
                images[ 0x400 * j + 0x32F ] = static_cast< std::uint8_t >( flags[ j ] );
                m.all.boats.push_back( images.data( ) + 0x400 * j );
            }
            m.debug_camera = static_cast< std::uint8_t >( b5c );
            m.draw_hud = static_cast< std::uint8_t >( b2b8 );
            m.viewport = static_cast< std::int32_t >( vp );
            boat_manager_draw( m, sort, [ & ]( const void* boat ) {
                log.push_back( { 1u, static_cast< unsigned >( ( static_cast< const std::uint8_t* >( boat ) - images.data( ) ) / 0x400 ), 1u } );
            } );
        }
        else
        {
            boat_manager_game_hooks_t h;
            h.register_tick = [ & ]( ) { log.push_back( { 2u, 1u, 1u } ); };
            h.register_draw = [ & ]( ) { log.push_back( { 3u, 1u } ); };
            h.create_trigger = [ & ]( ) { log.push_back( { 4u, 1u } ); log.push_back( { 5u } ); };
            h.name_trigger = [ & ]( const std::string& s ) { trigger_name = s; };
            h.load_trigger = [ & ]( ) { log.push_back( { 6u, 1u } ); };
            h.hash_trigger = [ & ]( std::uint32_t x ) { log.push_back( { 7u, 1u, x } ); };
            h.add_trigger = [ & ]( ) { log.push_back( { 8u, 1u } ); };
            h.remove_trigger = [ & ]( ) { log.push_back( { 9u, 1u } ); };
            h.delete_trigger = [ & ]( ) { log.push_back( { 10u, 1u } ); };
            h.unregister_component = [ & ]( ) { log.push_back( { 11u, 1u } ); };
            h.unregister_draw = [ & ]( ) { log.push_back( { 12u, 1u } ); };
            h.unregister_tick = [ & ]( ) { log.push_back( { 13u, 1u } ); };
            if ( fn == 5 )
                mine_ret = boat_manager_game_initialize( h ) ? 1u : 0u;
            else
                boat_manager_game_release( references, h );
        }
        const bool same = log == glog && sort == a74 && a70 == w70 && mine_ret == ret &&
                          static_cast< unsigned >( references ) == refs_after && ( fn != 5 || trigger_name == name );
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "boat_manager_game vector %u (function %u): sort 0x%X (retail 0x%X), %zu calls (%zu)", v, fn, sort, a74, log.size( ), glog.size( ) );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuBoatManager's draw 0x4AB300 / slot 5 0x4AAD70 / slot 3 0x4AAF10 match retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "boat_manager_game FAILED %d of %u vectors", failed, nv );
    return false;
}

// The document as the case serialises it: s<len>:<text>, o<n>{<len>:<key><value>...}, t<tag>.
inline std::string nuisance_serial( const value_t& v )
{
    if ( v.tag == 4 )
        return "s" + std::to_string( v.text.size( ) ) + ":" + v.text;
    if ( v.tag == 6 )
    {
        std::string out = "o" + std::to_string( v.object.size( ) ) + "{";
        for ( const auto& m : v.object )
            out += std::to_string( m.first.size( ) ) + ":" + m.first + nuisance_serial( m.second );
        return out + "}";
    }
    return "t" + std::to_string( v.tag );
}

// VuAiNuisanceDriverStartEntity's slots 6 0x42FDC0 / 7 0x42FF80 and its plugs DriverTarget 0x42FF90 / DriverDisable
// 0x430020 against retail (oracle case nuisance_start): the document handed to the child's load, the child's name, the
// hook order and arguments, and - after a plug - every weak-reference link word, +0x81 and the VuRetVal word.
inline bool verify_nuisance_start( )
{
    const unsigned* g = g_golden_nuisance_start_words;
    auto text = [ & ]( ) {
        const unsigned n = *g++;
        std::string s( reinterpret_cast< const char* >( g ), n );
        g += ( n + 3 ) / 4;
        return s;
    };
    const std::uint32_t tgt = 0x900, child = 0x400, old = 0x940, r0 = 0x980, r1 = 0x9C0, r2 = 0xA00, drv = 0x2200, ref = drv + 0xE4;
    auto sym = [ & ]( std::uint32_t x ) -> unsigned {
        if ( x == 0 ) return 0;
        if ( x == tgt ) return 1;
        if ( x == child ) return 2;
        if ( x == old ) return 3;
        if ( x == r0 ) return 4;
        if ( x == r1 ) return 5;
        if ( x == r2 ) return 6;
        if ( x == ref ) return 7;
        return 0x100 + ( x & 0xFF );
    };
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned fn = *g++, has_child = *g++, b80 = *g++, b81 = *g++, kind = *g++, state = *g++, has_r2 = *g++, ptype = *g++,
                       size = *g++, hash = *g++, key = *g++, answer = *g++;
        const std::string name = text( ), boat = text( );
        unsigned after[ 13 ];
        for ( unsigned& a : after )
            a = *g++;
        const std::string child_name = text( );
        std::vector< std::vector< unsigned > > glog( *g++ );
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        std::map< std::uint32_t, std::uint32_t > mem;
        vu_words_t w;
        w.get = [ & ]( std::uint32_t a ) { auto it = mem.find( a ); return it == mem.end( ) ? 0u : it->second; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ a ] = x; };
        mem[ child + 0x220 ] = drv;
        mem[ drv ] = kind;
        if ( state == 1 )
        {
            mem[ old + 8 ] = ref;
            mem[ ref + 8 ] = r1; mem[ ref + 0xC ] = old;
            mem[ r1 + 4 ] = ref; mem[ r1 + 0xC ] = old;
        }
        else if ( state == 2 || state == 3 )
        {
            mem[ old + 8 ] = r0;
            mem[ r0 + 8 ] = ref; mem[ r0 + 0xC ] = old;
            mem[ ref + 4 ] = r0; mem[ ref + 8 ] = state == 2 ? r1 : 0; mem[ ref + 0xC ] = old;
            if ( state == 2 )
            {
                mem[ r1 + 4 ] = ref; mem[ r1 + 0xC ] = old;
            }
        }
        if ( has_r2 )
        {
            mem[ tgt + 8 ] = r2;
            mem[ r2 + 0xC ] = tgt;
        }
        std::uint8_t params[ 0x84 ] = {};
        std::memcpy( params, &ptype, 4 );
        std::memcpy( params + 4, &hash, 4 );
        std::memcpy( params + 0x80, &size, 4 );
        const std::uint32_t answers[ 4 ] = { 0, tgt, child, old };
        std::vector< std::vector< unsigned > > log;
        auto find = [ & ]( std::uint32_t h ) -> std::uint32_t {
            log.push_back( { 6u, h } );
            return h == key && answer < 4 ? answers[ answer ] : 0u;
        };
        nuisance_start_t s;
        s.boat = boat;
        s.active = static_cast< std::uint8_t >( b80 );
        s.disabled = static_cast< std::uint8_t >( b81 );
        std::uint32_t ret = 0xCDCDCDCDu;
        std::string named;
        nuisance_start_hooks_t h;
        h.name_child = [ & ]( const std::string& n ) { named = n; };
        h.load_child = [ & ]( const value_t& doc ) {
            const std::string serial = nuisance_serial( doc );
            std::vector< unsigned > e = { 1u, 1u, static_cast< unsigned >( serial.size( ) ) };
            for ( std::size_t k = 0; k < serial.size( ); k += 4 )
            {
                unsigned word = 0;
                std::memcpy( &word, serial.data( ) + k, std::min< std::size_t >( 4, serial.size( ) - k ) );
                e.push_back( word );
            }
            log.push_back( e );
        };
        h.hash_child = [ & ]( std::uint32_t seed ) { log.push_back( { 2u, 1u, seed } ); };
        h.place_child = [ & ]( bool notify ) { log.push_back( { 3u, 1u, 1u, notify ? 1u : 0u } ); };
        h.add_child = [ & ]( ) { log.push_back( { 4u, 1u } ); };
        h.remove_child = [ & ]( ) { log.push_back( { 5u, 1u } ); };
        const std::uint32_t c = has_child ? child : 0u;
        if ( fn == 6 )
            nuisance_start_game_initialize( name, s, h );
        else if ( fn == 7 )
            nuisance_start_game_release( h );
        else if ( fn == 1 )
            ret = nuisance_start_driver_target( s, c, params, find, w );
        else
            ret = nuisance_start_driver_disable( s, c, params, find, w );
        const unsigned mine[ 13 ] = { sym( w.get( ref + 4 ) ), sym( w.get( ref + 8 ) ), sym( w.get( ref + 0xC ) ), sym( w.get( tgt + 8 ) ),
                                      sym( w.get( old + 8 ) ), sym( w.get( r0 + 4 ) ), sym( w.get( r0 + 8 ) ), sym( w.get( r1 + 4 ) ),
                                      sym( w.get( r1 + 8 ) ), sym( w.get( r2 + 4 ) ), sym( w.get( r2 + 8 ) ), s.disabled, ret };
        const bool same = log == glog && std::equal( mine, mine + 13, after ) && ( fn != 6 || named == child_name );
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "nuisance_start vector %u (function %u): %zu calls (%zu), +0xF0 %u (retail %u), +0x81 %u (%u), name \"%s\" (\"%s\")", v, fn,
                       log.size( ), glog.size( ), mine[ 2 ], after[ 2 ], mine[ 11 ], after[ 11 ], named.c_str( ), child_name.c_str( ) );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuAiNuisanceDriverStartEntity slots 6 0x42FDC0 / 7 0x42FF80, DriverTarget 0x42FF90 / DriverDisable 0x430020 match retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "nuisance_start FAILED %d of %u vectors", failed, nv );
    return false;
}

// VuAiNuisanceDriverStartEntity's ctor 0x42FA20 (create 0x42F930 run whole) and dtor 0x42FD20 against retail (oracle
// case nuisance_start_ctor): nuisance_start_construct's fields and its framework calls (the hooks below stand for the
// engine ctors: each makes the object retail's run left - the VuEntity ctor its transform component 0x74D324, the DB
// property 0x74C4C0, the components 0x74C3F0 / 0x74C550, an input plug 0x737138 with the delegate 0x739B84, the boat
// 0x7383D4 with count 1), then nuisance_start_destroy's call log and words per dtor vector.
inline bool verify_nuisance_start_ctor( )
{
    const unsigned* g = g_golden_nuisance_start_ctor_words;
    auto text = [ & ]( ) {
        const unsigned n = *g++;
        std::string s( reinterpret_cast< const char* >( g ), n );
        g += ( n + 3 ) / 4;
        return s;
    };
    auto word_text = [ ]( const std::string& s ) {
        std::vector< unsigned > w = { static_cast< unsigned >( s.size( ) ) };
        for ( std::size_t k = 0; k < s.size( ); k += 4 )
        {
            unsigned x = 0;
            std::memcpy( &x, s.data( ) + k, std::min< std::size_t >( 4, s.size( ) - k ) );
            w.push_back( x );
        }
        return w;
    };
    int failed = 0;
    // ---- the ctor: ours as the same word stream ----
    std::vector< unsigned > props, comps, plugs;
    std::uint32_t flags = 0xFFFFFFFFu, script = 0xFFFFFFFFu;
    unsigned nprops = 0, nplugs = 0;
    nuisance_start_ctor_hooks_t ch;
    ch.entity = [ & ]( std::uint32_t f ) { flags = f; comps.push_back( 0x74D324u ); };
    ch.db_property = [ & ]( const char* name, const char* db ) {
        ++nprops;
        props.push_back( 0x74C4C0u );
        for ( unsigned x : word_text( name ) ) props.push_back( x );
        props.push_back( 1u );   // its value is +0x64
        props.push_back( 1u );   // VuProperty 0x406B70's +8
        for ( unsigned x : word_text( std::string( "VuDBAsset/" ) + db ) ) props.push_back( x );
    };
    ch.script_component = [ & ]( std::uint32_t, std::uint32_t ) { script = static_cast< std::uint32_t >( comps.size( ) ); comps.push_back( k_script_component_vtable ); };
    ch.layout_component = [ & ]( ) { comps.push_back( 0x74C550u ); };
    ch.input_plug = [ & ]( const char* name, std::uint32_t method, std::uint32_t count, std::uint32_t type ) {
        ++nplugs;
        plugs.push_back( 0x737138u );
        for ( unsigned x : word_text( name ) ) plugs.push_back( x );
        plugs.push_back( 0u );
        plugs.push_back( count );
        plugs.push_back( type );
        plugs.push_back( 1u );
        plugs.push_back( 0x739B84u );
        plugs.push_back( 1u );
        plugs.push_back( method );
    };
    ch.make_child = [ ]( ) { return 0x400u; };
    nuisance_start_t s;
    s.boat = "stale";
    const std::uint32_t child = nuisance_start_construct( s, ch );
    std::vector< unsigned > mine = { k_nuisance_start_vtable, flags };
    for ( unsigned x : word_text( s.boat ) ) mine.push_back( x );
    mine.insert( mine.end( ), { static_cast< unsigned >( s.boat.size( ) ), 15u, s.active, s.disabled, nprops } );
    mine.insert( mine.end( ), props.begin( ), props.end( ) );
    mine.push_back( static_cast< unsigned >( comps.size( ) ) );
    mine.insert( mine.end( ), comps.begin( ), comps.end( ) );
    mine.insert( mine.end( ), { script, k_script_component_vtable, 0x74C550u, nplugs } );
    mine.insert( mine.end( ), plugs.begin( ), plugs.end( ) );
    mine.insert( mine.end( ), { child ? 0x7383D4u : 0u, child ? 1u : 0u, 0u } );
    const unsigned ctor_words = *g++;
    const unsigned* start = g;
    std::size_t at = 0;
    for ( ; at < mine.size( ) && at < ctor_words && start[ at ] == mine[ at ]; ++at )
        ;
    if ( at != mine.size( ) || ctor_words != mine.size( ) || s.boat_default != "Police" )
    {
        ++failed;
        LOG_ERROR( "nuisance_start_ctor: the ctor differs at word %zu of %zu (ours 0x%X, retail 0x%X; retail %u words)", at, mine.size( ),
                   at < mine.size( ) ? mine[ at ] : 0u, at < ctor_words ? start[ at ] : 0u, ctor_words );
    }
    g += ctor_words;
    // ---- the dtor vectors ----
    const unsigned nv = *g++;
    const std::uint32_t self = 0x100u, ch_ = 0x400u, heap_text = 0x3000u;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned count = *g++;
        const std::string boat = text( );
        const unsigned fl = *g++;
        std::vector< std::vector< unsigned > > glog( *g++ );
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        unsigned after[ 6 ];
        for ( unsigned& a : after )
            a = *g++;
        std::map< std::uint32_t, std::uint32_t > mem;
        vu_words_t w;
        w.get = [ & ]( std::uint32_t a ) { auto it = mem.find( a ); return it == mem.end( ) ? 0u : it->second; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ a ] = x; };
        mem[ self ] = 0xCDCDCDCDu;
        mem[ self + 0x7C ] = ch_;
        mem[ ch_ + 4 ] = count;
        mem[ self + 0x64 ] = boat.size( ) < 16 ? 0x41414141u : heap_text;
        mem[ self + 0x74 ] = static_cast< std::uint32_t >( boat.size( ) );
        mem[ self + 0x78 ] = boat.size( ) < 16 ? 15u : static_cast< std::uint32_t >( boat.size( ) | 15u );
        std::vector< std::vector< unsigned > > log;
        nuisance_start_dtor_hooks_t dh;
        dh.destroy_child = [ & ]( std::uint32_t c ) { log.push_back( { 1u, c == ch_ ? 1u : 0u, 1u } ); };
        dh.free_block = [ & ]( std::uint32_t a ) { log.push_back( { 3u, a == self ? 1u : a == heap_text ? 2u : 0u } ); };
        dh.entity_dtor = [ & ]( ) { log.push_back( { 2u, 1u, w.get( self ) } ); };
        nuisance_start_t st;
        st.boat = boat;
        const bool ok = nuisance_start_destroy( w, self, fl, dh, &st );
        const unsigned ours[ 6 ] = { w.get( ch_ + 4 ), w.get( self ), w.get( self + 0x64 ) & 0xFFu, w.get( self + 0x74 ), w.get( self + 0x78 ), ok ? 1u : 0u };
        if ( !( log == glog && std::equal( ours, ours + 6, after ) && st.boat.empty( ) ) && ++failed <= 8 )
            LOG_ERROR( "nuisance_start_ctor dtor vector %u (count %u, \"%s\", flags %u): %zu calls (%zu), count 0x%X (0x%X)", v, count, boat.c_str( ), fl,
                       log.size( ), glog.size( ), ours[ 0 ], after[ 0 ] );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuAiNuisanceDriverStartEntity ctor 0x42FA20 / dtor 0x42FD20 match retail: the ctor's %zu words, %u dtor vectors", mine.size( ), nv );
        return true;
    }
    LOG_ERROR( "nuisance_start_ctor FAILED %d checks", failed );
    return false;
}
// VuBoatEntity's OnCameraShake 0x41E7B0 / OnRumble 0x41E950 (-> 0x41E2E0) against retail (oracle case boat_events):
// the camera's shake words and the driver's rumble pair, bit for bit.
inline bool verify_boat_events( )
{
    const unsigned* g = g_golden_boat_events_words;
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned fn = *g++, vtable = *g++;
        std::uint8_t drv[ 0x100 ] = {}, cam[ 0x300 ] = {}, params[ 0x84 ] = {};
        std::memcpy( drv + 0xC8, g, 8 ); g += 2;
        std::memcpy( cam + 0x2E4, g, 28 ); g += 7;
        std::memcpy( params, g, 32 ); g += 8;
        std::memcpy( params + 0x80, g, 4 ); g += 1;
        const unsigned* after = g;
        g += 10;
        if ( fn == 1 )
            boat_on_camera_shake( cam, params );
        else
            boat_on_rumble( drv, vtable, params );
        unsigned mine[ 10 ];
        std::memcpy( mine, cam + 0x2E4, 28 );
        std::memcpy( mine + 7, drv + 0xC8, 8 );
        mine[ 9 ] = 1;
        if ( !std::equal( mine, mine + 10, after ) && ++failed <= 8 )
            LOG_ERROR( "boat_events vector %u (handler %u): +0x2E4 0x%08X (retail 0x%08X), +0x2E8 0x%08X (0x%08X), +0xC8 0x%08X (0x%08X), +0xCC 0x%08X (0x%08X)",
                       v, fn, mine[ 0 ], after[ 0 ], mine[ 1 ], after[ 1 ], mine[ 7 ], after[ 7 ], mine[ 8 ], after[ 8 ] );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuBoatEntity OnCameraShake 0x41E7B0 / OnRumble 0x41E950 match retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "boat_events FAILED %d of %u vectors", failed, nv );
    return false;
}
// btRigidBody's applyTorqueImpulse 0x4220C0 / applyImpulse 0x4221C0 against retail (oracle case body_impulse): the
// body's linear and angular velocity words, bit for bit.
inline bool verify_body_impulse( )
{
    const unsigned* g = g_golden_body_impulse_words;
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned fn = *g++;
        std::uint8_t b[ 0x280 ] = {};
        std::memcpy( b + 0x110, g, 0x70 ); g += 0x1C;
        std::memcpy( b + 0x260, g, 12 ); g += 3;
        float a[ 3 ], r[ 3 ];
        std::memcpy( a, g, 12 ); g += 3;
        std::memcpy( r, g, 12 ); g += 3;
        const unsigned* after = g;
        g += 9;
        if ( fn == 1 )
            body_apply_torque_impulse( b, a );
        else
            body_apply_impulse( b, a, r );
        unsigned mine[ 8 ];
        std::memcpy( mine, b + 0x140, 32 );
        if ( !( std::equal( mine, mine + 8, after ) && after[ 8 ] == 1 ) && ++failed <= 8 )
            LOG_ERROR( "body_impulse vector %u (function %u): +0x140 0x%08X (retail 0x%08X), +0x150 0x%08X (0x%08X), +0x154 0x%08X (0x%08X)", v, fn,
                       mine[ 0 ], after[ 0 ], mine[ 4 ], after[ 4 ], mine[ 5 ], after[ 5 ] );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "btRigidBody applyTorqueImpulse 0x4220C0 / applyImpulse 0x4221C0 match retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "body_impulse FAILED %d of %u vectors", failed, nv );
    return false;
}
// VuBoatDamageController::VuPiece's getWorldTransform 0x412CA0 / setWorldTransform 0x412D90 against retail (oracle
// case piece_motion): the btTransform out or the piece's matrix, bit for bit.
inline bool verify_piece_motion( )
{
    const unsigned* g = g_golden_piece_motion_words;
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned fn = *g++;
        std::uint8_t piece[ 0x200 ] = {};
        std::memcpy( piece + 8, g, 64 ); g += 16;
        std::memcpy( piece + 0x1B0, g, 12 ); g += 3;
        float bt[ 16 ];
        std::memcpy( bt, g, 64 ); g += 16;
        const unsigned* after = g;
        g += 17;
        unsigned mine[ 16 ];
        if ( fn == 1 )
        {
            float out[ 16 ];
            piece_motion_get( piece, out );
            std::memcpy( mine, out, 64 );
        }
        else
        {
            piece_motion_set( piece, bt );
            std::memcpy( mine, piece + 8, 64 );
        }
        if ( !( std::equal( mine, mine + 16, after ) && after[ 16 ] == 1 ) && ++failed <= 8 )
        {
            int k = 0;
            while ( k < 15 && mine[ k ] == after[ k ] )
                ++k;
            LOG_ERROR( "piece_motion vector %u (function %u): word %d 0x%08X (retail 0x%08X)", v, fn, k, mine[ k ], after[ k ] );
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuPiece getWorldTransform 0x412CA0 / setWorldTransform 0x412D90 match retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "piece_motion FAILED %d of %u vectors", failed, nv );
    return false;
}
// VuBoatEntity's setWorldTransform 0x41E460 against retail (oracle case boat_set_world): the component's world and
// world euler (+0xAC, a stack word, not compared), driving world/dynamics.hxx's boat_motion_set - the one
// recreation, which performs 0x534830 itself, so only retail's two-hook count is checked, not the hook calls.
inline bool verify_boat_set_world( )
{
    const unsigned* g = g_golden_boat_set_world_words;
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        float bt[ 16 ];
        std::memcpy( bt, g, 64 ); g += 16;
        std::uint8_t boat[ 0x520 ] = {}, tr[ 0xC8 ] = {};
        std::memcpy( boat + 0x290, g, 12 ); g += 3;
        const unsigned* after = g;
        g += 19;
        const unsigned rest = *g++;
        std::vector< std::vector< unsigned > > glog( *g++ ), log;
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        const bool ran = boat_motion_set( boat, tr, false, bt, bt + 12 );
        unsigned mine[ 19 ];
        std::memcpy( mine, tr + 0x50, 64 );
        std::memcpy( mine + 16, tr + 0xA0, 12 );
        if ( !( std::equal( mine, mine + 19, after ) && rest == 1 && ran && glog.size( ) == 2 ) && ++failed <= 8 )
        {
            int k = 0;
            while ( k < 18 && mine[ k ] == after[ k ] )
                ++k;
            LOG_ERROR( "boat_set_world vector %u: word %d 0x%08X (retail 0x%08X), %zu hooks (%zu)", v, k, mine[ k ], after[ k ], log.size( ), glog.size( ) );
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuBoatEntity setWorldTransform 0x41E460 matches retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "boat_set_world FAILED %d of %u vectors", failed, nv );
    return false;
}
// The drivers' scalar-deleting dtors 0x40EA30 / 0x40B7E0 / 0x40E530 / 0x40D920 against retail (oracle case
// driver_dtors): the hook log and the words left (the reference's only for the nuisance driver, +0x84 not for the
// entity class - random image words there).
inline bool verify_driver_dtors( )
{
    const unsigned* g = g_golden_driver_dtors_words;
    auto text = [ & ]( ) {
        const unsigned n = *g++;
        std::string s( reinterpret_cast< const char* >( g ), n );
        g += ( n + 3 ) / 4;
        return s;
    };
    const std::uint32_t drv = 0x100, txt = 0x800, plan = 0x880, ev = 0x900, old = 0x940, r0 = 0x980, r1 = 0x9C0, ref = drv + 0xE4;
    auto sym = [ & ]( std::uint32_t x ) -> unsigned {
        if ( x == 0 ) return 0;
        if ( x == drv ) return 1;
        if ( x == txt ) return 2;
        if ( x == plan ) return 3;
        if ( x == ev ) return 4;
        if ( x == old ) return 5;
        if ( x == r0 ) return 6;
        if ( x == r1 ) return 7;
        if ( x == ref ) return 8;
        return 0x100 + ( x & 0xFF );
    };
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned kind = *g++, flags = *g++;
        const std::string s = text( );
        const unsigned b8 = *g++, event = *g++, state = *g++, untouched = *g++;
        std::vector< std::vector< unsigned > > glog( *g++ ), log;
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        const unsigned* after = g;
        g += 16;
        std::map< std::uint32_t, std::uint32_t > mem;
        vu_words_t w;
        w.get = [ & ]( std::uint32_t a ) { auto it = mem.find( a ); return it == mem.end( ) ? 0u : it->second; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ a ] = x; };
        mem[ drv + 0x8C ] = plan;
        mem[ drv + 0xF8 ] = b8;
        mem[ drv + 0xFC ] = event ? ev : 0u;
        mem[ drv + 0x60 ] = s.size( ) < 16 ? 0x41414141u : txt;
        mem[ drv + 0x70 ] = static_cast< std::uint32_t >( s.size( ) );
        mem[ drv + 0x74 ] = s.size( ) < 16 ? 15u : static_cast< std::uint32_t >( s.size( ) | 15u );
        if ( state == 1 )
        {
            mem[ old + 8 ] = ref;
            mem[ ref + 8 ] = r1; mem[ ref + 0xC ] = old;
            mem[ r1 + 4 ] = ref; mem[ r1 + 0xC ] = old;
        }
        else if ( state == 2 || state == 3 )
        {
            mem[ old + 8 ] = r0;
            mem[ r0 + 8 ] = ref; mem[ r0 + 0xC ] = old;
            mem[ ref + 4 ] = r0; mem[ ref + 8 ] = state == 2 ? r1 : 0; mem[ ref + 0xC ] = old;
            if ( state == 2 )
            {
                mem[ r1 + 4 ] = ref; mem[ r1 + 0xC ] = old;
            }
        }
        driver_dtor_hooks_t h;
        h.free_block = [ & ]( std::uint32_t a ) { log.push_back( { 1u, sym( a ) } ); };
        h.aligned_free = [ & ]( std::uint32_t a ) { log.push_back( { 2u, sym( a ) } ); };
        h.entity_dtor = [ & ]( ) { log.push_back( { 3u, 1u, w.get( drv ) } ); };
        h.event_clear_callback = [ & ]( std::uint32_t e ) { log.push_back( { 4u, sym( e ), 0u, 0u } ); };
        h.event_stop = [ & ]( std::uint32_t e, bool immediate ) { log.push_back( { 5u, sym( e ), immediate ? 1u : 0u } ); };
        const driver_dtor_kind_t k = kind == 1 ? driver_dtor_kind_t::entity : kind == 2 ? driver_dtor_kind_t::ai : kind == 3 ? driver_dtor_kind_t::ui
                                                                                                                    : driver_dtor_kind_t::nuisance;
        const std::uint32_t r = driver_destroy( k, w, drv, flags, h );
        const unsigned mine[ 16 ] = { w.get( drv ), w.get( drv + 0x60 ) & 0xFFu, w.get( drv + 0x70 ), w.get( drv + 0x74 ), w.get( drv + 0x84 ),
                                      sym( w.get( drv + 0xFC ) ), sym( w.get( ref ) ), sym( w.get( ref + 4 ) ), sym( w.get( ref + 8 ) ),
                                      sym( w.get( ref + 0xC ) ), sym( w.get( old + 8 ) ), sym( w.get( r0 + 4 ) ), sym( w.get( r0 + 8 ) ),
                                      sym( w.get( r1 + 4 ) ), sym( w.get( r1 + 8 ) ), r == drv ? 1u : 0u };
        bool same = log == glog && untouched == 1;
        for ( int j = 0; j < 16 && same; ++j )
        {
            if ( j == 4 && kind == 1 ) continue;
            if ( j >= 6 && j <= 9 && kind != 4 ) continue;
            same = mine[ j ] == after[ j ];
        }
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "driver_dtors vector %u (class %u, flags %u): %zu calls (%zu), +0 0x%X (0x%X), +0xFC %u (%u)", v, kind, flags, log.size( ),
                       glog.size( ), mine[ 0 ], after[ 0 ], mine[ 5 ], after[ 5 ] );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "the drivers' dtors 0x40EA30 / 0x40B7E0 / 0x40E530 / 0x40D920 match retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "driver_dtors FAILED %d of %u vectors", failed, nv );
    return false;
}
// VuAiManagerImpl's slots 15 0x404210 / 3 0x404570 / 9 0x403510 against retail (oracle case fleet_small).
inline bool verify_fleet_small( )
{
    const unsigned* g = g_golden_fleet_small_words;
    const unsigned nv = *g++;
    int failed = 0;
    const std::uint32_t self = 0x100, arr = 0x200;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned slot = *g++, count = *g++, owned = *g++;
        unsigned before[ 7 ];
        for ( unsigned& b : before )
            b = *g++;
        std::vector< unsigned > ents( *g++ );
        for ( unsigned& e : ents )
            e = *g++;
        std::vector< std::vector< unsigned > > glog( *g++ ), log;
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        const unsigned* after = g;
        g += 7;
        const unsigned st0 = *g++, rest = *g++;
        std::map< std::uint32_t, std::uint32_t > mem;
        vu_words_t w;
        w.get = [ & ]( std::uint32_t a ) { auto it = mem.find( a ); return it == mem.end( ) ? 0u : it->second; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ a ] = x; };
        mem[ self + 0x1C ] = before[ 0 ];
        mem[ self + 0x24 ] = count;
        mem[ self + 0x28 ] = before[ 2 ];
        mem[ self + 0x2C ] = arr;
        mem[ self + 0x30 ] = owned;
        mem[ self + 0x34 ] = before[ 5 ];
        mem[ self + 0x38 ] = before[ 6 ];
        for ( std::size_t k = 0; k < ents.size( ); ++k )
            mem[ arr + 4 * static_cast< std::uint32_t >( k ) ] = ents[ k ] ? 0x1000u + ents[ k ] : 0u;
        auto sym = [ & ]( std::uint32_t x ) -> unsigned { return x == 0 ? 0u : x == self ? 1u : x == arr ? 2u : x >= 0x1000u ? x - 0x1000u : 0x100u + ( x & 0xFF ); };
        unsigned mine_st0 = 0;
        if ( slot == 15 )
        {
            const float f = fleet_slot15( );
            std::memcpy( &mine_st0, &f, 4 );
        }
        else if ( slot == 3 )
        {
            fleet_manager_t m;
            m.state = static_cast< std::int32_t >( before[ 0 ] );
            fleet_release( m, [ & ]( ) { log.push_back( { 3u, 1u } ); } );
            mem[ self + 0x1C ] = static_cast< std::uint32_t >( m.state );
        }
        else
        {
            fleet_reset_hooks_t h;
            h.free_block = [ & ]( std::uint32_t a ) { log.push_back( { 1u, sym( a ) } ); };
            h.aligned_free = [ & ]( std::uint32_t a ) { log.push_back( { 2u, sym( a ) } ); };
            fleet_reset_words( w, self, h );
        }
        const unsigned mine[ 7 ] = { w.get( self + 0x1C ), w.get( self + 0x24 ), w.get( self + 0x28 ), sym( w.get( self + 0x2C ) ), w.get( self + 0x30 ) & 0xFFu,
                                     w.get( self + 0x34 ), w.get( self + 0x38 ) };
        if ( !( std::equal( mine, mine + 7, after ) && log == glog && mine_st0 == st0 && rest == 1 ) && ++failed <= 8 )
            LOG_ERROR( "fleet_small vector %u (slot %u): %zu calls (%zu), +0x1C %u (%u), +0x30 %u (%u), ST0 0x%08X (0x%08X)", v, slot, log.size( ),
                       glog.size( ), mine[ 0 ], after[ 0 ], mine[ 4 ], after[ 4 ], mine_st0, st0 );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuAiManagerImpl slots 15 0x404210 / 3 0x404570 / 9 0x403510 match retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "fleet_small FAILED %d of %u vectors", failed, nv );
    return false;
}
// 0x4ABB40 / 0x4ABB10 / the atexit stubs 0x4AA960 / 0x403250 against retail (oracle case boat_small).
inline bool verify_boat_small( )
{
    const unsigned* g = g_golden_boat_small_words;
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned fn = *g++;
        std::uint8_t data[ 0x100 ];
        std::memcpy( data, g, 0x100 ); g += 0x40;
        const unsigned off = *g++, n = *g++, seed = *g++, ret = *g++;
        bool same = true;
        if ( fn == 1 )
            same = boat_fnv_range( data + off, n, seed ) == ret;
        else if ( fn == 2 )
        {
            std::uint8_t dst[ 0x80 ];
            std::memset( dst, 0xCD, sizeof( dst ) );
            boat_stream_t st;
            st.base = data;
            st.position = off;
            boat_stream_read( st, dst, n );
            same = std::memcmp( dst, g, 0x80 ) == 0 && st.position == g[ 0x20 ];
            g += 0x21;
        }
        else
        {
            std::uint32_t global = 0x12345678u;
            boat_atexit_clear( global );
            same = global == ret;
        }
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "boat_small vector %u (function %u) differs", v, fn );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "0x4ABB40 / 0x4ABB10 / atexit 0x4AA960 / 0x403250 match retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "boat_small FAILED %d of %u vectors", failed, nv );
    return false;
}
// The physics (Bullet) leaf virtuals (engine collision/leaves.hxx) against retail (oracle case bt_leaves): each row's
// eax (the bits its kind defines), ST0 for `one`, the ret's pop, the image's changed words, the name's text.
inline bool verify_bt_leaves( )
{
    const unsigned* g = g_golden_bt_leaves_words;
    auto text = [ & ]( ) {
        const unsigned n = *g++;
        std::string s( reinterpret_cast< const char* >( g ), n );
        g += ( n + 3 ) / 4;
        return s;
    };
    const unsigned nv = *g++;
    int failed = 0;
    const std::uint32_t self = 0x30000100u;   // the case's ARG_BASE + 0x100
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned address = *g++;
        std::uint8_t object[ 0x200 ] = {};
        std::memcpy( object, g, 0x200 ); g += 0x80;
        std::uint8_t before[ 0x200 ];
        std::memcpy( before, object, 0x200 );
        const unsigned arg0 = *g++, eax = *g++, st0 = *g++, pop = *g++;
        std::vector< std::pair< unsigned, unsigned > > changed( *g++ );
        for ( auto& c : changed )
        {
            c.first = *g++;
            c.second = *g++;
        }
        const std::string t = text( );
        const bool has_text = *g++ != 0;
        const bt_leaf_t* l = bt_leaf_find( address );
        bool same = l != nullptr;
        if ( same )
        {
            const bt_leaf_result_t r = bt_leaf_call( *l, object, self, arg0 );
            same = ( r.eax & r.eax_mask ) == ( eax & r.eax_mask ) && l->pop == pop;
            if ( r.st0_set )
            {
                unsigned bits;
                std::memcpy( &bits, &r.st0, 4 );
                same = same && bits == st0;
            }
            std::vector< std::pair< unsigned, unsigned > > mine;
            for ( unsigned o = 0; o < 0x200; o += 4 )
                if ( std::memcmp( object + o, before + o, 4 ) != 0 )
                {
                    unsigned w;
                    std::memcpy( &w, object + o, 4 );
                    mine.emplace_back( o, w );
                }
            // A store of 0 over a word that already held 0 changes nothing - retail's list is of changed words too.
            same = same && mine == changed;
            if ( l->kind == bt_leaf_kind_t::text )
                same = same && has_text && l->text && t == l->text;
        }
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "bt_leaves vector %u (0x%X): eax 0x%08X, pop %u, text \"%s\"", v, address, eax, pop, t.c_str( ) );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "the %zu physics (Bullet) leaf virtuals match retail: %u vectors", sizeof( k_bt_leaves ) / sizeof( k_bt_leaves[ 0 ] ), nv );
        return true;
    }
    LOG_ERROR( "bt_leaves FAILED %d of %u vectors", failed, nv );
    return false;
}
// The contact-added callback 0x574E90 -> 0x5751F0 -> 0x519720 against retail (oracle case contact_added): the manifold
// point after (every word, bit for bit), the return, and each listener / callback call's record (but its stale-stack
// words) with the case's scripted write-backs replayed.
inline bool verify_contact_added( )
{
    const unsigned* g = g_golden_contact_added_words;
    auto bits = []( float f ) { unsigned u; std::memcpy( &u, &f, 4 ); return u; };
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        collision_mesh_asset_t mesh;
        const unsigned nvert = *g++;
        mesh.vertices.bytes.assign( 16u * nvert, 0 );
        for ( unsigned k = 0; k < nvert; ++k )
        {
            std::memcpy( mesh.vertices.bytes.data( ) + 16 * k, g, 12 );
            const float one = 1.0f;
            std::memcpy( mesh.vertices.bytes.data( ) + 16 * k + 12, &one, 4 );
            g += 3;
        }
        const unsigned ntri = *g++;
        mesh.indices.bytes.assign( 6u * ntri, 0 );
        for ( unsigned k = 0; k < 3 * ntri; ++k )
        {
            const std::uint16_t i = static_cast< std::uint16_t >( *g++ );
            std::memcpy( mesh.indices.bytes.data( ) + 2 * k, &i, 2 );
        }
        mesh.triangles.bytes.assign( 4u * ntri, 0 );
        std::memcpy( mesh.triangles.bytes.data( ), g, 4u * ntri );
        g += ntri;
        mesh.materials.bytes.assign( 44u * 32, 0xCD );
        for ( unsigned k = 0; k < 32; ++k )
            mesh.materials.bytes[ 44 * k + 40 ] = static_cast< std::uint8_t >( *g++ );
        surface_table_t table;
        table.types.resize( 12 );
        for ( auto& t : table.types )
        {
            std::memcpy( &t.friction, g++, 4 );
            t.color = 0xFFFFFFFFu;
        }
        const std::int32_t index = static_cast< std::int32_t >( *g++ );
        std::uint8_t cp[ 0x80 ];
        std::memcpy( cp, g, 0x80 );
        g += 32;
        const unsigned has_mesh = *g++;
        const unsigned type0 = *g++, type1 = *g++, s0 = *g++, s1 = *g++, cb0 = *g++, cb1 = *g++, nl = *g++;
        std::vector< std::pair< unsigned, unsigned > > muts( *g++ );
        for ( auto& m : muts )
        {
            m.first = g[ 0 ];
            m.second = g[ 1 ];
            g += 2;
        }
        std::uint8_t img[ 2 ][ 0x300 ] = { };
        std::memcpy( img[ 1 ] + 0x10, g, 48 );
        g += 12;
        const unsigned ret = *g++;
        const unsigned* after = g;
        g += 32;
        std::vector< std::vector< unsigned > > glog( *g++ );
        for ( auto& e : glog )
        {
            e.assign( g, g + 14 );
            g += 14;
        }
        const unsigned types[ 2 ] = { type0, type1 }, surfs[ 2 ] = { s0, s1 }, cbs[ 2 ] = { cb0, cb1 };
        for ( int j = 0; j < 2; ++j )
        {
            std::memcpy( img[ j ] + 0xF4, &types[ j ], 4 );
            const unsigned cb = cbs[ j ] ? 0x100u + j : 0u;
            std::memcpy( img[ j ] + 0x2C4, &cb, 4 );
            img[ j ][ 0x2C8 ] = static_cast< std::uint8_t >( surfs[ j ] );
        }
        contact_body_t o0, o1;
        o0.image = img[ 0 ];
        o0.handle = 1;
        o1.image = img[ 1 ];
        o1.handle = 2;
        o1.mesh = has_mesh ? &mesh : nullptr;
        std::vector< std::vector< unsigned > > log;
        std::size_t next = 0;
        auto call = [ & ]( unsigned who ) {
            return [ &, who ]( contact_record_t& r ) {
                const std::pair< unsigned, unsigned > m = next < muts.size( ) ? muts[ next++ ] : std::pair< unsigned, unsigned >( 0u, 0u );
                log.push_back( { who, r.body0, r.body1, r.other, bits( r.pos[ 0 ] ), bits( r.pos[ 1 ] ), bits( r.pos[ 2 ] ), bits( r.normal[ 0 ] ),
                                 bits( r.normal[ 1 ] ), bits( r.normal[ 2 ] ), static_cast< unsigned >( r.surface0 | r.surface1 << 8 ),
                                 bits( r.friction ), m.first, m.second } );
                if ( m.first == 1 )
                    std::memcpy( &r.friction, &m.second, 4 );
                else if ( m.first == 2 )
                    std::memcpy( &r.normal[ 0 ], &m.second, 4 );
            };
        };
        contact_hooks_t h;
        for ( unsigned k = 0; k < nl; ++k )
            h.listeners.push_back( call( 1 + k ) );
        h.callback0 = call( 5 );
        h.callback1 = call( 6 );
        const bool known = contact_added( table, cp, o0, o1, index, h );
        const bool same = known && ret == 0 && std::memcmp( cp, after, 0x80 ) == 0 && log == glog;
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "contact_added vector %u: %s, normal 0x%X 0x%X 0x%X (retail 0x%X 0x%X 0x%X), %zu calls (%zu)", v, known ? "ran" : "UNKNOWN",
                       contact_u32( cp, 0x40 ), contact_u32( cp, 0x44 ), contact_u32( cp, 0x48 ), after[ 16 ], after[ 17 ], after[ 18 ], log.size( ), glog.size( ) );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "The contact-added callback 0x574E90 / 0x5751F0 / 0x519720 matches retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "contact_added FAILED %d of %u vectors", failed, nv );
    return false;
}

// The boat classes' VuRTTI (boat rtti.hxx) against retail (oracle case boat_rtti): every descriptor's parent and name,
// every class's slot 1 / descriptor / chain, and 0x41B4B0 for every class against every descriptor plus 0 / a stray word.
inline bool verify_boat_rtti( )
{
    const unsigned* g = g_golden_boat_rtti_words;
    int failed = 0;
    const unsigned nn = *g++;
    for ( unsigned k = 0; k < nn; ++k )
    {
        const unsigned d = *g++, parent = *g++, n = *g++;
        const std::string name( reinterpret_cast< const char* >( g ), n );
        g += ( n + 3 ) / 4;
        const boat_rtti_t* mine = nullptr;
        for ( const boat_rtti_t& r : g_boat_rtti )
            if ( r.descriptor == d )
                mine = &r;
        if ( ( !mine || mine->parent != parent || name != mine->name ) && ++failed <= 8 )
            LOG_ERROR( "boat_rtti descriptor 0x%X (%s): parent 0x%X (retail 0x%X)", d, name.c_str( ), mine ? mine->parent : 0u, parent );
    }
    if ( nn != sizeof( g_boat_rtti ) / sizeof( g_boat_rtti[ 0 ] ) && ++failed <= 8 )
        LOG_ERROR( "boat_rtti: %u descriptors met (the table has %zu)", nn, sizeof( g_boat_rtti ) / sizeof( g_boat_rtti[ 0 ] ) );
    const unsigned nc = *g++;
    std::vector< unsigned > vtables;
    for ( unsigned k = 0; k < nc; ++k )
    {
        const unsigned vt = *g++, get = *g++, d = *g++, nch = *g++;
        std::vector< unsigned > chain( g, g + nch );
        g += nch;
        vtables.push_back( vt );
        const boat_rtti_class_t* mine = nullptr;
        for ( const boat_rtti_class_t& c : g_boat_rtti_classes )
            if ( c.vtable == vt )
                mine = &c;
        std::vector< unsigned > walk;
        for ( std::uint32_t x = boat_rtti_of( vt ); x; x = boat_rtti_parent( x ) )
            walk.push_back( x );
        if ( ( !mine || mine->get_type != get || mine->descriptor != d || walk != chain ) && ++failed <= 8 )
            LOG_ERROR( "boat_rtti class 0x%X: getType 0x%X (retail 0x%X), descriptor 0x%X (0x%X)", vt, mine ? mine->get_type : 0u, get,
                       mine ? mine->descriptor : 0u, d );
    }
    const unsigned nw = *g++;
    std::vector< unsigned > wanted( g, g + nw );
    g += nw;
    unsigned pairs = 0;
    for ( unsigned vt : vtables )
        for ( unsigned w : wanted )
        {
            const unsigned r = *g++;
            ++pairs;
            if ( ( boat_rtti_is( vt, w ) ? 1u : 0u ) != r && ++failed <= 8 )
                LOG_ERROR( "boat_rtti 0x41B4B0(0x%X, 0x%X): %u (retail %u)", vt, w, boat_rtti_is( vt, w ) ? 1u : 0u, r );
        }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "The boat classes' getType slots and 0x41B4B0 match retail: %u descriptors, %u classes, %u pairs", nn, nc, pairs );
        return true;
    }
    LOG_ERROR( "boat_rtti FAILED %d", failed );
    return false;
}

// VuHumanDriver's explosion listener (0x40F4F0 / 0x40F520 / 0x40F5A0) and slots 6 / 7 / 20 against retail (oracle case
// human_explosion): the camera's shake words, the rumble pair, the radius and position bits, the hooks and arguments.
inline bool verify_human_explosion( )
{
    const unsigned* g = g_golden_human_explosion_words;
    const unsigned listener = *g++, nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned fn = *g++, null = *g++;
        std::uint8_t drv[ 0x100 ] = { }, boat[ 0x300 ] = { }, cam[ 0x300 ] = { }, tr[ 0xA0 ] = { }, params[ 0x40 ] = { };
        std::memcpy( drv + 0xC8, g, 8 );
        g += 2;
        std::memcpy( boat + 0x270, g, 12 );
        std::memcpy( boat + 0x280, g + 4, 12 );
        g += 7;
        std::memcpy( cam + 0x2E4, g, 28 );
        g += 7;
        std::memcpy( tr + 0x80, g, 16 );
        g += 4;
        std::memcpy( params + 0x18, g, 28 );
        g += 7;
        float scale;
        std::memcpy( &scale, g++, 4 );
        const unsigned ret = *g++;
        const unsigned* out = g;
        g += 4;
        const unsigned* drv_after = g;
        g += 2;
        const unsigned* cam_after = g;
        g += 7;
        std::vector< std::vector< unsigned > > glog( *g++ );
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        std::vector< std::vector< unsigned > > log;
        bool same = true;
        if ( fn == 1 )
            human_explosion_hit( drv, cam, params, scale, [ & ]( float f ) { unsigned u; std::memcpy( &u, &f, 4 ); log.push_back( { 3u, u } ); } );
        else if ( fn == 2 )
        {
            const float r = human_explosion_radius( boat );
            unsigned u;
            std::memcpy( &u, &r, 4 );
            same = u == ret;
        }
        else if ( fn == 3 )
        {
            float p[ 4 ];
            human_explosion_position( tr, p );
            same = std::memcmp( p, out, 16 ) == 0;
        }
        else if ( fn == 20 )
            human_recovered( [ & ]( std::uint32_t h, std::uint32_t size ) { log.push_back( { 4u, h, size } ); } );
        else
        {
            auto rec = [ & ]( std::uint8_t* l ) { log.push_back( { fn == 6 ? 1u : 2u, l == drv + 0x80 ? listener : l ? 1u : 0u } ); };
            if ( fn == 6 )
                human_game_initialize( null ? nullptr : drv, rec );
            else
                human_game_release( null ? nullptr : drv, rec );
        }
        same = same && log == glog && std::memcmp( drv + 0xC8, drv_after, 8 ) == 0 && std::memcmp( cam + 0x2E4, cam_after, 28 ) == 0;
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "human_explosion vector %u (function %u): %zu calls (%zu), amplitude 0x%X (retail 0x%X)", v, fn, log.size( ), glog.size( ),
                       contact_u32( cam, 0x2E4 ), cam_after[ 0 ] );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuHumanDriver's explosion listener 0x40F4F0 / 0x40F520 / 0x40F5A0 and slots 6 / 7 / 20 match retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "human_explosion FAILED %d of %u vectors", failed, nv );
    return false;
}

// VuAiWaypointEntity's plugs (0x4325D0 -> 0x432780, 0x432740, 0x432760) and the AI manager's slot 16 0x404220 against
// retail (oracle case waypoint_plugs): the waypoint's words, every sector's probability bits, the plan restarts.
inline bool verify_waypoint_plugs( )
{
    const unsigned* g = g_golden_waypoint_plugs_words;
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned fn = *g++, ns = *g++;
        track_manager_t tm;
        tm.sectors.resize( ns );
        for ( auto& s : tm.sectors )
            std::memcpy( s.image + 0xC8, g++, 4 );
        fleet_manager_t m;
        std::vector< fleet_pilot_t > pilots( *g++ );
        for ( std::size_t j = 0; j < pilots.size( ); ++j )
        {
            const unsigned n = *g++;
            for ( unsigned k = 0; k < n; ++k )
                pilots[ j ].plan.sectors.push_back( static_cast< int >( *g++ ) );
            fleet_entry_t e;
            e.pilot = static_cast< int >( j );
            m.entries.push_back( e );
        }
        track_waypoint_t w;
        w.sector_a = static_cast< std::int32_t >( *g++ );
        w.sector_b = static_cast< std::int32_t >( *g++ );
        std::memcpy( &w.p.branch_a, g++, 4 );
        std::memcpy( &w.p.branch_b, g++, 4 );
        w.p.allow_recovery = static_cast< std::uint8_t >( *g++ );
        std::uint8_t params[ 0x84 ];
        std::memcpy( params, g, 0x84 );
        g += 33;
        std::vector< int > direct( *g++ );
        for ( int& s : direct )
            s = static_cast< int >( *g++ );
        const unsigned a8b = *g++, aa = *g++, ab = *g++;
        const unsigned* aprobs = g;
        g += ns;
        const unsigned aret = *g++;
        std::vector< std::vector< unsigned > > glog( *g++ );
        for ( auto& e : glog )
        {
            e.assign( g, g + 2 );
            g += 2;
        }
        std::vector< std::vector< unsigned > > log;
        fleet_random_t rng;
        auto restart = [ & ]( int pilot, int sector ) { log.push_back( { static_cast< unsigned >( pilot ), static_cast< unsigned >( sector ) } ); };
        auto replan = [ & ]( const int ( &list )[ 2 ] ) { fleet_replan( m, pilots, tm, 2, list, rng, restart ); };
        unsigned ret = 0xCDCDCDCDu;
        bool known = true;
        if ( fn == 1 )
        {
            known = waypoint_update_branches( tm, w, params, replan );
            ret = 0;
        }
        else if ( fn == 2 || fn == 3 )
        {
            waypoint_allow_recovery( w, fn == 2 );
            ret = 0;
        }
        else
            fleet_replan( m, pilots, tm, static_cast< int >( direct.size( ) ), direct.data( ), rng, restart );
        unsigned mine_a, mine_b;
        std::memcpy( &mine_a, &w.p.branch_a, 4 );
        std::memcpy( &mine_b, &w.p.branch_b, 4 );
        bool same = known && log == glog && w.p.allow_recovery == a8b && mine_a == aa && mine_b == ab && ret == aret;
        for ( unsigned i = 0; i < ns; ++i )
            same = same && contact_u32( tm.sectors[ i ].image, 0xC8 ) == aprobs[ i ];
        if ( !same && ++failed <= 8 )
            LOG_ERROR( "waypoint_plugs vector %u (function %u): +0xA0 0x%X (retail 0x%X), +0xA4 0x%X (0x%X), %zu restarts (%zu)", v, fn, mine_a, aa, mine_b,
                       ab, log.size( ), glog.size( ) );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "VuAiWaypointEntity's plugs 0x4325D0 / 0x432780 / 0x432740 / 0x432760 and the AI manager's slot 16 0x404220 match retail: %u vectors", nv );
        return true;
    }
    LOG_ERROR( "waypoint_plugs FAILED %d of %u vectors", failed, nv );
    return false;
}

inline bool verify_race_all( )
{
    bool ok = true;
    ok = verify_boat_camera( ) && ok;
    ok = verify_boat_respawn( ) && ok;
    ok = verify_boat_anim( ) && ok;
    ok = verify_human_tick( ) && ok;
    ok = verify_ghost_tick( ) && ok;
    ok = verify_ui_tick( ) && ok;
    ok = verify_boat_drivers( ) && ok;
    ok = verify_nuisance_tick( ) && ok;
    ok = verify_driver_slots( ) && ok;
    ok = verify_boat_manager( ) && ok;
    ok = verify_viewport_tick( ) && ok;
    ok = verify_boost_apply( ) && ok;
    ok = verify_powerup_tick( ) && ok;
    ok = verify_mesh_assets( ) && ok;
    ok = verify_random_seed( ) && ok;
    ok = verify_powerup_contact( ) && ok;
    ok = verify_powerup_world( ) && ok;
    ok = verify_trigger_apply( ) && ok;
    ok = verify_powerup_db( ) && ok;
    ok = verify_surface_table( ) && ok;
    ok = verify_hidden_package( ) && ok;
    ok = verify_boat_manager_game( ) && ok;
    ok = verify_nuisance_start( ) && ok;
    ok = verify_nuisance_start_ctor( ) && ok;
    ok = verify_boat_events( ) && ok;
    ok = verify_body_impulse( ) && ok;
    ok = verify_piece_motion( ) && ok;
    ok = verify_boat_set_world( ) && ok;
    ok = verify_driver_dtors( ) && ok;
    ok = verify_fleet_small( ) && ok;
    ok = verify_boat_small( ) && ok;
    ok = verify_bt_leaves( ) && ok;
    ok = verify_thunk_build( ) && ok;
    ok = verify_contact_added( ) && ok;
    ok = verify_boat_rtti( ) && ok;
    ok = verify_human_explosion( ) && ok;
    ok = verify_waypoint_plugs( ) && ok;
    ok = verify_ray_test( ) && ok;
    ok = verify_ray_bvh( ) && ok;
    ok = verify_compound_pair( ) && ok;
    ok = verify_boat_pfx( ) && ok;
    ok = verify_boat_debris( ) && ok;
    ok = verify_piece_remove( ) && ok;
    ok = verify_hull_splash( ) && ok;
    ok = verify_debris_launch( ) && ok;
    ok = verify_bt_dtors( ) && ok;
    ok = verify_needs_collision( ) && ok;
    ok = verify_debris_follow( ) && ok;
    ok = verify_destroyed_event( ) && ok;
    ok = verify_bt_teardown( ) && ok;
    ok = verify_bt_misc( ) && ok;
    ok = verify_bt_rows( ) && ok;
    ok = verify_bt_rows2( ) && ok;
    ok = verify_bt_rows3( ) && ok;
    ok = verify_bt_rows4( ) && ok;
    ok = verify_bt_rows5( ) && ok;
    ok = verify_bt_rows6( ) && ok;
    ok = verify_bt_rows7( ) && ok;
    ok = verify_bt_rows8( ) && ok;
    ok = verify_bt_rows9( ) && ok;
    ok = verify_bt_rows10( ) && ok;
    ok = verify_bt_rows11( ) && ok;
    ok = verify_mesh_scaling( ) && ok;
    ok = verify_bvh_build( ) && ok;
    ok = verify_asset_rtti( ) && ok;
    ok = verify_asset_factory( ) && ok;
    ok = verify_package_filter( ) && ok;
    ok = verify_asset_package( ) && ok;
    ok = verify_asset_slots( ) && ok;
    ok = verify_input_manager( ) && ok;
    ok = verify_input_device( ) && ok;
    ok = verify_foliage( ) && ok;
    ok = verify_subtitle( ) && ok;
    ok = verify_asset_xml_load( ) && ok;
    ok = verify_asset_loads( ) && ok;
    ok = verify_powerup_dtors( ) && ok;
    ok = verify_asset_dtors( ) && ok;
    ok = verify_asset_creates( ) && ok;
    ok = verify_asset_dtors2( ) && ok;
    ok = verify_asset_props( ) && ok;
    ok = verify_asset_xml_dtor( ) && ok;
    ok = verify_asset_creates2( ) && ok;
    ok = verify_asset_dtors3( ) && ok;
    ok = verify_compound_remove( ) && ok;
    ok = verify_hull_scaling( ) && ok;
    ok = verify_simplex_ctor( ) && ok;
    ok = verify_thunk_exclude( ) && ok;
    ok = verify_boat_contact( ) && ok;
    ok = verify_piece_fluid( ) && ok;
    ok = verify_piece_dtor( ) && ok;
    return ok;
}
