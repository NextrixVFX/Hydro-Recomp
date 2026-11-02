#pragma once

#include <workspace/core/ui/button.hxx>
#include <workspace/core/ui/draw.hxx>
#include <workspace/core/ui/gauge.hxx>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

extern "C" void x87_sincos( float x, float* c, float* s );

// The race HUD's touch controls as draw data, transliterated from the
// disassembly (HUDs/*GameHUD's "#HUD/Controls" template):
// - VuHUDButtonEntity (ctor 0x424A20): +0x64 Screen Scale, +0x6C Image X / Y /
//   Width / Height, +0x7C Touch X / Y / Width / Height, +0xBC Horizontal / +0xC0
//   Vertical Anchor, +0xC4 / +0xC8 the ratios, +0xCC / +0xD0 the Texture /
//   Pressed Texture Asset properties (+0x4C the asset, its +0x58 the texture),
//   +0xE0 the touch state (1 = pressed; written by its touch code - an input
//   here). VuHUDPauseButtonEntity (ctor 0x426120, vtable 0x738AC4): visible
//   slot 15 0x426330, draw slot 16 0x4257A0. VuHUDRecoverButtonEntity (ctor
//   0x425B60, vtable 0x738B24; Boing Rate / Scale Min / Max / Duration +0xE4..
//   +0xF0, +0xF4 the current boing scale - its tick's, an input): visible slot
//   15 0x425F50, draw slot 16 0x425F70.
// - VuHUDOnScreenControlEntity (ctor 0x4292F0; VuHUDOnScreenAxisEntity 0x739020 /
//   VuHUDOnScreenButtonEntity 0x739058 share the draw 0x42A0C0): +0x60 Screen
//   Scale, +0x68 Image rect, +0x78 Touch rect, +0xB8 the anchor block, +0xC8..
//   +0xCB Enable for Tilt / Touch / GamePad / Keyboard, +0xCC / +0xD0 the
//   textures, +0xD4 pressed (its touch code's - an input).
// The globals they read are inputs: g 0x758FB8 (+0x28, the pause button's
// test) and the control-method manager g 0x758FC0 (+0x18: 0 gamepad, 1
// keyboard, 2 tilt, 3 touch).

struct ui_hud_control_t
{
    float m_scale[ 2 ] = { 1080.0f, 720.0f };
    float m_image[ 4 ] = { };
    ui_anchor_t m_anchor;
    std::uint8_t m_enable[ 4 ] = { 1, 1, 1, 1 };   // on-screen controls only: Tilt, Touch, GamePad, Keyboard
    float m_depth = 0.0f;                          // the draw component's Depth
};

// The inputs: the two textures (null = no asset / no texture), the touch
// state, the boing scale and the two globals.
struct ui_hud_control_inputs_t
{
    const ui_texture_t* m_texture = nullptr;
    const ui_texture_t* m_pressed_texture = nullptr;
    std::int32_t m_state = 0;          // +0xE0 (buttons) / the +0xD4 byte (on-screen)
    float m_boing = 1.0f;              // +0xF4 (recover)
    bool m_race_present = false;       // g 0x758FB8 non-null
    std::uint32_t m_race_28 = 0;       // its +0x28
    bool m_method_present = false;     // g 0x758FC0 non-null
    std::int32_t m_method = 0;         // its +0x18
};

// 0x426330: no g 0x758FB8, or its +0x28 non-zero.
inline bool ui_hud_pause_visible( const ui_hud_control_inputs_t& in ) { return !in.m_race_present || in.m_race_28 != 0; }

// 0x425F50: no control-method manager, or a method other than 0 / 1.
inline bool ui_hud_recover_visible( const ui_hud_control_inputs_t& in )
{
    return !in.m_method_present || ( in.m_method != 0 && in.m_method != 1 );
}

// The shared tail: the image rect over the screen scale through the anchor
// transform 0x6282D0, (the recover button's boing), colour 0x??FFFFFF with the
// alpha byte fistp( alpha x 255 ), uv (0, 0, 1, 1), depth Depth x 0.005 + 0.5.
inline void ui_hud_control_tail( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_crop_t& crop, const ui_hud_control_t& c,
                                 const ui_texture_t& texture, float alpha, const float* boing )
{
    const float ix = 1.0f / c.m_scale[ 0 ];
    const float iy = 1.0f / c.m_scale[ 1 ];
    float rect[ 4 ] = { ix * c.m_image[ 0 ], c.m_image[ 1 ] * iy, c.m_image[ 2 ] * ix, c.m_image[ 3 ] * iy };
    ui_anchor_transform( c.m_anchor, crop, rect, rect );
    if ( boing )
    {
        const float s = *boing;
        const float shrink = 1.0f - s;
        const float w = rect[ 2 ], h = rect[ 3 ];
        rect[ 2 ] = w * s;
        rect[ 0 ] = w * 0.5f * shrink + rect[ 0 ];
        rect[ 3 ] = h * s;
        rect[ 1 ] = h * 0.5f * shrink + rect[ 1 ];
    }
    const std::uint32_t a = static_cast< std::uint32_t >( ui_fistp32( alpha * 255.0f ) ) & 0xFFu;   // 0x6F7808
    const float uv[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
    ui_texture_draw( sort, env, texture.m_handle, ( a << 24 ) | 0x00FFFFFFu, rect, uv, c.m_depth * 0.005f + 0.5f );
}

// 0x4257A0 (pause) / 0x425F70 (recover): visible (slot 15), the pressed texture
// when the state is 1, nothing without a texture.
inline void ui_hud_button_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_crop_t& crop, const ui_hud_control_t& c,
                                const ui_hud_control_inputs_t& in, bool recover, float alpha )
{
    if ( !( recover ? ui_hud_recover_visible( in ) : ui_hud_pause_visible( in ) ) )
        return;
    const ui_texture_t* t = in.m_state == 1 ? in.m_pressed_texture : in.m_texture;
    if ( !t )
        return;
    ui_hud_control_tail( sort, env, crop, c, *t, alpha, recover ? &in.m_boing : nullptr );
}

// 0x42A0C0 (the on-screen axis / button): hidden under a control method its
// Enable flag is off (Tilt 2, Touch 3, GamePad 0, Keyboard 1), else the pressed
// texture when the +0xD4 byte is set.
inline void ui_hud_onscreen_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_crop_t& crop, const ui_hud_control_t& c,
                                  const ui_hud_control_inputs_t& in, float alpha )
{
    if ( in.m_method_present )
    {
        static const std::int32_t methods[ 4 ] = { 2, 3, 0, 1 };
        for ( int k = 0; k < 4; ++k )
            if ( !c.m_enable[ k ] && in.m_method == methods[ k ] )
                return;
    }
    const ui_texture_t* t = ( in.m_state & 0xFF ) ? in.m_pressed_texture : in.m_texture;
    if ( !t )
        return;
    ui_hud_control_tail( sort, env, crop, c, *t, alpha, nullptr );
}

// ---------------------------------------------------------------------------
// The controls' per-frame code (batch 17, disassembled).
//
// VuTouch g 0x759070: its callback list at +4..+0xC (8-byte entries
// { VuTouchCallback*, priority }) and the current priority +0x1C. The pause /
// recover buttons' gameInitialize slot 6 0x425390 adds { entity + 0x5C, 0 }
// through 0x53FB90 (a plain push_back - +0x1C untouched); gameRelease slot 7
// 0x4253D0 removes it through 0x53FA60. The device's other writers of +0x1C
// and its dispatch of the callbacks (down / up / move) are NOT read: the
// caller delivers the callbacks; the touch count / points are HOOKS.
//
// Batch 24: the device's dispatch, disassembled - VuMetroTouch (vtable 0x74D4E8,
// ctor 0x5392C0: +0x20 the touches array, 12-byte { id, x, y } entries, +0x24 the
// count, +0x28 the capacity 8). OUR dispatch target per callback is m_handlers
// (retail calls the callback's own vtable: slot 0 down (const VuVector2&), slot 1
// up (const VuVector2&), slot 2 move ()); a callback without a handler is
// counted in m_unhandled.
struct ui_touch_handler_t
{
    std::function< void( const float point[ 2 ] ) > m_down;   // vt+0
    std::function< void( const float point[ 2 ] ) > m_up;     // vt+4
    std::function< void( ) > m_move;                          // vt+8
};

struct ui_touch_entry_t
{
    std::uint32_t m_id = 0;
    float m_point[ 2 ] = { 0.0f, 0.0f };
};

struct ui_touch_device_t
{
    std::vector< std::pair< const void*, std::uint32_t > > m_callbacks;   // +4..+0xC
    std::uint32_t m_priority = 0;                                         // +0x1C
    // batch 24 (additive): the second callback list +0x10..+0x18 (dispatched with no
    // priority test). Batch 25: its filler is VuControlMethodManager (vtable 0x745538)
    // slot 5 0x4AE890 - 0x53FB90 { manager + 4, 0 } on device + 0x10 (slot 3 0x4AE980
    // erases it) - not recreated: data here. (0x4C6AB0's two 0x53FC00 calls are ONE
    // inlined push_back - the aliasing branch and the plain one - of VuTitleSequence
    // GameMode's callback { this + 4, 0 } onto the FIRST list +4.) The touches (+0x20 storage, +0x24 count; entries past the count keep
    // their old bytes - m_touches holds every entry ever written), the handlers.
    std::vector< std::pair< const void*, std::uint32_t > > m_low_level;
    std::vector< ui_touch_entry_t > m_touches;
    std::int32_t m_count = 0;
    std::map< const void*, ui_touch_handler_t > m_handlers;
    int m_unhandled = 0;
};

// One callback's slot k (0 down / 1 up / 2 move) through OUR handlers.
inline void ui_touch_call( ui_touch_device_t& d, const void* callback, int slot, const float point[ 2 ] )
{
    const auto it = d.m_handlers.find( callback );
    if ( it == d.m_handlers.end( ) )
    {
        ++d.m_unhandled;
        return;
    }
    const ui_touch_handler_t& h = it->second;
    if ( slot == 0 && h.m_down )
        h.m_down( point );
    else if ( slot == 1 && h.m_up )
        h.m_up( point );
    else if ( slot == 2 && h.m_move )
        h.m_move( );
    else
        ++d.m_unhandled;
}

// The callbacks of both lists for slot k: the first list's entries whose priority
// equals +0x1C (re-read per entry), then every entry of the second list. The
// vectors' ends are re-read per step (a handler may add / remove).
inline void ui_touch_broadcast( ui_touch_device_t& d, int slot, const float point[ 2 ] )
{
    for ( std::size_t k = 0; k < d.m_callbacks.size( ); ++k )
        if ( d.m_callbacks[ k ].second == d.m_priority )
            ui_touch_call( d, d.m_callbacks[ k ].first, slot, point );
    for ( std::size_t k = 0; k < d.m_low_level.size( ); ++k )
        ui_touch_call( d, d.m_low_level[ k ].first, slot, point );
}

// 0x5393E0 (VuMetroTouch's touch down; thiscall id, the point in xmm2 / xmm3 - the
// platform's pointer-pressed handler 0x4DAFD0 calls it): the entry holding the id
// (the first, from the front), else a new last entry (0x5395A0 resize to count + 1);
// it takes { id, x, y }; then slot 0 with the point to the callbacks.
inline void ui_touch_dispatch_down( ui_touch_device_t& d, std::uint32_t id, float x, float y )
{
    std::int32_t at = -1;
    for ( std::int32_t k = 0; k < d.m_count; ++k )
        if ( d.m_touches[ static_cast< std::size_t >( k ) ].m_id == id )
        {
            at = k;
            break;
        }
    if ( at < 0 )
    {
        at = d.m_count++;
        if ( d.m_touches.size( ) < static_cast< std::size_t >( d.m_count ) )
            d.m_touches.resize( static_cast< std::size_t >( d.m_count ) );
    }
    ui_touch_entry_t& e = d.m_touches[ static_cast< std::size_t >( at ) ];
    e.m_id = id;
    e.m_point[ 0 ] = x;
    e.m_point[ 1 ] = y;
    const float point[ 2 ] = { x, y };
    ui_touch_broadcast( d, 0, point );
}

// 0x5394A0 (touch up; thiscall id, the point in xmm2 / xmm3 - from the platform's
// 0x4DB280 / 0x4DB3B0): the entry holding the id swapped with the last one unless
// it is the last, the count - 1 (nothing when the id is absent); then slot 1 with
// the point.
inline void ui_touch_dispatch_up( ui_touch_device_t& d, std::uint32_t id, float x, float y )
{
    for ( std::int32_t k = 0; k < d.m_count; ++k )
        if ( d.m_touches[ static_cast< std::size_t >( k ) ].m_id == id )
        {
            if ( k + 1 < d.m_count )
                std::swap( d.m_touches[ static_cast< std::size_t >( k ) ], d.m_touches[ static_cast< std::size_t >( d.m_count - 1 ) ] );
            --d.m_count;
            break;
        }
    const float point[ 2 ] = { x, y };
    ui_touch_broadcast( d, 1, point );
}

// 0x53FB40 (VuTouch's move dispatch): slot 2 to the callbacks.
inline void ui_touch_dispatch_move( ui_touch_device_t& d )
{
    const float none[ 2 ] = { 0.0f, 0.0f };
    ui_touch_broadcast( d, 2, none );
}

// The engine half of the platform's pointer-moved handler 0x4DB100 (the point is
// the platform's: Position / the window bounds - a HOOK): the entry holding the id
// (from the front) takes the point and 0x53FB40 runs; an absent id: nothing.
inline void ui_touch_dispatch_moved( ui_touch_device_t& d, std::uint32_t id, float x, float y )
{
    for ( std::int32_t k = 0; k < d.m_count; ++k )
        if ( d.m_touches[ static_cast< std::size_t >( k ) ].m_id == id )
        {
            d.m_touches[ static_cast< std::size_t >( k ) ].m_point[ 0 ] = x;
            d.m_touches[ static_cast< std::size_t >( k ) ].m_point[ 1 ] = y;
            ui_touch_dispatch_move( d );
            return;
        }
}

// Slot 6 0x539210 (priority): 0 when the priority is below +0x1C (unsigned), else
// the touch count.
inline std::int32_t ui_touch_count( const ui_touch_device_t& d, std::uint32_t priority ) { return priority < d.m_priority ? 0 : d.m_count; }

// Slot 7 0x539230 (index, out): the entry's point, NO bounds test (retail reads the
// storage past the count - kept here - or past it: false = indeterminate).
inline bool ui_touch_point( const ui_touch_device_t& d, std::uint32_t index, float out[ 2 ] )
{
    if ( index >= d.m_touches.size( ) )
        return false;
    out[ 0 ] = d.m_touches[ index ].m_point[ 0 ];
    out[ 1 ] = d.m_touches[ index ].m_point[ 1 ];
    return true;
}

// Batch 25: OUR dispatch target for an object whose VuTouchCallback is the
// shared shape (the settings combos 0x73DF24, the leaderboard 0x73F2B4, the
// front-end button 0x73B79C: slot 0 down (inverse, the dispatched point), slot
// 1 up, slot 2 move = the device's touch 0 (slot 7 0x539230 - ui_touch_point;
// an entry never written is indeterminate in retail: `faults` + 1, nothing
// run)). `inverse` answers VuUIImpl +0x4C at the time of the call.
template < typename T >
inline void ui_touch_handlers( ui_touch_device_t& device, T* o, std::function< const float*( ) > inverse, int& faults )
{
    ui_touch_handler_t h;
    h.m_down = [ o, inverse ]( const float p[ 2 ] ) { o->touch_down( inverse( ), p ); };
    h.m_up = [ o ]( const float* ) { o->touch_up( ); };
    h.m_move = [ o, inverse, &device, &faults ]
    {
        float p[ 2 ];
        if ( ui_touch_point( device, 0u, p ) )
            o->touch_move( inverse( ), p );
        else
            ++faults;
    };
    device.m_handlers[ o ] = h;
}

// 0x53FB90 (thiscall device + 4, const pair&): push_back.
inline void ui_touch_add( ui_touch_device_t& d, const void* callback, std::uint32_t priority ) { d.m_callbacks.push_back( { callback, priority } ); }

// 0x53FA60 (thiscall device, callback): the first entry holding it erased
// (nothing when absent), then +0x1C = the largest (unsigned) priority left, 0
// for an empty list.
inline void ui_touch_remove( ui_touch_device_t& d, const void* callback )
{
    for ( auto it = d.m_callbacks.begin( ); it != d.m_callbacks.end( ); ++it )
        if ( it->first == callback )
        {
            d.m_callbacks.erase( it );
            break;
        }
    d.m_priority = 0;
    for ( const auto& c : d.m_callbacks )
        d.m_priority = c.second > d.m_priority ? c.second : d.m_priority;
}

// The Vu2dLayoutComponent draws (batch 27): 0x4258E0 (buttons: slot 16 at 1.0,
// through vt+0x40) / 0x42A240 (on-screen: 0x42A0C0 at 1.0). With the byte the
// touch rect (Touch X, Y, Width, Height: +0x7C / +0x78) then the image rect, each
// over the scale through the anchor, white (0x507A00); then the draw.
inline void ui_hud_layout_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_crop_t& crop, const ui_hud_control_t& c, const float touch[ 4 ],
                                const ui_hud_control_inputs_t& in, int kind, std::uint8_t layout )
{
    if ( layout )
    {
        const float one = 1.0f;
        const float ix = one / c.m_scale[ 0 ];
        const float iy = one / c.m_scale[ 1 ];
        const float depth = c.m_depth * 0.005f + 0.5f;
        const float* const rects[ 2 ] = { touch, c.m_image };
        for ( const float* r : rects )
        {
            const float src[ 4 ] = { r[ 0 ] * ix, r[ 1 ] * iy, r[ 2 ] * ix, r[ 3 ] * iy };
            float out[ 4 ];
            ui_anchor_transform( c.m_anchor, crop, src, out );
            ui_rect_draw( sort, env, 0xFFFFFFFFu, out, depth );
        }
    }
    if ( kind == 2 )
        ui_hud_onscreen_draw( sort, env, crop, c, in, 1.0f );
    else
        ui_hud_button_draw( sort, env, crop, c, in, kind == 1, 1.0f );
}

// Batch 27: VuControlMethodManager (vtable 0x745538) - the filler of the touch
// device's SECOND list and the keyboard device's callback list (disassembled).
// g 0x75906C (the key device): +4 a std::list of { callback, priority } (head
// node; each node +8 / +0xC), +8 its size, +0x150 the largest priority.
struct ui_key_device_t
{
    std::vector< std::pair< const void*, std::uint32_t > > m_callbacks;   // the +4 list in order
    std::uint32_t m_priority = 0;                                         // +0x150
};

// The platform half both slots reach: the tick manager g 0x759008 slot 8 (vt+0x20:
// register the method 0x4AEC40 - VuMethod1 vtable 0x7454F0 - in the phase "Input"
// 0x6AA2AC) and slot 10 (vt+0x28: unregister every tick of the manager), the
// input manager g 0x759038 slot 19 (vt+0x4C) with 1. HOOKS (null: skipped).
struct ui_control_methods_hooks_t
{
    std::function< void( ) > m_tick_add;
    std::function< void( ) > m_tick_remove;
    std::function< void( std::uint8_t ) > m_input;
};

// Slot 5 0x4AE890 (init; returns 1): 0x53FB90 { this + 4 (0 for a null this),
// 0 } onto the touch device's second list (+0x10); { this + 8, 0 } appended to
// the key device's list (size + 1; its +0x150 untouched); the tick; the input.
inline bool ui_control_methods_init( ui_touch_device_t& touch, ui_key_device_t& keys, const void* touch_callback, const void* key_callback,
                                     const ui_control_methods_hooks_t& h )
{
    touch.m_low_level.push_back( { touch_callback, 0u } );
    keys.m_callbacks.push_back( { key_callback, 0u } );
    if ( h.m_tick_add )
        h.m_tick_add( );
    if ( h.m_input )
        h.m_input( 1 );
    return true;
}

// Slot 3 0x4AE980 (release): the FIRST second-list entry holding this + 4 erased
// (the rest moved down; +0x1C untouched); 0x539020 on the key device: its first
// node holding this + 8 unlinked (size - 1), then +0x150 = the largest (unsigned,
// cmova) priority left, 0 when empty; the tick manager's unregister.
inline void ui_control_methods_release( ui_touch_device_t& touch, ui_key_device_t& keys, const void* touch_callback, const void* key_callback,
                                        const ui_control_methods_hooks_t& h )
{
    for ( auto it = touch.m_low_level.begin( ); it != touch.m_low_level.end( ); ++it )
        if ( it->first == touch_callback )
        {
            touch.m_low_level.erase( it );
            break;
        }
    for ( auto it = keys.m_callbacks.begin( ); it != keys.m_callbacks.end( ); ++it )
        if ( it->first == key_callback )
        {
            keys.m_callbacks.erase( it );
            break;
        }
    keys.m_priority = 0;
    for ( const auto& c : keys.m_callbacks )
        keys.m_priority = c.second > keys.m_priority ? c.second : keys.m_priority;
    if ( h.m_tick_remove )
        h.m_tick_remove( );
}

// 0x53FAE0 (thiscall device, callback, priority; ret 8 - disassembled, batch 21):
// EVERY entry holding the callback takes the priority, then +0x1C = the largest
// (unsigned, cmova) priority in the list, 0 for an empty list.
inline void ui_touch_set_priority( ui_touch_device_t& d, const void* callback, std::uint32_t priority )
{
    for ( auto& c : d.m_callbacks )
        if ( c.first == callback )
            c.second = priority;
    d.m_priority = 0;
    for ( const auto& c : d.m_callbacks )
        d.m_priority = c.second > d.m_priority ? c.second : d.m_priority;
}

// The touch callbacks map a device point through VuUIImpl +0x4C (0x81A144, the
// INVERSE crop matrix: m0 m1 m4 m5 m12 m13) - x' = y m4 + x m0 + m12,
// y' = x m1 + y m5 + m13 (the sums' order as the down 0x425400; the move
// 0x4254B0 adds the y terms the other way round - the same bits for a NaN-free
// point, the only kind the device gives).
inline void ui_hud_touch_map( const float inverse[ 16 ], const float in[ 2 ], float out[ 2 ] )
{
    const float x = in[ 1 ] * inverse[ 4 ] + in[ 0 ] * inverse[ 0 ] + inverse[ 12 ];
    const float y = in[ 0 ] * inverse[ 1 ] + in[ 1 ] * inverse[ 5 ] + inverse[ 13 ];
    out[ 0 ] = x;
    out[ 1 ] = y;
}

// The inclusive ordered hit test of 0x4255B0 / 0x429E10 (comiss / jb: a NaN
// misses): x >= left, width + left >= x, y >= top, height + top >= y.
inline bool ui_hud_touch_hit( const float point[ 2 ], const float rect[ 4 ] )
{
    return point[ 0 ] >= rect[ 0 ] && rect[ 2 ] + rect[ 0 ] >= point[ 0 ] && point[ 1 ] >= rect[ 1 ] && rect[ 3 ] + rect[ 1 ] >= point[ 1 ];
}

// The touch rect: Touch X / Y / Width / Height x (1.0 / Screen Scale) through
// the anchor transform 0x6282D0 (over the crop matrix 0x81A104).
inline void ui_hud_touch_rect( const float scale[ 2 ], const float touch[ 4 ], const ui_anchor_t& anchor, const ui_crop_t& crop, float rect[ 4 ] )
{
    const float sx = 1.0f / scale[ 0 ];   // 0x6F76B8 = 1.0
    const float sy = 1.0f / scale[ 1 ];
    rect[ 0 ] = touch[ 0 ] * sx;
    rect[ 1 ] = touch[ 1 ] * sy;
    rect[ 2 ] = touch[ 2 ] * sx;
    rect[ 3 ] = touch[ 3 ] * sy;
    ui_anchor_transform( anchor, crop, rect, rect );
}

// VuHUDButtonEntity's runtime (ctor 0x424A20: +0x64 Screen Scale, +0x7C Touch
// rect, +0xBC the anchor block; word +0xD4 = 0, byte +0xD6 = 0, +0xE0 = 0 -
// +0xD8 / +0xDC are not written by the ctor and read only after a down).
// Recover (ctor 0x425B60): +0xE4 Boing Rate, +0xE8 / +0xEC Boing Scale Min /
// Max, +0xF0 Boing Duration; +0xF4 the boing scale (ctor 1.0), +0xF8 the boing
// time (ctor 0) - its slot 13 writes both.
struct ui_hud_button_t
{
    bool m_recover = false;
    float m_scale[ 2 ] = { 1080.0f, 720.0f };
    float m_touch[ 4 ] = { 0.0f, 0.0f, 100.0f, 100.0f };
    ui_anchor_t m_anchor;
    std::uint8_t m_down = 0;      // +0xD4
    std::uint8_t m_held = 0;      // +0xD5
    std::uint8_t m_up = 0;        // +0xD6
    float m_point[ 2 ] = { 0.0f, 0.0f };   // +0xD8 / +0xDC
    std::int32_t m_state = 0;     // +0xE0 (1 = pressed: the draw's pressed texture)
    float m_rate = 4.0f;          // +0xE4
    float m_scale_min = 0.8f;     // +0xE8
    float m_scale_max = 1.2f;     // +0xEC
    float m_duration = 1.0f;      // +0xF0
    float m_boing = 1.0f;         // +0xF4
    float m_time = 0.0f;          // +0xF8
};

// VuTouchCallback (vtable 0x738B80, this = entity + 0x5C). Slot 0 0x425400
// (down, const VuVector2&): if the entity is visible (slot 15) and not held:
// +0xD4 = +0xD5 = 1 and the mapped point -> +0xD8.
inline void ui_hud_button_touch_down( ui_hud_button_t& b, bool visible, const float inverse[ 16 ], const float point[ 2 ] )
{
    if ( !visible || b.m_held )
        return;
    b.m_down = 1;
    b.m_held = 1;
    ui_hud_touch_map( inverse, point, b.m_point );
}

// Slot 1 0x425490 (up): if visible, +0xD6 = 1.
inline void ui_hud_button_touch_up( ui_hud_button_t& b, bool visible )
{
    if ( visible )
        b.m_up = 1;
}

// Slot 2 0x4254B0 (move): the device's touch 0 (slot 7 (0, out) - a HOOK, the
// point is the input) mapped -> +0xD8, always.
inline void ui_hud_button_touch_move( ui_hud_button_t& b, const float inverse[ 16 ], const float point[ 2 ] )
{
    ui_hud_touch_map( inverse, point, b.m_point );
}

// The class virtuals 0x4255B0 calls: slot 15 visible, slot 14 the press,
// slot 13 (dt) the class tick.
struct ui_hud_button_class_t
{
    std::function< bool( ) > m_visible;
    std::function< void( ) > m_press;
    std::function< void( float ) > m_tick;
};

// 0x4255B0 (slot 12, OnUITick through the thunk 0x4077E3; params: dt = the
// first record when a float, else 0): nothing unless visible; the touch rect;
// a down inside -> +0xE0 = 1; while pressed: an up -> slot 14 and +0xE0 = 0,
// the point outside -> +0xE0 = 0; an up clears +0xD5 / +0xD6; +0xD4 = 0;
// slot 13 (dt).
inline void ui_hud_button_tick( ui_hud_button_t& b, const ui_crop_t& crop, const ui_params_t& params, const ui_hud_button_class_t& cls )
{
    float dt = 0.0f;
    if ( params.m_size > 0 )
    {
        std::uint32_t type;
        std::memcpy( &type, params.m_bytes, 4 );
        if ( type == 2 )
            std::memcpy( &dt, params.m_bytes + 4, 4 );
    }
    if ( !cls.m_visible( ) )
        return;
    float rect[ 4 ];
    ui_hud_touch_rect( b.m_scale, b.m_touch, b.m_anchor, crop, rect );
    if ( b.m_down && ui_hud_touch_hit( b.m_point, rect ) )
        b.m_state = 1;
    if ( b.m_state == 1 )
    {
        if ( b.m_up )
        {
            cls.m_press( );
            b.m_state = 0;
        }
        if ( !ui_hud_touch_hit( b.m_point, rect ) )
            b.m_state = 0;
    }
    if ( b.m_up )
    {
        b.m_held = 0;
        b.m_up = 0;
    }
    b.m_down = 0;
    cls.m_tick( dt );
}

// The pause button's inputs / outputs: the input manager g 0x759038 slot 9
// (pad, name) -> down (HOOK), the FMOD event start (getEvent [0x64C348] /
// start [0x64C354] on g 0x759010 +8 with the name at *0x746D50 - the audio
// device, a HOOK), the broadcast 0x505690.
struct ui_hud_pause_hooks_t
{
    std::function< bool( int, const char* ) > m_input;
    std::function< void( const char* ) > m_sound;
    std::function< void( std::uint32_t, const ui_params_t& ) > m_broadcast;
};
inline constexpr const char* k_ui_hud_pause_sound = "Global/UI_and_Menu/main_menu_choose";   // *0x746D50 = 0x6C38DC

// Pause slot 13 0x4261B0 (dt unused): the device's first entry holding this
// callback (none: nothing); when its priority equals the device's +0x1C and
// the input manager answers "Pause" (pad 0): the sound, then the broadcast
// "OnPauseActionGame" with an empty VuParams.
inline void ui_hud_pause_tick( const ui_touch_device_t& device, const void* callback, const ui_hud_pause_hooks_t& h )
{
    for ( const auto& c : device.m_callbacks )
        if ( c.first == callback )
        {
            if ( c.second == device.m_priority && h.m_input && h.m_input( 0, "Pause" ) )
            {
                if ( h.m_sound )
                    h.m_sound( k_ui_hud_pause_sound );
                h.m_broadcast( fsm_hash( "OnPauseActionGame" ), ui_params_t( ) );
            }
            return;
        }
}

// Pause slot 14 0x4262A0 (the press): the broadcast "OnPauseActionGame"
// (empty), then the sound.
inline void ui_hud_pause_press( const ui_hud_pause_hooks_t& h )
{
    h.m_broadcast( fsm_hash( "OnPauseActionGame" ), ui_params_t( ) );
    if ( h.m_sound )
        h.m_sound( k_ui_hud_pause_sound );
}

// The recover button's view of the local boat's driver: g 0x758FB8 (read
// WITHOUT a null test - retail faults when absent; here: nothing), index +0x58
// against the count +0x10 (signed), boat +0xC[index] (null: nothing), its
// driver +0x220: slot 12 (an unread Vu virtual - HOOK answer), +0x8C byte,
// (+0x5C) +0x388 byte (inputs); the press writes the driver's +0xB8 = 1
// (an output, the world's).
struct ui_hud_recover_driver_t
{
    bool m_present = false;     // manager present, index in range, boat non-null
    bool m_slot12 = false;
    std::uint8_t m_8c = 0;
    std::uint8_t m_388 = 0;
};

// Recover slot 13 0x425DC0 (dt): the driver's slot 12 false -> nothing; with
// both bytes clear: +0xF4 = 1.0, +0xF8 = 0; else t = dt + +0xF8 -> +0xF8,
// s = fsin( t x Rate x 2pi (0x6F7778) ) (x87, stored as a float), u = (s + 1)
// x 0.5, a = maxss( 0, (Duration - t) / Duration ), +0xF4 = ((1 - u) x Min +
// Max x u) x a + (1 - a).
inline void ui_hud_recover_tick( ui_hud_button_t& b, const ui_hud_recover_driver_t& d, float dt )
{
    if ( !d.m_present || !d.m_slot12 )
        return;
    if ( !d.m_8c && !d.m_388 )
    {
        b.m_boing = 1.0f;
        b.m_time = 0.0f;
        return;
    }
    const float t = dt + b.m_time;
    b.m_time = t;
    const float arg = t * b.m_rate * 6.2831855f;
    float c, s;
    x87_sincos( arg, &c, &s );
    const float ratio = ( b.m_duration - t ) / b.m_duration;
    const float u = ( s + 1.0f ) * 0.5f;   // 0x6F765C
    const float a = 0.0f > ratio ? 0.0f : ratio;   // maxss( 0, ratio ): a NaN ratio passes through
    b.m_boing = ( ( 1.0f - u ) * b.m_scale_min + b.m_scale_max * u ) * a + ( 1.0f - a );
}

// Recover slot 14 0x425F10 (the press): the driver's slot 12 true -> its +0xB8
// = 1 (`recover`).
inline void ui_hud_recover_press( const ui_hud_recover_driver_t& d, const std::function< void( ) >& recover )
{
    if ( d.m_present && d.m_slot12 && recover )
        recover( );
}

// VuHUDOnScreenControlEntity's runtime (ctor 0x4292F0): +0x60 Screen Scale,
// +0x78 Touch rect, +0xB8 the anchor block, +0xC8..+0xCB the Enable flags,
// +0xD4 pressed, +0xD8 Axis Name / Button Name, +0xF0 Axis Direction (axis).
struct ui_hud_onscreen_t
{
    bool m_axis = false;
    float m_scale[ 2 ] = { 1080.0f, 720.0f };
    float m_touch[ 4 ] = { 0.0f, 0.0f, 100.0f, 100.0f };
    ui_anchor_t m_anchor;
    std::uint8_t m_enable[ 4 ] = { 1, 1, 0, 0 };
    std::uint8_t m_pressed = 0;   // +0xD4
    std::string m_name;           // +0xD8
    std::int32_t m_direction = 1; // +0xF0
};

// The on-screen controls' inputs / outputs: the control-method manager g
// 0x758FC0 (+0x18), the touch device's slot 6 count (-1) / slot 7 point
// (HOOKS), the inverse crop matrix, the input manager's slot 17 (vt+0x44:
// pad, button name) / slot 18 (vt+0x48: pad, axis name, value) - HOOKS.
struct ui_hud_onscreen_hooks_t
{
    bool m_method_present = false;
    std::int32_t m_method = 0;
    std::function< int( ) > m_touch_count;
    std::function< void( int, float[ 2 ] ) > m_touch_point;
    const float* m_inverse = nullptr;
    std::function< void( int, const char* ) > m_button;
    std::function< void( int, const char*, float ) > m_set_axis;
};

// OnUITick 0x429E10 (params read, dt unused): hidden under the current method
// (its Enable flag off: Tilt 2, Touch 3, GamePad 0, Keyboard 1) -> nothing;
// else +0xD4 = 0, for each touch k < count (re-read each pass): the point
// mapped through the inverse crop matrix, tested against the touch rect,
// +0xD4 |= hit; then if pressed: slot 12 = axis 0x42A840 (vt+0x48 (0, name,
// (float) Direction)) / button 0x42A610 (vt+0x44 (0, name)).
inline void ui_hud_onscreen_tick( ui_hud_onscreen_t& o, const ui_crop_t& crop, const ui_hud_onscreen_hooks_t& h )
{
    if ( h.m_method_present )
    {
        static const std::int32_t methods[ 4 ] = { 2, 3, 0, 1 };
        for ( int k = 0; k < 4; ++k )
            if ( !o.m_enable[ k ] && h.m_method == methods[ k ] )
                return;
    }
    static const float k_identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const float* inverse = h.m_inverse ? h.m_inverse : k_identity;
    o.m_pressed = 0;
    for ( int k = 0; h.m_touch_count && k < h.m_touch_count( ); ++k )
    {
        float device[ 2 ] = { 0.0f, 0.0f };
        if ( h.m_touch_point )
            h.m_touch_point( k, device );
        float point[ 2 ];
        point[ 0 ] = device[ 0 ] * inverse[ 0 ] + device[ 1 ] * inverse[ 4 ] + inverse[ 12 ];
        point[ 1 ] = device[ 0 ] * inverse[ 1 ] + device[ 1 ] * inverse[ 5 ] + inverse[ 13 ];
        float rect[ 4 ];
        ui_hud_touch_rect( o.m_scale, o.m_touch, o.m_anchor, crop, rect );
        o.m_pressed |= ui_hud_touch_hit( point, rect ) ? 1 : 0;
    }
    if ( !o.m_pressed )
        return;
    if ( o.m_axis )
    {
        if ( h.m_set_axis )
            h.m_set_axis( 0, o.m_name.c_str( ), static_cast< float >( o.m_direction ) );
    }
    else if ( h.m_button )
        h.m_button( 0, o.m_name.c_str( ) );
}

// VuHUDHiddenPackageToastEntity (ctor 0x424360 over the text base 0x627070,
// vtable 0x738BDC; its text getter slot 12 0x4242D0 = +0x118): +0x10C Total Time
// (5.0), +0x110 Fade Time (1.0), +0x114 the time left (0), +0x118 the text
// ("Text"); the base's +0x64 Visible and +0x108 alpha are what its handlers
// drive. Handlers (disassembled): OnPackageFound 0x424600 and OnUITick 0x4248E0.
struct ui_hud_toast_t
{
    float m_total = 5.0f;     // +0x10C
    float m_fade = 1.0f;      // +0x110
    float m_left = 0.0f;      // +0x114
    std::string m_text = "Text";   // +0x118
    bool m_visible = true;    // +0x64
    float m_alpha = 1.0f;     // +0x108
};

// 0x43E030 / 0x407450: the first occurrence of `key` replaced by `with`.
inline void ui_hud_toast_replace( std::string& s, const char* key, const std::string& with )
{
    const std::size_t at = s.find( key );
    if ( at != std::string::npos )
        s.replace( at, std::strlen( key ), with );
}

// 0x424600 (params: two int records - the found count, the track's count; a
// record of another type reads 0): the text = VuStringDB slot 6
// ("HiddenPackage_Toast", a HOOK) with "[FOUND_COUNT]" then "[TRACK_COUNT]"
// replaced by the "%d" texts; time left = Total Time; visible; alpha 0.
inline void ui_hud_toast_found( ui_hud_toast_t& t, const ui_params_t& params, const std::function< std::string( const char* ) >& string_db )
{
    ui_params_reader_t r( params );
    const std::int32_t found = static_cast< std::int32_t >( ui_params_read( r, 0 ) );
    const std::int32_t total = static_cast< std::int32_t >( ui_params_read( r, 0 ) );
    t.m_text = string_db ? string_db( "HiddenPackage_Toast" ) : std::string( );
    char a[ 16 ], b[ 16 ];
    std::snprintf( a, sizeof( a ), "%d", found );
    std::snprintf( b, sizeof( b ), "%d", total );
    ui_hud_toast_replace( t.m_text, "[FOUND_COUNT]", a );
    ui_hud_toast_replace( t.m_text, "[TRACK_COUNT]", b );
    t.m_left = t.m_total;
    t.m_visible = true;
    t.m_alpha = 0.0f;
}

// 0x4248E0 (params: dt - a float record, else 0): while time is left, fade in
// over the first Fade Time (alpha = left / fade while fade > left), out over the
// last (alpha = (total - left) / fade while left > total - fade), else 1; left
// -= dt. Without time left: hidden. (comiss / jbe: unordered takes the later branch.)
inline void ui_hud_toast_tick( ui_hud_toast_t& t, const ui_params_t& params )
{
    float dt = 0.0f;
    ui_params_reader_t r( params );
    std::uint32_t type = 0xFFFFFFFFu;
    if ( r.m_remaining > 0 )
    {
        std::memcpy( &type, r.m_at, 4 );
        if ( type == 2 )
            std::memcpy( &dt, r.m_at + 4, 4 );
    }
    const float left = t.m_left;
    if ( !( left > 0.0f ) )
    {
        t.m_visible = false;
        return;
    }
    const float fade = t.m_fade;
    if ( fade > left )
    {
        t.m_alpha = left / fade;
        t.m_left = left - dt;
        return;
    }
    if ( left > t.m_total - fade )
    {
        t.m_alpha = ( t.m_total - left ) / fade;
        t.m_left = left - dt;
        return;
    }
    t.m_alpha = 1.0f;
    t.m_left = left - dt;
}
