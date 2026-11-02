#pragma once

#include <workspace/core/boat/hull.hxx>

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

// The attach components, recreated 1:1 from the disassembly (world batch 33, tools tmp/b33):
//   VuOffsetAttachComponent   (vtable 0x75182C): attach slot 12 0x5AF7A0, detach slot 13 0x5AF830, update slot 14 0x5AF900.
//   VuAnimatedAttachComponent (vtable 0x75829C): attach slot 12 0x61DEA0, detach slot 13 0x61DF50, update slot 14 0x61E010.
// A component holds a VuArray of records (+0x10 count, +0x14 capacity, +0x18 data for the offset one, 0x44 bytes; +0x14 / +0x18 /
// +0x1C, 0x48 bytes for the animated one, whose +0x10 is the VuAnimatedModelInstance): the relative matrix, the child's
// VuMotionComponent and (animated) its node. Grown by 0x5AFA30 / 0x61E300 (doubling, memory only: a std::vector here).
// A VuMotionComponent: +0x10 its owner (slot 0 the motion update, slot 1 attached, slot 2 detached), +0x14 the entity it is
// attached to, +0x18 the world matrix, +0x58 the linear velocity, +0x68 the angular.
// Matrices are retail's: 16 floats, row 3 the translation; products through hull_multiply = VuMatrix_multiply 0x4078C0.

struct attach_motion_t
{
    void* user = nullptr;
    void ( *updated )( void* user ) = nullptr;   // the owner's slot 0
    void ( *attached )( void* user ) = nullptr;  // slot 1
    void ( *detached )( void* user ) = nullptr;  // slot 2
    const void* parent = nullptr;                // +0x14
    float matrix[ 16 ]{ };                       // +0x18
    float linear[ 4 ]{ };                        // +0x58 (w: a stack word retail never writes, see attach_*_update)
    float angular[ 4 ]{ };                       // +0x68
};

struct attach_record_t
{
    float relative[ 16 ]{ };
    attach_motion_t* motion = nullptr;  // +0x40
    std::int32_t node = 0;              // +0x44 (animated only)
};

struct attach_component_t
{
    const void* entity = nullptr;           // +8
    std::vector< attach_record_t > records;
    const float* bones = nullptr;           // animated: the instance's +0x18 (16 floats per node)
};

// 0x5AF7A0 / the head of 0x61DEA0: a motion already attached (+0x14 set) answers false. Else +0x14 = the component's entity
// and the owner's slot 1 run first; the record appended after.
inline bool attach_offset_attach( attach_component_t& c, attach_motion_t& m, const float* relative )
{
    if ( m.parent != nullptr )
        return false;
    m.parent = c.entity;
    if ( m.attached )
        m.attached( m.user );
    attach_record_t r;
    std::memcpy( r.relative, relative, 64 );
    r.motion = &m;
    c.records.push_back( r );
    return true;
}

// 0x61DEA0: as above with findNodeByName 0x52F3C0 on the instance's skeleton (`node`: its answer); cmovg: <= 0 (not found) is node 0.
inline bool attach_animated_attach( attach_component_t& c, attach_motion_t& m, const float* relative, std::int32_t node )
{
    if ( !attach_offset_attach( c, m, relative ) )
        return false;
    c.records.back( ).node = node > 0 ? node : 0;
    return true;
}

// 0x5AF830 / 0x61DF50: every record of this motion swapped with the last and popped (the walk goes on at the NEXT index, so the
// record swapped in is not tested), +0x14 cleared, the owner's slot 2. An array holding a motion twice is not reachable (the
// attach refuses it) but the walk is kept as retail's.
inline void attach_detach( attach_component_t& c, attach_motion_t& m )
{
    for ( std::size_t i = 0; i < c.records.size( ); ++i )
    {
        if ( c.records[ i ].motion != &m )
            continue;
        std::swap( c.records[ i ], c.records.back( ) );
        c.records.pop_back( );
        m.parent = nullptr;
        if ( m.detached )
            m.detached( m.user );
    }
}

// 0x5AF900 (thiscall; world*, linear*, angular*; retn 0xC), per record in order: matrix = relative . world; linear = parent
// linear + the three terms below; angular = the parent's four words. The linear's w lane is a stack word the function never
// writes: UNKNOWN, left as it was here. Then the owner's slot 0.
inline void attach_offset_update( const attach_component_t& c, const float* world, const float* linear, const float* angular )
{
    for ( std::size_t i = 0; i < c.records.size( ); ++i )
    {
        const attach_record_t& r = c.records[ i ];
        attach_motion_t& m = *r.motion;
        hull_multiply( r.relative, m.matrix, world );
        const float x = r.relative[ 12 ], y = r.relative[ 13 ], z = r.relative[ 14 ];
        m.linear[ 0 ] = linear[ 0 ] + ( z * angular[ 1 ] - y * angular[ 2 ] );
        m.linear[ 1 ] = linear[ 1 ] + ( x * angular[ 2 ] - z * angular[ 0 ] );
        m.linear[ 2 ] = linear[ 2 ] + ( y * angular[ 0 ] - x * angular[ 1 ] );
        std::memcpy( m.angular, angular, 16 );
        if ( m.updated )
            m.updated( m.user );
    }
}

// 0x61E010 (thiscall; world*, linear*, angular*; retn 0xC), per record in order, with n = the node's matrix (+0x18 + 64 x node):
//   R = relative . n, matrix = R . world.
//   a = ( z*0 - y*0, x*0 - z*0, y*0 - x*0 ) of the relative's translation: the node's angular term, retail's literal zero vector
//   (a NaN / infinite translation shows through it, so it is kept).
//   b = angular x R's translation; c = a through R's basis.
//   linear = ( parent linear + c ) + b; angular = parent + ( sum of R's column ) x 0. Both w lanes: stack words never written
//   (UNKNOWN, left as they were here). Then the owner's slot 0.
inline void attach_animated_update( const attach_component_t& c, const float* world, const float* linear, const float* angular )
{
    for ( std::size_t i = 0; i < c.records.size( ); ++i )
    {
        const attach_record_t& r = c.records[ i ];
        attach_motion_t& m = *r.motion;
        float R[ 16 ];
        hull_multiply( r.relative, R, c.bones + 16 * static_cast< std::size_t >( r.node ) );
        const float zero = 0.0f;
        const float z0 = r.relative[ 14 ] * zero, y0 = r.relative[ 13 ] * zero, x0 = r.relative[ 12 ] * zero;
        const float a0 = z0 - y0, a1 = x0 - z0, a2 = y0 - x0;
        hull_multiply( R, m.matrix, world );
        const float b0 = angular[ 1 ] * R[ 14 ] - angular[ 2 ] * R[ 13 ];
        const float b1 = angular[ 2 ] * R[ 12 ] - angular[ 0 ] * R[ 14 ];
        const float b2 = angular[ 0 ] * R[ 13 ] - angular[ 1 ] * R[ 12 ];
        const float c0 = ( a1 * R[ 4 ] + a0 * R[ 0 ] ) + a2 * R[ 8 ];
        const float c1 = ( a1 * R[ 5 ] + a0 * R[ 1 ] ) + a2 * R[ 9 ];
        const float c2 = ( a1 * R[ 6 ] + a0 * R[ 2 ] ) + a2 * R[ 10 ];
        const float d0 = ( ( R[ 4 ] + R[ 0 ] ) + R[ 8 ] ) * zero;
        const float d1 = ( ( R[ 5 ] + R[ 1 ] ) + R[ 9 ] ) * zero;
        const float d2 = ( ( R[ 6 ] + R[ 2 ] ) + R[ 10 ] ) * zero;
        m.linear[ 0 ] = ( linear[ 0 ] + c0 ) + b0;
        m.linear[ 1 ] = ( linear[ 1 ] + c1 ) + b1;
        m.linear[ 2 ] = ( linear[ 2 ] + c2 ) + b2;
        m.angular[ 0 ] = angular[ 0 ] + d0;
        m.angular[ 1 ] = angular[ 1 ] + d1;
        m.angular[ 2 ] = angular[ 2 ] + d2;
        if ( m.updated )
            m.updated( m.user );
    }
}
