#pragma once

#include <workspace/core/assets/tree.hxx>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include <emmintrin.h>

// VuJsonContainer (16 bytes in retail: +0 the tag - 0 null, 1 int, 2 float, 3
// bool, 4 string, 5 array, 6 object, 7 int64 - +8 / +0xC the payload: the
// value, or a pointer to a VuString / std::vector<VuJsonContainer> /
// std::map<VuString, VuJsonContainer>) over value_t (assets/tree.hxx; its tag
// is retail's). The members no other file recreates; the ones that exist are
// named here so a caller finds them:
//   0x4FBDF0 member (const lookup)        assets/tree.hxx member( )
//   0x4FDC60 vec3 parse                   assets/tree.hxx vec3( )
//   0x4FC550 getValue( int& )             entity/values.hxx property_int_value( )
//   0x4FBB10 clear / 0x4FC090 member-or-insert: json_clear / json_member below
//   (and, separately, ui/config.hxx ui_json_clear / ui_json_child: see the
//   DUPLICATES note at the end of this file).
// SUBSTITUTION: retail's std::vector / std::map internals (0x4FCC30 erase,
// 0x4FD360 copy-assign, 0x4FCFB0 uninitialised copy, 0x4FD840 the map insert)
// are value_t's std::vector members; a map's order is std::map's - the keys
// sorted by std::string's operator< (char_traits<char>::compare, unsigned).
// Verified against retail: hydro checks/water.hxx verify_json_access (case
// json_access: retail's own containers, built by its VUJB read 0x55B560).

// 0x4FBB10: the payload freed, tag 0.
inline void json_clear( value_t& j )
{
    j = value_t{ };
}

// 0x4FC090 (thiscall container, const char* key; 0x4FC6B0: anything not an
// object is cleared and becomes an empty one; then the map's find-or-insert
// 0x4FC970): the member, a null one inserted when absent. Retail's std::map
// finds a key wherever it sits; value_t's object vector keeps whatever order
// built it (c_reader: the file's), so the lookup is a FULL scan for the key
// (batch 27 fix: the earlier scan stopped at the first larger key and, on an
// unsorted vector, inserted a duplicate - the UI agent's forward exposed it).
// A new key goes before the first larger one (order is not observable through
// any lookup; every keys walk sorts).
inline value_t& json_member( value_t& j, const std::string& key )
{
    if ( j.tag != 6 )
    {
        json_clear( j );
        j.tag = 6;
        j.kind = value_t::kind_t::object;
    }
    for ( auto& field : j.object )
        if ( field.first == key )
            return field.second;
    auto at = j.object.begin( );
    while ( at != j.object.end( ) && !( key < at->first ) )
        ++at;
    return j.object.insert( at, { key, value_t{ } } )->second;
}

inline void json_set_int( value_t& j, std::int32_t v )
{
    json_clear( j );
    j.tag = 1, j.kind = value_t::kind_t::integer, j.integer = v;
}

inline void json_set_float( value_t& j, float v )
{
    json_clear( j );
    j.tag = 2, j.kind = value_t::kind_t::real, j.real = v;
}

// 0x4FC640: cleared, tag 4, the text (callers pass it whole: retail's strlen stops at a NUL no name here holds).
inline void json_set_string( value_t& j, const std::string& v )
{
    json_clear( j );
    j.tag = 4, j.kind = value_t::kind_t::text, j.text = v;
}

// The low and high dwords of an int64 payload.
inline std::uint32_t json_lo( const value_t& j )
{
    return static_cast< std::uint32_t >( static_cast< std::uint64_t >( j.int64 ) );
}

// 0x4FC470 asFloat: 1 the int (cvtdq2ps), 2 the float, 7 the int64 (x87 fild
// qword + fstp dword: one rounding to nearest), anything else +0 (xorps).
inline float json_as_float( const value_t& j )
{
    if ( j.tag == 1 )
        return static_cast< float >( j.integer );
    if ( j.tag == 2 )
        return j.real;
    if ( j.tag == 7 )
        return static_cast< float >( j.int64 );
    return 0.0f;
}

// 0x4FC500 asCString: a string's c_str (the VuString's inline buffer when its
// capacity +0x14 is below 16, else its heap pointer), anything else the empty
// literal 0x64D20B.
inline const char* json_as_cstring( const value_t& j )
{
    return j.tag == 4 ? j.text.c_str( ) : "";
}

// MSVC's _ftol2 0x631EB6 (x87: fistp of the rounded value, then the
// difference's sign corrects it toward zero): truncation, and the integer
// indefinite 0x8000000000000000 for NaN and |x| >= 2^63 - what cvttss2si r64
// gives.
inline std::int64_t json_ftol2( float x )
{
    return _mm_cvttss_si64( _mm_set_ss( x ) );
}

// 0x4FC520 asInt64: 1 the int sign-extended (cdq), 2 _ftol2 of the float, 7 the
// int64, anything else 0.
inline std::int64_t json_as_int64( const value_t& j )
{
    if ( j.tag == 1 )
        return j.integer;
    if ( j.tag == 2 )
        return json_ftol2( j.real );
    if ( j.tag == 7 )
        return j.int64;
    return 0;
}

// 0x4FC5E0 getValue( int64& ): asInt64's three tags write `out` and answer
// true; anything else false, `out` untouched.
inline bool json_get_int64( const value_t& j, std::int64_t& out )
{
    if ( j.tag != 1 && j.tag != 2 && j.tag != 7 )
        return false;
    out = json_as_int64( j );
    return true;
}

// 0x4FBD30 append: anything that is not an array is cleared and becomes an
// empty one (tag 5, a fresh vector); then 0x4FBC60( size ): grown by one null
// element, that element answered.
inline value_t& json_append( value_t& j )
{
    if ( j.tag != 5 )
    {
        json_clear( j );
        j.tag = 5;
        j.kind = value_t::kind_t::list;
    }
    j.list.emplace_back( );
    return j.list.back( );
}

// 0x4FBF30 getMemberKeys (thiscall container, std::vector<VuString>* out;
// the vector constructed empty): an object's keys in map order (0x431390
// push_back each), else none.
inline std::vector< std::string > json_member_keys( const value_t& j )
{
    std::vector< std::string > keys;
    if ( j.tag != 6 )
        return keys;
    for ( const auto& field : j.object )
        keys.push_back( field.first );
    std::sort( keys.begin( ), keys.end( ) );
    return keys;
}

// One lane of the vector parses: 1 the int (cvtdq2ps), 2 the float's bits, 7
// the int64 (fild + fstp); false for any other tag, the lane untouched.
inline bool json_lane( const value_t& part, float& out )
{
    if ( part.tag == 1 )
        out = static_cast< float >( part.integer );
    else if ( part.tag == 2 )
        std::memcpy( &out, &part.real, 4 );
    else if ( part.tag == 7 )
        out = static_cast< float >( part.int64 );
    else
        return false;
    return true;
}

// 0x4FDD30 vec4 parse (fastcall container, float* out): "X", "Y", "Z", "W"
// through 0x4FBDF0 (tree.hxx member), each a lane; the first lane that is not a
// number answers false with the lanes before it written.
inline bool json_vec4( const value_t& j, float* out )
{
    static const char* const axes[ 4 ] = { "X", "Y", "Z", "W" };
    for ( int lane = 0; lane < 4; ++lane )
        if ( !json_lane( member( j, axes[ lane ] ), out[ lane ] ) )
            return false;
    return true;
}

// 0x4FDE40 matrix parse (fastcall container, VuMatrix* out): the rows "X",
// "Y", "Z", "T" (0x4FBDF0 each) through 0x4FDD30 into out + 0 / 4 / 8 / 12;
// false at the first row that fails, the rows before it (and that row's
// leading lanes) written.
inline bool json_matrix( const value_t& j, float* out )
{
    static const char* const rows[ 4 ] = { "X", "Y", "Z", "T" };
    for ( int row = 0; row < 4; ++row )
        if ( !json_vec4( member( j, rows[ row ] ), out + 4 * row ) )
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// VuJsonReader - the JSON TEXT reader 0x4FEDF0 .. 0x4FFBB0 (callers: the
// game-mode filter 0x4BCFF0, aidebug.json 0x4042E0, the preview scene
// 0x4C94F0). The reader object: +0 the cursor (const char*), +4 a VuString
// that collects the error messages - emptied by every read, each error
// APPENDED (0x430290 append, then 0x4A9A70( 1, '\n' )): an error does not
// always stop the parse (the comment skipper 0x4FEFD0 ignores its own), so
// one read can leave several lines.
// Verified against retail: hydro checks/logic.hxx verify_json_text (case
// json_text: retail's own reader on text documents).
//
// Every parse function answers 1 (true), 0 (false) or -1: UNKNOWN - a path
// whose retail behaviour is not defined by the code (the string parse's
// cursor stepping past the closing quote it found runs on through memory
// until it happens to meet that address again; see json_text_string).
struct json_text_reader_t
{
    const char* cursor = nullptr;
    std::string error;
};

// 0x4FEF40 error( reader, format, arg ) (cdecl): _vsnprintf_s( buffer[ 256 ],
// 256, 255, format, arg ), buffer[ 255 ] = 0, the text appended to +4 and a
// '\n' after it; answers false. Every format retail passes is "...%s" with the
// one argument a C string (the text from the cursor on): the message is the
// format's text before "%s" plus the argument, at most 255 characters (the
// CRT's truncation of _vsnprintf_s with count 255 - the platform CRT,
// SUBSTITUTION: formatted here, not by MSVCR110).
inline int json_text_error( json_text_reader_t& r, const char* format, const char* arg )
{
    std::string text( format );
    const std::size_t at = text.find( "%s" );
    if ( at != std::string::npos )
        text = text.substr( 0, at ) + arg + text.substr( at + 2 );
    if ( text.size( ) > 255 )
        text.resize( 255 );
    r.error += text;
    r.error += '\n';
    return 0;
}

// 0x4FEFD0 (thiscall reader): skips ' ', '\t', '\r', '\n' and comments; the
// character it stops at (0 at the end of the text). "/*": past the next "*/"
// (strstr), else the error 0x6CADB8 with the cursor left on the '*' - which is
// then the answer. "//": past the next '\r' or '\n' (strpbrk), else to the
// end. '/' followed by anything else: the error 0x6CADE4, the cursor left on
// that character (answered unless it is 0). Its errors never stop a parse.
inline char json_text_skip( json_text_reader_t& r )
{
    char c = *r.cursor;
    if ( c == 0 )
        return 0;
    for ( ;; )
    {
        if ( c == '/' )
        {
            ++r.cursor;
            const char next = *r.cursor;
            if ( next == '*' )
            {
                const char* end = std::strstr( r.cursor, "*/" );
                if ( end != nullptr )
                    r.cursor = end + 2;
                else
                    json_text_error( r, "End of C-style comment not found: %s", r.cursor );
            }
            else if ( next == '/' )
            {
                const char* end = std::strpbrk( r.cursor, "\r\n" );
                r.cursor = end != nullptr ? end + 1 : r.cursor + std::strlen( r.cursor );
            }
            else
                json_text_error( r, "Invalid comment: %s", r.cursor );
        }
        else if ( c == ' ' || c == '\t' || c == '\r' || c == '\n' )
            ++r.cursor;
        else
            return c;
        c = *r.cursor;
        if ( c == 0 )
            return 0;
    }
}

// 0x559B50 (fastcall code, char* out): the UTF-8 bytes of a code point (1 to
// 4; 0 above 0x10FFFF - no surrogate test). 0x559EB0 appends them (0x430290,
// up to the first NUL) to a VuString.
inline int json_utf8( std::uint32_t code, char* out )
{
    if ( code <= 0x7F )
        return out[ 0 ] = static_cast< char >( code & 0x7F ), 1;
    if ( code <= 0x7FF )
        return out[ 0 ] = static_cast< char >( ( code >> 6 ) | 0xC0 ), out[ 1 ] = static_cast< char >( ( code & 0x3F ) | 0x80 ), 2;
    if ( code <= 0xFFFF )
    {
        out[ 0 ] = static_cast< char >( ( code >> 12 ) | 0xE0 );
        out[ 1 ] = static_cast< char >( ( ( code >> 6 ) & 0x3F ) | 0x80 );
        out[ 2 ] = static_cast< char >( ( code & 0x3F ) | 0x80 );
        return 3;
    }
    if ( code <= 0x10FFFF )
    {
        out[ 0 ] = static_cast< char >( ( code >> 18 ) | 0xF0 );
        out[ 1 ] = static_cast< char >( ( ( code >> 12 ) & 0x3F ) | 0x80 );
        out[ 2 ] = static_cast< char >( ( ( code >> 6 ) & 0x3F ) | 0x80 );
        out[ 3 ] = static_cast< char >( ( code & 0x3F ) | 0x80 );
        return 4;
    }
    return 0;
}

// 0x4FFB60 (stdcall text): four hex digits (0-9 a-f A-F) as a code; 0 at the
// first other character - so "\u0000" reads as invalid too.
inline std::uint32_t json_text_hex4( const char* text )
{
    std::uint32_t code = 0;
    for ( int i = 0; i < 4; ++i )
    {
        const char c = text[ i ];
        int digit;
        if ( c >= '0' && c <= '9' )
            digit = c - '0';
        else if ( c >= 'a' && c <= 'f' )
            digit = c - 'a' + 10;
        else if ( c >= 'A' && c <= 'F' )
            digit = c - 'A' + 10;
        else
            return 0;
        code = ( code << 4 ) | static_cast< std::uint32_t >( digit );
    }
    return code;
}

// 0x4FF870 (thiscall reader, VuString* out) - on '"'. The end is the first
// '"' after it (strchr) whose previous character is not '\\', or is "\\\\"
// (the two before it both backslashes: retail looks no further back); none:
// the error 0x6CAF00. Then (a reserve: not observable) each character up to
// that end is appended; '\\' escapes " \\ / b f n r t, and u + four hex digits
// (fewer than four left before the end: 0x6CAF98; not hex or 0: 0x6CAFD4)
// appended as UTF-8; any other escape 0x6CAF4C. The cursor ends past the
// quote. UNKNOWN (-1): an escape that consumes the end quote itself ("\\\\\\""
// - three backslashes before it): retail's loop compares the cursor with the
// end for equality only, so it runs on past the end.
inline int json_text_string( json_text_reader_t& r, std::string& out )
{
    ++r.cursor;
    const char* end = std::strchr( r.cursor, '"' );
    while ( end != nullptr && end[ -1 ] == '\\' && end[ -2 ] != '\\' )
        end = std::strchr( end + 1, '"' );
    if ( end == nullptr )
        return json_text_error( r, "String parsing error, trailing \" not found: %s", r.cursor );
    while ( r.cursor != end )
    {
        if ( r.cursor > end )
            return -1;
        const char c = *r.cursor;
        if ( c != '\\' )
            out += c;
        else
        {
            ++r.cursor;
            switch ( *r.cursor )
            {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
            {
                ++r.cursor;
                if ( end - r.cursor < 4 )
                    return json_text_error( r, "String parsing error, invalid unicode escape sequence: %s", r.cursor );
                const std::uint32_t code = json_text_hex4( r.cursor );
                if ( code == 0 )
                    return json_text_error( r, "String parsing error, invalid unicode escape sequence: %s", r.cursor );
                char bytes[ 5 ] = { };
                json_utf8( code, bytes );
                out += bytes;
                r.cursor += 3;
                break;
            }
            default:
                return json_text_error( r, "String parsing error, invalid escape sequence: %s", r.cursor );
            }
        }
        ++r.cursor;
    }
    r.cursor = end + 1;
    return 1;
}

// sscanf_s( text, "%g", float* ) == 1 (0x4FFA70's conversion; MSVCR110's
// input routine for a float): an optional sign, digits, an optional '.' and
// digits, then - only when a digit was seen - an optional e / E, sign and
// digits; nothing matched without a digit. SUBSTITUTION: the collected text is
// converted by the platform's strtof (correctly rounded) where MSVCR110 goes
// through its 80-bit __strgtold12 + _ld12tof - they can differ for a value
// within a hair of a float rounding boundary or outside the normal range
// (the case marks such vectors UNKNOWN). The text never holds an 'x' here
// (the collector stops at it), so strtof's hex form is unreachable.
inline bool json_text_scan_float( const char* text, float& out )
{
    std::string s;
    const char* p = text;
    while ( *p == ' ' || ( *p >= '\t' && *p <= '\r' ) )
        ++p;
    if ( *p == '-' || *p == '+' )
        s += *p++;
    bool started = false;
    while ( *p >= '0' && *p <= '9' )
        s += *p++, started = true;
    if ( *p == '.' )
    {
        s += *p++;
        while ( *p >= '0' && *p <= '9' )
            s += *p++, started = true;
    }
    if ( started && ( *p == 'e' || *p == 'E' ) )
    {
        s += 'e';
        ++p;
        if ( *p == '-' || *p == '+' )
            s += *p++;
        while ( *p >= '0' && *p <= '9' )
            s += *p++;
    }
    if ( !started )
        return false;
    out = std::strtof( s.c_str( ), nullptr );
    return true;
}

// 0x4FFA70 (thiscall reader, int length, VuJsonContainer* out): length >= 256
// -> the error 0x6CAF80; the text copied (memcpy_s) into a 256-byte buffer,
// NUL-terminated at length; sscanf_s "%g" != 1 -> 0x6CB014; else out cleared,
// tag 2, the float, the cursor moved by length.
inline int json_text_float( json_text_reader_t& r, int length, value_t& out )
{
    if ( static_cast< unsigned >( length ) >= 0x100 )
        return json_text_error( r, "Float parsing error: %s", r.cursor );
    char buffer[ 0x100 ];
    std::memcpy( buffer, r.cursor, static_cast< std::size_t >( length ) );
    buffer[ length ] = 0;
    float value = 0.0f;
    if ( !json_text_scan_float( buffer, value ) )
        return json_text_error( r, "Float parsing error: %s", r.cursor );
    json_set_float( out, value );
    r.cursor += length;
    return 1;
}

// 0x4FF5B0 (thiscall reader, VuJsonContainer* out) - on '-' or a digit. The
// token: the first character plus every following one of 0-9 . e E + - x a-f
// A-F. "0x" first: hex. Else when every character after the first is a digit
// (none counts): decimal; otherwise the float parse. The integer: from index
// 2 (hex), 1 ('-') or 0, value = value x radix + digit in 64 bits, wrapping
// (_allmul 0x631DD0); a digit is c - 'a' + 10 / c - 'A' + 10, else c - '0' -
// in hex that includes '.', '+', '-', 'x' (negative / large "digits", kept).
// '-' negates. -2^31 <= v < 2^31: cleared, tag 1; else tag 7 (the payload
// written over without a clear - the old payload is always null here). The
// cursor moves past the token. A lone '-' is the int 0.
inline int json_text_number( json_text_reader_t& r, value_t& out )
{
    const char* p = r.cursor;
    const bool hex = p[ 0 ] == '0' && p[ 1 ] == 'x';
    int length = 1;
    for ( char c = p[ 1 ]; c != 0; c = p[ ++length ] )
    {
        const bool allowed = ( c >= '0' && c <= '9' ) || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-' ||
                             c == 'x' || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' );
        if ( !allowed )
            break;
    }
    std::uint64_t radix = 16;
    if ( !hex )
    {
        for ( int i = 1; i < length; ++i )
            if ( p[ i ] < '0' || p[ i ] > '9' )
                return json_text_float( r, length, out );
        radix = 10;
    }
    const bool negative = p[ 0 ] == '-';
    std::uint64_t value = 0;
    for ( int i = hex ? 2 : negative ? 1 : 0; i < length; ++i )
    {
        const char c = p[ i ];
        std::int64_t digit;
        if ( c >= 'a' && c <= 'f' )
            digit = c - 'a' + 10;
        else if ( c >= 'A' && c <= 'F' )
            digit = c - 'A' + 10;
        else
            digit = static_cast< std::int64_t >( c ) - '0';
        value = value * radix + static_cast< std::uint64_t >( digit );
    }
    if ( negative )
        value = 0 - value;
    if ( ( ( value + 0x80000000ull ) >> 32 ) == 0 )
        json_set_int( out, static_cast< std::int32_t >( static_cast< std::uint32_t >( value ) ) );
    else
    {
        json_clear( out );
        out.tag = 7, out.kind = value_t::kind_t::int64, out.int64 = static_cast< std::int64_t >( value );
    }
    r.cursor += length;
    return 1;
}

inline int json_text_value( json_text_reader_t& r, value_t& out );

// 0x4FF1E0 (thiscall reader, VuJsonContainer* out) - on '{'. "{}" answers true
// with out UNTOUCHED (null: an empty object reads as null). Per member: '"'
// (else 0x6CAE54), the key (0x4FF870), a key already in out's map -> 0x6CADFC
// (only once out is a map), ':' (else 0x6CAE18), out made a map (cleared, tag
// 6, a new std::map) on the first member, the value into map[ key ]
// (0x4FCAB0), then '}' ends, ',' continues - a ',' before '}' ends too - and
// anything else is 0x6CAE8C.
inline int json_text_object( json_text_reader_t& r, value_t& out )
{
    ++r.cursor;
    if ( json_text_skip( r ) == '}' )
        return ++r.cursor, 1;
    for ( ;; )
    {
        if ( json_text_skip( r ) != '"' )
            return json_text_error( r, "Object parsing error, expecting '\"': %s", r.cursor );
        std::string key;
        const int got = json_text_string( r, key );
        if ( got != 1 )
            return got;
        if ( out.tag == 6 )
            for ( const auto& field : out.object )
                if ( field.first == key )
                    return json_text_error( r, "Duplicate object member: %s", key.c_str( ) );
        if ( json_text_skip( r ) != ':' )
            return json_text_error( r, "Object parsing error, expecting ':': %s", r.cursor );
        ++r.cursor;
        const int value = json_text_value( r, json_member( out, key ) );
        if ( value != 1 )
            return value;
        const char c = json_text_skip( r );
        if ( c == '}' )
            return ++r.cursor, 1;
        if ( c != ',' )
            return json_text_error( r, "Object parsing error, expecting '}' or ',': %s", r.cursor );
        ++r.cursor;
        if ( json_text_skip( r ) == '}' )
            return ++r.cursor, 1;
    }
}

// 0x4FF3E0 (thiscall reader, VuJsonContainer* out) - on '['. "[]" answers true
// with out UNTOUCHED (null). Per element: out made an array (cleared, tag 5, a
// new vector) the first time, grown by one null (0x4FC880( size + 1 )), the
// value into it; then ']' ends, ',' continues (',' before ']' ends too), else
// 0x6CAEBC.
inline int json_text_array( json_text_reader_t& r, value_t& out )
{
    ++r.cursor;
    if ( json_text_skip( r ) == ']' )
        return ++r.cursor, 1;
    for ( ;; )
    {
        const int value = json_text_value( r, json_append( out ) );
        if ( value != 1 )
            return value;
        const char c = json_text_skip( r );
        if ( c == ']' )
            return ++r.cursor, 1;
        if ( c != ',' )
            return json_text_error( r, "Array parsing error, expecting ']' or ',': %s", r.cursor );
        ++r.cursor;
        if ( json_text_skip( r ) == ']' )
            return ++r.cursor, 1;
    }
}

// 0x4FF090 (thiscall reader, VuJsonContainer* out): on the character after
// 0x4FEFD0 (tables 0x4FF15C / 0x4FF13C): 0 -> true, out untouched; '{' object;
// '[' array; '"' string (0x4FF510: the text set by 0x4FC640); '-' / digit
// number; 't' / 'f' 0x4FF790 ("true" / "false" by strncmp: tag 3, byte 1 / 0,
// cursor + 4 / 5; else 0x6CAF30); 'n' 0x4FF820 ("null": out = the static null
// 0x7593A0 via 0x4FB9B0, cursor + 4; else 0x6CAEEC); anything else (incl.
// bytes >= 0x80) 0x6CAE40 "Invalid token".
inline int json_text_value( json_text_reader_t& r, value_t& out )
{
    const char c = json_text_skip( r );
    switch ( c )
    {
    case 0:
        return 1;
    case '{':
        return json_text_object( r, out );
    case '[':
        return json_text_array( r, out );
    case '"':
    {
        std::string text;
        const int got = json_text_string( r, text );
        if ( got != 1 )
            return got;
        json_clear( out );
        out.tag = 4, out.kind = value_t::kind_t::text, out.text = text.c_str( );
        return 1;
    }
    case 't':
    case 'f':
        if ( std::strncmp( r.cursor, "true", 4 ) == 0 )
        {
            json_clear( out );
            out.tag = 3, out.kind = value_t::kind_t::integer, out.integer = 1;
            r.cursor += 4;
            return 1;
        }
        if ( std::strncmp( r.cursor, "false", 5 ) == 0 )
        {
            json_clear( out );
            out.tag = 3, out.kind = value_t::kind_t::integer, out.integer = 0;
            r.cursor += 5;
            return 1;
        }
        return json_text_error( r, "Invalid token: %s", r.cursor );
    case 'n':
        if ( std::strncmp( r.cursor, "null", 4 ) == 0 )
        {
            json_clear( out );
            r.cursor += 4;
            return 1;
        }
        return json_text_error( r, "Invalid token: %s", r.cursor );
    default:
        if ( c == '-' || ( c >= '0' && c <= '9' ) )
            return json_text_number( r, out );
        return json_text_error( r, "Invalid token: %s", r.cursor );
    }
}

// 0x4FEEC0 (thiscall reader, VuJsonContainer* out, const char* text): the
// error text emptied, the cursor at text, out cleared; the value (failure:
// out cleared, false); then anything but the end after 0x4FEFD0 -> out
// cleared, 0x6CAD98 "Expecting end of document". An empty text reads as null.
inline int json_read_text( json_text_reader_t& r, value_t& out, const char* text )
{
    r.error.clear( );
    r.cursor = text;
    json_clear( out );
    const int got = json_text_value( r, out );
    if ( got != 1 )
    {
        if ( got == 0 )
            json_clear( out );
        return got;
    }
    if ( json_text_skip( r ) != 0 )
    {
        json_clear( out );
        return json_text_error( r, "Expecting end of document: %s", r.cursor );
    }
    return 1;
}

// The file system g 0x75908C as 0x4FEDF0 uses it - a PLATFORM hook: vt+0x34
// open( path, 0 ) (0 = none), vt+0x2C size, vt+0x40 read( handle, buffer,
// size ), vt+0x3C close.
struct json_file_hooks_t
{
    void* user = nullptr;
    void* ( *open )( void* user, const std::string& path ) = nullptr;
    std::uint32_t ( *size )( void* user, void* handle ) = nullptr;
    void ( *read )( void* user, void* handle, char* buffer, std::uint32_t size ) = nullptr;
    void ( *close )( void* user, void* handle ) = nullptr;
};

// 0x4FEDF0 (thiscall reader, VuJsonContainer* out, const VuString& path): the
// error text emptied; not opened -> 0x6CAD78 "Unable to open for reading"
// (out untouched); else new[ size + 1 ], the read, a NUL at size, a UTF-8
// BOM (EF BB BF) skipped, 0x4FEEC0 on the text, delete[], close.
inline int json_read_file( json_text_reader_t& r, value_t& out, const std::string& path, const json_file_hooks_t& fs )
{
    r.error.clear( );
    void* handle = fs.open( fs.user, path );
    if ( handle == nullptr )
        return json_text_error( r, "Unable to open for reading: %s", path.c_str( ) );
    const std::uint32_t size = fs.size( fs.user, handle );
    std::vector< char > buffer( static_cast< std::size_t >( size ) + 1 );
    fs.read( fs.user, handle, buffer.data( ), size );
    buffer[ size ] = 0;
    const char* text = buffer.data( );
    if ( static_cast< unsigned char >( text[ 0 ] ) == 0xEF && static_cast< unsigned char >( text[ 1 ] ) == 0xBB &&
         static_cast< unsigned char >( text[ 2 ] ) == 0xBF )
        text += 3;
    const int got = json_read_text( r, out, text );
    fs.close( fs.user, handle );
    return got;
}

// --- VuJsonWriter (0x4FFBC0 .. 0x500360), batch 29 --------------------------
// The writer object: +0 byte compact (0 = pretty: a newline and the indent
// before containers and elements), +1 byte trailing comma, +4 the output
// std::string*, +8 the indent std::string (one tab per level). Its users found
// (0x4046F0 aidebug, 0x4BC670, 0x4C3590 VuGameModeManager's ProjectAsset,
// 0x4CA850 preview, 0x53C1F0 VuStringDBImpl) build it with word +0 = 0.
// SUBSTITUTION: the numbers go through MSVCR110's sprintf_s ("%d", "%lld",
// "%.8g" of the float widened to double): json_crt_format models that CRT -
// the C library's digits with the exponent padded to MSVCR110's three digits
// ("1e+020"); a non-finite float (MSVCR110 prints "1.#INF" / "1.#QNAN" / its
// precision-mangled forms, not measured) makes the result UNKNOWN.
struct json_writer_t
{
    bool compact = false;                 // +0
    bool trailing = false;                // +1
    std::string* out = nullptr;           // +4
    std::string indent;                   // +8
    bool unknown = false;                 // a non-finite float was written (its text is UNKNOWN)
};

inline std::string json_crt_format( const char* format, double d )
{
    char buf[ 64 ];
    std::snprintf( buf, sizeof( buf ), format, d );
    std::string s( buf );
    const std::size_t e = s.find( 'e' );
    if ( e != std::string::npos && e + 2 < s.size( ) )
    {
        std::string digits = s.substr( e + 2 );
        while ( digits.size( ) < 3 )
            digits.insert( digits.begin( ), '0' );
        s = s.substr( 0, e + 2 ) + digits;
    }
    return s;
}

// 0x4FFDA0 / 0x500150: sprintf_s "%d" (16 bytes) / "%lld" (32 bytes), appended up to the NUL.
inline void json_write_int( json_writer_t& w, std::int32_t v )
{
    char buf[ 16 ];
    std::snprintf( buf, sizeof( buf ), "%d", v );
    w.out->append( buf );
}

inline void json_write_int64( json_writer_t& w, std::int64_t v )
{
    char buf[ 32 ];
    std::snprintf( buf, sizeof( buf ), "%lld", static_cast< long long >( v ) );
    w.out->append( buf );
}

// 0x4FFE10 (the float in xmm1): sprintf_s "%.8g" of (double) f into 16 bytes; exactly "-0" becomes "0" (strcpy_s).
inline void json_write_float( json_writer_t& w, float f )
{
    if ( !std::isfinite( f ) )
    {
        w.unknown = true;
        return;
    }
    std::string s = json_crt_format( "%.8g", static_cast< double >( f ) );
    if ( s == "-0" )
        s = "0";
    w.out->append( s );
}

// 0x4FFED0: "true" when the bool byte (+8) is non-zero, else "false".
inline void json_write_bool( json_writer_t& w, bool b )
{
    w.out->append( b ? "true" : "false" );
}

// 0x4FFF20: '"', each character up to the first NUL - 8 \b, 9 \t, 10 \n, 12 \f, 13 \r, '"' \", '\\' \\ (the jump
// table 0x5000D8 through the byte map 0x5000F8 over c - 8 in 0 .. 0x54, signed char), anything else as it is - then
// '"'; the whole appended up to its NUL.
inline void json_write_string( json_writer_t& w, const std::string& text )
{
    std::string s( 1, '"' );
    for ( const char* p = text.c_str( ); *p; ++p )
    {
        switch ( *p )
        {
        case 8: s += "\\b"; break;
        case 9: s += "\\t"; break;
        case 10: s += "\\n"; break;
        case 12: s += "\\f"; break;
        case 13: s += "\\r"; break;
        case '"': s += "\\\""; break;
        case '\\': s += "\\\\"; break;
        default: s += *p; break;
        }
    }
    s += '"';
    w.out->append( s.c_str( ) );
}

// Pretty mode's line break: '\n' and the indent.
inline void json_write_break( json_writer_t& w )
{
    if ( w.compact )
        return;
    w.out->push_back( '\n' );
    w.out->append( w.indent );
}

inline void json_write_value( json_writer_t& w, const value_t& v );

// 0x5001C0: not an array, or empty -> "[]". Else the break, "[", one more tab; per element (index order) a break
// unless it is an array / object (those break themselves), the element, "," unless it is the last (or the trailing
// flag); one tab less, the break, "]".
inline void json_write_array( json_writer_t& w, const value_t& v )
{
    const std::size_t n = v.tag == 5 ? v.list.size( ) : 0;
    if ( n == 0 )
    {
        w.out->append( "[]" );
        return;
    }
    json_write_break( w );
    w.out->append( "[" );
    w.indent.append( "\t" );
    for ( std::size_t i = 0; i < n; ++i )
    {
        const value_t& e = v.list[ i ];
        if ( e.tag != 5 && e.tag != 6 )
            json_write_break( w );
        json_write_value( w, e );
        if ( i + 1 < n || w.trailing )
            w.out->append( "," );
    }
    w.indent.pop_back( );
    json_write_break( w );
    w.out->append( "]" );
}

// 0x500360: the keys by getMemberKeys 0x4FBF30 (map order); none -> "{}". Else the break, "{", one more tab; per
// key a break, the key as a string, " : " (pretty) or ":" (compact), the member (0x4F22B0's find; the null
// container 0x7593A0 when absent), "," unless last (or trailing); one tab less, the break, "}".
inline void json_write_object( json_writer_t& w, const value_t& v )
{
    const std::vector< std::string > keys = json_member_keys( v );
    if ( keys.empty( ) )
    {
        w.out->append( "{}" );
        return;
    }
    static const value_t null_value{ };
    json_write_break( w );
    w.out->append( "{" );
    w.indent.append( "\t" );
    for ( std::size_t i = 0; i < keys.size( ); ++i )
    {
        json_write_break( w );
        json_write_string( w, keys[ i ] );
        w.out->append( w.compact ? ":" : " : " );
        const value_t* member = &null_value;
        if ( v.tag == 6 )
            for ( const auto& field : v.object )
                if ( field.first == keys[ i ] )
                {
                    member = &field.second;
                    break;
                }
        json_write_value( w, *member );
        if ( i + 1 < keys.size( ) || w.trailing )
            w.out->append( "," );
    }
    w.indent.pop_back( );
    json_write_break( w );
    w.out->append( "}" );
}

// 0x4FFCC0: by the tag (jump table 0x4FFD7C) - 0 "null", 1 int, 2 float, 3 bool, 4 string, 5 array, 6 object, 7
// int64; any other tag writes nothing.
inline void json_write_value( json_writer_t& w, const value_t& v )
{
    switch ( v.tag )
    {
    case 0: w.out->append( "null" ); break;
    case 1: json_write_int( w, v.integer ); break;
    case 2: json_write_float( w, v.real ); break;
    case 3: json_write_bool( w, ( v.integer & 0xFF ) != 0 ); break;
    case 4: json_write_string( w, v.text ); break;
    case 5: json_write_array( w, v ); break;
    case 6: json_write_object( w, v ); break;
    case 7: json_write_int64( w, v.int64 ); break;
    default: break;
    }
}

// The file system 0x4FFBC0 writes through (g 0x75908C, PLATFORM): vt+0x34 open( path, 1 ) (0 = none), vt+0x44
// write( handle, bytes, size ), vt+0x3C close.
struct json_write_hooks_t
{
    void* user = nullptr;
    void* ( *open )( void* user, const std::string& path ) = nullptr;
    void ( *write )( void* user, void* handle, const char* bytes, std::uint32_t size ) = nullptr;
    void ( *close )( void* user, void* handle ) = nullptr;
};

// 0x4FFBC0 (thiscall writer, const VuJsonContainer&, const VuString& path; retn 8): the text into a local string
// (the indent emptied first), then open; not opened -> false; else the UTF-8 BOM EF BB BF, the text, close, true.
inline bool json_write_file( json_writer_t& w, const value_t& v, const std::string& path, const json_write_hooks_t& fs, std::string* text_out = nullptr )
{
    std::string text;
    w.out = &text;
    w.indent.clear( );
    json_write_value( w, v );
    if ( text_out )
        *text_out = text;
    void* handle = fs.open ? fs.open( fs.user, path ) : nullptr;
    if ( handle == nullptr )
        return false;
    const char bom[ 3 ] = { '\xEF', '\xBB', '\xBF' };
    fs.write( fs.user, handle, bom, 3 );
    fs.write( fs.user, handle, text.c_str( ), static_cast< std::uint32_t >( text.size( ) ) );
    fs.close( fs.user, handle );
    return true;
}
// DUPLICATES (for the integrator): VuJsonContainer is recreated three times
// over value_t -
//   (a) this file + assets/tree.hxx (member / vec3) + entity/values.hxx
//       (property_int_value);
//   (b) ui/config.hxx ui_json_clear / _make_map / _child (0x4FC090) /
//       _set_string (0x4FC640) / _at (0x4FBC60) / _erase (0x4FC140) / _find
//       (0x4F22B0) / _text / _keys (0x4FBE90 / 0x4FC000) and ui/select.hxx
//       ui_json_set_int / _set_bool / _element (0x4FBC10) / _size (0x4FBC40) /
//       _child_s (0x4FCAB0) / _as_int (0x4FC450) / _equal (0x4FC230).
// They agree on every answer; the one difference is the object vector's order
// after an insertion (ui_json_child appends, json_member keeps key order) -
// invisible to every lookup and to the keys walks (both sort). Proposal: keep
// this engine file as the one copy, move (b)'s functions here under the json_
// names, and leave the ui_ names as one-line forwards until their callers move.
