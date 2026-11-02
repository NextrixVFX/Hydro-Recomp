#pragma once

#include <workspace/utility/fluid.hxx>
#include <workspace/core/world/control.hxx>
#include <workspace/core/world/wave.hxx>
#include <workspace/core/world/transform.hxx>
#include <workspace/core/boat/parts.hxx>
#include <workspace/core/boat/construct.hxx>
#include <workspace/core/game/types.hxx>
#include <workspace/core/entity/dynamic.hxx>
#include <workspace/core/entity/corona.hxx>
#include <workspace/utility/golden.hxx>
#include <workspace/utility/logger.hxx>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// The game side of the differential oracle.
//
// engine/workspace/utility/verify.hxx holds the checks for engine maths. This
// holds the ones for game code, because the engine must not include a game
// header to test it - the separation is the point, not an inconvenience.

// whirlpool_vertex against VuWaterWhirlpoolWave's kernel @0x5a1ad0.
//
// THE BAR HERE IS BIT EXACTNESS, unlike the euler check. That one compares
// against x87 `fsincos` at 80-bit internal precision and can only agree to
// within float rounding. This kernel uses nothing but multiplies, adds,
// divides and square roots, all of which IEEE-754 specifies exactly, so a
// statement-for-statement transliteration reproduces retail's bits, and
// anything less than exact is worth failing on.
//
// WHAT THIS CHECK WAS MEASURED TO CATCH, AND WHAT IT WAS NOT. Three
// perturbations were compiled and run against it rather than reasoned about:
//
//   swapping the two CROSSED swirl terms   fails 6 of 10 vectors
//   reordering the normalising sum          passes - undetected
//   dropping the radius epsilon entirely    passes at the original 8 samples
//
// So the earlier claim here that "any deviation means the order was changed"
// was wrong: at a radius of 20 the epsilon is far below the ULP and the sum's
// three terms reorder exactly. Two samples at a radius of 1e-4 were added so
// the epsilon is a tenth of a percent and does move the answer. The sum's
// operand order is still NOT covered by any sample, and saying so is better
// than implying it is.
inline bool verify_whirlpool( )
{
    double worst = 0.0;
    int checked = 0;
    int failed = 0;

    for ( std::size_t index = 0; index < g_golden_whirlpool_count; ++index )
    {
        const golden_whirlpool_t& sample = g_golden_whirlpool[ index ];
        whirlpool_t wave{};
        wave.centre_x = sample.m_wave[ 0 ];
        wave.centre_y = sample.m_wave[ 1 ];
        wave.outer_radius = sample.m_wave[ 2 ];
        wave.inner_radius = sample.m_wave[ 3 ];
        wave.depth = sample.m_wave[ 4 ];
        wave.angular_speed = sample.m_wave[ 5 ];
        wave.linear_speed = sample.m_wave[ 6 ];

        // Zeroed exactly as the oracle's vertex was, so what comes out is one
        // wave's contribution and nothing else.
        float displacement[ 3 ]{};
        float height = 0.0f;
        whirlpool_vertex( wave, sample.m_point[ 0 ], sample.m_point[ 1 ],
            displacement, height );

        const float ours[ 4 ] = { displacement[ 0 ], displacement[ 1 ],
            displacement[ 2 ], height };
        double local_worst = 0.0;
        for ( int slot = 0; slot < 4; ++slot )
        {
            const double delta = std::fabs( static_cast< double >( ours[ slot ] )
                - static_cast< double >( sample.m_out[ slot ] ) );
            if ( delta > local_worst )
                local_worst = delta;
        }
        ++checked;
        if ( local_worst > worst )
            worst = local_worst;
        if ( local_worst != 0.0 )
        {
            ++failed;
            // %g, not %f: two samples sit at a radius of 1e-4 and %.2f prints
            // both of them as "0.00 0.00", which makes a real failure
            // unidentifiable. Found by reading an actual failure.
            LOG_ERROR( "whirlpool_vertex differs from the kernel at point %.6g %.6g by %.3e",
                sample.m_point[ 0 ], sample.m_point[ 1 ], local_worst );
        }
    }

    if ( failed == 0 )
    {
        LOG_SUCCESS( "whirlpool_vertex matches VuWaterWhirlpoolWave's kernel @0x5a1ad0 on %d vectors, worst deviation %.3e",
            checked, worst );
    }
    else
    {
        LOG_ERROR( "whirlpool_vertex FAILED %d of %d vectors against the kernel @0x5a1ad0",
            failed, checked );
    }
    return failed == 0;
}

// directional_vertex against VuWaterDirectionalWave's base kernel @0x59b110.
//
// THIS ONE CALLS cos AND sin, so bit exactness is not a given the way it is
// for the whirlpool. Retail's x86 CRT takes a double; ours promotes and
// narrows to match. The threshold is reported either way, and the honest
// reading of the number printed is in the commit that added it.
inline bool verify_directional( )
{
    double worst = 0.0;
    int checked = 0;
    int failed = 0;

    for ( std::size_t index = 0; index < g_golden_directional_count; ++index )
    {
        const golden_directional_t& sample = g_golden_directional[ index ];
        directional_t wave{};
        wave.amplitude = sample.m_wave[ 0 ];
        wave.wavenumber = sample.m_wave[ 1 ];
        wave.phase_scale = sample.m_wave[ 2 ];
        wave.edge_b = sample.m_wave[ 3 ];
        wave.edge_a = sample.m_wave[ 4 ];
        wave.clock = sample.m_wave[ 5 ];
        std::memcpy( wave.transform, sample.m_matrix, sizeof( wave.transform ) );

        float displacement[ 3 ]{};
        float height = 0.0f;
        directional_vertex( wave, sample.m_point[ 0 ], sample.m_point[ 1 ],
            displacement, height );

        const float ours[ 4 ] = { displacement[ 0 ], displacement[ 1 ],
            displacement[ 2 ], height };
        double local_worst = 0.0;
        for ( int slot = 0; slot < 4; ++slot )
        {
            const double delta = std::fabs( static_cast< double >( ours[ slot ] )
                - static_cast< double >( sample.m_out[ slot ] ) );
            if ( delta > local_worst )
                local_worst = delta;
        }
        ++checked;
        if ( local_worst > worst )
            worst = local_worst;
        if ( local_worst > 1.0e-6 )
        {
            ++failed;
            LOG_ERROR( "directional_vertex differs from the kernel at point %.6g %.6g by %.3e",
                sample.m_point[ 0 ], sample.m_point[ 1 ], local_worst );
        }
    }

    if ( failed == 0 )
    {
        LOG_SUCCESS( "directional_vertex matches VuWaterDirectionalWave's kernel @0x59b110 on %d vectors, worst deviation %.3e",
            checked, worst );
    }
    else
    {
        LOG_ERROR( "directional_vertex FAILED %d of %d vectors against the kernel @0x59b110",
            failed, checked );
    }
    return failed == 0;
}

// flow_vertex against VuWaterDirectionalFlowWave's base kernel @0x59c7c0.
// No transcendentals here, so the bar is bit exactness as it is for the
// whirlpool.
inline bool verify_flow( )
{
    double worst = 0.0;
    int checked = 0;
    int failed = 0;

    for ( std::size_t index = 0; index < g_golden_flow_count; ++index )
    {
        const golden_flow_t& sample = g_golden_flow[ index ];
        flow_t wave{};
        wave.flow[ 0 ] = sample.m_wave[ 0 ];
        wave.flow[ 1 ] = sample.m_wave[ 1 ];
        wave.flow[ 2 ] = sample.m_wave[ 2 ];
        wave.edge_b = sample.m_wave[ 3 ];
        wave.edge_a = sample.m_wave[ 4 ];
        std::memcpy( wave.transform, sample.m_matrix, sizeof( wave.transform ) );

        float displacement[ 3 ]{};
        float height = 0.0f;
        flow_vertex( wave, sample.m_point[ 0 ], sample.m_point[ 1 ],
            displacement, height );

        const float ours[ 4 ] = { displacement[ 0 ], displacement[ 1 ],
            displacement[ 2 ], height };
        double local_worst = 0.0;
        for ( int slot = 0; slot < 4; ++slot )
        {
            const double delta = std::fabs( static_cast< double >( ours[ slot ] )
                - static_cast< double >( sample.m_out[ slot ] ) );
            if ( delta > local_worst )
                local_worst = delta;
        }
        ++checked;
        if ( local_worst > worst )
            worst = local_worst;
        if ( local_worst != 0.0 )
        {
            ++failed;
            LOG_ERROR( "flow_vertex differs from the kernel at point %.6g %.6g by %.3e",
                sample.m_point[ 0 ], sample.m_point[ 1 ], local_worst );
        }
    }

    if ( failed == 0 )
    {
        LOG_SUCCESS( "flow_vertex matches VuWaterDirectionalFlowWave's kernel @0x59c7c0 on %d vectors, worst deviation %.3e",
            checked, worst );
    }
    else
    {
        LOG_ERROR( "flow_vertex FAILED %d of %d vectors against the kernel @0x59c7c0",
            failed, checked );
    }
    return failed == 0;
}

// ramp_vertex against VuWaterRampWave's base kernel @0x5a2c10. No
// transcendentals, so the bar is bit exactness.
inline bool verify_ramp( )
{
    double worst = 0.0;
    int checked = 0;
    int failed = 0;

    for ( std::size_t index = 0; index < g_golden_ramp_count; ++index )
    {
        const golden_ramp_t& sample = g_golden_ramp[ index ];
        ramp_t wave{};
        wave.blend_height = sample.m_wave[ 0 ];
        wave.band = sample.m_wave[ 1 ];
        wave.curve = sample.m_wave[ 2 ];
        wave.slope = sample.m_wave[ 3 ];
        for ( int axis = 0; axis < 3; ++axis )
        {
            wave.flow_inner[ axis ] = sample.m_wave[ 4 + axis ];
            wave.flow_outer[ axis ] = sample.m_wave[ 7 + axis ];
        }
        wave.along_x = sample.m_along[ 0 ];
        wave.along_y = sample.m_along[ 1 ];
        wave.along_offset = sample.m_along[ 2 ];

        float displacement[ 3 ]{};
        float height = 0.0f;
        ramp_vertex( wave, sample.m_point[ 0 ], sample.m_point[ 1 ],
            displacement, height );

        const float ours[ 4 ] = { displacement[ 0 ], displacement[ 1 ],
            displacement[ 2 ], height };
        double local_worst = 0.0;
        for ( int slot = 0; slot < 4; ++slot )
        {
            const double delta = std::fabs( static_cast< double >( ours[ slot ] )
                - static_cast< double >( sample.m_out[ slot ] ) );
            if ( delta > local_worst )
                local_worst = delta;
        }
        ++checked;
        if ( local_worst > worst )
            worst = local_worst;
        if ( local_worst != 0.0 )
        {
            ++failed;
            LOG_ERROR( "ramp_vertex differs from the kernel at point %.6g %.6g by %.3e",
                sample.m_point[ 0 ], sample.m_point[ 1 ], local_worst );
        }
    }

    if ( failed == 0 )
    {
        LOG_SUCCESS( "ramp_vertex matches VuWaterRampWave's kernel @0x5a2c10 on %d vectors, worst deviation %.3e",
            checked, worst );
    }
    else
    {
        LOG_ERROR( "ramp_vertex FAILED %d of %d vectors against the kernel @0x5a2c10",
            failed, checked );
    }
    return failed == 0;
}

// bump_vertex against VuWaterBumpWave's base kernel @0x5a4140. This one calls
// cos, so the threshold is loose for the same reason the directional's is.
inline bool verify_bump( )
{
    double worst = 0.0;
    int checked = 0;
    int failed = 0;

    for ( std::size_t index = 0; index < g_golden_bump_count; ++index )
    {
        const golden_bump_t& sample = g_golden_bump[ index ];
        bump_t wave{};
        wave.amplitude = sample.m_wave[ 0 ];
        wave.edge_a = sample.m_wave[ 1 ];
        std::memcpy( wave.transform, sample.m_matrix, sizeof( wave.transform ) );

        float displacement[ 3 ]{};
        float height = 0.0f;
        bump_vertex( wave, sample.m_point[ 0 ], sample.m_point[ 1 ],
            displacement, height );

        const float ours[ 4 ] = { displacement[ 0 ], displacement[ 1 ],
            displacement[ 2 ], height };
        double local_worst = 0.0;
        for ( int slot = 0; slot < 4; ++slot )
        {
            const double delta = std::fabs( static_cast< double >( ours[ slot ] )
                - static_cast< double >( sample.m_out[ slot ] ) );
            if ( delta > local_worst )
                local_worst = delta;
        }
        ++checked;
        if ( local_worst > worst )
            worst = local_worst;
        if ( local_worst > 1.0e-6 )
        {
            ++failed;
            LOG_ERROR( "bump_vertex differs from the kernel at point %.6g %.6g by %.3e",
                sample.m_point[ 0 ], sample.m_point[ 1 ], local_worst );
        }
    }

    if ( failed == 0 )
    {
        LOG_SUCCESS( "bump_vertex matches VuWaterBumpWave's kernel @0x5a4140 on %d vectors, worst deviation %.3e",
            checked, worst );
    }
    else
    {
        LOG_ERROR( "bump_vertex FAILED %d of %d vectors against the kernel @0x5a4140",
            failed, checked );
    }
    return failed == 0;
}

// point_wave_vertex against VuWaterPointWave's base kernel, inlined in slot 3
// @0x5a3510. Calls cos and sin, so the threshold is loose for the same reason
// the directional's is.
inline bool verify_point_wave( )
{
    double worst = 0.0;
    int checked = 0;
    int failed = 0;
    int live = 0;

    for ( std::size_t index = 0; index < g_golden_point_wave_count; ++index )
    {
        const golden_point_wave_t& sample = g_golden_point_wave[ index ];
        point_wave_t wave{};
        wave.centre_x = sample.m_wave[ 0 ];
        wave.centre_y = sample.m_wave[ 1 ];
        wave.tail_width = sample.m_wave[ 2 ];
        wave.amplitude = sample.m_wave[ 3 ];
        wave.inner_radius = sample.m_wave[ 4 ];
        wave.outer_radius = sample.m_wave[ 5 ];
        wave.phase_scale = sample.m_wave[ 6 ];
        wave.clock = sample.m_wave[ 7 ];
        wave.rate = sample.m_wave[ 8 ];
        wave.amplitude_scale = sample.m_wave[ 9 ];
        wave.slope = sample.m_wave[ 10 ];

        float displacement[ 3 ]{};
        float height = 0.0f;
        point_wave_vertex( wave, sample.m_point[ 0 ], sample.m_point[ 1 ],
            displacement, height );

        const float ours[ 4 ] = { displacement[ 0 ], displacement[ 1 ],
            displacement[ 2 ], height };
        double local_worst = 0.0;
        for ( int slot = 0; slot < 4; ++slot )
        {
            const double delta = std::fabs( static_cast< double >( ours[ slot ] )
                - static_cast< double >( sample.m_out[ slot ] ) );
            if ( delta > local_worst )
                local_worst = delta;
        }
        ++checked;
        if ( sample.m_out[ 2 ] != 0.0f || sample.m_out[ 3 ] != 0.0f )
            ++live;
        if ( local_worst > worst )
            worst = local_worst;
        if ( local_worst > 1.0e-5 )
        {
            ++failed;
            LOG_ERROR( "point_wave_vertex differs from the kernel at point %.6g %.6g by %.3e",
                sample.m_point[ 0 ], sample.m_point[ 1 ], local_worst );
        }
    }

    if ( failed == 0 )
    {
        // The live count is reported because most of this kernel's branches
        // REJECT the vertex: a sample set that happened to reject all of them
        // would pass while measuring nothing, and that nearly happened - three
        // samples sat outside the ring until they were moved.
        LOG_SUCCESS( "point_wave_vertex matches VuWaterPointWave's kernel @0x5a3510 on %d vectors (%d producing output), worst deviation %.3e",
            checked, live, worst );
    }
    else
    {
        LOG_ERROR( "point_wave_vertex FAILED %d of %d vectors against the kernel @0x5a3510",
            failed, checked );
    }
    return failed == 0;
}

// banked_turn_vertex against VuWaterBankedTurnWave's base kernel @0x5a55c0.
// Calls acos and cos, so the threshold is loose for the same reason the
// directional's is.
inline bool verify_banked_turn( )
{
    double worst = 0.0;
    int checked = 0;
    int failed = 0;
    int live = 0;

    for ( std::size_t index = 0; index < g_golden_banked_turn_count; ++index )
    {
        const golden_banked_turn_t& sample = g_golden_banked_turn[ index ];
        banked_turn_t wave{};
        wave.centre_x = sample.m_wave[ 0 ];
        wave.centre_y = sample.m_wave[ 1 ];
        wave.outer_radius = sample.m_wave[ 2 ];
        wave.inner_radius = sample.m_wave[ 3 ];
        wave.amplitude = sample.m_wave[ 4 ];
        wave.edge = sample.m_wave[ 5 ];
        wave.direction_x = sample.m_wave[ 6 ];
        wave.direction_y = sample.m_wave[ 7 ];
        wave.extent = sample.m_wave[ 8 ];

        float displacement[ 3 ]{};
        float height = 0.0f;
        banked_turn_vertex( wave, sample.m_point[ 0 ], sample.m_point[ 1 ],
            displacement, height );

        const float ours[ 4 ] = { displacement[ 0 ], displacement[ 1 ],
            displacement[ 2 ], height };
        double local_worst = 0.0;
        for ( int slot = 0; slot < 4; ++slot )
        {
            const double delta = std::fabs( static_cast< double >( ours[ slot ] )
                - static_cast< double >( sample.m_out[ slot ] ) );
            if ( delta > local_worst )
                local_worst = delta;
        }
        ++checked;
        if ( sample.m_out[ 3 ] != 0.0f )
            ++live;
        if ( local_worst > worst )
            worst = local_worst;
        if ( local_worst > 1.0e-5 )
        {
            ++failed;
            LOG_ERROR( "banked_turn_vertex differs from the kernel at point %.6g %.6g by %.3e",
                sample.m_point[ 0 ], sample.m_point[ 1 ], local_worst );
        }
    }

    if ( failed == 0 )
    {
        LOG_SUCCESS( "banked_turn_vertex matches VuWaterBankedTurnWave's kernel @0x5a55c0 on %d vectors (%d producing output), worst deviation %.3e",
            checked, live, worst );
    }
    else
    {
        LOG_ERROR( "banked_turn_vertex FAILED %d of %d vectors against the kernel @0x5a55c0",
            failed, checked );
    }
    return failed == 0;
}

// control_advance against VuAnimationControl's advance @0x516300. Compares
// the resulting time bit for bit (mulss, addss, minss, maxss and subtraction
// only) and the exact sequence of names the listener received.
inline bool verify_advance( )
{
    std::vector< timed_event_t > events;
    for ( std::size_t index = 0; index < sizeof( g_golden_advance_events ) / sizeof( float ); ++index )
    {
        timed_event_t event{};
        event.time = g_golden_advance_events[ index ];
        event.name = "E" + std::to_string( index );
        events.push_back( event );
    }

    int checked = 0;
    int failed = 0;
    int fires = 0;
    for ( std::size_t index = 0; index < g_golden_advance_count; ++index )
    {
        const golden_advance_t& sample = g_golden_advance[ index ];
        animation_control_t control{};
        control.end = g_golden_advance_end;
        control.loop = g_golden_advance_loop;
        control.looping = sample.m_in[ 0 ] != 0.0f;
        control.factor = sample.m_in[ 1 ];
        control.time = sample.m_in[ 2 ];
        control.events = sample.m_in[ 4 ] != 0.0f ? &events : nullptr;

        std::vector< std::string > fired;
        control_advance( control, sample.m_in[ 3 ], sample.m_in[ 5 ] != 0.0f,
            [ &fired ]( const std::string& name, const value_t* ) { fired.push_back( name ); } );

        bool same = std::memcmp( &control.time, &sample.m_time, sizeof( float ) ) == 0
            && static_cast< int >( fired.size( ) ) == sample.m_count;
        for ( int slot = 0; same && slot < sample.m_count; ++slot )
            same = fired[ slot ] == sample.m_fired[ slot ];
        ++checked;
        fires += sample.m_count;
        if ( !same )
        {
            ++failed;
            LOG_ERROR( "control_advance differs from @0x516300 at vector %d: time %.9g vs %.9g, %d fires vs %d",
                static_cast< int >( index ), control.time, sample.m_time,
                static_cast< int >( fired.size( ) ), sample.m_count );
        }
    }

    if ( failed == 0 )
    {
        LOG_SUCCESS( "control_advance matches VuAnimationControl's advance @0x516300 on %d vectors (%d fires, same order), time bit exact",
            checked, fires );
    }
    else
    {
        LOG_ERROR( "control_advance FAILED %d of %d vectors against @0x516300", failed, checked );
    }
    return failed == 0;
}

// transform_component_t against retail's VuTransformComponent: its ctor, the
// Position / Rotation / Scale loads and the recalc 0x534340 / 0x534AA0, on
// Properties maps from the Hydroelectric project and hand-built ones; the last
// documents under a parent world. Every word of both matrices and lanes 0-2 of
// rotation and scale (and of the world euler without a parent) bit exact.
inline bool verify_transform_load( )
{
    int failed = 0;
    for ( std::size_t index = 0; index < g_golden_transform_load_count; ++index )
    {
        const unsigned* s = g_golden_transform_load_words + g_golden_transform_load_offsets[ index ];
        const bool parented = s[ 0 ] != 0;
        const unsigned size = s[ 1 ];
        const unsigned char* raw = reinterpret_cast< const unsigned char* >( s + 2 );
        const unsigned* want = s + 2 + ( size + 3 ) / 4;
        const value_t properties = c_reader( std::vector< std::uint8_t >( raw, raw + size ) ).parse( );
        float parent_world[ 16 ];
        std::memcpy( parent_world, g_golden_transform_load_parent, sizeof( parent_world ) );

        transform_component_t c{};
        transform_construct( c );
        transform_load( c, properties );
        transform_recalc( c, parented ? parent_world : nullptr );

        unsigned ours[ 44 ];
        std::memcpy( ours, c.local, 64 );
        std::memcpy( ours + 16, c.world, 64 );
        std::memcpy( ours + 32, c.rotation, 16 );
        std::memcpy( ours + 36, c.world_rotation, 16 );
        std::memcpy( ours + 40, c.scale, 16 );
        int bad = -1;
        for ( int word = 0; word < 44 && bad < 0; ++word )
        {
            if ( word % 4 == 3 && word >= 32 )
                continue;
            if ( word >= 36 && word < 40 && parented )
                continue;
            if ( ours[ word ] != want[ word ] )
                bad = word;
        }
        if ( bad >= 0 )
        {
            ++failed;
            LOG_ERROR( "VuTransformComponent document %d differs at word %d (+0x%X): ours %08x, retail %08x",
                static_cast< int >( index ), bad, 0x10 + 4 * bad, ours[ bad ], want[ bad ] );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "transform_component_t matches retail's VuTransformComponent load + recalc 0x534340 on %d documents, every compared word exact",
            static_cast< int >( g_golden_transform_load_count ) );
    else
        LOG_ERROR( "transform_component_t FAILED %d of %d documents against 0x534340", failed,
            static_cast< int >( g_golden_transform_load_count ) );
    return failed == 0;
}

// One retail image off the boat_parts stream against ours: every byte the
// ctor wrote (the mask) equal, outside `skip` (indeterminate bytes); the heap
// pointer slots exactly the ones ours owns. Returns the first bad offset, or
// -1. Advances `s` past the image.
inline int parts_compare( const unsigned*& s, const std::uint8_t* ours, std::size_t size,
    std::vector< int > owned, std::vector< std::pair< int, int > > skip = {},
    std::vector< std::pair< int, int > > selfs = {} )
{
    const unsigned n = *s++;
    const unsigned char* bytes = reinterpret_cast< const unsigned char* >( s );
    s += ( n + 3 ) / 4;
    const unsigned char* mask = reinterpret_cast< const unsigned char* >( s );
    s += ( n + 3 ) / 4;
    const unsigned pointers = *s++;
    std::vector< int > theirs( s, s + pointers );
    s += pointers;
    // Pointers into the object itself (a property bound to a member), as
    // (slot, the member's offset).
    const unsigned self_count = *s++;
    std::vector< std::pair< int, int > > their_selfs;
    for ( unsigned k = 0; k < self_count; ++k, s += 2 )
        their_selfs.emplace_back( static_cast< int >( s[ 0 ] ), static_cast< int >( s[ 1 ] ) );
    if ( n != size || theirs != owned || their_selfs != selfs )
        return -2;
    for ( unsigned at = 0; at < n; ++at )
    {
        bool skipped = false;
        for ( const auto& range : skip )
            skipped = skipped || ( static_cast< int >( at ) >= range.first && static_cast< int >( at ) < range.second );
        if ( mask[ at ] && !skipped && bytes[ at ] != ours[ at ] )
            return static_cast< int >( at );
    }
    return -1;
}

// The VuBoatEntity ctor pieces (boat/parts.hxx) against retail's run of each
// (oracle case boat_parts): 0x41B400, 0x41CA50, 0x5240C0, 0x4192E0 and the big
// splash 0x415D60 reading the real ConstantDB (the golden vujb_read document
// 0, parsed by c_reader). Every written byte, every owned pointer, the string.
inline bool verify_boat_parts( )
{
    const unsigned* cdb = g_golden_vujb_words + g_golden_vujb_offsets[ 0 ];
    const unsigned char* cdb_raw = reinterpret_cast< const unsigned char* >( cdb + 2 );
    const value_t constant_db = c_reader( std::vector< std::uint8_t >( cdb_raw, cdb_raw + cdb[ 1 ] ) ).parse( );
    const std::vector< int > object_pointers = { 52, 56, 68 };
    int failed = 0;
    auto report = [ &failed ]( const char* what, int bad )
    {
        if ( bad == -1 )
            return;
        ++failed;
        LOG_ERROR( "boat ctor piece %s differs from retail at byte %d (-2: size or pointer slots)", what, bad );
    };
    for ( std::size_t index = 0; index < g_golden_boat_parts_count; ++index )
    {
        const unsigned* s = g_golden_boat_parts_words + g_golden_boat_parts_offsets[ index ];
        if ( index == 0 )
        {
            boat_41b400_t o;
            boat_41b400( o );
            report( "0x41B400", parts_compare( s, o.image, sizeof( o.image ), {} ) );
        }
        else if ( index == 1 )
        {
            boat_41ca50_t o;
            boat_41ca50( o );
            report( "0x41CA50", parts_compare( s, o.image, sizeof( o.image ), {} ) );
        }
        else if ( index == 2 )
        {
            auto o = boat_5240c0( );
            report( "0x5240C0", parts_compare( s, o->image, sizeof( o->image ), object_pointers ) );
        }
        else if ( index == 3 )
        {
            boat_4192e0_t o;
            boat_4192e0( o, 0x12345678u );
            report( "0x4192E0", parts_compare( s, o.image, sizeof( o.image ), { 52 }, { { 164, 168 }, { 180, 184 } } ) );
            if ( *s++ != 1 || *s++ != 52 )
                report( "0x4192E0 +0x34", -2 );
            else
                report( "0x4192E0 +0x34", parts_compare( s, o.at_34->image, sizeof( o.at_34->image ), object_pointers ) );
        }
        else if ( index == 5 )
        {
            boat_40fe00_t o;
            boat_40fe00( o, 0x12345678u );
            report( "0x40FE00", parts_compare( s, o.image, sizeof( o.image ), {} ) );
        }
        else if ( index == 6 )
        {
            boat_413590_t o;
            boat_413590( o, 0x12345678u );
            report( "0x413590", parts_compare( s, o.image, sizeof( o.image ), { 48 } ) );
        }
        else if ( index == 7 )
        {
            boat_409750_t o;
            boat_409750( o, 0x12345678u );
            report( "0x409750", parts_compare( s, o.image, sizeof( o.image ), {} ) );
        }
        else if ( index == 8 )
        {
            boat_410d90_t o;
            boat_410d90( o, 0x12345678u );
            report( "0x410D90", parts_compare( s, o.image, sizeof( o.image ), { 156 }, { { 180, 184 }, { 196, 200 } } ) );
        }
        else if ( index == 9 )
        {
            boat_4131e0_t o;
            std::uint8_t a5[ 16 ];
            for ( int k = 0; k < 16; ++k )
                a5[ k ] = static_cast< std::uint8_t >( 0x40 + k );
            boat_4131e0( o, 0x3FA00000u, 0x11111111u, 0x22222222u, a5 );
            report( "0x4131E0", parts_compare( s, o.image, sizeof( o.image ), {} ) );
        }
        else if ( index == 10 )
        {
            boat_41b1b0_t o;
            boat_41b1b0( o );
            report( "0x41B1B0", parts_compare( s, o.image, sizeof( o.image ), {} ) );
        }
        else if ( index == 11 )
        {
            vu_3d_layout_t o;
            vu_3d_layout( o, 0x12345678u );
            report( "0x50B8D0", parts_compare( s, o.image, sizeof( o.image ), {}, { { 36, 40 }, { 52, 56 } } ) );
        }
        else if ( index == 12 )
        {
            vu_3d_draw_t o;
            vu_3d_draw( o, 0x12345678u, 1 );
            report( "0x531A40", parts_compare( s, o.image, sizeof( o.image ), { 4 }, { { 48, 52 }, { 64, 68 } } ) );
            const unsigned subs = *s++;
            const unsigned strings = *s++;
            const unsigned nodes = *s++;
            if ( subs != 0 || strings != 0 || nodes != 2 )
                report( "0x531A40 property list", -2 );
            else
            {
                report( "0x531A40 \"Reflect\"", parts_compare( s, o.properties[ 0 ].image, 0x1C, { 16 }, {},
                    { { 24, o.properties[ 0 ].member } } ) );
                report( "0x531A40 \"Shadow\"", parts_compare( s, o.properties[ 1 ].image, 0x1C, {}, {},
                    { { 24, o.properties[ 1 ].member } } ) );
            }
        }
        else if ( index == 13 )
        {
            vu_animated_model_t o;
            vu_animated_model( o );
            report( "0x532080", parts_compare( s, o.image, sizeof( o.image ), {}, { { 56, 60 }, { 72, 76 } } ) );
        }
        else if ( index >= 14 && index <= 17 )
        {
            // The oracle's targets: the VuStrings at ARG_BASE + 0x7100 / 0x7140
            // / 0x7180 / 0x71C0 (13, 27, 15 and 16 characters).
            static const char* const values[ 4 ] = { "ReflectTarget", "Generic/Water/SplashOutLong", "ExactlyFifteen!",
                "SixteenCharacter" };
            static const std::uint32_t targets[ 4 ] = { 0x30007100u, 0x30007140u, 0x30007180u, 0x300071C0u };
            static const char* const names[ 4 ] = { "0x406B70 (13, inline)", "0x406B70 (27, heap)", "0x406B70 (15, inline)",
                "0x406B70 (16, heap)" };
            const std::string value = values[ index - 14 ];
            const bool longer = value.size( ) > 15;
            vu_string_property_t o;
            vu_string_property( o, 0x006CDEA0u, targets[ index - 14 ], value );
            const char* what = names[ index - 14 ];
            report( what, parts_compare( s, o.image, sizeof( o.image ), o.heap ? std::vector< int >{ 20, 44 } : std::vector< int >{} ) );
            const unsigned subs = *s++;
            const unsigned strings = *s++;
            bool same = subs == 0 && strings == ( longer ? 2u : 0u );
            for ( unsigned k = 0; same && k < strings; ++k )
            {
                const unsigned at = *s++;
                const unsigned length = *s++;
                const std::string want( reinterpret_cast< const char* >( s ), length );
                s += ( length + 3 ) / 4;
                same = ( at == 20 && o.fallback == want ) || ( at == 44 && o.current == want );
            }
            if ( !same )
                report( what, -2 );
        }
        else
        {
            boat_splash_t o;
            boat_splash( o, 0x12345678u, constant_db );
            report( "0x415D60", parts_compare( s, o.image, sizeof( o.image ), { 380, 460 } ) );
            if ( *s++ != 1 || *s++ != 460 )
                report( "0x415D60 +0x1CC", -2 );
            else
                report( "0x415D60 +0x1CC", parts_compare( s, o.at_1cc->image, sizeof( o.at_1cc->image ), object_pointers ) );
            const unsigned strings = *s++;
            const unsigned at = *s++;
            const unsigned length = *s++;
            const std::string want( reinterpret_cast< const char* >( s ), length );
            if ( strings != 1 || at != 380 || o.pfx != want )
                report( "0x415D60 +0x17C string", -2 );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "boat ctor pieces 0x41B400 / 0x41CA50 / 0x5240C0 / 0x4192E0 / 0x415D60 / 0x40FE00 / 0x413590 / 0x409750 / 0x410D90 / 0x4131E0 / 0x41B1B0 / 0x50B8D0 / 0x531A40 / 0x532080 / 0x406B70 match retail: every written byte, "
                     "pointer slot and the ConstantDB BigSplash reads" );
    else
        LOG_ERROR( "boat ctor pieces FAILED %d checks against retail", failed );
    return failed == 0;
}

// boat_construct (boat/construct.hxx) against the object graph retail's
// VuBoatEntity construction builds (oracle case boat_ctor): every written byte
// of all 43 objects (outside the stack-residue words listed below), every
// pointer by what it points at, and the two method registrations.
inline bool verify_boat_ctor( )
{
    const unsigned* cdb = g_golden_vujb_words + g_golden_vujb_offsets[ 0 ];
    const unsigned char* cdb_raw = reinterpret_cast< const unsigned char* >( cdb + 2 );
    const value_t constant_db = c_reader( std::vector< std::uint8_t >( cdb_raw, cdb_raw + cdb[ 1 ] ) ).parse( );
    const boat_graph_t g = boat_construct( constant_db );
    // Words retail fills from stack slots its code never sets (indeterminate):
    // object -> [first, end) byte ranges.
    const std::vector< std::pair< int, std::pair< int, int > > > residue = {
        { 0, { 0x27C, 0x280 } }, { 0, { 0x28C, 0x290 } }, { 0, { 0x3C0, 0x3C4 } }, { 0, { 0x3D0, 0x3D4 } },
        { 8, { 36, 40 } }, { 8, { 52, 56 } }, { 9, { 48, 52 } }, { 9, { 64, 68 } },
        { 31, { 164, 168 } }, { 31, { 180, 184 } }, { 36, { 180, 184 } }, { 36, { 196, 200 } },
        { 38, { 56, 60 } }, { 38, { 72, 76 } }, { 39, { 56, 60 } }, { 39, { 72, 76 } } };
    const unsigned* s = g_golden_boat_ctor_words;
    const unsigned count = *s++;
    int failed = 0;
    auto fail = [ &failed ]( const char* what, int object, int at )
    {
        if ( ++failed <= 12 )
            LOG_ERROR( "boat ctor graph: %s at object %d, byte %d", what, object, at );
    };
    if ( count != g.objects.size( ) )
    {
        LOG_ERROR( "boat ctor graph: retail built %u objects, ours %d", count, static_cast< int >( g.objects.size( ) ) );
        return false;
    }
    for ( unsigned k = 0; k < count; ++k )
    {
        const unsigned size = *s++;
        const unsigned char* bytes = reinterpret_cast< const unsigned char* >( s );
        s += ( size + 3 ) / 4;
        const unsigned char* mask = reinterpret_cast< const unsigned char* >( s );
        s += ( size + 3 ) / 4;
        std::vector< image_ref_t > refs( *s++ );
        for ( auto& r : refs )
        {
            const int kind = static_cast< int >( s[ 1 ] );
            const int target = kind == 0 ? static_cast< int >( s[ 2 ] ) : kind == 1 ? BOAT_REF_ASSET_BOATDB
                : kind == 2 ? BOAT_REF_ASSET_SKINDB : kind == 3 ? BOAT_REF_STL : -999;
            r = { static_cast< int >( s[ 0 ] ), target, static_cast< int >( s[ 3 ] ) };
            s += 4;
        }
        const image_object_t& o = g.objects[ k ];
        if ( size != o.image.size( ) )
        {
            fail( "size", static_cast< int >( k ), static_cast< int >( o.image.size( ) ) );
            continue;
        }
        if ( refs != o.refs )
        {
            int at = -1;
            for ( std::size_t r = 0; r < refs.size( ) && r < o.refs.size( ); ++r )
                if ( !( refs[ r ] == o.refs[ r ] ) && at < 0 )
                    at = refs[ r ].slot;
            fail( "pointers", static_cast< int >( k ), at );
        }
        for ( unsigned at = 0; at < size; ++at )
        {
            bool skipped = false;
            for ( const auto& r : residue )
                skipped = skipped || ( r.first == static_cast< int >( k ) && static_cast< int >( at ) >= r.second.first &&
                                       static_cast< int >( at ) < r.second.second );
            if ( mask[ at ] && !skipped && bytes[ at ] != o.image[ at ] )
            {
                fail( "bytes", static_cast< int >( k ), static_cast< int >( at ) );
                break;
            }
        }
    }
    std::vector< std::pair< std::uint32_t, int > > methods( *s++ );
    for ( auto& m : methods )
    {
        m = { s[ 0 ], static_cast< int >( s[ 1 ] ) };
        s += 2;
    }
    std::vector< std::pair< std::uint32_t, int > > events;
    for ( unsigned n = *s++; n; --n )
    {
        const std::uint32_t hash = *s++;
        for ( unsigned c = *s++; c; --c )
            events.push_back( { hash, static_cast< int >( *s++ ) } );
    }
    if ( methods != g.methods )
        fail( "the entity's method map", -1, -1 );
    if ( events != g.events )
        fail( "the event manager's registrations", -1, -1 );
    if ( failed == 0 )
        LOG_SUCCESS( "boat_construct matches retail's VuBoatEntity construction 0x41B850: %u objects, every written byte and "
                     "pointer, and the OnCameraShake / OnRumble registrations", count );
    else
        LOG_ERROR( "boat_construct FAILED %d checks against 0x41B850", failed );
    return failed == 0;
}

// build_entity_registry (game/types.hxx) against the VuEntityRegistry retail's
// start-up builds (oracle case entity_registry): fed the same VuTemplateAsset
// names the oracle served, every entry - class, category, display name and
// create fn - must sit at retail's vector position, and the class -> index map
// must name each one there.
inline bool verify_entity_registry( )
{
    std::vector< std::string > names( g_golden_entity_templates, g_golden_entity_templates + g_golden_entity_templates_count );
    const entity_registry_t registry = build_entity_registry( names );
    int failed = 0;
    if ( registry.m_types.size( ) != g_golden_entity_types_count || registry.m_index.size( ) != g_golden_entity_map_size )
    {
        LOG_ERROR( "entity registry: retail has %d types (map %d), ours %d (map %d)", static_cast< int >( g_golden_entity_types_count ),
            static_cast< int >( g_golden_entity_map_size ), static_cast< int >( registry.m_types.size( ) ),
            static_cast< int >( registry.m_index.size( ) ) );
        return false;
    }
    for ( std::size_t k = 0; k < g_golden_entity_types_count; ++k )
    {
        const golden_entity_type_t& want = g_golden_entity_types[ k ];
        const entity_type_t& have = registry.m_types[ k ];
        const auto at = registry.m_index.find( want.m_class );
        if ( have.m_class != want.m_class || have.m_category != want.m_category || have.m_display != want.m_display ||
             have.m_create != want.m_create || at == registry.m_index.end( ) || at->second != static_cast< int >( k ) )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "entity registry: entry %d is %s / %s, retail %s / %s", static_cast< int >( k ), have.m_class.c_str( ),
                    have.m_category.c_str( ), want.m_class, want.m_category );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "build_entity_registry matches retail's VuEntityRegistry (0x5685B0 + 0x556AC0 + 0x4D4180): %d types "
                     "in retail's order, %d of them templates", static_cast< int >( g_golden_entity_types_count ),
            static_cast< int >( g_golden_entity_templates_count ) );
    else
        LOG_ERROR( "build_entity_registry FAILED %d of %d entries", failed, static_cast< int >( g_golden_entity_types_count ) );
    return failed == 0;
}

// entity_load (engine entity/node.hxx) against retail's VuEntity::load 0x5009D0
// / loadChildEntities 0x501300 (oracle case load_children): the level document
// parsed by c_reader, each subtree's root created as retail's registry would and
// loaded, and every entity it builds compared pre-order with retail's - parent,
// name, vtable, the +0x14 flags the child sort keys on, and +0x4C (0: the
// template path not taken). An UNKNOWN entity anywhere fails the subtree.
inline bool verify_load_children( )
{
    const auto parse = []( const unsigned* words, std::size_t bytes )
    {
        const unsigned char* raw = reinterpret_cast< const unsigned char* >( words );
        return c_reader( std::vector< std::uint8_t >( raw, raw + bytes ) ).parse( );
    };
    const value_t documents[ 2 ] = { parse( g_golden_load_children_document0, g_golden_load_children_document0_bytes ),
        parse( g_golden_load_children_document1, g_golden_load_children_document1_bytes ) };
    std::vector< std::string > names( g_golden_entity_templates, g_golden_entity_templates + g_golden_entity_templates_count );
    const entity_registry_t registry = build_entity_registry( names );
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    int failed = 0;
    int compared = 0;
    for ( std::size_t s = 0; s < g_golden_load_subtrees_count; ++s )
    {
        const golden_load_subtree_t& subtree = g_golden_load_subtrees[ s ];
        const value_t* node = &member( member( documents[ subtree.m_document ], "VuProject" ), "RootEntity" );
        for ( int depth = 0; depth < subtree.m_depth && node; ++depth )
        {
            const value_t& children = member( member( *node, "data" ), "ChildEntities" );
            const value_t* next = nullptr;
            for ( const value_t& child : children.list )
                if ( member( child, "name" ).text == subtree.m_path[ depth ] )
                    next = &child;
            node = next;
        }
        if ( !node || member( *node, "type" ).text != subtree.m_type )
        {
            LOG_ERROR( "load children: subtree %d (%s) not found in the document", static_cast< int >( s ), subtree.m_type );
            ++failed;
            continue;
        }
        const std::unique_ptr< entity_t > created = entity_create( subtree.m_type, world );
        if ( !created )
        {
            LOG_ERROR( "load children: %s did not create", subtree.m_type );
            ++failed;
            continue;
        }
        entity_t& root = *created;
        entity_load( root, &member( *node, "data" ), world );
        std::vector< std::pair< int, const entity_t* > > ours;
        const auto walk = [ &ours ]( const auto& self, const entity_t& e, int parent ) -> void
        {
            const int index = static_cast< int >( ours.size( ) );
            ours.push_back( { parent, &e } );
            for ( const auto& child : e.m_children )
                self( self, *child, index );
        };
        walk( walk, root, -1 );
        if ( ours.size( ) != static_cast< std::size_t >( subtree.m_count ) )
        {
            LOG_ERROR( "load children: %s/%s built %d entities, retail %d", subtree.m_path[ 0 ], subtree.m_path[ subtree.m_depth - 1 ],
                static_cast< int >( ours.size( ) ), subtree.m_count );
            ++failed;
            continue;
        }
        for ( int k = 0; k < subtree.m_count; ++k )
        {
            const golden_load_entity_t& want = g_golden_load_entities[ subtree.m_first + k ];
            const entity_t& have = *ours[ k ].second;
            ++compared;
            const bool known = have.m_ctor != nullptr;
            if ( !known || ours[ k ].first != want.m_parent || have.m_name != want.m_name || have.m_ctor->m_vtable != want.m_vtable ||
                 have.m_flags != want.m_flags || want.m_template != 0 || !have.m_template.empty( ) )
            {
                if ( ++failed <= 8 )
                    LOG_ERROR( "load children: %s entity %d is '%s' (%s), retail '%s'", subtree.m_path[ subtree.m_depth - 1 ], k,
                        have.m_name.c_str( ), known ? "known" : "UNKNOWN", want.m_name );
            }
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "entity_load matches retail's VuEntity::load 0x5009D0 / 0x501300 on %d subtrees (the level's and one "
                     "hand-built): %d entities - names, parents, child order, vtables, flags",
            static_cast< int >( g_golden_load_subtrees_count ), compared );
    else
        LOG_ERROR( "entity_load FAILED %d checks against 0x501300", failed );
    return failed == 0;
}

// The template path (create 0x556E70, mark 0x5016A0, override load 0x5015D0)
// against oracle case load_templates: each document parsed by c_reader, the
// VuTemplateAssets served to entity_create by name, each subtree's root created
// through the registry and loaded, and every entity compared pre-order with
// retail's - parent, name, vtable, the +0x14 flags (bit 2 on a template's
// descendants) and +0x4C (which template made it).
inline bool verify_load_templates( )
{
    std::map< std::string, value_t > documents;
    for ( std::size_t d = 0; d < g_golden_load_templates_documents_count; ++d )
    {
        const golden_named_document_t& doc = g_golden_load_templates_documents[ d ];
        const unsigned char* raw = reinterpret_cast< const unsigned char* >( doc.m_words );
        documents[ doc.m_name ] = c_reader( std::vector< std::uint8_t >( raw, raw + doc.m_bytes ) ).parse( );
    }
    std::vector< std::string > names( g_golden_entity_templates, g_golden_entity_templates + g_golden_entity_templates_count );
    const entity_registry_t registry = build_entity_registry( names );
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    world.m_template = [ &documents ]( const std::string& name ) -> const value_t*
    {
        const auto found = documents.find( "VuTemplateAsset/" + name );
        return found == documents.end( ) ? nullptr : &found->second;
    };
    world.m_components_for = entity_components_constructed;
    world.m_component_load = entity_load_component;
    world.m_component_mark = entity_mark_component;
    int failed = 0;
    int compared = 0;
    for ( std::size_t s = 0; s < g_golden_load_templates_subtrees_count; ++s )
    {
        const golden_load_subtree_t& subtree = g_golden_load_templates_subtrees[ s ];
        const value_t& document = documents[ g_golden_load_templates_documents[ subtree.m_document ].m_name ];
        const value_t* node = &member( member( document, "VuProject" ), "RootEntity" );
        for ( int depth = 0; depth < subtree.m_depth && node; ++depth )
        {
            const value_t* next = nullptr;
            for ( const value_t& child : member( member( *node, "data" ), "ChildEntities" ).list )
                if ( member( child, "name" ).text == subtree.m_path[ depth ] )
                    next = &child;
            node = next;
        }
        const std::unique_ptr< entity_t > root = node ? entity_create( subtree.m_type, world ) : nullptr;
        if ( !root )
        {
            LOG_ERROR( "load templates: subtree %d (%s) not found or not created", static_cast< int >( s ), subtree.m_type );
            ++failed;
            continue;
        }
        entity_load( *root, &member( *node, "data" ), world );
        std::vector< std::pair< int, const entity_t* > > ours;
        const auto walk = [ &ours ]( const auto& self, const entity_t& e, int parent ) -> void
        {
            const int index = static_cast< int >( ours.size( ) );
            ours.push_back( { parent, &e } );
            for ( const auto& child : e.m_children )
                self( self, *child, index );
        };
        walk( walk, *root, -1 );
        if ( ours.size( ) != static_cast< std::size_t >( subtree.m_count ) )
        {
            LOG_ERROR( "load templates: %s built %d entities, retail %d", subtree.m_path[ subtree.m_depth - 1 ],
                static_cast< int >( ours.size( ) ), subtree.m_count );
            ++failed;
            continue;
        }
        for ( int k = 0; k < subtree.m_count; ++k )
        {
            const golden_template_entity_t& want = g_golden_load_templates_entities[ subtree.m_first + k ];
            const entity_t& have = *ours[ k ].second;
            ++compared;
            const std::string want_template = want.m_template ? want.m_template : "";
            // The root keeps its ctor's empty name in the case (no parent named it).
            if ( !have.m_ctor || ours[ k ].first != want.m_parent || have.m_name != want.m_name ||
                 have.m_ctor->m_vtable != want.m_vtable || have.m_flags != want.m_flags || have.m_template != want_template )
            {
                if ( ++failed <= 8 )
                    LOG_ERROR( "load templates: entity %d is '%s' flags %u template '%s', retail '%s' flags %u template '%s'", k,
                        have.m_name.c_str( ), have.m_flags, have.m_template.c_str( ), want.m_name, want.m_flags, want_template.c_str( ) );
                continue;
            }
            // The transform after the whole create + override load (the template's own load, the mark's new
            // defaults, the override, 0x534C00's child walk): every word, the world euler skipped under a
            // parent and the fourth lanes skipped (as verify_transform_load).
            const golden_transform_words_t& tw = g_golden_load_templates_transforms[ subtree.m_first + k ];
            transform_component_t* t = entity_transform( const_cast< entity_t& >( have ) );
            int bad = ( t != nullptr ) != ( tw.m_has != 0 ) || have.m_world_unknown ? 0 : -1;
            if ( bad < 0 && t )
            {
                unsigned words[ 44 ];
                std::memcpy( words, t->local, 64 );
                std::memcpy( words + 16, t->world, 64 );
                std::memcpy( words + 32, t->rotation, 16 );
                std::memcpy( words + 36, t->world_rotation, 16 );
                std::memcpy( words + 40, t->scale, 16 );
                for ( int w = 0; w < 44 && bad < 0; ++w )
                {
                    if ( ( w % 4 == 3 && w >= 32 ) || ( w >= 36 && w < 40 && have.m_parent ) )
                        continue;
                    if ( words[ w ] != tw.m_words[ w ] )
                        bad = w;
                }
            }
            if ( bad >= 0 && ++failed <= 8 )
                LOG_ERROR( "load templates: '%s' transform differs at word %d", want.m_name, bad );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "the template path matches retail's 0x556E70 / 0x5016A0 / 0x5015D0 on %d real instance(s): %d entities - "
                     "names, parents, order, vtables, flags (bit 2 on template descendants), template owner, and every "
                     "transform word after the override (the mark's defaults, 0x534C00's child walk)",
            static_cast< int >( g_golden_load_templates_subtrees_count ), compared );
    else
        LOG_ERROR( "the template path FAILED %d checks against 0x556E70", failed );
    return failed == 0;
}

// entity_load_project (engine entity/node.hxx) against VuProject::load 0x556390
// (oracle case load_project): each project's document and AssetData "File"
// path, the root created, named and loaded, and every entity compared
// pre-order with retail's - parent, name (the root's from the path), vtable,
// flags, template owner.
inline bool verify_load_project( )
{
    std::vector< std::string > names( g_golden_entity_templates, g_golden_entity_templates + g_golden_entity_templates_count );
    const entity_registry_t registry = build_entity_registry( names );
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    int failed = 0;
    int compared = 0;
    for ( std::size_t p = 0; p < g_golden_load_projects_count; ++p )
    {
        const golden_project_t& project = g_golden_load_projects[ p ];
        const unsigned char* raw = reinterpret_cast< const unsigned char* >( project.m_words );
        const value_t document = c_reader( std::vector< std::uint8_t >( raw, raw + project.m_bytes ) ).parse( );
        const std::unique_ptr< entity_t > root = entity_load_project( member( document, "VuProject" ), project.m_path, world );
        if ( ( root != nullptr ) != ( project.m_ok != 0 ) )
        {
            LOG_ERROR( "load project: %s - retail's load returned %d, ours %d", project.m_path, project.m_ok, root ? 1 : 0 );
            ++failed;
            continue;
        }
        std::vector< std::pair< int, const entity_t* > > ours;
        const auto walk = [ &ours ]( const auto& self, const entity_t& e, int parent ) -> void
        {
            const int index = static_cast< int >( ours.size( ) );
            ours.push_back( { parent, &e } );
            for ( const auto& child : e.m_children )
                self( self, *child, index );
        };
        if ( root )
            walk( walk, *root, -1 );
        if ( ours.size( ) != static_cast< std::size_t >( project.m_count ) )
        {
            LOG_ERROR( "load project: %s built %d entities, retail %d", project.m_path, static_cast< int >( ours.size( ) ), project.m_count );
            ++failed;
            continue;
        }
        for ( int k = 0; k < project.m_count; ++k )
        {
            const golden_template_entity_t& want = g_golden_load_project_entities[ project.m_first + k ];
            const entity_t& have = *ours[ k ].second;
            ++compared;
            const std::string want_template = want.m_template ? want.m_template : "";
            if ( !have.m_ctor || ours[ k ].first != want.m_parent || have.m_name != want.m_name ||
                 have.m_ctor->m_vtable != want.m_vtable || have.m_flags != want.m_flags || have.m_template != want_template )
            {
                if ( ++failed <= 8 )
                    LOG_ERROR( "load project: %s entity %d is '%s', retail '%s'", project.m_path, k, have.m_name.c_str( ), want.m_name );
            }
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "entity_load_project matches retail's VuProject::load 0x556390 on %d projects: %d entities - the root's "
                     "type and name from the path, the tree below it", static_cast< int >( g_golden_load_projects_count ), compared );
    else
        LOG_ERROR( "entity_load_project FAILED %d checks against 0x556390", failed );
    return failed == 0;
}

// dynamic_light_load (engine entity/dynamic.hxx) against VuDynamicLightEntity's
// ten property loads as retail's VuEntity::load runs them (oracle case
// load_dynamic_lights): the real DynLights lights and hand-built ones for the
// branches the level never authors, each target compared bit for bit.
inline bool verify_dynamic_lights( )
{
    std::vector< value_t > documents;
    for ( const golden_named_document_t& doc : g_golden_dynamic_lights_documents )
    {
        const unsigned char* raw = reinterpret_cast< const unsigned char* >( doc.m_words );
        documents.push_back( c_reader( std::vector< std::uint8_t >( raw, raw + doc.m_bytes ) ).parse( ) );
    }
    int failed = 0;
    int compared = 0;
    for ( std::size_t k = 0; k < g_golden_dynamic_lights_count; ++k )
    {
        const golden_dynamic_light_t& want = g_golden_dynamic_lights[ k ];
        const value_t* node = &member( member( documents[ want.m_document ], "VuProject" ), "RootEntity" );
        for ( int depth = 0; depth <= want.m_depth && node; ++depth )
        {
            const char* step = depth < want.m_depth ? want.m_path[ depth ] : want.m_name;
            const value_t* next = nullptr;
            for ( const value_t& child : member( member( *node, "data" ), "ChildEntities" ).list )
                if ( member( child, "name" ).text == step )
                    next = &child;
            node = next;
        }
        if ( !node )
        {
            LOG_ERROR( "dynamic lights: '%s' not found in its document", want.m_name );
            ++failed;
            continue;
        }
        dynamic_light_t light;
        dynamic_light_construct( light );
        dynamic_light_load( light, member( member( *node, "data" ), "Properties" ) );
        const auto word = []( float f )
        {
            std::uint32_t b;
            std::memcpy( &b, &f, sizeof( b ) );
            return b;
        };
        const std::uint32_t ours[ 10 ] = { light.initially_on, light.diffuse, light.specular, word( light.factor ),
            word( light.falloff_min ), word( light.falloff_max ), word( light.cone ), word( light.penumbra ),
            word( light.draw_distance ), light.reflecting };
        static const char* const names[ 10 ] = { "Initially On", "Diffuse Color", "Specular Color", "Factor", "Falloff Range Min",
            "Falloff Range Max", "Cone Angle", "Penumbra Angle", "Draw Distance", "Reflecting" };
        for ( int p = 0; p < 10; ++p )
        {
            ++compared;
            if ( ours[ p ] != want.m_values[ p ] && ++failed <= 8 )
                LOG_ERROR( "dynamic lights: '%s' %s is 0x%08X, retail 0x%08X", want.m_name, names[ p ], ours[ p ], want.m_values[ p ] );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "dynamic_light_load matches VuDynamicLightEntity's property loads (ctor 0x5EDF60) on %d lights: %d values, "
                     "bit for bit", static_cast< int >( g_golden_dynamic_lights_count ), compared );
    else
        LOG_ERROR( "dynamic_light_load FAILED %d checks against 0x5EDF60's properties", failed );
    return failed == 0;
}

// The component step of VuEntity::load (0x501F70) with VuTransformComponent's
// loads and recalc as the world's component callback (game/types.hxx), against
// oracle case load_components: every entity of the level's all-clean subtrees,
// pre-order - its component list (vtables, +0x48 order) and its transform's
// +0x10 .. +0xBF words (the world euler skipped under a parent, the fourth
// lanes skipped, as verify_transform_load does). An entity marked
// m_world_unknown would fail: none of these subtrees' parents move.
inline bool verify_load_components( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_load_components_document );
    const value_t document = c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_load_components_document_bytes ) ).parse( );
    std::vector< std::string > names( g_golden_entity_templates, g_golden_entity_templates + g_golden_entity_templates_count );
    const entity_registry_t registry = build_entity_registry( names );
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    world.m_components_for = entity_components_constructed;
    world.m_component_load = entity_load_component;
    int failed = 0;
    int compared = 0;
    for ( std::size_t s = 0; s < g_golden_load_components_subtrees_count; ++s )
    {
        const golden_load_subtree_t& subtree = g_golden_load_components_subtrees[ s ];
        const value_t* node = &member( member( document, "VuProject" ), "RootEntity" );
        for ( int depth = 0; depth < subtree.m_depth && node; ++depth )
        {
            const value_t* next = nullptr;
            for ( const value_t& child : member( member( *node, "data" ), "ChildEntities" ).list )
                if ( member( child, "name" ).text == subtree.m_path[ depth ] )
                    next = &child;
            node = next;
        }
        const std::unique_ptr< entity_t > root = node ? entity_create( subtree.m_type, world ) : nullptr;
        if ( !root )
        {
            LOG_ERROR( "load components: subtree %d (%s) not found or not created", static_cast< int >( s ), subtree.m_type );
            ++failed;
            continue;
        }
        entity_load( *root, &member( *node, "data" ), world );
        std::vector< std::pair< int, entity_t* > > ours;
        const auto walk = [ &ours ]( const auto& self, entity_t& e, int parent ) -> void
        {
            const int index = static_cast< int >( ours.size( ) );
            ours.push_back( { parent, &e } );
            for ( auto& child : e.m_children )
                self( self, *child, index );
        };
        walk( walk, *root, -1 );
        if ( ours.size( ) != static_cast< std::size_t >( subtree.m_count ) )
        {
            LOG_ERROR( "load components: subtree %d built %d entities, retail %d", static_cast< int >( s ),
                static_cast< int >( ours.size( ) ), subtree.m_count );
            ++failed;
            continue;
        }
        for ( int k = 0; k < subtree.m_count; ++k )
        {
            const golden_component_entity_t& want = g_golden_load_components_entities[ subtree.m_first + k ];
            entity_t& have = *ours[ k ].second;
            ++compared;
            bool same = ours[ k ].first == want.m_parent && have.m_name == want.m_name &&
                        have.m_components.size( ) == static_cast< std::size_t >( want.m_count ) && !have.m_world_unknown;
            for ( int c = 0; same && c < want.m_count; ++c )
                same = have.m_components[ c ].m_vtable == g_golden_load_components_vtables[ want.m_first + c ];
            const transform_component_t* t = entity_transform( have );
            if ( same && ( t != nullptr ) != ( want.m_has_transform != 0 ) )
                same = false;
            int bad = -1;
            if ( same && t )
            {
                unsigned words[ 44 ];
                std::memcpy( words, t->local, 64 );
                std::memcpy( words + 16, t->world, 64 );
                std::memcpy( words + 32, t->rotation, 16 );
                std::memcpy( words + 36, t->world_rotation, 16 );
                std::memcpy( words + 40, t->scale, 16 );
                const bool parented = have.m_parent != nullptr;
                for ( int w = 0; w < 44 && bad < 0; ++w )
                {
                    if ( ( w % 4 == 3 && w >= 32 ) || ( w >= 36 && w < 40 && parented ) )
                        continue;
                    if ( words[ w ] != want.m_transform[ w ] )
                        bad = w;
                }
            }
            if ( !same || bad >= 0 )
            {
                if ( ++failed <= 8 )
                    LOG_ERROR( "load components: '%s' differs (%s%s)", want.m_name, same ? "transform word " : "list / parent / name",
                        same ? std::to_string( bad ).c_str( ) : "" );
            }
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuEntity::load's components (0x501F70) with VuTransformComponent match retail on %d level subtrees: %d entities - "
                     "component lists and every transform word", static_cast< int >( g_golden_load_components_subtrees_count ), compared );
    else
        LOG_ERROR( "the component step FAILED %d checks against 0x501F70", failed );
    return failed == 0;
}

// corona_load (engine entity/corona.hxx) against VuCoronaEntity's property
// loads as retail's VuEntity::load runs them (oracle case load_coronas): every
// corona in the Hydroelectric level, each target compared bit for bit (the
// rotation axis's fourth lane, parse-stack residue, skipped). "Texture Name"
// is not compared: its class's load is unread and its change is the
// rendering layer's texture load, a hook in the case.
inline bool verify_coronas( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_coronas_document );
    const value_t document = c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_coronas_document_bytes ) ).parse( );
    int failed = 0;
    int compared = 0;
    const auto word = []( float f )
    {
        std::uint32_t b;
        std::memcpy( &b, &f, sizeof( b ) );
        return b;
    };
    for ( std::size_t k = 0; k < g_golden_coronas_count; ++k )
    {
        const golden_corona_t& want = g_golden_coronas[ k ];
        const value_t* node = &member( member( document, "VuProject" ), "RootEntity" );
        for ( int d = 0; d < want.m_depth && node; ++d )
        {
            const value_t& children = member( member( *node, "data" ), "ChildEntities" );
            node = want.m_index[ d ] < static_cast< int >( children.list.size( ) ) ? &children.list[ want.m_index[ d ] ] : nullptr;
        }
        if ( !node )
        {
            LOG_ERROR( "coronas: '%s' not found", want.m_name );
            ++failed;
            continue;
        }
        corona_properties_t c;
        corona_construct( c );
        corona_properties_t defaults;
        corona_construct( defaults );
        corona_load( c, member( member( *node, "data" ), "Properties" ), defaults );
        const std::uint32_t ours[ 17 ] = { c.initially_visible, word( c.rotation_speed ), word( c.rotation_axis[ 0 ] ),
            word( c.rotation_axis[ 1 ] ), word( c.rotation_axis[ 2 ] ), 0u, word( c.draw_distance ), word( c.fade_distance ),
            word( c.query_radius ), word( c.cone ), word( c.penumbra ), c.back_light, c.texture_color, c.screen_space,
            word( c.texture_size ), word( c.rotation_offset ), word( c.rotation_amount ) };
        for ( int p = 0; p < 17; ++p )
        {
            if ( p == 5 )
                continue;   // the rotation axis's fourth lane
            ++compared;
            if ( ours[ p ] != want.m_values[ p ] && ++failed <= 8 )
                LOG_ERROR( "coronas: '%s' value %d is 0x%08X, retail 0x%08X", want.m_name, p, ours[ p ], want.m_values[ p ] );
        }
        ++compared;
        if ( c.texture_name != want.m_texture_name && ++failed <= 8 )
            LOG_ERROR( "coronas: '%s' Texture Name is '%s', retail '%s'", want.m_name, c.texture_name.c_str( ), want.m_texture_name );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "corona_load matches VuCoronaEntity's property loads (ctor 0x5E8D80) on %d level coronas: %d values, bit "
                     "for bit", static_cast< int >( g_golden_coronas_count ), compared );
    else
        LOG_ERROR( "corona_load FAILED %d checks against 0x5E8D80's properties", failed );
    return failed == 0;
}

// model_draw_load (engine entity/draw.hxx) against the model-draw components'
// property loads as retail's VuEntity::load runs them (oracle case
// load_prop_models): every VuPropEntity / VuAnimatedPropEntity placed in the
// Hydroelectric level, each node's value compared bit for bit, plus each
// type's values straight after create (model_draw_construct).
inline bool verify_prop_models( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_coronas_document );
    const value_t document = c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_coronas_document_bytes ) ).parse( );
    const auto word = []( float f )
    {
        std::uint32_t b;
        std::memcpy( &b, &f, sizeof( b ) );
        return b;
    };
    int failed = 0;
    int compared = 0;
    for ( std::size_t k = 0; k < g_golden_props_count; ++k )
    {
        const golden_prop_t& want = g_golden_props[ k ];
        const bool animated = want.m_animated != 0;
        model_draw_t c;
        model_draw_construct( c, animated );
        if ( want.m_depth >= 0 )
        {
            const value_t* node = &member( member( document, "VuProject" ), "RootEntity" );
            for ( int d = 0; d < want.m_depth && node; ++d )
            {
                const value_t& children = member( member( *node, "data" ), "ChildEntities" );
                node = want.m_index[ d ] < static_cast< int >( children.list.size( ) ) ? &children.list[ want.m_index[ d ] ] : nullptr;
            }
            if ( !node )
            {
                LOG_ERROR( "prop models: '%s' not found", want.m_name );
                ++failed;
                continue;
            }
            model_draw_t defaults;
            model_draw_construct( defaults, animated );
            const char* key = animated ? "Vu3dDrawAnimatedModelComponent" : "Vu3dDrawStaticModelComponent";
            model_draw_load( c, member( member( member( member( *node, "data" ), "Components" ), key ), "Properties" ), defaults );
        }
        std::vector< std::string > assets = { c.model_asset };
        std::vector< std::uint32_t > values = { c.reflect, c.shadow, c.color, word( c.draw_distance ) };
        if ( !animated )
        {
            assets.insert( assets.end( ), { c.lod1_model_asset, c.lod2_model_asset, c.reflection_model_asset } );
            values.insert( values.end( ), { word( c.lod0_draw_distance ), word( c.lod1_draw_distance ), word( c.rejection_scale_modifier ) } );
        }
        values.push_back( c.dynamic_lighting );
        for ( std::size_t a = 0; a < assets.size( ); ++a )
        {
            ++compared;
            if ( assets[ a ] != want.m_assets[ a ] && ++failed <= 8 )
                LOG_ERROR( "prop models: '%s' asset %d is '%s', retail '%s'", want.m_name, static_cast< int >( a ), assets[ a ].c_str( ),
                    want.m_assets[ a ] );
        }
        for ( std::size_t v = 0; v < values.size( ); ++v )
        {
            ++compared;
            if ( values[ v ] != want.m_values[ v ] && ++failed <= 8 )
                LOG_ERROR( "prop models: '%s' value %d is 0x%08X, retail 0x%08X", want.m_name, static_cast< int >( v ), values[ v ],
                    want.m_values[ v ] );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "model_draw_load matches the model-draw components' property loads (vt 0x7519AC / 0x758240) on %d props "
                     "and both types' create defaults: %d values, bit for bit", static_cast< int >( g_golden_props_count ) - 2, compared );
    else
        LOG_ERROR( "model_draw_load FAILED %d checks against the model-draw components", failed );
    return failed == 0;
}

// An entity's own properties through a template (oracle case
// load_template_properties): the hand-built template instantiated
// (entity_instantiate_template = 0x556E70), its mark making the loaded values
// the defaults (vt+0x1C), then the instance's override load (0x5015D0) - each
// corona's values compared with retail's, bit for bit, as verify_coronas does.
inline bool verify_template_properties( )
{
    const auto parse = []( const unsigned* words, std::size_t bytes )
    {
        const unsigned char* raw = reinterpret_cast< const unsigned char* >( words );
        return c_reader( std::vector< std::uint8_t >( raw, raw + bytes ) ).parse( );
    };
    const value_t templ = parse( g_golden_template_properties_template, g_golden_template_properties_template_bytes );
    const value_t override_doc = parse( g_golden_template_properties_override, g_golden_template_properties_override_bytes );
    std::vector< std::string > names( g_golden_entity_templates, g_golden_entity_templates + g_golden_entity_templates_count );
    const entity_registry_t registry = build_entity_registry( names );
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    world.m_template = [ &templ ]( const std::string& name ) -> const value_t* { return name == "SynthCorona" ? &templ : nullptr; };
    world.m_components_for = entity_components_constructed;
    world.m_component_load = entity_load_component;
    world.m_component_mark = entity_mark_component;
    world.m_state_for = entity_state_for;
    world.m_properties_load = entity_load_properties;
    world.m_properties_mark = entity_mark_properties;
    std::unique_ptr< entity_t > instance = entity_instantiate_template( "#SynthCorona", world );
    if ( !instance )
    {
        LOG_ERROR( "template properties: the instance was not created" );
        return false;
    }
    entity_load( *instance, &member( member( member( override_doc, "VuProject" ), "RootEntity" ), "data" ), world );
    const auto word = []( float f )
    {
        std::uint32_t b;
        std::memcpy( &b, &f, sizeof( b ) );
        return b;
    };
    int failed = 0;
    int compared = 0;
    for ( std::size_t k = 0; k < g_golden_template_properties_count; ++k )
    {
        const golden_corona_t& want = g_golden_template_properties[ k ];
        const entity_t* e = k < instance->m_children.size( ) ? instance->m_children[ k ].get( ) : nullptr;
        if ( !e || e->m_name != want.m_name || !e->m_state )
        {
            LOG_ERROR( "template properties: corona %d ('%s') missing", static_cast< int >( k ), want.m_name );
            ++failed;
            continue;
        }
        const corona_properties_t& c = static_cast< const corona_state_t* >( e->m_state.get( ) )->value;
        const std::uint32_t ours[ 17 ] = { c.initially_visible, word( c.rotation_speed ), word( c.rotation_axis[ 0 ] ),
            word( c.rotation_axis[ 1 ] ), word( c.rotation_axis[ 2 ] ), 0u, word( c.draw_distance ), word( c.fade_distance ),
            word( c.query_radius ), word( c.cone ), word( c.penumbra ), c.back_light, c.texture_color, c.screen_space,
            word( c.texture_size ), word( c.rotation_offset ), word( c.rotation_amount ) };
        for ( int p = 0; p < 17; ++p )
        {
            if ( p == 5 )
                continue;
            ++compared;
            if ( ours[ p ] != want.m_values[ p ] && ++failed <= 8 )
                LOG_ERROR( "template properties: '%s' value %d is 0x%08X, retail 0x%08X", want.m_name, p, ours[ p ], want.m_values[ p ] );
        }
        ++compared;
        if ( c.texture_name != want.m_texture_name && ++failed <= 8 )
            LOG_ERROR( "template properties: '%s' Texture Name is '%s', retail '%s'", want.m_name, c.texture_name.c_str( ), want.m_texture_name );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "an entity's properties through a template (0x556E70, the mark's vt+0x1C, the override 0x5015D0) match retail: "
                     "%d coronas, %d values", static_cast< int >( g_golden_template_properties_count ), compared );
    else
        LOG_ERROR( "template properties FAILED %d checks", failed );
    return failed == 0;
}

#include <workspace/utility/checks/world.hxx>
#include <workspace/utility/checks/ui.hxx>
#include <workspace/utility/checks/boat.hxx>
#include <workspace/utility/checks/replay.hxx>
#include <workspace/utility/checks/race.hxx>
#include <workspace/utility/checks/water.hxx>

// Every oracle-backed check on game code.
inline bool verify_game( )
{
    bool ok = true;
    ok = verify_world_all( ) && ok;
    ok = verify_ui_all( ) && ok;
    ok = verify_boat_all( ) && ok;
    ok = verify_replay_all( ) && ok;
    ok = verify_race_all( ) && ok;
    ok = verify_water_all( ) && ok;
    ok = verify_template_properties( ) && ok;
    ok = verify_coronas( ) && ok;
    ok = verify_prop_models( ) && ok;
    ok = verify_load_components( ) && ok;
    ok = verify_dynamic_lights( ) && ok;
    ok = verify_entity_registry( ) && ok;
    ok = verify_load_project( ) && ok;
    ok = verify_load_children( ) && ok;
    ok = verify_load_templates( ) && ok;
    ok = verify_boat_ctor( ) && ok;
    ok = verify_boat_parts( ) && ok;
    ok = verify_transform_load( ) && ok;
    ok = verify_advance( ) && ok;
    ok = verify_whirlpool( ) && ok;
    ok = verify_directional( ) && ok;
    ok = verify_flow( ) && ok;
    ok = verify_ramp( ) && ok;
    ok = verify_bump( ) && ok;
    ok = verify_point_wave( ) && ok;
    ok = verify_banked_turn( ) && ok;
    ok = verify_fluid_prepare( ) && ok;
    ok = verify_fluid_solve( ) && ok;
    ok = verify_hull_forces( ) && ok;
    ok = verify_hull_fluid( ) && ok;
    ok = verify_hull_invert( ) && ok;
    ok = verify_hull_steer( ) && ok;
    ok = verify_hull_update( ) && ok;
    ok = verify_boat_tick( ) && ok;
    ok = verify_linstep( ) && ok;
    ok = verify_boat_d570( ) && ok;
    ok = verify_curve( ) && ok;
    ok = verify_quat( ) && ok;
    ok = verify_qmul( ) && ok;
    ok = verify_qaxis( ) && ok;
    ok = verify_boat_4760( ) && ok;
    ok = verify_boat_4010( ) && ok;
    ok = verify_tick_thunk( ) && ok;
    ok = verify_boat_enable( ) && ok;
    ok = verify_euler_x87( ) && ok;
    ok = verify_boat_19c0( ) && ok;
    ok = verify_boat_disable( ) && ok;
    ok = verify_boat_teardown( ) && ok;
    return ok;
}
