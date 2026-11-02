#pragma once

// VuPlayAnimationEntity -> VuAnimatedPropEntity: the params the sender packs (0x5F5E80), what the prop's plug reads from them
// (0x5ECB60) and the fade its tick runs before the advance (0x5ED067 .. 0x5ED17B). Free of c_world so case prop_blend can reach it.
//
// SUBSTITUTION, FLAGGED: an asset parameter's word is retail's asset hash (+0x48, 0x5050B0); here it is a token, 1 + an index into
// c_world::m_play_tokens, 0 for none. 0x5052F0's lookup is that index.

#include <workspace/core/world/skeleton.hxx>

#include <cstdint>
#include <cstring>

struct prop_play_params_t
{
    std::uint32_t asset = 0;    // tag 10
    float start = 0.0f;         // tag 2: the sender's +0x90
    float blend = 0.0f;         // tag 2: +0x94
    float factor = 0.0f;        // tag 2: +0x98
    bool looping = false;       // tag 3: +0x9C
    std::uint32_t timed = 0;    // tag 10, absent with no timed-event asset
    float advance = 0.0f;       // tag 2: no sender packs it, so 0
};

// 0x5F5EF2 .. 0x5F5F59: 0x5050B0, 0x405120 x3, 0x4051A0, 0x5050B0 when +0xA4's asset is set. VuParams: bytes at +0, size at +0x80.
inline void prop_params_write( std::uint8_t ( &out )[ 0x84 ], const prop_play_params_t& p )
{
    std::memset( out, 0, sizeof( out ) );
    std::uint32_t size = 0;
    const auto word = [ & ]( std::uint32_t tag, const void* value )
    {
        std::memcpy( out + size, &tag, 4 );
        std::memcpy( out + size + 4, value, 4 );
        size += 8;
    };
    word( 10, &p.asset );
    word( 2, &p.start );
    word( 2, &p.blend );
    word( 2, &p.factor );
    const std::uint32_t bool_tag = 3;
    std::memcpy( out + size, &bool_tag, 4 );
    out[ size + 4 ] = p.looping ? 1 : 0;
    size += 5;
    if ( p.timed != 0 )
        word( 10, &p.timed );
    std::memcpy( out + 0x80, &size, 4 );
}

// 0x5ECBC0 .. 0x5ECDBC: a tag is peeked; the value is taken and the cursor moved only when it matches, so a parameter that does not
// fit leaves its field at the plug's zero and is offered to the next read.
inline prop_play_params_t prop_params_read( const std::uint8_t* params )
{
    prop_play_params_t p;
    std::uint32_t left = 0;
    std::memcpy( &left, params + 0x80, 4 );
    const std::uint8_t* at = params;
    const auto peek = [ & ]( std::uint32_t tag, void* out, std::uint32_t bytes )
    {
        std::uint32_t found = 0;
        if ( static_cast< std::int32_t >( left ) <= 0 )
            return;
        std::memcpy( &found, at, 4 );
        if ( found != tag )
            return;
        std::memcpy( out, at + 4, bytes );
        at += 4 + bytes;
        left -= 4 + bytes;
    };
    peek( 10, &p.asset, 4 );   // 0x5052F0, unconditional in retail: every sender packs it first
    peek( 2, &p.start, 4 );
    peek( 2, &p.blend, 4 );
    peek( 2, &p.factor, 4 );
    std::uint8_t looping = 0;
    peek( 3, &looping, 1 );
    p.looping = looping != 0;
    peek( 10, &p.timed, 4 );
    peek( 2, &p.advance, 4 );
    return p;
}

// VuAnimatedPropEntity +0x7E (a fade runs) and +0x80 (its rate, 1 / Blend Time).
struct prop_blend_t
{
    bool on = false;
    float rate = 0.0f;
};

// 0x5ED067 .. 0x5ED17B. The newest control ramps up, the others down; when it reaches 1 the fade ends. Then every control but the
// newest whose weight is <= 0 leaves all three lists (0x4AB7D0, the first match), newest first. Released, not freed: the owner frees.
inline void prop_blend_step( skel_animated_t& a, prop_blend_t& blend, float dt )
{
    if ( blend.on )
    {
        const float rate = blend.rate * dt;
        const int count = static_cast< int >( a.controls.size( ) );
        for ( int k = 0; k < count - 1; ++k )
        {
            skel_control_t& c = *a.controls[ static_cast< std::size_t >( k ) ];
            c.weight = skel_maxss( c.weight - rate, 0.0f );
        }
        if ( count > 0 )
        {
            skel_control_t& c = *a.controls[ static_cast< std::size_t >( count ) - 1u ];
            const float w = skel_minss( c.weight + rate, 1.0f );
            c.weight = w;
            if ( w >= 1.0f )
            {
                blend.on = false;
                blend.rate = 0.0f;
            }
        }
    }
    const auto drop = [ ]( std::vector< skel_control_t* >& list, skel_control_t* c )
    {
        for ( std::size_t k = 0; k < list.size( ); ++k )
            if ( list[ k ] == c )
            {
                list.erase( list.begin( ) + static_cast< std::ptrdiff_t >( k ) );
                return;
            }
    };
    for ( int k = static_cast< int >( a.controls.size( ) ) - 2; k >= 0; --k )
    {
        skel_control_t* c = a.controls[ static_cast< std::size_t >( k ) ];
        if ( !( 0.0f >= c->weight ) )
            continue;
        drop( a.controls, c );
        drop( a.blended, c );
        drop( a.additive, c );
    }
}
