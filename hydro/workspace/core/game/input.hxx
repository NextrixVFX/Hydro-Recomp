#pragma once

#include <workspace/core/input/device.hxx>

#include <functional>

#if defined( _WIN32 )
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#  include <Xinput.h>
#  pragma comment( lib, "xinput.lib" )
#endif

// One --input-script key's pad state (world/drive.hxx, ours: a test input, not retail's).
struct input_t
{
    float throttle = 0.0f;
    float brake = 0.0f;
    float steer = 0.0f;
    bool boost = false;
};

// The input retail reads (boat batch 31): c_input_manager on InputDB's document, c_input_device on this platform. Not moved
// after init: the hooks point at the object. PLATFORM (ours, not 1:1): XInput 1.4's two calls are the static XInput library's,
// the CoreWindow key events 0x4DAEF0 / 0x4DAF70 are a poll of GetAsyncKeyState over retail's key table, edge-detected as
// WasKeyDown filters repeats.
// OURS, THE FOCUS TEST (flagged, not retail): retail's keys are CoreWindow events and its window only gets them with focus; a poll of GetAsyncKeyState and XInputGetState reads the machine
// whatever window is in front. So with another process's window in front every key reads up (a held key gets its key_up) and every connected pad reads neutral: not "not connected", which
// would raise OnGamePadDisconnected. Retail's control method test 0x4AEC40 itself has no focus test and no dead zone of its own. `--background` (a test driver) keeps the old reading.
struct platform_input_env_t
{
    bool any_window = false;                                // --background
    std::function< void( bool connected, int pad ) > pad;   // OnGamePadConnected / OnGamePadDisconnected
};

inline platform_input_env_t g_platform_input;

struct platform_input_t
{
    c_input_manager manager;
    c_input_device device;
    float real_dt = 0.0f;
    bool was_down[ 256 ] = { };
    bool focused = true;
};

// The process owns the foreground window.
inline bool platform_input_focused( )
{
#if defined( _WIN32 )
    if ( g_platform_input.any_window )
        return true;
    DWORD owner = 0;
    const HWND foreground = GetForegroundWindow( );
    if ( foreground != nullptr )
        GetWindowThreadProcessId( foreground, &owner );
    return owner == GetCurrentProcessId( );
#else
    return true;
#endif
}

// `document` is the decoded VuDBAsset/InputDB and outlives the manager.
inline void platform_input_init( platform_input_t& in, const value_t& document )
{
    in.device.hooks.user = &in;
#if defined( _WIN32 )
    in.device.hooks.get_state = []( void* user, int pad, input_xinput_state_t* out ) -> std::uint32_t {
        XINPUT_STATE s{};
        const DWORD result = XInputGetState( static_cast< DWORD >( pad ), &s );
        if ( result == ERROR_SUCCESS )
        {
            if ( !static_cast< platform_input_t* >( user )->focused )
                s.Gamepad = XINPUT_GAMEPAD{ };
            out->packet = s.dwPacketNumber;
            out->buttons = s.Gamepad.wButtons;
            out->left_trigger = s.Gamepad.bLeftTrigger;
            out->right_trigger = s.Gamepad.bRightTrigger;
            out->left_x = s.Gamepad.sThumbLX;
            out->left_y = s.Gamepad.sThumbLY;
            out->right_x = s.Gamepad.sThumbRX;
            out->right_y = s.Gamepad.sThumbRY;
        }
        return result;
    };
    in.device.hooks.get_caps = []( void*, int pad, std::uint8_t* caps ) -> std::uint32_t {
        XINPUT_CAPABILITIES c{};
        const DWORD result = XInputGetCapabilities( static_cast< DWORD >( pad ), 0, &c );
        caps[ 0 ] = c.Type;
        caps[ 1 ] = c.SubType;
        return result;
    };
#else
    in.device.hooks.get_state = []( void*, int, input_xinput_state_t* ) -> std::uint32_t { return 0x48Fu; };
    in.device.hooks.get_caps = []( void*, int, std::uint8_t* ) -> std::uint32_t { return 0x48Fu; };
#endif
    in.device.hooks.real_dt = []( void* u ) { return static_cast< platform_input_t* >( u )->real_dt; };
    in.device.hooks.pad_event = []( void*, bool connected, int pad ) {
        if ( g_platform_input.pad )
            g_platform_input.pad( connected, pad );
    };
    in.device.bind( in.manager );
    in.manager.init( document );
    in.device.initialize( );
}

// The "Input" phase: `dt` the tick's step, `real_dt` the frame's real time (the tick manager's vt+0x3C).
inline void platform_input_tick( platform_input_t& in, float dt, float real_dt )
{
    in.real_dt = real_dt;
    in.focused = platform_input_focused( );
#if defined( _WIN32 )
    for ( int vk = 0; vk < 256; ++vk )
    {
        const int key = g_input_key_table[ vk ];
        if ( key == 0 )
            continue;
        const bool down = in.focused && ( GetAsyncKeyState( vk ) & 0x8000 ) != 0;
        if ( down && !in.was_down[ vk ] )
            in.device.key_down( key );
        else if ( !down && in.was_down[ vk ] )
            in.device.key_up( key );
        in.was_down[ vk ] = down;
    }
#endif
    in.device.tick( dt );
}
