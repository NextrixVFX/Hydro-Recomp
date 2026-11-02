#pragma once

#include <workspace/core/assets/reader.hxx>
#include <workspace/core/world/foliage.hxx>
#include <workspace/core/world/subtitle.hxx>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// world/foliage.hxx against retail's 0x5BB150 0x5BB7B0 0x5BB9C0 0x5BBAA0 0x5BBE30 0x5E6FB0 0x5E72E0 0x5E7420 0x5E7000 0x5E7050
// 0x5E71E0 (case foliage, boat batch 31): twelve entities through retail's seeded operations on one manager, every word
// compared. The sort is a gfx_sort_t over the fake material of the case.
struct foliage_check_hooks_t : gfx_sort_hooks_t
{
    gfx_material_t m_material[ 2 ];
    std::uint32_t device( int, std::uint32_t, std::uint32_t, std::uint32_t ) override { return 0; }
    void program( std::uint32_t, int, std::uint32_t, std::uint32_t, const std::uint32_t* ) override { }
    std::uint32_t thread( int, std::uint32_t, std::uint32_t ) override { return 0; }
    void profile( int ) override { }
    void composer( int, std::uint32_t, std::uint32_t ) override { }
    void water_flip( ) override { }
    void light_flip( ) override { }
    std::uint32_t texture_handle( std::uint32_t ) override { return 0; }
    void draw( std::uint32_t, std::uint8_t* ) override { }
    const gfx_material_t* material( std::uint32_t id ) override { return &m_material[ id - 1 ]; }
    const gfx_chunk_t* chunk( std::uint32_t ) override { return nullptr; }
};

inline bool verify_foliage( )
{
    const unsigned* w = g_golden_foliage_words;
    const auto next = [ & ]( ) { return *w++; };
    const auto real = []( unsigned u ) {
        float f;
        std::memcpy( &f, &u, 4 );
        return f;
    };
    const auto bits = []( float f ) {
        unsigned u;
        std::memcpy( &u, &f, 4 );
        return u;
    };
    foliage_check_hooks_t hooks;
    hooks.m_material[ 0 ].m_key = next( );
    hooks.m_material[ 1 ].m_key = next( );
    gfx_sort_t sort( hooks );
    sort.m_key_lo = next( );
    sort.m_key_hi = next( );
    sort.m_buffers[ 0 ].m_memory.resize( 0x100000 );
    sort.m_buffers[ 0 ].m_capacity = 0x100000;
    foliage_manager_t manager;
    manager.m_material[ 0 ] = 1;
    manager.m_material[ 1 ] = 2;
    foliage_entity_t entities[ 12 ];
    std::size_t seen = 0;
    int failed = 0;
    unsigned compared = 0;
    const auto same = [ & ]( const char* what, unsigned at, unsigned ours, unsigned retail ) {
        ++compared;
        if ( ours != retail && ++failed <= 10 )
            LOG_ERROR( "foliage %s %u: ours 0x%X, retail 0x%X", what, at, ours, retail );
    };
    const unsigned rows = next( );
    int kinds[ 10 ] = { };
    for ( unsigned r = 0; r < rows; ++r )
    {
        const unsigned size = next( );
        const unsigned* stop = w + size;
        const unsigned op = next( );
        ++kinds[ op ];
        if ( op == 9 )
        {
            foliage_entity_t& e = entities[ next( ) ];
            e.m_texture = next( );
            e.m_fog = static_cast< std::uint8_t >( next( ) );
            for ( std::uint32_t& p : e.m_position )
                p = next( );
            e.m_scale_x = real( next( ) );
            e.m_scale_z = real( next( ) );
            e.m_color = next( );
            for ( std::uint32_t& t : e.m_uv )
                t = next( );
            e.m_draw_distance = real( next( ) );
        }
        else if ( op == 0 || op == 1 )
        {
            foliage_entity_t& e = entities[ next( ) ];
            if ( op == 0 )
                e.game_initialize( manager );
            else
                e.game_release( manager );
        }
        else if ( op == 2 )
        {
            foliage_entity_t& e = entities[ next( ) ];
            e.changed( manager, next( ) != 0 );
            for ( int k : { 0, 1, 2, 4, 5, 6 } )
                same( "aabb", r * 10 + k, e.m_aabb[ k ], next( ) );
            for ( int k : { 0, 1, 2, 4, 5, 6 } )
                same( "layout", r * 10 + k, e.m_layout[ k ], next( ) );
        }
        else if ( op == 3 )
        {
            foliage_entity_t& e = entities[ next( ) ];
            float eye[ 3 ], camera[ 3 ];
            for ( float& f : eye )
                f = real( next( ) );
            for ( float& f : camera )
                f = real( next( ) );
            const std::size_t before = e.m_bucket != nullptr ? e.m_bucket->m_quads.size( ) : 0;
            e.draw( eye, camera );
            const std::size_t after = e.m_bucket != nullptr ? e.m_bucket->m_quads.size( ) : 0;
            const unsigned added = next( );
            same( "added", r, static_cast< unsigned >( after - before ), added );
            if ( added != 0 && after != before )
            {
                const foliage_quad_t& q = e.m_bucket->m_quads.back( );
                for ( std::uint32_t p : q.m_position )
                    same( "quad position", r, p, next( ) );
                same( "quad width", r, bits( q.m_width ), next( ) );
                same( "quad height", r, bits( q.m_height ), next( ) );
                same( "quad colour", r, q.m_color, next( ) );
                for ( std::uint32_t t : q.m_uv )
                    same( "quad uv", r, t, next( ) );
                same( "quad dx", r, bits( q.m_direction[ 0 ] ), next( ) );
                same( "quad dy", r, bits( q.m_direction[ 1 ] ), next( ) );
            }
        }
        else if ( op == 4 )
        {
            std::uint8_t tint[ 3 ];
            for ( std::uint8_t& t : tint )
                t = static_cast< std::uint8_t >( next( ) );
            manager.flush( sort, tint );
            const unsigned commands = next( );
            same( "commands", r, static_cast< unsigned >( sort.m_commands[ 0 ].size( ) - seen ), commands );
            for ( unsigned c = 0; c < commands; ++c )
            {
                if ( seen >= sort.m_commands[ 0 ].size( ) )
                    break;
                const ui_gfx_command_t& ours = sort.m_commands[ 0 ][ seen++ ];
                same( "key lo", r, ours.m_words[ 0 ], next( ) );
                same( "key hi", r, ours.m_words[ 1 ], next( ) );
                same( "callback", r, ours.m_words[ 2 ], next( ) );
                const unsigned offset = next( );
                same( "offset", r, ours.m_words[ 3 ], offset );
                same( "material", r, ours.m_words[ 4 ] - 1u, next( ) );
                same( "chunk", r, ours.m_words[ 5 ], next( ) );
                same( "type", r, ours.m_words[ 6 ] & 0xFFFFu, next( ) );
                const std::uint8_t* block = sort.m_buffers[ 0 ].m_memory.data( ) + ours.m_words[ 3 ];
                std::uint32_t head[ 2 ];
                std::memcpy( head, block, 8 );
                same( "texture", r, head[ 0 ], next( ) );
                const unsigned quads = next( );
                same( "quads", r, head[ 1 ], quads );
                for ( unsigned v = 0; v < quads * 24; ++v )
                {
                    std::uint32_t word;
                    std::memcpy( &word, block + 8 + v * 4, 4 );
                    same( "vertex", r * 100 + v, word, next( ) );
                }
            }
        }
        // The two lists after every operation.
        if ( op != 9 )
        {
            const unsigned sizes[ 2 ] = { next( ), next( ) };
            for ( int l = 0; l < 2; ++l )
            {
                same( "list size", r, static_cast< unsigned >( manager.m_lists[ l ].size( ) ), sizes[ l ] );
                auto it = manager.m_lists[ l ].begin( );
                for ( unsigned k = 0; k < sizes[ l ]; ++k )
                {
                    const unsigned texture = next( ), fog = next( ), references = next( ), count = next( );
                    if ( it == manager.m_lists[ l ].end( ) )
                        continue;
                    same( "bucket texture", r, it->m_texture, texture );
                    same( "bucket fog", r, it->m_fog, fog );
                    same( "bucket references", r, static_cast< unsigned >( it->m_references ), references );
                    same( "bucket quads", r, static_cast< unsigned >( it->m_quads.size( ) ), count );
                    ++it;
                }
            }
        }
        w = stop;
    }
    if ( failed != 0 )
    {
        LOG_ERROR( "verify_foliage: %d of %u mismatches", failed, compared );
        return false;
    }
    LOG_SUCCESS( "foliage matches retail: %u operations (%d init, %d release, %d change, %d draw, %d flush), %u values", rows, kinds[ 0 ], kinds[ 1 ],
                 kinds[ 2 ], kinds[ 3 ], kinds[ 4 ], compared );
    return true;
}

// world/subtitle.hxx against retail's 0x4B5960 0x4B5B70 0x4B5B80 0x4B5C10 0x4B5C50 (case subtitle): the case's own document
// (SubtitleDB plus an int Time, a text Time, an entry with none), the rows' operations and what reached the font.
struct subtitle_check_t
{
    struct call_t
    {
        unsigned kind = 0;
        std::string text;
        float rect[ 4 ] = { };
        unsigned flags = 0;
        float alpha = 0.0f;
        float offset[ 2 ] = { };
        unsigned color = 0;
    };
    std::vector< call_t > calls;
    std::vector< std::string > store;
};

inline bool verify_subtitle( )
{
    const unsigned* w = g_golden_subtitle_words;
    const auto next = [ & ]( ) { return *w++; };
    const auto text = [ & ]( ) {
        const unsigned n = *w++;
        std::string out( reinterpret_cast< const char* >( w ), n );
        w += ( n + 3 ) / 4;
        return out;
    };
    const auto word = []( float f ) {
        unsigned u;
        std::memcpy( &u, &f, 4 );
        return u;
    };
    const unsigned size = next( );
    std::vector< std::uint8_t > bytes( size );
    std::memcpy( bytes.data( ), w, size );
    w += ( size + 3 ) / 4;
    const value_t document = c_reader( bytes ).parse( );
    static subtitle_check_t s_c;
    s_c = subtitle_check_t{ };
    s_c.store.reserve( 4096 );
    subtitle_manager_t m;
    // The case's table: "line " + id for an odd length, else the id's first six letters.
    m.hooks.text = []( void*, const char* id ) -> const char* {
        const std::string name( id );
        s_c.store.push_back( name.size( ) % 2 != 0 ? "line " + name : name.substr( 0, 6 ) );
        return s_c.store.back( ).c_str( );
    };
    m.hooks.font = []( void*, const char* t, const float rect[ 4 ], std::uint32_t flags, float alpha, const float offset[ 2 ] ) {
        subtitle_check_t::call_t c;
        c.kind = 1;
        c.text = t;
        std::memcpy( c.rect, rect, 16 );
        c.flags = flags;
        c.alpha = alpha;
        std::memcpy( c.offset, offset, 8 );
        s_c.calls.push_back( c );
    };
    m.hooks.box = []( void*, std::uint32_t color, const float rect[ 4 ] ) {
        subtitle_check_t::call_t c;
        c.kind = 3;
        c.color = color;
        std::memcpy( c.rect, rect, 16 );
        s_c.calls.push_back( c );
    };
    m.init( document );
    int failed = 0;
    unsigned compared = 0;
    const auto same = [ & ]( const char* what, unsigned at, unsigned ours, unsigned retail ) {
        ++compared;
        if ( ours != retail && ++failed <= 10 )
            LOG_ERROR( "subtitle %s %u: ours 0x%X, retail 0x%X", what, at, ours, retail );
    };
    const auto state = [ & ]( unsigned r ) {
        const unsigned on = next( ), time = next( );
        const std::string t = text( );
        same( "active", r, m.m_text != nullptr ? 1u : 0u, on );
        same( "time", r, word( m.m_time ), time );
        same( "text", r, m.m_text != nullptr && t == m.m_text ? 1u : 0u, on );
    };
    const unsigned rows = next( );
    for ( unsigned r = 0; r < rows; ++r )
    {
        const unsigned op = next( );
        if ( op == 0 )
        {
            for ( int k = 0; k < 4; ++k )
                same( "rect", k, word( m.m_rect[ k ] ), next( ) );
        }
        else if ( op == 1 )
        {
            const std::string id = text( );
            m.show( id.c_str( ) );
            state( r );
        }
        else if ( op == 2 )
        {
            const unsigned dt = next( );
            float seconds;
            std::memcpy( &seconds, &dt, 4 );
            m.tick( seconds );
            state( r );
        }
        else if ( op == 3 )
        {
            m.clear( );
            state( r );
        }
        else
        {
            const unsigned enabled = next( );
            m.m_show_rect = static_cast< std::uint8_t >( next( ) );
            std::uint32_t key = next( );
            const unsigned after = next( );
            s_c.calls.clear( );
            m.draw( enabled != 0, key );
            same( "key", r, key, after );
            const unsigned events = next( );
            std::size_t at = 0;
            for ( unsigned e = 0; e < events; ++e )
            {
                const unsigned kind = next( );
                if ( kind == 2 )
                {
                    same( "draw font", r, next( ), 1u );
                    same( "draw object", r, next( ), 1u );
                    continue;
                }
                const subtitle_check_t::call_t none;
                const subtitle_check_t::call_t& c = at < s_c.calls.size( ) ? s_c.calls[ at ] : none;
                ++at;
                same( "call", r, c.kind, kind );
                if ( kind == 1 )
                {
                    same( "font text", r, text( ) == c.text ? 1u : 0u, 1u );
                    same( "font params", r, 0x28u, next( ) - 0u );
                }
                else
                {
                    same( "colour", r, c.color, next( ) );
                }
                for ( int k = 0; k < 4; ++k )
                    same( "call rect", r, word( c.rect[ k ] ), next( ) );
                if ( kind == 1 )
                {
                    same( "flags", r, c.flags, next( ) );
                    same( "alpha", r, word( c.alpha ), next( ) );
                    same( "offset", r, word( c.offset[ 0 ] ), next( ) );
                    same( "offset", r, word( c.offset[ 1 ] ), next( ) );
                }
            }
            same( "calls", r, static_cast< unsigned >( s_c.calls.size( ) ), static_cast< unsigned >( at ) );
        }
    }
    if ( failed != 0 )
    {
        LOG_ERROR( "verify_subtitle: %d of %u mismatches", failed, compared );
        return false;
    }
    LOG_SUCCESS( "VuSubtitleManagerImpl matches retail over %u operations, %u values", rows, compared );
    return true;
}

// xml_asset_load against 0x5C7C40 (case asset_xml_load).
inline bool verify_asset_xml_load( )
{
    const unsigned* w = g_golden_asset_xml_load_words;
    const unsigned rows = *w++;
    static const std::uint8_t* s_seen;
    static bool s_answer;
    std::vector< std::uint8_t > data( 2048 );
    int failed = 0;
    for ( unsigned r = 0; r < rows; ++r )
    {
        const unsigned start = *w++, size = *w++, answer = *w++, eax = *w++, position = *w++, condense = *w++, object = *w++, text = *w++, a1 = *w++, a2 = *w++;
        std::memcpy( data.data( ) + start, &size, 4 );
        s_answer = answer != 0;
        std::size_t at = start;
        std::uint8_t byte = 1;
        const bool ok = xml_asset_load( data.data( ), at, byte, []( void*, const std::uint8_t* t ) {
            s_seen = t;
            return s_answer;
        }, nullptr );
        const bool same = ok == ( eax != 0 ) && at == position && byte == condense && object == 0x58u && a1 == 0u && a2 == 1u &&
                          static_cast< unsigned >( s_seen - data.data( ) ) == text;
        if ( !same && ++failed <= 5 )
            LOG_ERROR( "asset_xml_load row %u: ours %d %u %u, retail %u %u %u", r, ok ? 1 : 0, static_cast< unsigned >( at ), byte, eax, position, condense );
    }
    if ( failed != 0 )
    {
        LOG_ERROR( "verify_asset_xml_load: %d of %u rows differ", failed, rows );
        return false;
    }
    LOG_SUCCESS( "xml_asset_load matches retail's 0x5C7C40 over %u streams", rows );
    return true;
}
