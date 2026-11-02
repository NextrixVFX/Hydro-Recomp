#pragma once

#include <workspace/utility/logger.hxx>

namespace trace
{
    inline double now( )
    {
#if HYDRO_HOST_WINDOWS
        static double scale = 0.0;
        if ( scale == 0.0 )
        {
            LARGE_INTEGER frequency{};
            QueryPerformanceFrequency( &frequency );
            scale = 1000.0 / static_cast< double >( frequency.QuadPart );
        }
        LARGE_INTEGER counter{};
        QueryPerformanceCounter( &counter );
        return static_cast< double >( counter.QuadPart ) * scale;
#else
        return 0.0;
#endif
    }

    struct frame_t
    {
        double ocean = 0.0;
        double mesh = 0.0;
        double upload = 0.0;
        double shade = 0.0;
        double pfx = 0.0;
        // CPU skinning for the animated props, kept separate from pfx so the
        // Intel HD 505 budget can be read off rather than guessed at.
        double props = 0.0;
        double ensure = 0.0;
        double draw = 0.0;
        double present = 0.0;
        double gpu = 0.0;
        int draws = 0;
        int textures = 0;
        int placements = 0;
        // Draws that sampled an environment cube. The reflection costs a
        // cube fetch per pixel, so this is the number to watch against the
        // Intel HD 505 budget - it is the first term to drop.
        int reflections = 0;
        // Draws carrying a non-zero specular product. Separate from
        // reflections because far more materials author a SpecularAmount than
        // a cube, and because a ^32 lobe can be live yet invisible - the
        // counter says the term is reaching the GPU even when no pixel shows
        // a highlight at this camera angle.
        int speculars = 0;
    };

    inline frame_t& frame( )
    {
        static frame_t current{};
        return current;
    }

    // report() zeroes frame() every tick, and it runs inside present() before
    // anything downstream gets a look. The completed frame is kept here so a
    // slow frame can still be attributed after the fact.
    inline frame_t& previous( )
    {
        static frame_t value{};
        return value;
    }

    inline void report( )
    {
        static double last = now( );
        static frame_t sum{};
        static int count = 0;
        frame_t& current = frame( );
        sum.ocean += current.ocean;
        sum.mesh += current.mesh;
        sum.upload += current.upload;
        sum.shade += current.shade;
        sum.pfx += current.pfx;
        sum.ensure += current.ensure;
        sum.draw += current.draw;
        sum.present += current.present;
        sum.gpu += current.gpu;
        sum.draws += current.draws;
        sum.textures += current.textures;
        sum.placements += current.placements;
        sum.reflections += current.reflections;
        sum.speculars += current.speculars;
        ++count;
        previous( ) = current;
        current = {};
        const double time = now( );
        if ( time - last < 1000.0 || count == 0 )
            return;
        const double scale = 1.0 / static_cast< double >( count );
        LOG_VERBOSE(   // a console line a second is a hitch a second: only on request
            "trace ocean %.2f mesh %.2f upload %.2f normal %.2f pfx %.2f ensure %.2f draw %.2f present %.2f gpu %.2f draws %.0f tex %.1f places %.0f reflect %.0f spec %.0f",
            sum.ocean * scale,
            sum.mesh * scale,
            sum.upload * scale,
            sum.shade * scale,
            sum.pfx * scale,
            sum.ensure * scale,
            sum.draw * scale,
            sum.present * scale,
            sum.gpu * scale,
            static_cast< double >( sum.draws ) * scale,
            static_cast< double >( sum.textures ) * scale,
            static_cast< double >( sum.placements ) * scale,
            static_cast< double >( sum.reflections ) * scale,
            static_cast< double >( sum.speculars ) * scale
        );
        sum = {};
        count = 0;
        last = time;
    }
}
