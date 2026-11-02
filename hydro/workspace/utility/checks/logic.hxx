#pragma once

// The logic entities (world/logic.hxx) against retail (case logic_plugs): per vector the state / text / params / VuRand
// set as retail had them, the input plug dispatched through g_logic_plugs by (vtable, name), every output fire answered
// from the drawn list as retail's 0x5023A0 would ({0, 0} for a void plug after its link ran; the link's answer for a
// typed one). Compared: the VuRetVal words (a type-4 value by text), the fires (name, params size and bytes), the answers
// consumed, the state words / text after, the VuRand after. The "*ctor" rows: logic_construct's words against the ctor's.

#include <workspace/core/assets/reader.hxx>
#include <workspace/core/game/docked.hxx>
#include <workspace/core/world/logic.hxx>

#include <string>
#include <vector>

struct logic_check_t
{
    const unsigned* s;

    unsigned next( ) { return *s++; }

    std::string str( )
    {
        const unsigned n = next( );
        std::string out( reinterpret_cast< const char* >( s ), n );
        s += ( n + 3 ) / 4;
        return out;
    }
};

struct logic_check_log_t
{
    std::vector< std::pair< std::uint32_t, std::string > > answers;   // type, value word or text
    std::vector< std::uint32_t > values;
    std::size_t used = 0;
    std::vector< std::string > fired;
    std::vector< ui_params_t > params;
    std::string texts[ 16 ];
};

inline bool verify_logic_plugs( )
{
    logic_check_t c{ g_golden_lgp_words };
    const unsigned n = c.next( );
    int bad = 0, plugs = 0, ctors = 0, fires = 0, reported = 0;
    for ( unsigned i = 0; i < n; ++i )
    {
        const std::string cls = c.str( );
        const std::string plug = c.str( );
        const std::uint32_t vt = logic_vtable_of( cls.c_str( ) );
        if ( plug == "*ctor" )
        {
            const unsigned vtable = c.next( ), size = c.next( ), nw = c.next( );
            const logic_entity_t e = logic_construct( vt );
            bad += vt != vtable;
            for ( unsigned k = 0; k < nw; ++k )
            {
                const unsigned want = c.next( );
                // the string classes: +0x60 .. +0x6F the inline bytes, +0x70 size, +0x74 capacity 15 (an empty VuString)
                const unsigned have = ( cls == "VuStringEntity" || cls == "VuSetStringEntity" ) ? ( k == 5 ? 15u : 0u ) : e.words[ k ];
                bad += have != want;
            }
            bad += ( cls == "VuStringEntity" || cls == "VuSetStringEntity" ) && !e.text.empty( );
            ( void )size;
            ++ctors;
            continue;
        }
        const int bad_before = bad;
        logic_entity_t e = logic_construct( vt );
        const unsigned ns = c.next( );
        for ( unsigned k = 0; k < ns; ++k )
            e.words[ k ] = c.next( );
        if ( c.next( ) )
            e.text = c.str( );
        ui_params_t in{ };
        in.m_size = static_cast< int >( c.next( ) );
        std::memcpy( in.m_bytes, c.s, 128 );
        c.s += 32;
        logic_check_log_t L;
        const unsigned na = c.next( );
        for ( unsigned k = 0; k < na; ++k )
        {
            const unsigned t = c.next( );
            if ( t == 4u )
                L.answers.push_back( { t, c.str( ) } ), L.values.push_back( 0u );
            else
                L.answers.push_back( { t, std::string( ) } ), L.values.push_back( c.next( ) );
        }
        pfx_rand_t rnd;
        rnd.seed = static_cast< std::int32_t >( c.next( ) );
        rnd.previous = static_cast< std::int32_t >( c.next( ) );
        for ( int k = 0; k < 32; ++k )
            rnd.table[ k ] = static_cast< std::int32_t >( c.next( ) );
        const unsigned rt = c.next( ), rv = c.next( );
        std::string rtext;
        const bool has_text = c.next( ) != 0u;
        if ( has_text )
            rtext = c.str( );
        const unsigned used = c.next( );
        logic_hooks_t h;
        h.user = &L;
        h.rand = &rnd;
        h.fire = []( void* u, logic_entity_t& ent, const char* name, const ui_params_t& p ) -> logic_retval_t {
            logic_check_log_t& l = *static_cast< logic_check_log_t* >( u );
            l.fired.push_back( name );
            l.params.push_back( p );
            logic_retval_t r{ };
            if ( l.used < l.answers.size( ) )
            {
                const std::size_t k = l.used++;
                const logic_output_t* o = logic_output( ent.vtable, name );
                if ( o && o->type != 0u )
                {
                    r.type = l.answers[ k ].first;
                    r.value = l.values[ k ];
                    if ( r.type == 4u )
                    {
                        l.texts[ k % 16 ] = l.answers[ k ].second;
                        r.text = l.texts[ k % 16 ].c_str( );
                    }
                }
            }
            return r;
        };
        logic_retval_t out{ 0xA5A5A5A5u, 0xA5A5A5A5u, nullptr };
        bool ok = true;
        if ( plug == "*init" )
            logic_switch_init( e );
        else
            ok = logic_dispatch( e, plug.c_str( ), out, in, h );
        bad += !ok;
        bad += out.type != rt;
        // The VuMethod1 invoke 0x4050E0 copies the handler's LOCAL VuRetVal out: a word (or the bool's upper three bytes)
        // the handler does not write is retail's stale stack - compared only where the C++ wrote it.
        if ( has_text )
            bad += !out.text || rtext != out.text;
        else if ( out.type == 3u )
            bad += ( out.value & 0xFFu ) != ( rv & 0xFFu );
        else if ( out.value != 0xA5A5A5A5u )
            bad += out.value != rv;
        bad += L.used != used;
        const unsigned nf = c.next( );
        bad += L.fired.size( ) != nf;
        for ( unsigned k = 0; k < nf; ++k )
        {
            const std::string name = c.str( );
            const unsigned size = c.next( );
            const std::string bytes = c.str( );
            if ( k < L.fired.size( ) )
            {
                const ui_params_t& p = L.params[ k ];
                bad += L.fired[ k ] != name;
                bad += static_cast< unsigned >( p.m_size ) != size;
                bad += bytes.size( ) > 128 || std::memcmp( bytes.data( ), p.m_bytes, bytes.size( ) ) != 0;
            }
            ++fires;
        }
        const unsigned na2 = c.next( );
        for ( unsigned k = 0; k < na2; ++k )
            bad += e.words[ k ] != c.next( );
        if ( c.next( ) )
            bad += e.text != c.str( );
        bad += static_cast< unsigned >( rnd.seed ) != c.next( );
        bad += static_cast< unsigned >( rnd.previous ) != c.next( );
        for ( int k = 0; k < 32; ++k )
            bad += static_cast< unsigned >( rnd.table[ k ] ) != c.next( );
        ++plugs;
        if ( bad != bad_before && reported++ < 12 )
            LOG_ERROR( "logic_plugs: %s %s vector %u: bad %d (ret %08X %08X want %08X %08X; fires %u want %u; used %u want %u)", cls.c_str( ),
                       plug.c_str( ), i, bad - bad_before, out.type, out.value, rt, rv, static_cast< unsigned >( L.fired.size( ) ), nf,
                       static_cast< unsigned >( L.used ), used );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "world/logic.hxx matches the engine's logic entities' handlers (18 classes, 27 input plugs + the switch's slot 6) on "
                     "%d fired plugs (%d output fires) and %d ctor images", plugs, fires, ctors );
    else
        LOG_ERROR( "logic_plugs FAILED %d", bad );
    return bad == 0;
}

// g_logic_types (world/logic.hxx) against the classes' getType slots run by retail (case logic_types).
inline bool verify_logic_types( )
{
    int bad = 0;
    const std::size_t n = sizeof( g_logic_types ) / sizeof( g_logic_types[ 0 ] );
    for ( std::size_t i = 0; i < g_golden_lgt_count; ++i )
    {
        const golden_logic_type_t& g = g_golden_lgt[ i ];
        const logic_type_t* t = nullptr;
        for ( std::size_t j = 0; j < n; ++j )
            if ( g_logic_types[ j ].address == g.address )
                t = &g_logic_types[ j ];
        if ( t == nullptr || t->descriptor != g.descriptor || std::strcmp( t->chain, g.chain ) != 0 )
        {
            ++bad;
            LOG_ERROR( "logic_types 0x%X: descriptor / chain differ (retail 0x%X %s)", g.address, g.descriptor, g.chain );
        }
    }
    bad += n != g_golden_lgt_count ? 1 : 0;
    // the components' name getters (VuControlComponent's 0x619F50 first)
    const std::size_t nn = sizeof( g_component_names ) / sizeof( g_component_names[ 0 ] );
    bad += g_golden_lgt_name_count == nn && std::strcmp( g_component_names[ 0 ].name, control_component_name( ) ) == 0 ? 0 : 1;
    for ( std::size_t i = 0; i < g_golden_lgt_name_count && i < nn; ++i )
        if ( g_golden_lgt_names[ i ].address != g_component_names[ i ].address || std::strcmp( g_golden_lgt_names[ i ].chain, g_component_names[ i ].name ) != 0 )
        {
            ++bad;
            LOG_ERROR( "logic_types name getter 0x%X differs (retail %s)", g_golden_lgt_names[ i ].address, g_golden_lgt_names[ i ].chain );
        }
    if ( bad == 0 )
        LOG_SUCCESS( "g_logic_types match the logic / script / framework classes' %d getType slots (descriptor and chain each); %d component name getters",
                     static_cast< int >( n ), static_cast< int >( nn ) );
    else
        LOG_ERROR( "logic_types FAILED %d", bad );
    return bad == 0;
}

// The game-tier filters (case logic_game): the environment as retail had it - the document parsed by c_reader (= 0x55B560)
// as gameutil +0x50, the SKU string, the repository's table (a missing hash answers the first entity, as the case's stub
// did) and the entity / driver words. Compared: the VuRetVal type (the value word is the handler's stale stack), the
// hashes the repository was asked for, the fires (name, params size and bytes), the text after; nothing unrecreated.
struct logic_game_check_t
{
    std::vector< std::uint32_t > hashes, entities, asked;
    std::vector< std::pair< std::uint32_t, std::uint32_t > > words;
    std::vector< std::string > fired;
    std::vector< ui_params_t > params;
};

inline bool verify_logic_game( )
{
    logic_check_t c{ g_golden_lgg_words };
    const unsigned n = c.next( );
    int bad = 0, fires = 0, reported = 0, unrecreated = 0;
    for ( unsigned i = 0; i < n; ++i )
    {
        const int bad_before = bad;
        const std::string cls = c.str( );
        const std::string plug = c.str( );
        logic_entity_t e = logic_construct( logic_vtable_of( cls.c_str( ) ) );
        if ( c.next( ) )
            e.text = c.str( );
        value_t doc{ };
        const bool has_doc = c.next( ) != 0u;
        if ( has_doc )
        {
            const std::string raw = c.str( );
            doc = c_reader( std::vector< std::uint8_t >( raw.begin( ), raw.end( ) ) ).parse( );
        }
        std::string sku;
        const bool has_sku = c.next( ) != 0u;
        if ( has_sku )
            sku = c.str( );
        logic_game_check_t L;
        const unsigned ne = c.next( );
        for ( unsigned k = 0; k < ne; ++k )
        {
            const std::uint32_t hash = c.next( ), entity = c.next( ), vtable = c.next( ), driver = c.next( ), dvt = c.next( );
            L.hashes.push_back( hash );
            L.entities.push_back( entity );
            L.words.push_back( { entity, vtable } );
            L.words.push_back( { entity + 0x220u, driver } );
            L.words.push_back( { driver, dvt } );
        }
        ui_params_t in{ };
        in.m_size = static_cast< int >( c.next( ) );
        std::memcpy( in.m_bytes, c.s, 128 );
        c.s += 32;
        const unsigned rt = c.next( );
        c.next( );
        logic_game_t g;
        g.game_data = has_doc ? &doc : nullptr;
        g.sku = has_sku ? &sku : nullptr;
        g.user = &L;
        g.unrecreated = &unrecreated;
        g.find = []( void* u, std::uint32_t hash ) -> std::uint32_t {
            logic_game_check_t& l = *static_cast< logic_game_check_t* >( u );
            l.asked.push_back( hash );
            for ( std::size_t k = 0; k < l.hashes.size( ); ++k )
                if ( l.hashes[ k ] == hash )
                    return l.entities[ k ];
            return l.entities.empty( ) ? 0u : l.entities[ 0 ];
        };
        g.word = []( void* u, std::uint32_t address ) -> std::uint32_t {
            for ( const auto& w : static_cast< logic_game_check_t* >( u )->words )
                if ( w.first == address )
                    return w.second;
            return 0u;
        };
        logic_hooks_t h;
        h.user = &L;
        h.game = &g;
        h.fire = []( void* u, logic_entity_t&, const char* name, const ui_params_t& p ) -> logic_retval_t {
            logic_game_check_t& l = *static_cast< logic_game_check_t* >( u );
            l.fired.push_back( name );
            l.params.push_back( p );
            return logic_retval_t{ };
        };
        logic_retval_t out{ 0xA5A5A5A5u, 0xA5A5A5A5u, nullptr };
        bad += !logic_dispatch( e, plug.c_str( ), out, in, h );
        bad += out.type != rt;
        const unsigned na = c.next( );
        bad += L.asked.size( ) != na;
        for ( unsigned k = 0; k < na; ++k )
        {
            const unsigned want = c.next( );
            bad += k < L.asked.size( ) && L.asked[ k ] != want;
        }
        const unsigned nf = c.next( );
        bad += L.fired.size( ) != nf;
        for ( unsigned k = 0; k < nf; ++k )
        {
            const std::string name = c.str( );
            const unsigned size = c.next( );
            const std::string bytes = c.str( );
            if ( k < L.fired.size( ) )
            {
                bad += L.fired[ k ] != name;
                bad += static_cast< unsigned >( L.params[ k ].m_size ) != size;
                bad += bytes.size( ) > 128 || std::memcmp( bytes.data( ), L.params[ k ].m_bytes, bytes.size( ) ) != 0;
            }
            ++fires;
        }
        if ( c.next( ) )
            bad += e.text != c.str( );
        if ( bad != bad_before && reported++ < 12 )
            LOG_ERROR( "logic_game: %s vector %u: bad %d (fires %u want %u)", cls.c_str( ), i, bad - bad_before,
                       static_cast< unsigned >( L.fired.size( ) ), nf );
    }
    // the text classes' ctor images (VuIsEventTypeEntity's "AttractEvent", VuIsSkuEntity's empty SKU)
    const unsigned nc = c.next( );
    for ( unsigned k = 0; k < nc; ++k )
    {
        const std::string cls = c.str( );
        const std::string want = c.next( ) ? c.str( ) : std::string( );
        bad += logic_construct( logic_vtable_of( cls.c_str( ) ) ).text != want;
    }
    bad += unrecreated;
    if ( bad == 0 )
        LOG_SUCCESS( "world/logic.hxx matches VuIsTegraEntity 0x42DAA0 / VuIsSkuEntity 0x42CFF0 / VuHumanFilterEntity 0x42D370 / "
                     "VuIsEventTypeEntity 0x42CB00 on %u fired "
                     "plugs (%d output fires; documents, SKUs, repository tables and driver kinds as retail had them)", n, fires );
    else
        LOG_ERROR( "logic_game FAILED %d (%d unrecreated paths reached)", bad, unrecreated );
    return bad == 0;
}

// VuControlEntity (world/keyframe.hxx) against retail (case control_entity): per scenario the children, the component's
// records, the ref / component / templated inputs, the six bools and the real-time step as retail had them, the
// documents parsed by c_reader (= 0x55B560); each step replayed on control_entity_t with hooks logging what the case's
// stubs logged. Compared after every step: the state words (the entity, every keyframe's time / VuArray / property
// list by name hash, the component's +0x14 and records) and the log (fires, interface slots, tick registrations);
// nothing may count unrecreated.
#include <workspace/core/world/keyframe.hxx>

struct control_check_t
{
    std::vector< std::vector< std::uint32_t > > log;
};

inline std::uint32_t control_check_fnv( const std::string& s )
{
    std::uint32_t h = 0x811C9DC5u;
    for ( const unsigned char c : s )
        h = ( h ^ c ) * 0x01000193u;
    return h;
}

inline std::vector< std::uint32_t > control_check_state( const control_entity_t& e, const control_component_t* comp )
{
    std::vector< std::uint32_t > w = { e.initially_active, e.one_shot, e.network_sync, e.loop, e.ease, e.real_time, e.active, e.done };
    w.push_back( e.target == nullptr ? 0u : ( e.target == comp ? 1u : 2u ) );
    w.push_back( static_cast< std::uint32_t >( e.keys.size( ) ) );
    w.push_back( static_cast< std::uint32_t >( e.capacity ) );
    for ( const auto& k : e.keys )
    {
        w.push_back( k.first );
        w.push_back( static_cast< std::uint32_t >( k.second ) );
    }
    w.push_back( e.duration );
    w.push_back( e.time );
    for ( const control_child_t& ch : e.children )
    {
        if ( !ch.keyframe )
            continue;
        w.push_back( ch.frame.time );
        w.push_back( static_cast< std::uint32_t >( ch.frame.values.size( ) ) );
        w.push_back( static_cast< std::uint32_t >( ch.frame.capacity ) );
        w.insert( w.end( ), ch.frame.values.begin( ), ch.frame.values.end( ) );
        w.push_back( static_cast< std::uint32_t >( ch.frame.properties.size( ) ) );
        for ( const control_property_t& p : ch.frame.properties )
        {
            w.push_back( control_check_fnv( p.name ) );
            w.push_back( p.fallback );
            w.push_back( p.current );
        }
    }
    if ( comp )
    {
        w.push_back( comp->controller == nullptr ? 0u : ( comp->controller == &e ? 1u : 2u ) );
        for ( int i = 0; i < comp->count( ); ++i )
        {
            w.push_back( comp->records[ 3 * i + 1 ] );
            w.push_back( comp->records[ 3 * i + 2 ] );
        }
    }
    return w;
}

inline bool verify_control_entity( )
{
    logic_check_t c{ g_golden_cte_words };
    const unsigned n = c.next( );
    int bad = 0, steps = 0, reported = 0, unrecreated = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        control_entity_t e;
        const unsigned nk = c.next( );
        for ( unsigned k = 0; k < nk; ++k )
        {
            control_child_t ch;
            ch.keyframe = c.next( ) != 0;
            ch.name = c.str( );
            e.children.push_back( ch );
        }
        control_component_t comp;
        const unsigned nr = c.next( );
        for ( unsigned r = 0; r < nr; ++r )
        {
            comp.names.push_back( c.str( ) );
            comp.records.push_back( 0u );
            comp.records.push_back( c.next( ) );
            comp.records.push_back( c.next( ) );
        }
        const bool linked = c.next( ) != 0, has = c.next( ) != 0, templated = c.next( ) != 0;
        std::uint8_t bools[ 6 ];
        for ( auto& b : bools )
            b = static_cast< std::uint8_t >( c.next( ) );
        const std::uint32_t rt = c.next( );
        const std::string doc_bytes = c.str( );
        value_t doc = c_reader( std::vector< std::uint8_t >( doc_bytes.begin( ), doc_bytes.end( ) ) ).parse( );
        value_t tdoc;
        if ( c.next( ) )
        {
            const std::string t = c.str( );
            tdoc = c_reader( std::vector< std::uint8_t >( t.begin( ), t.end( ) ) ).parse( );
        }
        control_check_t log;
        control_component_t* cp = has ? &comp : nullptr;
        comp.user = &log;
        comp.changed = []( void* u, control_component_t& ) { static_cast< control_check_t* >( u )->log.push_back( { 2u, 0u } ); };
        comp.begin = []( void* u, control_component_t& ) { static_cast< control_check_t* >( u )->log.push_back( { 2u, 1u } ); };
        comp.end = []( void* u, control_component_t& ) { static_cast< control_check_t* >( u )->log.push_back( { 2u, 2u } ); };
        struct env_t { control_check_t* log; float rt; } env{ &log, control_f( rt ) };
        control_hooks_t h;
        h.user = &env;
        h.unrecreated = &unrecreated;
        h.fire = []( void* u, control_entity_t&, const char* plug ) { static_cast< env_t* >( u )->log->log.push_back( { 1u, control_check_fnv( plug ) } ); };
        h.tick_register = []( void* u, control_entity_t&, bool on )
        {
            if ( on )
                static_cast< env_t* >( u )->log->log.push_back( { 3u, control_check_fnv( "Motion" ), 0x7529F8u, 1u, 0x5CD9F0u } );
            else
                static_cast< env_t* >( u )->log->log.push_back( { 4u, 1u } );
        };
        h.real_dt = []( void* u ) { return static_cast< env_t* >( u )->rt; };
        const control_component_t* bound = linked && has ? &comp : nullptr;
        const unsigned ns = c.next( );
        for ( unsigned s = 0; s < ns; ++s )
        {
            const unsigned op = c.next( );
            const unsigned a0 = c.next( ), a1 = c.next( ), a2 = c.next( );
            std::vector< std::vector< std::uint32_t > > want_log( c.next( ) );
            for ( auto& l : want_log )
            {
                l.resize( c.next( ) );
                for ( auto& x : l )
                    x = c.next( );
            }
            std::vector< std::uint32_t > want( c.next( ) );
            for ( auto& x : want )
                x = c.next( );
            log.log.clear( );
            int expect_ret = -1;
            switch ( op )
            {
                case 0:
                    e.initially_active = bools[ 0 ]; e.one_shot = bools[ 1 ]; e.network_sync = bools[ 2 ];
                    e.loop = bools[ 3 ]; e.ease = bools[ 4 ]; e.real_time = bools[ 5 ];
                    break;
                case 1: control_load( e, doc ); break;
                case 2: control_load_template( e, tdoc ); break;
                case 3: control_post_load( e, bound, templated ); break;
                case 4: control_game_init( e, linked, linked ? cp : nullptr, h ); break;
                case 5: control_game_release( e, h ); break;
                case 6: control_reset( e ); break;
                case 7: control_rebind( e, bound ); break;
                case 8: control_child_added( e, static_cast< int >( a0 ), bound ); break;
                case 9: control_activate( e, h ); expect_ret = static_cast< int >( a0 ); break;
                case 10: control_deactivate( e, h ); expect_ret = static_cast< int >( a0 ); break;
                case 11: control_tick( e, control_f( a0 ), h ); break;
                case 12:
                {
                    std::uint8_t p[ 8 ];
                    std::memcpy( p, &a0, 4 );
                    std::memcpy( p + 4, &a1, 4 );
                    control_manual_tick( e, p, static_cast< std::int32_t >( a2 ), h );
                    break;
                }
                default: ++bad; break;
            }
            ++steps;
            const std::vector< std::uint32_t > got = control_check_state( e, has ? &comp : nullptr );
            int diff = expect_ret > 0 ? 1 : 0;   // the VuRetVal type word retail wrote (0: void)
            for ( std::size_t i = 0; i < ( std::max )( got.size( ), want.size( ) ); ++i )
                diff += i >= got.size( ) || i >= want.size( ) || got[ i ] != want[ i ] ? 1 : 0;
            diff += log.log == want_log ? 0 : 1;
            if ( diff && reported++ < 6 )
                LOG_ERROR( "control_entity scenario %u step %u (op %u): %d words differ (state %d / %d words, log %d / %d)", v, s, op, diff,
                           static_cast< int >( got.size( ) ), static_cast< int >( want.size( ) ), static_cast< int >( log.log.size( ) ),
                           static_cast< int >( want_log.size( ) ) );
            bad += diff;
        }
    }
    bad += unrecreated;
    if ( bad == 0 )
        LOG_SUCCESS( "VuControlEntity matches retail: %u scenarios, %d steps (slots 2 / 3 / 4 / 6 / 7 / 8 / 9, 0x5CD990, Activate, Deactivate, OnManualTick, the tick; every state word and log entry)", n, steps );
    else
        LOG_ERROR( "control_entity FAILED %d (unrecreated %d)", bad, unrecreated );
    return bad == 0;
}
// VuLensWaterEmitterEntity (world/lens.hxx) against retail (case lens_water): the emitter slot 0's answer bit for bit on
// drawn positions / points / radii / rates; slots 6 / 7 on the entity and a null this (the manager calls and their
// argument); the ctor's interface vtable 0x7555B0 and the two defaults (schema.hxx: 25 / 64).
#include <workspace/core/world/lens.hxx>

inline bool verify_lens_water( )
{
    logic_check_t c{ g_golden_lwe_words };
    const unsigned n = c.next( );
    int bad = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        float pos[ 3 ], pt[ 3 ];
        for ( float& x : pos )
            x = control_f( c.next( ) );
        for ( float& x : pt )
            x = control_f( c.next( ) );
        const float rad = control_f( c.next( ) ), drops = control_f( c.next( ) );
        const std::uint32_t want = c.next( );
        const std::uint32_t got = control_w( lens_water_emitter_rate( pos, rad, drops, pt ) );
        if ( got != want )
        {
            if ( bad < 6 )
                LOG_ERROR( "lens_water vector %u: 0x%08X, retail 0x%08X", v, got, want );
            ++bad;
        }
    }
    const unsigned nc = c.next( );
    for ( unsigned k = 0; k < nc; ++k )
    {
        std::vector< std::uint32_t > want( c.next( ) );
        for ( auto& x : want )
            x = c.next( );
        std::vector< std::uint32_t > got = { want[ 0 ], want[ 1 ] };
        static const int emitter_tag = 0;
        const void* emitter = want[ 1 ] ? &emitter_tag : nullptr;
        struct log_t { std::vector< std::uint32_t >* out; const void* e; } lg{ &got, &emitter_tag };
        lens_water_hooks_t h;
        h.user = &lg;
        h.add = []( void* u, const void* e ) { auto* l = static_cast< log_t* >( u ); l->out->push_back( 0u ); l->out->push_back( e == l->e ? 1u : ( e ? 2u : 0u ) ); };
        h.remove = []( void* u, const void* e ) { auto* l = static_cast< log_t* >( u ); l->out->push_back( 1u ); l->out->push_back( e == l->e ? 1u : ( e ? 2u : 0u ) ); };
        if ( want[ 0 ] == 0x5E6720u )
            lens_water_emitter_start( emitter, h );
        else
            lens_water_emitter_stop( emitter, h );
        bad += got == want ? 0 : 1;
    }
    const std::uint32_t vt = c.next( ), radius = c.next( ), drops = c.next( );
    bad += vt == 0x7555B0u && radius == 0x41C80000u && drops == 0x42800000u ? 0 : 1;
    if ( bad == 0 )
        LOG_SUCCESS( "VuLensWaterEmitterEntity matches retail: %u emitter answers (0x5E6780), slots 6 / 7 with the manager, the ctor's defaults", n );
    else
        LOG_ERROR( "lens_water FAILED %d", bad );
    return bad == 0;
}

// lens.hxx lens_water_drop_fill / lens_water_draw_scales against retail's 0x515060 (case lens_drop_fill): every vertex
// word of 150 synthetic drop arrays (0 .. 256 drops; the 4 words after the last vertex must stay the 0xA5 fill), the
// shader constants, the draw call's arguments, and no device slot outside the read ones.
inline bool verify_lens_drop_fill( )
{
    logic_check_t c{ g_golden_ldf_words };
    const unsigned n = c.next( );
    int bad = 0, verts = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const float aspect = control_f( c.next( ) );
        const std::int32_t width = static_cast< std::int32_t >( c.next( ) ), height = static_cast< std::int32_t >( c.next( ) );
        const std::uint32_t has40 = c.next( );
        const unsigned count = c.next( );
        std::vector< std::uint32_t > drops( 9 * count );
        for ( auto& x : drops )
            x = c.next( );
        const unsigned nv = c.next( );
        std::vector< lens_water_vertex_t > out( 6 * count + 1 );
        std::memset( out.data( ), 0xA5, out.size( ) * sizeof( lens_water_vertex_t ) );
        lens_water_drop_fill( static_cast< int >( count ), reinterpret_cast< const float* >( drops.data( ) ), out.data( ) );
        int diff = nv == 24 * count + 4 ? 0 : 1000;
        const std::uint8_t* got = reinterpret_cast< const std::uint8_t* >( out.data( ) );
        for ( unsigned k = 0; k < nv; ++k )
        {
            const std::uint32_t want = c.next( );
            diff += k < 24 * count + 4 && std::memcmp( got + 4 * k, &want, 4 ) == 0 ? 0 : 1;
        }
        if ( diff )
        {
            if ( bad < 4 )
                LOG_ERROR( "lens_drop_fill vector %u: %d of %u vertex words differ", v, diff, nv );
            ++bad;
        }
        verts += static_cast< int >( 6 * count );
        float s[ 3 ];
        lens_water_draw_scales( aspect, width, height, s );
        const unsigned n10 = c.next( );
        bad += n10 == 1 && c.next( ) == control_w( s[ 0 ] ) ? 0 : 1;
        const unsigned n20 = c.next( );
        if ( has40 )
        {
            bad += n20 == 2 ? 0 : 1;
            const std::uint32_t a = c.next( ), b = c.next( );
            bad += a == control_w( s[ 1 ] ) && b == control_w( s[ 2 ] ) ? 0 : 1;
        }
        else
            bad += n20 == 0 ? 0 : 1;
        const unsigned nd = c.next( );
        std::uint32_t d[ 7 ] = { };
        for ( unsigned k = 0; k < nd; ++k )
        {
            const std::uint32_t x = c.next( );
            if ( k < 7 )
                d[ k ] = x;
        }
        // vt+0xC4( 3, 0, 6 count, 4 count, manager +0x2478 (the case's 0x1B2B3B4B), 0x799898, 0x10 )
        bad += nd == 7 && d[ 0 ] == 3u && d[ 1 ] == 0u && d[ 2 ] == 6u * count && d[ 3 ] == 4u * count && d[ 4 ] == 0x1B2B3B4Bu &&
               d[ 5 ] == 0x799898u && d[ 6 ] == 0x10u ? 0 : 1;
        bad += c.next( ) == 0u ? 0 : 1;
    }
    if ( bad == 0 )
        LOG_SUCCESS( "The lens-water drop draw's vertex fill (0x515060, CPU half) matches retail: %u drop arrays, %d vertices word for word", n, verts );
    else
        LOG_ERROR( "lens_drop_fill FAILED %d", bad );
    return bad == 0;
}

// VuAudioEmitterEntity + 0x50FBB0 + VuAnimatedPropEntity's PlayAudioEvent (world/sound.hxx) against retail (case
// audio_emitter): every step replayed with hooks that log what the case's FMOD recorder logged (getEvent answering FMOD_OK
// and the next handle 0xE001.. unless the name holds "bad": 74 and 0xBAD0 + the low byte), compared entry for entry; the
// VuRetVal / callback answer; the state bytes +0x84 / +0x88 / +0x89 and the event +0x8C after each step; the ctor's
// defaults; the prop vectors' FMOD logs on documents parsed by c_reader (= 0x55B560).
#include <workspace/core/world/sound.hxx>

struct sound_check_log_t
{
    std::uint32_t handle = 0xE000u;
    const void* holder = nullptr;
    std::vector< std::vector< std::uint32_t > > log;

    std::uint32_t holder_code( const void* p ) const { return p == nullptr ? 0u : ( p == holder ? 1u : 2u ); }

    static void str( std::vector< std::uint32_t >& w, const std::string& s )
    {
        w.push_back( static_cast< std::uint32_t >( s.size( ) ) );
        for ( std::size_t i = 0; i < s.size( ); i += 4 )
        {
            std::uint32_t x = 0;
            std::memcpy( &x, s.data( ) + i, ( std::min )( std::size_t( 4 ), s.size( ) - i ) );
            w.push_back( x );
        }
    }

    sound_hooks_t hooks( )
    {
        sound_hooks_t h;
        h.user = this;
        h.get_event = []( void* u, const char* name, std::uint32_t mode, std::uint32_t* ev, const void* holder ) -> int
        {
            auto* l = static_cast< sound_check_log_t* >( u );
            const std::string nm( name );
            const bool ok = nm.find( "bad" ) == std::string::npos;
            ++l->handle;
            *ev = ok ? l->handle : 0xBAD0u + ( l->handle & 0xFFu );
            std::vector< std::uint32_t > w{ 0u, 1u };
            str( w, nm );
            // getEvent's out pointer is the holder's +4, or (the prop) a stack local
            w.push_back( mode );
            w.push_back( holder != nullptr && holder == l->holder ? 1u : 2u );
            w.push_back( *ev );
            l->log.push_back( w );
            return ok ? 0 : 74;
        };
        h.set_callback = []( void* u, std::uint32_t ev, std::uint32_t fn, const void* holder )
        {
            auto* l = static_cast< sound_check_log_t* >( u );
            l->log.push_back( { 1u, ev, fn, l->holder_code( holder ) } );
        };
        h.stop = []( void* u, std::uint32_t ev, bool immediate ) { static_cast< sound_check_log_t* >( u )->log.push_back( { 2u, ev, immediate ? 1u : 0u } ); };
        h.start = []( void* u, std::uint32_t ev ) { static_cast< sound_check_log_t* >( u )->log.push_back( { 3u, ev } ); };
        h.attributes = []( void* u, std::uint32_t ev, const float* p, const float* v, const float* o )
        {
            std::vector< std::uint32_t > w{ 4u, ev };
            for ( const float* x : { p, v, o } )
            {
                w.push_back( x ? 3u : 0u );
                for ( int k = 0; x && k < 3; ++k )
                    w.push_back( control_w( x[ k ] ) );
            }
            static_cast< sound_check_log_t* >( u )->log.push_back( w );
        };
        h.set_world = []( void* u, const float*, bool notify ) { static_cast< sound_check_log_t* >( u )->log.push_back( { 5u, 1u, 1u, notify ? 1u : 0u } ); };
        return h;
    }
};

inline bool verify_audio_emitter( )
{
    logic_check_t c{ g_golden_aem_words };
    sound_check_log_t lg;
    int bad = 0, steps = 0, entries = 0, reported = 0;
    auto read_log = [ & ]( ) {
        std::vector< std::vector< std::uint32_t > > want( c.next( ) );
        for ( auto& e : want )
        {
            e.resize( c.next( ) );
            for ( auto& x : e )
                x = c.next( );
        }
        return want;
    };
    const unsigned n = c.next( );
    for ( unsigned v = 0; v < n; ++v )
    {
        audio_emitter_t e;
        lg.holder = e.holder.image;
        // the ctor's defaults: +0x5C the IF vtable 0x756EA0, the empty VuString (+0x80 cap 15, +0x7C size 0, +0x6C 0),
        // Initially Active 0, the holder word 1, no event
        const std::uint32_t defaults[ 7 ] = { 0x756EA0u, 15u, static_cast< std::uint32_t >( e.event_name.size( ) ), 0u, e.initially_active,
                                              static_cast< std::uint32_t >( e.holder.image[ 0 ] | e.holder.image[ 1 ] << 8 ), e.holder.event( ) };
        e.event_name = c.str( );
        e.initially_active = static_cast< std::uint8_t >( c.next( ) );
        float pos[ 3 ];
        for ( float& x : pos )
            x = control_f( c.next( ) );
        for ( std::uint32_t d : defaults )
            bad += c.next( ) == d ? 0 : 1;
        const unsigned ns = c.next( );
        for ( unsigned s = 0; s < ns; ++s )
        {
            const unsigned op = c.next( );
            std::vector< std::uint32_t > arg( c.next( ) );
            for ( auto& x : arg )
                x = c.next( );
            const std::string text = c.str( );
            const std::uint32_t want_ret = c.next( );
            const auto want = read_log( );
            std::vector< std::uint32_t > want_state( c.next( ) );
            for ( auto& x : want_state )
                x = c.next( );
            lg.log.clear( );
            const sound_hooks_t h = lg.hooks( );
            std::uint32_t ret = 0;
            float m[ 20 ];
            switch ( op )
            {
                case 0: audio_emitter_game_init( e, pos, h ); break;
                case 1: audio_emitter_start( e, pos, h ); break;
                case 2: audio_emitter_stop( e, h ); break;
                case 3: audio_emitter_name_changed( e, pos, h ); break;
                case 4:
                    for ( int k = 0; k < 20; ++k )
                        m[ k ] = control_f( arg[ k ] );
                    audio_emitter_motion( e, m, h );
                    break;
                case 5: case 6: case 7: ret = sound_event_callback( e.holder, op == 5 ? 4u : ( op == 6 ? 2u : 0u ), h ); break;
                case 8: e.event_name = text; break;
                case 9: e.holder.image[ 1 ] = static_cast< std::uint8_t >( arg[ 0 ] ); break;
                case 10:
                    audio_emitter_destroy( e, h );
                    lg.log.push_back( { 6u, 1u } );   // the base dtor 0x500870 follows
                    break;
                default: ++bad; break;
            }
            std::vector< std::uint32_t > state{ e.initially_active, e.holder.image[ 0 ], e.holder.image[ 1 ], e.holder.event( ) };
            if ( op == 10 )
                state.insert( state.end( ), { static_cast< std::uint32_t >( e.event_name.size( ) ), 15u,
                                              e.event_name.empty( ) ? 0u : static_cast< std::uint8_t >( e.event_name[ 0 ] ) } );
            const int diff = ( ret == want_ret ? 0 : 1 ) + ( lg.log == want ? 0 : 1 ) + ( state == want_state ? 0 : 1 );
            if ( diff && reported++ < 6 )
                LOG_ERROR( "audio_emitter scenario %u step %u (op %u): ret %d log %d (%d / %d entries) state %d", v, s, op, ret != want_ret,
                           lg.log != want, static_cast< int >( lg.log.size( ) ), static_cast< int >( want.size( ) ), state != want_state );
            bad += diff;
            entries += static_cast< int >( want.size( ) );
            ++steps;
        }
    }
    const unsigned np = c.next( );
    lg.holder = nullptr;
    for ( unsigned v = 0; v < np; ++v )
    {
        const std::string name = c.str( );
        const std::string raw = c.str( );
        float m[ 16 ], s[ 3 ];
        for ( float& x : m )
            x = control_f( c.next( ) );
        for ( float& x : s )
            x = control_f( c.next( ) );
        const auto want = read_log( );
        const value_t doc = c_reader( std::vector< std::uint8_t >( raw.begin( ), raw.end( ) ) ).parse( );
        lg.log.clear( );
        if ( name == "PlayAudioEvent" )   // 0x5ED680's exact VuString compare picks the branch
            prop_play_audio_event( doc, m, s, lg.hooks( ) );
        if ( lg.log != want )
        {
            if ( reported++ < 6 )
                LOG_ERROR( "audio_emitter prop vector %u (%s): %d / %d entries", v, name.c_str( ), static_cast< int >( lg.log.size( ) ),
                           static_cast< int >( want.size( ) ) );
            ++bad;
        }
        entries += static_cast< int >( want.size( ) );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuAudioEmitterEntity (slot 6, Start, Stop, the name hook, the motion IF, the dtor), FMOD's callback 0x50FBB0 and "
                     "VuAnimatedPropEntity's PlayAudioEvent match retail: %u scenarios / %d steps, %u prop events, %d FMOD / transform calls",
                     n, steps, np, entries );
    else
        LOG_ERROR( "audio_emitter FAILED %d", bad );
    return bad == 0;
}

// VuKeyframeMotionEntity (world/keymotion.hxx) against retail (case key_motion): per scenario the children / flags / the
// target as retail had them, each step replayed with hooks logging what the case logged (plug fires by FNV-1a, the
// target IF's slots, the "Motion" registration {0x757F58, this, 0x61A6E0}, findMotionComponent); compared: the log, the
// VuRetVal word, every state word (flags, +0x68, counts, duration, clock, the keys, the claim, the motion component's
// matrix / linear / angular xyz). Scenarios that reach an unrecreated path fail.
#include <workspace/core/world/keymotion.hxx>

inline std::uint32_t key_motion_fnv( const char* s )
{
    std::uint32_t h = 0x811C9DC5u;
    for ( ; *s; ++s )
        h = ( h ^ static_cast< std::uint8_t >( *s ) ) * 0x01000193u;
    return h;
}

// One replay of the key_motion layout (key_curve: + the Curve Type after the flags and the +0x90 curve after each state).
inline int key_motion_replay( const unsigned* words, bool curves, const char* tag, unsigned& scenarios, int& steps )
{
    logic_check_t c{ words };
    int bad = 0, reported = 0, unrecreated = 0;
    {
        const key_motion_t d;
        const std::uint32_t want[ 11 ] = { d.initially_active, d.one_shot, d.active, d.latch, d.target ? 1u : 0u, static_cast< std::uint32_t >( d.curve ),
                                           static_cast< std::uint32_t >( d.loop | d.ease << 8 | d.affect_position << 16 | d.affect_rotation << 24 ),
                                           static_cast< std::uint32_t >( d.keys.size( ) ), static_cast< std::uint32_t >( d.capacity ), d.duration, d.clock };
        for ( std::uint32_t x : want )
            bad += c.next( ) == x ? 0 : 1;
    }
    struct env_t
    {
        std::vector< std::vector< std::uint32_t > > log;
        key_motion_target_t target;
        float mc[ 22 ] = { };
        bool has = false;
    };
    const unsigned n = c.next( );
    for ( unsigned v = 0; v < n; ++v )
    {
        std::vector< key_motion_child_t > kids( c.next( ) );
        for ( auto& k : kids )
        {
            k.keyframe = c.next( ) != 0;
            k.time = c.next( );
            for ( float& x : k.transform )
                x = control_f( c.next( ) );
        }
        const bool linked = c.next( ) != 0, has = c.next( ) != 0, tnull = c.next( ) != 0;
        float tw[ 16 ];
        for ( float& x : tw )
            x = control_f( c.next( ) );
        std::uint32_t fl[ 6 ];
        for ( auto& x : fl )
            x = c.next( );
        key_motion_t e;
        e.initially_active = static_cast< std::uint8_t >( fl[ 0 ] );
        e.one_shot = static_cast< std::uint8_t >( fl[ 1 ] );
        e.loop = static_cast< std::uint8_t >( fl[ 2 ] );
        e.ease = static_cast< std::uint8_t >( fl[ 3 ] );
        e.affect_position = static_cast< std::uint8_t >( fl[ 4 ] );
        e.affect_rotation = static_cast< std::uint8_t >( fl[ 5 ] );
        if ( curves )
            e.curve = static_cast< std::int32_t >( c.next( ) );
        env_t env;
        env.has = has;
        env.target.transform = tnull ? nullptr : tw;
        env.target.user = &env;
        env.target.begin = []( void* u ) { static_cast< env_t* >( u )->log.push_back( { 2u, 1u, 1u } ); };
        env.target.end = []( void* u ) { static_cast< env_t* >( u )->log.push_back( { 2u, 2u, 1u } ); };
        env.target.update = []( void* u, const float* m, const float* lin, const float* ang )
        {
            auto* x = static_cast< env_t* >( u );
            std::memcpy( x->mc, m, 64 );
            std::memcpy( x->mc + 16, lin, 12 );
            std::memcpy( x->mc + 19, ang, 12 );
            x->log.push_back( { 2u, 0u, 1u } );
        };
        key_motion_hooks_t h;
        h.user = &env;
        h.unrecreated = &unrecreated;
        h.fire = []( void* u, key_motion_t&, const char* plug ) { static_cast< env_t* >( u )->log.push_back( { 1u, key_motion_fnv( plug ) } ); };
        h.tick_register = []( void* u, key_motion_t&, bool on )
        {
            auto* x = static_cast< env_t* >( u );
            if ( on )
                x->log.push_back( { 3u, key_motion_fnv( "Motion" ), 0x757F58u, 1u, 0x61A6E0u } );
            else
                x->log.push_back( { 4u, 1u } );
        };
        h.find = []( void* u, key_motion_t& ) -> key_motion_target_t*
        {
            auto* x = static_cast< env_t* >( u );
            x->log.push_back( { 5u, 1u } );
            return x->has ? &x->target : nullptr;
        };
        static const int foreign = 0;
        const unsigned ns = c.next( );
        for ( unsigned s = 0; s < ns; ++s )
        {
            const unsigned op = c.next( );
            const std::uint32_t arg = c.next( );
            std::vector< std::vector< std::uint32_t > > want( c.next( ) );
            for ( auto& w : want )
            {
                w.resize( c.next( ) );
                for ( auto& x : w )
                    x = c.next( );
            }
            std::vector< std::uint32_t > want_state( c.next( ) );
            for ( auto& x : want_state )
                x = c.next( );
            env.log.clear( );
            const int before = unrecreated;
            std::uint32_t ret = arg;
            switch ( op )
            {
                case 0: key_motion_game_init( e, kids, linked, h ); break;
                case 1: key_motion_activate( e, h ); ret = 0; break;
                case 2: key_motion_deactivate( e, h ); ret = 0; break;
                case 3: key_motion_tick( e, control_f( arg ), h ); break;
                case 4: key_motion_reset( e ); break;
                case 5: env.target.claim = arg ? &foreign : nullptr; break;
                case 6: key_motion_game_release( e, h ); break;
                default: ++bad; break;
            }
            std::vector< std::uint32_t > st{ e.initially_active, e.one_shot, e.active, e.latch, e.target ? 1u : 0u, static_cast< std::uint32_t >( e.curve ),
                                             static_cast< std::uint32_t >( e.loop | e.ease << 8 | e.affect_position << 16 | e.affect_rotation << 24 ),
                                             static_cast< std::uint32_t >( e.keys.size( ) ), static_cast< std::uint32_t >( e.capacity ), e.duration, e.clock };
            // slot 7 zeroes the count only: retail's array words stay, the snapshot reads none (count 0)
            for ( const auto& k : e.keys )
                st.insert( st.end( ), k.begin( ), k.end( ) );
            st.push_back( env.target.claim == nullptr ? 0u : ( env.target.claim == &e ? 1u : 2u ) );
            for ( float x : env.mc )
                st.push_back( control_w( x ) );
            if ( curves )
            {
                st.insert( st.end( ), { static_cast< std::uint32_t >( e.cubic.size( ) ), static_cast< std::uint32_t >( e.cubic_capacity ),
                                        e.cubic_valid } );
                for ( const auto& r : e.cubic )
                    for ( int j : { 0, 1, 2, 3, 4, 5, 6, 8 } )
                        st.push_back( r[ static_cast< std::size_t >( j ) ] );
                st.insert( st.end( ), { static_cast< std::uint32_t >( e.euler.size( ) ), static_cast< std::uint32_t >( e.euler_capacity ),
                                        e.euler_valid } );
                for ( const auto& r : e.euler )
                    for ( int j : { 0, 1, 2, 4, 5, 6, 8 } )
                        st.push_back( r[ static_cast< std::size_t >( j ) ] );
                st.insert( st.end( ), { static_cast< std::uint32_t >( e.spline.size( ) ), static_cast< std::uint32_t >( e.spline_capacity ),
                                        static_cast< std::uint32_t >( e.lengths.size( ) ), static_cast< std::uint32_t >( e.lengths_capacity ),
                                        e.total, e.spline_valid } );
                for ( const auto& r : e.spline )
                    for ( int j = 0; j < 19; ++j )
                        if ( j != 10 && j != 14 && j != 18 )
                            st.push_back( r[ static_cast< std::size_t >( j ) ] );
                st.insert( st.end( ), e.lengths.begin( ), e.lengths.end( ) );
                st.insert( st.end( ), { static_cast< std::uint32_t >( e.squad.size( ) ), static_cast< std::uint32_t >( e.squad_capacity ),
                                        e.squad_valid } );
                for ( const auto& r : e.squad )
                    st.insert( st.end( ), r.begin( ), r.end( ) );
            }
            const int diff = ( ( op == 1 || op == 2 ) && ret != arg ? 1 : 0 ) + ( env.log == want ? 0 : 1 ) + ( st == want_state ? 0 : 1 )
                             + ( unrecreated != before ? 1 : 0 );
            if ( diff && reported++ < 6 )
                LOG_ERROR( "%s scenario %u step %u (op %u): log %d (%d / %d) state %d (%d / %d words) unrecreated %d", tag, v, s, op,
                           env.log != want, static_cast< int >( env.log.size( ) ), static_cast< int >( want.size( ) ), st != want_state,
                           static_cast< int >( st.size( ) ), static_cast< int >( want_state.size( ) ), unrecreated - before );
            bad += diff;
            ++steps;
        }
    }
    scenarios = n;
    if ( bad )
        LOG_ERROR( "%s FAILED %d (unrecreated %d)", tag, bad, unrecreated );
    return bad;
}

inline bool verify_key_motion( )
{
    unsigned n = 0;
    int steps = 0;
    if ( key_motion_replay( g_golden_kfm_words, false, "key_motion", n, steps ) )
        return false;
    LOG_SUCCESS( "VuKeyframeMotionEntity matches retail: %u scenarios, %d steps (slots 6 / 7 / 8, Activate / Deactivate, the Motion tick on "
                 "Curve Type 0: the keys, the clock, the target's matrix and velocities)", n, steps );
    return true;
}

// Curve Types 1 / 2 / 3 (case key_curve; type 3: 0x5CF800's +0xB0 / +0xD0 splines, every word but the position segments' 10 / 14 / 18): slot 6's position curve (0x5CEC60 -> 0x61A7A0 -> 0x61AE50: every record word but +0x1C,
// the count, capacity and valid byte), type 2's euler curve (0x5CEF20: every record word but +0xC / +0x1C) and the update's
// cubic branches of 0x5D0440, through the same replay.
inline bool verify_key_curve( )
{
    unsigned n = 0;
    int steps = 0;
    if ( key_motion_replay( g_golden_kcv_words, true, "key_curve", n, steps ) )
        return false;
    LOG_SUCCESS( "VuKeyframeMotionEntity Curve Types 1 / 2 / 3 (Cubic, Cubic Euler, Spline) match retail: %u scenarios, %d steps (the +0x90 / +0xA0 / +0xB0 / +0xD0 "
                 "splines - 0x5CEC60 / 0x5CEF20 / 0x61A7A0 / 0x61AE50 / 0x5CF800 / 0x61B120 / 0x61B9E0 - and 0x5D0440's branches: the target's matrix and velocities)",
                 n, steps );
    return true;
}

// The settings entities' Trigger handlers (world/settings.hxx) against retail (case settings_triggers): g_vulightmanager's
// +0xCC .. +0xF8 after each call (prefilled 0xA5A5A5A5; +0xEC, a stack word retail never wrote, excepted), the VuRetVal
// word, the substitute calls; the ctor defaults against engine entity/rig.hxx's constructs.
#include <workspace/core/entity/rig.hxx>
#include <workspace/core/world/settings.hxx>

inline bool verify_settings_triggers( )
{
    logic_check_t c{ g_golden_stt_words };
    int bad = 0;
    {
        ambient_light_t a;
        directional_light_t d;
        ambient_light_construct( a );
        directional_light_construct( d );
        const std::uint32_t want[ 4 ] = { a.color, d.front_color, d.back_color, d.specular_color };
        for ( std::uint32_t x : want )
            bad += c.next( ) == x ? 0 : 1;
    }
    const unsigned n = c.next( );
    int calls = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned kind = c.next( );
        std::vector< std::uint32_t > in( c.next( ) );
        for ( auto& x : in )
            x = c.next( );
        std::uint32_t want_lm[ 12 ];
        for ( auto& x : want_lm )
            x = c.next( );
        const std::uint32_t want_ret = c.next( );
        std::vector< std::uint32_t > want_log( 2 * c.next( ) );
        for ( auto& x : want_log )
            x = c.next( );
        light_manager_t m;
        std::memset( &m, 0xA5, sizeof( m ) );
        std::uint32_t ret = 0xFFFFFFFFu;
        std::vector< std::uint32_t > got_log;
        static const int a_tag = 1, b_tag = 2;   // distinct values: identical constants may be folded
        const void* pick[ 3 ] = { nullptr, &a_tag, &b_tag };
        if ( kind == 0 )
            ret = ambient_light_trigger( in[ 0 ], m );
        else if ( kind == 1 )
            ret = directional_light_trigger( in.data( ), in[ 16 ], in[ 17 ], in[ 18 ], m );
        else
            ret = substitute_asset_trigger( pick[ in[ 0 ] ], pick[ in[ 1 ] ], &got_log, []( void* u, const void* x, const void* y )
                                            {
                                                auto code = []( const void* p ) { return p == nullptr ? 0u : ( p == &a_tag ? 1u : 2u ); };
                                                static_cast< std::vector< std::uint32_t >* >( u )->push_back( code( x ) );
                                                static_cast< std::vector< std::uint32_t >* >( u )->push_back( code( y ) );
                                            } );
        std::uint32_t got[ 12 ];
        std::memcpy( got, &m, 48 );
        int diff = ret == want_ret ? 0 : 1;
        for ( int k = 0; k < 12; ++k )
            diff += ( kind == 1 && k == 8 ) || got[ k ] == want_lm[ k ] ? 0 : 1;
        diff += got_log == want_log ? 0 : 1;
        calls += static_cast< int >( want_log.size( ) / 2 );
        if ( diff && bad < 6 )
            LOG_ERROR( "settings_triggers vector %u (kind %u): %d words differ", v, kind, diff );
        bad += diff;
    }
    if ( bad == 0 )
        LOG_SUCCESS( "the settings Trigger handlers match retail (VuAmbientLightEntity 0x5EF780, VuDirectionalLightEntity 0x5EEEC0 / 0x5EEEE0, "
                     "VuSubstituteAssetEntity 0x5F4BD0): %u calls, the light manager's words, %d substitutions, the ctor defaults", n, calls );
    else
        LOG_ERROR( "settings_triggers FAILED %d", bad );
    return bad == 0;
}

// VuForceAffectorEntity (world/force.hxx) against retail (case force_affector): the ctor defaults; per step the log (the
// box scaling, the ghost add / remove, the step callback, the Build tick {0x73A84C, this, 0x439ED0, 0}), the VuRetVal, the
// objects' force words after the step, the active byte and the ghost words (each row's w lane - a stack word retail never
// wrote - excepted).
#include <workspace/core/world/force.hxx>

inline bool verify_force_affector( )
{
    logic_check_t c{ g_golden_fae_words };
    int bad = 0, steps = 0, pushes = 0, reported = 0;
    {
        const force_affector_t d;
        const std::uint32_t want[ 5 ] = { d.initially_active, control_w( d.acceleration ), control_w( d.max_speed ), d.active, 0x73A88Cu };
        for ( std::uint32_t x : want )
            bad += c.next( ) == x ? 0 : 1;
    }
    struct env_t { std::vector< std::vector< std::uint32_t > > log; };
    const unsigned n = c.next( );
    for ( unsigned v = 0; v < n; ++v )
    {
        force_affector_t e;
        e.initially_active = static_cast< std::uint8_t >( c.next( ) );
        e.acceleration = control_f( c.next( ) );
        e.max_speed = control_f( c.next( ) );
        float tm[ 16 ], scale[ 3 ];
        for ( float& x : tm )
            x = control_f( c.next( ) );
        for ( float& x : scale )
            x = control_f( c.next( ) );
        env_t env;
        force_affector_hooks_t h;
        h.user = &env;
        h.set_scaling = []( void* u, const float* s )
        {
            static_cast< env_t* >( u )->log.push_back( { 1u, 1u, control_w( s[ 0 ] ), control_w( s[ 1 ] ), control_w( s[ 2 ] ), control_w( s[ 3 ] ) } );
        };
        h.add_object = []( void* u, std::uint32_t g, std::uint32_t m ) { static_cast< env_t* >( u )->log.push_back( { 2u, 1u, g, m } ); };
        h.remove_object = []( void* u ) { static_cast< env_t* >( u )->log.push_back( { 3u, 1u } ); };
        h.step_callback = []( void* u, bool add ) { static_cast< env_t* >( u )->log.push_back( { 4u, add ? 1u : 0u, 1u } ); };
        h.tick_register = []( void* u, bool on )
        {
            if ( on )
                static_cast< env_t* >( u )->log.push_back( { 5u, key_motion_fnv( "Build" ), 0x73A84Cu, 1u, 0x439ED0u, 0u } );
            else
                static_cast< env_t* >( u )->log.push_back( { 6u, 1u } );
        };
        int unrecreated = 0;
        h.unrecreated = &unrecreated;
        const unsigned ns = c.next( );
        for ( unsigned s = 0; s < ns; ++s )
        {
            const unsigned op = c.next( );
            std::vector< std::uint32_t > arg( c.next( ) );
            for ( auto& x : arg )
                x = c.next( );
            std::vector< std::uint32_t > want_ret( c.next( ) );
            for ( auto& x : want_ret )
                x = c.next( );
            std::vector< std::vector< std::uint32_t > > want_log( c.next( ) );
            for ( auto& w : want_log )
            {
                w.resize( c.next( ) );
                for ( auto& x : w )
                    x = c.next( );
            }
            std::uint32_t want_state[ 17 ];
            for ( auto& x : want_state )
                x = c.next( );
            env.log.clear( );
            std::vector< std::uint32_t > got_ret{ 0u };
            switch ( op )
            {
                case 0: force_affector_game_init( e, tm, scale, h ); break;
                case 1: force_affector_activate( e, tm, scale, h ); break;
                case 2: force_affector_deactivate( e, h ); break;
                case 3:
                {
                    for ( int k = 0; k < 16; ++k )
                        e.ghost[ k ] = control_f( arg[ static_cast< std::size_t >( k ) ] );
                    float sc[ 3 ] = { control_f( arg[ 16 ] ), control_f( arg[ 17 ] ), control_f( arg[ 18 ] ) };
                    const int nb = static_cast< int >( arg[ 20 ] );
                    std::vector< force_body_t > bodies( static_cast< std::size_t >( nb ) );
                    for ( int k = 0; k < nb; ++k )
                    {
                        const std::uint32_t* a = arg.data( ) + 21 + 14 * k;
                        force_body_t& b = bodies[ static_cast< std::size_t >( k ) ];
                        b.type = a[ 0 ];
                        for ( int j = 0; j < 3; ++j )
                        {
                            b.position[ j ] = control_f( a[ 1 + j ] );
                            b.velocity[ j ] = control_f( a[ 4 + j ] );
                            b.factor[ j ] = control_f( a[ 8 + j ] );
                            b.force[ j ] = control_f( a[ 11 + j ] );
                        }
                        b.inverse_mass = control_f( a[ 7 ] );
                    }
                    force_affector_step( e, sc, bodies.data( ), nb );
                    got_ret.clear( );
                    for ( int k = 0; k < nb; ++k )
                    {
                        const std::uint32_t* a = arg.data( ) + 21 + 14 * k;
                        for ( int j = 0; j < 3; ++j )
                        {
                            const std::uint32_t f = control_w( bodies[ static_cast< std::size_t >( k ) ].force[ j ] );
                            got_ret.push_back( f );
                            pushes += f != a[ 11 + j ] ? 1 : 0;
                        }
                    }
                    break;
                }
                case 4: force_affector_build( e, false, h ); break;
                case 5: force_affector_game_release( e, h ); break;
                default: ++bad; break;
            }
            int diff = ( got_ret == want_ret ? 0 : 1 ) + ( env.log == want_log ? 0 : 1 ) + ( e.active == want_state[ 0 ] ? 0 : 1 );
            for ( int k = 0; k < 16; ++k )
                diff += control_w( e.ghost[ k ] ) == want_state[ 1 + k ] ? 0 : 1;
            if ( diff && reported++ < 6 )
                LOG_ERROR( "force_affector scenario %u step %u (op %u): %d words differ", v, s, op, diff );
            bad += diff + unrecreated;
            unrecreated = 0;
            ++steps;
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuForceAffectorEntity matches retail: %u scenarios, %d steps (slots 6 / 7, Activate / Deactivate, the step 0x439970: "
                     "%d force components changed, the Build tick)", n, steps, pushes );
    else
        LOG_ERROR( "force_affector FAILED %d", bad );
    return bad == 0;
}

// keymotion.hxx vu_matrix_rotate_axis_angle_pre / _post against retail's 0x55A350 / 0x5D0BE0 (case rotate_axis_angle):
// both results' 16 words per vector.
inline bool verify_rotate_axis_angle( )
{
    const unsigned* w = g_golden_raa_words;
    const unsigned n = *w++;
    int bad = 0;
    for ( unsigned v = 0; v < n; ++v, w += 52 )
    {
        float m[ 16 ], axis[ 3 ], angle;
        std::memcpy( axis, w + 16, 12 );
        std::memcpy( &angle, w + 19, 4 );
        std::memcpy( m, w, 64 );
        vu_matrix_rotate_axis_angle_pre( m, axis, angle );
        const bool pre = std::memcmp( m, w + 20, 64 ) == 0;
        std::memcpy( m, w, 64 );
        vu_matrix_rotate_axis_angle_post( m, axis, angle );
        const bool post = std::memcmp( m, w + 36, 64 ) == 0;
        if ( ( !pre || !post ) && bad++ < 4 )
            LOG_ERROR( "rotate_axis_angle vector %u: pre %s, post %s", v, pre ? "ok" : "differs", post ? "ok" : "differs" );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuMatrix_rotateAxisAngle_pre / _post (0x55A350 / 0x5D0BE0) match retail: %u matrices, both results word for word", n );
    else
        LOG_ERROR( "rotate_axis_angle FAILED %d", bad );
    return bad == 0;
}
// entity/json.hxx against retail's VuJsonContainer (case json_access): on retail's own containers (its VUJB read
// 0x55B560 of hand-built documents and two pack DBs; ours = assets/reader.hxx), every node in retail's pre-order -
// asFloat 0x4FC470, asCString 0x4FC500, asInt64 0x4FC520, getValue( int64& ) 0x4FC5E0, getValue( int& ) 0x4FC550
// (values.hxx property_int_value), vec4 0x4FDD30, matrix 0x4FDE40, getMemberKeys 0x4FBF30 - and append 0x4FBD30 on
// every node of the hand-built documents (the element index, the node's dump afterwards).
#include <workspace/core/entity/json.hxx>
#include <workspace/utility/verify.hxx>

inline void json_check_nodes( value_t& v, std::vector< value_t* >& out )
{
    out.push_back( &v );
    if ( v.tag == 5 )
        for ( value_t& e : v.list )
            json_check_nodes( e, out );
    else if ( v.tag == 6 )
    {
        std::vector< std::pair< std::string, value_t >* > sorted;
        for ( auto& f : v.object )
            sorted.push_back( &f );
        std::sort( sorted.begin( ), sorted.end( ), []( const auto* a, const auto* b ) { return a->first < b->first; } );
        for ( auto* f : sorted )
            json_check_nodes( f->second, out );
    }
}

inline std::vector< std::uint32_t > json_check_record( const value_t& j )
{
    auto w = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    std::vector< std::uint32_t > r{ j.tag, w( json_as_float( j ) ) };
    const char* s = json_as_cstring( j );
    const std::string text = j.tag == 4 ? std::string( s ) : std::string( );
    r.push_back( j.tag == 4 ? 0u : 1u );
    r.push_back( static_cast< std::uint32_t >( text.size( ) ) );
    for ( std::size_t i = 0; i < text.size( ); i += 4 )
    {
        std::uint32_t x = 0;
        std::memcpy( &x, text.data( ) + i, std::min< std::size_t >( 4, text.size( ) - i ) );
        r.push_back( x );
    }
    const std::uint64_t a = static_cast< std::uint64_t >( json_as_int64( j ) );
    r.push_back( static_cast< std::uint32_t >( a ) ), r.push_back( static_cast< std::uint32_t >( a >> 32 ) );
    std::int64_t g64 = 0x7777777777777777ll;
    const bool ok64 = json_get_int64( j, g64 );
    r.push_back( ok64 ? 1u : 0u ), r.push_back( static_cast< std::uint32_t >( g64 ) ), r.push_back( static_cast< std::uint32_t >( static_cast< std::uint64_t >( g64 ) >> 32 ) );
    std::int32_t g32 = 0x77777777;
    const bool ok32 = property_int_value( j, g32 );
    r.push_back( ok32 ? 1u : 0u ), r.push_back( static_cast< std::uint32_t >( g32 ) );
    float lanes[ 16 ];
    std::memset( lanes, 0x77, sizeof( lanes ) );
    r.push_back( json_vec4( j, lanes ) ? 1u : 0u );
    for ( int k = 0; k < 4; ++k )
        r.push_back( w( lanes[ k ] ) );
    std::memset( lanes, 0x77, sizeof( lanes ) );
    r.push_back( json_matrix( j, lanes ) ? 1u : 0u );
    for ( int k = 0; k < 16; ++k )
        r.push_back( w( lanes[ k ] ) );
    r.push_back( 1u );   // getMemberKeys answers its out pointer
    return r;
}

inline bool verify_json_access( )
{
    logic_check_t c{ g_golden_jsa_words };
    const unsigned n = c.next( );
    int bad = 0, nodes = 0, appends = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const std::string bytes = c.str( );
        const std::vector< std::uint8_t > blob( bytes.begin( ), bytes.end( ) );
        value_t root;
        if ( !c_reader::read_at_start( blob, root ) )
            ++bad;
        std::vector< value_t* > ns;
        json_check_nodes( root, ns );
        const unsigned nn = c.next( );
        if ( nn != ns.size( ) )
        {
            LOG_ERROR( "json_access document %u: %u nodes, retail %u", v, static_cast< unsigned >( ns.size( ) ), nn );
            return false;
        }
        for ( unsigned k = 0; k < nn; ++k, ++nodes )
        {
            std::vector< std::uint32_t > want( c.next( ) );
            for ( auto& x : want )
                x = c.next( );
            const std::vector< std::uint32_t > got = json_check_record( *ns[ k ] );
            std::vector< std::string > wk( c.next( ) );
            for ( auto& s : wk )
                s = c.str( );
            const bool same = got == want && json_member_keys( *ns[ k ] ) == wk;
            if ( !same && bad++ < 6 )
                LOG_ERROR( "json_access document %u node %u (tag %u) differs", v, k, ns[ k ]->tag );
        }
        const unsigned na = c.next( );
        for ( unsigned k = 0; k < na; ++k, ++appends )
        {
            const std::int32_t index = static_cast< std::int32_t >( c.next( ) );
            const std::string dump = c.str( );
            value_t fresh;
            c_reader::read_at_start( blob, fresh );
            std::vector< value_t* > fs;
            json_check_nodes( fresh, fs );
            value_t& node = *fs[ k ];
            const bool was_array = node.tag == 5;
            value_t& e = json_append( node );
            const std::int32_t got = was_array || node.tag == 5 ? static_cast< std::int32_t >( &e - node.list.data( ) ) : -1;
            std::string text;
            vujb_dump( node, text );
            if ( ( got != index || text != dump ) && bad++ < 6 )
                LOG_ERROR( "json_access document %u append on node %u: index %d (retail %d), dump %s", v, k, got, index, text == dump ? "ok" : "differs" );
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuJsonContainer's accessors match retail (0x4FC470 0x4FC500 0x4FC520 0x4FC5E0 0x4FC550 0x4FDD30 0x4FDE40 0x4FBF30 0x4FBD30): %u documents, %d nodes, %d appends", n, nodes, appends );
    else
        LOG_ERROR( "json_access FAILED %d", bad );
    return bad == 0;
}
// entity/repository.hxx against retail's VuEntityRepositoryImpl (case entity_repository): the same op sequence on 48
// entities (each prev starting at a dummy) and 8 projects; after every op the answer, every link, the bucket heads,
// the count and the project set.
#include <workspace/core/entity/repository.hxx>

inline bool verify_entity_repository( )
{
    logic_check_t c{ g_golden_erp_words };
    const unsigned n = c.next( );
    std::vector< entity_link_t > links( n + 1 );
    entity_link_t& dummy = links[ n ];
    for ( unsigned i = 0; i < n; ++i )
        links[ i ].hash = c.next( ), links[ i ].prev = &dummy;
    std::vector< std::string > projects( c.next( ) );
    for ( auto& p : projects )
        p = c.str( );
    auto index = [ & ]( const entity_link_t* e ) -> std::int32_t
    {
        return e == nullptr ? -1 : e == &dummy ? -2 : static_cast< std::int32_t >( e - links.data( ) );
    };
    entity_repository_t r;
    const unsigned no = c.next( );
    int bad = 0;
    for ( unsigned k = 0; k < no; ++k )
    {
        const unsigned kind = c.next( ), e = c.next( ), p = c.next( );
        std::int32_t answer = 0;
        if ( kind == 0 )
            entity_repository_add( r, links[ e ] );
        else if ( kind == 1 )
            entity_repository_remove( r, links[ e ] );
        else if ( kind == 2 )
            answer = index( entity_repository_find( r, c.next( ) ) );
        else if ( kind == 3 )
            answer = index( entity_repository_find( r, c.str( ).c_str( ) ) );
        else if ( kind == 4 )
            entity_repository_add_project( r, projects[ p ] );
        else if ( kind == 5 )
            entity_repository_remove_project( r, projects[ p ] );
        else
            answer = entity_repository_has_project( r, projects[ p ] ) ? 1 : 0;
        bool same = static_cast< std::int32_t >( c.next( ) ) == answer;
        for ( unsigned i = 0; i < n; ++i )
        {
            same = static_cast< std::int32_t >( c.next( ) ) == index( links[ i ].prev ) && same;
            same = static_cast< std::int32_t >( c.next( ) ) == index( links[ i ].next ) && same;
        }
        same = static_cast< std::int32_t >( c.next( ) ) == index( dummy.next ) && same;
        std::vector< std::pair< std::uint32_t, std::int32_t > > heads;
        for ( std::uint32_t b = 0; b < 256; ++b )
            if ( r.buckets[ b ] != nullptr )
                heads.push_back( { b, index( r.buckets[ b ] ) } );
        const unsigned nh = c.next( );
        same = nh == heads.size( ) && same;
        for ( unsigned h = 0; h < nh; ++h )
        {
            const std::uint32_t b = c.next( );
            const std::int32_t at = static_cast< std::int32_t >( c.next( ) );
            same = h < heads.size( ) && heads[ h ].first == b && heads[ h ].second == at && same;
        }
        same = c.next( ) == r.count && same;
        same = c.next( ) == r.projects.size( ) && same;
        const unsigned np = c.next( );
        auto it = r.projects.begin( );
        for ( unsigned q = 0; q < np; ++q )
        {
            const std::string name = c.str( );
            same = it != r.projects.end( ) && *it == name && same;
            if ( it != r.projects.end( ) )
                ++it;
        }
        if ( !same && bad++ < 6 )
            LOG_ERROR( "entity_repository op %u (kind %u, entity %u) differs", k, kind, e );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuEntityRepositoryImpl matches retail (0x5527D0 0x552810 0x5528A0 0x552860 0x552740 0x552790 0x5528D0): %u ops on %u entities", no, n );
    else
        LOG_ERROR( "entity_repository FAILED %d", bad );
    return bad == 0;
}

// entity/repository.hxx entity_repository_tree against retail's slot 13 0x552960 (case repository_tree): Hydroelectric's
// 1379 template entities registered in the case's orders into a fresh repository per vector; out's dump after.
inline bool verify_repository_tree( )
{
    logic_check_t c{ g_golden_rtr_words };
    const unsigned n = c.next( );
    std::vector< std::string > names( n ), paths( n );
    std::vector< entity_link_t > links( n );
    for ( unsigned i = 0; i < n; ++i )
    {
        names[ i ] = c.str( );
        const std::int32_t parent = static_cast< std::int32_t >( c.next( ) );
        paths[ i ] = parent < 0 ? names[ i ] : paths[ parent ] + "/" + names[ i ];   // 0x500BF0 (parents come first)
        links[ i ].hash = c.next( );
    }
    auto of = [ & ]( const entity_link_t& e, std::string& name, std::string& path )
    {
        const std::size_t i = static_cast< std::size_t >( &e - links.data( ) );
        name = names[ i ], path = paths[ i ];
    };
    const unsigned nv = c.next( );
    value_t out;
    int bad = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned preset = c.next( );
        if ( preset == 0 )
            out = value_t{ };
        else if ( preset == 1 )
            json_set_int( out, 7 );
        for ( auto& l : links )
            l.prev = nullptr, l.next = nullptr;
        entity_repository_t r;
        const unsigned no = c.next( );
        for ( unsigned k = 0; k < no; ++k )
            entity_repository_add( r, links[ c.next( ) ] );
        entity_repository_tree( r, out, of );
        const std::string want = c.str( );
        std::string got;
        vujb_dump( out, got );
        if ( got != want && bad++ < 6 )
            LOG_ERROR( "repository_tree vector %u (%u entities): dump differs (%u bytes, retail %u)", v, no,
                       static_cast< unsigned >( got.size( ) ), static_cast< unsigned >( want.size( ) ) );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuEntityRepositoryImpl slot 13 matches retail (0x552960 0x564940 0x5649C0 0x4FCAB0): %u vectors over %u entities", nv, n );
    else
        LOG_ERROR( "repository_tree FAILED %d", bad );
    return bad == 0;
}

// entity/repository.hxx vu_property_dtor / vu_entity_dtor against retail's 0x405260 / 0x500870 (case entity_dtors): fake
// objects in one region, the region's words after and every call out in order.
struct edt_check_memory_t
{
    std::uint32_t base = 0;
    std::vector< std::uint32_t > words;
    bool outside = false;

    std::uint32_t& operator[]( std::uint32_t a )
    {
        static std::uint32_t sink = 0;
        if ( a < base || ( a - base ) / 4u >= words.size( ) )
            return outside = true, sink = 0, sink;
        return words[ ( a - base ) / 4u ];
    }
};

inline bool verify_entity_dtors( )
{
    logic_check_t c{ g_golden_edt_words };
    const unsigned n = c.next( );
    const std::uint32_t base = c.next( );
    int bad = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned kind = c.next( ), self = c.next( ), count = c.next( );
        edt_check_memory_t m;
        m.base = base;
        m.words.resize( count );
        for ( auto& x : m.words )
            x = c.next( );
        std::vector< std::uint32_t > after( count );
        for ( auto& x : after )
            x = c.next( );
        std::vector< std::vector< std::uint32_t > > want( c.next( ) );
        for ( auto& e : want )
        {
            e.resize( c.next( ) );
            for ( auto& x : e )
                x = c.next( );
        }
        std::vector< std::vector< std::uint32_t > > got;
        vu_dtor_hooks_t h;
        h.free = [ & ]( std::uint32_t b ) { got.push_back( { 0u, b } ); };
        h.call = [ & ]( std::uint32_t o, std::uint32_t slot, std::int64_t arg )
        {
            got.push_back( { 1u, o, slot, static_cast< std::uint32_t >( arg ) } );
            if ( arg < 0 )   // a weak reference's slot 0: unlinked from its target's +8 list (the case's stub does the same)
                m[ m[ o + 8 ] + 8 ] = m[ o + 4 ];
        };
        h.asset_release = [ & ]( std::uint32_t a ) { got.push_back( { 2u, a } ); };
        h.event_remove = [ & ]( std::uint32_t k, std::uint32_t val ) { got.push_back( { 3u, k, val } ); };
        if ( kind == 0 )
            vu_property_dtor( m, self, h );
        else
            vu_entity_dtor( m, self, h );
        const bool same = m.words == after && got == want && !m.outside;
        if ( !same && bad++ < 6 )
            LOG_ERROR( "entity_dtors vector %u (%s): memory %s, calls %s (%u, retail %u)", v, kind == 0 ? "0x405260" : "0x500870",
                       m.words == after ? "ok" : "differs", got == want ? "ok" : "differ", static_cast< unsigned >( got.size( ) ),
                       static_cast< unsigned >( want.size( ) ) );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuProperty / VuEntity base dtors match retail (0x405260 0x500870 0x500E00 0x500DB0 0x515620 0x43CD80 0x43CD30 0x43CA10): %u vectors", n );
    else
        LOG_ERROR( "entity_dtors FAILED %d", bad );
    return bad == 0;
}

// entity/repository.hxx vu_framework_delete against retail's component / plug / ref / project deleting dtors (case
// framework_dtors): fake objects in one region; the region's words after and every call out in order.
inline bool verify_framework_dtors( )
{
    logic_check_t c{ g_golden_fdt_words };
    const unsigned n = c.next( );
    const std::uint32_t base = c.next( );
    const unsigned count = c.next( );
    int bad = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned kind = c.next( ), self = c.next( ), flags = c.next( );
        edt_check_memory_t m;
        m.base = base;
        m.words.assign( count, 0u );
        for ( unsigned k = c.next( ); k > 0; --k )
        {
            const unsigned i = c.next( );
            m.words[ i ] = c.next( );
        }
        std::vector< std::uint32_t > after = m.words;
        for ( unsigned k = c.next( ); k > 0; --k )
        {
            const unsigned i = c.next( );
            after[ i ] = c.next( );
        }
        std::vector< std::vector< std::uint32_t > > want( c.next( ) );
        for ( auto& e : want )
        {
            e.resize( c.next( ) );
            for ( auto& x : e )
                x = c.next( );
        }
        std::vector< std::vector< std::uint32_t > > got;
        vu_dtor_hooks_t h;
        h.free = [ & ]( std::uint32_t b ) { got.push_back( { 0u, b } ); };
        h.call = [ & ]( std::uint32_t o, std::uint32_t slot, std::int64_t arg )
        {
            got.push_back( { 1u, o, slot, static_cast< std::uint32_t >( arg ) } );
            if ( slot == 0 && arg < 0 )   // a weak reference's slot 0: unlinked from its target's +8 list (the case's stub does the same)
                m[ m[ o + 8 ] + 8 ] = m[ o + 4 ];
        };
        h.asset_release = [ & ]( std::uint32_t a ) { got.push_back( { 2u, a } ); };
        h.event_remove = [ & ]( std::uint32_t k, std::uint32_t val ) { got.push_back( { 3u, k, val } ); };
        h.instigator_remove = [ & ]( std::uint32_t comp ) { got.push_back( { 4u, 0x30000000u + 0x20C0u, comp } ); };   // the case's g 0x759064
        h.json_clear = [ & ]( std::uint32_t j ) { got.push_back( { 5u, j } ); };
        h.aligned_free = [ & ]( std::uint32_t b ) { got.push_back( { 6u, b } ); };
        bool ok = true;
        if ( kind == 11 )   // VuScriptComponent slot 8 0x5042A0 (not a dtor; the case's appended vectors)
            vu_script_component_snapshot( m, self );
        else
            ok = vu_framework_delete( m, static_cast< vu_framework_dtor_t >( kind ), self, flags, h );
        const bool same = ok && m.words == after && got == want && !m.outside;
        if ( !same && bad++ < 6 )
            LOG_ERROR( "framework_dtors vector %u (kind %u): %s, memory %s, calls %s (%u, retail %u)", v, kind, ok ? "ended" : "UNKNOWN",
                       m.words == after ? "ok" : "differs", got == want ? "ok" : "differ", static_cast< unsigned >( got.size( ) ),
                       static_cast< unsigned >( want.size( ) ) );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "the framework dtors match retail (0x423770 0x4268F0/0x539610 0x50B960/0x50B990 0x531D90/0x531DC0 0x534280/0x5342B0 "
                     "0x503B70/0x503BA0 0x4052E0/0x502180/0x502340 0x542680/0x5426B0/0x504980 0x4B4330/0x4B4290 0x5EFA30 0x4032A0 0x5CE980 0x5EB000 0x5F4AB0/0x5F4AE0 0x5D6F50/0x5DB430/0x59A300; VuScriptComponent "
                     "slot 8 0x5042A0): %u vectors", n );
    else
        LOG_ERROR( "framework_dtors FAILED %d", bad );
    return bad == 0;
}

// entity/broadcast.hxx against retail's VuEventManager (case event_manager): the same op sequence on the model; per op
// the calls out in order (fires with their params, locks, the platform calls), the queue (record words; the real-time
// word's low byte only - its other three bytes are the resize's fill, never written), the subscriber sets.
#include <workspace/core/entity/broadcast.hxx>

inline bool verify_event_manager( )
{
    logic_check_t c{ g_golden_evm_words };
    const std::uint32_t self = c.next( );
    const unsigned n = c.next( );
    event_manager_t m;
    std::uint32_t mutex = 0;
    std::vector< std::vector< std::uint32_t > > got;
    float real = 0.0f;
    event_manager_hooks_t h;
    h.fire = [ & ]( std::uint32_t handler, const std::uint32_t* p )
    {
        std::vector< std::uint32_t > e{ 1u, handler };
        e.insert( e.end( ), p, p + 33 );
        got.push_back( e );
    };
    h.real_delta = [ & ]( ) { return real; };
    h.lock = [ & ]( bool on ) { got.push_back( { on ? 6u : 7u, mutex } ); };
    int bad = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned kind = c.next( );
        std::vector< std::uint32_t > args( c.next( ) );
        for ( auto& a : args )
            a = c.next( );
        const std::string name = c.str( );
        std::vector< std::uint32_t > params( c.next( ) );
        for ( auto& p : params )
            p = c.next( );
        params.resize( 33 );
        const std::uint32_t poke = c.next( ), poke_delay = c.next( ), poke_handler = c.next( ), real_bits = c.next( ), g = c.next( );
        std::memcpy( &real, &real_bits, 4 );
        got.clear( );
        switch ( kind )
        {
        case 0:
            event_manager_init( self, mutex,
                                [ & ]( std::uint32_t vt, std::uint32_t s, std::uint32_t fn, const char* phase )
                                {
                                    std::vector< std::uint32_t > e{ 2u, vt, s, fn, 0u };
                                    for ( const char* q = phase; *q; ++q )
                                        e.push_back( static_cast< unsigned char >( *q ) );
                                    got.push_back( e );
                                },
                                [ & ]( ) { got.push_back( { 4u } ); return 0x4D555458u; } );
            break;
        case 1: event_manager_subscribe( m, event_hash( name.c_str( ) ), args[ 0 ] ); break;
        case 2: event_manager_broadcast( m, args[ 0 ], params.data( ), h ); break;
        case 3: event_manager_broadcast( m, name.c_str( ), params.data( ), h ); break;
        case 4:
        {
            float f;
            std::memcpy( &f, &args[ 0 ], 4 );
            event_manager_queue( m, f, ( args[ 1 ] & 0xFFu ) != 0, name.c_str( ), params.data( ), h );   // retail's bool: the word's low byte
            if ( poke & 1u )
                std::memcpy( &m.queue.back( ).delay, &poke_delay, 4 );
            if ( poke & 2u )
                m.queue.back( ).handler = poke_handler;
            break;
        }
        case 5:
        {
            float dt;
            std::memcpy( &dt, &args[ 0 ], 4 );
            event_manager_tick( m, dt, h );
            break;
        }
        case 6: event_manager_remove( m, args[ 0 ], args[ 1 ], h ); break;
        default:
            event_manager_release( self, mutex, [ & ]( std::uint32_t s ) { got.push_back( { 3u, s } ); },
                                   [ & ]( std::uint32_t x ) { got.push_back( { 5u, x } ); } );
            break;
        }
        std::vector< std::vector< std::uint32_t > > want( c.next( ) );
        for ( auto& e : want )
        {
            e.resize( c.next( ) );
            for ( auto& x : e )
                x = c.next( );
        }
        bool same = got == want && ( kind != 7 || g == 0 );
        const unsigned nq = c.next( );
        same = nq == m.queue.size( ) && same;
        for ( unsigned q = 0; q < nq; ++q )
        {
            std::uint32_t w[ 37 ];
            for ( auto& x : w )
                x = c.next( );
            if ( q >= m.queue.size( ) )
                continue;
            const event_record_t& r = m.queue[ q ];
            std::uint32_t delay;
            std::memcpy( &delay, &r.delay, 4 );
            same = same && w[ 0 ] == r.handler && w[ 1 ] == delay && ( w[ 2 ] & 0xFFu ) == r.real_time && w[ 3 ] == r.hash
                && std::memcmp( w + 4, r.params, sizeof( r.params ) ) == 0;
        }
        const unsigned ns = c.next( );
        same = ns == m.subscribers.size( ) && same;
        auto it = m.subscribers.begin( );
        for ( unsigned s = 0; s < ns; ++s )
        {
            std::vector< std::uint32_t > e( c.next( ) );
            for ( auto& x : e )
                x = c.next( );
            if ( it == m.subscribers.end( ) )
            {
                same = false;
                continue;
            }
            std::vector< std::uint32_t > mine{ it->first };
            mine.insert( mine.end( ), it->second.begin( ), it->second.end( ) );
            same = same && mine == e;
            ++it;
        }
        if ( !same && bad++ < 6 )
            LOG_ERROR( "event_manager op %u (kind %u) differs: calls %s (%u, retail %u), queue %u (retail %u)", v, kind, got == want ? "ok" : "differ",
                       static_cast< unsigned >( got.size( ) ), static_cast< unsigned >( want.size( ) ), static_cast< unsigned >( m.queue.size( ) ), nq );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuEventManager matches retail (0x505690 0x505650 0x505720 0x5057E0 0x505950 0x5055C0 0x505610 0x505430): %u ops", n );
    else
        LOG_ERROR( "event_manager FAILED %d", bad );
    return bad == 0;
}

// world/logic.hxx tick_is_paused / tick_delta / tick_real_delta against VuTickManagerImpl slots 14 / 15 / 16 (case
// tick_slots: retail's ctor, the four fields poked per vector).
inline bool verify_tick_slots( )
{
    logic_check_t c{ g_golden_tks_words };
    const unsigned n = c.next( );
    int bad = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        tick_manager_t tm;
        tm.paused = c.next( ) != 0;
        tm.pause_count = static_cast< std::int32_t >( c.next( ) );
        const std::uint32_t dt = c.next( ), raw = c.next( );
        std::memcpy( &tm.dt, &dt, 4 );
        std::memcpy( &tm.raw, &raw, 4 );
        const unsigned paused = c.next( ), delta = c.next( ), real = c.next( );
        const float d = tick_delta( tm ), r = tick_real_delta( tm );
        std::uint32_t db, rb;
        std::memcpy( &db, &d, 4 );
        std::memcpy( &rb, &r, 4 );
        if ( ( ( tick_is_paused( tm ) ? 1u : 0u ) != paused || db != delta || rb != real ) && bad++ < 6 )
            LOG_ERROR( "tick_slots vector %u differs", v );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuTickManagerImpl isPaused / getDeltaTime / getRealDeltaTime match retail (0x4F6EA0 0x4F6EC0 0x4F6ED0): %u vectors", n );
    else
        LOG_ERROR( "tick_slots FAILED %d", bad );
    return bad == 0;
}

// entity/json.hxx json_member against retail's member-or-insert 0x4FC090 (case json_member) on c_reader-built roots
// whose objects keep the file's (unsorted) key order: per op the returned member, the int written into it, the root.
inline bool verify_json_member( )
{
    logic_check_t c{ g_golden_jsm_words };
    const unsigned n = c.next( );
    int bad = 0, ops = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const std::string bytes = c.str( );
        const std::vector< std::uint8_t > blob( bytes.begin( ), bytes.end( ) );
        value_t root;
        c_reader::read_at_start( blob, root );
        const unsigned no = c.next( );
        for ( unsigned k = 0; k < no; ++k, ++ops )
        {
            const std::string key = c.str( ), want_member = c.str( );
            const bool wrote = c.next( ) != 0;
            const std::string want_root = c.str( );
            value_t& m = json_member( root, key );
            std::string member, whole;
            vujb_dump( m, member );
            if ( wrote )
                json_set_int( m, static_cast< std::int32_t >( 100 + k ) );
            vujb_dump( root, whole );
            if ( ( member != want_member || whole != want_root ) && bad++ < 6 )
                LOG_ERROR( "json_member document %u op %u (key '%s'): member %s, root %s", v, k, key.c_str( ),
                           member == want_member ? "ok" : "differs", whole == want_root ? "ok" : "differs" );
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "json_member matches retail's member-or-insert 0x4FC090 (0x4FC6B0 0x4FC970) on unsorted objects: %u documents, %d ops", n, ops );
    else
        LOG_ERROR( "json_member FAILED %d", bad );
    return bad == 0;
}

// entity/json.hxx's text reader against retail's VuJsonReader (case json_text): loadFromString 0x4FEEC0 on 173 texts
// (every value / comment / escape / number branch and error) and loadFromFile 0x4FEDF0 on a fake file system - per
// vector the answer, the container's dump, the error text (every line), the cursor's offset and the file-system calls.
// Vectors the case marks unknown (a float its correctly rounded stand-in for MSVCR110's sscanf_s may not match) are
// skipped and counted.
struct json_text_check_fs_t
{
    std::string path;
    std::string bytes;
    bool exists = false;
    const char* buffer = nullptr;
    std::vector< std::pair< std::uint32_t, std::uint32_t > > calls;
};

inline bool verify_json_text( )
{
    logic_check_t c{ g_golden_jst_words };
    const unsigned n = c.next( );
    int bad = 0, unknown = 0, texts = 0, files = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned kind = c.next( ), unk = c.next( ), want_ret = c.next( ), want_cursor = c.next( );
        const std::string text = c.str( ), want_dump = c.str( ), want_error = c.str( );
        json_text_reader_t r;
        r.error = "junk";
        value_t out;
        json_set_int( out, 5 );
        int got = 0;
        std::uint32_t cursor = ~0u;
        json_text_check_fs_t fs;
        std::vector< std::pair< std::uint32_t, std::uint32_t > > want_calls;
        if ( kind == 0 )
        {
            ++texts;
            const std::string buffer = text;   // NUL-terminated by std::string
            got = json_read_text( r, out, buffer.c_str( ) );
            cursor = static_cast< std::uint32_t >( r.cursor - buffer.c_str( ) );
        }
        else
        {
            ++files;
            fs.path = c.str( );
            fs.exists = c.next( ) != 0;
            fs.bytes = text;
            const unsigned calls = c.next( );
            for ( unsigned k = 0; k < calls; ++k )
            {
                const std::uint32_t op = c.next( );
                want_calls.push_back( { op, c.next( ) } );
            }
            json_file_hooks_t hooks;
            hooks.user = &fs;
            hooks.open = []( void* u, const std::string& path ) -> void*
            {
                auto& f = *static_cast< json_text_check_fs_t* >( u );
                f.calls.push_back( { 0x34u, 0u } );
                return path == f.path && f.exists ? &f : nullptr;
            };
            hooks.size = []( void* u, void* ) -> std::uint32_t
            {
                auto& f = *static_cast< json_text_check_fs_t* >( u );
                f.calls.push_back( { 0x2Cu, 0u } );
                return static_cast< std::uint32_t >( f.bytes.size( ) );
            };
            hooks.read = []( void* u, void*, char* buffer, std::uint32_t size )
            {
                auto& f = *static_cast< json_text_check_fs_t* >( u );
                f.calls.push_back( { 0x40u, size } );
                std::memcpy( buffer, f.bytes.data( ), size );
                f.buffer = buffer;
            };
            hooks.close = []( void* u, void* )
            {
                static_cast< json_text_check_fs_t* >( u )->calls.push_back( { 0x3Cu, 0u } );
            };
            got = json_read_file( r, out, fs.path, hooks );
            if ( fs.buffer != nullptr )
                cursor = static_cast< std::uint32_t >( reinterpret_cast< std::uintptr_t >( r.cursor ) - reinterpret_cast< std::uintptr_t >( fs.buffer ) );
        }
        if ( unk != 0 )
        {
            ++unknown;
            continue;
        }
        std::string dump;
        vujb_dump( out, dump );
        const bool same = got == static_cast< int >( want_ret ) && cursor == want_cursor && dump == want_dump &&
                          r.error == want_error && fs.calls == want_calls;
        if ( !same && bad++ < 8 )
            LOG_ERROR( "json_text vector %u (%s %.40s): answer %d (retail %u), cursor %u (%u), dump %s, error %s, calls %s", v,
                       kind == 0 ? "text" : "file", text.c_str( ), got, want_ret, cursor, want_cursor, dump == want_dump ? "ok" : "differs",
                       r.error == want_error ? "ok" : "differs", fs.calls == want_calls ? "ok" : "differ" );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuJsonReader matches retail (0x4FEEC0 0x4FEDF0 0x4FEF40 0x4FEFD0 0x4FF090 0x4FF1E0 0x4FF3E0 0x4FF510 0x4FF5B0 0x4FF790 0x4FF820 0x4FF870 0x4FFA70 0x4FFB60 0x559EB0 0x559B50): %d texts, %d files, %d unknown skipped", texts, files, unknown );
    else
        LOG_ERROR( "json_text FAILED %d", bad );
    return bad == 0;
}
// world/voice.hxx against retail's VuVoiceOverManagerImpl (case voice_over): two sessions (Base.apf's VoiceOverDB and a
// synthetic one) - init 0x4B7970's entries in full (keys, EventName, MaxPlayCount, StartDelay, the shuffled Waveforms
// from the logged rand values), then 260 ops each (say now 0x4B7EA0, queue 0x4B7F50, tick 0x4B8000, the FMOD callback
// 0x4B8180, reset 0x4B7E40, the gates): every FMOD / subtitle / ducking hook call in order with its arguments, and the
// manager's state after every op.
#include <workspace/core/world/voice.hxx>

struct vo_check_log_t
{
    std::vector< std::vector< std::uint32_t > > entries;   // kind, words...
    std::size_t at = 0;
    int bad = 0;

    // The next logged entry of a hook kind (8 .. 11 are the caller's: skipped).
    const std::vector< std::uint32_t >* take( std::uint32_t kind )
    {
        while ( at < entries.size( ) && entries[ at ][ 0 ] >= 8 && entries[ at ][ 0 ] <= 11 )
            ++at;
        if ( at >= entries.size( ) || entries[ at ][ 0 ] != kind )
        {
            ++bad;
            return nullptr;
        }
        return &entries[ at++ ];
    }
};

inline std::string vo_check_unpack( const std::uint32_t* w, std::size_t& i )
{
    const std::uint32_t n = w[ i++ ];
    std::string s( reinterpret_cast< const char* >( w + i ), n );
    i += ( n + 3 ) / 4;
    return s;
}

inline void vo_check_read_log( logic_check_t& c, vo_check_log_t& log )
{
    log.entries.clear( );
    log.at = 0;
    const unsigned n = c.next( );
    for ( unsigned k = 0; k < n; ++k )
    {
        std::vector< std::uint32_t > e{ c.next( ) };
        const unsigned len = c.next( );
        for ( unsigned j = 0; j < len; ++j )
            e.push_back( c.next( ) );
        log.entries.push_back( e );
    }
}

inline vo_hooks_t vo_check_hooks( vo_check_log_t& log, const std::int32_t& viewports, const bool& boat_ok )
{
    vo_hooks_t h;
    h.rand = [ &log ]( ) -> std::int32_t { const auto* e = log.take( 7 ); return e ? static_cast< std::int32_t >( ( *e )[ 1 ] ) : 0; };
    h.get_event = [ &log ]( const std::string& name, std::uint32_t& event ) -> std::int32_t
    {
        const auto* e = log.take( 1 );
        if ( !e )
            return -1;
        std::size_t i = 1;
        const std::string want = vo_check_unpack( e->data( ), i );
        if ( want != std::string( name.c_str( ) ) || ( *e )[ i ] != 1u )
            ++log.bad;
        event = ( *e )[ i + 2 ];
        return static_cast< std::int32_t >( ( *e )[ i + 1 ] );
    };
    h.set_callback = [ &log ]( std::uint32_t event, std::int32_t wave )
    {
        const auto* e = log.take( 2 );
        if ( e && ( ( *e )[ 1 ] != event || ( *e )[ 2 ] != 1u || ( *e )[ 3 ] != static_cast< std::uint32_t >( wave ) ) )
            ++log.bad;
    };
    h.start = [ &log ]( std::uint32_t event )
    {
        const auto* e = log.take( 3 );
        if ( e && ( *e )[ 1 ] != event )
            ++log.bad;
    };
    h.subtitle = [ &log ]( const char* text )
    {
        const auto* e = log.take( 4 );
        std::size_t i = 1;
        if ( e && vo_check_unpack( e->data( ), i ) != text )
            ++log.bad;
    };
    h.duck = [ &log ]( const std::string& category, float volume, float time ) -> std::uint32_t
    {
        const auto* e = log.take( 5 );
        if ( !e )
            return 0;
        std::size_t i = 1;
        const std::string want = vo_check_unpack( e->data( ), i );
        if ( want != std::string( category.c_str( ) ) || ( *e )[ i ] != control_w( volume ) || ( *e )[ i + 1 ] != control_w( time ) )
            ++log.bad;
        return ( *e )[ i + 2 ];
    };
    h.unduck = [ &log ]( std::uint32_t handle )
    {
        const auto* e = log.take( 6 );
        if ( e && ( *e )[ 1 ] != handle )
            ++log.bad;
    };
    h.viewports = [ &viewports ]( ) { return viewports; };
    h.boat_ok = [ &boat_ok ]( const void* ) { return boat_ok; };
    return h;
}

// The key of the entry a pointer names (0 for none).
inline std::uint32_t vo_check_key( const vo_manager_t& m, const vo_entry_t* e )
{
    if ( e == nullptr )
        return 0;
    for ( const auto& kv : m.entries )
        if ( &kv.second == e )
            return kv.first;
    return 0xFFFFFFFFu;
}

inline bool vo_check_state( logic_check_t& c, const vo_manager_t& m, bool full )
{
    bool ok = true;
    const std::uint32_t words[ 6 ] = { m.enabled ? 1u : 0u, m.busy ? 1u : 0u, vo_check_key( m, m.current ), control_w( m.timer ), m.duck_handle,
                                       static_cast< std::uint32_t >( m.entries.size( ) ) };
    for ( std::uint32_t w : words )
        ok = c.next( ) == w && ok;
    const unsigned nq = c.next( );
    ok = nq == m.queue.size( ) && ok;
    for ( unsigned k = 0; k < nq; ++k )
    {
        const std::uint32_t want = c.next( );
        ok = k < m.queue.size( ) && vo_check_key( m, m.queue[ k ] ) == want && ok;
    }
    ok = c.str( ) == m.duck_category && ok;
    ok = c.next( ) == control_w( m.duck_volume ) && ok;
    const unsigned ne = c.next( );
    ok = ne == m.entries.size( ) && ok;
    auto it = m.entries.begin( );
    for ( unsigned k = 0; k < ne; ++k )
    {
        std::uint32_t w[ 5 ];
        for ( auto& x : w )
            x = c.next( );
        std::string event;
        std::vector< std::int32_t > waves;
        if ( full )
        {
            event = c.str( );
            waves.resize( c.next( ) );
            for ( auto& x : waves )
                x = static_cast< std::int32_t >( c.next( ) );
        }
        if ( it == m.entries.end( ) )
        {
            ok = false;
            continue;
        }
        const vo_entry_t& e = it->second;
        ok = w[ 0 ] == it->first && w[ 1 ] == static_cast< std::uint32_t >( e.max_plays ) && w[ 2 ] == control_w( e.start_delay ) &&
             w[ 3 ] == static_cast< std::uint32_t >( e.plays ) && w[ 4 ] == e.next && ok;
        if ( full )
            ok = event == e.event && waves == e.waves && ok;
        ++it;
    }
    return ok;
}

inline bool verify_voice_over( )
{
    logic_check_t c{ g_golden_vo_words };
    const unsigned sessions = c.next( );
    int bad = 0, ops = 0, calls = 0;
    for ( unsigned s = 0; s < sessions; ++s )
    {
        const std::string bytes = c.str( );
        value_t root;
        c_reader::read_at_start( std::vector< std::uint8_t >( bytes.begin( ), bytes.end( ) ), root );
        const std::uint32_t init_ok = c.next( );
        vo_check_log_t log;
        vo_check_read_log( c, log );
        std::int32_t viewports = 1;
        bool boat_ok = true, has_boat = false;
        const vo_hooks_t h = vo_check_hooks( log, viewports, boat_ok );
        vo_manager_t m;
        const bool ok = vo_load( m, member( root, "VuDBAsset" ), h );
        // the caller's hooks in retail's order: tick registration, dev bool, asset create ... release
        const bool order = log.entries.size( ) >= 4 && log.entries[ 0 ][ 0 ] == 9u && log.entries[ 1 ][ 0 ] == 10u && log.entries[ 2 ][ 0 ] == 8u &&
                           log.entries.back( )[ 0 ] == 11u;
        log.take( 0xFFFFu );   // only the caller's entries may be left
        const bool rest = log.at == log.entries.size( );
        log.bad -= 1;          // that probe always counts one
        if ( init_ok != ( ok ? 1u : 0u ) || !order || !rest || log.bad || !vo_check_state( c, m, true ) )
        {
            if ( bad < 6 )
                LOG_ERROR( "voice_over session %u: the init differs (log %d, order %d, rest %d)", s, log.bad, order ? 1 : 0, rest ? 1 : 0 );
            ++bad;
        }
        calls += static_cast< int >( log.entries.size( ) );
        const unsigned nops = c.next( );
        for ( unsigned k = 0; k < nops; ++k, ++ops )
        {
            const std::uint32_t kind = c.next( );
            std::uint32_t head[ 6 ] = { };   // gates 6; callback type, userdata, answer, *p1, *p2
            std::string name;
            if ( kind == 1 )
                for ( auto& x : head )
                    x = c.next( );
            else if ( kind == 7 || kind == 8 )
                head[ 0 ] = c.next( ), name = c.str( );
            else if ( kind == 0 )
                head[ 0 ] = c.next( );
            else if ( kind == 9 )
                for ( int j = 0; j < 5; ++j )
                    head[ j ] = c.next( );
            vo_check_read_log( c, log );
            calls += static_cast< int >( log.entries.size( ) );
            bool good = true;
            if ( kind == 1 )
            {
                m.enabled = head[ 0 ] != 0;
                viewports = static_cast< std::int32_t >( head[ 1 ] );
                has_boat = head[ 2 ] != 0;
                boat_ok = head[ 3 ] == 0 && head[ 4 ] != 0 && head[ 5 ] != 0;
            }
            else if ( kind == 7 || kind == 8 )
            {
                static const int boat_tag = 0;
                const void* boat = head[ 0 ] ? &boat_tag : nullptr;
                ( kind == 7 ? vo_say_now : vo_queue )( m, boat, name.c_str( ), h );
            }
            else if ( kind == 0 )
                vo_tick( m, control_f( head[ 0 ] ), h );
            else if ( kind == 9 )
            {
                const std::int32_t p1 = 0x77777777;   // param1: retail never writes it
                std::int32_t p2 = 0x66666666;
                const std::int32_t r = vo_event_callback( m, static_cast< std::int32_t >( head[ 0 ] ), &p2, static_cast< std::int32_t >( head[ 1 ] ), h );
                good = static_cast< std::uint32_t >( r ) == head[ 2 ] && static_cast< std::uint32_t >( p1 ) == head[ 3 ] && static_cast< std::uint32_t >( p2 ) == head[ 4 ];
            }
            else if ( kind == 6 )
                vo_reset( m );
            ( void )has_boat;
            good = log.at == log.entries.size( ) && log.bad == 0 && good;
            good = vo_check_state( c, m, false ) && good;
            if ( !good )
            {
                if ( bad < 6 )
                    LOG_ERROR( "voice_over session %u op %u (kind %u) differs (hook log %d, consumed %u of %u)", s, k, kind, log.bad,
                               static_cast< unsigned >( log.at ), static_cast< unsigned >( log.entries.size( ) ) );
                ++bad;
                log.bad = 0;
            }
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuVoiceOverManagerImpl matches retail (init 0x4B7970 + shuffle 0x4B8F90, say 0x4B7EA0, queue 0x4B7F50, tick 0x4B8000, callback 0x4B8180, reset 0x4B7E40): %u sessions, %d ops, %d hook calls in order",
                     sessions, ops, calls );
    else
        LOG_ERROR( "voice_over FAILED %d", bad );
    return bad == 0;
}
// lens.hxx lens_water_tick / _clear / _add_drops / _set_push against retail's 0x514500 / 0x5142C0 / 0x513600 / 0x513620 /
// 0x5135D0 (case lens_water_tick): 400 ops on one manager - every manager word, every live drop and the global VuRand
// after each.
inline void lwt_rand_load( pfx_rand_t& r, const std::uint32_t* w )
{
    r.seed = static_cast< std::int32_t >( w[ 0 ] );
    r.previous = static_cast< std::int32_t >( w[ 1 ] );
    for ( int k = 0; k < 32; ++k )
        r.table[ k ] = static_cast< std::int32_t >( w[ 2 + k ] );
}

inline bool lwt_state_same( const lens_water_manager_t& m, const pfx_rand_t& r, logic_check_t& c )
{
    bool ok = true;
    const std::uint32_t words[ 9 ] = { m.enabled ? 1u : 0u, control_w( m.half_width ), m.clear ? 1u : 0u, control_w( m.scroll ),
                                       control_w( m.push ), control_w( m.idle ), m.active ? 1u : 0u, static_cast< std::uint32_t >( m.count ),
                                       control_w( m.owed ) };
    for ( std::uint32_t w : words )
        ok = c.next( ) == w && ok;
    const unsigned nd = c.next( );
    ok = nd == 9u * static_cast< unsigned >( m.count ) && ok;
    for ( unsigned k = 0; k < nd; ++k )
    {
        const std::uint32_t want = c.next( );
        ok = k < 9u * 256u && control_w( m.drops[ k ] ) == want && ok;
    }
    std::uint32_t rw[ 34 ];
    for ( auto& x : rw )
        x = c.next( );
    ok = rw[ 0 ] == static_cast< std::uint32_t >( r.seed ) && rw[ 1 ] == static_cast< std::uint32_t >( r.previous ) && ok;
    for ( int k = 0; k < 32; ++k )
        ok = rw[ 2 + k ] == static_cast< std::uint32_t >( r.table[ k ] ) && ok;
    return ok;
}

inline bool verify_lens_water_tick( )
{
    logic_check_t c{ g_golden_lwt_words };
    const unsigned n = c.next( );
    lens_water_manager_t m;
    std::uint32_t start[ 9 ];
    for ( auto& x : start )
        x = c.next( );
    m.enabled = start[ 0 ] != 0;
    m.half_width = control_f( start[ 1 ] );
    std::uint32_t rw[ 34 ];
    for ( auto& x : rw )
        x = c.next( );
    pfx_rand_t rand;
    lwt_rand_load( rand, rw );
    int bad = 0, peak = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        std::vector< std::uint32_t > op( c.next( ) );
        for ( auto& x : op )
            x = c.next( );
        switch ( op[ 0 ] )
        {
        case 0:
        {
            const int ne = static_cast< int >( op[ 2 ] );
            const std::function< float( int ) > rate = [ &op ]( int i ) { return control_f( op[ 3 + i ] ); };
            lens_water_tick( m, control_f( op[ 1 ] ), ne, rate, rand );
            break;
        }
        case 10: lens_water_add_drops( m, control_f( op[ 1 ] ) ); break;
        case 11: lens_water_set_push( m, control_f( op[ 1 ] ) ); break;
        case 6: m.enabled = ( op[ 1 ] & 0xFFu ) != 0; break;   // slot 6 0x5135D0: the byte
        case 9: lens_water_clear( m ); break;
        case 1: m.half_width = control_f( op[ 1 ] ); break;
        }
        if ( m.count > peak )
            peak = m.count;
        if ( !lwt_state_same( m, rand, c ) )
        {
            if ( bad < 4 )
                LOG_ERROR( "lens_water_tick op %u (kind %u) differs", v, op[ 0 ] );
            ++bad;
            // resynchronise nothing: every later op compares against our own state
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuLensWaterManagerImpl's drop tick matches retail (0x514500, slots 6 / 9 / 10 / 11): %u ops, up to %d drops, every drop word and the VuRand", n, peak );
    else
        LOG_ERROR( "lens_water_tick FAILED %d", bad );
    return bad == 0;
}
// lens.hxx lens_water_add_emitter / lens_water_remove_emitter against retail's slots 14 0x513640 (VuArray resize
// 0x415690) and 15 0x513670 (remove-swap 0x5154A0) (case lens_emitters): 600 ops - count, capacity and the block's
// words up to the count (and the swapped-out word just past it after a remove) after each.
inline bool verify_lens_emitters( )
{
    logic_check_t c{ g_golden_lem_words };
    const unsigned n = c.next( );
    lens_water_manager_t m;
    int bad = 0, peak = 0, swaps = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned kind = c.next( ), value = c.next( );
        if ( kind == 1u )
            lens_water_add_emitter( m, value );
        else
            lens_water_remove_emitter( m, value );
        const unsigned count = c.next( ), capacity = c.next( ), nw = c.next( );
        bool ok = count == static_cast< unsigned >( m.emitter_count ) && capacity == static_cast< unsigned >( m.emitter_capacity );
        if ( nw > count )
            ++swaps;
        for ( unsigned k = 0; k < nw; ++k )
        {
            const unsigned want = c.next( );
            ok = k < m.emitters.size( ) && m.emitters[ k ] == want && ok;
        }
        if ( m.emitter_count > peak )
            peak = m.emitter_count;
        if ( !ok )
        {
            if ( bad < 4 )
                LOG_ERROR( "lens_emitters op %u (kind %u) differs", v, kind );
            ++bad;
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuLensWaterManagerImpl's emitter list matches retail (slots 14 0x513640 / 15 0x513670, 0x415690, 0x5154A0): %u ops, up to %d emitters, %d swapped-out words", n, peak, swaps );
    else
        LOG_ERROR( "lens_emitters FAILED %d", bad );
    return bad == 0;
}
// entity/sort.hxx vc_sort_range against retail's std::sort instantiation _Sort 0x501810 (case entity_sort): 260 arrays
// (n up to 600, ties, shaped inputs, small ideals for the heap path) - the final order index for index.
inline bool verify_entity_sort( )
{
    logic_check_t c{ g_golden_esr_words };
    const unsigned n = c.next( );
    int bad = 0, unstable = 0;
    unsigned elements = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const unsigned count = c.next( );
        const std::int32_t ideal = static_cast< std::int32_t >( c.next( ) );
        std::vector< std::int32_t > keys( count );
        for ( auto& k : keys )
            k = static_cast< std::int32_t >( c.next( ) );
        std::vector< std::uint32_t > order( count );
        for ( unsigned j = 0; j < count; ++j )
            order[ j ] = j;
        auto pred = [ &keys ]( std::uint32_t a, std::uint32_t b ) { return keys[ a ] < keys[ b ]; };
        vc_sort_range( order.data( ), order.data( ) + count, ideal, pred );
        bool ok = true, stable = true;
        for ( unsigned j = 0; j < count; ++j )
        {
            const unsigned want = c.next( );
            ok = order[ j ] == want && ok;
            if ( j > 0 && keys[ want ] == keys[ order[ j - 1 ] ] && want < order[ j - 1 ] )
                stable = false;
        }
        unstable += !stable;
        elements += count;
        if ( !ok )
        {
            if ( bad < 4 )
                LOG_ERROR( "entity_sort vector %u (n %u, ideal %d) differs", v, count, ideal );
            ++bad;
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "vc_sort matches retail's std::sort _Sort 0x501810 (0x501900 0x501AF0 0x501CC0 0x501DA0 0x501E00 0x501EB0): %u arrays, %u elements, %d with ties out of input order", n, elements, unstable );
    else
        LOG_ERROR( "entity_sort FAILED %d", bad );
    return bad == 0;
}
// entity/json.hxx json_write_value against retail's VuJsonWriter writeValue 0x4FFCC0 (case json_write): 40 retail-read
// documents x the four flag pairs - the whole text and the indent left behind (MSVCR110's sprintf_s modelled on both
// sides: json_crt_format / the case's stand-in).
inline bool verify_json_write( )
{
    logic_check_t c{ g_golden_jsw_words };
    const unsigned n = c.next( );
    int bad = 0;
    std::size_t chars = 0;
    for ( unsigned v = 0; v < n; ++v )
    {
        const std::string bytes = c.str( );
        const std::vector< std::uint8_t > blob( bytes.begin( ), bytes.end( ) );
        value_t root;
        const bool read = c_reader::read_at_start( blob, root );
        json_writer_t w;
        w.compact = c.next( ) != 0;
        w.trailing = c.next( ) != 0;
        std::string text;
        w.out = &text;
        json_write_value( w, root );
        const std::string want = c.str( ), indent = c.str( );
        chars += want.size( );
        if ( !read || w.unknown || text != want || w.indent != indent )
        {
            if ( bad < 4 )
                LOG_ERROR( "json_write vector %u (compact %d, trailing %d) differs: %u chars vs retail %u", v, w.compact ? 1 : 0, w.trailing ? 1 : 0,
                           static_cast< unsigned >( text.size( ) ), static_cast< unsigned >( want.size( ) ) );
            ++bad;
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuJsonWriter matches retail (writeValue 0x4FFCC0, 0x4FFDA0 0x4FFE10 0x4FFED0 0x4FFF20 0x5001C0 0x500360 0x500150): %u documents x flags, %u characters", n, static_cast< unsigned >( chars ) );
    else
        LOG_ERROR( "json_write FAILED %d", bad );
    return bad == 0;
}
// The game tier's script entities (game/scripted.hxx) against retail (case game_plugs, cases/gameplug.py): per vector the state as retail had it, the plug dispatched
// through g_logic_plugs, every output fire, every service call, the VuRetVal, the documents after.
struct game_plug_log_t
{
    std::vector< std::string > names;
    std::vector< ui_params_t > params;
    std::vector< std::array< unsigned, 2 > > args;
};

inline bool verify_game_plugs( )
{
    save_words_t c{ g_golden_game_plugs_words };
    const unsigned n = c.next( );
    int bad = 0, fires = 0;
    const auto read_doc = [ ]( const std::string& raw, value_t& out ) {
        out = value_t{ };
        if ( !raw.empty( ) )
            c_reader::read_at_start( std::vector< std::uint8_t >( raw.begin( ), raw.end( ) ), out );
    };
    for ( unsigned v = 0; v < n; ++v )
    {
        int bad0 = bad;
        const std::string cls = c.str( ), plug = c.str( );
        const std::uint32_t vt = logic_vtable_of( cls.c_str( ) );
        logic_entity_t e = logic_construct( vt );
        const unsigned kind = c.next( );
        if ( kind & 1u )
            e.words[ 0 ] = c.next( );
        if ( kind & 2u )
        {
            const unsigned a = c.next( ), b = c.next( );
            e.words[ 0 ] = a | b << 8;
            e.words[ 1 ] = c.next( );
            e.words[ 2 ] = c.next( );
        }
        if ( kind & 4u )
            e.text = c.str( );
        const unsigned champ = kind & 8u ? c.next( ) : 0u;
        if ( kind & 16u )
        {
            for ( int k = 0; k < 4; ++k )
                e.head[ k ] = static_cast< std::uint8_t >( c.next( ) );
            e.words[ 0 ] = c.next( );
        }
        if ( kind & 32u )
            e.head[ 0 ] = static_cast< std::uint8_t >( c.next( ) );
        value_t doc, doc2;
        read_doc( c.str( ), doc );
        read_doc( c.str( ), doc2 );
        logic_game_check_t L;
        const unsigned ne = c.next( );
        for ( unsigned k = 0; k < ne; ++k )
        {
            const std::uint32_t hash = c.next( ), entity = c.next( ), vtable = c.next( ), lap = c.next( );
            L.hashes.push_back( hash );
            L.entities.push_back( entity );
            L.words.push_back( { entity, vtable } );
            L.words.push_back( { entity + 0x4ACu, lap } );
        }
        ui_params_t in{ };
        in.m_size = static_cast< int >( c.next( ) );
        std::memcpy( in.m_bytes, c.s, 128 );
        c.s += 32;
        const unsigned rt = c.next( ), rv = c.next( );
        const std::string rtext = c.str( );
        game_plug_log_t log, fire;
        logic_game_t g;
        g.user = &L;
        g.find = []( void* u, std::uint32_t hash ) -> std::uint32_t {
            logic_game_check_t& l = *static_cast< logic_game_check_t* >( u );
            for ( std::size_t k = 0; k < l.hashes.size( ); ++k )
                if ( l.hashes[ k ] == hash )
                    return l.entities[ k ];
            return l.entities.empty( ) ? 0u : l.entities[ 0 ];
        };
        g.word = []( void* u, std::uint32_t address ) -> std::uint32_t {
            for ( const auto& w : static_cast< logic_game_check_t* >( u )->words )
                if ( w.first == address )
                    return w.second;
            return 0u;
        };
        const auto note = [ & ]( const char* name, unsigned a = 0u, unsigned b = 0u, const ui_params_t& p = ui_params_t{ } ) {
            log.names.push_back( name );
            log.args.push_back( { a, b } );
            log.params.push_back( p );
        };
        logic_services_t s;
        s.broadcast = [ & ]( const char* event, const ui_params_t& p ) { note( "broadcast", event_hash( event ), static_cast< unsigned >( p.m_size ), p ); };
        s.champ_data = [ & ] { note( "champ?" ); return static_cast< std::uint8_t >( champ ) != 0u; };
        s.champ_clear = [ & ] { note( "clear", 0x7593A0u ); };
        s.defaults = [ & ]( const char* which ) { note( which ); };
        s.input_defaults = [ & ]( int method ) { note( "input", 0u, static_cast< unsigned >( method ) ); };
        s.save_profile = [ & ] { note( "save" ); };
        s.game_data = &doc;
        s.cloud_data = &doc;
        s.profile_saved = e.head[ 0 ] ? &doc : &doc2;
        s.profile_temp = e.head[ 0 ] ? &doc2 : &doc;
        struct user_t
        {
            game_plug_log_t* fire;
        } user{ &fire };
        logic_hooks_t h;
        h.user = &user;
        h.game = &g;
        h.services = &s;
        h.fire = []( void* u, logic_entity_t&, const char* name, const ui_params_t& p ) -> logic_retval_t {
            game_plug_log_t& f = *static_cast< user_t* >( u )->fire;
            f.names.push_back( name );
            f.params.push_back( p );
            return logic_retval_t{ };
        };
        logic_retval_t out{ 0xA5A5A5A5u, 0xA5A5A5A5u, nullptr };
        if ( plug == "*busy" )
            logic_award_busy( e, in, h );
        else
            bad += !logic_dispatch( e, plug.c_str( ), out, in, h );
        bad += out.type != rt;
        if ( rt == 4u )
            bad += rtext != out.text;
        else if ( rt == 3u )
            bad += ( out.value & 0xFFu ) != rv;
        else if ( rt != 0u && rt != 0xA5A5A5A5u )
            bad += out.value != rv;
        const unsigned nf = c.next( );
        bad += nf != fire.names.size( );
        for ( unsigned k = 0; k < nf; ++k )
        {
            const std::string name = c.str( ), bytes = c.str( );
            ++fires;
            if ( k < fire.names.size( ) )
                bad += name != fire.names[ k ] || bytes.size( ) != static_cast< std::size_t >( fire.params[ k ].m_size ) || std::memcmp( bytes.data( ), fire.params[ k ].m_bytes, bytes.size( ) ) != 0;
        }
        const unsigned nl = c.next( );
        bad += nl != log.names.size( );
        for ( unsigned k = 0; k < nl; ++k )
        {
            const std::string name = c.str( );
            const unsigned a = c.next( ), b = c.next( );
            const std::string bytes = c.str( );
            ++fires;
            if ( k < log.names.size( ) )
                bad += name != log.names[ k ] || a != log.args[ k ][ 0 ] || b != log.args[ k ][ 1 ] || bytes.size( ) != static_cast< std::size_t >( log.params[ k ].m_size ) ||
                       std::memcmp( bytes.data( ), log.params[ k ].m_bytes, bytes.size( ) ) != 0;
        }
        const unsigned tail = c.next( );
        if ( tail & 1u )
            bad += c.next( ) != e.words[ 0 ];
        if ( tail & 2u )
        {
            std::string d;
            vujb_dump( doc, d );
            bad += c.str( ) != d;
        }
        if ( tail & 4u )
        {
            std::string d;
            vujb_dump( doc2, d );
            bad += c.str( ) != d;
        }
        if ( bad != bad0 && bad0 < 8 )
            LOG_ERROR( "game_plugs vector %u (%s %s) differs from retail", v, cls.c_str( ), plug.c_str( ) );
    }
    if ( bad == 0 )
        LOG_SUCCESS( "game/scripted.hxx's plugs match retail: %u vectors, %d fires and service calls", n, fires );
    else
        LOG_ERROR( "game_plugs FAILED %d", bad );
    return bad == 0;
}

// VuDockedBoatEntity and VuPosterEntity against retail (case front_props, cases/frontprops.py): each script step's service log, the boat's Locked byte, the colour, gameutil +0x50's dump.
inline bool verify_front_props( )
{
    save_words_t c{ g_golden_front_props_words };
    const unsigned runs = c.next( );
    int bad = 0, steps = 0;
    for ( unsigned v = 0; v < runs; ++v )
    {
        const auto read = [ ]( const std::string& raw, value_t& out ) {
            out = value_t{ };
            c_reader::read_at_start( std::vector< std::uint8_t >( raw.begin( ), raw.end( ) ), out );
        };
        value_t game, db;
        read( c.str( ), game );
        read( c.str( ), db );
        std::vector< std::string > log;
        std::map< std::uint32_t, std::uint8_t > locks;
        std::map< std::uint32_t, unsigned > crazy;
        const unsigned count = c.next( );
        std::vector< std::string > made;   // the golden's boat answers are in step order
        value_t yes, no;
        no.tag = 6u;
        no.kind = value_t::kind_t::object;
        yes = no;
        value_t& flag = json_member( yes, "IsCrazy" );
        flag.tag = 3u;
        flag.kind = value_t::kind_t::integer;
        flag.integer = 1;
        std::vector< unsigned > answers;
        const unsigned na = c.next( );
        for ( unsigned k = 0; k < na; ++k )
            answers.push_back( c.next( ) );
        std::size_t next = 0u;
        std::uint32_t handle = 0u;
        docked_hooks_t h;
        h.game_data = &game;
        h.boat_db = &db;
        h.make = [ & ]( const value_t& d ) {
            std::string t;
            vujb_dump( d, t );
            log.push_back( "make " + t );
            crazy[ ++handle ] = next < answers.size( ) ? answers[ next++ ] : 0u;
            return handle;
        };
        h.drop = [ & ]( std::uint32_t ) { log.push_back( "drop" ); };
        h.db = [ & ]( std::uint32_t b ) -> const value_t& { return crazy[ b ] ? yes : no; };
        h.set_skin = [ & ]( std::uint32_t, const value_t& s ) {
            std::string t;
            vujb_dump( s, t );
            log.push_back( "skin " + t + " 1" );
        };
        h.set_locked = [ & ]( std::uint32_t b, std::uint8_t l ) { locks[ b ] = l; };
        h.voice = [ & ]( const char* e ) { log.push_back( std::string( "voice " ) + e ); };
        h.tick_on = [ & ] { log.push_back( "tick" ); };
        h.tick_off = [ & ] { log.push_back( "untick" ); };
        bool has = true;
        poster_hooks_t ph;
        ph.game_data = &game;
        ph.has_asset = [ & ]( const std::string& ) { return has; };
        ph.reload = [ & ] { log.push_back( "reload" ); };
        docked_boat_t e;
        for ( unsigned k = 0; k < count; ++k )
        {
            const unsigned op = c.next( );
            has = c.next( ) != 0u;
            ui_params_t in{ };
            in.m_size = static_cast< int >( c.next( ) );
            std::memcpy( in.m_bytes, c.s, 128 );
            c.s += 32;
            const unsigned want_color = c.next( ), want_locked = c.next( );
            log.clear( );
            unsigned color = 0u;
            switch ( op )
            {
            case 0: docked_initialize( e, h ); break;
            case 1: docked_change( e, in, h ); break;
            case 2: docked_reset( e, h ); break;
            case 3: poster_change( in, ph ); break;
            case 4: poster_reset( ph ); break;
            case 5: color = poster_color( game ); log.push_back( "draw" ); break;
            default: docked_release( e, h ); break;
            }
            const unsigned nl = c.next( );
            bool same = nl == log.size( );
            for ( unsigned j = 0; j < nl; ++j )
            {
                const std::string t = c.str( );
                same = j < log.size( ) && log[ j ] == t && same;
            }
            const std::string dump = c.str( );
            std::string got;
            vujb_dump( game, got );
            same = same && got == dump && ( op != 5u || color == want_color ) && ( op == 0u || op == 6u || locks[ e.boat ] == want_locked );
            ++steps;
            if ( !same && bad++ < 8 )
                LOG_ERROR( "front_props run %u step %u (op %u) differs from retail", v, k, op );
        }
    }
    if ( bad == 0 )
        LOG_SUCCESS( "VuDockedBoatEntity and VuPosterEntity match retail: %u runs, %d steps", runs, steps );
    else
        LOG_ERROR( "front_props FAILED %d", bad );
    return bad == 0;
}

// VuOneShotPfxEntity against retail (case oneshot_pfx): scripts on a hand manager; the handle, the instance calls, the serials and the reflect bytes after each step.
inline bool verify_oneshot_pfx( )
{
    save_words_t c{ g_golden_oneshot_pfx_words };
    const unsigned runs = c.next( );
    int bad = 0, steps = 0;
    struct run_t
    {
        pfx_manager_t* m = nullptr;
        std::vector< std::vector< unsigned > > log;
        int answer = -1;
    };
    static run_t* cur = nullptr;
    for ( unsigned v = 0; v < runs; ++v )
    {
        pfx_oneshot_t e;
        e.effect_name = c.str( );
        std::uint32_t mat[ 16 ];
        for ( std::uint32_t& w : mat )
            w = c.next( );
        e.matrix = reinterpret_cast< const float* >( mat );
        const std::uint32_t sb = c.next( );
        std::memcpy( &e.pfx_scale, &sb, 4 );
        for ( std::uint8_t& b : e.pfx_color )
            b = static_cast< std::uint8_t >( c.next( ) );
        e.enable_reflection = static_cast< std::uint8_t >( c.next( ) );
        pfx_manager_t m;
        m.capacity = c.next( );
        m.bits = c.next( );
        m.serial_cap = c.next( );
        m.mask = m.capacity - 1u;
        m.entries.resize( m.capacity );
        m.table.resize( m.capacity );
        for ( unsigned k = 0; k < m.capacity; ++k )
        {
            m.table[ k ] = static_cast< int >( k );
            m.entries[ k ].index = k;
            m.entries[ k ].instance = k;
            m.entries[ k ].serial = c.next( );
        }
        for ( unsigned k = 0; k < m.capacity; ++k )
            m.entries[ k ].flags = c.next( );
        run_t R;
        R.m = &m;
        cur = &R;
        pfx_oneshot_ops_t o;
        o.create = []( void*, const char* ) { return cur->answer; };
        o.duration = []( void*, std::uint32_t i ) { cur->log.push_back( { 10u, i } ); };
        o.instance.set_scale = []( void*, std::uint32_t i, float f ) {
            std::uint32_t w;
            std::memcpy( &w, &f, 4 );
            cur->log.push_back( { 4u, i, w } );
        };
        o.instance.set_color = []( void*, std::uint32_t i, const float* p ) {
            std::vector< unsigned > l{ 5u, i };
            l.resize( 6 );
            std::memcpy( &l[ 2 ], p, 16 );
            cur->log.push_back( l );
        };
        o.instance.set_matrix = []( void*, std::uint32_t i, const float* p ) {
            std::vector< unsigned > l{ 2u, i };
            l.resize( 18 );
            std::memcpy( &l[ 2 ], p, 64 );
            cur->log.push_back( l );
        };
        o.instance.start = []( void*, std::uint32_t i ) { cur->log.push_back( { 11u, i } ); };
        o.instance.stop = []( void*, std::uint32_t i, bool immediate ) { cur->log.push_back( { 12u, i, immediate ? 1u : 0u } ); };
        const unsigned count = c.next( );
        for ( unsigned k = 0; k < count; ++k )
        {
            const unsigned op = c.next( );
            R.answer = static_cast< int >( c.next( ) );
            R.log.clear( );
            if ( op == 0u )
                pfx_oneshot_start( e, m, o );
            else
                pfx_oneshot_stop( e, m, o, op == 2u );
            bool same = e.handle == c.next( );
            for ( unsigned j = 0; j < m.capacity; ++j )
                same = c.next( ) == m.entries[ j ].serial && same;
            for ( unsigned j = 0; j < m.capacity; ++j )
                same = c.next( ) == m.entries[ j ].reflect && same;
            const unsigned nl = c.next( );
            same = nl == R.log.size( ) && same;
            for ( unsigned j = 0; j < nl; ++j )
            {
                const unsigned nw = c.next( );
                for ( unsigned w = 0; w < nw; ++w )
                {
                    const unsigned x = c.next( );
                    same = j < R.log.size( ) && w < R.log[ j ].size( ) && R.log[ j ][ w ] == x && same;
                }
            }
            ++steps;
            if ( !same && bad++ < 8 )
                LOG_ERROR( "oneshot_pfx run %u step %u (op %u) differs from retail", v, k, op );
        }
    }
    cur = nullptr;
    if ( bad == 0 )
        LOG_SUCCESS( "VuOneShotPfxEntity 0x5EA540 0x5EA6A0 0x5EA6F0 match retail: %u runs, %d steps", runs, steps );
    else
        LOG_ERROR( "oneshot_pfx FAILED %d", bad );
    return bad == 0;
}
