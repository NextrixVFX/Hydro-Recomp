#pragma once

// The UI's records at the walk (world batch 37): what VuGfxSort's walk 0x53ADC0 runs for the commands the UI's draws submit (ui/draw.hxx),
// transliterated from the disassembly. The walk binds the material; these set its constants, the texture, and draw through the
// device's dynamic buffer. Verified against retail: checks/hudpass.hxx (case ui_record_draws).
//
// `set` is the basic shader block's entry (VuGfxUtil +4, 0x541DD0's records, 12 bytes each, filled by 0x5421C0): 0 Basic/2dXyz,
// 1 Basic/2dXyzUv. Each callback reads its program from the entry's asset (+0x58): the same program the material bound, except that
// the walk's material is the entry's own +8.
//

#include <impl/engine/engine.hxx>
#include <workspace/core/assets/texture.hxx>
#include <workspace/core/render/direct.hxx>
#include <workspace/core/ui/image.hxx>
#include <workspace/core/ui/screens.hxx>
#include <workspace/core/world/gfxsort.hxx>

#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct ui_record_device_t
{
    virtual ~ui_record_device_t( ) = default;
    virtual void colour( int set, const char* name, const std::uint8_t* rgba ) = 0;                                     // program vt+4, vt+0x30
    virtual void matrix( int set, const char* name, const float* m ) = 0;                                               // program vt+4, vt+0x1C
    virtual void texture( std::uint32_t slots, std::uint32_t handle ) = 0;                                              // g 0x75907C vt+0xB4
    virtual void draw( std::uint32_t kind, std::uint32_t primitives, const void* vertices, std::uint32_t stride ) = 0;  // vt+0xC0
};

// 0x507430: the textured quad of 0x507360's record.
inline void ui_record_texture( ui_record_device_t& d, const std::uint8_t* r )
{
    float a[ 4 ], b[ 4 ];
    std::memcpy( b, r + 0x48, 16 );
    std::memcpy( a, r + 0x58, 16 );
    std::uint32_t handle;
    std::memcpy( &handle, r, 4 );
    float m[ 16 ];
    std::memcpy( m, r + 4, 64 );
    const float x0 = a[ 0 ], y0 = a[ 1 ], x1 = a[ 2 ] + a[ 0 ], y1 = a[ 3 ] + a[ 1 ];
    const float u0 = b[ 0 ], v0 = b[ 1 ], u1 = b[ 2 ] + b[ 0 ], v1 = b[ 3 ] + b[ 1 ];
    d.colour( 1, "gColor", r + 0x44 );
    d.matrix( 1, "gTransform", m );
    d.texture( 0u, handle );
    const float v[ 20 ] = { x0, y0, 0.0f, u0, v0, x0, y1, 0.0f, u0, v1, x1, y0, 0.0f, u1, v0, x1, y1, 0.0f, u1, v1 };
    d.draw( 4u, 2u, v, 0x14u );
}

// 0x5078E0: the solid quad (ui/image.hxx's record): the first basic shader, no texture.
inline void ui_record_solid( ui_record_device_t& d, const std::uint8_t* r )
{
    float a[ 4 ], m[ 16 ];
    std::memcpy( m, r, 64 );
    std::memcpy( a, r + 0x44, 16 );
    const float x0 = a[ 0 ], y0 = a[ 1 ], x1 = a[ 2 ] + a[ 0 ], y1 = a[ 3 ] + a[ 1 ];
    d.colour( 0, "gColor", r + 0x40 );
    d.matrix( 0, "gTransform", m );
    d.texture( 0u, 0u );
    const float v[ 12 ] = { x0, y0, 0.0f, x0, y1, 0.0f, x1, y0, 0.0f, x1, y1, 0.0f };
    d.draw( 4u, 2u, v, 12u );
}

// 0x507AB0: 0x507A00's record, an outline: five vertices, a line strip.
inline void ui_record_outline( ui_record_device_t& d, const std::uint8_t* r )
{
    float a[ 4 ], m[ 16 ];
    std::memcpy( m, r, 64 );
    std::memcpy( a, r + 0x44, 16 );
    const float x0 = a[ 0 ], y0 = a[ 1 ], x1 = a[ 2 ] + a[ 0 ], y1 = a[ 3 ] + a[ 1 ];
    d.colour( 0, "gColor", r + 0x40 );
    d.matrix( 0, "gTransform", m );
    d.texture( 0u, 0u );
    const float v[ 15 ] = { x0, y0, 0.0f, x1, y0, 0.0f, x1, y1, 0.0f, x0, y1, 0.0f, x0, y0, 0.0f };
    d.draw( 2u, 4u, v, 12u );
}

// What 0x42C2A0 reaches: the shader "BoostMeter" (+0x88) by the handles the ctor 0x42AB60 took (gBoostTransform +0x98, gBoostTextureVOffset +0x9C: 0
// none; gBoostTexture +0x90, gBoostMaskTexture +0x94: -1 none).
struct ui_boost_device_t
{
    virtual ~ui_boost_device_t( ) = default;
    virtual void boost_matrix( const float* m ) = 0;                                                   // program vt+0x1C
    virtual void boost_float( float v ) = 0;                                                           // program vt+0x10
    virtual void boost_texture( bool mask, std::uint32_t handle ) = 0;                                 // g 0x75907C vt+0xB4
    virtual void boost_draw( std::uint32_t kind, std::uint32_t primitives, const float* vertices, std::uint32_t stride ) = 0;   // vt+0xC0
};

// 0x42C290 -> 0x42C2A0 (thiscall the entity = the record's +0; the record): the dial's size by the units; the texture: +0x58 set the active one, else +0x59
// clear the hold, else +0x5A set the inactive under 0.25 of the flash countdown and the flash past it, else the inactive; a strip of the mask's height.
inline void ui_record_boost( ui_boost_device_t& d, const ui_speedometer_t& s, bool has_transform, bool has_offset, bool has_texture, bool has_mask, const std::uint8_t* r )
{
    const ui_texture_t& back = s.m_metric == 1 ? s.m_back_metric : s.m_back_imperial;
    const float tex_w = static_cast< float >( back.m_width ), tex_h = static_cast< float >( back.m_height );
    const float mask_w = static_cast< float >( s.m_mask.m_width ), mask_h = static_cast< float >( s.m_mask.m_height );
    float b, rect[ 4 ];
    std::memcpy( &b, r + 0x54, 4 );
    std::memcpy( rect, r + 0x44, 16 );
    const float per_v = 1.0f / mask_h;
    const float v_top = per_v * 62.0f, v_span = per_v * 132.0f;   // 0x6F77D0, 0x6F77EC
    if ( has_transform )
    {
        float m[ 16 ];
        std::memcpy( m, r + 4, 64 );
        d.boost_matrix( m );
    }
    if ( has_offset )
        d.boost_float( s.m_v_offset );
    std::uint32_t texture = s.m_active.m_handle;
    if ( r[ 0x58 ] == 0 )
    {
        if ( r[ 0x59 ] == 0 )
            texture = s.m_hold.m_handle;
        else if ( r[ 0x5A ] == 0 )
            texture = s.m_inactive.m_handle;
        else
            texture = 0.25f > s.m_countdown ? s.m_inactive.m_handle : s.m_flash.m_handle;   // 0x6F762C, cmova
    }
    if ( has_texture )
        d.boost_texture( false, texture );
    if ( has_mask )
        d.boost_texture( true, s.m_mask.m_handle );
    const float per_h = 1.0f / tex_h, per_w = 1.0f / tex_w;
    float span = per_h * mask_h * rect[ 3 ];
    const float top = per_h * 0.0f * rect[ 3 ] + rect[ 1 ] + span * v_top;
    span = span * v_span;
    const float y1 = ( 1.0f - b ) * span + top;
    const float y0 = y1 + span * b;
    const float x0 = per_w * 9.0f * rect[ 2 ] + rect[ 0 ];     // 0x6F7788
    const float x1 = x0 + per_w * mask_w * rect[ 2 ];
    const float v1 = ( 1.0f - b ) * v_span + v_top;
    const float v0 = v_span * b + v1;
    const float v[ 16 ] = { x0, y1, 0.0f, v1, x0, y0, 0.0f, v0, x1, y1, 1.0f, v1, x1, y0, 1.0f, v0 };
    d.boost_draw( 4u, 2u, v, 0x10u );
}

// What 0x428080 reaches: the shader "MiniMap" (+0x88), its handles gMapTransform +0x90, gWaterColor +0x94, gDistMinMax +0x98; samplers gMapTexture +0x9C, gFrameTexture
// +0xA0, gMaskTexture +0xA4.
struct ui_map_device_t
{
    virtual ~ui_map_device_t( ) = default;
    virtual void map_matrix( const float* m ) = 0;                       // program vt+0x1C
    virtual void map_colour( const std::uint8_t* rgba ) = 0;             // vt+0x30
    virtual void map_range( const float* v ) = 0;                        // vt+0x20
    virtual void map_texture( int which, std::uint32_t handle ) = 0;     // g 0x75907C vt+0xB4
    virtual void map_draw( const float* vertices ) = 0;                  // vt+0xC0 ( 4, 2, v, 0x18 )
};

// 0x428060 -> 0x428080 (thiscall the entity = the record's +0; the record): the frame's quad, its first uv inside the frame's 38-pixel edge, its second the map around
// the boat at (+0x54, +0x58), turned by +0x5C.
inline void ui_record_map( ui_map_device_t& d, const ui_minimap_t& m, const std::uint8_t* r )
{
    const float half = 0.5f;
    const float soft = m.m_softness * half;
    const float range[ 2 ] = { half - soft, soft + half };
    float matrix[ 16 ], rect[ 4 ], position[ 2 ], angle;
    std::memcpy( matrix, r + 4, 64 );
    std::memcpy( rect, r + 0x44, 16 );
    std::memcpy( position, r + 0x54, 8 );
    std::memcpy( &angle, r + 0x5C, 4 );
    d.map_matrix( matrix );
    d.map_colour( m.m_water );
    d.map_range( range );
    d.map_texture( 0, m.m_map );
    d.map_texture( 1, m.m_frame );
    d.map_texture( 2, m.m_mask );
    const float x1 = rect[ 2 ] + rect[ 0 ], y1 = rect[ 3 ] + rect[ 1 ];
    const float a = 38.0f / static_cast< float >( m.m_frame_size[ 0 ] ), b = 38.0f / static_cast< float >( m.m_frame_size[ 1 ] );   // 0x6F77B8
    const float w = static_cast< float >( m.m_map_size[ 0 ] ) * m.m_scale, h = static_cast< float >( m.m_map_size[ 1 ] ) * m.m_scale;
    const float cu = ( 1.0f / w ) * ( position[ 0 ] - ( -m.m_origin[ 0 ] * w ) );
    const float cv = ( 1.0f / h ) * ( position[ 1 ] - ( -m.m_origin[ 1 ] * h ) );
    const float ru = ( 1.0f / w ) * m.m_view, rv = ( 1.0f / h ) * m.m_view;
    float c, s;
    x87_sincos( angle, &c, &s );
    const auto turn = [ & ]( float* v, float p, float q ) {
        v[ 4 ] = ( p * c - q * s ) + cu;
        v[ 5 ] = cv + ( p * s + q * c );
    };
    float v[ 24 ] = { rect[ 0 ], rect[ 1 ], a, b, 0, 0, rect[ 0 ], y1, a, 1.0f - b, 0, 0, x1, rect[ 1 ], 1.0f - a, b, 0, 0, x1, y1, 1.0f - a, 1.0f - b, 0, 0 };
    turn( v, -ru, rv );
    turn( v + 6, -ru, -rv );
    turn( v + 12, ru, rv );
    turn( v + 18, ru, -rv );
    d.map_draw( v );
}

// 0x428070 -> 0x4284F0: each boat within 0.8 of the view (a clipped entry +0x10 kept on that rim), the quad of Basic/2dXyzUv at its map place, faded past 0.8 by 20, turned
// by the view less its heading when +0x11 is set. `size` the icon texture's +0xC, +0x10.
inline void ui_record_icons( ui_record_device_t& d, const ui_minimap_t& m, const std::function< void( std::uint32_t, std::int32_t*, std::int32_t* ) >& size, const std::uint8_t* r )
{
    float record_matrix[ 16 ], rect[ 4 ], view[ 2 ], angle, icon_scale;
    std::int32_t count;
    std::memcpy( record_matrix, r + 4, 64 );
    std::memcpy( rect, r + 0x44, 16 );
    std::memcpy( view, r + 0x54, 8 );
    std::memcpy( &angle, r + 0x5C, 4 );
    std::memcpy( &icon_scale, r + 0x60, 4 );
    std::memcpy( &count, r + 0x64, 4 );
    for ( std::int32_t k = 0; k < count; ++k )
    {
        const std::uint8_t* e = r + 0x68 + 24 * k;
        float p[ 2 ], heading;
        std::uint32_t texture;
        std::memcpy( p, e, 8 );
        std::memcpy( &heading, e + 8, 4 );
        std::memcpy( &texture, e + 0xC, 4 );
        const bool clip = e[ 0x10 ] != 0, rotate = e[ 0x11 ] != 0;
        const float inv = 1.0f / m.m_view;
        const float x = inv * ( p[ 0 ] - view[ 0 ] ), y = inv * ( p[ 1 ] - view[ 1 ] );
        const float dist = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( y * y + x * x ) ) );
        const float rim = std::bit_cast< float >( 0x3F4CCCCDu );   // 0x6F7688
        if ( !( rim > dist ) && !clip )
            continue;
        d.texture( 0u, texture );
        float ax = x, ay = y;
        if ( clip && dist > rim )
        {
            const float pull = rim / dist;
            ax = pull * x;
            ay = pull * y;
        }
        float c, s;
        x87_sincos( angle, &c, &s );
        const float along = ( c * ay - s * ax ) * -1.0f, across = s * ay + c * ax;
        const float px = rect[ 2 ] * 0.5f + rect[ 0 ] + rect[ 2 ] * 0.5f * across;
        const float py = rect[ 3 ] * 0.5f + rect[ 1 ] + rect[ 3 ] * 0.5f * along;
        std::int32_t w = 0, h = 0;
        size( texture, &w, &h );
        const float sx = icon_scale * ( static_cast< float >( w ) / m.m_screen_scale[ 0 ] );
        const float sy = icon_scale * ( static_cast< float >( h ) / m.m_screen_scale[ 1 ] );
        float fade = 1.0f;
        if ( !clip )
            fade = _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( ( rim - dist ) * std::bit_cast< float >( 0x419FFFFEu ) ), _mm_set_ss( 1.0f ) ) );   // 0x6F779C
        std::uint8_t colour[ 4 ];
        std::memcpy( colour, e + 0x12, 4 );
        colour[ 3 ] = static_cast< std::uint8_t >( _mm_cvtss_si32( _mm_set_ss( fade * 255.0f ) ) );
        float mat[ 16 ], t[ 16 ];
        ui_matrix_translation( mat, px, py, 0.0f );
        ui_matrix_scale( t, sx, sy, 1.0f );
        scene_multiply( t, mat, mat );
        if ( rotate )
        {
            ui_matrix_rotation_z( t, angle - heading );
            scene_multiply( t, mat, mat );
        }
        scene_multiply( mat, mat, record_matrix );
        d.colour( 1, "gColor", colour );
        d.matrix( 1, "gTransform", mat );
        const float quad[ 20 ] = { -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f, 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.0f, 1.0f, 1.0f };
        d.draw( 4u, 2u, quad, 0x14u );
    }
}

// ---- the pass ---------------------------------------------------------------------------------------------------------------------
//
// One VuGfxSort for the UI: the frame's draws fill it, flip runs its walk 0x53ADC0 with the table's type 17 (blend on, depth off) over
// retail's programs. It needs water's render/program.hxx + resource.hxx (agent/water) and, for text, UI's core/ui/typeset.hxx (agent/ui):
// until each is on the tree the part behind it compiles out, and c_world::hud says so.
//
// What is OURS, flagged:
// - Its own sort, flipped once a frame after the composer: retail's UI commands share the frame's one sort, type 17 sorting them last.
// - A texture's sampler: clamp, linear, the first mip only (decode_texture's). Retail's options come from the asset (0x57C9B0's
//   options; the asset's load is not recreated): SUBSTITUTION.
// - The texture is decoded to RGBA8 (format 4) in place of the asset's own block-compressed format.
#if __has_include( <workspace/core/render/program.hxx> ) && __has_include( <workspace/core/render/resource.hxx> ) && HYDRO_HOST_WINDOWS
#  define HYDRO_UI_PASS 1
#  include <workspace/core/render/program.hxx>
#  include <workspace/core/render/resource.hxx>
#else
#  define HYDRO_UI_PASS 0
#endif
#if __has_include( <workspace/core/ui/typeset.hxx> )
#  define HYDRO_UI_TEXT 1
#  include <workspace/core/ui/typeset.hxx>
#else
#  define HYDRO_UI_TEXT 0
#endif

#if HYDRO_UI_PASS

struct gfx_ui_pass_t final : gfx_sort_hooks_t, ui_record_device_t, ui_boost_device_t, ui_map_device_t
{
    using read_t = std::function< bool( const std::string& asset, std::vector< std::uint8_t >& out ) >;

    // VuGfxUtil +4's records (0x541DD0): the program, its declaration, its material. Then the three font sets (0x538840).
    enum { k_flat, k_textured, k_font, k_outline, k_image, k_boost, k_map, k_sets };
    struct set_t
    {
        gfx_program_t* m_program = nullptr;
        gfx_declaration_t* m_declaration = nullptr;
        gfx_material_t* m_material = nullptr;
    };

    gfx_ui_pass_t( void* device, void* context, read_t read )
        : m_gfx( m_direct ), m_sort( *this ), m_programs( m_direct ), m_declarations( m_direct, m_programs ), m_read( std::move( read ) )
    {
        m_direct.m_device = static_cast< ID3D11Device* >( device );
        m_direct.m_context = static_cast< ID3D11DeviceContext* >( context );
        m_sort.reserve( );
        m_gfx.create_dynamic( );
        // The declarations: 0x541F00 xyz, 0x541F90 + uv at 0xC, 0x536A?? + colour at 0x14.
        gfx_vertex_element_t xyz, uv, colour;
        xyz.m_type = 2u;
        uv.m_offset = 0xCu;
        uv.m_type = 1u;
        uv.m_usage = 6u;
        colour.m_offset = 0x14u;
        colour.m_type = 7u;
        colour.m_usage = 2u;
        load( k_flat, "Basic/2dXyz", { xyz } );
        load( k_textured, "Basic/2dXyzUv", { xyz, uv } );
        load( k_font, "Font/Simple", { xyz, uv, colour } );
        load( k_outline, "Font/Outline", { xyz, uv, colour } );
        load( k_image, "Font/DrawImage", { xyz, uv, colour } );
        {
            gfx_vertex_element_t xy, boost_uv;      // the ctor 0x42AB60's two 16-byte rows
            xy.m_type = 1u;
            boost_uv.m_offset = 8u;
            boost_uv.m_type = 1u;
            boost_uv.m_usage = 6u;
            load( k_boost, "BoostMeter", { xy, boost_uv } );
            gfx_vertex_element_t map_uv;            // the ctor 0x426D70's third row
            map_uv.m_offset = 0x10u;
            map_uv.m_type = 1u;
            map_uv.m_usage = 6u;
            map_uv.m_index = 1u;
            load( k_map, "MiniMap", { xy, boost_uv, map_uv } );
        }
#if HYDRO_UI_TEXT
        // 0x538840's lookups (the handles here are 1 + the record's index in m_handles), 0x536A.. order: plain, outline, images.
        for ( int k = 0; k < 3; ++k )
        {
            ui_font_shader_t& s = m_renderer.m_sets[ k ];
            s.m_program = static_cast< std::uint32_t >( k_font + k + 1 );
            s.m_matrix = handle( k_font + k, "gTransform" );
            s.m_range = handle( k_font + k, "gDistMinMax" );
            s.m_outline = handle( k_font + k, "gOutlineMinMax" );
            s.m_colour = handle( k_font + k, "gOutlineColor" );
        }
        for ( int q = 0; q < 8192; ++q )
            for ( int v : { 0, 1, 2, 0, 2, 3 } )
                m_indices.push_back( static_cast< std::uint16_t >( q * 4 + v ) );   // 0x50B420
#endif
    }

    void load( int k, const char* name, const std::vector< gfx_vertex_element_t >& elements )
    {
        std::vector< std::uint8_t > bytes;
        if ( !m_read( std::string( "VuCompiledShaderAsset/" ) + name, bytes ) )
        {
            LOG_WARNING( "UI pass: shader %s not in the pack", name );
            return;
        }
        set_t& s = m_sets[ k ];
        s.m_program = m_programs.load( bytes.data( ) );
        s.m_declaration = m_declarations.create( elements, *s.m_program );
        s.m_material = m_sort.material_create( static_cast< std::uint32_t >( k + 1 ), static_cast< std::uint32_t >( k + 1 ), gfx_material_desc_t{ } );
        m_material_ids[ k ] = static_cast< std::uint32_t >( m_materials.size( ) + 1 );
        m_materials.push_back( s.m_material );
    }

    std::uint32_t handle( int k, const char* name )
    {
        const gfx_constant_slot_t* c = m_sets[ k ].m_program ? m_sets[ k ].m_program->constant( name ) : nullptr;
        if ( !c )
            return 0u;
        m_handles.push_back( c );
        return static_cast< std::uint32_t >( m_handles.size( ) );
    }

    // ---- gfx_sort_hooks_t --------------------------------------------------------------------------------------------------------
    std::uint32_t device( int slot, std::uint32_t a, std::uint32_t b, std::uint32_t ) override
    {
        switch ( slot )
        {
        case k_gfx_begin: m_gfx.begin( ); return 1;
        case k_gfx_program: if ( a ) m_gfx.set_program( m_sets[ a - 1 ].m_program ); break;
        case k_gfx_declaration: if ( a && m_sets[ a - 1 ].m_declaration ) m_gfx.set_declaration( m_sets[ a - 1 ].m_declaration->m_layout ); break;
        case k_gfx_depth: m_gfx.depth_enable( static_cast< std::uint8_t >( a ) ); break;
        case k_gfx_depth_write: m_gfx.depth_write( static_cast< std::uint8_t >( a ) ); break;
        case k_gfx_depth_func: m_gfx.depth_func( a ); break;
        case k_gfx_blend: m_gfx.blend_enable( static_cast< std::uint8_t >( a ) ); break;
        case k_gfx_blend_func: m_gfx.blend_func( a, b ); break;
        case k_gfx_colour_write: m_gfx.colour_write( static_cast< std::uint8_t >( a ) ); break;
        case k_gfx_cull: m_gfx.cull( a ); break;
        case k_gfx_texture: bind( a, b ); break;
        default: break;
        }
        return 0;
    }
    void program( std::uint32_t, int, std::uint32_t, std::uint32_t, const std::uint32_t* ) override { }
    std::uint32_t thread( int, std::uint32_t, std::uint32_t ) override { return 0; }
    void profile( int ) override { }
    void composer( int, std::uint32_t, std::uint32_t ) override { }
    void water_flip( ) override { }
    void light_flip( ) override { }
    std::uint32_t texture_handle( std::uint32_t ) override { return 0; }
    const gfx_material_t* material( std::uint32_t id ) override { return m_materials[ id - 1 ]; }
    const gfx_chunk_t* chunk( std::uint32_t ) override { return nullptr; }
    void draw( std::uint32_t callback, std::uint8_t* record ) override
    {
        if ( callback == k_ui_texture_callback )
            ui_record_texture( *this, record );
        else if ( callback == k_ui_solid_callback )
            ui_record_solid( *this, record );
        else if ( callback == k_ui_rect_callback )
            ui_record_outline( *this, record );
        else if ( callback == k_ui_map_callback || callback == k_ui_icons_callback )
        {
            std::uint32_t self;
            std::memcpy( &self, record, 4 );
            if ( !m_sets[ k_map ].m_program || self >= m_maps.size( ) )
            {
                ++m_unknown;
                return;
            }
            if ( callback == k_ui_map_callback )
                ui_record_map( *this, m_maps[ self ], record );
            else
                ui_record_icons( *this, m_maps[ self ], [ this ]( std::uint32_t h, std::int32_t* w, std::int32_t* t ) {
                    const auto at = m_sizes.find( h );
                    if ( at != m_sizes.end( ) )
                    {
                        *w = at->second.first;
                        *t = at->second.second;
                    }
                }, record );
        }
        else if ( callback == k_ui_boost_callback )
        {
            std::uint32_t self;
            std::memcpy( &self, record, 4 );
            const gfx_program_t* p = m_sets[ k_boost ].m_program;
            if ( !p || self >= m_boosts.size( ) )
            {
                ++m_unknown;
                return;
            }
            ui_record_boost( *this, m_boosts[ self ], p->constant( "gBoostTransform" ) != nullptr, p->constant( "gBoostTextureVOffset" ) != nullptr,
                             p->sampler( "gBoostTexture" ) != -1, p->sampler( "gBoostMaskTexture" ) != -1, record );
        }
#if HYDRO_UI_TEXT
        else if ( callback == k_ui_font_callback )
            font( record );
#endif
        else
            ++m_unknown;
    }

    // ---- ui_record_device_t ----------------------------------------------------------------------------------------------------
    void colour( int set, const char* name, const std::uint8_t* rgba ) override
    {
        if ( const gfx_program_t* p = m_sets[ set ].m_program )
            if ( const gfx_constant_slot_t* c = p->constant( name ) )
                gfx_programs_t::set_colour4( m_direct, *p, *c, rgba );
    }
    void matrix( int set, const char* name, const float* m ) override
    {
        if ( const gfx_program_t* p = m_sets[ set ].m_program )
            if ( const gfx_constant_slot_t* c = p->constant( name ) )
                gfx_programs_t::set_matrix( m_direct, *p, *c, m );
    }
    void texture( std::uint32_t slots, std::uint32_t handle ) override { bind( slots, handle ); }
    void draw( std::uint32_t kind, std::uint32_t primitives, const void* vertices, std::uint32_t stride ) override
    {
        m_gfx.draw_dynamic( kind, primitives, vertices, stride );
    }

    void bind( std::uint32_t slots, std::uint32_t handle ) { m_gfx.set_texture( slots, handle ? m_textures[ handle - 1 ] : nullptr ); }

    // ---- ui_boost_device_t -----------------------------------------------------------------------------------------------------
    void boost_matrix( const float* m ) override
    {
        const gfx_program_t& p = *m_sets[ k_boost ].m_program;
        gfx_programs_t::set_matrix( m_direct, p, *p.constant( "gBoostTransform" ), m );
    }
    void boost_float( float v ) override
    {
        const gfx_program_t& p = *m_sets[ k_boost ].m_program;
        gfx_programs_t::set_word( m_direct, p, *p.constant( "gBoostTextureVOffset" ), std::bit_cast< std::uint32_t >( v ) );
    }
    void boost_texture( bool mask, std::uint32_t handle ) override
    {
        bind( static_cast< std::uint32_t >( m_sets[ k_boost ].m_program->sampler( mask ? "gBoostMaskTexture" : "gBoostTexture" ) ), handle );
    }
    void boost_draw( std::uint32_t kind, std::uint32_t primitives, const float* vertices, std::uint32_t stride ) override { m_gfx.draw_dynamic( kind, primitives, vertices, stride ); }

    void boost_material( std::uint32_t& id, std::uint32_t& key ) const
    {
        id = m_material_ids[ k_boost ];
        key = m_sets[ k_boost ].m_material ? m_sets[ k_boost ].m_material->m_key : 0u;
    }

    // ---- ui_map_device_t -------------------------------------------------------------------------------------------------------
    void map_matrix( const float* m ) override
    {
        const gfx_program_t& p = *m_sets[ k_map ].m_program;
        if ( const gfx_constant_slot_t* c = p.constant( "gMapTransform" ) )
            gfx_programs_t::set_matrix( m_direct, p, *c, m );
    }
    void map_colour( const std::uint8_t* rgba ) override
    {
        const gfx_program_t& p = *m_sets[ k_map ].m_program;
        if ( const gfx_constant_slot_t* c = p.constant( "gWaterColor" ) )
            gfx_programs_t::set_colour4( m_direct, p, *c, rgba );
    }
    void map_range( const float* v ) override
    {
        const gfx_program_t& p = *m_sets[ k_map ].m_program;
        if ( const gfx_constant_slot_t* c = p.constant( "gDistMinMax" ) )
            gfx_programs_t::set_vec2( m_direct, p, *c, v );
    }
    void map_texture( int which, std::uint32_t handle ) override
    {
        static const char* const names[ 3 ] = { "gMapTexture", "gFrameTexture", "gMaskTexture" };
        bind( static_cast< std::uint32_t >( m_sets[ k_map ].m_program->sampler( names[ which ] ) ), handle );
    }
    void map_draw( const float* vertices ) override { m_gfx.draw_dynamic( 4u, 2u, vertices, 0x18u ); }

    void map_material( std::uint32_t& id, std::uint32_t& key ) const
    {
        id = m_material_ids[ k_map ];
        key = m_sets[ k_map ].m_material ? m_sets[ k_map ].m_material->m_key : 0u;
    }
    std::uint32_t map_add( const ui_minimap_t& m )
    {
        m_maps.push_back( m );
        return static_cast< std::uint32_t >( m_maps.size( ) - 1u );
    }

    // The entity behind a boost record (the record's +0), this frame's copy: ours for its address.
    std::uint32_t boost_add( const ui_speedometer_t& s )
    {
        m_boosts.push_back( s );
        return static_cast< std::uint32_t >( m_boosts.size( ) - 1u );
    }

    // ---- assets ---------------------------------------------------------------------------------------------------------------
    // The texture of an asset name ("UI/HUD_Speed_Needle"): 0x426A90's answer, null when the pack has none. Cached, as the asset is.
    const ui_texture_t* texture_asset( const std::string& name )
    {
        const auto at = m_by_name.find( name );
        if ( at != m_by_name.end( ) )
            return at->second.m_handle ? &at->second : nullptr;
        ui_texture_t& out = m_by_name[ name ];
        std::vector< std::uint8_t > bytes;
        image_t image;
        if ( !m_read( "VuTextureAsset/" + name, bytes ) || !decode_texture( bytes, image ) )
        {
            LOG_WARNING( "UI pass: texture %s did not load, %zu bytes, format word 0x%X (UNKNOWN: not drawn)", name.c_str( ), bytes.size( ), bytes.size( ) >= 36 ? bytes[ 32 ] | bytes[ 33 ] << 8 : 0 );
            return nullptr;
        }
        out.m_handle = upload( image.width, image.height, 4u, image.rgba.data( ), 0 );
        out.m_width = image.width;
        out.m_height = image.height;
        m_sizes[ out.m_handle ] = { image.width, image.height };
        return &out;
    }

    std::uint32_t upload( std::int32_t w, std::int32_t h, std::uint32_t format, const void* bytes, std::int32_t mip )
    {
        gfx_texture_options_t o;
        o.m_address_u = o.m_address_v = 1u;
        o.m_min = o.m_mag = 2;
        o.m_mip = mip;
        gfx_texture_object_t* t = gfx_texture_create( m_direct, w, h, format, o );
        gfx_texture_update( m_direct, *t, 0, bytes );
        m_textures.push_back( t );
        return static_cast< std::uint32_t >( m_textures.size( ) );
    }

#if HYDRO_UI_TEXT
    // FontDB's "FontAsset" -> its VuFont, loaded once; the handle the records hold (0 none).
    std::uint32_t font_asset( const std::string& name )
    {
        const auto at = m_font_names.find( name );
        if ( at != m_font_names.end( ) )
            return at->second;
        std::uint32_t& out = m_font_names[ name ];
        out = 0u;
        std::vector< std::uint8_t > bytes;
        auto f = std::make_unique< ui_font_t >( );
        if ( !m_read( "VuFontAsset/" + name, bytes ) || !ui_font_load( bytes.data( ), bytes.size( ), *f ) )
        {
            LOG_WARNING( "UI pass: font %s did not load (UNKNOWN: its text is not drawn)", name.c_str( ) );
            return 0u;
        }
        // The atlas and its chain (0x618890).
        gfx_texture_options_t o;
        o.m_address_u = o.m_address_v = 1u;
        o.m_min = o.m_mag = o.m_mip = 2;
        gfx_texture_object_t* t = gfx_texture_create( m_direct, f->m_width, f->m_height, 5u, o );
        std::vector< std::uint8_t > level = f->m_atlas;
        std::int32_t w = f->m_width, h = f->m_height;
        for ( std::uint32_t l = 0; l < t->m_levels; ++l )
        {
            gfx_texture_update( m_direct, *t, static_cast< std::int32_t >( l ), level.data( ) );
            const std::int32_t nw = w >> 1 > 1 ? w >> 1 : 1, nh = h >> 1 > 1 ? h >> 1 : 1;
            std::vector< std::uint8_t > next( static_cast< std::size_t >( nw ) * nh );
            ui_font_mip( w, h, level.data( ), next.data( ) );
            level.swap( next );
            w = nw;
            h = nh;
        }
        m_textures.push_back( t );
        f->m_texture = static_cast< std::uint32_t >( m_textures.size( ) );
        for ( std::size_t k = 0; k < f->m_images.size( ); ++k )
            if ( const ui_texture_t* image = texture_asset( f->m_images[ k ] ) )
                f->m_image_textures[ k ] = image->m_handle;
        m_fonts.push_back( std::move( f ) );
        out = static_cast< std::uint32_t >( m_fonts.size( ) );
        return out;
    }

    void font( const std::uint8_t* record )
    {
        std::uint32_t id;
        std::memcpy( &id, record, 4 );
        if ( id == 0u || id > m_fonts.size( ) )
        {
            ++m_unknown;
            return;
        }
        ui_font_ops_t ops;
        ops.m_texture = [ this ]( std::uint32_t t ) { bind( 0u, t ); };
        ops.m_scissor = [ this ]( const float rect[ 4 ] ) { m_gfx.scissor( rect ); };
        ops.m_scissor_on = [ this ]( bool on ) { m_gfx.scissor_enable( on ? 1 : 0 ); };
        ops.m_size = [ this ]( std::int32_t& a, std::int32_t& b ) { m_gfx.size( a, b ); };
        ops.m_program = [ this ]( std::uint32_t p ) { if ( p ) m_gfx.set_program( m_sets[ p - 1 ].m_program ); };
        ops.m_vec2 = [ this ]( std::uint32_t p, std::uint32_t h, const float v[ 2 ] ) {
            if ( p && h )
                gfx_programs_t::set_vec2( m_direct, *m_sets[ p - 1 ].m_program, *m_handles[ h - 1 ], v );
        };
        ops.m_colour = [ this ]( std::uint32_t p, std::uint32_t h, const std::uint8_t* v ) {
            if ( p && h )
                gfx_programs_t::set_colour4( m_direct, *m_sets[ p - 1 ].m_program, *m_handles[ h - 1 ], v );
        };
        ops.m_matrix = [ this ]( std::uint32_t p, std::uint32_t h, const float m[ 16 ] ) {
            if ( p && h )
                gfx_programs_t::set_matrix( m_direct, *m_sets[ p - 1 ].m_program, *m_handles[ h - 1 ], m );
        };
        ops.m_draw = [ this ]( int quads, const std::uint8_t* vertices ) {
            m_gfx.draw_indexed_dynamic( 3u, static_cast< std::uint32_t >( 4 * quads ), static_cast< std::uint32_t >( 2 * quads ), m_indices.data( ), vertices, 0x18u );
        };
        ui_font_submit( m_renderer, *m_fonts[ id - 1 ], record, ops );
    }
#endif

    // ---- the frame --------------------------------------------------------------------------------------------------------------
    void begin( std::int32_t width, std::int32_t height )
    {
        m_width = width;
        m_height = height;
        m_gfx.m_width = width;
        m_gfx.m_height = height;
        m_gfx.m_chain_width = width;
        m_gfx.m_chain_height = height;
    }

    // The materials of the frame's draws: their keys are renumbered by each create, so taken here.
    ui_font_draw_env_t env( ) const
    {
        ui_font_draw_env_t e;
        const auto take = [ & ]( int k, std::uint32_t& id, std::uint32_t& key ) {
            id = m_material_ids[ k ];
            key = m_sets[ k ].m_material ? m_sets[ k ].m_material->m_key : 0u;
        };
        take( k_font, e.m_material[ 0 ], e.m_material_key[ 0 ] );
        take( k_outline, e.m_material[ 1 ], e.m_material_key[ 1 ] );
        take( k_textured, e.m_texture_material, e.m_texture_material_key );
        take( k_flat, e.m_rect_material, e.m_rect_material_key );
        return e;
    }

    void end( )
    {
        gfx_viewport_t v{ 0.0f, 0.0f, static_cast< float >( m_width ), static_cast< float >( m_height ), 0.0f, 1.0f };
        m_direct.set_viewport( v );
        m_sort.flip( );
        m_boosts.clear( );
        m_maps.clear( );
    }

#  if HYDRO_HOST_WINDOWS
    gfx_direct_t m_direct;
#  endif
    gfx_device_t m_gfx;
    gfx_sort_t m_sort;
    gfx_programs_t m_programs;
    gfx_declarations_t m_declarations;
    read_t m_read;
    set_t m_sets[ k_sets ];
    std::uint32_t m_material_ids[ k_sets ] = { };
    std::vector< gfx_material_t* > m_materials;
    std::vector< const gfx_constant_slot_t* > m_handles;
    std::vector< gfx_texture_object_t* > m_textures;
    std::map< std::string, ui_texture_t > m_by_name;
#if HYDRO_UI_TEXT
    std::map< std::string, std::uint32_t > m_font_names;
    std::vector< std::unique_ptr< ui_font_t > > m_fonts;
    ui_font_renderer_t m_renderer;
    std::vector< std::uint16_t > m_indices;
#endif
    ui_impl_t m_impl;
    std::int32_t m_width = 0, m_height = 0;
    std::vector< ui_speedometer_t > m_boosts;
    std::vector< ui_minimap_t > m_maps;
    std::map< std::uint32_t, std::pair< std::int32_t, std::int32_t > > m_sizes;
    int m_unknown = 0;   // commands whose callback is not recreated
};

#endif
