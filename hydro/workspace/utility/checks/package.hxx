#pragma once

#include <workspace/core/assets/factory.hxx>
#include <workspace/core/boat/powerup.hxx>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// The package database (boat batch 25, the asset loaders): engine assets/package.hxx + archive.hxx open_pack /
// read_asset + factory.hxx's miss path against retail run whole on a factory retail built and the real Base.apf
// (cases/package.py). Compared: the filter evaluator on every text x sku; per header variant the load's answer, the
// listener calls, the package (sku, words, directory size), every type's entry count and FNV over (name, vujb_dump of
// the entry, used byte), every record's name vector; then the createAsset script with the case's fake asset types.

// Base.apf next to the exe or up to eight folders above it (the same walk as c_world::pack_path).
inline std::string package_check_pack( )
{
#if defined( _WIN32 )
    char module_path[ MAX_PATH ]{};
    GetModuleFileNameA( nullptr, module_path, MAX_PATH );
    std::string directory = module_path;
    for ( int step = 0; step < 9; ++step )
    {
        const auto slash = directory.find_last_of( "\\/" );
        if ( slash == std::string::npos )
            break;
        directory.resize( slash );
        for ( const char* leaf : { "\\HydroThunderUWP\\Base.apf", "\\Base.apf" } )
            if ( GetFileAttributesA( ( directory + leaf ).c_str( ) ) != INVALID_FILE_ATTRIBUTES )
                return directory + leaf;
    }
#endif
    return {};
}

inline bool verify_package_filter( )
{
    int failed = 0;
    for ( std::size_t i = 0; i < g_golden_package_filter_count; ++i )
    {
        const golden_package_filter_t& g = g_golden_package_filter[ i ];
        c_asset_filter f;
        f.set( "sku", g.m_sku );
        f.set( "platform", "Metro" );
        const unsigned eax = f.evaluate( g.m_text ) ? 1u : 0u;
        if ( eax != g.m_eax || ( f.result( ) ? 1u : 0u ) != g.m_result || f.error( ) != g.m_error )
        {
            if ( ++failed <= 6 )
                LOG_ERROR( "0x55F090 differs on '%s' (sku '%s'): ours %u / %d / '%s', retail %u / %u / '%s'", g.m_text, g.m_sku, eax,
                    f.result( ) ? 1 : 0, f.error( ).c_str( ), g.m_eax, g.m_result, g.m_error );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "the AssetData filter matches retail's 0x55F090 / 0x55F670 / 0x55F520 on %d texts x skus (answer, result, error)",
            static_cast< int >( g_golden_package_filter_count ) );
    else
        LOG_ERROR( "the AssetData filter FAILED %d of %d against 0x55F090", failed, static_cast< int >( g_golden_package_filter_count ) );
    return failed == 0;
}

struct package_check_t
{
    std::string pack;
    std::vector< vu_asset_t* > assets;
    std::vector< std::array< unsigned, 4 > > loads;
    std::vector< std::pair< std::size_t, unsigned > > deletes;
    std::vector< std::string > packages;
    std::vector< std::size_t > created;
    std::vector< std::string > fail_names{ "OptionDB" };

    std::size_t index_of( const vu_asset_t* a ) const
    {
        for ( std::size_t k = 0; k < assets.size( ); ++k )
            if ( assets[ k ] == a )
                return k;
        return ~std::size_t( 0 );
    }
};

inline void package_check_hooks( c_asset_factory& f, package_check_t& c )
{
    f.hooks.user = &c;
    f.hooks.pack_path = []( void* u, const std::string& file ) {
        return file == "Base.apf" ? static_cast< package_check_t* >( u )->pack : std::string( );
    };
    f.hooks.package_loaded = []( void* u, const std::string& name ) { static_cast< package_check_t* >( u )->packages.push_back( name ); };
    f.hooks.create = []( void* u, const std::string& ) {
        auto* c = static_cast< package_check_t* >( u );
        c->assets.push_back( new vu_asset_t );
        return c->assets.back( );
    };
    f.hooks.load_data = []( void* u, vu_asset_t& a, const std::vector< std::uint8_t >& data ) {
        auto* c = static_cast< package_check_t* >( u );
        const bool ok = std::find( c->fail_names.begin( ), c->fail_names.end( ), a.name ) == c->fail_names.end( );
        c->loads.push_back( { static_cast< unsigned >( c->index_of( &a ) ), static_cast< unsigned >( data.size( ) ),
                              c_archive::fnv( data.data( ), data.size( ) ), ok ? 1u : 0u } );
        return ok;
    };
    f.hooks.created = []( void* u, vu_asset_t& a ) {
        auto* c = static_cast< package_check_t* >( u );
        c->created.push_back( c->index_of( &a ) );
    };
    f.hooks.destroy = []( void* u, vu_asset_t* a ) {
        auto* c = static_cast< package_check_t* >( u );
        c->deletes.push_back( { c->index_of( a ), 1u } );
    };
}

// One snapshot against the golden ranges: the package's types and the factory's records.
inline int package_check_snapshot( const c_asset_factory& f, unsigned type_at, unsigned type_count, unsigned record_at,
                                   unsigned record_count, const char* what )
{
    int failed = 0;
    std::vector< std::tuple< std::string, unsigned, unsigned > > types;
    if ( f.package_count( ) != 0 )
    {
        for ( const auto& [ type, names ] : f.package_at( 0 ).assets )
        {
            std::uint32_t h = 0x811C9DC5u;
            for ( const auto& [ name, e ] : names )
            {
                std::string text = name;
                text.push_back( '\0' );
                vujb_dump( e.data, text );
                text.push_back( '\0' );
                text.push_back( e.used ? '1' : '0' );
                for ( unsigned char ch : text )
                    h = ( h ^ ch ) * 16777619u;
            }
            types.emplace_back( type, static_cast< unsigned >( names.size( ) ), h );
        }
    }
    if ( types.size( ) != type_count )
    {
        ++failed;
        LOG_ERROR( "asset_package %s: %d package types, retail %u", what, static_cast< int >( types.size( ) ), type_count );
    }
    for ( std::size_t k = 0; k < types.size( ) && k < type_count; ++k )
    {
        const golden_asset_package_snap_t& g = g_golden_asset_package_types[ type_at + k ];
        if ( std::get< 0 >( types[ k ] ) != g.m_name || std::get< 1 >( types[ k ] ) != g.m_count || std::get< 2 >( types[ k ] ) != g.m_fnv )
        {
            ++failed;
            LOG_ERROR( "asset_package %s: type %s %u entries fnv %08X, retail %s %u %08X", what, std::get< 0 >( types[ k ] ).c_str( ),
                std::get< 1 >( types[ k ] ), std::get< 2 >( types[ k ] ), g.m_name, g.m_count, g.m_fnv );
        }
    }
    std::size_t k = 0;
    for ( const auto& [ type, r ] : f.records( ) )
    {
        std::uint32_t h = 0x811C9DC5u;
        for ( const std::string& n : r.names )
        {
            for ( unsigned char ch : n )
                h = ( h ^ ch ) * 16777619u;
            h = ( h ^ 0u ) * 16777619u;
        }
        if ( k >= record_count )
        {
            ++failed;
            break;
        }
        const golden_asset_package_snap_t& g = g_golden_asset_package_records[ record_at + k ];
        if ( type != g.m_name || r.names.size( ) != g.m_count || h != g.m_fnv )
        {
            ++failed;
            LOG_ERROR( "asset_package %s: record %s %d names fnv %08X, retail %s %u %08X", what, type.c_str( ),
                static_cast< int >( r.names.size( ) ), h, g.m_name, g.m_count, g.m_fnv );
        }
        ++k;
    }
    if ( k != record_count )
        ++failed;
    return failed;
}

inline int package_check_ops( c_asset_factory& f, package_check_t& c )
{
    int failed = 0;
    const char* const fake[] = { "VuDBAsset", "VuFontAsset", "VuTemplateAsset", "VuProjectAsset", "VuTextureAsset", "VuStringAsset" };
    const unsigned versions[] = { 1u, 1u, 1u, 1u, 17u, 1u };
    for ( int k = 0; k < 6; ++k )
        f.register_type( fake[ k ], k == 5 ? 0u : 0x70000000u + 0xD200u + 4u * static_cast< unsigned >( k ), 0u, versions[ k ], true );
    for ( std::size_t i = 0; i < g_golden_asset_package_op_count; ++i )
    {
        const golden_asset_package_op_t& g = g_golden_asset_package_ops[ i ];
        f.set_sku( g.m_sku );
        f.set_language( g.m_lang );
        const std::size_t loads = c.loads.size( ), deletes = c.deletes.size( ), created = c.created.size( );
        vu_asset_t* a = f.create( g.m_type, g.m_name );
        const int eax = a == nullptr ? -1 : static_cast< int >( c.index_of( a ) );
        bool ok = eax == g.m_eax && c.deletes.size( ) - deletes == g.m_deletes;
        const std::array< unsigned, 4 > none{ 0xFFFFFFFFu, 0u, 0u, 0u };
        const std::array< unsigned, 4 > load = c.loads.size( ) == loads + 1 ? c.loads.back( ) : none;
        ok = ok && c.loads.size( ) <= loads + 1 && load[ 0 ] == g.m_load[ 0 ] && load[ 1 ] == g.m_load[ 1 ] && load[ 2 ] == g.m_load[ 2 ]
            && load[ 3 ] == g.m_load[ 3 ];
        const unsigned cr = c.created.size( ) == created + 1 ? static_cast< unsigned >( c.created.back( ) ) : 0xFFFFFFFFu;
        ok = ok && c.created.size( ) <= created + 1 && cr == g.m_created;
        if ( a != nullptr )
        {
            std::string info;
            vujb_dump( a->info, info );
            ok = ok && a->name == g.m_asset_name && a->lang == g.m_asset_lang && info == g.m_info && a->hash == g.m_hash
                && static_cast< unsigned >( a->refs ) == g.m_refs && a->data_hash == g.m_data_hash;
        }
        if ( !ok )
        {
            ++failed;
            LOG_ERROR( "asset_package op %d createAsset( %s, %s ) sku '%s' lang '%s': ours %d (load %08X %u %08X %u), retail %d (load %08X %u %08X %u)",
                static_cast< int >( i ), g.m_type, g.m_name, g.m_sku, g.m_lang, eax, load[ 0 ], load[ 1 ], load[ 2 ], load[ 3 ], g.m_eax,
                g.m_load[ 0 ], g.m_load[ 1 ], g.m_load[ 2 ], g.m_load[ 3 ] );
        }
    }
    return failed;
}

// powerup_destroy (boat/powerup.hxx) against retail's 0x43B830 / 0x43DCC0 (body 0x43B890) run whole on images (case
// powerup_dtors, cases/race.py): the hook log in order, every string's words after, +0x118, eax.
inline bool verify_powerup_dtors( )
{
    const unsigned* g = g_golden_powerup_dtors_words;
    const std::uint32_t ent = 0x100, head = 0x400, node = 0x440;
    const std::uint32_t offs[ 6 ] = { 0x138, 0xE8, 0xC8, 0xB0, 0x94, 0x74 };
    auto text_at = []( int k ) { return 0x800u + 0x40u * static_cast< std::uint32_t >( k ); };
    auto sym = [ & ]( std::uint32_t x ) -> unsigned {
        if ( x == 0 ) return 0;
        if ( x == ent ) return 1;
        if ( x == head ) return 2;
        if ( x == node ) return 3;
        if ( x == ent + 0x118 ) return 4;
        for ( int k = 0; k < 6; ++k )
            if ( x == text_at( k ) ) return 5u + static_cast< unsigned >( k );
        return 0x100 + ( x & 0xFF );
    };
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned kind = *g++, flags = *g++, leftmost = *g++, untouched = *g++;
        std::map< std::uint32_t, std::uint32_t > mem;
        vu_words_t w;
        w.get = [ & ]( std::uint32_t a ) { auto it = mem.find( a ); return it == mem.end( ) ? 0x5A5A5A5Au : it->second; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ a ] = x; };
        mem[ ent + 0x118 ] = head;
        mem[ head ] = leftmost == 2 ? head : node;
        for ( int k = 0; k < 6; ++k )
        {
            const unsigned n = *g++;
            const unsigned first = n != 0 ? *g : 0u;
            g += ( n + 3 ) / 4;
            mem[ ent + offs[ k ] ] = n < 16 ? first : text_at( k );
            mem[ ent + offs[ k ] + 0x10 ] = n;
            mem[ ent + offs[ k ] + 0x14 ] = n < 16 ? 15u : ( n | 15u );
        }
        std::vector< std::vector< unsigned > > glog( *g++ ), log;
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        const unsigned* after = g;
        g += 6 * 3 + 2;
        powerup_dtor_hooks_t h;
        h.free_block = [ & ]( std::uint32_t a ) { log.push_back( { 1u, sym( a ) } ); };
        h.map_erase = [ & ]( std::uint32_t m, std::uint32_t first, std::uint32_t last ) { log.push_back( { 2u, sym( m ), sym( first ), sym( last ) } ); };
        h.entity_dtor = [ & ]( ) { log.push_back( { 3u, 1u } ); };
        const std::uint32_t r = powerup_destroy( kind == 1 ? powerup_dtor_kind_t::entity : powerup_dtor_kind_t::hidden_package, w, ent, flags, h );
        bool ok = log == glog && untouched == 1;
        for ( int k = 0; k < 6 && ok; ++k )
        {
            const std::uint32_t at = ent + offs[ k ];
            ok = ( w.get( at ) & 0xFFu ) == after[ 3 * k ] && w.get( at + 0x10 ) == after[ 3 * k + 1 ] && w.get( at + 0x14 ) == after[ 3 * k + 2 ];
        }
        ok = ok && sym( w.get( ent + 0x118 ) ) == after[ 18 ] && ( r == ent ? 1u : 0u ) == after[ 19 ];
        if ( !ok && ++failed <= 4 )
            LOG_ERROR( "powerup dtor vector %u (kind %u flags %u) differs from retail: %d log entries, retail %d", v, kind, flags,
                static_cast< int >( log.size( ) ), static_cast< int >( glog.size( ) ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "powerup_destroy matches retail's 0x43B830 / 0x43DCC0 (body 0x43B890) on %u images: the hook log, every string, +0x118, eax", nv );
    else
        LOG_ERROR( "powerup_destroy FAILED %d of %u against 0x43B830 / 0x43DCC0", failed, nv );
    return failed == 0;
}

// asset_destroy (engine assets/factory.hxx) against retail's 0x515D70 / 0x53FD10 / 0x5F4AB0 (bodies 0x515D00 / 0x53FD40 /
// 0x5F4AE0) run whole on images (case asset_dtors, cases/package.py): the hook log in order, every string's words, +0, eax.
inline bool verify_asset_dtors( )
{
    const unsigned* g = g_golden_asset_dtors_words;
    const std::uint32_t obj = 0x100, fac = 0x600, asset = 0x700;
    const std::uint32_t offs[ 3 ][ 3 ] = { { 0x20, 0x8, 0 }, { 0x4C, 0, 0 }, { 0x90, 0x78, 0x60 } };
    auto text_at = []( unsigned k ) { return 0x800u + 0x40u * k; };
    auto sym = [ & ]( std::uint32_t x ) -> unsigned {
        if ( x == 0 ) return 0;
        if ( x == obj ) return 1;
        if ( x == fac ) return 2;
        if ( x == asset ) return 3;
        if ( x == obj + 0x38 ) return 4;
        for ( unsigned k = 0; k < 3; ++k )
            if ( x == text_at( k ) ) return 5u + k;
        return 0x100 + ( x & 0xFF );
    };
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned kind = *g++, flags = *g++, held = *g++, untouched = *g++, nt = *g++;
        std::map< std::uint32_t, std::uint32_t > mem;
        asset_words_t w;
        w.get = [ & ]( std::uint32_t a ) { auto it = mem.find( a ); return it == mem.end( ) ? 0x5A5A5A5Au : it->second; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ a ] = x; };
        mem[ obj + 0x64 ] = held == 3 ? asset : 0u;
        for ( unsigned k = 0; k < nt; ++k )
        {
            const unsigned n = *g++;
            const unsigned first = n != 0 ? *g : 0u;
            g += ( n + 3 ) / 4;
            const std::uint32_t at = obj + offs[ kind - 1 ][ k ];
            mem[ at ] = n < 16 ? first : text_at( k );
            mem[ at + 0x10 ] = n;
            mem[ at + 0x14 ] = n < 16 ? 15u : ( n | 15u );
        }
        std::vector< std::vector< unsigned > > glog( *g++ ), log;
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        const unsigned* after = g;
        g += 3 * nt + 2;
        asset_dtor_hooks_t h;
        h.free_block = [ & ]( std::uint32_t a ) { log.push_back( { 1u, sym( a ) } ); };
        h.json_clear = [ & ]( std::uint32_t a ) { log.push_back( { 2u, sym( a ) } ); };
        h.factory_release = [ & ]( std::uint32_t a ) { log.push_back( { 3u, sym( fac ), sym( a ) } ); };
        h.property_base_dtor = [ & ]( ) { log.push_back( { 4u, 1u } ); };
        h.entity_dtor = [ & ]( ) { log.push_back( { 5u, 1u } ); };
        const asset_dtor_kind_t k = kind == 1 ? asset_dtor_kind_t::asset
                                  : kind == 2 ? asset_dtor_kind_t::asset_property : asset_dtor_kind_t::substitute_entity;
        const std::uint32_t r = asset_destroy( k, w, obj, flags, h );
        bool ok = log == glog && untouched == 1;
        for ( unsigned s = 0; s < nt && ok; ++s )
        {
            const std::uint32_t at = obj + offs[ kind - 1 ][ s ];
            ok = ( w.get( at ) & 0xFFu ) == after[ 3 * s ] && w.get( at + 0x10 ) == after[ 3 * s + 1 ] && w.get( at + 0x14 ) == after[ 3 * s + 2 ];
        }
        // +0: the stored vtable; the substitute entity stores none (the image's random word, not compared).
        ok = ok && ( kind == 3 || w.get( obj ) == after[ 3 * nt ] ) && ( r == obj ? 1u : 0u ) == after[ 3 * nt + 1 ];
        if ( !ok && ++failed <= 4 )
            LOG_ERROR( "asset dtor vector %u (kind %u flags %u) differs from retail: %d log entries, retail %d", v, kind, flags,
                static_cast< int >( log.size( ) ), static_cast< int >( glog.size( ) ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "asset_destroy matches retail's 0x515D70 / 0x53FD10 / 0x5F4AB0 (bodies 0x515D00 / 0x53FD40 / 0x5F4AE0) on %u images: the hook log, every string, +0, eax", nv );
    else
        LOG_ERROR( "asset_destroy FAILED %d of %u against 0x515D70 / 0x53FD10 / 0x5F4AB0", failed, nv );
    return failed == 0;
}

// asset_destroy's class dtors (engine assets/factory.hxx) against retail's 0x515E10 / 0x554E90 / 0x55C1E0 / 0x55C230 /
// 0x5C8C70 (+ 0x5C9D20, body 0x515D00) run whole on images (case asset_dtors2, cases/package.py).
inline bool verify_asset_dtors2( )
{
    const unsigned* g = g_golden_asset_dtors2_words;
    const std::uint32_t obj = 0x100, anim = 0x600, events = 0x700;
    auto text_at = []( unsigned k ) { return 0xC00u + 0x40u * k; };
    auto sym = [ & ]( std::uint32_t x ) -> unsigned {
        if ( x == 0 ) return 0;
        if ( x == obj ) return 1;
        if ( x == anim ) return 2;
        if ( x == events ) return 3;
        for ( unsigned k = 0; k < 8; ++k )
            if ( x == text_at( k ) ) return 5u + k;
        for ( std::uint32_t o : { 0x38u, 0x58u, 0x68u } )
            if ( x == obj + o ) return 0x200u + o;
        for ( unsigned k = 0; k < 4; ++k )
            if ( x == events + 0x30u * k + 0x20u ) return 0x300u + k;
        return 0x100 + ( x & 0xFF );
    };
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned kind = *g++, flags = *g++, held = *g++, refs0 = *g++;
        const int n_ev = static_cast< int >( *g++ );
        const unsigned untouched = *g++;
        const unsigned init[ 3 ] = { g[ 0 ], g[ 1 ], g[ 2 ] };
        g += 3;
        const unsigned nt = *g++;
        std::map< std::uint32_t, std::uint32_t > mem;
        asset_words_t w;
        w.get = [ & ]( std::uint32_t a ) { auto it = mem.find( a ); return it == mem.end( ) ? 0x5A5A5A5Au : it->second; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ a ] = x; };
        mem[ anim + 4 ] = refs0;
        for ( unsigned k = 0; k < 3; ++k )
            mem[ obj + 0x58 + 4 * k ] = init[ k ];
        if ( kind == 3 )
            mem[ obj + 0x58 ] = held == 2 ? anim : 0u;
        std::vector< std::uint32_t > at( nt );
        for ( unsigned k = 0; k < nt; ++k )
        {
            at[ k ] = k == 0 ? obj + 0x20 : k == 1 ? obj + 0x8 : events + 0x30u * ( k - 2 ) + 4u;
            const unsigned n = *g++;
            const unsigned first = n != 0 ? *g : 0u;
            g += ( n + 3 ) / 4;
            mem[ at[ k ] ] = n < 16 ? first : text_at( k );
            mem[ at[ k ] + 0x10 ] = n;
            mem[ at[ k ] + 0x14 ] = n < 16 ? 15u : ( n | 15u );
        }
        std::vector< std::vector< unsigned > > glog( *g++ ), log;
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        const unsigned* after = g;
        g += 3 * nt + 6;
        asset_dtor_hooks_t h;
        h.free_block = [ & ]( std::uint32_t a ) { log.push_back( { 1u, sym( a ) } ); };
        h.json_clear = [ & ]( std::uint32_t a ) { log.push_back( { 2u, sym( a ) } ); };
        h.object_delete = [ & ]( std::uint32_t a, std::uint32_t f ) { log.push_back( { 6u, sym( a ), f } ); };
        const std::uint32_t r = asset_destroy( static_cast< asset_dtor_kind_t >( kind ), w, obj, flags, h );
        bool ok = log == glog && untouched == 1 && ( n_ev < 0 || kind != 7 || nt == 2u + static_cast< unsigned >( n_ev ) );
        for ( unsigned s = 0; s < nt && ok; ++s )
            ok = ( w.get( at[ s ] ) & 0xFFu ) == after[ 3 * s ] && w.get( at[ s ] + 0x10 ) == after[ 3 * s + 1 ] && w.get( at[ s ] + 0x14 ) == after[ 3 * s + 2 ];
        const unsigned* tail = after + 3 * nt;
        ok = ok && w.get( obj ) == tail[ 0 ] && w.get( anim + 4 ) == tail[ 1 ] && sym( w.get( obj + 0x58 ) ) == tail[ 2 ]
            && sym( w.get( obj + 0x5C ) ) == tail[ 3 ] && sym( w.get( obj + 0x60 ) ) == tail[ 4 ] && ( r == obj ? 1u : 0u ) == tail[ 5 ];
        if ( !ok && ++failed <= 4 )
            LOG_ERROR( "asset class dtor vector %u (kind %u flags %u) differs from retail: %d log entries, retail %d", v, kind, flags,
                static_cast< int >( log.size( ) ), static_cast< int >( glog.size( ) ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "asset_destroy matches retail's 0x515E10 / 0x554E90 / 0x55C1E0 / 0x55C230 / 0x5C8C70 (+ 0x5C9D20) on %u images: the hook log, every string, +0, +0x58..+0x60, eax", nv );
    else
        LOG_ERROR( "asset_destroy (class dtors) FAILED %d of %u against 0x515E10 / 0x554E90 / 0x55C1E0 / 0x55C230 / 0x5C8C70", failed, nv );
    return failed == 0;
}

// asset_destroy( xml ) (engine assets/factory.hxx) against retail's VuXmlAsset dtor 0x5C78F0 (body 0x5C7850) run whole on
// images (case asset_xml_dtor, cases/package.py): the hook log, the strings, +0 / +0x58 / +0x70 / +0x74 / +0x8C, eax.
inline bool verify_asset_xml_dtor( )
{
    const unsigned* g = g_golden_asset_xml_dtor_words;
    const std::uint32_t obj = 0x100, nodes = 0x400, rep = 0x600;
    auto text_at = []( unsigned k ) { return 0x800u + 0x40u * k; };
    auto sym = [ & ]( std::uint32_t x ) -> unsigned {
        if ( x == 0 ) return 0;
        if ( x == obj ) return 1;
        if ( x == rep ) return 2;
        if ( x == 0x759104u ) return 3;
        if ( x == obj + 0x38 ) return 4;
        if ( x == obj + 0x58 ) return 5;
        if ( x == text_at( 0 ) ) return 6;
        if ( x == text_at( 1 ) ) return 7;
        for ( unsigned k = 0; k < 4; ++k )
            if ( x == nodes + 0x40u * k ) return 0x10u + k;
        return 0x100 + ( x & 0xFF );
    };
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned flags = *g++, n = *g++, rep_sym = *g++, untouched = *g++, nt = *g++;
        std::map< std::uint32_t, std::uint32_t > mem;
        asset_words_t w;
        w.get = [ & ]( std::uint32_t a ) { auto it = mem.find( a ); return it == mem.end( ) ? 0x5A5A5A5Au : it->second; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ a ] = x; };
        mem[ obj + 0x70 ] = n ? nodes : 0u;
        mem[ obj + 0x8C ] = rep_sym == 3 ? 0x759104u : rep;
        for ( unsigned j = 0; j < n; ++j )
            mem[ nodes + 0x40u * j + 0x28 ] = j + 1 < n ? nodes + 0x40u * ( j + 1 ) : 0u;
        const std::uint32_t at[ 2 ] = { obj + 0x20, obj + 0x8 };
        for ( unsigned k = 0; k < nt; ++k )
        {
            const unsigned len = *g++;
            const unsigned first = len != 0 ? *g : 0u;
            g += ( len + 3 ) / 4;
            mem[ at[ k ] ] = len < 16 ? first : text_at( k );
            mem[ at[ k ] + 0x10 ] = len;
            mem[ at[ k ] + 0x14 ] = len < 16 ? 15u : ( len | 15u );
        }
        std::vector< std::vector< unsigned > > glog( *g++ ), log;
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        const unsigned* after = g;
        g += 3 * nt + 6;
        asset_dtor_hooks_t h;
        h.free_block = [ & ]( std::uint32_t a ) { log.push_back( { 1u, sym( a ) } ); };
        h.json_clear = [ & ]( std::uint32_t a ) { log.push_back( { 2u, sym( a ) } ); };
        h.object_delete = [ & ]( std::uint32_t a, std::uint32_t f ) { log.push_back( { 6u, sym( a ), f } ); };
        h.free_array = [ & ]( std::uint32_t a ) { log.push_back( { 9u, sym( a ) } ); };
        h.xml_node_dtor = [ & ]( std::uint32_t a ) { log.push_back( { 10u, sym( a ) } ); };
        const std::uint32_t r = asset_destroy( asset_dtor_kind_t::xml, w, obj, flags, h );
        bool ok = log == glog && untouched == 1 && nt == 2;
        for ( unsigned s = 0; s < nt && ok; ++s )
            ok = ( w.get( at[ s ] ) & 0xFFu ) == after[ 3 * s ] && w.get( at[ s ] + 0x10 ) == after[ 3 * s + 1 ] && w.get( at[ s ] + 0x14 ) == after[ 3 * s + 2 ];
        const unsigned* tail = after + 3 * nt;
        ok = ok && w.get( obj ) == tail[ 0 ] && w.get( obj + 0x58 ) == tail[ 1 ] && sym( w.get( obj + 0x70 ) ) == tail[ 2 ]
            && sym( w.get( obj + 0x74 ) ) == tail[ 3 ] && sym( w.get( obj + 0x8C ) ) == tail[ 4 ] && ( r == obj ? 1u : 0u ) == tail[ 5 ];
        if ( !ok && ++failed <= 4 )
            LOG_ERROR( "xml asset dtor vector %u (%u children, flags %u) differs from retail: %d log entries, retail %d", v, n, flags,
                static_cast< int >( log.size( ) ), static_cast< int >( glog.size( ) ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "asset_destroy( xml ) matches retail's VuXmlAsset dtor 0x5C78F0 / 0x5C7850 on %u images: the hook log, every string, +0..+0x8C, eax", nv );
    else
        LOG_ERROR( "asset_destroy( xml ) FAILED %d of %u against 0x5C78F0", failed, nv );
    return failed == 0;
}

// asset_destroy( collision_mesh / fluids_mesh ) (engine assets/factory.hxx) against retail's 0x518D60 (+ unload 0x51B140) /
// 0x5226D0 run whole on images (case asset_dtors3, cases/package.py).
inline bool verify_asset_dtors3( )
{
    const unsigned* g = g_golden_asset_dtors3_words;
    const std::uint32_t obj = 0x100, bvh = 0x400;
    auto text_at = []( unsigned k ) { return 0x800u + 0x40u * k; };
    auto sym = [ & ]( std::uint32_t x ) -> unsigned {
        if ( x == 0 ) return 0;
        if ( x == obj ) return 1;
        if ( x == bvh ) return 2;
        if ( x == obj + 0x38 ) return 4;
        if ( x == text_at( 0 ) ) return 6;
        if ( x == text_at( 1 ) ) return 7;
        for ( unsigned k = 0; k < 8; ++k )
            if ( x == 0x1000u + 0x100u * k ) return 0x20u + k;
        return 0x100 + ( x & 0xFF );
    };
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned kind = *g++, flags = *g++, clear = *g++, counter = *g++, untouched = *g++;
        std::map< std::uint32_t, std::uint32_t > mem;
        asset_words_t w;
        w.get = [ & ]( std::uint32_t a ) { auto it = mem.find( a ); return it == mem.end( ) ? 0x5A5A5A5Au : it->second; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ a ] = x; };
        for ( std::uint32_t o = 0x58; o < 0xAC; o += 4 )
            mem[ obj + o ] = *g++;
        mem[ 0x759050u ] = counter;
        const unsigned nt = *g++;
        const std::uint32_t at[ 2 ] = { obj + 0x20, obj + 0x8 };
        for ( unsigned k = 0; k < nt && k < 2; ++k )
        {
            const unsigned len = *g++;
            const unsigned first = len != 0 ? *g : 0u;
            g += ( len + 3 ) / 4;
            mem[ at[ k ] ] = len < 16 ? first : text_at( k );
            mem[ at[ k ] + 0x10 ] = len;
            mem[ at[ k ] + 0x14 ] = len < 16 ? 15u : ( len | 15u );
        }
        std::vector< std::vector< unsigned > > glog( *g++ ), log;
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        const unsigned* after = g;
        g += 3 * nt + 8;
        asset_dtor_hooks_t h;
        h.free_block = [ & ]( std::uint32_t a ) { log.push_back( { 1u, sym( a ) } ); };
        h.json_clear = [ & ]( std::uint32_t a ) { log.push_back( { 2u, sym( a ) } ); };
        h.aligned_free = [ & ]( std::uint32_t a ) { log.push_back( { 13u, sym( a ) } ); };
        h.bt_free = [ & ]( std::uint32_t a ) { log.push_back( { 14u, sym( a ) } ); };
        h.object_delete = [ & ]( std::uint32_t a, std::uint32_t f ) {
            log.push_back( { 6u, sym( a ), f } );
            if ( clear )
                mem[ obj + 0xA8 ] = 0;
        };
        const std::uint32_t r = asset_destroy( static_cast< asset_dtor_kind_t >( kind ), w, obj, flags, h );
        bool ok = log == glog && untouched == 1 && nt == 2;
        for ( unsigned s = 0; s < nt && ok; ++s )
            ok = ( w.get( at[ s ] ) & 0xFFu ) == after[ 3 * s ] && w.get( at[ s ] + 0x10 ) == after[ 3 * s + 1 ] && w.get( at[ s ] + 0x14 ) == after[ 3 * s + 2 ];
        const unsigned* tail = after + 3 * nt;
        ok = ok && w.get( obj ) == tail[ 0 ] && w.get( obj + 0x5C ) == tail[ 1 ] && w.get( obj + 0x68 ) == tail[ 2 ] && w.get( obj + 0x74 ) == tail[ 3 ]
            && w.get( obj + 0x80 ) == tail[ 4 ] && sym( w.get( obj + 0xA8 ) ) == tail[ 5 ] && w.get( 0x759050u ) == tail[ 6 ] && ( r == obj ? 1u : 0u ) == tail[ 7 ];
        if ( !ok && ++failed <= 4 )
            LOG_ERROR( "mesh asset dtor vector %u (kind %u flags %u) differs from retail: %d log entries, retail %d", v, kind, flags,
                static_cast< int >( log.size( ) ), static_cast< int >( glog.size( ) ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "asset_destroy matches retail's 0x518D60 (+ 0x51B140) / 0x5226D0 on %u images: the hook log, every string, the counts, +0xA8, gNumAlignedFree, eax", nv );
    else
        LOG_ERROR( "asset_destroy (mesh dtors) FAILED %d of %u against 0x518D60 / 0x5226D0", failed, nv );
    return failed == 0;
}

// The asset properties' virtuals (engine assets/factory.hxx) against retail's 0x423860 / 0x423870 / 0x423890 / 0x506BB0 /
// 0x506BE0 / 0x506B30 run whole (case asset_props, cases/package.py): eax, the hook log, +0 after the dtor.
inline bool verify_asset_props( )
{
    const unsigned* g = g_golden_asset_props_words;
    const std::uint32_t prop = 0x100, vec = 0x200, names = 0x400, asset = 0x1000, cont = 0x1100, cmap = 0x1140, key = 0x1180, fac = 0x1200;
    auto sym = [ & ]( std::uint32_t x ) -> unsigned {
        if ( x == 0 ) return 0;
        if ( x == prop ) return 1;
        if ( x == asset ) return 2;
        if ( x == asset + 0x58 ) return 3;
        if ( x == cont ) return 4;
        if ( x == key ) return 5;
        if ( x == fac ) return 6;
        return 0x100 + ( x & 0xFF );
    };
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned fn = *g++, arg = *g++, eax = *g++, word0 = *g++, held = *g++, kind = *g++, size = *g++, span = *g++;
        const unsigned nt = *g++;
        std::map< std::uint32_t, std::uint32_t > mem;
        asset_words_t w;
        w.get = [ & ]( std::uint32_t a ) { auto it = mem.find( a ); return it == mem.end( ) ? 0x5A5A5A5Au : it->second; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ a ] = x; };
        for ( unsigned k = 0; k < nt; ++k )
        {
            const std::uint32_t at = fn <= 2 ? names + 24u * k : key;
            const std::uint32_t heap = fn <= 2 ? 0x800u + 0x40u * k : 0x1300u;
            const unsigned n = *g++;
            const unsigned first = n != 0 ? *g : 0u;
            g += ( n + 3 ) / 4;
            mem[ at ] = n < 16 ? first : heap;
            mem[ at + 0x10 ] = n;
            mem[ at + 0x14 ] = n < 16 ? 15u : ( n | 15u );
        }
        std::vector< std::vector< unsigned > > glog( *g++ ), log;
        for ( auto& e : glog )
        {
            e.assign( g + 1, g + 1 + *g );
            g += 1 + e.size( );
        }
        if ( fn <= 2 )
        {
            mem[ prop + 0x48 ] = vec;
            mem[ vec ] = names;
            mem[ vec + 4 ] = names + span;
        }
        else
        {
            mem[ prop + 0x48 ] = held == 2 ? asset : 0u;
            mem[ cont ] = kind;
            mem[ cont + 8 ] = cmap;
            mem[ cmap + 4 ] = size;
        }
        asset_property_hooks_t ph;
        ph.json_member = [ & ]( std::uint32_t c, const char* k ) {
            log.push_back( { 7u, sym( c ), std::string( k ) == "VuDBAsset" ? 1u : 0u } );
            return cont;
        };
        ph.json_key = [ & ]( std::uint32_t c, std::uint32_t i ) {
            log.push_back( { 8u, sym( c ), i } );
            return key;
        };
        asset_dtor_hooks_t dh;
        dh.free_block = [ & ]( std::uint32_t a ) { log.push_back( { 1u, sym( a ) } ); };
        dh.factory_release = [ & ]( std::uint32_t a ) { log.push_back( { 3u, sym( fac ), sym( a ) } ); };
        dh.property_base_dtor = [ & ]( ) { log.push_back( { 4u, 1u } ); };
        auto ptr = [ & ]( std::uint32_t r, std::uint32_t self_at ) -> unsigned { return r == 0 ? 0u : r == self_at ? 1u : 2u; };
        unsigned r = 0;
        bool ok = true;
        switch ( fn )
        {
        case 0: r = asset_name_property_type( ); break;
        case 1: r = static_cast< unsigned >( asset_name_property_count( w, prop ) ); break;
        case 2: r = ptr( asset_name_property_choice( w, prop, static_cast< std::int32_t >( arg ) ), names + 24u * arg ); break;
        case 3: r = db_entry_property_count( w, prop, ph ); break;
        case 4: r = ptr( db_entry_property_choice( w, prop, arg, ph ), key ); break;
        default:
            r = sym( db_entry_property_destroy( w, prop, arg, dh ) );
            ok = w.get( prop ) == word0;
            break;
        }
        ok = ok && r == eax && log == glog;
        if ( !ok && ++failed <= 4 )
            LOG_ERROR( "asset property vector %u (virtual %u, argument %u) differs from retail: eax %u, retail %u", v, fn, arg, r, eax );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "the asset properties' virtuals match retail's 0x423860 / 0x423870 / 0x423890 / 0x506BB0 / 0x506BE0 / 0x506B30 on %u calls: eax, the hook log, +0", nv );
    else
        LOG_ERROR( "asset property virtuals FAILED %d of %u", failed, nv );
    return failed == 0;
}

// asset_create (engine assets/factory.hxx) against retail's create functions 0x515E90 / 0x554ED0 / 0x55C280 / 0x5644E0 /
// 0x5A9800 / 0x5BB0E0 run whole (case asset_creates, cases/package.py): the size asked, eax, all 32 words of the block.
inline bool verify_asset_creates( )
{
    const unsigned* g = g_golden_asset_creates_words;
    const std::uint32_t block = 0x1000;
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned kind = *g++, null = *g++, size = *g++, eax = *g++;
        const unsigned* fill = g;
        const unsigned* after = g + 32;
        g += 64;
        std::uint32_t mem[ 32 ];
        std::memcpy( mem, fill, sizeof( mem ) );
        asset_words_t w;
        w.get = [ & ]( std::uint32_t a ) { return mem[ ( a - block ) / 4 ]; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ ( a - block ) / 4 ] = x; };
        std::vector< std::uint32_t > asked;
        const std::uint32_t r = asset_create( static_cast< asset_create_kind_t >( kind ), w, [ & ]( std::uint32_t n ) {
            asked.push_back( n );
            return null ? 0u : block;
        } );
        bool ok = asked.size( ) == 1 && asked[ 0 ] == size && ( r == block ? 1u : 0u ) == eax && ( r == 0 || r == block );
        for ( int i = 0; i < 32 && ok; ++i )
            ok = mem[ i ] == after[ i ];
        if ( !ok && ++failed <= 4 )
            LOG_ERROR( "asset create vector %u (kind %u null %u) differs from retail", v, kind, null );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "asset_create matches retail's 0x515E90 / 0x554ED0 / 0x55C280 / 0x5644E0 / 0x5A9800 / 0x5BB0E0 on %u blocks: the size, eax, every word", nv );
    else
        LOG_ERROR( "asset_create FAILED %d of %u against the create functions", failed, nv );
    return failed == 0;
}

// asset_create (engine assets/factory.hxx) against retail's 0x518EC0 / 0x5C7920 / 0x5C8CD0 run whole (case asset_creates2):
// the hook log (new's size, TiXmlDocument's ctor), eax, all 44 words of the block (+0x94 / +0xA4: the stack word).
inline bool verify_asset_creates2( )
{
    const unsigned* g = g_golden_asset_creates2_words;
    const std::uint32_t block = 0x1000;
    const unsigned nv = *g++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned kind = *g++, null = *g++, eax = *g++, stack = *g++, nlog = *g++;
        std::vector< unsigned > glog( g, g + 2 * nlog ), log;
        g += 2 * nlog;
        const unsigned* fill = g;
        const unsigned* after = g + 44;
        g += 88;
        std::uint32_t mem[ 44 ];
        std::memcpy( mem, fill, sizeof( mem ) );
        asset_words_t w;
        w.get = [ & ]( std::uint32_t a ) { return mem[ ( a - block ) / 4 ]; };
        w.put = [ & ]( std::uint32_t a, std::uint32_t x ) { mem[ ( a - block ) / 4 ] = x; };
        asset_create_hooks_t h;
        h.alloc = [ & ]( std::uint32_t n ) { log.insert( log.end( ), { 11u, n } ); return null ? 0u : block; };
        h.xml_document_ctor = [ & ]( std::uint32_t a ) { log.insert( log.end( ), { 12u, a - block } ); };
        h.stack_word = [ & ]( ) { return stack; };
        const std::uint32_t r = asset_create( static_cast< asset_create_kind_t >( kind ), w, h );
        bool ok = log == glog && ( r == block ? 1u : 0u ) == eax && ( r == 0 || r == block );
        for ( int i = 0; i < 44 && ok; ++i )
            ok = mem[ i ] == after[ i ];
        if ( !ok && ++failed <= 4 )
            LOG_ERROR( "asset create vector %u (kind %u null %u) differs from retail", v, kind, null );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "asset_create matches retail's 0x518EC0 / 0x5C7920 / 0x5C8CD0 on %u blocks: the hook log, eax, every word", nv );
    else
        LOG_ERROR( "asset_create (2) FAILED %d of %u against 0x518EC0 / 0x5C7920 / 0x5C8CD0", failed, nv );
    return failed == 0;
}

inline bool verify_asset_package( )
{
    const std::string pack = package_check_pack( );
    if ( pack.empty( ) )
    {
        LOG_ERROR( "verify_asset_package: Base.apf not found" );
        return false;
    }
    int failed = 0;
    for ( std::size_t i = 0; i < g_golden_asset_package_load_count; ++i )
    {
        const golden_asset_package_load_t& g = g_golden_asset_package_loads[ i ];
        unsigned char header[ 68 ]{};
        for ( int b = 0; b < 68; ++b )
        {
            auto nib = []( char ch ) { return ch <= '9' ? ch - '0' : ch - 'a' + 10; };
            header[ b ] = static_cast< unsigned char >( nib( g.m_header[ 2 * b ] ) * 16 + nib( g.m_header[ 2 * b + 1 ] ) );
        }
        package_check_t c;
        c.pack = pack;
        c_asset_factory f;
        f.register_all_types( );
        package_check_hooks( f, c );
        f.hooks.pack_header = header;
        const unsigned eax = f.load_package( "Base" ) ? 1u : 0u;
        int bad = 0;
        if ( eax != g.m_eax || c.packages.size( ) != g.m_listen || f.package_count( ) != g.m_packages || f.unknown )
            ++bad;
        if ( f.package_count( ) != 0 )
        {
            const asset_package_t& p = f.package_at( 0 );
            if ( p.sku != g.m_sku || p.words[ 0 ] != g.m_words[ 0 ] || p.words[ 1 ] != g.m_words[ 1 ] || p.words[ 2 ] != g.m_words[ 2 ]
                 || p.pack.pack_records( ) != g.m_directory )
                ++bad;
        }
        if ( bad != 0 )
            LOG_ERROR( "asset_package load '%s': ours %u (listeners %d, packages %d, unknown %d), retail %u (%u, %u)", g.m_name, eax,
                static_cast< int >( c.packages.size( ) ), static_cast< int >( f.package_count( ) ), f.unknown ? 1 : 0, g.m_eax, g.m_listen,
                g.m_packages );
        bad += package_check_snapshot( f, g.m_type_at, g.m_type_count, g.m_record_at, g.m_record_count, g.m_name );
        if ( i == 0 )
        {
            c.packages.clear( );
            const unsigned again = f.load_package( "Base" ) ? 1u : 0u;
            if ( again != g_golden_asset_package_again[ 0 ] || c.packages.size( ) != g_golden_asset_package_again[ 1 ]
                 || f.package_count( ) != g_golden_asset_package_again[ 2 ] )
            {
                ++bad;
                LOG_ERROR( "asset_package: the second load of Base differs" );
            }
            bad += package_check_ops( f, c );
            bad += package_check_snapshot( f, g_golden_asset_package_after[ 0 ], g_golden_asset_package_after[ 1 ],
                g_golden_asset_package_after[ 2 ], g_golden_asset_package_after[ 3 ], "after the ops" );
            if ( f.unknown )
            {
                ++bad;
                LOG_ERROR( "asset_package: a path not recreated was reached in the ops" );
            }
        }
        for ( vu_asset_t* a : c.assets )
            delete a;
        failed += bad != 0 ? 1 : 0;
    }
    if ( failed == 0 )
        LOG_SUCCESS( "the package database matches retail's 0x4EFA50 / 0x4F0090 / 0x567760 / 0x5679F0 / 0x4F1DC0 / 0x4F0710 and createAsset's "
                     "miss path (0x4F1CF0 / 0x55FC50 / 0x4F08F0) on the real Base.apf: %d header variants, %d ops",
            static_cast< int >( g_golden_asset_package_load_count ), static_cast< int >( g_golden_asset_package_op_count ) );
    else
        LOG_ERROR( "the package database FAILED %d of %d load variants against retail", failed,
            static_cast< int >( g_golden_asset_package_load_count ) );
    return failed == 0;
}

// VuAssetFactoryImpl's remaining slots against retail's (case asset_slots, boat batch 30): the same op list replayed on
// c_asset_factory over the real Base.apf (Extra*.apf = the same file, Bad.apf a bad magic). Every op's answer text and
// event log must match. Slots 25 / 37's asset lists are compared sorted (head comment of factory.hxx).
struct asset_slots_state_t
{
    std::string pack;
    unsigned char bad[ 68 ]{};
    c_asset_factory* factory = nullptr;
    std::vector< vu_asset_t* > assets;
    std::string events, game, lang;
    bool has_game = false;
    std::vector< std::size_t > reloads;

    std::size_t index_of( const vu_asset_t* a ) const
    {
        return static_cast< std::size_t >( std::find( assets.begin( ), assets.end( ), a ) - assets.begin( ) );
    }
};

inline std::string asset_slots_list( std::vector< std::size_t > v )
{
    std::sort( v.begin( ), v.end( ) );
    std::string out;
    for ( std::size_t k = 0; k < v.size( ); ++k )
        out += ( k != 0 ? "," : "" ) + std::to_string( v[ k ] );
    return out;
}

inline void asset_slots_hooks( c_asset_factory& f, asset_slots_state_t& c )
{
    f.hooks.user = &c;
    f.hooks.pack_path = []( void* u, const std::string& file ) {
        auto* c = static_cast< asset_slots_state_t* >( u );
        if ( file != "Base.apf" && file != "Extra.apf" && file != "Extra2.apf" && file != "Extra3.apf" && file != "Bad.apf" )
            return std::string( );
        c->factory->hooks.pack_header = file == "Bad.apf" ? c->bad : nullptr;
        return c->pack;
    };
    f.hooks.create = []( void* u, const std::string& ) {
        auto* c = static_cast< asset_slots_state_t* >( u );
        c->assets.push_back( new vu_asset_t );
        return c->assets.back( );
    };
    f.hooks.load_data = []( void* u, vu_asset_t& a, const std::vector< std::uint8_t >& data ) {
        auto* c = static_cast< asset_slots_state_t* >( u );
        char text[ 64 ];
        std::snprintf( text, sizeof( text ), "L%u:%u:%x;", static_cast< unsigned >( c->index_of( &a ) ), static_cast< unsigned >( data.size( ) ),
            c_archive::fnv( data.data( ), data.size( ) ) );
        c->events += text;
        return true;
    };
    f.hooks.patch = []( void* u, vu_asset_t& a, vu_asset_t& from ) {
        auto* c = static_cast< asset_slots_state_t* >( u );
        c->events += "P" + std::to_string( c->index_of( &a ) ) + ":" + std::to_string( c->index_of( &from ) ) + ";";
        return true;
    };
    f.hooks.destroy = []( void* u, vu_asset_t* a ) {
        auto* c = static_cast< asset_slots_state_t* >( u );
        c->events += "D" + std::to_string( c->index_of( a ) ) + ";";
    };
    f.hooks.reload = []( void* u, vu_asset_t& a ) {
        auto* c = static_cast< asset_slots_state_t* >( u );
        c->reloads.push_back( c->index_of( &a ) );
    };
    f.hooks.language = []( void* u ) { return static_cast< asset_slots_state_t* >( u )->lang; };
    f.hooks.game_info = []( void* u, value_t& document, bool& parsed ) {
        auto* c = static_cast< asset_slots_state_t* >( u );
        if ( !c->has_game )
            return false;
        json_text_reader_t r;
        parsed = json_read_text( r, document, c->game.c_str( ) ) != 0;
        return true;
    };
}

inline bool verify_asset_slots( )
{
    const std::string pack = package_check_pack( );
    if ( pack.empty( ) )
    {
        LOG_ERROR( "verify_asset_slots: Base.apf not found" );
        return false;
    }
    asset_slots_state_t c;
    c.pack = pack;
{
        std::ifstream file( pack, std::ios::binary );
        file.read( reinterpret_cast< char* >( c.bad ), sizeof( c.bad ) );
    }
    std::memcpy( c.bad, "VUPG", 4 );
    const std::uint32_t head_hash = c_archive::fnv( c.bad, 0x40 );
    std::memcpy( c.bad + 0x40, &head_hash, 4 );

    const unsigned* w = g_golden_asset_slots_words;
    const auto text = [ & ]( ) {
        const unsigned n = *w++;
        std::string out( reinterpret_cast< const char* >( w ), n );
        w += ( n + 3 ) / 4;
        return out;
    };
    const unsigned rows = *w++;
    std::unique_ptr< c_asset_factory > f;
    asset_listener_t l[ 2 ];
    for ( int k = 0; k < 2; ++k )
    {
        l[ k ].user = reinterpret_cast< void* >( static_cast< std::intptr_t >( k ) );
    }
    static asset_slots_state_t* s_c;
    s_c = &c;
    l[ 0 ].package_loaded = []( void*, const std::string& n ) { s_c->events += "p0:" + n + ";"; };
    l[ 1 ].package_loaded = []( void*, const std::string& n ) { s_c->events += "p1:" + n + ";"; };
    l[ 0 ].created = []( void*, vu_asset_t& a ) { s_c->events += "c0:" + std::to_string( s_c->index_of( &a ) ) + ";"; };
    l[ 1 ].created = []( void*, vu_asset_t& a ) { s_c->events += "c1:" + std::to_string( s_c->index_of( &a ) ) + ";"; };
    l[ 0 ].unloaded = []( void* ) { s_c->events += "u0;"; };
    l[ 1 ].unloaded = []( void* ) { s_c->events += "u1;"; };

    int failed = 0;
    for ( unsigned i = 0; i < rows; ++i )
    {
        ( void )*w++;
        const std::string op = text( ), a = text( ), b = text( ), cc = text( ), d = text( ), want = text( ), want_events = text( );
        std::string got = "0";
        const auto flag = []( bool v ) { return std::string( v ? "1" : "0" ); };
        const auto dump = []( const value_t& v ) {
            std::string t;
            vujb_dump( v, t );
            return t;
        };
        if ( op == "init" )
        {
            f = std::make_unique< c_asset_factory >( );
            for ( vu_asset_t* x : c.assets )
                delete x;
            c.assets.clear( );
            c.factory = f.get( );
            c.has_game = a != "\x01";
            c.game = c.has_game ? a : std::string( );
            c.lang = cc;
            asset_slots_hooks( *f, c );
            got = flag( f->init( []( void* ) { s_c->events += "cb;"; }, nullptr, b ) );
        }
        else if ( op == "listeners" )
        {
            f->add_listener( &l[ 0 ] );
            f->add_listener( &l[ 1 ] );
            f->add_listener( &l[ 0 ] );
            for ( const char* t : { "VuDBAsset", "VuFontAsset", "VuTemplateAsset" } )
                f->register_type( t, 1u, 0u, 1u, true );
        }
        else if ( op == "exists" )
            got = flag( f->exists( a, b ) );
        else if ( op == "used" )
            got = flag( f->used( a, b ) );
        else if ( op == "base_lists" )
            got = flag( f->base_lists( a, b ) );
        else if ( op == "info" )
            got = dump( f->info( a, b, cc ) );
        else if ( op == "info_in" )
            got = dump( f->info_in( a, b, cc, d ) );
        else if ( op == "names" )
        {
            std::string all;
            const auto& n = f->names( a );
            for ( const std::string& x : n )
                all.append( x.c_str( ), x.size( ) + 1 );
            char t[ 32 ];
            std::snprintf( t, sizeof( t ), "%u:%x", static_cast< unsigned >( n.size( ) ), c_archive::fnv( reinterpret_cast< const unsigned char* >( all.data( ) ), all.size( ) ) );
            got = t;
        }
        else if ( op == "category" )
            got = std::to_string( f->category( a ) );
        else if ( op == "count" )
            got = std::to_string( f->package_count( ) );
        else if ( op == "package" )
        {
            const asset_package_t& p = f->package_at( static_cast< std::size_t >( std::atoi( a.c_str( ) ) ) );
            char t[ 32 ];
            std::snprintf( t, sizeof( t ), "|%x|%x", p.words[ 0 ], p.words[ 1 ] );
            got = p.name + t;
        }
        else if ( op == "mask" )
            got = std::to_string( f->license_mask( a ) );
        else if ( op == "skus" )
            got = dump( f->game_skus( ) );
        else if ( op == "packages" )
            got = dump( f->game_packages( ) );
        else if ( op == "create" )
        {
            vu_asset_t* made = f->create( a, b );
            got = std::to_string( made != nullptr ? static_cast< int >( c.index_of( made ) ) : -1 );
        }
        else if ( op == "remove_listener" )
            f->remove_listener( &l[ 0 ] );
        else if ( op == "forget" )
            f->forget( a, b );
        else if ( op == "loaded" )
        {
            std::vector< vu_asset_t* > v;
            f->loaded( v );
            std::vector< std::size_t > ids;
            for ( vu_asset_t* x : v )
                ids.push_back( c.index_of( x ) );
            got = asset_slots_list( ids );
        }
        else if ( op == "install" )
            got = flag( f->load_installed( ) );
        else if ( op == "set_sku" )
            f->set_sku_all( a );
        else if ( op == "sku" )
            got = f->sku( ) + "|" + f->package_at( 0 ).sku;
        else if ( op == "unload" )
        {
            c.reloads.clear( );
            f->unload_packages( );
            got = asset_slots_list( c.reloads );
        }
        else if ( op == "reset" )
            got = flag( f->reset( ) );
        else
            got = "?" + op;
        if ( got != want || c.events != want_events )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "asset_slots row %u %s( %s, %s ): ours '%s' [%s], retail '%s' [%s]", i, op.c_str( ), a.substr( 0, 24 ).c_str( ),
                    b.c_str( ), got.substr( 0, 80 ).c_str( ), c.events.substr( 0, 80 ).c_str( ), want.substr( 0, 80 ).c_str( ), want_events.substr( 0, 80 ).c_str( ) );
        }
        c.events.clear( );
    }
    for ( vu_asset_t* x : c.assets )
        delete x;
    if ( failed == 0 )
        LOG_SUCCESS( "VuAssetFactoryImpl's slots 5 10 12-16 24-26 28-30 32-41 match retail's on the real Base.apf: %u ops", rows );
    else
        LOG_ERROR( "VuAssetFactoryImpl's slots FAILED %d of %u", failed, rows );
    return failed == 0;
}

// project_asset_load and timed_events_load against retail's slot 5s on real pack records (case asset_loads, boat batch 30).
inline bool verify_asset_loads( )
{
    c_archive archive;
    const std::string pack = package_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "verify_asset_loads: Base.apf not readable" );
        return false;
    }
    const unsigned* w = g_golden_asset_loads_words;
    const auto text = [ & ]( ) {
        const unsigned n = *w++;
        std::string out( reinterpret_cast< const char* >( w ), n );
        w += ( n + 3 ) / 4;
        return out;
    };
    const auto hash_of = []( const value_t& v ) {
        std::string t;
        vujb_dump( v, t );
        return c_archive::fnv( reinterpret_cast< const unsigned char* >( t.data( ) ), t.size( ) );
    };
    const unsigned n = *w++;
    int failed = 0;
    for ( unsigned i = 0; i < n; ++i )
    {
        const std::string name = text( );
        const unsigned broken = *w++, eax = *w++, pos = *w++, word = *w++, doc = *w++, events = *w++;
        std::vector< std::uint8_t > bytes;
        bool bad = !archive.read( name.c_str( ), bytes );
        if ( broken != 0 )
        {
            static const char magic[ ] = "VUJB";
            auto at = std::search( bytes.begin( ), bytes.end( ), magic, magic + 4 );
            if ( at != bytes.end( ) )
                at[ 3 ] = 'X';
        }
        if ( name.rfind( "VuProject", 0 ) == 0 )
        {
            std::size_t position = 0;
            value_t document;
            std::uint32_t got = 0;
            const bool ok = project_asset_load( bytes, position, document, got );
            bad = bad || ( ok ? 1u : 0u ) != eax || position != pos || got != word || ( ok && hash_of( document ) != doc );
        }
        else
        {
            std::vector< timed_event_t > out;
            const bool ok = timed_events_load( bytes, out );
            bad = bad || ( ok ? 1u : 0u ) != eax || ( ok && out.size( ) != events );
            for ( unsigned e = 0; e < events; ++e )
            {
                const unsigned time = *w++;
                const std::string event = text( );
                const unsigned hash = *w++;
                if ( e < out.size( ) )
                {
                    unsigned bits;
                    std::memcpy( &bits, &out[ e ].time, 4 );
                    bad = bad || bits != time || out[ e ].name != event || hash_of( out[ e ].params ) != hash;
                }
            }
        }
        if ( bad && ++failed <= 6 )
            LOG_ERROR( "asset_loads: %s (broken %u) differs (retail eax %u, position %u)", name.c_str( ), broken, eax, pos );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuProjectAsset's load 0x5551B0 and VuTimedEventAsset's load 0x5C95B0 match retail on %u real records", n );
    else
        LOG_ERROR( "asset loads FAILED %d of %u", failed, n );
    return failed == 0;
}

#include <workspace/utility/checks/input.hxx>
#include <workspace/utility/checks/foliage.hxx>
#include <workspace/utility/checks/save.hxx>
