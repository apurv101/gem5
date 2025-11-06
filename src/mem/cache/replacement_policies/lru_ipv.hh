#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_LRU_IPV_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_LRU_IPV_HH__

#include "mem/cache/replacement_policies/base.hh"
#include "base/logging.hh"
#include "params/LRUIPVRP.hh"
/**
 * LRU-IPV (Insertion/Promotion Vector) replacement policy — hard-coded for k=16.
 *
 * This policy keeps a per-entry "depth" in [0..k-1] where 0≈MRU and k-1≈LRU.
 * Instead of maintaining a full per-set recency stack (which is awkward to
 * coordinate in gem5’s RP interface), we apply the IPV directly to each line’s
 * depth on hits and on insertions. Victim selection chooses the line with the
 * largest depth. This captures the intended insertion/promotion behavior while
 * avoiding global set-state and index coupling with candidates orderings.
 */
namespace ReplacementPolicy {

class LRUIPVRP : public Base
{
  public:
    /** SimObject params type (defined in Python). */
    using Params = LRUIPVRPParams;

    /**
     * Per-entry metadata for the replacement policy.
     * We store a single byte 'depth' where 0 indicates MRU and 15 indicates LRU.
     * This bounded counter approximates a recency position and is updated by
     * the insertion/promotion vector on reset() and touch() respectively.
     */
    struct IPVReplData : public ::ReplacementPolicy::ReplacementData
    {
        /** 0..15 recency depth; larger means more likely to be evicted. */
        mutable uint8_t depth = 0;
        IPVReplData() = default;
    };

  private:
    /** This implementation is hard-wired to 16-way sets. */
    static constexpr unsigned IPV_K = 16;

    /**
     * Hard-coded IPV with length k+1 as described in the paper’s example:
     * indices 0..k-1 give the promotion target for a block currently at that
     * "position/depth"; index k gives the insertion position for a new block.
     * All entries must be in [0..k-1]. The last entry (index 16) is 13 here.
     */
    static constexpr unsigned IPV[IPV_K + 1] = {
        0, 0, 1, 0, 3, 0, 1, 2, 1, 0, 5, 1, 0, 0, 1, 11, 13
    };

    /** Associativity as provided by the cache; must equal 16. */
    const unsigned ways;

  public:
    /**
     * Constructor.
     * We verify the set associativity matches the hard-coded k=16; this prevents
     * silent misconfiguration that would apply out-of-bounds indices or produce
     * undefined behavior. If the associativity differs, we fatal with a clear
     * message so the user knows to either adjust the cache or generalize IPV.
     */
    LRUIPVRP(const Params &p);

    /**
     * Allocate and attach our per-entry metadata.
     * gem5 calls instantiateEntry once per cache line to create opaque policy
     * state. We return a small object (IPVReplData) that holds the bounded
     * 'depth' counter. No per-set shared state is created here by design.
     */
    std::shared_ptr<::ReplacementPolicy::ReplacementData>
    instantiateEntry() override
    {
        return std::make_shared<IPVReplData>();
    }

    /**
     * Called when a block is (re)inserted into the set (fill/allocate).
     * We set the line's depth to the insertion target defined by IPV[k], where
     * k is the associativity (16 here). This embodies the "insertion policy"
     * component of IPV, and determines how aggressively a new line competes
     * with existing lines (near MRU vs near LRU).
     */
    void reset(const std::shared_ptr<::ReplacementPolicy::ReplacementData>& rd) const override;

    /**
     * Called on every cache hit to update recency information.
     * We interpret the current depth as the "position i" and map it to the new
     * position IPV[i]. This realizes the "promotion policy" component of IPV
     * without doing global per-set shifts, keeping the logic simple and local.
     */
    void touch(const std::shared_ptr<::ReplacementPolicy::ReplacementData>& rd) const override;

    /**
     * Optional cleanup when a line becomes invalid; not required here.
     * Some policies maintain additional flags or ages that need clearing on
     * invalidation. Our bounded 'depth' is harmless and will be reinitialized
     * by reset() on the next allocation, so we leave this empty intentionally.
     */
    void invalidate(const std::shared_ptr<::ReplacementPolicy::ReplacementData>& rd) const override;

    /**
     * Choose a victim among candidates in the set.
     * We scan the provided candidates (which are all lines in the set) and
     * select the one with the maximum depth, i.e., the “most LRU” according to
     * our bounded counter. This avoids assuming any order correspondence between
     * candidates and physical ways, which gem5 does not guarantee.
     */
    ReplaceableEntry* getVictim(const ReplacementCandidates& candidates) const override;
};

} // namespace ReplacementPolicy

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_LRU_IPV_HH__

