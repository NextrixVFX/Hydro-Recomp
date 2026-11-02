#pragma once
#include <array>

#include <workspace/core/assets/archive.hxx>
#include <workspace/core/assets/model.hxx>
#include <workspace/core/assets/skin.hxx>
#include <workspace/core/entity/bind.hxx>
#include <workspace/core/entity/schema.hxx>
#include <workspace/core/ui/overlay.hxx>
#include <workspace/core/light/rig.hxx>
#include <workspace/core/trigger/set.hxx>
#include <workspace/core/assets/reader.hxx>
#include <workspace/core/assets/tree.hxx>
#include <workspace/core/game/input.hxx>
#include <workspace/core/world/control.hxx>
#include <workspace/core/world/effects.hxx>
#include <workspace/core/world/pfx.hxx>
#include <workspace/core/entity/light.hxx>
#include <workspace/core/entity/node.hxx>
#include <workspace/core/game/types.hxx>
#include <workspace/core/world/kernel.hxx>
#include <workspace/core/world/grid.hxx>
#include <workspace/core/world/livery.hxx>
#include <workspace/core/world/race.hxx>
#include <workspace/core/world/strings.hxx>
#include <workspace/core/world/session.hxx>
#include <workspace/core/world/joints.hxx>
#include <workspace/core/world/latch.hxx>
#include <workspace/core/world/build.hxx>
#include <workspace/core/world/drive.hxx>
#include <workspace/core/world/water.hxx>
#include <workspace/core/world/watchers.hxx>
#include <workspace/core/world/background.hxx>
#include <workspace/core/world/action.hxx>
#include <workspace/core/world/settings.hxx>
#include <workspace/core/world/sorted.hxx>
#include <workspace/core/world/hudpass.hxx>
#include <workspace/core/world/mode.hxx>
#include <workspace/core/world/foliage.hxx>
#include <workspace/core/assets/texture.hxx>
#include <workspace/utility/logger.hxx>
#include <workspace/utility/trace.hxx>
#include <workspace/utility/math.hxx>

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#if defined( _WIN32 )
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <Windows.h>
#endif

// Screens/Background. Static models keep the transforms from their
// VuTransformComponent, and the front-end camera is the one stored on
// VuSetFrontEndCameraEntity.
class c_world
{
public:
    c_world( )
    {
        m_eye[ 0 ] = 0.0f;
        m_eye[ 1 ] = -40.0f;
        m_eye[ 2 ] = 18.0f;
        m_target[ 0 ] = 0.0f;
        m_target[ 1 ] = 0.0f;
        m_target[ 2 ] = 4.0f;
    }

    const float* eye( ) const { return m_eye; }
    const float* target( ) const { return m_target; }
    float far_plane( ) const { return m_far; }

private:
    // A VuRotateMotionEntity (world/rotate.hxx): slot 6 0x61A420 finds the "Entity" ref's motion component and, with "Initially
    // Active", runs "Activate" 0x61A4F0; the "Motion" tick 0x61A6E0 runs slot 14 while active.
    struct rotator_t
    {
        std::string path;
        entity_t* entity = nullptr;
        entity_t* target = nullptr;              // the ref's entity
        c_joints::motion_t* motion = nullptr;    // +0x68
        rotate_motion_t value;
        bool initially_active = false, one_shot = false;
        bool active = false, latch = false;      // +0x6C / +0x6D
    };

    struct cached_part_t
    {
        int geometry = -1;
        int texture = -1;
        int detail = -1;
        int index_start = 0;
        int index_count = 0;
        int alpha_test = 0;
        int shader = -1;   // the part's material (model_part_t::shader) - a skin's index
        int layer = -1;    // model_part_t's type, shading, DiffuseAlpha and aabb
        int shading = 0;
        float diffuse_alpha = 1.0f;
        bool has_box = false;
        float box[ 8 ]{};
        float node[ 16 ]{};
        // The authored shading constants, carried from the model's material
        // table through to c_engine::shade(). The cube is kept as a NAME and
        // resolved at place time, not here: the model cache is filled during
        // the entity walk, and a model loaded before the scene's
        // VuSubstituteAssetEntity is visited would resolve Proxy_cube to the
        // placeholder asset - which really exists in the pack, so the wrong
        // answer would succeed silently instead of erroring.
        std::string cube;
        float specular[ 3 ]{ 1.0f, 1.0f, 1.0f };
        // The material's DiffuseColor. The retail pixel shader folds it into
        // the lit term, so it tints every material, not just reflective ones.
        float diffuse[ 3 ]{ 1.0f, 1.0f, 1.0f };
        float specular_amount = 0.0f;
        float environment_amount = 0.0f;
    };

    // pack_path: models.hxx
    std::string pack_path( ) const;

    // texture_for: models.hxx
    int texture_for( const std::string& path );

    // cube_for: models.hxx
    int cube_for( const std::string& path );

    // The drawable parts of a geom pattern's model, in the small form c_pfx
    // asks for. A VuPfxGeomPattern draws a static model per particle, and the
    // model cache lives here, so the pfx system gets it through the same kind
    // of callback it already uses for texture ids.
    struct pfx_part_t
    {
        int geometry = -1;
        int texture = -1;
        int detail = -1;
        int index_start = 0;
        int index_count = 0;
        float node[ 16 ]{};
    };

    // pfx_parts: models.hxx
    const std::vector< pfx_part_t >& pfx_parts( const std::string& asset );

    // model_for: models.hxx
    const std::vector< cached_part_t >& model_for( const std::string& asset );

    // place_model: models.hxx
    // `skin`: per shader, what a skin pass (world/livery.hxx) changed for the draw (livery_draw_of).
    void foliage_build( const entity_t& root );
    void foliage_frame( );
    void place_model( const std::string& asset, const float* world, int bob = 0, int alpha_override = -1, const float* tint = nullptr, const std::vector< livery_draw_t >* skin = nullptr, int geometry_override = -1, std::vector< int >* out_placements = nullptr, std::vector< std::array< float, 16 > >* out_nodes = nullptr );

    const value_t* template_root( const std::string& name )
    {
        auto existing = m_templates.find( name );
        if ( existing == m_templates.end( ) )
        {
            std::vector< std::uint8_t > bytes;
            value_t parsed{};
            const std::string path = "VuTemplateAsset/" + name;
            if ( m_archive.read( path.c_str( ), bytes ) )
                parsed = c_reader( bytes ).parse( );
            else
                LOG_ERROR( "template %s", path.c_str( ) );
            existing = m_templates.emplace( name, std::move( parsed ) ).first;
        }

        const value_t* project = find( existing->second, "VuProject" );
        return project != nullptr ? find( *project, "RootEntity" ) : nullptr;
    }

    // Every property key a level actually authors, per entity class. This is
    // the input side of the silent-drop audit: what the data offers. What the
    // code consumes is the other side, and the gap between them is the list of
    // authored values this engine currently throws away.
    void record_authored( const std::string& type, const value_t* block )
    {
        if ( block == nullptr || block->kind != value_t::kind_t::object )
            return;
        std::set< std::string >& keys = m_authored_keys[ type ];
        for ( const auto& pair : block->object )
            keys.insert( pair.first );
    }

    // Report it against the retail schema. A key the schema does not declare
    // means the schema is incomplete for that class - the emulator could not
    // finish its constructor, or the class is not in the table at all - and
    // that is worth knowing before anything is built on top of it.
    void audit_authored( ) const
    {
        int classes_known = 0;
        int classes_unknown = 0;
        int keys_declared = 0;
        int keys_undeclared = 0;
        for ( const auto& pair : m_authored_keys )
        {
            const entity_schema_t* schema = entity_schema_find( pair.first.c_str( ) );
            if ( schema == nullptr )
            {
                ++classes_unknown;
                LOG_VERBOSE( "authored: %-34s %2u keys, NO SCHEMA",
                    pair.first.c_str( ), static_cast< unsigned >( pair.second.size( ) ) );
                continue;
            }
            ++classes_known;
            std::string missing;
            for ( const std::string& key : pair.second )
            {
                if ( entity_schema_property( schema, key.c_str( ) ) != nullptr )
                {
                    ++keys_declared;
                    continue;
                }
                ++keys_undeclared;
                missing += ( missing.empty( ) ? "" : ", " ) + key;
            }
            if ( !missing.empty( ) )
            {
                LOG_VERBOSE( "authored: %-34s declares %d, NOT in schema: %s",
                    pair.first.c_str( ), schema->m_count, missing.c_str( ) );
            }
        }
        LOG_INFO( "authored keys: %d classes with a schema, %d without; %d keys declared, %d not",
            classes_known, classes_unknown, keys_declared, keys_undeclared );
    }

    // VuSetFrontEndCameraEntity, read from its constructor @0x408b00 and its
    // one input plug's handler @0x408f90. All seven properties and all four
    // defaults are the constructor's own: Interpolation Time 0x40000000 = 2.0
    // at +0x60, the three Modify booleans FALSE at +0x64/+0x65/+0x66, Eye and
    // Target Position zero at +0x68/+0x78, Far Plane 0x447A0000 = 1000 at
    // +0x88.
    //
    // THE ENTITY DOES NOTHING AT LOAD. @0x408f90 is the whole of its
    // behaviour, and it runs only when the "Trigger" input plug fires.
    //
    // The members are VuSetFrontEndCameraEntity's loaded targets (engine
    // entity/actions.hxx, from the retail tree).
    struct front_end_camera_t
    {
        float interpolation_time = 0.0f;
        bool modify_eye = false;
        bool modify_target = false;
        bool modify_far = false;
        float eye[ 3 ]{};
        float target[ 3 ]{};
        float far_plane = 0.0f;
    };

    // The remaining front-end camera path - the screen-to-screen blend - was
    // inferred and is banked in reference/shot.hxx.

    // A type that starts with '#' is a VuTemplateAsset. The instance transform
    // and properties replace the template root; the template supplies the entity.
    // `trail` is the entity's absolute path, built from the "name" field that
    // sits BESIDE "type" and "data" rather than inside the Properties. The
    // event graph addresses entities by exactly this path - a
    // VuPlayAnimationEntity names its target as, for instance,
    // "Hydroelectric/Terrain/zone06 folder/Props/Helicopter stuff/Helicopter"
    // - so the path has to be carried down the walk to resolve any of it.
    void visit( const value_t& entity, const float* parent, bool use_local = true,
        const value_t* overlay = nullptr, const std::string& trail = std::string( ) )
    {
        const value_t* data = find( entity, "data" );
        const value_t* label = find( entity, "name" );
        const std::string here = label != nullptr && label->kind == value_t::kind_t::text
            && !label->text.empty( )
            ? ( trail.empty( ) ? label->text : trail + "/" + label->text )
            : trail;
        // A TEMPLATE INSTANCE OVERRIDES ITS CHILDREN BY NAME. The instance
        // node's "ChildEntities" is an OBJECT keyed by child name - not the
        // LIST an ordinary entity carries - and each entry holds "Components"
        // and "Properties" DIRECTLY, with no "data" wrapper. The template
        // definition supplies the structure; the instance supplies the
        // placement. Measured on the WarningSiren templates, whose thirteen
        // instances carry the only transform in the whole subtree.
        const value_t* override_node = m_pending_override;
        m_pending_override = nullptr;
        const value_t* over_components = override_node != nullptr
            ? find( *override_node, "Components" ) : nullptr;
        if ( override_node != nullptr )
        {
            ++m_overrides_seen;
            if ( over_components != nullptr && find( *over_components, "VuTransformComponent" ) != nullptr )
                ++m_overrides_placed;
            // Every component key an override carries that is NOT the
            // transform. The transform is merged; anything else here is read
            // from the TEMPLATE's copy, so this census is the measure of what
            // that costs.
            if ( over_components != nullptr )
                for ( const auto& pair : over_components->object )
                    if ( pair.first != "VuTransformComponent" )
                        ++m_override_components[ pair.first ];
        }
        if ( override_node != nullptr && overlay == nullptr )
        {
            const value_t* over_properties = find( *override_node, "Properties" );
            if ( over_properties != nullptr )
                overlay = over_properties;
        }

        const value_t* components = data != nullptr ? find( *data, "Components" ) : nullptr;
        float local[ 16 ]{};
        math_identity( local );
        if ( use_local )
        {
            // The instance's own transform wins over the template's.
            const value_t* transform = over_components != nullptr
                ? find( *over_components, "VuTransformComponent" ) : nullptr;
            if ( transform == nullptr && components != nullptr )
                transform = find( *components, "VuTransformComponent" );
            const value_t* properties = transform != nullptr ? find( *transform, "Properties" ) : nullptr;
            if ( properties != nullptr && properties->kind == value_t::kind_t::object && !properties->object.empty( ) )
                read_transform( *properties, local );
        }
        float world[ 16 ]{};
        math_multiply( world, parent, local );

        // Every entity's placed transform, by path. An attach composes the
        // child onto its PARENT ENTITY, not onto a placement, and the parent
        // is named by a ref that may resolve to an entity this walk has not
        // reached yet - so the matrix has to be recorded as it is computed.
        if ( !here.empty( ) )
        {
            std::array< float, 16 > placed{};
            std::memcpy( placed.data( ), world, sizeof( world ) );
            m_entity_world[ here ] = placed;
        }

        // The script connections and refs come from the retail tree
        // (load_entity_tree; engine entity/script.hxx).

        const value_t* type = find( entity, "type" );
        if ( type != nullptr && type->kind == value_t::kind_t::text && type->text.size( ) > 1 && type->text[ 0 ] == '#' )
        {
            const std::string name = type->text.substr( 1 );
            if ( m_stack.insert( name ).second )
            {
                const value_t* root = template_root( name );
                const value_t* properties = data != nullptr ? find( *data, "Properties" ) : nullptr;
                if ( root != nullptr )
                {
                    float carried[ 16 ];
                    std::memcpy( carried, world, sizeof( carried ) );
                    const float sx = std::sqrt( local[ 0 ] * local[ 0 ] + local[ 1 ] * local[ 1 ] + local[ 2 ] * local[ 2 ] );
                    const float sy = std::sqrt( local[ 4 ] * local[ 4 ] + local[ 5 ] * local[ 5 ] + local[ 6 ] * local[ 6 ] );
                    const float sz = std::sqrt( local[ 8 ] * local[ 8 ] + local[ 9 ] * local[ 9 ] + local[ 10 ] * local[ 10 ] );
                    const value_t* root_type = find( *root, "type" );
                    const bool foliage = root_type != nullptr && root_type->kind == value_t::kind_t::text && root_type->text == "VuFoliageEntity";
                    if ( foliage && sx > 0.95f && sx < 1.05f && sy > 0.95f && sy < 1.05f && sz > 0.95f && sz < 1.05f )
                    {
                        const value_t* root_data = find( *root, "data" );
                        const value_t* root_components = root_data != nullptr ? find( *root_data, "Components" ) : nullptr;
                        const value_t* root_transform = root_components != nullptr ? find( *root_components, "VuTransformComponent" ) : nullptr;
                        const value_t* root_properties = root_transform != nullptr ? find( *root_transform, "Properties" ) : nullptr;
                        if ( root_properties != nullptr )
                        {
                            float tmpl[ 16 ];
                            math_identity( tmpl );
                            read_transform( *root_properties, tmpl );
                            math_multiply( carried, world, tmpl );
                        }
                    }
                    // A template's root must not add a segment of its own, or
                    // every path beneath it gains a level the event graph's
                    // EntityName does not have.
                    //
                    // The instance's per-child overrides ride down with it.
                    // They key the template root's OWN children by name, one
                    // level only, so a nested template instance replaces the
                    // map rather than inheriting it - saved and restored here.
                    const value_t* saved_overrides = m_child_overrides;
                    const value_t* instance_children = data != nullptr
                        ? find( *data, "ChildEntities" ) : nullptr;
                    m_child_overrides = instance_children != nullptr
                        && instance_children->kind == value_t::kind_t::object
                        ? instance_children : nullptr;
                    visit( *root, carried, false, properties, here );
                    m_child_overrides = saved_overrides;
                }
                m_stack.erase( name );
                if ( root != nullptr )
                    return;
            }
        }

        if ( type != nullptr && type->kind == value_t::kind_t::text && data != nullptr )
        {
            const value_t* properties = find( *data, "Properties" );
            record_authored( type->text, properties );
            record_authored( type->text, overlay );
            // VuSetFrontEndCameraEntity is built from the retail entity tree (load_entity_tree).
            // VuWaterSurfaceEntity is handled by the water work in its own
            // right - it has 14 authored properties and none of the geometry,
            // height resolution or displacement that read them has been
            // transliterated. The partial reader that used to sit here, and
            // the rectangle-overlap height lookup it fed, are banked in
            // reference/water.hxx.
            // VuWaterTextureSettingsEntity feeds the ocean from the retail entity
            // tree (load_entity_tree).
            if ( ( type->text == "VuInfiniteOceanWaveEntity" || type->text == "VuRectangularOceanWaveEntity" ) && properties != nullptr )
            {
                const bool rectangular = type->text == "VuRectangularOceanWaveEntity";
                const float center[ 2 ] = { world[ 12 ], world[ 13 ] };
                const float extent[ 2 ] = {
                    std::sqrt( world[ 0 ] * world[ 0 ] + world[ 1 ] * world[ 1 ] + world[ 2 ] * world[ 2 ] ),
                    std::sqrt( world[ 4 ] * world[ 4 ] + world[ 5 ] * world[ 5 ] + world[ 6 ] * world[ 6 ] )
                };
                // Every key, unit and default below comes from the schema -
                // VuInfiniteOceanWaveEntity_ctor @0x5d8380 and
                // VuRectangularOceanWaveEntity_ctor @0x5d8ad0, both emulated.
                // Wave Direction is a VuAngleProperty and the two Decay Ratios
                // are VuPercentageProperty, so the binder converts an authored
                // value and leaves a constructor default alone, which is
                // already stored.
                const c_entity_binder bind( type->text, overlay, properties );
            }
            // VuPointWaveEntity comes from the retail tree (load_entity_tree: m_point_waves).
            // VuGlobalGfxSettingsEntity feeds the fog and the far plane from the
            // retail entity tree (load_entity_tree).
            // VuFoliageEntity drew a hand-built billboard quad from an invented
            // vertex layout. Deleted; build it from the entity's own draw path.
            // VuStaticPfxEntity is built from the retail entity tree (load_entity_tree).
            // VuSkyBoxEntity is read from the retail entity tree (load_entity_tree).
            // VuAmbientLightEntity / VuDirectionalLightEntity feed the rig from the
            // retail entity tree (load_entity_tree).
            // VuSubstituteAssetEntity (Proxy_cube -> Hydro/Outside on Hydroelectric) is its game-start Trigger 0x5F4BD0 on the
            // loaded entity (graph.hxx), no longer a load-time read here.
            // VuTriggerPlaneEntity / VuTriggerBoxEntity, VuOneShotFilterEntity, VuDelayEntity,
            // VuGenericEventEntity, VuOnProjectInitializedEntity, VuIfEntity, VuAttachEntity,
            // VuBooleanEntity, VuCoronaEntity, VuRotateMotionEntity and VuPlayAnimationEntity
            // (its properties AND its plugs' links), the per-class census and the class
            // behind each path come from the retail entity tree (load_entity_tree; engine
            // entity/actions.hxx, entity/script.hxx).
        }

        // The model-draw components (Vu3dDrawStaticModelComponent, Vu3dDrawAnimatedModelComponent) are placed
        // from the retail entity tree (load_entity_tree).

        const value_t* children = data != nullptr ? find( *data, "ChildEntities" ) : nullptr;
        if ( children != nullptr && children->kind == value_t::kind_t::list )
        {
            for ( const value_t& child : children->list )
            {
                // Hand this child its instance override, if the template
                // instance above named one, and take the map out of scope for
                // the child's own subtree - the override map is one level
                // deep, keyed by the template root's direct child names.
                const value_t* child_name = find( child, "name" );
                const value_t* saved_overrides = m_child_overrides;
                m_pending_override = m_child_overrides != nullptr && child_name != nullptr
                    && child_name->kind == value_t::kind_t::text
                    ? find( *m_child_overrides, child_name->text.c_str( ) ) : nullptr;
                m_child_overrides = nullptr;
                visit( child, world, true, nullptr, here );
                m_child_overrides = saved_overrides;
            }
        }

    }




    // ref_target: motion.hxx
    std::string ref_target( const std::string& owner, const std::string& name ) const;

    // generic_event: graph.hxx
    void generic_event( const std::string& name, int depth );

    // update_rotators: motion.hxx
    void update_rotators( float dt );

    // motion_child / motions_game_initialize / rotator_activate / rotator_deactivate: motion.hxx
    void motion_child( entity_t& child, const float* matrix, const float* linear, const float* angular );
    void motions_game_initialize( );
    void rotator_activate( rotator_t& motion );
    void rotator_deactivate( rotator_t& motion );
    skel_instance_t* prop_instance( entity_t& e );
    void animated_attach_tick( );
    bool motion_release( const entity_t& e );



    // One end of an event-graph link: the other plug's owner (its path) and name.
    // Every plug's links are held, inputs' as well as outputs' - as retail's +0x50
    // lists are - and only outputs are fired; the ctor-declared direction is in
    // the tree (engine entity/script.hxx).
    // Declared up here because fire() and the graph maps use it.
    struct edge_t
    {
        std::string entity;
        std::string plug;
    };

    // A VuRetVal - what a plug handler returns. Retail's is a type tag plus a
    // union; the only tag we have seen produced is 3, which
    // VuBoolean_onGet @0x5e57f0 writes along with the bool, and which
    // VuIfEntity_onTriggerPlug @0x5dde60 checks for before it branches. The
    // tag is carried rather than assumed so a later type cannot be silently
    // read as a bool. Declared here because fire() and dispatch() return it.
    struct retval_t
    {
        int type = 0;
        bool value = false;
        // The whole value word (type 1 an int, 3 a bool in its low byte) and,
        // for type 4, the char* - what the logic entities (world/logic.hxx)
        // answer and read back.
        std::uint32_t word = 0;
        const char* text = nullptr;
    };


    // plug_returns_value: graph.hxx
    static bool plug_returns_value( const std::string& type, const std::string& plug );

    // fire: graph.hxx
    // `params`: the VuParams the output packs (0x80 data bytes, +0x80 the size; null = none), handed to every link.
    retval_t fire( const std::string& owner, const std::string& plug, int depth = 0, const std::uint8_t* params = nullptr );

    // dispatch: graph.hxx
    retval_t dispatch( const std::string& entity, const std::string& plug, int depth = 0, const std::uint8_t* params = nullptr );

    // project_initialized / race_triggers: graph.hxx
    void project_initialized( );
    void race_triggers( );

    // set_boolean: graph.hxx
    retval_t set_boolean( const std::string& entity, bool value, int depth );

    // update_delays: graph.hxx
    void update_delays( float dt );

    // VuStringDBImpl's init (slot 5 0x53B700), once: the languages = the factory's getAssetNames("VuStringAsset")
    // (the pack's names; only their membership is read), then setLanguage (slot 11 0x53B9C0) with the PLATFORM's
    // language (g_vumetrosys vt+0x48 - a hook: --language, else "en-us"; the dev config's override is compiled out
    // of retail). The asset: the pack's VuStringAsset/<language> through VuDBAsset's read 0x5C1270.
    void string_db_ready( )
    {
        if ( m_string_db_ready )
            return;
        m_string_db_ready = true;
        for ( const std::string& n : m_archive.names( "VuStringAsset/" ) )
            m_string_db.languages.push_back( n );
        const char* lang = c_engine::option( "language" );
        const bool ok = string_db_set_language( m_string_db, lang ? lang : "en-us", [ this ]( const std::string& l ) -> const value_t* {
            std::vector< std::uint8_t > blob;
            if ( !m_archive.read( ( "VuStringAsset/" + l ).c_str( ), blob ) || !pfx_asset_document( blob, m_string_asset ) )
                return nullptr;
            return &m_string_asset;
        } );
        LOG_INFO( "VuStringDB: language %s (the platform's answer: %s), %u strings%s", m_string_db.language.c_str( ), lang ? "--language" : "en-us, a hook",
                  static_cast< unsigned >( m_string_db.strings.size( ) ), ok ? "" : " - the asset did not load (UNKNOWN)" );
    }

    // The race HUD projects and their countdown (world/huds.hxx).
    void load_huds( const char* const* names = k_race_hud_projects );
    void bind_game_screens( );   // huds.hxx: Results' VuCommonScreen into s.rules.screen_hooks
    void hud( int project, race_hud_verb_t verb, float dt );
    void ui_frame( );
    // The saves (world/saves.hxx).
    void saves_start( );
    void saves_profile_listeners( );
    void saves_commit( );
    void saves_game( );
    void saves_input( platform_input_t& in );
    void saves_bind( );
    bool m_saves_started = false;
    bool m_saves_on = false;
    c_profile_manager m_profile;                        // g 0x759084
    c_cloud_save m_cloud;                               // g 0x758FA0
    value_t m_hidden_db;                                // VuHiddenPackageManagerImpl +4's document
    // The game modes (world/mode.hxx, menu.hxx).
    friend class c_menu_mode;
    friend class c_title_mode;
    bool m_title = false;
    void menu_bind( );
    const value_t& title_db( );
    void title_enter( );
    void title_exit( );
    void title_sound( );
    void title_progress( int project, float value );
    bool title_launch( ) const;
    front_end_engine_t& title_engine( ) { return m_menu_engine; }
    friend class c_action_slot;
    void modes_bind( );
    void action_enter( );
    void action_exit( );
    void menu_enter( );
    void menu_exit( );
    const std::string& menu_tick( float dt );
    void menu_any_key( );
    std::uint32_t m_menu_pad = 0;
    ui_bind_hooks_t menu_hooks( );
    struct menu_project_t
    {
        std::unique_ptr< entity_t > root;
        std::unique_ptr< c_ui_binding > binding;
    };
    mode_manager_t m_modes;
    front_end_engine_t m_menu_engine;
    std::unique_ptr< c_front_end > m_menu;
    std::map< int, menu_project_t > m_menu_projects;
    int m_menu_serial = 0;
    int m_menu_frames = 0;
    value_t m_menu_util;                                // gameutil +0x50 across the modes
    value_t m_menu_input_db;
    std::shared_ptr< platform_input_t > m_menu_input;
    value_t m_chosen;                                   // the GameData of the mode change
    bool m_has_chosen = false;
    void race_broadcast( const char* name, const ui_params_t* params = nullptr, int origin = -1 );
    void race_broadcast_hash( std::uint32_t hash, const char* name, const ui_params_t& p, int origin );
    void hud_post( std::uint32_t hash, const ui_params_t& p, int origin );
    bool m_hud_posting = false;
    static void multi_delay_ticked( void* user, float dt );

    // --front-end-defaults: the test profile through the recreated selector chain (world/front.hxx).
    std::unique_ptr< entity_t > front_end_screen( const std::string& name );
    static const entity_t* front_end_find( const entity_t& e, const char* type );
    bool front_end_defaults( const std::string& mode, const std::string& track, const std::string& course, const race_session_dbs_t& dbs,
                             value_t& game_data );

    // count_unhandled: graph.hxx
    void count_unhandled( const std::string& entity, const std::string& plug );

    // on_animation_event: graph.hxx
    void on_animation_event( const std::string& owner, const std::string& name, const value_t* params );

    // reaches: graph.hxx
    bool reaches( const std::string& entity, const std::string& plug,
        const std::string& prop, int depth = 0 ) const;

    // animated_at: graph.hxx
    bool animated_at( const std::string& path ) const;

    // audit_graph: graph.hxx
    void audit_graph( ) const;

    // walks: graph.hxx
    bool walks( const std::string& owner, const std::string& plug,
        const std::string& prop, int depth ) const;

    // test_wiring: graph.hxx
    void test_wiring( ) const;


    // skin_props: animate.hxx
    void skin_props( float dt );

    // update_static_draws / prop_draw: models.hxx
    void update_static_draws( );
    void prop_draw( const entity_t& owner );

    void wipe_scene( )
    {
        for ( int alpha = 0; alpha <= 14; ++alpha )
            c_engine::drop( alpha );
        m_foliage = foliage_manager_t{ };
        m_foliage_entities.clear( );
        m_foliage_draws.clear( );
        m_pfx.clear( );
        m_draws = 0;
        m_stack.clear( );
        m_sky.clear( );
        m_animated.clear( );
        m_static_draws.clear( );
        m_play.clear( );
        m_scored.clear( );
        m_triggers.clear( );
        m_filters.clear( );
        m_actions.clear( );
        m_delays.clear( );
        m_events.clear( );
        m_initialized.clear( );
        m_ifs.clear( );
        m_joints.clear( );
        m_prop_skels.clear( );
        m_motion_unknown = 0;
        m_refs.clear( );
        m_booleans.clear( );
        m_graph.clear( );
        m_point_waves.clear( );
        m_unhandled_events = 0;
        m_unhandled_by.clear( );
        m_logic.clear( );
        m_logic_fired = 0;
        m_fired = 0;
        // The light rig and the asset substitutions are per scene, so they
        // must be dropped with it or a second level would inherit the first's.
        m_rotators.clear( );
        m_coronas.clear( );
        m_entity_world.clear( );
        m_overrides_seen = 0;
        m_overrides_placed = 0;
        m_override_components.clear( );
        m_rig.clear( );
        m_shaded.clear( );
        m_asset_objects.clear( );
        m_type_census.clear( );
        m_authored_keys.clear( );
        m_entity_type.clear( );
    }

    // bind_animations: animate.hxx
    void bind_animations( );

    // playing: animate.hxx
    int playing( ) const;

    // template_document: entities.hxx
    const value_t* template_document( const std::string& name );

    // load_entity_tree: entities.hxx
    void load_entity_tree( const char* asset, const value_t& project );

    // corona_from: entities.hxx
    static c_corona_entity::corona_t corona_from( const std::string& path, const transform_component_t& t, const corona_properties_t& c );

    // load_project: entities.hxx
    void load_project( const char* asset );

    // A model's scene materials for the skin pass (world/livery.hxx): its shaders' table 0 - the program's, the one
    // 0x5929B0 hands the material create - as descs, in shader order. A VuStaticModelAsset through 0x560C70's first
    // step, a VuAnimatedModelAsset through its load 0x5A83E0's (the same VuGfxScene load 0x5C1560, flag 1, from
    // the blob's start). The program / layout words are the platform's (0 here). False: no such asset, or a scene
    // or table that does not read.
    bool livery_scene_for( const std::string& asset, livery_scene_t& out )
    {
        out.clear( );
        std::vector< std::uint8_t > bytes;
        scene_t scene;
        if ( m_archive.read( ( "VuStaticModelAsset/" + asset ).c_str( ), bytes ) )
        {
            if ( !scene_load_static( bytes.data( ), bytes.size( ), scene ) )
                return false;
        }
        else if ( m_archive.read( ( "VuAnimatedModelAsset/" + asset ).c_str( ), bytes ) )
        {
            scene_reader_t r{ };
            r.data = bytes.data( );
            r.size = bytes.size( );
            if ( !scene_load_gfx( r, scene, 1, false ) )
                return false;
        }
        else
            return false;
        for ( const scene_shader_t& shader : scene.shaders )
        {
            livery_material_t m;
            if ( !livery_desc_of( shader.tables[ 0 ], m ) )
                return false;
            out.push_back( m );
        }
        return true;
    }

    void load( )
    {
        // Retained only as the pack opener. The front end it used to build -
        // the Screens/Background project, its fifteen screen cameras and the
        // blend between them - is banked in reference/shot.hxx.
        const std::string path = pack_path( );
        if ( path.empty( ) || !m_archive.open( path.c_str( ) ) )
            return;
        m_opened = true;
        seed_random( );
    }

    // THE global VuRandom 0x736810 (m_vurandom): statically seed -1 with a zeroed table, warmed by the CRT initializer
    // 0x64B620 - then the engine init 0x566230 (from 0x4D9900) reseeds it ONCE from the platform clock:
    // fleet_random_seed(0, clock) = the clock's low + high words | 0x80000000, 40 steps. The clock is VuMetroSys slot
    // 12 0x4EC7C0 (a HOOK: QueryPerformanceCounter, 0 when it fails). Every draw after - the pfx emitters (c_effects),
    // the front end's roster picks, the AI plans (the race session) - comes from this one state, so a run's
    // sequence differs per run, as in retail. A --bench run's clock answers a FIXED 0 (retail's own failure answer)
    // so the gate stills stay reproducible.
    void seed_random( )
    {
        fleet_random_seed( m_vurandom, 0, [ ]( ) -> std::int64_t {
            if ( c_engine::option( "bench" ) != nullptr )
                return 0;
#if defined( _WIN32 )
            LARGE_INTEGER c;
            return QueryPerformanceCounter( &c ) ? static_cast< std::int64_t >( c.QuadPart ) : 0;
#else
            return 0;
#endif
        } );
        m_pfx.bind_random( &m_vurandom );
        LOG_INFO( "VuRandom 0x736810 seeded by the engine init 0x566230 from the %s clock: seed 0x%08X",
                  c_engine::option( "bench" ) != nullptr ? "fixed bench (0)" : "host", static_cast< unsigned >( m_vurandom.seed ) );
    }

    // SCAFFOLD - ours, no retail counterpart.
    //
    // A vantage for looking at a loaded scene. Retail has nothing like it: a
    // race's camera is VuBoatCamera (it needs a boat); the front end's is set by
    // VuFrontEndGameMode's SetFrontEndCamera* handlers answering the events a
    // VuSetFrontEndCameraEntity in a screen project broadcasts (no project
    // authors a VuFrontEndCameraEntity; the water agent is recreating those
    // handlers, world/frontcam.hxx). A race project authors none of them. So this orbits the bounding box of whatever the
    // entity walk actually placed. It is a measuring instrument, not a
    // recreation, and no part of it may be taken as retail behaviour.
    // SCAFFOLD - ours. A free camera for looking around a loaded level: F
    // toggles it (or start in it with --freecam); WASD moves along the view,
    // Q / E down / up, Shift is 4x; hold the right mouse button and move the
    // mouse to look. Input counts only while this process owns the foreground
    // window, and never in a --bench run (the stills must stay reproducible).
    // World up is +z, as the orbit below assumes.
    bool dev_freecam( float dt )
    {
#if defined( _WIN32 )
        if ( c_engine::option( "bench" ) != nullptr )
            return false;
        DWORD owner = 0;
        const HWND foreground = GetForegroundWindow( );
        if ( foreground != nullptr )
            GetWindowThreadProcessId( foreground, &owner );
        const bool focused = owner == GetCurrentProcessId( );
        const auto down = [ focused ]( int key ) { return focused && ( GetAsyncKeyState( key ) & 0x8000 ) != 0; };

        // Entering the free camera starts from wherever the orbit is looking.
        const auto enter = [ this ]( )
        {
            const float dx = m_target[ 0 ] - m_eye[ 0 ];
            const float dy = m_target[ 1 ] - m_eye[ 1 ];
            const float dz = m_target[ 2 ] - m_eye[ 2 ];
            m_yaw = std::atan2( dy, dx );
            m_pitch = std::atan2( dz, std::sqrt( dx * dx + dy * dy ) );
            LOG_INFO( "dev camera: FREECAM (WASD / Q E / Shift, right mouse to look, F back to the orbit)" );
        };
        const bool toggle = down( 'F' );
        if ( !m_freecam_init )
        {
            m_freecam_init = true;
            m_freecam = c_engine::option( "freecam" ) != nullptr;
            if ( m_freecam )
                enter( );
        }
        if ( toggle && !m_freecam_toggle_held )
        {
            m_freecam = !m_freecam;
            if ( m_freecam )
                enter( );
            else
                LOG_INFO( "dev camera: orbit" );
        }
        m_freecam_toggle_held = toggle;
        if ( !m_freecam )
            return false;

        POINT cursor{};
        GetCursorPos( &cursor );
        if ( down( VK_RBUTTON ) && m_freecam_looking )
        {
            m_yaw -= static_cast< float >( cursor.x - m_freecam_cursor.x ) * 0.004f;
            m_pitch -= static_cast< float >( cursor.y - m_freecam_cursor.y ) * 0.004f;
            if ( m_pitch > 1.55f )
                m_pitch = 1.55f;
            if ( m_pitch < -1.55f )
                m_pitch = -1.55f;
        }
        m_freecam_looking = down( VK_RBUTTON );
        m_freecam_cursor = cursor;

        const float forward[ 3 ] = { std::cos( m_pitch ) * std::cos( m_yaw ), std::cos( m_pitch ) * std::sin( m_yaw ), std::sin( m_pitch ) };
        // forward x up (0, 0, 1), flattened: the view's right on the ground plane.
        float right[ 3 ] = { forward[ 1 ], -forward[ 0 ], 0.0f };
        const float length = std::sqrt( right[ 0 ] * right[ 0 ] + right[ 1 ] * right[ 1 ] );
        if ( length > 0.0f )
        {
            right[ 0 ] /= length;
            right[ 1 ] /= length;
        }
        const float speed = ( down( VK_SHIFT ) ? 160.0f : 40.0f ) * dt;
        const float along = ( down( 'W' ) ? 1.0f : 0.0f ) - ( down( 'S' ) ? 1.0f : 0.0f );
        const float side = ( down( 'D' ) ? 1.0f : 0.0f ) - ( down( 'A' ) ? 1.0f : 0.0f );
        const float rise = ( down( 'E' ) ? 1.0f : 0.0f ) - ( down( 'Q' ) ? 1.0f : 0.0f );
        for ( int axis = 0; axis < 3; ++axis )
            m_eye[ axis ] += ( forward[ axis ] * along + right[ axis ] * side ) * speed;
        m_eye[ 2 ] += rise * speed;
        for ( int axis = 0; axis < 3; ++axis )
            m_target[ axis ] = m_eye[ axis ] + forward[ axis ];
        m_far = m_authored_far;
        return true;
#else
        ( void )dt;
        return false;
#endif
    }

    void dev_vantage( float dt )
    {
        if ( m_authored_camera )
            return;
        if ( m_vantage_ready && dev_freecam( dt ) )
            return;
        if ( !m_vantage_ready )
        {
            // The STARTING GRID when the project has one: the mean of its
            // VuGridPositionEntity worlds from the retail tree - on the water at
            // the start line. The mean of every placement lands off the course
            // on a long ribbon of a level (the user saw it outside the map), so
            // it is only the fallback for a project without a grid.
            double sum[ 3 ]{};
            int grid = 0;
            if ( m_tree )
            {
                const auto walk = [ & ]( const auto& self, entity_t& e ) -> void
                {
                    const transform_component_t* t = e.m_type == "VuGridPositionEntity" && !e.m_world_unknown
                        ? entity_transform( e ) : nullptr;
                    if ( t )
                    {
                        for ( int axis = 0; axis < 3; ++axis )
                            sum[ axis ] += t->world[ 12 + axis ];
                        ++grid;
                    }
                    for ( auto& child : e.m_children )
                        self( self, *child );
                };
                walk( walk, *m_tree );
            }
            if ( grid == 0 )
                for ( const auto& pair : m_entity_world )
                    for ( int axis = 0; axis < 3; ++axis )
                        sum[ axis ] += pair.second[ 12 + axis ];
            const double count = grid > 0 ? static_cast< double >( grid )
                : m_entity_world.empty( ) ? 1.0 : static_cast< double >( m_entity_world.size( ) );
            for ( int axis = 0; axis < 3; ++axis )
                m_centre[ axis ] = static_cast< float >( sum[ axis ] / count );

            // Deliberately a close vantage. The orbit must sit well inside the
            // fog's range or the fog - which is 1:1 and correct - washes the
            // whole frame to the fog colour and the viewer shows nothing.
            m_radius = 80.0f;
            if ( const char* option = c_engine::option( "vantage" ) )
            {
                const float parsed = static_cast< float >( std::atof( option ) );
                if ( parsed > 1.0f )
                    m_radius = parsed;
            }
            m_vantage_ready = true;
            LOG_INFO( "dev vantage: %s %d, centre %.0f %.0f %.0f, orbit radius %.0f (--vantage <units>)",
                grid > 0 ? "starting grid positions" : "no grid - mean of placements",
                grid > 0 ? grid : static_cast< int >( m_entity_world.size( ) ),
                m_centre[ 0 ], m_centre[ 1 ], m_centre[ 2 ], m_radius );
        }

        m_orbit += dt * 0.1f;
        m_eye[ 0 ] = m_centre[ 0 ] + std::cos( m_orbit ) * m_radius;
        m_eye[ 1 ] = m_centre[ 1 ] + std::sin( m_orbit ) * m_radius;
        m_eye[ 2 ] = m_centre[ 2 ] + m_radius * 0.4f;
        std::memcpy( m_target, m_centre, sizeof( m_target ) );
        m_far = m_authored_far;
    }

public:
    bool loaded( ) const
    {
        return m_loaded || m_title;
    }

    // VuActionGameMode's load 0x4BA060, in its order: the project by 0x4D3480 (race_project_name: GameData's Level,
    // else "Games/" + its last path part + GameType [+ Course for Slalom]) from the GameData the front end left,
    // loaded; then the game object by GameType (race_game_kind) - "Race" -> the VuRaceGame ctor 0x489030 and the
    // race session (world/session.hxx: 0x481CE0, postLoad, begin 0x4829A0); "TimeTrial" -> the same session with
    // VuTimeTrialGame's machine (0x48ED30); "Slalom" -> VuSlalomGame's (0x48BEC0); "Ghost" -> VuGhostGame 0x484A20's machine
    // table with its state handlers UNKNOWN (session.hxx: only VuGame's machines and its read slots 5 / 7 / 13 run); anything
    // else -> no game.
    // THE GAMEDATA: with --front-end-defaults, the test profile's front end (world/front.hxx: the GameConfig<Mode> /
    // BoatConfig* screens through ui_front_end_root, --boat / --skin overriding the boat screen's picks, then 0x4BF9B0's
    // data step). Else with --boat / --skin, the front end's (race_session_game_data from --mode / --race / --course /
    // --boat / --skin - the roster drawn from the shared VuRandom). Without them (the gates' bench) there is no front
    // end run and retail has no counterpart (its dev config's ActionGameData is the null container,
    // 0x4FDB70): SCAFFOLD, ours - GameData {Level: --race, GameType: --mode (else "Race"), Course: --course}, the
    // inputs the old Games/<track><mode> path took; with Level a bare track name 0x4D3480 names the same projects.
    void load_background( );

    // A motion target moved (session.hxx race_session_key_motion): the animated model component's draw matrix 0x61DA50
    // = fromScale( transform +0xB0 ) x world +0x50 (transform_draw_matrix) into its item - a rigid (one-bone) item's
    // placements follow it on its next skin; a static model's placements through prop_draw. A streamed (cloned) item that
    // is not playing keeps its placement (the renderer's - flagged).
    void prop_moved( const entity_t& owner )
    {
        const transform_component_t* t = entity_transform( const_cast< entity_t& >( owner ) );
        if ( !t )
            return;
        const std::string path = entity_path( owner );
        for ( animated_t& item : m_animated )
            if ( item.path == path )
            {
                transform_draw_matrix( *t, item.world );
                if ( !item.playing && item.rigid )
                {
                    const std::vector< cached_part_t >& parts = model_for( item.asset );
                    for ( std::size_t index = 0; index < item.placements.size( ) && index < parts.size( ); ++index )
                        if ( item.placements[ index ] >= 0 )
                        {
                            float node[ 16 ];
                            math_multiply( node, item.world, parts[ index ].node );
                            c_engine::move( item.placements[ index ], node );
                        }
                }
            }
        prop_draw( owner );
    }

    // An entity's graph path: its names below the project root, '/'-joined (the event graph's key).
    static std::string entity_path( const entity_t& owner )
    {
        std::string path;
        for ( const entity_t* e = &owner; e && e->m_parent; e = e->m_parent )
            path = path.empty( ) ? e->m_name : e->m_name + "/" + path;
        return path;
    }
    void action_start( value_t& game_data, const race_session_dbs_t& dbs, const std::function< void( ) >& level );

    void load_scene( const char* track, const char* mode, const char* course = nullptr )
    {
        if ( !m_opened )
            load( );
        saves_start( );
        if ( !m_sorted )
        {
            void* device = nullptr;
            void* context = nullptr;
            c_engine::api( device, context );
            if ( device )
            {
                m_sorted = std::make_unique< gfx_sorted_t >( device, context );
                c_engine::sorted( []( void* u, const sorted_pass_t& pass ) {
                    c_world& w = *static_cast< c_world* >( u );
                    w.m_sorted->run( pass, w.m_far );
                }, this );
#if HYDRO_UI_PASS
                m_ui_pass = std::make_unique< gfx_ui_pass_t >( device, context, [ this ]( const std::string& asset, std::vector< std::uint8_t >& out ) { return m_archive.read( asset.c_str( ), out ); } );
                c_engine::overlay( []( void* u ) { static_cast< c_world* >( u )->ui_frame( ); }, this );
#endif
            }
        }
        if ( !m_modes.m_factory )
            modes_bind( );
        // 0x4CBD48: a normal launch's first mode (world/title.hxx). The command line's race stays for the tests.
        if ( title_launch( ) )
        {
            m_modes.m_next = "TitleSequence";
            m_modes.change( );
            return;
        }
        // Ours (a test driver): `--menu` starts at the front end.
        if ( c_engine::option( "menu" ) != nullptr && !m_has_chosen )
        {
            if ( !m_opened )
                load( );
            m_modes.m_next = "FrontEnd";
            m_modes.change( );
            return;
        }
        if ( c_engine::option( "front-end" ) != nullptr )
        {
            load_background( );
            return;
        }
        m_race = race_session_t{ };
        race_session_dbs_t dbs = race_dbs( );
        const auto text = []( const char* v ) { return std::string( v ? v : "" ); };
        const bool defaults = c_engine::option( "front-end-defaults" ) != nullptr;
        const bool race = m_has_chosen || defaults || c_engine::option( "boat" ) != nullptr || c_engine::option( "skin" ) != nullptr;
        value_t util;
        value_t& game_data = ui_json_child( util, "GameData" );
        bool front_end = false;
        if ( m_has_chosen )   // the mode change's: 0x4BF9B0 built it
        {
            game_data = m_chosen;
            front_end = true;
        }
        else if ( defaults )   // the test profile's front end (world/front.hxx); --boat / --skin override its picks
            front_end = front_end_defaults( text( mode ), text( track ), text( course ), dbs, game_data );
        else if ( race )
            front_end = race_session_game_data( text( mode ), text( track ), text( course ), text( c_engine::option( "boat" ) ),
                                                text( c_engine::option( "skin" ) ), dbs, m_vurandom, game_data );
        if ( !front_end )
        {
            game_data = value_t{ };
            ui_json_set_string( ui_json_child( game_data, "Level" ), text( track ).c_str( ) );
            ui_json_set_string( ui_json_child( game_data, "GameType" ), mode != nullptr && mode[ 0 ] != '\0' ? mode : "Race" );
            if ( course != nullptr && course[ 0 ] != '\0' )
                ui_json_set_string( ui_json_child( game_data, "Course" ), course );
        }
        // VuActionGameMode 0x4BA060 (LoadLevel's enter): the body below. A front-end race runs it through the action mode
        // (world/action.hxx); every other path (the scaffold, --front-end-defaults to a non-race) calls it directly.
        const auto level = [ & ]( )
        {
        const std::string name = race_project_name( util, nullptr, [ this ]( const std::string& type, const std::string& n ) {
            for ( const std::string& rest : m_archive.names( type + "/" + n ) )   // names() strips the prefix: exact = ""
                if ( rest.empty( ) )
                    return true;
            return false;
        } );
        const std::string path = "VuProjectAsset/" + name;
        wipe_scene( );
        m_vantage_ready = false;
        m_authored_camera = false;
        m_authored_far = 500.0f;
        // A race defers OnProjectInitialized to retail's place (0x4BA060: after the boats' load and the project's
        // gameInitialize, before begin - race_session_start's callback); otherwise it goes out at the end of the load.
        // g_vugameutil +0x50 as the game-tier logic filters read it (world/logic.hxx logic_game_t).
        m_game_util = util;
        m_logic_game.game_data = &m_game_util;
        m_logic_game.unrecreated = &m_logic_unrecreated;
        const race_game_kind_t game_kind = race_game_kind( race_game_data_text( util, nullptr, "GameType" ) );
        m_defer_project_initialized = race && front_end && ( game_kind == race_game_kind_t::race || game_kind == race_game_kind_t::time_trial ||
                                                              game_kind == race_game_kind_t::slalom || game_kind == race_game_kind_t::ghost );
        m_project_initialized = false;
        load_project( path.c_str( ) );
        m_loaded = true;
        LOG_SUCCESS( "scene %s (0x4D3480 from the %s GameData)", name.c_str( ), front_end ? "front end's" : "scaffold" );
        const std::string type = race_game_data_text( util, nullptr, "GameType" );
        switch ( race_game_kind( type ) )
        {
        case race_game_kind_t::race:
        case race_game_kind_t::time_trial:   // VuTimeTrialGame 0x48ED30: its machine (rules.hxx), slots 13 / 5 (start.hxx)
        case race_game_kind_t::slalom:       // VuSlalomGame 0x48BEC0: its machine (rules.hxx), its course (slalom.hxx), slots 13 / 5
        case race_game_kind_t::ghost:        // VuGhostGame 0x484A20: its machine's table; its handlers UNKNOWN (session.hxx)
            if ( front_end )
                race_switch_on( game_data, dbs, type );
            break;
        case race_game_kind_t::none:
            LOG_INFO( "0x4BA060: GameType '%s' makes no game object", type.c_str( ) );
            break;
        default:
            LOG_WARNING( "0x4BA060: the %s game's ctor (0x48BEC0 / 0x48ED30 / 0x484A20) is NOT recreated - no game object", type.c_str( ) );
            break;
        }
        saves_commit( );   // 0x4BA396 / 0x4BA3A7, at the load's end
        };
        m_action.reset( );
        if ( race && front_end )
            action_start( game_data, dbs, level );
        else
            level( );
    }

    // BoatDB / TrackDB / ConstantDB (and SkinDB for the skin pass) from the pack.
    race_session_dbs_t race_dbs( )
    {
        const auto db = [ this ]( const char* asset, value_t& out ) -> const value_t*
        {
            std::vector< std::uint8_t > bytes;
            if ( !m_archive.read( asset, bytes ) )
                return nullptr;
            out = c_reader( bytes ).parse( );
            return &out;
        };
        race_session_dbs_t dbs;
        dbs.boat_db = db( "VuDBAsset/BoatDB", m_boat_db );
        dbs.track_db = db( "VuDBAsset/TrackDB", m_track_db );
        dbs.constant_db = db( "VuDBAsset/ConstantDB", m_constant_db );
        db( "VuDBAsset/SkinDB", m_skin_db );
        return dbs;
    }

    // The race (world/session.hxx) on the front end's GameData, after the project load.
    void race_switch_on( const value_t& game_data, const race_session_dbs_t& dbs, const std::string& mode )
    {
        if ( !m_tree )
        {
            LOG_WARNING( "race: the project's entity tree did not load; no race" );
            if ( !m_project_initialized )
                project_initialized( );
            return;
        }
        m_race.world.triggers = [ this ] { race_triggers( ); };
        m_race.world.prop_draw = [ this ]( const entity_t& owner ) { prop_draw( owner ); };
        m_race.world.prop_drawn = [ this ]( const entity_t& owner, bool on ) {
            for ( static_draw_item_t& item : m_static_draws )
                if ( item.owner == &owner )
                    item.off = !on;
        };
        m_race.world.fire = [ this ]( const entity_t& owner, const char* plug ) { fire( entity_path( owner ), plug ); };
        m_race.world.sound = &m_sound;
        saves_bind( );
        m_race.world.prop_moved = [ this ]( const entity_t& owner ) { prop_moved( owner ); };
        m_race.world.attach_update = [ this ]( const entity_t& parent, const float* m, const float* l, const float* a ) { m_joints.parent_moved( parent, m, l, a ); };
        m_race.world.motion_release = [ this ]( const entity_t& e ) { return motion_release( e ); };
        // VuRaceGame's ctor 0x489030 loads its three HUD projects (0x4B4360) - before the boat load; VuTimeTrialGame's 0x48ED30
        // its own three (game_mode_defs: HUDs/TimeTrial*).
        load_huds( mode == "TimeTrial" ? game_mode_defs( )[ 1 ].huds : mode == "Slalom" ? game_mode_defs( )[ 0 ].huds
                   : mode == "Ghost" ? game_mode_defs( )[ 2 ].huds : k_race_hud_projects );
        m_race.world.hud = [ this ]( int project, race_hud_verb_t verb, float step ) { hud( project, verb, step ); };
        m_race.world.broadcast = [ this ]( const char* name, const ui_params_t* params ) { race_broadcast( name, params ); };
        // The VuPfx runtime / manager before the boats' postLoad (its Pfx fill creates systems - session.hxx
        // race_session_bind_pfx_fill); scene_frame refreshes them each frame.
        m_race.world.pfx = m_pfx.runtime( );
        m_race.world.pfx_manager = m_pfx.manager( );
        m_race.world.pfx_ops = m_pfx.manager_ops( );
        vo_ready( );
        m_race.world.vo = &m_vo;
        m_race.world.vo_hooks = &m_vo_hooks;
        m_race.world.string_db = [ this ]( const char* id ) {
            string_db_ready( );
            return string_db_get( m_string_db, id ? id : "" );
        };
        const bool started = race_session_start( m_race, mode, game_data, dbs, m_archive, *m_tree, track( ), m_vurandom,
                                                 [ this ] { project_initialized( ); } );
        if ( !m_project_initialized )
            project_initialized( );   // the session stopped before 0x4BA060 reached it: the event still goes out
        if ( !started )
            return;
        bind_game_screens( );
        // THE TEST INPUT (ours, flagged): --input-script answers the platform input hook from a file (world/drive.hxx).
        if ( const char* script = c_engine::option( "input-script" ) )
        {
            auto loaded = std::make_shared< race_input_script_t >( );
            if ( race_input_script_load( script, *loaded ) )
            {
                m_race.input_script = loaded;
                LOG_WARNING( "race: TEST INPUT - the human pad answered from %s (%d keys), not the device", script, static_cast< int >( loaded->keys.size( ) ) );
            }
            else
                LOG_ERROR( "race: --input-script %s did not load (no keys) - the device answers", script );
        }
        if ( const char* keys = c_engine::option( "keys" ) )
        {
            std::istringstream list( keys );
            std::string one;
            while ( std::getline( list, one, ',' ) )
            {
                const std::size_t a = one.find( ':' ), b = one.rfind( ':' );
                if ( a != std::string::npos && b > a )
                    m_race.key_script.push_back( { std::atoi( one.c_str( ) ), input_key_index( one.substr( a + 1, b - a - 1 ).c_str( ) ), one[ b + 1 ] == '1' } );
            }
        }
        // The queries' view of VuWater as the load left it: the race's first frame reads VuWater (DynamicsSync,
        // Decision) before its first "Water" phase rebuilds the view.
        water_view_build( m_water, m_water_view );
        // The hulls: each boat's three model instances (+0x2A4 / +0x2A8 / +0x2AC from BoatDB's Model Asset / LOD 1 /
        // LOD 2 Model Asset - the instance loads 0x5322C0 / 0x520080 are the rendering layer: our model path) placed
        // at the draw 0x41EAE0's matrix, hidden until race_draw shows the instance the draw picks. The SKIN is the
        // skin pass 0x421490 (world/livery.hxx, 1:1): the last step of 0x420A70, so it is reached only when the
        // sections load ran to its end (b.sections); the Skin watcher's earlier run (0x481CE0 sets Skin before the
        // models load) finds the instances' scenes null and leaves every skin empty. Until the boat agent's
        // collision-mesh reader lets 0x420A70 finish, the hulls therefore draw their own materials (the
        // Boat_proxy/Proxy_diffuse placeholder) - retail's state at that point, not a stand-in. The damage pieces'
        // skins (+0x238, 0x4119F0) are not built here: our renderer does not draw the pieces. race_draw moves the
        // shown instance each frame (0x41EAE0's matrix).
        for ( std::size_t k = 0; k < m_race.boats.size( ); ++k )
        {
            race_session_boat_t& b = *m_race.boats[ k ];
            const std::uint8_t* transform = m_race.start_boats[ k ].transform;
            if ( !transform )
                continue;
            float m[ 16 ];
            race_boat_draw_matrix( b.graph.objects[ 0 ].image.data( ), transform, m );
            const int at[ 3 ] = { 0xC4, 0xDC, 0xF4 };
            std::vector< livery_draw_t > draws[ 3 ];
            int skinned = 0;
            if ( b.sections )
            {
                livery_scene_t scenes[ 3 ];
                const livery_scene_t* pointers[ 3 ] = { };
                for ( int i = 0; i < 3; ++i )
                    if ( livery_scene_for( b.io.boat_strings[ at[ i ] ], scenes[ i ] ) )
                        pointers[ i ] = &scenes[ i ];
                std::vector< std::vector< livery_material_t > > skins;
                if ( livery_pass( m_skin_db.tag == 6 ? &m_skin_db : nullptr, b.skin, pointers, { }, skins ) )
                    for ( int i = 0; i < 3; ++i )
                        for ( std::size_t s = 0; pointers[ i ] && s < skins[ i ].size( ) && s < scenes[ i ].size( ); ++s )
                        {
                            draws[ i ].push_back( livery_draw_of( scenes[ i ][ s ], skins[ static_cast< std::size_t >( i ) ][ s ] ) );
                            skinned += draws[ i ].back( ).texture_set ? 1 : 0;
                        }
                else
                    LOG_WARNING( "race: %s skin pass 0x421490 UNKNOWN (a texture name of 64 bytes or more: strcpy_s's invalid-parameter path)",
                                 b.name.c_str( ) );
            }
            for ( int i = 0; i < 3; ++i )
            {
                const std::string& asset = b.io.boat_strings[ at[ i ] ];
                if ( asset.empty( ) )
                    continue;
                model_for( asset );
                // The animated instance +0x2A4: its own vertices, posed each draw (race_draw). SUBSTITUTION, as the animated
                // props: CPU skinning streamed to a clone, where retail uploads gMatrixArray to the vertex shader.
                const auto skinned = m_skinned.find( asset );
                if ( i == 0 && skinned != m_skinned.end( ) && skinned->second.skin.size( ) == skinned->second.bind.size( ) )
                    b.hull_geometry = c_engine::geometry( skinned->second.bind.data( ), static_cast< int >( skinned->second.bind.size( ) ),
                                                          skinned->second.indices.data( ), static_cast< int >( skinned->second.indices.size( ) ) );
                place_model( asset, m, 0, -1, nullptr, draws[ i ].empty( ) ? nullptr : &draws[ i ], i == 0 ? b.hull_geometry : -1, &b.placements[ i ] );
                for ( int handle : b.placements[ i ] )
                    c_engine::show( handle, false );
            }
            b.shown = -1;
            LOG_INFO( "race: %s hull at %.1f %.1f %.1f - models '%s' / '%s' / '%s', skin '%s': %s", b.name.c_str( ), m[ 12 ], m[ 13 ], m[ 14 ],
                      b.io.boat_strings[ 0xC4 ].c_str( ), b.io.boat_strings[ 0xDC ].c_str( ), b.io.boat_strings[ 0xF4 ].c_str( ), b.skin.c_str( ),
                      b.sections ? ( skinned ? "skin pass 0x421490 applied" : "skin pass 0x421490 ran, no texture changed" )
                                 : "skin pass 0x421490 not reached (0x420A70 stopped) - the models' own materials" );
        }
    }

    // Per frame: the draw 0x41EAE0's instance choice against this frame's eye, and its matrix: the transform's world
    // (+0x50) . translation( boat +0x49C ), rebuilt each draw (0x41EB47 .. 0x41EB8E) - the placed instance follows the boat.
    // Our model path keeps a part's node matrix per placement (retail's instance applies it under 0x532AD0 / 0x520130).
    void race_draw( )
    {
        for ( std::size_t k = 0; k < m_race.boats.size( ) && m_race.active; ++k )
        {
            race_session_boat_t& b = *m_race.boats[ k ];
            const std::uint8_t* transform = m_race.start_boats[ k ].transform;
            if ( !transform )
                continue;
            const std::uint8_t* boat = b.graph.objects[ 0 ].image.data( );
            const int pick = race_boat_draw_instance( boat, transform, m_eye );
            if ( pick != b.shown )
            {
                if ( b.shown >= 0 )
                    for ( int handle : b.placements[ b.shown ] )
                        c_engine::show( handle, false );
                if ( pick >= 0 )
                    for ( int handle : b.placements[ pick ] )
                        c_engine::show( handle, true );
                b.shown = pick;
            }
            if ( pick < 0 )
                continue;
            float m[ 16 ];
            race_boat_draw_matrix( boat, transform, m );
            const int at[ 3 ] = { 0xC4, 0xDC, 0xF4 };
            const std::vector< cached_part_t >& parts = model_for( b.io.boat_strings[ at[ pick ] ] );
            // 0x532AD0's palette: instance +0x1C / +0x20 by +0x28, the one setPose 0x532720 wrote last (world/rig.hxx boat_rig_pose).
            if ( pick == 0 && b.hull_geometry >= 0 && b.rig.model )
            {
                const auto skinned = m_skinned.find( b.io.boat_strings[ at[ 0 ] ] );
                const auto bones = m_skeletons.find( b.io.boat_strings[ at[ 0 ] ] );
                const skel_instance_t& pose = b.rig.model->instance;
                if ( skinned != m_skinned.end( ) && bones != m_skeletons.end( ) && pose.palette[ pose.word28 ].size( ) == bones->second.size( ) * 16 )
                {
                    const mesh_vertex_t* posed = skin_geometry( b.hull_geometry, skinned->second, bones->second, pose.palette[ pose.word28 ] );
                    // Dev trace (--verbose, ours): how far the pose took the hull from its bind vertices.
                    if ( g_verbose && posed && m_race.frames % 480 == 0 && k + 1 == m_race.boats.size( ) )
                    {
                        float most = 0.0f;
                        for ( std::size_t v = 0; v < skinned->second.bind.size( ); ++v )
                            for ( int c = 0; c < 3; ++c )
                                most = ( std::max )( most, std::fabs( posed[ v ].position[ c ] - skinned->second.bind[ v ].position[ c ] ) );
                        LOG_VERBOSE( "race: %s hull pose, frame %d: %d bones, a vertex moved %.3f from bind", b.name.c_str( ), m_race.frames,
                                     static_cast< int >( bones->second.size( ) ), most );
                    }
                }
                else if ( !b.hull_refused )
                {
                    b.hull_refused = true;
                    LOG_WARNING( "race: %s hull: the palette (%d floats) and the model's bones (%d) differ - bind pose (UNKNOWN)", b.name.c_str( ),
                                 static_cast< int >( pose.palette[ pose.word28 ].size( ) ), bones != m_skeletons.end( ) ? static_cast< int >( bones->second.size( ) ) : -1 );
                }
            }
            for ( std::size_t i = 0; i < b.placements[ pick ].size( ) && i < parts.size( ); ++i )
            {
                float placed[ 16 ];
                math_multiply( placed, m, parts[ i ].node );
                c_engine::move( b.placements[ pick ][ i ], placed );
            }
        }
    }

    // SCAFFOLD - ours. The per-frame order below is NOT retail's frame; retail
    // runs these off VuTickManager phases that have not been transliterated.
    // Every function it calls is 1:1, but the sequence is ours and proves
    // nothing about ordering.
    // The water surfaces on screen: 0x5979C0's body for our one viewport (grid.hxx water_grid_frame), then our
    // draw. The frustum block is 0x50C550's, fed from OUR camera (the dev vantage: eye / target, the
    // viewport's fov_y 1.05, near 0.5, the 1280x720 aspect; retail's +384 is tanf of the horizontal half
    // angle). SUBSTITUTED / NOT RECREATED in the draw (the platform draw of the 15-word records is unread):
    // the retail Water shader's colour is lerp( gAmbientColor, gDiffuseColor, record +0x18 ) per vertex -
    // computed here on the CPU from the gfx settings colours (whether the surface entity's own AmbientColor /
    // DiffuseColor override them is unread); the foam term (gFoam* colours, gFoamTexture, record +0x14), the
    // normal map, the planar reflection and the Fresnel LUT are absent; the pass is the engine's class 7
    // (unlit, fogged, blended, alpha x 0.55 - divided out here).
    void water_draw( )
    {
        if ( m_grid_geometry < 0 )
            return;
        float fw[ 3 ] = { m_target[ 0 ] - m_eye[ 0 ], m_target[ 1 ] - m_eye[ 1 ], m_target[ 2 ] - m_eye[ 2 ] };
        const float fl = std::sqrt( fw[ 0 ] * fw[ 0 ] + fw[ 1 ] * fw[ 1 ] + fw[ 2 ] * fw[ 2 ] );
        if ( !( fl > 0.0f ) )
            return;
        for ( float& c : fw )
            c /= fl;
        float rt[ 3 ] = { fw[ 1 ], -fw[ 0 ], 0.0f };
        const float rl = std::sqrt( rt[ 0 ] * rt[ 0 ] + rt[ 1 ] * rt[ 1 ] );
        if ( rl > 0.0f )
        {
            rt[ 0 ] /= rl;
            rt[ 1 ] /= rl;
        }
        else
            rt[ 0 ] = 1.0f;
        const float up[ 3 ] = { rt[ 1 ] * fw[ 2 ] - rt[ 2 ] * fw[ 1 ], rt[ 2 ] * fw[ 0 ] - rt[ 0 ] * fw[ 2 ], rt[ 0 ] * fw[ 1 ] - rt[ 1 ] * fw[ 0 ] };
        const float aspect = 1280.0f / 720.0f;
        const water_frustum_t cam = water_frustum_build( m_eye, rt, fw, up, aspect, 0.5f, m_far, std::tan( 1.05f * 0.5f ) * aspect );
        water_grid_frame( m_grid, cam, m_water, &m_water_view, m_grid_frame );
        int count = 0;
        mesh_vertex_t* out = c_engine::edit( m_grid_geometry, count );
        if ( !out )
            return;
        auto channel = []( std::uint32_t c, int k ) { return static_cast< float >( ( c >> ( 8 * k ) ) & 0xFF ) / 255.0f; };
        int n = 0;
        for ( const water_grid_draw_t& d : m_grid_frame.draws )
            for ( int i = 0; i < d.index_count; ++i )
            {
                if ( n >= count )
                {
                    ++m_grid_overflow;
                    break;
                }
                const water_grid_record_t& r = m_grid_frame.vertices[ static_cast< std::size_t >( d.vertex_start + m_grid_frame.indices[ static_cast< std::size_t >( d.index_start + i ) ] ) ];
                mesh_vertex_t& v = out[ n++ ];
                v = mesh_vertex_t{};
                v.position[ 0 ] = r.x;
                v.position[ 1 ] = r.y;
                v.position[ 2 ] = r.height;
                v.normal[ 2 ] = 1.0f;
                for ( int k = 0; k < 4; ++k )
                {
                    const float a = channel( m_water_ambient, k );
                    v.color[ k ] = a + ( channel( m_water_diffuse, k ) - a ) * r.shade;
                }
                v.color[ 3 ] = v.color[ 3 ] / 0.55f > 1.0f ? 1.0f : v.color[ 3 ] / 0.55f;
            }
        for ( int i = n; i < count; ++i )
            out[ i ] = mesh_vertex_t{};
        c_engine::stream( m_grid_geometry );
        if ( m_grid_overflow == 1 )
            LOG_WARNING( "water draw: more than %d expanded vertices in a frame - the tail is not drawn (our capacity)", count );
    }
    const track_manager_t* track( ) const
    {
        return m_track_built ? &m_track : nullptr;
    }
    void scene_frame( float dt )
    {
        if ( m_title )   // the title sequence runs no scene: the mode manager's tick alone
        {
            m_modes.tick( dt );
            return;
        }
        if ( !m_loaded )
            return;

        // VuWater's tick: with a race running it is the race tick manager's "Water" phase handler (registered by
        // race_session_start - after Decision, where the hulls' wake update 0x417D80 writes VuWater, and before
        // DynamicsKick); without a race it runs here, once per frame, as before.
        m_race.world.water = &m_water;
        m_race.world.pfx = m_pfx.runtime( );
        m_race.world.pfx_manager = m_pfx.manager( );
        m_race.world.pfx_ops = m_pfx.manager_ops( );
        m_race.world.view = &m_water_view;
        m_race.world.water_tick = [ this ]( float step ) { water_frame( step ); };
        m_race.world.triggers = [ this ] { race_triggers( ); };
        m_race.world.prop_draw = [ this ]( const entity_t& owner ) { prop_draw( owner ); };
        m_race.world.prop_drawn = [ this ]( const entity_t& owner, bool on ) {
            for ( static_draw_item_t& item : m_static_draws )
                if ( item.owner == &owner )
                    item.off = !on;
        };
        m_race.world.fire = [ this ]( const entity_t& owner, const char* plug ) { fire( entity_path( owner ), plug ); };
        m_race.world.sound = &m_sound;
        m_race.world.vo = m_vo_ready ? &m_vo : nullptr;
        m_race.world.vo_hooks = m_vo_ready ? &m_vo_hooks : nullptr;
        m_race.world.prop_moved = [ this ]( const entity_t& owner ) { prop_moved( owner ); };
        m_race.world.attach_update = [ this ]( const entity_t& parent, const float* m, const float* l, const float* a ) { m_joints.parent_moved( parent, m, l, a ); };
        m_race.world.motion_release = [ this ]( const entity_t& e ) { return motion_release( e ); };
        if ( !m_race.active )
            water_frame( dt );

        // The race's ticks see the world's VuWater (g 0x759058) and the display; then the view: with a race
        // running (--boat / --skin) the viewport's camera (g 0x759060 +0x28, the Viewports tick 0x4AB0A0's copy
        // of the viewport boat's VuBoatCamera): its eye +0x00 and target +0x10. The renderer takes only eye /
        // target / far (c_engine::camera): the camera's own projection +0x30 / view +0x70 (fov, roll) are not
        // consumed - SUBSTITUTION, the renderer's. The far plane stays the gfx settings' (the camera's +0x17C is
        // boat +0x408, which the Build tick 0x41F330 fills). No race: dev_vantage's orbit / freecam (scaffold).
        // The game mode manager's Decision tick: the action mode (world/action.hxx) when this race has one.
        if ( m_action )
            m_race.world.game_mode = [ this ]( float step ) {
                const std::string& next = m_action->tick( step );
                m_modes.m_next = next;   // 0x4CBFD0's tail; the change itself runs after the race's frame (it deletes the race)
                if ( m_action_reported != m_action->state( ) )
                {
                    m_action_reported = m_action->state( );
                    LOG_INFO( "action mode: %s (sync %d)%s%s", m_action_reported.c_str( ), m_race.dynamics ? m_race.dynamics->syncs : -1,
                              next.empty( ) ? "" : " - the next game mode: ", next.c_str( ) );
                }
            };
        else
            m_race.world.game_mode = nullptr;
        race_session_frame( m_race, dt );
        if ( m_menu )
            m_modes.tick( dt );
        else if ( !m_modes.m_next.empty( ) )
        {
            m_modes.change( );
            return;
        }
        // Ours (a test driver): `--end-race N` ends the session after its frame N (session.hxx race_session_end: VuGame's
        // release and the bodies' dtors) - retail's action-mode exit is not reached by our flow.
        {
            static const char* end = c_engine::option( "end-race" );
            if ( end && end[ 0 ] && m_race.active && m_race.frames == std::atoi( end ) )
                race_session_end( m_race );
        }
        // Ours (a test driver): `--deliver "N|entity path|plug"` hands the race-side receivers (drive.hxx race_event_dispatch)
        // that input plug with an empty VuParams after the session's frame N - a plug the run's own graph does not reach.
        {
            static const char* deliver = c_engine::option( "deliver" );
            if ( deliver && deliver[ 0 ] && m_race.active )
            {
                const std::string d( deliver );
                const std::size_t a = d.find( '|' ), b = a == std::string::npos ? a : d.find( '|', a + 1 );
                if ( b != std::string::npos && m_race.frames == std::atoi( d.substr( 0, a ).c_str( ) ) )
                {
                    const bool taken = race_event_dispatch( m_race, d.substr( a + 1, b - a - 1 ), d.substr( b + 1 ), nullptr );
                    LOG_INFO( "--deliver %s: %s", deliver, taken ? "taken" : "no race-side receiver" );
                }
            }
        }
        // g_vufademanager's "Final" tick 0x4AEE50 (ui/flow.hxx ui_fade_t; delta = the tick manager's = dt): after the
        // frame's other phases. Its two broadcasts (OnFadeOutComplete / OnFadeInComplete) reach no subscriber here.
        if ( m_action )
            m_fade.tick( dt, nullptr );
        // Dev trace (--verbose, ours): the event census at the session's milestone frames - the logic handlers run
        // and every plug fire that reached no handler, by class . plug.
        if ( g_verbose && m_race.active && ( m_race.frames == 960 || m_race.frames == 1920 || m_race.frames == 3840 || m_race.frames == 7680 ) )
        {
            std::string by;
            for ( const auto& u : m_unhandled_by )
                by += ( by.empty( ) ? "" : ", " ) + u.first + " x" + std::to_string( u.second );
            LOG_VERBOSE( "event census: frame %d: %d delivered (%d logic handlers), %d unhandled: %s", m_race.frames, m_fired, m_logic_fired,
                         m_unhandled_events, by.c_str( ) );
        }
        if ( m_race.active && m_race.viewport_set )
        {
            std::memcpy( m_eye, m_race.viewport_camera + 0x00, sizeof( m_eye ) );
            std::memcpy( m_target, m_race.viewport_camera + 0x10, sizeof( m_target ) );
            m_far = m_authored_far;
        }
        else if ( m_background.rig )
        {
            // --front-end: VuBackground's Decision tick 0x4BE1A0 (world/background.hxx), then the view from the
            // camera it publishes at g 0x758FB8 +0x2C0 (the rig's +0xC): eye +0, target +0x10, far +0x17C. The
            // renderer takes eye / target / far only (as for the race's viewport camera - SUBSTITUTION, the renderer's).
            background_tick( m_background, dt, m_background_env );
            const std::uint8_t* cam = m_background.rig->camera( );
            std::memcpy( m_eye, cam + 0x00, sizeof( m_eye ) );
            std::memcpy( m_target, cam + 0x10, sizeof( m_target ) );
            std::memcpy( &m_far, cam + 0x17C, sizeof( m_far ) );
        }
        else
            dev_vantage( dt );
        // Ours (a test driver): --look "ex ey ez tx ty tz" pins the view, to put a still on a chosen thing.
        if ( const char* look = c_engine::option( "look" ) )
        {
            float v[ 6 ] = { };
            std::istringstream in( look );
            for ( float& x : v )
                in >> x;
            std::memcpy( m_eye, v, sizeof( m_eye ) );
            std::memcpy( m_target, v + 3, sizeof( m_target ) );
        }
        scene_frame_rest( dt );
    }
    // VuWater's tick 0x529E30 on the built water (surface.hxx water_built_tick), then the query's view rebuilt
    // (the view is how the recreated queries read VuWater: rebuilt after each tick and at the race's start).
    // The expire hook is VuPointWaveEntity's OnExpired 0x5D4B60: the wave half (water_point_wave_expired),
    // then the entity's "OnExpired" plug through the event graph (the entity's +0xA0 = 0 is not modelled - no
    // reader of it is recreated). The ocean field rebuild and the render kick have no hook: counted.
    void water_frame( float dt )
    {
        const double ocean_start = trace::now( );
        water_built_hooks_t hooks{};
        hooks.user = this;
        hooks.expire = []( void* user, water_built_t& b, int wave )
        {
            c_world& self = *static_cast< c_world* >( user );
            water_point_wave_expired( b, wave );
            const auto owner = self.m_wave_owner.find( wave );
            if ( owner == self.m_wave_owner.end( ) )
                return;
            const std::string entity = owner->second;
            self.m_wave_owner.erase( owner );
            self.fire( entity, "OnExpired" );
        };
        const int missing = water_built_tick( m_water, dt, hooks );
        if ( missing > 0 && m_water_missing == 0 )
            LOG_WARNING( "water tick: %d unrecreated paths reached with no hook (the ocean field rebuild / render kick)", missing );
        m_water_missing += missing;
        water_view_build( m_water, m_water_view );
        trace::frame( ).ocean += trace::now( ) - ocean_start;
    }
    void scene_frame_rest( float dt )
    {
        race_draw( );
        foliage_frame( );
        water_draw( );
        // The crossing test wants the position of whatever the plane's Trigger
        // Type accepts. With no boat in the scene the dev vantage stands in,
        // so flying the orbit through a plane still exercises the real test.
        // In a race the crossings are the Triggers tick's (race_triggers, on the boats' instigators); the probe is ours.
        if ( !m_race.active )
            m_triggers.update( m_target, [ this ]( const std::string& path, const std::string& plug ) { fire( path, plug ); } );
        update_delays( dt );
        update_rotators( dt );

        const double props_start = trace::now( );
        skin_props( dt );
        animated_attach_tick( );
        update_static_draws( );
        trace::frame( ).props += trace::now( ) - props_start;

        const double pfx_start = trace::now( );
        // --no-pfx (diagnostic, ours): no pfx tick and no pfx draw - the A/B for a change that moves only the pfx.
        if ( c_engine::option( "no-pfx" ) == nullptr )
        {
            m_pfx.frame( dt, m_eye );
            race_session_pfx_draw( m_race, m_race_pfx_draw );
            m_pfx.draw( m_eye, m_target, [ this ]( const std::string& path ) { return texture_for( path ); },
                [ this ]( const std::string& path ) -> const std::vector< pfx_part_t >& { return pfx_parts( path ); }, &m_race_pfx_draw );
        }
        else
        {
            c_engine::drop( 11 );
            c_engine::drop( 14 );
        }
        m_coronas.tick( dt );
        m_coronas.draw( m_eye, m_target, [ this ]( const std::string& path ) { return texture_for( path ); } );
        trace::frame( ).pfx += trace::now( ) - pfx_start;
    }

private:
    bool m_opened = false;
    bool m_loaded = false;
    // Set when the walk found a real VuSetFrontEndCameraEntity. A race project
    // does not author one, but a screen project does, and an authored vantage
    // always beats the scaffold orbit.
    bool m_authored_camera = false;
    bool m_vantage_ready = false;
    // VuGlobalGfxSettingsEntity's "Camera Far Plane", or its ctor default.
    float m_authored_far = 500.0f;
    float m_centre[ 3 ]{};
    float m_radius = 64.0f;
    // The free camera's state (dev_freecam - scaffold).
    bool m_freecam = false;
    bool m_freecam_init = false;
    bool m_freecam_toggle_held = false;
    bool m_freecam_looking = false;
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
#if defined( _WIN32 )
    POINT m_freecam_cursor{};
#endif
    float m_orbit = 0.0f;

    c_archive m_archive{};
    // The world's VuPfx (world/effects.hxx: the verified runtime; the old c_pfx simulation is gone).
    c_effects m_pfx{};
    std::vector< std::uint32_t > m_race_pfx_draw;   // this frame's boat systems (race_session_pfx_draw)
    fleet_random_t m_vurandom{};   // VuRandom 0x736810 (seed_random)
    c_corona_entity m_coronas{};
    std::map< std::string, std::vector< cached_part_t > > m_models;
    std::map< std::string, std::vector< pfx_part_t > > m_pfx_models;
    std::map< std::string, std::vector< model_bone_t > > m_skeletons;
    std::map< std::string, c_clip > m_clips;

    // Bind-pose mesh kept for skinned hulls. The cached geometry is shared
    // between every instance of a model, so skinning writes into a per-rider
    // clone and needs the untouched bind vertices to transform from.
    std::map< std::string, skinned_mesh_t > m_skinned;
    std::vector< rotator_t > m_rotators;
    // Every entity's placed world matrix, by path - what an attach's "Parent"
    // ref resolves onto.
    std::map< std::string, std::array< float, 16 > > m_entity_world;
    // Every tree entity's transform world (scale kept out), by path - what an
    // attach's "Parent" ref seats its child on (VuOffsetAttachComponent_update
    // 0x5af900 composes onto the parent's transform world). m_entity_world above
    // (the old walk's, scale baked) now only feeds the dev vantage scaffold.
    // The template instance override map in scope for the current child loop,
    // and the single override handed to the next visit().
    const value_t* m_child_overrides = nullptr;
    const value_t* m_pending_override = nullptr;
    int m_overrides_seen = 0;
    int m_overrides_placed = 0;
    std::map< std::string, int > m_override_components;
    std::map< std::string, int > m_type_census;
    std::map< std::string, std::set< std::string > > m_authored_keys;
    std::map< std::string, std::string > m_entity_type;
    std::map< std::string, int > m_textures;
    std::map< std::string, int > m_cubes;
    // Every placement shaded with a cube (models.hxx), by its authored cube name: an asset substitution re-shades them.
    struct shaded_t
    {
        int handle = -1;
        std::string cube;
        float specular[ 3 ]{};
        float diffuse[ 3 ]{};
        float specular_amount = 0.0f;
        float environment_amount = 0.0f;
    };
    std::vector< shaded_t > m_shaded;
    // The asset factory's identity of an asset (doesAssetExist / createAsset - the asset manager's, a hook): one object
    // per "Type/Name" that exists in the pack.
    std::set< std::string > m_asset_objects;
    std::string m_sky;
    float m_eye[ 3 ]{};
    float m_target[ 3 ]{};
    float m_far = 800.0f;
    std::unique_ptr< gfx_sorted_t > m_sorted;   // the render seam (world/sorted.hxx)
    // VuFoliageManager and the level's VuFoliageEntity (models.hxx foliage_build / foliage_frame).
    struct foliage_draw_t
    {
        foliage_bucket_t* bucket = nullptr;
        int geometry = -1;
        int capacity = 0;
        std::vector< mesh_vertex_t > scratch;
    };
    foliage_manager_t m_foliage;
    std::vector< foliage_entity_t > m_foliage_entities;
    std::vector< foliage_draw_t > m_foliage_draws;
    std::uint32_t m_foliage_tint = 0xFFFFFFFFu;

    // VuWaterTextureSettingsEntity_ctor @0x5d2bd0, read in property order:
    // Gravity 9.801, WorldSize 10, WindSpeed 10, DirectionalPower 0,
    // SuppressionWaveLength 0, HeightFactor 1, TimeFactor 1,
    // NormalTextureScale 0.5 (the defaults; Hydroelectric authors none).
    // The water renderer's texture settings (VuWater +0x1C -> [0] +4 .. +0x23): VuWaterTextureSettingsEntity's Trigger
    // 0x5D2B00 copies the entity's +0x60 .. +0x7F there (the eight floats above, in that order). The renderer (the
    // platform's) is not recreated: nothing reads them yet. `m_water_texture_set` = a Trigger has written them.
    std::uint32_t m_water_texture[ 8 ]{};
    bool m_water_texture_set = false;
    std::map< std::string, value_t > m_templates;
    // The project's entity tree as retail's VuProject::load builds it.
    std::unique_ptr< entity_t > m_tree;
    // VuWater as the level builds it from the tree (water agent's surface.hxx
    // water_build_world); hold is what water_best_wake takes. Not rendered yet.
    water_built_t m_water;
    // Each emitted point wave's entity (by wave index), for the tick's expire hook.
    std::map< int, std::string > m_wave_owner;
    // The built water's tick paths reached with no hook to answer them (water_built_tick's return), summed.
    int m_water_missing = 0;
    // The query's view of m_water (kernel.hxx water_view_build) - water_view_kernel with &m_water_view as its user
    // is the water kernel the boat's tick takes (boat tick.hxx / fluid.hxx). Not consumed yet: the boat tick is
    // not wired into c_world.
    water_view_t m_water_view;
    // VuTrackManagerImpl as the race builds it from m_tree's AI waypoints (race.hxx / boat track.hxx); the AI
    // fleet and the boats are not composed (race.hxx: GameData's "Boats" is the front end's, not recreated).
    track_manager_t m_track{};
    bool m_track_built = false;
    // The race (world/session.hxx) and the databases it reads, as parsed documents.
    race_session_t m_race{};
    bool m_defer_project_initialized = false, m_project_initialized = false;
    value_t m_boat_db, m_track_db, m_constant_db, m_skin_db;
    // The water renderer (world/grid.hxx): its templates, this frame's records, and OUR draw of them - one
    // geometry holding the records expanded per index (a triangle list, capacity k_water_draw_vertices,
    // unused tail degenerate) drawn through the engine's class-7 blended pass. m_water_ambient / _diffuse:
    // VuGlobalGfxSettingsEntity's Water Ambient / Diffuse Color (memory water.md: gAmbientColor / gDiffuseColor).
    water_grid_t m_grid{};
    water_grid_frame_t m_grid_frame{};
    int m_grid_geometry = -1;
    int m_grid_placement = -1;
    int m_grid_overflow = 0;
    std::uint32_t m_water_ambient = 0xC0423C2Du;
    std::uint32_t m_water_diffuse = 0xC05A503Cu;
    static constexpr int k_water_draw_vertices = 60000;
    // Loaded VuTimedEventAssets, keyed by asset name. A control points into
    // this, so entries are only ever added.
    std::map< std::string, std::vector< timed_event_t > > m_timed;
    std::set< std::string > m_stack;

    // One Vu3dDrawStaticModelComponent: its loaded properties, its world aabb (+0x24 / +0x34, 0x531D10 at load from
    // the LOD 0 instance's scene aabb and the draw matrix +0x13C), and the placements of its model instances (+0xBC
    // LOD 0 / +0xDC LOD 1 / +0xFC LOD 2; the reflection model +0x11C is not placed - our renderer has no pass that
    // selects it). update_static_draws shows the one static_model_select (0x5B0730) picks each frame.
    struct static_draw_item_t
    {
        model_draw_t draw{};
        float aabb[ 8 ]{};
        std::vector< int > placements[ 3 ];
        int shown = -2;   // the instance whose placements are visible (-1 none; -2 before the first frame)
        // The owner (its Build tick re-hands the draw matrix: a dynamic prop's 0x546130) and each placement's part
        // node matrix (placed = draw matrix x node, models.hxx place_model).
        const entity_t* owner = nullptr;
        std::vector< std::array< float, 16 > > nodes[ 3 ];
        bool off = false;   // the component off the 3D draw manager (its +0x1E cleared: a hide 0x545F20)
    };

    // One Vu3dDrawAnimatedModelComponent instance. The clip and phase are
    // filled in by bind_animations() from the event graph, not from the
    // component, which names only the model.
    struct animated_t
    {
        entity_t* entity = nullptr;
        std::string asset;
        std::string path;
        std::string clip;
        std::string trigger;
        // "VuTimedEventAsset/<name>", empty when the driver names none.
        std::string timed;
        float world[ 16 ]{};
        float tint[ 3 ]{ 1.0f, 1.0f, 1.0f };
        float distance = 0.0f;
        float start = 0.0f;
        // Its own streamed clone and its own clock, so four pump pistons
        // sharing one model still animate out of step as authored.
        int geometry = -1;
        // The VuAnimationControl the last PlayAnimation built (@0x5ecb60).
        animation_control_t control{};
        // A single-bone clip is a RIGID motion: every vertex rides one
        // matrix, so the placement matrix can carry it and the mesh never has
        // to be touched. These instances keep the SHARED cached geometry and
        // a placement handle per part instead of a clone.
        bool rigid = false;
        std::vector< int > placements;
        // Props on an OnGameStart trigger begin at load; the rest wait for
        // their event to arrive and sit at the bind pose until it does.
        bool playing = false;
        // Copied from the driving VuPlayAnimationEntity - the params its
        // onTriggerPlug @0x5f5e80 packs. One Shot is not among them.
        bool looping = true;
        float factor = 1.0f;
    };

    // A resolved VuPlayAnimationEntity, keyed by the path of the prop it
    // drives.
    struct play_t
    {
        std::string clip;
        std::string timed;
        std::string trigger;
        std::string path;
        float start = 0.0f;
        // Authored on VuPlayAnimationEntity and previously ignored, which
        // made every clip loop forever at full speed. The "Looping" key is
        // written only when it is FALSE - every entity carrying Looping 0 is
        // a one-shot event (the rockslide, the Pitts flyby, the hangglider
        // swoops) and every entity without the key is a continuous motion
        // (the pump, the spin, the helicopter hover, the glider cruise), so
        // the default is true. Two unrelated groupings agreeing.
        bool looping = true;
        bool one_shot = false;
        float factor = 1.0f;
    };

    // VuOneShotFilterEntity: one byte of state, the flag at this+96.
    struct filter_t
    {
        bool fired = false;
    };

    // VuPlayAnimationEntity, keyed by its own path. "One Shot" is the byte at
    // +157 and `done` the latch at +168 that onTriggerPlug sets.
    struct action_t
    {
        bool one_shot = false;
        bool done = false;
    };

    // VuDelayEntity: the two authored floats and the two runtime fields the
    // constructor zeroes, in the same roles - pending at +104, remaining at
    // +108.
    struct delay_t
    {
        float delay = 1.0f;
        float window = 0.0f;
        bool pending = false;
        float remaining = 0.0f;
    };

    std::vector< animated_t > m_animated;
    std::vector< static_draw_item_t > m_static_draws;
    // Each VuStaticModelAsset's scene aabb (model_for, from load_model_scene), by asset name.
    std::map< std::string, std::array< float, 8 > > m_scene_aabbs;
    std::map< std::string, play_t > m_play;
    c_trigger_set m_triggers;
    c_light_rig m_rig;
    std::map< std::string, filter_t > m_filters;
    std::map< std::string, action_t > m_actions;
    std::map< std::string, delay_t > m_delays;
    std::map< std::string, front_end_camera_t > m_front_end;
    // update_delay: graph.hxx
    void update_delay( const std::string& path, delay_t& wait, float dt );

    // Every VuGenericEventEntity's Event Name, which is the whole bus.
    std::map< std::string, std::string > m_events;
    std::vector< std::string > m_initialized;
    // VuRaceGame's three HUD projects (+0x738 / +0x73C / +0x740; 0x4B4360 in its ctor 0x489030), loaded as data: their trees,
    // their graph merged into m_graph under the prefix "<project>:", their OnProjectInitialized nodes, whether their
    // gameInitialize ran (root +0x14 bit 2); the HUDs' VuMultiDelayEntity clocks (by prefixed path) and VuStartLightsOutAction nodes.
    struct hud_project_t
    {
        std::string name;
        std::unique_ptr< entity_t > tree;
        std::vector< std::string > initialized;
        std::vector< std::string > delays;
        bool live = false;
        // The UI agent's driver (core/ui overlay.hxx): the same project loaded through the UI's scripted world; when it
        // loads, ITS graph runs the HUD (the project's edges are then not merged into m_graph).
        std::unique_ptr< entity_t > ui_tree;
        std::unique_ptr< c_ui_race_hud > ui;
        std::map< const entity_t*, std::string > ui_trail;   // a UI-tree entity -> its path below the root
        std::vector< ui_hud_boat_t > boats;                  // ui_hud_env_fill's storage
    };
    hud_project_t m_huds[ 3 ];
#if HYDRO_UI_PASS
    std::unique_ptr< gfx_ui_pass_t > m_ui_pass;   // world/hudpass.hxx
    int m_hud_unrecreated = 0;
    ui_frame_inputs_t ui_inputs( );
    ui_screen_env_t ui_screen_environment( );
    value_t m_font_db;
    bool m_font_db_ready = false;
#endif
    void hud_env( hud_project_t& h );   // huds.hxx: ui_hud_env_fill before a HUD call
    struct multi_delay_node_t
    {
        multi_delay_t state;
        c_world* world = nullptr;
        std::string path;
        bool registered = false;
    };
    std::map< std::string, multi_delay_node_t > m_multi_delays;
    std::set< std::string > m_start_lights;
    std::set< std::string > m_ifs;
    c_joints m_joints;
    // An animated prop with attach records: its VuAnimatedModelInstance (+0x64 +0x68) and VuAnimatedSkeleton (+0x100), on
    // world/skeleton.hxx; the clip's time is the old pose path's control (animate.hxx). FLAGGED: one clip, no blend (0x5ED067's fade).
    struct prop_skel_t
    {
        skel_model_t model;
        skel_animation_t clip;
        skel_animated_t animated;
        skel_control_t control;
        bool has_clip = false, added = false;
    };
    std::map< entity_t*, std::unique_ptr< prop_skel_t > > m_prop_skels;
    bool prop_pose( const animated_t& item, std::vector< float >& palette );
    int m_motion_unknown = 0;   // a motion child's IF slot 0 not read
    // (entity, refName) -> the full path the ref points at.
    std::map< std::pair< std::string, std::string >, std::string > m_refs;
    std::map< std::string, bool > m_booleans;
    // The crossing sphere's radius. Retail's comes from
    // VuInstigatorComponent+36 via _register @0x531e10 and is written by boat
    // code at 0x41da90 / 0x421670 that has not been read; see gate_distance.
    float m_probe_radius = 0.0f;
    // Every plug's resolved links: (entity, plug) -> its +0x50 list in link order,
    // each end by path - from the retail tree (load_entity_tree), walked by fire().
    std::map< std::pair< std::string, std::string >, std::vector< edge_t > > m_graph;
    // Every VuPointWaveEntity by path, from the tree: the transform's +0x80 .. +0x8F (world position, 4 words) and
    // the entity's +0x80 .. +0x9B (point_wave_properties_t, 7 words) - VuPointWaveEntity_tick 0x5D4C50's descriptor.
    struct point_wave_source_t
    {
        std::uint32_t position[ 4 ];
        std::uint32_t properties[ 7 ];
    };
    std::map< std::string, point_wave_source_t > m_point_waves;
    // Events delivered to node kinds that are not built yet. Counted rather
    // than guessed at, so the size of the remaining graph stays visible.
    int m_unhandled_events = 0;
    // The same, per "class . plug" (the census a run logs at the end of the race).
    std::map< std::string, int > m_unhandled_by;
    // The engine's logic entities (world/logic.hxx) by path: their tree state (race.hxx logic_state_t), dispatched
    // by graph.hxx; m_logic_fired counts their handlers run.
    std::map< std::string, entity_t* > m_logic;
    std::map< std::string, std::uint32_t > m_static_pfx;   // VuStaticPfxEntity path -> its +0x94 instance (0: none)
    std::map< std::string, entity_t* > m_entities;   // every tree entity by its graph path
    light_manager_t m_lights;                        // g_vulightmanager 0x759094's +0xCC .. +0xF8 (world/settings.hxx)
    // THE AUDIO PLATFORM (FMOD's imports, world/sound.hxx sound_hooks_t): there is none on this platform yet - every call
    // retail would make is counted (and logged once per kind); getEvent answers a failure (1, not FMOD_OK) as FMOD does
    // when an event cannot be made, so nothing is started or held.
    sound_hooks_t m_sound = make_sound_hooks( this );
    int m_sound_calls[ 6 ] = { };   // getEvent, setCallback, stop, start, set3DAttributes, setWorldTransform
    static sound_hooks_t make_sound_hooks( c_world* self )
    {
        sound_hooks_t h;
        h.user = self;
        h.get_event = []( void* u, const char* name, std::uint32_t, std::uint32_t* ev, const void* ) {
            c_world& w = *static_cast< c_world* >( u );
            if ( w.m_sound_calls[ 0 ]++ == 0 )
                LOG_WARNING( "audio: FMOD getEvent('%s') - no audio platform: answered failure (further calls counted)", name );
            *ev = 0u;
            return 1;
        };
        h.set_callback = []( void* u, std::uint32_t, std::uint32_t, const void* ) { ++static_cast< c_world* >( u )->m_sound_calls[ 1 ]; };
        h.stop = []( void* u, std::uint32_t, bool ) { ++static_cast< c_world* >( u )->m_sound_calls[ 2 ]; };
        h.start = []( void* u, std::uint32_t ) { ++static_cast< c_world* >( u )->m_sound_calls[ 3 ]; };
        h.attributes = []( void* u, std::uint32_t, const float*, const float*, const float* ) { ++static_cast< c_world* >( u )->m_sound_calls[ 4 ]; };
        return h;
    }
    std::unique_ptr< c_action_mode > m_action;   // the game mode manager's current mode in a front-end race (world/action.hxx)
    action_fade_t m_fade;                        // g_vufademanager 0x758FC4
    std::function< void( ) > m_action_level;     // LoadLevel's enter while load_scene runs
    std::string m_action_reported;
    background_t m_background;           // g 0x758FF0 +0x44 (world/background.hxx; --front-end)
    camera_env_t m_background_env;       // the rig's display / VerticalFOV
    int m_logic_fired = 0;
    // Their game environment: the gameutil +0x50 root (load_scene's), no SKU (the asset manager's vt+0x7C: a platform
    // answer, not supplied) and no entity repository (find / word: not wired) - those filters count unrecreated.
    value_t m_game_util;
    // VuVoiceOverManagerImpl g 0x758FEC (world/voice.hxx): one for the program, loaded once (vo_ready); m_crt_rand the
    // CRT rand's _holdrand. SUBSTITUTION: rand (MSVCR110 0x64C1F8) is the VC CRT's published LCG (x 214013 + 2531011,
    // bits 16..30); retail imports no srand, so the seed is the CRT's 1, and the VO shuffle 0x4B8F90 is rand's only
    // caller in the image (one call site, 0x4B8FC6), so the sequence is the one retail draws.
    vo_manager_t m_vo;
    vo_hooks_t m_vo_hooks;
    std::uint32_t m_crt_rand = 1u;
    bool m_vo_ready = false;
    value_t m_vo_db;
    int m_vo_subtitles = 0, m_vo_ducks = 0;
    void vo_ready( )
    {
        if ( m_vo_ready )
            return;
        m_vo_ready = true;
        m_vo_hooks.rand = [ this ]( ) -> std::int32_t {
            m_crt_rand = m_crt_rand * 214013u + 2531011u;
            return static_cast< std::int32_t >( ( m_crt_rand >> 16 ) & 0x7FFFu );
        };
        m_vo_hooks.get_event = [ this ]( const std::string& name, std::uint32_t& event ) {
            return m_sound.get_event ? m_sound.get_event( m_sound.user, name.c_str( ), 1u, &event, nullptr ) : 1;
        };
        // setCallback( event, 0x4B8180, the waveform ): the audio platform's hook carries a holder, not the userdata word.
        m_vo_hooks.set_callback = [ this ]( std::uint32_t event, std::int32_t ) {
            if ( m_sound.set_callback )
                m_sound.set_callback( m_sound.user, event, 0x4B8180u, nullptr );
        };
        m_vo_hooks.start = [ this ]( std::uint32_t event ) {
            if ( m_sound.start )
                m_sound.start( m_sound.user, event );
        };
        // The subtitle manager g 0x758FE4 vt+0x1C and the ducking 0x4F94B0 / 0x4F95B0 (the audio platform): hooks, counted.
        m_vo_hooks.subtitle = [ this ]( const char* text ) {
            if ( m_vo_subtitles++ == 0 )
                LOG_WARNING( "voice over: the subtitle manager g 0x758FE4 vt+0x1C('%s') - not recreated (counted)", text );
        };
        m_vo_hooks.duck = [ this ]( const std::string&, float, float ) -> std::uint32_t { ++m_vo_ducks; return 0u; };
        m_vo_hooks.unduck = [ this ]( std::uint32_t ) { ++m_vo_ducks; };
        m_vo_hooks.viewports = [ this ]( ) { return static_cast< std::int32_t >( m_race.world.viewports ); };
        m_vo_hooks.boat_ok = [ this ]( const void* boat ) { return race_session_vo_boat_ok( m_race, boat ); };
        std::vector< std::uint8_t > bytes;
        if ( !m_archive.read( "VuDBAsset/VoiceOverDB", bytes ) )
        {
            LOG_WARNING( "voice over: VuDBAsset/VoiceOverDB not in the pack - the manager stays empty (UNKNOWN)" );
            return;
        }
        m_vo_db = c_reader( bytes ).parse( );
        vo_load( m_vo, member( m_vo_db, "VuDBAsset" ), m_vo_hooks );
        LOG_INFO( "voice over: VoiceOverDB loaded (slot 5 0x4B7970) - %u lines, ducking '%s' %.3f", static_cast< unsigned >( m_vo.entries.size( ) ),
                  m_vo.duck_category.c_str( ), m_vo.duck_volume );
    }
    // VuStringDBImpl (world/strings.hxx), the HUD's m_string_db; m_string_asset the loaded document.
    string_db_t m_string_db;
    value_t m_string_asset;
    bool m_string_db_ready = false;
    logic_game_t m_logic_game;
    int m_logic_unrecreated = 0;
    // UpdateBranchProbabilities events reached without their VuParams (UNKNOWN, graph.hxx).
    int m_waypoint_params_unknown = 0;
    // Assets already scored by the extent test, so it reports once each.
    std::set< std::string > m_scored;
    // Route single-bone props through the placement matrix instead of CPU
    // skinning. Kept switchable so the saving can be measured rather than
    // asserted: --stream-rigid-props forces them back through the vertex path.
    bool m_rigid_props = true;
    // --drive: let the AI controller steer the player boat round the gate
    // chain, so an unattended bench run exercises anything that needs the
    // player to actually move.
    bool m_drive = false;
    // Distance the player has travelled and how many graph events have fired,
    // so a run can show that a trigger fired AFTER motion rather than at t=0.
    float m_driven = 0.0f;
    int m_fired = 0;
    float m_last[ 2 ]{};
    bool m_tracked = false;
    float m_reported = 0.0f;
    int m_ticks = 0;
    std::string m_aim;
    int m_probes = 0;
    int m_draws = 0;
};

#include <workspace/core/world/graph.hxx>
#include <workspace/core/world/menu.hxx>
#include <workspace/core/world/saves.hxx>
#include <workspace/core/world/motion.hxx>
#include <workspace/core/world/animate.hxx>
#include <workspace/core/world/entities.hxx>
#include <workspace/core/world/huds.hxx>
#include <workspace/core/world/front.hxx>
#include <workspace/core/world/models.hxx>
