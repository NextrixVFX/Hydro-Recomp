#pragma once

// c_world's animate - member definitions moved out of world.hxx (one class, split by concern).

// The animated props are already placed, so this only refreshes their
// vertices. "Start Time" is a phase offset in seconds, which is why four
// identical pump pistons do not pump in step.
inline void c_world::skin_props( float dt )
{
    for ( animated_t& item : m_animated )
    {
        if ( !item.playing )
            continue;

        // VuAnimatedPropEntity's tick @0x5ed030 fades its controls, advances
        // them (@0x5169e0) and then evaluates the pose (@0x516cf0). The
        // listener is the prop's VuAnimationEventIF @0x5ed680: AnimDone and
        // AnimLoop become its outputs (motion.hxx prop_tick).
        //
        // Two things the old hand-written clock did are gone because retail
        // does not do them: a One Shot prop was retired at its last frame
        // (One Shot latches only the VuPlayAnimationEntity, at +168), and
        // Start Time was added as a phase every frame instead of being the
        // control's starting time.
        // The pose: 0x516CF0 (skel_animated_build) over the controls, 0x532720 the palette, the prop's own instance (world/motion.hxx
        // prop_instance, the same path the attach records read). The draw reads that instance's palette (prop_palette): there is no second
        // store to overwrite it, which is how the old one-clip-frame walk undid this pose once (8c7fdc2 + 729f931).
        if ( !prop_tick( item, dt ) && m_pose_unknown++ == 0 )
            LOG_WARNING( "prop %s has no pose: no instance or clip (UNKNOWN)", item.path.c_str( ) );
    }
}

// Join the animated prop instances to the clips the event graph points at
// them. Actions and props sit in unrelated parts of the tree and either
// may be visited first, so the join has to happen after the whole walk.
inline void c_world::bind_animations( )
{
    const char* named = c_engine::option( "drive-to" );
    m_triggers.watch( named, c_engine::option( "drive" ) != nullptr || ( named != nullptr && named[ 0 ] != '\0' ) );
    int bound = 0;
    int loaded = 0;
    std::map< std::string, int > by_trigger;
    // Every sender's assets, as the VuPlayAnimationEntity's asset refs loaded them with the project: its timed events, its tokens.
    m_play_tokens.clear( );
    for ( auto& sender : m_plays )
    {
        play_t& play = sender.second;
        m_play_tokens.push_back( "VuAnimationAsset/" + play.clip );
        play.clip_token = static_cast< std::uint32_t >( m_play_tokens.size( ) );
        if ( play.timed.empty( ) )
            continue;
        const std::string name = "VuTimedEventAsset/" + play.timed;
        m_play_tokens.push_back( name );
        play.timed_token = static_cast< std::uint32_t >( m_play_tokens.size( ) );
        if ( m_timed.find( name ) != m_timed.end( ) )
            continue;
        std::vector< std::uint8_t > bytes;
        std::vector< timed_event_t > events;
        if ( m_archive.read( name.c_str( ), bytes ) && timed_events_load( bytes, events ) )
            m_timed.emplace( name, std::move( events ) );
        else
            LOG_WARNING( "timed events %s failed to load", name.c_str( ) );
    }
    for ( animated_t& item : m_animated )
    {
        const auto found = m_play.find( item.path );
        if ( found == m_play.end( ) )
            continue;
        ++bound;
        item.clip = "VuAnimationAsset/" + found->second.clip;
        item.trigger = found->second.trigger;
        item.start = found->second.start;
        item.looping = found->second.looping;
        item.factor = found->second.factor;
        if ( !found->second.timed.empty( ) )
            item.timed = "VuTimedEventAsset/" + found->second.timed;
        ++by_trigger[ item.trigger ];

        if ( m_clips.find( item.clip ) == m_clips.end( ) )
        {
            std::vector< std::uint8_t > bytes;
            c_clip clip{};
            if ( m_archive.read( item.clip.c_str( ), bytes ) && clip.load( bytes ) )
                m_clips.emplace( item.clip, std::move( clip ) );
            else
                LOG_WARNING( "prop clip %s failed to parse", item.clip.c_str( ) );
        }
        const auto clip = m_clips.find( item.clip );
        if ( clip == m_clips.end( ) || !clip->second.valid( ) )
            continue;
        ++loaded;
        // Cross-check the two unrelated parsers against each other, the
        // same way the boat clips are checked: the clip's bone count must
        // agree with the skeleton read from the model file.
        model_for( item.asset );
        const auto skeleton = m_skeletons.find( item.asset );
        if ( skeleton != m_skeletons.end( )
            && clip->second.bones( ) != static_cast< int >( skeleton->second.size( ) ) )
        {
            LOG_WARNING( "prop %s clip has %d bones but model %s has %u",
                item.path.c_str( ), clip->second.bones( ), item.asset.c_str( ),
                static_cast< unsigned >( skeleton->second.size( ) ) );
        }

        // A CHECK THE STILLS CANNOT MAKE. Run a copy of this prop's control
        // over one pass of its clip at the bench step, offline - nothing is
        // fired - and count what the walk would deliver: every timed event
        // name, and for BroadcastGenericEvent how many VuGenericEventEntity
        // receivers carry the name. An event the clip never reaches, or a
        // broadcast nobody listens for, shows up here at load.
        const auto timed = m_timed.find( item.timed );
        if ( timed != m_timed.end( ) )
        {
            animation_control_t probe{};
            const int frames = clip->second.frames( );
            probe.end = static_cast< float >( frames - 1 ) * 0.033333335f;
            probe.loop = static_cast< float >( frames ) * 0.033333335f;
            // As authored: a looping prop is probed for exactly one loop, a
            // one-shot until its AnimDone. Forcing every probe one-shot
            // misreported the pistons - one starting at 15s, past its
            // 1.933s end, wraps rather than finishing.
            probe.looping = item.looping;
            probe.factor = item.factor;
            probe.time = item.start;
            probe.events = &timed->second;
            const float step_time = 0.016666667f;
            const float rate = std::fabs( item.factor ) * step_time;
            const int limit = !item.looping ? 100000
                : rate > 0.0f ? static_cast< int >( std::ceil( probe.loop / rate ) ) : 0;
            int fires = 0;
            int heard = 0;
            int deaf = 0;
            bool done = false;
            for ( int step = 0; step < limit && !done; ++step )
            {
                control_advance( probe, step_time, true,
                    [ & ]( const std::string& name, const value_t* params )
                    {
                        if ( name == "AnimDone" )
                        {
                            done = true;
                            return;
                        }
                        if ( name == "AnimLoop" )
                            return;
                        ++fires;
                        if ( name != "BroadcastGenericEvent" || params == nullptr )
                            return;
                        const value_t* carried = find( *params, "EventName" );
                        int receivers = 0;
                        for ( const auto& pair : m_events )
                        {
                            if ( carried != nullptr && pair.second == carried->text )
                                ++receivers;
                        }
                        receivers > 0 ? ++heard : ++deaf;
                    } );
            }
            LOG_SUCCESS( "timed events %s on %s: %d fires from %u events over one %s (end %.3fs, start %.3fs), %d broadcasts heard, %d with no receiver",
                item.timed.c_str( ), item.path.c_str( ), fires,
                static_cast< unsigned >( timed->second.size( ) ),
                item.looping ? "loop" : "pass", probe.end, item.start, heard, deaf );
        }
    }
    // Place them. These 19 were invisible before, because the walk only
    // ever handled Vu3dDrawStaticModelComponent.
    //
    // A prop never moves - only its vertices deform - so unlike a boat it
    // does not need re-placing every frame. Each animatable instance gets
    // its own cloned buffer, placed ONCE here, and the frame loop just
    // streams new vertices into it. That keeps every part's real material
    // tag (the tag doubles as the render-state selector AND the per-frame
    // drop group, so there is no spare one to borrow) and costs no
    // placement work per frame.
    // FIRST, decide which clips are even skin deformations.
    //
    // Does a prop clip's frame 0 equal its bind pose? For the boats that
    // was MEASURED (41 of 41 chained, worst 0.0000). For props it would
    // only be an assumption. Measured here, they split in two:
    //
    //   PumpSpin_skinned   2 of 2 bones identity, worst   0.0000
    //   PumpPiston_skinned 0 of 3 bones identity, worst 120.8034
    //   Helicopter_AS-350  0 of 3 bones identity, worst  21.9291
    //   HanggliderA        0 of 1 bones identity, worst 2138.0303
    //   HanggliderB        0 of 1 bones identity, worst 2174.8982
    //
    // A clip that merely started mid-stroke would be off by a few units -
    // the hull's own mismatch was 3.74. Thousands of units is a different
    // kind of thing: those clips hold an ABSOLUTE WORLD TRANSFORM, moving
    // the entity through the level (the hanggliders fly, the helicopter
    // hovers) rather than deforming the mesh. Skinning them against the
    // inverse bind pose would fling the geometry across the map.
    //
    // So only stream the ones that really are deformations. The rest stay
    // at their bind pose until the world-path case is reversed properly -
    // a static prop is honest, a prop flung 2000 units is not.
    std::set< std::string > deformable;
    std::set< std::string > scored;
    for ( const animated_t& item : m_animated )
    {
        if ( item.clip.empty( ) || !scored.insert( item.asset ).second )
            continue;
        const auto bones = m_skeletons.find( item.asset );
        const auto clip = m_clips.find( item.clip );
        if ( bones == m_skeletons.end( ) || clip == m_clips.end( ) || !clip->second.valid( ) )
            continue;
        if ( clip->second.bones( ) != static_cast< int >( bones->second.size( ) ) )
            continue;
        const std::vector< model_bone_t >& bind = bones->second;
        std::vector< float > chained;
        build_palette( chained, bind, clip->second, 0, nullptr, 0, 0.0f );
        // build_palette already folds in inverse(bind), so if frame 0 is
        // the bind pose every matrix is the identity. Scoring the matrices
        // is the same quantity the vertex round-trip measures, obtained
        // before anything is streamed.
        int identity = 0;
        float worst = 0.0f;
        for ( std::size_t b = 0; b < bind.size( ); ++b )
        {
            const float* m = chained.data( ) + b * 16;
            float off = 0.0f;
            for ( int cell = 0; cell < 16; ++cell )
            {
                const float want = cell % 5 == 0 ? 1.0f : 0.0f;
                const float delta = std::fabs( m[ cell ] - want );
                if ( delta > off )
                    off = delta;
            }
            if ( off > worst )
                worst = off;
            if ( off < 0.001f )
                ++identity;
        }
        // Frame 0 need NOT equal the bind pose, and demanding it was the
        // wrong test. Printing the three candidate spaces side by side
        // settled what these clips are:
        //
        //   HanggliderA  placement t (0,0,0)       clip f0 (-288, -2138, 55)
        //   Airplane_Pitts placement t (0,0,0)     clip f0 ( 317, -1598, 64)
        //     -> the entity has NO transform of its own; the clip carries
        //        world coordinates and IS the object's world path. Because
        //        the placement is the identity, streaming the clip-posed
        //        vertices puts them at the right world position anyway.
        //
        //   Helicopter   bind t (0, -0.69, 3.85)   clip f0 (22.33, -0.69, 3.85)
        //   PumpPiston   bind t (0, 0, 53.00)      clip f0 (0, 0, 49.80)
        //     -> Y and Z match the bind EXACTLY and only the moving axis
        //        differs, so these are ordinary model-local clips that
        //        merely START AWAY FROM BIND. The helicopter patrols in X,
        //        the piston reciprocates in Z from 49.80 to 61.83.
        //
        // So every case is handled by the same stream path; only the
        // acceptance test had to change, from "frame 0 reproduces bind" to
        // "the pose preserves the model's size" (see skin_props).
        const bool at_bind = identity == static_cast< int >( bind.size( ) );
        const clip_pose_t& first = clip->second.at( 0, 0 );
        const bool origin = std::fabs( item.world[ 12 ] ) < 0.01f
            && std::fabs( item.world[ 13 ] ) < 0.01f
            && std::fabs( item.world[ 14 ] ) < 0.01f;
        deformable.insert( item.asset );
        LOG_INFO( "prop %-34s bones %-3u frame 0 palette %d of %u identity (worst %8.2f) %s",
            item.asset.c_str( ), static_cast< unsigned >( bind.size( ) ),
            identity, static_cast< unsigned >( bind.size( ) ), worst,
            at_bind ? "starts at bind"
                : ( origin ? "world path, placement at origin" : "starts away from bind" ) );
        ( void )first;
    }

    int posed = 0;
    for ( animated_t& item : m_animated )
    {
        const auto mesh = m_skinned.find( item.asset );
        const auto bones = m_skeletons.find( item.asset );
        const auto clip = m_clips.find( item.clip );
        // Nothing starts itself any more. Props that move from the first
        // frame are started below by firing every
        // VuOnProjectInitializedEntity's "Trigger" through the graph,
        // which is how retail starts them; the name-sniffing test that
        // used to live here ("does the trigger's path contain
        // OnGameStart") was a guess at that mechanism and is gone.
        //
        // Prepare EVERY bound prop, whether or not its trigger has fired.
        // A gated prop still needs its clone or placement handles ready
        // at load, or the event would arrive with nowhere to put the pose.
        item.playing = false;
        const bool usable = deformable.count( item.asset ) != 0
            && mesh != m_skinned.end( ) && bones != m_skeletons.end( )
            && clip != m_clips.end( ) && clip->second.valid( )
            && clip->second.bones( ) == static_cast< int >( bones->second.size( ) )
            && mesh->second.skin.size( ) == mesh->second.bind.size( );
        if ( usable )
        {
            ++posed;
            item.geometry = 0;   // skin_props' test: a pose is wanted
        }
        model_for( item.asset );
        if ( m_retail )
            if ( const gfx_scene_view_t* v = m_retail->model( item.asset ) )
            {
                item.retail = std::make_unique< gfx_animated_model_t >( );
                gfx_scene_view_attach( *item.retail, *v );
            }
    }
    LOG_INFO( "animated props: %d posed", posed );
    // Every clip each prop's senders name, loaded with the level: no archive read at a plug.
    for ( const auto& sender : m_plays )
        for ( const std::string& target : sender.second.props )
            for ( animated_t& item : m_animated )
                if ( item.path == target && item.entity && prop_instance( *item.entity ) )
                    prop_clip( *m_prop_skels[ item.entity ], "VuAnimationAsset/" + sender.second.clip );


    if ( m_animated.empty( ) )
        return;
    if ( bound == static_cast< int >( m_animated.size( ) ) )
    {
        LOG_SUCCESS( "animated props: all %d bound to a clip, %d clips parsed",
            bound, loaded );
    }
    else
    {
        LOG_WARNING( "animated props: %d of %u bound to a clip, %d clips parsed",
            bound, static_cast< unsigned >( m_animated.size( ) ), loaded );
    }
    LOG_INFO( "event graph: %u trigger planes, %u one-shot filters, %u play actions, %u resolved plugs",
        static_cast< unsigned >( m_triggers.size( ) ),
        static_cast< unsigned >( m_filters.size( ) ),
        static_cast< unsigned >( m_actions.size( ) ),
        static_cast< unsigned >( m_graph.size( ) ) );
    motions_game_initialize( );
    audit_graph( );
    m_triggers.test_geometry( );
    test_wiring( );
    for ( const auto& pair : by_trigger )
        LOG_INFO( "  trigger %-62s x%d", pair.first.c_str( ), pair.second );
    // The project is built, every prop has its clone or its placement
    // handles, so the load-time event can go out. Retail raises
    // "OnProjectInitialized" once and every
    // VuOnProjectInitializedEntity's hook @0x5efa60 fires its "Trigger"
    // output in response; the graph does the rest.
    if ( !m_defer_project_initialized )
        project_initialized( );
}

// 0x501260(root, "OnProjectInitialized") as 0x4BA060 raises it: every VuOnProjectInitializedEntity's hook @0x5efa60
// fires its "Trigger" output (the walk's order is the tree's pre-order; ours is m_initialized's - the collection
// order, the same walk).
inline void c_world::project_initialized( )
{
    m_project_initialized = true;
    for ( const std::string& path : m_initialized )
        fire( path, "Trigger" );
    m_rig.report( );   // the rig as the lights' game-start Triggers left it (graph.hxx)
    LOG_INFO( "project initialized: %u entities fired, %d props playing, %d events delivered to a handler, %d not",
        static_cast< unsigned >( m_initialized.size( ) ), playing( ), m_fired, m_unhandled_events );
    std::string by;
    for ( const auto& u : m_unhandled_by )
        by += ( by.empty( ) ? "" : ", " ) + u.first + " x" + std::to_string( u.second );
    LOG_VERBOSE( "event census at project initialized: %d logic handlers run; unhandled by class . plug: %s", m_logic_fired, by.c_str( ) );
}

inline int c_world::playing( ) const
{
    int count = 0;
    for ( const animated_t& item : m_animated )
        count += item.playing ? 1 : 0;
    return count;
}
