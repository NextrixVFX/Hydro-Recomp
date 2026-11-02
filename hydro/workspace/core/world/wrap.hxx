#pragma once

#include <cstdint>

// The font's word wrap (ATG's XMB word-wrap shape), recreated 1:1 from the disassembly (water batch 33): the line finder 0x5A88F0 and its helpers 0x5A8600 0x5A8660 0x5A86C0 0x5A8760 0x5A8870 0x5A88B0.
// HOOK: the character width 0x537EF0 (the font's glyph lookup, a callback here). `flags` is the static 0x7359FC (bit 0 the kinsoku table, bit 2 Hangul breaks by word), initial value 1, written by 0x53D274 / 0x53D2BA.
// A position is an index into the text; -1 is retail's null. The text must hold a word past its terminator: 0x5A8760 reads p[1] unguarded, as retail does.
// Not wired: the caller 0x537DB0 is the font's wrap, no caller in the game yet.

struct wrap_kinsoku_t
{
    std::uint16_t c;
    std::uint8_t no_begin;   // +2
    std::uint8_t no_end;     // +3
};

// 0x735C90: 0x91 entries sorted by character, then the word after the table: retail's search starts with high = 0x91, one past the end, so it reads the neighbouring static as entry 0x91. Kept as read.
inline constexpr wrap_kinsoku_t g_wrap_kinsoku[ 0x92 ] = {
    { 0x0021u, 1u, 0u }, { 0x0024u, 0u, 1u }, { 0x0025u, 1u, 0u }, { 0x0027u, 1u, 1u }, { 0x0028u, 0u, 1u }, { 0x0029u, 1u, 0u },
    { 0x002Cu, 1u, 0u }, { 0x002Eu, 1u, 0u }, { 0x002Fu, 1u, 1u }, { 0x003Au, 1u, 0u }, { 0x003Bu, 1u, 0u }, { 0x003Fu, 1u, 0u },
    { 0x005Bu, 0u, 1u }, { 0x005Cu, 0u, 1u }, { 0x005Du, 1u, 0u }, { 0x007Bu, 0u, 1u }, { 0x007Du, 1u, 0u }, { 0x00A2u, 1u, 0u },
    { 0x00A3u, 0u, 1u }, { 0x00A5u, 0u, 1u }, { 0x00A7u, 0u, 1u }, { 0x00A8u, 1u, 0u }, { 0x00A9u, 1u, 0u }, { 0x00AEu, 1u, 0u },
    { 0x00B0u, 1u, 0u }, { 0x00B7u, 1u, 1u }, { 0x02C7u, 1u, 0u }, { 0x02C9u, 1u, 0u }, { 0x2013u, 1u, 0u }, { 0x2014u, 1u, 0u },
    { 0x2015u, 1u, 0u }, { 0x2016u, 1u, 0u }, { 0x2018u, 0u, 1u }, { 0x2019u, 1u, 0u }, { 0x201Cu, 0u, 1u }, { 0x201Du, 1u, 0u },
    { 0x2022u, 1u, 0u }, { 0x2025u, 1u, 0u }, { 0x2026u, 1u, 0u }, { 0x2027u, 1u, 0u }, { 0x2032u, 1u, 0u }, { 0x2033u, 1u, 0u },
    { 0x2035u, 0u, 1u }, { 0x2103u, 1u, 0u }, { 0x2122u, 1u, 0u }, { 0x2236u, 1u, 0u }, { 0x2574u, 1u, 0u }, { 0x266Fu, 0u, 1u },
    { 0x3001u, 1u, 0u }, { 0x3002u, 1u, 0u }, { 0x3003u, 1u, 0u }, { 0x3005u, 1u, 0u }, { 0x3008u, 0u, 1u }, { 0x3009u, 1u, 0u },
    { 0x300Au, 0u, 1u }, { 0x300Bu, 1u, 0u }, { 0x300Cu, 0u, 1u }, { 0x300Du, 1u, 0u }, { 0x300Eu, 0u, 1u }, { 0x300Fu, 1u, 0u },
    { 0x3010u, 0u, 1u }, { 0x3011u, 1u, 0u }, { 0x3012u, 0u, 1u }, { 0x3014u, 0u, 1u }, { 0x3015u, 1u, 0u }, { 0x3016u, 0u, 1u },
    { 0x3017u, 1u, 0u }, { 0x301Du, 0u, 1u }, { 0x301Eu, 1u, 0u }, { 0x301Fu, 1u, 0u }, { 0x3041u, 1u, 0u }, { 0x3043u, 1u, 0u },
    { 0x3045u, 1u, 0u }, { 0x3047u, 1u, 0u }, { 0x3049u, 1u, 0u }, { 0x3063u, 1u, 0u }, { 0x3083u, 1u, 0u }, { 0x3085u, 1u, 0u },
    { 0x3087u, 1u, 0u }, { 0x308Eu, 1u, 0u }, { 0x3099u, 1u, 0u }, { 0x309Au, 1u, 0u }, { 0x309Bu, 1u, 0u }, { 0x309Cu, 1u, 0u },
    { 0x309Du, 1u, 0u }, { 0x309Eu, 1u, 0u }, { 0x30A1u, 1u, 0u }, { 0x30A3u, 1u, 0u }, { 0x30A5u, 1u, 0u }, { 0x30A7u, 1u, 0u },
    { 0x30A9u, 1u, 0u }, { 0x30C3u, 1u, 0u }, { 0x30E3u, 1u, 0u }, { 0x30E5u, 1u, 0u }, { 0x30E7u, 1u, 0u }, { 0x30EEu, 1u, 0u },
    { 0x30F5u, 1u, 0u }, { 0x30F6u, 1u, 0u }, { 0x30FBu, 1u, 0u }, { 0x30FCu, 1u, 0u }, { 0x30FDu, 1u, 0u }, { 0x30FEu, 1u, 0u },
    { 0xFE30u, 1u, 0u }, { 0xFE50u, 1u, 0u }, { 0xFE51u, 1u, 0u }, { 0xFE52u, 1u, 0u }, { 0xFE54u, 1u, 0u }, { 0xFE55u, 1u, 0u },
    { 0xFE56u, 1u, 0u }, { 0xFE57u, 1u, 0u }, { 0xFE59u, 0u, 1u }, { 0xFE5Au, 1u, 0u }, { 0xFE5Bu, 0u, 1u }, { 0xFE5Cu, 1u, 0u },
    { 0xFE5Du, 0u, 1u }, { 0xFE5Eu, 1u, 0u }, { 0xFF01u, 1u, 0u }, { 0xFF02u, 1u, 0u }, { 0xFF04u, 0u, 1u }, { 0xFF05u, 1u, 0u },
    { 0xFF07u, 1u, 0u }, { 0xFF08u, 0u, 1u }, { 0xFF09u, 1u, 0u }, { 0xFF0Cu, 1u, 0u }, { 0xFF0Eu, 1u, 0u }, { 0xFF1Au, 1u, 0u },
    { 0xFF1Bu, 1u, 0u }, { 0xFF1Fu, 1u, 0u }, { 0xFF20u, 0u, 1u }, { 0xFF3Bu, 0u, 1u }, { 0xFF3Du, 1u, 0u }, { 0xFF40u, 1u, 0u },
    { 0xFF5Bu, 0u, 1u }, { 0xFF5Cu, 1u, 0u }, { 0xFF5Du, 1u, 0u }, { 0xFF5Eu, 1u, 0u }, { 0xFF61u, 1u, 0u }, { 0xFF64u, 1u, 0u },
    { 0xFF70u, 1u, 0u }, { 0xFF9Eu, 1u, 0u }, { 0xFF9Fu, 1u, 0u }, { 0xFFE0u, 1u, 1u }, { 0xFFE1u, 0u, 1u }, { 0xFFE5u, 0u, 1u },
    { 0xFFE6u, 0u, 1u }, { 0x9CF8u, 109u, 0u },
};

// 0x5A8600 (offset 2) / 0x5A8660 (offset 3): a binary search over the table, 0 when flag bit 0 is clear or the character is absent.
inline std::uint8_t wrap_lookup( std::uint16_t c, std::uint32_t flags, bool end )
{
    if ( !( flags & 1u ) )
        return 0;
    int low = 0, high = 0x91;
    while ( low <= high )
    {
        const int middle = low + ( high - low ) / 2;
        const std::uint16_t at = g_wrap_kinsoku[ middle ].c;
        if ( at == c )
            return end ? g_wrap_kinsoku[ middle ].no_end : g_wrap_kinsoku[ middle ].no_begin;
        if ( c < at )
            high = middle - 1;
        else
            low = middle + 1;
    }
    return 0;
}

// 0x5A86C0: a character a line may break beside. With flag bit 2 Hangul and its jamo are not.
inline bool wrap_breakable( std::uint16_t c, std::uint32_t flags )
{
    const auto within = []( std::uint16_t v, std::uint32_t a, std::uint32_t b ) { return a <= v && v <= b; };
    if ( flags & 4u )
    {
        if ( within( c, 0x1100u, 0x11FFu ) || within( c, 0x3130u, 0x318Fu ) || static_cast< std::uint16_t >( c + 0x5400u ) <= 0x2BA3u )
            return false;
    }
    return within( c, 0x1100u, 0x11FFu ) || within( c, 0x3000u, 0xD7AFu ) || within( c, 0xF900u, 0xFAFFu ) || static_cast< std::uint16_t >( c + 0x100u ) <= 0xDCu;
}

inline bool wrap_space( std::uint16_t c )
{
    return c == 9u || c == 0xDu || c == 0x20u || c == 0x3000u;
}

// 0x5A8760: may a line break before position p?
inline bool wrap_can_break( const std::uint16_t* t, int p, std::uint32_t flags )
{
    if ( p == 0 )
        return false;
    const std::uint16_t s = t[ p ];
    if ( wrap_space( s ) && wrap_lookup( t[ p + 1 ], flags, false ) )
        return false;
    if ( p * 2 > 2 && wrap_space( t[ p - 2 ] ) && t[ p - 1 ] == 0x22u && !wrap_space( s ) )
        return false;
    const std::uint16_t before = t[ p - 1 ];
    if ( !wrap_space( before ) && s == 0x22u && wrap_space( t[ p + 1 ] ) )
        return false;
    if ( !wrap_space( s ) && !wrap_breakable( s, flags ) && !wrap_breakable( before, flags ) && before != 0x2Du )
        return false;
    return !wrap_lookup( s, flags, false ) && !wrap_lookup( before, flags, true );
}

// 0x5A8870: past the spaces and one newline; -1 at the terminator.
inline int wrap_next_line( const std::uint16_t* t, int p )
{
    while ( wrap_space( t[ p ] ) )
        ++p;
    if ( t[ p ] == 0xAu )
        ++p;
    return t[ p ] ? p : -1;
}

// 0x5A88B0: back over spaces and newlines; -1 before the start.
inline int wrap_trim( const std::uint16_t* t, int p )
{
    while ( p >= 0 && ( wrap_space( t[ p ] ) || t[ p ] == 0xAu ) )
        --p;
    return p;
}

// 0x5A88F0: one line of at most `width`. Answers the next line's start (-1: none) and writes the line's last character to `last` (-1: an empty line).
template < typename Width >
inline int wrap_line( const std::uint16_t* t, std::uint32_t width, std::uint32_t flags, int& last, Width&& measure )
{
    if ( !t || t[ 0 ] == 0 )
    {
        last = -1;
        return -1;
    }
    int p = 0;
    std::uint32_t used = 0;
    while ( t[ p ] != 0xAu )
    {
        used += measure( t[ p ] );
        if ( used > width )
            break;
        ++p;
        if ( t[ p ] == 0 )
            break;
    }
    if ( p == 0 )
    {
        last = wrap_trim( t, 0 );
        return wrap_next_line( t, 1 );
    }
    if ( t[ p ] == 0xAu )
        ++p;
    if ( used <= width )
    {
        last = wrap_trim( t, p - 1 );
        if ( t[ p - 1 ] == 0xAu )
            return p;
        return t[ p ] ? wrap_next_line( t, p ) : -1;
    }
    const int saved = p;
    while ( p > 0 )
    {
        if ( wrap_space( t[ p ] ) )
        {
            const int r = wrap_trim( t, p );
            last = r;
            if ( r < 0 )
                return wrap_next_line( t, p + 1 );
            p = r + 1;
        }
        if ( wrap_can_break( t, p, flags ) )
        {
            if ( p <= 0 )
                break;
            last = p - 1;
            return wrap_next_line( t, p );
        }
        --p;
    }
    last = saved - 1;
    return saved;
}
