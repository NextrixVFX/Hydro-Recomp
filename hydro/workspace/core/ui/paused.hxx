#pragma once

#include <workspace/core/ui/fsm.hxx>
#include <workspace/core/ui/frame.hxx>
#include <workspace/core/ui/image.hxx>
#include <workspace/core/world/pause.hxx>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

// The pause menu past its gate: the object VuActionGameMode holds at +0x60 (ctor
// 0x4BAE80). world/pause.hxx keeps the gate (pause_menu_t: +0x30, +0x31, +0x38 and
// the handlers 0x4BB660 / 0x4BB900); this is the rest, transliterated from the
// disassembly (tools tmp/b28/d_4BB330.txt, d_4BAE80.txt):
//   the FSM +8: Unpaused (enter 0x4BBD10, exit 0x4BBD90), Paused (tick 0x4BBE30),
//     Transition (enter 0x4BBEE0); Unpaused -NextScreenSet-> Transition, Paused
//     -NextScreenSet-> Transition, Paused -Unpause-> Transition, Transition
//     -ScreenLoaded-> Paused, Transition -> Unpaused (always)
//   tick 0x4BB540, draw 0x4BB5E0 (the background 0x4BBCC0, the screen's vt+0x34)
//   the screen events: 0x4BB680 OnResumeActionGame, 0x4BB6F0 OnSetScreen, 0x4BB780
//     OnPushScreen, 0x4BB830 OnPopScreen, 0x4BB8E0 OnReloadScreen, 0x4BB980
//     OnSettingsCommand
//   the project: 0x4BBAE0 load, 0x4BBC20 release; the stack +0x68 (a deque of
//     VuString: 0x4BBF10 pop_back, 0x4BBF60 / 0x4BBFD0 push_back)
// HOOKS: FMOD (0x64C348 / 0x64C354 event play, 0x64C338 + vt+0x30 the "game"
// category), the tick manager g 0x759008 slots 11 / 12 / 15, the analytics g
// 0x758F9C (saves / online side: a platform hook), the input manager, the project
// load 0x4B4360 and the repository (the owner's, over the VuCommonScreen shape).

struct ui_pause_hooks_t
{
    std::function< void( const char* event ) > m_play;           // 0x64C348 + start 0x64C354
    std::function< void( bool paused ) > m_tick_paused;          // g 0x759008 vt+0x2C (true) / vt+0x30 (false)
    std::function< void( bool paused ) > m_category_paused;      // 0x64C338 "game", vt+0x30( paused )
    std::function< void( bool paused ) > m_game_paused;          // gameutil +0x60
    std::function< void( const char* name, const std::string& screen ) > m_analytics;   // g 0x758F9C vt+0x18 (screen "" : the empty 0x7593A0)
    std::function< float( ) > m_delta;                           // g 0x759008 vt+0x3C
    std::function< bool( ) > m_popup;                            // g 0x758FD4 +0x34 != 0
    std::function< bool( ) > m_busy;                             // gameutil +0x61 (the award screen sets it)
    std::function< bool( const char* name ) > m_input;           // g 0x759038 slot 9( 0, name )
    std::function< std::int32_t( ) > m_touches;                  // g 0x759070 +0x1C
    std::function< std::int32_t( ) > m_input_type;               // g 0x758FC0 +0x18
    // 0x4BBAE0's 0x4B4360 + repository + 0x500B10 + 0x501260 (when +0x14 bit 2 clear): true = a project (+0x7C).
    std::function< bool( const std::string& name ) > m_open;
    std::function< void( ) > m_close;                            // 0x4BBC20's release of +0x7C (the device block included)
    std::function< bool( ) > m_screen;                           // +0x7C's root is a VuUIScreenEntity (a non-null root)
    std::function< void( float dt, bool input ) > m_screen_tick; // its vt+0x30
    std::function< void( ) > m_screen_draw;                      // its vt+0x34( 1.0 )
    // The draw's sort writes and the background 0x4BBCC0 (the owner's gfx: null = no records).
    std::function< void( std::uint32_t colour ) > m_background;
    std::function< void( ) > m_sort_key;                         // ui_pause_key( ) on the sort's +0x74
};

// 0x4BB5E0's writes to the sort's +0x74.
inline std::uint32_t ui_pause_key( std::uint32_t k )
{
    k &= 0x07FFFFFFu;
    k |= 0x20000000u;
    k &= 0xFCFFFFFFu;
    k &= 0xFF7FFFFFu;
    k &= 0xFFC3FFFFu;
    k |= 0x00280000u;
    return k;
}

// 0x4BBCC0: VuGfxUtil's solid rect over the whole screen at depth 1.0 (0x6F76B8).
inline constexpr float k_ui_pause_rect[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
inline constexpr float k_ui_pause_depth = 1.0f;
inline void ui_pause_background( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, std::uint32_t colour )
{
    ui_solid_draw( sort, env, colour, k_ui_pause_rect, k_ui_pause_depth );
}

class c_ui_pause_menu
{
public:
    bool paused( ) const { return m_gate.paused; }   // ours: world/benchmark.hxx
    // `colour`: +0x32 (0xFF000000 from the ctor, then ConstantDB UI / PauseMenuBackgroundColor's 0x4FDEC0).
    c_ui_pause_menu( pause_menu_t& gate, ui_pause_hooks_t hooks, std::uint32_t colour ) : m_gate( gate ), m_hooks( std::move( hooks ) ), m_colour( colour )
    {
        fsm_state_t& unpaused = m_fsm.add_state( "Unpaused" );
        unpaused.m_enter = [ this ] { unpaused_enter( ); };
        unpaused.m_exit = [ this ] { unpaused_exit( ); };
        fsm_state_t& paused = m_fsm.add_state( "Paused" );
        paused.m_tick = [ this ]( float dt ) { paused_tick( dt ); };
        fsm_state_t& transition = m_fsm.add_state( "Transition" );
        transition.m_enter = [ this ] { transition_enter( ); };
        m_fsm.add_transition( "Unpaused", "Transition", "NextScreenSet" );
        m_fsm.add_transition( "Paused", "Transition", "NextScreenSet" );
        m_fsm.add_transition( "Paused", "Transition", "Unpause" );
        m_fsm.add_transition( "Transition", "Paused", "ScreenLoaded" );
        m_fsm.add_transition( "Transition", "Unpaused", "" );
        unpaused_enter( );   // 0x4BB2E5: the first state's enter
    }

    // 0x4BB540.
    void tick( )
    {
        const float dt = m_hooks.m_delta ? m_hooks.m_delta( ) : 0.0f;
        m_fsm.set_condition( "NextScreenSet", !m_gate.next.empty( ) );
        m_fsm.set_condition( "ScreenLoaded", m_project );
        m_fsm.evaluate( );
        fsm_state_t* s = m_fsm.current( );
        if ( s && s->m_tick )
            s->m_tick( dt );
        m_fsm.m_time = m_fsm.m_time + dt;
    }
    // 0x4BB5E0: the sort key's writes happen whether or not the menu shows.
    void draw( )
    {
        if ( m_hooks.m_sort_key )
            m_hooks.m_sort_key( );
        if ( !m_gate.paused )
            return;
        if ( m_hooks.m_background )
            m_hooks.m_background( m_colour );
        if ( m_project && m_hooks.m_screen && m_hooks.m_screen( ) && m_hooks.m_screen_draw )
            m_hooks.m_screen_draw( );
    }
    // 0x4BB680 "OnResumeActionGame", 0x4BB4D0 (the Pause button): "Unpause" + the sound 0x746D50.
    void on_resume( )
    {
        m_fsm.set_condition( "Unpause", true );
        play( "Global/UI_and_Menu/main_menu_choose" );
    }
    // 0x4BB6F0 "OnSetScreen".
    void on_set_screen( const std::string& name )
    {
        m_gate.next = name;
        play( "Global/UI_and_Menu/screen_next" );
    }
    // 0x4BB780 "OnPushScreen": the CURRENT screen (+0x50) goes on the stack.
    void on_push_screen( const std::string& name )
    {
        m_gate.next = name;
        m_stack.push_back( m_loaded );
        play( "Global/UI_and_Menu/screen_next" );
        if ( m_depth )
            ++m_depth;
    }
    // 0x4BB830 "OnPopScreen".
    void on_pop_screen( )
    {
        if ( m_stack.empty( ) )
            return;
        m_gate.next = m_stack.back( );
        m_stack.pop_back( );
        play( "Global/UI_and_Menu/screen_back" );
        if ( m_depth )
            --m_depth;
    }
    // 0x4BB8E0 "OnReloadScreen".
    void on_reload_screen( ) { m_gate.next = m_loaded; }
    // 0x4BB980 "OnSettingsCommand" (the params' first string).
    void on_settings_command( const std::string& name )
    {
        if ( ( m_hooks.m_popup && m_hooks.m_popup( ) ) || ( m_hooks.m_busy && m_hooks.m_busy( ) ) )
            return;
        if ( m_gate.paused )
        {
            if ( m_depth == 0 )
                on_push_screen( name );
            else
            {
                while ( m_depth >= 2 )
                {
                    if ( !m_stack.empty( ) )
                        m_stack.pop_back( );
                    --m_depth;
                }
                on_set_screen( name );
            }
            m_depth = 1;
            return;
        }
        m_stack.push_back( "Screens/PauseMenu" );
        set_pause( name );
    }

    const c_fsm& fsm( ) const { return m_fsm; }
    const std::string& loaded( ) const { return m_loaded; }
    const std::deque< std::string >& stack( ) const { return m_stack; }
    std::int32_t depth( ) const { return m_depth; }
    bool project( ) const { return m_project; }
    std::uint32_t colour( ) const { return m_colour; }

private:
    void play( const char* event )
    {
        if ( m_hooks.m_play )
            m_hooks.m_play( event );
    }
    // 0x4BB490: not paused and allowed: next = "Screens/PauseMenu", then the name when it is not null.
    void set_pause( const std::string& name )
    {
        if ( m_gate.paused || !m_gate.allow )
            return;
        m_gate.next = name;
    }
    // 0x4BBD10 / 0x4BBD90.
    void unpaused_enter( )
    {
        if ( !m_gate.paused )
            return;
        if ( m_hooks.m_tick_paused )
            m_hooks.m_tick_paused( false );
        if ( m_hooks.m_category_paused )
            m_hooks.m_category_paused( false );
        m_gate.paused = false;
        if ( m_hooks.m_game_paused )
            m_hooks.m_game_paused( false );
        if ( m_hooks.m_analytics )
            m_hooks.m_analytics( "Unpause", std::string( ) );
    }
    void unpaused_exit( )
    {
        if ( !m_gate.paused )
        {
            if ( m_hooks.m_tick_paused )
                m_hooks.m_tick_paused( true );
            if ( m_hooks.m_category_paused )
                m_hooks.m_category_paused( true );
            m_gate.paused = true;
            if ( m_hooks.m_game_paused )
                m_hooks.m_game_paused( true );
            if ( m_hooks.m_analytics )
                m_hooks.m_analytics( "Pause", std::string( ) );
        }
        m_fsm.set_condition( "Unpause", false );
    }
    // 0x4BBE30.
    void paused_tick( float dt )
    {
        if ( ( !m_hooks.m_touches || m_hooks.m_touches( ) == 0 ) && m_hooks.m_input && m_hooks.m_input( "Pause" ) )
            on_resume( );   // 0x4BB4D0
        if ( !m_project || !m_hooks.m_screen || !m_hooks.m_screen( ) )
            return;
        if ( ( m_hooks.m_popup && m_hooks.m_popup( ) ) || ( m_hooks.m_busy && m_hooks.m_busy( ) ) )
            return;
        const std::int32_t type = m_hooks.m_input_type ? m_hooks.m_input_type( ) : 0;
        if ( m_hooks.m_screen_tick )
            m_hooks.m_screen_tick( dt, type == 0 || type == 1 );
    }
    // 0x4BBEE0: the old project goes; a next name loads; next = "".
    void transition_enter( )
    {
        close( );
        if ( !m_gate.next.empty( ) )
            open( m_gate.next );
        m_gate.next.clear( );
    }
    // 0x4BBAE0.
    void open( const std::string& name )
    {
        m_loaded = name;
        m_project = m_hooks.m_open ? m_hooks.m_open( name ) : false;
        if ( m_hooks.m_analytics )
            m_hooks.m_analytics( "MenuShown", name );
    }
    // 0x4BBC20.
    void close( )
    {
        m_loaded.clear( );
        if ( m_project && m_hooks.m_close )
            m_hooks.m_close( );
        m_project = false;
    }

    pause_menu_t& m_gate;
    ui_pause_hooks_t m_hooks;
    std::uint32_t m_colour;
    c_fsm m_fsm;                        // +8
    std::string m_loaded;               // +0x50
    std::deque< std::string > m_stack;  // +0x68
    bool m_project = false;             // +0x7C non-null
    std::int32_t m_depth = 0;           // +0x80
};
