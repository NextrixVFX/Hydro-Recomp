#pragma once

// c_world's models - member definitions moved out of world.hxx (one class, split by concern).

inline std::string c_world::pack_path( ) const
{
#if defined( _WIN32 )
    char module_path[ MAX_PATH ]{};
    GetModuleFileNameA( nullptr, module_path, MAX_PATH );
    std::string directory = module_path;
    const auto slash = directory.find_last_of( "\\/" );
    if ( slash != std::string::npos )
        directory.resize( slash );
    for ( int step = 0; step < 8; ++step )
    {
        const std::string hydro = directory + "\\HydroThunderUWP\\Base.apf";
        const std::string local = directory + "\\Base.apf";
        if ( GetFileAttributesA( hydro.c_str( ) ) != INVALID_FILE_ATTRIBUTES )
            return hydro;
        if ( GetFileAttributesA( local.c_str( ) ) != INVALID_FILE_ATTRIBUTES )
            return local;
        const auto parent = directory.find_last_of( "\\/" );
        if ( parent == std::string::npos )
            break;
        directory.resize( parent );
    }
#endif
    return {};
}

inline int c_world::texture_for( const std::string& path )
{
    if ( path.empty( ) )
        return -1;
    const auto existing = m_textures.find( path );
    if ( existing != m_textures.end( ) )
        return existing->second;

    std::vector< std::uint8_t > bytes;
    image_t image{};
    const std::string name = "VuTextureAsset/" + path;
    if ( !m_archive.read( name.c_str( ), bytes ) || !decode_texture( bytes, image ) )
    {
        LOG_ERROR( "texture %s", name.c_str( ) );
        m_textures.emplace( path, -1 );
        return -1;
    }
    const int id = c_engine::texture( image.rgba.data( ), image.width, image.height );
    m_textures.emplace( path, id );
    return id;
}

// Resolve a material's declared cube to an engine handle. The name in the
// table is usually a placeholder - the hull asks for Proxy_cube - and the
// level's VuSubstituteAssetEntity is what points it at the real sky cube,
// Hydro/Outside on Hydroelectric. That is why 64 materials can all declare
// a cube and still share one reflection: it is a scene-wide asset.
inline int c_world::cube_for( const std::string& path )
{
    if ( path.empty( ) )
        return -1;
    // Keyed on the RESOLVED name, not the authored one. The cache outlives
    // a scene the way m_textures does, but m_substitute does not - so
    // caching "Proxy_cube" would hand the next level the previous level's
    // sky.
    const std::string resolved = m_rig.substitute( "VuCubeTextureAsset", path );
    const auto existing = m_cubes.find( resolved );
    if ( existing != m_cubes.end( ) )
        return existing->second;

    std::vector< std::uint8_t > bytes;
    cube_t decoded{};
    const std::string name = "VuCubeTextureAsset/" + resolved;
    if ( !m_archive.read( name.c_str( ), bytes ) || !decode_cube( bytes, decoded ) )
    {
        LOG_ERROR( "cube %s", name.c_str( ) );
        m_cubes.emplace( resolved, -1 );
        return -1;
    }
    const unsigned char* faces[ 6 ]{};
    for ( int index = 0; index < 6; ++index )
        faces[ index ] = decoded.rgba[ index ].data( );
    const int id = c_engine::cube( faces, decoded.face );
    LOG_SUCCESS( "cube %s -> %s  %d per face  id %d", path.c_str( ), resolved.c_str( ),
        decoded.face, id );
    m_cubes.emplace( resolved, id );
    return id;
}

inline const std::vector< c_world::pfx_part_t >& c_world::pfx_parts( const std::string& asset )
{
    const auto existing = m_pfx_models.find( asset );
    if ( existing != m_pfx_models.end( ) )
        return existing->second;
    std::vector< pfx_part_t > parts;
    for ( const cached_part_t& part : model_for( asset ) )
    {
        pfx_part_t piece{};
        piece.geometry = part.geometry;
        piece.texture = part.texture;
        piece.detail = part.detail;
        piece.index_start = part.index_start;
        piece.index_count = part.index_count;
        std::memcpy( piece.node, part.node, sizeof( piece.node ) );
        parts.push_back( piece );
    }
    LOG_INFO( "pfx geometry %s: %u parts", asset.c_str( ),
        static_cast< unsigned >( parts.size( ) ) );
    return m_pfx_models.emplace( asset, std::move( parts ) ).first->second;
}

inline const std::vector< c_world::cached_part_t >& c_world::model_for( const std::string& asset )
{
    const auto existing = m_models.find( asset );
    if ( existing != m_models.end( ) )
        return existing->second;

    std::vector< cached_part_t > cached;
    std::vector< std::uint8_t > bytes;
    model_t loaded{};
    const std::string static_name = "VuStaticModelAsset/" + asset;
    const std::string animated_name = "VuAnimatedModelAsset/" + asset;
    // A VuStaticModelAsset loads through retail's scene load (scene.hxx scene_load_static, via load_model_scene); the
    // animated asset still rides the heuristic load_model (its retail load, VuGfxAnimatedScene, is not recreated).
    bool loaded_ok = m_archive.read( static_name.c_str( ), bytes ) && load_model_scene( bytes, loaded );
    // --sc-variant O|X|Y (diagnostic, the pixel proof of the switch; tools tmp\variants.ps1): O = the old load_model
    // everywhere; X = the scene load, but the old part list where the counts differ (z09a, effects_7 / _11); Y = X plus
    // the old detail / alpha. Measured (2026-10-31): Y == O bit-exact on all three stills, X == the switched build -
    // every changed pixel is the next-shader-name defect; the part-list defects change none of the three stills.
    if ( loaded_ok && c_engine::option( "sc-variant" ) != nullptr )
    {
        model_t old{};
        load_model( bytes, old );
        const bool lists = old.parts.size( ) != loaded.parts.size( );
        if ( lists || c_engine::option( "sc-variant" )[ 0 ] == 'O' )
            loaded = old;
        else if ( c_engine::option( "sc-variant" )[ 0 ] == 'Y' )
            for ( std::size_t k = 0; k < old.parts.size( ); ++k )
            {
                loaded.parts[ k ].detail = old.parts[ k ].detail;
                loaded.parts[ k ].alpha_test = old.parts[ k ].alpha_test;
            }
    }
    // --scene-compare (diagnostic, the evidence for the switch): the old heuristic load_model beside the retail scene
    // load, per part. Measured on Hydroelectric (2026-10-31), all 77 static models: the buffers' contents identical on
    // all 77; geometry, textures, cubes, constants and node matrices identical on 74. Every difference is a PROVEN
    // load_model defect: (1) the detail / alpha differences - it took each material's shader name from the text
    // after table 2's "gAlphaTestEnabled", the NEXT shader's name (tools tmp\blendproof.py predicted 14 / 2 / 1 / 7 /
    // 1 / 66 / 1 / 7, measured the same); (2) z09a 41 of 259, effects_11 3 of 6 - it kept only the meshes after the
    // last mesh whose name lacks "Shape" (or has > 8 parts), then sized its pose table by that run and so rejected
    // the whole node tree (every part once, at the identity); (3) effects_7 52 of 88 - its invented duplicate filter
    // (same texture / chunk, index count within 32, aabb within 0.05, node within 0.01). tools tmp\partsproof.py
    // predicts the old part count from (2) + (3) on all 77 models: 77 of 77 equal to the measured count.
    if ( c_engine::option( "scene-compare" ) != nullptr && loaded_ok )
    {
        const model_t& scened = loaded;
        model_t heuristic{};
        const bool sok = load_model( bytes, heuristic );
        int geo = 0, tex = 0, det = 0, cube = 0, con = 0, alpha = 0, node = 0;
        const std::size_t n = scened.parts.size( ) < heuristic.parts.size( ) ? scened.parts.size( ) : heuristic.parts.size( );
        for ( std::size_t k = 0; k < n; ++k )
        {
            const model_part_t& a = heuristic.parts[ k ];
            const model_part_t& b = scened.parts[ k ];
            geo += a.buffer != b.buffer || a.index_start != b.index_start || a.index_count != b.index_count;
            tex += a.texture != b.texture;
            det += a.detail != b.detail;
            cube += a.cube != b.cube;
            con += std::memcmp( a.diffuse, b.diffuse, 12 ) != 0 || std::memcmp( a.specular, b.specular, 12 ) != 0
                || a.specular_amount != b.specular_amount || a.environment_amount != b.environment_amount;
            alpha += a.alpha_test != b.alpha_test;
            node += std::memcmp( a.node, b.node, 64 ) != 0;
            if ( a.texture != b.texture || a.alpha_test != b.alpha_test || std::memcmp( a.diffuse, b.diffuse, 12 ) != 0 )
                LOG_INFO( "scene-compare %s part %d: tex '%s' / '%s' alpha %d / %d diffuse %.3f %.3f %.3f / %.3f %.3f %.3f", asset.c_str( ), static_cast< int >( k ),
                    a.texture.c_str( ), b.texture.c_str( ), a.alpha_test, b.alpha_test, a.diffuse[ 0 ], a.diffuse[ 1 ], a.diffuse[ 2 ], b.diffuse[ 0 ], b.diffuse[ 1 ], b.diffuse[ 2 ] );
        }
        // The buffers' contents (the unpacked vertices, the indices, the skin binding), not only their count.
        int content = 0;
        for ( std::size_t k = 0; k < heuristic.buffers.size( ) && k < scened.buffers.size( ); ++k )
        {
            const model_buffer_t& a = heuristic.buffers[ k ];
            const model_buffer_t& b = scened.buffers[ k ];
            content += a.vertices.size( ) != b.vertices.size( ) || a.indices != b.indices || a.skin.size( ) != b.skin.size( )
                || a.authored != b.authored
                || std::memcmp( a.vertices.data( ), b.vertices.data( ), a.vertices.size( ) * sizeof( mesh_vertex_t ) ) != 0
                || std::memcmp( a.skin.data( ), b.skin.data( ), a.skin.size( ) * sizeof( model_skin_t ) ) != 0;
        }
        LOG_INFO( "scene-compare %s ok %d parts %d / %d buffers %d / %d: geo %d tex %d detail %d cube %d constants %d alpha %d node %d content %d", asset.c_str( ), sok ? 1 : 0,
            static_cast< int >( heuristic.parts.size( ) ), static_cast< int >( scened.parts.size( ) ), static_cast< int >( heuristic.buffers.size( ) ),
            static_cast< int >( scened.buffers.size( ) ), geo, tex, det, cube, con, alpha, node, content );
    }
    // The scene's aabb, for the static model component's world aabb (static_aabb_for).
    if ( loaded_ok && loaded.scened )
    {
        std::array< float, 8 > box{};
        std::memcpy( box.data( ), loaded.aabb, sizeof( loaded.aabb ) );
        m_scene_aabbs[ asset ] = box;
    }
    if ( !loaded_ok )
    {
        bytes.clear( );
        loaded = {};
        loaded_ok = m_archive.read( animated_name.c_str( ), bytes ) && load_model( bytes, loaded );
        // The scene VuAnimatedModelAsset's load reads (0x5322C0's scene_load_gfx with the flag 1): each part's shader gives its
        // sort type, as load_model_scene does for a static model. No mesh aabb here: the depth takes the node's origin (ours).
        if ( loaded_ok )
        {
            scene_t scene;
            scene_reader_t reader{ };
            reader.data = bytes.data( );
            reader.size = bytes.size( );
            if ( scene_load_gfx( reader, scene, 1, false ) )
                for ( model_part_t& part : loaded.parts )
                    if ( part.shader >= 0 && static_cast< std::size_t >( part.shader ) < scene.shaders.size( ) )
                    {
                        const scene_shader_t& shader = scene.shaders[ static_cast< std::size_t >( part.shader ) ];
                        part.layer = static_cast< int >( shader.mode );
                        part.shading = shader.name == "GenericVertexColor" ? 1 : 0;
                        for ( const scene_constant_t& c : shader.tables[ 0 ].constants )
                            if ( c.name == "DiffuseAlpha" && c.type == 1 )
                                std::memcpy( &part.diffuse_alpha, c.value, 4 );
                        if ( part.layer >= 5 )
                            LOG_INFO( "model %s: part's shader %s, sort type %d", asset.c_str( ), shader.name.c_str( ), part.layer );
                    }
        }
    }
    if ( !loaded_ok )
    {
        LOG_ERROR( "model %s", asset.c_str( ) );
        return m_models.emplace( asset, std::move( cached ) ).first->second;
    }

    std::vector< int > geometries( loaded.buffers.size( ), -1 );
    for ( std::size_t index = 0; index < loaded.buffers.size( ); ++index )
    {
        const model_buffer_t& buffer = loaded.buffers[ index ];
        geometries[ index ] = c_engine::geometry(
            buffer.vertices.data( ),
            static_cast< int >( buffer.vertices.size( ) ),
            buffer.indices.data( ),
            static_cast< int >( buffer.indices.size( ) )
        );
    }
    for ( const model_part_t& part : loaded.parts )
    {
        cached_part_t item{};
        item.geometry = geometries[ static_cast< std::size_t >( part.buffer ) ];
        item.texture = texture_for( part.texture );
        item.detail = part.detail.empty( ) ? -1 : texture_for( part.detail );
        item.index_start = part.index_start;
        item.index_count = part.index_count;
        item.alpha_test = part.alpha_test;
        item.shader = part.shader;
        item.layer = part.layer;
        item.shading = part.shading;
        item.diffuse_alpha = part.diffuse_alpha;
        item.has_box = part.has_box;
        std::memcpy( item.box, part.box, sizeof( item.box ) );
        std::memcpy( item.node, part.node, sizeof( item.node ) );
        item.specular[ 0 ] = part.specular[ 0 ];
        item.specular[ 1 ] = part.specular[ 1 ];
        item.specular[ 2 ] = part.specular[ 2 ];
        item.diffuse[ 0 ] = part.diffuse[ 0 ];
        item.diffuse[ 1 ] = part.diffuse[ 1 ];
        item.diffuse[ 2 ] = part.diffuse[ 2 ];
        item.specular_amount = part.specular_amount;
        item.environment_amount = part.environment_amount;
        item.cube = part.cube;
        cached.push_back( item );
    }
    // Report the authored shading constants once per model. These are the
    // numbers the hull pass must use; they come from the asset's material
    // table, never from values typed into the shader, or the next boat
    // with different ones would be silently wrong.
    for ( const model_part_t& part : loaded.parts )
    {
        if ( part.environment_amount <= 0.0f && part.specular_amount <= 0.0f )
            continue;
        LOG_SUCCESS( "material %-30s diffuse %.2f %.2f %.2f  specular %.2f %.2f %.2f x%.2f  env %.2f cube %s",
            part.texture.c_str( ),
            part.diffuse[ 0 ], part.diffuse[ 1 ], part.diffuse[ 2 ],
            part.specular[ 0 ], part.specular[ 1 ], part.specular[ 2 ],
            part.specular_amount, part.environment_amount,
            part.cube.empty( ) ? "-" : part.cube.c_str( ) );
    }

    // Keep the skeleton: the clip loader checks its bone count against
    // this, and the skinned draw path will need the bind pose.
    if ( !loaded.bones.empty( ) )
        m_skeletons.emplace( asset, loaded.bones );

    // Keep the bind mesh for anything carrying a per-vertex binding.
    for ( std::size_t index = 0; index < loaded.buffers.size( ); ++index )
    {
        const model_buffer_t& buffer = loaded.buffers[ index ];
        if ( buffer.skin.empty( ) || buffer.skin.size( ) != buffer.vertices.size( ) )
            continue;
        skinned_mesh_t mesh{};
        mesh.bind = buffer.vertices;
        mesh.skin = buffer.skin;
        mesh.indices = buffer.indices;
        for ( const cached_part_t& part : cached )
        {
            mesh.texture = part.texture;
            mesh.detail = part.detail;
            mesh.alpha_test = part.alpha_test;
            break;
        }
        m_skinned.emplace( asset, std::move( mesh ) );
        break;
    }
    LOG_SUCCESS( "model %s  %u parts", asset.c_str( ), static_cast< unsigned >( cached.size( ) ) );
    return m_models.emplace( asset, std::move( cached ) ).first->second;
}

// The level's VuFoliageEntity (world/foliage.hxx): properties, transform (+0x80 the position, +0xB0 / +0xB8 the scale), slot 6
// 0x5E7000's bucket. Ours, flagged: each bucket is one streamed mesh with one placement, drawn in the opaque pass with the same
// state as retail's type 4 (blend off, depth write on); retail writes the quads into the sort's memory (flush 0x5BBAA0 ->
// callback 0x5BBDD0). The texture is the id texture_for answers + 1 (0: the property holds none).
inline void c_world::foliage_build( const entity_t& root )
{
    const auto walk = [ & ]( const auto& self, const entity_t& e ) -> void
    {
        if ( e.m_type == "VuFoliageEntity" && e.m_state )
        {
            const foliage_state_t& s = *static_cast< const foliage_state_t* >( e.m_state.get( ) );
            const transform_component_t* t = entity_transform( const_cast< entity_t& >( e ) );
            if ( t && !e.m_world_unknown )
            {
                foliage_entity_t f;
                std::memcpy( f.m_position, t->world + 12, 16 );
                f.m_scale_x = t->scale[ 0 ];
                f.m_scale_z = t->scale[ 2 ];
                f.m_fog = s.fog;
                f.m_color = s.color;
                std::memcpy( f.m_uv, s.uv, 16 );
                f.m_draw_distance = s.draw_distance;
                if ( !s.texture.empty( ) )
                    f.m_texture = static_cast< std::uint32_t >( texture_for( s.texture ) + 1 );
                f.transform_changed( );
                m_foliage_entities.push_back( f );
            }
        }
        for ( const auto& child : e.m_children )
            self( self, *child );
    };
    walk( walk, root );
    for ( foliage_entity_t& f : m_foliage_entities )
        f.game_initialize( m_foliage );
    const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    for ( int l = 0; l < 2; ++l )
        for ( foliage_bucket_t& b : m_foliage.m_lists[ l ] )
        {
            foliage_draw_t d;
            d.bucket = &b;
            d.capacity = b.m_references;
            d.scratch.resize( static_cast< std::size_t >( d.capacity ) * 4u );
            std::vector< unsigned short > indices;
            for ( int q = 0; q < d.capacity; ++q )
                for ( const int corner : { 0, 1, 2, 2, 3, 0 } )
                    indices.push_back( static_cast< unsigned short >( q * 4 + corner ) );
            d.geometry = c_engine::geometry( d.scratch.data( ), static_cast< int >( d.scratch.size( ) ), indices.data( ), static_cast< int >( indices.size( ) ) );
            c_engine::place( d.geometry, static_cast< int >( b.m_texture ) - 1, 0, static_cast< int >( indices.size( ) ), identity, nullptr, l ? 13 : 12, nullptr, -1 );
            m_foliage_draws.push_back( std::move( d ) );
        }
    LOG_INFO( "foliage: %d VuFoliageEntity, %d buckets", static_cast< int >( m_foliage_entities.size( ) ), static_cast< int >( m_foliage_draws.size( ) ) );
}

// 0x5E71E0 on each entity, then the quads' vertices 0x5BBE30 tinted as the flush 0x5BBAA0 does: the tint is the level's "Foliage
// Tint" as loaded. Ours, flagged: whether [ebx + 0xA8] follows the blended settings is unread. A bucket's unused quads collapse.
inline void c_world::foliage_frame( )
{
    if ( m_foliage_draws.empty( ) )
        return;
    for ( foliage_entity_t& f : m_foliage_entities )
        f.draw( m_eye, m_eye );
    const float k = 0.0039215689f;   // 0x6F75BC
    const float tint[ 3 ] = { static_cast< float >( m_foliage_tint & 0xFFu ) * k, static_cast< float >( ( m_foliage_tint >> 8 ) & 0xFFu ) * k,
                              static_cast< float >( ( m_foliage_tint >> 16 ) & 0xFFu ) * k };
    for ( foliage_draw_t& d : m_foliage_draws )
    {
        int count = 0;
        mesh_vertex_t* out = c_engine::edit( d.geometry, count );
        if ( !out )
            continue;
        std::memset( out, 0, static_cast< std::size_t >( count ) * sizeof( mesh_vertex_t ) );
        int at = 0;
        for ( const foliage_quad_t& q : d.bucket->m_quads )
        {
            std::uint8_t bytes[ 0x60 ];
            foliage_vertices( q, tint, bytes );
            for ( int v = 0; v < 4; ++v, ++at )
            {
                const std::uint8_t* p = bytes + v * 0x18;
                mesh_vertex_t& o = out[ at ];
                std::memcpy( o.position, p, 12 );
                o.normal[ 2 ] = 1.0f;
                float uv[ 2 ];
                std::memcpy( uv, p + 12, 8 );
                o.uv[ 0 ] = uv[ 0 ];
                o.uv[ 1 ] = 1.0f - uv[ 1 ];   // the Foliage vertex shader's ( u, 1 - v )
                for ( int c = 0; c < 4; ++c )
                    o.color[ c ] = static_cast< float >( p[ 20 + c ] ) * k;
            }
        }
        d.bucket->m_quads.clear( );
        c_engine::stream( d.geometry );
    }
}

// A dynamic prop's Build tick 0x546130 (world/session.hxx race_session_dyn_prop_build) on its static model
// component: the draw matrix +0x13C = transform_draw_matrix (fromScale(+0xB0) x world +0x50), the world aabb by
// 0x531D10 (scene_component_aabb) from the LOD 0 instance's scene aabb (its +0x5C) or the zero aabb 0x7593F0 - as the
// load placed it (entities.hxx); then OUR renderer hand-off (the draw manager's, a hook): every placement moved to
// the matrix x its part node. Nothing for an owner with no static draw or no modelled transform.
inline void c_world::prop_draw( const entity_t& owner )
{
    const transform_component_t* t = entity_transform( const_cast< entity_t& >( owner ) );
    if ( !t || owner.m_world_unknown )
        return;
    for ( static_draw_item_t& item : m_static_draws )
    {
        if ( item.owner != &owner )
            continue;
        float matrix[ 16 ];
        transform_draw_matrix( *t, matrix );
        const auto box = m_scene_aabbs.find( item.draw.model_asset );
        scene_component_aabb( item.aabb, box != m_scene_aabbs.end( ) ? box->second.data( ) : scene_zero_aabb( ), matrix, false );
        if ( item.retail )
        {
            std::memcpy( item.retail->m_matrix, matrix, 64 );
            std::memcpy( item.retail->m_min, item.aabb, 32 );
        }
    }
}
