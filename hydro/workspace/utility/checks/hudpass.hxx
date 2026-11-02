#pragma once

#include <workspace/core/world/hudpass.hxx>

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
