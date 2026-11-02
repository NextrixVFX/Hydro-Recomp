#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <xmmintrin.h>

// Retail's static model load, recreated 1:1 from the disassembly (2026-10-31, the world agent):
// VuStaticModelAsset's load 0x542C30 builds a VuGfxStaticScene (0x7C: VuGfxScene ctor 0x5C1370, the root list
// +0x44, the counters 0x560930 at +0x4C) and runs its load 0x560C70 on the asset's reader - a VuBinaryDataReader
// ([0] data, [1] size, [2] offset). Every read is a memcpy_s / strlen at data + offset with NO bounds check.
//
//   0x560C70 VuGfxStaticScene load:  VuGfxScene load 0x5C1560 (flag 0); u32 roots; each root a VuGfxSceneNode
//            (0x90, ctor 0x5C1FE0) read by 0x5C24D0; then the mesh-instance links (root here, 0x5C2660 below)
//            and 0x5610F0 (the counters and the aabbs).
//   0x5C1560 VuGfxScene load: u32 shaders, each a VuGfxSceneShader (0x4C, ctor 0x592810(index, flag)) read by
//            0x5929B0; u32 meshes, each a VuGfxSceneMesh (0x4C, ctor 0x615C40) read by 0x615E40; u32 chunks, each a
//            VuGfxSceneChunk (0x1C) read by 0x618160; every part linked to its shader (+0xC = shaders[+0x14]) and
//            chunk (+0x10 = chunks[+0x18]); then the totals 0x5C1F30.
//
// What is NOT recreated, and why (the rendering platform layer - hooks): the D3D11 objects the load creates -
// the shader programs (0x5AC0C0 -> 0x5AC470 / 0x5AC5C0 CreateVertexShader / CreatePixelShader), the input
// layout (0x540E00), the pipeline states (0x53A440 -> VuShaderProgram_resolveConstants 0x541680), the
// constant handles (0x593C00), the vertex / index buffers (0x5637F0 / 0x563B50 and their CreateBuffer /
// UpdateSubresource), the chunk's stream binding (0x53A7C0 on g 0x759074). The bytes those take are kept as
// ranges into the blob, so the renderer reads exactly what retail hands the device. The shader's material
// tables (0x541290 constants, 0x5414C0 textures) are read here because they are stream content; retail only
// passes them to the pipeline state.
//
// OURS (flagged): the bounds guard. Retail reads past the end of a short blob; here any read that would leaves
// `bad` set and the load returns false - an explicit failure, never a guess. Likewise an out-of-range shader /
// chunk / mesh index (retail indexes its vector unchecked).

struct scene_reader_t
{
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::size_t at = 0;
    bool bad = false;
};

inline bool scene_take( scene_reader_t& r, void* out, std::size_t count )
{
    if ( r.bad || count > r.size || r.at > r.size - count )
    {
        r.bad = true;
        return false;
    }
    std::memcpy( out, r.data + r.at, count );
    r.at += count;
    return true;
}

inline std::uint32_t scene_u32( scene_reader_t& r )
{
    std::uint32_t value = 0;
    scene_take( r, &value, 4 );
    return value;
}

inline std::uint8_t scene_u8( scene_reader_t& r )
{
    std::uint8_t value = 0;
    scene_take( r, &value, 1 );
    return value;
}

// A string field: strlen at the cursor (0 when the first byte is 0), VuString_assign, offset += length + 1.
inline std::string scene_string( scene_reader_t& r )
{
    if ( r.bad || r.at >= r.size )
    {
        r.bad = true;
        return {};
    }
    const void* end = std::memchr( r.data + r.at, 0, r.size - r.at );
    if ( end == nullptr )
    {
        r.bad = true;
        return {};
    }
    const std::size_t length = static_cast< const std::uint8_t* >( end ) - ( r.data + r.at );
    std::string text( reinterpret_cast< const char* >( r.data + r.at ), length );
    r.at += length + 1;
    return text;
}

// A span of the blob the load hands on without reading (a D3D11 object's source bytes).
struct scene_span_t
{
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
};

inline scene_span_t scene_skip( scene_reader_t& r, std::uint32_t count )
{
    scene_span_t span{};
    span.offset = static_cast< std::uint32_t >( r.at );
    span.size = count;
    if ( r.bad || count > r.size || r.at > r.size - count )
        r.bad = true;
    else
        r.at += count;
    return span;
}

// 0x5AC470 (fastcall: type, reader): u32 the program's key (+0xC of the cached object), u32 the constant buffer
// size (0x5AC5C0's third argument), then 0x5AC7F0's blob (u32 n, n bytes - the DXBC bytecode).
struct scene_stage_t
{
    std::uint32_t key = 0;
    std::uint32_t constants = 0;
    scene_span_t bytecode;
};

// 0x5AC0C0 (thiscall reader): the vertex stage (type 0), the pixel stage (type 1), then 0x5AC7F0's blob (u32 n,
// n bytes) that 0x5AC240 parses into the program's constant table.
struct scene_program_t
{
    std::uint32_t at = 0;   // where the program's stream starts in the blob: what the device side hands 0x5AC0C0
    scene_stage_t vertex;
    scene_stage_t pixel;
    scene_span_t layout;
};

inline void scene_read_stage( scene_reader_t& r, scene_stage_t& stage )
{
    stage.key = scene_u32( r );
    stage.constants = scene_u32( r );
    stage.bytecode = scene_skip( r, scene_u32( r ) );
}

inline void scene_read_program( scene_reader_t& r, scene_program_t& program )
{
    program.at = static_cast< std::uint32_t >( r.at );
    scene_read_stage( r, program.vertex );
    scene_read_stage( r, program.pixel );
    program.layout = scene_skip( r, scene_u32( r ) );
}

// 0x541290: u32 count, then per constant a 52-byte record - the name (strlen + 1 in the stream, strcpy_s into
// +0, 32 bytes), u32 +0x24, three u32 +0x28 / +0x2C / +0x30. (0x541390 hashes exactly these: the name, +0x24,
// +0x28..+0x33.)
struct scene_constant_t
{
    std::string name;
    std::uint32_t type = 0;
    std::uint32_t value[ 3 ]{};
};

// 0x5414C0: u32 count, then per texture a 112-byte record - the name (strcpy_s into +0, 32 bytes), a second
// string (strcpy_s into +0x20, 8 bytes), u32 +0x2C, the asset name (strcpy_s into +0x30, 64 bytes).
struct scene_texture_t
{
    std::string name;
    std::string slot;
    std::uint32_t value = 0;
    std::string asset;
};

struct scene_table_t
{
    std::vector< scene_constant_t > constants;
    std::vector< scene_texture_t > textures;
};

inline void scene_read_table( scene_reader_t& r, scene_table_t& table )
{
    // The counts are signed: `> 0` guards both loops.
    const int constants = static_cast< int >( scene_u32( r ) );
    for ( int index = 0; index < constants && !r.bad; ++index )
    {
        scene_constant_t constant{};
        constant.name = scene_string( r );
        constant.type = scene_u32( r );
        for ( std::uint32_t& word : constant.value )
            word = scene_u32( r );
        table.constants.push_back( std::move( constant ) );
    }
    const int textures = static_cast< int >( scene_u32( r ) );
    for ( int index = 0; index < textures && !r.bad; ++index )
    {
        scene_texture_t texture{};
        texture.name = scene_string( r );
        texture.slot = scene_string( r );
        texture.value = scene_u32( r );
        texture.asset = scene_string( r );
        table.textures.push_back( std::move( texture ) );
    }
}

// One element of the shader's vertex declaration (16 bytes, copied whole by 0x607640's vector; 0x540F70 turns
// each into a D3D11_INPUT_ELEMENT_DESC: semantic off_7512CC[+8], index byte +0xC, format dword_751290[+4], slot
// u16 +0, offset u16 +2).
struct scene_element_t
{
    std::uint16_t stream = 0;
    std::uint16_t offset = 0;
    std::uint32_t type = 0;
    std::uint32_t usage = 0;
    std::uint8_t index = 0;
    std::uint8_t pad[ 3 ]{};
};
static_assert( sizeof( scene_element_t ) == 16, "the declaration element is 16 bytes" );

// VuGfxSceneShader (0x4C; ctor 0x592810(index, flag): +0xC index, +0x18 flag, +0x20 = 2), read by 0x5929B0.
struct scene_shader_t
{
    std::uint32_t index = 0;   // +0xC
    std::uint8_t flag = 0;   // +0x18 (the scene load's flag: 0 from the static scene)
    std::string name;   // read into a local and dropped
    std::uint8_t variant = 0;   // the byte after the name (a local)
    scene_program_t program;   // +0x14 (0x5AC0C0's result)
    scene_span_t other;   // the program the device does not take, skipped by its length
    std::vector< scene_element_t > elements;
    std::uint8_t bytes[ 6 ]{};   // +0x19 .. +0x1E
    std::uint32_t mode = 2;   // +0x20
    scene_table_t tables[ 3 ];   // for +0x24 (the program), +0x28 / +0x2C (g_vugfxutil +8 / +0xC's programs)
};

// 0x5929B0. `alternate` is g_vumetrod3d11gfx +0x28 (a device capability - the platform layer): with it and the
// variant byte set, the first program is skipped by its length and the second read; otherwise the first is read
// and the second skipped. (No VuStaticModelAsset in Base.apf sets the variant byte: 0 of 4,190 shaders.)
inline void scene_read_shader( scene_reader_t& r, scene_shader_t& shader, bool alternate )
{
    shader.name = scene_string( r );
    shader.variant = scene_u8( r );
    if ( shader.variant != 0 && alternate )
    {
        shader.other = scene_skip( r, scene_u32( r ) );
        scene_u32( r );
        scene_read_program( r, shader.program );
    }
    else
    {
        scene_u32( r );
        scene_read_program( r, shader.program );
        shader.other = scene_skip( r, scene_u32( r ) );
    }
    const std::uint32_t elements = scene_u32( r );
    if ( r.bad || elements > ( r.size - r.at ) / 16 )
    {
        r.bad = true;
        return;
    }
    shader.elements.resize( elements );
    scene_take( r, shader.elements.data( ), 16 * static_cast< std::size_t >( elements ) );
    scene_take( r, shader.bytes, 6 );
    shader.mode = scene_u32( r );
    for ( scene_table_t& table : shader.tables )
        scene_read_table( r, table );
    // g_vugfxcomposer 0x7590BC's vt+0x10 (a renderer setting) turns a mode of 2 or 3 into 1 - a hook, not taken.
}

// VuGfxSceneMeshPart (0x50; ctor 0x625820), read by 0x625990: +0x18 chunk, +0x14 shader, +0x1C first vertex,
// +0x20 last vertex, +0x28 first index, +0x2C triangles, +0x30 the aabb (32 bytes raw: min x y z w, max x y z w);
// +0x24 = last - first + 1.
struct scene_part_t
{
    std::uint32_t chunk = 0;
    std::uint32_t shader = 0;
    std::uint32_t first_vertex = 0;
    std::uint32_t last_vertex = 0;
    std::uint32_t vertices = 0;
    std::uint32_t first_index = 0;
    std::uint32_t triangles = 0;
    float aabb[ 8 ]{};
};

// VuGfxSceneMesh (0x4C), read by 0x615E40: the name (+0xC), u32 parts (+0x24 list), the aabb (+0x2C, 32 bytes).
struct scene_mesh_t
{
    std::string name;
    std::vector< scene_part_t > parts;
    float aabb[ 8 ]{};
};

// VuGfxSceneChunk (0x1C), read by 0x618160: u32 stride (+0xC), u32 vertex bytes + the bytes (the vertex buffer
// +0x10, whose +0xC is that byte count), u32 index count + 2 bytes each (the index buffer +0x14, +0xC the count).
struct scene_chunk_t
{
    std::uint32_t stride = 0;
    scene_span_t vertices;
    std::uint32_t indices = 0;
    scene_span_t index_bytes;
};

// VuGfxSceneNode (0x90; ctor 0x5C1FE0: identity matrix, the aabb FLT_MAX / -FLT_MAX), read by 0x5C24D0: the
// name (+0xC), the matrix (+0x24, 64 bytes), a byte - set: a VuGfxSceneMeshInstance (+0x64) whose +0xC is a u32
// mesh index -, u32 children (+0x68 list, read recursively), +0x70 the aabb 0x5610F0 fills.
struct scene_node_t
{
    std::string name;
    float matrix[ 16 ]{};
    int mesh = -1;
    std::vector< int > children;   // indices into the scene's nodes
    float aabb[ 8 ]{};
};

struct scene_t
{
    std::vector< scene_shader_t > shaders;   // +0xC
    std::vector< scene_mesh_t > meshes;   // +0x18
    std::vector< scene_chunk_t > chunks;   // +0x24
    // 0x5C1F30's totals: +0x30 meshes, +0x34 parts, +0x38 vertices (each chunk's vertex bytes / stride), +0x3C
    // triangles (each chunk's index count / 3), +0x40 shaders.
    std::uint32_t mesh_count = 0;
    std::uint32_t part_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t shader_count = 0;
    // The static scene: the nodes (each node before its children, children in stream order), the roots (+0x44),
    // and 0x560930's block at +0x4C: nodes, mesh instances, vertices and triangles of every instance's parts, the
    // aabb (+0x5C) of every instance's mesh aabb in the world.
    std::vector< scene_node_t > nodes;
    std::vector< int > roots;
    std::uint32_t node_total = 0;
    std::uint32_t instance_total = 0;
    std::uint32_t vertex_total = 0;
    std::uint32_t triangle_total = 0;
    float aabb[ 8 ]{};
};

// 0x7F7FFFFF / 0xFF7FFFFF - the ctors' empty aabb (min FLT_MAX, max -FLT_MAX; the w lanes are the ctors' stale
// stack and are not modelled).
inline void scene_aabb_empty( float* aabb )
{
    const std::uint32_t low = 0x7F7FFFFFu, high = 0xFF7FFFFFu;
    for ( int lane = 0; lane < 4; ++lane )
    {
        std::memcpy( aabb + lane, &low, 4 );
        std::memcpy( aabb + 4 + lane, &high, 4 );
    }
}

// VuMath_min 0x403260 / VuMath_max 0x403280: minss / maxss (a when a < b / a > b, else b).
inline float scene_min( float a, float b )
{
    return _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) );
}

inline float scene_max( float a, float b )
{
    return _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) );
}

// The x86 SSE scalar NaN rule, written out so the compiler cannot commute it away
// (boat/events.hxx sse_nan_pick's rule, which assets cannot include): a NaN first
// operand (mulss / addss's destination) wins, quieted; else a NaN second.
inline float scene_sse( float a, float b, bool add )
{
    std::uint32_t w;
    if ( a != a )
        std::memcpy( &w, &a, 4 );
    else if ( b != b )
        std::memcpy( &w, &b, 4 );
    else
        return add ? a + b : a * b;
    w |= 0x00400000u;
    float r;
    std::memcpy( &r, &w, 4 );
    return r;
}

// VuMatrix_multiply 0x4078C0 (thiscall a; out*, b*): out[4i+j] = p0 + p1 + p2 + p3 with
// pk = a[4i+k] x b[4k+j] - a in b's frame (row vectors) - in retail's operand order
// (world batch 23, read from the disassembly; it decides which NaN survives):
//   products: column 0 of every row and rows 0 - 2 everywhere b first (b x a), except
//   p3 of column 0 (a x b); row 3, columns 1 - 3, a first (a x b).
//   sums: row 0 and row 1 column 0 ((p0 + p1) + p2) + p3; every other element
//   (p2 + (p0 + p1)) + p3.
inline void scene_multiply( const float* a, float* out, const float* b )
{
    float m[ 16 ];
    for ( int i = 0; i < 4; ++i )
        for ( int j = 0; j < 4; ++j )
        {
            float p[ 4 ];
            for ( int k = 0; k < 4; ++k )
            {
                const bool a_first = k == 3 && j == 0 ? true : ( i == 3 && j != 0 );
                p[ k ] = a_first ? scene_sse( a[ 4 * i + k ], b[ 4 * k + j ], false ) : scene_sse( b[ 4 * k + j ], a[ 4 * i + k ], false );
            }
            const float p01 = scene_sse( p[ 0 ], p[ 1 ], true );
            const bool left = i == 0 || ( i == 1 && j == 0 );
            const float p012 = left ? scene_sse( p01, p[ 2 ], true ) : scene_sse( p[ 2 ], p01, true );
            m[ 4 * i + j ] = scene_sse( p012, p[ 3 ], true );
        }
    std::memcpy( out, m, 64 );
}

// 0x5175E0 (thiscall aabb; source aabb*, matrix*): the source's eight corners (0x5177A0: x fastest, then y, then
// z) through the matrix - x' = ((x*m0 + y*m4) + z*m8) + m12, likewise y, z - each folded in as min = VuMath_min(
// p, min ), max = VuMath_max( p, max ).
inline void scene_aabb_grow( float* aabb, const float* source, const float* m )
{
    for ( int corner = 0; corner < 8; ++corner )
    {
        const float x = ( corner & 1 ) ? source[ 4 ] : source[ 0 ];
        const float y = ( corner & 2 ) ? source[ 5 ] : source[ 1 ];
        const float z = ( corner & 4 ) ? source[ 6 ] : source[ 2 ];
        const float px = ( ( x * m[ 0 ] + y * m[ 4 ] ) + z * m[ 8 ] ) + m[ 12 ];
        const float py = ( ( x * m[ 1 ] + y * m[ 5 ] ) + z * m[ 9 ] ) + m[ 13 ];
        const float pz = ( ( x * m[ 2 ] + y * m[ 6 ] ) + z * m[ 10 ] ) + m[ 14 ];
        aabb[ 0 ] = scene_min( px, aabb[ 0 ] );
        aabb[ 1 ] = scene_min( py, aabb[ 1 ] );
        aabb[ 2 ] = scene_min( pz, aabb[ 2 ] );
        aabb[ 4 ] = scene_max( px, aabb[ 4 ] );
        aabb[ 5 ] = scene_max( py, aabb[ 5 ] );
        aabb[ 6 ] = scene_max( pz, aabb[ 6 ] );
    }
}

// 0x41B2A0 (thiscall out; source aabb*, matrix*): out = the empty aabb (FLT_MAX / -FLT_MAX), then 0x5175E0 - the
// source aabb's corners through the matrix.
inline void scene_aabb_transform( float* out, const float* source, const float* matrix )
{
    scene_aabb_empty( out );
    scene_aabb_grow( out, source, matrix );
}

// The engine's zero aabb 0x7593F0 (min 0 0 0, max 0 0 0; its CRT initializer 0x64B690) - what the static model
// component measures when its LOD 0 instance holds no scene (0x5B09E0 / 0x546AE0's `+0x10 ? +0x5C : 0x7593F0`).
inline const float* scene_zero_aabb( )
{
    static const float zero[ 8 ]{};
    return zero;
}

// The static model component's world aabb, 0x531D10 (thiscall component; source aabb*, matrix*): +0x24 .. +0x43 =
// 0x41B2A0( source, matrix ); then, its +0x1E byte set, the draw manager g_ 0x7590B8's vt+0x30( component ) - the
// rendering side, a hook: returned as true for the caller to act on.
inline bool scene_component_aabb( float* component_aabb, const float* source, const float* matrix, bool flag_1e )
{
    scene_aabb_transform( component_aabb, source, matrix );
    return flag_1e;
}

// The engine's static identity 0x7593B0 (its CRT initializer 0x64B520).
inline const float* scene_identity( )
{
    static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    return identity;
}

// 0x5C24D0 (thiscall node; reader*). The node is stored before its children are read.
inline int scene_read_node( scene_reader_t& r, scene_t& scene, std::size_t depth )
{
    // OURS: a recursion guard (retail recurses without one) - deeper than the blob could describe is malformed.
    if ( r.bad || depth > r.size )
    {
        r.bad = true;
        return -1;
    }
    const int self = static_cast< int >( scene.nodes.size( ) );
    scene.nodes.emplace_back( );
    scene_node_t node{};
    node.name = scene_string( r );
    scene_take( r, node.matrix, 64 );
    if ( scene_u8( r ) != 0 )
        node.mesh = static_cast< int >( scene_u32( r ) );
    scene_aabb_empty( node.aabb );
    const std::uint32_t children = scene_u32( r );
    scene.nodes[ self ] = std::move( node );
    for ( std::uint32_t child = 0; child < children && !r.bad; ++child )
    {
        const int index = scene_read_node( r, scene, depth + 1 );
        scene.nodes[ self ].children.push_back( index );
    }
    return self;
}

// 0x5C2770 (thiscall node; aabb*, matrix*): m = node x matrix; the node's mesh aabb grown into `aabb` through m;
// then each child with m.
inline void scene_node_aabb( const scene_t& scene, int node, float* aabb, const float* matrix )
{
    const scene_node_t& self = scene.nodes[ node ];
    float m[ 16 ];
    scene_multiply( self.matrix, m, matrix );
    if ( self.mesh >= 0 )
        scene_aabb_grow( aabb, scene.meshes[ self.mesh ].aabb, m );
    for ( int child : self.children )
        scene_node_aabb( scene, child, aabb, m );
}

// 0x618450 (thiscall instance; counters*, matrix*): +4 instances, the mesh aabb into +0x10 (the scene aabb)
// through the matrix, then +8 / +0xC the parts' vertices (+0x24) and triangles (+0x2C).
inline void scene_count_instance( scene_t& scene, const scene_mesh_t& mesh, const float* matrix )
{
    ++scene.instance_total;
    scene_aabb_grow( scene.aabb, mesh.aabb, matrix );
    for ( const scene_part_t& part : mesh.parts )
    {
        scene.vertex_total += part.vertices;
        scene.triangle_total += part.triangles;
    }
}

// 0x5C26F0 (thiscall node; counters*, matrix*): m = node x matrix; +0 nodes; the instance (0x618450) with m; the
// node's own aabb (+0x70) = 0x5C2770( node, +0x70, identity ); then each child with m.
inline void scene_count_node( scene_t& scene, int node, const float* matrix )
{
    float m[ 16 ];
    scene_multiply( scene.nodes[ node ].matrix, m, matrix );
    ++scene.node_total;
    if ( scene.nodes[ node ].mesh >= 0 )
        scene_count_instance( scene, scene.meshes[ scene.nodes[ node ].mesh ], m );
    scene_node_aabb( scene, node, scene.nodes[ node ].aabb, scene_identity( ) );
    const std::vector< int > children = scene.nodes[ node ].children;
    for ( int child : children )
        scene_count_node( scene, child, m );
}

// 0x5C1560 (thiscall scene; reader*, flag).
inline bool scene_load_gfx( scene_reader_t& r, scene_t& scene, std::uint8_t flag, bool alternate )
{
    const int shaders = static_cast< int >( scene_u32( r ) );
    for ( int index = 0; index < shaders && !r.bad; ++index )
    {
        scene_shader_t shader{};
        shader.index = static_cast< std::uint32_t >( index );
        shader.flag = flag;
        scene_read_shader( r, shader, alternate );
        scene.shaders.push_back( std::move( shader ) );
    }
    const int meshes = static_cast< int >( scene_u32( r ) );
    for ( int index = 0; index < meshes && !r.bad; ++index )
    {
        scene_mesh_t mesh{};
        mesh.name = scene_string( r );
        const std::uint32_t parts = scene_u32( r );
        for ( std::uint32_t part_index = 0; part_index < parts && !r.bad; ++part_index )
        {
            scene_part_t part{};
            part.chunk = scene_u32( r );
            part.shader = scene_u32( r );
            part.first_vertex = scene_u32( r );
            part.last_vertex = scene_u32( r );
            part.first_index = scene_u32( r );
            part.triangles = scene_u32( r );
            scene_take( r, part.aabb, 32 );
            part.vertices = part.last_vertex - part.first_vertex + 1;
            mesh.parts.push_back( part );
        }
        scene_take( r, mesh.aabb, 32 );
        scene.meshes.push_back( std::move( mesh ) );
    }
    const int chunks = static_cast< int >( scene_u32( r ) );
    for ( int index = 0; index < chunks && !r.bad; ++index )
    {
        scene_chunk_t chunk{};
        chunk.stride = scene_u32( r );
        chunk.vertices = scene_skip( r, scene_u32( r ) );
        chunk.indices = scene_u32( r );
        if ( chunk.indices > 0x7FFFFFFFu )
            r.bad = true;
        chunk.index_bytes = scene_skip( r, 2 * chunk.indices );
        scene.chunks.push_back( chunk );
    }
    if ( r.bad )
        return false;
    // The links: every part's shader (+0x14) and chunk (+0x18) must name an entry (OURS: retail does not check).
    for ( const scene_mesh_t& mesh : scene.meshes )
        for ( const scene_part_t& part : mesh.parts )
            if ( part.shader >= scene.shaders.size( ) || part.chunk >= scene.chunks.size( ) )
                return false;
    // 0x5C1F30: the totals. Unsigned divisions (the stride 0 a chunk never has: OURS refuses it).
    for ( const scene_chunk_t& chunk : scene.chunks )
    {
        if ( chunk.stride == 0 )
            return false;
        scene.triangle_count += chunk.indices / 3;
        scene.vertex_count += chunk.vertices.size / chunk.stride;
    }
    scene.shader_count += static_cast< std::uint32_t >( scene.shaders.size( ) );
    for ( const scene_mesh_t& mesh : scene.meshes )
    {
        ++scene.mesh_count;
        scene.part_count += static_cast< std::uint32_t >( mesh.parts.size( ) );
    }
    return true;
}

// 0x560C70 (thiscall static scene; reader*). `alternate`: see scene_read_shader.
inline bool scene_load_static( const std::uint8_t* data, std::size_t size, scene_t& scene, bool alternate = false )
{
    scene = {};
    scene_aabb_empty( scene.aabb );
    scene_reader_t r{};
    r.data = data;
    r.size = size;
    if ( !scene_load_gfx( r, scene, 0, alternate ) )
        return false;
    const std::uint32_t roots = scene_u32( r );
    for ( std::uint32_t root = 0; root < roots && !r.bad; ++root )
        scene.roots.push_back( scene_read_node( r, scene, 0 ) );
    if ( r.bad )
        return false;
    // The mesh-instance links (+0x64 +0x10 = meshes[+0xC]) - OURS: an index past the meshes fails the load.
    for ( const scene_node_t& node : scene.nodes )
        if ( node.mesh >= 0 && static_cast< std::size_t >( node.mesh ) >= scene.meshes.size( ) )
            return false;
    // 0x5610F0: per root, in list order.
    for ( int root : scene.roots )
    {
        float m[ 16 ];
        scene_multiply( scene.nodes[ root ].matrix, m, scene_identity( ) );
        ++scene.node_total;
        if ( scene.nodes[ root ].mesh >= 0 )
            scene_count_instance( scene, scene.meshes[ scene.nodes[ root ].mesh ], m );
        float own[ 16 ];
        scene_multiply( scene.nodes[ root ].matrix, own, scene_identity( ) );
        if ( scene.nodes[ root ].mesh >= 0 )
            scene_aabb_grow( scene.nodes[ root ].aabb, scene.meshes[ scene.nodes[ root ].mesh ].aabb, own );
        const std::vector< int > children = scene.nodes[ root ].children;
        for ( int child : children )
            scene_node_aabb( scene, child, scene.nodes[ root ].aabb, own );
        for ( int child : children )
            scene_count_node( scene, child, m );
    }
    return true;
}
