#pragma once

#include <workspace/core/world/world.hxx>
#include <chrono>
#include <cstdlib>

// SCAFFOLD - ours, no retail counterpart.
//
// Retail drives frames through VuGameModeManagerImpl and a front end built out
// of VuUI entities. Neither has been transliterated, so this is a deliberately
// thin stand-in whose only job is to load a project and advance the subsystems
// that HAVE been recovered 1:1. It is not a recreation of anything and must
// never be counted as one.
//
// What it does NOT do, because the code that did it was written from
// plausibility and has been deleted: no boat, no handling, no AI, no wake, no
// menus, no water surface assembly, no scene dressing.
class c_game : public c_draw
{
public:
    c_game( )
    {
        m_previous = std::chrono::steady_clock::now( );
        // --fixed-step <seconds> replaces the measured frame time with a
        // constant. Without it a capture is not reproducible: the ocean phase
        // and the animations advance by whatever the frame took, so the same
        // nominal moment lands on a different world state every run.
        if ( const char* step = c_engine::option( "fixed-step" ) )
            m_step = static_cast< float >( std::atof( step ) );
    }

    void tick( ) override
    {
        auto now = std::chrono::steady_clock::now( );
        float dt = m_step > 0.0f ? m_step : std::chrono::duration< float >( now - m_previous ).count( );
        m_previous = now;
        if ( dt < 0.0f )
            dt = 0.0f;
        if ( dt > 0.1f )
            dt = 0.1f;

        m_seconds += dt;
        c_engine::clock( m_seconds );

        if ( !m_world.loaded( ) )
        {
            const char* track = c_engine::option( "race" );
            m_world.load_scene( track != nullptr && track[ 0 ] != '\0' ? track : "Hydroelectric",
                c_engine::option( "mode" ), c_engine::option( "course" ) );
        }

        const spikes::scope_t spike( spikes::k_game_rest );
        m_world.scene_frame( dt );
        c_engine::camera( m_world.eye( ), m_world.target( ), m_world.far_plane( ) );
    }

private:
    c_world m_world{};
    std::chrono::steady_clock::time_point m_previous{};
    // Zero means use the real frame time; anything positive pins it.
    float m_step = 0.0f;
    float m_seconds = 0.0f;
};
