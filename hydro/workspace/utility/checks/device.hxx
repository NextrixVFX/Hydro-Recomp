#pragma once

#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/program.hxx>

#include <cstdint>
#include <cstring>
#include <vector>

// The D3D11 device side against retail (water batch 34; tools oracle/cases/device.py, emit/device.py): retail's own
// VuD3d11Gfx runs on a fake device, context and swap chain that log every call; gfx_check_api_t logs ours in the same rows.

struct gfx_check_api_t : gfx_api_t
{
    std::vector< std::uint32_t > m_log;
    std::vector< std::uint8_t > m_mapped = std::vector< std::uint8_t >( 16 );
    std::uint32_t m_next = 64;
    gfx_handle_t m_view = 0, m_depth = 0;

    void rec( std::initializer_list< std::uint32_t > w ) { m_log.insert( m_log.end( ), w ); }
    static std::uint32_t id( gfx_handle_t h ) { return static_cast< std::uint32_t >( h ); }
    static std::uint32_t bits( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; }

    template < typename T >
    gfx_handle_t created( std::uint32_t kind, const T& d, std::size_t words )
    {
        m_log.push_back( kind );
        std::vector< std::uint32_t > w( words );
        std::memcpy( w.data( ), &d, words * 4 );
        if ( kind == 6u )
            w[ 4 ] &= 0xFFFFu;   // the pad bytes, masked as the case masks retail's
        if ( kind == 5u )
            for ( int t = 0; t < 8; ++t )
                w[ 2 + 8 * t + 7 ] &= 0xFFu;
        m_log.insert( m_log.end( ), w.begin( ), w.end( ) );
        m_log.push_back( m_next );
        return m_next++;
    }

    gfx_handle_t create_buffer( const gfx_buffer_desc_t& d ) override { return created( 1, d, 6 ); }
    gfx_handle_t create_texture( const gfx_texture_desc_t& d ) override { return created( 2, d, 11 ); }
    gfx_handle_t create_target_view( gfx_handle_t t ) override { rec( { 3, id( t ), m_next } ); return m_next++; }
    gfx_handle_t create_depth_view( gfx_handle_t t ) override { rec( { 4, id( t ), m_next } ); return m_next++; }
    static std::uint32_t code_hash( const void* code, std::uint32_t size )
    {
        std::uint32_t h = 0x811C9DC5u;
        for ( std::uint32_t i = 0; i < size; ++i )
            h = ( h ^ static_cast< const std::uint8_t* >( code )[ i ] ) * 0x1000193u;
        return h;
    }
    gfx_handle_t create_vertex_shader( const void* c, std::uint32_t s ) override { rec( { 37, s, code_hash( c, s ), m_next } ); return m_next++; }
    gfx_handle_t create_pixel_shader( const void* c, std::uint32_t s ) override { rec( { 38, s, code_hash( c, s ), m_next } ); return m_next++; }
    gfx_handle_t create_blend( const gfx_blend_desc_t& d ) override
    {
        return created( 5, d, 66 );
    }
    gfx_handle_t create_depth( const gfx_depth_desc_t& d ) override { return created( 6, d, 13 ); }
    gfx_handle_t create_raster( const gfx_raster_desc_t& d ) override { return created( 7, d, 10 ); }
    void release( gfx_handle_t o ) override { rec( { 8, id( o ) } ); }
    void release_all( gfx_handle_t o ) override { rec( { 9, id( o ) } ); }
    void vs_constants( gfx_handle_t b ) override { rec( { 10, id( b ) } ); }
    void ps_views( std::uint32_t s, gfx_handle_t v ) override { rec( { 11, s, id( v ) } ); }
    void ps_shader( gfx_handle_t s ) override { rec( { 12, id( s ) } ); }
    void ps_samplers( std::uint32_t s, gfx_handle_t v ) override { rec( { 13, s, id( v ) } ); }
    void vs_shader( gfx_handle_t s ) override { rec( { 14, id( s ) } ); }
    void draw_indexed( std::uint32_t c, std::uint32_t s, std::int32_t b ) override { rec( { 15, c, s, static_cast< std::uint32_t >( b ) } ); }
    void draw( std::uint32_t c, std::uint32_t s ) override { rec( { 18, c, s } ); }
    void* map( gfx_handle_t r, std::uint32_t t ) override { rec( { 16, id( r ), t } ); return m_mapped.data( ); }
    void unmap( gfx_handle_t r ) override { rec( { 17, id( r ) } ); }
    void ps_constants( gfx_handle_t b ) override { rec( { 19, id( b ) } ); }
    void input_layout( gfx_handle_t l ) override { rec( { 26, id( l ) } ); }
    void vertex_buffer( gfx_handle_t b, std::uint32_t s, std::uint32_t o ) override { rec( { 20, id( b ), s, o } ); }
    void index_buffer( gfx_handle_t b, std::uint32_t f, std::uint32_t o ) override { rec( { 21, id( b ), f, o } ); }
    void topology( std::uint32_t v ) override { rec( { 22, v } ); }
    void set_targets( std::uint32_t n, gfx_handle_t v, gfx_handle_t d ) override { rec( { 23, n, id( v ), id( d ) } ); }
    void set_blend( gfx_handle_t s ) override { rec( { 24, id( s ) } ); }
    void set_depth( gfx_handle_t s ) override { rec( { 25, id( s ) } ); }
    void set_raster( gfx_handle_t s ) override { rec( { 27, id( s ) } ); }
    void set_viewport( const gfx_viewport_t& v ) override
    {
        rec( { 28, bits( v.m_x ), bits( v.m_y ), bits( v.m_width ), bits( v.m_height ), bits( v.m_near ), bits( v.m_far ) } );
    }
    void set_scissor( const gfx_rect_t& r ) override
    {
        rec( { 35, static_cast< std::uint32_t >( r.m_left ), static_cast< std::uint32_t >( r.m_top ), static_cast< std::uint32_t >( r.m_right ), static_cast< std::uint32_t >( r.m_bottom ) } );
    }
    void clear_target( gfx_handle_t v, const float* c ) override { rec( { 29, id( v ), bits( c[ 0 ] ), bits( c[ 1 ] ), bits( c[ 2 ] ), bits( c[ 3 ] ) } ); }
    void clear_depth( gfx_handle_t v, std::uint32_t f, float d, std::uint8_t s ) override { rec( { 30, id( v ), f, bits( d ), s } ); }
    void get_targets( gfx_handle_t& v, gfx_handle_t& d ) override { v = m_view; d = m_depth; rec( { 31 } ); }
    void present( std::uint32_t s, std::uint32_t f ) override { rec( { 32, s, f } ); }
    gfx_handle_t back_buffer( ) override { rec( { 33, m_next } ); return m_next++; }
    void resize( std::uint32_t c, std::uint32_t w, std::uint32_t h, std::uint32_t f, std::uint32_t l ) override { rec( { 34, c, w, h, f, l } ); }
    void copy( void* to, std::uint32_t room, const void* from, std::uint32_t count ) override
    {
        std::uint32_t h = 0x811C9DC5u;
        for ( std::uint32_t i = 0; i < count; ++i )
            h = ( h ^ static_cast< const std::uint8_t* >( from )[ i ] ) * 0x1000193u;
        rec( { 36, static_cast< std::uint32_t >( static_cast< std::uint8_t* >( to ) - m_mapped.data( ) ), room, count, h } );
    }
};

inline void gfx_check_fill( std::vector< std::uint8_t >& out, std::uint32_t seed, std::uint32_t n )
{
    out.resize( n );
    for ( std::uint32_t i = 0; i < n; ++i )
        out[ i ] = static_cast< std::uint8_t >( seed + i * 31u );
}

// Case gfx_device: the op stream through gfx_device_t, the call log and the state after each op.
inline bool verify_gfx_device( )
{
    anim_cursor_t g;
    g.w = g_golden_gdv_words;
    g.size = g_golden_gdv_size;
    g.where = "gfx_device";
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    unsigned ops = 0, calls = 0;
    const unsigned sessions = g.take( );
    for ( unsigned s = 0; s < sessions; ++s )
    {
        gfx_check_api_t api;
        gfx_device_t d( api );
        gfx_stage_t stages[ 8 ];
        std::vector< std::uint8_t > stage_data[ 8 ];
        gfx_program_stages_t programs[ 4 ];
        for ( int i = 0; i < 8; ++i )
        {
            stages[ i ].m_shader = 20 + i;
            stages[ i ].m_constants = 28 + i;
        }
        for ( int i = 0; i < 4; ++i )
            programs[ i ] = gfx_program_stages_t{ &stages[ 2 * i ], &stages[ 2 * i + 1 ] };
        gfx_buffer_t buffers[ 6 ];
        for ( int i = 0; i < 6; ++i )
            buffers[ i ].m_buffer = 40 + i;
        const gfx_target_t targets[ 3 ] = { { 640, 360, 50, 53 }, { 256, 256, 51, 54 }, { 1024, 512, 52, 55 } };
        const gfx_texture_t textures[ 3 ] = { { 0, 59, 58 }, { 1, 57, 56 }, { 2, 0, 0 } };
        std::vector< std::uint8_t > vertices, indices;
        const auto primitive_vertices = []( std::uint32_t k, std::uint32_t p ) { return k_gfx_vertices[ k ][ 0 ] * p + k_gfx_vertices[ k ][ 1 ]; };
        const unsigned rows = g.take( );
        for ( unsigned r = 0; r < rows; ++r )
        {
            ++ops;
            const std::size_t end = g.take( );
            const std::size_t after = g.at + end;
            const unsigned op = g.take( );
            api.m_log.clear( );
            if ( op == 100u )
            {
                d.m_width = d.m_chain_width = static_cast< std::int32_t >( g.take( ) );
                d.m_height = d.m_chain_height = static_cast< std::int32_t >( g.take( ) );
                d.m_sync = g.take( );
                d.m_chain_view = 1;
                d.m_chain_depth = 2;
                d.create_dynamic( );
                api.m_mapped.resize( 0x200000 );
            }
            else if ( op == 101u )
                d.release_states( );
            else
            {
                const unsigned n = g.take( );
                unsigned a[ 8 ] = { };
                for ( unsigned i = 0; i < n; ++i )
                    a[ i < 8 ? i : 7 ] = g.take( );
                float f[ 4 ];
                for ( int i = 0; i < 4; ++i )
                    f[ i ] = real( a[ i ] );
                switch ( op )
                {
                case 0: d.depth_enable( static_cast< std::uint8_t >( a[ 0 ] ) ); break;
                case 1: d.depth_write( static_cast< std::uint8_t >( a[ 0 ] ) ); break;
                case 2: d.depth_func( a[ 0 ] ); break;
                case 3: d.blend_enable( static_cast< std::uint8_t >( a[ 0 ] ) ); break;
                case 4: d.blend_func( a[ 0 ], a[ 1 ] ); break;
                case 5: d.colour_write( static_cast< std::uint8_t >( a[ 0 ] ) ); break;
                case 6: d.cull( a[ 0 ] ); break;
                case 7: d.scissor_enable( static_cast< std::uint8_t >( a[ 0 ] ) ); break;
                case 8: d.set_program( &programs[ a[ 0 ] ] ); break;
                case 9: d.set_declaration( 46 + a[ 0 ] ); break;
                case 10: d.set_stream( &buffers[ a[ 0 ] ], a[ 1 ], a[ 2 ] ); break;
                case 11: d.set_index_buffer( &buffers[ a[ 0 ] ] ); break;
                case 12:
                    if ( a[ 0 ] )
                    {
                        d.set_stream( &buffers[ 0 ], 0, 32 );
                        api.m_log.clear( );
                    }
                    d.draw( a[ 1 ], a[ 2 ], a[ 3 ] );
                    break;
                case 13:
                    if ( a[ 0 ] & 1u )
                        d.set_stream( &buffers[ 0 ], 0, 32 );
                    if ( a[ 0 ] & 2u )
                        d.set_index_buffer( &buffers[ 0 ] );
                    api.m_log.clear( );
                    d.draw_indexed( a[ 1 ], a[ 4 ], a[ 5 ] );
                    break;
                case 14:
                    gfx_check_fill( vertices, a[ 3 ], primitive_vertices( a[ 0 ], a[ 1 ] ) * a[ 2 ] );
                    d.draw_dynamic( a[ 0 ], a[ 1 ], vertices.data( ), a[ 2 ] );
                    break;
                case 15:
                    gfx_check_fill( vertices, a[ 4 ], a[ 1 ] * a[ 3 ] );
                    gfx_check_fill( indices, a[ 4 ] ^ 0x5Au, 2u * primitive_vertices( a[ 0 ], a[ 2 ] ) );
                    d.draw_indexed_dynamic( a[ 0 ], a[ 1 ], a[ 2 ], indices.data( ), vertices.data( ), a[ 3 ] );
                    break;
                case 16:
                {
                    api.m_view = a[ 1 ];
                    api.m_depth = a[ 2 ];
                    std::uint8_t c[ 4 ];
                    std::memcpy( c, &a[ 3 ], 4 );
                    d.clear( a[ 0 ], c, real( a[ 4 ] ) );
                    break;
                }
                case 17: d.viewport( f ); break;
                case 18: d.scissor( f ); break;
                case 19: d.set_texture( a[ 1 ], a[ 0 ] ? &textures[ a[ 0 ] - 1 ] : nullptr ); break;
                case 20: d.set_target( a[ 0 ] ? &targets[ a[ 0 ] - 1 ] : nullptr ); break;
                case 21: d.begin( ); break;
                case 22: d.end( ); break;
                case 23: d.resize( static_cast< std::int32_t >( a[ 0 ] ), static_cast< std::int32_t >( a[ 1 ] ) ); break;
                case 24:
                    gfx_check_fill( stage_data[ a[ 0 ] ], a[ 2 ], a[ 1 ] );
                    stages[ a[ 0 ] ].m_data = stage_data[ a[ 0 ] ].data( );
                    stages[ a[ 0 ] ].m_size = a[ 1 ];
                    stages[ a[ 0 ] ].m_dirty = true;
                    break;
                case 25:
                {
                    std::int32_t w, h, cw, ch;
                    d.size( w, h );
                    d.chain_size( cw, ch );
                    g.eq( static_cast< std::uint32_t >( w ), a[ 0 ] );
                    g.eq( static_cast< std::uint32_t >( h ), a[ 1 ] );
                    g.eq( static_cast< std::uint32_t >( cw ), a[ 2 ] );
                    g.eq( static_cast< std::uint32_t >( ch ), a[ 3 ] );
                    break;
                }
                default: g.eq( op, 0xFFFFFFFFu );
                }
            }
            const unsigned logged = g.take( );
            g.eq( static_cast< std::uint32_t >( api.m_log.size( ) ), logged );
            calls += logged;
            for ( unsigned i = 0; i < logged; ++i )
                g.eq( i < api.m_log.size( ) ? api.m_log[ i ] : 0xDEADBEEFu, g.take( ) );
            g.eq( d.m_primitives, g.take( ) );
            g.eq( d.m_draws, g.take( ) );
            g.eq( static_cast< std::uint32_t >( d.m_width ), g.take( ) );
            g.eq( static_cast< std::uint32_t >( d.m_height ), g.take( ) );
            for ( const float v : d.m_viewport )
                g.eq( gfx_check_api_t::bits( v ), g.take( ) );
            for ( const float v : d.m_scissor )
                g.eq( gfx_check_api_t::bits( v ), g.take( ) );
            g.eq( d.m_vertex_offset, g.take( ) );
            g.eq( d.m_index_offset, g.take( ) );
            g.eq( d.m_stream_stale ? 1u : 0u, g.take( ) );
            g.eq( d.m_indices_stale ? 1u : 0u, g.take( ) );
            g.eq( static_cast< std::uint32_t >( d.m_depths.size( ) ), g.take( ) );
            g.eq( static_cast< std::uint32_t >( d.m_blends.size( ) ), g.take( ) );
            g.eq( static_cast< std::uint32_t >( d.m_rasters.size( ) ), g.take( ) );
            g.eq( d.m_depth_dirty ? 1u : 0u, g.take( ) );
            g.eq( d.m_blend_dirty ? 1u : 0u, g.take( ) );
            g.eq( d.m_raster_dirty ? 1u : 0u, g.take( ) );
            for ( const gfx_stage_t& stage : stages )
                g.eq( stage.m_dirty ? 1u : 0u, g.take( ) );
            g.eq( static_cast< std::uint32_t >( g.at ), static_cast< std::uint32_t >( after ) );
            g.at = after;
        }
    }
    g.eq( static_cast< std::uint32_t >( g.at ), static_cast< std::uint32_t >( g.size ) );
    if ( !g.failed )
    {
        LOG_SUCCESS( "gfx_device matches retail's VuD3d11Gfx (commit 0x5AD3F0, the draws, targets, resize) on %u ops, %u log words: %d words exact", ops, calls, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_device FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// The stage constants' copy really stores here: the case compares the memory.
struct gfx_program_check_api_t : gfx_check_api_t
{
    void copy( void* to, std::uint32_t room, const void* from, std::uint32_t count ) override
    {
        rec( { 39, room, count } );
        std::memcpy( to, from, count );
    }
};

// Case gfx_program: blobs built by the case through load, release, lookups and all the setters; the log and each stage's memory.
inline bool verify_gfx_program( )
{
    anim_cursor_t g;
    g.w = g_golden_gpg_words;
    g.size = g_golden_gpg_size;
    g.where = "gfx_program";
    unsigned ops = 0;
    gfx_program_check_api_t api;
    gfx_programs_t set( api );
    std::vector< gfx_program_t* > live;
    const unsigned blobs = g.take( );
    std::vector< std::vector< std::uint8_t > > streams( blobs );
    for ( auto& s : streams )
    {
        s.resize( g.take( ) );
        for ( std::size_t i = 0; i < s.size( ); i += 4 )
        {
            const std::uint32_t w = g.take( );
            std::memcpy( s.data( ) + i, &w, std::min< std::size_t >( 4, s.size( ) - i ) );
        }
    }
    const unsigned rows = g.take( );
    for ( unsigned r = 0; r < rows; ++r )
    {
        ++ops;
        api.m_log.clear( );
        const unsigned op = g.take( );
        const unsigned which = g.take( );
        const unsigned index = g.take( );
        const unsigned count = g.take( );
        std::uint32_t data[ 64 ];
        const unsigned words = g.take( );
        for ( unsigned i = 0; i < words; ++i )
            data[ i ] = g.take( );
        gfx_program_t* p = which < live.size( ) ? live[ which ] : nullptr;
        const gfx_constant_slot_t* c = p && index < p->m_constants.size( ) ? &p->m_constants[ index ] : nullptr;
        const auto* bytes = reinterpret_cast< const std::uint8_t* >( data );
        switch ( op )
        {
        case 0:
        {
            gfx_program_t* made = set.load( streams[ index ].data( ) );
            live.push_back( made );
            break;
        }
        case 1:
            set.release( p );
            live.erase( live.begin( ) + which );
            break;
        case 2: gfx_programs_t::set_bool( api, *p, *c, static_cast< std::uint8_t >( data[ 0 ] ) ); break;
        case 3: gfx_programs_t::set_word( api, *p, *c, data[ 0 ] ); break;
        case 4: gfx_programs_t::set_vec3( api, *p, *c, data ); break;
        case 5: gfx_programs_t::set_matrix( api, *p, *c, data ); break;
        case 6: gfx_programs_t::set_vec2( api, *p, *c, data ); break;
        case 7: gfx_programs_t::set_vec4( api, *p, *c, data ); break;
        case 8: gfx_programs_t::set_colour3( api, *p, *c, bytes ); break;
        case 9: gfx_programs_t::set_colour4( api, *p, *c, bytes ); break;
        case 10: gfx_programs_t::set_bools( api, *p, *c, bytes, count ); break;
        case 11: gfx_programs_t::set_words( api, *p, *c, data, count ); break;
        case 12: gfx_programs_t::set_matrices( api, *p, *c, data, count ); break;
        case 13: gfx_programs_t::set_vectors( api, *p, *c, data, count ); break;
        case 14:
        {
            // A lookup by a name the case wrote into `data`.
            const char* name = reinterpret_cast< const char* >( data );
            const gfx_constant_slot_t* found = p->constant( name );
            g.eq( found ? static_cast< std::uint32_t >( found - p->m_constants.data( ) ) + 1u : 0u, g.take( ) );
            g.eq( static_cast< std::uint32_t >( p->sampler( name ) ), g.take( ) );
            break;
        }
        default: g.eq( op, 0xFFFFFFFFu );
        }
        const unsigned logged = g.take( );
        g.eq( static_cast< std::uint32_t >( api.m_log.size( ) ), logged );
        for ( unsigned i = 0; i < logged; ++i )
            g.eq( i < api.m_log.size( ) ? api.m_log[ i ] : 0xDEADBEEFu, g.take( ) );
        g.eq( static_cast< std::uint32_t >( set.m_programs.size( ) ), g.take( ) );
        g.eq( static_cast< std::uint32_t >( set.m_stages.size( ) ), g.take( ) );
        const unsigned shown = g.take( );
        g.eq( static_cast< std::uint32_t >( live.size( ) ), shown );
        for ( unsigned i = 0; i < shown && i < live.size( ); ++i )
        {
            const gfx_program_t& q = *live[ i ];
            g.eq( q.m_refs, g.take( ) );
            for ( const gfx_stage_t* s : { q.m_vertex, q.m_pixel } )
            {
                g.eq( s->m_refs, g.take( ) );
                g.eq( s->m_dirty ? 1u : 0u, g.take( ) );
                g.eq( s->m_size, g.take( ) );
                g.eq( gfx_check_api_t::code_hash( s->m_memory.data( ), s->m_size ), g.take( ) );
                g.eq( gfx_check_api_t::code_hash( s->m_code.data( ), static_cast< std::uint32_t >( s->m_code.size( ) ) ), g.take( ) );
            }
        }
    }
    g.eq( static_cast< std::uint32_t >( g.at ), static_cast< std::uint32_t >( g.size ) );
    if ( !g.failed )
    {
        LOG_SUCCESS( "gfx_program matches retail's VuD3d11ShaderProgram (load 0x5AC0C0, setters, sharing, dtors) on %u ops: %d words exact", ops, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_program FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}
