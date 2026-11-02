#pragma once

#include <cmath>

class c_viewport
{
public:
    void look( const float position[ 3 ] );
    void aim( const float* eye, const float* target, float far_plane );
    void lens( float tan_half, float near_plane ) { m_tan_half = tan_half; m_near = near_plane; }
    void tilt( float x, float y, float z );
    void build( int width, int height );
    const float* view( ) const { return m_view; }
    const float* projection( ) const { return m_projection; }
    const float* eye( ) const { return m_eye; }
    const float* target( ) const { return m_target; }
    float plane( ) const { return m_far; }
    void scale_depth( float scale );

private:
    float m_tan_half = 0.0f;   // tan( vertical fov / 2 ) of the camera; 0: no camera, the old 1.05 (the scaffold's view)
    float m_near = 0.5f;       // the camera's +0x178
    float m_target[ 3 ]{};
    float m_eye[ 3 ]{};
    float m_view[ 16 ]{};
    float m_projection[ 16 ]{};
    float m_up[ 3 ]{ 0.0f, 0.0f, 1.0f };
    float m_far = 800.0f;
    bool m_aimed = false;
};

c_viewport& g_viewport( );

#include <workspace/utility/math.hxx>

namespace
{
    c_viewport g_viewport_instance{};
}

c_viewport& g_viewport( )
{
    return g_viewport_instance;
}

void c_viewport::look( const float position[ 3 ] )
{
    m_aimed = false;
    m_target[ 0 ] = position[ 0 ];
    m_target[ 1 ] = position[ 1 ];
    m_target[ 2 ] = position[ 2 ];
}

void c_viewport::aim( const float* eye, const float* target, float far_plane )
{
    m_aimed = true;
    m_eye[ 0 ] = eye[ 0 ];
    m_eye[ 1 ] = eye[ 1 ];
    m_eye[ 2 ] = eye[ 2 ];
    m_target[ 0 ] = target[ 0 ];
    m_target[ 1 ] = target[ 1 ];
    m_target[ 2 ] = target[ 2 ];
    m_far = far_plane > 1.0f ? far_plane : 800.0f;
}

void c_viewport::tilt( float x, float y, float z )
{
    m_up[ 0 ] = x;
    m_up[ 1 ] = y;
    m_up[ 2 ] = z;
}

void c_viewport::build( int width, int height )
{
    float at[ 3 ]{};
    if ( m_aimed )
    {
        at[ 0 ] = m_target[ 0 ];
        at[ 1 ] = m_target[ 1 ];
        at[ 2 ] = m_target[ 2 ];
    }
    else
    {
        m_eye[ 0 ] = m_target[ 0 ];
        m_eye[ 1 ] = m_target[ 1 ] - 28.0f;
        m_eye[ 2 ] = m_target[ 2 ] + 16.0f;
        at[ 0 ] = m_target[ 0 ];
        at[ 1 ] = m_target[ 1 ] + 12.0f;
        at[ 2 ] = m_target[ 2 ];
    }

    math_look( m_view, m_eye, at, m_up );

    float aspect = height > 0 ? static_cast< float >( width ) / static_cast< float >( height ) : 1.0f;
    math_perspective( m_projection, !m_aimed ? 0.9f : m_tan_half > 0.0f ? 2.0f * std::atan( m_tan_half ) : 1.05f, aspect, m_aimed ? m_near : 0.5f, m_aimed ? m_far : 800.0f );
}

void c_viewport::scale_depth( float scale )
{
    float flip[ 16 ]{};
    math_identity( flip );
    flip[ 10 ] = scale;
    float view[ 16 ]{};
    math_multiply( view, m_view, flip );
    std::memcpy( m_view, view, sizeof( m_view ) );
}
