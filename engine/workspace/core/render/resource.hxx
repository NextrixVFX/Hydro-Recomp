#pragma once

// VuD3d11VertexDeclaration (vtable 0x74D8C8, 0x20 bytes) and VuD3d11Texture (vtable 0x74FC54, 0x28 bytes) recreated 1:1 (water batch 35).
// Retail's declaration list head 0x759738 / count 0x75973C is gfx_declarations_t's vector. SUBSTITUTION: std::vector for MSVC's std::list,
// the same order and the same first match.
//
// Not recreated: 0x606550, the byte swap 0x606150 runs when g 0x75906B is set (its writers not read: the case runs with 0).
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

// VuMetroD3d11VertexBuffer (vtable 0x74F108) and IndexBuffer (0x74F160), 0x30 bytes. gfx_buffer_t's +0x10 the buffer; +0xC the size in bytes or in indices; +0x14 the CPU copy, in retail only the
// creator's: a null copy maps instead. SUBSTITUTION: the base ctor 0x563540's list 0x759744 (the device-lost walk reads it, not recreated) is not kept.
struct gfx_data_buffer_t : gfx_buffer_t
{
    std::uint32_t m_refs = 1;                  // +4
    std::uint32_t m_count = 0;                 // +0xC
    std::vector< std::uint8_t > m_copy;        // +0x14
    bool m_has_copy = true;
    gfx_buffer_desc_t m_desc{ };               // +0x18
    std::uint32_t m_unit = 1u;                 // 1 a vertex buffer, 2 an index buffer: an index is 2 bytes

    // 0x5636D0 / 0x563A20: a new buffer of the size; the old released first, the copy replaced only if there was one.
    void resize( gfx_api_t& api, std::uint32_t count )
    {
        api.release( m_buffer );
        m_desc.m_size = count * m_unit;
        m_buffer = api.create_buffer( m_desc );
        m_count = count;
        if ( m_has_copy )
            m_copy.assign( count * m_unit, 0 );
    }

    // 0x563730 / 0x563A90 (data, count).
    void set( gfx_api_t& api, const void* data, std::uint32_t count )
    {
        const std::uint32_t bytes = count * m_unit;
        if ( m_has_copy )
        {
            api.copy( m_copy.data( ), m_count * m_unit, data, bytes );
            api.update_texture( m_buffer, 0u, data, bytes, 0u );
            return;
        }
        void* mapped = api.map( m_buffer, 4u );
        api.copy( mapped, m_count * m_unit, data, bytes );
        api.unmap( m_buffer );
    }
};

// 0x5637F0 (the size) / 0x563B50 (the index count).
inline gfx_data_buffer_t* gfx_buffer_create( gfx_api_t& api, std::uint32_t count, bool indices )
{
    gfx_data_buffer_t* b = new gfx_data_buffer_t( );
    b->m_unit = indices ? 2u : 1u;
    b->m_count = count;
    b->m_desc = gfx_buffer_desc_t{ count * b->m_unit, 0u, indices ? 2u : 1u, 0u, 0u, 0u };
    b->m_copy.assign( count * b->m_unit, 0 );
    b->m_buffer = api.create_buffer( b->m_desc );
    return b;
}

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

// 0x6061E0 0x606350's size of a level, 0x606490's pitch, by the image's code. A code past 14 answers 0 as retail does.
inline std::uint32_t gfx_image_size( std::uint32_t code, std::int32_t width, std::int32_t height )
{
    const std::int32_t w = ( std::max )( width, 1 ), h = ( std::max )( height, 1 );
    const std::int32_t blocks = ( ( h + 3 ) / 4 ) * ( ( w + 3 ) / 4 );
    switch ( code )
    {
    case 1: case 2: return static_cast< std::uint32_t >( h * w * 4 );
    case 3: return static_cast< std::uint32_t >( h * w * 3 );
    case 4: case 11: case 12: case 13: case 14: return static_cast< std::uint32_t >( h * w * 2 );
    case 5: return static_cast< std::uint32_t >( h * w );
    case 6: case 7: return static_cast< std::uint32_t >( blocks * 8 );
    case 8: return static_cast< std::uint32_t >( blocks * 16 );
    case 9: case 10: return static_cast< std::uint32_t >( ( ( std::max )( h, 8 ) * ( std::max )( w, 8 ) * 4 + 7 ) / 8 );
    default: return 0u;
    }
}

inline std::uint32_t gfx_image_pitch( std::uint32_t code, std::int32_t width )
{
    const std::int32_t w = ( std::max )( width, 1 );
    switch ( code )
    {
    case 1: case 2: return static_cast< std::uint32_t >( w * 4 );
    case 3: return static_cast< std::uint32_t >( w * 3 );
    case 4: case 11: case 12: case 13: case 14: return static_cast< std::uint32_t >( w * 2 );
    case 5: return static_cast< std::uint32_t >( w );
    case 6: case 7: case 9: case 10: return static_cast< std::uint32_t >( ( w + 3 ) / 4 * 8 );
    case 8: return static_cast< std::uint32_t >( ( w + 3 ) / 4 * 16 );
    default: return 0u;
    }
}

// 0x57C740 on a VuTextureAsset's bytes: 0x5A8A50's five option words, width, height, levels, the DXGI format, then 0x606150's image: code, width, height, levels, a counted block. The
// texture and its view first, every level through UpdateSubresource, the object and its sampler last.
inline gfx_texture_object_t* gfx_texture_load( gfx_api_t& api, const std::uint8_t* bytes )
{
    std::uint32_t head[ 14 ];
    std::memcpy( head, bytes, sizeof( head ) );
    gfx_texture_options_t o;
    o.m_address_u = head[ 0 ];
    o.m_address_v = head[ 1 ];
    o.m_min = static_cast< std::int32_t >( head[ 2 ] );
    o.m_mag = static_cast< std::int32_t >( head[ 3 ] );
    o.m_mip = static_cast< std::int32_t >( head[ 4 ] );
    const gfx_texture_desc_t d{ head[ 5 ], head[ 6 ], head[ 7 ], 1u, head[ 8 ], 1u, 0u, 0u, 8u, 0u, 0u };
    const gfx_handle_t texture = api.create_texture( d );
    const gfx_handle_t view = api.create_view( texture );
    const std::uint32_t code = head[ 9 ];
    const std::int32_t w = static_cast< std::int32_t >( head[ 10 ] ), h = static_cast< std::int32_t >( head[ 11 ] );
    const std::uint8_t* data = bytes + sizeof( head );
    for ( std::uint32_t level = 0; level < head[ 7 ]; ++level )
    {
        const std::int32_t lw = w >> level, lh = h >> level;
        const std::uint32_t size = gfx_image_size( code, lw, lh );
        api.update_texture( texture, level, data, gfx_image_pitch( code, lw ), size );
        data += size;
    }
    gfx_texture_object_t* t = new gfx_texture_object_t( );
    t->m_width = static_cast< std::int32_t >( head[ 5 ] );
    t->m_height = static_cast< std::int32_t >( head[ 6 ] );
    t->m_levels = head[ 7 ];
    gfx_texture_sampler( api, *t, o );
    t->m_format = head[ 8 ];
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

// VuMetroD3d11RenderTarget (vtable 0x74FD90, 0x1C bytes), create 0x57F7D0. gfx_target_t holds what the device reads; +0xC / +0x10 the two texture objects (slot 3 0x4B65A0 answers +0x10). Slot 1 is
// retail's empty 0x401560. SUBSTITUTION: the list 0x759874 (ctor 0x57F630, only the device-lost walk reads it, not recreated) is not kept.
struct gfx_target_object_t : gfx_target_t
{
    gfx_texture_object_t* m_depth_texture = nullptr;     // +0xC
    gfx_texture_object_t* m_colour = nullptr;            // +0x10
};

// 0x57F7D0 (ecx width, edx height, the format code: 9 and 4 are RGBA8, anything else DXGI 0). Sampler first, then the texture, then the views, for each of the two.
inline gfx_target_object_t* gfx_target_create( gfx_api_t& api, std::int32_t width, std::int32_t height, std::uint32_t format )
{
    gfx_texture_options_t o;
    o.m_address_u = o.m_address_v = 1u;
    o.m_min = o.m_mag = 2;
    const auto object = [ & ]( std::uint32_t dxgi )
    {
        gfx_texture_object_t* t = new gfx_texture_object_t( );
        t->m_width = width;
        t->m_height = height;
        t->m_levels = 1u;
        gfx_texture_sampler( api, *t, o );
        t->m_format = dxgi;
        return t;
    };
    gfx_texture_desc_t d{ static_cast< std::uint32_t >( width ), static_cast< std::uint32_t >( height ), 1u, 1u, k_gfx_texture_formats[ 1 ], 1u, 0u, 0u, 0x40u, 0u, 0u };
    gfx_target_object_t* r = new gfx_target_object_t( );
    r->m_width = static_cast< std::uint32_t >( width );
    r->m_height = static_cast< std::uint32_t >( height );
    r->m_depth_texture = object( k_gfx_texture_formats[ 1 ] );
    r->m_depth_texture->m_texture = api.create_texture( d );
    r->m_depth = api.create_depth_view( r->m_depth_texture->m_texture );
    const std::uint32_t colour = format == 9u || format == 4u ? 0x1Cu : 0u;
    r->m_colour = object( colour );
    d.m_format = colour;
    d.m_bind = 0x28u;
    r->m_colour->m_texture = api.create_texture( d );
    r->m_colour->m_view = api.create_view( r->m_colour->m_texture );
    r->m_view = api.create_target_view( r->m_colour->m_texture );
    return r;
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
