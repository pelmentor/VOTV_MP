// coop/props/prop_element_tracker_detail.h -- INTERNAL shared state between
// prop_element_tracker.cpp (Mark/Unmark maintenance + reaper) and
// prop_census.cpp (the seed/re-seed GUObjectArray walk, extracted 2026-07-10
// when the tracker passed the 800-LOC soft cap).
//
// Sibling-internal header (the net/session_lanes.h / event_dispatch.h
// precedent) -- NOT part of the public coop/ include surface; only the two
// TUs above include it.
//
// The maintained set semantics are the tracker's (see the H2-redux comment
// there): seeded once by the census walk, then maintained by Init POST
// (insert) + K2_DestroyActor PRE (evict). Mutex held only for brief set ops;
// no engine calls under lock.

#pragma once

#include <cstddef>
#include <mutex>
#include <unordered_set>

namespace coop::prop_element_tracker {

extern std::mutex g_knownKeyedPropsMutex;
extern std::unordered_set<void*> g_knownKeyedProps;
inline constexpr size_t kKnownKeyedPropsCap = 16384;

}  // namespace coop::prop_element_tracker
