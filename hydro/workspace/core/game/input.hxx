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

struct input_t
{
    float throttle = 0.0f;
    float brake = 0.0f;
    float steer = 0.0f;
    bool boost = false;
};

inline input_t read_input( )
{
    input_t input{};
#if defined( _WIN32 )
    XINPUT_STATE state{};
    if ( XInputGetState( 0, &state ) == ERROR_SUCCESS )
    {
        input.throttle = static_cast< float >( state.Gamepad.bRightTrigger ) / 255.0f;
        input.brake = static_cast< float >( state.Gamepad.bLeftTrigger ) / 255.0f;
        input.steer = static_cast< float >( state.Gamepad.sThumbLX ) / 32768.0f;
        input.boost = ( state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER ) != 0;
    }

    if ( GetAsyncKeyState( 'W' ) & 0x8000 )
        input.throttle = 1.0f;
    if ( GetAsyncKeyState( 'S' ) & 0x8000 )
        input.brake = 1.0f;
    if ( GetAsyncKeyState( 'A' ) & 0x8000 )
        input.steer -= 1.0f;
    if ( GetAsyncKeyState( 'D' ) & 0x8000 )
        input.steer += 1.0f;
    if ( input.steer > 1.0f )
        input.steer = 1.0f;
    if ( input.steer < -1.0f )
        input.steer = -1.0f;
    if ( GetAsyncKeyState( VK_SHIFT ) & 0x8000 )
        input.boost = true;
#endif
    return input;
}

// The input retail reads (boat batch 31): c_input_manager on InputDB's document, c_input_device on this platform. Not moved
// after init: the hooks point at the object. PLATFORM (ours, not 1:1): XInput 1.4's two calls are the static XInput library's,
// the CoreWindow key events 0x4DAEF0 / 0x4DAF70 are a poll of GetAsyncKeyState over retail's key table, edge-detected as
// WasKeyDown filters repeats; no focus test (0x4DAEF0's app byte +0x3C).
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
    for ( int vk = 0; vk < 256; ++vk )
    {
        const int key = g_input_key_table[ vk ];
        if ( key == 0 )
            continue;
        const bool down = ( GetAsyncKeyState( vk ) & 0x8000 ) != 0;
        if ( down && !in.was_down[ vk ] )
            in.device.key_down( key );
        else if ( !down && in.was_down[ vk ] )
            in.device.key_up( key );
        in.was_down[ vk ] = down;
    }
#endif
    in.device.tick( dt );
}
