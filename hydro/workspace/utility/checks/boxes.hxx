#pragma once

#include <workspace/core/game/boxes.hxx>
#include <workspace/core/game/touches.hxx>

// VuMessageBoxManager, the screen's runtime and VuMessageBoxEntity against retail (case message_box, cases/msgbox.py): after every step of 24
// scripts of 70 the machine, fade, time, queue, the screen's words, the five texts and the service log in order.
inline bool verify_message_box( )
{
    save_words_t c{ g_golden_message_box_words };
    const unsigned runs = c.next( );
    int bad = 0, steps = 0;
    const auto real = [ ]( unsigned u ) {
        float f;
        std::memcpy( &f, &u, 4 );
        return f;
    };
    for ( unsigned v = 0; v < runs; ++v )
    {
        ui_message_box_t box;
        msgbox_screen_t screen;
        screen.m_box = &box;
        box.m_screen_scale[ 0 ] = real( c.next( ) );
        box.m_screen_scale[ 1 ] = real( c.next( ) );
        for ( int k = 0; k < 8; ++k )
            box.m_touch[ k / 4 ][ k % 4 ] = real( c.next( ) );
        screen.m_spin = real( c.next( ) );
        const unsigned count = c.next( );
        std::string log;
        unsigned keys = 0, focus = 0;
        const auto say = [ & ]( const std::string& s ) {
            if ( !log.empty( ) )
                log += '|';
            log += s;
        };
        msgbox_hooks_t h;
        h.text = [ ]( const std::string& n ) { return n == "nokey" ? std::string( "[none]" ) : "T_" + n; };
        h.missing = [ ] { return std::string( "[none]" ); };
        h.ui_tick = [ & ]( float ) { say( "OnUITick" ); };
        h.ui_event = [ & ]( const char* n ) {
            if ( !std::strcmp( n, "MessageBoxClose" ) )
                say( "off" );    // the case logs 0x500B80 before it enters slot 7
            say( std::string( "ui:" ) + n );
        };
        h.pressed = [ & ]( const char* n ) {
            say( std::string( "key:" ) + n );
            static const char* const names[ 5 ] = { "Select", "Left", "Left2", "Right", "Right2" };
            for ( int k = 0; k < 5; ++k )
                if ( !std::strcmp( n, names[ k ] ) )
                    return ( keys >> k & 1u ) != 0;
            return false;
        };
        h.focus = [ & ] { return focus != 0; };
        h.play = [ & ]( const char* n ) { say( std::string( "play:" ) + n ); };
        h.pause = [ & ] { say( "pause" ); };
        h.resume = [ & ] { say( "resume" ); };
        h.project_on = [ & ] { say( "repo+" ); say( "on" ); };
        h.project_off = [ & ] { say( "repo-" ); };
        h.project_ready = [ & ] { say( "OnProjectInitialized" ); };
        h.texture_load = [ & ]( const std::string& n ) { say( "tex:" + n ); };
        h.texture_free = [ & ] { say( "free" ); };
        c_msgbox_manager m;
        m.init( &screen, h );
        msgbox_entity_t ents[ 2 ];
        for ( auto& e : ents )
            e.m_fire = [ & ]( const char* n ) { say( std::string( "plug:" ) + n ); };
        for ( unsigned k = 0; k < count; ++k )
        {
            ++steps;
            log.clear( );
            const unsigned op = c.next( );
            if ( op == 0 )
            {
                const float dt = real( c.next( ) );
                keys = c.next( );
                focus = c.next( );
                const unsigned t = c.next( );
                screen.m_touch_moved = t & 1u;
                screen.m_touch_down = t >> 1 & 1u;
                screen.m_touch_up = t >> 2 & 1u;
                screen.m_touch[ 0 ] = real( c.next( ) );
                screen.m_touch[ 1 ] = real( c.next( ) );
                m.tick( dt );
            }
            else
            {
                msgbox_entity_t& e = ents[ c.next( ) ];
                if ( op == 1 )
                {
                    e.m_params.m_heading = c.str( );
                    e.m_params.m_body = c.str( );
                    e.m_params.m_text_a = c.str( );
                    e.m_params.m_text_b = c.str( );
                    e.m_params.m_texture = c.str( );
                    e.m_params.m_pause = static_cast< std::uint8_t >( c.next( ) );
                    e.m_params.m_input = static_cast< std::uint8_t >( c.next( ) );
                    msgbox_entity_create( e, m );
                }
                else
                    msgbox_entity_destroy( e );
            }
            const auto bits = [ ]( float f ) {
                unsigned u;
                std::memcpy( &u, &f, 4 );
                return u;
            };
            bool same = true;
            same = c.next( ) == static_cast< unsigned >( m.fsm( ).m_current ) && same;
            same = c.next( ) == bits( m.fade( ) ) && same;
            same = c.next( ) == bits( m.fsm( ).m_time ) && same;
            same = c.next( ) == m.queued( ) && same;
            same = c.next( ) == ( m.drawing( ) ? 1u : 0u ) && same;
            same = c.next( ) == static_cast< unsigned >( screen.m_result ) && same;
            same = c.next( ) == ( static_cast< unsigned >( screen.m_press_a ) | static_cast< unsigned >( screen.m_press_b ) << 8 ) && same;
            same = c.next( ) == bits( box.m_rotation ) && same;
            same = c.next( ) == static_cast< unsigned >( box.m_button ) && same;
            same = c.next( ) == ( screen.m_on ? 1u : 0u ) && same;
            unsigned en = 0;
            for ( int j = 1; j < 5; ++j )
                en |= static_cast< unsigned >( box.m_blocks[ j ].m_enabled ) << ( j - 1 );
            same = c.next( ) == en && same;
            same = c.next( ) == ( static_cast< unsigned >( screen.m_touch_moved ) << 16 | static_cast< unsigned >( screen.m_touch_down ) << 8 | screen.m_touch_up ) && same;
            for ( int j = 0; j < 5; ++j )
                same = c.str( ) == box.m_blocks[ j ].m_text && same;
            const std::string want = c.str( );
            same = want == log && same;
            if ( !same )
            {
                if ( ++bad < 6 )
                    LOG_ERROR( "message_box run %u step %u differs: retail [%s] ours [%s]", v, k, want.c_str( ), log.c_str( ) );
            }
        }
    }
    if ( bad )
        LOG_ERROR( "message_box FAILED %d of %d", bad, steps );
    else
        LOG_SUCCESS( "verify_message_box: %d steps of VuMessageBoxManager, its screen and VuMessageBoxEntity match retail", steps );
    return bad == 0;
}


// 0x440890 0x440900 0x46C950 0x476120 0x476140 against retail (case ui_odds).
inline bool verify_ui_odds( )
{
    save_words_t c{ g_golden_ui_odds_words };
    int bad = 0;
    const auto real = [ ]( unsigned u ) {
        float f;
        std::memcpy( &f, &u, 4 );
        return f;
    };
    unsigned n = c.next( );
    const unsigned picks = n;
    for ( unsigned k = 0; k < n; ++k )
    {
        const unsigned count = c.next( ), clock = c.next( ), base = c.next( ), want = c.next( );
        const int i = barracuda_pick( count, real( clock ), static_cast< int >( base ) );
        if ( want != ( i < 0 ? 0u : 0x1000u + static_cast< unsigned >( i ) ) )
            ++bad;
    }
    n = c.next( );
    const unsigned ticks = n;
    for ( unsigned k = 0; k < n; ++k )
    {
        const unsigned before = c.next( ), type = c.next( ), value = c.next( ), size = c.next( ), want = c.next( );
        float clock = real( before );
        const unsigned rec[ 2 ] = { type, value };
        barracuda_tick( clock, reinterpret_cast< const std::uint8_t* >( rec ), static_cast< int >( size ) );
        unsigned got;
        std::memcpy( &got, &clock, 4 );
        if ( got != want )
            ++bad;
    }
    n = c.next( );
    const unsigned touches = n;
    for ( unsigned k = 0; k < n; ++k )
    {
        const unsigned type = c.next( ), hit = c.next( ), armed = c.next( ), mid = c.next( ), last = c.next( );
        const std::string want = c.str( );
        touch_special_t e;
        e.m_type = static_cast< int >( type );
        e.m_armed = static_cast< std::uint8_t >( armed );
        touch_special_hit( e, static_cast< int >( hit ) );
        const unsigned got = e.m_armed;
        std::string log;
        touch_special_tick( e, [ & ]( const char* s ) { log += std::string( "play:" ) + s + "|"; }, [ & ] { log += "plug:Trigger"; } );
        if ( got != mid || e.m_armed != last || log != want )
            ++bad;
    }
    n = c.next( );
    const unsigned tilts = n;
    for ( unsigned k = 0; k < n; ++k )
    {
        const unsigned a = c.next( ), b = c.next( ), s = c.next( ), type = c.next( ), v = c.next( );
        if ( type != 3u || v != tilt_answer( a != 0, b != 0, static_cast< std::uint8_t >( s ) ) )
            ++bad;
    }
    if ( bad )
        LOG_ERROR( "ui_odds FAILED %d", bad );
    else
        LOG_SUCCESS( "verify_ui_odds: %u picks, %u ticks, %u touches, %u tilt answers match retail", picks, ticks, touches, tilts );
    return bad == 0;
}
