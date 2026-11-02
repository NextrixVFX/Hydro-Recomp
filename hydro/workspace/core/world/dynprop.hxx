#pragma once

#include <workspace/core/boat/events.hxx>
#include <workspace/core/boat/hull.hxx>
#include <workspace/core/boat/math.hxx>
#include <workspace/core/world/query.hxx>
#include <workspace/core/entity/node.hxx>
#include <workspace/core/entity/property.hxx>
#include <workspace/core/entity/rigid.hxx>
#include <workspace/core/entity/values.hxx>
#include <workspace/core/game/types.hxx>
#include <workspace/core/world/bodies.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <xmmintrin.h>

// The level's dynamic game props (world batch 23): VuDynamicGamePropEntity (create
// 0x436740, ctor 0x4367E0) and VuCheckpointBuoyEntity (create 0x434EE0, ctor
// 0x434F80 over 0x4367E0), both over VuDynamicPropEntity's ctor 0x545360. Their
// property sets, the postLoad's component step (0x5459F0), the motion state
// (vt 0x73A260 / 0x73A580 slot 1 0x545C00) and gameInitialize (vt slot 6
// 0x437580 -> VuDynamicPropEntity's 0x545A80 -> createRigidBody 0x544100 ->
// the show vt+0x30 0x4381B0 -> 0x545EC0), checked against retail by the case
// dynamic_prop_bodies. VuFloatingMineEntity (world/mine.hxx) and
// VuBreakableDynamicGamePropEntity (create 0x433040, ctor 0x433160 over 0x4367E0:
// vtable 0x739F14 - slots 6 / 7 / 12 / 13 the base's, 15 Build 0x433AE0, 16
// Decision 0x433890 (session.hxx race_session_breakable_dynamic_tick); its
// +0xB4 contact 0x4335A0 = breakable_contact, case breakable_dynamic_contact;
// its properties case breakable_dynamic_properties) ride the same walk.

// VuDynamicPropEntity's three nodes (ctor 0x545360, in its +0x44 order):
//   "Initially Visible" +0x74 bool (vt 0x737450)  default 1
//   "Mass"              +0x78 float (vt 0x737484) default 100 (0x42C80000)
//   "Center of Mass"    +0x7C vector3 (vt 0x737A84) default 0 (the w lane +0x88
//                       is never written by the ctor - UNKNOWN, not modelled)
// (the Mass / Center of Mass watcher 0x5460E0 has notify 0: a load never fires
// it; postLoad does its work instead.) Then VuDynamicGamePropEntity's nodes
// (0x4367E0; the schema's captured rows, offsets +0xB8 .. +0x142): floats vt
// 0x737484, vector3 0x737A84, bools 0x737450, "Splash Pfx Name" +0xD8 a
// VuStringProperty (0x406B70, vt 0x7374B8) and "Sfx Name" +0x110 (vt 0x73A18C) -
// both load through 0x406C20, property_string's contract. The buoy adds
// "CheckPoint Index" +0x184 (VuBasicProperty<int> vt 0x73A218, default 0).
struct dynamic_prop_t
{
    std::uint8_t visible = 1;            // +0x74
    float mass = 100.0f;                 // +0x78
    float centre[ 3 ] = { 0, 0, 0 };     // +0x7C
    float fluids_radius = 0.0f;          // +0xB8
    float fluids_density = 0.5f;         // +0xBC
    float fluids_drag = 0.0f;            // +0xC0 (0x3EF0A3D7)
    float fluids_lift = 0.0f;            // +0xC4 (0x3E4CCCCD)
    float hydro_centre[ 3 ] = { 0, 0, 0 };   // +0xC8
    std::string splash_pfx;              // +0xD8
    float splash_speed = 10.0f;          // +0xF0
    float linear_damping = 0.5f;         // +0xF4
    float angular_damping = 0.5f;        // +0xF8
    std::uint8_t linear_anchor = 0;      // +0xFC
    std::uint8_t angular_anchor = 0;     // +0xFD
    float anchor_spring = 1.0f;          // +0x100
    float anchor_damping = 1.0f;         // +0x104
    std::uint8_t hard_hit = 0;           // +0x108
    float threshold_mph = 25.0f;         // +0x10C
    std::string sfx;                     // +0x110
    float shake_magnitude = 0.0f, shake_duration = 0.0f, shake_falloff = 0.0f, shake_frequency = 0.0f;   // +0x128 .. +0x134
    float rumble_low = 0.0f, rumble_high = 0.0f;   // +0x138 / +0x13C
    std::uint8_t throw_ai = 0;           // +0x140
    std::uint8_t initially_active = 1;   // +0x141
    std::uint8_t collide_static = 1;     // +0x142
    std::int32_t checkpoint_index = 0;   // +0x184 (the buoy only)
    // VuBreakableDynamicGamePropEntity (ctor 0x433160 over 0x4367E0): "Breakable Pfx Name" +0x188 (VuStringProperty 0x406B70,
    // 0x406C20's load: property_string), after the base nodes.
    std::string breakable_pfx;
    // VuFloatingMineEntity (ctor 0x438690): the explosion part at +0x184 (defaults 0x438BA0, nodes 0x5476D0 - nine
    // VuBasicProperty<float> +0x194 .. +0x1B4 and two VuStringProperty +0x1B8 / +0x1D0) and the point wave's seven
    // floats +0x1F8 .. +0x210 (the descriptor +0x1E8 .. +0x213 VuWater_createPointWave 0x528E10 takes).
    float explosion[ 9 ] = { 50.0f, 10.0f, 0.25f, 0.0f, 1.0f, 50.0f, 1.0f, 1.0f, 64.0f };   // +0x194: Outer / Inner Radius,
                                         // Cam Shake Magnitude / Duration / Falloff Time / Frequency, Rumble Low / High, Lens Splash
    std::string explosion_pfx;           // +0x1B8 "Explosion Pfx System Name"
    std::string explosion_audio;         // +0x1D0 "Explosion Audio Event Name"
    float wave[ 7 ] = { 5.0f, 1.0f, 0.0f, 20.0f, 10.0f, 5.0f, 1.0f };   // +0x1F8: Falloff Time, Magnitude, Range Start / End,
                                                                    // Speed, Frequency, Foaminess
};

// The mine's node names, in each part's order.
inline constexpr const char* k_mine_explosion_names[ 9 ] = {
    "Explosion Outer Radius", "Explosion Inner Radius", "Explosion Cam Shake Magnitude", "Explosion Cam Shake Duration",
    "Explosion Cam Shake Falloff Time", "Explosion Cam Shake Frequency", "Explosion Rumble Low Freq Mag", "Explosion Rumble High Freq Mag",
    "Explosion Lens Splash Amount" };
inline constexpr const char* k_mine_wave_names[ 7 ] = {
    "Wave Falloff Time", "Wave Magnitude", "Wave Range Start", "Wave Range End", "Wave Speed", "Wave Frequency", "Wave Foaminess" };

inline void dynamic_prop_construct( dynamic_prop_t& p )
{
    p = dynamic_prop_t{ };
    p.fluids_drag = support_bits( 0x3EF0A3D7u );
    p.fluids_lift = support_bits( 0x3E4CCCCDu );
}

struct dynamic_prop_state_t
{
    dynamic_prop_t value;
    dynamic_prop_t defaults;
    bool buoy = false;
    bool mine = false;
    bool breakable = false;   // VuBreakableDynamicGamePropEntity
};

inline bool dynamic_prop_class( const std::string& cls )
{
    return cls == "VuDynamicGamePropEntity" || cls == "VuCheckpointBuoyEntity" || cls == "VuFloatingMineEntity" ||
           cls == "VuBreakableDynamicGamePropEntity";
}

inline std::shared_ptr< void > dynamic_prop_state_for( const std::string& cls )
{
    if ( !dynamic_prop_class( cls ) )
        return nullptr;
    auto s = std::make_shared< dynamic_prop_state_t >( );
    dynamic_prop_construct( s->value );
    dynamic_prop_construct( s->defaults );
    s->buoy = cls == "VuCheckpointBuoyEntity";
    s->mine = cls == "VuFloatingMineEntity";
    s->breakable = cls == "VuBreakableDynamicGamePropEntity";
    return s;
}

// VuDynamicGamePropEntity's input "Activate" 0x438160 (the ctor 0x4367E0's plug; thiscall entity, VuRetVal*, params; retn 8): the
// rigid component's body (+0x70 +0x100, null: nothing), unless it is static / kinematic (+0xD8 & 3): activation +0xE4 = 1 unless it
// is 4 / 5, then the deactivation time +0xE8 = 0. VuRetVal 0.
inline void dynamic_prop_activate( std::uint8_t* body )
{
    if ( !body )
        return;
    std::uint32_t flags, state;
    std::memcpy( &flags, body + 0xD8, 4 );
    std::memcpy( &state, body + 0xE4, 4 );
    if ( ( flags & 3u ) != 0u )
        return;
    if ( state != 4u && state != 5u )
    {
        const std::uint32_t one = 1u;
        std::memcpy( body + 0xE4, &one, 4 );
    }
    const std::uint32_t zero = 0u;
    std::memcpy( body + 0xE8, &zero, 4 );
}

// ---- VuExplosionEntity (create -> ctor 0x5E6150; vtable 0x755488; world batch 29) ---------------------------------
// The ctor: VuEntity's 0x500710, a VuTransformComponent (+0x50), a Vu3dLayoutComponent, a VuScriptComponent (+0x60), the
// explosion part at +0x64 (defaults 0x438BA0, nodes 0x5476D0 - the mine's part: nine VuBasicProperty<float> +0x74 ..
// +0x94, "Explosion Pfx System Name" +0x98, "Explosion Audio Event Name" +0xB0), the layout's draw 0x5E63F0 (the
// editor's) and the input plug "Trigger" (0x5E6410 -> 0x5E63D0: the part's 0x547BB0( transform +0x50 ), VuRetVal 0).
struct explosion_entity_t
{
    float explosion[ 9 ] = { 50.0f, 10.0f, 0.25f, 0.0f, 1.0f, 50.0f, 1.0f, 1.0f, 64.0f };
    std::string pfx;     // +0x98
    std::string audio;   // +0xB0
};

struct explosion_entity_state_t
{
    explosion_entity_t value;
    explosion_entity_t defaults;
};

inline std::shared_ptr< void > explosion_entity_state_for( const std::string& cls )
{
    return cls == "VuExplosionEntity" ? std::make_shared< explosion_entity_state_t >( ) : nullptr;
}

inline explosion_entity_state_t* explosion_entity_state_of( const entity_t& e )
{
    return e.m_type == "VuExplosionEntity" && e.m_state ? static_cast< explosion_entity_state_t* >( e.m_state.get( ) ) : nullptr;
}

inline void explosion_entity_load_properties( entity_t& e, const value_t& p )
{
    explosion_entity_state_t* s = explosion_entity_state_of( e );
    if ( !s )
        return;
    for ( int k = 0; k < 9; ++k )
        property_float( p, k_mine_explosion_names[ k ], s->value.explosion[ k ], s->defaults.explosion[ k ], false );
    property_string( p, "Explosion Pfx System Name", s->value.pfx, s->defaults.pfx );
    property_string( p, "Explosion Audio Event Name", s->value.audio, s->defaults.audio );
}

inline void explosion_entity_mark_properties( entity_t& e )
{
    if ( explosion_entity_state_t* s = explosion_entity_state_of( e ) )
        s->defaults = s->value;
}

// The loads in node order, each reset to its default first.
inline void dynamic_prop_load_properties( entity_t& e, const value_t& p )
{
    if ( !dynamic_prop_class( e.m_type ) || !e.m_state )
        return;
    dynamic_prop_state_t& s = *static_cast< dynamic_prop_state_t* >( e.m_state.get( ) );
    dynamic_prop_t& v = s.value;
    const dynamic_prop_t& d = s.defaults;
    property_bool( p, "Initially Visible", v.visible, d.visible );
    property_float( p, "Mass", v.mass, d.mass, false );
    property_vector3( p, "Center of Mass", v.centre, d.centre );
    property_float( p, "Fluids Radius", v.fluids_radius, d.fluids_radius, false );
    property_float( p, "Fluids Density", v.fluids_density, d.fluids_density, false );
    property_float( p, "Fluids Drag Coeff", v.fluids_drag, d.fluids_drag, false );
    property_float( p, "Fluids Lift Coeff", v.fluids_lift, d.fluids_lift, false );
    property_vector3( p, "Hydrodynamic Center", v.hydro_centre, d.hydro_centre );
    property_string( p, "Splash Pfx Name", v.splash_pfx, d.splash_pfx );
    property_float( p, "Splash Speed", v.splash_speed, d.splash_speed, false );
    property_float( p, "Linear Damping Coeff", v.linear_damping, d.linear_damping, false );
    property_float( p, "Angular Damping Coeff", v.angular_damping, d.angular_damping, false );
    property_bool( p, "Linear Anchor", v.linear_anchor, d.linear_anchor );
    property_bool( p, "Angular Anchor", v.angular_anchor, d.angular_anchor );
    property_float( p, "Anchor Spring Coeff", v.anchor_spring, d.anchor_spring, false );
    property_float( p, "Anchor Damping Coeff", v.anchor_damping, d.anchor_damping, false );
    property_bool( p, "Hard Hit", v.hard_hit, d.hard_hit );
    property_float( p, "Threshold Speed (MPH)", v.threshold_mph, d.threshold_mph, false );
    property_string( p, "Sfx Name", v.sfx, d.sfx );
    property_float( p, "Cam Shake Magnitude", v.shake_magnitude, d.shake_magnitude, false );
    property_float( p, "Cam Shake Duration", v.shake_duration, d.shake_duration, false );
    property_float( p, "Cam Shake Falloff Time", v.shake_falloff, d.shake_falloff, false );
    property_float( p, "Cam Shake Frequency", v.shake_frequency, d.shake_frequency, false );
    property_float( p, "Rumble Low Freq", v.rumble_low, d.rumble_low, false );
    property_float( p, "Rumble High Freq", v.rumble_high, d.rumble_high, false );
    property_bool( p, "Throw Ai Driver", v.throw_ai, d.throw_ai );
    property_bool( p, "Initially Active", v.initially_active, d.initially_active );
    property_bool( p, "Collide With Static Props", v.collide_static, d.collide_static );
    if ( s.buoy )
        property_int( p, "CheckPoint Index", v.checkpoint_index, d.checkpoint_index );
    if ( s.breakable )
        property_string( p, "Breakable Pfx Name", v.breakable_pfx, d.breakable_pfx );
    if ( s.mine )
    {
        for ( int k = 0; k < 9; ++k )
            property_float( p, k_mine_explosion_names[ k ], v.explosion[ k ], d.explosion[ k ], false );
        property_string( p, "Explosion Pfx System Name", v.explosion_pfx, d.explosion_pfx );
        property_string( p, "Explosion Audio Event Name", v.explosion_audio, d.explosion_audio );
        for ( int k = 0; k < 7; ++k )
            property_float( p, k_mine_wave_names[ k ], v.wave[ k ], d.wave[ k ], false );
    }
}

// The template mark's property step (each node's vt+0x1C): default = value.
inline void dynamic_prop_mark_properties( entity_t& e )
{
    if ( dynamic_prop_class( e.m_type ) && e.m_state )
    {
        dynamic_prop_state_t& s = *static_cast< dynamic_prop_state_t* >( e.m_state.get( ) );
        s.defaults = s.value;
    }
}

// 0x544050 (thiscall component; xmm1 the mass): +0xEC = maxss(mass, 0) (a NaN
// mass gives 0 - maxss answers the second operand), +0xD0 bit 0 = that == 0
// (ucomiss / lahf / 0x44: equal only), and with a body (+0x100 - none before
// gameInitialize) the inertia / setMassProps 0x50E040 - not reached here.
inline bool dynamic_prop_set_mass( rigid_component_t& c, float mass, bool has_body )
{
    const __m128 m = _mm_max_ss( _mm_set_ss( mass ), _mm_setzero_ps( ) );
    c.mass = _mm_cvtss_f32( m );
    if ( _mm_ucomieq_ss( m, _mm_setzero_ps( ) ) )
        c.flags_d0 |= 1u;
    else
        c.flags_d0 &= ~1u;
    return !has_body;   // a body's re-mass is not recreated
}

// VuDynamicPropEntity's postLoad 0x5459F0, its component step: 0x544050(Mass),
// the centre of mass (+0x7C, 16 bytes) into the component's +0xDC (its w lane
// UNKNOWN - not modelled), 0x545010 only with a body (none at load). (Its first
// call 0x545FF0 is the draw matrix - world/models.hxx's transform_draw_matrix
// path - and its +0x90 .. +0xAC copies of the transform's +0x40 / +0x90 words
// are read by nothing recreated.)
inline void dynamic_prop_post_load( const dynamic_prop_t& p, rigid_component_t& c )
{
    dynamic_prop_set_mass( c, p.mass, false );
    std::memcpy( c.centre, p.centre, 12 );
}

// The motion state's getWorldTransform 0x545C00 (this = entity +0x5C; retn 4):
// the transform component's world (+0x50 of the entity's +0x50) under
// 0x410AB0's translation by the component's centre of mass (+0xDC) -
// VuMatrix_multiply 0x4078C0(translation; out, world) - converted by 0x410C30
// to a btTransform (the basis transposed into rows, w 0, then the origin).
struct dynamic_prop_motion_t
{
    const float* world = nullptr;     // transform +0x50
    const float* centre = nullptr;    // component +0xDC
};

inline bool dynamic_prop_motion_get( void* user, std::uint32_t, float* out )
{
    const dynamic_prop_motion_t& m = *static_cast< const dynamic_prop_motion_t* >( user );
    float offset[ 16 ], product[ 16 ];
    vu_identity( offset );
    std::memcpy( offset + 12, m.centre, 12 );
    offset[ 15 ] = 1.0f;
    hull_multiply( offset, product, m.world );
    bodies_bt_from_vu( product, out );
    return true;
}

// What gameInitialize registers / hands out, for the caller to act on.
struct dynamic_prop_init_t
{
    bool decision_tick = false;   // g 0x759008 vt+0x20 (0x4077E8 -> vt+0x40 slot 16 0x438260), "Decision" (0x685E74)
    bool build_tick = false;      // the same with 0x4077DE (-> vt+0x3C slot 15 0x546130), "Build" (0x6CEC6C)
    bool contact_callback = false;   // body +0x2C4 = entity +0xB4 (vt 0x73A24C / 0x73A56C slot 0)
    bool step_callback = false;   // g_vudynamicsimpl vt+0x2C(entity +0xB0: slot 1 0x437730)
    bool drawn = false;           // g 0x7590B8 vt+0x28(draw component +0x6C)
    bool added = false;           // g_vudynamicsimpl vt+0x3C addRigidBody
    std::uint8_t shown = 0;       // entity +0x8C
};

// VuDynamicGamePropEntity gameInitialize 0x437580 on the entity's state, its
// rigid component and transform:
//  - Hard Hit (+0x108) or a Splash Pfx Name (+0xD8's size +0xE8 != 0): component
//    +0xF4 = entity +0xB4 (the contact callback; the body's +0x2C4 when one
//    exists - none yet), +0xD8 |= 1, the "Decision" tick.
//  - 0x545A80: 0x544050's mass step (inlined; no body), +0xF0 = entity +0x5C
//    (the motion state), group 2 (+0xD4) / mask 0xFFFF (+0xD6), createRigidBody,
//    Initially Visible -> the show vt+0x30 0x4381B0 (0x545EC0: +0x8C = 1, the
//    draw add unless the draw component's +0x1E, addRigidBody unless +0x104 or no
//    body; then Fluids Radius > 0 (comiss) or an anchor -> the step callback;
//    +0x144 = the world's translation and 0x531000 on +0x154 - read by the
//    Decision tick, not modelled here), then the "Build" tick.
//  - Initially Active (+0x141) 0 -> body +0xE4 (activation) = 2; Collide With
//    Static Props (+0x142) 0 -> mask &= 0xFFFE (+0xD6, the body's +0x2CC; the
//    broadphase proxy's +6 when the body has one - the caller's world).
//  - body +0x1D0 / +0x1D4 = Linear / Angular Damping Coeff clamped to [0, 1]
//    (0x6F76B8; comiss / cmovbe as body_clamp01).
// The body is dereferenced unconditionally from there (retail has one by then).
inline bool dynamic_prop_game_initialize( level_body_t& b, rigid_component_t& c, const dynamic_prop_t& p, const transform_component_t& t,
                                          const collision_mesh_asset_t* asset, std::uint32_t& unique_id, const bodies_hooks_t& hooks,
                                          dynamic_prop_motion_t& motion, dynamic_prop_init_t& out, std::string& why )
{
    out = dynamic_prop_init_t{ };
    if ( p.hard_hit || !p.splash_pfx.empty( ) )
    {
        c.word_f4 = 1;   // = entity +0xB4: a pointer word, the marker 1 here
        c.flags_d8 |= 1u;
        out.contact_callback = true;
        out.decision_tick = true;
    }
    dynamic_prop_set_mass( c, p.mass, false );
    c.group = 2;
    c.mask = 0xFFFF;
    motion.world = t.world;
    motion.centre = c.centre;
    bodies_motion_t m;
    m.get = dynamic_prop_motion_get;
    m.user = &motion;
    bool built = false;
    if ( !bodies_create( b, c, asset, t.world, t.scale, unique_id, hooks, why, built, &m ) )
        return false;
    if ( !built )
    {
        why = "gameInitialize 0x437580: no body built - retail dereferences +0x100 regardless (UNKNOWN)";
        return false;
    }
    if ( p.visible )
    {
        out.shown = 1;
        out.drawn = true;
        if ( !b.in_world )
        {
            out.added = true;
            b.in_world = true;
        }
        if ( _mm_comigt_ss( _mm_set_ss( p.fluids_radius ), _mm_setzero_ps( ) ) || p.linear_anchor || p.angular_anchor )
            out.step_callback = true;
    }
    out.build_tick = true;
    if ( !p.initially_active )
        body_word( b.body, 0xE4 ) = 2;
    if ( !p.collide_static )
    {
        c.mask = static_cast< std::uint16_t >( c.mask & 0xFFFEu );
        std::memcpy( reinterpret_cast< std::uint8_t* >( &b.body ) + 0x2CC, &c.mask, 2 );
    }
    const float ld = body_clamp01( p.linear_damping ), ad = body_clamp01( p.angular_damping );
    std::memcpy( &body_word( b.body, 0x1D0 ), &ld, 4 );
    std::memcpy( &body_word( b.body, 0x1D4 ), &ad, 4 );
    return true;
}

// ---- The step callback 0x437730 (vtable 0x73A254 slot 1, on the entity's +0xB0; dt unused; retn 4) ----
// Recreated from the disassembly with every scalar op in retail's operand order (the SSE NaN rule written out:
// boat/events.hxx sse_mul / sse_add / sse_sub, dp_div here), checked by the case dynamic_prop_step.
inline float dp_div( float a, float b ) { bool p; const float n = sse_nan_pick( a, b, p ); return p ? n : a / b; }
inline float dp_neg( float a ) { std::uint32_t u; std::memcpy( &u, &a, 4 ); u ^= 0x80000000u; std::memcpy( &a, &u, 4 ); return a; }   // xorps 0x6F7CD0
inline bool dp_gt( float a, float b ) { return _mm_comigt_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) != 0; }   // comiss a, b; ja
inline float dp_min( float a, float b ) { return _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); }
inline float dp_f( const std::uint8_t* p, int at ) { float f; std::memcpy( &f, p + at, 4 ); return f; }
inline void dp_put( std::uint8_t* p, int at, float f ) { std::memcpy( p + at, &f, 4 ); }

// g_vuwater 0x529140 (thiscall out*, point*): the caller's water (world/drive.hxx water_point_height). False: a
// path not recreated - the step is then UNKNOWN.
using dynamic_prop_water_fn = std::function< bool( const float* point, water_vertex_t& out ) >;

// btRigidBody::applyForce 0x4133B0 (ecx body, edx force*, the position on the stack; plain ret): rel = point -
// origin (+0x40); +0x1B0 += force x linear factor (+0x170); +0x1C0 += angular factor (+0x260) x (rel x (force x
// linear factor)).
inline void dp_apply_force( std::uint8_t* b, const float* f, const float* point )
{
    const float rx = sse_sub( point[ 0 ], dp_f( b, 0x40 ) ), ry = sse_sub( point[ 1 ], dp_f( b, 0x44 ) ), rz = sse_sub( point[ 2 ], dp_f( b, 0x48 ) );
    dp_put( b, 0x1B0, sse_add( sse_mul( f[ 0 ], dp_f( b, 0x170 ) ), dp_f( b, 0x1B0 ) ) );
    dp_put( b, 0x1B4, sse_add( dp_f( b, 0x1B4 ), sse_mul( dp_f( b, 0x174 ), f[ 1 ] ) ) );
    dp_put( b, 0x1B8, sse_add( dp_f( b, 0x1B8 ), sse_mul( dp_f( b, 0x178 ), f[ 2 ] ) ) );
    const float a = sse_mul( f[ 0 ], dp_f( b, 0x170 ) ), bb = sse_mul( dp_f( b, 0x174 ), f[ 1 ] ), c = sse_mul( dp_f( b, 0x178 ), f[ 2 ] );
    const float tx = sse_sub( sse_mul( c, ry ), sse_mul( bb, rz ) );
    const float ty = sse_sub( sse_mul( a, rz ), sse_mul( c, rx ) );
    const float tz = sse_sub( sse_mul( bb, rx ), sse_mul( a, ry ) );
    dp_put( b, 0x1C0, sse_add( sse_mul( tx, dp_f( b, 0x260 ) ), dp_f( b, 0x1C0 ) ) );
    dp_put( b, 0x1C4, sse_add( dp_f( b, 0x1C4 ), sse_mul( dp_f( b, 0x264 ), ty ) ) );
    dp_put( b, 0x1C8, sse_add( dp_f( b, 0x1C8 ), sse_mul( dp_f( b, 0x268 ), tz ) ) );
}

// The sphere buoyancy 0x5474A0 (thiscall on the 0x50-byte block the step builds: +0 body, +4 the point, +0x14 mass,
// +0x18 radius, +0x1C density, +0x20 drag, +0x24 lift): the water under the point; with height > z - r (comiss):
// frac = min((height - (z - r)) / (r x 2), 1), vol = ((r x 4/3pi) r) r (0x6F7758), k = (mass / vol) / density,
// buoyancy z = -(((frac vol) k) gravity z (body +0x198)); dv = linear velocity (+0x140) - the water's disp;
// area = ((r pi) r) frac (0x6F7750), half = k x 0.5; D = -(|dv| ((area half) drag)); force = (D dvx, D dvy,
// lift + ((dvz D) + buoyancy)) with lift = ((half (dvx^2 + dvy^2)) area) lift coeff; in water = 1. Else force 0.
// Out: the force, the water's disp (16 bytes, its w the query's) and height; the in-water byte.
struct dp_fluid_out_t
{
    float force[ 3 ] = { 0, 0, 0 };
    float disp[ 3 ] = { 0, 0, 0 };
    float height = 0.0f;
    bool in_water = false;
};

inline bool dp_sphere_fluid( const std::uint8_t* b, const float* point, float mass, float r, float density, float drag, float lift,
                             const dynamic_prop_water_fn& water, dp_fluid_out_t& out )
{
    out = dp_fluid_out_t{ };
    water_vertex_t v{ };
    if ( !water || !water( point, v ) )
        return false;
    std::memcpy( out.disp, v.disp, 12 );
    out.height = v.height;
    const float bottom = sse_sub( v.pos[ 2 ], r );
    if ( !dp_gt( v.height, bottom ) )
        return true;
    const float depth = sse_sub( v.height, bottom );
    const float vol = sse_mul( sse_mul( sse_mul( r, support_bits( 0x40860A92u ) ), r ), r );
    const float frac = dp_min( dp_div( depth, sse_mul( r, 2.0f ) ), 1.0f );
    const float k = dp_div( dp_div( mass, vol ), density );
    const float dvy = sse_sub( dp_f( b, 0x144 ), out.disp[ 1 ] );
    const float dvx = sse_sub( dp_f( b, 0x140 ), out.disp[ 0 ] );
    const float dvz = sse_sub( dp_f( b, 0x148 ), out.disp[ 2 ] );
    const float buoy = dp_neg( sse_mul( sse_mul( sse_mul( frac, vol ), k ), dp_f( b, 0x198 ) ) );
    const float half = sse_mul( k, 0.5f );
    const float h2 = sse_add( sse_mul( dvx, dvx ), sse_mul( dvy, dvy ) );
    const float area = sse_mul( sse_mul( sse_mul( r, support_bits( 0x40490FDBu ) ), r ), frac );
    const float speed = hull_sqrt( sse_add( sse_mul( dvz, dvz ), h2 ) );
    const float lift_term = sse_mul( sse_mul( sse_mul( half, h2 ), area ), lift );
    const float d = dp_neg( sse_mul( speed, sse_mul( sse_mul( area, half ), drag ) ) );
    out.force[ 0 ] = sse_mul( d, dvx );
    out.force[ 1 ] = sse_mul( d, dvy );
    out.force[ 2 ] = sse_add( lift_term, sse_add( sse_mul( dvz, d ), buoy ) );
    out.in_water = true;
    return true;
}

// 0x437730 itself, on the entity's state (p), its in-water flags (+0x180 / +0x181), the anchor position (+0x144)
// and quaternion (+0x154) the show stored, the component's centre and the body's bytes. M = 0x410B40(the body's
// basis / origin): the entity's origin p = M(-centre); with Fluids Radius > 0 (comiss) the buoyancy at p and its
// force applied (0x4133B0) at M(Hydrodynamic Center - centre); entering the water (+0x180 0 -> 1) faster than Splash
// Speed x 0.44704 (0x6F7654; |linear velocity - disp|, VuMath_sqrt) sets +0x181; +0x180 = in water. Linear Anchor:
// +0x1B0 += linear factor x (spring m (anchor - p) - damping m v) in x / y (z: (spring m) 0 - (damping m) 0).
// Angular Anchor: the rotation from M (0x531000) against the anchor's (0x41B4E0), its axis-angle (0x531330), the
// torque (spring (M3 axis (-angle))) + (-angular velocity) damping) through the INVERSE of the world inverse
// inertia (+0x110, cofactors / determinant), x angular factor, into +0x1C0.
struct dynamic_prop_live_t
{
    std::uint8_t in_water = 0;      // +0x180
    std::uint8_t splash = 0;        // +0x181
    float anchor[ 3 ] = { 0, 0, 0 };    // +0x144 (the show: the world's translation)
    float anchor_q[ 4 ] = { 0, 0, 0, 1 };   // +0x154 (0x531000 of the world)
};

inline bool dynamic_prop_step( const dynamic_prop_t& p, dynamic_prop_live_t& live, const float* centre, std::uint8_t* b,
                               const dynamic_prop_water_fn& water )
{
    float m[ 16 ];
    for ( int row = 0; row < 3; ++row )
    {
        for ( int k = 0; k < 3; ++k )
            m[ 4 * row + k ] = dp_f( b, 0x10 + 16 * k + 4 * row );
        m[ 4 * row + 3 ] = 0.0f;
    }
    m[ 12 ] = dp_f( b, 0x40 );
    m[ 13 ] = dp_f( b, 0x44 );
    m[ 14 ] = dp_f( b, 0x48 );
    m[ 15 ] = 1.0f;
    const float ncx = dp_neg( centre[ 0 ] ), ncy = dp_neg( centre[ 1 ] ), ncz = dp_neg( centre[ 2 ] );
    float pos[ 4 ];
    pos[ 0 ] = sse_add( sse_add( sse_add( sse_mul( m[ 4 ], ncy ), sse_mul( m[ 0 ], ncx ) ), sse_mul( m[ 8 ], ncz ) ), m[ 12 ] );
    pos[ 1 ] = sse_add( sse_add( sse_add( sse_mul( m[ 5 ], ncy ), sse_mul( m[ 1 ], ncx ) ), sse_mul( m[ 9 ], ncz ) ), m[ 13 ] );
    pos[ 2 ] = sse_add( sse_add( sse_add( sse_mul( m[ 6 ], ncy ), sse_mul( m[ 2 ], ncx ) ), sse_mul( m[ 10 ], ncz ) ), m[ 14 ] );
    pos[ 3 ] = 0.0f;   // the stack word 0x9C is never written - the water query copies it to a word nothing reads
    if ( dp_gt( p.fluids_radius, 0.0f ) )
    {
        dp_fluid_out_t f;
        if ( !dp_sphere_fluid( b, pos, p.mass, p.fluids_radius, p.fluids_density, p.fluids_drag, p.fluids_lift, water, f ) )
            return false;
        const float hx = sse_sub( p.hydro_centre[ 0 ], centre[ 0 ] ), hy = sse_sub( p.hydro_centre[ 1 ], centre[ 1 ] ),
                    hz = sse_sub( p.hydro_centre[ 2 ], centre[ 2 ] );
        float at[ 3 ];
        at[ 0 ] = sse_add( sse_add( sse_add( sse_mul( m[ 0 ], hx ), sse_mul( m[ 4 ], hy ) ), sse_mul( m[ 8 ], hz ) ), m[ 12 ] );
        at[ 1 ] = sse_add( sse_add( sse_add( sse_mul( m[ 1 ], hx ), sse_mul( m[ 5 ], hy ) ), sse_mul( m[ 9 ], hz ) ), m[ 13 ] );
        at[ 2 ] = sse_add( sse_add( sse_add( sse_mul( m[ 2 ], hx ), sse_mul( m[ 6 ], hy ) ), sse_mul( m[ 10 ], hz ) ), m[ 14 ] );
        dp_apply_force( b, f.force, at );
        if ( f.in_water && !live.in_water )
        {
            const float dy = sse_sub( dp_f( b, 0x144 ), f.disp[ 1 ] ), dx = sse_sub( dp_f( b, 0x140 ), f.disp[ 0 ] ),
                        dz = sse_sub( dp_f( b, 0x148 ), f.disp[ 2 ] );
            const float speed = hull_sqrt( sse_add( sse_add( sse_mul( dy, dy ), sse_mul( dx, dx ) ), sse_mul( dz, dz ) ) );
            if ( dp_gt( speed, sse_mul( p.splash_speed, support_bits( 0x3EE4E26Du ) ) ) )
                live.splash = 1;
        }
        live.in_water = f.in_water ? 1 : 0;
    }
    if ( p.linear_anchor )
    {
        const float c = sse_mul( p.anchor_damping, p.mass );
        const float k = sse_mul( p.anchor_spring, p.mass );
        const float dx = sse_sub( live.anchor[ 0 ], pos[ 0 ] ), dy = sse_sub( live.anchor[ 1 ], pos[ 1 ] );
        const float fx = sse_sub( sse_mul( k, dx ), sse_mul( dp_f( b, 0x140 ), c ) );
        const float fy = sse_sub( sse_mul( k, dy ), sse_mul( dp_f( b, 0x144 ), c ) );
        const float fz = sse_sub( sse_mul( k, 0.0f ), sse_mul( c, 0.0f ) );
        dp_put( b, 0x1B0, sse_add( sse_mul( fx, dp_f( b, 0x170 ) ), dp_f( b, 0x1B0 ) ) );
        dp_put( b, 0x1B4, sse_add( sse_mul( dp_f( b, 0x174 ), fy ), dp_f( b, 0x1B4 ) ) );
        dp_put( b, 0x1B8, sse_add( sse_mul( dp_f( b, 0x178 ), fz ), dp_f( b, 0x1B8 ) ) );
    }
    if ( p.angular_anchor )
    {
        float q[ 4 ], qd[ 4 ], axis[ 4 ], angle[ 1 ];
        quat_from_rows( q, m );
        quat_inverse_mul( qd, q, live.anchor_q );
        quat_to_axis_angle( qd, axis, angle );
        const float na = dp_neg( angle[ 0 ] );
        const float ay = sse_mul( axis[ 1 ], na ), az = sse_mul( axis[ 2 ], na ), ax = sse_mul( axis[ 0 ], na );
        const float wx = sse_add( sse_add( sse_mul( m[ 4 ], ay ), sse_mul( m[ 0 ], ax ) ), sse_mul( m[ 8 ], az ) );
        const float wy = sse_add( sse_add( sse_mul( m[ 5 ], ay ), sse_mul( m[ 1 ], ax ) ), sse_mul( m[ 9 ], az ) );
        const float wz = sse_add( sse_add( sse_mul( m[ 6 ], ay ), sse_mul( m[ 2 ], ax ) ), sse_mul( m[ 10 ], az ) );
        const float k = p.anchor_spring, c = p.anchor_damping;
        const float tx = sse_add( sse_mul( dp_neg( dp_f( b, 0x150 ) ), c ), sse_mul( k, wx ) );
        const float ty = sse_add( sse_mul( dp_neg( dp_f( b, 0x154 ) ), c ), sse_mul( wy, k ) );
        const float tz = sse_add( sse_mul( dp_neg( dp_f( b, 0x158 ) ), c ), sse_mul( wz, k ) );
        const float m00 = dp_f( b, 0x110 ), m01 = dp_f( b, 0x114 ), m02 = dp_f( b, 0x118 );
        const float m10 = dp_f( b, 0x120 ), m11 = dp_f( b, 0x124 ), m12 = dp_f( b, 0x128 );
        const float m20 = dp_f( b, 0x130 ), m21 = dp_f( b, 0x134 ), m22 = dp_f( b, 0x138 );
        const float c00 = sse_sub( sse_mul( m11, m22 ), sse_mul( m12, m21 ) );
        const float c10 = sse_sub( sse_mul( m20, m12 ), sse_mul( m10, m22 ) );
        const float c20 = sse_sub( sse_mul( m10, m21 ), sse_mul( m20, m11 ) );
        const float det = sse_add( sse_add( sse_mul( c00, m00 ), sse_mul( c10, m01 ) ), sse_mul( c20, m02 ) );
        const float inv = dp_div( 1.0f, det );
        const float i22 = sse_mul( sse_sub( sse_mul( m00, m11 ), sse_mul( m01, m10 ) ), inv );
        const float i20 = sse_mul( c20, inv );
        const float i21 = sse_mul( sse_sub( sse_mul( m01, m20 ), sse_mul( m00, m21 ) ), inv );
        const float i12 = sse_mul( sse_sub( sse_mul( m02, m10 ), sse_mul( m00, m12 ) ), inv );
        const float i11 = sse_mul( sse_sub( sse_mul( m00, m22 ), sse_mul( m02, m20 ) ), inv );
        const float i10 = sse_mul( c10, inv );
        const float i01 = sse_mul( sse_sub( sse_mul( m02, m21 ), sse_mul( m01, m22 ) ), inv );
        const float i02 = sse_mul( sse_sub( sse_mul( m01, m12 ), sse_mul( m02, m11 ) ), inv );
        const float i00 = sse_mul( c00, inv );
        const float r0 = sse_add( sse_add( sse_mul( tx, i00 ), sse_mul( ty, i10 ) ), sse_mul( tz, i20 ) );
        const float r1 = sse_add( sse_add( sse_mul( tx, i01 ), sse_mul( ty, i11 ) ), sse_mul( tz, i21 ) );
        const float r2 = sse_add( sse_add( sse_mul( tx, i02 ), sse_mul( ty, i12 ) ), sse_mul( tz, i22 ) );
        dp_put( b, 0x1C0, sse_add( sse_mul( dp_f( b, 0x260 ), r0 ), dp_f( b, 0x1C0 ) ) );
        dp_put( b, 0x1C4, sse_add( sse_mul( dp_f( b, 0x264 ), r1 ), dp_f( b, 0x1C4 ) ) );
        dp_put( b, 0x1C8, sse_add( sse_mul( dp_f( b, 0x268 ), r2 ), dp_f( b, 0x1C8 ) ) );
    }
    return true;
}
// VuDynamicGamePropEntity's contact callback 0x437F40 (the body's +0x2C4 VuRigidBodyContactCallback at entity
// +0xB4; thiscall, the contact record*; retn 4 - read from the disassembly): nothing when the other body's flags
// +0xD8 & 4, when the hit timer +0x17C is not >= 1.0 (comiss 0x6F76B8; jb: a NaN returns) or when the other's
// +0x2CA byte & 0x10 (a boat) is clear. Else the relative normal speed at the contact point: v = linear (+0x140)
// + angular (+0x150) x (point - origin +0x40) for this prop's body (the rigid body component +0x70's +0x100) minus
// the other's, dot = (n.y dv.y + dv.x n.x) + n.z dv.z - each product / difference in the disassembly's operand
// order. When Threshold (+0x10C) x -0.44704 (0x6F7890) > dot (comiss; jbe: ordered): 0x433B70 points the weak
// ref +0x168 at the other's +0x2C0 owner (released first; a null owner leaves it empty), +0x164 = 1, +0x17C = 0,
// +0x178 = maxss(+0x178, 0x4AD340(this body, the other, point, normal)) - boat/events.hxx body_contact_strength.
struct dynamic_prop_hit_t
{
    std::uint8_t pending = 0;               // +0x164
    const std::uint8_t* target = nullptr;   // +0x174 (the weak ref's object): the other BODY whose +0x2C0 owner it holds
    float strength = 0.0f;                  // +0x178
};

// `owner`: the other body's +0x2C0 is non-null - the caller's (a body's owner is resolved by the session, the image's
// +0x2C0 word is not a pointer here).
inline bool dynamic_prop_contact( const dynamic_prop_t& p, dynamic_prop_hit_t& hit, float& timer, const std::uint8_t* e, const std::uint8_t* o,
                                  const float* point, const float* n, bool owner )
{
    std::uint32_t of;
    std::memcpy( &of, o + 0xD8, 4 );
    if ( of & 4u )
        return false;
    if ( !_mm_comige_ss( _mm_set_ss( timer ), _mm_set_ss( 1.0f ) ) )
        return false;
    if ( ( o[ 0x2CA ] & 0x10u ) == 0 )
        return false;
    const float o_ry = sse_sub( point[ 1 ], dp_f( o, 0x44 ) );
    const float o_rx = sse_sub( point[ 0 ], dp_f( o, 0x40 ) );
    const float o_rz = sse_sub( point[ 2 ], dp_f( o, 0x48 ) );
    const float vox = sse_add( sse_sub( sse_mul( o_rz, dp_f( o, 0x154 ) ), sse_mul( o_ry, dp_f( o, 0x158 ) ) ), dp_f( o, 0x140 ) );
    const float voz = sse_add( sse_sub( sse_mul( o_ry, dp_f( o, 0x150 ) ), sse_mul( o_rx, dp_f( o, 0x154 ) ) ), dp_f( o, 0x148 ) );
    const float voy = sse_add( sse_sub( sse_mul( o_rx, dp_f( o, 0x158 ) ), sse_mul( o_rz, dp_f( o, 0x150 ) ) ), dp_f( o, 0x144 ) );
    const float e_rz = sse_sub( point[ 2 ], dp_f( e, 0x48 ) );
    const float e_rx = sse_sub( point[ 0 ], dp_f( e, 0x40 ) );
    const float e_ry = sse_sub( point[ 1 ], dp_f( e, 0x44 ) );
    const float vex = sse_add( sse_sub( sse_mul( e_rz, dp_f( e, 0x154 ) ), sse_mul( e_ry, dp_f( e, 0x158 ) ) ), dp_f( e, 0x140 ) );
    const float vey = sse_add( sse_sub( sse_mul( e_rx, dp_f( e, 0x158 ) ), sse_mul( e_rz, dp_f( e, 0x150 ) ) ), dp_f( e, 0x144 ) );
    const float vez = sse_add( sse_sub( sse_mul( e_ry, dp_f( e, 0x150 ) ), sse_mul( e_rx, dp_f( e, 0x154 ) ) ), dp_f( e, 0x148 ) );
    const float dvx = sse_sub( vex, vox ), dvy = sse_sub( vey, voy ), dvz = sse_sub( vez, voz );
    const float dot = sse_add( sse_add( sse_mul( n[ 1 ], dvy ), sse_mul( dvx, n[ 0 ] ) ), sse_mul( n[ 2 ], dvz ) );
    const float limit = sse_mul( p.threshold_mph, support_bits( 0xBEE4E26Du ) );
    if ( !dp_gt( limit, dot ) )
        return false;
    hit.target = owner ? o : nullptr;
    hit.pending = 1;
    timer = 0.0f;
    hit.strength = _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( hit.strength ), _mm_set_ss( body_contact_strength( e, o, point, n ) ) ) );
    return true;
}