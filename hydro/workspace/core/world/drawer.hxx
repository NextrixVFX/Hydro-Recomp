#pragma once

#include <workspace/core/world/dbrt.hxx>
#include <workspace/core/world/gfxsort.hxx>

#include <cstdint>
#include <cstring>
#include <vector>

// VU3DDRAWMANAGER (render layer, platform independent), recreated 1:1 from the disassembly (water batch 31). g 0x7590B8, 0x24 bytes, vtable 0x750C18,
// creator 0x5A7320, the global's release 0x5A7310: the tree of every 3d drawable; per view the frustum walk, per light the shadow walk.
//   +4 the tree (world/dbrt.hxx), +8 / +0xC drawn / culled this frame, +0x10 / +0x14 the shadow pass's, +0x18 the aligned list, +0x1C its count, +0x20 its capacity.
// What it does is route calls; everything it calls is a hook here:
//   the light manager's submit 0x562B10 on g 0x759094 (world/lights.hxx submit), the composer [g 0x7590BC +4] vt+0x20, the tick manager g 0x759008 vt+0x20 / +0x28
//   (the method 0x5A79F0, phase "Final"), the render hook manager g 0x759024 vt+0x1C / +0x20 (the method 0x5A7980).
// A drawable's two methods are VuMethods (vt+8) of its owner.
// Not recreated, both dev: the ctor's dev page "3dDrawManager" (g 0x759020 vt+0x18 with the rect 50, 10, 40, 80), the page's five stats in the tick 0x5A79F0, and the
// leaf 0x5A7A90's magenta aabb (0x5085E0, colour 0xFF00FFFF, when the dev byte 0x759046 is set). STOPPED with the other dev pages / debug draw.
//
// The list +0x18: slot 8, the add, is retail's empty function 0x4021A0, so nothing ever enters it; remove 0x5A7800 and the flush 0x5A7980 run on nothing.

// What the draw reads of VuCamera.
struct gfx_camera_t
{
    float m_eye[ 3 ] = { };             // +0
    float m_far = 0.0f;                 // +0x17C
    float m_planes[ 24 ] = { };         // +0x1E0
};

// VuGfxDrawParams, as 0x5A75E0 / 0x562B10 / 0x535220 read it.
struct gfx_draw_params_t
{
    float m_eye[ 4 ] = { };             // +0
    const gfx_camera_t* m_camera = nullptr;  // +0x10
    float m_lod = 0.0f;                 // +0x14
    std::uint8_t m_reflecting = 0;      // +0x18 also the mask a drawable must carry
    float m_clip[ 4 ] = { };            // +0x1C the plane 0x535220 always reads; the walk's seventh when +0x18 is set
    std::uint8_t m_second_depth = 0;    // +0x2C
    std::uint8_t m_skip_models = 0;     // +0x2D the draw components draw nothing
};

// The light's volume: up to 12 planes (0xC0 bytes), +0xC0 the count.
struct gfx_shadow_volume_t
{
    float m_planes[ 48 ] = { };
    int m_count = 0;
};

// A cascade, 0x104 bytes: +0 the matrix, +0x40 its volume.
struct gfx_shadow_cascade_t
{
    float m_matrix[ 16 ] = { };
    gfx_shadow_volume_t m_volume;
};

// VuGfxDrawShadowParams, as 0x5A7730 / 0x5354B0 / 0x535740 read it.
struct gfx_shadow_params_t
{
    float m_eye[ 3 ] = { };             // +0
    const gfx_shadow_volume_t* m_volume = nullptr;    // +0x14
    int m_cascades = 0;                 // +0x18
    const gfx_shadow_cascade_t* m_cascade = nullptr;  // +0x1C
    float m_lod = 0.0f;                 // +0x20
    std::uint8_t m_clip_flag = 0;       // +0x24 also the mask a drawable must carry
    std::uint8_t m_skip_models = 0;     // +0x38 the draw components cast nothing
    float m_clip[ 4 ] = { };            // +0x28 the plane the two box tests always read; the walk's one more when +0x24 is set
};

// A VuMethod's call, vt+8.
struct gfx_draw_method_t
{
    virtual ~gfx_draw_method_t( ) = default;
    virtual void call( const void* params ) = 0;
};

// What the manager reads of a drawable (0x5A7A90, 0x5A7AF0, 0x5A78B0).
struct gfx_drawable_t
{
    gfx_draw_method_t* m_draw = nullptr;     // +0x10
    gfx_draw_method_t* m_shadow = nullptr;   // +0x14
    std::uint8_t m_mask = 0;                 // +0x1C
    dbrt_node_t* m_leaf = nullptr;           // +0x20
    float m_aabb[ 8 ] = { };                 // +0x24
};

// An entry of the list +0x18, vt+8.
struct gfx_draw_entry_t
{
    virtual ~gfx_draw_entry_t( ) = default;
    virtual void call( ) = 0;
};

struct gfx_draw_hooks_t
{
    virtual ~gfx_draw_hooks_t( ) = default;
    virtual void lights_submit( const gfx_draw_params_t& ) = 0;      // 0x562B10 on g 0x759094
    virtual bool composer_present( ) = 0;                            // g 0x7590BC non-null
    virtual void composer_draw( const gfx_draw_params_t& ) = 0;      // [g 0x7590BC +4] vt+0x20
    virtual void register_tick( ) = 0;                               // g 0x759008 vt+0x20 ( 0x5A79F0, "Final" )
    virtual void register_flush( ) = 0;                              // g 0x759024 vt+0x1C ( 0x5A7980 )
    virtual void unregister_tick( ) = 0;                             // g 0x759008 vt+0x28
    virtual void unregister_flush( ) = 0;                            // g 0x759024 vt+0x20
};

struct gfx_draw_manager_t
{
    // ctor 0x5A73B0: the list's capacity 8, the two registrations, the tree. Vtable and aligned alloc are the language's here.
    explicit gfx_draw_manager_t( gfx_draw_hooks_t& hooks )
        : m_hooks( hooks )
    {
        m_list.reserve( 8 );
        m_capacity = 8;
        m_hooks.register_tick( );
        m_hooks.register_flush( );
    }

    // dtor 0x5A7540.
    ~gfx_draw_manager_t( )
    {
        m_hooks.unregister_tick( );
        m_hooks.unregister_flush( );
    }

    gfx_draw_manager_t( const gfx_draw_manager_t& ) = delete;
    gfx_draw_manager_t& operator=( const gfx_draw_manager_t& ) = delete;

    // slot 10, 0x5A78B0.
    void add( gfx_drawable_t& d ) { d.m_leaf = m_tree.add( d.m_aabb, &d ); }

    // slot 11, 0x5A78F0.
    void remove( gfx_drawable_t& d )
    {
        m_tree.erase( d.m_leaf );
        d.m_leaf = nullptr;
    }

    // slot 12, 0x5A7930.
    void update( gfx_drawable_t& d ) { m_tree.move( d.m_leaf, d.m_aabb ); }

    // slot 6, 0x5A75E0: the lights of this view, the composer, then the walk 0x5A7B30 with the leaf 0x5A7A90 under the camera's six planes (seven with the clip plane).
    // The leaf counts every drawable it reaches, whether or not it draws.
    void draw( const gfx_draw_params_t& p )
    {
        m_hooks.lights_submit( p );
        if ( m_hooks.composer_present( ) )
            m_hooks.composer_draw( p );
        float planes[ 28 ];
        std::memcpy( planes, p.m_camera->m_planes, 24 * sizeof( float ) );
        int count = 6;
        if ( p.m_reflecting )
        {
            std::memcpy( planes + 24, p.m_clip, sizeof( p.m_clip ) );
            count = 7;
        }
        int reached = 0;
        dbrt_walk( m_tree.m_root, planes, count, [ & ]( void* data )
        {
            const gfx_drawable_t& d = *static_cast< const gfx_drawable_t* >( data );
            if ( ( d.m_mask & p.m_reflecting ) == p.m_reflecting && d.m_draw )
                d.m_draw->call( &p );     // retail's dev aabb follows here: header comment
            ++reached;
        } );
        m_drawn += reached;
        m_culled += m_tree.m_count - reached;
    }

    // slot 7, 0x5A7730: the shadow pass's planes (retail's buffer holds 13, unchecked) under the walk 0x5A7DB0, leaf 0x5A7AF0.
    void draw_shadow( const gfx_shadow_params_t& p )
    {
        float planes[ 52 ];
        int count = 0;
        if ( p.m_volume->m_count > 0 )
        {
            std::memcpy( planes, p.m_volume->m_planes, static_cast< std::size_t >( p.m_volume->m_count ) * 16 );
            count = p.m_volume->m_count;
        }
        if ( p.m_clip_flag )
        {
            std::memcpy( planes + 4 * count, p.m_clip, sizeof( p.m_clip ) );
            ++count;
        }
        int reached = 0;
        dbrt_walk( m_tree.m_root, planes, count, [ & ]( void* data )
        {
            const gfx_drawable_t& d = *static_cast< const gfx_drawable_t* >( data );
            if ( ( d.m_mask & p.m_clip_flag ) == p.m_clip_flag && d.m_shadow )
                d.m_shadow->call( &p );
            ++reached;
        } );
        m_shadow_drawn += reached;
        m_shadow_culled += m_tree.m_count - reached;
    }

    // slot 9, 0x5A7800: found -> swapped with the last, the count lowered. Retail then compares the new count with the capacity, growing to the larger of it and
    // capacity + capacity / 2: only a count already two past the capacity gets there.
    void list_remove( gfx_draw_entry_t* e )
    {
        const int count = static_cast< int >( m_list.size( ) );
        for ( int i = 0; i < count; ++i )
        {
            if ( m_list[ i ] != e )
                continue;
            if ( i + 1 < count )
                std::swap( m_list[ i ], m_list[ count - 1 ] );
            m_list.pop_back( );
            const int now = count - 1;
            if ( now > m_capacity )
            {
                int grow = m_capacity + m_capacity / 2;
                if ( now > grow )
                    grow = now;
                if ( grow > m_capacity )
                {
                    m_list.reserve( static_cast< std::size_t >( grow ) );
                    m_capacity = grow;
                }
            }
            return;
        }
    }

    // 0x5A7980, the render hook: with a non-empty list the sort's key is set (layer 1, bits 24, 25 and 23 clear, screen 8), each entry called, the list emptied.
    void flush( gfx_sort_t& sort )
    {
        if ( m_list.empty( ) )
            return;
        sort.set_layer( 1 );
        sort.m_key_hi &= 0xFCFFFFFFu;
        sort.m_key_hi &= 0xFF7FFFFFu;
        sort.set_screen( 8 );
        for ( gfx_draw_entry_t* e : m_list )
            e->call( );
        m_list.clear( );
    }

    // 0x5A79F0, tick "Final": the frame's counters zeroed (the dev page is read first in retail: header comment).
    void tick( )
    {
        m_drawn = m_culled = m_shadow_drawn = m_shadow_culled = 0;
    }

    gfx_draw_hooks_t& m_hooks;
    dbrt_tree_t m_tree;                       // +4
    int m_drawn = 0, m_culled = 0;            // +8 / +0xC
    int m_shadow_drawn = 0, m_shadow_culled = 0;   // +0x10 / +0x14
    std::vector< gfx_draw_entry_t* > m_list;  // +0x18 / +0x1C
    int m_capacity = 0;                       // +0x20
};

// g 0x7590B8.
inline gfx_draw_manager_t* g_gfx_draw_manager = nullptr;

// 0x5A7320.
inline gfx_draw_manager_t* gfx_draw_manager_create( gfx_draw_hooks_t& hooks )
{
    g_gfx_draw_manager = new gfx_draw_manager_t( hooks );
    return g_gfx_draw_manager;
}

// 0x5A7310: the global only (the object goes through slot 0).
inline void gfx_draw_manager_release( )
{
    g_gfx_draw_manager = nullptr;
}
