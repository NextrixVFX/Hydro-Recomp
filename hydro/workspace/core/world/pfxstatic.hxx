#pragma once

// VuStaticPfxEntity's behaviour (vtable 0x755E80; create 0x5EA940, ctor
// 0x5EAA40) - the front end's waterfall and cloud effects and every level's
// placed effect. Its properties are engine entity/pfx.hxx's static_pfx_t
// (+0x70 Effect Name, +0x88 Initially Active, +0x8C Pfx Scale, +0x90 Pfx
// Color). What it keeps besides: +0x94 its VuPfxSystemInstance (0 = none),
// and its Vu3dDrawComponent (+0x60) whose +0x1E byte says "registered with
// the 3D draw manager" and +0x24 .. +0x43 the AABB it last copied (8 words,
// the instance's slot 7 copied as four movq - the two pad words included).
//
// Everything it calls goes through Vu interfaces - here pfx_static_ops_t:
// g_vupfximpl (0x75904C) slot 10 createSystemInstance(name) 0x51DA70 / slot
// 11 release 0x51DAE0; the instance's slots 2 setMatrix 0x58AB00, 4 setScale
// 0x58A670, 5 setColor 0x58A690, 6 state 0x58A6C0, 7 the AABB 0x58A6D0, 8 the
// count 0x58A6E0, 11 start 0x58AC90, 12 stop(immediate) 0x58ACC0, 13 tick
// 0x58AD70, 14 / 15 the draws 0x58AF60 / 0x58AFA0; the tick manager g
// 0x759008 +0x20 register (the "Build" phase) / +0x28 unregister; the 3D draw
// manager g 0x7590B8 +0x28 add / +0x2C remove / +0x30 update. Those are the
// verified world/pfxsim.hxx / pfxload.hxx functions (pfx_static_runtime_ops
// binds them) and platform hooks (the draw manager, the tick manager).
//
// The motion handler 0x5EB4C0 (the VuMotionComponentIF at +0x5C) is world/
// watchers.hxx's pfx_static_motion (it needs world/race.hxx's entity_set_world).

#include <workspace/core/entity/pfx.hxx>
#include <workspace/core/world/pfxmgr.hxx>

#include <cstdint>
#include <cstring>
#include <string>

struct pfx_static_ops_t
{
    void* user = nullptr;
    std::uint32_t ( *create )( void* user, const std::string& name ) = nullptr;   // g_vupfximpl slot 10
    void ( *release )( void* user, std::uint32_t instance ) = nullptr;            // slot 11
    void ( *set_matrix )( void* user, std::uint32_t instance, const float* m16 ) = nullptr;   // instance slot 2
    void ( *set_scale )( void* user, std::uint32_t instance, float scale ) = nullptr;         // 4
    void ( *set_color )( void* user, std::uint32_t instance, const float* rgba ) = nullptr;   // 5
    std::uint32_t ( *state )( void* user, std::uint32_t instance ) = nullptr;                 // 6
    const std::uint32_t* ( *aabb )( void* user, std::uint32_t instance ) = nullptr;          // 7 (8 words)
    std::uint32_t ( *count )( void* user, std::uint32_t instance ) = nullptr;                 // 8
    void ( *start )( void* user, std::uint32_t instance ) = nullptr;                          // 11
    void ( *stop )( void* user, std::uint32_t instance, bool immediate ) = nullptr;           // 12
    void ( *tick )( void* user, std::uint32_t instance, float dt ) = nullptr;                 // 13
    void ( *draw )( void* user, std::uint32_t instance, const void* params ) = nullptr;       // 14
    void ( *draw_shadow )( void* user, std::uint32_t instance, const void* params ) = nullptr;   // 15
    void ( *tick_register )( void* user, void* entity, const char* phase ) = nullptr;      // g 0x759008 +0x20
    void ( *tick_unregister )( void* user, void* entity ) = nullptr;                       // +0x28
    void ( *draw_add )( void* user, void* entity ) = nullptr;      // g 0x7590B8 +0x28 (the entity's component)
    void ( *draw_remove )( void* user, void* entity ) = nullptr;   // +0x2C
    void ( *draw_update )( void* user, void* entity ) = nullptr;   // +0x30
};

struct pfx_static_entity_t
{
    const static_pfx_t* properties = nullptr;   // the loaded +0x70 .. +0x93
    const float* matrix = nullptr;              // the transform's +0x50 (16 floats)
    std::uint32_t instance = 0;                 // +0x94
    std::uint8_t drawn = 0;                     // the draw component's +0x1E
    std::uint32_t aabb[ 8 ]{};                  // the draw component's +0x24 .. +0x43
};

// The scale, then the colour (static_pfx_color: bytes x 1/255), as slot 6
// hands them; the watcher hands the matrix first.
inline void pfx_static_apply( pfx_static_entity_t& e, const pfx_static_ops_t& o, bool matrix_first )
{
    float rgba[ 4 ];
    if ( matrix_first )
        o.set_matrix( o.user, e.instance, e.matrix );
    o.set_scale( o.user, e.instance, e.properties->pfx_scale );
    static_pfx_color( *e.properties, rgba );
    o.set_color( o.user, e.instance, rgba );
    if ( !matrix_first )
        o.set_matrix( o.user, e.instance, e.matrix );
}

// Slot 6 0x5EB060 (onGameInitialize): the tick 0x5EB290 registered in "Build",
// the instance created from Effect Name, then scale, colour, matrix; started
// when Initially Active and created.
inline void pfx_static_game_initialize( pfx_static_entity_t& e, const pfx_static_ops_t& o )
{
    o.tick_register( o.user, &e, "Build" );
    e.instance = o.create( o.user, e.properties->effect_name );
    if ( e.instance != 0 )
        pfx_static_apply( e, o, false );
    if ( e.properties->initially_active != 0 && e.instance != 0 )
        o.start( o.user, e.instance );
}

// Slot 7 0x5EB1A0 (onGameRelease): stop(immediate), the draw registration
// dropped (whether or not there is an instance), the instance released, the
// entity's ticks unregistered.
inline void pfx_static_game_release( pfx_static_entity_t& e, const pfx_static_ops_t& o )
{
    if ( e.instance != 0 )
        o.stop( o.user, e.instance, true );
    if ( e.drawn != 0 )
    {
        o.draw_remove( o.user, &e );
        e.drawn = 0;
    }
    if ( e.instance != 0 )
    {
        o.release( o.user, e.instance );
        e.instance = 0;
    }
    o.tick_unregister( o.user, &e );
}

// The "Build" tick 0x5EB290 (dt): the instance's tick; with particles the
// component is registered (once), the AABB copied, then updated; without, it
// is unregistered.
inline void pfx_static_tick( pfx_static_entity_t& e, const pfx_static_ops_t& o, float dt )
{
    if ( e.instance == 0 )
        return;
    o.tick( o.user, e.instance, dt );
    if ( o.count( o.user, e.instance ) != 0 )
    {
        if ( e.drawn == 0 )
        {
            o.draw_add( o.user, &e );
            e.drawn = 1;
        }
        std::memcpy( e.aabb, o.aabb( o.user, e.instance ), sizeof( e.aabb ) );
        if ( e.drawn != 0 )   // always, here (retail tests it again)
            o.draw_update( o.user, &e );
        return;
    }
    if ( e.drawn != 0 )
    {
        o.draw_remove( o.user, &e );
        e.drawn = 0;
    }
}

// The property watcher 0x5EB390 (Effect Name / Pfx Scale / Pfx Color; a load
// never fires it): only with an instance - remember whether it was running
// (state == 1), release it, create the effect again, matrix / scale / colour,
// start it again if it was. The old word is overwritten by the create's answer
// (no store of 0 between).
inline void pfx_static_changed( pfx_static_entity_t& e, const pfx_static_ops_t& o )
{
    if ( e.instance == 0 )
        return;
    const bool running = o.state( o.user, e.instance ) == 1u;
    o.release( o.user, e.instance );
    e.instance = o.create( o.user, e.properties->effect_name );
    if ( e.instance == 0 )
        return;
    pfx_static_apply( e, o, true );
    if ( running )
        o.start( o.user, e.instance );
}

// The plugs (VuRetVal handlers; the answer is the empty VuRetVal, type 0):
// Start 0x5EB200, Stop 0x5EB220 (stop, not immediate), Kill 0x5EB250 (stop
// immediately, then the draw registration dropped with or without an instance).
inline void pfx_static_start( pfx_static_entity_t& e, const pfx_static_ops_t& o )
{
    if ( e.instance != 0 )
        o.start( o.user, e.instance );
}

inline void pfx_static_stop( pfx_static_entity_t& e, const pfx_static_ops_t& o )
{
    if ( e.instance != 0 )
        o.stop( o.user, e.instance, false );
}

inline void pfx_static_kill( pfx_static_entity_t& e, const pfx_static_ops_t& o )
{
    if ( e.instance != 0 )
        o.stop( o.user, e.instance, true );
    if ( e.drawn != 0 )
    {
        o.draw_remove( o.user, &e );
        e.drawn = 0;
    }
}

// The transform watcher 0x5EB4A0: the matrix again.
inline void pfx_static_moved( pfx_static_entity_t& e, const pfx_static_ops_t& o )
{
    if ( e.instance != 0 )
        o.set_matrix( o.user, e.instance, e.matrix );
}

// The draw component's draw 0x5EB350 / shadow 0x5EB370: the instance's slot
// 14 / 15 with the params.
inline void pfx_static_draw( const pfx_static_entity_t& e, const pfx_static_ops_t& o, const void* params )
{
    if ( e.instance != 0 )
        o.draw( o.user, e.instance, params );
}

inline void pfx_static_draw_shadow( const pfx_static_entity_t& e, const pfx_static_ops_t& o, const void* params )
{
    if ( e.instance != 0 )
        o.draw_shadow( o.user, e.instance, params );
}

// The binding onto the recreated VuPfx runtime (pfxsim.hxx / pfxload.hxx; the
// manager's binding struct: rt + the loaded library): create = 0x51DA70 by
// path, release 0x51DAE0, the instance slots on its heap words. The tick and
// draw managers and the draws (slots 14 / 15: the renderer) stay the caller's
// - set them on the returned ops. The tick runs slot 13 with rt.hooks; it does
// not count rt.frame.unrecreated (pfx_runtime_frame does, for a caller that
// wants it - set o.tick to it).
inline pfx_static_ops_t pfx_static_runtime_ops( pfx_manager_binding_t& b )
{
    pfx_static_ops_t o;
    o.user = &b;
    o.create = []( void* u, const std::string& name ) {
        pfx_manager_binding_t& x = *static_cast< pfx_manager_binding_t* >( u );
        return pfx_runtime_create_path( *x.rt, *x.lib, name );
    };
    o.release = []( void* u, std::uint32_t inst ) {
        pfx_runtime_t& rt = *static_cast< pfx_manager_binding_t* >( u )->rt;
        pfx_system_release( rt.heap, rt.resources, inst );
    };
    o.set_matrix = []( void* u, std::uint32_t inst, const float* m ) {
        std::uint32_t w[ 16 ];
        std::memcpy( w, m, sizeof( w ) );
        pfx_system_set_matrix( static_cast< pfx_manager_binding_t* >( u )->rt->heap.find( inst ), w );
    };
    o.set_scale = []( void* u, std::uint32_t inst, float s ) {
        std::uint32_t w;
        std::memcpy( &w, &s, 4 );
        pfx_system_set_scale( static_cast< pfx_manager_binding_t* >( u )->rt->heap.find( inst ), w );
    };
    o.set_color = []( void* u, std::uint32_t inst, const float* c ) {
        std::uint32_t w[ 4 ];
        std::memcpy( w, c, sizeof( w ) );
        pfx_system_set_wordsac( static_cast< pfx_manager_binding_t* >( u )->rt->heap.find( inst ), w );
    };
    o.state = []( void* u, std::uint32_t inst ) { return pfx_system_state( static_cast< pfx_manager_binding_t* >( u )->rt->heap.find( inst ) ); };
    o.aabb = []( void* u, std::uint32_t inst ) -> const std::uint32_t* {
        return static_cast< pfx_manager_binding_t* >( u )->rt->heap.find( inst + pfx_system_aabb_offset( ) );
    };
    o.count = []( void* u, std::uint32_t inst ) { return pfx_system_count( static_cast< pfx_manager_binding_t* >( u )->rt->heap.find( inst ) ); };
    o.start = []( void* u, std::uint32_t inst ) { pfx_system_start( static_cast< pfx_manager_binding_t* >( u )->rt->heap, inst ); };
    o.stop = []( void* u, std::uint32_t inst, bool immediate ) {
        pfx_runtime_t& rt = *static_cast< pfx_manager_binding_t* >( u )->rt;
        pfx_system_stop( rt.heap, rt.resources, inst, immediate );
    };
    o.tick = []( void* u, std::uint32_t inst, float dt ) {
        pfx_runtime_t& rt = *static_cast< pfx_manager_binding_t* >( u )->rt;
        pfx_system_tick( rt.heap, inst, dt, rt.hooks );
    };
    return o;
}

// VuOneShotPfxEntity (vtables 0x755C88 / 0x755C98, create 0x5EA030, ctor 0x5EA0B0; boat batch 34, case oneshot_pfx): +0x68 Effect Name, +0x80 Pfx Scale, +0x84 Pfx Color (bytes),
// +0x88 Enable Reflection, +0x8C the handle. The motion watcher 0x5EA740 is pfx_oneshot_watch (case oneshot_watch). Was NOT recreated.
struct pfx_oneshot_t
{
    std::string effect_name;
    float pfx_scale = 1.0f;
    std::uint8_t pfx_color[ 4 ] = { 255u, 255u, 255u, 255u };
    std::uint8_t enable_reflection = 0u;
    const float* matrix = nullptr;      // the transform's +0x50
    std::uint32_t handle = 0u;
};

struct pfx_oneshot_ops_t
{
    pfx_static_ops_t instance;
    int ( *create )( void* user, const char* name ) = nullptr;                    // 0x52BCD0: the entry, -1 none
    void ( *duration )( void* user, std::uint32_t instance ) = nullptr;           // slot 10, its answer discarded
};

// The motion watcher 0x5EA740: the transform takes the motion's matrix (+0x50, its euler, 0x534830, 0x534C00( 0 ): `set_world`, the caller's
// transform_set_world), then a live instance's slot 2 the same matrix.
template < typename set_world_fn >
inline void pfx_oneshot_watch( const pfx_oneshot_t& e, const pfx_manager_t& m, const pfx_oneshot_ops_t& o, const float* matrix, set_world_fn set_world )
{
    set_world( matrix );
    const int x = pfx_manager_lookup( m, e.handle );
    if ( x >= 0 )
        o.instance.set_matrix( o.instance.user, m.entries[ static_cast< std::size_t >( x ) ].instance, matrix );
}

// "Start" 0x5EA540: a fresh effect (a handle, 0 with none), its entry's reflect byte, slots 4 5 2 11.
inline void pfx_oneshot_start( pfx_oneshot_t& e, pfx_manager_t& m, const pfx_oneshot_ops_t& o )
{
    const pfx_static_ops_t& i = o.instance;
    const int created = o.create( i.user, e.effect_name.c_str( ) );
    const std::uint32_t h = pfx_manager_handle( m, created );
    if ( created >= 0 && o.duration )
        o.duration( i.user, m.entries[ static_cast< std::size_t >( created ) ].instance );
    e.handle = h;
    if ( h == 0u )
        return;
    const int x = pfx_manager_lookup( m, h );
    if ( x < 0 )
        return;
    pfx_manager_entry_t& entry = m.entries[ static_cast< std::size_t >( x ) ];
    entry.reflect = e.enable_reflection;
    i.set_scale( i.user, entry.instance, e.pfx_scale );
    const std::uint32_t scale_bits = 0x3B808081u;   // 0x6F75BC, 1 / 255
    float byte_scale;
    std::memcpy( &byte_scale, &scale_bits, 4 );
    float rgba[ 4 ];
    for ( int k = 0; k < 4; ++k )
        rgba[ k ] = static_cast< float >( e.pfx_color[ k ] ) * byte_scale;
    i.set_color( i.user, entry.instance, rgba );
    i.set_matrix( i.user, entry.instance, e.matrix );
    i.start( i.user, entry.instance );
}

// "Stop" 0x5EA6A0 (not immediate), "Kill" 0x5EA6F0 (immediate): slot 12 on the live handle.
inline void pfx_oneshot_stop( const pfx_oneshot_t& e, const pfx_manager_t& m, const pfx_oneshot_ops_t& o, bool immediate )
{
    const int x = pfx_manager_lookup( m, e.handle );
    if ( x >= 0 )
        o.instance.stop( o.instance.user, m.entries[ static_cast< std::size_t >( x ) ].instance, immediate );
}
