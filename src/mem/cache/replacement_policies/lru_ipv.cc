#include "mem/cache/replacement_policies/lru_ipv.hh"

#include "base/logging.hh"

namespace ReplacementPolicy {

/** Provide definitions for the static constexprs declared in the header. */
constexpr unsigned LRUIPVRP::IPV_K;
const unsigned LRUIPVRP::IPV[LRUIPVRP::IPV_K + 1] = {
    /* promotion targets for positions 0..15, then insertion at index 16 */
    0, 0, 1, 0, 3, 0, 1, 2, 1, 0, 5, 1, 0, 0, 1, 11, 13
};

LRUIPVRP::LRUIPVRP(const Params &p)
  : Base(p), ways(p.numWays)
{
    /**
     * Constructor safety / mode selection.
     * If the cache is 16-way, we can safely use the hard-coded IPV table.
     * If not (e.g., 2-way L1), use a simple LRU-like fallback so global
     * --repl_policy still works without crashing the simulation.
     */
    useIpv = (ways == IPV_K);
    if (!useIpv) {
        warn("LRUIPVRP: cache associativity is %u; using LRU-like fallback "
             "for this cache (IPV requires %u).", ways, IPV_K);
    }
}

void
LRUIPVRP::reset(const std::shared_ptr<::ReplacementPolicy::ReplacementData>& rd) const
{
    /**
     * Insertion behavior.
     * When a line is (re)allocated, initialize its recency depth.
     * - IPV mode (16-way): set depth to IPV[k] (index 16), which controls how
     *   aggressively a new line competes (e.g., 13 = near LRU).
     * - Fallback mode: set depth to (ways-1) to approximate "insert at LRU".
     * This mirrors common LRU variants where a new line must prove reuse.
     */
    auto d = std::static_pointer_cast<IPVReplData>(rd);
    if (useIpv) {
        d->depth = static_cast<uint8_t>(IPV[IPV_K]); // IPV[16] == 13
    } else {
        d->depth = static_cast<uint8_t>(ways - 1);
    }
}

void
LRUIPVRP::touch(const std::shared_ptr<::ReplacementPolicy::ReplacementData>& rd) const
{
    /**
     * Promotion behavior on hit.
     * - IPV mode: treat current depth as stack position i and update to IPV[i],
     *   modeling partial promotions that may outperform "promote to MRU".
     * - Fallback mode: promote directly to MRU (depth = 0), i.e., standard LRU.
     * A defensive clamp ensures i ∈ [0..k-1] in IPV mode, though it should not
     * trigger under normal operation.
     */
    auto d = std::static_pointer_cast<IPVReplData>(rd);
    if (useIpv) {
        unsigned cur = d->depth;
        if (cur >= IPV_K) cur = IPV_K - 1; // defensive clamp
        d->depth = static_cast<uint8_t>(IPV[cur]);
    } else {
        d->depth = 0;
    }
}

void
LRUIPVRP::invalidate(const std::shared_ptr<::ReplacementPolicy::ReplacementData>&) const
{
    /**
     * No special invalidation needed.
     * Our only state is a small depth value, which is ignored for invalid lines
     * and reinitialized on the next reset(). Leaving this empty is sufficient.
     */
    return;
}

ReplaceableEntry*
LRUIPVRP::getVictim(const ReplacementCandidates& candidates) const
{
    /**
     * Victim selection by maximum depth (approximate LRU).
     * Iterate over all candidates in the set and pick the one with the largest
     * depth value. This avoids any reliance on candidate order or physical way
     * mapping and works for both IPV and fallback modes.
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
