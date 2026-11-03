#pragma once

// VuActionGameMode - the game mode a race runs in (vtables 0x745F88 / 0x745F7C;
// ctor 0x4B9160), transliterated. Its VuFSM (ui/fsm.hxx c_fsm, at +0x10):
//
//   [LoadingScreen -(LoadingScreenDone)->] LoadLevel -("")-> Begin -(FadeManagerInactive)-> Run
//   Run -(Restart)-> Exit      Run -(NextGameModeSet)-> End -(FadeManagerInactive)-> Exit
//   Run -(SnapViewActive)-> SnapView -(SnapViewInactive)-> Run
//
// The LoadingScreen state and its transition exist only when GameData's
// "DevRestart" (g_vugameutil 0x758FF8 +0x50, 0x4FBDF0) is not a true bool
// (tag 3, byte +8); the ctor then erases "DevRestart" (0x4FC140). Its events
// (the mode's event map +8, 0x515800 / 0x4BACE0): "OnEndActionGame" 0x4B9B70
// (next = "FrontEnd") and "OnRestartActionGame" 0x4B9B90 (next = "Action").
// Nothing in the ctor or the tick sets the "Restart" condition (a transition
// that cannot fire unless something else writes it - none found).
//
// The states' delegates (enter / exit / tick / draw):
//   LoadingScreen 0x4B9CA0 / 0x4B9DE0 / 0x4B9E20 / 0x4B9FE0
//   LoadLevel     0x4BA060 (the load: world.hxx load_scene's body)
//   Begin         0x4BA9E0 / - / - / 0x4B9C10
//   End           0x4BAAD0 / - / 0x4BAB10 / 0x4B9C10
//   Run           0x401560 (ret) / - / 0x4BAB50 / 0x4B9C10
//   SnapView      0x4BABA0 / 0x4BAC50 / - / 0x4BAC90
//   Exit          none
// The draws (0x4B9C10 the game's draw + the pause menu; 0x4B9FE0 the loading
// screen's; 0x4BAC90 the snap screen) are the renderer's / UI's: hooks.
//
// The fade manager g 0x758FC4 is ui/flow.hxx's front_end_engine_t::ui_fade_t
// (+4 duration, +8 value, +0xC rate, +0x10 skip; its "Final" tick 0x4AEE50).
// SAME RETAIL FUNCTION TWICE, FLAGGED: End's enter 0x4BAAD0 / tick 0x4BAB10 are
// also ui/flow.hxx c_front_end::end_enter / end_tick (private there): the
// front end binds the same two member functions. action_end_enter /
// action_end_tick below are free functions over the fade the UI side can call.

#include <workspace/core/ui/flow.hxx>
#include <workspace/core/ui/fsm.hxx>

#include <cstdint>
#include <functional>
#include <string>

using action_fade_t = front_end_engine_t::ui_fade_t;

// End enter 0x4BAAD0: a fade out (duration not > 0 - comiss / jbe, a NaN too:
// value 1, rate 0; else value 0, rate 1 / duration).
inline void action_end_enter( action_fade_t& f )
{
    if ( !( f.m_duration > 0.0f ) )
    {
        f.m_value = 1.0f;
        f.m_rate = 0.0f;
        return;
    }
    f.m_value = 0.0f;
    f.m_rate = 1.0f / f.m_duration;
}

// End tick 0x4BAB10: with g_vugameutil's music channel (+0x44), FMOD
// Channel::setVolume( (1 - value) * +0x48 ) - the audio platform (a hook).
inline void action_end_tick( const action_fade_t& f, bool channel, float volume, const std::function< void( float ) >& set_volume )
{
    if ( channel && set_volume )
        set_volume( ( 1.0f - f.m_value ) * volume );
}

// Begin enter 0x4BA9E0's fade half (after the tutorial check): with value > 0
// (comiss / jbe), the skip count += 3 and then - duration > 0: value 1, rate
// -1 / duration (a fade in); else value 0, rate 0. Value not > 0: untouched.
inline void action_begin_fade( action_fade_t& f )
{
    if ( !( f.m_value > 0.0f ) )
        return;
    f.m_skip += 3u;
    if ( f.m_duration > 0.0f )
    {
        f.m_value = 1.0f;
        f.m_rate = -1.0f / f.m_duration;
        return;
    }
    f.m_value = 0.0f;
    f.m_rate = 0.0f;
}

// Everything the mode reaches that is not recreated here (each named where
// it is called). Null = nothing happens.
struct action_mode_hooks_t
{
    std::function< void( ) > erase_dev_restart;            // the ctor's 0x4FC140( "DevRestart" ) on GameData (whatever it held)
    std::function< void( int ) > viewports;                 // g 0x759060 0x530BB0 (unread)
    std::function< bool( ) > load_loading_screen;          // 0x4D3690 (GameData -> the name) + 0x4B4360 -> +0x38
    std::function< bool( ) > load_snap;                    // 0x4B4360( "Screens/SnapMode" ) -> +0x3C, then its init
    std::function< void( ) > loading_init;                 // 0x4B9CA0's project half (repository / 0x500B10 / 0x501260)
    std::function< void( ) > loading_assets;               // 0x4B9CA0: 0x4D3410 + 0x4D2440 (the asset list +0xE8)
    std::function< void( ) > loading_release;              // 0x4B9DE0's project half
    std::function< bool( ) > loading_more;                 // 0x4B9E20: list +0xE8 non-null and index +0xEC < count
    std::function< void( ) > loading_load;                 // 0x4D24F0 (the next asset)
    std::function< float( ) > loading_progress;            // +0x100 / +0x104 (cvtdq2ps), 1.0 when +0x104 == 0
    std::function< double( ) > clock;                      // g 0x759000 vt+0x28 (VuSys time, seconds - platform)
    std::function< void( const char*, float ) > broadcast_float;   // VuEventManager_broadcast 0x505690 (one float)
    std::function< void( const char* ) > broadcast;        // the same, empty VuParams
    std::function< void( ) > load_level;                   // LoadLevel's enter 0x4BA060
    std::function< void( ) > tutorial;                     // Begin enter's g 0x758FB8 +0x28 / profile "TutorialShown" half
    std::function< void( ) > action_game_data;             // 0x4D2DE0 (gameutil)
    std::function< bool( float ) > game_tick;              // Run: the game (+0x44) 0x482F10( dt ); null = no game
    std::function< void( float ) > pause_tick;             // Run: the pause menu +0x60 0x4BB540
    std::function< void( bool ) > snap_pause;              // SnapView 0x4BABA0 / 0x4BAC50 (tick manager / audio)
    std::function< void( int&, int& ) > display;           // g 0x75907C vt+0x40 (the display size)
    bool music_channel = false;                            // g_vugameutil +0x44 non-null
    float music_volume = 0.0f;                             // +0x48
    std::function< void( float ) > set_music_volume;       // FMOD Channel::setVolume
    std::function< void( const char* ) > draw;             // the draws, by retail address name ("0x4B9C10" ...)
};

class c_action_mode
{
public:
    // The ctor 0x4B9160 (the states, delegates and transitions in its order).
    c_action_mode( action_fade_t& fade, bool dev_restart, action_mode_hooks_t hooks ) : m_fade( fade ), m_hooks( std::move( hooks ) )
    {
        // (retail erases it after reading it, 0x4B92EE - before any state is added)
        if ( m_hooks.erase_dev_restart )
            m_hooks.erase_dev_restart( );
        if ( !dev_restart )
        {
            fsm_state_t& s = m_fsm.add_state( "LoadingScreen" );
            s.m_enter = [ this ] { loading_enter( ); };
            s.m_exit = [ this ] { loading_exit( ); };
            s.m_tick = [ this ]( float ) { loading_tick( ); };
            s.m_draw = [ this ] { draw( "0x4B9FE0" ); };
            s.m_address[ 0 ] = 0x4B9CA0u;
            s.m_address[ 1 ] = 0x4B9DE0u;
            s.m_address[ 2 ] = 0x4B9E20u;
            s.m_address[ 3 ] = 0x4B9FE0u;
        }
        fsm_state_t& load = m_fsm.add_state( "LoadLevel" );
        load.m_enter = [ this ] { if ( m_hooks.load_level ) m_hooks.load_level( ); };
        load.m_address[ 0 ] = 0x4BA060u;
        fsm_state_t& begin = m_fsm.add_state( "Begin" );
        begin.m_enter = [ this ] { begin_enter( ); };
        begin.m_draw = [ this ] { draw( "0x4B9C10" ); };
        begin.m_address[ 0 ] = 0x4BA9E0u;
        begin.m_address[ 3 ] = 0x4B9C10u;
        fsm_state_t& end = m_fsm.add_state( "End" );
        end.m_enter = [ this ] { action_end_enter( m_fade ); };
        end.m_tick = [ this ]( float ) { action_end_tick( m_fade, m_hooks.music_channel, m_hooks.music_volume, m_hooks.set_music_volume ); };
        end.m_draw = [ this ] { draw( "0x4B9C10" ); };
        end.m_address[ 0 ] = 0x4BAAD0u;
        end.m_address[ 2 ] = 0x4BAB10u;
        end.m_address[ 3 ] = 0x4B9C10u;
        fsm_state_t& run = m_fsm.add_state( "Run" );
        run.m_enter = [ ] { };   // 0x401560: ret
        run.m_tick = [ this ]( float dt ) { run_tick( dt ); };
        run.m_draw = [ this ] { draw( "0x4B9C10" ); };
        run.m_address[ 0 ] = 0x401560u;
        run.m_address[ 2 ] = 0x4BAB50u;
        run.m_address[ 3 ] = 0x4B9C10u;
        fsm_state_t& snap = m_fsm.add_state( "SnapView" );
        snap.m_enter = [ this ] { if ( m_hooks.snap_pause ) m_hooks.snap_pause( true ); };
        snap.m_exit = [ this ] { if ( m_hooks.snap_pause ) m_hooks.snap_pause( false ); };
        snap.m_draw = [ this ] { draw( "0x4BAC90" ); };
        snap.m_address[ 0 ] = 0x4BABA0u;
        snap.m_address[ 1 ] = 0x4BAC50u;
        snap.m_address[ 3 ] = 0x4BAC90u;
        m_fsm.add_state( "Exit" );
        if ( !dev_restart )
            m_fsm.add_transition( "LoadingScreen", "LoadLevel", "LoadingScreenDone" );
        m_fsm.add_transition( "LoadLevel", "Begin", "" );
        m_fsm.add_transition( "Begin", "Run", "FadeManagerInactive" );
        m_fsm.add_transition( "Run", "Exit", "Restart" );
        m_fsm.add_transition( "Run", "End", "NextGameModeSet" );
        m_fsm.add_transition( "Run", "SnapView", "SnapViewActive" );
        m_fsm.add_transition( "SnapView", "Run", "SnapViewInactive" );
        m_fsm.add_transition( "End", "Exit", "FadeManagerInactive" );
    }

    // The event map's two handlers.
    void music_level( float v ) { m_hooks.music_volume = v; m_hooks.music_channel = v > 0.0f; }
    void on_end_action_game( ) { m_next = "FrontEnd"; }       // 0x4B9B70
    void on_restart_action_game( ) { m_next = "Action"; }     // 0x4B9B90

    // Slot 2 0x4B9630 (enter): 0x530BB0( 1 ); the loading screen's project
    // (0x4D3690 names it from GameData) -> +0x38; "Screens/SnapMode" -> +0x3C,
    // initialised unless its root is (repository add, 0x500B10, 0x501260);
    // current = the first state, its enter. Answers true.
    bool enter( )
    {
        if ( m_hooks.viewports )
            m_hooks.viewports( 1 );
        m_loading = m_hooks.load_loading_screen && m_hooks.load_loading_screen( );
        if ( m_hooks.load_snap )
            m_hooks.load_snap( );
        m_fsm.start( );
        return true;
    }

    // Slot 4 0x4B99E0 (dt): the four conditions, evaluate 0x555AD0, the
    // current state's tick, time += dt; the next game mode's name (+0x48)
    // when the current state is "Exit" (a 4-byte compare of the name), else "".
    const std::string& tick( float dt )
    {
        m_fsm.set_condition( "FadeManagerInactive", m_fade.m_rate == 0.0f );   // ucomiss + lahf / 0x44: a NaN rate is "active"
        m_fsm.set_condition( "NextGameModeSet", !m_next.empty( ) );
        int w = 0, h = 0;
        if ( m_hooks.display )
            m_hooks.display( w, h );
        const float ratio = static_cast< float >( w ) / static_cast< float >( h );
        const bool snapped = 1.0f > ratio;   // comiss / seta: a NaN ratio is not snapped
        m_fsm.set_condition( "SnapViewActive", snapped );
        m_fsm.set_condition( "SnapViewInactive", !snapped );
        m_fsm.evaluate( );
        if ( m_fsm.current( )->m_tick )
            m_fsm.current( )->m_tick( dt );
        m_fsm.m_time = m_fsm.m_time + dt;
        static const std::string none;
        return m_fsm.current( )->m_name == "Exit" ? m_next : none;
    }

    // Slot 5 0x4B9B60: the current state's draw.
    void draw_state( )
    {
        if ( m_fsm.current( )->m_draw )
            m_fsm.current( )->m_draw( );
    }

    const char* state( ) const { return m_fsm.current( ) ? m_fsm.current( )->m_name.c_str( ) : ""; }

    c_fsm m_fsm;           // +0x10
    std::string m_next;    // +0x48
    bool m_loading = false;   // +0x38 non-null
    double m_clock = 0.0;     // +0x108

private:
    void draw( const char* which )
    {
        if ( m_hooks.draw )
            m_hooks.draw( which );
    }

    // 0x4B9CA0: the loading project's init (when loaded and not yet
    // initialised); the fade value and rate 0; the asset list (0x4D3410 /
    // 0x4D2440) and the clock into +0x108.
    void loading_enter( )
    {
        if ( m_loading && m_hooks.loading_init )
            m_hooks.loading_init( );
        m_fade.m_value = 0.0f;
        m_fade.m_rate = 0.0f;
        if ( m_hooks.loading_assets )
            m_hooks.loading_assets( );
        m_clock = m_hooks.clock ? m_hooks.clock( ) : 0.0;
    }

    // 0x4B9DE0: the loading project released when initialised; fade value 1, rate 0.
    void loading_exit( )
    {
        if ( m_loading && m_hooks.loading_release )
            m_hooks.loading_release( );
        m_fade.m_value = 1.0f;
        m_fade.m_rate = 0.0f;
    }

    // 0x4B9E20: while assets remain, load the next; once the clock passes
    // +0x108 + 0.1 s (comisd / jbe: strictly later) broadcast "OnLoadProgress"
    // with the fraction, keep the clock, return. With none left:
    // "OnLoadProgress" 1.0 and the "LoadingScreenDone" pulse (+0x1D).
    void loading_tick( )
    {
        while ( m_hooks.loading_more && m_hooks.loading_more( ) )
        {
            if ( m_hooks.loading_load )
                m_hooks.loading_load( );
            const double now = m_hooks.clock ? m_hooks.clock( ) : 0.0;
            if ( now > m_clock + 0.1 )
            {
                if ( m_hooks.broadcast_float )
                    m_hooks.broadcast_float( "OnLoadProgress", m_hooks.loading_progress ? m_hooks.loading_progress( ) : 1.0f );
                m_clock = now;
                return;
            }
        }
        if ( m_hooks.broadcast_float )
            m_hooks.broadcast_float( "OnLoadProgress", 1.0f );
        m_fsm.pulse_condition( "LoadingScreenDone" );
    }

    // 0x4BA9E0: the tutorial half (a hook), the fade in, 0x4D2DE0.
    void begin_enter( )
    {
        if ( m_hooks.tutorial )
            m_hooks.tutorial( );
        action_begin_fade( m_fade );
        if ( m_hooks.action_game_data )
            m_hooks.action_game_data( );
    }

    // 0x4BAB50: with a game, 0x482F10( dt ); false -> next = "FrontEnd"; then
    // the pause menu's tick 0x4BB540( dt ).
    void run_tick( float dt )
    {
        if ( m_hooks.game_tick && !m_hooks.game_tick( dt ) )
            m_next = "FrontEnd";
        if ( m_hooks.pause_tick )
            m_hooks.pause_tick( dt );
    }

    action_fade_t& m_fade;
    action_mode_hooks_t m_hooks;
};
