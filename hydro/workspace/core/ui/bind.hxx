#pragma once

#include <workspace/core/ui/chooser.hxx>
#include <workspace/core/ui/controls.hxx>
#include <workspace/core/ui/hud.hxx>
#include <workspace/core/ui/leave.hxx>
#include <workspace/core/ui/leaves.hxx>
#include <workspace/core/ui/navigate.hxx>
#include <workspace/core/ui/results.hxx>
#include <workspace/core/ui/board.hxx>
#include <workspace/core/ui/ladder.hxx>
#include <workspace/core/ui/screen.hxx>
#include <workspace/core/ui/slider.hxx>
#include <workspace/core/ui/standings.hxx>
#include <workspace/core/ui/ticker.hxx>
#include <workspace/core/ui/footer.hxx>
#include <workspace/core/world/latch.hxx>
#include <workspace/core/ui/completion.hxx>
#include <workspace/core/ui/wired.hxx>
#include <workspace/core/ui/wiring.hxx>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The per-frame update of a loaded screen: OUR loaded entity tree (the scripted
// world, wiring.hxx - plugs and links from the load) bound to the runtime
// objects that retail's ctors build, so c_ui_screen's enter / tick / exit drive
// them through the same event maps and the same graph.
//
// Every handler a ctor registers through VuEventMap 0x5156E0 goes BOTH into the
// entity's own map (+0xC, a std::map by hash; the dispatch 0x5155C0) and into
// the event manager g 0x759028 (+4: hash -> subscribers, in registration order);
// VuEventManager_broadcast 0x505690 runs every subscriber of the hash inside the
// call. So a broadcast here reaches every bound map holding the hash, in
// pre-order (the load creates a parent before its children).
//
// Recreated (bound) types, their handlers and input plugs:
// - VuUIScreenEntity (vtable 0x6F7140): ScreenEnter 0x629A70 / ScreenExit
//   0x629B00 / tick 0x629B90 (c_ui_screen); it registers no handler of its own.
// - VuUIContainerEntity (ctor 0x62F880, vtable 0x6F74F0): "OnUITick" 0x62F7A0
//   and "OnUIEvent" 0x62F7C0, each 0x5011A0 of the same name and params to its
//   own DIRECT children (disassembled).
// - VuBarracudaUIButtonEntity (0x73B7B0): c_ui_button (OnUISetFocus 0x442A40,
//   OnUITick 0x442A90, OnUIEvent 0x442D60; inputs SetFocus / SetStringID /
//   Enable / Disable / IsEnabled), its fields from OUR loaded Enabled / Label /
//   Can Have Focus / String ID, its id = FNV-1a of the long name (0x500D50).
// - VuUIEventEntity (0x6F7308): c_ui_event (OnUIEvent 0x62E950 -> Trigger).
// - VuIfEntity (0x754CD4): input Trigger 0x5DDE60 (disassembled): fire the
//   "Operand" output with an EMPTY VuParams; when its VuRetVal type (+0) is 3,
//   fire "True" when the value's LOW BYTE (+4) is non-zero else "False", both
//   with the Trigger's own params; the handler writes only +0 = 0 of its
//   VuRetVal (+4 is the caller's: UNKNOWN - m_known false).
// - VuInfoUITextEntity (0x73BC5C): the ticker (ticker.hxx: OnSetInfoStringID
//   0x446480, OnClearInfoStringID 0x446450, OnUITick 0x446580 - its delta is the
//   tick manager's, a hook), Screen Scale / Width from OUR loaded properties, the
//   font (0x626B80's answer) an input.
// - VuSetInfoStringUIAction (0x73E818): Trigger -> slot 12 0x471E30
//   (disassembled): a VuParams with the "Info String ID" string record (only
//   while strlen + 1 + 4 <= 0x80) broadcast as "OnSetInfoStringID"; its VuRetVal
//   +0 = 0 (+4 the caller's).
// - the screen actions and VuSetGameModeUIAction (wiring.hxx / action.hxx).
// - the four VuSliderEntity subclasses (slider.hxx): OnUISetFocus 0x46F8F0,
//   OnUITick 0x46F940, OnUIEvent 0x46FCC0; inputs Show / Hide / SetFocus; the
//   settings manager, the touch device and the FMOD categories are hooks.
// - batch 19: VuGameConfigEntity / VuBoatConfigEntity (see ui_bound_config_t),
//   VuShortUnlockUITextEntity 0x445570, VuBoatStatEntity "Set" 0x44FCA0, and
//   VuUISubtitlesSettingEntity (chooser.hxx: slots 13..16, 0x475B40).
// - batch 21: VuUserNextUIAction's Trigger (slot 12 0x470E50), and the
//   gameInitialize / gameRelease walk for the Screens (project_initialize /
//   project_release, k_ui_game_slots: the screen, button and subtitles slots).
// Anything else: an input plug reached is UNKNOWN (counted; its VuRetVal is
// the m_answer hook's - e.g. VuLicenseEntity's IsFullVersion, a platform
// service); a registered handler not recreated (k_ui_event_facts) is entered
// as UNKNOWN (counted, traced, nothing runs).

// The event-map rows of the types whose handlers are NOT recreated here, as
// retail's ctors registered them on the loaded Screens projects (case
// ui_frames' census of every entity's VuEventMap: vtable, hash, method).
struct ui_event_fact_t
{
    std::uint32_t m_vtable;
    std::uint32_t m_hash;
    std::uint32_t m_method;
};

inline constexpr ui_event_fact_t k_ui_event_facts[] = {
    { 0x738614u, 0x407F5DB0u, 0x4232B0u },  // VuSysNotifyEntity
    { 0x756828u, 0xD5127E2Du, 0x5EFA60u },  // VuOnProjectInitializedEntity
};

inline constexpr std::uint32_t k_ui_screen_vtable = 0x6F7140u;
inline constexpr std::uint32_t k_ui_container_vtable = 0x6F74F0u;
inline constexpr std::uint32_t k_ui_button_vtable = 0x73B7B0u;
inline constexpr std::uint32_t k_ui_event_vtable = 0x6F7308u;
inline constexpr std::uint32_t k_ui_if_vtable = 0x754CD4u;
inline constexpr std::uint32_t k_ui_set_string_vtable = 0x754E7Cu;   // VuSetStringEntity (ctor 0x5E3640)
inline constexpr std::uint32_t k_ui_info_vtable = 0x73BC5Cu;
inline constexpr std::uint32_t k_ui_page_vtable = 0x6F72C4u;
inline constexpr std::uint32_t k_ui_results_vtable = 0x73DA38u;
inline constexpr std::uint32_t k_ui_champ_vtable = 0x73C998u;
inline constexpr std::uint32_t k_ui_remap_vtable = 0x73D4F4u;
// Screens/Award's leaves: text bases over +0x10C (ctors 0x443870, 0x446D30, 0x446F00, 0x447060), image bases over +0xB8 (0x440B80, 0x446AE0).
inline constexpr std::uint32_t k_ui_award_credit_vtable = 0x73B86Cu, k_ui_award_trophy_text_vtable = 0x73C18Cu, k_ui_award_cat_text_vtable = 0x73C25Cu, k_ui_award_info_text_vtable = 0x73C32Cu;
inline constexpr std::uint32_t k_ui_award_image_vtable = 0x73B5DCu, k_ui_award_trophy_image_vtable = 0x73C0BCu;
// Screens/Loading*'s leaves (loading.hxx): slot 6 computes +0x10C (the tuning's +0x110), slot 12 hands it over. The track image: 0x440400 / 0x440830.
inline constexpr std::uint32_t k_ui_event_text_vtable = 0x73BFDCu, k_ui_penalty_text_vtable = 0x73BF9Cu, k_ui_track_text_vtable = 0x73BF1Cu, k_ui_tuning_text_vtable = 0x73BEDCu,
                               k_ui_unlock_text_vtable = 0x73BE9Cu, k_ui_track_image_vtable = 0x73B4FCu;
inline constexpr std::uint32_t k_ui_throttle_remap_vtable = 0x73D45Cu;
inline constexpr std::uint32_t k_ui_set_info_vtable = 0x73E818u;

// One step of the frame's trace, at the points retail's case logs them.
enum class ui_trace_kind_t
{
    fire,       // an output plug's 0x5023A0 entered: owner, plug, params
    input,      // an input plug's 0x404BE0 entered: owner, plug, params, recreated
    broadcast,  // 0x505690 entered: hash, params
    sound,      // FMOD getEvent: the name
    event,      // a registered handler not recreated: owner, its method
    string,     // VuStringDB slot 7 (the ticker's hook): the id
    measure,    // the font measure 0x5373D0 (the ticker's hook): the text, xmm3 bits, the font
    touches,    // the touch device's count (slot 6, the slider's hook)
    touch,      // the touch device's point (slot 7): the index
    category,   // FMOD getCategory (the volume sliders' hook): the name
    hook,       // the HUD's gameInitialize / gameRelease hooks: the label, the entity
    query,      // the input manager's slot 9 (the pause tick's "Pause"): the name, the answer
    button,     // the input manager's vt+0x44 (an on-screen button): the name
    axis,       // the input manager's vt+0x48 (an on-screen axis): the name, the value bits
    profile,    // a platform answer the config selectors ask (m_platform): "tag|text|..."
    game,       // batch 21: an entity's gameInitialize slot 6 / gameRelease slot 7 entered (0x500B10 /
                // 0x500B80): the owner, the retail address, recreated (k_ui_game_slots)
};

// Batch 21: every entity type of the 68 Screens / HUDs projects whose slot 6
// (gameInitialize) or slot 7 (gameRelease) is not the nullsub 0x401560 (tools
// tmp/b21/allslots.py over the vtables; every component type there - VuScript /
// VuUIDraw / Vu2dLayout / Vu3dDraw / VuTransform / VuControlComponent - has
// nullsubs at slots 9 / 10). Recreated in game_slot( ): VuUIScreenEntity
// 0x629A70 / 0x629B00 (c_ui_screen enter / exit), VuBarracudaUIButtonEntity
// 0x4426E0 / 0x4253D0, VuUISubtitlesSettingEntity 0x46D7A0 / 0x4253D0 (and the
// race HUD's pause / recover / toast / multi delay rows in game_initialize, with
// their own traces). Every other row is entered as UNKNOWN (traced, counted).
struct ui_game_slot_t
{
    std::uint32_t m_vtable;
    std::uint32_t m_init;     // slot 6
    std::uint32_t m_release;  // slot 7
};

inline constexpr std::uint32_t k_ui_nullsub = 0x401560u;

inline constexpr ui_game_slot_t k_ui_game_slots[] = {
    { 0x6F7140u, 0x629A70u, 0x629B00u },  // VuUIScreenEntity
    { 0x6F71ACu, 0x425390u, 0x4253D0u },  // VuUITouchButtonEntity
    { 0x6F72C4u, 0x62D9A0u, 0x62DA10u },  // VuUIPageLayoutEntity
    { 0x737104u, 0x401560u, 0x405090u },  // VuCheatEntity
    { 0x7375E4u, 0x405D70u, 0x405DA0u },  // VuSignInEntity
    { 0x7385E0u, 0x423560u, 0x423610u },  // VuOnAnyButtonOrKeyEntity
    { 0x738AC4u, 0x425390u, 0x4253D0u },  // VuHUDPauseButtonEntity
    { 0x738B24u, 0x425390u, 0x4253D0u },  // VuHUDRecoverButtonEntity
    { 0x738BDCu, 0x4245F0u, 0x401560u },  // VuHUDHiddenPackageToastEntity
    { 0x73B4FCu, 0x440400u, 0x440830u },  // VuBarracudaTrackUIImageEntity
    { 0x73B7B0u, 0x4426E0u, 0x4253D0u },  // VuBarracudaUIButtonEntity
    { 0x73BE9Cu, 0x444D80u, 0x401560u },  // VuBarracudaUnlockUITextEntity
    { 0x73BEDCu, 0x4448C0u, 0x401560u },  // VuBarracudaTuningUITextEntity
    { 0x73BF1Cu, 0x4445F0u, 0x401560u },  // VuBarracudaTrackUITextEntity
    { 0x73BF9Cu, 0x444160u, 0x401560u },  // VuBarracudaSlalomPenaltyUITextEntity
    { 0x73BFDCu, 0x443EB0u, 0x401560u },  // VuBarracudaEventUITextEntity
    { 0x73C69Cu, 0x44CD10u, 0x44D090u },  // VuBoatConfigEntity (config_setup( ) runs its slot 6)
    { 0x73C998u, 0x451370u, 0x451540u },  // VuChampionshipEntity
    { 0x73CC34u, 0x4595A0u, 0x401560u },  // VuChampionshipCompletionUITextEntity
    { 0x73CC74u, 0x459060u, 0x401560u },  // VuTimeTrialCompletionUITextEntity
    { 0x73CCB4u, 0x458A40u, 0x401560u },  // VuSlalomCompletionUITextEntity
    { 0x73CCF4u, 0x458500u, 0x401560u },  // VuRaceCompletionUITextEntity
    { 0x73D1DCu, 0x45D050u, 0x45D080u },  // VuGameConfigEntity (config_setup( ) runs its slot 6)
    { 0x73D45Cu, 0x461200u, 0x461290u },  // VuThrottleRemappingEntity
    { 0x73D4F4u, 0x461200u, 0x461290u },  // VuInputRemappingEntity
    { 0x73D724u, 0x463220u, 0x401560u },  // VuLoadingProgressBarEntity
    { 0x73D7E0u, 0x401560u, 0x464060u },  // VuMessageBoxEntity
    { 0x73D8E4u, 0x466C60u, 0x466D80u },  // VuMessageBoxScreenEntity
    { 0x73DA38u, 0x469B10u, 0x401560u },  // VuRaceResultsTableEntity (results.hxx ui_results_initialize, given m_root)
    { 0x73DAA8u, 0x46BE00u, 0x46BE10u },  // VuSellScreen
    { 0x73DE6Cu, 0x46DE30u, 0x4253D0u },  // VuUIFlipIntervalSettingEntity
    { 0x73DED4u, 0x46DA70u, 0x4253D0u },  // VuUIFidelitySettingEntity
    { 0x73DF3Cu, 0x46D7A0u, 0x4253D0u },  // VuUISubtitlesSettingEntity
    { 0x73DFA4u, 0x46D3E0u, 0x4253D0u },  // VuUIUnitsSettingEntity
    { 0x73E0BCu, 0x46CD70u, 0x4253D0u },  // VuVibrationSettingsEntity
    { 0x73E15Cu, 0x46C780u, 0x4253D0u },  // VuTiltSteeringSettingsEntity
    { 0x73F2C8u, 0x479C50u, 0x479CF0u },  // VuUILeaderboardEntity
    { 0x73F408u, 0x47DAB0u, 0x47DB10u },  // VuUIMiniLeaderboardEntity
    { 0x754B68u, 0x5E0610u, 0x43A420u },  // VuMultiDelayEntity
    { 0x754B9Cu, 0x5DFAF0u, 0x43A420u },  // VuDelayEntity
};

// VuUserNextUIAction (vtable 0x73EA10): Trigger -> slot 12 0x470E50 (results.hxx ui_user_next).
inline constexpr std::uint32_t k_ui_user_next_vtable = 0x73EA10u;

struct ui_trace_t
{
    ui_trace_kind_t m_kind;
    int m_index = -1;          // the owner's pre-order index
    std::string m_name;        // plug / sound name
    std::uint32_t m_word = 0;  // broadcast hash / event method
    ui_params_t m_params;
    bool m_known = true;
    std::uint32_t m_font = 0;  // measure: the font
};

// The race HUD (batch 16, census case hud_census on Race Pre / Game / PostGameHUD).
// Bound only when ui_bind_hooks_t::m_hud is set (the HUD driver, core/ui
// overlay.hxx) - except, since batch 18, the inputs that read no game state:
// the text bases' / images' Show / Hide / SetAlpha / SetStringID and
// VuTriggerForwarderEntity's Trigger run in every binding (case ui_frames runs
// them live in retail on the Screens projects). Types, handlers and inputs
// (all disassembled):
// - the text base (VuUITextBaseEntity ctor 0x627070) of VuHUDElementEntity
//   0x738C64, VuUITextEntity 0x6F7434, VuHUDHiddenPackageToastEntity 0x738BDC:
//   inputs Show 0x446C80 (+0x64 = 1), Hide 0x446FB0 (+0x64 = 0), SetAlpha
//   0x627AA0 (+0x108 = the first record when a float, else 0); VuUITextEntity's
//   SetStringID 0x62F480 (+0x10C = readString 0x505260); VuRetVal +0 = 0.
// - VuUIImageEntity 0x6F7484: Show 0x440B50 / Hide 0x6284C0 (+0x6C), SetAlpha
//   0x629270 (+0xB4, as the text's).
// - the toast: OnPackageFound 0x424600, OnUITick 0x4248E0 (controls.hxx);
//   gameInitialize slot 6 0x4245F0: +0x64 = 0.
// - VuHUDSpeedometerEntity 0x739250: OnUITick (thunk 0x4077E3) -> slot 12
//   0x42B860 (hud.hxx); VuHUDMiniMapEntity 0x738E10: OnUITick -> slot 12
//   0x427B60, which reads the dt record into a local and nothing else.
// - VuOnProjectInitializedEntity 0x756828: OnProjectInitialized 0x5EFA60 fires
//   "Trigger" with an EMPTY VuParams.
// - VuControlMethodEntity 0x7398E4: OnControlMethodChanged 0x42EC80 fires
//   "MethodChanged" with the event's params; input Trigger 0x42EAE0: the
//   control-method manager g 0x758FC0 +0x18 (read without a null test) 0 / 1 / 2
//   / 3 fires IsGamePad / IsKeyboard / IsTilt / IsTouch with the Trigger's
//   params, any other value nothing; VuRetVal +0 = 0.
// - VuTriggerForwarderEntity 0x754B00: Trigger 0x5E0FC0 fires "OnTrigger" with
//   its params (findPlug; nothing when absent); VuRetVal +0 = 0.
// - VuIsCircuitTrack 0x73994C: Get 0x42E1F0 = { 3, track manager g 0x758FF4
//   slot 8 (built) && slot 9 (circuit) } (the byte +4 only).
// - VuStartLightsOutAction 0x737664: Trigger -> slot 12 0x405470: the broadcast
//   "OnStartLightsOut" with an empty VuParams; VuRetVal +0 = 0.
// - the on-screen axis / button 0x739020 / 0x739058: OnUITick 0x429E10
//   (controls.hxx ui_hud_onscreen_tick); the pause / recover buttons 0x738AC4 /
//   0x738B24: OnUITick through the thunk 0x4077E3 -> slot 12 0x4255B0
//   (ui_hud_button_tick: pause slots 13 0x4261B0 / 14 0x4262A0, recover 13
//   0x425DC0 / 14 0x425F10), their touch callbacks 0x425400 / 0x425490 /
//   0x4254B0 (hud_touch - the device's dispatch is the caller's) and the
//   callback list (gameInitialize 0x425390 / gameRelease 0x4253D0 over
//   m_touch_device).
// The world's / the audio layer's inputs (VuMultiDelayEntity In 0x5E0660,
// VuAudioEventEntity Start 0x5F42D0 / Stop 0x5F4310) go to the m_external hook.
struct ui_hud_env_t
{
    ui_hud_world_t m_world;               // g 0x758FB8 / 0x758FE0 (hud.hxx)
    bool m_track_built = false;           // g 0x758FF4 slot 8 0x4D0720
    bool m_track_circuit = false;         // slot 9 0x4D0730
    std::int32_t m_control_method = 0;    // g 0x758FC0 +0x18
    // batch 17 (additive): the pause button's visibility reads the boat
    // manager's +0x28 (0x426330); the recover button's reads the control-method
    // manager's presence (0x425F50); its ticks read the local boat's driver.
    std::uint32_t m_manager_28 = 0;
    bool m_method_present = true;         // g 0x758FC0 != 0
    ui_hud_recover_driver_t m_driver;
};

inline constexpr std::uint32_t k_ui_hud_element_vtable = 0x738C64u;
inline constexpr std::uint32_t k_ui_text_vtable = 0x6F7434u;
inline constexpr std::uint32_t k_ui_hud_toast_vtable = 0x738BDCu;
inline constexpr std::uint32_t k_ui_image_vtable = 0x6F7484u;
inline constexpr std::uint32_t k_ui_hud_speedometer_vtable = 0x739250u;
inline constexpr std::uint32_t k_ui_hud_minimap_vtable = 0x738E10u;
inline constexpr std::uint32_t k_ui_project_initialized_vtable = 0x756828u;
inline constexpr std::uint32_t k_ui_control_method_vtable = 0x7398E4u;
inline constexpr std::uint32_t k_ui_trigger_forwarder_vtable = 0x754B00u;
inline constexpr std::uint32_t k_ui_is_circuit_vtable = 0x73994Cu;
inline constexpr std::uint32_t k_ui_start_lights_vtable = 0x737664u;
inline constexpr std::uint32_t k_ui_hud_pause_vtable = 0x738AC4u;
inline constexpr std::uint32_t k_ui_hud_recover_vtable = 0x738B24u;
inline constexpr std::uint32_t k_ui_onscreen_axis_vtable = 0x739020u;
inline constexpr std::uint32_t k_ui_onscreen_button_vtable = 0x739058u;
inline constexpr std::uint32_t k_ui_multi_delay_vtable = 0x754B68u;

// Batch 19: the GameConfig* screens. VuGameConfigEntity (ctor 0x45CC80 over
// VuBaseConfigEntity 0x4471C0, vtable 0x73D1DC) registers "OnUITick" -> the
// thunk 0x4077ED = slot 13 0x448470 (ui_config_tick; dt = the tick manager's,
// m_delta) and "OnUIEvent" -> the thunk 0x404AB2 = slot 14 0x448A50
// (ui_config_input); its input plugs Unlocked / Available / InstalledOnHost /
// NotUsedSP (select.hxx ui_config_plug). config_setup( ) = the load's slot 4
// 0x448120 (ui_game_config_build over m_option_db / m_track_db) then slot 6
// 0x45D050 (ui_game_config_initialize). Bound only with m_platform set: every
// platform answer the selector asks goes through it (traced as profile):
//   "92|name|value" / "96|..." / "100|mode|track|course" / "104|..."  unlock manager g 0x758FE8 slots
//   "license" (g 0x758FAC +0x18), "4D3170" (the store-beta build test)
//   "132|value", "144|asset" (asset factory 0x759004 +0x90)
//   "48|mode|track|course" / "88|championship" / "124|track"  progress manager g 0x758FD8:
//   .first the bool, .second the best placing +48 / +88 write.
// VuShortUnlockUITextEntity (ctor 0x445450, vtable 0x73BDDC): "OnSetShortUnlock
// StringID" 0x445570 (disassembled): +0x10C = VuStringDB slot 6 (m_string_db,
// a HOOK) of the params' first string.
inline constexpr std::uint32_t k_ui_game_config_vtable = 0x73D1DCu;
inline constexpr std::uint32_t k_ui_lock_text_vtable = 0x73BDDCu;

// VuBoatConfigEntity (ctor 0x44C9F0, vtable 0x73C69C) the same way over the
// boat class (dock.hxx): setup = slot 4 (ui_dock_build) + slot 6 0x44CD10
// (ui_dock_initialize); its slot 12 0x44E2B0 fires the four stat plugs into
// VuBoatStatEntity (0x73C808) "Set" 0x44FCA0 (leaves.hxx, bound in every
// binding: +0xB0).
inline constexpr std::uint32_t k_ui_boat_config_vtable = 0x73C69Cu;
inline constexpr std::uint32_t k_ui_boat_stat_vtable = 0x73C808u;
inline constexpr std::uint32_t k_ui_boat_player_vtable = 0x73C654u;  // VuSetBoatConfigPlayerEntity (ctor 0x44ECF0): SetSinglePlayer 0x44EF10, SetIndex 0x44EEA0
inline constexpr std::uint32_t k_ui_loading_bar_vtable = 0x73D724u;  // VuLoadingProgressBarEntity (leaves.hxx)

struct ui_bound_config_t
{
    bool m_boat = false;
    ui_dock_t m_dock;             // the boat selector (m_dock.m_base its base)
    ui_config_entity_t& base( ) { return m_boat ? m_dock.m_base : m_entity; }
    ui_config_entity_t m_entity;
    ui_game_config_t2 m_game;
    ui_config_nav_t m_nav;
    ui_config_env_t m_env;
    value_t m_doc;                // slot 4's options document (+0x2A0)
    ui_config_class_t m_class;
    ui_profile_t m_profile;
    ui_config_hooks_t m_hooks;
    std::string m_option_type;
    std::uint32_t m_flags = 2;    // entity +0x14
};

// A text base's / an image's runtime fields the HUD's inputs drive.
struct ui_hud_shown_t
{
    bool m_visible = true;     // +0x64 (image +0x6C) "Visible"
    std::string m_award;       // Screens/Award's leaves: text +0x10C, image: the asset of +0xB8 ("": none)
    float m_alpha = 1.0f;      // +0x108 (image +0xB4)
    std::string m_string_id;   // VuUITextEntity +0x10C "String ID"
};

struct ui_bound_t
{
    entity_t* m_entity = nullptr;
    int m_index = 0;
    std::uint32_t m_vtable = 0;
    ui_event_map_t m_map;
    std::vector< std::pair< std::uint32_t, std::uint32_t > > m_methods;  // (hash, retail method) as registered
    ui_button_hooks_t m_button_hooks;
    std::unique_ptr< c_ui_button > m_button;
    std::unique_ptr< c_ui_event > m_event;
    std::unique_ptr< ui_info_ticker_t > m_ticker;
    std::unique_ptr< ui_completion_t > m_completion;
    ui_slider_hooks_t m_slider_hooks;
    std::unique_ptr< c_ui_slider > m_slider;
    // the race HUD's (m_hud set)
    std::unique_ptr< ui_hud_shown_t > m_shown;              // text base / image
    std::unique_ptr< c_ui_hud_element > m_element;          // VuHUDElementEntity's Type / text
    std::unique_ptr< ui_hud_toast_t > m_toast;
    std::unique_ptr< c_ui_hud_speedometer > m_speedometer;
    std::unique_ptr< ui_hud_button_t > m_hud_button;        // pause / recover
    std::unique_ptr< ui_hud_onscreen_t > m_onscreen;        // on-screen axis / button
    // batch 19
    std::unique_ptr< ui_bound_config_t > m_config;          // VuGameConfigEntity
    std::unique_ptr< std::string > m_lock_text;             // VuShortUnlockUITextEntity +0x10C
    ui_combo_box_hooks_t m_combo_hooks;
    std::unique_ptr< c_ui_combo_box > m_combo;              // VuUISubtitlesSettingEntity (chooser.hxx)
    std::unique_ptr< ui_boat_stat_t > m_stat;               // VuBoatStatEntity +0xB0
    std::unique_ptr< ui_progress_bar_t > m_bar;             // batch 32: VuLoadingProgressBarEntity +0x84 / +0x88
    std::uint8_t m_bar_initially = 1;                       // +0x80 "Initially Visible"
    ui_config_combo_t m_combo_record;                       // batch 32: the settings combo's record +0x278 as the draw reads it (combo( e ))
    // batch 22: VuUIMiniLeaderboardEntity (board.hxx; bound when m_leaderboard is set)
    ui_mini_board_hooks_t m_board_hooks;
    std::unique_ptr< c_ui_mini_board > m_board;
    bool m_board_ticking = false;                           // "Decision" registered (slot 6) and not yet unregistered (slot 7)
    // batch 23: VuUILeaderboardEntity (ladder.hxx; bound when m_ladder is set)
    ui_leaderboard_hooks_t m_ladder_hooks;
    std::unique_ptr< c_ui_leaderboard > m_ladder;
    bool m_ladder_ticking = false;                          // "Decision" 0x47B640 registered (slot 6) and not yet unregistered (slot 7)
    // batch 28: VuUIPageLayoutEntity (pages.hxx) and the remapping rows (remap.hxx); the frame's m_page / m_remap read these
    std::unique_ptr< ui_page_layout_t > m_page;
    std::unique_ptr< ui_remap_t > m_remap;
    ui_remap_runtime_t m_remap_state;
    ui_remap_hooks_t m_remap_hooks;
    bool m_remap_ticking = false;                           // "Decision" 0x4623D0 registered (slot 6 0x461200) and not yet unregistered (slot 7 0x461290)
    // batch 29: VuRaceResultsTableEntity (results.hxx): the frame's m_results reads this
    std::unique_ptr< ui_results_table_t > m_results;
    // batch 30: VuChampionshipEntity (standings.hxx): the frame's m_champ reads this
    std::unique_ptr< ui_champ_state_t > m_champ;
};

struct ui_bind_hooks_t
{
    // batch 30: what VuChampionshipEntity's slots reach (standings.hxx); null: its slots 6 / 7 stay counted in m_unknown_slots.
    const ui_champ_env_t* m_champ = nullptr;
    // HOOK: the input manager g 0x759038 slot 9: (pad, name) -> down.
    std::function< bool( int, const char* ) > m_input;
    // HOOK: an input plug not recreated - its VuRetVal (default { 0, 0 }).
    std::function< ui_ret_t( const entity_t&, const char* ) > m_answer;
    value_t* m_root = nullptr;  // gameutil +80
    // The ticker's: VuStringDB slot 7 and the font measure 0x5373D0 (HOOKS, ticker.hxx),
    // the tick manager's delta (g 0x759008 slot 15, a HOOK) and the font 0x626B80
    // answers for an entity (an input: the fonts are the rendering layer's).
    ui_info_hooks_t m_info;
    std::function< float( ) > m_delta;
    std::function< std::uint32_t( const entity_t& ) > m_font;
    // Preferred over m_info.m_measure when set: 0x5373D0 needs the entity's own font params (+0xA0).
    std::function< float( const entity_t&, const std::string&, float ) > m_measure_of;
    // 0x4629D0: the throttle row's slot 17: the setting of Config 0 / 1.
    std::function< void( std::int32_t config, std::uint8_t value ) > m_remap_auto;
    // Screens/Loading*'s slot 6 (ui_loading_text) and the track image's 0x440400. Null: those slots stay counted in m_unknown_slots.
    std::function< void( std::uint32_t vt, std::int32_t place, std::string& out ) > m_loading;
    std::function< std::string( ) > m_loading_image;
    // The asset factory's test 0x4269D0 on a texture name (0x440CC0). Null: every name exists.
    std::function< bool( const char* ) > m_texture_exists;
    // HOOK: the license manager g 0x758FAC vt+0x18 (footer.hxx). Null: VuLicenseEntity stays an UNKNOWN input.
    std::function< bool( ) > m_trial;
    // The sliders': HOOKS the touch device g 0x759070 (slot 6 count, slot 7 point),
    // an input the inverse crop matrix (VuUIImpl +76; null = identity), the settings
    // manager g 0x758FE0 (null = none: the getters' defaults).
    std::function< int( ) > m_touch_count;
    std::function< void( int, float[ 2 ] ) > m_touch_point;
    const float* m_inverse = nullptr;
    ui_settings_t* m_settings = nullptr;
    // Batch 24: the other five settings combos (units / fidelity / flip interval /
    // vibration / tilt steering - chooser.hxx initialize_settings / apply_settings)
    // are bound only when this is set (the platform calls their slots 6 / 12 make
    // and the game state the fidelity setter 0x4B50A0 writes); null = unbound, their
    // slot 6 / 7 rows entered as UNKNOWN as before.
    ui_settings_platform_t* m_settings_platform = nullptr;
    // The race HUD's (null: not bound - see ui_hud_env_t): the game it reads,
    // VuStringDB slot 6 (the toast's template - a HOOK), the inputs owned by the
    // world / the audio layer (multi delay In, audio Start / Stop: entity, plug,
    // params -> VuRetVal; unset = { 0, 0 }) and the broadcasts' other
    // subscribers (the race's: VuRaceGame / the drivers registered theirs before
    // the HUD loaded, so they run first).
    const ui_hud_env_t* m_hud = nullptr;
    std::function< std::string( const char* ) > m_string_db;
    std::function< ui_ret_t( entity_t&, const char*, const ui_params_t& ) > m_external;
    // Batch 28: the page's load 0x62E3B0 (PageLayoutDB, FontDB, the measure / factory hooks; null m_page_db: an empty page) and the rows'
    // devices (remap.hxx; m_set_focus / m_nav are the binding's own).
    const value_t* m_page_db = nullptr;
    const value_t* m_font_db = nullptr;
    const value_t* m_dev_game_data = nullptr;   // batch 29: g 0x75901C slot 6 "ActionGameData" (0x469B10), null: none
    ui_page_env_t m_page_env;
    ui_page_hooks_t m_page_hooks;
    ui_remap_hooks_t m_remap_hooks;
    std::function< void( std::uint32_t, const ui_params_t& ) > m_broadcast_out;
    // The race HUD's controls (batch 17): the touch device's callback list
    // (null: no list - the pause tick finds nothing), the crop matrix the touch
    // rects are anchored over (null: ui_crop_t's default), the input manager's
    // vt+0x44 / vt+0x48 (HOOKS), the FMOD event start (HOOK; traced as sound
    // either way) and the recover press's driver +0xB8 = 1 (the world's).
    ui_touch_device_t* m_touch_device = nullptr;
    const ui_crop_t* m_crop = nullptr;
    std::function< void( int, const char* ) > m_input_button;
    std::function< void( int, const char*, float ) > m_input_axis;
    std::function< void( const char* ) > m_sound;
    std::function< void( const char*, float ) > m_category;   // FMOD getCategory + setVolume (the sliders; HOOK)
    std::function< void( ) > m_recover;
    // The GameConfig* screens (batch 19; see ui_bound_config_t): the platform
    // answers (null: VuGameConfigEntity not bound), the OptionDB / TrackDB
    // documents (gameutil +52 / +0x10), the entity flags +0x14 the load leaves.
    std::function< std::pair< bool, int >( const std::string& ) > m_platform;
    const value_t* m_option_db = nullptr;
    const value_t* m_track_db = nullptr;
    std::uint32_t m_config_flags = 2;
    // the boat selector's: BoatDB / SkinDB / ChampDB (gameutil +8 / +12 / +20)
    const value_t* m_boat_db = nullptr;
    const value_t* m_skin_db = nullptr;
    const value_t* m_champ_db = nullptr;
    // Batch 22: the platform leaderboard manager g 0x758FA8 (HOOKS, an online service: m_read / m_status /
    // m_count / m_row of board.hxx's hooks; the binding supplies the tick manager's register (traced
    // "board_tick|register|Decision"), m_delta and m_root itself). Null: VuUIMiniLeaderboardEntity not bound.
    const ui_mini_board_hooks_t* m_leaderboard = nullptr;
    // Batch 23: the same manager for the full leaderboard (ladder.hxx's m_read / m_status / m_total and the
    // OptionDB at gameutil +0x34; the binding supplies the tick register (traced "ladder_tick|register|Decision"),
    // the touch add / remove (m_touch_device, priority 0), m_delta and the sound). Null: VuUILeaderboardEntity not bound.
    const ui_leaderboard_hooks_t* m_ladder = nullptr;
    // VuGameConfigEntity's model half (world/dock.hxx): 0x520080 on its +0x304, and its draw object's add / remove (+0x324 passed by address). Unset: traced only.
    std::function< void( const entity_t&, const char* model ) > m_trophy_model;
    std::function< void( const entity_t&, bool added, const bool* shown ) > m_trophy_draw;
};

// An entity's own property slot (its loaded ui_state_t), or null.
inline const ui_slot_t* ui_bind_slot( const entity_t& e, const char* name )
{
    return ui_state_slot( e, name );
}

// Batch 25: VuBarracudaUIButtonEntity's touch rect fields (button.hxx
// ui_button_touch_t) from OUR loaded properties (the ctor's defaults otherwise).
inline ui_button_touch_t ui_button_touch_of( const entity_t& e )
{
    ui_button_touch_t t;
    const auto bits = [ &e ]( const char* name, void* out, std::size_t n )
    {
        if ( const ui_slot_t* s = ui_bind_slot( e, name ) )
            std::memcpy( out, s->m_bits, n );
    };
    bits( "Screen Scale", t.m_scale, 8 );
    bits( "X", &t.m_xy[ 0 ], 4 );
    bits( "Y", &t.m_xy[ 1 ], 4 );
    bits( "TouchX", &t.m_touch[ 0 ], 4 );
    bits( "TouchY", &t.m_touch[ 1 ], 4 );
    bits( "TouchWidth", &t.m_touch[ 2 ], 4 );
    bits( "TouchHeight", &t.m_touch[ 3 ], 4 );
    bits( "Horizontal Anchor", &t.m_anchor.m_horizontal, 4 );
    bits( "Vertical Anchor", &t.m_anchor.m_vertical, 4 );
    bits( "Horizontal Ratio", &t.m_anchor.m_h_ratio, 4 );
    bits( "Vertical Ratio", &t.m_anchor.m_v_ratio, 4 );
    return t;
}

// 0x500BF0: the ancestors' names and its own, root first, joined by "/".
inline std::string ui_long_name( const entity_t& e )
{
    return e.m_parent ? ui_long_name( *e.m_parent ) + "/" + e.m_name : e.m_name;
}

class c_ui_binding
{
public:
    c_ui_binding( entity_t& root, ui_bind_hooks_t hooks ) : m_hooks( std::move( hooks ) )
    {
        const auto walk = [ this ]( const auto& self, entity_t& e ) -> void
        {
            auto b = std::make_unique< ui_bound_t >( );
            b->m_entity = &e;
            b->m_index = static_cast< int >( m_bound.size( ) );
            b->m_vtable = e.m_ctor ? e.m_ctor->m_vtable : 0u;
            m_index[ &e ] = b->m_index;
            m_bound.push_back( std::move( b ) );
            for ( auto& child : e.m_children )
                self( self, *child );
        };
        walk( walk, root );
        m_hud_hooks.m_string = m_hooks.m_string_db;
        for ( auto& b : m_bound )
            bind( *b );
        m_wiring.m_root = m_hooks.m_root;
        m_wiring.m_broadcast = [ this ]( std::uint32_t hash, const ui_params_t& p ) { broadcast( hash, p ); };
        m_wiring.m_on_fire = [ this ]( const entity_t& owner, int plug, const ui_params_t& p )
        {
            trace( ui_trace_kind_t::fire, index_of( owner ), plug_name( owner, plug ), 0u, p, true );
        };
        m_wiring.m_on_input = [ this ]( const entity_t& owner, const char* plug, const ui_params_t& p, bool known )
        {
            trace( ui_trace_kind_t::input, index_of( owner ), plug, 0u, p, known );
        };
        m_wiring.m_bound_known = [ this ]( const entity_t& owner, const char* plug ) { return bound_input( owner, plug ); };
        m_wiring.m_bound_run = [ this ]( entity_t& owner, const char* plug, const ui_params_t& p ) { return run_input( owner, plug, p ); };
        m_wiring.m_answer = m_hooks.m_answer;
        if ( m_hooks.m_external )
            m_wiring.m_answer_params = m_hooks.m_external;
        m_screen.m_input = m_hooks.m_input;
        m_info.m_string = [ this ]( const std::string& id )
        {
            trace( ui_trace_kind_t::string, -1, id, 0u, ui_params_t( ), true );
            return m_hooks.m_info.m_string ? m_hooks.m_info.m_string( id ) : std::string( );
        };
        m_info.m_measure = [ this ]( std::uint32_t font, const std::string& text, float aspect )
        {
            std::uint32_t bits;
            std::memcpy( &bits, &aspect, 4 );
            trace( ui_trace_kind_t::measure, -1, text, bits, ui_params_t( ), true );
            m_trace.back( ).m_font = font;
            return m_hooks.m_info.m_measure ? m_hooks.m_info.m_measure( font, text, aspect ) : 0.0f;
        };
        for ( auto& child : root.m_children )
            m_screen.m_children.push_back( &m_bound[ static_cast< std::size_t >( m_index[ child.get( ) ] ) ]->m_map );
    }

    void enter( ) { m_screen.screen_enter( ); }
    void exit( ) { m_screen.screen_exit( ); }
    void tick( float dt, bool input ) { m_screen.tick( dt, input ); }

    // The GameConfig* screens' load + gameInitialize (see ui_bound_config_t):
    // slot 4 0x448120 then slot 6 0x45D050 on every bound selector, in order.
    void config_setup( )
    {
        for ( auto& b : m_bound )
            if ( b->m_config )
            {
                ui_bound_config_t& c = *b->m_config;
                const bool ok = c.m_boat ? ui_dock_build( c.m_dock, c.m_env, c.m_doc, m_hooks.m_option_db, c.m_option_type, c.m_flags, c.m_profile, c.m_hooks ) &&
                                               ui_dock_initialize( c.m_dock, c.m_env, c.m_doc, c.m_profile, c.m_hooks )
                                         : ui_game_config_build( c.m_entity, c.m_env, c.m_doc, m_hooks.m_option_db, c.m_option_type, m_hooks.m_track_db, c.m_flags,
                                                                 c.m_profile, c.m_hooks ) &&
                                               ui_game_config_initialize( c.m_entity, c.m_game, c.m_env, c.m_doc, c.m_profile, c.m_hooks );
                if ( !ok )
                    ++m_config_faults;
            }
    }

    std::vector< std::unique_ptr< ui_bound_t > > m_bound;  // pre-order
    std::vector< ui_trace_t > m_trace;
    int m_unknown_events = 0;  // registered handlers entered that are not recreated
    int m_config_faults = 0;   // a selector path that returned "not recreated"
    c_ui_wiring m_wiring;
    c_ui_screen m_screen;

private:
    void trace( ui_trace_kind_t kind, int index, std::string name, std::uint32_t word, const ui_params_t& p, bool known )
    {
        ui_trace_t t;
        t.m_kind = kind;
        t.m_index = index;
        t.m_name = std::move( name );
        t.m_word = word;
        t.m_params = p;
        t.m_known = known;
        m_trace.push_back( std::move( t ) );
    }

    int index_of( const entity_t& e ) const
    {
        const auto found = m_index.find( &e );
        return found == m_index.end( ) ? -1 : found->second;
    }

    static std::string plug_name( const entity_t& owner, int plug )
    {
        const script_component_t* sc = script_of( const_cast< entity_t& >( owner ) );
        if ( !sc || plug < 0 || static_cast< std::size_t >( plug ) >= sc->m_plugs.size( ) )
            return "?";
        return sc->m_plugs[ static_cast< std::size_t >( plug ) ].m_row->m_name;
    }

    static int plug_index( entity_t& owner, const char* name )
    {
        const script_component_t* sc = script_of( owner );
        for ( std::size_t k = 0; sc && k < sc->m_plugs.size( ); ++k )
            if ( std::strcmp( sc->m_plugs[ k ].m_row->m_name, name ) == 0 )
                return static_cast< int >( k );
        return -1;
    }

    // VuScriptComponent findPlug 0x504770 + the plug's slot 2.
    ui_ret_t fire_named( entity_t& owner, const char* plug, const ui_params_t& p )
    {
        return m_wiring.fire( owner, plug_index( owner, plug ), p );
    }

    void add( ui_bound_t& b, const char* event, std::uint32_t method, std::function< void( const ui_params_t& ) > fn )
    {
        b.m_map.add( event, std::move( fn ) );
        b.m_methods.push_back( { fsm_hash( event ), method } );
    }

    static bool shown_vtable( std::uint32_t vt )
    {
        return vt == k_ui_hud_element_vtable || vt == k_ui_text_vtable || vt == k_ui_hud_toast_vtable || vt == k_ui_image_vtable || award_vtable( vt ) || loading_vtable( vt );
    }

    static bool loading_vtable( std::uint32_t vt )
    {
        return vt == k_ui_event_text_vtable || vt == k_ui_penalty_text_vtable || vt == k_ui_track_text_vtable || vt == k_ui_tuning_text_vtable || vt == k_ui_unlock_text_vtable ||
               vt == k_ui_track_image_vtable;
    }

    static bool award_vtable( std::uint32_t vt )
    {
        return vt == k_ui_award_credit_vtable || vt == k_ui_award_trophy_text_vtable || vt == k_ui_award_cat_text_vtable || vt == k_ui_award_info_text_vtable ||
               vt == k_ui_award_image_vtable || vt == k_ui_award_trophy_image_vtable;
    }

    // The six leaves' listeners (footer.hxx).
    // 0x440CC0: the old texture released; a non-empty name the factory has (0x4269D0): created into +0xB8, +0x6C = 1.
    void bind_award( ui_bound_t& b )
    {
        ui_hud_shown_t* s = b.m_shown.get( );
        const std::uint32_t vt = b.m_vtable;
        if ( vt == k_ui_award_credit_vtable )
        {
            s->m_award = "0";   // the ctor's 0x6F36D4
            add( b, "OnCreditsChanged", 0x443930u, [ this, s ]( const ui_params_t& p ) {
                ui_params_reader_t r( p );
                const std::int32_t value = ui_params_read_int( r ), earned = ui_params_read_int( r );
                s->m_award = ui_award_credit_text( value, earned, [ this ]( const char* id ) { return m_hooks.m_string_db ? m_hooks.m_string_db( id ) : std::string( ); } );
            } );
        }
        else if ( vt == k_ui_award_trophy_text_vtable )
            add( b, "OnSetTrophyStringID", 0x446E00u, [ s ]( const ui_params_t& p ) { ui_params_reader_t r( p ); ui_award_string_id( s->m_award, s->m_visible, r.read_string( ) ); } );
        else if ( vt == k_ui_award_cat_text_vtable )
            add( b, "OnSetUnlockCatStringID", 0x446E00u, [ s ]( const ui_params_t& p ) { ui_params_reader_t r( p ); ui_award_string_id( s->m_award, s->m_visible, r.read_string( ) ); } );
        else if ( vt == k_ui_award_info_text_vtable )
            add( b, "OnSetUnlockInfoStringID", 0x447110u, [ s ]( const ui_params_t& p ) { ui_params_reader_t r( p ); ui_award_info_id( s->m_award, s->m_visible, r.read_string( ) ); } );
        else
            add( b, vt == k_ui_award_image_vtable ? "OnAwardImageChange" : "OnTrophyImageChange", 0x440CC0u, [ this, s ]( const ui_params_t& p ) {
                ui_params_reader_t r( p );
                const char* name = r.read_string( );
                s->m_award.clear( );
                if ( name && *name && ( !m_hooks.m_texture_exists || m_hooks.m_texture_exists( name ) ) )
                {
                    s->m_award = name;
                    s->m_visible = true;
                }
            } );
    }

    static void make_shown( ui_bound_t& b )
    {
        b.m_shown = std::make_unique< ui_hud_shown_t >( );
        if ( const ui_slot_t* s = ui_bind_slot( *b.m_entity, "Visible" ) )
            b.m_shown->m_visible = ( s->m_bits[ 0 ] & 0xFFu ) != 0;
        if ( const ui_slot_t* s = ui_bind_slot( *b.m_entity, "String ID" ) )
            b.m_shown->m_string_id = s->m_text;
    }

    // Batch 18: outside the race HUD (no m_hud - the Screens) the text bases' /
    // images' Show / Hide / SetAlpha / SetStringID and VuTriggerForwarderEntity's
    // Trigger 0x5E0FC0 run too (they read no game state); their event maps hold
    // nothing. The kinds that read ui_hud_env_t stay HUD-only.
    bool bind_shown( ui_bound_t& b )
    {
        if ( shown_vtable( b.m_vtable ) && b.m_vtable != k_ui_hud_element_vtable && b.m_vtable != k_ui_hud_toast_vtable )
        {
            make_shown( b );
            if ( award_vtable( b.m_vtable ) )
                bind_award( b );
            return true;
        }
        // VuGenericEventEntity on a screen (Screens/Award's Show / HideTrophy): the listener 0x5F0420 on the project's own events.
        if ( b.m_vtable == 0x75674Cu )
        {
            entity_t& e = *b.m_entity;
            const ui_slot_t* name = ui_bind_slot( e, "Event Name" );
            const std::string own = name ? name->m_text : std::string( );
            add( b, "OnGenericEvent", 0x5F0420u, [ this, &e, own ]( const ui_params_t& p ) {
                if ( generic_receive( own, p.m_bytes, p.m_size ) )
                    fire_named( e, "OnReceived", ui_params_t( ) );
            } );
            return true;
        }
        // Batch 33: the Screens hold this entity too (Main's license / sign-in branches run off it): as the HUD's, 0x5EFA60 fires "Trigger".
        if ( b.m_vtable == k_ui_project_initialized_vtable && m_hooks.m_trial )
        {
            entity_t& e = *b.m_entity;
            add( b, "OnProjectInitialized", 0x5EFA60u, [ this, &e ]( const ui_params_t& ) { fire_named( e, "Trigger", ui_params_t( ) ); } );
            return true;
        }
        return false;
    }

    static bool shown_input( std::uint32_t vt, const char* plug )
    {
        if ( shown_vtable( vt ) )
            for ( const char* name : { "Show", "Hide", "SetAlpha" } )
                if ( std::strcmp( plug, name ) == 0 )
                    return true;
        // Batch 20: VuShortUnlockUITextEntity's Show / Hide / SetAlpha are the text base's
        // (ctor 0x627070: 0x446C80 / 0x446FB0 / 0x627AA0; its own ctor 0x4454D0 adds none).
        if ( vt == k_ui_lock_text_vtable )
            for ( const char* name : { "Show", "Hide", "SetAlpha" } )
                if ( std::strcmp( plug, name ) == 0 )
                    return true;
        if ( vt == k_ui_text_vtable && std::strcmp( plug, "SetStringID" ) == 0 )
            return true;
        return vt == k_ui_trigger_forwarder_vtable && std::strcmp( plug, "Trigger" ) == 0;
    }

    // The race HUD's types (see ui_hud_env_t); true when `b` is one of them.
    bool bind_hud( ui_bound_t& b )
    {
        entity_t& e = *b.m_entity;
        const std::uint32_t vt = b.m_vtable;
        if ( shown_vtable( vt ) )
            make_shown( b );
        if ( vt == k_ui_hud_element_vtable )
        {
            b.m_element = std::make_unique< c_ui_hud_element >( m_hud_hooks );
            if ( const ui_slot_t* s = ui_bind_slot( e, "Type" ) )
                std::memcpy( &b.m_element->m_type, s->m_bits, 4 );
            return true;
        }
        if ( vt == k_ui_text_vtable || vt == k_ui_image_vtable )
            return true;
        if ( vt == k_ui_hud_toast_vtable )
        {
            b.m_toast = std::make_unique< ui_hud_toast_t >( );
            ui_hud_toast_t& t = *b.m_toast;
            if ( const ui_slot_t* s = ui_bind_slot( e, "Total Time" ) )
                std::memcpy( &t.m_total, s->m_bits, 4 );
            if ( const ui_slot_t* s = ui_bind_slot( e, "Fade Time" ) )
                std::memcpy( &t.m_fade, s->m_bits, 4 );
            ui_bound_t* self = &b;
            // its ctor 0x424360: OnPackageFound 0x424600, OnUITick 0x4248E0 (the base's +0x64 / +0x108 are m_shown's)
            add( b, "OnPackageFound", 0x424600u, [ this, self ]( const ui_params_t& p )
            {
                sync_toast( *self, true );
                ui_hud_toast_found( *self->m_toast, p, [ this ]( const char* key )
                {
                    trace( ui_trace_kind_t::string, -1, key, 0u, ui_params_t( ), true );
                    return m_hooks.m_string_db ? m_hooks.m_string_db( key ) : std::string( );
                } );
                sync_toast( *self, false );
            } );
            add( b, "OnUITick", 0x4248E0u, [ this, self ]( const ui_params_t& p )
            {
                sync_toast( *self, true );
                ui_hud_toast_tick( *self->m_toast, p );
                sync_toast( *self, false );
            } );
            return true;
        }
        if ( vt == k_ui_hud_speedometer_vtable )
        {
            b.m_speedometer = std::make_unique< c_ui_hud_speedometer >( );
            c_ui_hud_speedometer& s = *b.m_speedometer;
            add( b, "OnUITick", 0x4077E3u, [ this, &s ]( const ui_params_t& p ) { s.on_tick( p.m_bytes, p.m_size, m_hooks.m_hud->m_world ); } );
            return true;
        }
        if ( vt == k_ui_hud_minimap_vtable )
        {
            add( b, "OnUITick", 0x4077E3u, []( const ui_params_t& ) { } );   // slot 12 0x427B60: the dt read into a local only
            return true;
        }
        if ( vt == k_ui_project_initialized_vtable )
        {
            add( b, "OnProjectInitialized", 0x5EFA60u, [ this, &e ]( const ui_params_t& ) { fire_named( e, "Trigger", ui_params_t( ) ); } );
            return true;
        }
        if ( vt == k_ui_control_method_vtable )
        {
            add( b, "OnControlMethodChanged", 0x42EC80u, [ this, &e ]( const ui_params_t& p ) { fire_named( e, "MethodChanged", p ); } );
            return true;
        }
        const auto take = [ &e ]( const char* name, void* out, std::size_t bytes )
        {
            if ( const ui_slot_t* s = ui_bind_slot( e, name ) )
                std::memcpy( out, s->m_bits, bytes );
        };
        const auto take_anchor = [ &take ]( ui_anchor_t& a )
        {
            take( "Horizontal Anchor", &a.m_horizontal, 4 );
            take( "Vertical Anchor", &a.m_vertical, 4 );
            take( "Horizontal Ratio", &a.m_h_ratio, 4 );
            take( "Vertical Ratio", &a.m_v_ratio, 4 );
        };
        const char* const touch_names[ 4 ] = { "Touch X", "Touch Y", "Touch Width", "Touch Height" };
        if ( vt == k_ui_hud_pause_vtable || vt == k_ui_hud_recover_vtable )
        {
            b.m_hud_button = std::make_unique< ui_hud_button_t >( );
            ui_hud_button_t& hb = *b.m_hud_button;
            hb.m_recover = vt == k_ui_hud_recover_vtable;
            take( "Screen Scale", hb.m_scale, 8 );
            for ( int k = 0; k < 4; ++k )
                take( touch_names[ k ], &hb.m_touch[ k ], 4 );
            take_anchor( hb.m_anchor );
            if ( hb.m_recover )
            {
                take( "Boing Rate", &hb.m_rate, 4 );
                take( "Boing Scale Min", &hb.m_scale_min, 4 );
                take( "Boing Scale Max", &hb.m_scale_max, 4 );
                take( "Boing Duration", &hb.m_duration, 4 );
            }
            const int index = b.m_index;
            // ctor 0x424A20 registers OnUITick -> the thunk 0x4077E3 = slot 12 0x4255B0
            add( b, "OnUITick", 0x4077E3u, [ this, &hb, index ]( const ui_params_t& p ) { hud_button_tick( hb, index, p ); } );
            return true;
        }
        if ( vt == k_ui_onscreen_axis_vtable || vt == k_ui_onscreen_button_vtable )
        {
            b.m_onscreen = std::make_unique< ui_hud_onscreen_t >( );
            ui_hud_onscreen_t& o = *b.m_onscreen;
            o.m_axis = vt == k_ui_onscreen_axis_vtable;
            take( "Screen Scale", o.m_scale, 8 );
            for ( int k = 0; k < 4; ++k )
                take( touch_names[ k ], &o.m_touch[ k ], 4 );
            take_anchor( o.m_anchor );
            const char* const enables[ 4 ] = { "Enable for Tilt", "Enable for Touch", "Enable for GamePad", "Enable for Keyboard" };
            for ( int k = 0; k < 4; ++k )
                take( enables[ k ], &o.m_enable[ k ], 1 );
            if ( const ui_slot_t* s = ui_bind_slot( e, o.m_axis ? "Axis Name" : "Button Name" ) )
                o.m_name = s->m_text;
            if ( o.m_axis )
                take( "Axis Direction", &o.m_direction, 4 );
            // ctor 0x4292F0 registers OnUITick 0x429E10
            add( b, "OnUITick", 0x429E10u, [ this, &o ]( const ui_params_t& ) { hud_onscreen_tick( o ); } );
            return true;
        }
        return false;
    }

    const ui_crop_t& hud_crop( ) const
    {
        static const ui_crop_t k_default;
        return m_hooks.m_crop ? *m_hooks.m_crop : k_default;
    }

    // slot 15: pause 0x426330 / recover 0x425F50 over the env.
    bool hud_button_visible( const ui_hud_button_t& hb ) const
    {
        const ui_hud_env_t& env = *m_hooks.m_hud;
        ui_hud_control_inputs_t in;
        in.m_race_present = env.m_world.m_manager;
        in.m_race_28 = env.m_manager_28;
        in.m_method_present = env.m_method_present;
        in.m_method = env.m_control_method;
        return hb.m_recover ? ui_hud_recover_visible( in ) : ui_hud_pause_visible( in );
    }

    ui_hud_pause_hooks_t hud_pause_hooks( )
    {
        ui_hud_pause_hooks_t ph;
        ph.m_input = [ this ]( int pad, const char* name )
        {
            const bool down = m_hooks.m_input ? m_hooks.m_input( pad, name ) : false;
            trace( ui_trace_kind_t::query, -1, name, down ? 1u : 0u, ui_params_t( ), true );
            return down;
        };
        ph.m_sound = [ this ]( const char* name )
        {
            trace( ui_trace_kind_t::sound, -1, name, 0u, ui_params_t( ), true );
            if ( m_hooks.m_sound )
                m_hooks.m_sound( name );
        };
        ph.m_broadcast = [ this ]( std::uint32_t hash, const ui_params_t& p ) { broadcast( hash, p ); };
        return ph;
    }

    void hud_button_tick( ui_hud_button_t& hb, int index, const ui_params_t& p )
    {
        ui_hud_button_class_t cls;
        cls.m_visible = [ this, &hb ] { return hud_button_visible( hb ); };
        if ( !hb.m_recover )
        {
            const ui_hud_pause_hooks_t ph = hud_pause_hooks( );
            cls.m_press = [ ph ] { ui_hud_pause_press( ph ); };
            cls.m_tick = [ this, &hb, ph ]( float )
            {
                if ( m_hooks.m_touch_device )
                    ui_hud_pause_tick( *m_hooks.m_touch_device, &hb, ph );
            };
        }
        else
        {
            cls.m_press = [ this, index ]
            {
                ui_hud_recover_press( m_hooks.m_hud->m_driver, [ this, index ]
                {
                    trace( ui_trace_kind_t::hook, index, "recover", 0u, ui_params_t( ), true );   // driver +0xB8 = 1
                    if ( m_hooks.m_recover )
                        m_hooks.m_recover( );
                } );
            };
            cls.m_tick = [ this, &hb ]( float dt ) { ui_hud_recover_tick( hb, m_hooks.m_hud->m_driver, dt ); };
        }
        ui_hud_button_tick( hb, hud_crop( ), p, cls );
    }

    int traced_touch_count( )
    {
        trace( ui_trace_kind_t::touches, -1, std::string( ), 0u, ui_params_t( ), true );
        return m_hooks.m_touch_count ? m_hooks.m_touch_count( ) : 0;
    }

    void traced_touch_point( int index, float out[ 2 ] )
    {
        trace( ui_trace_kind_t::touch, -1, std::string( ), static_cast< std::uint32_t >( index ), ui_params_t( ), true );
        if ( m_hooks.m_touch_point )
            m_hooks.m_touch_point( index, out );
    }

    void hud_onscreen_tick( ui_hud_onscreen_t& o )
    {
        const ui_hud_env_t& env = *m_hooks.m_hud;
        ui_hud_onscreen_hooks_t h;
        h.m_method_present = env.m_method_present;
        h.m_method = env.m_control_method;
        h.m_touch_count = [ this ] { return traced_touch_count( ); };
        h.m_touch_point = [ this ]( int k, float out[ 2 ] ) { traced_touch_point( k, out ); };
        h.m_inverse = m_hooks.m_inverse;
        h.m_button = [ this ]( int pad, const char* name )
        {
            trace( ui_trace_kind_t::button, -1, name, 0u, ui_params_t( ), true );
            if ( m_hooks.m_input_button )
                m_hooks.m_input_button( pad, name );
        };
        h.m_set_axis = [ this ]( int pad, const char* name, float value )
        {
            std::uint32_t bits;
            std::memcpy( &bits, &value, 4 );
            trace( ui_trace_kind_t::axis, -1, name, bits, ui_params_t( ), true );
            if ( m_hooks.m_input_axis )
                m_hooks.m_input_axis( pad, name, value );
        };
        ui_hud_onscreen_tick( o, hud_crop( ), h );
    }

    // The toast's text-base fields live in m_shown (the inputs') and its own struct (the handlers').
    static void sync_toast( ui_bound_t& b, bool in )
    {
        if ( in )
        {
            b.m_toast->m_visible = b.m_shown->m_visible;
            b.m_toast->m_alpha = b.m_shown->m_alpha;
        }
        else
        {
            b.m_shown->m_visible = b.m_toast->m_visible;
            b.m_shown->m_alpha = b.m_toast->m_alpha;
        }
    }

    std::pair< bool, int > ask( const std::string& name )
    {
        trace( ui_trace_kind_t::profile, -1, name, 0u, ui_params_t( ), true );
        return m_hooks.m_platform( name );
    }

    // VuGameConfigEntity (see ui_bound_config_t): its fields from OUR loaded
    // properties, the platform answers through ask( ), the hooks into this
    // binding's graph / broadcast / trace.
    void bind_config( ui_bound_t& b )
    {
        entity_t& e = *b.m_entity;
        b.m_config = std::make_unique< ui_bound_config_t >( );
        ui_bound_config_t& c = *b.m_config;
        const ui_front_end_screen_t screen = ui_front_end_screen( e, m_hooks.m_config_flags );
        c.m_boat = b.m_vtable == k_ui_boat_config_vtable;
        c.m_option_type = screen.m_option_type;
        c.m_flags = screen.m_flags;
        c.base( ).m_single_player = screen.m_single_player;
        c.base( ).m_force_online = screen.m_force_online;
        c.m_dock.m_allow_special = screen.m_allow_special;
        c.m_dock.m_boat_db = m_hooks.m_boat_db;
        c.m_dock.m_skin_db = m_hooks.m_skin_db;
        c.m_dock.m_champ_db = m_hooks.m_champ_db;
        const auto take = [ &e ]( const char* name, float* out, std::size_t words )
        {
            if ( const ui_slot_t* s = ui_bind_slot( e, name ) )
                std::memcpy( out, s->m_bits, 4 * words );
        };
        take( "Screen Scale", c.m_nav.m_scale, 2 );
        take( "Position X", &c.m_nav.m_position[ 0 ], 1 );
        take( "Position Y", &c.m_nav.m_position[ 1 ], 1 );
        take( "Spacing", &c.m_nav.m_spacing, 1 );
        const char* const rect[ 4 ] = { " X", " Y", " Width", " Height" };
        for ( int k = 0; k < 4; ++k )
        {
            take( ( std::string( "TouchLeft" ) + rect[ k ] ).c_str( ), &c.m_nav.m_left[ k ], 1 );
            take( ( std::string( "TouchRight" ) + rect[ k ] ).c_str( ), &c.m_nav.m_right[ k ], 1 );
        }
        c.m_env.m_root = m_hooks.m_root;   // gameutil +0x60 clear, +0x64 "" (the caller's root)
        ui_profile_t& p = c.m_profile;
        const auto join = []( std::initializer_list< std::string > parts )
        {
            std::string out;
            for ( const std::string& s : parts )
                out += out.empty( ) ? s : "|" + s;
            return out;
        };
        p.unlock = [ this, join ]( int slot, const std::string& a, const std::string& x, const std::string& y )
        {
            const std::string tag = std::to_string( slot );
            return ask( slot >= 100 ? join( { tag, a, x, y } ) : join( { tag, a, x } ) ).first;
        };
        p.license = [ this ] { return ask( "license" ).first; };
        p.store_beta = [ this ] { return ask( "4D3170" ).first; };
        p.progress = [ this ]( const std::string& v ) { return ask( "132|" + v ).first; };
        p.package = [ this ]( const std::string& asset ) { return ask( "144|" + asset ).first; };
        p.champ_rank = [ this ]( const std::string& champ ) { return ask( "88|" + champ ).second; };
        p.champ_done = [ this ]( const std::string& champ ) { return ask( "88|" + champ ).first; };
        p.race_rank = [ this, join ]( const std::string& m, const std::string& t, const std::string& k ) { return ask( join( { "48", m, t, k } ) ).second; };
        p.race_done = [ this, join ]( const std::string& m, const std::string& t, const std::string& k ) { return ask( join( { "48", m, t, k } ) ).first; };
        p.ghost_done = [ this ]( const std::string& track ) { return ask( "124|" + track ).first; };
        const int index = b.m_index;
        ui_config_hooks_t& h = c.m_hooks;
        h.broadcast = [ this ]( std::uint32_t hash, const ui_params_t& params ) { broadcast( hash, params ); };
        // 0x448140's 0x53FB90 { this + 0x5C, 0 } on the touch device g 0x759070 (+4): batch 26 - with m_touch_device it
        // lands there and the device's dispatch reaches the selector's callbacks (navigate.hxx ui_config_touch_handlers)
        h.touch = [ this, index, &c ]
        {
            trace( ui_trace_kind_t::hook, index, "touch_add", 0u, ui_params_t( ), true );
            if ( !m_hooks.m_touch_device )
                return;
            ui_touch_add( *m_hooks.m_touch_device, &c.m_nav, 0u );
            ui_config_touch_handlers( *m_hooks.m_touch_device, &c.m_nav, [ this ] { return touch_inverse( ); }, m_config_faults );
        };
        // slot 7 (0x45D080 / 0x44D090): 0x53FA60 (this + 0x5C), and VuGameConfigEntity's 3D draw manager remove (HOOK)
        h.touch_remove = [ this, index, &c ]
        {
            trace( ui_trace_kind_t::hook, index, "touch_remove", 0u, ui_params_t( ), true );
            if ( !m_hooks.m_touch_device )
                return;
            ui_touch_remove( *m_hooks.m_touch_device, &c.m_nav );
            m_hooks.m_touch_device->m_handlers.erase( &c.m_nav );
        };
        h.draw_remove = [ this, index, &e, &c ]
        {
            trace( ui_trace_kind_t::hook, index, "draw_remove", 0u, ui_params_t( ), true );   // g 0x7590B8 +44 (HOOK)
            if ( !c.m_boat && m_hooks.m_trophy_draw )
                m_hooks.m_trophy_draw( e, false, &c.m_game.m_trophy );
        };
        h.fire = [ this, &e ]( const std::string& plug, const ui_params_t& params )
        {
            if ( plug_index( e, plug.c_str( ) ) < 0 )
                return false;
            fire_named( e, plug.c_str( ), params );
            return true;
        };
        h.sound = [ this ]( const char* name ) { sound( name ); };
        h.trophy = [ this, &e ]( const char* model )
        {
            trace( ui_trace_kind_t::hook, -1, std::string( "trophy:" ) + model, 0u, ui_params_t( ), true );
            if ( m_hooks.m_trophy_model )
                m_hooks.m_trophy_model( e, model );
        };
        h.draw_add = [ this, index, &e, &c ]
        {
            trace( ui_trace_kind_t::hook, index, "draw_add", 0u, ui_params_t( ), true );   // g 0x7590B8 +40 (HOOK)
            if ( !c.m_boat && m_hooks.m_trophy_draw )
                m_hooks.m_trophy_draw( e, true, &c.m_game.m_trophy );
        };
        if ( c.m_boat )
            ui_dock_class( c.m_class, c.m_dock, c.m_env, p, h );
        else
            ui_game_config_class_full( c.m_class, c.m_entity, c.m_game, *m_hooks.m_root, p, h );
        // both ctors (0x45CC80 / 0x44C9F0, through the base 0x4471C0) register these two
        add( b, "OnUITick", 0x4077EDu, [ this, &c ]( const ui_params_t& )
        {
            if ( !ui_config_tick( c.base( ), c.m_nav, c.m_env, c.m_doc, m_hooks.m_delta ? m_hooks.m_delta( ) : 0.0f, c.m_class, c.m_profile, c.m_hooks ) )
                ++m_config_faults;
        } );
        add( b, "OnUIEvent", 0x404AB2u, [ this, &c ]( const ui_params_t& params )
        {
            if ( !ui_config_input( c.base( ), c.m_env, c.m_doc, params, c.m_class, c.m_profile, c.m_hooks ) )
                ++m_config_faults;
        } );
    }

    void bind( ui_bound_t& b )
    {
        entity_t& e = *b.m_entity;
        if ( m_hooks.m_hud && bind_hud( b ) )
            return;
        if ( !m_hooks.m_hud && bind_shown( b ) )
            return;
        if ( b.m_vtable == k_ui_container_vtable )
        {
            // 0x62F880 registers OnUITick (0x62F7A0) then OnUIEvent (0x62F7C0).
            for ( const char* event : { "OnUITick", "OnUIEvent" } )
                add( b, event, std::strcmp( event, "OnUITick" ) == 0 ? 0x62F7A0u : 0x62F7C0u, [ this, &e, event ]( const ui_params_t& p )
                {
                    const std::uint32_t h = fsm_hash( event );
                    for ( auto& child : e.m_children )
                        m_bound[ static_cast< std::size_t >( m_index[ child.get( ) ] ) ]->m_map.handle( h, p );
                } );
        }
        else if ( b.m_vtable == k_ui_button_vtable )
        {
            b.m_button_hooks.m_broadcast = [ this ]( std::uint32_t hash, const ui_params_t& p ) { broadcast( hash, p ); };
            b.m_button_hooks.m_fire = [ this, &e ]( const char* plug, const ui_params_t& p ) { fire_named( e, plug, p ); };
            b.m_button_hooks.m_connections = [ &e ]( const char* plug ) -> int
            {
                const script_component_t* sc = script_of( e );
                const int k = plug_index( e, plug );
                return sc && k >= 0 ? static_cast< int >( sc->m_plugs[ static_cast< std::size_t >( k ) ].m_links.size( ) ) : 0;
            };
            b.m_button_hooks.m_sound = [ this ]( const char* name ) { sound( name ); };
            b.m_button = std::make_unique< c_ui_button >( ui_entity_id( ui_long_name( e ) ), b.m_button_hooks );
            c_ui_button& button = *b.m_button;
            if ( const ui_slot_t* s = ui_bind_slot( e, "Enabled" ) )
                button.m_enabled = ( s->m_bits[ 0 ] & 0xFFu ) != 0;
            if ( const ui_slot_t* s = ui_bind_slot( e, "Label" ) )
                button.m_label = ( s->m_bits[ 0 ] & 0xFFu ) != 0;
            if ( const ui_slot_t* s = ui_bind_slot( e, "Can Have Focus" ) )
                button.m_can_focus = ( s->m_bits[ 0 ] & 0xFFu ) != 0;
            if ( const ui_slot_t* s = ui_bind_slot( e, "String ID" ) )
                button.m_string_id = s->m_text;
            button.m_touch_fields = ui_button_touch_of( e );   // batch 25: the touch hit test's fields
            button.m_crop = m_hooks.m_crop;
            // 0x443800: OnUISetFocus, OnUITick, OnUIEvent.
            add( b, "OnUISetFocus", 0x442A40u, [ &button ]( const ui_params_t& p ) { button.on_set_focus( p ); } );
            add( b, "OnUITick", 0x442A90u, [ &button ]( const ui_params_t& ) { button.on_tick( ); } );
            add( b, "OnUIEvent", 0x442D60u, [ &button ]( const ui_params_t& p ) { button.on_event( p ); } );
        }
        else if ( b.m_vtable == k_ui_event_vtable )
        {
            const ui_slot_t* s = ui_bind_slot( e, "Event" );
            b.m_event = std::make_unique< c_ui_event >( s ? s->m_text : std::string( ) );
            c_ui_event& ev = *b.m_event;
            ev.m_fire = [ this, &e ]( const ui_params_t& p ) { fire_named( e, "Trigger", p ); };
            add( b, "OnUIEvent", 0x62E950u, [ &ev ]( const ui_params_t& p ) { ev.on_ui_event( p ); } );
        }
        else if ( b.m_vtable == k_ui_results_vtable )
            b.m_results = std::make_unique< ui_results_table_t >( ui_results_table_of( e ) );
        else if ( b.m_vtable == k_ui_champ_vtable )
        {
            // The ctor 0x450070: OnUITick 0x451560; inputs IsBusy 0x451660, Exit 0x451690, PlayNext 0x451720 (run_input).
            b.m_champ = std::make_unique< ui_champ_state_t >( );
            ui_bound_t* self = &b;
            add( b, "OnUITick", 0x451560u, [ this, self ]( const ui_params_t& p )
            {
                std::uint32_t type = 0xFFFFFFFFu;
                float dt = 0.0f;
                if ( p.m_size > 0 )
                    std::memcpy( &type, p.m_bytes, 4 );
                if ( type == 2u )
                    std::memcpy( &dt, p.m_bytes + 4, 4 );
                if ( m_hooks.m_champ )
                    ui_champ_tick( *self->m_champ, *m_hooks.m_champ, dt );
            } );
        }
        else if ( b.m_vtable == k_ui_page_vtable )
        {
            // The ctor 0x62CA50: OnUITick 0x62DA30; slot 2's load 0x62E3B0.
            b.m_page = std::make_unique< ui_page_layout_t >( ui_page_layout_of( e ) );
            ui_page_layout_t& page = *b.m_page;
            if ( m_hooks.m_page_db && m_hooks.m_font_db )
                ui_page_layout_load( page, m_hooks.m_page_db, *m_hooks.m_font_db, m_hooks.m_page_env, m_hooks.m_page_hooks );
            add( b, "OnUITick", 0x62DA30u, [ this, &page, &e ]( const ui_params_t& )
            {
                ui_page_layout_tick( page, hud_crop( ), m_hooks.m_delta ? m_hooks.m_delta( ) : 0.0f, [ this, &e ] { fire_named( e, "OnReachedEnd", ui_params_t( ) ); } );
            } );
        }
        else if ( b.m_vtable == k_ui_remap_vtable || b.m_vtable == k_ui_throttle_remap_vtable )
        {
            b.m_remap = std::make_unique< ui_remap_t >( ui_remap_of( e, m_hooks.m_font_db ) );
            b.m_remap_hooks = m_hooks.m_remap_hooks;
            b.m_remap_hooks.m_set_auto = nullptr;   // slot 17 0x4021A0 does nothing: only the throttle's 0x4629D0 writes, and only it offers Auto
            if ( b.m_vtable == k_ui_throttle_remap_vtable && m_hooks.m_remap_auto )
            {
                const std::int32_t config = b.m_remap->m_config;
                const auto set = m_hooks.m_remap_auto;
                b.m_remap_hooks.m_set_auto = [ set, config ]( std::uint8_t value ) { set( config, value ); };
            }
            const std::uint32_t id = ui_entity_id( ui_long_name( e ) );
            b.m_remap_hooks.m_set_focus = [ this, id ]
            {
                ui_params_t p;
                ui_params_add( p, 1, id );
                broadcast( fsm_hash( "OnUISetFocus" ), p );
            };
            // 0x462420: the output plug; the browse sound when it has a connection.
            const std::function< void( const char* ) > sound = m_hooks.m_sound;
            b.m_remap_hooks.m_nav = [ this, &e, sound ]( const char* plug, std::int32_t arg )
            {
                ui_params_t p;
                ui_params_add( p, 0, static_cast< std::uint32_t >( arg ) );
                fire_named( e, plug, p );
                const script_component_t* sc = script_of( e );
                const int k = plug_index( e, plug );
                if ( sound && sc && k >= 0 && !sc->m_plugs[ static_cast< std::size_t >( k ) ].m_links.empty( ) )
                    sound( "Global/UI_and_Menu/menu_browse" );
            };
            ui_bound_t* self = &b;
            add( b, "OnUISetFocus", 0x4613A0u, [ self, id ]( const ui_params_t& p )
            {
                ui_params_reader_t r( p );
                ui_remap_on_focus( self->m_remap_state, ui_params_read( r, 1 ) == id );
            } );
            add( b, "OnUITick", 0x4613F0u, [ self ]( const ui_params_t& ) { ui_remap_tick( *self->m_remap, self->m_remap_state, self->m_remap_hooks ); } );
            add( b, "OnUIEvent", 0x4615A0u, [ self ]( const ui_params_t& p )
            {
                ui_params_reader_t r( p );
                const std::string text = r.read_string( );
                const std::int32_t arg = static_cast< std::int32_t >( ui_params_read( r, 0 ) );
                ui_remap_event( *self->m_remap, self->m_remap_state, text, arg, self->m_remap_hooks );
            } );
        }
        else if ( b.m_vtable == k_ui_info_vtable )
        {
            b.m_ticker = std::make_unique< ui_info_ticker_t >( ui_info_ticker_of( e ) );
            ui_info_ticker_t& t = *b.m_ticker;
            std::array< float, 2 > scale = { 0.0f, 0.0f };
            float width = 0.0f;
            if ( const ui_slot_t* s = ui_bind_slot( e, "Screen Scale" ) )
                std::memcpy( scale.data( ), s->m_bits, 8 );
            if ( const ui_slot_t* s = ui_bind_slot( e, "Width" ) )
                std::memcpy( &width, s->m_bits, 4 );
            const std::uint32_t font = m_hooks.m_font ? m_hooks.m_font( e ) : 0u;
            // 0x446120 registers OnClearInfoStringID, OnSetInfoStringID, OnUITick.
            add( b, "OnClearInfoStringID", 0x446450u, [ &t ]( const ui_params_t& ) { ui_info_clear( t ); } );
            add( b, "OnSetInfoStringID", 0x446480u, [ this, &t, &e, scale, width, font ]( const ui_params_t& p )
            {
                ui_params_reader_t r( p );
                ui_info_hooks_t hooks = m_info;
                if ( m_hooks.m_measure_of )
                    hooks.m_measure = [ this, &e ]( std::uint32_t, const std::string& text, float aspect ) { return m_hooks.m_measure_of( e, text, aspect ); };
                ui_info_set( t, r.read_string( ), scale.data( ), width, font, hooks );
            } );
            add( b, "OnUITick", 0x446580u, [ this, &t, width ]( const ui_params_t& )
            {
                ui_info_tick( t, width, m_hooks.m_delta ? m_hooks.m_delta( ) : 0.0f );
            } );
        }
        else if ( ui_is_slider_vtable( b.m_vtable ) )
        {
            const ui_slider_t s = ui_bind_slider( e, b.m_vtable );
            ui_slider_hooks_t& h = b.m_slider_hooks;
            h.m_broadcast = [ this ]( std::uint32_t hash, const ui_params_t& p ) { broadcast( hash, p ); };
            h.m_fire = [ this, &e ]( const char* plug, const ui_params_t& p ) { fire_named( e, plug, p ); };
            h.m_connections = [ &e ]( const char* plug ) -> int
            {
                const script_component_t* sc = script_of( e );
                const int k = plug_index( e, plug );
                return sc && k >= 0 ? static_cast< int >( sc->m_plugs[ static_cast< std::size_t >( k ) ].m_links.size( ) ) : 0;
            };
            h.m_sound = [ this ]( const char* name ) { sound( name ); };
            h.m_category = [ this ]( const char* name, float v )
            {
                trace( ui_trace_kind_t::category, -1, name, 0u, ui_params_t( ), true );
                if ( m_hooks.m_category )
                    m_hooks.m_category( name, v );
            };
            if ( m_hooks.m_touch_count )
                h.m_touch_count = [ this ]
                {
                    trace( ui_trace_kind_t::touches, -1, std::string( ), 0u, ui_params_t( ), true );
                    return m_hooks.m_touch_count( );
                };
            h.m_touch_point = [ this ]( int index, float out[ 2 ] )
            {
                trace( ui_trace_kind_t::touch, -1, std::string( ), static_cast< std::uint32_t >( index ), ui_params_t( ), true );
                if ( m_hooks.m_touch_point )
                    m_hooks.m_touch_point( index, out );
            };
            h.m_inverse = m_hooks.m_inverse;
            h.m_settings = m_hooks.m_settings;
            b.m_slider = std::make_unique< c_ui_slider >( ui_entity_id( ui_long_name( e ) ), s, h );
            c_ui_slider& slider = *b.m_slider;
            // 0x46E300 registers OnUISetFocus, OnUITick, OnUIEvent.
            add( b, "OnUISetFocus", 0x46F8F0u, [ &slider ]( const ui_params_t& p ) { slider.on_set_focus( p ); } );
            add( b, "OnUITick", 0x46F940u, [ &slider ]( const ui_params_t& ) { slider.on_tick( ); } );
            add( b, "OnUIEvent", 0x46FCC0u, [ &slider ]( const ui_params_t& p ) { slider.on_event( p ); } );
        }
        else if ( ( b.m_vtable == k_ui_game_config_vtable || b.m_vtable == k_ui_boat_config_vtable ) && m_hooks.m_platform && m_hooks.m_root )
            bind_config( b );
        else if ( b.m_vtable == k_ui_boat_stat_vtable )
            b.m_stat = std::make_unique< ui_boat_stat_t >( );
        else if ( b.m_vtable == k_ui_loading_bar_vtable )
        {
            // The ctor 0x462D70: OnLoadProgress 0x463240; the input plugs Show 0x462C40 / Hide 0x462C60 (run_input)
            b.m_bar = std::make_unique< ui_progress_bar_t >( );
            if ( const ui_slot_t* s = ui_bind_slot( e, "Initially Visible" ) )
                b.m_bar_initially = static_cast< std::uint8_t >( s->m_bits[ 0 ] & 0xFFu );
            ui_progress_bar_t& bar = *b.m_bar;
            add( b, "OnLoadProgress", 0x463240u, [ &bar ]( const ui_params_t& p ) { ui_progress_bar_on_load_progress( bar, p ); } );
        }
        else if ( b.m_vtable == k_ui_mini_board_vtable && m_hooks.m_leaderboard )
        {
            // ctor 0x47CC40 registers no event handlers; its draw / slot 6 / slot 7 / "Decision" are board.hxx's
            ui_mini_board_hooks_t& h = b.m_board_hooks;
            h = *m_hooks.m_leaderboard;
            ui_bound_t* bp = &b;
            const auto read = h.m_read;
            h.m_read = [ this, bp, read ]( const std::string& key, int a, int rows )
            {
                trace( ui_trace_kind_t::hook, bp->m_index, "board_read|" + key + "|" + std::to_string( a ) + "|" + std::to_string( rows ), 0u,
                       ui_params_t( ), true );
                if ( read )
                    read( key, a, rows );
            };
            h.m_tick_register = [ this, bp ]( bool on )
            {
                // the tick manager g 0x759008 vt+0x20 / +0x24 (the binding's "Decision" list: decision( ))
                trace( ui_trace_kind_t::hook, bp->m_index, on ? "board_tick|register|Decision" : "board_tick|unregister|Decision", 0u,
                       ui_params_t( ), true );
                bp->m_board_ticking = on;
            };
            h.m_delta = [ this ] { return m_hooks.m_delta ? m_hooks.m_delta( ) : 0.0f; };
            if ( m_hooks.m_root )
                h.m_root = m_hooks.m_root;   // gameutil +80 (else the leaderboard hooks' own)
            b.m_board = std::make_unique< c_ui_mini_board >( ui_mini_board_of( e ), h );
        }
        else if ( b.m_vtable == k_ui_leaderboard_vtable && m_hooks.m_ladder )
        {
            // ctor 0x477FD0 registers no event handlers; slot 6 / 7, "Decision", the touch callbacks and the draw are ladder.hxx's
            ui_leaderboard_hooks_t& h = b.m_ladder_hooks;
            h = *m_hooks.m_ladder;
            ui_bound_t* bp = &b;
            const auto read = h.m_read;
            h.m_read = [ this, bp, read ]( const std::string& key, int first, int rows )
            {
                trace( ui_trace_kind_t::hook, bp->m_index, "ladder_read|" + key + "|" + std::to_string( first ) + "|" + std::to_string( rows ), 0u,
                       ui_params_t( ), true );
                if ( read )
                    read( key, first, rows );
            };
            const auto total = h.m_total;
            h.m_total = [ this, bp, total ]
            {
                trace( ui_trace_kind_t::hook, bp->m_index, "ladder_total", 0u, ui_params_t( ), true );
                return total ? total( ) : 0;
            };
            h.m_tick_register = [ this, bp ]( bool on )
            {
                // the tick manager g 0x759008 vt+0x20 / +0x24 (the binding's "Decision" list: decision( ))
                trace( ui_trace_kind_t::hook, bp->m_index, on ? "ladder_tick|register|Decision" : "ladder_tick|unregister|Decision", 0u,
                       ui_params_t( ), true );
                bp->m_ladder_ticking = on;
            };
            h.m_touch = [ this, bp ]( bool on )
            {
                // 0x53FB90 {entity + 0x5C, 0} / 0x53FA60 on the touch device g 0x759070
                if ( !m_hooks.m_touch_device )
                    return;
                if ( on )
                    ui_touch_add( *m_hooks.m_touch_device, bp->m_ladder.get( ), 0u );
                else
                    ui_touch_remove( *m_hooks.m_touch_device, bp->m_ladder.get( ) );
                touch_handlers( bp->m_ladder.get( ), on );   // batch 24: the device's dispatch reaches it
            };
            h.m_delta = [ this ] { return m_hooks.m_delta ? m_hooks.m_delta( ) : 0.0f; };
            h.m_sound = [ this ]( const char* name ) { sound( name ); };
            if ( !h.m_option_db )
                h.m_option_db = m_hooks.m_option_db;   // gameutil +0x34
            b.m_ladder = std::make_unique< c_ui_leaderboard >( ui_leaderboard_of( e ), h );
        }
        else if ( b.m_vtable == k_ui_subtitles_vtable || ( ui_is_settings_combo_vtable( b.m_vtable ) && m_hooks.m_settings_platform ) )
        {
            ui_combo_box_hooks_t& h = b.m_combo_hooks;
            h.m_broadcast = [ this ]( std::uint32_t hash, const ui_params_t& p ) { broadcast( hash, p ); };
            h.m_fire = [ this, &e ]( const char* plug, const ui_params_t& p ) { fire_named( e, plug, p ); };
            h.m_connections = [ &e ]( const char* plug ) -> int
            {
                const script_component_t* sc = script_of( e );
                const int k = plug_index( e, plug );
                return sc && k >= 0 ? static_cast< int >( sc->m_plugs[ static_cast< std::size_t >( k ) ].m_links.size( ) ) : 0;
            };
            h.m_sound = [ this ]( const char* name ) { sound( name ); };
            h.m_delta = [ this ] { return m_hooks.m_delta ? m_hooks.m_delta( ) : 0.0f; };
            h.m_apply = [ this, bp = &b ]( std::uint32_t vtable, int current )
            {
                // 0x46D9B0: g 0x758FE0 +0x5C = current != 0 (no null test in retail)
                if ( vtable == k_ui_subtitles_vtable )
                {
                    if ( m_hooks.m_settings )
                        m_hooks.m_settings->m_subtitles = current != 0;
                    else
                        ++m_config_faults;
                }
                // the other five: their slot 12 (chooser.hxx apply_settings; a table index past it = a fault)
                else if ( !m_hooks.m_settings || !m_hooks.m_settings_platform ||
                          !bp->m_combo->apply_settings( *m_hooks.m_settings, *m_hooks.m_settings_platform ) )
                    ++m_config_faults;
            };
            b.m_combo = std::make_unique< c_ui_combo_box >( ui_entity_id( ui_long_name( e ) ), b.m_vtable, h );
            c_ui_combo_box& c = *b.m_combo;
            ui_combo_box_fill( c, e );
            // 0x474590 registers OnUISetFocus / OnUITick / OnUIEvent through the slot thunks
            add( b, "OnUISetFocus", 0x404AB2u, [ &c ]( const ui_params_t& p ) { c.on_set_focus( p ); } );
            add( b, "OnUITick", 0x4077DEu, [ &c ]( const ui_params_t& ) { c.on_tick( ); } );
            add( b, "OnUIEvent", 0x4077E8u, [ &c ]( const ui_params_t& p ) { c.on_event( p ); } );
        }
        else if ( b.m_vtable == k_ui_lock_text_vtable )
        {
            b.m_lock_text = std::make_unique< std::string >( );
            make_shown( b );   // the text base's Visible / alpha for its Show / Hide / SetAlpha inputs
            std::string& text = *b.m_lock_text;
            // 0x445450 registers OnSetShortUnlockStringID 0x445570
            add( b, "OnSetShortUnlockStringID", 0x445570u, [ this, &text ]( const ui_params_t& p )
            {
                ui_params_reader_t r( p );
                const std::string id = r.read_string( );
                trace( ui_trace_kind_t::string, -1, id, 0u, ui_params_t( ), true );
                text = m_hooks.m_string_db ? m_hooks.m_string_db( id.c_str( ) ) : std::string( );
            } );
        }
        else
        {
            for ( const ui_event_fact_t& f : k_ui_event_facts )
                if ( f.m_vtable == b.m_vtable )
                {
                    const int index = b.m_index;
                    const std::uint32_t method = f.m_method;
                    b.m_methods.push_back( { f.m_hash, f.m_method } );
                    b.m_map.m_handlers.push_back( { f.m_hash, [ this, index, method ]( const ui_params_t& p )
                    {
                        ++m_unknown_events;
                        trace( ui_trace_kind_t::event, index, std::string( ), method, p, false );
                    } } );
                }
        }
    }

    // 0x505690 over the bound maps, in pre-order = registration order. ORDER UNKNOWN (water batch 28):
    // VuEventManager keeps a broadcast's subscribers in a std::set< VuMethodInterface1* >, so retail runs
    // them in ascending address of the VuMethod objects the registrations allocate (operator new on the
    // CRT heap - not a fixed order). The oracle's bump heap allocates ascending, so every golden holds
    // registration order; that is what this replays. Order-sensitive here: OnUIEvent (a handler's focus
    // move broadcasts OnUISetFocus inside the dispatch, so a later subscriber of the same OnUIEvent sees
    // the new focus) and OnUITick (the order of the broadcasts / plug fires the ticks emit). OnUISetFocus
    // handlers each test their own name - independent.
    // FMOD getEvent + start: traced, then the hook.
    void sound( const char* name )
    {
        trace( ui_trace_kind_t::sound, -1, name, 0u, ui_params_t( ), true );
        if ( m_hooks.m_sound )
            m_hooks.m_sound( name );
    }

    void broadcast( std::uint32_t hash, const ui_params_t& p )
    {
        trace( ui_trace_kind_t::broadcast, -1, std::string( ), hash, p, true );
        if ( m_hooks.m_broadcast_out )
            m_hooks.m_broadcast_out( hash, p );
        for ( auto& b : m_bound )
            b->m_map.handle( hash, p );
    }

    bool bound_input( const entity_t& owner, const char* plug ) const
    {
        const std::uint32_t vt = owner.m_ctor ? owner.m_ctor->m_vtable : 0u;
        if ( shown_input( vt, plug ) && ( m_hooks.m_hud || ( vt != k_ui_hud_element_vtable && vt != k_ui_hud_toast_vtable ) ) )
            return true;
        if ( vt == k_ui_license_vtable && m_hooks.m_trial && ( std::strcmp( plug, "IsTrialVersion" ) == 0 || std::strcmp( plug, "IsFullVersion" ) == 0 ) )
            return true;
        // ui_frames' golden stubs these inputs, so they run only in a live screen (the hook set): case completion_text holds them.
        if ( ui_completion_of_vtable( vt ) >= 0 && m_hooks.m_trial && ( std::strcmp( plug, "UnusedContent" ) == 0 || std::strcmp( plug, "Locked" ) == 0 ) )
            return true;
        if ( vt == k_ui_set_string_vtable && m_hooks.m_trial && std::strcmp( plug, "Trigger" ) == 0 )
            return true;
        if ( m_hooks.m_hud )
        {
            if ( ( vt == k_ui_control_method_vtable || vt == k_ui_start_lights_vtable ) && std::strcmp( plug, "Trigger" ) == 0 )
                return true;
            if ( vt == k_ui_is_circuit_vtable && std::strcmp( plug, "Get" ) == 0 )
                return true;
        }
        if ( vt == k_ui_page_vtable )
            for ( const char* name : { "StartScroll", "StopScroll", "ResetScroll" } )
                if ( std::strcmp( plug, name ) == 0 )
                    return true;
        if ( vt == k_ui_champ_vtable && m_hooks.m_champ )
            for ( const char* name : { "IsBusy", "Exit", "PlayNext", "Completed" } )
                if ( std::strcmp( plug, name ) == 0 )
                    return true;
        if ( ( vt == k_ui_remap_vtable || vt == k_ui_throttle_remap_vtable ) && ( std::strcmp( plug, "SetFocus" ) == 0 || std::strcmp( plug, "Hide" ) == 0 ) )
            return true;
        if ( vt == k_ui_button_vtable )
            for ( const char* name : { "SetFocus", "SetStringID", "Enable", "Disable", "IsEnabled" } )
                if ( std::strcmp( plug, name ) == 0 )
                    return true;
        if ( ui_is_slider_vtable( vt ) )
            for ( const char* name : { "Show", "Hide", "SetFocus" } )
                if ( std::strcmp( plug, name ) == 0 )
                    return true;
        if ( ui_is_settings_combo_vtable( vt ) && std::strcmp( plug, "SetFocus" ) == 0 &&
             ( vt == k_ui_subtitles_vtable || ( index_of( owner ) >= 0 && m_bound[ static_cast< std::size_t >( index_of( owner ) ) ]->m_combo ) ) )
            return true;
        if ( vt == k_ui_boat_stat_vtable && std::strcmp( plug, "Set" ) == 0 )
            return true;
        if ( vt == k_ui_loading_bar_vtable && ( std::strcmp( plug, "Show" ) == 0 || std::strcmp( plug, "Hide" ) == 0 ) )
            return true;
        if ( vt == k_ui_boat_player_vtable && m_hooks.m_root && ( std::strcmp( plug, "SetSinglePlayer" ) == 0 || std::strcmp( plug, "SetIndex" ) == 0 ) )
            return true;
        if ( vt == k_ui_user_next_vtable && std::strcmp( plug, "Trigger" ) == 0 )
            return true;
        if ( ( vt == k_ui_game_config_vtable || vt == k_ui_boat_config_vtable ) && index_of( owner ) >= 0 &&
             m_bound[ static_cast< std::size_t >( index_of( owner ) ) ]->m_config )
            for ( const char* name : { "Unlocked", "Available", "InstalledOnHost", "NotUsedSP" } )
                if ( std::strcmp( plug, name ) == 0 )
                    return true;
        return ( vt == k_ui_if_vtable || vt == k_ui_set_info_vtable ) && std::strcmp( plug, "Trigger" ) == 0;
    }

public:
    // The slider's authored fields from OUR loaded properties (ctor 0x46E300's names).
    static ui_slider_t ui_bind_slider( const entity_t& e, std::uint32_t vtable )
    {
        ui_slider_t s;
        s.m_vtable = vtable;
        const auto real = [ &e ]( const char* name, float& out )
        {
            if ( const ui_slot_t* v = ui_bind_slot( e, name ) )
                std::memcpy( &out, v->m_bits, 4 );
        };
        const auto pair = [ &e ]( const char* name, float out[ 2 ] )
        {
            if ( const ui_slot_t* v = ui_bind_slot( e, name ) )
                std::memcpy( out, v->m_bits, 8 );
        };
        const auto byte = [ &e ]( const char* name, std::uint8_t& out )
        {
            if ( const ui_slot_t* v = ui_bind_slot( e, name ) )
                out = static_cast< std::uint8_t >( v->m_bits[ 0 ] & 0xFFu );
        };
        const auto word = [ &e ]( const char* name, std::int32_t& out )
        {
            if ( const ui_slot_t* v = ui_bind_slot( e, name ) )
                std::memcpy( &out, v->m_bits, 4 );
        };
        std::uint8_t visible = 1;
        byte( "Visible", visible );
        s.m_visible = visible != 0;
        pair( "Screen Scale", s.m_scale );
        const char* const frame[ 4 ] = { "FrameX", "FrameY", "FrameWidth", "FrameHeight" };
        const char* const text[ 4 ] = { "TextX", "TextY", "TextWidth", "TextHeight" };
        const char* const dst[ 4 ] = { "Bar DstX", "Bar DstY", "Bar DstWidth", "Bar DstHeight" };
        const char* const src[ 4 ] = { "Bar SrcX", "Bar SrcY", "Bar SrcWidth", "Bar SrcHeight" };
        for ( int k = 0; k < 4; ++k )
        {
            real( frame[ k ], s.m_frame[ k ] );
            real( text[ k ], s.m_text[ k ] );
            real( dst[ k ], s.m_dst[ k ] );
            real( src[ k ], s.m_src[ k ] );
        }
        word( "Horizontal Alignment", s.m_h_align );
        word( "Vertical Alignment", s.m_v_align );
        byte( "No Clip", s.m_no_clip );
        byte( "Single Line", s.m_single_line );
        byte( "Wordbreak", s.m_wordbreak );
        if ( const ui_slot_t* v = ui_bind_slot( e, "String ID" ) )
            s.m_string_id = v->m_text;
        pair( "Bar Dst Delta", s.m_dst_delta );
        pair( "Bar Src Delta", s.m_src_delta );
        pair( "Bar Src On Delta", s.m_src_on_delta );
        word( "Bar Count", s.m_count );
        return s;
    }

private:
    // The race HUD's inputs (see ui_hud_env_t). VuRetVal +0 = 0 and +4 the
    // caller's (m_known false) unless written.
    ui_ret_t run_hud_input( entity_t& owner, std::uint32_t vt, const char* plug, const ui_params_t& p )
    {
        ui_ret_t out;
        out.m_known = false;
        ui_bound_t& b = *m_bound[ static_cast< std::size_t >( index_of( owner ) ) ];
        if ( b.m_shown && std::strcmp( plug, "Show" ) == 0 )
            b.m_shown->m_visible = true;
        else if ( b.m_shown && std::strcmp( plug, "Hide" ) == 0 )
            b.m_shown->m_visible = false;
        else if ( b.m_shown && std::strcmp( plug, "SetAlpha" ) == 0 )
        {
            // 0x627AA0 / 0x629270: 0 unless the first record is a float
            float a = 0.0f;
            std::uint32_t type = 0xFFFFFFFFu;
            if ( p.m_size > 0 )
            {
                std::memcpy( &type, p.m_bytes, 4 );
                if ( type == 2 )
                    std::memcpy( &a, p.m_bytes + 4, 4 );
            }
            b.m_shown->m_alpha = a;
        }
        else if ( b.m_shown && std::strcmp( plug, "SetStringID" ) == 0 )
        {
            ui_params_reader_t r( p );
            b.m_shown->m_string_id = r.read_string( );
        }
        else if ( vt == k_ui_trigger_forwarder_vtable )
        {
            if ( plug_index( owner, "OnTrigger" ) >= 0 )
                fire_named( owner, "OnTrigger", p );
        }
        else if ( vt == k_ui_control_method_vtable )
        {
            static const char* const k_names[ 4 ] = { "IsGamePad", "IsKeyboard", "IsTilt", "IsTouch" };
            const std::int32_t method = m_hooks.m_hud->m_control_method;
            if ( method >= 0 && method < 4 )
                fire_named( owner, k_names[ method ], p );
        }
        else if ( vt == k_ui_is_circuit_vtable )
        {
            out.m_words[ 0 ] = 3u;
            out.m_words[ 1 ] = m_hooks.m_hud->m_track_built && m_hooks.m_hud->m_track_circuit ? 1u : 0u;   // the byte +4 only
        }
        else if ( vt == k_ui_start_lights_vtable )
            broadcast( fsm_hash( "OnStartLightsOut" ), ui_params_t( ) );
        return out;
    }

    // 0x500B10 (gameInitialize) / 0x500B80 (gameRelease) over the tree. The
    // components' slots 9 / 10 are nullsubs for every component type the race
    // HUDs hold (VuScriptComponent, VuUIDrawComponent, Vu2dLayoutComponent).
    void game_initialize( entity_t& e )
    {
        if ( e.m_flags & 4u )
            return;
        if ( !( e.m_flags & 8u ) )
            trace( ui_trace_kind_t::hook, index_of( e ), "repository_add", 0u, ui_params_t( ), true );
        for ( auto& child : e.m_children )
            game_initialize( *child );
        const std::uint32_t vt = e.m_ctor ? e.m_ctor->m_vtable : 0u;
        if ( vt == k_ui_hud_pause_vtable || vt == k_ui_hud_recover_vtable )
        {
            trace( ui_trace_kind_t::hook, index_of( e ), "touch_add", 0u, ui_params_t( ), true );        // 0x425390 -> 0x53FB90
            ui_bound_t& b = *m_bound[ static_cast< std::size_t >( index_of( e ) ) ];
            if ( m_hooks.m_touch_device && b.m_hud_button )
            {
                ui_touch_add( *m_hooks.m_touch_device, b.m_hud_button.get( ), 0u );
                // batch 25: the device's dispatch reaches its callback vtable 0x738B80 (hud_touch: 0x425400 /
                // 0x425490 / 0x4254B0)
                entity_t* owner = &e;
                ui_touch_handler_t h;
                h.m_down = [ this, owner ]( const float p[ 2 ] ) { hud_touch( *owner, 0, p ); };
                h.m_up = [ this, owner ]( const float p[ 2 ] ) { hud_touch( *owner, 1, p ); };
                h.m_move = [ this, owner ]
                {
                    const float none[ 2 ] = { 0.0f, 0.0f };
                    hud_touch( *owner, 2, none );
                };
                m_hooks.m_touch_device->m_handlers[ b.m_hud_button.get( ) ] = h;
            }
        }
        else if ( vt == k_ui_hud_toast_vtable && m_hooks.m_hud )
            m_bound[ static_cast< std::size_t >( index_of( e ) ) ]->m_shown->m_visible = false;            // 0x4245F0
        else if ( vt == k_ui_multi_delay_vtable )
            trace( ui_trace_kind_t::hook, index_of( e ), "multi_delay_init", 0u, ui_params_t( ), true ); // 0x5E0610 (the world's)
        else
        {
            ui_bound_t& bound = *m_bound[ static_cast< std::size_t >( index_of( e ) ) ];
            if ( bound.m_remap )
                bound.m_remap_ticking = true;                          // 0x461200's "Decision"
            game_slot( e, vt, true );
        }
        e.m_flags |= 4u;
    }

    // Batch 21: the slot 6 / 7 of a k_ui_game_slots row (not the race HUD's four,
    // which game_initialize / game_release handle above). Recreated:
    // - VuUIScreenEntity (the bound root only): 0x629A70 = ScreenEnter, 0x629B00 =
    //   ScreenExit (c_ui_screen).
    // - VuBarracudaUIButtonEntity 0x4426E0: 0x53FB90 { entity + 0x5C, 0 } on the
    //   touch device g 0x759070 (+4), then 0x53FAE0 (callback, +0x178 "Focus
    //   Priority" - OUR loaded value); slot 7 0x4253D0: 0x53FA60 (callback).
    // - VuUISubtitlesSettingEntity 0x46D7A0 / 0x4253D0 (chooser.hxx initialize /
    //   release; the subtitles byte = the settings manager g 0x758FE0 +0x5C).
    // - batch 24, with m_settings_platform: the other five settings combos' slot 6
    //   (chooser.hxx initialize_settings) / the shared slot 7 0x4253D0.
    // Retail reads g 0x759070 / g 0x758FE0 without a null test: without
    // m_touch_device / m_settings the row is entered as UNKNOWN (and counted).
    // Batch 24: the touch device's dispatch (controls.hxx ui_touch_dispatch_*) reaches a
    // registered combo / leaderboard through these handlers - its callback vtable (combo
    // 0x73DF24: 0x474F80 / 0x475080 / 0x475090; leaderboard 0x73F2B4: 0x479D30 / 0x479E30 /
    // 0x479E40). The move reads the device's touch 0 (slot 7 0x539230: ui_touch_point -
    // never written = indeterminate in retail: counted in m_config_faults, nothing run).
    template < typename T >
    void touch_handlers( const T* object, bool on )
    {
        if ( !m_hooks.m_touch_device )
            return;
        ui_touch_device_t& device = *m_hooks.m_touch_device;
        if ( !on )
        {
            device.m_handlers.erase( object );
            return;
        }
        ui_touch_handlers( device, const_cast< T* >( object ), [ this ] { return touch_inverse( ); }, m_config_faults );
    }

    const float* touch_inverse( ) const
    {
        static const float k_identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        return m_hooks.m_inverse ? m_hooks.m_inverse : k_identity;
    }

    // Batch 29: slot 6 0x62D9A0 (the touch add inside ui_page_layout_initialize) / slot 7 0x62DA10. The callback {entity + 0x5C, 0}:
    // down 0x62DD20, up 0x62DED0, move 0x62DEF0 = the device's touch 0 (never written: indeterminate in retail, a fault here).
    void page_slot( ui_page_layout_t& page, bool init )
    {
        ui_touch_device_t& device = *m_hooks.m_touch_device;
        if ( !init )
        {
            ui_page_layout_release( page, [ & ] { ui_touch_remove( device, &page ); device.m_handlers.erase( &page ); } );
            return;
        }
        ui_page_layout_initialize( page, [ this, &page, &device ]
        {
            ui_touch_add( device, &page, 0u );
            ui_touch_handler_t h;
            h.m_down = [ this, &page ]( const float p[ 2 ] ) { ui_page_layout_touch_down( page, hud_crop( ), touch_inverse( ), p ); };
            h.m_up = [ &page ]( const float* ) { ui_page_layout_touch_up( page ); };
            h.m_move = [ this, &page, &device ]
            {
                float p[ 2 ];
                if ( ui_touch_point( device, 0u, p ) )
                    ui_page_layout_touch_move( page, hud_crop( ), touch_inverse( ), p );
                else
                    ++m_config_faults;
            };
            device.m_handlers[ &page ] = h;
        } );
    }
    // Batch 29: slot 6 0x461200 (the touch add; "Decision" is game_initialize's) / slot 7 0x461290: the remove, the unregister, the
    // open box's release. Callback down 0x461760, up 0x4617F0, move 0x461800.
    void remap_slot( ui_bound_t& b, bool init )
    {
        ui_touch_device_t& device = *m_hooks.m_touch_device;
        ui_remap_t* row = b.m_remap.get( );
        ui_remap_runtime_t* state = &b.m_remap_state;
        if ( !init )
        {
            ui_touch_remove( device, row );
            device.m_handlers.erase( row );
            b.m_remap_ticking = false;
            if ( state->m_box )
            {
                if ( b.m_remap_hooks.m_close_box )
                    b.m_remap_hooks.m_close_box( );
                state->m_box = false;
            }
            return;
        }
        ui_touch_add( device, row, 0u );
        ui_touch_handler_t h;
        h.m_down = [ this, row, state ]( const float p[ 2 ] ) { ui_remap_touch_down( *row, *state, touch_inverse( ), p ); };
        h.m_up = [ row, state ]( const float* ) { ui_remap_touch_up( *row, *state ); };
        h.m_move = [ this, state, &device ]
        {
            float p[ 2 ];
            if ( ui_touch_point( device, 0u, p ) )
                ui_remap_touch_point( *state, touch_inverse( ), p );
            else
                ++m_config_faults;
        };
        device.m_handlers[ row ] = h;
    }

    void game_slot( entity_t& e, std::uint32_t vt, bool init )
    {
        const ui_game_slot_t* row = nullptr;
        for ( const ui_game_slot_t& r : k_ui_game_slots )
            if ( r.m_vtable == vt )
                row = &r;
        if ( !row )
            return;
        const std::uint32_t address = init ? row->m_init : row->m_release;
        if ( address == k_ui_nullsub )
            return;
        const int index = index_of( e );
        ui_bound_t* b = index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ].get( ) : nullptr;
        bool known = false;
        if ( vt == k_ui_screen_vtable && index == 0 )
            known = true;
        else if ( vt == k_ui_button_vtable && b && b->m_button && m_hooks.m_touch_device )
            known = true;
        else if ( vt == k_ui_subtitles_vtable && b && b->m_combo && m_hooks.m_touch_device && ( !init || m_hooks.m_settings ) )
            known = true;
        else if ( ui_is_settings_combo_vtable( vt ) && b && b->m_combo && m_hooks.m_touch_device &&
                  ( !init || ( m_hooks.m_settings && m_hooks.m_settings_platform ) ) )
            known = true;
        else if ( vt == k_ui_mini_board_vtable && b && b->m_board )
            known = true;
        else if ( vt == k_ui_leaderboard_vtable && b && b->m_ladder )
            known = true;
        else if ( b && ( b->m_page || b->m_remap ) && m_hooks.m_touch_device )
            known = true;   // batch 29: 0x62D9A0 / 0x62DA10, 0x461200 / 0x461290
        else if ( b && b->m_results && init && m_hooks.m_root )
            known = true;   // batch 29: 0x469B10
        else if ( b && b->m_champ && m_hooks.m_champ )
            known = true;   // batch 30: 0x451370 / 0x451540
        else if ( vt == k_ui_loading_bar_vtable && b && b->m_bar )
            known = true;   // batch 32: slot 6 0x463220, slot 7 the nullsub 0x401560
        else if ( loading_vtable( vt ) && b && b->m_shown && m_hooks.m_loading )
            known = true;
        else if ( ui_completion_of_vtable( vt ) >= 0 && b && m_hooks.m_platform )
            known = true;   // batch 33: 0x458500 / 0x458A40 / 0x459060 / 0x4595A0 (completion.hxx)
        else if ( !init && ( vt == k_ui_game_config_vtable || ( vt == k_ui_boat_config_vtable && m_hooks.m_root ) ) && b && b->m_config )
            known = true;   // batch 26: slot 7 0x45D080 / 0x44D090 (slot 6 stays config_setup( )'s)
        trace( ui_trace_kind_t::game, index, std::string( ), address, ui_params_t( ), known );
        if ( !known )
        {
            ++m_unknown_slots;
            return;
        }
        if ( loading_vtable( vt ) )
        {
            if ( vt == k_ui_track_image_vtable )
                b->m_shown->m_award = init && m_hooks.m_loading_image ? m_hooks.m_loading_image( ) : std::string( );
            else if ( init )
            {
                const ui_slot_t* place = ui_bind_slot( e, "Place" );
                m_hooks.m_loading( vt, place ? static_cast< std::int32_t >( place->m_bits[ 0 ] ) : 0, b->m_shown->m_award );
            }
            return;
        }
        if ( ui_completion_of_vtable( vt ) >= 0 )
        {
            b->m_completion = std::make_unique< ui_completion_t >( );
            b->m_completion->m_kind = static_cast< ui_completion_kind_t >( ui_completion_of_vtable( vt ) );
            ui_completion_env_t env;
            env.m_track_db = m_hooks.m_track_db;
            env.m_champ_db = m_hooks.m_champ_db;
            env.m_package = [ this ]( const std::string& asset ) { return m_hooks.m_platform( "144|" + asset ).first; };
            env.m_record = [ this ]( ui_completion_kind_t k, const std::string& track, const std::string& course, std::int32_t& place )
            {
                const std::pair< bool, int > a = m_hooks.m_platform( ui_completion_record_key( k, track, course ) );
                place = a.second;
                return a.first;
            };
            env.m_open = [ this ]( ui_completion_kind_t k, const std::string& track, const std::string& course )
            {
                return m_hooks.m_platform( k == ui_completion_kind_t::championship ? "92|Championship|" + track
                                                                                   : "100|" + std::string( k_ui_completion_modes[ static_cast< int >( k ) ] ) + "|" + track + "|" + course ).first;
            };
            env.m_string6 = m_hooks.m_string_db;
            ui_completion_initialize( *b->m_completion, env );
        }
        else if ( b && b->m_champ )
        {
            if ( init )
                ui_champ_load( *b->m_champ, *m_hooks.m_champ );
            else
                ui_champ_release( *b->m_champ, *m_hooks.m_champ );
        }
        else if ( b && b->m_results )
            ui_results_initialize( *b->m_results, *m_hooks.m_root, m_hooks.m_dev_game_data );
        else if ( b && b->m_page )
            page_slot( *b->m_page, init );
        else if ( b && b->m_remap )
            remap_slot( *b, init );
        else if ( vt == k_ui_screen_vtable )
        {
            if ( init )
                m_screen.screen_enter( );
            else
                m_screen.screen_exit( );
        }
        else if ( vt == k_ui_game_config_vtable || vt == k_ui_boat_config_vtable )
        {
            ui_bound_config_t& c = *b->m_config;
            if ( !c.m_boat )
                ui_game_config_release( c.m_game, c.m_hooks );   // 0x45D080
            else if ( !ui_boat_config_game_release( *m_hooks.m_root, c.m_dock.m_player, c.m_dock.m_base.m_options.size( ), c.m_hooks ) )   // 0x44D090
                ++m_config_faults;
        }
        else if ( vt == k_ui_loading_bar_vtable )
        {
            if ( init )
            {
                // 0x463220: progress = 0, visible = Initially Visible
                b->m_bar->m_progress = 0.0f;
                b->m_bar->m_visible = b->m_bar_initially;
            }
        }
        else if ( vt == k_ui_button_vtable )
        {
            if ( init )
            {
                std::uint32_t priority = 0;
                if ( const ui_slot_t* s = ui_bind_slot( e, "Focus Priority" ) )
                    priority = s->m_bits[ 0 ];
                ui_touch_add( *m_hooks.m_touch_device, b->m_button.get( ), 0u );
                ui_touch_set_priority( *m_hooks.m_touch_device, b->m_button.get( ), priority );
            }
            else
                ui_touch_remove( *m_hooks.m_touch_device, b->m_button.get( ) );
            touch_handlers( b->m_button.get( ), init );   // batch 25: its callback vtable 0x73B79C (button.hxx)
        }
        else if ( vt == k_ui_mini_board_vtable )
        {
            // slot 6 0x47DAB0 / slot 7 0x47DB10 (board.hxx)
            if ( init )
                b->m_board->initialize( );
            else
                b->m_board->release( );
        }
        else if ( vt == k_ui_leaderboard_vtable )
        {
            // slot 6 0x479C50 / slot 7 0x479CF0 (ladder.hxx)
            if ( init )
                b->m_ladder->initialize( );
            else
                b->m_ladder->release( );
        }
        else if ( init && vt == k_ui_subtitles_vtable )
            b->m_combo->initialize( *m_hooks.m_touch_device, m_hooks.m_settings->m_subtitles ? 1u : 0u );
        else if ( init )   // units 0x46D3E0 / fidelity 0x46DA70 / flip 0x46DE30 / vibration 0x46CD70 / tilt 0x46C780
            b->m_combo->initialize_settings( *m_hooks.m_touch_device, *m_hooks.m_settings, *m_hooks.m_settings_platform );
        else
            b->m_combo->release( *m_hooks.m_touch_device );
        if ( b && b->m_combo && ui_is_settings_combo_vtable( vt ) )
            touch_handlers( b->m_combo.get( ), init );
    }

    void game_release( entity_t& e )
    {
        if ( !( e.m_flags & 4u ) )
            return;
        for ( auto& child : e.m_children )
            game_release( *child );
        const std::uint32_t vt = e.m_ctor ? e.m_ctor->m_vtable : 0u;
        if ( vt == k_ui_hud_pause_vtable || vt == k_ui_hud_recover_vtable )
        {
            trace( ui_trace_kind_t::hook, index_of( e ), "touch_remove", 0u, ui_params_t( ), true );        // 0x4253D0 -> 0x53FA60
            ui_bound_t& b = *m_bound[ static_cast< std::size_t >( index_of( e ) ) ];
            if ( m_hooks.m_touch_device && b.m_hud_button )
            {
                ui_touch_remove( *m_hooks.m_touch_device, b.m_hud_button.get( ) );
                m_hooks.m_touch_device->m_handlers.erase( b.m_hud_button.get( ) );
            }
        }
        else if ( vt == k_ui_multi_delay_vtable )
            trace( ui_trace_kind_t::hook, index_of( e ), "multi_delay_release", 0u, ui_params_t( ), true ); // 0x43A420 (the world's)
        else if ( vt != k_ui_hud_toast_vtable )
            game_slot( e, vt, false );
        if ( !( e.m_flags & 8u ) )
            trace( ui_trace_kind_t::hook, index_of( e ), "repository_remove", 0u, ui_params_t( ), true );
        e.m_flags &= ~4u;
    }

public:
    // The race HUD driver's entry points (core/ui overlay.hxx).
    // gameInitialize 0x500B10 then 0x501260 "OnProjectInitialized" (each
    // entity's own map, pre-order; an empty VuParams).
    void hud_initialize( )
    {
        game_initialize( *m_bound.front( )->m_entity );
        const std::uint32_t h = fsm_hash( "OnProjectInitialized" );
        for ( auto& b : m_bound )
            b->m_map.handle( h, ui_params_t( ) );
    }
    void hud_release( ) { game_release( *m_bound.front( )->m_entity ); }
    // Batch 21: a Screens project the same way - the front end's screen load
    // 0x4C0550 runs 0x500B10 + 0x501260 on the loaded root (unless its flags hold
    // 4; the repository add g 0x759080 vt+0x18 before is a HOOK), and the root's
    // slot 6 IS ScreenEnter 0x629A70 (the release's slot 7 ScreenExit 0x629B00):
    // a caller of project_initialize( ) does not call enter( ) / exit( ). The
    // GameConfig* selectors' slot 6 stays config_setup( )'s (UNKNOWN here).
    void project_initialize( ) { hud_initialize( ); }
    void project_release( ) { hud_release( ); }
    int m_unknown_slots = 0;   // slot 6 / 7 rows entered that are not recreated
    // Batch 22: the tick manager's "Decision" phase (g 0x759008) for the bound mini
    // leaderboards whose slot 6 registered 0x47E170 (in binding order; the caller
    // runs this where the game runs that phase).
    void decision( )
    {
        for ( auto& b : m_bound )
            if ( b->m_board && b->m_board_ticking )
                b->m_board->decision( );
            else if ( b->m_ladder && b->m_ladder_ticking )
                b->m_ladder->decision( );   // batch 23: 0x47B640
            else if ( b->m_remap && b->m_remap_ticking )
                ui_remap_decision( *b->m_remap, b->m_remap_state, b->m_remap_hooks );   // batch 28: 0x4623D0
    }
    // Batch 29: a bound front-end button's +0x188 / +0x198 (the frame's m_button); default 0 / 0.
    // What Show / Hide / SetAlpha left on a text base or image (the frame's m_shown); false: not one.
    bool shown( const entity_t& e, std::uint8_t& visible, float& alpha ) const
    {
        const int index = index_of( e );
        const ui_hud_shown_t* s = index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_shown.get( ) : nullptr;
        if ( !s )
            return false;
        visible = s->m_visible ? 1 : 0;
        alpha = s->m_alpha;
        return true;
    }
    // Screens/Award's leaves: what their listeners left (text +0x10C, image the asset of +0xB8), or null: not one.
    const std::string* award_state( const entity_t& e ) const
    {
        const int index = index_of( e );
        const ui_bound_t* b = index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ].get( ) : nullptr;
        return b && b->m_shown && ( award_vtable( b->m_vtable ) || ( loading_vtable( b->m_vtable ) && m_hooks.m_loading ) ) ? &b->m_shown->m_award : nullptr;
    }
    // The String ID a SetStringID left on a button (0x442980) or a text base (0x62F480), or null.
    const std::string* runtime_string( const entity_t& e ) const
    {
        const int index = index_of( e );
        if ( index < 0 )
            return nullptr;
        const ui_bound_t& b = *m_bound[ static_cast< std::size_t >( index ) ];
        if ( b.m_button )
            return b.m_button->m_string_id.empty( ) ? nullptr : &b.m_button->m_string_id;
        return b.m_shown && b.m_vtable == k_ui_text_vtable && !b.m_shown->m_string_id.empty( ) ? &b.m_shown->m_string_id : nullptr;
    }
    void button_state( const entity_t& e, std::uint8_t& focused, std::uint32_t& press )
    {
        const int index = index_of( e );
        const c_ui_button* button = index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_button.get( ) : nullptr;
        focused = button && button->m_focused ? 1u : 0u;
        press = button ? button->m_press : 0u;
    }
    // Batch 32: what the frame's m_config / m_combo / m_info / m_stat / m_bar read.
    // A bound selector's base (VuBaseConfigEntity: its option vector +0x2BC and records +0x2B0), or null.
    const ui_config_entity_t* config( const entity_t& e )
    {
        const int index = index_of( e );
        if ( index < 0 || !m_bound[ static_cast< std::size_t >( index ) ]->m_config )
            return nullptr;
        return &m_bound[ static_cast< std::size_t >( index ) ]->m_config->base( );
    }
    // A bound settings combo's record +0x278 (ctor 0x474590; 0x475B10 draws it): +4 Heading String ID, +0x1C the choices, +0x28 the latched
    // focus +0x2A0, +0x29 / +0x2A the arrows' pressed bytes, +0x2C the current choice, +0x30 the shown one. Null: not bound.
    const ui_config_combo_t* combo( const entity_t& e )
    {
        const int index = index_of( e );
        if ( index < 0 || !m_bound[ static_cast< std::size_t >( index ) ]->m_combo )
            return nullptr;
        ui_bound_t& b = *m_bound[ static_cast< std::size_t >( index ) ];
        const c_ui_combo_box& c = *b.m_combo;
        ui_config_combo_t& r = b.m_combo_record;
        r.m_focused = c.m_latched;
        r.m_b41 = c.m_left_pressed;
        r.m_b42 = c.m_right_pressed;
        r.m_index = c.m_current;
        r.m_shown = c.m_shown;
        r.m_choice_ids.clear( );
        r.m_locked.clear( );
        for ( const auto& choice : c.m_choice_texts )
        {
            r.m_choice_ids.push_back( choice.first );
            r.m_locked.push_back( choice.second );
        }
        if ( const ui_slot_t* s = ui_bind_slot( e, "Heading String ID" ) )
            r.m_string_id = s->m_text;
        return &r;
    }
    // A completion text's state after slot 6, or null.
    const ui_completion_t* completion( const entity_t& e ) const
    {
        const int index = index_of( e );
        return index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_completion.get( ) : nullptr;
    }

    // A bound ticker (VuInfoUITextEntity: +0x12C.. and slot 12's text +0x140), or null.
    const ui_info_ticker_t* ticker( const entity_t& e ) const
    {
        const int index = index_of( e );
        return index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_ticker.get( ) : nullptr;
    }
    const ui_boat_stat_t* stat( const entity_t& e ) const
    {
        const int index = index_of( e );
        return index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_stat.get( ) : nullptr;
    }
    const ui_progress_bar_t* bar( const entity_t& e ) const
    {
        const int index = index_of( e );
        return index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_bar.get( ) : nullptr;
    }
    // Batch 30: a bound championship's state (the frame's m_champ draws it), or null.
    ui_champ_state_t* champ( const entity_t& e ) const
    {
        const int index = index_of( e );
        return index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_champ.get( ) : nullptr;
    }
    // Batch 29: a bound race results table (the frame's m_results draws it), or null.
    ui_results_table_t* results( const entity_t& e )
    {
        const int index = index_of( e );
        return index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_results.get( ) : nullptr;
    }
    // A bound settings slider (the frame's m_slider draws it), or null.
    c_ui_slider* slider( const entity_t& e )
    {
        const int index = index_of( e );
        return index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_slider.get( ) : nullptr;
    }
    // Batch 28: a bound page / remapping row (the frame's m_page / m_remap draw these), or null.
    ui_page_layout_t* page( const entity_t& e )
    {
        const int index = index_of( e );
        return index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_page.get( ) : nullptr;
    }
    ui_remap_t* remap( const entity_t& e )
    {
        const int index = index_of( e );
        return index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_remap.get( ) : nullptr;
    }
    ui_remap_runtime_t* remap_state( const entity_t& e )
    {
        const int index = index_of( e );
        return index >= 0 ? &m_bound[ static_cast< std::size_t >( index ) ]->m_remap_state : nullptr;
    }
    // A bound full leaderboard (its FSM / records / touch callbacks; draw( ) for the frame's m_leaderboard), or null.
    c_ui_leaderboard* leaderboard( const entity_t& e )
    {
        const int index = index_of( e );
        return index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_ladder.get( ) : nullptr;
    }
    // A bound mini leaderboard (its FSM / key; draw( ) for the frame's m_board), or null.
    c_ui_mini_board* mini_board( const entity_t& e )
    {
        const int index = index_of( e );
        return index >= 0 ? m_bound[ static_cast< std::size_t >( index ) ]->m_board.get( ) : nullptr;
    }
    // VuHUDEntity slot 12 0x423A20: VuParams { float dt } -> "OnUITick" to the
    // DIRECT children (0x5011A0).
    void hud_tick( float dt )
    {
        ui_params_t p;
        std::uint32_t bits;
        std::memcpy( &bits, &dt, 4 );
        ui_params_add( p, 2, bits );
        const std::uint32_t h = fsm_hash( "OnUITick" );
        for ( auto& child : m_bound.front( )->m_entity->m_children )
            m_bound[ static_cast< std::size_t >( m_index[ child.get( ) ] ) ]->m_map.handle( h, p );
    }
    // 0x5011A0 "OnUIEvent" [ name, int 0 ] to the DIRECT children (the message box's 0x466C60 / 0x466D80).
    void hud_event( const char* name )
    {
        ui_params_t p;
        p.add_string( name );
        const std::uint32_t h = fsm_hash( "OnUIEvent" );
        for ( auto& child : m_bound.front( )->m_entity->m_children )
            m_bound[ static_cast< std::size_t >( m_index[ child.get( ) ] ) ]->m_map.handle( h, p );
    }
    // VuEventManager_broadcast 0x505690 from outside (the race's events).
    void post( std::uint32_t hash, const ui_params_t& p ) { broadcast( hash, p ); }
    ui_ret_t input( entity_t& owner, const char* plug, const ui_params_t& p ) { return run_input( owner, plug, p ); }
    // An output plug fired from outside (the world's VuMultiDelayEntity outputs).
    ui_ret_t fire_plug( entity_t& owner, const char* plug, const ui_params_t& p ) { return fire_named( owner, plug, p ); }
    int bound_index( const entity_t& e ) const { return index_of( e ); }
    // A pause / recover button's touch callback (VuTouchCallback vtable 0x738B80,
    // delivered by the caller - the device's dispatch is not read): kind 0 down
    // 0x425400 (the device point), 1 up 0x425490, 2 move 0x4254B0 (the device's
    // touch 0 through m_touch_point). False when `e` is not such a button.
    bool hud_touch( const entity_t& e, int kind, const float point[ 2 ] )
    {
        const int k = index_of( e );
        if ( k < 0 || !m_bound[ static_cast< std::size_t >( k ) ]->m_hud_button || !m_hooks.m_hud )
            return false;
        ui_hud_button_t& hb = *m_bound[ static_cast< std::size_t >( k ) ]->m_hud_button;
        static const float k_identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        const float* inverse = m_hooks.m_inverse ? m_hooks.m_inverse : k_identity;
        if ( kind == 0 )
            ui_hud_button_touch_down( hb, hud_button_visible( hb ), inverse, point );
        else if ( kind == 1 )
            ui_hud_button_touch_up( hb, hud_button_visible( hb ) );
        else
        {
            float device[ 2 ] = { 0.0f, 0.0f };
            traced_touch_point( 0, device );
            ui_hud_button_touch_move( hb, inverse, device );
        }
        return true;
    }

private:
    ui_ret_t run_input( entity_t& owner, const char* plug, const ui_params_t& p )
    {
        ui_ret_t out;
        const std::uint32_t vt = owner.m_ctor ? owner.m_ctor->m_vtable : 0u;
        if ( vt == k_ui_champ_vtable )
        {
            ui_bound_t& b = *m_bound[ static_cast< std::size_t >( index_of( owner ) ) ];
            if ( std::strcmp( plug, "IsBusy" ) == 0 )
            {
                out.m_words[ 0 ] = 3u;   // 0x451660: a VuRetVal bool: +0x294 set
                out.m_words[ 1 ] = b.m_champ->m_award ? 1u : 0u;
                return out;
            }
            if ( std::strcmp( plug, "Completed" ) == 0 )
            {
                out.m_words[ 0 ] = 3u;   // 0x44FEC0: a VuRetVal bool: 0x454E30
                out.m_words[ 1 ] = ui_champ_done( *b.m_champ, *m_hooks.m_champ ) ? 1u : 0u;
                return out;
            }
            if ( std::strcmp( plug, "Exit" ) == 0 )
                ui_champ_next( *b.m_champ, *m_hooks.m_champ );
            else
                ui_champ_start( *b.m_champ, *m_hooks.m_champ );
            return out;   // VuRetVal 0
        }
        if ( vt == k_ui_page_vtable )
        {
            // 0x62E0A0 / 0x62E0C0 / 0x62E0E0: VuRetVal 0
            ui_page_layout_t& page = *m_bound[ static_cast< std::size_t >( index_of( owner ) ) ]->m_page;
            if ( std::strcmp( plug, "StartScroll" ) == 0 )
                ui_page_layout_start( page );
            else if ( std::strcmp( plug, "StopScroll" ) == 0 )
                ui_page_layout_stop( page );
            else
                ui_page_layout_reset( page );
            out.m_known = false;
            return out;
        }
        if ( vt == k_ui_remap_vtable || vt == k_ui_throttle_remap_vtable )
        {
            // SetFocus 0x4612E0, Hide 0x461380
            ui_bound_t& b = *m_bound[ static_cast< std::size_t >( index_of( owner ) ) ];
            if ( std::strcmp( plug, "SetFocus" ) == 0 )
                ui_remap_set_focus( b.m_remap_state, b.m_remap_hooks );
            else
                ui_remap_hide( *b.m_remap, b.m_remap_state );
            out.m_known = false;
            return out;
        }
        if ( vt == k_ui_boat_stat_vtable )
        {
            // 0x44FCA0: +0xB0 = the first record as an int; VuRetVal +0 = 0
            ui_boat_stat_set( *m_bound[ static_cast< std::size_t >( index_of( owner ) ) ]->m_stat, p );
            out.m_known = false;
            return out;
        }
        if ( vt == k_ui_boat_player_vtable )
        {
            // 0x44EF10 (select.hxx ui_set_single_player); 0x44EEA0: BoatConfig.CurPlayerIndex = the first record as an int (0x4237B0)
            if ( std::strcmp( plug, "SetSinglePlayer" ) == 0 )
                ui_set_single_player( *m_hooks.m_root );
            else
                ui_boat_player_index( *m_hooks.m_root, p );
            out.m_known = false;
            return out;
        }
        if ( vt == k_ui_loading_bar_vtable )
        {
            // 0x462C40 / 0x462C60: visible +0x88; VuRetVal +0 = 0
            ui_progress_bar_t& bar = *m_bound[ static_cast< std::size_t >( index_of( owner ) ) ]->m_bar;
            out.m_words[ 0 ] = std::strcmp( plug, "Show" ) == 0 ? ui_progress_bar_show( bar ) : ui_progress_bar_hide( bar );
            out.m_known = false;
            return out;
        }
        if ( vt == k_ui_game_config_vtable || vt == k_ui_boat_config_vtable )
        {
            // 0x4483D0 / 0x4483F0 / 0x448410 / 0x448430: VuRetVal { 3, al } (the byte +4 only)
            ui_bound_config_t& c = *m_bound[ static_cast< std::size_t >( index_of( owner ) ) ]->m_config;
            out.m_known = false;
            out.m_words[ 0 ] = 3u;
            out.m_words[ 1 ] = static_cast< std::uint32_t >( ui_config_plug( plug, c.base( ), *m_hooks.m_root, c.m_class, c.m_profile ) );
            return out;
        }
        if ( vt == k_ui_user_next_vtable )
        {
            // Trigger (the action base's 0x472800 -> thunk 0x4077E3) -> slot 12 0x470E50: VuRetVal +0 = 0 (+4 the caller's)
            out.m_words[ 0 ] = ui_user_next( [ this ]( std::uint32_t hash, const ui_params_t& q ) { broadcast( hash, q ); } );
            out.m_known = false;
            return out;
        }
        if ( ui_is_settings_combo_vtable( vt ) && m_bound[ static_cast< std::size_t >( index_of( owner ) ) ]->m_combo )
        {
            // SetFocus -> slot 13 0x475190 (every settings combo's): VuRetVal +0 = 0 (+4 the caller's)
            m_bound[ static_cast< std::size_t >( index_of( owner ) ) ]->m_combo->set_focus( );
            out.m_known = false;
            return out;
        }
        if ( ( m_hooks.m_hud && vt != k_ui_if_vtable && vt != k_ui_set_info_vtable && !ui_is_slider_vtable( vt ) && vt != k_ui_button_vtable ) ||
             shown_input( vt, plug ) )
            return run_hud_input( owner, vt, plug, p );
        if ( vt == k_ui_set_string_vtable )
        {
            // Trigger 0x5E3860: the plug "Set" with one string record, the Value property +0x60. VuRetVal { 0, the caller's }.
            const ui_slot_t* value = ui_bind_slot( owner, "Value" );
            ui_params_t q;
            q.add_string( value ? value->m_text.c_str( ) : "" );
            fire_named( owner, "Set", q );
            out.m_known = false;
            return out;
        }
        if ( vt == k_ui_license_vtable && m_hooks.m_trial &&
             ( std::strcmp( plug, "IsTrialVersion" ) == 0 || std::strcmp( plug, "IsFullVersion" ) == 0 ) )
        {
            // 0x42DE10 / 0x42DE40: a bool VuRetVal, the answer or its inverse.
            out.m_words[ 0 ] = 3u;
            out.m_words[ 1 ] = ui_license_answer( m_hooks.m_trial( ), plug[ 2 ] == 'F' ) ? 1u : 0u;
            return out;
        }
        if ( ui_completion_of_vtable( vt ) >= 0 && ( std::strcmp( plug, "UnusedContent" ) == 0 || std::strcmp( plug, "Locked" ) == 0 ) )
        {
            // 0x4582C0 / 0x4582E0 (one pair per class): a bool VuRetVal of +0x124 / +0x125. Before slot 6 the ctor's 0 / 1.
            const ui_completion_t* c = m_bound[ static_cast< std::size_t >( index_of( owner ) ) ]->m_completion.get( );
            out.m_words[ 0 ] = 3u;
            out.m_words[ 1 ] = ( plug[ 0 ] == 'U' ? c && c->m_unused : !c || c->m_locked ) ? 1u : 0u;
            return out;
        }
        if ( vt == k_ui_if_vtable )
        {
            // 0x5DDE60.
            const ui_ret_t answer = fire_named( owner, "Operand", ui_params_t( ) );
            if ( answer.m_words[ 0 ] == 3u )
                fire_named( owner, ( answer.m_words[ 1 ] & 0xFFu ) != 0 ? "True" : "False", p );
            out.m_known = false;  // +4 left as the caller's
            return out;
        }
        if ( vt == k_ui_set_info_vtable )
        {
            // 0x471E30.
            const ui_slot_t* id = ui_bind_slot( owner, "Info String ID" );
            ui_params_t q;
            q.add_string( id ? id->m_text.c_str( ) : "" );
            broadcast( fsm_hash( "OnSetInfoStringID" ), q );
            out.m_known = false;  // +4 left as the caller's
            return out;
        }
        if ( ui_is_slider_vtable( vt ) )
        {
            // 0x446C80 / 0x446FB0 / 0x46F850: VuRetVal +0 = 0 (+4 the caller's).
            c_ui_slider& slider = *m_bound[ static_cast< std::size_t >( index_of( owner ) ) ]->m_slider;
            if ( std::strcmp( plug, "Show" ) == 0 )
                slider.m_visible = true;
            else if ( std::strcmp( plug, "Hide" ) == 0 )
                slider.m_visible = false;
            else
                slider.set_focus( );
            out.m_known = false;
            return out;
        }
        c_ui_button* button = m_bound[ static_cast< std::size_t >( index_of( owner ) ) ]->m_button.get( );
        // 0x4428E0 / 0x442980 / 0x4429E0 / 0x442A00: VuRetVal { 0, 0 }; 0x442A20: { 3, Enabled }.
        if ( std::strcmp( plug, "SetFocus" ) == 0 )
            button->set_focus( );
        else if ( std::strcmp( plug, "SetStringID" ) == 0 )
            button->set_string_id( p );
        else if ( std::strcmp( plug, "Enable" ) == 0 )
            button->enable( );
        else if ( std::strcmp( plug, "Disable" ) == 0 )
            button->disable( );
        else
        {
            out.m_words[ 0 ] = 3u;
            out.m_words[ 1 ] = button->is_enabled( ) ? 1u : 0u;
        }
        return out;
    }

    ui_bind_hooks_t m_hooks;
    ui_info_hooks_t m_info;  // m_hooks.m_info with the trace
    ui_hud_hooks_t m_hud_hooks;  // the HUD elements' VuStringDB (m_hooks.m_string_db)
    std::map< const entity_t*, int > m_index;
};
