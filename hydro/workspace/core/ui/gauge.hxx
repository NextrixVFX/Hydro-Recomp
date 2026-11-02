#pragma once

#include <workspace/core/assets/scene.hxx>
#include <workspace/core/ui/draw.hxx>

#include <cstdint>
#include <cstring>
#include <xmmintrin.h>

// The race HUD's speedometer draws as data: VuHUDSpeedometerEntity (ctor
// 0x42AB60, vtable 0x739250). Its VuUIDrawComponent (+0x5C; delegate vtable
// 0x739230, VuMethod1<float> 0x404A00) runs 0x42BA00 with the HUD's alpha -
// which 0x42BA00 never reads; its Vu2dLayoutComponent (vtable 0x738A78; delegate
// 0x739240 -> 0x4089B0) runs 0x42C1E0( selected ). Everything below is the
// disassembly transliterated; the records and commands are what retail leaves
// in VuGfxSort. The boost record's callback 0x42C290 is world/hudpass.hxx ui_record_boost.

// The x87 sequence of 0x40B5F0 (fld; fsincos; fstp c; fstp s) on this FPU (engine x87.asm).
extern "C" void x87_sincos( float x, float* c, float* s );

inline float ui_f32( std::uint32_t bits )
{
    float f;
    std::memcpy( &f, &bits, 4 );
    return f;
}

// A texture asset's VuTexture (asset +0x58): the handle stored in the records
// (retail's pointer, opaque here) and its +0xC / +0x10 ints (width, height).
struct ui_texture_t
{
    std::uint32_t m_handle = 0;
    std::int32_t m_width = 0;
    std::int32_t m_height = 0;
};

// The fields the draws read (ctor 0x42AB60: the property block, then the
// textures it asks the asset factory 0x426A90 for by name).
struct ui_speedometer_t
{
    float m_screen_scale[ 2 ] = { 1080.0f, 720.0f };      // +0x60 "Screen Scale"
    float m_rect[ 4 ] = { 0.0f, 0.0f, 100.0f, 100.0f };   // +0x68 X, Y, Width, Height
    ui_anchor_t m_anchor;                                  // +0x78 anchors, +0x80 / +0x84 ratios
    std::uint32_t m_material = 0;                          // +0x8C the pipeline state (0x53A440) handed to the sort
    std::uint32_t m_material_key = 0;                      // its +0x6C8
    ui_texture_t m_back_imperial;                          // +0xA0 "UI/HUD_Speed_Back_Imperial"
    ui_texture_t m_back_metric;                            // +0xA4 "UI/HUD_Speed_Back_Metric"
    ui_texture_t m_needle;                                 // +0xA8 "UI/HUD_Speed_Needle"
    ui_texture_t m_active;                                 // +0xAC "UI/HUD_Speed_BoostActive"
    ui_texture_t m_inactive;                               // +0xB0 "UI/HUD_Speed_BoostInactive"
    ui_texture_t m_hold;                                   // +0xB4 "UI/HUD_Speed_BoostHold"
    ui_texture_t m_mask;                                   // +0xB8 "UI/HUD_Speed_BoostMask"
    ui_texture_t m_flash;                                  // +0xBC "UI/HUD_Speed_BoostInactiveFlash"
    ui_texture_t m_boost[ 8 ];                             // +0xC0 "UI/HUD_Boost%dx", 1..8
    std::int32_t m_metric = 0;                             // +0xE0 (OnUITick: settings "Metric")
    float m_v_offset = 0.0f;                               // +0xE4 (OnUITick, gBoostTextureVOffset)
    float m_countdown = 0.0f;                              // +0xE8 (OnUITick)
    float m_depth = 0.0f;                                  // the draw component (+0x5C) +0x14 "Depth"
    std::uint32_t m_self = 0;                              // the entity (the boost record's +0)
};

// What 0x42BA00 reads of the local boat (VuBoatManager g 0x758FB8: +0x58 the
// index, below +0x10 the count, +0xC the array; a null slot is no boat).
// UNKNOWN on the boat side: nothing recreated fills these yet - an input.
struct ui_speedometer_boat_t
{
    bool m_present = false;
    float m_velocity[ 2 ] = { };          // +0x21C -> +0x140, +0x144
    std::int32_t m_boost_count = 0;       // +0x31C
    float m_digit_scale = 0.0f;           // +0x320
    std::uint8_t m_byte_32c = 0;          // +0x32C
    float m_boost = 0.0f;                 // +0x310
    std::int32_t m_mode = 0;              // +0x230 -> +0x78
    std::uint8_t m_byte_32d = 0;          // +0x32D
};

constexpr std::uint32_t k_ui_boost_callback = 0x42C290u;   // the boost record's draw at submit (GPU side)
constexpr int k_ui_boost_record = 92;

inline void ui_matrix_translation( float* m, float x, float y, float z )   // 0x410AB0
{
    const float t[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1 };
    std::memcpy( m, t, 64 );
}

inline void ui_matrix_scale( float* m, float x, float y, float z )   // VuMatrix_fromScale 0x407DB0
{
    const float s[ 16 ] = { x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1 };
    std::memcpy( m, s, 64 );
}

inline void ui_matrix_rotation_z( float* m, float angle )   // 0x40B5F0
{
    float c, s;
    x87_sincos( angle, &c, &s );
    const float r[ 16 ] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    std::memcpy( m, r, 64 );
}

// 0x42BA00 (thiscall; the alpha on the stack, unread): env.m_matrix is the
// matrix stack's top on entry (VuGfxUtil +0x20), `display` the two ints the
// device's display-size getter (g 0x75907C slot 16 - a HOOK) writes. The dial,
// the needle, the boost record, then the digit when the count is non-zero.
inline void ui_speedometer_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_crop_t& crop, const ui_speedometer_t& s,
                                 const ui_speedometer_boat_t& boat, const std::int32_t display[ 2 ] )
{
    float speed = ui_f32( 0x42480000u );        // 0x6F77C8: 50
    std::int32_t count = 5;
    float digit_scale = ui_f32( 0x3F800000u );  // 0x6F76B8: 1
    float boost = ui_f32( 0x3F400000u );        // 0x6F767C: 0.75
    std::uint8_t byte_58 = 0, byte_59 = 1, byte_5a = 0;
    if ( boat.m_present )
    {
        const float x = boat.m_velocity[ 0 ];
        const float y = boat.m_velocity[ 1 ];
        speed = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( y * y + x * x ) ) );   // VuMath_sqrt 0x40B5A0
        count = boat.m_boost_count;
        byte_58 = boat.m_byte_32c;
        digit_scale = boat.m_digit_scale;
        boost = boat.m_boost * ui_f32( 0x3D800000u );   // 0x6F7600: 0.0625
        byte_59 = boat.m_mode == 2 ? 1 : 0;
        byte_5a = boat.m_mode == 2 && boat.m_byte_32d == 0 ? 1 : 0;
    }
    const float mph = speed * ui_f32( 0x400F29F7u );   // 0x6F7710: 2.2369363
    const float aspect = static_cast< float >( display[ 0 ] ) / static_cast< float >( display[ 1 ] );
    const float one = ui_f32( 0x3F800000u );
    const float half = ui_f32( 0x3F000000u );                                  // 0x6F765C
    const float depth = s.m_depth * ui_f32( 0x3BA3D70Au ) + half;               // 0x6F75C4: 0.005
    const float unit[ 4 ] = { 0.0f, 0.0f, one, one };

    // the dial: the rect over the screen scale through the anchor 0x6282D0
    float ix = one / s.m_screen_scale[ 0 ];
    float iy = one / s.m_screen_scale[ 1 ];
    float rect[ 4 ] = { ix * s.m_rect[ 0 ], s.m_rect[ 1 ] * iy, s.m_rect[ 2 ] * ix, s.m_rect[ 3 ] * iy };
    ui_anchor_transform( s.m_anchor, crop, rect, rect );
    const ui_texture_t& back = s.m_metric == 1 ? s.m_back_metric : s.m_back_imperial;
    ui_texture_draw( sort, env, back.m_handle, 0xFFFFFFFFu, rect, unit, depth + ui_f32( 0x34400000u ) );   // 0x6F7588

    // the needle: its size over the screen scale, the pivot, the matrix chain
    ix = one / s.m_screen_scale[ 0 ];
    iy = one / s.m_screen_scale[ 1 ];
    const float w = static_cast< float >( s.m_needle.m_width ) * ix;
    const float h = static_cast< float >( s.m_needle.m_height ) * iy;
    const float lift = ui_f32( 0xC2080000u ) / s.m_screen_scale[ 1 ];   // 0x6F78EC: -34
    const float x0 = ix * 0.0f;
    const float y0 = iy * 0.0f;
    const float px = ( w * half + x0 ) + x0;
    const float py = ( h + y0 ) + lift;
    float m[ 16 ], t[ 16 ];
    const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };   // VuMatrix_identity 0x410A40
    std::memcpy( m, identity, 64 );
    ui_matrix_translation( t, -px, -py, -0.0f );
    scene_multiply( m, m, t );                                          // VuMatrix_multiply 0x4078C0 (this = m)
    ui_matrix_scale( t, aspect, one, one );
    scene_multiply( m, m, t );
    float turn = mph * ui_f32( 0x3B888889u );                           // 0x6F75C0: 1 / 240
    turn = _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( one ), _mm_set_ss( turn ) ) );
    turn = _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( 0.0f ), _mm_set_ss( turn ) ) );
    const float angle = turn * ui_f32( 0x43870000u ) * ui_f32( 0x3C8EFA35u ) - ui_f32( 0x4016CBE4u );   // 270, deg -> rad, 3pi/4
    ui_matrix_rotation_z( t, angle );
    scene_multiply( m, m, t );
    ui_matrix_scale( t, one / aspect, one, one );
    scene_multiply( m, m, t );
    ui_matrix_translation( t, rect[ 2 ] * half + rect[ 0 ], rect[ 3 ] * half + rect[ 1 ], 0.0f );
    scene_multiply( m, m, t );
    ui_font_draw_env_t pushed = env;                                    // 0x50B4B0: m . top pushed
    scene_multiply( m, pushed.m_matrix, env.m_matrix );
    const float needle[ 4 ] = { x0, y0, w, h };
    ui_texture_draw( sort, pushed, s.m_needle.m_handle, 0xFFFFFFFFu, needle, unit, depth );
    // popped inline (size--, offset 0 at 0): the top is env's again

    // the boost record
    std::uint8_t* r = sort.allocate( k_ui_boost_record );
    std::memcpy( r + 0, &s.m_self, 4 );
    std::memcpy( r + 4, env.m_matrix, 64 );
    std::memcpy( r + 0x44, rect, 16 );
    std::memcpy( r + 0x54, &boost, 4 );
    r[ 0x58 ] = byte_58;
    r[ 0x59 ] = byte_59;
    r[ 0x5A ] = byte_5a;
    sort.submit( 17, s.m_material, s.m_material_key, 0, 0, k_ui_boost_callback, depth );

    // the digit: "Boost%dx" for count - 1 in 0..7, scaled by +0x320
    if ( count == 0 )
        return;
    std::int32_t index = static_cast< std::int32_t >( static_cast< std::uint32_t >( count ) - 1u );
    if ( index > 7 )
        index = 7;
    if ( index < 0 )
        index = 0;
    const ui_texture_t& digit = s.m_boost[ index ];
    const float dx = digit_scale * ui_f32( 0xC2800000u ) + ui_f32( 0x41C00000u );   // 0x6F78F0 -64, 0x6F77A4 24
    const float dy = digit_scale * ui_f32( 0xC2000000u ) + ui_f32( 0x43000000u );   // 0x6F78E8 -32, 0x6F77E8 128
    const float dw = static_cast< float >( digit.m_width ) * digit_scale;
    const float dh = static_cast< float >( digit.m_height ) * digit_scale;
    ix = one / s.m_screen_scale[ 0 ];
    iy = one / s.m_screen_scale[ 1 ];
    const float place[ 4 ] = { rect[ 0 ] + ix * dx, rect[ 1 ] + iy * dy, ix * dw, iy * dh };
    ui_texture_draw( sort, env, digit.m_handle, 0xFFFFFFFFu, place, unit, depth );
}

// 0x42C1E0 (thiscall; selected; ret 4): when selected, the rect draw 0x507A00
// over the rect / screen scale (no anchor) in white at the Depth; then 0x42BA00
// with 1.0 pushed.
inline void ui_speedometer_layout_draw( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const ui_crop_t& crop, const ui_speedometer_t& s,
                                        const ui_speedometer_boat_t& boat, const std::int32_t display[ 2 ], bool selected )
{
    if ( selected )
    {
        const float one = ui_f32( 0x3F800000u );
        const float ix = one / s.m_screen_scale[ 0 ];
        const float iy = one / s.m_screen_scale[ 1 ];
        const float rect[ 4 ] = { s.m_rect[ 0 ] * ix, s.m_rect[ 1 ] * iy, s.m_rect[ 2 ] * ix, s.m_rect[ 3 ] * iy };
        ui_rect_draw( sort, env, 0xFFFFFFFFu, rect, s.m_depth * ui_f32( 0x3BA3D70Au ) + ui_f32( 0x3F000000u ) );
    }
    ui_speedometer_draw( sort, env, crop, s, boat, display );
}
