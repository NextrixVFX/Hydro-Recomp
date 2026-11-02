#pragma once

#include <workspace/core/world/hudpass.hxx>
#include <workspace/core/world/mode.hxx>

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

// The UI records' draws against retail (world batch 37; tools oracle/cases/hudpass.py): each row is one callback on one record, its full
// device log compared word by word.
struct ui_record_check_device_t final : ui_record_device_t
{
    std::vector< std::uint32_t > m_log;
    const std::uint8_t* m_record = nullptr;

    static std::uint32_t name_id( const char* name )
    {
        for ( std::uint32_t k = 0; k < sizeof( g_golden_urd_names ) / sizeof( g_golden_urd_names[ 0 ] ); ++k )
            if ( std::strcmp( g_golden_urd_names[ k ], name ) == 0 )
                return k + 1;
        return 0;
    }
    void constant( std::uint32_t kind, int set, const char* name, const void* at )
    {
        m_log.insert( m_log.end( ), { 4u, kind, static_cast< std::uint32_t >( set ), name_id( name ), static_cast< std::uint32_t >( static_cast< const std::uint8_t* >( at ) - m_record ) } );
    }
    void colour( int set, const char* name, const std::uint8_t* rgba ) override { constant( 1u, set, name, rgba ); }
    void matrix( int set, const char* name, const float* m ) override
    {
        // The callback takes a copy here; retail hands the record's own address: the offset is the callback's constant.
        const std::uint32_t offset = m_callback == 0u ? 4u : 0u;
        std::uint32_t same = std::memcmp( m, m_record + offset, 64 ) == 0 ? offset : 0xFFFFFFFFu;
        m_log.insert( m_log.end( ), { 4u, 2u, static_cast< std::uint32_t >( set ), name_id( name ), same } );
    }
    void texture( std::uint32_t slots, std::uint32_t handle ) override { m_log.insert( m_log.end( ), { 3u, 3u, slots, handle } ); }
    void draw( std::uint32_t kind, std::uint32_t primitives, const void* vertices, std::uint32_t stride ) override
    {
        const std::uint32_t count = kind == 4u ? primitives + 2u : kind == 2u ? primitives + 1u : 0u;
        const std::uint32_t words = count * stride / 4u;
        m_log.insert( m_log.end( ), { 5u + words, 4u, kind, primitives, stride, words } );
        const std::size_t at = m_log.size( );
        m_log.resize( at + words );
        std::memcpy( m_log.data( ) + at, vertices, words * 4u );
    }
    std::uint32_t m_callback = 0;
};

inline bool verify_ui_record_draws( )
{
    anim_cursor_t g;
    g.w = g_golden_urd_words;
    g.size = g_golden_urd_size;
    g.where = "ui_record_draws";
    const unsigned rows = g.take( );
    unsigned calls = 0;
    for ( unsigned k = 0; k < rows; ++k )
    {
        const unsigned length = g.take( );
        std::vector< std::uint32_t > row( length );
        for ( unsigned i = 0; i < length; ++i )
            row[ i ] = g.take( );
        const std::uint32_t callback = row[ 0 ], words = row[ 1 ];
        std::vector< std::uint8_t > record( words * 4u );
        std::memcpy( record.data( ), row.data( ) + 2, words * 4u );
        ui_record_check_device_t d;
        d.m_record = record.data( );
        d.m_callback = callback;
        if ( callback == 0u )
            ui_record_texture( d, record.data( ) );
        else if ( callback == 1u )
            ui_record_solid( d, record.data( ) );
        else
            ui_record_outline( d, record.data( ) );
        const std::uint32_t entries = row[ 2 + words ];
        calls += entries;
        const std::size_t expect = length - ( 3u + words );
        g.eq( static_cast< std::uint32_t >( d.m_log.size( ) ), static_cast< std::uint32_t >( expect ) );
        g.eq( d.m_log.size( ) == expect && std::memcmp( d.m_log.data( ), row.data( ) + 3 + words, expect * 4u ) == 0 ? 1u : 0u, 1u );
    }
    if ( !g.failed )
    {
        LOG_SUCCESS( "ui_record_draws matches retail's 0x507430 0x5078E0 0x507AB0 on %u records, %u device calls", rows, calls );
        return true;
    }
    LOG_ERROR( "ui_record_draws FAILED %d of %d words", g.failed, g.compared );
    return false;
}

// 0x42C290 -> 0x42C2A0 against retail (case boost_record): row: the entity's fields, the record, then the device log.
struct ui_boost_check_device_t final : ui_boost_device_t
{
    std::vector< std::uint32_t > m_log;
    void boost_matrix( const float* m ) override
    {
        m_log.push_back( 1u );
        for ( int k = 0; k < 16; ++k )
            m_log.push_back( std::bit_cast< std::uint32_t >( m[ k ] ) );
    }
    void boost_float( float v ) override { m_log.insert( m_log.end( ), { 2u, std::bit_cast< std::uint32_t >( v ) } ); }
    void boost_texture( bool mask, std::uint32_t handle ) override { m_log.insert( m_log.end( ), { 3u, mask ? 0x94u : 0x90u, handle } ); }
    void boost_draw( std::uint32_t kind, std::uint32_t primitives, const float* vertices, std::uint32_t stride ) override
    {
        m_log.insert( m_log.end( ), { 4u, kind, primitives, stride } );
        for ( int k = 0; k < 16; ++k )
            m_log.push_back( std::bit_cast< std::uint32_t >( vertices[ k ] ) );
    }
};

inline bool verify_boost_record( )
{
    anim_cursor_t g;
    g.w = g_golden_brd_words;
    g.size = g_golden_brd_size;
    g.where = "boost_record";
    const unsigned rows = g.take( );
    unsigned logged = 0;
    for ( unsigned k = 0; k < rows; ++k )
    {
        const auto tex = [ & ]( ui_texture_t& t ) {
            t.m_handle = g.take( );
            t.m_width = static_cast< std::int32_t >( g.take( ) );
            t.m_height = static_cast< std::int32_t >( g.take( ) );
        };
        ui_speedometer_t s;
        tex( s.m_back_imperial );
        tex( s.m_back_metric );
        tex( s.m_active );
        tex( s.m_inactive );
        tex( s.m_hold );
        tex( s.m_mask );
        tex( s.m_flash );
        s.m_metric = static_cast< std::int32_t >( g.take( ) );
        s.m_v_offset = std::bit_cast< float >( g.take( ) );
        s.m_countdown = std::bit_cast< float >( g.take( ) );
        const bool transform = g.take( ) != 0u, offset = g.take( ) != 0u, texture = g.take( ) != 0u, mask = g.take( ) != 0u;
        std::uint32_t words[ 23 ];
        for ( std::uint32_t& w : words )
            w = g.take( );
        ui_boost_check_device_t d;
        ui_record_boost( d, s, transform, offset, texture, mask, reinterpret_cast< const std::uint8_t* >( words ) );
        const unsigned n = g.take( );
        logged += n;
        g.eq( static_cast< std::uint32_t >( d.m_log.size( ) ), n );
        for ( unsigned i = 0; i < n; ++i )
            g.eq( i < d.m_log.size( ) ? d.m_log[ i ] : 0xDEADBEEFu, g.take( ) );
    }
    if ( !g.failed )
    {
        LOG_SUCCESS( "boost_record matches retail's 0x42C290 / 0x42C2A0 on %u records, %u log words", rows, logged );
        return true;
    }
    LOG_ERROR( "boost_record FAILED %d of %d words", g.failed, g.compared );
    return false;
}

// 0x428060 / 0x428070 against retail (case minimap_record): the entity's fields, the record, the log.
struct ui_map_check_device_t final : ui_map_device_t, ui_record_device_t
{
    std::vector< std::uint32_t > m_log;
    void words( std::uint32_t tag, const void* p, unsigned n )
    {
        m_log.push_back( tag );
        const std::size_t at = m_log.size( );
        m_log.resize( at + n );
        std::memcpy( m_log.data( ) + at, p, n * 4u );
    }
    void map_matrix( const float* m ) override { words( 1u, m, 16u ); }
    void map_colour( const std::uint8_t* rgba ) override { words( 2u, rgba, 1u ); }
    void map_range( const float* v ) override { words( 3u, v, 2u ); }
    void map_texture( int which, std::uint32_t handle ) override { m_log.insert( m_log.end( ), { 4u, static_cast< std::uint32_t >( which ), handle } ); }
    void map_draw( const float* v ) override { words( 5u, v, 24u ); }
    void colour( int, const char*, const std::uint8_t* rgba ) override { words( 6u, rgba, 1u ); }
    void matrix( int, const char*, const float* m ) override { words( 7u, m, 16u ); }
    void texture( std::uint32_t slots, std::uint32_t handle ) override { m_log.insert( m_log.end( ), { 8u, slots, handle } ); }
    void draw( std::uint32_t, std::uint32_t, const void* v, std::uint32_t ) override { words( 9u, v, 20u ); }
};

inline bool verify_minimap_record( )
{
    anim_cursor_t g;
    g.w = g_golden_mmr_words;
    g.size = g_golden_mmr_size;
    g.where = "minimap_record";
    const unsigned rows = g.take( );
    unsigned logged = 0;
    for ( unsigned k = 0; k < rows; ++k )
    {
        const std::uint32_t icons = g.take( );
        ui_minimap_t m;
        m.m_map = g.take( );
        m.m_map_size[ 0 ] = static_cast< std::int32_t >( g.take( ) );
        m.m_map_size[ 1 ] = static_cast< std::int32_t >( g.take( ) );
        m.m_frame = g.take( );
        m.m_frame_size[ 0 ] = static_cast< std::int32_t >( g.take( ) );
        m.m_frame_size[ 1 ] = static_cast< std::int32_t >( g.take( ) );
        m.m_mask = g.take( );
        const std::uint32_t tw = g.take( ), th = g.take( );
        const auto number = [ & ] { return std::bit_cast< float >( g.take( ) ); };
        m.m_screen_scale[ 0 ] = number( );
        m.m_screen_scale[ 1 ] = number( );
        m.m_origin[ 0 ] = number( );
        m.m_origin[ 1 ] = number( );
        m.m_scale = number( );
        m.m_view = number( );
        m.m_softness = number( );
        const std::uint32_t water = g.take( );
        std::memcpy( m.m_water, &water, 4 );
        const unsigned n_words = g.take( );
        std::vector< std::uint32_t > record( n_words );
        for ( std::uint32_t& w : record )
            w = g.take( );
        ui_map_check_device_t d;
        const auto* bytes = reinterpret_cast< const std::uint8_t* >( record.data( ) );
        if ( icons )
            ui_record_icons( d, m, [ & ]( std::uint32_t, std::int32_t* w, std::int32_t* h ) { *w = static_cast< std::int32_t >( tw ); *h = static_cast< std::int32_t >( th ); }, bytes );
        else
            ui_record_map( d, m, bytes );
        const unsigned n = g.take( );
        logged += n;
        g.eq( static_cast< std::uint32_t >( d.m_log.size( ) ), n );
        for ( unsigned i = 0; i < n; ++i )
            g.eq( i < d.m_log.size( ) ? d.m_log[ i ] : 0xDEADBEEFu, g.take( ) );
    }
    if ( !g.failed )
    {
        LOG_SUCCESS( "minimap_record matches retail's 0x428060 0x428080 0x428070 0x4284F0 on %u records, %u log words", rows, logged );
        return true;
    }
    LOG_ERROR( "minimap_record FAILED %d of %d words", g.failed, g.compared );
    return false;
}

// The mode change 0x4CBFD0 (case mode_switch): the scripted names, fake modes logging as the case's.
struct mode_check_t final : game_mode_t
{
    mode_check_t( std::vector< std::uint32_t >& log, std::uint32_t id, bool answer, const std::string& text ) : m_log( log ), m_id( id ), m_answer( answer ), m_text( text ) { }
    ~mode_check_t( ) override { m_log.insert( m_log.end( ), { 5u, m_id, 1u } ); }
    static std::uint32_t name_id( const std::string& s )
    {
        for ( std::uint32_t k = 0; k < 5u; ++k )
            if ( s == g_golden_mos_names[ k ] )
                return k;
        return 99u;
    }
    bool enter( const std::string& previous ) override { m_log.insert( m_log.end( ), { 7u, m_id, name_id( previous ) } ); return *m_answer_now; }
    void exit( ) override { m_log.insert( m_log.end( ), { 4u, m_id } ); }
    const std::string& tick( float dt ) override
    {
        std::uint32_t bits;
        std::memcpy( &bits, &dt, 4 );
        m_log.insert( m_log.end( ), { 8u, m_id, bits } );
        return *m_text_now;
    }
    void draw( ) override { }
    std::vector< std::uint32_t >& m_log;
    std::uint32_t m_id;
    bool m_answer;
    const std::string& m_text;
    const bool* m_answer_now = nullptr;
    const std::string* m_text_now = nullptr;
};

inline bool verify_mode_switch( )
{
    anim_cursor_t g;
    g.w = g_golden_mos_words;
    g.size = g_golden_mos_size;
    g.where = "mode_switch";
    const unsigned rows = g.take( );
    std::vector< std::uint32_t > log;
    bool answer = true, pending = false;
    std::string text;
    std::uint32_t serial = 1u;
    mode_manager_t m;
    m.m_factory = [ & ]( const std::string& name ) -> std::unique_ptr< game_mode_t > {
        log.insert( log.end( ), { 6u, mode_check_t::name_id( name ) } );
        if ( name == "None" )
            return nullptr;
        auto mode = std::make_unique< mode_check_t >( log, serial++, answer, text );
        mode->m_answer_now = &answer;
        mode->m_text_now = &text;
        return mode;
    };
    m.m_sync = [ & ] {
        if ( pending )
            log.insert( log.end( ), { 1u, 0x77u, 0xFFFFFFFFu, 2u, 3u } );
    };
    m.m_refused = [ & ] { log.push_back( 9u ); };
    for ( unsigned k = 0; k < rows; ++k )
    {
        const unsigned size = g.take( );
        std::vector< std::uint32_t > r( size );
        for ( unsigned i = 0; i < size; ++i )
            r[ i ] = g.take( );
        if ( r[ 0 ] )
            m.m_next = g_golden_mos_names[ r[ 0 ] ];
        pending = r[ 1 ] != 0u;
        answer = r[ 2 ] != 0u;
        text = g_golden_mos_names[ r[ 3 ] ];
        float dt;
        std::memcpy( &dt, &r[ 4 ], 4 );
        log.clear( );
        m.tick( dt );
        const unsigned n = r[ 5 ];
        g.eq( static_cast< std::uint32_t >( log.size( ) ), n );
        for ( unsigned i = 0; i < n && i < log.size( ); ++i )
            g.eq( log[ i ], r[ 6 + i ] );
        g.eq( mode_check_t::name_id( m.m_current ), r[ 6 + n ] );
        g.eq( mode_check_t::name_id( m.m_next ), r[ 7 + n ] );
    }
    if ( !g.failed )
    {
        LOG_SUCCESS( "mode_switch matches retail's 0x4CBFD0 on %u ticks", rows );
        return true;
    }
    LOG_ERROR( "mode_switch FAILED %d of %d words", g.failed, g.compared );
    return false;
}
