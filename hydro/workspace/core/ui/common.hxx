#pragma once

#include <workspace/core/ui/bind.hxx>
#include <workspace/core/ui/frame.hxx>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

// VuCommonScreen (vtable 0x73F588, 0x10 bytes: +4 an event map 0x515800, +8 0, +0xC
// the VuProject of 0x4B4360, its +0xC the root entity), the object VuGame's Results
// enter 0x484110 builds. Transliterated from the disassembly (tools tmp/b28/d_480950.txt):
//   slot 0 0x480950  scalar deleting dtor -> 0x480980: the device block (a HOOK), the project's
//                    reference (+4 - 1, slot 0(1) at zero), the event map
//   slot 1 0x480A50  open: when the root's +0x14 bit 2 is clear: the repository add g 0x759080
//                    vt+0x18, gameInitialize 0x500B10, "OnProjectInitialized" 0x501260
//   slot 2 0x480AB0  exit: when bit 2 is set: gameRelease 0x500B80, the repository remove vt+0x1C
//   slot 3 0x480AE0  tick( dt ): the root's vt+0x30( dt, 0xF ) when it is a VuUIScreenEntity
//   slot 4 0x480B40  draw: the root's vt+0x34( 1.0 ) when it is a VuUIScreenEntity
// The IS-A walk: the root's slot 1 RTTI chain looking for 0x736E88.
// Device and repository calls are hooks (ui_common_ops_t); the root's slots are the
// binding's in the game (ui_common_ops_of) and a recording replay in the check.

// The root's RTTI chain holds VuUIScreenEntity 0x736E88. SUBSTITUTION for the walk: the two vtables among the
// project roots that pass it (0x6F7140, the derived 0x73DAA8); every Screens/* and HUDs/* root is compared with
// retail by verify_ui_common_screen.
inline bool ui_common_is_screen( const entity_t& root )
{
    return root.m_ctor && ( root.m_ctor->m_vtable == k_ui_screen_vtable || root.m_ctor->m_vtable == 0x73DAA8u );
}

struct ui_common_ops_t
{
    std::function< void( ) > m_repository_add;      // g 0x759080 vt+0x18 (a HOOK)
    std::function< void( ) > m_repository_remove;   // g 0x759080 vt+0x1C (a HOOK)
    std::function< void( ) > m_initialize;          // 0x500B10 + 0x501260 "OnProjectInitialized"
    std::function< void( ) > m_release;             // 0x500B80
    std::function< void( float dt ) > m_tick;       // the root's vt+0x30( dt, 0xF ): input allowed
    std::function< void( ) > m_draw;                // the root's vt+0x34( 1.0 )
    std::function< void( ) > m_device;              // 0x480980's g 0x759074 block (+0xC set): the D3D layer's (a HOOK)
};

class c_ui_common_screen
{
public:
    // `root` null: 0x4B4360 gave no project / no root; every slot does nothing.
    c_ui_common_screen( const entity_t* root, ui_common_ops_t ops, bool initialized = false )
        : m_root( root ), m_ops( std::move( ops ) ), m_initialized( initialized )
    {
    }

    // slot 1 0x480A50.
    void open( )
    {
        if ( !m_root || m_initialized )
            return;
        if ( m_ops.m_repository_add )
            m_ops.m_repository_add( );
        if ( m_ops.m_initialize )
            m_ops.m_initialize( );
        m_initialized = true;   // 0x500B10's +0x14 |= 4
    }
    // slot 2 0x480AB0.
    void exit( )
    {
        if ( !m_root || !m_initialized )
            return;
        if ( m_ops.m_release )
            m_ops.m_release( );
        m_initialized = false;
        if ( m_ops.m_repository_remove )
            m_ops.m_repository_remove( );
    }
    // slot 3 0x480AE0.
    void tick( float dt )
    {
        if ( m_root && ui_common_is_screen( *m_root ) && m_ops.m_tick )
            m_ops.m_tick( dt );
    }
    // slot 4 0x480B40.
    void draw( )
    {
        if ( m_root && ui_common_is_screen( *m_root ) && m_ops.m_draw )
            m_ops.m_draw( );
    }
    // 0x480980's device block, before the project's reference goes.
    void close( )
    {
        if ( m_root && m_ops.m_device )
            m_ops.m_device( );
    }
    bool initialized( ) const { return m_initialized; }

private:
    const entity_t* m_root;
    ui_common_ops_t m_ops;
    bool m_initialized;
};

// What the world needs for vugame_screen_hooks_t's Results path (world/rules.hxx):
//   results_open        -> ui_game_screens_t::results_open
//   screen( 0, verb, dt ) -> ui_game_screens_t::screen
// m_load is 0x4B4360 (VuProject::load of the asset through the UI's world: the
// world's archive, as huds.hxx does with ui_hud_load); m_hooks the binding's
// environment; m_frame the render pass (null until the race has a UI pass).
struct ui_game_screens_t
{
    std::function< std::unique_ptr< entity_t >( const std::string& project ) > m_load;
    ui_bind_hooks_t m_hooks;
    std::function< void( ) > m_repository_add, m_repository_remove, m_device;
    std::function< void( const entity_t& root, c_ui_binding& binding ) > m_frame;

    std::unique_ptr< entity_t > m_root;
    std::unique_ptr< c_ui_binding > m_binding;
    std::unique_ptr< c_ui_common_screen > m_results;

    // Results' enter 0x484110: new, 0x4B4360 on the project, slot 1. True: +0x84 set (the object always exists in retail
    // - a project that does not load leaves its +0xC null and every slot empty).
    bool results_open( const char* project )
    {
        m_root = m_load ? m_load( project ) : nullptr;
        ui_common_ops_t ops;
        ops.m_repository_add = m_repository_add;
        ops.m_repository_remove = m_repository_remove;
        ops.m_device = m_device;
        if ( m_root )
        {
            m_binding = std::make_unique< c_ui_binding >( *m_root, m_hooks );
            c_ui_binding* b = m_binding.get( );
            entity_t* root = m_root.get( );
            ops.m_initialize = [ b ] { b->project_initialize( ); };   // the root's slot 6 is ScreenEnter 0x629A70
            ops.m_release = [ b ] { b->project_release( ); };
            ops.m_tick = [ b ]( float dt ) { b->tick( dt, true ); };
            ops.m_draw = [ this, b, root ] { if ( m_frame ) m_frame( *root, *b ); };
        }
        m_results = std::make_unique< c_ui_common_screen >( m_root.get( ), std::move( ops ) );
        m_results->open( );
        return true;
    }
    // vugame_screen_hooks_t::screen for the results object (screen 0): verb 0 its slot 2, 1 its deletion, 2 its tick, 3 its draw.
    void screen( int verb, float dt )
    {
        if ( !m_results )
            return;
        if ( verb == 0 )
            m_results->exit( );
        else if ( verb == 1 )
        {
            m_results->close( );
            m_results.reset( );
            m_binding.reset( );
            m_root.reset( );
        }
        else if ( verb == 2 )
            m_results->tick( dt );
        else if ( verb == 3 )
            m_results->draw( );
    }
};
