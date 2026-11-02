#pragma once

// VuGameModeManagerImpl's Decision tick 0x4CBFD0 (world batch 37): the game mode change. +0xC the current name, +0x24 the next, +0x3C the
// mode. Verified against retail: checks/hudpass.hxx (case mode_switch). The factory 0x4CC460 is the caller's: of its names only
// "FrontEnd" (0x4BEEA0) and "Action" (0x4B9160) have a recreation; the others answer null, which retail's own factory does for a
// name it does not know: no mode, no enter.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// VuGameMode's slots.
struct game_mode_t
{
    virtual ~game_mode_t( ) = default;                        // slot 1 ( 1 ): the deletion
    virtual bool enter( const std::string& previous ) = 0;    // slot 2
    virtual void exit( ) = 0;                                 // slot 3
    virtual const std::string& tick( float dt ) = 0;          // slot 4
    virtual void draw( ) = 0;                                 // slot 5
};

struct mode_manager_t
{
    std::string m_current;                                                         // +0xC
    std::string m_next;                                                            // +0x24
    std::unique_ptr< game_mode_t > m_mode;                                         // +0x3C
    std::function< std::unique_ptr< game_mode_t >( const std::string& ) > m_factory;   // 0x4CC460
    std::function< void( ) > m_sync;      // a frame pending on the render thread: g 0x759098 vt+0x3C( event, -1 ), device vt+0x30, vt+0xD0
    std::function< void( ) > m_refused;   // slot 2 answered false: g 0x759000 vt+0x20 ( 0x6BDEE4 ): the system's error

    // The change half of 0x4CBFD0: with a next name, the sync, the old mode's slot 3 and its deletion, the factory, slot 2 ( the previous
    // name ), the current name = the next, the next cleared.
    void change( )
    {
        if ( m_next.empty( ) )
            return;
        if ( m_sync )
            m_sync( );
        if ( m_mode )
        {
            m_mode->exit( );
            m_mode.reset( );
        }
        m_mode = m_factory ? m_factory( m_next ) : nullptr;
        if ( m_mode && !m_mode->enter( m_current ) && m_refused )
            m_refused( );
        m_current = m_next;
        m_next.clear( );
    }

    // 0x4CBFD0: the change, then slot 4, whose answer is the next name (an empty text clears it).
    void tick( float dt )
    {
        change( );
        if ( m_mode )
            m_next = m_mode->tick( dt );
    }

    // 0x4CC0F0: the sync 0x4CC110 (a display check, unread) then slot 5.
    void draw( )
    {
        if ( m_mode )
            m_mode->draw( );
    }
};
