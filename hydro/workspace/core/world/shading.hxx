#pragma once

// VuWaterShader (vtable 0x750738, 0xEC bytes; water batch 35), recreated 1:1: the variant choice and handles of the ctor 0x599B40, the bind
// 0x59A4A0, the Fresnel table 0x59A860 and the patch draw 0x595790. Retail's Water/... programs are the DXBC of the VuCompiledShaderAsset
// entries (56 in Base.apf): nothing here is a written shader.
//
// A SUBSTITUTION: 0x59A860's acosf / asinf / tanf are the x64 CRT's and its fsin goes through double; the table's bytes are compared to retail.
// HOOKS: water_shader_assets_t stands for the asset manager g 0x759004, the composer g 0x7590BC and the gfx sort's material 0x53A440.
// Verified against retail: hydro checks/device.hxx verify_water_material (case water_material).

#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/program.hxx>
#include <workspace/core/render/resource.hxx>

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// VuWaterShaderDesc as 0x59A1E0 copies it. The default values are 0x5DA350's; the colours are VuColor bytes in memory order.
struct water_shader_desc_t
{
    std::string m_name;                         // +0
    std::uint8_t m_own_colours = 0;             // +0x18
    std::uint8_t m_ambient[ 4 ] = { };          // +0x19
    std::uint8_t m_diffuse[ 4 ] = { };          // +0x1D
    std::uint8_t m_foam_ambient[ 4 ] = { };     // +0x21
    std::uint8_t m_foam_diffuse[ 4 ] = { };     // +0x25
    std::uint8_t m_fog = 0;                     // +0x29
    float m_foam_size = 0.0f;                   // +0x2C
    std::string m_foam_texture;                 // +0x30
    std::uint8_t m_reflect = 0;                 // +0x48
    std::uint8_t m_complex = 0;                 // +0x49
    std::string m_cube;                         // +0x4C
    std::uint8_t m_decal_ambient[ 4 ] = { };    // +0x64
    std::uint8_t m_decal_diffuse[ 4 ] = { };    // +0x68
    std::string m_decal_texture;                // +0x6C
};

// What the bind reads from outside the material.
struct water_shader_statics_t   // 0x759708 .. 0x759720
{
    std::uint8_t m_ambient[ 4 ] = { };
    std::uint8_t m_diffuse[ 4 ] = { };
    std::uint8_t m_foam_ambient[ 4 ] = { };
    std::uint8_t m_foam_diffuse[ 4 ] = { };
    float m_foam_size = 0.0f;
    std::uint8_t m_decal_ambient[ 4 ] = { };
    std::uint8_t m_decal_diffuse[ 4 ] = { };
};

struct water_shader_assets_t
{
    virtual ~water_shader_assets_t( ) = default;
    virtual const gfx_program_t* shader( const std::string& name ) = 0;                 // 0x428F60 VuCompiledShaderAsset, +0x58 the program
    virtual bool texture_exists( const std::string& name ) = 0;                         // 0x4269D0
    virtual const gfx_texture_t* texture( const std::string& name ) = 0;                // 0x426A90, +0x58 the texture
    virtual bool cube_exists( const std::string& name ) = 0;                            // 0x5941E0
    virtual const gfx_texture_t* cube( const std::string& name ) = 0;                   // 0x541D20, +0x58
    virtual std::uint32_t material( const gfx_program_t& program, gfx_declaration_t& declaration ) = 0;   // 0x53A440, holds its own reference
};

// g 0x7590BC [+4] as the bind reads it: slot 0 the texture, slot 2 the offset, slot 3 the scale.
struct water_reflection_source_t
{
    virtual ~water_reflection_source_t( ) = default;
    virtual const gfx_texture_t* texture( ) = 0;
    virtual void offset( float* out ) = 0;
    virtual void scale( float* out ) = 0;
};

// 0x59A860 (the texture's own creation aside): the 128 bytes. `factor` is the dev float WaterFresnelFactor, 0.9 (0x6F76A4) when the
// config answers nothing, which it does in the shipped game.
inline void water_fresnel_bytes( std::uint8_t* out, float factor )
{
    const auto fsin = []( float v ) { return static_cast< float >( std::sin( static_cast< double >( v ) ) ); };
    for ( int i = 0; i < 128; ++i )
    {
        const float cosine = static_cast< float >( i ) * 0.0078740157f;   // 0x6F75C8
        const float a = std::acos( cosine );
        const float least = 1.17549435e-38f;                              // 0x6F7574, through maxss
        const float incident = a > least ? a : least;
        const float refracted = std::asin( fsin( incident ) * 0.75187969f );   // 0x6F7680
        const float sum = refracted + incident, difference = refracted - incident;
        const float sine_ratio = fsin( difference ) / fsin( sum );
        const float tangent_ratio = std::tan( difference ) / std::tan( sum );
        const float v = ( tangent_ratio * tangent_ratio + sine_ratio * sine_ratio ) * 0.5f * factor * 255.0f;
        out[ i ] = static_cast< std::uint8_t >( static_cast< std::int32_t >( std::lrintf( v ) ) );
    }
}

struct water_shader_t
{
    water_shader_desc_t m_desc;                          // +0xC
    const gfx_program_t* m_program = nullptr;            // +0x90 [+0x58]
    const gfx_texture_t* m_foam = nullptr;               // +0x94 [+0x58]
    const gfx_texture_t* m_cube = nullptr;               // +0x98 [+0x58]
    const gfx_texture_t* m_decal = nullptr;              // +0x9C [+0x58]
    std::uint32_t m_material = 0;                        // +0xA0
    gfx_texture_object_t* m_fresnel = nullptr;           // +0xA4
    const gfx_constant_slot_t* m_ambient = nullptr;      // +0xA8 gAmbientColor
    const gfx_constant_slot_t* m_diffuse = nullptr;      // +0xAC gDiffuseColor
    const gfx_constant_slot_t* m_foam_ambient = nullptr; // +0xB0 gFoamAmbientColor
    const gfx_constant_slot_t* m_foam_diffuse = nullptr; // +0xB4 gFoamDiffuseColor
    const gfx_constant_slot_t* m_foam_size = nullptr;    // +0xB8 gFoamTextureSize
    const gfx_constant_slot_t* m_foam_center = nullptr;  // +0xBC gFoamCenter
    const gfx_constant_slot_t* m_water_z = nullptr;      // +0xC0 gWaterZ
    const gfx_constant_slot_t* m_offset = nullptr;       // +0xC4 gReflectionMapOffset
    const gfx_constant_slot_t* m_scale = nullptr;        // +0xC8 gReflectionMapScale
    const gfx_constant_slot_t* m_to_normal = nullptr;    // +0xCC gFoamToNormalTextureScale
    const gfx_constant_slot_t* m_decal_ambient = nullptr;// +0xD0 gDecalAmbientColor
    const gfx_constant_slot_t* m_decal_diffuse = nullptr;// +0xD4 gDecalDiffuseColor
    std::int32_t m_fresnel_slot = -1;                    // +0xD8 gFresnelTexture
    std::int32_t m_foam_slot = -1;                       // +0xDC gFoamTexture
    std::int32_t m_reflection_slot = -1;                 // +0xE0 ReflectionTexture
    std::int32_t m_decal_slot = -1;                      // +0xE4 DecalTexture
    std::int32_t m_normal_slot = -1;                     // +0xE8 NormalTexture

    // The name 0x599B40 asks for: the desc's own when it has one.
    static std::string variant( const water_shader_desc_t& d )
    {
        if ( !d.m_name.empty( ) )
            return d.m_name;
        std::string name = d.m_complex ? "Water/Complex/" : "Water/Simple/";
        if ( d.m_fog )
            name += "Fog";
        if ( d.m_reflect )
            name += "Reflect";
        if ( !d.m_decal_texture.empty( ) )
            name += "Decal";
        if ( name.back( ) == '/' )
            name += "Basic";
        return name;
    }

    // 0x599B40.
    void create( gfx_api_t& api, gfx_declarations_t& declarations, water_shader_assets_t& assets, const water_shader_desc_t& d, float fresnel_factor )
    {
        m_desc = d;
        m_program = assets.shader( variant( d ) );
        std::vector< gfx_vertex_element_t > elements( 3 );
        elements[ 0 ].m_type = 2u;
        elements[ 1 ].m_offset = 0xCu;
        elements[ 1 ].m_type = 1u;
        elements[ 1 ].m_usage = 1u;
        elements[ 2 ].m_offset = 0x14u;
        elements[ 2 ].m_type = 2u;
        elements[ 2 ].m_usage = 2u;
        gfx_declaration_t* declaration = declarations.create( elements, *m_program );
        m_material = assets.material( *m_program, *declaration );
        declarations.release( declaration );
        gfx_texture_options_t options;
        options.m_address_u = options.m_address_v = 1u;
        options.m_min = options.m_mag = 2;
        m_fresnel = gfx_texture_create( api, 0x80, 1, 6u, options );
        std::uint8_t bytes[ 128 ];
        water_fresnel_bytes( bytes, fresnel_factor );
        gfx_texture_update( api, *m_fresnel, 0, bytes );
        if ( assets.texture_exists( d.m_foam_texture ) )
            m_foam = assets.texture( d.m_foam_texture );
        if ( d.m_reflect == 0u && assets.cube_exists( d.m_cube ) )
            m_cube = assets.cube( d.m_cube );
        if ( assets.texture_exists( d.m_decal_texture ) )
            m_decal = assets.texture( d.m_decal_texture );
        const gfx_program_t& p = *m_program;
        m_ambient = p.constant( "gAmbientColor" );
        m_diffuse = p.constant( "gDiffuseColor" );
        m_foam_ambient = p.constant( "gFoamAmbientColor" );
        m_foam_diffuse = p.constant( "gFoamDiffuseColor" );
        m_foam_size = p.constant( "gFoamTextureSize" );
        m_foam_center = p.constant( "gFoamCenter" );
        m_water_z = p.constant( "gWaterZ" );
        m_offset = p.constant( "gReflectionMapOffset" );
        m_scale = p.constant( "gReflectionMapScale" );
        m_to_normal = p.constant( "gFoamToNormalTextureScale" );
        m_decal_ambient = p.constant( "gDecalAmbientColor" );
        m_decal_diffuse = p.constant( "gDecalDiffuseColor" );
        m_fresnel_slot = p.sampler( "gFresnelTexture" );
        m_foam_slot = p.sampler( "gFoamTexture" );
        m_reflection_slot = p.sampler( "ReflectionTexture" );
        m_decal_slot = p.sampler( "DecalTexture" );
        m_normal_slot = p.sampler( "NormalTexture" );
    }

    // 0x59A4A0 (the surface matrix R * T, of which it reads +0x30 / +0x34: T's x and y, `x` `y` here; an unread word; xmm1 the surface's z).
    // `normal` = g 0x759058 [+0x1C][0]'s texture [+0x4C], `normal_size` its +0x40.
    void bind( gfx_device_t& d, const water_shader_statics_t& statics, float position_x, float position_y, float water_z, const gfx_texture_t* normal, float normal_size, water_reflection_source_t* reflection ) const
    {
        gfx_api_t& api = d.m_api;
        const gfx_program_t& p = *m_program;
        const bool own = m_desc.m_own_colours != 0u;
        if ( m_ambient )
            gfx_programs_t::set_colour4( api, p, *m_ambient, own ? m_desc.m_ambient : statics.m_ambient );
        if ( m_diffuse )
            gfx_programs_t::set_colour4( api, p, *m_diffuse, own ? m_desc.m_diffuse : statics.m_diffuse );
        if ( m_foam_ambient )
            gfx_programs_t::set_colour3( api, p, *m_foam_ambient, own ? m_desc.m_foam_ambient : statics.m_foam_ambient );
        if ( m_foam_diffuse )
            gfx_programs_t::set_colour3( api, p, *m_foam_diffuse, own ? m_desc.m_foam_diffuse : statics.m_foam_diffuse );
        const float foam_size = own ? m_desc.m_foam_size : statics.m_foam_size;
        if ( m_foam_size )
            gfx_programs_t::set_word( api, p, *m_foam_size, bits( foam_size ) );
        if ( m_decal_ambient )
            gfx_programs_t::set_colour3( api, p, *m_decal_ambient, own ? m_desc.m_decal_ambient : statics.m_decal_ambient );
        if ( m_decal_diffuse )
            gfx_programs_t::set_colour3( api, p, *m_decal_diffuse, own ? m_desc.m_decal_diffuse : statics.m_decal_diffuse );
        if ( m_fresnel_slot >= 0 )
            d.set_texture( static_cast< std::uint32_t >( m_fresnel_slot ), m_fresnel );
        if ( m_foam_slot >= 0 )
            d.set_texture( static_cast< std::uint32_t >( m_foam_slot ), m_foam );
        if ( m_decal_slot >= 0 )
            d.set_texture( static_cast< std::uint32_t >( m_decal_slot ), m_decal );
        if ( m_normal_slot >= 0 )
            d.set_texture( static_cast< std::uint32_t >( m_normal_slot ), normal );
        if ( m_to_normal )
            gfx_programs_t::set_word( api, p, *m_to_normal, bits( 1.0f / normal_size ) );
        if ( m_foam_center )
        {
            const float inverse = 1.0f / foam_size;
            const float x = inverse * position_x, y = inverse * position_y;
            // fld, fsub 0.5 (exact in x87's precision: double holds it), fistp in the default rounding.
            const float center[ 2 ] = { static_cast< float >( static_cast< std::int32_t >( std::lrint( static_cast< double >( x ) - 0.5 ) ) ),
                                        static_cast< float >( static_cast< std::int32_t >( std::lrint( static_cast< double >( y ) - 0.5 ) ) ) };
            gfx_programs_t::set_vec2( api, p, *m_foam_center, center );
        }
        if ( m_desc.m_reflect != 0u )
        {
            const gfx_texture_t* texture = reflection->texture( );
            if ( m_water_z )
                gfx_programs_t::set_word( api, p, *m_water_z, bits( water_z ) );
            if ( m_offset )
            {
                float v[ 2 ];
                reflection->offset( v );
                gfx_programs_t::set_vec2( api, p, *m_offset, v );
            }
            if ( m_scale )
            {
                float v[ 2 ];
                reflection->scale( v );
                gfx_programs_t::set_vec2( api, p, *m_scale, v );
            }
            d.set_texture( static_cast< std::uint32_t >( m_reflection_slot ), texture );
        }
        else if ( m_cube )
            d.set_texture( static_cast< std::uint32_t >( m_reflection_slot ), m_cube );
        d.blend_enable( m_desc.m_diffuse[ 3 ] < 0xFFu ? 1u : 0u );
    }

    // 0x595790 (called through 0x594D70) for one of the 15-word records (grid.hxx water_grid_draw_t): bind, then the dynamic indexed draw of
    // the surface's records and indices. Retail's search of the frame's list for the surface is the caller's.
    void patch( gfx_device_t& d, const water_shader_statics_t& statics, const float* position, std::int32_t vertex_start, std::int32_t vertex_count, std::int32_t index_start, std::int32_t index_count,
                const void* vertices, const std::uint16_t* indices, const gfx_texture_t* normal, float normal_size, water_reflection_source_t* reflection ) const
    {
        bind( d, statics, position[ 0 ], position[ 1 ], position[ 2 ], normal, normal_size, reflection );
        d.draw_indexed_dynamic( 3u, static_cast< std::uint32_t >( vertex_count ), static_cast< std::uint32_t >( index_count / 3 ), indices + index_start,
                                static_cast< const std::uint8_t* >( vertices ) + ( static_cast< std::size_t >( vertex_start ) << 5 ), 0x20u );
    }

private:
    static std::uint32_t bits( float f )
    {
        std::uint32_t u;
        std::memcpy( &u, &f, 4 );
        return u;
    }
};
