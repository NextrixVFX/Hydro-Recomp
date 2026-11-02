#pragma once

// VuTitleSequenceGameMode (world batch 38; ctor 0x4C6AB0, vtable 0x7462EC, tick 0x4C7010, draw 0x4C7130), the first mode of a normal launch
// (0x4CBD48). From the disassembly; no case: every call it makes is a hook, its state table is ctor data. Its screens are projects
// through c_world's menu engine (world/menu.hxx). The fade manager g 0x758FC4 is the menu engine's.
// Hooks: the touch device g 0x759070 (ctor 0x4C6EA1 registers the +4 callback, slot 0 0x4C7180: +0x50 = 1; there is none here, so a screen
// always runs its WaitTime); the mode manager's preload slots 11 .. 14: our load is synchronous, so slot 14 answers done at once:
// SUBSTITUTION. FMOD's category volumes (PreLoad's exit 0x4C74C0, settings +0x38): agent/boat's game/settings.hxx, not on this tree.

class c_title_mode final : public game_mode_t
{
public:
    explicit c_title_mode( c_world& w ) : m_world( w )
    {
        // 0x4C6AB0's table.
        m_fsm.add_state( "LoadNextScreen" ).m_enter = [ this ] { load_next( ); };                // 0x4C7290
        m_fsm.add_state( "FadeIn" ).m_enter = [ this ] { fade( 1.0f, -1.0f ); m_touched = false; };   // 0x4C72E0
        m_fsm.add_state( "Wait" ).m_tick = [ this ]( float dt ) { wait( dt ); };                 // 0x4C7330
        m_fsm.add_state( "FadeOut" ).m_enter = [ this ] { fade_out( ); };                        // 0x4C73B0
        m_fsm.add_state( "FadeInLoading" ).m_enter = [ this ] { fade( 1.0f, -1.0f ); };          // 0x4C7450
        fsm_state_t& preload = m_fsm.add_state( "PreLoad" );
        preload.m_tick = [ this ]( float ) { preload_tick( ); };                                  // 0x4C7550
        m_fsm.add_state( "Exit" );
        m_fsm.add_transition( "LoadNextScreen", "FadeInLoading", "LoadingScreenLoaded" );
        m_fsm.add_transition( "LoadNextScreen", "FadeIn", "ScreenLoaded" );
        m_fsm.add_transition( "LoadNextScreen", "Exit", "" );
        m_fsm.add_transition( "FadeIn", "Wait", "FadeManagerInactive" );
        m_fsm.add_transition( "Wait", "FadeOut", "Expired" );
        m_fsm.add_transition( "Wait", "FadeOut", "Touched" );
        m_fsm.add_transition( "FadeInLoading", "PreLoad", "FadeManagerInactive" );
        m_fsm.add_transition( "PreLoad", "FadeOut", "LoadingDone" );
        m_fsm.add_transition( "FadeOut", "LoadNextScreen", "FadeManagerInactive" );
        const value_t& title = member( member( w.title_db( ), "VuDBAsset" ), "TitleSequence" );
        const value_t& screens = member( title, "Screens" );
        if ( screens.tag == 5 )
            for ( const value_t& s : screens.list )
                m_screens.push_back( s.tag == 4 ? s.text : std::string( ) );
        const auto number = []( const value_t& v ) {
            float f = 0.0f;
            return ui_json_float( v, f ) ? f : 0.0f;
        };
        m_fade_time = number( member( title, "FadeTime" ) );
        m_wait_time = number( member( title, "WaitTime" ) );
        LOG_INFO( "title sequence: %zu screens, fade %.2f, wait %.2f", m_screens.size( ), m_fade_time, m_wait_time );
    }

    // Slot 2 0x4C6FD0: the first state's enter.
    bool enter( const std::string& ) override
    {
        m_world.title_enter( );
        m_fsm.start( );
        if ( fsm_state_t* s = m_fsm.current( ) )
            if ( s->m_enter )
                s->m_enter( );
        return true;
    }

    // Slot 3 0x4C6FF0: the current state's exit.
    void exit( ) override
    {
        if ( fsm_state_t* s = m_fsm.current( ) )
            if ( s->m_exit )
                s->m_exit( );
        m_world.title_exit( );
    }

    // Slot 4 0x4C7010.
    const std::string& tick( float dt ) override
    {
        static const std::string none, front_end = "FrontEnd";
        front_end_engine_t& e = m_world.title_engine( );
        m_fsm.set_condition( "FadeManagerInactive", e.m_fade.m_rate == 0.0f );
        m_fsm.set_condition( "ScreenLoaded", m_project != 0 );
        m_fsm.set_condition( "LoadingScreenLoaded", m_project != 0 && m_screens.empty( ) );
        m_fsm.set_condition( "Touched", m_touched );
        m_fsm.evaluate( );
        if ( fsm_state_t* s = m_fsm.current( ) )
            if ( s->m_tick )
                s->m_tick( dt );
        m_fsm.m_time = m_fsm.m_time + dt;
        e.m_fade.tick( dt, nullptr );   // its own Final tick, as menu_tick
        if ( !m_reported || m_reported != m_fsm.current( ) )
        {
            m_reported = m_fsm.current( );
            if ( m_reported )
                LOG_INFO( "title sequence: %s", m_reported->m_name.c_str( ) );
        }
        const fsm_state_t* s = m_fsm.current( );
        return s && s->m_name == "Exit" ? front_end : none;
    }

    // Slot 5 0x4C7130: the state's draw (none has one), the screen's slot 13 at 1.0.
    void draw( ) override
    {
        front_end_engine_t& e = m_world.title_engine( );
        if ( m_project && e.m_is_screen && e.m_is_screen( m_project ) && e.m_screen_draw )
            e.m_screen_draw( m_project, 1.0f );
    }

private:
    // 0x4C7200.
    void release( )
    {
        if ( !m_project )
            return;
        m_world.title_engine( ).m_release_project( m_project );
        m_project = 0;
    }

    // 0x4C7290: the last screen released; the deque's first loaded (0x4C7190: 0x4B4360, its gameInitialize unless it has run,
    // "OnProjectInitialized") and popped.
    void load_next( )
    {
        release( );
        if ( m_screens.empty( ) )
            return;
        front_end_engine_t& e = m_world.title_engine( );
        m_project = e.m_load_project( m_screens.front( ) );
        if ( m_project )
            e.m_init_project( m_project );
        m_screens.pop_front( );
    }

    // 0x4C72E0 / 0x4C7450: FadeTime, a negative one the manager's duration; a positive time: value, rate = step / time; else value and rate 0.
    void fade( float value, float step )
    {
        action_fade_t& f = m_world.title_engine( ).m_fade;
        float time = m_fade_time;
        if ( 0.0f > time )
            time = f.m_duration;
        if ( time > 0.0f )
        {
            f.m_value = value;
            f.m_rate = step / time;
        }
        else
        {
            f.m_value = 1.0f - value;
            f.m_rate = 0.0f;
        }
    }

    // 0x4C7330: the screen's slot 12 unless the game is paused (g 0x758FD4 +0x34: nothing sets it here); WaitTime up: "Expired".
    void wait( float dt )
    {
        front_end_engine_t& e = m_world.title_engine( );
        if ( m_project && e.m_is_screen && e.m_is_screen( m_project ) && e.m_screen_tick )
            e.m_screen_tick( m_project, dt, true );
        if ( m_fsm.m_time >= m_wait_time )
            m_fsm.pulse_condition( "Expired" );
    }

    // 0x4C73B0: a touch's sound (the global 0x746D50, "Global/UI_and_Menu/main_menu_choose"), the fade to black.
    void fade_out( )
    {
        if ( m_touched )
            m_world.title_sound( );
        fade( 0.0f, 1.0f );
    }

    // 0x4C7550: slot 14 done: "OnLoadProgress" { 1.0 } and "LoadingDone".
    void preload_tick( )
    {
        m_world.title_progress( m_project, 1.0f );
        m_fsm.pulse_condition( "LoadingDone" );
    }

    c_world& m_world;
    c_fsm m_fsm;                         // +8
    std::deque< std::string > m_screens; // +0x34
    int m_project = 0;                   // +0x30
    float m_fade_time = 0.0f;            // +0x48
    float m_wait_time = 0.0f;            // +0x4C
    bool m_touched = false;              // +0x50
    const fsm_state_t* m_reported = nullptr;
};

inline const value_t& c_world::title_db( )
{
    if ( m_constant_db.tag == 0 )
        race_dbs( );
    return m_constant_db;
}

inline void c_world::title_enter( )
{
    m_title = true;
    menu_bind( );
}

inline void c_world::title_exit( )
{
    m_title = false;
    m_menu_projects.clear( );
}

inline void c_world::title_sound( )
{
    std::uint32_t event = 0;
    if ( m_sound.get_event && m_sound.get_event( m_sound.user, "Global/UI_and_Menu/main_menu_choose", 1u, &event, nullptr ) == 0 && m_sound.start )
        m_sound.start( m_sound.user, event );
}

inline void c_world::title_progress( int project, float value )
{
    const auto it = m_menu_projects.find( project );
    if ( it == m_menu_projects.end( ) )
        return;
    ui_params_t p;
    const std::uint32_t bits = std::bit_cast< std::uint32_t >( value );
    ui_params_add( p, 2u, bits );
    it->second.binding->post( fsm_hash( "OnLoadProgress" ), p );
}

// A plain launch: no test driver and no command-line game. `--title` (ours) forces it for a test.
inline bool c_world::title_launch( ) const
{
    if ( m_has_chosen )
        return false;
    if ( c_engine::option( "title" ) != nullptr )
        return true;
    for ( const char* other : { "bench", "selftest", "race", "mode", "course", "boat", "skin", "menu", "front-end", "front-end-defaults" } )
        if ( c_engine::option( other ) != nullptr )
            return false;
    return true;
}
