#pragma once

#include <impl/host.hxx>

#if HYDRO_HOST_WINDOWS
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#  include <cstdio>
#  include <cstring>

// First-chance handler for fatal SEH. Continues the search so the process
// still dies, after the record is on stderr and crash.log beside the exe.
// Ours, a diagnostic (no retail counterpart). Each record opens with the local
// time, the build (the exe's link stamp, its file time and size: with them a
// module offset below names one exe) and the command line.
class c_crash
{
public:
    static void install( ) noexcept
    {
        static PVOID installed = nullptr;
        if ( installed != nullptr )
            return;
        installed = AddVectoredExceptionHandler( 1, &c_crash::on_exception );
    }

private:
    static bool fatal( DWORD code ) noexcept
    {
        switch ( code )
        {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_INT_OVERFLOW:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_OVERFLOW:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_FLT_STACK_CHECK:
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        case 0xC0000409:
            return true;
        default:
            return false;
        }
    }

    static const char* name_of( DWORD code ) noexcept
    {
        switch ( code )
        {
        case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
        case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
        case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
        case EXCEPTION_FLT_STACK_CHECK: return "EXCEPTION_FLT_STACK_CHECK";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
        case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN";
        default: return "EXCEPTION";
        }
    }

    static void emit( const char* text ) noexcept
    {
        if ( text == nullptr )
            return;
        const DWORD bytes = static_cast< DWORD >( std::strlen( text ) );
        HANDLE error = GetStdHandle( STD_ERROR_HANDLE );
        if ( error != nullptr && error != INVALID_HANDLE_VALUE )
        {
            DWORD wrote = 0;
            WriteFile( error, text, bytes, &wrote, nullptr );
        }
        OutputDebugStringA( text );

        char path[ MAX_PATH ]{};
        const DWORD length = GetModuleFileNameA( nullptr, path, MAX_PATH );
        if ( length == 0 || length >= MAX_PATH )
            return;
        char* slash = std::strrchr( path, '\\' );
        if ( slash == nullptr )
            return;
        const char* file = "crash.log";
        if ( static_cast< std::size_t >( slash - path ) + 1 + std::strlen( file ) >= MAX_PATH )
            return;
        std::memcpy( slash + 1, file, std::strlen( file ) + 1 );
        HANDLE log = CreateFileA( path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
        if ( log == INVALID_HANDLE_VALUE )
            return;
        DWORD wrote = 0;
        WriteFile( log, text, bytes, &wrote, nullptr );
        CloseHandle( log );
    }

    // The record's first lines. No heap: this runs inside the exception.
    static void header( ) noexcept
    {
        SYSTEMTIME now{};
        GetLocalTime( &now );
        char line[ 512 ]{};
        std::snprintf( line, sizeof( line ), "=== %04u-%02u-%02u %02u:%02u:%02u\r\n", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond );
        emit( line );

        char path[ MAX_PATH ]{};
        GetModuleFileNameA( nullptr, path, MAX_PATH );
        const auto* base = reinterpret_cast< const unsigned char* >( GetModuleHandleA( nullptr ) );
        const auto* dos = reinterpret_cast< const IMAGE_DOS_HEADER* >( base );
        const auto* nt = reinterpret_cast< const IMAGE_NT_HEADERS* >( base + dos->e_lfanew );
        WIN32_FILE_ATTRIBUTE_DATA file{};
        SYSTEMTIME written{};
        if ( GetFileAttributesExA( path, GetFileExInfoStandard, &file ) )
        {
            FILETIME local{};
            FileTimeToLocalFileTime( &file.ftLastWriteTime, &local );
            FileTimeToSystemTime( &local, &written );
        }
        std::snprintf( line, sizeof( line ), "    build %s stamp 0x%08lx written %04u-%02u-%02u %02u:%02u:%02u size %lu\r\n", path,
                       nt->FileHeader.TimeDateStamp, written.wYear, written.wMonth, written.wDay, written.wHour, written.wMinute, written.wSecond, file.nFileSizeLow );
        emit( line );

        emit( "    command " );
        emit( GetCommandLineA( ) );
        emit( "\r\n" );
    }

    static void module_of( const void* address, char* out, std::size_t out_size ) noexcept
    {
        if ( out_size == 0 )
            return;
        out[ 0 ] = '\0';
        HMODULE module = nullptr;
        if ( !GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                static_cast< LPCSTR >( address ),
                &module ) )
        {
            std::snprintf( out, out_size, "%p", address );
            return;
        }
        char path[ MAX_PATH ]{};
        GetModuleFileNameA( module, path, MAX_PATH );
        const char* base = std::strrchr( path, '\\' );
        base = base != nullptr ? base + 1 : path;
        const auto offset = static_cast< const unsigned char* >( address ) - reinterpret_cast< const unsigned char* >( module );
        std::snprintf( out, out_size, "%s+0x%llx", base, static_cast< unsigned long long >( offset ) );
    }

    static LONG CALLBACK on_exception( EXCEPTION_POINTERS* info ) noexcept
    {
        if ( info == nullptr || info->ExceptionRecord == nullptr )
            return EXCEPTION_CONTINUE_SEARCH;

        const EXCEPTION_RECORD* record = info->ExceptionRecord;
        if ( !fatal( record->ExceptionCode ) )
            return EXCEPTION_CONTINUE_SEARCH;

        static volatile LONG busy = 0;
        if ( InterlockedCompareExchange( &busy, 1, 0 ) != 0 )
            return EXCEPTION_CONTINUE_SEARCH;

        header( );

        char where[ 160 ]{};
        module_of( record->ExceptionAddress, where, sizeof( where ) );

        char line[ 640 ]{};
        if ( record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2 )
        {
            const char* op = "accessed";
            if ( record->ExceptionInformation[ 0 ] == 0 )
                op = "reading";
            else if ( record->ExceptionInformation[ 0 ] == 1 )
                op = "writing";
            else if ( record->ExceptionInformation[ 0 ] == 8 )
                op = "executing";
            std::snprintf(
                line, sizeof( line ),
                "[-] crash %s code=0x%08lx at %s %s 0x%llx\r\n",
                name_of( record->ExceptionCode ),
                record->ExceptionCode,
                where,
                op,
                static_cast< unsigned long long >( record->ExceptionInformation[ 1 ] )
            );
        }
        else
        {
            std::snprintf(
                line, sizeof( line ),
                "[-] crash %s code=0x%08lx at %s\r\n",
                name_of( record->ExceptionCode ),
                record->ExceptionCode,
                where
            );
        }
        emit( line );

        if ( record->ExceptionCode != EXCEPTION_STACK_OVERFLOW )
        {
            void* frames[ 24 ]{};
            const USHORT count = CaptureStackBackTrace( 0, 24, frames, nullptr );
            for ( USHORT index = 0; index < count; ++index )
            {
                char frame[ 192 ]{};
                char site[ 160 ]{};
                module_of( frames[ index ], site, sizeof( site ) );
                std::snprintf( frame, sizeof( frame ), "    %s\r\n", site );
                emit( frame );
            }
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }
};
#endif
