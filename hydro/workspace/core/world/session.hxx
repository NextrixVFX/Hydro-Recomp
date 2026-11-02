#pragma once

#include <workspace/core/save/cloud.hxx>
#include <workspace/core/save/profile.hxx>
#include <workspace/core/ui/progress.hxx>
#include <workspace/core/save/ghost.hxx>
#include <workspace/core/world/ghost.hxx>
#include <workspace/core/world/ghostgame.hxx>
#include <workspace/core/game/packages.hxx>
#include <workspace/core/game/settings.hxx>
#include <workspace/core/ui/award.hxx>
#include <workspace/core/ui/paused.hxx>
#include <workspace/core/world/tiretrack.hxx>
#include <workspace/core/world/pause.hxx>
#include <workspace/core/boat/construct.hxx>
#include <workspace/core/boat/fleet.hxx>
#include <workspace/core/boat/ghost.hxx>
#include <workspace/core/boat/load.hxx>
#include <workspace/core/boat/meshes.hxx>
#include <workspace/core/boat/nuisance.hxx>
#include <workspace/core/boat/rays.hxx>
#include <workspace/core/boat/respawn.hxx>
#include <workspace/core/boat/thunk.hxx>
#include <workspace/core/dynamics/contact.hxx>
#include <workspace/core/trigger/volume.hxx>
#include <workspace/core/boat/powerup.hxx>
#include <workspace/core/ui/common.hxx>
#include <workspace/core/ui/config.hxx>
#include <workspace/core/world/blend.hxx>
#include <workspace/core/world/bodies.hxx>
#include <workspace/core/world/bus.hxx>
#include <workspace/core/world/collide.hxx>
#include <workspace/core/world/dynamics.hxx>
#include <workspace/core/world/kernel.hxx>
#include <workspace/core/world/lens.hxx>
#include <workspace/core/world/mine.hxx>
#include <workspace/core/world/modes.hxx>
#include <workspace/core/world/rig.hxx>
#include <workspace/core/world/voice.hxx>
#include <workspace/core/world/pfxmgr.hxx>
#include <workspace/core/world/pfxsim.hxx>
#include <workspace/core/world/dynprop.hxx>
#include <workspace/core/boat/effects.hxx>
#include <workspace/core/world/debris.hxx>
#include <workspace/core/world/race.hxx>
#include <workspace/core/world/rules.hxx>
#include <workspace/core/world/start.hxx>
#include <workspace/core/world/tick.hxx>
#include <workspace/core/world/watchers.hxx>
#include <workspace/core/world/affector.hxx>
#include <workspace/utility/logger.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The race switched on in the running game (world batch 11): the chain the
// world's start units were verified for, composed in retail's order -
//
//   the front end's GameData (ui_front_end_game_data, core/ui config.hxx) from
//   --mode / --race / --course / --boat / --skin (no defaults: without --mode,
//   --boat and --skin there is no race and nothing here runs - the gates' bench
//   passes none of them);
//   VuGame's boat load 0x481CE0 (race_load_boats) with the boat's postLoad
//   0x41CFF0 (race_boat_post_load) on each boat's ctor graph (boat_construct
//   0x41B850) - the driver built and loaded, then 0x420A70 (boat_load_sections)
//   and 0x421670 (boat_transform_changed) on the graph's objects;
//   VuGame's begin 0x4829A0 (race_begin: VuRaceGame slots 13 / 5) on the
//   world's track with the tree's VuGridPositionEntity worlds (0x43A740), each
//   boat's gameInitialize 0x41D350 registering its Decision / Build delegates
//   in the tick manager and adding it to the boat manager (0x4AAF60);
//   per frame, VuTickManagerImpl's tick 0x4F72F0 (tick_run) over its phases.
//
// STOPPED / UNKNOWN (logged, never stood in for):
// - VuRandom 0x736810 is the one global c_world seeds at its engine init as
//   retail's 0x566230 does (fleet_random_seed on the platform clock; a --bench
//   run's clock answers a fixed 0): "Race" builds its AI roster from it.
// - 0x420A70 loads the collision / fluids meshes (the boat agent's meshes.hxx)
//   and, with the surface-type table (surface.hxx, from SurfaceDB), runs to its
//   end, the skin pass 0x421490 (world/livery.hxx) included. Each boat's
//   Decision tick 0x41EFF0 runs (world/drive.hxx, its hooks recreated or each
//   declared there), and with it the race camera 0x409900; the Viewports tick
//   0x4AB0A0 copies the viewport boat's camera into the view. Still STOPPED
//   (declared, logged once): the Build tick 0x41F330's steps 1 - 6 (its 0x4200E0 runs).
// - The signed-in player's name (the human entry's Driver Name) is a platform
//   service: empty here.

struct race_session_boat_t;

// The load hooks' user (race_session_sections): the boat, its archive, the rendering assets' next handle.
struct race_session_load_ctx_t
{
    race_session_boat_t* b = nullptr;
    const c_archive* archive = nullptr;
    std::uint32_t next = 1;
};

struct race_session_boat_t
{
    const ghost_data_t* ghost = nullptr;   // VuGhostBoatEntity +0x520
    boat_graph_t graph;
    std::unique_ptr< race_boat_driver_t > driver;
    vu_rigid_body_t body{};
    collision_object_t collision{};   // the body's collision object in the race's dynamics world (world/dynamics.hxx)
    boat_engine_t engine{};
    boat_load_t io{};
    // The pfx controller (boat +0x234, boat effects.hxx boat_pfx_t): filled by postLoad's Pfx step (the boat agent's
    // boat_pfx_fill through io.pfx_fill - race_session_sections), ticked by Build step 5 (world/drive.hxx).
    boat_pfx_t pfx;
    bool pfx_filled = false;
    int pfx_unknown_bones = 0;   // findNodeByName calls answered UNKNOWN (the boat has no model: retail would fault)
    boat_rig_t rig;              // the model instance +0x2A4, the animation +0x230's skeleton and controls (world/rig.hxx)
    std::uint8_t* pfx_fluids = nullptr;   // the controller's +0x34 VuFluidsObject image
    // What the VuPfx processes bound to this boat read (pfx_frame_t's entries; world/drive.hxx race_drive_pfx_frame): the
    // controller's fluids object as the solver 0x4CE760 prepares (its mesh from the load's handle) and the boat's views.
    fluids_mesh_t pfx_mesh{ };
    fluid_solver_t pfx_solver{ };
    solver_rigid_t pfx_body{ };
    std::unique_ptr< boat_view_t > pfx_view;
    pfx_hull_boat_t pfx_hull{ };
    bool sections = false;    // 0x420A70 ran to its end
    std::string stop;         // where it stopped (UNKNOWN), when it did
    std::string name, boat, skin;
    boat_mesh_library_t meshes;   // the collision / fluids meshes the sections load created
    // The user of io.hooks: the BOAT's, not one static. The hooks outlive the sections load (0x419FF0's 0x5243A0 runs them at
    // gameInitialize); on a shared context every boat's fluids asset landed in the last loaded boat's library, so its handle
    // answered nothing in its own: a null solver mesh in the hull spray 0x4CE760 (the 11-01 crash, +0x50 = mesh->triangles).
    race_session_load_ctx_t load_ctx{ };
    std::unique_ptr< boat_thunk_t > thunk;   // the step callback's context (boat/thunk.hxx), persistent
    std::vector< std::uint32_t > excluded;   // the solver's +0x34 array words this step
    std::uint8_t draw_added = 0, instigator_registered = 0;
    float draw_aabb[ 8 ]{ };   // the draw component +0x6C's +0x24 .. +0x43 (Build step 6)
    // The hull draw (c_world): each model instance's placements, the one shown.
    std::vector< int > placements[ 3 ];
    int shown = -2;
    bool hull_refused = false;
    int hull_geometry = -1;      // instance 0's streamed clone: the skinned draw 0x532AD0's pose reaches it by CPU skinning
    // Ours: the session this boat is in (the transform watcher's 0x536350 reaches the trigger manager through it).
    struct race_session_t* owner = nullptr;
    int index = -1;
};

// What the world lends the race's ticks, refreshed by c_world every frame
// (world/drive.hxx reads it):
// - water / view: VuWater g 0x759058 (the built water and the query's view of
//   it; null: no VuWater);
// - viewports: g_vuviewportmanager 0x759060 +4 - VuActionGameMode's enter
//   0x4B9630 calls 0x530BB0(1) before its load, and 0x530BB0 stores min(n, 4)
//   (cmovl) at +4: 1 for the race;
// - control_method: g_vucontrolmethodmanager 0x758FC0 +0x18 - c_input_device's
//   answer, written each "Input" phase (race_input_tick; retail starts at 1);
// - display_w / _h: g_vumetrod3d11gfx vt+0x40, the display size (PLATFORM).
// - water_tick: VuWater's "Water" tick 0x529E30 (c_world's water_built_tick +
//   the view rebuilt), run by the race tick manager's "Water" phase.
struct race_world_t
{
    water_built_t* water = nullptr;   // written by the hulls' wake update 0x417D80 (world/drive.hxx)
    const water_view_t* view = nullptr;
    std::function< void( float ) > water_tick;
    // The "Triggers" tick 0x536460's trigger half: every listed trigger's vt+0x30 over the manager's
    // instigators (c_world's trigger list - world.hxx race_triggers), run after the instigator step.
    std::function< void( ) > triggers;
    // The game mode manager's Decision tick 0x4CBFD0 -> the action mode's slot 4 0x4B99E0 (world/action.hxx, c_world's);
    // null: no action mode - the session's game tick runs as if in Run (the scaffold paths).
    std::function< void( float ) > game_mode;
    // The race HUD projects (VuRaceGame +0x738 / +0x73C / +0x740: 0 PreGame, 1 Game, 2 PostGame) - their load, gameInitialize,
    // release and graph are the world's (c_world), their tick / draw the UI's; null: no HUD projects (UNKNOWN, logged).
    std::function< void( int project, race_hud_verb_t verb, float dt ) > hud;
    // A VuEventManager broadcast reaching the world's graph (the HUD's / the level's subscribers).
    std::function< void( const char* name, const ui_params_t* params ) > broadcast;   // params null: an empty VuParams
    // VuStringDBImpl's getString (g 0x759078 slot 6 0x53B830, world/strings.hxx): c_world's string DB. Null: UNKNOWN.
    std::function< std::string( const char* id ) > string_db;
    // VuVoiceOverManagerImpl g 0x758FEC (world/voice.hxx: c_world's, loaded once) and its hooks. Null: UNKNOWN (logged).
    vo_manager_t* vo = nullptr;
    const vo_hooks_t* vo_hooks = nullptr;
    // The world's VuPfx runtime / VuPfxManager g 0x75905C (c_effects; null: none - the boats' pfx calls are UNKNOWN).
    pfx_runtime_t* pfx = nullptr;
    pfx_manager_t* pfx_manager = nullptr;
    const pfx_manager_ops_t* pfx_ops = nullptr;
    // A dynamic prop's Build tick 0x546130: its static model component's draw matrix (+0x13C = fromScale(transform
    // +0xB0) x world +0x50) and world aabb (0x531D10) re-made and handed to the renderer - the world's static draw of
    // that owner (c_world: the placements moved). Null: not drawn from the bodies (logged).
    std::function< void( const entity_t& owner ) > prop_draw;
    // The 3D draw manager's add / remove of an owner's static model component (g 0x7590B8 vt+0x28 / +0x2C; the
    // component's +0x1E) - the renderer's: c_world's static draws. Null: not hidden (logged).
    std::function< void( const entity_t& owner, bool on ) > prop_drawn;
    // The model a breakable component names, for its debris (the renderer's: c_world over gfx_retail_t). Null, or null back: the break keeps its state, no pieces.
    std::function< const gfx_scene_view_t*( const std::string& asset ) > debris_model;
    // An entity's output plug fired (VuScriptComponent findPlug 0x504770 + the plug's vt+8 = 0x5023A0): c_world's graph.
    std::function< void( const entity_t& owner, const char* plug ) > fire;
    // An entity's transform moved by a motion component (its owner's draw matrix re-made: the animated prop's 0x61DA50 /
    // the static model's) - the renderer's hand-off (c_world). Null: not redrawn (logged).
    std::function< void( const entity_t& owner ) > prop_moved;
    // An attach component's slot 14 (world/joints.hxx: c_world's) on its owner's motion. Null: not run (logged).
    std::function< void( const entity_t& parent, const float* matrix, const float* linear, const float* angular ) > attach_update;
    // VuAttachEntity's slot 7 / VuRotateMotionEntity's 0x61A4B0 (c_world's). False: not one of them.
    std::function< bool( const entity_t& e ) > motion_release;
    // The audio platform (FMOD's imports - world/sound.hxx; c_world's). Null: no audio calls made (logged).
    const sound_hooks_t* sound = nullptr;
    // The game save (VuCloudSaveManager +0x20, c_world's; null: saves off) and the hidden package manager g 0x758FD0; 0x4ABD60; the input manager's profile load.
    value_t* save_doc = nullptr;
    c_hidden_packages* hidden = nullptr;
    const settings_t* settings = nullptr;     // g 0x758FE0
    c_unlock_manager* unlock = nullptr;       // g 0x758FE8
    std::function< void( ) > save_game;
    std::function< void( platform_input_t& ) > input_profile;
    std::uint32_t viewports = 1;
    std::int32_t control_method = 0;
    std::function< msgbox_ref_t( const std::string&, const std::string&, const std::string& ) > message_box;    // g 0x758FD4 0x4B1430 (world/msgbox.hxx)
    std::int32_t display_w = 1280;
    std::int32_t display_h = 720;
};

struct race_drive_t;
struct race_contact_t;

struct race_session_t
{
    bool active = false;
    std::string mode;
    value_t game_data;
    // gameutil (g 0x758FF8) +0x50, the game-util root: its GameData (VuGame's boat load reads it) and the GameResults the
    // results writer (VuRaceGame slot 12 0x489C60) fills - the UI's results table (core/ui/results.hxx) reads it.
    value_t util;
    bool results_logged = false;   // ours: the first GameResults write logged
    std::uint8_t timetrial_written = 0;   // VuTimeTrialGame +0x738 (0x48F560's once flag; its ctor 0x48ED30 zeroes it)
    // VuSlalomGame's course (world/slalom.hxx): its gates (the game project's VuSlalomGateEntity, linked by their "Next"
    // refs), +0x738 .. +0x74C, and the boat image / transform world its human list [0] is.
    std::vector< slalom_gate_t > slalom_gates;
    slalom_course_t slalom;
    std::uint8_t* slalom_human = nullptr;
    const float* slalom_world = nullptr;
    bool slalom_draw = false;             // +0x744's +0x1E (the draw manager's add - the renderer's, a hook)
    const value_t* boat_db = nullptr;     // BoatDB's document (gameutil +8's +0x58)
    std::uint32_t aligned_frees = 0;   // gNumAlignedFree 0x759050's increments by the session end's body dtors
    race_game_t game;
    race_start_game_t start;
    race_boat_manager_t manager;
    std::vector< std::unique_ptr< race_session_boat_t > > boats;
    // pfx_frame_t's boats / fluids (the words the boat processes hold -> their readers): one entry per boat, kept current by Build step 5.
    ghost_manager_t ghost;              // VuGhostManager g 0x758FC8 (world/ghost.hxx)
    pause_menu_t pause;                 // the pause menu VuActionGameMode +0x60: its gate only (world/pause.hxx)
    ui_game_screens_t screens;          // VuGame +0x84 the Results VuCommonScreen 0x73F588 (ui/common.hxx; bound by c_world::bind_game_screens)
    tire_track_manager_t tire_tracks;   // VuTireTrackManager g 0x7590F4 (world/tiretrack.hxx; its lists stay empty in retail)
    ghost_loads_t ghost_loads;          // VuGhostGame +0x75C .. and +0x740 (ShowError's message)
    ghost_game_t ghost_game;            // the handlers' view of the game (world/ghostplay.hxx)
    std::vector< std::unique_ptr< ghost_record_t > > ghost_records;   // VuGhostManager +0x64
    std::vector< std::unique_ptr< entity_t > > ghost_entities;
    std::vector< std::shared_ptr< void > > ghost_ticks;
    std::function< race_session_boat_t*( const std::string&, const value_t& ) > ghost_make;
    std::function< void( std::size_t ) > boat_models;   // the hull's three instances, c_world's
    const value_t* constant_db = nullptr;
    std::vector< pfx_frame_boat_t > pfx_boats;
    std::vector< pfx_frame_fluids_t > pfx_fluids;
    std::vector< race_start_boat_t > start_boats;
    tick_manager_t ticks;
    // VuEventManager g 0x759028 and the handlers the session's objects registered (world/bus.hxx).
    race_bus_t bus;
    double clock = 0.0;
    const track_manager_t* track = nullptr;
    fleet_manager_t fleet;
    std::vector< fleet_pilot_t > pilots;
    std::vector< fleet_boat_t > fleet_boats;
    // THE global VuRandom 0x736810 (c_world's, seeded once by the engine init
    // 0x566230; the pfx emitters draw from the same one): the roster picks and
    // the AI manager's plans.
    fleet_random_t* random = nullptr;
    // The surface-type table (g_vudynamicsimpl +0x38, VuDynamicsContactManagerImpl ctor 0x574EC0: the boat
    // agent's surface.hxx) from VuDBAsset/SurfaceDB; built when the session starts.
    surface_table_t surfaces;
    bool surfaces_built = false;
    // The lens-water manager g 0x759048's emitter list (VuLensWaterManagerImpl vt+0x38 / +0x3C, render side - a
    // hook): what each VuLensWaterEmitterEntity's slot 6 0x5E6720 handed it (world/lens.hxx). Nothing reads it yet.
    std::vector< const void* > lens_emitters;
    float viewport_blur = 0.0f;          // the viewport's +0x278 (boat manager 0.25, or boat +0x3F8 x 0.25): 0x5588B0's radial amount
    // VuLensWaterManagerImpl g 0x759048's drops (world/lens.hxx): slot 10 / 11 from the explosions / the boat manager,
    // its tick 0x514500 in "Build". Its +0x10 half width is written outside what was read (the render side) - 0 here.
    lens_water_manager_t lens;
    // Counts of the stopped paths reached (UNKNOWN), logged once each.
    int stopped_decision = 0, stopped_build = 0, stopped_transform = 0, stopped_viewports = 0, stopped_fleet = 0;
    // The world's services (refreshed per frame) and each boat's Decision
    // composition (world/drive.hxx; by boat index).
    race_world_t world;
    std::vector< std::shared_ptr< race_drive_t > > drives;
    // ConstantDB's ["VuDBAsset"]["Camera"]["VerticalFOV"] (gameutil +4's
    // document, 0x4D2C60's lookups) - the race camera's fov input.
    value_t vertical_fov;
    // The viewport's camera (g 0x759060 +0x28: the 0x240 bytes the Viewports
    // tick 0x4AB0A0 copies in) and whether one was copied yet.
    std::uint8_t viewport_camera[ 0x240 ]{};
    bool viewport_set = false;
    // Each hook reached that is not recreated / not wired, by name (logged once).
    std::map< std::string, int > unwired;
    // The nuisance boats' entities (VuAiNuisanceDriverStartEntity +0x7C's children, 0x42FDC0).
    std::vector< std::unique_ptr< entity_t > > nuisance;
    // Bullet's contact-added callback for the race's dynamics world (world/drive.hxx race_contact_added ->
    // boat contact.hxx contact_added): built by race_drive_setup, installed by whatever steps the bodies.
    std::shared_ptr< race_contact_t > contact_user;
    contact_callbacks_t contact{};
    // VuCollisionManager g 0x758FBC (world/collide.hxx, the water agent's): the manager, its surface-pair tables (from
    // VuDBAsset/SurfaceTableDB, held here as the asset's document), whether its listener is in the contact manager's +4 list.
    collision_manager_t collision;
    collision_tables_t collision_tables;
    value_t collision_db;
    bool collision_listening = false;
    int collision_impacts = 0;   // dev counts (not retail): impact records the tick played, live scrape records
    int collision_scrapes = 0;
    // g_vudynamicsimpl 0x75900C: the dynamics world (world/dynamics.hxx), made before the boats as the engine
    // init 0x566230 makes it; its DynamicsSync / DynamicsKick ticks registered first in their phases.
    std::unique_ptr< race_dynamics_t > dynamics;
    // The level's static bodies (world/bodies.hxx) and their collision mesh assets.
    boat_mesh_library_t level_meshes;
    std::vector< std::unique_ptr< level_body_t > > level_bodies;
    // The level's power-ups made live (world/pickups.hxx): their bodies are in level_bodies too.
    struct powerup_live_t
    {
        struct race_session_t* session = nullptr;
        entity_t* entity = nullptr;
        powerup_t p;
        bool boost = false;
        bool package = false;                 // VuPowerUpHiddenPackageEntity
        std::string package_name;             // its +0x138
        std::uint8_t randomize = 1;           // +0x138
        std::vector< std::uint8_t > image;    // what 0x43DB40 reads: +0x13C the energy, +0x140 the multiplayer byte
        level_body_t* body = nullptr;         // +0x114
    };
    std::vector< std::unique_ptr< powerup_live_t > > powerups;
    value_t powerup_db;                       // VuDBAsset/PowerUpDB
    int powerups_applied = 0;                 // ours: the census
    // The level's dynamic game props made live (world/dynprop.hxx): their bodies are in level_bodies too.
    struct dyn_prop_t
    {
        entity_t* entity = nullptr;
        dynamic_prop_t* p = nullptr;
        rigid_component_t* c = nullptr;
        level_body_t* body = nullptr;
        dynamic_prop_live_t live;
        dynamic_prop_motion_t motion;
        dynamic_prop_init_t init;
        bool buoy = false;
        bool mine = false;               // VuFloatingMineEntity (world/mine.hxx)
        std::uint8_t exploded = 0, request = 0;   // the mine's +0x214 / +0x215
        bool step_added = false;         // the step callback in g_vudynamicsimpl's list (the hide removes it)
        float timer = 0.0f;              // +0x17C (the Decision tick's)
        dynamic_prop_hit_t hit;          // +0x164 / +0x174 / +0x178 (the contact callback 0x437F40's)
        std::uint8_t step_tag = 0;       // the entity's +0xB0 part: the step callback's identity in +0x48
        // VuBreakableDynamicGamePropEntity: +0x164 the break request, +0x168 the weak ref (its object +0x174 the target),
        // +0x1A0 the kept velocity (its 4th word +0x1AC never written: UNKNOWN, 0 here), the breakable component's (+0x184)
        // state +0xE4.
        bool breakable = false;
        std::uint8_t break_request = 0;
        const void* break_target = nullptr;
        float break_velocity[ 3 ] = { };
        race_debris_t debris;              // the breakable component (+0x184): its +0xE4 is debris.m_state
        race_session_t* session = nullptr;
    };
    std::vector< std::unique_ptr< dyn_prop_t > > dyn_props;
    int buoy_hits = 0;                   // dev count (not retail): OnCheckPointBuoyHit broadcasts made
    int prop_hits = 0, prop_splashes = 0;   // dev counts (not retail): 0x437F40 hits taken, dynamic-prop splashes started
    int mine_explosions = 0, explosion_hits = 0;   // dev counts (not retail)
    // The level's VuBreakablePropEntity (world/breakable.hxx), live from their slot 6 0x434620.
    struct breakable_live_t
    {
        entity_t* entity = nullptr;
        breakable_t* p = nullptr;
        rigid_component_t* c = nullptr;
        level_body_t* body = nullptr;
        std::uint8_t request = 0;          // +0xD0
        float velocity[ 3 ] = { };         // +0xD4
        const void* target = nullptr;      // the weak ref's object (+0xF0): the other body's owner (a session boat's body here)
        race_debris_t debris;              // the breakable component (+0x80): its +0xE4 is debris.m_state
        race_session_t* session = nullptr;
    };
    std::vector< std::unique_ptr< breakable_live_t > > breakables;
    std::vector< std::unique_ptr< force_live_t > > forces;   // VuForceAffectorEntity (world/affector.hxx), live from slot 6 0x4398C0
    // The level's VuKeyframeMotionEntity (world/keymotion.hxx), live from their slot 6 0x5CE9E0.
    struct key_live_t
    {
        entity_t* entity = nullptr;
        key_motion_t* k = nullptr;
        entity_t* target_entity = nullptr;   // the "Entity" ref's entity (its VuMotionComponent the target)
        key_motion_target_t target;
        race_session_t* session = nullptr;
        bool stepped = false;   // ours: the first update logged
    };
    std::vector< std::unique_ptr< key_live_t > > key_motions;
    int key_unrecreated = 0;
    // The level's VuControlEntity (world/keyframe.hxx), live from their slot 6 0x5CD490; the target's VuControlComponent
    // (race.hxx control_component_of) and its owner, and the owner's layout box 0x5D9DD0 writes (nothing reads it here).
    struct control_live_t
    {
        entity_t* entity = nullptr;
        control_entity_t* c = nullptr;
        entity_t* target_entity = nullptr;
        control_component_t* comp = nullptr;
        water_layout_box_t layout;
        race_session_t* session = nullptr;
        int changes = 0;   // dev count (not retail): the component's slot 0 calls
    };
    std::vector< std::unique_ptr< control_live_t > > controls;
    int control_unrecreated = 0;
    // The race project's entity tree (c_world's; the gameInitialize walk's and the session end's 0x500B80).
    entity_t* tree = nullptr;
    int breaks = 0;   // dev count (not retail)
    // VuExplosionManagerImpl g 0x759034's listeners (+4 / +8; world/mine.hxx): the human drivers' +0x80 parts, added by
    // their slot 6 0x40F210 (boat human.hxx human_game_initialize).
    std::vector< explosion_listener_t > explosion_listeners;
    // The gfx-settings blender g 0x759430 (world/blend.hxx; 0x52F620 at the session's start) and the level's gfx-settings
    // entities' blend views (the zones in its list; the global's not listed - its timer only), made by their gameInitialize.
    gfx_blender_t blender;
    // The ray test's view of the dynamics world (boat rays.hxx; world/drive.hxx race_drive_ray).
    boat_ray_world_t rays;
    std::vector< std::unique_ptr< gfx_zone_t > > gfx_entities;
    // The event graph's race-side receivers, by the entity's path below the project root (c_world's graph keys):
    // the gfx-settings entities' "Trigger" (0x5A7230 zones / 0x5D2980 the global) and the nuisance start entities'
    // "DriverTarget" / "DriverDisable" (0x42FF90 / 0x430020).
    struct gfx_plug_t
    {
        gfx_zone_t* zone = nullptr;
        bool global = false;
    };
    std::map< std::string, gfx_plug_t > gfx_by_path;
    struct nuisance_plug_t
    {
        nuisance_start_t* state = nullptr;
        int child = -1;   // the session boat the start entity's +0x7C is (-1: none made)
    };
    std::map< std::string, nuisance_plug_t > nuisance_by_path;
    // g_vutriggermanager 0x759064's instigators (trigger/volume.hxx).
    trigger_manager_t triggers;
    // Retail's 32-bit words for the weak references the nuisance plugs link (boat nuisance.hxx vu_words_t): a
    // boat's entity is the token race_entity_token(k), its driver token + 0x1000 (+0x220 holds it), every other
    // word 0 until written. The entity repository g 0x759080 (hash -> entity) is not recreated: a VuParams
    // entity's hash word is the token here, the repository's find the identity on a boat's token.
    std::map< std::uint32_t, std::uint32_t > words;
    // THE RACE RULES (world/rules.hxx): VuRaceGame's +0x6F8 state machine and +0x744, the game fields they read, the game's
    // boats as they see them (s.game.boats' order: the first game.boats.size( ) session boats - the nuisance boats are not
    // the game's), whether the game's tick 0x482F10 still runs (slot 7 answered true) and its stopped paths.
    race_rules_t rules;
    race_rules_game_t rules_game;
    std::vector< race_rules_boat_t > rules_boats;
    bool rules_running = false;
    bool game_answer = true;   // VuGame's tick 0x482F10's answer, last frame (false: the game is done - Run's 0x4BAB50 reads it)
    int rules_frames = 0;
    // THE TEST INPUT (ours, flagged - not retail): --input-script's per-frame pad states (world/drive.hxx race_input_script_t),
    // answered by the platform input hook in place of the device; null: the device (input below). frames: the session frames
    // run (race_session_frame), the script's clock.
    std::shared_ptr< struct race_input_script_t > input_script;
    bool dock = false;        // the front end's dock boat (world/dock.hxx): ticks run, `active` stays false
    bool autopilot = false;   // --autopilot (world/autopilot.hxx)
    bool autopilot_keys = false;   // --autopilot-keys
    int autopilot_held = 0;
    // The input retail reads (game/input.hxx): the managers on InputDB's document, which `input_db` keeps alive. Made here at
    // the session's start, not by the engine's init 0x566230 (ours: no input exists outside a race yet). Null: InputDB did not
    // load; the hooks answer 0.
    value_t input_db;
    std::shared_ptr< platform_input_t > input;
    // Ours (a test driver): --keys "FRAME:KEY:0|1,..." presses retail key indices (input_key_index's names) on the device at
    // session frames, so the keyboard path through the managers runs without touching the desktop.
    struct key_script_t { int frame; int key; bool down; };
    std::vector< key_script_t > key_script;
    int frames = 0;
};

// The model's entity / driver tokens (see race_session_t::words).
inline std::uint32_t race_entity_token( int k )
{
    return 0x00100000u * static_cast< std::uint32_t >( k + 1 );
}
inline int race_entity_of_token( const race_session_t& s, std::uint32_t token )
{
    if ( token == 0 || ( token & 0x000FFFFFu ) != 0 )
        return -1;
    const int k = static_cast< int >( token / 0x00100000u ) - 1;
    return k >= 0 && static_cast< std::size_t >( k ) < s.boats.size( ) ? k : -1;
}

// An entity's path the way the event graph names it: its names below the project root, '/'-joined.
inline std::string race_entity_path( const entity_t* e )
{
    std::string path;
    for ( ; e && e->m_parent; e = e->m_parent )
        path = path.empty( ) ? e->m_name : e->m_name + "/" + path;
    return path;
}

// The Decision tick 0x41EFF0 and the Viewports tick 0x4AB0A0 composed on the
// session (world/drive.hxx).
// A broadcast the race's code makes (0x505650 / 0x505690): the world's (c_world::race_broadcast - the session's bus, then
// the live HUDs) when there is a world, else the bus alone.
inline void race_session_post( race_session_t& s, const char* name, const ui_params_t* params = nullptr )
{
    if ( s.world.broadcast )
    {
        s.world.broadcast( name, params );
        return;
    }
    const ui_params_t none{ };
    race_bus_broadcast( s.bus, event_hash( name ), params ? *params : none );
}

inline void race_drive_setup( race_session_t& s, const value_t* constant_db, std::size_t first = 0 );
inline void race_drive_decision( race_session_t& s, std::size_t boat, float dt );
inline void race_drive_viewports( race_session_t& s );
inline void race_drive_build( race_session_t& s, std::size_t boat, float dt );
// VuGame's tick 0x482F10 as the Decision phase reaches it (world/drive.hxx).
inline void race_session_game_mode( void* user, float dt );
// The Decision tick the game mode manager registers: the action mode when there is one (its Run state's 0x4BAB50
// calls race_session_game_mode), else race_session_game_mode itself.
inline void race_session_mode_tick( void* user, float dt )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    if ( s.world.game_mode )
        s.world.game_mode( dt );
    else
        race_session_game_mode( user, dt );
}

// The voice-over say gate's boat half (0x4B7EA0 / 0x4B7F50): boat +0x46B == 0, then its driver's (+0x220) vt+0x30 and
// vt+0x38 (slots 12 / 14: the start boats' driver class answers). A boat image not in this race: false.
inline bool race_session_vo_boat_ok( const race_session_t& s, const void* boat )
{
    for ( std::size_t k = 0; k < s.boats.size( ) && k < s.start_boats.size( ); ++k )
        if ( boat == s.boats[ k ]->graph.objects[ 0 ].image.data( ) )
            return s.boats[ k ]->graph.objects[ 0 ].image[ 0x46B ] == 0 && s.start_boats[ k ].cls.slot[ 0 ] && s.start_boats[ k ].cls.slot[ 2 ];
    return false;
}

// The voice-over manager's slot 7 (vt+0x1C, 0x4B7EA0: say now) / slot 8 (vt+0x20, 0x4B7F50: queue) on (boat, line).
inline void race_unwired( race_session_t& s, const std::string& what );
inline void race_session_vo_say( race_session_t& s, const void* boat, const char* line, bool queue )
{
    if ( !s.world.vo || !s.world.vo_hooks )
    {
        race_unwired( s, "the voice-over manager g 0x758FEC (no manager bound) - UNKNOWN" );
        return;
    }
    if ( queue )
        vo_queue( *s.world.vo, boat, line, *s.world.vo_hooks );
    else
        vo_say_now( *s.world.vo, boat, line, *s.world.vo_hooks );
}

// Its tick 0x4B8000 (registered in "Decision" by its init 0x4B7970 - the game init 0x4024E0 makes it after the AI
// manager (0x40281D) and before the game mode manager (0x402A81), so it runs between their ticks).
inline void race_session_vo_tick( void* user, float dt )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    if ( s.world.vo && s.world.vo_hooks )
        vo_tick( *s.world.vo, dt, *s.world.vo_hooks );
}

// VuLensWaterManagerImpl's tick 0x514500 (world/lens.hxx lens_water_tick): every emitter's slot 0 rate on the first
// viewport's position (g 0x759060 +0x28: the viewport camera's eye +0x00 - none copied yet: the rates are UNKNOWN and the
// tick is not run), THE global VuRand 0x736810 (the session's fleet_random_t, copied through pfx_rand_t as c_effects does).
inline void race_session_lens_tick( void* user, float dt )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    if ( !s.random )
    {
        race_unwired( s, "the lens-water tick 0x514500 with no VuRand bound - not run (UNKNOWN)" );
        return;
    }
    if ( !s.lens_emitters.empty( ) && !s.viewport_set )
    {
        race_unwired( s, "the lens-water tick 0x514500 before the viewport's camera is copied (its position UNKNOWN) - not run" );
        return;
    }
    float eye[ 3 ];
    std::memcpy( eye, s.viewport_camera, 12 );
    const auto rate = [ &s, &eye ]( int i ) -> float {
        entity_t& e = *const_cast< entity_t* >( static_cast< const entity_t* >( s.lens_emitters[ static_cast< std::size_t >( i ) ] ) );
        const transform_component_t* t = entity_transform( e );
        if ( !e.m_state || !t )
        {
            race_unwired( s, "a lens-water emitter with no state / transform - its rate UNKNOWN, answered 0" );
            return 0.0f;
        }
        const lens_emitter_state_t& ls = *static_cast< const lens_emitter_state_t* >( e.m_state.get( ) );
        return lens_water_emitter_rate( t->world + 12, ls.radius, ls.drops, eye );
    };
    pfx_rand_t r;
    r.seed = s.random->seed;
    r.previous = s.random->last;
    std::memcpy( r.table, s.random->table, sizeof( r.table ) );
    lens_water_tick( s.lens, dt, static_cast< int >( s.lens_emitters.size( ) ), rate, r );
    s.random->seed = r.seed;
    s.random->last = r.previous;
    std::memcpy( s.random->table, r.table, sizeof( s.random->table ) );
}

// The graph object a boat-image pointer slot refers to (-1 none).
inline int race_session_ref( const boat_graph_t& g, int object, int slot )
{
    if ( object < 0 || object >= static_cast< int >( g.objects.size( ) ) )
        return -1;
    for ( const image_ref_t& r : g.objects[ static_cast< std::size_t >( object ) ].refs )
        if ( r.slot == slot )
            return r.object;
    return -1;
}

// The bytes a boat-image pointer slot points at: the referenced object's
// image PLUS the ref's offset (the body: boat +0x21C points 16 bytes into its
// btAlignedAlloc block, past the block's own unaligned address - boat
// construct.hxx; the offset was ignored before world batch 14, so the
// sections load read the body 16 bytes early).
inline std::uint8_t* race_session_image( race_session_boat_t& b, int object, int slot )
{
    if ( object < 0 || object >= static_cast< int >( b.graph.objects.size( ) ) )
        return nullptr;
    for ( const image_ref_t& r : b.graph.objects[ static_cast< std::size_t >( object ) ].refs )
        if ( r.slot == slot )
            return r.object >= 0 ? b.graph.objects[ static_cast< std::size_t >( r.object ) ].image.data( ) + r.offset : nullptr;
    return nullptr;
}

// 0x420A70 on the boat's graph objects (boat +0x21C body, +0x224 camera, +0x228
// hull and its +0x1CC solver, +0x22C engine, +0x230 animation, +0x238 damage,
// +0x70 instigator; [[+0x234] +0x30] +0x14 the pfx owner's flags). The asset
// factory's doesAssetExist answers from the pack's directory (as the oracle's
// factory does); create hands a handle; the collision mesh / fluids count
// readers are absent - UNKNOWN, the load stops (b.stop says where).
inline void race_unwired( race_session_t& s, const std::string& what );

// 0x5322C0 on the boat's +0x2A4: the asset manager's exists / create (0x534000 / 0x5340B0) = the pack, through the mesh
// library's reader (race_session_sections sets it first).
inline void race_session_load_model( race_session_boat_t& b, const std::string& name )
{
    std::vector< std::uint8_t > blob;
    if ( !b.meshes.read( b.meshes.read_user, "VuAnimatedModelAsset/" + name, blob ) || !boat_rig_model( b.rig, blob.data( ), blob.size( ) ) )
        race_unwired( *b.owner, "0x420A70: VuAnimatedModelAsset/" + name + " did not load - the boat has no skeleton" );
}

// 0x410610 on +0x230 (gameInitialize): the strings the Animation sections left at +0x14 / +0x2C / +0x44.
inline void race_session_create_rig( race_session_boat_t& b )
{
    const std::string names[ 3 ] = { b.io.animation_strings[ 0x14 ], b.io.animation_strings[ 0x2C ], b.io.animation_strings[ 0x44 ] };
    boat_rig_create( b.rig, names, [ &b ]( const std::string& name, std::vector< std::uint8_t >& blob ) {
        return !name.empty( ) && b.meshes.read( b.meshes.read_user, "VuAnimationAsset/" + name, blob );
    } );
    LOG_VERBOSE( "boat rig: Thrust '%s' %d, Turn '%s' %d, Transform '%s' %d", names[ 0 ].c_str( ), b.rig.has[ 0 ] ? 1 : 0, names[ 1 ].c_str( ), b.rig.has[ 1 ] ? 1 : 0,
                 names[ 2 ].c_str( ), b.rig.has[ 2 ] ? 1 : 0 );
}

// The fill's hooks on a session boat (bound once per boat; used by postLoad's fill and the gameInitialize's 0x419FF0).
inline boat_pfx_fill_hooks_t race_session_pfx_hooks( race_session_boat_t& rb )
{
    boat_pfx_fill_hooks_t h;
    h.user = &rb;
    h.manager = true;
    h.create = []( void* v, const char* name ) {
        race_session_t& x = *static_cast< race_session_boat_t* >( v )->owner;
        return x.world.pfx_ops->create( x.world.pfx_ops->user, name );
    };
    h.release = []( void* v, std::uint32_t sys ) {
        race_session_t& x = *static_cast< race_session_boat_t* >( v )->owner;
        x.world.pfx_ops->release( x.world.pfx_ops->user, sys );
    };
    h.start = []( void* v, std::uint32_t sys ) {
        race_session_t& x = *static_cast< race_session_boat_t* >( v )->owner;
        const boat_pfx_ops_t o = boat_pfx_runtime_ops( *x.world.pfx );
        o.start( o.user, sys );
    };
    h.root = []( void*, std::uint32_t sys ) { return sys; };   // 0x401DA0
    h.type_of = []( void* v, std::uint32_t object ) -> std::uint32_t {
        race_session_t& x = *static_cast< race_session_boat_t* >( v )->owner;
        const std::uint32_t vt = x.world.pfx->heap[ object ];
        for ( const pfx_class_t& c : g_pfx_classes )
            if ( c.vtable == vt )
                return c.descriptor;
        race_unwired( x, "postLoad's Pfx step: a pfx object's vtable outside world/pfxclass.hxx's table - its getType UNKNOWN (answered 0)" );
        return 0u;
    };
    h.find_bone = []( void* v, const char* name ) -> std::int32_t {
        race_session_boat_t& x = *static_cast< race_session_boat_t* >( v );
        if ( x.rig.model )   // 0x41A5BC: findNodeByName 0x52F3C0 on the instance +0x2A4's skeleton, an empty name too
            return skel_instance_find_node( x.rig.model->instance, name );
        if ( name && name[ 0 ] )
        {
            ++x.pfx_unknown_bones;
            race_unwired( *x.owner, "postLoad's Pfx step: the boat's model instance +0x2A4 did not load: the record's bone UNKNOWN, -1 here" );
        }
        return -1;
    };
    h.fluids_set = []( void* v, const std::string& name ) {
        race_session_boat_t& x = *static_cast< race_session_boat_t* >( v );
        // 0x5243A0 on the controller's own VuFluidsObject (its ctor's 0x5240C0 - not the hull's +0x1CC): boat load.hxx
        // load_fluid_mesh on that image with the load's factory hooks. SUBSTITUTION: its two record arrays' retail
        // allocation addresses are not known - the hull solver's (io.array_address) are written as the data words.
        boat_load_t f{ };
        f.solver = x.pfx_fluids;
        f.hooks = x.io.hooks;
        f.array_address[ 0 ] = x.io.array_address[ 0 ];
        f.array_address[ 1 ] = x.io.array_address[ 1 ];
        if ( !load_fluid_mesh( f, name ) )
            race_unwired( *x.owner, "postLoad's Pfx step: 0x5243A0 on the solver stopped (a factory hook) - UNKNOWN" );
    };
    h.word = []( void* v, std::uint32_t a ) -> std::uint32_t {
        race_session_boat_t& x = *static_cast< race_session_boat_t* >( v );
        if ( a >= x.pfx.fluids && a - x.pfx.fluids < 0x104u )
            return load_word( x.pfx_fluids, static_cast< int >( a - x.pfx.fluids ) );
        for ( const pfx_class_t& c : g_pfx_classes )   // a descriptor's +4: its parent (the next name of its chain)
            if ( a == c.descriptor + 4u )
            {
                const char* bar = std::strchr( c.chain, '|' );
                if ( !bar )
                    return 0u;
                const std::string parent( bar + 1, std::strcspn( bar + 1, "|" ) );
                for ( const pfx_class_t& p : g_pfx_classes )
                    if ( parent == p.name )
                        return p.descriptor;
                return 0u;
            }
        return x.owner->world.pfx->heap[ a ];
    };
    h.put = []( void* v, std::uint32_t a, std::uint32_t value ) {
        race_session_boat_t& x = *static_cast< race_session_boat_t* >( v );
        if ( std::uint32_t* w = x.owner->world.pfx->heap.find( a ) )
            *w = value;
        else
            race_unwired( *x.owner, "postLoad's Pfx step: a bind write outside the pfx runtime's heap - UNKNOWN, not written" );
    };
    return h;
}

// postLoad's Pfx step (boat load.hxx io.pfx_fill -> boat effects.hxx boat_pfx_fill, the boat agent's 0x419EF0 / 0x419D70 /
// 0x41A1A0 / 0x419FF0) bound to the race (world batch 30). The controller +0x30 / +0x34 are retail heap addresses written
// into bound processes and compared by the pfx frame: SUBSTITUTION - tokens here, 0xB0A70000 + 0x1000 x the boat's index
// (the boat) and 0xF1D00000 + the same (the controller's own VuFluidsObject +0x34, made by its ctor 0x4192E0's 0x5240C0;
// 0x104 words read from its image). Hooks:
//   create / release   g_vupfx 0x75904C vt+0x28 / +0x2C = the world's VuPfx manager ops (world/pfxmgr.hxx, the water agent's)
//   start              the system's vt+0x2C (slot 11) = boat_pfx_runtime_ops's start
//   root               the system's vt+0x40 = 0x401DA0 (mov eax, ecx; ret - read): the system itself
//   type_of            the object's vt+4: world/pfxclass.hxx g_pfx_classes (vtable -> getType's descriptor); its +4 parents
//                      from the rows' chains (read from the image: 0x7367D0 -> 0x7369B4 -> 0x7369BC -> 0x7369D4 -> 0x7369CC,
//                      0x7367D8 -> 0x7369D4); a vtable outside the table answers 0 (logged UNKNOWN)
//   find_bone          0x52F3C0 on the model instance (+0x2A4, world/rig.hxx) +0x10; no model: answered -1 and counted / logged
//                      UNKNOWN for every non-empty name
//   fluids_set         0x5243A0 on that object = boat load.hxx load_fluid_mesh (the boat agent's, 1:1)
//   word / put         the pfx runtime's heap; the fluids token's words = the solver image's
inline void race_session_bind_pfx_fill( race_session_boat_t& b )
{
    b.pfx = boat_pfx_t{ };
    b.pfx_filled = false;
    b.pfx_unknown_bones = 0;
    race_session_t& s = *b.owner;
    if ( !s.world.pfx || !s.world.pfx_ops || !s.world.pfx_ops->create || !s.world.pfx_ops->release )
    {
        race_unwired( s, "postLoad's Pfx step with no VuPfx runtime / manager ops - the fill not bound (UNKNOWN)" );
        return;
    }
    const int pfx = race_session_ref( b.graph, 0, 0x234 );
    const int fluids = pfx < 0 ? -1 : race_session_ref( b.graph, pfx, 0x34 );
    if ( fluids < 0 )
    {
        race_unwired( s, "postLoad's Pfx step: the controller (0x4192E0) has no +0x34 VuFluidsObject in the boat graph - the fill not bound (UNKNOWN)" );
        return;
    }
    b.pfx_fluids = b.graph.objects[ static_cast< std::size_t >( fluids ) ].image.data( );
    b.pfx.boat = 0xB0A70000u + 0x1000u * static_cast< std::uint32_t >( b.index );
    b.pfx.fluids = 0xF1D00000u + 0x1000u * static_cast< std::uint32_t >( b.index );
    b.io.pfx_user = &b;
    b.io.pfx_fill = []( void* u, const value_t& fallback, const value_t& own, const boat_driver_t& driver, bool create ) {
        race_session_boat_t& rb = *static_cast< race_session_boat_t* >( u );
        const boat_pfx_fill_hooks_t h = race_session_pfx_hooks( rb );
        const bool ok = boat_pfx_fill( rb.pfx, fallback, own, driver, create, h );
        rb.pfx_filled = ok;
        return ok;
    };
}

inline bool race_session_sections( race_session_boat_t& b, const value_t& boatdb, const c_archive& archive, const surface_table_t* surfaces )
{
    std::uint8_t* boat = b.graph.objects[ 0 ].image.data( );
    std::uint8_t* body = race_session_image( b, 0, 0x21C );
    std::uint8_t* hull = race_session_image( b, 0, 0x228 );
    const int hull_k = race_session_ref( b.graph, 0, 0x228 );
    std::uint8_t* solver = race_session_image( b, hull_k, 0x1CC );
    std::uint8_t* engine = race_session_image( b, 0, 0x22C );
    const int pfx = race_session_ref( b.graph, 0, 0x234 );
    std::uint8_t* pfx_owner = race_session_image( b, pfx, 0x30 );
    boat_load_t& io = b.io;
    io = boat_load_t{ };
    io.boat = boat;
    io.boat_address = 0;   // retail's allocation address: read only by the shape (after the mesh - not reached)
    if ( body )
        std::memcpy( &b.body, body, 0x2D4 );   // retail's VuRigidBody is 0x2D4 bytes (sizeof is 0x2E0: alignment padding)
    if ( engine )
        std::memcpy( b.engine.object.image, engine, sizeof( b.engine.object.image ) );
    io.body = &b.body;
    io.camera = race_session_image( b, 0, 0x224 );
    io.hull = hull;
    io.solver = solver;
    io.engine = &b.engine;
    io.animation = race_session_image( b, 0, 0x230 );
    io.pfx_owner_flags = pfx_owner ? load_word( pfx_owner, 0x14 ) : 0;
    io.damage = race_session_image( b, 0, 0x238 );
    io.instigator = race_session_image( b, 0, 0x70 );
    if ( !io.camera || !io.hull || !io.solver || !io.animation || !io.damage || !io.instigator || !b.driver || b.driver->graph.empty( ) )
    {
        b.stop = "the boat graph lacks an object 0x420A70 reads";
        return false;
    }
    for ( int at : { 0xC4, 0xDC, 0xF4, 0x11C, 0x148, 0x160, 0x178, 0x1A4, 0x1BC, 0x1D4, 0x1EC, 0x204 } )
        io.boat_strings[ at ] = "";
    for ( int at : { 0x4, 0xD4, 0xEC, 0x104, 0x11C } )
        io.hull_strings[ at ] = "";
    for ( int at : { 0x14, 0x2C, 0x44, 0x7C, 0x94, 0xAC } )
        io.animation_strings[ at ] = "";
    io.boatdb = &boatdb;
    io.driver = boat_driver_of( load_word( b.driver->graph[ 0 ].image.data( ), 0 ) );
    // g_vudynamicsimpl is live in a race; its surface table (slot 2) and the
    // world's gravity are not wired: the surface hook is absent, so a load that
    // got that far would stop there rather than use a stand-in.
    io.dynamics = true;
    using ctx_t = race_session_load_ctx_t;
    b.load_ctx = ctx_t{ &b, &archive, 1 };
    io.hooks.user = &b.load_ctx;
    io.hooks.exists = []( void* u, const char* type, const std::string& n )
    {
        const ctx_t& c = *static_cast< ctx_t* >( u );
        const std::string full = std::string( type ) + "/" + n;
        for ( const std::string& rest : c.archive->names( full ) )   // names() strips the prefix: exact = ""
            if ( rest.empty( ) )
                return true;
        return false;
    };
    // The asset factory's create / release (g 0x759004 slots 7 / 9): the VuCollisionMeshAsset / VuFluidsMeshAsset
    // the load creates come from the boat agent's mesh library (core/boat/meshes.hxx: the 1:1 loads 0x51AF20 /
    // 0x523770; its name list and refcounted cache are its flagged substitution); every other create hands a
    // handle from 0x10000 (the rendering assets' loads are hooks); every other release is the null one 0x421870
    // makes on a fresh boat. The surface-type table (g_vudynamicsimpl +0x38, slot 2 0x5754D0 through 0x4F88A0:
    // the boat agent's surface.hxx, built from SurfaceDB) answers both the collision mesh materials' byte +40 and
    // the body's +0x2C8; without one (SurfaceDB unreadable) io.hooks.surface stays null and the load stops there.
    b.meshes = boat_mesh_library_t{ };
    b.meshes.surfaces = surfaces;
    b.meshes.read_user = const_cast< c_archive* >( &archive );
    b.meshes.read = []( void* u, const std::string& asset, std::vector< std::uint8_t >& blob ) {
        return static_cast< const c_archive* >( u )->read( asset.c_str( ), blob );
    };
    io.hooks.create = []( void* u, const char* type, const std::string& n ) -> std::uint32_t {
        ctx_t& c = *static_cast< ctx_t* >( u );
        if ( std::strcmp( type, "VuCollisionMeshAsset" ) == 0 || std::strcmp( type, "VuFluidsMeshAsset" ) == 0 )
            return boat_mesh_create( &c.b->meshes, type, n );
        return 0x10000u + c.next++;
    };
    io.hooks.release = []( void* u, std::uint32_t asset ) {
        if ( asset == 0 )
            return true;
        return asset < 0x10000u && boat_mesh_release( &static_cast< ctx_t* >( u )->b->meshes, asset );
    };
    io.hooks.surface = nullptr;
    if ( surfaces != nullptr )
        io.hooks.surface = []( void* u, const std::string& name ) -> std::uint8_t {
            return surface_id( *static_cast< ctx_t* >( u )->b->meshes.surfaces, name.c_str( ) );
        };
    io.hooks.mesh = []( void* u, std::uint32_t a, striding_mesh_t& out ) {
        const collision_mesh_asset_t* m = static_cast< ctx_t* >( u )->b->meshes.collision_of( a );
        if ( !m )
            return false;
        collision_mesh_striding( *m, out );
        return true;
    };
    io.hooks.fluid_counts = []( void* u, std::uint32_t a, int& vertices, int& edges ) {
        boat_load_hooks_t lib{ };
        boat_mesh_hooks( static_cast< ctx_t* >( u )->b->meshes, lib );
        return lib.fluid_counts( lib.user, a, vertices, edges );
    };
    io.hooks.fluid_aabb = []( void* u, std::uint32_t a, float* box ) {
        boat_load_hooks_t lib{ };
        boat_mesh_hooks( static_cast< ctx_t* >( u )->b->meshes, lib );
        return lib.fluid_aabb( lib.user, a, box );
    };
    race_session_bind_pfx_fill( b );
    io.model_user = &b;
    io.model0 = []( void* u, const std::string& name ) { race_session_load_model( *static_cast< race_session_boat_t* >( u ), name ); };
    const bool ok = boat_load_sections( io );
    if ( !ok && b.stop.empty( ) )
        b.stop = io.hooks.surface == nullptr
            ? "the surface-type table (g_vudynamicsimpl +0x38 slot 2, for '" + io.boat_strings[ 0x11C ] + "': not recreated) or a path before it ("
                + std::to_string( b.meshes.entries.size( ) ) + " mesh asset(s) loaded: " + ( b.meshes.entries.empty( ) ? std::string( "none" ) : b.meshes.entries[ 0 ].key )
                + ( b.meshes.entries.size( ) > 1 ? ", " + b.meshes.entries[ 1 ].key : std::string( ) ) + ")"
            : "a 0x420A70 path not modelled (boat load.hxx returned false)";
    return ok;
}

inline void race_unwired( race_session_t& s, const std::string& what );

// A VuWater wave POINTER in a race image (the hull's wake slots +0x1DC /
// +0x1E0 / +0x1E4 / +0x1E8, the solver's +0x34 array words) is a model value
// here: 0x80000000 | the wave's index in the built water (retail's heap
// address - only its identity is ever read: the thunk's exclusion compares,
// the wake update's dereference). 0 is null.
constexpr std::uint32_t k_race_wave_token = 0x80000000u;
inline std::uint32_t race_wave_token( int wave )
{
    return wave < 0 ? 0u : ( k_race_wave_token | static_cast< std::uint32_t >( wave ) );
}
// The token's wave: -1 for null, -2 when the word is not a token of a wave
// the built water holds (UNKNOWN).
inline int race_wave_of( const water_built_t* b, std::uint32_t word )
{
    if ( word == 0 )
        return -1;
    const std::uint32_t index = word & ~k_race_wave_token;
    if ( !( word & k_race_wave_token ) || !b || index >= b->waves.size( ) )
        return -2;
    return static_cast< int >( index );
}

// VuBoatEntity slot 17 0x421980 / slot 18 0x421940 (boat entity.hxx
// boat_enable / boat_disable: VuDynamicsImpl slots 15 / 11 and 16 / 12 -
// manager.hxx) on the race's dynamics world. The body's collision object is
// its compound (0x420A70's +0xCC, boat load.hxx io.compound) under the body's
// transform, its processing threshold the body's +0xC4; its motion state
// (+0x1F4 = boat +0x5C) is boat_motion_set 0x41E460 on the boat's transform
// component (+0x50). The +0x60 interface's step-callback slots: slot 0
// 0x410B30 (a ret 8 - nothing), slot 1 tickThunk 0x41E5E0 - NOT WIRED (its
// hull / fluid / engine contexts are not composed from the race's boat
// images): it stops the dynamics frame (world/dynamics.hxx).
inline void race_session_enable( race_session_t& s, race_session_boat_t& b, bool on )
{
    if ( !s.dynamics )
        return;
    race_dynamics_t& d = *s.dynamics;
    std::uint8_t* boat = b.graph.objects[ 0 ].image.data( );
    void* tick = boat + 0x60;
    if ( !b.sections )
    {
        race_unwired( s, "the boat's slot 17 / 18 on a boat whose sections load stopped (no shape) - not run (UNKNOWN)" );
        return;
    }
    if ( d.objects.find( &b.body ) == d.objects.end( ) )
    {
        float pth;
        std::memcpy( &pth, reinterpret_cast< const std::uint8_t* >( &b.body ) + 0xC4, 4 );
        b.collision = collision_object_t{ };
        b.collision.body = concave_body_t{ b.body.body.basis, nullptr, nullptr, &b.body.body, &b.io.compound, pth };
        d.objects[ &b.body ] = &b.collision;
        std::uint8_t* transform = race_session_image( b, 0, 0x50 );
        d.motion[ &b.body.body ] = [ &s, boat, transform ]( const float* basis, const float* origin ) {
            if ( !transform || !boat_motion_set( boat, transform, false, basis, origin ) )
                race_unwired( s, "the boat's setWorldTransform 0x41E460 with no transform image - UNKNOWN, not written" );
        };
        race_step_callback_t cb;
        cb.slot0 = []( float, bool ) {};   // 0x410B30: ret 8
        // Slot 1 tickThunk 0x41E5E0: the boat agent's builder (boat/thunk.hxx) on this boat's images, one persistent
        // boat_thunk_t per boat (the solver's records live in it). The water is the world's query view of VuWater,
        // refreshed per frame (none: UNKNOWN, not stepped); the gravity g_vudynamicsimpl vt+0x1C = the world's.
        // A VuWater wave pointer in the images (hull +0x1DC / +0x1E0, the solver's excluded array - the wake
        // update's, drive.hxx) is a race_wave_token: its index in the built water; any other word is refused
        // (UNKNOWN, `why`).
        b.thunk = std::make_unique< boat_thunk_t >( );
        cb.slot1 = [ &s, &b, &d ]( float dt ) {
            boat_thunk_in_t in;
            std::uint8_t* boat_image = b.graph.objects[ 0 ].image.data( );
            const int hull_k = race_session_ref( b.graph, 0, 0x228 );
            in.boat = boat_image;
            in.hull = race_session_image( b, 0, 0x228 );
            in.solver = race_session_image( b, hull_k, 0x1CC );
            in.transform = race_session_image( b, 0, 0x50 );
            in.engine = &b.engine;
            in.body = &b.body.body;
            if ( !in.hull || !in.solver || !in.transform || !s.world.view )
            {
                race_unwired( s, "the boat's tickThunk 0x41E5E0: no hull / solver / transform image or no VuWater view - the step is UNKNOWN (not run)" );
                return;
            }
            in.fluids = b.meshes.fluids_of( load_word( in.solver, 0 ) );
            boat_thunk_water( in, *s.world.view );
            in.gravity = []( void* u, float* out ) {
                std::memcpy( out, static_cast< race_dynamics_t* >( u )->world.gravity, 12 );
                return true;
            };
            in.gravity_user = &d;
            in.wave_index = [ &s ]( std::uint32_t word ) { return race_wave_of( s.world.water, word ); };
            const std::int32_t excluded = static_cast< std::int32_t >( load_word( in.solver, 0x30 ) );
            const int solver_k = race_session_ref( b.graph, hull_k, 0x1CC );
            const std::uint8_t* array = race_session_image( b, solver_k, 0x34 );
            b.excluded.clear( );
            if ( excluded > 0 && array )
                for ( std::int32_t k = 0; k < excluded; ++k )
                    b.excluded.push_back( load_word( array, 4 * k ) );
            in.solver_excluded = b.excluded.empty( ) ? nullptr : b.excluded.data( );
            if ( !boat_thunk_step( *b.thunk, in, dt ) )
                race_unwired( s, "the boat's tickThunk 0x41E5E0: " + b.thunk->why );
        };
        d.callbacks[ tick ] = cb;
    }
    const bool ok = on ? boat_enable( d.impl, boat[ 0x2A0 ], b.body, tick ) : boat_disable( d.impl, boat[ 0x2A0 ], b.body, tick );
    if ( !ok )
        race_unwired( s, on ? "the boat's slot 17 0x421980 reached a dynamics path not modelled - UNKNOWN"
                            : "the boat's slot 18 0x421940 reached a dynamics path not modelled - UNKNOWN" );
}

// The level's gameInitialize as 0x4BA060 runs it after the boat load (0x4BA322:
// VuEntity::gameInitialize 0x500B10 on the project root): post-order - each
// entity's children first (+0x38 order), then its own vt slot 6, skipped when
// +0x14 bit 2 (already initialised) is set; before that, unless +0x14 bit 3,
// g 0x759080 vt+0x20 (the entity registration - a hook here) and each
// component's vt+0x24 (VuRigidBodyComponent's: a nop). RECREATED: VuPropEntity's
// slot 6 0x546910 (world/bodies.hxx) - the level's static bodies into the race's
// dynamics world (g_vudynamicsimpl slot 15). Every other type's slot 6 is not
// recreated: counted, logged once (UNKNOWN). The collision mesh assets come
// from the pack (boat meshes.hxx's library - its factory substitution; retail's
// asset property creates them at load, 0x545200, here at the build). The
// VuRigidBody ids (the debug global 0x759030) continue from one per boat
// (FLAGGED: the boat ctor's own count is the boat agent's, seeded 0).
// The zones' "Build" tick 0x5A7210 (timer -= dt).
inline void race_session_gfx_tick( void* user, float dt )
{
    gfx_zone_tick( *static_cast< gfx_zone_t* >( user ), dt );
}

// The gfx-settings entities' slot 6 in the level walk: VuGfxSettingsEntity's
// 0x5A71B0 (VuGlobalGfxSettingsEntity: the "Build" tick 0x5A7210 registered
// unless Always Active) and the zones' 0x5D21B0 (the same, then pushed onto
// the blender's list g 0x759488 / 0x75948C). The blend view: the +0x70 record
// = the targets' +0x70 .. +0xC4 words, the timer +0xC8 = 0 (the base ctor), the
// transform component's world / scale. True when `e` is one of the three
// (UNKNOWN - logged, not built - without its state or a known world).
inline bool race_session_gfx_game_initialize( race_session_t& s, entity_t& e )
{
    const bool rect = e.m_type == "VuRectangularGfxSettingsEntity", ellip = e.m_type == "VuEllipticalGfxSettingsEntity";
    if ( !rect && !ellip && e.m_type != "VuGlobalGfxSettingsEntity" )
        return false;
    const transform_component_t* t = entity_transform( e );
    if ( !e.m_state || ( ( rect || ellip ) && ( !t || e.m_world_unknown ) ) )
    {
        race_unwired( s, "a gfx-settings entity without its state / known world - its gameInitialize not run (UNKNOWN)" );
        return true;
    }
    auto z = std::make_unique< gfx_zone_t >( );
    const gfx_settings_t& g = rect || ellip ? static_cast< gfx_zone_state_t* >( e.m_state.get( ) )->value
                                            : static_cast< gfx_settings_state_t* >( e.m_state.get( ) )->value;
    const gfx_zone_state_t* zs = rect || ellip ? static_cast< gfx_zone_state_t* >( e.m_state.get( ) ) : nullptr;
    gfx_zone_view( *z, g, ellip ? gfx_zone_kind_t::elliptical : gfx_zone_kind_t::rectangular, zs ? zs->inner : nullptr, t );
    if ( !z->always_active )
        tick_register( s.ticks, { z.get( ), race_session_gfx_tick, z.get( ) }, "Build" );
    if ( rect || ellip )
        s.blender.zones.push_back( z.get( ) );
    s.gfx_by_path[ race_entity_path( &e ) ] = { z.get( ), !( rect || ellip ) };
    s.gfx_entities.push_back( std::move( z ) );
    return true;
}

// VuParams' float add 0x405120: a {2, value} record (dropped past the 0x80 bytes).
inline void race_dyn_prop_param( std::uint8_t* params, std::int32_t& size, float v )
{
    if ( size + 8 > 0x80 )
        return;
    const std::uint32_t type = 2u;
    std::memcpy( params + size, &type, 4 );
    std::memcpy( params + size + 4, &v, 4 );
    size += 8;
}

// THE DECISION TICK of a dynamic game prop (0x4077E8 -> vt+0x40 slot 16 0x438260), read whole from the disassembly:
// +0x17C += dt (addss dt, +0x17C). A pending hit (+0x164, set by the contact callback 0x437F40 - world/dynprop.hxx
// dynamic_prop_contact): with a target (+0x174) a VuParams {Shake Magnitude, Duration, Falloff, Frequency} (+0x128
// .. +0x134, 0x405120 each) goes to the target's event map (+0xC, 0x5155C0) as FNV-1a "OnCameraShake" (0x685E80),
// then {Rumble Low, High} (+0x138 / +0x13C, the size re-zeroed) as "OnRumble" (0x685E90) - a session boat's map
// holds 0x41E7B0 / 0x41E950 (boat events.hxx boat_on_camera_shake on its camera +0x224, boat_on_rumble on its driver
// +0x220). Then with an Sfx Name (+0x120 != 0) FMOD's getEvent / set3DAttributes(transform +0x80) / "intensity"
// (0x685E9C) = +0x178 / start / release (imports 0x64C348 .. 0x64C358 - the audio PLATFORM, a hook). +0x164 = 0, the
// weak ref +0x168 released (vt+0). A pending splash (+0x181): cleared; createEffect 0x52BCD0 on g 0x75905C by the
// Splash Pfx Name (pfxmgr.hxx pfx_manager_create), the entry's serial bumped (pfx_manager_handle), its instance's
// slot 10 (fstp: discarded); a nonzero handle that looks up (pfx_manager_lookup) -> the instance's slot 0
// setPosition(transform +0x80) and slot 11 start.
inline void race_session_dyn_prop_decision( void* user, float dt )
{
    race_session_t::dyn_prop_t& d = *static_cast< race_session_t::dyn_prop_t* >( user );
    race_session_t& s = *d.session;
    d.timer = sse_add( dt, d.timer );
    if ( d.hit.pending )
    {
        if ( d.hit.target )
        {
            race_session_boat_t* boat = nullptr;
            for ( const auto& b : s.boats )
                if ( reinterpret_cast< const std::uint8_t* >( &b->body.body ) == d.hit.target )
                    boat = b.get( );
            std::uint8_t shake[ 0x84 ] = { }, rumble[ 0x84 ] = { };
            std::int32_t n = 0;
            race_dyn_prop_param( shake, n, d.p->shake_magnitude );
            race_dyn_prop_param( shake, n, d.p->shake_duration );
            race_dyn_prop_param( shake, n, d.p->shake_falloff );
            race_dyn_prop_param( shake, n, d.p->shake_frequency );
            std::memcpy( shake + 0x80, &n, 4 );
            n = 0;
            race_dyn_prop_param( rumble, n, d.p->rumble_low );
            race_dyn_prop_param( rumble, n, d.p->rumble_high );
            std::memcpy( rumble + 0x80, &n, 4 );
            if ( boat && boat->io.camera && boat->driver && !boat->driver->graph.empty( ) )
            {
                boat_on_camera_shake( boat->io.camera, shake );
                std::uint8_t* drv = boat->driver->graph[ 0 ].image.data( );
                boat_on_rumble( drv, load_word( drv, 0 ), rumble );
                LOG_VERBOSE( "race: dynamic prop '%s' hit %s: OnCameraShake / OnRumble (strength %.3f)", d.entity->m_name.c_str( ), boat->name.c_str( ),
                             d.hit.strength );
            }
            else
                race_unwired( s, "a dynamic prop's hit target is no session boat (its event map's OnCameraShake / OnRumble) - UNKNOWN, not run" );
        }
        if ( !d.p->sfx.empty( ) )
            race_unwired( s, "audio: a dynamic prop's hit Sfx (FMOD getEvent / set3DAttributes / intensity / start - platform)" );
        d.hit.pending = 0;
        d.hit.target = nullptr;
    }
    if ( d.live.splash )
    {
        d.live.splash = 0;
        if ( !s.world.pfx || !s.world.pfx_manager || !s.world.pfx_ops )
        {
            race_unwired( s, "a dynamic prop's splash 0x52BCD0 with no VuPfx runtime / manager - UNKNOWN, not run" );
            return;
        }
        pfx_manager_t& m = *s.world.pfx_manager;
        const int e = pfx_manager_create( m, d.p->splash_pfx.c_str( ), *s.world.pfx_ops );
        if ( e < 0 )
            return;
        const boat_pfx_ops_t o = boat_pfx_runtime_ops( *s.world.pfx );
        const std::uint32_t h = pfx_manager_handle( m, e );
        if ( o.duration )
            o.duration( o.user, m.entries[ static_cast< std::size_t >( e ) ].instance );
        if ( h == 0u )
            return;
        const int x = pfx_manager_lookup( m, h );
        const transform_component_t* t = entity_transform( *d.entity );
        if ( x < 0 || !t )
            return;
        const std::uint32_t inst = m.entries[ static_cast< std::size_t >( x ) ].instance;
        o.set_position( o.user, inst, t->world + 12 );
        o.start( o.user, inst );
        ++s.prop_splashes;
        LOG_VERBOSE( "race: dynamic prop '%s' splash '%s' started", d.entity->m_name.c_str( ), d.p->splash_pfx.c_str( ) );
    }
}

// The Build tick 0x546130 (thiscall; dt; retn 4 - dt unread), read whole: matrix = VuMatrix_multiply(
// VuMatrix_fromScale 0x407DB0(transform +0xB0), out, transform +0x50) (transform_draw_matrix) into the static model
// component (+0x6C) +0x13C, then 0x531D10(component, the LOD 0 instance's (+0xCC) scene +0x5C aabb or the zero aabb
// 0x7593F0, +0x13C) - the world's (race_world_t::prop_draw).
inline void race_session_dyn_prop_build( void* user, float )
{
    race_session_t::dyn_prop_t& d = *static_cast< race_session_t::dyn_prop_t* >( user );
    if ( d.session->world.prop_draw )
        d.session->world.prop_draw( *d.entity );
    else
        race_unwired( *d.session, "a dynamic prop's Build tick 0x546130 with no renderer hand-off (prop_draw) - not drawn from its body" );
}

// The mine's hide vt+0x34 0x438220: the step callback out of g_vudynamicsimpl (vt+0x30), then 0x545F20 - shown (+0x8C)
// -> 0, the draw component off the draw manager (the renderer's prop_drawn), the body out of the world when added.
inline void race_session_mine_hide( race_session_t::dyn_prop_t& d )
{
    race_session_t& s = *d.session;
    if ( d.step_added )
    {
        vu_dynamics_remove_step_callback( s.dynamics->impl, &d.step_tag );
        d.step_added = false;
    }
    if ( !d.init.shown )
        return;
    d.init.shown = 0;
    if ( s.world.prop_drawn )
        s.world.prop_drawn( *d.entity, false );
    else
        race_unwired( s, "the mine's hide 0x545F20: no renderer hand-off (prop_drawn) - still drawn" );
    if ( d.body->in_world )
    {
        vu_dynamics_remove_rigid_body( s.dynamics->impl, d.body->body );
        d.body->in_world = false;
    }
}

// The explosion part's trigger 0x547BB0 (thiscall part, const VuMatrix&; retn 4) - the mine's (+0x184) and VuExplosionEntity's
// (+0x64): +0 = the matrix's row 3 (16 bytes), the explosion manager g 0x759034 vt+0x18 = 0x510180 (explosion_add over the
// human listeners) on the part (+0x10 .. +0x30 the nine Explosion floats), the pfx 0x52BCD0 by +0x34's name (its +0x70
// handle, slot +0x28 discarded, then by the handle's lookup slot 8 setMatrix( the matrix ), slot +0x2C start) and FMOD's
// event by +0x4C's name at the row-3 position (getEvent / set3DAttributes / start - platform).
inline void race_session_explode( race_session_t& s, const float* m, const float* explosion9, const std::string& pfx, const std::string& audio,
                                  const char* who )
{
    std::uint8_t params[ 0x38 ] = { };
    std::memcpy( params, m + 12, 16 );
    std::memcpy( params + 0x10, explosion9, 36 );
    s.explosion_hits += explosion_add( params, s.explosion_listeners, hull_sqrt );
    if ( !pfx.empty( ) && s.world.pfx && s.world.pfx_manager && s.world.pfx_ops )
    {
        pfx_manager_t& pm = *s.world.pfx_manager;
        const int e = pfx_manager_create( pm, pfx.c_str( ), *s.world.pfx_ops );
        if ( e >= 0 )
        {
            const boat_pfx_ops_t o = boat_pfx_runtime_ops( *s.world.pfx );
            const std::uint32_t hd = pfx_manager_handle( pm, e );
            if ( o.duration )
                o.duration( o.user, pm.entries[ static_cast< std::size_t >( e ) ].instance );
            const int x = hd ? pfx_manager_lookup( pm, hd ) : -1;
            if ( x >= 0 )
            {
                const std::uint32_t inst = pm.entries[ static_cast< std::size_t >( x ) ].instance;
                o.set_matrix( o.user, inst, m );
                o.start( o.user, inst );
            }
        }
    }
    else if ( !pfx.empty( ) )
        race_unwired( s, std::string( who ) + "'s explosion pfx 0x52BCD0 with no VuPfx runtime / manager - UNKNOWN, not run" );
    if ( !audio.empty( ) )
        race_unwired( s, std::string( "audio: " ) + who + "'s explosion event (FMOD getEvent / set3DAttributes / start - platform)" );
}

// The mine's Decision tick 0x438EA0 (world/mine.hxx mine_tick): the hide; the explosion part 0x547BB0 - its params
// (+0 the position: the transform's row 3, +0x10 .. +0x30 the nine Explosion floats) to the explosion manager's add
// 0x510180 (explosion_add over the human listeners), the pfx 0x52BCD0 by "Explosion Pfx System Name" (slot 10 discarded,
// slot 2 setMatrix( the transform world ), slot 11 start) and FMOD's event (platform); the point wave 0x528E10 on the
// world's VuWater (its returned reference dropped: 2 -> 1); "OnExplode".
inline void race_session_mine_tick( void* user, float )
{
    race_session_t::dyn_prop_t& d = *static_cast< race_session_t::dyn_prop_t* >( user );
    race_session_t& s = *d.session;
    const transform_component_t* t = entity_transform( *d.entity );
    if ( !t || d.entity->m_world_unknown )
    {
        if ( !d.exploded && d.request )
            race_unwired( s, "a mine's tick 0x438EA0 on an entity whose world is unknown - UNKNOWN, not run" );
        return;
    }
    mine_tick_hooks_t h;
    h.hide = [ &d ] { race_session_mine_hide( d ); };
    h.explode = [ &s, &d ]( const float* m ) { race_session_explode( s, m, d.p->explosion, d.p->explosion_pfx, d.p->explosion_audio, "a mine" ); };
    h.point_wave = [ &s ]( const std::uint32_t* desc ) {
        if ( !s.world.water )
        {
            race_unwired( s, "a mine's point wave 0x528E10 with no VuWater - UNKNOWN, not made" );
            return;
        }
        water_create_point_wave( *s.world.water, desc );
        s.world.water->waves.back( ).set( 4, 1 );   // the mine's reference dropped (dec +4: 2 -> 1, not deleted)
    };
    h.on_explode = [ &s, &d ] {
        if ( s.world.fire )
            s.world.fire( *d.entity, "OnExplode" );
    };
    if ( mine_tick( d.exploded, d.request, t->world, d.p->wave, h ) )
    {
        ++s.mine_explosions;
        LOG_INFO( "race: mine '%s' exploded (sync %d) - %d listener hits so far", d.entity->m_name.c_str( ), s.dynamics ? s.dynamics->syncs : -1,
                  s.explosion_hits );
    }
}

// The keyframe motion's hooks (world/keymotion.hxx): the outputs through the world's graph, the tick registration as the
// session holds it (registered once at game init; the tick runs slot 14 only while active, so an unregistered state is
// the inactive one), find = the ref's VuMotionComponent (the target set up at game init), the unrecreated count.
inline key_motion_hooks_t race_session_key_hooks( race_session_t::key_live_t& k )
{
    key_motion_hooks_t h;
    h.user = &k;
    h.fire = []( void* u, key_motion_t&, const char* plug ) {
        auto& x = *static_cast< race_session_t::key_live_t* >( u );
        if ( x.session->world.fire )
            x.session->world.fire( *x.entity, plug );
    };
    h.tick_register = []( void*, key_motion_t&, bool ) { };
    h.find = []( void* u, key_motion_t& ) -> key_motion_target_t* {
        auto& x = *static_cast< race_session_t::key_live_t* >( u );
        return x.target_entity ? &x.target : nullptr;
    };
    h.unrecreated = &k.session->key_unrecreated;
    return h;
}

// VuRigidBodyComponent's kinematic step 0x544C50 (thiscall component; the motion record*; retn 4), which the props' motion
// IF slot 0 (0x546BD0, VuAnimatedPropEntity's 0x5ED4F0) calls after the transform / draw: with a body (+0x100) and a
// record, the body's linear velocity +0x140 = the record's +0x58 .. +0x60 (w 0) and angular +0x150 = +0x68 .. +0x70 (w
// 0); setCenterOfMassTransform 0x50E660 with the record's +0x18 matrix as a btTransform (0x410C30: the basis transposed
// into rows, w 0; the origin row 3, w 0 - boat load.hxx's form); then, when the body is in the world (+0x104), the
// dynamics world's (g 0x75900C vt+0x6C) updateSingleAabb 0x54C370 on it. No body: nothing (retail's test).
inline void rigid_kinematic_step( solver_rigid_t& b, const float* matrix, const float* linear, const float* angular )
{
    for ( int k = 0; k < 3; ++k )
    {
        b.linear_velocity[ k ] = linear[ k ];
        b.angular_velocity[ k ] = angular[ k ];
    }
    b.linear_velocity[ 3 ] = 0.0f;
    b.angular_velocity[ 3 ] = 0.0f;
    float basis[ 12 ], origin[ 4 ];
    for ( int row = 0; row < 3; ++row )
    {
        for ( int k = 0; k < 3; ++k )
            basis[ 4 * row + k ] = matrix[ 4 * k + row ];
        basis[ 4 * row + 3 ] = 0.0f;
    }
    origin[ 0 ] = matrix[ 12 ];
    origin[ 1 ] = matrix[ 13 ];
    origin[ 2 ] = matrix[ 14 ];
    origin[ 3 ] = 0.0f;
    body_set_center_of_mass_transform( b, basis, origin );
}

inline void race_session_kinematic_step( race_session_t& s, level_body_t& lb, const float* matrix, const float* linear, const float* angular )
{
    if ( !lb.built )
        return;
    rigid_kinematic_step( lb.body.body, matrix, linear, angular );
    if ( !lb.in_world || !s.dynamics )
        return;
    const auto it = s.dynamics->objects.find( &lb.body );
    if ( it == s.dynamics->objects.end( ) || !it->second || update_single_aabb( s.dynamics->collision.aabb, *it->second ) < 0 )
        race_unwired( s, "0x544C50's updateSingleAabb 0x54C370 on a moved body: its collision object / the broadphase setAabb hook UNKNOWN" );
}

// The "Motion" tick 0x61A6E0 -> slot 14 0x5CFE60 while active.
inline void race_session_key_tick( void* user, float dt )
{
    auto& k = *static_cast< race_session_t::key_live_t* >( user );
    const int before = k.session->key_unrecreated;
    key_motion_tick( *k.k, dt, race_session_key_hooks( k ) );
    if ( k.session->key_unrecreated != before )
        race_unwired( *k.session, "a keyframe motion reached a path not recreated (Curve Type / the key search below every key) - UNKNOWN" );
}

// A motion target's IF slot 0 after +0x18 / +0x58 / +0x68 were written, by its class: VuAudioEmitterEntity's is 0x5F3D80
// (world/sound.hxx audio_emitter_motion: setWorldTransform, then set3DAttributes on a held event with the translation and the
// record's +0x58 velocity); the props' 0x546BD0 (VuPropEntity / VuBreakablePropEntity, IF 0x73A13C) and VuAnimatedPropEntity's
// 0x5ED4F0 end with the attach component's slot 14 on the same matrix and velocities; any other class's IF is not read (UNKNOWN,
// not run). 0x546BD0 writes the world inline and walks the children with 0x534C00( 0 ): no watcher (entity_set_world's notify is moot here).
inline void race_session_motion_update( race_session_t& rs, entity_t& target, bool& stepped, const float* m, const float* linear, const float* angular )
{
    const std::string& cls = target.m_type;
    if ( cls == "VuAudioEmitterEntity" && target.m_state )
    {
        if ( !entity_set_world( target, m, true, { } ) )
        {
            race_unwired( rs, "an audio emitter motion target whose world is not modelled - setWorldTransform UNKNOWN, not written" );
            return;
        }
        if ( rs.world.sound )
        {
            sound_hooks_t h = *rs.world.sound;
            h.set_world = nullptr;   // done above (the same call, first)
            float record[ 20 ];
            std::memcpy( record, m, 64 );
            std::memcpy( record + 16, linear, 16 );
            audio_emitter_motion( static_cast< audio_emitter_state_t* >( target.m_state.get( ) )->value, record, h );
        }
        return;
    }
    if ( cls != "VuPropEntity" && cls != "VuRampEntity" && cls != "VuBreakablePropEntity" && cls != "VuAnimatedPropEntity" )
    {
        race_unwired( rs, "a motion target of class " + cls + " - its motion IF slot 0 is not read (UNKNOWN, not run)" );
        return;
    }
    if ( !entity_set_world( target, m, false, { } ) )
    {
        race_unwired( rs, "a motion target whose world is not modelled - setWorldTransform UNKNOWN, not written" );
        return;
    }
    if ( rs.world.prop_moved )
        rs.world.prop_moved( target );
    // The rigid component's kinematic step 0x544C50 on the target's body (none built: retail's +0x100 test - except
    // a VuAnimatedPropEntity, whose rigid component no level body models here: UNKNOWN).
    bool body = false;
    for ( const auto& lb : rs.level_bodies )
        if ( lb->owner == &target )
        {
            race_session_kinematic_step( rs, *lb, m, linear, angular );
            if ( !body && !stepped )
                LOG_INFO( "race: keyframe target %s: 0x544C50 %s (in the world %d; sync %d)", target.m_name.c_str( ),
                          lb->built ? "moves its body" : "finds no body (+0x100 null: bodies_create built none) - nothing",
                          lb->in_world ? 1 : 0, rs.dynamics ? rs.dynamics->syncs : -1 );
            body = true;
        }
    stepped = true;
    if ( !body && target.m_type == "VuAnimatedPropEntity" )
        race_unwired( rs, "a VuAnimatedPropEntity motion target's rigid component (no level body modelled) - 0x544C50 UNKNOWN, not run" );
    if ( rs.world.attach_update )
        rs.world.attach_update( target, m, linear, angular );
}

// VuKeyframeMotionEntity's slot 6 0x5CE9E0 (key_motion_game_init): its VuKeyframeEntity children in order (Time, the
// transform world), the "Entity" ref (its script component's ref) -> the target entity's VuMotionComponent (vtable
// 0x74E984; 0x5F5810 answers null without one). The target's IF (VuAnimatedPropEntity's 0x756228 / the props' 0x73A13C
// shape): slot 0 = race_session_motion_update; slots 1 / 2 (0x546D80 / 0x546DB0: the rigid component's +0xD0
// bit 1 set / cleared and copied to its body) - no body is built for an animated prop here (UNKNOWN when one is).
inline void race_session_key_motion( race_session_t& s, entity_t& e )
{
    key_motion_state_t* st = static_cast< key_motion_state_t* >( e.m_state.get( ) );
    auto k = std::make_unique< race_session_t::key_live_t >( );
    k->entity = &e;
    k->k = &st->value;
    k->session = &s;
    bool linked = false;
    if ( const script_component_t* sc = script_of( e ) )
        for ( const script_ref_t& ref : sc->m_refs )
            if ( std::string( ref.m_row->m_name ) == "Entity" && ref.m_target )
            {
                linked = true;
                for ( const entity_component_t& c : ref.m_target->m_components )
                    if ( c.m_vtable == 0x74E984u )
                        k->target_entity = ref.m_target;
            }
    if ( k->target_entity )
    {
        const transform_component_t* tt = entity_transform( *k->target_entity );
        k->target.transform = tt ? tt->world : nullptr;
        k->target.user = k.get( );
        k->target.update = []( void* u, const float* m, const float* linear, const float* angular ) {
            auto& x = *static_cast< race_session_t::key_live_t* >( u );
            race_session_motion_update( *x.session, *x.target_entity, x.stepped, m, linear, angular );
        };
    }
    std::vector< key_motion_child_t > kids;
    for ( auto& c : e.m_children )
    {
        key_motion_child_t kc;
        kc.keyframe = c->m_type == "VuKeyframeEntity";
        if ( kc.keyframe && c->m_state )
        {
            const float t = static_cast< keyframe_state_t* >( c->m_state.get( ) )->time;
            std::memcpy( &kc.time, &t, 4 );
        }
        if ( const transform_component_t* ct = entity_transform( *c ) )
            std::memcpy( kc.transform, ct->world, 64 );
        kids.push_back( kc );
    }
    race_session_t::key_live_t* kp = k.get( );
    s.key_motions.push_back( std::move( k ) );
    tick_register( s.ticks, { kp, race_session_key_tick, kp }, "Motion" );
    key_motion_game_init( *kp->k, kids, linked, race_session_key_hooks( *kp ) );
}

// VuControlEntity's hooks (world/keyframe.hxx): the outputs through the world's graph; the tick manager g 0x759008's vt+0x20
// ({0x7529F8, e, 0x5CD9F0}, "Motion") / vt+0x28 (slot 10 0x4F7590: every phase's handlers of this owner) on the session's
// tick manager; vt+0x3C = slot 15 0x4F6EC0 (fld [ecx+0x28]: this frame's capped step).
inline void race_session_control_tick( void* user, float dt );
inline control_hooks_t race_session_control_hooks( race_session_t::control_live_t& k )
{
    control_hooks_t h;
    h.user = &k;
    h.fire = []( void* u, control_entity_t&, const char* plug ) {
        auto& x = *static_cast< race_session_t::control_live_t* >( u );
        if ( x.session->world.fire )
            x.session->world.fire( *x.entity, plug );
    };
    h.tick_register = []( void* u, control_entity_t&, bool on ) {
        auto& x = *static_cast< race_session_t::control_live_t* >( u );
        if ( on )
            tick_register( x.session->ticks, { &x, race_session_control_tick, &x }, "Motion" );
        else
            tick_unregister_all( x.session->ticks, &x );
    };
    h.real_dt = []( void* u ) { return static_cast< race_session_t::control_live_t* >( u )->session->ticks.dt; };
    h.unrecreated = &k.session->control_unrecreated;
    return h;
}

// The "Motion" tick 0x5CD9F0.
inline void race_session_control_tick( void* user, float dt )
{
    auto& k = *static_cast< race_session_t::control_live_t* >( user );
    const int before = k.session->control_unrecreated;
    const bool was = k.c->active != 0;
    control_tick( *k.c, dt, race_session_control_hooks( k ) );
    if ( was && !k.c->active && k.comp )
        LOG_INFO( "race: control '%s' deactivated after %d changes (clock %.3f of %.3f): Max Height %.3f", k.entity->m_name.c_str( ), k.changes,
                  control_f( k.c->time ), control_f( k.c->duration ), control_f( k.comp->records[ 2 ] ) );
    if ( k.session->control_unrecreated != before )
        race_unwired( *k.session, "a VuControlEntity reached a path not recreated (a keyframe short of the records / no key below the clock / "
                                  "Deactivate without a target) - UNKNOWN" );
}

// VuControlEntity's slot 6 0x5CD490 (control_game_init): the "Entity" ref's entity's VuControlComponent (race.hxx
// control_component_of, bound at the post-load). Its interface (the owner's +0x5C VuControlComponentIF): slot 0 for a
// VuDirectionalWaveEntity = 0x5D9D90 (watchers.hxx water_directional_control: records 0..4 +8 to +0x6C .. +0x7C, then
// 0x5D9DD0 on VuWater's wave); slots 1 / 2 the nullsub 0x401560 (begin / end: nothing). Any other owner's slot 0 is not
// read (UNKNOWN, logged; control_component_of binds none for it).
inline void race_session_control( race_session_t& s, entity_t& e )
{
    auto k = std::make_unique< race_session_t::control_live_t >( );
    k->entity = &e;
    k->c = &static_cast< control_entity_state_t* >( e.m_state.get( ) )->value;
    k->session = &s;
    entity_t* t = control_ref_target( e );
    bool unknown = false;
    if ( t )
        k->comp = control_component_of( *t, unknown );
    if ( unknown )
        race_unwired( s, "a VuControlEntity's target " + t->m_type + " - its VuControlComponent's records / IF slot 0 not read (UNKNOWN, bound to none)" );
    if ( k->comp )
    {
        k->target_entity = t;
        k->comp->user = k.get( );
        k->comp->begin = nullptr;   // 0x401560
        k->comp->end = nullptr;     // 0x401560
        k->comp->changed = []( void* u, control_component_t& c ) {
            auto& x = *static_cast< race_session_t::control_live_t* >( u );
            race_session_t& rs = *x.session;
            ++x.changes;
            water_entity_state_t* ws = static_cast< water_entity_state_t* >( x.target_entity->m_state.get( ) );
            const transform_component_t* tt = entity_transform( *x.target_entity );
            if ( !rs.world.water || !ws || !tt )
            {
                race_unwired( rs, "a VuControlEntity's change on a VuDirectionalWaveEntity with no VuWater / state / transform - 0x5D9D90 not run" );
                return;
            }
            const water_built_t::entity_ref_t* r = water_entity_of( *rs.world.water, *x.target_entity );
            water_directional_control( *rs.world.water, *ws, r ? r->wave : -1, *tt, c.records.data( ), x.layout );
            if ( x.changes == 1 )
                LOG_INFO( "race: control '%s' drives '%s' (wave %d): Max Height %.3f", x.entity->m_name.c_str( ), x.target_entity->m_name.c_str( ),
                          r ? r->wave : -1, control_f( c.records[ 2 ] ) );
        };
    }
    race_session_t::control_live_t* kp = k.get( );
    s.controls.push_back( std::move( k ) );
    control_game_init( *kp->c, t != nullptr, kp->comp, race_session_control_hooks( *kp ) );
}

// VuBreakablePropEntity's Decision tick 0x434A20 (dt), read whole (world/breakable.hxx's head): the breakable
// component's 0x543640(dt) - its debris fade timer (+0xE8, state +0xE4 1 -> 2 below 0, the debris draw removed):
// world/debris.hxx. With a request (+0xD0): unless the component's state is already non-zero - 0x546A20 (the static draw off,
// the body out of the world when added), the debris start (0x51E790, state 1, the draw add);
// with a target: "OnBreak" (its entity parameter not carried by our fire), "OnCameraShake" {Magnitude, Duration,
// Falloff, Frequency} and "OnRumble" {Low, High} to the target's event map (0x501160; a session boat's: boat events.hxx);
// the pfx by Pfx Name (0x52BCD0; slot 10 discarded; slot 2 setMatrix the transform world, slot 3 the kept velocity
// +0xD4 (its 4th word UNKNOWN: 0 here), slot 11 start); FMOD by Sfx Name (platform). Then +0xD0 = 0 and the weak ref
// released either way.
inline void race_session_debris_bind( race_session_t& s, const entity_t& e, race_debris_t& d )
{
    if ( d.m_bound || !s.world.debris_model )
        return;
    for ( const entity_component_t& c : e.m_components )
        if ( c.m_vtable == k_breakable_model_component_vtable && c.m_state )
        {
            const breakable_draw_t& props = static_cast< const breakable_draw_state_t* >( c.m_state.get( ) )->value;
            if ( const gfx_scene_view_t* v = props.model_asset.empty( ) ? nullptr : s.world.debris_model( props.model_asset ) )
                d.bind( props, *v );
        }
}

// The start's +0xEC: the static model component's colour +0xA4.
inline void race_session_debris_start( race_session_t& s, entity_t& e, race_debris_t& d, const float* velocity )
{
    std::uint32_t colour = 0xFFFFFFFFu;
    for ( const entity_component_t& c : e.m_components )
        if ( c.m_vtable == k_static_model_component_vtable && c.m_state )
            colour = static_cast< const model_draw_state_t* >( c.m_state.get( ) )->value.color;
    const transform_component_t* t = entity_transform( e );
    if ( t )
    {
        d.start( t->world, velocity, colour, s.random );
        LOG_INFO( "race: debris of %s: %d pieces, life %.2f", e.m_name.c_str( ), static_cast< int >( d.m_parts.size( ) ), d.m_draw.m_life );
    }
    else
    {
        race_unwired( s, "a breakable with no transform: its debris start 0x51E790 - UNKNOWN, not run" );
        d.m_state = 1;
    }
}

// 0x434D90, "Build": 0x5436B0.
inline void race_session_breakable_build( void* user, float dt )
{
    static_cast< race_session_t::breakable_live_t* >( user )->debris.build( dt );
}

inline void race_session_breakable_tick( void* user, float dt )
{
    race_session_t::breakable_live_t& b = *static_cast< race_session_t::breakable_live_t* >( user );
    race_session_t& s = *b.session;
    race_session_debris_bind( s, *b.entity, b.debris );
    b.debris.decision( dt );   // 0x543640
    if ( !b.request )
        return;
    if ( b.debris.m_state == 0 )
    {
        if ( s.world.prop_drawn )
            s.world.prop_drawn( *b.entity, false );
        if ( b.body->in_world )
        {
            vu_dynamics_remove_rigid_body( s.dynamics->impl, b.body->body );
            b.body->in_world = false;
        }
        race_session_debris_start( s, *b.entity, b.debris, b.velocity );
        ++s.breaks;
        if ( b.target )
        {
            if ( s.world.fire )
                s.world.fire( *b.entity, "OnBreak" );
            race_session_boat_t* boat = nullptr;
            for ( const auto& x : s.boats )
                if ( &x->body.body == b.target )
                    boat = x.get( );
            if ( boat && boat->io.camera && boat->driver && !boat->driver->graph.empty( ) )
            {
                std::uint8_t shake[ 0x84 ] = { }, rumble[ 0x84 ] = { };
                std::int32_t n = 0;
                for ( int k = 0; k < 4; ++k )
                    race_dyn_prop_param( shake, n, b.p->shake[ k ] );
                std::memcpy( shake + 0x80, &n, 4 );
                n = 0;
                race_dyn_prop_param( rumble, n, b.p->rumble[ 0 ] );
                race_dyn_prop_param( rumble, n, b.p->rumble[ 1 ] );
                std::memcpy( rumble + 0x80, &n, 4 );
                boat_on_camera_shake( boat->io.camera, shake );
                std::uint8_t* drv = boat->driver->graph[ 0 ].image.data( );
                boat_on_rumble( drv, load_word( drv, 0 ), rumble );
            }
        }
        const transform_component_t* t = entity_transform( *b.entity );
        if ( !b.p->pfx.empty( ) && s.world.pfx && s.world.pfx_manager && s.world.pfx_ops && t )
        {
            pfx_manager_t& pm = *s.world.pfx_manager;
            const int e = pfx_manager_create( pm, b.p->pfx.c_str( ), *s.world.pfx_ops );
            if ( e >= 0 )
            {
                const boat_pfx_ops_t o = boat_pfx_runtime_ops( *s.world.pfx );
                const std::uint32_t hd = pfx_manager_handle( pm, e );
                if ( o.duration )
                    o.duration( o.user, pm.entries[ static_cast< std::size_t >( e ) ].instance );
                const int x = hd ? pfx_manager_lookup( pm, hd ) : -1;
                if ( x >= 0 )
                {
                    const std::uint32_t inst = pm.entries[ static_cast< std::size_t >( x ) ].instance;
                    std::uint32_t v[ 4 ] = { };
                    std::memcpy( v, b.velocity, 12 );
                    o.set_matrix( o.user, inst, t->world );
                    o.set_velocity( o.user, inst, v );
                    o.start( o.user, inst );
                }
            }
        }
        if ( !b.p->sfx.empty( ) )
            race_unwired( s, "audio: a breakable's Sfx (FMOD getEvent / set3DAttributes / start - platform)" );
        LOG_INFO( "race: breakable '%s' broke (sync %d)%s", b.entity->m_name.c_str( ), s.dynamics ? s.dynamics->syncs : -1, b.target ? " - by a boat" : "" );
    }
    b.request = 0;
    b.target = nullptr;
}

// VuBreakableDynamicGamePropEntity's Decision tick (vt+0x40 slot 16 0x433890, dt), read whole: the breakable component's
// (+0x184) 0x543640(dt) - its debris fade (world/debris.hxx). With
// a request (+0x164): unless the component's state (+0xE4) is already set - the hide vt+0x34 (slot 13 0x438220:
// race_session_mine_hide - the step callback out, the draw off, the body out of the world), the debris start 0x51E790,
// state 1, the draw add; "OnBreak" with the weak ref's object as its entity parameter
// (0x505150 - not carried by our fire); the pfx by Breakable Pfx Name (+0x188: 0x52BCD0 on g 0x75905C, the serial bump,
// slot 10 discarded, then lookup -> slot 2 setMatrix(the transform world), slot 3 the kept velocity +0x1A0, slot 11
// start). Then +0x164 = 0 and the weak ref released either way; then the base Decision 0x438260 (+0x164 is 0 by now).
inline void race_session_breakable_dynamic_tick( void* user, float dt )
{
    race_session_t::dyn_prop_t& d = *static_cast< race_session_t::dyn_prop_t* >( user );
    race_session_t& s = *d.session;
    race_session_debris_bind( s, *d.entity, d.debris );
    d.debris.decision( dt );   // 0x543640
    if ( d.break_request )
    {
        if ( d.debris.m_state == 0 )
        {
            race_session_mine_hide( d );
            race_session_debris_start( s, *d.entity, d.debris, d.break_velocity );
            ++s.breaks;
            if ( s.world.fire )
                s.world.fire( *d.entity, "OnBreak" );
            const transform_component_t* t = entity_transform( *d.entity );
            if ( s.world.pfx && s.world.pfx_manager && s.world.pfx_ops && t )
            {
                pfx_manager_t& pm = *s.world.pfx_manager;
                const int e = pfx_manager_create( pm, d.p->breakable_pfx.c_str( ), *s.world.pfx_ops );
                if ( e >= 0 )
                {
                    const boat_pfx_ops_t o = boat_pfx_runtime_ops( *s.world.pfx );
                    const std::uint32_t hd = pfx_manager_handle( pm, e );
                    if ( o.duration )
                        o.duration( o.user, pm.entries[ static_cast< std::size_t >( e ) ].instance );
                    const int x = hd ? pfx_manager_lookup( pm, hd ) : -1;
                    if ( x >= 0 )
                    {
                        const std::uint32_t inst = pm.entries[ static_cast< std::size_t >( x ) ].instance;
                        std::uint32_t v[ 4 ] = { };
                        std::memcpy( v, d.break_velocity, 12 );
                        o.set_matrix( o.user, inst, t->world );
                        o.set_velocity( o.user, inst, v );
                        o.start( o.user, inst );
                    }
                }
            }
            else
                race_unwired( s, "a breakable dynamic prop's pfx 0x52BCD0 with no VuPfx runtime / manager - UNKNOWN, not run" );
            LOG_INFO( "race: breakable dynamic prop '%s' broke (sync %d)%s", d.entity->m_name.c_str( ), s.dynamics ? s.dynamics->syncs : -1,
                      d.break_target ? " - by a body" : "" );
        }
        d.break_request = 0;
        d.break_target = nullptr;
    }
    race_session_dyn_prop_decision( user, dt );
}

// VuBreakableDynamicGamePropEntity's Build 0x433AE0: 0x5436B0, then the base 0x546130.
inline void race_session_breakable_dynamic_build( void* user, float dt )
{
    static_cast< race_session_t::dyn_prop_t* >( user )->debris.build( dt );
    race_session_dyn_prop_build( user, dt );
}

// The debris live this frame into the sort: the draw manager's walk reaches these through 0x543790.
inline void race_session_debris_draw( race_session_t& s, gfx_sort_t& sort, const gfx_draw_params_t& p )
{
    for ( const auto& b : s.breakables )
        b->debris.draw( sort, p );
    for ( const auto& d : s.dyn_props )
        d->debris.draw( sort, p );
}

// One dynamic game prop's gameInitialize (world/dynprop.hxx, verified by the cases dynamic_prop_bodies /
// dynamic_prop_step) made live: postLoad 0x5459F0's component step (retail runs it at load; nothing reads the
// component in between), 0x437580, the body into the dynamics world (vt+0x3C), the motion state's
// setWorldTransform (vt+8 0x545CF0: world = translation(-centre) x 0x410B40(the body) -> the transform's
// setWorldTransform steps, entity_set_world; its last call, the +0x68 attach component's vt+0x38, is a hook not
// run), the step callback (vt+0x2C: slot 0 0x410B30 a ret 8, slot 1 0x437730 with the world's water) and the
// Decision tick. The show's anchor (+0x144 = the world's translation, +0x154 = 0x531000 of it). The Build tick
// 0x546130 (the draw matrix for the renderer) is not registered: the renderer is not fed from it (a hook).
inline bool race_session_dynamic_prop( race_session_t& s, entity_t& e, const bodies_hooks_t& hooks, std::uint32_t& id )
{
    race_dynamics_t& dyn = *s.dynamics;
    rigid_component_state_t* rs = nullptr;
    for ( entity_component_t& c : e.m_components )
        if ( c.m_vtable == k_rigid_body_component_vtable )
            rs = static_cast< rigid_component_state_t* >( c.m_state.get( ) );
    transform_component_t* t = entity_transform( e );
    if ( !rs || !t || !e.m_state || e.m_world_unknown )
    {
        race_unwired( s, "a dynamic game prop without its component state / known world - gameInitialize 0x437580 not run (UNKNOWN)" );
        return false;
    }
    auto d = std::make_unique< race_session_t::dyn_prop_t >( );
    d->entity = &e;
    d->session = &s;
    d->p = &static_cast< dynamic_prop_state_t* >( e.m_state.get( ) )->value;
    d->buoy = static_cast< dynamic_prop_state_t* >( e.m_state.get( ) )->buoy;
    d->mine = static_cast< dynamic_prop_state_t* >( e.m_state.get( ) )->mine;
    d->breakable = static_cast< dynamic_prop_state_t* >( e.m_state.get( ) )->breakable;
    if ( d->breakable )
    {
        // The ctor 0x433160's tail: the rigid component (+0x70) +0xF4 = the +0xB4 part (vtable 0x739EF0: the contact
        // callback 0x4335A0) and +0xD8 |= 1 (no body yet).
        rs->value.word_f4 = 1;
        rs->value.flags_d8 |= 1u;
    }
    if ( d->mine )
        d->p->hard_hit = 0;   // the mine's slot 6 0x438CF0 begins with +0x108 = 0, before 0x437580
    d->c = &rs->value;
    dynamic_prop_post_load( *d->p, *d->c );
    const collision_mesh_asset_t* asset = nullptr;
    if ( ( d->c->collision_type == 6 || d->c->collision_type == 7 ) && !d->c->mesh_asset.empty( ) &&
         boat_mesh_exists( &s.level_meshes, "VuCollisionMeshAsset", d->c->mesh_asset ) )
        asset = s.level_meshes.collision_of( boat_mesh_create( &s.level_meshes, "VuCollisionMeshAsset", d->c->mesh_asset ) );
    auto b = std::make_unique< level_body_t >( );
    b->name = e.m_name;
    d->body = b.get( );
    std::string why;
    if ( !dynamic_prop_game_initialize( *b, *d->c, *d->p, *t, asset, id, hooks, d->motion, d->init, why ) )
    {
        race_unwired( s, e.m_type + " '" + e.m_name + "': " + why + " (UNKNOWN, no body)" );
        return false;
    }
    std::memcpy( d->live.anchor, t->world + 12, 12 );
    quat_from_rows( d->live.anchor_q, t->world );
    level_body_t* lb = b.get( );
    race_session_t::dyn_prop_t* dp = d.get( );
    // The mine's rest of 0x438CF0 (world/mine.hxx): retail sets group 0x80 on the body and its broadphase proxy after
    // 0x437580 added it; here it is written before the add below, which builds the proxy from the body's +0x2CA - the
    // same end state (ORDER OURS, flagged).
    if ( d->mine )
        mine_game_initialize( *d->c, reinterpret_cast< std::uint8_t* >( &lb->body ), nullptr );
    if ( d->init.added )
    {
        dyn.objects[ &lb->body ] = &lb->collision;
        if ( !vu_dynamics_add_rigid_body( dyn.impl, lb->body ) )
            race_unwired( s, "addRigidBody (g_vudynamicsimpl vt+0x3C) refused a dynamic prop's body" );
    }
    dyn.motion[ &lb->body.body ] = [ &s, dp ]( const float* basis, const float* origin ) {
        float m[ 16 ], tr[ 16 ], world[ 16 ];
        for ( int row = 0; row < 3; ++row )
        {
            for ( int k = 0; k < 3; ++k )
                m[ 4 * row + k ] = basis[ 4 * k + row ];
            m[ 4 * row + 3 ] = 0.0f;
        }
        std::memcpy( m + 12, origin, 12 );
        m[ 15 ] = 1.0f;
        vu_identity( tr );
        tr[ 12 ] = dp_neg( dp->c->centre[ 0 ] );
        tr[ 13 ] = dp_neg( dp->c->centre[ 1 ] );
        tr[ 14 ] = dp_neg( dp->c->centre[ 2 ] );
        hull_multiply( tr, world, m );
        if ( !entity_set_world( *dp->entity, world, false, { } ) )
            race_unwired( s, "a dynamic prop's setWorldTransform 0x545CF0 on an entity whose world is not modelled - UNKNOWN, not written" );
        else if ( s.world.attach_update )   // 0x545E5B .. 0x545EA5: its attach component's slot 14 with zero velocities
        {
            const float zero[ 4 ] = { };
            if ( const transform_component_t* t = entity_transform( *dp->entity ) )
                s.world.attach_update( *dp->entity, t->world, zero, zero );
        }
    };
    if ( d->init.step_callback )
    {
        void* tag = &dp->step_tag;
        race_step_callback_t cb;
        cb.slot0 = []( float, bool ) {};   // 0x410B30: ret 8
        cb.slot1 = [ &s, dp ]( float ) {
            const dynamic_prop_water_fn water = [ &s ]( const float* point, water_vertex_t& out ) {
                out = water_vertex_t{ };
                return s.world.view && water_point_height( s.world.view->water, point, water_view_kernel, const_cast< water_view_t* >( s.world.view ), out );
            };
            if ( !dynamic_prop_step( *dp->p, dp->live, dp->c->centre, reinterpret_cast< std::uint8_t* >( &dp->body->body ), water ) )
                race_unwired( s, "a dynamic prop's step callback 0x437730: the water query 0x529140 reached a path not recreated / no VuWater view - its forces UNKNOWN" );
        };
        dyn.callbacks[ tag ] = cb;
        if ( !vu_dynamics_add_step_callback( dyn.impl, tag ) )
            race_unwired( s, "g_vudynamicsimpl vt+0x2C refused a dynamic prop's step callback" );
        else
            dp->step_added = true;
    }
    // The Decision delegate (0x4077E8 -> vt+0x40) reaches the MINE's 0x438EA0 for a mine; the mine then registers it
    // once more itself (0x438D73) - two registrations when 0x437580 made one.
    if ( d->init.decision_tick )
        tick_register( s.ticks, { dp, d->mine ? race_session_mine_tick : d->breakable ? race_session_breakable_dynamic_tick : race_session_dyn_prop_decision, dp },
                       "Decision" );
    if ( d->mine )
        tick_register( s.ticks, { dp, race_session_mine_tick, dp }, "Decision" );
    if ( d->init.build_tick )
        tick_register( s.ticks, { dp, d->breakable ? race_session_breakable_dynamic_build : race_session_dyn_prop_build, dp }, "Build" );
    s.level_bodies.push_back( std::move( b ) );
    s.dyn_props.push_back( std::move( d ) );
    return true;
}

inline bool race_session_powerup( race_session_t& s, entity_t& e, std::uint32_t& id, const c_archive& archive );   // world/pickups.hxx

inline void race_session_level_bodies( race_session_t& s, entity_t& tree, const c_archive& archive )
{
    if ( !s.dynamics )
        return;
    race_dynamics_t& d = *s.dynamics;
    s.level_meshes.read_user = const_cast< c_archive* >( &archive );
    s.level_meshes.read = []( void* u, const std::string& asset, std::vector< std::uint8_t >& blob ) {
        return static_cast< const c_archive* >( u )->read( asset.c_str( ), blob );
    };
    s.level_meshes.surfaces = s.surfaces_built ? &s.surfaces : nullptr;
    bodies_hooks_t hooks;
    hooks.user = &s;
    hooks.surface = []( void* u, const std::string& n ) -> std::uint8_t {
        race_session_t& r = *static_cast< race_session_t* >( u );
        return r.surfaces_built ? surface_id( r.surfaces, n.c_str( ) ) : 0;
    };
    hooks.gravity = []( void* u, float* g ) { std::memcpy( g, static_cast< race_session_t* >( u )->dynamics->world.gravity, 12 ); };
    std::uint32_t id = static_cast< std::uint32_t >( s.boats.size( ) );
    std::map< std::string, int > unknown;
    int added = 0, dynamic = 0;
    const auto walk = [ & ]( const auto& self, entity_t& e ) -> void
    {
        for ( auto& c : e.m_children )
            self( self, *c );
        if ( race_session_gfx_game_initialize( s, e ) )
            return;
        if ( race_session_powerup( s, e, id, archive ) )
            return;
        if ( dynamic_prop_class( e.m_type ) )   // the mine's slot 6 0x438CF0 included (race_session_dynamic_prop)
        {
            if ( race_session_dynamic_prop( s, e, hooks, id ) )
                ++dynamic;
            return;
        }
        // The logic entities' slot 6 (world/logic.hxx): VuSwitchEntity's 0x5E24E0 (state = Initial State); every other
        // logic class's slot 6 is 0x401560 (a bare ret - read from each vtable's +0x18).
        if ( logic_state_t* ls = logic_state_of( e ) )
        {
            if ( e.m_type == "VuSwitchEntity" )
                logic_switch_init( ls->value );
            return;
        }
        // VuForceAffectorEntity's slot 6 0x4398C0 (world/force.hxx): Initially Active -> "Activate" 0x439CB0.
        if ( e.m_type == "VuForceAffectorEntity" && e.m_state )
        {
            const transform_component_t* t = entity_transform( e );
            if ( !t || e.m_world_unknown )
            {
                race_unwired( s, "a VuForceAffectorEntity without a known world - gameInitialize 0x4398C0 not run (UNKNOWN)" );
                return;
            }
            auto f = std::make_unique< force_live_t >( );
            f->entity = &e;
            f->value = &static_cast< force_affector_state_t* >( e.m_state.get( ) )->value;
            f->world = t->world;
            f->scale = t->scale;
            f->dyn = &d;
            race_force_bind( *f );
            force_affector_game_init( *f->value, f->world, f->scale, f->hooks );
            if ( f->failed )
                race_unwired( s, "VuForceAffectorEntity '" + e.m_name + "': the ghost's addCollisionObject / step callback refused" );
            s.forces.push_back( std::move( f ) );
            return;
        }
        // VuLensWaterEmitterEntity's slot 6 0x5E6720 (world/lens.hxx): its emitter IF to the lens-water manager.
        if ( e.m_type == "VuLensWaterEmitterEntity" )
        {
            lens_water_hooks_t lh;
            lh.user = &s;
            lh.add = []( void* u, const void* emitter ) { static_cast< race_session_t* >( u )->lens_emitters.push_back( emitter ); };
            lens_water_emitter_start( &e, lh );
            return;
        }
        // c_world's, when the level bound (world/joints.hxx, motion.hxx): FLAGGED, at the end of the load, not in this walk.
        if ( e.m_type == "VuAttachEntity" || e.m_type == "VuRotateMotionEntity" )
            return;
        if ( e.m_type == "VuKeyframeMotionEntity" && e.m_state )
        {
            race_session_key_motion( s, e );
            return;
        }
        if ( e.m_type == "VuControlEntity" && e.m_state )
        {
            race_session_control( s, e );
            return;
        }
        // VuControlKeyframeEntity's slot 6 is VuEntity's (its vtable 0x7528AC +0x18 = 0x401560, a bare ret).
        if ( e.m_type == "VuControlKeyframeEntity" )
            return;
        // VuAudioEmitterEntity's slot 6 0x5F3B30 (world/sound.hxx audio_emitter_game_init: plays when Initially Active).
        if ( e.m_type == "VuAudioEmitterEntity" && e.m_state )
        {
            const transform_component_t* et = entity_transform( e );
            if ( !s.world.sound || !et || e.m_world_unknown )
                race_unwired( s, "a VuAudioEmitterEntity's gameInitialize 0x5F3B30 with no audio platform / known world - not run" );
            else
                audio_emitter_game_init( static_cast< audio_emitter_state_t* >( e.m_state.get( ) )->value, et->world + 12, *s.world.sound );
            return;
        }
        const bool breakable = e.m_type == "VuBreakablePropEntity";
        const bool ramp = e.m_type == "VuRampEntity";    // slot 6 0x43E3F0: 0x546910, then +0xD8 |= 0x10 (below)
        if ( e.m_type != "VuPropEntity" && !breakable && !ramp )
        {
            ++unknown[ e.m_type.empty( ) ? std::string( "(UNKNOWN node)" ) : e.m_type ];
            return;
        }
        rigid_component_state_t* rs = nullptr;
        for ( entity_component_t& c : e.m_components )
            if ( c.m_vtable == k_rigid_body_component_vtable )
                rs = static_cast< rigid_component_state_t* >( c.m_state.get( ) );
        const transform_component_t* t = entity_transform( e );
        if ( !rs || !t || !e.m_state || e.m_world_unknown )
        {
            race_unwired( s, "a VuPropEntity without its component state / known world - its gameInitialize 0x546910 not run (UNKNOWN)" );
            return;
        }
        rigid_component_t& c = rs->value;
        const collision_mesh_asset_t* asset = nullptr;
        if ( !c.mesh_asset.empty( ) && boat_mesh_exists( &s.level_meshes, "VuCollisionMeshAsset", c.mesh_asset ) )
            asset = s.level_meshes.collision_of( boat_mesh_create( &s.level_meshes, "VuCollisionMeshAsset", c.mesh_asset ) );
        auto b = std::make_unique< level_body_t >( );
        b->name = e.m_name;
        b->owner = &e;
        level_body_t* bp = b.get( );
        std::string why;
        const std::uint8_t visible = breakable ? static_cast< breakable_state_t* >( e.m_state.get( ) )->value.visible
                                               : static_cast< prop_entity_state_t* >( e.m_state.get( ) )->visible;
        // The breakable's ctor 0x433F80 ends with the rigid component's +0xF4 = its +0x7C part (the contact callback) and
        // +0xD8 |= 1 - component state the body is built from.
        if ( breakable )
        {
            c.word_f4 = 1;
            c.flags_d8 |= 1u;
        }
        // VuRampEntity's create 0x43E340: the rigid component's +0xF4 = its +0x7C part (contact callback 0x43E420).
        if ( ramp )
            c.word_f4 = 1;
        const bool ok = bodies_prop_game_initialize( *bp, c, visible, asset, t->world, t->scale, id, hooks, [] { /* g 0x7590B8 vt+0x28: the renderer */ },
            [ & ]( level_body_t& lb ) {
                d.objects[ &lb.body ] = &lb.collision;
                return vu_dynamics_add_rigid_body( d.impl, lb.body );
            },
            why );
        if ( !ok )
            race_unwired( s, "VuPropEntity '" + e.m_name + "': " + why + " (UNKNOWN, no body)" );
        if ( ramp && ok )
        {
            c.flags_d8 |= 0x10u;
            body_word( bp->body, 0x2D0 ) = c.flags_d8;
        }
        if ( ramp )    // Ours (a log line, no retail function): a ramp that got no body is the failure race_unwired names above.
            LOG_INFO( "race: ramp '%s' (%s, %s): body %s", e.m_name.c_str( ), c.mesh_asset.c_str( ), asset ? "asset loaded" : "NO ASSET",
                      !ok ? "NOT made" : bp->in_world ? "made, in the world" : bp->built ? "made, not in the world (Initially Visible off)" : "not built" );
        if ( bp->in_world )
            ++added;
        // VuBreakablePropEntity's slot 6 0x434620: 0x546910 above, then the "Decision" tick 0x434A20 and the "Build" tick
        // 0x434D90 (0x5436B0, world/debris.hxx).
        if ( breakable && ok )
        {
            auto br = std::make_unique< race_session_t::breakable_live_t >( );
            br->entity = &e;
            br->p = &static_cast< breakable_state_t* >( e.m_state.get( ) )->value;
            br->c = &c;
            br->body = bp;
            br->session = &s;
            tick_register( s.ticks, { br.get( ), race_session_breakable_tick, br.get( ) }, "Decision" );
            tick_register( s.ticks, { br.get( ), race_session_breakable_build, br.get( ) }, "Build" );
            s.breakables.push_back( std::move( br ) );
        }
        s.level_bodies.push_back( std::move( b ) );
    };
    walk( walk, tree );
    LOG_INFO( "race: the level's gameInitialize: %d static bodies added to the dynamics world (VuPropEntity 0x546910), %d dynamic game props live (0x437580), %d lens-water emitters (0x5E6720)", added, dynamic,
              static_cast< int >( s.lens_emitters.size( ) ) );
    std::string rest;
    for ( const auto& u : unknown )
        rest += ( rest.empty( ) ? "" : ", " ) + u.first + " x" + std::to_string( u.second );
    if ( !rest.empty( ) )
        race_unwired( s, "the level's gameInitialize (0x500B10): slot 6 not recreated for " + rest + " (UNKNOWN)" );
}

inline void race_contact_counts( const race_session_t& s, int& calls, int& ran, int& meshes );   // world/drive.hxx

// DEV TRACE - NOT RETAIL (no retail function; only with --verbose, logs
// nothing else and writes nothing): at sync counts 1 / 10 / 30 / 60 / 120 /
// 240 / 480, each boat's transform world position (+0x50 row 3) and the water
// under it as the recreated queries answer - the 0x52AEC0 surface that holds
// the point (-1 none: between surfaces, e.g. the Hydroelectric spillway
// drop) and its height (+0x14), the 0x52ACB0 surface under it (the Build
// tick's), and 0x529140's out +0x20 (the height with its waves).
inline void race_session_dev_trace( const race_session_t& s, int before )
{
    const int n = s.dynamics->syncs;
    // Ours (dev trace, a test aid): --trace-boat N prints the human boats' transform position and forward row
    // (world +16) every N syncs - the input a waypoint-following --input-script is generated from.
    static const int every = [ ]( ) {
        const char* v = c_engine::option( "trace-boat" );
        return v ? std::atoi( v ) : 0;
    }( );
    if ( every > 0 && n != before && n == every && s.track )
        for ( std::size_t w = 0; w < s.track->waypoints.size( ); ++w )
        {
            const track_waypoint_t& p = s.track->waypoints[ w ];
            LOG_INFO( "boat trace: waypoint %d pos %.4f %.4f %.4f next %d %d width %.3f checkpoint %d", static_cast< int >( w ), p.position[ 0 ],
                      p.position[ 1 ], p.position[ 2 ], p.next_a, p.next_b, p.p.width, track_checkpoint_index( *s.track, static_cast< int >( w ) ) );
        }
    if ( every > 0 && n != before && n % every == 0 )
        for ( std::size_t k = 0; k < s.boats.size( ); ++k )
        {
            const std::uint8_t* t = race_session_image( *s.boats[ k ], 0, 0x50 );
            bool human = false;
            for ( const void* h : s.manager.lists.humans.boats )
                human = human || h == s.boats[ k ]->graph.objects[ 0 ].image.data( );
            if ( !t || !human )
                continue;
            float m[ 16 ];
            std::memcpy( m, t + 0x50, 64 );
            LOG_INFO( "boat trace: sync %d frame %d %s pos %.4f %.4f %.4f fwd %.5f %.5f %.5f", n, s.frames, s.boats[ k ]->name.c_str( ), m[ 12 ], m[ 13 ], m[ 14 ],
                      m[ 4 ], m[ 5 ], m[ 6 ] );
        }
    if ( !g_verbose || n == before ||
         !( n == 1 || n == 10 || n == 30 || n == 60 || n == 120 || n == 240 || n == 480 || n == 960 || n == 1920 || n == 3840 ) )
        return;
    {
        int calls = 0, ran = 0, meshes = 0;
        race_contact_counts( s, calls, ran, meshes );
        LOG_VERBOSE( "race (dev trace): sync %d: contact-added callbacks %d, 0x5751F0 run %d (%d with obj1's collision mesh); "
                     "VuCollisionManager impacts played %d (%d pending), scrape records %d", n, calls, ran, meshes, s.collision_impacts,
                     static_cast< int >( s.collision.impacts.count ), s.collision_scrapes );
    }
    if ( s.world.water )
    {
        const int wakes = water_live_wakes( *s.world.water );
        LOG_VERBOSE( "race (dev trace): sync %d: VuWater holds %d wave record(s), %d live wake / flat wake (water_live_wakes)", n,
                     static_cast< int >( s.world.water->waves.size( ) ), wakes );
    }
    // The live dynamic props (dev trace): each one's transform world position, in water, OnCheckPointBuoyHit so far.
    for ( const auto& dp : s.dyn_props )
        if ( const transform_component_t* t = entity_transform( *dp->entity ) )
            LOG_VERBOSE( "race (dev trace): sync %d: dynamic prop '%s' at (%.3f, %.3f, %.3f) in water %d (buoy hits so far %d)", n,
                         dp->entity->m_name.c_str( ), t->world[ 12 ], t->world[ 13 ], t->world[ 14 ], dp->live.in_water, s.buoy_hits );
    for ( const auto& b : s.boats )
    {
        const std::uint8_t* t = race_session_image( *b, 0, 0x50 );
        if ( !t )
            continue;
        float q[ 4 ] = { 0.0f, 0.0f, 0.0f, 0.0f };
        std::memcpy( q, t + 0x50 + 48, 12 );
        if ( !s.world.view )
        {
            LOG_VERBOSE( "race (dev trace): sync %d: %s at (%.3f, %.3f, %.3f), no VuWater", n, b->name.c_str( ), q[ 0 ], q[ 1 ], q[ 2 ] );
            continue;
        }
        const water_t& w = s.world.view->water;
        const int holds = water_point_surface( w, q );
        const int under = water_surface_under( w, q, water_bits( 0xFEFFFFFFu ) );
        water_vertex_t o{};
        const bool known = water_point_height( w, q, water_view_kernel, const_cast< water_view_t* >( s.world.view ), o );
        LOG_VERBOSE( "race (dev trace): sync %d: %s at (%.3f, %.3f, %.3f) - water: holds surface %d (height %.1f), under %d, "
                     "0x529140 %s %.3f",
                     n, b->name.c_str( ), q[ 0 ], q[ 1 ], q[ 2 ], holds, holds >= 0 ? w.surfaces[ holds ].height : 0.0f, under,
                     known ? "" : "UNKNOWN", holds >= 0 ? o.height : 0.0f );
    }
}

// The transform watcher's 0x536350 (the instigator update, trigger/volume.hxx) for a boat of the session.
// The "Input" phase: the device's, the manager's and the control method's ticks (c_input_device::tick), `real_dt` = the tick
// manager's unscaled frame time; then g 0x758FC0 +0x18 where the session reads it. A loaded --input-script keeps method 0,
// the method its keys were written for (ours: the device still ticks, so the UI's "Select" stays live).
inline float race_autopilot_axis( const race_session_t& s, const char* name );   // world/autopilot.hxx

inline void race_input_tick( void* user, float dt )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    // --autopilot-keys (ours): the autopilot's steering as LEFT / RIGHT on the device, so the whole keyboard path runs; ConstantDB's auto throttle drives.
    if ( s.autopilot_keys )
    {
        // A pad on the test machine (XInput 0) would take the control method to 0 at a time of its own: this run has no pads.
        s.input->device.hooks.get_state = [ ]( void*, int, input_xinput_state_t* ) -> std::uint32_t { return 0x48Fu; };
        s.input->device.hooks.get_caps = [ ]( void*, int, std::uint8_t* ) -> std::uint32_t { return 0x48Fu; };
        const float steer = race_autopilot_axis( s, "Steering" );
        const int left = input_key_index( "LEFT" ), right = input_key_index( "RIGHT" );
        const int want = steer < -0.2f ? -1 : steer > 0.2f ? 1 : 0;
        if ( want != s.autopilot_held )
        {
            if ( s.autopilot_held < 0 )
                s.input->device.key_up( left );
            else if ( s.autopilot_held > 0 )
                s.input->device.key_up( right );
            if ( want < 0 )
                s.input->device.key_down( left );
            else if ( want > 0 )
                s.input->device.key_down( right );
            s.autopilot_held = want;
        }
    }
    for ( const race_session_t::key_script_t& k : s.key_script )
        if ( k.frame == s.frames )
        {
            if ( k.down )
                s.input->device.key_down( k.key );
            else
                s.input->device.key_up( k.key );
            LOG_INFO( "race: --keys frame %d key %d %s", k.frame, k.key, k.down ? "down" : "up" );
        }
    platform_input_tick( *s.input, dt, s.ticks.raw );
    const std::int32_t method = s.input_script ? 0 : s.input->device.control_method( );
    if ( method != s.world.control_method )
        LOG_INFO( "race: control method %d -> %d (frame %d)", s.world.control_method, method, s.frames );
    s.world.control_method = method;
}

inline boat_transform_hooks_t race_session_transform_hooks( race_session_boat_t& b )
{
    boat_transform_hooks_t h;
    h.user = &b;
    h.instigator_update = []( void* u ) {
        race_session_boat_t& r = *static_cast< race_session_boat_t* >( u );
        if ( r.owner )
            trigger_instigator_update( r.owner->triggers, r.io.instigator );
    };
    return h;
}

// VuInstigatorComponent's gameInitialize 0x531F50 on the boat's +0x70 (its gameInitialize's last call).
inline void race_session_register_instigator( race_session_t& s, race_session_boat_t& b )
{
    std::uint8_t* t = race_session_image( b, 0, 0x50 );
    if ( !b.io.instigator || !t )
    {
        race_unwired( s, "the instigator's gameInitialize 0x531F50 on a boat with no instigator / transform image - not registered (UNKNOWN)" );
        return;
    }
    trigger_instigator_initialize( s.triggers, b.io.instigator, t, b.index );
    b.instigator_registered = b.io.instigator[ 0x28 ];
}

// The "Triggers" phase: the trigger manager's tick 0x536460 (registered by its init 0x536110 at the engine's
// start) - the instigator step, then every listed trigger's vt+0x30 (the world's list).
inline void race_session_triggers( void* user, float )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    trigger_instigators_step( s.triggers );
    if ( s.world.triggers )
        s.world.triggers( );
    else
        race_unwired( s, "the Triggers tick 0x536460 with no trigger list (no world) - the crossing tests not run (UNKNOWN)" );
}

// The DynamicsSync / DynamicsKick ticks 0x4F8A20 / 0x4F8A90 (world/dynamics.hxx).
inline void race_session_dynamics_sync( void* user, float dt )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    const int before = s.dynamics->syncs;
    race_dynamics_sync( *s.dynamics, dt );
    race_session_dev_trace( s, before );
}

// The "Water" phase: VuWater's tick 0x529E30 (the world's water_tick).
inline void race_session_water( void* user, float dt )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    if ( s.world.water_tick )
        s.world.water_tick( dt );
    else
        race_unwired( s, "VuWater's Water tick 0x529E30 with no VuWater (no water_tick) - not run (UNKNOWN)" );
}

inline void race_session_dynamics_kick( void* user, float dt )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    race_dynamics_kick( *s.dynamics, dt );
    if ( !s.dynamics->stopped.empty( ) )
        race_unwired( s, s.dynamics->stopped );
}

// The Decision / Build delegates the boat's gameInitialize registers (0x41EFF0
// / 0x41F330 behind the thunks 0x4077ED / 0x404AB2):
// - Decision 0x41EFF0 = boat_decision_tick (core/boat state.hxx) composed on
//   the boat's objects with its hooks by world/drive.hxx (race_drive_decision);
//   a boat whose sections load stopped stays STOPPED (UNKNOWN, logged once).
// - Build 0x41F330 (world/build.hxx, read whole): its last step 0x4200E0
//   (boat_build_audio, verified) runs; steps 1 - 6 are declared (drive.hxx).
struct race_session_tick_t
{
    race_session_t* session;
    race_session_boat_t* boat;
    std::size_t index;
};

inline void race_session_decision( void* user, float dt )
{
    race_session_tick_t& t = *static_cast< race_session_tick_t* >( user );
    if ( t.boat->sections && t.index < t.session->drives.size( ) && t.session->drives[ t.index ] )
    {
        race_drive_decision( *t.session, t.index, dt );
        return;
    }
    if ( t.session->stopped_decision++ == 0 )
        LOG_WARNING( t.boat->sections ? "race: VuBoatEntity Decision tick 0x41EFF0 STOPPED (UNKNOWN) - %s's Decision composition did not build"
                                      : "race: VuBoatEntity Decision tick 0x41EFF0 STOPPED (UNKNOWN) - %s's sections load did not complete",
                     t.boat->name.c_str( ) );
}

inline void race_session_build( void* user, float dt )
{
    race_session_tick_t& t = *static_cast< race_session_tick_t* >( user );
    if ( t.boat->sections && t.index < t.session->drives.size( ) && t.session->drives[ t.index ] )
    {
        race_drive_build( *t.session, t.index, dt );
        return;
    }
    if ( t.session->stopped_build++ == 0 )
        LOG_WARNING( t.boat->sections ? "race: VuBoatEntity Build tick 0x41F330 STOPPED (UNKNOWN) - %s's Decision composition did not build"
                                      : "race: VuBoatEntity Build tick 0x41F330 STOPPED (UNKNOWN) - %s's sections load did not complete",
                     t.boat->name.c_str( ) );
}

// The AI manager's tick 0x403900 (fleet_tick), in Decision ahead of the boats
// (the order the boat agent's ai_race case runs - its registration is not read).
inline void race_session_fleet( void* user, float dt )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    fleet_tick( s.fleet, *s.track, s.pilots, s.fleet_boats, *s.random, dt );
}

// The dynamics world's persistent manifolds as VuCollisionManager's scrape tick reads them (g_vudynamicsimpl vt+0x6C
// -> the dispatcher's count / i-th, in the dispatcher's order): the body images (solver_rigid_t is the image's first
// 672 bytes), a boat body's owner = its session boat's image (the +0x2C0 word is a 32-bit image, not a pointer here),
// the shape's +8 mesh (race_contact_level_mesh for a level body; a boat's compound has none), the point cache
// (btManifoldPoint images, 0x120 stride). A manifold whose boat-flagged body is no session boat, or whose body is
// neither a boat nor a level body, is UNKNOWN: false (the caller does not run the tick on a partial list).
inline bool race_session_manifolds( race_session_t& s, std::vector< collision_manifold_t >& out )
{
    out.clear( );
    if ( !s.dynamics )
        return true;
    for ( const solver_manifold_t* m : s.dynamics->collision.dispatcher.manifolds )
    {
        collision_manifold_t mf;
        bool known[ 2 ] = { false, false };
        const solver_rigid_t* bodies[ 2 ] = { m->body0, m->body1 };
        for ( int k = 0; k < 2; ++k )
        {
            const std::uint8_t*& owner = k == 0 ? mf.owner0 : mf.owner1;
            const collision_mesh_asset_t*& mesh = k == 0 ? mf.mesh0 : mf.mesh1;
            for ( const auto& b : s.boats )
                if ( &b->body.body == bodies[ k ] )
                {
                    owner = b->graph.objects[ 0 ].image.data( );
                    known[ k ] = true;
                }
            for ( const auto& lb : s.level_bodies )
                if ( !known[ k ] && &lb->body.body == bodies[ k ] )
                {
                    mesh = lb->type == 7 ? lb->asset : nullptr;   // = drive.hxx race_contact_level_mesh (0x5445C0 / 0x5444D5)
                    known[ k ] = true;
                }
        }
        if ( !known[ 0 ] || !known[ 1 ] )
            return false;
        mf.body0 = reinterpret_cast< const std::uint8_t* >( m->body0 );
        mf.body1 = reinterpret_cast< const std::uint8_t* >( m->body1 );
        std::uint16_t g0, g1;
        std::memcpy( &g0, mf.body0 + 0x2CA, 2 );
        std::memcpy( &g1, mf.body1 + 0x2CA, 2 );
        if ( ( ( g0 & 0x10u ) && !mf.owner0 ) || ( ( g1 & 0x10u ) && !mf.owner1 ) )
            return false;
        mf.contacts = m->num_contacts;
        mf.points = reinterpret_cast< const std::uint8_t* >( m->points );
        out.push_back( mf );
    }
    return true;
}

// VuCollisionManager's Decision tick 0x4AD970 (registered by its init 0x4AD110) = collide.hxx collision_manager_tick:
// the two FMOD groups (EventSystem::getGroup), the impacts 0x4ADA10, the scrapes 0x4ADC00 over the dynamics world's
// manifolds, the dev stats page. HOOKS: every FMOD call (audio, a platform hook - no group / event made, so the
// sound steps do nothing), the dev page (a dev hook, not made); the effects on the world's VuPfxManager / runtime.
// With no VuPfx runtime / manager, or a manifold the session cannot name (UNKNOWN), the tick is not run.
inline void race_session_collision( void* user, float )
{
    race_session_t& s = *static_cast< race_session_t* >( user );
    if ( !s.world.pfx || !s.world.pfx_manager || !s.world.pfx_ops )
    {
        race_unwired( s, "VuCollisionManager's tick 0x4AD970 with no VuPfx runtime / manager - UNKNOWN, not run" );
        return;
    }
    std::vector< collision_manifold_t > manifolds;
    if ( !race_session_manifolds( s, manifolds ) )
    {
        race_unwired( s, "VuCollisionManager's tick 0x4AD970: a manifold body that is no session boat / level body - UNKNOWN, not run" );
        return;
    }
    collision_impact_hooks_t ik;
    ik.user = &s;
    ik.get_event = []( void* u, const char*, std::uint32_t& ) {
        race_unwired( *static_cast< race_session_t* >( u ), "audio: VuCollisionManager's impact sound (FMOD getEvent - platform) - no event made" );
        return false;
    };
    collision_scrape_hooks_t sk;
    sk.user = &s;
    sk.get_event = []( void* u, std::uint32_t, const char*, std::uint32_t& event ) {
        race_unwired( *static_cast< race_session_t* >( u ), "audio: VuCollisionManager's scrape sound (FMOD getEvent - platform) - no event made" );
        event = 0u;   // the hook writes the record's event word: no event
    };
    collision_tick_hooks_t th;
    th.user = &s;
    th.get_group = []( void* u, const char*, std::uint32_t& ) {
        race_unwired( *static_cast< race_session_t* >( u ), "audio: VuCollisionManager's FMOD event groups (EventSystem::getGroup - platform) - none" );
    };
    s.collision_impacts += s.collision.impacts.count;
    const int unread = collision_manager_tick( s.collision, s.collision_tables, manifolds.data( ), static_cast< std::int32_t >( manifolds.size( ) ),
                                               *s.world.pfx_manager, *s.world.pfx_ops, s.world.pfx->heap, s.world.pfx->resources, ik, sk, th );
    s.collision_scrapes = s.collision.scrapes.count;
    if ( unread )
        race_unwired( s, "VuCollisionManager's scrapes 0x4ADC00 read past a collision mesh's arrays (UNKNOWN in retail) - the body's surface byte kept" );
}

// VuCollisionManager's reset 0x4AD2B0, called by VuGame's 0x482D50 (its teardown, after gameRelease 0x500B80) -
// 0x482D50 is NOT recreated and no session path reaches it yet: this is its call, for when it is.
inline void race_session_collision_reset( race_session_t& s )
{
    if ( !s.world.pfx || !s.world.pfx_manager )
        return;
    collision_scrape_hooks_t sk;
    collision_manager_reset( s.collision, *s.world.pfx_manager, s.world.pfx->heap, s.world.pfx->resources, sk );
}

// The Viewports phase's boat manager tick 0x4AB0A0 (boat_manager_viewport_tick,
// core/boat manager.hxx, composed by world/drive.hxx): the viewport boat's
// camera image (the Decision tick's camera step 0x409900) into the viewport.
inline void race_session_viewports( void* user, float )
{
    race_drive_viewports( *static_cast< race_session_t* >( user ) );
}
// A boat property's text into the boat image's inline std::string (+0x74 /
// +0x8C / +0xA4 - VuEntity::load's targets): false for a text retail would
// keep on the heap (16 characters or more - not modelled by the image).
inline bool race_session_text( std::uint8_t* boat, int at, const std::string& text )
{
    if ( text.size( ) > 15 )
        return false;
    image_empty_string( boat, at );
    parts_string( boat, at, text );
    return true;
}

// What the session reads from the pack (the databases' document roots).
struct race_session_dbs_t
{
    const value_t* boat_db = nullptr;
    const value_t* track_db = nullptr;
    const value_t* constant_db = nullptr;
};

// The front end's GameData (ui_front_end_game_data, core/ui config.hxx) from
// --mode / --race / --course / --boat / --skin - built BEFORE the project load,
// as retail's front end leaves it for VuActionGameMode's load 0x4BA060 (whose
// project choice 0x4D3480 reads its Level / GameType / Course). False (logged)
// when an option is missing or the builder is not recreated. The roster picks
// draw from the shared VuRandom 0x736810 in whatever state the run has left it.
inline bool race_session_game_data( const std::string& mode, const std::string& track_name, const std::string& course, const std::string& boat,
                                    const std::string& skin, const race_session_dbs_t& db, fleet_random_t& random, value_t& out,
                                   std::function< std::string( const char* ) > string_db = nullptr )
{
    if ( mode.empty( ) || boat.empty( ) || skin.empty( ) || track_name.empty( ) )
    {
        LOG_WARNING( "race: boats UNKNOWN - --mode, --race, --boat and --skin are all required (no defaults); no race" );
        return false;
    }
    if ( !db.boat_db || !db.track_db || !db.constant_db )
    {
        LOG_ERROR( "race: BoatDB / TrackDB / ConstantDB not readable from the pack; no race" );
        return false;
    }
    ui_game_env_t env;
    env.boat_db = db.boat_db;
    env.track_db = db.track_db;
    env.random = &random;
    env.player_name = platform_gamer_tag( );
    env.string_db = std::move( string_db );
    ui_game_config_t built;
    out = ui_front_end_game_data( mode, track_name, course, boat, skin, env, &built );
    if ( !built.built )
    {
        LOG_WARNING( "race: GameData UNKNOWN - mode '%s' reaches builder 0x%X, %s; no race", mode.c_str( ), built.builder,
                     built.builder ? "not recreated" : "none (not a front-end event type)" );
        return false;
    }
    return true;
}

// The start (0x4BA060 after the project load, for GameType "Race": the
// VuRaceGame ctor, the boat load + postLoad, begin) on the GameData
// race_session_game_data built. `tree` the race project's entity tree, `track`
// the world's track manager built from it. False (logged) when a boat cannot
// be made.
// THE SESSION END (world batch 26): what leaving the race runs - VuActionGameMode's exit 0x4B97E0 calls VuGame's
// release 0x482D50 (rules.hxx vugame_release: the machines' current exits run here; its other callees are the AI
// manager's / analytics' (platform) / the entity gameRelease 0x500B80 / track / collision / lens water / listener / DB
// managers' - not recreated, each logged), then the level's entities go and with them their bodies: VuRigidBody's dtor
// 0x50FAF0 (bodies.hxx rigid_body_destroy) on every level body and boat body (the boats' via VuBoatEntity's dtor
// 0x41CB10 - the boat lane's - stated, not run; the body dtor itself is run on them). VuRigidBodyComponent's dtor
// 0x543FC0 frees two VuStrings and deletes its property list: ours are std::string / C++ objects (RAII - the library's
// frees, no retail-visible word). The rest of 0x4B97E0, read (batch 27): the current action-mode state's exit delegate,
// 0x4D3100 on g_vugameutil (not recreated), 0x482D50, the game's deleting dtor vt+0xC(1), then 0x500B80 on the project
// root (race_session_game_release) and the repository's vt+0x1C, then the project asset's release (the render device's
// flush vt+0x30 / +0xD0 around it - the renderer's) whose last reference deletes it - the entities and their bodies
// (below); the same for +0x38's asset. Ours: the caller decides WHEN (a replaced session, or the --end-race test driver).
// VuEntity's gameRelease 0x500B80 on the project's root (0x4B97E0 after VuGame's release 0x482D50 and the game's deleting
// dtor, when the root's +0x14 bit 2 - gameInitialize'd - is set; then the repository vt+0x1C and the project asset's
// release, which destroys the entities): per entity with bit 2 set - each component's vt+0x28 (+0x48 list), the
// children in +0x38 order (recursively), its own slot 7 (vt+0x1C), the repository g 0x759080 vt+0x24 unless +0x14 & 8
// (a hook), +0x14 &= ~4. Slot 7 recreated here: VuControlEntity 0x5CD660 (keyframe.hxx control_game_release) and
// VuKeyframeMotionEntity 0x5CEA40 (keymotion.hxx key_motion_game_release); every other class's slot 7 and every
// component's vt+0x28 are not run (counted, logged).
inline void race_session_game_release( race_session_t& s )
{
    if ( !s.tree )
        return;
    std::map< std::string, int > unknown;
    int components = 0, released = 0;
    const auto walk = [ & ]( const auto& self, entity_t& e ) -> void {
        components += static_cast< int >( e.m_components.size( ) );
        for ( auto& c : e.m_children )
            self( self, *c );
        if ( e.m_type == "VuControlEntity" )
            for ( const auto& k : s.controls )
                if ( k->entity == &e )
                {
                    control_game_release( *k->c, race_session_control_hooks( *k ) );
                    ++released;
                    return;
                }
        if ( s.world.motion_release && s.world.motion_release( e ) )
        {
            ++released;
            return;
        }
        if ( e.m_type == "VuForceAffectorEntity" )
            for ( const auto& f : s.forces )
                if ( f->entity == &e )
                {
                    force_affector_game_release( *f->value, f->hooks );   // slot 7 0x439920
                    ++released;
                    return;
                }
        if ( e.m_type == "VuKeyframeMotionEntity" )
            for ( const auto& k : s.key_motions )
                if ( k->entity == &e )
                {
                    key_motion_game_release( *k->k, race_session_key_hooks( *k ) );
                    ++released;
                    return;
                }
        ++unknown[ e.m_type.empty( ) ? std::string( "(UNKNOWN node)" ) : e.m_type ];
    };
    walk( walk, *s.tree );
    {
        int pushed = 0, pushes = 0;   // ours, a log line
        for ( const auto& f : s.forces )
            if ( f->pushes )
            {
                ++pushed;
                pushes += f->pushes;
            }
        if ( !s.forces.empty( ) )
            LOG_INFO( "race: %d VuForceAffectorEntity, %d pushed a body (%d pushes)", static_cast< int >( s.forces.size( ) ), pushed, pushes );
    }
    std::string by;
    for ( const auto& u : unknown )
        by += ( by.empty( ) ? "" : ", " ) + u.first + " x" + std::to_string( u.second );
    LOG_INFO( "race: the project's gameRelease 0x500B80 - %d slot 7s run (VuControlEntity / VuKeyframeMotionEntity / VuAttachEntity / VuRotateMotionEntity)", released );
    race_unwired( s, "the project's gameRelease 0x500B80: slot 7 not recreated for " + by + "; " + std::to_string( components ) +
                     " components' vt+0x28 not run; the repository vt+0x24 / +0x1C (hooks)" );
}

inline void race_powerups_release( race_session_t& s );   // world/pickups.hxx

inline void race_session_end( race_session_t& s )
{
    if ( !s.active )
        return;
    race_powerups_release( s );
    if ( s.world.pfx )   // the pfx frame's boats die with the session (world/drive.hxx race_drive_pfx_frame)
    {
        pfx_frame_t& pf = s.world.pfx->frame;
        pf.boats = nullptr;
        pf.boat_count = 0;
        pf.fluids = nullptr;
        pf.fluids_count = 0;
        pf.user = nullptr;
        pf.prepare = nullptr;
        pf.point = nullptr;
        pf.snap = nullptr;
        pf.water = true;
    }
    std::vector< std::uint32_t > flags( s.rules_boats.size( ), 4u );   // every game boat was gameInitialize'd (+0x14 bit 2)
    vugame_release_hooks_t h;
    h.exit = [ &s ]( int machine ) {
        c_fsm& f = machine == 0 ? s.rules.event : machine == 1 ? s.rules.split : s.rules.fsm;
        if ( fsm_state_t* st = f.current( ) )
            if ( st->m_exit )
                st->m_exit( );
    };
    // The AI manager g 0x758F8C (vtable 0x735734): vt+0x20 = slot 8 0x403500 (state 4, +0x3C 0, then 0x4046F0 - the
    // RecordDebugEvents aidebug.json save: a dev hook) = fleet_end; vt+0x24 = slot 9 0x403510 (reset) = fleet_reset (boat
    // fleet.hxx, the boat agent's).
    h.ai = [ &s ]( int slot ) {
        if ( slot == 8 )
            fleet_end( s.fleet );
        else
            fleet_reset( s.fleet );
    };
    h.analytics = [ &s ]( const char* name, const value_t& data ) {
        LOG_INFO( "race: analytics %s (platform hook): Finished %d TotalTime %.3f", name, ui_json_find( data, "Finished" ).integer,
                  ui_json_find( data, "TotalTime" ).real );
        (void)s;
    };
    // 0x500B80 on a game boat: its slot 7 0x41D470 (race.hxx race_boat_game_release): the ticks, slot 18, the rig. Not run, named: the
    // driver's slot 7, the hull's 0x419260, the engine's 0x415330, the debris' 0x411950, the draw manager's remove (the renderer's).
    h.release = [ &s ]( std::size_t k ) {
        if ( k >= s.boats.size( ) )
            return;
        race_session_boat_t& b = *s.boats[ k ];
        race_boat_init_hooks_t rh;
        if ( k < s.start_boats.size( ) )
        {
            rh.manager = &s.manager.lists;
            rh.entry = s.start_boats[ k ].boat;
        }
        rh.slot18 = [ &s, &b ] { race_session_enable( s, b, false ); };
        rh.anim_release = [ &b ] { boat_rig_release( b.rig ); };
        rh.call = [ &s ]( const std::string& row ) {
            if ( row == "driver slot7 driver" || row == "0x419260 hull" || row == "0x415330 engine" || row == "0x411950 damage" )
                race_unwired( s, "the boat's gameRelease 0x41D470: " + row + " - not recreated" );
        };
        race_boat_game_release( &b, s.ticks, b.draw_added, b.instigator_registered, rh );
    };
    // The game's slot 6 by mode (rules.hxx vugame_release_hooks_t::slot6).
    h.slot6 = s.start.slalom ? 0x48C880u : s.start.timetrial ? 0x48F330u : s.mode == "Ghost" ? 0x485360u : 0x489A10u;
    h.draw_remove = [ &s ] {
        if ( !s.slalom_draw )
            return;
        s.slalom_draw = false;   // +0x744's +0x1E = 0 after the draw manager's remove (g 0x7590B8 vt+0x2C - the renderer's)
        race_unwired( s, "VuSlalomGame slot 6 0x48C880: the draw manager's remove (g 0x7590B8 vt+0x2C) of +0x744 - the renderer's (a hook)" );
    };
    h.ghost_release = [ &s ] { ghost_clear( s.ghost ); };   // VuGhostGame slot 6 0x485360: 0x4AF490
    h.call = [ &s ]( const char* what ) {
        const std::string w( what );
        if ( w == "cameras" )
            s.viewport_set = false;   // the viewport manager's cameras = 0: the view stops following the boat's camera
        else
            race_unwired( s, "VuGame release: " + w + " (" +
                             ( w == "track28" ? "the track manager vt+0x28" : w == "collision" ? "0x4AD2B0 on the collision manager" :
                               w == "lens24" ? "lens water vt+0x24" : w == "listener" ? "0x539020 on g 0x75906C" : "0x4D2010" ) + ") - not recreated" );
    };
    vugame_release( s.rules, flags, h );
    race_session_game_release( s );
    int bodies = 0;
    for ( const auto& lb : s.level_bodies )
    {
        if ( lb->in_world && s.dynamics )
        {
            vu_dynamics_remove_rigid_body( s.dynamics->impl, lb->body );
            lb->in_world = false;
        }
        if ( lb->built )
        {
            rigid_body_destroy( reinterpret_cast< std::uint8_t* >( &lb->body ), s.aligned_frees, { } );
            ++bodies;
        }
    }
    for ( const auto& b : s.boats )
    {
        rigid_body_destroy( reinterpret_cast< std::uint8_t* >( &b->body ), s.aligned_frees, { } );
        ++bodies;
    }
    LOG_INFO( "race: session end - VuGame release 0x482D50 run, %d bodies through VuRigidBody's dtor 0x50FAF0 (gNumAlignedFree +%u)", bodies,
              s.aligned_frees );
    // The game's / boats' / drivers' deletion: their VuEventMap dtors 0x515620 - each registration's 0x5057E0.
    const std::size_t registrations = s.bus.registrations.size( );
    std::map< std::string, int > by_owner;
    for ( const auto& h : s.bus.handlers )
        ++by_owner[ h.second.owner.substr( 0, h.second.owner.find( ' ' ) ) ];   // the class
    std::string owners;
    for ( const auto& o : by_owner )
        owners += ( owners.empty( ) ? "" : ", " ) + o.first + " x" + std::to_string( o.second );
    race_bus_release( s.bus );
    LOG_INFO( "race: VuEventManager - %d registrations removed (0x5057E0: %s); %d handler calls, %d broadcasts nobody in the race's code heard",
              static_cast< int >( registrations ), owners.c_str( ), s.bus.fired, s.bus.unheard );
    s.active = false;
}

// GameType "Slalom": VuSlalomGame (world/rules.hxx slalom_rules_construct, case slalom_game_fsm) - its machine, TrackDB and
// the human's +0x160 for its Game exit 0x48DE10, the course +0x720 (gameutil 0x4D32F0: the GameData "Course"), and the
// gates its ctor 0x48BEC0 finds (0x48E610 on the game project's root: world/slalom.hxx slalom_first_gate, then each gate's
// "Next" ref target - its script component's) with their Diameter / Allow Recovery (slalom_gate_state_t) and transform
// worlds. Slot 7 0x48C8C0's gates tick (hooks.gates): slalom_gates_tick on the human (FMOD - a platform hook; the
// broadcasts OnSlalomGateHit / OnSlalomGateMissed through the world's event manager, an empty VuParams).
inline void race_session_slalom( race_session_t& s, entity_t& tree, const race_session_dbs_t& db )
{
    slalom_rules_construct( s.rules );
    s.rules.track_db = db.track_db;
    s.boat_db = db.boat_db;
    for ( std::size_t k = 0; k < s.start_boats.size( ); ++k )
        if ( s.start_boats[ k ].cls.slot[ 0 ] )
        {
            const auto it = s.boats[ k ]->io.boat_strings.find( 0x160 );
            s.rules.human_boat = it == s.boats[ k ]->io.boat_strings.end( ) ? std::string( ) : it->second;
            break;
        }
    s.rules.course = race_game_data_text( s.util, nullptr, "Course" );
    const auto next_of = []( entity_t& e ) -> entity_t* {
        if ( const script_component_t* sc = script_of( e ) )
            for ( const script_ref_t& ref : sc->m_refs )
                if ( std::string( ref.m_row->m_name ) == "Next" )
                    return ref.m_target;
        return nullptr;
    };
    std::vector< entity_t* > chain;
    bool known = true;
    for ( entity_t* e = const_cast< entity_t* >( slalom_first_gate( tree ) ); e; e = next_of( *e ) )
    {
        if ( std::find( chain.begin( ), chain.end( ), e ) != chain.end( ) )
        {
            race_unwired( s, "VuSlalomGame: the gates' Next refs loop (retail's slot 5 walk would not end) - the course not run (UNKNOWN)" );
            known = false;
            break;
        }
        const transform_component_t* t = entity_transform( *e );
        if ( e->m_type != "VuSlalomGateEntity" || !t || e->m_world_unknown || !slalom_gate_state_of( *e ) )
        {
            race_unwired( s, "VuSlalomGame: a gate whose class / world / properties are not known here - the course not run (UNKNOWN)" );
            known = false;
            break;
        }
        chain.push_back( e );
    }
    s.slalom_gates.assign( known ? chain.size( ) : 0u, slalom_gate_t{ } );
    for ( std::size_t k = 0; k < s.slalom_gates.size( ); ++k )
    {
        const slalom_gate_state_t& p = *slalom_gate_state_of( *chain[ k ] );
        slalom_gate_t& g = s.slalom_gates[ k ];
        g.world = entity_transform( *chain[ k ] )->world;
        g.diameter = p.value.diameter;
        g.allow_recovery = p.value.allow_recovery;
        g.model = p.value.model;            // the draw's instances (slalom.hxx slalom_draw_gates - the renderer's list)
        g.reflection = p.value.reflection;
        g.next = k + 1 < s.slalom_gates.size( ) ? &s.slalom_gates[ k + 1 ] : nullptr;
    }
    s.slalom = slalom_course_t{ };
    s.slalom.first = s.slalom.current = s.slalom_gates.empty( ) ? nullptr : &s.slalom_gates[ 0 ];
    s.rules.hooks.gates = [ &s ] {
        if ( !s.slalom_human )
            return;
        slalom_hooks_t h;
        h.user = &s;
        h.sound = []( void* u, const char* event, const float* ) {
            race_unwired( *static_cast< race_session_t* >( u ), std::string( "audio: FMOD " ) + event + " (getEvent / set3DAttributes / start - platform)" );
        };
        h.broadcast = []( void* u, const char* name ) {
            race_session_t& rs = *static_cast< race_session_t* >( u );
            ui_params_t p;
            race_session_post( rs, name, &p );
        };
        const slalom_gate_t* before = s.slalom.current;
        const int r = slalom_gates_tick( s.slalom, s.slalom_human, s.slalom_world, h );
        if ( r == 1 || r == 2 || ( r == 0 && s.slalom.current != before ) )
            LOG_INFO( "race: slalom gate %d %s - next %d, recovery gate %d, boost %.3f, race time %.3f, missed %d",
                      static_cast< int >( before - s.slalom_gates.data( ) ), r == 1 ? "PASSED" : "MISSED",
                      s.slalom.current ? static_cast< int >( s.slalom.current - s.slalom_gates.data( ) ) : -1,
                      s.slalom.recover ? static_cast< int >( s.slalom.recover - s.slalom_gates.data( ) ) : -1, rules_f( s.slalom_human, 0x310 ),
                      rules_f( s.slalom_human, 0x4B4 ), rules_i32( s.slalom_human, 0x4D0 ) );
    };
    LOG_INFO( "race: VuSlalomGame - course '%s', %d gates (first '%s'), the human's +0x160 '%s' keys TrackDB [%s][Slalom]", s.rules.course.c_str( ),
              static_cast< int >( s.slalom_gates.size( ) ), chain.empty( ) ? "" : chain[ 0 ]->m_name.c_str( ), s.rules.human_boat.c_str( ), s.game.track.c_str( ) );
}

// VuSlalomGame slot 5 0x48C660: the course start (slalom_course_start on the human = the boat manager's +0x24 list [0]),
// the first state's enter, then the draw component's add (its draw callback is the renderer's - a hook).
inline void race_session_slalom_start( race_session_t& s, const race_session_dbs_t& db )
{
    const auto& humans = s.manager.lists.humans.boats;
    for ( std::size_t k = 0; !humans.empty( ) && k < s.boats.size( ); ++k )
        if ( humans[ 0 ] == s.boats[ k ]->graph.objects[ 0 ].image.data( ) )
        {
            s.slalom_human = s.boats[ k ]->graph.objects[ 0 ].image.data( );
            const std::uint8_t* t = race_session_image( *s.boats[ k ], 0, 0x50 );
            s.slalom_world = t ? reinterpret_cast< const float* >( t + 0x50 ) : nullptr;
            const auto it = s.boats[ k ]->io.boat_strings.find( 0x160 );
            const std::string cls = it == s.boats[ k ]->io.boat_strings.end( ) ? std::string( ) : it->second;
            if ( !s.slalom_world )
                s.slalom_human = nullptr;
            else
                slalom_course_start( s.slalom, member( member( *db.constant_db, "VuDBAsset" ), "Slalom" ), cls, s.slalom_human, s.slalom_world );
        }
    if ( !s.slalom_human )
        race_unwired( s, "VuSlalomGame slot 5 0x48C660: no human boat (retail reads the +0x24 list's [0] unchecked) - the course not started (UNKNOWN)" );
    else
        LOG_INFO( "race: VuSlalomGame slot 5 - GateMissPenalty %.3f, BoostAddAmount %.3f", s.slalom.penalty, s.slalom.boost_add );
    race_rules_start( s.rules );
    if ( !s.slalom_draw )
    {
        s.slalom_draw = true;
        race_unwired( s, "VuSlalomGame +0x744 Vu3dDrawComponent: the draw manager's add (g 0x7590B8 vt+0x28) - its draw callback not recreated (the renderer's)" );
    }
}

// The game's registrations into the bus (0x5156E0), in its ctor's order: VuGame's base 0x480BA0 - "OnUserNext" 0x483170,
// "OnCheatFinish" 0x483190 (VuMethod1 vtable 0x73F65C) - then the mode's own (rules.hxx k_race_rules_events /
// game_mode_defs: VuRaceGame 0x489030 "OnStartLightsOut" 0x48A780 + "OnCheckPointBuoyHit" 0x48A7B0; VuTimeTrialGame
// 0x490540; VuSlalomGame 0x48DB40 + "OnBoatRecovered" 0x48DB70; VuGhostGame 0x485860). The handlers, read:
//   0x483170: the +0xC machine's condition "OnUserNext" (0x555C60) +0x1D = 1 - the pulse (c_fsm::pulse_condition);
//   0x483190: 0x4848C0 (rules.hxx race_rules_cheat_finish; its AI-manager slot 12 0x4036C0 = fleet_remove_driver);
//   0x48A780 / 0x490540 / 0x48DB40 / 0x485860: the mode's machine (+0x6F8; the ghost's +0x700) condition
//     "StartLightsOut" +0x1C = 1 (the four are byte-identical but for the machine offset and the string's copy);
//   0x48A7B0: rules.hxx race_rules_on_buoy_hit;  0x48DB70: slalom.hxx slalom_recovered (below).
inline void race_session_game_events( race_session_t& s, const std::string& mode )
{
    s.bus.ticks = &s.ticks;
    // The pause menu's ctor 0x4BAE80 (the action mode's ctor, before any game): its two gate events. Its others: c_world::pause_bind.
    race_bus_subscribe( s.bus, "OnPauseActionGame", 0x4BB660u, "VuPauseMenu", [ &s ]( const ui_params_t& ) { pause_on_pause( s.pause ); } );
    race_bus_subscribe( s.bus, "OnSetAllowPause", 0x4BB900u, "VuPauseMenu", [ &s ]( const ui_params_t& p ) { pause_on_set_allow( s.pause, p ); } );
    race_bus_subscribe( s.bus, "OnUserNext", 0x483170u, "VuGame", [ &s ]( const ui_params_t& ) {
        s.rules.event.pulse_condition( "OnUserNext" );
    } );
    race_bus_subscribe( s.bus, "OnCheatFinish", 0x483190u, "VuGame", [ &s ]( const ui_params_t& ) {
        std::vector< std::uint8_t* > humans;
        for ( const void* b : s.manager.lists.humans.boats )
            humans.push_back( const_cast< std::uint8_t* >( static_cast< const std::uint8_t* >( b ) ) );
        race_rules_cheat_finish( humans, [ & ]( std::size_t h )
        {
            for ( std::size_t k = 0; k < s.rules_boats.size( ); ++k )
                if ( s.rules_boats[ k ].boat == humans[ h ] )
                    fleet_remove_driver( s.fleet, static_cast< int >( k ) );   // the AI manager's slot 12 0x4036C0
        } );
        LOG_INFO( "race: OnCheatFinish (VuGame 0x483190): %d human boats finished", static_cast< int >( humans.size( ) ) );
    } );
    const game_mode_def_t* def = nullptr;
    for ( const game_mode_def_t& d : game_mode_defs( ) )
        if ( std::string( d.rtti ) == ".?AVVu" + mode + "Game@@" )
            def = &d;
    std::vector< race_rules_event_t > events;
    if ( def )
        events = def->events;
    else
        events.assign( std::begin( k_race_rules_events ), std::end( k_race_rules_events ) );
    const std::string owner = def ? std::string( def->rtti ) : std::string( ".?AVVuRaceGame@@" );
    for ( const race_rules_event_t& e : events )
    {
        const std::string name( e.name );
        std::function< void( const ui_params_t& ) > fn;
        if ( name == "OnStartLightsOut" )
            fn = [ &s, e ]( const ui_params_t& ) {
                race_rules_on_start_lights_out( s.rules );
                LOG_INFO( "race: OnStartLightsOut (0x%X) - the race starts on the next game tick", e.method );
            };
        else if ( name == "OnCheckPointBuoyHit" )
            fn = [ &s ]( const ui_params_t& p ) { race_rules_on_buoy_hit( s.rules, p.m_bytes, p.m_size ); };
        else if ( name == "OnBoatRecovered" )
            fn = [ &s ]( const ui_params_t& p ) {
                // 0x505370: the first record, when the size is above 0, its tag 11 (an entity) then its id; else 0. The
                // repository g 0x759080 vt+0x28( id ) - not recreated: a session boat whose entity id (0x500D50: FNV-1a of
                // the long name - a race boat has no parent, so its name) is it (a HOOK standing for the lookup).
                std::uint32_t tag = 0xFFFFFFFFu, id = 0;
                if ( p.m_size > 0 )
                {
                    std::memcpy( &tag, p.m_bytes, 4 );
                    if ( tag == 11 )
                        std::memcpy( &id, p.m_bytes + 4, 4 );
                }
                race_session_boat_t* boat = nullptr;
                for ( const auto& b : s.boats )
                    if ( id != 0 && event_hash( b->name.c_str( ) ) == id )
                        boat = b.get( );
                if ( !boat )
                    return;   // 0x48DB70: no entity / not a VuBoatEntity - nothing
                const std::uint8_t* t = race_session_image( *boat, 0, 0x50 );
                if ( !t )
                {
                    race_unwired( s, "OnBoatRecovered 0x48DB70: the boat's transform image is missing - not run (UNKNOWN)" );
                    return;
                }
                slalom_recovered( s.slalom, boat->graph.objects[ 0 ].image.data( ), reinterpret_cast< const float* >( t + 0x50 ) );
                LOG_INFO( "race: OnBoatRecovered (VuSlalomGame 0x48DB70) - %s, next gate %d", boat->name.c_str( ),
                          s.slalom.current ? static_cast< int >( s.slalom.current - s.slalom_gates.data( ) ) : -1 );
            };
        else
            fn = [ &s, e ]( const ui_params_t& ) {
                char text[ 128 ];
                std::snprintf( text, sizeof( text ), "the game's event %s 0x%X - not recreated, not run (UNKNOWN)", e.name, e.method );
                race_unwired( s, text );
            };
        race_bus_subscribe( s.bus, e.name, e.method, owner, std::move( fn ) );
    }
}

// The boats' and drivers' registrations, in the boat load's order (0x481CE0: each boat's ctor 0x41B850 - "OnCameraShake"
// 0x41E7B0, "OnRumble" 0x41E950 (boat construct.hxx) - then its postLoad's driver: a VuHumanDriver's ctor 0x40ED50
// "OnStartLightsOut" 0x40F680 = driver +0xC0 = 0x3E800000 (0.25), read whole). The boat handlers are boat events.hxx's
// (boat_on_camera_shake on the camera, boat_on_rumble on the driver). No code in a race broadcasts OnCameraShake /
// OnRumble (their three name copies: 0x41B850's registration, 0x434A20 and 0x438260 - both into the TARGET's own map),
// so those two never run from the bus; they are registered because 0x5156E0 does both.
inline void race_session_boat_events( race_session_t& s )
{
    for ( std::size_t k = 0; k < s.boats.size( ); ++k )
    {
        race_session_boat_t* b = s.boats[ k ].get( );
        race_bus_subscribe( s.bus, "OnCameraShake", 0x41E7B0u, "VuBoatEntity " + b->name, [ b ]( const ui_params_t& p ) {
            if ( b->io.camera )
                boat_on_camera_shake( b->io.camera, p.m_bytes );
        } );
        race_bus_subscribe( s.bus, "OnRumble", 0x41E950u, "VuBoatEntity " + b->name, [ b ]( const ui_params_t& p ) {
            if ( b->driver && !b->driver->graph.empty( ) )
            {
                std::uint8_t* drv = b->driver->graph[ 0 ].image.data( );
                boat_on_rumble( drv, load_word( drv, 0 ), p.m_bytes );
            }
        } );
        if ( k < s.start_boats.size( ) && s.start_boats[ k ].driver && load_word( s.start_boats[ k ].driver, 0 ) == 0x737F44u )
        {
            std::uint8_t* driver = s.start_boats[ k ].driver;
            race_bus_subscribe( s.bus, "OnStartLightsOut", 0x40F680u, "VuHumanDriver " + b->name, [ driver ]( const ui_params_t& ) {
                const std::uint32_t quarter = 0x3E800000u;
                std::memcpy( driver + 0xC0, &quarter, 4 );
            } );
        }
    }
}

#include <workspace/core/world/ghostplay.hxx>
#include <workspace/core/world/autopilot.hxx>
#include <workspace/core/world/opening.hxx>
