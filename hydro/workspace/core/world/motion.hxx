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
    m_joints.game_initialize( );
    for ( rotator_t& motion : m_rotators )
    {
        if ( motion.target != nullptr )
            motion.motion = m_joints.find_motion( motion.target );
        if ( motion.initially_active )
            rotator_activate( motion );
    }
}

// The instance of an animated prop that has attach records (joints.hxx), made on first use: 0x5EC300's model component loads "Model
// Asset" as the boat's does (world/skeleton.hxx skel_model_create), its clip the prop's VuPlayAnimationEntity's.
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
        std::vector< std::uint8_t > clip;
        if ( !item.clip.empty( ) && m_archive.read( item.clip.c_str( ), clip ) &&
             skel_animation_load( clip.data( ), clip.size( ), s->clip ) )
        {
            s->control.animation = &s->clip;
            s->has_clip = true;
        }
        skel_instance_t* i = &s->model.instance;
        m_prop_skels[ &e ] = std::move( s );
        return i;
    }
    return nullptr;
}

// The prop's pose at item.control.time: false with no instance / clip (UNKNOWN: the old path stands, counted).
inline bool c_world::prop_pose( const animated_t& item, std::vector< float >& palette )
{
    entity_t* owner = item.entity;
    skel_instance_t* instance = owner ? prop_instance( *owner ) : nullptr;
    if ( !instance )
        return false;
    prop_skel_t& s = *m_prop_skels[ owner ];
    if ( !s.has_clip )
        return false;
    if ( !s.added )
    {
        skel_animated_add( s.animated, &s.control );
        s.added = true;
    }
    s.control.time = item.control.time;
    if ( !skel_animated_build( s.animated ) || !skel_instance_set_pose( s.model.instance, &s.animated ) )
        return false;
    palette = s.model.instance.palette[ s.model.instance.current & 1 ];   // the one setPose wrote
    return true;
}

// VuAnimatedPropEntity's tick 0x5ED030 as the attach records see it: the pose (0x5169E0's time: the old path's control; 0x516CF0,
// 0x532720), then its attach component's slot 14 with the transform's world and zero velocities (0x5ED3BE), whether a clip plays or not.
inline void c_world::animated_attach_tick( )
{
    for ( auto& pair : m_prop_skels )
    {
        prop_skel_t& s = *pair.second;
        const std::string path = entity_path( *pair.first );
        for ( const animated_t& item : m_animated )
            if ( item.path == path && s.has_clip && item.playing )
            {
                if ( !s.added )
                {
                    skel_animated_add( s.animated, &s.control );
                    s.added = true;
                }
                s.control.time = item.control.time;
                if ( skel_animated_build( s.animated ) )
                    skel_instance_set_pose( s.model.instance, &s.animated );
            }
        const transform_component_t* t = entity_transform( *pair.first );
        if ( !t )
            continue;
        const float zero[ 4 ] = { };
        m_joints.parent_moved( *pair.first, t->world, zero, zero );
    }
}

// A child's motion IF slot 0 by class: VuCoronaEntity 0x5DC3B0 setWorldTransform( the matrix, notify 1 ), the corona's draw reads the
// world; VuStaticPfxEntity 0x5EB4C0 the world (0x534C00( 0 )) then its +0x94 system's setMatrix; VuAudioEmitterEntity 0x5F3D80. Any
// other class's is not read: UNKNOWN, not run, counted.
inline void c_world::motion_child( entity_t& child, const float* matrix, const float* linear, const float* angular )
{
    ( void ) angular;
    const std::string path = entity_path( child );
    if ( child.m_type == "VuCoronaEntity" )
    {
        if ( entity_set_world( child, matrix, true, { } ) )
            m_coronas.place( path, matrix );
        else
            ++m_motion_unknown;
        return;
    }
    if ( child.m_type == "VuStaticPfxEntity" )
    {
        if ( !entity_set_world( child, matrix, false, { } ) )
        {
            ++m_motion_unknown;
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
            ++m_motion_unknown;
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
    ++m_motion_unknown;
}
