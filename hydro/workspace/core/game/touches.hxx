#pragma once

// Small UI entity handlers (boat batch 35), case ui_odds (cases/msgbox.py). Nothing here is wired: the world / UI agents' dispatch calls them.

#include <cstdint>
#include <cstring>
#include <functional>

// 0x440890 VuBarracudaTrackUIImageEntity's image pick: the list's index for the clock +0xC4 and the base +0xC8, -1 on an empty list (retail
// answers 0). Measured for base >= 0 only: retail's `%` on a negative base was not run.
inline int barracuda_pick( std::uint32_t count, float clock, std::int32_t base )
{
    if ( !count )
        return -1;
    int step = 0;
    if ( clock > 15.0f )
        step = static_cast< int >( ( clock - 15.0f ) / 5.0f ) + 1;
    return static_cast< int >( static_cast< std::uint32_t >( step + base ) % count );
}

// 0x440900 "OnUITick": a first record of type 2 adds its float to the clock; any other adds 0.
inline void barracuda_tick( float& clock, const std::uint8_t* params, std::int32_t size )
{
    float add = 0.0f;
    if ( size > 0 )
    {
        std::int32_t type = -1;
        std::memcpy( &type, params, 4 );
        if ( type == 2 )
            std::memcpy( &add, params + 4, 4 );
    }
    clock = clock + add;
}

// 0x46C950 VuTiltSteeringSettingsEntity "IsTilt": the VuMetroSys answers vt+0x54 / vt+0x58 and the settings' +0x2E (chooser.hxx's same choice).
inline std::uint8_t tilt_answer( bool a, bool b, std::uint8_t setting )
{
    if ( a )
        return b ? setting : 0;
    return b ? 1 : 0;
}

// VuOnTouchSpecialEntity: 0x476120 the VuTouch callback (type +0x64, armed +0x68), 0x476140 fires once.
struct touch_special_t
{
    std::int32_t m_type = 0;
    std::uint8_t m_armed = 0;
};

inline void touch_special_hit( touch_special_t& e, std::int32_t type )
{
    if ( e.m_type == type )
        e.m_armed = 1;
}

inline void touch_special_tick( touch_special_t& e, const std::function< void( const char* ) >& play, const std::function< void( ) >& trigger )
{
    if ( !e.m_armed )
        return;
    play( "Global/UI_and_Menu/main_menu_choose" );    // off 0x746D50[ 0 ]
    trigger( );
    e.m_armed = 0;
}
