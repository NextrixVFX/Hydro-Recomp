#pragma once

#include <workspace/core/ui/combo.hxx>
#include <workspace/core/ui/compose.hxx>
#include <workspace/core/ui/difficulty.hxx>
#include <workspace/core/ui/image.hxx>
#include <workspace/core/ui/leave.hxx>
#include <workspace/core/ui/leaves.hxx>
#include <workspace/core/ui/message.hxx>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// THE EXPORT: one frame of a loaded screen's draws as data. ui_screen_frame
// runs the screen's draw (VuUIScreenEntity slot 13 0x629D60, ui_screen_draw)
// and, under it, every container's draw (VuUIContainerEntity 0x62F7E0,
// ui_container_draw) recursively - the walk retail makes - and returns each
// LEAF draw retail would run there, in order, with exactly what that draw sees:
// the entity, its UI draw component's index, the alpha, the matrix-stack top
// (the crop matrix), the float-stack top (VuUIImpl +0x8C) and the sort key
// words at that moment. Both walks are checked against retail (verify_ui_screen_
// draw, verify_ui_container_draw; verify_ui_screen_frame re-checks the screen
// level through this path).
//
// What a leaf draw is: retail's VuUIDrawComponent +0x10 delegate calls a member
// pointer on the entity; k_ui_draw_targets below is that pointer per entity
// vtable as the census case ui_draw_targets read it from retail's own loaded
// objects (tools tmp/frametable.py prints the rows). Three families are
// recreated and, given `inputs`, the frame EMITS their real draw records into
// `sort` (m_first / m_commands index sort.m_commands[ sort.m_index ]):
//   text   - member 0x627040 (the text base ctor 0x627070 -> 0x627E30, delegate
//            0x6F703C): if Visible (+0x64), slot 14 0x627C60( slot 12's text,
//            alpha ) = ui_text_draw; slot 12 (the text getter: VuStringDB etc.)
//            is per type and a HOOK input.
//   image  - member 0x4077DE = slot 15 0x4401C0 -> slot 13 0x629300 (image.hxx).
//   button - member 0x443280 (image.hxx ui_button_draw).
// Recreated too, each emitted only when inputs carries its runtime state:
//   info (m_info), config (m_config: 0x448F40), combo_box (m_combo: 0x475B10),
//   stat (m_stat: 0x44FB60), bar (m_bar: 0x4632E0), difficulty_image / _text
//   (m_difficulty: 0x45AF40 / 0x45AA30 over the class picker 0x459D70).
// Everything else is `other`: NOT recreated; m_target names the retail function.

enum class ui_frame_kind_t
{
    text,       // 0x627040 -> slot 14 0x627C60 (VuUITextEntity and the text base's types)
    image,      // slot 15 0x4401C0 -> slot 13 0x629300
    other,      // any other UI-drawn type: its draw NOT recreated
    button,     // 0x443280
    container,  // 0x62F7E0: walked into, never a leaf
    info,       // VuInfoUITextEntity: 0x627040 -> its slot 14 0x446670 (draw.hxx ui_info_text_draw; needs inputs->m_info)
    config,     // VuGameConfigEntity / VuBoatConfigEntity: 0x4077E8 -> slot 16 0x448F40 -> 0x448F50 (combo.hxx ui_config_draw; needs m_config)
    combo_box,  // the settings combo entities: 0x4390BE -> slot 17 0x475B10 -> 0x4578D0 (combo.hxx ui_combo_draw; needs m_combo)
    stat,       // VuBoatStatEntity 0x44FB60 (leaves.hxx ui_boat_stat_draw; needs m_stat)
    bar,        // VuLoadingProgressBarEntity 0x4632E0 (leaves.hxx ui_progress_bar_draw; needs m_bar)
    difficulty_image,  // VuDifficultyImageEntity: 0x4077E3 -> slot 12 0x45AF40 (difficulty.hxx; needs m_difficulty)
    difficulty_text,   // VuDifficultyTextEntity: 0x4077E3 -> slot 12 0x45AA30 (difficulty.hxx; needs m_difficulty)
    board,             // VuUIMiniLeaderboardEntity: 0x47DB40 (board.hxx c_ui_mini_board::draw over its runtime FSM; needs m_board)
    leaderboard,       // VuUILeaderboardEntity: 0x47A360 (ladder.hxx ui_leaderboard_draw over its runtime state; needs m_leaderboard)
    message_box,       // VuMessageBoxScreenEntity: 0x4675A0 (message.hxx: the box, then its children walked like a container's; needs m_message_box)
    page,              // VuUIPageLayoutEntity: 0x62E120 (pages.hxx ui_page_layout_draw over its runtime page; needs m_page)
    remap,             // VuInputRemappingEntity / VuThrottleRemappingEntity: 0x45FB90 (remap.hxx ui_remap_draw; needs m_remap)
    champ              // VuChampionshipEntity: 0x451CA0 (champ.hxx ui_champ_draw over the standings; needs m_champ)
};

struct ui_draw_target_t
{
    std::uint32_t m_vtable;   // the entity's vtable
    std::uint32_t m_method;   // the delegate's member pointer (+8)
    std::uint32_t m_target;   // the function it reaches (a thunk resolved through the vtable; text: slot 14)
    ui_frame_kind_t m_kind;
    std::uint32_t m_text;     // text: slot 12, the text getter (a HOOK); 0 otherwise
};

// From the census case ui_draw_targets on the 67 front-end / HUD projects (every
// loaded entity's FIRST VuUIDrawComponent's delegate: vtable, slot 2 0x404A00 /
// 0x422000, member pointer).
inline const ui_draw_target_t k_ui_draw_targets[ ] = {
    { 0x006F72C4u, 0x0062E120u, 0x0062E120u, ui_frame_kind_t::page, 0x00000000u },   // Screens/Credits (3 loaded)
    { 0x006F737Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00443920u },   // Screens/Cheats (1 loaded)
    { 0x006F7434u, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00446DE0u },   // HUDs/GhostGameHUD (253 loaded)
    { 0x006F7484u, 0x004077DEu, 0x004401C0u, ui_frame_kind_t::image, 0x00000000u },   // HUDs/GhostGameHUD (311 loaded)
    { 0x006F74F0u, 0x0062F7E0u, 0x0062F7E0u, ui_frame_kind_t::container, 0x00000000u },   // HUDs/GhostGameHUD (339 loaded)
    { 0x00738AC4u, 0x004077E8u, 0x004257A0u, ui_frame_kind_t::other, 0x00000000u },   // HUDs/GhostGameHUD (8 loaded)
    { 0x00738B24u, 0x004077E8u, 0x00425F70u, ui_frame_kind_t::other, 0x00000000u },   // HUDs/GhostGameHUD (8 loaded)
    { 0x00738BDCu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x004242D0u },   // HUDs/GhostGameHUD (8 loaded)
    { 0x00738C64u, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00423F40u },   // HUDs/GhostGameHUD (40 loaded)
    { 0x00738E10u, 0x004077EDu, 0x00427BD0u, ui_frame_kind_t::other, 0x00000000u },   // HUDs/GhostGameHUD (8 loaded)
    { 0x00739020u, 0x0042A0C0u, 0x0042A0C0u, ui_frame_kind_t::other, 0x00000000u },   // HUDs/GhostGameHUD (32 loaded)
    { 0x00739058u, 0x0042A0C0u, 0x0042A0C0u, ui_frame_kind_t::other, 0x00000000u },   // HUDs/GhostGameHUD (24 loaded)
    { 0x00739250u, 0x0042BA00u, 0x0042BA00u, ui_frame_kind_t::other, 0x00000000u },   // HUDs/GhostGameHUD (8 loaded)
    { 0x0073B4FCu, 0x004077DEu, 0x004401C0u, ui_frame_kind_t::image, 0x00000000u },   // Screens/LoadingChamp (5 loaded)
    { 0x0073B5DCu, 0x004077DEu, 0x004401C0u, ui_frame_kind_t::image, 0x00000000u },   // Screens/Award (2 loaded)
    { 0x0073B7B0u, 0x00443280u, 0x00443280u, ui_frame_kind_t::button, 0x00000000u },   // Screens/Achievements (104 loaded)
    { 0x0073B86Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00443920u },   // Screens/Award (2 loaded)
    { 0x0073BC5Cu, 0x00627040u, 0x00446670u, ui_frame_kind_t::info, 0x00446860u },   // Screens/Achievements (39 loaded)
    { 0x0073BC9Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00445F00u },   // Screens/Achievements (39 loaded)
    { 0x0073BCDCu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00445DE0u },   // Screens/Achievements (39 loaded)
    { 0x0073BD1Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00445C50u },   // Screens/Achievements (12 loaded)
    { 0x0073BD5Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x004458F0u },   // Screens/Achievements (12 loaded)
    { 0x0073BD9Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00445680u },   // Screens/AchievementNotification (1 loaded)
    { 0x0073BDDCu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00443920u },   // Screens/BoatConfig (10 loaded)
    { 0x0073BE1Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00445340u },   // Screens/Cheats (1 loaded)
    { 0x0073BE9Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00443920u },   // Screens/LoadingChamp (15 loaded)
    { 0x0073BEDCu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00444720u },   // Screens/LoadingChamp (15 loaded)
    { 0x0073BF1Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00443920u },   // Screens/LoadingChamp (5 loaded)
    { 0x0073BF9Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00443920u },   // Screens/LoadingChampSlalom (2 loaded)
    { 0x0073BFDCu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00443920u },   // Screens/LoadingChamp (5 loaded)
    { 0x0073C0BCu, 0x004077DEu, 0x004401C0u, ui_frame_kind_t::image, 0x00000000u },   // Screens/Award (2 loaded)
    { 0x0073C18Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00446DE0u },   // Screens/Award (2 loaded)
    { 0x0073C25Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00446DE0u },   // Screens/Award (2 loaded)
    { 0x0073C32Cu, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00446DE0u },   // Screens/Award (2 loaded)
    { 0x0073C69Cu, 0x004077E8u, 0x00448F40u, ui_frame_kind_t::config, 0x00000000u },   // Screens/BoatConfig (5 loaded)
    { 0x0073C808u, 0x0044FB60u, 0x0044FB60u, ui_frame_kind_t::stat, 0x00000000u },   // Screens/BoatConfig (20 loaded)
    { 0x0073C998u, 0x00451CA0u, 0x00451CA0u, ui_frame_kind_t::champ, 0x00000000u },   // Screens/Championship (1 loaded)
    { 0x0073CC34u, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00443920u },   // Screens/SinglePlayer (1 loaded)
    { 0x0073CC74u, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00443920u },   // Screens/SinglePlayer (1 loaded)
    { 0x0073CCB4u, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00443920u },   // Screens/SinglePlayer (1 loaded)
    { 0x0073CCF4u, 0x00627040u, 0x00627C60u, ui_frame_kind_t::text, 0x00443920u },   // Screens/SinglePlayer (1 loaded)
    { 0x0073CE3Cu, 0x004077E3u, 0x0045AF40u, ui_frame_kind_t::difficulty_image, 0x00000000u },   // Screens/BoatConfig (11 loaded)
    { 0x0073CE74u, 0x004077E3u, 0x0045AA30u, ui_frame_kind_t::difficulty_text, 0x00000000u },   // Screens/BoatConfig (11 loaded)
    { 0x0073D1DCu, 0x004077E8u, 0x00448F40u, ui_frame_kind_t::config, 0x00000000u },   // Screens/GameConfigChampionship (5 loaded)
    { 0x0073D45Cu, 0x0045FB90u, 0x0045FB90u, ui_frame_kind_t::remap, 0x00000000u },   // Screens/ControlsGamepad (2 loaded)
    { 0x0073D4F4u, 0x0045FB90u, 0x0045FB90u, ui_frame_kind_t::remap, 0x00000000u },   // Screens/ControlsGamepad (14 loaded)
    { 0x0073D724u, 0x004632E0u, 0x004632E0u, ui_frame_kind_t::bar, 0x00000000u },   // Screens/LoadingChamp (7 loaded)
    { 0x0073D8E4u, 0x004675A0u, 0x004675A0u, ui_frame_kind_t::message_box, 0x00000000u },   // Screens/MessageBox (1 loaded)
    { 0x0073DA38u, 0x0046A500u, 0x0046A500u, ui_frame_kind_t::other, 0x00000000u },   // Screens/RaceResults (2 loaded)
    { 0x0073DE6Cu, 0x004390BEu, 0x00475B10u, ui_frame_kind_t::combo_box, 0x00000000u },   // Screens/SettingsGraphics (1 loaded)
    { 0x0073DED4u, 0x004390BEu, 0x00475B10u, ui_frame_kind_t::combo_box, 0x00000000u },   // Screens/Settings (2 loaded)
    { 0x0073DF3Cu, 0x004390BEu, 0x00475B10u, ui_frame_kind_t::combo_box, 0x00000000u },   // Screens/Settings (2 loaded)
    { 0x0073DFA4u, 0x004390BEu, 0x00475B10u, ui_frame_kind_t::combo_box, 0x00000000u },   // Screens/Settings (2 loaded)
    { 0x0073DFF4u, 0x004700E0u, 0x004700E0u, ui_frame_kind_t::other, 0x00000000u },   // Screens/SettingsGraphics (1 loaded)
    { 0x0073E030u, 0x004700E0u, 0x004700E0u, ui_frame_kind_t::other, 0x00000000u },   // Screens/SettingsAudio (1 loaded)
    { 0x0073E06Cu, 0x004700E0u, 0x004700E0u, ui_frame_kind_t::other, 0x00000000u },   // Screens/SettingsAudio (1 loaded)
    { 0x0073E0BCu, 0x004390BEu, 0x00475B10u, ui_frame_kind_t::combo_box, 0x00000000u },   // Screens/ControlsGamepad (1 loaded)
    { 0x0073E10Cu, 0x004700E0u, 0x004700E0u, ui_frame_kind_t::other, 0x00000000u },   // Screens/ControlsTiltTouch (1 loaded)
    { 0x0073E15Cu, 0x004390BEu, 0x00475B10u, ui_frame_kind_t::combo_box, 0x00000000u },   // Screens/ControlsTiltTouch (1 loaded)
    { 0x0073F110u, 0x00477240u, 0x00477240u, ui_frame_kind_t::other, 0x00000000u },   // HUDs/GhostPreGameHUD (1 loaded)
    { 0x0073F2C8u, 0x0047A360u, 0x0047A360u, ui_frame_kind_t::leaderboard, 0x00000000u },   // Screens/Leaderboard (1 loaded)
    { 0x0073F408u, 0x0047DB40u, 0x0047DB40u, ui_frame_kind_t::board, 0x00000000u },   // Screens/GameConfigGhost (4 loaded)
};

inline const ui_draw_target_t* ui_draw_target_of( const entity_t& e )
{
    if ( !e.m_ctor )
        return nullptr;
    for ( const ui_draw_target_t& t : k_ui_draw_targets )
        if ( t.m_vtable == e.m_ctor->m_vtable )
            return &t;
    return nullptr;
}

struct ui_frame_draw_t
{
    const entity_t* m_entity = nullptr;
    int m_component = 0;
    float m_alpha = 1.0f;
    std::array< float, 16 > m_matrix{ };
    float m_top = 1.0f;
    std::uint32_t m_key_lo = 0;
    std::uint32_t m_key_hi = 0;
    int m_depth = 0;  // container nesting under the screen (0 = a direct child)
    ui_frame_kind_t m_kind = ui_frame_kind_t::other;
    std::uint32_t m_target = 0;   // the retail draw this leaf runs (0: a type the census did not see)
    // With inputs: the commands this leaf emitted into the sort (sort.m_commands[ sort.m_index ]).
    int m_first = 0;
    int m_commands = 0;
    bool m_emitted = false;       // a recreated draw ran (text / image / button)
};

struct ui_frame_t
{
    std::vector< ui_frame_draw_t > m_draws;
    int m_containers = 0;  // container draws walked through
    int m_unrecreated = 0; // leaves whose draw is not recreated (other)
    int m_unknown = 0;     // emitted draws that reached a path not recreated (the font draw's immediate path)
};

// The leaf kinds by the loaded entity's vtable (the census table).
inline ui_frame_kind_t ui_frame_kind_of( const entity_t& e )
{
    const ui_draw_target_t* t = ui_draw_target_of( e );
    return t ? t->m_kind : ui_frame_kind_t::other;
}

// What the recreated leaf draws need beyond the loaded properties - each a HOOK
// for the rendering / string layers. Null callbacks answer "none".
struct ui_frame_inputs_t
{
    ui_font_draw_env_t m_env;            // the materials (the matrix / float tops are each leaf's)
    ui_string_db_t m_db;                 // 0x5546B0's "right-ea-left" answer
    const value_t* m_fontdb = nullptr;   // FontDB's document (VuUIFont's reload 0x626CF0 over it)
    // A text leaf's slot 12 / a button's VuStringDB slot 7 on its String ID.
    std::function< std::string( const entity_t& ) > m_text;
    // 0x626B80: the font's asset font (+0x58), else VuUIImpl's default.
    std::function< ui_text_font_t( const entity_t& ) > m_font;
    // An image's slot 14 (which 0); a button's selected / unselected / pressed (which 0 / 1 / 2). Null: none.
    std::function< const ui_texture_t*( const entity_t&, int which ) > m_texture;
    // A button's runtime +0x188 (focused) / +0x198 (press) - c_ui_button's; default 0 / 0.
    std::function< void( const entity_t&, std::uint8_t& focused, std::uint32_t& press ) > m_button;
    // UNKNOWN: the font params' bytes 41..59 (retail's 0x626CF0 copies them from stack it never wrote); this
    // may fill them per entity (else they stay 0).
    std::function< void( const entity_t&, std::uint8_t params[ 60 ] ) > m_params_tail;
    // VuInfoUITextEntity's ticker state +0x12C..+0x13C (its tick is NOT recreated). Null: its leaves stay unrecreated.
    std::function< ui_info_scroll_t( const entity_t& ) > m_info;
    // The config selectors' runtime (the option vector + combo records slot 4 builds: select.hxx / expand.hxx). Null: unrecreated.
    std::function< const ui_config_entity_t*( const entity_t& ) > m_config;
    // A settings combo entity's record (+0x278; built by its own code - an input). Null: unrecreated.
    std::function< const ui_config_combo_t*( const entity_t& ) > m_combo;
    // The combo draws' fonts / VuStringDB / the seven textures per entity (combo.hxx). Null: none.
    std::function< ui_combo_inputs_t( const entity_t& ) > m_combo_inputs;
    // UNKNOWN as above: a combo block's label (0) / value (1) params' bytes 41..59.
    std::function< void( const entity_t&, int which, std::uint8_t params[ 60 ] ) > m_combo_params_tail;
    // VuBoatStatEntity's +0xB0 (leaves.hxx; its textures: m_texture which 0 off / 1 on). Null: unrecreated.
    std::function< ui_boat_stat_t( const entity_t& ) > m_stat;
    // VuLoadingProgressBarEntity's +0x84 / +0x88 (leaves.hxx). Null: unrecreated.
    std::function< ui_progress_bar_t( const entity_t& ) > m_bar;
    // The difficulty leaves (difficulty.hxx): the class picker 0x459D70's environment. Null: unrecreated.
    const ui_difficulty_env_t* m_difficulty = nullptr;
    // Their per-class inputs (k = 0 C / 1 B / 2 A): the image's Texture Asset k (+0x4C -> +0x58; null none),
    // the text's Font k (0x626B80), its params' UNKNOWN bytes 41..59, VuStringDB slot 7 on its String ID.
    std::function< const ui_texture_t*( const entity_t&, int k ) > m_class_texture;
    std::function< ui_text_font_t( const entity_t&, int k ) > m_class_font;
    std::function< void( const entity_t&, int k, std::uint8_t params[ 60 ] ) > m_class_params_tail;
    std::function< std::string( const std::string& id ) > m_string;
    // Optional (the race HUD, overlay.hxx): a text / image leaf's RUNTIME +0x64 (image +0x6C) Visible and
    // +0x108 (image +0xB4) alpha - what Show / Hide / SetAlpha left - in place of the loaded Visible / the
    // ctor's 1.0. false: none (the loaded values stay).
    std::function< bool( const entity_t&, std::uint8_t& visible, float& alpha ) > m_shown;
    // Optional (the race HUD, overlay.hxx, batch 17): a pause / recover / on-screen control's Texture Asset
    // (which 0) / Pressed Texture Asset (which 1) texture (+0xCC / +0xD0 -> +0x4C -> +0x58; null: no asset or
    // no texture). Set: those leaves emit through controls.hxx over the binding's runtime state.
    std::function< const ui_texture_t*( const entity_t&, int which ) > m_control_texture;
    // Batch 22: the leaderboards' draws over their runtime objects (the FSM, the records, the platform
    // leaderboard manager's rows - the owner's, e.g. c_ui_binding::mini_board( )): run the draw into `sort`
    // with the leaf's environment; false = it reached a path not recreated. Null: unrecreated.
    std::function< bool( const entity_t&, ui_gfx_sort_t&, const ui_font_draw_env_t& ) > m_board;
    std::function< bool( const entity_t&, ui_gfx_sort_t&, const ui_font_draw_env_t& ) > m_leaderboard;
    // Batch 27: the message box's runtime state (message.hxx: the enabled bytes, texts, fonts, +0x528..+0x588) and its textures,
    // over the fields the frame takes from the loaded properties (ui_message_box_of). false: unrecreated.
    std::function< bool( const entity_t&, ui_message_box_t&, ui_message_box_inputs_t& ) > m_message_box;
    // Batch 28: the page layout / remapping row / championship table over their runtime objects (wired.hxx builds
    // the loaded fields, c_ui_binding owns the runtime): run the draw into `sort` with the leaf's environment; false =
    // it reached a path not recreated. Null: unrecreated.
    std::function< bool( const entity_t&, ui_gfx_sort_t&, const ui_font_draw_env_t&, float alpha ) > m_page;
    std::function< bool( const entity_t&, ui_gfx_sort_t&, const ui_font_draw_env_t&, float alpha ) > m_remap;
    std::function< bool( const entity_t&, ui_gfx_sort_t&, const ui_font_draw_env_t&, float alpha ) > m_champ;
};

// A loaded entity's own / its first UI draw component's property bits.
inline bool ui_frame_take( const entity_t& e, const char* name, void* out, std::size_t bytes )
{
    const ui_slot_t* s = ui_property_value( e, name );
    if ( !s )
        return false;
    std::memcpy( out, s->m_bits, bytes );
    return true;
}

inline float ui_frame_depth( const entity_t& e )
{
    float d = 0.0f;
    for ( const entity_component_t& c : e.m_components )
        if ( c.m_vtable == k_ui_draw_component_vtable )
        {
            if ( c.m_state )
            {
                const ui_state_t* s = static_cast< const ui_state_t* >( c.m_state.get( ) );
                for ( int k = 0; k < s->m_count; ++k )
                    if ( std::strcmp( s->m_rows[ k ]->m_name, "Depth" ) == 0 )
                        std::memcpy( &d, s->m_value[ k ].m_bits, 4 );
            }
            break;
        }
    return d;
}

inline void ui_frame_params( const entity_t& e, const ui_frame_inputs_t& in, std::uint8_t params[ 60 ] )
{
    std::memset( params, 0, 60 );
    if ( in.m_params_tail )
        in.m_params_tail( e, params );
    const ui_slot_t* font = ui_property_value( e, "Font" );
    std::string asset;
    if ( in.m_fontdb )
        ui_font_params( *in.m_fontdb, font ? font->m_text : std::string( ), params, asset );
}

// The difficulty leaves from the loaded properties (the field lists verify_ui_
// difficulty compares with retail's bytes); the class from the picker.
inline bool ui_frame_emit_difficulty( ui_gfx_sort_t& sort, const ui_font_draw_env_t& env, const entity_t& e, bool text, const ui_frame_inputs_t& in )
{
    if ( !text )
    {
        ui_difficulty_image_t f;
        ui_frame_take( e, "Screen Scale", f.m_screen_scale, 8 );
        ui_frame_take( e, "Loading Screen", &f.m_loading_screen, 1 );
        ui_frame_take( e, "Position X", &f.m_position[ 0 ], 4 );
        ui_frame_take( e, "Position Y", &f.m_position[ 1 ], 4 );
        f.m_depth = ui_frame_depth( e );
        const int k = ui_difficulty_class( f.m_loading_screen != 0, *in.m_difficulty );
        ui_difficulty_image_draw( sort, env, f, k, k >= 0 && in.m_class_texture ? in.m_class_texture( e, k ) : nullptr );
        return true;
    }
    ui_difficulty_text_t t;
    ui_frame_take( e, "Screen Scale", t.m_screen_scale, 8 );
    ui_frame_take( e, "Loading Screen", &t.m_loading_screen, 1 );
    ui_frame_take( e, "X", &t.m_rect[ 0 ], 4 );
    ui_frame_take( e, "Y", &t.m_rect[ 1 ], 4 );
    ui_frame_take( e, "Width", &t.m_rect[ 2 ], 4 );
    ui_frame_take( e, "Height", &t.m_rect[ 3 ], 4 );
    ui_frame_take( e, "Horizontal Alignment", &t.m_h_align, 4 );
    ui_frame_take( e, "Vertical Alignment", &t.m_v_align, 4 );
    ui_frame_take( e, "No Clip", &t.m_no_clip, 1 );
    ui_frame_take( e, "Single Line", &t.m_single_line, 1 );
    ui_frame_take( e, "Wordbreak", &t.m_wordbreak, 1 );
    const char* const fonts[ 3 ] = { "Font C ", "Font B ", "Font A " };
    const char* const ids[ 3 ] = { "C String ID", "B String ID", "A String ID" };
    for ( int k = 0; k < 3; ++k )
    {
        if ( const ui_slot_t* id = ui_property_value( e, ids[ k ] ) )
            t.m_string_id[ k ] = id->m_text;
        std::memset( t.m_params[ k ], 0, 60 );
        if ( in.m_class_params_tail )
            in.m_class_params_tail( e, k, t.m_params[ k ] );
        const ui_slot_t* font = ui_property_value( e, fonts[ k ] );
        std::string asset;
        if ( in.m_fontdb )
            ui_font_params( *in.m_fontdb, font ? font->m_text : std::string( ), t.m_params[ k ], asset );
    }
    t.m_depth = ui_frame_depth( e );
    const int k = ui_difficulty_class( t.m_loading_screen != 0, *in.m_difficulty );
    const ui_text_font_t font = k >= 0 && in.m_class_font ? in.m_class_font( e, k ) : ui_text_font_t( );
    return ui_difficulty_text_draw( sort, env, in.m_db, t, k, font, in.m_string );
}

// One recreated leaf's draw from the loaded properties (the field lists the
// draw checks verify against retail's bytes).
inline bool ui_frame_emit( ui_gfx_sort_t& sort, const ui_impl_t& impl, const ui_frame_draw_t& d, const ui_frame_inputs_t& in )
{
    const entity_t& e = *d.m_entity;
    ui_font_draw_env_t env = in.m_env;
    std::memcpy( env.m_matrix, d.m_matrix.data( ), 64 );
    env.m_top = d.m_top;
    if ( d.m_kind == ui_frame_kind_t::text || d.m_kind == ui_frame_kind_t::info )
    {
        std::uint8_t visible = 1;
        ui_frame_take( e, "Visible", &visible, 1 );   // 0x627040: +0x64
        float shown_alpha = 1.0f;
        if ( in.m_shown )
            in.m_shown( e, visible, shown_alpha );
        if ( !visible )
            return true;
        // 0x627040: slot 14( slot 12( ), alpha ) - the text getter runs first (VuHUDElementEntity's 0x423F40
        // writes +0x108 for its wrong-way Type), then the draw reads +0x108.
        const std::string text = in.m_text ? in.m_text( e ) : std::string( );
        if ( in.m_shown )
            in.m_shown( e, visible, shown_alpha );
        ui_text_base_t t;
        t.m_alpha = shown_alpha;
        ui_frame_take( e, "Screen Scale", t.m_screen_scale, 8 );
        ui_frame_take( e, "X", &t.m_rect[ 0 ], 4 );
        ui_frame_take( e, "Y", &t.m_rect[ 1 ], 4 );
        ui_frame_take( e, "Width", &t.m_rect[ 2 ], 4 );
        ui_frame_take( e, "Height", &t.m_rect[ 3 ], 4 );
        ui_frame_take( e, "Horizontal Alignment", &t.m_h_align, 4 );
        ui_frame_take( e, "Vertical Alignment", &t.m_v_align, 4 );
        ui_frame_take( e, "No Clip", &t.m_no_clip, 1 );
        ui_frame_take( e, "Single Line", &t.m_single_line, 1 );
        ui_frame_take( e, "Wordbreak", &t.m_wordbreak, 1 );
        ui_frame_take( e, "Horizontal Anchor", &t.m_anchor.m_horizontal, 4 );
        ui_frame_take( e, "Horizontal Ratio", &t.m_anchor.m_h_ratio, 4 );
        ui_frame_take( e, "Vertical Anchor", &t.m_anchor.m_vertical, 4 );
        ui_frame_take( e, "Vertical Ratio", &t.m_anchor.m_v_ratio, 4 );
        t.m_depth = ui_frame_depth( e );
        ui_frame_params( e, in, t.m_params );
        const ui_text_font_t font = in.m_font ? in.m_font( e ) : ui_text_font_t( );
        if ( d.m_kind == ui_frame_kind_t::info )
            return ui_info_text_draw( sort, env, impl.m_crop, in.m_db, t, font, text, in.m_info( e ), d.m_alpha );
        return ui_text_draw( sort, env, impl.m_crop, in.m_db, t, font, text, d.m_alpha );
    }
    if ( d.m_kind == ui_frame_kind_t::image )
    {
        ui_image_t im;
        ui_frame_take( e, "Visible", &im.m_visible, 1 );
        ui_frame_take( e, "Color", &im.m_colour, 4 );
        ui_frame_take( e, "Screen Scale", im.m_screen_scale, 8 );
        ui_frame_take( e, "DstX", &im.m_dst[ 0 ], 4 );
        ui_frame_take( e, "DstY", &im.m_dst[ 1 ], 4 );
        ui_frame_take( e, "DstWidth", &im.m_dst[ 2 ], 4 );
        ui_frame_take( e, "DstHeight", &im.m_dst[ 3 ], 4 );
        ui_frame_take( e, "SrcX", &im.m_src[ 0 ], 4 );
        ui_frame_take( e, "SrcY", &im.m_src[ 1 ], 4 );
        ui_frame_take( e, "SrcWidth", &im.m_src[ 2 ], 4 );
        ui_frame_take( e, "SrcHeight", &im.m_src[ 3 ], 4 );
        ui_frame_take( e, "Rotation", &im.m_rotation, 4 );
        ui_frame_take( e, "FlipX", &im.m_flip_x, 1 );
        ui_frame_take( e, "FlipY", &im.m_flip_y, 1 );
        ui_frame_take( e, "Absolute Coords", &im.m_absolute, 1 );
        ui_frame_take( e, "Horizontal Anchor", &im.m_anchor.m_horizontal, 4 );
        ui_frame_take( e, "Horizontal Ratio", &im.m_anchor.m_h_ratio, 4 );
        ui_frame_take( e, "Vertical Anchor", &im.m_anchor.m_vertical, 4 );
        ui_frame_take( e, "Vertical Ratio", &im.m_anchor.m_v_ratio, 4 );
        im.m_depth = ui_frame_depth( e );
        if ( in.m_shown )
            in.m_shown( e, im.m_visible, im.m_alpha );
        const ui_texture_t* texture = in.m_texture ? in.m_texture( e, 0 ) : nullptr;
        ui_image_component_draw( sort, env, impl.m_crop, impl.m_aspect, im, texture, d.m_alpha );
        return true;
    }
    if ( d.m_kind == ui_frame_kind_t::button )
    {
        ui_button_draw_t b;
        ui_frame_take( e, "Enabled", &b.m_enabled, 1 );
        ui_frame_take( e, "Label", &b.m_label, 1 );
        ui_frame_take( e, "Selected Text Color", &b.m_selected_colour, 4 );
        ui_frame_take( e, "Screen Scale", b.m_screen_scale, 8 );
        ui_frame_take( e, "X", &b.m_rect[ 0 ], 4 );
        ui_frame_take( e, "Y", &b.m_rect[ 1 ], 4 );
        ui_frame_take( e, "Width", &b.m_rect[ 2 ], 4 );
        ui_frame_take( e, "Height", &b.m_rect[ 3 ], 4 );
        ui_frame_take( e, "Horizontal Alignment", &b.m_h_align, 4 );
        ui_frame_take( e, "Vertical Alignment", &b.m_v_align, 4 );
        ui_frame_take( e, "No Clip", &b.m_no_clip, 1 );
        ui_frame_take( e, "Single Line", &b.m_single_line, 1 );
        ui_frame_take( e, "Wordbreak", &b.m_wordbreak, 1 );
        ui_frame_take( e, "Horizontal Anchor", &b.m_anchor.m_horizontal, 4 );
        ui_frame_take( e, "Horizontal Ratio", &b.m_anchor.m_h_ratio, 4 );
        ui_frame_take( e, "Vertical Anchor", &b.m_anchor.m_vertical, 4 );
        ui_frame_take( e, "Vertical Ratio", &b.m_anchor.m_v_ratio, 4 );
        b.m_depth = ui_frame_depth( e );
        ui_frame_params( e, in, b.m_params );
        if ( in.m_button )
            in.m_button( e, b.m_focused, b.m_press );
        ui_button_inputs_t bi;
        const ui_text_font_t font = in.m_font ? in.m_font( e ) : ui_text_font_t( );
        bi.m_font = font.m_asset ? font.m_asset_font : font.m_default ? font.m_default_font : 0u;
        bi.m_text = in.m_text ? in.m_text( e ) : std::string( );
        if ( in.m_texture )
        {
            bi.m_selected = in.m_texture( e, 0 );
            bi.m_unselected = in.m_texture( e, 1 );
            bi.m_pressed = in.m_texture( e, 2 );
        }
        return ui_button_draw( sort, env, impl.m_crop, in.m_db, b, bi, d.m_alpha );
    }
    if ( d.m_kind == ui_frame_kind_t::config || d.m_kind == ui_frame_kind_t::combo_box )
    {
        ui_combo_block_t b = ui_combo_block_of( e );
        for ( int f = 0; f < 2; ++f )
        {
            std::uint8_t* params = f ? b.m_value_params : b.m_label_params;
            std::memset( params, 0, 60 );
            if ( in.m_combo_params_tail )
                in.m_combo_params_tail( e, f, params );
            const ui_slot_t* name = ui_property_value( e, f ? "ValueFont" : "HeadingFont" );
            std::string asset;
            if ( in.m_fontdb )
                ui_font_params( *in.m_fontdb, name ? name->m_text : std::string( ), params, asset );
        }
        float scale[ 2 ] = { 100.0f, 100.0f }, position[ 2 ] = { 0.0f, 0.0f }, spacing = 0.0f;
        ui_frame_take( e, "Screen Scale", scale, 8 );
        ui_frame_take( e, "Position X", &position[ 0 ], 4 );
        ui_frame_take( e, "Position Y", &position[ 1 ], 4 );
        const ui_combo_inputs_t ci = in.m_combo_inputs ? in.m_combo_inputs( e ) : ui_combo_inputs_t( );
        if ( d.m_kind == ui_frame_kind_t::config )
        {
            ui_frame_take( e, "Spacing", &spacing, 4 );
            const ui_config_entity_t* ce = in.m_config( e );
            return !ce || ui_config_draw( sort, env, *ce, b, scale, position, spacing, ui_frame_depth( e ), ci );
        }
        const ui_config_combo_t* c = in.m_combo( e );
        return !c || ui_combo_draw( sort, env, b, *c, position, scale, ui_frame_depth( e ) * 0.005f + 0.5f, ci );   // 0x475B10
    }
    if ( d.m_kind == ui_frame_kind_t::stat )
    {
        ui_boat_stat_fields_t f;
        ui_frame_take( e, "Screen Scale", f.m_screen_scale, 8 );
        ui_frame_take( e, "Position X", &f.m_position[ 0 ], 4 );
        ui_frame_take( e, "Position Y", &f.m_position[ 1 ], 4 );
        ui_frame_take( e, "Spacing", &f.m_spacing, 4 );
        f.m_depth = ui_frame_depth( e );
        ui_boat_stat_draw( sort, env, f, in.m_stat( e ), in.m_texture ? in.m_texture( e, 0 ) : nullptr, in.m_texture ? in.m_texture( e, 1 ) : nullptr );
        return true;
    }
    if ( d.m_kind == ui_frame_kind_t::bar )
    {
        ui_progress_bar_fields_t f;
        ui_frame_take( e, "Screen Scale", f.m_screen_scale, 8 );
        ui_frame_take( e, "Rect", f.m_rect, 16 );
        ui_frame_take( e, "Color", &f.m_colour, 4 );
        ui_frame_take( e, "Initially Visible", &f.m_initially_visible, 1 );
        f.m_depth = ui_frame_depth( e );
        ui_progress_bar_draw( sort, env, f, in.m_bar( e ), d.m_alpha );
        return true;
    }
    if ( d.m_kind == ui_frame_kind_t::difficulty_image || d.m_kind == ui_frame_kind_t::difficulty_text )
        return ui_frame_emit_difficulty( sort, env, e, d.m_kind == ui_frame_kind_t::difficulty_text, in );
    if ( d.m_kind == ui_frame_kind_t::board )
        return in.m_board( e, sort, env );
    if ( d.m_kind == ui_frame_kind_t::leaderboard )
        return in.m_leaderboard( e, sort, env );
    if ( d.m_kind == ui_frame_kind_t::page )
        return in.m_page( e, sort, env, d.m_alpha );
    if ( d.m_kind == ui_frame_kind_t::remap )
        return in.m_remap( e, sort, env, d.m_alpha );
    if ( d.m_kind == ui_frame_kind_t::champ )
        return in.m_champ( e, sort, env, d.m_alpha );
    return true;
}

// Whether a leaf's draw is recreated AND given what it needs (else it counts unrecreated).
inline bool ui_frame_ready( ui_frame_kind_t kind, const ui_frame_inputs_t* in )
{
    switch ( kind )
    {
    case ui_frame_kind_t::other:
    case ui_frame_kind_t::container:
        return false;
    case ui_frame_kind_t::info:
        return in && in->m_info;
    case ui_frame_kind_t::config:
        return in && in->m_config;
    case ui_frame_kind_t::combo_box:
        return in && in->m_combo;
    case ui_frame_kind_t::stat:
        return in && in->m_stat;
    case ui_frame_kind_t::bar:
        return in && in->m_bar;
    case ui_frame_kind_t::difficulty_image:
    case ui_frame_kind_t::difficulty_text:
        return in && in->m_difficulty;
    case ui_frame_kind_t::board:
        return in && in->m_board;
    case ui_frame_kind_t::leaderboard:
        return in && in->m_leaderboard;
    case ui_frame_kind_t::message_box:
        return in && in->m_message_box;
    case ui_frame_kind_t::page:
        return in && in->m_page;
    case ui_frame_kind_t::remap:
        return in && in->m_remap;
    case ui_frame_kind_t::champ:
        return in && in->m_champ;
    default:
        return true;
    }
}
// The message box's fields from the loaded properties (the names verify_ui_
// message_box_draw compares with retail's bytes): the screen scale, the image
// rect, the five blocks' rects / alignments / flags and font params (bytes
// 0..40 from OUR "<block> Font" over the FontDB; 41..59 left 0 - UNKNOWN), the
// button / spinner / touch rects, flips, colours, Depth. The runtime fields are
// the m_message_box input's.
inline void ui_message_box_of( const entity_t& e, const ui_frame_inputs_t& in, ui_message_box_t& b )
{
    static const char* const k_blocks[ 5 ] = { "Heading", "Body", "BodyImage", "ButtonA", "ButtonB" };
    static const char* const k_xywh[ 4 ] = { " X", " Y", " Width", " Height" };
    ui_frame_take( e, "Screen Scale", b.m_screen_scale, 8 );
    for ( int j = 0; j < 4; ++j )
    {
        ui_frame_take( e, ( std::string( "Image" ) + k_xywh[ j ] ).c_str( ), &b.m_image[ j ], 4 );
        ui_frame_take( e, ( std::string( "ButtonA Texture" ) + k_xywh[ j ] ).c_str( ), &b.m_button_rect[ 0 ][ j ], 4 );
        ui_frame_take( e, ( std::string( "ButtonB Texture" ) + k_xywh[ j ] ).c_str( ), &b.m_button_rect[ 1 ][ j ], 4 );
        ui_frame_take( e, ( std::string( "Animated Texture" ) + k_xywh[ j ] ).c_str( ), &b.m_spinner_rect[ j ], 4 );
        ui_frame_take( e, ( std::string( "ButtonA Touch" ) + k_xywh[ j ] ).c_str( ), &b.m_touch[ 0 ][ j ], 4 );
        ui_frame_take( e, ( std::string( "ButtonB Touch" ) + k_xywh[ j ] ).c_str( ), &b.m_touch[ 1 ][ j ], 4 );
    }
    for ( int k = 0; k < 5; ++k )
    {
        ui_message_block_t& t = b.m_blocks[ k ];
        const std::string p = k_blocks[ k ];
        for ( int c = 0; c < 4; ++c )
            ui_frame_take( e, ( p + k_xywh[ c ] ).c_str( ), &t.m_rect[ c ], 4 );
        ui_frame_take( e, ( p + " Horizontal Alignment" ).c_str( ), &t.m_h_align, 4 );
        ui_frame_take( e, ( p + " Vertical Alignment" ).c_str( ), &t.m_v_align, 4 );
        ui_frame_take( e, ( p + " No Clip" ).c_str( ), &t.m_no_clip, 1 );
        ui_frame_take( e, ( p + " Single Line" ).c_str( ), &t.m_single_line, 1 );
        ui_frame_take( e, ( p + " Wordbreak" ).c_str( ), &t.m_wordbreak, 1 );
        std::memset( t.m_params, 0, 60 );
        const ui_slot_t* font = ui_property_value( e, ( p + " Font" ).c_str( ) );
        std::string asset;
        if ( in.m_fontdb )
            ui_font_params( *in.m_fontdb, font ? font->m_text : std::string( ), t.m_params, asset );
    }
    ui_frame_take( e, "ButtonA Texture FlipX", &b.m_button_flip[ 0 ], 1 );
    ui_frame_take( e, "ButtonB Texture FlipX", &b.m_button_flip[ 1 ], 1 );
    ui_frame_take( e, "Selected Text Color", &b.m_selected_colour, 4 );
    ui_frame_take( e, "Animated Texture Color", &b.m_spinner_colour, 4 );
    b.m_depth = ui_frame_depth( e );
}

// The walk both exports run: containers walked into; the message box (with its
// input) drawn - its own records, a leaf entry - then its children walked; every
// other draw a leaf (emitted when ready).
struct ui_frame_walk_t
{
    ui_frame_t m_frame;
    ui_gfx_stacks_t m_stacks;
    int m_depth = 0;
    std::function< void( const entity_t&, int, float ) > m_draw;

    ui_frame_walk_t( const ui_impl_t& impl, ui_gfx_sort_t& sort, const ui_frame_inputs_t* inputs )
    {
        m_draw = [ this, &impl, &sort, inputs ]( const entity_t& e, int component, float a ) { visit( impl, sort, inputs, e, component, a ); };
    }
    ui_frame_walk_t( const ui_frame_walk_t& ) = delete;
    ui_frame_walk_t& operator=( const ui_frame_walk_t& ) = delete;

    void visit( const ui_impl_t& impl, ui_gfx_sort_t& sort, const ui_frame_inputs_t* inputs, const entity_t& e, int component, float a );
};

inline void ui_frame_walk_t::visit( const ui_impl_t& impl, ui_gfx_sort_t& sort, const ui_frame_inputs_t* inputs, const entity_t& e, int component, float a )
{
    ui_frame_t& frame = m_frame;
    ui_gfx_stacks_t& stacks = m_stacks;
    int& depth = m_depth;
    const std::function< void( const entity_t&, int, float ) >& draw = m_draw;
    {
        const ui_draw_target_t* target = ui_draw_target_of( e );
        if ( target && target->m_kind == ui_frame_kind_t::container )
        {
            ++frame.m_containers;
            ++depth;
            ui_container_draw( e, a, draw );
            --depth;
            return;
        }
        if ( target && target->m_kind == ui_frame_kind_t::message_box && ui_frame_ready( target->m_kind, inputs ) )
        {
            ui_message_box_t b;
            ui_message_box_of( e, *inputs, b );
            ui_message_box_inputs_t mi;
            if ( inputs->m_message_box( e, b, mi ) )
            {
                // 0x4675A0: the box's leaf entry (the key / tops it sees after its own pushes), its records, then the children
                const std::size_t at = frame.m_draws.size( );
                ui_frame_draw_t d;
                d.m_entity = &e;
                d.m_component = component;
                d.m_alpha = a;
                d.m_key_lo = sort.m_key_lo;
                d.m_key_hi = ui_message_box_key_hi( sort.m_key_hi );
                std::memcpy( d.m_matrix.data( ), impl.m_crop.m_matrix, 64 );
                d.m_top = impl.m_float;
                d.m_depth = depth;
                d.m_kind = ui_frame_kind_t::message_box;
                d.m_target = target->m_target;
                d.m_emitted = true;
                const std::size_t before = sort.m_commands[ sort.m_index ].size( );
                d.m_first = static_cast< int >( before );
                frame.m_draws.push_back( d );
                ++depth;
                if ( !ui_message_box_draw( sort, stacks, impl, inputs->m_env, inputs->m_db, e, b, mi, a, draw ) )
                    ++frame.m_unknown;
                --depth;
                int inner = 0;
                for ( std::size_t k = at + 1; k < frame.m_draws.size( ); ++k )
                    inner += frame.m_draws[ k ].m_commands;
                frame.m_draws[ at ].m_commands = static_cast< int >( sort.m_commands[ sort.m_index ].size( ) - before ) - inner;
                return;
            }
        }
        ui_frame_draw_t d;
        d.m_entity = &e;
        d.m_component = component;
        d.m_alpha = a;
        if ( !stacks.m_matrices.empty( ) )
            d.m_matrix = stacks.m_matrices.back( );
        if ( !stacks.m_floats.empty( ) )
            d.m_top = stacks.m_floats.back( );
        d.m_key_lo = sort.m_key_lo;
        d.m_key_hi = sort.m_key_hi;
        d.m_depth = depth;
        d.m_kind = target ? target->m_kind : ui_frame_kind_t::other;
        d.m_target = target ? target->m_target : 0u;
        if ( !ui_frame_ready( d.m_kind, inputs ) )
            ++frame.m_unrecreated;
        else if ( inputs )
        {
            const std::size_t before = sort.m_commands[ sort.m_index ].size( );
            if ( !ui_frame_emit( sort, impl, d, *inputs ) )
                ++frame.m_unknown;
            d.m_first = static_cast< int >( before );
            d.m_commands = static_cast< int >( sort.m_commands[ sort.m_index ].size( ) - before );
            d.m_emitted = true;
        }
        frame.m_draws.push_back( d );
    }
}

// `sort` holds the key words before the frame (VuGfxSort +0x70 / +0x74);
// `layer` = the screen's +0x5C (4 from its ctor). With `inputs`, the text /
// image / button leaves emit their records into `sort` in walk order.
inline ui_frame_t ui_screen_frame( const entity_t& screen, const ui_impl_t& impl, ui_gfx_sort_t& sort, float alpha, std::uint32_t layer = 4,
                                   const ui_frame_inputs_t* inputs = nullptr )
{
    ui_frame_walk_t walk( impl, sort, inputs );
    ui_screen_draw( sort, walk.m_stacks, impl, screen, layer, alpha, walk.m_draw );
    return walk.m_frame;
}

// Batch 27, THE EXPORT for a root that is not a VuUIScreenEntity (Screens/
// MessageBox's root is the VuMessageBoxScreenEntity itself - its vtable 0x73D8E4
// has no slot 13): its own first UI draw component's draw (+0x48 order) - for
// the box 0x4675A0 - run as the walk runs every draw, stacks empty.
inline ui_frame_t ui_root_frame( const entity_t& root, const ui_impl_t& impl, ui_gfx_sort_t& sort, float alpha, const ui_frame_inputs_t* inputs = nullptr )
{
    ui_frame_walk_t walk( impl, sort, inputs );
    for ( std::size_t c = 0; c < root.m_components.size( ); ++c )
        if ( root.m_components[ c ].m_vtable == k_ui_draw_component_vtable )
        {
            walk.m_draw( root, static_cast< int >( c ), alpha );
            break;
        }
    return walk.m_frame;
}