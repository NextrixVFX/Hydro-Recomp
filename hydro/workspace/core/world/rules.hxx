#pragma once

#include <workspace/core/boat/manager.hxx>
#include <workspace/core/boat/track.hxx>
#include <workspace/core/entity/node.hxx>
#include <workspace/core/entity/property.hxx>
#include <workspace/core/ui/config.hxx>
#include <workspace/core/ui/fsm.hxx>
#include <workspace/core/ui/split.hxx>
#include <workspace/core/world/awards.hxx>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <emmintrin.h>
#include <xmmintrin.h>

// THE RACE RULES - VuGame's per-frame race bookkeeping and VuRaceGame's own
// part, recreated from the disassembly / Hex-Rays (world batch 19):
//
//   VuGame's tick 0x482F10 (__usercall: ecx the game, xmm1 dt), reached every
//   frame from VuActionGameMode's "Run" state tick 0x4BAB50, which the game
//   mode manager's Decision tick 0x4CBFD0 runs through the mode's slot 4
//   0x4B99E0 (its FSM): 0x483230 (each boat's distances along the track),
//   0x483540 (the checkpoints: laps, lap times, the finish), 0x483AD0 (the
//   sixth finished AI boat hidden), the game's slot 7 (VuRaceGame 0x489A40: its
//   +0x6F8 state machine), then VuGame's two own state machines (+0x0C, +0x67C:
//   hooks here), then each boat's +0x4DC = its transform's world (the previous
//   position the next frame's checkpoint test starts from).
//
// The boat image fields (the 0x520 VuBoatEntity) these read and write:
//   +0x308 the input-off count (the drivers ignore input while non-zero),
//   +0x32F finished, +0x4AC the lap (0 before the start line), +0x4B0 the lap
//   time, +0x4B4 the race time, +0x4B8 the best lap, +0x4BC the distance left
//   in the lap, +0x4C0 the distance done in the lap, +0x4C4 the distance left in
//   the race, +0x4C8 the distance done in the race, +0x4CC the place (0 first),
//   +0x4D8 the next checkpoint (a VuAiWaypointEntity; here a waypoint index, -1
//   none), +0x4DC the world last frame (row 3 at +0x50C).

inline float rules_f( const std::uint8_t* b, int at )
{
    float f;
    std::memcpy( &f, b + at, 4 );
    return f;
}
inline void rules_put_f( std::uint8_t* b, int at, float v ) { std::memcpy( b + at, &v, 4 ); }
inline std::int32_t rules_i32( const std::uint8_t* b, int at )
{
    std::int32_t v;
    std::memcpy( &v, b + at, 4 );
    return v;
}
inline void rules_put_i32( std::uint8_t* b, int at, std::int32_t v ) { std::memcpy( b + at, &v, 4 ); }
inline float rules_minss( float a, float b ) { return _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); }

// One of the game's boats (+0x38 list, +0x3C count) as the rules see it.
// `sector` is the driver's vt+0x5C: VuHumanDriver 0x40ED30 = its +0x84 (the
// human's current sector), VuAiDriver 0x40BD20 = +0x90 > 0 ? [+0x8C][0] : null
// (its plan's first sector) - the sector's image or null. `human` the driver's
// slot 12 predicate.
struct race_rules_boat_t
{
    std::uint8_t* boat = nullptr;
    const std::uint8_t* transform = nullptr;   // boat +0x50's component: world at +0x50, row 3 at +0x80
    std::uint8_t* driver = nullptr;            // boat +0x220's image (+0x78 written)
    const std::uint8_t* sector = nullptr;
    int checkpoint = -1;                       // +0x4D8
    bool human = false;
    int drive = -1;                            // ours: its index in the session's boats when not its own (a ghost, made after the nuisance boats)
};

// The VuGame fields the rules read: +0x44 a circuit, +0x48 the track length,
// +0x4C the lap count; VuRaceGame's +0x720 the place order (boat indices).
struct race_rules_game_t
{
    std::uint8_t circuit = 0;
    float length = 0.0f;
    std::int32_t laps = 0;
    std::vector< int > order;
};

// ---- 0x483230: the distances --------------------------------------------------
//
// For each boat whose driver has a sector q (row 3 p of its world): a =
// ((q+0x44 p.y + q+0x40 p.x) + q+0x48 p.z) + q+0x4C (the sector's entry
// plane), b = ((q+0x74 p.y + p.x q+0x70) + q+0x78 p.z) + q+0x7C (its exit
// plane), f = minss(1.0, a / (b + a)); +0x4BC = q+0xBC - q+0xB0 f; +0x4C0 =
// length - that. A circuit: +0x4C8 = (float)lap x length - +0x4BC, +0x4C4 =
// (float)laps x length - +0x4C8; else +0x4C8 = +0x4C0, +0x4C4 = +0x4BC.
inline void race_rules_distances( const race_rules_game_t& g, std::vector< race_rules_boat_t >& boats )
{
    for ( race_rules_boat_t& r : boats )
    {
        const std::uint8_t* q = r.sector;
        if ( !q )
            continue;
        const float x = rules_f( r.transform, 0x80 ), y = rules_f( r.transform, 0x84 ), z = rules_f( r.transform, 0x88 );
        const float a = ( ( rules_f( q, 0x44 ) * y + rules_f( q, 0x40 ) * x ) + rules_f( q, 0x48 ) * z ) + rules_f( q, 0x4C );
        const float b = ( ( rules_f( q, 0x74 ) * y + x * rules_f( q, 0x70 ) ) + rules_f( q, 0x78 ) * z ) + rules_f( q, 0x7C );
        const float f = rules_minss( 1.0f, a / ( b + a ) );
        const float left = rules_f( q, 0xBC ) - rules_f( q, 0xB0 ) * f;
        rules_put_f( r.boat, 0x4BC, left );
        const float done = g.length - left;
        rules_put_f( r.boat, 0x4C0, done );
        if ( g.circuit )
        {
            const float total = static_cast< float >( rules_i32( r.boat, 0x4AC ) ) * g.length - rules_f( r.boat, 0x4BC );
            rules_put_f( r.boat, 0x4C8, total );
            rules_put_f( r.boat, 0x4C4, static_cast< float >( g.laps ) * g.length - total );
        }
        else
        {
            rules_put_f( r.boat, 0x4C8, done );
            rules_put_f( r.boat, 0x4C4, left );
        }
    }
}

// ---- 0x483540: the checkpoints ------------------------------------------------
//
// A checkpoint as the test reads its VuAiWaypointEntity: the transform's world
// row 1 (+0x60, the line's normal) and row 3 (+0x80, its centre), +0x8A Finish,
// +0x90 CheckPoint Width, +0x94 CheckPoint Height.
struct race_checkpoint_view_t
{
    float normal[ 3 ];
    float origin[ 3 ];
    std::uint8_t finish;
    float width, height;
};

struct race_checkpoint_hooks_t
{
    // The checkpoint's view (false: unknown - the boat is skipped, counted).
    std::function< bool( int waypoint, race_checkpoint_view_t& ) > view;
    // The track manager g 0x758FF4: slot 19 vt+0x4C 0x4D0900 (the checkpoint's
    // id) and slot 15 vt+0x3C 0x4D0820 (the next checkpoint, -1 none).
    std::function< std::int32_t( int waypoint ) > index;
    std::function< int( int waypoint ) > next;
    // The game's vt+0x3C (VuRaceGame slot 15 0x48B180, the split / result
    // display) with the boat and the checkpoint crossed.
    std::function< void( int boat, int waypoint ) > crossed;
    // 0x483B60's g_vuaimanagerimpl vt+0x30 (slot 12 0x4036C0) on the boat.
    std::function< void( int boat ) > finished;
    int unknown = 0;
};

// 0x483B60 (cdecl boat): +0x308 += 1, +0x32F = 1, the AI manager's slot 12.
inline void race_rules_finish( race_rules_boat_t& r, int index, const race_checkpoint_hooks_t& h )
{
    rules_put_i32( r.boat, 0x308, rules_i32( r.boat, 0x308 ) + 1 );
    r.boat[ 0x32F ] = 1;
    if ( h.finished )
        h.finished( index );
}

// For each unfinished boat with a next checkpoint c: the step from P (+0x50C,
// last frame) to C (the world's row 3) against c's line. a = ((n0 (P0 - o0) +
// n1 (P1 - o1)) + n2 (P2 - o2)), b the same at C; comiss 0, b a / jb: not
// crossed. The crossing point: |a| = A, inv = 1 / (|b| + A), px = inv ((C0 -
// P0) A) + P0, py likewise; inside when width x 0.5 > VuMath_sqrt(ry^2 + rx^2)
// (ry = py - o1, rx = px - o0) and C2 - o2 > -10.0 and height > C2 - o2. Then
// driver +0x78 = slot 19(c), +0x4D8 = slot 15(c), the game's vt+0x3C(boat, c);
// and when c is a Finish or the boat is still on lap 0: with lap >= 1, +0x4B8 =
// minss(+0x4B8, +0x4B0) and +0x4B0 = 0; lap == the lap count -> 0x483B60; lap
// += 1; +0x4BC = the track length.
inline void race_rules_checkpoints( const race_rules_game_t& g, std::vector< race_rules_boat_t >& boats, race_checkpoint_hooks_t& h )
{
    for ( std::size_t k = 0; k < boats.size( ); ++k )
    {
        race_rules_boat_t& r = boats[ k ];
        if ( r.boat[ 0x32F ] || r.checkpoint < 0 )
            continue;
        race_checkpoint_view_t c{};
        if ( !h.view || !h.view( r.checkpoint, c ) )
        {
            ++h.unknown;
            continue;
        }
        const float p0 = rules_f( r.boat, 0x50C ), p1 = rules_f( r.boat, 0x510 ), p2 = rules_f( r.boat, 0x514 );
        const float c0 = rules_f( r.transform, 0x80 ), c1 = rules_f( r.transform, 0x84 ), c2 = rules_f( r.transform, 0x88 );
        const float a = ( c.normal[ 0 ] * ( p0 - c.origin[ 0 ] ) + c.normal[ 1 ] * ( p1 - c.origin[ 1 ] ) ) + c.normal[ 2 ] * ( p2 - c.origin[ 2 ] );
        const float b = ( c.normal[ 0 ] * ( c0 - c.origin[ 0 ] ) + c.normal[ 1 ] * ( c1 - c.origin[ 1 ] ) ) + c.normal[ 2 ] * ( c2 - c.origin[ 2 ] );
        if ( !( 0.0f >= b * a ) )
            continue;
        const float aa = std::fabs( a );
        const float dx = ( c0 - p0 ) * aa, dy = ( c1 - p1 ) * aa;
        const float inv = 1.0f / ( std::fabs( b ) + aa );
        const float ry = ( inv * dy + p1 ) - c.origin[ 1 ];
        const float rx = ( inv * dx + p0 ) - c.origin[ 0 ];
        const float d = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( ry * ry + rx * rx ) ) );
        if ( !( c.width * 0.5f > d ) )
            continue;
        const float dz = c2 - c.origin[ 2 ];
        if ( !( dz > -10.0f ) || !( c.height > dz ) )
            continue;
        const int crossed = r.checkpoint;
        if ( r.driver && h.index )
            rules_put_i32( r.driver, 0x78, h.index( crossed ) );
        r.checkpoint = h.next ? h.next( crossed ) : -1;
        if ( h.crossed )
            h.crossed( static_cast< int >( k ), crossed );
        const std::int32_t lap = rules_i32( r.boat, 0x4AC );
        if ( !c.finish && lap != 0 )
            continue;
        if ( lap >= 1 )
        {
            rules_put_f( r.boat, 0x4B8, rules_minss( rules_f( r.boat, 0x4B8 ), rules_f( r.boat, 0x4B0 ) ) );
            rules_put_i32( r.boat, 0x4B0, 0 );
        }
        if ( lap == g.laps )
            race_rules_finish( r, static_cast< int >( k ), h );
        rules_put_i32( r.boat, 0x4AC, rules_i32( r.boat, 0x4AC ) + 1 );
        rules_put_f( r.boat, 0x4BC, g.length );
    }
}

// Slot 15 0x4D0820 (thiscall track; waypoint): its index i in the checkpoint
// list (+0x28 data, +0x20 count), none -> null; j = i + 1, taken modulo the
// count on a circuit (slot 9); j past the list -> null; else checkpoints[j].
inline int race_rules_next_checkpoint( const track_manager_t& tm, int waypoint )
{
    const int n = static_cast< int >( tm.checkpoints.size( ) );
    for ( int i = 0; i < n; ++i )
    {
        if ( tm.checkpoints[ static_cast< std::size_t >( i ) ] != waypoint )
            continue;
        int j = i + 1;
        if ( track_is_circuit( tm ) )
            j %= n;
        return j < n ? tm.checkpoints[ static_cast< std::size_t >( j ) ] : -1;
    }
    return -1;
}

// ---- 0x4831D0 / 0x48AB20: the clocks and the places --------------------------
//
// 0x4831D0 (__usercall ecx the game, xmm1 dt): each unfinished boat's +0x4B4
// += dt, and +0x4B0 += dt once its lap is >= 1.
inline void race_rules_clocks( std::vector< race_rules_boat_t >& boats, float dt )
{
    for ( race_rules_boat_t& r : boats )
    {
        if ( r.boat[ 0x32F ] )
            continue;
        rules_put_f( r.boat, 0x4B4, dt + rules_f( r.boat, 0x4B4 ) );
        if ( rules_i32( r.boat, 0x4AC ) >= 1 )
            rules_put_f( r.boat, 0x4B0, rules_f( r.boat, 0x4B0 ) + dt );
    }
}

// The order predicate MSVC's std::sort inlines (0x48BB30 / 0x48B460 /
// 0x48B9C0): a before b when a finished and (b did not, or b's race time
// +0x4B4 is greater: comiss, so a NaN orders nothing); an unfinished a never
// before a finished b; else the higher lap (+0x4AC) first, then the greater
// lap distance left +0x4BC of b (a has less left).
inline bool race_rules_before( const std::uint8_t* a, const std::uint8_t* b )
{
    if ( a[ 0x32F ] )
        return !b[ 0x32F ] || _mm_comigt_ss( _mm_set_ss( rules_f( b, 0x4B4 ) ), _mm_set_ss( rules_f( a, 0x4B4 ) ) );
    if ( b[ 0x32F ] )
        return false;
    const std::int32_t la = rules_i32( a, 0x4AC ), lb = rules_i32( b, 0x4AC );
    if ( la != lb )
        return la > lb;
    return _mm_comigt_ss( _mm_set_ss( rules_f( b, 0x4BC ) ), _mm_set_ss( rules_f( a, 0x4BC ) ) );
}

// 0x48AB20 (thiscall game): std::sort 0x48B360 over the +0x720 order list with
// that predicate, then each boat's +0x4CC = its place. Retail's sort is MSVC's
// introsort; at 32 elements or fewer it is the insertion sort 0x48BB30 alone,
// recreated here 1:1 (an element less than the first is moved to the front,
// else walked back while it is less than its left neighbour). More than 32
// boats takes the partition / heap paths (0x48B460 / 0x48B9C0 / 0x48BAE0) -
// not recreated: false, nothing sorted (UNKNOWN).
inline bool race_rules_places( race_rules_game_t& g, std::vector< race_rules_boat_t >& boats )
{
    std::vector< int >& o = g.order;
    if ( o.size( ) > 32 )
        return false;
    const auto less = [ & ]( int x, int y ) { return race_rules_before( boats[ static_cast< std::size_t >( x ) ].boat, boats[ static_cast< std::size_t >( y ) ].boat ); };
    for ( std::size_t k = 1; k < o.size( ); ++k )
    {
        const int v = o[ k ];
        if ( less( v, o[ 0 ] ) )
        {
            std::memmove( &o[ 1 ], &o[ 0 ], k * sizeof( int ) );
            o[ 0 ] = v;
            continue;
        }
        std::size_t i = k;
        while ( less( v, o[ i - 1 ] ) )
        {
            o[ i ] = o[ i - 1 ];
            --i;
        }
        o[ i ] = v;
    }
    for ( std::size_t p = 0; p < boats.size( ) && p < o.size( ); ++p )
        rules_put_i32( boats[ static_cast< std::size_t >( o[ p ] ) ].boat, 0x4CC, static_cast< std::int32_t >( p ) );
    return true;
}

// ---- 0x483AD0: the sixth finished AI boat ----------------------------------
//
// Over the boat manager's AI list (g 0x758FB8 +0x30 data / +0x34 count): the
// finished (+0x32F) and not yet hidden (+0x494) boats are counted and the one
// with the greatest +0x330 (comiss > the running best, which starts at 0.0;
// the index starts at 0) is remembered; with more than five such boats that
// one is hidden - its draw component (+0x6C) taken from g_vu3ddrawmanager
// 0x7590B8 (vt+0x2C) when its +0x1E is set (then +0x1E = 0), the boat's
// vt+0x48 (slot 18 0x421940, the disable), +0x494 = 1. `hide` is those calls
// on that AI boat index.
inline void race_rules_hide_finished( const std::vector< std::uint8_t* >& ais, const std::function< void( int ai ) >& hide )
{
    int count = 0, best = 0;
    float most = 0.0f;
    for ( std::size_t k = 0; k < ais.size( ); ++k )
    {
        const std::uint8_t* b = ais[ k ];
        if ( !b[ 0x32F ] || b[ 0x494 ] )
            continue;
        ++count;
        const float v = rules_f( b, 0x330 );
        if ( _mm_comigt_ss( _mm_set_ss( v ), _mm_set_ss( most ) ) )
        {
            best = static_cast< int >( k );
            most = v;
        }
    }
    if ( count <= 5 )
        return;
    if ( hide )
        hide( best );
    ais[ static_cast< std::size_t >( best ) ][ 0x494 ] = 1;
}

// ---- VuRaceGame's own part: the ctor 0x489030 and slot 7 0x489A40 ----------
//
// The ctor, past its base 0x480BA0: +0x6F8 .. +0x734 and +0x744 zeroed (the
// +0x6F8 VuFSM's words and the checkpoint-buoy mask); two event methods
// through 0x5156E0 (VuMethod1<VuRaceGame, void, VuParams const&>, vtable
// 0x73F8EC): "OnStartLightsOut" -> 0x48A780, "OnCheckPointBuoyHit" ->
// 0x48A7B0; the FSM's states (addState 0x555850; VuMethod0 vtable 0x73F8DC,
// VuMethod1<float> 0x73F8CC): PreGame {enter 0x48A7F0, exit 0x48A860, tick
// 0x48A8B0, draw 0x486C00}, Game {0x48A910, 0x4872A0, 0x48A970, 0x487370},
// PostGame {0x48AB70, 0x48ADE0, 0x48AE80, 0x48AF30}, Exit {none}; transitions
// (addTransition 0x555920): PreGame -> Game "StartLightsOut", Game -> PostGame
// "HumanFinished", Game -> PostGame "EveryoneFinished", "" (every state) ->
// Exit "Exit"; then the three HUD projects loaded by 0x4B4360 into +0x738
// (HUDs/RacePreGameHUD), +0x73C (HUDs/RaceGameHUD), +0x740
// (HUDs/RacePostGameHUD). The machine is the UI agent's verified VuFSM
// (ui/fsm.hxx c_fsm); the time in the state is its +0x24 (game +0x71C).
//
// What the states reach that is not the rules' (hooks, each named):
//   hud(project, verb): a HUD project's gameInitialize (when its root's +0x14
//     & 4 is clear: g_vu_entity_repository vt+0x18, 0x500B10, 0x501260
//     "OnProjectInitialized"), its release (when set: 0x500B80, repository
//     vt+0x1C), its tick (the root, when it is a VuHUDEntity - RTTI 0x7362AC -
//     vt+0x30(dt)) and its draw (vt+0x34(1.0)) - the HUD's update / drawing is
//     the UI's; the project load / init / release run through the world.
//   broadcast(name, value): PostGame's enter 0x48AB70 after its HUD broadcasts
//     "OnSetAllowPause" (VuEventManager_broadcast 0x505690, VuParams {bool
//     false}: 5 bytes) - to the world's subscribers.
//   platform(call): the rest of 0x48AB70, with a human (boat manager +0x28 not
//     0; human = its +0x24 [0]): the progress manager g 0x758FD8 vt+0x34 (the
//     track +0x68, the boat +0x160, 0, place +0x4CC, +0x4B4, +0x4B8 - the save),
//     the leaderboard 0x4B0650 (track + "Race", -(int)(+0x4B4 x 1000), as int64)
//     when +0x334 is clear, the voice-over g 0x758FEC vt+0x1C (the line chosen
//     below) and vt+0x20 "DidNotUseBoost" (place > 0, +0x328 > 0 ordered, +0x32D
//     clear); its exit 0x48ADE0's achievement 0x4AA1A0 "DingDong" (the track
//     "Hydroelectric", the human first, +0x744 == 0x1F) - saves / online /
//     achievements / audio: platform services (BRIEF). The calls reach the hook
//     as text ("progress <track> <place> <4B4 hex> <4B8 hex>", "leaderboard
//     <name> <value>", "vo <line>", "vo20 <line>", "achievement <name>").
// All of it oracle-verified (case race_game_fsm: the ctor run whole, then the
// machine driven through retail's own handlers).
enum class race_hud_verb_t { initialize, release, tick, draw };

// The ctor's project loads (0x4B4360, in order: +0x738 / +0x73C / +0x740) and
// its two event methods (0x5156E0, VuMethod1<VuRaceGame, void, VuParams const&>
// vtable 0x73F8EC, in order) - VuGame's base 0x480BA0 registers its own
// OnUserNext / OnCheatFinish before them.
inline constexpr const char* k_race_hud_projects[ 3 ] = { "HUDs/RacePreGameHUD", "HUDs/RaceGameHUD", "HUDs/RacePostGameHUD" };

struct race_rules_event_t
{
    const char* name;
    std::uint32_t method;
};

inline constexpr race_rules_event_t k_race_rules_events[ 2 ] = { { "OnStartLightsOut", 0x48A780u }, { "OnCheckPointBuoyHit", 0x48A7B0u } };

struct race_rules_hooks_t
{
    std::function< void( int project, race_hud_verb_t verb, float dt ) > hud;
    std::function< void( const char* name, bool value ) > broadcast;
    std::function< void( const std::string& call ) > platform;
    // The progress manager's record of the three games, the human's (0x48AB70 vt+0x34 "Race", 0x4909B0 vt+0x38 "TimeTrial", 0x48E090 vt+0x3C "Slalom").
    std::function< void( const char* type, const std::string& course, std::int32_t place, float total, float lap ) > record;
    // VuSlalomGame's slot 7 0x48C8C0 only: the gates tick 0x48E6E0 after +0x71C += dt (world/slalom.hxx slalom_gates_tick).
    std::function< void( ) > gates;
};

// VuGame's Results / Awards handlers past their enters (0x484250 .. 0x484580):
// what they call that is the UI's, the platform's or a game slot's. `screen`
// is the screen object's (0 the results VuCommonScreen at +0x84, 1 the award
// screen at +0x88): verb 0 its exit (results vt+8; the award screen's
// 0x47F4C0), 1 its deletion (vt+0(1)), 2 its tick vt+0xC(dt), 3 its draw
// vt+0x10. `json_type` answers gameutil g 0x758FF8 +0x50's 0x4FBDF0 lookup -
// the value's type word (-1: not known here). `paused` is g 0x758FD4 +0x34 !=
// 0. `game_results_tick` the game's vt+0x30(dt) (not read). `award_done` the
// award screen's 0x47F720.
struct vugame_screen_hooks_t
{
    std::function< void( int screen, int verb, float dt ) > screen;
    std::function< int( const char* name ) > json_type;
    std::function< bool( ) > paused;
    std::function< void( float dt ) > game_results_tick;
    std::function< bool( ) > award_done;
    std::function< bool( ) > personal_best;                       // the game's vt+0x38(&+0x6C4, &+0x6C8) (saves: a platform hook)
    std::function< std::string( const char* key ) > string_db;     // g 0x759078 vt+0x18 (the localised text)
    // Results' enter 0x484110: new 0x10, the project slot 11 names (0x4B4360), vt+4 0x480A50 (the repository add, gameInitialize,
    // "OnProjectInitialized") - VuCommonScreen 0x73F588, a UI screen. True when it opened (+0x84).
    std::function< bool( const char* project ) > results_open;
    vugame_award_hooks_t award;                                    // Awards' enter 0x484320 (world/awards.hxx)
    std::function< vugame_award_boat_t( ) > award_boat;            // the boat manager's +0x24 [0]
};

struct race_rules_t
{
    // VuGame's own two machines (its ctor 0x480BA0; begun by 0x4829A0 - the first state, its enter; ticked by
    // 0x482F10 around slot 7): +0x0C the post-race flow, +0x67C the split / leaderboard display.
    c_fsm event;                     // +0x0C
    c_fsm split;                     // +0x67C
    // The split / result display's fields (VuRaceGame slot 15 0x48B180 - core/ui split.hxx ui_race_checkpoint): +0x4C the
    // laps (set from the rules' game), +0x6C4 / +0x6C8 the personal best (the game's vt+0x38 from SAVES - a platform
    // hook: none here, so 0 / the null container), +0x6D8 / +0x6DC / +0x72C written by the crossings.
    ui_race_split_t split_display;
    std::function< void( std::uint32_t address, const char* what ) > unrecreated;   // a VuGame state handler reached
    vugame_screen_hooks_t screen_hooks;   // what the Results / Awards handlers call that is not theirs
    bool results_screen = false;     // +0x84 non-null (Results' enter 0x484110)
    bool award_screen = false;       // +0x88 non-null (Awards' enter 0x484320)
    std::string game_mode = "Race";  // which game's slots 9 / 10 / 11 (the vtable): Race, Slalom, TimeTrial, Ghost
    bool championship = false;       // +0x80 (GameData "IsChampionship")
    float split_duration = 0.0f;     // +0x674: ConstantDB VuDBAsset / HUD / ChallengeTextDuration (the ctor 0x481630)
    std::int32_t split_kind = 0;     // +0x6A4
    std::string opponent;            // +0x6AC
    c_fsm fsm;                       // +0x6F8
    std::uint32_t buoys = 0;         // +0x744
    std::string track;               // the game's +0x68 (VuGame's track name)
    race_rules_hooks_t hooks;
    // The tick's inputs, refreshed by the caller before slot 7: the game's
    // boats, the boat manager's human list.
    std::vector< race_rules_boat_t >* boats = nullptr;
    std::vector< std::uint8_t* > humans;
    race_rules_game_t* game = nullptr;
    bool sorted = true;              // false once race_rules_places refused (more than 32 boats)
    // VuTimeTrialGame's Game exit 0x490750 reads these: TrackDB's document (g_vugameutil 0x758FF8 +0x10's +0x58) and the
    // first human's boat name (its +0x160 VuString).
    const value_t* track_db = nullptr;
    std::string human_boat;
    std::string course;              // VuSlalomGame +0x720 (slot 5 0x48C660: gameutil 0x4D32F0, the GameData Course)
};

// 0x48A7F0 / 0x48A860: every game boat's +0x308 += 1 / -= 1 (the drivers'
// input held off), then the PreGame HUD project's gameInitialize / release.
inline void race_rules_hold( race_rules_t& r, int by )
{
    if ( r.boats )
        for ( race_rules_boat_t& b : *r.boats )
            rules_put_i32( b.boat, 0x308, rules_i32( b.boat, 0x308 ) + by );
}

inline void race_rules_hud( race_rules_t& r, int project, race_hud_verb_t verb, float dt = 0.0f )
{
    if ( r.hooks.hud )
        r.hooks.hud( project, verb, dt );
}

// 0x48A970 (Game) / 0x48AE80 (PostGame) begin with 0x4831D0(dt) and 0x48AB20.
inline void race_rules_clock_and_place( race_rules_t& r, float dt )
{
    if ( !r.boats || !r.game )
        return;
    race_rules_clocks( *r.boats, dt );
    if ( !race_rules_places( *r.game, *r.boats ) )
        r.sorted = false;
}

inline void race_rules_platform( race_rules_t& r, const std::string& call )
{
    if ( r.hooks.platform )
        r.hooks.platform( call );
}

// 0x48AB70's tail past the HUD and the broadcast (see the hooks above). The
// leaderboard value: mulss by 1000.0 (0x6F7828), fld / fistp (round to
// nearest; out of range the integer indefinite 0x80000000 - cvtss2si gives
// the same), neg, cdq. The voice-over line: "FinishLessThanThird" unless the
// human finished (+0x32F); then place 0 / 1 / 2 "FinishFirst" / "FinishSecond"
// / "FinishThird", else "FinishLast" only when the place EQUALS the boat count
// (+0x3C - cmp / cmovne: as retail has it), else "FinishLessThanThird".
inline void race_rules_post_results( race_rules_t& r )
{
    if ( r.humans.empty( ) )
        return;
    const std::uint8_t* h = r.humans[ 0 ];
    const std::int32_t place = rules_i32( h, 0x4CC );
    char text[ 160 ];
    std::snprintf( text, sizeof( text ), "progress %s %d %08X %08X", r.track.c_str( ), place, rules_i32( h, 0x4B4 ), rules_i32( h, 0x4B8 ) );
    race_rules_platform( r, text );
    if ( r.hooks.record )
        r.hooks.record( "Race", "None", place, rules_f( h, 0x4B4 ), rules_f( h, 0x4B8 ) );
    if ( !h[ 0x334 ] )
    {
        const std::int32_t ms = _mm_cvtss_si32( _mm_set_ss( rules_f( h, 0x4B4 ) * 1000.0f ) );
        const std::int64_t value = static_cast< std::int32_t >( 0u - static_cast< std::uint32_t >( ms ) );
        std::snprintf( text, sizeof( text ), "leaderboard %sRace %lld", r.track.c_str( ), static_cast< long long >( value ) );
        race_rules_platform( r, text );
    }
    const char* line = "FinishLessThanThird";
    if ( h[ 0x32F ] )
    {
        const std::int32_t boats = r.boats ? static_cast< std::int32_t >( r.boats->size( ) ) : 0;
        line = place == 0 ? "FinishFirst" : place == 1 ? "FinishSecond" : place == 2 ? "FinishThird" : place == boats ? "FinishLast" : "FinishLessThanThird";
    }
    race_rules_platform( r, std::string( "vo " ) + line );
    if ( place > 0 && _mm_comigt_ss( _mm_set_ss( rules_f( h, 0x328 ) ), _mm_set_ss( 0.0f ) ) && !h[ 0x32D ] )
        race_rules_platform( r, "vo20 DidNotUseBoost" );
}

// 0x48ADE0's tail past the HUD release: the track compared with
// "Hydroelectric" (0x404AC0 over min(size, 13) bytes, then the sizes), the
// human's place +0x4CC == 0 and +0x744 == 0x1F -> the achievement "DingDong".
// Retail reads the human list's first entry whatever its count (a race always
// has a human); with none here, nothing.
inline void race_rules_post_exit( race_rules_t& r )
{
    if ( r.track != "Hydroelectric" || r.humans.empty( ) )
        return;
    if ( rules_i32( r.humans[ 0 ], 0x4CC ) == 0 && r.buoys == 0x1Fu )
        race_rules_platform( r, "achievement DingDong" );
}

// ---- VuRaceGame slot 12 0x489C60: the results writer (world batch 26) -----
//
// thiscall(game, float dt; retn 4), reached as the game's vt+0x30 from VuGame's
// Results tick 0x484280 every frame of that state (case results_writer: retail's
// run whole on its own containers). First 0x4831D0(dt) and 0x48AB20 (the clocks
// and the places, as the Game / PostGame ticks begin), then gameutil (g
// 0x758FF8) +0x50 ["GameResults"] (0x4FC090: the root made a map, the member
// inserted): ["Table"] cleared (0x4FBB10); ["CircuitRace"] = tag 3, the game's
// +0x44 byte as it is; ["Table"] resized to the boat count (0x4FBCD0: made an
// array, then 0x4FC880 - shrunk or grown with nulls). Then per boat k (+0x38
// list, +0x3C count): GameResults made a map again if it is not, its "Table"
// (0x4FC970, a VuString key) made an array if it is not, grown to place + 1 when
// the place +0x4CC is not below its size (signed jl), the row [place] - each
// field below first makes the row a map (a cmp [row], 6 before every one), the
// member cleared and set:
//   DriverName   tag 4  the driver's (+0x220) +0x60 text up to its first NUL
//   BestLapTime  tag 2  +0x4B8         LapsCompleted tag 1 +0x4D4
//   TotalTime    tag 2  +0x4B4         IsLocal  tag 3 the driver's vt+0x38
//   IsHuman      tag 3  vt+0x30        BoatClass tag 4 +0x160 up to its NUL
//   IsFinished   tag 3  +0x32F         Boat     0x4FC640 +0x178 (strlen)
//   PlayerUID    tag 1  0 (0x4FC090)   PadIndex tag 1 0 - only when vt+0x30
//                                      and then vt+0x38 answer true
// A negative place indexes before the array in retail: UNKNOWN, that row is not
// written here (the places 0x48AB20 writes are 0 .. count - 1).
struct race_results_entry_t
{
    std::string driver_name;   // the driver's +0x60 VuString
    std::string boat_class;    // +0x160 (BoatDB "Class")
    std::string boat_name;     // +0x178 (BoatDB "StringID")
    bool human = false;        // the driver's vt+0x30 (slot 12)
    bool local = false;        // the driver's vt+0x38 (slot 14)
};

inline void race_results_set( value_t& v, std::uint32_t tag, int integer )
{
    ui_json_clear( v );
    v.tag = tag;
    v.kind = value_t::kind_t::integer;
    v.integer = integer;
}

inline void race_results_set_float( value_t& v, float f )
{
    ui_json_clear( v );
    v.tag = 2;
    v.kind = value_t::kind_t::real;
    v.real = f;
}

// False when a path not recreated was reached (the sort over more than 32 boats,
// a negative place): the rest still runs as retail's does.
inline bool race_game_write_results( value_t& util_root, race_rules_game_t& g, std::vector< race_rules_boat_t >& boats,
                                     const std::vector< race_results_entry_t >& entries, float dt )
{
    bool known = true;
    race_rules_clocks( boats, dt );
    if ( !race_rules_places( g, boats ) )
        known = false;
    value_t& results = ui_json_child( util_root, "GameResults" );
    ui_json_clear( ui_json_child( results, "Table" ) );
    race_results_set( ui_json_child( results, "CircuitRace" ), 3, g.circuit );
    {
        value_t& table = ui_json_child( results, "Table" );
        if ( table.tag != 5 )
        {
            ui_json_clear( table );
            table.tag = 5;
            table.kind = value_t::kind_t::list;
        }
        table.list.resize( boats.size( ) );
    }
    for ( std::size_t k = 0; k < boats.size( ); ++k )
    {
        const std::uint8_t* b = boats[ k ].boat;
        const race_results_entry_t& e = k < entries.size( ) ? entries[ k ] : race_results_entry_t{ };
        value_t& table = ui_json_child( results, "Table" );
        if ( table.tag != 5 )
        {
            ui_json_clear( table );
            table.tag = 5;
            table.kind = value_t::kind_t::list;
        }
        const std::int32_t place = rules_i32( b, 0x4CC );
        if ( place < 0 )
        {
            known = false;
            continue;
        }
        if ( static_cast< std::size_t >( place ) >= table.list.size( ) )
            table.list.resize( static_cast< std::size_t >( place ) + 1 );
        value_t& row = table.list[ static_cast< std::size_t >( place ) ];
        const auto text = [ & ]( const char* key, const std::string& s )
        {
            value_t& v = ui_json_child( row, key );
            ui_json_clear( v );
            v.tag = 4;
            v.kind = value_t::kind_t::text;
            v.text = s.c_str( );
        };
        text( "DriverName", e.driver_name );
        race_results_set_float( ui_json_child( row, "BestLapTime" ), rules_f( b, 0x4B8 ) );
        race_results_set( ui_json_child( row, "LapsCompleted" ), 1, rules_i32( b, 0x4D4 ) );
        race_results_set_float( ui_json_child( row, "TotalTime" ), rules_f( b, 0x4B4 ) );
        race_results_set( ui_json_child( row, "IsLocal" ), 3, e.local ? 1 : 0 );
        race_results_set( ui_json_child( row, "IsHuman" ), 3, e.human ? 1 : 0 );
        text( "BoatClass", e.boat_class );
        race_results_set( ui_json_child( row, "IsFinished" ), 3, b[ 0x32F ] );
        ui_json_set_string( ui_json_child( row, "Boat" ), e.boat_name.c_str( ) );
        race_results_set( ui_json_child( row, "PlayerUID" ), 1, 0 );
        if ( e.human && e.local )
            race_results_set( ui_json_child( row, "PadIndex" ), 1, 0 );
    }
    return known;
}

// ---- VuGame's OnCheatFinish 0x483190 -> 0x4848C0 (world batch 26; case cheat_finish)
//
// The event method (VuMethod1, registered by VuGame's base ctor 0x480BA0 - a
// developer cheat's broadcast) calls 0x4848C0: for each human boat (the boat
// manager g 0x758FB8 +0x24 list / +0x28 count) +0x308 += 1, +0x32F = 1, the AI
// manager g 0x758F8C's vt+0x30(boat) (slot 12 0x4036C0: fleet_remove_driver),
// +0x334 = 1, +0x4AC = the lap when it is above 2 else 2 (cmovg), +0x4B8 =
// minss(+0x4B8, +0x4B0).
inline void race_rules_cheat_finish( const std::vector< std::uint8_t* >& humans, const std::function< void( std::size_t human ) >& ai_remove )
{
    for ( std::size_t k = 0; k < humans.size( ); ++k )
    {
        std::uint8_t* b = humans[ k ];
        rules_put_i32( b, 0x308, rules_i32( b, 0x308 ) + 1 );
        b[ 0x32F ] = 1;
        if ( ai_remove )
            ai_remove( k );
        b[ 0x334 ] = 1;
        const std::int32_t lap = rules_i32( b, 0x4AC );
        rules_put_i32( b, 0x4AC, lap > 2 ? lap : 2 );
        rules_put_f( b, 0x4B8, rules_minss( rules_f( b, 0x4B8 ), rules_f( b, 0x4B0 ) ) );
    }
}

// ---- VuGame's release 0x482D50 (world batch 26; case vugame_release) -------
//
// thiscall(game), from VuActionGameMode's exit 0x4B97E0: the game's vt+0x18 (VuRaceGame slot 6 0x489A10: the +0x6F8
// machine's current state's exit (vt+8, when there is one), the AI manager g 0x758F8C vt+0x20 then vt+0x24 - the other
// modes' slot 6 differ past the exit: vugame_release_hooks_t::slot6); a local
// container - with a human (the boat manager g 0x758FB8 +0x28 non-zero) ["Finished"] = tag 3 the first human's +0x32F
// byte, ["TotalTime"] = tag 2 its +0x4B4 - handed to the analytics g 0x758F9C vt+0x18("LevelExit", container) (a
// PLATFORM service) and cleared; the +0x0C machine's current state's exit, then the +0x67C machine's; 0x500B80
// (gameRelease) on each game boat (+0x38 / +0x3C) whose +0x14 has bit 2; the track manager g 0x758FF4 vt+0x28;
// 0x4AD2B0 on the collision manager g 0x758FBC; the viewport manager g 0x759060's four cameras +0x278 / +0x4EC / +0x760
// / +0x9D4 = 0; lens water g 0x759048 vt+0x24; 0x539020(game) on g 0x75906C; 0x4D2010. Each callee not this file's
// is a hook, called in retail's order.
struct vugame_release_hooks_t
{
    std::function< void( int machine ) > exit;             // 0 = +0x0C, 1 = +0x67C, 2 = +0x6F8: the current state's vt+8
    std::function< void( int slot ) > ai;                  // the AI manager's vt+0x20 / vt+0x24 (slot 8 / 9)
    std::function< void( const char* name, const value_t& data ) > analytics;
    std::function< void( std::size_t boat ) > release;     // 0x500B80
    std::function< void( const char* what ) > call;        // "track28", "collision", "cameras", "lens24", "listener", "db"
    // The game's slot 6 (vt+0x18), read for each mode (world batch 30): every one first runs its machine's current state's
    // exit (+0x6F8, the ghost's +0x700: [machine] +0x24's vt+8, when there is one); then VuRaceGame 0x489A10 the AI manager
    // g 0x758F8C vt+0x20 / vt+0x24; VuTimeTrialGame 0x48F330 nothing more; VuSlalomGame 0x48C880 the draw manager g
    // 0x7590B8 vt+0x2C( +0x744 ) when its +0x1E is set, then +0x1E = 0 (draw_remove); VuGhostGame 0x485360 0x4AF490 on
    // the ghost manager g 0x758FC8 (ghost_release - not recreated: a hook).
    std::uint32_t slot6 = 0x489A10u;
    std::function< void( ) > draw_remove;
    std::function< void( ) > ghost_release;
};

inline void vugame_release( race_rules_t& r, const std::vector< std::uint32_t >& boat_flags, const vugame_release_hooks_t& h )
{
    const auto exit = [ & ]( int machine, c_fsm& f ) {
        if ( f.current( ) && h.exit )
            h.exit( machine );
    };
    exit( 2, r.fsm );                     // the game's slot 6 (vt+0x18): its machine's exit, then its own tail
    if ( h.slot6 == 0x489A10u && h.ai )
        h.ai( 8 ), h.ai( 9 );
    else if ( h.slot6 == 0x48C880u && h.draw_remove )
        h.draw_remove( );
    else if ( h.slot6 == 0x485360u && h.ghost_release )
        h.ghost_release( );
    value_t data;
    if ( !r.humans.empty( ) )
    {
        const std::uint8_t* b = r.humans[ 0 ];
        race_results_set( ui_json_child( data, "Finished" ), 3, b[ 0x32F ] );
        race_results_set_float( ui_json_child( data, "TotalTime" ), rules_f( b, 0x4B4 ) );
    }
    if ( h.analytics )
        h.analytics( "LevelExit", data );
    exit( 0, r.event );
    exit( 1, r.split );
    for ( std::size_t k = 0; k < boat_flags.size( ); ++k )
        if ( ( boat_flags[ k ] >> 2 ) & 1u )
            if ( h.release )
                h.release( k );
    for ( const char* c : { "track28", "collision", "cameras", "lens24", "listener", "db" } )
        if ( h.call )
            h.call( c );
}

// ---- VuGame's machines (ctor 0x480BA0): the tables -------------------------
//
// +0x0C: Event (no handlers) -> Results "EventFinished"; Results {enter
// 0x484110, exit 0x484250, tick 0x484280, draw 0x4842F0} -> Awards
// "OnUserNext" (VuGame's OnUserNext 0x483170 pulses it: +0x1D), -> Exit
// "NoResults"; Awards {0x484320, 0x4844F0, 0x484520, 0x484580} -> Exit
// "AwardsDone"; Exit. +0x67C (the friend-score / split flow - its handlers are
// the leaderboard's (OpenFeint: online, a platform service) and the HUD's):
// GetFriendScores {enter 0x483B90, tick 0x483BE0}, DownloadBlob {enter the
// nullsub 0x401560, tick 0x483C10}, PersonalBestConfig {enter 0x483C40},
// Intro {tick 0x483CB0, draw 0x483CF0}, Racing, Split {tick 0x483D00, draw
// 0x483D40}, Result {enter 0x483D50, tick 0x483E00, draw 0x483E40}, Done; the
// transitions below. Verified against the ctor run whole (case race_game_fsm).
// The handlers are NOT recreated: reaching one calls `unrecreated` (UNKNOWN);
// the nullsub is nothing.
inline void vugame_fsm_handlers( race_rules_t& r, fsm_state_t& s, std::uint32_t enter, std::uint32_t exit, std::uint32_t tick,
                                 std::uint32_t draw )
{
    s.m_address[ 0 ] = enter, s.m_address[ 1 ] = exit, s.m_address[ 2 ] = tick, s.m_address[ 3 ] = draw;
    const auto hook = [ &r ]( std::uint32_t a, const std::string& name, const char* verb ) {
        return [ &r, a, name, verb ] {
            if ( r.unrecreated )
                r.unrecreated( a, ( "VuGame " + name + " " + verb ).c_str( ) );
        };
    };
    if ( enter && enter != 0x401560u )
        s.m_enter = hook( enter, s.m_name, "enter" );
    if ( exit )
        s.m_exit = hook( exit, s.m_name, "exit" );
    if ( tick )
    {
        auto h = hook( tick, s.m_name, "tick" );
        s.m_tick = [ h ]( float ) { h( ); };
    }
    if ( draw )
        s.m_draw = hook( draw, s.m_name, "draw" );
}

// ---- VuGame's Results / Awards handlers (world batch 21; case vugame_handlers)
//
// Results exit 0x484250: with a screen (+0x84) its vt+8, its deletion, +0x84
// = 0. Results tick 0x484280(dt): the game's vt+0x30(dt); the screen's
// vt+0xC(dt) when there is one and g 0x758FD4 +0x34 is 0; then gameutil +0x50's
// "GameResults" value - type 0 (null) PULSES "NoResults" (+0x1D; 0x555C60
// finds it - an absent name would write before the array: UNKNOWN). Results
// draw 0x4842F0: "GameResults" of type 6 and a screen -> its vt+0x10 (the
// lookup first, whatever the screen). Awards exit 0x4844F0: with an award
// screen (+0x88) its 0x47F4C0, its deletion, +0x88 = 0. Awards tick
// 0x484520(dt): no award screen -> "AwardsDone"'s VALUE (+0x1C) = 1; else,
// unless +0x34 is set (then nothing), its vt+0xC(dt) and, when 0x47F720
// answers true, "AwardsDone" = 1. Awards draw 0x484580: the award screen's
// vt+0x10. A hook not given is UNKNOWN (`unrecreated`) and stops the handler.
inline bool vugame_screen_need( race_rules_t& r, bool have, std::uint32_t address, const char* what )
{
    if ( !have && r.unrecreated )
        r.unrecreated( address, what );
    return have;
}

inline void vugame_condition_byte( c_fsm& f, const char* name, bool pulse )
{
    const int i = f.condition_index( name );
    if ( i < 0 )
        return;   // 0x555C60's -1: retail writes before the array (UNKNOWN) - not reproduced
    if ( pulse )
        f.m_conditions[ static_cast< std::size_t >( i ) ].m_pulse = 1;
    else
        f.m_conditions[ static_cast< std::size_t >( i ) ].m_value = 1;
}

// Results' enter 0x484110: the screen on slot 11's project.
inline void vugame_results_enter( race_rules_t& r )
{
    if ( !vugame_screen_need( r, static_cast< bool >( r.screen_hooks.results_open ), 0x484110u, "VuGame Results enter: VuCommonScreen 0x73F588 on the results project (UI)" ) )
        return;
    r.results_screen = r.screen_hooks.results_open( vugame_results_project( r.game_mode, r.championship ) );
}

// Awards' enter 0x484320 (world/awards.hxx).
inline void vugame_awards_enter_state( race_rules_t& r )
{
    if ( !vugame_screen_need( r, static_cast< bool >( r.screen_hooks.award_boat ), 0x484320u, "VuGame Awards enter: the services (a platform hook) not wired" ) )
        return;
    r.award_screen = vugame_awards_enter( r.game_mode, r.track, r.championship, r.screen_hooks.award_boat( ), r.screen_hooks.award );
}

inline void vugame_results_exit( race_rules_t& r )
{
    if ( !r.results_screen )
        return;
    if ( !vugame_screen_need( r, static_cast< bool >( r.screen_hooks.screen ), 0x484250u, "VuGame Results exit: the screen's vt+8 / deletion (UI)" ) )
        return;
    r.screen_hooks.screen( 0, 0, 0.0f );
    r.screen_hooks.screen( 0, 1, 0.0f );
    r.results_screen = false;
}

inline void vugame_results_tick( race_rules_t& r, float dt )
{
    if ( !vugame_screen_need( r, static_cast< bool >( r.screen_hooks.game_results_tick ), 0x484280u, "VuGame Results tick: the game's vt+0x30 (not read)" ) )
        return;
    r.screen_hooks.game_results_tick( dt );
    if ( r.results_screen )
    {
        if ( !vugame_screen_need( r, r.screen_hooks.paused && r.screen_hooks.screen, 0x484280u, "VuGame Results tick: g 0x758FD4 +0x34 / the screen (UI)" ) )
            return;
        if ( !r.screen_hooks.paused( ) )
            r.screen_hooks.screen( 0, 2, dt );
    }
    const int type = r.screen_hooks.json_type ? r.screen_hooks.json_type( "GameResults" ) : -1;
    if ( !vugame_screen_need( r, type >= 0, 0x484280u, "VuGame Results tick: gameutil +0x50 \"GameResults\" (not known here)" ) )
        return;
    if ( type == 0 )
        vugame_condition_byte( r.event, "NoResults", true );
}

inline void vugame_results_draw( race_rules_t& r )
{
    const int type = r.screen_hooks.json_type ? r.screen_hooks.json_type( "GameResults" ) : -1;
    if ( !vugame_screen_need( r, type >= 0, 0x4842F0u, "VuGame Results draw: gameutil +0x50 \"GameResults\" (not known here)" ) )
        return;
    if ( type == 6 && r.results_screen && vugame_screen_need( r, static_cast< bool >( r.screen_hooks.screen ), 0x4842F0u, "VuGame Results draw: the screen (UI)" ) )
        r.screen_hooks.screen( 0, 3, 0.0f );
}

inline void vugame_awards_exit( race_rules_t& r )
{
    if ( !r.award_screen )
        return;
    if ( !vugame_screen_need( r, static_cast< bool >( r.screen_hooks.screen ), 0x4844F0u, "VuGame Awards exit: the award screen's 0x47F4C0 / deletion (UI)" ) )
        return;
    r.screen_hooks.screen( 1, 0, 0.0f );
    r.screen_hooks.screen( 1, 1, 0.0f );
    r.award_screen = false;
}

inline void vugame_awards_tick( race_rules_t& r, float dt )
{
    if ( !r.award_screen )
    {
        vugame_condition_byte( r.event, "AwardsDone", false );
        return;
    }
    if ( !vugame_screen_need( r, r.screen_hooks.paused && r.screen_hooks.screen && r.screen_hooks.award_done, 0x484520u,
                              "VuGame Awards tick: g 0x758FD4 +0x34 / the award screen (UI)" ) )
        return;
    if ( r.screen_hooks.paused( ) )
        return;
    r.screen_hooks.screen( 1, 2, dt );
    if ( r.screen_hooks.award_done( ) )
        vugame_condition_byte( r.event, "AwardsDone", false );
}

inline void vugame_awards_draw( race_rules_t& r )
{
    if ( r.award_screen && vugame_screen_need( r, static_cast< bool >( r.screen_hooks.screen ), 0x484580u, "VuGame Awards draw: the award screen (UI)" ) )
        r.screen_hooks.screen( 1, 3, 0.0f );
}

// The +0x67C machine's Intro / Split / Result ticks 0x483CB0 / 0x483D00 /
// 0x483E00: comiss +0x6A0 (the machine's time), +0x674 / jb - unless the time
// is below the duration (or either is NaN) "Expired" is PULSED. Their draws
// (0x483CF0 / 0x483D40 -> 0x484590, the split display) are the HUD's - hooks.
inline void vugame_split_expire( race_rules_t& r )
{
    const float t = r.split.m_time, d = r.split_duration;
    if ( _mm_comilt_ss( _mm_set_ss( t ), _mm_set_ss( d ) ) || t != t || d != d )
        return;
    vugame_condition_byte( r.split, "Expired", true );
}

// PersonalBestConfig's enter 0x483C40: the game's vt+0x38(&+0x6C4, &+0x6C8)
// true -> +0x6A4 = 0 and +0x6AC = the string DB's "HUD_PersonalBestOpponentName"
// (0x401DB0's assign); false -> "NoPersonalBestTime"'s VALUE = 1.
inline void vugame_personal_best_enter( race_rules_t& r )
{
    if ( !vugame_screen_need( r, r.screen_hooks.personal_best && r.screen_hooks.string_db, 0x483C40u,
                              "VuGame PersonalBestConfig enter: the game's vt+0x38 / the string DB (platform)" ) )
        return;
    if ( r.screen_hooks.personal_best( ) )
    {
        r.split_kind = 0;
        r.opponent = r.screen_hooks.string_db( "HUD_PersonalBestOpponentName" );
    }
    else
        vugame_condition_byte( r.split, "NoPersonalBestTime", false );
}

inline void vugame_fsms_construct( race_rules_t& r )
{
    r.event = c_fsm{ };
    r.event.add_state( "Event" );
    fsm_state_t& results = r.event.add_state( "Results" );
    vugame_fsm_handlers( r, results, 0x484110u, 0x484250u, 0x484280u, 0x4842F0u );
    results.m_enter = [ &r ] { vugame_results_enter( r ); };
    results.m_exit = [ &r ] { vugame_results_exit( r ); };
    results.m_tick = [ &r ]( float dt ) { vugame_results_tick( r, dt ); };
    results.m_draw = [ &r ] { vugame_results_draw( r ); };
    fsm_state_t& awards = r.event.add_state( "Awards" );
    vugame_fsm_handlers( r, awards, 0x484320u, 0x4844F0u, 0x484520u, 0x484580u );
    awards.m_enter = [ &r ] { vugame_awards_enter_state( r ); };
    awards.m_exit = [ &r ] { vugame_awards_exit( r ); };
    awards.m_tick = [ &r ]( float dt ) { vugame_awards_tick( r, dt ); };
    awards.m_draw = [ &r ] { vugame_awards_draw( r ); };
    r.event.add_state( "Exit" );
    r.event.add_transition( "Event", "Results", "EventFinished" );
    r.event.add_transition( "Results", "Awards", "OnUserNext" );
    r.event.add_transition( "Results", "Exit", "NoResults" );
    r.event.add_transition( "Awards", "Exit", "AwardsDone" );
    r.split = c_fsm{ };
    vugame_fsm_handlers( r, r.split.add_state( "GetFriendScores" ), 0x483B90u, 0, 0x483BE0u, 0 );
    vugame_fsm_handlers( r, r.split.add_state( "DownloadBlob" ), 0x401560u, 0, 0x483C10u, 0 );
    fsm_state_t& pb = r.split.add_state( "PersonalBestConfig" );
    vugame_fsm_handlers( r, pb, 0x483C40u, 0, 0, 0 );
    pb.m_enter = [ &r ] { vugame_personal_best_enter( r ); };
    fsm_state_t& intro = r.split.add_state( "Intro" );
    vugame_fsm_handlers( r, intro, 0, 0, 0x483CB0u, 0x483CF0u );
    intro.m_tick = [ &r ]( float ) { vugame_split_expire( r ); };
    r.split.add_state( "Racing" );
    fsm_state_t& split = r.split.add_state( "Split" );
    vugame_fsm_handlers( r, split, 0, 0, 0x483D00u, 0x483D40u );
    split.m_tick = [ &r ]( float ) { vugame_split_expire( r ); };
    fsm_state_t& result = r.split.add_state( "Result" );
    vugame_fsm_handlers( r, result, 0x483D50u, 0, 0x483E00u, 0x483E40u );
    result.m_tick = [ &r ]( float ) { vugame_split_expire( r ); };
    r.split.add_state( "Done" );
    static const char* const k_split_transitions[ 15 ][ 3 ] = {
        { "GetFriendScores", "Done", "NoHumansPlaying" },          { "GetFriendScores", "PersonalBestConfig", "UserNotLoggedIn" },
        { "GetFriendScores", "PersonalBestConfig", "LeaderboardNotFound" }, { "GetFriendScores", "PersonalBestConfig", "OpenFeintFailure" },
        { "GetFriendScores", "PersonalBestConfig", "NoFriendsFound" }, { "GetFriendScores", "DownloadBlob", "OpponentChosen" },
        { "DownloadBlob", "PersonalBestConfig", "OpenFeintFailure" }, { "DownloadBlob", "Intro", "OpenFeintSuccess" },
        { "PersonalBestConfig", "Done", "NoPersonalBestTime" },    { "PersonalBestConfig", "Intro", "" },
        { "Intro", "Racing", "Expired" },                          { "Racing", "Split", "ShowSplit" },
        { "Racing", "Result", "ShowResult" },                      { "Split", "Racing", "Expired" },
        { "Result", "Done", "Expired" } };
    for ( const auto& t : k_split_transitions )
        r.split.add_transition( t[ 0 ], t[ 1 ], t[ 2 ] );
}

// 0x4829A0's tail: each machine's current = its first state, then that state's enter.
inline void vugame_fsm_begin( c_fsm& f )
{
    f.m_current = f.m_states.empty( ) ? -1 : 0;
    if ( fsm_state_t* s = f.current( ) )
        if ( s->m_enter )
            s->m_enter( );
}

// VuGame's tick 0x482F10 past the bookkeeping: slot 7 answered `running`;
// false -> +0x0C's "EventFinished" +28 = 1; +0x0C evaluate (0x555AD0), the
// current state's tick, +0x30 += dt; the same on +0x67C (+0x6A0 += dt); the
// tick's answer = +0x0C's current state is not "Exit" (0x404AC0 over min(size,
// 4), then the sizes).
inline bool vugame_fsms_tick( race_rules_t& r, bool running, float dt )
{
    if ( !running )
        r.event.set_condition( "EventFinished", true );
    for ( c_fsm* f : { &r.event, &r.split } )
    {
        f->evaluate( );
        if ( fsm_state_t* s = f->current( ) )
            if ( s->m_tick )
                s->m_tick( dt );
        f->m_time = f->m_time + dt;
    }
    const fsm_state_t* s = r.event.current( );
    return !s || s->m_name != "Exit";
}

// The action mode's Run / Begin / End draw 0x4B9C10 with a game: the key bits, the +0x0C machine's current state's draw, the +0x67C
// machine's when the boat manager's +0x2B8 is set, the game's vt+0x20. VuRaceGame's slot 8 0x489AD0: the +0x6F8 machine's, under the same
// byte. +0x2B8: the manager's ctor stores 1 (0x4AAC81) and no code in the image clears it as a byte: 1 here. The pause menu's draw
// 0x4BB5E0 follows in retail: not wired (session.hxx holds pause_menu_t only).
inline void race_game_draw( race_rules_t& r )
{
    const auto draw = []( c_fsm& f ) {
        if ( f.current( ) && f.current( )->m_draw )
            f.current( )->m_draw( );
    };
    draw( r.event );
    draw( r.split );
    draw( r.fsm );
}

inline void race_rules_construct( race_rules_t& r )
{
    vugame_fsms_construct( r );
    r.fsm = c_fsm{ };
    r.buoys = 0;
    fsm_state_t& pre = r.fsm.add_state( "PreGame" );
    pre.m_enter = [ &r ] { race_rules_hold( r, 1 ); race_rules_hud( r, 0, race_hud_verb_t::initialize ); };
    pre.m_exit = [ &r ] { race_rules_hold( r, -1 ); race_rules_hud( r, 0, race_hud_verb_t::release ); };
    pre.m_tick = [ &r ]( float dt ) { race_rules_hud( r, 0, race_hud_verb_t::tick, dt ); };
    pre.m_address[ 0 ] = 0x48A7F0u, pre.m_address[ 1 ] = 0x48A860u, pre.m_address[ 2 ] = 0x48A8B0u, pre.m_address[ 3 ] = 0x486C00u;
    pre.m_draw = [ &r ] { race_rules_hud( r, 0, race_hud_verb_t::draw ); };
    fsm_state_t& game = r.fsm.add_state( "Game" );
    game.m_enter = [ &r ] { race_rules_hud( r, 1, race_hud_verb_t::initialize ); };
    game.m_exit = [ &r ] { race_rules_hud( r, 1, race_hud_verb_t::release ); };
    // 0x48A970: the clocks and places; HumanFinished = the first human's +0x32F
    // (0 with no human), EveryoneFinished = the AND of every game boat's +0x32F
    // (1 with none) - stored into the conditions' +28; then the HUD's tick.
    game.m_tick = [ &r ]( float dt ) {
        race_rules_clock_and_place( r, dt );
        const std::uint8_t human = r.humans.empty( ) ? 0 : r.humans[ 0 ][ 0x32F ];
        std::uint8_t everyone = 1;
        if ( r.boats )
            for ( const race_rules_boat_t& b : *r.boats )
                everyone &= b.boat[ 0x32F ];
        r.fsm.set_condition( "HumanFinished", human != 0 );
        r.fsm.set_condition( "EveryoneFinished", everyone != 0 );
        race_rules_hud( r, 1, race_hud_verb_t::tick, dt );
    };
    game.m_address[ 0 ] = 0x48A910u, game.m_address[ 1 ] = 0x4872A0u, game.m_address[ 2 ] = 0x48A970u, game.m_address[ 3 ] = 0x487370u;
    game.m_draw = [ &r ] { race_rules_hud( r, 1, race_hud_verb_t::draw ); };
    fsm_state_t& post = r.fsm.add_state( "PostGame" );
    post.m_enter = [ &r ] {
        race_rules_hud( r, 2, race_hud_verb_t::initialize );
        if ( r.hooks.broadcast )
            r.hooks.broadcast( "OnSetAllowPause", false );
        race_rules_post_results( r );
    };
    post.m_exit = [ &r ] {
        race_rules_hud( r, 2, race_hud_verb_t::release );
        race_rules_post_exit( r );
    };
    // 0x48AE80: the clocks and places; "Exit" = the time in the state >= 4.0.
    post.m_tick = [ &r ]( float dt ) {
        race_rules_clock_and_place( r, dt );
        r.fsm.set_condition( "Exit", r.fsm.m_time >= 4.0f );
        race_rules_hud( r, 2, race_hud_verb_t::tick, dt );
    };
    post.m_address[ 0 ] = 0x48AB70u, post.m_address[ 1 ] = 0x48ADE0u, post.m_address[ 2 ] = 0x48AE80u, post.m_address[ 3 ] = 0x48AF30u;
    post.m_draw = [ &r ] { race_rules_hud( r, 2, race_hud_verb_t::draw ); };
    r.fsm.add_state( "Exit" );
    r.fsm.add_transition( "PreGame", "Game", "StartLightsOut" );
    r.fsm.add_transition( "Game", "PostGame", "HumanFinished" );
    r.fsm.add_transition( "Game", "PostGame", "EveryoneFinished" );
    r.fsm.add_transition( "", "Exit", "Exit" );
}

// ---- VuTimeTrialGame's machine (ctor 0x48ED30; world batch 27, case timetrial_game_fsm) --------------------------
//
// The table is the ctor's (game_mode_defs, case game_ctors): PreGame / Game / PostGame / Exit, PreGame -> Game
// "StartLightsOut", Game -> PostGame "Finished", "" -> Exit "Exit". The handlers, read whole (thiscall game):
//   OnStartLightsOut 0x490540: "StartLightsOut" +0x1C = 1.
//   PreGame  enter 0x490570: every game boat's +0x308 += 1, the HUD +0x72C's gameInitialize (when its root's +0x14 bit 2
//            is clear: repository vt+0x18, 0x500B10, 0x501260 "OnProjectInitialized"); exit 0x4905E0: +0x308 -= 1 and
//            its release (when set: 0x500B80, repository vt+0x1C); tick 0x490630 the HUD's tick (root a VuHUDEntity -
//            RTTI 0x7362AC - vt+0x30(dt)); draw 0x490690 (vt+0x34(1.0): the UI's, not run here - as the race's).
//   Game     enter 0x4906F0 / tick 0x4908B0 / draw 0x490950 on +0x730; the tick: 0x4831D0(dt) (the clocks - no places),
//            then with a human (boat manager g 0x758FB8 +0x28 != 0) "Finished" = the first human's +0x32F, then the
//            HUD tick. Exit 0x490750: the HUD release, then the first human's place +0x4CC (the list's [0], its count
//            NOT tested): TrackDB ["VuDBAsset"] (0x4FBDF0) -> [the game's track +0x68] (0x4F22B0 find, a map only) ->
//            ["TimeTrial"] -> [the boat's name +0x160] = the reference times; +0x4CC = 0, then while +0x4CC < the
//            array's count (0x4FBC40: an array only, else 0): element +0x4CC (an array in range, else the null
//            container) as a float (tag 1 cvtdq2ps, 2 the float, 7 fild qword / fstp, else 0); stop when it is >= the
//            human's +0x4B4 (comiss / jae: an unordered compare goes on), else +0x4CC += 1.
//   PostGame enter 0x4909B0 on +0x734: the HUD init, "OnSetAllowPause" {bool false}; with a human: the progress manager
//            g 0x758FD8 vt+0x38 (track +0x68, boat +0x160, 0, +0x4CC, +0x4B4, +0x4B8 - its save: a platform hook), unless
//            +0x334 the leaderboard 0x4B0650 (track + "TimeTrial", -(int)(+0x4B4 x 1000.0) as int64 - fld / fistp,
//            neg, cdq), and when +0x4CC > 0, +0x328 > 0.0 and +0x32D clear the voice-over g 0x758FEC vt+0x20 (human,
//            "DidNotUseBoost") - no vt+0x1C line. Exit 0x490BD0: the HUD release. Tick 0x490C00: "Exit" = the time in
//            the state +0x71C >= 4.0 (no clocks), then the HUD tick; draw 0x486720 (the UI's).
// The platform calls reach r.hooks.platform as text: "progress38 <track> <place> <4B4 hex> <4B8 hex>", "leaderboard
// <name> <value>", "vo20 <line>". Slot 5 0x48F310 = race_rules_start, slot 7 0x48F350 = race_rules_tick (the same
// instructions as VuRaceGame's 0x489A40 but the name test: 0x404AC0 over min(size, 4) bytes of "Exit", then the
// sizes - the same answer).
inline float timetrial_value_float( const value_t& v )
{
    if ( v.tag == 1 )
        return static_cast< float >( v.integer );
    if ( v.tag == 2 )
        return v.real;
    if ( v.tag == 7 )
        return static_cast< float >( v.int64 );
    return 0.0f;
}

// VuSlalomGame's Game exit 0x48DE10 is the same but for the path: ["Slalom"] -> [boat] -> ["Times"] (`leaf`).
inline void timetrial_rules_place( race_rules_t& r, const char* mode_key = "TimeTrial", const char* leaf = nullptr, std::uint32_t at = 0x490750u )
{
    if ( r.humans.empty( ) )
    {
        if ( r.unrecreated )
            r.unrecreated( at, "the Game exit with no human (retail reads the list's [0] unchecked) - UNKNOWN" );
        return;
    }
    std::uint8_t* h = r.humans[ 0 ];
    static const value_t null{ };
    const value_t& db = r.track_db ? member( *r.track_db, "VuDBAsset" ) : null;
    const value_t* t = db.tag == 6 ? find( db, r.track.c_str( ) ) : nullptr;
    const value_t& mode = member( t ? *t : null, mode_key );
    const value_t* b = mode.tag == 6 ? find( mode, r.human_boat.c_str( ) ) : nullptr;
    const value_t& times = leaf ? member( b ? *b : null, leaf ) : b ? *b : null;
    rules_put_i32( h, 0x4CC, 0 );
    const std::int32_t count = times.tag == 5 ? static_cast< std::int32_t >( times.list.size( ) ) : 0;
    const float clock = rules_f( h, 0x4B4 );
    while ( rules_i32( h, 0x4CC ) < count )
    {
        const std::int32_t k = rules_i32( h, 0x4CC );
        const value_t& e = times.tag == 5 && k >= 0 && k < count ? times.list[ static_cast< std::size_t >( k ) ] : null;
        if ( _mm_comige_ss( _mm_set_ss( timetrial_value_float( e ) ), _mm_set_ss( clock ) ) )
            break;
        rules_put_i32( h, 0x4CC, k + 1 );
    }
}

inline void timetrial_rules_post_results( race_rules_t& r )
{
    if ( r.humans.empty( ) )
        return;
    const std::uint8_t* h = r.humans[ 0 ];
    const std::int32_t place = rules_i32( h, 0x4CC );
    char text[ 160 ];
    std::snprintf( text, sizeof( text ), "progress38 %s %d %08X %08X", r.track.c_str( ), place, rules_i32( h, 0x4B4 ), rules_i32( h, 0x4B8 ) );
    race_rules_platform( r, text );
    if ( r.hooks.record )
        r.hooks.record( "TimeTrial", "None", place, rules_f( h, 0x4B4 ), rules_f( h, 0x4B8 ) );
    if ( !h[ 0x334 ] )
    {
        const std::int32_t ms = _mm_cvtss_si32( _mm_set_ss( rules_f( h, 0x4B4 ) * 1000.0f ) );
        const std::int64_t value = static_cast< std::int32_t >( 0u - static_cast< std::uint32_t >( ms ) );
        std::snprintf( text, sizeof( text ), "leaderboard %sTimeTrial %lld", r.track.c_str( ), static_cast< long long >( value ) );
        race_rules_platform( r, text );
    }
    if ( place > 0 && _mm_comigt_ss( _mm_set_ss( rules_f( h, 0x328 ) ), _mm_set_ss( 0.0f ) ) && !h[ 0x32D ] )
        race_rules_platform( r, "vo20 DidNotUseBoost" );
}

inline void timetrial_rules_construct( race_rules_t& r )
{
    vugame_fsms_construct( r );
    r.fsm = c_fsm{ };
    r.buoys = 0;
    fsm_state_t& pre = r.fsm.add_state( "PreGame" );
    pre.m_enter = [ &r ] { race_rules_hold( r, 1 ); race_rules_hud( r, 0, race_hud_verb_t::initialize ); };
    pre.m_exit = [ &r ] { race_rules_hold( r, -1 ); race_rules_hud( r, 0, race_hud_verb_t::release ); };
    pre.m_tick = [ &r ]( float dt ) { race_rules_hud( r, 0, race_hud_verb_t::tick, dt ); };
    pre.m_address[ 0 ] = 0x490570u, pre.m_address[ 1 ] = 0x4905E0u, pre.m_address[ 2 ] = 0x490630u, pre.m_address[ 3 ] = 0x490690u;
    pre.m_draw = [ &r ] { race_rules_hud( r, 0, race_hud_verb_t::draw ); };
    fsm_state_t& game = r.fsm.add_state( "Game" );
    game.m_enter = [ &r ] { race_rules_hud( r, 1, race_hud_verb_t::initialize ); };
    game.m_exit = [ &r ] { race_rules_hud( r, 1, race_hud_verb_t::release ); timetrial_rules_place( r ); };
    game.m_tick = [ &r ]( float dt ) {
        if ( r.boats )
            race_rules_clocks( *r.boats, dt );
        if ( !r.humans.empty( ) )
            r.fsm.set_condition( "Finished", r.humans[ 0 ][ 0x32F ] != 0 );
        race_rules_hud( r, 1, race_hud_verb_t::tick, dt );
    };
    game.m_address[ 0 ] = 0x4906F0u, game.m_address[ 1 ] = 0x490750u, game.m_address[ 2 ] = 0x4908B0u, game.m_address[ 3 ] = 0x490950u;
    game.m_draw = [ &r ] { race_rules_hud( r, 1, race_hud_verb_t::draw ); };
    fsm_state_t& post = r.fsm.add_state( "PostGame" );
    post.m_enter = [ &r ] {
        race_rules_hud( r, 2, race_hud_verb_t::initialize );
        if ( r.hooks.broadcast )
            r.hooks.broadcast( "OnSetAllowPause", false );
        timetrial_rules_post_results( r );
    };
    post.m_exit = [ &r ] { race_rules_hud( r, 2, race_hud_verb_t::release ); };
    post.m_tick = [ &r ]( float dt ) {
        r.fsm.set_condition( "Exit", r.fsm.m_time >= 4.0f );
        race_rules_hud( r, 2, race_hud_verb_t::tick, dt );
    };
    post.m_address[ 0 ] = 0x4909B0u, post.m_address[ 1 ] = 0x490BD0u, post.m_address[ 2 ] = 0x490C00u, post.m_address[ 3 ] = 0x486720u;
    post.m_draw = [ &r ] { race_rules_hud( r, 2, race_hud_verb_t::draw ); };
    r.fsm.add_state( "Exit" );
    r.fsm.add_transition( "PreGame", "Game", "StartLightsOut" );
    r.fsm.add_transition( "Game", "PostGame", "Finished" );
    r.fsm.add_transition( "", "Exit", "Exit" );
}

// ---- VuSlalomGame's machine (ctor 0x48BEC0; world batch 29, case slalom_game_fsm) --------------------------------
//
// The table is the ctor's (game_mode_defs): PreGame / Game / PostGame / Exit, PreGame -> Game "StartLightsOut", Game ->
// PostGame "Finished", "" -> Exit "Exit"; events OnStartLightsOut 0x48DB40 (as 0x490540) and OnBoatRecovered 0x48DB70
// (world/slalom.hxx slalom_recovered - the session's). The twelve handlers are TimeTrial's shape on the Slalom HUDs
// (+0x75C / +0x760 / +0x764), read whole:
//   PreGame  enter 0x48DC30 / exit 0x48DCA0 / tick 0x48DCF0 / draw 0x48DD50 - as 0x490570 / 0x4905E0 / 0x490630 / 0x490690.
//   Game     enter 0x48DDB0, tick 0x48DF90 (0x4831D0 the clocks; with a human "Finished" = its +0x32F; the HUD tick), draw
//            0x48E030; exit 0x48DE10: the HUD release, then the place +0x4CC from TrackDB ["VuDBAsset"][track]["Slalom"]
//            [the human's +0x160]["Times"] (timetrial_rules_place's loop: +0x4B4 read once, stop at the first time >= it).
//   PostGame enter 0x48E090: the HUD init, "OnSetAllowPause" {false}; with a human the progress manager vt+0x3C (track +0x68,
//            course +0x720, boat +0x160, 0, +0x4CC, +0x4B4, +0x4B8 - saves, a platform hook), unless +0x334 the
//            leaderboard 0x4B0650 (track + "Slalom" + course, -(int)(+0x4B4 x 1000.0) - fld / fistp, neg, cdq), then
//            the voice-over vt+0x20 "DidNotUseBoost" as TimeTrial's; exit 0x48E300 the release; tick 0x48E330 "Exit" =
//            +0x71C >= 4.0 then the HUD tick; draw 0x48E3D0.
// Slot 5 0x48C660 = slalom_course_start, then the first state's enter (race_rules_start), then the draw add; slot 7
// 0x48C8C0 = race_rules_tick with the gates tick (hooks.gates) after the time; slot 6 0x48C880 the current state's exit
// and the draw remove.
inline void slalom_rules_post_results( race_rules_t& r )
{
    if ( r.humans.empty( ) )
        return;
    const std::uint8_t* h = r.humans[ 0 ];
    const std::int32_t place = rules_i32( h, 0x4CC );
    char text[ 200 ];
    std::snprintf( text, sizeof( text ), "progress3C %s %s %d %08X %08X", r.track.c_str( ), r.course.c_str( ), place, rules_i32( h, 0x4B4 ), rules_i32( h, 0x4B8 ) );
    race_rules_platform( r, text );
    if ( r.hooks.record )
        r.hooks.record( "Slalom", r.course, place, rules_f( h, 0x4B4 ), rules_f( h, 0x4B8 ) );
    if ( !h[ 0x334 ] )
    {
        const std::int32_t ms = _mm_cvtss_si32( _mm_set_ss( rules_f( h, 0x4B4 ) * 1000.0f ) );
        const std::int64_t value = static_cast< std::int32_t >( 0u - static_cast< std::uint32_t >( ms ) );
        std::snprintf( text, sizeof( text ), "leaderboard %sSlalom%s %lld", r.track.c_str( ), r.course.c_str( ), static_cast< long long >( value ) );
        race_rules_platform( r, text );
    }
    if ( place > 0 && _mm_comigt_ss( _mm_set_ss( rules_f( h, 0x328 ) ), _mm_set_ss( 0.0f ) ) && !h[ 0x32D ] )
        race_rules_platform( r, "vo20 DidNotUseBoost" );
}

inline void slalom_rules_construct( race_rules_t& r )
{
    timetrial_rules_construct( r );
    fsm_state_t& pre = *r.fsm.m_states[ 0 ];
    pre.m_address[ 0 ] = 0x48DC30u, pre.m_address[ 1 ] = 0x48DCA0u, pre.m_address[ 2 ] = 0x48DCF0u, pre.m_address[ 3 ] = 0x48DD50u;
    pre.m_draw = [ &r ] { race_rules_hud( r, 0, race_hud_verb_t::draw ); };
    fsm_state_t& game = *r.fsm.m_states[ 1 ];
    game.m_exit = [ &r ] { race_rules_hud( r, 1, race_hud_verb_t::release ); timetrial_rules_place( r, "Slalom", "Times", 0x48DE10u ); };
    game.m_address[ 0 ] = 0x48DDB0u, game.m_address[ 1 ] = 0x48DE10u, game.m_address[ 2 ] = 0x48DF90u, game.m_address[ 3 ] = 0x48E030u;
    game.m_draw = [ &r ] { race_rules_hud( r, 1, race_hud_verb_t::draw ); };
    fsm_state_t& post = *r.fsm.m_states[ 2 ];
    post.m_enter = [ &r ] {
        race_rules_hud( r, 2, race_hud_verb_t::initialize );
        if ( r.hooks.broadcast )
            r.hooks.broadcast( "OnSetAllowPause", false );
        slalom_rules_post_results( r );
    };
    post.m_address[ 0 ] = 0x48E090u, post.m_address[ 1 ] = 0x48E300u, post.m_address[ 2 ] = 0x48E330u, post.m_address[ 3 ] = 0x48E3D0u;
    post.m_draw = [ &r ] { race_rules_hud( r, 2, race_hud_verb_t::draw ); };
}

// ---- The other game modes' ctors: VuSlalomGame 0x48BEC0, VuTimeTrialGame
//      0x48ED30, VuGhostGame 0x484A20 (world batch 21) ---------------------
//
// Created by 0x4BA060 by the mode's name (operator new 0x770 / 0x740 /
// 0x778; VuRaceGame's 0x748), each thiscall(game, project) retn 4 and built
// as VuRaceGame's 0x489030 is: VuGame's base 0x480BA0 first (its +0x0C /
// +0x67C machines and its events OnUserNext 0x483170 / OnCheatFinish 0x483190,
// VuMethod1 vtable 0x73F65C), the game's own events (0x5156E0), its FSM's
// states (addState 0x555850) and transitions (addTransition 0x555920, "" =
// every state), then its three HUD projects (0x4B4360) into three words.
// VuGhostGame's FSM is at +0x700 (its +0x6F8 / +0x6FC hold two more vtables,
// 0x73F7D0 / 0x73F7C8). VuSlalomGame also makes a Vu3dDrawComponent (0x531A40)
// into +0x744 (its +0x24 .. +0x40 = (-1e9 x3, 1e9 x3) as an aabb; the draw
// manager's add only when its +0x1E is set - not after the ctor). The tables
// are verified against the three ctors run whole (case game_ctors). The
// STATE HANDLERS ARE NOT READ: each is a hook (`unrecreated`, UNKNOWN) - the
// rules of these modes are not recreated yet.
struct game_mode_state_def_t
{
    const char* name;
    std::uint32_t address[ 4 ];   // enter / exit / tick / draw (0: none)
};

struct game_mode_def_t
{
    const char* ctor;
    const char* rtti;
    std::uint32_t vtable, size, fsm_at;
    std::uint32_t method0_vtable, method1_vtable, event_vtable;   // VuMethod0 / VuMethod1<float> / VuMethod1<VuParams const&>
    const char* huds[ 3 ];
    std::uint32_t hud_at[ 3 ];
    std::vector< game_mode_state_def_t > states;
    std::vector< std::array< const char*, 3 > > transitions;     // from ("" = every state), to, condition
    std::vector< race_rules_event_t > events;                    // the game's own, after VuGame's two
};

inline constexpr race_rules_event_t k_vugame_events[ 2 ] = { { "OnUserNext", 0x483170u }, { "OnCheatFinish", 0x483190u } };
inline constexpr std::uint32_t k_vugame_event_vtable = 0x73F65Cu;

inline const std::vector< game_mode_def_t >& game_mode_defs( )
{
    static const std::vector< game_mode_def_t > defs = {
        { "0x48BEC0", ".?AVVuSlalomGame@@", 0x73FA6Cu, 0x770u, 0x6F8u, 0x73FA3Cu, 0x73FA2Cu, 0x73FA5Cu,
          { "HUDs/SlalomPreGameHUD", "HUDs/SlalomGameHUD", "HUDs/SlalomPostGameHUD" }, { 0x75Cu, 0x760u, 0x764u },
          { { "PreGame", { 0x48DC30u, 0x48DCA0u, 0x48DCF0u, 0x48DD50u } }, { "Game", { 0x48DDB0u, 0x48DE10u, 0x48DF90u, 0x48E030u } },
            { "PostGame", { 0x48E090u, 0x48E300u, 0x48E330u, 0x48E3D0u } }, { "Exit", { 0, 0, 0, 0 } } },
          { { "PreGame", "Game", "StartLightsOut" }, { "Game", "PostGame", "Finished" }, { "", "Exit", "Exit" } },
          { { "OnStartLightsOut", 0x48DB40u }, { "OnBoatRecovered", 0x48DB70u } } },
        { "0x48ED30", ".?AVVuTimeTrialGame@@", 0x73FB9Cu, 0x740u, 0x6F8u, 0x73FB7Cu, 0x73FB6Cu, 0x73FB8Cu,
          { "HUDs/TimeTrialPreGameHUD", "HUDs/TimeTrialGameHUD", "HUDs/TimeTrialPostGameHUD" }, { 0x72Cu, 0x730u, 0x734u },
          { { "PreGame", { 0x490570u, 0x4905E0u, 0x490630u, 0x490690u } }, { "Game", { 0x4906F0u, 0x490750u, 0x4908B0u, 0x490950u } },
            { "PostGame", { 0x4909B0u, 0x490BD0u, 0x490C00u, 0x486720u } }, { "Exit", { 0, 0, 0, 0 } } },
          { { "PreGame", "Game", "StartLightsOut" }, { "Game", "PostGame", "Finished" }, { "", "Exit", "Exit" } },
          { { "OnStartLightsOut", 0x490540u } } },
        { "0x484A20", ".?AVVuGhostGame@@", 0x73F7DCu, 0x778u, 0x700u, 0x73F7A0u, 0x73F790u, 0x73F7B0u,
          { "HUDs/GhostPreGameHUD", "HUDs/GhostGameHUD", "HUDs/GhostPostGameHUD" }, { 0x734u, 0x738u, 0x73Cu },
          { { "GetLeaderboard", { 0x485890u, 0x485AA0u, 0x485AF0u, 0 } }, { "LoadGhosts", { 0x485B50u, 0x485AA0u, 0, 0 } },
            { "PreGame", { 0x486060u, 0x486660u, 0x4866C0u, 0x486720u } }, { "Game", { 0x486780u, 0x486880u, 0x486A70u, 0x486C00u } },
            { "PostGame", { 0x486EC0u, 0x4872A0u, 0x4872D0u, 0x487370u } }, { "ShowError", { 0x4873D0u, 0x487560u, 0, 0 } },
            { "Exit", { 0, 0, 0, 0 } } },
          { { "GetLeaderboard", "LoadGhosts", "Success" }, { "GetLeaderboard", "ShowError", "Failure" }, { "LoadGhosts", "PreGame", "Success" },
            { "LoadGhosts", "ShowError", "Failure" }, { "PreGame", "Game", "StartLightsOut" }, { "Game", "PostGame", "Finished" },
            { "ShowError", "Exit", "MessageBoxClosed" }, { "", "Exit", "Exit" } },
          { { "OnStartLightsOut", 0x485860u } } } };
    return defs;
}

// The ctor's FSM: the states in order (each handler a hook calling
// `unrecreated` - not read), then the transitions in the ctor's order.
inline void game_mode_construct( c_fsm& f, const game_mode_def_t& d, const std::function< void( std::uint32_t, const char* ) >& unrecreated )
{
    f = c_fsm{ };
    for ( const game_mode_state_def_t& s : d.states )
    {
        fsm_state_t& st = f.add_state( s.name );
        for ( int k = 0; k < 4; ++k )
            st.m_address[ k ] = s.address[ k ];
        const std::string what = std::string( d.rtti ) + " " + s.name;
        const auto hook = [ unrecreated, what ]( std::uint32_t a, const char* verb ) {
            return [ unrecreated, what, a, verb ] {
                if ( unrecreated )
                    unrecreated( a, ( what + " " + verb ).c_str( ) );
            };
        };
        if ( s.address[ 0 ] )
            st.m_enter = hook( s.address[ 0 ], "enter" );
        if ( s.address[ 1 ] )
            st.m_exit = hook( s.address[ 1 ], "exit" );
        if ( s.address[ 2 ] )
        {
            auto h = hook( s.address[ 2 ], "tick" );
            st.m_tick = [ h ]( float ) { h( ); };
        }
        if ( s.address[ 3 ] )
            st.m_draw = hook( s.address[ 3 ], "draw" );
    }
    for ( const auto& t : d.transitions )
        f.add_transition( t[ 0 ], t[ 1 ], t[ 2 ] );
}

// VuRaceGame slot 5 0x4896A0's "the state machine enters its first state":
// the current state's (+0x6F8 -> PreGame) enter.
inline void race_rules_start( race_rules_t& r )
{
    if ( fsm_state_t* s = r.fsm.current( ) )
        if ( s->m_enter )
            s->m_enter( );
}

// 0x48A780 ("OnStartLightsOut"): the condition StartLightsOut's +28 = 1.
inline void race_rules_on_start_lights_out( race_rules_t& r ) { r.fsm.set_condition( "StartLightsOut", true ); }

// 0x48A7B0 ("OnCheckPointBuoyHit"): 0x4237B0 reads the params' first entry -
// when the size (+0x80, signed) is above 0 a 4-byte tag, then when the tag is
// 0 the next 4 bytes as the value whatever the size left (memcpy_s(4, 4): the
// size is only decremented); else 0: +0x744 |= 1 << that value (shl: the count
// taken mod 32).
inline void race_rules_on_buoy_hit( race_rules_t& r, const std::uint8_t* params, std::int32_t size )
{
    std::int32_t value = 0;
    if ( size > 0 )
    {
        std::int32_t tag = -1;
        std::memcpy( &tag, params, 4 );
        if ( tag == 0 )
            std::memcpy( &value, params + 4, 4 );
    }
    r.buoys |= 1u << ( static_cast< std::uint32_t >( value ) & 31u );
}

// ---- VuCheckpointBuoyEntity's contact callback 0x4350B0 (the broadcaster) ---
//
// VuRigidBodyContactCallback slot 0 (vtable 0x73A24C; thiscall on the
// entity's +0xB4 part - the pointer VuDynamicGamePropEntity's gameInitialize
// 0x437580 stores in its body's +0x2C4; the contact; retn 4). First the base
// callback 0x437F40 (VuDynamicGamePropEntity's own contact response - NOT
// recreated: the caller's `base` hook). Then the contact's other body (+8):
// its word +0x2CA must EQUAL 0x10 (cmp word / jne - not a bit test), its
// owner +0x2C0 non-null and its vt+4 RTTI chain must reach VuBoatEntity
// 0x73628C; the owner's driver (+0x220) vt+0x38 (0x404BD0, true in every
// driver vtable) and vt+0x30 (boat_driver_kind's human) must both answer
// true. Then a VuParams on the stack (size +0x80 = 0) gets 0x434E50's int:
// a 4-byte tag 0 and the entity's +0x184 (the part's +0xD0: the buoy's
// index), size 8; and VuEventManager g 0x759028's 0x505650 broadcasts it as
// "OnCheckPointBuoyHit" (its FNV-1a hash -> 0x505690). The handler is
// VuRaceGame's 0x48A7B0 (race_rules_on_buoy_hit).
// Answers 1 (the broadcast: `params` filled, `size` 8), 0 (none), -1 for a
// driver vtable that is not one of the five (UNKNOWN - no answer made up).
inline constexpr const char* k_buoy_hit_event = "OnCheckPointBuoyHit";

struct buoy_contact_t
{
    std::uint16_t group = 0;         // the other body's +0x2CA
    bool owner = false;              // its +0x2C0 non-null
    bool owner_is_boat = false;      // the owner's vt+4 RTTI chain reaches 0x73628C
    std::uint32_t driver_vtable = 0; // the owner's +0x220 driver's vtable
    std::int32_t index = 0;          // the buoy entity's +0x184
};

inline int checkpoint_buoy_contact( const buoy_contact_t& c, const std::function< void( ) >& base, std::uint8_t ( &params )[ 0x84 ], std::int32_t& size )
{
    if ( base )
        base( );
    size = 0;
    if ( c.group != 0x10 || !c.owner || !c.owner_is_boat )
        return 0;
    boat_driver_kind_t kind;
    if ( !boat_driver_kind( c.driver_vtable, kind ) )
        return -1;
    if ( !kind.human )   // vt+0x38 is true for all five
        return 0;
    const std::int32_t tag = 0;
    std::memcpy( params, &tag, 4 );
    std::memcpy( params + 4, &c.index, 4 );
    size = 8;
    std::memcpy( params + 0x80, &size, 4 );
    return 1;
}

// Slot 7 0x489A40(dt): evaluate 0x555AD0, the current state's tick, +0x71C +=
// dt; false once the current state's name is "Exit".
inline bool race_rules_tick( race_rules_t& r, float dt )
{
    r.fsm.evaluate( );
    fsm_state_t* s = r.fsm.current( );
    if ( s && s->m_tick )
        s->m_tick( dt );
    r.fsm.m_time = r.fsm.m_time + dt;
    if ( r.hooks.gates )
        r.hooks.gates( );   // VuSlalomGame's 0x48C8C0: 0x48E6E0 here
    s = r.fsm.current( );
    return !s || s->m_name != "Exit";
}
// ---- VuMultiDelayEntity (create 0x5DFD00, ctor 0x5DFD80) ---------------------
//
// The countdown's clock (the HUD template HUD/Countdown). Eight VuBasicProperty
// <float,1> "Delay 1" .. "Delay 8" -> +0x60 .. +0x7C (default 0); the input
// plug "In" (0x5E2AD0 -> 0x5E0660); outputs "1" .. "8" (+0x80 .. +0x9C); its
// gameInitialize 0x5E0610 registers 0x5E06B0 in "Decision".
struct multi_delay_state_t
{
    float value[ 8 ] = { };
    float defaults[ 8 ] = { };
};

inline constexpr const char* k_multi_delay_class = "VuMultiDelayEntity";

inline std::shared_ptr< void > multi_delay_state_for( const std::string& cls )
{
    return cls == k_multi_delay_class ? std::make_shared< multi_delay_state_t >( ) : nullptr;
}

inline void multi_delay_load_properties( entity_t& e, const value_t& properties )
{
    if ( e.m_type != k_multi_delay_class || !e.m_state )
        return;
    multi_delay_state_t& s = *static_cast< multi_delay_state_t* >( e.m_state.get( ) );
    static const char* names[ 8 ] = { "Delay 1", "Delay 2", "Delay 3", "Delay 4", "Delay 5", "Delay 6", "Delay 7", "Delay 8" };
    for ( int k = 0; k < 8; ++k )
        property_float( properties, names[ k ], s.value[ k ], s.defaults[ k ], false );
}

inline void multi_delay_mark_properties( entity_t& e )
{
    if ( e.m_type != k_multi_delay_class || !e.m_state )
        return;
    multi_delay_state_t& s = *static_cast< multi_delay_state_t* >( e.m_state.get( ) );
    std::memcpy( s.defaults, s.value, sizeof( s.defaults ) );
}

// The runtime words: +0xA0 active, +0xA4 the time, +0xA8 the In's params.
struct multi_delay_t
{
    float delay[ 8 ] = { };
    std::uint8_t active = 0;
    float time = 0.0f;
    std::uint8_t params[ 0x84 ] = { };
};

// "In" 0x5E0660 (thiscall; VuRetVal* out, VuParams const&; retn 8): out = 0;
// when not active: +0xA0 = 1, +0xA4 = 0xB4000000 (-2^-23, so a Delay of 0
// fires on the first tick) and +0xA8 = a LOCAL VuParams (rep movsd 0x21 dwords
// from [ebp-0x84], whose size word [ebp-4] is set 0 - the incoming params are
// NOT copied; the oracle shows +0xA8 never equals them): an empty VuParams,
// its 0x80 data bytes stack garbage nothing reads (size 0) - zeros here.
// Active: ignored.
inline void multi_delay_in( multi_delay_t& m )
{
    if ( m.active )
        return;
    m.active = 1;
    const std::uint32_t start = 0xB4000000u;
    std::memcpy( &m.time, &start, 4 );
    std::memset( m.params, 0, sizeof( m.params ) );
}

// The Decision tick 0x5E06B0(dt): only while active. t = dt + time; active =
// 0; for each output k: when Delay k > the OLD time (comiss, ordered) - t >=
// Delay k (comiss / jb: ordered) fires output k with the stored params, else
// active = 1 again; then time = t.
inline void multi_delay_tick( multi_delay_t& m, float dt, const std::function< void( int output, const std::uint8_t* params ) >& fire )
{
    if ( !m.active )
        return;
    const float t = dt + m.time;
    m.active = 0;
    for ( int k = 0; k < 8; ++k )
    {
        const float d = m.delay[ k ];
        if ( !_mm_comigt_ss( _mm_set_ss( d ), _mm_set_ss( m.time ) ) )
            continue;
        if ( _mm_comige_ss( _mm_set_ss( t ), _mm_set_ss( d ) ) )
        {
            if ( fire )
                fire( k, m.params );
        }
        else
            m.active = 1;
    }
    m.time = t;
}