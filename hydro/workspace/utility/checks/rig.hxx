#pragma once

// World batch 33: checks on the boat's animated model (world/rig.hxx). Included by checks/world.hxx.

#include <workspace/core/world/attach.hxx>
#include <workspace/core/world/rig.hxx>
#include <workspace/core/world/latch.hxx>
#include <workspace/core/world/rotate.hxx>

inline std::uint32_t rig_fnv( const void* data, std::size_t size )
{
    const std::uint8_t* b = static_cast< const std::uint8_t* >( data );
    std::uint32_t h = 0x811C9DC5u;
    for ( std::size_t i = 0; i < size; ++i )
        h = ( h ^ b[ i ] ) * 0x01000193u;
    return h;
}

// Case boat_rig: 0x410610's three controls, findNodeByName 0x52F3C0 on the boat's Pfx bones, and the Decision tick's 0x40FFF0 +
// the Build tick's step 4 (0x5169E0, 0x516CF0, 0x532720, the two flips) per step, on eight shipped boats, against retail's own code.
inline bool verify_boat_rig( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "boat_rig: Base.apf did not open" );
        return false;
    }
    const unsigned* g = g_golden_brg_words;
    std::size_t at = 0;
    int failed = 0, compared = 0, total_steps = 0, total_finds = 0;
    const char* where = "";
    auto same = [ & ]( const char* what, int index, std::uint32_t mine ) {
        const std::uint32_t r = g[ at++ ];
        ++compared;
        if ( r != mine )
        {
            if ( failed < 8 )
                LOG_ERROR( "boat_rig %s: %s[%d] 0x%08X, retail 0x%08X", where, what, index, mine, r );
            ++failed;
        }
    };
    auto bits = []( float f ) {
        std::uint32_t b;
        std::memcpy( &b, &f, 4 );
        return b;
    };
    auto real = []( unsigned b ) {
        float f;
        std::memcpy( &f, &b, 4 );
        return f;
    };
    const unsigned boats = g[ at++ ];
    for ( unsigned bi = 0; bi < boats; ++bi )
    {
        where = g_golden_brg_names[ g[ at++ ] ];
        const char* model = g_golden_brg_names[ g[ at++ ] ];
        const unsigned count = g[ at++ ];
        const unsigned animated = g[ at++ ];
        std::string names[ 3 ];
        for ( std::string& n : names )
            n = g_golden_brg_names[ g[ at++ ] ];
        std::uint8_t anim[ 0xE0 ]{ };
        for ( int k = 0; k < 6; ++k )
            image_put32( anim, 0x5C + 4 * k, g[ at++ ] );
        std::vector< std::uint8_t > blob;
        boat_rig_t rig;
        if ( !archive.read( ( std::string( "VuAnimatedModelAsset/" ) + model ).c_str( ), blob ) || !boat_rig_model( rig, blob.data( ), blob.size( ) ) )
        {
            LOG_ERROR( "boat_rig: %s did not load", model );
            return false;
        }
        boat_rig_create( rig, names, [ & ]( const std::string& name, std::vector< std::uint8_t >& out ) {
            return !name.empty( ) && archive.read( ( "VuAnimationAsset/" + name ).c_str( ), out );
        } );
        if ( static_cast< unsigned >( rig.model->asset.skeleton.count ) != count )
        {
            LOG_ERROR( "boat_rig %s: %d bones, retail %u", where, rig.model->asset.skeleton.count, count );
            return false;
        }
        if ( ( rig.animated != nullptr ) != ( animated != 0 ) )
        {
            LOG_ERROR( "boat_rig %s: animated skeleton made %d, retail %u", where, rig.animated != nullptr, animated );
            return false;
        }
        same( "controls", 0, static_cast< std::uint32_t >( rig.animated->controls.size( ) ) );
        same( "blended", 0, static_cast< std::uint32_t >( rig.animated->blended.size( ) ) );
        same( "additive", 0, static_cast< std::uint32_t >( rig.animated->additive.size( ) ) );
        for ( int k = 0; k < 3; ++k )
        {
            const skel_control_t& c = rig.control[ k ];
            same( "present", k, rig.has[ k ] ? 1u : 0u );
            same( "looping", k, rig.has[ k ] && c.looping ? 1u : 0u );
            same( "factor", k, rig.has[ k ] ? bits( c.factor ) : 0u );
            same( "time", k, rig.has[ k ] ? bits( c.time ) : 0u );
            same( "weight", k, rig.has[ k ] ? bits( c.weight ) : 0u );
            same( "events", k, 0u );
            same( "listener", k, 0u );
        }
        const unsigned finds = g[ at++ ];
        for ( unsigned k = 0; k < finds; ++k )
        {
            const char* probe = g_golden_brg_names[ g[ at++ ] ];
            same( "find", static_cast< int >( k ), static_cast< std::uint32_t >( skel_instance_find_node( rig.model->instance, probe ) ) );
            ++total_finds;
        }
        const unsigned steps = g[ at++ ];
        std::uint8_t boat[ 0x520 ]{ };
        std::uint8_t engine[ 0x100 ]{ };
        const float position[ 4 ] = { }, velocity[ 4 ] = { };
        int voices = 0;
        for ( unsigned s = 0; s < steps; ++s )
        {
            const float dt = real( g[ at++ ] );
            boat_rig_inputs_t in;
            in.anim = anim;
            in.boat = boat;
            in.throttle = real( g[ at++ ] );
            in.steer = real( g[ at++ ] );
            image_put32( boat, 0x310, g[ at++ ] );
            boat[ 0x32F ] = static_cast< std::uint8_t >( g[ at++ ] );
            in.position = position;
            in.velocity = velocity;
            in.engine = engine;
            voices = 0;
            in.voice_user = &voices;
            in.voice = []( void* u, const char* ) { ++*static_cast< int* >( u ); };
            boat_rig_animation_tick( rig, in, dt );
            const skel_instance_t& i = rig.model->instance;
            const int current = i.current;
            if ( !boat_rig_pose( rig, dt ) )
            {
                LOG_ERROR( "boat_rig %s: step 4 refused at step %u", where, s );
                return false;
            }
            same( "state", static_cast< int >( s ), load_word( anim, 0x78 ) );
            same( "voices", static_cast< int >( s ), static_cast< std::uint32_t >( voices ) );
            for ( int k = 0; k < 3; ++k )
            {
                same( "step factor", static_cast< int >( s ), rig.has[ k ] ? bits( rig.control[ k ].factor ) : 0u );
                same( "step time", static_cast< int >( s ), rig.has[ k ] ? bits( rig.control[ k ].time ) : 0u );
            }
            same( "current", static_cast< int >( s ), static_cast< std::uint32_t >( current ) );
            same( "current after", static_cast< int >( s ), static_cast< std::uint32_t >( i.current ) );
            same( "word28", static_cast< int >( s ), static_cast< std::uint32_t >( i.word28 ) );
            same( "matrices", static_cast< int >( s ), rig_fnv( i.matrices.data( ), 64u * count ) );
            same( "palette", static_cast< int >( s ), rig_fnv( i.palette[ current & 1 ].data( ), 64u * count ) );
            ++at;   // the local pose's digest: its position / scale w lanes are UNKNOWN (checks/render.hxx poses); the matrices carry it
            ++total_steps;
        }
        for ( unsigned k = 0; k < 16u * count; ++k )
            same( "last matrices", static_cast< int >( k ), bits( rig.model->instance.matrices[ k ] ) );
    }
    if ( at != g_golden_brg_size )
    {
        LOG_ERROR( "boat_rig: %zu words read of %u", at, g_golden_brg_size );
        return false;
    }
    if ( failed )
    {
        LOG_ERROR( "boat_rig: %d of %d values differ", failed, compared );
        return false;
    }
    LOG_SUCCESS( "The boat's rig (0x410610, 0x40FFF0, Build step 4, 0x52F3C0 on the Pfx bones) on %u shipped boats, %d steps, %d bone names: "
                 "%d values bit-exact against retail's own code", boats, total_steps, total_finds, compared );
    return true;
}

// Case attach_components: VuOffsetAttachComponent 0x5AF7A0 / 0x5AF830 / 0x5AF900 and VuAnimatedAttachComponent 0x61DEA0 / 0x61DF50 /
// 0x61E010 on fake owners, seeded sequences, against retail: the return, the records' order and nodes, the owners' calls in order,
// every motion's words.
inline bool verify_attach_components( )
{
    const unsigned* g = g_golden_atc_words;
    std::size_t at = 0;
    int failed = 0, compared = 0, total = 0;
    int seq = 0, opi = 0;
    auto same = [ & ]( const char* what, std::uint32_t mine ) {
        const std::uint32_t r = g[ at++ ];
        ++compared;
        if ( r != mine )
        {
            if ( failed < 8 )
                LOG_ERROR( "attach_components seq %d op %d: %s 0x%08X, retail 0x%08X", seq, opi, what, mine, r );
            ++failed;
        }
    };
    const unsigned sequences = g[ at++ ];
    for ( seq = 0; seq < static_cast< int >( sequences ); ++seq )
    {
        const bool animated = g[ at++ ] != 0;
        const unsigned bone_words = g[ at++ ];
        std::vector< float > bones( bone_words );
        if ( bone_words )
            std::memcpy( bones.data( ), g + at, 4 * bone_words );
        at += bone_words;
        struct log_t
        {
            std::vector< std::pair< int, int > > calls;
        } log;
        struct owner_t
        {
            log_t* log;
            int index;
        } owners[ 6 ];
        attach_component_t c;
        int entity = 0;
        c.entity = &entity;
        c.bones = bones.data( );
        attach_motion_t motions[ 6 ];
        for ( int k = 0; k < 6; ++k )
        {
            owners[ k ] = { &log, k };
            motions[ k ].user = &owners[ k ];
            motions[ k ].updated = []( void* u ) { owner_t& o = *static_cast< owner_t* >( u ); o.log->calls.push_back( { 0, o.index } ); };
            motions[ k ].attached = []( void* u ) { owner_t& o = *static_cast< owner_t* >( u ); o.log->calls.push_back( { 1, o.index } ); };
            motions[ k ].detached = []( void* u ) { owner_t& o = *static_cast< owner_t* >( u ); o.log->calls.push_back( { 2, o.index } ); };
        }
        const unsigned ops = g[ at++ ];
        for ( opi = 0; opi < static_cast< int >( ops ); ++opi )
        {
            log.calls.clear( );
            const unsigned kind = g[ at++ ];
            if ( kind == 0 )
            {
                const unsigned m = g[ at++ ];
                const std::int32_t node = static_cast< std::int32_t >( g[ at++ ] );
                const unsigned ret = g[ at++ ];
                float relative[ 16 ];
                std::memcpy( relative, g + at, 64 );
                at += 16;
                const bool ok = animated ? attach_animated_attach( c, motions[ m ], relative, node ) : attach_offset_attach( c, motions[ m ], relative );
                ++compared;
                if ( ok != ( ret != 0 ) )
                    ++failed;
            }
            else if ( kind == 1 )
                attach_detach( c, motions[ g[ at++ ] ] );
            else
            {
                float world[ 16 ], linear[ 4 ], angular[ 4 ];
                std::memcpy( world, g + at, 64 );
                std::memcpy( linear, g + at + 16, 16 );
                std::memcpy( angular, g + at + 20, 16 );
                at += 24;
                if ( animated )
                    attach_animated_update( c, world, linear, angular );
                else
                    attach_offset_update( c, world, linear, angular );
            }
            same( "records", static_cast< std::uint32_t >( c.records.size( ) ) );
            for ( const attach_record_t& r : c.records )
            {
                same( "record motion", static_cast< std::uint32_t >( r.motion - motions ) );
                same( "record node", static_cast< std::uint32_t >( r.node ) );
            }
            same( "calls", static_cast< std::uint32_t >( log.calls.size( ) ) );
            for ( const auto& e : log.calls )
            {
                same( "call slot", static_cast< std::uint32_t >( e.first ) );
                same( "call motion", static_cast< std::uint32_t >( e.second ) );
            }
            for ( const attach_motion_t& m : motions )
            {
                same( "parent", m.parent != nullptr ? 1u : 0u );
                const std::uint32_t* w = reinterpret_cast< const std::uint32_t* >( m.matrix );
                for ( int k = 0; k < 16; ++k )
                    same( "matrix", w[ k ] );
                w = reinterpret_cast< const std::uint32_t* >( m.linear );
                for ( int k = 0; k < 3; ++k )
                    same( "linear", w[ k ] );
                w = reinterpret_cast< const std::uint32_t* >( m.angular );
                for ( int k = 0; k < ( animated ? 3 : 4 ); ++k )
                    same( "angular", w[ k ] );
            }
            ++total;
        }
    }
    if ( at != g_golden_atc_size )
    {
        LOG_ERROR( "attach_components: %zu words read of %u", at, g_golden_atc_size );
        return false;
    }
    if ( failed )
    {
        LOG_ERROR( "attach_components: %d of %d values differ", failed, compared );
        return false;
    }
    LOG_SUCCESS( "The attach components (offset 0x5AF7A0 / 0x5AF830 / 0x5AF900, animated 0x61DEA0 / 0x61DF50 / 0x61E010): %d ops of %u sequences, "
                 "%d values bit-exact against retail's own code", total, sequences, compared );
    return true;
}

// Case attach_entity: VuAttachEntity's slot 6 0x5F54F0, slot 7 0x5F5520, plugs 0x5F5550 / 0x5F5690 (resolve 0x5F56D0, finds 0x5F57E0 /
// 0x5F5810) on fake parents and children: the components' slots in order with their arguments.
inline bool verify_attach_entity( )
{
    const unsigned* g = g_golden_ate_words;
    std::size_t at = 0;
    int failed = 0, compared = 0;
    int vec = 0;
    auto same = [ & ]( const char* what, std::uint32_t mine, std::uint32_t r ) {
        ++compared;
        if ( r != mine )
        {
            if ( failed < 8 )
                LOG_ERROR( "attach_entity vector %d: %s 0x%08X, retail 0x%08X", vec, what, mine, r );
            ++failed;
        }
    };
    const unsigned vectors = g[ at++ ];
    struct log_t
    {
        std::vector< std::uint32_t > words;
        std::vector< int > attach, motion;
        bool has_transform = false;
    };
    using hooks_t = attach_entity_hooks_t< const int, const int, const int >;
    for ( vec = 0; vec < static_cast< int >( vectors ); ++vec )
    {
        const unsigned which = g[ at++ ], at_start = g[ at++ ];
        float position[ 3 ], rotation[ 3 ];
        std::memcpy( position, g + at, 12 );
        std::memcpy( rotation, g + at + 3, 12 );
        at += 6;
        const unsigned parent_link = g[ at++ ], child_link = g[ at++ ];
        log_t log;
        log.has_transform = g[ at++ ] != 0;
        // The two component lists: count, then per component the chain (count, descriptors).
        std::vector< std::vector< std::uint32_t > > lists[ 2 ];
        for ( int side = 0; side < 2; ++side )
        {
            const unsigned n = g[ at++ ];
            for ( unsigned k = 0; k < n; ++k )
            {
                const unsigned d = g[ at++ ];
                lists[ side ].emplace_back( g + at, g + at + d );
                at += d;
            }
        }
        const unsigned name_size = g[ at++ ];
        std::string node;
        for ( unsigned k = 0; k < name_size; ++k )
            node.push_back( static_cast< char >( g[ at++ ] ) );
        static const int parent_entity = 0, child_entity = 0;
        const int a_index = attach_find_component( lists[ 0 ], 0x736E18u ), m_index = attach_find_component( lists[ 1 ], 0x736968u );
        const int a_slot = a_index, m_slot = m_index;
        hooks_t h;
        struct ctx_t
        {
            log_t* log;
            const int* a;
            const int* m;
        } ctx{ &log, a_index >= 0 ? &a_slot : nullptr, m_index >= 0 ? &m_slot : nullptr };
        h.user = &ctx;
        h.find_attach = []( void* u, const int* ) { return static_cast< ctx_t* >( u )->a; };
        h.find_motion = []( void* u, const int* ) { return static_cast< ctx_t* >( u )->m; };
        h.attach = []( void* u, const int* c, const int* m, const float* relative, const std::string& name ) {
            std::vector< std::uint32_t >& w = static_cast< ctx_t* >( u )->log->words;
            w.push_back( 12 );
            w.push_back( static_cast< std::uint32_t >( *c ) );
            w.push_back( static_cast< std::uint32_t >( *m ) );
            for ( int k = 0; k < 16; ++k )
            {
                std::uint32_t b;
                std::memcpy( &b, relative + k, 4 );
                w.push_back( b );
            }
            w.push_back( static_cast< std::uint32_t >( name.size( ) ) );
            for ( char ch : name )
                w.push_back( static_cast< std::uint8_t >( ch ) );
        };
        h.detach = []( void* u, const int* c, const int* m ) {
            std::vector< std::uint32_t >& w = static_cast< ctx_t* >( u )->log->words;
            w.push_back( 13 );
            w.push_back( static_cast< std::uint32_t >( *c ) );
            w.push_back( static_cast< std::uint32_t >( *m ) );
        };
        h.update_from_owner = []( void* u, const int* c ) {
            ctx_t& x = *static_cast< ctx_t* >( u );
            if ( !x.log->has_transform )
                return;
            x.log->words.push_back( 14 );
            x.log->words.push_back( static_cast< std::uint32_t >( *c ) );
            for ( int k = 0; k < 6; ++k )
                x.log->words.push_back( 0u );
        };
        const int* parent = parent_link ? &parent_entity : nullptr;
        const int* child = child_link ? &child_entity : nullptr;
        switch ( which )
        {
        case 0:   // slot 6 0x5F54F0
            if ( at_start )
                attach_entity_attach( position, rotation, node, parent, child, h );
            break;
        case 1:   // the plug 0x5F5550
            attach_entity_attach( position, rotation, node, parent, child, h );
            break;
        default:  // slot 7 0x5F5520, the plug 0x5F5690
            attach_entity_detach( parent, child, h );
            break;
        }
        const unsigned n = g[ at++ ];
        same( "log words", static_cast< std::uint32_t >( log.words.size( ) ), n );
        for ( unsigned k = 0; k < n; ++k )
            same( "log word", k < log.words.size( ) ? log.words[ k ] : 0xDEADu, g[ at + k ] );
        at += n;
    }
    if ( at != g_golden_ate_size )
    {
        LOG_ERROR( "attach_entity: %zu words read of %u", at, g_golden_ate_size );
        return false;
    }
    if ( failed )
    {
        LOG_ERROR( "attach_entity: %d of %d values differ", failed, compared );
        return false;
    }
    LOG_SUCCESS( "VuAttachEntity (0x5F54F0, 0x5F5520, 0x5F5550, 0x5F5690, 0x5F56D0, 0x5F57E0): %u vectors, %d values bit-exact against retail's own code", vectors, compared );
    return true;
}

// Case rotate_motion: VuRotateMotionEntity's 0x5D0FE0 then its tick 0x5D1090 over seeded sequences against retail: the axis and
// angle after enable, each tick's angle, the target's matrix / linear / angular, the IF calls.
inline bool verify_rotate_motion( )
{
    const unsigned* g = g_golden_rot_words;
    std::size_t at = 0;
    int failed = 0, compared = 0, ticks = 0;
    int vec = 0;
    auto bits = []( float f ) {
        std::uint32_t b;
        std::memcpy( &b, &f, 4 );
        return b;
    };
    auto same = [ & ]( const char* what, std::uint32_t mine ) {
        const std::uint32_t r = g[ at++ ];
        ++compared;
        if ( r != mine )
        {
            if ( failed < 8 )
                LOG_ERROR( "rotate_motion vector %d: %s 0x%08X, retail 0x%08X", vec, what, mine, r );
            ++failed;
        }
    };
    const unsigned vectors = g[ at++ ];
    for ( vec = 0; vec < static_cast< int >( vectors ); ++vec )
    {
        rotate_motion_t r;
        std::memcpy( r.axis, g + at, 12 );
        at += 3;
        std::memcpy( &r.speed, g + at++, 4 );
        r.local = g[ at++ ] != 0;
        std::memcpy( r.base, g + at, 64 );   // the words +0x88 held before
        at += 16;
        const bool has_world = g[ at++ ] != 0;
        float world[ 16 ];
        std::memcpy( world, g + at, 64 );
        at += 16;
        rotate_motion_enable( r, has_world ? world : nullptr );
        for ( int k = 0; k < 3; ++k )
            same( "axis", bits( r.axis[ k ] ) );
        for ( int k = 0; k < 16; ++k )
            same( "base", bits( r.base[ k ] ) );
        same( "angle", bits( r.angle ) );
        const unsigned n = g[ at++ ];
        for ( unsigned t = 0; t < n; ++t )
        {
            float dt;
            std::memcpy( &dt, g + at++, 4 );
            float m[ 16 ], angular[ 4 ];
            rotate_motion_tick( r, dt, m, angular );
            same( "tick angle", bits( r.angle ) );
            for ( int k = 0; k < 16; ++k )
                same( "matrix", bits( m[ k ] ) );
            for ( int k = 0; k < 4; ++k )
                same( "linear", 0u );
            for ( int k = 0; k < 3; ++k )
                same( "angular", bits( angular[ k ] ) );
            same( "IF calls", 1u );
            ++ticks;
        }
    }
    if ( at != g_golden_rot_size )
    {
        LOG_ERROR( "rotate_motion: %zu words read of %u", at, g_golden_rot_size );
        return false;
    }
    if ( failed )
    {
        LOG_ERROR( "rotate_motion: %d of %d values differ", failed, compared );
        return false;
    }
    LOG_SUCCESS( "VuRotateMotionEntity (0x5D0FE0, the tick 0x5D1090): %d ticks of %u sequences, %d values bit-exact against retail's own code", ticks, vectors, compared );
    return true;
}

// Case graph_logic: the handlers of VuOneShotFilterEntity 0x5DECC0 / 0x5DECE0 / 0x5DF000, VuDelayEntity 0x5DFBE0 / 0x5DFC40 / 0x5DFB50,
// VuBooleanEntity 0x5E5790 .. 0x5E57F0 / 0x5E5E10 / 0x5E5EA0, VuGenericEventEntity 0x5F0330 / 0x5F0420 over seeded sequences on
// retail's offsets: the state after each op, the outputs in order with their params, the Broadcast's params and hash.
inline bool verify_graph_logic( )
{
    static const char* const names[ 5 ] = { "Out", "OnChanged", "OnChangedTrue", "OnChangedFalse", "OnReceived" };
    const unsigned* g = g_golden_lgc_words;
    std::size_t at = 0;
    int failed = 0, compared = 0, op_index = 0;
    auto same = [ & ]( const char* what, std::uint32_t mine ) {
        const std::uint32_t r = g[ at++ ];
        ++compared;
        if ( r != mine )
        {
            if ( failed < 8 )
                LOG_ERROR( "graph_logic op %d: %s 0x%08X, retail 0x%08X", op_index, what, mine, r );
            ++failed;
        }
    };
    auto bits = []( float f ) {
        std::uint32_t b;
        std::memcpy( &b, &f, 4 );
        return b;
    };
    auto as_float = []( std::uint32_t b ) {
        float f;
        std::memcpy( &f, &b, 4 );
        return f;
    };
    std::vector< std::array< std::uint32_t, 3 > > fires;
    auto fires_same = [ & ]( ) {
        same( "fires", static_cast< std::uint32_t >( fires.size( ) ) );
        const unsigned n = g[ at - 1 ];
        for ( unsigned k = 0; k < n; ++k )
        {
            const std::array< std::uint32_t, 3 > mine = k < fires.size( ) ? fires[ k ] : std::array< std::uint32_t, 3 >{ 99u, 99u, 99u };
            same( "fire name", mine[ 0 ] );
            same( "fire params size", mine[ 1 ] );
            same( "fire params incoming", mine[ 2 ] );
        }
    };
    auto out = [ & ]( const char* name, std::uint32_t incoming ) {
        std::uint32_t id = 99;
        for ( std::uint32_t k = 0; k < 5; ++k )
            if ( std::strcmp( names[ k ], name ) == 0 )
                id = k;
        fires.push_back( { id, incoming ? 16u : 0u, incoming } );
    };
    std::uint8_t shot = 0, pending = 0, value = 0;
    float delay = 0.0f, window = 0.0f, remaining = 0.0f;
    const unsigned ops = g[ at++ ];
    for ( op_index = 0; op_index < static_cast< int >( ops ); ++op_index )
    {
        fires.clear( );
        const unsigned kind = g[ at++ ];
        if ( kind == 0 )   // a new object
        {
            shot = pending = value = 0;
            delay = as_float( g[ at++ ] );
            window = as_float( g[ at++ ] );
            remaining = 0.0f;
        }
        else if ( kind == 1 )
        {
            const unsigned op = g[ at++ ];
            if ( op == 0 )
                shot = 0;
            else if ( op == 1 )
                shot = 1;
            else
                one_shot_in( shot, [ & ] { out( "Out", 1 ); } );
            same( "shot", shot );
            fires_same( );
        }
        else if ( kind == 2 )
        {
            const unsigned op = g[ at++ ];
            const std::uint32_t a = g[ at++ ], b = g[ at++ ], c = g[ at++ ];
            if ( op == 0 )
                delay_in( pending, remaining, delay, window, [ & ] { return as_float( a ); } );
            else if ( op == 1 )
                delay_update( pending, remaining, as_float( a ), [ & ] { out( "Out", 0 ); } );
            else
            {
                std::uint32_t image[ 2 ] = { b, c };
                delay_update( pending, remaining, delay_manual_dt( reinterpret_cast< const std::uint8_t* >( image ), static_cast< std::int32_t >( a ) ), [ & ] { out( "Out", 0 ); } );
            }
            same( "pending", pending );
            same( "remaining", bits( remaining ) );
            fires_same( );
        }
        else if ( kind == 3 )
        {
            const unsigned op = g[ at++ ];
            const std::uint32_t size = g[ at++ ], tag = g[ at++ ], byte = g[ at++ ];
            std::uint32_t ret_type = 0, ret_byte = 0;
            auto fire = [ & ]( const char* name ) { out( name, 0 ); };
            if ( op == 0 )
                boolean_set( value, 1, fire );
            else if ( op == 1 )
                boolean_set( value, 0, fire );
            else if ( op == 2 )
                boolean_set( value, value == 0 ? 1 : 0, fire );
            else if ( op == 3 )
            {
                ret_type = 3;
                ret_byte = value;
            }
            else
            {
                std::uint32_t image[ 2 ] = { tag, byte };
                boolean_set( value, boolean_set_value( reinterpret_cast< const std::uint8_t* >( image ), static_cast< std::int32_t >( size ) ), fire );
            }
            same( "value", value );
            same( "ret type", ret_type );
            same( "ret byte", ret_byte );
            fires_same( );
        }
        else
        {
            std::string name, own;
            for ( unsigned n = g[ at++ ]; n; --n )
                name.push_back( static_cast< char >( g[ at++ ] ) );
            for ( unsigned n = g[ at++ ]; n; --n )
                own.push_back( static_cast< char >( g[ at++ ] ) );
            const ui_params_t p = generic_broadcast_params( name );
            std::uint32_t hash = 0x811C9DC5u;
            for ( const char* c = "OnGenericEvent"; *c; ++c )
                hash = ( hash ^ static_cast< std::uint8_t >( *c ) ) * 0x1000193u;
            same( "hash", hash );
            same( "params size", static_cast< std::uint32_t >( p.m_size ) );
            for ( int k = 0; k < p.m_size; ++k )
                same( "params byte", p.m_bytes[ k ] );
            if ( generic_receive( own, p.m_bytes, p.m_size ) )
                out( "OnReceived", 0 );
            fires_same( );
        }
    }
    if ( at != g_golden_lgc_size )
    {
        LOG_ERROR( "graph_logic: %zu words read of %u", at, g_golden_lgc_size );
        return false;
    }
    if ( failed )
    {
        LOG_ERROR( "graph_logic: %d of %d values differ", failed, compared );
        return false;
    }
    LOG_SUCCESS( "The graph's logic entities (one-shot filter, delay, boolean, generic event): %u ops, %d values bit-exact against retail's own code", ops, compared );
    return true;
}

// Case aabb_merge: 0x517530 on seeded aabbs (the default NaN among them): the six lanes.
inline bool verify_aabb_merge( )
{
    const unsigned* g = g_golden_amg_words;
    int failed = 0, compared = 0;
    const unsigned n = g[ 0 ];
    std::size_t at = 1;
    for ( unsigned v = 0; v < n; ++v )
    {
        float a[ 8 ], b[ 8 ];
        std::memcpy( a, g + at, 32 );
        std::memcpy( b, g + at + 8, 32 );
        at += 16;
        boat_rig_aabb_merge( a, b );
        for ( int k : { 0, 1, 2, 4, 5, 6 } )
        {
            std::uint32_t w;
            std::memcpy( &w, a + k, 4 );
            ++compared;
            if ( w != g[ at++ ] )
                ++failed;
        }
    }
    if ( at != g_golden_amg_size || failed )
    {
        LOG_ERROR( "aabb_merge: %d of %d values differ", failed, compared );
        return false;
    }
    LOG_SUCCESS( "0x517530 (the aabb merge of Build step 6): %u vectors, %d values bit-exact against retail's own code", n, compared );
    return true;
}

inline bool verify_rig_all( )
{
    bool ok = true;
    ok = verify_aabb_merge( ) && ok;
    ok = verify_graph_logic( ) && ok;
    ok = verify_boat_rig( ) && ok;
    ok = verify_attach_components( ) && ok;
    ok = verify_attach_entity( ) && ok;
    ok = verify_rotate_motion( ) && ok;
    return ok;
}
