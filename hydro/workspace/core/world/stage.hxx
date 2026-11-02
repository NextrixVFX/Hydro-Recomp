#pragma once

// The running game's scene on retail's device path (water batch 38): the shaders, buffers and materials a VuGfxStaticScene's load makes (0x5929B0, 0x593C00, 0x618160), retail's own sort (world/gfxsort.hxx)
// over gfx_backend_t, and the model callbacks (world/drawing.hxx). The programs are the DXBC embedded in each model.
//
// SUBSTITUTIONS, each until the composer frame owns the view (world/composer.hxx):
// - The engine still owns the frame: engine_pass runs inside c_geometry::draw's opaque stage, one flip per engine pass (main, mirror); retail has one flip a frame with the passes in the key's screen field.
// - The reflection's camera and clip plane take the viewport's plane (0x530F10, surface.hxx water_reflection_plane_tick).
// - The six planes come from the engine's view-projection, normalised, not from the camera's 0x57CBA0: inward and unit length as retail's, order near / far / sides differs (only a sign OR reads them).
// - m_depth_material and m_shadow_material hold the colour material: VuGfxUtil's depth and shadow programs (g 0x75902C +8 / +0xC) are not made; nothing submits those passes yet (no depth pre-pass, no shadow pass).
// NOT RECREATED: retain / release never free: the models live as long as the stage.

#include <impl/engine/engine.hxx>
#include <workspace/core/assets/scene.hxx>
#include <workspace/core/render/direct.hxx>
#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/program.hxx>
#include <workspace/core/render/resource.hxx>
#include <workspace/core/world/backend.hxx>
#include <workspace/core/world/corona.hxx>
#include <workspace/core/world/drawing.hxx>
#include <workspace/core/world/foliage.hxx>
#include <workspace/core/world/lights.hxx>
#include <workspace/core/world/gfxsort.hxx>
#include <workspace/core/world/meshes.hxx>
#include <workspace/core/world/livery.hxx>
#include <workspace/core/world/pfxdraw.hxx>
#include <workspace/core/world/settings.hxx>
#include <workspace/core/world/skeleton.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#if HYDRO_HOST_WINDOWS
constexpr std::uint32_t k_gfx_cb_water = 0x594D70u;

struct gfx_retail_model_t
{
    scene_t m_scene;
    std::vector< std::uint8_t > m_blob;
    gfx_scene_view_t m_view;
    std::vector< std::uint32_t > m_program_ids, m_layout_ids;   // per shader: what a skin's material is made on
    bool m_animated = false;
};

// A material list a skin made (0x421490's result, instance +8): the shader's index to the material.
struct gfx_retail_map_t final : gfx_material_map_t
{
    std::map< std::uint32_t, std::uint32_t > m_ids;
    std::uint32_t map( std::uint32_t name ) override { return m_ids.at( name ); }
};

// One boat's three instances (+0x2A4, +0x2A8, +0x2AC).
struct gfx_retail_boat_t
{
    gfx_animated_model_t m_high, m_middle;
    gfx_static_model_t m_low;
    gfx_retail_map_t m_maps[ 3 ];
};

struct gfx_retail_t
{
    gfx_retail_t( void* device, void* context ) : m_device( m_api ), m_programs( m_api ), m_declarations( m_api, m_programs ), m_backend( m_device ), m_sort( m_backend )
    {
        m_api.m_device = static_cast< ID3D11Device* >( device );
        m_api.m_context = static_cast< ID3D11DeviceContext* >( context );
        m_device.create_dynamic( );
        m_sort.reserve( );
        m_sort.m_single_thread = false;
        m_backend.m_has_light = true;
        m_backend.m_light_flip = [ this ]( ) { m_dynamic.m_fill ^= 1u; m_dynamic.m_render ^= 1u; };
        m_backend.m_asset = [ this ]( std::uint32_t kind, const std::string& name ) { return texture( kind, name ); };
        m_backend.m_source = [ this ]( std::uint32_t name ) { return source( name ); };
        const auto colour = [ this ]( gfx_model_pass_t pass ) { return [ this, pass ]( std::uint8_t* r ) { model_record( r, pass ); }; };
        m_backend.m_callbacks[ k_gfx_cb_model ] = colour( k_gfx_pass_colour );
        m_backend.m_callbacks[ k_gfx_cb_model_back ] = colour( k_gfx_pass_prepass );
        m_backend.m_callbacks[ k_gfx_cb_model_front ] = colour( k_gfx_pass_equal );
        const auto skinned = [ this ]( gfx_model_pass_t pass ) { return [ this, pass ]( std::uint8_t* r ) { skin_record( r, pass ); }; };
        m_backend.m_callbacks[ k_gfx_cb_skin ] = skinned( k_gfx_pass_colour );
        m_backend.m_callbacks[ k_gfx_cb_skin_back ] = skinned( k_gfx_pass_prepass );
        m_backend.m_callbacks[ k_gfx_cb_skin_front ] = skinned( k_gfx_pass_equal );
        // 0x5EBFF0 (the sky's record: the matrix, the part at +0x40): only the matrix constant, then the draw.
        m_backend.m_callbacks[ k_gfx_cb_sky ] = [ this ]( std::uint8_t* r )
        {
            std::uint32_t id;
            std::memcpy( &id, r + 0x40, 4 );
            const gfx_model_part_t& part = m_parts[ id - 1u ];
            gfx_programs_t::set_matrix( m_api, *part.m_shader->m_program, *part.m_shader->m_matrix, r );
            m_device.draw_indexed( 3u, part.m_start, part.m_triangles );
        };
        m_backend.m_callbacks[ k_gfx_cb_foliage ] = [ this ]( std::uint8_t* r ) { foliage_record( r ); };
        // 0x595520's layer: VuWater draws here. One command (retail one per surface, depth keyed); material 0 makes the walk bind the next material whole.
        m_backend.m_callbacks[ k_gfx_cb_water ] = [ this ]( std::uint8_t* ) { c_engine::water_now( ); m_device.invalidate( ); };
        m_backend.m_callbacks[ k_gfx_cb_sun ] = [ this ]( std::uint8_t* r ) { corona_record( r ); };
        m_backend.m_callbacks[ k_gfx_cb_pfx_quad ] = [ this ]( std::uint8_t* r ) { pfx_quad_record( r ); };
        m_backend.m_callbacks[ k_gfx_cb_pfx_trail ] = [ this ]( std::uint8_t* r ) { pfx_trail_record( r ); };
    }

    gfx_direct_t m_api;
    gfx_device_t m_device;
    gfx_programs_t m_programs;
    gfx_declarations_t m_declarations;
    gfx_backend_t m_backend;
    gfx_sort_t m_sort;
    std::function< bool( const std::string& entry, std::vector< std::uint8_t >& out ) > m_read;
    std::function< void( gfx_sort_t& sort, const gfx_draw_params_t& params ) > m_draw;   // the frame's walk: the draw manager's 0x5A75E0 until it owns the list
    std::map< std::string, std::unique_ptr< gfx_retail_model_t > > m_models;
    std::map< std::string, std::uint32_t > m_textures;
    std::vector< std::unique_ptr< gfx_texture_object_t > > m_kept;
    std::map< const void*, std::uint32_t > m_ids;                 // a program, layout or material's table id
    std::deque< gfx_model_shader_t > m_shaders;
    std::deque< gfx_model_part_t > m_parts;                       // a record's part id - 1
    std::uint32_t m_missing_textures = 0, m_cubes_absent = 0;
    struct cube_slot_t
    {
        gfx_texture_object_t* m_object;
        std::string m_resolved;
    };
    std::map< std::string, std::uint32_t > m_cube_ids;
    std::map< std::string, cube_slot_t > m_cube_objects;
    std::function< std::string( const std::string& ) > m_cube_name;
    const gfx_scene_view_t* m_sky = nullptr;   // VuSkyBoxEntity's asset
    bool m_sky_reflect = false;
    float m_view[ 16 ] = { }, m_view_proj[ 16 ] = { }, m_eye[ 4 ] = { };
    std::uint8_t m_fog_colour[ 4 ] = { };
    light_manager_t m_lights;
    gfx_camera_t m_camera;
    int m_passes = 0;
    std::vector< const float* > m_palettes;   // this pass's skinned draws
    std::vector< std::uint16_t > m_quad_indices;   // VuGfxUtil +0x48
    float m_plane = 0.0f;                          // the reflection plane, viewport +0x268
    gfx_light_manager_t m_dynamic;                 // g 0x759094's lists and tree
    std::deque< gfx_light_t > m_dynamic_lights;    // the VuDynamicLightEntity lights, +0x70
    // The quad renderer's table (0x14 each: 0x58CCC0) and the trail renderer's.
    struct pfx_quad_program_t
    {
        const gfx_program_t* m_program = nullptr;
        std::uint32_t m_material = 0;
        const gfx_constant_slot_t* m_clip = nullptr;
        std::int32_t m_colour = -1, m_tile = -1;
    };
    pfx_quad_program_t m_pfx_quad[ 8 ];
    std::uint32_t m_pfx_materials[ 8 ] = { }, m_pfx_trail_material = 0;
    std::int32_t m_pfx_trail_sampler = -1;
    std::vector< std::uint8_t > m_pfx_vertices;    // the static 0x799898
    const gfx_texture_t* m_white = nullptr;
    gfx_program_t* m_corona_program = nullptr;
    std::uint32_t m_corona_layout = 0;
    std::map< std::string, std::uint32_t > m_corona_materials;
    float m_projection[ 16 ] = { };
    float m_right[ 3 ] = { }, m_up[ 3 ] = { };
    std::function< const std::uint8_t*( std::uint32_t ) > m_pfx_node;
    std::function< void( std::uint32_t, std::uint32_t&, std::uint32_t& ) > m_pfx_assets;

    // A VuCubeTextureAsset's object (0x5AE140). A substitution (VuSubstituteAssetEntity) changes the object in place: `m_cube_name` answers the resolved name, frame( ) follows it.
    gfx_texture_object_t* cube_object( const std::string& name )
    {
        std::vector< std::uint8_t > bytes;
        if ( name.empty( ) || !m_read( "VuCubeTextureAsset/" + name, bytes ) )
            return nullptr;
        return gfx_cube_load( m_api, bytes.data( ) );
    }

    // 0x4269D0 + 0x426A90 by asset kind (0 VuTextureAsset, 1 VuCubeTextureAsset).
    std::uint32_t texture( std::uint32_t kind, const std::string& name )
    {
        if ( kind != 0u )
        {
            const auto cube = m_cube_ids.find( name );
            if ( cube != m_cube_ids.end( ) )
                return cube->second;
            gfx_texture_object_t* object = cube_object( m_cube_name ? m_cube_name( name ) : name );
            if ( object == nullptr )
            {
                ++m_cubes_absent;
                return 0u;
            }
            const std::uint32_t id = m_backend.add_texture( object );
            m_cube_ids.emplace( name, id );
            m_cube_objects.emplace( name, cube_slot_t{ object, m_cube_name ? m_cube_name( name ) : name } );
            return id;
        }
        const auto found = m_textures.find( name );
        if ( found != m_textures.end( ) )
            return found->second;
        std::vector< std::uint8_t > bytes;
        std::uint32_t id = 0u;
        if ( !name.empty( ) && m_read( "VuTextureAsset/" + name, bytes ) )
            id = m_backend.add_texture( gfx_texture_load( m_api, bytes.data( ) ) );
        else
            ++m_missing_textures;
        m_textures.emplace( name, id );
        return id;
    }

    const void* source( std::uint32_t name ) const
    {
        switch ( name )
        {
        case k_gfx_view_matrix: return m_view;
        case k_gfx_view_proj_matrix: return m_view_proj;
        case k_gfx_eye_world: return m_eye;
        case k_gfx_fog_color: return m_fog_colour;
        case k_gfx_light + 0xCC: return &m_lights.ambient;
        case k_gfx_light + 0xE0: return m_lights.direction;
        case k_gfx_light + 0xF0: return &m_lights.front;
        case k_gfx_light + 0xF4: return &m_lights.back;
        case k_gfx_light + 0xF8: return &m_lights.specular;
        default: return m_view;
        }
    }

    std::uint32_t id_of( const void* object, const std::function< std::uint32_t( ) >& add )
    {
        const auto found = m_ids.find( object );
        if ( found != m_ids.end( ) )
            return found->second;
        const std::uint32_t id = add( );
        m_ids.emplace( object, id );
        return id;
    }

    // A model's own record, null when the pack has none: the asset's load 0x542C30 (static) or 0x5A83E0 (animated, its scene with no roots; the skeleton is rig.hxx's), then the device half of 0x5C1560:
    // per shader 0x5929B0's tail, per chunk 0x618160's.
    gfx_retail_model_t* record( const std::string& asset )
    {
        const auto found = m_models.find( asset );
        if ( found != m_models.end( ) )
            return found->second.get( );
        auto made = std::make_unique< gfx_retail_model_t >( );
        if ( !m_read( "VuStaticModelAsset/" + asset, made->m_blob ) || !scene_load_static( made->m_blob.data( ), made->m_blob.size( ), made->m_scene ) )
        {
            skel_model_asset_t animated;
            if ( !m_read( "VuAnimatedModelAsset/" + asset, made->m_blob ) || !skel_model_asset_load( made->m_blob.data( ), made->m_blob.size( ), animated ) )
            {
                m_models.emplace( asset, nullptr );
                return nullptr;
            }
            made->m_scene = std::move( animated.scene );
            made->m_animated = true;
        }
        gfx_retail_model_t& m = *made;
        gfx_scene_view_build( m.m_view, m.m_scene, m.m_blob.data( ) );
        std::vector< const gfx_model_shader_t* > shaders( m.m_scene.shaders.size( ) );
        for ( std::size_t i = 0; i < m.m_scene.shaders.size( ); ++i )
            shaders[ i ] = shader( m, m.m_scene.shaders[ i ], m.m_blob.data( ), m.m_view.m_rows[ i ] );
        std::vector< std::uint32_t > chunks( m.m_scene.chunks.size( ) );
        for ( std::size_t i = 0; i < m.m_scene.chunks.size( ); ++i )
            chunks[ i ] = chunk( m.m_scene.chunks[ i ], m.m_blob.data( ) );
        std::size_t at = 0;
        for ( const scene_mesh_t& mesh : m.m_scene.meshes )
            for ( const scene_part_t& part : mesh.parts )
            {
                gfx_scene_part_t& p = m.m_view.m_parts[ at++ ];
                p.m_chunk = chunks[ part.chunk ];
                m_parts.emplace_back( );
                gfx_model_part_t& out = m_parts.back( );
                out.m_shader = shaders[ part.shader ];
                std::memcpy( out.m_aabb, part.aabb, sizeof( out.m_aabb ) );
                out.m_start = part.first_index;
                out.m_triangles = part.triangles;
                p.m_id = static_cast< std::uint32_t >( m_parts.size( ) );
            }
        return m_models.emplace( asset, std::move( made ) ).first->second.get( );
    }

    const gfx_scene_view_t* model( const std::string& asset )
    {
        const gfx_retail_model_t* m = record( asset );
        return m ? &m->m_view : nullptr;
    }

    static void table_desc( const scene_table_t& t, gfx_material_desc_t& desc )
    {
        for ( const scene_constant_t& c : t.constants )
        {
            gfx_material_desc_t::constant_t k;
            k.m_name = c.name;
            k.m_kind = c.type;
            std::memcpy( k.m_value, c.value, sizeof( k.m_value ) );
            desc.m_constants.push_back( k );
        }
        for ( const scene_texture_t& t2 : t.textures )
        {
            gfx_material_desc_t::texture_t k;
            k.m_name = t2.name;
            k.m_slot = t2.slot;
            k.m_kind = t2.value;
            k.m_asset = t2.asset;
            desc.m_textures.push_back( k );
        }
    }

    // A skin's material of shader `s` (0x421490): the same program and declaration, the skinned desc, 0x53A440.
    std::uint32_t skinned_material( const gfx_retail_model_t& m, std::size_t s, const scene_table_t& table )
    {
        gfx_material_desc_t desc;
        table_desc( table, desc );
        const gfx_material_t* material = m_sort.material_create( m.m_program_ids[ s ], m.m_layout_ids[ s ], desc );
        return id_of( material, [ & ] { return m_backend.add_material( material ); } );
    }

    // 0x5AC0C0, 0x540E00, 0x541290 + 0x5414C0 into the desc, 0x53A440, then 0x593C00's constants.
    const gfx_model_shader_t* shader( gfx_retail_model_t& model, const scene_shader_t& s, const std::uint8_t* blob, gfx_scene_material_t& row )
    {
        gfx_program_t* program = m_programs.load( blob + s.program.at );
        std::vector< gfx_vertex_element_t > elements( s.elements.size( ) );
        for ( std::size_t i = 0; i < elements.size( ); ++i )
        {
            elements[ i ].m_stream = s.elements[ i ].stream;
            elements[ i ].m_offset = s.elements[ i ].offset;
            elements[ i ].m_type = s.elements[ i ].type;
            elements[ i ].m_usage = s.elements[ i ].usage;
            elements[ i ].m_index = s.elements[ i ].index;
        }
        gfx_declaration_t* declaration = m_declarations.create( elements, *program );
        gfx_material_desc_t desc;
        table_desc( s.tables[ 0 ], desc );
        const std::uint32_t program_id = id_of( program, [ & ] { return m_backend.add_program( program ); } );
        const gfx_handle_t layout = declaration->m_layout;
        const std::uint32_t layout_id = id_of( declaration, [ & ] { return m_backend.add_layout( layout ); } );
        model.m_program_ids.push_back( program_id );
        model.m_layout_ids.push_back( layout_id );
        const gfx_material_t* material = m_sort.material_create( program_id, layout_id, desc );
        row.m_material = row.m_depth_material = row.m_shadow_material = id_of( material, [ & ] { return m_backend.add_material( material ); } );
        m_shaders.emplace_back( );
        gfx_model_shader_t& out = m_shaders.back( );
        out.m_material = s.bytes[ 0 ];
        out.m_program = program;
        out.m_matrix = program->constant( "gModelMatrix" );
        out.m_palette = program->constant( "gMatrixArray" );
        out.m_colour = program->constant( "gColor" );
        out.m_float = program->constant( "gWaterZ" );
        out.m_light_dirs = program->constant( "gDynamicLightDirections" );
        out.m_light_cols = program->constant( "gDynamicLightDiffuseColors" );
        return &out;
    }

    // 0x618160: the vertex and index buffers, their contents, 0x53A7C0.
    std::uint32_t chunk( const scene_chunk_t& c, const std::uint8_t* blob )
    {
        gfx_data_buffer_t* vertices = gfx_buffer_create( m_api, c.vertices.size, false );
        vertices->set( m_api, blob + c.vertices.offset, c.vertices.size );
        gfx_data_buffer_t* indices = gfx_buffer_create( m_api, c.indices, true );
        indices->set( m_api, blob + c.index_bytes.offset, c.indices );
        const std::uint32_t stream = m_backend.add_buffer( vertices ), index = m_backend.add_buffer( indices );
        const gfx_chunk_t* made = m_sort.chunk_create( stream, index, c.stride );
        return id_of( made, [ & ] { return m_backend.add_chunk( made ); } );
    }

    // 0x5BB3A0's device half: the two programs, the declaration { position 0, uv 12, colour 20 } made on the first, one material each.
    void foliage_materials( std::uint32_t out[ 2 ] )
    {
        const char* names[ 2 ] = { "Foliage/Simple", "Foliage/Fog" };
        std::vector< std::uint8_t > blob;
        gfx_program_t* programs[ 2 ] = { };
        for ( int i = 0; i < 2; ++i )
        {
            if ( !m_read( std::string( "VuCompiledShaderAsset/" ) + names[ i ], blob ) )
                return;
            programs[ i ] = m_programs.load( blob.data( ) );
        }
        gfx_vertex_element_t elements[ 3 ];
        elements[ 0 ].m_stream = 0; elements[ 0 ].m_offset = 0;  elements[ 0 ].m_type = 2; elements[ 0 ].m_usage = 0; elements[ 0 ].m_index = 0;
        elements[ 1 ].m_stream = 0; elements[ 1 ].m_offset = 12; elements[ 1 ].m_type = 1; elements[ 1 ].m_usage = 6; elements[ 1 ].m_index = 0;
        elements[ 2 ].m_stream = 0; elements[ 2 ].m_offset = 20; elements[ 2 ].m_type = 7; elements[ 2 ].m_usage = 2; elements[ 2 ].m_index = 0;
        gfx_declaration_t* declaration = m_declarations.create( std::vector< gfx_vertex_element_t >( elements, elements + 3 ), *programs[ 0 ] );
        const gfx_handle_t layout = declaration->m_layout;
        const std::uint32_t layout_id = id_of( declaration, [ & ] { return m_backend.add_layout( layout ); } );
        for ( int i = 0; i < 2; ++i )
        {
            gfx_program_t* program = programs[ i ];
            const std::uint32_t program_id = id_of( program, [ & ] { return m_backend.add_program( program ); } );
            const gfx_material_t* material = m_sort.material_create( program_id, layout_id, gfx_material_desc_t{ } );
            out[ i ] = id_of( material, [ & ] { return m_backend.add_material( material ); } );
        }
    }

    // 0x50B420: VuGfxUtil +0x48 holds at least `count` quads, 0 1 2 0 2 3 each.
    void quad_indices( std::int32_t count )
    {
        for ( std::size_t q = m_quad_indices.size( ) / 6u; q < static_cast< std::size_t >( count ); ++q )
        {
            const std::uint16_t v = static_cast< std::uint16_t >( q * 4u );
            for ( const std::uint16_t i : { std::uint16_t( v ), std::uint16_t( v + 1 ), std::uint16_t( v + 2 ), std::uint16_t( v ), std::uint16_t( v + 2 ), std::uint16_t( v + 3 ) } )
                m_quad_indices.push_back( i );
        }
    }

    // 0x5BBDD0: { texture, count, count x 4 vertices }: the texture in slot 0, the shared quad indices (0x50B420: 0 1 2 0 2 3 per quad), the dynamic draw.
    void foliage_record( std::uint8_t* record )
    {
        std::uint32_t texture;
        std::int32_t count;
        std::memcpy( &texture, record, 4 );
        std::memcpy( &count, record + 4, 4 );
        m_device.set_texture( 0u, texture ? m_backend.m_texture_table[ texture - 1u ] : nullptr );
        quad_indices( count );
        m_device.draw_indexed_dynamic( 3u, static_cast< std::uint32_t >( count ) * 4u, static_cast< std::uint32_t >( count ) * 2u, m_quad_indices.data( ), record + 8, 0x18u );
    }

    // The quad renderer's 0x58BAC0 + 0x58CCC0 per program and the trail renderer's 0x58CE40, the device half: { position 0, colour 12, uv pair 16 } / { position 0, uv 12, colour 20 }, one material each with no desc.
    bool pfx_programs( )
    {
        static const char* const names[ 8 ] = { "Pfx/Quad/Simple", "Pfx/Quad/Fog", "Pfx/Quad/Tile", "Pfx/Quad/TileFog", "Pfx/Quad/Clip", "Pfx/Quad/ClipFog", "Pfx/Quad/ClipTile", "Pfx/Quad/ClipTileFog" };
        const auto material = [ this ]( gfx_program_t* program, gfx_declaration_t* declaration ) {
            const gfx_handle_t layout = declaration->m_layout;
            const std::uint32_t layout_id = id_of( declaration, [ & ] { return m_backend.add_layout( layout ); } );
            const std::uint32_t program_id = id_of( program, [ & ] { return m_backend.add_program( program ); } );
            const gfx_material_t* made = m_sort.material_create( program_id, layout_id, gfx_material_desc_t{ } );
            return id_of( made, [ & ] { return m_backend.add_material( made ); } );
        };
        const auto element = []( std::uint32_t offset, std::uint32_t type, std::uint32_t usage ) {
            gfx_vertex_element_t e;
            e.m_stream = 0; e.m_offset = offset; e.m_type = type; e.m_usage = usage; e.m_index = 0;
            return e;
        };
        std::vector< std::uint8_t > blob;
        const std::vector< gfx_vertex_element_t > quad = { element( 0, 2, 0 ), element( 12, 7, 2 ), element( 16, 3, 6 ) };
        for ( int i = 0; i < 8; ++i )
        {
            if ( !m_read( std::string( "VuCompiledShaderAsset/" ) + names[ i ], blob ) )
                return false;
            gfx_program_t* program = m_programs.load( blob.data( ) );
            m_pfx_quad[ i ].m_program = program;
            m_pfx_quad[ i ].m_material = material( program, m_declarations.create( quad, *program ) );
            m_pfx_quad[ i ].m_clip = program->constant( "gClipThreshold" );
            m_pfx_quad[ i ].m_tile = program->sampler( "gTileTexture" );
            m_pfx_quad[ i ].m_colour = program->sampler( "gColorTexture" );
            m_pfx_materials[ i ] = m_pfx_quad[ i ].m_material;
        }
        if ( !m_read( "VuCompiledShaderAsset/Pfx/Trail", blob ) )
            return false;
        gfx_program_t* trail = m_programs.load( blob.data( ) );
        m_pfx_trail_material = material( trail, m_declarations.create( { element( 0, 2, 0 ), element( 12, 1, 6 ), element( 20, 7, 2 ) }, *trail ) );
        m_pfx_trail_sampler = trail->sampler( "gColorTexture" );
        return true;
    }

    // A corona's material (0x5E7ED0): VuGfxUtil's shader set entry 5 (+0x3C, +0x44: 0x5421C0's Basic/3dXyzUv), tex0 = the texture.
    std::uint32_t corona_material( const std::string& name )
    {
        const auto found = m_corona_materials.find( name );
        if ( found != m_corona_materials.end( ) )
            return found->second;
        if ( m_corona_program == nullptr )
        {
            std::vector< std::uint8_t > blob;
            if ( !m_read( "VuCompiledShaderAsset/Basic/3dXyzUv", blob ) )
                return 0u;
            m_corona_program = m_programs.load( blob.data( ) );
            gfx_vertex_element_t xyz, uv;
            xyz.m_type = 2u;
            uv.m_offset = 12u;
            uv.m_type = 1u;
            uv.m_usage = 6u;
            gfx_declaration_t* declaration = m_declarations.create( { xyz, uv }, *m_corona_program );
            const gfx_handle_t layout = declaration->m_layout;
            m_corona_layout = id_of( declaration, [ & ] { return m_backend.add_layout( layout ); } );
        }
        gfx_program_t* program = m_corona_program;
        const std::uint32_t program_id = id_of( program, [ & ] { return m_backend.add_program( program ); } );
        gfx_material_desc_t desc;
        gfx_material_desc_t::texture_t t;
        t.m_name = "tex0";
        t.m_asset = name;
        desc.m_textures.push_back( t );
        const gfx_material_t* material = m_sort.material_create( program_id, m_corona_layout, desc );
        const std::uint32_t id = id_of( material, [ & ] { return m_backend.add_material( material ); } );
        m_corona_materials.emplace( name, id );
        return id;
    }

    // The record 0x61D1A0 / 0x5E82D0 write.
    void corona_submit( gfx_sort_t& sort, const float* position, std::uint32_t colour, float size, float rotation, float amount, std::uint32_t material )
    {
        std::uint8_t* r = sort.allocate( 0x20 );
        std::memcpy( r, position, 12 );
        std::memcpy( r + 0x10, &colour, 4 );
        std::memcpy( r + 0x14, &size, 4 );
        std::memcpy( r + 0x18, &rotation, 4 );
        std::memcpy( r + 0x1C, &amount, 4 );
        sort.submit_keyed( 0x12, material, 0, k_gfx_cb_sun, 0.0f );
    }

    // 0x5E8410: the camera-facing card. The roll is the view-space x over z through the projection, scaled and offset. M = rotationY( roll ) x ( scale( s, 1, s ) x the camera's world at the position ).
    void corona_record( const std::uint8_t* r )
    {
        float p[ 3 ], size, offset, amount;
        std::memcpy( p, r, 12 );
        std::memcpy( &size, r + 0x14, 4 );
        std::memcpy( &offset, r + 0x18, 4 );
        std::memcpy( &amount, r + 0x1C, 4 );
        const float* v = m_view;
        const float vx = ( ( p[ 0 ] * v[ 0 ] + p[ 1 ] * v[ 4 ] ) + p[ 2 ] * v[ 8 ] ) + v[ 12 ];
        const float vz = ( ( p[ 0 ] * v[ 2 ] + p[ 1 ] * v[ 6 ] ) + p[ 2 ] * v[ 10 ] ) + v[ 14 ];
        float m[ 16 ] = { m_right[ 0 ], m_right[ 1 ], m_right[ 2 ], 0.0f, m_camera.m_direction[ 0 ], m_camera.m_direction[ 1 ], m_camera.m_direction[ 2 ], 0.0f, m_up[ 0 ], m_up[ 1 ], m_up[ 2 ], 0.0f, p[ 0 ], p[ 1 ], p[ 2 ], 1.0f };
        const float scale[ 16 ] = { size, 0, 0, 0, 0, 1.0f, 0, 0, 0, 0, size, 0, 0, 0, 0, 1.0f };
        float a[ 16 ], b[ 16 ], mvp[ 16 ];
        hull_multiply( scale, a, m );
        const float u = 0.5f - ( ( m_projection[ 0 ] * vx ) / vz ) * 0.5f;
        const float angle = ( 0.5f - u ) * amount + offset;
        const float c = wave_x87_cos( angle ), s = wave_x87_sin( angle );
        const float rotate[ 16 ] = { c, 0, -s, 0, 0, 1.0f, 0, 0, s, 0, c, 0, 0, 0, 0, 1.0f };
        hull_multiply( rotate, b, a );
        hull_multiply( b, mvp, m_view_proj );
        gfx_programs_t::set_colour4( m_api, *m_corona_program, *m_corona_program->constant( "gColor" ), r + 0x10 );
        gfx_programs_t::set_matrix( m_api, *m_corona_program, *m_corona_program->constant( "gModelViewProjMatrix" ), mvp );
        const float strip[ 20 ] = { -0.5f, 0.0f, -0.5f, 0.0f, 1.0f, 0.5f, 0.0f, -0.5f, 1.0f, 1.0f, -0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 1.0f, 0.0f };
        m_device.depth_enable( 0u );
        m_device.draw_dynamic( 4u, 2u, strip, 0x14u );
        m_device.depth_enable( 1u );
    }

    const gfx_texture_t* pfx_texture( std::uint32_t id ) const { return id ? m_backend.m_texture_table[ id - 1u ] : nullptr; }

    // 0x58C340: the clip threshold when the program has one, the colour texture, the tile when the program has a sampler, the fill, the quads.
    void pfx_quad_record( std::uint8_t* record )
    {
        std::uint32_t shader, node;
        std::memcpy( &shader, record + 4, 4 );
        std::memcpy( &node, record + 8, 4 );
        std::int32_t count;
        std::memcpy( &count, record + 0x6C, 4 );
        const pfx_quad_program_t& q = m_pfx_quad[ shader ];
        const std::uint8_t* bytes = m_pfx_node( node );
        if ( q.m_clip != nullptr )
        {
            std::uint32_t bits;
            std::memcpy( &bits, bytes + 0x74, 4 );
            gfx_programs_t::set_word( m_api, *q.m_program, *q.m_clip, bits );
        }
        std::uint32_t colour = 0u, tile = 0u;
        m_pfx_assets( node, colour, tile );
        m_device.set_texture( static_cast< std::uint32_t >( q.m_colour ), pfx_texture( colour ) );
        if ( q.m_tile >= 0 )
            m_device.set_texture( static_cast< std::uint32_t >( q.m_tile ), pfx_texture( tile ) );
        const float camera[ 12 ] = { m_right[ 0 ], m_right[ 1 ], m_right[ 2 ], 0.0f, m_camera.m_direction[ 0 ], m_camera.m_direction[ 1 ], m_camera.m_direction[ 2 ], 0.0f, m_up[ 0 ], m_up[ 1 ], m_up[ 2 ], 0.0f };
        m_pfx_vertices.resize( static_cast< std::size_t >( count ) * 4u * sizeof( pfx_quad_vertex_t ) );
        pfx_quad_fill( record, bytes, camera, reinterpret_cast< pfx_quad_vertex_t* >( m_pfx_vertices.data( ) ) );
        quad_indices( count );
        m_device.draw_indexed_dynamic( 3u, static_cast< std::uint32_t >( count ) * 4u, static_cast< std::uint32_t >( count ) * 2u, m_quad_indices.data( ), m_pfx_vertices.data( ), 0x20u );
    }

    // 0x58D3F0: the texture, the ribbon as one strip; a 3D Ribbon (+0x3C not 0) with culling off around it.
    void pfx_trail_record( std::uint8_t* record )
    {
        std::uint32_t node;
        std::memcpy( &node, record + 4, 4 );
        std::int32_t count, type;
        std::memcpy( &count, record + 0x68, 4 );
        const std::uint8_t* bytes = m_pfx_node( node );
        std::memcpy( &type, bytes + 0x3C, 4 );
        std::uint32_t colour = 0u, tile = 0u;
        m_pfx_assets( node, colour, tile );
        // No asset: VuGfxUtil's white texture, g 0x75902C +0x1C (a 1 x 1 here).
        if ( colour == 0u && m_white == nullptr )
        {
            const std::uint32_t texel = 0xFFFFFFFFu;
            gfx_texture_object_t* white = gfx_texture_create( m_api, 1, 1, 4u, gfx_texture_options_t{ } );
            gfx_texture_update( m_api, *white, 0, &texel );
            m_white = white;
        }
        m_device.set_texture( static_cast< std::uint32_t >( m_pfx_trail_sampler ), colour != 0u ? pfx_texture( colour ) : m_white );
        m_pfx_vertices.resize( static_cast< std::size_t >( count ) * 2u * sizeof( pfx_trail_vertex_t ) );
        if ( pfx_trail_fill( record, bytes, m_eye, reinterpret_cast< pfx_trail_vertex_t* >( m_pfx_vertices.data( ) ) ) <= 0 )
            return;
        if ( type != 0 )
            m_device.cull( 0u );
        m_device.draw_dynamic( 4u, static_cast< std::uint32_t >( count ) * 2u - 2u, m_pfx_vertices.data( ), 0x18u );
        if ( type != 0 )
            m_device.cull( 1u );
    }

    // 0x562C40 for the command being drawn: the block's count at +0x140.
    void gather( const gfx_model_record_t& r, float* block )
    {
        std::memset( block, 0, 81 * 4 );
        const std::int32_t count = m_dynamic.gather( r.m_matrix, r.m_part->m_aabb, m_sort.m_drawn_hi, block );
        std::memcpy( block + 80, &count, 4 );
    }

    void model_record( std::uint8_t* record, gfx_model_pass_t pass )
    {
        gfx_model_record_t r;
        std::memcpy( r.m_matrix, record, 64 );
        std::memcpy( r.m_colour, record + 0x40, 4 );
        std::memcpy( &r.m_word, record + 0x44, 4 );
        std::uint32_t id;
        std::memcpy( &id, record + 0x48, 4 );
        r.m_part = &m_parts[ id - 1u ];
        r.m_lit = record[ 0x4C ];
        gfx_model_draw( m_device, r, pass, false, [ & ]( float* block ) { gather( r, block ); } );
    }

    // 0x533C90 0x533D40 0x533E30: the 0x58-byte record, +0x4C the bone count, +0x50 the palette (here m_palettes' index + 1), +0x54 the lit byte.
    void skin_record( std::uint8_t* record, gfx_model_pass_t pass )
    {
        gfx_model_record_t r;
        std::memcpy( r.m_matrix, record, 64 );
        std::memcpy( r.m_colour, record + 0x40, 4 );
        std::memcpy( &r.m_word, record + 0x44, 4 );
        std::uint32_t id, bones;
        std::memcpy( &id, record + 0x48, 4 );
        std::memcpy( &r.m_palette_count, record + 0x4C, 4 );
        std::memcpy( &bones, record + 0x50, 4 );
        r.m_part = &m_parts[ id - 1u ];
        r.m_palette = m_palettes[ bones - 1u ];
        r.m_lit = record[ 0x54 ];
        gfx_model_draw( m_device, r, pass, true, [ & ]( float* block ) { gather( r, block ); } );
    }

    // The instance's current palette (+0x1C + 4 x +0x28) as a record word.
    std::uint32_t palette_id( const float* palette )
    {
        m_palettes.push_back( palette );
        return static_cast< std::uint32_t >( m_palettes.size( ) );
    }

    // The settings record's fields the walk reads (0x7596E0: +8 / +0xC the fog range, +0x10 the colour), the far plane, the frame time: once a game frame.
    void frame( float dt, const std::uint32_t* record, float far_plane, const light_manager_t& lights )
    {
        std::memcpy( &m_backend.m_fog_start, &record[ 2 ], 4 );
        std::memcpy( &m_backend.m_fog_end, &record[ 3 ], 4 );
        std::memcpy( m_fog_colour, &record[ 4 ], 4 );
        m_backend.m_far_plane = far_plane;
        m_lights = lights;
        m_sort.tick( dt );
        if ( m_cube_name )
            for ( auto& slot : m_cube_objects )
            {
                const std::string resolved = m_cube_name( slot.first );
                if ( resolved == slot.second.m_resolved )
                    continue;
                slot.second.m_resolved = resolved;
                if ( gfx_texture_object_t* object = cube_object( resolved ) )
                {
                    slot.second.m_object->m_view = object->m_view;
                    slot.second.m_object->m_sampler = object->m_sampler;
                }
            }
    }

    // One engine pass: the view's statics, the planes, the frame's walk, the sort's frame. `reflecting`: the mirror pass, retail's 0x558C00 params: lod 0.05, the plane z = 0, the clip plane in clip space.
    void pass( bool reflecting )
    {
        if ( !m_draw )
            return;
        float* projection = m_projection;
        c_engine::matrices( m_view, projection, m_eye );
        for ( int r = 0; r < 4; ++r )
            for ( int c = 0; c < 4; ++c )
            {
                float v = 0.0f;
                for ( int k = 0; k < 4; ++k )
                    v += m_view[ r * 4 + k ] * projection[ k * 4 + c ];
                m_view_proj[ r * 4 + c ] = v;
            }
        const float* m = m_view_proj;
        const float rows[ 6 ][ 4 ] = { { m[ 2 ], m[ 6 ], m[ 10 ], m[ 14 ] }, { m[ 3 ] - m[ 2 ], m[ 7 ] - m[ 6 ], m[ 11 ] - m[ 10 ], m[ 15 ] - m[ 14 ] },
                                       { m[ 3 ] + m[ 0 ], m[ 7 ] + m[ 4 ], m[ 11 ] + m[ 8 ], m[ 15 ] + m[ 12 ] }, { m[ 3 ] - m[ 0 ], m[ 7 ] - m[ 4 ], m[ 11 ] - m[ 8 ], m[ 15 ] - m[ 12 ] },
                                       { m[ 3 ] + m[ 1 ], m[ 7 ] + m[ 5 ], m[ 11 ] + m[ 9 ], m[ 15 ] + m[ 13 ] }, { m[ 3 ] - m[ 1 ], m[ 7 ] - m[ 5 ], m[ 11 ] - m[ 9 ], m[ 15 ] - m[ 13 ] } };
        for ( int i = 0; i < 6; ++i )
        {
            const float n = std::sqrt( rows[ i ][ 0 ] * rows[ i ][ 0 ] + rows[ i ][ 1 ] * rows[ i ][ 1 ] + rows[ i ][ 2 ] * rows[ i ][ 2 ] );
            for ( int k = 0; k < 4; ++k )
                m_camera.m_planes[ i * 4 + k ] = rows[ i ][ k ] / n;
        }
        std::memcpy( m_camera.m_eye, m_eye, 12 );
        // The statics 0x7595D0 (right, direction, up): the view's rows in the engine's math_look.
        for ( int i = 0; i < 3; ++i )
        {
            m_right[ i ] = m_view[ i * 4 ];
            m_up[ i ] = m_view[ i * 4 + 1 ];
            m_camera.m_direction[ i ] = m_view[ i * 4 + 2 ];
        }
        m_camera.m_far = m_backend.m_far_plane;
        gfx_draw_params_t p;
        std::memcpy( p.m_eye, m_eye, 12 );
        p.m_camera = &m_camera;
        float clip[ 4 ] = { };
        if ( reflecting )
        {
            p.m_lod = 0.05f;
            p.m_reflecting = 1u;
            p.m_clip[ 2 ] = 1.0f;
            p.m_clip[ 3 ] = -m_plane;
            float inverse[ 16 ];
            std::memcpy( inverse, m_view_proj, 64 );
            hull_invert( inverse );
            for ( int r = 0; r < 4; ++r )
                clip[ r ] = ( ( inverse[ r * 4 + 1 ] + inverse[ r * 4 ] ) * 0.0f + inverse[ r * 4 + 2 ] ) + inverse[ r * 4 + 3 ] * -m_plane;   // 0x558C00's row
        }
        m_device.clip_plane( clip );
        // 0x562B10 for this view: the seventh plane is the reflection's.
        float planes[ 28 ];
        std::memcpy( planes, m_camera.m_planes, 96 );
        std::memcpy( planes + 24, p.m_clip, 16 );
        m_sort.set_layer( 1u );
        m_sort.set_screen( 8u );
        m_dynamic.submit( m_eye, planes, p.m_reflecting, m_sort.m_key_hi );
        m_palettes.clear( );
        if ( m_sky != nullptr && ( !reflecting || m_sky_reflect ) )
            gfx_sky_draw( m_sort, m_eye, m_sky );
        m_draw( m_sort, p );
        if ( !reflecting )
            m_sort.command( m_sort.m_key_lo, ( 0xAu << 14 ) | m_sort.m_key_hi | 0x2000u, k_gfx_cb_water, 0u, 0u, 0xAu );
        m_sort.flip( );
        if ( ++m_passes == 300 )
        {
            std::uint32_t unknown = 0;
            for ( const auto& u : m_backend.m_unknown )
                unknown += u.second;
            LOG_INFO( "retail scene: %d models, %d parts, %d materials, %d shaders; %u textures missing, %u cube slots absent, %u commands with no callback; last frame %u material binds, %u chunk binds",
                static_cast< int >( m_models.size( ) ), static_cast< int >( m_parts.size( ) ), m_sort.m_material_count, static_cast< int >( m_shaders.size( ) ), m_missing_textures, m_cubes_absent, unknown,
                m_sort.m_stats[ 0 ], m_sort.m_stats[ 1 ] );
        }
    }

    static void engine_pass( void* self, bool reflecting ) { static_cast< gfx_retail_t* >( self )->pass( reflecting ); }
};
#endif
