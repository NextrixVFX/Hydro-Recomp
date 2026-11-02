#pragma once

#include <cstring>

#include <workspace/core/boat/hull.hxx>
#include <workspace/core/ui/compose.hxx>

// 0x4CC110: the UI's crop, each frame: the mode manager's draw 0x4CC0F0 runs it before the mode's slot 5. It writes VuUIImpl's fields inline (the virtual
// setCropMatrix 0x626750 has no caller). `width` / `height`: g 0x75907C slot 16 0x5409D0, VuGfx +0xDC / +0xE0, the swap chain's size (0x540560, resize 0x540890).
// The UI's aspect +0x90 against the display's: a display narrower than it squeezes y about the centre and +0x8C takes the scale; a wider one squeezes x and
// +0x8C stays 1. Matrices as 0x407DB0 x 0x410AB0 through 0x4078C0, whose products here are the entries below; the inverse is 0x4157C0.
inline void ui_crop_update( ui_impl_t& impl, std::int32_t width, std::int32_t height )
{
    const float display = static_cast< float >( width ) / static_cast< float >( height );
    const float ui = impl.m_aspect;
    float scale = 1.0f;
    float m[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    if ( ui > display )
    {
        const float s = display / ui;
        m[ 5 ] = s;
        m[ 13 ] = ( 1.0f - s ) * 0.5f;   // 0x6F76B8, 0x6F765C
        scale = s;
    }
    else if ( display > ui )
    {
        const float s = ui / display;
        m[ 0 ] = s;
        m[ 12 ] = ( 1.0f - s ) * 0.5f;
    }
    std::memcpy( impl.m_crop.m_matrix, m, sizeof( m ) );
    std::memcpy( impl.m_inverse, m, sizeof( m ) );
    hull_invert( impl.m_inverse );
    impl.m_float = scale;
}
