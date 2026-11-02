#pragma once

#include <workspace/core/world/pfxclass.hxx>
#include <workspace/core/world/spray.hxx>

#include <cstdint>
#include <cstring>
#include <map>
#include <utility>
#include <vector>
#include <xmmintrin.h>

// THE VuPfx RUNTIME ON RETAIL'S OWN MEMORY SHAPE. Retail keeps every system,
// pattern and process instance and every particle in four fixed pools owned
// by VuPfxResources (g_vupfximpl 0x75904C +8), linked by raw pointers. This
// file recreates that machine word for word over a 32-bit address space
// (pfx_heap_t: regions of words at retail-sized addresses, 0 = null), so a
// state can be compared with retail's memory directly.
//
// VuPfxResources (0x64 bytes, ctor 0x58B5B0):
//   +0x00 .. +0x0C the four blocks (systems, patterns, processes, particles);
//   +0x10 / +0x14 / +0x18 the free systems (first, last, count; links +4 next,
//   +8 previous); +0x1C .. +0x24 the patterns', +0x28 .. +0x30 the processes'
//   (the same links); +0x34 / +0x38 / +0x3C the free particles (links +0 next,
//   +4 previous); +0x40 .. +0x5C four (count, size) pairs - the ctor's 512 /
//   188, 1024 / 108, 4096 / 52, 16384 / 100, VuPfxImpl's init 0x51C840 passes
//   the same; +0x60 particles allocated since the last VuPfxImpl tick.
// A system instance: +0x0C its VuPfxSystem node, +0x10 .. +0x18 its patterns
// (first, last, count). A pattern instance: +0x0C its system instance, +0x10
// its node, +0x14 .. +0x1C its processes, +0x20 .. +0x28 its particles (first,
// last, count; links +0 / +4). A process instance: +0x0C its pattern, +0x10
// its node. A node: +0x0C its children (an MSVC std::map by name - here the
// caller's `children` list in the map's order), its params from +0x30.
struct pfx_heap_t
{
    struct region_t
    {
        std::uint32_t base = 0;
        std::vector< std::uint32_t > words;
    };
    std::vector< region_t > regions;

    std::uint32_t* find( std::uint32_t a )
    {
        for ( region_t& r : regions )
            if ( a >= r.base && a - r.base < 4u * r.words.size( ) )
                return &r.words[ ( a - r.base ) / 4u ];
        return nullptr;
    }
    std::uint32_t& operator[]( std::uint32_t a )
    {
        static std::uint32_t sink = 0;
        std::uint32_t* p = find( a );
        return p != nullptr ? *p : ( sink = 0, sink );
    }
    float f( std::uint32_t a )
    {
        float v;
        std::memcpy( &v, &( *this )[ a ], 4 );
        return v;
    }
    void set( std::uint32_t a, float v ) { std::memcpy( &( *this )[ a ], &v, 4 ); }
    void add( std::uint32_t base, std::uint32_t bytes )
    {
        region_t r;
        r.base = base;
        r.words.assign( ( bytes + 3u ) / 4u, 0u );
        regions.push_back( std::move( r ) );
    }
};

// What the runtime asks of its caller: the children of a node in map order,
// a pattern instance's tick (its vtable slot 1 - quad 0x591C30, geom 0x58FEA0,
// trail 0x58F0C0), with `user`.
struct pfx_sim_hooks_t
{
    void* user = nullptr;
    const std::vector< std::uint32_t >* ( *children )( void* user, std::uint32_t node ) = nullptr;
    void ( *pattern_tick )( void* user, pfx_heap_t& h, std::uint32_t pattern, float dt ) = nullptr;
};

// The node class a node's vtable names (g_pfx_classes), or null.
inline const pfx_class_t* pfx_node_class( pfx_heap_t& h, std::uint32_t node )
{
    const std::uint32_t vt = h[ node ];
    for ( const pfx_class_t& c : g_pfx_classes )
        if ( c.vtable == vt )
            return &c;
    return nullptr;
}

// The two list shapes. `list` addresses the (first, last, count) triple.
// Instances: next +4, previous +8; append at the last end (0x58B950 et al.),
// pop from the last end (0x58B880 et al.).
inline void pfx_list_append( pfx_heap_t& h, std::uint32_t list, std::uint32_t b )
{
    h[ b + 4 ] = 0u;
    const std::uint32_t last = h[ list + 4 ];
    if ( last != 0u )
    {
        h[ b + 8 ] = last;
        h[ last + 4 ] = b;
    }
    else
    {
        h[ b + 8 ] = 0u;
        h[ list ] = b;
    }
    ++h[ list + 8 ];
    h[ list + 4 ] = b;
}

inline std::uint32_t pfx_list_pop( pfx_heap_t& h, std::uint32_t list )
{
    const std::uint32_t b = h[ list + 4 ];
    if ( b == 0u )
        return 0u;
    const std::uint32_t prev = h[ b + 8 ];
    const std::uint32_t next = h[ b + 4 ];
    if ( b == h[ list ] )
        h[ list ] = next;
    h[ list + 4 ] = prev;
    if ( next != 0u )
        h[ next + 8 ] = prev;
    if ( prev != 0u )
        h[ prev + 4 ] = next;
    h[ b + 4 ] = 0u;
    h[ b + 8 ] = 0u;
    --h[ list + 8 ];
    return b;
}

// Particles: next +0, previous +4. The free pool pops and pushes at its +0x38
// end (0x58B8E0 / the retirements); a pattern's list takes new particles at
// its first end (0x58B570).
inline std::uint32_t pfx_particle_pop( pfx_heap_t& h, std::uint32_t r )
{
    const std::uint32_t p = h[ r + 0x38 ];
    if ( p == 0u )
        return 0u;
    const std::uint32_t next = h[ p ];
    const std::uint32_t prev = h[ p + 4 ];
    if ( p == h[ r + 0x34 ] )
        h[ r + 0x34 ] = next;
    h[ r + 0x38 ] = prev;
    if ( next != 0u )
        h[ next + 4 ] = prev;
    if ( prev != 0u )
        h[ prev ] = next;
    h[ p ] = 0u;
    h[ p + 4 ] = 0u;
    --h[ r + 0x3C ];
    return p;
}

inline void pfx_particle_free( pfx_heap_t& h, std::uint32_t r, std::uint32_t p )   // the push of 0x58B3F0 / 0x58ACC0
{
    h[ p ] = 0u;
    const std::uint32_t end = h[ r + 0x38 ];
    if ( end != 0u )
    {
        h[ p + 4 ] = end;
        h[ end ] = p;
    }
    else
    {
        h[ p + 4 ] = 0u;
        h[ r + 0x34 ] = p;
    }
    ++h[ r + 0x3C ];
    h[ r + 0x38 ] = p;
}

// The particle list's own unlink from its +4 (last) end, as 0x58B3F0 / 0x58ACC0
// empty a pattern: first +0x20, last +0x24, count +0x28.
inline std::uint32_t pfx_particle_unlink_last( pfx_heap_t& h, std::uint32_t pattern )
{
    const std::uint32_t p = h[ pattern + 0x24 ];
    if ( p == 0u )
        return 0u;
    const std::uint32_t next = h[ p ];
    const std::uint32_t prev = h[ p + 4 ];
    if ( p == h[ pattern + 0x20 ] )
        h[ pattern + 0x20 ] = next;
    h[ pattern + 0x24 ] = prev;
    if ( next != 0u )
        h[ next + 4 ] = prev;
    if ( prev != 0u )
        h[ prev ] = next;
    h[ p ] = 0u;
    h[ p + 4 ] = 0u;
    --h[ pattern + 0x28 ];
    return p;
}

// 0x58B5B0 VuPfxResources' ctor.
inline void pfx_resources_ctor( pfx_heap_t& h, std::uint32_t r )
{
    for ( int k = 0; k < 16; ++k )
        h[ r + 4 * k ] = 0u;
    const std::uint32_t d[ 8 ] = { 512u, 188u, 1024u, 108u, 4096u, 52u, 0x4000u, 100u };
    for ( int k = 0; k < 8; ++k )
        h[ r + 0x40 + 4 * k ] = d[ k ];
    h[ r + 0x60 ] = 0u;
}

// 0x58B670 VuPfxResources' init (VuPfxImpl slot 6 0x51D6B0): the (count, size)
// pairs to +0x40, the old blocks deleted and four new ones made by 0x58B9A0 -
// which first rounds every size up to a multiple of 16 in place, then calls
// operator new (the platform: `blocks` are their addresses, each count x the
// rounded size bytes, regions of `h`) -, each zeroed and every block appended to its free
// list in address order (particles: pushed at the +0x38 end).
inline void pfx_resources_init( pfx_heap_t& h, std::uint32_t r, const std::uint32_t* pairs, const std::uint32_t* blocks )
{
    for ( int k = 0; k < 8; ++k )
        h[ r + 0x40 + 4 * k ] = pairs[ k ];
    for ( int k = 0; k < 4; ++k )
    {
        h[ r + 0x44 + 8 * k ] = ( h[ r + 0x44 + 8 * k ] + 15u ) & 0xFFFFFFF0u;
        h[ r + 4 * k ] = blocks[ k ];
    }
    for ( int pool = 0; pool < 3; ++pool )
    {
        const std::uint32_t list = r + 0x10 + 0xC * pool;
        h[ list ] = h[ list + 4 ] = h[ list + 8 ] = 0u;
        const std::uint32_t count = h[ r + 0x40 + 8 * pool ], size = h[ r + 0x44 + 8 * pool ];
        for ( std::uint32_t o = 0; o < count * size; o += 4 )
            h[ blocks[ pool ] + o ] = 0u;
        for ( std::uint32_t i = 0; i < count; ++i )
            pfx_list_append( h, list, blocks[ pool ] + i * size );
    }
    h[ r + 0x34 ] = h[ r + 0x38 ] = h[ r + 0x3C ] = 0u;
    const std::uint32_t count = h[ r + 0x58 ], size = h[ r + 0x5C ];
    for ( std::uint32_t o = 0; o < count * size; o += 4 )
        h[ blocks[ 3 ] + o ] = 0u;
    for ( std::uint32_t i = 0; i < count; ++i )
        pfx_particle_free( h, r, blocks[ 3 ] + i * size );
}

// 0x58A830 VuPfxSystemInstance's ctor: the vtable 0x74FF8C, the pattern list 0,
// the matrix the static identity 0x7593B0, +0x5C .. +0x64, +0x6C .. +0x74 and
// +0x7C 0, the AABB +0x80 .. +0x98 0 but its pads +0x8C / +0x9C (stack words
// retail never writes: UNKNOWN, not written), +0xA0 / +0xA4 0, the scale +0xA8
// and the tint +0xAC .. +0xB8 1.0.
inline void pfx_system_ctor( pfx_heap_t& h, std::uint32_t s )
{
    h[ s ] = 0x74FF8Cu;
    h[ s + 0x10 ] = h[ s + 0x14 ] = h[ s + 0x18 ] = 0u;
    static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    for ( int k = 0; k < 16; ++k )
        h.set( s + 0x1C + 4 * k, identity[ k ] );
    for ( std::uint32_t o : { 0x5Cu, 0x60u, 0x64u, 0x6Cu, 0x70u, 0x74u, 0x7Cu, 0x80u, 0x84u, 0x88u, 0x90u, 0x94u, 0x98u, 0xA0u, 0xA4u } )
        h[ s + o ] = 0u;
    for ( std::uint32_t o = 0xA8u; o <= 0xB8u; o += 4u )
        h[ s + o ] = 0x3F800000u;
}

// 0x58B3F0 a pattern instance's release: every process back to the free
// processes (popped from its last end, appended), then every particle back to
// the free particles.
inline void pfx_pattern_release( pfx_heap_t& h, std::uint32_t r, std::uint32_t pattern )
{
    for ( std::uint32_t b; ( b = pfx_list_pop( h, pattern + 0x14 ) ) != 0u; )
        pfx_list_append( h, r + 0x28, b );
    for ( std::uint32_t p; ( p = pfx_particle_unlink_last( h, pattern ) ) != 0u; )
        pfx_particle_free( h, r, p );
}

// 0x58B2D0 a pattern instance's processes: per child node in map order, fail
// when its instance size (slot 6) exceeds the process block size (+0x54) or no
// block is free; else the node's create (slot 7) on a popped block, +0x10 the
// node, +0x0C the pattern, appended to the pattern's list.
inline bool pfx_pattern_create( pfx_heap_t& h, std::uint32_t r, std::uint32_t pattern, const pfx_sim_hooks_t& k )
{
    const std::vector< std::uint32_t >* kids = k.children( k.user, h[ pattern + 0x10 ] );
    for ( std::size_t i = 0; kids != nullptr && i < kids->size( ); ++i )
    {
        const std::uint32_t node = ( *kids )[ i ];
        const pfx_class_t* c = pfx_node_class( h, node );
        if ( c == nullptr || c->instance_size > h[ r + 0x54 ] )
            return false;
        const std::uint32_t b = pfx_list_pop( h, r + 0x28 );
        if ( b == 0u || h.find( b ) == nullptr )   // the second: an address outside the heap (never, on retail's own state)
            return false;
        pfx_create_class( c->create, h.find( b ) );
        h[ b + 0x10 ] = node;
        h[ b + 0x0C ] = pattern;
        pfx_list_append( h, pattern + 0x14, b );
    }
    return true;
}

// 0x58A940 a system instance's patterns: the same walk over the system node's
// children with the pattern pool (+0x1C, block size +0x4C); a pattern whose
// processes fail is released (0x58B3F0), its block appended to the free
// patterns, and the whole create answers false.
inline bool pfx_system_create( pfx_heap_t& h, std::uint32_t r, std::uint32_t s, const pfx_sim_hooks_t& k )
{
    const std::vector< std::uint32_t >* kids = k.children( k.user, h[ s + 0x0C ] );
    for ( std::size_t i = 0; kids != nullptr && i < kids->size( ); ++i )
    {
        const std::uint32_t node = ( *kids )[ i ];
        const pfx_class_t* c = pfx_node_class( h, node );
        if ( c == nullptr || c->instance_size > h[ r + 0x4C ] )
            return false;
        const std::uint32_t b = pfx_list_pop( h, r + 0x1C );
        if ( b == 0u || h.find( b ) == nullptr )
            return false;
        pfx_create_class( c->create, h.find( b ) );
        h[ b + 0x10 ] = node;
        h[ b + 0x0C ] = s;
        if ( !pfx_pattern_create( h, r, b, k ) )
        {
            pfx_pattern_release( h, r, b );
            pfx_list_append( h, r + 0x1C, b );
            return false;
        }
        pfx_list_append( h, s + 0x10, b );
    }
    return true;
}

// 0x58AA70 a system instance's patterns released (popped from the last end,
// 0x58B3F0 each, appended to the free patterns).
inline void pfx_system_release_patterns( pfx_heap_t& h, std::uint32_t r, std::uint32_t s )
{
    for ( std::uint32_t b; ( b = pfx_list_pop( h, s + 0x10 ) ) != 0u; )
    {
        pfx_pattern_release( h, r, b );
        pfx_list_append( h, r + 0x1C, b );
    }
}

// 0x58B880 a system instance from the free list (0 when none): popped, 0x58A830,
// +0x0C the node.
inline std::uint32_t pfx_system_alloc( pfx_heap_t& h, std::uint32_t r, std::uint32_t node )
{
    const std::uint32_t s = pfx_list_pop( h, r + 0x10 );
    if ( s == 0u )
        return 0u;
    pfx_system_ctor( h, s );
    h[ s + 0x0C ] = node;
    return s;
}

// 0x51DAE0 VuPfxImpl's releaseSystemInstance: 0x58AA70, then the instance back
// to the free systems with +0x0C = 0 (0x58B950's body, inlined).
inline void pfx_system_release( pfx_heap_t& h, std::uint32_t r, std::uint32_t s )
{
    pfx_system_release_patterns( h, r, s );
    pfx_list_append( h, r + 0x10, s );
    h[ s + 0x0C ] = 0u;
}

// 0x51DA70 VuPfxImpl's createSystemInstance after its path lookup 0x51DBD0 (a
// std::map walk by name - the caller's: `node` is what it found, 0 for none):
// 0 unless the node's getType chain holds VuPfxSystem (descriptor 0x736A10);
// then 0x58B880 and 0x58A940, a failed create released (0x58AA70 + 0x58B950).
inline std::uint32_t pfx_system_instance_create( pfx_heap_t& h, std::uint32_t r, std::uint32_t node, const pfx_sim_hooks_t& k )
{
    const pfx_class_t* c = node != 0u ? pfx_node_class( h, node ) : nullptr;
    if ( c == nullptr || std::strstr( c->chain, "VuPfxSystem" ) == nullptr )
        return 0u;
    const std::uint32_t s = pfx_system_alloc( h, r, node );
    if ( s == 0u )
        return 0u;
    if ( pfx_system_create( h, r, s, k ) )
        return s;
    pfx_system_release( h, r, s );
    return 0u;
}

// An instance's slot 0 (reset), by its node's class (pfxclass.hxx): the
// fountains' 0x55D020, the hull's 0x4CE750, the patterns' 0x58FE80 (slot 0 on
// every process, first to last) and the trail's 0x58F080 (the same, then
// +0x4C .. +0x54 0); the rest are nullsubs.
inline void pfx_instance_reset( pfx_heap_t& h, std::uint32_t b )
{
    const pfx_class_t* c = pfx_node_class( h, h[ b + 0x10 ] );
    if ( c == nullptr )
        return;
    switch ( c->reset )
    {
    case 0x55D020u: if ( h.find( b ) != nullptr ) pfx_fountain_reset( h.find( b ) ); break;
    case 0x4CE750u: if ( h.find( b ) != nullptr ) pfx_hull_reset( h.find( b ) ); break;
    case 0x58FE80u:
    case 0x58F080u:
        for ( std::uint32_t p = h[ b + 0x14 ]; p != 0u; p = h[ p + 4 ] )
            pfx_instance_reset( h, p );
        if ( c->reset == 0x58F080u )
            h[ b + 0x4C ] = h[ b + 0x50 ] = h[ b + 0x54 ] = 0u;
        break;
    default: break;
    }
}

// Slot 11 0x58AC90 start: state +0x7C = 1, the clock +0xA4 = 0, slot 0 on every
// pattern.
inline void pfx_system_start( pfx_heap_t& h, std::uint32_t s )
{
    h[ s + 0x7C ] = 1u;
    h[ s + 0xA4 ] = 0u;
    for ( std::uint32_t p = h[ s + 0x10 ]; p != 0u; p = h[ p + 4 ] )
        pfx_instance_reset( h, p );
}

// Slot 12 0x58ACC0 stop( immediate ): immediate -> state 0 and every pattern's
// particles freed (from each list's last end); else state 2.
inline void pfx_system_stop( pfx_heap_t& h, std::uint32_t r, std::uint32_t s, bool immediate )
{
    if ( !immediate )
    {
        h[ s + 0x7C ] = 2u;
        return;
    }
    h[ s + 0x7C ] = 0u;
    for ( std::uint32_t p = h[ s + 0x10 ]; p != 0u; p = h[ p + 4 ] )
        for ( std::uint32_t q; ( q = pfx_particle_unlink_last( h, p ) ) != 0u; )
            pfx_particle_free( h, r, q );
}

// Slot 13 0x58AD70 tick( dt ): the AABB reset (min +0x80 .. +0x88 FLT_MAX, max
// +0x90 .. +0x98 -FLT_MAX; pads +0x8C / +0x9C UNKNOWN), the count +0xA0 = 0;
// unless idle: clock += dt, then per pattern its tick (slot 1 - the hook) and,
// when its count +0x28 is not 0, min = minss( system, pattern +0x2C .. ), max =
// maxss( system, pattern +0x3C .. ), count += its count; state 2 with no
// particles -> 0; Duration (node +0x30) > 0 and state 1 and clock >= Duration
// -> 2 (comiss: NaN never).
inline void pfx_system_tick( pfx_heap_t& h, std::uint32_t s, float dt, const pfx_sim_hooks_t& k )
{
    for ( std::uint32_t o : { 0x80u, 0x84u, 0x88u } )
        h[ s + o ] = 0x7F7FFFFFu;
    for ( std::uint32_t o : { 0x90u, 0x94u, 0x98u } )
        h[ s + o ] = 0xFF7FFFFFu;
    h[ s + 0xA0 ] = 0u;
    if ( h[ s + 0x7C ] == 0u )
        return;
    h.set( s + 0xA4, h.f( s + 0xA4 ) + dt );
    for ( std::uint32_t p = h[ s + 0x10 ]; p != 0u; p = h[ p + 4 ] )
    {
        k.pattern_tick( k.user, h, p, dt );
        if ( h[ p + 0x28 ] == 0u )
            continue;
        for ( std::uint32_t c = 0; c < 3; ++c )
        {
            h.set( s + 0x80 + 4 * c, _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( h.f( s + 0x80 + 4 * c ) ), _mm_set_ss( h.f( p + 0x2C + 4 * c ) ) ) ) );
            h.set( s + 0x90 + 4 * c, _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( h.f( s + 0x90 + 4 * c ) ), _mm_set_ss( h.f( p + 0x3C + 4 * c ) ) ) ) );
        }
        h[ s + 0xA0 ] += h[ p + 0x28 ];
    }
    if ( h[ s + 0x7C ] == 2u && h[ s + 0xA0 ] == 0u )
        h[ s + 0x7C ] = 0u;
    const float duration = h.f( h[ s + 0x0C ] + 0x30 );
    if ( _mm_comigt_ss( _mm_set_ss( duration ), _mm_setzero_ps( ) ) && h[ s + 0x7C ] == 1u &&
         _mm_comige_ss( _mm_set_ss( h.f( s + 0xA4 ) ), _mm_set_ss( duration ) ) )
        h[ s + 0x7C ] = 2u;
}

// The pattern instance's slot 4 0x58B4E0 allocateParticle: 0 unless its system
// is emitting (state 1) and under the node's Max Particle Count (+0x30, 0 = no
// limit) by the pattern's count; then 0x58B8E0 - 0 when the node's particle
// size (slot 8) exceeds the particle block size (+0x5C) or none is free, else
// popped, the node's slot 9 init on it, +0x60 counted - and 0x58B570 puts it
// at the first end of the pattern's list.
inline std::uint32_t pfx_particle_obtain( pfx_heap_t& h, std::uint32_t r, std::uint32_t node );
inline void pfx_particle_push_first( pfx_heap_t& h, std::uint32_t pattern, std::uint32_t p );
inline std::uint32_t pfx_particle_allocate( pfx_heap_t& h, std::uint32_t r, std::uint32_t pattern )
{
    if ( h[ h[ pattern + 0x0C ] + 0x7C ] != 1u )
        return 0u;
    const std::uint32_t node = h[ pattern + 0x10 ];
    const std::uint32_t max = h[ node + 0x30 ];
    if ( max != 0u && static_cast< std::int32_t >( h[ pattern + 0x28 ] ) >= static_cast< std::int32_t >( max ) )
        return 0u;
    const std::uint32_t p = pfx_particle_obtain( h, r, node );
    if ( p == 0u )
        return 0u;
    pfx_particle_push_first( h, pattern, p );
    return p;
}

// 0x58B8E0( node ) on the resources: 0 when the node's particle size (slot 8)
// exceeds the particle block size (+0x5C) or none is free; else popped, the
// node's slot 9 init, +0x60 counted.
inline std::uint32_t pfx_particle_obtain( pfx_heap_t& h, std::uint32_t r, std::uint32_t node )
{
    const pfx_class_t* c = pfx_node_class( h, node );
    if ( c == nullptr || c->particle_size > h[ r + 0x5C ] )
        return 0u;
    const std::uint32_t p = pfx_particle_pop( h, r );
    if ( p == 0u || h.find( p ) == nullptr )
        return 0u;
    pfx_particle_class( c->particle, h.find( p ) );
    ++h[ r + 0x60 ];
    return p;
}

// 0x58B570: a particle onto a pattern's list at its first end.
inline void pfx_particle_push_first( pfx_heap_t& h, std::uint32_t pattern, std::uint32_t p )
{
    const std::uint32_t first = h[ pattern + 0x20 ];
    h[ p + 4 ] = 0u;
    if ( first != 0u )
    {
        h[ p ] = first;
        h[ first + 4 ] = p;
    }
    else
    {
        h[ p ] = 0u;
        h[ pattern + 0x24 ] = p;
    }
    ++h[ pattern + 0x28 ];
    h[ pattern + 0x20 ] = p;
}

// The pattern's slots 5 / 6 0x58B530 / 0x58B550: the spawn and draw matrices -
// node Space (+0x34) == 1: identity 0x7593B0 / the system matrix; else the
// other way round. Returned as an address; 0 stands for the static identity.
inline std::uint32_t pfx_pattern_spawn_matrix( pfx_heap_t& h, std::uint32_t pattern )
{
    return h[ h[ pattern + 0x10 ] + 0x34 ] == 1u ? 0u : h[ pattern + 0x0C ] + 0x1C;
}

inline std::uint32_t pfx_pattern_draw_matrix( pfx_heap_t& h, std::uint32_t pattern )
{
    return h[ h[ pattern + 0x10 ] + 0x34 ] == 1u ? h[ pattern + 0x0C ] + 0x1C : 0u;
}

// VuPfxImpl's tick 0x51DD80 ("Final" phase, registered by 0x51E390): the dev
// stats (g_vudevstat) and the debug draw (byte +0x1D) are hooks; the
// resources' +0x60 = 0.
inline void pfx_impl_tick( pfx_heap_t& h, std::uint32_t r )
{
    h[ r + 0x60 ] = 0u;
}

// ---------------------------------------------------------------------------
// THE PER-FRAME BODIES ON THE HEAP: a pattern instance's tick and its
// processes' slot 1, dispatched by vtable as retail's virtual calls are.
// What a frame needs from outside the pools: the viewports' camera positions
// (g_vuviewportmanager 0x759060: +4 count, +0x28 + 0x274 k), the global VuRand
// 0x736810, the static box 0x7593F0 (CRT initializer 0x64B690) a geom pattern
// without a model reads, and the count of paths reached that are not
// recreated.
//
// THE BOATS. The three boat processes hold a boat word the boat's own effect
// set-up writes after creating the system (a walk over the system's patterns'
// processes by node type - pfx_system_bind): the thrust fountain's +0x28 and
// the boat spring's +0x14 (0x41A640 / 0x43BDB0: the boat), the hull's +0x14
// (the boat) and +0x18 (its VuFluidsObject) (0x419FF0). The frame resolves a
// word to what the slot 1 bodies read (world/spray.hxx): 0 is retail's null
// boat; a word with no entry here counts as unrecreated (the process is not
// run).
//   thrust 0x4CD5B0: `view` (0x41D570's reads) and `records` (+0x22C -> +0xF8,
//     8-byte records), then the shared fountain emit with the quad fountain's
//     slot 2 0x55E100;
//   boat spring 0x4CD980: `position` (+0x50 -> +0x80 .. +0x88) and `velocity`
//     (+0x21C -> +0x140 .. +0x148);
//   hull 0x4CE760: `hull` (pfx_hull_boat_t) and the fluids entry's solver;
//     `prepare` = VuFluidSolver_prepare 0x524450 (boat/fluid.hxx fluid_prepare)
//     and `point` = VuWater's one-point query 0x5292D0 (world/query.hxx
//     water_point_query); a missing hook counts as unrecreated.
// THE WATER SNAP (a fountain whose params +0x90 byte is set): `water` = g
// 0x759058 is not null (false: retail's no-VuWater path, nothing happens) and
// `snap` = 0x529140 (world/query.hxx water_point_height) on the particle's +8
// .. +0x14, writing out +0x20 into the particle's +0x10; no hook (with water)
// or a false answer counts as unrecreated.
struct pfx_frame_boat_t
{
    std::uint32_t address = 0;
    boat_view_t* view = nullptr;
    const std::uint8_t* records = nullptr;
    float position[ 3 ]{};
    float velocity[ 3 ]{};
    const pfx_hull_boat_t* hull = nullptr;
};

struct pfx_frame_fluids_t
{
    std::uint32_t address = 0;
    fluid_solver_t* solver = nullptr;
};

struct pfx_frame_t
{
    const float* cameras = nullptr;   // x y z per viewport
    int camera_count = 0;
    pfx_rand_t* rand = nullptr;
    float geom_box[ 6 ]{};            // 0x7593F0: min x y z, max x y z
    int unrecreated = 0;
    const pfx_frame_boat_t* boats = nullptr;
    int boat_count = 0;
    const pfx_frame_fluids_t* fluids = nullptr;
    int fluids_count = 0;
    void* user = nullptr;
    void ( *prepare )( void* user, fluid_solver_t& solver, const float* matrix ) = nullptr;
    bool ( *point )( void* user, const float* point, float* out ) = nullptr;
    bool water = true;
    bool ( *snap )( void* user, const float* point, water_vertex_t& out ) = nullptr;
};

// A boat word to its entry: 0 = retail's null boat (`found` true, null
// returned); not listed = `found` false.
inline const pfx_frame_boat_t* pfx_frame_boat( const pfx_frame_t& f, std::uint32_t word, bool& found )
{
    found = true;
    if ( word == 0u )
        return nullptr;
    for ( int k = 0; k < f.boat_count; ++k )
        if ( f.boats[ k ].address == word )
            return &f.boats[ k ];
    found = false;
    return nullptr;
}

// The effect set-ups' bind walk (0x41A69B .. in 0x41A640, 0x41A08A .. in
// 0x419FF0, 0x43BFE8 .. in 0x43BDB0): for every pattern instance of the system
// (+0x10, next +4) and every process instance of it (+0x14, next +4) whose
// node's type (slot 1, then the descriptor chain) is `type`, word +`offset` =
// `value`. The callers: type "VuPfxEmitBoatThrustQuadFountain" +0x28 = the
// boat; "VuPfxEmitBoatHull" +0x18 = the fluids object, then +0x14 = the boat
// (0x419FF0 then starts the system: slot 11); "VuPfxBoatSpringConstraint"
// +0x14 = the boat. What surrounds each walk (the effect list, the creates,
// the matrices) is the boat's and not recreated here.
inline int pfx_system_bind( pfx_heap_t& h, std::uint32_t system, const char* type, std::uint32_t offset, std::uint32_t value )
{
    int bound = 0;
    const std::size_t n = std::strlen( type );
    for ( std::uint32_t p = h[ system + 0x10 ]; p != 0u; p = h[ p + 4 ] )
        for ( std::uint32_t b = h[ p + 0x14 ]; b != 0u; b = h[ b + 4 ] )
        {
            const pfx_class_t* c = pfx_node_class( h, h[ b + 0x10 ] );
            bool is = false;
            for ( const char* q = c != nullptr ? c->chain : ""; *q != '\0' && !is; )
            {
                const char* e = std::strchr( q, '|' );
                const std::size_t len = e != nullptr ? static_cast< std::size_t >( e - q ) : std::strlen( q );
                is = len == n && std::strncmp( q, type, n ) == 0;
                q += e != nullptr ? len + 1 : len;
            }
            if ( is )
            {
                h[ b + offset ] = value;
                ++bound;
            }
        }
    return bound;
}

inline void pfx_system_start( pfx_heap_t& h, std::uint32_t s );

// The three binds as their callers run them. Thrust (0x41A69B): the walk only.
// Hull (0x41A083): slot 16 (0x401DA0, the instance itself), per hull process
// +0x18 = the fluids and +0x14 = the boat (here two walks: nothing reads in
// between), then slot 11 start (`jmp [eax+0x2C]`). Spring (0x43BFD6): slot 11
// start on the entry's instance (+0x68), then the walk.
inline void pfx_bind_thrust( pfx_heap_t& h, std::uint32_t system, std::uint32_t boat )
{
    pfx_system_bind( h, system, "VuPfxEmitBoatThrustQuadFountain", 0x28, boat );
}

inline void pfx_bind_hull( pfx_heap_t& h, std::uint32_t system, std::uint32_t boat, std::uint32_t fluids )
{
    pfx_system_bind( h, system, "VuPfxEmitBoatHull", 0x18, fluids );
    pfx_system_bind( h, system, "VuPfxEmitBoatHull", 0x14, boat );
    pfx_system_start( h, system );
}

inline void pfx_bind_spring( pfx_heap_t& h, std::uint32_t system, std::uint32_t boat )
{
    pfx_system_start( h, system );
    pfx_system_bind( h, system, "VuPfxBoatSpringConstraint", 0x14, boat );
}

inline pfx_quad_particle_t& pfx_heap_particle( pfx_heap_t& h, std::uint32_t p )
{
    return *reinterpret_cast< pfx_quad_particle_t* >( h.find( p ) );   // a particle's words are contiguous in its block
}

inline pfx_tick_particle_t pfx_heap_tick_in( pfx_heap_t& h, std::uint32_t p )
{
    pfx_tick_particle_t t;
    t.velocity[ 0 ] = h.f( p + 0x18 );
    t.velocity[ 1 ] = h.f( p + 0x1C );
    t.velocity[ 2 ] = h.f( p + 0x20 );
    t.alpha = h.f( p + 0x34 );
    t.scale = h.f( p + 0x38 );
    t.age = h.f( p + 0x3C );
    t.world_scale_z = h.f( p + 0x4C );
    return t;
}

inline void pfx_heap_tick_out( pfx_heap_t& h, std::uint32_t p, const pfx_tick_particle_t& t )
{
    h.set( p + 0x18, t.velocity[ 0 ] );
    h.set( p + 0x1C, t.velocity[ 1 ] );
    h.set( p + 0x20, t.velocity[ 2 ] );
    h.set( p + 0x34, t.alpha );
    h.set( p + 0x38, t.scale );
    h.set( p + 0x3C, t.age );
    h.set( p + 0x4C, t.world_scale_z );
}

struct pfx_heap_emit_t
{
    pfx_heap_t* h;
    std::uint32_t r, pattern, process;
    pfx_rand_t* rand;
    float matrix[ 16 ];
    pfx_frame_t* frame;
};

// One process instance's slot 1 (dt). The fountains' 0x55D030 (world/pfx.hxx
// pfx_fountain_emit: allocation = the pattern's slot 4 pfx_particle_allocate,
// the matrix its slot 5, the init the instance's slot 2 - 0x55E100 / 0x55E9B0
// / 0x55E640 by the instance vtable); the tick processes walk the pattern's
// particles first to next with their per-particle bodies.
inline void pfx_process_tick( pfx_heap_t& h, std::uint32_t r, std::uint32_t b, float dt, pfx_frame_t& f )
{
    const std::uint32_t node = h[ b + 0x10 ];
    const std::uint32_t pattern = h[ b + 0x0C ];
    const std::uint32_t vt = h[ b ];
    float params[ 8 ];
    for ( int k = 0; k < 8; ++k )
        params[ k ] = h.f( node + 0x30 + 4 * k );
    auto each = [ & ]( auto&& body ) {
        for ( std::uint32_t p = h[ pattern + 0x20 ]; p != 0u; p = h[ p ] )
        {
            pfx_tick_particle_t t = pfx_heap_tick_in( h, p );
            body( t );
            pfx_heap_tick_out( h, p, t );
        }
    };
    switch ( vt )
    {
    case 0x746AF0u:   // the boat thrust quad fountain (0x4CD5B0: the rate, then the fountain)
    case 0x746AE0u:   // the quad fountain
    case 0x74EEE8u:   // the directional quad fountain
    case 0x74EF1Cu:   // the geom fountain
    {
        const std::uint8_t* bytes = reinterpret_cast< const std::uint8_t* >( h.find( node ) );
        if ( vt == 0x746AF0u )
        {
            bool found;
            const pfx_frame_boat_t* boat = pfx_frame_boat( f, h[ b + 0x28 ], found );
            if ( !found || ( boat != nullptr && ( boat->view == nullptr || boat->records == nullptr ) ) )
            {
                ++f.unrecreated;
                break;
            }
            pfx_fountain_node_t rate;
            rate.rate_scale = h.f( b + 0x20 );
            pfx_boat_thrust_rate( rate, bytes, boat != nullptr ? boat->view : nullptr, boat != nullptr ? boat->records : nullptr );
            h.set( b + 0x20, rate.rate_scale );
        }
        pfx_fountain_node_t n;
        n.alive = static_cast< int >( h[ b + 0x14 ] );
        n.accumulator = h.f( b + 0x18 );
        n.count_scale = h.f( b + 0x1C );
        n.rate_scale = h.f( b + 0x20 );
        n.alpha_scale = h.f( b + 0x24 );
        pfx_heap_emit_t e{ &h, r, pattern, b, f.rand, {}, &f };
        const std::uint32_t m = pfx_pattern_spawn_matrix( h, pattern );
        static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        for ( int k = 0; k < 16; ++k )
            e.matrix[ k ] = m == 0u ? identity[ k ] : h.f( m + 4 * k );
        pfx_fountain_hooks_t hooks;
        hooks.user = &e;
        hooks.allocate = []( void* u ) -> pfx_quad_particle_t* {
            pfx_heap_emit_t& x = *static_cast< pfx_heap_emit_t* >( u );
            const std::uint32_t p = pfx_particle_allocate( *x.h, x.r, x.pattern );
            return p != 0u ? &pfx_heap_particle( *x.h, p ) : nullptr;
        };
        hooks.matrix = []( void* u ) -> const float* { return static_cast< pfx_heap_emit_t* >( u )->matrix; };
        hooks.init = []( void* u, pfx_quad_particle_t& p ) {
            pfx_heap_emit_t& x = *static_cast< pfx_heap_emit_t* >( u );
            pfx_heap_t& hh = *x.h;
            const std::uint8_t* pb = reinterpret_cast< const std::uint8_t* >( hh.find( hh[ x.process + 0x10 ] ) );
            const std::uint32_t s = hh[ x.pattern + 0x0C ];
            const float velocity[ 3 ] = { hh.f( s + 0x5C ), hh.f( s + 0x60 ), hh.f( s + 0x64 ) };
            switch ( hh[ x.process ] )
            {
            case 0x74EEE8u: pfx_directional_fountain_init( *x.rand, pb, velocity, x.matrix, p ); break;
            case 0x74EF1Cu: pfx_geom_fountain_init( *x.rand, pb, x.matrix, p ); break;
            default: pfx_quad_fountain_init( *x.rand, pb, p ); break;   // the quad and the boat thrust fountains
            }
        };
        hooks.snap = []( void* u, pfx_quad_particle_t& p ) {
            pfx_frame_t& fr = *static_cast< pfx_heap_emit_t* >( u )->frame;
            if ( !fr.water )
                return;
            water_vertex_t out;
            std::memset( &out, 0, sizeof( out ) );
            if ( fr.snap == nullptr || !fr.snap( fr.user, reinterpret_cast< const float* >( &p.w[ 2 ] ), out ) )   // +8 .. +0x14
            {
                ++fr.unrecreated;
                return;
            }
            p.set( 0x10, out.height );
        };
        const std::uint32_t s = h[ pattern + 0x0C ];
        const float position[ 3 ] = { h.f( s + 0x4C ), h.f( s + 0x50 ), h.f( s + 0x54 ) };
        const float velocity[ 3 ] = { h.f( s + 0x5C ), h.f( s + 0x60 ), h.f( s + 0x64 ) };
        pfx_fountain_emit( n, bytes, *f.rand, f.cameras, f.camera_count, position, velocity, dt, hooks );
        h[ b + 0x14 ] = static_cast< std::uint32_t >( n.alive );
        h.set( b + 0x18, n.accumulator );
        break;
    }
    case 0x7501B0u: each( [ & ]( pfx_tick_particle_t& t ) { pfx_tick_dampen( t, params, dt ); } ); break;
    case 0x750240u: each( [ & ]( pfx_tick_particle_t& t ) { pfx_tick_accelerate( t, params, dt ); } ); break;
    case 0x7503E8u: each( [ & ]( pfx_tick_particle_t& t ) { pfx_tick_world_scale_z( t, params, dt ); } ); break;
    case 0x75043Cu: each( [ & ]( pfx_tick_particle_t& t ) { pfx_tick_scale( t, params, dt ); } ); break;
    case 0x7504BCu: each( [ & ]( pfx_tick_particle_t& t ) { pfx_tick_alpha_in_out( t, params, dt ); } ); break;
    case 0x750558u: each( [ & ]( pfx_tick_particle_t& t ) { pfx_tick_alpha( t, params, dt ); } ); break;
    case 0x750128u:   // the spring constraint: its offset at the instance's +0x14 .. +0x1C
    {
        const float offset[ 3 ] = { h.f( b + 0x14 ), h.f( b + 0x18 ), h.f( b + 0x1C ) };
        for ( std::uint32_t p = h[ pattern + 0x20 ]; p != 0u; p = h[ p ] )
        {
            float w[ 25 ];
            std::memcpy( w, h.find( p ), sizeof( w ) );
            pfx_tick_spring( w, params, offset, dt );
            std::memcpy( h.find( p ), w, sizeof( w ) );
        }
        break;
    }
    case 0x746AD4u:   // the boat spring constraint 0x4CD980: the boat's position and velocity, read once
    {
        bool found;
        const pfx_frame_boat_t* boat = pfx_frame_boat( f, h[ b + 0x14 ], found );
        if ( !found )
        {
            ++f.unrecreated;
            break;
        }
        for ( std::uint32_t p = h[ pattern + 0x20 ]; p != 0u; p = h[ p ] )
        {
            float w[ 25 ];
            std::memcpy( w, h.find( p ), sizeof( w ) );
            pfx_tick_boat_spring( w, params, boat != nullptr ? boat->position : nullptr, boat != nullptr ? boat->velocity : nullptr, dt );
            std::memcpy( h.find( p ), w, sizeof( w ) );
        }
        break;
    }
    case 0x746B90u:   // the boat hull 0x4CE760: +0x14 the boat, +0x18 the fluids, +0x1C the accumulator
    {
        bool found;
        const pfx_frame_boat_t* boat = pfx_frame_boat( f, h[ b + 0x14 ], found );
        fluid_solver_t* solver = nullptr;
        for ( int k = 0; k < f.fluids_count; ++k )
            if ( f.fluids[ k ].address == h[ b + 0x18 ] )
                solver = f.fluids[ k ].solver;
        // Retail returns before the fluids are touched without a boat or with dt
        // at most FLT_EPSILON; past that it needs them, the hull view and both hooks.
        const bool runs = boat != nullptr && _mm_comige_ss( _mm_set_ss( 1.1920929e-07f ), _mm_set_ss( dt ) ) == 0;
        if ( !found || ( runs && ( boat->hull == nullptr || solver == nullptr || solver->mesh == nullptr || f.prepare == nullptr || f.point == nullptr ) ) )
        {
            ++f.unrecreated;
            break;
        }
        struct hull_t
        {
            pfx_heap_t* h;
            std::uint32_t r, pattern;
            pfx_frame_t* f;
            fluid_solver_t* solver;
        } x{ &h, r, pattern, &f, solver };
        pfx_hull_hooks_t hooks;
        hooks.user = &x;
        hooks.prepare = []( void* u, const float* m ) {
            hull_t& y = *static_cast< hull_t* >( u );
            y.f->prepare( y.f->user, *y.solver, m );
        };
        hooks.point = []( void* u, const float* p, float* out ) {
            hull_t& y = *static_cast< hull_t* >( u );
            return y.f->point( y.f->user, p, out );
        };
        hooks.allocate = []( void* u ) -> pfx_quad_particle_t* {   // the pattern's slot 4 0x58B4E0
            hull_t& y = *static_cast< hull_t* >( u );
            const std::uint32_t p = pfx_particle_allocate( *y.h, y.r, y.pattern );
            return p != 0u ? &pfx_heap_particle( *y.h, p ) : nullptr;
        };
        float accumulator = h.f( b + 0x1C );
        static fluid_solver_t none;
        f.unrecreated += pfx_boat_hull_emit( accumulator, reinterpret_cast< const std::uint8_t* >( h.find( node ) ), runs ? boat->hull : nullptr,
            solver != nullptr ? *solver : none, *f.rand, f.cameras, f.camera_count, dt, hooks );
        h.set( b + 0x1C, accumulator );
        break;
    }
    default: ++f.unrecreated; break;
    }
}

// VuPfxTrailPatternInstance's slot 1 0x58F0C0 (instance vtable 0x7502C0; +0x4C
// the spawn accumulator, +0x50 the trail clock, +0x54 the newest particle).
// Node: +0x30 Max Particle Count (here the spawn rate's numerator), +0x34
// Space, +0x38 Start Delay, +0x40 Lifespan, +0x44 Fade In Time, +0x48 Fade Out
// Start Time, +0x4C .. +0x4F Color bytes, +0x50 Width, +0x54 .. +0x5C Linear
// Velocity, +0x64 Spawn Distance, +0x80 Tex Coord Rate. A trail particle: +8
// position (4 words), +0x18 velocity, +0x28 .. +0x34 colour (alpha +0x34),
// +0x38 width, +0x3C age, +0x40 lifespan, +0x44 .. +0x50 the spawn matrix's
// row 0, +0x54 the texture coordinate.
// Past the system clock's Start Delay: while emitting (state 1) the
// accumulator gains (Max Particle Count / Lifespan) x dt; when positive, +0x54
// = 0 and, any viewport within Spawn Distance (squared, strict) of the system
// position +0x4C, a particle by 0x58B8E0 (the pool's pop + slot 9 init) put at
// the list's first end (0x58B570) as +0x54; then the accumulator loses 1.0
// until it is not positive. Every particle integrates (position += velocity x
// dt, age += dt, alpha = min(clamp01(age / Fade In), clamp01((Lifespan - age)
// x 1 / fade)) x A, fade = max(Lifespan - Fade Out Start, 0), A = alpha byte /
// 255), the processes tick, the newest particle (still emitting; else +0x54 =
// 0) takes the spawn matrix's (pfx_pattern_spawn_matrix) row 3 as position,
// the velocity through its 3x3, the colour, width, lifespan, age = max(Lifespan
// - clock, 0), alpha as above but (Lifespan - age) / fade (a divss) and at least
// FLT_EPSILON, row 0, -(Tex Coord Rate x clock). With two or more particles the
// LAST one is retired when the one before it has alpha <= 0 (a NaN keeps it); with one,
// once no longer emitting, it is. The clock gains dt (reset to 0 unless
// emitting). The AABB: per particle its position through the draw matrix,
// widened by width x 0.5; none - all 0.
// UNKNOWN (stack words, never compared): particle +0x24 (velocity w), the AABB
// pads +0x38 / +0x48.
inline void pfx_heap_trail_tick( pfx_heap_t& h, std::uint32_t r, std::uint32_t pattern, float dt, pfx_frame_t& f )
{
    const std::uint32_t s = h[ pattern + 0x0C ];
    const std::uint32_t node = h[ pattern + 0x10 ];
    auto mn = []( float a, float b ) { return _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); };
    auto mx = []( float a, float b ) { return _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); };
    auto gt = []( float a, float b ) { return _mm_comigt_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) != 0; };
    const float k255 = 0.003921568859368563f;   // 0x3B808081
    if ( gt( h.f( s + 0xA4 ), h.f( node + 0x38 ) ) )
    {
        const float lifespan = h.f( node + 0x40 );
        const float alpha = static_cast< float >( static_cast< std::int32_t >( h[ node + 0x4C ] >> 24 ) ) * k255;
        const float fade = mx( lifespan - h.f( node + 0x48 ), 0.0f );
        if ( h[ s + 0x7C ] == 1u )
        {
            float acc = ( ( static_cast< float >( static_cast< std::int32_t >( h[ node + 0x30 ] ) ) / lifespan ) * dt ) + h.f( pattern + 0x4C );
            h.set( pattern + 0x4C, acc );
            if ( gt( acc, 0.0f ) )
            {
                bool in_range = false;
                const float d2 = h.f( node + 0x64 ) * h.f( node + 0x64 );
                for ( int c = 0; c < f.camera_count; ++c )
                {
                    const float dx = f.cameras[ 3 * c ] - h.f( s + 0x4C );
                    const float dy = f.cameras[ 3 * c + 1 ] - h.f( s + 0x50 );
                    const float dz = f.cameras[ 3 * c + 2 ] - h.f( s + 0x54 );
                    if ( gt( d2, ( ( dx * dx ) + ( dy * dy ) ) + ( dz * dz ) ) )
                        in_range = true;
                }
                h[ pattern + 0x54 ] = 0u;
                if ( in_range )
                {
                    const std::uint32_t p = pfx_particle_obtain( h, r, node );
                    if ( p != 0u )
                    {
                        pfx_particle_push_first( h, pattern, p );
                        h[ pattern + 0x54 ] = p;
                    }
                }
                acc = h.f( pattern + 0x4C );
                if ( gt( acc, 0.0f ) )
                {
                    do
                        acc = acc - 1.0f;
                    while ( gt( acc, 0.0f ) );
                    h.set( pattern + 0x4C, acc );
                }
            }
        }
        if ( h[ pattern + 0x20 ] != 0u )
        {
            const float inv = 1.0f / fade;
            for ( std::uint32_t n = h[ pattern + 0x20 ]; n != 0u; n = h[ n ] )
            {
                const float vy = h.f( n + 0x1C ) * dt, vz = h.f( n + 0x20 ) * dt;
                h.set( n + 0x08, h.f( n + 0x08 ) + ( h.f( n + 0x18 ) * dt ) );
                h.set( n + 0x0C, h.f( n + 0x0C ) + vy );
                h.set( n + 0x10, h.f( n + 0x10 ) + vz );
                const float age = h.f( n + 0x3C ) + dt;
                h.set( n + 0x3C, age );
                const float b1 = mx( 0.0f, mn( 1.0f, age / h.f( node + 0x44 ) ) );
                const float b2 = mx( 0.0f, mn( 1.0f, ( h.f( node + 0x40 ) - age ) * inv ) );
                h.set( n + 0x34, mn( b1, b2 ) * alpha );
            }
        }
        for ( std::uint32_t b = h[ pattern + 0x14 ]; b != 0u; b = h[ b + 4 ] )
            pfx_process_tick( h, r, b, dt, f );
        const std::uint32_t p = h[ pattern + 0x54 ];
        if ( p != 0u )
        {
            if ( h[ s + 0x7C ] == 1u )
            {
                static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
                float m[ 16 ];
                const std::uint32_t at = pfx_pattern_spawn_matrix( h, pattern );
                for ( int k = 0; k < 16; ++k )
                    m[ k ] = at != 0u ? h.f( at + 4 * k ) : identity[ k ];
                for ( int k = 0; k < 4; ++k )
                    h.set( p + 0x08 + 4 * k, m[ 12 + k ] );
                const float vx = h.f( node + 0x54 ), vy = h.f( node + 0x58 ), vz = h.f( node + 0x5C );
                h.set( p + 0x18, ( ( m[ 4 ] * vy ) + ( m[ 0 ] * vx ) ) + ( m[ 8 ] * vz ) );
                h.set( p + 0x1C, ( ( m[ 1 ] * vx ) + ( m[ 5 ] * vy ) ) + ( m[ 9 ] * vz ) );
                h.set( p + 0x20, ( ( m[ 2 ] * vx ) + ( m[ 6 ] * vy ) ) + ( m[ 10 ] * vz ) );
                for ( int k = 0; k < 4; ++k )
                    h.set( p + 0x28 + 4 * k, static_cast< float >( static_cast< std::int32_t >( ( h[ node + 0x4C ] >> ( 8 * k ) ) & 0xFFu ) ) * k255 );
                h[ p + 0x38 ] = h[ node + 0x50 ];
                h[ p + 0x40 ] = h[ node + 0x40 ];
                const float age = mx( h.f( node + 0x40 ) - h.f( pattern + 0x50 ), 0.0f );
                h.set( p + 0x3C, age );
                const float b1 = mx( 0.0f, mn( 1.0f, age / h.f( node + 0x44 ) ) );
                const float b2 = mx( 0.0f, mn( 1.0f, ( h.f( node + 0x40 ) - age ) / fade ) );
                h.set( p + 0x34, mx( mn( b1, b2 ) * alpha, 1.1920928955078125e-07f ) );
                for ( int k = 0; k < 4; ++k )
                    h.set( p + 0x44 + 4 * k, m[ k ] );
                const float tex = h.f( node + 0x80 ) * h.f( pattern + 0x50 );
                std::uint32_t bits;
                std::memcpy( &bits, &tex, 4 );
                h[ p + 0x54 ] = bits ^ 0x80000000u;   // xorps with the sign mask 0x6F7CD0
            }
            else
                h[ pattern + 0x54 ] = 0u;
        }
        const std::uint32_t count = h[ pattern + 0x28 ];
        if ( static_cast< std::int32_t >( count ) >= 2 )
        {
            const std::uint32_t last = h[ pattern + 0x24 ];
            const std::uint32_t prev = h[ last + 4 ];
            if ( _mm_comige_ss( _mm_setzero_ps( ), _mm_set_ss( h.f( prev + 0x34 ) ) ) )   // comiss 0, alpha; jb keeps (NaN kept)
            {
                pfx_particle_unlink_last( h, pattern );
                pfx_particle_free( h, r, last );
            }
        }
        else if ( h[ s + 0x7C ] != 1u && count == 1u )
        {
            pfx_particle_free( h, r, h[ pattern + 0x20 ] );
            h[ pattern + 0x20 ] = 0u;
            h[ pattern + 0x24 ] = 0u;
            h[ pattern + 0x28 ] = 0u;
            h[ pattern + 0x54 ] = 0u;
        }
        h.set( pattern + 0x50, dt + h.f( pattern + 0x50 ) );
        if ( h[ s + 0x7C ] != 1u )
            h[ pattern + 0x50 ] = 0u;
    }
    if ( h[ pattern + 0x28 ] == 0u )
    {
        for ( std::uint32_t o : { 0x2Cu, 0x30u, 0x34u, 0x3Cu, 0x40u, 0x44u } )
            h[ pattern + o ] = 0u;
        return;
    }
    const float big = 3.4028235e38f;
    float lo[ 3 ] = { big, big, big }, hi[ 3 ] = { -big, -big, -big };
    for ( int k = 0; k < 3; ++k )
    {
        h.set( pattern + 0x2C + 4 * k, lo[ k ] );
        h.set( pattern + 0x3C + 4 * k, hi[ k ] );
    }
    static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    float m[ 16 ];
    const std::uint32_t at = pfx_pattern_draw_matrix( h, pattern );
    for ( int k = 0; k < 16; ++k )
        m[ k ] = at != 0u ? h.f( at + 4 * k ) : identity[ k ];
    for ( std::uint32_t n = h[ pattern + 0x20 ]; n != 0u; n = h[ n ] )
    {
        const float px = h.f( n + 0x08 ), py = h.f( n + 0x0C ), pz = h.f( n + 0x10 );
        const float x = ( ( ( px * m[ 0 ] ) + ( py * m[ 4 ] ) ) + ( m[ 8 ] * pz ) ) + m[ 12 ];
        const float y = ( ( ( px * m[ 1 ] ) + ( py * m[ 5 ] ) ) + ( m[ 9 ] * pz ) ) + m[ 13 ];
        const float z = ( ( ( m[ 2 ] * px ) + ( m[ 6 ] * py ) ) + ( m[ 10 ] * pz ) ) + m[ 14 ];
        const float rr = h.f( n + 0x38 ) * 0.5f;
        lo[ 0 ] = mn( lo[ 0 ], x - rr );
        lo[ 1 ] = mn( lo[ 1 ], y - rr );
        lo[ 2 ] = mn( lo[ 2 ], z - rr );
        hi[ 0 ] = mx( hi[ 0 ], x + rr );
        hi[ 1 ] = mx( hi[ 1 ], y + rr );
        hi[ 2 ] = mx( hi[ 2 ], z + rr );
        for ( int k = 0; k < 3; ++k )
        {
            h.set( pattern + 0x2C + 4 * k, lo[ k ] );
            h.set( pattern + 0x3C + 4 * k, hi[ k ] );
        }
    }
}

// The quad / geom pattern instances' slot 1 (0x591C30 / 0x58FEA0; world/pfx.hxx
// pfx_pattern_tick, the same body on the heap): past the system clock's Start
// Delay (params +0x38) integrate every particle, run every process's slot 1
// first to last, retire the dead to the free particles; then the AABB.
inline void pfx_heap_pattern_tick( pfx_heap_t& h, std::uint32_t r, std::uint32_t pattern, float dt, pfx_frame_t& f )
{
    const std::uint32_t s = h[ pattern + 0x0C ];
    const std::uint32_t node = h[ pattern + 0x10 ];
    const std::uint32_t vt = h[ pattern ];
    if ( vt == 0x7502C0u )
    {
        pfx_heap_trail_tick( h, r, pattern, dt, f );
        return;
    }
    if ( vt != 0x7505D8u && vt != 0x750354u )
    {
        ++f.unrecreated;
        return;
    }
    const bool geom = vt == 0x750354u;
    if ( _mm_comigt_ss( _mm_set_ss( h.f( s + 0xA4 ) ), _mm_set_ss( h.f( node + 0x38 ) ) ) )
    {
        for ( std::uint32_t n = h[ pattern + 0x20 ]; n != 0u; n = h[ n ] )
        {
            const float vy = h.f( n + 0x1C ) * dt;
            const float vz = h.f( n + 0x20 ) * dt;
            h.set( n + 0x08, ( dt * h.f( n + 0x18 ) ) + h.f( n + 0x08 ) );
            h.set( n + 0x0C, h.f( n + 0x0C ) + vy );
            h.set( n + 0x10, h.f( n + 0x10 ) + vz );
            h.set( n + 0x3C, dt + h.f( n + 0x3C ) );
            if ( !geom )
                h.set( n + 0x44, ( h.f( n + 0x48 ) * dt ) + h.f( n + 0x44 ) );
            else
            {
                h.set( n + 0x44, ( dt * h.f( n + 0x54 ) ) + h.f( n + 0x44 ) );
                h.set( n + 0x48, ( h.f( n + 0x58 ) * dt ) + h.f( n + 0x48 ) );
                h.set( n + 0x4C, ( h.f( n + 0x5C ) * dt ) + h.f( n + 0x4C ) );
            }
        }
        for ( std::uint32_t b = h[ pattern + 0x14 ]; b != 0u; b = h[ b + 4 ] )
            pfx_process_tick( h, r, b, dt, f );
        auto below = []( float x ) { return !_mm_comige_ss( _mm_set_ss( x ), _mm_setzero_ps( ) ); };
        for ( std::uint32_t n = h[ pattern + 0x20 ]; n != 0u; )
        {
            const std::uint32_t next = h[ n ];
            const bool dead = below( h.f( n + 0x40 ) - h.f( n + 0x3C ) ) || below( h.f( n + 0x34 ) ) || below( h.f( n + 0x38 ) ) ||
                              ( !geom && below( h.f( n + 0x4C ) ) );
            if ( dead )
            {
                const std::uint32_t prev = h[ n + 4 ];
                if ( n == h[ pattern + 0x20 ] )
                    h[ pattern + 0x20 ] = next;
                if ( n == h[ pattern + 0x24 ] )
                    h[ pattern + 0x24 ] = prev;
                if ( next != 0u )
                    h[ next + 4 ] = prev;
                if ( prev != 0u )
                    h[ prev ] = next;
                --h[ pattern + 0x28 ];
                pfx_particle_free( h, r, n );
            }
            n = next;
        }
    }
    if ( h[ pattern + 0x28 ] == 0u )
    {
        for ( std::uint32_t o : { 0x2Cu, 0x30u, 0x34u, 0x3Cu, 0x40u, 0x44u } )
            h[ pattern + o ] = 0u;
        return;
    }
    static const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    float m[ 16 ];
    for ( int k = 0; k < 16; ++k )
        m[ k ] = h[ node + 0x34 ] == 1u ? h.f( s + 0x1C + 4 * k ) : identity[ k ];
    const float instance_scale = h.f( s + 0xA8 );
    const float big = 3.4028235e38f;
    float lo[ 3 ] = { big, big, big }, hi[ 3 ] = { -big, -big, -big };
    auto mn = []( float a, float b ) { return _mm_cvtss_f32( _mm_min_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); };
    auto mx = []( float a, float b ) { return _mm_cvtss_f32( _mm_max_ss( _mm_set_ss( a ), _mm_set_ss( b ) ) ); };
    float half_diagonal = 0.0f;
    if ( geom )
    {
        const std::uint32_t asset = h[ node + 0x6C ];
        float box[ 6 ];
        for ( int k = 0; k < 6; ++k )
            box[ k ] = asset != 0u ? h.f( asset + 0x5C + 4 * ( k < 3 ? k : k + 1 ) ) : f.geom_box[ k ];
        const float hy = ( box[ 4 ] - box[ 1 ] ) * 0.5f;
        const float hx = ( box[ 3 ] - box[ 0 ] ) * 0.5f;
        const float hz = ( box[ 5 ] - box[ 2 ] ) * 0.5f;
        half_diagonal = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( ( ( hy * hy ) + ( hx * hx ) ) + ( hz * hz ) ) ) );
    }
    for ( std::uint32_t n = h[ pattern + 0x20 ]; n != 0u; n = h[ n ] )
    {
        const float px = h.f( n + 0x08 ), py = h.f( n + 0x0C ), pz = h.f( n + 0x10 );
        const float x = ( ( ( m[ 0 ] * px ) + ( m[ 4 ] * py ) ) + ( m[ 8 ] * pz ) ) + m[ 12 ];
        const float y = ( ( ( m[ 1 ] * px ) + ( m[ 5 ] * py ) ) + ( m[ 9 ] * pz ) ) + m[ 13 ];
        const float z = ( ( ( m[ 2 ] * px ) + ( m[ 6 ] * py ) ) + ( pz * m[ 10 ] ) ) + m[ 14 ];
        const float rr = !geom ? ( instance_scale * 0.5f ) * h.f( n + 0x38 ) : ( h.f( n + 0x38 ) * instance_scale ) * half_diagonal;
        lo[ 0 ] = mn( lo[ 0 ], x - rr );
        lo[ 1 ] = mn( lo[ 1 ], y - rr );
        lo[ 2 ] = mn( lo[ 2 ], z - rr );
        hi[ 0 ] = mx( hi[ 0 ], x + rr );
        hi[ 1 ] = mx( hi[ 1 ], y + rr );
        hi[ 2 ] = mx( hi[ 2 ], z + rr );
    }
    for ( int k = 0; k < 3; ++k )
    {
        h.set( pattern + 0x2C + 4 * k, lo[ k ] );
        h.set( pattern + 0x3C + 4 * k, hi[ k ] );
    }
}
// ---------------------------------------------------------------------------
// THE EXPORT FOR THE WORLD AGENT (c_pfx is world-owned): one VuPfx runtime as
// the race holds it - VuPfxImpl's resources with retail's own pool sizes (the
// pairs 0x51C840 passes), a node area the effect loader fills, the global
// VuRand. Per frame, per live system instance: pfx_runtime_frame (retail's
// VuPfxSystemInstance slot 13, run by its owner's tick); once per frame after
// them all: pfx_impl_tick (VuPfxImpl's "Final" tick 0x51DD80). Draw data: the
// pattern instances on the instance's +0x10 list - pfx_geom_draw_matrix /
// pfx_geom_draw_colour per geom particle, the particle list itself for the
// quad / trail renderers (0x58BD60 / 0x58D030, GPU: hooks).
// The effect asset's node load and the path lookup are world/pfxload.hxx's
// (pfx_load_asset, pfx_find, pfx_runtime_create_path); the VuPfxManager pool
// is world/pfxmgr.hxx's. NOT RECREATED: the VuRand's seeding.
struct pfx_runtime_t
{
    pfx_heap_t heap;
    std::uint32_t resources = 0x00100000u;
    std::uint32_t nodes = 0x00200000u, next_node = 0x00200000u;
    std::map< std::uint32_t, std::vector< std::uint32_t > > children;
    pfx_rand_t rand;
    pfx_frame_t frame;
    pfx_sim_hooks_t hooks;
};

inline void pfx_runtime_init( pfx_runtime_t& rt, std::uint32_t node_bytes = 0x100000u )
{
    static const std::uint32_t pairs[ 8 ] = { 512u, 188u, 1024u, 108u, 4096u, 52u, 0x4000u, 100u };
    std::uint32_t blocks[ 4 ], at = 0x00400000u;
    rt.heap.regions.clear( );
    rt.heap.add( rt.resources, 0x64 );
    for ( int k = 0; k < 4; ++k )
    {
        const std::uint32_t bytes = pairs[ 2 * k ] * ( ( pairs[ 2 * k + 1 ] + 15u ) & 0xFFFFFFF0u );
        blocks[ k ] = at;
        rt.heap.add( at, bytes );
        at += ( bytes + 0xFFFFu ) & 0xFFFF0000u;
    }
    rt.heap.add( rt.nodes, node_bytes );
    rt.next_node = rt.nodes;
    pfx_resources_ctor( rt.heap, rt.resources );
    pfx_resources_init( rt.heap, rt.resources, pairs, blocks );
    rt.frame.rand = &rt.rand;
    rt.hooks.user = &rt;
    rt.hooks.children = []( void* u, std::uint32_t node ) -> const std::vector< std::uint32_t >* {
        auto& m = static_cast< pfx_runtime_t* >( u )->children;
        auto it = m.find( node );
        return it == m.end( ) ? nullptr : &it->second;
    };
    rt.hooks.pattern_tick = []( void* u, pfx_heap_t& h, std::uint32_t pattern, float dt ) {
        pfx_runtime_t& r = *static_cast< pfx_runtime_t* >( u );
        pfx_heap_pattern_tick( h, r.resources, pattern, dt, r.frame );
    };
}

// A node: its class's node vtable (g_pfx_classes .vtable), its params as
// (retail offset, word) pairs, its children in the order retail's std::map by
// child name would hold them. Returns its address (0x100 bytes each).
inline std::uint32_t pfx_runtime_node( pfx_runtime_t& rt, std::uint32_t vtable,
    const std::vector< std::pair< std::uint32_t, std::uint32_t > >& params, const std::vector< std::uint32_t >& kids )
{
    const std::uint32_t a = rt.next_node;
    rt.next_node += 0x100u;
    rt.heap[ a ] = vtable;
    for ( const auto& p : params )
        rt.heap[ a + p.first ] = p.second;
    rt.children[ a ] = kids;
    return a;
}

inline std::uint32_t pfx_runtime_create( pfx_runtime_t& rt, std::uint32_t system_node )
{
    return pfx_system_instance_create( rt.heap, rt.resources, system_node, rt.hooks );
}

// One instance's frame: its position / velocity (+0x4C / +0x5C, as its owner
// sets them through slots 0 / 3) and the viewports' cameras, then slot 13.
// Returns the unrecreated paths this frame reached (0 = all retail's).
inline int pfx_runtime_frame( pfx_runtime_t& rt, std::uint32_t instance, float dt, const float* cameras, int camera_count )
{
    rt.frame.cameras = cameras;
    rt.frame.camera_count = camera_count;
    rt.frame.unrecreated = 0;
    pfx_system_tick( rt.heap, instance, dt, rt.hooks );
    return rt.frame.unrecreated;
}

// ---------------------------------------------------------------------------
// THE INSTANCE CALLS - EXPORT for the boat agent (verified: case pfx_calls, every
// call through VuPfxSystemInstance's vtable 0x74FF8C on retail's memory with the
// real frames after it). What the boat's emitter lists (0x419700's helpers:
// 0x41ABE0 = slot 11 per record, 0x41AC50( list, immediate ) = slot 12,
// 0x41AB70( list, colour ) = slot 5, 0x41AAD0 = slot 3 with the body's linear
// velocity +0x140, 0x41A790 = slot 2 / slot 13 / slot 7 per record, 0x41ACC0 =
// slot 14, 0x41A0E0 = g_vupfximpl's release 0x51DAE0) and the hull's splash
// (0x416630: VuPfxManager createEffect 0x52BCD0 - pfxmgr.hxx pfx_manager_create /
// pfx_manager_lookup -, then the entry's instance +0x68 slot 10, slot 2, slot 3,
// slot 11) make. `instance` is a system instance address on h (rt.heap): what
// pfx_runtime_create / pfx_runtime_create_path / the manager entry's instance hold.
// Slot 2's euler is surface.hxx's water_world_euler (its CRT SUBSTITUTION); its
// fourth lane +0x78 is retail's stack word: UNKNOWN, left as it is.
inline void pfx_instance_set_matrix( pfx_heap_t& h, std::uint32_t instance, const float* m16 )   // slot 2 0x58AB00
{
    std::uint32_t m[ 16 ];
    std::memcpy( m, m16, 64 );
    pfx_system_set_matrix( h.find( instance ), m );
}

inline void pfx_instance_set_velocity( pfx_heap_t& h, std::uint32_t instance, const float* v4 )   // slot 3 0x58A650: +0x5C
{
    std::uint32_t v[ 4 ];
    std::memcpy( v, v4, 16 );
    pfx_system_set_words5c( h.find( instance ), v );
}

inline void pfx_instance_set_scale( pfx_heap_t& h, std::uint32_t instance, float s )   // slot 4 0x58A670: +0xA8
{
    h.set( instance + 0xA8, s );
}

inline void pfx_instance_set_colour( pfx_heap_t& h, std::uint32_t instance, const float* c4 )   // slot 5 0x58A690: +0xAC
{
    std::uint32_t c[ 4 ];
    std::memcpy( c, c4, 16 );
    pfx_system_set_wordsac( h.find( instance ), c );
}

// Slot 7 0x58A6D0: the AABB's address, this + 0x80 - min +0x80 .. +0x88, max
// +0x90 .. +0x98 (pads +0x8C / +0x9C UNKNOWN), as slot 13 left it.
inline const float* pfx_instance_aabb( pfx_heap_t& h, std::uint32_t instance )
{
    return reinterpret_cast< const float* >( h.find( instance + pfx_system_aabb_offset( ) ) );
}

inline float pfx_instance_duration( pfx_heap_t& h, std::uint32_t instance )   // slot 10 0x58A700: the node's Duration
{
    return h.f( h[ instance + 0x0C ] + 0x30 );
}

inline std::uint32_t pfx_instance_state( pfx_heap_t& h, std::uint32_t instance )   // slot 6 0x58A6C0: +0x7C
{
    return h[ instance + 0x7C ];
}

inline void pfx_instance_start( pfx_heap_t& h, std::uint32_t instance ) { pfx_system_start( h, instance ); }   // slot 11 0x58AC90

inline void pfx_instance_stop( pfx_heap_t& h, std::uint32_t resources, std::uint32_t instance, bool immediate )   // slot 12 0x58ACC0
{
    pfx_system_stop( h, resources, instance, immediate );
}

inline void pfx_instance_tick( pfx_heap_t& h, std::uint32_t instance, float dt, const pfx_sim_hooks_t& k )   // slot 13 0x58AD70
{
    pfx_system_tick( h, instance, dt, k );
}