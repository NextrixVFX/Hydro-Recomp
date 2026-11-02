#pragma once

#include <workspace/core/assets/archive.hxx>
#include <workspace/core/boat/fleet.hxx>
#include <workspace/core/world/pfxload.hxx>
#include <workspace/core/world/pfxmgr.hxx>
#include <workspace/core/world/pfxsim.hxx>
#include <workspace/core/world/pfxstatic.hxx>
#include <workspace/utility/logger.hxx>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

// The world's VuPfx (world batch 12): the water agent's verified runtime (world/pfxsim.hxx, pfxload.hxx, pfxmgr.hxx)
// driven the way the race drives it, replacing the old c_pfx simulation.
//
//   The effect assets: VuPfxAsset's load 0x5CAB30 (pfx_load_asset) - each asset a static pfx entity names (the
//     first segment of its Effect Name), loaded once when first named. Which projects a race loads, and when, is
//     the asset system's (unread): OURS, flagged - loading the named one first is what makes the lookup answer.
//   VuStaticPfxEntity's onGameInitialize 0x5EB060: the "Build" tick registered (VuMethod1 0x5EB290); the system
//     from g_vupfximpl's createSystemInstance 0x51DA70 (vt+0x28: pfx_runtime_create_path) on the Effect Name (+0x70)
//     -> +0x94; when there is one: slot 4 setScale(+0x8C), slot 5 setColor(the four colour bytes +0x90 .. +0x93,
//     each cvtdq2ps x 0.0039215689: static_pfx_color), slot 2 setMatrix(the transform's +0x50 world); then, iff
//     Initially Active (+0x88), slot 11 start. (The transform watcher 0x5EB4A0 hands slot 2 the world again: our
//     static pfx entities do not move.)
//   Its Build tick 0x5EB290: slot 13 tick(dt) (pfx_runtime_frame); then slot 8 (the live count +0xA0): non-zero ->
//     the draw component (+0x60) added to the 3D draw manager once (+0x1E set) and its AABB (+0x24 .. +0x43) the
//     system's slot 7 eight words; zero -> removed (+0x1E clear). The draw manager is the platform's (a hook): here
//     a system is drawn while its component is added.
//   The "Final" phase: VuPfxManager's tick 0x52BBF0 (pfx_manager_tick), then VuPfxImpl's 0x51DD80 (pfx_impl_tick).
//
// THE DRAW (ours - the renderer, flagged): retail's quad / trail renderers 0x58BD60 / 0x58D030 build GPU batches
// (hooks). A system drawn here walks its pattern instances (+0x10, next +4; the node +0x10, its class by node vtable)
// and places, per particle of the list (+0x20, next +0): a QUAD as the camera-facing card the old c_pfx drew - the
// particle's position (+0x08; through the system matrix +0x1C when the pattern's Space +0x34 is Object, the rule
// 0x5906A0 applies to geom), scale (+0x38) x the instance scale (+0xA8), rotation (+0x44), World Scale Z (+0x4C),
// tile offsets (+0x54 / +0x58), colour (+0x28 .. +0x34) x the system tint (+0xAC); the pattern's Texture / Tile
// Texture (+0x3C / +0x54), Near Fade Min / Max (+0x78 / +0x7C), Tile Scroll U / V (+0x80 / +0x84), Tile Scale
// (+0x8C). A GEOM particle as its model (+0x3C) at pfx_geom_draw_matrix with pfx_geom_draw_colour (0x5906A0 /
// 0x5903A0, verified). TRAIL patterns are not drawn: counted, logged once. READ (world batch 13): the trail
// renderer 0x58D030 (from the pattern's slot 2 0x58F9C0 while +0x28 is non-zero) only PACKS - with >= 2 particles
// (+0x28) - a sort-command payload: 108 header bytes (the renderer, the node, the system matrix +0x1C when Space
// +0x34 is Object else the identity 0x7593B0, the pattern's +0x2C .. +0x4B box, min(count, 5461)) and per particle
// 44 bytes (position +0x08 16 b, +0x44 / +0x4C 16 b, +0x54, the colour +0x28 .. +0x34 x the system tint +0xAC ..
// x 255 as four bytes, scale +0x38 x 0.5), sorted by the eye distance to the box centre / [+0x17C] (minss 1.0).
// The ribbon GEOMETRY is built by the draw callback 0x58CE30 the command names - NOT READ: the trails stay
// undrawn until it is.
// VuRand: retail's emitters draw from THE global VuRandom 0x736810, the one the AI plans and the front end's roster
// picks draw from (core/boat fleet.hxx) - the engine init 0x566230 seeds it from the platform clock (c_world owns it,
// bind_random). The runtime keeps its own copy of the 34 words (pfx_runtime_t::rand, the verified emitters' operand),
// so every runtime call here loads the shared state in and stores it back out: one sequence, as retail's one global.

class c_effects
{
public:

    void clear( )
    {
        m_rt = pfx_runtime_t{ };
        pfx_runtime_init( m_rt );
        m_lib = pfx_library_t{ };
        m_binding = pfx_manager_binding_t{ &m_rt, &m_lib };
        m_ops = pfx_manager_runtime_ops( m_binding );
        m_manager = pfx_manager_t{ };
        pfx_manager_init( m_manager, m_ops );
        m_assets.clear( );
        m_statics.clear( );
        m_trails_logged = false;
        m_frames = 0;
        m_unrecreated = 0;
        m_ready = true;
    }

    // The shared VuRandom 0x736810 (see the top); null -> the runtime's draws are UNKNOWN and nothing runs.
    void bind_random( fleet_random_t* random )
    {
        m_random = random;
    }

    // VuStaticPfxEntity's onGameInitialize 0x5EB060. Returns the system instance (0: none).
    std::uint32_t add_static( const c_archive& archive, const std::string& effect, const float* world, float scale,
                              const float* color, bool active )
    {
        if ( !m_ready )
            clear( );
        if ( m_random == nullptr )
        {
            LOG_WARNING( "pfx %s: no VuRandom 0x736810 bound - not created", effect.c_str( ) );
            return 0u;
        }
        shared_t shared( *this );
        const std::size_t slash = effect.find( '/' );
        const std::string asset = effect.substr( 0, slash );
        if ( m_assets.insert( asset ).second )
        {
            std::vector< std::uint8_t > blob;
            if ( !archive.read( ( "VuPfxAsset/" + asset ).c_str( ), blob ) || pfx_load_asset( m_rt, m_lib, asset, blob ) == 0u )
                LOG_WARNING( "pfx: VuPfxAsset/%s did not load", asset.c_str( ) );
        }
        const std::uint32_t inst = pfx_runtime_create_path( m_rt, m_lib, effect );
        if ( inst == 0u )
        {
            LOG_VERBOSE( "pfx %s: createSystemInstance 0x51DA70 answered none", effect.c_str( ) );
            return 0u;
        }
        std::uint32_t* w = m_rt.heap.find( inst );
        std::uint32_t bits[ 16 ];
        std::memcpy( bits, &scale, 4 );
        pfx_system_set_scale( w, bits[ 0 ] );
        std::memcpy( bits, color, 16 );
        pfx_system_set_wordsac( w, bits );
        std::memcpy( bits, world, 64 );
        pfx_system_set_matrix( w, bits );
        if ( active )
            pfx_system_start( m_rt.heap, inst );
        static_t s;
        s.instance = inst;
        s.effect = effect;
        m_statics.push_back( s );
        return inst;
    }

    // The system's vt+8 setMatrix (VuStaticPfxEntity's motion IF 0x5EB4C0 with the motion's matrix).
    void set_matrix( std::uint32_t instance, const float* world )
    {
        if ( !m_ready || instance == 0u )
            return;
        std::uint32_t* w = m_rt.heap.find( instance );
        if ( w == nullptr )
            return;
        std::uint32_t bits[ 16 ];
        std::memcpy( bits, world, 64 );
        pfx_system_set_matrix( w, bits );
    }

    int systems( ) const
    {
        return static_cast< int >( m_statics.size( ) );
    }

    // VuStaticPfxEntity's three input plugs on the entity's +0x94 instance (world/pfxstatic.hxx, the water agent's
    // verified handlers; 0 = none -> nothing, as retail's guard): Start 0x5EB200 = slot 11 start; Stop 0x5EB220 =
    // slot 12 stop(not immediate); Kill 0x5EB250 = slot 12 stop(immediate), then the draw component removed from the
    // 3D draw manager (+0x1E cleared) - here: not drawn until the next Build tick re-adds it.
    void plug( std::uint32_t instance, int which )
    {
        if ( !m_ready || m_random == nullptr )
            return;
        static_t* s = nullptr;
        for ( static_t& each : m_statics )
            if ( each.instance == instance )
                s = &each;
        if ( s == nullptr )
            return;
        shared_t shared( *this );
        pfx_static_entity_t e;
        e.instance = s->instance;
        e.drawn = s->drawn ? 1 : 0;
        pfx_static_ops_t o = pfx_static_runtime_ops( m_binding );
        o.draw_remove = []( void*, void* ) { };   // the 3D draw manager: the platform's (a hook); +0x1E is s->drawn
        if ( which == 0 )
            pfx_static_start( e, o );
        else if ( which == 1 )
            pfx_static_stop( e, o );
        else
            pfx_static_kill( e, o );
        s->drawn = e.drawn != 0;
    }

    // One frame: every static pfx's Build tick 0x5EB290, then the "Final" phase.
    void frame( float dt, const float* eye )
    {
        if ( !m_ready || m_random == nullptr )
            return;
        shared_t shared( *this );
        std::memcpy( m_camera, eye, sizeof( m_camera ) );
        for ( static_t& s : m_statics )
        {
            m_unrecreated += pfx_runtime_frame( m_rt, s.instance, dt, m_camera, 1 );
            s.drawn = m_rt.heap[ s.instance + 0xA0 ] != 0u;
        }
        pfx_manager_tick( m_manager, dt, m_ops );
        pfx_impl_tick( m_rt.heap, m_rt.resources );
        // Once, two seconds in: what the runtime holds (a measurement, not retail's).
        if ( ++m_frames == 120 )
        {
            int drawing = 0, active = 0;
            std::uint32_t particles = 0;
            for ( const static_t& s : m_statics )
            {
                drawing += s.drawn ? 1 : 0;
                active += m_rt.heap[ s.instance + 0x7C ] != 0u ? 1 : 0;
                particles += m_rt.heap[ s.instance + 0xA0 ];
            }
            LOG_INFO( "pfx: %d systems created, %d not idle, %d drawing, %u live particles; %d unrecreated paths reached so far",
                      static_cast< int >( m_statics.size( ) ), active, drawing, particles, m_unrecreated );            for ( const static_t& s : m_statics )
                if ( s.drawn )
                    LOG_VERBOSE( "pfx: drawing %s at %.0f %.0f %.0f (%u particles)", s.effect.c_str( ), m_rt.heap.f( s.instance + 0x4C ),
                                 m_rt.heap.f( s.instance + 0x50 ), m_rt.heap.f( s.instance + 0x54 ), m_rt.heap[ s.instance + 0xA0 ] );
        }
    }

    // The draw (ours - see the top), into placement group 11 (dropped first, as the old c_pfx did). Texture: path ->
    // texture id; Model: path -> the model's parts (c_world::pfx_parts).
    template < typename Texture, typename Model >
    void draw( const float* eye, const float* target, Texture&& texture, Model&& model, const std::vector< std::uint32_t >* systems = nullptr )
    {
        c_engine::drop( 11 );
        c_engine::drop( 14 );
        if ( !m_ready || eye == nullptr || target == nullptr )
            return;
        if ( m_card < 0 )
            m_card = make_card( );
        const int card = m_card;
        float right[ 3 ], up[ 3 ];
        facing( eye, target, right, up );
        int trails = 0;
        auto draw_system = [ & ]( std::uint32_t instance ) {
            for ( std::uint32_t pattern = m_rt.heap[ instance + 0x10 ]; pattern != 0u; pattern = m_rt.heap[ pattern + 4 ] )
            {
                const std::uint32_t node = m_rt.heap[ pattern + 0x10 ];
                const std::uint32_t vt = m_rt.heap[ node ];
                if ( vt == 0x007505ACu )
                    draw_quads( instance, pattern, node, card, right, up, texture );
                else if ( vt == 0x00750328u )
                    draw_geoms( instance, pattern, node, model );
                else if ( vt == 0x00750294u && m_rt.heap[ pattern + 0x28 ] != 0u )
                    ++trails;
            }
        };
        for ( const static_t& s : m_statics )
            if ( s.drawn )
                draw_system( s.instance );
        // The race boats' systems (0x419CE0's order: world/session.hxx race_session_pfx_draw): slot 14 0x58AF60 has no gate on the system.
        if ( systems != nullptr )
            for ( std::uint32_t instance : *systems )
                draw_system( instance );
        // VuPfxManager's entries (power-ups' StaticPfx, the hull's one-shots): each VuPfxEntity's draw component, added to the draw manager while +0x1E is set
        // (0x52BC30, pfx_manager_entry_t::registered). A system the list above already drew is not drawn twice.
        for ( int e = m_manager.active_first; e >= 0; e = m_manager.entries[ static_cast< std::size_t >( e ) ].next )
        {
            const pfx_manager_entry_t& x = m_manager.entries[ static_cast< std::size_t >( e ) ];
            if ( x.registered && ( systems == nullptr || std::find( systems->begin( ), systems->end( ), x.instance ) == systems->end( ) ) )
                draw_system( x.instance );
        }
        if ( trails > 0 && !m_trails_logged )
        {
            m_trails_logged = true;
            LOG_WARNING( "pfx: %d trail pattern(s) with particles are not drawn (the trail renderer 0x58D030 has no draw here)", trails );
        }
    }

    // The runtime / VuPfxManager the race's boats reach (the Build tick's pfx, the hull's splash).
    pfx_runtime_t* runtime( ) { return m_ready ? &m_rt : nullptr; }
    pfx_manager_t* manager( ) { return m_ready ? &m_manager : nullptr; }
    const pfx_manager_ops_t* manager_ops( ) const { return m_ready ? &m_ops : nullptr; }

private:
    // The shared VuRandom in for one runtime call, back out after it (see the top).
    struct shared_t
    {
        c_effects& e;
        explicit shared_t( c_effects& owner ) : e( owner )
        {
            e.m_rt.rand.seed = e.m_random->seed;
            e.m_rt.rand.previous = e.m_random->last;
            std::memcpy( e.m_rt.rand.table, e.m_random->table, sizeof( e.m_rt.rand.table ) );
        }
        ~shared_t( )
        {
            e.m_random->seed = e.m_rt.rand.seed;
            e.m_random->last = e.m_rt.rand.previous;
            std::memcpy( e.m_random->table, e.m_rt.rand.table, sizeof( e.m_random->table ) );
        }
    };

    struct static_t
    {
        std::uint32_t instance = 0;
        bool drawn = false;   // the draw component's +0x1E
        std::string effect;
    };

    pfx_quad_particle_t particle( std::uint32_t at )
    {
        pfx_quad_particle_t q;
        for ( int k = 0; k < 30; ++k )
            q.w[ k ] = m_rt.heap[ at + 4u * static_cast< std::uint32_t >( k ) ];
        return q;
    }

    const float* system_matrix( std::uint32_t inst, float* m )
    {
        for ( int k = 0; k < 16; ++k )
            m[ k ] = m_rt.heap.f( inst + 0x1C + 4u * static_cast< std::uint32_t >( k ) );
        return m;
    }

    const std::string& text( std::uint32_t node, std::uint32_t offset ) const
    {
        static const std::string none;
        const auto it = m_lib.strings.find( { node, offset } );
        return it == m_lib.strings.end( ) ? none : it->second;
    }

    template < typename Texture >
    void draw_quads( std::uint32_t inst, std::uint32_t pattern, std::uint32_t node, int card, const float* right, const float* up, Texture& texture )
    {
        const int soft = texture( text( node, 0x3C ) );
        const std::string& tile_name = text( node, 0x54 );
        const int tile = tile_name.empty( ) ? -1 : texture( tile_name );
        const float surface[ 4 ] = { m_rt.heap.f( node + 0x78 ), m_rt.heap.f( node + 0x7C ), m_rt.heap.f( node + 0x80 ), m_rt.heap.f( node + 0x8C ) };
        const float scroll_v = m_rt.heap.f( node + 0x84 );
        const bool object = m_rt.heap[ node + 0x34 ] == 1u;
        float m[ 16 ];
        system_matrix( inst, m );
        const float instance_scale = m_rt.heap.f( inst + 0xA8 );
        float tint[ 4 ];
        for ( int k = 0; k < 4; ++k )
            tint[ k ] = m_rt.heap.f( inst + 0xAC + 4u * static_cast< std::uint32_t >( k ) );
        for ( std::uint32_t p = m_rt.heap[ pattern + 0x20 ]; p != 0u; p = m_rt.heap[ p ] )
        {
            const pfx_quad_particle_t q = particle( p );
            const float scale = q.f( 0x38 ) * instance_scale;
            const float color[ 4 ] = { q.f( 0x28 ) * tint[ 0 ], q.f( 0x2C ) * tint[ 1 ], q.f( 0x30 ) * tint[ 2 ], q.f( 0x34 ) * tint[ 3 ] };
            if ( color[ 3 ] <= 0.0f || scale <= 0.0f )
                continue;
            float position[ 3 ] = { q.f( 0x08 ), q.f( 0x0C ), q.f( 0x10 ) };
            if ( object )
            {
                const float local[ 3 ] = { position[ 0 ], position[ 1 ], position[ 2 ] };
                for ( int i = 0; i < 3; ++i )
                    position[ i ] = m[ i ] * local[ 0 ] + m[ 4 + i ] * local[ 1 ] + m[ 8 + i ] * local[ 2 ] + m[ 12 + i ];
            }
            const float rotation = q.f( 0x44 );
            const float world_z = q.f( 0x4C );
            const float cosine = std::cos( rotation ), sine = std::sin( rotation );
            const float flatten = world_z > 0.0f ? world_z : 1.0f;
            float placed[ 16 ]{};
            placed[ 15 ] = 1.0f;
            placed[ 10 ] = 1.0f;
            for ( int i = 0; i < 3; ++i )
            {
                placed[ i ] = ( right[ i ] * cosine + up[ i ] * sine ) * scale;
                placed[ 4 + i ] = ( up[ i ] * cosine - right[ i ] * sine ) * scale * flatten;
            }
            placed[ 8 ] = placed[ 9 ] = 0.0f;
            placed[ 12 ] = position[ 0 ];
            placed[ 13 ] = position[ 1 ];
            placed[ 14 ] = position[ 2 ];
            const float bias[ 2 ] = { q.f( 0x54 ), q.f( 0x58 ) };
            c_engine::place( card, soft, 0, 6, placed, color, 11, surface, tile, scroll_v, bias );
        }
    }

    template < typename Model >
    void draw_geoms( std::uint32_t inst, std::uint32_t pattern, std::uint32_t node, Model& model )
    {
        const std::string& name = text( node, 0x3C );
        if ( name.empty( ) )
            return;
        const auto& parts = model( name );
        if ( parts.empty( ) )
            return;
        static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        float m[ 16 ];
        const float* d = m_rt.heap[ node + 0x34 ] == 1u ? system_matrix( inst, m ) : identity;
        const float instance_scale = m_rt.heap.f( inst + 0xA8 );
        float tint[ 4 ];
        for ( int k = 0; k < 4; ++k )
            tint[ k ] = m_rt.heap.f( inst + 0xAC + 4u * static_cast< std::uint32_t >( k ) );
        for ( std::uint32_t p = m_rt.heap[ pattern + 0x20 ]; p != 0u; p = m_rt.heap[ p ] )
        {
            const pfx_quad_particle_t q = particle( p );
            float placed[ 16 ];
            pfx_geom_draw_matrix( q, d, instance_scale, placed );
            const std::uint32_t c = pfx_geom_draw_colour( q, tint );
            const float color[ 4 ] = { static_cast< float >( c & 0xFFu ) / 255.0f, static_cast< float >( ( c >> 8 ) & 0xFFu ) / 255.0f,
                                       static_cast< float >( ( c >> 16 ) & 0xFFu ) / 255.0f, static_cast< float >( c >> 24 ) / 255.0f };
            for ( const auto& part : parts )
            {
                if ( part.geometry < 0 )
                    continue;
                float placed_node[ 16 ];
                math_multiply( placed_node, placed, part.node );
                // 0x5903A0's tail is the model instance's draw 0x520130: the model's own material, lit, with the particle's colour. 14: a lit
                // placement this class drops each frame (the pfx quad's 11 is the unlit branch: user item 1, the windmill rotors).
                c_engine::place( part.geometry, part.texture, part.index_start, part.index_count, placed_node, color, 14, nullptr, part.detail );
            }
        }
    }

    // The old c_pfx's unit card (the renderer's, ours): a 1 x 1 quad about its centre, uv flipped in v.
    static int make_card( )
    {
        mesh_vertex_t vertices[ 4 ]{};
        const float corner[ 4 ][ 2 ] = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
        const float uv[ 4 ][ 2 ] = { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } };
        for ( int index = 0; index < 4; ++index )
        {
            vertices[ index ].position[ 0 ] = corner[ index ][ 0 ];
            vertices[ index ].position[ 1 ] = corner[ index ][ 1 ];
            vertices[ index ].normal[ 2 ] = 1.0f;
            vertices[ index ].uv[ 0 ] = uv[ index ][ 0 ];
            vertices[ index ].uv[ 1 ] = uv[ index ][ 1 ];
            for ( int c = 0; c < 4; ++c )
                vertices[ index ].color[ c ] = 1.0f;
        }
        const unsigned short indices[ 6 ] = { 0, 1, 2, 0, 2, 3 };
        return c_engine::geometry( vertices, 4, indices, 6 );
    }

    // The old c_pfx's camera-facing basis (the renderer's, ours).
    static void facing( const float* eye, const float* target, float* right, float* up )
    {
        float forward[ 3 ] = { target[ 0 ] - eye[ 0 ], target[ 1 ] - eye[ 1 ], target[ 2 ] - eye[ 2 ] };
        math_normalize( forward );
        const float world_up[ 3 ] = { 0.0f, 0.0f, 1.0f };
        math_cross( right, world_up, forward );
        math_normalize( right );
        math_cross( up, forward, right );
        for ( int i = 0; i < 3; ++i )
            right[ i ] = -right[ i ];
    }

    bool m_ready = false;
    fleet_random_t* m_random = nullptr;   // VuRandom 0x736810 (c_world's)
    int m_card = -1;
    bool m_trails_logged = false;
    pfx_runtime_t m_rt;
    pfx_library_t m_lib;
    pfx_manager_binding_t m_binding;
    pfx_manager_ops_t m_ops;
    pfx_manager_t m_manager;
    std::set< std::string > m_assets;
    std::vector< static_t > m_statics;
    float m_camera[ 3 ]{};
    int m_unrecreated = 0;
    int m_frames = 0;
};
