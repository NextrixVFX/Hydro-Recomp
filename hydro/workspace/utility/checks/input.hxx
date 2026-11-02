#pragma once

#include <workspace/core/assets/archive.hxx>
#include <workspace/core/assets/reader.hxx>
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
