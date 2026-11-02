#pragma once

// The model records' draws (water batch 34): what the sort's walk calls for the commands world/instance.hxx queues. VuStaticModelInstance
// 0x521220 (colour pass), 0x5212B0 (depth pre-pass), 0x521380 (colour on a pre-passed depth), 0x521430 / 0x521490 (depth and shadow);
// VuAnimatedModelInstance 0x533C90 0x533D40 0x533E30 0x533F00 0x533F80; 0x593E70, the program's four point lights. They run on the
// device (render/gfx.hxx) and the program setters (render/program.hxx), the same calls in the same order.
// Verified against retail: hydro checks/device.hxx verify_gfx_model_draws (case gfx_model_draws): retail's callbacks on retail's
// VuD3d11Gfx and VuD3d11ShaderProgram over the fake D3D11 device.

#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/program.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>

// VuGfxSceneMaterial's shader block as these read it (part +0xC): +0x14 the program, then its resolved constants.
struct gfx_model_shader_t
{
    std::uint8_t m_material = 0;                        // +0x19 the flat shaders' index
    const gfx_program_t* m_program = nullptr;           // +0x14
    const gfx_constant_slot_t* m_matrix = nullptr;      // +0x30
    const gfx_constant_slot_t* m_palette = nullptr;     // +0x34
    const gfx_constant_slot_t* m_colour = nullptr;      // +0x38
    const gfx_constant_slot_t* m_float = nullptr;       // +0x3C
    const gfx_constant_slot_t* m_light_dirs = nullptr;  // +0x44 gLightDirs-class 4 vectors
    const gfx_constant_slot_t* m_light_cols = nullptr;  // +0x48
};

// The part fields the draw reads: the third and fourth arguments of 0x5AD0C0.
struct gfx_model_part_t
{
    const gfx_model_shader_t* m_shader = nullptr;   // +0xC
    float m_aabb[ 8 ] = { };                        // +0x30
    std::uint32_t m_start = 0;                      // +0x28
    std::uint32_t m_triangles = 0;                  // +0x2C
};

// The 0x50-byte record (and the skinned one's +0x4C / +0x50 / +0x54).
struct gfx_model_record_t
{
    float m_matrix[ 16 ] = { };             // +0
    std::uint8_t m_colour[ 4 ] = { };       // +0x40
    std::uint32_t m_word = 0;               // +0x44
    const gfx_model_part_t* m_part = nullptr;   // +0x48
    std::uint8_t m_lit = 0;                 // +0x4C, skinned +0x54
    const float* m_palette = nullptr;       // skinned +0x50
    std::uint32_t m_palette_count = 0;      // skinned +0x4C
};

// 0x593E70 (lit, matrix, aabb): the lights' directions and colours for this draw, four vectors each, zero past the ones that reach.
// `gather` = 0x562C40 on the light manager, filling the 0x144-byte block: five 16-byte fields by four lights, the count at +0x140.
inline void gfx_model_lights( gfx_api_t& api, const gfx_model_shader_t& s, bool lit, const float* m, const float* aabb, const std::function< void( float* block ) >& gather )
{
    if ( s.m_light_dirs == nullptr || s.m_light_cols == nullptr )
        return;
    float dirs[ 16 ] = { }, cols[ 16 ] = { };
    if ( lit )
    {
        float block[ 81 ];
        gather( block );
        std::int32_t count;
        std::memcpy( &count, block + 80, 4 );
        if ( count > 0 )
        {
            const float half = 0.5f;   // 0x6F765C
            const float cx = ( aabb[ 4 ] + aabb[ 0 ] ) * half, cy = ( aabb[ 5 ] + aabb[ 1 ] ) * half, cz = ( aabb[ 6 ] + aabb[ 2 ] ) * half;
            const float px = ( ( m[ 4 ] * cy + m[ 0 ] * cx ) + m[ 8 ] * cz ) + m[ 12 ];
            const float py = ( ( m[ 5 ] * cy + m[ 1 ] * cx ) + m[ 9 ] * cz ) + m[ 13 ];
            const float pz = ( ( m[ 6 ] * cy + m[ 2 ] * cx ) + m[ 10 ] * cz ) + m[ 14 ];
            int n = 0;
            for ( std::int32_t i = 0; i < count; ++i )
            {
                const float* position = block + 4 * i;
                const float* direction = block + 16 + 4 * i;
                const float* colour = block + 32 + 4 * i;
                const float* range = block + 64 + 4 * i;
                const float dy = py - position[ 1 ], dx = px - position[ 0 ], dz = pz - position[ 2 ];
                const float dist = std::sqrt( ( dy * dy + dx * dx ) + dz * dz );
                const float far_ = range[ 1 ];
                if ( !( far_ > dist ) )
                    continue;
                const float inv = 1.0f / dist;
                const float ux = inv * dx, uy = inv * dy, uz = inv * dz;
                const float dot = ( direction[ 0 ] * ux + direction[ 1 ] * uy ) + direction[ 2 ] * uz;
                const float outer = range[ 3 ], inner = range[ 2 ], near_ = range[ 0 ];
                if ( !( dot > outer ) )
                    continue;
                float a = 1.0f;
                if ( dist > near_ )
                    a = ( far_ - dist ) / ( far_ - near_ );
                if ( inner > dot )
                    a = ( ( outer - dot ) / ( outer - inner ) ) * a;
                dirs[ 4 * n + 0 ] = ux;
                dirs[ 4 * n + 1 ] = uy;
                dirs[ 4 * n + 2 ] = uz;
                dirs[ 4 * n + 3 ] = 0.0f;
                for ( int k = 0; k < 4; ++k )
                    cols[ 4 * n + k ] = colour[ k ] * a;
                ++n;
            }
        }
    }
    gfx_programs_t::set_vectors( api, *s.m_program, *s.m_light_dirs, dirs, 4u );
    gfx_programs_t::set_vectors( api, *s.m_program, *s.m_light_cols, cols, 4u );
}

enum gfx_model_pass_t
{
    k_gfx_pass_colour,      // 0x521220 0x533C90
    k_gfx_pass_prepass,     // 0x5212B0 0x533D40: no colour, depth written, restored after to colour on, depth off
    k_gfx_pass_equal,       // 0x521380 0x533E30: depth function 2 for the draw, 1 after
};

// The shared body: matrix, colour, word, (skinned: palette,) lights, then the pass's draw.
inline void gfx_model_draw( gfx_device_t& d, const gfx_model_record_t& r, gfx_model_pass_t pass, bool skinned, const std::function< void( float* block ) >& gather )
{
    const gfx_model_part_t& part = *r.m_part;
    const gfx_model_shader_t& s = *part.m_shader;
    gfx_programs_t::set_matrix( d.m_api, *s.m_program, *s.m_matrix, r.m_matrix );
    if ( s.m_colour )
        gfx_programs_t::set_colour4( d.m_api, *s.m_program, *s.m_colour, r.m_colour );
    if ( s.m_float )
        gfx_programs_t::set_word( d.m_api, *s.m_program, *s.m_float, r.m_word );
    if ( skinned && s.m_palette )
        gfx_programs_t::set_matrices( d.m_api, *s.m_program, *s.m_palette, r.m_palette, r.m_palette_count );
    gfx_model_lights( d.m_api, s, r.m_lit != 0, r.m_matrix, part.m_aabb, gather );
    if ( pass == k_gfx_pass_prepass )
    {
        d.colour_write( 0 );
        d.depth_write( 1 );
    }
    else if ( pass == k_gfx_pass_equal )
        d.depth_func( 2u );
    d.draw_indexed( 3u, part.m_start, part.m_triangles );
    if ( pass == k_gfx_pass_prepass )
    {
        d.colour_write( 1 );
        d.depth_write( 0 );
    }
    else if ( pass == k_gfx_pass_equal )
        d.depth_func( 1u );
}

// VuGfxUtil's 12-byte shadow / depth entry (g 0x75902C +8 for 0x521430 / 0x533F00, +0xC for 0x521490 / 0x533F80, then [0], indexed by the
// shader block's +0x19; animated +2): the shader's +0x58 program, its matrix constant, its palette constant.
struct gfx_flat_shader_t
{
    const gfx_program_t* m_program = nullptr;
    const gfx_constant_slot_t* m_matrix = nullptr;
    const gfx_constant_slot_t* m_palette = nullptr;
};

// 0x521430 / 0x521490 (record: the matrix, part at +0x40): the flat shader's matrix, the draw.
inline void gfx_flat_draw( gfx_device_t& d, const gfx_flat_shader_t* entries, const float* matrix, const gfx_model_part_t& part )
{
    const gfx_flat_shader_t& s = entries[ part.m_shader->m_material ];
    gfx_programs_t::set_matrix( d.m_api, *s.m_program, *s.m_matrix, matrix );
    d.draw_indexed( 3u, part.m_start, part.m_triangles );
}

// 0x533F00 / 0x533F80 (record +0x44 the palette count, +0x48 the palette).
inline void gfx_flat_skinned_draw( gfx_device_t& d, const gfx_flat_shader_t* entries, const float* matrix, const float* palette, std::uint32_t count, const gfx_model_part_t& part )
{
    const gfx_flat_shader_t& s = entries[ part.m_shader->m_material + 2u ];
    gfx_programs_t::set_matrix( d.m_api, *s.m_program, *s.m_matrix, matrix );
    gfx_programs_t::set_matrices( d.m_api, *s.m_program, *s.m_palette, palette, count );
    d.draw_indexed( 3u, part.m_start, part.m_triangles );
}
