#pragma once

// VuD3d11ShaderProgram (vtable 0x751124, 0x3A0 bytes; the object) + VuD3d11VertexShader / VuD3d11PixelShader (vtables 0x751174 /
// 0x75117C, 0x34 bytes): the compiled shaders' sharing, their constants' memory and the constant setters, recreated 1:1 (water
// batch 34). Retail's two lists 0x75987C (programs) and 0x759884 (stages), made by the CRT initializer 0x5AB990 and freed by
// 0x5ABA20, are gfx_programs_t's vectors. SUBSTITUTION: std::vector for MSVC's std::list, the same order and the same first match.
//
// The stream 0x5AC0C0 reads (little endian, no bounds check): vertex stage, pixel stage, a counted byte block; a stage is hash,
// constants size, a counted byte block (the DXBC). The block holds the constant table: count, then { name hash, stage 0 / 1,
// offset }, then the sampler count and { name hash, slot }, and 0x5AC240 reads it only for a program that is new.
// Verified against retail: hydro checks/device.hxx verify_gfx_program (case gfx_program).

#include <workspace/core/render/gfx.hxx>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

// 0x5ABBC0's answer: a table record (+0x1C + 12i).
struct gfx_constant_slot_t
{
    std::uint32_t m_hash = 0, m_stage = 0, m_offset = 0;
};

struct gfx_sampler_slot_t
{
    std::uint32_t m_hash = 0, m_slot = 0;
};

struct gfx_program_t : gfx_program_stages_t
{
    std::uint32_t m_refs = 1;                          // +4
    std::vector< gfx_constant_slot_t > m_constants;    // +0x18 count, +0x1C
    std::vector< gfx_sampler_slot_t > m_samplers;      // +0x31C count, +0x320

    // The 0x44C780 hash, inline in both lookups.
    static std::uint32_t hash( const char* text )
    {
        std::uint32_t h = 0x811C9DC5u;
        for ( ; *text; ++text )
            h = ( h ^ static_cast< std::uint8_t >( *text ) ) * 0x1000193u;
        return h;
    }

    // 0x5ABBC0 slot 1: the first record of the name, or null.
    const gfx_constant_slot_t* constant( const char* name ) const
    {
        const std::uint32_t h = hash( name );
        for ( const gfx_constant_slot_t& c : m_constants )
            if ( c.m_hash == h )
                return &c;
        return nullptr;
    }

    // 0x5ABC20 slot 2: the slot, or -1.
    std::int32_t sampler( const char* name ) const
    {
        const std::uint32_t h = hash( name );
        for ( const gfx_sampler_slot_t& s : m_samplers )
            if ( s.m_hash == h )
                return static_cast< std::int32_t >( s.m_slot );
        return -1;
    }
};

struct gfx_programs_t
{
    explicit gfx_programs_t( gfx_api_t& api ) : m_api( api ) { }

    gfx_api_t& m_api;
    std::vector< gfx_program_t* > m_programs;   // 0x75987C
    std::vector< gfx_stage_t* > m_stages;       // 0x759884

    // A cursor over the stream: 0x5AC7F0's reader object is { data, size, position }.
    struct reader_t
    {
        const std::uint8_t* m_data = nullptr;
        std::uint32_t m_at = 0;

        std::uint32_t word( )
        {
            std::uint32_t v;
            std::memcpy( &v, m_data + m_at, 4 );
            m_at += 4;
            return v;
        }

        // 0x5AC7F0: a count, then the bytes.
        std::vector< std::uint8_t > block( )
        {
            const std::uint32_t n = word( );
            std::vector< std::uint8_t > out( m_data + m_at, m_data + m_at + n );
            m_at += n;
            return out;
        }
    };

    // 0x5AC0C0: the program of the stream, shared when both its stages match one already made.
    gfx_program_t* load( const std::uint8_t* stream )
    {
        reader_t r{ stream, 0 };
        gfx_stage_t* vertex = read_stage( 0u, r );
        gfx_stage_t* pixel = read_stage( 1u, r );
        const std::vector< std::uint8_t > table = r.block( );
        for ( gfx_program_t* p : m_programs )
            if ( p->m_vertex == vertex && p->m_pixel == pixel )
            {
                drop( vertex );
                drop( pixel );
                ++p->m_refs;
                return p;
            }
        gfx_program_t* p = new gfx_program_t( );
        p->m_vertex = vertex;
        p->m_pixel = pixel;
        read_table( *p, table );
        m_programs.push_back( p );
        return p;
    }

    // Vu's reference count: 0 runs the dtor 0x5ABAE0 (each stage dropped, then the list record removed).
    void release( gfx_program_t* p )
    {
        if ( --p->m_refs != 0u )
            return;
        drop( p->m_vertex );
        drop( p->m_pixel );
        m_programs.erase( std::find( m_programs.begin( ), m_programs.end( ), p ) );
        delete p;
    }

    // Slot 3 0x5ABC90: a bool as an int.
    static void set_bool( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, std::uint8_t v )
    {
        const std::int32_t i = v;
        store( api, p, c, &i, 4u );
    }

    // Slots 4 / 6 0x5ABCF0: a word by value (int and float share it).
    static void set_word( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, std::uint32_t v ) { store( api, p, c, &v, 4u ); }
    // Slots 5 / 9 0x5ABD30: 12 bytes.
    static void set_vec3( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, const void* v ) { store( api, p, c, v, 12u ); }
    // Slot 7 0x5ABD70: 64 bytes.
    static void set_matrix( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, const void* v ) { store( api, p, c, v, 0x40u ); }
    // Slot 8 0x5ABDB0: 8 bytes.
    static void set_vec2( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, const void* v ) { store( api, p, c, v, 8u ); }
    // Slot 10 0x5ABDF0: 16 bytes.
    static void set_vec4( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, const void* v ) { store( api, p, c, v, 0x10u ); }

    // Slot 11 0x5ABE30: a VuColor's r, g, b over 255.
    static void set_colour3( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, const std::uint8_t* v )
    {
        const float s = 0.0039215689f;   // 0x6F75BC
        const float f[ 3 ] = { static_cast< float >( v[ 0 ] ) * s, static_cast< float >( v[ 1 ] ) * s, static_cast< float >( v[ 2 ] ) * s };
        store( api, p, c, f, 12u );
    }

    // Slot 12 0x5ABED0: r, g, b, a over 255.
    static void set_colour4( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, const std::uint8_t* v )
    {
        const float s = 0.0039215689f;
        const float f[ 4 ] = { static_cast< float >( v[ 0 ] ) * s, static_cast< float >( v[ 1 ] ) * s, static_cast< float >( v[ 2 ] ) * s, static_cast< float >( v[ 3 ] ) * s };
        store( api, p, c, f, 0x10u );
    }

    // Slot 13 0x5ABF80: bools as ints (retail's local holds at most 16).
    static void set_bools( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, const std::uint8_t* v, std::uint32_t n )
    {
        std::int32_t w[ 16 ] = { };
        for ( std::uint32_t i = 0; i < n; ++i )
            w[ i ] = v[ i ];
        store( api, p, c, w, n * 4u );
    }

    // Slots 14 / 15 0x5AC000, 16 0x5AC040, 17 0x5AC080: n words, n matrices, n vectors.
    static void set_words( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, const void* v, std::uint32_t n ) { store( api, p, c, v, n << 2 ); }
    static void set_matrices( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, const void* v, std::uint32_t n ) { store( api, p, c, v, n << 6 ); }
    static void set_vectors( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, const void* v, std::uint32_t n ) { store( api, p, c, v, n << 4 ); }

    // A stage's reference: 0 runs 0x5AB890 / 0x5AB910 then 0x5AC380: the shader released, the constant buffer if any, the record removed.
    void drop( gfx_stage_t* s )
    {
        if ( --s->m_refs != 0u )
            return;
        m_api.release( s->m_shader );
        if ( s->m_constants )
            m_api.release( s->m_constants );
        m_stages.erase( std::find( m_stages.begin( ), m_stages.end( ), s ) );
        delete s;
    }

private:
    static void store( gfx_api_t& api, const gfx_program_t& p, const gfx_constant_slot_t& c, const void* from, std::uint32_t bytes )
    {
        gfx_stage_t* s = c.m_stage == 0u ? p.m_vertex : p.m_pixel;
        api.copy( s->m_memory.data( ) + c.m_offset, s->m_size - c.m_offset, from, bytes );
        s->m_dirty = true;
    }

    // 0x5AC470 (the kind in ecx, the reader in edx): shared by hash alone, across both kinds, as retail's search is.
    gfx_stage_t* read_stage( std::uint32_t kind, reader_t& r )
    {
        const std::uint32_t hash = r.word( );
        const std::uint32_t size = r.word( );
        const std::vector< std::uint8_t > code = r.block( );
        for ( gfx_stage_t* s : m_stages )
            if ( s->m_hash == hash )
            {
                ++s->m_refs;
                return s;
            }
        gfx_stage_t* s = new gfx_stage_t( );
        make_stage( *s, kind, code, size );
        s->m_hash = hash;
        m_stages.push_back( s );
        return s;
    }

    // 0x5AC5C0: the shader from the DXBC, its code kept, then (only when the shader has constants) zeroed memory and a dynamic
    // constant buffer.
    void make_stage( gfx_stage_t& s, std::uint32_t kind, const std::vector< std::uint8_t >& code, std::uint32_t size )
    {
        s.m_shader = kind == 0u ? m_api.create_vertex_shader( code.data( ), static_cast< std::uint32_t >( code.size( ) ) )
                                : m_api.create_pixel_shader( code.data( ), static_cast< std::uint32_t >( code.size( ) ) );
        s.m_kind = kind;
        s.m_code = code;
        if ( size == 0u )
            return;
        s.m_memory.assign( size, 0 );
        s.m_data = s.m_memory.data( );
        s.m_size = size;
        s.m_constants = m_api.create_buffer( gfx_buffer_desc_t{ size, 2u, 4u, 0x10000u, 0u, 0u } );
    }

    // 0x5AC240.
    static void read_table( gfx_program_t& p, const std::vector< std::uint8_t >& t )
    {
        reader_t r{ t.data( ), 0 };
        p.m_constants.resize( r.word( ) );
        for ( gfx_constant_slot_t& c : p.m_constants )
        {
            c.m_hash = r.word( );
            c.m_stage = r.word( );
            c.m_offset = r.word( );
        }
        p.m_samplers.resize( r.word( ) );
        for ( gfx_sampler_slot_t& s : p.m_samplers )
        {
            s.m_hash = r.word( );
            s.m_slot = r.word( );
        }
    }
};
