#pragma once

#include <workspace/core/assets/tree.hxx>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// VuJsonBinaryWriter, the VUJB writer: reader.hxx's other half, on value_t. Retail's writer object is two words, +0 the
// cursor and +4 the bytes left; every failed call leaves what it already wrote and answers false.

struct vujb_cursor_t
{
    std::uint8_t* at = nullptr;
    std::uint32_t left = 0;
};

// 0x55C100: a big-endian dword; false below 4 bytes left, nothing written.
inline bool vujb_write_word( vujb_cursor_t& w, std::uint32_t v )
{
    if ( w.left < 4 )
        return false;
    w.at[ 0 ] = static_cast< std::uint8_t >( v >> 24 );
    w.at[ 1 ] = static_cast< std::uint8_t >( v >> 16 );
    w.at[ 2 ] = static_cast< std::uint8_t >( v >> 8 );
    w.at[ 3 ] = static_cast< std::uint8_t >( v );
    w.at += 4;
    w.left -= 4;
    return true;
}

// 0x55C140: a big-endian qword; false below 8 bytes left.
inline bool vujb_write_wide( vujb_cursor_t& w, std::uint64_t v )
{
    if ( w.left < 8 )
        return false;
    for ( int k = 0; k < 8; ++k )
        w.at[ k ] = static_cast< std::uint8_t >( v >> ( 56 - 8 * k ) );
    w.at += 8;
    w.left -= 8;
    return true;
}

// 0x55C070: the length word (itself checked against 4), then the bytes. Short of room after the length: false with the
// length written.
inline bool vujb_write_text( vujb_cursor_t& w, const std::string& text )
{
    const std::int32_t length = static_cast< std::int32_t >( text.size( ) );
    if ( w.left < 4 )
        return false;
    vujb_write_word( w, static_cast< std::uint32_t >( length ) );
    if ( length > static_cast< std::int32_t >( w.left ) )
        return false;
    std::memcpy( w.at, text.data( ), static_cast< std::size_t >( length ) );
    w.at += length;
    w.left -= static_cast< std::uint32_t >( length );
    return true;
}

// A map's members in std::map order: the keys sorted by std::string's operator< (value_t keeps the order that built it).
inline std::vector< const std::pair< std::string, value_t >* > vujb_sorted( const value_t& value )
{
    std::vector< const std::pair< std::string, value_t >* > members;
    members.reserve( value.object.size( ) );
    for ( const auto& field : value.object )
        members.push_back( &field );
    std::sort( members.begin( ), members.end( ), []( const auto* a, const auto* b ) { return a->first < b->first; } );
    return members;
}

// 0x55BEA0: the tag word, then by tag 1 / 2 a word, 3 one byte, 4 a text, 5 the count and the items, 6 the count and per
// member (key order) the key's text and the value, 7 a qword; any other tag ends after its tag word, true.
inline bool vujb_write( vujb_cursor_t& w, const value_t& value )
{
    if ( !vujb_write_word( w, value.tag ) )
        return false;
    switch ( value.tag )
    {
    case 1:
        return vujb_write_word( w, static_cast< std::uint32_t >( value.integer ) );
    case 2:
    {
        std::uint32_t bits = 0;
        std::memcpy( &bits, &value.real, sizeof( bits ) );
        return vujb_write_word( w, bits );
    }
    case 3:
        if ( w.left == 0 )
            return false;
        *w.at++ = static_cast< std::uint8_t >( value.integer );
        --w.left;
        return true;
    case 4:
        return vujb_write_text( w, value.text );
    case 5:
    {
        if ( !vujb_write_word( w, static_cast< std::uint32_t >( value.list.size( ) ) ) )
            return false;
        for ( const value_t& item : value.list )
            if ( !vujb_write( w, item ) )
                return false;
        return true;
    }
    case 6:
    {
        const auto members = vujb_sorted( value );
        if ( !vujb_write_word( w, static_cast< std::uint32_t >( members.size( ) ) ) )
            return false;
        for ( const auto* field : members )
            if ( !vujb_write_text( w, field->first ) || !vujb_write( w, field->second ) )
                return false;
        return true;
    }
    case 7:
        return vujb_write_wide( w, static_cast< std::uint64_t >( value.int64 ) );
    default:
        return true;
    }
}

// 0x55BDA0: the bytes vujb_write will use.
inline std::uint32_t vujb_size( const value_t& value )
{
    switch ( value.tag )
    {
    case 1:
    case 2:
        return 8;
    case 3:
        return 5;
    case 4:
        return static_cast< std::uint32_t >( value.text.size( ) ) + 8;
    case 5:
    {
        std::uint32_t size = 8;
        for ( const value_t& item : value.list )
            size += vujb_size( item );
        return size;
    }
    case 6:
    {
        std::uint32_t size = 8;
        for ( const auto& field : value.object )
            size += static_cast< std::uint32_t >( field.first.size( ) ) + vujb_size( field.second ) + 4;
        return size;
    }
    case 7:
        return 12;
    default:
        return 4;
    }
}

// The document as 0x554CD0 and 0x4ACAB0 both build it: the magic, version 1 and the root. `used` = what the write took;
// false when it failed (the bytes it wrote stay in `out`).
inline bool vujb_document( const value_t& root, std::vector< std::uint8_t >& out, std::uint32_t& used )
{
    const std::uint32_t size = vujb_size( root ) + 8;
    out.assign( size, 0 );
    vujb_cursor_t w{ out.data( ), size };
    const bool ok = vujb_write_word( w, 0x56554A42u ) && vujb_write_word( w, 1 ) && vujb_write( w, root );
    used = size - w.left;
    return ok;
}

// 0x4ABB40: FNV-1a over the bytes, from `seed`.
inline std::uint32_t save_hash( const std::uint8_t* bytes, std::size_t size, std::uint32_t seed = 0x811C9DC5u )
{
    for ( std::size_t k = 0; k < size; ++k )
        seed = ( bytes[ k ] ^ seed ) * 0x1000193u;
    return seed;
}
