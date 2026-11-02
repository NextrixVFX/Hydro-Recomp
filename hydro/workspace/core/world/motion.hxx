#pragma once

// c_world's motion - member definitions moved out of world.hxx (one class, split by concern).

// One of an entity's refs, as its load bound it: the target's path, or ""
// when the ref is unbound. The resolution is retail's (VuScriptRef's load
// 0x542830 / RefConnections 0x5049E0 through the path lookup 0x500F60, in
// the tree - engine entity/script.hxx); the template-relative rule that
// stood here was a measured inference, and matched it on all 219 bound refs
// of Hydroelectric before it was deleted.
inline std::string c_world::ref_target( const std::string& owner, const std::string& name ) const
{
    const auto found = m_refs.find( std::make_pair( owner, name ) );
    return found != m_refs.end( ) ? found->second : std::string( );
}

// The "Motion" tick 0x61A6E0 on each rotator (world/rotate.hxx), then the target's IF slot 0 with M1, linear 0 and the angular
// velocity. FLAGGED: run from the scene frame, not a tick manager's "Motion" phase; a rotator's order against the others' as loaded.
inline void c_world::update_rotators( float dt )
{
    for ( rotator_t& motion : m_rotators )
    {
        if ( !motion.active )
            continue;
        float matrix[ 16 ], angular[ 4 ];
        rotate_motion_tick( motion.value, dt, matrix, angular );
        const float linear[ 4 ] = { };
        attach_motion_t& m = motion.motion->m;
        std::memcpy( m.matrix, matrix, 64 );
        std::memcpy( m.linear, linear, 16 );
        std::memcpy( m.angular, angular, 16 );
        bool stepped = true;
        race_session_motion_update( m_race, *motion.target, stepped, matrix, linear, angular );
    }
}

// VuMotionEntity "Activate" 0x61A4F0 (keymotion.hxx key_motion_activate's shape): with a target, no latch and its +0x14 free: +0x14
// = the entity, the target's IF slot 1 (0x546D80 on a prop: its rigid component's flag - no siren holds a body; a body's is
// UNKNOWN here), active, One Shot -> latch, slot 12 0x5D0FE0, "OnActivated".
inline void c_world::rotator_activate( rotator_t& motion )
{
    if ( motion.motion == nullptr || motion.latch || motion.motion->m.parent != nullptr )
        return;
    motion.motion->m.parent = motion.entity;
    motion.active = true;
    if ( motion.one_shot )
        motion.latch = true;
    const transform_component_t* t = entity_transform( *motion.target );
    rotate_motion_enable( motion.value, t ? t->world : nullptr );
    fire( motion.path, "OnActivated" );
}

// "Deactivate" 0x61A5D0: when active: inactive, slot 13 (the nullsub), "OnDeactivated", +0x14 = 0, the target's IF slot 2.
inline void c_world::rotator_deactivate( rotator_t& motion )
{
    if ( !motion.active )
        return;
    motion.active = false;
    fire( motion.path, "OnDeactivated" );
    motion.motion->m.parent = nullptr;
}

// VuAttachEntity's slot 7 0x5F5520 / VuRotateMotionEntity's 0x61A4B0 (Deactivate; its tick unregistered: the scene frame's loop
// sees it inactive).
inline bool c_world::motion_release( const entity_t& e )
{
    if ( e.m_type == "VuAttachEntity" )
    {
        m_joints.plug_detach( &e );
        return true;
    }
    // VuAnimatedPropEntity's slot 7 0x5ECB40: Hide (vt+0x34, 0x5ECFE0: the draw off), 0x544A20 the rigid part (not built for an animated
    // prop, no shipped one authors it), its tick unregistered (vt+0x28 on g 0x759008).
    if ( e.m_type == "VuAnimatedPropEntity" )
    {
        for ( animated_t& item : m_animated )
            if ( item.entity == &e )
            {
                item.visible = false;
                item.playing = false;
            }
        return true;
    }
    if ( e.m_type == "VuRotateMotionEntity" )
    {
        for ( rotator_t& motion : m_rotators )
            if ( motion.entity == &e )
                rotator_deactivate( motion );
        return true;
    }
    return false;
}

// Slot 6 of the VuAttachEntity and VuRotateMotionEntity of the level (FLAGGED: at the end of the load, in the walk's order - each
// type's own list, not 0x500B10's single walk; the worlds they read are the loaded ones either way).
inline void c_world::motions_game_initialize( )
{
    m_joints.on_moved( [ this ]( entity_t& child, const float* matrix, const float* linear, const float* angular ) {
        motion_child( child, matrix, linear, angular );
    } );
    m_joints.on_instance( [ this ]( entity_t& parent ) { return prop_instance( parent ); } );
    m_joints.on_claim( [ this ]( entity_t& child, bool on ) {
        // VuWakeWaveEntity's IF slot 2 0x5D5870 (slot 1 the nullsub).
        const auto wake = m_wake_of.find( &child );
        if ( !on && wake != m_wake_of.end( ) )
            water_wake_entity_detach( m_water, wake->second );
        race_session_motion_claim( m_race, child, on );
    } );
    m_joints.game_initialize( );
    for ( rotator_t& motion : m_rotators )
    {
        if ( motion.target != nullptr )
            motion.motion = m_joints.find_motion( motion.target );
        if ( motion.initially_active )
            rotator_activate( motion );
    }
}

// The instance of an animated prop (the draw's palette, joints.hxx's attach records), made on first use: 0x5EC300's model component
// loads "Model Asset" as the boat's does (world/skeleton.hxx skel_model_create).
inline skel_instance_t* c_world::prop_instance( entity_t& e )
{
    const auto found = m_prop_skels.find( &e );
    if ( found != m_prop_skels.end( ) )
        return &found->second->model.instance;
    const std::string path = entity_path( e );
    for ( const animated_t& item : m_animated )
    {
        if ( item.path != path )
            continue;
        std::vector< std::uint8_t > blob;
        if ( !m_archive.read( ( "VuAnimatedModelAsset/" + item.asset ).c_str( ), blob ) )
            return nullptr;
        auto s = std::make_unique< prop_skel_t >( );
        if ( !skel_model_create( s->model, blob.data( ), blob.size( ) ) || s->model.instance.skeleton == nullptr )
            return nullptr;
        skel_animated_init( s->animated, *s->model.instance.skeleton );
        skel_instance_t* i = &s->model.instance;
        m_prop_skels[ &e ] = std::move( s );
        return i;
    }
    return nullptr;
}

// One of the prop's VuAnimations by its asset name, null when it does not load.
inline const skel_animation_t* c_world::prop_clip( prop_skel_t& s, const std::string& name )
{
    const auto found = s.clips.find( name );
    if ( found != s.clips.end( ) )
        return found->second.get( );
    std::vector< std::uint8_t > blob;
    auto clip = std::make_unique< skel_animation_t >( );
    if ( !m_archive.read( name.c_str( ), blob ) || !skel_animation_load( blob.data( ), blob.size( ), *clip ) )
    {
        LOG_WARNING( "prop clip %s failed to load", name.c_str( ) );
        return nullptr;
    }
    return s.clips.emplace( name, std::move( clip ) ).first->second.get( );
}

// VuAnimatedPropEntity's PlayAnimation plug 0x5ECB60, to the control's last advance (the caller fires OnAnimStart). Each of the three
// advances 0x516300 is skel_animated_advance's body on this one control. False: no instance, no clip or no params (UNKNOWN, counted).
inline bool c_world::prop_play_plug( animated_t& item, const std::uint8_t* params )
{
    if ( params == nullptr || item.entity == nullptr || prop_instance( *item.entity ) == nullptr )
        return false;
    const prop_play_params_t p = prop_params_read( params );
    if ( p.asset == 0 || p.asset > m_play_tokens.size( ) )
        return false;
    prop_skel_t& s = *m_prop_skels[ item.entity ];
    const skel_animation_t* clip = prop_clip( s, m_play_tokens[ p.asset - 1u ] );
    if ( clip == nullptr )
        return false;
    auto made = std::make_unique< skel_control_t >( );
    skel_control_t* c = made.get( );
    c->animation = clip;
    const std::string owner = item.path;
    const auto advance = [ & ]( float dt )
    {
        skel_animated_t alone;
        alone.controls.push_back( c );
        skel_animated_advance( alone, dt, [ & ]( int, const std::string& name, const value_t* carried ) {
            on_animation_event( owner, name, carried );
        } );
    };
    c->time = p.start;
    advance( 0.0f );
    c->factor = p.factor;
    c->looping = p.looping;
    advance( 0.0f );
    if ( p.blend > 0.0f )   // comiss against 0x6F7570 = 0
    {
        s.blend.on = true;
        s.blend.rate = 1.0f / p.blend;
        c->weight = 0.0f;
    }
    else
        skel_animated_clear( s.animated );
    skel_animated_add( s.animated, c );
    if ( p.timed != 0 && p.timed <= m_play_tokens.size( ) )
    {
        const auto timed = m_timed.find( m_play_tokens[ p.timed - 1u ] );
        c->events = timed != m_timed.end( ) ? &timed->second : nullptr;
    }
    c->listener = true;
    advance( p.advance );
    s.owned.push_back( std::move( made ) );
    return true;
}

// VuAnimatedPropEntity's tick 0x5ED030 up to the pose: the fade (world/playback.hxx), the advance 0x5169E0 through the listener 0x5ED680,
// 0x516CF0, 0x532720. False with no instance or a pose read UNKNOWN (the palette stays as it was).
inline bool c_world::prop_tick( const animated_t& item, float dt )
{
    const auto found = item.entity ? m_prop_skels.find( item.entity ) : m_prop_skels.end( );
    if ( found == m_prop_skels.end( ) )
        return false;
    prop_skel_t& s = *found->second;
    const std::string owner = item.path;
    prop_blend_step( s.animated, s.blend, dt );
    skel_animated_advance( s.animated, dt, [ & ]( int, const std::string& name, const value_t* params ) {
        on_animation_event( owner, name, params );
    } );
    const bool posed = skel_animated_build( s.animated ) && skel_instance_set_pose( s.model.instance, &s.animated );
    const std::vector< skel_control_t* >& live = s.animated.controls;
    s.owned.erase( std::remove_if( s.owned.begin( ), s.owned.end( ), [ & ]( const std::unique_ptr< skel_control_t >& c ) {
        return std::find( live.begin( ), live.end( ), c.get( ) ) == live.end( );
    } ), s.owned.end( ) );
    return posed;
}

// What 0x533C90 draws: the instance's current palette (identity until it plays, 0x532360), null with no instance.
inline const std::vector< float >* c_world::prop_palette( const animated_t& item )
{
    const skel_instance_t* i = item.entity ? prop_instance( *item.entity ) : nullptr;
    return i ? &i->palette[ i->current & 1 ] : nullptr;
}

// VuAnimatedPropEntity's tick 0x5ED030 after the pose (skin_props ran it): its attach component's slot 14 with the transform's world and
// zero velocities (0x5ED3BE), whether a clip plays or not.
inline void c_world::animated_attach_tick( )
{
    for ( auto& pair : m_prop_skels )
    {
        const transform_component_t* t = entity_transform( *pair.first );
        if ( !t )
            continue;
        const float zero[ 4 ] = { };
        m_joints.parent_moved( *pair.first, t->world, zero, zero );
    }
}

// One child's update not run: counted, and named once per child (the gate counts these lines; 0 is the only pass).
inline void c_world::motion_unread( const entity_t& child )
{
    ++m_motion_unknown;
    if ( m_motion_unread.insert( &child ).second )
        LOG_WARNING( "motion child not read: %s %s", child.m_type.c_str( ), entity_path( child ).c_str( ) );
}

// A child's motion IF slot 0 by class: VuCoronaEntity 0x5DC3B0 setWorldTransform( the matrix, notify 1 ), the corona's draw reads the
// world; VuStaticPfxEntity 0x5EB4C0 the world (0x534C00( 0 )) then its +0x94 system's setMatrix; VuAudioEmitterEntity 0x5F3D80; the
// props 0x546BD0; VuDynamicLightEntity 0x43FE40 setWorldTransform( the matrix, notify 1 ), its watcher 0x5EE870 (position, direction,
// 0x5C30D0); the power-ups 0x43BD90 (no watcher: the ctor 0x43B180 sets none) and VuPointWaveEntity 0x5D4C30 the same (none: 0x5D4C50
// reads the transform at the emit); VuWaterSurfaceEntity 0x5DC3B0 (the watcher its slot 12; the draw / layout boxes are the renderer's
// hook, nothing reads them here); VuWakeWaveEntity 0x5D5660. Any other class's is not read: UNKNOWN, not run, counted.
inline void c_world::motion_child( entity_t& child, const float* matrix, const float* linear, const float* angular )
{
    ( void ) angular;
    const std::string path = entity_path( child );
    if ( child.m_type == "VuCoronaEntity" )
    {
        if ( entity_set_world( child, matrix, true, { } ) )
            m_coronas.place( path, matrix );
        else
            motion_unread( child );
        return;
    }
    if ( child.m_type == "VuStaticPfxEntity" )
    {
        if ( !entity_set_world( child, matrix, false, { } ) )
        {
            motion_unread( child );
            return;
        }
        const auto it = m_static_pfx.find( path );
        if ( it != m_static_pfx.end( ) )
            m_pfx.set_matrix( it->second, matrix );
        return;
    }
    if ( child.m_type == "VuAudioEmitterEntity" && child.m_state )
    {
        if ( !entity_set_world( child, matrix, true, { } ) )
        {
            motion_unread( child );
            return;
        }
        sound_hooks_t h = m_sound;
        h.set_world = nullptr;   // done above (the same call, first)
        float record[ 20 ];
        std::memcpy( record, matrix, 64 );
        std::memcpy( record + 16, linear, 16 );
        audio_emitter_motion( static_cast< audio_emitter_state_t* >( child.m_state.get( ) )->value, record, h );
        return;
    }
    if ( child.m_type == "VuDynamicLightEntity" )
    {
        const bool ran = entity_set_world( child, matrix, true, [ & ]( ) {
            const auto light = m_light_of.find( path );
            const transform_component_t* t = entity_transform( child );
            if ( light == m_light_of.end( ) || !t || !m_retail )
                return;
            std::memcpy( light->second->m_position, t->world + 12, 12 );
            std::memcpy( light->second->m_direction, t->world + 4, 12 );
            m_retail->m_dynamic.update( *light->second );
        } );
        if ( !ran )
            motion_unread( child );
        return;
    }
    if ( child.m_type == "VuPowerUpBoostEntity" || child.m_type == "VuPowerUpHiddenPackageEntity" )
    {
        if ( !entity_set_world( child, matrix, true, { } ) )
            motion_unread( child );
        return;
    }
    if ( child.m_type == "VuPointWaveEntity" )
    {
        if ( !water_point_entity_motion( child, matrix ) )
        {
            motion_unread( child );
            return;
        }
        const auto wave = m_point_waves.find( path );
        if ( wave != m_point_waves.end( ) )
            std::memcpy( wave->second.position, matrix + 12, 16 );   // the tick's copy of the transform's +0x80, kept current for the emit
        return;
    }
    if ( child.m_type == "VuWaterSurfaceEntity" )
    {
        water_surface_boxes_t boxes;
        if ( !water_surface_motion( m_water, child, matrix, false, boxes ) )
            motion_unread( child );
        return;
    }
    if ( child.m_type == "VuWakeWaveEntity" )
    {
        const wake_wave_properties_t* p = entity_action< wake_wave_properties_t >( child );
        if ( !p || !entity_set_world( child, matrix, true, { } ) )
        {
            motion_unread( child );
            return;
        }
        std::uint32_t entity[ 14 ], motion[ 20 ];
        std::memcpy( entity, p->word, sizeof( entity ) );
        std::memcpy( motion, matrix, 64 );
        std::memcpy( motion + 16, linear, 16 );
        water_wake_entity_motion_water( m_water, entity, motion, m_wake_of.try_emplace( &child, -1 ).first->second );
        return;
    }
    // The props' IF 0x73A13C / VuRampEntity's 0x73B04C / VuAnimatedPropEntity's 0x756228: slot 0 is the same 0x546BD0 / 0x5ED4F0 a keyframe
    // target runs (ecx + 0xC's +0x18 = the attach's matrix): the world, the draw, the kinematic step of the body, its own attach children.
    if ( child.m_type == "VuPropEntity" || child.m_type == "VuRampEntity" || child.m_type == "VuBreakablePropEntity" || child.m_type == "VuAnimatedPropEntity" )
    {
        bool stepped = true;
        race_session_motion_update( m_race, child, stepped, matrix, linear, angular );
        return;
    }
    motion_unread( child );
}
