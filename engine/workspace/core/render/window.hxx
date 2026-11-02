#pragma once

#include <impl/host.hxx>

#if HYDRO_HOST_WINDOWS
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#endif

class c_window
{
public:
    c_window( ) = default;
    ~c_window( );

    // `background` keeps the window off the user's primary display and out of
    // the foreground: it is placed on a secondary monitor when one exists,
    // shown without activation, and kept out of the taskbar and alt-tab. A
    // headless bench run must never steal focus from whatever the user is
    // doing.
    bool create( int width, int height, const char* title, bool background = false );
    bool pump( );

    int width( ) const { return m_width; }
    int height( ) const { return m_height; }
    // No client area (minimized, or dragged down to the caption): nothing to draw and nothing to size the chain to. width( ) / height( ) keep the last real size.
    bool minimized( ) const { return m_minimized; }
    // Ours (a test driver, --resize-at): the client to this size without taking the foreground; 0 x 0 minimizes.
    void script_size( int width, int height );

#if HYDRO_HOST_WINDOWS
    HWND handle( ) const { return m_hwnd; }
#endif

private:
#if HYDRO_HOST_WINDOWS
    static LRESULT CALLBACK procedure( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam );
    LRESULT on_message( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam );

    HWND m_hwnd{};
#endif
    int  m_width{};
    int  m_height{};
    bool m_open{};
    bool m_minimized{};
};

#include <workspace/utility/logger.hxx>

c_window::~c_window( )
{
#if HYDRO_HOST_WINDOWS
    if ( m_hwnd )
        DestroyWindow( m_hwnd );
#endif
}

#if HYDRO_HOST_WINDOWS
namespace
{
    struct monitor_pick_t
    {
        RECT secondary{};
        RECT primary{};
        bool found_secondary = false;
    };

    inline BOOL CALLBACK pick_monitor( HMONITOR monitor, HDC, LPRECT, LPARAM user )
    {
        MONITORINFO info{};
        info.cbSize = sizeof( info );
        if ( !GetMonitorInfoW( monitor, &info ) )
            return TRUE;

        auto* pick = reinterpret_cast< monitor_pick_t* >( user );
        if ( info.dwFlags & MONITORINFOF_PRIMARY )
            pick->primary = info.rcWork;
        else if ( !pick->found_secondary )
        {
            pick->secondary = info.rcWork;
            pick->found_secondary = true;
        }
        return TRUE;
    }
}
#endif

bool c_window::create( int width, int height, const char* title, bool background )
{
#if HYDRO_HOST_WINDOWS
    wchar_t wide_title[ 128 ]{};
    if ( !title || !title[ 0 ] )
        title = "engine";
    MultiByteToWideChar( CP_UTF8, 0, title, -1, wide_title, 128 );

    HINSTANCE instance = GetModuleHandleW( nullptr );

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof( window_class );
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = &c_window::procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW( nullptr, IDC_ARROW );
    window_class.lpszClassName = L"hydro_window";

    if ( !RegisterClassExW( &window_class ) && GetLastError( ) != ERROR_CLASS_ALREADY_EXISTS )
    {
        LOG_ERROR_WIN32( "RegisterClassExW" );
        return false;
    }

    // Retail's UWP window is the user's to size (the shots in hydro-tools\reference are about 1200 x 900).
    const DWORD style = WS_OVERLAPPEDWINDOW;
    // WS_EX_NOACTIVATE stops the window taking focus on creation or click;
    // WS_EX_TOOLWINDOW keeps it out of the taskbar and the alt-tab list.
    const DWORD extended = background ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0;
    RECT bounds{ 0, 0, width, height };
    AdjustWindowRectEx( &bounds, style, FALSE, extended );

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if ( background )
    {
        monitor_pick_t pick{};
        EnumDisplayMonitors( nullptr, nullptr, &pick_monitor, reinterpret_cast< LPARAM >( &pick ) );
        const RECT& area = pick.found_secondary ? pick.secondary : pick.primary;
        x = area.left;
        y = area.top;
        // With no second display, tuck it into the bottom-right corner rather
        // than landing in the middle of whatever the user is looking at.
        if ( !pick.found_secondary )
        {
            x = area.right - ( bounds.right - bounds.left );
            y = area.bottom - ( bounds.bottom - bounds.top );
        }
    }

    m_hwnd = CreateWindowExW(
        extended,
        window_class.lpszClassName,
        wide_title,
        style,
        x,
        y,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        nullptr,
        nullptr,
        instance,
        this
    );

    if ( !m_hwnd )
    {
        LOG_ERROR_WIN32( "CreateWindowExW" );
        return false;
    }

    RECT client{};
    GetClientRect( m_hwnd, &client );
    m_width = client.right > 0 ? client.right : width;
    m_height = client.bottom > 0 ? client.bottom : height;
    m_open = true;
    // SW_SHOWNOACTIVATE leaves the caller's foreground window alone. Pairing it
    // with SWP_NOACTIVATE and HWND_BOTTOM keeps a bench run from ever surfacing
    // over whatever is in front.
    ShowWindow( m_hwnd, background ? SW_SHOWNOACTIVATE : SW_SHOW );
    if ( background )
        SetWindowPos( m_hwnd, HWND_BOTTOM, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
    UpdateWindow( m_hwnd );
    LOG_SUCCESS( "window %dx%d%s", m_width, m_height, background ? " (background)" : "" );
    return true;
#else
    ( void )width;
    ( void )height;
    ( void )title;
    ( void )background;
    LOG_ERROR( "window create is unimplemented on %s", host::name( ) );
    return false;
#endif
}

bool c_window::pump( )
{
#if HYDRO_HOST_WINDOWS
    MSG message{};
    while ( PeekMessageW( &message, nullptr, 0, 0, PM_REMOVE ) )
    {
        if ( message.message == WM_QUIT )
            m_open = false;

        TranslateMessage( &message );
        DispatchMessageW( &message );
    }
#endif
    return m_open;
}

void c_window::script_size( int width, int height )
{
#if HYDRO_HOST_WINDOWS
    if ( width <= 0 || height <= 0 )
    {
        ShowWindow( m_hwnd, SW_SHOWMINNOACTIVE );
        return;
    }
    if ( m_minimized )
        ShowWindow( m_hwnd, SW_SHOWNOACTIVATE );
    RECT bounds{ 0, 0, width, height };
    AdjustWindowRectEx( &bounds, static_cast< DWORD >( GetWindowLongPtrW( m_hwnd, GWL_STYLE ) ), FALSE, static_cast< DWORD >( GetWindowLongPtrW( m_hwnd, GWL_EXSTYLE ) ) );
    SetWindowPos( m_hwnd, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE );
#else
    ( void )width;
    ( void )height;
#endif
}

#if HYDRO_HOST_WINDOWS
LRESULT CALLBACK c_window::procedure( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam )
{
    if ( message == WM_NCCREATE )
    {
        auto* created = reinterpret_cast< CREATESTRUCTW* >( lparam );
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, reinterpret_cast< LONG_PTR >( created->lpCreateParams ) );
    }

    auto* window = reinterpret_cast< c_window* >( GetWindowLongPtrW( hwnd, GWLP_USERDATA ) );
    if ( window )
        return window->on_message( hwnd, message, wparam, lparam );

    return DefWindowProcW( hwnd, message, wparam, lparam );
}

LRESULT c_window::on_message( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam )
{
    if ( message == WM_DESTROY )
    {
        m_open = false;
        m_hwnd = nullptr;
        PostQuitMessage( 0 );
        return 0;
    }

    if ( message == WM_ERASEBKGND )
        return 1;

    // Only noted: the frame loop applies it at the top of its next frame, so a border drag (the pump sits in the modal loop until it ends) resizes once, when it has settled.
    if ( message == WM_SIZE )
    {
        const int width = LOWORD( lparam ), height = HIWORD( lparam );
        m_minimized = wparam == SIZE_MINIMIZED || width <= 0 || height <= 0;
        if ( !m_minimized )
        {
            m_width = width;
            m_height = height;
        }
        return 0;
    }

    return DefWindowProcW( hwnd, message, wparam, lparam );
}
#endif
