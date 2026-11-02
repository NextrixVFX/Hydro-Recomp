#pragma once

// VuBoatManager's keyboard callback 0x4AB380 (vtable 0x745330 slot 0; thiscall this = manager + 4, so the offsets here
// are boat_manager_t's less 4; key; retn 4), on retail's words (boat batch 29). A dev path: g 0x75906C's keyboard calls it
// (registered by slot 5 0x4AAD70); that a UWP key press reaches it is NOT traced, and nothing in the game calls this yet.
// count +0xC == 0: nothing. Key 0xD: the debug-camera byte +0x58 flipped (sete); set: the viewport boat's camera
// ( *( boat +0x224 ), 0x90 words ) into +0x5C; cleared: when the keyboard's byte +0x45 is set (the `cmp [+0x150], -1; ja`
// before it is never taken) the 16 words at +0x18C with +0x6C / +0x70 / +0x74 as the row 3 x y z go to
// setWorldTransform 0x5344A0 ( the boat's +0x50, matrix, 1 ) - a hook. Key 0x20: +0x54 + 1, byte +0x58 = 0, the index
// back to 0 at count (signed). Key 0xC: the byte +0x2B5 flipped.
#include <workspace/core/boat/nuisance.hxx>

#include <cstdint>
#include <functional>

using boat_manager_key_world_fn = std::function< void( std::uint32_t, const std::uint32_t*, std::uint32_t ) >;

inline void boat_manager_key( const vu_words_t& w, std::uint32_t self, std::uint32_t key, const boat_manager_key_world_fn& set_world )
{
    auto byte_put = [ & ]( std::uint32_t at, std::uint32_t v ) {
        const std::uint32_t word = at & ~3u;
        const std::uint32_t shift = ( at & 3u ) * 8u;
        w.put( word, ( w.get( word ) & ~( 0xFFu << shift ) ) | ( v << shift ) );
    };
    auto byte_get = [ & ]( std::uint32_t at ) { return ( w.get( at & ~3u ) >> ( ( at & 3u ) * 8u ) ) & 0xFFu; };
    const std::uint32_t count = w.get( self + 0xC );
    if ( count == 0 )
        return;
    if ( key == 0xD )
    {
        const std::uint32_t now = byte_get( self + 0x58 ) == 0 ? 1u : 0u;
        byte_put( self + 0x58, now );
        const std::uint32_t boat = w.get( w.get( self + 8 ) + 4u * w.get( self + 0x54 ) );
        if ( now )
        {
            const std::uint32_t camera = w.get( boat + 0x224 );
            for ( std::uint32_t k = 0; k < 0x90u; ++k )
                w.put( self + 0x5C + 4u * k, w.get( camera + 4u * k ) );
            return;
        }
        if ( byte_get( w.get( 0x75906Cu ) + 0x45 ) == 0 )
            return;
        std::uint32_t m[ 16 ];
        for ( std::uint32_t k = 0; k < 16u; ++k )
            m[ k ] = w.get( self + 0x18C + 4u * k );
        m[ 12 ] = w.get( self + 0x6C );
        m[ 13 ] = w.get( self + 0x70 );
        m[ 14 ] = w.get( self + 0x74 );
        if ( set_world )
            set_world( w.get( boat + 0x50 ), m, 1u );
    }
    else if ( key == 0x20 )
    {
        const std::uint32_t next = w.get( self + 0x54 ) + 1u;
        w.put( self + 0x54, next );
        byte_put( self + 0x58, 0u );
        if ( static_cast< std::int32_t >( next ) >= static_cast< std::int32_t >( count ) )
            w.put( self + 0x54, 0u );
    }
    else if ( key == 0xC )
    {
        byte_put( self + 0x2B5, byte_get( self + 0x2B5 ) == 0 ? 1u : 0u );
    }
}
