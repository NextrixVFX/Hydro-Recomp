#pragma once

#include <workspace/core/world/pfxclass.hxx>
#include <workspace/core/world/pfxdraw.hxx>
#include <workspace/core/world/pfxsim.hxx>
#include <workspace/core/world/pfxload.hxx>
#include <workspace/core/world/pfxmgr.hxx>
#include <workspace/core/world/pfxstatic.hxx>

#include <cstring>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

// The water area's particle checks (included by checks/water.hxx before verify_water_all, which runs
// verify_pfx_all). Golden tables come from hydro-tools oracle/cases/water.py through emit/water.py.

// The fountain instances' slot 2 (world/pfxclass.hxx) against retail (case fountain_init): 0x55E100 quad,
// 0x55E9B0 directional quad, 0x55E640 geom, on random particles, params, system velocities and slot 5
// matrices, the global VuRand seeded per vector. Compared: the particle's 32 words after (but the one word
// each of 0x55E9B0 / 0x55E640 takes from an unwritten stack word: +0x24 / +0x50, UNKNOWN), the VuRand's 34
// words after, and the number of matrix fetches.
inline bool verify_fountain_init( )
{
    int failed = 0, words = 0;
    const unsigned* s = g_golden_fin_words;
    for ( std::size_t index = 0; index < g_golden_fin_count; ++index, s += 169 )
    {
        const unsigned kind = s[ 0 ];
        pfx_rand_t rand;
        rand.seed = static_cast< std::int32_t >( s[ 1 ] );
        rand.previous = static_cast< std::int32_t >( s[ 2 ] );
        for ( int k = 0; k < 32; ++k )
            rand.table[ k ] = static_cast< std::int32_t >( s[ 3 + k ] );
        std::uint8_t params[ 0x100 ]{};
        std::memcpy( params + 0x98, s + 35, 64 );
        float svel[ 3 ], matrix[ 16 ];
        std::memcpy( svel, s + 51, 12 );
        std::memcpy( matrix, s + 54, 64 );
        const unsigned* before = s + 70;
        const unsigned* after = s + 102;
        const unsigned* rng_after = s + 134;
        const unsigned fetches = s[ 168 ];
        pfx_quad_particle_t p;
        std::memcpy( p.w, before, sizeof( p.w ) );
        unsigned got_fetches = 0;
        if ( kind == 0 )
            pfx_quad_fountain_init( rand, params, p );
        else if ( kind == 1 )
            pfx_directional_fountain_init( rand, params, svel, matrix, p ), got_fetches = 1;
        else
            pfx_geom_fountain_init( rand, params, matrix, p ), got_fetches = 1;
        const int unknown = kind == 1 ? 0x24 / 4 : kind == 2 ? 0x50 / 4 : -1;
        int bad = 0;
        for ( int k = 0; k < 30; ++k )
        {
            if ( k == unknown )
                continue;
            ++words;
            if ( p.w[ k ] != after[ k ] )
            {
                if ( bad < 3 )
                    LOG_ERROR( "fountain_init vector %d kind %u word +0x%X: 0x%08X, retail 0x%08X", static_cast< int >( index ), kind, 4 * k,
                        p.w[ k ], after[ k ] );
                ++bad;
            }
        }
        // a particle is 30 words in this model; retail's words +0x78 / +0x7C stay as they were
        bad += ( after[ 30 ] != before[ 30 ] || after[ 31 ] != before[ 31 ] ) ? 1 : 0;
        const std::uint32_t r[ 34 ] = { static_cast< std::uint32_t >( rand.seed ), static_cast< std::uint32_t >( rand.previous ) };
        for ( int k = 0; k < 34; ++k )
        {
            const std::uint32_t v = k < 2 ? r[ k ] : static_cast< std::uint32_t >( rand.table[ k - 2 ] );
            ++words;
            bad += v != rng_after[ k ] ? 1 : 0;
        }
        bad += got_fetches != fetches ? 1 : 0;
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "fountain_init vector %d (kind %u): %d differences", static_cast< int >( index ), kind, bad );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "pfx_quad / directional / geom_fountain_init match the fountain instances' slot 2 0x55E100 / 0x55E9B0 / 0x55E640 "
                     "on %d vectors, %d words (particle + VuRand) exact",
            static_cast< int >( g_golden_fin_count ), words );
    else
        LOG_ERROR( "fountain_init FAILED %d of %d", failed, static_cast< int >( g_golden_fin_count ) );
    return failed == 0;
}

// The pattern instances' draw slots (world/pfxclass.hxx) against retail (case pattern_draw): geom 0x5903A0 / 0x5906A0,
// quad 0x5920B0, trail 0x58F9C0 with the model draws 0x520130 / 0x520670 and the renderers 0x58BD60 / 0x58D030 recorded.
// Rebuilt here from the recreation: every call in order - the model matrix's 16 words, the colour retail stored at
// params +0x74 and the +0x78 byte, the list element, the pass argument, the renderer and its arguments.
inline bool verify_pattern_draw( )
{
    int failed = 0, calls = 0;
    const unsigned* s = g_golden_pdr_words;
    float identity[ 16 ];
    std::memcpy( identity, s, 64 );
    s += 16;
    for ( std::size_t index = 0; index < g_golden_pdr_count; ++index )
    {
        const unsigned kind = s[ 0 ], n = s[ 1 ], space = s[ 2 ];
        float matrix[ 16 ], tint[ 4 ], scale;
        std::memcpy( matrix, s + 3, 64 );
        std::memcpy( &scale, s + 19, 4 );
        std::memcpy( tint, s + 20, 16 );
        const unsigned flag = s[ 24 ], asset = s[ 25 ], nel = s[ 26 ], arg = s[ 27 ];
        s += 28;
        std::vector< pfx_quad_particle_t > parts( n );
        for ( unsigned i = 0; i < n; ++i, s += 25 )
            std::memcpy( parts[ i ].w, s, 100 );
        std::vector< std::vector< unsigned > > want;
        const unsigned ncalls = *s++;
        for ( unsigned c = 0; c < ncalls; ++c )
        {
            want.emplace_back( s + 1, s + 1 + s[ 0 ] );
            s += 1 + s[ 0 ];
        }
        std::vector< std::vector< unsigned > > got;
        const float* d = space == 1u ? matrix : identity;
        auto words = [ & ]( std::vector< unsigned >& v, const float* m ) {
            for ( int k = 0; k < 16; ++k )
            {
                unsigned u;
                std::memcpy( &u, m + k, 4 );
                v.push_back( u );
            }
        };
        if ( kind == 0u || kind == 1u )
        {
            for ( const pfx_quad_particle_t& q : parts )
            {
                float m[ 16 ];
                pfx_geom_draw_matrix( q, d, scale, m );
                if ( kind == 0u )
                {
                    std::vector< unsigned > v{ 0u, 0x5Cu, arg };
                    words( v, m );
                    v.push_back( pfx_geom_draw_colour( q, tint ) );
                    v.push_back( flag );
                    got.push_back( v );
                }
                else
                {
                    for ( unsigned e = 0; asset != 0u && e < nel; ++e )
                    {
                        std::vector< unsigned > v{ 1u, 0x5Cu, 0x1001u + e, arg };   // the case's element k has the value 0x1001 + k
                        words( v, m );
                        got.push_back( v );
                    }
                }
            }
        }
        else if ( pfx_pattern_submits( n ) )
        {
            got.push_back( { kind, kind == 2u ? 0xCu : 0x10u, arg, 0u } );
        }
        calls += static_cast< int >( want.size( ) );
        if ( got != want )
        {
            ++failed;
            int call = -1, word = -1;
            for ( std::size_t c = 0; call < 0 && c < got.size( ) && c < want.size( ); ++c )
                for ( std::size_t k = 0; k < got[ c ].size( ) && k < want[ c ].size( ); ++k )
                    if ( got[ c ][ k ] != want[ c ][ k ] )
                    {
                        call = static_cast< int >( c ), word = static_cast< int >( k );
                        break;
                    }
            LOG_ERROR( "pattern_draw vector %d (kind %u): %d calls, retail %d; first difference call %d word %d", static_cast< int >( index ),
                kind, static_cast< int >( got.size( ) ), static_cast< int >( want.size( ) ), call, word );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "pfx_geom_draw_matrix / pfx_geom_draw_colour / pfx_pattern_submits match the draw slots 0x5903A0 / 0x5906A0 / "
                     "0x5920B0 / 0x58F9C0 on %d vectors, %d draw calls word for word",
            static_cast< int >( g_golden_pdr_count ), calls );
    else
        LOG_ERROR( "pattern_draw FAILED %d of %d", failed, static_cast< int >( g_golden_pdr_count ) );
    return failed == 0;
}

// The VuPfx pools and the system lifecycle (world/pfxsim.hxx) against retail's memory (case pfx_lifecycle): the
// resources' ctor + init, then a plan of createSystemInstance / start / allocateParticle / tick / stop / release /
// VuPfxImpl tick on hand-built node trees (real node vtables); the pattern ticks are the case's stubs, replayed. After
// the init and after every operation: the 25 resource words and every block word (but the system AABB pads +0x8C /
// +0x9C and the pattern AABB pads +0x38 / +0x48, stack words), every return, every pattern tick's pattern and dt.
struct pfx_check_sim_t
{
    std::map< std::uint32_t, std::vector< std::uint32_t > > kids;
    const unsigned* ticks = nullptr;
    int nticks = 0, bad = 0;
};

inline bool verify_pfx_lifecycle( )
{
    const unsigned* s = g_golden_plc_words;
    const std::uint32_t res = s[ 0 ];
    const std::uint32_t blocks[ 4 ] = { s[ 1 ], s[ 2 ], s[ 3 ], s[ 4 ] };
    std::uint32_t pairs[ 8 ];
    std::memcpy( pairs, s + 5, 32 );
    s += 13 + 16;
    for ( int k = 1; k < 8; k += 2 )
        pairs[ k ] = ( pairs[ k ] + 15u ) & 0xFFFFFFF0u;   // the block sizes as 0x58B9A0 rounds them (the regions)
    pfx_heap_t h;
    h.add( res, 0x64 );
    for ( int k = 0; k < 4; ++k )
        h.add( blocks[ k ], pairs[ 2 * k ] * pairs[ 2 * k + 1 ] );
    pfx_check_sim_t sim;
    const unsigned nnodes = *s++;
    std::vector< std::uint32_t > addr;
    const unsigned* ns = s;
    for ( unsigned i = 0; i < nnodes; ++i )
    {
        addr.push_back( ns[ 0 ] );
        ns += 5 + ns[ 4 ];
    }
    h.add( addr[ 0 ], 0x80u * nnodes );
    for ( unsigned i = 0; i < nnodes; ++i )
    {
        h[ s[ 0 ] ] = s[ 1 ];
        h[ s[ 0 ] + 0x30 ] = s[ 2 ];
        h[ s[ 0 ] + 0x34 ] = s[ 3 ];
        std::vector< std::uint32_t >& v = sim.kids[ s[ 0 ] ];
        for ( unsigned k = 0; k < s[ 4 ]; ++k )
            v.push_back( addr[ s[ 5 + k ] ] );
        s += 5 + s[ 4 ];
    }
    pfx_sim_hooks_t hooks;
    hooks.user = &sim;
    hooks.children = []( void* u, std::uint32_t node ) -> const std::vector< std::uint32_t >* {
        auto& m = static_cast< pfx_check_sim_t* >( u )->kids;
        auto it = m.find( node );
        return it == m.end( ) ? nullptr : &it->second;
    };
    hooks.pattern_tick = []( void* u, pfx_heap_t& hp, std::uint32_t pattern, float dt ) {
        pfx_check_sim_t& c = *static_cast< pfx_check_sim_t* >( u );
        if ( c.nticks <= 0 )
        {
            ++c.bad;
            return;
        }
        std::uint32_t d;
        std::memcpy( &d, &dt, 4 );
        c.bad += ( c.ticks[ 0 ] != pattern || c.ticks[ 1 ] != d ) ? 1 : 0;
        hp[ pattern + 0x28 ] = c.ticks[ 2 ];
        for ( int k = 0; k < 3; ++k )
        {
            hp[ pattern + 0x2C + 4 * k ] = c.ticks[ 3 + k ];
            hp[ pattern + 0x3C + 4 * k ] = c.ticks[ 6 + k ];
        }
        c.ticks += 9;
        --c.nticks;
    };
    const std::uint32_t sizes[ 4 ] = { pairs[ 0 ] * pairs[ 1 ], pairs[ 2 ] * pairs[ 3 ], pairs[ 4 ] * pairs[ 5 ], pairs[ 6 ] * pairs[ 7 ] };
    int failed = 0, compared = 0;
    auto compare = [ & ]( const unsigned* want, int step ) {
        int bad = 0;
        for ( int k = 0; k < 25; ++k, ++compared )
            if ( h[ res + 4 * k ] != want[ k ] )
            {
                if ( bad < 2 )
                    LOG_ERROR( "pfx_lifecycle step %d: resources +0x%X = 0x%08X, retail 0x%08X", step, 4 * k, h[ res + 4 * k ], want[ k ] );
                ++bad;
            }
        want += 25;
        for ( int b = 0; b < 4; ++b )
            for ( std::uint32_t o = 0; o < sizes[ b ]; o += 4, ++want )
            {
                if ( b == 0 && ( o % pairs[ 1 ] == 0x8Cu || o % pairs[ 1 ] == 0x9Cu ) )
                    continue;
                if ( b == 1 && ( o % pairs[ 3 ] == 0x38u || o % pairs[ 3 ] == 0x48u ) )   // 0x58B240 stores stack words there
                    continue;
                ++compared;
                if ( h[ blocks[ b ] + o ] != *want )
                {
                    if ( bad < 2 )
                        LOG_ERROR( "pfx_lifecycle step %d: block %d +0x%X = 0x%08X, retail 0x%08X", step, b, o, h[ blocks[ b ] + o ], *want );
                    ++bad;
                }
            }
        return bad;
    };
    const std::size_t region = 25 + ( sizes[ 0 ] + sizes[ 1 ] + sizes[ 2 ] + sizes[ 3 ] ) / 4;
    pfx_resources_ctor( h, res );
    std::uint32_t authored[ 8 ];
    std::memcpy( authored, g_golden_plc_words + 5, 32 );
    pfx_resources_init( h, res, authored, blocks );
    failed += compare( s, -1 ) ? 1 : 0;
    s += region;
    const unsigned nops = *s++;
    for ( unsigned i = 0; i < nops; ++i )
    {
        const unsigned code = *s++;
        int bad = 0;
        switch ( code )
        {
        case 0:
            bad += pfx_system_instance_create( h, res, addr[ s[ 0 ] ], hooks ) != s[ 1 ] ? 1 : 0;
            s += 2;
            break;
        case 1: pfx_system_start( h, s[ 0 ] ); s += 1; break;
        case 2:
            for ( unsigned k = 0; k < s[ 1 ]; ++k )
                bad += pfx_particle_allocate( h, res, s[ 0 ] ) != s[ 2 + k ] ? 1 : 0;
            s += 2 + s[ 1 ];
            break;
        case 3:
        {
            float dt;
            std::memcpy( &dt, s + 1, 4 );
            sim.ticks = s + 3;
            sim.nticks = static_cast< int >( s[ 2 ] );
            sim.bad = 0;
            pfx_system_tick( h, s[ 0 ], dt, hooks );
            bad += sim.bad + sim.nticks;
            s += 3 + 9 * s[ 2 ];
            break;
        }
        case 4: pfx_system_stop( h, res, s[ 0 ], s[ 1 ] != 0u ); s += 2; break;
        case 5: pfx_system_release( h, res, s[ 0 ] ); s += 1; break;
        default: pfx_impl_tick( h, res ); break;
        }
        bad += compare( s, static_cast< int >( i ) );
        s += region;
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "pfx_lifecycle step %d (operation %u): %d differences", static_cast< int >( i ), code, bad );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuPfxResources 0x58B5B0 / 0x58B670 and the system lifecycle (0x51DA70 / 0x58B880 / 0x58A940 / 0x58B2D0 / 0x58AC90 / "
                     "0x58B4E0 / 0x58AD70 / 0x58ACC0 / 0x51DAE0 / 0x58AA70 / 0x58B3F0 / 0x51DD80) match retail's memory over %d operations, "
                     "%d words",
            static_cast< int >( nops ), compared );
    else
        LOG_ERROR( "pfx_lifecycle FAILED %d of %d steps", failed, static_cast< int >( nops ) + 1 );
    return failed == 0;
}

// VuPfx systems END TO END (world/pfxsim.hxx: pfx_system_tick with pfx_heap_pattern_tick, every process's slot 1 by
// its vtable, the fountains' emit + slot 2 on the heap) against retail running its own per-frame code (case pfx_run):
// after the init and every operation, the 25 resource words, every block word (but the stack-word pads: system +0x8C /
// +0x9C, pattern +0x38 / +0x48, and a directional fountain particle's +0x24 / a geom fountain particle's +0x50 - here
// every particle's +0x24 and +0x50 are skipped, as the case cannot tell which fountain made a particle) and the
// VuRand's 34 words; no unrecreated path may be reached.
//
// With `boats` (case pfx_boats): the boat processes on the heap - the frame's boats / fluids from the table, the binds
// (operation 8: pfx_bind_thrust / pfx_bind_hull / pfx_bind_spring) and every operation's recorded hook calls replayed in
// order (prepare: the fluids and the matrix compared; the one-point query: the point compared, the answer handed back;
// the snap: the point compared, the height handed back) - a call out of order or left over is a difference. Every
// particle's +0x14 is skipped too: a hull particle from a picked sample takes retail's stale stack word there (spray.hxx).
struct pfx_check_run_t
{
    std::map< std::uint32_t, std::vector< std::uint32_t > > kids;
    pfx_frame_t frame;
    std::uint32_t r = 0;
    // the boats
    std::vector< pfx_frame_boat_t > boats;
    std::vector< pfx_frame_fluids_t > fluids;
    std::vector< std::unique_ptr< fluids_mesh_t > > meshes;
    std::vector< std::unique_ptr< fluid_solver_t > > solvers;
    std::vector< std::unique_ptr< solver_rigid_t > > bodies;
    std::vector< std::unique_ptr< boat_view_t > > views;
    std::vector< std::unique_ptr< pfx_hull_boat_t > > hulls;
    std::vector< std::vector< std::uint8_t > > records;
    const unsigned* calls = nullptr;
    unsigned left = 0;
    int bad = 0;
    int made[ 4 ]{};
    const unsigned* next( unsigned type )
    {
        if ( left == 0u || calls[ 1 ] != type )
        {
            ++bad;
            return nullptr;
        }
        const unsigned* c = calls + 2;
        ++made[ type & 3u ];
        calls += 1 + calls[ 0 ];
        --left;
        return c;
    }
};

// The boat table (g_golden_pbt_boats) into the frame's entries.
inline void pfx_check_boats( pfx_check_run_t& run, const unsigned* b, std::size_t count )
{
    for ( std::size_t k = 0; k < count; ++k )
    {
        pfx_frame_boat_t e;
        e.address = b[ 0 ];
        const std::uint32_t fluids = b[ 1 ];
        run.bodies.push_back( std::make_unique< solver_rigid_t >( ) );
        solver_rigid_t& body = *run.bodies.back( );
        const unsigned* m = b + 13;   // the matrix
        const unsigned* bodies = b + 13 + 16 + 4 + 2 + 16;
        for ( int i = 0; i < 4; ++i )
            body.linear_velocity[ i ] = water_bits( bodies[ 4 + i ] );
        run.views.push_back( std::make_unique< boat_view_t >( boat_view_t{ water_bits( b[ 2 ] ), b[ 3 ], static_cast< std::uint8_t >( b[ 4 ] ),
            static_cast< std::uint8_t >( b[ 5 ] ), { }, 0, 0, 0, body } ) );
        boat_view_t& v = *run.views.back( );
        for ( int i = 0; i < 3; ++i )
            v.o50_60[ i ] = water_bits( m[ 4 + i ] );
        v.o22C_04 = water_bits( b[ 6 ] );
        v.o22C_0C = water_bits( b[ 7 ] );
        v.o22C0_470 = water_bits( b[ 8 ] );
        run.records.emplace_back( 32, std::uint8_t( 0 ) );
        for ( int i = 0; i < 4; ++i )
            run.records.back( )[ 8 * i ] = static_cast< std::uint8_t >( b[ 9 + i ] );
        e.view = &v;
        e.records = run.records.back( ).data( );
        for ( int i = 0; i < 3; ++i )
        {
            e.position[ i ] = water_bits( m[ 12 + i ] );
            e.velocity[ i ] = water_bits( bodies[ 4 + i ] );
        }
        run.hulls.push_back( std::make_unique< pfx_hull_boat_t >( ) );
        pfx_hull_boat_t& h = *run.hulls.back( );
        std::memcpy( h.transform, m, 64 );
        std::memcpy( h.offset, m + 16, 16 );
        h.part_flag = static_cast< std::uint8_t >( m[ 20 ] );
        h.part_count = static_cast< std::int32_t >( m[ 21 ] );
        std::memcpy( h.part_matrix, m + 22, 64 );
        pfx_hull_body_t* hb[ 2 ] = { &h.body, &h.part_body };
        for ( int j = 0; j < 2; ++j )
        {
            std::memcpy( hb[ j ]->position, bodies + 12 * j, 12 );
            std::memcpy( hb[ j ]->linear, bodies + 12 * j + 4, 12 );
            std::memcpy( hb[ j ]->angular, bodies + 12 * j + 8, 12 );
        }
        e.hull = &h;
        run.boats.push_back( e );
        const unsigned* t = bodies + 24;
        const unsigned ntri = t[ 0 ], nedge = t[ 1 ];
        run.meshes.push_back( std::make_unique< fluids_mesh_t >( ) );
        run.meshes.back( )->triangles.assign( t + 2, t + 2 + 15 * ntri );
        run.solvers.push_back( std::make_unique< fluid_solver_t >( ) );
        fluid_solver_t& sv = *run.solvers.back( );
        sv.mesh = run.meshes.back( ).get( );
        sv.edges.resize( nedge );
        std::memcpy( sv.edges.data( ), t + 2 + 15 * ntri, 40u * nedge );
        run.fluids.push_back( { fluids, &sv } );
        b = t + 2 + 15 * ntri + 10 * nedge;
    }
    run.frame.boats = run.boats.data( );
    run.frame.boat_count = static_cast< int >( run.boats.size( ) );
    run.frame.fluids = run.fluids.data( );
    run.frame.fluids_count = static_cast< int >( run.fluids.size( ) );
    run.frame.user = &run;
    run.frame.prepare = []( void* u, fluid_solver_t& solver, const float* matrix ) {
        pfx_check_run_t& c = *static_cast< pfx_check_run_t* >( u );
        const unsigned* r = c.next( 1u );
        if ( r == nullptr )
            return;
        if ( r[ 0 ] >= c.solvers.size( ) || c.solvers[ r[ 0 ] ].get( ) != &solver || std::memcmp( r + 1, matrix, 64 ) != 0 )
            ++c.bad;
    };
    run.frame.point = []( void* u, const float* point, float* out ) {
        pfx_check_run_t& c = *static_cast< pfx_check_run_t* >( u );
        const unsigned* r = c.next( 2u );
        if ( r == nullptr )
            return false;
        if ( std::memcmp( r, point, 12 ) != 0 )
            ++c.bad;
        std::memcpy( out, r + 3, 32 );
        return true;
    };
    run.frame.water = true;
    run.frame.snap = []( void* u, const float* point, water_vertex_t& out ) {
        pfx_check_run_t& c = *static_cast< pfx_check_run_t* >( u );
        const unsigned* r = c.next( 3u );
        if ( r == nullptr )
            return false;
        if ( std::memcmp( r, point, 12 ) != 0 )   // the w word is the particle's +0x14 (skipped above); the height never reads it
            ++c.bad;
        std::memcpy( &out.height, r + 4, 4 );
        return true;
    };
}

inline bool pfx_check_run( const unsigned* s, const char* tag, const unsigned* boats = nullptr, std::size_t boat_count = 0, bool calls = false )
{
    const std::uint32_t res = s[ 0 ];
    const std::uint32_t blocks[ 4 ] = { s[ 1 ], s[ 2 ], s[ 3 ], s[ 4 ] };
    std::uint32_t authored[ 8 ], pairs[ 8 ];
    std::memcpy( authored, s + 5, 32 );
    std::memcpy( pairs, s + 5, 32 );
    for ( int k = 1; k < 8; k += 2 )
        pairs[ k ] = ( pairs[ k ] + 15u ) & 0xFFFFFFF0u;
    pfx_check_run_t run;
    run.r = res;
    if ( boats != nullptr )
        pfx_check_boats( run, boats, boat_count );
    const unsigned* box = s + 13;
    for ( int k = 0; k < 3; ++k )
    {
        std::memcpy( &run.frame.geom_box[ k ], box + k, 4 );
        std::memcpy( &run.frame.geom_box[ 3 + k ], box + 4 + k, 4 );
    }
    float camera[ 3 ];
    std::memcpy( camera, s + 21, 12 );
    pfx_rand_t rand;
    rand.seed = static_cast< std::int32_t >( s[ 24 ] );
    rand.previous = static_cast< std::int32_t >( s[ 25 ] );
    for ( int k = 0; k < 32; ++k )
        rand.table[ k ] = static_cast< std::int32_t >( s[ 26 + k ] );
    run.frame.cameras = camera;
    run.frame.camera_count = 1;
    run.frame.rand = &rand;
    s += 58;
    pfx_heap_t h;
    h.add( res, 0x64 );
    for ( int k = 0; k < 4; ++k )
        h.add( blocks[ k ], pairs[ 2 * k ] * pairs[ 2 * k + 1 ] );
    const unsigned nnodes = *s++;
    std::vector< std::uint32_t > addr;
    for ( const unsigned* ns = s; addr.size( ) < nnodes; )
    {
        addr.push_back( ns[ 0 ] );
        const unsigned nw = ns[ 2 ];
        ns += 4 + 2 * nw + ns[ 3 + 2 * nw ];
    }
    h.add( addr[ 0 ], 0x100u * nnodes );
    for ( unsigned i = 0; i < nnodes; ++i )
    {
        const std::uint32_t a = s[ 0 ];
        h[ a ] = s[ 1 ];
        const unsigned nw = s[ 2 ];
        for ( unsigned k = 0; k < nw; ++k )
            h[ a + s[ 3 + 2 * k ] ] = s[ 4 + 2 * k ];
        const unsigned nk = s[ 3 + 2 * nw ];
        std::vector< std::uint32_t >& v = run.kids[ a ];
        for ( unsigned k = 0; k < nk; ++k )
            v.push_back( addr[ s[ 4 + 2 * nw + k ] ] );
        s += 4 + 2 * nw + nk;
    }
    pfx_sim_hooks_t hooks;
    hooks.user = &run;
    hooks.children = []( void* u, std::uint32_t node ) -> const std::vector< std::uint32_t >* {
        auto& m = static_cast< pfx_check_run_t* >( u )->kids;
        auto it = m.find( node );
        return it == m.end( ) ? nullptr : &it->second;
    };
    hooks.pattern_tick = []( void* u, pfx_heap_t& hp, std::uint32_t pattern, float dt ) {
        pfx_check_run_t& c = *static_cast< pfx_check_run_t* >( u );
        pfx_heap_pattern_tick( hp, c.r, pattern, dt, c.frame );
    };
    const std::uint32_t sizes[ 4 ] = { pairs[ 0 ] * pairs[ 1 ], pairs[ 2 ] * pairs[ 3 ], pairs[ 4 ] * pairs[ 5 ], pairs[ 6 ] * pairs[ 7 ] };
    int compared = 0;
    auto compare = [ & ]( const unsigned* want, int step ) {
        int bad = 0;
        auto one = [ & ]( std::uint32_t got, std::uint32_t w, const char* what, std::uint32_t o ) {
            ++compared;
            if ( got == w )
                return;
            static std::map< const char*, int > logged;
            if ( logged[ what ]++ < 3 )
                LOG_ERROR( "%s step %d: %s +0x%X = 0x%08X, retail 0x%08X", tag, step, what, o, got, w );
            ++bad;
        };
        for ( std::uint32_t k = 0; k < 25; ++k )
            one( h[ res + 4 * k ], *want++, "resources", 4 * k );
        static const char* const names[ 4 ] = { "systems", "patterns", "processes", "particles" };
        for ( int b = 0; b < 4; ++b )
            for ( std::uint32_t o = 0; o < sizes[ b ]; o += 4, ++want )
            {
                const std::uint32_t in = o % pairs[ 2 * b + 1 ];
                if ( ( b == 0 && ( in == 0x8Cu || in == 0x9Cu || ( calls && in == 0x78u ) ) ) || ( b == 1 && ( in == 0x38u || in == 0x48u ) ) ||
                     ( b == 3 && ( in == 0x24u || in == 0x50u || ( boats != nullptr && in == 0x14u ) ) ) )
                    continue;
                one( h[ blocks[ b ] + o ], *want, names[ b ], o );
            }
        one( static_cast< std::uint32_t >( rand.seed ), *want++, "VuRand", 0 );
        one( static_cast< std::uint32_t >( rand.previous ), *want++, "VuRand", 4 );
        for ( int k = 0; k < 32; ++k )
            one( static_cast< std::uint32_t >( rand.table[ k ] ), *want++, "VuRand", 8 + 4 * k );
        return bad;
    };
    const std::size_t region = 25 + ( sizes[ 0 ] + sizes[ 1 ] + sizes[ 2 ] + sizes[ 3 ] ) / 4 + 34;
    pfx_resources_ctor( h, res );
    pfx_resources_init( h, res, authored, blocks );
    int failed = compare( s, -1 ) ? 1 : 0;
    s += region;
    const unsigned nops = *s++;
    int particles = 0;
    for ( unsigned i = 0; i < nops; ++i )
    {
        const unsigned code = *s++;
        int bad = 0;
        if ( boats != nullptr )
        {
            static const unsigned operands[ 9 ] = { 2, 1, 0, 2, 2, 1, 0, 7, 3 };
            const unsigned* cl = s + operands[ code < 9u ? code : 6u ];
            run.left = cl[ 0 ];
            run.calls = cl + 1;
        }
        switch ( code )
        {
        case 0: bad += pfx_system_instance_create( h, res, addr[ s[ 0 ] ], hooks ) != s[ 1 ] ? 1 : 0; s += 2; break;
        case 1: pfx_instance_start( h, s[ 0 ] ); s += 1; break;
        case 3:
        {
            float dt;
            std::memcpy( &dt, s + 1, 4 );
            pfx_instance_tick( h, s[ 0 ], dt, hooks );
            s += 2;
            break;
        }
        case 4: pfx_instance_stop( h, res, s[ 0 ], s[ 1 ] != 0u ); s += 2; break;
        case 5: pfx_system_release( h, res, s[ 0 ] ); s += 1; break;
        case 9:
        case 10:
        case 12:
        {
            float v[ 16 ];
            std::memcpy( v, s + 1, code == 9u ? 64 : 16 );
            if ( code == 9u )
                pfx_instance_set_matrix( h, s[ 0 ], v );
            else if ( code == 10u )
                pfx_instance_set_velocity( h, s[ 0 ], v );
            else
                pfx_instance_set_colour( h, s[ 0 ], v );
            s += code == 9u ? 17 : 5;
            break;
        }
        case 11: pfx_instance_set_scale( h, s[ 0 ], water_bits( s[ 1 ] ) ); s += 2; break;
        case 13:
            bad += reinterpret_cast< const std::uint32_t* >( pfx_instance_aabb( h, s[ 0 ] ) ) != h.find( s[ 0 ] + s[ 1 ] ) ? 1 : 0;
            s += 2;
            break;
        case 7:
            for ( int k = 0; k < 3; ++k )
            {
                h[ s[ 0 ] + 0x4C + 4 * k ] = s[ 1 + k ];
                h[ s[ 0 ] + 0x5C + 4 * k ] = s[ 4 + k ];
            }
            s += 7;
            break;
        case 8:
        {
            const int bi = static_cast< int >( s[ 2 ] );
            const std::uint32_t boat = bi >= 0 ? run.boats[ static_cast< std::size_t >( bi ) ].address : 0u;
            const std::uint32_t fluids = bi >= 0 ? run.fluids[ static_cast< std::size_t >( bi ) ].address : 0u;
            if ( s[ 1 ] == 0u )
                pfx_bind_thrust( h, s[ 0 ], boat );
            else if ( s[ 1 ] == 1u )
                pfx_bind_hull( h, s[ 0 ], boat, fluids );
            else
                pfx_bind_spring( h, s[ 0 ], boat );
            s += 3;
            break;
        }
        default: pfx_impl_tick( h, res ); break;
        }
        if ( boats != nullptr )
        {
            bad += run.bad + ( run.left != 0u ? 1 : 0 );   // the operation's calls all consumed, none differing
            if ( run.bad != 0 || run.left != 0u )
                LOG_ERROR( "%s step %d: %d hook calls differ, %u not made (next type %u)", tag, static_cast< int >( i ), run.bad, run.left,
                    run.left != 0u ? run.calls[ 1 ] : 0u );
            run.bad = 0;
            const unsigned ncalls = *s++;
            for ( unsigned c = 0; c < ncalls; ++c )
                s += 1 + s[ 0 ];
        }
        bad += compare( s, static_cast< int >( i ) );
        const int live = static_cast< int >( pairs[ 6 ] - h[ res + 0x3C ] );
        particles = live > particles ? live : particles;
        s += region;
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "%s step %d (operation %u): %d differences", tag, static_cast< int >( i ), code, bad );
        }
    }
    if ( run.frame.unrecreated != 0 )
    {
        ++failed;
        LOG_ERROR( "%s reached %d unrecreated paths", tag, run.frame.unrecreated );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "%s: VuPfx systems end to end (create, start, the real pattern / fountain / tick-process frames, stop, release) "
                     "match retail's memory over %d operations (up to %d live particles), %d words",
            tag, static_cast< int >( nops ), particles, compared );
    else
        LOG_ERROR( "%s FAILED %d of %d steps", tag, failed, static_cast< int >( nops ) + 1 );
    if ( failed == 0 && boats != nullptr )
        LOG_INFO( "%s: the boat hooks replayed in order - %d VuFluidSolver_prepare, %d one-point queries, %d water snaps", tag, run.made[ 1 ],
            run.made[ 2 ], run.made[ 3 ] );
    return failed == 0;
}

inline bool verify_pfx_run( ) { return pfx_check_run( g_golden_prn_words, "pfx_run" ); }

// The trail pattern's tick 0x58F0C0 (pfxsim.hxx pfx_heap_trail_tick) in the same end-to-end run (case pfx_trail: pfx_run's
// harness with trail patterns and their generic processes among the quad / geom ones).
inline bool verify_pfx_trail( ) { return pfx_check_run( g_golden_ptr_words, "pfx_trail" ); }

// The boat processes on the heap (pfxsim.hxx pfx_process_tick: thrust 0x4CD5B0, hull 0x4CE760, boat spring 0x4CD980, the
// fountains' water snap; the binds 0x41A69B / 0x41A083 / 0x43BFD6) in the same end-to-end run (case pfx_boats).
inline bool verify_pfx_boats( ) { return pfx_check_run( g_golden_pbt_words, "pfx_boats", g_golden_pbt_boats, g_golden_pbt_boat_count ); }

// pfxsim.hxx's instance calls (pfx_instance_*: slots 2 / 3 / 4 / 5 / 7 / 11 / 12 / 13 - the boat's emitter lists' and the
// splash's) against every call made through VuPfxSystemInstance's vtable 0x74FF8C in the same end-to-end run (case
// pfx_calls; system +0x78, slot 2's unwritten fourth euler lane, skipped; the vtable's 17 entries compared too).
inline bool verify_pfx_calls( )
{
    static const std::uint32_t slots[ 17 ] = { 0x58ABF0u, 0x58AC10u, 0x58AB00u, 0x58A650u, 0x58A670u, 0x58A690u, 0x58A6C0u, 0x58A6D0u, 0x58A6E0u,
        0x58A6F0u, 0x58A700u, 0x58AC90u, 0x58ACC0u, 0x58AD70u, 0x58AF60u, 0x58AFA0u, 0x401DA0u };
    if ( std::memcmp( slots, g_golden_psv_slots, sizeof( slots ) ) != 0 )
    {
        LOG_ERROR( "pfx_calls: VuPfxSystemInstance's vtable 0x74FF8C differs from the slots pfxsim.hxx / pfxclass.hxx recreate" );
        return false;
    }
    return pfx_check_run( g_golden_psv_words, "pfx_calls", nullptr, 0, true );
}

// The water classes' getType slots (world/surface.hxx g_water_types) against retail (case water_types): per slot the
// descriptor it returns and the chain of names its descriptor walks to.
inline bool verify_water_types( )
{
    int bad = 0;
    const std::size_t n = sizeof( g_water_types ) / sizeof( g_water_types[ 0 ] );
    for ( std::size_t i = 0; i < g_golden_wty_count; ++i )
    {
        const golden_water_type_t& g = g_golden_wty[ i ];
        const water_type_t* t = nullptr;
        for ( std::size_t k = 0; k < n; ++k )
            if ( g_water_types[ k ].address == g.address )
                t = &g_water_types[ k ];
        if ( t == nullptr || t->descriptor != g.descriptor || std::strcmp( t->chain, g.chain ) != 0 )
        {
            ++bad;
            LOG_ERROR( "water_types 0x%X: descriptor / chain differ (retail 0x%X %s)", g.address, g.descriptor, g.chain );
        }
    }
    bad += n != g_golden_wty_count ? 1 : 0;
    if ( bad == 0 )
        LOG_SUCCESS( "g_water_types match the water classes' %d getType slots (descriptor and chain each)", static_cast< int >( n ) );
    else
        LOG_ERROR( "water_types FAILED %d", bad );
    return bad == 0;
}

// The effect-asset load (world/pfxload.hxx) against retail's own (case pfx_load): the race's VuPfxAsset documents
// (Hydroelectric, Generic) read from Base.apf and loaded - every node depth first in map order (path, node vtable,
// refcount +4, +8, child count +0x10, the words +0x30 .. size with the std::string members and the VuAssetProperty
// object pointers - quad +0x98 / +0x9C, trail +0x8C, retail heap addresses - zeroed on both sides; the strings
// compared as text), the texture / model hook calls in order, and 0x51DBD0 on every node path plus misses.
inline void pfx_load_rows( const pfx_runtime_t& rt, const pfx_library_t& lib, std::uint32_t node, const std::string& path,
    std::vector< std::pair< std::string, std::uint32_t > >& out )
{
    out.emplace_back( path, node );
    auto k = lib.kids.find( node );
    if ( k != lib.kids.end( ) )
        for ( const auto& c : k->second )
            pfx_load_rows( rt, lib, c.second, path + "/" + c.first, out );
}

inline bool verify_pfx_load( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "pfx_load: Base.apf did not open" );
        return false;
    }
    static pfx_runtime_t rt;
    pfx_runtime_init( rt );
    pfx_library_t lib;
    struct hook_t
    {
        int project, kind;
        std::uint32_t node, offset;
        std::string text;
    };
    struct log_t
    {
        int project = 0;
        std::vector< hook_t > calls;
    } log;
    pfx_load_hooks_t hooks;
    hooks.user = &log;
    hooks.texture = []( void* u, std::uint32_t node, std::uint32_t offset, const std::string& name ) {
        log_t& l = *static_cast< log_t* >( u );
        l.calls.push_back( { l.project, 0, node, offset, name } );
    };
    hooks.model = []( void* u, std::uint32_t node, const std::string& name ) {
        log_t& l = *static_cast< log_t* >( u );
        l.calls.push_back( { l.project, 1, node, 0u, name } );
    };
    int failed = 0, words = 0;
    for ( std::size_t p = 0; p < g_golden_pld_project_count; ++p )
    {
        std::vector< std::uint8_t > blob;
        log.project = static_cast< int >( p );
        if ( !archive.read( g_golden_pld_projects[ p ].asset, blob ) ||
             pfx_load_asset( rt, lib, g_golden_pld_projects[ p ].name, blob, hooks ) == 0u )
        {
            LOG_ERROR( "pfx_load: %s did not load", g_golden_pld_projects[ p ].asset );
            return false;
        }
    }
    std::vector< std::pair< std::string, std::uint32_t > > rows;
    for ( const auto& pr : lib.projects )
        pfx_load_rows( rt, lib, pr.second, pr.first, rows );
    std::map< std::uint32_t, std::string > path_of;
    for ( const auto& r : rows )
        path_of[ r.second ] = r.first;
    if ( rows.size( ) != g_golden_pld_node_count )
    {
        LOG_ERROR( "pfx_load: %d nodes, retail %d", static_cast< int >( rows.size( ) ), static_cast< int >( g_golden_pld_node_count ) );
        return false;
    }
    std::map< int, std::map< std::uint32_t, std::string > > texts;
    for ( std::size_t i = 0; i < g_golden_pld_text_count; ++i )
        texts[ g_golden_pld_texts[ i ].row ][ g_golden_pld_texts[ i ].offset ] = g_golden_pld_texts[ i ].text;
    for ( std::size_t i = 0; i < rows.size( ); ++i )
    {
        const golden_pfx_node_t& g = g_golden_pld_nodes[ i ];
        const std::uint32_t a = rows[ i ].second;
        pfx_heap_t& h = rt.heap;
        int bad = rows[ i ].first != g.path || h[ a ] != g.vtable || h[ a + 4 ] != g.w4 || h[ a + 8 ] != g.w8 || h[ a + 0x10 ] != g.count;
        const pfx_node_type_t* t = pfx_node_type_of( h[ a ] );
        std::vector< std::uint32_t > masked, unknown;
        if ( t != nullptr )
            for ( int k = 0; k < t->prop_count; ++k )
            {
                if ( t->props[ k ].kind == pfx_prop_name || t->props[ k ].kind == pfx_prop_texture )
                    for ( std::uint32_t w = 0; w < 6; ++w )
                        masked.push_back( t->props[ k ].offset + 4u * w );
                if ( t->props[ k ].kind == pfx_prop_vec3 || t->props[ k ].kind == pfx_prop_rot3 )
                    unknown.push_back( t->props[ k ].offset + 12u );   // UNKNOWN: the stored w lane (pfxload.hxx)
            }
        if ( h[ a ] == 0x7505ACu )
            masked.insert( masked.end( ), { 0x98u, 0x9Cu } );
        if ( h[ a ] == 0x750294u )
            masked.push_back( 0x8Cu );
        for ( unsigned w = 0; w < g.nwords; ++w )
        {
            const std::uint32_t off = 0x30u + 4u * w;
            if ( std::find( unknown.begin( ), unknown.end( ), off ) != unknown.end( ) )
                continue;
            const std::uint32_t ours = std::find( masked.begin( ), masked.end( ), off ) != masked.end( ) ? 0u : h[ a + off ];
            bad += ours != g_golden_pld_words[ g.first + w ];
            ++words;
        }
        const auto& want = texts[ static_cast< int >( i ) ];
        for ( const auto& tx : want )
        {
            auto s = lib.strings.find( { a, tx.first } );
            bad += s == lib.strings.end( ) || s->second != tx.second;
        }
        if ( bad )
        {
            if ( failed < 6 )
                LOG_ERROR( "pfx_load: node %s differs from retail's load (%d)", g.path, bad );
            ++failed;
        }
    }
    int hook_bad = log.calls.size( ) != g_golden_pld_hook_count;
    for ( std::size_t i = 0; i < g_golden_pld_hook_count && i < log.calls.size( ); ++i )
    {
        const golden_pfx_hook_t& g = g_golden_pld_hooks[ i ];
        const hook_t& c = log.calls[ i ];
        hook_bad += c.project != g.project || c.kind != g.kind || path_of[ c.node ] != g.path || c.offset != g.offset || c.text != g.text;
    }
    int lookup_bad = 0;
    for ( std::size_t i = 0; i < g_golden_pld_lookup_count; ++i )
    {
        const std::uint32_t n = pfx_find( lib, g_golden_pld_lookups[ i ].query );
        lookup_bad += ( n == 0u ? std::string( ) : path_of[ n ] ) != g_golden_pld_lookups[ i ].found;
    }
    if ( failed == 0 && hook_bad == 0 && lookup_bad == 0 )
        LOG_SUCCESS( "pfx_load builds retail's node trees from %d VuPfxAsset documents: %d nodes, %d words, %d strings, %d hook calls, "
                     "%d path lookups exact",
            static_cast< int >( g_golden_pld_project_count ), static_cast< int >( rows.size( ) ), words,
            static_cast< int >( g_golden_pld_text_count ), static_cast< int >( g_golden_pld_hook_count ),
            static_cast< int >( g_golden_pld_lookup_count ) );
    else
        LOG_ERROR( "pfx_load FAILED: %d nodes, %d hook calls, %d lookups differ", failed, hook_bad, lookup_bad );
    return failed == 0 && hook_bad == 0 && lookup_bad == 0;
}

// VuPfxManager (world/pfxmgr.hxx) against retail (case pfx_manager): init, then 70 planned operations - createEffect
// (the instance answer planned), the "Final" tick, a release by entry, releaseAll, new instance answers - on scripted
// instances; after each the manager's words, every entry's words and the hook log (draw add / remove / update, the
// instance tick / release, the create request) in order. The draw component's +0x30 / +0x40 before its first AABB
// copy are UNKNOWN (not compared).
inline bool pfx_check_manager( const unsigned* golden, std::size_t golden_size, const char* const* golden_names, std::size_t golden_name_count,
    const char* tag )
{
    static const char* const* names_p;
    static std::size_t names_n;
    names_p = golden_names;
    names_n = golden_name_count;
    struct script_t
    {
        std::vector< std::vector< std::uint32_t > > insts;   // state, count, AABB 8
        std::uint32_t answer = 0;
        std::vector< std::uint32_t > log;
    } s;
    pfx_manager_ops_t ops;
    ops.user = &s;
    static auto name_index = []( const char* p ) -> std::uint32_t {
        for ( std::uint32_t i = 0; i < names_n; ++i )
            if ( std::strcmp( names_p[ i ], p ) == 0 )
                return i;
        return 0xFFFFu;
    };
    ops.create = []( void* u, const char* p ) {
        script_t& x = *static_cast< script_t* >( u );
        x.log.insert( x.log.end( ), { 4u, name_index( p ) } );
        return x.answer;
    };
    ops.release = []( void* u, std::uint32_t i ) { static_cast< script_t* >( u )->log.insert( static_cast< script_t* >( u )->log.end( ), { 5u, i - 1u } ); };
    ops.tick = []( void* u, std::uint32_t i, float dt ) {
        std::uint32_t b;
        std::memcpy( &b, &dt, 4 );
        static_cast< script_t* >( u )->log.insert( static_cast< script_t* >( u )->log.end( ), { 6u, i - 1u, b } );
    };
    ops.state = []( void* u, std::uint32_t i ) { return static_cast< script_t* >( u )->insts[ i - 1 ][ 0 ]; };
    ops.count = []( void* u, std::uint32_t i ) { return static_cast< script_t* >( u )->insts[ i - 1 ][ 1 ]; };
    ops.aabb = []( void* u, std::uint32_t i ) -> const std::uint32_t* { return static_cast< script_t* >( u )->insts[ i - 1 ].data( ) + 2; };
    ops.draw_add = []( void* u, int e ) { static_cast< script_t* >( u )->log.insert( static_cast< script_t* >( u )->log.end( ), { 1u, static_cast< std::uint32_t >( e ) } ); };
    ops.draw_remove = []( void* u, int e ) { static_cast< script_t* >( u )->log.insert( static_cast< script_t* >( u )->log.end( ), { 2u, static_cast< std::uint32_t >( e ) } ); };
    ops.draw_update = []( void* u, int e ) { static_cast< script_t* >( u )->log.insert( static_cast< script_t* >( u )->log.end( ), { 3u, static_cast< std::uint32_t >( e ) } ); };
    ops.register_tick = []( void* u ) { static_cast< script_t* >( u )->log.push_back( 0u ); };
    ops.unregister_tick = []( void* u ) { static_cast< script_t* >( u )->log.push_back( 7u ); };
    ops.destroy = []( void* u, int e ) { static_cast< script_t* >( u )->log.insert( static_cast< script_t* >( u )->log.end( ), { 8u, static_cast< std::uint32_t >( e ) } ); };
    const unsigned* w = golden;
    std::size_t at = 0;
    pfx_manager_t m;
    int failed = 0, compared = 0;
    const auto ix = []( int e ) { return e < 0 ? 0xFFFFFFFEu : static_cast< std::uint32_t >( e ); };
    auto check_state = [&]( const char* what, int step ) {
        const std::uint32_t head[ 12 ] = { 0u, m.capacity, 0u, m.mask, m.bits, m.serial_cap, ix( m.free_first ), ix( m.free_last ),
            m.free_count, ix( m.active_first ), ix( m.active_last ), m.active_count };
        int bad = 0;
        for ( int k = 0; k < 12; ++k, ++compared )
            bad += head[ k ] != w[ at + k ];
        at += 12;
        for ( const pfx_manager_entry_t& x : m.entries )
        {
            const std::uint32_t ours[ 16 ] = { x.flags, ix( x.next ), ix( x.prev ), x.instance ? x.instance - 1u : 0xFFFFFFFFu, x.index, x.serial,
                static_cast< std::uint32_t >( x.reflect ) | ( static_cast< std::uint32_t >( x.b1d ) << 8 ) |
                    ( static_cast< std::uint32_t >( x.registered ) << 16 ) | ( static_cast< std::uint32_t >( x.b1f ) << 24 ),
                x.w20, x.aabb[ 0 ], x.aabb[ 1 ], x.aabb[ 2 ], x.aabb[ 3 ], x.aabb[ 4 ], x.aabb[ 5 ], x.aabb[ 6 ], x.aabb[ 7 ] };
            for ( int k = 0; k < 16; ++k )
            {
                if ( !x.aabb_known && ( k == 11 || k == 15 ) )
                    continue;   // UNKNOWN component +0x30 / +0x40
                bad += ours[ k ] != w[ at + k ];
                ++compared;
            }
            at += 16;
        }
        const std::uint32_t n = w[ at++ ];
        std::vector< std::uint32_t > want( w + at, w + at );
        for ( std::uint32_t k = 0; k < n; ++k )
        {
            const std::uint32_t code = w[ at++ ];
            want.push_back( code );
            const int args = code == 0 || code == 7 ? 0 : code == 6 ? 2 : 1;
            for ( int j = 0; j < args; ++j )
                want.push_back( w[ at++ ] );
        }
        bad += want != s.log;
        ++compared;
        if ( bad )
        {
            if ( failed < 6 )
                LOG_ERROR( "%s: %s (step %d) differs from retail (%d)", tag, what, step, bad );
            ++failed;
        }
    };
    pfx_manager_init( m, ops );
    check_state( "init", -1 );
    const std::uint32_t nops = w[ at++ ];
    for ( std::uint32_t i = 0; i < nops; ++i )
    {
        s.log.clear( );
        const std::uint32_t code = w[ at++ ];
        if ( code == 0 )
        {
            const std::uint32_t name = w[ at++ ], answer = w[ at++ ], ret = w[ at++ ];
            s.answer = 0u;
            if ( answer != 0xFFFFFFFFu )
            {
                s.insts.emplace_back( w + at, w + at + 10 );
                at += 10;
                s.answer = answer + 1u;
            }
            const int e = pfx_manager_create( m, golden_names[ name ], ops );
            failed += ix( e ) != ret ? 1 : 0;
        }
        else if ( code == 1 )
        {
            float dt;
            std::memcpy( &dt, &w[ at++ ], 4 );
            pfx_manager_tick( m, dt, ops );
        }
        else if ( code == 2 )
            pfx_manager_release( m, static_cast< int >( w[ at++ ] ), ops );
        else if ( code == 3 )
            pfx_manager_release_all( m, ops );
        else if ( code == 5 )
            pfx_manager_destroy( m, ops );
        else if ( code == 4 )
            for ( auto& inst : s.insts )
            {
                inst.assign( w + at, w + at + 10 );
                at += 10;
            }
        check_state( "operation", static_cast< int >( i ) );
    }
    if ( at != golden_size )
    {
        LOG_ERROR( "%s: golden layout read %d of %d words", tag, static_cast< int >( at ), static_cast< int >( golden_size ) );
        return false;
    }
    if ( failed == 0 )
        LOG_SUCCESS( "%s: VuPfxManager matches retail's 0x52B7B0 / 0x52BCD0 / 0x52BBF0 / 0x52BC30 / 0x52BD50 / 0x52BB60%s over %d "
                     "operations: %d words and hook logs exact",
            tag, std::strcmp( tag, "pfx_manager" ) == 0 ? "" : " / the dtor 0x52B810", static_cast< int >( nops ), compared );
    else
        LOG_ERROR( "%s FAILED %d of %d steps", tag, failed, static_cast< int >( nops ) + 1 );
    return failed == 0;
}

inline bool verify_pfx_manager( )
{
    return pfx_check_manager( g_golden_pmg_words, g_golden_pmg_size, g_golden_pmg_names, sizeof( g_golden_pmg_names ) / sizeof( g_golden_pmg_names[ 0 ] ),
        "pfx_manager" );
}

// VuPfxManager's dtor 0x52B810 (pfxmgr.hxx pfx_manager_destroy; case pfx_manager_dtor: nine effects, a plan, two ticks, a
// release, then the dtor - the unregister, releaseAll, every free entry's release / slot 0 0x405000 in order).
inline bool verify_pfx_manager_dtor( )
{
    return pfx_check_manager( g_golden_pmd_words, g_golden_pmd_size, g_golden_pmd_names, sizeof( g_golden_pmd_names ) / sizeof( g_golden_pmd_names[ 0 ] ),
        "pfx_manager_dtor" );
}

// water_point_height (world/query.hxx) against VuWater's base-mode one-point query 0x529140 - the fountain's water snap -
// on Hydroelectric's built water (case water_snap): every out word (+0x1C, retail's stale stack, skipped; +0x24 must stay
// as the caller had it), the slot 3 calls in order (wave, tag, ctx+0x60, ctx+0x64), kernels water_view_kernel.
inline bool verify_water_snap( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "water_snap: the document did not load" );
        return false;
    }
    water_built_t built{};
    water_build_world( *tree, built, []( entity_t& e ) { return entity_transform( e ); } );
    water_view_t view{};
    water_view_build( built, view );
    struct logged_t
    {
        const water_view_t* view;
        std::vector< std::uint32_t > log;
    } logged{ &view, {} };
    const water_kernel_fn kernel = []( void* user, int wave, water_query_t& ctx ) {
        logged_t& l = *static_cast< logged_t* >( user );
        l.log.insert( l.log.end( ), { static_cast< std::uint32_t >( wave ), ctx.tag, ctx.flag96, ctx.flag100 } );
        return water_view_kernel( const_cast< water_view_t* >( l.view ), wave, ctx );
    };
    int failed = 0, found = 0, calls = 0, skipped = 0, moved = 0;
    for ( std::size_t index = 0; index < g_golden_wsn_count; ++index )
    {
        const unsigned* s = g_golden_wsn_words + g_golden_wsn_offsets[ index ];
        view.water.skip_waves = s[ 0 ] != 0u;
        skipped += s[ 0 ] ? 1 : 0;
        float point[ 4 ];
        std::memcpy( point, s + 1, 16 );
        std::uint32_t out[ 11 ];
        std::memset( out, 0x77, sizeof( out ) );
        logged.log.clear( );
        const bool reached = water_point_height( view.water, point, kernel, &logged, *reinterpret_cast< water_vertex_t* >( out ) );
        const char* what = reached ? nullptr : "a path not recreated was reached";
        for ( int k = 0; k < 11; ++k )
            if ( !what && k != 7 && out[ k ] != s[ 5 + k ] )
                what = "an out word";
        found += s[ 5 + 8 ] != 0xFEFFFFFFu ? 1 : 0;
        moved += ( s[ 5 + 4 ] | s[ 5 + 5 ] | s[ 5 + 6 ] ) != 0u ? 1 : 0;
        const unsigned n = s[ 16 ];
        calls += static_cast< int >( n );
        if ( !what && logged.log.size( ) != 4 * n )
            what = "the number of slot 3 calls";
        for ( unsigned k = 0; k < 4 * n && !what; ++k )
            if ( logged.log[ k ] != s[ 17 + k ] )
                what = "a slot 3 call";
        if ( what )
        {
            ++failed;
            LOG_ERROR( "water_snap: query %d: %s", static_cast< int >( index ), what );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "water_point_height matches VuWater's base-mode one-point query 0x529140 (0x52AEC0 / 0x413500(0) / 0x52B0E0) on %d "
                     "queries: %d on a surface, %d displaced by waves, %d slot 3 calls, %d with waves skipped - every out word but the stale "
                     "+0x1C",
            static_cast< int >( g_golden_wsn_count ), found, moved, calls, skipped );
    else
        LOG_ERROR( "water_snap FAILED %d of %d", failed, static_cast< int >( g_golden_wsn_count ) );
    return failed == 0;
}

// pfx_reload_project / pfx_remove_project and the node dtors (world/pfxload.hxx) against VuPfxImpl's 0x51D870 / 0x51D9C0
// and retail's node slot 0s (case pfx_reload): the race's two documents loaded, three nodes held (refcount 2), then reload
// Generic with Hydroelectric's document, remove Hydroelectric, reload / remove a missing name. Compared: each return, the
// events in order (0x563C00's entries, the geom model resets, the texture asset releases: by the node's path before the
// operation and the offset), every remaining node (path, vtable, +4, child count), the held nodes' refcounts.
inline bool verify_pfx_reload( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "pfx_reload: Base.apf did not open" );
        return false;
    }
    static pfx_runtime_t rt;
    pfx_runtime_init( rt );
    pfx_library_t lib;
    std::map< std::string, value_t > docs;
    for ( const char* asset : { "VuPfxAsset/Hydroelectric", "VuPfxAsset/Generic" } )
    {
        std::vector< std::uint8_t > blob;
        const std::string name = std::string( asset ).substr( 11 );
        if ( !archive.read( asset, blob ) || !pfx_asset_document( blob, docs[ asset ] ) || pfx_add_project( rt, lib, name, docs[ asset ] ) == 0u )
        {
            LOG_ERROR( "pfx_reload: %s did not load", asset );
            return false;
        }
    }
    auto paths = [ & ]( ) {
        std::vector< std::pair< std::string, std::uint32_t > > rows;
        for ( const auto& pr : lib.projects )
            pfx_load_rows( rt, lib, pr.second, pr.first, rows );
        return rows;
    };
    std::map< std::string, std::uint32_t > held;
    for ( const auto& r : paths( ) )
        for ( std::size_t i = 0; i < g_golden_prl_ref_count; ++i )
            if ( r.first == g_golden_prl_refs[ i ].path )
            {
                rt.heap[ r.second + 4 ] = 2u;
                held[ r.first ] = r.second;
            }
    struct log_t
    {
        std::map< std::uint32_t, std::string > path_of;
        std::vector< std::tuple< int, std::string, std::uint32_t > > events;
    } log;
    pfx_release_hooks_t hooks;
    hooks.user = &log;
    hooks.destroyed = []( void* u, std::uint32_t node ) {
        log_t& l = *static_cast< log_t* >( u );
        l.events.emplace_back( 0, l.path_of[ node ], 0u );
    };
    hooks.model = []( void* u, std::uint32_t node ) {
        log_t& l = *static_cast< log_t* >( u );
        l.events.emplace_back( 1, l.path_of[ node ], 0u );
    };
    hooks.texture = []( void* u, std::uint32_t node, std::uint32_t offset ) {
        log_t& l = *static_cast< log_t* >( u );
        l.events.emplace_back( 2, l.path_of[ node ], offset );
    };
    int failed = 0, events = 0;
    for ( std::size_t i = 0; i < g_golden_prl_op_count; ++i )
    {
        const golden_pfx_reload_op_t& op = g_golden_prl_ops[ i ];
        log.path_of.clear( );
        for ( const auto& r : paths( ) )
            log.path_of[ r.second ] = r.first;
        log.events.clear( );
        unsigned ret = 0;
        if ( op.remove )
            pfx_remove_project( rt, lib, op.name, hooks );
        else
            ret = pfx_reload_project( rt, lib, op.name, docs[ op.doc ], pfx_load_hooks_t( ), hooks ) ? 1u : 0u;
        int bad = ret != op.ret || log.events.size( ) != op.count;
        for ( unsigned e = 0; e < op.count && e < log.events.size( ); ++e )
        {
            const golden_pfx_reload_event_t& g = g_golden_prl_events[ op.first + e ];
            bad += std::get< 0 >( log.events[ e ] ) != g.kind || std::get< 1 >( log.events[ e ] ) != g.path || std::get< 2 >( log.events[ e ] ) != g.offset;
        }
        events += static_cast< int >( op.count );
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "pfx_reload: %s %s: %d differences (%d events, retail %u)", op.remove ? "remove" : "reload", op.name, bad,
                static_cast< int >( log.events.size( ) ), op.count );
        }
    }
    const auto rows = paths( );
    int row_bad = rows.size( ) != g_golden_prl_row_count;
    for ( std::size_t i = 0; i < rows.size( ) && i < g_golden_prl_row_count; ++i )
    {
        const golden_pfx_reload_row_t& g = g_golden_prl_rows[ i ];
        const std::uint32_t a = rows[ i ].second;
        row_bad += rows[ i ].first != g.path || rt.heap[ a ] != g.vtable || rt.heap[ a + 4 ] != g.w4 || rt.heap[ a + 0x10 ] != g.count;
    }
    for ( std::size_t i = 0; i < g_golden_prl_ref_count; ++i )
        row_bad += held.count( g_golden_prl_refs[ i ].path ) == 0 || rt.heap[ held[ g_golden_prl_refs[ i ].path ] + 4 ] != g_golden_prl_refs[ i ].refs;
    if ( row_bad )
    {
        ++failed;
        LOG_ERROR( "pfx_reload: %d differences in the nodes after (%d nodes, retail %d)", row_bad, static_cast< int >( rows.size( ) ),
            static_cast< int >( g_golden_prl_row_count ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "pfx_reload_project / pfx_remove_project and the node dtors match VuPfxImpl 0x51D870 / 0x51D9C0 and 0x563C00 et al.: "
                     "%d operations, %d destruction events in order, %d nodes after, %d held refcounts",
            static_cast< int >( g_golden_prl_op_count ), events, static_cast< int >( rows.size( ) ), static_cast< int >( g_golden_prl_ref_count ) );
    else
        LOG_ERROR( "pfx_reload FAILED %d", failed );
    return failed == 0;
}

// water_entity_release (world/surface.hxx) against the wave entities' slot 7s 0x5D3690 / 0x5D3F10 / 0x5D6710 / 0x5D5E90 /
// 0x5D8D30 on Hydroelectric's built water (case water_release): every such entity in order, every third with its wave's
// refcount first forced to 1. Compared per release: which wave's dtor ran, the wave list node by node, the pool count and
// free node, every wave's refcount (gone ones excepted).
inline bool water_release_check( entity_t& tree, const unsigned* s, const char* label, const char* where )
{
    water_built_t b{};
    water_build_world( tree, b, []( entity_t& e ) { return entity_transform( e ); } );
    const unsigned n = *s++;
    int failed = 0, destroyed = 0, forced = 0;
    for ( unsigned r = 0; r < n; ++r )
    {
        const int wave = static_cast< int >( *s++ );
        const unsigned force = *s++;
        s++;   // the entity's word after: 0 (the caller's store)
        forced += force ? 1 : 0;
        if ( force )
            b.waves[ static_cast< std::size_t >( wave ) ].word[ 1 ] = 1u;
        std::vector< std::uint8_t > before = b.wave_gone;
        before.resize( b.waves.size( ), 0 );
        const int unread = b.unread_dtors;
        water_entity_release( b, wave );
        b.wave_gone.resize( b.waves.size( ), 0 );
        int bad = b.unread_dtors != unread;
        std::vector< unsigned > events;
        for ( std::size_t k = 0; k < b.waves.size( ); ++k )
            if ( b.wave_gone[ k ] && !before[ k ] )
                events.push_back( static_cast< unsigned >( k ) );
        const unsigned ne = *s++;
        bad += events.size( ) != ne;
        for ( unsigned k = 0; k < ne; ++k, ++s )
            bad += k < events.size( ) && events[ k ] != *s ? 1 : 0;
        destroyed += static_cast< int >( ne );
        const unsigned nl = *s++;
        int e = b.hold.list;
        for ( unsigned k = 0; k < nl; ++k, s += 2 )
        {
            bad += static_cast< unsigned >( e ) != s[ 0 ];
            if ( e < 0 )
                break;
            bad += static_cast< unsigned >( b.hold.pool[ e ].leaf ) != s[ 1 ];
            e = b.hold.pool[ e ].next;
        }
        bad += e != -1;
        bad += static_cast< unsigned >( b.hold.pool_count ) != *s++;
        bad += static_cast< unsigned >( b.hold.pool_free ) != *s++;
        const unsigned nr = *s++;
        for ( unsigned k = 0; k < nr; ++k, ++s )
            if ( *s != 0xFFFFFFFFu )
                bad += k >= b.waves.size( ) || b.wave_gone[ k ] || b.waves[ k ].word[ 1 ] != *s;
            else
                bad += k < b.wave_gone.size( ) && !b.wave_gone[ k ];
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "%s: release %u (wave %d): %d differences", label, r, wave, bad );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "water_entity_release matches the wave entities' slot 7 0x5D3690 / 0x5D3F10 / 0x5D6710 / 0x5D5E90 / 0x5D8D30 / 0x5D8A00 on "
                     "%s built water: %u releases (%d with the refcount forced), %d dtors in order, the list, the pool and every "
                     "refcount after each",
            where, n, forced, destroyed );
    else
        LOG_ERROR( "%s FAILED %d of %u", label, failed, n );
    return failed == 0;
}

inline bool verify_water_release( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "water_release: the document did not load" );
        return false;
    }
    return water_release_check( *tree, g_golden_wrl_words, "water_release", "Hydroelectric's" );
}

// A tree as case water_surface_release's flat words: root, free, leaves, used, free count, node count, then per node its
// rect words, parent, first, second, tag.
inline std::vector< unsigned > water_tree_words( const water_tree_t& t )
{
    std::vector< unsigned > w = { static_cast< unsigned >( t.root ), static_cast< unsigned >( t.free ), static_cast< unsigned >( t.leaves ),
        static_cast< unsigned >( t.used ), static_cast< unsigned >( t.free_count ), static_cast< unsigned >( t.nodes.size( ) ) };
    for ( const water_tree_node_t& n : t.nodes )
    {
        unsigned r[ 4 ];
        std::memcpy( r, n.rect, 16 );
        w.insert( w.end( ), { r[ 0 ], r[ 1 ], r[ 2 ], r[ 3 ], static_cast< unsigned >( n.parent ), static_cast< unsigned >( n.first ),
                                static_cast< unsigned >( n.second ), n.tag } );
    }
    return w;
}

// water_surface_entity_release / water_ramp_entity_release (world/surface.hxx) against VuWaterSurfaceEntity's slot 7
// 0x5DB5C0 and VuWaterRampEntity's 0x5D7030 on Hydroelectric's built water (case water_surface_release): the surface list's
// sorted order after the build, then every surface / ramp entity released in entity order (release 2 with the surface's
// refcount forced to 2, the renderer's byte set on every fourth from 1, two listeners). Compared per release: the hook
// calls in order (draw removal, render wait, both listeners' slot 1, the tree dtor) and the destroyed surfaces / waves,
// the wave tree as 0x598D10 receives it word for word, the draw / render bytes after, the surface list, the surface tree
// word for word, the wave list, every listed wave's entries, the pool, every wave and surface refcount, every live
// surface's wave tree (summary + FNV-1a 64).
inline bool verify_water_surface_release( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "water_surface_release: the document did not load" );
        return false;
    }
    water_built_t b{};
    water_build_world( *tree, b, []( entity_t& e ) { return entity_transform( e ); } );
    const unsigned* s = g_golden_wsr_words;
    int failed = 0;
    const unsigned nf = *s++;
    int bad0 = b.surface_list.size( ) != nf;
    for ( unsigned k = 0; k < nf; ++k )
        bad0 += k < b.surface_list.size( ) && static_cast< unsigned >( b.surface_list[ k ] ) != s[ k ];
    s += nf;
    if ( bad0 )
    {
        ++failed;
        LOG_ERROR( "water_surface_release: the surface list after the build: %d differences", bad0 );
    }
    struct log_t
    {
        std::vector< std::array< unsigned, 3 > > events;
        std::vector< std::vector< unsigned > > trees;
    } log;
    bool busy = false;
    water_surface_hooks_t hk;
    hk.user = &log;
    hk.render_busy = &busy;
    hk.render_wait = []( void* u ) { static_cast< log_t* >( u )->events.push_back( { 1u, 0u, 0u } ); };
    hk.listeners = 2;
    hk.listener = []( void* u, int k, int surface ) {
        static_cast< log_t* >( u )->events.push_back( { 2u, static_cast< unsigned >( k ), static_cast< unsigned >( surface ) } );
    };
    hk.draw_remove = []( void* u, int surface ) { static_cast< log_t* >( u )->events.push_back( { 0u, static_cast< unsigned >( surface ), 0u } ); };
    hk.destroying = []( void* u, int surface, const water_tree_t& t ) {
        static_cast< log_t* >( u )->events.push_back( { 4u, static_cast< unsigned >( surface ), 0u } );
        static_cast< log_t* >( u )->trees.push_back( water_tree_words( t ) );
    };
    const unsigned n = *s++;
    int hooked = 0, destroyed = 0, words = 0;
    for ( unsigned r = 0; r < n; ++r )
    {
        const int surface = static_cast< int >( *s++ );
        const int wave = static_cast< int >( *s++ );
        const unsigned forced = *s++;
        busy = *s++ != 0;
        bool drawn = *s++ != 0;
        if ( forced )
            b.surfaces[ static_cast< std::size_t >( surface ) ].word[ 1 ] = 2u;
        std::vector< std::uint8_t > wbefore = b.wave_gone, sbefore = b.surface_gone;
        wbefore.resize( b.waves.size( ), 0 );
        sbefore.resize( b.surfaces.size( ), 0 );
        log.events.clear( );
        log.trees.clear( );
        const int unread = b.unread_dtors;
        const int missing = wave >= 0 ? water_ramp_entity_release( b, surface, wave, drawn, hk ) : water_surface_entity_release( b, surface, drawn, hk );
        b.wave_gone.resize( b.waves.size( ), 0 );
        b.surface_gone.resize( b.surfaces.size( ), 0 );
        int bad = missing != 0 || b.unread_dtors != unread;
        // the events: the hooks' in order; the dtors (kinds 3 / 5) as the gone transitions, in index order
        std::vector< std::array< unsigned, 3 > > want;
        std::vector< unsigned > sd, wd;
        const unsigned ne = *s++;
        for ( unsigned k = 0; k < ne; ++k, s += 3 )
            if ( s[ 0 ] == 3u )
                sd.push_back( s[ 1 ] );
            else if ( s[ 0 ] == 5u )
                wd.push_back( s[ 1 ] );
            else
                want.push_back( { s[ 0 ], s[ 0 ] == 1u ? 0u : s[ 1 ], s[ 0 ] == 2u ? s[ 2 ] : 0u } );
        bad += want != log.events;
        hooked += static_cast< int >( want.size( ) );
        std::vector< unsigned > sg, wg;
        for ( std::size_t k = 0; k < b.surfaces.size( ); ++k )
            if ( b.surface_gone[ k ] && !sbefore[ k ] )
                sg.push_back( static_cast< unsigned >( k ) );
        for ( std::size_t k = 0; k < b.waves.size( ); ++k )
            if ( b.wave_gone[ k ] && !wbefore[ k ] )
                wg.push_back( static_cast< unsigned >( k ) );
        bad += sg != sd || wg != wd;
        destroyed += static_cast< int >( sd.size( ) + wd.size( ) );
        const unsigned nt = *s++;
        bad += nt != log.trees.size( );
        for ( unsigned k = 0; k < nt; ++k )
        {
            const unsigned len = *s++;
            bad += k >= log.trees.size( ) || log.trees[ k ] != std::vector< unsigned >( s, s + len );
            words += static_cast< int >( len );
            s += len;
        }
        bad += s[ 0 ] != ( drawn ? 1u : 0u ) || s[ 3 ] != ( busy ? 1u : 0u );   // +0x128 / +0x140 = 0: the caller's stores
        s += 4;
        const unsigned nl = *s++;
        bad += b.surface_list.size( ) != nl;
        for ( unsigned k = 0; k < nl; ++k )
            bad += k < b.surface_list.size( ) && static_cast< unsigned >( b.surface_list[ k ] ) != s[ k ];
        s += nl;
        bad += *s++ != b.surface_list.size( );
        const unsigned ns = *s++;
        bad += water_tree_words( b.hold.surface_tree ) != std::vector< unsigned >( s, s + ns );
        words += static_cast< int >( ns );
        s += ns;
        const unsigned nw = *s++;
        int e = b.hold.list;
        std::vector< int > listed;
        for ( unsigned k = 0; k < nw; ++k, s += 2 )
        {
            bad += static_cast< unsigned >( e ) != s[ 0 ];
            if ( e < 0 )
                break;
            bad += static_cast< unsigned >( b.hold.pool[ e ].leaf ) != s[ 1 ];
            listed.push_back( b.hold.pool[ e ].leaf );
            e = b.hold.pool[ e ].next;
        }
        bad += e != -1;
        for ( unsigned k = 0; k < nw; ++k )
        {
            const unsigned m = *s++;
            int c = k < listed.size( ) ? b.hold.waves[ static_cast< std::size_t >( listed[ k ] ) ].entries : -1;
            for ( unsigned j = 0; j < m; ++j, s += 2 )
            {
                bad += c < 0 || static_cast< unsigned >( c ) != s[ 0 ] || static_cast< unsigned >( b.hold.pool[ c ].surface ) != s[ 1 ];
                if ( c >= 0 )
                    c = b.hold.pool[ c ].next;
            }
            bad += c != -1;
        }
        bad += static_cast< unsigned >( b.hold.pool_count ) != *s++;
        bad += static_cast< unsigned >( b.hold.pool_free ) != *s++;
        const unsigned nr = *s++;
        for ( unsigned k = 0; k < nr; ++k, ++s )
            bad += *s != 0xFFFFFFFFu ? ( b.wave_gone[ k ] || b.waves[ k ].word[ 1 ] != *s ) : !b.wave_gone[ k ];
        const unsigned nsr = *s++;
        for ( unsigned k = 0; k < nsr; ++k, ++s )
            bad += *s != 0xFFFFFFFFu ? ( b.surface_gone[ k ] || b.surfaces[ k ].word[ 1 ] != *s ) : !b.surface_gone[ k ];
        const unsigned nwt = *s++;
        unsigned live = 0;
        for ( std::size_t k = 0; k < b.surfaces.size( ); ++k )
            live += b.surface_gone[ k ] ? 0u : 1u;
        bad += live != nwt;
        for ( unsigned k = 0; k < nwt; ++k, s += 9 )
        {
            const std::vector< unsigned > t = water_tree_words( b.hold.surfaces[ s[ 0 ] ].waves );
            const std::uint64_t h = water_fnv64( t.data( ) + 6, t.size( ) - 6 );
            for ( int j = 0; j < 6; ++j )
                bad += t[ static_cast< std::size_t >( j ) ] != s[ 1 + j ];
            bad += static_cast< unsigned >( h ) != s[ 7 ] || static_cast< unsigned >( h >> 32 ) != s[ 8 ];
        }
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "water_surface_release: release %u (surface %d, wave %d): %d differences", r, surface, wave, bad );
        }
    }
    if ( s != g_golden_wsr_words + g_golden_wsr_size )
    {
        ++failed;
        LOG_ERROR( "water_surface_release: the golden words were not consumed exactly" );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "water_surface_entity_release / water_ramp_entity_release match VuWaterSurfaceEntity 0x5DB5C0 / VuWaterRampEntity "
                     "0x5D7030 (removeSurface 0x529770, 0x529990, the trees 0x598FC0 / 0x598D10) on Hydroelectric's built water: the "
                     "sorted surface list, %u releases, %d hook calls in order, %d dtors, %d tree words, the lists, pool, refcounts and "
                     "wave trees after each",
            n, hooked, destroyed, words );
    else
        LOG_ERROR( "water_surface_release FAILED %d", failed );
    return failed == 0;
}

// water_point_entity_init / _tick / _release (world/surface.hxx) against VuPointWaveEntity's slot 6 0x5D4A90, tick 0x5D4C50
// and slot 7 0x5D4AF0 on Hydroelectric's built water (case point_entity): eight entities, pending on all but every third,
// entity 4's wave refcount forced to 1 before its release. Compared: the tick registration's phase (and the functor's
// recorded words: vtable 0x753414, the entity, 0x5D4C50, 0), the descriptor / pending byte / wave after each of two ticks,
// the new wave's words, the release's unregistration and dtor in order, the wave list, the pool, every point wave's +4.
inline bool verify_point_entity( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "point_entity: the document did not load" );
        return false;
    }
    water_built_t b{};
    water_build_world( *tree, b, []( entity_t& e ) { return entity_transform( e ); } );
    const int nb = static_cast< int >( b.waves.size( ) );
    struct log_t
    {
        std::vector< std::string > registered;
        int unregistered = 0;
    } log;
    water_point_entity_hooks_t hk;
    hk.user = &log;
    hk.tick_register = []( void* u, const char* phase ) { static_cast< log_t* >( u )->registered.push_back( phase ); };
    hk.tick_unregister = []( void* u ) { ++static_cast< log_t* >( u )->unregistered; };
    auto index = [ & ]( int w ) { return w < 0 ? 0xFFFFFFFFu : w < nb ? static_cast< unsigned >( w ) : static_cast< unsigned >( 100 + w - nb ); };
    const unsigned* s = g_golden_pen_words;
    const unsigned n = *s++;
    int failed = 0, compared = 0;
    for ( unsigned r = 0; r < n; ++r )
    {
        int bad = 0;
        log = log_t{};
        water_point_entity_t e{};
        bad += water_point_entity_init( hk );
        const unsigned nreg = *s++;
        bad += nreg != log.registered.size( );
        for ( unsigned k = 0; k < nreg; ++k, s += 5 )
            bad += s[ 0 ] != 0x753414u || s[ 1 ] != 1u || s[ 2 ] != 0x5D4C50u || s[ 3 ] != 0u || s[ 4 ] != 1u ||
                   ( k < log.registered.size( ) && log.registered[ k ] != "Decision" );
        std::uint32_t position[ 4 ];
        std::memcpy( position, s, 16 );
        s += 4;
        std::memcpy( e.desc + 4, s, 28 );
        s += 7;
        const bool pending = *s++ != 0;
        e.pending = pending;
        for ( int t = 0; t < 2; ++t, s += 14 )
        {
            water_point_entity_tick( b, e, position );
            for ( int k = pending ? 0 : 4; k < 11; ++k, ++compared )
                bad += e.desc[ k ] != s[ k ];
            bad += ( s[ 12 ] != 0u ) != e.pending || s[ 13 ] != ( e.wave < 0 ? 0xFFFFFFFFu : static_cast< unsigned >( e.wave - nb ) );
        }
        if ( *s++ )
        {
            const water_words_t& w = b.waves[ static_cast< std::size_t >( e.wave < 0 ? 0 : e.wave ) ];
            bad += e.wave < 0;
            for ( int k = 0; k < 37; ++k )
                if ( k != 16 && k != 35 && w.known[ k ] )
                {
                    bad += w.word[ k ] != s[ k ];
                    ++compared;
                }
            bad += s[ 37 ] != 1u || w.word[ 35 ] == 0u;
            s += 38;
        }
        else
            bad += e.wave >= 0;
        const int wave = e.wave;
        if ( *s++ && wave >= 0 )
            b.waves[ static_cast< std::size_t >( wave ) ].word[ 1 ] = 1u;
        std::vector< std::uint8_t > before = b.wave_gone;
        before.resize( b.waves.size( ), 0 );
        bad += water_point_entity_release( b, e, hk );
        b.wave_gone.resize( b.waves.size( ), 0 );
        std::vector< std::array< unsigned, 2 > > ours = { { 0u, 1u } };
        for ( std::size_t k = 0; k < b.waves.size( ); ++k )
            if ( b.wave_gone[ k ] && !before[ k ] )
                ours.push_back( { 1u, static_cast< unsigned >( static_cast< int >( k ) - nb ) } );
        const unsigned ne = *s++;
        std::vector< std::array< unsigned, 2 > > want;
        for ( unsigned k = 0; k < ne; ++k, s += 2 )
            want.push_back( { s[ 0 ], s[ 1 ] } );
        bad += want != ours || log.unregistered != 1;
        bad += *s++ != 0u || e.wave != -1;   // +0xA0 after
        const unsigned nl = *s++;
        int at = b.hold.list;
        for ( unsigned k = 0; k < nl; ++k, s += 2 )
        {
            bad += static_cast< unsigned >( at ) != s[ 0 ];
            if ( at < 0 )
                break;
            bad += index( b.hold.pool[ at ].leaf ) != s[ 1 ];
            at = b.hold.pool[ at ].next;
        }
        bad += at != -1;
        bad += static_cast< unsigned >( b.hold.pool_count ) != *s++;
        bad += static_cast< unsigned >( b.hold.pool_free ) != *s++;
        const unsigned nr = *s++;
        for ( unsigned k = 0; k < nr; ++k, ++s )
        {
            const std::size_t w = static_cast< std::size_t >( nb ) + k;
            bad += w >= b.waves.size( ) || ( *s == 0xFFFFFFFFu ? !b.wave_gone[ w ] : ( b.wave_gone[ w ] || b.waves[ w ].word[ 1 ] != *s ) );
        }
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "point_entity: entity %u: %d differences", r, bad );
        }
    }
    if ( s != g_golden_pen_words + g_golden_pen_size )
    {
        ++failed;
        LOG_ERROR( "point_entity: the golden words were not consumed exactly" );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "water_point_entity_init / _tick / _release match VuPointWaveEntity 0x5D4A90 / 0x5D4C50 / 0x5D4AF0 on Hydroelectric's "
                     "built water: %u entities, %d words, the registration, the release order, the list, pool and refcounts after each",
            n, compared );
    else
        LOG_ERROR( "point_entity FAILED %d of %u", failed, n );
    return failed == 0;
}

// water_built_release / water_global_clear / water_built_destroy_water (world/surface.hxx) against VuWater's slot 3 0x528410,
// slot 4 0x527E80 and slot 0 0x528120 on Hydroelectric's built water plus six point waves (case water_shutdown; every third
// listed wave's refcount forced to 1 first). Compared: the wave list before, the hook calls in order (the waves' dtors as the
// destroyed set, in list order, between the unregistration and the shader), the surface tree at 0x598D10 word for word, the
// pool, every refcount, every surface's wave tree after (summary + FNV-1a 64), the global, the dtor's list sizes.
inline bool verify_water_shutdown( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "water_shutdown: the document did not load" );
        return false;
    }
    water_built_t b{};
    water_build_world( *tree, b, []( entity_t& e ) { return entity_transform( e ); } );
    const unsigned* s = g_golden_wsd_words;
    int bad = 0;
    const unsigned np = *s++;
    for ( unsigned k = 0; k < np; ++k, s += 11 )
        water_create_point_wave( b, s );
    const unsigned nl = *s++;
    std::vector< int > order;
    int e = b.hold.list;
    for ( unsigned k = 0; k < nl; ++k, s += 2 )
    {
        bad += static_cast< unsigned >( e ) != s[ 0 ];
        if ( e < 0 )
            break;
        bad += static_cast< unsigned >( b.hold.pool[ e ].leaf ) != s[ 1 ];
        order.push_back( b.hold.pool[ e ].leaf );
        e = b.hold.pool[ e ].next;
    }
    bad += e != -1;
    const unsigned nfo = *s++;
    for ( unsigned k = 0; k < nfo; ++k )
        b.waves[ *s++ ].word[ 1 ] = 1u;
    struct log_t
    {
        std::vector< std::array< unsigned, 2 > > events;
        std::vector< std::vector< unsigned > > trees;
    } log;
    std::uint32_t shader = 1;
    water_release_hooks_t hk;
    hk.user = &log;
    hk.tick_unregister = []( void* u ) { static_cast< log_t* >( u )->events.push_back( { 0u, 1u } ); };
    hk.shader_refs = &shader;
    hk.shader_destroy = []( void* u ) { static_cast< log_t* >( u )->events.push_back( { 2u, 0u } ); };
    hk.renderer = true;
    hk.renderer_destroy = []( void* u ) { static_cast< log_t* >( u )->events.push_back( { 3u, 1u } ); };
    hk.tree_destroying = []( void* u, const water_tree_t& t ) {
        static_cast< log_t* >( u )->events.push_back( { 4u, 0u } );
        static_cast< log_t* >( u )->trees.push_back( water_tree_words( t ) );
    };
    std::vector< std::uint8_t > before = b.wave_gone;
    before.resize( b.waves.size( ), 0 );
    bad += water_built_release( b, hk );
    b.wave_gone.resize( b.waves.size( ), 0 );
    std::vector< std::array< unsigned, 2 > > ours = log.events;
    std::vector< std::array< unsigned, 2 > > dtors;
    for ( const int w : order )
        if ( b.wave_gone[ static_cast< std::size_t >( w ) ] && !before[ static_cast< std::size_t >( w ) ] )
            dtors.push_back( { 1u, static_cast< unsigned >( w ) } );
    if ( !ours.empty( ) )
        ours.insert( ours.begin( ) + 1, dtors.begin( ), dtors.end( ) );
    const unsigned ne = *s++;
    std::vector< std::array< unsigned, 2 > > want;
    for ( unsigned k = 0; k < ne; ++k, s += 2 )
        want.push_back( { s[ 0 ], s[ 1 ] } );
    bad += want != ours;
    const unsigned nt = *s++;
    bad += nt != log.trees.size( );
    int words = 0;
    for ( unsigned k = 0; k < nt; ++k )
    {
        const unsigned len = *s++;
        bad += k >= log.trees.size( ) || log.trees[ k ] != std::vector< unsigned >( s, s + len );
        words += static_cast< int >( len );
        s += len;
    }
    bad += static_cast< unsigned >( b.hold.pool_count ) != *s++;
    bad += static_cast< unsigned >( b.hold.pool_free ) != *s++;
    bad += *s++ != 0u || b.hold.list != -1;   // the list head
    s++;                                       // +0x14 = 0: the caller's store
    const unsigned nr = *s++;
    for ( unsigned k = 0; k < nr; ++k, ++s )
        bad += *s != 0xFFFFFFFFu ? ( b.wave_gone[ k ] || b.waves[ k ].word[ 1 ] != *s ) : !b.wave_gone[ k ];
    const unsigned nwt = *s++;
    for ( unsigned k = 0; k < nwt; ++k, s += 9 )
    {
        const std::vector< unsigned > t = water_tree_words( b.hold.surfaces[ s[ 0 ] ].waves );
        const std::uint64_t h = water_fnv64( t.data( ) + 6, t.size( ) - 6 );
        for ( int j = 0; j < 6; ++j )
            bad += t[ static_cast< std::size_t >( j ) ] != s[ 1 + j ];
        bad += static_cast< unsigned >( h ) != s[ 7 ] || static_cast< unsigned >( h >> 32 ) != s[ 8 ];
    }
    water_built_t* global = &b;
    water_global_clear( global );
    bad += *s++ != 0u || global != nullptr;
    water_built_destroy_water( b );
    bad += s[ 0 ] != 0x736EE8u || s[ 1 ] != b.surface_list.size( ) || s[ 2 ] != 0u || s[ 3 ] != 0u || !b.hold.pool.empty( );
    s += 4;
    bad += s != g_golden_wsd_words + g_golden_wsd_size;
    if ( bad == 0 )
        LOG_SUCCESS( "water_built_release / water_global_clear / water_built_destroy_water match VuWater 0x528410 / 0x527E80 / 0x528120 on "
                     "Hydroelectric's built water + %u point waves: %u waves drained, %d dtors in order, %d tree words, the pool, every "
                     "refcount and wave tree after",
            np, nl, static_cast< int >( dtors.size( ) ), words );
    else
        LOG_ERROR( "water_shutdown FAILED: %d differences", bad );
    return bad == 0;
}

// water_map_unload / water_map_destroy (world/query.hxx) against VuWaterMapAsset's unload 0x5C8780 and slot 0 0x5C7D00 on
// Hydroelectric's maps loaded from the pack (case water_map_unload). Compared: the loaded size and entry count, the CRT
// frees, VuAsset's dtor reached with the vtable 0x752498, the words +0x58 .. +0x8F after (each array as pointer / size /
// capacity: ours null and empty).
inline bool verify_water_map_unload( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "water_map_unload: the pack did not open" );
        return false;
    }
    int failed = 0;
    for ( std::size_t i = 0; i < g_golden_wmu_count; ++i )
    {
        const golden_water_map_unload_t& g = g_golden_wmu[ i ];
        std::vector< std::uint8_t > blob;
        water_map_t m;
        if ( !archive.read( ( std::string( "VuWaterMapAsset/" ) + g.name ).c_str( ), blob ) || !water_map_load( blob.data( ), blob.size( ), m ) )
        {
            ++failed;
            LOG_ERROR( "water_map_unload: %s did not load", g.name );
            continue;
        }
        int bad = m.width != g.before[ 0 ] || m.height != g.before[ 1 ] || m.entries.size( ) != g.before[ 2 ];
        int based = 0;
        const int frees = g.dtor ? water_map_destroy( m, []( void* u ) { ++*static_cast< int* >( u ); }, &based ) : water_map_unload( m );
        bad += frees != static_cast< int >( g.frees ) || ( g.dtor ? ( based != 1 || g.base != 0x752498u ) : g.base != 0u );
        auto arr = []( const auto& v, unsigned* w ) {
            w[ 0 ] = v.empty( ) ? 0u : 1u;
            w[ 1 ] = static_cast< unsigned >( v.size( ) );
            w[ 2 ] = static_cast< unsigned >( v.capacity( ) );
        };
        unsigned ours[ 14 ] = { m.width, m.height };
        arr( m.map, ours + 2 );
        arr( m.second, ours + 5 );
        arr( m.third, ours + 8 );
        arr( m.entries, ours + 11 );
        for ( int k = 0; k < 14; ++k )
            bad += ours[ k ] != g.after[ k ];
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "water_map_unload: %s (%s): %d differences", g.name, g.dtor ? "slot 0" : "unload", bad );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "water_map_unload / water_map_destroy match VuWaterMapAsset 0x5C8780 / 0x5C7D00 on %d loads of Hydroelectric's maps: "
                     "the frees, the base dtor, every word after",
            static_cast< int >( g_golden_wmu_count ) );
    else
        LOG_ERROR( "water_map_unload FAILED %d of %d", failed, static_cast< int >( g_golden_wmu_count ) );
    return failed == 0;
}

// water_build_world (world/surface.hxx) on the FRONT END's water against retail (case water_build_menu): Screens/Background's
// "Objects" with its water entities only - VuInfiniteOceanWaveEntity's slot 6 0x5D8970 (0x528A50, the ocean ctor 0x59CB10,
// vtable 0x7508C0), two rectangular oceans, one surface. Compared as verify_water_build: every water entity's loaded
// property and transform words, every surface's and wave's recreated words, the surface tree and the surface's wave tree
// node for node, the wave list, each wave's entries, the pool.
inline std::unique_ptr< entity_t > water_menu_document_tree( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_wbm_document );
    static const value_t document = c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_wbm_document_bytes ) ).parse( );
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    world.m_template = []( const std::string& ) -> const value_t* { return nullptr; };
    world.m_components_for = entity_components_constructed;
    world.m_component_load = entity_load_component;
    world.m_component_mark = entity_mark_component;
    world.m_state_for = entity_state_for;
    world.m_properties_load = entity_load_properties;
    world.m_properties_mark = entity_mark_properties;
    return entity_load_project( member( document, "VuProject" ), "Data/Projects/Background.vuprj", world );
}

inline bool verify_water_build_menu( )
{
    const std::unique_ptr< entity_t > tree = water_menu_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "water_build_menu: the document did not load" );
        return false;
    }
    water_built_t b{};
    water_build_world( *tree, b, []( entity_t& e ) { return entity_transform( e ); } );
    std::vector< entity_t* > order;
    const auto post = [ & ]( const auto& self, entity_t& e ) -> void {
        for ( auto& child : e.m_children )
            self( self, *child );
        if ( water_kind_of( e.m_type ) != water_kind_none )
            order.push_back( &e );
    };
    post( post, *tree );
    const unsigned* s = g_golden_wbm_words;
    int bad = 0, compared = 0;
    auto cmp = [ & ]( const char* what, int a, int k, std::uint32_t ours, std::uint32_t theirs ) {
        ++compared;
        if ( ours != theirs && ++bad <= 12 )
            LOG_ERROR( "water_build_menu %s %d word %d: ours 0x%08X retail 0x%08X", what, a, k, ours, theirs );
    };
    const unsigned ne = *s++;
    bad += order.size( ) != ne;
    for ( unsigned i = 0; i < ne; ++i, s += 1 + 57 + 44 + 2 )
    {
        if ( i >= order.size( ) )
            continue;
        entity_t& e = *order[ i ];
        const water_entity_state_t* st = static_cast< const water_entity_state_t* >( e.m_state.get( ) );
        const transform_component_t* t = entity_transform( e );
        if ( !st || !t || static_cast< unsigned >( water_kind_of( e.m_type ) ) != s[ 0 ] )
        {
            ++bad;
            LOG_ERROR( "water_build_menu: entity %u is %s (kind %u retail)", i, e.m_type.c_str( ), s[ 0 ] );
            continue;
        }
        for ( int k = 0; k < k_water_entity_words; ++k )
            if ( st->value.known[ k ] )
                cmp( "entity property", static_cast< int >( i ), 0x60 + 4 * k, st->value.word[ k ], s[ 1 + k ] );
        std::uint32_t words[ k_water_transform_words ];
        water_transform_words( *t, words );
        for ( int k = 0; k < k_water_transform_words; ++k )
            if ( k != 35 && k != 39 && k != 43 )
                cmp( "entity transform", static_cast< int >( i ), 0x10 + 4 * k, words[ k ], s[ 58 + k ] );
    }
    const unsigned ns = *s++;
    bad += b.surfaces.size( ) != ns;
    for ( unsigned i = 0; i < ns; ++i, s += 65 )
        for ( int k = 0; k < 65 && i < b.surfaces.size( ); ++k )
            if ( b.surfaces[ i ].known[ k ] )
                cmp( "surface", static_cast< int >( i ), 4 * k, b.surfaces[ i ].word[ k ], s[ k ] );
    const unsigned nw = *s++;
    bad += b.waves.size( ) != nw;
    for ( unsigned i = 0; i < nw; ++i )
    {
        const unsigned len = *s++;
        for ( unsigned k = 0; k < len && i < b.waves.size( ); ++k )
            if ( k < 64 && b.waves[ i ].known[ k ] && k != 64 / 4 )
                cmp( "wave", static_cast< int >( i ), static_cast< int >( 4 * k ), b.waves[ i ].word[ k ], s[ k ] );
        s += len;
    }
    const unsigned nt = *s++;
    bad += nt != 1 + b.hold.surfaces.size( );
    for ( unsigned i = 0; i < nt; ++i )
    {
        const water_tree_t& t = i == 0 ? b.hold.surface_tree : b.hold.surfaces[ i - 1 ].waves;
        std::vector< unsigned > w = water_tree_words( t );
        const unsigned len = 6 + 8 * s[ 5 ];
        for ( unsigned k = 0; k < len; ++k )
            cmp( "tree", static_cast< int >( i ), static_cast< int >( k ), k < w.size( ) ? w[ k ] : 0xDEADBEEFu, s[ k ] );
        bad += w.size( ) != len;
        s += len;
    }
    const unsigned nl = *s++;
    std::vector< int > listed;
    int e = b.hold.list;
    for ( unsigned k = 0; k < nl; ++k, s += 2 )
    {
        cmp( "list", static_cast< int >( k ), 0, static_cast< unsigned >( e ), s[ 0 ] );
        if ( e < 0 )
            break;
        cmp( "list", static_cast< int >( k ), 1, static_cast< unsigned >( b.hold.pool[ e ].leaf ), s[ 1 ] );
        e = b.hold.pool[ e ].next;
    }
    bad += e != -1;
    for ( unsigned i = 0; i < nw; ++i )
    {
        const unsigned m = *s++;
        int c = i < b.hold.waves.size( ) ? b.hold.waves[ i ].entries : -1;
        for ( unsigned j = 0; j < m; ++j, s += 3 )
        {
            cmp( "entry", static_cast< int >( i ), static_cast< int >( j ), static_cast< unsigned >( c ), s[ 0 ] );
            if ( c < 0 )
                break;
            cmp( "entry surface", static_cast< int >( i ), static_cast< int >( j ), static_cast< unsigned >( b.hold.pool[ c ].surface ), s[ 1 ] );
            cmp( "entry leaf", static_cast< int >( i ), static_cast< int >( j ), static_cast< unsigned >( b.hold.pool[ c ].leaf ), s[ 2 ] );
            c = b.hold.pool[ c ].next;
        }
        bad += c != -1;
    }
    cmp( "pool count", 0, 0, static_cast< unsigned >( b.hold.pool_count ), *s++ );
    cmp( "pool free", 0, 0, static_cast< unsigned >( b.hold.pool_free ), *s++ );
    bad += s != g_golden_wbm_words + g_golden_wbm_size || b.skipped != 0;
    if ( bad == 0 )
        LOG_SUCCESS( "water_build_world matches retail on the front end's water (Screens/Background: the infinite ocean 0x5D8970 / "
                     "0x528A50, two rectangular oceans, a surface): %d values",
            compared );
    else
        LOG_ERROR( "water_build_menu FAILED: %d differences", bad );
    return bad == 0;
}

// ocean_infinite_vertex (world/wave.hxx) against VuWaterInfiniteOceanWave's slot 3 0x59D900 -> SampleA 0x59E070 and, with
// ctx+100 set, SampleB 0x59E520 (case ocean_infinite: synthetic fields, wide-magnitude points, the rounding ties). Compared:
// every displacement / height word bit for bit (the +100 vectors behind the tag test, wanted tag 1).
inline bool verify_ocean_infinite( )
{
    const unsigned* s = g_golden_oin_words;
    const unsigned n = *s++;
    int failed = 0, live = 0, points = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned variant = *s++;
        ocean_patch_t k{};
        k.shift = static_cast< int >( *s++ );
        k.mask = ( 1 << k.shift ) - 1;
        std::memcpy( &k.scale, s++, 4 );
        std::memcpy( &k.sine, s++, 4 );
        std::memcpy( &k.cosine, s++, 4 );
        std::memcpy( &k.now, s, 8 );
        std::memcpy( &k.time[ 0 ], s + 2, 8 );
        std::memcpy( &k.time[ 1 ], s + 4, 8 );
        s += 6;
        k.current = static_cast< int >( *s++ );
        k.previous = static_cast< int >( *s++ );
        const unsigned np = *s++;
        k.patches[ 0 ] = reinterpret_cast< const float* >( s );
        k.patches[ 1 ] = reinterpret_cast< const float* >( s + np );
        s += 2 * np;
        const unsigned count = *s++;
        const unsigned* pts = s;
        const unsigned* tags = s + 2 * count;
        const unsigned* out = tags + count;
        s = out + 4 * count;
        int bad = 0;
        for ( unsigned i = 0; i < count; ++i )
        {
            float disp[ 3 ] = { 0.25f, -0.5f, 1.5f };
            float height = 2.0f;
            float x, y;
            std::memcpy( &x, pts + 2 * i, 4 );
            std::memcpy( &y, pts + 2 * i + 1, 4 );
            if ( !variant || tags[ i ] == 1u )
            {
                ocean_infinite_vertex( k, x, y, disp, height );
                ++live;
            }
            unsigned w[ 4 ];
            std::memcpy( w, disp, 12 );
            std::memcpy( w + 3, &height, 4 );
            for ( int j = 0; j < 4; ++j )
                bad += w[ j ] != out[ 4 * i + j ];
        }
        points += static_cast< int >( count );
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "ocean_infinite: vector %u (variant %u): %d words differ", v, variant, bad );
        }
    }
    if ( s != g_golden_oin_words + g_golden_oin_size )
    {
        ++failed;
        LOG_ERROR( "ocean_infinite: the golden words were not consumed exactly" );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ocean_infinite_vertex matches VuWaterInfiniteOceanWave 0x59E070 / 0x59E520 (slot 3 0x59D900): %u vectors, %d points, "
                     "%d evaluated, bit for bit",
            n, points, live );
    else
        LOG_ERROR( "ocean_infinite FAILED %d of %u", failed, n );
    return failed == 0;
}

// The infinite ocean's +96 kernels (case ocean_infinite96): slot 3 0x59D900 with ctx+0x60 set -> SampleHeight 0x59E9E0 and,
// with ctx+100, SampleHeight2 0x59EB80 - through water_view_kernel (kernel.hxx: ocean_infinite_vertex96) on 32-byte records.
// Compared: the record words +8..+0x1C (height, flow x / y, foam, the two pads) bit for bit.
inline bool verify_ocean_infinite96( )
{
    const unsigned* s = g_golden_oi96_words;
    const unsigned n = *s++;
    int failed = 0, live = 0, points = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned variant = *s++;
        water_view_t view{};
        view.kernels.resize( 1 );
        water_wave_kernel_t& kk = view.kernels[ 0 ];
        kk.kind = water_kind_infinite;
        ocean_patch_t& k = kk.ocean;
        k.shift = static_cast< int >( *s++ );
        k.mask = ( 1 << k.shift ) - 1;
        std::memcpy( &k.scale, s++, 4 );
        std::memcpy( &k.sine, s++, 4 );
        std::memcpy( &k.cosine, s++, 4 );
        std::memcpy( &k.now, s, 8 );
        std::memcpy( &k.time[ 0 ], s + 2, 8 );
        std::memcpy( &k.time[ 1 ], s + 4, 8 );
        s += 6;
        k.current = static_cast< int >( *s++ );
        k.previous = static_cast< int >( *s++ );
        const unsigned np = *s++;
        k.patches[ 0 ] = reinterpret_cast< const float* >( s );
        k.patches[ 1 ] = reinterpret_cast< const float* >( s + np );
        s += 2 * np;
        const unsigned count = *s++;
        const unsigned* pts = s;
        const unsigned* tags = s + 2 * count;
        const unsigned* out = tags + count;
        s = out + 6 * count;
        std::vector< unsigned > rec( 8 * count );
        std::vector< uint32_t > tag( count );
        for ( unsigned i = 0; i < count; ++i )
        {
            const float r[ 6 ] = { 0.0f, 0.0f, 2.0f, 0.25f, -0.5f, 1.5f };
            std::memcpy( &rec[ 8 * i ], r, sizeof( r ) );
            rec[ 8 * i ] = pts[ 2 * i ];
            rec[ 8 * i + 1 ] = pts[ 2 * i + 1 ];
            rec[ 8 * i + 6 ] = rec[ 8 * i + 7 ] = 0x77777777u;
            tag[ i ] = tags[ i ];
            live += !variant || tags[ i ] == 1u ? 1 : 0;
        }
        water_query_t ctx{};
        ctx.count = static_cast< int >( count );
        ctx.flag96 = 1;
        ctx.flag100 = variant;
        ctx.tags = tag.data( );
        ctx.tag = 1;
        ctx.records = reinterpret_cast< unsigned char* >( rec.data( ) );
        ctx.stride = 32;
        int bad = water_view_kernel( &view, 0, ctx ) ? 0 : 1;
        for ( unsigned i = 0; i < count; ++i )
            for ( int j = 0; j < 6; ++j )
                bad += rec[ 8 * i + 2 + j ] != out[ 6 * i + j ];
        points += static_cast< int >( count );
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "ocean_infinite96: vector %u (variant %u): %d words differ", v, variant, bad );
        }
    }
    if ( s != g_golden_oi96_words + g_golden_oi96_size )
    {
        ++failed;
        LOG_ERROR( "ocean_infinite96: the golden words were not consumed exactly" );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ocean_infinite_vertex96 (water_view_kernel's +96 infinite kind) matches VuWaterInfiniteOceanWave SampleHeight 0x59E9E0 / "
                     "SampleHeight2 0x59EB80 (slot 3 0x59D900): %u vectors, %d records, %d evaluated, bit for bit",
            n, points, live );
    else
        LOG_ERROR( "ocean_infinite96 FAILED %d of %u", failed, n );
    return failed == 0;
}

// THE FRONT END'S OCEAN FIELDS (case ocean_field_menu): water_build_world on Screens/Background's water (the same document
// as verify_water_build_menu), its three ocean waves (the infinite 0x528A50 -> 0x59CB10, n = 128; two rectangular) - the
// field arrays after the build, three water_built_tick, slot 3 in both modes (the infinite's +96 = SampleHeight 0x59E9E0)
// - compared as verify_ocean_field (world/field.hxx, kernel.hxx).
// The infinite ocean's slot 7 0x5D8A00 (case water_release_menu): water_entity_release on the front end's built water (the
// infinite ocean, then the two rectangular oceans' 0x5D8D30, the third with its refcount forced), compared as
// verify_water_release.
inline bool verify_water_release_menu( )
{
    const std::unique_ptr< entity_t > tree = water_menu_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "water_release_menu: the document did not load" );
        return false;
    }
    return water_release_check( *tree, g_golden_wrm_words, "water_release_menu", "the front end's" );
}

inline bool verify_ocean_field_menu( )
{
    const std::unique_ptr< entity_t > tree = water_menu_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "ocean_field_menu: the document did not load" );
        return false;
    }
    return ocean_field_check( *tree, OCEAN_FIELD_GOLDEN( "ocean_field_menu", ofm ), "the front end's" );
}

// VuStaticPfxEntity (world/pfxstatic.hxx) against retail (case static_pfx): the front end's five (Screens/Background's child
// 2 loaded as a project), each through the plan - slot 6 0x5EB060, the "Build" tick 0x5EB290, draws 0x5EB350 / 0x5EB370,
// plugs 0x5EB200 / 0x5EB220 / 0x5EB250, watchers 0x5EB4A0 / 0x5EB390, slot 7 0x5EB1A0 - on recording hooks that answer as
// retail's fakes did. Compared: the loaded properties and the transform's world matrix, every hook call with its arguments in
// order, and after each op the instance, the draw byte and the copied AABB words.
struct pfx_static_check_t
{
    std::vector< std::vector< unsigned > > log;
    const std::vector< pfx_static_entity_t >* entities = nullptr;
    unsigned create = 0, state = 0, count = 0;
    const unsigned* aabb = nullptr;
    static unsigned word( float f )
    {
        unsigned u;
        std::memcpy( &u, &f, 4 );
        return u;
    }
    unsigned ent( const void* e ) const
    {
        return static_cast< unsigned >( static_cast< const pfx_static_entity_t* >( e ) - entities->data( ) );
    }
};

inline unsigned pfx_static_string( const std::string& s )
{
    for ( unsigned i = 0; i < sizeof( g_golden_spx_strings ) / sizeof( g_golden_spx_strings[ 0 ] ); ++i )
        if ( s == g_golden_spx_strings[ i ] )
            return i;
    return 0xFFFFFFFEu;
}

inline bool verify_static_pfx_entity( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_spx_document );
    static const value_t document = c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_spx_document_bytes ) ).parse( );
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    world.m_template = []( const std::string& ) -> const value_t* { return nullptr; };
    world.m_components_for = entity_components_constructed;
    world.m_component_load = entity_load_component;
    world.m_component_mark = entity_mark_component;
    world.m_state_for = entity_state_for;
    world.m_properties_load = entity_load_properties;
    world.m_properties_mark = entity_mark_properties;
    const std::unique_ptr< entity_t > tree = entity_load_project( member( document, "VuProject" ), "Data/Projects/Background.vuprj", world );
    if ( !tree )
    {
        LOG_ERROR( "static_pfx: the document did not load" );
        return false;
    }
    std::vector< entity_t* > order;
    const auto post = [ & ]( const auto& self, entity_t& e ) -> void {
        for ( auto& child : e.m_children )
            self( self, *child );
        if ( e.m_type == "VuStaticPfxEntity" )
            order.push_back( &e );
    };
    post( post, *tree );
    const unsigned* s = g_golden_spx_words;
    const unsigned n = *s++;
    int bad = 0, compared = 0, calls = 0;
    auto cmp = [ & ]( const char* what, unsigned a, unsigned k, unsigned ours, unsigned theirs ) {
        ++compared;
        if ( ours != theirs && ++bad <= 12 )
            LOG_ERROR( "static_pfx %s %u / %u: ours 0x%08X retail 0x%08X", what, a, k, ours, theirs );
    };
    cmp( "entity count", 0, 0, static_cast< unsigned >( order.size( ) ), n );
    std::vector< pfx_static_entity_t > ents( n );
    std::vector< std::vector< float > > matrices( n, std::vector< float >( 16, 0.0f ) );
    pfx_static_check_t c;
    c.entities = &ents;
    static const char params[ 0x100 ] = {};
    pfx_static_ops_t o;
    o.user = &c;
    using C = pfx_static_check_t;
    static C* cc = nullptr;
    cc = &c;
    o.tick_register = []( void*, void* e, const char* phase ) { cc->log.push_back( { 0u, 2u, cc->ent( e ), pfx_static_string( phase ) } ); };
    o.tick_unregister = []( void*, void* e ) { cc->log.push_back( { 1u, 1u, cc->ent( e ) } ); };
    o.draw_add = []( void*, void* e ) { cc->log.push_back( { 2u, 1u, cc->ent( e ) } ); };
    o.draw_remove = []( void*, void* e ) { cc->log.push_back( { 3u, 1u, cc->ent( e ) } ); };
    o.draw_update = []( void*, void* e ) { cc->log.push_back( { 4u, 1u, cc->ent( e ) } ); };
    o.create = []( void*, const std::string& name ) -> std::uint32_t {
        cc->log.push_back( { 5u, 1u, pfx_static_string( name ) } );
        return cc->create == 0xFFFFFFFFu ? 0u : cc->create + 1u;
    };
    o.release = []( void*, std::uint32_t i ) { cc->log.push_back( { 6u, 1u, i - 1u } ); };
    o.set_matrix = []( void*, std::uint32_t i, const float* m ) {
        std::vector< unsigned > e{ 7u, 17u, i - 1u };
        for ( int k = 0; k < 16; ++k )
            e.push_back( C::word( m[ k ] ) );
        cc->log.push_back( e );
    };
    o.set_scale = []( void*, std::uint32_t i, float f ) { cc->log.push_back( { 8u, 2u, i - 1u, C::word( f ) } ); };
    o.set_color = []( void*, std::uint32_t i, const float* v ) {
        cc->log.push_back( { 9u, 5u, i - 1u, C::word( v[ 0 ] ), C::word( v[ 1 ] ), C::word( v[ 2 ] ), C::word( v[ 3 ] ) } );
    };
    o.state = []( void*, std::uint32_t i ) -> std::uint32_t {
        cc->log.push_back( { 10u, 1u, i - 1u } );
        return cc->state;
    };
    o.aabb = []( void*, std::uint32_t i ) -> const std::uint32_t* {
        cc->log.push_back( { 11u, 1u, i - 1u } );
        return cc->aabb;
    };
    o.count = []( void*, std::uint32_t i ) -> std::uint32_t {
        cc->log.push_back( { 12u, 1u, i - 1u } );
        return cc->count;
    };
    o.start = []( void*, std::uint32_t i ) { cc->log.push_back( { 13u, 1u, i - 1u } ); };
    o.stop = []( void*, std::uint32_t i, bool immediate ) { cc->log.push_back( { 14u, 2u, i - 1u, immediate ? 1u : 0u } ); };
    o.tick = []( void*, std::uint32_t i, float dt ) { cc->log.push_back( { 15u, 2u, i - 1u, C::word( dt ) } ); };
    o.draw = []( void*, std::uint32_t i, const void* p ) {
        cc->log.push_back( { 16u, 2u, i - 1u, static_cast< unsigned >( static_cast< const char* >( p ) - params ) } );
    };
    o.draw_shadow = []( void*, std::uint32_t i, const void* p ) {
        cc->log.push_back( { 17u, 2u, i - 1u, static_cast< unsigned >( static_cast< const char* >( p ) - params ) } );
    };
    std::vector< static_pfx_t > props( n );
    int ops = 0;
    for ( unsigned k = 0; k < n; ++k )
    {
        const unsigned name = *s++;
        const unsigned effect = *s++;
        const unsigned* loaded = s;
        const unsigned* matrix = s + 3;
        s += 3 + 16;
        const unsigned nops = *s++;
        if ( k < order.size( ) )
        {
            entity_t& e = *order[ k ];
            cmp( "name", k, 0, pfx_static_string( e.m_name ), name );
            const static_pfx_state_t* st = static_cast< const static_pfx_state_t* >( e.m_state.get( ) );
            const transform_component_t* tc = entity_transform( e );
            if ( st && tc )
            {
                props[ k ] = st->value;
                cmp( "effect", k, 0, pfx_static_string( props[ k ].effect_name ), effect );
                cmp( "loaded", k, 0, props[ k ].initially_active, loaded[ 0 ] );
                cmp( "loaded", k, 1, C::word( props[ k ].pfx_scale ), loaded[ 1 ] );
                cmp( "loaded", k, 2, props[ k ].pfx_color, loaded[ 2 ] );
                for ( int j = 0; j < 16; ++j )
                {
                    matrices[ k ][ static_cast< std::size_t >( j ) ] = tc->world[ j ];
                    cmp( "matrix", k, static_cast< unsigned >( j ), C::word( tc->world[ j ] ), matrix[ j ] );
                }
            }
            else
                cmp( "state", k, 0, 0, 1 );
        }
        pfx_static_entity_t& x = ents[ k ];
        x.properties = &props[ k ];
        x.matrix = matrices[ k ].data( );
        bool copied = false;
        for ( unsigned q = 0; q < nops; ++q, ++ops )
        {
            const unsigned op = s[ 0 ];
            c.create = s[ 1 ];
            c.state = s[ 2 ];
            c.count = s[ 3 ];
            c.aabb = s + 4;
            if ( op == 8 )
            {
                std::memcpy( &props[ k ].pfx_scale, &s[ 12 ], 4 );
                props[ k ].pfx_color = s[ 13 ];
            }
            const unsigned retval = s[ 14 ];
            const unsigned nlog = s[ 15 ];
            s += 16;
            c.log.clear( );
            unsigned rv = 0xDEADBEEFu;
            switch ( op )
            {
            case 0: pfx_static_game_initialize( x, o ); break;
            case 1: pfx_static_tick( x, o, 1.0f / 60.0f ); break;
            case 2: pfx_static_draw( x, o, params + 0x10 * k ); break;
            case 3: pfx_static_draw_shadow( x, o, params + 0x10 * k ); break;
            case 4: pfx_static_start( x, o ); rv = 0; break;
            case 5: pfx_static_stop( x, o ); rv = 0; break;
            case 6: pfx_static_kill( x, o ); rv = 0; break;
            case 7: pfx_static_moved( x, o ); break;
            case 8: pfx_static_changed( x, o ); break;
            default: pfx_static_game_release( x, o ); break;
            }
            if ( op >= 4 && op <= 6 )
                cmp( "retval", k, q, rv, retval );
            cmp( "log length", k, q, static_cast< unsigned >( c.log.size( ) ), nlog );
            for ( unsigned j = 0; j < nlog; ++j )
            {
                const unsigned kind = s[ 0 ], args = s[ 1 ];
                const std::vector< unsigned >* ours = j < c.log.size( ) ? &c.log[ j ] : nullptr;
                cmp( "log kind", k, q * 100 + j, ours ? ( *ours )[ 0 ] : 0xFFFFFFFFu, kind );
                for ( unsigned a = 0; a < args; ++a )
                    cmp( "log arg", k, q * 100 + j, ours && ( *ours )[ 1 ] == args ? ( *ours )[ 2 + a ] : 0xFFFFFFFFu, s[ 2 + a ] );
                s += 2 + args;
                ++calls;
            }
            cmp( "instance", k, q, x.instance ? x.instance - 1u : 0xFFFFFFFFu, s[ 0 ] );
            cmp( "drawn", k, q, x.drawn, s[ 1 ] );
            // before the first copy the words are the draw component's own (its ctor 0x531A40: not this entity's): not compared
            copied = copied || ( op == 1 && c.count != 0 && x.instance != 0 );
            for ( int j = 0; j < 8 && copied; ++j )
                cmp( "aabb", k, q, x.aabb[ j ], s[ 2 + j ] );
            s += 10;
        }
    }
    bad += s != g_golden_spx_words + g_golden_spx_size;
    if ( bad == 0 )
        LOG_SUCCESS( "VuStaticPfxEntity (world/pfxstatic.hxx: slot 6 0x5EB060, tick 0x5EB290, plugs 0x5EB200 / 0x5EB220 / 0x5EB250, "
                     "watchers 0x5EB390 / 0x5EB4A0, draws 0x5EB350 / 0x5EB370, slot 7 0x5EB1A0) matches retail on the front end's %u: %d ops, "
                     "%d hook calls, %d values",
            n, ops, calls, compared );
    else
        LOG_ERROR( "static_pfx FAILED: %d differences", bad );
    return bad == 0;
}

// pfx_asset_load / pfx_asset_unload / pfx_asset_destroy (world/pfxmgr.hxx) against VuPfxAsset's slot 5 0x5CAB30 / slot 6
// 0x5CAB80 / slot 0 0x5CAAD0 -> 0x5CAA30 (case pfx_unload): the race's two documents loaded into two assets, then four
// unloads (Hydroelectric; Generic with both globals null; Generic; Generic again), Hydroelectric loaded again, both destroyed. Compared per unload: the events in order (releaseAll 0x52BB60 - here: the one
// effect created before each unload with the manager present, released through ops.release -, 0x563C00's entries, the
// geom model resets, the texture releases by path and offset), the document cleared (retail's +0 / +8 / +0xC), the
// projects left.
inline bool verify_pfx_unload( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "pfx_unload: Base.apf did not open" );
        return false;
    }
    static pfx_runtime_t rt;
    pfx_runtime_init( rt );
    pfx_library_t lib;
    std::map< std::string, pfx_asset_t > assets;
    for ( const char* name : { "Hydroelectric", "Generic" } )
    {
        std::vector< std::uint8_t > blob;
        pfx_asset_t& a = assets[ name ];
        a.name = name;
        if ( !archive.read( ( std::string( "VuPfxAsset/" ) + name ).c_str( ), blob ) || !pfx_asset_load( &rt, &lib, a, blob ) )
        {
            LOG_ERROR( "pfx_unload: %s did not load", name );
            return false;
        }
    }
    struct log_t
    {
        std::map< std::uint32_t, std::string > path_of;
        std::vector< std::tuple< int, std::string, std::uint32_t > > events;
    } log;
    pfx_release_hooks_t hooks;
    hooks.user = &log;
    hooks.destroyed = []( void* u, std::uint32_t node ) {
        log_t& l = *static_cast< log_t* >( u );
        l.events.emplace_back( 0, l.path_of[ node ], 0u );
    };
    hooks.model = []( void* u, std::uint32_t node ) {
        log_t& l = *static_cast< log_t* >( u );
        l.events.emplace_back( 1, l.path_of[ node ], 0u );
    };
    hooks.texture = []( void* u, std::uint32_t node, std::uint32_t offset ) {
        log_t& l = *static_cast< log_t* >( u );
        l.events.emplace_back( 2, l.path_of[ node ], offset );
    };
    pfx_manager_ops_t ops;
    ops.user = &log;
    ops.create = []( void*, const char* ) { return 1u; };
    ops.release = []( void* u, std::uint32_t ) { static_cast< log_t* >( u )->events.emplace_back( 3, std::string( "manager" ), 0u ); };
    pfx_manager_t manager;
    pfx_manager_init( manager, ops );
    int failed = 0, events = 0;
    for ( std::size_t i = 0; i < g_golden_pul_op_count; ++i )
    {
        const golden_pfx_unload_op_t& op = g_golden_pul_ops[ i ];
        log.path_of.clear( );
        std::vector< std::pair< std::string, std::uint32_t > > rows;
        for ( const auto& pr : lib.projects )
            pfx_load_rows( rt, lib, pr.second, pr.first, rows );
        for ( const auto& r : rows )
            log.path_of[ r.second ] = r.first;
        log.events.clear( );
        unsigned ret = 0;
        if ( op.kind == 1 )
        {
            std::vector< std::uint8_t > blob;
            ret = archive.read( ( std::string( "VuPfxAsset/" ) + op.name ).c_str( ), blob ) && pfx_asset_load( &rt, &lib, assets[ op.name ], blob ) ? 1u : 0u;
        }
        else
        {
            if ( op.globals )
                pfx_manager_create( manager, "effect", ops );
            if ( op.kind == 0 )
                pfx_asset_unload( assets[ op.name ], op.globals ? &manager : nullptr, ops, op.globals ? &rt : nullptr, op.globals ? &lib : nullptr, hooks );
            else
                pfx_asset_destroy( assets[ op.name ], op.globals ? &manager : nullptr, ops, op.globals ? &rt : nullptr, op.globals ? &lib : nullptr, hooks,
                    []( void* u ) { static_cast< log_t* >( u )->events.emplace_back( 4, std::string( "asset" ), 0u ); }, &log );
        }
        int bad = log.events.size( ) != op.count || ret != op.ret;
        for ( unsigned e = 0; e < op.count && e < log.events.size( ); ++e )
        {
            const golden_pfx_unload_event_t& g = g_golden_pul_events[ op.first + e ];
            bad += std::get< 0 >( log.events[ e ] ) != g.kind || std::get< 1 >( log.events[ e ] ) != g.path || std::get< 2 >( log.events[ e ] ) != g.offset;
        }
        // retail's tag +0: 0 none, 6 an object (the loaded document's root)
        const value_t::kind_t kind = assets[ op.name ].document.kind;
        bad += ( op.doc[ 0 ] == 0u ) != ( kind == value_t::kind_t::none ) || ( op.doc[ 0 ] == 6u ) != ( kind == value_t::kind_t::object );
        std::string left;
        for ( const auto& pr : lib.projects )
            left += ( left.empty( ) ? "" : "," ) + pr.first;
        bad += left != op.projects;
        events += static_cast< int >( op.count );
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "pfx_unload: operation %d (%s): %d differences (%d events, retail %u)", static_cast< int >( i ), op.name, bad,
                static_cast< int >( log.events.size( ) ), op.count );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "pfx_asset_load / _unload / _destroy match VuPfxAsset's 0x5CAB30 / 0x5CAB80 (0x4FBB10, releaseAll 0x52BB60, removeProject "
                     "0x51D9C0) / 0x5CAAD0 -> 0x5CAA30: %d operations, %d events in order, the documents, the projects left",
            static_cast< int >( g_golden_pul_op_count ), events );
    else
        LOG_ERROR( "pfx_unload FAILED %d", failed );
    return failed == 0;
}

// pfx_impl_destroy (world/pfxload.hxx) against VuPfxImpl's slot 0 0x51C640 -> dtor 0x51C700 (case pfx_impl_teardown):
// VuPfxImpl as its ctor 0x51C4E0 / init 0x51C840 leave it (the pools' blocks null: their init is hooked there), deleted;
// and with Hydroelectric + Generic loaded, the blocks and render words set (inputs), not deleted. Compared: every object
// freed in order (by what it is), every render release with its word, the groups' refcounts after, no project left.
inline bool verify_pfx_impl_teardown( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "pfx_impl_teardown: Base.apf did not open" );
        return false;
    }
    struct ev_t
    {
        int kind;
        std::string what, name;
        std::uint32_t value;
    };
    std::vector< ev_t > log;
    pfx_impl_teardown_hooks_t hooks;
    hooks.user = &log;
    hooks.freed = []( void* u, bool array, const char* what, const std::string& name ) {
        static_cast< std::vector< ev_t >* >( u )->push_back( { array ? 1 : 0, what, name, 0u } );
    };
    hooks.shader = []( void* u, std::uint32_t v ) { static_cast< std::vector< ev_t >* >( u )->push_back( { 2, "", "", v } ); };
    hooks.asset = []( void* u, std::uint32_t v ) { static_cast< std::vector< ev_t >* >( u )->push_back( { 3, "", "", v } ); };
    int failed = 0, events = 0;
    for ( std::size_t i = 0; i < g_golden_pit_variant_count; ++i )
    {
        const golden_pfx_teardown_t& v = g_golden_pit_variants[ i ];
        static pfx_runtime_t rt;
        pfx_runtime_init( rt );
        pfx_library_t lib;
        if ( v.load )
        {
            for ( const char* name : { "Hydroelectric", "Generic" } )
            {
                std::vector< std::uint8_t > blob;
                if ( !archive.read( ( std::string( "VuPfxAsset/" ) + name ).c_str( ), blob ) || !pfx_load_asset( rt, lib, name, blob ) )
                {
                    LOG_ERROR( "pfx_impl_teardown: %s did not load", name );
                    return false;
                }
            }
        }
        else
            for ( int b = 0; b < 4; ++b )
                rt.heap[ rt.resources + 4u * b ] = 0u;
        std::map< std::string, std::uint32_t > groups( lib.projects.begin( ), lib.projects.end( ) );
        pfx_impl_render_t render;
        for ( int e = 0; e < 8; ++e )
        {
            render.quad[ e ][ 0 ] = v.render[ 2 * e ];
            render.quad[ e ][ 1 ] = v.render[ 2 * e + 1 ];
        }
        render.extra[ 0 ] = v.render[ 16 ];
        render.extra[ 1 ] = v.render[ 17 ];
        const std::size_t regions = rt.heap.regions.size( );
        log.clear( );
        pfx_impl_destroy( rt, lib, render, v.flags, hooks );
        int bad = log.size( ) != v.count;
        for ( unsigned e = 0; e < v.count && e < log.size( ); ++e )
        {
            const golden_pfx_teardown_event_t& g = g_golden_pit_events[ v.first + e ];
            bad += log[ e ].kind != g.kind || log[ e ].what != g.what || log[ e ].name != g.name || log[ e ].value != g.value;
        }
        std::string after;
        for ( const auto& g : groups )
            after += ( after.empty( ) ? "" : "," ) + g.first + ":" + std::to_string( rt.heap[ g.second + 4u ] );
        bad += after != v.groups || !lib.projects.empty( );
        // the resources and their blocks left the heap (the blocks only when they were allocated)
        bad += rt.heap.regions.size( ) != regions - ( v.load ? 5u : 1u );
        events += static_cast< int >( v.count );
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "pfx_impl_teardown: variant %d: %d differences (%d events, retail %u)", static_cast< int >( i ), bad,
                static_cast< int >( log.size( ) ), v.count );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "pfx_impl_destroy matches VuPfxImpl's 0x51C640 -> 0x51C700 (0x51DF10 / 0x51E2B0, 0x4CCCE0, 0x4CDB30): %d variants, "
                     "%d frees / render releases in order, the groups untouched",
            static_cast< int >( g_golden_pit_variant_count ), events );
    else
        LOG_ERROR( "pfx_impl_teardown FAILED %d", failed );
    return failed == 0;
}

// VuPfxEntity's draw delegates (world/pfxmgr.hxx pfx_entity_draw / _draw_shadow) against 0x552EF0 / 0x552F10 (case
// pfx_entity_draw): with and without a system instance, the slot reached, its this and its argument.
inline bool verify_pfx_entity_draw( )
{
    struct log_t
    {
        std::vector< unsigned > words;
        std::uint32_t instance = 0;
        const void* params = nullptr;
    } log;
    pfx_entity_draw_ops_t o{};
    o.user = &log;
    o.draw = []( void* u, std::uint32_t inst, const void* p ) {
        log_t& l = *static_cast< log_t* >( u );
        l.words.insert( l.words.end( ), { 14u, inst == l.instance ? 1u : 0u, p == l.params ? 1u : 0u } );
    };
    o.draw_shadow = []( void* u, std::uint32_t inst, const void* p ) {
        log_t& l = *static_cast< log_t* >( u );
        l.words.insert( l.words.end( ), { 15u, inst == l.instance ? 1u : 0u, p == l.params ? 1u : 0u } );
    };
    const unsigned* s = g_golden_ped_words;
    const unsigned n = *s++;
    int bad = 0;
    const int params = 0;
    for ( unsigned c = 0; c < n; ++c )
    {
        const unsigned fn = *s++, on = *s++, count = *s++;
        pfx_manager_entry_t x{};
        x.instance = on ? 0x20000200u : 0u;
        log.words.clear( );
        log.instance = x.instance;
        log.params = &params;
        if ( fn == 0x552EF0u )
            pfx_entity_draw( x, o, &params );
        else
            pfx_entity_draw_shadow( x, o, &params );
        bad += log.words != std::vector< unsigned >( s, s + 3 * count );
        s += 3 * count;
    }
    if ( bad == 0 )
        LOG_SUCCESS( "pfx_entity_draw / _draw_shadow match VuPfxEntity's draw delegates 0x552EF0 / 0x552F10: %u calls", n );
    else
        LOG_ERROR( "pfx_entity_draw FAILED %d of %u", bad, n );
    return bad == 0;
}
// pfx_sort_layer (world/pfxclass.hxx) against the layer the quad / trail renderers 0x58BD60 / 0x58D030 load for every
// authored (Blend Mode, Water Interaction) pair (case pfx_sort_layers).
inline bool verify_pfx_sort_layers( )
{
    int bad = 0;
    for ( unsigned i = 0; i < g_golden_psl_count; ++i )
    {
        const unsigned* v = g_golden_psl_words + 4 * i;
        if ( pfx_sort_layer( v[ 1 ], v[ 2 ] ) != v[ 3 ] )
        {
            ++bad;
            LOG_ERROR( "pfx_sort_layers: 0x%X blend %u water %u: ours %u, retail %u", v[ 0 ], v[ 1 ], v[ 2 ], pfx_sort_layer( v[ 1 ], v[ 2 ] ), v[ 3 ] );
        }
        bad += pfx_sort_layer_additive( v[ 3 ] ) != ( v[ 1 ] == 0u );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "pfx_sort_layer matches the quad / trail renderers' sort layer on %u (blend, water) pairs", g_golden_psl_count );
    else
        LOG_ERROR( "pfx_sort_layers FAILED %d", bad );
    return bad == 0;
}

// pfxclass.hxx pfx_quad_fill against retail's 0x58C340 (case pfx_quad_fill): every vertex word (position, colour, both uv
// pairs) of 80 synthetic draw records; the GPU half's calls are the case's fakes, not compared.
inline bool verify_pfx_quad_fill( )
{
    const unsigned* w = g_golden_pqf_words;
    const unsigned n = *w++;
    int bad = 0, verts = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        float cam[ 12 ] = { };
        for ( int k = 0; k < 3; ++k )
            for ( int j = 0; j < 3; ++j )
                std::memcpy( &cam[ 4 * k + j ], w + 3 * k + j, 4 );
        w += 9;
        std::vector< std::uint8_t > node( 0x94, 0 );
        std::memcpy( node.data( ) + 0x80, w, 20 );
        w += 5;
        const unsigned nr = *w++;
        std::vector< std::uint8_t > rec( 4 * nr );
        std::memcpy( rec.data( ), w, 4 * nr );
        w += nr;
        const unsigned nv = *w++;
        std::vector< pfx_quad_vertex_t > out( nv / 8 );
        const int count = pfx_quad_fill( rec.data( ), node.data( ), cam, out.data( ) );
        if ( static_cast< unsigned >( count ) * 32u != nv )
            ++bad;
        else if ( nv && std::memcmp( out.data( ), w, 4 * nv ) != 0 )
        {
            int diff = 0;
            for ( unsigned k = 0; k < nv; ++k )
                diff += std::memcmp( reinterpret_cast< const std::uint8_t* >( out.data( ) ) + 4 * k, w + k, 4 ) ? 1 : 0;
            if ( bad < 4 )
                LOG_ERROR( "pfx_quad_fill vector %u: %d of %u vertex words differ", v, diff, nv );
            ++bad;
        }
        verts += static_cast< int >( nv / 8 );
        w += nv;
    }
    if ( bad == 0 )
        LOG_SUCCESS( "The quad renderer's vertex fill (0x58C340, CPU half) matches retail: %u draw records, %d vertices word for word", n, verts );
    else
        LOG_ERROR( "pfx_quad_fill FAILED %d", bad );
    return bad == 0;
}

// world/pfxdraw.hxx against retail's 0x58BD60 / 0x58D030 (case pfx_submit): the record's words but the renderer (+0) on real particle lists, the buffer's used bytes, the command's key, callback, memory offset and type.
inline bool verify_pfx_submit( )
{
    struct hooks_t final : gfx_sort_hooks_t
    {
        gfx_material_t m;
        std::uint32_t device( int, std::uint32_t, std::uint32_t, std::uint32_t ) override { return 0u; }
        void program( std::uint32_t, int, std::uint32_t, std::uint32_t, const std::uint32_t* ) override { }
        std::uint32_t thread( int, std::uint32_t, std::uint32_t ) override { return 0u; }
        void profile( int ) override { }
        void composer( int, std::uint32_t, std::uint32_t ) override { }
        void water_flip( ) override { }
        void light_flip( ) override { }
        std::uint32_t texture_handle( std::uint32_t t ) override { return t; }
        void draw( std::uint32_t, std::uint8_t* ) override { }
        const gfx_material_t* material( std::uint32_t ) override { return &m; }
        const gfx_chunk_t* chunk( std::uint32_t ) override { return nullptr; }
    };
    const unsigned* w = g_golden_pst_words;
    const unsigned n = *w++, base = *w++, size = *w++;
    int bad = 0, commands = 0, words = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned trail = *w++, lo = *w++, hi = *w++, mkey = *w++, count = *w++, used = *w++, offset = *w++;
        gfx_camera_t camera;
        std::memcpy( camera.m_eye, w, 12 );
        std::memcpy( camera.m_direction, w + 3, 12 );
        std::memcpy( &camera.m_far, w + 6, 4 );
        w += 7;
        pfx_heap_t h;
        h.add( base, size );
        std::memcpy( h.find( base ), w, size );
        w += size / 4;
        auto hooks = std::make_unique< hooks_t >( );
        hooks->m.m_key = mkey;
        gfx_sort_t sort( *hooks );
        sort.reserve( );
        sort.m_buffers[ 0 ].m_used = 0x10;
        sort.m_key_lo = lo;
        sort.m_key_hi = hi;
        const std::uint32_t pattern = base + 0x100, node = h[ pattern + 0x10 ];
        const std::uint32_t materials[ 8 ] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        const bool made = trail ? pfx_trail_submit( sort, h, pattern, camera, 1u ) : pfx_quad_submit( sort, h, pattern, camera, materials, h[ h[ node + 0x9C ] + 0x4C ] != 0u );
        const unsigned got = static_cast< unsigned >( sort.m_commands[ 0 ].size( ) );
        bool same = made == ( count != 0u ) && got == count;
        if ( count )
        {
            const ui_gfx_command_t& c = sort.m_commands[ 0 ].front( );
            same = same && c.m_words[ 0 ] == w[ 0 ] && c.m_words[ 1 ] == w[ 1 ] && c.m_words[ 2 ] == w[ 2 ] && c.m_words[ 3 ] == w[ 3 ] && ( c.m_words[ 6 ] & 0xFFFFu ) == w[ 4 ];
            w += 5;
            same = same && static_cast< unsigned >( sort.m_buffers[ 0 ].m_used ) - 0x10u == used && static_cast< unsigned >( sort.m_offset ) == offset;
            ++commands;
        }
        const unsigned nr = *w++;
        if ( same && nr )
        {
            words += static_cast< int >( nr );
            const std::uint8_t* mine = sort.m_buffers[ 0 ].m_memory.data( ) + sort.m_offset;
            same = std::memcmp( mine + 4, w + 1, 4 * ( nr - 1 ) ) == 0;
        }
        w += nr;
        if ( !same )
        {
            if ( bad < 6 )
                LOG_ERROR( "pfx_submit vector %u (trail %u) differs", v, trail );
            ++bad;
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "The pfx renderers' submits (0x58BD60, 0x58D030) match retail: %u patterns, %d commands, %d record words", n, commands, words );
    else
        LOG_ERROR( "pfx_submit FAILED %d", bad );
    return bad == 0;
}

// pfxclass.hxx pfx_trail_fill against retail's 0x58D3F0 (case pfx_trail_fill): every vertex word (position, colour, u, v)
// of 120 synthetic draw records (80 2D Trail, 40 3D Ribbon), and the strip's primitive count / the 3D Ribbon's vt+0xAC
// pair from the GPU half's logged calls.
inline bool verify_pfx_trail_fill( )
{
    const unsigned* w = g_golden_ptf_words;
    const unsigned n = *w++;
    int bad = 0, verts = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        std::uint8_t node[ 0xA0 ] = { };
        std::memcpy( node + 0x3C, w++, 4 );
        float eye[ 3 ];
        std::memcpy( eye, w, 12 );
        w += 3;
        const unsigned nr = *w++;
        std::vector< std::uint8_t > rec( 4 * nr );
        std::memcpy( rec.data( ), w, 4 * nr );
        w += nr;
        const unsigned nv = *w++;
        std::vector< pfx_trail_vertex_t > out( nv / 6 + 1 );
        const int written = pfx_trail_fill( rec.data( ), node, eye, out.data( ) );
        if ( static_cast< unsigned >( written ) * 6u != nv )
            ++bad;
        else if ( nv && std::memcmp( out.data( ), w, 4 * nv ) != 0 )
        {
            int diff = 0;
            for ( unsigned k = 0; k < nv; ++k )
                diff += std::memcmp( reinterpret_cast< const std::uint8_t* >( out.data( ) ) + 4 * k, w + k, 4 ) ? 1 : 0;
            if ( bad < 4 )
                LOG_ERROR( "pfx_trail_fill vector %u: %d of %u vertex words differ", v, diff, nv );
            ++bad;
        }
        verts += static_cast< int >( nv / 6 );
        w += nv;
        std::int32_t count;
        std::memcpy( &count, rec.data( ) + 0x68, 4 );
        const unsigned nd = *w++;
        // the draw vt+0xC0( 4, 2 count - 2, 0x799898, 0x18 )
        if ( nd != 4 || w[ 0 ] != 4u || w[ 1 ] != static_cast< unsigned >( 2 * count - 2 ) || w[ 2 ] != 0x799898u || w[ 3 ] != 0x18u )
            ++bad;
        w += nd;
        const unsigned na = *w++;
        std::int32_t type;
        std::memcpy( &type, node + 0x3C, 4 );
        if ( type != 0 ? ( na != 2 || w[ 0 ] != 0u || w[ 1 ] != 1u ) : na != 0 )
            ++bad;
        w += na;
    }
    if ( bad == 0 )
        LOG_SUCCESS( "The trail renderer's vertex fill (0x58D3F0, CPU half) matches retail: %u draw records, %d vertices word for word", n, verts );
    else
        LOG_ERROR( "pfx_trail_fill FAILED %d", bad );
    return bad == 0;
}

inline bool verify_pfx_all( )
{
    bool ok = true;
    ok = verify_pfx_trail_fill( ) && ok;
    ok = verify_pfx_quad_fill( ) && ok;
    ok = verify_pfx_submit( ) && ok;
    ok = verify_pfx_sort_layers( ) && ok;
    ok = verify_pfx_impl_teardown( ) && ok;
    ok = verify_pfx_entity_draw( ) && ok;
    ok = verify_pfx_calls( ) && ok;
    ok = verify_pfx_unload( ) && ok;
    ok = verify_ocean_infinite( ) && ok;
    ok = verify_ocean_infinite96( ) && ok;
    ok = verify_ocean_field_menu( ) && ok;
    ok = verify_water_release_menu( ) && ok;
    ok = verify_static_pfx_entity( ) && ok;
    ok = verify_water_build_menu( ) && ok;
    ok = verify_water_map_unload( ) && ok;
    ok = verify_water_shutdown( ) && ok;
    ok = verify_point_entity( ) && ok;
    ok = verify_water_surface_release( ) && ok;
    ok = verify_water_release( ) && ok;
    ok = verify_pfx_reload( ) && ok;
    ok = verify_water_snap( ) && ok;
    ok = verify_pfx_boats( ) && ok;
    ok = verify_pfx_manager( ) && ok;
    ok = verify_pfx_manager_dtor( ) && ok;
    ok = verify_pfx_load( ) && ok;
    ok = verify_water_types( ) && ok;
    ok = verify_pfx_run( ) && ok;
    ok = verify_pfx_trail( ) && ok;
    ok = verify_pfx_lifecycle( ) && ok;
    ok = verify_pattern_draw( ) && ok;
    ok = verify_fountain_init( ) && ok;
    return ok;
}
