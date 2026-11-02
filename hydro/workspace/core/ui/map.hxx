#pragma once

#include <workspace/core/ui/draw.hxx>

#include <cmath>
#include <cstdint>
#include <functional>
#include <cstring>
#include <vector>

// The race HUD's minimap draw as data: VuHUDMiniMapEntity (ctor 0x426D70,
// vtable 0x738E10). Its VuUIDrawComponent's delegate (vtable 0x738DF0 ->
// 0x4077ED, jmp [vt+0x34]) runs slot 13 0x427BD0 with the HUD's alpha - which
// it never reads. Transliterated from the disassembly: two records in
// VuGfxSort's memory and their commands. Their callbacks 0x428060 / 0x428070
// (the map and the boat icons' shader draws at submit time) are the GPU side
// and are NOT recreated.

// What 0x427BD0 reads of one boat (VuBoatManager g 0x758FB8). UNKNOWN on the
// boat side: nothing recreated fills these yet - an input.
struct ui_map_boat_t
{
    float m_position[ 2 ] = { };    // +0x50 -> +0x80, +0x84
    float m_direction[ 2 ] = { };   // +0x50 -> +0x50, +0x54
    float m_heading[ 2 ] = { };     // +0x224 -> +0x130, +0x134 (read for the local boat only)
    bool m_answer = false;          // HOOK: the +0x220 object's slot 12 (an unread Vu virtual)
    std::int32_t m_kind = 0;        // +0x490
};

// The manager: present or not (g 0x758FB8 null), the local boat (+0x58 the
// index, below +0x10 the count, +0xC the array, a null slot is none) and every
// boat (+0x18 the array, +0x1C the count).
struct ui_map_world_t
{
    bool m_manager = false;
    bool m_local_present = false;
    ui_map_boat_t m_local;
    std::vector< ui_map_boat_t > m_boats;
};

// The fields the draw reads (ctor 0x426D70).
struct ui_minimap_t
{
    float m_screen_scale[ 2 ] = { 1080.0f, 720.0f };      // +0x60 "Screen Scale"
    float m_rect[ 4 ] = { 0.0f, 0.0f, 100.0f, 100.0f };   // +0x68 X, Y, Width, Height
    ui_anchor_t m_anchor;                                  // +0x78 anchors, +0x80 / +0x84 ratios
    std::uint32_t m_material = 0;                          // +0x8C the pipeline state (0x53A440) handed to the sort
    std::uint32_t m_material_key = 0;                      // its +0x6C8
    std::uint32_t m_icon = 0;                              // +0xB0 the icon texture asset's +0x58 (opaque)
    float m_depth = 0.0f;                                  // the draw component (+0x5C) +0x14 "Depth"
    std::uint32_t m_self = 0;                              // the entity (both records' +0)
};

constexpr std::uint32_t k_ui_map_callback = 0x428060u;     // the map record's draw at submit (GPU side)
constexpr std::uint32_t k_ui_icons_callback = 0x428070u;   // the icons record's
constexpr int k_ui_map_record = 96;
constexpr int k_ui_icons_record = 680;
constexpr int k_ui_icons_fit = ( k_ui_icons_record - 0x68 ) / 24;   // 24 entries fit the record

// The heading 0x427BD0 stores: (float) __libm_sse2_atan2( (double) y, (double) x ).
// SUBSTITUTION: the host CRT's atan2 for the game's CRT DLL import (the oracle
// serves the import with the same host function, so its check cannot tell them apart).
inline float ui_map_heading( float y, float x )
{
    return static_cast< float >( std::atan2( static_cast< double >( y ), static_cast< double >( x ) ) );
}

// 0x427BD0 (thiscall; the alpha on the stack, unread). env.m_matrix is the
// matrix stack's top, env.m_texture_material the icons' material (***(VuGfxUtil
// +4) +0x14). false: a path not recreated - a boat count the 680-byte record
// cannot hold (above 24: retail writes past it into the sort's memory) or a
// negative one (the swap reads before it).
inline bool ui_minimap_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_crop_t& crop, const ui_minimap_t& m,
                             const ui_map_world_t& world )
{
    const float one = 1.0f;
    const float ix = one / m.m_screen_scale[ 0 ];
    const float iy = one / m.m_screen_scale[ 1 ];
    float rect[ 4 ] = { ix * m.m_rect[ 0 ], m.m_rect[ 1 ] * iy, m.m_rect[ 2 ] * ix, m.m_rect[ 3 ] * iy };
    ui_anchor_transform( m.m_anchor, crop, rect, rect );
    float position[ 2 ] = { 0.0f, 0.0f };
    float heading = 0.0f;
    if ( world.m_manager && world.m_local_present )
    {
        std::memcpy( position, world.m_local.m_position, 8 );
        heading = ui_map_heading( world.m_local.m_heading[ 1 ], world.m_local.m_heading[ 0 ] );
    }
    const float depth = m.m_depth * 0.005f + 0.5f;   // 0x6F75C4, 0x6F765C
    const auto head = [ & ]( std::uint8_t* r )
    {
        std::memcpy( r + 0, &m.m_self, 4 );
        std::memcpy( r + 4, env.m_matrix, 64 );
        std::memcpy( r + 0x44, rect, 16 );
        std::memcpy( r + 0x54, position, 8 );
        std::memcpy( r + 0x5C, &heading, 4 );
    };
    head( sort.allocate( k_ui_map_record ) );
    sort.submit( 17, m.m_material, m.m_material_key, 0, 0, k_ui_map_callback, depth );
    if ( !world.m_manager )
        return true;
    const std::int32_t count = static_cast< std::int32_t >( world.m_boats.size( ) );
    if ( count > k_ui_icons_fit )
        return false;
    std::uint8_t* r = sort.allocate( k_ui_icons_record );
    head( r );
    std::memcpy( r + 0x60, &one, 4 );
    std::memcpy( r + 0x64, &count, 4 );
    for ( std::int32_t i = 0; i < count; ++i )
    {
        const ui_map_boat_t& b = world.m_boats[ static_cast< std::size_t >( i ) ];
        std::uint8_t* e = r + 0x68 + 24 * i;
        std::memcpy( e + 0, b.m_position, 8 );
        const float h = ui_map_heading( b.m_direction[ 1 ], b.m_direction[ 0 ] );
        std::memcpy( e + 8, &h, 4 );
        std::memcpy( e + 0xC, &m.m_icon, 4 );
        e[ 0x10 ] = 0x00;   // word 0x100
        e[ 0x11 ] = 0x01;
        std::uint32_t colour = 0xFF00FF7Du;   // 0x7362E4
        if ( b.m_answer )
        {
            colour = 0xFFFFB400u;   // 0x7362E0
            if ( b.m_kind == 0 )
                colour = 0xFFFF4040u;   // 0x7362E8
            else if ( b.m_kind == 1 )
                colour = 0xFF4040FFu;   // 0x7362EC
        }
        std::memcpy( e + 0x12, &colour, 4 );
    }
    if ( count == 0 )
        return true;
    // the first and the last entries swapped (three qwords each way)
    std::uint8_t first[ 24 ], last[ 24 ];
    std::memcpy( first, r + 0x68, 24 );
    std::memcpy( last, r + 0x68 + 24 * ( count - 1 ), 24 );
    std::memcpy( r + 0x68, last, 24 );
    std::memcpy( r + 0x68 + 24 * ( count - 1 ), first, 24 );
    sort.submit( 17, env.m_texture_material, env.m_texture_material_key, 0, 0, k_ui_icons_callback, depth );
    return true;
}

// 0x427FB0 (the Vu2dLayoutComponent draw; byte; ret 4): with the byte the rect
// over the screen scale, white (0x507A00, NOT anchored); then the draw (slot 13
// vt+0x34) at 1.0 - `draw`.
inline void ui_minimap_layout_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_minimap_t& m, std::uint8_t layout, const std::function< void( float ) >& draw )
{
    if ( layout )
    {
        const float one = 1.0f;
        const float ix = one / m.m_screen_scale[ 0 ];
        const float iy = one / m.m_screen_scale[ 1 ];
        const float rect[ 4 ] = { m.m_rect[ 0 ] * ix, m.m_rect[ 1 ] * iy, m.m_rect[ 2 ] * ix, m.m_rect[ 3 ] * iy };
        ui_rect_draw( sort, env, 0xFFFFFFFFu, rect, m.m_depth * 0.005f + 0.5f );
    }
    draw( 1.0f );
}
