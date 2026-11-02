#pragma once

#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/post.hxx>
#include <workspace/core/render/program.hxx>
#include <workspace/core/world/composer.hxx>
#include <workspace/core/world/drawing.hxx>
#include <workspace/core/world/shading.hxx>
#include <workspace/core/world/waterpass.hxx>

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

// Case gfx_post: create, render targets, the two callbacks; the log and the state after each.
inline bool verify_gfx_post( )
{
    anim_cursor_t g;
    g.w = g_golden_gpo_words;
    g.size = g_golden_gpo_size;
    g.where = "gfx_post";
    gfx_rig_api_t api;
    api.m_mapped.resize( 0x100000 );
    gfx_device_t d( api );
    d.m_width = d.m_chain_width = 1280;
    d.m_height = d.m_chain_height = 720;
    d.m_chain_view = 1;
    d.m_chain_depth = 2;
    d.create_dynamic( );
    gfx_programs_t programs( api );
    gfx_declarations_t declarations( api, programs );
    const char* const names[ 4 ] = { "PostProcess/Copy", "PostProcess/GaussBlur5x5", "PostProcess/RadialBlur", "PostProcess/Effect" };
    const gfx_program_t* loaded[ 4 ];
    std::vector< std::uint8_t > streams[ 4 ];
    for ( int i = 0; i < 4; ++i )
    {
        gfx_check_read_stream( g, streams[ i ] );
        loaded[ i ] = programs.load( streams[ i ].data( ) );
    }
    const auto compare_log = [ & ]( )
    {
        const unsigned logged = g.take( );
        g.eq( static_cast< std::uint32_t >( api.m_log.size( ) ), logged );
        for ( unsigned i = 0; i < logged; ++i )
            g.eq( i < api.m_log.size( ) ? api.m_log[ i ] : 0xDEADBEEFu, g.take( ) );
    };
    gfx_post_t post;
    api.m_log.clear( );
    post.create( declarations, [ & ]( const std::string& name )
    {
        api.rec( { 50, gfx_check_api_t::code_hash( name.data( ), static_cast< std::uint32_t >( name.size( ) ) ) } );
        for ( int i = 0; i < 4; ++i )
            if ( name == names[ i ] )
                return loaded[ i ];
        return static_cast< const gfx_program_t* >( nullptr );
    } );
    compare_log( );
    gfx_target_object_t* targets[ 4 ] = { };
    const unsigned made = g.take( );
    for ( unsigned t = 0; t < made; ++t )
    {
        const std::int32_t w = static_cast< std::int32_t >( g.take( ) ), h = static_cast< std::int32_t >( g.take( ) );
        const std::uint32_t f = g.take( );
        api.m_log.clear( );
        targets[ t ] = gfx_target_create( api, w, h, f );
        g.eq( gfx_check_api_t::id( targets[ t ]->m_depth ), g.take( ) );
        g.eq( gfx_check_api_t::id( targets[ t ]->m_view ), g.take( ) );
        compare_log( );
    }
    const unsigned draws = g.take( );
    for ( unsigned n = 0; n < draws; ++n )
    {
        const unsigned kind = g.take( );
        gfx_texture_object_t texture;
        texture.m_width = static_cast< std::int32_t >( g.take( ) );
        texture.m_height = static_cast< std::int32_t >( g.take( ) );
        texture.m_view = 11;
        texture.m_sampler = 12;
        const unsigned ti = g.take( );
        const std::uint32_t amount_bits = g.take( );
        float amount;
        std::memcpy( &amount, &amount_bits, 4 );
        const gfx_target_t* target = ti == 4u ? nullptr : targets[ ti ];
        d.begin( );
        api.m_log.clear( );
        if ( kind )
            post.radial_to( d, texture, target, amount );
        else
            post.copy_to( d, texture, target );
        compare_log( );
        g.eq( d.m_depth_enable, g.take( ) );
        g.eq( d.m_depth_write, g.take( ) );
        g.eq( d.m_cull, g.take( ) );
        g.eq( static_cast< std::uint32_t >( d.m_width ), g.take( ) );
        g.eq( static_cast< std::uint32_t >( d.m_height ), g.take( ) );
        g.eq( d.m_primitives, g.take( ) );
        g.eq( d.m_draws, g.take( ) );
    }
    g.eq( static_cast< std::uint32_t >( g.at ), static_cast< std::uint32_t >( g.size ) );
    if ( !g.failed )
    {
        LOG_SUCCESS( "gfx_post matches retail's VuPostProcess (create 0x57BF10, copy 0x57C240, radial 0x57C2D0, quad 0x57C370, callbacks 0x5BC6F0 0x5BC7D0) and render target create 0x57F7D0 on %u draws: %d words exact", draws, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_post FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Case gfx_composer: the logging hooks. A target is the case's address (the records hold it), logged as its number n; its texture 0x100 + n.
struct gfx_composer_check_t : gfx_composer_hooks_t
{
    std::vector< std::uint32_t > m_log;
    std::int32_t m_display[ 2 ] = { };
    std::uint32_t m_next = 1;
    bool m_lens[ 2 ] = { };
    const std::uint8_t* m_comp = nullptr;
    void rec( std::initializer_list< std::uint32_t > w ) { m_log.insert( m_log.end( ), w ); }
    static std::uint32_t hash( const void* p, std::uint32_t n ) { return gfx_check_api_t::code_hash( p, n ); }
    void display( std::int32_t& w, std::int32_t& h ) override { w = m_display[ 0 ]; h = m_display[ 1 ]; }
    static constexpr std::uint32_t k_targets = 0x6C010000u;
    static std::uint32_t number( std::uint32_t t ) { return t ? ( t - k_targets ) / 0x40u + 1u : 0u; }
    std::uint32_t target_create( std::int32_t w, std::int32_t h, std::uint32_t f ) override
    {
        rec( { 65, static_cast< std::uint32_t >( w ), static_cast< std::uint32_t >( h ), f, m_next } );
        return k_targets + 0x40u * ( m_next++ - 1u );
    }
    void target_release( std::uint32_t t ) override { rec( { 66, number( t ) } ); }
    std::uint32_t target_texture( std::uint32_t t ) override { return 0x100u + number( t ); }
    bool lens_active( ) override { return m_lens[ 0 ]; }
    bool lens_on( ) override { return m_lens[ 1 ]; }
    void lens_size( std::int32_t w, std::int32_t h ) override { rec( { 70, static_cast< std::uint32_t >( w ), static_cast< std::uint32_t >( h ) } ); }
    void lens_draw( std::uint32_t tex, std::uint32_t t ) override { rec( { 71, tex, number( t ) } ); }
    void camera_static( const std::uint8_t*, const std::uint8_t* ) override { rec( { 60, 0x28u, 0x54u } ); }
    void sort_view( const std::uint8_t* ) override { rec( { 61, 0x54u } ); }
    void sort_camera( const std::uint8_t* c ) override { rec( { 62, hash( c, 0x240 ) } ); }
    void draw( const gfx_draw_params_t& p, const std::uint8_t* camera ) override
    {
        std::uint8_t raw[ 0x2A ] = { };
        std::memcpy( raw, p.m_eye, 16 );
        std::memcpy( raw + 0x10, &p.m_lod, 4 );
        raw[ 0x14 ] = p.m_reflecting;
        std::memcpy( raw + 0x15, p.m_clip, 16 );
        raw[ 0x25 ] = p.m_second_depth;
        raw[ 0x26 ] = p.m_skip_models;
        rec( { 67, hash( raw, 0x27 + 0u ) , hash( camera, 0x240 ) } );
    }
    void trail( ) override { rec( { 63, 0xA8u } ); }
    void depth_extra( const std::uint8_t* c ) override { rec( { 68, hash( c, 0x240 ) } ); }
    void overlay( std::uint32_t tex, const float* ) override { rec( { 64, tex } ); }
};

struct gfx_composer_sort_hooks_t : gfx_sort_hooks_t
{
    std::uint32_t device( int, std::uint32_t, std::uint32_t, std::uint32_t ) override { return 0; }
    void program( std::uint32_t, int, std::uint32_t, std::uint32_t, const std::uint32_t* ) override { }
    std::uint32_t thread( int, std::uint32_t, std::uint32_t ) override { return 0; }
    void profile( int ) override { }
    void composer( int, std::uint32_t, std::uint32_t ) override { }
    void water_flip( ) override { }
    void light_flip( ) override { }
    std::uint32_t texture_handle( std::uint32_t ) override { return 0; }
    void draw( std::uint32_t, std::uint8_t* ) override { }
    const gfx_material_t* material( std::uint32_t ) override { return nullptr; }
    const gfx_chunk_t* chunk( std::uint32_t ) override { return nullptr; }
};

inline bool verify_gfx_composer( )
{
    anim_cursor_t g;
    g.w = g_golden_gcm_words;
    g.size = g_golden_gcm_size;
    g.where = "gfx_composer";
    gfx_composer_sort_hooks_t sort_hooks;
    static gfx_sort_t sort( sort_hooks );
    gfx_composer_check_t hooks;
    gfx_composer_t c( sort, hooks );
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    const unsigned rows = g.take( );
    unsigned commands = 0;
    for ( unsigned r = 0; r < rows; ++r )
    {
        c.m_scale = real( g.take( ) );
        c.m_forced_w = static_cast< std::int32_t >( g.take( ) );
        c.m_forced_h = static_cast< std::int32_t >( g.take( ) );
        c.m_lod = real( g.take( ) );
        c.m_flag_c = static_cast< std::uint8_t >( g.take( ) );
        c.m_radial = static_cast< std::uint8_t >( g.take( ) );
        c.m_no_copy = static_cast< std::uint8_t >( g.take( ) );
        c.m_depth_first = static_cast< std::uint8_t >( g.take( ) );
        c.m_depth_pass = static_cast< std::uint8_t >( g.take( ) );
        hooks.m_lens[ 0 ] = g.take( ) != 0u;
        hooks.m_lens[ 1 ] = g.take( ) != 0u;
        const bool reflection = g.take( ) != 0u;
        const float plane = real( g.take( ) ), blur = real( g.take( ) );
        sort.m_key_lo = g.take( );
        sort.m_key_hi = g.take( );
        hooks.m_display[ 0 ] = static_cast< std::int32_t >( g.take( ) );
        hooks.m_display[ 1 ] = static_cast< std::int32_t >( g.take( ) );
        std::uint32_t camera[ 0x90 ];
        for ( std::uint32_t& w : camera )
            w = g.take( );
        sort.m_commands[ sort.m_index ].clear( );
        sort.m_buffers[ sort.m_index ].m_used = 0;
        sort.m_buffers[ sort.m_index ].m_memory.assign( sort.m_buffers[ sort.m_index ].m_memory.size( ), 0 );
        sort.m_offset = 0;
        hooks.m_log.clear( );
        c.frame( reinterpret_cast< const std::uint8_t* >( camera ), plane, blur, reflection );
        const unsigned logged = g.take( );
        std::vector< std::uint32_t > got;
        // Retail logs each allocation too: drop them from the golden side.
        std::vector< std::uint32_t > want;
        const std::size_t end = g.at + logged;
        while ( g.at < end )
        {
            const unsigned kind = g.take( );
            const unsigned len = kind == 69 ? 1 : kind == 65 ? 4 : kind == 66 || kind == 61 || kind == 62 || kind == 63 || kind == 64 || kind == 68 ? 1 : 2;
            std::vector< std::uint32_t > w;
            for ( unsigned i = 0; i < len; ++i )
                w.push_back( g.take( ) );
            if ( kind == 69 )
                continue;
            want.push_back( kind );
            want.insert( want.end( ), w.begin( ), w.end( ) );
        }
        g.eq( static_cast< std::uint32_t >( hooks.m_log.size( ) ), static_cast< std::uint32_t >( want.size( ) ) );
        for ( std::size_t i = 0; i < want.size( ); ++i )
            g.eq( i < hooks.m_log.size( ) ? hooks.m_log[ i ] : 0xDEADBEEFu, want[ i ] );
        const unsigned count = g.take( );
        g.eq( static_cast< std::uint32_t >( sort.m_commands[ sort.m_index ].size( ) ), count );
        commands += count;
        for ( unsigned i = 0; i < count; ++i )
            for ( int j = 0; j < 7; ++j )
            {
                const std::uint32_t mine = i < sort.m_commands[ sort.m_index ].size( ) ? sort.m_commands[ sort.m_index ][ i ].m_words[ j ] : 0xDEADBEEFu;
                g.eq( j == 6 ? mine & 0xFFFFu : mine, g.take( ) );
            }
        const std::uint32_t used = g.take( );
        g.eq( static_cast< std::uint32_t >( sort.m_buffers[ sort.m_index ].m_used ), used );
        g.eq( gfx_check_api_t::code_hash( sort.m_buffers[ sort.m_index ].m_memory.data( ), used ), g.take( ) );
        g.eq( sort.m_key_hi, g.take( ) );
        g.eq( gfx_composer_check_t::number( c.m_targets[ 0 ] ), g.take( ) );
        g.eq( gfx_composer_check_t::number( c.m_targets[ 1 ] ), g.take( ) );
        g.eq( gfx_composer_check_t::number( c.m_reflection ), g.take( ) );
        g.eq( static_cast< std::uint32_t >( c.m_width ), g.take( ) );
        g.eq( static_cast< std::uint32_t >( c.m_height ), g.take( ) );
        g.eq( c.m_resized, g.take( ) );
    }
    g.eq( static_cast< std::uint32_t >( g.at ), static_cast< std::uint32_t >( g.size ) );
    if ( !g.failed )
    {
        LOG_SUCCESS( "gfx_composer matches retail's frame 0x5588B0 (targets 0x559100, reflection 0x558C00, scene 0x558AC0, commands) on %u frames, %u commands: %d words exact", rows, commands, g.compared );
        return true;
    }
    LOG_ERROR( "gfx_composer FAILED %d of %d words (cursor %zu of %zu)", g.failed, g.compared, g.at, g.size );
    return false;
}

// Item 6 of batch 35: render/direct.hxx on a REAL device, no window: the hardware adapter, WARP when there is none. Retail's own Water/Complex/FogReflect from the pack, a flat 33 x 33
// grid at z = 0 with a camera above it, the real wave texture, a checker for the foam and a gradient for the reflection. Passes when every object was created, the debug layer's state is not
// "device removed", the surface covers the screen's lower half, and two wave frames differ (the normals reach the pixels).
#if HYDRO_HOST_WINDOWS
inline void water_direct_bmp( const char* path, const std::vector< std::uint8_t >& rgba, int w, int h )
{
    std::FILE* f = nullptr;
    if ( fopen_s( &f, path, "wb" ) != 0 || f == nullptr )
        return;
    const std::uint32_t size = 54u + static_cast< std::uint32_t >( w * h * 4 );
    std::uint8_t head[ 54 ] = { 'B', 'M' };
    std::memcpy( head + 2, &size, 4 );
    head[ 10 ] = 54;
    head[ 14 ] = 40;
    std::memcpy( head + 18, &w, 4 );
    const std::int32_t up = -h;
    std::memcpy( head + 22, &up, 4 );
    head[ 26 ] = 1;
    head[ 28 ] = 32;
    std::fwrite( head, 1, 54, f );
    std::vector< std::uint8_t > bgra( rgba );
    for ( std::size_t i = 0; i + 3 < bgra.size( ); i += 4 )
        std::swap( bgra[ i ], bgra[ i + 2 ] );
    std::fwrite( bgra.data( ), 1, bgra.size( ), f );
    std::fclose( f );
}

inline bool verify_gfx_direct( )
{
    gfx_direct_t api;
    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDevice( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &api.m_device, &level, &api.m_context );
    const char* driver = "hardware";
    if ( FAILED( hr ) )
    {
        driver = "WARP";
        hr = D3D11CreateDevice( nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &api.m_device, &level, &api.m_context );
    }
    if ( FAILED( hr ) )
    {
        LOG_ERROR( "gfx_direct: no device: 0x%08x", static_cast< unsigned >( hr ) );
        return false;
    }
    bool ok = true;
    int w = 480, h = 270;
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "gfx_direct: Base.apf did not open" );
        return false;
    }
    // The target, its depth buffer: 0x57F7D0's description, with the read back's staging copy beside it.
    gfx_texture_desc_t td{ static_cast< std::uint32_t >( w ), static_cast< std::uint32_t >( h ), 1u, 1u, 0x1Cu, 1u, 0u, 0u, 0x28u, 0u, 0u };
    const gfx_handle_t colour = api.create_texture( td );
    const gfx_handle_t view = api.create_target_view( colour );
    water_pass_t pass( api );
    gfx_handle_t depth = 0;
    pass.m_device.create_depth_buffer( w, h, depth );
    ok = ok && colour && view && depth;
    pass.m_device.m_width = pass.m_device.m_chain_width = w;
    pass.m_device.m_height = pass.m_device.m_chain_height = h;
    pass.m_device.m_chain_view = view;
    pass.m_device.m_chain_depth = depth;
    pass.m_device.set_target( nullptr );

    // A checker for the foam, a blue gradient for the reflection.
    gfx_texture_options_t linear;
    linear.m_min = linear.m_mag = 2;
    gfx_texture_object_t* foam = gfx_texture_create( api, 16, 16, 4u, linear );
    gfx_texture_object_t* reflection = gfx_texture_create( api, 64, 64, 4u, linear );
    std::vector< std::uint32_t > pixels( 16 * 16 );
    for ( int i = 0; i < 16 * 16; ++i )
        pixels[ i ] = ( ( i ^ ( i >> 4 ) ) & 1 ) ? 0xFFFFFFFFu : 0x00FFFFFFu;
    gfx_texture_update( api, *foam, 0, pixels.data( ) );
    pixels.resize( 64 * 64 );
    for ( int i = 0; i < 64 * 64; ++i )
        pixels[ i ] = 0xFF000000u | ( static_cast< std::uint32_t >( 255 - ( i >> 6 ) * 4 ) << 16 ) | ( static_cast< std::uint32_t >( ( i & 63 ) * 4 ) << 8 ) | 0x20u;
    gfx_texture_update( api, *reflection, 0, pixels.data( ) );

    water_pass_source_t source;
    source.m_read = [ & ]( const std::string& entry, std::vector< std::uint8_t >& out ) { return archive.read( entry.c_str( ), out ); };
    source.m_texture = [ & ]( const std::string& name ) { return name == "Water/WakeFroth" ? foam->m_view : gfx_handle_t{ 0 }; };
    ok = pass.create( source ) && ok;
    const int shader = pass.add_shader( water_pass_t::default_desc( 1u ) );
    const water_shader_t& s = *pass.m_shaders[ 0 ];
    ok = ok && s.m_program && s.m_fresnel_slot >= 0 && s.m_normal_slot >= 0 && s.m_reflection_slot >= 0 && pass.m_missing_programs == 0;
    if ( !ok )
    {
        LOG_ERROR( "gfx_direct: creation failed (device %s)", driver );
        return false;
    }

    // Flat water 400 wide, one surface.
    water_grid_frame_t frame;
    const int n = 33;
    for ( int y = 0; y < n; ++y )
        for ( int x = 0; x < n; ++x )
        {
            water_grid_record_t r{ };
            r.x = -200.0f + 400.0f * static_cast< float >( x ) / static_cast< float >( n - 1 );
            r.y = -200.0f + 400.0f * static_cast< float >( y ) / static_cast< float >( n - 1 );
            r.foam = ( x + y ) % 8 == 0 ? 0.8f : 0.0f;
            r.shade = 1.0f;
            frame.vertices.push_back( r );
        }
    for ( int y = 0; y + 1 < n; ++y )
        for ( int x = 0; x + 1 < n; ++x )
        {
            const std::uint16_t a = static_cast< std::uint16_t >( y * n + x ), b = static_cast< std::uint16_t >( a + 1 ), c = static_cast< std::uint16_t >( a + n ), e = static_cast< std::uint16_t >( c + 1 );
            for ( const std::uint16_t i : { a, b, c, c, b, e } )
                frame.indices.push_back( i );
        }
    water_grid_draw_t d;
    d.surface = 0;
    d.vertex_count = static_cast< int >( frame.vertices.size( ) );
    d.index_count = static_cast< int >( frame.indices.size( ) );
    frame.draws.push_back( d );

    // The camera: 12 above the water, looking along +y and down, rows of view * projection (D3D's z, the row vector).
    water_pass_view_t v;
    const float eye[ 3 ] = { 0.0f, -40.0f, 12.0f };
    const float fw[ 3 ] = { 0.0f, 0.9578263f, -0.2873479f }, rt[ 3 ] = { 1.0f, 0.0f, 0.0f }, up[ 3 ] = { 0.0f, 0.2873479f, 0.9578263f };
    const float near_z = 0.5f, far_z = 800.0f, f = 1.5f;
    const float a = f / ( static_cast< float >( w ) / static_cast< float >( h ) );
    const float q = far_z / ( far_z - near_z );
    const float view_m[ 4 ][ 3 ] = { { rt[ 0 ], up[ 0 ], fw[ 0 ] }, { rt[ 1 ], up[ 1 ], fw[ 1 ] }, { rt[ 2 ], up[ 2 ], fw[ 2 ] },
                                     { -( rt[ 0 ] * eye[ 0 ] + rt[ 1 ] * eye[ 1 ] + rt[ 2 ] * eye[ 2 ] ), -( up[ 0 ] * eye[ 0 ] + up[ 1 ] * eye[ 1 ] + up[ 2 ] * eye[ 2 ] ),
                                       -( fw[ 0 ] * eye[ 0 ] + fw[ 1 ] * eye[ 1 ] + fw[ 2 ] * eye[ 2 ] ) } };
    for ( int r = 0; r < 4; ++r )
    {
        v.m_view_proj[ r * 4 + 0 ] = view_m[ r ][ 0 ] * a;
        v.m_view_proj[ r * 4 + 1 ] = view_m[ r ][ 1 ] * f;
        v.m_view_proj[ r * 4 + 2 ] = view_m[ r ][ 2 ] * q + ( r == 3 ? -near_z * q : 0.0f );
        v.m_view_proj[ r * 4 + 3 ] = view_m[ r ][ 2 ];
    }
    std::memcpy( v.m_eye, eye, 12 );
    const float light[ 3 ] = { 0.3f, 0.3f, 0.9f };
    std::memcpy( v.m_light, light, 12 );
    v.m_fog_start = 50.0f;
    v.m_fog_end = 300.0f;
    const float fog[ 4 ] = { 0.55f, 0.74f, 0.9f, 1.0f };
    std::memcpy( v.m_fog_colour, fog, 16 );
    const std::uint8_t ambient[ 4 ] = { 0x2D, 0x3C, 0x42, 0xC0 }, diffuse[ 4 ] = { 0x50, 0x5A, 0xC0, 0x80 };
    std::memcpy( v.m_statics.m_ambient, ambient, 4 );
    std::memcpy( v.m_statics.m_diffuse, diffuse, 4 );
    std::memset( v.m_statics.m_foam_ambient, 0x80, 4 );
    std::memset( v.m_statics.m_foam_diffuse, 0xFF, 4 );
    v.m_statics.m_foam_size = 14.0f;

    D3D11_TEXTURE2D_DESC staging{ };
    reinterpret_cast< ID3D11Texture2D* >( colour )->GetDesc( &staging );
    staging.BindFlags = 0;
    staging.Usage = D3D11_USAGE_STAGING;
    staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* copy = nullptr;
    hr = api.m_device->CreateTexture2D( &staging, nullptr, &copy );
    const auto frame_at = [ & ]( float dt, std::vector< std::uint8_t >& out )
    {
        const std::uint8_t clear[ 4 ] = { 255, 0, 255, 255 };
        pass.tick( dt );
        pass.m_device.clear( 3u, clear, 1.0f );
        pass.draw( frame, v, reflection->m_view, [ shader ]( int ) { return shader; } );
        api.m_context->CopyResource( copy, reinterpret_cast< ID3D11Resource* >( colour ) );
        api.m_context->Flush( );
        D3D11_MAPPED_SUBRESOURCE m{ };
        api.m_context->Map( copy, 0, D3D11_MAP_READ, 0, &m );
        out.resize( static_cast< std::size_t >( w * h * 4 ) );
        for ( int y = 0; y < h; ++y )
            std::memcpy( out.data( ) + static_cast< std::size_t >( y * w * 4 ), static_cast< const std::uint8_t* >( m.pData ) + static_cast< std::size_t >( y ) * m.RowPitch, static_cast< std::size_t >( w * 4 ) );
        api.m_context->Unmap( copy, 0 );
    };
    std::vector< std::uint8_t > one, two;
    // The commit that creates a rasterizer state binds null (render/gfx.hxx): D3D11's default, which culls this grid. Found by this test's first run; the frame after has the state.
    frame_at( 0.05f, one );
    frame_at( 0.1f, one );
    frame_at( 1.0f, two );
    const HRESULT removed = api.m_device->GetDeviceRemovedReason( );
    int covered = 0, moved = 0;
    for ( int y = h / 2; y < h; ++y )
        for ( int x = 0; x < w; ++x )
        {
            const std::uint8_t* p = one.data( ) + static_cast< std::size_t >( ( y * w + x ) * 4 );
            const std::uint8_t* q2 = two.data( ) + static_cast< std::size_t >( ( y * w + x ) * 4 );
            if ( !( p[ 0 ] == 255 && p[ 1 ] == 0 && p[ 2 ] == 255 ) )
                ++covered;
            if ( std::memcmp( p, q2, 3 ) != 0 )
                ++moved;
        }
    const int half = w * ( h - h / 2 );
    char shot[ 512 ] = { };
    if ( GetEnvironmentVariableA( "HYDRO_WATER_SHOT", shot, sizeof( shot ) ) != 0 )
        water_direct_bmp( shot, one, w, h );
    if ( FAILED( removed ) || covered < half * 9 / 10 || moved < half / 4 )
    {
        LOG_ERROR( "gfx_direct FAILED on the %s device: removed 0x%08x, %d of %d covered, %d moved", driver, static_cast< unsigned >( removed ), covered, half, moved );
        return false;
    }
    LOG_SUCCESS( "gfx_direct: render/direct.hxx ran retail's Water/Complex/FogReflect on a real %s device (level 0x%x): %d of %d lower pixels covered, %d moved between two wave frames", driver, static_cast< unsigned >( level ), covered, half, moved );
    return true;
}
#endif