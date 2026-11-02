#pragma once

#include <workspace/core/world/wrap.hxx>

#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include <string>
#include <unordered_map>
#include <vector>

// The font's metrics the word wrap stands on (VuFont, the object an asset's +0x58 holds): its glyph records +0x10 (0x2C
// bytes: +0 the code, +0x24 the advance) and the code -> record index map +0x28 that VuFontAsset's 0x6197B0 builds (the
// last record of a code wins). SUBSTITUTION: std::unordered_map for retail's container (its find 0x5389B0, insert
// 0x619F20): only the mapping is read. Transliterated from the disassembly: width 0x537EF0, wrap 0x537DB0 over water's
// wrap_line 0x5A88F0 (world/wrap.hxx), UTF-8 0x559A40 / 0x559D80 / 0x559B50.
struct ui_font_glyphs_t
{
    std::unordered_map< std::uint32_t, std::uint16_t > m_index;
    std::vector< float > m_advance;

    // 0x6197B0's body for one record. (Its index word is the record's, truncated.)
    void add( std::uint32_t code, std::uint32_t record, float advance )
    {
        if ( record >= m_advance.size( ) )
            m_advance.resize( record + 1u );
        m_advance[ record ] = advance;
        m_index[ code ] = static_cast< std::uint16_t >( record );
    }
};

// fistp qword under the truncate mode, the low dword (the width's two conversions): 0 for a NaN / out of range value (the
// indefinite integer's low dword).
inline std::uint32_t ui_font_fixed( float value )
{
    return static_cast< std::uint32_t >( _mm_cvttss_si64( _mm_set_ss( value * 1000.0f ) ) );   // 0x6F7828
}

// 0x537EF0: the character's advance in thousandths; 0 with no record (or the index 0xFFFF).
inline std::uint32_t ui_font_glyph_width( const ui_font_glyphs_t& f, std::uint16_t c )
{
    const auto at = f.m_index.find( c );
    if ( at == f.m_index.end( ) || at->second == 0xFFFFu )
        return 0;
    return ui_font_fixed( f.m_advance[ at->second ] );
}

// 0x559A40: one UTF-8 character at `s`: its byte count, 0 for a bad sequence or past U+10FFFF.
inline int ui_utf8_read( const char* s, std::uint32_t& out )
{
    const std::uint8_t b0 = static_cast< std::uint8_t >( s[ 0 ] );
    if ( b0 < 0x80u )
    {
        out = b0;
        return 1;
    }
    const auto more = [ s ]( int k ) { return static_cast< std::uint8_t >( s[ k ] ); };
    const auto tail = [ &more ]( int k ) { return ( more( k ) & 0xC0u ) == 0x80u; };
    if ( ( b0 & 0xE0u ) == 0xC0u )
    {
        if ( !tail( 1 ) )
            return 0;
        out = ( b0 & 0x1Fu ) << 6 | ( more( 1 ) & 0x3Fu );
        return 2;
    }
    if ( ( b0 & 0xF0u ) == 0xE0u )
    {
        if ( !tail( 1 ) || !tail( 2 ) )
            return 0;
        out = ( ( b0 & 0xFu ) << 6 | ( more( 1 ) & 0x3Fu ) ) << 6 | ( more( 2 ) & 0x3Fu );
        return 3;
    }
    if ( ( b0 & 0xF8u ) == 0xF0u )
    {
        if ( !tail( 1 ) || !tail( 2 ) || !tail( 3 ) )
            return 0;
        out = ( ( ( b0 & 7u ) << 6 | ( more( 1 ) & 0x3Fu ) ) << 6 | ( more( 2 ) & 0x3Fu ) ) << 6 | ( more( 3 ) & 0x3Fu );
        return out > 0x10FFFFu ? 0 : 4;
    }
    return 0;
}

// 0x559D80: UTF-8 to a wide string, one 16-bit unit per character (a character past 0xFFFF loses its high bits); stops at a
// bad sequence. Two zero words end it: wrap_line's 0x5A8760 reads one word past the terminator.
inline std::vector< std::uint16_t > ui_utf8_wide( const char* s )
{
    std::vector< std::uint16_t > out;
    while ( *s )
    {
        std::uint32_t c = 0;
        const int n = ui_utf8_read( s, c );
        if ( n == 0 )
            break;
        out.push_back( static_cast< std::uint16_t >( c ) );
        s += n;
    }
    out.push_back( 0 );
    out.push_back( 0 );
    return out;
}

// 0x559B50 on one unit, appended.
inline void ui_utf8_append( std::string& out, std::uint32_t c )
{
    if ( c <= 0x7Fu )
        out += static_cast< char >( c & 0x7Fu );
    else if ( c <= 0x7FFu )
    {
        out += static_cast< char >( c >> 6 | 0xC0u );
        out += static_cast< char >( ( c & 0x3Fu ) | 0x80u );
    }
    else
    {
        out += static_cast< char >( c >> 12 | 0xE0u );
        out += static_cast< char >( ( c >> 6 & 0x3Fu ) | 0x80u );
        out += static_cast< char >( ( c & 0x3Fu ) | 0x80u );
    }
}

// 0x537DB0: `text` broken into lines of at most `width` (0x5A88F0), joined by "\n". `flags`: the static 0x7359FC (1).
inline std::string ui_font_wrap( const ui_font_glyphs_t& f, const char* text, float width, std::uint32_t flags = 1u )
{
    std::string out;
    const std::vector< std::uint16_t > wide = ui_utf8_wide( text );
    const std::uint32_t limit = ui_font_fixed( width );
    const std::uint16_t* at = wide.data( );
    for ( ;; )
    {
        int last = -1;
        const int next = wrap_line( at, limit, flags, last, [ &f ]( std::uint16_t c ) { return ui_font_glyph_width( f, c ); } );
        for ( int k = 0; k <= last; ++k )
            ui_utf8_append( out, at[ k ] );
        if ( next < 0 )
            break;
        out += '\n';
        at += next;
    }
    return out;
}
