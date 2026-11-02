#pragma once

// VuGfxComposer's frame (g 0x7590BC, the object's +4 interface vtable 0x74EC10; ctor 0x5585F0), recreated 1:1 (water batch 35): the targets 0x559100, the frame 0x5588B0, the planar reflection
// 0x558C00, the scene 0x558AC0, the overlay 0x558FA0 and the sort commands they queue (0x5BBFC0 0x5BC290 0x5BC380 0x5BC410 0x5BC660 0x5BC730). Which layer and screen each pass runs in is the
// sort key's part of the job: it is compared word by word.
//
// HOOKS, by retail address, in gfx_composer_hooks_t: everything that draws or is another object's: the sort's frame constants 0x53AAB0 / 0x53A9C0, the camera static 0x52F690, the draw manager g 0x7590B8,
// g 0x7590DC's 0x5BBAA0, g 0x75900C vt+0x68, the lens water g 0x759048, VuGfxUtil's quad 0x507360, the render targets 0x57F7D0 and their references.
// The commands' callbacks: 0x5BC6F0 / 0x5BC7D0 are render/post.hxx; the others are gfx_composer_callback, at the end.
// Verified against retail: hydro checks/device.hxx verify_gfx_composer (case gfx_composer).

#include <workspace/core/boat/camera.hxx>
#include <workspace/core/boat/hull.hxx>
#include <workspace/core/render/gfx.hxx>
#include <workspace/core/world/drawer.hxx>
#include <workspace/core/world/gfxsort.hxx>

#include <cstdint>
#include <cstring>

struct gfx_composer_hooks_t
{
    virtual ~gfx_composer_hooks_t( ) = default;
    virtual void display( std::int32_t& width, std::int32_t& height ) = 0;              // the device's vt+0x40
    virtual std::uint32_t target_create( std::int32_t width, std::int32_t height, std::uint32_t format ) = 0;   // 0x57F7D0
    virtual void target_release( std::uint32_t target ) = 0;                            // its reference, 0x5592D0
    virtual std::uint32_t target_texture( std::uint32_t target ) = 0;                   // its vt+0xC
    virtual bool lens_active( ) = 0;                                                    // g 0x759048 vt+0x1C
    virtual bool lens_on( ) = 0;                                                        // vt+0x20
    virtual void lens_size( std::int32_t width, std::int32_t height ) = 0;              // vt+0x30
    virtual void lens_draw( std::uint32_t texture, std::uint32_t target ) = 0;          // vt+0x34
    virtual void camera_static( const std::uint8_t* camera, const std::uint8_t* extra ) = 0;   // 0x52F690 on dword_759430
    virtual void sort_view( const std::uint8_t* extra ) = 0;                            // 0x53AAB0
    virtual void sort_camera( const std::uint8_t* camera ) = 0;                         // 0x53A9C0
    virtual void draw( const gfx_draw_params_t& params, const std::uint8_t* camera ) = 0;   // g 0x7590B8 vt+0x18: drawer.hxx's params, m_camera being the caller's view of `camera`
    virtual void trail( ) = 0;                                                          // g 0x7590DC 0x5BBAA0( composer +0xA8 )
    virtual void depth_extra( const std::uint8_t* camera ) = 0;                         // g 0x75900C vt+0x68
    virtual void overlay( std::uint32_t texture, const float* rect ) = 0;               // 0x507360, the two rects and white, at depth 0
};

struct gfx_composer_t
{
    gfx_composer_t( gfx_sort_t& sort, gfx_composer_hooks_t& hooks ) : m_sort( sort ), m_hooks( hooks ) { }

    gfx_sort_t& m_sort;
    gfx_composer_hooks_t& m_hooks;
    float m_scale = 1.0f;                 // +8
    std::uint8_t m_flag_c = 0;            // +0xC
    std::uint8_t m_radial = 0;            // +0xD
    std::uint8_t m_no_copy = 0;           // +0xE
    std::int32_t m_forced_w = 0, m_forced_h = 0;   // +0x10, +0x14
    float m_lod = 0.0f;                   // +0x18
    std::uint8_t m_depth_first = 0;       // +0x1C
    std::uint8_t m_depth_pass = 0;        // +0x1D
    std::int32_t m_width = 0, m_height = 0;        // +0x20, +0x24
    std::uint32_t m_targets[ 2 ] = { };   // +0x28, +0x2C
    std::uint8_t m_resized = 0;           // +0x30
    float m_reflection_scale = 0.25f;     // +0x34 "GfxComposer/Reflection Render Target Scale"
    float m_rejection = 0.05f;            // +0x38 "GfxComposer/Reflection Rejection Scale"
    std::uint8_t m_show_reflection = 0;   // +0x3C "GfxComposer/Show Reflection Map"
    std::uint32_t m_reflection = 0;       // +0x40
    const std::uint8_t* m_extra = nullptr;     // +0x54, what 0x52F690 fills and 0x53AAB0 reads

    // 0x5588B0. `camera` the viewport's +0x28 (0x240 bytes), the viewport's +0x268 `plane`, +0x278 `blur`, VuWater's +0x44 `reflection`, the sort's pending frame `pending`.
    void frame( const std::uint8_t* camera, float plane, float blur, bool reflection )
    {
        targets( reflection );
        m_sort.set_layer( 1u );
        m_sort.m_key_hi &= 0xFCFFFFFFu;
        const bool radial = m_radial != 0u && blur > 0.0f;
        const bool lens = m_hooks.lens_active( ) && m_hooks.lens_on( );
        const bool through = radial || lens || m_resized != 0u;
        m_hooks.camera_static( camera, m_extra );
        m_hooks.sort_view( m_extra );
        reflect( camera, plane, reflection );
        scene( through ? m_targets[ 0 ] : 0u, camera );
        overlay( );
        std::uint32_t from = m_targets[ 0 ], to = m_targets[ 1 ];
        if ( from )
        {
            if ( radial )
            {
                if ( !lens && m_resized == 0u )
                    to = 0u;
                m_sort.set_layer( 2u );
                m_sort.set_screen( 0u );
                blur_command( m_hooks.target_texture( from ), to, blur );
                std::swap( from, to );
            }
            if ( lens )
            {
                if ( m_resized == 0u )
                    to = 0u;
                m_sort.set_layer( 2u );
                m_sort.set_screen( 1u );
                const std::uint32_t texture = m_hooks.target_texture( from );
                m_hooks.lens_draw( texture, to );
                from = to;
            }
            if ( m_resized != 0u && m_no_copy == 0u )
            {
                m_sort.set_layer( m_flag_c != 0u ? 0xBu : 2u );
                m_sort.set_screen( 0xBu );
                copy_command( m_hooks.target_texture( from ) );
            }
        }
        m_sort.m_key_hi &= 0xFCFFFFFFu;
    }

private:
    static float scaled( std::int32_t v, float s ) { return static_cast< float >( v ) * s; }

    // 0x559100.
    void targets( bool reflection )
    {
        std::int32_t display_w = 0, display_h = 0;
        m_hooks.display( display_w, display_h );
        std::int32_t w = static_cast< std::int32_t >( std::lrintf( scaled( display_w, m_scale ) ) );
        std::int32_t h = static_cast< std::int32_t >( std::lrintf( scaled( display_h, m_scale ) ) );
        if ( m_forced_w != 0 && m_forced_h != 0 )
        {
            w = m_forced_w;
            h = m_forced_h;
        }
        const std::int32_t have = ( m_targets[ 0 ] != 0u ? 1 : 0 ) + ( m_targets[ 1 ] != 0u ? 1 : 0 );
        std::int32_t want = w != display_w || h != display_h ? 1 : 0;
        if ( m_radial != 0u )
            ++want;
        if ( m_hooks.lens_active( ) )
            ++want;
        if ( want > 2 )
            want = 2;
        const bool has_reflection = m_reflection != 0u;
        const bool changed = m_width != w || m_height != h || have != want;
        if ( has_reflection != reflection || changed )
        {
            release( );
            if ( want >= 1 )
                m_targets[ 0 ] = m_hooks.target_create( w, h, 9u );
            if ( want >= 2 )
                m_targets[ 1 ] = m_hooks.target_create( w, h, 9u );
            if ( reflection )
            {
                const std::int32_t rw = static_cast< std::int32_t >( std::lrintf( scaled( w, m_reflection_scale ) ) );
                const std::int32_t rh = static_cast< std::int32_t >( std::lrintf( scaled( h, m_reflection_scale ) ) );
                m_reflection = m_hooks.target_create( rw, rh, 9u );
            }
            m_width = w;
            m_height = h;
            m_resized = w != display_w || h != display_h ? 1u : 0u;
        }
        m_hooks.lens_size( w >> 1, h >> 1 );
    }

    // 0x5592D0, the sort's pending frame being the caller's business: both targets, then the reflection.
    void release( )
    {
        for ( std::uint32_t& t : m_targets )
        {
            if ( t )
                m_hooks.target_release( t );
            t = 0u;
        }
        if ( m_reflection )
            m_hooks.target_release( m_reflection );
        m_reflection = 0u;
        m_width = 0;
        m_height = 0;
    }

    // 0x558C00: the camera mirrored in z = plane, the clip plane through the inverted view-projection, the frame into the reflection target.
    void reflect( const std::uint8_t* camera, float plane, bool reflection )
    {
        if ( !reflection )
            return;
        m_sort.m_key_hi &= 0xFF7FFFFFu;
        const float negative = -plane;
        std::uint8_t copy[ 0x240 ];
        std::memcpy( copy, camera, sizeof( copy ) );
        float eye[ 4 ], target[ 4 ], up[ 4 ];
        std::memcpy( eye, camera, 16 );
        std::memcpy( target, camera + 0x10, 16 );
        std::memcpy( up, camera + 0x150, 16 );
        eye[ 2 ] = plane - ( eye[ 2 ] - plane );
        target[ 2 ] = plane - ( target[ 2 ] - plane );
        up[ 0 ] = -up[ 0 ];
        up[ 1 ] = -up[ 1 ];
        camera_view( copy, eye, target, up );
        float m[ 16 ];
        std::memcpy( m, copy + 0xB0, sizeof( m ) );
        hull_invert( m );
        float v[ 4 ];
        for ( int r = 0; r < 4; ++r )
            v[ r ] = ( ( m[ r * 4 + 1 ] + m[ r * 4 ] ) * 0.0f + m[ r * 4 + 2 ] ) + m[ r * 4 + 3 ] * negative;
        plane_command( v );
        target_command( m_reflection );
        m_sort.set_screen( 8u );
        gfx_draw_params_t p;
        std::memcpy( p.m_eye, copy, 16 );
        p.m_lod = m_rejection;
        p.m_reflecting = 1u;
        p.m_clip[ 2 ] = 1.0f;
        p.m_clip[ 3 ] = negative;
        p.m_skip_models = m_depth_pass;
        m_hooks.draw( p, copy );
        m_hooks.trail( );
        if ( m_depth_pass != 0u )
            m_hooks.depth_extra( camera );
        m_sort.set_screen( 0xBu );
        end_command( m_reflection );
        m_hooks.sort_camera( copy );
        m_sort.m_key_hi = ( m_sort.m_key_hi & 0xFF7FFFFFu ) | 0x800000u;
    }

    // 0x558AC0.
    void scene( std::uint32_t target, const std::uint8_t* camera )
    {
        m_sort.m_key_hi = ( m_sort.m_key_hi & 0xFF7FFFFFu ) | 0x800000u;
        if ( m_depth_first != 0u )
            prepass_command( target );
        else
            target_command( target );
        if ( target )
        {
            m_sort.set_screen( 0xBu );
            end_command( target );
        }
        m_sort.set_screen( 8u );
        gfx_draw_params_t p;
        std::memcpy( p.m_eye, camera, 16 );
        p.m_lod = m_lod;
        p.m_skip_models = m_depth_pass;
        m_hooks.draw( p, camera );
        m_hooks.trail( );
        if ( m_depth_pass != 0u )
            m_hooks.depth_extra( camera );
        m_hooks.sort_camera( camera );
    }

    // 0x558FA0 (the viewport's +8 / +0xC / +0x10 / +0x14 only reach the dev corner's height ratio). Slot 0 of the main vtable is 0x558870: the reflection's texture.
    void overlay( )
    {
        const std::uint32_t texture = m_reflection ? m_hooks.target_texture( m_reflection ) : 0u;
        if ( m_show_reflection != 0u )
            return;   // the dev corner: STOPPED with the other dev pages
        if ( texture == 0u )
            return;
        m_sort.set_screen( 0xAu );
        const float rect[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
        m_hooks.overlay( texture, rect );
    }

    void record( std::uint32_t a, std::uint32_t b = 0u, std::uint32_t c = 0u, std::int32_t size = 4 )
    {
        std::uint8_t* r = m_sort.allocate( size );
        std::memcpy( r, &a, 4 );
        if ( size >= 8 )
            std::memcpy( r + 4, &b, 4 );
        if ( size >= 12 )
            std::memcpy( r + 8, &c, 4 );
    }

    // 0x5BC290.
    void target_command( std::uint32_t target )
    {
        m_sort.set_screen( 6u );
        record( target );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi, 0x5BC330u, 0u, 0u, 0u );
    }

    // 0x5BBFC0: the target, the depth pre-pass's three commands.
    void prepass_command( std::uint32_t target )
    {
        m_sort.set_screen( 6u );
        record( target );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi, 0x5BC1A0u, 0u, 0u, 0u );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi | 0x4C000u, 0x5BC210u, 0u, 0u, 0x13u );
        m_sort.set_screen( 8u );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi | 0x4000u, 0x5BC230u, 0u, 0u, 1u );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi | 0x8000u, 0x5BC260u, 0u, 0u, 2u );
    }

    // 0x5BC380.
    void end_command( std::uint32_t target )
    {
        record( target );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi, 0x5BC400u, 0u, 0u, 0u );
    }

    // 0x5BC410: the plane, on and off, around both screens.
    void plane_command( const float* v )
    {
        std::uint8_t* r = m_sort.allocate( 0x10 );
        std::memcpy( r, v, 16 );
        m_sort.set_screen( 6u );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi, 0x5BC5F0u, 0u, 0u, 0u );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi | 0x4C000u, 0x5BC610u, 0u, 0u, 0x13u );
        m_sort.set_screen( 8u );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi, 0x5BC5F0u, 0u, 0u, 0u );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi | 0x4C000u, 0x5BC610u, 0u, 0u, 0x13u );
    }

    // 0x5BC730 (ecx the texture, edx the target, xmm2 the amount): record 0x5BC7D0 reads: texture, target, amount.
    void blur_command( std::uint32_t texture, std::uint32_t target, float amount )
    {
        std::uint32_t bits;
        std::memcpy( &bits, &amount, 4 );
        record( texture, target, bits, 0x14 );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi, 0x5BC7D0u, 0u, 0u, 0u );
    }

    // 0x5BC660.
    void copy_command( std::uint32_t texture )
    {
        record( texture, 0u, 0u, 8 );
        m_sort.command( m_sort.m_key_lo, m_sort.m_key_hi, 0x5BC6F0u, 0u, 0u, 0u );
    }
};

// The commands' device side, the sort's callbacks by retail address (0x5BC6F0 / 0x5BC7D0: render/post.hxx). `clear` the four bytes at 0x7596E4, `target` the record's first word as a target, null for the chain.
inline bool gfx_composer_callback( gfx_device_t& d, std::uint32_t callback, const std::uint8_t* record, const std::uint8_t* clear, const gfx_target_t* target )
{
    switch ( callback )
    {
    case 0x5BC330u:
        d.set_target( target );
        d.clear( 3u, clear, 1.0f );
        return true;
    case 0x5BC1A0u:
        d.set_target( target );
        d.cull( 1u );
        d.clear( 2u, clear, 1.0f );
        d.colour_write( 0u );
        return true;
    case 0x5BC210u:
        d.colour_write( 1u );
        return true;
    case 0x5BC230u:
        d.depth_write( 0u );
        d.depth_func( 2u );
        return true;
    case 0x5BC260u:
        d.depth_write( 1u );
        d.depth_func( 1u );
        return true;
    case 0x5BC400u:   // the target's slot 1, retail's empty 0x401560
        return true;
    case 0x5BC5F0u:
    {
        float v[ 4 ];
        std::memcpy( v, record, 16 );
        d.clip_plane( v );
        return true;
    }
    case 0x5BC610u:
    {
        const float zero[ 4 ] = { };
        d.clip_plane( zero );
        return true;
    }
    default:
        return false;
    }
}
