#pragma once

#include <impl/host.hxx>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#if HYDRO_HOST_WINDOWS
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#endif

inline bool g_verbose = false;

namespace logger
{
    enum class level
    {
        verbose,
        info,
        success,
        warning,
        error
    };

    namespace detail
    {
        static inline void initialize_console( ) noexcept
        {
            static bool initialized = false;
            if ( initialized )
                return;
            initialized = true;

#if HYDRO_HOST_WINDOWS
            HANDLE console = GetStdHandle( STD_OUTPUT_HANDLE );
            if ( console == INVALID_HANDLE_VALUE )
                return;

            DWORD mode = 0;
            if ( !GetConsoleMode( console, &mode ) )
                return;

            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode( console, mode );
#endif
        }

        // Ours: one console write per line. A console's stdout is unbuffered, so printing the per-character gradient
        // directly was one write per printf / putchar, some 500 a line: 4 ms a line, a visible hitch. Same bytes, composed here.
        inline std::string& line( ) noexcept
        {
            thread_local std::string text;
            return text;
        }

        template<typename... Args>
        static inline void put( const char* format, Args... args ) noexcept
        {
            char piece[ 4200 ];
            const int size = std::snprintf( piece, sizeof( piece ), format, args... );
            if ( size > 0 )
                line( ).append( piece, static_cast< std::size_t >( size ) < sizeof( piece ) ? static_cast< std::size_t >( size ) : sizeof( piece ) - 1 );
        }

        static inline void flush( ) noexcept
        {
            std::fwrite( line( ).data( ), 1, line( ).size( ), stdout );
            std::fflush( stdout );
            line( ).clear( );
        }

        static inline void set_rgb( uint8_t r, uint8_t g, uint8_t b ) noexcept
        {
            put( "\x1b[38;2;%u;%u;%um", r, g, b );
        }

        static inline void reset( ) noexcept
        {
            put( "\x1b[0m" );
        }

        static inline uint8_t lerp( uint8_t a, uint8_t b, float t ) noexcept
        {
            return static_cast<uint8_t>( a + ( b - a ) * t );
        }

        static inline void print_gradient( const char* text,
            uint8_t r1 = 168, uint8_t g1 = 85, uint8_t b1 = 247,
            uint8_t r2 = 59, uint8_t g2 = 130, uint8_t b2 = 246 ) noexcept
        {
            if ( !text || !*text )
                return;

            const size_t len = std::strlen( text );
            if ( len == 0 )
                return;

            for ( size_t i = 0; i < len; ++i )
            {
                float t = ( len > 1 )
                    ? static_cast<float>( i ) / static_cast<float>( len - 1 )
                    : 0.0f;

                set_rgb(
                    lerp( r1, r2, t ),
                    lerp( g1, g2, t ),
                    lerp( b1, b2, t )
                );
                put( "%c", text[ i ] );
            }

            reset( );
        }

        static inline void print_timestamp( ) noexcept
        {
            auto now = std::chrono::system_clock::now( );
            std::time_t time = std::chrono::system_clock::to_time_t( now );

            tm local_tm{};
#if HYDRO_HOST_WINDOWS
            ::localtime_s( &local_tm, &time );
#else
            ::localtime_r( &time, &local_tm );
#endif

            set_rgb( 100, 100, 100 );
            put( "[%02d/%02d/%04d %02d:%02d:%02d] ",
                local_tm.tm_mon + 1, local_tm.tm_mday, local_tm.tm_year + 1900,
                local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec );
            reset( );
        }

        static inline void print_level( level lvl ) noexcept
        {
            switch ( lvl )
            {
            case level::verbose: set_rgb( 140, 140, 140 ); put( "[*] " ); break;
            case level::info:    set_rgb( 59, 130, 246 );   put( "[~] " ); break;
            case level::success: set_rgb( 80, 200, 120 );   put( "[+] " ); break;
            case level::warning: set_rgb( 255, 215, 0 );    put( "[!] " ); break;
            case level::error:   set_rgb( 255, 80, 80 );    put( "[-] " ); break;
            }
            reset( );
        }

        static inline void print_caller( const char* function ) noexcept
        {
            const char* separator = std::strrchr( function, ':' );

            if ( separator && separator > function && *( separator - 1 ) == ':' )
            {
                size_t context_len = ( separator - 1 ) - function;
                char context[ 256 ]{};
                if ( context_len >= sizeof( context ) )
                    context_len = sizeof( context ) - 1;
                std::memcpy( context, function, context_len );

                const char* name = separator + 1;

                set_rgb( 100, 100, 100 ); put( "[" );
                set_rgb( 180, 180, 210 ); put( "%s", context );
                set_rgb( 100, 100, 100 ); put( "] " );
                set_rgb( 220, 220, 235 ); put( "%s", name );
                set_rgb( 100, 100, 100 ); put( ": " );
            }
            else
            {
                set_rgb( 220, 220, 235 ); put( "%s", function );
                set_rgb( 100, 100, 100 ); put( ": " );
            }

            reset( );
        }
    }

    // Ours: what the console cost, for runtime.hxx's --frame-log.
    inline int g_lines = 0;

    template<typename... Args>
    inline void log( level lvl, const char* caller, const char* format, Args... args ) noexcept
    {
        ++g_lines;
        detail::initialize_console( );
        detail::print_timestamp( );
        detail::print_level( lvl );
        detail::print_caller( caller );

        char buffer[ 4096 ];
        std::snprintf( buffer, sizeof( buffer ), format, args... );
        detail::print_gradient( buffer );

        detail::put( "\n" );
        detail::flush( );
    }

#if HYDRO_HOST_WINDOWS
    inline void log_win32_error( const char* caller, const char* api_name, DWORD error_code ) noexcept
    {
        LPWSTR message = NULL;

        FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, error_code, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
            (LPWSTR)&message, 0, NULL
        );

        if ( message )
        {
            size_t len = wcslen( message );
            while ( len > 0 && ( message[ len - 1 ] == '\n' || message[ len - 1 ] == '\r' ) )
                message[ --len ] = '\0';
        }

        if ( message )
        {
            log( level::error, caller, "%s failed: err=%lu - %ws", api_name, error_code, message );
            LocalFree( message );
        }
        else
        {
            log( level::error, caller, "%s failed: err=%lu", api_name, error_code );
        }
    }
#endif
}

#define LOG_VERBOSE(...) do { if ( g_verbose ) logger::log( logger::level::verbose, __FUNCTION__, __VA_ARGS__ ); } while ( 0 )
#define LOG_INFO(...)    logger::log( logger::level::info, __FUNCTION__, __VA_ARGS__ )
#define LOG_SUCCESS(...) logger::log( logger::level::success, __FUNCTION__, __VA_ARGS__ )
#define LOG_WARNING(...) logger::log( logger::level::warning, __FUNCTION__, __VA_ARGS__ )
#define LOG_ERROR(...)   logger::log( logger::level::error, __FUNCTION__, __VA_ARGS__ )

#if HYDRO_HOST_WINDOWS
#  define LOG_ERROR_WIN32( api ) do { DWORD _le = GetLastError( ); logger::log_win32_error( __FUNCTION__, api, _le ); } while ( 0 )
#endif
