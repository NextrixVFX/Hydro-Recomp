#pragma once

#include <workspace/core/assets/tree.hxx>

#include <xmmintrin.h>

#include <algorithm>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// VuInputManagerImpl (boat batch 30): g_vuinputmanagerimpl 0x759038, vtable 0x74C7B4, ctor 0x510450. The named channels
// a driver reads: InputDB's axes and buttons, four controllers, two input methods per binding (0 gamepad, 1 keyboard;
// +0x2C picks the method for all four). Everything here runs against retail's own run in oracle case input_manager.
//
// The device is the caller's (input_hooks_t): g_vugamepad 0x759040's name tables (+4 axes, stride 0x10, +8 count, name
// hash +4; +0x10 buttons, stride 8, +0x14 count, name hash +4: 0x57E770 / 0x57E7D0 answer the index, -1 when absent), its
// pad state (vt+0x18( i ): +0xC the button bits, +0x10 the axis floats) and the keyboard's bytes (g_vukeyboard 0x75906C
// +0xC + key). Not recreated, the profile side of the platform layer: the "OnSaveProfile" writer 0x5111B0 / 0x5118D0 /
// 0x511E50 (saves are re-made for the platform, not 1:1), the tick registrations and the dtor (the caller's lifetime).
// SUBSTITUTION: std::vector for the axis / button / controller arrays (retail's resize 0x512340 / 0x512470 zero-fill).

// FNV-1a of a C string, the hash every lookup here uses.
inline std::uint32_t input_hash( const char* text )
{
    std::uint32_t h = 0x811C9DC5u;
    for ( const unsigned char* p = reinterpret_cast< const unsigned char* >( text ); *p != 0; ++p )
        h = ( h ^ *p ) * 16777619u;
    return h;
}

// 0x5390C0: the key name table 0x7358B8; a name that is not there is 0 ("NONE").
inline int input_key_index( const char* name )
{
    static constexpr const char* s_keys[ 81 ] = {
        "NONE", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
        "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "BACK", "ENTER", "SPACE", "TAB", "COMMA", "PERIOD", "MINUS", "PLUS",
        "APOSTROPHE", "SEMICOLON", "SLASH", "GRAVE", "BACKSLASH", "LEFT BRACKET", "RIGHT BRACKET", "LEFT", "RIGHT", "UP", "DOWN", "CENTER",
        "SHIFT", "ALT", "CONTROL", "ESCAPE", "SCROLL", "PAUSE", "INSERT", "DELETE", "HOME", "END", "PRIOR", "NEXT", "F1", "F2", "F3", "F4",
        "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
    for ( int k = 0; k < 81; ++k )
    {
        if ( std::strcmp( s_keys[ k ], name ) == 0 )
            return k;
    }
    return 0;
}

// The pad state of vt+0x18( i ).
struct input_pad_t
{
    std::uint32_t buttons = 0;     // +0xC
    const float* axes = nullptr;   // +0x10
};

struct input_hooks_t
{
    void* user = nullptr;
    int ( *axis_index )( void* user, const char* name ) = nullptr;      // 0x57E770
    int ( *button_index )( void* user, const char* name ) = nullptr;    // 0x57E7D0
    input_pad_t ( *pad )( void* user, int controller ) = nullptr;       // [0x759040] vt+0x18
    std::uint8_t ( *key )( void* user, int key ) = nullptr;             // [0x75906C] +0xC + key
};

// A binding (8 bytes): +0 the type (1 AxisPos, 2 AxisNeg, 3 Button, 4 Key; 0 unbound), +4 the index.
struct input_binding_t
{
    std::int32_t type = 0;
    std::int32_t index = 0;
};

// 48 bytes per axis: the "+" bindings by method, the "-" bindings by method, +0x20 the raw value, +0x24 the smoothed
// value, +0x28 the smoothing velocity, +0x2C what slot 18 added since the last tick.
struct input_axis_t
{
    input_binding_t pos[ 2 ];
    input_binding_t neg[ 2 ];
    float raw = 0.0f;
    float value = 0.0f;
    float velocity = 0.0f;
    float added = 0.0f;
};

// 20 bytes per button: the bindings by method, +0x10 held, +0x11 pressed this tick, +0x12 slot 17's request.
struct input_button_t
{
    input_binding_t bind[ 2 ];
    std::uint8_t held = 0;
    std::uint8_t pressed = 0;
    std::uint8_t request = 0;
};

class c_input_manager
{
public:
    // +0x10 records, 0x24 bytes: the name, +0x18 its hash, +0x1C the group's hash, +0x20 SmoothTime.
    struct axis_def_t
    {
        std::string name;
        std::uint32_t hash = 0;
        std::uint32_t group = 0;
        float smooth = 0.0f;
    };

    // +0x1C records, 0x20 bytes.
    struct button_def_t
    {
        std::string name;
        std::uint32_t hash = 0;
        std::uint32_t group = 0;
    };

    struct controller_t
    {
        std::vector< input_axis_t > axes;
        std::vector< input_button_t > buttons;
    };

    input_hooks_t hooks{};

    // Slot 5 0x5105F0 past its two registrations, on InputDB's document (+0xC's asset +0x58): the "VuDBAsset" member's
    // Axes and Buttons lists; four controllers sized from them (0x510310: both vectors zero-filled); each one's default
    // mapping, method 0 then 1 (vt+0x40). A name or group that is not text hashes as its empty fallback; SmoothTime is
    // an int (cvtdq2ps), a float, an int64 (fild + fstp: one rounding) or 0.
    void init( const value_t& document )
    {
        m_document = &document;
        const value_t& db = member( document, "VuDBAsset" );
        const value_t& axes = member( db, "Axes" );
        const int axis_count = list_size( axes );
        for ( int i = 0; i < axis_count; ++i )
        {
            const value_t& a = item( axes, i );
            axis_def_t def;
            def.name = text_of( member( a, "Name" ) );
            def.hash = input_hash( def.name.c_str( ) );
            def.group = input_hash( text_of( member( a, "Group" ) ).c_str( ) );
            def.smooth = float_of( member( a, "SmoothTime" ) );
            m_axes.push_back( def );
        }
        const value_t& buttons = member( db, "Buttons" );
        const int button_count = list_size( buttons );
        for ( int i = 0; i < button_count; ++i )
        {
            const value_t& b = item( buttons, i );
            button_def_t def;
            def.name = text_of( member( b, "Name" ) );
            def.hash = input_hash( def.name.c_str( ) );
            def.group = input_hash( text_of( member( b, "Group" ) ).c_str( ) );
            m_buttons.push_back( def );
        }
        m_controllers.resize( 4 );
        for ( int c = 0; c < 4; ++c )
        {
            m_controllers[ c ].axes.resize( m_axes.size( ) );
            m_controllers[ c ].buttons.resize( m_buttons.size( ) );
            load_default_mapping( c, 0 );
            load_default_mapping( c, 1 );
        }
    }

    // Slot 1 0x510E10, the profile's mappings into controller 0: profile[ "InputMapping" ][ "Gamepad" ] for method 0,
    // [ "InputMapping" ][ "Keyboard" ] for method 1 ( [0x759084] + 8 is the caller's `profile` ; null skips this ).
    void load_profile( const value_t& profile )
    {
        load_mapping( 0, member( member( profile, "InputMapping" ), "Gamepad" ), 0 );
        load_mapping( 0, member( member( profile, "InputMapping" ), "Keyboard" ), 1 );
    }

    // Slot 16 0x5110F0 ( controller, method ): VuDBAsset.DefaultMapping.Metro.GamePad / .Keyboard; another method does
    // nothing.
    void load_default_mapping( int controller, int method )
    {
        const value_t& metro = member( member( member( *m_document, "VuDBAsset" ), "DefaultMapping" ), "Metro" );
        if ( method == 0 )
            load_mapping( controller, member( metro, "GamePad" ), 0 );
        else if ( method == 1 )
            load_mapping( controller, member( metro, "Keyboard" ), 1 );
    }

    // Slot 19 0x510300: +0x2C.
    void set_method( int method )
    {
        m_method = method;
    }

    int method( ) const
    {
        return m_method;
    }

    // Slot 6 0x510E80: the smoothed value; 0 for an unknown name.
    float value( int controller, const char* name ) const
    {
        const input_axis_t* a = find_axis( controller, name );
        return a != nullptr ? a->value : 0.0f;
    }

    // Slot 7 0x510EA0: the raw value.
    float raw( int controller, const char* name ) const
    {
        const input_axis_t* a = find_axis( controller, name );
        return a != nullptr ? a->raw : 0.0f;
    }

    // Slot 8 0x510EC0 / slot 9 0x510EE0.
    bool held( int controller, const char* name ) const
    {
        const input_button_t* b = find_button( controller, name );
        return b != nullptr && b->held != 0;
    }

    bool pressed( int controller, const char* name ) const
    {
        const input_button_t* b = find_button( controller, name );
        return b != nullptr && b->pressed != 0;
    }

    // Slots 10 0x510F00 / 11 0x510F40 / 12 0x510F80 ( controller, method, name, out type, out index ): false for an
    // unknown name, the outputs untouched.
    bool get_positive( int controller, int method, const char* name, input_binding_t& out ) const
    {
        const input_axis_t* a = find_axis( controller, name );
        if ( a == nullptr )
            return false;
        out = a->pos[ method ];
        return true;
    }

    bool get_negative( int controller, int method, const char* name, input_binding_t& out ) const
    {
        const input_axis_t* a = find_axis( controller, name );
        if ( a == nullptr )
            return false;
        out = a->neg[ method ];
        return true;
    }

    bool get_button( int controller, int method, const char* name, input_binding_t& out ) const
    {
        const input_button_t* b = find_button( controller, name );
        if ( b == nullptr )
            return false;
        out = b->bind[ method ];
        return true;
    }

    // Slots 13 0x510FC0 / 14 0x511020 / 15 0x511080 ( controller, method, name, type, index ): an unknown name does
    // nothing (0x511610 / 0x511680 answer -1); a group other than 0 first clears the same binding everywhere in the
    // group (0x5121D0); then the binding is written.
    void set_positive( int controller, int method, const char* name, input_binding_t bind )
    {
        const int i = axis_at( name );
        if ( i < 0 )
            return;
        if ( m_axes[ i ].group != 0 )
            clear_group( controller, method, m_axes[ i ].group, bind );
        m_controllers[ controller ].axes[ i ].pos[ method ] = bind;
    }

    void set_negative( int controller, int method, const char* name, input_binding_t bind )
    {
        const int i = axis_at( name );
        if ( i < 0 )
            return;
        if ( m_axes[ i ].group != 0 )
            clear_group( controller, method, m_axes[ i ].group, bind );
        m_controllers[ controller ].axes[ i ].neg[ method ] = bind;
    }

    void set_button( int controller, int method, const char* name, input_binding_t bind )
    {
        const int i = button_at( name );
        if ( i < 0 )
            return;
        if ( m_buttons[ i ].group != 0 )
            clear_group( controller, method, m_buttons[ i ].group, bind );
        m_controllers[ controller ].buttons[ i ].bind[ method ] = bind;
    }

    // Slot 17 0x511160: +0x12 set (held next tick).
    void request_button( int controller, const char* name )
    {
        if ( input_button_t* b = find_button( controller, name ) )
            b->request = 1;
    }

    // Slot 18 0x511180: +0x2C += amount.
    void add_axis( int controller, const char* name, float amount )
    {
        if ( input_axis_t* a = find_axis( controller, name ) )
            a->added = a->added + amount;
    }

    // The tick 0x511220 ( dt ), four controllers. An axis: positive minus negative binding (an unbound one reads 0) plus
    // +0x2C (cleared), clamped by minss / maxss with 1 / -1 as the FIRST operand - a NaN falls through - into +0x20.
    // SmoothTime > 0: one step of the rational critically damped spring (0.48, 0.235) on +0x24 / +0x28; else +0x24 is
    // +0x20. A button: held = request | binding, pressed = held and not held before, request cleared.
    void tick( float dt )
    {
        for ( int c = 0; c < 4; ++c )
        {
            controller_t& controller = m_controllers[ c ];
            const input_pad_t pad = hooks.pad( hooks.user, c );
            for ( std::size_t i = 0; i < m_axes.size( ); ++i )
            {
                input_axis_t& a = controller.axes[ i ];
                const float pos = read_axis( pad, a.pos[ m_method ] );
                const float neg = read_axis( pad, a.neg[ m_method ] );
                const float sum = ( pos - neg ) + a.added;
                a.added = 0.0f;
                const float upper = _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( 1.0f ), _mm_set_ss( sum ) ) );
                const float target = _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( -1.0f ), _mm_set_ss( upper ) ) );
                a.raw = target;
                const float smooth = m_axes[ i ].smooth;
                if ( smooth > 0.0f )
                {
                    const float omega = 2.0f / smooth;
                    const float x = omega * dt;
                    const float inverse = 1.0f / ( ( x * 0.48f * x + ( x + 1.0f ) ) + x * 0.235f * x * x );
                    const float change = a.value - target;
                    const float temp = ( change * omega + a.velocity ) * dt;
                    a.velocity = ( a.velocity - temp * omega ) * inverse;
                    a.value = ( temp + change ) * inverse + target;
                }
                else
                {
                    a.value = target;
                }
            }
            for ( std::size_t i = 0; i < m_buttons.size( ); ++i )
            {
                input_button_t& b = controller.buttons[ i ];
                const std::uint8_t before = b.held;
                const std::uint8_t now = read_button( pad, b.bind[ m_method ] ) ? 1 : 0;
                b.held = b.request | now;
                b.pressed = ( b.held != 0 && before == 0 ) ? 1 : 0;
                b.request = 0;
            }
        }
    }

    const std::vector< axis_def_t >& axes( ) const
    {
        return m_axes;
    }

    const std::vector< button_def_t >& buttons( ) const
    {
        return m_buttons;
    }

    const controller_t& controller( int index ) const
    {
        return m_controllers[ index ];
    }

private:
    static int list_size( const value_t& v )
    {
        return v.tag == 5 ? static_cast< int >( v.list.size( ) ) : 0;
    }

    static const value_t& item( const value_t& v, int i )
    {
        static const value_t null{};
        return v.tag == 5 && i >= 0 && i < static_cast< int >( v.list.size( ) ) ? v.list[ i ] : null;
    }

    static std::string text_of( const value_t& v )
    {
        return v.tag == 4 ? v.text : std::string( );
    }

    static float float_of( const value_t& v )
    {
        if ( v.tag == 1 )
            return static_cast< float >( v.integer );
        if ( v.tag == 2 )
            return v.real;
        if ( v.tag == 7 )
            return static_cast< float >( v.int64 );
        return 0.0f;
    }

    // 0x511610: the axis index, -1 when absent.
    int axis_at( const char* name ) const
    {
        const std::uint32_t h = input_hash( name );
        for ( std::size_t k = 0; k < m_axes.size( ); ++k )
        {
            if ( m_axes[ k ].hash == h )
                return static_cast< int >( k );
        }
        return -1;
    }

    // 0x511680.
    int button_at( const char* name ) const
    {
        const std::uint32_t h = input_hash( name );
        for ( std::size_t k = 0; k < m_buttons.size( ); ++k )
        {
            if ( m_buttons[ k ].hash == h )
                return static_cast< int >( k );
        }
        return -1;
    }

    // 0x511510.
    input_axis_t* find_axis( int controller, const char* name )
    {
        const int i = axis_at( name );
        return i >= 0 ? &m_controllers[ controller ].axes[ i ] : nullptr;
    }

    const input_axis_t* find_axis( int controller, const char* name ) const
    {
        const int i = axis_at( name );
        return i >= 0 ? &m_controllers[ controller ].axes[ i ] : nullptr;
    }

    // 0x5115A0.
    input_button_t* find_button( int controller, const char* name )
    {
        const int i = button_at( name );
        return i >= 0 ? &m_controllers[ controller ].buttons[ i ] : nullptr;
    }

    const input_button_t* find_button( int controller, const char* name ) const
    {
        const int i = button_at( name );
        return i >= 0 ? &m_controllers[ controller ].buttons[ i ] : nullptr;
    }

    // 0x511C90 ( value, out ): "Type" and "Index", both text or the empty string. AxisPos 1 / AxisNeg 2 through the axis
    // table, Button 3 through the button table, Key 4 through the key names. Any other type leaves the binding alone.
    void parse_binding( const value_t& value, input_binding_t& out ) const
    {
        const std::string type = text_of( member( value, "Type" ) );
        const std::string index = text_of( member( value, "Index" ) );
        if ( type == "AxisPos" )
        {
            out.type = 1;
            out.index = hooks.axis_index( hooks.user, index.c_str( ) );
        }
        else if ( type == "AxisNeg" )
        {
            out.type = 2;
            out.index = hooks.axis_index( hooks.user, index.c_str( ) );
        }
        else if ( type == "Button" )
        {
            out.type = 3;
            out.index = hooks.button_index( hooks.user, index.c_str( ) );
        }
        else if ( type == "Key" )
        {
            out.type = 4;
            out.index = input_key_index( index.c_str( ) );
        }
    }

    // 0x5116E0 ( controller, value, method ): the "Axes" object's members in key order: a known axis takes its "+" and
    // "-" objects; the "Buttons" members, a known button takes the member itself.
    void load_mapping( int controller, const value_t& value, int method )
    {
        const value_t& axes = member( value, "Axes" );
        for ( const auto* m : sorted_members( axes ) )
        {
            input_axis_t* a = find_axis( controller, m->first.c_str( ) );
            if ( a == nullptr )
                continue;
            parse_binding( member( m->second, "+" ), a->pos[ method ] );
            parse_binding( member( m->second, "-" ), a->neg[ method ] );
        }
        const value_t& buttons = member( value, "Buttons" );
        for ( const auto* m : sorted_members( buttons ) )
        {
            input_button_t* b = find_button( controller, m->first.c_str( ) );
            if ( b != nullptr )
                parse_binding( m->second, b->bind[ method ] );
        }
    }

    static std::vector< const std::pair< std::string, value_t >* > sorted_members( const value_t& v )
    {
        std::vector< const std::pair< std::string, value_t >* > out;
        if ( v.tag != 6 )
            return out;
        for ( const auto& m : v.object )
            out.push_back( &m );
        std::sort( out.begin( ), out.end( ), []( const auto* a, const auto* b ) { return a->first < b->first; } );
        return out;
    }

    // 0x5121D0 ( controller, method, group, type, index ): every axis of the group loses this binding (positive and
    // negative), every button of the group too.
    void clear_group( int controller, int method, std::uint32_t group, input_binding_t bind )
    {
        controller_t& c = m_controllers[ controller ];
        const auto same = [ & ]( const input_binding_t& b ) { return b.type == bind.type && b.index == bind.index; };
        for ( std::size_t k = 0; k < m_axes.size( ); ++k )
        {
            if ( m_axes[ k ].group != group )
                continue;
            if ( same( c.axes[ k ].pos[ method ] ) )
                c.axes[ k ].pos[ method ] = input_binding_t{ };
            if ( same( c.axes[ k ].neg[ method ] ) )
                c.axes[ k ].neg[ method ] = input_binding_t{ };
        }
        for ( std::size_t k = 0; k < m_buttons.size( ); ++k )
        {
            if ( m_buttons[ k ].group == group && same( c.buttons[ k ].bind[ method ] ) )
                c.buttons[ k ].bind[ method ] = input_binding_t{ };
        }
    }

    // 0x512080: 1 max( axis, 0 ) (maxss, a NaN axis gives 0), 2 -min( axis, 0 ) (the sign flipped by xorps, so -0 for
    // an axis >= 0), 3 the button's bit, 4 the key (retail's "+0x150 above -1" test can never hold). Unbound 0.
    float read_axis( const input_pad_t& pad, const input_binding_t& b ) const
    {
        switch ( b.type )
        {
        case 1:
            return _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( pad.axes[ b.index ] ), _mm_setzero_ps( ) ) );
        case 2:
        {
            const float m = _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( pad.axes[ b.index ] ), _mm_setzero_ps( ) ) );
            std::uint32_t bits;
            std::memcpy( &bits, &m, 4 );
            bits ^= 0x80000000u;
            float out;
            std::memcpy( &out, &bits, 4 );
            return out;
        }
        case 3:
            return ( pad.buttons & ( 1u << ( b.index & 31 ) ) ) != 0 ? 1.0f : 0.0f;
        case 4:
            return hooks.key( hooks.user, b.index ) != 0 ? 1.0f : 0.0f;
        default:
            return 0.0f;
        }
    }

    // 0x512130: 1 axis > 0.5, 2 -0.5 > axis, 3 the bit, 4 the key.
    bool read_button( const input_pad_t& pad, const input_binding_t& b ) const
    {
        switch ( b.type )
        {
        case 1:
            return pad.axes[ b.index ] > 0.5f;
        case 2:
            return -0.5f > pad.axes[ b.index ];
        case 3:
            return ( pad.buttons & ( 1u << ( b.index & 31 ) ) ) != 0;
        case 4:
            return hooks.key( hooks.user, b.index ) != 0;
        default:
            return false;
        }
    }

    const value_t* m_document = nullptr;
    std::vector< axis_def_t > m_axes;
    std::vector< button_def_t > m_buttons;
    std::vector< controller_t > m_controllers;
    int m_method = 0;
};
