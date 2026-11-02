#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

// zlib 1.2.7's compress as the cloud save calls it (0x5BD730: 0x5BD870 deflateInit at level 6, window 15, memLevel 8,
// default strategy, then 0x5BDB30 deflate( Z_FINISH ), 0x5BE2A0 deflateEnd). MIDDLEWARE, like Bullet: zlib's own algorithm
// (deflate_slow, longest_match, trees.c), written out for this one configuration - good 8, lazy 16, nice 128, chain 128,
// hash 15 bits, 0x4000 literals per block - and verified byte for byte against retail's code (case save_cloud).
class c_deflate
{
public:
    static std::vector< std::uint8_t > compress( const std::uint8_t* input, std::size_t size )
    {
        auto state = std::make_unique< c_deflate >( input, size );
        state->run( );
        return std::move( state->m_out );
    }

    c_deflate( const std::uint8_t* input, std::size_t size ) : m_in( input ), m_left( size )
    {
        init_tables( );
        m_lit_buf.resize( k_lit_bufsize );
        m_dist_buf.resize( k_lit_bufsize );
        m_window.assign( 2 * k_wsize, 0 );
        m_prev.assign( k_wsize, 0 );
        m_head.assign( k_hash_size, 0 );
        init_block( );
    }

private:
    struct ct_t
    {
        std::uint16_t fc = 0;  // Freq / Code
        std::uint16_t dl = 0;  // Dad / Len
    };

    struct desc_t
    {
        ct_t* tree;
        int max_code;
        const ct_t* stree;
        const int* extra;
        int base;
        int elems;
        int max_length;
    };

    static constexpr int k_wsize = 0x8000, k_wmask = k_wsize - 1, k_hash_size = 0x8000, k_hash_mask = k_hash_size - 1, k_hash_shift = 5;
    static constexpr int k_min_match = 3, k_max_match = 258, k_min_lookahead = k_max_match + k_min_match + 1;
    static constexpr int k_max_dist = k_wsize - k_min_lookahead, k_lit_bufsize = 0x4000, k_too_far = 4096;
    static constexpr int k_good = 8, k_lazy = 16, k_nice = 128, k_chain = 128;
    static constexpr int k_length_codes = 29, k_literals = 256, k_l_codes = k_literals + 1 + k_length_codes, k_d_codes = 30, k_bl_codes = 19;
    static constexpr int k_heap_size = 2 * k_l_codes + 1, k_max_bits = 15, k_end_block = 256, k_rep_3_6 = 16, k_repz_3_10 = 17, k_repz_11_138 = 18;

    static inline const int k_extra_lbits[ k_length_codes ] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static inline const int k_extra_dbits[ k_d_codes ] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    static inline const int k_extra_blbits[ k_bl_codes ] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 7 };
    static inline const std::uint8_t k_bl_order[ k_bl_codes ] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

    // tr_static_init.
    struct tables_t
    {
        ct_t static_ltree[ k_l_codes + 2 ];
        ct_t static_dtree[ k_d_codes ];
        std::uint8_t dist_code[ 512 ];
        std::uint8_t length_code[ k_max_match - k_min_match + 1 ];
        int base_length[ k_length_codes ];
        int base_dist[ k_d_codes ];
    };

    static std::uint32_t reverse( std::uint32_t code, int len )
    {
        std::uint32_t res = 0;
        do
        {
            res |= code & 1;
            code >>= 1;
            res <<= 1;
        } while ( --len > 0 );
        return res >> 1;
    }

    static void gen_codes( ct_t* tree, int max_code, const std::uint16_t* bl_count )
    {
        std::uint16_t next_code[ k_max_bits + 1 ];
        std::uint32_t code = 0;
        for ( int bits = 1; bits <= k_max_bits; ++bits )
        {
            code = ( code + bl_count[ bits - 1 ] ) << 1;
            next_code[ bits ] = static_cast< std::uint16_t >( code );
        }
        for ( int n = 0; n <= max_code; ++n )
        {
            const int len = tree[ n ].dl;
            if ( len == 0 )
                continue;
            tree[ n ].fc = static_cast< std::uint16_t >( reverse( next_code[ len ]++, len ) );
        }
    }

    static const tables_t& tables( )
    {
        static const tables_t t = []( ) {
            tables_t s{};
            int length = 0, code, n, dist;
            for ( code = 0; code < k_length_codes - 1; ++code )
            {
                s.base_length[ code ] = length;
                for ( n = 0; n < ( 1 << k_extra_lbits[ code ] ); ++n )
                    s.length_code[ length++ ] = static_cast< std::uint8_t >( code );
            }
            s.length_code[ length - 1 ] = static_cast< std::uint8_t >( code );
            dist = 0;
            for ( code = 0; code < 16; ++code )
            {
                s.base_dist[ code ] = dist;
                for ( n = 0; n < ( 1 << k_extra_dbits[ code ] ); ++n )
                    s.dist_code[ dist++ ] = static_cast< std::uint8_t >( code );
            }
            dist >>= 7;
            for ( ; code < k_d_codes; ++code )
            {
                s.base_dist[ code ] = dist << 7;
                for ( n = 0; n < ( 1 << ( k_extra_dbits[ code ] - 7 ) ); ++n )
                    s.dist_code[ 256 + dist++ ] = static_cast< std::uint8_t >( code );
            }
            std::uint16_t bl_count[ k_max_bits + 1 ]{};
            n = 0;
            while ( n <= 143 )
                s.static_ltree[ n++ ].dl = 8, bl_count[ 8 ]++;
            while ( n <= 255 )
                s.static_ltree[ n++ ].dl = 9, bl_count[ 9 ]++;
            while ( n <= 279 )
                s.static_ltree[ n++ ].dl = 7, bl_count[ 7 ]++;
            while ( n < k_l_codes + 2 )
                s.static_ltree[ n++ ].dl = 8, bl_count[ 8 ]++;
            gen_codes( s.static_ltree, k_l_codes + 1, bl_count );
            for ( n = 0; n < k_d_codes; ++n )
            {
                s.static_dtree[ n ].dl = 5;
                s.static_dtree[ n ].fc = static_cast< std::uint16_t >( reverse( static_cast< std::uint32_t >( n ), 5 ) );
            }
            return s;
        }( );
        return t;
    }

    void init_tables( )
    {
        const tables_t& t = tables( );
        m_l_desc = { m_dyn_ltree, 0, t.static_ltree, k_extra_lbits, k_literals + 1, k_l_codes, k_max_bits };
        m_d_desc = { m_dyn_dtree, 0, t.static_dtree, k_extra_dbits, 0, k_d_codes, k_max_bits };
        m_bl_desc = { m_bl_tree, 0, nullptr, k_extra_blbits, 0, k_bl_codes, 7 };
    }

    static int d_code( std::uint32_t dist )
    {
        return dist < 256 ? tables( ).dist_code[ dist ] : tables( ).dist_code[ 256 + ( dist >> 7 ) ];
    }

    void init_block( )
    {
        for ( int n = 0; n < k_l_codes; ++n )
            m_dyn_ltree[ n ].fc = 0;
        for ( int n = 0; n < k_d_codes; ++n )
            m_dyn_dtree[ n ].fc = 0;
        for ( int n = 0; n < k_bl_codes; ++n )
            m_bl_tree[ n ].fc = 0;
        m_dyn_ltree[ k_end_block ].fc = 1;
        m_opt_len = m_static_len = 0;
        m_last_lit = 0;
    }

    void send_bits( std::uint32_t value, int length )
    {
        m_bit_buf |= static_cast< std::uint64_t >( value ) << m_bit_count;
        m_bit_count += length;
        while ( m_bit_count >= 8 )
        {
            m_out.push_back( static_cast< std::uint8_t >( m_bit_buf ) );
            m_bit_buf >>= 8;
            m_bit_count -= 8;
        }
    }

    void windup( )
    {
        if ( m_bit_count > 0 )
            m_out.push_back( static_cast< std::uint8_t >( m_bit_buf ) );
        m_bit_buf = 0;
        m_bit_count = 0;
    }

    void send_code( int c, const ct_t* tree )
    {
        send_bits( tree[ c ].fc, tree[ c ].dl );
    }

    bool smaller( const ct_t* tree, int n, int m ) const
    {
        return tree[ n ].fc < tree[ m ].fc || ( tree[ n ].fc == tree[ m ].fc && m_depth[ n ] <= m_depth[ m ] );
    }

    void pqdownheap( const ct_t* tree, int k )
    {
        const int v = m_heap[ k ];
        int j = k << 1;
        while ( j <= m_heap_len )
        {
            if ( j < m_heap_len && smaller( tree, m_heap[ j + 1 ], m_heap[ j ] ) )
                ++j;
            if ( smaller( tree, v, m_heap[ j ] ) )
                break;
            m_heap[ k ] = m_heap[ j ];
            k = j;
            j <<= 1;
        }
        m_heap[ k ] = v;
    }

    void gen_bitlen( desc_t& desc )
    {
        ct_t* tree = desc.tree;
        int h, n, m, bits, xbits, overflow = 0;
        for ( bits = 0; bits <= k_max_bits; ++bits )
            m_bl_count[ bits ] = 0;
        tree[ m_heap[ m_heap_max ] ].dl = 0;
        for ( h = m_heap_max + 1; h < k_heap_size; ++h )
        {
            n = m_heap[ h ];
            bits = tree[ tree[ n ].dl ].dl + 1;
            if ( bits > desc.max_length )
                bits = desc.max_length, ++overflow;
            tree[ n ].dl = static_cast< std::uint16_t >( bits );
            if ( n > desc.max_code )
                continue;
            m_bl_count[ bits ]++;
            xbits = 0;
            if ( n >= desc.base )
                xbits = desc.extra[ n - desc.base ];
            const std::uint32_t f = tree[ n ].fc;
            m_opt_len += f * static_cast< std::uint32_t >( bits + xbits );
            if ( desc.stree != nullptr )
                m_static_len += f * static_cast< std::uint32_t >( desc.stree[ n ].dl + xbits );
        }
        if ( overflow == 0 )
            return;
        do
        {
            bits = desc.max_length - 1;
            while ( m_bl_count[ bits ] == 0 )
                --bits;
            m_bl_count[ bits ]--;
            m_bl_count[ bits + 1 ] += 2;
            m_bl_count[ desc.max_length ]--;
            overflow -= 2;
        } while ( overflow > 0 );
        for ( bits = desc.max_length; bits != 0; --bits )
        {
            n = m_bl_count[ bits ];
            while ( n != 0 )
            {
                m = m_heap[ --h ];
                if ( m > desc.max_code )
                    continue;
                if ( tree[ m ].dl != bits )
                {
                    m_opt_len += ( static_cast< std::uint32_t >( bits ) - tree[ m ].dl ) * tree[ m ].fc;
                    tree[ m ].dl = static_cast< std::uint16_t >( bits );
                }
                --n;
            }
        }
    }

    void build_tree( desc_t& desc )
    {
        ct_t* tree = desc.tree;
        int n, m, max_code = -1, node;
        m_heap_len = 0;
        m_heap_max = k_heap_size;
        for ( n = 0; n < desc.elems; ++n )
        {
            if ( tree[ n ].fc != 0 )
            {
                m_heap[ ++m_heap_len ] = max_code = n;
                m_depth[ n ] = 0;
            }
            else
            {
                tree[ n ].dl = 0;
            }
        }
        while ( m_heap_len < 2 )
        {
            node = m_heap[ ++m_heap_len ] = ( max_code < 2 ? ++max_code : 0 );
            tree[ node ].fc = 1;
            m_depth[ node ] = 0;
            --m_opt_len;
            if ( desc.stree != nullptr )
                m_static_len -= desc.stree[ node ].dl;
        }
        desc.max_code = max_code;
        for ( n = m_heap_len / 2; n >= 1; --n )
            pqdownheap( tree, n );
        node = desc.elems;
        do
        {
            n = m_heap[ 1 ];
            m_heap[ 1 ] = m_heap[ m_heap_len-- ];
            pqdownheap( tree, 1 );
            m = m_heap[ 1 ];
            m_heap[ --m_heap_max ] = n;
            m_heap[ --m_heap_max ] = m;
            tree[ node ].fc = static_cast< std::uint16_t >( tree[ n ].fc + tree[ m ].fc );
            m_depth[ node ] = static_cast< std::uint8_t >( ( m_depth[ n ] >= m_depth[ m ] ? m_depth[ n ] : m_depth[ m ] ) + 1 );
            tree[ n ].dl = tree[ m ].dl = static_cast< std::uint16_t >( node );
            m_heap[ 1 ] = node++;
            pqdownheap( tree, 1 );
        } while ( m_heap_len >= 2 );
        m_heap[ --m_heap_max ] = m_heap[ 1 ];
        gen_bitlen( desc );
        gen_codes( tree, max_code, m_bl_count );
    }

    void scan_tree( ct_t* tree, int max_code )
    {
        int prevlen = -1, curlen, nextlen = tree[ 0 ].dl, count = 0, max_count = 7, min_count = 4;
        if ( nextlen == 0 )
            max_count = 138, min_count = 3;
        tree[ max_code + 1 ].dl = 0xFFFF;
        for ( int n = 0; n <= max_code; ++n )
        {
            curlen = nextlen;
            nextlen = tree[ n + 1 ].dl;
            if ( ++count < max_count && curlen == nextlen )
                continue;
            else if ( count < min_count )
                m_bl_tree[ curlen ].fc = static_cast< std::uint16_t >( m_bl_tree[ curlen ].fc + count );
            else if ( curlen != 0 )
            {
                if ( curlen != prevlen )
                    m_bl_tree[ curlen ].fc++;
                m_bl_tree[ k_rep_3_6 ].fc++;
            }
            else if ( count <= 10 )
                m_bl_tree[ k_repz_3_10 ].fc++;
            else
                m_bl_tree[ k_repz_11_138 ].fc++;
            count = 0;
            prevlen = curlen;
            if ( nextlen == 0 )
                max_count = 138, min_count = 3;
            else if ( curlen == nextlen )
                max_count = 6, min_count = 3;
            else
                max_count = 7, min_count = 4;
        }
    }

    void send_tree( ct_t* tree, int max_code )
    {
        int prevlen = -1, curlen, nextlen = tree[ 0 ].dl, count = 0, max_count = 7, min_count = 4;
        if ( nextlen == 0 )
            max_count = 138, min_count = 3;
        for ( int n = 0; n <= max_code; ++n )
        {
            curlen = nextlen;
            nextlen = tree[ n + 1 ].dl;
            if ( ++count < max_count && curlen == nextlen )
                continue;
            else if ( count < min_count )
            {
                do
                    send_code( curlen, m_bl_tree );
                while ( --count != 0 );
            }
            else if ( curlen != 0 )
            {
                if ( curlen != prevlen )
                {
                    send_code( curlen, m_bl_tree );
                    --count;
                }
                send_code( k_rep_3_6, m_bl_tree );
                send_bits( static_cast< std::uint32_t >( count - 3 ), 2 );
            }
            else if ( count <= 10 )
            {
                send_code( k_repz_3_10, m_bl_tree );
                send_bits( static_cast< std::uint32_t >( count - 3 ), 3 );
            }
            else
            {
                send_code( k_repz_11_138, m_bl_tree );
                send_bits( static_cast< std::uint32_t >( count - 11 ), 7 );
            }
            count = 0;
            prevlen = curlen;
            if ( nextlen == 0 )
                max_count = 138, min_count = 3;
            else if ( curlen == nextlen )
                max_count = 6, min_count = 3;
            else
                max_count = 7, min_count = 4;
        }
    }

    int build_bl_tree( )
    {
        scan_tree( m_dyn_ltree, m_l_desc.max_code );
        scan_tree( m_dyn_dtree, m_d_desc.max_code );
        build_tree( m_bl_desc );
        int max_blindex;
        for ( max_blindex = k_bl_codes - 1; max_blindex >= 3; --max_blindex )
            if ( m_bl_tree[ k_bl_order[ max_blindex ] ].dl != 0 )
                break;
        m_opt_len += 3 * ( static_cast< std::uint32_t >( max_blindex ) + 1 ) + 5 + 5 + 4;
        return max_blindex;
    }

    void compress_block( const ct_t* ltree, const ct_t* dtree )
    {
        const tables_t& t = tables( );
        for ( int lx = 0; lx < m_last_lit; ++lx )
        {
            std::uint32_t dist = m_dist_buf[ lx ];
            int lc = m_lit_buf[ lx ];
            if ( dist == 0 )
            {
                send_code( lc, ltree );
            }
            else
            {
                int code = t.length_code[ lc ];
                send_code( code + k_literals + 1, ltree );
                int extra = k_extra_lbits[ code ];
                if ( extra != 0 )
                    send_bits( static_cast< std::uint32_t >( lc - t.base_length[ code ] ), extra );
                --dist;
                code = d_code( dist );
                send_code( code, dtree );
                extra = k_extra_dbits[ code ];
                if ( extra != 0 )
                    send_bits( dist - static_cast< std::uint32_t >( t.base_dist[ code ] ), extra );
            }
        }
        send_code( k_end_block, ltree );
    }

    // _tr_flush_block; `buf` null when the block's start has slid out of the window.
    void flush_block( const std::uint8_t* buf, std::uint32_t stored_len, bool last )
    {
        build_tree( m_l_desc );
        build_tree( m_d_desc );
        const int max_blindex = build_bl_tree( );
        std::uint32_t opt_lenb = ( m_opt_len + 3 + 7 ) >> 3;
        const std::uint32_t static_lenb = ( m_static_len + 3 + 7 ) >> 3;
        if ( static_lenb <= opt_lenb )
            opt_lenb = static_lenb;
        if ( stored_len + 4 <= opt_lenb && buf != nullptr )
        {
            send_bits( last ? 1 : 0, 3 );
            windup( );
            const std::uint16_t len = static_cast< std::uint16_t >( stored_len );
            m_out.push_back( static_cast< std::uint8_t >( len ) );
            m_out.push_back( static_cast< std::uint8_t >( len >> 8 ) );
            m_out.push_back( static_cast< std::uint8_t >( ~len ) );
            m_out.push_back( static_cast< std::uint8_t >( ~len >> 8 ) );
            m_out.insert( m_out.end( ), buf, buf + stored_len );
        }
        else if ( static_lenb == opt_lenb )
        {
            send_bits( 2 + ( last ? 1 : 0 ), 3 );
            compress_block( tables( ).static_ltree, tables( ).static_dtree );
        }
        else
        {
            send_bits( 4 + ( last ? 1 : 0 ), 3 );
            const int lcodes = m_l_desc.max_code + 1, dcodes = m_d_desc.max_code + 1, blcodes = max_blindex + 1;
            send_bits( static_cast< std::uint32_t >( lcodes - 257 ), 5 );
            send_bits( static_cast< std::uint32_t >( dcodes - 1 ), 5 );
            send_bits( static_cast< std::uint32_t >( blcodes - 4 ), 4 );
            for ( int rank = 0; rank < blcodes; ++rank )
                send_bits( m_bl_tree[ k_bl_order[ rank ] ].dl, 3 );
            send_tree( m_dyn_ltree, lcodes - 1 );
            send_tree( m_dyn_dtree, dcodes - 1 );
            compress_block( m_dyn_ltree, m_dyn_dtree );
        }
        init_block( );
        if ( last )
            windup( );
    }

    void flush_only( bool last )
    {
        flush_block( m_block_start >= 0 ? m_window.data( ) + m_block_start : nullptr, static_cast< std::uint32_t >( static_cast< std::int64_t >( m_strstart ) - m_block_start ), last );
        m_block_start = m_strstart;
    }

    // _tr_tally: true when the block is full.
    bool tally( std::uint32_t dist, int lc )
    {
        m_dist_buf[ m_last_lit ] = static_cast< std::uint16_t >( dist );
        m_lit_buf[ m_last_lit++ ] = static_cast< std::uint8_t >( lc );
        if ( dist == 0 )
        {
            m_dyn_ltree[ lc ].fc++;
        }
        else
        {
            --dist;
            m_dyn_ltree[ tables( ).length_code[ lc ] + k_literals + 1 ].fc++;
            m_dyn_dtree[ d_code( dist ) ].fc++;
        }
        return m_last_lit == k_lit_bufsize - 1;
    }

    void update_hash( std::uint8_t c )
    {
        m_ins_h = ( ( m_ins_h << k_hash_shift ) ^ c ) & k_hash_mask;
    }

    std::uint32_t insert_string( std::uint32_t str )
    {
        update_hash( m_window[ str + 2 ] );
        const std::uint32_t head = m_prev[ str & k_wmask ] = m_head[ m_ins_h ];
        m_head[ m_ins_h ] = static_cast< std::uint16_t >( str );
        return head;
    }

    std::uint32_t read_buf( std::uint8_t* to, std::uint32_t size )
    {
        std::uint32_t len = m_left < size ? static_cast< std::uint32_t >( m_left ) : size;
        if ( len == 0 )
            return 0;
        m_left -= len;
        std::memcpy( to, m_in, len );
        for ( std::uint32_t k = 0; k < len; ++k )
        {
            m_adler_a = ( m_adler_a + to[ k ] ) % 65521u;
            m_adler_b = ( m_adler_b + m_adler_a ) % 65521u;
        }
        m_in += len;
        return len;
    }

    void fill_window( )
    {
        do
        {
            std::uint32_t more = 2 * k_wsize - m_lookahead - m_strstart;
            if ( m_strstart >= static_cast< std::uint32_t >( k_wsize + k_max_dist ) )
            {
                std::memcpy( m_window.data( ), m_window.data( ) + k_wsize, k_wsize );
                m_match_start -= k_wsize;
                m_strstart -= k_wsize;
                m_block_start -= k_wsize;
                for ( std::uint16_t& h : m_head )
                    h = static_cast< std::uint16_t >( h >= k_wsize ? h - k_wsize : 0 );
                for ( std::uint16_t& p : m_prev )
                    p = static_cast< std::uint16_t >( p >= k_wsize ? p - k_wsize : 0 );
                more += k_wsize;
            }
            if ( m_left == 0 )
                break;
            m_lookahead += read_buf( m_window.data( ) + m_strstart + m_lookahead, more );
            if ( m_lookahead >= static_cast< std::uint32_t >( k_min_match ) )
            {
                m_ins_h = m_window[ m_strstart ];
                update_hash( m_window[ m_strstart + 1 ] );
            }
        } while ( m_lookahead < static_cast< std::uint32_t >( k_min_lookahead ) && m_left != 0 );
    }

    std::uint32_t longest_match( std::uint32_t cur_match )
    {
        int chain = k_chain;
        const std::uint8_t* scan = m_window.data( ) + m_strstart;
        int best_len = static_cast< int >( m_prev_length );
        int nice = k_nice;
        const std::uint32_t limit = m_strstart > static_cast< std::uint32_t >( k_max_dist ) ? m_strstart - k_max_dist : 0;
        std::uint8_t scan_end1 = scan[ best_len - 1 ], scan_end = scan[ best_len ];
        if ( m_prev_length >= static_cast< std::uint32_t >( k_good ) )
            chain >>= 2;
        if ( static_cast< std::uint32_t >( nice ) > m_lookahead )
            nice = static_cast< int >( m_lookahead );
        do
        {
            const std::uint8_t* match = m_window.data( ) + cur_match;
            if ( match[ best_len ] != scan_end || match[ best_len - 1 ] != scan_end1 || match[ 0 ] != scan[ 0 ] || match[ 1 ] != scan[ 1 ] )
                continue;
            int len = 3;
            while ( len < k_max_match && scan[ len ] == match[ len ] )
                ++len;
            if ( len > best_len )
            {
                m_match_start = cur_match;
                best_len = len;
                if ( len >= nice )
                    break;
                scan_end1 = scan[ best_len - 1 ];
                scan_end = scan[ best_len ];
            }
        } while ( ( cur_match = m_prev[ cur_match & k_wmask ] ) > limit && --chain != 0 );
        return static_cast< std::uint32_t >( best_len ) <= m_lookahead ? static_cast< std::uint32_t >( best_len ) : m_lookahead;
    }

    void run( )
    {
        m_out.push_back( 0x78 );
        m_out.push_back( 0x9C );
        bool match_available = false;
        m_match_length = m_prev_length = k_min_match - 1;
        for ( ;; )
        {
            if ( m_lookahead < static_cast< std::uint32_t >( k_min_lookahead ) )
            {
                fill_window( );
                if ( m_lookahead == 0 )
                    break;
            }
            std::uint32_t hash_head = 0;
            if ( m_lookahead >= static_cast< std::uint32_t >( k_min_match ) )
                hash_head = insert_string( m_strstart );
            m_prev_length = m_match_length;
            const std::uint32_t prev_match = m_match_start;
            m_match_length = k_min_match - 1;
            if ( hash_head != 0 && m_prev_length < static_cast< std::uint32_t >( k_lazy ) && m_strstart - hash_head <= static_cast< std::uint32_t >( k_max_dist ) )
            {
                m_match_length = longest_match( hash_head );
                if ( m_match_length <= 5 && m_match_length == static_cast< std::uint32_t >( k_min_match ) && m_strstart - m_match_start > static_cast< std::uint32_t >( k_too_far ) )
                    m_match_length = k_min_match - 1;
            }
            if ( m_prev_length >= static_cast< std::uint32_t >( k_min_match ) && m_match_length <= m_prev_length )
            {
                const std::uint32_t max_insert = m_strstart + m_lookahead - k_min_match;
                const bool full = tally( m_strstart - 1 - prev_match, static_cast< int >( m_prev_length ) - k_min_match );
                m_lookahead -= m_prev_length - 1;
                m_prev_length -= 2;
                do
                {
                    if ( ++m_strstart <= max_insert )
                        insert_string( m_strstart );
                } while ( --m_prev_length != 0 );
                match_available = false;
                m_match_length = k_min_match - 1;
                ++m_strstart;
                if ( full )
                    flush_only( false );
            }
            else if ( match_available )
            {
                if ( tally( 0, m_window[ m_strstart - 1 ] ) )
                    flush_only( false );
                ++m_strstart;
                --m_lookahead;
            }
            else
            {
                match_available = true;
                ++m_strstart;
                --m_lookahead;
            }
        }
        if ( match_available )
            tally( 0, m_window[ m_strstart - 1 ] );
        flush_only( true );
        for ( int shift = 8; shift >= 0; shift -= 8 )
            m_out.push_back( static_cast< std::uint8_t >( m_adler_b >> shift ) );
        for ( int shift = 8; shift >= 0; shift -= 8 )
            m_out.push_back( static_cast< std::uint8_t >( m_adler_a >> shift ) );
    }

    const std::uint8_t* m_in;
    std::size_t m_left;
    std::vector< std::uint8_t > m_out, m_window, m_lit_buf;
    std::vector< std::uint16_t > m_prev, m_head, m_dist_buf;
    ct_t m_dyn_ltree[ k_heap_size ], m_dyn_dtree[ 2 * k_d_codes + 1 ], m_bl_tree[ 2 * k_bl_codes + 1 ];
    desc_t m_l_desc{}, m_d_desc{}, m_bl_desc{};
    std::uint16_t m_bl_count[ k_max_bits + 1 ]{};
    int m_heap[ 2 * k_l_codes + 1 ]{};
    int m_heap_len = 0, m_heap_max = 0;
    std::uint8_t m_depth[ 2 * k_l_codes + 1 ]{};
    std::uint32_t m_opt_len = 0, m_static_len = 0;
    int m_last_lit = 0;
    std::uint64_t m_bit_buf = 0;
    int m_bit_count = 0;
    std::uint32_t m_ins_h = 0, m_strstart = 0, m_lookahead = 0, m_match_start = 0, m_match_length = 0, m_prev_length = 0;
    std::int64_t m_block_start = 0;
    std::uint32_t m_adler_a = 1, m_adler_b = 0;
};
