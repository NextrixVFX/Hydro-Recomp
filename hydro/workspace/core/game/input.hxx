#pragma once

#include <workspace/core/input/device.hxx>

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
// WasKeyDown filters repeats. A CoreWindow delivers key events to the focused window only, so the poll reads the keys as up unless this process owns the foreground
// window: a second hydro, or any other program, typing does not reach this one. (0x4DAEF0's app byte +0x3C is not this: not read.)
struct platform_input_t
{
    c_input_manager manager;
    c_input_device device;
    float real_dt = 0.0f;
    bool was_down[ 256 ] = { };
};

// `document` is the decoded VuDBAsset/InputDB and outlives the manager.
inline void platform_input_init( platform_input_t& in, const value_t& document )
{
    in.device.hooks.user = &in;
#if defined( _WIN32 )
    in.device.hooks.get_state = []( void*, int pad, input_xinput_state_t* out ) -> std::uint32_t {
        XINPUT_STATE s{};
        const DWORD result = XInputGetState( static_cast< DWORD >( pad ), &s );
        if ( result == ERROR_SUCCESS )
        {
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
    in.device.hooks.set_state = []( void*, int pad, std::uint16_t left, std::uint16_t right ) -> std::uint32_t {
        XINPUT_VIBRATION v{};
        v.wLeftMotorSpeed = left;
        v.wRightMotorSpeed = right;
        return XInputSetState( static_cast< DWORD >( pad ), &v );
    };
#else
    in.device.hooks.get_state = []( void*, int, input_xinput_state_t* ) -> std::uint32_t { return 0x48Fu; };
    in.device.hooks.get_caps = []( void*, int, std::uint8_t* ) -> std::uint32_t { return 0x48Fu; };
#endif
    in.device.hooks.real_dt = []( void* u ) { return static_cast< platform_input_t* >( u )->real_dt; };
    in.device.bind( in.manager );
    in.manager.init( document );
    in.device.initialize( );
}

// The "Input" phase: `dt` the tick's step, `real_dt` the frame's real time (the tick manager's vt+0x3C).
inline void platform_input_tick( platform_input_t& in, float dt, float real_dt )
{
    in.real_dt = real_dt;
#if defined( _WIN32 )
    DWORD owner = 0;
    if ( const HWND foreground = GetForegroundWindow( ) )
        GetWindowThreadProcessId( foreground, &owner );
    const bool focused = owner == GetCurrentProcessId( );
    for ( int vk = 0; vk < 256; ++vk )
    {
        const int key = g_input_key_table[ vk ];
        if ( key == 0 )
            continue;
        const bool down = focused && ( GetAsyncKeyState( vk ) & 0x8000 ) != 0;
        if ( down && !in.was_down[ vk ] )
            in.device.key_down( key );
        else if ( !down && in.was_down[ vk ] )
            in.device.key_up( key );
        in.was_down[ vk ] = down;
    }
#endif
    in.device.tick( dt );
}
