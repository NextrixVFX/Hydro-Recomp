#pragma once

// VuD3d11Gfx + VuMetroD3d11Gfx (vtable 0x74D79C, g 0x75907C, 0xE4 bytes; ctors 0x5AC8C0 / 0x540450), recreated 1:1 from the
// disassembly (water batch 34): the pending pipeline state, its state-object caches, the dynamic buffers and every draw.
//
// THE SEAM. Retail calls ID3D11Device / ID3D11DeviceContext / IDXGISwapChain directly. Here each of those calls is one virtual
// of gfx_api_t, named after the D3D11 call, in retail's order with retail's values. gfx_device_t makes no platform decision, so
// WebGL can implement gfx_api_t later without touching it. The descriptions below hold D3D11's numeric values and D3D11's exact
// layouts (render/direct.hxx passes them straight through; a WebGL backend maps the numbers).
//
// Kept as read, because they are what retail does:
//  - commit 0x5AD3F0 never clears a dirty flag: once set, all three states are rebound before every draw.
//  - The rasterizer branch of commit binds the CACHED answer only: the frame that creates a state binds null (D3D11's default
//    rasterizer), the next commit finds it. The depth and blend branches bind the new object at once.
//  - A cache hit on a null object creates again and appends; the search still finds the first record.
//  - The dynamic buffer offsets +0xB0 / +0xB4 reset only at begin 0x540A90; memcpy_s is the only limit.
// Verified against retail: hydro checks/device.hxx verify_gfx_device (case gfx_device).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using gfx_handle_t = std::uintptr_t;

struct gfx_buffer_desc_t   // D3D11_BUFFER_DESC
{
    std::uint32_t m_size, m_usage, m_bind, m_cpu, m_misc, m_stride;
};

struct gfx_texture_desc_t   // D3D11_TEXTURE2D_DESC
{
    std::uint32_t m_width, m_height, m_mips, m_array, m_format, m_samples, m_quality, m_usage, m_bind, m_cpu, m_misc;
};

struct gfx_stencil_op_t   // D3D11_DEPTH_STENCILOP_DESC
{
    std::uint32_t m_fail, m_depth_fail, m_pass, m_func;
};

struct gfx_depth_desc_t   // D3D11_DEPTH_STENCIL_DESC
{
    std::uint32_t m_depth, m_write, m_func, m_stencil;
    std::uint8_t m_read_mask, m_write_mask;
    gfx_stencil_op_t m_front, m_back;
};

struct gfx_target_blend_t   // D3D11_RENDER_TARGET_BLEND_DESC
{
    std::uint32_t m_enable, m_src, m_dst, m_op, m_src_alpha, m_dst_alpha, m_op_alpha;
    std::uint8_t m_mask;
};

struct gfx_blend_desc_t   // D3D11_BLEND_DESC
{
    std::uint32_t m_coverage, m_independent;
    gfx_target_blend_t m_targets[ 8 ];
};

struct gfx_raster_desc_t   // D3D11_RASTERIZER_DESC
{
    std::uint32_t m_fill, m_cull, m_front_ccw;
    std::int32_t m_bias;
    float m_bias_clamp, m_slope;
    std::uint32_t m_clip, m_scissor, m_multisample, m_line_aa;
};

struct gfx_viewport_t   // D3D11_VIEWPORT
{
    float m_x, m_y, m_width, m_height, m_near, m_far;
};

struct gfx_rect_t   // D3D11_RECT
{
    std::int32_t m_left, m_top, m_right, m_bottom;
};

struct gfx_layout_element_t   // D3D11_INPUT_ELEMENT_DESC
{
    const char* m_name;
    std::uint32_t m_index, m_format, m_slot, m_offset, m_class, m_step;
};

struct gfx_sampler_desc_t   // D3D11_SAMPLER_DESC
{
    std::uint32_t m_filter, m_u, m_v, m_w;
    float m_bias;
    std::uint32_t m_anisotropy, m_compare;
    float m_border[ 4 ];
    float m_min, m_max;
};

struct gfx_api_t
{
    virtual ~gfx_api_t( ) = default;

    // ID3D11Device. A failure answers 0: retail tests none of the state calls and stores what it got.
    virtual gfx_handle_t create_buffer( const gfx_buffer_desc_t& d ) = 0;           // +0xC
    virtual gfx_handle_t create_texture( const gfx_texture_desc_t& d ) = 0;         // +0x14
    virtual gfx_handle_t create_view( gfx_handle_t texture ) = 0;                   // +0x1C (null description)
    virtual gfx_handle_t create_target_view( gfx_handle_t texture ) = 0;            // +0x24 (null description)
    virtual gfx_handle_t create_depth_view( gfx_handle_t texture ) = 0;             // +0x28 (null description)
    virtual gfx_handle_t create_vertex_shader( const void* code, std::uint32_t size ) = 0;   // +0x30 (code, size, 0, &out)
    virtual gfx_handle_t create_pixel_shader( const void* code, std::uint32_t size ) = 0;    // +0x3C (code, size, 0, &out)
    virtual gfx_handle_t create_layout( const gfx_layout_element_t* e, std::uint32_t count, const void* code, std::uint32_t size ) = 0;   // +0x2C
    virtual gfx_handle_t create_sampler( const gfx_sampler_desc_t& d ) = 0;         // +0x5C
    virtual gfx_handle_t create_blend( const gfx_blend_desc_t& d ) = 0;             // +0x50
    virtual gfx_handle_t create_depth( const gfx_depth_desc_t& d ) = 0;             // +0x54
    virtual gfx_handle_t create_raster( const gfx_raster_desc_t& d ) = 0;           // +0x58
    // IUnknown::Release once, and retail's "do Release while the count is not 0".
    virtual void release( gfx_handle_t object ) = 0;
    virtual void release_all( gfx_handle_t object ) = 0;

    // ID3D11DeviceContext.
    virtual void vs_constants( gfx_handle_t buffer ) = 0;                           // +0x1C (0, 1, &buffer)
    virtual void ps_views( std::uint32_t slot, gfx_handle_t view ) = 0;             // +0x20 (slot, 1, &view)
    virtual void ps_shader( gfx_handle_t shader ) = 0;                              // +0x24 (shader, 0, 0)
    virtual void ps_samplers( std::uint32_t slot, gfx_handle_t sampler ) = 0;       // +0x28 (slot, 1, &sampler)
    virtual void vs_shader( gfx_handle_t shader ) = 0;                              // +0x2C (shader, 0, 0)
    virtual void draw_indexed( std::uint32_t count, std::uint32_t start, std::int32_t base ) = 0;   // +0x30
    virtual void draw( std::uint32_t count, std::uint32_t start ) = 0;              // +0x34
    virtual void* map( gfx_handle_t resource, std::uint32_t type ) = 0;             // +0x38 (resource, 0, type, 0, &mapped)
    virtual void unmap( gfx_handle_t resource ) = 0;                                // +0x3C (resource, 0)
    virtual void ps_constants( gfx_handle_t buffer ) = 0;                           // +0x40 (0, 1, &buffer)
    virtual void input_layout( gfx_handle_t layout ) = 0;                           // +0x44
    virtual void vertex_buffer( gfx_handle_t buffer, std::uint32_t stride, std::uint32_t offset ) = 0;   // +0x48 (0, 1, ...)
    virtual void index_buffer( gfx_handle_t buffer, std::uint32_t format, std::uint32_t offset ) = 0;    // +0x4C
    virtual void topology( std::uint32_t value ) = 0;                               // +0x60
    virtual void set_targets( std::uint32_t count, gfx_handle_t view, gfx_handle_t depth ) = 0;   // +0x84
    virtual void set_blend( gfx_handle_t state ) = 0;                               // +0x8C (state, null factor, 0xFFFFFFFF)
    virtual void set_depth( gfx_handle_t state ) = 0;                               // +0x90 (state, 0)
    virtual void set_raster( gfx_handle_t state ) = 0;                              // +0xAC
    virtual void set_viewport( const gfx_viewport_t& v ) = 0;                       // +0xB0 (1, &v)
    virtual void set_scissor( const gfx_rect_t& r ) = 0;                            // +0xB4 (1, &r)
    virtual void update_texture( gfx_handle_t texture, std::uint32_t level, const void* data, std::uint32_t row, std::uint32_t depth ) = 0;   // +0xC0 (null box)
    virtual void clear_target( gfx_handle_t view, const float* rgba ) = 0;          // +0xC8
    virtual void clear_depth( gfx_handle_t view, std::uint32_t flags, float depth, std::uint8_t stencil ) = 0;   // +0xD4
    // +0x164 OMGetRenderTargets( 1, &view, &depth ). Retail never releases what it gets, and its resize answers that with the
    // release_all loops. SUBSTITUTION: a backend hands these out without holding the extra reference; nothing else differs.
    virtual void get_targets( gfx_handle_t& view, gfx_handle_t& depth ) = 0;

    // IDXGISwapChain.
    virtual void present( std::uint32_t sync, std::uint32_t flags ) = 0;            // +0x20
    virtual gfx_handle_t back_buffer( ) = 0;                                        // +0x24 (0, the texture's IID, &out)
    virtual void resize( std::uint32_t count, std::uint32_t width, std::uint32_t height, std::uint32_t format, std::uint32_t flags ) = 0;   // +0x34

    // memcpy_s( to, room, from, count ), one seam so the room retail passes can be measured.
    virtual void copy( void* to, std::uint32_t room, const void* from, std::uint32_t count ) = 0;
};

// The static tables, read from the image.
inline constexpr std::uint32_t k_gfx_topology[ 5 ] = { 1, 2, 3, 4, 5 };                                   // 0x7512E8
inline constexpr std::uint32_t k_gfx_vertices[ 5 ][ 2 ] = { { 1, 0 }, { 2, 0 }, { 1, 1 }, { 3, 0 }, { 1, 2 } };   // 0x7512FC: x prims + y
inline constexpr std::uint32_t k_gfx_blend_map[ 11 ] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };             // 0x75132C
inline constexpr std::uint32_t k_gfx_cull_map[ 3 ] = { 1, 3, 2 };                                         // 0x751358
inline constexpr std::uint32_t k_gfx_compare_map[ 8 ] = { 1, 2, 3, 4, 5, 6, 7, 8 };                       // 0x751364

// One shader stage's part of VuD3d11ShaderProgram: VuD3d11VertexShader / VuD3d11PixelShader +0x30 the shader, +0x1C the constant
// buffer, +0x20 the dirty byte, +0x10 / +0x14 the constants' memory.
struct gfx_stage_t
{
    gfx_handle_t m_shader = 0, m_constants = 0;
    bool m_dirty = false;
    const void* m_data = nullptr;
    std::uint32_t m_size = 0;
    // render/program.hxx's part: +4 references, +0xC the DXBC's hash, +0x24 the code, the memory behind m_data.
    std::uint32_t m_refs = 1, m_hash = 0, m_kind = 0;
    std::vector< std::uint8_t > m_code, m_memory;
};

// VuD3d11ShaderProgram's +0x10 / +0x14.
struct gfx_program_stages_t
{
    gfx_stage_t* m_vertex = nullptr;
    gfx_stage_t* m_pixel = nullptr;
};

// VuMetroD3d11VertexBuffer / IndexBuffer: +0x10 the buffer.
struct gfx_buffer_t
{
    gfx_handle_t m_buffer = 0;
};

// VuMetroD3d11RenderTarget: +0x10 its size object (+0xC width, +0x10 height), +0x14 the depth view, +0x18 the target view.
struct gfx_target_t
{
    std::uint32_t m_width = 0, m_height = 0;
    gfx_handle_t m_depth = 0, m_view = 0;
};

// A texture as 0x540B50 reads it, the two RTTI tests made: 2D (typeinfo 0x736AE8): +0x24 sampler, +0x20 view; cube (0x736AF0):
// +0x1C sampler, +0x18 view. m_kind 0 = 2D, 1 = cube, anything else binds nothing.
struct gfx_texture_t
{
    std::uint32_t m_kind = 0;
    gfx_handle_t m_sampler = 0, m_view = 0;
};

struct gfx_device_t
{
    explicit gfx_device_t( gfx_api_t& api ) : m_api( api ) { }

    gfx_api_t& m_api;
    std::uint32_t m_sync = 0;                  // +8
    std::uint32_t m_primitives = 0;            // +0x10
    std::uint32_t m_draws = 0;                 // +0x14
    gfx_handle_t m_chain_view = 0;             // +0xD4
    gfx_handle_t m_chain_depth = 0;            // +0xD8
    std::int32_t m_width = 0, m_height = 0;    // +0x34, +0x38: the bound target's
    std::int32_t m_chain_width = 0, m_chain_height = 0;   // +0xDC, +0xE0
    float m_viewport[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };   // +0x3C
    float m_scissor[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };    // +0x4C
    const gfx_program_stages_t* m_program = nullptr;      // +0x5C

    // The pending depth state, +0x60 its cache of 12-byte records.
    struct depth_record_t { std::uint8_t m_enable, m_write; std::uint32_t m_func; gfx_handle_t m_state; };
    std::vector< depth_record_t > m_depths;
    bool m_depth_dirty = true;                 // +0x6C
    std::uint8_t m_depth_enable = 1;           // +0x6D
    std::uint8_t m_depth_write = 1;            // +0x6E
    std::uint32_t m_depth_func = 1;            // +0x70

    // The pending blend state, +0x74 its cache of 20-byte records.
    struct blend_record_t { std::uint8_t m_enable; std::uint32_t m_src, m_dst; std::uint8_t m_colour; gfx_handle_t m_state; };
    std::vector< blend_record_t > m_blends;
    bool m_blend_dirty = true;                 // +0x80
    std::uint8_t m_blend_enable = 0;           // +0x81
    std::uint32_t m_blend_src = 4, m_blend_dst = 5;       // +0x84, +0x88
    std::uint8_t m_colour_write = 1;           // +0x8C

    // The pending rasterizer state, +0x90 its cache of 12-byte records.
    struct raster_record_t { std::uint32_t m_cull; std::uint8_t m_scissor; gfx_handle_t m_state; };
    std::vector< raster_record_t > m_rasters;
    bool m_raster_dirty = true;                // +0x9C
    std::uint32_t m_cull = 1;                  // +0xA0
    std::uint8_t m_scissor_enable = 0;         // +0xA4

    gfx_handle_t m_dynamic_vertices = 0;       // +0xA8
    gfx_handle_t m_dynamic_indices = 0;        // +0xAC
    std::uint32_t m_vertex_offset = 0;         // +0xB0
    std::uint32_t m_index_offset = 0;          // +0xB4
    const gfx_buffer_t* m_stream = nullptr;    // +0xB8
    std::uint32_t m_stream_offset = 0;         // +0xBC
    std::uint32_t m_stream_stride = 0;         // +0xC0
    const gfx_buffer_t* m_indices = nullptr;   // +0xC4
    bool m_stream_stale = false;               // +0xC8: a dynamic draw replaced the stream
    bool m_indices_stale = false;              // +0xC9

    // 0x5ACF40 0x5ACF60 0x5ACF80: vt +0x94 +0x98 +0x9C.
    void depth_enable( std::uint8_t v ) { m_depth_dirty = true; m_depth_enable = v; }
    void depth_write( std::uint8_t v ) { m_depth_dirty = true; m_depth_write = v; }
    void depth_func( std::uint32_t v ) { m_depth_dirty = true; m_depth_func = v; }
    // 0x5ACFA0 0x5ACFC0 0x5ACFE0: vt +0xA0 +0xA4 +0xA8.
    void blend_enable( std::uint8_t v ) { m_blend_dirty = true; m_blend_enable = v; }
    void blend_func( std::uint32_t src, std::uint32_t dst ) { m_blend_src = src; m_blend_dst = dst; m_blend_dirty = true; }
    void colour_write( std::uint8_t v ) { m_blend_dirty = true; m_colour_write = v; }
    // 0x5AD000 0x5AD020: vt +0xAC +0xB0.
    void cull( std::uint32_t v ) { m_raster_dirty = true; m_cull = v; }
    void scissor_enable( std::uint8_t v ) { m_raster_dirty = true; m_scissor_enable = v; }

    // 0x5ACC00 vt +0x44.
    void size( std::int32_t& w, std::int32_t& h ) const { w = m_width; h = m_height; }
    // 0x5409D0 vt +0x40.
    void chain_size( std::int32_t& w, std::int32_t& h ) const { w = m_chain_width; h = m_chain_height; }

    // 0x5ACA40: the two dynamic buffers, then commit first as retail does.
    void create_dynamic( )
    {
        commit( );
        gfx_buffer_desc_t d{ 0x100000u, 2u, 1u, 0x10000u, 0u, 0u };
        m_dynamic_vertices = m_api.create_buffer( d );
        d.m_size = 0x80000u;
        d.m_bind = 2u;
        m_dynamic_indices = m_api.create_buffer( d );
    }

    // 0x5ACAE0: every cached state object, then the two buffers. The caches empty, their memory stays.
    void release_states( )
    {
        for ( const depth_record_t& r : m_depths )
            m_api.release( r.m_state );
        m_depths.clear( );
        for ( const blend_record_t& r : m_blends )
            m_api.release( r.m_state );
        m_blends.clear( );
        for ( const raster_record_t& r : m_rasters )
            m_api.release( r.m_state );
        m_rasters.clear( );
        m_api.release( m_dynamic_vertices );
        m_api.release( m_dynamic_indices );
    }

    // 0x5ACC20 vt +0x68 (flags: 1 colour, 2 depth; the colour's bytes; depth).
    void clear( std::uint32_t flags, const std::uint8_t* colour, float depth )
    {
        gfx_handle_t view = 0, dsv = 0;
        m_api.get_targets( view, dsv );
        if ( ( flags & 1u ) && view )
        {
            const float scale = 0.0039215689f;   // 0x6F75BC, 1 / 255
            const float rgba[ 4 ] = { static_cast< float >( colour[ 0 ] ) * scale, static_cast< float >( colour[ 1 ] ) * scale,
                                      static_cast< float >( colour[ 2 ] ) * scale, static_cast< float >( colour[ 3 ] ) * scale };
            m_api.clear_target( view, rgba );
        }
        if ( ( flags & 2u ) && dsv )
            m_api.clear_depth( dsv, 1u, depth, 0 );
    }

    // 0x5ACD10 vt +0x70: a rect in 0..1 of the bound target.
    void viewport( const float* rect )
    {
        const float w = static_cast< float >( m_width ), h = static_cast< float >( m_height );
        m_api.set_viewport( gfx_viewport_t{ rect[ 0 ] * w, rect[ 1 ] * h, rect[ 2 ] * w, rect[ 3 ] * h, 0.0f, 1.0f } );
        std::memcpy( m_viewport, rect, 16 );
    }

    // 0x5ACDC0 vt +0x78. fistp in the default rounding mode, which lrintf is; right and bottom sum before rounding.
    void scissor( const float* rect )
    {
        const float w = static_cast< float >( m_width ), h = static_cast< float >( m_height );
        const float x = rect[ 0 ] * w, y = rect[ 1 ] * h;
        const float right = rect[ 2 ] * w + x, bottom = rect[ 3 ] * h + y;
        m_api.set_scissor( gfx_rect_t{ static_cast< std::int32_t >( std::lrintf( x ) ), static_cast< std::int32_t >( std::lrintf( y ) ),
                                       static_cast< std::int32_t >( std::lrintf( right ) ), static_cast< std::int32_t >( std::lrintf( bottom ) ) } );
        std::memcpy( m_scissor, rect, 16 );
    }

    // 0x5ACEB0 vt +0x84.
    void set_program( const gfx_program_stages_t* program )
    {
        m_program = program;
        const gfx_stage_t* v = program->m_vertex;
        const gfx_stage_t* p = program->m_pixel;
        m_api.vs_shader( v->m_shader );
        m_api.vs_constants( v->m_constants );
        m_api.ps_shader( p->m_shader );
        m_api.ps_constants( p->m_constants );
    }

    // 0x5ACF20 vt +0x88: VuD3d11VertexDeclaration +0x18.
    void set_declaration( gfx_handle_t layout ) { m_api.input_layout( layout ); }

    // 0x540AD0 vt +0x8C (buffer, offset, stride).
    void set_stream( const gfx_buffer_t* buffer, std::uint32_t offset, std::uint32_t stride )
    {
        m_api.vertex_buffer( buffer->m_buffer, stride, offset );
        m_stream = buffer;
        m_stream_offset = offset;
        m_stream_stride = stride;
        m_stream_stale = false;
    }

    // 0x540B20 vt +0x90: format 0x39 = R16_UINT.
    void set_index_buffer( const gfx_buffer_t* buffer )
    {
        m_api.index_buffer( buffer->m_buffer, 0x39u, 0u );
        m_indices = buffer;
        m_indices_stale = false;
    }

    // 0x540B50 vt +0xB4. `slots`: the low 16 bits the sampler slot, the high 16 (arithmetic) the view slot. Null: both unbound,
    // on the whole word as a slot, as retail passes it.
    void set_texture( std::uint32_t slots, const gfx_texture_t* texture )
    {
        if ( texture == nullptr )
        {
            m_api.ps_views( slots, 0 );
            m_api.ps_samplers( slots, 0 );
            return;
        }
        if ( texture->m_kind > 1u )
            return;
        m_api.ps_samplers( slots & 0xFFFFu, texture->m_sampler );
        m_api.ps_views( static_cast< std::uint32_t >( static_cast< std::int32_t >( slots ) >> 16 ), texture->m_view );
    }

    // 0x5AD040 vt +0xB8 (topology, start vertex, primitives).
    void draw( std::uint32_t kind, std::uint32_t start, std::uint32_t primitives )
    {
        if ( m_stream_stale )
            set_stream( m_stream, m_stream_offset, m_stream_stride );
        commit( );
        const std::uint32_t count = k_gfx_vertices[ kind ][ 0 ] * primitives + k_gfx_vertices[ kind ][ 1 ];
        m_api.topology( k_gfx_topology[ kind ] );
        m_api.draw( count, start );
        m_primitives += primitives;
        ++m_draws;
    }

    // 0x5AD0C0 vt +0xBC (topology, min index, vertex count, start index, primitives): the second and third are unread.
    void draw_indexed( std::uint32_t kind, std::uint32_t start, std::uint32_t primitives )
    {
        if ( m_stream_stale )
            set_stream( m_stream, m_stream_offset, m_stream_stride );
        if ( m_indices_stale )
            set_index_buffer( m_indices );
        commit( );
        const std::uint32_t count = k_gfx_vertices[ kind ][ 0 ] * primitives + k_gfx_vertices[ kind ][ 1 ];
        m_api.topology( k_gfx_topology[ kind ] );
        m_api.draw_indexed( count, start, 0 );
        m_primitives += primitives;
        ++m_draws;
    }

    // 0x5AD150 vt +0xC0 (topology, primitives, vertices, stride): through the dynamic vertex buffer.
    void draw_dynamic( std::uint32_t kind, std::uint32_t primitives, const void* data, std::uint32_t stride )
    {
        commit( );
        const std::uint32_t count = k_gfx_vertices[ kind ][ 0 ] * primitives + k_gfx_vertices[ kind ][ 1 ];
        upload( m_dynamic_vertices, m_vertex_offset, 0x100000u, data, count * stride );
        m_api.vertex_buffer( m_dynamic_vertices, stride, m_vertex_offset );
        m_api.topology( k_gfx_topology[ kind ] );
        m_api.draw( count, 0u );
        m_vertex_offset += count * stride;
        m_primitives += primitives;
        ++m_draws;
        m_stream_stale = true;
    }

    // 0x5AD260 vt +0xC4 (topology, min index, vertex count, primitives, indices, vertices, stride): the second is unread.
    void draw_indexed_dynamic( std::uint32_t kind, std::uint32_t vertices, std::uint32_t primitives, const void* indices, const void* data, std::uint32_t stride )
    {
        commit( );
        const std::uint32_t count = k_gfx_vertices[ kind ][ 0 ] * primitives + k_gfx_vertices[ kind ][ 1 ];
        upload( m_dynamic_vertices, m_vertex_offset, 0x100000u, data, vertices * stride );
        upload( m_dynamic_indices, m_index_offset, 0x80000u, indices, count + count );
        m_api.vertex_buffer( m_dynamic_vertices, stride, m_vertex_offset );
        m_api.index_buffer( m_dynamic_indices, 0x39u, m_index_offset );
        m_api.topology( k_gfx_topology[ kind ] );
        m_api.draw_indexed( count, 0u, 0 );
        m_vertex_offset += vertices * stride;
        m_index_offset += count + count;
        m_primitives += primitives;
        ++m_draws;
        m_stream_stale = true;
        m_indices_stale = true;
    }

    // 0x540A90 vt +0x58.
    void begin( )
    {
        m_vertex_offset = 0u;
        m_index_offset = 0u;
    }

    // 0x540AB0 vt +0x5C.
    void end( ) { m_api.present( m_sync, 0u ); }

    // 0x5AD7F0 (width, height, an unread word, out): the D24S8 depth buffer. The one that fails leaves *out alone.
    void create_depth_buffer( std::int32_t width, std::int32_t height, gfx_handle_t& out )
    {
        const gfx_texture_desc_t d{ static_cast< std::uint32_t >( width ), static_cast< std::uint32_t >( height ), 1u, 1u, 0x2Du, 1u, 0u, 0u, 0x40u, 0u, 0u };
        const gfx_handle_t texture = m_api.create_texture( d );
        if ( !texture )
            return;
        out = m_api.create_depth_view( texture );
        m_api.release( texture );
    }

    // 0x540890 vt +0x3C: a new size for the chain, ignored when not positive or unchanged.
    void resize( std::int32_t width, std::int32_t height )
    {
        if ( width <= 0 || height <= 0 || ( m_chain_width == width && m_chain_height == height ) )
            return;
        m_api.set_targets( 0u, 0, 0 );
        m_api.release_all( m_chain_depth );
        m_api.release_all( m_chain_view );
        m_api.resize( 2u, static_cast< std::uint32_t >( width ), static_cast< std::uint32_t >( height ), 0x1Cu, 0u );
        const gfx_handle_t back = m_api.back_buffer( );
        m_chain_view = m_api.create_target_view( back );
        m_api.release( back );
        create_depth_buffer( width, height, m_chain_depth );
        m_api.set_targets( 1u, m_chain_view, m_chain_depth );
        m_chain_width = m_width = width;
        m_chain_height = m_height = height;
        const float whole[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
        viewport( whole );
    }

    // 0x5409F0 vt +0x48: a render target, or null for the chain's own.
    void set_target( const gfx_target_t* target )
    {
        if ( target )
        {
            m_api.set_targets( 1u, target->m_view, target->m_depth );
            m_width = static_cast< std::int32_t >( target->m_width );
            m_height = static_cast< std::int32_t >( target->m_height );
        }
        else
        {
            m_api.set_targets( 1u, m_chain_view, m_chain_depth );
            m_width = m_chain_width;
            m_height = m_chain_height;
        }
        const float whole[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
        viewport( whole );
    }

    // 0x5AD8C0 / 0x5AD950 are the caches' push_back (0x5AD9F0, 0x5ADA80, 0x5ADB20, 0x5ADBB0, 0x512970 their growth): SUBSTITUTION
    // std::vector, the same records in the same order. 0x5AD3F0, called before every draw.
    void commit( )
    {
        if ( m_depth_dirty )
        {
            gfx_handle_t state = 0;
            for ( const depth_record_t& r : m_depths )
                if ( r.m_enable == m_depth_enable && r.m_write == m_depth_write && r.m_func == m_depth_func )
                {
                    state = r.m_state;
                    break;
                }
            if ( !state )
            {
                const gfx_stencil_op_t op{ 1u, 1u, 1u, 8u };
                const gfx_depth_desc_t d{ m_depth_enable, m_depth_write != 0 ? 1u : 0u, k_gfx_compare_map[ m_depth_func ], 0u, 0xFFu, 0xFFu, op, op };
                state = m_api.create_depth( d );
                m_depths.push_back( depth_record_t{ m_depth_enable, m_depth_write, m_depth_func, state } );
            }
            m_api.set_depth( state );
        }
        if ( m_blend_dirty )
        {
            gfx_handle_t state = 0;
            for ( const blend_record_t& r : m_blends )
                if ( r.m_enable == m_blend_enable && r.m_src == m_blend_src && r.m_dst == m_blend_dst && r.m_colour == m_colour_write )
                {
                    state = r.m_state;
                    break;
                }
            if ( !state )
            {
                // 0x5ADBE0: CD3D11_BLEND_DESC's defaults for the eight targets.
                gfx_blend_desc_t d{ };
                for ( gfx_target_blend_t& t : d.m_targets )
                    t = gfx_target_blend_t{ 0u, 2u, 1u, 1u, 2u, 1u, 1u, 0xFu };
                d.m_targets[ 0 ].m_enable = m_blend_enable;
                d.m_targets[ 0 ].m_src = k_gfx_blend_map[ m_blend_src ];
                d.m_targets[ 0 ].m_dst = k_gfx_blend_map[ m_blend_dst ];
                d.m_targets[ 0 ].m_mask = m_colour_write != 0 ? 0xFu : 0u;
                state = m_api.create_blend( d );
                m_blends.push_back( blend_record_t{ m_blend_enable, m_blend_src, m_blend_dst, m_colour_write, state } );
            }
            m_api.set_blend( state );
        }
        if ( m_raster_dirty )
        {
            gfx_handle_t state = 0;
            for ( const raster_record_t& r : m_rasters )
                if ( r.m_cull == m_cull && r.m_scissor == m_scissor_enable )
                {
                    state = r.m_state;
                    break;
                }
            if ( !state )
            {
                const gfx_raster_desc_t d{ 3u, k_gfx_cull_map[ m_cull ], 1u, 0, 0.0f, 0.0f, 1u, m_scissor_enable, 0u, 0u };
                m_rasters.push_back( raster_record_t{ m_cull, m_scissor_enable, m_api.create_raster( d ) } );
            }
            m_api.set_raster( state );   // retail pushes its local: still null on the commit that created the object
        }
        if ( m_program )
        {
            upload_constants( *m_program->m_vertex );
            upload_constants( *m_program->m_pixel );
        }
    }

private:
    void upload( gfx_handle_t buffer, std::uint32_t offset, std::uint32_t capacity, const void* data, std::uint32_t bytes )
    {
        void* mapped = m_api.map( buffer, offset != 0u ? 5u : 4u );   // D3D11_MAP_WRITE_NO_OVERWRITE : D3D11_MAP_WRITE_DISCARD
        m_api.copy( static_cast< char* >( mapped ) + offset, capacity - offset, data, bytes );
        m_api.unmap( buffer );
    }

    void upload_constants( gfx_stage_t& stage )
    {
        if ( !stage.m_dirty )
            return;
        void* mapped = m_api.map( stage.m_constants, 4u );
        m_api.copy( mapped, stage.m_size, stage.m_data, stage.m_size );
        m_api.unmap( stage.m_constants );
        stage.m_dirty = false;
    }
};
