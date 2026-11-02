#pragma once

// VuD3d11VertexDeclaration (vtable 0x74D8C8, 0x20 bytes) and VuD3d11Texture (vtable 0x74FC54, 0x28 bytes) recreated 1:1 (water batch 35).
// Retail's declaration list head 0x759738 / count 0x75973C is gfx_declarations_t's vector. SUBSTITUTION: std::vector for MSVC's std::list,
// the same order and the same first match.
//
// Not recreated: the asset-file texture create 0x57C740 and its DDS reader 0x606150 (the running game loads textures itself).
// Verified against retail: hydro checks/device.hxx verify_water_material (case water_material runs both through the water shader's ctor).

#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/program.hxx>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

// VuVertexElement: u16 stream, u16 offset, type, usage, u8 index (0x10 bytes, three pad bytes retail compares: 0x541150 is a memcmp of
// the whole array, and the elements here are made zeroed).
struct gfx_vertex_element_t
{
    std::uint16_t m_stream = 0, m_offset = 0;
    std::uint32_t m_type = 0, m_usage = 0;
    std::uint8_t m_index = 0;
};

inline bool operator==( const gfx_vertex_element_t& a, const gfx_vertex_element_t& b )
{
    return a.m_stream == b.m_stream && a.m_offset == b.m_offset && a.m_type == b.m_type && a.m_usage == b.m_usage && a.m_index == b.m_index;
}

inline const char* const k_gfx_semantics[ 7 ] = { "POSITION", "NORMAL", "COLOR", "TANGENT", "BLENDWEIGHT", "BLENDINDICES", "TEXCOORD" };   // 0x7512CC
inline constexpr std::uint32_t k_gfx_vertex_formats[ 15 ] = { 41, 16, 6, 2, 32, 31, 30, 28, 37, 13, 0, 0, 34, 10, 0 };                     // 0x751290
inline constexpr std::uint32_t k_gfx_texture_formats[ 14 ] = { 0, 45, 51, 51, 28, 61, 61, 0, 0, 85, 85, 41, 4, 8 };                        // 0x757BD4
inline constexpr std::uint32_t k_gfx_address_map[ 2 ] = { 1, 3 };                                                                          // 0x751324

struct gfx_declaration_t
{
    std::uint32_t m_refs = 1;                         // +4
    std::vector< gfx_vertex_element_t > m_elements;   // +0xC
    gfx_handle_t m_layout = 0;                        // +0x18
    gfx_stage_t* m_stage = nullptr;                   // +0x1C, the vertex stage, one reference held
};

struct gfx_declarations_t
{
    gfx_declarations_t( gfx_api_t& api, gfx_programs_t& programs ) : m_api( api ), m_programs( programs ) { }

    gfx_api_t& m_api;
    gfx_programs_t& m_programs;
    std::vector< gfx_declaration_t* > m_list;

    // 0x540E00 (fastcall: the elements, the program): shared when the vertex stage and the elements match.
    gfx_declaration_t* create( const std::vector< gfx_vertex_element_t >& elements, const gfx_program_stages_t& program )
    {
        gfx_stage_t* stage = program.m_vertex;
        for ( gfx_declaration_t* d : m_list )
            if ( d->m_stage == stage && d->m_elements == elements )
            {
                ++d->m_refs;
                return d;
            }
        gfx_declaration_t* d = new gfx_declaration_t( );
        d->m_elements = elements;
        d->m_stage = stage;
        ++stage->m_refs;
        // 0x540F70: the D3D11 elements.
        std::vector< gfx_layout_element_t > in( elements.size( ) );
        for ( std::size_t i = 0; i < elements.size( ); ++i )
            in[ i ] = gfx_layout_element_t{ k_gfx_semantics[ elements[ i ].m_usage ], elements[ i ].m_index, k_gfx_vertex_formats[ elements[ i ].m_type ],
                                            elements[ i ].m_stream, elements[ i ].m_offset, 0u, 0u };
        d->m_layout = m_api.create_layout( in.data( ), static_cast< std::uint32_t >( in.size( ) ), stage->m_code.data( ), static_cast< std::uint32_t >( stage->m_code.size( ) ) );
        m_list.push_back( d );
        return d;
    }

    // Vu's reference count, then the dtor 0x540CD0: the stage's reference first, the list, the layout.
    void release( gfx_declaration_t* d )
    {
        if ( --d->m_refs != 0u )
            return;
        m_programs.drop( d->m_stage );
        m_list.erase( std::find( m_list.begin( ), m_list.end( ), d ) );
        if ( d->m_layout )
            m_api.release( d->m_layout );
        delete d;
    }
};

// The options 0x57C9B0 reads: +0 / +4 the addressing (0 wrap, 1 clamp through 0x751324), +8 / +0xC / +0x10 the filters (2 or more: linear),
// and +0x10 not 0 asks for the whole mip chain.
struct gfx_texture_options_t
{
    std::uint32_t m_address_u = 0, m_address_v = 0;
    std::int32_t m_min = 0, m_mag = 0, m_mip = 0;
};

// VuD3d11Texture: the part 0x540B50 binds is gfx_texture_t (kind 0).
struct gfx_texture_object_t : gfx_texture_t
{
    std::uint32_t m_refs = 1;                        // +4
    std::int32_t m_width = 0, m_height = 0;          // +0xC, +0x10
    std::uint32_t m_levels = 0;                      // +0x14
    std::uint32_t m_format = 0;                      // +0x18, DXGI
    gfx_handle_t m_texture = 0;                      // +0x1C (+0x20 the view, +0x24 the sampler: gfx_texture_t)
};

// 0x57C4B0 (the part after the fields): the sampler.
inline void gfx_texture_sampler( gfx_api_t& api, gfx_texture_object_t& t, const gfx_texture_options_t& o )
{
    std::uint32_t filter = o.m_min >= 2 ? 4u : 0u;
    if ( o.m_mag >= 2 )
        filter |= 0x10u;
    if ( o.m_mip >= 2 )
        filter |= 1u;
    t.m_sampler = api.create_sampler( gfx_sampler_desc_t{ filter, k_gfx_address_map[ o.m_address_u ], k_gfx_address_map[ o.m_address_v ], 1u, 0.0f, 1u, 8u, { 0.0f, 0.0f, 0.0f, 0.0f }, 0.0f, 3.4028235e38f } );
}

// 0x57C9B0 (width, height, an unread word, format, options): an empty dynamic-by-update texture: the description, the view, the sampler.
inline gfx_texture_object_t* gfx_texture_create( gfx_api_t& api, std::int32_t width, std::int32_t height, std::uint32_t format, const gfx_texture_options_t& o )
{
    std::uint32_t levels = 1u;
    if ( o.m_mip != 0 )
    {
        std::uint32_t n = 0u;
        for ( std::uint32_t v = static_cast< std::uint32_t >( width > height ? width : height ) >> 1; v != 0u; v >>= 1 )
            ++n;
        levels = n + 1u;
    }
    gfx_texture_desc_t d{ static_cast< std::uint32_t >( width ), static_cast< std::uint32_t >( height ), levels, 1u, k_gfx_texture_formats[ format ], 1u, 0u, 0u, 8u, 0u, 0u };
    const gfx_handle_t texture = api.create_texture( d );
    const gfx_handle_t view = api.create_view( texture );
    gfx_texture_object_t* t = new gfx_texture_object_t( );
    t->m_width = width;
    t->m_height = height;
    t->m_levels = levels;
    gfx_texture_sampler( api, *t, o );
    t->m_format = k_gfx_texture_formats[ format ];
    t->m_texture = texture;
    t->m_view = view;
    return t;
}

// 0x57C6C0 vt+8 (level, bytes, an unread size): a format the table does not name uploads with pitch 0, as retail does.
inline void gfx_texture_update( gfx_api_t& api, const gfx_texture_object_t& t, std::int32_t level, const void* bytes )
{
    const std::int32_t w = ( std::max )( t.m_width >> level, 1 ), h = ( std::max )( t.m_height >> level, 1 );
    std::uint32_t pixel = 0u;
    if ( t.m_format == 0x3Du )
        pixel = 1u;
    else if ( t.m_format == 0x33u || t.m_format == 0x55u )
        pixel = 2u;
    else if ( t.m_format == 0x1Cu )
        pixel = 4u;
    const std::uint32_t row = pixel * static_cast< std::uint32_t >( w );
    api.update_texture( t.m_texture, static_cast< std::uint32_t >( level ), bytes, row, row * static_cast< std::uint32_t >( h ) );
}

// 0x57C610: sampler handle +0x20 (the view), +0x1C, +0x24: retail releases the view, the texture, then the sampler.
inline void gfx_texture_release( gfx_api_t& api, gfx_texture_object_t* t )
{
    if ( --t->m_refs != 0u )
        return;
    if ( t->m_view )
        api.release( t->m_view );
    if ( t->m_texture )
        api.release( t->m_texture );
    if ( t->m_sampler )
        api.release( t->m_sampler );
    delete t;
}
