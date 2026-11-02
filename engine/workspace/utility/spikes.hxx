#pragma once

#include <workspace/utility/trace.hxx>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// OURS, not retail: a frame's time by named scope, for finding what causes the worst frames. Off (the default) a scope is one
// test of a bool. --spike-log FILE turns it on: every frame keeps each scope's SELF time (children taken off) and a few counters
// in memory, and the file is written when the run ends. Nothing here reads or changes game state.
// The allocation counters stay 0 unless the build defines HYDRO_SPIKE_ALLOC (hydro/main.cxx: a counting operator new).
namespace spikes
{
    enum slot_t : std::uint32_t
    {
        // The 17 tick phases, in tick_manager_init's order (tick_run adds the index).
        k_phase, k_phase_last = k_phase + 16,
        k_fleet, k_boat_decision, k_boat_build, k_physics, k_water_tick, k_water_grid, k_water_stage, k_skin, k_pfx_tick, k_pfx_emit, k_corona, k_game_rest,
        k_commands, k_flip, k_sort, k_walk_other, k_walk_mirror, k_walk_scene, k_walk_water, k_walk_pfx, k_walk_ui, k_walk_post, k_blit,
        k_ui, k_ui_end, k_present, k_create, k_read, k_texture_load,
        k_slots
    };
    inline constexpr const char* k_names[ k_slots ] = {
        "DynamicsSync", "Network", "Input", "Services", "PreDecision", "Decision", "PostDecision", "Triggers", "Viewports", "Motion", "Water", "Anim", "Corona", "DynamicsKick", "Build", "Final", "Audio",
        "ai_fleet", "boat_decision", "boat_build", "physics", "water_tick", "water_grid", "water_stage", "skin", "pfx_tick", "pfx_emit", "corona_scene", "game_rest",
        "commands", "flip", "sort", "walk_other", "walk_mirror", "walk_scene", "walk_water", "walk_pfx", "walk_ui", "walk_post", "blit",
        "ui", "ui_end", "present", "create", "read", "texture_load" };

    enum counter_t : std::uint32_t { c_allocs, c_alloc_bytes, c_alloc_big, c_creates, c_reads, c_read_bytes, c_substeps, c_manifolds, c_explosions, c_pfx_created, c_textures, c_draws, c_water_vertices, c_counters };
    inline constexpr const char* k_counters[ c_counters ] = { "allocs", "alloc_bytes", "alloc_big", "creates", "reads", "read_bytes", "substeps", "manifolds", "explosions", "pfx_created", "textures", "draws", "water_vertices" };

    struct row_t
    {
        float ms[ k_slots ];
        std::uint32_t count[ c_counters ];
        std::uint32_t allocs[ k_slots ];
    };

    struct state_t
    {
        bool on = false;
        std::uint32_t current = k_slots;   // the innermost scope, for the allocation counter
        double child = 0.0;                // what the running scope's children took
        row_t now{ };
        std::vector< row_t > rows;
    };
    inline state_t g_state;

    inline void count( counter_t c, std::uint32_t n = 1u )
    {
        if ( g_state.on )
            g_state.now.count[ c ] += n;
    }

    inline void configure( bool on )
    {
        g_state.on = on;
        if ( on )
            g_state.rows.reserve( 40000 );
    }

    struct scope_t
    {
        explicit scope_t( std::uint32_t slot ) : m_slot( slot )
        {
            state_t& s = g_state;
            m_live = s.on;
            if ( !m_live )
                return;
            m_last = s.current;
            m_child = s.child;
            s.current = slot;
            s.child = 0.0;
            m_first = m_start = trace::now( );
        }
        // The walk runs through the passes: what ran so far is the old slot's, the rest the new one's.
        void retarget( std::uint32_t slot )
        {
            state_t& s = g_state;
            if ( !m_live || slot == m_slot )
                return;
            const double now = trace::now( );
            s.now.ms[ m_slot ] += static_cast< float >( now - m_start - s.child );
            s.child = 0.0;
            s.current = slot;
            m_slot = slot;
            m_start = now;
        }
        ~scope_t( )
        {
            if ( !m_live )
                return;
            state_t& s = g_state;
            const double now = trace::now( );
            s.now.ms[ m_slot ] += static_cast< float >( now - m_start - s.child );
            s.current = m_last;
            s.child = m_child + ( now - m_first );   // the parent's children: this scope's whole span
        }
        std::uint32_t m_slot;
        std::uint32_t m_last = k_slots;
        bool m_live = false;
        double m_child = 0.0;
        double m_first = 0.0;
        double m_start = 0.0;
    };

    // A named one-off (an asset read, a resource made): the frame it happened on, with its cost. Stored, not printed.
    struct note_t
    {
        std::uint32_t frame;
        std::uint32_t bytes;
        float ms;
        char name[ 64 ];
    };
    inline std::vector< note_t > g_notes;
    inline void note( const char* name, std::uint32_t bytes, double ms )
    {
        state_t& s = g_state;
        if ( !s.on )
            return;
        note_t n{ static_cast< std::uint32_t >( s.rows.size( ) ), bytes, static_cast< float >( ms ), { } };
        std::snprintf( n.name, sizeof( n.name ), "%s", name != nullptr ? name : "" );
        g_notes.push_back( n );
    }

    // After present.
    inline void frame_end( )
    {
        state_t& s = g_state;
        if ( !s.on )
            return;
        s.rows.push_back( s.now );
        s.now = row_t{ };
    }

    // Binary-free: one header line, then one line a frame (ms x slots, counters, allocations by slot).
    inline void report( const char* path )
    {
        state_t& s = g_state;
        s.on = false;
        FILE* out = nullptr;
        if ( path == nullptr || fopen_s( &out, path, "w" ) != 0 || !out )
            return;
        for ( const char* n : k_names )
            std::fprintf( out, "%s ", n );
        for ( const char* n : k_counters )
            std::fprintf( out, "#%s ", n );
        for ( const char* n : k_names )
            std::fprintf( out, "@%s ", n );
        std::fprintf( out, "\n" );
        for ( const row_t& r : s.rows )
        {
            for ( const float v : r.ms )
                std::fprintf( out, "%.4f ", v );
            for ( const std::uint32_t v : r.count )
                std::fprintf( out, "%u ", v );
            for ( const std::uint32_t v : r.allocs )
                std::fprintf( out, "%u ", v );
            std::fprintf( out, "\n" );
        }
        std::fclose( out );
        if ( fopen_s( &out, ( std::string( path ) + ".notes" ).c_str( ), "w" ) == 0 && out )
        {
            for ( const note_t& n : g_notes )
                std::fprintf( out, "%u %u %.3f %s\n", n.frame, n.bytes, n.ms, n.name );
            std::fclose( out );
        }
    }
}
