#pragma once

// World batch 33: checks on the boat's animated model (world/rig.hxx). Included by checks/world.hxx.

#include <workspace/core/world/attach.hxx>
#include <workspace/core/world/rig.hxx>

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

inline bool verify_rig_all( )
{
    bool ok = true;
    ok = verify_boat_rig( ) && ok;
    ok = verify_attach_components( ) && ok;
    return ok;
}
