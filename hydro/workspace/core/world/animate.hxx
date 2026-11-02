#pragma once

// c_world's animate - member definitions moved out of world.hxx (one class, split by concern).

// The animated props are already placed, so this only refreshes their
// vertices. "Start Time" is a phase offset in seconds, which is why four
// identical pump pistons do not pump in step.
inline void c_world::skin_props( float dt )
{
    std::vector< float > palette;
    for ( animated_t& item : m_animated )
    {
        if ( ( item.geometry < 0 && !item.rigid ) || !item.playing )
            continue;
        const auto mesh = m_skinned.find( item.asset );
        const auto bones = m_skeletons.find( item.asset );
        const auto clip = m_clips.find( item.clip );
        if ( mesh == m_skinned.end( ) || bones == m_skeletons.end( ) || clip == m_clips.end( ) )
            continue;

        // VuAnimatedPropEntity's tick @0x5ed030 advances every control
        // (@0x5169e0, verified as control_advance) and then evaluates the
        // pose (@0x516cf0). The listener is the prop's VuAnimationEventIF
        // @0x5ed680: AnimDone and AnimLoop become its outputs.
        //
        // Two things the old hand-written clock did are gone because retail
        // does not do them: a One Shot prop was retired at its last frame
        // (One Shot latches only the VuPlayAnimationEntity, at +168), and
        // Start Time was added as a phase every frame instead of being the
        // control's starting time.
        // VuAnimation's load @0x562250 sets +24 = (frames - 1) / 30 and
        // +28 = frames / 30, multiplying by the float 0.033333335.
        const int frames = clip->second.frames( );
        item.control.end = static_cast< float >( frames - 1 ) * 0.033333335f;
        item.control.loop = static_cast< float >( frames ) * 0.033333335f;
        const std::string owner = item.path;
        control_advance( item.control, dt, true,
            [ this, &owner ]( const std::string& name, const value_t* params )
            {
                on_animation_event( owner, name, params );
            } );

        // @0x516cf0 picks ONE frame, no interpolation between frames:
        //   ( (int)( frames * time / loop - 0.5 ) + 1 ) % frames
        // so time 0 shows frame 1, not frame 0.
        int frame = 0;
        if ( frames > 0 )
            frame = ( static_cast< int >( static_cast< float >( frames ) * item.control.time
                / item.control.loop - 0.5f ) + 1 ) % frames;

        // The pose for gMatrixArray: 0x533C90 takes the palette; the draw walks the model itself.
        build_palette( item.palette, bones->second, clip->second, frame, nullptr, 0, 0.0f );
        continue;

        // Rigid: fold the single bone's palette matrix into each part's
        // placement. The streamed path computes
        // (world * node) * (palette * vertex); this computes
        // (world * node * palette) * vertex, which is the same transform
        // regrouped, so the picture is identical and the mesh is untouched.
        if ( item.rigid )
        {
            build_palette( palette, bones->second, clip->second, frame, nullptr, 0, 0.0f );
            const std::vector< cached_part_t >& parts = model_for( item.asset );
            for ( std::size_t index = 0;
                index < item.placements.size( ) && index < parts.size( ); ++index )
            {
                if ( item.placements[ index ] < 0 )
                    continue;
                float node[ 16 ]{};
                float posed[ 16 ]{};
                math_multiply( node, item.world, parts[ index ].node );
                math_multiply( posed, node, palette.data( ) );
                c_engine::move( item.placements[ index ], posed );

                // The extent test cannot see this path - no vertices are
                // written - so check the equivalent property on the
                // matrix. A rigid motion must not change the volume the
                // mesh occupies, so the determinant of the posed basis has
                // to match the unposed one. A bad palette scale or a
                // transposed block shows up here as a ratio far from 1.
                if ( index != 0 || m_scored.count( item.asset ) != 0 )
                    continue;
                // Mid-clip here too, for the same reason as the streamed
                // branch: scoring a rigid prop on its first frame caught
                // Props/TNT at frame 0 and reported a translation of
                // 0 0 0, which says nothing about where the clip takes it.
                const int frames = clip->second.frames( );
                const int cycle = frames > 0 ? frame % frames : 0;
                if ( frames > 2 && cycle != frames / 2 )
                    continue;
                m_scored.insert( item.asset );
                const float before = determinant( node );
                const float after = determinant( posed );
                const float ratio = std::fabs( before ) > 1.0e-6f
                    ? std::fabs( after / before ) : 0.0f;

                // "A rigid motion preserves volume" is NOT true in
                // general: Props/TNT/PittsFlyby authors a scale track
                // running 1.000 to 2.000, so at its half-way frame the
                // determinant is exactly 8 and the old test called correct
                // data broken. Compare against the volume the clip's OWN
                // scale implies instead - that still catches a wrong
                // composition while accepting an authored scale.
                const clip_pose_t& pose = clip->second.at( frame, 0 );
                const model_bone_t& rest = bones->second[ 0 ];
                const float clip_volume = pose.scale[ 0 ] * pose.scale[ 1 ] * pose.scale[ 2 ];
                const float bind_volume = rest.scale[ 0 ] * rest.scale[ 1 ] * rest.scale[ 2 ];
                const float expect = std::fabs( bind_volume ) > 1.0e-6f
                    ? clip_volume / bind_volume : clip_volume;
                const float error = std::fabs( expect ) > 1.0e-6f
                    ? std::fabs( ratio / expect ) : 0.0f;
                if ( error > 0.9f && error < 1.1f )
                {
                    LOG_SUCCESS( "prop %-34s rigid by matrix: determinant x%.3f, clip scale implies x%.3f, translation %.1f %.1f %.1f",
                        item.asset.c_str( ), ratio, expect,
                        posed[ 12 ], posed[ 13 ], posed[ 14 ] );
                }
                else
                {
                    LOG_ERROR( "prop %-34s rigid BROKEN: determinant x%.3f but the clip scale implies x%.3f",
                        item.asset.c_str( ), ratio, expect );
                }
            }
            continue;
        }
        build_palette( palette, bones->second, clip->second, frame, nullptr, 0, 0.0f );
        const mesh_vertex_t* target = skin_geometry(
            item.geometry, mesh->second, bones->second, palette );

        // The hull's frame-0 round-trip does NOT transfer to props: a prop
        // clip is entitled to start away from its bind pose, and several
        // measurably do. What must hold for any correct pose, whether the
        // clip is a local deformation or a world path, is that it
        // PRESERVES THE MODEL'S SIZE - articulated motion moves a mesh
        // about, it does not inflate it. A mesh flung apart by a bad
        // palette blows the extent up by orders of magnitude, which is
        // exactly how the earlier mis-space showed itself.
        if ( target == nullptr || m_scored.count( item.asset ) != 0 )
            continue;
        // Score MID-CLIP, not on the first streamed frame. A
        // trigger-started prop gets its first frame the instant the event
        // fires, and for a clip whose frame 0 is the bind pose that frame
        // is the bind pose: the rockslide duly reported a perfect extent
        // ratio with 0 of 2002 vertices moved, which proves nothing about
        // motion. Waiting for the half-way frame makes the vertex count
        // meaningful for every prop, however it was started.
        const int cycle = frames > 0 ? frame % frames : 0;
        if ( frames > 2 && cycle != frames / 2 )
            continue;
        m_scored.insert( item.asset );

        float low[ 3 ]{}, high[ 3 ]{}, rest_low[ 3 ]{}, rest_high[ 3 ]{};
        for ( int axis = 0; axis < 3; ++axis )
        {
            low[ axis ] = rest_low[ axis ] = 1.0e30f;
            high[ axis ] = rest_high[ axis ] = -1.0e30f;
        }
        int moved = 0;
        for ( std::size_t v = 0; v < mesh->second.bind.size( ); ++v )
        {
            const mesh_vertex_t& rest = mesh->second.bind[ v ];
            for ( int axis = 0; axis < 3; ++axis )
            {
                const float posed = target[ v ].position[ axis ];
                if ( posed < low[ axis ] ) low[ axis ] = posed;
                if ( posed > high[ axis ] ) high[ axis ] = posed;
                if ( rest.position[ axis ] < rest_low[ axis ] ) rest_low[ axis ] = rest.position[ axis ];
                if ( rest.position[ axis ] > rest_high[ axis ] ) rest_high[ axis ] = rest.position[ axis ];
            }
            const float dx = target[ v ].position[ 0 ] - rest.position[ 0 ];
            const float dy = target[ v ].position[ 1 ] - rest.position[ 1 ];
            const float dz = target[ v ].position[ 2 ] - rest.position[ 2 ];
            if ( dx * dx + dy * dy + dz * dz > 1.0e-4f )
                ++moved;
        }
        float span = 0.0f;
        float rest_span = 0.0f;
        for ( int axis = 0; axis < 3; ++axis )
        {
            const float a = high[ axis ] - low[ axis ];
            const float b = rest_high[ axis ] - rest_low[ axis ];
            span += a * a;
            rest_span += b * b;
        }
        span = std::sqrt( span );
        rest_span = std::sqrt( rest_span );
        const float ratio = rest_span > 0.001f ? span / rest_span : 0.0f;
        // A rigid or articulated pose keeps the diagonal within a small
        // factor. Anything outside this is a broken palette, not a pose.
        if ( ratio > 0.5f && ratio < 3.0f )
        {
            LOG_SUCCESS( "prop %-34s posed: extent %.1f vs bind %.1f (x%.2f), %d of %u vertices moved",
                item.asset.c_str( ), span, rest_span, ratio, moved,
                static_cast< unsigned >( mesh->second.bind.size( ) ) );
        }
        else
        {
            LOG_ERROR( "prop %-34s BROKEN: extent %.1f vs bind %.1f (x%.2f) - the palette is wrong",
                item.asset.c_str( ), span, rest_span, ratio );
        }
    }
}

// Join the animated prop instances to the clips the event graph points at
// them. Actions and props sit in unrelated parts of the tree and either
// may be visited first, so the join has to happen after the whole walk.
inline void c_world::bind_animations( )
{
    // --stream-rigid-props forces the single-bone props back through CPU
    // skinning, so the saving from the placement-matrix path is a measured
    // A/B rather than an assertion.
    m_rigid_props = c_engine::option( "stream-rigid-props" ) == nullptr;
    const char* named = c_engine::option( "drive-to" );
    m_triggers.watch( named, c_engine::option( "drive" ) != nullptr || ( named != nullptr && named[ 0 ] != '\0' ) );
    int bound = 0;
    int loaded = 0;
    std::map< std::string, int > by_trigger;
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
        {
            item.timed = "VuTimedEventAsset/" + found->second.timed;
            if ( m_timed.find( item.timed ) == m_timed.end( ) )
            {
                std::vector< std::uint8_t > bytes;
                std::vector< timed_event_t > events;
                if ( m_archive.read( item.timed.c_str( ), bytes ) && timed_events_load( bytes, events ) )
                    m_timed.emplace( item.timed, std::move( events ) );
                else
                    LOG_WARNING( "timed events %s failed to load", item.timed.c_str( ) );
            }
        }
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

    int clones = 0;
    int rigid = 0;
    int streamed = 0;
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
        // A single-bone clip is a rigid motion, so the placement matrix
        // can carry it: one matrix write per part per frame instead of
        // transforming 1331 to 6604 vertices and re-uploading. These keep
        // the shared cached geometry - no clone, no streaming, and the
        // result is mathematically identical because the final transform
        // is just regrouped from (world*node)*(palette*v) to
        // (world*node*palette)*v.
        if ( usable )
        {
            item.rigid = bones->second.size( ) == 1 && m_rigid_props;
            ++( item.rigid ? rigid : clones );
            item.geometry = 0;   // skin_props' test: a pose is wanted
        }
        // Retail's instance (stage.hxx): the palette identity until it plays (0x532360).
        const std::size_t count = bones != m_skeletons.end( ) ? bones->second.size( ) : 0u;
        item.palette.assign( ( std::max )( count, std::size_t{ 1 } ) * 16, 0.0f );
        for ( std::size_t b = 0; b < item.palette.size( ) / 16; ++b )
            for ( std::size_t k = 0; k < 4; ++k )
                item.palette[ b * 16 + k * 5 ] = 1.0f;
        model_for( item.asset );
        if ( m_retail )
            if ( const gfx_scene_view_t* v = m_retail->model( item.asset ) )
            {
                item.retail = std::make_unique< gfx_animated_model_t >( );
                gfx_scene_view_attach( *item.retail, *v );
            }
    }
    LOG_INFO( "animated props: %d streaming %d vertices per frame, %d rigid by placement matrix (0 vertices)",
        clones, streamed, rigid );


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
