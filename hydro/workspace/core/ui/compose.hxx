#pragma once

#include <workspace/core/entity/node.hxx>
#include <workspace/core/ui/draw.hxx>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

// VuHUDEntity's draw (0x423A70) and the child walk it runs (0x628070),
// transliterated: the HUD composes its children's draws under the crop matrix.
// The children's own draws are the caller's (only the text base's, core/ui
// draw.hxx ui_text_draw, is recreated); this file decides WHICH draw runs, with
// what alpha, under which stack tops and sort key bits.

// VuGfxUtil (g 0x75902C) +0x20 / +0x34: a std::deque of 64-byte matrices and
// one of floats - the "tops" the draws read (ui_font_draw_env_t's m_matrix /
// m_top). Push 0x50B4B0 / 0x50B530 (grow when full, then the element at
// offset + size, size++); pop (inlined: size-- when non-zero, offset = 0 when
// it reaches 0). SUBSTITUTION: std::vector for MSVC's std::deque - only push,
// pop, top and size are observable.
struct ui_gfx_stacks_t
{
    std::vector< std::array< float, 16 > > m_matrices;  // +0x20 (size +0x30)
    std::vector< float > m_floats;                      // +0x34 (size +0x44)
};

// VuUIImpl +0x8C (0x81A184): 0x626190 stores 1.0; the HUD draw pushes it on
// the float stack (the font draw stores that top in its record +88).
struct ui_impl_t
{
    ui_crop_t m_crop;          // +12 (0x81A104)
    float m_inverse[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };   // +0x4C (0x81A144)
    float m_float = 1.0f;      // +0x8C (0x81A184)
    float m_aspect = 1.7777778f;   // +0x90 (0x81A188: 0x626190 stores 0x3FE38E39 = 16 / 9; the image draw 0x629300 reads it)
};

// The VuUIDrawComponent descriptor 0x736E78: 0x628070 takes the first
// component whose getType chain holds it. Only VuUIDrawComponent itself (vtable
// 0x738A44) does over every captured type (census plug_probe.json's component
// chains), so: the first component of that vtable.
constexpr std::uint32_t k_ui_draw_component_vtable = 0x738A44u;

// 0x628070 (this = the entity, the alpha in xmm1): each DIRECT child in +0x38
// order (the count re-read each step), its first UI draw component's +0x10
// object, slot 2 with the alpha - `draw`(child, that component's index in
// +0x48 order, alpha). A child without one is skipped.
inline void ui_draw_children( const entity_t& e, float alpha, const std::function< void( const entity_t&, int, float ) >& draw )
{
    for ( std::size_t k = 0; k < e.m_children.size( ); ++k )
    {
        const entity_t& child = *e.m_children[ k ];
        for ( std::size_t c = 0; c < child.m_components.size( ); ++c )
            if ( child.m_components[ c ].m_vtable == k_ui_draw_component_vtable )
            {
                draw( child, static_cast< int >( c ), alpha );
                break;
            }
    }
}

// VuUIContainerEntity's draw (ctor 0x62F880, vtable 0x6F74F0; its draw
// component's delegate 0x6F7524 -> 0x404A00) 0x62F7E0: the alpha into xmm1,
// then the child walk 0x628070 - nothing else.
inline void ui_container_draw( const entity_t& e, float alpha, const std::function< void( const entity_t&, int, float ) >& draw )
{
    ui_draw_children( e, alpha, draw );
}

// 0x423A70's key bits on VuGfxSort +0x74 (+0x70 re-stored unchanged).
inline std::uint32_t ui_hud_key_hi( std::uint32_t hi )
{
    hi &= 0x7FFFFFFu;
    hi |= 0x18000000u;
    hi &= 0xFCFFFFFFu;
    hi &= ~0x800000u;
    hi &= 0xFFC3FFFFu;
    hi |= 0x240000u;
    return hi;
}

// VuUIScreenEntity's draw, slot 13 0x629D60 (thiscall; the alpha on the
// stack): the key bits with the screen's +0x5C (`layer`, 4 from the ctor
// 0x6299A0) as bits 27..31, the crop matrix and VuUIImpl +0x8C pushed (as the
// HUD's), then the screen's OWN first UI draw component (+0x48 order; its
// +0x10 object's slot 2 with the alpha) - `draw`(screen, its index, alpha) -
// then the child walk 0x628070, then both popped (floats first).
inline std::uint32_t ui_screen_key_hi( std::uint32_t hi, std::uint32_t layer )
{
    hi &= 0x7FFFFFFu;
    hi |= layer << 27;
    hi &= 0xFCFFFFFFu;
    hi &= 0xFF7FFFFFu;
    hi &= 0xFFC3FFFFu;
    hi |= 0x280000u;
    return hi;
}

inline void ui_screen_draw( ui_gfx_sort_t& sort, ui_gfx_stacks_t& stacks, const ui_impl_t& impl, const entity_t& screen, std::uint32_t layer, float alpha,
                            const std::function< void( const entity_t&, int, float ) >& draw )
{
    sort.m_key_hi = ui_screen_key_hi( sort.m_key_hi, layer );
    std::array< float, 16 > m;
    std::memcpy( m.data( ), impl.m_crop.m_matrix, sizeof( impl.m_crop.m_matrix ) );
    stacks.m_matrices.push_back( m );
    stacks.m_floats.push_back( impl.m_float );
    for ( std::size_t c = 0; c < screen.m_components.size( ); ++c )
        if ( screen.m_components[ c ].m_vtable == k_ui_draw_component_vtable )
        {
            draw( screen, static_cast< int >( c ), alpha );
            break;
        }
    ui_draw_children( screen, alpha, draw );
    if ( !stacks.m_floats.empty( ) )
        stacks.m_floats.pop_back( );
    if ( !stacks.m_matrices.empty( ) )
        stacks.m_matrices.pop_back( );
}

// 0x423A70 (thiscall; the alpha on the stack, passed on in xmm1): the key bits,
// VuUIImpl's guard (0x626190 on first use - the caller's ui_impl_t), the crop
// matrix pushed on the matrix stack and +0x8C on the float stack, the child
// walk, then both popped (floats first).
inline void ui_hud_draw( ui_gfx_sort_t& sort, ui_gfx_stacks_t& stacks, const ui_impl_t& impl, const entity_t& hud, float alpha,
                         const std::function< void( const entity_t&, int, float ) >& draw )
{
    sort.m_key_hi = ui_hud_key_hi( sort.m_key_hi );
    std::array< float, 16 > m;
    std::memcpy( m.data( ), impl.m_crop.m_matrix, sizeof( impl.m_crop.m_matrix ) );
    stacks.m_matrices.push_back( m );
    stacks.m_floats.push_back( impl.m_float );
    ui_draw_children( hud, alpha, draw );
    if ( !stacks.m_floats.empty( ) )
        stacks.m_floats.pop_back( );
    if ( !stacks.m_matrices.empty( ) )
        stacks.m_matrices.pop_back( );
}
