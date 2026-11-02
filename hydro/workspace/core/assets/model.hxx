#pragma once

#include <impl/engine/engine.hxx>
#include <workspace/utility/math.hxx>
#include <workspace/core/assets/scene.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Cooked PC static model, read the way VuGfxStaticScene does. Each mesh is a
// name, a subpart count, and one record per subpart. The scene-node tree
// after the vertex chunks carries the matrix that places that mesh.
// Per-vertex skin binding, present only on stride-36 buffers: four weight
// bytes at offset 28 then four bone index bytes at 32. Weights are stored
// normalised to sum to 1.
struct model_skin_t
{
    std::uint8_t bone[ 4 ]{};
    float weight[ 4 ]{};
};

struct model_buffer_t
{
    std::vector< mesh_vertex_t > vertices;
    std::vector< unsigned short > indices;
    std::vector< model_skin_t > skin;
    bool authored = false;
};

struct model_part_t
{
    int buffer = 0;
    // The mesh part's shader index (the stream's second word, VuGfxSceneMeshPart +0x14): which material - and so
    // which skin material (world/livery.hxx) - draws it.
    int shader = -1;
    int index_start = 0;
    int index_count = 0;
    std::string texture;
    std::string detail;
    int alpha_test = 0;
    // load_model_scene: the shader's sort type +0x20 (-1: not read), 1 for GenericVertexColor's unlit colour, DiffuseAlpha, the mesh's aabb.
    int layer = -1;
    int shading = 0;
    float diffuse_alpha = 1.0f;
    bool has_box = false;
    float box[ 8 ]{};
    float node[ 16 ]{};
    // Authored shading constants from the material table, which lives in the
    // model file outside every DXBC chunk. The shader bytecode declares these
    // names with no defaults, so the values have to come from here.
    std::string cube;
    float diffuse[ 3 ]{ 1.0f, 1.0f, 1.0f };
    float specular[ 3 ]{ 1.0f, 1.0f, 1.0f };
    float specular_amount = 0.0f;
    float environment_amount = 0.0f;
};

// One entry of a skinned model's bone block. Hull vertices bind rigidly to a
// single bone, so the pose is a plain parent-chain compose with no blending.
struct model_bone_t
{
    std::string name;
    int parent = -1;
    float position[ 3 ]{};
    float rotation[ 4 ]{ 0.0f, 0.0f, 0.0f, 1.0f };
    float scale[ 3 ]{ 1.0f, 1.0f, 1.0f };
    float world[ 16 ]{};
    float inverse[ 16 ]{};
};

struct model_t
{
    std::vector< model_buffer_t > buffers;
    std::vector< model_part_t > parts;
    std::vector< model_bone_t > bones;
    // load_model_scene only: the VuGfxStaticScene's aabb (+0x5C, 0x560930's block) - what the static model
    // component's world aabb (0x531D10) measures.
    bool scened = false;
    float aabb[ 8 ]{};
};

namespace
{
    std::uint32_t model_u32( const std::uint8_t* data )
    {
        return static_cast< std::uint32_t >( data[ 0 ] )
            | ( static_cast< std::uint32_t >( data[ 1 ] ) << 8 )
            | ( static_cast< std::uint32_t >( data[ 2 ] ) << 16 )
            | ( static_cast< std::uint32_t >( data[ 3 ] ) << 24 );
    }

    float model_f32( const std::uint8_t* data )
    {
        float value = 0.0f;
        std::memcpy( &value, data, sizeof( value ) );
        return value;
    }

    // Column-major to match the rest of the engine: basis in 0..10, the
    // translation in 12..14. Scale multiplies each basis column.
    void model_compose( float* out, const float* position, const float* rotation, const float* scale )
    {
        const float x = rotation[ 0 ];
        const float y = rotation[ 1 ];
        const float z = rotation[ 2 ];
        const float w = rotation[ 3 ];

        out[ 0 ] = ( 1.0f - 2.0f * ( y * y + z * z ) ) * scale[ 0 ];
        out[ 1 ] = ( 2.0f * ( x * y + z * w ) ) * scale[ 0 ];
        out[ 2 ] = ( 2.0f * ( x * z - y * w ) ) * scale[ 0 ];
        out[ 3 ] = 0.0f;

        out[ 4 ] = ( 2.0f * ( x * y - z * w ) ) * scale[ 1 ];
        out[ 5 ] = ( 1.0f - 2.0f * ( x * x + z * z ) ) * scale[ 1 ];
        out[ 6 ] = ( 2.0f * ( y * z + x * w ) ) * scale[ 1 ];
        out[ 7 ] = 0.0f;

        out[ 8 ] = ( 2.0f * ( x * z + y * w ) ) * scale[ 2 ];
        out[ 9 ] = ( 2.0f * ( y * z - x * w ) ) * scale[ 2 ];
        out[ 10 ] = ( 1.0f - 2.0f * ( x * x + y * y ) ) * scale[ 2 ];
        out[ 11 ] = 0.0f;

        out[ 12 ] = position[ 0 ];
        out[ 13 ] = position[ 1 ];
        out[ 14 ] = position[ 2 ];
        out[ 15 ] = 1.0f;
    }

    // Affine inverse of a column-major transform: invert the 3x3 basis by
    // adjugate over determinant, then carry the translation through it.
    // Needed for the bind pose, since the skinning matrix is
    // animatedWorld * inverse(bindWorld).
    bool model_invert( float* out, const float* m )
    {
        const float a = m[ 0 ], b = m[ 4 ], c = m[ 8 ];
        const float d = m[ 1 ], e = m[ 5 ], f = m[ 9 ];
        const float g = m[ 2 ], h = m[ 6 ], i = m[ 10 ];

        const float A = e * i - f * h;
        const float B = f * g - d * i;
        const float C = d * h - e * g;
        const float determinant = a * A + b * B + c * C;
        if ( std::fabs( determinant ) < 1.0e-12f )
        {
            math_identity( out );
            return false;
        }
        const float scale = 1.0f / determinant;

        // Store column-major: out[col * 4 + row] = inverse[row][col]. Writing
        // the adjugate row-major instead transposes the result, which is
        // invisible on a diagonal basis and doubles the angle on a rotated
        // one - for a rotation R it stores R rather than R^T, so world *
        // stored becomes R*R. That bug moved 290 of 6024 hull vertices,
        // exactly the ones bound to the few bones with a real rotation.
        out[ 0 ] = A * scale;
        out[ 1 ] = B * scale;
        out[ 2 ] = C * scale;
        out[ 3 ] = 0.0f;
        out[ 4 ] = ( c * h - b * i ) * scale;
        out[ 5 ] = ( a * i - c * g ) * scale;
        out[ 6 ] = ( b * g - a * h ) * scale;
        out[ 7 ] = 0.0f;
        out[ 8 ] = ( b * f - c * e ) * scale;
        out[ 9 ] = ( c * d - a * f ) * scale;
        out[ 10 ] = ( a * e - b * d ) * scale;
        out[ 11 ] = 0.0f;

        const float tx = m[ 12 ], ty = m[ 13 ], tz = m[ 14 ];
        out[ 12 ] = -( out[ 0 ] * tx + out[ 4 ] * ty + out[ 8 ] * tz );
        out[ 13 ] = -( out[ 1 ] * tx + out[ 5 ] * ty + out[ 9 ] * tz );
        out[ 14 ] = -( out[ 2 ] * tx + out[ 6 ] * ty + out[ 10 ] * tz );
        out[ 15 ] = 1.0f;
        return true;
    }

    bool model_shape_name( const std::uint8_t* data, std::size_t size, std::size_t at, std::size_t& end )
    {
        if ( at >= size || data[ at ] != '|' )
            return false;
        end = at;
        while ( end < size && end - at < 180 && data[ end ] != 0 )
        {
            if ( data[ end ] < 32 || data[ end ] >= 127 )
                return false;
            ++end;
        }
        if ( end >= size || data[ end ] != 0 || end - at < 8 )
            return false;
        const std::string text( reinterpret_cast< const char* >( data + at ), end - at );
        if ( text.find( "Shape" ) == std::string::npos )
            return false;
        ++end;
        return true;
    }

    int model_alpha( const std::string& path, bool one_bit )
    {
        if ( path.find( "Waterfall" ) != std::string::npos )
            return 7;
        if ( one_bit )
            return 1;
        static const char* words[ ] = { "Foliage", "Fern", "Plant", "Palm", "Grass", "Leaf", "Fishing" };
        for ( const char* word : words )
        {
            if ( path.find( word ) != std::string::npos )
                return 1;
        }
        return 0;
    }

    // map1 / map2 are stored as a key, padding nulls, then the asset path.
    std::string model_keyed( const std::uint8_t* data, std::size_t begin, std::size_t end, const char* key )
    {
        const std::size_t length = std::strlen( key );
        for ( std::size_t index = begin; index + length < end; ++index )
        {
            if ( std::memcmp( data + index, key, length ) != 0 || data[ index + length ] != 0 )
                continue;
            std::size_t cursor = index + length;
            while ( cursor < end && data[ cursor ] == 0 )
                ++cursor;
            std::string path;
            while ( cursor < end && data[ cursor ] >= 32 && data[ cursor ] < 127 )
                path.push_back( static_cast< char >( data[ cursor++ ] ) );
            if ( path.find( '/' ) != std::string::npos )
                return path;
        }
        return {};
    }

    // A shader constant in the material table: name, NUL, a u32 type tag,
    // then that many floats. Tag 2 is a float3, tag 1 a float1, tag 0 a bool
    // with no payload. Verified against Vector_lod1's table at 0x219f, where
    // DiffuseColor and SpecularColor are tag 2 = (1,1,1), EnvironmentAmount is
    // tag 1 = 0.7 and SpecularAmount is tag 1 = 2.0.
    //
    // These live in the MODEL file but OUTSIDE every DXBC chunk - the shader
    // bytecode declares the names with no defaults, and the values sit in this
    // separate table, which is why reading only the RDEF found nothing.
    // `tag` is the stored type and `count` the floats it carries - they are
    // NOT the same number. Tag 2 is a float3 and tag 1 a float1; conflating
    // them copied only two floats of every colour and left the third at its
    // default. That was invisible on the boat, whose DiffuseColor really is
    // (1,1,1) and so matched the default exactly, and showed up only on a prop
    // with a genuine tint: Car_SUVa reads (0.8000, 0.8588, 0.9176).
    bool model_constant( const std::uint8_t* data, std::size_t begin, std::size_t end,
        const char* key, int tag_wanted, int count, float* out )
    {
        // Search BACKWARD from the marker so the CLOSEST preceding entry wins.
        // Scanning forward from the window start picked up the previous
        // material's constants instead - every prop came back with the same
        // implausible specular and a 1.00 amount, which is what gave it away.
        const std::size_t length = std::strlen( key );
        if ( end < begin + length + 8 )
            return false;
        for ( std::size_t index = end - length - 8; index + 1 > begin; --index )
        {
            if ( std::memcmp( data + index, key, length ) != 0 || data[ index + length ] != 0 )
                continue;
            std::uint32_t tag = 0;
            std::memcpy( &tag, data + index + length + 1, sizeof( tag ) );
            if ( static_cast< int >( tag ) != tag_wanted )
                continue;
            if ( index + length + 5 + sizeof( float ) * static_cast< std::size_t >( count ) > end )
                continue;
            std::memcpy( out, data + index + length + 5, sizeof( float ) * static_cast< std::size_t >( count ) );
            return true;
        }
        return false;
    }

    // EnvironmentTexture names its cube with no UV-set key in between, unlike
    // DiffuseTexture which carries "map1" first. Scan forward for the next
    // printable run rather than assuming a fixed gap.
    std::string model_named( const std::uint8_t* data, std::size_t begin, std::size_t end, const char* key )
    {
        const std::size_t length = std::strlen( key );
        if ( end < begin + length )
            return {};
        for ( std::size_t index = end - length; index + 1 > begin; --index )
        {
            if ( std::memcmp( data + index, key, length ) != 0 || data[ index + length ] != 0 )
                continue;
            std::size_t cursor = index + length + 1;
            const std::size_t limit = cursor + 24 < end ? cursor + 24 : end;
            while ( cursor < limit )
            {
                if ( data[ cursor ] >= 32 && data[ cursor ] < 127 )
                {
                    std::string name;
                    while ( cursor < end && data[ cursor ] >= 32 && data[ cursor ] < 127 )
                        name.push_back( static_cast< char >( data[ cursor++ ] ) );
                    if ( name.size( ) >= 3 )
                        return name;
                    break;
                }
                ++cursor;
            }
        }
        return {};
    }

    std::string model_path( const std::uint8_t* data, std::size_t begin, std::size_t end )
    {
        std::string current;
        for ( std::size_t index = begin; index < end; ++index )
        {
            const unsigned char byte = data[ index ];
            if ( byte >= 32 && byte < 127 )
            {
                current.push_back( static_cast< char >( byte ) );
                continue;
            }
            if ( current.find( '/' ) != std::string::npos )
                return current;
            current.clear( );
        }
        return {};
    }

    void model_normals( model_buffer_t& buffer )
    {
        std::vector< float > sum( buffer.vertices.size( ) * 3, 0.0f );
        for ( std::size_t index = 0; index + 2 < buffer.indices.size( ); index += 3 )
        {
            const unsigned short ia = buffer.indices[ index ];
            const unsigned short ib = buffer.indices[ index + 1 ];
            const unsigned short ic = buffer.indices[ index + 2 ];
            if ( ia >= buffer.vertices.size( ) || ib >= buffer.vertices.size( ) || ic >= buffer.vertices.size( ) )
                continue;
            const float* a = buffer.vertices[ ia ].position;
            const float* b = buffer.vertices[ ib ].position;
            const float* c = buffer.vertices[ ic ].position;
            const float e1[ 3 ] = { b[ 0 ] - a[ 0 ], b[ 1 ] - a[ 1 ], b[ 2 ] - a[ 2 ] };
            const float e2[ 3 ] = { c[ 0 ] - a[ 0 ], c[ 1 ] - a[ 1 ], c[ 2 ] - a[ 2 ] };
            const float n[ 3 ] = {
                e1[ 1 ] * e2[ 2 ] - e1[ 2 ] * e2[ 1 ],
                e1[ 2 ] * e2[ 0 ] - e1[ 0 ] * e2[ 2 ],
                e1[ 0 ] * e2[ 1 ] - e1[ 1 ] * e2[ 0 ]
            };
            for ( unsigned short vertex : { ia, ib, ic } )
            {
                sum[ vertex * 3 ] += n[ 0 ];
                sum[ vertex * 3 + 1 ] += n[ 1 ];
                sum[ vertex * 3 + 2 ] += n[ 2 ];
            }
        }

        for ( std::size_t vertex = 0; vertex < buffer.vertices.size( ); ++vertex )
        {
            float* normal = buffer.vertices[ vertex ].normal;
            normal[ 0 ] = sum[ vertex * 3 ];
            normal[ 1 ] = sum[ vertex * 3 + 1 ];
            normal[ 2 ] = sum[ vertex * 3 + 2 ];
            const float length = std::sqrt( normal[ 0 ] * normal[ 0 ] + normal[ 1 ] * normal[ 1 ] + normal[ 2 ] * normal[ 2 ] );
            if ( length > 1.0e-6f )
            {
                normal[ 0 ] /= length;
                normal[ 1 ] /= length;
                normal[ 2 ] /= length;
            }
            else
            {
                normal[ 0 ] = 0.0f;
                normal[ 1 ] = 0.0f;
                normal[ 2 ] = 1.0f;
            }
        }
    }

    struct model_pose_t
    {
        float matrix[ 16 ]{};
    };

    // VuGfxSceneNode: name, 64-byte matrix, optional mesh index, then children.
    // The matrix is the mesh's transform. Parents multiply in, the same way the
    // entity transform multiplies the node's.
    bool model_scene_node(
        const std::uint8_t* data,
        std::size_t size,
        std::size_t& cursor,
        const float* parent,
        std::uint32_t mesh_count,
        std::vector< std::vector< model_pose_t > >& poses,
        int& order,
        bool trace )
    {
        if ( cursor >= size )
            return false;
        std::size_t end = cursor;
        while ( end < size && data[ end ] != 0 )
        {
            if ( data[ end ] < 32 || data[ end ] >= 127 || end - cursor > 180 )
                return false;
            ++end;
        }
        if ( end >= size )
            return false;
        const int visit = order++;
        if ( trace )
        {
            const std::string label( reinterpret_cast< const char* >( data + cursor ), end - cursor );
            LOG_INFO( "node %d '%s'", visit, label.c_str( ) );
        }

        cursor = end + 1;
        if ( cursor + 65 > size )
        {
            if ( trace )
                LOG_INFO( "node %d reject: matrix-eof", visit );
            return false;
        }

        float local[ 16 ]{};
        std::memcpy( local, data + cursor, sizeof( local ) );
        if ( trace && cursor + 80 <= size )
        {
            const std::uint8_t* post = data + cursor + 64;
            char text[ 17 ]{};
            for ( int at = 0; at < 16; ++at )
                text[ at ] = ( post[ at ] >= 32 && post[ at ] < 127 ) ? static_cast< char >( post[ at ] ) : '.';
            LOG_INFO( "node %d post-matrix: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x | %s",
                visit,
                post[ 0 ], post[ 1 ], post[ 2 ], post[ 3 ], post[ 4 ], post[ 5 ], post[ 6 ], post[ 7 ],
                post[ 8 ], post[ 9 ], post[ 10 ], post[ 11 ], post[ 12 ], post[ 13 ], post[ 14 ], post[ 15 ],
                text );
        }
        cursor += 64;
        const std::uint8_t has_mesh = data[ cursor++ ];
        int mesh = -1;
        if ( has_mesh != 0 )
        {
            if ( cursor + 4 > size )
            {
                if ( trace )
                    LOG_INFO( "node %d reject: mesh-eof", visit );
                return false;
            }
            mesh = static_cast< int >( model_u32( data + cursor ) );
            cursor += 4;
            if ( mesh < 0 || static_cast< std::uint32_t >( mesh ) >= mesh_count )
            {
                if ( trace )
                    LOG_INFO( "node %d reject: mesh-range mesh %d of %u", visit, mesh, static_cast< unsigned >( mesh_count ) );
                return false;
            }
        }
        if ( trace )
            LOG_INFO( "node %d has_mesh %u mesh %d", visit, static_cast< unsigned >( has_mesh ), mesh );

        float world[ 16 ]{};
        math_multiply( world, parent, local );
        if ( mesh >= 0 )
        {
            model_pose_t pose{};
            std::memcpy( pose.matrix, world, sizeof( pose.matrix ) );
            poses[ static_cast< std::size_t >( mesh ) ].push_back( pose );
        }

        if ( cursor + 4 > size )
        {
            if ( trace )
                LOG_INFO( "node %d reject: children-eof", visit );
            return false;
        }
        const std::uint32_t children = model_u32( data + cursor );
        cursor += 4;
        if ( children > 4096 )
        {
            if ( trace )
                LOG_INFO( "node %d reject: children-count %u", visit, static_cast< unsigned >( children ) );
            return false;
        }
        if ( trace )
            LOG_INFO( "node %d children %u", visit, static_cast< unsigned >( children ) );
        for ( std::uint32_t child = 0; child < children; ++child )
        {
            if ( !model_scene_node( data, size, cursor, world, mesh_count, poses, order, trace ) )
                return false;
        }
        return true;
    }
}

inline bool load_model( const std::vector< std::uint8_t >& blob, model_t& model )
{
    model = {};
    if ( blob.size( ) < 128 )
        return false;

    struct record_t
    {
        std::uint32_t buffer = 0;
        std::uint32_t material = 0;
        std::uint32_t vertex_start = 0;
        std::uint32_t vertex_end = 0;
        std::uint32_t index_start = 0;
        std::uint32_t index_count = 0;
        std::uint32_t mesh = 0;
        std::string name;
    };

    std::size_t best_at = 0;
    std::size_t best_records = 0;
    std::vector< record_t > best;
    std::size_t best_end = 0;

    for ( std::size_t start = 0; start < blob.size( ); ++start )
    {
        if ( blob[ start ] != '|' )
            continue;

        std::vector< record_t > records;
        std::size_t cursor = start;
        std::uint32_t mesh_index = 0;
        while ( cursor < blob.size( ) && blob[ cursor ] == '|' )
        {
            std::size_t end = 0;
            if ( !model_shape_name( blob.data( ), blob.size( ), cursor, end ) || end + 28 > blob.size( ) )
                break;
            const std::uint32_t subparts = model_u32( blob.data( ) + end );
            if ( subparts < 1 || subparts > 8 )
                break;
            const std::size_t record = static_cast< std::size_t >( 92 + ( subparts - 1 ) * 56 );
            if ( end + record > blob.size( ) )
                break;

            bool mesh_ok = true;
            std::vector< record_t > mesh_records;
            for ( std::uint32_t sub = 0; sub < subparts && mesh_ok; ++sub )
            {
                const std::size_t part_at = end + 4 + static_cast< std::size_t >( sub ) * 56;
                const std::uint32_t vertex_start = model_u32( blob.data( ) + part_at + 8 );
                const std::uint32_t vertex_end = model_u32( blob.data( ) + part_at + 12 );
                const std::uint32_t index_start = model_u32( blob.data( ) + part_at + 16 );
                const std::uint32_t triangles = model_u32( blob.data( ) + part_at + 20 );
                if ( triangles < 1 || triangles > 300000 || vertex_end < vertex_start || vertex_end >= 500000 || index_start >= 8000000 )
                {
                    mesh_ok = false;
                    break;
                }

                record_t item{};
                item.buffer = model_u32( blob.data( ) + part_at );
                item.material = model_u32( blob.data( ) + part_at + 4 );
                item.vertex_start = vertex_start;
                item.vertex_end = vertex_end;
                item.index_start = index_start;
                item.index_count = triangles * 3;
                item.mesh = mesh_index;
                item.name.assign( reinterpret_cast< const char* >( blob.data( ) + cursor ), end - cursor - 1 );
                mesh_records.push_back( std::move( item ) );
            }
            if ( !mesh_ok )
                break;
            for ( record_t& item : mesh_records )
                records.push_back( std::move( item ) );
            ++mesh_index;
            cursor = end + record;
        }

        if ( records.empty( ) || cursor + 12 > blob.size( ) )
            continue;
        const std::uint32_t buffer_count = model_u32( blob.data( ) + cursor );
        if ( buffer_count < 1 || buffer_count > 16 )
            continue;

        std::size_t probe = cursor + 4;
        bool valid = true;
        for ( std::uint32_t index = 0; index < buffer_count && valid; ++index )
        {
            if ( probe + 8 > blob.size( ) )
            {
                valid = false;
                break;
            }
            const std::uint32_t stride = model_u32( blob.data( ) + probe );
            const std::uint32_t bytes = model_u32( blob.data( ) + probe + 4 );
            probe += 8;
            if ( stride < 12 || stride > 64 || bytes == 0 || bytes % stride != 0 || probe + bytes + 4 > blob.size( ) )
            {
                valid = false;
                break;
            }
            const std::uint32_t vertices = bytes / stride;
            if ( vertices > 200000 )
            {
                valid = false;
                break;
            }
            probe += bytes;
            const std::uint32_t indices = model_u32( blob.data( ) + probe );
            probe += 4;
            if ( indices < 3 || indices > 700000 || probe + static_cast< std::size_t >( indices ) * 2 > blob.size( ) )
            {
                valid = false;
                break;
            }
            probe += static_cast< std::size_t >( indices ) * 2;
        }

        if ( valid && records.size( ) > best_records )
        {
            best_records = records.size( );
            best = std::move( records );
            best_at = start;
            best_end = probe;
            ( void )best_end;
        }
    }

    if ( best.empty( ) )
        return false;

    struct model_material_t
    {
        std::string texture;
        std::string detail;
        std::string cube;
        // Defaults match the shader's own behaviour when the table omits a
        // constant: white diffuse and specular, and NO specular or reflection
        // contribution. Absent must mean off, never a guessed strength.
        float diffuse[ 3 ]{ 1.0f, 1.0f, 1.0f };
        float specular[ 3 ]{ 1.0f, 1.0f, 1.0f };
        float specular_amount = 0.0f;
        float environment_amount = 0.0f;
        bool one_bit = false;
        bool blend = false;
    };
    std::vector< model_material_t > materials;
    std::size_t search = 0;
    const std::string marker = "gAlphaTestEnabled";
    const std::string one_bit_name = "OneBitAlphaTexture";
    while ( search + marker.size( ) < best_at )
    {
        if ( std::memcmp( blob.data( ) + search, marker.data( ), marker.size( ) ) != 0 )
        {
            ++search;
            continue;
        }
        const std::size_t window = search > 700 ? search - 700 : 0;
        model_material_t material{};
        material.texture = model_keyed( blob.data( ), window, search, "map1" );
        if ( material.texture.empty( ) )
            material.texture = model_path( blob.data( ), window, search );
        const std::string second = model_keyed( blob.data( ), window, search, "map2" );
        // The shader constants sit in the same window as the texture keys.
        model_constant( blob.data( ), window, search, "DiffuseColor", 2, 3, material.diffuse );
        model_constant( blob.data( ), window, search, "SpecularColor", 2, 3, material.specular );
        model_constant( blob.data( ), window, search, "SpecularAmount", 1, 1, &material.specular_amount );
        model_constant( blob.data( ), window, search, "EnvironmentAmount", 1, 1, &material.environment_amount );
        material.cube = model_named( blob.data( ), window, search, "EnvironmentTexture" );
        material.one_bit = false;
        for ( std::size_t index = window; index + one_bit_name.size( ) < search; ++index )
        {
            if ( std::memcmp( blob.data( ) + index, one_bit_name.data( ), one_bit_name.size( ) ) == 0 )
            {
                material.one_bit = true;
                break;
            }
        }
        std::size_t cursor = search + marker.size( );
        const std::size_t limit = cursor + 64 < blob.size( ) ? cursor + 64 : blob.size( );
        std::string shader;
        while ( cursor < limit )
        {
            const unsigned char byte = blob[ cursor ];
            if ( ( byte >= 'A' && byte <= 'Z' ) || ( byte >= 'a' && byte <= 'z' ) )
            {
                while ( cursor < blob.size( ) && blob[ cursor ] >= 32 && blob[ cursor ] < 127 )
                    shader.push_back( static_cast< char >( blob[ cursor++ ] ) );
                break;
            }
            ++cursor;
        }
        // Blend samples DiffuseTexture_A and DiffuseTexture_B. Vertex alpha
        // is the mix: the beach top is the second map, the skirt is the first.
        material.blend = shader == "Blend" && !second.empty( );
        if ( material.blend )
            material.detail = second;
        materials.push_back( std::move( material ) );
        search += marker.size( );
    }

    std::size_t cursor = best_at;
    while ( cursor < blob.size( ) && blob[ cursor ] == '|' )
    {
        std::size_t end = 0;
        if ( !model_shape_name( blob.data( ), blob.size( ), cursor, end ) )
            break;
        const std::uint32_t flag = model_u32( blob.data( ) + end );
        cursor = end + static_cast< std::size_t >( 92 + ( flag - 1 ) * 56 );
    }

    const std::uint32_t buffer_count = model_u32( blob.data( ) + cursor );
    cursor += 4;
    model.buffers.resize( buffer_count );
    bool skinned = false;
    float skin_low[ 3 ] = { 1.0e9f, 1.0e9f, 1.0e9f };
    float skin_high[ 3 ] = { -1.0e9f, -1.0e9f, -1.0e9f };
    for ( std::uint32_t index = 0; index < buffer_count; ++index )
    {
        const std::uint32_t stride = model_u32( blob.data( ) + cursor );
        const std::uint32_t bytes = model_u32( blob.data( ) + cursor + 4 );
        cursor += 8;
        const std::uint32_t vertices = bytes / stride;
        // Layout past offset 28, measured from the shipped assets:
        //   stride 32 - colour4
        //   stride 36 - skin binding: 4 weight bytes @28 then 4 bone index
        //               bytes @32. Hull vertices carry weight 255 on a single
        //               bone, so the binding is rigid rather than blended.
        //   stride 40 - a second float2 texture coordinate set @28 plus 4
        //               trailing bytes. Not skinning.
        if ( stride == 36 && vertices > 0 )
        {
            skinned = true;
            unsigned lowest = 255;
            unsigned highest = 0;
            unsigned multi = 0;
            bool seen[ 256 ]{};
            for ( std::uint32_t sample = 0; sample < vertices; ++sample )
            {
                const std::uint8_t* tail =
                    blob.data( ) + cursor + static_cast< std::size_t >( sample ) * stride + 28;
                if ( tail[ 1 ] != 0 || tail[ 2 ] != 0 || tail[ 3 ] != 0 )
                    ++multi;
                const unsigned bone = tail[ 4 ];
                seen[ bone ] = true;
                if ( bone < lowest )
                    lowest = bone;
                if ( bone > highest )
                    highest = bone;
            }
            unsigned distinct = 0;
            for ( unsigned bone = 0; bone < 256; ++bone )
                distinct += seen[ bone ] ? 1u : 0u;

            // Bounding box of the raw positions. If these already span hull
            // scale then the vertices are stored in bind-pose model space,
            // the current draw is the correct bind pose, and multiplying by
            // the bone world matrices would double-transform. Skinning would
            // then only matter once an animation displaces a bone.
            float low[ 3 ] = { 1.0e9f, 1.0e9f, 1.0e9f };
            float high[ 3 ] = { -1.0e9f, -1.0e9f, -1.0e9f };
            for ( std::uint32_t sample = 0; sample < vertices; ++sample )
            {
                const std::uint8_t* source =
                    blob.data( ) + cursor + static_cast< std::size_t >( sample ) * stride;
                for ( int axis = 0; axis < 3; ++axis )
                {
                    const float value = model_f32( source + axis * 4 );
                    if ( value < low[ axis ] )
                        low[ axis ] = value;
                    if ( value > high[ axis ] )
                        high[ axis ] = value;
                }
            }
            std::memcpy( skin_low, low, sizeof( skin_low ) );
            std::memcpy( skin_high, high, sizeof( skin_high ) );

            // Hull UVs measured clean: Vector spans u 0.004..0.994,
            // v 0.005..0.997 with 0 of 6024 vertices outside [0,1]. The
            // unwrap and the uv float2 @20 offset are both correct.
            LOG_INFO( "skin blob %u verts %u bones used %u range %u..%u multi-weight %u  bbox x %.2f..%.2f y %.2f..%.2f z %.2f..%.2f",
                static_cast< unsigned >( blob.size( ) ),
                static_cast< unsigned >( vertices ),
                distinct, lowest, highest, multi,
                low[ 0 ], high[ 0 ], low[ 1 ], high[ 1 ], low[ 2 ], high[ 2 ] );
        }
        model_buffer_t& buffer = model.buffers[ index ];
        buffer.vertices.resize( vertices );
        if ( stride == 36 )
        {
            buffer.skin.resize( vertices );
            for ( std::uint32_t vertex = 0; vertex < vertices; ++vertex )
            {
                const std::uint8_t* tail =
                    blob.data( ) + cursor + static_cast< std::size_t >( vertex ) * stride + 28;
                model_skin_t& bind = buffer.skin[ vertex ];
                float total = 0.0f;
                for ( int slot = 0; slot < 4; ++slot )
                {
                    bind.weight[ slot ] = static_cast< float >( tail[ slot ] );
                    bind.bone[ slot ] = tail[ 4 + slot ];
                    total += bind.weight[ slot ];
                }
                // Weights are bytes summing to 255; normalise so the skinned
                // position is a convex blend regardless of rounding.
                const float scale = total > 0.0f ? 1.0f / total : 0.0f;
                for ( int slot = 0; slot < 4; ++slot )
                    bind.weight[ slot ] *= scale;
            }
        }
        for ( std::uint32_t vertex = 0; vertex < vertices; ++vertex )
        {
            const std::uint8_t* source = blob.data( ) + cursor + static_cast< std::size_t >( vertex ) * stride;
            mesh_vertex_t& item = buffer.vertices[ vertex ];
            item.position[ 0 ] = model_f32( source );
            item.position[ 1 ] = model_f32( source + 4 );
            item.position[ 2 ] = model_f32( source + 8 );
            item.color[ 0 ] = item.color[ 1 ] = item.color[ 2 ] = item.color[ 3 ] = 1.0f;
            if ( stride >= 28 )
            {
                auto snorm = []( const std::uint8_t* bytes ) -> float
                {
                    const int value = static_cast< int >( static_cast< std::int16_t >(
                        static_cast< std::uint16_t >( bytes[ 0 ] ) | ( static_cast< std::uint16_t >( bytes[ 1 ] ) << 8 ) ) );
                    return static_cast< float >( value ) / 32767.0f;
                };
                item.normal[ 0 ] = snorm( source + 12 );
                item.normal[ 1 ] = snorm( source + 14 );
                item.normal[ 2 ] = snorm( source + 16 );
                item.uv[ 0 ] = model_f32( source + 20 );
                // V is flipped. The retail vertex shader's first instruction is
                // o0.xy = v2.xy * (1,-1) + (0,1), so the authored V runs the
                // other way from the sampler's. Done here rather than in the
                // shader because the water grid, the sky and the particles
                // generate their own UVs through the same pixel shader and
                // must not be flipped with the asset ones.
                item.uv[ 1 ] = 1.0f - model_f32( source + 24 );
                buffer.authored = true;
                if ( stride == 32 )
                {
                    item.color[ 0 ] = static_cast< float >( source[ 28 ] ) / 255.0f;
                    item.color[ 1 ] = static_cast< float >( source[ 29 ] ) / 255.0f;
                    item.color[ 2 ] = static_cast< float >( source[ 30 ] ) / 255.0f;
                    item.color[ 3 ] = static_cast< float >( source[ 31 ] ) / 255.0f;
                }
            }
            else
            {
                item.uv[ 0 ] = model_f32( source + 12 );
                item.uv[ 1 ] = 1.0f - model_f32( source + 16 );
            }
            if ( !std::isfinite( item.position[ 0 ] ) || !std::isfinite( item.position[ 1 ] ) || !std::isfinite( item.position[ 2 ] ) )
            {
                item.position[ 0 ] = item.position[ 1 ] = item.position[ 2 ] = 0.0f;
            }
        }
        cursor += bytes;
        const std::uint32_t indices = model_u32( blob.data( ) + cursor );
        cursor += 4;
        buffer.indices.resize( indices );
        std::memcpy( buffer.indices.data( ), blob.data( ) + cursor, static_cast< std::size_t >( indices ) * 2 );
        cursor += static_cast< std::size_t >( indices ) * 2;
    }

    for ( model_buffer_t& buffer : model.buffers )
    {
        if ( !buffer.authored )
            model_normals( buffer );
    }

    std::uint32_t mesh_count = 0;
    for ( const record_t& record : best )
    {
        if ( record.mesh + 1 > mesh_count )
            mesh_count = record.mesh + 1;
    }
    std::vector< std::vector< model_pose_t > > poses( mesh_count );
    bool posed = false;
    std::size_t node_end = 0;
    if ( mesh_count > 0 && cursor + 4 <= blob.size( ) )
    {
        std::size_t node_at = cursor;
        const std::uint32_t roots = model_u32( blob.data( ) + node_at );
        node_at += 4;
        if ( roots > 0 && roots <= 4096 )
        {
            float identity[ 16 ]{};
            math_identity( identity );
            bool ok = true;
            // A skinned model stores a bone block here instead of the static
            // scene-node tree:
            //
            //   u32          roots            bone count
            //   roots x 32   names            fixed width, zero padded
            //   roots x  4   parent indices   int32, -1 for a root
            //   roots x 48   transform        position f4, quaternion f4, scale f4
            //   32           trailer
            //
            // The size equality below is the discriminator - it holds exactly
            // on every skinned model measured and never on a static one, so
            // the static path is left untouched for the 81 of 84 models that
            // parse with it.
            const std::size_t names_at = cursor + 4;
            const std::size_t parents_at = names_at + static_cast< std::size_t >( roots ) * 32;
            const std::size_t bones_at = parents_at + static_cast< std::size_t >( roots ) * 4;
            const std::size_t block_end = bones_at + static_cast< std::size_t >( roots ) * 48 + 32;
            const bool bone_block = skinned && block_end == blob.size( );

            if ( bone_block )
            {
                model.bones.resize( roots );
                for ( std::uint32_t bone = 0; bone < roots; ++bone )
                {
                    model_bone_t& entry = model.bones[ bone ];
                    const char* label = reinterpret_cast< const char* >( blob.data( ) + names_at + bone * 32 );
                    const std::size_t span = ::strnlen( label, 32 );
                    entry.name.assign( label, span );
                    entry.parent = static_cast< int >( model_u32( blob.data( ) + parents_at + bone * 4 ) );
                    if ( entry.parent < -1 || entry.parent >= static_cast< int >( roots ) )
                        entry.parent = -1;

                    const std::uint8_t* trs = blob.data( ) + bones_at + bone * 48;
                    std::memcpy( entry.position, trs, sizeof( float ) * 3 );
                    std::memcpy( entry.rotation, trs + 16, sizeof( float ) * 4 );
                    std::memcpy( entry.scale, trs + 32, sizeof( float ) * 3 );
                }

                // The stored TRS is ALREADY ABSOLUTE, not parent-relative.
                // Measured by scoring bone world positions against the mesh
                // bounding box: taking the TRS directly puts 41/41 of Vector's
                // bones and 13/13 of TidalBlade's inside the hull, while
                // chaining through parents scores 20/41 and 1/13 because it
                // double-transforms - worse the deeper a bone sits. The parent
                // index is still parsed; animation will need the hierarchy
                // even though the bind pose does not.
                //
                // UNRESOLVED: the quaternion component order. A position-only
                // test cannot distinguish (x,y,z,w) from (w,x,y,z) because
                // rotation does not move a bone's own origin - both score
                // identically here. Settle it with a rotation-sensitive check
                // before relying on bone orientation.
                int singular = 0;
                for ( std::uint32_t bone = 0; bone < roots; ++bone )
                {
                    model_bone_t& entry = model.bones[ bone ];
                    model_compose( entry.world, entry.position, entry.rotation, entry.scale );
                    if ( !model_invert( entry.inverse, entry.world ) )
                        ++singular;
                }
                if ( singular != 0 )
                    LOG_WARNING( "skin %d of %u bind matrices are singular", singular, static_cast< unsigned >( roots ) );

                // Quaternion component order is (x, y, z, w) - measured, not
                // assumed. A skeleton has many unrotated bones, and identity
                // is (0,0,0,1) under xyzw but (1,0,0,0) under wxyz, which are
                // mutually exclusive: Vector reports 35 of 41 bones exactly
                // identity under xyzw and 0 under wxyz. Every quaternion is
                // unit length on both boats, which also confirms the field
                // offsets. A spread test would NOT have settled this, since
                // distance from a bone's own origin is rotation-invariant.
                const float slack = 0.25f;
                int within = 0;
                for ( const model_bone_t& entry : model.bones )
                {
                    const float* placed = entry.world + 12;
                    if ( placed[ 0 ] >= skin_low[ 0 ] - slack && placed[ 0 ] <= skin_high[ 0 ] + slack
                        && placed[ 1 ] >= skin_low[ 1 ] - slack && placed[ 1 ] <= skin_high[ 1 ] + slack
                        && placed[ 2 ] >= skin_low[ 2 ] - slack && placed[ 2 ] <= skin_high[ 2 ] + slack )
                    {
                        ++within;
                    }
                }
                if ( within != static_cast< int >( roots ) )
                {
                    LOG_WARNING( "skin bones %u: only %d inside the mesh box",
                        static_cast< unsigned >( roots ), within );
                }
                else
                {
                    LOG_SUCCESS( "skin bones %u all inside the mesh box", static_cast< unsigned >( roots ) );
                }
            }

            int order = 0;
            for ( std::uint32_t root = 0; root < roots && ok && !bone_block; ++root )
                ok = model_scene_node( blob.data( ), blob.size( ), node_at, identity, mesh_count, poses, order, false );
            // Requiring the tree to land exactly on the end of the blob threw
            // away every placement for animated models, which store their bone
            // block after it. Measured on a Hydroelectric load: 3 of 84 models
            // failed this way, including Boats/Vector_lod1 - the player hull -
            // and two that had already parsed 27 and 4 valid placements. A
            // tree that parses cleanly is trusted; trailing bytes are data we
            // do not consume yet, not corruption.
            posed = ok;
            node_end = node_at;
        }
    }

    ( void )node_end;
    // Diagnostic: node placements are only kept when the tree consumes the
    // blob exactly. Animated models may store bone data past the tree, which
    // would silently discard every placement and draw parts at identity.
    if ( !posed && mesh_count > 0 )
    {
        LOG_WARNING( "model node tree unparsed: meshes %u, tree starts %u of %u",
            static_cast< unsigned >( mesh_count ),
            static_cast< unsigned >( cursor ),
            static_cast< unsigned >( blob.size( ) ) );
    }

    if ( !posed )
        poses.assign( mesh_count, {} );

    for ( const record_t& record : best )
    {
        if ( record.buffer >= model.buffers.size( ) )
            continue;
        const model_buffer_t& buffer = model.buffers[ record.buffer ];
        if ( record.index_start + record.index_count > buffer.indices.size( ) )
            continue;

        const std::vector< model_pose_t >* placements = posed && record.mesh < poses.size( ) ? &poses[ record.mesh ] : nullptr;
        const int copies = placements != nullptr && !placements->empty( ) ? static_cast< int >( placements->size( ) ) : 1;
        for ( int copy = 0; copy < copies; ++copy )
        {
        model_part_t part{};
        math_identity( part.node );
        if ( placements != nullptr && !placements->empty( ) )
            std::memcpy( part.node, ( *placements )[ static_cast< std::size_t >( copy ) ].matrix, sizeof( part.node ) );
        part.buffer = static_cast< int >( record.buffer );
        part.shader = static_cast< int >( record.material );
        part.index_start = static_cast< int >( record.index_start );
        part.index_count = static_cast< int >( record.index_count );
        if ( record.material < materials.size( ) )
        {
            part.texture = materials[ record.material ].texture;
            part.detail = materials[ record.material ].detail;
            part.cube = materials[ record.material ].cube;
            part.specular_amount = materials[ record.material ].specular_amount;
            part.environment_amount = materials[ record.material ].environment_amount;
            std::memcpy( part.diffuse, materials[ record.material ].diffuse, sizeof( part.diffuse ) );
            std::memcpy( part.specular, materials[ record.material ].specular, sizeof( part.specular ) );
            part.alpha_test = model_alpha( part.texture, materials[ record.material ].one_bit );
            if ( materials[ record.material ].blend && part.alpha_test == 0 )
                part.alpha_test = 10;
        }
        else
            part.alpha_test = model_alpha( part.texture, false );

        float min_p[ 3 ] = { 1.0e9f, 1.0e9f, 1.0e9f };
        float max_p[ 3 ] = { -1.0e9f, -1.0e9f, -1.0e9f };
        for ( int index = 0; index < part.index_count; ++index )
        {
            const unsigned short vertex = buffer.indices[ static_cast< std::size_t >( part.index_start + index ) ];
            if ( vertex >= buffer.vertices.size( ) )
                continue;
            const float* position = buffer.vertices[ vertex ].position;
            for ( int axis = 0; axis < 3; ++axis )
            {
                if ( position[ axis ] < min_p[ axis ] )
                    min_p[ axis ] = position[ axis ];
                if ( position[ axis ] > max_p[ axis ] )
                    max_p[ axis ] = position[ axis ];
            }
        }
        bool duplicate = false;
        for ( const model_part_t& existing : model.parts )
        {
            if ( existing.texture != part.texture || existing.buffer != part.buffer )
                continue;
            if ( std::abs( existing.index_count - part.index_count ) > 32 )
                continue;
            const model_buffer_t& other = model.buffers[ static_cast< std::size_t >( existing.buffer ) ];
            float omin[ 3 ] = { 1.0e9f, 1.0e9f, 1.0e9f };
            float omax[ 3 ] = { -1.0e9f, -1.0e9f, -1.0e9f };
            for ( int index = 0; index < existing.index_count; ++index )
            {
                const unsigned short vertex = other.indices[ static_cast< std::size_t >( existing.index_start + index ) ];
                if ( vertex >= other.vertices.size( ) )
                    continue;
                const float* position = other.vertices[ vertex ].position;
                for ( int axis = 0; axis < 3; ++axis )
                {
                    if ( position[ axis ] < omin[ axis ] )
                        omin[ axis ] = position[ axis ];
                    if ( position[ axis ] > omax[ axis ] )
                        omax[ axis ] = position[ axis ];
                }
            }
            duplicate = true;
            for ( int axis = 0; axis < 3; ++axis )
            {
                if ( std::abs( omin[ axis ] - min_p[ axis ] ) > 0.05f || std::abs( omax[ axis ] - max_p[ axis ] ) > 0.05f )
                    duplicate = false;
            }
            for ( int element = 0; element < 16; ++element )
            {
                if ( std::fabs( existing.node[ element ] - part.node[ element ] ) > 0.01f )
                    duplicate = false;
            }
            if ( duplicate )
                break;
        }
        if ( duplicate )
            continue;
        model.parts.push_back( std::move( part ) );
        }
    }

    return !model.parts.empty( );
}

// Skinned lod1 vertices carry one bone index at byte 32. The name table sits
// immediately after the index buffer, 32 bytes per bone, in that same order.
// The attachment point is the average of the vertices bound to that bone.
inline bool model_bone( const std::vector< std::uint8_t >& blob, const char* bone, float out[ 3 ] )
{
    if ( blob.size( ) < 128 || bone == nullptr || bone[ 0 ] == 0 || out == nullptr )
        return false;

    std::size_t best_end = 0;
    int best_count = 0;
    for ( std::size_t start = 0; start < blob.size( ); ++start )
    {
        if ( blob[ start ] != '|' )
            continue;
        std::size_t cursor = start;
        int count = 0;
        while ( cursor < blob.size( ) && blob[ cursor ] == '|' )
        {
            std::size_t end = 0;
            if ( !model_shape_name( blob.data( ), blob.size( ), cursor, end ) || end + 28 > blob.size( ) )
                break;
            const std::uint32_t subparts = model_u32( blob.data( ) + end );
            if ( subparts < 1 || subparts > 8 )
                break;
            const std::size_t record = static_cast< std::size_t >( 92 + ( subparts - 1 ) * 56 );
            if ( end + record > blob.size( ) )
                break;
            bool mesh_ok = true;
            for ( std::uint32_t sub = 0; sub < subparts && mesh_ok; ++sub )
            {
                const std::size_t part_at = end + 4 + static_cast< std::size_t >( sub ) * 56;
                const std::uint32_t vertex_start = model_u32( blob.data( ) + part_at + 8 );
                const std::uint32_t vertex_end = model_u32( blob.data( ) + part_at + 12 );
                const std::uint32_t index_start = model_u32( blob.data( ) + part_at + 16 );
                const std::uint32_t triangles = model_u32( blob.data( ) + part_at + 20 );
                if ( triangles < 1 || triangles > 300000 || vertex_end < vertex_start || vertex_end >= 500000 || index_start >= 8000000 )
                    mesh_ok = false;
            }
            if ( !mesh_ok )
                break;
            cursor = end + record;
            ++count;
        }
        if ( count > best_count )
        {
            best_count = count;
            best_end = cursor;
        }
    }
    if ( best_count == 0 || best_end + 12 > blob.size( ) )
        return false;

    float sum[ 64 ][ 3 ]{};
    int hits[ 64 ]{};
    std::size_t cursor = best_end;
    const std::uint32_t buffer_count = model_u32( blob.data( ) + cursor );
    if ( buffer_count < 1 || buffer_count > 8 )
        return false;
    cursor += 4;
    for ( std::uint32_t index = 0; index < buffer_count; ++index )
    {
        if ( cursor + 8 > blob.size( ) )
            return false;
        const std::uint32_t stride = model_u32( blob.data( ) + cursor );
        const std::uint32_t bytes = model_u32( blob.data( ) + cursor + 4 );
        cursor += 8;
        if ( stride < 12 || stride > 64 || bytes == 0 || bytes % stride != 0 || cursor + bytes > blob.size( ) )
            return false;
        if ( stride >= 36 )
        {
            const std::uint32_t vertices = bytes / stride;
            for ( std::uint32_t vertex = 0; vertex < vertices; ++vertex )
            {
                const std::uint8_t* source = blob.data( ) + cursor + static_cast< std::size_t >( vertex ) * stride;
                const unsigned bone_index = source[ 32 ];
                if ( bone_index >= 64 )
                    continue;
                const float position[ 3 ] = {
                    model_f32( source ),
                    model_f32( source + 4 ),
                    model_f32( source + 8 )
                };
                if ( !std::isfinite( position[ 0 ] ) || !std::isfinite( position[ 1 ] ) || !std::isfinite( position[ 2 ] ) )
                    continue;
                sum[ bone_index ][ 0 ] += position[ 0 ];
                sum[ bone_index ][ 1 ] += position[ 1 ];
                sum[ bone_index ][ 2 ] += position[ 2 ];
                ++hits[ bone_index ];
            }
        }
        cursor += bytes;
        if ( cursor + 4 > blob.size( ) )
            return false;
        const std::uint32_t indices = model_u32( blob.data( ) + cursor );
        cursor += 4;
        if ( cursor + static_cast< std::size_t >( indices ) * 2 > blob.size( ) )
            return false;
        cursor += static_cast< std::size_t >( indices ) * 2;
    }
    if ( cursor + 4 > blob.size( ) )
        return false;
    const std::uint32_t bones = model_u32( blob.data( ) + cursor );
    cursor += 4;
    if ( bones == 0 || bones > 64 || cursor + static_cast< std::size_t >( bones ) * 32 > blob.size( ) )
        return false;
    for ( std::uint32_t index = 0; index < bones; ++index )
    {
        const char* name = reinterpret_cast< const char* >( blob.data( ) + cursor );
        if ( std::strcmp( name, bone ) == 0 && hits[ index ] > 0 )
        {
            out[ 0 ] = sum[ index ][ 0 ] / static_cast< float >( hits[ index ] );
            out[ 1 ] = sum[ index ][ 1 ] / static_cast< float >( hits[ index ] );
            out[ 2 ] = sum[ index ][ 2 ] / static_cast< float >( hits[ index ] );
            return true;
        }
        cursor += 32;
    }
    return false;
}

// One chunk's vertex and index bytes into a model_buffer_t - the RENDERER's unpack (retail hands these bytes to
// the device as they are; the layout is the shader's declaration). The same per-stride unpack load_model uses:
// position float3 @0, normal snorm16 x3 @12, uv float2 @20 (V flipped - the vertex shader's first instruction),
// colour bytes @28 on stride 32, the skin binding (weights @28, bones @32) on stride 36.
inline void model_read_chunk( const std::uint8_t* vertices, std::uint32_t stride, std::uint32_t bytes,
    const std::uint8_t* indices, std::uint32_t index_count, model_buffer_t& buffer )
{
    const std::uint32_t count = stride != 0 ? bytes / stride : 0;
    buffer.vertices.resize( count );
    if ( stride == 36 )
    {
        buffer.skin.resize( count );
        for ( std::uint32_t vertex = 0; vertex < count; ++vertex )
        {
            const std::uint8_t* tail = vertices + static_cast< std::size_t >( vertex ) * stride + 28;
            model_skin_t& bind = buffer.skin[ vertex ];
            float total = 0.0f;
            for ( int slot = 0; slot < 4; ++slot )
            {
                bind.weight[ slot ] = static_cast< float >( tail[ slot ] );
                bind.bone[ slot ] = tail[ 4 + slot ];
                total += bind.weight[ slot ];
            }
            const float scale = total > 0.0f ? 1.0f / total : 0.0f;
            for ( int slot = 0; slot < 4; ++slot )
                bind.weight[ slot ] *= scale;
        }
    }
    for ( std::uint32_t vertex = 0; vertex < count; ++vertex )
    {
        const std::uint8_t* source = vertices + static_cast< std::size_t >( vertex ) * stride;
        mesh_vertex_t& item = buffer.vertices[ vertex ];
        item.position[ 0 ] = model_f32( source );
        item.position[ 1 ] = model_f32( source + 4 );
        item.position[ 2 ] = model_f32( source + 8 );
        item.color[ 0 ] = item.color[ 1 ] = item.color[ 2 ] = item.color[ 3 ] = 1.0f;
        if ( stride >= 28 )
        {
            auto snorm = []( const std::uint8_t* b ) -> float
            {
                const int value = static_cast< int >( static_cast< std::int16_t >(
                    static_cast< std::uint16_t >( b[ 0 ] ) | ( static_cast< std::uint16_t >( b[ 1 ] ) << 8 ) ) );
                return static_cast< float >( value ) / 32767.0f;
            };
            item.normal[ 0 ] = snorm( source + 12 );
            item.normal[ 1 ] = snorm( source + 14 );
            item.normal[ 2 ] = snorm( source + 16 );
            item.uv[ 0 ] = model_f32( source + 20 );
            item.uv[ 1 ] = 1.0f - model_f32( source + 24 );
            buffer.authored = true;
            if ( stride == 32 )
                for ( int c = 0; c < 4; ++c )
                    item.color[ c ] = static_cast< float >( source[ 28 + c ] ) / 255.0f;
        }
        else
        {
            item.uv[ 0 ] = model_f32( source + 12 );
            item.uv[ 1 ] = 1.0f - model_f32( source + 16 );
        }
        if ( !std::isfinite( item.position[ 0 ] ) || !std::isfinite( item.position[ 1 ] ) || !std::isfinite( item.position[ 2 ] ) )
            item.position[ 0 ] = item.position[ 1 ] = item.position[ 2 ] = 0.0f;
    }
    buffer.indices.resize( index_count );
    std::memcpy( buffer.indices.data( ), indices, static_cast< std::size_t >( index_count ) * 2 );
    if ( !buffer.authored )
        model_normals( buffer );
}

// A VuStaticModelAsset through retail's scene load (scene.hxx scene_load_static) into the renderer's model_t:
// one buffer per chunk; one part per (mesh part, node instancing that mesh) - meshes in order, each part, then the
// instancing nodes in tree order - placed at the node's chained matrix (0x5C26F0's node x parent, through
// VuMatrix_multiply 0x4078C0 from the identity). The material is the part's shader's first table (the program's;
// 0x5929B0's first 0x541290 / 0x5414C0): the "map1" texture, the "map2" texture on a "Blend" shader (detail),
// "EnvironmentTexture" (the cube), DiffuseColor / SpecularColor (type 2, three floats) and SpecularAmount /
// EnvironmentAmount (type 1). RENDERER CONCERNS, NOT RETAIL (flagged): which table entries the draw reads (retail
// binds them through the pipeline state - not recreated), the alpha test (model_alpha's path rule), the paint rule.
inline bool load_model_scene( const std::vector< std::uint8_t >& blob, model_t& model )
{
    model = {};
    scene_t scene;
    if ( !scene_load_static( blob.data( ), blob.size( ), scene ) )
        return false;
    model.scened = true;
    std::memcpy( model.aabb, scene.aabb, sizeof( model.aabb ) );
    model.buffers.resize( scene.chunks.size( ) );
    for ( std::size_t k = 0; k < scene.chunks.size( ); ++k )
    {
        const scene_chunk_t& c = scene.chunks[ k ];
        model_read_chunk( blob.data( ) + c.vertices.offset, c.stride, c.vertices.size, blob.data( ) + c.index_bytes.offset,
            c.indices, model.buffers[ k ] );
    }
    // Each node's world: node x parent (roots x the identity), in tree order.
    std::vector< std::array< float, 16 > > world( scene.nodes.size( ) );
    const auto place = [ & ]( const auto& self, int node, const float* parent ) -> void
    {
        scene_multiply( scene.nodes[ node ].matrix, world[ node ].data( ), parent );
        for ( int child : scene.nodes[ node ].children )
            self( self, child, world[ node ].data( ) );
    };
    for ( int root : scene.roots )
        place( place, root, scene_identity( ) );
    for ( std::size_t mesh = 0; mesh < scene.meshes.size( ); ++mesh )
        for ( const scene_part_t& p : scene.meshes[ mesh ].parts )
            for ( std::size_t node = 0; node < scene.nodes.size( ); ++node )
            {
                if ( scene.nodes[ node ].mesh != static_cast< int >( mesh ) )
                    continue;
                const model_buffer_t& buffer = model.buffers[ p.chunk ];
                if ( static_cast< std::size_t >( p.first_index ) + 3u * p.triangles > buffer.indices.size( ) )
                    return false;
                model_part_t part{};
                std::memcpy( part.node, world[ node ].data( ), sizeof( part.node ) );
                part.buffer = static_cast< int >( p.chunk );
                part.shader = static_cast< int >( p.shader );
                part.index_start = static_cast< int >( p.first_index );
                part.index_count = static_cast< int >( 3u * p.triangles );
                const scene_shader_t& shader = scene.shaders[ p.shader ];
                const scene_table_t& table = shader.tables[ 0 ];
                std::string second;
                for ( const scene_texture_t& t : table.textures )
                {
                    if ( t.slot == "map1" && part.texture.empty( ) )
                        part.texture = t.asset;
                    else if ( t.slot == "map2" && second.empty( ) )
                        second = t.asset;
                    if ( t.name == "EnvironmentTexture" )
                        part.cube = t.asset;
                }
                for ( const scene_constant_t& c : table.constants )
                {
                    float v[ 3 ];
                    std::memcpy( v, c.value, sizeof( v ) );
                    if ( c.name == "DiffuseColor" && c.type == 2 )
                        std::memcpy( part.diffuse, v, sizeof( v ) );
                    else if ( c.name == "SpecularColor" && c.type == 2 )
                        std::memcpy( part.specular, v, sizeof( v ) );
                    else if ( c.name == "SpecularAmount" && c.type == 1 )
                        part.specular_amount = v[ 0 ];
                    else if ( c.name == "EnvironmentAmount" && c.type == 1 )
                        part.environment_amount = v[ 0 ];
                }
                part.layer = static_cast< int >( shader.mode );
                part.shading = shader.name == "GenericVertexColor" ? 1 : 0;
                for ( const scene_constant_t& c : table.constants )
                    if ( c.name == "DiffuseAlpha" && c.type == 1 )
                        std::memcpy( &part.diffuse_alpha, c.value, 4 );
                part.has_box = true;
                std::memcpy( part.box, scene.meshes[ mesh ].aabb, sizeof( part.box ) );
                const bool blend = shader.name == "Blend" && !second.empty( );
                if ( blend )
                    part.detail = second;
                // The one-bit alpha texture is bound by the shader's other two programs' tables (the depth / shadow
                // passes: "OneBitAlphaTexture" beside "gAlphaTestEnabled") - which retail binds is the renderer's.
                // The cut-out is retail's sort type +0x20 = 3, not the texture's name.
                part.alpha_test = shader.mode == 3u ? 1 : 0;
                if ( blend && part.alpha_test == 0 )
                    part.alpha_test = 10;
                model.parts.push_back( std::move( part ) );
            }
    return !model.parts.empty( );
}