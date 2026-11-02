#pragma once

#include <cstring>
#include <map>
#include <string>

// VuParseCommandLine: the switches of the app's command line, std::map< VuString, VuString > (the map's operator[] 0x55EE80).
// The shipped package passes none: the app init's three reads (0x4D9900) and the two others (0x4BCE00, 0x4CBCD0) all answer false.
class c_options
{
public:
    // 0x55EB30: "--name value --flag --name "a value"". Works in place on a 0x100-byte copy (strcpy_s: a longer text is the CRT's
    // invalid parameter, retail's process ends: `false` here, nothing parsed). A later switch overwrites an earlier one of the name.
    bool parse( const char* source )
    {
        char text[ 0x100 ];
        if ( std::strlen( source ) >= sizeof( text ) )
            return false;
        std::memcpy( text, source, std::strlen( source ) + 1 );
        char* at = std::strstr( text, "--" );
        while ( at != nullptr )
        {
            char* name = at + 2;
            at = std::strstr( name, "--" );
            if ( at != nullptr )
                *at = 0;
            while ( std::strlen( name ) != 0 && name[ std::strlen( name ) - 1 ] == ' ' )
                name[ std::strlen( name ) - 1 ] = 0;
            char* space = std::strchr( name, ' ' );
            if ( space == nullptr )
            {
                m_values[ name ].clear( );
                continue;
            }
            *space = 0;
            char* value = space + 1;
            if ( space[ 1 ] == '"' && space[ std::strlen( space + 1 ) ] == '"' )
            {
                const std::size_t length = std::strlen( value++ );
                value[ length - 2 ] = 0;
            }
            m_values[ name ] = value;
        }
        return true;
    }

    // 0x55EDD0 ( name, out ): false for an unknown name, `out` untouched.
    bool get( const char* name, std::string& out ) const
    {
        const auto it = m_values.find( name );
        if ( it == m_values.end( ) )
            return false;
        out = it->second;
        return true;
    }

    const std::map< std::string, std::string >& values( ) const
    {
        return m_values;
    }

private:
    std::map< std::string, std::string > m_values;
};
