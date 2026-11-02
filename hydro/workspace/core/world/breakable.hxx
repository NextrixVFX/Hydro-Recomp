#pragma once

// VuBreakablePropEntity (vtables 0x73A14C / +0x5C 0x73A13C / +0x7C 0x73A134; create 0x433EE0, ctor 0x433F80 over
// VuPropEntity's 0x5464C0) - transliterated from the disassembly.
//
// The ctor: a Vu3dDrawBreakableModelComponent (0xF0 bytes, 0x433530 + 0x542D70) at +0x80; its own nodes - "Threshold
// Speed (MPH)" +0x84 (float, 25), "Pfx Name" +0x88 (VuStringProperty 0x406B70), "Sfx Name" +0xA0 (vt 0x73A18C, the same
// load), "Cam Shake Magnitude" +0xB8 / "Duration" +0xBC / "Falloff Time" +0xC0 / "Frequency" +0xC4, "Rumble Low Freq"
// +0xC8 / "High Freq" +0xCC (floats, 0); the output "OnBreak" (an entity parameter, 0x505400(.., 1, 0xB)); then the
// rigid component's (+0x74) +0xF4 = the +0x7C part (the contact callback; its body's +0x2C4 when one exists) and +0xD8
// |= 1 (the body's +0x2D0). Runtime: +0xD0 the break request, +0xD4 .. +0xDF the hit velocity, +0xE4 a weak ref whose
// object (+0xF0) is the target.
// Slot 6 0x434620 (gameInitialize): VuPropEntity's 0x546910 (world/bodies.hxx bodies_prop_game_initialize), then the
// "Decision" tick (0x4077E3 -> vt+0x30 0x434A20) and the "Build" tick (0x4077ED -> vt+0x34 0x434D90: the breakable
// component's tick 0x5436B0 - the debris' draw, the renderer's).
// Slot 7 0x4346D0: the static draw off, the body out of the world when added, 0x544A20, the ticks unregistered.

#include <workspace/core/boat/events.hxx>
#include <workspace/core/entity/property.hxx>
#include <workspace/core/entity/rigid.hxx>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <xmmintrin.h>

// VuBreakablePropEntity's input "Break" 0x4349F0 (the ctor's plug; thiscall entity, VuRetVal*, params; retn 8): the weak ref +0xE4
// released (vt+0), the request +0xD0 = 1, VuRetVal 0.
inline void breakable_on_break( std::uint8_t& request, const void*& target )
{
    target = nullptr;
    request = 1;
}

struct breakable_t
{
    std::uint8_t visible = 1;        // VuPropEntity's "Initially Visible" (+0x78)
    float threshold_mph = 25.0f;     // +0x84
    std::string pfx;                 // +0x88
    std::string sfx;                 // +0xA0
    float shake[ 4 ] = { };          // +0xB8 .. +0xC4: Magnitude, Duration, Falloff Time, Frequency
    float rumble[ 2 ] = { };         // +0xC8 / +0xCC: Low, High
};

struct breakable_state_t
{
    breakable_t value;
    breakable_t defaults;
};

inline std::shared_ptr< void > breakable_state_for( const std::string& cls )
{
    return cls == "VuBreakablePropEntity" ? std::make_shared< breakable_state_t >( ) : nullptr;
}

inline void breakable_load_properties( entity_t& e, const value_t& p )
{
    if ( e.m_type != "VuBreakablePropEntity" || !e.m_state )
        return;
    breakable_state_t& s = *static_cast< breakable_state_t* >( e.m_state.get( ) );
    breakable_t& v = s.value;
    const breakable_t& d = s.defaults;
    property_bool( p, "Initially Visible", v.visible, d.visible );
    property_float( p, "Threshold Speed (MPH)", v.threshold_mph, d.threshold_mph, false );
    property_string( p, "Pfx Name", v.pfx, d.pfx );
    property_string( p, "Sfx Name", v.sfx, d.sfx );
    property_float( p, "Cam Shake Magnitude", v.shake[ 0 ], d.shake[ 0 ], false );
    property_float( p, "Cam Shake Duration", v.shake[ 1 ], d.shake[ 1 ], false );
    property_float( p, "Cam Shake Falloff Time", v.shake[ 2 ], d.shake[ 2 ], false );
    property_float( p, "Cam Shake Frequency", v.shake[ 3 ], d.shake[ 3 ], false );
    property_float( p, "Rumble Low Freq", v.rumble[ 0 ], d.rumble[ 0 ], false );
    property_float( p, "Rumble High Freq", v.rumble[ 1 ], d.rumble[ 1 ], false );
}

inline void breakable_mark_properties( entity_t& e )
{
    if ( e.m_type == "VuBreakablePropEntity" && e.m_state )
    {
        breakable_state_t& s = *static_cast< breakable_state_t* >( e.m_state.get( ) );
        s.defaults = s.value;
    }
}

// The contact callback 0x434730 (thiscall on the +0x7C part; the contact record; retn 4), whole. `own` / `other` are
// body images (+0x40 origin, +0x140 linear, +0x150 angular velocity, +0xD8 / +0x2D0 flags); `point` the record's +0xC,
// `normal` its +0x1C. Other's +0xD8 bit 2 (no contact response) -> nothing (answer 0). Else each body's velocity at
// the point (linear + angular x (point - origin), retail's operand order), d = normal . (own - other) as
// ((ny dy + dx nx) + nz dz); when Threshold x -0.44704 (0x6F7890, mph -> m/s, negated) > d (comiss): the component's
// +0xD0 |= 4 copied to the own body's +0xD8 and the other's velocity kept (+0xD4: `velocity`; the 4th word +0xE0 is an
// unwritten stack word - UNKNOWN, not modelled); else only when the other's +0x2D0 bit 1: the flags as above, no
// velocity. Either way then: the weak ref +0xE4 re-pointed at the other's owner (+0x2C0, or cleared - the caller's)
// and +0xD0 = 1. Answers 0 (nothing), 1 (with the velocity), 2 (by the other's flag).
inline int breakable_contact( float threshold_mph, const std::uint8_t* own, const std::uint8_t* other, const float* point, const float* normal,
                              std::uint8_t& request, float* velocity, rigid_component_t& c, std::uint8_t* own_w )
{
    const auto f = []( const std::uint8_t* b, int at ) { float v; std::memcpy( &v, b + at, 4 ); return v; };
    std::uint32_t of;
    std::memcpy( &of, other + 0xD8, 4 );
    if ( ( of >> 2 ) & 1u )
        return 0;
    const float px = point[ 0 ], py = point[ 1 ], pz = point[ 2 ];
    // the other's velocity at the point
    const float rxo = sse_sub( px, f( other, 0x40 ) ), ryo = sse_sub( py, f( other, 0x44 ) ), rzo = sse_sub( pz, f( other, 0x48 ) );
    const float owx = f( other, 0x150 ), owy = f( other, 0x154 ), owz = f( other, 0x158 );
    const float vox = sse_add( sse_sub( sse_mul( rzo, owy ), sse_mul( ryo, owz ) ), f( other, 0x140 ) );
    const float voz = sse_add( sse_sub( sse_mul( ryo, owx ), sse_mul( rxo, owy ) ), f( other, 0x148 ) );
    const float voy = sse_add( sse_sub( sse_mul( rxo, owz ), sse_mul( rzo, owx ) ), f( other, 0x144 ) );
    // the own body's
    const float rzs = sse_sub( pz, f( own, 0x48 ) ), rxs = sse_sub( px, f( own, 0x40 ) ), rys = sse_sub( py, f( own, 0x44 ) );
    const float swx = f( own, 0x150 ), swy = f( own, 0x154 ), swz = f( own, 0x158 );
    const float vsx = sse_add( sse_sub( sse_mul( rzs, swy ), sse_mul( rys, swz ) ), f( own, 0x140 ) );
    const float vsy = sse_add( sse_sub( sse_mul( rxs, swz ), sse_mul( rzs, swx ) ), f( own, 0x144 ) );
    const float vsz = sse_add( sse_sub( sse_mul( rys, swx ), sse_mul( rxs, swy ) ), f( own, 0x148 ) );
    const float dx = sse_sub( vsx, vox ), dy = sse_sub( vsy, voy ), dz = sse_sub( vsz, voz );
    const float d = sse_add( sse_add( sse_mul( normal[ 1 ], dy ), sse_mul( dx, normal[ 0 ] ) ), sse_mul( normal[ 2 ], dz ) );
    const std::uint32_t k_bits = 0xBEE4E26Du;   // 0x6F7890
    float k;
    std::memcpy( &k, &k_bits, 4 );
    const float lim = sse_mul( threshold_mph, k );
    int answer = 0;
    if ( _mm_comigt_ss( _mm_set_ss( lim ), _mm_set_ss( d ) ) )
    {
        velocity[ 0 ] = vox;
        velocity[ 1 ] = voy;
        velocity[ 2 ] = voz;
        answer = 1;
    }
    else
    {
        std::uint32_t of2;
        std::memcpy( &of2, other + 0x2D0, 4 );
        if ( !( of2 & 2u ) )
            return 0;
        answer = 2;
    }
    c.flags_d0 |= 4u;
    if ( own_w )
        std::memcpy( own_w + 0xD8, &c.flags_d0, 4 );
    request = 1;
    return answer;
}
