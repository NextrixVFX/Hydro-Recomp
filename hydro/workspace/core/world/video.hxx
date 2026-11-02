#pragma once

#include <workspace/core/assets/tree.hxx>
#include <workspace/core/entity/json.hxx>
#include <workspace/core/world/strings.hxx>

#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

// OURS, NOT RETAIL: the user's Video settings screen. Screens/SettingsGraphics is patched in memory as it loads, the game's
// files untouched: "Vertical Sync" (FlipInterval01) becomes the Frame Rate Limit, and clones of retail's own combo and slider
// entities (so their look, handlers and input) add VSync, Field of View, Camera Shake and Camera Zoom Out as a second column.
// What each does: ui/slider.hxx ui_ours_tag and ui_settings_ours_t. Retail's project reads as authored everywhere else: the
// self-test binds it unpatched, and no retail entity carries one of these names.

inline const char* const k_video_settings = "SettingsGraphics/Settings/";

inline value_t* video_child( value_t& list, const char* name )
{
    for ( value_t& e : list.list )
        if ( member( e, "name" ).text == name )
            return &e;
    return nullptr;
}

// Plug `plug`'s connections, replaced: { entity's name in the Settings container, its plug }.
inline void video_links( value_t& e, const char* plug, std::initializer_list< std::pair< const char*, const char* > > links )
{
    value_t& connections = json_member( json_member( json_member( json_member( json_member( json_member( e, "data" ), "Components" ), "VuScriptComponent" ), "Plugs" ), plug ), "Connections" );
    json_clear( connections );
    connections.tag = 5;
    connections.kind = value_t::kind_t::list;
    for ( const auto& link : links )
    {
        value_t one;
        json_set_string( json_member( one, "EntityName" ), std::string( k_video_settings ) + link.first );
        json_set_string( json_member( one, "PlugName" ), link.second );
        connections.list.push_back( std::move( one ) );
    }
}

inline void video_rename( value_t& v, const std::string& from, const std::string& to )
{
    if ( v.tag == 4 )
    {
        const std::size_t at = v.text.find( from );
        if ( at != std::string::npos && at + from.size( ) == v.text.size( ) )
            v.text.replace( at, from.size( ), to );
    }
    for ( value_t& e : v.list )
        video_rename( e, from, to );
    for ( auto& field : v.object )
        video_rename( field.second, from, to );
}

inline value_t& video_properties( value_t& e )
{
    return json_member( json_member( e, "data" ), "Properties" );
}

// One step of the ring, top to bottom of each column: NavDown / NavUp out, SetFocus in from both neighbours.
inline void video_nav( value_t& e, const char* up, const char* down )
{
    video_links( e, "NavUp", { { up, "SetFocus" } } );
    video_links( e, "NavDown", { { down, "SetFocus" } } );
    video_links( e, "SetFocus", { { up, "NavDown" }, { down, "NavUp" } } );
}

inline void video_screen_patch( value_t& document )
{
    value_t& root = json_member( json_member( document, "VuProject" ), "RootEntity" );
    value_t* settings = video_child( json_member( json_member( root, "data" ), "ChildEntities" ), "Settings" );
    if ( settings == nullptr )
        return;
    value_t& list = json_member( json_member( *settings, "data" ), "ChildEntities" );
    video_rename( root, "FlipInterval01", "OursFrameLimit" );
    value_t* limit = video_child( list, "OursFrameLimit" );
    const value_t* scale = video_child( list, "DisplayScale01" );
    if ( limit == nullptr || scale == nullptr )
        return;
    json_set_string( json_member( video_properties( *limit ), "Heading String ID" ), "Settings_Ours_FrameLimit" );

    // The second column stands one frame width (515) left of retail's.
    value_t vsync = *limit;
    json_set_string( json_member( vsync, "name" ), "OursVSync" );
    json_set_string( json_member( video_properties( vsync ), "Heading String ID" ), "Settings_Ours_VSync" );
    json_set_int( json_member( video_properties( vsync ), "Position X" ), 185 );
    json_set_int( json_member( video_properties( vsync ), "Position Y" ), 170 );

    const auto slider = [ & ]( const char* name, const char* text, int y, int bars ) {
        value_t s = *scale;
        json_set_string( json_member( s, "name" ), name );
        value_t& p = video_properties( s );
        json_set_string( json_member( p, "String ID" ), text );
        json_set_int( json_member( p, "FrameX" ), 15 );
        json_set_int( json_member( p, "FrameY" ), y );
        json_set_int( json_member( p, "Bar Count" ), bars );
        const float wide = 370.0f / static_cast< float >( bars );   // retail's bar strip, 10 x 37
        json_set_float( json_member( json_member( p, "Bar Dst Delta" ), "X" ), wide );
        json_set_float( json_member( p, "Bar DstWidth" ), wide );
        return s;
    };
    value_t fov = slider( "OursFieldOfView", "Settings_Ours_FieldOfView", 290, 12 );     // 5 degrees a bar
    value_t shake = slider( "OursCameraShake", "Settings_Ours_CameraShake", 380, 10 );   // 20% a bar
    value_t zoom = slider( "OursCameraZoomOut", "Settings_Ours_CameraZoomOut", 470, 10 );

    // The ring: retail's column, then ours, then Default: left / right change a value, so up / down walk everything.
    video_links( *limit, "NavUp", { { "UnitsSetting", "SetFocus" } } );
    video_links( *limit, "NavDown", { { "OursVSync", "SetFocus" } } );
    video_links( *limit, "SetFocus", { { "UnitsSetting", "NavDown" }, { "OursVSync", "NavUp" } } );
    video_nav( vsync, "OursFrameLimit", "OursFieldOfView" );
    video_nav( fov, "OursVSync", "OursCameraShake" );
    video_nav( shake, "OursFieldOfView", "OursCameraZoomOut" );
    video_nav( zoom, "OursCameraShake", "DefaultButton" );
    if ( value_t* units = video_child( list, "UnitsSetting" ) )
        video_nav( *units, "DisplayScale01", "OursFrameLimit" );
    if ( value_t* button = video_child( list, "DefaultButton" ) )
    {
        video_links( *button, "NavUp", { { "OursCameraZoomOut", "SetFocus" } } );
        video_links( *button, "SetFocus", { { "Fidelity01", "NavUp" }, { "OursCameraZoomOut", "NavDown" } } );
    }
    list.list.push_back( std::move( vsync ) );
    list.list.push_back( std::move( fov ) );
    list.list.push_back( std::move( shake ) );
    list.list.push_back( std::move( zoom ) );
}

// The screen's new text, every language alike: StringDB has no entry for any of it.
inline void video_strings( string_db_t& db )
{
    static const char* const rows[ ][ 2 ] = {
        { "Settings_Ours_FrameLimit", "Frame Rate Limit" }, { "Settings_Ours_VSync", "VSync" }, { "Settings_Ours_FieldOfView", "Field of View" },
        { "Settings_Ours_CameraShake", "Camera Shake" }, { "Settings_Ours_CameraZoomOut", "Camera Zoom Out" }, { "Settings_Ours_Unlimited", "Unlimited" },
        { "Settings_Ours_Limit_30", "30" }, { "Settings_Ours_Limit_60", "60" }, { "Settings_Ours_Limit_75", "75" }, { "Settings_Ours_Limit_90", "90" },
        { "Settings_Ours_Limit_120", "120" }, { "Settings_Ours_Limit_144", "144" }, { "Settings_Ours_Limit_165", "165" }, { "Settings_Ours_Limit_240", "240" },
        { "Settings_Ours_Limit_360", "360" } };
    for ( const auto& row : rows )
        db.strings[ string_db_hash( row[ 0 ] ) ] = row[ 1 ];
}
