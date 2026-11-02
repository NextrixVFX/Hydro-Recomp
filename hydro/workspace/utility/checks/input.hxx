#pragma once

#include <workspace/core/assets/archive.hxx>
#include <workspace/core/assets/reader.hxx>
#include <workspace/core/input/device.hxx>
#include <workspace/core/input/manager.hxx>

#include <string>
#include <vector>

// c_input_manager against retail's run on the real InputDB (case input_manager, boat batch 30): the defs, then every
// step's four controller images (bindings, raw / smoothed / velocity / added words, button bytes) and the accessor
// samples. The pads, keys, dt and requests are retail's; the device name tables are the case's (a sorted list of the
// names InputDB binds), since the real tables belong to the platform's gamepad.
struct input_check_t
{
    std::vector< std::string > axes, buttons;
    input_pad_t pads[ 4 ];
    std::vector< float > pad_axes[ 4 ];
    unsigned char keys[ 84 ]{};
};

inline bool verify_input_manager( )
{
    const unsigned* w = g_golden_input_manager_words;
    const auto next = [ & ]( ) { return *w++; };
    const auto text = [ & ]( ) {
        const unsigned n = *w++;
        std::string out( reinterpret_cast< const char* >( w ), n );
        w += ( n + 3 ) / 4;
        return out;
    };
    input_check_t c;
    for ( unsigned k = next( ), i = 0; i < k; ++i )
        c.axes.push_back( text( ) );
    for ( unsigned k = next( ), i = 0; i < k; ++i )
        c.buttons.push_back( text( ) );

    c_archive archive;
    std::vector< std::uint8_t > bytes;
    const std::string pack = package_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) || !archive.read( "VuDBAsset/InputDB", bytes ) )
    {
        LOG_ERROR( "verify_input_manager: the pack's InputDB is not readable" );
        return false;
    }
    const value_t document = c_reader( bytes ).parse( );
    // The case's profile: the two default mappings crossed over.
    value_t profile;
    {
        const value_t& metro = member( member( member( document, "VuDBAsset" ), "DefaultMapping" ), "Metro" );
        value_t mapping, root;
        mapping.kind = root.kind = value_t::kind_t::object;
        mapping.tag = root.tag = 6;
        mapping.object.emplace_back( "Gamepad", member( metro, "Keyboard" ) );
        mapping.object.emplace_back( "Keyboard", member( metro, "GamePad" ) );
        root.object.emplace_back( "InputMapping", mapping );
        profile = root;
    }

    c_input_manager m;
    static input_check_t* s_c;
    s_c = &c;
    m.hooks.axis_index = []( void*, const char* n ) {
        for ( std::size_t k = 0; k < s_c->axes.size( ); ++k )
            if ( s_c->axes[ k ] == n )
                return static_cast< int >( k );
        return -1;
    };
    m.hooks.button_index = []( void*, const char* n ) {
        for ( std::size_t k = 0; k < s_c->buttons.size( ); ++k )
            if ( s_c->buttons[ k ] == n )
                return static_cast< int >( k );
        return -1;
    };
    m.hooks.pad = []( void*, int i ) { return s_c->pads[ i ]; };
    m.hooks.key = []( void*, int k ) { return static_cast< std::uint8_t >( s_c->keys[ k ] ); };
    m.init( document );

    int failed = 0;
    const auto bad = [ & ]( const char* what, unsigned step, unsigned ours, unsigned retail ) {
        if ( ++failed <= 10 )
            LOG_ERROR( "input_manager step %u %s: ours 0x%X, retail 0x%X", step, what, ours, retail );
    };
    const unsigned def_axes = next( );
    if ( def_axes != m.axes( ).size( ) )
        bad( "axis count", 0, static_cast< unsigned >( m.axes( ).size( ) ), def_axes );
    for ( unsigned i = 0; i < def_axes; ++i )
    {
        const std::string name = text( );
        const unsigned hash = next( ), group = next( ), smooth = next( );
        if ( i >= m.axes( ).size( ) )
            continue;
        const auto& d = m.axes( )[ i ];
        unsigned bits;
        std::memcpy( &bits, &d.smooth, 4 );
        if ( d.name != name || d.hash != hash || d.group != group || bits != smooth )
            bad( "axis def", i, d.hash, hash );
    }
    const unsigned def_buttons = next( );
    if ( def_buttons != m.buttons( ).size( ) )
        bad( "button count", 0, static_cast< unsigned >( m.buttons( ).size( ) ), def_buttons );
    for ( unsigned i = 0; i < def_buttons; ++i )
    {
        const std::string name = text( );
        const unsigned hash = next( ), group = next( );
        if ( i >= m.buttons( ).size( ) )
            continue;
        const auto& d = m.buttons( )[ i ];
        if ( d.name != name || d.hash != hash || d.group != group )
            bad( "button def", i, d.hash, hash );
    }
    const unsigned steps = next( );
    unsigned compared = 0;
    for ( unsigned s = 0; s < steps; ++s )
    {
        const unsigned op = next( ), slot = next( ), a = next( ), b = next( ), bits = next( ), type = next( ), index = next( );
        const std::string name = text( );
        float amount;
        std::memcpy( &amount, &bits, 4 );
        const input_binding_t bind{ static_cast< std::int32_t >( type ), static_cast< std::int32_t >( index ) };
        if ( op == 2 )
            m.set_method( static_cast< int >( a ) );
        else if ( op == 3 )
            m.request_button( static_cast< int >( a ), name.c_str( ) );
        else if ( op == 4 )
            m.add_axis( static_cast< int >( a ), name.c_str( ), amount );
        else if ( op == 5 && slot == 13 )
            m.set_positive( static_cast< int >( a ), static_cast< int >( b ), name.c_str( ), bind );
        else if ( op == 5 && slot == 14 )
            m.set_negative( static_cast< int >( a ), static_cast< int >( b ), name.c_str( ), bind );
        else if ( op == 5 )
            m.set_button( static_cast< int >( a ), static_cast< int >( b ), name.c_str( ), bind );
        else if ( op == 6 )
            m.load_default_mapping( static_cast< int >( a ), static_cast< int >( b ) );
        else if ( op == 7 )
            m.load_profile( profile );
        if ( next( ) != 0 )
        {
            for ( int p = 0; p < 4; ++p )
            {
                c.pads[ p ].buttons = next( );
                c.pad_axes[ p ].assign( c.axes.size( ) + 1, 0.0f );
                std::memcpy( c.pad_axes[ p ].data( ), w, 4 * c.axes.size( ) );
                w += c.axes.size( );
                c.pads[ p ].axes = c.pad_axes[ p ].data( );
            }
            std::memcpy( c.keys, w, 84 );
            w += 21;
            const unsigned dt = next( );
            float seconds;
            std::memcpy( &seconds, &dt, 4 );
            m.tick( seconds );
        }
        std::vector< unsigned > ours;
        const auto word = [ &ours ]( const void* p ) {
            unsigned v;
            std::memcpy( &v, p, 4 );
            ours.push_back( v );
        };
        for ( int k = 0; k < 4; ++k )
        {
            const auto& ctl = m.controller( k );
            for ( const input_axis_t& x : ctl.axes )
            {
                word( &x.pos[ 0 ].type ), word( &x.pos[ 0 ].index ), word( &x.pos[ 1 ].type ), word( &x.pos[ 1 ].index );
                word( &x.neg[ 0 ].type ), word( &x.neg[ 0 ].index ), word( &x.neg[ 1 ].type ), word( &x.neg[ 1 ].index );
                word( &x.raw ), word( &x.value ), word( &x.velocity ), word( &x.added );
            }
            for ( const input_button_t& x : ctl.buttons )
            {
                word( &x.bind[ 0 ].type ), word( &x.bind[ 0 ].index ), word( &x.bind[ 1 ].type ), word( &x.bind[ 1 ].index );
                ours.push_back( x.held );
                ours.push_back( x.pressed );
                ours.push_back( x.request );
            }
        }
        const unsigned count = next( );
        if ( count != ours.size( ) )
            bad( "image size", s, static_cast< unsigned >( ours.size( ) ), count );
        for ( unsigned k = 0; k < count; ++k )
        {
            const unsigned want = next( );
            ++compared;
            if ( k < ours.size( ) && ours[ k ] != want )
                bad( "image word", s * 10000 + k, ours[ k ], want );
        }
        for ( unsigned n = next( ), q = 0; q < n; ++q )
        {
            const unsigned kind = next( );
            ++compared;
            if ( kind == 0 )
            {
                const int ctl = static_cast< int >( next( ) );
                const std::string axis = text( );
                const int want = static_cast< int >( next( ) );
                int got = -1;
                const auto& defs = m.axes( );
                for ( std::size_t k = 0; k < defs.size( ) && got < 0; ++k )
                    if ( defs[ k ].hash == input_hash( axis.c_str( ) ) )
                        got = static_cast< int >( k );
                const float expect_value = got >= 0 ? m.controller( ctl ).axes[ got ].value : 0.0f;
                const float expect_raw = got >= 0 ? m.controller( ctl ).axes[ got ].raw : 0.0f;
                const float v = m.value( ctl, axis.c_str( ) ), r = m.raw( ctl, axis.c_str( ) );
                if ( got != want || std::memcmp( &v, &expect_value, 4 ) != 0 || std::memcmp( &r, &expect_raw, 4 ) != 0 )
                    bad( "axis lookup", s, static_cast< unsigned >( got ), static_cast< unsigned >( want ) );
            }
            else if ( kind == 1 )
            {
                const int ctl = static_cast< int >( next( ) );
                const std::string button = text( );
                const unsigned want_pressed = next( ), want_held = next( );
                if ( ( m.pressed( ctl, button.c_str( ) ) ? 1u : 0u ) != want_pressed || ( m.held( ctl, button.c_str( ) ) ? 1u : 0u ) != want_held )
                    bad( "button read", s, m.held( ctl, button.c_str( ) ) ? 1u : 0u, want_held );
            }
            else
            {
                const int ctl = static_cast< int >( next( ) ), method = static_cast< int >( next( ) );
                const unsigned which = next( );
                const std::string nm = text( );
                const unsigned want = next( ), want_type = next( ), want_index = next( );
                input_binding_t got{ 0x55555555, 0x66666666 };
                const bool ok = which == 10 ? m.get_positive( ctl, method, nm.c_str( ), got )
                                : which == 11 ? m.get_negative( ctl, method, nm.c_str( ), got )
                                              : m.get_button( ctl, method, nm.c_str( ), got );
                if ( ( ok ? 1u : 0u ) != want || static_cast< unsigned >( got.type ) != want_type || static_cast< unsigned >( got.index ) != want_index )
                    bad( "binding read", s, static_cast< unsigned >( got.type ), want_type );
            }
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuInputManagerImpl (0x5105F0 / 0x511220 / 0x5116E0 / 0x511C90 / 0x5121D0, slots 1 6-19) matches retail on the real InputDB: %u steps, %u values",
            steps, compared );
    else
        LOG_ERROR( "VuInputManagerImpl FAILED %d checks of %u", failed, compared );
    return failed == 0;
}

// The rumble half of 0x57EFE0, the controller's 0x57F470 and the pad's slot 1 0x57EFB0 against retail (case pad_rumble): per step the op, its
// inputs, the XInputSetState calls in order and all eight motor words after.
inline bool verify_pad_rumble( )
{
    const unsigned* w = g_golden_pad_rumble_words;
    const auto next = [ & ]( ) { return *w++; };
    const auto real = [ ]( unsigned u ) {
        float f;
        std::memcpy( &f, &u, 4 );
        return f;
    };
    const auto word = [ ]( float f ) {
        unsigned u;
        std::memcpy( &u, &f, 4 );
        return u;
    };
    struct call_t
    {
        unsigned pad, left, right;
    };
    static std::vector< call_t > s_calls;
    const unsigned n = next( );
    int bad = 0;
    c_input_device device;
    device.hooks.real_dt = []( void* ) { return 0.0f; };
    device.hooks.get_state = []( void*, int, input_xinput_state_t* ) -> std::uint32_t { return 1u; };
    device.hooks.get_caps = []( void*, int, std::uint8_t* ) -> std::uint32_t { return 1u; };
    device.hooks.set_state = []( void*, int pad, std::uint16_t left, std::uint16_t right ) -> std::uint32_t {
        s_calls.push_back( { static_cast< unsigned >( pad ), left, right } );
        return 0u;
    };
    for ( unsigned k = 0; k < n; ++k )
    {
        s_calls.clear( );
        const unsigned op = next( );
        for ( int p = 0; p < 4; ++p )
            device.connect( p, next( ) != 0u );
        for ( int p = 0; p < 4; ++p )
        {
            const float low = real( next( ) );
            const float high = real( next( ) );
            device.set_rumble( p, low, high );
        }
        const unsigned a = next( ), b = next( ), c = next( );
        if ( op == 0 )
        {
            device.rumble_config( 3, static_cast< std::uint8_t >( a ) );
            device.pad_tick( real( b ) );
        }
        else if ( op == 1 )
        {
            device.rumble_config( 3, 1 );
            device.rumble( static_cast< int >( a ), real( b ), real( c ) );
        }
        else
        {
            device.rumble_config( 3, static_cast< std::uint8_t >( 1u - c ) );
            device.rumble_config( static_cast< std::int32_t >( b ), static_cast< std::uint8_t >( c ) );
        }
        const unsigned active = next( ), calls = next( );
        if ( active != device.rumble_active( ) || calls != s_calls.size( ) )
            ++bad;
        for ( unsigned j = 0; j < calls; ++j )
        {
            const unsigned pad = next( ), left = next( ), right = next( );
            if ( j < s_calls.size( ) && ( s_calls[ j ].pad != pad || s_calls[ j ].left != left || s_calls[ j ].right != right ) )
                ++bad;
        }
        for ( int p = 0; p < 4; ++p )
            for ( int m = 0; m < 2; ++m )
                if ( word( device.controller( p ).rumble[ m ] ) != next( ) )
                    ++bad;
    }
    if ( bad )
        LOG_ERROR( "pad_rumble FAILED %d of %u", bad, n );
    else
        LOG_SUCCESS( "verify_pad_rumble: %u steps of 0x57EFE0's rumble half, 0x57F470 and 0x57EFB0 match retail", n );
    return bad == 0;
}

// c_input_device + c_input_manager against retail's own gamepad, keyboard, control method and manager on the real InputDB
// (case input_device, boat batch 31): the name tables, the key table, the lookups, the dead zone, then frames of fake XInput
// and key events through the four "Input" ticks, down to the channels VuHumanDriver reads.
struct input_device_check_t
{
    bool connected[ 4 ] = { };
    input_xinput_state_t state[ 4 ];
    unsigned sub[ 4 ] = { };
    float real_dt = 0.0f;
    std::vector< unsigned > events;
};

inline bool verify_input_device( )
{
    const unsigned* w = g_golden_input_device_words;
    const auto next = [ & ]( ) { return *w++; };
    const auto text = [ & ]( ) {
        const unsigned n = *w++;
        std::string out( reinterpret_cast< const char* >( w ), n );
        w += ( n + 3 ) / 4;
        return out;
    };
    int failed = 0;
    unsigned compared = 0;
    const auto same = [ & ]( const char* what, unsigned at, unsigned ours, unsigned retail ) {
        ++compared;
        if ( ours != retail && ++failed <= 10 )
            LOG_ERROR( "input_device %s %u: ours 0x%X, retail 0x%X", what, at, ours, retail );
    };
    const auto word = []( float f ) {
        unsigned v;
        std::memcpy( &v, &f, 4 );
        return v;
    };

    const unsigned axes = next( );
    same( "axis count", 0, 6, axes );
    for ( unsigned k = 0; k < axes; ++k )
    {
        const std::string name = text( );
        const unsigned hash = next( ), low = next( ), high = next( );
        same( "axis name", k, name == g_input_axis_names[ k ].name, 1 );
        same( "axis hash", k, input_hash( g_input_axis_names[ k ].name ), hash );
        same( "axis minimum", k, word( g_input_axis_names[ k ].minimum ), low );
        same( "axis maximum", k, word( 1.0f ), high );
    }
    const unsigned buttons = next( );
    same( "button count", 0, 16, buttons );
    for ( unsigned k = 0; k < buttons; ++k )
    {
        const std::string name = text( );
        const unsigned hash = next( );
        same( "button name", k, name == g_input_button_names[ k ], 1 );
        same( "button hash", k, input_hash( g_input_button_names[ k ] ), hash );
    }
    for ( unsigned k = 0; k < 256; ++k )
        same( "key table", k, g_input_key_table[ k ], next( ) );
    for ( unsigned n = next( ), k = 0; k < n; ++k )
    {
        const std::string name = text( );
        same( "axis index", k, static_cast< unsigned >( c_input_device::axis_index( name.c_str( ) ) ), next( ) );
        same( "button index", k, static_cast< unsigned >( c_input_device::button_index( name.c_str( ) ) ), next( ) );
    }
    for ( unsigned n = next( ), k = 0; k < n; ++k )
    {
        input_controller_t c;
        c.subtype = static_cast< std::int32_t >( next( ) );
        std::memcpy( c.axes, w, 24 );
        w += 6;
        c_input_device::dead_zone( c );
        for ( int a = 0; a < 6; ++a )
            same( "dead zone", k * 10 + a, word( c.axes[ a ] ), next( ) );
    }

    c_archive archive;
    std::vector< std::uint8_t > bytes;
    const std::string pack = package_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) || !archive.read( "VuDBAsset/InputDB", bytes ) )
    {
        LOG_ERROR( "verify_input_device: the pack's InputDB is not readable" );
        return false;
    }
    const value_t document = c_reader( bytes ).parse( );
    c_input_manager manager;
    c_input_device device;
    static input_device_check_t s_c;
    s_c = input_device_check_t{ };
    device.hooks.get_state = []( void*, int p, input_xinput_state_t* out ) -> std::uint32_t {
        if ( !s_c.connected[ p ] )
            return 0x48Fu;
        *out = s_c.state[ p ];
        return 0u;
    };
    device.hooks.get_caps = []( void*, int p, std::uint8_t* caps ) -> std::uint32_t {
        caps[ 1 ] = static_cast< std::uint8_t >( s_c.sub[ p ] );
        return 0u;
    };
    device.hooks.real_dt = []( void* ) { return s_c.real_dt; };
    device.hooks.pad_event = []( void*, bool on, int p ) {
        s_c.events.push_back( input_hash( on ? "OnGamePadConnected" : "OnGamePadDisconnected" ) );
        s_c.events.push_back( static_cast< unsigned >( p ) );
    };
    device.hooks.method_event = []( void* ) {
        s_c.events.push_back( 0xC0u );
        s_c.events.push_back( 0u );
    };
    device.bind( manager );
    s_c.connected[ 0 ] = true;
    s_c.sub[ 0 ] = 1;
    manager.init( document );
    device.initialize( );

    std::vector< std::string > ax, bt;
    for ( unsigned n = next( ), k = 0; k < n; ++k )
        ax.push_back( text( ) );
    for ( unsigned n = next( ), k = 0; k < n; ++k )
        bt.push_back( text( ) );

    const unsigned frames = next( );
    for ( unsigned f = 0; f < frames; ++f )
    {
        for ( int p = 0; p < 4; ++p )
        {
            s_c.connected[ p ] = next( ) != 0;
            input_xinput_state_t& s = s_c.state[ p ];
            s.buttons = static_cast< std::uint16_t >( next( ) );
            s.left_trigger = static_cast< std::uint8_t >( next( ) );
            s.right_trigger = static_cast< std::uint8_t >( next( ) );
            s.left_x = static_cast< std::int16_t >( next( ) );
            s.left_y = static_cast< std::int16_t >( next( ) );
            s.right_x = static_cast< std::int16_t >( next( ) );
            s.right_y = static_cast< std::int16_t >( next( ) );
            s_c.sub[ p ] = next( );
        }
        s_c.events.clear( );
        for ( unsigned n = next( ), k = 0; k < n; ++k )
        {
            const unsigned down = next( ), key = next( );
            if ( down != 0 )
                device.key_down( static_cast< int >( key ) );
            else
                device.key_up( static_cast< int >( key ) );
        }
        const unsigned rdt = next( ), dt = next( );
        std::memcpy( &s_c.real_dt, &rdt, 4 );
        float seconds;
        std::memcpy( &seconds, &dt, 4 );
        device.tick( seconds );
        for ( int p = 0; p < 4; ++p )
        {
            const input_controller_t& c = device.controller( p );
            same( "connected", f, c.connected, next( ) );
            same( "subtype", f, static_cast< unsigned >( c.subtype ), next( ) );
            same( "buttons", f, c.buttons, next( ) );
            for ( int a = 0; a < 6; ++a )
                same( "axis", f * 10 + a, word( c.axes[ a ] ), next( ) );
        }
        same( "timer", f, word( device.timer( ) ), next( ) );
        const unsigned char* keys = reinterpret_cast< const unsigned char* >( w );
        for ( int k = 0; k < 81; ++k )
        {
            same( "key down", f, device.down( )[ k ], keys[ k ] );
            same( "key latch", f, device.latch( )[ k ], keys[ 81 + k ] );
            same( "key last", f, device.last( )[ k ], keys[ 162 + k ] );
            same( "key pressed", f, device.pressed( )[ k ], keys[ 243 + k ] );
        }
        w += 81;
        same( "control method", f, static_cast< unsigned >( device.control_method( ) ), next( ) );
        same( "manager method", f, static_cast< unsigned >( manager.method( ) ), next( ) );
        const unsigned count = next( );
        same( "channels", f, static_cast< unsigned >( ( ax.size( ) * 2 + bt.size( ) * 2 ) * 2 ), count );
        for ( int c = 0; c < 2; ++c )
        {
            for ( const std::string& n : ax )
            {
                same( "value", f, word( manager.value( c, n.c_str( ) ) ), next( ) );
                same( "raw", f, word( manager.raw( c, n.c_str( ) ) ), next( ) );
            }
            for ( const std::string& n : bt )
            {
                same( "held", f, manager.held( c, n.c_str( ) ) ? 1u : 0u, next( ) );
                same( "pressed", f, manager.pressed( c, n.c_str( ) ) ? 1u : 0u, next( ) );
            }
        }
        const unsigned events = next( );
        same( "event count", f, static_cast< unsigned >( s_c.events.size( ) / 2 ), events );
        for ( unsigned k = 0; k < events; ++k )
        {
            const unsigned kind = next( ), pad = next( );
            if ( k * 2 + 1 >= s_c.events.size( ) )
                continue;
            same( "event", f, s_c.events[ k * 2 ], kind );
            same( "event pad", f, s_c.events[ k * 2 + 1 ], pad );
        }
    }
    if ( failed != 0 )
    {
        LOG_ERROR( "verify_input_device: %d of %u mismatches", failed, compared );
        return false;
    }
    LOG_SUCCESS( "c_input_device matches retail's pad, keyboard and control method over %u frames, %u values", frames, compared );
    return true;
}
