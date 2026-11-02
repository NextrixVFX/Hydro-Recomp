#pragma once

#include <workspace/core/assets/reader.hxx>
#include <workspace/core/save/deflate.hxx>
#include <workspace/core/save/profile.hxx>
#include <workspace/core/save/seam.hxx>
#include <workspace/core/save/writer.hxx>

#include <impl/inflate/puff.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// VuCloudSaveManager (ctor 0x4ABBC0, VuMetroCloudSaveManager 0x492F70, g 0x758FBC): THE GAME SAVE, despite the name. The
// document at +0x20 is every piece of progress (Unlock.TotalCredits and the rest); it lives in a local file, "cloudsave"
// by default, and retail also mirrors it to Xbox title storage. Local only here (the Saves decision): the title storage
// half is out - the GET on sign-in 0x4ABDC0 with its conflict box 0x4ABE80 / 0x4AC290, and the PUT at the end of 0x4AC6C0 -
// because the sign-in manager never signs in, so 0x4ABE70's plain file name stands.
//   +0x10 the file's bytes as last read or written, +0x20 the document, +0x50 the load's answer (save_ok / save_missing /
//   save_corrupt), +0x54 the file name.
// The file: 'SCUV' 0x56554353, version 2, the rest's size, its FNV-1a, then a zlib stream of the VUJB document.
struct cloud_hooks_t
{
    std::function< void( const char* ) > broadcast;  // 0x505690, "OnCloudSaveDataChanged" / "OnCloudSavePreSave"
    std::function< void( ) > corrupt_box;            // SaveGame_Corrupt_Heading / _Body / MessageBox_OK, UI lane
};

// 0x5BD7E0 uncompress into the 0x40000-byte buffer 0x759898. 0 on a whole stream that fits. SUBSTITUTION: puff + the zlib
// header and adler-32 tests for zlib's inflate, as archive.hxx.
inline bool cloud_inflate( const std::uint8_t* in, std::size_t size, std::vector< std::uint8_t >& out )
{
    out.assign( 0x40000, 0 );
    if ( size < 2 )
        return false;
    const unsigned header = static_cast< unsigned >( in[ 0 ] ) * 256u + in[ 1 ];
    if ( ( in[ 0 ] & 0x0F ) != 8 || ( in[ 0 ] >> 4 ) > 7 || ( header % 31u ) != 0 || ( in[ 1 ] & 0x20 ) != 0 )
        return false;
    unsigned long dest_len = 0x40000;
    unsigned long source_len = static_cast< unsigned long >( size - 2 );
    if ( puff( out.data( ), &dest_len, in + 2, &source_len ) != 0 )
        return false;
    const std::size_t tail = 2 + source_len;
    if ( tail + 4 > size )
        return false;
    out.resize( dest_len );
    std::uint32_t a = 1, b = 0;
    for ( std::uint8_t byte : out )
    {
        a = ( a + byte ) % 65521u;
        b = ( b + a ) % 65521u;
    }
    const std::uint32_t want = ( static_cast< std::uint32_t >( in[ tail ] ) << 24 ) | ( static_cast< std::uint32_t >( in[ tail + 1 ] ) << 16 ) |
                               ( static_cast< std::uint32_t >( in[ tail + 2 ] ) << 8 ) | in[ tail + 3 ];
    return ( ( b << 16 ) | a ) == want;
}

// 0x4ACAB0: 16 header bytes, the deflated document, then the header over the rest.
inline std::vector< std::uint8_t > cloud_encode( const value_t& root )
{
    std::vector< std::uint8_t > document;
    std::uint32_t used = 0;
    const bool ok = vujb_document( root, document, used );
    if ( !ok )
        used = static_cast< std::uint32_t >( document.size( ) );
    std::vector< std::uint8_t > packed = c_deflate::compress( document.data( ), used );
    std::vector< std::uint8_t > blob( 16 + packed.size( ) );
    std::memcpy( blob.data( ) + 16, packed.data( ), packed.size( ) );
    const std::uint32_t size = static_cast< std::uint32_t >( packed.size( ) );
    const std::uint32_t head[ 4 ] = { 0x56554353u, 2, size, save_hash( packed.data( ), size ) };
    std::memcpy( blob.data( ), head, 16 );
    return blob;
}

// 0x4AC930. 0x55B560's own refusals (a short document, the magic, the version) leave `root` alone; a bad node clears it.
inline bool cloud_decode( const std::vector< std::uint8_t >& blob, value_t& root )
{
    if ( blob.size( ) < 16 )
        return false;
    std::uint32_t head[ 4 ];
    std::memcpy( head, blob.data( ), 16 );
    const std::uint32_t size = static_cast< std::uint32_t >( blob.size( ) ) - 16;
    if ( head[ 0 ] != 0x56554353u || head[ 1 ] != 2 || head[ 2 ] != size || head[ 3 ] != save_hash( blob.data( ) + 16, size ) )
        return false;
    std::vector< std::uint8_t > document;
    if ( !cloud_inflate( blob.data( ) + 16, size, document ) )
        return false;
    value_t parsed;
    if ( c_reader::read_at_start( document, parsed ) )
    {
        root = std::move( parsed );
        return true;
    }
    const auto u32 = [ & ]( std::size_t at ) {
        return static_cast< std::uint32_t >( document[ at ] ) << 24 | static_cast< std::uint32_t >( document[ at + 1 ] ) << 16 |
               static_cast< std::uint32_t >( document[ at + 2 ] ) << 8 | document[ at + 3 ];
    };
    if ( document.size( ) >= 8 && u32( 0 ) == 0x56554A42u && u32( 4 ) == 1 )
        root = value_t{ };
    return false;
}

class c_cloud_save
{
public:
    std::vector< std::uint8_t > blob;  // +0x10
    value_t data;                      // +0x20
    int status = 0;                    // +0x50
    std::string name = "cloudsave";    // +0x54
    cloud_hooks_t hooks;

    // 0x4ABD20 (slot 5): the load, then the sign-in listener (out).
    void init( )
    {
        load( );
    }

    // 0x4AC5C0 (0x55B260 the whole file).
    void load( )
    {
        blob.clear( );
        save_seam::file_t* file = save_seam::open( name, 0 );
        bool read = false;
        if ( file != nullptr )
        {
            const std::uint32_t size = save_seam::size( file );
            blob.resize( size );
            read = save_seam::read( file, blob.data( ), size ) == size;
            save_seam::close( file );
        }
        if ( read )
            status = cloud_decode( blob, data ) ? save_ok : save_corrupt;
        else
            status = save_missing;
    }

    // 0x4AC480: the file changes (0x4ABE70 "cloudsave" signed out, 0x4ABDC0 "%llu_cloudsave" signed in): the document is cleared
    // and read again, listeners told; a corrupt file puts up the box and counts as clean.
    void change_file( const std::string& next )
    {
        name = next;
        data = value_t{ };
        load( );
        if ( hooks.broadcast )
            hooks.broadcast( "OnCloudSaveDataChanged" );
        if ( status == save_corrupt )
        {
            if ( hooks.corrupt_box )
                hooks.corrupt_box( );
            status = save_ok;
        }
    }

    // 0x4ABD60, the save: "OnCloudSavePreSave" (the game's listeners write into `data`), then the store.
    void save( )
    {
        if ( hooks.broadcast )
            hooks.broadcast( "OnCloudSavePreSave" );
        store( );
    }

    // 0x4AC6C0: bytes the same as +0x10: nothing. Else +0x10 replaced and the file written. A failed open or a short write has
    // no answer in retail: +0x10 stays replaced.
    void store( )
    {
        std::vector< std::uint8_t > next = cloud_encode( data );
        if ( next == blob )
            return;
        blob = std::move( next );
        save_seam::file_t* file = save_seam::open( name, 1 );
        if ( file != nullptr )
        {
            save_seam::write( file, blob.data( ), static_cast< std::uint32_t >( blob.size( ) ) );
            save_seam::close( file );
        }
    }
};
