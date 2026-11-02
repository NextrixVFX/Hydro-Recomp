#pragma once

#include <workspace/core/world/race.hxx>
#include <workspace/core/world/strings.hxx>
#include <workspace/core/world/background.hxx>
#include <workspace/core/world/action.hxx>
#include <workspace/core/world/mine.hxx>
#include <workspace/core/world/rules.hxx>
#include <workspace/core/world/profile.hxx>

#include <algorithm>
#include <cstdio>

// The world area's second checks file (checks/world.hxx is at its size limit): the race session's
// pieces from world batch 24 on. Included at the end of checks/world.hxx; verify_session_all runs
// from verify_world_all.

// logic_load_properties (world/race.hxx: the logic entities' property sets on the tree through
// race_entity_world, as the race loads them) against retail's VuEntity::load 0x5009D0 on every logic
// entity with a property in the level templates and projects plus hand-built ones (oracle case
// logic_properties), bit for bit: the words from +0x60 and the VuString.
inline bool verify_logic_properties( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_logic_properties_document );
    static std::vector< value_t > documents;
    documents.push_back( c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_logic_properties_document_bytes ) ).parse( ) );
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world = race_entity_world( registry, nullptr );
    const std::unique_ptr< entity_t > tree = entity_load_project( member( documents.back( ), "VuProject" ), "Data/Projects/Check.vuprj", world );
    std::map< std::string, entity_t* > by_name;
    if ( tree )
        for ( auto& child : tree->m_children )
            by_name[ child->m_name ] = child.get( );
    int failed = 0;
    int compared = 0;
    for ( std::size_t k = 0; k < g_golden_logic_properties_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_logic_properties[ k ];
        const auto found = by_name.find( want.m_child );
        logic_state_t* s = found != by_name.end( ) && found->second->m_type == want.m_type ? logic_state_of( *found->second ) : nullptr;
        if ( !s )
        {
            LOG_ERROR( "logic properties: '%s' (%s) missing or without state", want.m_source, want.m_child );
            ++failed;
            continue;
        }
        const std::string type = want.m_type;
        const bool text = type == "VuSetStringEntity" || type == "VuStringEntity" || type == "VuIsSkuEntity" || type == "VuIsEventTypeEntity";
        std::vector< std::uint32_t > ours;
        if ( type == "VuSetBooleanEntity" )
            ours = { s->value.words[ 0 ] & 0xFFu };
        else if ( type == "VuRandomTriggerEntity" )
            ours.assign( s->value.words, s->value.words + 8 );
        else if ( type == "VuCounterEntity" )
            ours = { s->value.words[ 0 ], s->value.words[ 1 ] };
        else if ( !text )
            ours = { s->value.words[ 0 ] };
        world_check_words( "logic properties", want, ours, failed, compared );
        if ( text )
        {
            ++compared;
            if ( s->value.text != want.m_texts[ 0 ] && ++failed <= 8 )
                LOG_ERROR( "logic properties: '%s' Value is \"%s\", retail \"%s\"", want.m_source, s->value.text.c_str( ), want.m_texts[ 0 ] );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "logic_load_properties matches the logic entities' property loads (ctors 0x5DC910 / 0x5DD710 / 0x5E2F40 / 0x5E32C0 / "
                     "0x5E3640 / 0x5E5240 / 0x5E10E0 / 0x5E21B0 / 0x5E4C00, VuEntity::load 0x5009D0) on %d entities: %d values, bit for bit",
            static_cast< int >( g_golden_logic_properties_count ), compared );
    else
        LOG_ERROR( "the logic entities' property loads FAILED %d checks", failed );
    return failed == 0;
}

// dynamic_prop_contact (world/dynprop.hxx) against retail's 0x437F40 run whole (case prop_contact): the two body
// images built from the vector's words (origin +0x40, linear +0x140, angular +0x150, flags +0xD8, group +0x2CA),
// the record's point / normal; compared: +0x164, +0x178, +0x17C and the weak ref's target (the owner or none).
inline bool verify_prop_contact( )
{
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_prop_contact_count; ++k )
    {
        const golden_prop_contact_t& v = g_golden_prop_contact[ k ];
        std::uint8_t own[ 0x2D8 ] = { }, other[ 0x2D8 ] = { };
        const auto fill = [ ]( std::uint8_t* b, const unsigned* w ) {
            static const int at[ 9 ] = { 0x40, 0x44, 0x48, 0x140, 0x144, 0x148, 0x150, 0x154, 0x158 };
            for ( int i = 0; i < 9; ++i )
                std::memcpy( b + at[ i ], &w[ i ], 4 );
            std::memcpy( b + 0xD8, &w[ 9 ], 4 );
            const std::uint16_t g = static_cast< std::uint16_t >( w[ 10 ] );
            std::memcpy( b + 0x2CA, &g, 2 );
        };
        fill( own, v.own );
        fill( other, v.other );
        dynamic_prop_t p{ };
        std::memcpy( &p.threshold_mph, &v.threshold, 4 );
        dynamic_prop_hit_t hit;
        hit.pending = static_cast< std::uint8_t >( v.pending );
        std::memcpy( &hit.strength, &v.strength, 4 );
        float timer;
        std::memcpy( &timer, &v.timer, 4 );
        float point[ 4 ] = { }, normal[ 4 ] = { };
        std::memcpy( point, v.point, 12 );
        std::memcpy( normal, v.normal, 12 );
        dynamic_prop_contact( p, hit, timer, own, other, point, normal, v.owner != 0 );
        std::uint32_t s, t;
        std::memcpy( &s, &hit.strength, 4 );
        std::memcpy( &t, &timer, 4 );
        const int target = hit.target == other ? 1 : ( hit.target ? 2 : 0 );
        if ( hit.pending != v.out_pending || s != v.out_strength || t != v.out_timer || target != v.out_target )
        {
            if ( ++failed <= 8 )
                LOG_ERROR( "prop contact: vector %d: pending %d strength 0x%08X timer 0x%08X target %d, retail %d 0x%08X 0x%08X %d",
                           static_cast< int >( k ), hit.pending, s, t, target, v.out_pending, v.out_strength, v.out_timer, v.out_target );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "dynamic_prop_contact matches VuDynamicGamePropEntity's contact callback 0x437F40 (0x4AD340, the weak ref 0x433B70) on %d "
                     "contacts: the hit, strength, timer and target, bit for bit", static_cast< int >( g_golden_prop_contact_count ) );
    else
        LOG_ERROR( "dynamic_prop_contact FAILED on %d of %d contacts", failed, static_cast< int >( g_golden_prop_contact_count ) );
    return failed == 0;
}

// string_db_reload / string_db_get (world/strings.hxx) against retail's VuStringDBImpl (case string_db): per load
// (four real languages from the pack, two hand-built documents; g 0x7359FC seeded two ways) the result, the
// word-wrap flags, g 0x73579A, the map's entry count and digest (FNV-1a over the entries by hash) and getString on
// the probe keys.
inline bool verify_string_db( )
{
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "string db: the pack is not readable" );
        return false;
    }
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_string_db_count; ++k )
    {
        const golden_string_db_t& want = g_golden_string_db[ k ];
        value_t doc;
        bool have = false;
        if ( want.document )
        {
            const unsigned char* raw = reinterpret_cast< const unsigned char* >( want.document );
            doc = c_reader( std::vector< std::uint8_t >( raw, raw + want.document_bytes ) ).parse( );
            have = true;
        }
        else
        {
            std::vector< std::uint8_t > blob;
            have = archive.read( ( std::string( "VuStringAsset/" ) + want.language ).c_str( ), blob ) && pfx_asset_document( blob, doc );
        }
        string_db_t db;
        db.language = want.language;
        db.wrap_flags = want.flags_in;
        const bool r = string_db_reload( db, [ & ]( const std::string& ) -> const value_t* { return have ? &doc : nullptr; } );
        std::uint32_t h = 0x811C9DC5u;
        const auto mix = [ &h ]( std::uint8_t b ) { h = ( h ^ b ) * 0x1000193u; };
        for ( const auto& e : db.strings )
        {
            for ( int b = 0; b < 4; ++b )
                mix( static_cast< std::uint8_t >( e.first >> ( 8 * b ) ) );
            for ( const char c : e.second )
                mix( static_cast< std::uint8_t >( c ) );
            mix( 0 );
        }
        bool bad = ( r ? 1 : 0 ) != want.result || db.wrap_flags != want.flags || db.flag_73579a != want.flag_73579a ||
                   static_cast< int >( db.strings.size( ) ) != want.count || h != want.digest;
        for ( int g = 0; g < want.gets; ++g )
            if ( string_db_get( db, want.get[ g ].key ) != want.get[ g ].value )
            {
                bad = true;
                LOG_ERROR( "string db %s: getString('%s') = '%s', retail '%s'", want.language, want.get[ g ].key, string_db_get( db, want.get[ g ].key ).c_str( ),
                           want.get[ g ].value );
            }
        if ( bad && ++failed <= 8 )
            LOG_ERROR( "string db %s (flags in 0x%08X): result %d flags 0x%08X 0x73579A %d entries %d digest 0x%08X, retail %d 0x%08X %d %d 0x%08X",
                       want.language, want.flags_in, r ? 1 : 0, db.wrap_flags, db.flag_73579a, static_cast< int >( db.strings.size( ) ), h, want.result,
                       want.flags, want.flag_73579a, want.count, want.digest );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "string_db_reload / string_db_get match VuStringDBImpl's reload 0x53BB40 / loader 0x53D210 / getString 0x53B830 on %d loads "
                     "(the map, the word-wrap flags, the probes), bit for bit", static_cast< int >( g_golden_string_db_count ) );
    else
        LOG_ERROR( "the string db FAILED on %d of %d loads", failed, static_cast< int >( g_golden_string_db_count ) );
    return failed == 0;
}

// front_end_trigger (world/background.hxx: VuSetFrontEndCameraEntity's Trigger 0x408F90) against retail's handler on
// 24 entity images (every Modify combination x 3 value sets; oracle case front_end_trigger): the broadcasts in order -
// the name hash, the VuParams size word and its defined bytes - and the answer (VuRetVal 0: graph.hxx returns retval_t{}).
inline bool verify_front_end_trigger( )
{
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_front_end_trigger_count; ++k )
    {
        const golden_front_end_trigger_t& want = g_golden_front_end_trigger[ k ];
        front_end_trigger_t e;
        std::memcpy( &e.interpolation, &want.interpolation, 4 );
        for ( int m = 0; m < 3; ++m )
            e.modify[ m ] = static_cast< std::uint8_t >( want.modify[ m ] );
        std::memcpy( e.eye, want.eye, 16 );
        std::memcpy( e.target, want.target, 16 );
        std::memcpy( &e.far_plane, &want.far_plane, 4 );
        int n = 0;
        bool bad = false;
        front_end_trigger( e, [ & ]( std::uint32_t hash, const std::uint8_t* p ) {
            std::int32_t size;
            std::memcpy( &size, p + 0x80, 4 );
            if ( n >= want.count )
            {
                bad = true;
                ++n;
                return;
            }
            const golden_front_end_broadcast_t& b = g_golden_front_end_broadcasts[ want.first + n++ ];
            std::uint8_t expect[ 20 ];
            std::memcpy( expect, b.words, 20 );
            if ( hash != b.hash || size != b.size || size < 0 || size > 20 || std::memcmp( p, expect, static_cast< std::size_t >( size ) ) != 0 )
                bad = true;
        } );
        bad = bad || n != want.count || want.retval != 0u || want.eax_is_out != 1;
        if ( bad && ++failed <= 8 )
            LOG_ERROR( "front end trigger %d (modify %d %d %d): %d broadcasts, retail %d (or a hash / size / byte differs)", static_cast< int >( k ),
                       want.modify[ 0 ], want.modify[ 1 ], want.modify[ 2 ], n, want.count );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "front_end_trigger matches VuSetFrontEndCameraEntity's Trigger 0x408F90 on %d entity images (broadcast order, hashes, "
                     "VuParams bytes and sizes, VuRetVal 0), bit for bit", static_cast< int >( g_golden_front_end_trigger_count ) );
    else
        LOG_ERROR( "the front end trigger FAILED on %d of %d images", failed, static_cast< int >( g_golden_front_end_trigger_count ) );
    return failed == 0;
}

// c_action_mode (world/action.hxx: VuActionGameMode's ctor 0x4B9160 / enter 0x4B9630 / tick 0x4B99E0 and the state
// delegates it recreates) against retail's on 3 scripts (oracle case action_mode): per step the state, the FSM time, the
// answer, the fade manager's value / rate / skip words and the calls into the hooks (the same tokens the case logs).
inline bool verify_action_mode( )
{
    const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    const auto real = []( std::uint32_t u ) { float f; std::memcpy( &f, &u, 4 ); return f; };
    int failed = 0, compared = 0;
    for ( std::size_t k = 0; k < g_golden_action_script_count; ++k )
    {
        const golden_action_script_t& want = g_golden_action_scripts[ k ];
        action_fade_t fade;
        fade.m_duration = real( want.duration );
        std::string calls;
        int display[ 2 ] = { 0, 0 };
        const auto note = [ &calls ]( const std::string& s ) { calls += ( calls.empty( ) ? "" : "," ) + s; };
        action_mode_hooks_t h;
        h.erase_dev_restart = [ & ] { note( "0x4FC140" ); };
        h.viewports = [ & ]( int on ) { note( "0x530BB0:" + std::to_string( on ) ); };
        h.load_loading_screen = [ & ] { note( "0x4D3690" ); note( "0x4B4360" ); return false; };
        h.load_snap = [ & ] { note( "0x4B4360" ); return false; };
        h.loading_assets = [ & ] { note( "0x4D3410" ); note( "0x4D2440" ); };
        h.loading_more = [ ] { return false; };   // the case's asset list +0xE8 is null
        h.clock = [ ] { return 12.5; };
        h.broadcast_float = [ & ]( const char* name, float v ) {
            char t[ 64 ];
            std::snprintf( t, sizeof( t ), "bc:%08X:8:00000002:%08X", fsm_hash( name ), bits( v ) );
            note( t );
        };
        h.load_level = [ & ] { note( "0x4BA060" ); };
        h.action_game_data = [ & ] { note( "0x4D2DE0" ); };
        h.pause_tick = [ & ]( float ) { note( "0x4BB540" ); };
        h.snap_pause = [ & ]( bool on ) { note( on ? "0x4BABA0" : "0x4BAC50" ); };
        h.display = [ & ]( int& w, int& hh ) { w = display[ 0 ]; hh = display[ 1 ]; };
        c_action_mode mode( fade, want.dev_restart != 0, h );
        bool bad = calls != want.ctor_calls;
        calls.clear( );
        mode.enter( );
        bad = bad || calls != want.enter_calls || std::string( mode.state( ) ) != want.enter_state || bits( fade.m_value ) != want.enter_fade[ 0 ] ||
              bits( fade.m_rate ) != want.enter_fade[ 1 ] || fade.m_skip != want.enter_fade[ 2 ];
        for ( int s = 0; s < want.count; ++s )
        {
            const golden_action_step_t& step = g_golden_action_steps[ want.first + s ];
            calls.clear( );
            display[ 0 ] = step.w;
            display[ 1 ] = step.h;
            if ( step.has_rate )
                fade.m_rate = real( step.rate );
            if ( step.event == 1 )
                mode.on_end_action_game( );
            else if ( step.event == 2 )
                mode.on_restart_action_game( );
            const std::string answer = mode.tick( real( step.dt ) );
            ++compared;
            const bool wrong = std::string( mode.state( ) ) != step.state || bits( mode.m_fsm.m_time ) != step.time || answer != step.answer ||
                               bits( fade.m_value ) != step.fade[ 0 ] || bits( fade.m_rate ) != step.fade[ 1 ] || fade.m_skip != step.fade[ 2 ] ||
                               calls != step.calls;
            if ( wrong && failed < 8 )
                LOG_ERROR( "action mode script %d step %d: state %s time 0x%08X answer '%s' fade %08X %08X %u calls '%s'; retail %s 0x%08X '%s' %08X %08X %u '%s'",
                           static_cast< int >( k ), s, mode.state( ), bits( mode.m_fsm.m_time ), answer.c_str( ), bits( fade.m_value ), bits( fade.m_rate ),
                           fade.m_skip, calls.c_str( ), step.state, step.time, step.answer, step.fade[ 0 ], step.fade[ 1 ], step.fade[ 2 ], step.calls );
            bad = bad || wrong;
        }
        if ( bad )
            ++failed;
    }
    if ( failed == 0 )
        LOG_SUCCESS( "c_action_mode matches VuActionGameMode's ctor 0x4B9160 / enter 0x4B9630 / tick 0x4B99E0 on %d scripts (%d steps: state, "
                     "FSM time, answer, fade words, hook calls), bit for bit", static_cast< int >( g_golden_action_script_count ), compared );
    else
        LOG_ERROR( "the action mode FAILED on %d of %d scripts", failed, static_cast< int >( g_golden_action_script_count ) );
    return failed == 0;
}

// dynamic_prop_load_properties' mine half (world/dynprop.hxx) against retail's VuEntity::load 0x5009D0 on every
// VuFloatingMineEntity in the templates / projects plus hand-built ones (oracle case mine_properties): the nine explosion
// floats, the seven wave floats (bit for bit) and the two names.
inline bool verify_mine_properties( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_mine_properties_document );
    static std::vector< value_t > documents;
    documents.push_back( c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_mine_properties_document_bytes ) ).parse( ) );
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world = race_entity_world( registry, nullptr );
    const std::unique_ptr< entity_t > tree = entity_load_project( member( documents.back( ), "VuProject" ), "Data/Projects/Check.vuprj", world );
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_mine_properties_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_mine_properties[ k ];
        const entity_t* e = nullptr;
        if ( tree )
            for ( auto& child : tree->m_children )
                if ( child->m_name == want.m_child )
                    e = child.get( );
        const dynamic_prop_state_t* s = e && e->m_state && e->m_type == "VuFloatingMineEntity" ? static_cast< const dynamic_prop_state_t* >( e->m_state.get( ) ) : nullptr;
        if ( !s || !s->mine || want.m_count != 16 )
        {
            LOG_ERROR( "mine properties: %s (%s) missing, without state or not 16 words", want.m_child, want.m_source );
            ++failed;
            continue;
        }
        std::uint32_t got[ 16 ];
        std::memcpy( got, s->value.explosion, 36 );
        std::memcpy( got + 9, s->value.wave, 28 );
        bool bad = s->value.explosion_pfx != want.m_texts[ 0 ] || s->value.explosion_audio != want.m_texts[ 1 ];
        for ( int w = 0; w < 16; ++w )
            bad = bad || got[ w ] != want.m_values[ w ];
        if ( bad && ++failed <= 8 )
            LOG_ERROR( "mine properties: %s (%s) differ from retail's (pfx '%s' / '%s', audio '%s' / '%s')", want.m_child, want.m_source,
                       s->value.explosion_pfx.c_str( ), want.m_texts[ 0 ], s->value.explosion_audio.c_str( ), want.m_texts[ 1 ] );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "dynamic_prop_load_properties (mine) matches VuFloatingMineEntity's loads on %d entities (16 floats + 2 names), bit for bit",
                     static_cast< int >( g_golden_mine_properties_count ) );
    else
        LOG_ERROR( "the mine properties FAILED on %d of %d entities", failed, static_cast< int >( g_golden_mine_properties_count ) );
    return failed == 0;
}

// explosion_add / mine_contact (world/mine.hxx) against retail's VuExplosionManagerImpl add 0x510180 (fake listeners:
// the hits in order and their scale words) and VuFloatingMineEntity's contact callback 0x438E10 (the request byte, the
// component's +0xD0, the body's +0xD8) - oracle case mine_explode.
inline bool verify_mine_explode( )
{
    struct lis_t
    {
        const golden_mine_listener_t* g;
        int index;
        std::vector< std::pair< int, std::uint32_t > >* hits;
    };
    int failed = 0, hits_total = 0;
    for ( std::size_t k = 0; k < g_golden_mine_add_count; ++k )
    {
        const golden_mine_add_t& want = g_golden_mine_adds[ k ];
        std::uint8_t params[ 0x38 ] = { };
        std::memcpy( params, want.position, 12 );
        std::memcpy( params + 0x10, &want.outer, 4 );
        std::memcpy( params + 0x14, &want.inner, 4 );
        std::vector< std::pair< int, std::uint32_t > > got;
        std::vector< lis_t > owners( static_cast< std::size_t >( want.count ) );
        std::vector< explosion_listener_t > ls;
        for ( int i = 0; i < want.count; ++i )
        {
            owners[ static_cast< std::size_t >( i ) ] = { &g_golden_mine_listeners[ want.first + i ], i, &got };
            explosion_listener_t l;
            l.user = &owners[ static_cast< std::size_t >( i ) ];
            l.position = []( void* u, float* out ) { std::memcpy( out, static_cast< lis_t* >( u )->g->position, 16 ); };
            l.radius = []( void* u ) { float r; std::memcpy( &r, &static_cast< lis_t* >( u )->g->radius, 4 ); return r; };
            l.hit = []( void* u, const std::uint8_t*, float scale ) {
                std::uint32_t w;
                std::memcpy( &w, &scale, 4 );
                static_cast< lis_t* >( u )->hits->push_back( { static_cast< lis_t* >( u )->index, w } );
            };
            ls.push_back( l );
        }
        explosion_add( params, ls, hull_sqrt );
        bool bad = static_cast< int >( got.size( ) ) != want.hits;
        for ( int h = 0; !bad && h < want.hits; ++h )
            bad = got[ static_cast< std::size_t >( h ) ].first != g_golden_mine_hits[ want.hit_first + h ].listener ||
                  got[ static_cast< std::size_t >( h ) ].second != g_golden_mine_hits[ want.hit_first + h ].scale;
        hits_total += want.hits;
        if ( bad && ++failed <= 8 )
            LOG_ERROR( "explosion add %d: %d hits, retail %d (or a listener / scale differs)", static_cast< int >( k ), static_cast< int >( got.size( ) ), want.hits );
    }
    for ( std::size_t k = 0; k < g_golden_mine_contact_count; ++k )
    {
        const golden_mine_contact_t& want = g_golden_mine_contacts[ k ];
        mine_contact_t in;
        in.owner = want.owner != 0;
        in.owner_is_boat = want.owner == 1;
        in.driver_answer = ( want.driver & 0xFFu ) != 0u;
        in.other_flags = want.flags;
        rigid_component_t c;
        c.flags_d0 = want.d0;
        std::uint8_t body[ 0x2E0 ] = { };
        std::uint8_t request = 0;
        mine_contact( in, request, c, want.body ? body : nullptr );
        std::uint32_t d8;
        std::memcpy( &d8, body + 0xD8, 4 );
        if ( ( request != want.request || c.flags_d0 != want.out_d0 || d8 != want.out_d8 ) && ++failed <= 8 )
            LOG_ERROR( "mine contact %d (owner %d flags 0x%X driver 0x%X): request %d +0xD0 0x%X +0xD8 0x%X, retail %d 0x%X 0x%X", static_cast< int >( k ),
                       want.owner, want.flags, want.driver, request, c.flags_d0, d8, want.request, want.out_d0, want.out_d8 );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "explosion_add / mine_contact match retail's 0x510180 (%d explosions, %d hits) and 0x438E10 (%d contacts), bit for bit",
                     static_cast< int >( g_golden_mine_add_count ), hits_total, static_cast< int >( g_golden_mine_contact_count ) );
    else
        LOG_ERROR( "the mine explosion FAILED on %d vectors", failed );
    return failed == 0;
}

// breakable_load_properties (world/breakable.hxx) against retail's VuEntity::load on the breakable templates /
// projects plus hand-built ones (oracle case breakable_properties): Initially Visible, the 7 floats, the 2 names.
inline bool verify_breakable_properties( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_breakable_properties_document );
    static std::vector< value_t > documents;
    documents.push_back( c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_breakable_properties_document_bytes ) ).parse( ) );
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world = race_entity_world( registry, nullptr );
    const std::unique_ptr< entity_t > tree = entity_load_project( member( documents.back( ), "VuProject" ), "Data/Projects/Check.vuprj", world );
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_breakable_properties_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_breakable_properties[ k ];
        const entity_t* e = nullptr;
        if ( tree )
            for ( auto& child : tree->m_children )
                if ( child->m_name == want.m_child )
                    e = child.get( );
        const breakable_state_t* s = e && e->m_state && e->m_type == "VuBreakablePropEntity" ? static_cast< const breakable_state_t* >( e->m_state.get( ) ) : nullptr;
        if ( !s || want.m_count != 8 )
        {
            LOG_ERROR( "breakable properties: %s (%s) missing, without state or not 8 words", want.m_child, want.m_source );
            ++failed;
            continue;
        }
        const breakable_t& v = s->value;
        std::uint32_t got[ 8 ];
        got[ 0 ] = v.visible;
        std::memcpy( got + 1, &v.threshold_mph, 4 );
        std::memcpy( got + 2, v.shake, 16 );
        std::memcpy( got + 6, v.rumble, 8 );
        bool bad = v.pfx != want.m_texts[ 0 ] || v.sfx != want.m_texts[ 1 ];
        for ( int w = 0; w < 8; ++w )
            bad = bad || got[ w ] != want.m_values[ w ];
        if ( bad && ++failed <= 8 )
            LOG_ERROR( "breakable properties: %s (%s) differ from retail's", want.m_child, want.m_source );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "breakable_load_properties matches VuBreakablePropEntity's loads on %d entities (8 words + 2 names), bit for bit",
                     static_cast< int >( g_golden_breakable_properties_count ) );
    else
        LOG_ERROR( "the breakable properties FAILED on %d of %d entities", failed, static_cast< int >( g_golden_breakable_properties_count ) );
    return failed == 0;
}

// explosion_entity_load_properties (world/dynprop.hxx) against retail's VuEntity::load on every VuExplosionEntity of the
// level templates / projects plus two hand-built (oracle case explosion_properties): the nine floats, the two names.
inline bool verify_explosion_properties( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_explosion_properties_document );
    static std::vector< value_t > documents;
    documents.push_back( c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_explosion_properties_document_bytes ) ).parse( ) );
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world = race_entity_world( registry, nullptr );
    const std::unique_ptr< entity_t > tree = entity_load_project( member( documents.back( ), "VuProject" ), "Data/Projects/Check.vuprj", world );
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_explosion_properties_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_explosion_properties[ k ];
        const entity_t* e = nullptr;
        if ( tree )
            for ( auto& child : tree->m_children )
                if ( child->m_name == want.m_child )
                    e = child.get( );
        const explosion_entity_state_t* s = e ? explosion_entity_state_of( *e ) : nullptr;
        if ( !s || want.m_count != 9 )
        {
            LOG_ERROR( "explosion properties: %s (%s) missing, without state or not 9 words", want.m_child, want.m_source );
            ++failed;
            continue;
        }
        bool bad = s->value.pfx != want.m_texts[ 0 ] || s->value.audio != want.m_texts[ 1 ];
        for ( int w = 0; w < 9; ++w )
        {
            std::uint32_t x;
            std::memcpy( &x, &s->value.explosion[ w ], 4 );
            bad = bad || x != want.m_values[ w ];
        }
        if ( bad && ++failed <= 8 )
            LOG_ERROR( "explosion properties: %s (%s) differ from retail's", want.m_child, want.m_source );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "explosion_entity_load_properties matches VuExplosionEntity's loads on %d entities (9 floats + 2 names), bit for bit",
                     static_cast< int >( g_golden_explosion_properties_count ) );
    else
        LOG_ERROR( "the explosion properties FAILED on %d of %d entities", failed, static_cast< int >( g_golden_explosion_properties_count ) );
    return failed == 0;
}

// slalom_gate_load_properties (world/slalom.hxx) against retail's VuEntity::load on the first 3 gates of every Slalom
// game project plus four hand-built ones (oracle case slalom_gate_properties): Diameter, Allow Recovery, the 2 names.
inline bool verify_slalom_gate_properties( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_slalom_gate_properties_document );
    static std::vector< value_t > documents;
    documents.push_back( c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_slalom_gate_properties_document_bytes ) ).parse( ) );
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world = race_entity_world( registry, nullptr );
    const std::unique_ptr< entity_t > tree = entity_load_project( member( documents.back( ), "VuProject" ), "Data/Projects/Check.vuprj", world );
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_slalom_gate_properties_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_slalom_gate_properties[ k ];
        const entity_t* e = nullptr;
        if ( tree )
            for ( auto& child : tree->m_children )
                if ( child->m_name == want.m_child )
                    e = child.get( );
        const slalom_gate_state_t* s = e ? slalom_gate_state_of( *e ) : nullptr;
        if ( !s || want.m_count != 2 )
        {
            LOG_ERROR( "slalom gate properties: %s (%s) missing, without state or not 2 words", want.m_child, want.m_source );
            ++failed;
            continue;
        }
        std::uint32_t d;
        std::memcpy( &d, &s->value.diameter, 4 );
        const bool bad = d != want.m_values[ 0 ] || s->value.allow_recovery != want.m_values[ 1 ] || s->value.model != want.m_texts[ 0 ] ||
                         s->value.reflection != want.m_texts[ 1 ];
        if ( bad && ++failed <= 8 )
            LOG_ERROR( "slalom gate properties: %s (%s) differ from retail's (Diameter 0x%X Allow Recovery %d '%s' '%s')", want.m_child, want.m_source, d,
                       s->value.allow_recovery, s->value.model.c_str( ), s->value.reflection.c_str( ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "slalom_gate_load_properties matches VuSlalomGateEntity's loads on %d gates (Diameter, Allow Recovery, 2 names), bit for bit",
                     static_cast< int >( g_golden_slalom_gate_properties_count ) );
    else
        LOG_ERROR( "the slalom gate properties FAILED on %d of %d gates", failed, static_cast< int >( g_golden_slalom_gate_properties_count ) );
    return failed == 0;
}

// breakable_contact (world/breakable.hxx) against retail's 0x434730 on 600 hand-built contacts (oracle case
// breakable_contact): the request byte, the kept velocity (when one is kept), the component's +0xD0, the own body's
// +0xD8 and whether the target became the other's owner (the caller's weak ref: set iff a request and an owner).
inline int breakable_contact_failures( const golden_breakable_contact_t* table, std::size_t count );

inline bool verify_breakable_contact( )
{
    const int failed = breakable_contact_failures( g_golden_breakable_contact, g_golden_breakable_contact_count );
    if ( failed == 0 )
        LOG_SUCCESS( "breakable_contact matches VuBreakablePropEntity's contact callback 0x434730 on %d contacts, bit for bit",
                     static_cast< int >( g_golden_breakable_contact_count ) );
    else
        LOG_ERROR( "the breakable contact FAILED on %d of %d contacts", failed, static_cast< int >( g_golden_breakable_contact_count ) );
    return failed == 0;
}

// The same function against VuBreakableDynamicGamePropEntity's 0x4335A0 on the same vectors (case breakable_dynamic_contact:
// its outputs equal 0x434730's on every vector) - the dynamic breakable's contact (drive.hxx) calls breakable_contact.
inline bool verify_breakable_dynamic_contact( )
{
    const int failed = breakable_contact_failures( g_golden_breakable_dynamic_contact, g_golden_breakable_dynamic_contact_count );
    if ( failed == 0 )
        LOG_SUCCESS( "breakable_contact matches VuBreakableDynamicGamePropEntity's contact callback 0x4335A0 on %d contacts, bit for bit",
                     static_cast< int >( g_golden_breakable_dynamic_contact_count ) );
    else
        LOG_ERROR( "the breakable dynamic contact FAILED on %d of %d contacts", failed, static_cast< int >( g_golden_breakable_dynamic_contact_count ) );
    return failed == 0;
}

inline int breakable_contact_failures( const golden_breakable_contact_t* table, std::size_t count )
{
    int failed = 0;
    for ( std::size_t k = 0; k < count; ++k )
    {
        const golden_breakable_contact_t& want = table[ k ];
        std::uint8_t own[ 0x2E0 ] = { }, other[ 0x2E0 ] = { };
        const int at[ 9 ] = { 0x40, 0x44, 0x48, 0x140, 0x144, 0x148, 0x150, 0x154, 0x158 };
        for ( int i = 0; i < 9; ++i )
        {
            std::memcpy( own + at[ i ], &want.own[ i ], 4 );
            std::memcpy( other + at[ i ], &want.other[ i ], 4 );
        }
        std::memcpy( own + 0xD8, &want.own[ 9 ], 4 );
        std::memcpy( own + 0x2D0, &want.own[ 10 ], 4 );
        std::memcpy( other + 0xD8, &want.other[ 9 ], 4 );
        std::memcpy( other + 0x2D0, &want.other[ 10 ], 4 );
        float threshold, point[ 3 ], normal[ 3 ], velocity[ 3 ] = { 0.0f, 0.0f, 0.0f };
        std::memcpy( &threshold, &want.threshold, 4 );
        std::memcpy( point, want.point, 12 );
        std::memcpy( normal, want.normal, 12 );
        rigid_component_t c;
        c.flags_d0 = want.d0;
        std::uint8_t request = 0;
        const int r = breakable_contact( threshold, own, other, point, normal, request, velocity, c, own );
        std::uint32_t d8, vw[ 3 ];
        std::memcpy( &d8, own + 0xD8, 4 );
        std::memcpy( vw, velocity, 12 );
        const int target = request && want.other[ 11 ] ? 1 : 0;
        const bool bad = request != want.request || c.flags_d0 != want.out_d0 || d8 != want.out_d8 || target != want.target ||
                         ( r == 1 ? std::memcmp( vw, want.velocity, 12 ) != 0 : ( want.velocity[ 0 ] | want.velocity[ 1 ] | want.velocity[ 2 ] ) != 0u );
        if ( bad && ++failed <= 8 )
            LOG_ERROR( "breakable contact %d: request %d (retail %d) +0xD0 0x%X (0x%X) +0xD8 0x%X (0x%X) velocity %08X %08X %08X (%08X %08X %08X)",
                       static_cast< int >( k ), request, want.request, c.flags_d0, want.out_d0, d8, want.out_d8, vw[ 0 ], vw[ 1 ], vw[ 2 ],
                       want.velocity[ 0 ], want.velocity[ 1 ], want.velocity[ 2 ] );
    }
    return failed;
}

// The canonical text of a tree, as oracle case results_writer prints retail's containers: n null, i / f / b / l the
// payload in hex (an int, float bits, the bool byte, an int64), s<len>:<text>, [..] an array, {key=..} a map in key
// order (std::map's), t<tag> anything else.
inline std::string race_results_canonical( const value_t& v )
{
    char t[ 32 ];
    switch ( v.tag )
    {
    case 0: return "n";
    case 1: std::snprintf( t, sizeof( t ), "i%X", static_cast< unsigned >( v.integer ) ); return t;
    case 2:
    {
        std::uint32_t w;
        std::memcpy( &w, &v.real, 4 );
        std::snprintf( t, sizeof( t ), "f%X", w );
        return t;
    }
    case 3: std::snprintf( t, sizeof( t ), "b%X", static_cast< unsigned >( v.integer & 0xFF ) ); return t;
    case 4: return "s" + std::to_string( v.text.size( ) ) + ":" + v.text;
    case 5:
    {
        std::string s = "[";
        for ( std::size_t k = 0; k < v.list.size( ); ++k )
            s += ( k ? "," : "" ) + race_results_canonical( v.list[ k ] );
        return s + "]";
    }
    case 6:
    {
        std::vector< const std::pair< std::string, value_t >* > fields;
        for ( const auto& f : v.object )
            fields.push_back( &f );
        std::sort( fields.begin( ), fields.end( ), [ ]( const auto* a, const auto* b ) { return a->first < b->first; } );
        std::string s = "{";
        for ( std::size_t k = 0; k < fields.size( ); ++k )
            s += ( k ? "," : "" ) + std::string( "s" ) + std::to_string( fields[ k ]->first.size( ) ) + ":" + fields[ k ]->first + "=" +
                 race_results_canonical( fields[ k ]->second );
        return s + "}";
    }
    case 7: std::snprintf( t, sizeof( t ), "l%llX", static_cast< unsigned long long >( v.int64 ) ); return t;
    default: return "t" + std::to_string( v.tag );
    }
}

inline std::string race_results_unhex( const char* h )
{
    std::string s;
    for ( ; h[ 0 ] && h[ 1 ]; h += 2 )
    {
        const auto nib = [ ]( char c ) { return c <= '9' ? c - '0' : ( c | 0x20 ) - 'a' + 10; };
        s.push_back( static_cast< char >( nib( h[ 0 ] ) * 16 + nib( h[ 1 ] ) ) );
    }
    return s;
}

// race_game_write_results (world/rules.hxx) against retail's VuRaceGame slot 12 0x489C60 run whole (oracle case
// results_writer): sessions of calls on one gameutil root (fresh; GameResults a string; GameResults.Table an int), each
// boat's fields / texts (inline, heap, empty, an embedded NUL) / driver class (the five vtables' slots 12 / 14 through
// boat_driver_of, and two fake (human, not local), (neither)); compared: every boat's place and the whole tree after.
inline bool verify_race_results( )
{
    int failed = 0;
    value_t root;
    for ( std::size_t k = 0; k < g_golden_results_writer_count; ++k )
    {
        const golden_results_writer_call_t& want = g_golden_results_writer[ k ];
        if ( want.first )
        {
            root = value_t{ };
            if ( std::strcmp( want.pre, "string" ) == 0 )
                ui_json_set_string( ui_json_child( root, "GameResults" ), "junk text" );
            else if ( std::strcmp( want.pre, "table_int" ) == 0 )
                race_results_set( ui_json_child( ui_json_child( root, "GameResults" ), "Table" ), 1, 77 );
        }
        std::vector< std::vector< std::uint8_t > > images( static_cast< std::size_t >( want.nb ), std::vector< std::uint8_t >( 0x520, 0 ) );
        std::vector< race_rules_boat_t > boats( static_cast< std::size_t >( want.nb ) );
        std::vector< race_results_entry_t > entries( static_cast< std::size_t >( want.nb ) );
        race_rules_game_t g;
        g.circuit = static_cast< std::uint8_t >( want.circuit );
        static const std::uint32_t k_vt[ 5 ] = { 0x737B5Cu, 0x737F44u, 0x737CDCu, 0x737C4Cu, 0x737DDCu };
        for ( int b = 0; b < want.nb; ++b )
        {
            const golden_results_writer_boat_t& w = want.boats[ b ];
            std::uint8_t* img = images[ static_cast< std::size_t >( b ) ].data( );
            img[ 0x32F ] = static_cast< std::uint8_t >( w.finished );
            std::memcpy( img + 0x4B4, &w.time, 4 );
            std::memcpy( img + 0x4B8, &w.best, 4 );
            std::memcpy( img + 0x4D4, &w.laps, 4 );
            boats[ static_cast< std::size_t >( b ) ].boat = img;
            g.order.push_back( want.order[ b ] );
            race_results_entry_t& e = entries[ static_cast< std::size_t >( b ) ];
            e.boat_class = race_results_unhex( w.cls );
            e.boat_name = race_results_unhex( w.boat );
            e.driver_name = race_results_unhex( w.name );
            if ( w.kind < 5 )
            {
                const boat_driver_t d = boat_driver_of( k_vt[ w.kind ] );
                e.human = d.slot[ 0 ], e.local = d.slot[ 2 ];
            }
            else
                e.human = w.kind == 5, e.local = false;
        }
        float dt;
        std::memcpy( &dt, &want.dt, 4 );
        race_game_write_results( root, g, boats, entries, dt );
        bool same = race_results_canonical( root ) == want.root;
        for ( int b = 0; b < want.nb; ++b )
            same = same && rules_i32( images[ static_cast< std::size_t >( b ) ].data( ), 0x4CC ) == want.places[ b ];
        if ( !same && ++failed <= 4 )
            LOG_ERROR( "race results call %d (session %d): ours %.300s\n retail %.300s", static_cast< int >( k ), want.session,
                       race_results_canonical( root ).c_str( ), want.root );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "race_game_write_results matches VuRaceGame slot 12 0x489C60 on %d calls (the places and the whole GameResults tree)",
                     static_cast< int >( g_golden_results_writer_count ) );
    else
        LOG_ERROR( "the race results writer FAILED on %d of %d calls", failed, static_cast< int >( g_golden_results_writer_count ) );
    return failed == 0;
}

// timetrial_write_results (world/modes.hxx) against retail's VuTimeTrialGame slot 12 0x48F560 run whole (oracle case
// timetrial_results): the once flag, IsChampionship / the circuit / the laps / the track, the racer (+0x18 [0]) and the
// human (+0x24 [0]: the same boat or another), their drivers (the five vtables through boat_driver_of and two fakes),
// TrackDB / BoatDB / the progress manager's round (VUJB read by c_reader), the pre-filled root, the VuRandom state.
// Compared per vector: the whole root (canonical), +0x738, the 34 VuRandom words after, the StringDB ids asked for in
// order, the progress-manager calls and the qsort's element count - one failure per differing vector.
// The same for VuSlalomGame slot 12 0x48CAF0 (case slalom_results: + each boat's +0x4D0 GatesMissed, its once flag +0x768).
inline bool verify_game_results_writer( const unsigned* words, const char* const* texts, bool slalom )
{
    int failed = 0;
    const unsigned* w = words;
    std::size_t s = 0;
    const auto next = [ & ] { return *w++; };
    const auto text = [ & ] { return std::string( texts[ s++ ] ); };
    const auto doc = [ & ] {
        const std::string hex = text( );
        std::vector< std::uint8_t > bytes;
        for ( std::size_t k = 0; k + 1 < hex.size( ); k += 2 )
            bytes.push_back( static_cast< std::uint8_t >( std::stoul( hex.substr( k, 2 ), nullptr, 16 ) ) );
        return c_reader( bytes ).parse( );
    };
    static const std::uint32_t k_vt[ 5 ] = { 0x737B5Cu, 0x737F44u, 0x737CDCu, 0x737C4Cu, 0x737DDCu };
    const unsigned nv = next( );
    for ( unsigned v = 0; v < nv; ++v )
    {
        std::uint8_t once = static_cast< std::uint8_t >( next( ) );
        timetrial_results_in_t in;
        in.slalom = slalom;
        in.championship = static_cast< std::uint8_t >( next( ) );
        in.circuit = static_cast< std::uint8_t >( next( ) );
        in.laps = static_cast< std::int32_t >( next( ) );
        const bool two = next( ) != 0;
        in.track = text( );
        std::vector< std::uint8_t > images[ 2 ] = { std::vector< std::uint8_t >( 0x520, 0 ), std::vector< std::uint8_t >( 0x520, 0 ) };
        race_results_entry_t entries[ 2 ];
        for ( int b = 0; b < 2; ++b )
        {
            std::uint8_t* img = images[ b ].data( );
            img[ 0x32F ] = static_cast< std::uint8_t >( next( ) );
            const unsigned time = next( ), best = next( );
            std::memcpy( img + 0x4B4, &time, 4 );
            std::memcpy( img + 0x4B8, &best, 4 );
            rules_put_i32( img, 0x4CC, static_cast< std::int32_t >( next( ) ) );
            rules_put_i32( img, 0x4D4, static_cast< std::int32_t >( next( ) ) );
            const unsigned kind = next( );
            if ( slalom )
                rules_put_i32( img, 0x4D0, static_cast< std::int32_t >( next( ) ) );
            race_results_entry_t& e = entries[ b ];
            e.boat_class = text( );
            e.boat_name = text( );
            e.driver_name = text( );
            if ( kind < 5 )
            {
                const boat_driver_t d = boat_driver_of( k_vt[ kind ] );
                e.human = d.slot[ 0 ], e.local = d.slot[ 2 ];
            }
            else
                e.human = kind == 5, e.local = false;
        }
        in.racer = { images[ 0 ].data( ), entries[ 0 ] };
        in.human = two ? timetrial_results_boat_t{ images[ 1 ].data( ), entries[ 1 ] } : in.racer;
        const value_t tdb = doc( ), bdb = doc( ), cur = doc( );
        in.track_db = &tdb;
        in.boat_db = &bdb;
        int pm = 0;
        in.current = [ & ]( ) { ++pm; return &cur; };
        std::vector< std::string > ids;
        in.string_db = [ & ]( const char* id ) {
            ids.push_back( id );
            return "TN" + std::string( id ) + "#" + std::to_string( v );
        };
        fleet_random_t rnd;
        {
            unsigned words[ 34 ];
            for ( unsigned& x : words )
                x = next( );
            std::memcpy( &rnd, words, sizeof( words ) );
        }
        in.random = &rnd;
        value_t root;
        const std::string pre = text( );
        if ( pre == "string" )
            ui_json_set_string( ui_json_child( root, "GameResults" ), "junk text" );
        else if ( pre == "table_int" )
            race_results_set( ui_json_child( ui_json_child( root, "GameResults" ), "Table" ), 1, 77 );
        else if ( pre == "stale" )
        {
            race_results_set( ui_json_child( ui_json_child( root, "GameResults" ), "Old" ), 1, 5 );
            race_results_set( ui_json_child( root, "Old" ), 1, 6 );
        }
        const bool known = timetrial_write_results( root, once, in );
        const std::string want_root = text( );
        const unsigned want_once = next( );
        bool same = known && race_results_canonical( root ) == want_root && once == want_once;
        unsigned out[ 34 ];
        std::memcpy( out, &rnd, sizeof( out ) );
        for ( unsigned x : out )
            same = next( ) == x && same;
        const unsigned nid = next( );
        same = ids.size( ) == nid && same;
        for ( unsigned k = 0; k < nid; ++k )
            same = text( ) == ( k < ids.size( ) ? ids[ k ] : std::string( ) ) && same;
        same = static_cast< unsigned >( pm ) == next( ) && same;
        const unsigned nq = next( );
        for ( unsigned k = 0; k < nq; ++k )
            next( );   // the qsort's element count: the rows the tree holds
        if ( !same && ++failed <= 4 )
            LOG_ERROR( "%s results vector %u: ours %.400s\n retail %.400s", slalom ? "slalom" : "timetrial", v, race_results_canonical( root ).c_str( ),
                       want_root.c_str( ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "timetrial_write_results matches %s on %u vectors (the whole root, the once flag, VuRandom, the StringDB ids, the round calls)",
                     slalom ? "VuSlalomGame slot 12 0x48CAF0" : "VuTimeTrialGame slot 12 0x48F560", nv );
    else
        LOG_ERROR( "the %s results writer FAILED on %d of %u vectors", slalom ? "slalom" : "time trial", failed, nv );
    return failed == 0;
}

inline bool verify_timetrial_results( ) { return verify_game_results_writer( g_golden_ttr_words, g_golden_ttr_text, false ); }
inline bool verify_slalom_results( ) { return verify_game_results_writer( g_golden_slr_words, g_golden_slr_text, true ); }

// ui_race_checkpoint (core/ui split.hxx - the UI agent's VuRaceGame slot 15 0x48B180) against retail's VuTimeTrialGame
// slot 15 0x490D70 run whole (oracle case timetrial_split): the same code byte for byte but for the split vector's place
// (+0x720, VuRaceGame's +0x72C) - compared: +0x6D8 / +0x6DC, every condition's value / pulse (and the record before the
// vector: the missing-condition write), the split vector after. One failure per differing vector.
inline bool verify_game_split( const unsigned* words, const char* const* texts, const char* label )
{
    int failed = 0;
    const unsigned* w = words;
    std::size_t s = 0;
    const auto next = [ & ] { return *w++; };
    const unsigned sentinel = next( );
    std::vector< std::vector< std::string > > sets( next( ) );
    for ( auto& set : sets )
        set.resize( next( ) );
    for ( auto& set : sets )
        for ( std::string& n : set )
            n = texts[ s++ ];
    std::vector< value_t > docs( next( ) );
    for ( value_t& d : docs )
    {
        d.tag = next( );
        const unsigned n = next( );
        for ( unsigned k = 0; k < n; ++k )
        {
            value_t e;
            e.tag = next( );
            const unsigned lo = next( ), hi = next( );
            e.integer = static_cast< int >( lo );
            std::memcpy( &e.real, &lo, 4 );
            e.int64 = static_cast< long long >( ( static_cast< unsigned long long >( hi ) << 32 ) | lo );
            if ( d.tag == 5 )
                d.list.push_back( e );
        }
    }
    const unsigned nv = next( );
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned human = next( ), lap = next( ), laps = next( ), finish = next( ), ji = next( ), prior = next( ), ci = next( ), race = next( ),
                       best = next( );
        c_fsm fsm;
        for ( const std::string& n : sets[ ci ] )
        {
            fsm_condition_t c;
            c.m_name = n;
            c.m_hash = fsm_hash( n.c_str( ) );
            fsm.m_conditions.push_back( c );
        }
        ui_race_split_t g;
        g.m_laps = static_cast< std::int32_t >( laps );
        std::memcpy( &g.m_best_time, &best, 4 );
        g.m_best_splits = docs[ ji ];
        std::memcpy( &g.m_split_delta, &sentinel, 4 );
        std::memcpy( &g.m_result_delta, &sentinel, 4 );
        for ( unsigned k = 0; k < prior; ++k )
        {
            const unsigned t = next( );
            float f;
            std::memcpy( &f, &t, 4 );
            g.m_splits.push_back( f );
        }
        ui_race_split_boat_t boat;
        boat.m_human = human != 0;
        boat.m_lap = static_cast< std::int32_t >( lap );
        std::memcpy( &boat.m_race_time, &race, 4 );
        const bool ok = ui_race_checkpoint( g, fsm, boat, finish != 0 );
        std::string have = ok ? "0000" : "0001";
        for ( const fsm_condition_t& c : fsm.m_conditions )
        {
            char t[ 8 ];
            std::snprintf( t, sizeof( t ), "%02x%02x", c.m_value, c.m_pulse );
            have += t;
        }
        const unsigned after = next( );
        bool same = g.m_splits.size( ) == after;
        for ( unsigned k = 0; k < after; ++k )
        {
            const unsigned t = next( );
            same = same && std::memcmp( &g.m_splits[ k ], &t, 4 ) == 0;
        }
        unsigned d[ 2 ];
        std::memcpy( &d[ 0 ], &g.m_split_delta, 4 );
        std::memcpy( &d[ 1 ], &g.m_result_delta, 4 );
        const unsigned d0 = next( ), d1 = next( );
        const std::string want = texts[ s++ ];
        same = same && d[ 0 ] == d0 && d[ 1 ] == d1 && have == want;
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "%s split vector %u: conditions %s / %s, deltas 0x%08X 0x%08X / 0x%08X 0x%08X", label, v, have.c_str( ), texts[ s - 1 ], d[ 0 ],
                       d[ 1 ], d0, d1 );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "ui_race_checkpoint matches %s on %u vectors (+0x6D8 / +0x6DC, the conditions, the splits)", label, nv );
    else
        LOG_ERROR( "the %s split FAILED on %d of %u vectors", label, failed, nv );
    return failed == 0;
}

// The same against VuSlalomGame slot 15 0x48E500 (case slalom_split: its split vector at +0x750).
inline bool verify_timetrial_split( ) { return verify_game_split( g_golden_tts_words, g_golden_tts_text, "VuTimeTrialGame slot 15 0x490D70" ); }
inline bool verify_slalom_split( ) { return verify_game_split( g_golden_sls_words, g_golden_sls_text, "VuSlalomGame slot 15 0x48E500" ); }
// And VuGhostGame slot 15 0x487640 (case ghost_split: its split vector at +0x728).
inline bool verify_ghost_split( ) { return verify_game_split( g_golden_gts_words, g_golden_gts_text, "VuGhostGame slot 15 0x487640" ); }

// The course system (world/slalom.hxx slalom_course_start / slalom_gates_tick / slalom_recovered) against retail's
// VuSlalomGame slot 5 0x48C660, the gates tick 0x48E6E0 (0x43FC70 / 0x48E910 / 0x48EA40 / 0x48EBB0) and OnBoatRecovered
// 0x48DB70 run whole (oracle case slalom_gates: 40 courses of 0..8 drawn gates, 160 frames of a boat steered along them,
// drawn recoveries, ConstantDB drawn and broken). Compared after the start and every frame: +0x73C / +0x740, +0x748 /
// +0x74C, every gate's +0xE8, the boat's +0x310 / +0x344 / +0x348 / +0x4B0 / +0x4B4 / +0x4D0 / +0x4DC, and the platform
// calls (the FMOD sounds by name and position, the broadcasts by hash) - one failure per differing snapshot.
inline bool verify_slalom_gates( )
{
    const unsigned* w = g_golden_sg_words;
    const auto next = [ & ] { return *w++; };
    std::size_t s = 0;
    int failed = 0, snaps = 0, sounds = 0;
    const unsigned nv = next( );
    struct log_t
    {
        std::vector< unsigned > calls;
    };
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned ng = next( );
        std::vector< std::array< float, 16 > > worlds( ng );
        std::vector< slalom_gate_t > gates( ng );
        std::vector< unsigned > connected( ng );
        for ( unsigned k = 0; k < ng; ++k )
        {
            for ( float& x : worlds[ k ] )
            {
                const unsigned b = next( );
                std::memcpy( &x, &b, 4 );
            }
            const unsigned d = next( );
            std::memcpy( &gates[ k ].diameter, &d, 4 );
            gates[ k ].allow_recovery = static_cast< std::uint8_t >( next( ) );
            connected[ k ] = next( );
            gates[ k ].world = worlds[ k ].data( );
            gates[ k ].prev = nullptr;
        }
        for ( unsigned k = 0; k < ng; ++k )
            gates[ k ].next = connected[ k ] && k + 1 < ng ? &gates[ k + 1 ] : nullptr;
        slalom_course_t c;
        c.first = c.current = ng ? &gates[ 0 ] : nullptr;
        std::vector< std::uint8_t > boat( 0x600, 0 );
        const auto put = [ & ]( int at ) { const unsigned b = next( ); std::memcpy( boat.data( ) + at, &b, 4 ); };
        put( 0x190 ), put( 0x194 ), put( 0x198 ), put( 0x1A0 ), put( 0x310 ), put( 0x4B0 ), put( 0x4B4 ), put( 0x4D0 );
        float world[ 16 ];
        for ( float& x : world )
        {
            const unsigned b = next( );
            std::memcpy( &x, &b, 4 );
        }
        std::memcpy( boat.data( ) + 0x4DC, world, 64 );
        const std::string cls = g_golden_sg_text[ s++ ];
        const std::string hex = g_golden_sg_text[ s++ ];
        std::vector< std::uint8_t > bytes;
        for ( std::size_t k = 0; k + 1 < hex.size( ); k += 2 )
            bytes.push_back( static_cast< std::uint8_t >( std::stoul( hex.substr( k, 2 ), nullptr, 16 ) ) );
        const value_t cdb = c_reader( bytes ).parse( );
        log_t log;
        slalom_hooks_t h;
        h.user = &log;
        h.sound = []( void* u, const char* name, const float* p ) {
            auto& l = static_cast< log_t* >( u )->calls;
            unsigned pw[ 3 ];
            std::memcpy( pw, p, 12 );
            l.insert( l.end( ), { 0u, std::strstr( name, "slalom_miss" ) ? 0u : 1u, pw[ 0 ], pw[ 1 ], pw[ 2 ] } );
        };
        h.broadcast = []( void* u, const char* name ) {
            std::uint32_t x = 0x811C9DC5u;
            for ( const char* p = name; *p; ++p )
                x = ( x ^ static_cast< std::uint8_t >( *p ) ) * 0x1000193u;
            static_cast< log_t* >( u )->calls.insert( static_cast< log_t* >( u )->calls.end( ), { 1u, x, 0u, 0u, 0u } );
        };
        const auto index = [ & ]( const slalom_gate_t* g ) { return g ? static_cast< unsigned >( g - gates.data( ) ) : 0xFFFFFFFFu; };
        const auto snap = [ & ]( unsigned frame ) {
            ++snaps;
            bool bad = next( ) != index( c.current );
            bad = ( next( ) != index( c.recover ) ) || bad;
            unsigned pb, bb;
            std::memcpy( &pb, &c.penalty, 4 );
            std::memcpy( &bb, &c.boost_add, 4 );
            bad = ( next( ) != pb ) || bad;
            bad = ( next( ) != bb ) || bad;
            for ( unsigned k = 0; k < ng; ++k )
                bad = ( next( ) != index( gates[ k ].prev ) ) || bad;
            const auto word = [ & ]( int at ) { unsigned x; std::memcpy( &x, boat.data( ) + at, 4 ); return x; };
            bad = ( next( ) != word( 0x310 ) ) || bad;
            bad = ( next( ) != boat[ 0x344 ] ) || bad;
            for ( int k = 0; k < 16; ++k )
                bad = ( next( ) != word( 0x348 + 4 * k ) ) || bad;
            bad = ( next( ) != word( 0x4B0 ) ) || bad;
            bad = ( next( ) != word( 0x4B4 ) ) || bad;
            bad = ( next( ) != word( 0x4D0 ) ) || bad;
            for ( int k = 0; k < 16; ++k )
                bad = ( next( ) != word( 0x4DC + 4 * k ) ) || bad;
            const unsigned nc = next( );
            std::size_t at = 0;
            bad = ( log.calls.size( ) != 5u * nc ) || bad;
            for ( unsigned k = 0; k < nc; ++k )
            {
                const unsigned kind = next( ), value = next( ), has = next( );
                const unsigned p[ 3 ] = { next( ), next( ), next( ) };
                if ( at + 5 > log.calls.size( ) )
                {
                    bad = true;
                    continue;
                }
                bad = ( log.calls[ at ] != kind || log.calls[ at + 1 ] != value ) || bad;
                if ( has )
                    bad = ( log.calls[ at + 2 ] != p[ 0 ] || log.calls[ at + 3 ] != p[ 1 ] || log.calls[ at + 4 ] != p[ 2 ] ) || bad;
                sounds += kind == 0 ? 1 : 0;
                at += 5;
            }
            log.calls.clear( );
            if ( bad && ++failed <= 8 )
                LOG_ERROR( "slalom gates: vector %u frame %d differs from retail's (+0x73C %d, +0x740 %d, +0x310 0x%X)", v, static_cast< int >( frame ) - 1,
                           static_cast< int >( index( c.current ) ), static_cast< int >( index( c.recover ) ), word( 0x310 ) );
        };
        slalom_course_start( c, member( member( cdb, "VuDBAsset" ), "Slalom" ), cls, boat.data( ), world );
        snap( 0 );
        const unsigned nf = next( );
        float last[ 16 ];
        std::memcpy( last, world, 64 );
        for ( unsigned f = 0; f < nf; ++f )
        {
            const unsigned kind = next( );
            for ( float& x : world )
            {
                const unsigned b = next( );
                std::memcpy( &x, &b, 4 );
            }
            if ( kind == 1 )
                slalom_recovered( c, boat.data( ), world );
            else if ( kind == 0 )
            {
                std::memcpy( boat.data( ) + 0x4DC, last, 64 );   // VuGame's tick after slot 7 (the frame before)
                slalom_gates_tick( c, boat.data( ), world, h );
            }
            std::memcpy( last, world, 64 );
            snap( f + 1 );
        }
    }
    if ( static_cast< std::size_t >( w - g_golden_sg_words ) != g_golden_sg_count )
    {
        LOG_ERROR( "slalom gates: the words stream desynchronised (%d of %d read)", static_cast< int >( w - g_golden_sg_words ), static_cast< int >( g_golden_sg_count ) );
        return false;
    }
    if ( failed == 0 )
        LOG_SUCCESS( "slalom_course_start / slalom_gates_tick / slalom_recovered match VuSlalomGame's 0x48C660 / 0x48E6E0 / 0x48DB70 on %u courses, "
                     "%d snapshots (%d FMOD sounds), bit for bit",
                     nv, snaps, sounds );
    else
        LOG_ERROR( "the slalom gates FAILED on %d of %d snapshots", failed, snaps );
    return failed == 0;
}

// race_place_timetrial (world/start.hxx) against retail's VuTimeTrialGame slot 13 0x490CA0 run whole (oracle case
// timetrial_place: 0..2 game boats, 0..3 grid entries, ConstantDB TimeTrial InitialBoostEnergy as float / int / int64 /
// string / absent / broken). Compared: whether boat [0]'s transform got setWorldTransform (0x5344A0) and with which
// world (+0x50 after) and notify (the +0xC0 delegate), and every boat's +0x310 bits - one failure per differing vector.
inline bool verify_game_place( const unsigned* words, const char* const* texts, const char* mode, const char* label )
{
    int failed = 0;
    const unsigned* w = words;
    std::size_t s = 0;
    const auto next = [ & ] { return *w++; };
    const unsigned nv = next( );
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned nb = next( ), ng = next( );
        race_boat_manager_t m;
        float worlds[ 3 ][ 16 ];
        for ( auto& world : worlds )
            for ( float& x : world )
            {
                const unsigned b = next( );
                std::memcpy( &x, &b, 4 );
            }
        for ( unsigned k = 0; k < ng; ++k )
        {
            race_grid_entry_t g;
            g.name = "Grid" + std::to_string( k );
            std::memcpy( g.world, worlds[ k ], 64 );
            m.grid.push_back( g );
        }
        const unsigned want_boost[ 2 ] = { next( ), next( ) };
        const unsigned nc = next( );
        int want_t = -1, want_notify = 0;
        unsigned want_world[ 16 ] = { };
        for ( unsigned c = 0; c < nc; ++c )
        {
            want_t = static_cast< int >( next( ) );
            for ( unsigned& x : want_world )
                x = next( );
            want_notify = static_cast< int >( next( ) );
        }
        std::vector< std::uint8_t > images[ 2 ] = { std::vector< std::uint8_t >( 0x600, 0 ), std::vector< std::uint8_t >( 0x600, 0 ) };
        std::vector< std::uint8_t > trans[ 2 ] = { std::vector< std::uint8_t >( 0x100, 0 ), std::vector< std::uint8_t >( 0x100, 0 ) };
        std::vector< race_start_boat_t > boats;
        for ( unsigned k = 0; k < 2; ++k )
        {
            const unsigned fill = 0xCDCDCDCDu;
            std::memcpy( images[ k ].data( ) + 0x310, &fill, 4 );
            if ( k < nb )
            {
                race_start_boat_t b;
                b.boat = images[ k ].data( );
                b.transform = trans[ k ].data( );
                boats.push_back( b );
            }
        }
        const std::string hex = texts[ s++ ];
        std::vector< std::uint8_t > bytes;
        for ( std::size_t k = 0; k + 1 < hex.size( ); k += 2 )
            bytes.push_back( static_cast< std::uint8_t >( std::stoul( hex.substr( k, 2 ), nullptr, 16 ) ) );
        const value_t cdb = c_reader( bytes ).parse( );
        int changed = 0;
        race_start_hooks_t hooks;
        hooks.transform_changed = [ & ]( int ) { ++changed; };
        race_place_timetrial( boats, m, cdb, hooks, mode );
        bool same = changed == ( nc ? want_notify : 0 );
        if ( nc )
            same = same && want_t == 0 && std::memcmp( trans[ 0 ].data( ) + 0x50, want_world, 64 ) == 0;
        else
        {
            static const std::uint8_t zero[ 64 ] = { };
            same = same && std::memcmp( trans[ 0 ].data( ) + 0x50, zero, 64 ) == 0;
        }
        for ( unsigned k = 0; k < 2; ++k )
            same = same && std::memcmp( images[ k ].data( ) + 0x310, &want_boost[ k ], 4 ) == 0;
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "%s place vector %u (boats %u, grid %u, calls %u): differs from retail", mode, v, nb, ng, nc );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "race_place_timetrial matches %s on %u vectors (the grid placement, notify, +0x310)", label, nv );
    else
        LOG_ERROR( "the %s placement FAILED on %d of %u vectors", mode, failed, nv );
    return failed == 0;
}

// The same against VuSlalomGame slot 13 0x48E430 (case slalom_place: ConstantDB "Slalom").
inline bool verify_timetrial_place( ) { return verify_game_place( g_golden_ttp_words, g_golden_ttp_text, "TimeTrial", "VuTimeTrialGame slot 13 0x490CA0" ); }
inline bool verify_slalom_place( ) { return verify_game_place( g_golden_slp_words, g_golden_slp_text, "Slalom", "VuSlalomGame slot 13 0x48E430" ); }

// race_rules_cheat_finish (world/rules.hxx) against retail's OnCheatFinish 0x483190 -> 0x4848C0 (oracle case
// cheat_finish): 300 human lists of 0..4 boats - the six words of each boat after, and the AI manager's vt+0x30 calls.
inline bool verify_cheat_finish( )
{
    static const int k_at[ 6 ] = { 0x308, 0x32F, 0x334, 0x4AC, 0x4B0, 0x4B8 };
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_cheat_finish_count; ++k )
    {
        const golden_cheat_finish_t& want = g_golden_cheat_finish[ k ];
        std::vector< std::vector< std::uint8_t > > images( static_cast< std::size_t >( want.n ), std::vector< std::uint8_t >( 0x520, 0 ) );
        std::vector< std::uint8_t* > humans;
        for ( int b = 0; b < want.n; ++b )
        {
            std::uint8_t* img = images[ static_cast< std::size_t >( b ) ].data( );
            for ( int i = 0; i < 6; ++i )
            {
                if ( i == 1 || i == 2 )
                    img[ k_at[ i ] ] = static_cast< std::uint8_t >( want.in[ b ][ i ] );
                else
                    std::memcpy( img + k_at[ i ], &want.in[ b ][ i ], 4 );
            }
            humans.push_back( img );
        }
        std::vector< int > calls;
        race_rules_cheat_finish( humans, [ & ]( std::size_t h ) { calls.push_back( static_cast< int >( h ) ); } );
        bool same = static_cast< int >( calls.size( ) ) == want.calls;
        for ( std::size_t c = 0; same && c < calls.size( ); ++c )
            same = calls[ c ] == want.call[ c ];
        for ( int b = 0; b < want.n; ++b )
            for ( int i = 0; i < 6; ++i )
            {
                const std::uint8_t* img = images[ static_cast< std::size_t >( b ) ].data( );
                std::uint32_t w = 0;
                if ( i == 1 || i == 2 )
                    w = img[ k_at[ i ] ];
                else
                    std::memcpy( &w, img + k_at[ i ], 4 );
                same = same && w == want.out[ b ][ i ];
            }
        if ( !same && ++failed <= 6 )
            LOG_ERROR( "cheat finish %d: %d boats - a word or the vt+0x30 calls differ", static_cast< int >( k ), want.n );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "race_rules_cheat_finish matches VuGame's OnCheatFinish 0x4848C0 on %d human lists, bit for bit",
                     static_cast< int >( g_golden_cheat_finish_count ) );
    else
        LOG_ERROR( "the cheat finish FAILED on %d of %d lists", failed, static_cast< int >( g_golden_cheat_finish_count ) );
    return failed == 0;
}

// rigid_kinematic_step (world/session.hxx) against retail's VuRigidBodyComponent kinematic step 0x544C50 (oracle case
// kinematic_step): 200 seeded btRigidBody images - the body's 0x2D4 bytes after (retail's size: our image is larger) and
// whether updateSingleAabb ran (retail: +0x100 and the record non-null, +0x104 set).
inline bool verify_kinematic_step( )
{
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_kinematic_step_count; ++k )
    {
        const golden_kinematic_step_t& want = g_golden_kinematic_step[ k ];
        static vu_rigid_body_t b;
        std::memset( &b, 0, sizeof( b ) );
        std::memcpy( &b, want.body, 0x2D4 );
        float rec[ 24 ];
        std::memcpy( rec, want.rec, sizeof( rec ) );
        const bool run = want.has_body && want.has_rec;
        if ( run )
            rigid_kinematic_step( b.body, rec, rec + 16, rec + 20 );
        const int aabb = run && want.in_world ? 1 : 0;
        if ( ( std::memcmp( &b, want.out, 0x2D4 ) != 0 || aabb != want.aabb ) && ++failed <= 6 )
        {
            int first = -1;
            for ( int i = 0; i < 0x2D4 / 4 && first < 0; ++i )
                if ( std::memcmp( reinterpret_cast< const std::uint8_t* >( &b ) + 4 * i, &want.out[ i ], 4 ) != 0 )
                    first = 4 * i;
            LOG_ERROR( "kinematic step %d: first differing body word +0x%X, aabb %d (retail %d)", static_cast< int >( k ), first, aabb, want.aabb );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "rigid_kinematic_step matches VuRigidBodyComponent's kinematic step 0x544C50 on %d bodies (the 0x2D4 image, the aabb update)",
                     static_cast< int >( g_golden_kinematic_step_count ) );
    else
        LOG_ERROR( "the kinematic step FAILED on %d of %d bodies", failed, static_cast< int >( g_golden_kinematic_step_count ) );
    return failed == 0;
}

// dynamic_prop_load_properties on VuBreakableDynamicGamePropEntity (world/dynprop.hxx: the base nodes + "Breakable Pfx
// Name") against retail's VuEntity::load 0x5009D0 (ctor 0x433160) on its entities in the templates / projects and two
// hand-built ones (oracle case breakable_dynamic_properties): 30 words and the Splash / Breakable Pfx Names.
inline bool verify_breakable_dynamic_properties( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_breakable_dynamic_properties_document );
    static std::vector< value_t > documents;
    documents.push_back( c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_breakable_dynamic_properties_document_bytes ) ).parse( ) );
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world = race_entity_world( registry, nullptr );
    const std::unique_ptr< entity_t > tree = entity_load_project( member( documents.back( ), "VuProject" ), "Data/Projects/Check.vuprj", world );
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_breakable_dynamic_properties_count; ++k )
    {
        const golden_entity_targets_t& want = g_golden_breakable_dynamic_properties[ k ];
        const entity_t* e = nullptr;
        if ( tree )
            for ( auto& child : tree->m_children )
                if ( child->m_name == want.m_child )
                    e = child.get( );
        const dynamic_prop_state_t* s = e && e->m_state && e->m_type == "VuBreakableDynamicGamePropEntity"
                                            ? static_cast< const dynamic_prop_state_t* >( e->m_state.get( ) ) : nullptr;
        if ( !s || !s->breakable || want.m_count != 30 )
        {
            LOG_ERROR( "breakable dynamic properties: %s (%s) missing, without state or not 30 words", want.m_child, want.m_source );
            ++failed;
            continue;
        }
        const dynamic_prop_t& v = s->value;
        std::uint32_t got[ 30 ];
        int n = 0;
        const auto f = [ & ]( float x ) { std::memcpy( &got[ n++ ], &x, 4 ); };
        got[ n++ ] = v.visible;
        f( v.mass );
        f( v.centre[ 0 ] ), f( v.centre[ 1 ] ), f( v.centre[ 2 ] );
        f( v.fluids_radius ), f( v.fluids_density ), f( v.fluids_drag ), f( v.fluids_lift );
        f( v.hydro_centre[ 0 ] ), f( v.hydro_centre[ 1 ] ), f( v.hydro_centre[ 2 ] );
        f( v.splash_speed ), f( v.linear_damping ), f( v.angular_damping );
        got[ n++ ] = v.linear_anchor;
        got[ n++ ] = v.angular_anchor;
        f( v.anchor_spring ), f( v.anchor_damping );
        got[ n++ ] = v.hard_hit;
        f( v.threshold_mph );
        f( v.shake_magnitude ), f( v.shake_duration ), f( v.shake_falloff ), f( v.shake_frequency );
        f( v.rumble_low ), f( v.rumble_high );
        got[ n++ ] = v.throw_ai;
        got[ n++ ] = v.initially_active;
        got[ n++ ] = v.collide_static;
        bool bad = v.splash_pfx != want.m_texts[ 0 ] || v.breakable_pfx != want.m_texts[ 1 ];
        for ( int w = 0; w < 30; ++w )
            bad = bad || got[ w ] != want.m_values[ w ];
        if ( bad && ++failed <= 8 )
            LOG_ERROR( "breakable dynamic properties: %s (%s) differ from retail's", want.m_child, want.m_source );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "dynamic_prop_load_properties matches VuBreakableDynamicGamePropEntity's loads on %d entities (30 words + 2 names), bit for bit",
                     static_cast< int >( g_golden_breakable_dynamic_properties_count ) );
    else
        LOG_ERROR( "the breakable dynamic properties FAILED on %d of %d entities", failed,
                   static_cast< int >( g_golden_breakable_dynamic_properties_count ) );
    return failed == 0;
}

// vugame_release (world/rules.hxx) against retail's VuGame release 0x482D50 with VuRaceGame's slot 6 0x489A10 run (oracle
// case vugame_release): 60 games - which machines have a current state, the humans, the boats' +0x14 - compared as the
// whole call log (the analytics container canonical, as race_results_canonical prints it); the cameras retail zeroes.
inline bool verify_vugame_release( )
{
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_vugame_release_count; ++k )
    {
        const golden_vugame_release_t& want = g_golden_vugame_release[ k ];
        race_rules_t r;
        race_rules_construct( r );
        c_fsm* m[ 3 ] = { &r.event, &r.split, &r.fsm };
        for ( int i = 0; i < 3; ++i )
            m[ i ]->m_current = want.states[ i ] ? 0 : -1;
        std::vector< std::uint8_t > human( 0x520, 0 );
        human[ 0x32F ] = static_cast< std::uint8_t >( want.finished );
        std::memcpy( human.data( ) + 0x4B4, &want.time, 4 );
        for ( int i = 0; i < want.humans; ++i )
            r.humans.push_back( human.data( ) );
        const std::vector< std::uint32_t > flags( want.flags, want.flags + want.nb );
        std::string log;
        const auto add = [ &log ]( const std::string& e ) { log += ( log.empty( ) ? "" : "|" ) + e; };
        vugame_release_hooks_t h;
        h.exit = [ & ]( int machine ) { add( "exit " + std::to_string( machine ) ); };
        h.ai = [ & ]( int slot ) { add( slot == 8 ? "ai20" : "ai24" ); };
        h.analytics = [ & ]( const char* name, const value_t& data ) { add( std::string( "analytics " ) + name + " " + race_results_canonical( data ) ); };
        h.release = [ & ]( std::size_t b ) { add( "release " + std::to_string( b ) ); };
        h.call = [ & ]( const char* what ) {
            const std::string w( what );
            if ( w == "collision" )
                add( "collision 1" );
            else if ( w == "listener" )
                add( "listener 1 1" );
            else if ( w != "cameras" )
                add( w );
        };
        vugame_release( r, flags, h );
        if ( log != want.log && ++failed <= 4 )
            LOG_ERROR( "vugame release %d:\n ours   %s\n retail %s", static_cast< int >( k ), log.c_str( ), want.log );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "vugame_release matches VuGame's release 0x482D50 (VuRaceGame slot 6 0x489A10) on %d games: the whole call log",
                     static_cast< int >( g_golden_vugame_release_count ) );
    else
        LOG_ERROR( "the VuGame release FAILED on %d of %d games", failed, static_cast< int >( g_golden_vugame_release_count ) );
    return failed == 0;
}

// rigid_body_destroy (world/bodies.hxx) against retail's VuRigidBody dtor 0x50FAC0(0) -> 0x50FAF0 (oracle case
// rigid_body_dtor): 64 seeded images - the five words after, btAlignedFree's call, gNumAlignedFree.
inline bool verify_rigid_body_dtor( )
{
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_rigid_body_dtor_count; ++k )
    {
        const golden_rigid_body_dtor_t& want = g_golden_rigid_body_dtor[ k ];
        std::uint8_t img[ 0x2D4 ] = { };
        std::memcpy( img + 0x1FC, want.in, 12 );
        img[ 0x208 ] = static_cast< std::uint8_t >( want.in[ 3 ] );
        std::uint32_t counter = 0, freed = 0;
        int frees = 0;
        rigid_body_destroy( img, counter, [ & ]( std::uint32_t p ) { ++frees, freed = p; } );
        std::uint32_t out[ 5 ];
        std::memcpy( out, img, 4 );
        std::memcpy( out + 1, img + 0x1FC, 12 );
        out[ 4 ] = img[ 0x208 ];
        if ( ( std::memcmp( out, want.out, sizeof( out ) ) != 0 || frees != want.frees || freed != want.freed || counter != want.counter ) && ++failed <= 6 )
            LOG_ERROR( "rigid body dtor %d: a word, the free or the counter differs", static_cast< int >( k ) );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "rigid_body_destroy matches VuRigidBody's dtor 0x50FAF0 on %d bodies", static_cast< int >( g_golden_rigid_body_dtor_count ) );
    else
        LOG_ERROR( "the rigid body dtor FAILED on %d of %d bodies", failed, static_cast< int >( g_golden_rigid_body_dtor_count ) );
    return failed == 0;
}

// VuSubstituteAssetEntity's state (world/emitters.hxx: Asset Type, and the postLoad's two names) against retail's
// VuEntity::load 0x5009D0 (ctor 0x5F48A0, postLoad 0x5F4B70 -> 0x5F4C10) on the levels' / projects' entities and three
// hand-built (oracle case substitute_asset_properties; the factory's createAsset answered null - the names are compared).
inline bool verify_substitute_asset_properties( )
{
    const unsigned char* raw = reinterpret_cast< const unsigned char* >( g_golden_substitute_asset_document );
    static std::vector< value_t > documents;
    documents.push_back( c_reader( std::vector< std::uint8_t >( raw, raw + g_golden_substitute_asset_document_bytes ) ).parse( ) );
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world = race_entity_world( registry, nullptr );
    const std::unique_ptr< entity_t > tree = entity_load_project( member( documents.back( ), "VuProject" ), "Data/Projects/Check.vuprj", world );
    int failed = 0;
    for ( std::size_t k = 0; k < g_golden_substitute_asset_count; ++k )
    {
        const golden_substitute_asset_t& want = g_golden_substitute_asset[ k ];
        const entity_t* e = nullptr;
        if ( tree )
            for ( auto& child : tree->m_children )
                if ( child->m_name == want.child )
                    e = child.get( );
        const substitute_asset_state_t* s = e && e->m_state && e->m_type == "VuSubstituteAssetEntity"
                                                ? static_cast< const substitute_asset_state_t* >( e->m_state.get( ) ) : nullptr;
        if ( ( !s || s->type != want.names[ 0 ] || s->name != want.names[ 1 ] || s->subst != want.names[ 2 ] ) && ++failed <= 6 )
            LOG_ERROR( "substitute asset %s (%s): '%s' / '%s' / '%s', retail '%s' / '%s' / '%s'", want.child, want.source, s ? s->type.c_str( ) : "-",
                       s ? s->name.c_str( ) : "-", s ? s->subst.c_str( ) : "-", want.names[ 0 ], want.names[ 1 ], want.names[ 2 ] );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "the VuSubstituteAssetEntity state matches retail's loads (0x5F48A0 / 0x5F4B70) on %d entities: its three names",
                     static_cast< int >( g_golden_substitute_asset_count ) );
    else
        LOG_ERROR( "the substitute asset properties FAILED on %d of %d entities", failed, static_cast< int >( g_golden_substitute_asset_count ) );
    return failed == 0;
}

// world/slalom.hxx slalom_draw_gates against retail's 0x48E680 + 0x440050 (case slalom_gate_draw): per vector the gates
// drawn in order, which model instance (Model / Reflection Model Asset), the 16 matrix words; a ref whose type chain
// misses VuSlalomGateEntity (kind 2) or an unconnected one ends the walk (`next` null here, as the session builds it).
inline bool verify_slalom_gate_draw( )
{
    const unsigned* p = g_golden_sgd_words;
    const unsigned nv = *p++;
    int failed = 0, draws = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        const unsigned ng = *p++;
        const std::int32_t current = static_cast< std::int32_t >( *p++ );
        const bool reflection = *p++ != 0;
        std::vector< slalom_gate_t > gates( ng );
        std::vector< std::array< float, 16 > > worlds( ng );
        for ( unsigned k = 0; k < ng; ++k )
        {
            std::memcpy( &gates[ k ].diameter, p, 4 );
            const bool linked = p[ 1 ] != 0 && p[ 2 ] != 2;
            std::memcpy( worlds[ k ].data( ), p + 3, 64 );
            gates[ k ].world = worlds[ k ].data( );
            gates[ k ].model = "Model" + std::to_string( k );
            gates[ k ].reflection = "Reflection" + std::to_string( k );
            gates[ k ].next = linked && k + 1 < ng ? &gates[ k + 1 ] : nullptr;
            p += 19;
        }
        slalom_course_t c;
        c.current = current >= 0 ? &gates[ static_cast< std::size_t >( current ) ] : nullptr;
        slalom_gate_draw_t got[ 8 ];
        const int n = slalom_draw_gates( c, reflection, got );
        const unsigned want = *p++;
        bool bad = static_cast< unsigned >( n ) != want;
        for ( unsigned d = 0; d < want; ++d, p += 18 )
        {
            if ( static_cast< int >( d ) >= n )
                continue;
            const slalom_gate_t* g = got[ d ].gate;
            const unsigned k = static_cast< unsigned >( g - gates.data( ) );
            const std::string name = ( p[ 1 ] ? "Reflection" : "Model" ) + std::to_string( p[ 0 ] );
            if ( k != p[ 0 ] || *got[ d ].model != name || std::memcmp( got[ d ].matrix, p + 2, 64 ) != 0 )
                bad = true;
        }
        p += 2 * ng;   // +0xC0 / +0xE0 after: the instances' own words (the renderer's), not modelled
        draws += static_cast< int >( want );
        if ( bad && ++failed <= 6 )
            LOG_ERROR( "slalom gate draw vector %u: %d draws (retail %u) or a gate / instance / matrix differs", v, n, want );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "slalom_draw_gates matches retail's VuSlalomGame draw 0x48E680 + the gate draw 0x440050 on %u vectors, %d draws "
                     "(gate, instance, the 16 matrix words)", nv, draws );
    else
        LOG_ERROR( "the slalom gate draw FAILED on %d of %u vectors", failed, nv );
    return failed == 0;
}

// The boat pfx controller's draw 0x419CE0 (boat effects.hxx boat_pfx_draw_systems) against retail (case boat_pfx_draw):
// hand-built controllers, the systems whose slot 14 it called, in order.
inline bool verify_boat_pfx_draw( )
{
    const unsigned* p = g_golden_bpd_words;
    const unsigned nv = *p++;
    int failed = 0, calls = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        boat_pfx_t c;
        c.on[ 2 ] = static_cast< std::uint8_t >( *p++ );
        const std::uint8_t b46b = static_cast< std::uint8_t >( *p++ );
        const bool reflection = *p++ != 0;
        const std::int32_t extra = static_cast< std::int32_t >( *p++ );
        c.extra = extra >= 0 ? 0x1000u + static_cast< std::uint32_t >( extra ) : 0u;
        for ( int l = 0; l < 7; ++l )
        {
            const unsigned n = *p++;
            for ( unsigned k = 0; k < n; ++k )
            {
                boat_pfx_record_t r;
                r.system = 0x1000u + *p++;
                c.list[ l ].push_back( r );
            }
        }
        std::vector< std::uint32_t > got;
        boat_pfx_draw_systems( c, b46b, reflection, got );
        const unsigned want = *p++;
        bool bad = got.size( ) != want;
        for ( unsigned k = 0; k < want; ++k, ++p )
            if ( k < got.size( ) && got[ k ] != 0x1000u + *p )
                bad = true;
        calls += static_cast< int >( want );
        if ( bad && ++failed <= 6 )
            LOG_ERROR( "boat pfx draw vector %u: %d systems (retail %u) or their order differs", v, static_cast< int >( got.size( ) ), want );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "boat_pfx_draw_systems matches retail's 0x419CE0 + 0x41ACC0 on %u controllers, %d slot 14 calls in order", nv, calls );
    else
        LOG_ERROR( "the boat pfx draw FAILED on %d of %u controllers", failed, nv );
    return failed == 0;
}

// VuGhostManager (world/ghost.hxx) against retail (case ghost_manager): the start, the per-step tick, the frames and the
// counters after; the writer's blob (over a buffer holding `pre` bytes); the reader on the blob good and mutated (answer,
// strings, count, capacity, frames); the clear.
inline bool verify_ghost_manager( )
{
    const unsigned* p = g_golden_gmd_words;
    const unsigned nv = *p++;
    auto text = [ & ]( ) {
        const unsigned n = *p++;
        std::string t;
        for ( unsigned k = 0; k < n; ++k )
            t.push_back( static_cast< char >( *p++ ) );
        return t;
    };
    auto bytes = [ & ]( ) {
        const unsigned n = *p++;
        std::vector< std::uint8_t > t;
        for ( unsigned k = 0; k < n; ++k )
            t.push_back( static_cast< std::uint8_t >( *p++ ) );
        return t;
    };
    int failed = 0, frames = 0, reads = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        bool bad = false;
        const std::string sb = text( ), ss = text( ), sd = text( );
        const unsigned preset = *p++;
        const unsigned steps = *p++;
        ghost_manager_t m;
        ghost_start( m, sb, ss, sd );
        if ( preset )
            m.clock = 299.5f;
        std::vector< std::uint8_t > boat( 0x600 ), transform( 0x100 ), debris( 0x100 );
        ghost_boat_t b{ boat.data( ), transform.data( ), debris.data( ), 0.0f };
        for ( unsigned k = 0; k < steps; ++k, p += 14 )
        {
            std::memcpy( transform.data( ) + 0x80, p + 1, 16 );
            std::memcpy( transform.data( ) + 0xA0, p + 5, 16 );
            std::memcpy( boat.data( ) + 0x310, p + 9, 4 );
            boat[ 0x32C ] = static_cast< std::uint8_t >( p[ 10 ] );
            boat[ 0x464 ] = static_cast< std::uint8_t >( p[ 11 ] );
            debris[ 0x94 ] = static_cast< std::uint8_t >( p[ 12 ] );
            std::memcpy( &b.throttle, p + 13, 4 );
            float dt;
            std::memcpy( &dt, p, 4 );
            ghost_tick( m, b, dt );
        }
        const std::int32_t count = static_cast< std::int32_t >( p[ 0 ] ), capacity = static_cast< std::int32_t >( p[ 1 ] );
        bad = bad || m.record.count != count || m.record.capacity != capacity || std::memcmp( &m.clock, p + 2, 4 ) != 0 || std::memcmp( &m.accumulator, p + 3, 4 ) != 0;
        p += 5;
        const unsigned nf = *p++;
        if ( nf == 6u * static_cast< unsigned >( m.record.count ) && nf != 0u )
            bad = bad || std::memcmp( m.record.frames.data( ), p, 4u * nf ) != 0;
        p += nf;
        frames += static_cast< int >( nf / 6u );
        const unsigned pre = *p++;
        const std::vector< std::uint8_t > blob = bytes( );
        std::vector< std::uint8_t > out;
        for ( unsigned k = 0; k < pre; ++k )
            out.push_back( static_cast< std::uint8_t >( k ) );
        ghost_write( m.record, out );
        bad = bad || out != blob;
        const unsigned nr = *p++;
        for ( unsigned r = 0; r < nr; ++r, ++reads )
        {
            const std::vector< std::uint8_t > in = bytes( );
            const bool answer = *p++ != 0;
            const std::string rb = text( ), rs = text( ), rd = text( );
            ghost_data_t d;
            d.skin = "stale skin";
            bool overrun = false;
            const bool got = ghost_read( d, in.data( ), in.size( ), &overrun );
            const std::int32_t c = static_cast< std::int32_t >( p[ 0 ] ), cap = static_cast< std::int32_t >( p[ 1 ] );
            p += 2;
            const unsigned rf = *p++;
            bad = bad || overrun || got != answer || d.boat != rb || d.skin != rs || d.driver != rd || d.count != c || d.capacity != cap;
            if ( rf == 6u * static_cast< unsigned >( d.count ) && rf != 0u )
                bad = bad || std::memcmp( d.frames.data( ), p, 4u * rf ) != 0;
            p += rf;
        }
        ghost_clear( m );
        const std::string cb = text( ), cs = text( ), cd = text( );
        bad = bad || m.record.boat != cb || m.record.skin != cs || m.record.driver != cd || m.record.count != static_cast< std::int32_t >( p[ 0 ] ) ||
              m.record.capacity != static_cast< std::int32_t >( p[ 1 ] );
        p += 2;
        if ( bad && ++failed <= 6 )
            LOG_ERROR( "ghost manager run %u: a frame, counter, blob or read differs from retail", v );
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuGhostManager (0x4AF520 / 0x4AF9C0 / 0x4AFA30 / 0x4AFE10 / 0x4AFB90 / 0x4AFCE0 / 0x4AF490) matches retail on %u runs: %d frames, %d blob reads", nv, frames, reads );
    else
        LOG_ERROR( "the ghost manager FAILED on %d of %u runs", failed, nv );
    return failed == 0;
}

// VuTireTrackManager (world/tiretrack.hxx) against retail (case tire_track): the init on the real SurfaceTableDB / SurfaceDB
// (pool counts, list ends and links, every skin), 0x5C5190 on synthetic objects, and the tick over 5 steps of hand-built lists
// (the three lists in order, the counts and tails, each live track's bounds, chain and alphas).
inline bool verify_tire_track( )
{
    const unsigned* p = g_golden_ttm_words;
    auto bytes = [ & ]( ) {
        const unsigned n = *p++;
        std::vector< std::uint8_t > t;
        for ( unsigned k = 0; k < n; ++k )
            t.push_back( static_cast< std::uint8_t >( *p++ ) );
        return t;
    };
    auto list = [ & ]( ) {
        const unsigned n = *p++;
        std::vector< unsigned > t( p, p + n );
        p += n;
        return t;
    };
    auto word = []( float f ) {
        std::uint32_t u;
        std::memcpy( &u, &f, 4 );
        return u;
    };
    auto skin_ok = [ & ]( const tire_skin_t& s, const unsigned* g ) {
        return ( s.present ? 1u : 0u ) == g[ 0 ] && word( s.tex0 ) == g[ 1 ] && word( s.tex1 ) == g[ 2 ] && word( s.ambient[ 0 ] ) == g[ 3 ] &&
               word( s.ambient[ 1 ] ) == g[ 4 ] && word( s.ambient[ 2 ] ) == g[ 5 ] && word( s.diffuse[ 0 ] ) == g[ 6 ] &&
               word( s.diffuse[ 1 ] ) == g[ 7 ] && word( s.diffuse[ 2 ] ) == g[ 8 ];
    };
    int failed = 0;
    // the init
    {
        const std::vector< std::uint8_t > sdb = bytes( ), tdb = bytes( );
        surface_table_t st;
        const value_t sv = c_reader( sdb ).parse( );
        const value_t tv = c_reader( tdb ).parse( );
        surface_table_build( st, &sv );
        tire_track_manager_t m;
        tire_track_init( m, st, tv );
        bool bad = *p++ != 1u;
        const unsigned w[ 6 ] = { static_cast< unsigned >( m.track_pool.size( ) ), static_cast< unsigned >( m.segment_pool.size( ) ), static_cast< unsigned >( m.surfaces ),
                                  static_cast< unsigned >( m.free_count ), static_cast< unsigned >( m.active_count ), static_cast< unsigned >( m.free_segment_count ) };
        for ( unsigned x : w )
            bad = *p++ != x || bad;
        auto ti = [ & ]( const tire_track_t* t ) { return t ? static_cast< unsigned >( t - m.track_pool.data( ) ) : ~0u; };
        auto si = [ & ]( const tire_segment_t* s ) { return s ? static_cast< unsigned >( s - m.segment_pool.data( ) ) : ~0u; };
        bad = *p++ != ti( m.free_head ) || bad;
        bad = *p++ != ti( m.free_tail ) || bad;
        bad = *p++ != si( m.free_segment_head ) || bad;
        bad = *p++ != si( m.free_segment_tail ) || bad;
        for ( unsigned k : { 0u, 1u, 62u, 63u } )
        {
            bad = *p++ != ti( m.track_pool[ k ].next ) || bad;
            bad = *p++ != ti( m.track_pool[ k ].prev ) || bad;
        }
        for ( unsigned k : { 0u, 1u, 1022u, 1023u } )
        {
            bad = *p++ != si( m.segment_pool[ k ].next ) || bad;
            bad = *p++ != si( m.segment_pool[ k ].prev ) || bad;
        }
        ++p;   // batch_on: the render batch's register (a device hook)
        const unsigned n = *p++;
        bad = n != m.skins.size( ) || bad;
        for ( unsigned k = 0; k < n; ++k, p += 9 )
            bad = !skin_ok( m.skins[ k ], p ) || bad;
        failed += bad ? 1 : 0;
    }
    const unsigned ns = *p++;
    for ( unsigned k = 0; k < ns; ++k, p += 9 )
    {
        const std::vector< std::uint8_t > blob = bytes( );
        tire_skin_t s;
        tire_skin_load( s, c_reader( blob ).parse( ) );
        if ( !skin_ok( s, p ) )
        {
            ++failed;
            if ( failed < 4 )
                LOG_ERROR( "tire skin %u: present %d / %u, tex %f %f / %x %x, ambient %x %x %x / %x %x %x", k, s.present ? 1 : 0, p[ 0 ], s.tex0, s.tex1, p[ 1 ], p[ 2 ],
                           word( s.ambient[ 0 ] ), word( s.ambient[ 1 ] ), word( s.ambient[ 2 ] ), p[ 3 ], p[ 4 ], p[ 5 ] );
        }
    }
    const unsigned nv = *p++;
    int steps = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        bool bad = false;
        const unsigned nt = *p++;
        tire_track_manager_t m;
        m.track_pool.assign( 8, tire_track_t{ } );
        m.segment_pool.assign( 48, tire_segment_t{ } );
        auto ti = [ & ]( const tire_track_t* t ) { return t ? static_cast< unsigned >( t - m.track_pool.data( ) ) : ~0u; };
        auto si = [ & ]( const tire_segment_t* s ) { return s ? static_cast< unsigned >( s - m.segment_pool.data( ) ) : ~0u; };
        unsigned next_segment = 0;
        for ( unsigned t = 0; t < nt; ++t )
        {
            tire_track_t& tr = m.track_pool[ t ];
            std::memcpy( &tr.width, p++, 4 );
            std::memcpy( &tr.fade, p++, 4 );
            tr.ending = *p++ != 0u;
            const unsigned n = *p++;
            for ( unsigned k = 0; k < n; ++k, p += 4 )
            {
                tire_segment_t& s = m.segment_pool[ next_segment++ ];
                std::memcpy( s.position, p, 12 );
                std::memcpy( &s.alpha, p + 3, 4 );
                s.next = nullptr;
                s.prev = tr.tail;
                ( tr.tail ? tr.tail->next : tr.head ) = &s;
                tr.tail = &s;
                ++tr.count;
            }
            tire_track_t* tail = m.active_tail;
            tr.prev = tail;
            ( tail ? tail->next : m.active_head ) = &tr;
            m.active_tail = &tr;
            ++m.active_count;
        }
        for ( unsigned t = nt; t < 8u; ++t )
            tire_push( &m.track_pool[ t ], m.free_head, m.free_tail, m.free_count );
        for ( unsigned k : list( ) )
            tire_push( &m.segment_pool[ k ], m.free_segment_head, m.free_segment_tail, m.free_segment_count );
        const unsigned ticks = *p++;
        for ( unsigned k = 0; k < ticks; ++k, ++steps )
        {
            float dt;
            std::memcpy( &dt, p++, 4 );
            tire_track_tick( m, dt );
            const std::vector< unsigned > active = list( ), free_t = list( ), free_s = list( );
            std::size_t i = 0;
            for ( const tire_track_t* t = m.active_head; t; t = t->next, ++i )
                bad = i >= active.size( ) || ti( t ) != active[ i ] || bad;
            bad = i != active.size( ) || bad;
            i = 0;
            for ( const tire_track_t* t = m.free_head; t; t = t->next, ++i )
                bad = i >= free_t.size( ) || ti( t ) != free_t[ i ] || bad;
            bad = i != free_t.size( ) || bad;
            i = 0;
            for ( const tire_segment_t* s = m.free_segment_head; s; s = s->next, ++i )
                bad = i >= free_s.size( ) || si( s ) != free_s[ i ] || bad;
            bad = i != free_s.size( ) || bad;
            bad = *p++ != static_cast< unsigned >( m.free_count ) || bad;
            bad = *p++ != static_cast< unsigned >( m.active_count ) || bad;
            bad = *p++ != static_cast< unsigned >( m.free_segment_count ) || bad;
            bad = *p++ != ti( m.active_tail ) || bad;
            bad = *p++ != ti( m.free_tail ) || bad;
            bad = *p++ != si( m.free_segment_tail ) || bad;
            for ( unsigned a : active )
            {
                const tire_track_t& t = m.track_pool[ a < 8u ? a : 0u ];
                bad = *p++ != static_cast< unsigned >( t.count ) || bad;
                bad = *p++ != si( t.tail ) || bad;
                for ( int q = 0; q < 3; ++q )
                    bad = p[ q ] != word( t.aabb_min[ q ] ) || p[ 4 + q ] != word( t.aabb_max[ q ] ) || bad;
                p += 8;   // +0x34 / +0x44: a stale stack word in retail (the reset's second movq) - not compared
                const std::vector< unsigned > seg = list( ), alpha = list( );
                std::size_t j = 0;
                for ( const tire_segment_t* s = t.head; s; s = s->next, ++j )
                    bad = j >= seg.size( ) || si( s ) != seg[ j ] || word( s->alpha ) != alpha[ j ] || bad;
                bad = j != seg.size( ) || bad;
            }
        }
        if ( bad )
        {
            ++failed;
            if ( failed < 4 )
                LOG_ERROR( "tire run %u failed", v );
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuTireTrackManager (init 0x5C40C0's CPU half, 0x5C46D0, 0x5C5190, tick 0x5C4940, 0x5C4860) matches retail: the real tables, %u skins, %u runs over %d steps", ns, nv, steps );
    else
        LOG_ERROR( "the tire track manager FAILED on %d (init + %u skins + %u runs)", failed, ns, nv );
    return failed == 0;
}

// The pause menu's gate (world/pause.hxx) against retail (case pause_gate): OnSetAllowPause 0x4BB900 on every record shape, then
// OnPauseActionGame 0x4BB660 on every pre-state: the byte +0x31 (zero or not: its readers test it) and the VuString +0x38.
inline bool verify_pause_gate( )
{
    const unsigned* p = g_golden_pgt_words;
    const unsigned nv = *p++;
    int failed = 0;
    for ( unsigned v = 0; v < nv; ++v )
    {
        ui_params_t params;
        params.m_size = static_cast< int >( *p++ );
        const unsigned n = *p++;
        for ( unsigned k = 0; k < n; ++k )
            params.m_bytes[ k ] = static_cast< std::uint8_t >( *p++ );
        pause_menu_t m;
        m.paused = *p++ != 0u;
        m.allow = *p++ != 0u;
        pause_on_set_allow( m, params );
        const bool allow = *p++ != 0u;
        pause_on_pause( m );
        const unsigned nn = *p++;
        std::string next;
        for ( unsigned k = 0; k < nn; ++k )
            next.push_back( static_cast< char >( *p++ ) );
        if ( m.allow != allow || m.next != next )
            ++failed;
    }
    if ( failed == 0 )
        LOG_SUCCESS( "the pause menu's gate (OnSetAllowPause 0x4BB900, OnPauseActionGame 0x4BB660) matches retail on %u vectors", nv );
    else
        LOG_ERROR( "the pause menu's gate FAILED on %d of %u vectors", failed, nv );
    return failed == 0;
}

// VuGame's results / awards names (world/awards.hxx) against retail (case game_awards): slot 11 of the four games; Awards' enter
// 0x484320 whole with slots 9 and 10: the ordered log of every service call with its arguments, and +0x88.
inline bool verify_game_awards( )
{
    int failed = 0;
    for ( unsigned k = 0; k < g_golden_gaw_name_count; ++k )
    {
        const std::string mode = g_golden_gaw_names[ 2 * k ];
        if ( mode != g_golden_gaw_names[ 2 * k + 1 ] && std::string( vugame_results_project( mode, k % 2 != 0 ) ) != g_golden_gaw_names[ 2 * k + 1 ] )
            ++failed;
    }
    for ( unsigned v = 0; v < g_golden_gaw_count; ++v )
    {
        const char* const* t = g_golden_gaw_text + 5 * v;
        const unsigned* w = g_golden_gaw_words + 6 * v;
        const std::string mode = t[ 0 ], track = t[ 1 ];
        vugame_award_boat_t b;
        b.name = t[ 2 ];
        b.boat_class = t[ 3 ];
        b.tier = static_cast< std::int32_t >( w[ 0 ] );
        std::string log;
        const auto add = [ & ]( const std::string& e ) { log += log.empty( ) ? e : ";" + e; };
        const auto joined = []( const std::vector< std::string >& n ) {
            std::string out;
            for ( std::size_t k = 0; k < n.size( ); ++k )
                out += ( k ? "," : "" ) + n[ k ];
            return out;
        };
        vugame_award_hooks_t h;
        h.hidden_found = [ & ]( const std::string& x ) { add( "hidden|" + x ); return w[ 2 ] != 0u; };
        h.progress = [ & ]( const std::string& x ) { add( "progress|" + x ); };
        h.unlock_value = [ & ]( std::uint32_t slot, const std::string& c, std::int32_t tier ) {
            add( "slot9|" + std::to_string( slot ) + "|" + c + "|" + std::to_string( tier ) );
            return static_cast< std::int32_t >( w[ 3 ] );
        };
        h.unlock_show = [ & ]( std::int32_t value, const std::vector< std::string >& n ) {
            add( "show|" + std::to_string( value ) + "|" + joined( n ) );
            return w[ 4 ] != 0u;
        };
        h.award_open = [ & ]( const char* project, std::int32_t tier, const std::string& c, std::int32_t value, const std::vector< std::string >& n ) {
            add( std::string( "screen|" ) + project );
            add( "open|" + std::to_string( tier ) + "|" + c + "|" + std::to_string( value ) + "|" + joined( n ) );
            return true;
        };
        const bool opened = vugame_awards_enter( mode, track, w[ 1 ] != 0u, b, h );
        if ( opened != ( w[ 5 ] != 0u ) || log != t[ 4 ] )
        {
            if ( failed < 2 )
                LOG_ERROR( "game_awards run %u: %s vs %s", v, log.c_str( ), t[ 4 ] );
            ++failed;
        }
    }
    if ( failed == 0 )
        LOG_SUCCESS( "VuGame's results / awards (slot 11 x4, Awards' enter 0x484320, slots 9 / 10 x4, 0x4830B0) match retail on %u names, %u runs", g_golden_gaw_name_count, g_golden_gaw_count );
    else
        LOG_ERROR( "VuGame's awards FAILED on %d (of %u names + %u runs)", failed, g_golden_gaw_name_count, g_golden_gaw_count );
    return failed == 0;
}

// The two prop plugs against retail (case prop_plugs): "Activate" 0x438160 on every flag / state pair, "Break" 0x4349F0.
inline bool verify_prop_plugs( )
{
    const unsigned* p = g_golden_ppl_words;
    const unsigned na = *p++;
    int failed = 0;
    for ( unsigned k = 0; k < na; ++k, p += 6 )
    {
        solver_rigid_t b{ };
        std::memcpy( reinterpret_cast< std::uint8_t* >( &b ) + 0xD8, &p[ 0 ], 4 );
        std::memcpy( reinterpret_cast< std::uint8_t* >( &b ) + 0xE4, &p[ 1 ], 4 );
        const unsigned two = 0x40000000u;
        std::memcpy( reinterpret_cast< std::uint8_t* >( &b ) + 0xE8, &two, 4 );
        unsigned state, time;
        dynamic_prop_activate( p[ 2 ] ? reinterpret_cast< std::uint8_t* >( &b ) : nullptr );
        std::memcpy( &state, reinterpret_cast< std::uint8_t* >( &b ) + 0xE4, 4 );
        std::memcpy( &time, reinterpret_cast< std::uint8_t* >( &b ) + 0xE8, 4 );
        if ( state != p[ 3 ] || time != p[ 4 ] || p[ 5 ] != 0u )
            ++failed;
    }
    const unsigned nb = *p++;
    for ( unsigned k = 0; k < nb; ++k, p += 4 )
    {
        std::uint8_t request = static_cast< std::uint8_t >( p[ 0 ] );
        int x = 0;
        const void* target = &x;
        breakable_on_break( request, target );
        if ( request != p[ 1 ] || target != nullptr || p[ 2 ] != 1u || p[ 3 ] != 0u )
            ++failed;
    }
    if ( failed == 0 )
        LOG_SUCCESS( "the prop plugs (Activate 0x438160, Break 0x4349F0) match retail on %u + %u vectors", na, nb );
    else
        LOG_ERROR( "the prop plugs FAILED on %d of %u vectors", failed, na + nb );
    return failed == 0;
}

// ROUTING (no retail side: the slots themselves are verified by settings_profile, unlock_rewards, progress_records, progress_champ): ui_profile_saved reaches
// the slot each selector's offset names, on a hand-built UnlockDB and save document.
inline bool verify_profile_saved( )
{
    value_t db;
    value_t& root = json_member( db, "VuDBAsset" );
    value_t& rows = json_member( root, "Unlockables" );
    rows.tag = 5;
    rows.kind = value_t::kind_t::list;
    const auto row = [ &rows ]( const char* type, const char* name, const char* mode, const char* track, std::int32_t credits, bool trial ) {
        value_t r;
        json_set_string( json_member( r, "Type" ), type );
        json_set_string( json_member( r, "Name" ), name );
        json_set_string( json_member( r, "Mode" ), mode );
        json_set_string( json_member( r, "Track" ), track );
        json_set_int( json_member( r, "Credits" ), credits );
        value_t& t = json_member( r, "AvailableInTrial" );
        t.tag = 3;
        t.kind = value_t::kind_t::integer;
        t.integer = trial ? 1 : 0;
        rows.list.push_back( r );
    };
    row( "Boat", "Costly", "", "", 1000, false );
    row( "Boat", "Free", "", "", 0, true );
    row( "Track", "", "Race", "Far", 2000, false );
    row( "Track", "", "Race", "Near", 0, true );
    c_unlock_manager unlock;
    unlock.init( &db, value_t{ }, value_t{ } );
    value_t save;
    const ui_profile_t p = ui_profile_saved( unlock, save );
    int failed = 0, compared = 0;
    const auto same = [ & ]( const char* what, int got, int want ) {
        ++compared;
        if ( got != want )
        {
            LOG_ERROR( "profile: %s = %d, want %d", what, got, want );
            ++failed;
        }
    };
    same( "boat locked", p.unlock( 92, "Boat", "Costly", "" ), 0 );
    same( "boat open", p.unlock( 92, "Boat", "Free", "" ), 1 );
    same( "boat trial no", p.unlock( 96, "Boat", "Costly", "" ), 0 );
    same( "boat trial yes", p.unlock( 96, "Boat", "Free", "" ), 1 );
    same( "track locked", p.unlock( 100, "Race", "Far", "" ), 0 );
    same( "track open", p.unlock( 100, "Race", "Near", "" ), 1 );
    same( "track trial no", p.unlock( 104, "Race", "Far", "" ), 0 );
    same( "track trial yes", p.unlock( 104, "Race", "Near", "" ), 1 );
    same( "no row answers open", p.unlock( 92, "Boat", "Unlisted", "" ), 1 );
    unlock.add_credits( 1500, 0, nullptr );
    same( "boat bought", p.unlock( 92, "Boat", "Costly", "" ), 1 );
    same( "track not yet", p.unlock( 100, "Race", "Far", "" ), 0 );
    same( "fresh race_done", p.race_done( "Race", "Near", "None" ), 0 );
    same( "fresh race_rank", p.race_rank( "Race", "Near", "None" ), 0x7FFFFFFF );
    same( "fresh boat", p.progress( "Free" ), 0 );
    same( "fresh champ", p.champ_done( "Cup" ), 0 );
    same( "fresh champ rank", p.champ_rank( "Cup" ), 0x7FFFFFFF );
    same( "fresh ghost", p.ghost_done( "Near" ), 0 );
    ui_progress_add( save, "Race", "Near", "None", "Free", 0, 2, 90.0f, 45.0f );
    ui_progress_boat_used( save, "Free" );
    ui_progress_champ_result( save, "Cup", 1 );
    ui_progress_ghost_add( save, "Near", 0, 80.0f );
    same( "race_done", p.race_done( "Race", "Near", "None" ), 1 );
    same( "race_rank", p.race_rank( "Race", "Near", "None" ), 2 );
    same( "other mode's record", p.race_done( "TimeTrial", "Near", "None" ), 0 );
    same( "another mode index holds nothing", p.race_rank( "Race", "Far", "None" ), 0x7FFFFFFF );
    same( "boat used", p.progress( "Free" ), 1 );
    same( "champ", p.champ_done( "Cup" ), 1 );
    same( "champ rank", p.champ_rank( "Cup" ), 1 );
    same( "ghost", p.ghost_done( "Near" ), 1 );
    if ( failed )
        return false;
    LOG_SUCCESS( "profile on the saved state: %d answers (ROUTING onto the verified slots)", compared );
    return true;
}

inline bool verify_session_all( )
{
    bool ok = true;
    ok = verify_profile_saved( ) && ok;
    ok = verify_prop_plugs( ) && ok;
    ok = verify_game_awards( ) && ok;
    ok = verify_pause_gate( ) && ok;
    ok = verify_tire_track( ) && ok;
    ok = verify_slalom_gate_draw( ) && ok;
    ok = verify_boat_pfx_draw( ) && ok;
    ok = verify_ghost_manager( ) && ok;
    ok = verify_substitute_asset_properties( ) && ok;
    ok = verify_rigid_body_dtor( ) && ok;
    ok = verify_vugame_release( ) && ok;
    ok = verify_breakable_dynamic_properties( ) && ok;
    ok = verify_kinematic_step( ) && ok;
    ok = verify_cheat_finish( ) && ok;
    ok = verify_race_results( ) && ok;
    ok = verify_timetrial_results( ) && ok;
    ok = verify_slalom_results( ) && ok;
    ok = verify_timetrial_split( ) && ok;
    ok = verify_ghost_split( ) && ok;
    ok = verify_slalom_split( ) && ok;
    ok = verify_timetrial_place( ) && ok;
    ok = verify_slalom_place( ) && ok;
    ok = verify_breakable_properties( ) && ok;
    ok = verify_slalom_gate_properties( ) && ok;
    ok = verify_explosion_properties( ) && ok;
    ok = verify_slalom_gates( ) && ok;
    ok = verify_breakable_contact( ) && ok;
    ok = verify_breakable_dynamic_contact( ) && ok;
    ok = verify_mine_explode( ) && ok;
    ok = verify_mine_properties( ) && ok;
    ok = verify_action_mode( ) && ok;
    ok = verify_front_end_trigger( ) && ok;
    ok = verify_logic_properties( ) && ok;
    ok = verify_string_db( ) && ok;
    ok = verify_prop_contact( ) && ok;
    return ok;
}
