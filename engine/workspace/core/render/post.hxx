#pragma once

// VuPostProcess (g 0x75902C +0x18; create 0x57BF10): the full-screen programs PostProcess/Copy, GaussBlur5x5, RadialBlur and Effect, their handles and declaration, copy 0x57C240,
// radial blur 0x57C2D0 and the quad 0x57C370, recreated 1:1 (water batch 35). The shaders are the pack's DXBC: nothing here is a written shader.
// GaussBlur5x5 and Effect are created and their handles resolved as retail does; their draws sit under 0x514D70, not read past its calls.
// Verified against retail: hydro checks/device.hxx verify_gfx_post (case gfx_post).

#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/program.hxx>
#include <workspace/core/render/resource.hxx>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct gfx_post_t
{
    gfx_declaration_t* m_declaration = nullptr;          // +0
    const gfx_program_t* m_copy = nullptr;               // +4 [+0x58]
    const gfx_program_t* m_gauss = nullptr;              // +8 [+0x58]
    const gfx_program_t* m_radial = nullptr;             // +0xC [+0x58]
    const gfx_program_t* m_effect = nullptr;             // +0x10 [+0x58]
    const gfx_constant_slot_t* m_copy_texel = nullptr;   // +0x14 gTexelSize
    const gfx_constant_slot_t* m_gauss_texel = nullptr;  // +0x18 gTexelSize
    const gfx_constant_slot_t* m_gauss_offsets = nullptr;// +0x1C gAvgSampleOffsets
    const gfx_constant_slot_t* m_gauss_weights = nullptr;// +0x20 gAvgSampleWeights
    const gfx_constant_slot_t* m_radial_texel = nullptr; // +0x24 gTexelSize
    const gfx_constant_slot_t* m_radial_amount = nullptr;// +0x28 gRadialBlurAmount
    const gfx_constant_slot_t* m_effect_texel = nullptr; // +0x2C gTexelSize
    const gfx_constant_slot_t* m_effect_contrast = nullptr;   // +0x30 gContrast
    const gfx_constant_slot_t* m_effect_tint = nullptr;  // +0x34 gTint

    // 0x57BF10. `shader` = 0x428F60's asset, +0x58. Declaration: POSITION (type 1, float2) at 0, TEXCOORD (usage 6, type 1) at +8.
    void create( gfx_declarations_t& declarations, const std::function< const gfx_program_t*( const std::string& ) >& shader )
    {
        m_copy = shader( "PostProcess/Copy" );
        m_copy_texel = m_copy->constant( "gTexelSize" );
        m_gauss = shader( "PostProcess/GaussBlur5x5" );
        m_gauss_texel = m_gauss->constant( "gTexelSize" );
        m_gauss_offsets = m_gauss->constant( "gAvgSampleOffsets" );
        m_gauss_weights = m_gauss->constant( "gAvgSampleWeights" );
        m_radial = shader( "PostProcess/RadialBlur" );
        m_radial_texel = m_radial->constant( "gTexelSize" );
        m_radial_amount = m_radial->constant( "gRadialBlurAmount" );
        m_effect = shader( "PostProcess/Effect" );
        m_effect_texel = m_effect->constant( "gTexelSize" );
        m_effect_contrast = m_effect->constant( "gContrast" );
        m_effect_tint = m_effect->constant( "gTint" );
        std::vector< gfx_vertex_element_t > elements( 2 );
        elements[ 0 ].m_type = 1u;
        elements[ 1 ].m_offset = 8u;
        elements[ 1 ].m_type = 1u;
        elements[ 1 ].m_usage = 6u;
        m_declaration = declarations.create( elements, *m_copy );
    }

    // 0x57C240 (the texture; its width +0xC and height +0x10 give gTexelSize).
    void copy( gfx_device_t& d, const gfx_texture_object_t& texture ) const
    {
        d.set_program( m_copy );
        if ( m_copy_texel )
            texel( d, *m_copy, *m_copy_texel, texture );
        d.set_texture( 0u, &texture );
        quad( d );
    }

    // 0x57C2D0 (xmm2 the amount; the texture).
    void radial( gfx_device_t& d, const gfx_texture_object_t& texture, float amount ) const
    {
        d.set_program( m_radial );
        if ( m_radial_texel )
            texel( d, *m_radial, *m_radial_texel, texture );
        std::uint32_t bits;
        std::memcpy( &bits, &amount, 4 );
        gfx_programs_t::set_word( d.m_api, *m_radial, *m_radial_amount, bits );
        d.set_texture( 0u, &texture );
        quad( d );
    }

    // 0x5BC6F0 / 0x5BC7D0, the sort's callbacks (a null target is the chain). Their last step is the target's slot 1, empty.
    void copy_to( gfx_device_t& d, const gfx_texture_object_t& texture, const gfx_target_t* target ) const
    {
        d.set_target( target );
        copy( d, texture );
    }
    void radial_to( gfx_device_t& d, const gfx_texture_object_t& texture, const gfx_target_t* target, float amount ) const
    {
        d.set_target( target );
        radial( d, texture, amount );
    }

    // 0x57C370: no culling, no depth; the strip's four corners of { x, y, u, v }; everything put back. vt+0x60 / +0x64 are retail's empty 0x401560.
    void quad( gfx_device_t& d ) const
    {
        static const float corners[ 16 ] = { -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f };
        d.set_declaration( m_declaration->m_layout );
        d.cull( 0u );
        d.depth_enable( 0u );
        d.depth_write( 0u );
        d.draw_dynamic( 4u, 2u, corners, 0x10u );
        d.depth_enable( 1u );
        d.depth_write( 1u );
        d.cull( 1u );
    }

private:
    static void texel( gfx_device_t& d, const gfx_program_t& p, const gfx_constant_slot_t& c, const gfx_texture_object_t& t )
    {
        const float v[ 2 ] = { 1.0f / static_cast< float >( t.m_width ), 1.0f / static_cast< float >( t.m_height ) };
        gfx_programs_t::set_vec2( d.m_api, p, c, v );
    }
};
