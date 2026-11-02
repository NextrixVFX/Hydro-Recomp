#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include <workspace/core/ui/flow.hxx>

// The small logic entities' handlers (graph.hxx dispatches to them), recreated from their disassembly. Verified: checks/rig.hxx
// verify_graph_logic (case graph_logic). `fire( name )` = VuScriptComponent_findPlug 0x504770 + the output plug's vt+8. An output's
// VuParams: the handler's incoming one for VuOneShotFilterEntity's "Out"; an empty one (+0x80 = 0) for the others.

// VuOneShotFilterEntity: +0x60. "In" 0x5DF000: unless set, "Out" with the incoming params, THEN set. "Reset" 0x5DECC0 clears,
// "SetShot" 0x5DECE0 sets.
template < typename F >
inline void one_shot_in( std::uint8_t& shot, F&& fire_out )
{
    if ( shot )
        return;
    fire_out( );
    shot = 1;
}

// VuDelayEntity: +0x60 Delay, +0x64 Random Window, +0x68 pending, +0x6C remaining, +0x70 the Out's VuParams. "In" 0x5DFBE0: unless
// pending: pending, remaining = draw (0x50FD20 on g 0x736810) x window + delay; +0x70 = a fresh empty VuParams (rep movsd of a local
// whose size word alone is written: the Out carries nothing).
template < typename D >
inline void delay_in( std::uint8_t& pending, float& remaining, float delay, float window, D&& draw )
{
    if ( pending )
        return;
    pending = 1;
    remaining = draw( ) * window + delay;
}

// 0x5DFC40 (the "Decision" tick slot 6 0x5DFAF0 registers, and "OnManualTick"): while pending, remaining -= dt; when ordered 0 >
// remaining (comiss / jbe: a NaN never fires): pending 0, "Out".
template < typename F >
inline void delay_update( std::uint8_t& pending, float& remaining, float dt, F&& fire_out )
{
    if ( !pending )
        return;
    remaining -= dt;
    if ( 0.0f > remaining )
    {
        pending = 0;
        fire_out( );
    }
}

// "OnManualTick" 0x5DFB50: its dt = the first parameter when its tag is 2, else 0.
inline float delay_manual_dt( const std::uint8_t* params, std::int32_t size )
{
    float dt = 0.0f;
    std::uint32_t tag = 0xFFFFFFFFu;
    if ( size > 0 )
    {
        std::memcpy( &tag, params, 4 );
        if ( tag == 2u )
            std::memcpy( &dt, params + 4, 4 );
    }
    return dt;
}

// VuBooleanEntity: +0x60. setValue 0x5E5EA0: nothing when equal (the byte); else stored, "OnChanged", then "OnChangedTrue" /
// "OnChangedFalse" by the byte.
template < typename F >
inline void boolean_set( std::uint8_t& value, std::uint8_t v, F&& fire )
{
    if ( v == value )
        return;
    value = v;
    fire( "OnChanged" );
    fire( v ? "OnChangedTrue" : "OnChangedFalse" );
}

// "Set" 0x5E5E10's byte: the first parameter when its tag is 3, else 0.
inline std::uint8_t boolean_set_value( const std::uint8_t* params, std::int32_t size )
{
    std::uint8_t v = 0;
    std::uint32_t tag = 0xFFFFFFFFu;
    if ( size > 0 )
    {
        std::memcpy( &tag, params, 4 );
        if ( tag == 3u )
            v = params[ 4 ];
    }
    return v;
}

// VuGenericEventEntity "Broadcast" 0x5F0330's VuParams: the Event Name as a string parameter (dropped when it does not fit: the
// 0x80 test); the event is "OnGenericEvent" (the FNV-1a at 0x5F03C7).
inline ui_params_t generic_broadcast_params( const std::string& name )
{
    ui_params_t p;
    p.add_string( name.c_str( ) );
    return p;
}

// The "OnGenericEvent" handler 0x5F0420: VuParams_readString 0x505260 (a size <= 0 or a first tag other than 4: the static empty
// string 0x64D283) against the entity's own Event Name, equal -> "OnReceived" (empty VuParams).
inline bool generic_receive( const std::string& own, const std::uint8_t* params, std::int32_t size )
{
    const char* carried = "";
    std::uint32_t tag = 0xFFFFFFFFu;
    if ( size > 0 )
    {
        std::memcpy( &tag, params, 4 );
        if ( tag == 4u )
            carried = reinterpret_cast< const char* >( params + 4 );
    }
    return own == carried;
}
