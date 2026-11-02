#pragma once

// VuUnlockManagerImpl (g 0x758FE8, 0x30 bytes, ctor 0x4B5DD0, vtable 0x745D3C; boat batch 33): the credits and feats a save
// holds and what they unlock, over UnlockDB. Read from the decompilation and disassembly. Saved in the game save's "Unlock"
// member: the "OnCloudSavePreSave" listener 0x4B6A90 is save( ), the "OnCloudSaveDataChanged" listener 0x4B6AC0 and the
// init 0x4B5F70's last step are load( ).
//   +0xC ready, +0x10 total credits, +0x14 local multiplayer credits, +0x18 online multiplayer credits, +0x20 the feats (64
//   bits, one per UnlockDB "Feats" row), +0x28 UnlockDB, +0x2C "unlock all".
// The DB's walk 0x4B7250 reports every Unlockables row that the step from (old credits, old feats) to (new credits, new
// feats) unlocks: the callback (retail's functor; 0x4B6600 / 0x4B6540's caller's, VuUnlockCounter 0x4B67F0 for the count) is
// how the UI hears of them.
// HOOK: 0x4D3170 (the sku is "StoreBeta"), g_vudevconfig's "UnlockAll" (the init's caller passes it).
// NOT RECREATED here: the rewards slots 6 - 11 0x4B60B0 .. 0x4B6440 (world/awards.hxx's unlock_value is their caller; the reads
// are short and will follow it), the event map at +4 (the caller registers listeners).

#include <workspace/core/assets/tree.hxx>
#include <workspace/core/entity/json.hxx>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// 0x4FDB90 (and each inline copy of it here): int, float (cvttss2si), int64's low word. False: none of those.
inline bool unlock_int( const value_t& v, std::int32_t& out )
{
    if ( v.tag == 1 )
        out = v.integer;
    else if ( v.tag == 2 )
        out = _mm_cvttss_si32( _mm_set_ss( v.real ) );
    else if ( v.tag == 7 )
        out = static_cast< std::int32_t >( json_lo( v ) );
    else
        return false;
    return true;
}

inline std::int32_t unlock_int_or_zero( const value_t& v )
{
    std::int32_t out = 0;
    unlock_int( v, out );
    return out;
}

// The array read every walk here inlines: an element in range, else the null container 0x7593A0.
inline const value_t& unlock_at( const value_t& list, std::int32_t k )
{
    static const value_t null{ };
    return list.tag == 5 && k >= 0 && k < static_cast< std::int32_t >( list.list.size( ) ) ? list.list[ static_cast< std::size_t >( k ) ] : null;
}

// A string member, else the static empty string 0x819950.
inline const std::string& unlock_text( const value_t& row, const char* key )
{
    static const std::string empty;
    const value_t& v = member( row, key );
    return v.tag == 4 ? v.text : empty;
}

inline std::int32_t unlock_count( const value_t& list )
{
    return list.tag == 5 ? static_cast< std::int32_t >( list.list.size( ) ) : 0;
}

class c_unlock_manager
{
public:
    using found_t = std::function< void( const value_t& row ) >;

    std::int32_t ready = 0;            // +0xC
    std::int32_t total = 0;            // +0x10
    std::int32_t local = 0;            // +0x14
    std::int32_t online = 0;           // +0x18
    std::uint64_t feats = 0;           // +0x20
    std::uint8_t unlock_all = 0;       // +0x2C
    const value_t* db = nullptr;       // +0x28's +0x58 (null until the init)
    std::function< bool( ) > store_beta;   // 0x4D3170

    // Slot 5 0x4B5F70 (the asset's load is the caller's: `database` its document; null: false, nothing else happens).
    bool init( const value_t* database, bool dev_unlock_all, const value_t& cloud_data )
    {
        db = database;
        if ( db == nullptr )
            return false;
        ready = 1;
        if ( dev_unlock_all )
            unlock_all = 1;
        load( member( cloud_data, "Unlock" ) );
        return true;
    }

    // Slot 3 0x4B6080 (the asset's release is the caller's).
    void release( )
    {
        db = nullptr;
        ready = 0;
    }

    // 0x4B7430: Feats[ k ]'s Name exact -> 1 << k (past bit 63: 0, the bts + two cmov pair); no such row: 0.
    std::uint64_t feat_bit( const std::string& name ) const
    {
        const value_t& rows = member( root( ), "Feats" );
        const std::int32_t count = unlock_count( rows );
        for ( std::int32_t k = 0; k < count; ++k )
            if ( unlock_text( unlock_at( rows, k ), "Name" ) == name )
                return k < 64 ? std::uint64_t{ 1 } << k : 0u;
        return 0;
    }

    // 0x4B75D0: the OR of its "Feats" strings' bits (a non-string element adds nothing).
    std::uint64_t row_feats( const value_t& row ) const
    {
        const value_t& list = member( row, "Feats" );
        std::uint64_t out = 0;
        const std::int32_t count = unlock_count( list );
        for ( std::int32_t k = 0; k < count; ++k )
        {
            const value_t& e = unlock_at( list, k );
            if ( e.tag == 4 )
                out |= feat_bit( e.text );
        }
        return out;
    }

    // 0x4B73B0: the row's feats held and the credits reached.
    bool row_met( const value_t& row, std::int32_t credits, std::uint64_t have ) const
    {
        const std::uint64_t need = row_feats( row );
        const std::int32_t cost = unlock_int_or_zero( member( row, "Credits" ) );
        return ( need == 0 || ( have & need ) == need ) && credits >= cost;
    }

    // 0x4B7250: rows not met at (old) and met at (new). Each found row's "Type" lookup in retail has no effect.
    void walk( std::int32_t old_credits, std::int32_t new_credits, std::uint64_t old_feats, std::uint64_t new_feats, const found_t* found ) const
    {
        const value_t& rows = member( root( ), "Unlockables" );
        const std::int32_t count = unlock_count( rows );
        for ( std::int32_t k = 0; k < count; ++k )
        {
            const value_t& row = unlock_at( rows, k );
            if ( !row_met( row, old_credits, old_feats ) && row_met( row, new_credits, new_feats ) && found != nullptr && *found )
                ( *found )( row );
        }
    }

    // Slot 12 0x4B6500.
    void set_credits( std::int32_t t, std::int32_t l, std::int32_t o )
    {
        total = t;
        local = l;
        online = o;
        walk( -1, total, 0, feats, nullptr );
    }

    // Slot 13 0x4B6540: `kind` 1 local multiplayer, 2 online.
    void add_credits( std::int32_t amount, std::int32_t kind, const found_t* found )
    {
        const std::int32_t old = total;
        total = static_cast< std::int32_t >( static_cast< std::uint32_t >( old ) + static_cast< std::uint32_t >( amount ) );
        if ( kind == 1 )
            local = static_cast< std::int32_t >( static_cast< std::uint32_t >( local ) + static_cast< std::uint32_t >( amount ) );
        else if ( kind == 2 )
            online = static_cast< std::int32_t >( static_cast< std::uint32_t >( online ) + static_cast< std::uint32_t >( amount ) );
        walk( old, total, feats, feats, found );
    }

    // Slot 17 0x4B65D0: every row the feats alone unlock is met at (credits, 0) -> (credits, feats); the found functor null.
    void refresh( )
    {
        walk( total, total, 0, feats, nullptr );
    }

    // Slot 18 0x4B6600.
    void add_feat( const std::string& name, const found_t* found )
    {
        const std::uint64_t old = feats;
        feats = old | feat_bit( name );
        walk( total, total, old, feats, found );
    }

    // Slot 19 0x4B6660.
    bool has_feat( const std::string& name ) const
    {
        return ( feat_bit( name ) & feats ) != 0;
    }

    // Slot 22 0x4B66E0 (VuUnlockCounter): how many rows `credits` more and these feats would unlock.
    std::int32_t unlock_count_for( std::int32_t credits, const std::vector< std::string >& names ) const
    {
        std::uint64_t mask = 0;
        for ( const std::string& n : names )
            mask |= feat_bit( n );
        std::int32_t count = 0;
        const found_t counter = [ & ]( const value_t& ) { ++count; };
        walk( total, static_cast< std::int32_t >( static_cast< std::uint32_t >( credits ) + static_cast< std::uint32_t >( total ) ), feats, mask | feats, &counter );
        return count;
    }

    // 0x4B6CC0: the row by Type and Name, exact; none: null.
    const value_t* find( const std::string& type, const std::string& name ) const
    {
        const value_t& rows = member( root( ), "Unlockables" );
        const std::int32_t count = unlock_count( rows );
        for ( std::int32_t k = 0; k < count; ++k )
        {
            const value_t& row = unlock_at( rows, k );
            if ( unlock_text( row, "Type" ) == type && unlock_text( row, "Name" ) == name )
                return &row;
        }
        return nullptr;
    }

    // 0x4B6F10: by Mode and Track exact, and for Mode "Slalom" also Course; none: null.
    const value_t* find_track( const std::string& mode, const std::string& track, const std::string& course ) const
    {
        const value_t& rows = member( root( ), "Unlockables" );
        const std::int32_t count = unlock_count( rows );
        for ( std::int32_t k = 0; k < count; ++k )
        {
            const value_t& row = unlock_at( rows, k );
            if ( unlock_text( row, "Mode" ) != mode || unlock_text( row, "Track" ) != track )
                continue;
            if ( mode == "Slalom" && unlock_text( row, "Course" ) != course )
                continue;
            return &row;
        }
        return nullptr;
    }

    // Slots 23 0x4B6800 / 25 0x4B68C0: unlock all, or no row (a null row reads as tag 0 in retail), else the store beta gate,
    // else met at the current credits and feats.
    bool is_unlocked( const std::string& type, const std::string& name ) const
    {
        return unlock_all != 0 || gate( find( type, name ) );
    }

    bool is_track_unlocked( const std::string& mode, const std::string& track, const std::string& course ) const
    {
        return unlock_all != 0 || gate( find_track( mode, track, course ) );
    }

    // Slots 24 0x4B6880 / 26 0x4B6940: no row: true; else its AvailableInTrial bool (else false).
    bool in_trial( const std::string& type, const std::string& name ) const
    {
        return trial( find( type, name ) );
    }

    bool track_in_trial( const std::string& mode, const std::string& track, const std::string& course ) const
    {
        return trial( find_track( mode, track, course ) );
    }

    // Slot 27 0x4B6980: the fewest credits to the next row dearer than the current total; false: no such row.
    bool next_credits( std::int32_t& out ) const
    {
        const value_t& rows = member( root( ), "Unlockables" );
        const std::int32_t count = unlock_count( rows );
        bool any = false;
        std::int32_t best = 0x7FFFFFFF;
        for ( std::int32_t k = 0; k < count; ++k )
        {
            const std::int32_t cost = unlock_int_or_zero( member( unlock_at( rows, k ), "Credits" ) );
            if ( total < cost )
            {
                any = true;
                const std::int32_t need = static_cast< std::int32_t >( static_cast< std::uint32_t >( cost ) - static_cast< std::uint32_t >( total ) );
                if ( need < best )
                    best = need;
            }
        }
        if ( any )
            out = best;
        return any;
    }

    // 0x4B6AF0: the document's four members (the listener passes cloud.data["Unlock"]).
    void save( value_t& out ) const
    {
        json_set_int( json_member( out, "TotalCredits" ), total );
        json_set_int( json_member( out, "LocalMultiplayerCredits" ), local );
        json_set_int( json_member( out, "OnlineMultiplayerCredits" ), online );
        value_t& f = json_member( out, "Feats" );
        json_clear( f );
        f.tag = 7;
        f.kind = value_t::kind_t::int64;
        f.int64 = static_cast< std::int64_t >( feats );
    }

    // 0x4B6BA0: Feats: int (sign extended), float (__ftol2: truncated to 64 bits), int64, else 0; then slots 12 and 17.
    void load( const value_t& in )
    {
        const std::int32_t t = unlock_int_or_zero( member( in, "TotalCredits" ) );
        const std::int32_t l = unlock_int_or_zero( member( in, "LocalMultiplayerCredits" ) );
        const std::int32_t o = unlock_int_or_zero( member( in, "OnlineMultiplayerCredits" ) );
        const value_t& f = member( in, "Feats" );
        if ( f.tag == 1 )
            feats = static_cast< std::uint64_t >( static_cast< std::int64_t >( f.integer ) );
        else if ( f.tag == 2 )
            feats = static_cast< std::uint64_t >( _mm_cvttss_si64( _mm_set_ss( f.real ) ) );
        else if ( f.tag == 7 )
            feats = static_cast< std::uint64_t >( f.int64 );
        else
            feats = 0;
        set_credits( t, l, o );
        refresh( );
    }

private:
    const value_t& root( ) const
    {
        static const value_t null{ };
        return db != nullptr ? member( *db, "VuDBAsset" ) : null;
    }

    bool gate( const value_t* row ) const
    {
        if ( row == nullptr )
            return true;
        if ( store_beta && store_beta( ) )
        {
            const value_t& beta = member( *row, "AvailableInStoreBeta" );
            if ( beta.tag != 3 || beta.integer == 0 )
                return false;
        }
        return row_met( *row, total, feats );
    }

    static bool trial( const value_t* row )
    {
        if ( row == nullptr )
            return true;
        const value_t& v = member( *row, "AvailableInTrial" );
        return v.tag == 3 && v.integer != 0;
    }
};
