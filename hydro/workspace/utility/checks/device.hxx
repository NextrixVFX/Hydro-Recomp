#pragma once

#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/program.hxx>
#include <workspace/core/world/drawing.hxx>
#include <workspace/core/world/shading.hxx>

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
    static std::uint32_t code_hash( const void* code, std::uint32_t size )
    {
        std::uint32_t h = 0x811C9DC5u;
        for ( std::uint32_t i = 0; i < size; ++i )
            h = ( h ^ static_cast< const std::uint8_t* >( code )[ i ] ) * 0x1000193u;
        return h;
    }
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
    gfx_handle_t create_view( gfx_handle_t t ) override { rec( { 42, id( t ), m_next } ); return m_next++; }
    gfx_handle_t create_sampler( const gfx_sampler_desc_t& d ) override { return created( 41, d, 13 ); }
    gfx_handle_t create_layout( const gfx_layout_element_t* e, std::uint32_t n, const void* code, std::uint32_t size ) override
    {
        m_log.push_back( 44 );
        m_log.push_back( n );
        for ( std::uint32_t i = 0; i < n; ++i )
            m_log.insert( m_log.end( ), { code_hash( e[ i ].m_name, static_cast< std::uint32_t >( std::strlen( e[ i ].m_name ) ) ), e[ i ].m_index, e[ i ].m_format, e[ i ].m_slot, e[ i ].m_offset, e[ i ].m_class, e[ i ].m_step } );
        rec( { size, code_hash( code, size ), m_next } );
        return m_next++;
    }
    void update_texture( gfx_handle_t t, std::uint32_t l, const void* d, std::uint32_t r, std::uint32_t s ) override { rec( { 43, id( t ), l, r, s, code_hash( d, s ) } ); }
    gfx_handle_t create_target_view( gfx_handle_t t ) override { rec( { 3, id( t ), m_next } ); return m_next++; }
    gfx_handle_t create_depth_view( gfx_handle_t t ) override { rec( { 4, id( t ), m_next } ); return m_next++; }
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

// GfxRig's memcpy_s: logged with the source's FNV, and stored.
struct gfx_rig_api_t : gfx_check_api_t
{
    gfx_rig_api_t( ) { m_mapped.resize( 0x4000 ); }
    void copy( void* to, std::uint32_t room, const void* from, std::uint32_t count ) override
    {
        rec( { 40, room, count, code_hash( from, count ) } );
        std::memcpy( to, from, count );
    }
};

inline void gfx_check_read_stream( anim_cursor_t& g, std::vector< std::uint8_t >& s )
{
    s.resize( g.take( ) );
    for ( std::size_t i = 0; i < s.size( ); i += 4 )
    {
        const std::uint32_t w = g.take( );
        std::memcpy( s.data( ) + i, &w, std::min< std::size_t >( 4, s.size( ) - i ) );
    }
}

// Case gfx_model_draws: the ten callbacks + 0x593E70 on the real device and program, the whole call log with every constant's bytes hashed.
inline bool verify_gfx_model_draws( )
{
    anim_cursor_t g;
    g.w = g_golden_gmx_words;
    g.size = g_golden_gmx_size;
    g.where = "gfx_model_draws";
    gfx_rig_api_t api;
    gfx_device_t d( api );
    d.m_width = d.m_chain_width = 1280;
    d.m_height = d.m_chain_height = 720;
    d.m_chain_view = 1;
    d.m_chain_depth = 2;
    d.create_dynamic( );
    gfx_programs_t set( api );
    std::vector< std::uint8_t > stream;
    gfx_check_read_stream( g, stream );
    const gfx_program_t* prog = set.load( stream.data( ) );
    const auto slot = [ & ]( int i ) { return &prog->m_constants[ i ]; };
    gfx_flat_shader_t tables[ 2 ][ 4 ];
    for ( int t = 0; t < 2; ++t )
        for ( int e = 0; e < 4; ++e )
            tables[ t ][ e ] = gfx_flat_shader_t{ prog, slot( ( e + t ) % 2 ? 6 : 8 ), slot( e % 2 ? 7 : 9 ) };
    d.set_program( prog );
    const unsigned rows = g.take( );
    unsigned logged_total = 0;
    for ( unsigned r = 0; r < rows; ++r )
    {
        const unsigned cb = g.take( ), flags = g.take( ), material = g.take( ), lit = g.take( ), pal = g.take( );
        gfx_model_shader_t s;
        s.m_material = static_cast< std::uint8_t >( material );
        s.m_program = prog;
        s.m_matrix = slot( 0 );
        s.m_palette = flags & 4u ? slot( 1 ) : nullptr;
        s.m_colour = flags & 1u ? slot( 2 ) : nullptr;
        s.m_float = flags & 2u ? slot( 3 ) : nullptr;
        s.m_light_dirs = flags & 8u ? slot( 4 ) : nullptr;
        s.m_light_cols = flags & 8u ? slot( 5 ) : nullptr;
        gfx_model_part_t part;
        part.m_shader = &s;
        part.m_start = g.take( );
        part.m_triangles = g.take( );
        gfx_model_record_t rec;
        const std::uint32_t colour = g.take( );
        std::memcpy( rec.m_colour, &colour, 4 );
        rec.m_word = g.take( );
        const auto words = [ & ]( void* to, unsigned n )
        {
            for ( unsigned i = 0; i < n; ++i )
            {
                const std::uint32_t w = g.take( );
                std::memcpy( static_cast< std::uint32_t* >( to ) + i, &w, 4 );
            }
        };
        words( rec.m_matrix, 16 );
        words( part.m_aabb, 8 );
        std::uint32_t block[ 81 ];
        words( block, 81 );
        std::vector< std::uint32_t > palette( 16u * pal + 1u );
        words( palette.data( ), 16u * pal );
        rec.m_part = &part;
        rec.m_lit = static_cast< std::uint8_t >( lit );
        rec.m_palette = reinterpret_cast< const float* >( palette.data( ) );
        rec.m_palette_count = pal;
        const std::function< void( float* ) > gather = [ & ]( float* out ) { std::memcpy( out, block, sizeof( block ) ); };
        api.m_log.clear( );
        if ( cb < 6u )
            gfx_model_draw( d, rec, static_cast< gfx_model_pass_t >( cb % 3u ), cb >= 3u, gather );
        else if ( cb < 8u )
            gfx_flat_draw( d, tables[ cb - 6u ], rec.m_matrix, part );
        else
            gfx_flat_skinned_draw( d, tables[ cb - 8u ], rec.m_matrix, rec.m_palette, pal, part );
        const unsigned logged = g.take( );
        logged_total += logged;
        g.eq( static_cast< std::uint32_t >( api.m_log.size( ) ), logged );
        for ( unsigned i = 0; i < logged; ++i )
            g.eq( i < api.m_log.size( ) ? api.m_log[ i ] : 0xDEADBEEFu, g.take( ) );
        g.eq( prog->m_vertex->m_dirty ? 1u : 0u, g.take( ) );
        g.eq( prog->m_pixel->m_dirty ? 1u : 0u, g.take( ) );
        g.eq( d.m_primitives, g.take( ) );
        g.eq( d.m_draws, g.take( ) );
    }
    g.eq( static_cast< std::uint32_t >( g.at ), static_cast< std::uint32_t >( g.size ) );
    if ( !g.failed )
    {
        LOG_SUCCESS( "gfx_model_draws matches retail's ten model callbacks + 0x593E70 on %u draws, %u log words: %d words exact", rows, logged_total, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_model_draws FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case water_material: the fake asset manager (the ids are the case's six fake textures), the reflection source.
struct water_check_assets_t : water_shader_assets_t
{
    gfx_rig_api_t& m_api;
    const gfx_program_t* m_program = nullptr;
    std::vector< std::string > m_known;
    gfx_texture_t m_textures[ 6 ];
    explicit water_check_assets_t( gfx_rig_api_t& api ) : m_api( api )
    {
        for ( std::uint32_t k = 0; k < 5; ++k )
            m_textures[ k ] = gfx_texture_t{ 0, 10 + 4 * k + 3, 10 + 4 * k + 2 };
        m_textures[ 5 ] = gfx_texture_t{ 1, 31, 30 };
    }
    std::uint32_t hash( const std::string& s ) { return gfx_check_api_t::code_hash( s.data( ), static_cast< std::uint32_t >( s.size( ) ) ); }
    bool known( const std::string& s ) { return std::find( m_known.begin( ), m_known.end( ), s ) != m_known.end( ); }
    const gfx_program_t* shader( const std::string& name ) override { m_api.rec( { 50, hash( name ) } ); return m_program; }
    bool texture_exists( const std::string& name ) override { m_api.rec( { 52, hash( name ) } ); return known( name ); }
    const gfx_texture_t* texture( const std::string& name ) override { m_api.rec( { 53, hash( name ) } ); return &m_textures[ name.rfind( "fx", 0 ) == 0 ? 0 : 1 ]; }
    bool cube_exists( const std::string& name ) override { m_api.rec( { 54, hash( name ) } ); return known( name ); }
    const gfx_texture_t* cube( const std::string& name ) override { m_api.rec( { 55, hash( name ) } ); return &m_textures[ 5 ]; }
    std::uint32_t material( const gfx_program_t&, gfx_declaration_t& d ) override { ++d.m_refs; m_api.rec( { 51 } ); return 0x1234u; }
};

struct water_check_reflection_t : water_reflection_source_t
{
    const gfx_texture_t* m_texture = nullptr;
    float m_offset[ 2 ] = { }, m_scale[ 2 ] = { };
    const gfx_texture_t* texture( ) override { return m_texture; }
    void offset( float* out ) override { out[ 0 ] = m_offset[ 0 ]; out[ 1 ] = m_offset[ 1 ]; }
    void scale( float* out ) override { out[ 0 ] = m_scale[ 0 ]; out[ 1 ] = m_scale[ 1 ]; }
};

// Case water_material: desc -> ctor log, handles; three patch draws per desc: the whole log and both stages' memory.
inline bool verify_water_material( )
{
    anim_cursor_t g;
    g.w = g_golden_wmt_words;
    g.size = g_golden_wmt_size;
    g.where = "water_material";
    gfx_rig_api_t api;
    api.m_mapped.resize( 0x10000 );
    gfx_device_t d( api );
    d.m_width = d.m_chain_width = 1280;
    d.m_height = d.m_chain_height = 720;
    d.m_chain_view = 1;
    d.m_chain_depth = 2;
    d.create_dynamic( );
    gfx_programs_t programs( api );
    gfx_declarations_t declarations( api, programs );
    water_check_assets_t assets( api );
    const char* const names[ ] = { "", "", "", "", "Water/Forced" };
    const char* const foams[ ] = { "", "fx/foam", "fx/foam2" };
    const char* const decals[ ] = { "", "dc/decal" };
    const auto bytes4 = []( std::uint8_t* to, std::uint32_t v ) { std::memcpy( to, &v, 4 ); };
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    const auto compare_log = [ & ]( )
    {
        const unsigned logged = g.take( );
        g.eq( static_cast< std::uint32_t >( api.m_log.size( ) ), logged );
        for ( unsigned i = 0; i < logged; ++i )
            g.eq( i < api.m_log.size( ) ? api.m_log[ i ] : 0xDEADBEEFu, g.take( ) );
    };
    const unsigned rows = g.take( );
    unsigned draws = 0;
    std::vector< std::vector< std::uint8_t > > streams;
    for ( unsigned r = 0; r < rows; ++r )
    {
        water_shader_desc_t desc;
        desc.m_name = names[ g.take( ) ];
        desc.m_own_colours = static_cast< std::uint8_t >( g.take( ) );
        desc.m_fog = static_cast< std::uint8_t >( g.take( ) );
        desc.m_reflect = static_cast< std::uint8_t >( g.take( ) );
        desc.m_complex = static_cast< std::uint8_t >( g.take( ) );
        desc.m_foam_texture = foams[ g.take( ) ];
        const unsigned foam_ok = g.take( ), cube_ok = g.take( );
        desc.m_decal_texture = decals[ g.take( ) ];
        const unsigned decal_ok = g.take( );
        desc.m_cube = "cu/cube";
        bytes4( desc.m_ambient, g.take( ) );
        bytes4( desc.m_diffuse, g.take( ) );
        bytes4( desc.m_foam_ambient, g.take( ) );
        bytes4( desc.m_foam_diffuse, g.take( ) );
        bytes4( desc.m_decal_ambient, g.take( ) );
        bytes4( desc.m_decal_diffuse, g.take( ) );
        desc.m_foam_size = real( g.take( ) );
        std::vector< std::uint8_t > stream;
        gfx_check_read_stream( g, stream );
        const gfx_program_t* program = programs.load( stream.data( ) );
        assets.m_program = program;
        assets.m_known.clear( );
        if ( foam_ok && !desc.m_foam_texture.empty( ) )
            assets.m_known.push_back( desc.m_foam_texture );
        if ( decal_ok && !desc.m_decal_texture.empty( ) )
            assets.m_known.push_back( desc.m_decal_texture );
        if ( cube_ok )
            assets.m_known.push_back( desc.m_cube );
        water_shader_t shader;
        api.m_log.clear( );
        shader.create( api, declarations, assets, desc, 0.89999998f );
        compare_log( );
        const gfx_constant_slot_t* const handles[ 12 ] = { shader.m_ambient, shader.m_diffuse, shader.m_foam_ambient, shader.m_foam_diffuse, shader.m_foam_size, shader.m_foam_center, shader.m_water_z,
                                                           shader.m_offset, shader.m_scale, shader.m_to_normal, shader.m_decal_ambient, shader.m_decal_diffuse };
        for ( const gfx_constant_slot_t* h : handles )
            g.eq( h ? static_cast< std::uint32_t >( h - program->m_constants.data( ) ) + 1u : 0u, g.take( ) );
        for ( const std::int32_t s : { shader.m_fresnel_slot, shader.m_foam_slot, shader.m_reflection_slot, shader.m_decal_slot, shader.m_normal_slot } )
            g.eq( static_cast< std::uint32_t >( s ), g.take( ) );
        for ( int b = 0; b < 3; ++b )
        {
            water_shader_statics_t statics;
            bytes4( statics.m_ambient, g.take( ) );
            bytes4( statics.m_diffuse, g.take( ) );
            bytes4( statics.m_foam_ambient, g.take( ) );
            bytes4( statics.m_foam_diffuse, g.take( ) );
            statics.m_foam_size = real( g.take( ) );
            bytes4( statics.m_decal_ambient, g.take( ) );
            bytes4( statics.m_decal_diffuse, g.take( ) );
            const unsigned current = g.take( );
            const float size = real( g.take( ) );
            const unsigned match = g.take( );
            const float position[ 3 ] = { real( g.take( ) ), real( g.take( ) ), real( g.take( ) ) };
            const std::int32_t vstart = static_cast< std::int32_t >( g.take( ) ), vcount = static_cast< std::int32_t >( g.take( ) );
            const std::int32_t istart = static_cast< std::int32_t >( g.take( ) ), icount = static_cast< std::int32_t >( g.take( ) );
            const unsigned seed = g.take( );
            water_check_reflection_t reflection;
            reflection.m_texture = &assets.m_textures[ 4 ];
            reflection.m_offset[ 0 ] = real( g.take( ) );
            reflection.m_offset[ 1 ] = real( g.take( ) );
            reflection.m_scale[ 0 ] = real( g.take( ) );
            reflection.m_scale[ 1 ] = real( g.take( ) );
            std::vector< std::uint8_t > vertices, indices;
            gfx_check_fill( vertices, seed, static_cast< std::uint32_t >( vstart + vcount ) * 32u );
            gfx_check_fill( indices, seed ^ 0x5Au, static_cast< std::uint32_t >( istart + icount ) * 2u );
            d.set_program( program );
            d.begin( );
            api.m_log.clear( );
            if ( match )
            {
                shader.patch( d, statics, position, vstart, vcount, istart, icount, vertices.data( ), reinterpret_cast< const std::uint16_t* >( indices.data( ) ),
                              &assets.m_textures[ 2 + current ], size, &reflection );
                ++draws;
            }
            compare_log( );
            g.eq( gfx_check_api_t::code_hash( program->m_vertex->m_memory.data( ), 0x200u ), g.take( ) );
            g.eq( gfx_check_api_t::code_hash( program->m_pixel->m_memory.data( ), 0x200u ), g.take( ) );
            g.eq( d.m_blend_enable, g.take( ) );
        }
    }
    g.eq( static_cast< std::uint32_t >( g.at ), static_cast< std::uint32_t >( g.size ) );
    if ( !g.failed )
    {
        LOG_SUCCESS( "water_material matches retail's VuWaterShader (ctor 0x599B40, Fresnel 0x59A860, bind 0x59A4A0, patch 0x595790, declaration 0x540E00, texture 0x57C9B0) on %u descs, %u draws: %d words exact", rows, draws, g.compared );
        return true;
    }
    LOG_ERROR( "water_material FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}
