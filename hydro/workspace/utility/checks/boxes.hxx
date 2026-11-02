#pragma once

#include <workspace/core/game/boxes.hxx>
#include <workspace/core/game/docked.hxx>
#include <workspace/core/game/language.hxx>
#include <workspace/core/game/touches.hxx>

// VuMessageBoxManager, the screen's runtime and VuMessageBoxEntity against retail (case message_box, cases/msgbox.py): after every step of 24
// scripts of 70 the machine, fade, time, queue, the screen's words, the five texts and the service log in order.
inline bool verify_message_box( )
{
    save_words_t c{ g_golden_message_box_words };
    const unsigned runs = c.next( );
    int bad = 0, steps = 0;
    const auto real = [ ]( unsigned u ) {
        float f;
        std::memcpy( &f, &u, 4 );
        return f;
    };
    for ( unsigned v = 0; v < runs; ++v )
    {
        ui_message_box_t box;
        msgbox_screen_t screen;
        screen.m_box = &box;
        box.m_screen_scale[ 0 ] = real( c.next( ) );
        box.m_screen_scale[ 1 ] = real( c.next( ) );
        for ( int k = 0; k < 8; ++k )
            box.m_touch[ k / 4 ][ k % 4 ] = real( c.next( ) );
        screen.m_spin = real( c.next( ) );
        const unsigned count = c.next( );
        std::string log;
        unsigned keys = 0, focus = 0;
        const auto say = [ & ]( const std::string& s ) {
            if ( !log.empty( ) )
                log += '|';
            log += s;
        };
        msgbox_hooks_t h;
        h.text = [ ]( const std::string& n ) { return n == "nokey" ? std::string( "[none]" ) : "T_" + n; };
        h.missing = [ ] { return std::string( "[none]" ); };
        h.ui_tick = [ & ]( float ) { say( "OnUITick" ); };
        h.ui_event = [ & ]( const char* n ) {
            if ( !std::strcmp( n, "MessageBoxClose" ) )
                say( "off" );    // the case logs 0x500B80 before it enters slot 7
            say( std::string( "ui:" ) + n );
        };
        h.pressed = [ & ]( const char* n ) {
            say( std::string( "key:" ) + n );
            static const char* const names[ 5 ] = { "Select", "Left", "Left2", "Right", "Right2" };
            for ( int k = 0; k < 5; ++k )
                if ( !std::strcmp( n, names[ k ] ) )
                    return ( keys >> k & 1u ) != 0;
            return false;
        };
        h.focus = [ & ] { return focus != 0; };
        h.play = [ & ]( const char* n ) { say( std::string( "play:" ) + n ); };
        h.pause = [ & ] { say( "pause" ); };
        h.resume = [ & ] { say( "resume" ); };
        h.project_on = [ & ] { say( "repo+" ); say( "on" ); };
        h.project_off = [ & ] { say( "repo-" ); };
        h.project_ready = [ & ] { say( "OnProjectInitialized" ); };
        h.texture_load = [ & ]( const std::string& n ) { say( "tex:" + n ); };
        h.texture_free = [ & ] { say( "free" ); };
        c_msgbox_manager m;
        m.init( &screen, h );
        msgbox_entity_t ents[ 2 ];
        for ( auto& e : ents )
            e.m_fire = [ & ]( const char* n ) { say( std::string( "plug:" ) + n ); };
        for ( unsigned k = 0; k < count; ++k )
        {
            ++steps;
            log.clear( );
            const unsigned op = c.next( );
            if ( op == 0 )
            {
                const float dt = real( c.next( ) );
                keys = c.next( );
                focus = c.next( );
                const unsigned t = c.next( );
                screen.m_touch_moved = t & 1u;
                screen.m_touch_down = t >> 1 & 1u;
                screen.m_touch_up = t >> 2 & 1u;
                screen.m_touch[ 0 ] = real( c.next( ) );
                screen.m_touch[ 1 ] = real( c.next( ) );
                m.tick( dt );
            }
            else
            {
                msgbox_entity_t& e = ents[ c.next( ) ];
                if ( op == 1 )
                {
                    e.m_params.m_heading = c.str( );
                    e.m_params.m_body = c.str( );
                    e.m_params.m_text_a = c.str( );
                    e.m_params.m_text_b = c.str( );
                    e.m_params.m_texture = c.str( );
                    e.m_params.m_pause = static_cast< std::uint8_t >( c.next( ) );
                    e.m_params.m_input = static_cast< std::uint8_t >( c.next( ) );
                    msgbox_entity_create( e, m );
                }
                else
                    msgbox_entity_destroy( e );
            }
            const auto bits = [ ]( float f ) {
                unsigned u;
                std::memcpy( &u, &f, 4 );
                return u;
            };
            bool same = true;
            same = c.next( ) == static_cast< unsigned >( m.fsm( ).m_current ) && same;
            same = c.next( ) == bits( m.fade( ) ) && same;
            same = c.next( ) == bits( m.fsm( ).m_time ) && same;
            same = c.next( ) == m.queued( ) && same;
            same = c.next( ) == ( m.drawing( ) ? 1u : 0u ) && same;
            same = c.next( ) == static_cast< unsigned >( screen.m_result ) && same;
            same = c.next( ) == ( static_cast< unsigned >( screen.m_press_a ) | static_cast< unsigned >( screen.m_press_b ) << 8 ) && same;
            same = c.next( ) == bits( box.m_rotation ) && same;
            same = c.next( ) == static_cast< unsigned >( box.m_button ) && same;
            same = c.next( ) == ( screen.m_on ? 1u : 0u ) && same;
            unsigned en = 0;
            for ( int j = 1; j < 5; ++j )
                en |= static_cast< unsigned >( box.m_blocks[ j ].m_enabled ) << ( j - 1 );
            same = c.next( ) == en && same;
            same = c.next( ) == ( static_cast< unsigned >( screen.m_touch_moved ) << 16 | static_cast< unsigned >( screen.m_touch_down ) << 8 | screen.m_touch_up ) && same;
            for ( int j = 0; j < 5; ++j )
                same = c.str( ) == box.m_blocks[ j ].m_text && same;
            const std::string want = c.str( );
            same = want == log && same;
            if ( !same )
            {
                if ( ++bad < 6 )
                    LOG_ERROR( "message_box run %u step %u differs: retail [%s] ours [%s]", v, k, want.c_str( ), log.c_str( ) );
            }
        }
    }
    if ( bad )
        LOG_ERROR( "message_box FAILED %d of %d", bad, steps );
    else
        LOG_SUCCESS( "verify_message_box: %d steps of VuMessageBoxManager, its screen and VuMessageBoxEntity match retail", steps );
    return bad == 0;
}


// 0x440890 0x440900 0x46C950 0x476120 0x476140 against retail (case ui_odds).
inline bool verify_ui_odds( )
{
    save_words_t c{ g_golden_ui_odds_words };
    int bad = 0;
    const auto real = [ ]( unsigned u ) {
        float f;
        std::memcpy( &f, &u, 4 );
        return f;
    };
    unsigned n = c.next( );
    const unsigned picks = n;
    for ( unsigned k = 0; k < n; ++k )
    {
        const unsigned count = c.next( ), clock = c.next( ), base = c.next( ), want = c.next( );
        const int i = barracuda_pick( count, real( clock ), static_cast< int >( base ) );
        if ( want != ( i < 0 ? 0u : 0x1000u + static_cast< unsigned >( i ) ) )
            ++bad;
    }
    n = c.next( );
    const unsigned ticks = n;
    for ( unsigned k = 0; k < n; ++k )
    {
        const unsigned before = c.next( ), type = c.next( ), value = c.next( ), size = c.next( ), want = c.next( );
        float clock = real( before );
        const unsigned rec[ 2 ] = { type, value };
        barracuda_tick( clock, reinterpret_cast< const std::uint8_t* >( rec ), static_cast< int >( size ) );
        unsigned got;
        std::memcpy( &got, &clock, 4 );
        if ( got != want )
            ++bad;
    }
    n = c.next( );
    const unsigned touches = n;
    for ( unsigned k = 0; k < n; ++k )
    {
        const unsigned type = c.next( ), hit = c.next( ), armed = c.next( ), mid = c.next( ), last = c.next( );
        const std::string want = c.str( );
        touch_special_t e;
        e.m_type = static_cast< int >( type );
        e.m_armed = static_cast< std::uint8_t >( armed );
        touch_special_hit( e, static_cast< int >( hit ) );
        const unsigned got = e.m_armed;
        std::string log;
        touch_special_tick( e, [ & ]( const char* s ) { log += std::string( "play:" ) + s + "|"; }, [ & ] { log += "plug:Trigger"; } );
        if ( got != mid || e.m_armed != last || log != want )
            ++bad;
    }
    n = c.next( );
    const unsigned tilts = n;
    for ( unsigned k = 0; k < n; ++k )
    {
        const unsigned a = c.next( ), b = c.next( ), s = c.next( ), type = c.next( ), v = c.next( );
        if ( type != 3u || v != tilt_answer( a != 0, b != 0, static_cast< std::uint8_t >( s ) ) )
            ++bad;
    }
    if ( bad )
        LOG_ERROR( "ui_odds FAILED %d", bad );
    else
        LOG_SUCCESS( "verify_ui_odds: %u picks, %u ticks, %u touches, %u tilt answers match retail", picks, ticks, touches, tilts );
    return bad == 0;
}


// VuRampEntity's contact callback 0x43E420 against retail (case ramp_contact): the friction's bits and the hull's byte.
inline bool verify_ramp_contact( )
{
    save_words_t c{ g_golden_ramp_contact_words };
    const unsigned n = c.next( );
    int bad = 0;
    for ( unsigned k = 0; k < n; ++k )
    {
        contact_record_t r;
        float v[ 3 ];
        for ( int j = 0; j < 3; ++j )
        {
            const unsigned u = c.next( );
            std::memcpy( &r.normal[ j ], &u, 4 );
        }
        for ( int j = 0; j < 3; ++j )
        {
            const unsigned u = c.next( );
            std::memcpy( &v[ j ], &u, 4 );
        }
        r.friction = 0.625f;
        const bool hit = ramp_contact( r, v );
        const unsigned want = c.next( ), flag = c.next( );
        unsigned got;
        std::memcpy( &got, &r.friction, 4 );
        if ( got != want || flag != ( hit ? 1u : 0u ) )
            ++bad;
    }
    if ( bad )
        LOG_ERROR( "ramp_contact FAILED %d of %u", bad, n );
    else
        LOG_SUCCESS( "verify_ramp_contact: %u contacts match retail's 0x43E420", n );
    return bad == 0;
}

// The props' motion IF slots 1 / 2 0x546D80 / 0x546DB0 against retail (case prop_motion_flags): per call the component's +0xD0 in, whether the
// body exists, the component's word and the body's word after.
inline bool verify_prop_motion_flags( )
{
    save_words_t c{ g_golden_prop_motion_flags_words };
    const unsigned n = c.next( );
    int bad = 0;
    for ( unsigned k = 0; k < n; ++k )
    {
        std::uint32_t flags = c.next( );
        const unsigned on = c.next( ), body = c.next( ), want_flags = c.next( ), want_body = c.next( );
        std::uint32_t word = 0x55555555u;
        rigid_motion_flag( flags, body ? &word : nullptr, on != 0u );
        if ( flags != want_flags || word != want_body )
            ++bad;
    }
    if ( bad )
        LOG_ERROR( "prop_motion_flags FAILED %d of %u", bad, n );
    else
        LOG_SUCCESS( "verify_prop_motion_flags: %u calls of 0x546D80 / 0x546DB0 match retail", n );
    return bad == 0;
}

// createRigidBody 0x544100's sphere and box branches against retail (case prop_box_bodies): the real box / sphere props of
// the level templates, through hydro's tree: the component's words after gameInitialize, the shape's type, scaling, implicit
// dimensions and margin, the compound's aabb, scaling and child origin, the body's 0x2D4 bytes (pointer words masked).
inline bool verify_prop_box_bodies( )
{
    save_words_t c{ g_golden_prop_box_bodies_words };
    static std::vector< std::string > surfaces;
    surfaces.clear( );
    for ( unsigned n = c.next( ); n; --n )
        surfaces.push_back( c.str( ) );
    const unsigned count = c.next( );
    c_archive archive;
    const std::string pack = world_check_pack( );
    if ( pack.empty( ) || !archive.open( pack.c_str( ) ) )
    {
        LOG_ERROR( "verify_prop_box_bodies: Base.apf not readable" );
        return false;
    }
    static const entity_registry_t registry = build_entity_registry( { } );
    entity_world_t world;
    world.m_registry = &registry;
    world.m_ctor_for = entity_ctor_for;
    world.m_template = []( const std::string& ) -> const value_t* { return nullptr; };
    world.m_components_for = entity_components_scripted;
    world.m_component_load = entity_load_component;
    world.m_component_post = entity_post_load_component;
    world.m_component_mark = entity_mark_component;
    world.m_state_for = entity_state_for;
    world.m_properties_load = entity_load_properties;
    world.m_properties_mark = entity_mark_properties;
    boat_mesh_library_t lib;
    lib.read_user = &archive;
    lib.read = []( void* u, const std::string& key, std::vector< std::uint8_t >& blob ) { return static_cast< c_archive* >( u )->read( key.c_str( ), blob ); };
    bodies_hooks_t hooks;
    hooks.surface = []( void*, const std::string& n ) -> std::uint8_t {
        for ( std::size_t k = 0; k < surfaces.size( ); ++k )
            if ( n == surfaces[ k ] )
                return static_cast< std::uint8_t >( 1 + k );
        return 0xEE;
    };
    hooks.gravity = []( void*, float* g ) {
        const std::uint32_t z = 0xC19CE560u;
        g[ 0 ] = g[ 1 ] = 0.0f;
        std::memcpy( &g[ 2 ], &z, 4 );
    };
    static std::vector< value_t > documents;
    std::string open_doc;
    std::unique_ptr< entity_t > tree;
    const auto bits = []( float f ) { std::uint32_t u; std::memcpy( &u, &f, 4 ); return u; };
    int bad = 0, compounds = 0, words = 0;
    for ( unsigned v = 0; v < count; ++v )
    {
        const std::string doc = c.str( ), path = c.str( );
        const bool ramp = c.next( ) != 0;
        const unsigned mass = c.next( );
        unsigned after[ 3 ], added, drawn, has;
        for ( unsigned& a : after )
            a = c.next( );
        added = c.next( );
        drawn = c.next( );
        has = c.next( );
        if ( doc != open_doc )
        {
            std::vector< std::uint8_t > bytes;
            if ( !archive.read( doc.c_str( ), bytes ) )
            {
                LOG_ERROR( "verify_prop_box_bodies: %s not readable", doc.c_str( ) );
                return false;
            }
            documents.push_back( c_reader( bytes ).parse( ) );
            tree = entity_load_project( member( documents.back( ), "VuProject" ), "Data/Projects/Check.vuprj", world );
            open_doc = doc;
        }
        entity_t* e = tree.get( );
        for ( std::size_t at = 0; e && at <= path.size( ); )
        {
            const std::size_t slash = path.find( '/', at );
            const std::string part = path.substr( at, slash == std::string::npos ? std::string::npos : slash - at );
            entity_t* next = nullptr;
            for ( auto& k : e->m_children )
                if ( k->m_name == part )
                    next = k.get( );
            e = next;
            at = slash == std::string::npos ? path.size( ) + 1 : slash + 1;
        }
        rigid_component_state_t* rs = nullptr;
        if ( e )
            for ( entity_component_t& k : e->m_components )
                if ( k.m_vtable == k_rigid_body_component_vtable )
                    rs = static_cast< rigid_component_state_t* >( k.m_state.get( ) );
        transform_component_t* t = e ? entity_transform( *e ) : nullptr;
        // The golden row is read whatever happens below: one failure must not desynchronise the stream.
        unsigned shape[ 8 ] = { }, comp[ 12 ] = { }, has_compound = 0;
        static unsigned body[ 181 ][ 2 ];
        if ( has )
        {
            for ( unsigned& w : shape )
                w = c.next( );
            has_compound = c.next( );
            if ( has_compound )
                for ( unsigned& w : comp )
                    w = c.next( );
            for ( auto& w : body )
            {
                w[ 0 ] = c.next( );
                w[ 1 ] = c.next( );
            }
        }
        if ( !e || !rs || !t || !e->m_state )
        {
            if ( ++bad <= 8 )
                LOG_ERROR( "prop box bodies: '%s' not found in hydro's tree", path.c_str( ) );
            continue;
        }
        rigid_component_t r = rs->value;
        std::memcpy( &r.mass, &mass, 4 );
        if ( ramp )
            r.word_f4 = 1;
        const collision_mesh_asset_t* asset = nullptr;
        if ( !r.mesh_asset.empty( ) && boat_mesh_exists( &lib, "VuCollisionMeshAsset", r.mesh_asset ) )
            asset = lib.collision_of( boat_mesh_create( &lib, "VuCollisionMeshAsset", r.mesh_asset ) );
        const std::uint8_t visible = static_cast< prop_entity_state_t* >( e->m_state.get( ) )->visible;
        auto b = std::make_unique< level_body_t >( );
        std::uint32_t id = has ? body[ 0x210 / 4 ][ 1 ] : 0u;
        std::string why;
        int got_drawn = 0, got_added = 0;
        const bool ok = bodies_prop_game_initialize( *b, r, visible, asset, t->local, t->scale, id, hooks, [ & ] { ++got_drawn; },
                                                     [ & ]( level_body_t& ) { ++got_added; return true; }, why );
        int fail = -1;
        const unsigned now[ 3 ] = { r.flags_d0, static_cast< unsigned >( r.group ) | static_cast< unsigned >( r.mask ) << 16, r.flags_d8 };
        if ( !ok )
            fail = 1000;
        for ( int k = 0; k < 3 && fail < 0; ++k )
            if ( now[ k ] != after[ k ] )
                fail = 1001 + k;
        if ( fail < 0 && ( static_cast< unsigned >( got_added ) != added || static_cast< unsigned >( got_drawn ) != drawn ) )
            fail = 1004;
        if ( fail < 0 && ( ( b->body.body.internal_type == 2 ) != ( has != 0 ) || b->has_compound != ( has_compound != 0 ) ) )
            fail = 1005;
        if ( fail < 0 && has )
        {
            const convex_shape_t& s = b->cylinder;
            const unsigned mine[ 8 ] = { static_cast< unsigned >( s.type ), bits( s.local_scaling[ 0 ] ), bits( s.local_scaling[ 1 ] ), bits( s.local_scaling[ 2 ] ),
                                         bits( s.implicit[ 0 ] ), bits( s.implicit[ 1 ] ), bits( s.implicit[ 2 ] ), bits( s.margin ) };
            for ( int k = 0; k < 8 && fail < 0; ++k )
                if ( mine[ k ] != shape[ k ] && !( s.type == 8 && ( k == 5 || k == 6 ) ) )   // the sphere's +0x24 / +0x28: the ctor leaves them
                    fail = 1100 + k;
            if ( has_compound && fail < 0 )
            {
                ++compounds;
                const compound_shape_t& m = b->compound;
                const float* o = m.children[ 0 ].transform + 12;
                const unsigned cw[ 12 ] = { bits( m.local_aabb_min[ 0 ] ), bits( m.local_aabb_min[ 1 ] ), bits( m.local_aabb_min[ 2 ] ), bits( m.local_aabb_max[ 0 ] ),
                                            bits( m.local_aabb_max[ 1 ] ), bits( m.local_aabb_max[ 2 ] ), bits( m.local_scaling[ 0 ] ), bits( m.local_scaling[ 1 ] ),
                                            bits( m.local_scaling[ 2 ] ), bits( o[ 0 ] ), bits( o[ 1 ] ), bits( o[ 2 ] ) };
                for ( int k = 0; k < 12 && fail < 0; ++k )
                    if ( cw[ k ] != comp[ k ] )
                        fail = 1200 + k;
            }
            for ( int k = 0; k < 181 && fail < 0; ++k )
            {
                ++words;
                if ( body[ k ][ 0 ] && body_word( b->body, k * 4 ) != body[ k ][ 1 ] )
                    fail = k * 4;
            }
        }
        if ( fail >= 0 && ++bad <= 8 )
            LOG_ERROR( "prop box bodies: '%s' (%s) mass 0x%X differs at 0x%X (%s)", path.c_str( ), doc.c_str( ), mass, fail, why.c_str( ) );
    }
    if ( bad )
        LOG_ERROR( "prop_box_bodies FAILED %d of %u", bad, count );
    else
        LOG_SUCCESS( "verify_prop_box_bodies: %u box / sphere props, %d compounds, %d body words match retail's 0x544100", count, compounds, words );
    return bad == 0;
}

// 0x4EBFE0 against retail (case system_language): the language for every tag, from the initializer's default.
inline bool verify_system_language( )
{
    save_words_t c{ g_golden_system_language_words };
    const unsigned n = c.next( );
    int bad = 0;
    for ( unsigned k = 0; k < n; ++k )
    {
        const std::string tag = c.str( ), def = c.str( ), want = c.str( );
        std::string got = k_language_default;
        if ( got != def )
            ++bad;
        system_language( tag, got );
        if ( got != want && ++bad <= 4 )
            LOG_ERROR( "system_language: '%s' -> '%s', retail '%s'", tag.c_str( ), got.c_str( ), want.c_str( ) );
    }
    if ( bad )
        LOG_ERROR( "system_language FAILED %d of %u", bad, n );
    else
        LOG_SUCCESS( "verify_system_language: %u tags match retail's 0x4EBFE0", n );
    return bad == 0;
}

// 0x4AEA00 0x4AEC30 against retail (case gamepad_box): the byte, the box's params, the broadcast, the callback.
inline bool verify_gamepad_box( )
{
    save_words_t c{ g_golden_gamepad_box_words };
    const unsigned n = c.next( );
    int bad = 0;
    for ( unsigned k = 0; k < n; ++k )
    {
        const unsigned opened = c.next( ), method = c.next( ), tag = c.next( ), value = c.next( ), size = c.next( ), after = c.next( ), events = c.next( ), cb = c.next( ), cb_after = c.next( );
        std::string want_h, want_b, want_a, want_tb, want_tx, sfx_a, sfx_b, event;
        unsigned flip = 0, scale[ 2 ] = { }, flags[ 2 ] = { }, extra = 0;
        for ( unsigned e = 0; e < events; ++e )
        {
            const std::string kind = c.str( );
            if ( kind == "create" )
            {
                want_h = c.str( ); want_b = c.str( ); want_a = c.str( ); want_tb = c.str( ); want_tx = c.str( );
                flip = c.next( ); scale[ 0 ] = c.next( ); scale[ 1 ] = c.next( );
                sfx_a = c.str( ); sfx_b = c.str( );
                flags[ 0 ] = c.next( ); flags[ 1 ] = c.next( ); extra = c.next( );
            }
            else
                event = c.str( );
        }
        c_msgbox_manager m;
        std::string got_event;
        control_method_box_t e;
        e.m_open = static_cast< std::uint8_t >( opened );
        const unsigned params[ 2 ] = { tag, value };
        control_method_pad_lost( e, reinterpret_cast< const std::uint8_t* >( params ), size, static_cast< std::int32_t >( method ), m,
                                 [ & ]( const char* n2 ) { got_event = n2; } );
        const msgbox_ref_t made = m.queue( ).empty( ) ? nullptr : m.queue( ).back( );
        bool ok = e.m_open == after && got_event == event && cb == 0u && ( events != 0 ) == ( made != nullptr );
        if ( made )
        {
            const msgbox_params_t& seen = made->m_params;
            unsigned sb[ 2 ];
            std::memcpy( sb, seen.m_scale, 8 );
            ok = ok && seen.m_heading == want_h && seen.m_body == want_b && seen.m_text_a == want_a && seen.m_text_b == want_tb && seen.m_texture == want_tx &&
                 seen.m_flip == flip && sb[ 0 ] == scale[ 0 ] && sb[ 1 ] == scale[ 1 ] && seen.m_sfx_a == sfx_a && seen.m_sfx_b == sfx_b && seen.m_pause == flags[ 0 ] &&
                 seen.m_input == flags[ 1 ] && static_cast< unsigned >( seen.m_extra ) == extra;
            made->m_callback( *made );
        }
        else if ( cb_after != 2u )
            e.m_open = 0;
        if ( cb_after != 2u )
            ok = ok && e.m_open == cb_after;
        if ( !ok )
            ++bad;
    }
    if ( bad )
        LOG_ERROR( "gamepad_box FAILED %d of %u", bad, n );
    else
        LOG_SUCCESS( "verify_gamepad_box: %u events match retail's 0x4AEA00 / 0x4AEC30", n );
    return bad == 0;
}

// The dock's tick 0x436450 against retail (case dock_tick): the 16 words of the body's new transform.
inline bool verify_dock_tick( )
{
    save_words_t c{ g_golden_dock_tick_words };
    const unsigned n = c.next( );
    int bad = 0;
    for ( unsigned k = 0; k < n; ++k )
    {
        unsigned in[ 16 ], com[ 3 ], spot[ 3 ], want[ 16 ], got[ 16 ];
        for ( unsigned& w : in ) w = c.next( );
        for ( unsigned& w : com ) w = c.next( );
        for ( unsigned& w : spot ) w = c.next( );
        for ( unsigned& w : want ) w = c.next( );
        float b[ 16 ], x[ 3 ], s[ 3 ], o[ 16 ];
        std::memcpy( b, in, 64 );
        std::memcpy( x, com, 12 );
        std::memcpy( s, spot, 12 );
        docked_build_tick( b, x, s, o );
        std::memcpy( got, o, 64 );
        int first = -1;
        for ( int j = 0; j < 16 && first < 0; ++j )
            if ( got[ j ] != want[ j ] )
                first = j;
        if ( first >= 0 && ++bad <= 4 )
            LOG_ERROR( "dock_tick vector %u word %d: 0x%08X, retail 0x%08X", k, first, got[ first ], want[ first ] );
    }
    if ( bad )
        LOG_ERROR( "dock_tick FAILED %d of %u", bad, n );
    else
        LOG_SUCCESS( "verify_dock_tick: %u transforms match retail's 0x436450", n );
    return bad == 0;
}

// The motion watcher 0x5EA740 against retail (case oneshot_watch): the world, its euler, the instance's matrix or none.
inline bool verify_oneshot_watch( )
{
    save_words_t c{ g_golden_oneshot_watch_words };
    const unsigned n = c.next( ), cap = c.next( ), bits = c.next( );
    int bad = 0;
    struct seen_t { int calls = 0; std::uint32_t instance = 0; float m[ 16 ]; } seen;
    for ( unsigned k = 0; k < n; ++k )
    {
        unsigned m[ 16 ], world[ 16 ], euler[ 3 ], want[ 16 ] = { };
        for ( unsigned& w : m ) w = c.next( );
        const unsigned handle = c.next( ), mode = c.next( ), slot = c.next( );
        for ( unsigned& w : world ) w = c.next( );
        for ( unsigned& w : euler ) w = c.next( );
        c.next( );
        const unsigned calls = c.next( );
        for ( unsigned j = 0; j < calls; ++j )
            for ( unsigned& w : want ) w = c.next( );
        pfx_manager_t mgr;
        mgr.capacity = mode == 3u ? slot : cap;
        mgr.mask = cap - 1u;
        mgr.bits = bits;
        mgr.table.resize( cap );
        mgr.entries.resize( cap );
        for ( unsigned j = 0; j < cap; ++j )
        {
            mgr.table[ j ] = static_cast< int >( j );
            mgr.entries[ j ].flags = mode == 1u && j == slot ? 8u : 4u;
            mgr.entries[ j ].serial = 3u;
            mgr.entries[ j ].instance = 100u + j;
        }
        pfx_oneshot_t e;
        e.handle = handle;
        pfx_oneshot_ops_t ops;
        seen.calls = 0;
        ops.instance.user = &seen;
        ops.instance.set_matrix = []( void* u, std::uint32_t i, const float* a ) {
            seen_t& z = *static_cast< seen_t* >( u );
            ++z.calls;
            z.instance = i;
            std::memcpy( z.m, a, 64 );
        };
        transform_component_t t;
        float source[ 16 ];
        std::memcpy( source, m, 64 );
        pfx_oneshot_watch( e, mgr, ops, source, [ & ]( const float* a ) { transform_set_world( t, a, nullptr ); } );
        bool ok = static_cast< unsigned >( seen.calls ) == calls && std::memcmp( t.world, world, 64 ) == 0 && std::memcmp( t.world_rotation, euler, 12 ) == 0;
        if ( calls )
            ok = ok && seen.instance == 100u + slot && std::memcmp( seen.m, want, 64 ) == 0;
        if ( !ok )
            ++bad;
    }
    if ( bad )
        LOG_ERROR( "oneshot_watch FAILED %d of %u", bad, n );
    else
        LOG_SUCCESS( "verify_oneshot_watch: %u watcher calls match retail's 0x5EA740", n );
    return bad == 0;
}

// VuForceAffectorEntity's watcher 0x439DD0 against retail (case force_watch): the ghost's 16 words, the box's scale, the aabb call.
inline bool verify_force_watch( )
{
    save_words_t c{ g_golden_force_watch_words };
    const unsigned n = c.next( );
    int bad = 0;
    struct seen_t { int aabb = 0; unsigned scale[ 4 ] = { }; } seen;
    for ( unsigned k = 0; k < n; ++k )
    {
        const unsigned active = c.next( );
        unsigned m[ 16 ], s[ 3 ], ghost[ 16 ], want[ 4 ];
        for ( unsigned& w : m ) w = c.next( );
        for ( unsigned& w : s ) w = c.next( );
        for ( unsigned& w : ghost ) w = c.next( );
        const unsigned logged = c.next( );
        for ( unsigned& w : want ) w = c.next( );
        force_affector_t e;
        e.active = static_cast< std::uint8_t >( active );
        std::memset( e.ghost, 0x11, 64 );
        force_affector_hooks_t h;
        seen = seen_t{ };
        h.user = &seen;
        h.set_scaling = []( void* u, const float* v ) { std::memcpy( static_cast< seen_t* >( u )->scale, v, 16 ); };
        h.update_aabb = []( void* u ) { ++static_cast< seen_t* >( u )->aabb; };
        float wm[ 16 ], ws[ 3 ];
        std::memcpy( wm, m, 64 );
        std::memcpy( ws, s, 12 );
        force_affector_watch( e, wm, ws, h );
        const bool ok = std::memcmp( e.ghost, ghost, 64 ) == 0 && std::memcmp( seen.scale, want, 16 ) == 0 && ( seen.aabb != 0 ) == ( logged != 0 );
        if ( !ok )
            ++bad;
    }
    if ( bad )
        LOG_ERROR( "force_watch FAILED %d of %u", bad, n );
    else
        LOG_SUCCESS( "verify_force_watch: %u watcher calls match retail's 0x439DD0", n );
    return bad == 0;
}
