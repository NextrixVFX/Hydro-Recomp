#pragma once

#include <workspace/core/ui/common.hxx>
#include <workspace/core/ui/flow.hxx>
#include <workspace/core/ui/fsm.hxx>
#include <workspace/core/ui/select.hxx>

#include <cstdint>
#include <deque>
#include <emmintrin.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// VuAwardScreen (vtable 0x73F570, 0x94 bytes, a VuCommonScreen; its second vtable 0x73F568 at +0x10 is the unlock
// listener), the object VuGame's Awards enter 0x484320 builds. Transliterated from the disassembly (tools
// tmp/b28/d_47ECC0.txt, d_47F220.txt):
//   ctor 0x47ECC0: the project 0x4B4360 (+0xC), "OnAwardFlowNext" 0x47F770, the FSM +0x14: Initial, Trophy (enter
//     0x47F820, exit 0x47FCC0), Credits (tick 0x47FCD0), Feats (tick 0x47FE40), UnlockFromCredits / UnlockFromFeats
//     (enter 0x47FE90, exit 0x47FCC0), Decision, ExitDecision, PreExit (enter 0x4803B0, exit 0x47FCC0), Exit
//   0x47F350 start, 0x47F4C0 exit, 0x47F580 tick, 0x47F6D0 draw, 0x47F720 done
//   0x47F790 the listener's slot 0 (the unlock queue +0x7C, a deque: 0x4805C0 / 0x4806B0 / 0x4806E0 / 0x480650)
//   0x47F7B0 "OnAwardScreenBusy", 0x47FCC0 / 0x4803B0 its two fixed calls
//   0x47F270 / 0x47F220 / 0x47F250 the dtor, 0x4803C0 / 0x480550 / 0x480860 / 0x4808A0 the names' vector assignment:
//     the members' own destructors / std::vector's assignment here
// HOOKS: the game manager g 0x758FE8 (saves: credits and unlocks are a platform hook), FMOD, the input manager, the
// analytics g 0x758FA8 0x4B0650, the event manager's broadcast 0x505690.

// An unlock (the listener's VuJsonContainer): 0x47FE90 reads four string members; 0x4FBDF0's type test answers "" for a
// member that is not a string (0x64D20B). "Name" is looked up too, its value unused.
struct ui_award_unlock_t
{
    std::string m_string_id, m_image, m_type, m_mode;
};

struct ui_award_hooks_t
{
    std::function< void( const char* name, const ui_params_t& params ) > m_event;  // 0x501260 on the root
    std::function< void( bool busy ) > m_screen_busy;                              // 0x47F7B0's broadcast 0x505690
    std::function< void( const char* event ) > m_play;                             // 0x64C348 + start 0x64C354
    std::function< bool( const char* name ) > m_input;                             // g 0x759038 slot 9( 0, name )
    std::function< std::int32_t( ) > m_touches;                                    // g 0x759070 +0x1C
    std::function< void( std::int32_t delta, std::int32_t flag ) > m_credits_add;  // g 0x758FE8 vt+0x34( delta, +0x6C, the listener )
    std::function< std::int32_t( ) > m_credits;                                    // g 0x758FE8 vt+0x38
    std::function< bool( std::int32_t& room ) > m_credits_room;                    // g 0x758FE8 vt+0x6C( &room )
    std::function< void( const std::string& name ) > m_unlock;                     // g 0x758FE8 vt+0x48( name, the listener )
    std::function< void( const char* name, std::int64_t value ) > m_analytics;     // g 0x758FA8 0x4B0650
    std::function< void( bool busy ) > m_game_busy;                                // gameutil +0x61
};

// 0x434E50: a VuParams int record (type 0), dropped when it does not fit.
inline void ui_params_add_int( ui_params_t& p, std::int32_t value )
{
    if ( p.m_size + 8 > 128 )
        return;
    const std::uint32_t type = 0;
    std::memcpy( p.m_bytes + p.m_size, &type, 4 );
    std::memcpy( p.m_bytes + p.m_size + 4, &value, 4 );
    p.m_size += 8;
}

class c_ui_award_screen
{
public:
    c_ui_award_screen( const entity_t* root, ui_common_ops_t ops, ui_award_hooks_t hooks ) : m_common( root, std::move( ops ) ), m_hooks( std::move( hooks ) )
    {
        m_fsm.add_state( "Initial" );
        fsm_state_t& trophy = m_fsm.add_state( "Trophy" );
        trophy.m_enter = [ this ] { trophy_enter( ); };
        trophy.m_exit = [ this ] { busy( true ); };
        m_fsm.add_state( "Credits" ).m_tick = [ this ]( float dt ) { credits_tick( dt ); };
        m_fsm.add_state( "Feats" ).m_tick = [ this ]( float ) { feats_tick( ); };
        for ( const char* name : { "UnlockFromCredits", "UnlockFromFeats" } )
        {
            fsm_state_t& unlock = m_fsm.add_state( name );
            unlock.m_enter = [ this ] { unlock_enter( ); };
            unlock.m_exit = [ this ] { busy( true ); };
        }
        m_fsm.add_state( "Decision" );
        m_fsm.add_state( "ExitDecision" );
        fsm_state_t& pre_exit = m_fsm.add_state( "PreExit" );
        pre_exit.m_enter = [ this ] { busy( false ); };   // 0x4803B0
        pre_exit.m_exit = [ this ] { busy( true ); };
        m_fsm.add_state( "Exit" );
        m_fsm.add_transition( "Initial", "Trophy", "EarnedTrophy" );
        m_fsm.add_transition( "Initial", "Credits", "EarnedCredits" );
        m_fsm.add_transition( "Initial", "Feats", "EarnedFeats" );
        m_fsm.add_transition( "Initial", "Exit", "" );
        m_fsm.add_transition( "Trophy", "Credits", "OnUserNext" );
        m_fsm.add_transition( "Credits", "UnlockFromCredits", "UnlockableQueued" );
        m_fsm.add_transition( "Credits", "Feats", "CreditsDone" );
        m_fsm.add_transition( "Feats", "UnlockFromFeats", "UnlockableQueued" );
        m_fsm.add_transition( "Feats", "ExitDecision", "FeatsDone" );
        m_fsm.add_transition( "UnlockFromCredits", "Credits", "OnUserNext" );
        m_fsm.add_transition( "UnlockFromFeats", "Feats", "OnUserNext" );
        m_fsm.add_transition( "ExitDecision", "PreExit", "WaitForInput" );
        m_fsm.add_transition( "ExitDecision", "Exit", "" );
        m_fsm.add_transition( "PreExit", "Exit", "OnUserNext" );
    }

    // 0x47F350 (the caller's first and last dwords are not read). `tier`: the finish place, 4 in a championship.
    // The root must be a screen: retail's event then has a null `this` otherwise (no Screens/Award root is one).
    void start( std::int32_t tier, const std::string& boat_class, std::int32_t value, const std::vector< std::string >& names )
    {
        m_team = false;
        m_tier = tier;
        m_class = boat_class;
        m_value = value;
        m_names = names;
        m_flag = 0;
        m_common.open( );
        m_fsm.start( );
        credits_changed( );
        m_fsm.set_condition( "EarnedTrophy", m_tier <= 2 );
        m_fsm.set_condition( "EarnedCredits", m_value > 0 );
        m_fsm.set_condition( "EarnedFeats", !m_names.empty( ) );
        m_fsm.evaluate( );
        if ( m_hooks.m_game_busy )
            m_hooks.m_game_busy( true );
    }
    // 0x47F4C0.
    void exit( )
    {
        const fsm_state_t* s = m_fsm.current( );
        if ( s->m_exit )
            s->m_exit( );
        m_common.exit( );
        if ( m_hooks.m_game_busy )
            m_hooks.m_game_busy( false );
        if ( m_hooks.m_analytics )
            m_hooks.m_analytics( "CreditsEarned", m_hooks.m_credits ? static_cast< std::int64_t >( m_hooks.m_credits( ) ) : 0 );
    }
    // The object's deletion: 0x480980's device block.
    void close( ) { m_common.close( ); }
    // 0x47F580.
    void tick( float dt )
    {
        m_common.tick( dt );
        const bool pressed = m_hooks.m_input && m_hooks.m_input( "Select" );
        if ( m_hooks.m_touches && m_hooks.m_touches( ) == 0x7FFF && pressed && !m_select )
            m_fsm.pulse_condition( "OnUserNext" );
        m_select = pressed;
        m_fsm.set_condition( "CreditsDone", m_earned >= m_value );
        m_fsm.set_condition( "FeatsDone", m_feat >= static_cast< std::int32_t >( m_names.size( ) ) );
        m_fsm.set_condition( "UnlockableQueued", !m_queue.empty( ) );
        m_fsm.evaluate( );
        const fsm_state_t* s = m_fsm.current( );
        if ( s->m_tick )
            s->m_tick( dt );
        m_fsm.m_time = m_fsm.m_time + dt;
    }
    // 0x47F6D0: no state of this machine has a draw.
    void draw( ) { m_common.draw( ); }
    // 0x47F720.
    bool done( ) const { return m_fsm.current( )->m_name == "Exit"; }
    // 0x47F770 "OnAwardFlowNext".
    void on_flow_next( ) { m_fsm.pulse_condition( "OnUserNext" ); }
    // 0x47F790: what the game manager calls for each thing that unlocks.
    void on_unlock( ui_award_unlock_t unlock ) { m_queue.push_back( std::move( unlock ) ); }
    // +0x3C: 0x47F350 clears it and nothing recreated sets it. For the team branch of 0x47F820 (the check enters it directly).
    void trophy_team( std::int32_t tier )
    {
        m_team = true;
        m_tier = tier;
        trophy_enter( );
    }

    const c_fsm& fsm( ) const { return m_fsm; }
    std::int32_t earned( ) const { return m_earned; }
    std::int32_t feat( ) const { return m_feat; }
    float elapsed( ) const { return m_elapsed; }
    std::size_t queued( ) const { return m_queue.size( ); }
    bool select( ) const { return m_select; }

private:
    void event( const char* name, const ui_params_t& params )
    {
        if ( m_hooks.m_event )
            m_hooks.m_event( name, params );
    }
    void event_string( const char* name, const std::string& text )
    {
        ui_params_t p;
        p.add_string( text.c_str( ) );
        event( name, p );
    }
    void credits_changed( )
    {
        ui_params_t p;
        ui_params_add_int( p, m_value );
        ui_params_add_int( p, m_earned );
        event( "OnCreditsChanged", p );
    }
    // 0x47F7B0.
    void busy( bool value )
    {
        if ( m_hooks.m_screen_busy )
            m_hooks.m_screen_busy( value );
    }
    // 0x47F820.
    void trophy_enter( )
    {
        if ( m_tier <= 2 )
        {
            std::string id = "Award_", image = "UI/Award/Trophy/";
            if ( m_team )
            {
                id += "Team";
                const char* word = m_tier == 0 ? "Win" : "Lose";
                id += word;
                image += word;
            }
            else
            {
                if ( m_class == "A" )
                    id += "Expert";
                else if ( m_class == "B" )
                    id += "Pro";
                else if ( m_class == "C" )
                    id += "Novice";
                else if ( m_class == "S" )
                    id += "Special";
                const char* word = m_tier == 0 ? "Gold" : m_tier == 1 ? "Silver" : m_tier == 2 ? "Bronze" : "";
                id += word;
                image += word;
            }
            event_string( "OnSetTrophyStringID", id );
            event_string( "OnTrophyImageChange", image );
            event_string( "OnGenericEvent", "ShowTrophy" );
        }
        busy( false );
    }
    // 0x47FCD0.
    void credits_tick( float dt )
    {
        std::int32_t to = m_value;
        if ( m_earned >= to )
            return;
        m_elapsed = m_elapsed + dt;
        const std::int32_t curve = _mm_cvttss_si32( _mm_set_ss( m_elapsed * 250.0f * m_elapsed ) );
        if ( curve < to )
            to = curve;
        std::int32_t room = 0;
        if ( m_hooks.m_credits_room && m_hooks.m_credits_room( room ) )
        {
            const std::int32_t most = static_cast< std::int32_t >( static_cast< std::uint32_t >( m_earned ) + static_cast< std::uint32_t >( room ) );
            if ( to < most )
                ;
            else
                to = most;
        }
        if ( to / 50 != m_earned / 50 && m_hooks.m_play )
            m_hooks.m_play( "Global/UI_and_Menu/credits_tick_2" );
        if ( m_hooks.m_credits_add )
            m_hooks.m_credits_add( static_cast< std::int32_t >( static_cast< std::uint32_t >( to ) - static_cast< std::uint32_t >( m_earned ) ), m_flag );
        m_earned = to;
        credits_changed( );
        m_fsm.set_condition( "WaitForInput", true );
    }
    // 0x47FE40.
    void feats_tick( )
    {
        if ( m_feat < static_cast< std::int32_t >( m_names.size( ) ) )
        {
            if ( m_hooks.m_unlock )
                m_hooks.m_unlock( m_names[ static_cast< std::size_t >( m_feat ) ] );
            ++m_feat;
        }
    }
    // 0x47FE90.
    void unlock_enter( )
    {
        const ui_award_unlock_t unlock = std::move( m_queue.front( ) );
        m_queue.pop_front( );
        if ( m_hooks.m_play )
            m_hooks.m_play( "Global/UI_and_Menu/ui_credits_screen" );
        event_string( "OnAwardImageChange", unlock.m_image );
        event_string( "OnSetUnlockCatStringID", "Unlock_" + ( unlock.m_type.empty( ) ? unlock.m_mode : unlock.m_type ) );
        event_string( "OnSetUnlockInfoStringID", unlock.m_string_id );
        event_string( "OnGenericEvent", "HideTrophy" );
        m_fsm.set_condition( "WaitForInput", false );
        busy( false );
    }

    c_ui_common_screen m_common;
    ui_award_hooks_t m_hooks;
    c_fsm m_fsm;                            // +0x14
    bool m_team = false;                    // +0x3C
    std::int32_t m_tier = 0;                // +0x40
    std::string m_class;                    // +0x44
    std::int32_t m_value = 0;               // +0x5C the credits to award
    std::vector< std::string > m_names;     // +0x60
    std::int32_t m_flag = 0;                // +0x6C
    std::int32_t m_earned = 0;              // +0x70
    float m_elapsed = 0.0f;                 // +0x74
    std::int32_t m_feat = 0;                // +0x78
    std::deque< ui_award_unlock_t > m_queue;// +0x7C
    bool m_select = true;                   // +0x90 (the ctor's 1: a held Select is not a press)
};

// VuAwardScreen in the game: VuGame's Awards enter 0x484320 (+0x88) and the championship's 0x454C70 (+0x294). `m_award_hooks` of
// ui_game_award_setup_t is the world's (the game manager's credits and unlocks are platform hooks). The root's event 0x501260
// (the project's tree, each entity's map +0xC): c_ui_binding::post over the bound maps, ROUTING (verify_ui_champ_bind).
struct ui_game_award_t
{
    std::unique_ptr< entity_t > m_root;
    std::unique_ptr< c_ui_binding > m_binding;
    std::unique_ptr< c_ui_award_screen > m_screen;
};

struct ui_game_award_setup_t
{
    ui_award_hooks_t m_hooks;
};

// One per process, as the hooks of ui_game_screens_t are: set by the owner before the first award_open.
inline ui_game_award_setup_t& ui_game_award_setup( )
{
    static ui_game_award_setup_t setup;
    return setup;
}

// new 0x94, ctor 0x47ECC0 ( project ), 0x47F350. A project that does not load: the object still exists with a null root (every
// common slot empty), as Results' enter.
inline bool ui_game_screens_t::award_open( const char* project, std::int32_t tier, const std::string& boat_class, std::int32_t value, const std::vector< std::string >& names )
{
    auto award = std::make_shared< ui_game_award_t >( );
    award->m_root = m_load ? m_load( project ) : nullptr;
    ui_common_ops_t ops;
    ops.m_repository_add = m_repository_add;
    ops.m_repository_remove = m_repository_remove;
    ops.m_device = m_device;
    ui_award_hooks_t hooks = ui_game_award_setup( ).m_hooks;
    if ( award->m_root )
    {
        award->m_binding = std::make_unique< c_ui_binding >( *award->m_root, m_hooks );
        c_ui_binding* b = award->m_binding.get( );
        entity_t* root = award->m_root.get( );
        ops.m_initialize = [ b ] { b->project_initialize( ); };
        ops.m_release = [ b ] { b->project_release( ); };
        ops.m_tick = [ b ]( float dt ) { b->tick( dt, true ); };
        ops.m_draw = [ this, b, root ] { if ( m_frame ) m_frame( *root, *b ); };
        hooks.m_event = [ b ]( const char* name, const ui_params_t& p ) { b->post( fsm_hash( name ), p ); };
    }
    award->m_screen = std::make_unique< c_ui_award_screen >( award->m_root.get( ), std::move( ops ), std::move( hooks ) );
    m_award = award;
    m_award->m_screen->start( tier, boat_class, value, names );
    return true;
}

// verb 0 0x47F4C0, 1 its deletion, 2 its tick, 3 its draw.
inline void ui_game_screens_t::award_screen( int verb, float dt )
{
    if ( !m_award )
        return;
    if ( verb == 0 )
        m_award->m_screen->exit( );
    else if ( verb == 1 )
    {
        m_award->m_screen->close( );
        m_award.reset( );
    }
    else if ( verb == 2 )
        m_award->m_screen->tick( dt );
    else if ( verb == 3 )
        m_award->m_screen->draw( );
}

inline bool ui_game_screens_t::award_done( ) const { return m_award && m_award->m_screen->done( ); }

// The championship's award slots over the same object (standings.hxx ui_champ_env_t).
inline void ui_champ_award_bind( ui_champ_env_t& env, ui_game_screens_t& screens )
{
    ui_game_screens_t* g = &screens;
    env.m_award_open = [ g ]( const char* project, std::int32_t place, const std::string& cls, std::int32_t value, const std::vector< std::string >& names ) { g->award_open( project, place, cls, value, names ); };
    env.m_award_tick = [ g ]( float dt ) { g->award_screen( 2, dt ); };
    env.m_award_done = [ g ] { return g->award_done( ); };
    env.m_award_exit = [ g ] { g->award_screen( 0, 0.0f ); g->award_screen( 1, 0.0f ); };
    env.m_award_draw = [ g ] { g->award_screen( 3, 0.0f ); };
}
