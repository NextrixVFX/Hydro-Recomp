#pragma once

#include <workspace/utility/logger.hxx>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

// OURS, not retail: what a frame asks of the device, per pass, against the draw call budget of the WebGL target. Plain integer
// increments at the device seam (gfx_direct_t, the composer's blit), always on; the per-frame sums and the report are only
// live with --draw-stats. Nothing here reads or changes what is drawn.
namespace drawstats
{
    enum pass_t : std::uint32_t { k_other, k_mirror, k_scene, k_water, k_pfx, k_ui, k_post, k_passes };
    inline constexpr const char* k_names[ k_passes ] = { "other", "mirror", "scene", "water", "pfx", "ui", "post" };

    struct counts_t
    {
        std::uint32_t draws = 0;       // Draw + DrawIndexed
        std::uint32_t shaders = 0;     // VS + PS binds
        std::uint32_t textures = 0;    // PS view binds
        std::uint32_t constants = 0;   // Map (constant and dynamic buffers) + UpdateSubresource
        std::uint32_t targets = 0;     // OMSetRenderTargets
        std::uint32_t states = 0;      // blend, depth, raster binds
        std::uint64_t indices = 0;     // vertices or indices drawn
    };

    struct state_t
    {
        pass_t pass = k_other;
        counts_t now[ k_passes ];
    };
    inline state_t g_state;

    inline counts_t& at( ) { return g_state.now[ g_state.pass ]; }
    inline void set( pass_t pass ) { g_state.pass = pass; }

    struct scope_t
    {
        explicit scope_t( pass_t pass ) : m_last( g_state.pass ) { g_state.pass = pass; }
        ~scope_t( ) { g_state.pass = m_last; }
        pass_t m_last;
    };

    struct session_t
    {
        bool on = false;
        std::uint64_t frames = 0;
        std::uint64_t sum[ k_passes ][ 7 ] = { };
        std::uint32_t peak[ k_passes ] = { };          // each pass's own peak draws
        std::uint32_t peak_frame_draws = 0;
        std::uint64_t peak_at = 0;
        std::uint32_t peak_split[ k_passes ] = { };    // the worst frame's draws by pass
        std::uint32_t over_1500 = 0, over_2500 = 0;
        std::vector< std::uint32_t > by_second;        // the worst total of every 60 frames
    };
    inline session_t g_session;

    inline void configure( bool on )
    {
        g_session.on = on;
        if ( on )
            g_session.by_second.reserve( 4096 );
    }

    inline std::uint32_t total_draws( )
    {
        std::uint32_t total = 0;
        for ( const counts_t& c : g_state.now )
            total += c.draws;
        return total;
    }

    // After present.
    inline void frame_end( )
    {
        session_t& s = g_session;
        if ( s.on )
        {
            std::uint32_t total = 0;
            for ( int p = 0; p < k_passes; ++p )
            {
                const counts_t& c = g_state.now[ p ];
                std::uint64_t* row = s.sum[ p ];
                row[ 0 ] += c.draws;
                row[ 1 ] += c.shaders;
                row[ 2 ] += c.textures;
                row[ 3 ] += c.constants;
                row[ 4 ] += c.targets;
                row[ 5 ] += c.states;
                row[ 6 ] += c.indices;
                s.peak[ p ] = ( std::max )( s.peak[ p ], c.draws );
                total += c.draws;
            }
            if ( total > s.peak_frame_draws )
            {
                s.peak_frame_draws = total;
                s.peak_at = s.frames;
                for ( int p = 0; p < k_passes; ++p )
                    s.peak_split[ p ] = g_state.now[ p ].draws;
            }
            s.over_1500 += total > 1500u ? 1u : 0u;
            s.over_2500 += total > 2500u ? 1u : 0u;
            if ( s.frames % 60u == 0u )
                s.by_second.push_back( 0u );
            s.by_second.back( ) = ( std::max )( s.by_second.back( ), total );
            ++s.frames;
        }
        for ( counts_t& c : g_state.now )
            c = counts_t{ };
        g_state.pass = k_other;
    }

    // `path` null: the log only.
    inline void report( const char* path )
    {
        const session_t& s = g_session;
        if ( !s.on || s.frames == 0u )
            return;
        const double n = static_cast< double >( s.frames );
        FILE* file = nullptr;
        if ( path != nullptr && fopen_s( &file, path, "w" ) != 0 )
            file = nullptr;
        double total[ 7 ] = { };
        for ( int p = 0; p < k_passes; ++p )
            for ( int k = 0; k < 7; ++k )
                total[ k ] += static_cast< double >( s.sum[ p ][ k ] ) / n;
        const auto line = [ & ]( const char* text ) {
            LOG_INFO( "%s", text );
            if ( file != nullptr )
                std::fprintf( file, "%s\n", text );
        };
        char text[ 256 ];
        std::snprintf( text, sizeof( text ), "drawstats frames %llu avg_draws %.1f peak_draws %u peak_frame %llu over1500 %u over2500 %u", static_cast< unsigned long long >( s.frames ), total[ 0 ],
                       s.peak_frame_draws, static_cast< unsigned long long >( s.peak_at ), s.over_1500, s.over_2500 );
        line( text );
        for ( int p = 0; p < k_passes; ++p )
        {
            const std::uint64_t* r = s.sum[ p ];
            std::snprintf( text, sizeof( text ), "drawstats %-6s draws %.1f peak %u at_worst %u shaders %.1f textures %.1f constants %.1f targets %.1f states %.1f indices %.0f", k_names[ p ],
                           static_cast< double >( r[ 0 ] ) / n, s.peak[ p ], s.peak_split[ p ], static_cast< double >( r[ 1 ] ) / n, static_cast< double >( r[ 2 ] ) / n, static_cast< double >( r[ 3 ] ) / n,
                           static_cast< double >( r[ 4 ] ) / n, static_cast< double >( r[ 5 ] ) / n, static_cast< double >( r[ 6 ] ) / n );
            line( text );
        }
        std::snprintf( text, sizeof( text ), "drawstats total  shaders %.1f textures %.1f constants %.1f targets %.1f states %.1f indices %.0f", total[ 1 ], total[ 2 ], total[ 3 ], total[ 4 ], total[ 5 ], total[ 6 ] );
        line( text );
        if ( file != nullptr )
        {
            std::fprintf( file, "by60" );
            for ( const std::uint32_t v : s.by_second )
                std::fprintf( file, " %u", v );
            std::fprintf( file, "\n" );
            std::fclose( file );
        }
    }
}
