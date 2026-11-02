#pragma once

#include <workspace/core/ui/draw.hxx>
#include <workspace/utility/drawstats.hxx>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// VUGFXSORT'S CPU SIDE (render layer, platform independent), recreated 1:1 from the disassembly (water batch 30): the
// frame's draw commands, their 64-bit keys, the swap of the two command buffers, the radix sort, and the walk that turns
// the sorted list into state changes and callback calls. g 0x759074 (ctor 0x539A60, vtable 0x74D5C8).
//
// allocate 0x426B80 and submit 0x429010 are ui_gfx_sort_t's (the UI agent's core/ui/draw.hxx): this class extends it.
//
// Everything past the CPU side is a hook, gfx_sort_hooks_t, named by the retail vtable offset it stands for: the device
// g 0x75907C (vtable 0x74D79C), the shader program, the thread g 0x759098, the profiler g 0x7590A0, the composer g 0x7590BC,
// the water renderer g 0x759058, the light manager's flip. No device or shader decision is made here.
// Not recreated: the dev statistics page +0xE8 (0x5A8B10, 0x53B2C0: g 0x759020 dev, STOPPED with the other dev pages).
//
// VuGfxSort: +4 single-threaded flag, +8 the render thread, +0xC a frame is pending on it, +0x10 / +0x20 the sorted material and
// chunk caches (+0x1C / +0x2C their live counts), +0x30 the last allocation, +0x34 + 12i the command memory, +0x4C + 12i the commands,
// +0x64 the buffer being filled, +0x68 the buffer being drawn, +0x70 / +0x74 the key bits, +0x78 / +0x7C the key being drawn,
// +0x80 the sort keys, +0x8C the sorted order, +0x98 the time, +0x9C / +0xA0 two thread events, +0xA4 a byte the wait
// 0x53A250 sets (nothing here reads it), +0xA8 eight counters, +0xC8 last frame's.

// The 12-byte record of the static table 0x735A00, one per command type (the sort layer): no writer in .text. Applied by the
// walk 0x53ADC0 when the type changes. src / dst are the engine's blend codes.
struct gfx_layer_t
{
    std::uint8_t blend, depth, write;
    std::uint32_t src, dst;
};

inline constexpr gfx_layer_t k_gfx_layers[ 20 ] = {
    { 0, 1, 1, 4, 5 }, { 0, 1, 1, 4, 5 }, { 0, 1, 1, 4, 5 }, { 0, 1, 1, 4, 5 }, { 0, 1, 1, 4, 5 },
    { 0, 1, 0, 4, 5 }, { 1, 1, 0, 1, 5 }, { 1, 1, 0, 4, 5 }, { 1, 1, 0, 4, 5 }, { 1, 1, 0, 4, 1 },
    { 1, 1, 0, 4, 5 }, { 1, 1, 0, 4, 5 }, { 1, 1, 0, 4, 1 }, { 0, 1, 1, 4, 5 }, { 1, 1, 0, 4, 5 },
    { 1, 1, 0, 4, 1 }, { 0, 0, 0, 4, 5 }, { 1, 0, 0, 4, 5 }, { 1, 0, 0, 4, 1 }, { 0, 1, 1, 4, 5 },
};

// The device's vtable offsets the sort calls, named after their use at the call sites.
enum gfx_device_slot_t
{
    k_gfx_acquire = 0x30, k_gfx_release = 0x34, k_gfx_begin = 0x58, k_gfx_end = 0x5C, k_gfx_slot_80 = 0x80, k_gfx_program = 0x84,
    k_gfx_declaration = 0x88, k_gfx_stream = 0x8C, k_gfx_indices = 0x90, k_gfx_depth = 0x94, k_gfx_depth_write = 0x98,
    k_gfx_depth_func = 0x9C, k_gfx_blend = 0xA0, k_gfx_blend_func = 0xA4, k_gfx_colour_write = 0xA8, k_gfx_cull = 0xAC, k_gfx_texture = 0xB4, k_gfx_flush = 0xD0,
};

// The shader program's vtable offsets.
enum gfx_program_slot_t
{
    k_gfx_set_float = 0x10, k_gfx_set_vector = 0x14, k_gfx_set_int = 0x18, k_gfx_set_matrix = 0x1C, k_gfx_set_vec3 = 0x24,
    k_gfx_set_value = 0x28, k_gfx_set_vec4 = 0x2C, k_gfx_bind = 0x48,
};

// A material's constant, a 0x34-byte record from material +0 (count +0x340): +0 the name (32 bytes), +0x20 the shader's
// handle, +0x24 0 an int, 1 a float, 2 a vector (0x541C40), +0x28 the three value words.
struct gfx_constant_t
{
    std::string m_name;
    std::uint32_t m_handle = 0;
    std::uint32_t m_kind = 0;
    std::uint32_t m_value[ 3 ] = { };
};

// VuGfxSortMaterialDesc as 0x53A440 reads it: the constants (+0, count +0x340) and textures (+0x344, 0x70 bytes each, count
// +0x6C4: +0 the sampler name, +0x20 a second string, +0x2C the kind, +0x30 the asset name). scene.hxx scene_table_t is the same
// data as the shipped scenes hold it.
struct gfx_material_desc_t
{
    struct constant_t
    {
        std::string m_name;
        std::uint32_t m_kind = 0;
        std::uint32_t m_value[ 3 ] = { };
    };
    struct texture_t
    {
        std::string m_name;
        std::string m_slot;
        std::uint32_t m_kind = 0;
        std::string m_asset;
    };
    std::vector< constant_t > m_constants;
    std::vector< texture_t > m_textures;
};

// VuGfxSortMaterial (0x760; init 0x53A610 + 0x541200): the walk's state block. Made and shared by 0x53A440.
struct gfx_material_t
{
    std::uint32_t m_key = 0;           // +0x6C8 the bits 0x429010 puts in the key: the index in the cache, renumbered per create
    std::uint32_t m_constants_id = 0;  // +0x6CC 0x541390's hash
    std::uint32_t m_textures_id = 0;   // +0x6D0 0x5415D0's hash
    std::uint32_t m_program = 0;       // +0x6D4
    std::uint32_t m_declaration = 0;   // +0x6D8
    // The shader's handles of the frame constants, by the name 0x541680 looks up; 0 = the shader has none.
    std::uint32_t m_view_matrix = 0, m_view_proj_matrix = 0, m_eye_world = 0, m_far_plane = 0;         // +0x6FC +0x700 +0x704 +0x708
    std::uint32_t m_amb_light_color = 0, m_dir_light_world = 0, m_dir_light_front_color = 0;           // +0x70C +0x710 +0x714
    std::uint32_t m_dir_light_back_color = 0, m_dir_light_specular_color = 0;                          // +0x718 +0x71C
    std::uint32_t m_fog_start = 0, m_fog_inv_range = 0, m_fog_color = 0, m_ssao_enabled = 0;           // +0x720 +0x724 +0x728 +0x72C
    std::uint32_t m_screen_size = 0, m_ssao_texture = 0, m_shadow_enabled = 0;                         // +0x730 +0x734 +0x738
    std::uint32_t m_shadow_texture_matrices = 0, m_split_planes = 0, m_shadow_map_size = 0;            // +0x73C +0x740 +0x744
    std::uint32_t m_shadow_map_texel_size = 0, m_shadow_map0 = 0, m_time = 0, m_clip_plane = 0;        // +0x748 +0x74C +0x750 +0x754
    std::uint32_t m_composer = 0;      // +0x758
    std::uint32_t m_refs = 1;          // +0x75C
    std::vector< gfx_constant_t > m_constants;
    struct texture_t
    {
        std::string m_name;
        std::uint32_t m_kind = 0;      // +0x344 + 0x70i + 0x2C
        std::uint32_t m_sampler = 0;   // +0x36C + 0x70i
        std::uint32_t m_texture = 0;   // +0x6DC + 4i (kinds past 1 leave it unset in retail; 0 here)
    };
    std::vector< texture_t > m_textures;
};

// 0x541680's lookups of the frame constants, in its order: by the program's vt+4, the two textures by vt+8.
struct gfx_frame_constant_t
{
    std::uint32_t gfx_material_t::* m_field;
    const char* m_name;
    bool m_sampler;
};

inline constexpr gfx_frame_constant_t k_gfx_frame_constants[ 23 ] = {
    { &gfx_material_t::m_view_matrix, "gViewMatrix", false }, { &gfx_material_t::m_view_proj_matrix, "gViewProjMatrix", false },
    { &gfx_material_t::m_eye_world, "gEyeWorld", false }, { &gfx_material_t::m_far_plane, "gFarPlane", false },
    { &gfx_material_t::m_amb_light_color, "gAmbLightColor", false }, { &gfx_material_t::m_dir_light_world, "gDirLightWorld", false },
    { &gfx_material_t::m_dir_light_front_color, "gDirLightFrontColor", false },
    { &gfx_material_t::m_dir_light_back_color, "gDirLightBackColor", false },
    { &gfx_material_t::m_dir_light_specular_color, "gDirLightSpecularColor", false }, { &gfx_material_t::m_fog_start, "gFogStart", false },
    { &gfx_material_t::m_fog_inv_range, "gFogInvRange", false }, { &gfx_material_t::m_fog_color, "gFogColor", false },
    { &gfx_material_t::m_ssao_enabled, "gSSAOEnabled", false }, { &gfx_material_t::m_screen_size, "gScreenSize", false },
    { &gfx_material_t::m_ssao_texture, "SSAOTexture", true }, { &gfx_material_t::m_shadow_enabled, "gShadowEnabled", false },
    { &gfx_material_t::m_shadow_texture_matrices, "gShadowTextureMatrices", false }, { &gfx_material_t::m_split_planes, "gSplitPlanes", false },
    { &gfx_material_t::m_shadow_map_size, "gShadowMapSize", false }, { &gfx_material_t::m_shadow_map_texel_size, "gShadowMapTexelSize", false },
    { &gfx_material_t::m_shadow_map0, "gShadowMap0", true }, { &gfx_material_t::m_time, "gTime", false }, { &gfx_material_t::m_clip_plane, "gClipPlane", false },
};

// The third submit argument (0x14 bytes, made and shared by 0x53A7C0): 0x53B070's stream state.
struct gfx_chunk_t
{
    std::uint32_t m_stream = 0;        // +0, reference counted
    std::uint32_t m_indices = 0;       // +4, reference counted
    std::uint32_t m_stride = 0;        // +8
    std::uint32_t m_key = 0;           // +0xC, 0x429010's key bits: the index in the cache, renumbered per create
    std::uint32_t m_refs = 1;          // +0x10
};

// Where a program call's argument comes from: retail hands the address of a static or a light-manager field. The platform
// side reads them; here they are named by that retail address (the light manager's fields: k_gfx_light + the field).
constexpr std::uint32_t k_gfx_view_matrix = 0x759510u, k_gfx_view_proj_matrix = 0x759550u, k_gfx_eye_world = 0x7594A0u, k_gfx_fog_color = 0x7596F0u;
constexpr std::uint32_t k_gfx_light = 0x10000000u;

struct gfx_sort_hooks_t
{
    virtual ~gfx_sort_hooks_t( ) = default;
    virtual std::uint32_t device( int slot, std::uint32_t a = 0, std::uint32_t b = 0, std::uint32_t c = 0 ) = 0;
    virtual void program( std::uint32_t id, int slot, std::uint32_t handle = 0, std::uint32_t value = 0, const std::uint32_t* vector = nullptr ) = 0;
    virtual std::uint32_t thread( int slot, std::uint32_t a = 0, std::uint32_t b = 0 ) = 0;  // g 0x759098
    virtual void profile( int slot ) = 0;                                                    // g 0x7590A0, when present
    virtual void scheduler( int, std::uint32_t = 0 ) { }                                     // g 0x759008
    virtual void composer( int slot, std::uint32_t a = 0, std::uint32_t b = 0 ) = 0;         // [g 0x7590BC +4], when present
    virtual void water_flip( ) = 0;                                                          // 0x595690 on [g 0x759058 +0x1C]
    virtual void light_flip( ) = 0;                                                          // g 0x759094 +0xC4 / +0xC8 ^= 1
    virtual std::uint32_t texture_handle( std::uint32_t texture ) = 0;                       // texture vt+0x1C
    virtual void draw( std::uint32_t callback, std::uint8_t* record ) = 0;                   // the command's callback
    virtual const gfx_material_t* material( std::uint32_t id ) = 0;
    virtual const gfx_chunk_t* chunk( std::uint32_t id ) = 0;
    // The caches' collaborators: the program's lookups, the asset manager g 0x759004, an object's reference count (+4), the composer.
    virtual std::uint32_t program_constant( std::uint32_t, const std::string& ) { return 0; }                // vt+4
    virtual std::int32_t program_sampler( std::uint32_t, const std::string& ) { return -1; }                 // vt+8
    virtual std::uint32_t asset_get( std::uint32_t, const std::string& ) { return 0; }                       // vt+0x1C (kind 0 VuTextureAsset, 1 VuCubeTextureAsset)
    virtual void asset_put( std::uint32_t ) { }                                                              // vt+0x24
    virtual void retain( std::uint32_t ) { }
    virtual void release( std::uint32_t ) { }                                                                // at 0: vt+0( 1 )
    virtual std::uint32_t composer_add( const gfx_material_t& ) { return 0; }                                // vt+0x14
    virtual void composer_remove( std::uint32_t ) { }                                                        // vt+0x18
    bool m_has_profile = false, m_has_composer = false, m_has_water = false, m_has_light = false;
    float m_far_plane = 0.0f, m_fog_start = 0.0f, m_fog_end = 0.0f;                          // statics 0x75961C 0x7596E8 0x7596EC
};

// 0x5A9660 (keys ecx, count edx, order, an unread arg, scratch): a byte-wise LSD radix sort over 8 passes, the keys and the
// order array carried together, ascending and stable. RETAIL'S CAP: only the first 0x5555 entries take part (the scratch
// 0x799898 holds 256 KB, 12 bytes an entry); the rest stay where they are.
inline void gfx_radix_sort( std::uint64_t* keys, int count, std::uint32_t* order )
{
    const int n = ( std::min )( count, 0x5555 );
    std::vector< std::uint64_t > scratch_keys( static_cast< std::size_t >( ( std::max )( n, 0 ) ) );
    std::vector< std::uint32_t > scratch_order( static_cast< std::size_t >( ( std::max )( n, 0 ) ) );
    std::uint64_t* from_keys = keys;
    std::uint64_t* to_keys = scratch_keys.data( );
    std::uint32_t* from_order = order;
    std::uint32_t* to_order = scratch_order.data( );
    for ( int pass = 0; pass < 8; ++pass )
    {
        std::uint32_t counts[ 256 ] = { };
        for ( int i = 0; i < n; ++i )
            ++counts[ ( from_keys[ i ] >> ( 8 * pass ) ) & 0xFF ];
        std::uint32_t offsets[ 256 ];
        offsets[ 0 ] = 0;
        for ( int i = 1; i < 256; ++i )
            offsets[ i ] = offsets[ i - 1 ] + counts[ i - 1 ];
        for ( int i = 0; i < n; ++i )
        {
            const std::uint32_t at = offsets[ ( from_keys[ i ] >> ( 8 * pass ) ) & 0xFF ]++;
            to_keys[ at ] = from_keys[ i ];
            to_order[ at ] = from_order[ i ];
        }
        std::swap( from_keys, to_keys );
        std::swap( from_order, to_order );
    }
}

inline std::uint32_t gfx_fnv_text( std::uint32_t h, const std::string& text )
{
    for ( const char c : text )
        h = ( h ^ static_cast< std::uint8_t >( c ) ) * 0x01000193u;
    return h;
}

inline std::uint32_t gfx_fnv_word( std::uint32_t h, std::uint32_t word )
{
    for ( int i = 0; i < 4; ++i )
        h = ( h ^ ( ( word >> ( 8 * i ) ) & 0xFFu ) ) * 0x01000193u;
    return h;
}

struct gfx_sort_t : ui_gfx_sort_t
{
    // ctor 0x539A60 (m_index 0 and the zeroed members are the base's); the two thread events are the hook's.
    explicit gfx_sort_t( gfx_sort_hooks_t& hooks )
        : m_hooks( hooks )
    {
        m_event[ 0 ] = hooks.thread( 0x30 );
        m_event[ 1 ] = hooks.thread( 0x30 );
        for ( buffer_t& b : m_buffers )
        {
            b.m_memory.resize( 8 );
            b.m_capacity = 8;
        }
    }

    // 0x539E90 (the argument byte): the working capacities; the argument starts the render thread (+8, g 0x759098 vt+0x20 with
    // the entry 0x539990 -> 0x53ABC0) - that thread is the platform's, so it stays a hook of the caller.
    void reserve( )
    {
        for ( buffer_t& b : m_buffers )
        {
            if ( b.m_capacity < 0x80000 )
            {
                b.m_memory.resize( 0x80000 );
                b.m_capacity = 0x80000;
            }
        }
        for ( std::vector< ui_gfx_command_t >& list : m_commands )
            list.reserve( 0x2000 );
        m_sort_keys.reserve( 0x2000 );
        m_order.reserve( 0x2000 );
    }

    // 0x423900 / 0x423920: the key's layer (bits 27..31 of +0x74) and screen (bits 18..21 of +0x74) fields.
    void set_layer( std::uint32_t v ) { m_key_hi = ( m_key_hi & 0x07FFFFFFu ) | ( v << 27 ); }
    std::uint32_t& key_high( ) { return m_key_hi; }   // +0x74
    void set_screen( std::uint32_t v ) { m_key_hi = ( m_key_hi & 0xFFC3FFFFu ) | ( v << 18 ); }

    // submit 0x429010 on a material and an optional chunk, by their ids.
    void submit( std::uint32_t type, std::uint32_t material, std::uint32_t chunk, std::uint32_t callback, float depth )
    {
        const gfx_material_t* m = m_hooks.material( material );
        const gfx_chunk_t* c = chunk ? m_hooks.chunk( chunk ) : nullptr;
        ui_gfx_sort_t::submit( type, material, m->m_key, chunk, c ? c->m_key : 0, callback, depth );
    }

    // 0x50B7B0 (type, material, chunk, callback, depth): the other key: type and material in the high word above the set's bits, 0x2000, the chunk's key astride the
    // two words, and the depth itself in 24 bits (not 1 - depth as in 0x429010, no bit 24). Same command.
    void submit_keyed( std::uint32_t type, std::uint32_t material, std::uint32_t chunk, std::uint32_t callback, float depth )
    {
        const gfx_material_t* m = m_hooks.material( material );
        const std::uint32_t bits = static_cast< std::uint32_t >( ui_fistp64_trunc( depth * 16777215.0f ) ) & 0xFFFFFFu;  // 0x6F784C
        std::uint32_t lo = m_key_lo | bits;
        std::uint32_t hi = ( ( ( type << 11 ) | m->m_key ) << 3 ) | m_key_hi | 0x2000u;
        if ( chunk )
        {
            const std::uint32_t key = m_hooks.chunk( chunk )->m_key;
            lo |= key << 24;
            hi |= key >> 8;
        }
        command( lo, hi, callback, material, chunk, type );
    }

    // 0x50B250's command: the key's bits alone, type 0, no material.
    void submit_bare( std::uint32_t callback ) { command( m_key_lo, m_key_hi, callback, 0, 0, 0 ); }

    void command( std::uint32_t lo, std::uint32_t hi, std::uint32_t callback, std::uint32_t material, std::uint32_t chunk, std::uint32_t type )
    {
        std::vector< ui_gfx_command_t >& list = m_commands[ m_index ];
        list.resize( list.size( ) + 1 );
        ui_gfx_command_t& c = list.back( );
        c.m_words[ 0 ] = lo;
        c.m_words[ 1 ] = hi;
        c.m_words[ 2 ] = callback;
        c.m_words[ 3 ] = static_cast< std::uint32_t >( m_offset );
        c.m_words[ 4 ] = material;
        c.m_words[ 5 ] = chunk;
        c.m_words[ 6 ] = ( c.m_words[ 6 ] & 0xFFFF0000u ) | ( type & 0xFFFFu );
    }

    // 0x53ACA0, the tick "Draw"'s callee: the frame time.
    void tick( float dt ) { m_time += dt; }

    // Slot 1 0x53A0C0: the tick "Decision" (0x53ACA0, tick( ) below) registered on g 0x759008. The dev statistics object it makes first when g 0x759020 is set (0x5A8B10): STOPPED.
    void start( ) { m_hooks.scheduler( 0x20, 0x53ACA0u ); }

    // Slot 2 0x53A1C0: wait( )'s body inline, the dev statistics' teardown (STOPPED), the tick off.
    void finish( )
    {
        wait( );
        m_hooks.scheduler( 0x28 );
    }

    // The dtor 0x539DC0: the two events closed; its frees are this object's own members here.
    void close_events( )
    {
        m_hooks.thread( 0x34, m_event[ 0 ] );
        m_hooks.thread( 0x34, m_event[ 1 ] );
    }

    // 0x53A400: the frame pending on the render thread, finished.
    void wait( )
    {
        if ( !m_pending )
            return;
        m_hooks.thread( 0x3C, m_event[ 1 ], 0xFFFFFFFFu );
        m_hooks.device( k_gfx_acquire );
        m_pending = false;
        m_hooks.device( k_gfx_flush );
    }

    // 0x53A250: the same, then the render thread told to leave (event +0x9C, then its +8 joined).
    void stop_thread( )
    {
        wait( );
        if ( !m_thread )
            return;
        m_hooks.thread( 0x38, m_event[ 0 ] );
        m_thread_stopping = true;
        m_hooks.thread( 0x24, m_thread );
    }

    // 0x53A2C0, the end of the game thread's frame: the pending draw waited for, the two buffers swapped, the light manager's
    // and the water renderer's own buffers flipped with them, the frame handed to the render thread (+4 = 0, +8 set) or drawn
    // here (+4 = 0, +8 = 0; with +4 set nobody draws), and the new fill buffer emptied.
    void flip( )
    {
        if ( m_hooks.m_has_profile )
            m_hooks.profile( 0x1C );
        wait( );
        // +0xE8 non-null -> 0x53B2C0, the dev statistics: not recreated (header comment).
        m_index = m_index == 0;
        m_render = m_render == 0;
        if ( m_hooks.m_has_profile )
            m_hooks.profile( 0x28 );
        if ( m_hooks.m_has_light )
            m_hooks.light_flip( );
        if ( m_hooks.m_has_water )
            m_hooks.water_flip( );
        if ( m_hooks.m_has_composer )
            m_hooks.composer( 0x24 );
        m_hooks.device( k_gfx_flush );
        if ( !m_single_thread )
        {
            if ( m_thread )
            {
                m_hooks.device( k_gfx_release );
                m_pending = true;
                m_hooks.thread( 0x38, m_event[ 0 ] );
            }
            else
                draw_frame( );
        }
        m_offset = 0;
        m_buffers[ m_index ].m_used = 0;
        m_commands[ m_index ].clear( );
        if ( m_hooks.m_has_profile )
            m_hooks.profile( 0x18 );
    }

    // Ours: drawstats' reading of a command. Only the world's sort (gfx_retail_t) sets m_stat_passes; the others keep their caller's scope.
    bool m_stat_passes = false;
    static drawstats::pass_t stat_pass( std::uint32_t hi, std::uint32_t callback )
    {
        if ( callback == 0x5BC6F0u || callback == 0x5BC7D0u || ( hi >> 27 ) >= 2u )   // the copy, the radial blur, layer 2 up
            return drawstats::k_post;
        if ( ( hi & 0x800000u ) == 0u )
            return drawstats::k_mirror;
        if ( callback == 0x594D70u )
            return drawstats::k_water;
        if ( callback == 0x58BA20u || callback == 0x58CE30u || callback == 0x5E8410u )   // pfx quads, trails, coronas
            return drawstats::k_pfx;
        return drawstats::k_scene;
    }

    // 0x53ACC0: what the render thread runs for a frame.
    void draw_frame( )
    {
        if ( !( m_hooks.device( k_gfx_begin ) & 0xFF ) )
            return;
        if ( m_hooks.m_has_profile )
            m_hooks.profile( 0x20 );
        {
            const spikes::scope_t spike( spikes::k_sort );
            sort( );
        }
        walk( );
        if ( m_hooks.m_has_profile )
            m_hooks.profile( 0x24 );
        m_hooks.device( k_gfx_end );
    }

    // 0x53AD10: the keys of the drawn buffer into +0x80, the order 0, 1, 2 .. into +0x8C, radix sorted.
    void sort( )
    {
        const std::vector< ui_gfx_command_t >& list = m_commands[ m_render ];
        const int n = static_cast< int >( list.size( ) );
        m_sort_keys.resize( list.size( ) );
        m_order.resize( list.size( ) );
        for ( int i = 0; i < n; ++i )
        {
            m_sort_keys[ i ] = list[ i ].m_words[ 0 ] | ( static_cast< std::uint64_t >( list[ i ].m_words[ 1 ] ) << 32 );
            m_order[ i ] = static_cast< std::uint32_t >( i );
        }
        gfx_radix_sort( m_sort_keys.data( ), n, m_order.data( ) );
    }

    // 0x507240: the state a frame starts from.
    void default_state( )
    {
        m_hooks.device( k_gfx_depth, 1 );
        m_hooks.device( k_gfx_depth_write, 1 );
        m_hooks.device( k_gfx_depth_func, 1 );
        m_hooks.device( k_gfx_blend, 0 );
        m_hooks.device( k_gfx_blend_func, 4, 5 );
        m_hooks.device( k_gfx_cull, 1 );
        for ( std::uint32_t i = 0; i < 8; ++i )
            m_hooks.device( k_gfx_texture, i, 0 );
    }

    // 0x53ADC0: the sorted commands in order. A change of material binds it (0x53AF80), a change of chunk binds it (0x53B070), a
    // change of type applies the table - the "previous type" starts at 2, so a frame's first type-2 command applies nothing
    // - and each command's callback runs with its record. A null material or chunk binds nothing but does count as the previous.
    void walk( )
    {
        spikes::scope_t spike( m_stat_passes ? spikes::k_walk_other : spikes::k_walk_ui );
        default_state( );
        std::memcpy( m_stats_last, m_stats, sizeof( m_stats ) );
        std::memset( m_stats, 0, sizeof( m_stats ) );
        std::uint32_t last_material = 0, last_chunk = 0;
        std::uint16_t last_type = 2;
        const std::vector< ui_gfx_command_t >& list = m_commands[ m_render ];
        for ( const std::uint32_t at : m_order )
        {
            const ui_gfx_command_t& c = list[ at ];
            if ( m_stat_passes )   // ours: drawstats' pass from the key (0x800000 clear = the reflection's view) and the callback
            {
                const drawstats::pass_t pass = stat_pass( c.m_words[ 1 ], c.m_words[ 2 ] );
                drawstats::set( pass );
                spike.retarget( spikes::k_walk_other + pass );
            }
            const std::uint32_t material = c.m_words[ 4 ], chunk = c.m_words[ 5 ];
            if ( material != last_material )
            {
                if ( material )
                {
                    ++m_stats[ 0 ];
                    bind_material( last_material ? m_hooks.material( last_material ) : nullptr, *m_hooks.material( material ) );
                }
                last_material = material;
            }
            if ( chunk != last_chunk )
            {
                if ( chunk )
                {
                    ++m_stats[ 1 ];
                    bind_chunk( last_chunk ? m_hooks.chunk( last_chunk ) : nullptr, *m_hooks.chunk( chunk ) );
                }
                last_chunk = chunk;
            }
            const std::uint16_t type = static_cast< std::uint16_t >( c.m_words[ 6 ] );
            if ( type != last_type )
            {
                last_type = type;
                const gfx_layer_t& layer = k_gfx_layers[ type ];
                m_hooks.device( k_gfx_blend, layer.blend );
                m_hooks.device( k_gfx_depth, layer.depth );
                m_hooks.device( k_gfx_depth_write, layer.write );
                m_hooks.device( k_gfx_blend_func, layer.src, layer.dst );
            }
            m_drawn_lo = c.m_words[ 0 ];
            m_drawn_hi = c.m_words[ 1 ];
            m_hooks.draw( c.m_words[ 2 ], m_buffers[ m_render ].m_memory.data( ) + c.m_words[ 3 ] );
        }
    }

    // 0x53AF80 (previous material, may be null; new one).
    void bind_material( const gfx_material_t* last, const gfx_material_t& m )
    {
        const std::uint32_t last_constants = last ? last->m_constants_id : 0, last_textures = last ? last->m_textures_id : 0;
        const std::uint32_t last_program = last ? last->m_program : 0, last_declaration = last ? last->m_declaration : 0;
        if ( m.m_program != last_program )
        {
            ++m_stats[ 2 ];
            m_hooks.device( k_gfx_program, m.m_program );
            m_hooks.program( m.m_program, k_gfx_bind );
            bind_frame_constants( m );
            ++m_stats[ 3 ];
            bind_constants( m );
            ++m_stats[ 4 ];
            bind_textures( m );
        }
        else
        {
            if ( m.m_constants_id != last_constants )
            {
                ++m_stats[ 3 ];
                bind_constants( m );
            }
            if ( m.m_textures_id != last_textures )
            {
                ++m_stats[ 4 ];
                bind_textures( m );
            }
        }
        if ( m.m_declaration != last_declaration )
        {
            m_hooks.device( k_gfx_declaration, m.m_declaration );
            ++m_stats[ 5 ];
        }
    }

    // 0x53B0F0: the frame constants the new shader takes. A float is passed by value (its bits), the rest as the source's name.
    void bind_frame_constants( const gfx_material_t& m )
    {
        const std::uint32_t p = m.m_program;
        const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
        const std::uint32_t light = m_hooks.m_has_light ? k_gfx_light : 0;
        if ( m.m_view_matrix )
            m_hooks.program( p, k_gfx_set_matrix, m.m_view_matrix, k_gfx_view_matrix );
        if ( m.m_view_proj_matrix )
            m_hooks.program( p, k_gfx_set_matrix, m.m_view_proj_matrix, k_gfx_view_proj_matrix );
        if ( m.m_eye_world )
            m_hooks.program( p, k_gfx_set_vec3, m.m_eye_world, k_gfx_eye_world );
        if ( m.m_far_plane )
            m_hooks.program( p, k_gfx_set_float, m.m_far_plane, bits( m_hooks.m_far_plane ) );
        if ( m.m_time )
            m_hooks.program( p, k_gfx_set_float, m.m_time, bits( m_time ) );
        if ( m.m_amb_light_color )
            m_hooks.program( p, k_gfx_set_vec4, m.m_amb_light_color, light + 0xCC );
        if ( m.m_dir_light_world )
            m_hooks.program( p, k_gfx_set_vec3, m.m_dir_light_world, light + 0xE0 );
        if ( m.m_dir_light_front_color )
            m_hooks.program( p, k_gfx_set_vec4, m.m_dir_light_front_color, light + 0xF0 );
        if ( m.m_dir_light_back_color )
            m_hooks.program( p, k_gfx_set_vec4, m.m_dir_light_back_color, light + 0xF4 );
        if ( m.m_dir_light_specular_color )
            m_hooks.program( p, k_gfx_set_vec4, m.m_dir_light_specular_color, light + 0xF8 );
        if ( m.m_fog_start )
            m_hooks.program( p, k_gfx_set_float, m.m_fog_start, bits( m_hooks.m_fog_start ) );
        if ( m.m_fog_inv_range )
            m_hooks.program( p, k_gfx_set_float, m.m_fog_inv_range, bits( 1.0f / ( m_hooks.m_fog_end - m_hooks.m_fog_start ) ) );
        if ( m.m_fog_color )
            m_hooks.program( p, k_gfx_set_vec4, m.m_fog_color, k_gfx_fog_color );
        if ( m.m_clip_plane )
        {
            const std::uint32_t v = m_hooks.device( k_gfx_slot_80 );
            m_hooks.program( p, k_gfx_set_value, m.m_clip_plane, v );
        }
        if ( m.m_composer && m_hooks.m_has_composer )
            m_hooks.composer( 0x1C, m.m_composer, p );
    }

    // 0x541C40: the material's own constants, by kind; a kind past 2 sets nothing.
    void bind_constants( const gfx_material_t& m )
    {
        for ( const gfx_constant_t& c : m.m_constants )
        {
            if ( c.m_kind == 0 )
                m_hooks.program( m.m_program, k_gfx_set_int, c.m_handle, c.m_value[ 0 ] );
            else if ( c.m_kind == 1 )
                m_hooks.program( m.m_program, k_gfx_set_float, c.m_handle, c.m_value[ 0 ] );
            else if ( c.m_kind == 2 )
                m_hooks.program( m.m_program, k_gfx_set_vector, c.m_handle, 0, c.m_value );
        }
    }

    // 0x541CB0: the material's textures.
    void bind_textures( const gfx_material_t& m )
    {
        for ( const gfx_material_t::texture_t& t : m.m_textures )
            m_hooks.device( k_gfx_texture, t.m_sampler, m_hooks.texture_handle( t.m_texture ) );
    }

    // 0x53B070 (previous chunk, may be null; new one).
    void bind_chunk( const gfx_chunk_t* last, const gfx_chunk_t& c )
    {
        const std::uint32_t last_stream = last ? last->m_stream : 0, last_indices = last ? last->m_indices : 0;
        const std::uint32_t last_stride = last ? last->m_stride : 0;
        if ( c.m_stream != last_stream || c.m_stride != last_stride )
        {
            m_hooks.device( k_gfx_stream, c.m_stream, 0, c.m_stride );
            ++m_stats[ 6 ];
        }
        if ( c.m_indices != last_indices )
        {
            m_hooks.device( k_gfx_indices, c.m_indices );
            ++m_stats[ 7 ];
        }
    }

    // 0x541390: FNV-1a over each constant's name (no terminator), its kind and its three value words.
    static std::uint32_t hash_constants( const gfx_material_desc_t& d )
    {
        std::uint32_t h = 0x811C9DC5u;
        for ( const gfx_material_desc_t::constant_t& c : d.m_constants )
        {
            h = gfx_fnv_text( h, c.m_name );
            h = gfx_fnv_word( h, c.m_kind );
            for ( const std::uint32_t v : c.m_value )
                h = gfx_fnv_word( h, v );
        }
        return h;
    }

    // 0x5415D0: the same over each texture's name, second string, kind and asset name.
    static std::uint32_t hash_textures( const gfx_material_desc_t& d )
    {
        std::uint32_t h = 0x811C9DC5u;
        for ( const gfx_material_desc_t::texture_t& t : d.m_textures )
        {
            h = gfx_fnv_text( h, t.m_name );
            h = gfx_fnv_text( h, t.m_slot );
            h = gfx_fnv_word( h, t.m_kind );
            h = gfx_fnv_text( h, t.m_asset );
        }
        return h;
    }

    // 0x53A440 (program, declaration, desc): the one material of that key, its count raised, or a new one. The array +0x10 stays
    // DESCENDING by (program, declaration, constants hash, textures hash), all unsigned, by binary search; a new material goes in
    // at the search's end, and every material's key is renumbered to its index. SUBSTITUTION: retail compares the program and
    // declaration POINTERS, here their ids, so with real assets the two orders can differ (only a key tie-break depends on it).
    gfx_material_t* material_create( std::uint32_t program, std::uint32_t declaration, const gfx_material_desc_t& desc )
    {
        const std::uint32_t a = hash_constants( desc ), b = hash_textures( desc );
        const auto below = []( std::uint32_t x, std::uint32_t y ) { return x < y; };
        int low = 0, high = static_cast< int >( m_materials.size( ) );
        while ( low < high )
        {
            const int mid = ( low + high ) >> 1;
            gfx_material_t& e = *m_materials[ mid ];
            bool less;
            if ( e.m_program != program )
                less = below( e.m_program, program );
            else if ( e.m_declaration != declaration )
                less = below( e.m_declaration, declaration );
            else if ( e.m_constants_id != a )
                less = below( e.m_constants_id, a );
            else if ( e.m_textures_id != b )
                less = below( e.m_textures_id, b );
            else
            {
                ++e.m_refs;
                return &e;
            }
            if ( less )
                high = mid;
            else
                low = mid + 1;
        }
        auto made = std::make_unique< gfx_material_t >( );
        gfx_material_t* m = made.get( );
        resolve( *m, program, declaration, desc );
        if ( m_hooks.m_has_composer )
            m->m_composer = m_hooks.composer_add( *m );
        m->m_constants_id = a;
        m->m_textures_id = b;
        m_materials.insert( m_materials.begin( ) + low, std::move( made ) );
        ++m_material_count;
        for ( std::size_t i = 0; i < m_materials.size( ); ++i )
            m_materials[ i ]->m_key = static_cast< std::uint32_t >( i );
        return m;
    }

    // 0x541680: the references, the constants the shader has (vt+4 non-zero), the textures it has (vt+8 not negative) with their
    // assets, then the frame constants by name.
    void resolve( gfx_material_t& m, std::uint32_t program, std::uint32_t declaration, const gfx_material_desc_t& desc )
    {
        m.m_program = program;
        m_hooks.retain( program );
        m.m_declaration = declaration;
        m_hooks.retain( declaration );
        for ( const gfx_material_desc_t::constant_t& c : desc.m_constants )
        {
            const std::uint32_t handle = m_hooks.program_constant( program, c.m_name );
            if ( !handle )
                continue;
            gfx_constant_t k;
            k.m_name = c.m_name;
            k.m_handle = handle;
            k.m_kind = c.m_kind;
            std::memcpy( k.m_value, c.m_value, sizeof( k.m_value ) );
            m.m_constants.push_back( k );
        }
        for ( const gfx_material_desc_t::texture_t& t : desc.m_textures )
        {
            const std::int32_t sampler = m_hooks.program_sampler( program, t.m_name );
            if ( sampler < 0 )
                continue;
            gfx_material_t::texture_t k;
            k.m_name = t.m_name;
            k.m_kind = t.m_kind;
            k.m_sampler = static_cast< std::uint32_t >( sampler );
            if ( t.m_kind == 0 || t.m_kind == 1 )
                k.m_texture = m_hooks.asset_get( t.m_kind, t.m_asset );
            m.m_textures.push_back( k );
        }
        for ( const gfx_frame_constant_t& f : k_gfx_frame_constants )
            m.*f.m_field = f.m_sampler ? static_cast< std::uint32_t >( m_hooks.program_sampler( program, f.m_name ) )
                                       : m_hooks.program_constant( program, f.m_name );
    }

    // 0x53A710: the last reference waits for the pending frame, leaves the composer, the array (0x4AB7D0; the rest keep their keys)
    // and the live count; the dtor 0x541BE0 releases the program, the textures, the declaration.
    void material_release( gfx_material_t* m )
    {
        if ( !m || --m->m_refs )
            return;
        wait( );
        if ( m->m_composer && m_hooks.m_has_composer )
            m_hooks.composer_remove( m->m_composer );
        const auto at = std::find_if( m_materials.begin( ), m_materials.end( ), [ m ]( const std::unique_ptr< gfx_material_t >& e ) { return e.get( ) == m; } );
        std::unique_ptr< gfx_material_t > gone = std::move( *at );
        m_materials.erase( at );
        m_hooks.release( gone->m_program );
        for ( const gfx_material_t::texture_t& t : gone->m_textures )
            m_hooks.asset_put( t.m_texture );
        m_hooks.release( gone->m_declaration );
        --m_material_count;
    }

    // 0x53A7C0 (stream, indices, stride): the same cache for the chunks, ASCENDING by stream (unsigned), stride (SIGNED), indices.
    gfx_chunk_t* chunk_create( std::uint32_t stream, std::uint32_t indices, std::uint32_t stride )
    {
        const auto less = []( std::uint32_t a0, std::uint32_t a1, std::uint32_t a2, std::uint32_t b0, std::uint32_t b1, std::uint32_t b2 )
        {
            if ( a0 != b0 )
                return a0 < b0;
            if ( a2 != b2 )
                return static_cast< std::int32_t >( a2 ) < static_cast< std::int32_t >( b2 );
            return a1 < b1;
        };
        int low = 0, high = static_cast< int >( m_chunks.size( ) );
        while ( low < high )
        {
            const int mid = ( low + high ) >> 1;
            gfx_chunk_t& e = *m_chunks[ mid ];
            if ( less( stream, indices, stride, e.m_stream, e.m_indices, e.m_stride ) )
                high = mid;
            else if ( less( e.m_stream, e.m_indices, e.m_stride, stream, indices, stride ) )
                low = mid + 1;
            else
            {
                ++e.m_refs;
                return &e;
            }
        }
        auto made = std::make_unique< gfx_chunk_t >( );
        gfx_chunk_t* c = made.get( );
        c->m_stream = stream;
        c->m_indices = indices;
        c->m_stride = stride;
        m_hooks.retain( stream );
        m_hooks.retain( indices );
        m_chunks.insert( m_chunks.begin( ) + low, std::move( made ) );
        ++m_chunk_count;
        for ( std::size_t i = 0; i < m_chunks.size( ); ++i )
            m_chunks[ i ]->m_key = static_cast< std::uint32_t >( i );
        return c;
    }

    // 0x53A920.
    void chunk_release( gfx_chunk_t* c )
    {
        if ( !c || --c->m_refs )
            return;
        wait( );
        const auto at = std::find_if( m_chunks.begin( ), m_chunks.end( ), [ c ]( const std::unique_ptr< gfx_chunk_t >& e ) { return e.get( ) == c; } );
        std::unique_ptr< gfx_chunk_t > gone = std::move( *at );
        m_chunks.erase( at );
        m_hooks.release( gone->m_stream );
        m_hooks.release( gone->m_indices );
        --m_chunk_count;
    }

    gfx_sort_hooks_t& m_hooks;
    std::vector< std::unique_ptr< gfx_material_t > > m_materials;  // +0x10
    std::uint32_t m_material_count = 0;                            // +0x1C
    std::vector< std::unique_ptr< gfx_chunk_t > > m_chunks;        // +0x20
    std::uint32_t m_chunk_count = 0;                               // +0x2C
    bool m_single_thread = false;                  // +4
    std::uint32_t m_thread = 0;                    // +8
    bool m_pending = false;                        // +0xC
    std::int32_t m_render = 1;                     // +0x68
    std::uint32_t m_drawn_lo = 0, m_drawn_hi = 0;  // +0x78 / +0x7C
    std::vector< std::uint64_t > m_sort_keys;      // +0x80
    std::vector< std::uint32_t > m_order;          // +0x8C
    float m_time = 0.0f;                           // +0x98
    std::uint32_t m_event[ 2 ] = { };              // +0x9C / +0xA0
    bool m_thread_stopping = false;                // +0xA4
    std::uint32_t m_stats[ 8 ] = { };              // +0xA8
    std::uint32_t m_stats_last[ 8 ] = { };         // +0xC8
};
