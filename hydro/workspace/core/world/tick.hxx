#pragma once

#include <cstdint>
#include <cstring>
#include <list>
#include <string>
#include <xmmintrin.h>

// VuTickManagerImpl (vtables 0x74BEBC / 0x74BF04, ctor 0x4F6F60): the
// per-frame phase list every entity registers its ticks in. Recreated from
// retail's decompilation / disassembly:
//
//   0x4F6F60  ctor: +8 the dt cap = FLT_MAX (0x7F7FFFFF), +0xC the phase list,
//             +0x18 the last clock reading (double) 0, +0x20 paused, +0x21
//             slow motion, +0x22 single step, +0x24 the pause count, +0x28 /
//             +0x2C / +0x30 0.
//   slot 5    0x4F7050 (init): the seventeen phases, in this order, each
//             0x4F75C0(name, flag) = 0x4F6D50 (record: +0 the name, +0x18 the
//             handler list, +0x20 the flag, +0x21 enabled = 1) pushed on the
//             BACK of the list.
//   slot 7    0x4F7530: the dt cap.
//   slot 8    0x4F7550(handler, phase): the first phase whose name equals
//             (0x4F76C0) gets the handler on the BACK of its list (0x4F6E00);
//             an unknown phase drops it (returns 0, the handler not freed).
//   slot 9    0x4F7570(owner, phase): from that phase, every handler whose
//             slot 1 (its owner) is `owner` is destroyed and removed (0x4F7890).
//   slot 10   0x4F7590(owner): the same over every phase.
//   slots 11 / 12 / 13  0x4F6E70 / 0x4F6E80 / 0x4F6E90: +0x24 ++ / -- / = 0.
//   slot 6    0x4F72F0 (tick): see tick_run.
//
// HOOKS / NOT RECREATED: the clock (g_vumetrosys vt+0x28, a double - the
// platform layer: tick_run takes its reading); the dev menu ("TickManager/
// <phase>" toggles each record's +0x21, slot 1 0x4F7150), the dev stat page
// ("%8s: %3d handlers") and the keyboard callback (slot 2 0x4F72C0) - dev
// only. A handler is a VuMethod1<T, void, float>: here an owner key and a
// callable.
struct tick_handler_t
{
    const void* owner = nullptr;   // slot 1 (VuMethod +4, the object)
    void ( *fn )( void* user, float dt ) = nullptr;
    void* user = nullptr;
};

struct tick_phase_t
{
    std::string name;                  // record +0
    std::list< tick_handler_t > handlers;   // +0x18
    bool last_dt = false;              // +0x20: 0x4F75C0's flag - ticks with the previous frame's dt
    bool enabled = true;               // +0x21
};

struct tick_manager_t
{
    float cap = 3.4028235e38f;         // +8
    std::list< tick_phase_t > phases;  // +0xC
    double clock = 0.0;                // +0x18
    bool paused = false;               // +0x20
    bool slow = false;                 // +0x21
    bool step = false;                 // +0x22
    std::int32_t pause_count = 0;      // +0x24
    float dt = 0.0f;                   // +0x28: this frame's, capped
    float raw = 0.0f;                  // +0x2C
    float previous = 0.0f;             // +0x30: the last frame's +0x28
};

// slot 5 0x4F7050.
inline void tick_manager_init( tick_manager_t& tm )
{
    static const struct
    {
        const char* name;
        bool flag;
    } k_phases[] = { { "DynamicsSync", false }, { "Network", true }, { "Input", true }, { "Services", true }, { "PreDecision", true },
        { "Decision", true }, { "PostDecision", true }, { "Triggers", true }, { "Viewports", true }, { "Motion", true }, { "Water", false },
        { "Anim", true }, { "Corona", true }, { "DynamicsKick", false }, { "Build", true }, { "Final", true }, { "Audio", true } };
    for ( const auto& p : k_phases )
    {
        tick_phase_t r{};
        r.name = p.name;
        r.last_dt = p.flag;
        tm.phases.push_back( std::move( r ) );
    }
}

// 0x4F76C0: the first phase whose name equals `name` (length and bytes).
inline tick_phase_t* tick_find_phase( tick_manager_t& tm, const char* name )
{
    for ( tick_phase_t& p : tm.phases )
        if ( p.name == name )
            return &p;
    return nullptr;
}

// slot 8 0x4F7550.
inline bool tick_register( tick_manager_t& tm, const tick_handler_t& h, const char* phase )
{
    tick_phase_t* p = tick_find_phase( tm, phase );
    if ( !p )
        return false;
    p->handlers.push_back( h );
    return true;
}

// 0x4F7890 on one phase's list.
inline void tick_remove_owner( tick_phase_t& p, const void* owner )
{
    for ( auto it = p.handlers.begin( ); it != p.handlers.end( ); )
        it = it->owner == owner ? p.handlers.erase( it ) : std::next( it );
}

// slot 9 0x4F7570.
inline void tick_unregister( tick_manager_t& tm, const void* owner, const char* phase )
{
    if ( tick_phase_t* p = tick_find_phase( tm, phase ) )
        tick_remove_owner( *p, owner );
}

// slot 10 0x4F7590.
inline void tick_unregister_all( tick_manager_t& tm, const void* owner )
{
    for ( tick_phase_t& p : tm.phases )
        tick_remove_owner( p, owner );
}

// slot 6 0x4F72F0, with the clock's reading `now` (seconds, double). raw =
// (float)(now - +0x18), +0x18 = now; dt = maxss( 0, minss( cap, raw ) ) into
// +0x28; the previous-frame dt read from +0x30. Paused: a pending single step
// is consumed and the frame runs, otherwise both dts are 0. Slow motion: both
// x 0.1 (0x6F7610). A pause count: both 0. Then every enabled phase in order,
// each handler in order, with the previous dt when the phase's flag is set,
// else this frame's. Last, +0x30 = +0x28 (the capped dt, before the pause /
// slow-motion terms). Both loops read the next node AFTER the call and compare
// it with the list head re-read (0x4F7415 / 0x4F741D, 0x4F7431 / 0x4F7433):
// a handler registered during a call lands on the back of its phase and runs
// in the SAME pass when that phase is the running one or a later one, on the
// next tick when it is earlier (std::list's range-for has the same order;
// oracle case tick_same_pass). A handler unregistering ITSELF during its call
// leaves retail reading a freed node - not modelled (std::list: undefined).
inline void tick_run( tick_manager_t& tm, double now )
{
    const float raw = static_cast< float >( now - tm.clock );
    tm.clock = now;
    tm.raw = raw;
    float previous = tm.previous;
    float dt = _mm_cvtss_f32( _mm_max_ss( _mm_setzero_ps( ), _mm_min_ss( _mm_set_ss( tm.cap ), _mm_set_ss( raw ) ) ) );
    tm.dt = dt;
    if ( tm.paused )
    {
        if ( tm.step )
            tm.step = false;
        else
            dt = previous = 0.0f;
    }
    if ( tm.slow )
    {
        dt = dt * 0.1f;
        previous = previous * 0.1f;
    }
    if ( tm.pause_count != 0 )
        dt = previous = 0.0f;
    std::uint32_t phase_index = spikes::k_phase;   // ours: utility/spikes.hxx
    for ( tick_phase_t& p : tm.phases )
    {
        const spikes::scope_t spike( phase_index++ );
        if ( !p.enabled )
            continue;
        const float d = p.last_dt ? previous : dt;
        for ( const tick_handler_t& h : p.handlers )
            h.fn( h.user, d );
    }
    tm.previous = tm.dt;
}
