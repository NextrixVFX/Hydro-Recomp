#pragma once

// The running game's scene on retail's device path (water batch 38): the shaders, buffers and materials a VuGfxStaticScene's load makes (0x5929B0, 0x593C00, 0x618160), retail's own sort (world/gfxsort.hxx)
// over gfx_backend_t, and the model callbacks (world/drawing.hxx). The programs are the DXBC embedded in each model.
//
// SUBSTITUTIONS, each until the composer frame owns the view (world/composer.hxx):
// - The engine still owns the frame: engine_pass runs inside c_geometry::draw's opaque stage, one flip per engine pass (main, mirror); retail has one flip a frame with the passes in the key's screen field.
// - The six planes come from the engine's view-projection, normalised, not from the camera's 0x57CBA0: inward and unit length as retail's, order near / far / sides differs (only a sign OR reads them).
// - m_depth_material and m_shadow_material hold the colour material: VuGfxUtil's depth and shadow programs (g 0x75902C +8 / +0xC) are not made; nothing submits those passes yet (no depth pre-pass, no shadow pass).
// NOT RECREATED: the dynamic lights' gather (0x562C40) answers no light until lights.hxx is fed; retain / release never free: the models live as long as the stage.

#include <impl/engine/engine.hxx>
#include <workspace/core/assets/scene.hxx>
#include <workspace/core/render/direct.hxx>
#include <workspace/core/render/gfx.hxx>
#include <workspace/core/render/program.hxx>
#include <workspace/core/render/resource.hxx>
#include <workspace/core/world/backend.hxx>
#include <workspace/core/world/drawing.hxx>
#include <workspace/core/world/foliage.hxx>
#include <workspace/core/world/gfxsort.hxx>
#include <workspace/core/world/meshes.hxx>
#include <workspace/core/world/livery.hxx>
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

    // 0x5BBDD0: { texture, count, count x 4 vertices }: the texture in slot 0, the shared quad indices (0x50B420: 0 1 2 0 2 3 per quad), the dynamic draw.
    void foliage_record( std::uint8_t* record )
    {
        std::uint32_t texture;
        std::int32_t count;
        std::memcpy( &texture, record, 4 );
        std::memcpy( &count, record + 4, 4 );
        m_device.set_texture( 0u, texture ? m_backend.m_texture_table[ texture - 1u ] : nullptr );
        for ( std::size_t q = m_quad_indices.size( ) / 6u; q < static_cast< std::size_t >( count ); ++q )
        {
            const std::uint16_t v = static_cast< std::uint16_t >( q * 4u );
            for ( const std::uint16_t i : { std::uint16_t( v ), std::uint16_t( v + 1 ), std::uint16_t( v + 2 ), std::uint16_t( v ), std::uint16_t( v + 2 ), std::uint16_t( v + 3 ) } )
                m_quad_indices.push_back( i );
        }
        m_device.draw_indexed_dynamic( 3u, static_cast< std::uint32_t >( count ) * 4u, static_cast< std::uint32_t >( count ) * 2u, m_quad_indices.data( ), record + 8, 0x18u );
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
        gfx_model_draw( m_device, r, pass, false, []( float* block ) { std::memset( block, 0, 81 * 4 ); } );
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
        gfx_model_draw( m_device, r, pass, true, []( float* block ) { std::memset( block, 0, 81 * 4 ); } );
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
        float projection[ 16 ];
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
            float inverse[ 16 ];
            std::memcpy( inverse, m_view_proj, 64 );
            hull_invert( inverse );
            for ( int r = 0; r < 4; ++r )
                clip[ r ] = ( inverse[ r * 4 + 1 ] + inverse[ r * 4 ] ) * 0.0f + inverse[ r * 4 + 2 ];   // 0x558C00's row, plane 0
        }
        m_device.clip_plane( clip );
        m_sort.set_layer( 1u );
        m_sort.set_screen( 8u );
        m_palettes.clear( );
        if ( m_sky != nullptr && ( !reflecting || m_sky_reflect ) )
            gfx_sky_draw( m_sort, m_eye, m_sky );
        m_draw( m_sort, p );
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
