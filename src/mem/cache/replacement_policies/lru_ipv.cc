#include "mem/cache/replacement_policies/lru_ipv.hh"

#include "base/logging.hh"

namespace ReplacementPolicy {
constexpr unsigned LRUIPVRP::IPV_K;                         // <-- add this line
constexpr unsigned LRUIPVRP::IPV[LRUIPVRP::IPV_K + 1];      // existing (or add if missing);

LRUIPVRP::LRUIPVRP(const Params &p)
  : Base(p), ways(p.numWays)
{
    /**
     * Constructor safety checks.
     * We deliberately hard-code this policy to k=16 to match the given IPV.
     * If the backing cache has a different associativity, fail fast with a
     * descriptive error so the misconfiguration is surfaced during init.
     */
    fatal_if(ways != IPV_K,
        "LRUIPVRP is hard-coded for %u-way sets, but cache has %u ways.",
        IPV_K, ways);
}

void
LRUIPVRP::reset(const std::shared_ptr<::ReplacementPolicy::ReplacementData>& rd) const
{
    /**
     * Insertion behavior.
     * On allocation/fill, set the line's depth to IPV[k], which is the policy-
     * defined insertion position. Depth is clamped to [0..k-1] by construction
     * of the IPV array, so no further bounds checks are required here.
     */
    auto d = std::static_pointer_cast<IPVReplData>(rd);
    d->depth = static_cast<uint8_t>(IPV[IPV_K]); // IPV[16] == 13
}

void
LRUIPVRP::touch(const std::shared_ptr<::ReplacementPolicy::ReplacementData>& rd) const
{
    /**
     * Promotion behavior on hit.
     * We treat the current depth as "position i" in the virtual recency stack
     * and update it to IPV[i]. This models promoting some hits part-way toward
     * MRU (or leaving them), which can outperform classic "promote to MRU".
     */
    auto d = std::static_pointer_cast<IPVReplData>(rd);
    unsigned cur = d->depth;
    if (cur >= IPV_K) cur = IPV_K - 1;     // defensive clamp, should not trigger
    d->depth = static_cast<uint8_t>(IPV[cur]);
}

void
LRUIPVRP::invalidate(const std::shared_ptr<::ReplacementPolicy::ReplacementData>&) const
{
    /**
     * No special invalidation needed.
     * Our per-entry state consists solely of a bounded depth counter, which is
     * ignored for invalid lines and will be reset upon the next allocation.
     * Leaving this empty keeps the implementation minimal and correct.
     */
    return;
}

ReplaceableEntry*
LRUIPVRP::getVictim(const ReplacementCandidates& candidates) const
{
    /**
     * Victim selection by maximum depth (approximate LRU).
     * We iterate all candidate lines in this set and choose the one with the
     * largest 'depth' value. This avoids relying on any implicit mapping between
     * candidate index and physical way, which gem5 does not specify.
     */
    assert(!candidates.empty());

    ReplaceableEntry* victim = candidates[0];
    unsigned worst = std::static_pointer_cast<IPVReplData>(
        victim->replacementData)->depth;

    for (auto* e : candidates) {
        auto d = std::static_pointer_cast<IPVReplData>(e->replacementData);
        if (d->depth > worst) {
            worst = d->depth;
            victim = e;
        }
    }
    return victim;
}

} // namespace ReplacementPolicy

