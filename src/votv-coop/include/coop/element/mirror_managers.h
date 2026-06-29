// coop/element/mirror_managers.h -- the ONE canonical home for the entity mirror
// managers. There are exactly THREE streamed-mirror kinds, each a process-wide
// MirrorManager<T> singleton (the MTA per-type CClient*Manager analog):
//
//   PropMirrors()  -> MirrorManager<Prop>        chipPiles / trash / kerfur OFF-props / all grabbable props
//   NpcMirrors()   -> MirrorManager<Npc>         kerfur NPC form / zombies / ariral / wisps / all AI characters
//   WaMirrors()    -> MirrorManager<WorldActor>  UFOs / ships / jellyfish (non-Character event actors)
//
// (Player is NOT here -- it has its own coop::players::Registry. Kerfur is a
// host-only logical id record with no manager; its rendered form is a Prop/Npc
// mirror above.)
//
// RULE 2 (one concept, one implementation): these accessors were previously
// re-declared as a local `inline` 1-liner in 11 subsystem .cpp files
// (PropMirrors x4, NpcMirrors x6, WaMirrors x1). Consolidated here 2026-06-29 --
// include this header and `using coop::element::{Prop,Npc,Wa}Mirrors;` (or call
// fully-qualified) instead of re-defining the wrapper.

#pragma once

#include "coop/element/mirror_manager.h"
#include "coop/element/npc.h"
#include "coop/element/prop.h"
#include "coop/element/world_actor.h"

namespace coop::element {

inline MirrorManager<Prop>& PropMirrors() { return MirrorManager<Prop>::Instance(); }
inline MirrorManager<Npc>& NpcMirrors() { return MirrorManager<Npc>::Instance(); }
inline MirrorManager<WorldActor>& WaMirrors() { return MirrorManager<WorldActor>::Instance(); }

}  // namespace coop::element
