#pragma once

// c_world's graph - member definitions moved out of world.hxx (one class, split by concern).

// ---- the event graph -------------------------------------------------
//
// RETAIL MODEL, and it is small. VuScriptOutputPlug_fire @0x5023a0 is the
// WHOLE dispatcher in 138 bytes, reached as vtable slot 2 through the
// thunk @0x503270 after VuScriptComponent_findPlug @0x504770 looks the
// plug up by name. Connections are resolved ONCE at load - by
// VuScriptComponent's loads 0x503D70 / 0x504320, recreated in engine
// entity/script.hxx and verified (verify_script_graph) - and firing then
// just walks the resolved list calling slot 2 on every entry. this[20] /
// this[21] are the array bounds. m_graph is that list per plug, from the
// tree (load_entity_tree).
//
// this[9] == 0 is a BROADCAST: call every connection and discard the
// results. Non-zero calls ONLY THE FIRST and propagates its return value.
// this[9] is the plug's return type (+0x24, VuScriptOutputPlug_init
// @0x502080's second argument) - see plug_returns_value() below.
//
// DIRECTION IS NOT IN THE DATA, it is the ctor's: an INPUT plug is a
// VuScriptInputPlug (binds a VuMethod1 handler), an OUTPUT plug a
// VuScriptOutputPlug - captured per class (hydro game/plugs.hxx). Every
// handler below fires only names its ctor declares as outputs.

// Which plugs carry a VALUE: this[9] in VuScriptOutputPlug_fire is the
// plug's return type (+0x24), which its ctor passes to
// VuScriptOutputPlug_init @0x502080 - captured per class (game/plugs.hxx;
// VuIfEntity's "Operand" and VuBooleanEntity's "Get" are 3, the tag
// VuBoolean_onGet returns). 0 is void, which selects the broadcast half; a
// typed plug calls ONLY THE FIRST link and propagates its return value.
inline bool c_world::plug_returns_value( const std::string& type, const std::string& plug )
{
    for ( const script_plug_row_t& row : g_script_plug_rows )
        if ( type == row.m_class && plug == row.m_name )
            return row.m_type != 0;
    return false;
}

// VuScriptOutputPlug_fire @0x5023a0, both halves.
inline c_world::retval_t c_world::fire( const std::string& owner, const std::string& plug, int depth, const std::uint8_t* params )
{
    const auto found = m_graph.find( std::make_pair( owner, plug ) );
    if ( found == m_graph.end( ) || found->second.empty( ) )
        return retval_t{};
    const auto type = m_entity_type.find( owner );
    if ( type != m_entity_type.end( ) && plug_returns_value( type->second, plug ) )
        return dispatch( found->second.front( ).entity, found->second.front( ).plug, depth + 1, params );
    // The handler may edit m_graph-adjacent state but never m_graph
    // itself, so iterating the resolved list directly is safe.
    for ( const edge_t& edge : found->second )
        dispatch( edge.entity, edge.plug, depth + 1, params );
    return retval_t{};
}

// Slot 2 on an INPUT plug: invoke the VuMethod1 its entity's constructor
// bound to it. Each branch below is one such handler, transliterated.
// `params` reaches the handler (VuScriptOutputPlug_fire 0x5023A0 passes the
// sender's VuParams to every link). A handler below that fires its own output
// passes NONE on (null): whether each passes its input through is not read for
// it - only the race's nuisance plugs (race_event_dispatch) read params so far.
// DEVIATION, FLAGGED: the depth guard. Retail has none - VuOneShotFilter's
// In handler even sets its latch AFTER firing Out, so a cyclic graph
// recurses forever there too - and nothing in the authored graph is known
// to be cyclic, so the guard only bounds a bug.
inline c_world::retval_t c_world::dispatch( const std::string& entity, const std::string& plug, int depth, const std::uint8_t* params )
{
    if ( depth > 8 )
        return retval_t{};

    // VuPlayAnimationEntity. INPUT plug "Trigger", built by
    // VuPlayAnimationEntity_initTriggerPlug @0x5f6240 and handled by
    // VuPlayAnimationEntity_onTriggerPlug @0x5f5e80:
    //
    //   if ( !this->done )                       // byte +168
    //   {
    //       if ( this->oneShot ) this->done = 1; // byte +157
    //       if ( animationAsset->loaded )        // *(ref+76)
    //           fire( "PlayAnimation", params );
    //   }
    //   return VuRetVal( 0 );
    //
    // The params it builds carry the asset, Start Time (+144), Blend Time
    // (+148), Time Factor (+152) and Looping (+156): the PROP owns none of
    // that, which is why our play_t is indexed by the target's path and
    // copied onto the prop in bind_animations.
    // VuAttachEntity's two input plugs 0x5F5550 / 0x5F5690 (world/joints.hxx); each answers the empty VuRetVal.
    if ( const auto joint = m_entities.find( entity ); joint != m_entities.end( ) && m_joints.owns( joint->second ) )
    {
        if ( plug == "Attach" )
            m_joints.plug_attach( joint->second );
        else if ( plug == "Detach" )
            m_joints.plug_detach( joint->second );
        else
            count_unhandled( entity, plug );
        return retval_t{};
    }

    // VuRotateMotionEntity's (VuMotionEntity's) inputs "Activate" 0x61A4F0 / "Deactivate" 0x61A5D0 (motion.hxx).
    for ( rotator_t& motion : m_rotators )
        if ( motion.path == entity )
        {
            if ( plug == "Activate" )
                rotator_activate( motion );
            else if ( plug == "Deactivate" )
                rotator_deactivate( motion );
            else
                count_unhandled( entity, plug );
            return retval_t{};
        }

    // VuCoronaEntity's two input plugs. @0x5e8be0 and @0x5e8c30 register
    // and unregister the corona's draw component with the global
    // Vu3dDrawManager, guarded by the same +240 flag, and both return
    // VuRetVal( 0 ). A redundant Show or Hide does nothing at all.
    if ( m_coronas.owns( entity ) )
    {
        if ( plug == "Show" )
        {
            m_coronas.show( entity );
            return retval_t{};
        }
        if ( plug == "Hide" )
        {
            m_coronas.hide( entity );
            return retval_t{};
        }
        count_unhandled( entity, plug );
        return retval_t{};
    }

    // VuStaticPfxEntity's input plugs (ctor 0x5EAA40): Start 0x5EB200, Stop 0x5EB220, Kill 0x5EB250 on its +0x94
    // system (world/pfxstatic.hxx, via c_effects::plug); each returns the empty VuRetVal.
    const auto effect = m_static_pfx.find( entity );
    if ( effect != m_static_pfx.end( ) )
    {
        const int which = plug == "Start" ? 0 : plug == "Stop" ? 1 : plug == "Kill" ? 2 : -1;
        if ( which < 0 )
        {
            count_unhandled( entity, plug );
            return retval_t{};
        }
        m_pfx.plug( effect->second, which );
        ++m_fired;
        return retval_t{};
    }

    // The lights' game-start "Trigger" (world/settings.hxx, the water agent's - case settings_triggers): VuAmbientLightEntity
    // 0x5EF780 / VuDirectionalLightEntity 0x5EEEC0 write g_vulightmanager 0x759094's +0xCC .. +0xF8 (m_lights). The
    // manager's consumer is the renderer's (not recreated): OUR light setup reads its words - the hull shader's ambient /
    // front / back colours (each VuColor's R, G, B bytes / 255; the shader's light is two-sided), the sun direction +0xE0
    // (the transform's row 2 negated, retail's own), Specular (+0xF8) not consumed. With several lights of a kind the last
    // trigger wins (the manager holds one of each).
    const auto lt = plug == "Trigger" ? m_entities.find( entity ) : m_entities.end( );
    if ( lt != m_entities.end( ) && lt->second->m_state &&
         ( lt->second->m_type == "VuAmbientLightEntity" || lt->second->m_type == "VuDirectionalLightEntity" ) )
    {
        const entity_t& le = *lt->second;
        const auto to_rgb = []( std::uint32_t c, float* out ) {
            for ( int k = 0; k < 3; ++k )
                out[ k ] = static_cast< float >( ( c >> ( 8 * k ) ) & 0xFF ) / 255.0f;
        };
        if ( le.m_type == "VuAmbientLightEntity" )
        {
            ambient_light_trigger( static_cast< const ambient_light_state_t* >( le.m_state.get( ) )->value.color, m_lights );
            to_rgb( m_lights.ambient, m_rig.ambient( ) );
            m_rig.mark_lit( );
        }
        else
        {
            const transform_component_t* t = entity_transform( const_cast< entity_t& >( le ) );
            if ( !t || le.m_world_unknown )
            {
                count_unhandled( entity, plug );
                return retval_t{};
            }
            const directional_light_t& l = static_cast< const directional_light_state_t* >( le.m_state.get( ) )->value;
            std::uint32_t words[ 16 ];
            std::memcpy( words, t->world, 64 );
            directional_light_trigger( words, l.front_color, l.back_color, l.specular_color, m_lights );
            to_rgb( m_lights.front, m_rig.front( ) );
            to_rgb( m_lights.back, m_rig.back( ) );
            std::memcpy( m_rig.sun_world( ), t->world, 64 );
            float direction[ 3 ];
            std::memcpy( direction, m_lights.direction, 12 );
            c_engine::sun( direction );
            m_rig.mark_sun( );
            m_rig.mark_lit( );
        }
        if ( c_engine::option( "no-rig" ) == nullptr )
            c_engine::rig( m_rig.ambient( ), m_rig.front( ), m_rig.back( ) );
        ++m_fired;
        return retval_t{};
    }

    // VuSubstituteAssetEntity's "Trigger" 0x5F4BD0 (world/settings.hxx substitute_asset_trigger, the water agent's - case
    // settings_triggers) on the state its postLoad left (world/emitters.hxx): each asset-name property's +0x64 is the asset
    // factory's object for (Asset Type, name) when the pack holds it (doesAssetExist / createAsset: THE ASSET MANAGER'S, a
    // hook - ours is the archive lookup, one object per "Type/Name"); both present and different -> the asset's vt+0x10
    // (substitute, the asset class's - a hook: ours points the type's name at the other in m_rig's table and re-shades
    // every placement already shaded with that cube, as retail's materials hold the asset object itself).
    const auto sa = plug == "Trigger" ? m_entities.find( entity ) : m_entities.end( );
    if ( sa != m_entities.end( ) && sa->second->m_type == "VuSubstituteAssetEntity" && sa->second->m_state )
    {
        const substitute_asset_state_t& st = *static_cast< const substitute_asset_state_t* >( sa->second->m_state.get( ) );
        const auto object = [ this, &st ]( const std::string& name ) -> const void* {
            const std::string key = st.type + "/" + name;
            std::vector< std::uint8_t > bytes;
            if ( name.empty( ) || !m_archive.read( key.c_str( ), bytes ) )
                return nullptr;
            return &*m_asset_objects.insert( key ).first;
        };
        struct subst_t { c_world* w; const substitute_asset_state_t* st; };
        subst_t user{ this, &st };
        const void* asset = object( st.name );
        const void* with = object( st.subst );
        LOG_VERBOSE( "VuSubstituteAssetEntity %s Trigger: Asset Type '%s', Asset Name '%s' (%s), Subst Asset Name '%s' (%s)", entity.c_str( ),
                     st.type.c_str( ), st.name.c_str( ), asset ? "exists" : "no asset", st.subst.c_str( ), with ? "exists" : "no asset" );
        substitute_asset_trigger( asset, with, &user, []( void* u, const void*, const void* ) {
            subst_t& x = *static_cast< subst_t* >( u );
            x.w->m_rig.add_substitution( x.st->type + "/" + x.st->name, x.st->subst );
            int reshaded = 0;
            if ( x.st->type == "VuCubeTextureAsset" )
                for ( const shaded_t& sh : x.w->m_shaded )
                    if ( sh.cube == x.st->name )
                    {
                        c_engine::shade( sh.handle, x.w->cube_for( sh.cube ), sh.specular, sh.specular_amount, sh.environment_amount, sh.diffuse );
                        ++reshaded;
                    }
            LOG_INFO( "VuSubstituteAssetEntity Trigger: %s/%s -> %s (%d placements re-shaded)", x.st->type.c_str( ), x.st->name.c_str( ),
                      x.st->subst.c_str( ), reshaded );
        } );
        ++m_fired;
        return retval_t{};
    }

    // VuWaterTextureSettingsEntity's "Trigger" 0x5D2B00 (world/watchers.hxx water_texture_settings_apply, the water agent's -
    // verified there): with VuWater (g 0x759058 non-null - ours: true, c_world's m_water is built with every level; the
    // store to g 0x759058 is not read here, FLAGGED) the entity's +0x60 .. +0x7F (its
    // eight floats, ctor 0x5D2BD0's order) into the water renderer's settings (m_water_texture: the platform's, not read).
    const auto wt = plug == "Trigger" ? m_entities.find( entity ) : m_entities.end( );
    if ( wt != m_entities.end( ) && wt->second->m_type == "VuWaterTextureSettingsEntity" )
    {
        const water_texture_properties_t* p = entity_action< water_texture_properties_t >( *wt->second );
        if ( !p )
        {
            count_unhandled( entity, plug );
            return retval_t{};
        }
        static_assert( sizeof( water_texture_properties_t ) == 32, "+0x60 .. +0x7F" );
        std::uint32_t words[ 8 ];
        std::memcpy( words, p, 32 );
        water_texture_settings_apply( true, words, m_water_texture );
        m_water_texture_set = true;
        LOG_VERBOSE( "VuWaterTextureSettingsEntity %s Trigger: Gravity %.3f WorldSize %.3f WindSpeed %.3f DirectionalPower %.3f "
                     "SuppressionWaveLength %.3f HeightFactor %.3f TimeFactor %.3f NormalTextureScale %.3f", entity.c_str( ), p->gravity,
                     p->world_size, p->wind_speed, p->directional_power, p->suppression_wave_length, p->height_factor, p->time_factor,
                     p->normal_texture_scale );
        ++m_fired;
        return retval_t{};
    }

    // VuAudioEmitterEntity's inputs (world/sound.hxx, the water agent's - case audio_emitter): Start 0x5F3B80 (play at the
    // transform's world position, +0x80) and Stop 0x5F3BE0, on the entity's state (world/emitters.hxx) and the audio platform.
    const auto em = m_entities.find( entity );
    if ( em != m_entities.end( ) && em->second->m_type == "VuAudioEmitterEntity" && em->second->m_state )
    {
        audio_emitter_t& a = static_cast< audio_emitter_state_t* >( em->second->m_state.get( ) )->value;
        const transform_component_t* t = entity_transform( *em->second );
        if ( plug == "Start" && t && !em->second->m_world_unknown )
            audio_emitter_start( a, t->world + 12, m_sound );
        else if ( plug == "Stop" )
            audio_emitter_stop( a, m_sound );
        else
        {
            count_unhandled( entity, plug );
            return retval_t{};
        }
        ++m_fired;
        return retval_t{};
    }

    // VuSetFrontEndCameraEntity. "Trigger" is its ONE input plug, named
    // inside the init helper @0x409570 which binds
    // VuMethod1<VuSetFrontEndCameraEntity,VuRetVal,VuParams const&> to the
    // handler @0x408f90. That handler, entire:
    //
    //   if ( this->modifyEyePosition )    { params.add( eye    ); broadcast( "OnSetFrontEndCameraEyePosition",    params ); }
    //   if ( this->modifyTargetPosition ) { params.add( target ); broadcast( "OnSetFrontEndCameraTargetPosition", params ); }
    //   if ( this->modifyFarPlane )       { params.add( far    ); broadcast( "OnSetFrontEndCameraDrawDistance",   params ); }
    //                                       params.add( lerp   ); broadcast( "OnSetFrontEndCameraInterpolation",  params );
    //   return VuRetVal( 0 );
    //
    // Four separate engine-wide broadcasts, each name hashed with FNV-1a
    // (basis 0x811c9dc5, prime 16777619) and each carrying exactly ONE
    // parameter - the param count is re-zeroed before every add, so the
    // buffer is reused but never accumulates. The interpolation broadcast
    // is UNCONDITIONAL: there is no Modify boolean for it.
    //
    // The handler 0x408F90: world/background.hxx front_end_trigger (verified,
    // case front_end_trigger). The subscriber is VuBackground's rig while one
    // exists; a broadcast nobody subscribes to does nothing. UNKNOWN: the
    // vectors' w lanes (+0x74 / +0x84: the VuVector3 property's, not loaded
    // here) - written 0; the rig never reads them back into a camera
    // (front_rig_place / camera_view read x y z).
    const auto shot = m_front_end.find( entity );
    if ( shot != m_front_end.end( ) )
    {
        if ( plug == "Trigger" )
        {
            const front_end_camera_t& set = shot->second;
            front_end_trigger_t t;
            t.interpolation = set.interpolation_time;
            t.modify[ 0 ] = set.modify_eye ? 1 : 0;
            t.modify[ 1 ] = set.modify_target ? 1 : 0;
            t.modify[ 2 ] = set.modify_far ? 1 : 0;
            std::memcpy( t.eye, set.eye, sizeof( set.eye ) );
            std::memcpy( t.target, set.target, sizeof( set.target ) );
            t.far_plane = set.far_plane;
            front_end_trigger( t, [ this ]( std::uint32_t hash, const std::uint8_t* p ) {
                if ( background_event( m_background, hash, p ) )
                    ++m_fired;
            } );
            LOG_INFO( "event: %s . Trigger 0x408F90: eye %s target %s far %s interpolation %.3f -> %s", entity.c_str( ),
                      set.modify_eye ? "yes" : "no", set.modify_target ? "yes" : "no", set.modify_far ? "yes" : "no", set.interpolation_time,
                      m_background.rig ? "VuBackground's rig" : "no subscriber" );
            return retval_t{};
        }
        count_unhandled( entity, plug );
        return retval_t{};
    }

    const auto action = m_actions.find( entity );
    if ( action != m_actions.end( ) )
    {
        if ( plug == "Trigger" )
        {
            if ( action->second.done )
                return retval_t{};
            if ( action->second.one_shot )
                action->second.done = true;
            fire( entity, "PlayAnimation", depth );
            return retval_t{};
        }
        count_unhandled( entity, plug );
        return retval_t{};
    }

    // The receiving end of a "PlayAnimation" edge: VuAnimatedPropEntity's
    // INPUT plug "PlayAnimation", bound in its ctor @0x5ec300 to the
    // handler @0x5ecb60. That handler builds a NEW VuAnimationControl on
    // every call, so a prop that is already playing restarts - the old
    // `|| item.playing` skip here was invented. It then:
    //
    //   control+28 = Start Time; advance( 0 )          a no-op
    //   control+24 = Time Factor; +20 = Looping; advance( 0 )
    //   Blend Time <= 0 ? clear the skeleton's controls @0x517440
    //                   : this+126 = 1, this+128 = 1/Blend, weight 0
    //   add the control @0x5173d0; +36 = timed events; +40 = the prop
    //   advance( sixth param )                         0 from this sender
    //   fire( "OnAnimStart" )
    //
    // UNKNOWN, not reproduced: Blend Time. Its weight ramp lives in the
    // prop's tick @0x5ed030 and the pose blend in @0x516cf0; with a single
    // control the blend normalises by the control's own weight, so it
    // only shows as the bind pose until the first tick - but the case of
    // a prop with a previous clip still fading has not been read.
    bool started = false;
    for ( animated_t& item : m_animated )
    {
        if ( item.path != entity )
            continue;
        item.playing = true;
        // control.end / control.loop are the clip's own members, so they
        // are read from the clip where it is advanced, in skin_props().
        item.control.looping = item.looping;
        item.control.factor = item.factor;
        item.control.time = item.start;
        const auto timed = m_timed.find( item.timed );
        item.control.events = timed != m_timed.end( ) ? &timed->second : nullptr;
        ++m_fired;
        started = true;
        fire( entity, "OnAnimStart", depth );
        // The distance proves the ordering: a trigger that only fires
        // after the player has travelled cannot have fired at t = 0 on
        // the start grid.
        LOG_SUCCESS( "event: %s . %s fired after %.0f units driven, %s begins its clip",
            entity.c_str( ), plug.c_str( ), m_triggers.driven( ), item.asset.c_str( ) );
    }
    if ( started )
        return retval_t{};

    // VuPointWaveEntity. "Emit" is the INPUT plug, named inside
    // VuPointWaveEntity_initEmitPlug @0x5d4e60; "OnEmit" and "OnExpired"
    // are outputs the entity fires, so only "Emit" may start a ripple.
    const auto ripple = m_point_waves.find( entity );
    if ( ripple != m_point_waves.end( ) )
    {
        if ( plug == "Emit" )
        {
            // VuPointWaveEntity_tick 0x5D4C50 -> VuWater_createPointWave 0x528E10 (surface.hxx), in place of
            // c_ocean::emit: the wave goes into m_water's hold.
            const point_wave_source_t& source = ripple->second;
            const int wave = water_point_wave_emit( m_water, source.position, source.properties );
            // The wave's +0x8C callback is this entity's VuPointWaveCallbackIF (its OnExpired 0x5D4B60): kept so
            // the tick's expire hook can find the entity whose "OnExpired" plug it fires.
            if ( wave >= 0 )
                m_wave_owner[ wave ] = entity;
            ++m_fired;
            LOG_SUCCESS( "event: %s emitted point wave %d into the water", entity.c_str( ), wave );
            return retval_t{};
        }
        count_unhandled( entity, plug );
        return retval_t{};
    }

    // VuOneShotFilterEntity, all three handlers over the byte at this+96.
    const auto latch = m_filters.find( entity );
    if ( latch != m_filters.end( ) )
    {
        // VuOneShotFilter_onResetPlug @0x5decc0: this[96] = 0.
        if ( plug == "Reset" )
        {
            latch->second.fired = false;
            return retval_t{};
        }
        // VuOneShotFilter_onSetShotPlug @0x5dece0: this[96] = 1. It arms
        // the latch as ALREADY SHOT, so a later "In" does nothing - the
        // opposite of Reset, and nothing in the name says so.
        if ( plug == "SetShot" )
        {
            latch->second.fired = true;
            return retval_t{};
        }
        // VuOneShotFilter_onInPlug @0x5df000: if the byte is clear, fire
        // "Out" and THEN set it. The order is retail's, not a tidy-up.
        if ( plug == "In" )
        {
            std::uint8_t shot = latch->second.fired ? 1 : 0;
            one_shot_in( shot, [ & ] { fire( entity, "Out", depth, params ); } );
            latch->second.fired = shot != 0;
            return retval_t{};
        }
        count_unhandled( entity, plug );
        return retval_t{};
    }

    // VuTriggerEntity's own two INPUT plugs, declared by its ctor
    // @0x61c6a0 through VuScriptInputPlug_create @0x61cd90 and shared by
    // the plane and the box. VuTriggerEntity_onActivate @0x61ccb0 and
    // _onDeactivate @0x61cd20 flip the byte at this+132, which is what
    // puts the trigger into (or takes it out of) the manager's active
    // list; a trigger that is not in the list is never tested. Both guard
    // on the current state, so a redundant Activate does nothing.
    if ( plug == "Activate" || plug == "Deactivate" )
    {
        if ( m_triggers.set_active( entity, plug == "Activate" ) )
            return retval_t{};
    }
    else
    {
        for ( const trigger_t& gate : m_triggers.all( ) )
        {
            if ( gate.path != entity )
                continue;
            count_unhandled( entity, plug );
            return retval_t{};
        }
    }

    // VuDelayEntity. VuDelayEntity_onInPlug @0x5dfbe0:
    //
    //   if ( !this->pending )               // byte +104
    //   {
    //       this->pending  = 1;
    //       this->remaining = rand01() * randomWindow + delay;  // +108
    //       copy the incoming params into this+112 for the Out
    //   }
    //   return VuRetVal( 0 );
    //
    // A delay already counting down IGNORES a second In - the retrigger
    // is dropped, not restarted, and that is not what the name suggests.
    // rand01 is VuRandom_draw 0x50FD20 on THE global 0x736810 (ecx at
    // 0x5DFBF3) - c_world's m_vurandom, shared with the pfx and the AI.
    const auto wait = m_delays.find( entity );
    if ( wait != m_delays.end( ) )
    {
        if ( plug == "In" )
        {
            std::uint8_t pending = wait->second.pending ? 1 : 0;
            delay_in( pending, wait->second.remaining, wait->second.delay, wait->second.window, [ this ] { return m_vurandom.draw( ); } );
            wait->second.pending = pending != 0;
            return retval_t{};
        }
        if ( plug == "OnManualTick" )
        {
            std::int32_t size = 0;
            if ( params )
                std::memcpy( &size, params + 0x80, 4 );
            update_delay( wait->first, wait->second, delay_manual_dt( params, size ) );
            return retval_t{};
        }
        count_unhandled( entity, plug );
        return retval_t{};
    }

    // VuGenericEventEntity. The "Broadcast" input handler @0x5f0330 posts
    // the global "OnGenericEvent" carrying this entity's Event Name, and
    // every instance's hook @0x5f0420 fires its own "OnReceived" when the
    // carried name equals its own. Walking the name map IS that
    // comparison; the sender matches itself too, exactly as retail does.
    const auto event = m_events.find( entity );
    if ( event != m_events.end( ) )
    {
        if ( plug == "Broadcast" )
        {
            generic_event( event->second, depth );
            return retval_t{};
        }
        count_unhandled( entity, plug );
        return retval_t{};
    }

    // VuIfEntity, ctor @0x5ddbe0. INPUT "Trigger" (named in the init
    // helper @0x5e2670, handler VuIfEntity_onTriggerPlug @0x5dde60),
    // OUTPUTS "Operand" (return type 3), "True" and "False".
    //
    //   ret = fire( "Operand" );
    //   if ( ret.type == 3 ) fire( ret.value ? "True" : "False", params );
    //
    // The TYPE CHECK IS LOAD-BEARING: an Operand that answers with
    // anything other than a bool - including an unconnected Operand -
    // takes neither branch. It does not fall through to False.
    if ( m_ifs.count( entity ) != 0 )
    {
        if ( plug == "Trigger" )
        {
            const retval_t answer = fire( entity, "Operand", depth );
            if ( answer.type == 3 )
                fire( entity, answer.value ? "True" : "False", depth );
            return retval_t{};
        }
        count_unhandled( entity, plug );
        return retval_t{};
    }

    // VuBooleanEntity, ctor @0x5e5880. One property, "Value", on the byte
    // at +96. INPUTS "Set", "SetTrue" (@0x5e5790), "SetFalse" (@0x5e57b0),
    // "Toggle" (@0x5e57d0), "Get" (@0x5e57f0, return type 3); OUTPUTS
    // "OnChanged", "OnChangedTrue", "OnChangedFalse".
    const auto boolean = m_booleans.find( entity );
    if ( boolean != m_booleans.end( ) )
    {
        // VuBoolean_onGet: returns VuRetVal{ 3, value }. This is the only
        // handler in the engine so far that answers rather than acts.
        if ( plug == "Get" )
        {
            retval_t answer{};
            answer.type = 3;
            answer.value = boolean->second;
            answer.word = boolean->second ? 1u : 0u;
            return answer;
        }
        if ( plug == "SetTrue" )
            return set_boolean( entity, true, depth );
        if ( plug == "SetFalse" )
            return set_boolean( entity, false, depth );
        // VuBoolean_onToggle: setValue( value == 0 ).
        if ( plug == "Toggle" )
            return set_boolean( entity, !boolean->second, depth );
        // "Set" 0x5E5E10: the first parameter's byte when its tag is 3, else false.
        if ( plug == "Set" )
        {
            std::int32_t size = 0;
            if ( params )
                std::memcpy( &size, params + 0x80, 4 );
            return set_boolean( entity, boolean_set_value( params, size ) != 0, depth );
        }
        count_unhandled( entity, plug );
        return retval_t{};
    }

    // The race's own receivers (world/drive.hxx race_event_dispatch): the
    // gfx-settings entities' Trigger and the nuisance start's DriverTarget /
    // DriverDisable - built by the race session's gameInitialize walk.
    // The race HUD projects' (world/huds.hxx): VuMultiDelayEntity's "In" 0x5E0660 and VuStartLightsOutAction's "Trigger"
    // (VuGameActionEntity's input 0x406F40 -> 0x4077E3, vt+0x30 = 0x405470: the "OnStartLightsOut" broadcast, an empty
    // VuParams; VuRetVal 0).
    // The engine's logic entities (world/logic.hxx, verified against retail by
    // case logic_plugs): the input plug's VuMethod1 by (vtable, plug), on the
    // entity's tree state. Its outputs go back through fire() (0x5023A0) with
    // the handler's VuParams; VuRandomTriggerEntity draws THE global VuRandom
    // 0x736810 (m_vurandom - handed in and back around every fire, so nested
    // draws keep one sequence).
    const auto logic = m_logic.find( entity );
    if ( logic != m_logic.end( ) )
    {
        logic_state_t* ls = logic_state_of( *logic->second );
        struct context_t
        {
            c_world* world;
            const std::string* path;
            int depth;
            pfx_rand_t rand;
        } ctx{ this, &logic->first, depth, { } };
        static_assert( sizeof( pfx_rand_t ) == sizeof( fleet_random_t ), "one VuRandom image" );
        static_assert( sizeof( ui_params_t ) == 0x84, "a VuParams image" );
        std::memcpy( &ctx.rand, &m_vurandom, sizeof( ctx.rand ) );
        logic_hooks_t hooks;
        hooks.user = &ctx;
        hooks.rand = &ctx.rand;
        hooks.game = &m_logic_game;
        hooks.fire = []( void* user, logic_entity_t&, const char* out_plug, const ui_params_t& p ) -> logic_retval_t {
            context_t& c = *static_cast< context_t* >( user );
            std::memcpy( &c.world->m_vurandom, &c.rand, sizeof( c.rand ) );
            std::uint8_t bytes[ 0x84 ];
            std::memcpy( bytes, &p, sizeof( bytes ) );
            const retval_t r = c.world->fire( *c.path, out_plug, c.depth, bytes );
            std::memcpy( &c.rand, &c.world->m_vurandom, sizeof( c.rand ) );
            logic_retval_t out;
            out.type = static_cast< std::uint32_t >( r.type );
            out.value = r.word;
            out.text = r.text;
            return out;
        };
        ui_params_t in{ };
        if ( params )
            std::memcpy( &in, params, sizeof( in ) );
        logic_retval_t out{ };
        const bool ran = ls && logic_dispatch( ls->value, plug.c_str( ), out, in, hooks );
        std::memcpy( &m_vurandom, &ctx.rand, sizeof( ctx.rand ) );
        if ( !ran )
        {
            count_unhandled( entity, plug );
            return retval_t{};
        }
        ++m_fired;
        ++m_logic_fired;
        LOG_VERBOSE( "event: %s . %s ran (logic %s; %d unrecreated game-environment reads so far)", entity.c_str( ), plug.c_str( ),
                     logic->second->m_type.c_str( ), m_logic_unrecreated );
        retval_t answer{};
        answer.type = static_cast< int >( out.type );
        answer.word = out.value;
        answer.value = ( out.value & 0xFFu ) != 0u;
        answer.text = out.text;
        return answer;
    }

    const auto clock = m_multi_delays.find( entity );
    if ( clock != m_multi_delays.end( ) )
    {
        if ( plug == "In" )
            multi_delay_in( clock->second.state );
        else
            count_unhandled( entity, plug );
        return retval_t{};
    }
    if ( m_start_lights.count( entity ) != 0 )
    {
        if ( plug == "Trigger" )
            race_broadcast( "OnStartLightsOut" );
        else
            count_unhandled( entity, plug );
        return retval_t{};
    }
    if ( race_event_dispatch( m_race, entity, plug, params ) )
    {
        ++m_fired;
        return retval_t{};
    }

    // VuAiWaypointEntity's input plugs (ctor 0x431770; boat track.hxx) on the
    // track manager's waypoint the path names (its names below the project
    // root): "AllowRecovery" 0x432740 / "DisallowRecovery" 0x432760 set +0x8B
    // (what the respawn's track_nearest filters on); both return VuRetVal(0).
    // "UpdateBranchProbabilities" 0x4325D0 reads the event's VuParams (two
    // bools, two floats) - our events carry none, so it is UNKNOWN here
    // (counted, never run on made-up params); its replan, the AI manager's
    // slot 16 0x404220, is fleet_replan (race_drive_replan) once params arrive.
    if ( m_track_built && ( plug == "AllowRecovery" || plug == "DisallowRecovery" || plug == "UpdateBranchProbabilities" ) )
        for ( track_waypoint_t& w : m_track.waypoints )
        {
            std::string path;
            for ( const entity_t* e = w.entity; e && e->m_parent; e = e->m_parent )
                path = path.empty( ) ? e->m_name : e->m_name + "/" + path;
            if ( path != entity )
                continue;
            if ( plug == "UpdateBranchProbabilities" )
            {
                if ( ++m_waypoint_params_unknown == 1 )
                    LOG_WARNING( "event %s . UpdateBranchProbabilities 0x4325D0: its VuParams are not carried by our events - UNKNOWN, not run "
                                 "(further ones counted)", entity.c_str( ) );
                return retval_t{};
            }
            waypoint_allow_recovery( w, plug == "AllowRecovery" );
            return retval_t{};
        }

    // Everything else in the 813-node graph - keyframe motion, audio
    // emitters, pfx emitters, the other wave emitters - is not built yet.
    // Count it and say so once, so the gap stays visible instead of
    // events vanishing silently.
    count_unhandled( entity, plug );
    return retval_t{};
}

// VuBooleanEntity_setValue @0x5e5ea0. It fires NOTHING when the value is
// unchanged - the whole body sits inside `if ( a2 != this[96] )` - and
// when it does change it fires "OnChanged" first and then exactly one of
// "OnChangedTrue" / "OnChangedFalse".
inline c_world::retval_t c_world::set_boolean( const std::string& entity, bool value, int depth )
{
    const auto found = m_booleans.find( entity );
    if ( found == m_booleans.end( ) )
        return retval_t{};
    std::uint8_t held = found->second ? 1 : 0;
    boolean_set( held, value ? 1 : 0, [ & ]( const char* plug ) {
        found->second = held != 0;
        fire( entity, plug, depth );
    } );
    return retval_t{};
}

// The "OnGenericEvent" post (VuGenericEventEntity's Broadcast 0x5F0330, a clip's BroadcastGenericEvent 0x5ED680): every instance's
// handler 0x5F0420 on the VuParams the name made (a name that does not fit leaves them empty: instances with an empty name answer).
// FLAGGED: the subscribers' order is ours (the path map), retail's is the VuEventManager's.
inline void c_world::generic_event( const std::string& name, int depth )
{
    const ui_params_t p = generic_broadcast_params( name );
    for ( const auto& pair : m_events )
        if ( generic_receive( pair.second, p.m_bytes, p.m_size ) )
            fire( pair.first, "OnReceived", depth );
}

// One delay's 0x5DFC40.
inline void c_world::update_delay( const std::string& path, delay_t& wait, float dt )
{
    std::uint8_t pending = wait.pending ? 1 : 0;
    delay_update( pending, wait.remaining, dt, [ & ] {
        wait.pending = false;
        fire( path, "Out" );
    } );
}

// VuDelayEntity's countdown. VuDelayEntity_tick @0x5dfb50 is the method
// registered under "OnManualTick": it unpacks dt from the params - the
// first entry, and only when its type tag is 2, zero otherwise - and
// calls VuDelayEntity_update @0x5dfc40, which is exactly
//
//   if ( pending ) { remaining -= dt;
//                    if ( remaining < 0.0f ) { pending = false; fire( "Out" ); } }
//
// STRICTLY less than zero, so a zero-length delay still costs one tick.
// Firing inside the sweep is retail's own order: a delay restarted by an
// Out still counts down this frame if it is later in the walk. The walk
// is ours though - retail's is the tick registration (load) order and
// ours is alphabetical by path - so two delays wired to each other can
// resolve a frame apart. FLAGGED, no constant involved.
inline void c_world::update_delays( float dt )
{
    for ( auto& pair : m_delays )
        update_delay( pair.first, pair.second, dt );
}

// The Triggers tick 0x536460's trigger half in a race, after the instigator
// step (session.hxx race_session_triggers): every trigger in the manager's
// list (+0x10: the active ones - VuTriggerEntity +132; the list's ORDER is our
// load order, its registration order unread) gets its vt+0x30. A PLANE's is
// VuTriggerPlaneEntity_testCrossings 0x5D1570: for each instigator record in
// order, trigger_plane_crossing; a crossing is VuTriggerEntity_fireCrossing
// 0x61CB70(the instigator component's +8 entity, enter): VuParams built by
// 0x505150 - one entity parameter {11, the entity's hash} (size 8) - and fired
// on "OnEnter" / "OnExit". A BOX's vt+0x30 is VuTriggerBoxEntity_testCrossings
// 0x5D1AB0 (trigger_box_crossing), fired the same way. The +0x1C in-tick byte and the pending add / remove
// lists (Activate / Deactivate during the tick) are not modelled: our
// set_active applies at once (FLAGGED - 0x61CCB0 / 0x61CD20 unread for it).
inline void c_world::race_triggers( )
{
    for ( const trigger_t& gate : m_triggers.all( ) )
    {
        if ( !gate.active )
            continue;
        const std::string path = gate.path;
        for ( std::size_t k = 0; k < m_race.triggers.instigators.size( ); ++k )
        {
            const trigger_record_t r = m_race.triggers.instigators[ k ];
            const int crossed = gate.box ? trigger_box_crossing( gate.frame, gate.half, gate.mask, r.at )
                                         : trigger_plane_crossing( gate.frame, gate.half, gate.mask, r.at );
            if ( crossed < 0 )
                continue;
            std::uint8_t params[ 0x84 ] = { };
            const std::uint32_t type = 11, hash = race_entity_token( r.owner ), size = 8;
            std::memcpy( params, &type, 4 );
            std::memcpy( params + 4, &hash, 4 );
            std::memcpy( params + 0x80, &size, 4 );
            LOG_INFO( "race: %s crossed %s - %s", r.owner >= 0 && static_cast< std::size_t >( r.owner ) < m_race.boats.size( )
                          ? m_race.boats[ static_cast< std::size_t >( r.owner ) ]->name.c_str( ) : "?", path.c_str( ), crossed ? "OnEnter" : "OnExit" );
            fire( path, crossed ? "OnEnter" : "OnExit", 0, params );
        }
    }
}

inline void c_world::count_unhandled( const std::string& entity, const std::string& plug )
{
    const auto type = m_entity_type.find( entity );
    ++m_unhandled_by[ ( type == m_entity_type.end( ) ? std::string( "?" ) : type->second ) + " . " + plug ];
    if ( ++m_unhandled_events == 1 )
    {
        LOG_WARNING( "event %s . %s has no handler yet; further unhandled events are counted only",
            entity.c_str( ), plug.c_str( ) );
    }
}

// VuAnimatedPropEntity's VuAnimationEventIF slot 0 @0x5ed680 (the IDB
// calls it `..._load_EventName`, which is wrong). The control's walk calls
// it with each timed event's name and VUJB params, and with AnimDone /
// AnimLoop and no params when the clip ends or wraps.
inline void c_world::on_animation_event( const std::string& owner, const std::string& name, const value_t* params )
{
    // The first test compares min( length, 8 ) bytes against "AnimDone"
    // and then the lengths, so it is an exact match.
    if ( name == "AnimDone" )
    {
        fire( owner, "OnAnimDone" );
        return;
    }
    if ( name == "AnimLoop" )
    {
        fire( owner, "OnAnimLoop" );
        return;
    }
    if ( name == "BroadcastGenericEvent" )
    {
        // json "EventName", then the same global "OnGenericEvent" post as
        // VuGenericEventEntity's Broadcast @0x5f0330 - every instance whose
        // Event Name matches fires OnReceived. Retail posts only when the
        // name is non-empty (v26[4], the string's length).
        const value_t* named = params != nullptr ? find( *params, "EventName" ) : nullptr;
        const std::string carried = named != nullptr && named->kind == value_t::kind_t::text
            ? named->text : std::string( );
        if ( carried.empty( ) )
            return;
        generic_event( carried, 0 );
        return;
    }
    // UNBUILT, named rather than guessed: PlayAudioEvent (FMOD event at
    // json Pos through the prop's matrix and scale), PlayPfxModelSpace
    // (json PfxName/Pos/Rot), Show and Hide (the prop's vtable slots 12
    // and 13, @0x5ecf90 / @0x5ecfe0, unread). Any other name does nothing
    // in retail either.
    // PlayAudioEvent (world/sound.hxx prop_play_audio_event, the water agent's): json EventName / Pos through the prop's
    // transform +0x50 and its +0xB0 scale, on the audio platform.
    if ( name == "PlayAudioEvent" )
    {
        const auto e = m_entities.find( owner );
        const transform_component_t* t = e != m_entities.end( ) ? entity_transform( *e->second ) : nullptr;
        if ( !t || e->second->m_world_unknown )
        {
            count_unhandled( owner, name );
            return;
        }
        static const value_t none;
        prop_play_audio_event( params ? *params : none, t->world, t->scale, m_sound );
        ++m_fired;
        return;
    }
    if ( name == "PlayPfxModelSpace" || name == "Show" || name == "Hide" )
        count_unhandled( owner, name );
}

// Does an event sent to this plug reach the named prop? A side-effect-free
// mirror of dispatch(), for the wiring check below - the bench has no
// input, so the player never moves and never crosses a plane, which means
// a bench run alone can never exercise any of this.
inline bool c_world::reaches( const std::string& entity, const std::string& plug,
    const std::string& prop, int depth) const
{
    if ( depth > 8 )
        return false;
    if ( entity == prop )
        return true;
    const auto action = m_actions.find( entity );
    if ( action != m_actions.end( ) )
        return plug == "Trigger" && walks( entity, "PlayAnimation", prop, depth );
    const auto latch = m_filters.find( entity );
    if ( latch != m_filters.end( ) )
        return plug == "In" && walks( entity, "Out", prop, depth );
    if ( m_delays.count( entity ) != 0 )
        return plug == "In" && walks( entity, "Out", prop, depth );
    // An If takes one branch or the other, so either reaching the prop
    // makes it reachable.
    if ( m_ifs.count( entity ) != 0 )
        return plug == "Trigger" && ( walks( entity, "True", prop, depth )
            || walks( entity, "False", prop, depth ) );
    if ( m_booleans.count( entity ) != 0 )
    {
        if ( plug != "SetTrue" && plug != "SetFalse" && plug != "Toggle" )
            return false;
        return walks( entity, "OnChanged", prop, depth )
            || walks( entity, "OnChangedTrue", prop, depth )
            || walks( entity, "OnChangedFalse", prop, depth );
    }
    const auto event = m_events.find( entity );
    if ( event != m_events.end( ) && plug == "Broadcast" )
    {
        for ( const auto& pair : m_events )
        {
            if ( pair.second == event->second && walks( pair.first, "OnReceived", prop, depth ) )
                return true;
        }
    }
    return false;
}

inline bool c_world::animated_at( const std::string& path ) const
{
    for ( const animated_t& item : m_animated )
    {
        if ( item.path == path )
            return true;
    }
    return false;
}

// What the resolved graph actually holds, printed once per load. This is
// the measurement that says which plug names the unbuilt node kinds use -
// names that must be read, never guessed - and whether anything at all is
// wired to the entities we do handle. The nine point waves are called out
// by name because each one needs an "Emit" to arrive from somewhere.
inline void c_world::audit_graph( ) const
{
    std::map< std::string, int > waiting;
    for ( const auto& pair : m_graph )
    {
        for ( const edge_t& edge : pair.second )
        {
            const bool known = m_actions.count( edge.entity ) != 0
                || m_filters.count( edge.entity ) != 0
                || m_point_waves.count( edge.entity ) != 0
                || m_delays.count( edge.entity ) != 0
                || m_events.count( edge.entity ) != 0
                || m_ifs.count( edge.entity ) != 0
                || m_booleans.count( edge.entity ) != 0
                || animated_at( edge.entity );
            if ( known )
                continue;
            const auto type = m_entity_type.find( edge.entity );
            ++waiting[ ( type == m_entity_type.end( ) ? std::string( "?" ) : type->second )
                + " . " + edge.plug ];
        }
    }
    // Can anything actually reach each point wave? The bench never drives
    // far enough to cross the planes these hang off, so reachability is
    // the only check available without playing the course, and it is the
    // one that exercises the new node kinds end to end.
    int reachable = 0;
    for ( const auto& source : m_point_waves )
    {
        std::string via;
        for ( const trigger_t& gate : m_triggers.all( ) )
        {
            if ( walks( gate.path, "OnExit", source.first, 0 )
                || walks( gate.path, "OnEnter", source.first, 0 ) )
            {
                via = gate.path;
                break;
            }
        }
        for ( std::size_t i = 0; via.empty( ) && i < m_initialized.size( ); ++i )
        {
            if ( walks( m_initialized[ i ], "Trigger", source.first, 0 ) )
                via = m_initialized[ i ];
        }
        if ( !via.empty( ) )
        {
            ++reachable;
            LOG_SUCCESS( "point wave %s is reached from %s",
                source.first.c_str( ), via.c_str( ) );
            continue;
        }
        // Unreached: walk the chain BACKWARDS and print it, so the one
        // missing node kind is named instead of guessed at. Connections
        // are stored on both ends, so the reverse walk is the same map.
        LOG_WARNING( "point wave %s is reached from nothing built; upstream chain:",
            source.first.c_str( ) );
        std::set< std::pair< std::string, std::string > > seen;
        std::vector< std::pair< std::string, std::string > > front;
        front.emplace_back( source.first, "Emit" );
        for ( int depth = 0; depth < 6 && !front.empty( ); ++depth )
        {
            std::vector< std::pair< std::string, std::string > > next;
            for ( const auto& node : front )
            {
                for ( const auto& pair : m_graph )
                {
                    for ( const edge_t& edge : pair.second )
                    {
                        if ( edge.entity != node.first || edge.plug != node.second )
                            continue;
                        if ( !seen.insert( pair.first ).second )
                            continue;
                        const auto type = m_entity_type.find( pair.first.first );
                        LOG_WARNING( "   %*s%s . %s   [%s]", depth * 2, "",
                            pair.first.first.c_str( ), pair.first.second.c_str( ),
                            type == m_entity_type.end( ) ? "?" : type->second.c_str( ) );
                        next.push_back( pair.first );
                    }
                }
            }
            front.swap( next );
        }
    }
    LOG_INFO( "point waves: %d of %u reachable through the built node kinds",
        reachable, static_cast< unsigned >( m_point_waves.size( ) ) );
    // The refs, grouped by the owning class and ref name, so the census
    // says which constructors have to be read to consume them.
    std::map< std::string, int > by_ref;
    for ( const auto& pair : m_refs )
    {
        const auto type = m_entity_type.find( pair.first.first );
        ++by_ref[ ( type == m_entity_type.end( ) ? std::string( "?" ) : type->second )
            + " . " + pair.first.second ];
    }
    for ( const auto& pair : by_ref )
        LOG_INFO( "refs: %4d %s", pair.second, pair.first.c_str( ) );
    // THE ROTATOR AUDIT: every motion entity's "Entity" ref, as the script load bound it.
    int absolute = 0, relative = 0, unresolved = 0, missing = 0;
    for ( const rotator_t& motion : m_rotators )
    {
        const auto ref = m_refs.find( std::make_pair( motion.path, "Entity" ) );
        if ( ref == m_refs.end( ) )
        {
            ++missing;
            LOG_WARNING( "rotator %s has no Entity ref", motion.path.c_str( ) );
            continue;
        }
        const std::string target = ref_target( motion.path, "Entity" );
        if ( m_entity_type.count( target ) == 0 )
        {
            ++unresolved;
            LOG_WARNING( "rotator %s: ref \"%s\" resolves to nothing",
                motion.path.c_str( ), ref->second.c_str( ) );
        }
        else if ( target == ref->second )
            ++absolute;
        else
            ++relative;
    }
    LOG_INFO( "rotator refs: %u rotators - %d absolute, %d template-relative, %d unresolved, %d without a ref",
        static_cast< unsigned >( m_rotators.size( ) ),
        absolute, relative, unresolved, missing );
    // THE CORONA CENSUS. The two draw branches we could not read out of
    // our own renderer - the screen-space sprite size and the roll's
    // screen-x term - only matter for coronas that author them, so say how
    // many do instead of leaving the substitution unmeasured. A corona
    // with no Texture Name has nothing to draw and is a parse failure, not
    // a feature gap.
    int screen_space = 0;
    int rolling = 0;
    int spinning = 0;
    int untextured = 0;
    int hidden = 0;
    for ( const c_corona_entity::corona_t& light : m_coronas.lights( ) )
    {
        if ( light.screen_space )
            ++screen_space;
        if ( light.rotation_amount != 0.0f )
            ++rolling;
        if ( light.rotation_speed != 0.0f )
            ++spinning;
        if ( light.texture.empty( ) )
            ++untextured;
        if ( !light.visible )
            ++hidden;
    }
    LOG_INFO( "coronas: %d placed - %d screen-space sized, %d roll with screen x, %d sweep a cone, %d start hidden, %d without a Texture Name",
        m_coronas.total( ), screen_space, rolling, spinning, hidden, untextured );
    // Where each one actually sits, with the two numbers that decide
    // whether it is ever on screen. A corona is only PROVEN once one is
    // seen, and the attach children cannot be in the right place until the
    // attach transform lands, so the positions are listed rather than
    // summarised.
    for ( const c_corona_entity::corona_t& light : m_coronas.lights( ) )
        LOG_INFO( "corona %s at %.0f %.0f %.0f - draw %.0f fade %.0f size %.1f cone %.0f deg tex %s",
            light.path.c_str( ), light.world[ 12 ], light.world[ 13 ], light.world[ 14 ],
            light.draw_distance, light.fade_distance, light.size,
            light.cone * 57.29578f, light.texture.c_str( ) );
    LOG_INFO( "attach: %u VuAttachEntity, %d records on their parents' components, %d updates of an animated parent not run (UNKNOWN), %d motion children of a class not read",
        static_cast< unsigned >( m_joints.joins( ).size( ) ), m_joints.attached( ), m_joints.unknown( ), m_motion_unknown );
    LOG_INFO( "template overrides: %d children overridden by their instance, %d of them carrying the instance's transform",
        m_overrides_seen, m_overrides_placed );
    for ( const auto& pair : m_override_components )
        LOG_INFO( "  override component %-28s x%d (read from the template, not the instance)",
            pair.first.c_str( ), pair.second );
    // WHAT EACH ATTACH ACTUALLY JOINS. VuAttachEntity's own semantics are
    // read (see memory/audit.md), but whether an attach is VISIBLE here
    // depends on what the Child is: a model's placements can be driven,
    // an audio emitter or a pfx emitter cannot until those subsystems
    // exist. Printing both sides' classes is what decides the scope.
    std::map< std::string, int > joins;
    for ( const c_joints::join_t& join : m_joints.joins( ) )
        ++joins[ ( join.child ? join.child->m_type : std::string( "?" ) ) + " -> " + ( join.parent ? join.parent->m_type : std::string( "?" ) )
            + ( join.properties->node_name.empty( ) ? std::string( " (no node)" ) : std::string( " (node)" ) ) ];
    for ( const auto& pair : joins )
        LOG_INFO( "attach: %4d %s", pair.second, pair.first.c_str( ) );
    // Every volume's own outgoing edges. These are the only entry points
    // the player can trip, so what they reach bounds what the graph can
    // ever do without a timed event or a global broadcast.
    for ( const trigger_t& gate : m_triggers.all( ) )
    {
        for ( const char* plug : { "OnEnter", "OnExit" } )
        {
            const auto found = m_graph.find( std::make_pair( gate.path, plug ) );
            if ( found == m_graph.end( ) )
                continue;
            for ( const edge_t& edge : found->second )
            {
                const auto type = m_entity_type.find( edge.entity );
                LOG_INFO( "%s %s . %s -> %s . %s [%s]", gate.box ? "box " : "plane",
                    gate.path.c_str( ), plug, edge.entity.c_str( ), edge.plug.c_str( ),
                    type == m_entity_type.end( ) ? "?" : type->second.c_str( ) );
            }
        }
    }
    for ( const auto& pair : waiting )
        LOG_INFO( "graph: %4d edges land on %s", pair.second, pair.first.c_str( ) );
}

// The side-effect-free mirror of fire().
inline bool c_world::walks( const std::string& owner, const std::string& plug,
    const std::string& prop, int depth ) const
{
    const auto found = m_graph.find( std::make_pair( owner, plug ) );
    if ( found == m_graph.end( ) )
        return false;
    for ( const edge_t& edge : found->second )
    {
        if ( reaches( edge.entity, edge.plug, prop, depth + 1 ) )
            return true;
    }
    return false;
}

// For each prop still waiting on a trigger, confirm some volume actually
// reaches it through the graph. A prop wired to nothing would sit at its
// bind pose forever and look merely static rather than broken.
//
// This is the wiring half of what used to be one test_triggers; the
// geometry half moved to c_trigger_set, which can check itself without
// knowing anything about props or the graph.
inline void c_world::test_wiring( ) const
{
    for ( const animated_t& item : m_animated )
    {
        if ( item.playing || item.clip.empty( ) )
            continue;
        bool wired = false;
        std::string via;
        for ( const trigger_t& gate : m_triggers.all( ) )
        {
            if ( !walks( gate.path, "OnExit", item.path, 0 )
                && !walks( gate.path, "OnEnter", item.path, 0 ) )
                continue;
            wired = true;
            via = gate.path;
            break;
        }
        // A prop that no plane reaches is not necessarily unwired: most of
        // them hang off the load-time event instead.
        for ( std::size_t i = 0; !wired && i < m_initialized.size( ); ++i )
        {
            if ( !walks( m_initialized[ i ], "Trigger", item.path, 0 ) )
                continue;
            wired = true;
            via = m_initialized[ i ] + " (project init)";
        }
        if ( wired )
        {
            LOG_SUCCESS( "trigger wiring: %-34s waits on %s",
                item.asset.c_str( ), via.c_str( ) );
        }
        else
        {
            LOG_WARNING( "trigger wiring: %-34s is reachable from nothing built (%s)",
                item.asset.c_str( ), item.trigger.c_str( ) );
        }
    }
}
