//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_LEDGER_READVIEW_H_INCLUDED
#define RIPPLE_LEDGER_READVIEW_H_INCLUDED

#include <ripple/ledger/detail/ReadViewFwdRange.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STLedgerEntry.h>
#include <ripple/protocol/STTx.h>
#include <boost/optional.hpp>
#include <cassert>
#include <cstdint>
#include <memory>

namespace ripple {

/** Reflects the fee settings for a particular ledger.

    The fees are always the same for any transactions applied
    to a ledger. Changes to fees occur in between ledgers.
*/
struct Fees
{
    std::uint64_t base = 0;         // Reference tx cost (drops)
    std::uint32_t units = 0;        // Reference fee units
    std::uint32_t reserve = 0;      // Reserve base (drops)
    std::uint32_t increment = 0;    // Reserve increment (drops)

    Fees() = default;
    Fees (Fees const&) = default;
    Fees& operator= (Fees const&) = default;

    /** Returns the account reserve given the owner count, in drops.

        The reserve is calculated as the reserve base plus
        the reserve increment times the number of increments.
    */
    std::uint64_t
    accountReserve (std::size_t ownerCount) const
    {
        return reserve + ownerCount * increment;
    }
};

//------------------------------------------------------------------------------

/** Information about the notional ledger backing the view. */
struct LedgerInfo
{
    // Fields for all ledgers
    bool open = true;
    LedgerIndex seq = 0;
    std::uint32_t parentCloseTime = 0;

    // Fields for closed ledgers
    // Closed means "tx set already determined"
    uint256 hash = zero;
    uint256 txHash = zero;
    uint256 accountHash = zero;
    uint256 parentHash = zero;

    //uint256 stateHash;
    std::uint64_t drops = 0;

    // If validated is false, it means "not yet validated."
    // Once validated is true, it will never be set false at a later time.
    mutable
    bool validated = false;
    bool accepted = false;

    // flags indicating how this ledger close took place
    int closeFlags = 0;

    // the resolution for this ledger close time (2-120 seconds)
    int closeTimeResolution = 0;

    // For closed ledgers, the time the ledger
    // closed. For open ledgers, the time the ledger
    // will close if there's no transactions.
    //
    std::uint32_t closeTime = 0;
};

// ledger close flags
static
std::uint32_t const sLCF_NoConsensusTime = 1;


inline
bool getCloseAgree (LedgerInfo const& info)
{
    return (info.closeFlags & sLCF_NoConsensusTime) == 0;
}

void addRaw (LedgerInfo const&, Serializer&);

//------------------------------------------------------------------------------

/** A view into a ledger.

    This interface provides read access to state
    and transaction items. There is no checkpointing
    or calculation of metadata.
*/
class ReadView
{
public:
    using tx_type =
        std::pair<std::shared_ptr<STTx const>,
            std::shared_ptr<STObject const>>;

    using key_type = uint256;

    using mapped_type =
        std::shared_ptr<SLE const>;

    using sles_type = detail::ReadViewFwdRange<
        std::shared_ptr<SLE const>>;

    using txs_type =
        detail::ReadViewFwdRange<tx_type>;

    virtual ~ReadView() = default;

    ReadView& operator= (ReadView&& other) = delete;
    ReadView& operator= (ReadView const& other) = delete;

    ReadView()
        : txs(*this)
    {
    }

    ReadView (ReadView const&)
        : txs(*this)
    {
    }

    ReadView (ReadView&& other)
        : txs(*this)
    {
    }

    /** Returns information about the ledger. */
    virtual
    LedgerInfo const&
    info() const = 0;

    /** Returns true if this reflects an open ledger. */
    bool
    open() const
    {
        return info().open;
    }

    /** Returns true if this reflects a closed ledger. */
    bool
    closed() const
    {
        return ! info().open;
    }

    /** Returns the close time of the previous ledger. */
    std::uint32_t
    parentCloseTime() const
    {
        return info().parentCloseTime;
    }

    /** Returns the sequence number of the base ledger. */
    LedgerIndex
    seq() const
    {
        return info().seq;
    }

    /** Returns the fees for the base ledger. */
    virtual
    Fees const&
    fees() const = 0;

    /** Determine if a state item exists.

        @note This can be more efficient than calling read.

        @return `true` if a SLE is associated with the
                specified key.
    */
    virtual
    bool
    exists (Keylet const& k) const = 0;

    /** Return the key of the next state item.

        This returns the key of the first state item
        whose key is greater than the specified key. If
        no such key is present, boost::none is returned.

        If `last` is engaged, returns boost::none when
        the key returned would be outside the open
        interval (key, last).
    */
    virtual
    boost::optional<key_type>
    succ (key_type const& key, boost::optional<
        key_type> last = boost::none) const = 0;

    /** Return the state item associated with a key.

        Effects:
            If the key exists, gives the caller ownership
            of the non-modifiable corresponding SLE.

        @note While the returned SLE is `const` from the
              perspective of the caller, it can be changed
              by other callers through raw operations.

        @return `nullptr` if the key is not present or
                if the type does not match.
    */
    virtual
    std::shared_ptr<SLE const>
    read (Keylet const& k) const = 0;

    // Called to adjust returned balances
    // This is required to support PaymentSandbox
    virtual
    STAmount
    balanceHook (AccountID const& account,
        AccountID const& issuer,
            STAmount const& amount) const
    {
        return amount;
    }

    // used by the implementation
    virtual
    std::unique_ptr<txs_type::iter_base>
    txsBegin() const = 0;

    // used by the implementation
    virtual
    std::unique_ptr<txs_type::iter_base>
    txsEnd() const = 0;

    /** Returns `true` if a tx exists in the tx map.

        A tx exists in the map if it is part of the
        base ledger, or if it is a newly inserted tx.
    */
    virtual
    bool
    txExists (key_type const& key) const = 0;

    /** Read a transaction from the tx map.

        If the view represents an open ledger,
        the metadata object will be empty.

        @return A pair of nullptr if the
                key is not found in the tx map.
    */
    virtual
    tx_type
    txRead (key_type const& key) const = 0;

    //
    // Memberspaces
    //

    // The range of ledger entries
    //sles_type sles;

    // The range of transactions
    txs_type txs;
};

//------------------------------------------------------------------------------

/** ReadView that associates keys with digests. */
class DigestAwareReadView
    : public ReadView
{
public:
    using digest_type = uint256;

    /** Return the digest associated with the key.

        @return boost::none if the item does not exist.
    */
    virtual
    boost::optional<digest_type>
    digest (key_type const& key) const = 0;
};

/** Run a functor on each SLE in a ReadView starting from the key start,
    as long as the functor returns true.
 */
template <class Functor>
void forEachSLE(ReadView const& view, Functor func, uint256 const& start = {})
{
    for (auto k = view.succ(start); k; k = view.succ(*k))
        if (auto sle = view.read(keylet::unchecked(*k)))
            if (! func(*sle))
                break;
}

/** Run a functor on each transaction in a ReadView, as long as the functor
    returns true. Might throw an exception if the ledger is corrupt. */
template <class Functor>
void forEachTx(ReadView const& view, Functor func)
{
    for (auto i = view.txs.begin(); i != view.txs.begin(); ++i)
        if (i->first)
            if (! func(*i))
                break;
}

} // ripple

#include <ripple/ledger/detail/ReadViewFwdRange.ipp>

#endif