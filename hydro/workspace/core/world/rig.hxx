#pragma once

#include <workspace/core/boat/anim.hxx>
#include <workspace/core/world/skeleton.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// A boat's animated model, CPU side (world batch 33): what the pfx records' bones read.
//   boat +0x2A4          the first VuAnimatedModelInstance. 0x420A70's 0x5322C0 loads "Model Asset" (+0xC4) into it, BEFORE
//                        the Pfx section reader, whose 0x41A1A0 asks its skeleton (+0x10) findNodeByName 0x52F3C0.
//   anim (+0x230) +4     the VuAnimatedSkeleton, made by 0x410610 (gameInitialize 0x41D350 -> 0x41D442; 0x420A70 too when the
//                        owner's bit 2 says it is already initialized, which a race's first load is not).
//   anim +8 / +C / +10   the three VuAnimationControls: ThrustAsset (+0x14), TurnAsset (+0x2C), TransformAsset (+0x44).
// HOOKS: the model's GPU half (world/skeleton.hxx), the second instance +0x2A8 and the static LOD +0x2AC (the copy 0x532990
// is a draw input only: nothing on the CPU side reads them). The renderer still draws its own hull (world/models.hxx).

struct boat_rig_t
{
    std::unique_ptr< skel_model_t > model;        // boat +0x2A4 (null: no model loaded)
    std::unique_ptr< skel_animated_t > animated;  // anim +4
    skel_animation_t clip[ 3 ];                   // the three controls' VuAnimation (their asset's +0x58)
    skel_control_t control[ 3 ];
    bool has[ 3 ] = { };                          // anim +8 / +C / +10 non-null
    std::uint8_t control_image[ 3 ][ 0x2C ]{ };   // what boat_anim_tick reads and writes: +0x18 factor, +0x1C time
    std::uint8_t asset_image[ 3 ][ 0x20 ]{ };     // +0x18 the clip's end
};

// 0x5322C0's first load (+0xC null): the asset's skeleton and scene into a fresh instance (0x532360). False: no such asset.
inline bool boat_rig_model( boat_rig_t& rig, const std::uint8_t* blob, std::size_t size )
{
    std::unique_ptr< skel_model_t > m( new skel_model_t( ) );
    if ( !skel_model_create( *m, blob, size ) )
        return false;
    rig.model = std::move( m );
    return true;
}

// 0x516300 with dt 0 and no listener (+0x28 stays 0 on all three): only the wrap / clamp can move the time. The clamp is
// minss( end, t ) then maxss( 0, . ), read at 0x5163A1.
inline void boat_rig_settle( skel_control_t& c )
{
    const skel_animation_t& anim = *c.animation;
    float t = c.factor * 0.0f + c.time;
    c.time = t;
    if ( c.looping )
    {
        while ( t >= anim.loop )
        {
            t = t - anim.loop;
            c.time = t;
        }
        t = c.time;
        while ( 0.0f > t )
        {
            t = t + anim.loop;
            c.time = t;
        }
    }
    else
        c.time = skel_maxss( 0.0f, skel_minss( anim.end, t ) );
}

// 0x410610 (thiscall anim; ret): nothing without the model's skeleton. Else the VuAnimatedSkeleton 0x516750 on it, then per
// asset that exists (0x410930 = asset factory slot 10) a control 0x516120 (asset factory slot 7 through 0x5166A0; looping 1,
// factor 1, time 0, weight 1; the asset's +0x58 the animation - a null one releases the control). In retail's order:
//   Thrust: added 0x5173D0, factor 0.
//   Turn:   added, factor 0, time = end x 0.5 (0x6F765C), 0x516300( 0 ). Looping stays 1.
//   Xform:  looping 0, 0x516300( 0 ) (at factor 1: dt 0 moves nothing), factor 0, added.
// `read`: the asset's blob by its name, false when the asset does not exist.
inline void boat_rig_create( boat_rig_t& rig, const std::string* names,
                             const std::function< bool( const std::string& name, std::vector< std::uint8_t >& blob ) >& read )
{
    if ( !rig.model || rig.model->instance.skeleton == nullptr )
        return;
    rig.animated.reset( new skel_animated_t( ) );
    skel_animated_init( *rig.animated, *rig.model->instance.skeleton );
    for ( int k = 0; k < 3; ++k )
    {
        rig.has[ k ] = false;
        std::vector< std::uint8_t > blob;
        if ( !read( names[ k ], blob ) )
            continue;
        if ( !skel_animation_load( blob.data( ), blob.size( ), rig.clip[ k ] ) )
            continue;   // the asset's +0x58 null: the control released, the slot 0
        skel_control_t& c = rig.control[ k ];
        c = skel_control_t{ };
        c.animation = &rig.clip[ k ];
        rig.has[ k ] = true;
        state_put_f( rig.asset_image[ k ], 0x18, c.animation->end );
        if ( k == 0 )
        {
            skel_animated_add( *rig.animated, &c );
            c.factor = 0.0f;
        }
        else if ( k == 1 )
        {
            skel_animated_add( *rig.animated, &c );
            c.factor = 0.0f;
            c.time = c.animation->end * 0.5f;
            boat_rig_settle( c );
        }
        else
        {
            c.looping = false;
            boat_rig_settle( c );
            c.factor = 0.0f;
            skel_animated_add( *rig.animated, &c );
        }
    }
}

// The animation tick 0x40FFF0 (boat/anim.hxx boat_anim_tick) on the rig's controls. `throttle` / `steer`: the driver's vt+0x64 /
// vt+0x68 answers (pure reads of boat state, so taken before the call). Events: the audio layer's, none made (0x50FBF0 a hook
// answering false), so no FMOD event is ever set.
struct boat_rig_inputs_t
{
    std::uint8_t* anim = nullptr;     // boat +0x230
    const std::uint8_t* boat = nullptr;
    float throttle = 0.0f, steer = 0.0f;
    const float* position = nullptr;  // the transform's +0x80
    const float* velocity = nullptr;  // the body's +0x140
    const std::uint8_t* engine = nullptr;
    void ( *voice )( void* user, const char* line ) = nullptr;
    void* voice_user = nullptr;
};

inline void boat_rig_animation_tick( boat_rig_t& rig, const boat_rig_inputs_t& in, float dt )
{
    boat_anim_t a;
    a.anim = in.anim;
    a.boat = in.boat;
    a.position = in.position;
    a.velocity = in.velocity;
    a.engine = in.engine;
    for ( int k = 0; k < 3; ++k )
        if ( rig.has[ k ] )
        {
            state_put_f( rig.control_image[ k ], 0x18, rig.control[ k ].factor );
            state_put_f( rig.control_image[ k ], 0x1C, rig.control[ k ].time );
            a.control[ k ] = rig.control_image[ k ];
            a.asset[ k ] = rig.asset_image[ k ];
        }
    struct ctx_t
    {
        boat_rig_t* rig;
        const boat_rig_inputs_t* in;
    } ctx{ &rig, &in };
    boat_anim_hooks_t h;
    h.user = &ctx;
    h.driver_64 = []( void* u ) { return static_cast< ctx_t* >( u )->in->throttle; };
    h.driver_68 = []( void* u ) { return static_cast< ctx_t* >( u )->in->steer; };
    h.control_516300 = []( void* u ) {
        boat_rig_t& r = *static_cast< ctx_t* >( u )->rig;
        r.control[ 1 ].time = state_f( r.control_image[ 1 ], 0x1C );
        boat_rig_settle( r.control[ 1 ] );
        state_put_f( r.control_image[ 1 ], 0x1C, r.control[ 1 ].time );
    };
    h.voice = []( void* u, const char* line ) {
        const boat_rig_inputs_t& i = *static_cast< ctx_t* >( u )->in;
        if ( i.voice )
            i.voice( i.voice_user, line );
    };
    boat_anim_tick( a, dt, h );
    for ( int k = 0; k < 3; ++k )
        if ( rig.has[ k ] )
        {
            rig.control[ k ].factor = state_f( rig.control_image[ k ], 0x18 );
            rig.control[ k ].time = state_f( rig.control_image[ k ], 0x1C );
        }
}

// The Build tick's step 4 (0x41F56C .. 0x41F5D1) on +0x2A4: anim +4's advance 0x5169E0 + build 0x516CF0 when set, setPose
// 0x532720( anim +4 ), the instance's +0x24 and +0x28 flipped (== 0). No control has a listener, so advance fires nothing.
// False: a path skeleton.hxx refuses (an animation with fewer bones than the skeleton: retail's stale words - UNKNOWN).
inline bool boat_rig_pose( boat_rig_t& rig, float dt )
{
    if ( !rig.model )
        return false;
    bool ok = true;
    if ( rig.animated )
    {
        skel_animated_advance( *rig.animated, dt, []( int, const std::string&, const value_t* ) { } );
        ok = skel_animated_build( *rig.animated );
    }
    skel_instance_t& i = rig.model->instance;
    if ( ok )
        ok = skel_instance_set_pose( i, rig.animated.get( ) );
    i.current = i.current == 0 ? 1 : 0;
    i.word28 = i.word28 == 0 ? 1 : 0;
    return ok;
}
