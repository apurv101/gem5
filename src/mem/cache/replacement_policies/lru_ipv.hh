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
    /** This implementation is hard-wired to 16-way sets for IPV behavior. */
    static constexpr unsigned IPV_K = 16;

    /**
     * Hard-coded IPV with length k+1 as described in the paper’s example:
     * indices 0..k-1 give the promotion target for a block currently at that
     * "position/depth"; index k gives the insertion position for a new block.
     * All entries must be in [0..k-1]. The last entry (index 16) is 13 here.
     *
     * NOTE: Only declared here; definition with initializer is in the .cc to
     * avoid ODR issues on older compilers/toolchains.
     */
    static const unsigned IPV[IPV_K + 1];

    /** Associativity as provided by the cache. */
    const unsigned ways;

    /**
     * Whether to use the hard-coded IPV (only when associativity == 16).
     * If false (e.g., 2-way L1), we use a minimal LRU-like fallback so runs
     * don’t fail fatally when a global --repl_policy applies to all levels.
     */
    bool useIpv = false;

  public:
    /**
     * Constructor.
     * If the associativity matches k=16, we enable the IPV behavior. Otherwise
     * we warn and switch to an LRU-like fallback for this cache while preserving
     * the depth-based victim choice contract.
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
     * If IPV is active (16-way), set depth to IPV[k] (the insertion position).
     * Otherwise, insert near LRU (depth = ways-1) so the line must prove reuse.
     */
    void reset(const std::shared_ptr<::ReplacementPolicy::ReplacementData>& rd) const override;

    /**
     * Called on every cache hit to update recency information.
     * If IPV is active, interpret current depth as position i and set depth to
     * IPV[i] (promotion policy). Otherwise, promote to MRU (depth = 0).
     */
    void touch(const std::shared_ptr<::ReplacementPolicy::ReplacementData>& rd) const override;

    /**
     * Optional cleanup when a line becomes invalid; not required here.
     * Our bounded 'depth' is ignored for invalid lines and reinitialized by
     * reset() on the next allocation, so nothing is needed.
     */
    void invalidate(const std::shared_ptr<::ReplacementPolicy::ReplacementData>& rd) const override;

    /**
     * Choose a victim among candidates in the set.
     * We scan the provided candidates and select the one with the maximum depth,
     * i.e., the “most LRU” according to our bounded counter. This avoids any
     * reliance on candidate order vs physical way.
     */
    ReplaceableEntry* getVictim(const ReplacementCandidates& candidates) const override;
};

} // namespace ReplacementPolicy

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_LRU_IPV_HH__
