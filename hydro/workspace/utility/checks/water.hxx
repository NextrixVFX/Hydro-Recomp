#pragma once

#include <workspace/core/world/tree.hxx>
#include <workspace/core/world/kernel.hxx>
#include <workspace/core/world/spray.hxx>
#include <workspace/core/world/pfxclass.hxx>

#include <map>
#include <string>

// The water area's oracle-backed checks (one file per parallel agent; included by check.hxx after every
// shared check, so anything above is usable here). verify_water_all runs them all.

// ocean_patch_vertex against the rectangular and circular ocean waves' base kernels @0x59ed30 / @0x59fed0
// and their ctx+100 variants @0x59f2b0 / @0x5a03f0, on hand-built double-buffered patch fields. Every
// operation is IEEE-exact (mul, add, sub, div, sqrtss, the double divide and its narrowing), so the bar is
// bit exactness. The +100 variants run the same body only where the vertex's tag equals ctx+92 (1 here);
// the walk below is that filter.
inline bool verify_ocean_kernels( )
{
    int checked = 0;
    int failed = 0;
    int live = 0;
    for ( std::size_t index = 0; index < g_golden_ocean_kernels_count; ++index )
    {
        const golden_ocean_kernel_t& g = g_golden_ocean_kernels[ index ];
        ocean_patch_t wave{};
        wave.shift = g.m_shift;
        wave.mask = ( 1 << g.m_shift ) - 1;
        wave.scale = g.m_scale;
        wave.sine = g.m_sine;
        wave.cosine = g.m_cosine;
        wave.now = g.m_now;
        wave.current = g.m_current;
        wave.previous = g.m_previous;
        wave.time[ 0 ] = g.m_times[ 0 ];
        wave.time[ 1 ] = g.m_times[ 1 ];
        wave.patches[ 0 ] = g_golden_ocean_patches + g.m_patches;
        wave.patches[ 1 ] = g_golden_ocean_patches + g.m_patches + g.m_patch_count;
        wave.centre_x = g.m_centre[ 0 ];
        wave.centre_y = g.m_centre[ 1 ];
        for ( int k = 0; k < 4; ++k )
            wave.shape[ k ] = g.m_shape[ k ];
        for ( int i = 0; i < g.m_point_count; ++i )
        {
            float displacement[ 3 ] = { 0.25f, -0.5f, 1.5f };
            float height = 2.0f;
            if ( !g.m_variant || g_golden_ocean_tags[ g.m_tags + i ] == 1u )
            {
                ocean_patch_vertex( wave, g.m_kind == 1, g_golden_ocean_points[ g.m_points + 2 * i ],
                    g_golden_ocean_points[ g.m_points + 2 * i + 1 ], displacement, height );
            }
            const float ours[ 4 ] = { displacement[ 0 ], displacement[ 1 ], displacement[ 2 ], height };
            const float* want = g_golden_ocean_out + g.m_out + 4 * i;
            ++checked;
            if ( want[ 3 ] != 2.0f )
                ++live;
            if ( std::memcmp( ours, want, sizeof( ours ) ) != 0 )
            {
                ++failed;
                if ( failed <= 6 )
                {
                    LOG_ERROR( "ocean_patch_vertex %s variant %d point %.6g %.6g: ours %.9g %.9g retail %.9g %.9g",
                        g.m_kind ? "circular" : "rectangular", g.m_variant, g_golden_ocean_points[ g.m_points + 2 * i ],
                        g_golden_ocean_points[ g.m_points + 2 * i + 1 ], ours[ 2 ], ours[ 3 ], want[ 2 ], want[ 3 ] );
                }
            }
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "ocean_patch_vertex matches the rectangular / circular ocean kernels @0x59ed30 / @0x59fed0 (+100 @0x59f2b0 / @0x5a03f0) bit for bit on %d vertices (%d producing output)",
            checked, live );
    }
    else
    {
        LOG_ERROR( "ocean_patch_vertex FAILED %d of %d vertices against the ocean kernels", failed, checked );
    }
    return failed == 0;
}

inline wake_record_t wake_record( const float* f )
{
    wake_record_t r{};
    std::memcpy( &r, f, sizeof( r ) );
    return r;
}

// wake_vertex against VuWaterWakeWave's base kernel @0x52ca60 and its ctx+100 variant @0x52ce20, the flat
// wake's slot 3 @0x52ca30 with ctx+96 clear (it must leave every vertex alone), and wake_tick against the
// shared tick @0x52c210 (al and both ages). Bit exact; the sine is fsincos under Unicorn (see wave.hxx).
inline bool verify_wake_kernels( )
{
    int checked = 0;
    int failed = 0;
    int live = 0;
    for ( std::size_t index = 0; index < g_golden_wake_kernels_count; ++index )
    {
        const golden_wake_kernel_t& g = g_golden_wake_kernels[ index ];
        wake_wave_t wave{};
        wave.inner_ratio = g.m_head[ 0 ];
        wave.outer_ratio = g.m_head[ 1 ];
        wave.scale = g.m_head[ 2 ];
        wave.a = wake_record( g.m_a );
        wave.b = wake_record( g.m_b );
        for ( int i = 0; i < g.m_point_count; ++i )
        {
            float displacement[ 3 ] = { 0.25f, -0.5f, 1.5f };
            float height = 2.0f;
            if ( !g.m_flat && ( !g.m_variant || g_golden_wake_tags[ g.m_tags + i ] == 1u ) )
            {
                wake_vertex( wave, g_golden_wake_points[ g.m_points + 2 * i ],
                    g_golden_wake_points[ g.m_points + 2 * i + 1 ], displacement, height );
            }
            const float ours[ 4 ] = { displacement[ 0 ], displacement[ 1 ], displacement[ 2 ], height };
            const float* want = g_golden_wake_out + g.m_out + 4 * i;
            ++checked;
            if ( want[ 3 ] != 2.0f )
                ++live;
            if ( std::memcmp( ours, want, sizeof( ours ) ) != 0 )
            {
                ++failed;
                if ( failed <= 6 )
                {
                    LOG_ERROR( "wake_vertex flat %d variant %d point %.6g %.6g: ours %.9g retail %.9g", g.m_flat,
                        g.m_variant, g_golden_wake_points[ g.m_points + 2 * i ],
                        g_golden_wake_points[ g.m_points + 2 * i + 1 ], ours[ 3 ], want[ 3 ] );
                }
            }
        }
    }
    int ticks_failed = 0;
    for ( std::size_t index = 0; index < g_golden_wake_ticks_count; ++index )
    {
        const golden_wake_tick_t& g = g_golden_wake_ticks[ index ];
        wake_wave_t wave{};
        wave.a = wake_record( g.m_a );
        wave.b = wake_record( g.m_b );
        const bool alive = wake_tick( wave, g.m_mode, g.m_dt );
        const float ages[ 2 ] = { wave.a.age, wave.b.age };
        if ( alive != ( g.m_alive != 0 ) || std::memcmp( ages, g.m_ages, sizeof( ages ) ) != 0 )
        {
            ++ticks_failed;
            LOG_ERROR( "wake_tick mode %d dt %.6g: ours %d %.9g %.9g retail %d %.9g %.9g", g.m_mode, g.m_dt, alive,
                ages[ 0 ], ages[ 1 ], g.m_alive, g.m_ages[ 0 ], g.m_ages[ 1 ] );
        }
    }
    if ( failed == 0 && ticks_failed == 0 )
    {
        LOG_SUCCESS( "wake_vertex matches VuWaterWakeWave's kernels @0x52ca60 / +100 @0x52ce20 and the flat wake's @0x52ca30 bit for bit on %d vertices (%d producing output); wake_tick matches @0x52c210 on %d ticks",
            checked, live, static_cast< int >( g_golden_wake_ticks_count ) );
    }
    else
    {
        LOG_ERROR( "wake kernels FAILED %d of %d vertices, %d ticks", failed, checked, ticks_failed );
    }
    return failed == 0 && ticks_failed == 0;
}

// The wave entities' wave creation against retail's slot 6 on every rectangular ocean, circular ocean,
// banked turn, whirlpool, directional and bump entity of Hydroelectric (loaded by retail's VuEntity::load):
// every word the recreated descriptor + constructor defines must equal the word retail's wave object holds -
// for the directional and bump that includes both 16-word frames (the matrix and its inverse).
inline bool verify_wave_bridges( )
{
    int checked = 0;
    int words = 0;
    int failed = 0;
    int skipped = 0;
    for ( std::size_t index = 0; index < g_golden_wave_bridges_count; ++index )
    {
        const golden_wave_bridge_t& g = g_golden_wave_bridges[ index ];
        wave_source_t s{};
        s.entity = g.m_entity;
        s.transform = g.m_transform;
        wave_image_t w{};
        switch ( g.m_kind )
        {
        case 0: w = rectangular_ocean_create( s ); break;
        case 1: w = circular_ocean_create( s ); break;
        case 2: w = banked_turn_create( s ); break;
        case 3: w = whirlpool_create( s ); break;
        case 4: w = directional_create( s ); break;
        case 5: w = bump_create( s ); break;
        default: ++skipped; continue;
        }
        ++checked;
        int bad = 0;
        for ( int k = 0; k < 64; ++k )
        {
            if ( !w.known[ k ] )
                continue;
            ++words;
            if ( k >= g.m_words || w.word[ k ] != g.m_wave[ k ] )
            {
                if ( ++bad <= 3 )
                {
                    LOG_ERROR( "wave creation %s (kind %d) +%d: ours 0x%08X retail 0x%08X", g.m_name, g.m_kind, 4 * k,
                        w.word[ k ], k < g.m_words ? g.m_wave[ k ] : 0u );
                }
            }
        }
        if ( bad )
            ++failed;
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "the wave entities' creation (0x5d8ce0 / 0x5d9340 / 0x5d35f0 / 0x5d7d90 / 0x5d9d40 / 0x5d3e70 -> the wave ctors) matches retail on %d Hydroelectric entities, %d words (%d of an unknown kind)",
            checked, words, skipped );
    }
    else
    {
        LOG_ERROR( "wave creation FAILED on %d of %d entities", failed, checked );
    }
    return failed == 0;
}

// water_tick against VuWater's "Water" tick @0x529e30 on fake wave lists: the exact event sequence (each
// wave's slot 2 with its scaled dt, the tree removals, the destructor calls, the renderer's kick) and the
// final list, pool and refcounts. The waves' answers are the scenario's inputs.
struct water_tick_record_t
{
    const float* specs = nullptr;
    std::vector< int > cursor;
    std::vector< std::uint32_t > events;
};

inline bool verify_water_tick( )
{
    int failed = 0;
    int events_total = 0;
    for ( std::size_t index = 0; index < g_golden_water_tick_count; ++index )
    {
        const golden_water_tick_t& g = g_golden_water_tick[ index ];
        water_tick_record_t rec{};
        rec.specs = g_golden_water_tick_specs + g.m_spec;
        rec.cursor.assign( g.m_specs, 0 );
        std::vector< float > scale( g.m_specs );
        std::vector< int > refs( g.m_specs );
        for ( int k = 0; k < g.m_specs; ++k )
        {
            scale[ k ] = rec.specs[ 5 * k ];
            refs[ k ] = static_cast< int >( rec.specs[ 5 * k + 1 ] );
        }
        const int* state = g_golden_water_tick_state + g.m_state;
        water_list_t list{};
        list.nodes.resize( g.m_nodes + 2 );
        for ( int i = 0; i < g.m_nodes; ++i )
            list.nodes[ i ] = { state[ i ], i + 1 < g.m_nodes ? i + 1 : -1 };
        list.nodes[ g.m_nodes ] = { 0, g.m_nodes + 1 };
        list.nodes[ g.m_nodes + 1 ] = { 0, -1 };
        list.used = g.m_nodes ? 0 : -1;
        list.free = g.m_nodes;
        list.free_count = 2;
        water_tick_hooks_t hooks{};
        hooks.user = &rec;
        hooks.tick = []( void* u, int wave, float dt ) {
            auto& r = *static_cast< water_tick_record_t* >( u );
            std::uint32_t bits;
            std::memcpy( &bits, &dt, 4 );
            r.events.insert( r.events.end( ), { 0u, static_cast< std::uint32_t >( wave ), bits } );
            const int answer = static_cast< int >( r.specs[ 5 * wave + 3 + r.cursor[ wave ]++ ] );
            return answer != 0;
        };
        hooks.remove = []( void* u, int wave ) {
            static_cast< water_tick_record_t* >( u )->events.insert(
                static_cast< water_tick_record_t* >( u )->events.end( ), { 1u, static_cast< std::uint32_t >( wave ), 1u } );
        };
        hooks.destroy = []( void* u, int wave, int flag ) {
            static_cast< water_tick_record_t* >( u )->events.insert( static_cast< water_tick_record_t* >( u )->events.end( ),
                { 2u, static_cast< std::uint32_t >( wave ), static_cast< std::uint32_t >( flag ) } );
        };
        hooks.render = []( void* u ) {
            static_cast< water_tick_record_t* >( u )->events.insert(
                static_cast< water_tick_record_t* >( u )->events.end( ), { 3u, 0xFFFFFFFFu, 1u } );
        };
        water_tick( list, scale.data( ), refs.data( ), g.m_dt, hooks );

        auto node_id = [ & ]( int n ) { return n < 0 ? -1 : ( n >= g.m_nodes ? 100 + ( n - g.m_nodes ) : n ); };
        bool ok = rec.events.size( ) == static_cast< std::size_t >( 3 * g.m_events ) &&
                  std::memcmp( rec.events.data( ), g_golden_water_tick_events + g.m_event,
                      rec.events.size( ) * sizeof( std::uint32_t ) ) == 0;
        ok = ok && list.free_count == g.m_free_count && node_id( list.free ) == g.m_free_head &&
             node_id( list.used ) == g.m_used_head;
        for ( int i = 0; i < g.m_nodes && ok; ++i )
            ok = node_id( list.nodes[ i ].next ) == state[ g.m_nodes + i ];
        for ( int k = 0; k < g.m_specs && ok; ++k )
            ok = refs[ k ] == state[ 2 * g.m_nodes + k ];
        events_total += g.m_events;
        if ( !ok )
        {
            ++failed;
            LOG_ERROR( "water_tick scenario %d differs from @0x529e30 (%d events ours, %d retail)",
                static_cast< int >( index ), static_cast< int >( rec.events.size( ) / 3 ), g.m_events );
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "water_tick matches VuWater's Water tick @0x529e30 on %d scenarios (%d events, list / pool / refcounts)",
            static_cast< int >( g_golden_water_tick_count ), events_total );
    }
    else
    {
        LOG_ERROR( "water_tick FAILED %d of %d scenarios", failed, static_cast< int >( g_golden_water_tick_count ) );
    }
    return failed == 0;
}

// The waves' slot 2 ticks against retail's: al, the members each tick writes (as words), and the calls it
// makes - the point wave's one-shot callback and the ocean's four rebuild calls.
inline bool verify_wave_ticks( )
{
    int failed = 0;
    for ( std::size_t index = 0; index < g_golden_wave_ticks_count; ++index )
    {
        const golden_wave_tick_t& g = g_golden_wave_ticks[ index ];
        int calls = 0;
        auto count = []( void* u ) { ++*static_cast< int* >( u ); };
        bool alive = false;
        bool ok = true;
        auto word = [ & ]( int offset ) { return g.m_after[ offset / 4 ]; };
        auto same = [ & ]( int offset, float value ) {
            std::uint32_t bits;
            std::memcpy( &bits, &value, 4 );
            return bits == word( offset );
        };
        switch ( g.m_kind )
        {
        case 0:
        {
            float clock = g.m_in[ 0 ];
            alive = whirlpool_tick( clock, g.m_refs, g.m_dt );
            ok = same( 0x74, clock );
            break;
        }
        case 1:
        {
            float clock = g.m_in[ 1 ];
            alive = directional_tick( clock, g.m_refs, g.m_dt );
            ok = same( 0x7C, clock );
            break;
        }
        case 2: alive = static_wave_tick( g.m_refs ); break;
        case 3:
        {
            point_wave_life_t w{};
            w.rate = g.m_in[ 1 ];
            w.tail_width = g.m_in[ 2 ];
            w.outer_radius = g.m_in[ 3 ];
            w.elapsed = g.m_in[ 4 ];
            w.has_callback = g.m_point[ 0 ] != 0;
            w.expired = g.m_point[ 1 ] != 0;
            alive = point_wave_tick( w, g.m_refs, g.m_dt, count, &calls );
            ok = same( 0x78, w.elapsed ) && ( word( 0x90 ) & 0xFFu ) == ( w.expired ? 1u : 0u );
            break;
        }
        default:
        {
            ocean_clock_t w{};
            w.time = g.m_ocean[ 0 ];
            w.buffer_time[ 0 ] = g.m_ocean[ 1 ];
            w.buffer_time[ 1 ] = g.m_ocean[ 2 ];
            w.current = g.m_index[ 0 ];
            w.previous = g.m_index[ 1 ];
            int rebuilds = 0;
            alive = ocean_tick( w, g.m_refs, g.m_dt, count, &rebuilds );
            calls = rebuilds * 4;   // retail's four rebuild calls per rebuild
            double t;
            double b0;
            double b1;
            std::memcpy( &t, &g.m_after[ 0x88 / 4 ], 8 );
            std::memcpy( &b0, &g.m_after[ 0xB0 / 4 ], 8 );
            std::memcpy( &b1, &g.m_after[ 0xC0 / 4 ], 8 );
            ok = std::memcmp( &t, &w.time, 8 ) == 0 && std::memcmp( &b0, &w.buffer_time[ 0 ], 8 ) == 0 &&
                 std::memcmp( &b1, &w.buffer_time[ 1 ], 8 ) == 0 &&
                 word( 0xA4 ) == static_cast< std::uint32_t >( w.current ) &&
                 word( 0xA8 ) == static_cast< std::uint32_t >( w.previous );
            break;
        }
        }
        ok = ok && alive == ( g.m_al != 0 ) && calls == g.m_calls;
        if ( !ok )
        {
            ++failed;
            LOG_ERROR( "wave tick kind %d refs %d dt %.6g: ours al %d calls %d, retail al %d calls %d", g.m_kind, g.m_refs,
                g.m_dt, alive, calls, g.m_al, g.m_calls );
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "the wave ticks match retail's slot 2s (@0x5a1680 / @0x59aad0 / @0x59c380 / @0x5a34b0 / @0x59cc00) on %d samples",
            static_cast< int >( g_golden_wave_ticks_count ) );
    }
    else
    {
        LOG_ERROR( "the wave ticks FAILED %d of %d samples", failed, static_cast< int >( g_golden_wave_ticks_count ) );
    }
    return failed == 0;
}

// The pfx tick nodes' per-particle bodies against retail's slot 1s on 12 linked particles per sample: every
// particle's 32 words after the tick (the five members the bodies write, the rest untouched).
inline bool verify_pfx_ticks( )
{
    int failed = 0;
    int particles = 0;
    for ( std::size_t index = 0; index < g_golden_pfx_ticks_count; ++index )
    {
        const golden_pfx_tick_t& g = g_golden_pfx_ticks[ index ];
        int bad = 0;
        for ( int i = 0; i < 12; ++i )
        {
            std::uint32_t words[ 32 ];
            std::memcpy( words, g_golden_pfx_tick_words + g.m_words + 32 * i, sizeof( words ) );
            auto get = [ & ]( int offset ) { return wave_bits( words[ offset / 4 ] ); };
            auto put = [ & ]( int offset, float value ) { std::memcpy( &words[ offset / 4 ], &value, 4 ); };
            pfx_tick_particle_t p{ { get( 0x18 ), get( 0x1C ), get( 0x20 ) }, get( 0x34 ), get( 0x38 ), get( 0x3C ), get( 0x4C ) };
            switch ( g.m_kind )
            {
            case 0: pfx_tick_dampen( p, g.m_params, g.m_dt ); break;
            case 1: pfx_tick_accelerate( p, g.m_params, g.m_dt ); break;
            case 2: pfx_tick_scale( p, g.m_params, g.m_dt ); break;
            case 3: pfx_tick_alpha( p, g.m_params, g.m_dt ); break;
            case 4: pfx_tick_alpha_in_out( p, g.m_params, g.m_dt ); break;
            default: pfx_tick_world_scale_z( p, g.m_params, g.m_dt ); break;
            }
            put( 0x18, p.velocity[ 0 ] );
            put( 0x1C, p.velocity[ 1 ] );
            put( 0x20, p.velocity[ 2 ] );
            put( 0x34, p.alpha );
            put( 0x38, p.scale );
            put( 0x3C, p.age );
            put( 0x4C, p.world_scale_z );
            ++particles;
            if ( std::memcmp( words, g_golden_pfx_tick_words + g.m_words + 32 * 12 + 32 * i, sizeof( words ) ) != 0 )
                ++bad;
        }
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "pfx tick kind %d dt %.6g: %d of 12 particles differ from retail", g.m_kind, g.m_dt, bad );
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "the pfx tick nodes (dampen @0x58e4b0, acceleration @0x58e810, scale @0x590da0, alpha @0x591360, alpha in/out @0x5910e0, world scale z @0x590b40) match retail on %d samples, %d particles",
            static_cast< int >( g_golden_pfx_ticks_count ), particles );
    }
    else
    {
        LOG_ERROR( "the pfx tick nodes FAILED %d of %d samples", failed, static_cast< int >( g_golden_pfx_ticks_count ) );
    }
    return failed == 0;
}

// The wave frame's matrix helpers against retail: 0x40b5f0 (rotation about z, x87 fsincos under Unicorn),
// VuMatrix_fromScale @0x407db0 and VuMatrix_identity @0x410a40 - all 16 words of each output.
inline bool verify_matrix_helpers( )
{
    int failed = 0;
    for ( std::size_t index = 0; index < g_golden_matrix_helpers_count; ++index )
    {
        const golden_matrix_helper_t& g = g_golden_matrix_helpers[ index ];
        float m[ 16 ];
        if ( g.m_fn == 0 )
            wave_matrix_rotation_z( m, g.m_in[ 0 ] );
        else if ( g.m_fn == 1 )
            wave_matrix_scale( m, g.m_in );
        else
            wave_matrix_identity( m );
        if ( std::memcmp( m, g.m_words, 64 ) != 0 )
        {
            ++failed;
            LOG_ERROR( "matrix helper %d on %.9g differs from retail", g.m_fn, g.m_in[ 0 ] );
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "the wave frame's matrix helpers (0x40b5f0 / 0x407db0 / 0x410a40) match retail on %d outputs, all 16 words",
            static_cast< int >( g_golden_matrix_helpers_count ) );
    }
    else
    {
        LOG_ERROR( "the matrix helpers FAILED %d of %d", failed, static_cast< int >( g_golden_matrix_helpers_count ) );
    }
    return failed == 0;
}

// water_tree_* against VuWater's 2D tree (ctor @0x598c70, chunk growth @0x598e50, leaf insert @0x598de0 ->
// @0x599010 / @0x598f00, removal @0x599220) on 170 operations crossing two chunk growths: every operation's
// return and, at the end, every node (rect words, parent, children, tag - stale words of freed nodes
// included) and the header.
inline bool verify_water_tree( )
{
    water_tree_t t{};
    water_tree_init( t );
    int failed = 0;
    for ( std::size_t i = 0; i < g_golden_tree_ops_count; ++i )
    {
        const golden_tree_op_t& op = g_golden_tree_ops[ i ];
        const int r = op.m_op == 0 ? water_tree_add( t, op.m_arg, op.m_rect ) : water_tree_remove( t, op.m_arg );
        if ( r != op.m_result )
        {
            if ( ++failed <= 3 )
                LOG_ERROR( "water tree op %d (%d): ours %d retail %d", static_cast< int >( i ), op.m_op, r, op.m_result );
        }
    }
    int bad_nodes = 0;
    if ( t.nodes.size( ) != g_golden_tree_nodes_count )
    {
        ++failed;
        LOG_ERROR( "water tree: %d nodes, retail %d", static_cast< int >( t.nodes.size( ) ), static_cast< int >( g_golden_tree_nodes_count ) );
    }
    else
    {
        for ( std::size_t k = 0; k < g_golden_tree_nodes_count; ++k )
        {
            const golden_tree_node_t& g = g_golden_tree_nodes[ k ];
            const water_tree_node_t& n = t.nodes[ k ];
            if ( std::memcmp( n.rect, g.m_rect, 16 ) != 0 || n.parent != g.m_parent || n.first != g.m_first ||
                 n.second != g.m_second || n.tag != g.m_tag )
            {
                if ( ++bad_nodes <= 3 )
                    LOG_ERROR( "water tree node %d differs (parent %d / %d, first %d / %d, second %d / %d)",
                        static_cast< int >( k ), n.parent, g.m_parent, n.first, g.m_first, n.second, g.m_second );
            }
        }
    }
    const int header[ 6 ] = { t.root, t.free, t.chunk, t.leaves, t.used, t.free_count };
    const bool header_ok = std::memcmp( header, g_golden_tree_header, sizeof( header ) ) == 0;
    if ( failed == 0 && bad_nodes == 0 && header_ok )
    {
        LOG_SUCCESS( "water_tree matches VuWater's 2D tree (@0x598c70 / @0x598e50 / @0x598de0 / @0x599010 / @0x598f00 / @0x599220) on %d operations, %d final nodes and the header",
            static_cast< int >( g_golden_tree_ops_count ), static_cast< int >( g_golden_tree_nodes_count ) );
        return true;
    }
    LOG_ERROR( "water_tree FAILED: %d op returns, %d nodes, header %s", failed, bad_nodes, header_ok ? "ok" : "differs" );
    return false;
}

// water_hold_add / water_hold_remove against VuWater's add @0x529a90 (walks @0x52b4d0 / @0x52b3e0, the
// per-surface test @0x529c60) and removal @0x529b90 on 4 hand-built surfaces and 30 waves: after every op
// each wave tree's node count and the pool count; at the end every tree's nodes and header, every wave's
// entry list and the pool.
inline bool verify_water_waves( )
{
    water_hold_t w{};
    water_tree_init( w.surface_tree );
    w.surfaces.resize( 4 );
    for ( int i = 0; i < 4; ++i )
    {
        const golden_ww_surface_t& g = g_golden_ww_surfaces[ i ];
        water_held_surface_t& s = w.surfaces[ i ];
        std::memcpy( s.rows, g.m_rows, 64 );
        s.z_low = g.m_z[ 0 ];
        s.z_high = g.m_z[ 1 ];
        s.half[ 0 ] = g.m_half[ 0 ];
        s.half[ 1 ] = g.m_half[ 1 ];
        water_tree_init( s.waves );
        water_tree_add( w.surface_tree, i, g.m_rect );
    }
    w.pool.resize( 200 );
    for ( int i = 0; i < 200; ++i )
        w.pool[ i ].next = i + 1 < 200 ? i + 1 : -1;
    w.pool_free = 0;
    w.pool_count = 200;
    for ( std::size_t k = 0; k < g_golden_ww_waves_count; ++k )
    {
        water_held_wave_t v{};
        const float* b = g_golden_ww_waves[ k ].m_bounds;
        std::memcpy( v.box, b, 32 );
        v.centre[ 0 ] = b[ 8 ];
        v.centre[ 1 ] = b[ 9 ];
        v.radius = b[ 10 ];
        v.point = g_golden_ww_waves[ k ].m_flag & 1;
        w.waves.push_back( v );
    }
    int failed = 0;
    for ( std::size_t i = 0; i < g_golden_ww_ops_count; ++i )
    {
        const golden_ww_op_t& op = g_golden_ww_ops[ i ];
        if ( op.m_op == 0 )
            water_hold_add( w, op.m_wave );
        else
            water_hold_remove( w, op.m_wave );
        const int after[ 5 ] = { w.surfaces[ 0 ].waves.used, w.surfaces[ 1 ].waves.used, w.surfaces[ 2 ].waves.used,
            w.surfaces[ 3 ].waves.used, w.pool_count };
        if ( std::memcmp( after, op.m_after, sizeof( after ) ) != 0 && ++failed <= 3 )
            LOG_ERROR( "water waves op %d (%d wave %d): the counts differ from retail", static_cast< int >( i ), op.m_op, op.m_wave );
    }
    int bad = 0;
    for ( int k = 0; k < 5; ++k )
    {
        const water_tree_t& t = k == 0 ? w.surface_tree : w.surfaces[ k - 1 ].waves;
        const golden_ww_tree_t& g = g_golden_ww_trees[ k ];
        const int header[ 6 ] = { t.root, t.free, t.chunk, t.leaves, t.used, t.free_count };
        if ( std::memcmp( header, &g, sizeof( header ) ) != 0 || static_cast< int >( t.nodes.size( ) ) != g.m_count )
        {
            ++bad;
            continue;
        }
        for ( int n = 0; n < g.m_count; ++n )
        {
            const golden_tree_node_t& gn = g_golden_ww_nodes[ g.m_nodes + n ];
            const water_tree_node_t& m = t.nodes[ n ];
            // a leaf's object and a wave-tree leaf's tag were retail pointers, mapped in the golden to
            // 0x20000 + surface / 0x10000 + wave
            const int base = k == 0 ? 0x20000 : 0x10000;
            const int want_first = gn.m_first >= 0x10000 ? gn.m_first - base : gn.m_first;
            const std::uint32_t want_tag = gn.m_tag >= 0x20000u ? gn.m_tag - 0x20000u : gn.m_tag;
            const int first = m.first;
            const std::uint32_t tag = m.tag;
            if ( std::memcmp( m.rect, gn.m_rect, 16 ) != 0 || m.parent != gn.m_parent || first != want_first ||
                 m.second != gn.m_second || tag != want_tag )
                ++bad;
        }
    }
    for ( std::size_t k = 0; k < g_golden_ww_waves_count; ++k )
    {
        const int start = g_golden_ww_list_heads[ 2 * k ];
        const int count = g_golden_ww_list_heads[ 2 * k + 1 ];
        int e = w.waves[ k ].entries;
        for ( int j = 0; j < count; ++j, e = e >= 0 ? w.pool[ e ].next : -1 )
        {
            const int* g = g_golden_ww_list_entries + 3 * ( start + j );
            if ( e < 0 || w.pool[ e ].surface != g[ 0 ] || w.pool[ e ].leaf != g[ 1 ] || e != g[ 2 ] )
                ++bad;
        }
        if ( e >= 0 )
            ++bad;
    }
    if ( w.pool_count != g_golden_ww_pool[ 0 ] || w.pool_free != g_golden_ww_pool[ 1 ] )
        ++bad;
    if ( failed == 0 && bad == 0 )
    {
        LOG_SUCCESS( "water_hold matches VuWater's wave add / removal (@0x529a90 / @0x52b4d0 / @0x52b3e0 / @0x529c60 / @0x529b90) on %d ops: every count after each, every final tree node, entry list and the pool",
            static_cast< int >( g_golden_ww_ops_count ) );
        return true;
    }
    LOG_ERROR( "water_hold FAILED: %d ops' counts, %d final items", failed, bad );
    return false;
}

// pfx_quad_pattern_tick against VuPfxQuadPatternInstance's tick @0x591c30 on 8 hand-built patterns: every
// particle's 30 words after, the pattern's list and AABB (pads excluded), the pool and the child calls.
struct quad_pattern_record_t
{
    std::vector< std::uint32_t > calls;
};

// The model box the geom golden carries (min x y z, max x y z); the quad golden has none.
inline const float* pattern_box( const golden_geom_pattern_t& g ) { return g.m_box; }
inline const float* pattern_box( const golden_quad_pattern_t& ) { return nullptr; }

template < typename G >
inline int pattern_tick_failures( const G* table, std::size_t count, const unsigned* words, const unsigned* calls, bool geom )
{
    int failed = 0;
    for ( std::size_t index = 0; index < count; ++index )
    {
        const G& g = table[ index ];
        const float* box = geom ? pattern_box( g ) : nullptr;
        std::vector< pfx_quad_particle_t > parts( g.m_count );
        for ( int i = 0; i < g.m_count; ++i )
        {
            std::memcpy( parts[ i ].w, words + g.m_words + 30 * i, 120 );
            parts[ i ].w[ 0 ] = static_cast< std::uint32_t >( i + 1 < g.m_count ? i + 1 : -1 );
            parts[ i ].w[ 1 ] = static_cast< std::uint32_t >( i > 0 ? i - 1 : -1 );
        }
        pfx_quad_pattern_t p{};
        p.particles = &parts;
        p.head = g.m_count ? 0 : -1;
        p.tail = g.m_count ? g.m_count - 1 : -1;
        p.count = g.m_count;
        pfx_pool_t pool{};
        pool.head = pool.tail = g.m_pool_pre ? 20 : -1;
        pool.count = g.m_pool_pre;
        quad_pattern_record_t rec{};
        auto children = []( void* u, float dt ) {
            std::uint32_t bits;
            std::memcpy( &bits, &dt, 4 );
            auto& r = *static_cast< quad_pattern_record_t* >( u );
            r.calls.insert( r.calls.end( ), { 0u, bits, 1u, bits } );
        };
        pfx_pattern_tick( p, pool, g.m_clock, g.m_inst_scale, g.m_matrix, g.m_space, g.m_delay, g.m_dt, children, &rec, box );
        bool ok = true;
        for ( int i = 0; i < g.m_count && ok; ++i )
            ok = std::memcmp( parts[ i ].w, words + g.m_words + 30 * ( g.m_count + i ), 120 ) == 0;
        ok = ok && static_cast< std::uint32_t >( p.head ) == g.m_pattern[ 0 ] && static_cast< std::uint32_t >( p.tail ) == g.m_pattern[ 1 ] &&
             static_cast< std::uint32_t >( p.count ) == g.m_pattern[ 2 ];
        const int slots[ 6 ] = { 3, 4, 5, 7, 8, 9 };
        for ( int k = 0; k < 6 && ok; ++k )
        {
            std::uint32_t bits;
            std::memcpy( &bits, &p.aabb[ k ], 4 );
            ok = bits == g.m_pattern[ slots[ k ] ];
        }
        // the golden marks "still the pre-existing node" -9; with no pre-existing node that is null
        auto pool_id = [ & ]( int n ) { return ( g.m_pool_pre ? n == 20 : n == -1 ) ? -9 : n; };
        ok = ok && pool_id( pool.tail ) == g.m_pool[ 0 ] && pool_id( pool.head ) == g.m_pool[ 1 ] && pool.count == g.m_pool[ 2 ];
        ok = ok && rec.calls.size( ) == static_cast< std::size_t >( 2 * g.m_calls ) &&
             ( rec.calls.empty( ) || std::memcmp( rec.calls.data( ), calls + g.m_call,
                                         rec.calls.size( ) * 4 ) == 0 );
        if ( !ok )
        {
            ++failed;
            LOG_ERROR( "pfx pattern tick (%s) sample %d differs from retail", geom ? "geom" : "quad", static_cast< int >( index ) );
        }
    }
    return failed;
}

inline bool verify_quad_pattern_tick( )
{
    const int failed = pattern_tick_failures( g_golden_quad_patterns, g_golden_quad_patterns_count, g_golden_quad_pattern_words,
        g_golden_quad_pattern_calls, false );
    if ( failed == 0 )
    {
        LOG_SUCCESS( "pfx_quad_pattern_tick matches VuPfxQuadPatternInstance's tick @0x591c30 on %d patterns (particles, list, AABB, pool, child ticks)",
            static_cast< int >( g_golden_quad_patterns_count ) );
    }
    else
    {
        LOG_ERROR( "pfx_quad_pattern_tick FAILED %d of %d", failed, static_cast< int >( g_golden_quad_patterns_count ) );
    }
    return failed == 0;
}

// pfx_pattern_tick in its geom form against VuPfxGeomPatternInstance's tick @0x58fea0, the same comparison.
inline bool verify_geom_pattern_tick( )
{
    const int failed = pattern_tick_failures( g_golden_geom_patterns, g_golden_geom_patterns_count, g_golden_geom_pattern_words,
        g_golden_geom_pattern_calls, true );
    if ( failed == 0 )
    {
        LOG_SUCCESS( "pfx_pattern_tick (geom) matches VuPfxGeomPatternInstance's tick @0x58fea0 on %d patterns (particles, list, AABB, pool, child ticks)",
            static_cast< int >( g_golden_geom_patterns_count ) );
    }
    else
    {
        LOG_ERROR( "pfx_pattern_tick (geom) FAILED %d of %d", failed, static_cast< int >( g_golden_geom_patterns_count ) );
    }
    return failed == 0;
}

// pfx_fountain_emit against VuPfxEmitFountain_emit @0x55d030 on 10 scenarios: every particle it filled (32
// words), the node's count and accumulator after, the global VuRand's 34 words after, and the number of
// allocations and slot-2 calls.
struct fountain_record_t
{
    std::vector< pfx_quad_particle_t > parts;
    int allow = 0;
    int next = 0;
    int allocs = 0;
    int inits = 0;
    const float* matrix = nullptr;
};

inline bool verify_fountain_emit( )
{
    int failed = 0;
    for ( std::size_t index = 0; index < g_golden_fountains_count; ++index )
    {
        const golden_fountain_t& g = g_golden_fountains[ index ];
        std::uint8_t params[ 0x98 ]{};
        std::memcpy( params, g.m_params, sizeof( g.m_params ) );
        std::memcpy( params + 0x34, &g.m_max, 4 );
        std::memcpy( params + 0x40, g.m_colours, 8 );
        params[ 0x90 ] = 0;
        pfx_rand_t rand{};
        rand.seed = g.m_seed;
        rand.previous = g.m_prev;
        std::memcpy( rand.table, g.m_table, sizeof( rand.table ) );
        pfx_fountain_node_t node{};
        node.alive = g.m_alive;
        node.accumulator = g.m_node[ 0 ];
        node.count_scale = g.m_node[ 1 ];
        node.rate_scale = g.m_node[ 2 ];
        node.alpha_scale = g.m_node[ 3 ];
        fountain_record_t rec{};
        rec.parts.resize( g.m_allow );
        rec.allow = g.m_allow;
        rec.matrix = g.m_matrix;
        pfx_fountain_hooks_t hooks{};
        hooks.user = &rec;
        hooks.allocate = []( void* u ) -> pfx_quad_particle_t* {
            auto& r = *static_cast< fountain_record_t* >( u );
            ++r.allocs;
            return r.next < r.allow ? &r.parts[ r.next++ ] : nullptr;
        };
        hooks.matrix = []( void* u ) { return static_cast< fountain_record_t* >( u )->matrix; };
        hooks.init = []( void* u, pfx_quad_particle_t& ) { ++static_cast< fountain_record_t* >( u )->inits; };
        pfx_fountain_emit( node, params, rand, g.m_camera, g.m_cameras, g.m_system, g.m_system + 3, g.m_dt, hooks );
        bool ok = rec.next == g.m_count && rec.allocs == g.m_allocs && rec.inits == g.m_inits && node.alive == g.m_alive_after &&
                  std::memcmp( &node.accumulator, &g.m_accumulator_after, 4 ) == 0;
        for ( int i = 0; i < rec.next && ok; ++i )
            ok = std::memcmp( rec.parts[ i ].w, g_golden_fountain_words + g.m_words + 32 * i, 120 ) == 0;
        std::uint32_t after[ 34 ];
        std::memcpy( after, &rand.seed, 4 );
        std::memcpy( after + 1, &rand.previous, 4 );
        std::memcpy( after + 2, rand.table, 128 );
        ok = ok && std::memcmp( after, g.m_rand_after, sizeof( after ) ) == 0;
        if ( !ok )
        {
            ++failed;
            LOG_ERROR( "pfx_fountain_emit scenario %d differs from @0x55d030 (particles %d / %d, allocs %d / %d)",
                static_cast< int >( index ), rec.next, g.m_count, rec.allocs, g.m_allocs );
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "pfx_fountain_emit matches VuPfxEmitFountain_emit @0x55d030 on %d scenarios (particles, node, VuRand, calls)",
            static_cast< int >( g_golden_fountains_count ) );
    }
    else
    {
        LOG_ERROR( "pfx_fountain_emit FAILED %d of %d", failed, static_cast< int >( g_golden_fountains_count ) );
    }
    return failed == 0;
}

// water_best_wake against VuWater's wake lookup @0x5296c0 (surface lookup @0x52aec0, wave walk @0x52b270,
// VuBestWakeFinder @0x421e50, the record blend @0x52bf10): the hold built through water_hold_add as retail's
// 0x529a90 built it (verified by verify_water_waves), then 48 queries; the finder's +0x14 after each.
inline bool verify_wake_lookup( )
{
    water_hold_t w{};
    water_tree_init( w.surface_tree );
    w.surfaces.resize( g_golden_wl_surfaces_count );
    for ( std::size_t i = 0; i < g_golden_wl_surfaces_count; ++i )
    {
        const golden_wl_surface_t& g = g_golden_wl_surfaces[ i ];
        water_held_surface_t& s = w.surfaces[ i ];
        std::memcpy( s.rows, g.m_rows, 64 );
        s.below = g.m_below;
        s.above = g.m_above;
        s.z_low = g.m_z[ 0 ];
        s.z_high = g.m_z[ 1 ];
        s.half[ 0 ] = g.m_half[ 0 ];
        s.half[ 1 ] = g.m_half[ 1 ];
        water_tree_init( s.waves );
        water_tree_add( w.surface_tree, static_cast< int >( i ), g.m_rect );
    }
    w.pool.resize( 200 );
    for ( int i = 0; i < 200; ++i )
        w.pool[ i ].next = i + 1 < 200 ? i + 1 : -1;
    w.pool_free = 0;
    w.pool_count = 200;
    for ( std::size_t k = 0; k < g_golden_wl_waves_count; ++k )
    {
        const golden_wl_wave_t& g = g_golden_wl_waves[ k ];
        water_held_wave_t v{};
        std::memcpy( v.box, g.m_bounds, 32 );
        v.centre[ 0 ] = g.m_bounds[ 8 ];
        v.centre[ 1 ] = g.m_bounds[ 9 ];
        v.radius = g.m_bounds[ 10 ];
        v.wake = g.m_kind == 0;
        v.wake_data.inner_ratio = g.m_head[ 0 ];
        v.wake_data.outer_ratio = g.m_head[ 1 ];
        v.wake_data.scale = g.m_head[ 2 ];
        v.wake_data.a = wake_record( g.m_a );
        v.wake_data.b = wake_record( g.m_b );
        w.waves.push_back( v );
        water_hold_add( w, static_cast< int >( k ) );
    }
    int failed = 0;
    int live = 0;
    for ( std::size_t i = 0; i < g_golden_wl_queries_count; ++i )
    {
        const golden_wl_query_t& q = g_golden_wl_queries[ i ];
        water_wake_finder_t f{};
        f.point[ 0 ] = q.m_finder[ 0 ];
        f.point[ 1 ] = q.m_finder[ 1 ];
        f.direction[ 0 ] = q.m_finder[ 2 ];
        f.direction[ 1 ] = q.m_finder[ 3 ];
        f.best = q.m_finder[ 4 ];
        water_best_wake( w, q.m_skip != 0, q.m_point, f );
        if ( q.m_best != q.m_finder[ 4 ] )
            ++live;
        if ( std::memcmp( &f.best, &q.m_best, 4 ) != 0 && ++failed <= 3 )
            LOG_ERROR( "water_best_wake query %d: ours %.9g retail %.9g", static_cast< int >( i ), f.best, q.m_best );
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "water_best_wake matches VuWater's wake lookup @0x5296c0 (0x52aec0 / 0x52b270 / 0x421e50 / 0x52bf10) on %d queries (%d changing the answer)",
            static_cast< int >( g_golden_wl_queries_count ), live );
        return true;
    }
    LOG_ERROR( "water_best_wake FAILED %d of %d queries", failed, static_cast< int >( g_golden_wl_queries_count ) );
    return false;
}

// water_build_world (world/surface.hxx) against retail on Hydroelectric's water: child 8 "Water" of
// VuTemplateAsset/Levels/Hydroelectric re-encoded as a project, loaded by hydro's tree (entity_load_project
// with the verified callbacks) and by retail's VuEntity::load, then every water entity's slot 6 in
// gameInitialize order (oracle case water_build). Compared: each water entity's loaded property words and
// transform words (+ the world euler of 0x534AA0), every surface's and wave's recreated words, the surface tree
// and every surface's wave tree node for node, the wave list, each wave's entry list and the pool.
inline std::unique_ptr< entity_t > water_build_document_tree( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_water_build_document );
    static const value_t document =
        c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_water_build_document_bytes ) ).parse( );
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
    return entity_load_project( member( document, "VuProject" ), "Data/Projects/Water.vuprj", world );
}

inline bool verify_water_build( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "water_build: the document did not load" );
        return false;
    }
    water_built_t built{};
    water_build_world( *tree, built, []( entity_t& e ) { return entity_transform( e ); } );
    int failed = 0;
    int compared = 0;
    auto cmp = [ & ]( const char* what, int a, int b, std::uint32_t ours, std::uint32_t theirs ) {
        ++compared;
        if ( ours != theirs && ++failed <= 12 )
            LOG_ERROR( "water_build %s %d word %d: ours 0x%08X retail 0x%08X", what, a, b, ours, theirs );
    };
    // The water entities in visit order: their loads and transform words.
    std::vector< entity_t* > order;
    const auto post = [ & ]( const auto& self, entity_t& e ) -> void {
        for ( auto& child : e.m_children )
            self( self, *child );
        if ( water_kind_of( e.m_type ) != water_kind_none )
            order.push_back( &e );
    };
    post( post, *tree );
    if ( order.size( ) != g_golden_water_entities_count )
    {
        LOG_ERROR( "water_build: %d water entities, retail %d", static_cast< int >( order.size( ) ), static_cast< int >( g_golden_water_entities_count ) );
        return false;
    }
    int props = 0;
    for ( std::size_t i = 0; i < order.size( ); ++i )
    {
        const golden_water_entity_t& g = g_golden_water_entities[ i ];
        entity_t& e = *order[ i ];
        const water_entity_state_t* s = static_cast< const water_entity_state_t* >( e.m_state.get( ) );
        const transform_component_t* t = entity_transform( e );
        if ( !s || !t || static_cast< int >( water_kind_of( e.m_type ) ) != g.m_kind || e.m_name != g.m_name )
        {
            LOG_ERROR( "water_build: entity %d is %s '%s', retail kind %d '%s' (or has no state / transform)", static_cast< int >( i ),
                e.m_type.c_str( ), e.m_name.c_str( ), g.m_kind, g.m_name );
            ++failed;
            continue;
        }
        for ( int k = 0; k < k_water_entity_words; ++k )
            if ( s->value.known[ k ] )
            {
                cmp( "entity property", static_cast< int >( i ), 0x60 + 4 * k, s->value.word[ k ], g.m_words[ k ] );
                ++props;
            }
        std::uint32_t words[ k_water_transform_words ];
        water_transform_words( *t, words );
        for ( int k = 0; k < k_water_transform_words; ++k )
            if ( k != 35 && k != 39 && k != 43 )
                cmp( "entity transform", static_cast< int >( i ), 0x10 + 4 * k, words[ k ], g.m_transform[ k ] );
    }
    // The objects.
    if ( built.surfaces.size( ) != g_golden_water_surfaces_count || built.waves.size( ) != g_golden_water_waves_count )
    {
        LOG_ERROR( "water_build: %d surfaces / %d waves, retail %d / %d", static_cast< int >( built.surfaces.size( ) ),
            static_cast< int >( built.waves.size( ) ), static_cast< int >( g_golden_water_surfaces_count ),
            static_cast< int >( g_golden_water_waves_count ) );
        return false;
    }
    for ( std::size_t i = 0; i < built.surfaces.size( ); ++i )
        for ( int k = 0; k < 65; ++k )
            if ( built.surfaces[ i ].known[ k ] )
                cmp( "surface", static_cast< int >( i ), 4 * k, built.surfaces[ i ].word[ k ], g_golden_water_surfaces[ i ][ k ] );
    for ( std::size_t i = 0; i < built.waves.size( ); ++i )
        for ( int k = 0; k < 80; ++k )
            if ( built.waves[ i ].known[ k ] && k != 64 / 4 )   // +0x40 the entry list: compared as the lists below
                cmp( "wave", static_cast< int >( i ), 4 * k, built.waves[ i ].word[ k ], k < g_golden_water_waves[ i ].m_words ? g_golden_water_waves[ i ].m_word[ k ] : 0xDEADBEEFu );
    // The trees: the surface tree, then each surface's wave tree.
    int nodes = 0;
    for ( std::size_t i = 0; i < g_golden_water_trees_count; ++i )
    {
        const golden_water_tree_t& g = g_golden_water_trees[ i ];
        const water_tree_t& t = i == 0 ? built.hold.surface_tree : built.hold.surfaces[ i - 1 ].waves;
        const int id = static_cast< int >( i );
        cmp( "tree root", id, 0, static_cast< std::uint32_t >( t.root ), static_cast< std::uint32_t >( g.m_root ) );
        cmp( "tree free", id, 0, static_cast< std::uint32_t >( t.free ), static_cast< std::uint32_t >( g.m_free ) );
        cmp( "tree leaves", id, 0, static_cast< std::uint32_t >( t.leaves ), static_cast< std::uint32_t >( g.m_leaves ) );
        cmp( "tree used", id, 0, static_cast< std::uint32_t >( t.used ), static_cast< std::uint32_t >( g.m_used ) );
        cmp( "tree free count", id, 0, static_cast< std::uint32_t >( t.free_count ), static_cast< std::uint32_t >( g.m_free_count ) );
        cmp( "tree size", id, 0, static_cast< std::uint32_t >( t.nodes.size( ) ), static_cast< std::uint32_t >( g.m_count ) );
        for ( int n = 0; n < g.m_count && n < static_cast< int >( t.nodes.size( ) ); ++n )
        {
            const golden_water_node_t& gn = g_golden_water_nodes[ g.m_first + n ];
            const water_tree_node_t& o = t.nodes[ n ];
            std::uint32_t r[ 4 ];
            std::memcpy( r, o.rect, 16 );
            for ( int k = 0; k < 4; ++k )
                cmp( "tree node rect", id, n, r[ k ], gn.m_rect[ k ] );
            cmp( "tree node parent", id, n, static_cast< std::uint32_t >( o.parent ), static_cast< std::uint32_t >( gn.m_parent ) );
            cmp( "tree node first", id, n, static_cast< std::uint32_t >( o.first ), static_cast< std::uint32_t >( gn.m_first ) );
            cmp( "tree node second", id, n, static_cast< std::uint32_t >( o.second ), static_cast< std::uint32_t >( gn.m_second ) );
            cmp( "tree node tag", id, n, o.tag, static_cast< std::uint32_t >( gn.m_tag ) );
            ++nodes;
        }
    }
    // The wave list from its head, each wave's entries, the pool.
    std::size_t at = 0;
    for ( int e = built.hold.list; e >= 0; e = built.hold.pool[ e ].next, ++at )
    {
        if ( at >= g_golden_water_list_count )
            break;
        cmp( "list pool", static_cast< int >( at ), 0, static_cast< std::uint32_t >( e ), static_cast< std::uint32_t >( g_golden_water_list[ at ][ 0 ] ) );
        cmp( "list wave", static_cast< int >( at ), 0, static_cast< std::uint32_t >( built.hold.pool[ e ].leaf ),
            static_cast< std::uint32_t >( g_golden_water_list[ at ][ 1 ] ) );
    }
    cmp( "list length", 0, 0, static_cast< std::uint32_t >( at ), static_cast< std::uint32_t >( g_golden_water_list_count ) );
    for ( std::size_t i = 0; i < built.hold.waves.size( ); ++i )
    {
        int k = 0;
        const int first = g_golden_water_entry_range[ i ][ 0 ];
        const int count = g_golden_water_entry_range[ i ][ 1 ];
        for ( int e = built.hold.waves[ i ].entries; e >= 0; e = built.hold.pool[ e ].next, ++k )
        {
            if ( k >= count )
                break;
            const golden_water_entry_t& g = g_golden_water_entries[ first + k ];
            cmp( "entry pool", static_cast< int >( i ), k, static_cast< std::uint32_t >( e ), static_cast< std::uint32_t >( g.m_pool ) );
            cmp( "entry surface", static_cast< int >( i ), k, static_cast< std::uint32_t >( built.hold.pool[ e ].surface ),
                static_cast< std::uint32_t >( g.m_surface ) );
            cmp( "entry leaf", static_cast< int >( i ), k, static_cast< std::uint32_t >( built.hold.pool[ e ].leaf ), static_cast< std::uint32_t >( g.m_leaf ) );
        }
        cmp( "entries", static_cast< int >( i ), 0, static_cast< std::uint32_t >( k ), static_cast< std::uint32_t >( count ) );
    }
    cmp( "pool count", 0, 0, static_cast< std::uint32_t >( built.hold.pool_count ), static_cast< std::uint32_t >( g_golden_water_pool[ 0 ] ) );
    cmp( "pool free", 0, 0, static_cast< std::uint32_t >( built.hold.pool_free ), static_cast< std::uint32_t >( g_golden_water_pool[ 1 ] ) );
    if ( failed == 0 )
    {
        LOG_SUCCESS( "water_build_world matches retail's game initialise of Hydroelectric's water (0x5DB510 / 0x5D6F80 / the wave entities' slot 6, "
                     "0x5284C0 / 0x5359E0 / 0x529860 / 0x528D70 / 0x529A90): %d entities (%d property words), %d surfaces, %d waves, %d tree "
                     "nodes, %d values, bit for bit",
            static_cast< int >( order.size( ) ), props, static_cast< int >( built.surfaces.size( ) ), static_cast< int >( built.waves.size( ) ),
            nodes, compared );
        return true;
    }
    LOG_ERROR( "water_build FAILED %d of %d values", failed, compared );
    return false;
}

struct water_query_stats_t
{
    int vertices = 0, moved = 0, calls = 0, tagged = 0, skipped = 0;
};

// The queries of one golden stream (emit/water.py _wq_stream) through query.hxx's water_query on `view`, its kernel
// hook water_view_kernel with the calls logged; returns the queries that differ.
// f96: the renderer's mode (ctx+0x60 = 1, 32-byte records, 8 words each, all compared). `unread`: per wave, true when its
// kernel for this mode is not recreated - a query whose retail slot 3 calls include one is SKIPPED (counted), not compared.
inline int water_query_vectors( const water_view_t& view, const unsigned* words, const unsigned* offsets, std::size_t count, const char* name,
    water_query_stats_t& st, bool f96 = false, const std::vector< bool >* unread = nullptr )
{
    struct logged_t
    {
        const water_view_t* view;
        std::vector< std::uint32_t > log;
    } logged{ &view, {} };
    const water_kernel_fn kernel = []( void* user, int wave, water_query_t& ctx ) {
        logged_t& l = *static_cast< logged_t* >( user );
        l.log.push_back( static_cast< std::uint32_t >( wave ) );
        l.log.push_back( ctx.tag );
        l.log.push_back( ctx.flag100 );
        return water_view_kernel( const_cast< water_view_t* >( l.view ), wave, ctx );
    };
    int failed = 0;
    for ( std::size_t index = 0; index < count; ++index )
    {
        const unsigned* s = words + offsets[ index ];
        auto u = [ & ]( ) { return *s++; };
        water_query_t ctx{};
        ctx.count = static_cast< int >( u( ) );
        for ( float& x : ctx.aabb )
            x = water_bits( u( ) );
        for ( float& x : ctx.centre )
            x = water_bits( u( ) );
        ctx.radius = water_bits( u( ) );
        ctx.exclude_count = static_cast< int >( u( ) );
        for ( int k = 0; k < ctx.exclude_count; ++k )
            ctx.exclude[ k ] = static_cast< int >( u( ) );
        ctx.forced = -1;
        const int nw = f96 ? 8 : 10;
        std::vector< water_vertex_t > v( ctx.count );
        std::vector< std::uint32_t > raw( static_cast< std::size_t >( ctx.count ) * 8, 0x77777777u );
        std::memset( v.data( ), 0x77, v.size( ) * sizeof( water_vertex_t ) );
        for ( int i = 0; i < ctx.count; ++i )
        {
            water_vertex_t& x = v[ static_cast< std::size_t >( i ) ];
            for ( int k = 0; k < 3; ++k )
            {
                const unsigned p = u( );
                x.pos[ k ] = water_bits( p );
                raw[ static_cast< std::size_t >( i ) * 8 + static_cast< std::size_t >( k ) ] = p;
            }
            x.pos_w = 0x3F800000u;
        }
        ctx.vertices = v.data( );
        if ( f96 )
        {
            ctx.flag96 = 1;
            ctx.records = reinterpret_cast< unsigned char* >( raw.data( ) );
            ctx.stride = 32;
        }
        logged.log.clear( );
        std::vector< std::uint32_t > tags;
        const char* what = nullptr;
        int bad = -1;
        const bool reached = water_query( view.water, ctx, kernel, &logged, tags );
        const std::uint32_t* ours = f96 ? raw.data( ) : reinterpret_cast< const std::uint32_t* >( v.data( ) );
        for ( int k = 0; k < nw * ctx.count; ++k )
        {
            const unsigned r = u( );
            if ( !what && ( f96 || k % 10 != 7 ) && ours[ k ] != r )
            {
                what = "a vertex record word";
                bad = k;
            }
            if ( k % nw == ( f96 ? 3 : 4 ) && r != 0u )
                ++st.moved;
        }
        st.vertices += ctx.count;
        if ( u( ) != ctx.flag100 && !what )
            what = "ctx+0x64";
        st.tagged += ctx.flag100 ? 1 : 0;
        const unsigned n = u( );
        st.calls += static_cast< int >( n );
        if ( logged.log.size( ) != 3 * n && !what )
            what = "the number of slot 3 calls";
        bool skip = false;
        for ( unsigned k = 0; k < 3 * n; ++k )
        {
            const unsigned r = u( );
            if ( unread && k % 3 == 0 && r < unread->size( ) && ( *unread )[ r ] )
                skip = true;
            if ( !what && k < logged.log.size( ) && logged.log[ k ] != r )
            {
                what = "a slot 3 call";
                bad = static_cast< int >( k );
            }
        }
        if ( skip )
        {
            ++st.skipped;
            continue;
        }
        if ( !reached )
            what = "a path not recreated was reached";
        if ( what )
        {
            ++failed;
            LOG_ERROR( "%s vector %d: %s (%d)", name, static_cast< int >( index ), what, bad );
        }
    }
    return failed;
}

// The water query 0x528FC0 on Hydroelectric's built water against retail (case water_query): query.hxx's water_query
// over world/kernel.hxx's water_view_build of water_build_world's result, every wave's slot 3 the kernel hook
// water_view_kernel (the five kinds' verified kernels and their tag variants) - retail's own slot 3s under the
// oracle. Compared: every vertex record word except +28 (0x529ED0's / 0x529F70's stale stack), ctx+0x64 after, and
// the slot 3 calls in order (wave, ctx+0x5C, ctx+0x64).
inline bool verify_water_query( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "water_query: the document did not load" );
        return false;
    }
    water_built_t built{};
    water_build_world( *tree, built, []( entity_t& e ) { return entity_transform( e ); } );
    water_view_t view{};
    water_view_build( built, view );
    water_query_stats_t st{};
    const int failed = water_query_vectors( view, g_golden_water_query_words, g_golden_water_query_offsets, g_golden_water_query_count,
        "water query", st );
    if ( failed == 0 )
        LOG_SUCCESS( "water_query + water_view_kernel match retail's 0x528FC0 on Hydroelectric's built water over %d queries (%d vertices, %d "
                     "with disp x moved; %d slot 3 calls, %d queries tagged), every record word, the calls in order",
            static_cast< int >( g_golden_water_query_count ), st.vertices, st.moved, st.calls, st.tagged );
    else
        LOG_ERROR( "water_query FAILED %d of %d queries against 0x528FC0", failed, static_cast< int >( g_golden_water_query_count ) );
    return failed == 0;
}

// The query in the renderer's +96 mode (case water_query96): water_build_world's water plus 12 point waves
// (water_create_point_wave, each ticked once), query.hxx's +96 branches of 0x529ED0 / 0x529F70 and the waves' +96
// kernels through water_view_kernel. Compared: all 8 words of every record, ctx+0x64, the slot 3 calls in order. A query
// reaching a kind whose +96 kernel is not recreated yet is skipped and counted.
inline bool verify_water_query96( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "water_query96: the document did not load" );
        return false;
    }
    water_built_t built{};
    water_build_world( *tree, built, []( entity_t& e ) { return entity_transform( e ); } );
    for ( std::size_t k = 0; k < g_golden_wq96_point_count; ++k )
    {
        const unsigned* p = g_golden_wq96_points + 12 * k;
        const int wave = water_create_point_wave( built, p );
        water_words_t& w = built.waves[ static_cast< std::size_t >( wave ) ];
        point_wave_life_t life{};
        life.tail_width = w.f( 92 );
        life.outer_radius = w.f( 104 );
        life.elapsed = w.f( 120 );
        life.rate = w.f( 124 );
        point_wave_tick( life, static_cast< int >( w.word[ 1 ] ), water_bits( p[ 11 ] ), nullptr, nullptr );
        w.setf( 120, life.elapsed );
        w.word[ 144 / 4 ] = ( w.word[ 144 / 4 ] & ~0xFFu ) | ( life.expired ? 1u : 0u );
    }
    water_view_t view{};
    water_view_build( built, view );
    std::vector< bool > unread( built.waves.size( ) );
    for ( std::size_t i = 0; i < built.waves.size( ); ++i )
    {
        const water_kind_t k = built.wave_kind[ i ];
        unread[ i ] = !( k == water_kind_whirlpool || k == water_kind_ramp || k == water_kind_point || k == water_kind_bump || k == water_kind_banked ||
                         k == water_kind_directional );
    }
    water_query_stats_t st{};
    const int failed = water_query_vectors( view, g_golden_wq96_words, g_golden_wq96_offsets, g_golden_wq96_count, "query96", st, true, &unread );
    if ( failed == 0 )
        LOG_SUCCESS( "water_query + the +96 kernels (whirlpool 0x5A1F80 / 0x5A2160, ramp 0x5A2FC0 / 0x5A3160, point 0x5A39B0 / 0x5A3CA0, "
                     "bump 0x5A4610 / 0x5A49A0, banked turn 0x5A5AB0 / 0x5A5F10, directional 0x59B860 / 0x59BDE0; 0x529ED0 / 0x529F70's +96 "
                     "branches) match retail's 0x528FC0 in the renderer's mode: %d of %d queries compared (%d "
                     "vertices, %d with a displacement; %d slot 3 calls, %d tagged), %d skipped (a +96 kernel not recreated)",
            static_cast< int >( g_golden_wq96_count ) - st.skipped, static_cast< int >( g_golden_wq96_count ), st.vertices, st.moved, st.calls,
            st.tagged, st.skipped );
    else
        LOG_ERROR( "water_query96 FAILED %d of %d queries", failed, static_cast< int >( g_golden_wq96_count ) );
    return failed == 0;
}

// VuWater_createPointWave 0x528E10 on Hydroelectric's built water against retail (case point_waves): 24 point waves'
// words after water_create_point_wave and after point_wave_tick (0x5A34B0; no expiry callback), except +24 / +40
// (stack), +0x40 (a pointer: the entry list is compared instead) and +0x91 .. +0x93 (heap); each wave's entry list;
// the wave list; then the queries around them (verify_water_query's comparison, the point kernel 0x5A36A0 / 0x5A3820
// among the slot 3s).
inline bool verify_point_waves( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "point_waves: the document did not load" );
        return false;
    }
    water_built_t built{};
    water_build_world( *tree, built, []( entity_t& e ) { return entity_transform( e ); } );
    int failed = 0, compared = 0, entries = 0;
    auto cmp = [ & ]( const char* what, int a, int b, std::uint32_t ours, std::uint32_t theirs ) {
        ++compared;
        if ( ours != theirs && ++failed <= 12 )
            LOG_ERROR( "point_waves %s %d word %d: ours 0x%08X retail 0x%08X", what, a, b, ours, theirs );
    };
    auto words = [ & ]( const char* what, int i, const water_words_t& w, const unsigned* r ) {
        for ( int k = 0; k < 37; ++k )
        {
            const int at = 4 * k;
            if ( at == 24 || at == 40 || at == 64 )
                continue;
            cmp( what, i, at, at == 144 ? ( w.word[ k ] & 0xFFu ) : w.word[ k ], at == 144 ? ( r[ k ] & 0xFFu ) : r[ k ] );
        }
    };
    std::vector< const unsigned* > after( g_golden_pw_wave_count );
    std::vector< int > index( g_golden_pw_wave_count );
    for ( std::size_t i = 0; i < g_golden_pw_wave_count; ++i )
    {
        const unsigned* s = g_golden_pw_wave_words + g_golden_pw_wave_offsets[ i ];
        index[ i ] = water_create_point_wave( built, s );
        water_words_t& w = built.waves[ static_cast< std::size_t >( index[ i ] ) ];
        words( "created", static_cast< int >( i ), w, s + 12 );
        point_wave_life_t life{};
        life.tail_width = w.f( 92 );
        life.outer_radius = w.f( 104 );
        life.elapsed = w.f( 120 );
        life.rate = w.f( 124 );
        life.has_callback = w.word[ 140 / 4 ] != 0;
        life.expired = ( w.word[ 144 / 4 ] & 0xFFu ) != 0;
        point_wave_tick( life, static_cast< int >( w.word[ 1 ] ), water_bits( s[ 11 ] ), nullptr, nullptr );
        w.setf( 120, life.elapsed );
        w.word[ 144 / 4 ] = ( w.word[ 144 / 4 ] & ~0xFFu ) | ( life.expired ? 1u : 0u );
        after[ i ] = s + 12 + 37;
    }
    // The entry lists and the wave list once every wave is made (retail's dump order).
    for ( std::size_t i = 0; i < g_golden_pw_wave_count; ++i )
    {
        const unsigned* s = after[ i ];
        words( "ticked", static_cast< int >( i ), built.waves[ static_cast< std::size_t >( index[ i ] ) ], s );
        s += 37;
        const unsigned n = *s++;
        int e = built.hold.waves[ static_cast< std::size_t >( index[ i ] ) ].entries;
        for ( unsigned k = 0; k < n; ++k, s += 3 )
        {
            cmp( "entry pool", static_cast< int >( i ), static_cast< int >( k ), static_cast< std::uint32_t >( e ), s[ 0 ] );
            if ( e < 0 )
                break;
            cmp( "entry surface", static_cast< int >( i ), static_cast< int >( k ), static_cast< std::uint32_t >( built.hold.pool[ e ].surface ), s[ 1 ] );
            cmp( "entry leaf", static_cast< int >( i ), static_cast< int >( k ), static_cast< std::uint32_t >( built.hold.pool[ e ].leaf ), s[ 2 ] );
            e = built.hold.pool[ e ].next;
            ++entries;
        }
        cmp( "entries end", static_cast< int >( i ), 0, static_cast< std::uint32_t >( e ), 0xFFFFFFFFu );
    }
    int e = built.hold.list;
    for ( std::size_t k = 0; k < g_golden_pw_wave_list_count; ++k )
    {
        cmp( "list pool", static_cast< int >( k ), 0, static_cast< std::uint32_t >( e ), static_cast< std::uint32_t >( g_golden_pw_wave_list[ k ][ 0 ] ) );
        if ( e < 0 )
            break;
        cmp( "list wave", static_cast< int >( k ), 0, static_cast< std::uint32_t >( built.hold.pool[ e ].leaf ),
            static_cast< std::uint32_t >( g_golden_pw_wave_list[ k ][ 1 ] ) );
        e = built.hold.pool[ e ].next;
    }
    cmp( "list end", 0, 0, static_cast< std::uint32_t >( e ), 0xFFFFFFFFu );
    water_view_t view{};
    water_view_build( built, view );
    water_query_stats_t st{};
    const int qfailed = water_query_vectors( view, g_golden_pw_query_words, g_golden_pw_query_offsets, g_golden_pw_query_count,
        "point query", st );
    if ( failed == 0 && qfailed == 0 )
    {
        LOG_SUCCESS( "water_create_point_wave / point_wave_tick / the point kernel match retail's 0x528E10 / 0x5A3340 / 0x5A34B0 / 0x5A36A0 "
                     "on Hydroelectric's built water: %d waves (%d values, %d entries, the wave list), %d queries (%d vertices, %d slot 3 "
                     "calls, %d tagged), bit for bit",
            static_cast< int >( g_golden_pw_wave_count ), compared, entries, static_cast< int >( g_golden_pw_query_count ), st.vertices,
            st.calls, st.tagged );
        return true;
    }
    LOG_ERROR( "point_waves FAILED %d of %d values, %d of %d queries", failed, compared, qfailed, static_cast< int >( g_golden_pw_query_count ) );
    return false;
}

// The built water's tick against retail's 0x529E30 (case water_built_tick): water_build_world's water plus 20 point
// waves (water_create_point_wave; two in three with the expiry callback, answered by water_point_wave_expired as
// VuPointWaveEntity's OnExpired 0x5D4B60 does), then 10 ticks of water_built_tick, the case's refcounts set to 1 (the
// whirlpool 11 / banked turn 15 before tick 3, one wave of every built class before tick 5 - g_golden_wbt_forced). Compared per tick: the hook events in order (rebuild / render / expire / plug /
// destroy), which waves stay listed, their known words (not +24 / +40 / +0x40; +0x8C as set or not; +0x90's low byte),
// the wave list, the pool, every listed wave's entry list; after the last tick every surface's wave tree node for
// node; and that water_built_tick reports no unrecreated path (every class's slot 0 dtor is recreated).
inline bool verify_water_built_tick( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "water_built_tick: the document did not load" );
        return false;
    }
    water_built_t built{};
    water_build_world( *tree, built, []( entity_t& e ) { return entity_transform( e ); } );
    for ( std::size_t k = 0; k < g_golden_wbt_point_count; ++k )
    {
        const unsigned* p = g_golden_wbt_points + 12 * k;
        const int wave = water_create_point_wave( built, p );
        if ( p[ 11 ] )
            built.waves[ static_cast< std::size_t >( wave ) ].word[ 0x8C / 4 ] = 1u;   // the callback: set, its pointer unknown
    }
    int failed = 0, compared = 0, events = 0, words = 0;
    auto cmp = [ & ]( const char* what, int a, int b, std::uint32_t ours, std::uint32_t theirs ) {
        ++compared;
        if ( ours != theirs && ++failed <= 12 )
            LOG_ERROR( "water_built_tick %s %d / %d: ours 0x%08X retail 0x%08X", what, a, b, ours, theirs );
    };
    std::vector< std::uint32_t > log;
    water_built_hooks_t hooks{};
    hooks.user = &log;
    hooks.expire = []( void* u, water_built_t& b, int wave ) {
        auto& l = *static_cast< std::vector< std::uint32_t >* >( u );
        l.insert( l.end( ), { 2u, static_cast< std::uint32_t >( wave ) } );
        const bool was = wave < static_cast< int >( b.wave_gone.size( ) ) && b.wave_gone[ static_cast< std::size_t >( wave ) ];
        water_point_wave_expired( b, wave );
        if ( !was && wave < static_cast< int >( b.wave_gone.size( ) ) && b.wave_gone[ static_cast< std::size_t >( wave ) ] )
            l.insert( l.end( ), { 4u, static_cast< std::uint32_t >( wave ) } );
        l.insert( l.end( ), { 3u, 0xFFFFFFFFu } );   // the "OnExpired" plug
    };
    hooks.rebuild = []( void* u, int wave ) {
        static_cast< std::vector< std::uint32_t >* >( u )->insert( static_cast< std::vector< std::uint32_t >* >( u )->end( ),
            { 0u, static_cast< std::uint32_t >( wave ) } );
    };
    hooks.render = []( void* u ) {
        static_cast< std::vector< std::uint32_t >* >( u )->insert( static_cast< std::vector< std::uint32_t >* >( u )->end( ), { 1u, 0xFFFFFFFFu } );
    };
    hooks.destroyed = []( void* u, int wave ) {
        static_cast< std::vector< std::uint32_t >* >( u )->insert( static_cast< std::vector< std::uint32_t >* >( u )->end( ),
            { 4u, static_cast< std::uint32_t >( wave ) } );
    };
    for ( std::size_t t = 0; t < g_golden_wbt_count; ++t )
    {
        const int ti = static_cast< int >( t );
        for ( std::size_t f = 0; f < g_golden_wbt_forced_count; ++f )
            if ( g_golden_wbt_forced[ 2 * f ] == t )
                built.waves[ static_cast< std::size_t >( g_golden_wbt_forced[ 2 * f + 1 ] ) ].word[ 1 ] = 1u;
        const unsigned* s = g_golden_wbt_words + g_golden_wbt_offsets[ t ];
        const float dt = water_bits( *s++ );
        log.clear( );
        const int missing = water_built_tick( built, dt, hooks );
        cmp( "unrecreated paths", ti, 0, static_cast< std::uint32_t >( missing ), 0u );
        const unsigned n_events = *s++;
        cmp( "event count", ti, 0, static_cast< std::uint32_t >( log.size( ) / 2 ), n_events );
        for ( unsigned k = 0; k < 2 * n_events; ++k, ++s )
            cmp( "event", ti, static_cast< int >( k ), k < log.size( ) ? log[ k ] : 0xDEADBEEFu, *s );
        events += static_cast< int >( n_events );
        std::vector< int > live;
        for ( int e = built.hold.list; e >= 0; e = built.hold.pool[ e ].next )
            live.push_back( built.hold.pool[ e ].leaf );
        std::sort( live.begin( ), live.end( ) );
        const unsigned n_live = *s++;
        cmp( "listed waves", ti, 0, static_cast< std::uint32_t >( live.size( ) ), n_live );
        for ( unsigned i = 0; i < n_live; ++i )
        {
            const int wave = static_cast< int >( *s++ );
            const unsigned n = *s++;
            cmp( "listed wave", ti, static_cast< int >( i ), i < live.size( ) ? static_cast< std::uint32_t >( live[ i ] ) : 0xDEADBEEFu,
                static_cast< std::uint32_t >( wave ) );
            const water_words_t& w = built.waves[ static_cast< std::size_t >( wave ) ];
            const bool point = built.wave_kind[ static_cast< std::size_t >( wave ) ] == water_kind_point;
            for ( unsigned k = 0; k < n && k < 80; ++k )
            {
                const unsigned at = 4 * k;
                if ( at == 24 || at == 40 || at == 64 )
                    continue;
                if ( point && at == 0x8C )
                    cmp( "point callback", ti, wave, w.word[ k ] != 0, s[ k ] != 0 );
                else if ( point && at == 0x90 )
                    cmp( "point expired", ti, wave, w.word[ k ] & 0xFFu, s[ k ] & 0xFFu );
                else if ( w.known[ k ] || point )
                {
                    cmp( "wave word", ti, wave * 1000 + static_cast< int >( at ), w.word[ k ], s[ k ] );
                    ++words;
                }
            }
            s += n;
        }
        const unsigned n_list = *s++;
        int e = built.hold.list;
        for ( unsigned k = 0; k < n_list; ++k, s += 2 )
        {
            cmp( "list pool", ti, static_cast< int >( k ), static_cast< std::uint32_t >( e ), s[ 0 ] );
            if ( e < 0 )
                break;
            cmp( "list wave", ti, static_cast< int >( k ), static_cast< std::uint32_t >( built.hold.pool[ e ].leaf ), s[ 1 ] );
            e = built.hold.pool[ e ].next;
        }
        cmp( "list end", ti, 0, static_cast< std::uint32_t >( e ), 0xFFFFFFFFu );
        cmp( "pool count", ti, 0, static_cast< std::uint32_t >( built.hold.pool_count ), *s++ );
        cmp( "pool free", ti, 0, static_cast< std::uint32_t >( built.hold.pool_free ), *s++ );
        for ( unsigned i = 0; i < n_live && i < live.size( ); ++i )
        {
            const unsigned n = *s++;
            int x = built.hold.waves[ static_cast< std::size_t >( live[ i ] ) ].entries;
            for ( unsigned k = 0; k < n; ++k, s += 2 )
            {
                cmp( "entry pool", ti, live[ i ], static_cast< std::uint32_t >( x ), s[ 0 ] );
                if ( x < 0 )
                    break;
                cmp( "entry surface", ti, live[ i ], static_cast< std::uint32_t >( built.hold.pool[ x ].surface ), s[ 1 ] );
                x = built.hold.pool[ x ].next;
            }
            cmp( "entries end", ti, live[ i ], static_cast< std::uint32_t >( x ), 0xFFFFFFFFu );
        }
    }
    int nodes = 0;
    for ( std::size_t i = 0; i < g_golden_wbt_trees_count && i < built.hold.surfaces.size( ); ++i )
    {
        const golden_water_tree_t& g = g_golden_wbt_trees[ i ];
        const water_tree_t& t = built.hold.surfaces[ i ].waves;
        const int id = static_cast< int >( i );
        cmp( "tree root", id, 0, static_cast< std::uint32_t >( t.root ), static_cast< std::uint32_t >( g.m_root ) );
        cmp( "tree free", id, 0, static_cast< std::uint32_t >( t.free ), static_cast< std::uint32_t >( g.m_free ) );
        cmp( "tree leaves", id, 0, static_cast< std::uint32_t >( t.leaves ), static_cast< std::uint32_t >( g.m_leaves ) );
        cmp( "tree used", id, 0, static_cast< std::uint32_t >( t.used ), static_cast< std::uint32_t >( g.m_used ) );
        cmp( "tree free count", id, 0, static_cast< std::uint32_t >( t.free_count ), static_cast< std::uint32_t >( g.m_free_count ) );
        cmp( "tree size", id, 0, static_cast< std::uint32_t >( t.nodes.size( ) ), static_cast< std::uint32_t >( g.m_count ) );
        for ( int n = 0; n < g.m_count && n < static_cast< int >( t.nodes.size( ) ); ++n )
        {
            const golden_water_node_t& gn = g_golden_wbt_nodes[ g.m_first + n ];
            const water_tree_node_t& o = t.nodes[ n ];
            std::uint32_t r[ 4 ];
            std::memcpy( r, o.rect, 16 );
            for ( int k = 0; k < 4; ++k )
                cmp( "tree node rect", id, n, r[ k ], gn.m_rect[ k ] );
            cmp( "tree node parent", id, n, static_cast< std::uint32_t >( o.parent ), static_cast< std::uint32_t >( gn.m_parent ) );
            cmp( "tree node first", id, n, static_cast< std::uint32_t >( o.first ), static_cast< std::uint32_t >( gn.m_first ) );
            cmp( "tree node second", id, n, static_cast< std::uint32_t >( o.second ), static_cast< std::uint32_t >( gn.m_second ) );
            cmp( "tree node tag", id, n, o.tag, static_cast< std::uint32_t >( gn.m_tag ) );
            ++nodes;
        }
    }
    if ( failed == 0 )
    {
        LOG_SUCCESS( "water_built_tick matches retail's 0x529E30 on Hydroelectric's built water + 20 point waves over %d ticks (%d hook "
                     "events, %d wave words, %d tree nodes, %d values; the slot 2s 0x59C380 / 0x59CC00 / 0x5A1680 / 0x59AAD0 / "
                     "0x5A34B0, OnExpired 0x5D4B60's wave half, 0x529800, the slot 0 dtors 0x5A3310 / 0x5A1440 / 0x5A4D50 / "
                     "0x5A2340 / 0x5A3FB0 / 0x59AAA0 / 0x59CAB0), bit for bit",
            static_cast< int >( g_golden_wbt_count ), events, words, nodes, compared );
        return true;
    }
    LOG_ERROR( "water_built_tick FAILED %d of %d values", failed, compared );
    return false;
}

// The ocean +96 kernels (case ocean_kernels96): ocean_patch_vertex96 (and the evaluator 0x5A0F70 inside it) against
// 0x59F840 / 0x59FB80 / 0x5A0920 / 0x5A0C40 on the hand-built fields - flow x, flow y, foam (untouched) and height of
// every vertex, bit for bit; the tag variants run the same body only where the vertex's tag equals ctx+0x5C (1).
inline bool verify_ocean_kernels96( )
{
    int checked = 0, failed = 0, live = 0;
    for ( std::size_t index = 0; index < g_golden_ok96_kernels_count; ++index )
    {
        const golden_ocean_kernel_t& g = g_golden_ok96_kernels[ index ];
        ocean_patch_t wave{};
        wave.shift = g.m_shift;
        wave.mask = ( 1 << g.m_shift ) - 1;
        wave.scale = g.m_scale;
        wave.sine = g.m_sine;
        wave.cosine = g.m_cosine;
        wave.now = g.m_now;
        wave.current = g.m_current;
        wave.previous = g.m_previous;
        wave.time[ 0 ] = g.m_times[ 0 ];
        wave.time[ 1 ] = g.m_times[ 1 ];
        wave.patches[ 0 ] = g_golden_ok96_patches + g.m_patches;
        wave.patches[ 1 ] = g_golden_ok96_patches + g.m_patches + g.m_patch_count;
        wave.centre_x = g.m_centre[ 0 ];
        wave.centre_y = g.m_centre[ 1 ];
        for ( int k = 0; k < 4; ++k )
            wave.shape[ k ] = g.m_shape[ k ];
        for ( int i = 0; i < g.m_point_count; ++i )
        {
            float flow[ 2 ] = { 0.25f, -0.5f };
            const float foam = 1.5f;
            float height = 2.0f;
            if ( !g.m_variant || g_golden_ok96_tags[ g.m_tags + i ] == 1u )
                ocean_patch_vertex96( wave, g.m_kind == 1, g_golden_ok96_points[ g.m_points + 2 * i ], g_golden_ok96_points[ g.m_points + 2 * i + 1 ],
                    height, flow );
            const float ours[ 4 ] = { flow[ 0 ], flow[ 1 ], foam, height };
            const float* want = g_golden_ok96_out + g.m_out + 4 * i;
            ++checked;
            if ( want[ 3 ] != 2.0f )
                ++live;
            if ( std::memcmp( ours, want, sizeof( ours ) ) != 0 && ++failed <= 6 )
                LOG_ERROR( "ocean_patch_vertex96 %s variant %d point %d: ours %.9g %.9g %.9g retail %.9g %.9g %.9g", g.m_kind ? "circular" : "rectangular",
                    g.m_variant, i, ours[ 0 ], ours[ 1 ], ours[ 3 ], want[ 0 ], want[ 1 ], want[ 3 ] );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ocean_patch_vertex96 + ocean_patch_eval_d match the ocean +96 kernels 0x59F840 / 0x5A0920 (tag 0x59FB80 / 0x5A0C40, "
                     "evaluator 0x5A0F70) bit for bit on %d vertices (%d producing output)",
            checked, live );
    else
        LOG_ERROR( "ocean_patch_vertex96 FAILED %d of %d vertices", failed, checked );
    return failed == 0;
}

// FNV-1a 64 over words' little-endian bytes (the oracle's _fnv64).
inline std::uint64_t water_fnv64( const void* data, std::size_t words )
{
    const unsigned char* p = static_cast< const unsigned char* >( data );
    std::uint64_t h = 0xCBF29CE484222325ull;
    for ( std::size_t i = 0; i < words * 4; ++i )
    {
        h ^= p[ i ];
        h *= 0x100000001B3ull;
    }
    return h;
}

// THE OCEAN FIELD on Hydroelectric's 9 ocean waves (case ocean_field): water_build_world's ocean waves (their field
// built by world/field.hxx's ocean_field_update as the ctor 0x59CB10 -> 0x59CCB0 does), then three water_built_tick
// (0x59CC00's rebuild: EvolveField 0x59D4D0, rlft3 0x608BB0 + fourn 0x608880, 0x59D640, BuildBezierPatches 0x59D6C0),
// then each wave's slot 3 through water_view_kernel in both modes. Compared: the parameter words, the clock words after
// the ctor and each tick, FNV-1a 64 of every array (omega, h0, cells, data, speq, both buffers' heights and patches
// after the ctor; data, speq and the current buffer after each tick), the first wave's arrays word for word, and every
// slot 3 output (24 points per wave per mode) bit for bit.
struct ocean_field_golden_t
{
    const char* label;   // the case, for the log lines
    std::size_t count, ticks, points_per;
    const unsigned* kind;   // 0 rectangular, 1 circular, 2 infinite
    const float* dt;
    const unsigned* words;
    const unsigned long long* hash;
    const float* points;
    const float* base;
    const float* k96;
    const unsigned* first[ 5 ];   // the first wave's omega, h0, data, heights0, patches0 words
    std::size_t first_count[ 5 ];
};

#define OCEAN_FIELD_GOLDEN( label, p )                                                                                              \
    ocean_field_golden_t{ label, g_golden_##p##_count, g_golden_##p##_ticks, g_golden_##p##_points_per, g_golden_##p##_kind,          \
        g_golden_##p##_dt, g_golden_##p##_words, g_golden_##p##_hash, g_golden_##p##_points, g_golden_##p##_base, g_golden_##p##_k96, \
        { g_golden_##p##_first_omega, g_golden_##p##_first_h0, g_golden_##p##_first_data, g_golden_##p##_first_heights0,              \
            g_golden_##p##_first_patches0 },                                                                                         \
        { g_golden_##p##_first_omega_count, g_golden_##p##_first_h0_count, g_golden_##p##_first_data_count,                          \
            g_golden_##p##_first_heights0_count, g_golden_##p##_first_patches0_count } }

// The body of verify_ocean_field on any built water (the menu's too: verify_ocean_field_menu): its ocean waves (rectangular,
// circular, infinite) in build order against one case's golden data.
inline bool ocean_field_check( entity_t& tree, const ocean_field_golden_t& g, const char* where )
{
    water_built_t built{};
    water_build_world( tree, built, []( entity_t& e ) { return entity_transform( e ); } );
    std::vector< int > oceans;
    for ( std::size_t i = 0; i < built.waves.size( ); ++i )
        if ( built.wave_kind[ i ] == water_kind_rectangular || built.wave_kind[ i ] == water_kind_circular ||
             built.wave_kind[ i ] == water_kind_infinite )
            oceans.push_back( static_cast< int >( i ) );
    int failed = 0, compared = 0;
    std::size_t hashed = 0;
    std::map< std::string, int > tally;   // failures by what, for the FAILED line
    auto cmp = [ & ]( const char* what_, int wave, int at, std::uint64_t ours, std::uint64_t theirs ) {
        ++compared;
        if ( ours != theirs )
            ++tally[ what_ ];
        if ( ours != theirs && ++failed <= 12 )
            LOG_ERROR( "%s %s wave %d / %d: ours 0x%llX retail 0x%llX", g.label, what_, wave, at, static_cast< unsigned long long >( ours ),
                static_cast< unsigned long long >( theirs ) );
    };
    if ( oceans.size( ) != g.count )
    {
        LOG_ERROR( "%s: %u ocean waves built, retail %u", g.label, static_cast< unsigned >( oceans.size( ) ), static_cast< unsigned >( g.count ) );
        return false;
    }
    const std::size_t per_words = 16 + 9 * g.ticks;
    const std::size_t per_hash = 9 + 4 * g.ticks;
    const int clock_at[ 8 ] = { 0x88, 0x8C, 0xA4, 0xA8, 0xB0, 0xB4, 0xC0, 0xC4 };
    auto check_clock = [ & ]( const char* what, int k, const water_words_t& w, const unsigned* want ) {
        for ( int j = 0; j < 8; ++j )
            cmp( what, k, j, w.word[ clock_at[ j ] / 4 ], want[ j ] );
    };
    for ( std::size_t k = 0; k < oceans.size( ); ++k )
    {
        const int ki = static_cast< int >( k );
        const water_words_t& w = built.waves[ static_cast< std::size_t >( oceans[ k ] ) ];
        const unsigned* gw = g.words + per_words * k;
        const unsigned long long* gh = g.hash + per_hash * k;
        const water_kind_t kind = built.wave_kind[ static_cast< std::size_t >( oceans[ k ] ) ];
        cmp( "kind", ki, 0, kind == water_kind_circular ? 1u : kind == water_kind_infinite ? 2u : 0u, g.kind[ k ] );
        for ( int j = 0; j < 8; ++j )
            cmp( "parameter", ki, j, w.word[ 0x4C / 4 + j ], gw[ j ] );
        check_clock( "ctor clock", ki, w, gw + 8 );
        const water_built_t::ocean_t* o = water_ocean_of( built, oceans[ k ] );
        if ( !o || o->field.n == 0 )
        {
            cmp( "field built", ki, 0, 0, 1 );
            continue;
        }
        const ocean_field_t& f = o->field;
        const std::pair< const void*, std::size_t > arrays[ 9 ] = { { f.omega.data( ), f.omega.size( ) }, { f.h0.data( ), f.h0.size( ) },
            { f.cells.data( ), f.cells.size( ) }, { f.data.data( ), f.data.size( ) }, { f.speq.data( ), f.speq.size( ) },
            { f.heights[ 0 ].data( ), f.heights[ 0 ].size( ) }, { f.patches[ 0 ].data( ), f.patches[ 0 ].size( ) },
            { f.heights[ 1 ].data( ), f.heights[ 1 ].size( ) }, { f.patches[ 1 ].data( ), f.patches[ 1 ].size( ) } };
        static const char* const names[ 9 ] = { "omega", "h0", "cells", "data", "speq", "heights0", "patches0", "heights1", "patches1" };
        for ( int j = 0; j < 9; ++j )
        {
            cmp( names[ j ], ki, 0, water_fnv64( arrays[ j ].first, arrays[ j ].second ), gh[ j ] );
            hashed += arrays[ j ].second;
        }
        if ( k == 0 )
        {
            auto words = [ & ]( const char* what, const void* ours, const unsigned* want, std::size_t count ) {
                const unsigned* u = static_cast< const unsigned* >( ours );
                for ( std::size_t i = 0; i < count; ++i )
                    cmp( what, 0, static_cast< int >( i ), u[ i ], want[ i ] );
            };
            words( "omega word", f.omega.data( ), g.first[ 0 ], g.first_count[ 0 ] );
            words( "h0 word", f.h0.data( ), g.first[ 1 ], g.first_count[ 1 ] );
            words( "data word", f.data.data( ), g.first[ 2 ], g.first_count[ 2 ] );
            words( "heights word", f.heights[ 0 ].data( ), g.first[ 3 ], g.first_count[ 3 ] );
            words( "patch word", f.patches[ 0 ].data( ), g.first[ 4 ], g.first_count[ 4 ] );
        }
    }
    water_built_hooks_t hooks{};
    hooks.render = []( void* ) {};
    for ( std::size_t t = 0; t < g.ticks; ++t )
    {
        const int missing = water_built_tick( built, g.dt[ t ], hooks );
        cmp( "unrecreated paths", -1, static_cast< int >( t ), static_cast< std::uint64_t >( missing ), 0 );
        for ( std::size_t k = 0; k < oceans.size( ); ++k )
        {
            const int ki = static_cast< int >( k );
            const water_words_t& w = built.waves[ static_cast< std::size_t >( oceans[ k ] ) ];
            const unsigned* gw = g.words + per_words * k + 16 + 9 * t;
            const unsigned long long* gh = g.hash + per_hash * k + 9 + 4 * t;
            bool listed = false;
            for ( int e = built.hold.list; e >= 0; e = built.hold.pool[ e ].next )
                listed = listed || built.hold.pool[ e ].leaf == oceans[ k ];
            cmp( "alive", ki, static_cast< int >( t ), listed ? 1u : 0u, gw[ 0 ] );
            check_clock( "tick clock", ki, w, gw + 1 );
            const water_built_t::ocean_t* o = water_ocean_of( built, oceans[ k ] );
            if ( !o || o->field.n == 0 )
                continue;
            const ocean_field_t& f = o->field;
            const int cur = static_cast< int >( w.word[ 0xA4 / 4 ] & 1u );
            cmp( "tick data", ki, static_cast< int >( t ), water_fnv64( f.data.data( ), f.data.size( ) ), gh[ 0 ] );
            cmp( "tick speq", ki, static_cast< int >( t ), water_fnv64( f.speq.data( ), f.speq.size( ) ), gh[ 1 ] );
            cmp( "tick heights", ki, static_cast< int >( t ), water_fnv64( f.heights[ cur ].data( ), f.heights[ cur ].size( ) ), gh[ 2 ] );
            cmp( "tick patches", ki, static_cast< int >( t ), water_fnv64( f.patches[ cur ].data( ), f.patches[ cur ].size( ) ), gh[ 3 ] );
            hashed += f.data.size( ) + f.speq.size( ) + f.heights[ cur ].size( ) + f.patches[ cur ].size( );
        }
    }
    water_view_t view{};
    water_view_build( built, view );
    const std::size_t np = g.points_per;
    int outputs = 0, live = 0;
    for ( std::size_t k = 0; k < oceans.size( ); ++k )
    {
        const int ki = static_cast< int >( k );
        const float* pts = g.points + 2 * np * k;
        std::vector< water_vertex_t > v( np );
        for ( std::size_t i = 0; i < np; ++i )
        {
            v[ i ] = water_vertex_t{};
            v[ i ].pos[ 0 ] = pts[ 2 * i ];
            v[ i ].pos[ 1 ] = pts[ 2 * i + 1 ];
            v[ i ].disp[ 0 ] = 0.25f;
            v[ i ].disp[ 1 ] = -0.5f;
            v[ i ].disp[ 2 ] = 1.5f;
            v[ i ].height = 2.0f;
        }
        water_query_t ctx{};
        ctx.count = static_cast< int >( np );
        ctx.vertices = v.data( );
        cmp( "slot 3 answered", ki, 0, water_view_kernel( &view, oceans[ k ], ctx ) ? 1u : 0u, 1u );
        for ( std::size_t i = 0; i < np; ++i )
        {
            const float ours[ 4 ] = { v[ i ].disp[ 0 ], v[ i ].disp[ 1 ], v[ i ].disp[ 2 ], v[ i ].height };
            const float* want = g.base + 4 * ( np * k + i );
            for ( int j = 0; j < 4; ++j )
            {
                std::uint32_t a, b;
                std::memcpy( &a, &ours[ j ], 4 );
                std::memcpy( &b, &want[ j ], 4 );
                cmp( "base out", ki, static_cast< int >( 4 * i + j ), a, b );
            }
            ++outputs;
            live += want[ 3 ] != 2.0f ? 1 : 0;
        }
        std::vector< unsigned char > rec( np * 32, 0x77 );
        for ( std::size_t i = 0; i < np; ++i )
        {
            const float r[ 6 ] = { pts[ 2 * i ], pts[ 2 * i + 1 ], 2.0f, 0.25f, -0.5f, 1.5f };
            std::memcpy( &rec[ 32 * i ], r, sizeof( r ) );
        }
        water_query_t c96{};
        c96.count = static_cast< int >( np );
        c96.flag96 = 1;
        c96.records = rec.data( );
        c96.stride = 32;
        cmp( "slot 3 +96 answered", ki, 0, water_view_kernel( &view, oceans[ k ], c96 ) ? 1u : 0u, 1u );
        for ( std::size_t i = 0; i < np; ++i )
        {
            const float* want = g.k96 + 5 * ( np * k + i );
            for ( int j = 0; j < 5; ++j )
            {
                std::uint32_t a, b;
                std::memcpy( &a, &rec[ 32 * i + 8 + 4 * ( j < 4 ? j : 3 ) ], 4 );
                std::memcpy( &b, &want[ j ], 4 );
                cmp( "+96 out", ki, static_cast< int >( 5 * i + j ), a, b );
            }
            ++outputs;
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ocean field (world/field.hxx: 0x59CCB0's arrays, cells 0x59D010, dispersion 0x59D170, BuildSpectrum 0x59D220 + ran1 / "
                     "Vu_GaussianDeviate 0x50FD90, EvolveField 0x59D4D0, rlft3 0x608BB0 + fourn 0x608880, heights 0x59D640, "
                     "BuildBezierPatches 0x59D6C0; the tick 0x59CC00's rebuild) matches retail on %s %d ocean waves: %d "
                     "comparisons, %u array words hashed, %d slot 3 outputs (%d base points with a height)",
            where, static_cast< int >( oceans.size( ) ), compared, static_cast< unsigned >( hashed ), outputs, live );
    else
    {
        std::string by;
        for ( const auto& t : tally )
            by += " " + t.first + "=" + std::to_string( t.second );
        LOG_ERROR( "%s FAILED %d of %d comparisons:%s", g.label, failed, compared, by.c_str( ) );
    }
    return failed == 0;
}

inline bool verify_ocean_field( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "ocean_field: the document did not load" );
        return false;
    }
    return ocean_field_check( *tree, OCEAN_FIELD_GOLDEN( "ocean_field", of ), "Hydroelectric's" );
}

// The wakes' +96 kernels (case wake_kernels96): wake_vertex96 against 0x52D1D0 (tag 0x52DCC0) and flat_wake_vertex96
// against 0x52E7D0 (tag 0x52EAE0) on hand-built waves - height, flow x, flow y and foam of every record, bit for bit;
// the tag variants run the same body only where the record's tag equals ctx+0x5C (1).
inline bool verify_wake_kernels96( )
{
    int checked = 0, failed = 0, live = 0;
    const std::size_t np = g_golden_wk96_points_per;
    for ( std::size_t index = 0; index < g_golden_wk96_count; ++index )
    {
        const bool flat = g_golden_wk96_kind[ 2 * index ] == 1u;
        const bool tagged = g_golden_wk96_kind[ 2 * index + 1 ] == 1u;
        const float* p = g_golden_wk96_params + 29 * index;
        wake_wave_t wave{};
        wave.inner_ratio = p[ 0 ];
        wave.outer_ratio = p[ 1 ];
        wave.scale = p[ 2 ];
        std::memcpy( &wave.a, p + 3, sizeof( wave.a ) );
        std::memcpy( &wave.b, p + 16, sizeof( wave.b ) );
        for ( std::size_t i = 0; i < np; ++i )
        {
            const float x = g_golden_wk96_points[ 2 * ( np * index + i ) ];
            const float y = g_golden_wk96_points[ 2 * ( np * index + i ) + 1 ];
            float height = 2.0f;
            float flow[ 2 ] = { 0.25f, -0.5f };
            float foam = 1.5f;
            if ( !tagged || g_golden_wk96_tags[ np * index + i ] == 1u )
            {
                if ( flat )
                    flat_wake_vertex96( wave, x, y, foam );
                else
                    wake_vertex96( wave, x, y, height, flow, foam );
            }
            const float ours[ 4 ] = { height, flow[ 0 ], flow[ 1 ], foam };
            const float* want = g_golden_wk96_out + 4 * ( np * index + i );
            ++checked;
            if ( want[ 3 ] != 1.5f )
                ++live;
            if ( std::memcmp( ours, want, sizeof( ours ) ) != 0 && ++failed <= 6 )
                LOG_ERROR( "wake +96 %s variant %d set %d point %d: ours %.9g %.9g %.9g %.9g retail %.9g %.9g %.9g %.9g", flat ? "flat" : "wake",
                    tagged ? 1 : 0, static_cast< int >( index ), static_cast< int >( i ), ours[ 0 ], ours[ 1 ], ours[ 2 ], ours[ 3 ], want[ 0 ], want[ 1 ],
                    want[ 2 ], want[ 3 ] );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "wake_vertex96 / flat_wake_vertex96 match the wakes' +96 kernels 0x52D1D0 / 0x52E7D0 (tag 0x52DCC0 / 0x52EAE0) bit for "
                     "bit on %d records (%d producing output)",
            checked, live );
    else
        LOG_ERROR( "wake +96 kernels FAILED %d of %d records", failed, checked );
    return failed == 0;
}

// The water maps and VuWater's one-point query against retail (case water_point). Hydroelectric's water built by
// water_build_world, its maps fetched from Base.apf by name through water_build_maps (the fetch is the asset manager
// hook) and loaded by water_map_load (0x5C8650); then
//   1. per surface: the WaterMap name, the asset's +0x58 / +0x5C / +0x64, each byte array's size and FNV-1a 64, every
//      entry's +0 / +4 / size / FNV - against retail's own load of the same bytes;
//   2. water_map_sample (0x535E10) at points inside every mapped surface - the float bits;
//   3. water_point_query (0x5292D0 over 0x52AEC0 / 0x535E10 / 0x52B0E0, kernels water_view_kernel): all 8 out words
//      and the slot 3 calls in order; the last queries with water +0x18 set.
inline std::uint64_t water_check_fnv( const std::vector< std::uint8_t >& v )
{
    std::uint64_t x = 0xCBF29CE484222325ull;
    for ( std::uint8_t b : v )
        x = ( x ^ b ) * 0x100000001B3ull;
    return x;
}

inline bool verify_water_point( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( !tree || pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "water_point: the document or Base.apf did not load" );
        return false;
    }
    water_built_t built{};
    water_build_world( *tree, built, []( entity_t& e ) { return entity_transform( e ); } );
    const int missing = water_build_maps(
        built,
        []( void* user, const std::string& asset, std::vector< std::uint8_t >& blob ) {
            return static_cast< c_archive* >( user )->read( asset.c_str( ), blob );
        },
        &archive );
    int failed = 0;
    // 1. the loads
    int words = 0;
    if ( built.map_names.size( ) != g_golden_wpt_surfaces )
    {
        LOG_ERROR( "water_point: %d surfaces built, retail %d", static_cast< int >( built.map_names.size( ) ), static_cast< int >( g_golden_wpt_surfaces ) );
        return false;
    }
    const unsigned* m = g_golden_wpt_maps;
    for ( std::size_t k = 0; k < g_golden_wpt_surfaces; ++k )
    {
        const char* what = nullptr;
        if ( built.map_names[ k ] != g_golden_wpt_names[ k ] )
            what = "the WaterMap name";
        const water_map_t* map = built.maps[ k ].get( );
        const unsigned loaded = *m++;
        if ( !what && ( loaded != 0 ) != ( map != nullptr ) )
            what = "loaded or not";
        if ( loaded )
        {
            std::vector< unsigned > ours;
            if ( map )
            {
                ours = { map->width, map->height, static_cast< unsigned >( map->map.size( ) ) };
                for ( const std::vector< std::uint8_t >* a : { &map->map, &map->second, &map->third } )
                {
                    const std::uint64_t h = water_check_fnv( *a );
                    ours.insert( ours.end( ), { static_cast< unsigned >( a->size( ) ), static_cast< unsigned >( h ), static_cast< unsigned >( h >> 32 ) } );
                }
                ours.push_back( static_cast< unsigned >( map->entries.size( ) ) );
                for ( const water_map_t::entry_t& e : map->entries )
                {
                    const std::uint64_t h = water_check_fnv( e.bytes );
                    ours.insert( ours.end( ), { e.a, e.b, static_cast< unsigned >( e.bytes.size( ) ), static_cast< unsigned >( h ),
                                                  static_cast< unsigned >( h >> 32 ) } );
                }
            }
            const std::size_t n = 13 + 5 * static_cast< std::size_t >( m[ 12 ] );
            if ( !what && ours.size( ) != n )
                what = "the entry count";
            for ( std::size_t i = 0; i < n; ++i )
                if ( !what && ours[ i ] != m[ i ] )
                    what = "an asset word";
            words += static_cast< int >( n );
            m += n;
        }
        if ( what )
        {
            ++failed;
            LOG_ERROR( "water_point: surface %d (%s): %s", static_cast< int >( k ), g_golden_wpt_names[ k ], what );
        }
    }
    water_view_t view{};
    water_view_build( built, view );
    // 2. the samples
    int nonzero = 0;
    for ( std::size_t k = 0; k < g_golden_wpt_sample_count; ++k )
    {
        const unsigned* s = g_golden_wpt_samples + 5 * k;
        const float p[ 3 ] = { water_bits( s[ 1 ] ), water_bits( s[ 2 ] ), water_bits( s[ 3 ] ) };
        float r = 0.0f;
        const bool reached = water_map_sample( view.water.surfaces[ s[ 0 ] ], p, r );
        std::uint32_t bits;
        std::memcpy( &bits, &r, 4 );
        nonzero += s[ 4 ] != 0u ? 1 : 0;
        if ( !reached || bits != s[ 4 ] )
        {
            ++failed;
            LOG_ERROR( "water_point: sample %d on surface %u: 0x%08X, retail 0x%08X", static_cast< int >( k ), s[ 0 ], bits, s[ 4 ] );
        }
    }
    // 3. the point queries
    struct logged_t
    {
        const water_view_t* view;
        std::vector< std::uint32_t > log;
    } logged{ &view, {} };
    const water_kernel_fn kernel = []( void* user, int wave, water_query_t& ctx ) {
        logged_t& l = *static_cast< logged_t* >( user );
        l.log.push_back( static_cast< std::uint32_t >( wave ) );
        l.log.push_back( ctx.tag );
        l.log.push_back( ctx.flag100 );
        return water_view_kernel( const_cast< water_view_t* >( l.view ), wave, ctx );
    };
    int found = 0, calls = 0, skipped = 0;
    for ( std::size_t index = 0; index < g_golden_wpt_count; ++index )
    {
        const unsigned* s = g_golden_wpt_words + g_golden_wpt_offsets[ index ];
        view.water.skip_waves = s[ 0 ] != 0u;
        skipped += s[ 0 ] ? 1 : 0;
        float point[ 4 ];
        for ( int k = 0; k < 4; ++k )
            point[ k ] = water_bits( s[ 1 + k ] );
        std::uint32_t out[ 8 ];
        std::memset( out, 0x77, sizeof( out ) );
        logged.log.clear( );
        const bool reached = water_point_query( view.water, point, kernel, &logged, reinterpret_cast< float* >( out ) );
        const char* what = reached ? nullptr : "a path not recreated was reached";
        for ( int k = 0; k < 8; ++k )
            if ( !what && out[ k ] != s[ 5 + k ] )
                what = "an out word";
        found += s[ 5 + 2 ] != 0xFEFFFFFFu ? 1 : 0;
        const unsigned n = s[ 13 ];
        calls += static_cast< int >( n );
        if ( !what && logged.log.size( ) != 3 * n )
            what = "the number of slot 3 calls";
        for ( unsigned k = 0; k < 3 * n && !what; ++k )
            if ( logged.log[ k ] != s[ 14 + k ] )
                what = "a slot 3 call";
        if ( what )
        {
            ++failed;
            LOG_ERROR( "water_point: query %d: %s", static_cast< int >( index ), what );
        }
    }
    view.water.skip_waves = false;
    if ( failed == 0 )
        LOG_SUCCESS( "water_build_maps / water_map_load (VuWaterMapAsset 0x5C8650: %d surfaces, %d asset words incl. FNV-1a of every "
                     "array, %d names unfetched), water_map_sample (0x535E10: %d samples, %d non-zero) and water_point_query (0x5292D0 / "
                     "0x52AEC0 / 0x52B0E0: %d queries, %d on a surface, %d slot 3 calls, %d with waves skipped) match retail",
            static_cast< int >( g_golden_wpt_surfaces ), words, missing, static_cast< int >( g_golden_wpt_sample_count ), nonzero,
            static_cast< int >( g_golden_wpt_count ), found, calls, skipped );
    else
        LOG_ERROR( "water_point FAILED %d", failed );
    return failed == 0;
}

// pfx_boat_thrust_rate (world/spray.hxx) against VuPfxEmitBoatThrustQuadFountainInstance's emit 0x4CD5B0 (case
// boat_thrust): fake nodes and boats, the fountain 0x55D030 a recorded hook. Compared: the rate scale (+0x20) the
// fountain receives, bit for bit, and that it received dt.
inline bool verify_boat_thrust( )
{
    int failed = 0, zeroed = 0, live = 0, boatless = 0;
    for ( std::size_t index = 0; index < g_golden_bth_count; ++index )
    {
        const unsigned* s = g_golden_bth_words + 24 * index;
        static solver_rigid_t b;
        boat_view_t c{ water_bits( s[ 1 ] ), s[ 2 ], static_cast< uint8_t >( s[ 3 ] ), static_cast< uint8_t >( s[ 4 ] ), { }, 0, 0, 0, b };
        for ( int k = 0; k < 3; ++k )
            c.o50_60[ k ] = water_bits( s[ 5 + k ] );
        c.o22C_04 = water_bits( s[ 8 ] );
        c.o22C_0C = water_bits( s[ 9 ] );
        c.o22C0_470 = water_bits( s[ 10 ] );
        for ( int k = 0; k < 4; ++k )
            b.linear_velocity[ k ] = water_bits( s[ 11 + k ] );
        std::uint8_t params[ 0x100 ]{};
        std::memcpy( params + 0xC8, &s[ 15 ], 4 );
        std::uint8_t records[ 32 ]{};
        for ( int k = 0; k < 4; ++k )
            records[ 8 * k ] = static_cast< std::uint8_t >( s[ 16 + k ] );
        pfx_fountain_node_t node{};
        node.rate_scale = water_bits( s[ 20 ] );
        pfx_boat_thrust_rate( node, params, s[ 0 ] ? &c : nullptr, s[ 0 ] ? records : nullptr );
        unsigned o;
        std::memcpy( &o, &node.rate_scale, 4 );
        boatless += s[ 0 ] ? 0 : 1;
        zeroed += o == 0u ? 1 : 0;
        live += o != 0u && s[ 0 ] ? 1 : 0;
        if ( o != s[ 22 ] || s[ 23 ] != s[ 21 ] )
        {
            ++failed;
            LOG_ERROR( "boat_thrust vector %d: rate 0x%08X, retail 0x%08X", static_cast< int >( index ), o, s[ 22 ] );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "pfx_boat_thrust_rate matches VuPfxEmitBoatThrustQuadFountainInstance 0x4CD5B0 on %d vectors (%d with a live "
                     "rate, %d zero, %d boatless), the rate scale bit for bit",
            static_cast< int >( g_golden_bth_count ), live, zeroed, boatless );
    else
        LOG_ERROR( "boat_thrust FAILED %d of %d", failed, static_cast< int >( g_golden_bth_count ) );
    return failed == 0;
}

// pfx_tick_spring (world/pfx.hxx) and pfx_tick_boat_spring (world/spray.hxx) against the spring constraints' slot 1
// 0x58E1C0 / 0x4CD980 (case pfx_springs): 12 linked particles per vector, every particle word after the tick.
inline bool verify_pfx_springs( )
{
    int failed = 0, moved = 0;
    const unsigned* s = g_golden_psp_words;
    for ( std::size_t index = 0; index < g_golden_psp_count; ++index )
    {
        const bool boat = s[ 0 ] != 0u;
        const bool present = s[ 1 ] != 0u;
        const float dt = water_bits( s[ 2 ] );
        float params[ 6 ], offset[ 3 ], pos[ 3 ], vel[ 3 ];
        for ( int k = 0; k < 6; ++k )
            params[ k ] = water_bits( s[ 3 + k ] );
        for ( int k = 0; k < 3; ++k )
        {
            offset[ k ] = water_bits( s[ 9 + k ] );
            pos[ k ] = water_bits( s[ 12 + k ] );
            vel[ k ] = water_bits( s[ 15 + k ] );
        }
        s += 18;
        int bad = 0;
        for ( int i = 0; i < 12; ++i, s += 64 )
        {
            std::uint32_t w[ 32 ];
            std::memcpy( w, s, sizeof( w ) );
            float* p = reinterpret_cast< float* >( w );
            if ( boat )
                pfx_tick_boat_spring( p, params, present ? pos : nullptr, present ? vel : nullptr, dt );
            else
                pfx_tick_spring( p, params, offset, dt );
            moved += std::memcmp( s, s + 32, 128 ) != 0 ? 1 : 0;
            bad += std::memcmp( w, s + 32, 128 ) != 0 ? 1 : 0;
        }
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "pfx_springs vector %d (%s): %d particles differ", static_cast< int >( index ), boat ? "boat" : "spring", bad );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "pfx_tick_spring / pfx_tick_boat_spring match VuPfxSpringConstraintInstance 0x58E1C0 and "
                     "VuPfxBoatSpringConstraintInstance 0x4CD980 on %d vectors x 12 particles (%d moved), every word",
            static_cast< int >( g_golden_psp_count ), moved );
    else
        LOG_ERROR( "pfx_springs FAILED %d of %d", failed, static_cast< int >( g_golden_psp_count ) );
    return failed == 0;
}

// pfx_boat_hull_emit (world/spray.hxx) against VuPfxEmitBoatHullInstance's emit 0x4CE760 (case boat_hull): fake nodes,
// boats and fluids objects (triangle and edge records written before the call), VuFluidSolver_prepare 0x524450 and the
// one-point query 0x5292D0 recorded hooks (both verified on their own). Compared: every call in order (allocations, the
// prepare's matrix, each query's point x / y / z - its w is UNKNOWN on the blended path -, the sample each particle
// picked, the loop's sample count and total weight), the accumulator, the VuRand after, every particle word (+0x14
// skipped where a sample was picked: retail's stale stack).
struct bhu_check_t
{
    const unsigned* calls = nullptr;
    int ncalls = 0;
    int at = 0;
    const unsigned* cursor = nullptr;
    std::vector< pfx_quad_particle_t > parts;
    std::vector< int > picks;
    int bad = 0;
    void skip_tally( )
    {
        while ( at < ncalls && cursor[ 1 ] == 4u )   // the tally record is compared after the call
        {
            cursor += 1 + cursor[ 0 ];
            ++at;
        }
    }
    const unsigned* next( unsigned type )
    {
        skip_tally( );
        if ( at >= ncalls || cursor[ 1 ] != type )
        {
            ++bad;
            return nullptr;
        }
        const unsigned* c = cursor + 1;
        cursor += 1 + cursor[ 0 ];
        ++at;
        return c;
    }
};

inline unsigned bhu_word( float f )
{
    unsigned u;
    std::memcpy( &u, &f, 4 );
    return u;
}

inline bool verify_boat_hull( )
{
    int failed = 0, emitted = 0, blended = 0, sample0 = 0, capped = 0;
    for ( std::size_t index = 0; index < g_golden_bhu_count; ++index )
    {
        const unsigned* s = g_golden_bhu_words + g_golden_bhu_offsets[ index ];
        const bool present = s[ 0 ] != 0u;
        const float dt = water_bits( s[ 1 ] );
        float accumulator = water_bits( s[ 2 ] );
        pfx_rand_t rand;
        rand.seed = static_cast< std::int32_t >( s[ 3 ] );
        rand.previous = static_cast< std::int32_t >( s[ 4 ] );
        std::memcpy( rand.table, s + 5, 128 );
        s += 37;
        static pfx_hull_boat_t boat;
        std::memcpy( boat.transform, s, 64 );
        std::memcpy( boat.offset, s + 16, 16 );
        boat.part_flag = static_cast< std::uint8_t >( s[ 20 ] );
        boat.part_count = static_cast< std::int32_t >( s[ 21 ] );
        std::memcpy( boat.part_matrix, s + 22, 64 );
        s += 38;
        pfx_hull_body_t* bodies[ 2 ] = { &boat.body, &boat.part_body };
        for ( int b = 0; b < 2; ++b, s += 12 )
        {
            std::memcpy( bodies[ b ]->position, s, 12 );
            std::memcpy( bodies[ b ]->linear, s + 4, 12 );
            std::memcpy( bodies[ b ]->angular, s + 8, 12 );
        }
        std::uint8_t params[ 0x80 ]{};
        for ( int k = 0, o = 0x30; o < 0x74; o += 4 )
            if ( o != 0x5C )
                std::memcpy( params + o, &s[ k++ ], 4 );
        s += 16;
        for ( int c = 0; c < 4; ++c )
            params[ 0x5C + c ] = static_cast< std::uint8_t >( s[ c ] );
        s += 5;   // + the allocations granted (the recorded calls carry each answer)
        const int ntri = static_cast< int >( s[ 0 ] ), nedge = static_cast< int >( s[ 1 ] ), ncam = static_cast< int >( s[ 2 ] );
        s += 3;
        static fluids_mesh_t mesh;
        mesh.triangles.assign( s, s + 15 * ntri );
        s += 15 * ntri;
        static fluid_solver_t solver;
        solver.mesh = &mesh;
        solver.edges.resize( static_cast< std::size_t >( nedge ) );
        std::memcpy( solver.edges.data( ), s, 40 * static_cast< std::size_t >( nedge ) );
        s += 10 * nedge;
        float cameras[ 3 * 4 ];
        std::memcpy( cameras, s, 12 * static_cast< std::size_t >( ncam ) );
        s += 3 * ncam;
        const unsigned acc_after = s[ 0 ];
        const unsigned* rng_after = s + 1;
        s += 35;
        bhu_check_t ck;
        ck.ncalls = static_cast< int >( s[ 0 ] );
        ck.calls = ck.cursor = s + 1;
        const unsigned* tally_rec = nullptr;
        const unsigned* q = s + 1;
        for ( int c = 0; c < ck.ncalls; ++c, q += 1 + q[ 0 ] )
            if ( q[ 1 ] == 4u )
                tally_rec = q + 1;
        const int nparts = static_cast< int >( q[ 0 ] );
        const unsigned* golden_parts = q + 1;
        ck.parts.resize( 64 );
        for ( auto& p : ck.parts )
            for ( auto& w : p.w )
                w = 0x77777777u;
        int handed = 0;
        pfx_hull_hooks_t hooks;
        hooks.user = &ck;
        hooks.prepare = []( void* u, const float* m ) {
            auto& c = *static_cast< bhu_check_t* >( u );
            const unsigned* r = c.next( 1u );
            if ( r && std::memcmp( r + 1, m, 64 ) != 0 )
                ++c.bad;
        };
        hooks.point = []( void* u, const float* p, float* out ) {
            auto& c = *static_cast< bhu_check_t* >( u );
            const unsigned* r = c.next( 2u );
            if ( !r )
                return false;
            if ( std::memcmp( r + 1, p, 12 ) != 0 )
                ++c.bad;
            std::memcpy( out, r + 5, 32 );
            return true;
        };
        static int* handed_p;
        handed_p = &handed;
        hooks.allocate = []( void* u ) -> pfx_quad_particle_t* {
            auto& c = *static_cast< bhu_check_t* >( u );
            const unsigned* r = c.next( 0u );
            if ( !r || static_cast< int >( r[ 1 ] ) < 0 )
                return nullptr;
            if ( static_cast< int >( r[ 1 ] ) != *handed_p )
                ++c.bad;
            return &c.parts[ static_cast< std::size_t >( ( *handed_p )++ ) ];
        };
        hooks.picked = []( void* u, int sample ) {
            auto& c = *static_cast< bhu_check_t* >( u );
            const unsigned* r = c.next( 3u );
            if ( !r || static_cast< int >( r[ 1 ] ) != sample )
                ++c.bad;
            c.picks.push_back( sample );
        };
        pfx_hull_tally_t tally;
        const int unknown = pfx_boat_hull_emit( accumulator, params, present ? &boat : nullptr, solver, rand, cameras, ncam, dt, hooks, &tally );
        int bad = ck.bad + unknown;
        ck.skip_tally( );
        bad += ck.at == ck.ncalls ? 0 : 1;
        if ( ( tally_rec != nullptr ) != tally.reached ||
             ( tally_rec && ( static_cast< int >( tally_rec[ 1 ] ) != tally.samples || tally_rec[ 2 ] != bhu_word( tally.weight ) ) ) )
            ++bad;
        if ( bhu_word( accumulator ) != acc_after )
            ++bad;
        if ( static_cast< std::uint32_t >( rand.seed ) != rng_after[ 0 ] || static_cast< std::uint32_t >( rand.previous ) != rng_after[ 1 ] ||
             std::memcmp( rand.table, rng_after + 2, 128 ) != 0 )
            ++bad;
        if ( handed != nparts )
            ++bad;
        for ( int i = 0; i < nparts && i < handed; ++i )
        {
            const unsigned* g = golden_parts + 32 * i;
            const bool picked = i < static_cast< int >( ck.picks.size( ) ) && ck.picks[ static_cast< std::size_t >( i ) ] >= 0;
            for ( int w = 0; w < 30; ++w )
                if ( !( w == 5 && picked ) && ck.parts[ static_cast< std::size_t >( i ) ].w[ w ] != g[ w ] )
                    ++bad;
            bad += g[ 30 ] == 0x77777777u && g[ 31 ] == 0x77777777u ? 0 : 1;
            blended += picked ? 1 : 0;
            sample0 += picked ? 0 : 1;
        }
        emitted += nparts;
        capped += tally.samples == 128 ? 1 : 0;
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "boat_hull vector %d: %d differences (calls %d of %d, unknown %d)", static_cast< int >( index ), bad, ck.at, ck.ncalls, unknown );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "pfx_boat_hull_emit matches VuPfxEmitBoatHullInstance 0x4CE760 on %d vectors: %d particles (%d from a picked "
                     "sample, %d from sample 0), %d vectors at the 128-sample cap - calls, accumulator, VuRand, every particle word",
            static_cast< int >( g_golden_bhu_count ), emitted, blended, sample0, capped );
    else
        LOG_ERROR( "boat_hull FAILED %d of %d", failed, static_cast< int >( g_golden_bhu_count ) );
    return failed == 0;
}

// world/pfxclass.hxx's g_pfx_classes and its creates / inits / resets against each VuPfx node class's slots run by
// retail (cases pfx_class_*, one per class): the vtable's slot addresses, getType's descriptor and chain, the sizes,
// every word slot 7 / slot 9 leave on two fills (the pattern instances' +0x38 / +0x48 skipped: stack words 0x58B240
// never sets), the instance's slot 0 with its child calls. One line per class.
inline bool verify_pfx_classes( )
{
    int failed = 0;
    for ( std::size_t index = 0; index < g_golden_pcl_count; ++index )
    {
        const unsigned* s = g_golden_pcl_words + g_golden_pcl_offsets[ index ];
        const char* name = g_golden_pcl_names[ index ];
        const pfx_class_t* c = pfx_class_find( name );
        int bad = 0;
        if ( !c )
        {
            ++failed;
            LOG_ERROR( "pfx_class %s: not in g_pfx_classes", name );
            continue;
        }
        const unsigned* slots = s + 1;
        bad += s[ 0 ] != c->vtable;
        bad += slots[ 1 ] != c->get_type;
        bad += s[ 11 ] != c->descriptor;
        bad += std::strcmp( g_golden_pcl_chains[ index ], c->chain ) != 0;
        bad += c->size_slot ? ( slots[ 6 ] != c->size_slot || s[ 12 ] != c->instance_size ) : s[ 12 ] != 0u;
        const bool created = s[ 13 ] != 0u;
        bad += created != ( c->create != 0u );
        s += 14;
        const bool pattern = c->create == 0x58E9A0u || c->create == 0x58FAA0u || c->create == 0x591470u;
        if ( created )
        {
            bad += slots[ 7 ] != c->create;
            for ( int f = 0; f < 2; ++f, s += 33 )
            {
                std::uint32_t w[ 32 ];
                for ( auto& x : w )
                    x = f ? 0x33333333u : 0xCCCCCCCCu;
                pfx_create_class( c->create, w );
                bad += s[ 0 ] != 0u;
                for ( int k = 0; k < 32; ++k )
                    if ( !( pattern && ( k == 0x38 / 4 || k == 0x48 / 4 ) ) && w[ k ] != s[ 1 + k ] )
                        ++bad;
            }
            const unsigned reset_slot = *s++;
            bad += c->reset ? reset_slot != c->reset : reset_slot != 0x401560u;
            for ( int f = 0; f < 2; ++f )
            {
                const int calls = static_cast< int >( s[ 0 ] );
                const unsigned* order = s + 1;
                s += 1 + calls;
                std::uint32_t w[ 32 ];
                std::memcpy( w, s, sizeof( w ) );
                struct kids_t { int n; int at[ 4 ]; } kids{ 0, { } };
                auto child = []( void* u, int k ) { auto& q = *static_cast< kids_t* >( u ); if ( q.n < 4 ) q.at[ q.n ] = k; ++q.n; };
                switch ( c->reset )
                {
                case 0x55D020u: pfx_fountain_reset( w ); break;
                case 0x4CE750u: pfx_hull_reset( w ); break;
                case 0x58FE80u: pfx_pattern_reset( w, false, 2, child, &kids ); break;
                case 0x58F080u: pfx_pattern_reset( w, true, 2, child, &kids ); break;
                default: break;   // 0x401560: a nullsub
                }
                bad += kids.n != calls;
                for ( int k = 0; k < calls && k < 4; ++k )
                    bad += kids.at[ k ] != static_cast< int >( order[ k ] );
                bad += std::memcmp( w, s + 32, sizeof( w ) ) != 0;
                s += 64;
            }
        }
        bad += c->particle_slot ? ( slots[ 8 ] != c->particle_slot || s[ 0 ] != c->particle_size ) : s[ 0 ] != 0u;
        const bool particle = s[ 1 ] != 0u;
        bad += particle != ( c->particle != 0u );
        s += 2;
        if ( particle )
        {
            bad += slots[ 9 ] != c->particle;
            for ( int f = 0; f < 2; ++f, s += 32 )
            {
                std::uint32_t w[ 32 ];
                for ( auto& x : w )
                    x = f ? 0x33333333u : 0xCCCCCCCCu;
                pfx_particle_class( c->particle, w );
                bad += std::memcmp( w, s, sizeof( w ) ) != 0;
            }
        }
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "pfx_class %s: %d differences", name, bad );
        }
        else
            LOG_SUCCESS( "pfx_class %s matches retail (vtable 0x%X): getType 0x%X -> %s%s%s%s", name, c->vtable, c->get_type, c->chain,
                c->size_slot ? ", instance size / create" : "", c->reset ? ", reset" : "", c->particle ? ", particle size / init" : "" );
    }
    return failed == 0;
}

// world/pfxclass.hxx's VuPfxSystemInstance slots against retail's (case pfx_system_instance): per vector each slot on
// a fresh instance, its return and all 48 instance words after (slot 2's +0x78 skipped: a stack word retail never
// sets), the draw passes' pattern calls.
inline bool verify_pfx_system_instance( )
{
    int failed = 0, compared = 0;
    const unsigned* s = g_golden_psi_words;
    for ( std::size_t index = 0; index < g_golden_psi_count; ++index )
    {
        const unsigned* words = s;
        const unsigned duration = s[ 48 ];
        const int patterns = static_cast< int >( s[ 49 ] );
        const bool enabled = s[ 50 ] != 0u;
        s += 51;
        int bad = 0;
        for ( int n = 0; n < 14; ++n )
        {
            const unsigned slot = s[ 0 ];
            const unsigned* arg = s + 1;
            const unsigned ret = s[ 17 ];
            const unsigned* after = s + 18;
            const int calls = static_cast< int >( s[ 66 ] );
            const unsigned* seen = s + 67;
            s += 67 + 3 * calls;
            std::uint32_t w[ 48 ];
            std::memcpy( w, words, sizeof( w ) );
            w[ 0 ] = 0x74FF8Cu;
            std::uint32_t params[ 16 ]{};
            params[ 0x30 / 4 ] = duration;
            unsigned r = 0;
            struct pass_t { unsigned kind, arg; int n; unsigned got[ 12 ]; } pass{ slot == 15u ? 3u : 2u, arg[ 0 ], 0, { } };
            auto record = []( void* u, int k ) {
                auto& p = *static_cast< pass_t* >( u );
                if ( p.n < 4 )
                {
                    p.got[ 3 * p.n ] = p.kind;
                    p.got[ 3 * p.n + 1 ] = static_cast< unsigned >( k );
                    p.got[ 3 * p.n + 2 ] = p.arg;
                }
                ++p.n;
            };
            switch ( slot )
            {
            case 0: pfx_system_set_position( w, arg ); r = ret; break;   // eax: whatever the copy left (not a result)
            case 1: pfx_system_set_rotation( w, arg ); r = ret; break;
            case 2: pfx_system_set_matrix( w, arg ); w[ 0x78 / 4 ] = after[ 0x78 / 4 ]; r = ret; break;
            case 3: pfx_system_set_words5c( w, arg ); r = ret; break;
            case 4: pfx_system_set_scale( w, arg[ 0 ] ); r = ret; break;
            case 5: pfx_system_set_wordsac( w, arg ); r = ret; break;
            case 6: r = pfx_system_state( w ); break;
            case 7: r = pfx_system_aabb_offset( ); break;
            case 8: r = pfx_system_count( w ); break;
            case 9: r = pfx_system_clock( w ); break;
            case 10: r = pfx_system_duration( params ); break;
            case 14:
            case 15: pfx_system_draw( enabled, patterns, record, &pass ); r = ret; break;
            case 16: r = 0u; break;   // this itself
            default: ++bad; break;
            }
            bad += r != ret;
            bad += std::memcmp( w, after, sizeof( w ) ) != 0;
            bad += pass.n != calls;
            if ( calls <= 4 && pass.n == calls )
                bad += calls && std::memcmp( pass.got, seen, 12 * static_cast< std::size_t >( calls ) ) != 0;
            ++compared;
        }
        if ( bad )
        {
            ++failed;
            LOG_ERROR( "pfx_system_instance vector %d: %d differences", static_cast< int >( index ), bad );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuPfxSystemInstance slots 0 .. 10, 14, 15, 16 (0x58ABF0 .. 0x58A700, 0x58AF60, 0x58AFA0, 0x401DA0) match retail on %d "
                     "vectors (%d slot runs): returns, every instance word, the draw passes' pattern calls",
            static_cast< int >( g_golden_psi_count ), compared );
    else
        LOG_ERROR( "pfx_system_instance FAILED %d of %d", failed, static_cast< int >( g_golden_psi_count ) );
    return failed == 0;
}

// The WaterMap through the race project's templates: HydroelectricRace loaded the way c_world loads it (checks/world.hxx
// race_check_tree), its water built (water_build_world) - each surface's WaterMap must be the one retail's own load of
// the level template gives (case water_point's names: VuAssetProperty<VuWaterMapAsset>'s mark 0x406A70 copies the
// template's value into the default, its load 0x406C20 resets to that default), and every named map must load from the
// pack (water_build_maps).
inline bool verify_water_template_maps( )
{
    c_archive archive;
    std::map< std::string, value_t > templates;
    const std::unique_ptr< entity_t > tree = race_check_tree( archive, templates, "VuProjectAsset/Games/HydroelectricRace" );
    if ( !tree )
    {
        LOG_ERROR( "water_template_maps: HydroelectricRace did not load (pack missing?)" );
        return false;
    }
    water_built_t built{};
    water_build_world( *tree, built, []( entity_t& e ) { return entity_transform( e ); } );
    const int missing = water_build_maps(
        built, []( void* user, const std::string& asset, std::vector< std::uint8_t >& blob ) { return static_cast< c_archive* >( user )->read( asset.c_str( ), blob ); },
        &archive );
    int bad = 0, named = 0;
    if ( built.map_names.size( ) != g_golden_wpt_surfaces )
        ++bad;
    for ( std::size_t k = 0; k < built.map_names.size( ) && k < g_golden_wpt_surfaces; ++k )
    {
        named += built.map_names[ k ].empty( ) ? 0 : 1;
        if ( built.map_names[ k ] != g_golden_wpt_names[ k ] )
        {
            if ( ++bad <= 4 )
                LOG_ERROR( "water_template_maps: surface %d names \"%s\", retail's template \"%s\"", static_cast< int >( k ), built.map_names[ k ].c_str( ),
                    g_golden_wpt_names[ k ] );
        }
    }
    bad += missing;
    if ( bad == 0 )
        LOG_SUCCESS( "WaterMap through the race project's templates (0x406A70 mark / 0x406C20 load): %d of %d HydroelectricRace surfaces name "
                     "retail's map, all %d load", named, static_cast< int >( built.map_names.size( ) ), named );
    else
        LOG_ERROR( "water_template_maps FAILED: %d differences (%d of %d surfaces named, %d not loaded)", bad, named,
            static_cast< int >( built.map_names.size( ) ), missing );
    return bad == 0;
}

// query.hxx water_surface_distance against retail's 0x594BD0 (case water_surface_distance): every answer word for word.
inline bool verify_water_surface_distance( )
{
    const unsigned* w = g_golden_wsdist_words;
    const unsigned n = *w++;
    int bad = 0;
    auto f = []( unsigned x ) { float r; std::memcpy( &r, &x, 4 ); return r; };
    for ( unsigned v = 0; v < n; ++v, w += 24 )
    {
        water_surface_t s{ };
        s.below = f( w[ 0 ] );
        s.above = f( w[ 1 ] );
        for ( int k = 0; k < 16; ++k )
            s.inverse[ k ] = f( w[ 2 + k ] );
        s.half_x = f( w[ 18 ] );
        s.half_y = f( w[ 19 ] );
        const float p[ 3 ] = { f( w[ 20 ] ), f( w[ 21 ] ), f( w[ 22 ] ) };
        const float d = water_surface_distance( s, p );
        unsigned got;
        std::memcpy( &got, &d, 4 );
        if ( got != w[ 23 ] && bad++ < 4 )
            LOG_ERROR( "water_surface_distance vector %u: 0x%08X, retail 0x%08X", v, got, w[ 23 ] );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuWaterSurface's point-to-box distance (0x594BD0) matches retail: %u points", n );
    else
        LOG_ERROR( "water_surface_distance FAILED %d", bad );
    return bad == 0;
}

// surface.hxx water_reflection_nearest against retail's VuWater 0x529470 (case water_reflection) on Hydroelectric's water
// as water_build_world builds it: each surface's ProceduralReflection word (+0x38, loaded), then every query's height and
// distance words, with the query's disabled surfaces' +0x38 cleared as the case poked them.
inline bool verify_water_reflection( )
{
    const std::unique_ptr< entity_t > tree = water_build_document_tree( );
    if ( !tree )
    {
        LOG_ERROR( "water_reflection: the document did not load" );
        return false;
    }
    water_built_t built{};
    water_build_world( *tree, built, []( entity_t& e ) { return entity_transform( e ); } );
    const unsigned* w = g_golden_wrefl_words;
    const unsigned ns = *w++;
    int bad = 0;
    if ( ns != built.surfaces.size( ) )
    {
        LOG_ERROR( "water_reflection: %d surfaces, retail %u", static_cast< int >( built.surfaces.size( ) ), ns );
        return false;
    }
    for ( unsigned k = 0; k < ns; ++k )
        if ( built.surfaces[ k ].word[ 0x38 / 4 ] != w[ k ] && bad++ < 4 )
            LOG_ERROR( "water_reflection surface %u +0x38: 0x%08X, retail 0x%08X", k, built.surfaces[ k ].word[ 0x38 / 4 ], w[ k ] );
    w += ns;
    const unsigned n = *w++;
    for ( unsigned v = 0; v < n; ++v )
    {
        float p[ 3 ];
        std::memcpy( p, w, 12 );
        w += 3;
        const unsigned nd = *w++;
        std::vector< std::uint32_t > saved( ns );
        for ( unsigned k = 0; k < ns; ++k )
            saved[ k ] = built.surfaces[ k ].word[ 0x38 / 4 ];
        for ( unsigned k = 0; k < nd; ++k )
            built.surfaces[ w[ k ] ].word[ 0x38 / 4 ] &= ~0xFFu;
        w += nd;
        float height = 0.0f, distance = 0.0f;
        water_reflection_nearest( built, p, height, distance );
        for ( unsigned k = 0; k < ns; ++k )
            built.surfaces[ k ].word[ 0x38 / 4 ] = saved[ k ];
        unsigned hw, dw;
        std::memcpy( &hw, &height, 4 );
        std::memcpy( &dw, &distance, 4 );
        if ( ( hw != w[ 0 ] || dw != w[ 1 ] ) && bad++ < 4 )
            LOG_ERROR( "water_reflection query %u: height 0x%08X distance 0x%08X, retail 0x%08X 0x%08X", v, hw, dw, w[ 0 ], w[ 1 ] );
        w += 2;
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuWater's nearest reflecting surface (0x529470) matches retail on Hydroelectric's water: %u surfaces' ProceduralReflection, %u queries", ns, n );
    else
        LOG_ERROR( "water_reflection FAILED %d", bad );
    return bad == 0;
}

// surface.hxx water_reflection_plane_step against retail's viewport tick 0x530F10 (case reflection_plane): 400 ticks over a one-surface VuWater whose height and
// point the case moves, NaN heights among them; the four state words after each.
inline bool verify_reflection_plane( )
{
    int bad = 0;
    water_reflection_plane_t v;
    for ( unsigned k = 0; k < g_golden_rplane_size; k += 6 )
    {
        const unsigned* w = g_golden_rplane_words + k;
        float height, distance;
        std::memcpy( &height, &w[ 0 ], 4 );
        std::memcpy( &distance, &w[ 1 ], 4 );
        water_reflection_plane_step( v, height, distance );
        unsigned o[ 4 ];
        std::memcpy( &o[ 0 ], &v.m_plane, 4 );
        std::memcpy( &o[ 1 ], &v.m_from, 4 );
        std::memcpy( &o[ 2 ], &v.m_target, 4 );
        std::memcpy( &o[ 3 ], &v.m_start, 4 );
        for ( int i = 0; i < 4; ++i )
            if ( o[ i ] != w[ 2 + i ] && bad++ < 4 )
                LOG_ERROR( "reflection_plane tick %u word %d: 0x%08X, retail 0x%08X", k / 6, i, o[ i ], w[ 2 + i ] );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "the viewport's reflection plane (0x530F10) matches retail: %u ticks, 4 words each", g_golden_rplane_size / 6 );
    else
        LOG_ERROR( "reflection_plane FAILED %d", bad );
    return bad == 0;
}

// engine entity/values.hxx's property save side against retail's slots (case property_save): per node (every property of
// Hydroelectric's water entities as retail loaded them, plus synthetic vector2 / int / colour nodes) slot 5 / 6 / 3 / 3 at
// the default as JSON text, the target after slot 8, slot 1 for vector2 / int enum; the notify and string-enum rows.
inline bool verify_property_save( )
{
    const unsigned* w = g_golden_psave_words;
    const char* const* s = g_golden_psave_text;
    const unsigned n = *w++;
    int bad = 0;
    auto fail = [ & ]( unsigned v, const char* what, const std::string& ours, const char* theirs ) {
        if ( bad++ < 6 )
            LOG_ERROR( "property_save node %u %s: ours %s, retail %s", v, what, ours.c_str( ), theirs );
    };
    for ( unsigned v = 0; v < n; ++v )
    {
        property_node_t node;
        node.cls = static_cast< property_class_t >( *w++ );
        const unsigned slot1 = *w++;
        std::memcpy( node.words, w, 32 );
        w += 8;
        std::memcpy( node.target, w, 16 );
        w += 4;
        const unsigned* reset = w;
        w += 4;
        const unsigned nc = *w++;
        std::vector< property_choice_t > choices( nc );
        node.name = *s++;
        const char* texts[ 4 ] = { s[ 0 ], s[ 1 ], s[ 2 ], s[ 3 ] };
        s += 4;
        for ( unsigned k = 0; k < nc; ++k )
            choices[ k ] = { *s++, static_cast< std::int32_t >( w[ k ] ) };
        w += nc;
        node.choices = choices.data( );
        node.choice_count = static_cast< int >( nc );
        value_t j;
        property_write( node, j );
        if ( property_json_text( j ) != texts[ 0 ] )
            fail( v, "slot 5", property_json_text( j ), texts[ 0 ] );
        j = value_t{ };
        property_write_default( node, j );
        if ( property_json_text( j ) != texts[ 1 ] )
            fail( v, "slot 6", property_json_text( j ), texts[ 1 ] );
        j = value_t{ };
        property_save( node, j );
        if ( property_json_text( j ) != texts[ 2 ] )
            fail( v, "slot 3", property_json_text( j ), texts[ 2 ] );
        property_node_t at = node;
        const int lanes = node.cls == property_class_t::vector3 || node.cls == property_class_t::rotation3d ? 3
                        : node.cls == property_class_t::vector2 ? 2 : 1;
        if ( node.cls == property_class_t::boolean )
            at.target[ 0 ] = ( at.target[ 0 ] & ~0xFFu ) | ( at.words[ 0 ] & 0xFFu );
        else
            std::memcpy( at.target, at.words, 4 * lanes );
        j = value_t{ };
        property_save( at, j );
        if ( property_json_text( j ) != texts[ 3 ] )
            fail( v, "slot 3 at the default", property_json_text( j ), texts[ 3 ] );
        property_reset( node );
        const int words = node.cls == property_class_t::vector3 || node.cls == property_class_t::rotation3d ? 4 : lanes;
        if ( std::memcmp( node.target, reset, 4 * words ) != 0 )
            fail( v, "slot 8", "(target words)", "(differs)" );
        if ( node.cls == property_class_t::vector2 && slot1 != static_cast< unsigned >( property_type_vector2( ) ) )
            fail( v, "slot 1", std::to_string( property_type_vector2( ) ), "vector2" );
        if ( node.cls == property_class_t::int_enum && slot1 != static_cast< unsigned >( property_type_int_enum( ) ) )
            fail( v, "slot 1", std::to_string( property_type_int_enum( ) ), "int enum" );
    }
    const unsigned nn = *w++;
    for ( unsigned k = 0; k < nn; ++k, w += 4 )
        if ( w[ 2 ] != static_cast< unsigned >( property_type_notify( ) ) || ( w[ 3 ] != 0 ) != property_notify_read( w[ 0 ] != 0, w[ 1 ] != 0 ) )
            fail( k, "notify", "", "" );
    const unsigned ns = *w++;
    for ( unsigned k = 0; k < ns; ++k, w += 2 )
    {
        std::vector< const char* > names( w[ 0 ], "Synthetic" );
        names.push_back( nullptr );
        if ( static_cast< unsigned >( property_string_enum_count( names.data( ) ) ) != w[ 1 ] )
            fail( k, "string enum", "", "" );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "The property classes' save / write / default / reset slots match retail: %u nodes (Hydroelectric's water entities as loaded + synthetic), %u notify, %u string-enum rows", n, nn, ns );
    else
        LOG_ERROR( "property_save FAILED %d", bad );
    return bad == 0;
}

#include <workspace/utility/checks/pfx.hxx>
#include <workspace/utility/checks/wake.hxx>
#include <workspace/utility/checks/watchers.hxx>
#include <workspace/utility/checks/wrapper.hxx>
#include <workspace/utility/checks/logic.hxx>
#include <workspace/utility/checks/boxes.hxx>
#include <workspace/utility/checks/render.hxx>

inline bool verify_water_all( )
{
    bool ok = true;
    ok = verify_render_all( ) && ok;
    ok = verify_pfx_all( ) && ok;
    ok = verify_water_template_maps( ) && ok;
    ok = verify_pfx_system_instance( ) && ok;
    ok = verify_pfx_classes( ) && ok;
    ok = verify_boat_hull( ) && ok;
    ok = verify_pfx_springs( ) && ok;
    ok = verify_boat_thrust( ) && ok;
    ok = verify_water_point( ) && ok;
    ok = verify_wake_kernels96( ) && ok;
    ok = verify_ocean_field( ) && ok;
    ok = verify_water_wake( ) && ok;
    ok = verify_wake_reuse( ) && ok;
    ok = verify_water_texture( ) && ok;
    ok = verify_watchers_all( ) && ok;
    ok = verify_dynamics_impl( ) && ok;
    ok = verify_rigid_collide( ) && ok;
    ok = verify_simple_mesh( ) && ok;
    ok = verify_collision_contact( ) && ok;
    ok = verify_collision_impact( ) && ok;
    ok = verify_collision_init( ) && ok;
    ok = verify_collision_scrape( ) && ok;
    ok = verify_dynamics_sync( ) && ok;
    ok = verify_dynamics_create( ) && ok;
    ok = verify_logic_plugs( ) && ok;
    ok = verify_game_plugs( ) && ok;
    ok = verify_front_props( ) && ok;
    ok = verify_oneshot_pfx( ) && ok;
    ok = verify_message_box( ) && ok;
    ok = verify_ui_odds( ) && ok;
    ok = verify_ramp_contact( ) && ok;
    ok = verify_prop_box_bodies( ) && ok;
    ok = verify_system_language( ) && ok;
    ok = verify_gamepad_box( ) && ok;
    ok = verify_dock_tick( ) && ok;
    ok = verify_oneshot_watch( ) && ok;
    ok = verify_force_watch( ) && ok;
    ok = verify_logic_game( ) && ok;
    ok = verify_logic_types( ) && ok;
    ok = verify_control_entity( ) && ok;
    ok = verify_lens_water( ) && ok;
    ok = verify_lens_drop_fill( ) && ok;
    ok = verify_lens_water_tick( ) && ok;
    ok = verify_lens_emitters( ) && ok;
    ok = verify_entity_sort( ) && ok;
    ok = verify_json_write( ) && ok;
    ok = verify_json_access( ) && ok;
    ok = verify_json_text( ) && ok;
    ok = verify_json_member( ) && ok;
    ok = verify_entity_repository( ) && ok;
    ok = verify_repository_tree( ) && ok;
    ok = verify_tick_slots( ) && ok;
    ok = verify_entity_dtors( ) && ok;
    ok = verify_framework_dtors( ) && ok;
    ok = verify_event_manager( ) && ok;
    ok = verify_voice_over( ) && ok;
    ok = verify_audio_emitter( ) && ok;
    ok = verify_force_affector( ) && ok;
    ok = verify_settings_triggers( ) && ok;
    ok = verify_key_motion( ) && ok;
    ok = verify_key_curve( ) && ok;
    ok = verify_rotate_axis_angle( ) && ok;
    ok = verify_water_surface_distance( ) && ok;
    ok = verify_water_reflection( ) && ok;
    ok = verify_reflection_plane( ) && ok;
    ok = verify_property_save( ) && ok;
    ok = verify_ocean_kernels96( ) && ok;
    ok = verify_water_built_tick( ) && ok;
    ok = verify_water_query96( ) && ok;
    ok = verify_water_build( ) && ok;
    ok = verify_water_query( ) && ok;
    ok = verify_point_waves( ) && ok;
    ok = verify_ocean_kernels( ) && ok;
    ok = verify_wake_kernels( ) && ok;
    ok = verify_wave_bridges( ) && ok;
    ok = verify_water_tick( ) && ok;
    ok = verify_wave_ticks( ) && ok;
    ok = verify_pfx_ticks( ) && ok;
    ok = verify_matrix_helpers( ) && ok;
    ok = verify_water_tree( ) && ok;
    ok = verify_water_waves( ) && ok;
    ok = verify_wake_lookup( ) && ok;
    ok = verify_quad_pattern_tick( ) && ok;
    ok = verify_geom_pattern_tick( ) && ok;
    ok = verify_fountain_emit( ) && ok;
    return ok;
}
