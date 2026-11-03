#pragma once

#include <impl/host.hxx>
#include <workspace/core/input/device.hxx>

#include <functional>

#if defined( _WIN32 )
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#  include <Xinput.h>
#  pragma comment( lib, "xinput.lib" )
#elif HYDRO_HOST_WASM
#  include <emscripten.h>
#  include <emscripten/html5.h>
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

#if HYDRO_HOST_WASM
// DOM keyCode matches the virtual-key index of g_input_key_table. The page's log box starts focused, so the
// listener is on the window and the canvas is focused once the menu is up.
inline bool g_platform_wasm_keys[ 256 ]{};

inline void platform_wasm_focus_canvas( )
{
    EM_ASM( {
        var canvas = document.getElementById( 'canvas' );
        if ( !canvas )
            return;
        canvas.tabIndex = 0;
        canvas.focus( );
    } );
}

// A real keypress carries the Windows virtual key in keyCode. A dispatched event often leaves that 0 and only fills `code`.
inline int platform_wasm_vk( const EmscriptenKeyboardEvent* event )
{
    if ( event->keyCode > 0 && event->keyCode < 256 && g_input_key_table[ event->keyCode ] != 0 )
        return static_cast< int >( event->keyCode );
    const char* code = event->code;
    const char* key = event->key;
    if ( std::strcmp( code, "Enter" ) == 0 || std::strcmp( key, "Enter" ) == 0 )
        return 13;
    if ( std::strcmp( code, "Space" ) == 0 || std::strcmp( key, " " ) == 0 )
        return 32;
    if ( std::strcmp( code, "Escape" ) == 0 || std::strcmp( key, "Escape" ) == 0 )
        return 27;
    if ( std::strcmp( code, "Backspace" ) == 0 )
        return 8;
    if ( std::strcmp( code, "Tab" ) == 0 )
        return 9;
    if ( std::strcmp( code, "ArrowLeft" ) == 0 )
        return 37;
    if ( std::strcmp( code, "ArrowUp" ) == 0 )
        return 38;
    if ( std::strcmp( code, "ArrowRight" ) == 0 )
        return 39;
    if ( std::strcmp( code, "ArrowDown" ) == 0 )
        return 40;
    if ( std::strcmp( code, "ShiftLeft" ) == 0 || std::strcmp( code, "ShiftRight" ) == 0 )
        return 16;
    if ( std::strcmp( code, "ControlLeft" ) == 0 || std::strcmp( code, "ControlRight" ) == 0 )
        return 17;
    if ( std::strcmp( code, "AltLeft" ) == 0 || std::strcmp( code, "AltRight" ) == 0 )
        return 18;
    if ( code[ 0 ] == 'K' && code[ 1 ] == 'e' && code[ 2 ] == 'y' && code[ 3 ] >= 'A' && code[ 3 ] <= 'Z' && code[ 4 ] == '\0' )
        return code[ 3 ];
    if ( std::strncmp( code, "Digit", 5 ) == 0 && code[ 5 ] >= '0' && code[ 5 ] <= '9' && code[ 6 ] == '\0' )
        return code[ 5 ];
    if ( code[ 0 ] == 'F' && code[ 1 ] >= '1' && code[ 1 ] <= '9' )
    {
        int n = code[ 1 ] - '0';
        if ( code[ 2 ] == '0' || code[ 2 ] == '1' || code[ 2 ] == '2' )
            n = ( code[ 1 ] - '0' ) * 10 + ( code[ 2 ] - '0' );
        else if ( code[ 2 ] != '\0' )
            n = 0;
        if ( n >= 1 && n <= 12 )
            return 111 + n;
    }
    if ( ( ( key[ 0 ] >= 'a' && key[ 0 ] <= 'z' ) || ( key[ 0 ] >= 'A' && key[ 0 ] <= 'Z' ) || ( key[ 0 ] >= '0' && key[ 0 ] <= '9' ) ) && key[ 1 ] == '\0' )
        return key[ 0 ] >= 'a' ? key[ 0 ] - 'a' + 'A' : key[ 0 ];
    return event->keyCode > 0 && event->keyCode < 256 ? static_cast< int >( event->keyCode ) : 0;
}

inline EM_BOOL platform_wasm_key( int type, const EmscriptenKeyboardEvent* event, void* )
{
    if ( event == nullptr )
        return EM_TRUE;
    const int vk = platform_wasm_vk( event );
    if ( vk > 0 && vk < 256 )
        g_platform_wasm_keys[ vk ] = type == EMSCRIPTEN_EVENT_KEYDOWN;
    return EM_TRUE;
}

inline EM_BOOL platform_wasm_pointer( int, const EmscriptenMouseEvent*, void* )
{
    platform_wasm_focus_canvas( );
    return EM_FALSE;
}

// Standard Gamepad buttons, in order, as XInput wButtons. Triggers stay in the trigger bytes.
inline std::uint16_t platform_wasm_pad_bit( int button )
{
    static const std::uint16_t bits[ 16 ] = {
        0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0, 0, 0x0020, 0x0010, 0x0040, 0x0080, 0x0001, 0x0002, 0x0004, 0x0008 };
    return button >= 0 && button < 16 ? bits[ button ] : 0;
}

inline bool platform_wasm_gamepad( int pad, EmscriptenGamepadEvent& out )
{
    emscripten_sample_gamepad_data( );
    const int count = emscripten_get_num_gamepads( );
    int seen = 0;
    for ( int i = 0; i < count; ++i )
    {
        EmscriptenGamepadEvent event{};
        if ( emscripten_get_gamepad_status( i, &event ) != EMSCRIPTEN_RESULT_SUCCESS || !event.connected )
            continue;
        if ( seen++ != pad )
            continue;
        out = event;
        return true;
    }
    return false;
}
#endif

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
    in.device.hooks.set_state = []( void*, int pad, std::uint16_t left, std::uint16_t right ) -> std::uint32_t {
        XINPUT_VIBRATION v{};
        v.wLeftMotorSpeed = left;
        v.wRightMotorSpeed = right;
        return XInputSetState( static_cast< DWORD >( pad ), &v );
    };
#elif HYDRO_HOST_WASM
    platform_wasm_focus_canvas( );
    emscripten_set_keydown_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, platform_wasm_key );
    emscripten_set_keyup_callback( EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, platform_wasm_key );
    emscripten_set_mousedown_callback( "#canvas", nullptr, EM_TRUE, platform_wasm_pointer );
    in.device.hooks.get_state = []( void*, int pad, input_xinput_state_t* out ) -> std::uint32_t {
        EmscriptenGamepadEvent pad_state{};
        if ( out == nullptr || !platform_wasm_gamepad( pad, pad_state ) )
            return 0x48Fu;
        out->packet = 0;
        out->buttons = 0;
        const int buttons = pad_state.numButtons < 64 ? pad_state.numButtons : 64;
        for ( int b = 0; b < buttons; ++b )
            if ( pad_state.digitalButton[ b ] )
                out->buttons = static_cast< std::uint16_t >( out->buttons | platform_wasm_pad_bit( b ) );
        auto stick = []( double v ) -> std::int16_t {
            if ( !( v > -2.0 ) || !( v < 2.0 ) )
                return 0;
            if ( v > 1.0 )
                v = 1.0;
            if ( v < -1.0 )
                v = -1.0;
            return static_cast< std::int16_t >( v * 32767.0 );
        };
        auto trigger = []( double v ) -> std::uint8_t {
            if ( !( v > 0.0 ) )
                return 0;
            if ( v > 1.0 )
                v = 1.0;
            return static_cast< std::uint8_t >( v * 255.0 );
        };
        out->left_x = pad_state.numAxes > 0 ? stick( pad_state.axis[ 0 ] ) : 0;
        out->left_y = pad_state.numAxes > 1 ? stick( -pad_state.axis[ 1 ] ) : 0;
        out->right_x = pad_state.numAxes > 2 ? stick( pad_state.axis[ 2 ] ) : 0;
        out->right_y = pad_state.numAxes > 3 ? stick( -pad_state.axis[ 3 ] ) : 0;
        out->left_trigger = buttons > 6 ? trigger( pad_state.analogButton[ 6 ] ) : 0;
        out->right_trigger = buttons > 7 ? trigger( pad_state.analogButton[ 7 ] ) : 0;
        return 0;
    };
    in.device.hooks.get_caps = []( void*, int pad, std::uint8_t* caps ) -> std::uint32_t {
        EmscriptenGamepadEvent pad_state{};
        if ( !platform_wasm_gamepad( pad, pad_state ) )
            return 0x48Fu;
        if ( caps != nullptr )
        {
            std::memset( caps, 0, 20 );
            caps[ 0 ] = 1;
            caps[ 1 ] = 1;   // XINPUT_DEVSUBTYPE_GAMEPAD, which the device turns into subtype 0
        }
        return 0;
    };
    in.device.hooks.set_state = []( void*, int, std::uint16_t, std::uint16_t ) -> std::uint32_t { return 0; };
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
    DWORD owner = 0;
    if ( const HWND foreground = GetForegroundWindow( ) )
        GetWindowThreadProcessId( foreground, &owner );
    const bool focused = owner == GetCurrentProcessId( );
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
#elif HYDRO_HOST_WASM
    for ( int vk = 0; vk < 256; ++vk )
    {
        const int key = g_input_key_table[ vk ];
        if ( key == 0 )
            continue;
        const bool down = g_platform_wasm_keys[ vk ];
        if ( down && !in.was_down[ vk ] )
            in.device.key_down( key );
        else if ( !down && in.was_down[ vk ] )
            in.device.key_up( key );
        in.was_down[ vk ] = down;
    }
#endif
    in.device.tick( dt );
}
