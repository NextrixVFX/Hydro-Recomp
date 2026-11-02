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
#include <workspace/core/world/gfxsort.hxx>
#include <workspace/core/world/meshes.hxx>
#include <workspace/core/world/settings.hxx>

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
    float m_view[ 16 ] = { }, m_view_proj[ 16 ] = { }, m_eye[ 4 ] = { };
    std::uint8_t m_fog_colour[ 4 ] = { };
    light_manager_t m_lights;
    gfx_camera_t m_camera;
    int m_passes = 0;

    // 0x4269D0 + 0x426A90 by asset kind. A kind 1 is VuCubeTextureAsset: not made yet, counted; the sampler stays as it was.
    std::uint32_t texture( std::uint32_t kind, const std::string& name )
    {
        if ( kind != 0u )
        {
            ++m_cubes_absent;
            return 0u;
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

    // The asset's load 0x542C30: the scene (assets/scene.hxx), then the device half of 0x5C1560: per shader 0x5929B0's tail, per chunk 0x618160's. Null: the pack has no such model.
    const gfx_scene_view_t* model( const std::string& asset )
    {
        const auto found = m_models.find( asset );
        if ( found != m_models.end( ) )
            return found->second ? &found->second->m_view : nullptr;
        auto made = std::make_unique< gfx_retail_model_t >( );
        if ( !m_read( "VuStaticModelAsset/" + asset, made->m_blob ) || !scene_load_static( made->m_blob.data( ), made->m_blob.size( ), made->m_scene ) )
        {
            m_models.emplace( asset, nullptr );
            return nullptr;
        }
        gfx_retail_model_t& m = *made;
        gfx_scene_view_build( m.m_view, m.m_scene, m.m_blob.data( ) );
        std::vector< const gfx_model_shader_t* > shaders( m.m_scene.shaders.size( ) );
        for ( std::size_t i = 0; i < m.m_scene.shaders.size( ); ++i )
            shaders[ i ] = shader( m.m_scene.shaders[ i ], m.m_blob.data( ), m.m_view.m_rows[ i ] );
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
        return &m_models.emplace( asset, std::move( made ) ).first->second->m_view;
    }

    // 0x5AC0C0, 0x540E00, 0x541290 + 0x5414C0 into the desc, 0x53A440, then 0x593C00's constants.
    const gfx_model_shader_t* shader( const scene_shader_t& s, const std::uint8_t* blob, gfx_scene_material_t& row )
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
        for ( const scene_constant_t& c : s.tables[ 0 ].constants )
        {
            gfx_material_desc_t::constant_t k;
            k.m_name = c.name;
            k.m_kind = c.type;
            std::memcpy( k.m_value, c.value, sizeof( k.m_value ) );
            desc.m_constants.push_back( k );
        }
        for ( const scene_texture_t& t : s.tables[ 0 ].textures )
        {
            gfx_material_desc_t::texture_t k;
            k.m_name = t.name;
            k.m_slot = t.slot;
            k.m_kind = t.value;
            k.m_asset = t.asset;
            desc.m_textures.push_back( k );
        }
        const std::uint32_t program_id = id_of( program, [ & ] { return m_backend.add_program( program ); } );
        const gfx_handle_t layout = declaration->m_layout;
        const std::uint32_t layout_id = id_of( declaration, [ & ] { return m_backend.add_layout( layout ); } );
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

    // The settings record's fields the walk reads (0x7596E0: +8 / +0xC the fog range, +0x10 the colour), the far plane, the frame time: once a game frame.
    void frame( float dt, const std::uint32_t* record, float far_plane, const light_manager_t& lights )
    {
        std::memcpy( &m_backend.m_fog_start, &record[ 2 ], 4 );
        std::memcpy( &m_backend.m_fog_end, &record[ 3 ], 4 );
        std::memcpy( m_fog_colour, &record[ 4 ], 4 );
        m_backend.m_far_plane = far_plane;
        m_lights = lights;
        m_sort.tick( dt );
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
