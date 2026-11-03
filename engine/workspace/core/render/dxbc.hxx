#pragma once

// Shader Model 4.0 bytecode (the pack's DXBC) to GLSL ES 3.00. The pack's stages are vs_4_0 / ps_4_0 and the
// opcodes that actually occur: the ALU and sample set, one loop, dynamic constant-buffer indexes, and uint
// vertex inputs. WebGL2 compiles the text this produces.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct dxbc_input_t
{
    std::string m_name;
    std::uint32_t m_index = 0;
    std::uint32_t m_register = 0;
    std::uint32_t m_type = 3;
};

struct dxbc_stage_t
{
    std::string m_source;
    std::vector< int > m_tex;
    std::vector< int > m_samp;
    std::vector< char > m_cube;
    std::string m_error;
};

namespace dxbc_detail
{
    struct param_t
    {
        std::string m_name;
        std::uint32_t m_index = 0, m_sys = 0, m_type = 3, m_reg = 0, m_mask = 0;
    };

    struct operand_t
    {
        int m_type = 0, m_comps = 0, m_sel = 0, m_field = 0, m_mod = 0, m_nimm = 0;
        int m_index[ 2 ]{};
        bool m_rel[ 2 ]{};
        int m_add[ 2 ]{};
        std::string m_rel_expr[ 2 ];
        std::uint32_t m_raw[ 4 ]{};
    };

    inline bool signature( const std::uint8_t* data, std::uint32_t size, std::vector< param_t >& out )
    {
        if ( data == nullptr || size < 8 )
            return false;
        std::uint32_t count = 0, at = 0;
        std::memcpy( &count, data, 4 );
        std::memcpy( &at, data + 4, 4 );
        if ( count > 32 || at > size || count > ( size - at ) / 24 )
            return false;
        out.resize( count );
        for ( std::uint32_t i = 0; i < count; ++i )
        {
            std::uint32_t rec[ 6 ]{};
            std::memcpy( rec, data + at + i * 24, 24 );
            param_t& p = out[ i ];
            p.m_index = rec[ 1 ];
            p.m_sys = rec[ 2 ];
            p.m_type = rec[ 3 ];
            p.m_reg = rec[ 4 ];
            p.m_mask = rec[ 5 ];
            if ( rec[ 0 ] < size )
            {
                const char* text = reinterpret_cast< const char* >( data + rec[ 0 ] );
                p.m_name.assign( text, ::strnlen( text, size - rec[ 0 ] ) );
            }
        }
        return true;
    }

    inline const param_t* find_reg( const std::vector< param_t >& list, std::uint32_t reg )
    {
        for ( const param_t& p : list )
            if ( p.m_reg == reg )
                return &p;
        return nullptr;
    }

    class translator_t
    {
        const std::uint32_t* m_words = nullptr;
        std::uint32_t m_count = 0, m_at = 0;
        bool m_vertex = false, m_failed = false, m_sat = false;
        int m_temps = 0, m_cb = 0;
        std::vector< param_t > m_in, m_out;
        int m_dim[ 16 ]{};
        std::string m_body, m_error;
        std::vector< int > m_tex, m_samp;
        std::vector< char > m_cube;

        bool take( std::uint32_t& out )
        {
            if ( m_at >= m_count )
            {
                m_failed = true;
                return false;
            }
            out = m_words[ m_at++ ];
            return true;
        }

        static std::string f32( std::uint32_t bits )
        {
            float value = 0;
            std::memcpy( &value, &bits, 4 );
            char buf[ 64 ];
            std::snprintf( buf, sizeof( buf ), "%.9g", static_cast< double >( value ) );
            std::string text = buf;
            if ( text.find( '.' ) == std::string::npos && text.find( 'e' ) == std::string::npos )
                text += ".0";
            return text;
        }

        static std::string swizzle( int field )
        {
            std::string text = ".";
            for ( int c = 0; c < 4; ++c )
                text += "xyzw"[ ( field >> ( 2 * c ) ) & 3 ];
            return text;
        }

        static std::string mask_of( const operand_t& o )
        {
            std::string text;
            for ( int c = 0; c < 4; ++c )
                if ( ( o.m_field & ( 1 << c ) ) != 0 )
                    text += "xyzw"[ c ];
            return text;
        }

        std::string input_expr( int reg ) const
        {
            const param_t* p = find_reg( m_in, static_cast< std::uint32_t >( reg ) );
            const std::string name = "a" + std::to_string( reg );
            if ( p != nullptr && p->m_type == 1 )
                return "uintBitsToFloat(" + name + ")";
            if ( p != nullptr && p->m_type == 2 )
                return "intBitsToFloat(" + name + ")";
            return name;
        }

        std::string output_name( int reg ) const
        {
            const param_t* p = find_reg( m_out, static_cast< std::uint32_t >( reg ) );
            if ( p != nullptr && ( p->m_sys == 1 || p->m_name == "SV_Position" ) )
                return "gl_Position";
            if ( !m_vertex )
                return "f" + std::to_string( reg );
            if ( p != nullptr && !p->m_name.empty( ) )
                return "v_" + p->m_name + std::to_string( p->m_index );
            return "v_o" + std::to_string( reg );
        }

        std::string varying_name( const param_t& p ) const
        {
            if ( p.m_sys == 1 || p.m_name == "SV_Position" )
                return "gl_Position";
            if ( !p.m_name.empty( ) )
                return "v_" + p.m_name + std::to_string( p.m_index );
            return "v_o" + std::to_string( p.m_reg );
        }

        std::string core( const operand_t& o ) const
        {
            if ( o.m_type == 4 )
            {
                std::string text = "vec4(";
                const int n = o.m_nimm == 0 ? 1 : o.m_nimm;
                for ( int c = 0; c < 4; ++c )
                {
                    if ( c )
                        text += ",";
                    text += f32( o.m_raw[ c < n ? c : 0 ] );
                }
                return text + ")";
            }
            if ( o.m_type == 8 )
            {
                std::string index = "0";
                if ( o.m_rel[ 1 ] )
                    index = "floatBitsToInt(" + o.m_rel_expr[ 1 ] + ")+" + std::to_string( o.m_add[ 1 ] );
                else if ( o.m_index[ 1 ] || o.m_add[ 1 ] )
                    index = std::to_string( o.m_index[ 1 ] + o.m_add[ 1 ] );
                return std::string( m_vertex ? "vc[" : "pc[" ) + index + "]";
            }
            if ( o.m_type == 0 )
                return "r" + std::to_string( o.m_index[ 0 ] );
            if ( o.m_type == 1 )
            {
                if ( m_vertex )
                    return input_expr( o.m_index[ 0 ] );
                if ( const param_t* p = find_reg( m_in, static_cast< std::uint32_t >( o.m_index[ 0 ] ) ) )
                    return varying_name( *p );
                return "v_o" + std::to_string( o.m_index[ 0 ] );
            }
            if ( o.m_type == 2 )
                return output_name( o.m_index[ 0 ] );
            return "vec4(0.0)";
        }

        std::string as_vec4( const operand_t& o ) const
        {
            std::string text = core( o );
            if ( o.m_comps == 1 || ( o.m_comps == 2 && o.m_sel == 2 ) )
            {
                if ( o.m_type == 4 )
                    text = "vec4(" + f32( o.m_raw[ o.m_field & 3 ] ) + ")";
                else
                    text = "vec4(" + text + "." + std::string( 1, "xyzw"[ o.m_field & 3 ] ) + ")";
            }
            else if ( o.m_comps == 2 && o.m_sel == 1 )
                text += swizzle( o.m_field );
            if ( o.m_mod == 1 )
                text = "(-(" + text + "))";
            else if ( o.m_mod == 2 )
                text = "abs(" + text + ")";
            else if ( o.m_mod == 3 )
                text = "(-abs(" + text + "))";
            return text;
        }

        std::string as_ivec4( const operand_t& o ) const
        {
            if ( o.m_type == 4 )
            {
                std::string text = "ivec4(";
                const int n = o.m_nimm == 0 ? 1 : o.m_nimm;
                for ( int c = 0; c < 4; ++c )
                {
                    if ( c )
                        text += ",";
                    text += std::to_string( static_cast< int >( o.m_raw[ c < n ? c : 0 ] ) );
                }
                return text + ")";
            }
            return "floatBitsToInt(" + as_vec4( o ) + ")";
        }

        std::string scalar( const operand_t& o ) const
        {
            if ( o.m_comps == 1 || ( o.m_comps == 2 && o.m_sel == 2 ) )
            {
                std::string text = core( o );
                if ( o.m_type != 4 )
                    text += "." + std::string( 1, "xyzw"[ o.m_field & 3 ] );
                else
                    text = f32( o.m_raw[ 0 ] );
                if ( o.m_mod == 1 )
                    text = "(-(" + text + "))";
                return text;
            }
            return "(" + as_vec4( o ) + ").x";
        }

        bool parse_operand( operand_t& o )
        {
            std::uint32_t tok = 0;
            if ( !take( tok ) )
                return false;
            o.m_comps = static_cast< int >( tok & 3u );
            o.m_sel = static_cast< int >( ( tok >> 2 ) & 3u );
            o.m_field = static_cast< int >( ( tok >> 4 ) & 0xFFu );
            o.m_type = static_cast< int >( ( tok >> 12 ) & 0xFFu );
            const int dims = static_cast< int >( ( tok >> 20 ) & 3u );
            if ( ( tok >> 31 ) != 0 )
            {
                std::uint32_t ext = 0;
                if ( !take( ext ) )
                    return false;
                if ( ( ext & 0x3Fu ) == 1u )
                    o.m_mod = static_cast< int >( ( ext >> 6 ) & 3u );
                while ( ( ext >> 31 ) != 0 )
                    if ( !take( ext ) )
                        return false;
            }
            for ( int d = 0; d < dims; ++d )
            {
                const int rep = static_cast< int >( ( tok >> ( 22 + 3 * d ) ) & 7u );
                if ( rep == 0 )
                {
                    std::uint32_t imm = 0;
                    if ( !take( imm ) )
                        return false;
                    o.m_index[ d ] = static_cast< int >( imm );
                }
                else if ( rep == 2 || rep == 3 )
                {
                    if ( rep == 3 )
                    {
                        std::uint32_t imm = 0;
                        if ( !take( imm ) )
                            return false;
                        o.m_add[ d ] = static_cast< int >( imm );
                    }
                    operand_t rel;
                    if ( !parse_operand( rel ) )
                        return false;
                    o.m_rel[ d ] = true;
                    o.m_rel_expr[ d ] = scalar( rel );
                }
                else
                {
                    m_failed = true;
                    m_error = "index";
                    return false;
                }
            }
            if ( o.m_type == 4 )
            {
                o.m_nimm = o.m_comps == 1 ? 1 : 4;
                for ( int c = 0; c < o.m_nimm; ++c )
                    if ( !take( o.m_raw[ c ] ) )
                        return false;
            }
            return true;
        }

        void assign( const operand_t& dest, const std::string& expr )
        {
            const std::string mask = mask_of( dest );
            const std::string name = dest.m_type == 2 ? output_name( dest.m_index[ 0 ] ) : "r" + std::to_string( dest.m_index[ 0 ] );
            if ( mask.empty( ) )
                return;
            // SM4 _sat is bit 13 of the opcode token. Font/Outline clamps the SDF mix here; a mesh light term does too.
            const std::string value = m_sat ? "clamp(" + expr + ",0.0,1.0)" : expr;
            if ( mask.size( ) == 4 )
                m_body += name + "=" + value + ";\n";
            else
                m_body += name + "." + mask + "=(" + value + ")." + mask + ";\n";
        }

        bool instruction( std::uint32_t tok, std::uint32_t end )
        {
            const int op = static_cast< int >( tok & 0x7FFu );
            const bool nonzero = ( ( tok >> 18 ) & 1u ) != 0;
            m_sat = ( tok & 0x2000u ) != 0u && op != 1 && op != 30 && op != 41;
            if ( op == 104 )
            {
                std::uint32_t n = 0;
                return take( n ) && ( m_temps = static_cast< int >( n ), true );
            }
            if ( op == 89 )
            {
                operand_t o;
                if ( !parse_operand( o ) )
                    return false;
                m_cb = o.m_index[ 1 ];
                return m_at == end;
            }
            if ( op == 88 )
            {
                operand_t o;
                std::uint32_t ret = 0;
                if ( !parse_operand( o ) || !take( ret ) )
                    return false;
                if ( o.m_index[ 0 ] >= 0 && o.m_index[ 0 ] < 16 )
                    m_dim[ o.m_index[ 0 ] ] = static_cast< int >( ( tok >> 11 ) & 0xFu );
                return m_at == end;
            }
            if ( op == 90 || op == 95 || op == 98 || op == 101 )
            {
                operand_t o;
                return parse_operand( o ) && m_at == end;
            }
            if ( op == 103 )
            {
                operand_t o;
                std::uint32_t sys = 0;
                return parse_operand( o ) && take( sys ) && m_at == end;
            }
            if ( op == 48 )
            {
                m_body += "while(true){\n";
                return m_at == end;
            }
            if ( op == 22 )
            {
                m_body += "}\n";
                return m_at == end;
            }
            if ( op == 62 )
            {
                m_body += "return;\n";
                return m_at == end;
            }
            operand_t ops[ 4 ];
            int n = 0;
            while ( m_at < end && n < 4 )
                if ( !parse_operand( ops[ n++ ] ) )
                    return false;
            if ( m_at != end )
            {
                m_error = "opcode " + std::to_string( op );
                m_failed = true;
                return false;
            }
            const auto a = [ & ]( int i ) { return as_vec4( ops[ i ] ); };
            if ( op == 54 && n == 2 )
                assign( ops[ 0 ], a( 1 ) );
            else if ( op == 0 && n == 3 )
                assign( ops[ 0 ], "(" + a( 1 ) + "+" + a( 2 ) + ")" );
            else if ( op == 56 && n == 3 )
                assign( ops[ 0 ], "(" + a( 1 ) + "*" + a( 2 ) + ")" );
            else if ( op == 50 && n == 4 )
                assign( ops[ 0 ], "(" + a( 1 ) + "*" + a( 2 ) + "+" + a( 3 ) + ")" );
            else if ( op == 14 && n == 3 )
                assign( ops[ 0 ], "(" + a( 1 ) + "/" + a( 2 ) + ")" );
            else if ( op == 51 && n == 3 )
                assign( ops[ 0 ], "min(" + a( 1 ) + "," + a( 2 ) + ")" );
            else if ( op == 52 && n == 3 )
                assign( ops[ 0 ], "max(" + a( 1 ) + "," + a( 2 ) + ")" );
            else if ( op == 75 && n == 2 )
                assign( ops[ 0 ], "sqrt(" + a( 1 ) + ")" );
            else if ( op == 68 && n == 2 )
                assign( ops[ 0 ], "inversesqrt(" + a( 1 ) + ")" );
            else if ( op == 26 && n == 2 )
                assign( ops[ 0 ], "fract(" + a( 1 ) + ")" );
            else if ( op == 65 && n == 2 )
                assign( ops[ 0 ], "floor(" + a( 1 ) + ")" );
            else if ( op == 16 && n == 3 )
                assign( ops[ 0 ], "vec4(dot((" + a( 1 ) + ").xyz,(" + a( 2 ) + ").xyz))" );
            else if ( op == 15 && n == 3 )
                assign( ops[ 0 ], "vec4(dot((" + a( 1 ) + ").xy,(" + a( 2 ) + ").xy))" );
            else if ( op == 17 && n == 3 )
                assign( ops[ 0 ], "vec4(dot(" + a( 1 ) + "," + a( 2 ) + "))" );
            else if ( op == 49 && n == 3 )
                assign( ops[ 0 ], "mix(vec4(0.0),vec4(1.0),lessThan(" + a( 1 ) + "," + a( 2 ) + "))" );
            else if ( op == 29 && n == 3 )
                assign( ops[ 0 ], "mix(vec4(0.0),vec4(1.0),greaterThanEqual(" + a( 1 ) + "," + a( 2 ) + "))" );
            else if ( op == 55 && n == 4 )
                assign( ops[ 0 ], "mix(" + a( 3 ) + "," + a( 2 ) + ",notEqual(" + a( 1 ) + ",vec4(0.0)))" );
            else if ( op == 30 && n == 3 )
                assign( ops[ 0 ], "intBitsToFloat(" + as_ivec4( ops[ 1 ] ) + "+" + as_ivec4( ops[ 2 ] ) + ")" );
            else if ( op == 41 && n == 3 )
                assign( ops[ 0 ], "intBitsToFloat(" + as_ivec4( ops[ 1 ] ) + "<<" + as_ivec4( ops[ 2 ] ) + ")" );
            else if ( op == 1 && n == 3 )
                assign( ops[ 0 ], "intBitsToFloat(" + as_ivec4( ops[ 1 ] ) + "&" + as_ivec4( ops[ 2 ] ) + ")" );
            else if ( op == 33 && n == 3 )
                assign( ops[ 0 ], "mix(vec4(0.0),vec4(1.0),greaterThanEqual(" + as_ivec4( ops[ 1 ] ) + "," + as_ivec4( ops[ 2 ] ) + "))" );
            else if ( op == 3 && n == 1 )
                m_body += std::string( "if(" ) + ( nonzero ? "" : "!" ) + "(floatBitsToInt(" + scalar( ops[ 0 ] ) + ")!=0))break;\n";
            else if ( op == 13 )
            {
                if ( n == 0 )
                    m_body += "discard;\n";
                else
                    m_body += std::string( "if(" ) + ( nonzero ? "" : "!" ) + "any(notEqual(floatBitsToInt(" + a( 0 ) + "),ivec4(0))))discard;\n";
            }
            else if ( op == 69 && n == 4 )
            {
                const int tex = ops[ 2 ].m_index[ 0 ];
                const int samp = ops[ 3 ].m_index[ 0 ];
                const bool cube = tex >= 0 && tex < 16 && m_dim[ tex ] == 6;
                m_tex.push_back( tex );
                m_samp.push_back( samp );
                m_cube.push_back( cube ? 1 : 0 );
                const char* coord = cube ? ".xyz" : ".xy";
                assign( ops[ 0 ], "texture(t" + std::to_string( tex ) + ",(" + a( 1 ) + ")" + coord + ")" );
            }
            else
            {
                m_error = "opcode " + std::to_string( op );
                m_failed = true;
                return false;
            }
            return true;
        }

    public:
        bool run( const void* code, std::uint32_t size, dxbc_stage_t& out )
        {
            if ( code == nullptr || size < 48 || std::memcmp( code, "DXBC", 4 ) != 0 )
            {
                out.m_error = "not dxbc";
                return false;
            }
            const auto* bytes = static_cast< const std::uint8_t* >( code );
            std::uint32_t chunks = 0;
            std::memcpy( &chunks, bytes + 28, 4 );
            if ( chunks > 16 || 32 + chunks * 4 > size )
            {
                out.m_error = "chunks";
                return false;
            }
            const std::uint8_t* shader = nullptr;
            std::uint32_t shader_bytes = 0;
            for ( std::uint32_t c = 0; c < chunks; ++c )
            {
                std::uint32_t offset = 0;
                std::memcpy( &offset, bytes + 32 + c * 4, 4 );
                if ( offset + 8 > size )
                    continue;
                char tag[ 5 ] = {};
                std::memcpy( tag, bytes + offset, 4 );
                std::uint32_t csize = 0;
                std::memcpy( &csize, bytes + offset + 4, 4 );
                if ( offset + 8 + csize > size )
                    continue;
                const std::uint8_t* payload = bytes + offset + 8;
                if ( std::strcmp( tag, "ISGN" ) == 0 )
                    signature( payload, csize, m_in );
                else if ( std::strcmp( tag, "OSGN" ) == 0 )
                    signature( payload, csize, m_out );
                else if ( std::strcmp( tag, "SHDR" ) == 0 || std::strcmp( tag, "SHEX" ) == 0 )
                {
                    shader = payload;
                    shader_bytes = csize;
                }
            }
            if ( shader == nullptr || shader_bytes < 8 )
            {
                out.m_error = "no shader";
                return false;
            }
            m_count = shader_bytes / 4;
            std::vector< std::uint32_t > words( m_count );
            std::memcpy( words.data( ), shader, m_count * 4 );
            m_words = words.data( );
            const std::uint32_t version = words[ 0 ];
            const std::uint32_t claimed = words[ 1 ];
            if ( claimed < m_count )
                m_count = claimed;
            m_vertex = ( ( version >> 16 ) & 0xFFFFu ) == 1u;
            m_at = 2;
            while ( m_at < m_count && !m_failed )
            {
                const std::uint32_t start = m_at;
                std::uint32_t tok = 0;
                if ( !take( tok ) )
                    break;
                const std::uint32_t length = ( tok >> 24 ) & 0x7Fu;
                if ( length == 0 || start + length > m_count )
                {
                    m_error = "length";
                    m_failed = true;
                    break;
                }
                if ( ( tok >> 31 ) != 0 )
                {
                    std::uint32_t ext = 0;
                    if ( !take( ext ) )
                        break;
                    while ( ( ext >> 31 ) != 0 )
                        if ( !take( ext ) )
                            break;
                }
                if ( !instruction( tok, start + length ) )
                    break;
                m_at = start + length;
            }
            if ( m_failed )
            {
                out.m_error = m_error.empty( ) ? "translate" : m_error;
                return false;
            }
            std::string text = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
            if ( m_cb > 0 )
                text += "layout(std140) uniform " + std::string( m_vertex ? "VCB{vec4 vc[" : "PCB{vec4 pc[" ) + std::to_string( m_cb ) + "];};\n";
            if ( m_vertex )
            {
                for ( const param_t& p : m_in )
                {
                    const char* kind = p.m_type == 1 ? "uvec4" : p.m_type == 2 ? "ivec4" : "vec4";
                    text += "layout(location=" + std::to_string( p.m_reg ) + ") in " + kind + " a" + std::to_string( p.m_reg ) + ";\n";
                }
                for ( const param_t& p : m_out )
                    if ( varying_name( p ) != "gl_Position" )
                        text += "out vec4 " + varying_name( p ) + ";\n";
            }
            else
            {
                for ( const param_t& p : m_in )
                    if ( varying_name( p ) != "gl_Position" )
                        text += "in vec4 " + varying_name( p ) + ";\n";
                for ( const param_t& p : m_out )
                    text += "layout(location=" + std::to_string( p.m_index ) + ") out vec4 f" + std::to_string( p.m_reg ) + ";\n";
                bool declared[ 16 ] = {};
                for ( std::size_t s = 0; s < m_tex.size( ); ++s )
                {
                    const int slot = m_tex[ s ];
                    if ( slot < 0 || slot >= 16 || declared[ slot ] )
                        continue;
                    declared[ slot ] = true;
                    text += std::string( "uniform " ) + ( m_cube[ s ] ? "samplerCube" : "sampler2D" ) + " t" + std::to_string( slot ) + ";\n";
                }
            }
            text += "void main(){\n";
            for ( int t = 0; t < m_temps; ++t )
                text += "vec4 r" + std::to_string( t ) + "=vec4(0.0);\n";
            if ( m_vertex )
                text += "gl_Position=vec4(0.0);\n";
            else
                for ( const param_t& p : m_out )
                    text += "f" + std::to_string( p.m_reg ) + "=vec4(0.0);\n";
            for ( const param_t& p : m_out )
                if ( m_vertex && varying_name( p ) != "gl_Position" )
                    text += varying_name( p ) + "=vec4(0.0);\n";
            text += m_body;
            text += "}\n";
            out.m_source = std::move( text );
            out.m_tex = std::move( m_tex );
            out.m_samp = std::move( m_samp );
            out.m_cube = std::move( m_cube );
            return true;
        }
    };
}

inline bool dxbc_translate( const void* code, std::uint32_t size, dxbc_stage_t& out )
{
    return dxbc_detail::translator_t{ }.run( code, size, out );
}

inline int dxbc_input_register( const void* code, std::uint32_t size, const char* name, std::uint32_t index )
{
    if ( code == nullptr || name == nullptr || size < 48 || std::memcmp( code, "DXBC", 4 ) != 0 )
        return -1;
    const auto* bytes = static_cast< const std::uint8_t* >( code );
    std::uint32_t chunks = 0;
    std::memcpy( &chunks, bytes + 28, 4 );
    if ( chunks > 16 || 32 + chunks * 4 > size )
        return -1;
    for ( std::uint32_t c = 0; c < chunks; ++c )
    {
        std::uint32_t offset = 0;
        std::memcpy( &offset, bytes + 32 + c * 4, 4 );
        if ( offset + 8 > size )
            continue;
        if ( std::memcmp( bytes + offset, "ISGN", 4 ) != 0 )
            continue;
        std::uint32_t csize = 0;
        std::memcpy( &csize, bytes + offset + 4, 4 );
        if ( offset + 8 + csize > size )
            return -1;
        std::vector< dxbc_detail::param_t > params;
        if ( !dxbc_detail::signature( bytes + offset + 8, csize, params ) )
            return -1;
        for ( const dxbc_detail::param_t& p : params )
            if ( p.m_index == index && p.m_name == name )
                return static_cast< int >( p.m_reg );
        return -1;
    }
    return -1;
}
