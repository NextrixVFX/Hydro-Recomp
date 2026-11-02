#pragma once

// The front end's two scene entities that show the player's choice (boat batch 34, case front_props, cases/gameplug.py): VuDockedBoatEntity (vtable 0x73A45C, create
// 0x435800) keeps a whole VuBoatEntity at the dock, VuPosterEntity (vtable 0x73ABD0) the selected track's poster. Both read their state from gameutil +0x50.
// HOOKS, the caller's: the boat's creation 0x436230 (new 0x520 VuBoatEntity, the document, placed at this entity's transform: the same steps as ghost.hxx's make_boat),
// its removal 0x500B80 + the release, the Skin property's set (0x500630 "Skin", vt+0x10( value, 1 )), its DB 0x41D500, +0x466 the Locked byte, the voice event, the model
// reload 0x43AF50, the asset test 0x43B080. NOT RECREATED: the dock's "Build" tick 0x436450. The properties (Position X, Position Y, Rotation Z at +0x5C +0x60 +0x64) are
// schema.hxx's.

#include <workspace/core/world/logic.hxx>

struct docked_hooks_t
{
    value_t* game_data = nullptr;                                                   // gameutil +0x50
    const value_t* boat_db = nullptr;                                               // gameutil +4's document: DefaultBoatProperties
    std::function< std::uint32_t( const value_t& document ) > make;                 // 0x436230
    std::function< void( std::uint32_t boat ) > drop;                               // 0x500B80, the release
    std::function< const value_t&( std::uint32_t boat ) > db;                       // 0x41D500
    std::function< void( std::uint32_t boat, const value_t& skin ) > set_skin;      // 0x500630 "Skin", vt+0x10( value, 1 )
    std::function< void( std::uint32_t boat, std::uint8_t locked ) > set_locked;    // boat +0x466
    std::function< void( const char* event ) > voice;                               // FMOD getEvent + start
    std::function< void( ) > tick_on;                                               // "Build"
    std::function< void( ) > tick_off;
};

struct docked_boat_t
{
    std::uint32_t boat = 0;   // +0x68
};

inline const value_t& docked_defaults( const docked_hooks_t& h )
{
    return member( member( member( *h.boat_db, "VuDBAsset" ), "Boat" ), "DefaultBoatProperties" );
}

// The boat's document: Properties, Driver.Type = "UI".
inline value_t docked_document( const value_t& properties )
{
    value_t doc;
    json_member( doc, "Properties" ) = properties;
    json_set_string( json_member( json_member( doc, "Driver" ), "Type" ), "UI" );
    return doc;
}

inline bool docked_true( const value_t& v )
{
    return v.tag == 3u && static_cast< std::uint8_t >( v.integer ) != 0u;
}

// Slot 0x435B00 (game initialize): Properties, filled from DefaultBoatProperties when null; the boat; +0x466; the tick.
inline void docked_initialize( docked_boat_t& e, const docked_hooks_t& h )
{
    value_t& dock = json_member( *h.game_data, "DockedBoat" );
    value_t& properties = json_member( dock, "Properties" );
    const value_t& locked = member( dock, "Locked" );
    const std::uint8_t lock = locked.tag == 3u ? static_cast< std::uint8_t >( locked.integer ) : 0u;
    if ( properties.tag == 0u )
        properties = docked_defaults( h );
    e.boat = h.make( docked_document( properties ) );
    h.set_locked( e.boat, lock );
    h.tick_on( );
}

// 0x435C90 (game release): the boat out, the tick off.
inline void docked_release( docked_boat_t& e, const docked_hooks_t& h )
{
    h.drop( e.boat );
    h.tick_off( );
}

// 0x435CC0 "OnChangeDockedBoat" ( boat, skin, locked ): the document updated; a new boat when the name changed, with the voice line when the boat became IsCrazy; a new skin
// on the same boat otherwise.
inline void docked_change( docked_boat_t& e, const ui_params_t& in, const docked_hooks_t& h )
{
    value_t& dock = json_member( *h.game_data, "DockedBoat" );
    json_member( dock, "Properties" );
    json_member( dock, "Locked" );          // both made first: json_member's vector moves its siblings
    value_t& properties = json_member( dock, "Properties" );
    json_member( properties, "Boat" );
    json_member( properties, "Skin" );
    ui_params_reader_t r( in );
    const std::string boat = r.read_string( ), skin = r.read_string( );
    std::uint8_t locked = 0u;
    if ( r.m_remaining > 0 )
    {
        std::uint32_t type;
        std::memcpy( &type, r.m_at, 4 );
        if ( type == 3u )
            locked = r.m_at[ 4 ];
    }
    const value_t& old_boat = json_member( properties, "Boat" );
    const bool new_boat = ( old_boat.tag == 4u ? old_boat.text : std::string( ) ) != boat;
    const value_t& old_skin = json_member( properties, "Skin" );
    const bool new_skin = ( old_skin.tag == 4u ? old_skin.text : std::string( ) ) != skin;
    json_set_string( json_member( properties, "Boat" ), boat );
    json_set_string( json_member( properties, "Skin" ), skin );
    value_t& lock = json_member( dock, "Locked" );
    json_clear( lock );
    lock.tag = 3u;
    lock.kind = value_t::kind_t::integer;
    lock.integer = locked;
    if ( new_boat )
    {
        const bool was = docked_true( member( h.db( e.boat ), "IsCrazy" ) );
        const value_t doc = docked_document( properties );
        h.drop( e.boat );
        e.boat = h.make( doc );
        if ( docked_true( member( h.db( e.boat ), "IsCrazy" ) ) && !was )
            h.voice( "Global/Voice/vo_menu_YoureCrazy" );
    }
    else if ( new_skin )
        h.set_skin( e.boat, json_member( properties, "Skin" ) );
    h.set_locked( e.boat, locked );
}

// 0x436060 "OnResetDockedBoat": the change with the default Boat and Skin, unlocked.
inline void docked_reset( docked_boat_t& e, const docked_hooks_t& h )
{
    const value_t& d = docked_defaults( h );
    const value_t& boat = member( d, "Boat" );
    const value_t& skin = member( d, "Skin" );
    ui_params_t p;
    p.add_string( boat.tag == 4u ? boat.text.c_str( ) : "" );
    p.add_string( skin.tag == 4u ? skin.text.c_str( ) : "" );
    scripted_add_bool( p, 0u );
    docked_change( e, p, h );
}

struct poster_hooks_t
{
    value_t* game_data = nullptr;                                 // gameutil +0x50
    std::function< bool( const std::string& name ) > has_asset;   // 0x43B080
    std::function< void( ) > reload;                              // 0x43AF50
};

// 0x43AC50 ( model name, locked ): Poster.ModelAssetName, Locked (false unless a bool record follows, 0x42C760); a name the factory lacks becomes "UI/Level_DLC"; the reload.
inline void poster_change( const ui_params_t& in, const poster_hooks_t& h )
{
    value_t& poster = json_member( *h.game_data, "Poster" );
    json_member( poster, "ModelAssetName" );
    json_member( poster, "Locked" );        // both made first: json_member's vector moves its siblings
    value_t& model = json_member( poster, "ModelAssetName" );
    value_t& locked = json_member( poster, "Locked" );
    ui_params_reader_t r( in );
    json_set_string( model, r.read_string( ) );
    json_clear( locked );
    locked.tag = 3u;
    locked.kind = value_t::kind_t::integer;
    locked.integer = 0;
    if ( r.m_remaining > 0 )
    {
        std::uint32_t type;
        std::memcpy( &type, r.m_at, 4 );
        if ( type == 3u )
            locked.integer = r.m_at[ 4 ];
    }
    if ( !h.has_asset( model.tag == 4u ? model.text : std::string( ) ) )
        json_set_string( model, "UI/Level_DLC" );
    h.reload( );
}

// 0x43ADA0: the change with an empty name.
inline void poster_reset( const poster_hooks_t& h )
{
    ui_params_t p;
    p.add_string( "" );
    poster_change( p, h );
}

// 0x43AEA0's colour (+0x7C, then the draw 0x520130): alpha 0x80 when locked, else opaque white.
inline std::uint32_t poster_color( const value_t& game_data )
{
    const value_t& v = member( member( game_data, "Poster" ), "Locked" );
    return v.tag == 3u && static_cast< std::uint8_t >( v.integer ) != 0u ? 0x80FFFFFFu : 0xFFFFFFFFu;
}
