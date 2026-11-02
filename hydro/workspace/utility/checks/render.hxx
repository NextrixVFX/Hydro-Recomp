#pragma once

#include <workspace/core/world/dbrt.hxx>
#include <workspace/core/world/gfxsort.hxx>
#include <workspace/core/world/lights.hxx>
#include <workspace/core/world/instance.hxx>
#include <workspace/core/world/meshes.hxx>
#include <workspace/core/world/corona.hxx>
#include <workspace/core/world/wrap.hxx>
#include <workspace/core/world/primitive.hxx>
#include <workspace/core/world/drawer.hxx>
#include <workspace/core/world/skeleton.hxx>

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <cstring>
#include <string>
#include <vector>

// The render layer's CPU side against retail (water batch 30; tools oracle/cases/render.py, emit/render.py).

// The findNodeByName probes the case makes (cases/render.py anim_find_names): every bone name, then "", "NoSuchBone",
// bone 0's name + "x" and (when longer than one character) bone 0's name less its last character.
inline std::vector< std::string > anim_find_names( const skel_skeleton_t& s )
{
    std::vector< std::string > out;
    for ( int i = 0; i < s.count; ++i )
        out.emplace_back( s.names.data( ) + 32 * i );
    const std::string first = out.empty( ) ? std::string( ) : out[ 0 ];
    out.emplace_back( "" );
    out.emplace_back( "NoSuchBone" );
    out.push_back( first + "x" );
    if ( first.size( ) > 1 )
        out.push_back( first.substr( 0, first.size( ) - 1 ) );
    return out;
}

inline std::uint32_t anim_fnv( const void* data, std::size_t bytes )
{
    const std::uint8_t* p = static_cast< const std::uint8_t* >( data );
    std::uint32_t h = 0x811C9DC5u;
    for ( std::size_t i = 0; i < bytes; ++i )
        h = ( h ^ p[ i ] ) * 0x01000193u;
    return h;
}

struct anim_cursor_t
{
    const unsigned* w = nullptr;
    std::size_t at = 0;
    std::size_t size = 0;
    int compared = 0;
    int failed = 0;
    const char* where = "";
    unsigned take( ) { return at < size ? w[ at++ ] : 0u; }
    void eq( std::uint32_t mine, std::uint32_t retail )
    {
        ++compared;
        if ( mine != retail )
        {
            if ( failed < 8 )
                LOG_ERROR( "%s: word %zu 0x%08X, retail 0x%08X", where, at, mine, retail );
            ++failed;
        }
    }
    void same( const char* what, int index, std::uint32_t mine )
    {
        const std::uint32_t r = take( );
        ++compared;
        if ( r != mine )
        {
            if ( failed < 8 )
                LOG_ERROR( "anim_model %s: %s[%d] 0x%08X, retail 0x%08X", where, what, index, mine, r );
            ++failed;
        }
    }
    void floats( const char* what, const float* f, int count )
    {
        for ( int k = 0; k < count; ++k )
        {
            std::uint32_t b;
            std::memcpy( &b, f + k, 4 );
            same( what, k, b );
        }
    }
    // A pose array, 12 words per bone; `skip_w`: the position / scale w lanes (UNKNOWN) are skipped from bone `from`.
    void poses( const char* what, const skel_xform_t* x, int count, bool skip_w, int from )
    {
        for ( int b = 0; b < count; ++b )
        {
            const float* f = &x[ b ].pos[ 0 ];
            for ( int k = 0; k < 12; ++k )
            {
                if ( skip_w && b >= from && ( k == 3 || k == 11 ) )
                {
                    take( );
                    continue;
                }
                std::uint32_t v;
                std::memcpy( &v, f + k, 4 );
                same( what, 12 * b + k, v );
            }
        }
    }
    void box( const char* what, const float* aabb, bool w_lanes )   // x y z lanes; the w lanes only when known
    {
        for ( int k = 0; k < 8; ++k )
        {
            if ( !w_lanes && ( k == 3 || k == 7 ) )
            {
                take( );
                continue;
            }
            std::uint32_t v;
            std::memcpy( &v, aabb + k, 4 );
            same( what, k, v );
        }
    }
};

inline float anim_float( unsigned w )
{
    float f;
    std::memcpy( &f, &w, 4 );
    return f;
}

// Case anim_model: the shipped animated models and clips through world/skeleton.hxx against retail's own code -
// VuSkeleton 0x52F140 / 0x52F4C0 / 0x57FE80, findNodeByName 0x52F3C0, VuAnimation 0x562250, VuAnimationControl
// 0x5161F0, VuAnimatedSkeleton 0x516750 / 0x5173D0 / 0x5169E0 / 0x516CF0 (0x5802D0, 0x580AC0), VuAnimatedModelInstance
// 0x532360 / 0x532720 (0x57FAF0) / 0x532990. The asset bytes are read from Base.apf here, as the case read them.
inline bool verify_anim_model( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "anim_model: Base.apf did not open" );
        return false;
    }
    anim_cursor_t g;
    g.w = g_golden_anm_words;
    g.size = g_golden_anm_size;
    const unsigned models = g.take( );
    int steps_total = 0, bones_total = 0, events_total = 0;
    for ( unsigned mi = 0; mi < models; ++mi )
    {
        const unsigned n = g.take( );
        const unsigned skel_off = g.take( ), reader_end = g.take( );
        const char* model = g_golden_anm_names[ g.take( ) ];
        g.where = model;
        std::vector< std::uint8_t > blob;
        skel_model_asset_t asset;
        if ( !archive.read( ( std::string( "VuAnimatedModelAsset/" ) + model ).c_str( ), blob )
            || !skel_model_asset_load( blob.data( ), blob.size( ), asset ) )
        {
            LOG_ERROR( "anim_model: %s did not load", model );
            return false;
        }
        const skel_skeleton_t& s = asset.skeleton;
        // The bone count, where our scene read ends (= where retail's skeleton read starts) and where the skeleton
        // read ends.
        g.compared += 3;
        if ( static_cast< unsigned >( s.count ) != n || asset.skeleton_offset != skel_off || asset.reader_end != reader_end )
        {
            LOG_ERROR( "anim_model %s: bones %d / offset %zu / end %zu, retail %u / %u / %u", model, s.count,
                asset.skeleton_offset, asset.reader_end, n, skel_off, reader_end );
            return false;
        }
        const int count = s.count;
        bones_total += count;
        for ( int k = 0; k < 8 * count; ++k )
        {
            std::uint32_t v;
            std::memcpy( &v, s.names.data( ) + 4 * k, 4 );
            g.same( "names", k, v );
        }
        for ( int k = 0; k < count; ++k )
            g.same( "parents", k, static_cast< std::uint32_t >( s.parents[ static_cast< std::size_t >( k ) ] ) );
        g.poses( "model pose", s.model.data( ), count, false, 0 );
        g.poses( "local pose", s.local.data( ), count, true, 1 );
        g.floats( "inverse", s.inverse.data( ), 16 * count );
        g.floats( "skeleton aabb", s.aabb, 8 );
        // findNodeByName
        const std::vector< std::string > probes = anim_find_names( s );
        const unsigned finds = g.take( );
        if ( finds != probes.size( ) )
        {
            LOG_ERROR( "anim_model %s: %u probes, ours %zu", model, finds, probes.size( ) );
            return false;
        }
        for ( std::size_t k = 0; k < probes.size( ); ++k )
            g.same( "findNodeByName", static_cast< int >( k ), static_cast< std::uint32_t >( skel_find_node( s, probes[ k ].c_str( ) ) ) );
        // The instances.
        float scene_aabb[ 8 ];
        for ( int k = 0; k < 8; ++k )
            scene_aabb[ k ] = anim_float( g.take( ) );
        skel_instance_t A, B;
        skel_instance_init( A );
        skel_instance_init( B );
        skel_instance_set_model( A, s, scene_aabb );
        skel_instance_set_model( B, s, scene_aabb );
        g.floats( "setModel pad", &A.pad, 1 );
        g.floats( "setModel aabb", A.aabb, 8 );
        g.floats( "setModel matrices", A.matrices.data( ), 16 * count );
        g.floats( "setModel palette 0", A.palette[ 0 ].data( ), 16 * count );
        g.floats( "setModel palette 1", A.palette[ 1 ].data( ), 16 * count );
        skel_instance_set_pose( A, nullptr );
        g.floats( "setPose(null) matrices", A.matrices.data( ), 16 * count );
        g.floats( "setPose(null) palette", A.palette[ 0 ].data( ), 16 * count );
        g.floats( "setPose(null) root", A.root, 16 );
        // The animated skeleton and its controls.
        skel_animated_t animated;
        skel_animated_init( animated, s );
        g.poses( "animated init local", animated.local.data( ), count, true, 0 );
        g.box( "animated init aabb", animated.aabb, false );
        const unsigned clips = g.take( );
        std::vector< skel_animation_t > anims( clips );
        std::vector< skel_control_t > controls( clips );
        std::vector< std::vector< timed_event_t > > events( clips );
        for ( unsigned k = 0; k < clips; ++k )
        {
            const char* clip = g_golden_anm_names[ g.take( ) ];
            std::vector< std::uint8_t > cb;
            std::size_t end = 0;
            if ( !archive.read( ( std::string( "VuAnimationAsset/" ) + clip ).c_str( ), cb )
                || !skel_animation_load( cb.data( ), cb.size( ), anims[ k ], &end ) )
            {
                LOG_ERROR( "anim_model: %s did not load", clip );
                return false;
            }
            const skel_animation_t& a = anims[ k ];
            g.same( "clip bones", static_cast< int >( k ), static_cast< std::uint32_t >( a.bones ) );
            g.same( "clip frames", static_cast< int >( k ), static_cast< std::uint32_t >( a.frames ) );
            g.floats( "clip end", &a.end, 1 );
            g.floats( "clip loop", &a.loop, 1 );
            g.same( "clip additive", static_cast< int >( k ), a.additive );
            g.same( "clip reader end", static_cast< int >( k ), static_cast< std::uint32_t >( end ) );
            g.floats( "clip aabb", a.aabb, 8 );
            g.same( "clip data", static_cast< int >( k ), anim_fnv( a.data.data( ), 48u * a.data.size( ) ) );
            skel_control_t& c = controls[ k ];
            c.animation = &anims[ k ];
            c.looping = g.take( ) != 0;
            c.factor = anim_float( g.take( ) );
            c.time = anim_float( g.take( ) );
            c.weight = anim_float( g.take( ) );
            const unsigned ne = g.take( );
            for ( unsigned j = 0; j < ne; ++j )
            {
                timed_event_t e{ };
                e.time = anim_float( g.take( ) );
                e.name = "E" + std::to_string( j );
                events[ k ].push_back( e );
            }
            c.events = ne ? &events[ k ] : nullptr;
            c.listener = true;
            skel_animated_add( animated, &c );
            events_total += static_cast< int >( ne );
        }
        const unsigned steps = g.take( );
        for ( unsigned st = 0; st < steps; ++st )
        {
            const float dt = anim_float( g.take( ) );
            std::vector< std::pair< int, int > > fired;
            skel_animated_advance( animated, dt, [ & ]( int k, const std::string& name, const value_t* )
                {
                    const int code = name == "AnimDone" ? 0 : name == "AnimLoop" ? 1 : 2 + std::atoi( name.c_str( ) + 1 );
                    fired.emplace_back( k, code );
                } );
            for ( unsigned k = 0; k < clips; ++k )
                g.floats( "control time", &controls[ k ].time, 1 );
            const unsigned nf = g.take( );
            if ( nf != fired.size( ) )
            {
                LOG_ERROR( "anim_model %s step %u: %zu events fired, retail %u", model, st, fired.size( ), nf );
                ++g.failed;
                return false;
            }
            for ( unsigned k = 0; k < nf; ++k )
            {
                g.same( "fired control", static_cast< int >( k ), static_cast< std::uint32_t >( fired[ k ].first ) );
                g.same( "fired name", static_cast< int >( k ), static_cast< std::uint32_t >( fired[ k ].second ) );
            }
            if ( !skel_animated_build( animated ) || !skel_instance_set_pose( A, &animated ) )
            {
                LOG_ERROR( "anim_model %s step %u: a path read UNKNOWN memory", model, st );
                return false;
            }
            skel_instance_copy( B, A );
            g.poses( "built local", animated.local.data( ), count, true, 0 );
            g.box( "built aabb", animated.aabb, false );
            g.same( "A current", 0, static_cast< std::uint32_t >( A.current ) );
            g.same( "B current", 0, static_cast< std::uint32_t >( B.current ) );
            g.floats( "A matrices", A.matrices.data( ), 16 * count );
            g.floats( "A palette", A.palette[ A.current ].data( ), 16 * count );
            g.box( "A aabb", A.aabb, false );
            g.floats( "A root", A.root, 16 );
            g.box( "B aabb", B.aabb, false );
            g.floats( "B root", B.root, 16 );
            g.same( "B palette", 0, anim_fnv( B.palette[ B.current ].data( ), 64u * count ) );
            g.same( "B matrices", 0, anim_fnv( B.matrices.data( ), 64u * count ) );
            A.current = A.current == 0 ? 1 : 0;   // the Build tick's flips
            B.current = B.current == 0 ? 1 : 0;
            ++steps_total;
        }
    }
    if ( g.failed == 0 && g.at == g.size )
    {
        LOG_SUCCESS( "anim_model matches retail's animated model CPU side (VuSkeleton 0x52F140 / 0x52F4C0 / 0x57FE80, findNodeByName "
                     "0x52F3C0, VuAnimation 0x562250, VuAnimatedSkeleton 0x516750 / 0x5173D0 / advance 0x5169E0 / build 0x516CF0 "
                     "(0x5802D0, 0x580AC0), VuAnimatedModelInstance 0x532360 / setPose 0x532720 (0x57FAF0) / copy 0x532990) on %u "
                     "shipped models (%d bones), %d steps, %d timed events: %d words exact",
            models, bones_total, steps_total, events_total, g.compared );
        return true;
    }
    LOG_ERROR( "anim_model FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// The collaborators of case gfx_sort: every call logged as the case logged it (cases/render.py GFX_LOG_*), 5 words each.
struct gfx_check_hooks_t : gfx_sort_hooks_t
{
    std::uint32_t device( int slot, std::uint32_t a, std::uint32_t b, std::uint32_t c ) override
    {
        record( 1, static_cast< std::uint32_t >( slot ), a, b, c );
        return slot == k_gfx_begin ? 1u : slot == k_gfx_slot_80 ? 0x12345u : 0u;
    }
    void program( std::uint32_t id, int slot, std::uint32_t handle, std::uint32_t value, const std::uint32_t* vector ) override
    {
        if ( vector )
            value = vector[ 0 ] + vector[ 1 ] + vector[ 2 ];
        record( 2, static_cast< std::uint32_t >( slot ) | ( id << 16 ), handle, value, 0 );
    }
    std::uint32_t thread( int slot, std::uint32_t a, std::uint32_t b ) override
    {
        if ( slot == 0x30 )
            return ++m_events;
        record( 3, static_cast< std::uint32_t >( slot ), a, b, 0 );
        return 0;
    }
    void profile( int slot ) override { record( 4, static_cast< std::uint32_t >( slot ), 0, 0, 0 ); }
    void scheduler( int slot, std::uint32_t a ) override { record( 8, static_cast< std::uint32_t >( slot ), a, 0, 0 ); }
    void composer( int slot, std::uint32_t a, std::uint32_t b ) override { record( 6, static_cast< std::uint32_t >( slot ), a, b, 0 ); }
    void water_flip( ) override { record( 7, 0, 0, 0, 0 ); }
    void light_flip( ) override { m_light ^= 1; }
    std::uint32_t texture_handle( std::uint32_t texture ) override
    {
        record( 8, 0x1Cu | ( texture << 16 ), 0, 0, 0 );
        return 0xA00u + texture;
    }
    void draw( std::uint32_t callback, std::uint8_t* record_at ) override
    {
        std::uint32_t tag;
        std::memcpy( &tag, record_at, 4 );
        record( 5, callback, tag, m_sort->m_drawn_lo, m_sort->m_drawn_hi );
    }
    const gfx_material_t* material( std::uint32_t id ) override { return &m_materials[ id - 1 ]; }
    const gfx_chunk_t* chunk( std::uint32_t id ) override { return &m_chunks[ id - 1 ]; }
    void record( std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t e )
    {
        m_log.insert( m_log.end( ), { a, b, c, d, e } );
    }
    std::vector< std::uint32_t > m_log;
    std::vector< gfx_material_t > m_materials;
    std::vector< gfx_chunk_t > m_chunks;
    const gfx_sort_t* m_sort = nullptr;
    std::uint32_t m_events = 0x7000u, m_light = 0;
};

// cases/render.py gfx_lcg.
inline std::vector< std::uint64_t > gfx_lcg_keys( int n, std::uint32_t s )
{
    std::vector< std::uint64_t > out;
    for ( int i = 0; i < n; ++i )
    {
        s = s * 1664525u + 1013904223u;
        const std::uint32_t lo = s & ( s & 0x100u ? 0xFFu : s & 0x200u ? 0xFFFFFFu : 0xFFFFFFFFu );
        s = s * 1664525u + 1013904223u;
        const std::uint32_t hi = s & ( s & 0x100u ? 0u : s & 0x200u ? 0xF000u : 0xFFFFFFFFu );
        out.push_back( lo | ( static_cast< std::uint64_t >( hi ) << 32 ) );
    }
    return out;
}

inline void gfx_compare_log( anim_cursor_t& g, gfx_check_hooks_t& h )
{
    const unsigned n = g.take( );
    g.eq( static_cast< unsigned >( h.m_log.size( ) / 5 ), n );
    for ( unsigned i = 0; i < 5 * n; ++i )
        g.eq( i < h.m_log.size( ) ? h.m_log[ i ] : 0xDEADBEEFu, g.take( ) );
    h.m_log.clear( );
}

// Case gfx_sort: the fake frames of cases/render.py through world/gfxsort.hxx: keys, commands, the swap, the radix sort, and
// the whole sequence of device / program / thread / callback calls against retail's own walk.
inline bool verify_gfx_sort( )
{
    anim_cursor_t g;
    g.w = g_golden_gfs_words;
    g.size = g_golden_gfs_size;
    g.where = "gfx_sort";
    gfx_check_hooks_t h;
    h.m_far_plane = 1234.5f;
    h.m_fog_start = 0.25f;
    h.m_fog_end = 900.0f;
    gfx_sort_t sort( h );
    h.m_sort = &sort;
    g.eq( sort.m_event[ 0 ], g.take( ) );
    g.eq( sort.m_event[ 1 ], g.take( ) );
    g.eq( static_cast< unsigned >( sort.m_index ), g.take( ) );
    g.eq( static_cast< unsigned >( sort.m_render ), g.take( ) );
    const unsigned caps_0 = g.take( ), caps_1 = g.take( );
    g.eq( static_cast< unsigned >( sort.m_buffers[ 0 ].m_capacity ), g.take( ) );
    g.eq( static_cast< unsigned >( sort.m_buffers[ 1 ].m_capacity ), g.take( ) );
    sort.reserve( );
    g.eq( static_cast< unsigned >( sort.m_buffers[ 0 ].m_capacity ), caps_0 );
    g.eq( static_cast< unsigned >( sort.m_buffers[ 1 ].m_capacity ), caps_1 );
    for ( const gfx_layer_t& layer : k_gfx_layers )
    {
        const unsigned w0 = g.take( );
        g.eq( static_cast< unsigned >( layer.blend ) | ( static_cast< unsigned >( layer.depth ) << 8 ) | ( static_cast< unsigned >( layer.write ) << 16 ), w0 );
        g.eq( layer.src, g.take( ) );
        g.eq( layer.dst, g.take( ) );
    }
    const unsigned material_count = g.take( );
    for ( unsigned i = 0; i < material_count; ++i )
    {
        g.take( );
        gfx_material_t m;
        m.m_key = g.take( );
        m.m_constants_id = g.take( );
        m.m_textures_id = g.take( );
        m.m_program = g.take( ) + 1;
        m.m_declaration = g.take( );
        std::uint32_t* binds[ 13 ] = { &m.m_view_matrix, &m.m_view_proj_matrix, &m.m_eye_world, &m.m_far_plane, &m.m_time, &m.m_amb_light_color,
                                       &m.m_dir_light_world, &m.m_dir_light_front_color, &m.m_dir_light_back_color, &m.m_dir_light_specular_color,
                                       &m.m_fog_start, &m.m_fog_inv_range, &m.m_fog_color };
        for ( std::uint32_t* b : binds )
            *b = g.take( );
        m.m_clip_plane = g.take( );
        m.m_composer = g.take( );
        const unsigned constants = g.take( );
        for ( unsigned c = 0; c < constants; ++c )
        {
            gfx_constant_t k;
            k.m_handle = g.take( );
            k.m_kind = g.take( );
            for ( int v = 0; v < 3; ++v )
                k.m_value[ v ] = g.take( );
            m.m_constants.push_back( k );
        }
        const unsigned textures = g.take( );
        for ( unsigned t = 0; t < textures; ++t )
        {
            gfx_material_t::texture_t x;
            x.m_sampler = g.take( );
            x.m_texture = g.take( );
            m.m_textures.push_back( x );
        }
        h.m_materials.push_back( m );
    }
    for ( int i = 0; i < 4; ++i )
    {
        gfx_chunk_t c;
        c.m_stream = g.take( );
        c.m_indices = g.take( );
        c.m_stride = g.take( );
        c.m_key = g.take( );
        h.m_chunks.push_back( c );
    }
    const unsigned frames = g.take( );
    int commands_total = 0, calls_total = 0;
    for ( unsigned fi = 0; fi < frames; ++fi )
    {
        sort.m_single_thread = g.take( ) != 0;
        sort.m_thread = g.take( ) ? 0x99u : 0u;
        const bool full = g.take( ) != 0;
        h.m_has_profile = h.m_has_composer = h.m_has_water = h.m_has_light = full;
        const unsigned ops = g.take( );
        for ( unsigned o = 0; o < ops; ++o )
        {
            const unsigned layer = g.take( ), screen = g.take( ), size = g.take( ), tag = g.take( ), type = g.take( ), material = g.take( );
            const unsigned chunk = g.take( ), callback = g.take( ), depth = g.take( ), nulled = g.take( );
            sort.set_layer( layer );
            sort.set_screen( screen );
            std::uint8_t* at = sort.allocate( static_cast< std::int32_t >( size ) );
            std::memcpy( at, &tag, 4 );
            float d;
            std::memcpy( &d, &depth, 4 );
            sort.submit( type, material + 1, chunk == 0xFFFFFFFFu ? 0u : chunk + 1, callback, d );
            if ( nulled )
                sort.m_commands[ sort.m_index ].back( ).m_words[ nulled == 1 ? 4 : 5 ] = 0;
        }
        const unsigned big_cap = g.take( ), big_used = g.take( ), big_offset = g.take( );
        if ( big_cap )
        {
            sort.allocate( 0xA0000 );
            g.eq( static_cast< unsigned >( sort.m_buffers[ sort.m_index ].m_capacity ), big_cap );
            g.eq( static_cast< unsigned >( sort.m_buffers[ sort.m_index ].m_used ), big_used );
            g.eq( static_cast< unsigned >( sort.m_offset ), big_offset );
        }
        const unsigned dt = g.take( );
        float dtf;
        std::memcpy( &dtf, &dt, 4 );
        sort.tick( dtf );
        for ( const ui_gfx_command_t& c : sort.m_commands[ sort.m_index ] )
        {
            g.eq( c.m_words[ 0 ], g.take( ) );
            g.eq( c.m_words[ 1 ], g.take( ) );
            g.eq( c.m_words[ 2 ], g.take( ) );
            g.eq( c.m_words[ 3 ], g.take( ) );
            g.eq( c.m_words[ 4 ] - 1, g.take( ) );
            g.eq( c.m_words[ 5 ] - 1, g.take( ) );
            g.eq( c.m_words[ 6 ] & 0xFFFFu, g.take( ) );
            ++commands_total;
        }
        sort.flip( );
        calls_total += static_cast< int >( h.m_log.size( ) / 5 );
        gfx_compare_log( g, h );
        unsigned time_bits;
        std::memcpy( &time_bits, &sort.m_time, 4 );
        g.eq( static_cast< unsigned >( sort.m_index ), g.take( ) );
        g.eq( static_cast< unsigned >( sort.m_render ), g.take( ) );
        g.eq( static_cast< unsigned >( sort.m_offset ), g.take( ) );
        g.eq( sort.m_pending ? 1u : 0u, g.take( ) );
        g.eq( static_cast< unsigned >( sort.m_buffers[ sort.m_index ].m_used ), g.take( ) );
        g.eq( static_cast< unsigned >( sort.m_commands[ sort.m_index ].size( ) ), g.take( ) );
        g.eq( time_bits, g.take( ) );
        g.eq( h.m_light, g.take( ) );
        g.eq( h.m_light, g.take( ) );
        for ( const std::uint32_t v : sort.m_stats )
            g.eq( v, g.take( ) );
        for ( const std::uint32_t v : sort.m_stats_last )
            g.eq( v, g.take( ) );
        if ( g.take( ) )
        {
            const unsigned kind = g.take( );
            sort.draw_frame( );
            calls_total += static_cast< int >( h.m_log.size( ) / 5 );
            gfx_compare_log( g, h );
            if ( kind )
                sort.wait( );
            else
                sort.stop_thread( );
            gfx_compare_log( g, h );
            g.eq( sort.m_pending ? 1u : 0u, g.take( ) );
            g.eq( sort.m_thread_stopping ? 1u : 0u, g.take( ) );
            sort.m_thread_stopping = false;
            for ( const std::uint32_t v : sort.m_stats )
                g.eq( v, g.take( ) );
            for ( const std::uint32_t v : sort.m_stats_last )
                g.eq( v, g.take( ) );
        }
        const unsigned order = g.take( );
        g.eq( static_cast< unsigned >( sort.m_order.size( ) ), order );
        for ( unsigned i = 0; i < order; ++i )
            g.eq( i < sort.m_order.size( ) ? sort.m_order[ i ] : 0xDEADBEEFu, g.take( ) );
    }
    const unsigned radix = g.take( );
    for ( unsigned r = 0; r < radix; ++r )
    {
        const int n = static_cast< int >( g.take( ) );
        std::vector< std::uint64_t > keys = gfx_lcg_keys( n, 0x5A9660u + static_cast< unsigned >( n ) );
        std::vector< std::uint32_t > order( static_cast< std::size_t >( n ) );
        for ( int i = 0; i < n; ++i )
            order[ i ] = static_cast< std::uint32_t >( i );
        gfx_radix_sort( keys.data( ), n, order.data( ) );
        g.eq( anim_fnv( keys.data( ), keys.size( ) * 8 ), g.take( ) );
        g.eq( anim_fnv( order.data( ), order.size( ) * 4 ), g.take( ) );
    }
    if ( g.failed == 0 && g.at == g.size )
    {
        LOG_SUCCESS( "gfx_sort matches retail's VuGfxSort CPU side (ctor 0x539A60, reserve 0x539E90, setters 0x423900 / 0x423920, 0x426B80, "
                     "0x429010, tick 0x53ACA0, flip 0x53A2C0, waits 0x53A400 / 0x53A250, draw 0x53ACC0, sort 0x53AD10, radix 0x5A9660, walk "
                     "0x53ADC0, 0x507240, 0x53AF80, 0x53B070, 0x53B0F0, 0x541C40, 0x541CB0, table 0x735A00) on %u frames, %d commands, %d "
                     "hook calls: %d words exact", frames, commands_total, calls_total, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_sort FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_cache's collaborators: the programs answer from the name's FNV-1a, a name ending in X is absent.
struct gfx_cache_hooks_t : gfx_sort_hooks_t
{
    std::uint32_t device( int slot, std::uint32_t, std::uint32_t, std::uint32_t ) override { record( 9, static_cast< std::uint32_t >( slot ) ); return 0; }
    void program( std::uint32_t, int, std::uint32_t, std::uint32_t, const std::uint32_t* ) override { }
    std::uint32_t thread( int slot, std::uint32_t a, std::uint32_t ) override
    {
        if ( slot == 0x30 )
            return ++m_events;
        record( 8, a );
        return 0;
    }
    void profile( int ) override { }
    void composer( int, std::uint32_t, std::uint32_t ) override { }
    void water_flip( ) override { }
    void light_flip( ) override { }
    std::uint32_t texture_handle( std::uint32_t ) override { return 0; }
    void draw( std::uint32_t, std::uint8_t* ) override { }
    const gfx_material_t* material( std::uint32_t ) override { return nullptr; }
    const gfx_chunk_t* chunk( std::uint32_t ) override { return nullptr; }
    std::uint32_t program_constant( std::uint32_t, const std::string& name ) override
    {
        const std::uint32_t h = gfx_fnv_text( 0x811C9DC5u, name );
        record( 1, h );
        return name.back( ) == 'X' ? 0u : ( h & 0xFFu ) | 1u;
    }
    std::int32_t program_sampler( std::uint32_t, const std::string& name ) override
    {
        const std::uint32_t h = gfx_fnv_text( 0x811C9DC5u, name );
        record( 2, h );
        return name.back( ) == 'X' ? -1 : static_cast< std::int32_t >( h & 7u );
    }
    std::uint32_t asset_get( std::uint32_t kind, const std::string& name ) override
    {
        const std::uint32_t h = gfx_fnv_text( 0x811C9DC5u, name );
        record( 4, gfx_fnv_text( 0x811C9DC5u, kind ? "VuCubeTextureAsset" : "VuTextureAsset" ), h );
        return 0x5000u + ( h & 0xFFu );
    }
    void asset_put( std::uint32_t asset ) override { record( 5, asset ); }
    void retain( std::uint32_t object ) override { ++m_refs[ object ]; }
    void release( std::uint32_t object ) override
    {
        if ( --m_refs[ object ] == 0 )
            record( 3, object );
    }
    std::uint32_t composer_add( const gfx_material_t& ) override { record( 6, 0 ); return 0x4444u; }
    void composer_remove( std::uint32_t handle ) override { record( 7, handle ); }
    void record( std::uint32_t tag, std::uint32_t a, std::uint32_t b = 0 ) { m_log.insert( m_log.end( ), { tag, a, b } ); }
    std::vector< std::uint32_t > m_log;
    std::uint32_t m_refs[ 8 ] = { };
    std::uint32_t m_events = 0x7000u;
};

template < typename T >
inline std::uint32_t gfx_cache_position( const std::vector< std::unique_ptr< T > >& list, const T* e )
{
    for ( std::size_t i = 0; i < list.size( ); ++i )
        if ( list[ i ].get( ) == e )
            return static_cast< std::uint32_t >( i );
    return 0xFFFFFFFFu;
}

// Case gfx_cache: seeded creates and releases through world/gfxsort.hxx's material and chunk caches against retail's 0x53A440 /
// 0x53A710 / 0x53A7C0 / 0x53A920: every array, key, hash, handle, reference and collaborator call after every step.
inline bool verify_gfx_cache( )
{
    anim_cursor_t g;
    g.w = g_golden_gfc_words;
    g.size = g_golden_gfc_size;
    g.where = "gfx_cache";
    std::vector< gfx_material_desc_t > descs( g.take( ) );
    for ( gfx_material_desc_t& d : descs )
    {
        d.m_constants.resize( g.take( ) );
        for ( gfx_material_desc_t::constant_t& c : d.m_constants )
        {
            c.m_name = g_golden_gfc_names[ g.take( ) ];
            c.m_kind = g.take( );
            for ( std::uint32_t& v : c.m_value )
                v = g.take( );
        }
        d.m_textures.resize( g.take( ) );
        for ( gfx_material_desc_t::texture_t& t : d.m_textures )
        {
            t.m_name = g_golden_gfc_names[ g.take( ) ];
            t.m_slot = g_golden_gfc_names[ g.take( ) ];
            t.m_kind = g.take( );
            t.m_asset = g_golden_gfc_names[ g.take( ) ];
        }
    }
    gfx_cache_hooks_t h;
    gfx_sort_t sort( h );
    const unsigned steps = g.take( );
    int hits = 0, kept = 0;
    for ( unsigned step = 0; step < steps; ++step )
    {
        h.m_has_composer = g.take( ) != 0;
        const unsigned op = g.take( ), a = g.take( ), b = g.take( ), c = g.take( ), d = g.take( );
        if ( op == 0 )
        {
            const std::uint32_t before = sort.m_material_count;
            gfx_material_t* m = sort.material_create( a, b, descs[ c ] );
            hits += sort.m_material_count == before;
            g.eq( gfx_cache_position( sort.m_materials, m ), d );   // the key goes stale after a release, the position does not
        }
        else if ( op == 1 )
        {
            gfx_chunk_t* e = sort.chunk_create( a, b, c );
            g.eq( gfx_cache_position( sort.m_chunks, e ), d );
        }
        else if ( op == 2 )
        {
            sort.m_pending = a != 0;
            sort.material_release( sort.m_materials[ b ].get( ) );
        }
        else
        {
            sort.m_pending = a != 0;
            sort.chunk_release( sort.m_chunks[ b ].get( ) );
        }
        const unsigned logged = g.take( );
        g.eq( static_cast< unsigned >( h.m_log.size( ) / 3 ), logged );
        for ( unsigned i = 0; i < 3 * logged; ++i )
            g.eq( i < h.m_log.size( ) ? h.m_log[ i ] : 0xDEADBEEFu, g.take( ) );
        h.m_log.clear( );
        g.eq( sort.m_material_count, g.take( ) );
        g.eq( sort.m_chunk_count, g.take( ) );
        for ( const std::uint32_t r : h.m_refs )
            g.eq( r, g.take( ) );
        g.eq( sort.m_pending ? 1u : 0u, g.take( ) );
        const unsigned materials = g.take( );
        g.eq( static_cast< unsigned >( sort.m_materials.size( ) ), materials );
        for ( unsigned i = 0; i < materials; ++i )
        {
            const gfx_material_t& m = *sort.m_materials[ i ];
            const std::size_t length = g.take( );
            const std::size_t end = g.at + length;
            g.eq( m.m_program, g.take( ) );
            g.eq( m.m_declaration, g.take( ) );
            g.eq( m.m_key, g.take( ) );
            g.eq( m.m_constants_id, g.take( ) );
            g.eq( m.m_textures_id, g.take( ) );
            g.eq( m.m_composer, g.take( ) );
            g.eq( m.m_refs, g.take( ) );
            for ( const gfx_frame_constant_t& f : k_gfx_frame_constants )
                g.eq( m.*f.m_field, g.take( ) );
            g.eq( static_cast< unsigned >( m.m_constants.size( ) ), g.take( ) );
            g.eq( static_cast< unsigned >( m.m_textures.size( ) ), g.take( ) );
            for ( const gfx_constant_t& k : m.m_constants )
            {
                g.eq( gfx_fnv_text( 0x811C9DC5u, k.m_name ), g.take( ) );
                g.eq( k.m_handle, g.take( ) );
                g.eq( k.m_kind, g.take( ) );
                for ( const std::uint32_t v : k.m_value )
                    g.eq( v, g.take( ) );
            }
            for ( const gfx_material_t::texture_t& t : m.m_textures )
            {
                g.eq( gfx_fnv_text( 0x811C9DC5u, t.m_name ), g.take( ) );
                g.eq( t.m_sampler, g.take( ) );
                g.eq( t.m_kind, g.take( ) );
                const unsigned asset = g.take( );
                if ( t.m_kind < 2 )   // a kind past 1 leaves retail's word as the heap left it
                    g.eq( t.m_texture, asset );
            }
            g.at = end;
            ++kept;
        }
        const unsigned chunks = g.take( );
        g.eq( static_cast< unsigned >( sort.m_chunks.size( ) ), chunks );
        for ( unsigned i = 0; i < chunks; ++i )
        {
            const gfx_chunk_t& e = *sort.m_chunks[ i ];
            g.eq( e.m_stream, g.take( ) );
            g.eq( e.m_indices, g.take( ) );
            g.eq( e.m_stride, g.take( ) );
            g.eq( e.m_key, g.take( ) );
            g.eq( e.m_refs, g.take( ) );
        }
    }
    if ( g.failed == 0 && g.at == g.size )
    {
        LOG_SUCCESS( "gfx_cache matches retail's material and chunk caches (0x53A440, 0x541390, 0x5415D0, 0x53A610, 0x541200, 0x541680, 0x53A710, 0x541BE0, "
                     "0x4AB7D0, 0x53A7C0, 0x53A920) on %u steps (%d material hits, %d materials compared): %d words exact", steps, hits, kept, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_cache FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

inline void gfx_dbrt_dump( const dbrt_node_t* n, const dbrt_node_t* const* leaves, std::vector< std::uint32_t >& out )
{
    if ( !n )
        return;
    const auto push_box = [ & ]( const float* f )
    {
        for ( int i = 0; i < 3; ++i )
        {
            std::uint32_t b;
            std::memcpy( &b, f + i, 4 );
            out.push_back( b );
        }
    };
    out.push_back( n->leaf( ) ? 0u : 1u );
    push_box( n->m_min );
    push_box( n->m_max );
    if ( n->leaf( ) )
        out.push_back( static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( n->m_data ) ) );
    else
    {
        gfx_dbrt_dump( n->m_child[ 0 ], leaves, out );
        gfx_dbrt_dump( n->m_child[ 1 ], leaves, out );
    }
}

// Case gfx_dbrt: seeded adds, removes and moves through world/dbrt.hxx against retail's Vu3dDrawManager 0x5A78B0 / 0x5A78F0 / 0x5A7930
// (tree 0x5C2860 / 0x5C28E0 / 0x5C2A30 / 0x5C2D40): the whole tree after each; the walks 0x5A7B30 / 0x5A7DB0 in visiting order; 0x562510 alone.
// The object table: the case's slot +0x10 is empty for i % 11 == 10, +0x14 for i % 13 == 12, as there.
inline bool verify_gfx_dbrt( )
{
    anim_cursor_t g;
    g.w = g_golden_gfd_words;
    g.size = g_golden_gfd_size;
    g.where = "gfx_dbrt";
    dbrt_tree_t tree;
    dbrt_node_t* leaves[ 48 ] = { };
    unsigned masks[ 48 ] = { };
    const unsigned steps = g.take( );
    int walks = 0, visits = 0;
    for ( unsigned s = 0; s < steps; ++s )
    {
        const unsigned op = g.take( ), id = g.take( ), mask = g.take( );
        float aabb[ 8 ];
        for ( float& f : aabb )
        {
            const unsigned w = g.take( );
            std::memcpy( &f, &w, 4 );
        }
        if ( op == 0 )
        {
            leaves[ id ] = tree.add( aabb, reinterpret_cast< void* >( static_cast< std::uintptr_t >( id ) ) );
            masks[ id ] = mask;
        }
        else if ( op == 1 )
        {
            tree.erase( leaves[ id ] );
            leaves[ id ] = nullptr;
        }
        else
            tree.move( leaves[ id ], aabb );
        std::vector< std::uint32_t > mine;
        mine.push_back( static_cast< std::uint32_t >( tree.m_count ) );
        gfx_dbrt_dump( tree.m_root, leaves, mine );
        const unsigned size = g.take( );
        g.eq( static_cast< unsigned >( mine.size( ) ), size );
        for ( unsigned i = 0; i < size; ++i )
            g.eq( i < mine.size( ) ? mine[ i ] : 0xDEADBEEFu, g.take( ) );
        if ( !g.take( ) )
            continue;
        const unsigned want = g.take( ), count = g.take( );
        std::vector< float > planes( count * 4 );
        for ( float& f : planes )
        {
            const unsigned w = g.take( );
            std::memcpy( &f, &w, 4 );
        }
        for ( int pass = 0; pass < 2; ++pass )
        {
            std::vector< std::uint32_t > visited;
            unsigned reached = 0;
            dbrt_walk( tree.m_root, planes.data( ), static_cast< int >( count ), [ & ]( void* data )
            {
                const unsigned i = static_cast< unsigned >( reinterpret_cast< std::uintptr_t >( data ) );
                const bool empty = pass == 0 ? i % 11 == 10 : i % 13 == 12;
                ++reached;                     // retail's counter takes every leaf the walk reaches
                if ( ( masks[ i ] & want ) == want && !empty )
                    visited.push_back( i );
            } );
            g.eq( reached, g.take( ) );
            const unsigned n = g.take( );
            g.eq( static_cast< unsigned >( visited.size( ) ), n );
            for ( unsigned i = 0; i < n; ++i )
                g.eq( i < visited.size( ) ? visited[ i ] : 0xDEADBEEFu, g.take( ) );
            visits += static_cast< int >( n );
        }
        ++walks;
    }
    const unsigned tests = g.take( );
    for ( unsigned t = 0; t < tests; ++t )
    {
        float f[ 10 ];
        for ( float& v : f )
        {
            const unsigned w = g.take( );
            std::memcpy( &v, &w, 4 );
        }
        const int sign = static_cast< int >( g.take( ) );
        g.eq( static_cast< unsigned >( dbrt_plane_box( f, f + 3, f + 6, sign ) ), g.take( ) );
    }
    if ( g.failed == 0 && g.at == g.size )
    {
        LOG_SUCCESS( "gfx_dbrt matches retail's render tree (0x5C2860, 0x5C28E0, 0x5C2A30, 0x5C2D40, 0x5A78B0, 0x5A78F0, 0x5A7930), walks 0x5A7B30 / 0x5A8010 / "
                     "0x5A7A90 / 0x5A7DB0 / 0x5A8040 and plane test 0x562510 on %u steps, %d walks (%d leaf visits), %u plane tests: %d words exact",
                     steps, walks, visits, tests, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_dbrt FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_lights: the dynamic lights through world/lights.hxx against retail's 0x5C2F30 / 0x5C30D0, 0x5EDD90 / 0x5EDDF0, submit 0x562B10 (0x5630F0, 0x563490,
// 0x563400) and gather 0x562C40: the record, the aabb, every list and every shader block word by word.
inline bool verify_gfx_lights( )
{
    anim_cursor_t g;
    g.w = g_golden_gfl_words;
    g.size = g_golden_gfl_size;
    g.where = "gfx_lights";
    const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    gfx_light_manager_t manager;
    g.eq( manager.m_fill, g.take( ) );
    g.eq( manager.m_render, g.take( ) );
    g.eq( manager.m_ambient, g.take( ) );
    g.eq( bits( manager.m_ambient_b ), g.take( ) );
    g.eq( bits( manager.m_ambient_c ), g.take( ) );
    g.eq( bits( manager.m_d8 ), g.take( ) );
    for ( const float v : manager.m_dir_light )
        g.eq( bits( v ), g.take( ) );
    g.eq( manager.m_front, g.take( ) );
    g.eq( manager.m_back, g.take( ) );
    g.eq( manager.m_specular, g.take( ) );
    g.eq( 0u, g.take( ) );
    static gfx_light_t lights[ 24 ];
    for ( gfx_light_t& l : lights )
        manager.update( l );
    for ( const float v : lights[ 0 ].m_record.m_words )
        g.eq( bits( v ), g.take( ) );
    const unsigned frames = g.take( );
    int listed = 0, taken = 0;
    for ( unsigned fi = 0; fi < frames; ++fi )
    {
        const unsigned ops = g.take( );
        for ( unsigned o = 0; o < ops; ++o )
        {
            const unsigned kind = g.take( ), i = g.take( );
            std::uint32_t v[ 15 ];
            for ( std::uint32_t& w : v )
                w = g.take( );
            gfx_light_t& l = lights[ i ];
            if ( kind == 1 )
            {
                manager.light_off( l );
                continue;
            }
            for ( int k = 0; k < 3; ++k )
            {
                l.m_position[ k ] = real( v[ k ] );
                l.m_direction[ k ] = real( v[ 3 + k ] );
            }
            l.m_diffuse = v[ 6 ];
            l.m_specular = v[ 7 ];
            l.m_factor = real( v[ 8 ] );
            l.m_range_a = real( v[ 9 ] );
            l.m_range_b = real( v[ 10 ] );
            l.m_cone = real( v[ 11 ] );
            l.m_penumbra = real( v[ 12 ] );
            l.m_draw_distance = real( v[ 13 ] );
            l.m_reflecting = static_cast< std::uint8_t >( v[ 14 ] );
            manager.update( l );
            manager.light_on( l );
            for ( const float r : l.m_record.m_words )
                g.eq( bits( r ), g.take( ) );
            for ( int k = 0; k < 3; ++k )
                g.eq( bits( l.m_aabb[ k ] ), g.take( ) );
            for ( int k = 0; k < 3; ++k )
                g.eq( bits( l.m_aabb[ 4 + k ] ), g.take( ) );
        }
        manager.m_fill = g.take( );
        manager.m_render = g.take( );
        const unsigned key = g.take( );
        const std::uint8_t reflecting = static_cast< std::uint8_t >( g.take( ) );
        float eye[ 3 ], planes[ 28 ];
        for ( float& f : eye )
            f = real( g.take( ) );
        for ( float& f : planes )
            f = real( g.take( ) );
        const int index = static_cast< int >( g.take( ) );
        const unsigned count = g.take( );
        manager.submit( eye, planes, reflecting, key );
        g.eq( static_cast< unsigned >( gfx_light_manager_t::list_index( manager.m_fill, key ) ), static_cast< unsigned >( index ) );
        const std::vector< gfx_light_record_t >& list = manager.m_lists[ index ];
        g.eq( static_cast< unsigned >( list.size( ) ), count );
        for ( unsigned i = 0; i < 20 * count; ++i )
            g.eq( i / 20 < list.size( ) ? bits( list[ i / 20 ].m_words[ i % 20 ] ) : 0xDEADBEEFu, g.take( ) );
        listed += static_cast< int >( count );
        manager.m_render = manager.m_fill;
        for ( int t = 0; t < 4; ++t )
        {
            float box[ 8 ], matrix[ 16 ];
            for ( float& f : box )
                f = real( g.take( ) );
            for ( float& f : matrix )
                f = real( g.take( ) );
            std::uint32_t pattern[ 80 ];
            for ( std::uint32_t& w : pattern )
                w = 0xAAAAAAAAu;
            float block[ 80 ];
            std::memcpy( block, pattern, sizeof( block ) );
            const int n = manager.gather( matrix, box, key, block );
            g.eq( static_cast< unsigned >( n ), g.take( ) );
            for ( const float f : block )
                g.eq( bits( f ), g.take( ) );
            taken += n;
        }
    }
    if ( g.failed == 0 && g.at == g.size )
    {
        LOG_SUCCESS( "gfx_lights matches retail's VuLightManager CPU side (ctor 0x562890, light 0x5C2F30 / 0x5C30D0, 0x5EDD90, 0x5EDDF0, submit 0x562B10, walk 0x5630F0 / "
                     "0x563490 / 0x563400, gather 0x562C40) on %u frames, %d listed lights, %d gathered: %d words exact", frames, listed, taken, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_lights FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_draw_manager: every call of the manager's collaborators, logged as the case logged it (kind, object, params flag).
struct gfx_dm_check_t : gfx_draw_hooks_t
{
    using log_t = std::vector< std::array< unsigned, 3 > >;
    struct method_t : gfx_draw_method_t
    {
        void call( const void* params ) override { m_log->push_back( { m_kind, m_id, static_cast< unsigned >( params == m_want ) } ); }
        log_t* m_log = nullptr;
        unsigned m_kind = 0, m_id = 0;
        const void* m_want = nullptr;
    };
    struct entry_t : gfx_draw_entry_t
    {
        void call( ) override { m_log->push_back( { 11u, m_id, 0u } ); }
        log_t* m_log = nullptr;
        unsigned m_id = 0;
    };
    void lights_submit( const gfx_draw_params_t& p ) override { m_log.push_back( { 7u, 0u, static_cast< unsigned >( &p == m_want ) } ); }
    bool composer_present( ) override { return m_composer; }
    void composer_draw( const gfx_draw_params_t& p ) override { m_log.push_back( { 8u, 0u, static_cast< unsigned >( &p == m_want ) } ); }
    void register_tick( ) override { m_calls.push_back( 3u ); }
    void register_flush( ) override { m_calls.push_back( 5u ); }
    void unregister_tick( ) override { m_calls.push_back( 4u ); }
    void unregister_flush( ) override { m_calls.push_back( 6u ); }
    log_t m_log;
    std::vector< unsigned > m_calls;
    bool m_composer = false;
    const void* m_want = nullptr;
};

// Case gfx_draw_manager: world/drawer.hxx against retail's creator 0x5A7320, ctor 0x5A73B0, add / remove / update, draw 0x5A75E0 (walk + leaf 0x5A7A90), shadow 0x5A7730 (leaf 0x5A7AF0),
// list remove 0x5A7800 with its growth, flush 0x5A7980, tick 0x5A79F0, dtor 0x5A7540, release 0x5A7310.
inline bool verify_gfx_draw_manager( )
{
    anim_cursor_t g;
    g.w = g_golden_gdm_words;
    g.size = g_golden_gdm_size;
    g.where = "gfx_draw_manager";
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    gfx_dm_check_t hooks;
    g_gfx_draw_manager = nullptr;
    gfx_draw_manager_t* manager = gfx_draw_manager_create( hooks );
    g.eq( 1u, g.take( ) );                     // the dev page: not recreated, the golden's first call
    g.eq( hooks.m_calls.size( ) > 0 ? hooks.m_calls[ 0 ] : 0u, g.take( ) );
    g.eq( hooks.m_calls.size( ) > 1 ? hooks.m_calls[ 1 ] : 0u, g.take( ) );
    g.eq( 0x5A79F0u, g.take( ) );
    g.eq( 0x5A7980u, g.take( ) );
    g.eq( static_cast< unsigned >( manager->m_capacity ), g.take( ) );
    g.eq( 0u, g.take( ) );
    static gfx_drawable_t objects[ 40 ];
    static gfx_dm_check_t::method_t draws[ 40 ], shadows[ 40 ];
    static gfx_dm_check_t::entry_t entries[ 32 ];
    gfx_draw_params_t params;
    gfx_shadow_params_t shadow;
    gfx_check_hooks_t sort_hooks;
    gfx_sort_t sort( sort_hooks );
    for ( unsigned i = 0; i < 40; ++i )
    {
        objects[ i ] = gfx_drawable_t( );
        draws[ i ].m_log = &hooks.m_log;
        draws[ i ].m_kind = 9;
        draws[ i ].m_id = i;
        draws[ i ].m_want = &params;
        shadows[ i ].m_log = &hooks.m_log;
        shadows[ i ].m_kind = 10;
        shadows[ i ].m_id = i;
        shadows[ i ].m_want = &shadow;
        objects[ i ].m_draw = i % 11 == 10 ? nullptr : &draws[ i ];
        objects[ i ].m_shadow = i % 13 == 12 ? nullptr : &shadows[ i ];
    }
    for ( unsigned i = 0; i < 32; ++i )
    {
        entries[ i ].m_log = &hooks.m_log;
        entries[ i ].m_id = i;
    }
    hooks.m_want = &params;
    const auto compare_log = [ & ]( )
    {
        const unsigned n = g.take( );
        g.eq( static_cast< unsigned >( hooks.m_log.size( ) ), n );
        for ( unsigned i = 0; i < n; ++i )
            for ( unsigned k = 0; k < 3; ++k )
                g.eq( i < hooks.m_log.size( ) ? hooks.m_log[ i ][ k ] : 0xDEADBEEFu, g.take( ) );
        hooks.m_log.clear( );
    };
    const unsigned steps = g.take( );
    int calls = 0;
    for ( unsigned s = 0; s < steps; ++s )
    {
        const unsigned op = g.take( );
        if ( op == 0 || op == 2 )
        {
            gfx_drawable_t& d = objects[ g.take( ) ];
            if ( op == 0 )
                d.m_mask = static_cast< std::uint8_t >( g.take( ) );
            for ( float& f : d.m_aabb )
                f = real( g.take( ) );
            if ( op == 0 )
                manager->add( d );
            else
                manager->update( d );
        }
        else if ( op == 1 )
            manager->remove( objects[ g.take( ) ] );
        else if ( op == 3 )
        {
            params.m_reflecting = static_cast< std::uint8_t >( g.take( ) );
            hooks.m_composer = g.take( ) != 0;
            gfx_camera_t camera;
            for ( float& f : camera.m_planes )
                f = real( g.take( ) );
            for ( float& f : params.m_clip )
                f = real( g.take( ) );
            params.m_camera = &camera;
            manager->draw( params );
            calls += static_cast< int >( hooks.m_log.size( ) );
            compare_log( );
        }
        else if ( op == 4 )
        {
            gfx_shadow_volume_t volume;
            volume.m_count = static_cast< int >( g.take( ) );
            shadow.m_clip_flag = static_cast< std::uint8_t >( g.take( ) );
            for ( int i = 0; i < 4 * volume.m_count; ++i )
                volume.m_planes[ i ] = real( g.take( ) );
            for ( float& f : shadow.m_clip )
                f = real( g.take( ) );
            shadow.m_volume = &volume;
            manager->draw_shadow( shadow );
            calls += static_cast< int >( hooks.m_log.size( ) );
            compare_log( );
        }
        else
        {
            const unsigned n = g.take( ), flush = g.take( ), gone = g.take( );
            sort.m_key_lo = g.take( );
            sort.m_key_hi = g.take( );
            manager->m_list.clear( );
            for ( unsigned i = 0; i < n; ++i )
                manager->m_list.push_back( &entries[ g.take( ) ] );
            manager->list_remove( &entries[ gone ] );
            const unsigned left = g.take( );
            g.eq( static_cast< unsigned >( manager->m_list.size( ) ), left );
            for ( unsigned i = 0; i < left; ++i )
                g.eq( i < manager->m_list.size( ) ? static_cast< gfx_dm_check_t::entry_t* >( manager->m_list[ i ] )->m_id : 0xDEADBEEFu, g.take( ) );
            if ( flush )
                manager->flush( sort );
            compare_log( );
            g.eq( sort.m_key_lo, g.take( ) );
            g.eq( sort.m_key_hi, g.take( ) );
            g.eq( static_cast< unsigned >( manager->m_list.size( ) ), g.take( ) );
        }
        g.eq( static_cast< unsigned >( manager->m_drawn ), g.take( ) );
        g.eq( static_cast< unsigned >( manager->m_culled ), g.take( ) );
        g.eq( static_cast< unsigned >( manager->m_shadow_drawn ), g.take( ) );
        g.eq( static_cast< unsigned >( manager->m_shadow_culled ), g.take( ) );
        g.eq( static_cast< unsigned >( manager->m_tree.m_count ), g.take( ) );
        if ( g.take( ) )
        {
            g.take( );                         // the dev page's query
            manager->tick( );
            g.eq( static_cast< unsigned >( manager->m_drawn ), g.take( ) );
            g.eq( static_cast< unsigned >( manager->m_culled ), g.take( ) );
            g.eq( static_cast< unsigned >( manager->m_shadow_drawn ), g.take( ) );
            g.eq( static_cast< unsigned >( manager->m_shadow_culled ), g.take( ) );
        }
    }
    const unsigned runs = g.take( );
    for ( unsigned r = 0; r < runs; ++r )
    {
        g.take( );                             // the row's length
        const unsigned count = g.take( ), cap = g.take( );
        manager->m_list.clear( );
        for ( unsigned i = 0; i < count; ++i )
            manager->m_list.push_back( &entries[ i ] );
        manager->m_capacity = static_cast< int >( cap );
        manager->list_remove( &entries[ 0 ] );
        const unsigned left = g.take( );
        g.eq( static_cast< unsigned >( manager->m_list.size( ) ), left );
        g.eq( static_cast< unsigned >( manager->m_capacity ), g.take( ) );
        g.take( );                             // a new block or not: the vector's business
        for ( unsigned i = 0; i < left; ++i )
            g.eq( i < manager->m_list.size( ) ? static_cast< gfx_dm_check_t::entry_t* >( manager->m_list[ i ] )->m_id : 0xDEADBEEFu, g.take( ) );
    }
    manager->m_list.clear( );
    hooks.m_calls.clear( );
    delete manager;
    gfx_draw_manager_release( );
    for ( unsigned i = 0; i < 2; ++i )
        g.eq( i < hooks.m_calls.size( ) ? hooks.m_calls[ i ] : 0u, g.take( ) );
    g.eq( 1u, g.take( ) );
    g.eq( 1u, g.take( ) );
    g.eq( static_cast< unsigned >( g_gfx_draw_manager != nullptr ), g.take( ) );
    if ( g.failed == 0 && g.at == g.size )
    {
        LOG_SUCCESS( "gfx_draw_manager matches retail's Vu3dDrawManager (0x5A7320, 0x5A73B0, 0x5A75E0, 0x5A7730, 0x5A7800, 0x5A7980, 0x5A79F0, 0x5A7A90, 0x5A7AF0, 0x5A7540, 0x5A7310) on %u steps, "
                     "%d collaborator calls: %d words exact", steps, calls, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_draw_manager FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_primitives: world/primitive.hxx against retail's builders 0x507BE0 .. 0x50B250, the keyed submit 0x50B7B0, the sphere test 0x50C250 and the segment / triangle test 0x50C6B0.
inline bool verify_gfx_primitives( )
{
    anim_cursor_t g;
    g.w = g_golden_gpr_words;
    g.size = g_golden_gpr_size;
    g.where = "gfx_primitives";
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    gfx_check_hooks_t hooks;
    static const std::uint32_t k_material_keys[ 4 ] = { 0x3FF, 0x001, 0x155, 0x2AA }, k_chunk_keys[ 3 ] = { 0x7FF, 0x401, 0x0AB };
    hooks.m_materials.resize( 4 );
    hooks.m_chunks.resize( 3 );
    for ( int i = 0; i < 4; ++i )
        hooks.m_materials[ i ].m_key = k_material_keys[ i ];
    for ( int i = 0; i < 3; ++i )
        hooks.m_chunks[ i ].m_key = k_chunk_keys[ i ];
    gfx_sort_t sort( hooks );
    sort.m_buffers[ 0 ].m_memory.resize( 0xF0000 );
    sort.m_buffers[ 0 ].m_capacity = 0xF0000;
    sort.m_buffers[ 1 ].m_memory.resize( 0xF0000 );
    sort.m_buffers[ 1 ].m_capacity = 0xF0000;
    sort.m_buffers[ 1 ].m_used = 0x35;
    gfx_util_t util{ sort };
    util.m_flat = 1;
    util.m_lines = 2;
    util.m_lines2 = 3;
    util.m_lit = 4;
    const unsigned rows = g.take( );
    for ( unsigned row = 0; row < rows; ++row )
    {
        const unsigned fn = g.take( );
        sort.m_index = static_cast< std::int32_t >( g.take( ) );
        sort.m_key_lo = g.take( );
        sort.m_key_hi = g.take( );
        const std::uint32_t colour = g.take( ), colour2 = g.take( ), a = g.take( ), b = g.take( );
        const std::uint32_t fw[ 3 ] = { g.take( ), g.take( ), g.take( ) };
        const float f0 = real( fw[ 0 ] ), f1 = real( fw[ 1 ] ), f2 = real( fw[ 2 ] );
        float va[ 16 ], vb[ 16 ], m0[ 16 ], m1[ 16 ], v[ 4 ], top[ 16 ];
        for ( float& x : va )
            x = real( g.take( ) );
        for ( float& x : vb )
            x = real( g.take( ) );
        for ( float& x : m0 )
            x = real( g.take( ) );
        for ( float& x : m1 )
            x = real( g.take( ) );
        for ( float& x : v )
            x = real( g.take( ) );
        for ( float& x : top )
            x = real( g.take( ) );
        util.m_top = top;
        switch ( fn )
        {
        case 0: util.ellipse( colour, va, a ); break;
        case 1: util.line_2d( colour, va, vb ); break;
        case 2: util.line_3d( colour, va, vb, m0 ); break;
        case 3: util.line_3d_two( va, colour, vb, colour2, m0 ); break;
        case 4:
        {
            float points[ 20 ];
            std::memcpy( points, va, sizeof( va ) );
            std::memcpy( points + 16, vb, 16 );
            util.strip_lines( colour, points, m0 );
            break;
        }
        case 5: util.aabb_lines( colour, va, m0 ); break;
        case 6: util.shape_508780( colour, a, m0, f0, f1 ); break;
        case 7: util.shape_508A40( colour, a, b, m0, f0 ); break;
        case 8: util.shape_508E30( colour, f2, m0, f0, f1 ); break;
        case 9: util.shape_509050( colour, va, vb, v, f2, m0 ); break;
        case 10: util.quad_fill( colour, va, m0 ); break;
        case 11: util.lit_509810( colour, va, m0, m1, v ); break;
        case 12: util.lit_shape( k_gfx_cb_509A40, colour, a, m0, m1, v, f0, f1 ); break;
        case 13: util.lit_509E80( colour, a, b, m0, m1, v, f0 ); break;
        case 14: util.lit_shape( k_gfx_cb_50A420, colour, a, m0, m1, v, f0, f1 ); break;
        case 15: util.lit_shape( k_gfx_cb_50A890, colour, a, m0, m1, v, f0, f1 ); break;
        case 16: util.key_command( a, colour2 ); break;
        default:
            sort.allocate( 0x20 );
            sort.submit_keyed( a, ( b >> 8 ) + 1, b & 0xFF, fw[ 1 ], f0 );
            break;
        }
        const unsigned at = g.take( ), used = g.take( ), count = g.take( );
        g.eq( static_cast< unsigned >( sort.m_offset ), at );
        g.eq( static_cast< unsigned >( sort.m_buffers[ sort.m_index ].m_used ), used );
        const std::uint8_t* record = sort.m_buffers[ sort.m_index ].m_memory.data( ) + at;
        for ( unsigned i = 0; i < count; ++i )
        {
            std::uint32_t w;
            std::memcpy( &w, record + 4 * i, 4 );
            g.eq( w, g.take( ) );
        }
        const ui_gfx_command_t& c = sort.m_commands[ sort.m_index ].back( );
        for ( int i = 0; i < 7; ++i )
            g.eq( i == 6 ? c.m_words[ i ] & 0xFFFFu : c.m_words[ i ], g.take( ) );
    }
    const unsigned spheres = g.take( );
    int visible = 0;
    for ( unsigned s = 0; s < spheres; ++s )
    {
        float planes[ 24 ], centre[ 3 ];
        for ( float& x : planes )
            x = real( g.take( ) );
        for ( float& x : centre )
            x = real( g.take( ) );
        const float radius = real( g.take( ) );
        const bool in = gfx_sphere_visible( planes, centre, radius );
        visible += in;
        g.eq( static_cast< unsigned >( in ), g.take( ) );
    }
    const unsigned triangles = g.take( );
    int hits = 0;
    for ( unsigned t = 0; t < triangles; ++t )
    {
        float p[ 15 ], hit[ 3 ] = { };
        for ( float& x : p )
            x = real( g.take( ) );
        const bool in = gfx_segment_triangle( p, p + 3, p + 6, p + 9, p + 12, hit );
        hits += in;
        g.eq( static_cast< unsigned >( in ), g.take( ) );
        for ( const float x : hit )
        {
            const std::uint32_t w = g.take( );
            g.eq( in ? bits( x ) : w, w );
        }
    }
    if ( g.failed == 0 && g.at == g.size )
    {
        LOG_SUCCESS( "gfx_primitives matches retail's VuGfxUtil builders (0x507BE0 .. 0x50B250), 0x50B7B0, 0x50C250 (%d of %u visible) and 0x50C6B0 (%d of %u hit) on %u rows: %d words exact",
                     visible, spheres, hits, triangles, rows, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_primitives FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_static_model: world/instance.hxx against retail's draw 0x520130, 0x5215A0, 0x521960 and 0x535220 on seeded fake scenes.
struct gfx_model_map_check_t : gfx_material_map_t
{
    std::uint32_t map( std::uint32_t name ) override
    {
        for ( const auto& e : m_table )
            if ( e.first == name )
                return e.second;
        return 0;
    }
    std::vector< std::pair< std::uint32_t, std::uint32_t > > m_table;
};

struct gfx_model_scene_t
{
    std::vector< std::unique_ptr< gfx_scene_node_t > > m_nodes;
    std::vector< std::unique_ptr< gfx_scene_mesh_t > > m_meshes;
    std::vector< std::unique_ptr< gfx_scene_part_t > > m_parts;
    std::vector< const gfx_scene_node_t* > m_roots;
    std::vector< const gfx_scene_mesh_t* > m_mesh_table;
};

inline const gfx_scene_node_t* gfx_model_read_node( anim_cursor_t& g, gfx_model_scene_t& scene, const std::vector< gfx_scene_material_t >& rows )
{
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    scene.m_nodes.push_back( std::make_unique< gfx_scene_node_t >( ) );
    gfx_scene_node_t* n = scene.m_nodes.back( ).get( );
    for ( float& f : n->m_matrix )
        f = real( g.take( ) );
    for ( float& f : n->m_aabb )
        f = real( g.take( ) );
    if ( g.take( ) )
    {
        scene.m_meshes.push_back( std::make_unique< gfx_scene_mesh_t >( ) );
        gfx_scene_mesh_t* m = scene.m_meshes.back( ).get( );
        n->m_mesh = m;
        const unsigned index = g.take( );
        if ( scene.m_mesh_table.size( ) <= index )
            scene.m_mesh_table.resize( index + 1 );
        scene.m_mesh_table[ index ] = m;
        for ( float& f : m->m_min )
            f = real( g.take( ) );
        for ( float& f : m->m_max )
            f = real( g.take( ) );
        const unsigned parts = g.take( );
        for ( unsigned i = 0; i < parts; ++i )
        {
            scene.m_parts.push_back( std::make_unique< gfx_scene_part_t >( ) );
            gfx_scene_part_t* p = scene.m_parts.back( ).get( );
            p->m_id = g.take( );
            p->m_row = &rows[ g.take( ) ];
            p->m_chunk = g.take( ) + 1;
            for ( float& f : p->m_aabb )
                f = real( g.take( ) );
            m->m_parts.push_back( p );
        }
    }
    const unsigned kids = g.take( );
    for ( unsigned i = 0; i < kids; ++i )
        n->m_children.push_back( gfx_model_read_node( g, scene, rows ) );
    return n;
}

inline bool verify_gfx_static_model( )
{
    anim_cursor_t g;
    g.w = g_golden_gsm_words;
    g.size = g_golden_gsm_size;
    g.where = "gfx_static_model";
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    gfx_check_hooks_t hooks;
    hooks.m_materials.resize( g.take( ) );
    for ( gfx_material_t& m : hooks.m_materials )
        m.m_key = g.take( );
    hooks.m_chunks.resize( g.take( ) );
    for ( gfx_chunk_t& c : hooks.m_chunks )
        c.m_key = g.take( );
    gfx_sort_t sort( hooks );
    sort.m_buffers[ 0 ].m_memory.resize( 0x2C0000 );
    sort.m_buffers[ 0 ].m_capacity = 0x2C0000;
    std::vector< gfx_scene_material_t > rows( g.take( ) );
    gfx_model_map_check_t map;
    for ( gfx_scene_material_t& r : rows )
    {
        r.m_name = g.take( );
        r.m_skip = static_cast< std::uint8_t >( g.take( ) );
        r.m_flag_1d = static_cast< std::uint8_t >( g.take( ) );
        r.m_by_depth = static_cast< std::uint8_t >( g.take( ) );
        r.m_type = g.take( );
        r.m_material = g.take( ) + 1;
        r.m_depth_material = g.take( ) + 1;
        map.m_table.push_back( { r.m_name, g.take( ) + 1 } );
        r.m_casts = static_cast< std::uint8_t >( g.take( ) );
        r.m_shadow_material = g.take( ) + 1;
    }
    const unsigned draws = g.take( );
    gfx_model_scene_t scene;
    int commands = 0;
    const auto compare_commands = [ & ]( )
    {
        const unsigned count = g.take( );
        g.eq( static_cast< unsigned >( sort.m_commands[ 0 ].size( ) ), count );
        commands += static_cast< int >( count );
        for ( unsigned c = 0; c < count; ++c )
        {
            const unsigned words = g.take( );
            if ( c >= sort.m_commands[ 0 ].size( ) )
            {
                for ( unsigned i = 0; i < words; ++i )
                    g.eq( 0xDEADBEEFu, g.take( ) );
                continue;
            }
            const ui_gfx_command_t& cmd = sort.m_commands[ 0 ][ c ];
            for ( unsigned i = 0; i < 7; ++i )
            {
                std::uint32_t w = cmd.m_words[ i ];
                if ( i == 4 || i == 5 )
                    w -= 1;
                if ( i == 6 )
                    w &= 0xFFFFu;
                g.eq( w, g.take( ) );
            }
            const std::uint8_t* r = sort.m_buffers[ 0 ].m_memory.data( ) + cmd.m_words[ 3 ];
            for ( unsigned i = 0; i + 7 < words; ++i )
            {
                std::uint32_t w;
                std::memcpy( &w, r + 4 * i, 4 );
                if ( i == 0x13 )
                    w &= 0xFFu;
                g.eq( w, g.take( ) );
            }
        }
    };
    const auto fresh = [ & ]( )
    {
        sort.m_commands[ 0 ].clear( );
        sort.m_buffers[ 0 ].m_used = 0;
        std::fill( sort.m_buffers[ 0 ].m_memory.begin( ), sort.m_buffers[ 0 ].m_memory.begin( ) + 0x10000, static_cast< std::uint8_t >( 0 ) );
    };
    for ( unsigned d = 0; d < draws; ++d )
    {
        const unsigned trees = g.take( );
        if ( trees )
        {
            scene = gfx_model_scene_t( );
            for ( unsigned i = 0; i < trees; ++i )
                scene.m_roots.push_back( gfx_model_read_node( g, scene, rows ) );
        }
        sort.m_key_lo = g.take( );
        sort.m_key_hi = g.take( );
        gfx_static_model_t model;
        model.m_colour = g.take( );
        model.m_word = g.take( );
        model.m_lod = real( g.take( ) );
        model.m_byte_1c = static_cast< std::uint8_t >( g.take( ) );
        model.m_two_pass = static_cast< std::uint8_t >( g.take( ) );
        model.m_map = g.take( ) ? &map : nullptr;
        model.m_scene = &scene.m_roots;
        model.m_meshes = &scene.m_mesh_table;
        gfx_draw_params_t params;
        gfx_camera_t camera;
        params.m_second_depth = static_cast< std::uint8_t >( g.take( ) );
        params.m_lod = real( g.take( ) );
        camera.m_far = real( g.take( ) );
        for ( float& f : camera.m_eye )
            f = real( g.take( ) );
        for ( int i = 0; i < 3; ++i )
            params.m_eye[ i ] = real( g.take( ) );
        for ( float& f : params.m_clip )
            f = real( g.take( ) );
        for ( float& f : camera.m_planes )
            f = real( g.take( ) );
        float parent[ 16 ];
        for ( float& f : parent )
            f = real( g.take( ) );
        params.m_camera = &camera;
        fresh( );
        model.draw( sort, parent, params );
        compare_commands( );
        g.eq( sort.m_key_lo, g.take( ) );
        g.eq( sort.m_key_hi, g.take( ) );
        fresh( );
        model.prefetch( sort );
        compare_commands( );
        fresh( );
        gfx_shadow_params_t shadow;
        gfx_shadow_volume_t volume;
        std::vector< gfx_shadow_cascade_t > cascades;
        for ( int i = 0; i < 3; ++i )
            shadow.m_eye[ i ] = real( g.take( ) );
        shadow.m_lod = real( g.take( ) );
        for ( float& f : shadow.m_clip )
            f = real( g.take( ) );
        volume.m_count = static_cast< int >( g.take( ) );
        for ( int i = 0; i < 4 * volume.m_count; ++i )
            volume.m_planes[ i ] = real( g.take( ) );
        cascades.resize( g.take( ) );
        for ( gfx_shadow_cascade_t& c : cascades )
        {
            c.m_volume.m_count = static_cast< int >( g.take( ) );
            for ( float& f : c.m_matrix )
                f = real( g.take( ) );
            for ( int i = 0; i < 4 * c.m_volume.m_count; ++i )
                c.m_volume.m_planes[ i ] = real( g.take( ) );
        }
        shadow.m_volume = &volume;
        shadow.m_cascades = static_cast< int >( cascades.size( ) );
        shadow.m_cascade = cascades.data( );
        model.draw_shadow( sort, parent, shadow );
        compare_commands( );
        g.eq( sort.m_key_lo, g.take( ) );
        g.eq( sort.m_key_hi, g.take( ) );
    }
    const unsigned boxes = g.take( );
    int visible = 0;
    for ( unsigned b = 0; b < boxes; ++b )
    {
        float a[ 8 ], m[ 16 ];
        gfx_draw_params_t p;
        gfx_camera_t camera;
        for ( float& f : a )
            f = real( g.take( ) );
        for ( float& f : m )
            f = real( g.take( ) );
        for ( int i = 0; i < 3; ++i )
            p.m_eye[ i ] = real( g.take( ) );
        p.m_lod = real( g.take( ) );
        for ( float& f : p.m_clip )
            f = real( g.take( ) );
        for ( float& f : camera.m_planes )
            f = real( g.take( ) );
        const float size = real( g.take( ) );
        p.m_camera = &camera;
        const bool in = gfx_box_visible( p, a, m, size );
        visible += in;
        g.eq( static_cast< unsigned >( in ), g.take( ) );
    }
    if ( g.failed == 0 && g.at == g.size )
    {
        LOG_SUCCESS( "gfx_static_model matches retail's VuStaticModelInstance draw (0x520130, 0x5215A0, 0x521960), shadow (0x5201B0, 0x520670, 0x5354B0, 0x535740, 0x5A8580), prefetch 0x520200 on %u draws, %d commands, and 0x535220 on %u boxes (%d visible): %d words exact",
                     draws, commands, boxes, visible, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_static_model FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_real_model: world/meshes.hxx's view of a scene loaded by assets/scene.hxx, field by field against retail's own loaded VuGfxStaticScene; then draw, prefetch, shadow and the ray
// test 0x520620 over the real meshes.
inline bool verify_gfx_real_model( )
{
    anim_cursor_t g;
    g.w = g_golden_grm_words;
    g.size = g_golden_grm_size;
    g.where = "gfx_real_model";
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "verify_gfx_real_model: Base.apf not found" );
        return false;
    }
    const unsigned models = g.take( );
    int commands = 0, rays = 0, hits = 0;
    for ( unsigned model_no = 0; model_no < models; ++model_no )
    {
        std::vector< std::uint8_t > blob;
        scene_t scene;
        const std::string name = std::string( "VuStaticModelAsset/" ) + g_golden_grm_names[ model_no ];
        if ( !archive.read( name.c_str( ), blob ) || !scene_load_static( blob.data( ), blob.size( ), scene ) )
        {
            LOG_ERROR( "verify_gfx_real_model: '%s' did not load", name.c_str( ) );
            return false;
        }
        gfx_scene_view_t view;
        gfx_scene_view_build( view, scene, blob.data( ) );
        gfx_check_hooks_t hooks;
        hooks.m_materials.resize( g.take( ) );
        for ( gfx_material_t& m : hooks.m_materials )
            m.m_key = g.take( );
        hooks.m_chunks.resize( g.take( ) );
        for ( gfx_chunk_t& c : hooks.m_chunks )
            c.m_key = g.take( );
        gfx_sort_t sort( hooks );
        sort.m_buffers[ 0 ].m_memory.resize( 0x2C0000 );
        sort.m_buffers[ 0 ].m_capacity = 0x2C0000;
        // The layout: every word the walks read, in retail's objects' order.
        const std::size_t layout_words = g.take( );
        const std::size_t layout_end = g.at + layout_words;
        g.eq( static_cast< unsigned >( view.m_rows.size( ) ), g.take( ) );
        g.eq( static_cast< unsigned >( view.m_meshes.size( ) ), g.take( ) );
        g.eq( static_cast< unsigned >( view.m_chunks.size( ) ), g.take( ) );
        for ( const gfx_scene_material_t& r : view.m_rows )
        {
            g.eq( r.m_name, g.take( ) );
            g.eq( r.m_skip, g.take( ) );
            g.eq( r.m_casts, g.take( ) );
            g.eq( r.m_flag_1d, g.take( ) );
            g.eq( r.m_by_depth, g.take( ) );
            g.eq( r.m_type, g.take( ) );
        }
        for ( const gfx_scene_chunk_t& c : view.m_chunks )
            g.eq( c.m_stride, g.take( ) );
        for ( const gfx_scene_mesh_t* mesh : view.m_meshes )
        {
            g.eq( static_cast< unsigned >( mesh->m_parts.size( ) ), g.take( ) );
            for ( float f : mesh->m_min )
                g.eq( bits( f ), g.take( ) );
            for ( float f : mesh->m_max )
                g.eq( bits( f ), g.take( ) );
            for ( const gfx_scene_part_t* p : mesh->m_parts )
            {
                g.eq( p->m_id, g.take( ) );
                g.eq( static_cast< unsigned >( p->m_row - view.m_rows.data( ) ), g.take( ) );
                g.eq( static_cast< unsigned >( p->m_arrays - view.m_chunks.data( ) ), g.take( ) );
                g.eq( p->m_first_index, g.take( ) );
                g.eq( static_cast< unsigned >( p->m_triangles ), g.take( ) );
                for ( float f : p->m_aabb )
                    g.eq( bits( f ), g.take( ) );
            }
        }
        const std::function< void( const gfx_scene_node_t& ) > compare_node = [ & ]( const gfx_scene_node_t& n )
        {
            for ( float f : n.m_matrix )
                g.eq( bits( f ), g.take( ) );
            for ( int k : { 0, 1, 2, 4, 5, 6 } )
                g.eq( bits( n.m_aabb[ k ] ), g.take( ) );
            std::uint32_t mesh = 0;
            for ( std::size_t i = 0; i < view.m_meshes.size( ); ++i )
                if ( view.m_meshes[ i ] == n.m_mesh )
                    mesh = static_cast< std::uint32_t >( i ) + 1u;
            g.eq( mesh, g.take( ) );
            g.eq( static_cast< unsigned >( n.m_children.size( ) ), g.take( ) );
            for ( const gfx_scene_node_t* child : n.m_children )
                compare_node( *child );
        };
        g.eq( static_cast< unsigned >( view.m_roots.size( ) ), g.take( ) );
        for ( const gfx_scene_node_t* root : view.m_roots )
            compare_node( *root );
        g.eq( static_cast< unsigned >( g.at ), static_cast< unsigned >( layout_end ) );
        g.at = layout_end;
        const auto compare_commands = [ & ]( )
        {
            const unsigned count = g.take( );
            g.eq( static_cast< unsigned >( sort.m_commands[ 0 ].size( ) ), count );
            commands += static_cast< int >( count );
            for ( unsigned c = 0; c < count; ++c )
            {
                const unsigned words = g.take( );
                if ( c >= sort.m_commands[ 0 ].size( ) )
                {
                    for ( unsigned i = 0; i < words; ++i )
                        g.eq( 0xDEADBEEFu, g.take( ) );
                    continue;
                }
                const ui_gfx_command_t& cmd = sort.m_commands[ 0 ][ c ];
                for ( unsigned i = 0; i < 7; ++i )
                {
                    std::uint32_t w = cmd.m_words[ i ];
                    if ( i == 4 || i == 5 )
                        w -= 1;
                    if ( i == 6 )
                        w &= 0xFFFFu;
                    g.eq( w, g.take( ) );
                }
                const std::uint8_t* r = sort.m_buffers[ 0 ].m_memory.data( ) + cmd.m_words[ 3 ];
                for ( unsigned i = 0; i + 7 < words; ++i )
                {
                    std::uint32_t w;
                    std::memcpy( &w, r + 4 * i, 4 );
                    if ( i == 0x13 )
                        w &= 0xFFu;
                    g.eq( w, g.take( ) );
                }
            }
        };
        const auto fresh = [ & ]( )
        {
            sort.m_commands[ 0 ].clear( );
            sort.m_buffers[ 0 ].m_used = 0;
            std::fill( sort.m_buffers[ 0 ].m_memory.begin( ), sort.m_buffers[ 0 ].m_memory.begin( ) + 0x40000, static_cast< std::uint8_t >( 0 ) );
        };
        const unsigned draws = g.take( );
        for ( unsigned d = 0; d < draws; ++d )
        {
            sort.m_key_lo = g.take( );
            sort.m_key_hi = g.take( );
            gfx_static_model_t model;
            gfx_scene_view_attach( model, view );
            model.m_colour = g.take( );
            model.m_word = g.take( );
            model.m_lod = real( g.take( ) );
            model.m_byte_1c = static_cast< std::uint8_t >( g.take( ) );
            model.m_two_pass = static_cast< std::uint8_t >( g.take( ) );
            gfx_draw_params_t params;
            gfx_camera_t camera;
            params.m_second_depth = static_cast< std::uint8_t >( g.take( ) );
            params.m_lod = real( g.take( ) );
            camera.m_far = real( g.take( ) );
            for ( float& f : camera.m_eye )
                f = real( g.take( ) );
            for ( int i = 0; i < 3; ++i )
                params.m_eye[ i ] = real( g.take( ) );
            for ( float& f : params.m_clip )
                f = real( g.take( ) );
            for ( float& f : camera.m_planes )
                f = real( g.take( ) );
            float parent[ 16 ];
            for ( float& f : parent )
                f = real( g.take( ) );
            params.m_camera = &camera;
            fresh( );
            model.draw( sort, parent, params );
            compare_commands( );
            g.eq( sort.m_key_lo, g.take( ) );
            g.eq( sort.m_key_hi, g.take( ) );
            fresh( );
            model.prefetch( sort );
            compare_commands( );
            fresh( );
            gfx_shadow_params_t shadow;
            gfx_shadow_volume_t volume;
            std::vector< gfx_shadow_cascade_t > cascades;
            for ( int i = 0; i < 3; ++i )
                shadow.m_eye[ i ] = real( g.take( ) );
            shadow.m_lod = real( g.take( ) );
            for ( float& f : shadow.m_clip )
                f = real( g.take( ) );
            volume.m_count = static_cast< int >( g.take( ) );
            for ( int i = 0; i < 4 * volume.m_count; ++i )
                volume.m_planes[ i ] = real( g.take( ) );
            cascades.resize( g.take( ) );
            for ( gfx_shadow_cascade_t& c : cascades )
            {
                c.m_volume.m_count = static_cast< int >( g.take( ) );
                for ( float& f : c.m_matrix )
                    f = real( g.take( ) );
                for ( int i = 0; i < 4 * c.m_volume.m_count; ++i )
                    c.m_volume.m_planes[ i ] = real( g.take( ) );
            }
            shadow.m_volume = &volume;
            shadow.m_cascades = static_cast< int >( cascades.size( ) );
            shadow.m_cascade = cascades.data( );
            model.draw_shadow( sort, parent, shadow );
            compare_commands( );
            g.eq( sort.m_key_lo, g.take( ) );
            g.eq( sort.m_key_hi, g.take( ) );
            fresh( );
            model.m_colour = g.take( );
            std::vector< gfx_scene_piece_t > pieces( g.take( ) );
            for ( gfx_scene_piece_t& piece : pieces )
            {
                piece.m_node = &view.m_nodes[ g.take( ) ];
                for ( float& f : piece.m_matrix )
                    f = real( g.take( ) );
            }
            model.draw_pieces( sort, pieces.data( ), static_cast< int >( pieces.size( ) ), params );
            compare_commands( );
            g.eq( sort.m_key_lo, g.take( ) );
            g.eq( sort.m_key_hi, g.take( ) );
            sort.m_key_lo = g.take( );
            sort.m_key_hi = g.take( );
            const unsigned sky_slot = g.take( ), sky_over = g.take( );
            gfx_skybox_t sky;
            if ( sky_over )
                sky.m_viewport[ sky_slot ] = &view;
            else
                sky.m_asset = &view;
            float sky_eye[ 3 ];
            for ( float& f : sky_eye )
                f = real( g.take( ) );
            fresh( );
            sky.draw( sort, sky_eye );
            compare_commands( );
            g.eq( sort.m_key_lo, g.take( ) );
            g.eq( sort.m_key_hi, g.take( ) );
        }
        const std::vector< gfx_piece_seed_t > seed = gfx_pieces_seed( view );
        g.take( );
        g.eq( static_cast< unsigned >( seed.size( ) ), g.take( ) );
        for ( const gfx_piece_seed_t& s : seed )
        {
            g.eq( static_cast< unsigned >( s.m_node - view.m_nodes.data( ) ), g.take( ) );
            for ( float f : s.m_inverse )
                g.eq( bits( f ), g.take( ) );
            for ( float f : s.m_pivot )
                g.eq( bits( f ), g.take( ) );
            g.eq( 0u, g.take( ) );
        }
        const unsigned count = g.take( );
        gfx_static_model_t model;
        gfx_scene_view_attach( model, view );
        for ( unsigned r = 0; r < count; ++r )
        {
            float parent[ 16 ], a[ 3 ], b[ 3 ];
            for ( float& f : parent )
                f = real( g.take( ) );
            for ( float& f : a )
                f = real( g.take( ) );
            for ( float& f : b )
                f = real( g.take( ) );
            const bool hit = model.ray( parent, a, b );
            g.eq( hit ? 1u : 0u, g.take( ) );
            for ( float f : b )
                g.eq( bits( f ), g.take( ) );
            ++rays;
            hits += hit ? 1 : 0;
        }
    }
    g.eq( static_cast< unsigned >( g.at ), static_cast< unsigned >( g.size ) );
    if ( !g.failed )
    {
        LOG_SUCCESS( "gfx_real_model matches retail on shipped models: the scene view field by field (world/meshes.hxx), draw 0x520130, prefetch 0x520200, shadow 0x5201B0, ray test 0x520620 "
                     "0x520A50 0x520D30 0x520F20: %u models, %d commands, %d rays (%d hits), %d words", models, commands, rays, hits, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_real_model FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_components: entity/draw.hxx's choices and world/meshes.hxx's components against 0x5B0600, 0x5B08D0, 0x5B09B0, 0x61DAE0, 0x61DC00, 0x543790 with the instances' own draws intercepted.
inline bool verify_gfx_components( )
{
    anim_cursor_t g;
    g.w = g_golden_gco_words;
    g.size = g_golden_gco_size;
    g.where = "gfx_components";
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    gfx_check_hooks_t hooks;
    gfx_sort_t sort( hooks );
    const unsigned rows = g.take( );
    int drawn = 0;
    const auto compare_params = [ & ]( const gfx_draw_params_t& p )
    {
        for ( float f : p.m_eye )
            g.eq( bits( f ), g.take( ) );
        g.eq( p.m_camera != nullptr ? 1u : 0u, g.take( ) );
        g.eq( bits( p.m_lod ), g.take( ) );
        g.eq( p.m_reflecting, g.take( ) );
        for ( float f : p.m_clip )
            g.eq( bits( f ), g.take( ) );
        g.eq( static_cast< unsigned >( p.m_second_depth ) | ( static_cast< unsigned >( p.m_skip_models ) << 8 ), g.take( ) );
    };
    for ( unsigned r = 0; r < rows; ++r )
    {
        const std::size_t end = g.take( );
        const std::size_t stop = g.at + end;
        const unsigned kind = g.take( );
        model_draw_t c{ };
        gfx_static_component_t sc;
        gfx_animated_component_t ac;
        gfx_breakable_component_t bc;
        float mn[ 4 ] = { }, mx[ 4 ] = { };
        for ( int i = 0; i < 3; ++i )
            mn[ i ] = real( g.take( ) );
        for ( int i = 0; i < 3; ++i )
            mx[ i ] = real( g.take( ) );
        gfx_camera_t camera;
        for ( float& f : camera.m_eye )
            f = real( g.take( ) );
        const float eye_w = real( g.take( ) );
        const float d0 = real( g.take( ) ), d1 = real( g.take( ) ), d2 = real( g.take( ) );
        const std::uint32_t rejection_bits = g.take( );
        c.rejection_scale_modifier = real( rejection_bits );
        c.color = g.take( );
        c.dynamic_lighting = static_cast< std::uint8_t >( g.take( ) );
        const bool flag_a = g.take( ) != 0, flag_b = g.take( ) != 0;
        c.draw_distance = d0;
        c.lod0_draw_distance = d1;
        c.lod1_draw_distance = d2;
        sc.m_draw = &c;
        ac.m_draw = &c;
        std::memcpy( sc.m_min, mn, 16 );
        std::memcpy( sc.m_max, mx, 16 );
        std::memcpy( ac.m_min, mn, 16 );
        std::memcpy( ac.m_max, mx, 16 );
        for ( gfx_static_model_t& lod : sc.m_lods )
        {
            lod.m_lod = 0.0f;
            lod.m_colour = 0;
            lod.m_byte_1c = 0;
        }
        const auto lod_words = [ & ]( int i, bool hit, std::uint32_t matrix )
        {
            g.eq( hit ? 0xBCu + 0x20u * static_cast< unsigned >( i ) : 0u, g.take( ) );
            g.eq( hit ? bits( sc.m_lods[ i ].m_lod ) : 0u, g.take( ) );
            g.eq( hit ? sc.m_lods[ i ].m_colour : 0u, g.take( ) );
            g.eq( hit ? sc.m_lods[ i ].m_byte_1c : 0u, g.take( ) );
            g.eq( hit ? matrix : 0u, g.take( ) );
            drawn += hit ? 1 : 0;
        };
        if ( kind == 0 )
        {
            sc.draw_info( sort, camera, eye_w, flag_a );
            const static_draw_t s = static_model_select_info( c, mn, mx, camera.m_eye, flag_a );
            lod_words( s.instance < 0 ? 0 : s.instance, s.instance >= 0, 0x13Cu );
            if ( s.instance >= 0 )
                compare_params( gfx_info_params( camera, eye_w ) );
            else
                g.at += 12;
        }
        else if ( kind == 1 )
        {
            gfx_shadow_params_t p;
            std::memcpy( p.m_eye, camera.m_eye, 12 );
            p.m_clip_flag = flag_a;
            p.m_skip_models = flag_b;
            sc.draw_shadow( sort, p );
            const int i = static_model_select_shadow( c, mn, mx, p.m_eye, flag_a, flag_b );
            lod_words( i < 0 ? 0 : i, i >= 0, 0x13Cu );
        }
        else if ( kind == 2 )
        {
            for ( unsigned i = 0; i < 4; ++i )
                g.eq( 0xBCu + 0x20u * i, g.take( ) );
        }
        else if ( kind == 3 )
        {
            ac.draw_info( sort, camera, eye_w );
            g.eq( 0x68u, g.take( ) );
            g.eq( ac.m_model.m_colour, g.take( ) );
            g.eq( ac.m_model.m_byte, g.take( ) );
            g.eq( 0x104u, g.take( ) );
            compare_params( gfx_info_params( camera, eye_w ) );
            ++drawn;
        }
        else if ( kind == 4 )
        {
            const bool casts = animated_model_casts( c, mn, mx, camera.m_eye );
            g.eq( casts ? 1u : 0u, g.take( ) );
            g.eq( casts ? 0x68u : 0u, g.take( ) );
            g.eq( casts ? 0x104u : 0u, g.take( ) );
            drawn += casts ? 1 : 0;
        }
        else if ( kind == 6 )
        {
            float euler[ 3 ], m[ 16 ];
            for ( float& f : euler )
                f = real( g.take( ) );
            gfx_matrix_set_euler( m, euler );
            for ( float f : m )
                g.eq( bits( f ), g.take( ) );
        }
        else
        {
            bc.m_colour = c.color;
            bc.m_draw_distance = d0;
            bc.m_rejection = c.rejection_scale_modifier;
            bc.m_lighting = c.dynamic_lighting;
            bc.m_life = real( g.take( ) );
            bc.m_fade_time = real( g.take( ) );
            std::memcpy( bc.m_min, mn, 16 );
            std::memcpy( bc.m_max, mx, 16 );
            bc.m_model.m_lod = 0.0f;
            bc.m_model.m_colour = 0;
            bc.m_model.m_byte_1c = 0;
            const bool hit = bc.prepare( camera.m_eye );
            g.eq( hit ? 1u : 0u, g.take( ) );
            g.eq( hit ? 0x70u : 0u, g.take( ) );
            g.eq( bc.m_colour, g.take( ) );
            g.eq( bits( bc.m_model.m_lod ), g.take( ) );
            g.eq( bc.m_model.m_colour, g.take( ) );
            g.eq( bc.m_model.m_byte_1c, g.take( ) );
            drawn += hit ? 1 : 0;
        }
        g.eq( static_cast< unsigned >( g.at ), static_cast< unsigned >( stop ) );
        g.at = stop;
    }
    if ( !g.failed )
    {
        LOG_SUCCESS( "gfx_components matches retail's draw components (0x5B0600, 0x5B08D0, 0x5B09B0, 0x61DAE0, 0x61DC00, 0x543790): %u rows, %d handed on, %d words", rows, drawn, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_components FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_corona: world/corona.hxx against retail's 0x5E8080, 0x5E82D0, 0x5E7630, 0x5E7650.
struct gfx_corona_check_ray_t : gfx_corona_ray_t
{
    bool blocked( const float from[ 3 ], const float to[ 3 ] ) override
    {
        const bool hit = m_seeds[ m_used++ ] != 0;
        for ( int i = 0; i < 3; ++i )
            m_seen.push_back( from[ i ] );
        for ( int i = 0; i < 3; ++i )
            m_seen.push_back( to[ i ] );
        return hit;
    }
    unsigned m_seeds[ 4 ] = { };
    unsigned m_used = 0;
    std::vector< float > m_seen;
};

// Case text_word_wrap: world/wrap.hxx against retail's 0x5A8600 0x5A8660 0x5A86C0 0x5A8760 0x5A8870 0x5A88B0 0x5A88F0. The wrap helpers' positions are relative to the text; a line's call starts at its own position.
inline bool verify_text_word_wrap( )
{
    anim_cursor_t g;
    g.w = g_golden_gww_words;
    g.size = g_golden_gww_size;
    g.where = "text_word_wrap";
    const unsigned rows = g.take( );
    int chars = 0, texts = 0, lines = 0;
    for ( unsigned r = 0; r < rows; ++r )
    {
        const std::size_t words = g.take( );
        const std::size_t stop = g.at + words;
        const unsigned kind = g.take( );
        const std::uint32_t flags = g.take( );
        if ( kind == 0 )
        {
            const std::uint16_t c = static_cast< std::uint16_t >( g.take( ) );
            g.eq( wrap_lookup( c, flags, false ), g.take( ) );
            g.eq( wrap_lookup( c, flags, true ), g.take( ) );
            g.eq( wrap_breakable( c, flags ) ? 1u : 0u, g.take( ) );
            ++chars;
        }
        else
        {
            const std::uint32_t width = g.take( );
            const unsigned length = g.take( );
            std::vector< std::uint16_t > text( length + 512, 0 );
            for ( unsigned i = 0; i < length; ++i )
                text[ i ] = static_cast< std::uint16_t >( g.take( ) );
            for ( unsigned p = g.take( ); p != 0xFFFFFFFFu; p = g.take( ) )
            {
                g.eq( wrap_can_break( text.data( ), static_cast< int >( p ), flags ) ? 1u : 0u, g.take( ) & 1u ? 1u : 0u );
                g.eq( static_cast< std::uint32_t >( wrap_next_line( text.data( ), static_cast< int >( p ) ) + 1 ), g.take( ) );
                g.eq( static_cast< std::uint32_t >( wrap_trim( text.data( ), static_cast< int >( p ) ) + 1 ), g.take( ) );
            }
            for ( unsigned at = g.take( ); at != 0xFFFFFFFFu; at = g.take( ) )
            {
                int last = 0;
                const auto measure = []( std::uint16_t c ) { return ( static_cast< std::uint32_t >( c ) * 7u + 3u ) & 0x1Fu; };
                const int next = wrap_line( text.data( ) + at, width, flags, last, measure );
                g.eq( next < 0 ? 0u : next + at + 1u, g.take( ) );
                g.eq( last < 0 ? 0u : last + at + 1u, g.take( ) );
                ++lines;
            }
            ++texts;
        }
        if ( g.at != stop )
        {
            LOG_ERROR( "text_word_wrap: row %u off by %zd words", r, static_cast< std::ptrdiff_t >( g.at ) - static_cast< std::ptrdiff_t >( stop ) );
            return false;
        }
    }
    if ( g.failed == 0 && g.at == g.size && chars > 0 && lines > texts )
    {
        LOG_SUCCESS( "text_word_wrap matches retail's 0x5A88F0 and its helpers: %d words, %d characters, %d texts, %d lines", g.compared, chars, texts, lines );
        return true;
    }
    LOG_ERROR( "text_word_wrap FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_corona_entity: world/corona.hxx against retail's 0x5E9B60, 0x61D000, 0x5E9BD0, 0x61CE30, 0x50CDF0, 0x50CE20.
struct gfx_corona_entity_check_ray_t : gfx_corona_entity_ray_t
{
    bool blocked( const float from[ 3 ], const float to[ 3 ], std::uint32_t mask ) override
    {
        const bool hit = m_seeds[ m_used++ ] != 0;
        for ( int i = 0; i < 3; ++i )
            m_seen.push_back( from[ i ] );
        for ( int i = 0; i < 3; ++i )
            m_seen.push_back( to[ i ] );
        m_masks.push_back( mask );
        return hit;
    }
    unsigned m_seeds[ 4 ] = { };
    unsigned m_used = 0;
    std::vector< float > m_seen;
    std::vector< std::uint32_t > m_masks;
};

inline bool verify_gfx_corona_entity( )
{
    anim_cursor_t g;
    g.w = g_golden_gce_words;
    g.size = g_golden_gce_size;
    g.where = "gfx_corona_entity";
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    static std::uint8_t image[ 0x2E0 ];
    static collision_object_t object;
    static int surface_answer;
    gfx_corona_collision_ray_t bullet;
    bullet.m_image = []( void*, const collision_object_t& ) { return static_cast< const std::uint8_t* >( image ); };
    bullet.m_surface = []( void*, const collision_object_t&, int ) { return surface_answer; };
    const unsigned rows = g.take( );
    int kinds[ 4 ] = { };
    for ( unsigned r = 0; r < rows; ++r )
    {
        const std::size_t words = g.take( );
        const std::size_t stop = g.at + words;
        const unsigned kind = g.take( );
        ++kinds[ kind & 3u ];
        if ( kind == 0 )
        {
            gfx_corona_entity_t e;
            const int count = static_cast< int >( g.take( ) );
            e.m_speed = real( g.take( ) );
            e.m_radius = real( g.take( ) );
            e.m_mask = g.take( );
            const float dt = real( g.take( ) );
            e.m_phase = real( g.take( ) );
            float position[ 3 ];
            for ( float& f : position )
                f = real( g.take( ) );
            for ( std::uint8_t& b : e.m_request )
                b = static_cast< std::uint8_t >( g.take( ) );
            for ( float& f : e.m_visible )
                f = real( g.take( ) );
            std::vector< gfx_sun_view_t > views( count );
            for ( gfx_sun_view_t& v : views )
                for ( float& f : v.m_camera.m_eye )
                    f = real( g.take( ) );
            gfx_corona_entity_check_ray_t ray;
            for ( unsigned& u : ray.m_seeds )
                u = g.take( );
            e.tick( position, views.data( ), count, ray, dt );
            const unsigned casts = static_cast< unsigned >( ray.m_masks.size( ) );
            g.eq( casts, g.take( ) );
            for ( unsigned i = 0; i < casts; ++i )
            {
                for ( int k = 0; k < 6; ++k )
                    g.eq( bits( ray.m_seen[ i * 6u + k ] ), g.take( ) );
                g.eq( ray.m_masks[ i ], g.take( ) );
            }
            for ( std::uint8_t b : e.m_request )
                g.eq( b, g.take( ) );
            for ( float f : e.m_visible )
                g.eq( bits( f ), g.take( ) );
            g.eq( bits( e.m_phase ), g.take( ) );
        }
        else if ( kind == 1 )
        {
            gfx_corona_entity_t e;
            e.m_radius = real( g.take( ) );
            float low[ 3 ], high[ 3 ];
            e.aabb( low, high );
            for ( float f : low )
                g.eq( bits( f ), g.take( ) );
            for ( float f : high )
                g.eq( bits( f ), g.take( ) );
        }
        else if ( kind == 2 )
        {
            const std::uint8_t flags = static_cast< std::uint8_t >( g.take( ) );
            const std::int16_t group = static_cast< std::int16_t >( g.take( ) );
            const std::uint32_t mask = g.take( );
            object.internal_type = static_cast< int >( g.take( ) );
            g.eq( gfx_corona_ray_wants_mask( flags, group, mask ) ? 1u : 0u, g.take( ) );
            image[ 0x2D0 ] = flags;
            std::memcpy( image + 0x2CA, &group, 2 );
            bullet.m_use_mask = true;
            bullet.m_mask = mask;
            g.eq( bullet.wants( object ) ? 1u : 0u, g.take( ) );
        }
        else
        {
            const bool in_world = g.take( ) != 0;
            const bool info = g.take( ) != 0;
            const bool shape = g.take( ) != 0;
            const unsigned surface = g.take( );
            const int triangle = info ? static_cast< int >( g.take( ) ) : ( g.take( ), -1 );
            surface_answer = shape ? static_cast< int >( surface ) : -1;
            const float fraction = real( g.take( ) );
            bullet.m_result = gfx_corona_result_t( );
            bullet.m_result.m_fraction = real( g.take( ) );
            bullet.m_hit = false;
            for ( int i = 0; i < 12; ++i )
            {
                const std::uint32_t u = g.take( );
                std::memcpy( image + 0x10 + 4 * i, &u, 4 );
            }
            float normal[ 4 ];
            for ( float& f : normal )
                f = real( g.take( ) );
            g.eq( bits( bullet.add( object, triangle, fraction, normal, in_world ) ), g.take( ) );
            g.eq( bullet.m_hit ? 1u : 0u, g.take( ) );
            g.eq( bullet.m_result.m_body ? 1u : 0u, g.take( ) );
            g.eq( bits( bullet.m_result.m_fraction ), g.take( ) );
            g.eq( bullet.m_result.m_triangle, g.take( ) );
            for ( int i = 0; i < 3; ++i )
                g.eq( bullet.m_result.m_normal[ i ], g.take( ) );
        }
        if ( g.at != stop )
        {
            LOG_ERROR( "gfx_corona_entity: row %u used %zu of %zu words", r, g.at + words - stop, words );
            return false;
        }
    }
    if ( g.failed == 0 && g.at == g.size && kinds[ 0 ] > 0 && kinds[ 1 ] > 0 && kinds[ 2 ] > 0 && kinds[ 3 ] > 0 )
    {
        LOG_SUCCESS( "gfx_corona_entity matches retail's VuCoronaEntity tick 0x5E9B60 + 0x61D000, aabb 0x5E9BD0, ray result 0x61CE30, RayTestWrapper 0x50CDF0 / 0x50CE20: %d words, rows %d / %d / %d / %d",
            g.compared, kinds[ 0 ], kinds[ 1 ], kinds[ 2 ], kinds[ 3 ] );
        return true;
    }
    LOG_ERROR( "gfx_corona_entity FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

inline bool verify_gfx_corona( )
{
    anim_cursor_t g;
    g.w = g_golden_gcn_words;
    g.size = g_golden_gcn_size;
    g.where = "gfx_corona";
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    gfx_check_hooks_t hooks;
    hooks.m_materials.resize( 1 );
    hooks.m_materials[ 0 ].m_key = g.take( );
    gfx_sort_t sort( hooks );
    sort.m_buffers[ 0 ].m_memory.resize( 0x10000 );
    sort.m_buffers[ 0 ].m_capacity = 0x10000;
    const unsigned rows = g.take( );
    int hit_rays = 0, records = 0;
    for ( unsigned r = 0; r < rows; ++r )
    {
        const std::size_t words = g.take( );
        const std::size_t stop = g.at + words;
        if ( g.take( ) == 0 )
        {
            gfx_sun_corona_t sun;
            const int count = static_cast< int >( g.take( ) );
            for ( float& f : sun.m_forward )
                f = real( g.take( ) );
            for ( std::uint8_t& b : sun.m_request )
                b = static_cast< std::uint8_t >( g.take( ) );
            for ( float& f : sun.m_visible )
                f = real( g.take( ) );
            std::vector< gfx_sun_view_t > views( count );
            for ( gfx_sun_view_t& v : views )
            {
                for ( float& f : v.m_camera.m_eye )
                    f = real( g.take( ) );
                v.m_camera.m_far = real( g.take( ) );
            }
            gfx_corona_check_ray_t ray;
            for ( unsigned& u : ray.m_seeds )
                u = g.take( );
            sun.tick( views.data( ), count, ray );
            g.eq( static_cast< unsigned >( ray.m_seen.size( ) / 6 ), g.take( ) );
            for ( float f : ray.m_seen )
                g.eq( bits( f ), g.take( ) );
            hit_rays += static_cast< int >( ray.m_seen.size( ) / 6 );
            for ( std::uint8_t b : sun.m_request )
                g.eq( b, g.take( ) );
            for ( float f : sun.m_visible )
                g.eq( bits( f ), g.take( ) );
            sun.m_colour = g.take( );
            sun.m_size = real( g.take( ) );
            sun.m_word_94 = g.take( );
            sun.m_word_98 = g.take( );
            sun.m_material = 1;
            gfx_camera_t camera;
            for ( float& f : camera.m_eye )
                f = real( g.take( ) );
            camera.m_far = real( g.take( ) );
            const float c44 = real( g.take( ) );
            gfx_draw_params_t p;
            p.m_camera = &camera;
            sort.m_key_lo = g.take( );
            sort.m_key_hi = g.take( );
            sort.m_commands[ 0 ].clear( );
            sort.m_buffers[ 0 ].m_used = 0;
            std::fill( sort.m_buffers[ 0 ].m_memory.begin( ), sort.m_buffers[ 0 ].m_memory.end( ), static_cast< std::uint8_t >( 0 ) );
            sun.draw( sort, p, c44 );
            for ( std::uint8_t b : sun.m_request )
                g.eq( b, g.take( ) );
            const unsigned commands = g.take( );
            g.eq( static_cast< unsigned >( sort.m_commands[ 0 ].size( ) ), commands );
            for ( unsigned c = 0; c < commands && c < sort.m_commands[ 0 ].size( ); ++c )
            {
                const ui_gfx_command_t& cmd = sort.m_commands[ 0 ][ c ];
                for ( unsigned i = 0; i < 7; ++i )
                {
                    std::uint32_t w = cmd.m_words[ i ];
                    if ( i == 4 )
                        w -= 1;
                    if ( i == 6 )
                        w &= 0xFFFFu;
                    g.eq( w, g.take( ) );
                }
                const std::uint8_t* rec = sort.m_buffers[ 0 ].m_memory.data( ) + cmd.m_words[ 3 ];
                for ( unsigned i = 0; i < 8; ++i )
                {
                    std::uint32_t w;
                    std::memcpy( &w, rec + 4 * i, 4 );
                    g.eq( w, g.take( ) );
                }
                ++records;
            }
        }
        else
        {
            const std::uint8_t flags = static_cast< std::uint8_t >( g.take( ) );
            g.eq( gfx_corona_ray_wants( flags ) ? 1u : 0u, g.take( ) );
            const bool shape = g.take( ) != 0;
            const std::uint8_t surface = static_cast< std::uint8_t >( g.take( ) );
            const std::uint32_t tri = g.take( );
            const float fraction = real( g.take( ) );
            std::uint32_t normal[ 4 ];
            for ( std::uint32_t& n : normal )
                n = g.take( );
            gfx_corona_result_t result;
            const bool kept = result.add( 1u, fraction, tri, normal, shape, surface );
            g.eq( kept ? 1u : 0u, g.take( ) );
            g.eq( result.m_body, g.take( ) );
            g.eq( bits( result.m_fraction ), g.take( ) );
            g.eq( result.m_triangle, g.take( ) );
            for ( std::uint32_t n : result.m_normal )
                g.eq( n, g.take( ) );
        }
        g.eq( static_cast< unsigned >( g.at ), static_cast< unsigned >( stop ) );
        g.at = stop;
    }
    if ( !g.failed )
    {
        LOG_SUCCESS( "gfx_corona matches retail's VuDirectionalCoronaEntity tick 0x5E8080, draw 0x5E82D0, ray result 0x5E7630 / 0x5E7650: %u rows, %d rays, %d records, %d words", rows, hit_rays, records, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_corona FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_sort_life: gfx_sort_t start / finish / close_events against 0x53A0C0, 0x53A1C0, 0x539DC0: the collaborators' calls, in order.
inline bool verify_gfx_sort_life( )
{
    anim_cursor_t g;
    g.w = g_golden_gsl_words;
    g.size = g_golden_gsl_size;
    g.where = "gfx_sort_life";
    const unsigned rows = g.take( );
    for ( unsigned r = 0; r < rows; ++r )
    {
        gfx_check_hooks_t hooks;
        gfx_sort_t sort( hooks );
        hooks.m_log.clear( );
        sort.m_pending = g.take( ) != 0;
        sort.m_event[ 0 ] = g.take( );
        sort.m_event[ 1 ] = g.take( );
        sort.start( );
        sort.finish( );
        g.eq( sort.m_pending ? 1u : 0u, g.take( ) );
        sort.close_events( );
        const unsigned calls = g.take( );
        g.eq( static_cast< unsigned >( hooks.m_log.size( ) / 5 ), calls );
        for ( unsigned i = 0; i < 5 * calls; ++i )
            g.eq( i < hooks.m_log.size( ) ? hooks.m_log[ i ] : 0xDEADBEEFu, g.take( ) );
    }
    g.eq( static_cast< unsigned >( g.at ), static_cast< unsigned >( g.size ) );
    if ( !g.failed )
    {
        LOG_SUCCESS( "gfx_sort_life matches retail's VuGfxSort 0x53A0C0, 0x53A1C0, 0x539DC0: %d words", g.compared );
        return true;
    }
    LOG_ERROR( "gfx_sort_life FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_animated_model: world/instance.hxx against retail's draw 0x532AD0, shadow 0x533060 and prefetch 0x5333A0 on seeded fake meshes.
inline bool verify_gfx_animated_model( )
{
    anim_cursor_t g;
    g.w = g_golden_gam_words;
    g.size = g_golden_gam_size;
    g.where = "gfx_animated_model";
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    gfx_check_hooks_t hooks;
    hooks.m_materials.resize( g.take( ) );
    for ( gfx_material_t& m : hooks.m_materials )
        m.m_key = g.take( );
    hooks.m_chunks.resize( g.take( ) );
    for ( gfx_chunk_t& c : hooks.m_chunks )
        c.m_key = g.take( );
    gfx_sort_t sort( hooks );
    sort.m_buffers[ 0 ].m_memory.resize( 0x2C0000 );
    sort.m_buffers[ 0 ].m_capacity = 0x2C0000;
    std::vector< gfx_scene_material_t > rows( g.take( ) );
    gfx_model_map_check_t map;
    for ( gfx_scene_material_t& r : rows )
    {
        r.m_name = g.take( );
        r.m_skip = static_cast< std::uint8_t >( g.take( ) );
        r.m_flag_1d = static_cast< std::uint8_t >( g.take( ) );
        r.m_by_depth = static_cast< std::uint8_t >( g.take( ) );
        r.m_type = g.take( );
        r.m_material = g.take( ) + 1;
        r.m_depth_material = g.take( ) + 1;
        map.m_table.push_back( { r.m_name, g.take( ) + 1 } );
        r.m_casts = static_cast< std::uint8_t >( g.take( ) );
        r.m_shadow_material = g.take( ) + 1;
    }
    int commands = 0;
    const auto compare_commands = [ & ]( )
    {
        const unsigned count = g.take( );
        g.eq( static_cast< unsigned >( sort.m_commands[ 0 ].size( ) ), count );
        commands += static_cast< int >( count );
        for ( unsigned c = 0; c < count; ++c )
        {
            const unsigned words = g.take( );
            if ( c >= sort.m_commands[ 0 ].size( ) )
            {
                for ( unsigned i = 0; i < words; ++i )
                    g.eq( 0xDEADBEEFu, g.take( ) );
                continue;
            }
            const ui_gfx_command_t& cmd = sort.m_commands[ 0 ][ c ];
            for ( unsigned i = 0; i < 7; ++i )
            {
                std::uint32_t w = cmd.m_words[ i ];
                if ( i == 4 || i == 5 )
                    w -= 1;
                if ( i == 6 )
                    w &= 0xFFFFu;
                g.eq( w, g.take( ) );
            }
            const std::uint8_t* r = sort.m_buffers[ 0 ].m_memory.data( ) + cmd.m_words[ 3 ];
            for ( unsigned i = 0; i + 7 < words; ++i )
            {
                std::uint32_t w;
                std::memcpy( &w, r + 4 * i, 4 );
                if ( i == 0x15 )
                    w &= 0xFFu;
                g.eq( w, g.take( ) );
            }
        }
    };
    const auto fresh = [ & ]( )
    {
        sort.m_commands[ 0 ].clear( );
        sort.m_buffers[ 0 ].m_used = 0;
        std::fill( sort.m_buffers[ 0 ].m_memory.begin( ), sort.m_buffers[ 0 ].m_memory.begin( ) + 0x10000, static_cast< std::uint8_t >( 0 ) );
    };
    const unsigned draws = g.take( );
    std::deque< gfx_scene_part_t > parts;
    std::deque< gfx_scene_mesh_t > meshes;
    std::vector< const gfx_scene_mesh_t* > table;
    for ( unsigned d = 0; d < draws; ++d )
    {
        const unsigned count_meshes = g.take( );
        if ( count_meshes )
        {
            parts.clear( );
            meshes.clear( );
            table.clear( );
            for ( unsigned i = 0; i < count_meshes; ++i )
            {
                meshes.emplace_back( );
                gfx_scene_mesh_t& m = meshes.back( );
                for ( float& f : m.m_min )
                    f = real( g.take( ) );
                for ( float& f : m.m_max )
                    f = real( g.take( ) );
                const unsigned n = g.take( );
                for ( unsigned k = 0; k < n; ++k )
                {
                    parts.emplace_back( );
                    gfx_scene_part_t& p = parts.back( );
                    p.m_id = g.take( );
                    p.m_row = &rows[ g.take( ) ];
                    p.m_chunk = g.take( ) + 1;
                    m.m_parts.push_back( &p );
                }
                table.push_back( &m );
            }
        }
        sort.m_key_lo = g.take( );
        sort.m_key_hi = g.take( );
        gfx_animated_model_t model;
        model.m_colour = g.take( );
        model.m_word = g.take( );
        model.m_model_word = g.take( );
        model.m_bones = g.take( );
        model.m_byte = static_cast< std::uint8_t >( g.take( ) );
        model.m_two_pass = static_cast< std::uint8_t >( g.take( ) );
        model.m_map = g.take( ) ? &map : nullptr;
        model.m_meshes = &table;
        gfx_draw_params_t params;
        gfx_camera_t camera;
        params.m_second_depth = static_cast< std::uint8_t >( g.take( ) );
        camera.m_far = real( g.take( ) );
        for ( float& f : camera.m_eye )
            f = real( g.take( ) );
        float parent[ 16 ];
        for ( float& f : parent )
            f = real( g.take( ) );
        params.m_camera = &camera;
        fresh( );
        model.draw( sort, parent, params );
        compare_commands( );
        g.eq( sort.m_key_lo, g.take( ) );
        g.eq( sort.m_key_hi, g.take( ) );
        fresh( );
        model.prefetch( sort );
        compare_commands( );
        fresh( );
        gfx_shadow_params_t shadow;
        std::vector< gfx_shadow_cascade_t > cascades( g.take( ) );
        for ( gfx_shadow_cascade_t& c : cascades )
            for ( float& f : c.m_matrix )
                f = real( g.take( ) );
        shadow.m_cascades = static_cast< int >( cascades.size( ) );
        shadow.m_cascade = cascades.data( );
        model.draw_shadow( sort, parent, shadow );
        compare_commands( );
        g.eq( sort.m_key_lo, g.take( ) );
        g.eq( sort.m_key_hi, g.take( ) );
    }
    if ( g.failed == 0 && g.at == g.size )
    {
        LOG_SUCCESS( "gfx_animated_model matches retail's VuAnimatedModelInstance draw 0x532AD0, shadow 0x533060, prefetch 0x5333A0 on %u draws, %d commands: %d words exact", draws, commands, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_animated_model FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

#include <workspace/utility/checks/device.hxx>
#include <workspace/utility/checks/hudpass.hxx>

inline bool verify_render_all( )
{
    bool ok = true;
    ok = verify_anim_model( ) && ok;
    ok = verify_gfx_sort( ) && ok;
    ok = verify_gfx_cache( ) && ok;
    ok = verify_gfx_dbrt( ) && ok;
    ok = verify_gfx_lights( ) && ok;
    ok = verify_gfx_draw_manager( ) && ok;
    ok = verify_gfx_primitives( ) && ok;
    ok = verify_gfx_static_model( ) && ok;
    ok = verify_gfx_animated_model( ) && ok;
    ok = verify_gfx_real_model( ) && ok;
    ok = verify_gfx_components( ) && ok;
    ok = verify_gfx_corona( ) && ok;
    ok = verify_gfx_corona_entity( ) && ok;
    ok = verify_text_word_wrap( ) && ok;
    ok = verify_gfx_sort_life( ) && ok;
    ok = verify_gfx_device( ) && ok;
    ok = verify_ui_record_draws( ) && ok;
    ok = verify_boost_record( ) && ok;
    ok = verify_mode_switch( ) && ok;
    ok = verify_gfx_program( ) && ok;
    ok = verify_gfx_model_draws( ) && ok;
    ok = verify_water_material( ) && ok;
    ok = verify_gfx_post( ) && ok;
    ok = verify_gfx_composer( ) && ok;
    ok = verify_gfx_buffers( ) && ok;
    ok = verify_gfx_backend( ) && ok;
    ok = verify_gfx_quads( ) && ok;
    ok = verify_gfx_texture_load( ) && ok;
#if HYDRO_HOST_WINDOWS
    ok = verify_gfx_direct( ) && ok;
#endif
    return ok;
}
