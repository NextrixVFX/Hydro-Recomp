#pragma once

// The water surfaces on the real device (water batch 35): retail's own Water/* programs (the DXBC of the VuCompiledShaderAsset entries), VuWaterShader
// (world/shading.hxx), the 64 x 64 wave texture (world/wavemap.hxx) and the frame's 32-byte records (world/grid.hxx water_grid_frame).
//
// What retail does around this, and who stands for it here:
//   - 0x595520 submits one command per surface in sort layer 0xA; its state is k_gfx_layers[ 10 ] (blend on, depth on, depth write off, 4 / 5).
//     The command's callback is 0x594D70 -> 0x595790: water_shader_t::patch. SUBSTITUTION until the gfx sort is wired: draw( ) walks the surfaces
//     itself, in the frame's order, sets the layer's state once and sets the program's frame constants as 0x53B0F0 does.
//   - VuWater +0x44 and +0x46 (1 from its ctor 0x528050, then 0x4B50A0's fidelity: Low 0 0, Medium 1 0, High 1 1): water_stage_t::surface applies 0x5DB800's gates, tick( ) 0x607D20's, the engine 0x558C00's.
//   - The texture build is the render thread's 0x607D20 -> 0x607F10, the tick 0x607D00 in the "Build" phase: tick( ) is both, once per game frame.
// Not recreated: the depth pass 0x595AA0 (only with renderer +0x2AC: the ctor's byte argument, its caller not read).

#include <workspace/core/entity/property.hxx>
#include <workspace/core/render/direct.hxx>
#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/program.hxx>
#include <workspace/core/render/resource.hxx>
#include <workspace/core/world/grid.hxx>
#include <workspace/core/world/shading.hxx>
#include <workspace/core/world/wavemap.hxx>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The frame's inputs, retail's statics: 0x759550 gViewProjMatrix (rows, a row vector times the matrix), 0x7594A0 gEyeWorld, the light manager's
// gDirLightWorld, 0x7596E8 / 0x7596EC the fog range, 0x7596F0 gFogColor, 0x759708 the water colours.
struct water_pass_view_t
{
    float m_view_proj[ 16 ] = { };
    float m_eye[ 3 ] = { };
    float m_light[ 3 ] = { };
    float m_fog_start = 0.0f, m_fog_end = 1.0f;
    std::uint8_t m_fog_colour[ 4 ] = { };   // a VuColor: 0x53B0F0 sets it through slot 11, r g b over 255
    water_shader_statics_t m_statics;
};

// The bytes of a pack entry: shaders and textures are made from them here, as retail does.
struct water_pass_source_t
{
    std::function< bool( const std::string& entry, std::vector< std::uint8_t >& out ) > m_read;
    std::function< const gfx_texture_t*( const std::string& name ) > m_cube;   // 0x5941E0 + 0x541D20: null back = absent
};

struct water_pass_t final : water_shader_assets_t, water_reflection_source_t
{
    explicit water_pass_t( gfx_api_t& api ) : m_api( api ), m_device( api ), m_programs( api ), m_declarations( api, m_programs ) { }

    gfx_api_t& m_api;
    gfx_device_t m_device;
    gfx_programs_t m_programs;
    gfx_declarations_t m_declarations;
    water_texture_t m_wave;
    gfx_texture_object_t* m_wave_textures[ 2 ] = { };   // 0x607720's +0x44 / +0x48
    water_pass_source_t m_source;
    std::map< std::string, gfx_texture_object_t* > m_named;
    std::map< std::string, const gfx_program_t* > m_loaded;
    std::vector< std::unique_ptr< water_shader_t > > m_shaders;    // VuWater's shader list (+0x28), creation order
    std::vector< water_shader_desc_t > m_descs;                    // m_shaders' descs, for shared_shader
    gfx_handle_t m_reflection_sampler = 0;
    gfx_texture_t m_reflection;
    int m_missing_programs = 0;

    // The device's first steps, then 0x607720's textures (format 3, the options on the stack at 0x6079A2: wrap, linear, linear, the mip chain).
    bool create( const water_pass_source_t& source )
    {
        m_source = source;
        m_device.create_dynamic( );
        water_texture_init( m_wave, 3u );
        gfx_texture_options_t wave;
        wave.m_min = wave.m_mag = 2;
        wave.m_mip = 1;
        for ( gfx_texture_object_t*& t : m_wave_textures )
            t = gfx_texture_create( m_api, 0x40, 0x40, 3u, wave );
        // 0x57F7D0's sampler for a render target: clamp, linear, linear, one level.
        gfx_texture_options_t target;
        target.m_address_u = target.m_address_v = 1u;
        target.m_min = target.m_mag = 2;
        gfx_texture_object_t sampler;
        gfx_texture_sampler( m_api, sampler, target );
        m_reflection_sampler = sampler.m_sampler;
        return true;
    }

    // 0x5DA350's desc, for a surface whose entity changes nothing: the fog byte is the entity's, reflect and complex stay 1.
    static water_shader_desc_t default_desc( std::uint8_t fog )
    {
        water_shader_desc_t d;
        const std::uint8_t ambient[ 4 ] = { 0x2D, 0x3C, 0x42, 0xC0 }, diffuse[ 4 ] = { 0x3C, 0x50, 0x5A, 0xC0 }, foam_ambient[ 4 ] = { 0x80, 0x80, 0x80, 0xFF };
        const std::uint8_t foam_diffuse[ 4 ] = { 0xFF, 0xFF, 0xFF, 0xFF }, decal_ambient[ 4 ] = { 0x80, 0x80, 0x80, 0xFF };
        std::memcpy( d.m_ambient, ambient, 4 );
        std::memcpy( d.m_diffuse, diffuse, 4 );
        std::memcpy( d.m_foam_ambient, foam_ambient, 4 );
        std::memcpy( d.m_foam_diffuse, foam_diffuse, 4 );
        std::memcpy( d.m_decal_ambient, decal_ambient, 4 );
        std::memset( d.m_decal_diffuse, 0xFF, 4 );
        d.m_fog = fog;
        d.m_foam_size = 14.0f;
        d.m_foam_texture = "Water/WakeFroth";
        d.m_reflect = 1u;
        d.m_complex = 1u;
        d.m_cube = "Proxy_cube";
        return d;
    }

    // 0x5DBB80 on the entity's desc (+0xA4, its properties registered by the ctor 0x5DA4F0); 0x5DB800's gates follow in water_stage_t::surface.
    // The names, their desc fields: OverrideGlobalSettings +0x18, AmbientColor +0x19, DiffuseColor +0x1D, FoamAmbientColor +0x21, FoamDiffuseColor +0x25, FogEnabled +0x29, FoamTextureSize +0x2C,
    // FoamTextureAsset +0x30, ProceduralReflection +0x48, NormalMapEnabled +0x49 (Complex against Simple), ReflectionCubeTextureAsset +0x4C. The decal fields have no property.
    static water_shader_desc_t read_desc( const value_t& properties, const water_shader_desc_t& base = default_desc( 0u ) )
    {
        water_shader_desc_t d = base;
        const auto colour = [ & ]( const char* name, std::uint8_t* bytes )
        {
            std::uint32_t fallback, v;
            std::memcpy( &fallback, bytes, 4 );
            property_color( properties, name, v, fallback );
            std::memcpy( bytes, &v, 4 );
        };
        property_bool( properties, "OverrideGlobalSettings", d.m_own_colours, d.m_own_colours );
        colour( "AmbientColor", d.m_ambient );
        colour( "DiffuseColor", d.m_diffuse );
        colour( "FoamAmbientColor", d.m_foam_ambient );
        colour( "FoamDiffuseColor", d.m_foam_diffuse );
        property_bool( properties, "FogEnabled", d.m_fog, d.m_fog );
        property_float( properties, "FoamTextureSize", d.m_foam_size, d.m_foam_size, false );
        property_string( properties, "FoamTextureAsset", d.m_foam_texture, d.m_foam_texture );
        property_bool( properties, "ProceduralReflection", d.m_reflect, d.m_reflect );
        property_bool( properties, "NormalMapEnabled", d.m_complex, d.m_complex );
        property_string( properties, "ReflectionCubeTextureAsset", d.m_cube, d.m_cube );
        return d;
    }

    // 0x527D50: the strings and bytes always; the colours, the foam size (ucomiss: a NaN is unequal) and the decal colours only with +0x18. The colour words overlap: +0x18's own byte is compared first.
    static bool desc_equal( const water_shader_desc_t& a, const water_shader_desc_t& b )
    {
        if ( a.m_name != b.m_name || a.m_own_colours != b.m_own_colours || a.m_fog != b.m_fog || a.m_foam_texture != b.m_foam_texture || a.m_reflect != b.m_reflect || a.m_complex != b.m_complex
             || a.m_cube != b.m_cube || a.m_decal_texture != b.m_decal_texture )
            return false;
        if ( a.m_own_colours == 0u )
            return true;
        return std::memcmp( a.m_ambient, b.m_ambient, 4 ) == 0 && std::memcmp( a.m_diffuse, b.m_diffuse, 4 ) == 0 && std::memcmp( a.m_foam_ambient, b.m_foam_ambient, 4 ) == 0
            && std::memcmp( a.m_foam_diffuse, b.m_foam_diffuse, 4 ) == 0 && a.m_foam_size == b.m_foam_size && std::memcmp( a.m_decal_ambient, b.m_decal_ambient, 4 ) == 0
            && std::memcmp( a.m_decal_diffuse, b.m_decal_diffuse, 4 ) == 0;
    }

    // 0x528610: an equal desc's shader, else a new one. The search walks the list from its head; retail's new shader goes on the list's end (list order is creation order).
    int shared_shader( const water_shader_desc_t& d )
    {
        for ( std::size_t k = 0; k < m_descs.size( ); ++k )
            if ( desc_equal( m_descs[ k ], d ) )
                return static_cast< int >( k );
        m_descs.push_back( d );
        return add_shader( d );
    }

    // One shader, shared or not as the caller decides.
    int add_shader( const water_shader_desc_t& d )
    {
        auto s = std::make_unique< water_shader_t >( );
        s->create( m_api, m_declarations, *this, d, 0.89999998f );
        m_shaders.push_back( std::move( s ) );
        return static_cast< int >( m_shaders.size( ) ) - 1;
    }

    // The "Build" tick 0x607D00, then the render thread's 0x607F10.
    void tick( float dt, bool build = true )
    {
        water_texture_tick( m_wave, dt );   // 0x607D00, the "Build" tick: always
        if ( !build )
            return;                         // 0x607D20: no command while VuWater +0x46 is clear
        water_texture_hooks_t h;
        h.user = this;
        h.upload = []( void* user, int texture, int level, const std::uint8_t* bytes, std::uint32_t )
        {
            water_pass_t& self = *static_cast< water_pass_t* >( user );
            gfx_texture_update( self.m_api, *self.m_wave_textures[ texture ], level, bytes );
        };
        water_texture_build( m_wave, h );
    }

    // The frame's constants as 0x53B0F0 sets them on a program that takes them.
    void frame_constants( const gfx_program_t& p, const water_pass_view_t& v )
    {
        if ( const gfx_constant_slot_t* c = p.constant( "gViewProjMatrix" ) )
            gfx_programs_t::set_matrix( m_api, p, *c, v.m_view_proj );
        if ( const gfx_constant_slot_t* c = p.constant( "gEyeWorld" ) )
            gfx_programs_t::set_vec3( m_api, p, *c, v.m_eye );
        if ( const gfx_constant_slot_t* c = p.constant( "gDirLightWorld" ) )
            gfx_programs_t::set_vec3( m_api, p, *c, v.m_light );
        const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
        if ( const gfx_constant_slot_t* c = p.constant( "gFogStart" ) )
            gfx_programs_t::set_word( m_api, p, *c, bits( v.m_fog_start ) );
        if ( const gfx_constant_slot_t* c = p.constant( "gFogInvRange" ) )
            gfx_programs_t::set_word( m_api, p, *c, bits( 1.0f / ( v.m_fog_end - v.m_fog_start ) ) );
        if ( const gfx_constant_slot_t* c = p.constant( "gFogColor" ) )
            gfx_programs_t::set_colour3( m_api, p, *c, v.m_fog_colour );
    }

    // The surfaces of one frame. `shader_of( surface )` answers the index add_shader gave; `reflection` is the planar target's view.
    void draw( const water_grid_frame_t& frame, const water_pass_view_t& v, gfx_handle_t reflection, const std::function< int( int surface ) >& shader_of )
    {
        m_reflection = gfx_texture_t{ 0u, m_reflection_sampler, reflection };
        const gfx_layer_values_t layer;
        m_device.begin( );
        m_device.depth_enable( layer.m_depth );
        m_device.depth_write( layer.m_write );
        m_device.blend_func( layer.m_src, layer.m_dst );
        m_device.colour_write( 1u );
        m_device.cull( 1u );   // the walk 0x53ADC0's k_gfx_cull on a layer change: index 1, D3D11_CULL_BACK
        for ( const water_grid_draw_t& d : frame.draws )
        {
            const int index = shader_of( d.surface );
            if ( index < 0 || index >= static_cast< int >( m_shaders.size( ) ) || d.vertex_count <= 0 )
                continue;
            const water_shader_t& s = *m_shaders[ static_cast< std::size_t >( index ) ];
            m_device.set_program( s.m_program );
            m_device.set_declaration( m_declaration_of( s ) );
            frame_constants( *s.m_program, v );
            s.patch( m_device, v.m_statics, d.position, d.vertex_start, d.vertex_count, d.index_start, d.index_count, frame.vertices.data( ), frame.indices.data( ),
                     m_wave_textures[ m_wave.current ], m_wave.built[ 7 ], this );   // the texture object's +0x40: NormalTextureScale as last built
        }
    }

    // water_shader_assets_t.
    const gfx_program_t* shader( const std::string& name ) override
    {
        const auto found = m_loaded.find( name );
        if ( found != m_loaded.end( ) )
            return found->second;
        std::vector< std::uint8_t > blob;
        if ( !m_source.m_read( "VuCompiledShaderAsset/" + name, blob ) )
        {
            ++m_missing_programs;
            return nullptr;
        }
        const gfx_program_t* p = m_programs.load( blob.data( ) );
        m_loaded.emplace( name, p );
        return p;
    }
    // 0x4269D0 asks the pack; 0x426A90 makes the asset through 0x57C740.
    bool texture_exists( const std::string& name ) override
    {
        std::vector< std::uint8_t > bytes;
        return !name.empty( ) && m_source.m_read( "VuTextureAsset/" + name, bytes );
    }
    const gfx_texture_t* texture( const std::string& name ) override
    {
        auto& slot = m_named[ name ];
        if ( slot == nullptr )
        {
            std::vector< std::uint8_t > bytes;
            m_source.m_read( "VuTextureAsset/" + name, bytes );
            slot = gfx_texture_load( m_api, bytes.data( ) );
        }
        return slot;
    }
    bool cube_exists( const std::string& name ) override { return m_source.m_cube && m_source.m_cube( name ) != nullptr; }   // only a shader with reflect 0 asks
    const gfx_texture_t* cube( const std::string& name ) override { return m_source.m_cube( name ); }
    std::uint32_t material( const gfx_program_t& program, gfx_declaration_t& d ) override
    {
        ++d.m_refs;
        m_layouts[ &program ] = d.m_layout;
        return 0u;
    }

    // water_reflection_source_t: the composer's defaults (ctor 0x5585F0: +0x44 / +0x48 0.5, +0x4C / +0x50 0.5).
    const gfx_texture_t* texture( ) override { return &m_reflection; }
    void offset( float* out ) override { out[ 0 ] = 0.5f; out[ 1 ] = 0.5f; }
    void scale( float* out ) override { out[ 0 ] = 0.5f; out[ 1 ] = 0.5f; }

private:
    // k_gfx_layers[ 10 ], which core/world/gfxsort.hxx's table holds; this header stays free of the sort.
    struct gfx_layer_values_t
    {
        std::uint8_t m_depth = 1, m_write = 0;
        std::uint32_t m_src = 4, m_dst = 5;
    };
    std::map< const gfx_program_t*, gfx_handle_t > m_layouts;
    gfx_handle_t m_declaration_of( const water_shader_t& s ) const { return m_layouts.at( s.m_program ); }
};

// THE RUNNING GAME'S SIDE: one object the world owns. attach( ) at the level's load, frame( ) where water_draw built the grid, the engine calls engine_pass inside its transparent stage.
#if HYDRO_HOST_WINDOWS || HYDRO_HOST_WASM
struct water_stage_t
{
    gfx_platform_api_t m_api;
    std::unique_ptr< water_pass_t > m_pass;
    std::vector< int > m_surface_shader;   // per surface index (the entity's +0x12C)
    gfx_handle_t m_mirror = 0;             // the composer's reflection target's texture (0x558870 -> vt+0xC)
    const water_grid_frame_t* m_frame = nullptr;
    water_pass_view_t m_view;

    // After attach: surface `index`'s shader, 0x5DB800 on the desc read from the surface entity's loads.
    void surface( int index, const std::vector< value_t >& loads, const std::uint8_t* flags )
    {
        water_shader_desc_t d = water_pass_t::default_desc( 0u );
        for ( const value_t& properties : loads )
            d = water_pass_t::read_desc( properties, d );
        if ( flags[ 0 ] == 0u )     // 0x5DB89C: cmove on VuWater +0x44
            d.m_reflect = 0u;
        if ( flags[ 2 ] == 0u )     // 0x5DB8AD: cmove on VuWater +0x46
            d.m_complex = 0u;
        if ( static_cast< int >( m_surface_shader.size( ) ) <= index )
            m_surface_shader.resize( static_cast< std::size_t >( index ) + 1u, -1 );
        m_surface_shader[ static_cast< std::size_t >( index ) ] = m_pass->shared_shader( d );
    }

    void attach( const water_pass_source_t& source )
    {
        void* device = nullptr;
        void* context = nullptr;
        void* mirror = nullptr;
        c_engine::device( device, context, mirror );
#if HYDRO_HOST_WINDOWS
        m_api.m_device = static_cast< ID3D11Device* >( device );
        m_api.m_context = static_cast< ID3D11DeviceContext* >( context );
#else
        ( void )device;
        ( void )context;
        ( void )mirror;
#endif
        m_pass = std::make_unique< water_pass_t >( m_api );
        m_pass->create( source );
        c_engine::water_pass( &water_stage_t::engine_pass, this );
    }

    // VuWaterTextureSettingsEntity's Trigger 0x5D2B00 wrote the wave texture's +4 (world/watchers.hxx); a change rebuilds the spectrum at the next build (wavemap.hxx).
    void wave_settings( const std::uint32_t* words )
    {
        std::memcpy( m_pass->m_wave.live, words, sizeof( m_pass->m_wave.live ) );
    }

    void frame( float dt, const water_grid_frame_t& grid, const water_pass_view_t& view, bool build )
    {
        m_frame = &grid;
        m_view = view;
        if ( m_pass )
            m_pass->tick( dt, build );
    }

    static void engine_pass( void* self )
    {
        water_stage_t& s = *static_cast< water_stage_t* >( self );
        if ( !s.m_pass || s.m_frame == nullptr )
            return;
        float view[ 16 ], projection[ 16 ];
        c_engine::matrices( view, projection, s.m_view.m_eye );
        // Our matrices are memory-compatible with retail's rows: view then projection, row vectors.
        for ( int r = 0; r < 4; ++r )
            for ( int c = 0; c < 4; ++c )
            {
                float v = 0.0f;
                for ( int k = 0; k < 4; ++k )
                    v += view[ r * 4 + k ] * projection[ k * 4 + c ];
                s.m_view.m_view_proj[ r * 4 + c ] = v;
            }
        s.m_pass->draw( *s.m_frame, s.m_view, s.m_mirror, [ &s ]( int surface )
        {
            return surface >= 0 && surface < static_cast< int >( s.m_surface_shader.size( ) ) ? s.m_surface_shader[ static_cast< std::size_t >( surface ) ] : -1;
        } );
    }
};
#endif
