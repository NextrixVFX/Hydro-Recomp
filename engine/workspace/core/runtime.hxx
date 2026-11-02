#pragma once

#include <impl/engine.hxx>
#include <impl/host.hxx>
#include <workspace/utility/logger.hxx>
#include <workspace/utility/crash.hxx>
#include <workspace/core/render/window.hxx>
#include <workspace/core/render/device.hxx>
#include <workspace/core/render/swap.hxx>
#include <workspace/core/render/shader.hxx>
#include <workspace/core/scene/draw.hxx>
#include <workspace/core/scene/scene.hxx>
#include <workspace/core/scene/viewport.hxx>
#include <workspace/core/scene/screen.hxx>
#include <workspace/core/render/mesh.hxx>
#include <workspace/core/scene/composer.hxx>
#include <workspace/core/bench.hxx>

#include <cstdio>
#include <cstring>

namespace
{
    int    g_argument_count = 0;
    char** g_argument_values = nullptr;
}

void c_engine::arguments( int count, char** values )
{
    g_argument_count = count;
    g_argument_values = values;
}

// Returns the value after --name, an empty string when the flag is present
// with no value, and nullptr when it is absent. Presence is tested against
// nullptr so bare flags like --bench still read as set.
const char* c_engine::option( const char* name )
{
    if ( name == nullptr || g_argument_values == nullptr )
        return nullptr;
    char flag[ 128 ]{};
    std::snprintf( flag, sizeof( flag ), "--%s", name );
    for ( int index = 1; index < g_argument_count; ++index )
    {
        if ( std::strcmp( g_argument_values[ index ], flag ) != 0 )
            continue;
        const char* next = index + 1 < g_argument_count ? g_argument_values[ index + 1 ] : nullptr;
        if ( next != nullptr && next[ 0 ] != '-' )
            return next;
        return "";
    }
    return nullptr;
}

void c_engine::attach( c_draw& draw )
{
    g_draw( ).add( draw );
}

void c_engine::add( c_node& node )
{
    g_scene( ).add( node );
}

void c_engine::look( const float position[ 3 ] )
{
    g_viewport( ).look( position );
}

void c_engine::camera( const float* eye, const float* target, float far_plane )
{
    if ( eye == nullptr || target == nullptr )
        return;
    g_viewport( ).aim( eye, target, far_plane );
}

void c_engine::lens( float tan_half, float near_plane )
{
    g_viewport( ).lens( tan_half, near_plane );
}

void c_engine::flip_interval( int interval )
{
    g_swap_interval = static_cast< std::uint32_t >( interval );
}

void c_engine::sun( const float* direction )
{
    g_light( ).aim( direction );
}

void c_engine::rig( const float* ambient, const float* front, const float* back )
{
    g_light( ).rig( ambient, front, back );
}

void c_engine::fog( float start, float end, float red, float green, float blue )
{
    g_geometry( ).fog( start, end, red, green, blue );
}

void c_engine::clock( float seconds )
{
    g_geometry( ).clock( seconds );
}

void c_engine::guard( )
{
#if HYDRO_HOST_WINDOWS
    c_crash::install( );
#endif
}

void c_engine::screen( const unsigned char* rgba, int width, int height )
{
    g_screen( ).pixels = rgba;
    g_screen( ).width = width;
    g_screen( ).height = height;
}

namespace
{
    void ( *g_overlay_pass )( void* ) = nullptr;
    void* g_overlay_user = nullptr;
}

void c_engine::overlay( void ( *pass )( void* ), void* user )
{
    g_overlay_pass = pass;
    g_overlay_user = user;
}

int c_engine::run( const char* title )
{
    c_engine::guard( );
    LOG_INFO( "host %s", host::name( ) );

    // A --bench run is automation, not play: put its window on a secondary
    // display and never let it take focus. Any run can ask for the same with
    // --background.
    const bool background = c_engine::option( "bench" ) != nullptr
        || c_engine::option( "background" ) != nullptr;

    c_window window{};
    // Ours (a test driver): --window-size WxH, the window a comparison with a retail shot needs.
    int window_w = 1280, window_h = 720;
    if ( const char* size = c_engine::option( "window-size" ) )
        sscanf_s( size, "%dx%d", &window_w, &window_h );
    if ( !window.create( window_w, window_h, title, background ) )
        return 1;

    c_device device{};
    if ( !device.create( ) )
        return 1;

    c_swap swap{};
    if ( !swap.create( device, window ) )
        return 1;

    if ( !g_shader( ).create( ) )
        return 1;

    c_composer composer{};
    g_draw( ).add( composer );
    LOG_SUCCESS( "draw handlers %d  scene nodes %d", g_draw( ).count( ), g_scene( ).count( ) );

    g_bench( ).configure( );

    // Ours (a test driver): --exit-after N ends the run after N frames, so the real window path can be run unattended.
    const char* exit_after = c_engine::option( "exit-after" );
    long frames = 0;

    // Ours, not retail: the frame rate in the title bar every half second, the average and the worst frame of that
    // window. In the title so it costs nothing to draw and never reaches a still.
    double fps_start = trace::now( ), fps_last = fps_start, fps_worst = 0.0;
    int fps_frames = 0;

    // Ours (a test driver): --frame-log FILE keeps every frame's phases in memory and writes them when the run ends: finding a hitch must not cause one.
    struct frame_row_t
    {
        double at, pump, tick, overlay, present, end;
        int lines;
    };
    const char* frame_log = c_engine::option( "frame-log" );
    std::vector< frame_row_t > frame_rows;
    if ( frame_log )
        frame_rows.reserve( 400000 );

    while ( window.pump( ) )
    {
        const double phase_pump = trace::now( );
        const int lines_before = logger::g_lines;
        g_draw( ).tick( );
        const double phase_tick = trace::now( );
        if ( g_overlay_pass )
            g_overlay_pass( g_overlay_user );
        const double phase_overlay = trace::now( );
        swap.present( );
        const double phase_present = trace::now( );
        if ( frame_log )
            frame_rows.push_back( { fps_last, phase_pump, phase_tick, phase_overlay, phase_present, 0.0, logger::g_lines - lines_before } );

        const double fps_now = trace::now( );
        fps_worst = ( std::max )( fps_worst, fps_now - fps_last );
        fps_last = fps_now;
        ++fps_frames;
        if ( fps_now - fps_start >= 500.0 )   // trace::now is milliseconds
        {
#if HYDRO_HOST_WINDOWS
            const double span = fps_now - fps_start;
            wchar_t text[ 96 ];
            swprintf_s( text, L"%hs  |  %.0f fps  |  %.2f ms  |  worst %.2f ms", title, fps_frames * 1000.0 / span,
                span / fps_frames, fps_worst );
            SetWindowTextW( window.handle( ), text );
#endif
            fps_start = fps_now;
            fps_worst = 0.0;
            fps_frames = 0;
        }
        if ( frame_log )
            frame_rows.back( ).end = trace::now( );

        if ( !g_bench( ).step( ) )
            break;
        if ( exit_after && ++frames >= std::atol( exit_after ) )
            break;
    }

    if ( frame_log )
    {
        FILE* out = nullptr;
        if ( fopen_s( &out, frame_log, "w" ) == 0 && out )
        {
            for ( const frame_row_t& r : frame_rows )
                std::fprintf( out, "%.3f %.3f %.3f %.3f %.3f %.3f %d\n", r.at, r.pump, r.tick, r.overlay, r.present, r.end, r.lines );
            std::fclose( out );
        }
    }

    const int code = g_bench( ).report( );
    g_geometry( ).release( );
    return code;
}
