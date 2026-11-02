#pragma once

#include <workspace/core/assets/reader.hxx>
#include <workspace/core/save/seam.hxx>
#include <workspace/core/save/writer.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// VuMetroProfileManager (vtable 0x74EB24, 0x30 bytes, ctor 0x554740, g 0x759084): the settings document, the local file
// "profile". +8 the document, +0x18 a second container nothing here touches (dtor 0x5547A0 only), +0x28 the load's answer.
// The file: 'RPUV' 0x56555052, version 1, the payload's size, its FNV-1a (0x4ABB40), then the payload, a VUJB document.
enum : int
{
    save_ok = 0,
    save_missing = 1,
    save_corrupt = 2,
};

// 0x554B50: 1 no file; 2 a bad header, a short read, a bad hash or a document that does not parse; 0 the document in
// `out`. Anything but 0 clears `out`.
inline int profile_read( const std::string& name, value_t& out )
{
    save_seam::file_t* file = save_seam::open( name, 0 );
    if ( file == nullptr )
        return save_missing;
    int status = save_corrupt;
    const std::uint32_t size = save_seam::size( file ) - 16;
    std::uint32_t head[ 4 ]{};
    if ( save_seam::read( file, head, 16 ) == 16 && head[ 0 ] == 0x56555052u && head[ 1 ] == 1 && head[ 2 ] == size )
    {
        std::vector< std::uint8_t > bytes( size );
        if ( save_seam::read( file, bytes.data( ), size ) == size && head[ 3 ] == save_hash( bytes.data( ), size ) )
        {
            value_t document;
            if ( c_reader::read_at_start( bytes, document ) )
            {
                out = std::move( document );
                status = save_ok;
            }
        }
    }
    save_seam::close( file );
    if ( status != save_ok )
        out = value_t{ };
    return status;
}

// 0x55B4B0: the whole file as a bare VUJB document, no header (0x55B560's answer); a missing file false.
inline bool profile_read_bare( const std::string& name, value_t& out )
{
    save_seam::file_t* file = save_seam::open( name, 0 );
    if ( file == nullptr )
        return false;
    const std::uint32_t size = save_seam::size( file );
    std::vector< std::uint8_t > bytes( size );
    save_seam::read( file, bytes.data( ), size );
    const bool ok = c_reader::read_at_start( bytes, out );
    save_seam::close( file );
    return ok;
}

// 0x554CD0: the header and the document, two writes; false when the document or a write failed (the file then holds
// what was written). A file that does not open: false.
inline bool profile_write( const std::string& name, const value_t& root )
{
    std::vector< std::uint8_t > bytes;
    std::uint32_t used = 0;
    if ( !vujb_document( root, bytes, used ) )
        return false;
    const std::uint32_t head[ 4 ] = { 0x56555052u, 1, used, save_hash( bytes.data( ), used ) };
    save_seam::file_t* file = save_seam::open( name, 1 );
    if ( file == nullptr )
        return false;
    bool ok = false;
    if ( save_seam::write( file, head, 16 ) == 16 )
        ok = save_seam::write( file, bytes.data( ), used ) == used;
    save_seam::close( file );
    return ok;
}

class c_profile_manager
{
public:
    value_t data;    // +8
    int status = 0;  // +0x28

    // Slot 5 0x5547E0 -> 0x554850, the load; true. A corrupt headered file (2) is tried once as a bare document (the flag
    // 0x759848 it also tests has no writer: always 0) and clears the status if that parses; the document it left stays.
    void load( )
    {
        status = profile_read( "profile", data );
        if ( status == save_corrupt && profile_read_bare( "profile", data ) )
            status = save_ok;
    }

    // 0x554A30.
    void store( ) const
    {
        profile_write( "profile", data );
    }

    // 0x5547F0, the save: "OnSaveProfile" to every listener (0x505690, FNV of the name; retail's listeners are
    // VuInputManagerImpl 0x5111B0, VuSettingsManager and VuCloudManager: they add theirs to `data`), then the store.
    void save( const std::function< void( ) >& broadcast )
    {
        if ( broadcast )
            broadcast( );
        store( );
    }
};
