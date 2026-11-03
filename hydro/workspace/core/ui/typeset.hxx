#pragma once

#include <workspace/core/ui/glyphs.hxx>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// VuFont: the loader from the asset's bytes, the text layout, the measure and the device draw. Transliterated from the
// disassembly (tools tmp/b30/d_619310.txt, d_537AD0.txt, d_5370F0.txt, d_537600.txt, d_538rest.txt):
//   0x619310  the load (VuFontAsset slot 5 0x5CAE60 -> ctor 0x618970): 3 floats, the 0x2C glyph records, the atlas, the images
//   0x6197B0  the code map (glyphs.hxx ui_font_glyphs_t::add)
//   0x618890  one box-filtered mip of the 8-bit atlas
//   0x537AD0  the text's units: record indices, 0xFFFE a line break, 0xFFFD the default colour, 0xFFF0.. a palette colour
//   0x537D60  a colour tag's palette slot
//   0x5370F0  the text's extent in screen units, 0x5373D0 its widest line
//   0x5375E0 / 0x537600  the sort command's draw (the callback draw.hxx's ui_font_draw submits)
//   0x537FA0  the first line's y, 0x5380B0 a line's width, 0x538110 one line's quads, 0x538680 a line skipped, 0x538700 the images
// HOOKS (ui_font_ops_t): the device g 0x75907C and the shader program's constants. The texture create 0x57C9B0 + the image
// assets (g 0x759004 slot 7): the caller's (m_texture, m_image_textures hold its handles; 0 until a device exists).
// 0x50B420's index buffer is the device side: { 0, 1, 2, 0, 2, 3 } + 4 per quad; the draw's arguments are ui_font_ops_t::m_draw's.

// The 0x2C-byte glyph record +0x10.
struct ui_font_glyph_t
{
    std::uint32_t m_code = 0;
    float m_u[ 2 ] = { };             // +4 +8
    float m_v[ 2 ] = { };             // +0xC +0x10
    float m_x[ 2 ] = { };             // +0x14 +0x18
    float m_y[ 2 ] = { };             // +0x1C +0x20
    float m_advance = 0.0f;           // +0x24
    std::uint16_t m_image = 0;        // +0x28
    std::uint8_t m_has_image = 0;     // +0x2A
};

struct ui_font_t
{
    std::uint32_t m_texture = 0;              // +0: the atlas's texture (a caller's handle)
    float m_ascent = 0.0f, m_descent = 0.0f, m_scale = 0.0f;   // +4 +8 +0xC
    std::vector< ui_font_glyph_t > m_glyphs;  // +0x10
    ui_font_glyphs_t m_map;                   // +0x28
    std::int32_t m_width = 0, m_height = 0;
    std::vector< std::uint8_t > m_atlas;
    std::vector< std::string > m_images;      // the names 0x619310 reads; +0x1C the assets
    std::vector< std::uint32_t > m_image_textures;   // each asset's +0x58 (the caller's handles)
};

// 0x619310 on the asset's bytes. False: a read past the end (retail has no such test: its memcpy_s faults).
inline bool ui_font_load( const std::uint8_t* data, std::size_t size, ui_font_t& f )
{
    std::size_t at = 0;
    const auto u32 = [ & ]( std::uint32_t& out )
    {
        if ( at + 4 > size )
            return false;
        std::memcpy( &out, data + at, 4 );
        at += 4;
        return true;
    };
    std::uint32_t v = 0, count = 0;
    f = ui_font_t{ };
    if ( !u32( v ) )
        return false;
    std::memcpy( &f.m_ascent, &v, 4 );
    if ( !u32( v ) )
        return false;
    std::memcpy( &f.m_descent, &v, 4 );
    if ( !u32( v ) )
        return false;
    std::memcpy( &f.m_scale, &v, 4 );
    if ( !u32( count ) || at + 0x2Cull * count > size )
        return false;
    f.m_glyphs.resize( count );
    for ( std::uint32_t i = 0; i < count; ++i )
    {
        const std::uint8_t* r = data + at + 0x2Cull * i;
        ui_font_glyph_t& g = f.m_glyphs[ i ];
        std::memcpy( &g.m_code, r, 4 );
        std::memcpy( g.m_u, r + 4, 8 );
        std::memcpy( g.m_v, r + 0xC, 8 );
        std::memcpy( g.m_x, r + 0x14, 8 );
        std::memcpy( g.m_y, r + 0x1C, 8 );
        std::memcpy( &g.m_advance, r + 0x24, 4 );
        std::memcpy( &g.m_image, r + 0x28, 2 );
        g.m_has_image = r[ 0x2A ];
        f.m_map.add( g.m_code, i, g.m_advance );
    }
    at += 0x2Cull * count;
    std::uint32_t w = 0, h = 0, images = 0;
    if ( !u32( w ) || !u32( h ) )
        return false;
    const std::size_t pixels = static_cast< std::size_t >( w ) * h;
    if ( at + pixels > size )
        return false;
    f.m_width = static_cast< std::int32_t >( w );
    f.m_height = static_cast< std::int32_t >( h );
    f.m_atlas.assign( data + at, data + at + pixels );
    at += pixels;
    if ( !u32( images ) )
        return false;
    for ( std::uint32_t i = 0; i < images; ++i )
    {
        const std::size_t start = at;
        while ( at < size && data[ at ] )
            ++at;
        if ( at >= size )
            return false;
        f.m_images.emplace_back( reinterpret_cast< const char* >( data + start ), at - start );
        ++at;
    }
    f.m_image_textures.assign( images, 0u );
    return true;
}

// 0x618890 (fastcall width, height; src, dst): one mip of half the size (min 1): each texel the sum of the 2x2 block >> 2,
// the block's second row / column clamped to the last.
inline void ui_font_mip( int width, int height, const std::uint8_t* src, std::uint8_t* dst )
{
    const int w = width >> 1 > 1 ? width >> 1 : 1;
    const int h = height >> 1 > 1 ? height >> 1 : 1;
    const int last_y = height - 1, last_x = width - 1;
    for ( int y = 0; y < h; ++y )
    {
        const int y0 = ( 2 * y ) < last_y ? 2 * y : last_y;
        const int y1 = ( 2 * y + 1 ) < last_y ? 2 * y + 1 : last_y;
        const std::uint8_t* r0 = src + y0 * width;
        const std::uint8_t* r1 = src + y1 * width;
        for ( int x = 0; x < w; ++x )
        {
            const int x0 = ( 2 * x ) < last_x ? 2 * x : last_x;
            const int x1 = ( 2 * x + 1 ) < last_x ? 2 * x + 1 : last_x;
            *dst++ = static_cast< std::uint8_t >( ( r0[ x1 ] + r0[ x0 ] + r1[ x1 ] + r1[ x0 ] ) >> 2 );
        }
    }
}

// The layout context 0x537AD0 / 0x538110 work on (the callers' locals).
struct ui_font_context_t
{
    const ui_font_t* m_font = nullptr;     // +0
    float m_clip[ 4 ] = { };               // +4
    float m_scale_x = 0.0f;                // +0x14
    float m_scale_y = 0.0f;                // +0x18
    float m_origin_x = 0.0f;               // +0x1C
    float m_origin_y = 0.0f;               // +0x20
    std::vector< std::uint16_t >* m_units = nullptr;   // +0x24
    std::uint32_t m_colour = 0;            // +0x28
    std::uint32_t m_palette[ 10 ] = { };   // +0x2C
    std::int32_t m_colours = 0;            // +0x54
};

// 0x537D60: the tag's slot, added when new; 0xFFFD when the ten are full.
inline std::uint16_t ui_font_palette( ui_font_context_t& c, std::uint32_t colour )
{
    for ( int i = 0; i < c.m_colours; ++i )
        if ( c.m_palette[ i ] == colour )
            return static_cast< std::uint16_t >( i + 0xFFF0 );
    if ( c.m_colours >= 10 )
        return 0xFFFDu;
    c.m_palette[ c.m_colours ] = colour;
    ++c.m_colours;
    return static_cast< std::uint16_t >( c.m_colours + 0xFFEF );
}

// The tag "{[%d,%d,%d,%d]}" through sscanf_s 0x64C22C: the count of ints read; a stops at what it holds when absent.
inline int ui_font_tag( const char* text, int& r, int& g, int& b, int& a )
{
    return sscanf_s( text, "{[%d,%d,%d,%d]}", &r, &g, &b, &a );
}

// 0x537AD0 (fastcall context, text; tab size, flags).
inline void ui_font_units( ui_font_context_t& c, const char* text, std::int32_t tab, std::uint32_t flags )
{
    const ui_font_t& f = *c.m_font;
    std::vector< std::uint16_t >& out = *c.m_units;
    const auto space_at = f.m_map.m_index.find( 0x20u );
    const std::uint16_t space = space_at == f.m_map.m_index.end( ) ? 0xFFFFu : space_at->second;
    out.clear( );
    const char* s = text;
    while ( *s )
    {
        const char ch = *s;
        if ( ch == '\n' )
        {
            if ( !( flags & 0x40u ) )
                out.push_back( 0xFFFEu );
            ++s;
        }
        else if ( ch == '\t' )
        {
            for ( std::int32_t k = 0; k < tab; ++k )
                out.push_back( space );
            ++s;
        }
        else if ( ch == '{' && s[ 1 ] == '[' )
        {
            const char* end = std::strstr( s, "]}" );
            if ( !end )
                return;
            int r = 0, g = 0, b = 0, a = 0xFF;
            std::uint16_t unit = 0xFFFDu;
            if ( ui_font_tag( s, r, g, b, a ) >= 3 )
                unit = ui_font_palette( c, static_cast< std::uint32_t >( r & 0xFF ) | static_cast< std::uint32_t >( g & 0xFF ) << 8 | static_cast< std::uint32_t >( b & 0xFF ) << 16 |
                                               static_cast< std::uint32_t >( a & 0xFF ) << 24 );
            out.push_back( unit );
            s = end + 2;
        }
        else
        {
            std::uint32_t code = 0;
            const int n = ui_utf8_read( s, code );
            if ( n == 0 )
            {
                ++s;
                continue;
            }
            const auto at = f.m_map.m_index.find( code );
            if ( at != f.m_map.m_index.end( ) && at->second != 0xFFFFu )
                out.push_back( at->second );
            s += n;
        }
    }
}

// maxss a, b: b when not a > b.
inline float ui_font_maxss( float a, float b ) { return a > b ? a : b; }
inline float ui_font_minss( float a, float b ) { return a < b ? a : b; }

// 0x5370F0 (fastcall out, font; text, params, width, flags, the screen's size): the extent of the text in screen units.
inline void ui_font_measure( const ui_font_t& f, const char* text, const std::uint8_t params[ 60 ], float width, std::uint32_t flags, const float screen[ 2 ], float out[ 2 ] )
{
    float p[ 15 ];
    std::memcpy( p, params, 60 );
    const float aspect = screen[ 0 ] / screen[ 1 ];
    const float size = p[ 0 ] * 0.01f;                                  // 0x6F75D4
    const float scale_x = p[ 9 ] * size / aspect;
    const float inverse = 1.0f / ( f.m_scale * 2.0f * size * 720.0f );  // 0x6F76F8 0x6F7820
    const float bold = 1.0f - p[ 1 ] * 0.01f * 0.5f - p[ 4 ] * inverse - p[ 2 ] * inverse * 0.5f;
    const float origin_y = ( 0.5f - bold ) * 2.0f * f.m_scale;
    const float origin_x = p[ 7 ] * f.m_ascent + origin_y;
    std::string wrapped;
    if ( ( flags & 0x80u ) && !( flags & 0x40u ) )
    {
        wrapped = ui_font_wrap( f.m_map, text, width / scale_x - origin_x );
        text = wrapped.c_str( );
    }
    std::vector< std::uint16_t > units;
    ui_font_context_t c;
    c.m_font = &f;
    c.m_units = &units;
    c.m_scale_x = scale_x;
    c.m_scale_y = size;
    c.m_origin_x = origin_x;
    c.m_origin_y = origin_y;
    std::uint32_t tab = 0;
    std::memcpy( &tab, params + 0x20, 4 );
    std::memcpy( &c.m_colour, params + 0xC, 4 );
    ui_font_units( c, text, static_cast< std::int32_t >( tab ), flags );
    float x = 0.0f, y = 0.0f, line = 0.0f;
    for ( const std::uint16_t u : units )
    {
        if ( u < 0xFFF0u )
            line = line + f.m_glyphs[ u ].m_advance;
        else if ( u == 0xFFFEu )
        {
            x = ui_font_maxss( x, line );
            y = y + 1.0f;
            line = 0.0f;
        }
    }
    x = ui_font_maxss( x, line );
    out[ 0 ] = ( x + origin_x ) * scale_x;
    out[ 1 ] = ( origin_y * 2.0f + ( f.m_ascent - f.m_descent + y ) ) * size;
}

// 0x5373D0 (thiscall font, edx text; params; xmm3 the aspect): the widest line in screen units. A tab adds the tab size x the
// space's advance (0 with no space); a colour tag is stepped over (no "]}": the walk ends); a bad sequence skips one byte.
inline float ui_font_text_width( const ui_font_t& f, const char* text, const std::uint8_t params[ 60 ], float aspect )
{
    float p[ 15 ];
    std::memcpy( p, params, 60 );
    const auto space_at = f.m_map.m_index.find( 0x20u );
    float tab = 0.0f;
    if ( space_at != f.m_map.m_index.end( ) && space_at->second != 0xFFFFu )
    {
        std::int32_t size = 0;
        std::memcpy( &size, params + 0x20, 4 );
        tab = static_cast< float >( size ) * f.m_glyphs[ space_at->second ].m_advance;
    }
    float best = 0.0f, cur = 0.0f;
    const char* s = text;
    while ( *s )
    {
        const char ch = *s;
        if ( ch == '\n' )
        {
            best = ui_font_maxss( best, cur );
            cur = 0.0f;
            ++s;
        }
        else if ( ch == '\t' )
        {
            cur = cur + tab;
            ++s;
        }
        else if ( ch == '{' && s[ 1 ] == '[' )
        {
            const char* end = std::strstr( s, "]}" );
            if ( !end )
                break;
            s = end + 2;
        }
        else
        {
            std::uint32_t code = 0;
            const int n = ui_utf8_read( s, code );
            if ( n == 0 )
            {
                ++s;
                continue;
            }
            const auto at = f.m_map.m_index.find( code );
            if ( at != f.m_map.m_index.end( ) && at->second != 0xFFFFu )
                cur = f.m_glyphs[ at->second ].m_advance + cur;
            s += n;
        }
    }
    return p[ 0 ] * 0.01f * p[ 9 ] / aspect * ui_font_maxss( best, cur );
}

// The device the draw talks to: g 0x75907C's slots and the shader program's constants. `program` = the set's +0 asset's +0x58.
struct ui_font_ops_t
{
    std::function< void( std::uint32_t texture ) > m_texture;                                  // vt+0xB4( 0, texture )
    std::function< void( const float rect[ 4 ] ) > m_scissor;                                  // vt+0x78
    std::function< void( bool on ) > m_scissor_on;                                             // vt+0xB0
    std::function< void( std::int32_t& a, std::int32_t& b ) > m_size;                          // vt+0x44
    std::function< void( std::uint32_t program ) > m_program;                                  // vt+0x84
    std::function< void( std::uint32_t program, std::uint32_t id, const float v[ 2 ] ) > m_vec2;          // the program's vt+0x20
    std::function< void( std::uint32_t program, std::uint32_t id, const std::uint8_t* v ) > m_colour;     // vt+0x30 (the record's +0x20)
    std::function< void( std::uint32_t program, std::uint32_t id, const float m[ 16 ] ) > m_matrix;       // vt+0x1C
    // vt+0xC4( 3, 0, 4 x quads, 2 x quads, the indices, vertices, 0x18 ): 0x18-byte vertices (position, uv, colour).
    std::function< void( int quads, const std::uint8_t* vertices ) > m_draw;
};

// A shader set (the array VuGfxUtil +0x14 +4, 0x18 bytes each): 0 plain, 1 outline, 2 the images'.
struct ui_font_shader_t
{
    std::uint32_t m_program = 0;   // +0
    std::uint32_t m_matrix = 0;    // +8
    std::uint32_t m_range = 0;     // +0xC
    std::uint32_t m_outline = 0;   // +0x10
    std::uint32_t m_colour = 0;    // +0x14
};

struct ui_font_image_t
{
    float m_matrix[ 16 ];
    std::uint8_t m_vertices[ 0x60 ];
    std::uint32_t m_texture;
};

// VuGfxUtil +0x14.
struct ui_font_renderer_t
{
    ui_font_shader_t m_sets[ 3 ];
    int m_current = 0;                        // +0
    std::vector< std::uint16_t > m_units;     // +8
    std::vector< ui_font_image_t > m_images;  // +0x44
    std::vector< std::uint8_t > m_vertices = std::vector< std::uint8_t >( 0x40000 );  // the static 0x799898
};

// 0x4078C0 out = a . b in retail's terms ( this, out, a3 ): out[ i j ] = sum a3[ k j ] x this[ i k ].
inline void ui_font_multiply( const float self[ 16 ], const float a3[ 16 ], float out[ 16 ] )
{
    for ( int i = 0; i < 4; ++i )
        for ( int j = 0; j < 4; ++j )
            out[ 4 * i + j ] = a3[ j ] * self[ 4 * i ] + a3[ 4 + j ] * self[ 4 * i + 1 ] + a3[ 8 + j ] * self[ 4 * i + 2 ] + a3[ 12 + j ] * self[ 4 * i + 3 ];
}

// 0x537FA0 (fastcall context, rect; flags): the first line's y.
inline float ui_font_first_y( const ui_font_context_t& c, const float rect[ 4 ], std::uint32_t flags )
{
    const ui_font_t& f = *c.m_font;
    if ( !( flags & 0x1Cu ) )
        return ( c.m_origin_y + f.m_ascent ) * c.m_scale_y + rect[ 1 ];
    int lines = 1;
    if ( !( flags & 0x40u ) )
        for ( const std::uint16_t u : *c.m_units )
            lines += u == 0xFFFEu ? 1 : 0;
    const float breaks = static_cast< float >( lines - 1 );
    if ( flags & 4u )
    {
        const float bottom = rect[ 3 ] + rect[ 1 ];
        return ( f.m_descent - c.m_origin_y ) * c.m_scale_y + bottom - breaks * c.m_scale_y;
    }
    if ( flags & 8u )
        return rect[ 3 ] + rect[ 1 ] - breaks * c.m_scale_y;
    const float s = c.m_scale_y;
    return rect[ 3 ] * 0.5f + rect[ 1 ] - ( f.m_ascent - f.m_descent + breaks ) * s * 0.5f + s * f.m_ascent;
}

// 0x5380B0 (fastcall context, the unit index): the line from there.
inline float ui_font_line_width( const ui_font_context_t& c, int index )
{
    float sum = 0.0f;
    for ( std::size_t i = static_cast< std::size_t >( index ); i < c.m_units->size( ); ++i )
    {
        const std::uint16_t u = ( *c.m_units )[ i ];
        if ( u >= 0xFFF0u )
            break;
        sum = sum + c.m_font->m_glyphs[ u ].m_advance;
    }
    return ( c.m_origin_x + sum ) * c.m_scale_x;
}

// 0x538680: past the line starting at `index`, the colour units still taken. Answers the index after the break.
inline int ui_font_skip_line( ui_font_context_t& c, std::uint32_t default_colour, int index )
{
    const std::vector< std::uint16_t >& units = *c.m_units;
    while ( index < static_cast< int >( units.size( ) ) )
    {
        const std::uint16_t u = units[ static_cast< std::size_t >( index ) ];
        ++index;
        if ( u < 0xFFF0u )
            continue;
        if ( u == 0xFFFEu )
            break;
        if ( u == 0xFFFDu )
            c.m_colour = default_colour;
        else if ( u <= 0xFFF9u )
            c.m_colour = c.m_palette[ u - 0xFFF0u ];
    }
    return index;
}

// 0x538110: one line's quads from unit `index`, its draw and the images' records.
inline void ui_font_line( ui_font_renderer_t& R, ui_font_context_t& c, const ui_font_ops_t& ops, std::uint32_t default_colour, float slant, const float rect[ 4 ], std::uint32_t flags, float y, int& index, float z,
                          float x_offset )
{
    const ui_font_t& f = *c.m_font;
    const std::vector< std::uint16_t >& units = R.m_units;
    if ( units[ static_cast< std::size_t >( index ) ] == 0xFFFEu )
    {
        ++index;
        return;
    }
    if ( c.m_clip[ 1 ] > y - ( f.m_descent - c.m_origin_y ) * c.m_scale_y || y - ( f.m_ascent + c.m_origin_y ) * c.m_scale_y > c.m_clip[ 3 ] + c.m_clip[ 1 ] )
    {
        index = ui_font_skip_line( c, default_colour, index );
        return;
    }
    const float width = ui_font_line_width( c, index );
    float x;
    if ( !( flags & 3u ) )
        x = rect[ 0 ];
    else if ( flags & 1u )
        x = rect[ 2 ] + rect[ 0 ] - width;
    else
        x = rect[ 2 ] * 0.5f + rect[ 0 ] - width * 0.5f;
    x = x + x_offset;
    if ( c.m_clip[ 0 ] > x + width || x > c.m_clip[ 0 ] + c.m_clip[ 2 ] )
    {
        index = ui_font_skip_line( c, default_colour, index );
        return;
    }
    const float identity[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const float scale[ 16 ] = { c.m_scale_x, 0, 0, 0, 0, c.m_scale_y, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    const float move[ 16 ] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, 0, 1 };
    float scaled[ 16 ], m[ 16 ];
    ui_font_multiply( identity, scale, scaled );
    ui_font_multiply( scaled, move, m );
    const float inverse = 1.0f / c.m_scale_x;
    const float left = ( c.m_clip[ 0 ] - x ) * inverse;
    const float right = ( c.m_clip[ 0 ] + c.m_clip[ 2 ] - x ) * inverse;
    float pen = 0.0f;
    std::uint8_t* out = R.m_vertices.data( );
    std::uint32_t room = 0x40000u;
    int quads = 0;
    const int count = static_cast< int >( units.size( ) );
    while ( index < count )
    {
        const std::uint16_t u = units[ static_cast< std::size_t >( index ) ];
        ++index;
        if ( u >= 0xFFF0u )
        {
            if ( u == 0xFFFEu )
                break;
            if ( u == 0xFFFDu )
                c.m_colour = default_colour;
            else if ( u <= 0xFFF9u )
                c.m_colour = c.m_palette[ u - 0xFFF0u ];
            continue;
        }
        const ui_font_glyph_t& g = f.m_glyphs[ u ];
        if ( pen > right )
        {
            index = ui_font_skip_line( c, default_colour, index );
            break;
        }
        if ( pen + g.m_advance >= left && room >= 0x60u )
        {
            const float px[ 4 ] = { pen + g.m_x[ 0 ] - g.m_y[ 0 ] * slant, pen + g.m_x[ 0 ] - g.m_y[ 1 ] * slant, g.m_x[ 1 ] + pen - g.m_y[ 1 ] * slant, g.m_x[ 1 ] + pen - g.m_y[ 0 ] * slant };
            const float py[ 4 ] = { g.m_y[ 0 ], g.m_y[ 1 ], g.m_y[ 1 ], g.m_y[ 0 ] };
            const float pu[ 4 ] = { g.m_u[ 0 ], g.m_u[ 0 ], g.m_u[ 1 ], g.m_u[ 1 ] };
            const float pv[ 4 ] = { g.m_v[ 0 ], g.m_v[ 1 ], g.m_v[ 1 ], g.m_v[ 0 ] };
            for ( int k = 0; k < 4; ++k )
            {
                std::memcpy( out + 0x18 * k, &px[ k ], 4 );
                std::memcpy( out + 0x18 * k + 4, &py[ k ], 4 );
                std::memcpy( out + 0x18 * k + 8, &z, 4 );
                std::memcpy( out + 0x18 * k + 0xC, &pu[ k ], 4 );
                std::memcpy( out + 0x18 * k + 0x10, &pv[ k ], 4 );
                std::memcpy( out + 0x18 * k + 0x14, &c.m_colour, 4 );
            }
            if ( g.m_has_image )
            {
                ui_font_image_t image;
                std::memcpy( image.m_matrix, m, sizeof( m ) );
                std::memcpy( image.m_vertices, out, 0x60 );
                image.m_texture = g.m_image < f.m_image_textures.size( ) ? f.m_image_textures[ g.m_image ] : 0u;
                R.m_images.push_back( image );
            }
            else
            {
                room -= 0x60u;
                out += 0x60;
                ++quads;
            }
        }
        pen = g.m_advance + pen;
    }
    if ( quads == 0 )
        return;
    const ui_font_shader_t& set = R.m_sets[ R.m_current ];
    if ( ops.m_matrix )
        ops.m_matrix( set.m_program, set.m_matrix, m );
    if ( ops.m_draw )
        ops.m_draw( quads, R.m_vertices.data( ) );
}

// 0x538700: the glyphs that are images, through the third set, then the set back.
inline void ui_font_flush_images( ui_font_renderer_t& R, const ui_font_ops_t& ops )
{
    if ( R.m_images.empty( ) )
        return;
    const ui_font_shader_t& set = R.m_sets[ 2 ];
    if ( ops.m_program )
        ops.m_program( set.m_program );
    for ( const ui_font_image_t& image : R.m_images )
    {
        if ( ops.m_texture )
            ops.m_texture( image.m_texture );
        if ( ops.m_matrix )
            ops.m_matrix( set.m_program, set.m_matrix, image.m_matrix );
        if ( ops.m_draw )
            ops.m_draw( 1, image.m_vertices );
    }
    if ( ops.m_program )
        ops.m_program( R.m_sets[ R.m_current ].m_program );
    R.m_images.clear( );
}

// 0x537600 (thiscall the renderer; the record, the text; ret 8), through 0x5375E0 ( the record, its +0x78 ): the record as
// draw.hxx's ui_font_draw wrote it.
inline void ui_font_submit( ui_font_renderer_t& R, const ui_font_t& f, const std::uint8_t* r, const ui_font_ops_t& ops )
{
    const auto fl = [ r ]( int at ) { float v; std::memcpy( &v, r + at, 4 ); return v; };
    const auto u4 = [ r ]( int at ) { std::uint32_t v; std::memcpy( &v, r + at, 4 ); return v; };
    const std::uint32_t flags = u4( 0x54 );
    const char* text = reinterpret_cast< const char* >( r + 0x78 );
    R.m_current = static_cast< int >( u4( 4 ) );
    if ( ops.m_texture )
        ops.m_texture( f.m_texture );
    float clip[ 4 ] = { fl( 0x68 ), fl( 0x6C ), fl( 0x70 ), fl( 0x74 ) };
    bool clipped = r[ 0x30 ] != 0;
    if ( !( flags & 0x20u ) )
    {
        const float x0 = ui_font_maxss( clip[ 0 ], fl( 0x44 ) );
        const float x1 = ui_font_minss( clip[ 0 ] + clip[ 2 ], fl( 0x4C ) + fl( 0x44 ) );
        const float y0 = ui_font_maxss( clip[ 1 ], fl( 0x48 ) );
        const float y1 = ui_font_minss( clip[ 1 ] + clip[ 3 ], fl( 0x50 ) + fl( 0x48 ) );
        clip[ 0 ] = x0;
        clip[ 1 ] = y0;
        clip[ 2 ] = x1 - x0;
        clip[ 3 ] = y1 - y0;
        clipped = true;
    }
    const float epsilon = 1.1920928955078125e-07f;   // 0x6F7584
    if ( epsilon > clip[ 2 ] || epsilon > clip[ 3 ] )
        return;
    if ( clipped )
    {
        if ( ops.m_scissor )
            ops.m_scissor( clip );
        if ( ops.m_scissor_on )
            ops.m_scissor_on( true );
    }
    else if ( ops.m_scissor_on )
        ops.m_scissor_on( false );
    std::int32_t a = 0, b = 0;
    if ( ops.m_size )
        ops.m_size( a, b );
    const float aspect = static_cast< float >( a ) / static_cast< float >( b );
    const float size = fl( 8 ) * 0.01f;
    const float scale_x = fl( 0x2C ) * size / aspect;
    const float inverse = 1.0f / ( f.m_scale * 2.0f * size * 720.0f );
    const float edge = fl( 0x18 ) * inverse;
    const float soft = edge > 0.0f ? fl( 0x1C ) * inverse : 0.0f;
    const float half = fl( 0x10 ) * inverse * 0.5f;
    const float centre = 1.0f - fl( 0xC ) * 0.01f * 0.5f - edge;
    const float range[ 2 ] = { centre - half, half + centre };
    const float origin_y = ( 0.5f - range[ 0 ] ) * 2.0f * f.m_scale;
    const float origin_x = f.m_ascent * fl( 0x24 ) + origin_y;
    const float step_x = fl( 0x58 ) * scale_x;
    const float step_y = fl( 0x58 ) * size;
    const ui_font_shader_t& set = R.m_sets[ R.m_current ];
    if ( ops.m_vec2 )
        ops.m_vec2( set.m_program, set.m_range, range );
    if ( set.m_outline && ops.m_vec2 )
    {
        const float outline[ 2 ] = { centre + edge, centre + edge + soft };
        ops.m_vec2( set.m_program, set.m_outline, outline );
    }
    if ( set.m_colour && ops.m_colour )
        ops.m_colour( set.m_program, set.m_colour, r + 0x20 );
    std::string wrapped;
    if ( ( flags & 0x80u ) && !( flags & 0x40u ) )
    {
        wrapped = ui_font_wrap( f.m_map, text, fl( 0x4C ) / step_x - origin_x );
        text = wrapped.c_str( );
    }
    ui_font_context_t c;
    c.m_font = &f;
    std::memcpy( c.m_clip, clip, 16 );
    c.m_scale_x = step_x;
    c.m_scale_y = step_y;
    c.m_origin_x = origin_x;
    c.m_origin_y = origin_y;
    c.m_units = &R.m_units;
    const std::uint32_t colour = u4( 0x14 );
    c.m_colour = colour;
    ui_font_units( c, text, static_cast< std::int32_t >( u4( 0x28 ) ), flags );
    const float rect[ 4 ] = { fl( 0x44 ), fl( 0x48 ), fl( 0x4C ), fl( 0x50 ) };
    float y = ui_font_first_y( c, rect, flags ) + fl( 0x64 );
    int index = 0;
    while ( index < static_cast< int >( R.m_units.size( ) ) )
    {
        ui_font_line( R, c, ops, colour, fl( 0x24 ), rect, flags, y, index, fl( 0x5C ), fl( 0x60 ) );
        y = y + step_y;
    }
    ui_font_flush_images( R, ops );
    if ( clipped && ops.m_scissor_on )
        ops.m_scissor_on( false );
}
