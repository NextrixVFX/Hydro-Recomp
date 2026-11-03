#pragma once

// Windows is the MSVC build. The same sources are meant to compile later
// for macOS and Linux; keep Win32 out of workspace code.

#if defined( __EMSCRIPTEN__ )
#  define HYDRO_HOST_WASM 1
#elif defined( _WIN32 )
#  define HYDRO_HOST_WINDOWS 1
#elif defined( __APPLE__ )
#  define HYDRO_HOST_APPLE 1
#elif defined( __linux__ )
#  define HYDRO_HOST_LINUX 1
#else
#  error unsupported host
#endif

namespace host
{
    inline const char* name( ) noexcept
    {
#if HYDRO_HOST_WASM
        return "wasm";
#elif HYDRO_HOST_WINDOWS
        return "windows";
#elif HYDRO_HOST_APPLE
        return "macos";
#else
        return "linux";
#endif
    }
}

#if HYDRO_HOST_WASM
#  include <cstdio>
#  include <cstring>
inline int hydro_fopen_s( std::FILE** file, const char* path, const char* mode )
{
    if ( file == nullptr )
        return 22;
    *file = std::fopen( path, mode );
    return *file != nullptr ? 0 : 2;
}
inline int hydro_wfopen_s( std::FILE** file, const wchar_t* path, const wchar_t* mode )
{
    char narrow[ 1024 ]{};
    char flags[ 8 ]{};
    if ( path != nullptr )
        for ( int i = 0; path[ i ] != L'\0' && i < 1023; ++i )
            narrow[ i ] = path[ i ] < 128 ? static_cast< char >( path[ i ] ) : '?';
    if ( mode != nullptr )
        for ( int i = 0; mode[ i ] != L'\0' && i < 7; ++i )
            flags[ i ] = static_cast< char >( mode[ i ] );
    return hydro_fopen_s( file, narrow, flags );
}
#  define fopen_s hydro_fopen_s
#  define _wfopen_s hydro_wfopen_s
#  define sscanf_s std::sscanf
#endif
