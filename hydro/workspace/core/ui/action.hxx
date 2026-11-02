#pragma once

#include <workspace/core/ui/flow.hxx>

#include <cstdint>
#include <functional>
#include <string>

// The front end's screen actions, transliterated.
//
// VuUIAction (ctor 0x470750, vtable 0x73EB40) is a VuEntity (0x500710) with
// one VuScriptComponent (order 150, enabled) holding ONE input plug, "Trigger"
// (0x472800: the output initialiser, then the VuScriptInputPlug vtable and a
// VuMethod1 bound to the thunk 0x4077E3 = jmp [vtable + 0x30]). So a trigger
// runs the class's slot 12 with the trigger's params; slot 12 is pure in the
// base. The subclasses:
//   VuPushScreenUIAction  (ctor 0x471780, vtable 0x73E8F8) slot 12 0x4718C0
//   VuSetScreenUIAction   (ctor 0x4714C0, vtable 0x73E930) slot 12 0x471600
//     both: +92 "Screen", a VuAssetNameProperty (0x406B70 + vtable 0x74D690)
//     over the factory's "VuProjectAsset" names (slot 12, the editor's
//     choices), default "";
//   VuPopScreenUIAction   (create 0x471A20, vtable 0x73E8C0) slot 12 0x4719C0
//   VuReloadScreenUIAction (create 0x471B10, vtable 0x73E888) slot 12 0x471AB0
//     no properties of their own.
// Each slot 12 builds a fresh VuParams (count 0), for push / set adds the
// screen as a string record when strlen + 5 <= 0x80 (else nothing), broadcasts
// the FNV-1a hash of its event name through VuEventManager_broadcast 0x505690
// (g 0x759028), and returns VuRetVal 0. The trigger's own params are never
// read.

enum class ui_action_kind_t
{
    push,
    pop,
    set,
    reload,
    set_game_mode,  // VuSetGameModeUIAction (vtable 0x73E6E0), slot 12 0x472740
    start_game,     // VuStartActionGameUIAction (ctor 0x470920, vtable 0x73EB08), slot 12 0x470A80: "OnStartActionGame" + its +0x5C "Type" (default "AttractEvent")
    clear_results,  // VuClearGameResultsUIAction (create 0x4721D0, vtable 0x73E788), slot 12 0x4721A0: gameutil +0x50 ["GameResults"] cleared (0x4FC090, 0x4FBB10)
    presence        // VuSetMenuPresenceUIAction (ctor 0x472010, vtable 0x73E7E0), slot 12 0x4050B0: VuRetVal 0, nothing else
};

// The class's kind; false for a type that is not one of the four.
inline bool ui_action_kind_of( const std::string& cls, ui_action_kind_t& kind )
{
    if ( cls == "VuPushScreenUIAction" )
        kind = ui_action_kind_t::push;
    else if ( cls == "VuPopScreenUIAction" )
        kind = ui_action_kind_t::pop;
    else if ( cls == "VuSetScreenUIAction" )
        kind = ui_action_kind_t::set;
    else if ( cls == "VuReloadScreenUIAction" )
        kind = ui_action_kind_t::reload;
    else if ( cls == "VuSetGameModeUIAction" )
        kind = ui_action_kind_t::set_game_mode;
    else if ( cls == "VuStartActionGameUIAction" )
        kind = ui_action_kind_t::start_game;
    else if ( cls == "VuClearGameResultsUIAction" )
        kind = ui_action_kind_t::clear_results;
    else if ( cls == "VuSetMenuPresenceUIAction" )
        kind = ui_action_kind_t::presence;
    else
        return false;
    return true;
}

class c_ui_action
{
public:
    c_ui_action( ui_action_kind_t kind, std::string screen ) : m_kind( kind ), m_screen( std::move( screen ) ) { }

    // The Trigger plug -> 0x4077E3 -> slot 12. Returns the VuRetVal's type
    // word (0, nothing).
    std::uint32_t trigger( const ui_params_t& )
    {
        ui_params_t p;
        const char* event = nullptr;
        switch ( m_kind )
        {
        case ui_action_kind_t::push:
            p.add_string( m_screen.c_str( ) );
            event = "OnPushScreen";
            break;
        case ui_action_kind_t::set:
            p.add_string( m_screen.c_str( ) );
            event = "OnSetScreen";
            break;
        case ui_action_kind_t::pop:
            event = "OnPopScreen";
            break;
        case ui_action_kind_t::reload:
            event = "OnReloadScreen";
            break;
        case ui_action_kind_t::set_game_mode:
            // 0x472740: no broadcast (ui_set_game_mode below; m_screen holds
            // its +92 "Game Mode"). Without a game-data root nothing is written.
            if ( m_root )
                ui_json_set_string( ui_json_child( ui_json_child( *m_root, "GameConfig" ), "Mode" ), m_screen.c_str( ) );
            return 0;
        case ui_action_kind_t::start_game:
            p.add_string( m_screen.c_str( ) );
            event = "OnStartActionGame";
            break;
        case ui_action_kind_t::clear_results:
            if ( m_root )
                ui_json_child( *m_root, "GameResults" ) = value_t( );
            return 0;
        case ui_action_kind_t::presence:
            return 0;
        }
        if ( m_broadcast )
            m_broadcast( fsm_hash( event ), p );
        return 0;
    }

    ui_action_kind_t m_kind;
    std::string m_screen;  // +92 "Screen" (push / set), "Game Mode" (set game mode)
    value_t* m_root = nullptr;  // gameutil +80 (set game mode only)
    // HOOK: VuEventManager_broadcast 0x505690 (immediate: each subscriber runs inside the call).
    std::function< void( std::uint32_t, const ui_params_t& ) > m_broadcast;
};

// VuSetGameModeUIAction (create 0x472790; ctor 0x472650 over VuUIAction
// 0x470750; vtable 0x73E6E0): +92 "Game Mode", a VuStringProperty (0x406B70,
// the base string node vtable 0x7374B8), default "". Its Trigger runs slot 12
// 0x472740: gameutil +80 (`root`) ["GameConfig"] ["Mode"] (0x4FC090 twice:
// each made a map if it is not) = the text up to its first NUL (0x4FC640);
// VuRetVal 0 (its type word only). No broadcast; the trigger's params are
// never read. (c_ui_action's set_game_mode kind runs the same write for the
// wiring.)
inline std::uint32_t ui_set_game_mode( value_t& root, const std::string& mode )
{
    ui_json_set_string( ui_json_child( ui_json_child( root, "GameConfig" ), "Mode" ), mode.c_str( ) );
    return 0;
}
