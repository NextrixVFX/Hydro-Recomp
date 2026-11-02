#pragma once

// The UI's records at the walk (world batch 37): what VuGfxSort's walk 0x53ADC0 runs for the commands the UI's draws submit (ui/draw.hxx),
// transliterated from the disassembly. The walk binds the material; these set its constants, the texture, and draw through the
// device's dynamic buffer. Verified against retail: checks/hudpass.hxx (case ui_record_draws).
//
// `set` is the basic shader block's entry (VuGfxUtil +4, 0x541DD0's records, 12 bytes each, filled by 0x5421C0): 0 Basic/2dXyz,
// 1 Basic/2dXyzUv. Each callback reads its program from the entry's asset (+0x58): the same program the material bound, except that
// the walk's material is the entry's own +8.
//
// NOT recreated: the boost meter's 0x42C290 -> 0x42C2A0 and the minimap's 0x428060 / 0x428070 (0x428080, 0x4284F0): read, each
// needs its entity's shader handles from the ctor; a command with such a callback counts in gfx_ui_pass_t::m_unknown.

#include <cstdint>
#include <cstring>

struct ui_record_device_t
{
    virtual ~ui_record_device_t( ) = default;
    virtual void colour( int set, const char* name, const std::uint8_t* rgba ) = 0;                                     // program vt+4, vt+0x30
    virtual void matrix( int set, const char* name, const float* m ) = 0;                                               // program vt+4, vt+0x1C
    virtual void texture( std::uint32_t slots, std::uint32_t handle ) = 0;                                              // g 0x75907C vt+0xB4
    virtual void draw( std::uint32_t kind, std::uint32_t primitives, const void* vertices, std::uint32_t stride ) = 0;  // vt+0xC0
};

// 0x507430: the textured quad of 0x507360's record.
inline void ui_record_texture( ui_record_device_t& d, const std::uint8_t* r )
{
    float a[ 4 ], b[ 4 ];
    std::memcpy( b, r + 0x48, 16 );
    std::memcpy( a, r + 0x58, 16 );
    std::uint32_t handle;
    std::memcpy( &handle, r, 4 );
    float m[ 16 ];
    std::memcpy( m, r + 4, 64 );
    const float x0 = a[ 0 ], y0 = a[ 1 ], x1 = a[ 2 ] + a[ 0 ], y1 = a[ 3 ] + a[ 1 ];
    const float u0 = b[ 0 ], v0 = b[ 1 ], u1 = b[ 2 ] + b[ 0 ], v1 = b[ 3 ] + b[ 1 ];
    d.colour( 1, "gColor", r + 0x44 );
    d.matrix( 1, "gTransform", m );
    d.texture( 0u, handle );
    const float v[ 20 ] = { x0, y0, 0.0f, u0, v0, x0, y1, 0.0f, u0, v1, x1, y0, 0.0f, u1, v0, x1, y1, 0.0f, u1, v1 };
    d.draw( 4u, 2u, v, 0x14u );
}

// 0x5078E0: the solid quad (ui/image.hxx's record): the first basic shader, no texture.
inline void ui_record_solid( ui_record_device_t& d, const std::uint8_t* r )
{
    float a[ 4 ], m[ 16 ];
    std::memcpy( m, r, 64 );
    std::memcpy( a, r + 0x44, 16 );
    const float x0 = a[ 0 ], y0 = a[ 1 ], x1 = a[ 2 ] + a[ 0 ], y1 = a[ 3 ] + a[ 1 ];
    d.colour( 0, "gColor", r + 0x40 );
    d.matrix( 0, "gTransform", m );
    d.texture( 0u, 0u );
    const float v[ 12 ] = { x0, y0, 0.0f, x0, y1, 0.0f, x1, y0, 0.0f, x1, y1, 0.0f };
    d.draw( 4u, 2u, v, 12u );
}

// 0x507AB0: 0x507A00's record, an outline: five vertices, a line strip.
inline void ui_record_outline( ui_record_device_t& d, const std::uint8_t* r )
{
    float a[ 4 ], m[ 16 ];
    std::memcpy( m, r, 64 );
    std::memcpy( a, r + 0x44, 16 );
    const float x0 = a[ 0 ], y0 = a[ 1 ], x1 = a[ 2 ] + a[ 0 ], y1 = a[ 3 ] + a[ 1 ];
    d.colour( 0, "gColor", r + 0x40 );
    d.matrix( 0, "gTransform", m );
    d.texture( 0u, 0u );
    const float v[ 15 ] = { x0, y0, 0.0f, x1, y0, 0.0f, x1, y1, 0.0f, x0, y1, 0.0f, x0, y0, 0.0f };
    d.draw( 2u, 4u, v, 12u );
}
