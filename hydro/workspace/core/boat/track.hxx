#pragma once

#include <workspace/core/boat/nuisance.hxx>
#include <workspace/core/boat/state.hxx>
#include <workspace/core/game/types.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <xmmintrin.h>

// VuTrackManagerImpl (vtable 0x746C24, 25 slots; g_vutrackmanagerimpl
// 0x758FF4; ctor 0x4CF9F0) and the VuAiWaypointEntity (vtable 0x739D44; ctor
// 0x431770) it is built from. The manager: +4 / +8 / +0xC the sectors (count,
// capacity, data), +0x14 the start sector, +0x18 the end sector, +0x1C the
// track length, +0x20 / +0x24 / +0x28 the checkpoint waypoints, +0x30 the
// state (0 constructed, 1 after slot 24's init, 2 built). It is built in two
// steps: every waypoint's gameInitialize (slot 6 0x432400) hands it the
// waypoint (slot 5 0x4CFAF0: one sector to each of NextA / NextB) and, on the
// Start waypoint, collects the checkpoints (slot 6 0x4CFD60); then slot 7
// 0x4CFFD0 links the sectors, measures each one's distance to the end and
// numbers them by checkpoint (the race's 0x4829A0 and the background's
// 0x4BDEB0 call it). A sector is 0xF0 bytes (0x4D0F90) - kept here as that
// image, because the AI drivers' path points ARE these sectors (driver +0x8C
// holds pointers to them; boat/ai.hxx reads them by offset).

inline std::uint32_t track_u32( const std::uint8_t* b, int at )
{
    std::uint32_t v;
    std::memcpy( &v, b + at, 4 );
    return v;
}

inline void track_put_u32( std::uint8_t* b, int at, std::uint32_t v ) { std::memcpy( b + at, &v, 4 ); }

inline float track_maxss( float a, float b ) { return _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); }

// The waypoint's properties, from its ctor 0x431770 (each a VuBasicProperty
// whose default is the target's value at registration): +0x84 "Speed Hint"
// (float 0), +0x88 "Start" / +0x89 "CheckPoint" / +0x8A "Finish" / +0x8B
// "Allow Recovery" (the dword 0x1000000: false, false, false, true), +0x8C
// "Show Wrong Way" (byte 1), +0x90 "CheckPoint Width" / +0x94 "CheckPoint
// Height" (float 1), +0xA0 "Branch A Probability" (1) / +0xA4 "Branch B
// Probability" (0). +0x98 / +0xA4 the sector ids (-1) are not properties.
struct waypoint_properties_t
{
    float speed_hint = 0.0f;
    std::uint8_t start = 0;
    std::uint8_t checkpoint = 0;
    std::uint8_t finish = 0;
    std::uint8_t allow_recovery = 1;
    std::uint8_t show_wrong_way = 1;
    float width = 1.0f;
    float height = 1.0f;
    float branch_a = 1.0f;
    float branch_b = 0.0f;
};

struct waypoint_state_t
{
    waypoint_properties_t value;
    waypoint_properties_t defaults;
};

inline constexpr const char* k_waypoint_class = "VuAiWaypointEntity";

// The state the ctor leaves (both the targets and the nodes' defaults), or
// null for another class. For the world's m_state_for.
inline std::shared_ptr< void > waypoint_state_for( const std::string& cls )
{
    return cls == k_waypoint_class ? std::make_shared< waypoint_state_t >( ) : nullptr;
}

// VuEntity::load's "Properties" step for a waypoint: each node's load in
// registration order (engine entity/property.hxx). For m_properties_load.
inline void waypoint_load_properties( entity_t& e, const value_t& properties )
{
    if ( e.m_type != k_waypoint_class || !e.m_state )
        return;
    waypoint_state_t* s = static_cast< waypoint_state_t* >( e.m_state.get( ) );
    property_float( properties, "Speed Hint", s->value.speed_hint, s->defaults.speed_hint, false );
    property_bool( properties, "Start", s->value.start, s->defaults.start );
    property_bool( properties, "CheckPoint", s->value.checkpoint, s->defaults.checkpoint );
    property_bool( properties, "Finish", s->value.finish, s->defaults.finish );
    property_bool( properties, "Allow Recovery", s->value.allow_recovery, s->defaults.allow_recovery );
    property_bool( properties, "Show Wrong Way", s->value.show_wrong_way, s->defaults.show_wrong_way );
    property_float( properties, "CheckPoint Width", s->value.width, s->defaults.width, false );
    property_float( properties, "CheckPoint Height", s->value.height, s->defaults.height, false );
    property_float( properties, "Branch A Probability", s->value.branch_a, s->defaults.branch_a, false );
    property_float( properties, "Branch B Probability", s->value.branch_b, s->defaults.branch_b, false );
}

// The template mark's property step (each node's vt+0x1C). For m_properties_mark.
inline void waypoint_mark_properties( entity_t& e )
{
    if ( e.m_type == k_waypoint_class && e.m_state )
    {
        waypoint_state_t* s = static_cast< waypoint_state_t* >( e.m_state.get( ) );
        s->defaults = s->value;
    }
}

// One waypoint as the manager sees it: the entity's words +0x5C..+0xA4.
struct track_waypoint_t
{
    entity_t* entity = nullptr;
    float right[ 4 ] = { };     // +0x5C: the transform's (0.5, 0, 0) - its lane 3 is stale stack (UNKNOWN)
    float left[ 4 ] = { };      // +0x6C: (-0.5, 0, 0) - lane 3 stale stack (UNKNOWN)
    float position[ 4 ] = { };  // the transform component's world row 3 (+0x80 of the component)
    waypoint_properties_t p;
    std::int32_t sector_a = -1;   // +0x98
    std::int32_t sector_b = -1;   // +0x9C
    int next_a = -1;              // NextA / NextB: the refs' targets (0x432470 / 0x432520), as waypoint ordinals
    int next_b = -1;
};

// The postLoad (vt+8 0x432170; the transform watcher 0x432890, installed on
// the component's +0xC0 by the ctor, is the same body): M =
// VuMatrix_multiply(fromScale(scale), world) (transform_draw_matrix); +0x5C =
// M x (0.5, 0, 0, 1) and +0x6C = M x (-0.5, 0, 0, 1), each divided by its w
// (one 1 / w, then a multiply per lane); width = maxss(width, scale x),
// height = maxss(height, scale y).
inline void waypoint_post_load( track_waypoint_t& w, const transform_component_t& t )
{
    float m[ 16 ];
    transform_draw_matrix( t, m );
    const float half = 0.5f;
    const float zero = 0.0f;
    const float r7 = m[ 7 ] * zero, r11 = m[ 11 ] * zero, r4 = m[ 4 ] * zero, r8 = m[ 8 ] * zero;
    const float r5 = m[ 5 ] * zero, r9 = m[ 9 ] * zero, r6 = m[ 6 ] * zero, r10 = m[ 10 ] * zero;
    const float inv = 1.0f / ( ( ( m[ 3 ] * half + r7 ) + r11 ) + m[ 15 ] );
    w.right[ 0 ] = ( ( ( m[ 0 ] * half + r4 ) + r8 ) + m[ 12 ] ) * inv;
    w.right[ 1 ] = ( ( ( m[ 1 ] * half + r5 ) + r9 ) + m[ 13 ] ) * inv;
    w.right[ 2 ] = ( ( ( m[ 2 ] * half + r6 ) + r10 ) + m[ 14 ] ) * inv;
    const float inv2 = 1.0f / ( ( ( r7 - m[ 3 ] * half ) + r11 ) + m[ 15 ] );
    w.left[ 0 ] = ( ( ( r4 - m[ 0 ] * half ) + r8 ) + m[ 12 ] ) * inv2;
    w.left[ 1 ] = ( ( ( r5 - m[ 1 ] * half ) + r9 ) + m[ 13 ] ) * inv2;
    w.left[ 2 ] = ( ( ( r6 - m[ 2 ] * half ) + r10 ) + m[ 14 ] ) * inv2;
    w.p.width = track_maxss( w.p.width, t.scale[ 0 ] );
    w.p.height = track_maxss( w.p.height, t.scale[ 1 ] );
}

// One sector, 0x4D0F90's image and its two lists (+0xD8 next, +0xE4 prev)
// as sector indices. Image words: +0x00 / +0x10 the waypoint's left / right
// edge points, +0x20 / +0x30 the next waypoint's; +0x40 .. +0x70 four planes
// (normal, d); +0x80 / +0x90 the two waypoints' positions (16 b each, w 1);
// +0xA0 the unit direction (its w lane = the last plane's d - the register
// the ctor stores with it), +0xB0 the length, +0xB4 / +0xB8 the two speed
// hints x 0.44704 (mph to m/s, 0x6F7654), +0xBC the distance to the end
// (FLT_MAX until built), +0xC0 / +0xC4 the two edge widths, +0xC8 the branch
// probability, +0xCC its index, +0xD0 its checkpoint id, +0xD4 the waypoint
// (a pointer: here 0 - the ordinal is `waypoint`), +0xDC / +0xE8 the lists'
// counts. Lanes +0x0C / +0x1C / +0x2C / +0x3C are the waypoints' stale lanes
// (UNKNOWN).
inline constexpr int k_sector_bytes = 0xF0;

struct track_sector_t
{
    std::uint8_t image[ k_sector_bytes ] = { };
    int waypoint = -1;
    std::vector< int > next;
    std::vector< int > prev;
};

struct track_manager_t
{
    std::vector< track_waypoint_t > waypoints;
    std::vector< track_sector_t > sectors;       // +4 / +0xC
    std::vector< int > checkpoints;              // +0x20 / +0x28, waypoint ordinals
    int start = -1;                              // +0x14
    int end = -1;                                // +0x18
    float length = 0.0f;                         // +0x1C
    std::int32_t state = 1;                      // +0x30: 1 after slot 24 0x4D0C20's init
};

// The cross product 0x4D0F90 writes four times: (dy nz - dz ny, dz nx - dx nz,
// dx ny - dy nx), then normalised (sqrt((y y + x x) + z z), one 1 / len).
inline void track_cross_unit( float dx, float dy, float dz, const float* n, float* out )
{
    float x = dy * n[ 2 ] - dz * n[ 1 ];
    float y = dz * n[ 0 ] - dx * n[ 2 ];
    float z = dx * n[ 1 ] - dy * n[ 0 ];
    const float inv = 1.0f / hull_sqrt( ( y * y + x * x ) + z * z );
    out[ 0 ] = x * inv;
    out[ 1 ] = y * inv;
    out[ 2 ] = z * inv;
}

// 0x4D0F90 (thiscall; a = the waypoint's +0x6C, b its +0x5C, its position,
// c / d the next waypoint's +0x6C / +0x5C, its position, the two speeds, the
// branch probability, the waypoint; retn 28h), from its disassembly (Hex-Rays
// renders the first cross product's x lane wrongly).
inline void track_sector_build( track_sector_t& s, const track_waypoint_t& w, const track_waypoint_t& n, float speed_a, float speed_b,
    float probability )
{
    std::uint8_t* im = s.image;
    std::memcpy( im + 0x00, w.left, 16 );
    std::memcpy( im + 0x10, w.right, 16 );
    std::memcpy( im + 0x20, n.left, 16 );
    std::memcpy( im + 0x30, n.right, 16 );
    std::memcpy( im + 0x80, w.position, 16 );
    std::memcpy( im + 0x90, n.position, 16 );
    state_put_f( im, 0xB4, speed_a );
    state_put_f( im, 0xB8, speed_b );
    state_put_f( im, 0xC8, probability );
    track_put_u32( im, 0xBC, 0x7F7FFFFFu );
    track_put_u32( im, 0xCC, 0xFFFFFFFFu );
    track_put_u32( im, 0xD0, 0u );
    const float* p0 = w.left;
    const float* p1 = w.right;
    const float* p2 = n.left;
    const float* p3 = n.right;
    // the face normal: (p3 - p1) x (p0 - p1)
    const float ax = p3[ 0 ] - p1[ 0 ], ay = p3[ 1 ] - p1[ 1 ], az = p3[ 2 ] - p1[ 2 ];
    const float ex = p0[ 0 ] - p1[ 0 ], ey = p0[ 1 ] - p1[ 1 ], ez = p0[ 2 ] - p1[ 2 ];
    float nx = ay * ez - az * ey;
    float ny = az * ex - ax * ez;
    float nz = ax * ey - ay * ex;
    const float ninv = 1.0f / hull_sqrt( ( ny * ny + nx * nx ) + nz * nz );
    ny *= ninv;
    nz *= ninv;
    nx *= ninv;
    const float nrm[ 3 ] = { nx, ny, nz };
    float u[ 3 ], v[ 3 ], q[ 3 ], x[ 3 ];
    track_cross_unit( ex, ey, ez, nrm, u );                                                // plane 0: the start edge
    track_cross_unit( p1[ 0 ] - p3[ 0 ], p1[ 1 ] - p3[ 1 ], p1[ 2 ] - p3[ 2 ], nrm, v );  // plane 1: the right side
    track_cross_unit( p2[ 0 ] - p0[ 0 ], p2[ 1 ] - p0[ 1 ], p2[ 2 ] - p0[ 2 ], nrm, q );  // plane 2: the left side
    track_cross_unit( p3[ 0 ] - p2[ 0 ], p3[ 1 ] - p2[ 1 ], p3[ 2 ] - p2[ 2 ], nrm, x );  // plane 3: the end edge
    // d = -((y . y + x . x) + z . z) through the plane's point: p1, p1, p2, p2
    const float* through[ 4 ] = { p1, p1, p2, p2 };
    const float* normal[ 4 ] = { u, v, q, x };
    float d3 = 0.0f;
    for ( int k = 0; k < 4; ++k )
    {
        const float* pt = through[ k ];
        const float* nn = normal[ k ];
        const float d = -( ( pt[ 1 ] * nn[ 1 ] + pt[ 0 ] * nn[ 0 ] ) + pt[ 2 ] * nn[ 2 ] );
        state_put_f( im, 0x40 + 16 * k, nn[ 0 ] );
        state_put_f( im, 0x44 + 16 * k, nn[ 1 ] );
        state_put_f( im, 0x48 + 16 * k, nn[ 2 ] );
        state_put_f( im, 0x4C + 16 * k, d );
        d3 = d;
    }
    float dir[ 3 ] = { state_f( im, 0x90 ) - state_f( im, 0x80 ), state_f( im, 0x94 ) - state_f( im, 0x84 ),
        state_f( im, 0x98 ) - state_f( im, 0x88 ) };
    const float len = hull_sqrt( ( dir[ 0 ] * dir[ 0 ] + dir[ 1 ] * dir[ 1 ] ) + dir[ 2 ] * dir[ 2 ] );
    const float inv = 1.0f / len;
    state_put_f( im, 0xA0, dir[ 0 ] * inv );
    state_put_f( im, 0xA4, dir[ 1 ] * inv );
    state_put_f( im, 0xA8, dir[ 2 ] * inv );
    state_put_f( im, 0xAC, d3 );
    state_put_f( im, 0xB0, len );
    const float w0x = p1[ 0 ] - p0[ 0 ], w0y = p1[ 1 ] - p0[ 1 ], w0z = p1[ 2 ] - p0[ 2 ];
    state_put_f( im, 0xC0, hull_sqrt( ( w0x * w0x + w0y * w0y ) + w0z * w0z ) );
    const float w1x = p3[ 0 ] - p2[ 0 ], w1y = p3[ 1 ] - p2[ 1 ], w1z = p3[ 2 ] - p2[ 2 ];
    state_put_f( im, 0xC4, hull_sqrt( ( w1x * w1x + w1y * w1y ) + w1z * w1z ) );
}

// Slot 20 0x4D0940: the first sector whose +0x80 and +0x90 x, y, z equal the
// two positions (ucomiss, ordered), or -1.
inline int track_find_sector( const track_manager_t& tm, const float* from, const float* to )
{
    for ( std::size_t k = 0; k < tm.sectors.size( ); ++k )
    {
        const std::uint8_t* im = tm.sectors[ k ].image;
        if ( state_f( im, 0x80 ) == from[ 0 ] && state_f( im, 0x84 ) == from[ 1 ] && state_f( im, 0x88 ) == from[ 2 ] &&
             state_f( im, 0x90 ) == to[ 0 ] && state_f( im, 0x94 ) == to[ 1 ] && state_f( im, 0x98 ) == to[ 2 ] )
            return static_cast< int >( k );
    }
    return -1;
}

// Slot 5 0x4CFAF0 (the waypoint): for NextA, then NextB, unless slot 20
// already holds a sector between the two positions, a new sector (index =
// the count before the push, at +0xCC and in the waypoint's +0x98 / +0x9C)
// with the waypoint's speed and NextA's / NextB's, and Branch A / B
// Probability; on a Start waypoint it becomes the start (+0x14). Allocation
// failure paths (operator new null) are not modelled.
inline void track_add_waypoint( track_manager_t& tm, int wi )
{
    const float mph = 0.44703999f;
    for ( int branch = 0; branch < 2; ++branch )
    {
        track_waypoint_t& w = tm.waypoints[ wi ];
        const int ni = branch == 0 ? w.next_a : w.next_b;
        if ( ni < 0 )
            continue;
        const track_waypoint_t& n = tm.waypoints[ ni ];
        if ( track_find_sector( tm, w.position, n.position ) >= 0 )
            continue;
        track_sector_t s;
        track_sector_build( s, w, n, w.p.speed_hint * mph, n.p.speed_hint * mph, branch == 0 ? w.p.branch_a : w.p.branch_b );
        s.waypoint = wi;
        const std::int32_t index = static_cast< std::int32_t >( tm.sectors.size( ) );
        track_put_u32( s.image, 0xCC, static_cast< std::uint32_t >( index ) );
        ( branch == 0 ? w.sector_a : w.sector_b ) = index;
        tm.sectors.push_back( std::move( s ) );
        if ( w.p.start )
            tm.start = index;
    }
}

// Slot 6 0x4CFD60 (the Start waypoint): a std::deque used as a STACK (push_back
// 0x4805C0, back 0x4BC130 / 0x4BC2E0, pop_back) from the start; each popped
// waypoint with Start, CheckPoint or Finish set is appended to the checkpoint
// list unless already in it; then NextA, then NextB, are pushed when set and
// not the start waypoint. No visited set (a waypoint two branches reach is
// walked twice). SUBSTITUTION: std::vector for the MSVC deque (library code).
inline void track_collect_checkpoints( track_manager_t& tm, int start )
{
    std::vector< int > stack{ start };
    while ( !stack.empty( ) )
    {
        const int at = stack.back( );
        stack.pop_back( );
        const track_waypoint_t& w = tm.waypoints[ at ];
        if ( w.p.start || w.p.checkpoint || w.p.finish )
        {
            bool fresh = true;
            for ( int c : tm.checkpoints )
                if ( c == at )
                    fresh = false;
            if ( fresh )
                tm.checkpoints.push_back( at );
        }
        if ( w.next_a >= 0 && w.next_a != start )
            stack.push_back( w.next_a );
        if ( w.next_b >= 0 && w.next_b != start )
            stack.push_back( w.next_b );
    }
}

// Slot 19 0x4D0900: the waypoint's index in the checkpoint list, or -1.
inline std::int32_t track_checkpoint_index( const track_manager_t& tm, int waypoint )
{
    for ( std::size_t k = 0; k < tm.checkpoints.size( ); ++k )
        if ( tm.checkpoints[ k ] == waypoint )
            return static_cast< std::int32_t >( k );
    return -1;
}

// The waypoint's gameInitialize, slot 6 0x432400: the branch probabilities
// normalised (1 / (a + b) once, a x it, it x b), then slot 5, and slot 6 when
// it is the Start waypoint.
inline void track_waypoint_initialize( track_manager_t& tm, int wi )
{
    track_waypoint_t& w = tm.waypoints[ wi ];
    const float inv = 1.0f / ( w.p.branch_a + w.p.branch_b );
    w.p.branch_a = w.p.branch_a * inv;
    w.p.branch_b = inv * w.p.branch_b;
    track_add_waypoint( tm, wi );
    if ( tm.waypoints[ wi ].p.start )
        track_collect_checkpoints( tm, wi );
}

inline bool track_end_of_path( const track_manager_t& tm, const track_sector_t& s )
{
    return s.next.empty( ) || s.next[ 0 ] == tm.start;
}

// Slot 7 0x4CFFD0. (1) +0x1C = 0; every ordered pair i != j whose j's +0x00 /
// +0x10 x, y, z equal i's +0x20 / +0x30 (ucomiss, ordered): j appended to i's
// next list, i to j's prev list. (2) No start: return false (the state is
// left). (3) The end: from the start follow next[0] while there is one, until
// it returns to the start. (4) Distances: a deque as a STACK seeded, in sector
// order, with every sector at an end of the path (no next, or next[0] the
// start); each popped sector's +0xBC = +0xB0 + (0 at an end, else the least
// next +0xBC: comiss, strict), then its prev sectors except the end sector are
// pushed - no visited set, a sector is relaxed again each time it is reached.
// (5) +0x1C = maxss over every sector's +0xBC. (6) Checkpoint ids: a queue
// (an aligned array popped from the front by memcpy) from the start; the id
// carried across pops (-1 first) becomes slot 19's index of the popped
// sector's waypoint when that has Start, CheckPoint or Finish, and is stored
// in +0xD0; unless at an end, every next sector whose prev[0] is the popped
// one is queued. (7) The state = 2. SUBSTITUTION: std::vector for the deque
// and the aligned array (library code).
inline bool track_build( track_manager_t& tm )
{
    tm.length = 0.0f;
    const int count = static_cast< int >( tm.sectors.size( ) );
    for ( int i = 0; i < count; ++i )
        for ( int j = 0; j < count; ++j )
        {
            if ( i == j )
                continue;
            const std::uint8_t* a = tm.sectors[ i ].image;
            const std::uint8_t* b = tm.sectors[ j ].image;
            if ( state_f( b, 0x00 ) == state_f( a, 0x20 ) && state_f( b, 0x04 ) == state_f( a, 0x24 ) && state_f( b, 0x08 ) == state_f( a, 0x28 ) &&
                 state_f( b, 0x10 ) == state_f( a, 0x30 ) && state_f( b, 0x14 ) == state_f( a, 0x34 ) && state_f( b, 0x18 ) == state_f( a, 0x38 ) )
            {
                tm.sectors[ i ].next.push_back( j );
                tm.sectors[ j ].prev.push_back( i );
            }
        }
    for ( track_sector_t& s : tm.sectors )
    {
        track_put_u32( s.image, 0xDC, static_cast< std::uint32_t >( s.next.size( ) ) );
        track_put_u32( s.image, 0xE8, static_cast< std::uint32_t >( s.prev.size( ) ) );
    }
    if ( tm.start < 0 )
        return false;
    int at = tm.start;
    do
    {
        tm.end = at;
        if ( tm.sectors[ at ].next.empty( ) )
            break;
        at = tm.sectors[ at ].next[ 0 ];
    } while ( at != tm.start );
    std::vector< int > stack;
    for ( int k = 0; k < count; ++k )
        if ( track_end_of_path( tm, tm.sectors[ k ] ) )
            stack.push_back( k );
    while ( !stack.empty( ) )
    {
        const int k = stack.back( );
        stack.pop_back( );
        track_sector_t& s = tm.sectors[ k ];
        float least = 0.0f;
        if ( !track_end_of_path( tm, s ) )
        {
            least = 3.4028235e38f;
            for ( int n : s.next )
            {
                const float d = state_f( tm.sectors[ n ].image, 0xBC );
                if ( least > d )
                    least = d;
            }
        }
        state_put_f( s.image, 0xBC, state_f( s.image, 0xB0 ) + least );
        for ( int p : s.prev )
            if ( p != tm.end )
                stack.push_back( p );
    }
    for ( const track_sector_t& s : tm.sectors )
        tm.length = track_maxss( tm.length, state_f( s.image, 0xBC ) );
    std::vector< int > queue{ tm.start };
    std::int32_t id = -1;
    do
    {
        const int k = queue.front( );
        queue.erase( queue.begin( ) );
        track_sector_t& s = tm.sectors[ k ];
        const waypoint_properties_t& p = tm.waypoints[ s.waypoint ].p;
        if ( p.checkpoint || p.start || p.finish )
            id = track_checkpoint_index( tm, s.waypoint );
        track_put_u32( s.image, 0xD0, static_cast< std::uint32_t >( id ) );
        if ( !track_end_of_path( tm, s ) )
            for ( int n : s.next )
                if ( tm.sectors[ n ].prev[ 0 ] == k )
                    queue.push_back( n );
    } while ( !queue.empty( ) );
    tm.state = 2;
    return true;
}

// The queries the AI manager and the race read: slot 11 0x4B65B0 / slot 12
// 0x4B65C0 (the start / end sectors - VuAiManagerImpl's 0x4034B0 calls them
// as vt+0x2C / +0x30), slot 8 0x4D0720 (built), slot 9 0x4D0730 (the end
// sector has a next: a circuit), slot 13 0x4D0800 (the length), slot 14
// 0x4D0810 (the first checkpoint or none), slot 18 0x4D0880 (sector by
// index), slot 16 0x4D0880 (checkpoint by index), slot 17 0x4D0890 (the
// checkpoint count).
inline int track_start_sector( const track_manager_t& tm ) { return tm.start; }
inline int track_end_sector( const track_manager_t& tm ) { return tm.end; }
inline bool track_is_built( const track_manager_t& tm ) { return tm.state == 2; }
inline bool track_is_circuit( const track_manager_t& tm ) { return !tm.sectors[ tm.end ].next.empty( ); }
inline float track_length( const track_manager_t& tm ) { return tm.length; }
inline int track_first_checkpoint( const track_manager_t& tm ) { return tm.checkpoints.empty( ) ? -1 : tm.checkpoints[ 0 ]; }

inline float track_minss( float a, float b ) { return _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); }

// One edge of 0x4D17E0: t = maxss(0, minss(1, ((e.y d.y + e.x d.x) + e.z d.z)
// / ((e.y e.y + e.x e.x) + e.z e.z))) with e = to - from, d = p - from; out =
// (from + e t) - p per lane.
inline void track_edge( const float* from, const float* to, const float* p, float* out )
{
    const float ex = to[ 0 ] - from[ 0 ], ey = to[ 1 ] - from[ 1 ], ez = to[ 2 ] - from[ 2 ];
    const float num = ( ey * ( p[ 1 ] - from[ 1 ] ) + ex * ( p[ 0 ] - from[ 0 ] ) ) + ez * ( p[ 2 ] - from[ 2 ] );
    const float den = ( ey * ey + ex * ex ) + ez * ez;
    const float t = track_maxss( 0.0f, track_minss( 1.0f, num / den ) );
    out[ 0 ] = ( from[ 0 ] + ex * t ) - p[ 0 ];
    out[ 1 ] = ( from[ 1 ] + ey * t ) - p[ 1 ];
    out[ 2 ] = ( from[ 2 ] + ez * t ) - p[ 2 ];
}

inline float track_edge_length( const float* d )
{
    return hull_sqrt( ( d[ 1 ] * d[ 1 ] + d[ 0 ] * d[ 0 ] ) + d[ 2 ] * d[ 2 ] );
}

// 0x4D17E0 (thiscall, the sector; point*; retn 4; xmm0), from its
// disassembly: m = minss over the four planes' distances ((b . p.y + a .
// p.x) + c . p.z) + d, from FLT_MAX; m >= 0 (ordered: inside every plane) is
// 0; else (a NaN too) the least distance to the four edges p2 -> p3, p3 -> p1,
// p1 -> p0, p0 -> p2 (the points +0x00 / +0x10 / +0x20 / +0x30), taken as
// minss(|p1 p0|, |p0 p2|), then |p3 p1| against it, then |p2 p3|.
inline float track_distance( const std::uint8_t* s, const float* p )
{
    float m = 3.4028235e38f;
    for ( int k = 0; k < 4; ++k )
    {
        const int at = 0x40 + 16 * k;
        const float d = ( ( state_f( s, at + 4 ) * p[ 1 ] + p[ 0 ] * state_f( s, at ) ) + state_f( s, at + 8 ) * p[ 2 ] ) + state_f( s, at + 12 );
        m = track_minss( m, d );
    }
    if ( _mm_comige_ss( _mm_set_ss( m ), _mm_set_ss( 0.0f ) ) )
        return 0.0f;
    float p0[ 3 ], p1[ 3 ], p2[ 3 ], p3[ 3 ];
    for ( int lane = 0; lane < 3; ++lane )
    {
        p0[ lane ] = state_f( s, 0x00 + 4 * lane );
        p1[ lane ] = state_f( s, 0x10 + 4 * lane );
        p2[ lane ] = state_f( s, 0x20 + 4 * lane );
        p3[ lane ] = state_f( s, 0x30 + 4 * lane );
    }
    float a[ 3 ], b[ 3 ], c[ 3 ], d[ 3 ];
    track_edge( p2, p3, p, a );
    track_edge( p3, p1, p, b );
    track_edge( p1, p0, p, c );
    track_edge( p0, p2, p, d );
    float r = track_minss( track_edge_length( c ), track_edge_length( d ) );
    r = track_minss( track_edge_length( b ), r );
    return track_minss( track_edge_length( a ), r );
}

// Slots 21 0x4D09F0 / 22 0x4D0AF0 (point*, checkpoint id or -1; retn 8): over
// the sectors in order - those of that checkpoint id (+0xD0) unless -1, and
// for slot 22 only those whose waypoint has Allow Recovery - the one nearest
// (0x4D17E0; best > d, comiss, from FLT_MAX); on a tie (ucomiss, ordered) the
// later one when the point's fraction along it (plane 0 / (plane 3 + plane 0))
// is within [0, 1] (both comiss). -1 for none. The respawn 0x41D790 asks
// slot 22 with the boat's position and its driver's +0x78.
inline int track_nearest( const track_manager_t& tm, const float* p, std::int32_t checkpoint, bool recovery )
{
    float best = 3.4028235e38f;
    int pick = -1;
    for ( std::size_t k = 0; k < tm.sectors.size( ); ++k )
    {
        const track_sector_t& s = tm.sectors[ k ];
        if ( checkpoint != -1 && static_cast< std::int32_t >( track_u32( s.image, 0xD0 ) ) != checkpoint )
            continue;
        if ( recovery && !tm.waypoints[ s.waypoint ].p.allow_recovery )
            continue;
        const float d = track_distance( s.image, p );
        if ( best > d )
        {
            best = d;
            pick = static_cast< int >( k );
        }
        else if ( d == best )
        {
            const std::uint8_t* q = s.image;
            const float d0 = ( ( state_f( q, 0x40 ) * p[ 0 ] + state_f( q, 0x44 ) * p[ 1 ] ) + state_f( q, 0x48 ) * p[ 2 ] ) + state_f( q, 0x4C );
            const float d3 = ( ( ( state_f( q, 0x70 ) * p[ 0 ] + state_f( q, 0x74 ) * p[ 1 ] ) + state_f( q, 0x78 ) * p[ 2 ] ) + state_f( q, 0x7C ) ) + d0;
            const float f = d0 / d3;
            if ( _mm_comige_ss( _mm_set_ss( f ), _mm_set_ss( 0.0f ) ) && _mm_comige_ss( _mm_set_ss( 1.0f ), _mm_set_ss( f ) ) )
                pick = static_cast< int >( k );
        }
    }
    return pick;
}

// The whole build from a loaded entity tree, as the race does it: every
// VuAiWaypointEntity in VuEntity::gameInitialize 0x500B10's order (children
// first, +0x38 order) gets its postLoad's edge points from its transform (the
// postLoad and the watcher run during the load in retail; both read only the
// final transform here - ASSUMED equal: the waypoints' ancestors carry no
// transform), its refs resolved to ordinals, then its slot 6; then slot 7.
// A waypoint without a transform, a property state or a known world cannot be
// placed: the build is refused (false), never stood in for.
inline bool track_build_from_tree( entity_t& root, track_manager_t& tm )
{
    tm = track_manager_t{ };
    std::vector< entity_t* > order;
    const auto post = [ & ]( const auto& self, entity_t& e ) -> void {
        for ( auto& child : e.m_children )
            self( self, *child );
        if ( e.m_type == k_waypoint_class )
            order.push_back( &e );
    };
    post( post, root );
    for ( entity_t* e : order )
    {
        const transform_component_t* t = entity_transform( *e );
        if ( !t || !e->m_state || e->m_world_unknown )
            return false;
        track_waypoint_t w;
        w.entity = e;
        w.p = static_cast< const waypoint_state_t* >( e->m_state.get( ) )->value;
        std::memcpy( w.position, t->world + 12, 16 );
        waypoint_post_load( w, *t );
        tm.waypoints.push_back( w );
    }
    for ( track_waypoint_t& w : tm.waypoints )
    {
        const script_component_t* sc = script_of( *w.entity );
        if ( !sc )
            return false;
        const auto target = [ & ]( const char* name ) -> int {
            const int r = script_find_ref( *sc, name );
            if ( r < 0 || !sc->m_refs[ r ].m_target )
                return -1;
            for ( std::size_t k = 0; k < tm.waypoints.size( ); ++k )
                if ( tm.waypoints[ k ].entity == sc->m_refs[ r ].m_target )
                    return static_cast< int >( k );
            return -1;
        };
        w.next_a = target( "NextA" );
        w.next_b = target( "NextB" );
    }
    for ( std::size_t k = 0; k < tm.waypoints.size( ); ++k )
        track_waypoint_initialize( tm, static_cast< int >( k ) );
    return track_build( tm );
}

// ---- VuAiWaypointEntity's input plugs (ctor 0x431770: VuScriptComponent +0x5C) ----
//
// "AllowRecovery" 0x432740 / "DisallowRecovery" 0x432760 (thiscall(this,
// VuRetVal* out, VuParams const&), retn 8; *out = 0): +0x8B "Allow
// Recovery" = 1 / 0 (what track slot 22's respawn search filters on).
inline void waypoint_allow_recovery( track_waypoint_t& w, bool allow )
{
    w.p.allow_recovery = allow ? 1 : 0;
}

// 0x432780 (thiscall; a in xmm1, b in xmm2): the branch sectors (+0x98 /
// +0x9C, -1 none) through track slot 23 0x4D0C00 (the sector array's entry,
// unchecked - false, UNKNOWN, past it); a = 0 without branch A, b = 0
// without B; +0xA0 = a, then inv = 1 / (b + a), +0xA0 = +0xA0 x inv, +0xA4 =
// inv x b; each branch sector's probability +0xC8 = its value; the AI
// manager's slot 16 (vt+0x40, 0x404220: fleet_replan) with 2 and the list of
// the sectors that exist (compacted, the rest -1) - `replan`.
inline bool waypoint_set_branches( track_manager_t& tm, track_waypoint_t& w, float a, float b,
                                   const std::function< void( const int ( &sectors )[ 2 ] ) >& replan )
{
    const int count = static_cast< int >( tm.sectors.size( ) );
    if ( ( w.sector_a != -1 && ( w.sector_a < 0 || w.sector_a >= count ) ) || ( w.sector_b != -1 && ( w.sector_b < 0 || w.sector_b >= count ) ) )
        return false;
    const int sa = w.sector_a, sb = w.sector_b;
    w.p.branch_a = sa != -1 ? a : 0.0f;
    if ( sb == -1 )
        b = 0.0f;
    const float inv = 1.0f / ( b + w.p.branch_a );
    w.p.branch_a = w.p.branch_a * inv;
    w.p.branch_b = inv * b;
    int list[ 2 ] = { -1, -1 };
    int n = 0;
    if ( sa != -1 )
    {
        state_put_f( tm.sectors[ static_cast< std::size_t >( sa ) ].image, 0xC8, w.p.branch_a );
        list[ n++ ] = sa;
    }
    if ( sb != -1 )
    {
        state_put_f( tm.sectors[ static_cast< std::size_t >( sb ) ].image, 0xC8, w.p.branch_b );
        list[ n ] = sb;
    }
    if ( replan )
        replan( list );
    return true;
}

// "UpdateBranchProbabilities" 0x4325D0 (thiscall(this, VuRetVal* out,
// VuParams const&), retn 8; *out = 0). The params (+0x80 the size) read in
// order, each only while the size left is > 0: a bool (type 3: its byte,
// 5 bytes consumed), a second bool, a float (type 2: 8 consumed), a second
// float; a type that does not match consumes nothing (the next read sees
// it again). a = the first float when the first bool's byte is set, else
// +0xA0; b likewise from the second, else +0xA4; then 0x432780.
inline bool waypoint_update_branches( track_manager_t& tm, track_waypoint_t& w, const std::uint8_t* params,
                                      const std::function< void( const int ( &sectors )[ 2 ] ) >& replan )
{
    std::int32_t left;
    std::memcpy( &left, params + 0x80, 4 );
    const std::uint8_t* at = params;
    std::uint8_t use[ 2 ] = { 0, 0 };
    for ( int k = 0; k < 2; ++k )
        if ( left > 0 && track_u32( at, 0 ) == 3 )
        {
            use[ k ] = at[ 4 ];
            at += 5;
            left -= 5;
        }
    float value[ 2 ] = { 0.0f, 0.0f };
    if ( left > 0 && track_u32( at, 0 ) == 2 )
    {
        std::memcpy( &value[ 0 ], at + 4, 4 );
        at += 8;
        left -= 8;
    }
    if ( left > 0 && track_u32( at, 0 ) == 2 )
        std::memcpy( &value[ 1 ], at + 4, 4 );
    const float a = use[ 0 ] ? value[ 0 ] : w.p.branch_a;
    const float b = use[ 1 ] ? value[ 1 ] : w.p.branch_b;
    return waypoint_set_branches( tm, w, a, b, replan );
}
// ---- VuAiWaypointEntity's dtor (vtable 0x739D44 slot 0, scalar-deleting: thiscall(entity, flags), retn 4; eax =
// this), on retail's words (boat batch 28) ----
//
// 0x4320E0: the two 3-element arrays the ctor 0x431770 builds with `eh vector constructor iterator` 0x631110 (element
// 0x10 bytes, ctor 0x432DB0) - +0xE4 first, then +0xA8 - destroyed by `eh vector destructor iterator` 0x630EF0 with
// the element dtor 0x432DD0, then VuEntity's dtor 0x500870, then with flags & 1 operator delete(this).
// 0x432DD0 (thiscall element): an array { +0 count, +4 capacity, +8 data, +0xC owned byte } of 0x1C-byte records
// whose std::string is at +4: 0x432EC0 (thiscall array; an unused stack word, count; retn 8) resets the first
// `count` strings (the heap text freed when the capacity +0x14 is >= 16; then capacity 15, size 0, the first byte 0),
// then with the owned byte the data is _aligned_free'd; +0 / +4 / +8 = 0, the owned byte = 1.
// The imports and 0x500870 are hooks, in retail's order.
struct waypoint_dtor_hooks_t
{
    std::function< void( std::uint32_t ) > free_block;     // operator delete
    std::function< void( std::uint32_t ) > aligned_free;   // _aligned_free
    std::function< void( ) > entity_dtor;                  // 0x500870
};

// 0x432DD0 (with 0x432EC0).
inline void waypoint_array_destroy( const vu_words_t& w, std::uint32_t a, const waypoint_dtor_hooks_t& h )
{
    const std::int32_t count = static_cast< std::int32_t >( w.get( a ) );
    for ( std::int32_t k = 0; k < count; ++k )
    {
        const std::uint32_t s = w.get( a + 8 ) + 0x1Cu * static_cast< std::uint32_t >( k ) + 4u;
        if ( w.get( s + 0x14 ) >= 0x10u && h.free_block )
            h.free_block( w.get( s ) );
        w.put( s + 0x14, 0xFu );
        w.put( s + 0x10, 0u );
        w.put( s, w.get( s ) & 0xFFFFFF00u );
    }
    if ( ( w.get( a + 0xC ) & 0xFFu ) != 0 && h.aligned_free )
        h.aligned_free( w.get( a + 8 ) );
    w.put( a, 0u );
    w.put( a + 4, 0u );
    w.put( a + 8, 0u );
    w.put( a + 0xC, ( w.get( a + 0xC ) & 0xFFFFFF00u ) | 1u );
}

// 0x4320E0. `eh vector destructor iterator` walks an array from its last element to its first.
inline std::uint32_t waypoint_destroy( const vu_words_t& w, std::uint32_t self, std::uint32_t flags, const waypoint_dtor_hooks_t& h )
{
    for ( std::uint32_t base : { 0xE4u, 0xA8u } )
        for ( std::uint32_t k = 3; k-- > 0; )
            waypoint_array_destroy( w, self + base + 0x10u * k, h );
    if ( h.entity_dtor )
        h.entity_dtor( );
    if ( ( flags & 1u ) && h.free_block )
        h.free_block( self );
    return self;
}