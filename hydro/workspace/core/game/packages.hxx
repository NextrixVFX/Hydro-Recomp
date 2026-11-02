#pragma once

// VuHiddenPackageManagerImpl (g 0x758FD0, 8 bytes, create 0x4AFEC0, vtable 0x7456D8; boat batch 33): which hidden packages this save
// has found, in the game save's "PackagesFound" object (key: the package's name, value true) over HiddenPackagesDB's
// "PackagesInLevel" counts. Read from the decompilation. VuPowerUpHiddenPackageEntity's hooks (boat/powerup.hxx
// powerup_package_hooks_t) are these slots: is_found 7, set_found 6, first 8, second 9.
// HOOKS: the cloud save 0x4ABD60 (`save`), the achievement progress 0x4AA1A0 on g 0x758F98 (`progress`: the platform's).

#include <workspace/core/assets/tree.hxx>
#include <workspace/core/entity/json.hxx>
#include <workspace/core/game/unlock.hxx>

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

// 0x6F75D0: 1 / 110, the bits read from the image.
inline float collector_scale( )
{
    const std::uint32_t bits = 0x3C14F209u;
    float f;
    std::memcpy( &f, &bits, 4 );
    return f;
}

class c_hidden_packages
{
public:
    const value_t* db = nullptr;                                         // +4's +0x58: HiddenPackagesDB's document
    std::function< void( ) > save;                                       // 0x4ABD60
    std::function< void( const char* name, float percent ) > progress;   // 0x4AA1A0 (the name on the stack, the percent in xmm2)

    // Slot 5 0x4B0270 (the asset's load is the caller's): false with no document.
    bool init( const value_t* database )
    {
        db = database;
        return db != nullptr;
    }

    // Slot 6 0x4AFEF0: an object made of the member when it is not one (0x4FC6B0), [ name ] = true, the save, then "Collector"
    // at ( found x 100 ) x 0x6F75D0.
    void set_found( value_t& cloud_data, const std::string& name ) const
    {
        value_t& found = json_member( cloud_data, "PackagesFound" );
        if ( found.tag != 6 )
        {
            json_clear( found );
            found.tag = 6;
            found.kind = value_t::kind_t::object;
        }
        value_t& v = json_member( found, name );
        json_clear( v );
        v.tag = 3;
        v.kind = value_t::kind_t::integer;
        v.integer = 1;
        if ( save )
            save( );
        const std::int32_t total = found_total( cloud_data );
        if ( progress )
            progress( "Collector", static_cast< float >( static_cast< std::int32_t >( static_cast< std::uint32_t >( total ) * 100u ) ) * collector_scale( ) );
    }

    // Slot 7 0x4AFFA0.
    static bool is_found( const value_t& cloud_data, const std::string& name )
    {
        const value_t& v = member( member( cloud_data, "PackagesFound" ), name.c_str( ) );
        return v.tag == 3 && v.integer != 0;
    }

    // Slot 8 0x4B0000: `out` = 0 first; false when the level's count is no number.
    bool level_total( const std::string& level, std::int32_t& out ) const
    {
        out = 0;
        static const value_t null{ };
        const value_t& v = member( member( db != nullptr ? member( *db, "VuDBAsset" ) : null, "PackagesInLevel" ), level.c_str( ) );
        return unlock_int( v, out );
    }

    // Slot 9 0x4B0090: true members whose key begins with the level's name (0x43E030 is a find: only a match at 0 counts).
    static void level_found( const value_t& cloud_data, const std::string& level, std::int32_t& out )
    {
        out = 0;
        const value_t& found = member( cloud_data, "PackagesFound" );
        if ( found.tag != 6 )
            return;
        for ( const auto& f : found.object )
            if ( f.first.find( level ) == 0u && f.second.tag == 3 && f.second.integer != 0 )
                ++out;
    }

    // Slot 10 0x4B0140 (out by reference in retail).
    static std::int32_t found_total( const value_t& cloud_data )
    {
        std::int32_t out = 0;
        const value_t& found = member( cloud_data, "PackagesFound" );
        if ( found.tag == 6 )
            for ( const auto& f : found.object )
                if ( f.second.tag == 3 && f.second.integer != 0 )
                    ++out;
        return out;
    }

    // Slot 11 0x4B01D0.
    bool level_complete( const value_t& cloud_data, const std::string& level ) const
    {
        std::int32_t total = 0, have = 0;
        if ( !level_total( level, total ) )
            return false;
        level_found( cloud_data, level, have );
        return have >= total;
    }

    // Slot 12 0x4B0250: 0x4FC140 erases the member.
    static void clear( value_t& cloud_data )
    {
        for ( auto it = cloud_data.object.begin( ); it != cloud_data.object.end( ); ++it )
            if ( it->first == "PackagesFound" )
            {
                cloud_data.object.erase( it );
                return;
            }
    }
};
