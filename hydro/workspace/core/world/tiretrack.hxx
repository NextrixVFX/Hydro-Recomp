#pragma once

#include <workspace/core/boat/surface.hxx>
#include <workspace/core/world/modes.hxx>

#include <cstdint>
#include <cstring>
#include <vector>

// VuTireTrackManager (g 0x7590F4; create 0x5C3D40, ctor 0x5C3E30, vtable 0x752200): the skid mark ribbons (world batch 32).
//
// THE MANAGER IS INERT IN THIS BINARY. g 0x7590F4 has exactly four references in the image: its zeroing 0x5C3D30, the two
// stores of 0x5C3D40 and the engine's init call at 0x566CD4 (vt+0x14 = slot 5). Nothing else can reach the object, nothing
// in the image takes a track from the free list +0x2C, so the active list +0x38 stays empty and the "Final" tick only
// runs its dev stats. SurfaceTableDB has no "TireTracks" member either (its keys: ImpactPfx, ImpactSfx, ScrapePfx, ScrapeSfx), so
// every skin stays absent. Recreated all the same (the CPU half): the pools, the per-surface skins, the tick.
//
// The init, slot 5 0x5C40C0 (thiscall manager -> true):
//   0x5C46D0 the pools; the tick {0x7521F0, this, 0x5C4940} registered in "Final" (g 0x759008 vt+0x20);
//   HOOKS, DEVICE (not recreated): the "TireTrack" shader +8 (0x428F60 on g 0x759004), the vertex declaration +4 (0x540E00:
//   the three elements {2, 0, 0}, {0xC0000, 1, 6, 0}, {0x140000, 7, 2, 0}), the pipeline +0xC (0x541200 / 0x541410 "tex0"
//   the texture, 0x53A440 on g 0x759074), the render batch +0x54 / its draw 0x5C4BF0, dev menu "TireTracks" (g 0x759020 vt+0x18);
//   SurfaceTableDB (0x4AE5A0 on g 0x759004): ["VuDBAsset"]["TireTracks"]["Texture"] the texture name (a string, else ""),
//   +0x20 = the surface count (g_vudynamicsimpl vt+0x4C), +0x24 = new 2 x count skins (the 0x2C ctor loop); per surface i
//   (vt+0x50 its name) skins[2i] from TireTracks.SlideOff[name], skins[2i + 1] from TireTracks.SlideOn[name] (0x5C5190).
//   The two lookups ["TireTrackTextures"] / ["SkidMarkTextures"] at 0x5C4530 .. are read and their answers dropped.
// The release, slot 3 0x5C4660, is all hooks (batch, tick, declaration, shader, pipeline) + the three frees.
// The dev stats 0x5C4B80 (the "Final" tick's tail): g 0x759020's page "TireTracks", printf only - dev, a hook not taken.

struct tire_segment_t             // 0x44 bytes
{
    tire_segment_t* next = nullptr;   // +0
    tire_segment_t* prev = nullptr;   // +4
    float position[ 3 ] = { };        // +8
    float alpha = 0.0f;               // +0x3C
};

struct tire_track_t               // 0x4C bytes
{
    tire_track_t* next = nullptr;     // +0
    tire_track_t* prev = nullptr;     // +4
    tire_segment_t* head = nullptr;   // +8
    tire_segment_t* tail = nullptr;   // +0xC
    std::int32_t count = 0;           // +0x10
    float width = 0.0f;               // +0x18
    float fade = 0.0f;                // +0x1C
    float aabb_min[ 3 ] = { };        // +0x28 (the tick's reset stores a stale stack word at +0x34: UNKNOWN, not kept)
    float aabb_max[ 3 ] = { };        // +0x38 (the same at +0x44)
    bool ending = false;              // +0x48
};

// 0x2C bytes; the words +0x18 and +0x28 (the colours' fourth) are never set meaningfully: 0x5C5190 stores a stale stack
// word there - UNKNOWN, not kept.
struct tire_skin_t
{
    bool present = false;                               // +0
    float tex0 = 0.0f;                                  // +4
    float tex1 = 1.0f;                                  // +8
    float ambient[ 3 ] = { 1.0f, 1.0f, 1.0f };          // +0xC
    float diffuse[ 3 ] = { 1.0f, 1.0f, 1.0f };          // +0x1C
};

struct tire_track_manager_t
{
    std::vector< tire_track_t > track_pool;     // +0x28, 0x40 of them (+0x10)
    std::vector< tire_segment_t > segment_pool; // +0x44, 0x400 of them (+0x14)
    tire_track_t* free_head = nullptr;          // +0x2C
    tire_track_t* free_tail = nullptr;          // +0x30
    std::int32_t free_count = 0;                // +0x34
    tire_track_t* active_head = nullptr;        // +0x38
    tire_track_t* active_tail = nullptr;        // +0x3C
    std::int32_t active_count = 0;              // +0x40
    tire_segment_t* free_segment_head = nullptr;// +0x48
    tire_segment_t* free_segment_tail = nullptr;// +0x4C
    std::int32_t free_segment_count = 0;        // +0x50
    std::int32_t surfaces = 0;                  // +0x20
    std::vector< tire_skin_t > skins;           // +0x24
};

// The free lists' push: at the tail, +0 next cleared, +4 the old tail.
template < typename node_t >
inline void tire_push( node_t* n, node_t*& head, node_t*& tail, std::int32_t& count )
{
    n->next = nullptr;
    n->prev = tail;
    if ( tail )
        tail->next = n;
    else
        head = n;
    ++count;
    tail = n;
}

// The pools 0x5C46D0, retail's sizes as arguments only for the check's small runs. The nodes are zeroed (retail's memset
// after the placement loop) and pushed in index order.
inline void tire_track_pools( tire_track_manager_t& m, std::size_t tracks = 0x40u, std::size_t segments = 0x400u )
{
    m.track_pool.assign( tracks, tire_track_t{ } );
    for ( tire_track_t& t : m.track_pool )
        tire_push( &t, m.free_head, m.free_tail, m.free_count );
    m.segment_pool.assign( segments, tire_segment_t{ } );
    for ( tire_segment_t& s : m.segment_pool )
        tire_push( &s, m.free_segment_head, m.free_segment_tail, m.free_segment_count );
}

// A segment's unlink from its track (the inlined list erase of 0x5C4A30 / 0x5C4870) and its return to the free list.
inline void tire_segment_free( tire_track_manager_t& m, tire_track_t& t, tire_segment_t* s )
{
    tire_segment_t* n = s->next;
    tire_segment_t* p = s->prev;
    if ( s == t.head )
        t.head = n;
    if ( s == t.tail )
        t.tail = p;
    if ( n )
        n->prev = p;
    if ( p )
        p->next = n;
    --t.count;
    tire_push( s, m.free_segment_head, m.free_segment_tail, m.free_segment_count );
}

// 0x5C4860 (thiscall manager, track): every segment from the tail back freed, the track unlinked from the active list and
// pushed on the free list.
inline void tire_track_free( tire_track_manager_t& m, tire_track_t& t )
{
    while ( t.tail )
        tire_segment_free( m, t, t.tail );
    tire_track_t* n = t.next;
    tire_track_t* p = t.prev;
    if ( &t == m.active_head )
        m.active_head = n;
    if ( &t == m.active_tail )
        m.active_tail = p;
    if ( n )
        n->prev = p;
    if ( p )
        p->next = n;
    --m.active_count;
    tire_push( &t, m.free_head, m.free_tail, m.free_count );
}

// The "Final" tick 0x5C4940 (thiscall manager, float dt; retn 4) past its dev stats 0x5C4B80. Per active track (the next
// read first): the bounds reset to +- FLT_MAX, half = width x 0.5 (0x6F765C); per segment head to tail:
// alpha -= fade x dt; when 0 >= alpha by comiss (a NaN stays): alpha = 0, and when the segment has a next or the track is
// ending, every segment before it is freed; the bounds take the position +- half (minss / maxss: the bound stays only when
// strictly past - a NaN takes the new value). A track whose tail then has no prev (one segment left) is freed. RETAIL READS
// [tail + 4] UNCHECKED: a track with no segments faults there; here it is left alone (unreachable: tracks are never made).
inline void tire_track_tick( tire_track_manager_t& m, float dt )
{
    static constexpr std::uint32_t flt_max = 0x7F7FFFFFu, flt_low = 0xFF7FFFFFu;
    tire_track_t* t = m.active_head;
    while ( t )
    {
        tire_track_t* following = t->next;
        for ( int k = 0; k < 3; ++k )
        {
            std::memcpy( &t->aabb_min[ k ], &flt_max, 4 );
            std::memcpy( &t->aabb_max[ k ], &flt_low, 4 );
        }
        const float half = t->width * 0.5f;
        for ( tire_segment_t* s = t->head; s; )
        {
            tire_segment_t* next = s->next;
            s->alpha -= t->fade * dt;
            if ( 0.0f >= s->alpha )
            {
                s->alpha = 0.0f;
                if ( next || t->ending )
                    while ( s->prev )
                        tire_segment_free( m, *t, s->prev );
            }
            for ( int k = 0; k < 3; ++k )
            {
                const float low = s->position[ k ] - half, high = s->position[ k ] + half;
                t->aabb_min[ k ] = t->aabb_min[ k ] < low ? t->aabb_min[ k ] : low;
                t->aabb_max[ k ] = t->aabb_max[ k ] > high ? t->aabb_max[ k ] : high;
            }
            s = next;
        }
        if ( t->tail && !t->tail->prev )
            tire_track_free( m, *t );
        t = following;
    }
}

// 0x5C5190 (thiscall skin, const VuJsonContainer&; retn 4): an object (tag 6) only: present, then TexX0 / TexX1 (0x4FBDF0;
// a number by tag 1 / 2 / 7, else left as it was), AmbientTint / DiffuseTint by 0x4FDEC0 (white when absent or refused),
// each channel x 0.00392156886 (0x6F75BC).
inline void tire_skin_load( tire_skin_t& s, const value_t& v )
{
    if ( v.tag != 6 )
        return;
    s.present = true;
    const value_t& x0 = modes_member( v, "TexX0" );
    if ( x0.tag == 1 || x0.tag == 2 || x0.tag == 7 )
        s.tex0 = modes_number( x0 );
    const value_t& x1 = modes_member( v, "TexX1" );
    if ( x1.tag == 1 || x1.tag == 2 || x1.tag == 7 )
        s.tex1 = modes_number( x1 );
    std::uint32_t ambient = 0xFFFFFFFFu, diffuse = 0xFFFFFFFFu;
    surface_color( modes_member( v, "AmbientTint" ), ambient );
    surface_color( modes_member( v, "DiffuseTint" ), diffuse );
    for ( int k = 0; k < 3; ++k )
    {
        s.ambient[ k ] = static_cast< float >( ( ambient >> ( 8 * k ) ) & 0xFFu ) * 0.00392156886f;
        s.diffuse[ k ] = static_cast< float >( ( diffuse >> ( 8 * k ) ) & 0xFFu ) * 0.00392156886f;
    }
}

// The init 0x5C40C0's CPU half (see the top). `doc` = SurfaceTableDB's document.
inline void tire_track_init( tire_track_manager_t& m, const surface_table_t& surfaces, const value_t& doc )
{
    tire_track_pools( m );
    const value_t& c = modes_member( modes_member( doc, "VuDBAsset" ), "TireTracks" );
    m.surfaces = surface_count( surfaces );
    m.skins.assign( static_cast< std::size_t >( m.surfaces ) * 2u, tire_skin_t{ } );
    for ( std::int32_t i = 0; i < m.surfaces; ++i )
    {
        const std::string* name = surface_name( surfaces, i );
        if ( !name )
            continue;
        tire_skin_load( m.skins[ 2 * i ], modes_member( modes_member( c, "SlideOff" ), name->c_str( ) ) );
        tire_skin_load( m.skins[ 2 * i + 1 ], modes_member( modes_member( c, "SlideOn" ), name->c_str( ) ) );
    }
}
