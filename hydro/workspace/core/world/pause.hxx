#pragma once

#include <workspace/core/ui/flow.hxx>

#include <cstdint>
#include <cstring>
#include <string>

// The pause menu (VuActionGameMode +0x60; ctor 0x4BAE80, world batch 32): ONLY its gate. Past it - the FSM +8 (states Unpaused
// 0x4BBD10 / 0x4BBD90, Paused 0x4BBE30, Transition 0x4BBEE0), the screen stack +0x68, the screen project +0x7C, the screen
// events 0x4BB680 .. 0x4BB8E0, OnSettingsCommand 0x4BB980, the tick 0x4BB540, the draw 0x4BB5E0 - NOT RECREATED: the menu
// is not built, so `next` is read by nobody yet.
//
// The ctor: the dword +0x30 = 0x100, so +0x30 = 0 (the paused flag, written by the FSM's handlers, not read here) and +0x31 = 1.
struct pause_menu_t
{
    bool paused = false;      // +0x30
    bool allow = true;        // +0x31
    std::string next;         // +0x38, the screen set next
};

// "OnSetAllowPause" 0x4BB900 (thiscall menu, const VuParams&; retn 4): +0x31 = the params' first record's byte when there is
// a record (+0x80 > 0) and its type word is 3 (bool), else 0.
inline void pause_on_set_allow( pause_menu_t& m, const ui_params_t& p )
{
    bool allow = false;
    if ( p.m_size > 0 )
    {
        std::uint32_t type;
        std::memcpy( &type, p.m_bytes, 4 );
        if ( type == 3u )
            allow = p.m_bytes[ 4 ] != 0;
    }
    m.allow = allow;
}

// "OnPauseActionGame" 0x4BB660 (thiscall menu, params - unread; retn 4): not paused and allowed: next = "Screens/PauseMenu".
inline void pause_on_pause( pause_menu_t& m )
{
    if ( !m.paused && m.allow )
        m.next = "Screens/PauseMenu";
}
