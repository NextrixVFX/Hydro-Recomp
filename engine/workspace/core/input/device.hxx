#pragma once

#include <workspace/core/input/manager.hxx>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

// The device side of retail's input (boat batch 31, oracle case input_device): what g_vuinputmanagerimpl 0x759038 reads.
// VuGamePad / VuMetroGamePad g 0x759040 (ctor 0x57E910, vtable 0x74C8AC), VuMetroKeyboard g 0x75906C (ctor 0x538F10 + slot 5
// 0x538BB0, vtable 0x74D438), VuControlMethodManager's device choice (g 0x758FC0: 0x4AEBE0 / 0x4AEC40). All three tick in
// the "Input" phase, in the order of c_input_device::tick.
//
// THE PLATFORM, each a hook (input_device_hooks_t): XInputGetState / XInputGetCapabilities (XINPUT1_4 ordinals 2 / 4), the
// real frame time (tick manager vt+0x3C), the two event broadcasts. Not recreated, platform: the rumble (0x57EFE0's second
// half, XInputSetState; the +0x30 / +0x34 fade), "RumbleActive" 0x57EFB0, the CoreWindow handlers 0x4DAEF0 / 0x4DAF70 (the
// platform calls key_down / key_up: 0x4DAEF0 ignores a repeat, WasKeyDown set, and both ignore a key while the app's +0x3C
// byte is set).
// SUBSTITUTION: retail's keyboard callback list (+4, priorities +0x150; 0x4F6700 / 0x539020) is the one call to the control
// method's 0x4AEBE0; the other two listeners that add themselves (0x423560 the HUD, 0x5EFEA0 VuOnKeyboardEventEntity) are not
// in it. std::array / inline storage for the VuArrays 0x57E8A0 / 0x524050 fill.

// XINPUT_STATE as retail reads it.
struct input_xinput_state_t
{
    std::uint32_t packet = 0;
    std::uint16_t buttons = 0;
    std::uint8_t left_trigger = 0;
    std::uint8_t right_trigger = 0;
    std::int16_t left_x = 0;
    std::int16_t left_y = 0;
    std::int16_t right_x = 0;
    std::int16_t right_y = 0;
};

struct input_device_hooks_t
{
    void* user = nullptr;
    std::uint32_t ( *get_state )( void* user, int pad, input_xinput_state_t* state ) = nullptr;   // 0 ok, 0x48F not connected
    std::uint32_t ( *get_caps )( void* user, int pad, std::uint8_t* caps20 ) = nullptr;           // 0 ok
    float ( *real_dt )( void* user ) = nullptr;                                                   // [0x759008] vt+0x3C
    void ( *pad_event )( void* user, bool connected, int pad ) = nullptr;                         // OnGamePadConnected / Disconnected
    void ( *method_event )( void* user ) = nullptr;                                               // OnControlMethodChanged
};

// The 56 byte controller of VuGamePad (+0x1C, four of them): +4 connected, +8 the subtype, +0xC the button bits, +0x10 the
// axis floats.
struct input_controller_t
{
    std::uint8_t connected = 0;
    std::int32_t subtype = 0;
    std::uint32_t buttons = 0;
    float axes[ 6 ] = { };
};

// 0x57E910's tables: the axes (0x57E8A0: name, hash, minimum, 1), then the buttons (name, hash), each at the index a
// binding names: a button's index is its XINPUT_GAMEPAD bit.
struct input_axis_name_t
{
    const char* name;
    float minimum;
};

inline constexpr input_axis_name_t g_input_axis_names[ 6 ] = {
    { "LEFT_STICK_X", -1.0f }, { "LEFT_STICK_Y", -1.0f }, { "RIGHT_STICK_X", -1.0f }, { "RIGHT_STICK_Y", -1.0f },
    { "LEFT_TRIGGER", 0.0f }, { "RIGHT_TRIGGER", 0.0f } };

inline constexpr const char* g_input_button_names[ 16 ] = {
    "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT", "START", "BACK", "LEFT_THUMB", "RIGHT_THUMB", "LEFT_SHOULDER", "RIGHT_SHOULDER",
    "???", "BIGBUTTON", "A", "B", "X", "Y" };

// 0x538BB0: the table at the keyboard's +0x154, a virtual key's index in the key names of 0x7358B8 (0: no key).
inline constexpr std::array< std::uint8_t, 256 > input_key_table( )
{
    std::array< std::uint8_t, 256 > t{ };
    for ( int k = 0; k < 10; ++k )
        t[ 48 + k ] = static_cast< std::uint8_t >( 1 + k );    // '0'..'9'
    for ( int k = 0; k < 26; ++k )
        t[ 65 + k ] = static_cast< std::uint8_t >( 11 + k );   // 'A'..'Z'
    for ( int k = 0; k < 12; ++k )
        t[ 112 + k ] = static_cast< std::uint8_t >( 69 + k );  // F1..F12
    t[ 8 ] = 37, t[ 9 ] = 40, t[ 13 ] = 38, t[ 16 ] = 57, t[ 17 ] = 59, t[ 18 ] = 58, t[ 19 ] = 62, t[ 27 ] = 60, t[ 32 ] = 39;
    t[ 33 ] = 67, t[ 34 ] = 68, t[ 35 ] = 66, t[ 36 ] = 65, t[ 37 ] = 52, t[ 38 ] = 54, t[ 39 ] = 53, t[ 40 ] = 55, t[ 45 ] = 63, t[ 46 ] = 64;
    t[ 145 ] = 61, t[ 186 ] = 46, t[ 187 ] = 44, t[ 188 ] = 41, t[ 189 ] = 43, t[ 190 ] = 42, t[ 191 ] = 47, t[ 192 ] = 48;
    t[ 219 ] = 50, t[ 220 ] = 49, t[ 221 ] = 51, t[ 222 ] = 45;
    return t;
}

inline constexpr std::array< std::uint8_t, 256 > g_input_key_table = input_key_table( );

class c_input_device
{
public:
    input_device_hooks_t hooks{};

    // Hand this device to the manager: its input_hooks_t, the names and the pad state.
    void bind( c_input_manager& manager )
    {
        m_manager = &manager;
        manager.hooks.user = this;
        manager.hooks.axis_index = []( void*, const char* name ) { return axis_index( name ); };
        manager.hooks.button_index = []( void*, const char* name ) { return button_index( name ); };
        manager.hooks.pad = []( void* u, int controller ) {
            const input_controller_t& c = static_cast< c_input_device* >( u )->m_controllers[ controller ];
            input_pad_t pad;
            pad.buttons = c.buttons;
            pad.axes = c.axes;
            return pad;
        };
        manager.hooks.key = []( void* u, int key ) { return static_cast< c_input_device* >( u )->m_down[ key ]; };
    }

    // Slot 5 of the pad 0x57EF40: the first scan, no event (0x57E6B0 zero-fills the arrays). The control method's slot 5
    // 0x4AE890 then sets the manager's method to 1, the control method's ctor 0x4AE6E0 left +0x18 at 1 too.
    void initialize( )
    {
        scan( false );
        if ( m_manager != nullptr )
            m_manager->set_method( 1 );
    }

    // 0x57E770: the index of an axis by the name's hash, -1 when absent.
    static int axis_index( const char* name )
    {
        const std::uint32_t h = input_hash( name );
        for ( int k = 0; k < 6; ++k )
            if ( input_hash( g_input_axis_names[ k ].name ) == h )
                return k;
        return -1;
    }

    // 0x57E7D0.
    static int button_index( const char* name )
    {
        const std::uint32_t h = input_hash( name );
        for ( int k = 0; k < 16; ++k )
            if ( input_hash( g_input_button_names[ k ] ) == h )
                return k;
        return -1;
    }

    // 0x57F4B0: on a gamepad (subtype 0) only. Each stick's dead zone rescaled, then squared with its sign. The triggers
    // are left alone. A NaN falls through both compares to 0.
    static void dead_zone( input_controller_t& c )
    {
        if ( c.subtype != 0 )
            return;
        zone( c.axes[ 0 ], bits( 0x3E754800u ), bits( 0xBE754800u ), bits( 0x3FA85148u ) );
        zone( c.axes[ 1 ], bits( 0x3E754800u ), bits( 0xBE754800u ), bits( 0x3FA85148u ) );
        zone( c.axes[ 2 ], bits( 0x3E87C400u ), bits( 0xBE87C400u ), bits( 0x3FAE3076u ) );
        zone( c.axes[ 3 ], bits( 0x3E87C400u ), bits( 0xBE87C400u ), bits( 0x3FAE3076u ) );
        for ( int k = 0; k < 4; ++k )
            c.axes[ k ] = std::fabs( c.axes[ k ] ) * c.axes[ k ];
    }

    // 0x57EFE0, past which the rumble is the platform's. Every second of real time, the unconnected pads are scanned.
    void pad_tick( )
    {
        m_timer -= hooks.real_dt( hooks.user );
        if ( _mm_comige_ss( _mm_setzero_ps( ), _mm_set_ss( m_timer ) ) != 0 )
        {
            scan( true );
            m_timer = 1.0f;
        }
        for ( int p = 0; p < 4; ++p )
        {
            input_controller_t& c = m_controllers[ p ];
            if ( c.connected == 0 )
                continue;
            input_xinput_state_t state;
            const std::uint32_t result = hooks.get_state( hooks.user, p, &state );
            if ( result == 0 )
            {
                const float stick = bits( 0x38000000u );
                const float trigger = bits( 0x3B808081u );
                c.buttons = state.buttons;
                c.axes[ 0 ] = static_cast< float >( state.left_x ) * stick;
                c.axes[ 1 ] = static_cast< float >( state.left_y ) * stick;
                c.axes[ 2 ] = static_cast< float >( state.right_x ) * stick;
                c.axes[ 3 ] = static_cast< float >( state.right_y ) * stick;
                c.axes[ 4 ] = static_cast< float >( state.left_trigger ) * trigger;
                c.axes[ 5 ] = static_cast< float >( state.right_trigger ) * trigger;
                dead_zone( c );
            }
            else if ( result == 0x48Fu )
            {
                c.connected = 0;
                c.buttons = 0;
                std::memset( c.axes, 0, sizeof( c.axes ) );
                if ( hooks.pad_event )
                    hooks.pad_event( hooks.user, false, p );
            }
        }
    }

    // 0x539120 (+ the callback: 0x4AEBE0 here): the key's byte in +0xC and the latch +0x5D.
    void key_down( int key )
    {
        m_down[ key ] = 1;
        m_latch[ key ] = 1;
        if ( m_method != 1 )
        {
            m_method = 1;
            if ( hooks.method_event )
                hooks.method_event( hooks.user );
            if ( m_manager != nullptr )
                m_manager->set_method( 1 );
        }
    }

    // 0x539170 (the callback's slot 1 is 0x4021A0: nothing).
    void key_up( int key )
    {
        m_down[ key ] = 0;
    }

    // 0x5391C0: pressed this tick = latched and not latched last tick.
    void keyboard_tick( )
    {
        for ( int k = 0; k < 81; ++k )
        {
            m_pressed[ k ] = static_cast< std::uint8_t >( ~m_last[ k ] & m_latch[ k ] );
            m_last[ k ] = m_latch[ k ];
            m_latch[ k ] = 0;
        }
    }

    // 0x4AEC40: pad 0, any button or any axis past 0.5 (comiss / seta: a NaN is not) takes the control method to 0.
    void control_tick( )
    {
        const input_controller_t& c = m_controllers[ 0 ];
        std::uint32_t any = c.buttons;
        for ( int k = 0; k < 6; ++k )
            any |= _mm_comigt_ss( _mm_set_ss( std::fabs( c.axes[ k ] ) ), _mm_set_ss( 0.5f ) ) != 0 ? 1u : 0u;
        if ( any != 0 && m_method != 0 )
        {
            m_method = 0;
            if ( hooks.method_event )
                hooks.method_event( hooks.user );
            if ( m_manager != nullptr )
                m_manager->set_method( 0 );
        }
    }

    // One frame of the "Input" phase, in retail's registration order (0x4F7550 appends to the phase): the keyboard's slot 5
    // and the pad's run in 0x566230 in that order, the manager's after them, the control method's in 0x4024E0.
    void tick( float dt )
    {
        keyboard_tick( );
        pad_tick( );
        if ( m_manager != nullptr )
            m_manager->tick( dt );
        control_tick( );
    }

    // g_vucontrolmethodmanager +0x18 (0 gamepad, 1 keyboard; 2 / 3 are the touch and tilt answers of 0x4AEB30's +0x14, not this).
    int control_method( ) const
    {
        return m_method;
    }

    const input_controller_t& controller( int k ) const
    {
        return m_controllers[ k ];
    }

    float timer( ) const
    {
        return m_timer;
    }

    const std::uint8_t* down( ) const
    {
        return m_down;
    }

    const std::uint8_t* latch( ) const
    {
        return m_latch;
    }

    const std::uint8_t* last( ) const
    {
        return m_last;
    }

    const std::uint8_t* pressed( ) const
    {
        return m_pressed;
    }

private:
    static float bits( std::uint32_t word )
    {
        float out;
        std::memcpy( &out, &word, 4 );
        return out;
    }

    static void zone( float& v, float high, float low, float scale )
    {
        if ( low > v )
            v = ( v + high ) * scale;
        else if ( v > high )
            v = ( v - high ) * scale;
        else
            v = 0.0f;
    }

    // 0x57F330 ( event ): a pad that is not connected and answers both GetState and GetCapabilities connects; slot 8
    // 0x513590 turns the capabilities' SubType ( +1 ) into 0 for a gamepad, 1 for a wheel, else 2.
    void scan( bool event )
    {
        for ( int p = 0; p < 4; ++p )
        {
            input_controller_t& c = m_controllers[ p ];
            if ( c.connected != 0 )
                continue;
            input_xinput_state_t state;
            if ( hooks.get_state( hooks.user, p, &state ) != 0 )
                continue;
            std::uint8_t caps[ 20 ] = { };
            if ( hooks.get_caps( hooks.user, p, caps ) != 0 )
                continue;
            c.connected = 1;
            c.subtype = caps[ 1 ] == 1 ? 0 : ( caps[ 1 ] == 2 ? 1 : 2 );
            if ( event && hooks.pad_event )
                hooks.pad_event( hooks.user, true, p );
        }
    }

    c_input_manager* m_manager = nullptr;
    input_controller_t m_controllers[ 4 ];
    float m_timer = 1.0f;          // +0x20
    std::uint8_t m_down[ 81 ]{};     // keyboard +0xC
    std::uint8_t m_latch[ 81 ]{};    // +0x5D
    std::uint8_t m_last[ 81 ]{};     // +0xAE
    std::uint8_t m_pressed[ 81 ]{};  // +0xFF
    int m_method = 1;              // 0x4AE6E0
};
