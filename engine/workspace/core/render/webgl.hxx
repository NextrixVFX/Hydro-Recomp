#pragma once

// gfx_api_t on WebGL2. A handle is an index into this object's table. The descriptions keep D3D11's numbers; this file maps them.
// Retail shaders are DXBC. Each stage is translated to GLSL ES 3.00 as it is created (render/dxbc.hxx).

#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/dxbc.hxx>
#include <workspace/utility/logger.hxx>

#include <GLES3/gl3.h>
#include <emscripten/html5.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

// GL names are shared by every gfx_webgl_t. Skipping a bind the last draw already made is safe only in this one table.
struct gl_state_t
{
    GLuint program = 0xFFFFFFFFu;
    GLuint vao = 0;
    GLuint tex[ 16 ]{};
    GLenum tex_target[ 16 ]{};
    bool tex_known[ 16 ]{};
    GLuint sampler[ 16 ]{};
    bool sampler_known[ 16 ]{};
    GLuint ubo[ 8 ]{};
    bool ubo_known[ 8 ]{};
    const void* blend_api = nullptr;
    const void* depth_api = nullptr;
    const void* raster_api = nullptr;
    gfx_handle_t blend = 0, depth = 0, raster = 0;
    GLuint fbo = 0xFFFFFFFFu;
    GLuint fbo_depth = 0xFFFFFFFFu;
    int vp_x = 0x7FFFFFFF, vp_y = 0, vp_w = 0, vp_h = 0;
    float vp_near = -1.0f, vp_far = -1.0f;
    int sc_x = 0x7FFFFFFF, sc_y = 0, sc_w = 0, sc_h = 0;
};

inline gl_state_t g_gl_state;

inline void gl_state_use( GLuint program )
{
    if ( g_gl_state.program == program )
        return;
    glUseProgram( program );
    g_gl_state.program = program;
}

inline void gl_state_texture( GLuint unit, GLenum target, GLuint name )
{
    if ( unit < 16u && g_gl_state.tex_known[ unit ] && g_gl_state.tex[ unit ] == name && g_gl_state.tex_target[ unit ] == target )
        return;
    glActiveTexture( GL_TEXTURE0 + unit );
    glBindTexture( target, name );
    if ( unit < 16u )
    {
        g_gl_state.tex_known[ unit ] = true;
        g_gl_state.tex[ unit ] = name;
        g_gl_state.tex_target[ unit ] = target;
    }
}

inline void gl_state_sampler( GLuint unit, GLuint name )
{
    if ( unit < 16u && g_gl_state.sampler_known[ unit ] && g_gl_state.sampler[ unit ] == name )
        return;
    glBindSampler( unit, name );
    if ( unit < 16u )
    {
        g_gl_state.sampler_known[ unit ] = true;
        g_gl_state.sampler[ unit ] = name;
    }
}

inline void gl_state_drop_textures( )
{
    for ( bool& known : g_gl_state.tex_known )
        known = false;
}

struct gfx_webgl_t final : gfx_api_t
{
    gfx_webgl_t( )
    {
        // The stage and the water pass are separate gfx_webgl_t objects on one GL context. D3D handles are the COM
        // pointer, so either wrapper can sample a texture the other created. An index into a private vector cannot:
        // the reflection view then resolves to nothing and the water mixes in black.
        if ( table( ).empty( ) )
        {
            table( ).push_back( {} );
            gl_object_t back{};
            back.kind = gl_object_t::back;
            back.refs = 1;
            back.height = 720;
            table( ).push_back( back );
        }
        m_back = 1;
    }

    gfx_handle_t create_buffer( const gfx_buffer_desc_t& d ) override
    {
        GLuint name = 0;
        glGenBuffers( 1, &name );
        const GLenum target = ( d.m_bind & 0x2u ) ? GL_ELEMENT_ARRAY_BUFFER : ( d.m_bind & 0x4u ) ? GL_UNIFORM_BUFFER : GL_ARRAY_BUFFER;
        glBindBuffer( target, name );
        const GLenum usage = d.m_usage == 2u ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
        glBufferData( target, d.m_size ? d.m_size : 4u, nullptr, usage );
        gl_object_t object{};
        object.kind = gl_object_t::buffer;
        object.name = name;
        object.binding = target;
        object.usage = usage;
        object.shadow.resize( d.m_size ? d.m_size : 4u );
        return add( object );
    }

    gfx_handle_t create_texture( const gfx_texture_desc_t& d ) override
    {
        GLuint name = 0;
        glGenTextures( 1, &name );
        const GLenum target = ( d.m_misc & 0x4u ) && d.m_array == 6u ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        glBindTexture( target, name );
        gl_state_drop_textures( );
        gl_object_t object{};
        object.kind = gl_object_t::texture;
        object.name = name;
        object.binding = target;
        object.width = static_cast< int >( d.m_width );
        object.height = static_cast< int >( d.m_height );
        object.format = d.m_format;
        object.levels = d.m_mips > 0u ? static_cast< int >( d.m_mips ) : 1;
        format_of( d.m_format, object.internal, object.glformat, object.type, object.compressed, object.stencil );
        glTexParameteri( target, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
        glTexParameteri( target, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
        glTexParameteri( target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        // WebGL cube maps are incomplete unless wrap R is clamp too. The default is repeat, and an
        // incomplete cube samples as black, which is what a metal shader then multiplies by.
        if ( target == GL_TEXTURE_CUBE_MAP )
            glTexParameteri( target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE );
        glTexParameteri( target, GL_TEXTURE_MAX_LEVEL, object.levels - 1 );
        if ( !object.compressed && target == GL_TEXTURE_2D && d.m_width > 0 && d.m_height > 0 )
            glTexImage2D( target, 0, static_cast< GLint >( object.internal ), object.width, object.height, 0, object.glformat, object.type, nullptr );
        return add( object );
    }

    gfx_handle_t create_view( gfx_handle_t texture ) override
    {
        gl_object_t* source = get( texture );
        if ( source == nullptr )
            return 0;
        gl_object_t object = *source;
        object.kind = gl_object_t::view;
        object.fbo = 0;
        object.refs = 1;
        return add( object );
    }

    gfx_handle_t create_target_view( gfx_handle_t texture ) override
    {
        gl_object_t* source = get( texture );
        if ( source == nullptr )
            return 0;
        // The canvas is the default framebuffer. Attaching texture 0 to an FBO draws the scene somewhere the page never shows.
        if ( source->kind == gl_object_t::back )
        {
            gl_object_t object = *source;
            object.refs = 1;
            return add( object );
        }
        GLuint fbo = 0;
        glGenFramebuffers( 1, &fbo );
        glBindFramebuffer( GL_FRAMEBUFFER, fbo );
        glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, source->binding, source->name, 0 );
        gl_object_t object = *source;
        object.kind = gl_object_t::target;
        object.fbo = fbo;
        object.refs = 1;
        return add( object );
    }

    gfx_handle_t create_depth_view( gfx_handle_t texture ) override
    {
        gl_object_t* source = get( texture );
        if ( source == nullptr )
            return 0;
        gl_object_t object = *source;
        object.kind = gl_object_t::depth;
        object.refs = 1;
        return add( object );
    }

    gfx_handle_t create_vertex_shader( const void* code, std::uint32_t size ) override { return compile_shader( GL_VERTEX_SHADER, code, size ); }
    gfx_handle_t create_pixel_shader( const void* code, std::uint32_t size ) override { return compile_shader( GL_FRAGMENT_SHADER, code, size ); }

    gfx_handle_t create_layout( const gfx_layout_element_t* elements, std::uint32_t count, const void* code, std::uint32_t size ) override
    {
        gl_object_t object{};
        object.kind = gl_object_t::layout;
        if ( elements != nullptr && count > 0 )
        {
            object.elements.assign( elements, elements + count );
            object.locations.resize( count, -1 );
            for ( std::uint32_t i = 0; i < count; ++i )
                object.locations[ i ] = dxbc_input_register( code, size, elements[ i ].m_name, elements[ i ].m_index );
        }
        return add( object );
    }

    gfx_handle_t create_sampler( const gfx_sampler_desc_t& d ) override
    {
        GLuint name = 0;
        glGenSamplers( 1, &name );
        apply_sampler( name, d );
        gl_object_t object{};
        object.kind = gl_object_t::sampler;
        object.name = name;
        auto clamp = []( std::uint32_t value ) { return value == 3u || value == 4u || value == 5u; };
        object.clamp_wrap = clamp( d.m_u ) && clamp( d.m_v ) && clamp( d.m_w );
        return add( object );
    }

    gfx_handle_t create_blend( const gfx_blend_desc_t& d ) override
    {
        gl_object_t object{};
        object.kind = gl_object_t::blend;
        object.blend_state = d;
        return add( object );
    }

    gfx_handle_t create_depth( const gfx_depth_desc_t& d ) override
    {
        gl_object_t object{};
        object.kind = gl_object_t::depth_state;
        object.depth_state_desc = d;
        return add( object );
    }

    gfx_handle_t create_raster( const gfx_raster_desc_t& d ) override
    {
        gl_object_t object{};
        object.kind = gl_object_t::raster;
        object.raster_state = d;
        return add( object );
    }

    void release( gfx_handle_t handle ) override
    {
        gl_object_t* object = get( handle );
        if ( object == nullptr || object->kind == gl_object_t::back )
            return;
        if ( --object->refs > 0 )
            return;
        destroy( *object );
        object->kind = gl_object_t::none;
    }

    void release_all( gfx_handle_t handle ) override
    {
        while ( get( handle ) != nullptr && get( handle )->kind != gl_object_t::none && get( handle )->kind != gl_object_t::back )
            release( handle );
    }

    void vs_constants( gfx_handle_t buffer ) override { bind_uniform( 0, buffer ); }
    void ps_constants( gfx_handle_t buffer ) override { bind_uniform( 1, buffer ); }
    void ps_views( std::uint32_t slot, gfx_handle_t view ) override
    {
        if ( slot < 16 )
            m_bound_view[ slot ] = view;
        const gl_object_t* object = get( view );
        gl_state_texture( slot, object ? object->binding : GL_TEXTURE_2D, object ? object->name : 0 );
    }
    void ps_samplers( std::uint32_t slot, gfx_handle_t sampler ) override
    {
        if ( slot < 16 )
            m_bound_sampler[ slot ] = sampler;
        const gl_object_t* object = get( sampler );
        gl_state_sampler( slot, object ? object->name : 0 );
    }
    void ps_shader( gfx_handle_t shader ) override { m_ps = shader; m_linked = 0; }
    void vs_shader( gfx_handle_t shader ) override { m_vs = shader; m_linked = 0; }

    void draw_indexed( std::uint32_t count, std::uint32_t start, std::int32_t ) override
    {
        if ( !prepare( ) )
            return;
        const GLenum type = m_index_format == 42u ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        const unsigned stride = type == GL_UNSIGNED_INT ? 4u : 2u;
        // D3D11 adds IASetIndexBuffer's byte offset to StartIndexLocation. Dynamic draws append
        // into one buffer and bind that offset with a start of 0; reading from 0 redraws only
        // the first draw's indices, so a later longer string keeps its first few glyphs.
        const unsigned byte_offset = m_index_offset + start * stride;
        glDrawElements( m_topology, static_cast< GLsizei >( count ), type, reinterpret_cast< const void* >( static_cast< std::uintptr_t >( byte_offset ) ) );
    }

    void draw( std::uint32_t count, std::uint32_t start ) override
    {
        if ( !prepare( ) )
            return;
        glDrawArrays( m_topology, static_cast< GLint >( start ), static_cast< GLsizei >( count ) );
    }

    void* map( gfx_handle_t resource, std::uint32_t flags ) override
    {
        gl_object_t* object = get( resource );
        if ( object == nullptr || object->kind != gl_object_t::buffer )
            return nullptr;
        object->map_discard = flags == 4u;   // D3D11_MAP_WRITE_DISCARD
        m_mapped = resource;
        return object->shadow.empty( ) ? nullptr : object->shadow.data( );
    }

    void unmap( gfx_handle_t resource ) override
    {
        gl_object_t* object = get( resource );
        if ( object == nullptr || object->kind != gl_object_t::buffer )
            return;
        glBindBuffer( object->binding, object->name );
        if ( !object->shadow.empty( ) )
        {
            const GLsizeiptr size = static_cast< GLsizeiptr >( object->shadow.size( ) );
            // A discard orphans the buffer. glBufferSubData into a buffer the previous draw still reads
            // stalls Firefox for the whole frame; Chrome renames it on its own.
            if ( object->map_discard )
                glBufferData( object->binding, size, object->shadow.data( ), object->usage );
            else
                glBufferSubData( object->binding, 0, size, object->shadow.data( ) );
        }
        object->map_discard = false;
        m_mapped = 0;
    }

    void input_layout( gfx_handle_t layout ) override { m_layout = layout; }
    void vertex_buffer( gfx_handle_t buffer, std::uint32_t stride, std::uint32_t offset ) override
    {
        m_vertices = buffer;
        m_stride = stride;
        m_vertex_offset = offset;
    }
    void index_buffer( gfx_handle_t buffer, std::uint32_t format, std::uint32_t offset ) override
    {
        m_indices = buffer;
        m_index_format = format;
        m_index_offset = offset;
    }
    void topology( std::uint32_t value ) override
    {
        switch ( value )
        {
        case 1: m_topology = GL_POINTS; break;
        case 2: m_topology = GL_LINES; break;
        case 3: m_topology = GL_LINE_STRIP; break;
        case 5: m_topology = GL_TRIANGLE_STRIP; break;
        default: m_topology = GL_TRIANGLES; break;
        }
    }

    void set_targets( std::uint32_t, gfx_handle_t view, gfx_handle_t depth ) override
    {
        m_color = view;
        m_depth = depth;
        const gl_object_t* color = get( view );
        if ( color == nullptr || color->kind == gl_object_t::back )
        {
            if ( g_gl_state.fbo != 0u )
            {
                glBindFramebuffer( GL_FRAMEBUFFER, 0 );
                g_gl_state.fbo = 0u;
                g_gl_state.fbo_depth = 0u;
            }
            m_fb_height = m_canvas_h;
            return;
        }
        const gl_object_t* d = get( depth );
        const GLuint depth_name = d ? d->name : 0u;
        if ( g_gl_state.fbo != color->fbo || g_gl_state.fbo_depth != depth_name )
        {
            glBindFramebuffer( GL_FRAMEBUFFER, color->fbo );
            if ( d )
                glFramebufferTexture2D( GL_FRAMEBUFFER, d->stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, d->name, 0 );
            g_gl_state.fbo = color->fbo;
            g_gl_state.fbo_depth = depth_name;
        }
        m_fb_height = color->height > 0 ? color->height : m_canvas_h;
    }

    void set_blend( gfx_handle_t state ) override
    {
        if ( g_gl_state.blend_api == this && g_gl_state.blend == state )
            return;
        apply_blend( get( state ) );
        g_gl_state.blend_api = this;
        g_gl_state.blend = state;
    }
    void set_depth( gfx_handle_t state ) override
    {
        if ( g_gl_state.depth_api == this && g_gl_state.depth == state )
            return;
        apply_depth( get( state ) );
        g_gl_state.depth_api = this;
        g_gl_state.depth = state;
    }
    void set_raster( gfx_handle_t state ) override
    {
        if ( g_gl_state.raster_api == this && g_gl_state.raster == state )
            return;
        apply_raster( get( state ) );
        g_gl_state.raster_api = this;
        g_gl_state.raster = state;
    }

    void set_viewport( const gfx_viewport_t& v ) override
    {
        const int y = m_fb_height - static_cast< int >( v.m_y ) - static_cast< int >( v.m_height );
        const int x = static_cast< int >( v.m_x );
        const int w = static_cast< int >( v.m_width );
        const int h = static_cast< int >( v.m_height );
        if ( g_gl_state.vp_x != x || g_gl_state.vp_y != y || g_gl_state.vp_w != w || g_gl_state.vp_h != h )
        {
            glViewport( x, y, w, h );
            g_gl_state.vp_x = x;
            g_gl_state.vp_y = y;
            g_gl_state.vp_w = w;
            g_gl_state.vp_h = h;
        }
        if ( g_gl_state.vp_near != v.m_near || g_gl_state.vp_far != v.m_far )
        {
            glDepthRangef( v.m_near, v.m_far );
            g_gl_state.vp_near = v.m_near;
            g_gl_state.vp_far = v.m_far;
        }
    }

    void set_scissor( const gfx_rect_t& r ) override
    {
        const int height = r.m_bottom - r.m_top;
        const int y = m_fb_height - r.m_bottom;
        const int w = r.m_right - r.m_left;
        if ( g_gl_state.sc_x == r.m_left && g_gl_state.sc_y == y && g_gl_state.sc_w == w && g_gl_state.sc_h == height )
            return;
        glScissor( r.m_left, y, w, height );
        g_gl_state.sc_x = r.m_left;
        g_gl_state.sc_y = y;
        g_gl_state.sc_w = w;
        g_gl_state.sc_h = height;
    }

    void update_texture( gfx_handle_t texture, std::uint32_t level, const void* data, std::uint32_t row, std::uint32_t bytes ) override
    {
        gl_object_t* object = get( texture );
        if ( object == nullptr || data == nullptr )
            return;
        // D3D UpdateSubresource is also how a vertex or index buffer is filled. Those handles share this call.
        if ( object->kind == gl_object_t::buffer )
        {
            const GLsizeiptr size = static_cast< GLsizeiptr >( bytes != 0u ? bytes : row );
            if ( size <= 0 )
                return;
            glBindBuffer( object->binding, object->name );
            glBufferSubData( object->binding, 0, size, data );
            if ( object->shadow.size( ) >= static_cast< std::size_t >( size ) )
                std::memcpy( object->shadow.data( ), data, static_cast< std::size_t >( size ) );
            return;
        }
        // Cube loads pass face * mipCount + mip. A 2D texture is face 0.
        int mip = static_cast< int >( level );
        int face = 0;
        if ( object->binding == GL_TEXTURE_CUBE_MAP && object->levels > 0 )
        {
            face = static_cast< int >( level ) / object->levels;
            mip = static_cast< int >( level ) % object->levels;
        }
        int width = object->width >> mip;
        int height = object->height >> mip;
        if ( width < 1 )
            width = 1;
        if ( height < 1 )
            height = 1;
        // Cube faces are GL_TEXTURE_CUBE_MAP_POSITIVE_X (0x8515) through NEGATIVE_Z. 0x8892 is GL_ARRAY_BUFFER.
        if ( object->binding == GL_TEXTURE_CUBE_MAP && ( face < 0 || face > 5 ) )
            return;
        const GLenum image = object->binding == GL_TEXTURE_CUBE_MAP ? static_cast< GLenum >( GL_TEXTURE_CUBE_MAP_POSITIVE_X + face ) : object->binding;
        glBindTexture( object->binding, object->name );
        gl_state_drop_textures( );
        if ( object->compressed )
        {
            glCompressedTexImage2D( image, mip, object->internal, width, height, 0, static_cast< GLsizei >( bytes ), data );
            return;
        }
        const int bpp = texel_bytes( object->format );
        glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
        if ( bpp > 0 && row >= static_cast< std::uint32_t >( bpp ) )
            glPixelStorei( GL_UNPACK_ROW_LENGTH, static_cast< GLint >( row / static_cast< std::uint32_t >( bpp ) ) );
        // Level 0 is allocated at create. Later mips (the font atlas chain) are not, and SubImage on those is dropped.
        if ( mip == 0 )
            glTexSubImage2D( image, mip, 0, 0, width, height, object->glformat, object->type, data );
        else
            glTexImage2D( image, mip, static_cast< GLint >( object->internal ), width, height, 0, object->glformat, object->type, data );
        glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
    }

    void clear_target( gfx_handle_t view, const float* rgba ) override
    {
        set_targets( 1, view, m_depth );
        // D3D's ClearRenderTargetView ignores the blend write mask. glClear does not, so a mask left
        // at zero (a depth pre-pass) clears the reflection to nothing and the water samples black.
        glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
        if ( rgba )
            glClearColor( rgba[ 0 ], rgba[ 1 ], rgba[ 2 ], rgba[ 3 ] );
        glClear( GL_COLOR_BUFFER_BIT );
        g_gl_state.blend_api = nullptr;
        g_gl_state.blend = 0;
    }

    void clear_depth( gfx_handle_t view, std::uint32_t flags, float depth, std::uint8_t stencil ) override
    {
        g_gl_state.depth_api = nullptr;
        set_targets( 1, m_color ? m_color : m_back, view );
        GLbitfield mask = 0;
        if ( flags & 1u )
        {
            glDepthMask( GL_TRUE );
            glClearDepthf( depth );
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if ( flags & 2u )
        {
            glClearStencil( stencil );
            mask |= GL_STENCIL_BUFFER_BIT;
        }
        if ( mask )
            glClear( mask );
    }

    void get_targets( gfx_handle_t& view, gfx_handle_t& depth ) override
    {
        view = m_color ? m_color : m_back;
        depth = m_depth;
    }

    void present( std::uint32_t, std::uint32_t ) override { glFlush( ); }

    gfx_handle_t back_buffer( ) override
    {
        if ( gl_object_t* back = get( m_back ) )
            back->height = m_canvas_h;
        return m_back;
    }

    void resize( std::uint32_t, std::uint32_t width, std::uint32_t height, std::uint32_t, std::uint32_t ) override
    {
        if ( width == 0 || height == 0 )
            return;
        m_canvas_w = static_cast< int >( width );
        m_canvas_h = static_cast< int >( height );
        m_fb_height = m_canvas_h;
        emscripten_set_canvas_element_size( "#canvas", m_canvas_w, m_canvas_h );
        if ( gl_object_t* back = get( m_back ) )
            back->height = m_canvas_h;
    }

    void copy( void* to, std::uint32_t room, const void* from, std::uint32_t count ) override
    {
        if ( to == nullptr || from == nullptr )
            return;
        std::memcpy( to, from, count < room ? count : room );
    }

private:
    struct gl_object_t
    {
        enum kind_t
        {
            none, buffer, texture, view, target, depth, shader, sampler, blend, depth_state, raster, layout, back
        } kind = none;
        int refs = 1;
        GLuint name = 0;
        GLuint fbo = 0;
        GLenum binding = GL_TEXTURE_2D;
        GLenum usage = GL_STATIC_DRAW;
        GLenum internal = GL_RGBA8;
        GLenum glformat = GL_RGBA;
        GLenum type = GL_UNSIGNED_BYTE;
        bool compressed = false;
        bool stencil = false;
        bool clamp_wrap = false;   // sampler: all three wraps are clamp, so it is legal on a cube map
        bool map_discard = false;   // D3D11_MAP_WRITE_DISCARD: replace the buffer instead of writing into one the GPU is still reading
        int width = 0;
        int height = 0;
        int levels = 1;
        std::uint32_t format = 0;
        std::vector< std::uint8_t > shadow;
        std::vector< gfx_layout_element_t > elements;
        std::vector< int > locations;
        std::vector< int > tex, samp;
        std::vector< char > cube;
        gfx_blend_desc_t blend_state{};
        gfx_depth_desc_t depth_state_desc{};
        gfx_raster_desc_t raster_state{};
    };

    static std::vector< gl_object_t >& table( )
    {
        static std::vector< gl_object_t > objects;
        return objects;
    }
    std::unordered_map< std::uint64_t, GLuint > m_programs;
    gfx_handle_t m_back = 0;
    gfx_handle_t m_vs = 0, m_ps = 0, m_linked_vs = 0, m_linked_ps = 0, m_layout = 0;
    gfx_handle_t m_vertices = 0, m_indices = 0, m_color = 0, m_depth = 0, m_mapped = 0;
    GLuint m_linked = 0;
    GLuint m_vao = 0;
    gfx_handle_t m_applied_layout = 0;
    gfx_handle_t m_applied_vertices = 0;
    std::uint32_t m_applied_stride = 0xFFFFFFFFu;
    std::uint32_t m_applied_vertex_offset = 0xFFFFFFFFu;
    std::uint32_t m_attrib_mask = 0;
    GLenum m_topology = GL_TRIANGLES;
    std::uint32_t m_stride = 0, m_vertex_offset = 0, m_index_offset = 0, m_index_format = 57;
    int m_canvas_w = 1280, m_canvas_h = 720, m_fb_height = 720;
    int m_shader_failures = 0;
    gfx_handle_t m_bound_view[ 16 ]{};
    gfx_handle_t m_bound_sampler[ 16 ]{};

    gfx_handle_t add( gl_object_t object )
    {
        table( ).push_back( std::move( object ) );
        return static_cast< gfx_handle_t >( table( ).size( ) - 1u );
    }

    gl_object_t* get( gfx_handle_t handle )
    {
        if ( handle == 0 || handle >= table( ).size( ) )
            return nullptr;
        gl_object_t& object = table( )[ static_cast< std::size_t >( handle ) ];
        return object.kind == gl_object_t::none ? nullptr : &object;
    }

    void destroy( gl_object_t& object )
    {
        if ( object.fbo )
            glDeleteFramebuffers( 1, &object.fbo );
        if ( object.kind == gl_object_t::buffer )
            glDeleteBuffers( 1, &object.name );
        else if ( object.kind == gl_object_t::texture )
            glDeleteTextures( 1, &object.name );
        else if ( object.kind == gl_object_t::shader )
            glDeleteShader( object.name );
        else if ( object.kind == gl_object_t::sampler )
            glDeleteSamplers( 1, &object.name );
        object.fbo = 0;
        object.name = 0;
    }

    gfx_handle_t compile_shader( GLenum stage, const void* code, std::uint32_t size )
    {
        if ( code == nullptr || size < 8 )
            return 0;
        if ( std::memcmp( code, "DXBC", 4 ) == 0 )
        {
            dxbc_stage_t translated;
            if ( !dxbc_translate( code, size, translated ) )
            {
                if ( m_shader_failures < 8 )
                {
                    ++m_shader_failures;
                    LOG_WARNING( "shader translate: %s", translated.m_error.c_str( ) );
                }
                return 0;
            }
            const GLuint shader = glCreateShader( stage );
            const char* text = translated.m_source.c_str( );
            const GLint length = static_cast< GLint >( translated.m_source.size( ) );
            glShaderSource( shader, 1, &text, &length );
            glCompileShader( shader );
            GLint ok = 0;
            glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
            if ( !ok )
            {
                char log[ 512 ]{};
                glGetShaderInfoLog( shader, static_cast< GLsizei >( sizeof( log ) ), nullptr, log );
                if ( m_shader_failures < 8 )
                {
                    ++m_shader_failures;
                    LOG_WARNING( "shader compile: %s", log );
                }
                glDeleteShader( shader );
                return 0;
            }
            gl_object_t object{};
            object.kind = gl_object_t::shader;
            object.name = shader;
            object.tex = std::move( translated.m_tex );
            object.samp = std::move( translated.m_samp );
            object.cube = std::move( translated.m_cube );
            return add( object );
        }
        const char* text = static_cast< const char* >( code );
        const bool glsl = std::memcmp( text, "#version", 8 ) == 0 || std::memcmp( text, "precision", 9 ) == 0 || std::memcmp( text, "void", 4 ) == 0;
        if ( !glsl )
            return 0;
        const GLuint shader = glCreateShader( stage );
        const GLint length = static_cast< GLint >( size );
        glShaderSource( shader, 1, &text, &length );
        glCompileShader( shader );
        GLint ok = 0;
        glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
        if ( !ok )
        {
            glDeleteShader( shader );
            return 0;
        }
        gl_object_t object{};
        object.kind = gl_object_t::shader;
        object.name = shader;
        return add( object );
    }

    void bind_uniform( std::uint32_t slot, gfx_handle_t buffer )
    {
        const gl_object_t* object = get( buffer );
        const GLuint name = object ? object->name : 0u;
        if ( slot < 8u && g_gl_state.ubo_known[ slot ] && g_gl_state.ubo[ slot ] == name )
            return;
        glBindBufferBase( GL_UNIFORM_BUFFER, slot, name );
        if ( slot < 8u )
        {
            g_gl_state.ubo_known[ slot ] = true;
            g_gl_state.ubo[ slot ] = name;
        }
    }

    void ensure_vao( )
    {
        if ( m_vao == 0 )
        {
            glGenVertexArrays( 1, &m_vao );
            m_applied_layout = 0;
            m_applied_vertices = 0;
            m_applied_stride = 0xFFFFFFFFu;
            m_applied_vertex_offset = 0xFFFFFFFFu;
            m_attrib_mask = 0;
        }
        if ( m_vao != 0 && g_gl_state.vao != m_vao )
        {
            glBindVertexArray( m_vao );
            g_gl_state.vao = m_vao;
        }
    }

    bool prepare( )
    {
        ensure_vao( );
        const gl_object_t* vs = get( m_vs );
        const gl_object_t* ps = get( m_ps );
        if ( vs == nullptr || ps == nullptr || vs->kind != gl_object_t::shader || ps->kind != gl_object_t::shader )
            return false;
        if ( m_linked == 0 || m_linked_vs != m_vs || m_linked_ps != m_ps )
        {
            const std::uint64_t key = ( static_cast< std::uint64_t >( m_vs ) << 32 ) | m_ps;
            const auto found = m_programs.find( key );
            if ( found != m_programs.end( ) )
                m_linked = found->second;
            else
            {
                const GLuint program = glCreateProgram( );
                glAttachShader( program, vs->name );
                glAttachShader( program, ps->name );
                glLinkProgram( program );
                GLint ok = 0;
                glGetProgramiv( program, GL_LINK_STATUS, &ok );
                if ( !ok )
                {
                    char log[ 512 ]{};
                    glGetProgramInfoLog( program, static_cast< GLsizei >( sizeof( log ) ), nullptr, log );
                    if ( m_shader_failures < 8 )
                    {
                        ++m_shader_failures;
                        LOG_WARNING( "shader link: %s", log );
                    }
                    glDeleteProgram( program );
                    return false;
                }
                gl_state_use( program );
                const auto block = [ program ]( const char* block_name, GLuint binding )
                {
                    const GLuint index = glGetUniformBlockIndex( program, block_name );
                    if ( index != GL_INVALID_INDEX )
                        glUniformBlockBinding( program, index, binding );
                };
                block( "VCB", 0 );
                block( "PCB", 1 );
                if ( const gl_object_t* pixel = get( m_ps ) )
                {
                    bool set[ 16 ] = {};
                    for ( std::size_t s = 0; s < pixel->tex.size( ) && s < pixel->samp.size( ); ++s )
                    {
                        const int slot = pixel->tex[ s ];
                        if ( slot < 0 || slot >= 16 || set[ slot ] )
                            continue;
                        set[ slot ] = true;
                        const std::string name = "t" + std::to_string( slot );
                        const GLint loc = glGetUniformLocation( program, name.c_str( ) );
                        if ( loc >= 0 )
                            glUniform1i( loc, slot );
                    }
                }
                m_programs.emplace( key, program );
                m_linked = program;
            }
            m_linked_vs = m_vs;
            m_linked_ps = m_ps;
        }
        gl_state_use( m_linked );
        const gl_object_t* vertices = get( m_vertices );
        const gl_object_t* layout = get( m_layout );
        const bool same_mesh = layout != nullptr && m_layout == m_applied_layout && m_vertices == m_applied_vertices
            && m_stride == m_applied_stride && m_vertex_offset == m_applied_vertex_offset;
        if ( layout && !same_mesh )
        {
            if ( vertices )
                glBindBuffer( GL_ARRAY_BUFFER, vertices->name );
            std::uint32_t need = 0;
            for ( std::size_t i = 0; i < layout->elements.size( ); ++i )
            {
                const int loc = i < layout->locations.size( ) ? layout->locations[ i ] : static_cast< int >( i );
                if ( loc < 0 || loc >= 16 )
                    continue;
                const gfx_layout_element_t& element = layout->elements[ i ];
                int components = 4;
                GLenum type = GL_FLOAT;
                GLboolean normalized = GL_FALSE;
                bool integer = false;
                switch ( element.m_format )
                {
                case 41: components = 1; break;
                case 16: components = 2; break;
                case 6: components = 3; break;
                case 34: components = 2; type = GL_HALF_FLOAT; break;
                case 10: components = 4; type = GL_HALF_FLOAT; break;
                case 28:
                case 87: components = 4; type = GL_UNSIGNED_BYTE; normalized = GL_TRUE; break;
                case 31: components = 4; type = GL_BYTE; normalized = GL_TRUE; break;
                case 37: components = 2; type = GL_SHORT; normalized = GL_TRUE; break;
                case 13: components = 4; type = GL_SHORT; normalized = GL_TRUE; break;
                case 30: components = 4; type = GL_UNSIGNED_BYTE; integer = true; break;
                case 32: components = 4; type = GL_BYTE; integer = true; break;
                default: break;
                }
                const auto offset = reinterpret_cast< const void* >( static_cast< std::uintptr_t >( m_vertex_offset + element.m_offset ) );
                const auto index = static_cast< GLuint >( loc );
                if ( ( m_attrib_mask & ( 1u << loc ) ) == 0u )
                    glEnableVertexAttribArray( index );
                need |= 1u << loc;
                if ( integer )
                    glVertexAttribIPointer( index, components, type, static_cast< GLsizei >( m_stride ), offset );
                else
                    glVertexAttribPointer( index, components, type, normalized, static_cast< GLsizei >( m_stride ), offset );
            }
            const std::uint32_t drop = m_attrib_mask & ~need;
            for ( int loc = 0; loc < 16; ++loc )
                if ( ( drop & ( 1u << loc ) ) != 0u )
                    glDisableVertexAttribArray( static_cast< GLuint >( loc ) );
            m_attrib_mask = need;
            m_applied_layout = m_layout;
            m_applied_vertices = m_vertices;
            m_applied_stride = m_stride;
            m_applied_vertex_offset = m_vertex_offset;
        }
        if ( const gl_object_t* pixel = get( m_ps ) )
        {
            for ( std::size_t s = 0; s < pixel->tex.size( ) && s < pixel->samp.size( ); ++s )
            {
                const int tex = pixel->tex[ s ];
                const int samp = pixel->samp[ s ];
                if ( tex < 0 || tex >= 16 )
                    continue;
                const gl_object_t* view = get( m_bound_view[ tex ] );
                gl_state_texture( static_cast< GLuint >( tex ), view ? view->binding : GL_TEXTURE_2D, view ? view->name : 0 );
                const gl_object_t* sampler = samp >= 0 && samp < 16 ? get( m_bound_sampler[ samp ] ) : nullptr;
                // A repeat or mirrored sampler makes a WebGL cube incomplete, so it samples as 0.
                const bool cube = view != nullptr && view->binding == GL_TEXTURE_CUBE_MAP;
                gl_state_sampler( static_cast< GLuint >( tex ), ( !cube || ( sampler && sampler->clamp_wrap ) ) && sampler ? sampler->name : 0 );
            }
        }
        if ( const gl_object_t* indices = get( m_indices ) )
            glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, indices->name );
        return true;
    }

    static void apply_sampler( GLuint name, const gfx_sampler_desc_t& d )
    {
        auto wrap = []( std::uint32_t value )
        {
            if ( value == 2u )
                return GL_MIRRORED_REPEAT;
            if ( value == 3u || value == 4u || value == 5u )
                return GL_CLAMP_TO_EDGE;
            return GL_REPEAT;
        };
        const bool min_linear = ( d.m_filter & 4u ) != 0u;
        const bool mag_linear = ( d.m_filter & 0x10u ) != 0u;
        const bool mip_linear = ( d.m_filter & 1u ) != 0u;
        // A one-level texture (MaxLOD 0: the reflection target, the fresnel ramp) sampled with a mipmap
        // filter is incomplete in WebGL and returns black. The water then multiplies the reflection by 0.
        const bool one_level = d.m_max <= 0.0f;
        GLenum min_filter = GL_NEAREST_MIPMAP_NEAREST;
        if ( one_level )
            min_filter = min_linear ? GL_LINEAR : GL_NEAREST;
        else if ( min_linear && mip_linear )
            min_filter = GL_LINEAR_MIPMAP_LINEAR;
        else if ( min_linear )
            min_filter = GL_LINEAR_MIPMAP_NEAREST;
        else if ( mip_linear )
            min_filter = GL_NEAREST_MIPMAP_LINEAR;
        glSamplerParameteri( name, GL_TEXTURE_WRAP_S, wrap( d.m_u ) );
        glSamplerParameteri( name, GL_TEXTURE_WRAP_T, wrap( d.m_v ) );
        glSamplerParameteri( name, GL_TEXTURE_WRAP_R, wrap( d.m_w ) );
        glSamplerParameteri( name, GL_TEXTURE_MIN_FILTER, min_filter );
        glSamplerParameteri( name, GL_TEXTURE_MAG_FILTER, mag_linear ? GL_LINEAR : GL_NEAREST );
        glSamplerParameterf( name, GL_TEXTURE_MIN_LOD, d.m_min );
        glSamplerParameterf( name, GL_TEXTURE_MAX_LOD, d.m_max );
    }

    static GLenum blend_factor( std::uint32_t value )
    {
        switch ( value )
        {
        case 2: return GL_ONE;
        case 3: return GL_SRC_COLOR;
        case 4: return GL_ONE_MINUS_SRC_COLOR;
        case 5: return GL_SRC_ALPHA;
        case 6: return GL_ONE_MINUS_SRC_ALPHA;
        case 7: return GL_DST_ALPHA;
        case 8: return GL_ONE_MINUS_DST_ALPHA;
        case 9: return GL_DST_COLOR;
        case 10: return GL_ONE_MINUS_DST_COLOR;
        default: return GL_ZERO;
        }
    }

    static GLenum blend_op( std::uint32_t value )
    {
        if ( value == 2u )
            return GL_FUNC_SUBTRACT;
        if ( value == 3u )
            return GL_FUNC_REVERSE_SUBTRACT;
        if ( value == 4u )
            return GL_MIN;
        if ( value == 5u )
            return GL_MAX;
        return GL_FUNC_ADD;
    }

    static GLenum compare_func( std::uint32_t value )
    {
        switch ( value )
        {
        case 2: return GL_LESS;
        case 3: return GL_EQUAL;
        case 4: return GL_LEQUAL;
        case 5: return GL_GREATER;
        case 6: return GL_NOTEQUAL;
        case 7: return GL_GEQUAL;
        case 8: return GL_ALWAYS;
        default: return GL_NEVER;
        }
    }

    static void apply_blend( const gl_object_t* object )
    {
        if ( object == nullptr || object->kind != gl_object_t::blend )
        {
            glDisable( GL_BLEND );
            return;
        }
        // D3D's blend state carries the write mask whether or not blending is enabled. Leaving the
        // mask alone when blending is off kept a depth pre-pass's mask of zero for every later draw.
        const gfx_target_blend_t& target = object->blend_state.m_targets[ 0 ];
        glColorMask( ( target.m_mask & 1u ) != 0, ( target.m_mask & 2u ) != 0, ( target.m_mask & 4u ) != 0, ( target.m_mask & 8u ) != 0 );
        if ( target.m_enable == 0u )
        {
            glDisable( GL_BLEND );
            return;
        }
        glEnable( GL_BLEND );
        glBlendFuncSeparate( blend_factor( target.m_src ), blend_factor( target.m_dst ), blend_factor( target.m_src_alpha ), blend_factor( target.m_dst_alpha ) );
        glBlendEquationSeparate( blend_op( target.m_op ), blend_op( target.m_op_alpha ) );
    }

    static void apply_depth( const gl_object_t* object )
    {
        if ( object == nullptr || object->kind != gl_object_t::depth_state || object->depth_state_desc.m_depth == 0u )
        {
            glDisable( GL_DEPTH_TEST );
            return;
        }
        glEnable( GL_DEPTH_TEST );
        glDepthFunc( compare_func( object->depth_state_desc.m_func ) );
        glDepthMask( object->depth_state_desc.m_write ? GL_TRUE : GL_FALSE );
    }

    static void apply_raster( const gl_object_t* object )
    {
        if ( object == nullptr || object->kind != gl_object_t::raster )
            return;
        if ( object->raster_state.m_cull == 1u )
            glDisable( GL_CULL_FACE );
        else
        {
            glEnable( GL_CULL_FACE );
            glCullFace( object->raster_state.m_cull == 2u ? GL_FRONT : GL_BACK );
        }
        glFrontFace( object->raster_state.m_front_ccw ? GL_CCW : GL_CW );
        if ( object->raster_state.m_scissor )
            glEnable( GL_SCISSOR_TEST );
        else
            glDisable( GL_SCISSOR_TEST );
    }

    static int texel_bytes( std::uint32_t format )
    {
        switch ( format )
        {
        case 2: return 16;
        case 6: return 12;
        case 10:
        case 11: return 8;
        case 16: return 8;
        case 28:
        case 29:
        case 87:
        case 91: return 4;
        case 41: return 4;
        case 51: return 2;   // R8G8_SNORM, the water normal
        case 61: return 1;
        default: return 4;
        }
    }

    static void format_of( std::uint32_t format, GLenum& internal, GLenum& layout, GLenum& type, bool& compressed, bool& stencil )
    {
        compressed = false;
        stencil = false;
        internal = GL_RGBA8;
        layout = GL_RGBA;
        type = GL_UNSIGNED_BYTE;
        switch ( format )
        {
        case 2: internal = GL_RGBA32F; layout = GL_RGBA; type = GL_FLOAT; break;
        case 6: internal = GL_RGB32F; layout = GL_RGB; type = GL_FLOAT; break;
        case 10: internal = GL_RGBA16F; layout = GL_RGBA; type = GL_HALF_FLOAT; break;
        case 16: internal = GL_RG32F; layout = GL_RG; type = GL_FLOAT; break;
        case 28: internal = GL_RGBA8; break;
        case 29: internal = GL_SRGB8_ALPHA8; break;
        case 40: internal = GL_DEPTH_COMPONENT32F; layout = GL_DEPTH_COMPONENT; type = GL_FLOAT; break;
        case 41: internal = GL_R32F; layout = GL_RED; type = GL_FLOAT; break;
        case 45: internal = GL_DEPTH24_STENCIL8; layout = GL_DEPTH_STENCIL; type = GL_UNSIGNED_INT_24_8; stencil = true; break;
        case 51: internal = GL_RG8_SNORM; layout = GL_RG; type = GL_BYTE; break;   // DXGI R8G8_SNORM: the wave normal. RGBA8 reads it as black and the fresnel weight stays ~0.
        case 55: internal = GL_DEPTH_COMPONENT16; layout = GL_DEPTH_COMPONENT; type = GL_UNSIGNED_SHORT; break;
        case 61: internal = GL_R8; layout = GL_RED; type = GL_UNSIGNED_BYTE; break;
        case 71: internal = 0x83F1; compressed = true; break;   // COMPRESSED_RGBA_S3TC_DXT1_EXT
        case 72: internal = 0x8C4D; compressed = true; break;
        case 74: internal = 0x83F2; compressed = true; break;
        case 77: internal = 0x83F3; compressed = true; break;   // COMPRESSED_RGBA_S3TC_DXT5_EXT
        case 78: internal = 0x8C4F; compressed = true; break;
        case 87: internal = GL_RGBA8; break;
        case 91: internal = GL_SRGB8_ALPHA8; break;
        default: break;
        }
    }
};
