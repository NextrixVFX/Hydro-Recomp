#pragma once

// Boat batch 33's checks: VuGhostBoatEntity and VuGhostManager 0x4AF640 (case ghost_boat, cases/ghostboat.py). The golden
// words are one stream per table; each reader below walks one vector.

#include <workspace/core/boat/ghost.hxx>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

inline const unsigned* replay_apply( std::uint8_t* image, const unsigned* s )
{
    const unsigned n = *s++;
    for ( unsigned k = 0; k < n; ++k, s += 2 )
        std::memcpy( image + s[ 0 ], &s[ 1 ], 4 );
    return s;
}

// One logged row as the hook saw it; the golden rows follow the same words.
struct replay_log_t
{
    std::vector< std::vector< unsigned > > rows;
    std::uint8_t* debris = nullptr;

    static void push( void* u, std::vector< unsigned > row ) { static_cast< replay_log_t* >( u )->rows.push_back( std::move( row ) ); }

    // The rows against the stream at s; s moved past them.
    bool match( const unsigned*& s ) const
    {
        bool same = rows.size( ) == *s;
        const unsigned n = *s++;
        for ( unsigned k = 0; k < n; ++k )
        {
            const unsigned len = *s++;
            if ( same && ( k >= rows.size( ) || rows[ k ].size( ) != len || std::memcmp( rows[ k ].data( ), s, 4u * len ) != 0 ) )
                same = false;
            s += len;
        }
        return same;
    }
};

inline bool verify_ghost_boat_tick( )
{
    int failed = 0;
    const unsigned* s = g_golden_ghost_boat_tick;
    for ( std::size_t index = 0; index < g_golden_ghost_boat_tick_count; ++index )
    {
        ghost_data_t d;
        d.count = static_cast< std::int32_t >( *s++ );
        d.capacity = d.count;
        d.frames.resize( 24u * static_cast< std::size_t >( d.count ) + 24u );
        if ( d.count != 0 )
            std::memcpy( d.frames.data( ), s, 24u * static_cast< std::size_t >( d.count ) );
        s += 6 * d.count;
        float dt;
        std::memcpy( &dt, s, 4 );
        const bool has_proxy = s[ 1 ] != 0;
        std::uint8_t proxy[ 8 ] = { };
        std::memcpy( proxy + 4, &s[ 2 ], 4 );
        std::uint8_t debris[ 0x100 ] = { };
        debris[ 0x94 ] = static_cast< std::uint8_t >( s[ 3 ] );
        const unsigned post_proxy = s[ 4 ];
        s += 5;
        std::vector< std::uint8_t > boat( 0x600, 0 ), want( 0x600, 0 );
        auto body = std::make_unique< vu_rigid_body_t >( );
        std::memset( body.get( ), 0, 0x2D4 );
        s = replay_apply( boat.data( ), s );
        s = replay_apply( reinterpret_cast< std::uint8_t* >( body.get( ) ), s );
        std::memcpy( want.data( ), boat.data( ), 0x600 );
        std::vector< std::uint8_t > body_want( 0x2D4 );
        std::memcpy( body_want.data( ), body.get( ), 0x2D4 );
        s = replay_apply( want.data( ), s );
        s = replay_apply( body_want.data( ), s );
        replay_log_t log;
        log.debris = debris;
        ghost_replay_hooks_t h;
        h.user = &log;
        h.debris_launch = []( void* u ) {
            replay_log_t::push( u, { 1u } );
            static_cast< replay_log_t* >( u )->debris[ 0x94 ] = 1;
        };
        h.destroyed_event = []( void* u ) { replay_log_t::push( u, { 2u } ); };
        h.set_world = []( void* u, const float* m ) {
            std::vector< unsigned > r( 18, 3u );
            r[ 1 ] = 0u;
            std::memcpy( r.data( ) + 2, m, 64 );
            replay_log_t::push( u, std::move( r ) );
        };
        h.solver_prepare = []( void* u, const float* m ) {
            std::vector< unsigned > r( 17, 4u );
            std::memcpy( r.data( ) + 1, m, 64 );
            replay_log_t::push( u, std::move( r ) );
        };
        h.add_body = []( void* u ) { replay_log_t::push( u, { 5u, 1u } ); };
        h.add_step = []( void* u ) { replay_log_t::push( u, { 6u, 1u } ); };
        h.decision = []( void* u, float t ) {
            unsigned w;
            std::memcpy( &w, &t, 4 );
            replay_log_t::push( u, { 7u, w } );
        };
        ghost_boat_tick( boat.data( ), debris, d, *body, has_proxy ? proxy : nullptr, dt, h );
        unsigned proxy_word;
        std::memcpy( &proxy_word, proxy + 4, 4 );
        const bool same_log = log.match( s );
        if ( ( std::memcmp( boat.data( ), want.data( ), 0x600 ) != 0 || std::memcmp( body.get( ), body_want.data( ), 0x2D4 ) != 0 || proxy_word != post_proxy || !same_log ) &&
             ++failed <= 4 )
        {
            int bad_boat = -1, bad_body = -1;
            for ( int o = 0x5FC; o >= 0; o -= 4 )
                if ( std::memcmp( boat.data( ) + o, want.data( ) + o, 4 ) != 0 )
                    bad_boat = o;
            for ( int o = 0x2D0; o >= 0; o -= 4 )
                if ( std::memcmp( reinterpret_cast< std::uint8_t* >( body.get( ) ) + o, body_want.data( ) + o, 4 ) != 0 )
                    bad_body = o;
            LOG_ERROR( "ghost tick vector %d differs from retail: boat +0x%X, body +0x%X, proxy %08X / %08X, log %d", static_cast< int >( index ), bad_boat, bad_body,
                       proxy_word, post_proxy, same_log ? 1 : 0 );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuGhostBoatEntity's tick 0x422A10 (+ 0x531850, 0x410C30, 0x422FE0) matches retail: %d vectors, the boat, the body, the proxy, the hook rows",
                     static_cast< int >( g_golden_ghost_boat_tick_count ) );
    else
        LOG_ERROR( "ghost tick FAILED %d of %d vectors", failed, static_cast< int >( g_golden_ghost_boat_tick_count ) );
    return failed == 0;
}

inline bool verify_ghost_boat_draw( )
{
    int failed = 0, labels = 0;
    const unsigned* s = g_golden_ghost_boat_draw;
    for ( std::size_t index = 0; index < g_golden_ghost_boat_draw_count; ++index )
    {
        std::vector< std::uint8_t > boat( 0x600, 0 ), want( 0x600, 0 ), tr( 0x100 ), first( 0x100 ), cam( 0x200 );
        s = replay_apply( boat.data( ), s );
        std::memcpy( tr.data( ), s, 0x100 );
        std::memcpy( first.data( ), s + 64, 0x100 );
        s += 128;
        const bool any = *s++ != 0u;
        std::memcpy( cam.data( ), s, 0x200 );
        s += 128;
        const bool flag = *s++ != 0u;
        std::memcpy( want.data( ), boat.data( ), 0x600 );
        s = replay_apply( want.data( ), s );
        ghost_boat_fade( boat.data( ), tr.data( ), any ? first.data( ) : nullptr );
        ghost_label_t l;
        const bool label = !flag && ghost_boat_label( boat.data( ), tr.data( ), cam.data( ), l );
        replay_log_t log;
        replay_log_t::push( &log, { 11u, 1u } );
        if ( label )
        {
            ++labels;
            unsigned w[ 27 ] = { 12u, 0u, 0x12345678u, 0u, 0u, 0u, 0u, 0x66u, 0x3F800000u, 0u, 0u, 1u };
            std::memcpy( &w[ 1 ], &l.depth, 4 );
            std::memcpy( &w[ 3 ], &l.x, 4 );
            std::memcpy( &w[ 4 ], &l.y, 4 );
            std::memcpy( &w[ 12 ], l.params, 60 );
            log.rows.emplace_back( w, w + 27 );
        }
        const bool same_log = log.match( s );
        if ( ( std::memcmp( boat.data( ), want.data( ), 0x600 ) != 0 || !same_log ) && ++failed <= 4 )
            LOG_ERROR( "ghost draw vector %d differs from retail: +0x2C8 %08X / %08X, log %d", static_cast< int >( index ), *reinterpret_cast< unsigned* >( boat.data( ) + 0x2C8 ),
                       *reinterpret_cast< unsigned* >( want.data( ) + 0x2C8 ), same_log ? 1 : 0 );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuGhostBoatEntity's draw 0x422590 matches retail: %d vectors, %d labels (the fade, the point, the depth, the 60 params)",
                     static_cast< int >( g_golden_ghost_boat_draw_count ), labels );
    else
        LOG_ERROR( "ghost draw FAILED %d of %d vectors", failed, static_cast< int >( g_golden_ghost_boat_draw_count ) );
    return failed == 0;
}

// The canonical text case ghost_boat's document row holds (water.py _json_dump).
inline std::string replay_dump( const value_t& v )
{
    if ( v.tag == 4 )
        return "s'" + v.text + "'";
    std::string out = "{";
    std::vector< std::pair< std::string, const value_t* > > keys;
    for ( const auto& f : v.object )
        keys.push_back( { f.first, &f.second } );
    std::sort( keys.begin( ), keys.end( ), []( const auto& a, const auto& b ) { return a.first < b.first; } );
    for ( std::size_t k = 0; k < keys.size( ); ++k )
        out += ( k ? "," : "" ) + keys[ k ].first + ":" + replay_dump( *keys[ k ].second );
    return out + "}";
}

inline bool verify_ghost_add( )
{
    int failed = 0, made = 0;
    const unsigned* s = g_golden_ghost_boat_add;
    for ( std::size_t index = 0; index < g_golden_ghost_boat_add_count; ++index )
    {
        const std::int32_t at = static_cast< std::int32_t >( s[ 0 ] );
        const unsigned already = s[ 1 ], nblob = s[ 2 ];
        s += 3;
        std::vector< std::uint8_t > blob( nblob );
        for ( unsigned k = 0; k < nblob; ++k )
            blob[ k ] = static_cast< std::uint8_t >( s[ k ] );
        s += nblob;
        const unsigned answer = s[ 0 ], size = s[ 1 ], nname = s[ 2 ];
        s += 3;
        std::string want_name;
        for ( unsigned k = 0; k < nname; ++k )
            want_name += static_cast< char >( s[ k ] );
        s += nname;
        const unsigned record_index = s[ 0 ];
        s += 3;
        std::vector< std::unique_ptr< ghost_record_t > > ghosts;
        for ( unsigned k = 0; k < already; ++k )
            ghosts.push_back( std::make_unique< ghost_record_t >( ) );
        replay_log_t log;
        std::string name;
        int boat_tag = 0;
        const ghost_data_t* bound = nullptr;
        ghost_add_hooks_t h;
        h.make_boat = [ & ]( const std::string& n, const value_t& doc ) -> void* {
            name = n;
            const std::string text = replay_dump( doc );
            std::vector< unsigned > r = { 21u, static_cast< unsigned >( text.size( ) ) };
            for ( unsigned char c : text )
                r.push_back( c );
            log.rows.push_back( std::move( r ) );
            log.rows.push_back( { 22u, 0x811C9DC5u } );
            return &boat_tag;
        };
        h.place = [ & ]( void*, const float* p, const float* e ) {
            std::vector< unsigned > r( 4, 23u ), q( 4, 25u );
            std::memcpy( r.data( ) + 1, p, 12 );
            std::memcpy( q.data( ) + 1, e, 12 );
            log.rows.push_back( std::move( r ) );
            log.rows.push_back( { 24u, 1u } );
            if ( index % 2u )
                log.rows.push_back( { 26u } );
            log.rows.push_back( std::move( q ) );
        };
        h.bind = [ & ]( void*, const ghost_data_t& d ) { bound = &d; };
        const bool r = ghost_add( ghosts, at, blob.data( ), blob.size( ), h );
        const bool same_log = log.match( s );
        const bool same = r == ( answer != 0u ) && ghosts.size( ) == size && name == want_name && same_log &&
                          ( !r || ( ghosts.back( )->index == static_cast< std::int32_t >( record_index ) && bound == &ghosts.back( )->data && ghosts.back( )->boat == &boat_tag ) );
        made += r ? 1 : 0;
        if ( !same && ++failed <= 4 )
            LOG_ERROR( "ghost add vector %d differs from retail: answer %d / %u, size %d / %u, name %s / %s, log %d", static_cast< int >( index ), r ? 1 : 0, answer,
                       static_cast< int >( ghosts.size( ) ), size, name.c_str( ), want_name.c_str( ), same_log ? 1 : 0 );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuGhostManager's 0x4AF640 matches retail: %d vectors, %d boats made (the name, the document, the first pose, the 8 ghost limit, refused blobs)",
                     static_cast< int >( g_golden_ghost_boat_add_count ), made );
    else
        LOG_ERROR( "ghost add FAILED %d of %d vectors", failed, static_cast< int >( g_golden_ghost_boat_add_count ) );
    return failed == 0;
}

inline bool verify_ghost_boat_ctor( )
{
    int failed = 0;
    const unsigned* s = g_golden_ghost_boat_ctor;
    for ( std::size_t index = 0; index < g_golden_ghost_boat_ctor_count; ++index, s += 17 )
    {
        value_t list;
        list.tag = s[ 0 ];
        const unsigned count = s[ 1 ];
        for ( unsigned k = 0; k < count; ++k )
        {
            const unsigned* it = s + 2 + 3 * k;
            value_t v;
            v.tag = it[ 0 ];
            v.integer = static_cast< int >( it[ 1 ] );
            std::memcpy( &v.real, &it[ 1 ], 4 );
            v.int64 = static_cast< std::int64_t >( static_cast< std::uint64_t >( it[ 1 ] ) | static_cast< std::uint64_t >( it[ 2 ] ) << 32 );
            list.list.push_back( v );
        }
        value_t root;
        json_member( json_member( json_member( root, "VuDBAsset" ), "Boat" ), "GhostFadeDist" ) = list;
        std::vector< std::uint8_t > boat( 0x600, 0xCC ), hull( 0x200, 0 );
        ghost_boat_construct( boat.data( ), hull.data( ), root );
        if ( ( std::memcmp( boat.data( ) + 0x520, s + 11, 12 ) != 0 || std::memcmp( boat.data( ) + 0x590, s + 14, 8 ) != 0 || hull[ 0x1EC ] != s[ 16 ] ) && ++failed <= 4 )
            LOG_ERROR( "ghost ctor vector %d differs from retail: fade %08X %08X / %08X %08X", static_cast< int >( index ), *reinterpret_cast< unsigned* >( boat.data( ) + 0x590 ),
                       *reinterpret_cast< unsigned* >( boat.data( ) + 0x594 ), s[ 14 ], s[ 15 ] );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuGhostBoatEntity's ctor tail 0x422310 matches retail: %d vectors of GhostFadeDist", static_cast< int >( g_golden_ghost_boat_ctor_count ) );
    else
        LOG_ERROR( "ghost ctor FAILED %d of %d vectors", failed, static_cast< int >( g_golden_ghost_boat_ctor_count ) );
    return failed == 0;
}

inline bool verify_replay_all( )
{
    bool ok = verify_ghost_boat_tick( );
    ok = verify_ghost_boat_draw( ) && ok;
    ok = verify_ghost_add( ) && ok;
    ok = verify_ghost_boat_ctor( ) && ok;
    return ok;
}
