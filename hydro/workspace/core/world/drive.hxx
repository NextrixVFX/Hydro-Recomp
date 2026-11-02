#pragma once

#include <workspace/core/boat/anim.hxx>
#include <workspace/core/boat/camera.hxx>
#include <workspace/core/boat/contact.hxx>
#include <workspace/core/boat/effects.hxx>
#include <workspace/core/boat/fleet.hxx>
#include <workspace/core/boat/human.hxx>
#include <workspace/core/boat/manager.hxx>
#include <workspace/core/boat/pilots.hxx>
#include <workspace/core/boat/rays.hxx>
#include <workspace/core/boat/respawn.hxx>
#include <workspace/core/boat/state.hxx>
#include <workspace/core/game/input.hxx>
#include <workspace/core/world/blend.hxx>
#include <workspace/core/world/build.hxx>
#include <workspace/core/world/dynamics.hxx>
#include <workspace/core/world/kernel.hxx>
#include <workspace/core/world/query.hxx>
#include <workspace/core/world/pickups.hxx>
#include <workspace/core/world/session.hxx>
#include <workspace/core/world/tree.hxx>
#include <workspace/core/world/wake.hxx>
#include <workspace/utility/logger.hxx>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <xmmintrin.h>

// The race's per-boat Decision tick (VuBoatEntity slot 13 0x41EFF0 =
// boat_decision_tick, core/boat state.hxx) and the boat manager's Viewports
// tick 0x4AB0A0 (boat_manager_viewport_tick, core/boat manager.hxx), composed
// on the session's boat objects. Each hook 0x41EFF0 calls is one of:
//
//   RECREATED, run:  the driver's tick vt+0x48 - VuHumanDriver 0x40F270
//                    (human_decision_driver) and VuAiDriver 0x40B940
//                    (fleet_driver_tick); the camera 0x409900
//                    (boat_camera_tick); the respawn 0x41D790 (boat_respawn);
//                    VuWater's wake lookup 0x5296C0 (water_best_wake); the
//                    water under a point 0x529140 (water_point_height); the
//                    hull's wake update 0x417D80 (race_drive_hull_wake:
//                    0x417F80 + world/wake.hxx water_wake_update).
//   The animation tick 0x40FFF0 = world/rig.hxx on boat_anim_tick.
//   NOT RECREATED / NOT WIRED (counted, each logged once by name - never a
//   stand-in): 0x410FF0, 0x41E240, the
//   debris pieces' follow / removal; the driver tick of a UI / nuisance /
//   ghost driver (none in a Race roster - their ticks are the boat agent's);
//   the hull's splash pfx 0x52BCD0, the lens-water manager g 0x759048 (VuLensWaterManagerImpl, the water /
//   render side's). RESOLVED (world batch 19): the driver's vt+0x50 (the human's 'OnHumanRecovered' and
//   0x41D790's 'OnBoatRecovered' have no subscriber in a Race - nothing runs) and 0x41E240 (FMOD - platform). WIRED (world batch 18): the trigger manager's 0x536350
//   (trigger/volume.hxx) and the respawn's 0x534C00(0) (no children: nothing).
//   PLATFORM HOOKS (not copied - BRIEF): FMOD (every get_event answers false:
//   no event is made, so no audio calls follow), the stats / voice-over
//   service g 0x758FEC, the gamepad rumble, the accelerometer, the display
//   size. The input is game/input.hxx's platform_input_t: the recreated
//   managers over XInput and the keyboard.
//   RUN ON THE RACE'S DYNAMICS WORLD (world/dynamics.hxx, world/session.hxx
//   race_session_enable): slots 17 / 18 0x421980 / 0x421940 (boat_enable /
//   boat_disable) - the body into the world, the +0x60 interface onto its step
//   callbacks; the world's frame is STOPPED while tickThunk 0x41E5E0 has no
//   context (dynamics.hxx).
//   NOT WIRED, ANSWERING "NO HIT" (UNKNOWN - flagged, logged once): the
//   dynamics world's ray test (g_vudynamicsimpl vt+0x6C's collision world
//   vt+0x18) the camera's collision 0x40B090 and the AI's avoidance 0x40C750
//   cast - no collision world holds the level in the race yet.
//
// The settings manager's values the human tick reads (g_vusettingsmanager
// 0x758FE0 +0x2C / +0x2D / +0x30 / +0x34): race_world_t::settings, each tick.

// The engine's +0xF8 std::vector of 8-byte records: 0x413700 resizes it to
// the drive count through 0x4153A0, whose growth 0x415700 writes each new
// record as {byte 0, float 0}; 0x414760 refills them per tick (in the Build
// tick - not recreated). The engine image's +0xF8 / +0xFC words are the
// vector's begin / end: model addresses 0 and 8n (only their difference is
// read, by 0x413830).
struct race_drive_t
{
    bool ghost = false;   // VuGhostDriver 0x737DDC
    ghost_pilot_t ghost_pilot;
    race_session_t* session = nullptr;
    std::size_t index = 0;
    std::vector< std::uint8_t > records;
    boat_decision_t d{};
    boat_decision_hooks_t hooks{};
    // The driver step, by class.
    bool human = false, ai = false, nuisance = false;
    int pilot = -1;   // the fleet pilot of an AI boat
    // VuAiNuisanceDriver (vtable 0x737C4C): its pilot (boat pilots.hxx) and target views - no target until the
    // DriverTarget plug (0x42FF90, not wired) sets one.
    nuisance_pilot_t nuisance_pilot;
    nuisance_target_t nuisance_target;
    human_pilot_t human_pilot;
    human_decision_driver_t human_driver;
    camera_env_t camera_env;
    boat_respawn_t respawn{};
    boat_respawn_hooks_t respawn_hooks{};
    // Build step 5's pfx controller (boat effects.hxx): its on bytes / aabb, read from the image once, written back.
    bool pfx_read = false;
    int pfx_ticks = 0;   // ours: Build step 5 runs (the dev census below)
};

inline void race_drive_contact_setup( race_session_t& s );
inline void race_drive_release_target( race_drive_t& r );

inline void race_unwired( race_session_t& s, const std::string& what )
{
    if ( s.unwired[ what ]++ == 0 )
        LOG_WARNING( "race: %s", what.c_str( ) );
}

// ---- THE TEST INPUT (ours, FLAGGED: not retail) -----------------------------
//
// Retail's input is a platform hook (the input device behind the control
// method manager); a --bench run has no player. --input-script FILE answers
// that hook from a file of pad states instead of the device, so the human
// boat can be driven through the verified input -> controls mapping
// (VuHumanDriver's 0x40F690, boat human.hxx) in a repeatable run. One key per
// line, "FRAME THROTTLE BRAKE STEERING BOOST" (the four channels the device
// answer has: the right / left trigger 0..1, the stick -1..1, the shoulder 0 /
// 1); a key holds from its session frame until the next one; '#' starts a
// comment. A TEST INPUT, logged as such - never a stand-in for the AI.
struct race_input_script_t
{
    struct key_t
    {
        int frame = 0;
        input_t pad{};
    };
    std::vector< key_t > keys;
    std::string path;
};

inline bool race_input_script_load( const std::string& path, race_input_script_t& out )
{
    std::ifstream f( path );
    if ( !f )
        return false;
    out = race_input_script_t{ };
    out.path = path;
    std::string line;
    while ( std::getline( f, line ) )
    {
        const std::size_t hash = line.find( '#' );
        if ( hash != std::string::npos )
            line.resize( hash );
        std::istringstream in( line );
        race_input_script_t::key_t k;
        int boost = 0;
        if ( in >> k.frame >> k.pad.throttle >> k.pad.brake >> k.pad.steer )
        {
            if ( in >> boost )
                k.pad.boost = boost != 0;
            out.keys.push_back( k );
        }
    }
    return !out.keys.empty( );
}

// The pad state at a session frame: the last key at or before it (none: a released pad).
inline input_t race_input_script_at( const race_input_script_t& s, int frame )
{
    input_t pad{};
    for ( const race_input_script_t::key_t& k : s.keys )
        if ( k.frame <= frame )
            pad = k.pad;
    return pad;
}

// The platform input hooks' answers (VuInputManagerImpl slots 6 / 8 / 9 on pad 0): the script's channels when one is loaded,
// else the manager. A channel the script has no column for answers 0 with a script.
inline float race_autopilot_axis( const race_session_t& s, const char* name );   // world/autopilot.hxx

inline float race_drive_axis( const race_session_t& s, std::int32_t pad, const char* name )
{
    if ( s.autopilot )
        return race_autopilot_axis( s, name );
    if ( s.input_script )
    {
        const input_t in = race_input_script_at( *s.input_script, s.frames );
        return std::strcmp( name, "Throttle" ) == 0 ? in.throttle : std::strcmp( name, "Brake" ) == 0 ? in.brake
               : std::strcmp( name, "Steering" ) == 0 ? in.steer : 0.0f;
    }
    return s.input ? s.input->manager.value( pad, name ) : 0.0f;
}

inline std::uint8_t race_drive_held( const race_session_t& s, std::int32_t pad, const char* name )
{
    if ( s.input_script )
        return std::strcmp( name, "Boost" ) == 0 && race_input_script_at( *s.input_script, s.frames ).boost ? 1 : 0;
    return s.input && s.input->manager.held( pad, name ) ? 1 : 0;
}

inline std::uint8_t race_drive_pressed( const race_session_t& s, std::int32_t pad, const char* name )
{
    if ( s.input_script )
        return 0;
    return s.input && s.input->manager.pressed( pad, name ) ? 1 : 0;
}

// g 0x759058's first surface's +0xD8 (boat_decision_t::floor). VuWater +0x20
// is a std::list of the surfaces (+0x24 its size): VuWater's surface add
// 0x5284C0 pushes each new surface on the BACK (0x4031F0 before the head) and
// then sorts the whole list with std::list::sort 0x52A670 and the predicate
// 0x527F50 - a < b when b's +0x14 (the height) is greater (comiss / seta). A
// stable sort run after each add leaves first the EARLIEST-added surface of
// the least height. Null with no surface (+0x24 == 0). A NaN height makes the
// predicate inconsistent: UNKNOWN (`unknown` set, null returned).
inline const float* race_drive_floor( const water_built_t* water, bool& unknown )
{
    unknown = false;
    if ( !water || water->surfaces.empty( ) )
        return nullptr;
    std::size_t best = 0;
    for ( std::size_t k = 0; k < water->surfaces.size( ); ++k )
    {
        const float h = water->surfaces[ k ].f( 20 );
        if ( h != h || !water->surfaces[ k ].known[ 20 / 4 ] || !water->surfaces[ k ].known[ 216 / 4 ] )
        {
            unknown = true;
            return nullptr;
        }
        if ( k > 0 && _mm_comigt_ss( _mm_set_ss( water->surfaces[ best ].f( 20 ) ), _mm_set_ss( h ) ) )
            best = k;
    }
    return reinterpret_cast< const float* >( &water->surfaces[ best ].word[ 216 / 4 ] );
}

// The VerticalFOV container as 0x4D2C60 reads it (+0 the type, +8 the payload).
inline camera_db_value_t race_drive_fov_value( const value_t& v )
{
    camera_db_value_t r;
    r.type = static_cast< std::int32_t >( v.tag );
    if ( v.tag == 1 )
        r.payload = static_cast< std::uint32_t >( v.integer );
    else if ( v.tag == 2 )
    {
        std::uint32_t bits;
        std::memcpy( &bits, &v.real, 4 );
        r.payload = bits;
    }
    else if ( v.tag == 7 )
        r.payload = static_cast< std::uint64_t >( v.int64 );
    return r;
}

// ---- the hooks -------------------------------------------------------------

inline race_drive_t& race_drive_of( void* user )
{
    return *static_cast< race_drive_t* >( user );
}

inline void race_drive_animation( race_drive_t& r, float dt );

inline race_session_boat_t& race_drive_boat( race_drive_t& r )
{
    return *r.session->boats[ r.index ];
}

// The dynamics world's ray test (g_vudynamicsimpl vt+0x6C's collision world vt+0x18): the boat agent's
// boat_ray_hits (boat/rays.hxx, engine collision/raytest.hxx) on the race's collision world; a hit object's
// image is its VuRigidBody (the solver_rigid_t starts it). A ray that reached a path not recreated (rw.unknown:
// e.g. the BVH ray path of the level's triangle meshes) is logged UNKNOWN.
inline int race_drive_ray( void* user, const float* from, const float* to, const ai_ray_hit_t** hits )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    *hits = nullptr;
    if ( !s.dynamics )
    {
        race_unwired( s, "the dynamics world's ray test with no dynamics world - no hit (UNKNOWN)" );
        return 0;
    }
    boat_ray_world_t& rw = s.rays;
    rw.world = &s.dynamics->collision;
    rw.image = []( void*, const collision_object_t& o ) { return reinterpret_cast< const std::uint8_t* >( o.body.object ); };
    rw.unknown = false;
    const int n = boat_ray_hits( &rw, from, to, hits );
    if ( rw.unknown )
        race_unwired( s, "the dynamics world's ray test reached a path not recreated (boat rays.hxx: rw.unknown) - its hits are retail's only up to there (UNKNOWN)" );
    return n;
}

// 0x40C750's aabb: the hit body's shape (+0xCC) vt+4 getAabb( body +0x10, min, max ). A body not in the world answers an empty box and logs UNKNOWN.
inline void race_drive_aabb( void* user, const std::uint8_t* body, float* mn, float* mx )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    if ( s.dynamics )
        for ( const collision_object_t* o : s.dynamics->collision.objects )
            if ( reinterpret_cast< const std::uint8_t* >( o->body.object ) == body )
            {
                if ( !body_get_aabb( o->body, o->body.transform, mn, mx ) )
                    race_unwired( s, "0x40C750: a hit body's getAabb reached a shape not recreated (UNKNOWN)" );
                return;
            }
    race_unwired( s, "0x40C750: the ray's hit body is not in the collision world (UNKNOWN)" );
}

// 0x5296C0 on the probe point with the callback {vtable, point x, y,
// direction x, y}: the finder starts at 0 (retail's [ebp-8]); water +0x18
// (skip waves) is the build's 0.
inline float race_drive_wake( void* user, const boat_probe_io_t& io )
{
    race_drive_t& r = race_drive_of( user );
    const race_world_t& w = r.session->world;
    if ( !w.water )
    {
        race_unwired( *r.session, "VuWater 0x5296C0 asked with no VuWater built (UNKNOWN answer 0)" );
        return 0.0f;
    }
    water_wake_finder_t f;
    std::memcpy( f.point, &io.callback[ 1 ], 8 );
    std::memcpy( f.direction, &io.callback[ 3 ], 8 );
    f.best = 0.0f;
    water_best_wake( w.water->hold, false, io.point, f );
    return f.best;
}

// The hull's wake update 0x417D80 (thiscall hull; the boat state tick's last
// call): the 0x34-byte record memset to 0, then 0x417F80 (boat anim.hxx
// boat_wake_params: the hull, the boat's transform world, the body boat
// +0x21C, the solver hull +0x1CC) into it; then on VuWater g 0x759058 with
// the descriptor hull +0x1D0 (3 words) and the record's 13 words: hull
// +0x1EC set - the flat wake on slots +0x1E4 / +0x1E8, else the wake on
// +0x1DC / +0x1E0 (world/wake.hxx water_wake_update, the water half). When
// the plain wake made a new segment (1): solver +0x30 = 2 and its +0x34 array
// [0] = +0x1DC, [1] = +0x1E0 (the slots after the swap). The slots hold
// race_wave_token words. UNKNOWN (false, `why`, nothing written): a slot word
// that is no token; the plain wake with no solver / +0x34 array of 8 bytes
// (retail writes through them). The part after 0x417F80, on its record:
inline bool race_hull_wake_apply( water_built_t& water, std::uint8_t* hull, std::uint8_t* solver, std::uint8_t* array, const std::uint32_t* record,
                                  std::string& why )
{
    std::uint32_t desc[ 3 ];
    std::memcpy( desc, hull + 0x1D0, 12 );
    const bool flat = hull[ 0x1EC ] != 0;
    const int at = flat ? 0x1E4 : 0x1DC;
    water_wake_slots_t slots;
    slots.current = race_wave_of( &water, load_word( hull, at ) );
    slots.previous = race_wave_of( &water, load_word( hull, at + 4 ) );
    if ( slots.current == -2 || slots.previous == -2 )
    {
        why = "a wake slot word that names no wave";
        return false;
    }
    if ( !flat && ( !solver || !array ) )
    {
        why = "the plain wake with no solver (hull +0x1CC) / +0x34 array of 8 bytes in the boat's graph";
        return false;
    }
    const int made = water_wake_update( water, desc, record, flat, slots );
    image_put32( hull, at, race_wave_token( slots.current ) );
    image_put32( hull, at + 4, race_wave_token( slots.previous ) );
    if ( made == 1 )
    {
        image_put32( solver, 0x30, 2 );
        image_put32( array, 0, load_word( hull, 0x1DC ) );
        image_put32( array, 4, load_word( hull, 0x1E0 ) );
    }
    return true;
}

inline void race_drive_hull_wake( void* user )
{
    race_drive_t& r = race_drive_of( user );
    race_session_t& s = *r.session;
    race_session_boat_t& b = race_drive_boat( r );
    if ( !s.world.water )
    {
        race_unwired( s, "the hull's wake update 0x417D80 with no VuWater (g 0x759058 null) - UNKNOWN, not run" );
        return;
    }
    std::uint8_t* hull = r.d.hull;
    std::uint8_t out[ 0x34 ] = { };
    float world[ 16 ];
    std::memcpy( world, r.d.transform + 0x50, 64 );
    boat_wake_params( hull, world, r.d.body, r.d.solver, out );
    std::uint32_t record[ 13 ];
    std::memcpy( record, out, 52 );
    const int hull_k = race_session_ref( b.graph, 0, 0x228 );
    std::uint8_t* solver = race_session_image( b, hull_k, 0x1CC );   // r.d.solver's bytes, writable
    const int solver_k = race_session_ref( b.graph, hull_k, 0x1CC );
    std::uint8_t* array = race_session_image( b, solver_k, 0x34 );
    if ( array )
        for ( const image_ref_t& ref : b.graph.objects[ static_cast< std::size_t >( solver_k ) ].refs )
            if ( ref.slot == 0x34 && ref.object >= 0 &&
                 b.graph.objects[ static_cast< std::size_t >( ref.object ) ].image.size( ) < static_cast< std::size_t >( ref.offset ) + 8 )
                array = nullptr;
    std::string why;
    if ( !race_hull_wake_apply( *s.world.water, hull, solver, array, record, why ) )
        race_unwired( s, "the hull's wake update 0x417D80: " + why + " - UNKNOWN, not run" );
}

// 0x529140's out +0x20 under a point (respawn.hxx's water hook).
inline float race_drive_water( void* user, const float* point )
{
    race_drive_t& r = race_drive_of( user );
    const race_world_t& w = r.session->world;
    water_vertex_t out{};
    if ( !w.view )
        return respawn_bits( 0xFEFFFFFFu );
    if ( !water_point_height( w.view->water, point, water_view_kernel, const_cast< water_view_t* >( w.view ), out ) )
        race_unwired( *r.session, "0x529140 reached a water path not recreated - the respawn's water answer is UNKNOWN" );
    return out.height;
}

inline void race_drive_driver( void* user, float dt )
{
    race_drive_t& r = race_drive_of( user );
    race_session_t& s = *r.session;
    if ( r.human )
    {
        human_env_t& env = r.human_driver.env;
        env.control_method = s.world.control_method;
        if ( const settings_t* set = s.world.settings )
        {
            env.brake_only_0 = set->auto_throttle_pad ? 1u : 0u;
            env.brake_only_1 = set->auto_throttle_keys ? 1u : 0u;
            env.tilt_blend = set->ui.m_steering;
            env.rumble = set->ui.m_vibration ? 1u : 0u;
        }
        human_decision_driver( &r.human_driver, dt );
        return;
    }
    if ( r.ai && r.pilot >= 0 )
    {
        std::vector< const std::uint8_t* > transforms;
        for ( const race_start_boat_t& b : s.start_boats )
            transforms.push_back( b.transform );
        std::vector< const std::uint8_t* > humans;
        for ( const void* b : s.manager.lists.humans.boats )
            humans.push_back( static_cast< const std::uint8_t* >( b ) );
        ai_tick_t t;
        t.boat = r.d.boat;
        t.body = r.d.body;
        t.solver = r.d.solver;
        t.transform = r.d.transform;
        t.engine = r.d.engine;
        t.owner = r.d.boat;   // engine +0 is the boat
        t.animation = r.d.animation;
        t.boats = humans.empty( ) ? nullptr : humans.data( );
        t.boat_count = static_cast< std::int32_t >( humans.size( ) );
        ai_tick_hooks_t h;
        h.avoid.user = &s;
        h.avoid.ray = race_drive_ray;
        h.avoid.aabb = race_drive_aabb;
        h.controls.user = &r;
        h.controls.misfire = []( void* u ) { race_unwired( *race_drive_of( u ).session, "audio: the BoostMisfire sound 0x41E060 (FMOD - platform)" ); };
        // The voice-over manager g 0x758FEC vt+0x1C (boat, line): world/voice.hxx's say-now on this boat.
        h.controls.voice = []( void* u, const char* line ) { race_session_vo_say( *race_drive_of( u ).session, race_drive_of( u ).d.boat, line, false ); };
        fleet_driver_tick( *s.track, s.pilots[ static_cast< std::size_t >( r.pilot ) ], t, transforms.data( ), dt, h );
        return;
    }
    if ( r.nuisance )
    {
        // Slot 18 0x40DA90 = nuisance_driver_tick (boat pilots.hxx). Its audio-line hooks (the boat's DB
        // "AudioLines", FMOD) are reached only with a target: absent here.
        ai_tick_t t;
        t.boat = r.d.boat;
        t.body = r.d.body;
        t.solver = r.d.solver;
        t.transform = r.d.transform;
        t.engine = r.d.engine;
        t.owner = r.d.boat;
        t.animation = r.d.animation;
        nuisance_hooks_t h;
        h.ai.avoid.user = &s;
        h.ai.avoid.ray = race_drive_ray;
        h.ai.avoid.aabb = race_drive_aabb;
        h.ai.controls.user = &r;
        h.ai.controls.misfire = []( void* u ) { race_unwired( *race_drive_of( u ).session, "audio: the BoostMisfire sound 0x41E060 (FMOD - platform)" ); };
        h.ai.controls.voice = []( void* u, const char* line ) { race_session_vo_say( *race_drive_of( u ).session, race_drive_of( u ).d.boat, line, false ); };
        // The boat's DB "AudioLines" (0x41D500 - the asset system, not wired): the count is answered -1 (the not-a-list
        // answer, so no line is picked) and logged UNKNOWN.
        h.line_count = []( void* u ) { race_unwired( *race_drive_of( u ).session, "the nuisance driver's AudioLines from the boat's DB 0x41D500 (the asset system, not wired) - answered -1, no line (UNKNOWN)" ); return -1; };
        // The weak reference's release 0x40E4D0 (vt+0 of +0xE4) on the session's words (boat nuisance.hxx); the target
        // views go with it.
        h.user = &r;
        h.release_target = []( void* u ) { race_drive_release_target( race_drive_of( u ) ); };
        nuisance_driver_tick( *s.track, r.nuisance_pilot, t, r.nuisance_target, *s.random, dt, h );
        return;
    }
    if ( r.ghost )
    {
        ghost_driver_tick( *s.track, r.ghost_pilot, r.d.transform );   // slot 18 0x40EBA0
        return;
    }
    if ( s.dock )
        return;   // VuAiUiDriver's tick 0x40E5C0: not recreated (world/dock.hxx)
    race_unwired( s, "a driver tick of a class not wired here (UI, or an AI boat with no pilot) - UNKNOWN" );
}

inline void race_drive_camera( void* user, float dt )
{
    race_drive_t& r = race_drive_of( user );
    const boat_decision_t& d = r.d;
    camera_boat_t cb;
    cb.world = reinterpret_cast< const float* >( d.transform + 0x50 );
    cb.destroyed = d.boat[ 0x46B ] != 0;
    std::memcpy( &cb.debris_count, d.debris + 0xA0, 4 );
    if ( cb.destroyed && cb.debris_count > 0 )
    {
        // The first piece's image (debris +0x9C's array).
        cb.piece = race_drive_boat( r ).debris.pieces[ 0 ]->image.data( );
    }
    cb.velocity = reinterpret_cast< const float* >( d.body + 0x140 );
    cb.engine = d.engine;
    cb.engine_owner_470 = state_f( d.boat, 0x470 );
    cb.far_plane = state_f( d.boat, 0x408 );
    r.camera_env.control_method = r.session->world.control_method;
    r.camera_env.display_w = r.session->world.display_w;
    r.camera_env.display_h = r.session->world.display_h;
    if ( !boat_camera_tick( d.camera, cb, r.camera_env, dt ) )
        race_unwired( *r.session, "the camera's mode +0x2A0 is none of 0 / 1 / 2 under control method 2 - UNKNOWN, nothing written" );
}

// What 0x412270 and 0x410FF0 ask of g_vudynamicsimpl and g_vupfximpl, by a piece's body word (the index + 1: world/pieces.hxx).
inline boat_launch_hooks_t race_piece_launch_hooks( race_drive_t& r )
{
    boat_launch_hooks_t h;
    h.remove.user = &r;
    h.remove.remove_body = []( void* u, std::uint32_t body ) {
        race_drive_t& d = *static_cast< race_drive_t* >( u );
        if ( !vu_dynamics_remove_rigid_body( d.session->dynamics->impl, race_drive_boat( d ).debris.pieces[ body - 1 ]->body.body ) )
            race_unwired( *d.session, "a debris piece's body removal (g_vudynamicsimpl vt+0x40) refused" );
    };
    h.remove.remove_step = []( void* u, std::uint8_t* step ) {
        race_drive_t& d = *static_cast< race_drive_t* >( u );
        if ( !vu_dynamics_remove_step_callback( d.session->dynamics->impl, step ) )
            race_unwired( *d.session, "a debris piece's step removal (g_vudynamicsimpl vt+0x30) refused" );
    };
    h.remove.pfx_release = []( void* u, std::uint32_t system ) {
        race_session_t& s = *static_cast< race_drive_t* >( u )->session;
        s.world.pfx_ops->release( s.world.pfx_ops->user, system );
    };
    h.add_body = []( void* u, std::uint32_t body ) {
        race_drive_t& d = *static_cast< race_drive_t* >( u );
        if ( !vu_dynamics_add_rigid_body( d.session->dynamics->impl, race_drive_boat( d ).debris.pieces[ body - 1 ]->body.body ) )
            race_unwired( *d.session, "a debris piece's body add (g_vudynamicsimpl vt+0x3C) refused" );
    };
    h.add_step = []( void* u, std::uint8_t* step ) {
        race_drive_t& d = *static_cast< race_drive_t* >( u );
        if ( !vu_dynamics_add_step_callback( d.session->dynamics->impl, step ) )
            race_unwired( *d.session, "a debris piece's step add (g_vudynamicsimpl vt+0x2C) refused" );
    };
    h.pfx_create = []( void* u, const std::string& name ) -> std::uint32_t {
        race_session_t& s = *static_cast< race_drive_t* >( u )->session;
        return s.world.pfx_ops && !name.empty( ) ? s.world.pfx_ops->create( s.world.pfx_ops->user, name.c_str( ) ) : 0u;
    };
    h.pfx_start = []( void* u, std::uint32_t system ) {
        race_session_t& s = *static_cast< race_drive_t* >( u )->session;
        const boat_pfx_ops_t o = boat_pfx_runtime_ops( *s.world.pfx );
        o.start( o.user, system );
    };
    return h;
}

// The pieces' links to the race's world, made at the first launch (ORDER OURS: world/pieces.hxx). The step = interface slot 1 0x412E30 (boat/damage.hxx
// piece_fluid_step), the motion = setWorldTransform 0x412D90 (piece_motion_set), the models = 0x520080's on +0x48 and on the sub-object +4.
inline void race_boat_debris_bind( race_session_t& s, race_session_boat_t& b )
{
    race_dynamics_t& dyn = *s.dynamics;
    for ( const std::unique_ptr< race_piece_t >& q : b.debris.pieces )
    {
        race_piece_t* p = q.get( );
        if ( !p->stepping )
        {
            rigid_set_gravity( p->body.body.body, dyn.world.gravity );
            dyn.objects[ &p->body.body ] = &p->body.collision;
            dyn.motion[ &p->body.body.body ] = [ p ]( const float* basis, const float* origin ) {
                float bt[ 16 ];
                std::memcpy( bt, basis, 48 );
                std::memcpy( bt + 12, origin, 16 );
                piece_motion_set( p->image.data( ), bt );
            };
            race_step_callback_t cb;
            cb.slot0 = []( float, bool ) {};   // 0x410B30: ret 8
            cb.slot1 = [ &s, &dyn, p ]( float dt ) {
                if ( !s.world.view )
                {
                    race_unwired( s, "a debris piece's step 0x412E30 with no VuWater view - UNKNOWN, not run" );
                    return;
                }
                const fluid_gravity_fn gravity = []( void* u, float* out ) {
                    std::memcpy( out, static_cast< race_dynamics_t* >( u )->world.gravity, 12 );
                    return true;
                };
                if ( !piece_fluid_step( p->solver, p->image.data( ), reinterpret_cast< std::uint8_t* >( &p->body.body ), s.world.view->water, water_view_kernel,
                                        const_cast< water_view_t* >( s.world.view ), gravity, &dyn, dt ) )
                    race_unwired( s, "a debris piece's step 0x412E30 reached a path not recreated - UNKNOWN" );
            };
            dyn.callbacks[ p->image.data( ) + 4 ] = cb;
            p->stepping = true;
        }
        if ( !p->bound && s.world.debris_model )
            if ( const gfx_scene_view_t* v = s.world.debris_model( p->asset ) )
            {
                gfx_scene_view_attach( p->model, *v );
                p->model.m_map = p->skin;
                p->model.m_colour = 0xFFFFFFFFu;   // 0x412270: +0x5C 1.0, +0x60 -1, +0x64 0
                p->model.m_lod = 1.0f;
                p->bound = true;
            }
    }
    if ( !b.debris.bound && s.world.debris_model && !b.io.damage_model.empty( ) )
        if ( const gfx_scene_view_t* v = s.world.debris_model( b.io.damage_model ) )
        {
            const std::vector< gfx_piece_seed_t > seeds = gfx_pieces_seed( *v );
            b.debris.parts.assign( seeds.size( ), boat_debris_part_t{ } );
            b.debris.drawn.assign( seeds.size( ), gfx_scene_piece_t{ } );
            for ( std::size_t i = 0; i < seeds.size( ); ++i )
            {
                std::memcpy( b.debris.parts[ i ].image + 4, seeds[ i ].m_inverse, 64 );
                std::memcpy( b.debris.parts[ i ].image + 0x44, seeds[ i ].m_pivot, 12 );
                b.debris.parts[ i ].model_aabb = seeds[ i ].m_node->m_aabb;
                b.debris.parts[ i ].model_matrix = seeds[ i ].m_node->m_matrix;
                b.debris.drawn[ i ].m_node = seeds[ i ].m_node;
            }
            gfx_scene_view_attach( b.debris.model, *v );
            b.debris.model.m_lod = state_f( b.io.damage, 0x18 );
            b.debris.model.m_byte_1c = b.io.damage[ 0x20 ];
            b.debris.bound = true;
        }
}

inline void race_drive_respawn( void* user )
{
    race_drive_t& r = race_drive_of( user );
    std::memcpy( &r.respawn.checkpoint, r.d.driver + 0x78, 4 );
    boat_respawn( *r.session->track, r.respawn, r.respawn_hooks );
}

// ---- setup -----------------------------------------------------------------

inline void race_drive_setup( race_session_t& s, const value_t* constant_db, std::size_t first )
{
    if ( first == 0 )
        s.drives.assign( s.boats.size( ), nullptr );
    else
        s.drives.resize( s.boats.size( ) );
    if ( constant_db )
        s.vertical_fov = member( member( member( *constant_db, "VuDBAsset" ), "Camera" ), "VerticalFOV" );
    for ( std::size_t k = first; k < s.boats.size( ); ++k )
    {
        race_session_boat_t& b = *s.boats[ k ];
        std::uint8_t* transform = race_session_image( b, 0, 0x50 );
        if ( !b.sections || !transform || !b.driver || b.driver->graph.empty( ) )
            continue;
        auto r = std::make_shared< race_drive_t >( );
        r->session = &s;
        r->index = k;
        std::uint8_t* engine = b.engine.object.image;
        r->records.assign( 8 * b.engine.records, 0 );
        image_put32( engine, 0xF8, 0 );
        image_put32( engine, 0xFC, static_cast< std::uint32_t >( 8 * b.engine.records ) );
        std::uint8_t* body = reinterpret_cast< std::uint8_t* >( &b.body );
        std::uint8_t* driver = b.driver->graph[ 0 ].image.data( );
        const std::uint32_t vtable = load_word( driver, 0 );
        if ( load_word( body, 0xC8 ) != 0 )
        {
            // body +0xC8, the broadphase proxy: none is modelled (no dynamics world).
            LOG_WARNING( "race: %s's body +0xC8 broadphase proxy is set (0x%08X) but not modelled - Decision STOPPED (UNKNOWN)", b.name.c_str( ),
                         load_word( body, 0xC8 ) );            continue;
        }
        boat_decision_t& d = r->d;
        d.boat = b.graph.objects[ 0 ].image.data( );
        d.body = body;
        d.proxy = nullptr;
        d.driver = driver;
        d.slots = boat_driver_of( vtable );
        d.human = vtable == 0x737F44u;
        d.camera = b.io.camera;
        d.hull = b.io.hull;
        d.hull_names = &b.io.hull_strings;
        d.engine = engine;
        d.engine_names = &b.engine.strings;
        d.records = r->records.empty( ) ? nullptr : r->records.data( );
        d.animation = b.io.animation;
        d.debris = b.io.damage;
        d.transform = transform;
        d.solver = b.io.solver;
        d.local_players = s.world.viewports;
        d.floor = nullptr;
        // The driver step.
        r->human = d.human;
        // VuHumanDriver's slot 6 0x40F210 (boat human.hxx human_game_initialize): its +0x80 listener onto the explosion
        // manager g 0x759034 (vt+0x1C 0x510290: push_back) - slot 0 position 0x40F4F0, slot 1 radius 0x40F520, slot 2 hit
        // 0x40F5A0 (the lens-water manager's vt+0x28 inside it: a hook).
        if ( d.human )
            human_game_initialize( driver, [ &s, rp = r.get( ) ]( std::uint8_t* ) {
                explosion_listener_t l;
                l.user = rp;
                l.position = []( void* u, float* out ) { human_explosion_position( static_cast< race_drive_t* >( u )->d.transform, out ); };
                l.radius = []( void* u ) { return human_explosion_radius( static_cast< race_drive_t* >( u )->d.boat ); };
                l.hit = []( void* u, const std::uint8_t* params, float scale ) {
                    race_drive_t& x = *static_cast< race_drive_t* >( u );
                    human_explosion_hit( x.d.driver, x.d.camera, params, scale,
                                         [ &x ]( float n ) { lens_water_add_drops( x.session->lens, n ); } );   // g 0x759048 vt+0x28 = slot 10 0x513600
                };
                s.explosion_listeners.push_back( l );
            } );
        r->ai = vtable == 0x737B5Cu;
        r->ghost = vtable == 0x737DDCu;
        if ( vtable == 0x737C4Cu )
        {
            // The ctor 0x40D880's state: no target (+0xF0 0), no target sector (+0xF4 0), an empty plan (+0x84).
            if ( load_word( driver, 0xF0 ) != 0 || load_word( driver, 0xF4 ) != 0 )
                LOG_WARNING( "race: %s's nuisance driver has a target / sector word set (not modelled) - its tick is UNKNOWN", b.name.c_str( ) );
            else
            {
                r->nuisance = true;
                r->nuisance_pilot.driver = driver;
            }
        }
        for ( std::size_t p = 0; p < s.pilots.size( ); ++p )
            if ( s.pilots[ p ].boat == static_cast< int >( k ) )
                r->pilot = static_cast< int >( p );
        r->human_pilot.driver = driver;
        r->human_pilot.sector = s.start_boats[ k ].sector;
        human_decision_driver_t& hd = r->human_driver;
        hd.track = s.track;
        hd.pilot = &r->human_pilot;
        hd.tick.boat = d.boat;
        hd.tick.transform = transform;
        hd.tick.body = body;
        hd.tick.camera = d.camera;
        hd.tick.solver = d.solver;
        hd.tick.animation = d.animation;
        hd.env.control_method = s.world.control_method;
        hd.hooks.user = &s;
        hd.hooks.axis = []( void* u, std::int32_t pad, const char* name ) -> float { return race_drive_axis( *static_cast< const race_session_t* >( u ), pad, name ); };
        hd.hooks.held = []( void* u, std::int32_t pad, const char* name ) -> std::uint8_t { return race_drive_held( *static_cast< const race_session_t* >( u ), pad, name ); };
        hd.hooks.pressed = []( void* u, std::int32_t pad, const char* name ) -> std::uint8_t { return race_drive_pressed( *static_cast< const race_session_t* >( u ), pad, name ); };
        hd.hooks.misfire = []( void* u ) { race_unwired( *static_cast< race_session_t* >( u ), "audio: the BoostMisfire sound 0x41E060 (FMOD - platform)" ); };
        hd.hooks.rumble = []( void* u, float low, float high ) { race_session_t& rs = *static_cast< race_session_t* >( u ); if ( rs.input ) rs.input->device.rumble( 0, low, high ); };   // pad vt+0x18( 0 ), the controller's vt+0
        // vt+0x1C (boat, line) from the human's decision: these hooks carry the session, not the boat - the human list's
        // only entry is the boat (a second human: UNKNOWN, logged).
        hd.hooks.voice = []( void* u, const char* line ) {
            race_session_t& x = *static_cast< race_session_t* >( u );
            if ( x.manager.lists.humans.boats.size( ) != 1 )
                race_unwired( x, "the voice-over vt+0x1C from a human decision with more than one human (which boat: UNKNOWN)" );
            else
                race_session_vo_say( x, x.manager.lists.humans.boats[ 0 ], line, false );
        };
        hd.hooks.jump_refused = []( void* u ) { race_unwired( *static_cast< race_session_t* >( u ), "audio: the jump-refused sound 0x41E1A0 (FMOD - platform)" ); };
        hd.hooks.jump_sound = []( void* u ) { race_unwired( *static_cast< race_session_t* >( u ), "audio: the jump sound 0x41E100 (FMOD - platform)" ); };
        hd.hooks.menu_change = []( void* u ) { race_unwired( *static_cast< race_session_t* >( u ), "audio: menu_change (FMOD - platform)" ); };
        // The camera's environment (0x409900's outside reads).
        r->camera_env.driver_34 = d.slots.slot[ 1 ];
        r->camera_env.vertical_fov = race_drive_fov_value( s.vertical_fov );
        r->camera_env.user = &s;
        r->camera_env.ray = race_drive_ray;
        // The respawn's views: the boat 0x481CE0 creates has no parent (race_load_boats).
        r->respawn.boat = d.boat;
        r->respawn.transform = transform;
        r->respawn.transform_parent = false;
        r->respawn.body = &b.body;
        body_word( b.body, 0x2C4 ) = 1u;   // 0x41C3EC: the body's contact callback = boat +0x64 (a marker word here: race_contact_added's any_callback)
        r->respawn.proxy = nullptr;
        r->respawn.camera = d.camera;
        r->respawn.instigator = b.io.instigator;
        r->respawn_hooks.user = r.get( );
        r->respawn_hooks.water = race_drive_water;
        // The respawn's 0x534C00(0): the walk over the boat entity's +0x38 children (game/types.hxx transform_walk_children) -
        // a race's VuBoatEntity has none (its ctor 0x41B850 makes no child entity and nothing adds one), so it does nothing.
        r->respawn_hooks.transform_children = []( void* ) {};
        r->respawn_hooks.instigator = []( void* u ) { race_drive_t& d = race_drive_of( u ); trigger_instigator_update( d.session->triggers, race_drive_boat( d ).io.instigator ); };
        r->respawn_hooks.enable = []( void* u ) { race_session_enable( *race_drive_of( u ).session, race_drive_boat( race_drive_of( u ) ), true ); };
        // The driver's slot 20 vt+0x50 after a respawn: VuHumanDriver 0x40F4A0 broadcasts "OnHumanRecovered" (an empty
        // VuParams, 0x505690) - NO SUBSCRIBER exists: the string's one reference is that broadcaster and no asset names it -
        // so it changes nothing; VuAiDriver / VuAiUiDriver / VuAiNuisanceDriver's slot 20 is the nullsub 0x401560.
        r->respawn_hooks.driver_50 = []( void* ) { };
        // "OnBoatRecovered" (0x41D790's broadcast): the params 0x505150 = one entity record {11, the boat's entity id 0x500D50
        // - FNV-1a of its long name, its name (no parent)}, size 8, then the bus by hash. Its only subscriber is VuSlalomGame
        // (ctor 0x48BEC0 - the string's other reference): session.hxx race_session_game_events. No asset names it.
        r->respawn_hooks.recovered = []( void* u, std::uint32_t ) {   // the hash is FNV-1a("OnBoatRecovered")
            race_drive_t& d = race_drive_of( u );
            ui_params_t p;
            const std::uint32_t tag = 11, id = event_hash( race_drive_boat( d ).name.c_str( ) );
            std::memcpy( p.m_bytes, &tag, 4 );
            std::memcpy( p.m_bytes + 4, &id, 4 );
            p.m_size = 8;
            race_session_post( *d.session, "OnBoatRecovered", &p );
        };
        // The Decision tick's hooks.
        boat_decision_hooks_t& h = r->hooks;
        h.user = r.get( );
        h.wake_lookup = race_drive_wake;
        h.jump_sound = []( void* u ) { race_unwired( *race_drive_of( u ).session, "audio: the jump sound 0x41E100 (FMOD - platform)" ); };
        h.driver_tick = race_drive_driver;
        h.animation_tick = []( void* u, float dt ) { race_drive_animation( race_drive_of( u ), dt ); };
        h.camera_tick = race_drive_camera;
        h.respawn = race_drive_respawn;
        h.enable = []( void* u ) { race_session_enable( *race_drive_of( u ).session, race_drive_boat( race_drive_of( u ) ), true ); };
        h.disable = []( void* u ) { race_session_enable( *race_drive_of( u ).session, race_drive_boat( race_drive_of( u ) ), false ); };
        // 0x410FF0 (boat effects.hxx boat_debris_launch) on the pieces world/pieces.hxx loaded.
        h.debris_410FF0 = []( void* u ) {
            race_drive_t& d = race_drive_of( u );
            race_session_t& rs = *d.session;
            if ( !rs.random || !rs.dynamics )
            {
                race_unwired( rs, "the debris launch 0x410FF0 with no VuRandom or no dynamics - UNKNOWN, not run" );
                return;
            }
            race_session_boat_t& rb = race_drive_boat( d );
            race_boat_debris_bind( rs, rb );
            std::uint8_t* db = d.d.debris;
            boat_debris_sub_t sub;
            std::memcpy( sub.range, db + 0x24, sizeof( sub.range ) );
            std::vector< boat_launch_piece_t > launch;
            for ( const std::unique_ptr< race_piece_t >& q : rb.debris.pieces )
                launch.push_back( boat_launch_piece_t{ q->image.data( ), &q->body.body.body, q->pfx } );
            const solver_rigid_t body = boat_decision_body( d.d.body );
            boat_debris_launch( db, sub, rb.debris.parts, launch, reinterpret_cast< const float* >( d.d.transform + 0x50 ), body, *rs.random, race_piece_launch_hooks( d ) );
            LOG_INFO( "race: boat %d destroyed (sync %d): %d pieces, %d parts, at %.1f %.1f %.1f", static_cast< int >( d.index ), rs.dynamics->syncs, static_cast< int >( launch.size( ) ),
                      static_cast< int >( rb.debris.parts.size( ) ), state_f( d.d.transform, 0x80 ), state_f( d.d.transform, 0x84 ), state_f( d.d.transform, 0x88 ) );
        };
        // 0x41E240: an FMOD one-shot at the boat (g 0x759010 EventSystem getEvent on the name at +0x1EC, set3DAttributes at the
        // transform's +0x80, start, release - imports 0x64C348 / 0x64C358 / 0x64C354): the audio platform layer, a hook.
        h.boat_41E240 = []( void* u ) {
            race_drive_t& d = race_drive_of( u );
            const auto it = race_drive_boat( d ).io.boat_strings.find( 0x1EC );
            boat_event_hooks_t eh;
            eh.user = d.session;
            eh.get = []( void* v, const std::string&, std::uint32_t& ) {
                race_unwired( *static_cast< race_session_t* >( v ), "audio: the boat's destroyed event 0x41E240 (FMOD getEvent - platform) - no event made" );
                return false;
            };
            boat_destroyed_event( it == race_drive_boat( d ).io.boat_strings.end( ) ? std::string( ) : it->second,
                                  reinterpret_cast< const float* >( d.d.transform + 0x80 ), eh );
        };
        h.piece_follow = []( void* u ) {
            race_drive_t& d = race_drive_of( u );
            race_session_boat_t& rb = race_drive_boat( d );
            race_piece_t& q = *rb.debris.pieces[ 0 ];
            boat_debris_follow( rb.body.body, q.body.body.body, reinterpret_cast< const float* >( q.image.data( ) + 8 ), []( void* t, const float* m ) {
                // 0x5344A0( piece +8, notify 0 ): the boat has no parent and no children in a race (boat_motion_set's reading).
                transform_component_t c{ };
                transform_set_world( c, m, nullptr );
                std::uint8_t* transform = static_cast< std::uint8_t* >( t );
                std::memcpy( transform + 0x10, c.local, 64 );
                std::memcpy( transform + 0x50, c.world, 64 );
                std::memcpy( transform + 0x90, c.rotation, 12 );
                std::memcpy( transform + 0xA0, c.world_rotation, 12 );
            }, d.d.transform );
        };
        h.piece_remove = []( void* u, int k ) {
            race_drive_t& d = race_drive_of( u );
            boat_piece_remove( race_drive_boat( d ).debris.pieces[ static_cast< std::size_t >( k ) ]->image.data( ), race_piece_launch_hooks( d ).remove );
        };
        h.boost.user = r.get( );
        h.boost.sound = []( void* u, const std::string& ) { race_unwired( *race_drive_of( u ).session, "audio: the boost sounds (FMOD - platform)" ); };
        // g 0x758FEC vt+0x1C (boat, "BoostLow" / "Drafting") is the voice-over manager's say-now (world/voice.hxx).
        h.boost.stats = []( void* u, const char* line ) { race_session_vo_say( *race_drive_of( u ).session, race_drive_of( u ).d.boat, line, false ); };
        h.hull.user = r.get( );
        h.hull.fmod.user = r.get( );
        h.hull.fmod.get_event = []( void* u, int, const std::string&, std::uint32_t* ) {
            race_unwired( *race_drive_of( u ).session, "audio: FMOD getEvent (platform) - no event made" );
            return false;
        };
        // The hull's splash (boat effects.hxx boat_hull_splash): createEffect 0x52BCD0 on the world's VuPfxManager
        // (world/pfxmgr.hxx pfx_manager_create - the water agent's), the runtime's ops.
        h.hull.splash = []( void* u, const std::string& name ) {
            race_drive_t& d = race_drive_of( u );
            race_session_t& rs = *d.session;
            if ( !rs.world.pfx || !rs.world.pfx_manager || !rs.world.pfx_ops )
            {
                race_unwired( rs, "the hull's splash 0x52BCD0 with no VuPfx runtime / manager - UNKNOWN, not run" );
                return;
            }
            const auto create = []( void* v, const char* n ) {
                race_session_t& x = *static_cast< race_session_t* >( v );
                const int e = pfx_manager_create( *x.world.pfx_manager, n, *x.world.pfx_ops );
                if ( e < 0 )
                    race_unwired( x, std::string( "the hull's splash: createEffect 0x52BCD0 made no effect for '" ) + n + "' (no free entry or the project is not loaded)" );
                return e;
            };
            boat_hull_splash( *rs.world.pfx_manager, name.c_str( ), create, &rs, reinterpret_cast< const std::uint32_t* >( d.d.body + 0x140 ),
                              reinterpret_cast< const float* >( d.d.transform + 0x80 ), boat_pfx_runtime_ops( *rs.world.pfx ) );
        };
        // VuLensWaterManagerImpl g 0x759048 (the camera's water droplets): vt+0x28 0x513600 adds to its +0x2490, vt+0x2C
        // 0x513620 stores [0x7358A0] x v at +0x68 - state only its own tick 0x513B90 / draw read (the water / render side's).
        h.hull.lens = []( void* u, float ) { race_unwired( *race_drive_of( u ).session, "the lens-water manager g 0x759048 vt+0x28 / vt+0x2C (VuLensWaterManagerImpl - the water / render side's) - not wired" ); };
        h.hull.wake = race_drive_hull_wake;
        s.drives[ k ] = r;
    }
    if ( first == 0 )
        race_drive_contact_setup( s );
    int made = 0;
    for ( const auto& r : s.drives )
        made += r ? 1 : 0;
    LOG_INFO( "race: the Decision tick 0x41EFF0 composed on %d of %d boat(s)", made, static_cast< int >( s.boats.size( ) ) );
}

// A waypoint's replan (boat track.hxx waypoint_set_branches' `replan`): the
// AI manager's slot 16 0x404220 with 2 and the branch sectors = fleet_replan
// on the session's fleet, pilots, track and the shared VuRandom.
inline void race_drive_replan( race_session_t& s, const int ( &sectors )[ 2 ] )
{
    if ( !s.active || !s.track || !s.random )
        return;
    fleet_replan( s.fleet, s.pilots, *s.track, 2, sectors, *s.random );
}

// Bullet's gContactAddedCallback 0x7590D8 = 0x574E90 -> the contact manager's
// 0x5751F0 (boat contact.hxx contact_added, on the session's surface-type
// table): the engine's contact_callbacks_t::added (dynamics/contact.hxx)
// adapted. The objects are btCollisionObject images (VuRigidBody: +0x2C4 /
// +0x2C8 read past btRigidBody); a world object's collision mesh (shape +0xD4
// +8) comes from `mesh` - the dynamics world that owns the bodies answers it.
// NOT REACHED YET: no dynamics world steps in the race. Unwired pieces stay
// declared: no mesh lookup -> UNKNOWN (the callback does nothing); the
// manager's +4 listener list and the bodies' +0x2C4 callbacks are not wired
// (none registered here). The record's body words are the objects' addresses
// truncated to 32 bits (retail's pointers - a model value).
struct race_contact_t
{
    race_session_t* session = nullptr;
    std::function< const collision_mesh_asset_t*( const solver_rigid_t* object, bool& known ) > mesh;
    int calls = 0, ran = 0, meshes = 0;   // dev counts (not retail): callbacks, 0x5751F0 runs, runs with obj1's mesh
};

inline void race_contact_added( void* user, solver_contact_point_t& cp, solver_rigid_t* object0, int, int, solver_rigid_t* object1, int,
                                int index1 )
{
    race_contact_t& c = *static_cast< race_contact_t* >( user );
    race_session_t& s = *c.session;
    ++c.calls;
    if ( !s.surfaces_built )
    {
        race_unwired( s, "the contact-added callback 0x574E90 with no surface-type table - UNKNOWN" );
        return;
    }
    if ( !c.mesh )
    {
        race_unwired( s, "the contact-added callback 0x574E90: no collision-mesh lookup for the bodies' shapes - UNKNOWN, not run" );
        return;
    }
    bool known0 = false, known1 = false;
    contact_body_t o0, o1;
    o0.image = reinterpret_cast< const std::uint8_t* >( object0 );
    o1.image = reinterpret_cast< const std::uint8_t* >( object1 );
    o0.mesh = c.mesh( object0, known0 );
    o1.mesh = c.mesh( object1, known1 );
    if ( !known0 || !known1 )
    {
        race_unwired( s, "the contact-added callback 0x574E90: a body's shape is not known to the mesh lookup - UNKNOWN, not run" );
        return;
    }
    o0.handle = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( object0 ) );
    o1.handle = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( object1 ) );
    contact_hooks_t h;
    // The manager's +4 listener list: VuCollisionManager's listener 0x4AD540 (world/collide.hxx collision_contact_added),
    // added by its init. The record's bodies are object0 / object1; a boat body's owner (+0x2C0) is resolved by the
    // session's boat list (the image's +0x2C0 is a 32-bit word, not a pointer here) - a flagged body (+0x2CA & 0x10)
    // that is no session boat is UNKNOWN (the listener not run for it).
    if ( s.collision_listening )
        h.listeners.push_back( [ &s, object0, object1 ]( contact_record_t& r ) {
            const auto owner = [ &s ]( const solver_rigid_t* o ) -> const std::uint8_t* {
                for ( const auto& b : s.boats )
                    if ( &b->body.body == o )
                        return b->graph.objects[ 0 ].image.data( );
                return nullptr;
            };
            const std::uint8_t* a = reinterpret_cast< const std::uint8_t* >( object0 );
            const std::uint8_t* b = reinterpret_cast< const std::uint8_t* >( object1 );
            std::uint16_t ga, gb;
            std::memcpy( &ga, a + 0x2CA, 2 );
            std::memcpy( &gb, b + 0x2CA, 2 );
            const std::uint8_t* oa = ( ga & 0x10u ) ? owner( object0 ) : nullptr;
            const std::uint8_t* ob = ( gb & 0x10u ) ? owner( object1 ) : nullptr;
            if ( ( ( ga & 0x10u ) && !oa ) || ( ( gb & 0x10u ) && !ob ) )
            {
                race_unwired( s, "VuCollisionManager's listener 0x4AD540: a boat-flagged body that is no session boat - UNKNOWN, not run" );
                return;
            }
            collision_contact_added( s.collision, a, b, oa, ob, r.pos, r.normal, r.surface0, r.surface1 );
        } );
    // The bodies' +0x2C4 contact callbacks (dynamic game props with one - 0x437580): the buoy's 0x4350B0 (world/rules.hxx
    // checkpoint_buoy_contact) with the record's other body; a plain VuDynamicGamePropEntity's is the base 0x437F40 -
    // not recreated (logged, UNKNOWN), as the buoy's own first call is.
    const auto prop_callback = [ &s ]( solver_rigid_t* self, solver_rigid_t* other ) -> std::function< void( contact_record_t& ) > {
        for ( const auto& dp : s.dyn_props )
            if ( &dp->body->body.body == self )
            {
                race_session_t::dyn_prop_t* d = dp.get( );
                return [ &s, d, other ]( contact_record_t& rec ) {
                    // VuDynamicGamePropEntity's 0x437F40 (world/dynprop.hxx dynamic_prop_contact) on this prop's body and the other.
                    const auto base = [ &s, d, other, &rec ] {
                        bool owner = false;
                        for ( const auto& b : s.boats )
                            owner = owner || &b->body.body == other;
                        if ( dynamic_prop_contact( *d->p, d->hit, d->timer, reinterpret_cast< const std::uint8_t* >( &d->body->body ),
                                                   reinterpret_cast< const std::uint8_t* >( other ), rec.pos, rec.normal, owner ) )
                            ++d->session->prop_hits;
                    };
                    // A mine's +0xB4 part is its own 0x438E10 (world/mine.hxx mine_contact; the base 0x437F40 is not called).
                    // The other body's owner (+0x2C0): a session boat (its driver vt+0x38 0x404BD0: true for the five known
                    // driver vtables) or a level body (its component's entity: not a boat - its +0x2D0 bit 0 decides).
                    // A VuBreakableDynamicGamePropEntity's +0xB4 part is 0x4335A0: VuBreakablePropEntity's 0x434730 arithmetic
                    // whole (world/breakable.hxx breakable_contact - the same instructions, its fields at +0x10C threshold,
                    // the component +0x70, +0x1A0 the kept velocity, +0x168 the weak ref, +0x164 the request); the base
                    // 0x437F40 is not called.
                    if ( d->breakable )
                    {
                        bool owner = false;
                        for ( const auto& x : s.boats )
                            owner = owner || &x->body.body == other;
                        for ( const auto& lbp : s.level_bodies )
                            owner = owner || &lbp->body.body == other;
                        if ( !owner )
                        {
                            race_unwired( s, "a breakable dynamic prop contact with a body that is no session boat / level body - its owner UNKNOWN, not run" );
                            return;
                        }
                        std::uint8_t* own = reinterpret_cast< std::uint8_t* >( &d->body->body );
                        if ( breakable_contact( d->p->threshold_mph, own, reinterpret_cast< const std::uint8_t* >( other ), rec.pos, rec.normal,
                                                d->break_request, d->break_velocity, *d->c, own ) != 0 )
                            d->break_target = other;
                        return;
                    }
                    if ( d->mine )
                    {
                        mine_contact_t mc;
                        for ( const auto& b : s.boats )
                            if ( &b->body.body == other )
                            {
                                mc.owner = mc.owner_is_boat = true;
                                std::uint32_t vt = 0;
                                if ( b->driver && b->driver->known && !b->driver->graph.empty( ) )
                                    std::memcpy( &vt, b->driver->graph[ 0 ].image.data( ), 4 );
                                boat_driver_kind_t kind;
                                if ( !boat_driver_kind( vt, kind ) )
                                {
                                    race_unwired( s, "a mine contact with a boat whose driver vtable is none of the five - UNKNOWN, not run" );
                                    return;
                                }
                                mc.driver_answer = true;
                            }
                        if ( !mc.owner )
                            for ( const auto& lbp : s.level_bodies )
                                if ( &lbp->body.body == other )
                                {
                                    mc.owner = true;
                                    std::memcpy( &mc.other_flags, reinterpret_cast< const std::uint8_t* >( other ) + 0x2D0, 4 );
                                }
                        if ( !mc.owner )
                        {
                            race_unwired( s, "a mine contact with a body that is no session boat / level body - its owner UNKNOWN, not run" );
                            return;
                        }
                        if ( mine_contact( mc, d->request, *d->c, reinterpret_cast< std::uint8_t* >( &d->body->body ) ) )
                            LOG_INFO( "race: mine '%s' touched (sync %d) - the explode request set", d->entity->m_name.c_str( ),
                                      s.dynamics ? s.dynamics->syncs : -1 );
                        return;
                    }
                    if ( !d->buoy )
                    {
                        base( );
                        return;
                    }
                    buoy_contact_t bc;
                    const std::uint8_t* o = reinterpret_cast< const std::uint8_t* >( other );
                    std::memcpy( &bc.group, o + 0x2CA, 2 );
                    for ( const auto& b : s.boats )
                        if ( &b->body.body == other )
                        {
                            bc.owner = true;
                            bc.owner_is_boat = true;
                            if ( b->driver && b->driver->known && !b->driver->graph.empty( ) )
                                std::memcpy( &bc.driver_vtable, b->driver->graph[ 0 ].image.data( ), 4 );
                        }
                    if ( !bc.owner && ( bc.group == 0x10 ) )
                    {
                        race_unwired( s, "a buoy contact with a boat-group body that is no session boat - 0x4350B0's owner UNKNOWN, not run" );
                        return;
                    }
                    bc.index = d->p->checkpoint_index;
                    std::uint8_t params[ 0x84 ] = { };
                    std::int32_t size = 0;
                    const int r = checkpoint_buoy_contact( bc, base, params, size );
                    if ( r < 0 )
                        race_unwired( s, "a buoy contact with a boat whose driver vtable is none of the five - UNKNOWN" );
                    else if ( r == 1 )
                    {
                        ui_params_t p;
                        std::memcpy( p.m_bytes, params, 128 );
                        p.m_size = size;
                        ++s.buoy_hits;
                        LOG_INFO( "race: OnCheckPointBuoyHit (buoy '%s', index %d, sync %d)", d->entity->m_name.c_str( ), bc.index,
                                  s.dynamics ? s.dynamics->syncs : -1 );
                        race_session_post( s, k_buoy_hit_event, &p );
                    }
                };
            }
        // A breakable's +0x7C part: 0x434730 (world/breakable.hxx breakable_contact). The other's owner (+0x2C0) for the
        // weak ref: a session boat or a level body (its component's entity); anything else is UNKNOWN (not run).
        for ( const auto& bp : s.breakables )
            if ( &bp->body->body.body == self )
            {
                race_session_t::breakable_live_t* b = bp.get( );
                return [ &s, b, self, other ]( contact_record_t& rec ) {
                    bool owner = false;
                    for ( const auto& x : s.boats )
                        owner = owner || &x->body.body == other;
                    for ( const auto& lbp : s.level_bodies )
                        owner = owner || &lbp->body.body == other;
                    if ( !owner )
                    {
                        race_unwired( s, "a breakable contact with a body that is no session boat / level body - its owner UNKNOWN, not run" );
                        return;
                    }
                    std::uint8_t* own = reinterpret_cast< std::uint8_t* >( self );
                    const int r = breakable_contact( b->p->threshold_mph, own, reinterpret_cast< const std::uint8_t* >( other ), rec.pos, rec.normal,
                                                     b->request, b->velocity, *b->c, own );
                    if ( r != 0 )
                        b->target = other;
                };
            }
        // VuRampEntity's +0x7C part: 0x43E420 (boat/contact.hxx ramp_contact). The other's owner being a boat is the type walk.
        for ( const auto& lbp : s.level_bodies )
            if ( &lbp->body.body == self && static_cast< const entity_t* >( lbp->owner )->m_type == "VuRampEntity" )
                return [ &s, other ]( contact_record_t& rec ) {
                    for ( const auto& x : s.boats )
                        if ( &x->body.body == other )
                            if ( ramp_contact( rec, other->linear_velocity ) )
                                race_session_image( *x, 0, 0x228 )[ 0x174 ] = 1;
                };
        return nullptr;
    };
    const auto any_callback = [ & ]( solver_rigid_t* self, solver_rigid_t* other ) -> std::function< void( contact_record_t& ) > {
        // A boat body's +0x2C4 is VuBoatEntity's +0x64 part, slot 0 0x41E620 (boat/events.hxx boat_contact_handler): the rumble raise, the
        // AI's push off a flagged body, +0x389, and the hard hit +0x468 that the Decision tick turns into 0x41E010.
        for ( std::size_t k = 0; k < s.boats.size( ) && k < s.drives.size( ); ++k )
            if ( &s.boats[ k ]->body.body == self )
            {
                race_drive_t* dr = s.drives[ k ].get( );
                vu_rigid_body_t* own = &s.boats[ k ]->body;
                return [ dr, own, other ]( contact_record_t& rec ) {
                    boat_contact_handler( dr->d.boat, reinterpret_cast< std::uint8_t* >( own ), reinterpret_cast< const std::uint8_t* >( other ), rec.pos, rec.normal,
                                          dr->d.slots.slot[ 1 ] != 0, dr->d.human, dr->d.driver );
                };
            }
        for ( const auto& u : s.powerups )
            if ( u->body && &u->body->body.body == self )
                return [ &s, self, other ]( contact_record_t& ) { race_powerup_contact( s, self, other ); };
        return prop_callback( self, other );
    };
    h.callback0 = any_callback( object0, object1 );
    h.callback1 = any_callback( object1, object0 );
    ++c.ran;
    c.meshes += o1.mesh ? 1 : 0;
    if ( !contact_added( s.surfaces, reinterpret_cast< std::uint8_t* >( &cp ), o0, o1, index1, h ) )
        race_unwired( s, "the contact-added callback 0x5751F0 read past the surface table / mesh materials - UNKNOWN" );
}

// A level body's shape +8 (createRigidBody 0x544100): the collision mesh asset for the triangle mesh 7
// (0x5445C0), null for the convex hull 6 (0x5444D5) - the only two types built (bodies.hxx).
inline const collision_mesh_asset_t* race_contact_level_mesh( const level_body_t& lb )
{
    return lb.type == 7 ? lb.asset : nullptr;
}

inline void race_contact_counts( const race_session_t& s, int& calls, int& ran, int& meshes )
{
    calls = s.contact_user ? s.contact_user->calls : 0;
    ran = s.contact_user ? s.contact_user->ran : 0;
    meshes = s.contact_user ? s.contact_user->meshes : 0;
}

// The callbacks a race's dynamics world installs (0x574EC0's gContactAddedCallback).
inline contact_callbacks_t race_contact_callbacks( race_contact_t& c )
{
    contact_callbacks_t cb;
    cb.user = &c;
    cb.added = race_contact_added;
    return cb;
}

inline void race_drive_contact_setup( race_session_t& s )
{
    s.contact_user = std::make_shared< race_contact_t >( );
    s.contact_user->session = &s;
    // The object's shape (+0xD4) +8, btCollisionShape's user pointer: a boat's shape is its compound (0x420A70:
    // +0xD4 = the compound; the compound ctor 0x5269C0 writes +8 = 0, nothing in 0x420A70 sets it after) - no
    // mesh; a level body's (createRigidBody 0x544100) is 0 for the convex hull 6 (+8 = 0 before 0x526380) and the
    // collision mesh asset for the triangle mesh 7 (0x5445C0: +8 = the asset property's +0x4C, the asset +0xC0
    // also holds). Any other object: not known (UNKNOWN).
    s.contact_user->mesh = [ &s ]( const solver_rigid_t* object, bool& known ) -> const collision_mesh_asset_t* {
        known = true;
        for ( const auto& b : s.boats )
            if ( &b->body.body == object )
                return nullptr;
        for ( const auto& lb : s.level_bodies )
            if ( &lb->body.body == object )
                return race_contact_level_mesh( *lb );
        known = false;
        return nullptr;
    };
    s.contact = race_contact_callbacks( *s.contact_user );
}

// ---- per frame ---------------------------------------------------------------

inline void race_drive_decision( race_session_t& s, std::size_t boat, float dt )
{
    race_drive_t& r = *s.drives[ boat ];
    bool unknown = false;
    r.d.floor = race_drive_floor( s.world.water, unknown );
    if ( unknown )
        race_unwired( s, "a VuWater surface height is NaN - 0x52A670's order (the floor) is UNKNOWN" );
    r.d.local_players = s.world.viewports;
    r.human_driver.env.control_method = s.world.control_method;
    boat_decision_tick( r.d, dt, r.hooks );
}

// ---- Build step 5: the pfx controller 0x419700 and the debris 0x411A50 (boat effects.hxx) ----------------------
//
// The controller (boat +0x234) keeps seven record lists (+0x38 .. +0x88, std::vector) and a system at +0x94 that
// its ctor fills (NOT READ); every record's system is a VuPfxSystemInstance from the runtime. A race controller
// image holding any record or a +0x94 system is UNKNOWN here (systems the recreated runtime never made) and the
// step is not run; with none, 0x419700's on / off machine, its raises (0x41E2E0 on the driver) and the aabb run.
// The debris (boat +0x238): parts (+4 +0x6C) or pieces (+0xA0) present -> UNKNOWN (their images / models are not
// built here); with none, 0x411A50's aabb reset (and the empty walks).
inline bool race_drive_pfx_empty( race_session_boat_t& b, const std::uint8_t* pc )
{
    const int k = race_session_ref( b.graph, 0, 0x234 );
    if ( k < 0 || !pc )
        return false;
    for ( const image_ref_t& ref : b.graph.objects[ static_cast< std::size_t >( k ) ].refs )
        if ( ( ref.slot >= 0x38 && ref.slot < 0x8C ) || ref.slot == 0x94 )
            return false;
    for ( int l = 0; l < 7; ++l )
        if ( load_word( pc, 0x38 + 0xC * l ) != load_word( pc, 0x3C + 0xC * l ) )
            return false;
    return load_word( pc, 0x94 ) == 0u;
}

// 0x41E2E0 (ops.raise): boat_driver_raise on the boat's driver; the runtime's ops own `user`, so the boat being
// ticked is held here for the call (one boat at a time).
inline thread_local race_drive_t* g_race_drive_raising = nullptr;

// The driver's vt+0x64 (boat pilots.hxx): the human / ghost 0x40EB00 (0x41D570), the AI / UI / nuisance 0x40BDD0.
inline bool race_drive_throttle( const race_drive_t& r, float& t )
{
    if ( r.human )
        t = driver_entity_throttle( r.d.boat, r.d.transform, r.d.engine, r.d.body );
    else if ( r.ai || r.nuisance )
        t = ai_driver_throttle( r.d.driver, r.d.boat, r.d.engine, r.d.boat );
    else
        return false;
    return true;
}

// The ghost recorder's throttle (0x4AFA30: the driver's vt+0x64).
inline float race_ghost_throttle( race_session_t& s, std::size_t k )
{
    float t = 0.0f;
    if ( k >= s.drives.size( ) || !s.drives[ k ] || !race_drive_throttle( *s.drives[ k ], t ) )
        race_unwired( s, "the ghost recorder: the driver's vt+0x64 for this driver class - UNKNOWN, 0" );
    return t;
}

// The Decision tick's animation tick 0x40FFF0 on the boat's rig (world/rig.hxx); the driver's vt+0x68 is slot 26 0x40EB20 in every class.
inline void race_drive_animation( race_drive_t& r, float dt )
{
    race_session_t& s = *r.session;
    boat_rig_inputs_t in;
    in.anim = r.d.animation;
    in.boat = r.d.boat;
    if ( !race_drive_throttle( r, in.throttle ) )
    {
        race_unwired( s, "the animation tick 0x40FFF0: the driver's vt+0x64 for this driver class - UNKNOWN, not run" );
        return;
    }
    in.steer = driver_steer_value( r.d.boat );
    float position[ 4 ], velocity[ 4 ];
    std::memcpy( position, r.d.transform + 0x80, 16 );
    std::memcpy( velocity, r.d.body + 0x140, 16 );
    in.position = position;
    in.velocity = velocity;
    in.engine = r.d.engine;
    in.voice_user = &r;
    in.voice = []( void* u, const char* line ) {   // vt+0x1C( boat, line )
        race_drive_t& d = *static_cast< race_drive_t* >( u );
        race_session_vo_say( *d.session, d.d.boat, line, false );
    };
    std::uint32_t before, after;
    std::memcpy( &before, r.d.animation + 0x78, 4 );
    boat_rig_animation_tick( race_drive_boat( r ).rig, in, dt );
    std::memcpy( &after, r.d.animation + 0x78, 4 );
    // Diagnostic (ours): the boost state's transitions, the measured run behind boat_boost's +0x78 == 2.
    if ( before != after )
        LOG_VERBOSE( "boost state: boat %p frame %d %u -> %u, energy %.3f, boosting %d, Xform control %d", r.d.boat, s.frames, before, after,
                     state_f( r.d.boat, 0x310 ), r.d.boat[ 0x32C ], race_drive_boat( r ).rig.has[ 2 ] ? 1 : 0 );
}

// The pfx frame's boats (pfxsim.hxx pfx_frame_t): what the processes bound to boat `b` read of it, refreshed at its Build
// step 5 - the thrust 0x4CD5B0 (0x41D570's view, engine +0xF8), the boat spring 0x4CD980, the hull 0x4CE760 (below). The
// solver is the controller's OWN VuFluidsObject (+0x34, 0x419FF0 hands it to the hull process): its mesh is the asset
// 0x5243A0 left at +0, its waves excluded (+0x30) must be none - nothing read writes them but the hull's (+0x1CC) wake.
// The hull's part (byte +0x46B and debris +0xA0): race_drive_effects stops at a debris with pieces (UNKNOWN), so no part.
inline void race_drive_pfx_frame( race_session_t& s, race_drive_t& r, race_session_boat_t& b )
{
    pfx_runtime_t& rt = *s.world.pfx;
    if ( s.pfx_boats.size( ) != s.boats.size( ) )
    {
        s.pfx_boats.assign( s.boats.size( ), pfx_frame_boat_t{ } );
        s.pfx_fluids.assign( s.boats.size( ), pfx_frame_fluids_t{ } );
    }
    if ( !b.pfx_solver.mesh && b.pfx_fluids && load_word( b.pfx_fluids, 0 ) )
    {
        const fluids_mesh_asset_t* a = b.meshes.fluids_of( load_word( b.pfx_fluids, 0 ) );
        if ( a )
        {
            fluids_mesh_t& m = b.pfx_mesh;
            std::memcpy( m.aabb_min, a->aabb, 16 );
            std::memcpy( m.aabb_max, a->aabb + 4, 16 );
            m.verts.resize( 4 * static_cast< std::size_t >( a->vertices.size ) );
            std::memcpy( m.verts.data( ), a->vertices.bytes.data( ), 16 * static_cast< std::size_t >( a->vertices.size ) );
            m.edges.resize( 2 * static_cast< std::size_t >( a->edges.size ) );
            std::memcpy( m.edges.data( ), a->edges.bytes.data( ), 8 * static_cast< std::size_t >( a->edges.size ) );
            m.triangles.resize( 15 * static_cast< std::size_t >( a->records.size ) );
            std::memcpy( m.triangles.data( ), a->records.bytes.data( ), 60 * static_cast< std::size_t >( a->records.size ) );
            std::memcpy( &m.total_area, &a->w9c, 4 );
            b.pfx_solver.mesh = &m;
            b.pfx_solver.vertices.assign( static_cast< std::size_t >( a->vertices.size ), water_vertex_t{ } );
            b.pfx_solver.edges.assign( static_cast< std::size_t >( a->edges.size ), fluid_edge_t{ } );
        }
        else
            race_unwired( s, "Build step 5: the controller's fluids asset handle has no mesh in the library - the hull process unrecreated" );
    }
    if ( b.pfx_fluids && load_word( b.pfx_fluids, 0x30 ) != 0u )
        race_unwired( s, "Build step 5: the controller's VuFluidsObject has excluded waves (+0x30) - the hull spray's prepare ignores them (UNKNOWN)" );
    b.pfx_body = boat_decision_body( r.d.body );
    std::uint32_t i308;
    std::memcpy( &i308, r.d.boat + 0x308, 4 );
    b.pfx_view = std::make_unique< boat_view_t >( boat_view_t{ state_f( r.d.boat, 0x2FC ), i308, r.d.boat[ 0x32C ], r.d.boat[ 0x32F ], { }, state_f( r.d.engine, 4 ),
                                                               state_f( r.d.engine, 0xC ), state_f( r.d.boat, 0x470 ), b.pfx_body } );
    for ( int k = 0; k < 3; ++k )
        b.pfx_view->o50_60[ k ] = state_f( r.d.transform, 0x60 + 4 * k );
    pfx_hull_boat_t& h = b.pfx_hull;
    std::memcpy( h.transform, r.d.transform + 0x50, 64 );
    std::memcpy( h.offset, r.d.boat + 0x49C, 16 );
    h.part_flag = r.d.boat[ 0x46B ];
    std::memcpy( &h.part_count, r.d.debris + 0xA0, 4 );
    if ( h.part_flag != 0 && h.part_count > 0 )
    {
        const race_piece_t& q = *b.debris.pieces[ 0 ];
        const std::uint8_t* pb = reinterpret_cast< const std::uint8_t* >( &q.body.body );
        std::memcpy( h.part_matrix, q.image.data( ) + 8, 64 );
        std::memcpy( h.part_body.position, pb + 0x40, 12 );
        std::memcpy( h.part_body.linear, pb + 0x140, 12 );
        std::memcpy( h.part_body.angular, pb + 0x150, 12 );
    }
    std::memcpy( h.body.position, r.d.body + 0x40, 12 );
    std::memcpy( h.body.linear, r.d.body + 0x140, 12 );
    std::memcpy( h.body.angular, r.d.body + 0x150, 12 );
    pfx_frame_boat_t& e = s.pfx_boats[ r.index ];
    e.address = b.pfx.boat;
    e.view = b.pfx_view.get( );
    // The engine's +0xF8 records are the ones 0x414760 refills: the thunk's (boat/thunk.hxx). r.records never leave zero: every thrust process with a drive index stood still.
    std::vector< std::uint8_t >& records = b.thunk && !b.thunk->records.empty( ) ? b.thunk->records : r.records;
    r.d.records = records.empty( ) ? nullptr : records.data( );
    e.records = r.d.records;
    std::memcpy( e.position, r.d.transform + 0x80, 12 );
    std::memcpy( e.velocity, r.d.body + 0x140, 12 );
    e.hull = &h;
    s.pfx_fluids[ r.index ] = pfx_frame_fluids_t{ b.pfx.fluids, &b.pfx_solver };
    rt.frame.boats = s.pfx_boats.data( );
    rt.frame.boat_count = static_cast< int >( s.pfx_boats.size( ) );
    rt.frame.fluids = s.pfx_fluids.data( );
    rt.frame.fluids_count = static_cast< int >( s.pfx_fluids.size( ) );
    rt.frame.user = &s;
    rt.frame.water = s.world.view != nullptr;
    rt.frame.prepare = []( void* u, fluid_solver_t& solver, const float* matrix ) {
        race_session_t& x = *static_cast< race_session_t* >( u );
        if ( !fluid_prepare( solver, x.world.view->water, matrix, water_view_kernel, const_cast< water_view_t* >( x.world.view ) ) )
            race_unwired( x, "the hull spray's VuFluidSolver_prepare 0x524450 reached a path not recreated - its edges UNKNOWN" );
    };
    rt.frame.point = []( void* u, const float* point, float* out ) {
        race_session_t& x = *static_cast< race_session_t* >( u );
        return water_point_query( x.world.view->water, point, water_view_kernel, const_cast< water_view_t* >( x.world.view ), out );
    };
    rt.frame.snap = []( void* u, const float* point, water_vertex_t& out ) {
        race_session_t& x = *static_cast< race_session_t* >( u );
        return water_point_height( x.world.view->water, point, water_view_kernel, const_cast< water_view_t* >( x.world.view ), out );
    };
}

// The boat processes draw from THE global VuRand 0x736810 (the runtime holds a copy; the session's fleet_random_t is it).
struct race_drive_pfx_rand_t
{
    pfx_rand_t& r;
    fleet_random_t* g;
    race_drive_pfx_rand_t( pfx_rand_t& rand, fleet_random_t* global ) : r( rand ), g( global )
    {
        if ( g )
        {
            r.seed = g->seed;
            r.previous = g->last;
            std::memcpy( r.table, g->table, sizeof( r.table ) );
        }
    }
    ~race_drive_pfx_rand_t( )
    {
        if ( g )
        {
            g->seed = r.seed;
            g->last = r.previous;
            std::memcpy( g->table, r.table, sizeof( g->table ) );
        }
    }
};

inline void race_drive_effects( race_session_t& s, race_drive_t& r, float dt )
{
    race_session_boat_t& b = race_drive_boat( r );
    std::uint8_t* pc = race_session_image( b, 0, 0x234 );
    float throttle = 0.0f;
    if ( !race_drive_pfx_empty( b, pc ) )
        race_unwired( s, "Build step 5: the pfx controller 0x419700 holds records / a +0x94 system its unread ctor made - UNKNOWN, not run" );
    else if ( !race_drive_throttle( r, throttle ) )
        race_unwired( s, "Build step 5: 0x419700's driver vt+0x64 for this driver class - UNKNOWN, not run" );
    else
    {
        if ( !r.pfx_read )
        {
            std::memcpy( b.pfx.on, pc + 0x8C, 5 );
            std::memcpy( b.pfx.aabb, pc + 0x98, 32 );
            r.pfx_read = true;
            LOG_INFO( "race: boat %d Build step 5 runs - 0x419700 on an empty controller (on bytes %d%d%d%d%d), 0x411A50", static_cast< int >( r.index ),
                      b.pfx.on[ 0 ], b.pfx.on[ 1 ], b.pfx.on[ 2 ], b.pfx.on[ 3 ], b.pfx.on[ 4 ] );
        }
        if ( !b.pfx_filled )
            race_unwired( s, "Build step 5: postLoad's Pfx fill did not run / stopped - the seven record lists are empty (UNKNOWN); 0x419700's "
                             "on / off machine, raises and aabb reset run" );
        else if ( b.pfx_unknown_bones )
            race_unwired( s, "Build step 5: pfx records whose bone findNodeByName could not answer (no model) tick at bone -1 - UNKNOWN placement" );
        boat_pfx_boat_t pb;
        pb.throttle = throttle;
        pb.d570 = driver_entity_throttle( r.d.boat, r.d.transform, r.d.engine, r.d.body );   // 0x41D570 itself
        pb.velocity = reinterpret_cast< const std::uint32_t* >( r.d.body + 0x140 );
        pb.b32c = r.d.boat[ 0x32C ];
        pb.f400 = load_word( r.d.boat, 0x400 );
        pb.f404 = load_word( r.d.boat, 0x404 );
        pb.f2b4 = state_f( r.d.boat, 0x2B4 );
        pb.colour = r.d.boat + 0x454;
        pb.offset = reinterpret_cast< const float* >( r.d.boat + 0x49C );
        pb.world = reinterpret_cast< const float* >( r.d.transform + 0x50 );
        if ( b.rig.model )   // 0x41A81D: the instance +0x2A4's +0x18
        {
            pb.bones = b.rig.model->instance.matrices.data( );
            pb.bone_count = b.rig.model->asset.skeleton.count;
        }
        boat_pfx_ops_t o = s.world.pfx ? boat_pfx_runtime_ops( *s.world.pfx ) : boat_pfx_ops_t{ };
        o.raise = []( void*, float a, float c ) {
            if ( race_drive_t* d = g_race_drive_raising )
                boat_driver_raise( d->d.driver, d->d.human, a, c );
        };
        bool ran = true;
        if ( s.world.pfx )
        {
            race_drive_pfx_frame( s, r, b );
            race_drive_pfx_rand_t shared( s.world.pfx->rand, s.random );
            const int before = s.world.pfx->frame.unrecreated;
            g_race_drive_raising = &r;
            ran = boat_pfx_tick( b.pfx, pb, dt, o );
            g_race_drive_raising = nullptr;
            if ( s.world.pfx->frame.unrecreated != before )
                race_unwired( s, "Build step 5: a boat pfx process reached a path not recreated (pfx_frame_t::unrecreated)" );
        }
        else
            race_unwired( s, "Build step 5 with no VuPfx runtime - not run" );
        if ( !ran )
            race_unwired( s, "Build step 5: a pfx record's bone outside the model's palette - UNKNOWN" );
        if ( s.world.pfx && b.pfx_filled && r.index == 0 && ( ++r.pfx_ticks % 240 ) == 0 )
        {
            // Ours (a dev census, not retail): what boat 0's systems hold - per list the live particle count (+0xA0).
            std::string by;
            int live = 0;
            int all_records = 0, all_hull = 0, hull_on = 0;
            for ( const auto& q : s.boats )
            {
                for ( const auto& l : q->pfx.list )
                    for ( const boat_pfx_record_t& rec : l )
                        if ( const std::uint32_t* w = s.world.pfx->heap.find( rec.system ) )
                            all_records += static_cast< int >( pfx_system_count( w ) );
                if ( const std::uint32_t* w = q->pfx.extra ? s.world.pfx->heap.find( q->pfx.extra ) : nullptr )
                {
                    all_hull += static_cast< int >( pfx_system_count( w ) );
                    hull_on += pfx_system_state( w ) != 0 ? 1 : 0;
                }
            }
            LOG_INFO( "race: pfx census, all %d boats: %d live in records, %d in hull effects (%d running)", static_cast< int >( s.boats.size( ) ), all_records, all_hull, hull_on );
            for ( int l = 0; l < 7; ++l )
                for ( const boat_pfx_record_t& rec : b.pfx.list[ l ] )
                    if ( const std::uint32_t* w = s.world.pfx->heap.find( rec.system ) )
                    {
                        const int n = static_cast< int >( pfx_system_count( w ) );
                        live += n;
                        by += " L" + std::to_string( l ) + " " + rec.name + " state " + std::to_string( pfx_system_state( w ) ) + " x" + std::to_string( n );
                    }
            const std::uint32_t* x = b.pfx.extra ? s.world.pfx->heap.find( b.pfx.extra ) : nullptr;
            LOG_INFO( "race: boat 0 pfx at Build tick %d: %d live particles in its records (%s ), the hull effect state %d x%d, on %d%d%d%d%d",
                      r.pfx_ticks, live, by.c_str( ), x ? static_cast< int >( pfx_system_state( x ) ) : -1, x ? static_cast< int >( pfx_system_count( x ) ) : -1,
                      b.pfx.on[ 0 ], b.pfx.on[ 1 ], b.pfx.on[ 2 ], b.pfx.on[ 3 ], b.pfx.on[ 4 ] );
        }
        std::memcpy( pc + 0x8C, b.pfx.on, 5 );
        std::memcpy( pc + 0x98, b.pfx.aabb, 32 );
        if ( b.pfx.aabb_w_stale )
            race_unwired( s, "Build step 5: 0x41A790's aabb merge left +0xA4 / +0xB4 = a stale stack word - UNKNOWN" );
    }
    std::uint8_t* db = r.d.debris;
    race_session_boat_t& rb = race_drive_boat( r );
    boat_debris_t d;
    d.gravity = state_f( db, 0x6C );
    d.out = db[ 0x94 ];
    d.timer = state_f( db, 0xC8 );
    std::memcpy( d.aabb, db + 0xA8, 32 );
    d.parts.swap( rb.debris.parts );
    // SUBSTITUTION (flagged): a piece's model aabb is not kept, so 0x5175E0 merges the engine's zero aabb: this aabb drives no draw here.
    for ( const std::unique_ptr< race_piece_t >& q : rb.debris.pieces )
        d.pieces.push_back( boat_debris_piece_t{ reinterpret_cast< const float* >( q->image.data( ) + 8 ), nullptr, reinterpret_cast< const float* >( q->image.data( ) + 0x1B0 ), load_word( q->image.data( ), 0x228 ) } );
    boat_debris_tick( d, dt, s.world.pfx ? boat_pfx_runtime_ops( *s.world.pfx ) : boat_pfx_ops_t{ } );
    d.parts.swap( rb.debris.parts );
    std::memcpy( db + 0xA8, d.aabb, 12 );
    std::memcpy( db + 0xB8, d.aabb + 4, 12 );   // the w lanes +0xB4 / +0xC4: a stack word retail never writes - left
}

// The Build tick 0x41F330 (world/build.hxx): steps 1 - 6 are NOT recreated
// (declared, logged once each); step 7, 0x4200E0 = boat_build_audio, reads
// nothing steps 1 - 6 write (body, engine, solver, camera +0x20, +0x30C,
// +0x3F8..+0x404, +0x204, +0x484 / +0x48C) and runs. Its event slot / FMOD
// calls are audio (platform): the slot answers false (no event made).
inline void race_drive_build( race_session_t& s, std::size_t boat, float dt )
{
    race_drive_t& r = *s.drives[ boat ];
    // Step 3, the driver's vt+0x4C(dt): 0x4021A0 (ret 4) in every driver vtable - VuHumanDriver 0x737F44, VuAiDriver
    // 0x737B5C, VuAiNuisanceDriver 0x737C4C, the UI driver 0x737CDC, VuGhostDriver 0x737DDC (read from the image): nothing.
    race_unwired( s, "the Build tick 0x41F330's g_vu3ddrawmanager vt+0x30 (the renderer's) and step 4's second instance 0x532990 - NOT recreated" );
    // Step 1: 0x52F690 on the blender g 0x759430 (world/blend.hxx) with the position (transform +0x80, copied first)
    // into boat +0x408 (the 22-word record: +0x408 the far plane the race camera reads, +0x454 / +0x458 the pfx
    // colours step 2 lerps).
    {
        float position[ 4 ];
        std::memcpy( position, r.d.transform + 0x80, 16 );
        std::uint32_t out[ 22 ];
        gfx_blend( s.blender, position, out );
        std::memcpy( r.d.boat + 0x408, out, sizeof( out ) );
    }
    // Step 2: the surface under the position (0x52ACB0) and the colour lerp (world/build.hxx boat_build_water). It
    // reads the position copied before step 1 and writes only +0x2B4 .. +0x2C4, which step 1 does not touch.
    {
        float position[ 4 ];
        std::memcpy( position, r.d.transform + 0x80, 16 );
        if ( !boat_build_water( r.d.boat, position, s.world.view ? &s.world.view->water : nullptr ) )
            race_unwired( s, "the Build tick's step 2 (0x52ACB0 / 0x535E10) with no VuWater, a tree overflow or a map cell outside its map - UNKNOWN, nothing written" );
    }
    // Step 4 (world/rig.hxx): the animated skeleton, the instance +0x2A4's pose. Its second instance's copy 0x532990: a draw input.
    if ( !boat_rig_pose( race_drive_boat( r ).rig, dt ) )
        race_unwired( s, "the Build tick's step 4: no model, or a clip with fewer bones than the skeleton - UNKNOWN" );
    // Step 5.
    race_drive_effects( s, r, dt );
    // Step 6 (world/rig.hxx): the draw aabb.
    {
        race_session_boat_t& b6 = race_drive_boat( r );
        if ( b6.rig.model )
        {
            float pfx[ 8 ];
            std::memcpy( pfx, b6.pfx.aabb, 32 );
            boat_rig_draw_aabb( b6.rig.model->instance, reinterpret_cast< const float* >( r.d.transform + 0x50 ), reinterpret_cast< const float* >( r.d.boat + 0x49C ), pfx,
                                reinterpret_cast< const float* >( r.d.debris + 0xA8 ), b6.draw_aabb );
        }
    }
    boat_build_audio_hooks_t h;
    h.user = &s;
    h.slot = []( void* u, std::uint8_t*, const std::string& ) {
        race_unwired( *static_cast< race_session_t* >( u ), "audio: the Build tick's event slot 0x50FBF0 (FMOD - platform) - no event made" );
        return false;
    };
    const auto it = race_drive_boat( r ).io.boat_strings.find( 0x204 );
    const std::string name = it == race_drive_boat( r ).io.boat_strings.end( ) ? std::string( ) : it->second;
    boat_build_audio( r.d.boat, r.d.body, r.d.engine, r.d.solver, r.d.camera, name, dt, h );
}

// 0x4AB0A0 on the session's boat manager: the chosen boat's camera image
// (0x240 bytes of *(boat +0x224)) into the viewport (g 0x759060 +0x28); +0x467
// set on the viewport boat, cleared on the rest.
inline void race_drive_viewports( race_session_t& s )
{
    boat_viewport_hooks_t hooks;
    hooks.boat_camera = [ &s ]( const void* boat ) {
        for ( const auto& b : s.boats )
            if ( b->graph.objects[ 0 ].image.data( ) == boat && b->io.camera )
            {
                std::memcpy( s.viewport_camera, b->io.camera, sizeof( s.viewport_camera ) );
                s.viewport_set = true;
                return;
            }
        race_unwired( s, "the viewport boat has no camera image - UNKNOWN" );
    };
    hooks.viewport_camera = [ &s ]( const std::uint8_t* ) { race_unwired( s, "the boat manager's override camera +0x2C0 (who sets it is not established)" ); };
    hooks.debug_camera = [ &s ] { race_unwired( s, "the boat manager's debug camera (dev layer)" ); };
    hooks.lens_scale = [ &s ]( float v ) {
        if ( v > 0.0f && !( s.viewport_blur > 0.0f ) )
            LOG_INFO( "race: the radial blur's amount rose to %.4f", v );
        s.viewport_blur = v;
    };
    hooks.lens_water = [ &s ]( float x ) { lens_water_set_push( s.lens, x ); };   // g 0x759048 vt+0x2C = slot 11 0x513620
    hooks.local = []( const void* boat, std::uint8_t local ) { const_cast< std::uint8_t* >( static_cast< const std::uint8_t* >( boat ) )[ 0x467 ] = local; };
    hooks.listener = [ &s ] { race_unwired( s, "the FMOD listener + the ActiveCameraTrigger's position (0x534830 / 0x534C00) (not wired)" ); };
    hooks.trigger = [ &s ]( bool ) { race_unwired( s, "the ActiveCameraTrigger's registration (the trigger manager, not wired)" ); };
    boat_viewport_t out;
    if ( !boat_manager_viewport_tick( s.manager.lists, out, hooks ) )
        race_unwired( s, "the boat manager's viewport index +0x58 is negative - UNKNOWN" );
}

// The weak reference's release 0x40E4D0 on the nuisance driver's +0xE4 (boat nuisance.hxx vu_weak_ref_release on
// the session's words) - what the driver tick's own release (0x40E060, no sector after) and DriverDisable reach;
// the target's views (nuisance_target_t) cleared with it.
inline vu_words_t race_session_words( race_session_t& s )
{
    vu_words_t w;
    w.get = [ &s ]( std::uint32_t a ) {
        const auto it = s.words.find( a );
        return it == s.words.end( ) ? 0u : it->second;
    };
    w.put = [ &s ]( std::uint32_t a, std::uint32_t v ) { s.words[ a ] = v; };
    return w;
}

inline void race_drive_release_target( race_drive_t& r )
{
    race_session_t& s = *r.session;
    vu_weak_ref_release( race_session_words( s ), race_entity_token( static_cast< int >( r.index ) ) + 0x1000u + 0xE4u );
    r.nuisance_pilot.target = false;
    r.nuisance_target = nuisance_target_t{ };
}

// The driver's +0xF0 (its weak reference's target) onto the drive's views: the target boat's transform
// component (+0x50) and body (+0x21C).
inline void race_drive_sync_target( race_session_t& s, int child )
{
    if ( child < 0 || static_cast< std::size_t >( child ) >= s.drives.size( ) || !s.drives[ static_cast< std::size_t >( child ) ] )
        return;
    race_drive_t& r = *s.drives[ static_cast< std::size_t >( child ) ];
    const std::uint32_t target = race_session_words( s ).get( race_entity_token( child ) + 0x1000u + 0xF0u );
    const int k = race_entity_of_token( s, target );
    r.nuisance_pilot.target = k >= 0;
    r.nuisance_target = nuisance_target_t{ };
    if ( k < 0 )
        return;
    race_session_boat_t& b = *s.boats[ static_cast< std::size_t >( k ) ];
    r.nuisance_target.transform = race_session_image( b, 0, 0x50 );
    r.nuisance_target.body = reinterpret_cast< const std::uint8_t* >( &b.body );
}

// The event graph's race-side input plugs (slot 2 on them; c_world's dispatch hands over what is not its own):
// - a gfx-settings entity's "Trigger": the zones' slot 13 0x5A7230 (gfx_zone_trigger) / the global's 0x5D2980
//   (gfx_global_trigger: its record into the blender);
// - VuAiNuisanceDriverStartEntity's "DriverTarget" 0x42FF90 / "DriverDisable" 0x430020 (boat nuisance.hxx, on the
//   session's words; `params` the event's VuParams - 0x80 data bytes, +0x80 the size - null: an empty one).
// True when the plug is one of these (consumed); false: not a race-side receiver.
inline void race_session_explode( race_session_t& s, const float* m, const float* explosion9, const std::string& pfx, const std::string& audio,
                                  const char* who );

inline bool race_event_dispatch( race_session_t& s, const std::string& entity, const std::string& plug, const std::uint8_t* params )
{
    if ( race_powerup_plug( s, entity, plug ) )
        return true;
    // VuPropEntity's inputs (ctor 0x5464C0, plug inits 0x546EE0): "Show" 0x546380 - the static model component (+0x70) onto
    // the 3D draw manager unless its +0x1E, the body (+0x74 -> +0x100) into the dynamics world unless added (+0x104);
    // "Hide" 0x5463E0 - the reverse. VuRetVal 0. (The draw manager is the renderer's: prop_drawn.)
    if ( plug == "Show" || plug == "Hide" )
        for ( const auto& lb : s.level_bodies )
        {
            const entity_t* o = static_cast< const entity_t* >( lb->owner );
            if ( !o || ( o->m_type != "VuPropEntity" && o->m_type != "VuRampEntity" && o->m_type != "VuBreakablePropEntity" ) )
                continue;
            std::string path;
            for ( const entity_t* e = o; e && e->m_parent; e = e->m_parent )
                path = path.empty( ) ? e->m_name : e->m_name + "/" + path;
            if ( path != entity )
                continue;
            const bool show = plug == "Show";
            if ( s.world.prop_drawn )
                s.world.prop_drawn( *o, show );
            if ( show && !lb->in_world && lb->built )
                lb->in_world = vu_dynamics_add_rigid_body( s.dynamics->impl, lb->body );
            else if ( !show && lb->in_world )
            {
                vu_dynamics_remove_rigid_body( s.dynamics->impl, lb->body );
                lb->in_world = false;
            }
            return true;
        }
    // VuKeyframeMotionEntity's (VuMotionEntity's) inputs "Activate" 0x61A4F0 / "Deactivate" 0x61A5D0 (world/keymotion.hxx).
    if ( plug == "Activate" || plug == "Deactivate" )
        for ( const auto& kp : s.key_motions )
        {
            std::string path;
            for ( const entity_t* e = kp->entity; e && e->m_parent; e = e->m_parent )
                path = path.empty( ) ? e->m_name : e->m_name + "/" + path;
            if ( path != entity )
                continue;
            if ( plug == "Activate" )
                key_motion_activate( *kp->k, race_session_key_hooks( *kp ) );
            else
                key_motion_deactivate( *kp->k, race_session_key_hooks( *kp ) );
            LOG_INFO( "race: event %s . %s (sync %d)", entity.c_str( ), plug.c_str( ), s.dynamics ? s.dynamics->syncs : -1 );
            return true;
        }
    // VuForceAffectorEntity's inputs "Activate" 0x439C40 / "Deactivate" 0x439C60 (world/force.hxx).
    if ( plug == "Activate" || plug == "Deactivate" )
        for ( const auto& fp : s.forces )
        {
            std::string path;
            for ( const entity_t* e = fp->entity; e && e->m_parent; e = e->m_parent )
                path = path.empty( ) ? e->m_name : e->m_name + "/" + path;
            if ( path != entity )
                continue;
            if ( plug == "Activate" )
                force_affector_activate( *fp->value, fp->world, fp->scale, fp->hooks );
            else
                force_affector_deactivate( *fp->value, fp->hooks );
            LOG_INFO( "race: event %s . %s (force; sync %d)", entity.c_str( ), plug.c_str( ), s.dynamics ? s.dynamics->syncs : -1 );
            return true;
        }
    // VuControlEntity's inputs (ctor 0x5CCBA0): "Activate" 0x5CD7A0 / "Deactivate" 0x5CD890 (VuRetVal void) and the method
    // "OnManualTick" 0x5CD710 on the event's VuParams (world/keyframe.hxx).
    if ( plug == "Activate" || plug == "Deactivate" || plug == "OnManualTick" )
        for ( const auto& cp : s.controls )
        {
            std::string path;
            for ( const entity_t* e = cp->entity; e && e->m_parent; e = e->m_parent )
                path = path.empty( ) ? e->m_name : e->m_name + "/" + path;
            if ( path != entity )
                continue;
            const control_hooks_t h = race_session_control_hooks( *cp );
            if ( plug == "Activate" )
                control_activate( *cp->c, h );
            else if ( plug == "Deactivate" )
                control_deactivate( *cp->c, h );
            else
            {
                static const std::uint8_t empty[ 0x84 ] = { };
                const std::uint8_t* p = params ? params : empty;
                std::int32_t size;
                std::memcpy( &size, p + 0x80, 4 );
                control_manual_tick( *cp->c, p, size, h );
            }
            LOG_INFO( "race: event %s . %s (control; sync %d)", entity.c_str( ), plug.c_str( ), s.dynamics ? s.dynamics->syncs : -1 );
            return true;
        }
    // VuExplosionEntity's "Trigger" (0x5E6410's plug, 0x5E63D0): the part (+0x64) 0x547BB0( transform +0x50 ) (session.hxx
    // race_session_explode), VuRetVal 0.
    if ( plug == "Trigger" && s.tree )
    {
        entity_t* e = s.tree;
        std::size_t at = 0;
        while ( e && at <= entity.size( ) )
        {
            const std::size_t slash = entity.find( '/', at );
            const std::string part = entity.substr( at, slash == std::string::npos ? std::string::npos : slash - at );
            e = entity_find_child( *e, part );
            if ( slash == std::string::npos )
                break;
            at = slash + 1;
        }
        if ( const explosion_entity_state_t* x = e ? explosion_entity_state_of( *e ) : nullptr )
        {
            const transform_component_t* t = entity_transform( *e );
            if ( !t || e->m_world_unknown )
                race_unwired( s, "VuExplosionEntity . Trigger 0x5E63D0 on an entity whose world is unknown - UNKNOWN, not run" );
            else
            {
                race_session_explode( s, t->world, x->value.explosion, x->value.pfx, x->value.audio, "VuExplosionEntity" );
                LOG_INFO( "race: event %s . Trigger (VuExplosionEntity 0x5E63D0: 0x547BB0; sync %d)", entity.c_str( ), s.dynamics ? s.dynamics->syncs : -1 );
            }
            return true;
        }
    }
    // VuFloatingMineEntity's "Detonate" (0x439020's plug; thunk 0x4390BE -> vt+0x44 0x438DF0): +0x215 = 1, VuRetVal 0.
    if ( plug == "Detonate" )
        for ( const auto& dp : s.dyn_props )
        {
            if ( !dp->mine )
                continue;
            std::string path;
            for ( const entity_t* e = dp->entity; e && e->m_parent; e = e->m_parent )
                path = path.empty( ) ? e->m_name : e->m_name + "/" + path;
            if ( path != entity )
                continue;
            dp->request = 1;
            return true;
        }
    // VuDynamicGamePropEntity's "Activate" 0x438160 and VuBreakablePropEntity's "Break" 0x4349F0.
    if ( plug == "Activate" )
        for ( const auto& dp : s.dyn_props )
        {
            std::string path;
            for ( const entity_t* e = dp->entity; e && e->m_parent; e = e->m_parent )
                path = path.empty( ) ? e->m_name : e->m_name + "/" + path;
            if ( path != entity )
                continue;
            dynamic_prop_activate( dp->body ? reinterpret_cast< std::uint8_t* >( &dp->body->body ) : nullptr );
            return true;
        }
    if ( plug == "Break" )
        for ( const auto& br : s.breakables )
        {
            std::string path;
            for ( const entity_t* e = br->entity; e && e->m_parent; e = e->m_parent )
                path = path.empty( ) ? e->m_name : e->m_name + "/" + path;
            if ( path != entity )
                continue;
            breakable_on_break( br->request, br->target );
            return true;
        }
    // VuBreakableDynamicGamePropEntity's "Break" (the ctor's plug 0x433BA0, method 0x433860): the weak ref +0x168 released
    // (vt+0), +0x164 = 1, VuRetVal 0.
    if ( plug == "Break" )
        for ( const auto& dp : s.dyn_props )
        {
            if ( !dp->breakable )
                continue;
            std::string path;
            for ( const entity_t* e = dp->entity; e && e->m_parent; e = e->m_parent )
                path = path.empty( ) ? e->m_name : e->m_name + "/" + path;
            if ( path != entity )
                continue;
            dp->break_target = nullptr;
            dp->break_request = 1;
            return true;
        }
    const auto g = s.gfx_by_path.find( entity );
    if ( g != s.gfx_by_path.end( ) && plug == "Trigger" )
    {
        if ( g->second.global )
            gfx_global_trigger( s.blender, g->second.zone->record );
        else
            gfx_zone_trigger( *g->second.zone );
        LOG_INFO( "race: event %s . Trigger (%s)", entity.c_str( ), g->second.global ? "0x5D2980: its record into the blender" : "0x5A7230" );
        return true;
    }
    const auto n = s.nuisance_by_path.find( entity );
    if ( n != s.nuisance_by_path.end( ) && n->second.state && ( plug == "DriverTarget" || plug == "DriverDisable" ) )
    {
        static const std::uint8_t empty[ 0x84 ] = { };
        const std::uint8_t* p = params ? params : empty;
        const std::uint32_t child = n->second.child >= 0 ? race_entity_token( n->second.child ) : 0u;
        const auto find = [ &s ]( std::uint32_t h ) { return race_entity_of_token( s, h ) >= 0 ? h : 0u; };
        const vu_words_t w = race_session_words( s );
        if ( plug == "DriverTarget" )
            nuisance_start_driver_target( *n->second.state, child, p, find, w );
        else
            nuisance_start_driver_disable( *n->second.state, child, p, find, w );
        race_drive_sync_target( s, n->second.child );
        const int who = race_entity_of_token( s, vu_params_entity( p ) );
        LOG_INFO( "race: event %s . %s from %s - target now %s%s", entity.c_str( ), plug.c_str( ),
                  who >= 0 ? s.boats[ static_cast< std::size_t >( who ) ]->name.c_str( ) : "(no entity)",
                  n->second.child >= 0 && s.drives[ static_cast< std::size_t >( n->second.child ) ] &&
                          s.drives[ static_cast< std::size_t >( n->second.child ) ]->nuisance_pilot.target
                      ? "set" : "none", n->second.state->disabled ? ", disabled" : "" );
        return true;
    }
    return false;
}
// ---- VuGame's tick 0x482F10 in a race (world/rules.hxx) ----------------------
//
// The Decision phase's game mode manager tick 0x4CBFD0 -> the action mode's
// slot 4 0x4B99E0 (its own state machine: LoadingScreen / LoadLevel / Begin -
// the front end's loading flow, NOT recreated: the session starts in its Run
// state) -> Run's tick 0x4BAB50 -> 0x482F10(dt) on the game: 0x483230,
// 0x483540, 0x483AD0, slot 7 0x489A40, VuGame's +0x0C / +0x67C machines (not
// recreated - logged), each game boat's +0x4DC = its world. A game boat's
// driver vt+0x5C: the human's sector (its pilot), an AI's plan's first sector
// (+0x90 > 0 ? [+0x8C][0] : null).
inline void race_session_game_mode( void* user, float dt )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    if ( !s.rules_running || !s.track )
        return;
    ++s.rules_frames;
    {
        // Ours (a test driver): `--cheat-finish N` broadcasts VuGame's "OnCheatFinish" (retail's developer cheat, 0x483190 ->
        // 0x4848C0: rules.hxx race_rules_cheat_finish) on game tick N, before this tick's bookkeeping - a forced finish that
        // takes the race to PostGame and the results writer without driving the whole race.
        static const char* cheat = c_engine::option( "cheat-finish" );
        if ( cheat && cheat[ 0 ] && s.rules_frames == std::atoi( cheat ) )
        {
            LOG_INFO( "race: --cheat-finish %d: OnCheatFinish broadcast (an empty VuParams)", s.rules_frames );
            race_session_post( s, "OnCheatFinish" );   // VuGame's subscriber 0x483190 (session.hxx race_session_game_events)
        }
    }
    const track_manager_t& tm = *s.track;
    for ( std::size_t k = 0; k < s.rules_boats.size( ); ++k )
    {
        race_rules_boat_t& rb = s.rules_boats[ k ];
        rb.sector = nullptr;
        const std::size_t at = rb.drive >= 0 ? static_cast< std::size_t >( rb.drive ) : k;
        const race_drive_t* d = at < s.drives.size( ) ? s.drives[ at ].get( ) : nullptr;
        int sector = -1;
        if ( d && d->human )
            sector = d->human_pilot.sector;
        else if ( d && d->ghost )
            sector = d->ghost_pilot.sector;
        else if ( d && d->ai && d->pilot >= 0 )
        {
            const fleet_plan_t& plan = s.pilots[ static_cast< std::size_t >( d->pilot ) ].plan;
            sector = plan.sectors.empty( ) ? -1 : plan.sectors[ 0 ];
        }
        else
            race_unwired( s, "a game boat's driver vt+0x5C with no human / AI drive composed - no sector (UNKNOWN)" );
        if ( sector >= 0 && static_cast< std::size_t >( sector ) < tm.sectors.size( ) )
            rb.sector = tm.sectors[ static_cast< std::size_t >( sector ) ].image;
    }
    race_rules_distances( s.rules_game, s.rules_boats );
    race_checkpoint_hooks_t h;
    h.view = [ & ]( int w, race_checkpoint_view_t& v )
    {
        if ( w < 0 || static_cast< std::size_t >( w ) >= tm.waypoints.size( ) )
            return false;
        const track_waypoint_t& wp = tm.waypoints[ static_cast< std::size_t >( w ) ];
        const transform_component_t* t = wp.entity ? entity_transform( *wp.entity ) : nullptr;
        if ( !t || wp.entity->m_world_unknown )
            return false;
        std::memcpy( v.normal, t->world + 4, 12 );
        std::memcpy( v.origin, t->world + 12, 12 );
        v.finish = wp.p.finish;
        v.width = wp.p.width;
        v.height = wp.p.height;
        return true;
    };
    h.index = [ & ]( int w ) { return track_checkpoint_index( tm, w ); };
    h.next = [ & ]( int w ) { return race_rules_next_checkpoint( tm, w ); };
    h.crossed = [ & ]( int boat, int w )
    {
        LOG_INFO( "race: %s crossed checkpoint %d (waypoint %d) - lap %d, sync %d", s.boats[ static_cast< std::size_t >( boat ) ]->name.c_str( ),
                  track_checkpoint_index( tm, w ), w, rules_i32( s.rules_boats[ static_cast< std::size_t >( boat ) ].boat, 0x4AC ),
                  s.dynamics ? s.dynamics->syncs : -1 );
        // VuRaceGame slot 15 0x48B180 (core/ui split.hxx ui_race_checkpoint): the boat's driver slot 12 (human), lap +0x4AC,
        // race time +0x4B4; the checkpoint's +0x8A finish byte; the game's +0x4C laps; the +0x67C machine (rules.hxx).
        // VuTimeTrialGame's slot 15 0x490D70 is the same code (its split vector at +0x720; case timetrial_split), and so is
        // VuSlalomGame's 0x48E500 (its split vector at +0x750: byte-compared with 0x490D70 - 16 bytes differ, all offsets /
        // string addresses; not oracle-run). VuGhostGame's 0x487640 too (its split vector at +0x728; case ghost_split).
        const race_rules_boat_t& rb = s.rules_boats[ static_cast< std::size_t >( boat ) ];
        ui_race_split_boat_t sb;
        sb.m_human = rb.human;
        sb.m_lap = rules_i32( rb.boat, 0x4AC );
        sb.m_race_time = rules_f( rb.boat, 0x4B4 );
        s.rules.split_display.m_laps = s.rules_game.laps;
        const bool finish = w >= 0 && static_cast< std::size_t >( w ) < tm.waypoints.size( ) && tm.waypoints[ static_cast< std::size_t >( w ) ].p.finish;
        if ( !ui_race_checkpoint( s.rules.split_display, s.rules.split, sb, finish ) )
            race_unwired( s, "VuRaceGame slot 15 0x48B180: the +0x67C machine has no ShowSplit / ShowResult condition - UNKNOWN" );
        else if ( sb.m_human && sb.m_lap >= 1 )
            LOG_INFO( "race: %s split display - split delta %.3f, result delta %.3f, %d splits", s.boats[ static_cast< std::size_t >( boat ) ]->name.c_str( ),
                      s.rules.split_display.m_split_delta, s.rules.split_display.m_result_delta, static_cast< int >( s.rules.split_display.m_splits.size( ) ) );
    };
    h.finished = [ & ]( int boat )
    {
        fleet_remove_driver( s.fleet, boat );   // 0x483B60's g_vuaimanagerimpl slot 12 0x4036C0
        LOG_INFO( "race: %s FINISHED - race time %.3f, place %d", s.boats[ static_cast< std::size_t >( boat ) ]->name.c_str( ),
                  rules_f( s.rules_boats[ static_cast< std::size_t >( boat ) ].boat, 0x4B4 ),
                  rules_i32( s.rules_boats[ static_cast< std::size_t >( boat ) ].boat, 0x4CC ) );
    };
    race_rules_checkpoints( s.rules_game, s.rules_boats, h );
    if ( h.unknown )
        race_unwired( s, "0x483540 on a checkpoint waypoint whose world is unknown - that boat skipped (UNKNOWN)" );
    std::vector< std::uint8_t* > ais;
    for ( const void* b : s.manager.lists.ais.boats )
        ais.push_back( const_cast< std::uint8_t* >( static_cast< const std::uint8_t* >( b ) ) );
    race_rules_hide_finished( ais, [ & ]( int ai )
    {
        for ( std::size_t k = 0; k < s.boats.size( ); ++k )
            if ( s.boats[ k ]->graph.objects[ 0 ].image.data( ) == ais[ static_cast< std::size_t >( ai ) ] )
            {
                race_session_enable( s, *s.boats[ k ], false );   // slot 18 0x421940
                LOG_INFO( "race: %s hidden (0x483AD0: the sixth finished AI boat)", s.boats[ k ]->name.c_str( ) );
            }
        race_unwired( s, "0x483AD0's draw removal (g_vu3ddrawmanager 0x7590B8 vt+0x2C - the renderer's)" );
    } );
    s.rules.humans.clear( );
    for ( const void* b : s.manager.lists.humans.boats )
        s.rules.humans.push_back( const_cast< std::uint8_t* >( static_cast< const std::uint8_t* >( b ) ) );
    const std::string before = s.rules.fsm.current( ) ? s.rules.fsm.current( )->m_name : std::string( );
    const bool running = race_rules_tick( s.rules, dt );
    const std::string after = s.rules.fsm.current( ) ? s.rules.fsm.current( )->m_name : std::string( );
    if ( before != after )
        LOG_INFO( "race: VuRaceGame state %s -> %s (sync %d)", before.c_str( ), after.c_str( ), s.dynamics ? s.dynamics->syncs : -1 );
    if ( !s.rules.sorted )
        race_unwired( s, "0x48AB20 over more than 32 boats (MSVC's partition / heap sort paths, not recreated) - the places not sorted (UNKNOWN)" );
    // VuGame's two machines after slot 7 (world/rules.hxx vugame_fsms_tick: verified tables, handlers hooks).
    const std::string event_before = s.rules.event.current( ) ? s.rules.event.current( )->m_name : std::string( );
    const bool game_running = vugame_fsms_tick( s.rules, running, dt );
    s.game_answer = game_running;
    const std::string event_after = s.rules.event.current( ) ? s.rules.event.current( )->m_name : std::string( );
    if ( event_before != event_after )
        LOG_INFO( "race: VuGame +0x0C state %s -> %s (sync %d)%s", event_before.c_str( ), event_after.c_str( ), s.dynamics ? s.dynamics->syncs : -1,
                  game_running ? "" : " - the game's tick answers done" );
    for ( race_rules_boat_t& rb : s.rules_boats )
        std::memcpy( rb.boat + 0x4DC, rb.transform + 0x50, 64 );
    // Ours (dev trace): every boat's progress every 240 game ticks.
    if ( s.rules_frames % 240 == 0 )
        for ( std::size_t k = 0; k < s.rules_boats.size( ); ++k )
        {
            const race_rules_boat_t& rb = s.rules_boats[ k ];
            int sector = -1;
            for ( std::size_t q = 0; q < tm.sectors.size( ); ++q )
                if ( tm.sectors[ q ].image == rb.sector )
                    sector = static_cast< int >( q );
            LOG_INFO( "race progress tick %d: %s sector %d lap %d place %d next checkpoint %d race done %.1f / left %.1f, time %.2f, input-off %d, "
                      "at (%.1f %.1f %.1f)", s.rules_frames, s.boats[ k ]->name.c_str( ), sector, rules_i32( rb.boat, 0x4AC ),
                      rules_i32( rb.boat, 0x4CC ), rb.checkpoint >= 0 ? track_checkpoint_index( tm, rb.checkpoint ) : -1, rules_f( rb.boat, 0x4C8 ),
                      rules_f( rb.boat, 0x4C4 ), rules_f( rb.boat, 0x4B4 ), rules_i32( rb.boat, 0x308 ), rules_f( rb.transform, 0x80 ),
                      rules_f( rb.transform, 0x84 ), rules_f( rb.transform, 0x88 ) );
        }
}

// The ghost's replay, its two hooks on the Decision composition.
inline void race_ghost_debris( race_session_t& s, std::size_t k )
{
    if ( k < s.drives.size( ) && s.drives[ k ] )
        s.drives[ k ]->hooks.debris_410FF0( s.drives[ k ].get( ) );
}

inline void race_ghost_destroyed( race_session_t& s, std::size_t k )
{
    if ( k < s.drives.size( ) && s.drives[ k ] )
        s.drives[ k ]->hooks.boat_41E240( s.drives[ k ].get( ) );
}
