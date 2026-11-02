#pragma once

// The message boxes (boat batch 35): VuMessageBox, VuMessageBoxManager (vtable 0x745C50's class, ctor 0x4B0EC0), the runtime half of
// VuMessageBoxScreenEntity and VuMessageBoxEntity's plugs. ui/message.hxx draws the box; this writes what its notes call INPUTS.
// Checked by case message_box (cases/msgbox.py): retail's ctor, 0x4B14D0, 0x4670F0, 0x464090 run on a hand screen.
//
// SUBSTITUTION: VuMessageBox's refcount (+4, 1 at 0x4B0BA0) is a std::shared_ptr: the queue's reference and the caller's are the same two.
// Platform hooks (msgbox_hooks_t): FMOD (play), the touch stack (focus: 0x53FB90 / 0x53FAE0 / 0x53FA60 push / priority / pop, g 0x759070).

#include <workspace/core/ui/fsm.hxx>
#include <workspace/core/ui/message.hxx>

#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

// 0x4B0DA0's defaults. +0 heading, +24 body, +48 text A, +72 text B, +96 texture: string ids / asset name.
struct msgbox_params_t
{
    std::string m_heading, m_body, m_text_a, m_text_b, m_texture;
    std::uint8_t m_flip = 0;                                        // +120
    float m_scale[ 2 ] = { 1.0f, 1.0f };                            // +124
    std::string m_sfx_a = "Global/UI_and_Menu/menu_browse";         // +132
    std::string m_sfx_b = "Global/UI_and_Menu/menu_browse";         // +156
    std::uint8_t m_pause = 0;                                       // +180 "Pause Game"
    std::uint8_t m_input = 1;                                       // +181: 0x4670F0's gate on the pad keys
    std::int32_t m_extra = 0;                                       // +184
};

struct msgbox_t;

// VuMessageBox::Callback: slot 0 (the result is in the box).
using msgbox_callback_t = std::function< void( msgbox_t& ) >;

// VuMessageBox (0xD8 bytes, 0x4B0BA0): +12 the params, +200 result, +204 destroyed, +208 callback.
struct msgbox_t
{
    msgbox_params_t m_params;
    std::int32_t m_result = 0;
    bool m_destroyed = false;
    msgbox_callback_t m_callback;
};

using msgbox_ref_t = std::shared_ptr< msgbox_t >;

struct msgbox_hooks_t
{
    std::function< std::string( const std::string& ) > text;        // g 0x759078 vt+0x1C, the string db's lookup
    std::function< std::string( ) > missing;                        // g 0x759078 vt+0x24, what a lookup answers for no string
    std::function< void( float ) > ui_tick;                         // 0x5011A0 "OnUITick"( dt ) over the screen's children
    std::function< void( const char* ) > ui_event;                  // 0x5011A0 "OnUIEvent"( name )
    std::function< bool( const char* ) > pressed;                   // g 0x759038 vt+0x24( 0, name )
    std::function< bool( ) > focus;                                 // the screen's touch entry holds g 0x759070 +0x1C
    std::function< void( const char* ) > play;                      // FMOD getEvent + start
    std::function< void( ) > pause;                                 // g 0x759008 vt+0x2C
    std::function< void( ) > resume;                                // g 0x759008 vt+0x30
    std::function< void( ) > project_on;                            // the project's repository add, then 0x500B10 of its root's other components
    std::function< void( ) > project_off;                           // 0x500B80's, then the repository's remove
    std::function< void( ) > project_ready;                         // 0x501260 "OnProjectInitialized"
    std::function< void( const std::string& ) > texture_load;       // 0x426A90 into +0x570
    std::function< void( ) > texture_free;                          // g 0x759004 vt+0x24( +0x570 )
};

inline constexpr const char* k_msgbox_browse = "Global/UI_and_Menu/menu_browse";    // off 0x746D48[ 0 ], 0x4D2D40( 0 )

// VuMessageBoxScreenEntity's runtime (+0x4B0 ..), beside the draw's ui_message_box_t.
struct msgbox_screen_t
{
    ui_message_box_t* m_box = nullptr;
    msgbox_params_t m_params;           // +1200
    float m_spin = 0.0f;                // +1132 "Animated Rotation Speed"
    std::int32_t m_result = 0;          // +1388
    std::uint8_t m_press_a = 0;         // +1396
    std::uint8_t m_press_b = 0;         // +1397
    std::uint8_t m_touch_moved = 0;     // +1404: the touch callback's (platform)
    std::uint8_t m_touch_down = 0;      // +1405
    std::uint8_t m_touch_up = 0;        // +1406
    float m_touch[ 2 ] = { };           // +1408
    bool m_on = false;                  // root +20 bit 2
};

// 0x466E40: the params in, the five blocks' enabled bytes and texts, the selection. Block 0's enabled byte is not written here.
inline void msgbox_screen_set( msgbox_screen_t& s, const msgbox_params_t& p, const msgbox_hooks_t& h )
{
    ui_message_box_t& b = *s.m_box;
    s.m_params = p;
    b.m_blocks[ 0 ].m_text = h.text( p.m_heading );
    b.m_blocks[ 1 ].m_text = h.text( p.m_body );
    b.m_blocks[ 1 ].m_enabled = p.m_texture.empty( ) ? 1 : 0;
    b.m_blocks[ 2 ].m_text = h.text( p.m_body );
    b.m_blocks[ 2 ].m_enabled = p.m_texture.empty( ) ? 0 : 1;
    b.m_blocks[ 3 ].m_enabled = p.m_text_a.empty( ) ? 0 : 1;
    b.m_blocks[ 3 ].m_text = h.text( p.m_text_a );
    b.m_blocks[ 4 ].m_enabled = p.m_text_b.empty( ) ? 0 : 1;
    b.m_blocks[ 4 ].m_text = h.text( p.m_text_b );
    s.m_press_a = 0;
    s.m_press_b = 0;
    b.m_rotation = 0.0f;
    // A body with no string shows its id; only block 1, the image's copy keeps the db's answer.
    if ( b.m_blocks[ 1 ].m_text == h.missing( ) )
        b.m_blocks[ 1 ].m_text = p.m_body;
    if ( b.m_blocks[ 3 ].m_enabled )
        b.m_button = 1;
    else if ( b.m_blocks[ 4 ].m_enabled )
        b.m_button = 3;
}

// 0x466C60, slot 6 (from 0x500B10): the result cleared, "MessageBoxOpen", the texture.
inline void msgbox_screen_on( msgbox_screen_t& s, const msgbox_hooks_t& h )
{
    s.m_result = 0;
    h.ui_event( "MessageBoxOpen" );
    if ( !s.m_params.m_texture.empty( ) )
        h.texture_load( s.m_params.m_texture );
    s.m_on = true;
}

// 0x466D80, slot 7 (from 0x500B80).
inline void msgbox_screen_off( msgbox_screen_t& s, const msgbox_hooks_t& h )
{
    h.ui_event( "MessageBoxClose" );
    h.texture_free( );
    s.m_on = false;
}

// 0x4670F0, the screen's tick.
inline void msgbox_screen_tick( msgbox_screen_t& s, float dt, const msgbox_hooks_t& h )
{
    ui_message_box_t& b = *s.m_box;
    h.ui_tick( dt );
    const float sx = 1.0f / b.m_screen_scale[ 0 ], sy = 1.0f / b.m_screen_scale[ 1 ];
    const float ax = sx * b.m_touch[ 0 ][ 0 ], ay = b.m_touch[ 0 ][ 1 ] * sy, aw = b.m_touch[ 0 ][ 2 ] * sx, ah = b.m_touch[ 0 ][ 3 ] * sy;
    const float bx = sx * b.m_touch[ 1 ][ 0 ], by = b.m_touch[ 1 ][ 1 ] * sy, bw = b.m_touch[ 1 ][ 2 ] * sx, bh = b.m_touch[ 1 ][ 3 ] * sy;
    const float tx = s.m_touch[ 0 ], ty = s.m_touch[ 1 ];
    const bool out_a = tx < ax || aw + ax < tx || ty < ay || ah + ay < ty;
    const bool out_b = tx < bx || bw + bx < tx || ty < by || bh + by < ty;
    if ( s.m_touch_moved )
    {
        if ( b.m_blocks[ 3 ].m_enabled && tx >= ax && aw + ax >= tx && ty >= ay && ah + ay >= ty )
            b.m_button = 2;
        else if ( b.m_blocks[ 4 ].m_enabled && tx >= bx && bw + bx >= tx && ty >= by && bh + by >= ty )
            b.m_button = 4;
    }
    if ( b.m_button == 2 )
    {
        if ( s.m_touch_up )
        {
            s.m_press_a = 1;
            b.m_button = 1;
        }
        if ( out_a )
            b.m_button = 1;
    }
    if ( b.m_button == 4 )
    {
        if ( s.m_touch_up )
        {
            s.m_press_b = 1;
            b.m_button = 3;
        }
        if ( out_b )
            b.m_button = 3;
    }
    if ( s.m_touch_up )
    {
        s.m_touch_down = 0;
        s.m_touch_up = 0;
    }
    s.m_touch_moved = 0;
    if ( s.m_params.m_input && h.focus( ) )
    {
        bool select = h.pressed( "Select" );
        const bool left = h.pressed( "Left" ) || h.pressed( "Left2" );
        const bool right = h.pressed( "Right" ) || h.pressed( "Right2" );
        if ( b.m_button == 1 )
        {
            if ( select )
                s.m_press_a = 1;
            else if ( left && b.m_blocks[ 4 ].m_enabled )
            {
                b.m_button = 3;
                h.play( k_msgbox_browse );
                select = false;
            }
        }
        if ( b.m_button == 3 )
        {
            if ( select )
                s.m_press_b = 1;
            else if ( right && b.m_blocks[ 3 ].m_enabled )
            {
                b.m_button = 1;
                h.play( k_msgbox_browse );
            }
        }
    }
    if ( s.m_press_a )
    {
        h.play( s.m_params.m_sfx_a.c_str( ) );
        s.m_result = 1;
        s.m_press_a = 0;
    }
    if ( s.m_press_b )
    {
        h.play( s.m_params.m_sfx_b.c_str( ) );
        s.m_result = 2;
        s.m_press_b = 0;
    }
    const float turned = s.m_spin * dt + b.m_rotation;
    b.m_rotation = static_cast< float >( std::fmod( static_cast< double >( turned ), 6.283185482025146 ) );
}

// VuMessageBoxManager. m_screen is the project's root (slot 5 0x4B11F0: "Screens/MessageBox", which must hold a VuMessageBoxScreenEntity).
class c_msgbox_manager
{
public:
    // ctor 0x4B0EC0's machine; slot 5's end: the first state, its enter (Inactive has none), no time reset.
    void init( msgbox_screen_t* screen, const msgbox_hooks_t& hooks )
    {
        m_screen = screen;
        m_hooks = hooks;
        m_fsm.add_state( "Inactive" ).m_tick = [ this ]( float ) { inactive( ); };
        fsm_state_t& in = m_fsm.add_state( "FadeIn" );
        in.m_enter = [ this ] { fade_in_enter( ); };
        in.m_tick = [ this ]( float dt ) {
            m_fade += dt * 8.0f;
            if ( m_fade > 1.0f )
            {
                m_fade = 1.0f;
                m_fsm.pulse_condition( "FadeInComplete" );
            }
        };
        m_fsm.add_state( "Active" ).m_tick = [ this ]( float dt ) {
            msgbox_screen_tick( *m_screen, dt, m_hooks );
            if ( m_active->m_destroyed )
                m_fsm.pulse_condition( "MessageBoxDestroyed" );
            if ( m_screen->m_result )
                m_fsm.pulse_condition( "MessageBoxResult" );
        };
        fsm_state_t& out = m_fsm.add_state( "FadeOut" );
        out.m_enter = [ this ] {
            const std::int32_t r = m_screen->m_result;
            if ( r && !m_active->m_destroyed )
            {
                m_active->m_result = r;
                if ( m_active->m_callback )
                    m_active->m_callback( *m_active );
            }
        };
        out.m_tick = [ this ]( float dt ) {
            m_fade -= dt * 8.0f;
            if ( m_fade < 0.0f )
            {
                m_fade = 0.0f;
                m_fsm.pulse_condition( "FadeOutComplete" );
            }
        };
        out.m_exit = [ this ] { finish( ); };
        m_fsm.add_transition( "Inactive", "FadeIn", "MessageBoxQueued" );
        m_fsm.add_transition( "FadeIn", "Active", "FadeInComplete" );
        m_fsm.add_transition( "Active", "FadeOut", "MessageBoxDestroyed" );
        m_fsm.add_transition( "Active", "FadeOut", "MessageBoxResult" );
        m_fsm.add_transition( "FadeOut", "Inactive", "FadeOutComplete" );
        m_fsm.start( );
    }

    // 0x4B1430: the caller holds a reference (release = forget it; retail's own callers then set +0xD0 first).
    msgbox_ref_t create( const msgbox_params_t& p )
    {
        auto box = std::make_shared< msgbox_t >( );
        box->m_params = p;
        m_queue.push_back( box );
        return box;
    }

    const std::deque< msgbox_ref_t >& queue( ) const { return m_queue; }

    // 0x4B14D0 "Decision", dt = the tick manager's real dt (vt+0x3C).
    void tick( float dt )
    {
        m_fsm.evaluate( );
        if ( fsm_state_t* s = m_fsm.current( ) )
            if ( s->m_tick )
                s->m_tick( dt );
        m_fsm.m_time += dt;
    }

    // Slot 3 0x4B1340's game half: the current state's exit, the queue dropped, 0x4B1540.
    void release( )
    {
        if ( fsm_state_t* s = m_fsm.current( ) )
            if ( s->m_exit )
                s->m_exit( );
        m_queue.clear( );
        finish( );
    }

    // 0x4B1520: while a box is active, 0x4675A0( fade ).
    bool drawing( ) const { return m_active != nullptr; }
    float fade( ) const { return m_fade; }
    const c_fsm& fsm( ) const { return m_fsm; }
    std::size_t queued( ) const { return m_queue.size( ); }

private:
    // 0x4B15A0: boxes destroyed while queued are dropped; one left raises the condition.
    void inactive( )
    {
        while ( !m_queue.empty( ) && m_queue.front( )->m_destroyed )
            m_queue.pop_front( );
        if ( !m_queue.empty( ) )
            m_fsm.pulse_condition( "MessageBoxQueued" );
    }

    // 0x4B1670.
    void fade_in_enter( )
    {
        m_active = m_queue.front( );
        m_queue.pop_front( );
        msgbox_screen_set( *m_screen, m_active->m_params, m_hooks );
        if ( m_active->m_params.m_pause )
            m_hooks.pause( );
        if ( !m_screen->m_on )
        {
            m_hooks.project_on( );
            msgbox_screen_on( *m_screen, m_hooks );
            m_hooks.project_ready( );
        }
        m_hooks.play( "Global/UI_and_Menu/message_box_show" );
    }

    // 0x4B1540.
    void finish( )
    {
        if ( !m_active )
            return;
        if ( m_active->m_params.m_pause )
            m_hooks.resume( );
        if ( m_screen->m_on )
        {
            msgbox_screen_off( *m_screen, m_hooks );
            m_hooks.project_off( );
        }
        m_active.reset( );
    }

    c_fsm m_fsm;
    std::deque< msgbox_ref_t > m_queue;     // +0x38 ..
    msgbox_ref_t m_active;                  // +0x34
    float m_fade = 0.0f;                    // +0x4C
    msgbox_screen_t* m_screen = nullptr;    // +0x30
    msgbox_hooks_t m_hooks;
};

// VuMessageBoxEntity (ctor 0x463840; its properties are ui/schema.hxx's): the params at +0x64, the held box +0x120, the callback at +0x5C.
struct msgbox_entity_t
{
    msgbox_params_t m_params;
    msgbox_ref_t m_box;
    std::function< void( const char* ) > m_fire;    // an output plug by name (0x4xxxxx findPlug, then vt+8)
};

// 0x464110, the callback: "OnA" on result 1, "OnB" on 2; then the box let go, marked destroyed.
inline void msgbox_entity_result( msgbox_entity_t& e, const msgbox_t& box )
{
    if ( box.m_result == 1 )
        e.m_fire( "OnA" );
    else if ( box.m_result == 2 )
        e.m_fire( "OnB" );
    if ( e.m_box )
    {
        e.m_box->m_destroyed = true;
        e.m_box.reset( );
    }
}

// 0x464090 plug "Create": nothing while one is held.
inline void msgbox_entity_create( msgbox_entity_t& e, c_msgbox_manager& m )
{
    if ( e.m_box )
        return;
    e.m_box = m.create( e.m_params );
    e.m_box->m_callback = [ &e ]( msgbox_t& b ) { msgbox_entity_result( e, b ); };
}

// 0x4640D0 plug "Destroy".
inline void msgbox_entity_destroy( msgbox_entity_t& e )
{
    if ( e.m_box )
    {
        e.m_box->m_destroyed = true;
        e.m_box.reset( );
    }
}

// VuControlMethodManager's message box (g 0x758FC0; ctor 0x4AE6E0 registers the event, boat batch 36, case gamepad_box).
// "OnGamePadDisconnected" 0x4AEA00: the params' int (0x4237B0: read only behind an int tag 0; any other tag answers 0) zero, no box
// open and the method (+0x18) the pad's 0: the box, its callback slot 0 0x4AEC30 clearing +0x1C, +0x1C = 1 and "OnPauseActionGame".
// NOT WIRED: the event bus delivers the event, and it comes from the platform's device (a pad lost).
struct control_method_box_t
{
    std::uint8_t m_open = 0;    // +0x1C
};

inline void control_method_pad_lost( control_method_box_t& e, const std::uint8_t* params, std::uint32_t size, std::int32_t method, c_msgbox_manager& m,
                                     const std::function< void( const char* ) >& broadcast )
{
    std::int32_t value = 0, tag = -1;
    if ( static_cast< std::int32_t >( size ) > 0 )
    {
        std::memcpy( &tag, params, 4 );
        if ( tag == 0 )
            std::memcpy( &value, params + 4, 4 );
    }
    if ( e.m_open || value != 0 || method != 0 )
        return;
    msgbox_params_t p;
    p.m_heading = "GamePadDisconnected_Heading";
    p.m_body = "GamePadDisconnected_Body";
    p.m_text_a = "MessageBox_OK";
    msgbox_ref_t box = m.create( p );
    box->m_callback = [ &e ]( msgbox_t& ) { e.m_open = 0; };
    e.m_open = 1;
    broadcast( "OnPauseActionGame" );
}
