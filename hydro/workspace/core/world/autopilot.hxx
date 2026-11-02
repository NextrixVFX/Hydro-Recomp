#pragma once

// --autopilot (OURS, a test driver, nothing of retail's: as --input-script): the human pad's Throttle and Steering answered from the track, so a
// headless run can finish a race and reach the paths behind the finish line for real (Results, the records, the ghost store). Full throttle, the steering
// proportional to the heading error to a point on the waypoints ahead of the nearest one, past the first NextA. It only feeds the input channels: no game
// state is touched.
inline float race_autopilot_axis( const race_session_t& s, const char* name )
{
    if ( std::strcmp( name, "Throttle" ) == 0 )
        return 1.0f;
    if ( std::strcmp( name, "Steering" ) != 0 || !s.track || s.track->waypoints.empty( ) )
        return 0.0f;
    const std::size_t k = race_ghost_human( s );
    if ( k >= s.boats.size( ) )
        return 0.0f;
    const std::uint8_t* t = race_session_image( *s.boats[ k ], 0, 0x50 );
    if ( !t )
        return 0.0f;
    float p[ 3 ], f[ 3 ];
    std::memcpy( p, t + 0x80, 12 );
    std::memcpy( f, t + 0x60, 12 );
    const std::vector< track_waypoint_t >& w = s.track->waypoints;
    // The nearest waypoint in front of the boat (a waypoint just passed would pull it back).
    int near_at = -1;
    float best = 3.4e38f;
    for ( std::size_t n = 0; n < w.size( ); ++n )
    {
        const float dx = w[ n ].position[ 0 ] - p[ 0 ], dy = w[ n ].position[ 1 ] - p[ 1 ];
        const float d = dx * dx + dy * dy;
        if ( dx * f[ 0 ] + dy * f[ 1 ] > 0.0f && d < best )
        {
            best = d;
            near_at = static_cast< int >( n );
        }
    }
    if ( near_at < 0 )
        return 0.0f;
    const track_waypoint_t& a = w[ static_cast< std::size_t >( near_at ) ];
    const track_waypoint_t& b = a.next_a >= 0 ? w[ static_cast< std::size_t >( a.next_a ) ] : a;
    const float tx = 0.5f * ( a.position[ 0 ] + b.position[ 0 ] ) - p[ 0 ], ty = 0.5f * ( a.position[ 1 ] + b.position[ 1 ] ) - p[ 1 ];
    const float cross = f[ 0 ] * ty - f[ 1 ] * tx, dot = f[ 0 ] * tx + f[ 1 ] * ty;
    const float steer = -std::atan2( cross, dot ) * 2.5f;
    return steer > 1.0f ? 1.0f : steer < -1.0f ? -1.0f : steer;
}
