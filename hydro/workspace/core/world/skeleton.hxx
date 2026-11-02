#pragma once

#include <workspace/core/assets/scene.hxx>
#include <workspace/core/boat/hull.hxx>
#include <workspace/core/world/control.hxx>
#include <workspace/core/world/frontcam.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <new>
#include <vector>
#include <xmmintrin.h>

// THE ANIMATED MODEL'S CPU SIDE (render layer, platform independent), recreated 1:1 from the disassembly
// (water batch 30, tools tmp/b30/skel.txt / skel2.txt / anim.txt / blend.txt / clip.txt):
//   VuSkeleton (0x44; ctor 0x52EDF0, vtable 0x74D19C, +4 the refcount): its binary load 0x52F140 (+ the arrays
//     0x52F420, the derived poses 0x52F4C0), findNodeByName 0x52F3C0; the model -> local transform 0x57FE80.
//     +0xC count, +0x10 names (32 bytes each), +0x14 parent indices, +0x18 the MODEL-space bind pose, +0x1C the
//     LOCAL bind pose (derived), +0x20 the inverse bind matrices (derived), +0x24 the aabb (read).
//   VuAnimation (0x44): its binary load 0x562250 (VuAnimationAsset slot 5 0x5160A0) - +0xC bones, +0x10 frames, +0x14
//     the frame-major TRS records, +0x18 (frames - 1) / 30, +0x1C frames / 30, +0x20 the aabb, +0x40 the additive byte.
//   VuAnimationControl (0x2C; ctor 0x5161F0): +0x10 the animation, +0x14 looping, +0x18 time factor, +0x1C time,
//     +0x20 weight, +0x24 the timed-event asset, +0x28 the listener.
//   VuAnimatedSkeleton (0x58; ctor 0x516750, vtable 0x74CA78): +0xC the skeleton, VuArrays of controls +0x10 (all),
//     +0x1C (blended), +0x28 (additive), +0x34 the local pose, +0x38 the aabb. addControl 0x5173D0, clear 0x517440,
//     advance 0x5169E0, build 0x516CF0 (frame pair 0x5802D0, additive 0x580AC0).
//   VuAnimatedModelInstance (0x98; ctor 0x532080, vtable 0x74D2F8): +0xC the asset, +0x10 the skeleton, +0x14 the
//     scene, +0x18 THE BONE MODEL MATRICES, +0x1C / +0x20 two palettes (current = +0x24), +0x28 1, +0x2C the aabb,
//     +0x4C the mesh's pad past the skeleton aabb, +0x50 bone 0's matrix. setModel 0x532360, setPose 0x532720,
//     copy 0x532990; the local -> model matrices 0x57FAF0.
// Matrices are retail's: 16 floats, row 3 the translation; products through hull_multiply = VuMatrix_multiply
// 0x4078C0 (boat/hull.hxx), the inverse hull_invert = 0x4157C0, quaternion -> matrix front_rig_matrix = 0x531180.
//
// HOOKS (render device / platform, not recreated): VuAnimatedModelInstance's release 0x532200's first block (with
// VuGfxSort g 0x759074 +0xC set: the render-thread flush - g 0x759098 vt+0x3C, VuMetroD3d11Gfx g 0x75907C vt+0x30 /
// vt+0xD0), the asset manager's release / create (g 0x759004 vt+0x24, 0x534000 / 0x5340B0) and the draw 0x532AD0
// (shader constants, gMatrixArray upload); the scene's GPU objects (assets/scene.hxx). The timed events' walk is
// world/control.hxx's (0x516570 / 0x516610, verified there). The dev / tool paths are read, not recreated: the
// skeleton's JSON load 0x52EF50 and save 0x52F200, the source-file loader 0x5A8200, the pose aabb 0x580670 (only
// 0x52EF50 calls it), the dev stat dump 0x517480.
//
// UNKNOWN (retail stores an unwritten stack word; never compared): the w lane of every position 0x57FE80 / 0x57FAF0
// write, the position and scale w lanes after an additive pass (0x580AC0), the animated skeleton's aabb w lanes
// after a build that blends (0x516CF0), and the static empty aabb 0x7593F0's w lanes (its CRT initializer 0x64B690
// reads unset locals).

struct skel_xform_t   // VuAnimationTransform: position, quaternion (x y z w), scale - 16 bytes each
{
    float pos[ 4 ]{ };
    float rot[ 4 ]{ };
    float scale[ 4 ]{ };
};
static_assert( sizeof( skel_xform_t ) == 48, "the 48-byte record" );

inline float skel_minss( float a, float b )   // minss a, b: a < b ? a : b (b on NaN)
{
    return _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) );
}

inline float skel_maxss( float a, float b )   // maxss a, b: a > b ? a : b (b on NaN)
{
    return _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) );
}

inline float skel_epsilon( )   // 0x6F7584
{
    float f;
    const std::uint32_t w = 0x34000000u;
    std::memcpy( &f, &w, 4 );
    return f;
}

// The static aabb 0x7593F0 as its CRT initializer 0x64B690 leaves it: every x y z lane 0 (the w lanes are two unset
// locals - UNKNOWN, 0 here). Not an "empty" box: a zero-size box at the origin.
inline void skel_aabb_static( float* aabb )
{
    for ( int k = 0; k < 8; ++k )
        aabb[ k ] = 0.0f;
}

// The empty aabb the builds write inline (0x516CF0, 0x5617B0, the ctors 0x52EDF0 / 0x561990): min FLT_MAX, max
// -FLT_MAX (w lanes: the stack pair's - UNKNOWN in the builds, 0 here).
inline void skel_aabb_empty( float* aabb )
{
    for ( int k = 0; k < 3; ++k )
    {
        aabb[ k ] = 3.40282347e+38f;
        aabb[ 4 + k ] = -3.40282347e+38f;
    }
    aabb[ 3 ] = 0.0f;
    aabb[ 7 ] = 0.0f;
}

// The matrix of a transform as 0x52F4C0 / 0x532360 / 0x532720 / 0x57FE80 build it: Q = 0x531180( rot ), S = the
// scale diagonal (zeros and the 1 written in place), M = VuMatrix_multiply( S, Q ), row 3 = the position (x y z).
inline void skel_xform_matrix( const skel_xform_t& x, float* m )
{
    float q[ 16 ];
    front_rig_matrix( x.rot, q );
    const float s[ 16 ] = { x.scale[ 0 ], 0, 0, 0, 0, x.scale[ 1 ], 0, 0, 0, 0, x.scale[ 2 ], 0, 0, 0, 0, 1 };
    hull_multiply( s, m, q );
    m[ 12 ] = x.pos[ 0 ];
    m[ 13 ] = x.pos[ 1 ];
    m[ 14 ] = x.pos[ 2 ];
}

// ---------------------------------------------------------------------------------------------------------------
// VuSkeleton

struct skel_skeleton_t
{
    int count = 0;                          // +0xC
    std::vector< char > names;              // +0x10, 32 bytes per bone
    std::vector< std::int32_t > parents;    // +0x14
    std::vector< skel_xform_t > model;      // +0x18
    std::vector< skel_xform_t > local;      // +0x1C
    std::vector< float > inverse;           // +0x20, 16 per bone
    float aabb[ 8 ]{ };                     // +0x24 (min x y z w, max x y z w)
    bool unknown = false;                   // a parent index retail would read out of its array with
};

// 0x52F3C0 (thiscall skeleton; name; retn 4): the first bone whose 32-byte name strcmp-equals `name`, else -1.
inline int skel_find_node( const skel_skeleton_t& s, const char* name )
{
    for ( int i = 0; i < s.count; ++i )
        if ( std::strcmp( s.names.data( ) + 32 * i, name ) == 0 )
            return i;
    return -1;
}

// 0x57FE80 (ecx count, edx parents, cdecl model in, local out): bone 0 copied (all 48 bytes); each other bone's
// local = inverse( matrix( model[ parent ] ) ) applied to its position, conjugate( q_parent ) / |q_parent|^2 times
// its rotation, its scale copied. False (UNKNOWN) on a parent index outside the array.
inline bool skel_model_to_local( int count, const std::int32_t* parents, const skel_xform_t* in, skel_xform_t* out )
{
    if ( count <= 0 )
        return true;
    out[ 0 ] = in[ 0 ];
    for ( int i = 1; i < count; ++i )
    {
        const int p = parents[ i ];
        if ( p < 0 || p >= count )
            return false;
        const skel_xform_t& parent = in[ p ];
        float m[ 16 ];
        skel_xform_matrix( parent, m );
        hull_invert( m );
        const float x = in[ i ].pos[ 0 ], y = in[ i ].pos[ 1 ], z = in[ i ].pos[ 2 ];
        skel_xform_t& o = out[ i ];
        o.pos[ 0 ] = ( ( x * m[ 0 ] + y * m[ 4 ] ) + z * m[ 8 ] ) + m[ 12 ];
        o.pos[ 1 ] = ( ( x * m[ 1 ] + y * m[ 5 ] ) + z * m[ 9 ] ) + m[ 13 ];
        o.pos[ 2 ] = ( ( x * m[ 2 ] + y * m[ 6 ] ) + z * m[ 10 ] ) + m[ 14 ];
        o.pos[ 3 ] = 0.0f;   // UNKNOWN (ebp-0x50)
        float ax, ay, az, aw;
        {
            const float qx = parent.rot[ 0 ], qy = parent.rot[ 1 ], qz = parent.rot[ 2 ], qw = parent.rot[ 3 ];
            const float n = ( ( qy * qy + qx * qx ) + qz * qz ) + qw * qw;
            if ( n > 0.0f )
            {
                const float inv = 1.0f / n;
                ax = -( qx * inv );
                ay = -( qy * inv );
                az = -( qz * inv );
                aw = qw * inv;
            }
            else
                ax = ay = az = aw = 0.0f;
        }
        const float bx = in[ i ].rot[ 0 ], by = in[ i ].rot[ 1 ], bz = in[ i ].rot[ 2 ], bw = in[ i ].rot[ 3 ];
        o.rot[ 0 ] = ( ( bx * aw + bw * ax ) + by * az ) - bz * ay;
        o.rot[ 1 ] = ( ( bw * ay + by * aw ) + bz * ax ) - bx * az;
        o.rot[ 2 ] = ( ( bz * aw + bw * az ) + bx * ay ) - by * ax;
        o.rot[ 3 ] = ( ( bw * aw - bx * ax ) - by * ay ) - bz * az;
        std::memcpy( o.scale, in[ i ].scale, 16 );
    }
    return true;
}

// 0x57FAF0 (ecx count, edx parents, cdecl local in, model out, matrices out): bone 0's model = its local (all 48
// bytes), its matrix through VuMatrix_fromScale 0x407DB0; each other bone: position = the parent's MATRIX applied to
// the local position, rotation = q_parent(model) x q_local, scale = the LOCAL scale (not the parent's product), and
// its matrix from that model transform. False (UNKNOWN) on a parent index outside the array.
inline bool skel_local_to_model( int count, const std::int32_t* parents, const skel_xform_t* local, skel_xform_t* model,
    float* matrices )
{
    if ( count <= 0 )
        return true;
    model[ 0 ] = local[ 0 ];
    skel_xform_matrix( model[ 0 ], matrices );
    for ( int i = 1; i < count; ++i )
    {
        const int p = parents[ i ];
        if ( p < 0 || p >= count )
            return false;
        const float* P = matrices + 16 * p;
        const skel_xform_t& l = local[ i ];
        skel_xform_t& o = model[ i ];
        const float x = l.pos[ 0 ], y = l.pos[ 1 ], z = l.pos[ 2 ];
        o.pos[ 0 ] = ( ( P[ 4 ] * y + x * P[ 0 ] ) + P[ 8 ] * z ) + P[ 12 ];
        o.pos[ 1 ] = ( ( P[ 1 ] * x + P[ 5 ] * y ) + P[ 9 ] * z ) + P[ 13 ];
        o.pos[ 2 ] = ( ( P[ 2 ] * x + P[ 6 ] * y ) + P[ 10 ] * z ) + P[ 14 ];
        o.pos[ 3 ] = 0.0f;   // UNKNOWN (ebp-0x30)
        const float* a = model[ p ].rot;
        const float ax = a[ 0 ], ay = a[ 1 ], az = a[ 2 ], aw = a[ 3 ];
        const float bx = l.rot[ 0 ], by = l.rot[ 1 ], bz = l.rot[ 2 ], bw = l.rot[ 3 ];
        o.rot[ 0 ] = ( ( aw * bx + ax * bw ) + az * by ) - ay * bz;
        o.rot[ 1 ] = ( ( ay * bw + aw * by ) + ax * bz ) - az * bx;
        o.rot[ 2 ] = ( ( az * bw + aw * bz ) + ay * bx ) - ax * by;
        o.rot[ 3 ] = ( ( aw * bw - ax * bx ) - ay * by ) - az * bz;
        std::memcpy( o.scale, l.scale, 16 );
        skel_xform_matrix( o, matrices + 16 * i );
    }
    return true;
}

// 0x52F4C0: the local bind pose (0x57FE80 on +0x18 -> +0x1C), then each bone's inverse bind matrix (+0x20) = the
// matrix of its model pose inverted in place (0x4157C0).
inline void skel_skeleton_finish( skel_skeleton_t& s )
{
    if ( !skel_model_to_local( s.count, s.parents.data( ), s.model.data( ), s.local.data( ) ) )
        s.unknown = true;
    for ( int b = 0; b < s.count; ++b )
    {
        float* m = s.inverse.data( ) + 16 * b;
        skel_xform_matrix( s.model[ static_cast< std::size_t >( b ) ], m );
        hull_invert( m );
    }
}

// 0x52F140 (thiscall skeleton; reader*; retn 4): u32 count, 0x52F420's arrays, then the names (32 x count), the
// parents (4 x count), the model poses (48 x count) and the aabb (32), each a memcpy_s at the cursor; then 0x52F4C0.
inline bool skel_skeleton_read( scene_reader_t& r, skel_skeleton_t& s )
{
    s = skel_skeleton_t{ };
    std::uint32_t count = scene_u32( r );
    if ( r.bad )
        return false;
    // 0x52F420 new[ ] x 5: 32, 4, 48, 48, 64 x count, a byte size past 32 bits asking for -1, which operator new throws. Retail has no other limit.
    if ( static_cast< std::uint64_t >( count ) * 64u > 0xFFFFFFFFu )
        throw std::bad_alloc( );
    s.count = static_cast< int >( count );
    s.names.assign( 32u * count + 1u, 0 );   // OURS: one trailing NUL - a full 32-byte last name ends there
    s.parents.assign( count, 0 );
    s.model.assign( count, skel_xform_t{ } );
    s.local.assign( count, skel_xform_t{ } );
    s.inverse.assign( 16u * count, 0.0f );
    if ( count )
    {
        scene_take( r, s.names.data( ), 32u * count );
        scene_take( r, s.parents.data( ), 4u * count );
        scene_take( r, s.model.data( ), 48u * count );
    }
    scene_take( r, s.aabb, 32 );
    if ( r.bad )
        return false;
    skel_skeleton_finish( s );
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// VuAnimation

struct skel_animation_t
{
    int bones = 0;                          // +0xC
    int frames = 0;                         // +0x10
    std::vector< skel_xform_t > data;       // +0x14, frame-major
    float end = 0.0f;                       // +0x18
    float loop = 0.0f;                      // +0x1C
    float aabb[ 8 ]{ };                     // +0x20
    std::uint8_t additive = 0;              // +0x40
};

// 0x562250 (thiscall animation; reader*; retn 4).
inline bool skel_animation_read( scene_reader_t& r, skel_animation_t& a )
{
    a = skel_animation_t{ };
    const std::uint32_t bones = scene_u32( r );
    const std::uint32_t frames = scene_u32( r );
    if ( r.bad )
        return false;
    a.bones = static_cast< int >( bones );
    a.frames = static_cast< int >( frames );
    // new[ ] of 0x30 * ( bones * frames ): the product wraps at 32 bits, a byte size past 32 bits asks for -1 (seto / neg / or) and
    // operator new throws. Retail has no other limit.
    const std::uint32_t count = bones * frames;
    if ( static_cast< std::uint64_t >( count ) * 48u > 0xFFFFFFFFu )
        throw std::bad_alloc( );
    a.data.assign( count, skel_xform_t{ } );
    if ( !a.data.empty( ) )
        scene_take( r, a.data.data( ), 48u * a.data.size( ) );
    scene_take( r, a.aabb, 32 );
    a.additive = scene_u8( r );
    if ( r.bad )
        return false;
    const float k = 0.0333333351f;   // 0x6F75F4
    a.end = static_cast< float >( a.frames - 1 ) * k;
    a.loop = static_cast< float >( a.frames ) * k;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// VuAnimationControl / VuAnimatedSkeleton

struct skel_control_t
{
    const skel_animation_t* animation = nullptr;          // +0x10
    bool looping = true;                                  // +0x14
    float factor = 1.0f;                                  // +0x18
    float time = 0.0f;                                    // +0x1C
    float weight = 1.0f;                                  // +0x20
    const std::vector< timed_event_t >* events = nullptr; // +0x24 (null: no timed-event asset)
    bool listener = false;                                // +0x28 non-null
};

struct skel_animated_t
{
    const skel_skeleton_t* skeleton = nullptr;            // +0xC
    std::vector< skel_control_t* > controls;              // +0x10
    std::vector< skel_control_t* > blended;               // +0x1C
    std::vector< skel_control_t* > additive;              // +0x28
    std::vector< skel_xform_t > local;                    // +0x34
    float aabb[ 8 ]{ };                                   // +0x38
    std::vector< skel_xform_t > scratch;                  // the static 0x759898 (interpolated frames, model poses)
    bool unknown = false;
};

// 0x516750 (thiscall; skeleton): the three arrays (capacity 8), the aabb = the static 0x7593F0, the local pose = a copy of the
// skeleton's local bind pose.
inline void skel_animated_init( skel_animated_t& a, const skel_skeleton_t& s )
{
    a = skel_animated_t{ };
    a.skeleton = &s;
    a.local = s.local;
    skel_aabb_static( a.aabb );
}

// 0x5173D0 (thiscall; control; retn 4): appended to +0x10, then to +0x28 when its animation's +0x40 byte is set,
// else to +0x1C. The control's refcount + 1 (lifetime: the caller's).
inline void skel_animated_add( skel_animated_t& a, skel_control_t* c )
{
    a.controls.push_back( c );
    if ( c->animation->additive )
        a.additive.push_back( c );
    else
        a.blended.push_back( c );
}

// 0x517440: every control released, the three counts zeroed.
inline void skel_animated_clear( skel_animated_t& a )
{
    a.controls.clear( );
    a.blended.clear( );
    a.additive.clear( );
}

// 0x5169E0 (__usercall: ecx the skeleton, xmm1 dt): per control of +0x10, in order, VuAnimationControl's advance
// inlined (control.hxx control_advance's body - read again here: the clamp is minss( end, t ) then maxss( 0, . ),
// so a NaN time stays NaN). `fire( control index, name, params )` = the listener's slot 0 (AnimDone / AnimLoop with
// retail's shared container 0x7593A0 = null params).
template < typename fire_t >
inline void skel_animated_advance( skel_animated_t& a, float dt, fire_t&& fire )
{
    for ( std::size_t index = 0; index < a.controls.size( ); ++index )
    {
        skel_control_t& c = *a.controls[ index ];
        const skel_animation_t& anim = *c.animation;
        const float old = c.time;
        float t = c.factor * dt + old;
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
        const float now = c.time;
        if ( old == now )   // ucomiss; lahf; test ah, 44h; jnp: only an ordered equality skips
            continue;
        if ( !c.listener )
            continue;
        const bool has = c.events != nullptr && !c.events->empty( );
        animation_control_t walk{ };
        walk.events = c.events;
        auto on = [ & ]( const std::string& name, const value_t* params ) { fire( static_cast< int >( index ), name, params ); };
        if ( dt > 0.0f )
        {
            if ( now > old )
            {
                if ( has )
                    control_walk_forward( walk, old, now, on );
                if ( c.time == anim.end )
                    fire( static_cast< int >( index ), std::string( "AnimDone" ), nullptr );
            }
            else
            {
                if ( has )
                {
                    control_walk_forward( walk, old, anim.loop, on );
                    control_walk_forward( walk, 0.0f, c.time, on );
                }
                fire( static_cast< int >( index ), std::string( "AnimLoop" ), nullptr );
            }
        }
        else if ( 0.0f > dt )
        {
            if ( old > now )
            {
                if ( has )
                    control_walk_backward( walk, old, now, on );
                if ( c.time == 0.0f )
                    fire( static_cast< int >( index ), std::string( "AnimDone" ), nullptr );
            }
            else
            {
                if ( has )
                {
                    control_walk_backward( walk, old, 0.0f, on );
                    control_walk_backward( walk, anim.loop, c.time, on );
                }
                fire( static_cast< int >( index ), std::string( "AnimLoop" ), nullptr );
            }
        }
    }
}

// 0x5802D0 (ecx count, edx frame A, cdecl frame B, out; xmm3 t): out zeroed (memset), then A with weight 1 - t and
// B with weight t added in, each rotation's weight negated when its dot with the running sum is negative (A's against
// zeros: never); the rotation renormalised (len > FLT_EPSILON, else zero).
inline void skel_interpolate( int count, const skel_xform_t* A, const skel_xform_t* B, float t, skel_xform_t* out )
{
    if ( count > 0 )
        std::memset( out, 0, 48u * static_cast< std::size_t >( count ) );
    const float u = 1.0f - t;
    for ( int b = 0; b < count; ++b )
    {
        skel_xform_t& o = out[ b ];
        const skel_xform_t* src[ 2 ] = { A + b, B + b };
        const float w[ 2 ] = { u, t };
        for ( int pass = 0; pass < 2; ++pass )
        {
            const skel_xform_t& s = *src[ pass ];
            const float k0 = w[ pass ];
            o.pos[ 0 ] = s.pos[ 0 ] * k0 + o.pos[ 0 ];
            o.pos[ 1 ] = o.pos[ 1 ] + s.pos[ 1 ] * k0;
            o.pos[ 2 ] = s.pos[ 2 ] * k0 + o.pos[ 2 ];
            o.scale[ 0 ] = o.scale[ 0 ] + s.scale[ 0 ] * k0;
            o.scale[ 1 ] = o.scale[ 1 ] + s.scale[ 1 ] * k0;
            o.scale[ 2 ] = o.scale[ 2 ] + s.scale[ 2 ] * k0;
            const float ox = o.rot[ 0 ];
            const float dot = ( ( s.rot[ 0 ] * ox + o.rot[ 1 ] * s.rot[ 1 ] ) + s.rot[ 2 ] * o.rot[ 2 ] ) + s.rot[ 3 ] * o.rot[ 3 ];
            const float k = ( 0.0f > dot ) ? -k0 : k0;
            o.rot[ 0 ] = ox + s.rot[ 0 ] * k;
            o.rot[ 3 ] = s.rot[ 3 ] * k + o.rot[ 3 ];
            o.rot[ 1 ] = o.rot[ 1 ] + s.rot[ 1 ] * k;
            o.rot[ 2 ] = o.rot[ 2 ] + s.rot[ 2 ] * k;
        }
        const float x = o.rot[ 0 ];
        const float len = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss(
            ( ( o.rot[ 1 ] * o.rot[ 1 ] + x * x ) + o.rot[ 2 ] * o.rot[ 2 ] ) + o.rot[ 3 ] * o.rot[ 3 ] ) ) );
        if ( len > skel_epsilon( ) )
        {
            const float k = 1.0f / len;
            o.rot[ 0 ] = x * k;
            o.rot[ 1 ] = o.rot[ 1 ] * k;
            o.rot[ 2 ] = o.rot[ 2 ] * k;
            o.rot[ 3 ] = k * o.rot[ 3 ];
        }
        else
            o.rot[ 0 ] = o.rot[ 1 ] = o.rot[ 2 ] = o.rot[ 3 ] = 0.0f;
    }
}

// 0x580AC0 (ecx count, edx the additive frame, cdecl pose; xmm2 weight w): per bone, u = 1 - w, position (A + P) w +
// u P, rotation (P x A) w + u P renormalised, scale (A P) w + u P. The position and scale w lanes come back UNKNOWN.
inline void skel_add( int count, const skel_xform_t* A, skel_xform_t* pose, float w )
{
    const float u = 1.0f - w;
    for ( int b = 0; b < count; ++b )
    {
        const skel_xform_t& a = A[ b ];
        skel_xform_t& p = pose[ b ];
        const float x = ( a.pos[ 0 ] + p.pos[ 0 ] ) * w + u * p.pos[ 0 ];
        const float y = ( a.pos[ 1 ] + p.pos[ 1 ] ) * w + u * p.pos[ 1 ];
        const float z = ( a.pos[ 2 ] + p.pos[ 2 ] ) * w + u * p.pos[ 2 ];
        p.pos[ 0 ] = x;
        p.pos[ 1 ] = y;
        p.pos[ 2 ] = z;
        p.pos[ 3 ] = 0.0f;   // UNKNOWN (ebp-0x34)
        const float ax = a.rot[ 0 ], ay = a.rot[ 1 ], az = a.rot[ 2 ], aw = a.rot[ 3 ];
        const float px = p.rot[ 0 ], py = p.rot[ 1 ], pz = p.rot[ 2 ], pw = p.rot[ 3 ];
        const float rx = ( ( aw * px + ax * pw ) + az * py ) - pz * ay;
        const float ry = ( ( aw * py + ay * pw ) + pz * ax ) - az * px;
        const float rz = ( ( pz * aw + az * pw ) + ay * px ) - ax * py;
        const float rw = ( ( aw * pw - ax * px ) - ay * py ) - pz * az;
        const float qx = u * px + rx * w;
        const float qy = py * u + ry * w;
        const float qw = u * pw + rw * w;
        const float qz = pz * u + rz * w;
        const float sx = ( a.scale[ 0 ] * p.scale[ 0 ] ) * w + u * p.scale[ 0 ];
        const float sy = ( a.scale[ 1 ] * p.scale[ 1 ] ) * w + u * p.scale[ 1 ];
        const float sz = ( a.scale[ 2 ] * p.scale[ 2 ] ) * w + p.scale[ 2 ] * u;
        p.scale[ 0 ] = sx;
        p.scale[ 1 ] = sy;
        p.scale[ 2 ] = sz;
        p.scale[ 3 ] = 0.0f;   // UNKNOWN (ebp-0x44)
        const float len = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( ( ( qx * qx + qy * qy ) + qz * qz ) + qw * qw ) ) );
        if ( len > skel_epsilon( ) )
        {
            const float k = 1.0f / len;
            p.rot[ 0 ] = qx * k;
            p.rot[ 1 ] = qy * k;
            p.rot[ 2 ] = qz * k;
            p.rot[ 3 ] = qw * k;
        }
        else
            p.rot[ 0 ] = p.rot[ 1 ] = p.rot[ 2 ] = p.rot[ 3 ] = 0.0f;
    }
}

// The frame pair of a control as 0x516CF0 picks it: f = (float)frames x time / loop, i = fistp( f - 0.5 ) (x87,
// round to nearest even), t = f - (float)i, frames i % frames and (i + 1) % frames (C remainders), interpolated
// (0x5802D0, over the ANIMATION's bones) into the scratch. False on a frame count of 0 (retail divides by it).
inline bool skel_control_sample( const skel_control_t& c, std::vector< skel_xform_t >& scratch )
{
    const skel_animation_t& anim = *c.animation;
    if ( anim.frames <= 0 )
        return false;
    const float f = static_cast< float >( anim.frames ) * c.time / anim.loop;
    const double shifted = static_cast< double >( f ) - 0.5;
    if ( !( shifted > -2147483648.0 && shifted < 2147483647.0 ) )
        return false;   // fistp's integer indefinite - UNKNOWN
    const int i = static_cast< int >( std::nearbyint( shifted ) );
    const float t = f - static_cast< float >( i );
    const int fa = i % anim.frames, fb = ( i + 1 ) % anim.frames;
    if ( fa < 0 || fb < 0 )
        return false;   // UNKNOWN: retail reads before its array
    if ( scratch.size( ) < static_cast< std::size_t >( anim.bones ) )
        scratch.resize( static_cast< std::size_t >( anim.bones ) );
    skel_interpolate( anim.bones, anim.data.data( ) + static_cast< std::size_t >( fa ) * anim.bones,
        anim.data.data( ) + static_cast< std::size_t >( fb ) * anim.bones, t, scratch.data( ) );
    return true;
}

// 0x516CF0 (thiscall animated skeleton): the blended controls' weight sum; above FLT_EPSILON the local pose is zeroed
// and every control of weight > 0 sampled and added in weighted (its rotation's weight negated against a negative
// dot), the aabb grown by each animation's aabb, then everything divided by the sum and the rotations renormalised;
// otherwise the local pose = the skeleton's local bind pose. Then each additive control of weight > 0 sampled and
// applied through 0x580AC0 (over the SKELETON's bones), the aabb grown. False when a path read UNKNOWN memory.
inline bool skel_animated_build( skel_animated_t& a )
{
    const skel_skeleton_t& s = *a.skeleton;
    const int count = s.count;
    float sum = 0.0f;
    for ( const skel_control_t* c : a.blended )
        sum = sum + c->weight;
    auto grow = [ & ]( const skel_animation_t& anim )
    {
        a.aabb[ 0 ] = skel_minss( a.aabb[ 0 ], anim.aabb[ 0 ] );
        a.aabb[ 1 ] = skel_minss( a.aabb[ 1 ], anim.aabb[ 1 ] );
        a.aabb[ 2 ] = skel_minss( a.aabb[ 2 ], anim.aabb[ 2 ] );
        a.aabb[ 4 ] = skel_maxss( a.aabb[ 4 ], anim.aabb[ 4 ] );
        a.aabb[ 5 ] = skel_maxss( a.aabb[ 5 ], anim.aabb[ 5 ] );
        a.aabb[ 6 ] = skel_maxss( a.aabb[ 6 ], anim.aabb[ 6 ] );
        a.aabb[ 3 ] = 0.0f;   // UNKNOWN
        a.aabb[ 7 ] = 0.0f;   // UNKNOWN
    };
    bool ok = true;
    if ( sum > skel_epsilon( ) )
    {
        if ( count > 0 )
            std::memset( a.local.data( ), 0, 48u * static_cast< std::size_t >( count ) );
        skel_aabb_empty( a.aabb );
        for ( const skel_control_t* c : a.blended )
        {
            if ( !( c->weight > 0.0f ) )
                continue;
            if ( c->animation->bones < count || !skel_control_sample( *c, a.scratch ) )   // fewer: retail reads the static's stale words
            {
                ok = false;
                continue;
            }
            const float w = c->weight;
            for ( int b = 0; b < count; ++b )
            {
                const skel_xform_t& sp = a.scratch[ static_cast< std::size_t >( b ) ];
                skel_xform_t& p = a.local[ static_cast< std::size_t >( b ) ];
                p.pos[ 0 ] = p.pos[ 0 ] + sp.pos[ 0 ] * w;
                p.pos[ 2 ] = sp.pos[ 2 ] * w + p.pos[ 2 ];
                p.pos[ 1 ] = sp.pos[ 1 ] * w + p.pos[ 1 ];
                p.scale[ 0 ] = p.scale[ 0 ] + sp.scale[ 0 ] * w;
                p.scale[ 2 ] = sp.scale[ 2 ] * w + p.scale[ 2 ];
                p.scale[ 1 ] = p.scale[ 1 ] + sp.scale[ 1 ] * w;
                const float px = p.rot[ 0 ];
                const float dot = ( ( sp.rot[ 1 ] * p.rot[ 1 ] + sp.rot[ 0 ] * px ) + sp.rot[ 2 ] * p.rot[ 2 ] ) + sp.rot[ 3 ] * p.rot[ 3 ];
                const float k = ( dot >= 0.0f ) ? w : -w;
                p.rot[ 0 ] = px + sp.rot[ 0 ] * k;
                p.rot[ 1 ] = sp.rot[ 1 ] * k + p.rot[ 1 ];
                p.rot[ 2 ] = sp.rot[ 2 ] * k + p.rot[ 2 ];
                p.rot[ 3 ] = sp.rot[ 3 ] * k + p.rot[ 3 ];
            }
            grow( *c->animation );
        }
        if ( count > 0 )
        {
            const float inv = 1.0f / sum;
            for ( int b = 0; b < count; ++b )
            {
                skel_xform_t& p = a.local[ static_cast< std::size_t >( b ) ];
                p.pos[ 0 ] = p.pos[ 0 ] * inv;
                p.pos[ 1 ] = inv * p.pos[ 1 ];
                p.pos[ 2 ] = inv * p.pos[ 2 ];
                p.scale[ 0 ] = p.scale[ 0 ] * inv;
                p.scale[ 1 ] = inv * p.scale[ 1 ];
                p.scale[ 2 ] = inv * p.scale[ 2 ];
                const float x = p.rot[ 0 ];
                const float len = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss(
                    ( ( x * x + p.rot[ 1 ] * p.rot[ 1 ] ) + p.rot[ 2 ] * p.rot[ 2 ] ) + p.rot[ 3 ] * p.rot[ 3 ] ) ) );
                if ( len > skel_epsilon( ) )
                {
                    const float k = 1.0f / len;
                    p.rot[ 0 ] = x * k;
                    p.rot[ 1 ] = k * p.rot[ 1 ];
                    p.rot[ 2 ] = k * p.rot[ 2 ];
                    p.rot[ 3 ] = p.rot[ 3 ] * k;
                }
                else
                    p.rot[ 0 ] = p.rot[ 1 ] = p.rot[ 2 ] = p.rot[ 3 ] = 0.0f;
            }
        }
    }
    else if ( count > 0 )
        std::memcpy( a.local.data( ), s.local.data( ), 48u * static_cast< std::size_t >( count ) );
    for ( const skel_control_t* c : a.additive )
    {
        if ( !( c->weight > 0.0f ) )
            continue;
        if ( c->animation->bones < count || !skel_control_sample( *c, a.scratch ) )   // fewer: retail reads the static's stale words
        {
            ok = false;
            continue;
        }
        skel_add( count, a.scratch.data( ), a.local.data( ), c->weight );
        grow( *c->animation );
    }
    if ( !ok )
        a.unknown = true;
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------
// VuAnimatedModelInstance

struct skel_instance_t
{
    const skel_skeleton_t* skeleton = nullptr;            // +0x10
    std::vector< float > matrices;                        // +0x18, 16 per bone
    std::vector< float > palette[ 2 ];                    // +0x1C / +0x20
    int current = 0;                                      // +0x24
    int word28 = 1;                                       // +0x28
    float aabb[ 8 ]{ };                                   // +0x2C
    float pad = 0.0f;                                     // +0x4C
    float root[ 16 ]{ };                                  // +0x50
    std::vector< skel_xform_t > scratch;                  // the static 0x759898 (setPose's model poses)
    bool unknown = false;
};

// 0x532080: +0x2C = the static 0x7593F0, +0x50 = the static identity 0x7593B0, +0x24 = 0, +0x28 = 1.
inline void skel_instance_init( skel_instance_t& i )
{
    i = skel_instance_t{ };
    skel_aabb_static( i.aabb );
    static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    std::memcpy( i.root, identity, 64 );
}

// 0x532360 (thiscall; skeleton, scene; retn 8) - nothing when both are the ones held. +0x18[ b ] = the matrix of the
// skeleton's model pose b; both palettes identity; +0x2C = the scene's aabb (+0x44); +0x4C = the largest of the six
// amounts the scene box reaches past the skeleton box (max side: scene - skeleton; min side: skeleton - scene).
inline void skel_instance_set_model( skel_instance_t& i, const skel_skeleton_t& s, const float* scene_aabb )
{
    if ( i.skeleton == &s )
        return;   // OURS: the scene half of the same-test (a scene pointer) is the caller's
    i.skeleton = &s;
    const std::size_t n = static_cast< std::size_t >( s.count );
    i.matrices.assign( 16 * n, 0.0f );
    static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    for ( int k = 0; k < 2; ++k )
    {
        i.palette[ k ].assign( 16 * n, 0.0f );
        for ( std::size_t b = 0; b < n; ++b )
            std::memcpy( i.palette[ k ].data( ) + 16 * b, identity, 64 );
    }
    for ( std::size_t b = 0; b < n; ++b )
        skel_xform_matrix( s.model[ b ], i.matrices.data( ) + 16 * b );
    std::memcpy( i.aabb, scene_aabb, 32 );
    const float* sk = s.aabb;
    const float z = scene_aabb[ 6 ] - sk[ 6 ];
    const float ny = -( scene_aabb[ 1 ] - sk[ 1 ] );
    const float nz = -( scene_aabb[ 2 ] - sk[ 2 ] );
    const float nx = -( scene_aabb[ 0 ] - sk[ 0 ] );
    const float y = scene_aabb[ 5 ] - sk[ 5 ];
    const float x = scene_aabb[ 4 ] - sk[ 4 ];
    const float mz = skel_maxss( z, nz );
    const float my = skel_maxss( y, ny );
    const float mx = skel_maxss( x, nx );
    i.pad = skel_maxss( skel_maxss( mx, my ), mz );
}

// 0x532720 (thiscall; animated skeleton or null; retn 4). With one: 0x57FAF0 on its local pose (count = ITS
// skeleton's, parents = the instance's) into +0x18, palette[ +0x24 ][ b ] = inverse bind b x +0x18[ b ], +0x2C = its
// aabb grown by +0x4C, +0x50 = +0x18[ 0 ]. Without: +0x18 = the bind matrices, the palette identity, +0x50 identity.
inline bool skel_instance_set_pose( skel_instance_t& i, const skel_animated_t* a )
{
    const skel_skeleton_t& s = *i.skeleton;
    float* palette = i.palette[ i.current & 1 ].data( );
    if ( a != nullptr )
    {
        const int count = a->skeleton->count;
        if ( count > s.count || static_cast< int >( a->local.size( ) ) < count )
        {
            i.unknown = true;
            return false;   // UNKNOWN: retail indexes past the instance's arrays
        }
        if ( i.scratch.size( ) < static_cast< std::size_t >( count ) )
            i.scratch.resize( static_cast< std::size_t >( count ) );
        if ( !skel_local_to_model( count, s.parents.data( ), a->local.data( ), i.scratch.data( ), i.matrices.data( ) ) )
        {
            i.unknown = true;
            return false;
        }
        for ( int b = 0; b < count; ++b )
            hull_multiply( s.inverse.data( ) + 16 * b, palette + 16 * b, i.matrices.data( ) + 16 * b );
        std::memcpy( i.aabb, a->aabb, 32 );
        const float pad = i.pad;
        i.aabb[ 4 ] = pad + i.aabb[ 4 ];
        i.aabb[ 5 ] = i.aabb[ 5 ] + pad;
        i.aabb[ 6 ] = i.aabb[ 6 ] + pad;
        i.aabb[ 0 ] = i.aabb[ 0 ] - pad;
        i.aabb[ 1 ] = i.aabb[ 1 ] - pad;
        i.aabb[ 2 ] = i.aabb[ 2 ] - pad;
        std::memcpy( i.root, i.matrices.data( ), 64 );
        return true;
    }
    static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    for ( int b = 0; b < s.count; ++b )
    {
        skel_xform_matrix( s.model[ static_cast< std::size_t >( b ) ], i.matrices.data( ) + 16 * b );
        std::memcpy( palette + 16 * b, identity, 64 );
    }
    std::memcpy( i.root, identity, 64 );
    return true;
}

// 0x532990 (thiscall; other instance; retn 4): +0x18 and the current palette copied from the other's (this
// skeleton's count of matrices), the aabb = the other's with the other's pad taken back off and this pad put on,
// +0x50 = +0x18[ 0 ].
inline void skel_instance_copy( skel_instance_t& i, const skel_instance_t& o )
{
    const std::size_t n = 16 * static_cast< std::size_t >( i.skeleton->count );
    std::memcpy( i.matrices.data( ), o.matrices.data( ), 4 * n );
    std::memcpy( i.palette[ i.current & 1 ].data( ), o.palette[ o.current & 1 ].data( ), 4 * n );
    std::memcpy( i.aabb, o.aabb, 32 );
    const float op = o.pad, mp = i.pad;
    i.aabb[ 4 ] = i.aabb[ 4 ] - op;
    i.aabb[ 5 ] = i.aabb[ 5 ] - op;
    i.aabb[ 6 ] = i.aabb[ 6 ] - op;
    i.aabb[ 0 ] = op + i.aabb[ 0 ];
    i.aabb[ 1 ] = i.aabb[ 1 ] + op;
    i.aabb[ 2 ] = i.aabb[ 2 ] + op;
    i.aabb[ 4 ] = mp + i.aabb[ 4 ];
    i.aabb[ 5 ] = i.aabb[ 5 ] + mp;
    i.aabb[ 6 ] = i.aabb[ 6 ] + mp;
    i.aabb[ 0 ] = i.aabb[ 0 ] - mp;
    i.aabb[ 1 ] = i.aabb[ 1 ] - mp;
    i.aabb[ 2 ] = i.aabb[ 2 ] - mp;
    std::memcpy( i.root, i.matrices.data( ), 64 );
}

// The bone matrix a caller attaches to (the boat's pfx fill, the attach entities): +0x18 + 64 x index, or null for
// an index findNodeByName answered -1 / one past the skeleton.
inline const float* skel_instance_bone( const skel_instance_t& i, int index )
{
    if ( i.skeleton == nullptr || index < 0 || index >= i.skeleton->count )
        return nullptr;
    return i.matrices.data( ) + 16 * static_cast< std::size_t >( index );
}

// ---------------------------------------------------------------------------------------------------------------
// VuAnimatedModelAsset's load 0x5A83E0 (slot 5): the VuGfxScene (0x64, ctor 0x561470) read by 0x5C1560 with flag 1,
// its aabb 0x5617B0 (every mesh's aabb merged; still empty -> both corners 0), then the VuSkeleton (0x44) read by
// 0x52F140 from the same reader. A failed scene read returns false (retail's slot 6 then releases).

struct skel_model_asset_t
{
    scene_t scene;
    float scene_aabb[ 8 ]{ };   // the VuGfxScene +0x44
    skel_skeleton_t skeleton;
    std::size_t skeleton_offset = 0;   // where the scene's read left the reader
    std::size_t reader_end = 0;        // and where the skeleton's left it
};

// 0x5617B0: min / max over the meshes (+0x18 .. +0x1C, each's aabb at +0x2C / +0x3C: minss( current, mesh ),
// maxss( current, mesh )); then, if min.x is still FLT_MAX (ordered equal), min = VuMath_min( 0, min ) and max =
// 0x403280( 0, max ) per lane. The w lanes: the stack words the movq pairs carry (UNKNOWN; 0 here).
inline void skel_scene_aabb( const scene_t& scene, float* aabb )
{
    skel_aabb_empty( aabb );
    for ( const scene_mesh_t& mesh : scene.meshes )
    {
        for ( int k = 0; k < 3; ++k )
            aabb[ k ] = skel_minss( aabb[ k ], mesh.aabb[ k ] );
        for ( int k = 0; k < 3; ++k )
            aabb[ 4 + k ] = skel_maxss( aabb[ 4 + k ], mesh.aabb[ 4 + k ] );
    }
    if ( aabb[ 0 ] == 3.40282347e+38f )
    {
        for ( int k = 0; k < 3; ++k )
        {
            aabb[ k ] = skel_minss( 0.0f, aabb[ k ] );
            aabb[ 4 + k ] = skel_maxss( 0.0f, aabb[ 4 + k ] );
        }
    }
}

inline bool skel_model_asset_load( const std::uint8_t* data, std::size_t size, skel_model_asset_t& out )
{
    out = skel_model_asset_t{ };
    scene_reader_t r{ };
    r.data = data;
    r.size = size;
    if ( !scene_load_gfx( r, out.scene, 1, false ) )
        return false;
    skel_scene_aabb( out.scene, out.scene_aabb );
    out.skeleton_offset = r.at;
    const bool ok = skel_skeleton_read( r, out.skeleton );
    out.reader_end = r.at;
    return ok;
}

inline bool skel_animation_load( const std::uint8_t* data, std::size_t size, skel_animation_t& out, std::size_t* end = nullptr )
{
    scene_reader_t r{ };
    r.data = data;
    r.size = size;
    const bool ok = skel_animation_read( r, out );
    if ( end )
        *end = r.at;
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------
// THE EXPORTS for the world / boat agents (the pfx fill's find_bone hook, the attach paths, the draw).
//
// A model instance as VuAnimatedModelInstance's setModelAsset 0x5322C0 makes it: the asset (the asset manager's
// create 0x5340B0 = the caller's blob fetch - a platform hook) loaded by 0x5A83E0's CPU half, then setModel 0x532360(
// skeleton +0x5C, scene +0x58). Holds pointers into itself: keep it where it is (std::unique_ptr), never copy it.
struct skel_model_t
{
    skel_model_asset_t asset;
    skel_instance_t instance;
    skel_model_t( ) = default;
    skel_model_t( const skel_model_t& ) = delete;
    skel_model_t& operator=( const skel_model_t& ) = delete;
};

inline bool skel_model_create( skel_model_t& m, const std::uint8_t* blob, std::size_t size )
{
    if ( !skel_model_asset_load( blob, size, m.asset ) )
        return false;
    skel_instance_init( m.instance );
    skel_instance_set_model( m.instance, m.asset.skeleton, m.asset.scene_aabb );
    return true;
}

// findNodeByName 0x52F3C0 on the instance's skeleton (+0x10): -1 when none / no skeleton.
inline int skel_instance_find_node( const skel_instance_t& i, const char* name )
{
    return i.skeleton != nullptr ? skel_find_node( *i.skeleton, name ) : -1;
}