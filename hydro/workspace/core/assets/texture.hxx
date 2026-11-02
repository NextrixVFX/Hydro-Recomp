#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// PC VuTextureAsset. The first mip is linear DXGI block compression.
// 0x47 is BC1 and 0x4D is BC3. Width and height sit at byte 20.
struct image_t
{
    int width = 0;
    int height = 0;
    std::vector< unsigned char > rgba;
};

namespace
{
    void texture_rgb565( unsigned short packed, unsigned char* pixel )
    {
        const int red = ( packed >> 11 ) & 31;
        const int green = ( packed >> 5 ) & 63;
        const int blue = packed & 31;
        pixel[ 0 ] = static_cast< unsigned char >( red * 255 / 31 );
        pixel[ 1 ] = static_cast< unsigned char >( green * 255 / 63 );
        pixel[ 2 ] = static_cast< unsigned char >( blue * 255 / 31 );
        pixel[ 3 ] = 255;
    }

    void texture_bc3_alpha( const unsigned char* block, unsigned char* alpha )
    {
        const int a0 = block[ 0 ];
        const int a1 = block[ 1 ];
        unsigned long long bits = 0;
        for ( int byte = 0; byte < 6; ++byte )
            bits |= static_cast< unsigned long long >( block[ 2 + byte ] ) << ( 8 * byte );

        int table[ 8 ] = { a0, a1 };
        if ( a0 > a1 )
        {
            for ( int index = 1; index < 7; ++index )
                table[ index + 1 ] = ( ( 7 - index ) * a0 + index * a1 ) / 7;
        }
        else
        {
            for ( int index = 1; index < 5; ++index )
                table[ index + 1 ] = ( ( 5 - index ) * a0 + index * a1 ) / 5;
            table[ 6 ] = 0;
            table[ 7 ] = 255;
        }

        for ( int pixel = 0; pixel < 16; ++pixel )
            alpha[ pixel ] = static_cast< unsigned char >( table[ ( bits >> ( 3 * pixel ) ) & 7 ] );
    }

    // Decode one linear block-compressed surface into RGBA. Shared by the 2D
    // textures and the cube faces, which carry identical block data and differ
    // only in their headers.
    void texture_surface( const unsigned char* payload, int width, int height,
        unsigned format, unsigned char* rgba )
    {
        const int blocks_x = ( width + 3 ) / 4;
        const int blocks_y = ( height + 3 ) / 4;
        const int stride = format == 0x4Du ? 16 : 8;

        for ( int block_y = 0; block_y < blocks_y; ++block_y )
        {
            for ( int block_x = 0; block_x < blocks_x; ++block_x )
            {
                const unsigned char* block = payload + ( static_cast< std::size_t >( block_y ) * blocks_x + block_x ) * stride;
                unsigned char alpha[ 16 ];
                const unsigned char* color = block;
                if ( format == 0x4Du )
                {
                    texture_bc3_alpha( block, alpha );
                    color = block + 8;
                }
                else
                {
                    for ( int pixel = 0; pixel < 16; ++pixel )
                        alpha[ pixel ] = 255;
                }

                const unsigned short c0 = static_cast< unsigned short >( color[ 0 ] | ( color[ 1 ] << 8 ) );
                const unsigned short c1 = static_cast< unsigned short >( color[ 2 ] | ( color[ 3 ] << 8 ) );
                const unsigned bits = static_cast< unsigned >( color[ 4 ] )
                    | ( static_cast< unsigned >( color[ 5 ] ) << 8 )
                    | ( static_cast< unsigned >( color[ 6 ] ) << 16 )
                    | ( static_cast< unsigned >( color[ 7 ] ) << 24 );

                unsigned char palette[ 4 ][ 4 ];
                texture_rgb565( c0, palette[ 0 ] );
                texture_rgb565( c1, palette[ 1 ] );
                if ( c0 > c1 )
                {
                    for ( int channel = 0; channel < 4; ++channel )
                    {
                        palette[ 2 ][ channel ] = static_cast< unsigned char >( ( 2 * palette[ 0 ][ channel ] + palette[ 1 ][ channel ] ) / 3 );
                        palette[ 3 ][ channel ] = static_cast< unsigned char >( ( palette[ 0 ][ channel ] + 2 * palette[ 1 ][ channel ] ) / 3 );
                    }
                }
                else
                {
                    for ( int channel = 0; channel < 4; ++channel )
                        palette[ 2 ][ channel ] = static_cast< unsigned char >( ( palette[ 0 ][ channel ] + palette[ 1 ][ channel ] ) / 2 );
                    palette[ 3 ][ 0 ] = palette[ 3 ][ 1 ] = palette[ 3 ][ 2 ] = 0;
                    palette[ 3 ][ 3 ] = 0;
                }

                for ( int py = 0; py < 4; ++py )
                {
                    for ( int px = 0; px < 4; ++px )
                    {
                        const int x = block_x * 4 + px;
                        const int y = block_y * 4 + py;
                        if ( x >= width || y >= height )
                            continue;
                        const int index = ( bits >> ( 2 * ( py * 4 + px ) ) ) & 3;
                        unsigned char* dest = rgba + ( static_cast< std::size_t >( y ) * width + x ) * 4;
                        dest[ 0 ] = palette[ index ][ 0 ];
                        dest[ 1 ] = palette[ index ][ 1 ];
                        dest[ 2 ] = palette[ index ][ 2 ];
                        dest[ 3 ] = format == 0x4Du ? alpha[ py * 4 + px ] : palette[ index ][ 3 ];
                    }
                }
            }
        }
    }
}

inline bool decode_texture( const std::vector< std::uint8_t >& blob, image_t& image )
{
    image = {};
    if ( blob.size( ) < 64 )
        return false;

    const int width = static_cast< int >( blob[ 20 ] | ( blob[ 21 ] << 8 ) | ( blob[ 22 ] << 16 ) | ( blob[ 23 ] << 24 ) );
    const int height = static_cast< int >( blob[ 24 ] | ( blob[ 25 ] << 8 ) | ( blob[ 26 ] << 16 ) | ( blob[ 27 ] << 24 ) );
    const unsigned format = blob[ 32 ] | ( blob[ 33 ] << 8 ) | ( blob[ 34 ] << 16 ) | ( blob[ 35 ] << 24 );
    if ( width <= 0 || height <= 0 || width > 8192 || height > 8192 )
        return false;
    if ( format == 0x3Du )   // DXGI R8_UNORM (the minimap's Maps/*): the byte in the red lane, as the device reads it
    {
        const std::size_t texels = static_cast< std::size_t >( width ) * height;
        if ( blob.size( ) < 56 + texels )
            return false;
        image.width = width;
        image.height = height;
        image.rgba.assign( texels * 4, 0 );
        for ( std::size_t k = 0; k < texels; ++k )
        {
            image.rgba[ k * 4 ] = blob[ 56 + k ];
            image.rgba[ k * 4 + 3 ] = 0xFF;
        }
        return true;
    }
    if ( format != 0x47u && format != 0x4Du )
        return false;

    const std::size_t need = 56
        + static_cast< std::size_t >( ( width + 3 ) / 4 ) * ( ( height + 3 ) / 4 )
        * ( format == 0x4Du ? 16 : 8 );
    if ( blob.size( ) < need )
        return false;

    image.width = width;
    image.height = height;
    image.rgba.assign( static_cast< std::size_t >( width ) * height * 4, 0 );
    texture_surface( blob.data( ) + 56, width, height, format, image.rgba.data( ) );
    return true;
}

// VuCubeTextureAsset, transliterated from VuCubeTexture_createFromStream
// @0x609460, reached through VuCubeTextureAsset_create @0x5adf50 -> the ctor
// @0x5ade70 -> the vtable @0x7513ac slot 5. The layout is NOT a fixed-size
// header followed by six equal faces; each face carries its own header and a
// length prefix, so the file is self-describing:
//
//   offset  0   five u32   read by sub_5A8A50 before anything else - texture
//                          metadata, carried into the texture object
//   offset 20   u32 edge   face size
//   offset 24   u32 mips
//   offset 28   u32 format DXGI (0x47 = BC1 on every cube in the pack)
//   offset 32   six faces, each:
//                   u32 code, width, height, levels     (the image header)
//                   u32 byteCount                       (sub_5AC7F0)
//                   byte payload[ byteCount ]           (that face's mip chain)
//
// An earlier decoder assumed a flat 152-byte header and a uniform face stride.
// 152 is really 32 + 6*20: it had folded all six per-face headers into one
// leading block, so face 0 started 100 bytes late and every later face was
// displaced again. That is what produced the rainbow.
//
// `code` is the engine's own format enum from sub_6061E0, not the DXGI value:
// 1,2 = 4 B/px; 3 = 3; 4,11,12,13,14 = 2; 5 = 1; 6,7 = 8 B per 4x4 block;
// 8 = 16 B per block; 9,10 = (4*max(w,8)*max(h,8)+7)/8.
//
// Only the top mip is decoded: the sampler generates its own chain, and the
// Intel HD 505 target gains nothing from uploading eight levels of each face.

// Bytes for one mip level, from the switch in sub_6061E0 @0x6061e0.
inline std::size_t cube_mip_bytes( unsigned code, int width, int height )
{
    const std::size_t w = static_cast< std::size_t >( width > 1 ? width : 1 );
    const std::size_t h = static_cast< std::size_t >( height > 1 ? height : 1 );
    const std::size_t blocks = ( ( w + 3 ) / 4 ) * ( ( h + 3 ) / 4 );
    switch ( code )
    {
    case 1: case 2:                       return 4 * w * h;
    case 3:                               return 3 * w * h;
    case 4: case 11: case 12: case 13: case 14: return 2 * w * h;
    case 5:                               return w * h;
    case 6: case 7:                       return 8 * blocks;
    case 8:                               return 16 * blocks;
    case 9: case 10:
    {
        const std::size_t cw = w > 8 ? w : 8;
        const std::size_t ch = h > 8 ? h : 8;
        return ( 4 * cw * ch + 7 ) / 8;
    }
    default:                              return 0;
    }
}
struct cube_t
{
    int face = 0;
    std::vector< unsigned char > rgba[ 6 ];
};

inline bool decode_cube( const std::vector< std::uint8_t >& blob, cube_t& cube )
{
    cube = {};
    if ( blob.size( ) < 32 )
        return false;

    std::uint32_t head[ 8 ]{};
    std::memcpy( head, blob.data( ), sizeof( head ) );
    const int edge = static_cast< int >( head[ 5 ] );
    const int mips = static_cast< int >( head[ 6 ] );
    const unsigned format = head[ 7 ];

    if ( edge <= 0 || edge > 4096 || mips <= 0 || mips > 14 )
        return false;
    if ( format != 0x47u && format != 0x4Du )
        return false;

    cube.face = edge;
    std::size_t offset = 32;
    for ( int index = 0; index < 6; ++index )
    {
        if ( offset + 20 > blob.size( ) )
            return false;
        std::uint32_t image[ 5 ]{};
        std::memcpy( image, blob.data( ) + offset, sizeof( image ) );
        offset += sizeof( image );

        const unsigned code = image[ 0 ];
        const int width = static_cast< int >( image[ 1 ] );
        const int height = static_cast< int >( image[ 2 ] );
        const int levels = static_cast< int >( image[ 3 ] );
        const std::size_t count = image[ 4 ];

        if ( width != edge || height != edge || levels <= 0 || levels > 14 )
            return false;

        // The face declares its own payload length, and the mip chain implied
        // by its own header must account for exactly that many bytes. A header
        // read at the wrong offset cannot satisfy this.
        std::size_t expect = 0;
        for ( int level = 0; level < levels; ++level )
            expect += cube_mip_bytes( code, width >> level, height >> level );
        if ( expect != count || offset + count > blob.size( ) )
            return false;

        cube.rgba[ index ].assign( static_cast< std::size_t >( edge ) * edge * 4, 0 );
        texture_surface( blob.data( ) + offset, edge, edge, format, cube.rgba[ index ].data( ) );
        offset += count;
    }
    // A correct parse lands on the last byte. This is the check that would have
    // caught the old flat-stride layout immediately.
    return offset == blob.size( );
}
