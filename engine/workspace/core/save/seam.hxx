#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

// THE ONE LOCAL FILE SEAM of the save path (the user's Saves decision, 11-01): every save, ghosts included, opens, reads,
// writes and closes its file here and nowhere else, so a later backend (online saves, local fallback when the server is
// down) replaces this one file. Platform layer: a hook on the shape of retail's VuGenericFile slots, not copied.
//   slot 13 0x5BCE80 / slot 14 0x5BCF40 open: mode 0 "rb", 1 "wb", 2 "rb+"; null when the file does not open
//   slot 11 0x5BD110 size   slot 15 0x5BD040 close   slot 16 0x5BD090 read   slot 17 0x5BD0B0 write
//   slots 6 / 7 0x5BCB90 / 0x5BCC60 pushRootPath / popRootPath: retail's only root here is 0x736980, the app's own data
//   folder (VuProfileManager slot 6 0x4930B0), so the push and pop are the root below.
// The root is %APPDATA%\HydroThunderHurricane\ . NOT 1:1 by design: retail's folder is the package's; a write creates
// the folders it needs (retail: slot 22 0x5BD4B0 on the way there).
//
// Tests: redirect( ) points the seam at a temporary directory. The self-test flavour (HYDRO_GOLDEN_PRESENT) has NO default
// root: until a check redirects it, open answers null, so no check can reach the user's real save folder.
namespace save_seam
{
    struct file_t
    {
        std::FILE* stream = nullptr;
    };

    inline std::filesystem::path& override_root( )
    {
        static std::filesystem::path path;
        return path;
    }

    // The test override (empty restores the default).
    inline void redirect( const std::filesystem::path& path )
    {
        override_root( ) = path;
    }

    // Where the saves live; empty when there is nowhere to put them.
    inline std::filesystem::path root( )
    {
        if ( !override_root( ).empty( ) )
            return override_root( );
#if defined( HYDRO_GOLDEN_PRESENT )
        return { };
#else
        std::size_t needed = 0;
        wchar_t value[ 1024 ];
        if ( _wgetenv_s( &needed, value, L"APPDATA" ) != 0 || needed == 0 )
            return { };
        return std::filesystem::path( value ) / L"HydroThunderHurricane";
#endif
    }

    // A save's name is a UTF-8 path relative to the root ("profile", "cloudsave", "ghost/...").
    inline std::filesystem::path full_path( const std::string& name )
    {
        const std::filesystem::path base = root( );
        if ( base.empty( ) )
            return { };
        return base / std::filesystem::path( std::u8string( name.begin( ), name.end( ) ) );
    }

    // Slots 13 / 14.
    inline file_t* open( const std::string& name, int mode )
    {
        const std::filesystem::path path = full_path( name );
        if ( path.empty( ) )
            return nullptr;
        const wchar_t* flags = mode == 0 ? L"rb" : mode == 1 ? L"wb" : mode == 2 ? L"rb+" : nullptr;
        if ( flags == nullptr )
            return nullptr;
        if ( mode == 1 )
        {
            std::error_code ignored;
            std::filesystem::create_directories( path.parent_path( ), ignored );
        }
        std::FILE* stream = nullptr;
        if ( _wfopen_s( &stream, path.c_str( ), flags ) != 0 || stream == nullptr )
            return nullptr;
        return new file_t{ stream };
    }

    // Slot 15.
    inline bool close( file_t* file )
    {
        std::FILE* stream = file->stream;
        delete file;
        return std::fclose( stream ) == 0;
    }

    // Slot 11: 0 on a failed stat.
    inline std::uint32_t size( file_t* file )
    {
        struct _stat64i32 info;
        if ( _fstat64i32( _fileno( file->stream ), &info ) == -1 )
            return 0;
        return static_cast< std::uint32_t >( info.st_size );
    }

    // Slot 16.
    inline std::uint32_t read( file_t* file, void* buffer, std::uint32_t count )
    {
        return static_cast< std::uint32_t >( std::fread( buffer, 1, count, file->stream ) );
    }

    // Slot 17.
    inline std::uint32_t write( file_t* file, const void* buffer, std::uint32_t count )
    {
        return static_cast< std::uint32_t >( std::fwrite( buffer, 1, count, file->stream ) );
    }
}
