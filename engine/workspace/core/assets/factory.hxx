#pragma once

#include <workspace/core/assets/package.hxx>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <xmmintrin.h>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// VuAssetFactoryImpl's core (boat batch 24, the asset loaders): g_vuassetfactoryimpl 0x759004, 0xA8 bytes, vtable
// 0x74BDCC, built by 0x4ECFD0 (new 0xA8, ctors 0x4ED060 / 0x4ED0F0). What is recreated here and checked against
// retail run on a factory retail built itself (oracle case asset_factory):
//   - the type registry: registerType slot 6 0x4ED9E0 and VuAssetFactory_registerAllTypes 0x56B730's 22 types;
//   - the asset key hash 0x4ECB80;
//   - the loaded-asset table +0x54: createAsset slot 7 0x4EDA20 up to its package walk, addRef slot 8 0x4EDE60,
//     release slot 9 0x4EDE70, findAsset slot 22 0x4EF4D0 (hash) / slot 23 0x4EF4B0 (type, name);
//   - the substitutes +0x9C: lookup 0x4F1C90, addSubstitute slot 42 0x4EF8E0, removeSubstitute slot 43 0x4EF9A0.
// createAsset past the cache is recreated since boat batch 25 (package.hxx + below, oracle case asset_package): the
// packages +0x48 (loadPackage 0x4EFA50 -> 0x4F0090), the AssetData entry and its used byte, skus 0x4F1CF0 / langs
// 0x55FC50, the pack read + version check 0x4F08F0, later packages' patches 0x4F1B50, newly loaded packages' patches
// of loaded assets 0x4F05D0, the per-type name vectors 0x4F0710. The caller's (asset_factory_hooks_t): the record's
// create function, the asset's own load (vt+0x14) and patch (vt+8), the listeners, the platform file system (where
// <name>.apf lives), the frame sync, the delete. NOT recreated (`unknown` is set, the answer is false / 0): the loose
// Assets loader 0x4EFBB0 (no <name>.apf), 0x4F08F0's path for an asset its pack does not list (0x4F0EF0 / 0x4F1330 /
// the bake: the dev tree's raw files). With no package loaded createAsset answers 0 for every name the table does not
// hold, as retail does.
// SUBSTITUTIONS (flagged): the +0x40 type map is std::map< std::string, ... > (retail's is the same std::map: find
// 0x4F22B0, operator[] 0x4F23C0); the +0x54 table is std::unordered_map< std::uint32_t, vu_asset_t* > (retail's is
// MSVC's _Hash, list + bucket vector, mask 7 / maxidx 8 after construction - its iteration order, which only slots
// 25 / 37 walk, is not modelled); the +0x9C substitutes are a std::vector in insertion order (retail: 28-byte
// { hash, std::string } records, push_back 0x4F27F0, erase-by-shift 0x4F4470).

// 0x4ECB80, fastcall (ecx type, edx name): FNV-1a 32 over the type's bytes, then the name's, no separator.
inline std::uint32_t asset_key_hash( const char* type, const char* name )
{
    std::uint32_t h = 0x811C9DC5u;
    for ( const unsigned char* p = reinterpret_cast< const unsigned char* >( type ); *p != 0; ++p )
        h = ( h ^ *p ) * 16777619u;
    for ( const unsigned char* p = reinterpret_cast< const unsigned char* >( name ); *p != 0; ++p )
        h = ( h ^ *p ) * 16777619u;
    return h;
}

// One registerType call of VuAssetFactory_registerAllTypes 0x56B730, in its order: the type name, the create
// function (record +0xC), the second function (+0x10: the bake - called only by slot 17 0x4EE3B0, which nothing
// calls), the version (+0x14) and the flag byte (+0x18).
struct asset_type_t
{
    const char* name;
    std::uint32_t create;
    std::uint32_t bake;
    std::uint32_t version;
    bool flag;
};

inline constexpr asset_type_t g_asset_types[] = {
    { "VuAnimatedModelAsset", 0x5A81B0u, 0x5A81E0u, 15u, true },
    { "VuAnimationAsset", 0x515E90u, 0x515EB0u, 1u, true },
    { "VuAssetListAsset", 0x5644E0u, 0x55C330u, 1u, true },
    { "VuAudioBankAsset", 0x5CB3F0u, 0x5CB420u, 1u, false },
    { "VuAudioProjectAsset", 0x5CAFD0u, 0x5CAFF0u, 1u, true },
    { "VuAudioStreamAsset", 0x5C3820u, 0x5C3900u, 1u, false },
    { "VuCollisionMeshAsset", 0x518EC0u, 0x518F50u, 7u, true },
    { "VuCompiledShaderAsset", 0x53FEB0u, 0x53FF30u, 2u, true },
    { "VuCubeTextureAsset", 0x5ADF50u, 0x5ADF70u, 13u, true },
    { "VuDBAsset", 0x55C280u, 0x55C330u, 1u, true },
    { "VuFluidsMeshAsset", 0x522720u, 0x522760u, 2u, true },
    { "VuFontAsset", 0x5CACC0u, 0x5CACE0u, 1u, true },
    { "VuPfxAsset", 0x5CAB00u, 0x55C330u, 1u, true },
    { "VuProjectAsset", 0x554ED0u, 0x554EF0u, 1u, true },
    { "VuShaderAsset", 0x5CA9F0u, 0x55C330u, 1u, true },
    { "VuStaticModelAsset", 0x5429F0u, 0x542A10u, 17u, true },
    { "VuStringAsset", 0x5A9800u, 0x55C330u, 1u, true },
    { "VuTemplateAsset", 0x5BB0E0u, 0x55C330u, 1u, true },
    { "VuTextureAsset", 0x539790u, 0x5397B0u, 18u, true },
    { "VuTimedEventAsset", 0x5C8CD0u, 0x5C8D50u, 1u, true },
    { "VuWaterMapAsset", 0x5C7D60u, 0x5C7DA0u, 2u, true },
    { "VuXmlAsset", 0x5C7920u, 0x5C7A70u, 1u, true },
};

// The factory-side words of a VuAsset (the base ctor, inlined e.g. in 0x515DB0): +8 the name, +0x48 the key hash
// (createAsset's miss path writes it), +0x4C the reference count (the ctor's 1). `object` is the caller's asset.
struct vu_asset_t
{
    std::string type;
    std::string name;
    std::uint32_t hash = 0;
    std::int32_t refs = 1;
    void* object = nullptr;
    // +0x20 the language (0x55FC50), +0x38 the AssetData info (0x55FC50's copy of 0x4F1CF0's pick), +0x50 the pack
    // record's hash once the load succeeded (0x4F08F0).
    std::string lang;
    value_t info;
    std::uint32_t data_hash = 0;
};

// The record's create, the asset's own virtuals, the listeners and the platform calls: the caller's.
struct asset_factory_hooks_t
{
    void* user = nullptr;
    // NO LONGER CALLED (boat batch 25: the package walk is recreated). Kept for source compatibility.
    bool ( *listed )( void* user, const std::string& type, const std::string& name ) = nullptr;
    // The type record's create function (+0xC), called with no arguments; answers a new asset, refs 1.
    vu_asset_t* ( *create )( void* user, const std::string& type ) = nullptr;
    // NO LONGER CALLED (batch 25): load_data is the asset's load now.
    void ( *load )( void* user, vu_asset_t& asset ) = nullptr;
    // The +0x88 listeners' vt+4(asset).
    void ( *created )( void* user, vu_asset_t& asset ) = nullptr;
    // release's / createAsset's frame sync 0x53A400 when g_vugfxsort +0xC is set (VuMetroThread vt+0x3C, D3D11
    // vt+0x30 / +0xD0): platform.
    void ( *sync )( void* user ) = nullptr;
    // The asset's vt+4(1), the scalar deleting dtor.
    void ( *destroy )( void* user, vu_asset_t* asset ) = nullptr;
    // The asset's vt+0x14 load( { data, size } ) on the pack record's bytes: its answer.
    bool ( *load_data )( void* user, vu_asset_t& asset, const std::vector< std::uint8_t >& data ) = nullptr;
    // The asset's vt+8 patch( loaded by a later package ): its answer.
    bool ( *patch )( void* user, vu_asset_t& asset, vu_asset_t& from ) = nullptr;
    // The +0x88 listeners' vt+0( package name ), after a package load succeeded.
    void ( *package_loaded )( void* user, const std::string& name ) = nullptr;
    // The platform file system (g_vumetrofile vt+0x28 exists, then the open the pack makes): the path to open for
    // a file name such as "Base.apf", or "" when there is none.
    std::string ( *pack_path )( void* user, const std::string& file ) = nullptr;
    // The self-test's header variants: the 68 bytes served for the pack's header, or null for the file's own.
    const unsigned char* pack_header = nullptr;
    // The asset's vt+0xC, called by slot 37 on every loaded asset.
    void ( *reload )( void* user, vu_asset_t& asset ) = nullptr;
    // 0x4ED410's platform reads: g_vumetrosys vt+0x48 (the language text) and g_vumetrofile's "GameInfo.json": false
    // when the file is not there (the shipped install has none); the document otherwise, `parsed` = 0x4FEDF0's answer.
    std::string ( *language )( void* user ) = nullptr;
    bool ( *game_info )( void* user, value_t& document, bool& parsed ) = nullptr;
};

// One +0x88 listener (a std::list of interface pointers in retail: add slot 28 0x4ECEC0 pushes at the back, remove
// slot 29 0x4ECF30 = 0x4A44A0 std::list::remove, EVERY node of that pointer). The hooks below stay the caller's first
// listener; these run after it, in list order. vt+0 package loaded, vt+4 asset created, vt+8 packages unloaded.
struct asset_listener_t
{
    void* user = nullptr;
    void ( *package_loaded )( void* user, const std::string& name ) = nullptr;
    void ( *created )( void* user, vu_asset_t& asset ) = nullptr;
    void ( *unloaded )( void* user ) = nullptr;
};

class c_asset_factory
{
public:
    // The +0x40 map's value: +0 the asset names every package lists for the type (slot 12's answer, filled by
    // 0x4F0710), +0xC create, +0x10 bake, +0x14 version, +0x18 flag.
    struct record_t
    {
        std::vector< std::string > names;
        std::uint32_t create = 0;
        std::uint32_t bake = 0;
        std::uint32_t version = 0;
        bool flag = false;
    };

    // A path past what is recreated was reached (the loose loader 0x4EFBB0, 0x4F08F0's unlisted-asset path, a pack
    // path archive.hxx flags): the answer given was false / 0, retail's is not known.
    bool unknown = false;

    asset_factory_hooks_t hooks{};

    // registerType, slot 6 0x4ED9E0: push the name onto +0x34 (0x431390, a std::vector< std::string > push_back),
    // then +0x40[ name ] (operator[] 0x4F23C0) gets the four fields - a second registration of a name overwrites
    // the record and lists the name twice.
    void register_type( const char* name, std::uint32_t create, std::uint32_t bake, std::uint32_t version, bool flag )
    {
        m_type_names.emplace_back( name );
        record_t& r = m_types[ name ];
        r.create = create;
        r.bake = bake;
        r.version = version;
        r.flag = flag;
    }

    // VuAssetFactory_registerAllTypes 0x56B730 (called by init slot 5 0x4ED410).
    void register_all_types( )
    {
        for ( const asset_type_t& t : g_asset_types )
            register_type( t.name, t.create, t.bake, t.version, t.flag );
    }

    // getAssetTypes, slot 11 0x4EDF30 (= +0x34).
    const std::vector< std::string >& types( ) const
    {
        return m_type_names;
    }

    const record_t* record( const std::string& type ) const
    {
        auto it = m_types.find( type );
        return it == m_types.end( ) ? nullptr : &it->second;
    }

    // findAsset( hash ), slot 22 0x4EF4D0: the +0x54 table's value, else 0.
    vu_asset_t* find( std::uint32_t hash ) const
    {
        auto it = m_loaded.find( hash );
        return it == m_loaded.end( ) ? nullptr : it->second;
    }

    // findAsset( type, name ), slot 23 0x4EF4B0 (char* type, char* name): slot 22 of the key hash.
    vu_asset_t* find( const char* type, const char* name ) const
    {
        return find( asset_key_hash( type, name ) );
    }

    // addRef, slot 8 0x4EDE60: +0x4C += 1, answers the asset.
    vu_asset_t* add_ref( vu_asset_t* asset )
    {
        ++asset->refs;
        return asset;
    }

    // release, slot 9 0x4EDE70. Null does nothing. Else the frame sync (hook), then +0x4C -= 1; at 0 the table entry
    // of the asset's OWN +0x48 hash is erased when there is one, and the asset is deleted (vt+4(1)) either way.
    void release( vu_asset_t* asset )
    {
        if ( asset == nullptr )
            return;
        if ( hooks.sync != nullptr )
            hooks.sync( hooks.user );
        if ( --asset->refs != 0 )
            return;
        auto it = m_loaded.find( asset->hash );
        if ( it != m_loaded.end( ) )
            m_loaded.erase( it );
        if ( hooks.destroy != nullptr )
            hooks.destroy( hooks.user, asset );
    }

    // 0x4F1C90: the first +0x9C record whose hash is the key hash copies its substitute name out.
    bool substitute( const std::string& type, const std::string& name, std::string& out ) const
    {
        const std::uint32_t h = asset_key_hash( type.c_str( ), name.c_str( ) );
        for ( const auto& s : m_substitutes )
        {
            if ( s.first == h )
            {
                out = s.second;
                return true;
            }
        }
        return false;
    }

    // addSubstitute, slot 42 0x4EF8E0: removeSubstitute( type, name ) (slot 43, through the vtable), then
    // push_back { key hash, substitute }.
    void add_substitute( const std::string& type, const std::string& name, const std::string& with )
    {
        remove_substitute( type, name );
        m_substitutes.emplace_back( asset_key_hash( type.c_str( ), name.c_str( ) ), with );
    }

    // removeSubstitute, slot 43 0x4EF9A0: the first record of the key hash, erased (the tail shifted down).
    void remove_substitute( const std::string& type, const std::string& name )
    {
        const std::uint32_t h = asset_key_hash( type.c_str( ), name.c_str( ) );
        for ( auto it = m_substitutes.begin( ); it != m_substitutes.end( ); ++it )
        {
            if ( it->first == h )
            {
                m_substitutes.erase( it );
                return;
            }
        }
    }

    const std::vector< std::pair< std::uint32_t, std::string > >& substitutes( ) const
    {
        return m_substitutes;
    }

    // createAsset, slot 7 0x4EDA20. A substitute -> createAsset( type, substitute ) (its answer, nothing else). Else
    // the key hash through slot 22: a loaded asset gets +0x4C += 1 and is answered. Else the miss path: the first
    // package (+0x48 order) listing type / name (none -> 0) has the entry's used byte set; the type's record (none,
    // or its create null -> 0, the byte stays set); the frame sync 0x53A400; the sku pick 0x4F1CF0( "Metro", +4 sku,
    // entry ); create; +8 = the name, +0x48 = the hash; the language pick 0x55FC50( info, +0x1C language ); the load
    // 0x4F08F0 (its answer ignored: a failed load is cached too); each LATER package listing it patches it (0x4F1B50;
    // their used bytes are not set); the table insert (operator[] 0x4F3850 of the hash); the listeners.
    vu_asset_t* create( const std::string& type, const std::string& name )
    {
        std::string with;
        if ( substitute( type, name, with ) )
            return create( type, with );
        const std::uint32_t h = asset_key_hash( type.c_str( ), name.c_str( ) );
        if ( vu_asset_t* hit = find( h ) )
        {
            ++hit->refs;
            return hit;
        }
        for ( std::size_t k = 0; k < m_packages.size( ); ++k )
        {
            asset_entry_t* e = m_packages[ k ]->entry( type, name );
            if ( e == nullptr )
                continue;
            e->used = true;
            const record_t* r = record( type );
            if ( r == nullptr || r->create == 0 )
                return nullptr;
            if ( hooks.sync != nullptr )
                hooks.sync( hooks.user );
            const value_t& info = asset_pick_sku( "Metro", m_sku, e->data );
            if ( hooks.create == nullptr )
            {
                unknown = true;
                return nullptr;
            }
            vu_asset_t* asset = hooks.create( hooks.user, type );
            if ( asset == nullptr )
                return nullptr;
            asset->type = type;
            asset->name = name;
            asset->hash = h;
            asset_pick_language( asset->info, asset->lang, info, m_lang );
            load_from( *r, *m_packages[ k ], *asset );
            for ( std::size_t j = k + 1; j < m_packages.size( ); ++j )
            {
                if ( asset_entry_t* later = m_packages[ j ]->entry( type, name ) )
                    patch_from( *r, *m_packages[ j ], *later, *asset );
            }
            m_loaded[ h ] = asset;
            if ( hooks.created != nullptr )
                hooks.created( hooks.user, *asset );
            for ( const asset_listener_t* l : m_listeners )
            {
                if ( l->created != nullptr )
                    l->created( l->user, *asset );
            }
            return asset;
        }
        return nullptr;
    }

    // +4 the sku (0x4ED410: the init argument, then the "Base" package's +0x18), +0x1C the language (0x4ED410: the
    // platform's g_vumetrosys vt+0x48, or VuDevConfig "Language").
    void set_sku( const std::string& sku )
    {
        m_sku = sku;
    }

    void set_language( const std::string& language )
    {
        m_lang = language;
    }

    const std::string& sku( ) const
    {
        return m_sku;
    }

    // 0x4F0470: the loaded package of the name, or null.
    asset_package_t* package( const std::string& name ) const
    {
        for ( const auto& p : m_packages )
        {
            if ( p->name == name )
                return p.get( );
        }
        return nullptr;
    }

    std::size_t package_count( ) const
    {
        return m_packages.size( );
    }

    const asset_package_t& package_at( std::size_t index ) const
    {
        return *m_packages[ index ];
    }

    const std::map< std::string, record_t >& records( ) const
    {
        return m_types;
    }

    // loadPackage 0x4EFA50. Already loaded (0x4F0470) -> true. (VuDevConfig "IgnoreAssetDBs": the dev config is the
    // null container in retail - batch 24 - so that early true is never taken.) <name>.apf through the platform file
    // system -> 0x4F0090; none -> the loose Assets loader 0x4EFBB0, NOT recreated (unknown, false). A load that
    // succeeds is announced to the +0x88 listeners (vt+0, the name).
    bool load_package( const std::string& name )
    {
        if ( package( name ) != nullptr )
            return true;
        const std::string path = hooks.pack_path != nullptr ? hooks.pack_path( hooks.user, name + ".apf" ) : std::string( );
        bool ok = false;
        if ( path.empty( ) )
            unknown = true;
        else
            ok = load_package_file( name, path );
        if ( ok && hooks.package_loaded != nullptr )
            hooks.package_loaded( hooks.user, name );
        if ( ok )
        {
            for ( const asset_listener_t* l : m_listeners )
            {
                if ( l->package_loaded != nullptr )
                    l->package_loaded( l->user, name );
            }
        }
        return ok;
    }

    // 0x4ED410's package part: loadPackage( "Base" ) (false -> false), then +4 = that package's sku.
    bool load_base_package( )
    {
        if ( !load_package( "Base" ) )
            return false;
        m_sku = package( "Base" )->sku;
        return true;
    }

    // Put an asset in the table as createAsset's miss path does (the check seeds the table this way).
    void insert( vu_asset_t* asset )
    {
        m_loaded[ asset->hash ] = asset;
    }

    // ---- The remaining vtable slots (boat batch 30, oracle case asset_slots). Slot 20 0x4EF270 is NOT here: past
    // 0x4F0550 and the bake check (vt+0x44 = slot 17) it is 0x4F0EF0, the raw-file path never taken with Base.apf. ----

    // Slot 5 0x4ED410, init( callback, sku ): +4 = sku; the language +0x1C (dev "Language": the dev config 0x75901C is
    // null in retail); GameInfo.json when it exists: +0x74 set, the document in +0x78 (a failed parse answers false);
    // the 22 types, the callback, loadPackage( "Base" ) (false -> false), +4 = its sku; the +0x90 map. [0x759018] set
    // would make a slot 35 ( 0 ) call whose answer is dropped: no effect. +0x99 (g_vumetrofile vt+0x24's +0x10 != 0) is
    // the platform's and is left out.
    bool init( void ( *callback )( void* ), void* callback_user, const std::string& sku )
    {
        m_sku = sku;
        if ( hooks.language != nullptr )
            m_lang = hooks.language( hooks.user );
        bool parsed = false;
        if ( hooks.game_info != nullptr && hooks.game_info( hooks.user, m_game_info, parsed ) )
        {
            m_has_game_info = true;
            if ( !parsed )
                return false;
        }
        register_all_types( );
        if ( callback != nullptr )
            callback( callback_user );
        if ( !load_base_package( ) )
            return false;
        m_categories[ "VuAnimatedModelAsset" ] = 1;
        m_categories[ "VuStaticModelAsset" ] = 1;
        m_categories[ "VuPfxAsset" ] = 2;
        m_categories[ "VuAudioEventAsset" ] = 3;
        return true;
    }

    // 0x4F0550 ( type, name, out package ): the first package (+0x48 order) listing it: its entry, else null.
    asset_entry_t* find_listing( const std::string& type, const std::string& name ) const
    {
        for ( const auto& p : m_packages )
        {
            if ( asset_entry_t* e = p->entry( type, name ) )
                return e;
        }
        return nullptr;
    }

    // Slot 10 0x4EDF10: 0x4F0550 != 0.
    bool exists( const std::string& type, const std::string& name ) const
    {
        return find_listing( type, name ) != nullptr;
    }

    // Slot 12 0x4EDF40: the record's +0 name vector; no record: the function-local empty vector 0x819924.
    const std::vector< std::string >& names( const std::string& type ) const
    {
        static const std::vector< std::string > none;
        const record_t* r = record( type );
        return r != nullptr ? r->names : none;
    }

    // Slots 13 / 14 share this tail: an object entry holding `key` answers that member, ANY OTHER entry answers the
    // entry itself (a missing key gives the whole entry, not null).
    static const value_t& info_of( const value_t& entry, const std::string& key )
    {
        if ( entry.tag == 6 )
        {
            for ( const auto& field : entry.object )
            {
                if ( field.first == key )
                    return field.second;
            }
        }
        return entry;
    }

    // The null container 0x7593A0.
    static const value_t& null_container( )
    {
        static const value_t null{};
        return null;
    }

    // Slot 13 0x4EE030 ( key, type, name ): the first package listing it; none -> the null container.
    const value_t& info( const std::string& key, const std::string& type, const std::string& name ) const
    {
        const asset_entry_t* e = find_listing( type, name );
        return e != nullptr ? info_of( e->data, key ) : null_container( );
    }

    // Slot 14 0x4EDFC0 ( key, package, type, name ): that package only.
    const value_t& info_in( const std::string& key, const std::string& package_name, const std::string& type, const std::string& name ) const
    {
        asset_package_t* p = package( package_name );
        const asset_entry_t* e = p != nullptr ? p->entry( type, name ) : nullptr;
        return e != nullptr ? info_of( e->data, key ) : null_container( );
    }

    // Slot 15 0x4EE110: the key hash's table entry erased (0x4F2BA0 find + 0x4F25A0 erase); the asset is NOT deleted.
    void forget( const std::string& type, const std::string& name )
    {
        m_loaded.erase( asset_key_hash( type.c_str( ), name.c_str( ) ) );
    }

    // Slot 16 0x4EE170, reset: every package deleted (0x4ED930 + delete), every record's name vector emptied, +0x48
    // emptied, the +0x54 table emptied (buckets back to mask 7 / maxidx 8; the assets are NOT deleted); then
    // loadPackage( "Base" ) (false -> false, the sku is not re-read) and slot 26's answer. The substitutes stay.
    bool reset( )
    {
        m_packages.clear( );
        for ( auto& [ type, r ] : m_types )
            r.names.clear( );
        m_loaded.clear( );
        if ( !load_package( "Base" ) )
            return false;
        return load_installed( );
    }

    // Slot 24 0x4EF510: the used byte of the first package listing it; none -> false.
    bool used( const std::string& type, const std::string& name ) const
    {
        const asset_entry_t* e = find_listing( type, name );
        return e != nullptr && e->used;
    }

    // Slot 25 0x4EF590: every loaded asset pushed on the VuArray. SUBSTITUTION: retail walks the _Hash's list, whose
    // order std::unordered_map does not model; the set is the same, the order is ours (the head comment).
    void loaded( std::vector< vu_asset_t* >& out ) const
    {
        for ( const auto& [ h, a ] : m_loaded )
            out.push_back( a );
    }

    // Slot 26 0x4EF610, the installed packages: nothing without GameInfo.json (+0x74), or when the dev config is set
    // and its "IgnoreInstalledPackages" is not a true bool (the dev config is null in retail: past that test). Each
    // "Packages" member by index (0x4FC000, key order, 0 members when not an object): loadPackage (false -> false),
    // then its "LicenseMask" OR-ed into the package's +0x38: int 1 / int64 7 as the low word, float 2 through
    // cvttss2si, anything else 0.
    bool load_installed( )
    {
        if ( !m_has_game_info )
            return true;
        for ( const auto* k : asset_sorted_members( game_packages( ) ) )
        {
            if ( !load_package( k->first ) )
                return false;
            asset_package_t* p = package( k->first );
            if ( p == nullptr )
                continue;
            const value_t& m = member( k->second, "LicenseMask" );
            std::uint32_t bits = 0;
            if ( m.tag == 1 )
                bits = static_cast< std::uint32_t >( m.integer );
            else if ( m.tag == 2 )
                bits = static_cast< std::uint32_t >( _mm_cvtt_ss2si( _mm_set_ss( m.real ) ) );
            else if ( m.tag == 7 )
                bits = static_cast< std::uint32_t >( m.int64 );
            p->words[ 2 ] |= bits;
        }
        return true;
    }

    // Slot 28 0x4ECEC0 / slot 29 0x4ECF30 (asset_listener_t).
    void add_listener( const asset_listener_t* listener )
    {
        m_listeners.push_back( listener );
    }

    void remove_listener( const asset_listener_t* listener )
    {
        m_listeners.erase( std::remove( m_listeners.begin( ), m_listeners.end( ), listener ), m_listeners.end( ) );
    }

    // Slot 30 0x4EF760: +4 and every package's +0x18.
    void set_sku_all( const std::string& sku )
    {
        m_sku = sku;
        for ( const auto& p : m_packages )
            p->sku = sku;
    }

    // Slot 36 0x4EF7C0: the named package's +0x38, 0 when not loaded.
    std::uint32_t license_mask( const std::string& name ) const
    {
        const asset_package_t* p = package( name );
        return p != nullptr ? p->words[ 2 ] : 0u;
    }

    // Slot 37 0x4EF7E0: while more than one package: the last deleted. Then every loaded asset's vt+0xC, then every
    // listener's vt+8. The records' name vectors and the +0x54 table keep what those packages added.
    void unload_packages( )
    {
        while ( m_packages.size( ) > 1 )
            m_packages.pop_back( );
        if ( hooks.reload != nullptr )
        {
            for ( const auto& [ h, a ] : m_loaded )
                hooks.reload( hooks.user, *a );
        }
        for ( const asset_listener_t* l : m_listeners )
        {
            if ( l->unloaded != nullptr )
                l->unloaded( l->user );
        }
    }

    // Slot 38 0x4EF880: the +0x90 map's value, 0 when the name is not there.
    int category( const std::string& type ) const
    {
        auto it = m_categories.find( type );
        return it == m_categories.end( ) ? 0 : it->second;
    }

    // Slot 39 0x4EF8B0: does the FIRST package list it (0x4F20A0); false with no package.
    bool base_lists( const std::string& type, const std::string& name ) const
    {
        return !m_packages.empty( ) && m_packages[ 0 ]->entry( type, name ) != nullptr;
    }

    // Slot 40 0x4ECFB0 / slot 41 0x4ECFC0: GameInfo.json's "Packages" / "Skus".
    const value_t& game_packages( ) const
    {
        return member( m_game_info, "Packages" );
    }

    const value_t& game_skus( ) const
    {
        return member( m_game_info, "Skus" );
    }

private:
    // 0x4F0090 ( name ): a new package (0x4ECC90: words -1, -1, 0); its pack opened (0x567760; false -> the package
    // deleted, false); +0x18 = the pack's sku, +0x30 / +0x34 = its two words; Assets/AssetData read (0x5679F0): 1 ->
    // the document (0x55B560 on the bytes as read; a failed read -> false) and per root member, in key order, 0x4F1DC0
    // (false -> false); 2 -> false; 0 (not listed) -> the package stays, empty. Then, when assets are loaded (+0x58,
    // the table's size), 0x4F05D0 patches them from the new package; the package goes on +0x48; 0x4F0710 adds its
    // names to the records.
    bool load_package_file( const std::string& name, const std::string& path )
    {
        auto p = std::make_unique< asset_package_t >( );
        p->name = name;
        if ( !p->pack.open_pack( path.c_str( ), hooks.pack_header ) )
        {
            unknown = unknown || p->pack.pack_unknown( );
            return false;
        }
        p->sku = p->pack.pack_sku( );
        p->words[ 0 ] = p->pack.pack_word( 0 );
        p->words[ 1 ] = p->pack.pack_word( 1 );
        std::vector< std::uint8_t > blob;
        std::uint32_t version = 0, hash = 0;
        const int read = p->pack.read_asset( "Assets", "AssetData", "", version, hash, blob );
        unknown = unknown || p->pack.pack_unknown( );
        if ( read == 2 )
            return false;
        if ( read == 1 )
        {
            value_t root;
            if ( !c_reader::read_at_start( blob, root ) )
                return false;
            for ( const auto* member : asset_sorted_members( root ) )
            {
                if ( !asset_package_load_type( *p, member->first, member->second ) )
                    return false;
            }
        }
        if ( !m_loaded.empty( ) )
            patch_loaded( *p );
        m_packages.push_back( std::move( p ) );
        add_names( *m_packages.back( ) );
        return true;
    }

    // 0x4F0710: for each registered type the package lists, its names pushed onto the record's +0 vector, then
    // sorted and made unique (0x4F38D0 introsort, 0x4F3A00 unique, the tail erased). SUBSTITUTION: std::sort /
    // std::unique - the same result (distinct strings, total order; unique keeps one of each).
    void add_names( const asset_package_t& p )
    {
        for ( auto& [ type, r ] : m_types )
        {
            auto t = p.assets.find( type );
            if ( t == p.assets.end( ) )
                continue;
            for ( const auto& n : t->second )
                r.names.push_back( n.first );
            std::sort( r.names.begin( ), r.names.end( ) );
            r.names.erase( std::unique( r.names.begin( ), r.names.end( ) ), r.names.end( ) );
        }
    }

    // 0x4F05D0: each type / name of the new package that is loaded (slot 23) is patched from it (0x4F1B50).
    void patch_loaded( asset_package_t& p )
    {
        for ( auto& [ type, names ] : p.assets )
        {
            const record_t* r = record( type );
            for ( auto& [ n, e ] : names )
            {
                vu_asset_t* asset = find( type.c_str( ), n.c_str( ) );
                if ( asset == nullptr )
                    continue;
                if ( r == nullptr )
                {
                    // 0x4F1B50 would call through a null record: not modelled.
                    unknown = true;
                    continue;
                }
                patch_from( *r, p, e, *asset );
            }
        }
    }

    // 0x4F1B50 ( record, package, entry, asset ): a second asset from the record's create, with the asset's name and
    // +0x48 hash, the sku pick ( "Metro", +4, entry ) and the language pick ( +0x1C ), loaded from this package
    // (0x4F08F0); a failed load deletes it, false. Else the asset's vt+8 patch( it ), then it is deleted either way;
    // the patch's answer.
    bool patch_from( const record_t& r, asset_package_t& p, asset_entry_t& e, vu_asset_t& asset )
    {
        if ( r.create == 0 || hooks.create == nullptr )
        {
            unknown = true;
            return false;
        }
        const value_t& info = asset_pick_sku( "Metro", m_sku, e.data );
        vu_asset_t* from = hooks.create( hooks.user, asset.type );
        if ( from == nullptr )
            return false;
        from->type = asset.type;
        from->name = asset.name;
        from->hash = asset.hash;
        asset_pick_language( from->info, from->lang, info, m_lang );
        bool ok = load_from( r, p, *from );
        if ( ok )
            ok = hooks.patch != nullptr && hooks.patch( hooks.user, asset, *from );
        if ( hooks.destroy != nullptr )
            hooks.destroy( hooks.user, from );
        return ok;
    }

    // 0x4F08F0 ( record, package, asset ): the pack read of "<type>/<name><lang>" (0x5679F0). 1: the record's version
    // must equal the pack record's, then the asset's vt+0x14 load( data ); true -> +0x50 = the pack hash, true.
    // Otherwise false. 2 -> false. 0 (the pack does not list it): the raw-file / bake path, NOT recreated.
    bool load_from( const record_t& r, asset_package_t& p, vu_asset_t& asset )
    {
        std::vector< std::uint8_t > data;
        std::uint32_t version = 0, hash = 0;
        const int read = p.pack.read_asset( asset.type, asset.name, asset.lang, version, hash, data );
        unknown = unknown || p.pack.pack_unknown( );
        if ( read == 0 )
        {
            unknown = true;
            return false;
        }
        if ( read != 1 || version != r.version )
            return false;
        if ( hooks.load_data == nullptr || !hooks.load_data( hooks.user, asset, data ) )
            return false;
        asset.data_hash = hash;
        return true;
    }

    std::string m_sku;
    std::string m_lang;
    std::vector< std::unique_ptr< asset_package_t > > m_packages;
    std::vector< std::string > m_type_names;
    std::map< std::string, record_t > m_types;
    std::unordered_map< std::uint32_t, vu_asset_t* > m_loaded;
    std::vector< std::pair< std::uint32_t, std::string > > m_substitutes;
    std::vector< const asset_listener_t* > m_listeners;
    std::map< std::string, int > m_categories;
    bool m_has_game_info = false;
    value_t m_game_info;
};

// ---- The asset rows' scalar-deleting dtors on retail's words (boat batch 26) ----
//
// Each is thiscall( object, flags ), retn 4, eax = this: the body, then operator delete( this ) when flags & 1. The
// objects are retail images (asset_words_t: the caller's 32-bit memory); what the bodies call outside is a hook, in
// retail's order (asset_dtor_hooks_t). A std::string member is reset as MSVC's dtor does: the heap text freed when
// the capacity +0x14 is >= 16, then capacity 15, size +0x10 = 0, the first byte 0.
//   0x515D70 VuAsset (body 0x515D00): +0 = VuAsset's vtable 0x74CA08, the +0x38 VuJsonContainer cleared (0x4FBB10), the
//            strings +0x20 then +0x8. Every asset class's dtor ends in this body (21 callers).
//   0x53FD10 VuBaseAssetProperty (body 0x53FD40): +0 = 0x74D6CC; a held asset (+0x64 non-null) released through
//            g_vuassetfactoryimpl's slot 9 (0x4EDE70, c_asset_factory::release); the string +0x4C; then the property
//            base's dtor 0x405260 (the entity framework's: a hook). +0x64 itself is not cleared.
//   0x5F4AB0 VuSubstituteAssetEntity (body 0x5F4AE0): the strings +0x90, +0x78, +0x60, then a tail jump to VuEntity's
//            dtor 0x500870 (a hook). No vtable store of its own.
struct asset_words_t
{
    std::function< std::uint32_t( std::uint32_t ) > get;
    std::function< void( std::uint32_t, std::uint32_t ) > put;
};

struct asset_dtor_hooks_t
{
    std::function< void( std::uint32_t ) > free_block;        // operator delete (0x64C150's import)
    std::function< void( std::uint32_t ) > json_clear;        // 0x4FBB10 ( container )
    std::function< void( std::uint32_t ) > factory_release;   // [0x759004] slot 9 0x4EDE70 ( asset )
    std::function< void( ) > property_base_dtor;              // 0x405260 ( this )
    std::function< void( ) > entity_dtor;                     // 0x500870 ( this )
    std::function< void( std::uint32_t, std::uint32_t ) > object_delete;   // an object's slot 0 ( object, flags )
    std::function< void( std::uint32_t ) > free_array;        // operator delete[] (0x64C148's import)
    std::function< void( std::uint32_t ) > xml_node_dtor;     // TiXmlNode's dtor 0x5AA280 ( node )
    std::function< void( std::uint32_t ) > aligned_free;      // _aligned_free (0x64C15C's import)
    std::function< void( std::uint32_t ) > bt_free;           // the btAlignedFree pointer [0x7358AC] ( block )
};

enum class asset_dtor_kind_t
{
    asset,               // 0x515D70
    asset_property,      // 0x53FD10
    substitute_entity,   // 0x5F4AB0
    animation,           // 0x515E10
    project,             // 0x554E90
    generic_data,        // 0x55C1E0
    db,                  // 0x55C230
    timed_event,         // 0x5C8C70
    xml,                 // 0x5C78F0
    collision_mesh,      // 0x518D60
    fluids_mesh,         // 0x5226D0
};

inline void asset_string_reset( const asset_words_t& w, std::uint32_t at, const asset_dtor_hooks_t& h )
{
    if ( w.get( at + 0x14 ) >= 0x10u && h.free_block )
        h.free_block( w.get( at ) );
    w.put( at + 0x14, 0xFu );
    w.put( at + 0x10, 0u );
    w.put( at, w.get( at ) & 0xFFFFFF00u );
}

// 0x515D00: VuAsset's dtor body.
inline void asset_base_dtor( const asset_words_t& w, std::uint32_t self, const asset_dtor_hooks_t& h )
{
    w.put( self, 0x74CA08u );
    if ( h.json_clear )
        h.json_clear( self + 0x38 );
    asset_string_reset( w, self + 0x20, h );
    asset_string_reset( w, self + 0x8, h );
}

// 0x53FD40: VuBaseAssetProperty's dtor body.
inline void asset_property_dtor( const asset_words_t& w, std::uint32_t self, const asset_dtor_hooks_t& h )
{
    w.put( self, 0x74D6CCu );
    if ( const std::uint32_t held = w.get( self + 0x64 ) )
        if ( h.factory_release )
            h.factory_release( held );
    asset_string_reset( w, self + 0x4C, h );
    if ( h.property_base_dtor )
        h.property_base_dtor( );
}

// 0x5F4AE0: VuSubstituteAssetEntity's dtor body.
inline void substitute_entity_dtor( const asset_words_t& w, std::uint32_t self, const asset_dtor_hooks_t& h )
{
    for ( std::uint32_t at : { 0x90u, 0x78u, 0x60u } )
        asset_string_reset( w, self + at, h );
    if ( h.entity_dtor )
        h.entity_dtor( );
}

// The asset classes' own dtors (slot 1; each then VuAsset's body 0x515D00):
//   0x515E10 VuAnimationAsset: +0 = 0x74C9E8; a +0x58 VuAnimation has its refcount +4 decremented - at 0 its slot 0
//            ( 1 ) deletes it - and +0x58 = 0 (whatever the count; nothing when +0x58 is null).
//   0x554E90 VuProjectAsset: +0 = 0x74EB64, the +0x58 container cleared.
//   0x55C1E0 VuGenericDataAsset (also VuAssetListAsset / VuStringAsset / VuTemplateAsset / VuShaderAsset's slot 1):
//            +0 = 0x74EDCC, the +0x58 document cleared twice (the member's clear, then its dtor's).
//   0x55C230 VuDBAsset: +0 = 0x74EDEC, +0x68 cleared, then the generic data part (+0 = 0x74EDCC, +0x58 twice).
//   0x5C8C70 VuTimedEventAsset: +0 = 0x7524D8; with +0x58 non-null its events [+0x58, +0x5C) - 0x30 bytes each -
//            destroyed by 0x5C9D20 (per event the +0x20 container cleared, then the +4 string reset), the block freed
//            and +0x58 / +0x5C / +0x60 = 0.
//   0x5C78F0 VuXmlAsset (body 0x5C7850): +0 = 0x752458; the embedded TiXmlDocument's (+0x58) children - first +0x70,
//            next each node's +0x28, read before the delete - deleted through their slot 0 ( 1 ); +0x70 / +0x74 = 0;
//            +0x58 = 0x750F2C; the +0x8C TiXmlString's rep freed (operator delete[]) unless it is the null rep 0x759104
//            (+0x8C is left as it was); TiXmlNode's dtor 0x5AA280 on +0x58 (TinyXML: a hook).
//   0x518D60 VuCollisionMeshAsset: +0 = 0x74CC3C; its unload 0x51B140 (collision_mesh_unload_words - the word twin of
//            boat meshes.hxx's); _aligned_free of +0x7C, +0x70, +0x64, +0x58 (the pointers are not cleared).
//   0x5226D0 VuFluidsMeshAsset: +0 = 0x74CDDC; _aligned_free of +0x90, +0x84, +0x78.
inline void asset_events_destroy( const asset_words_t& w, std::uint32_t first, std::uint32_t last, const asset_dtor_hooks_t& h )
{
    for ( std::uint32_t e = first; e != last; e += 0x30 )   // 0x5C9D20 (fastcall first, last)
    {
        if ( h.json_clear )
            h.json_clear( e + 0x20 );
        asset_string_reset( w, e + 4, h );
    }
}

// 0x51B140: VuCollisionMeshAsset's unload (slot 6) - the four array counts +0x5C / +0x68 / +0x74 / +0x80 = 0; a +0xA8
// VuOptimizedBvh deleted through its slot 0 ( 0 ), then +0xA8 READ AGAIN and, when still set, gNumAlignedFree 0x759050
// += 1 and the btAlignedFree pointer [0x7358AC] called on it; +0xA8 = 0 (only when it was set at first).
inline void collision_mesh_unload_words( const asset_words_t& w, std::uint32_t self, const asset_dtor_hooks_t& h )
{
    for ( std::uint32_t at : { 0x5Cu, 0x68u, 0x74u, 0x80u } )
        w.put( self + at, 0u );
    if ( const std::uint32_t bvh = w.get( self + 0xA8 ) )
    {
        if ( h.object_delete )
            h.object_delete( bvh, 0u );
        if ( const std::uint32_t again = w.get( self + 0xA8 ) )
        {
            w.put( 0x759050u, w.get( 0x759050u ) + 1u );
            if ( h.bt_free )
                h.bt_free( again );
        }
        w.put( self + 0xA8, 0u );
    }
}

inline void asset_class_dtor( asset_dtor_kind_t kind, const asset_words_t& w, std::uint32_t self, const asset_dtor_hooks_t& h )
{
    auto clear = [ & ]( std::uint32_t at ) {
        if ( h.json_clear )
            h.json_clear( self + at );
    };
    switch ( kind )
    {
    case asset_dtor_kind_t::animation:
        w.put( self, 0x74C9E8u );
        if ( const std::uint32_t a = w.get( self + 0x58 ) )
        {
            const std::uint32_t refs = w.get( a + 4 ) - 1u;
            w.put( a + 4, refs );
            if ( refs == 0 && h.object_delete )
                h.object_delete( a, 1u );
            w.put( self + 0x58, 0u );
        }
        break;
    case asset_dtor_kind_t::project:
        w.put( self, 0x74EB64u );
        clear( 0x58 );
        break;
    case asset_dtor_kind_t::db:
        w.put( self, 0x74EDECu );
        clear( 0x68 );
        [[fallthrough]];
    case asset_dtor_kind_t::generic_data:
        w.put( self, 0x74EDCCu );
        clear( 0x58 );
        clear( 0x58 );
        break;
    case asset_dtor_kind_t::timed_event:
        w.put( self, 0x7524D8u );
        if ( const std::uint32_t first = w.get( self + 0x58 ) )
        {
            asset_events_destroy( w, first, w.get( self + 0x5C ), h );
            if ( h.free_block )
                h.free_block( w.get( self + 0x58 ) );
            for ( std::uint32_t at : { 0x58u, 0x5Cu, 0x60u } )
                w.put( self + at, 0u );
        }
        break;
    case asset_dtor_kind_t::collision_mesh:
        w.put( self, 0x74CC3Cu );
        collision_mesh_unload_words( w, self, h );
        for ( std::uint32_t at : { 0x7Cu, 0x70u, 0x64u, 0x58u } )
            if ( h.aligned_free )
                h.aligned_free( w.get( self + at ) );
        break;
    case asset_dtor_kind_t::fluids_mesh:
        w.put( self, 0x74CDDCu );
        for ( std::uint32_t at : { 0x90u, 0x84u, 0x78u } )
            if ( h.aligned_free )
                h.aligned_free( w.get( self + at ) );
        break;
    case asset_dtor_kind_t::xml:
        w.put( self, 0x752458u );
        for ( std::uint32_t n = w.get( self + 0x70 ); n != 0; )
        {
            const std::uint32_t next = w.get( n + 0x28 );
            if ( h.object_delete )
                h.object_delete( n, 1u );
            n = next;
        }
        w.put( self + 0x70, 0u );
        w.put( self + 0x74, 0u );
        w.put( self + 0x58, 0x750F2Cu );
        if ( w.get( self + 0x8C ) != 0x759104u && h.free_array )
            h.free_array( w.get( self + 0x8C ) );
        if ( h.xml_node_dtor )
            h.xml_node_dtor( self + 0x58 );
        break;
    default:
        break;
    }
    asset_base_dtor( w, self, h );
}

// 0x515D70 / 0x53FD10 / 0x5F4AB0 / 0x515E10 / 0x554E90 / 0x55C1E0 / 0x55C230 / 0x5C8C70 / 0x5C78F0 / 0x518D60 / 0x5226D0: the
// scalar-deleting dtors.
inline std::uint32_t asset_destroy( asset_dtor_kind_t kind, const asset_words_t& w, std::uint32_t self, std::uint32_t flags,
                                    const asset_dtor_hooks_t& h )
{
    if ( kind == asset_dtor_kind_t::asset )
        asset_base_dtor( w, self, h );
    else if ( kind == asset_dtor_kind_t::asset_property )
        asset_property_dtor( w, self, h );
    else if ( kind == asset_dtor_kind_t::substitute_entity )
        substitute_entity_dtor( w, self, h );
    else
        asset_class_dtor( kind, w, self, h );
    if ( ( flags & 1u ) && h.free_block )
        h.free_block( self );
    return self;
}
// ---- The asset types' create functions on retail's words (boat batch 26) ----
//
// The record's create (+0xC of a registered type, cdecl, no arguments): operator new( size ) (the caller's `alloc`;
// 0 answers 0), then the class's ctor on the block, eax = the block. Every ctor inlines VuAsset's: the strings +0x8 /
// +0x20 empty (capacity 15, size 0, first byte 0), the +0x38 VuJsonContainer null (+0x38 / +0x40 / +0x44 = 0), +0x48 =
// 0, refs +0x4C = 1, +0x50 = 0; then its own vtable at +0 and members.
//   0x515E90 VuAnimationAsset  new 0x60; +0x58 = 0; ctor 0x515DB0: base, vtable 0x74C9E8, +0x58 = 0.
//   0x554ED0 VuProjectAsset    new 0x70; ctor 0x554E10: base, vtable 0x74EB64, +0x58 / +0x60 / +0x64 / +0x68 = 0.
//   0x55C280 VuDBAsset         new 0x78; VuGenericDataAsset's ctor 0x55C2C0 (base, vtable 0x74EDCC, +0x58 / +0x60 /
//                              +0x64 = 0 - its document), then vtable 0x74EDEC, +0x68 / +0x70 / +0x74 = 0.
//   0x5644E0 VuAssetListAsset / 0x5A9800 VuStringAsset / 0x5BB0E0 VuTemplateAsset: new 0x68; 0x55C2C0; vtable 0x74F194 /
//                              0x750D38 / 0x751DF0.
//   0x518EC0 VuCollisionMeshAsset new 0xB0; +0x58 / +0x64 / +0x70 / +0x7C / +0xA8 = 0; ctor 0x518F70: base, vtable
//                              0x74CC3C, +0x58..+0x84 = 0, the aabb +0x88 = ( FLT_MAX x3, ? ) and +0x98 = ( -FLT_MAX x3, ? ),
//                              +0xA8 = 0, the byte +0xAC = 0. "?": the ctor writes each aabb vector as two 8-byte copies of
//                              a stack local whose fourth word it never writes ([ebp - 0x10]), so +0x94 / +0xA4 get
//                              whatever the stack held there (asset_create_hooks_t::stack_word - the caller's answer).
//   0x5C7920 VuXmlAsset        new 0xA0; +0x64 / +0x68 / +0x70..+0x80 / +0x8C = 0; ctor 0x5C79D0: base, vtable 0x752458,
//                              TiXmlDocument's ctor 0x5AAB90 on +0x58 (TinyXML: a hook).
//   0x5C8CD0 VuTimedEventAsset new 0x68; +0x58 / +0x5C / +0x60 = 0; ctor 0x5C8BB0: base, vtable 0x7524D8, the event vector
//                              +0x58..+0x60 = 0, then 0x5C9810 resize( 0 ) - on the empty vector its size 0 equals 0:
//                              nothing (the shrink / grow branches 0x5C98D0 / 0x5C9960 cannot run from here).
// The other registered types' creates are not read.
enum class asset_create_kind_t
{
    animation,        // 0x515E90
    project,          // 0x554ED0
    db,               // 0x55C280
    asset_list,       // 0x5644E0
    string,           // 0x5A9800
    templ,            // 0x5BB0E0
    collision_mesh,   // 0x518EC0
    xml,              // 0x5C7920
    timed_event,      // 0x5C8CD0
};

struct asset_create_hooks_t
{
    std::function< std::uint32_t( std::uint32_t ) > alloc;           // operator new ( size )
    std::function< void( std::uint32_t ) > xml_document_ctor;        // 0x5AAB90 ( document )
    std::function< std::uint32_t( ) > stack_word;                    // 0x518F70's unwritten [ebp - 0x10]
};

inline void asset_byte_zero( const asset_words_t& w, std::uint32_t at )
{
    w.put( at, w.get( at ) & 0xFFFFFF00u );
}

// VuAsset's ctor, inlined in every asset class's.
inline void asset_base_init( const asset_words_t& w, std::uint32_t p )
{
    w.put( p + 0x1C, 0xFu );
    w.put( p + 0x18, 0u );
    asset_byte_zero( w, p + 0x8 );
    w.put( p + 0x34, 0xFu );
    w.put( p + 0x30, 0u );
    asset_byte_zero( w, p + 0x20 );
    for ( std::uint32_t at : { 0x38u, 0x40u, 0x44u, 0x48u } )
        w.put( p + at, 0u );
    w.put( p + 0x4C, 1u );
    w.put( p + 0x50, 0u );
}

// 0x55C2C0: VuGenericDataAsset's ctor.
inline void generic_data_asset_init( const asset_words_t& w, std::uint32_t p )
{
    asset_base_init( w, p );
    w.put( p, 0x74EDCCu );
    for ( std::uint32_t at : { 0x58u, 0x60u, 0x64u } )
        w.put( p + at, 0u );
}

inline std::uint32_t asset_create( asset_create_kind_t kind, const asset_words_t& w, const asset_create_hooks_t& h )
{
    static constexpr std::uint32_t k_size[ ] = { 0x60u, 0x70u, 0x78u, 0x68u, 0x68u, 0x68u, 0xB0u, 0xA0u, 0x68u };
    const std::uint32_t p = h.alloc ? h.alloc( k_size[ static_cast< int >( kind ) ] ) : 0u;
    if ( p == 0 )
        return 0;
    switch ( kind )
    {
    case asset_create_kind_t::animation:
        w.put( p + 0x58, 0u );
        asset_base_init( w, p );
        w.put( p, 0x74C9E8u );
        w.put( p + 0x58, 0u );
        break;
    case asset_create_kind_t::project:
        asset_base_init( w, p );
        w.put( p, 0x74EB64u );
        for ( std::uint32_t at : { 0x58u, 0x60u, 0x64u, 0x68u } )
            w.put( p + at, 0u );
        break;
    case asset_create_kind_t::db:
        generic_data_asset_init( w, p );
        w.put( p, 0x74EDECu );
        for ( std::uint32_t at : { 0x68u, 0x70u, 0x74u } )
            w.put( p + at, 0u );
        break;
    case asset_create_kind_t::asset_list:
        generic_data_asset_init( w, p );
        w.put( p, 0x74F194u );
        break;
    case asset_create_kind_t::string:
        generic_data_asset_init( w, p );
        w.put( p, 0x750D38u );
        break;
    case asset_create_kind_t::templ:
        generic_data_asset_init( w, p );
        w.put( p, 0x751DF0u );
        break;
    case asset_create_kind_t::collision_mesh:
    {
        for ( std::uint32_t at : { 0x58u, 0x64u, 0x70u, 0x7Cu, 0xA8u } )
            w.put( p + at, 0u );
        asset_base_init( w, p );
        w.put( p, 0x74CC3Cu );
        for ( std::uint32_t at = 0x58u; at <= 0x84u; at += 4u )
            w.put( p + at, 0u );
        const std::uint32_t junk = h.stack_word ? h.stack_word( ) : 0u;
        for ( std::uint32_t at : { 0x88u, 0x8Cu, 0x90u } )
            w.put( p + at, 0x7F7FFFFFu );
        w.put( p + 0x94, junk );
        for ( std::uint32_t at : { 0x98u, 0x9Cu, 0xA0u } )
            w.put( p + at, 0xFF7FFFFFu );
        w.put( p + 0xA4, junk );
        w.put( p + 0xA8, 0u );
        asset_byte_zero( w, p + 0xAC );
        break;
    }
    case asset_create_kind_t::xml:
        for ( std::uint32_t at : { 0x64u, 0x68u, 0x70u, 0x74u, 0x78u, 0x7Cu, 0x80u, 0x8Cu } )
            w.put( p + at, 0u );
        asset_base_init( w, p );
        w.put( p, 0x752458u );
        if ( h.xml_document_ctor )
            h.xml_document_ctor( p + 0x58 );
        break;
    case asset_create_kind_t::timed_event:
        for ( std::uint32_t at : { 0x58u, 0x5Cu, 0x60u } )
            w.put( p + at, 0u );
        asset_base_init( w, p );
        w.put( p, 0x7524D8u );
        for ( std::uint32_t at : { 0x58u, 0x5Cu, 0x60u } )
            w.put( p + at, 0u );
        break;
    }
    return p;
}

inline std::uint32_t asset_create( asset_create_kind_t kind, const asset_words_t& w,
                                   const std::function< std::uint32_t( std::uint32_t ) >& alloc )
{
    asset_create_hooks_t h;
    h.alloc = alloc;
    return asset_create( kind, w, h );
}
// ---- The asset properties' small virtuals on retail's words (boat batch 26) ----
//
// VuAssetNameProperty (the same slots in VuBaseAssetProperty / VuAssetProperty<...>): +0x48 points at a std::vector of
// std::string (24 bytes each) - the asset names of its type.
//   slot 1 0x423860 getType: 11.
//   slot 12 0x423870 choice count: ( end - begin ) / 24 as a signed division (imul 0x2AAAAAAB, sar 2, + the sign bit).
//   slot 13 0x423890( i ) choice i: the i-th string's c_str (its heap text when the capacity +0x14 is >= 16, else the
//            string itself) - no bounds check.
// VuDBEntryProperty: +0x48 the VuDBAsset (or null).
//   slot 12 0x506BB0: null asset -> 0; else its +0x58 document's member "VuDBAsset" (0x4FBDF0: the caller's); type 6 (an
//            object) -> its map's (+8) size (+4), else 0.
//   slot 13 0x506BE0( i ): null asset -> 0; else that member's i-th key (0x4FC000: the caller's) as a c_str.
//   slot 0 0x506B30( flags ): +0 = 0x74C4C0, the +0x48 asset released through [0x759004] slot 9 EVEN WHEN NULL, the
//            property base's dtor 0x405260, delete with flags & 1; eax = this.
struct asset_property_hooks_t
{
    std::function< std::uint32_t( std::uint32_t, const char* ) > json_member;    // 0x4FBDF0 ( container, key )
    std::function< std::uint32_t( std::uint32_t, std::uint32_t ) > json_key;     // 0x4FC000 ( container, index )
};

inline std::uint32_t asset_string_c_str( const asset_words_t& w, std::uint32_t s )
{
    return w.get( s + 0x14 ) >= 0x10u ? w.get( s ) : s;
}

inline std::uint32_t asset_name_property_type( )
{
    return 11u;
}

inline std::int32_t asset_name_property_count( const asset_words_t& w, std::uint32_t self )
{
    const std::uint32_t v = w.get( self + 0x48 );
    return static_cast< std::int32_t >( w.get( v + 4 ) - w.get( v ) ) / 24;
}

inline std::uint32_t asset_name_property_choice( const asset_words_t& w, std::uint32_t self, std::int32_t i )
{
    return asset_string_c_str( w, w.get( w.get( self + 0x48 ) ) + 24u * static_cast< std::uint32_t >( i ) );
}

inline std::uint32_t db_entry_property_count( const asset_words_t& w, std::uint32_t self, const asset_property_hooks_t& h )
{
    const std::uint32_t a = w.get( self + 0x48 );
    if ( a == 0 || !h.json_member )
        return 0;
    const std::uint32_t c = h.json_member( a + 0x58, "VuDBAsset" );
    return w.get( c ) == 6u ? w.get( w.get( c + 8 ) + 4 ) : 0u;
}

inline std::uint32_t db_entry_property_choice( const asset_words_t& w, std::uint32_t self, std::uint32_t i, const asset_property_hooks_t& h )
{
    const std::uint32_t a = w.get( self + 0x48 );
    if ( a == 0 || !h.json_member || !h.json_key )
        return 0;
    return asset_string_c_str( w, h.json_key( h.json_member( a + 0x58, "VuDBAsset" ), i ) );
}

inline std::uint32_t db_entry_property_destroy( const asset_words_t& w, std::uint32_t self, std::uint32_t flags, const asset_dtor_hooks_t& h )
{
    w.put( self, 0x74C4C0u );
    if ( h.factory_release )
        h.factory_release( w.get( self + 0x48 ) );
    if ( h.property_base_dtor )
        h.property_base_dtor( );
    if ( ( flags & 1u ) && h.free_block )
        h.free_block( self );
    return self;
}

// VuProjectAsset's load, slot 5 0x5551B0 ( stream { data, size, position } ): u32 size, that many bytes of VUJB document
// (0x55B560: a failed read answers false, the position left after the size word), then the +0x68 word. Both words are
// little-endian (memcpy_s of 4). The caller's pack record guarantees the bytes; retail does no bounds check, past the
// end this answers false.
inline bool project_asset_load( const std::vector< std::uint8_t >& data, std::size_t& position, value_t& document, std::uint32_t& word )
{
    std::uint32_t size = 0;
    if ( position + 4 > data.size( ) )
        return false;
    std::memcpy( &size, data.data( ) + position, 4 );
    position += 4;
    if ( position + size + 4 > data.size( ) )
        return false;
    const std::vector< std::uint8_t > blob( data.begin( ) + position, data.begin( ) + position + size );
    if ( !c_reader::read_at_start( blob, document ) )
        return false;
    position += size;
    std::memcpy( &word, data.data( ) + position, 4 );
    position += 4;
    return true;
}
