// coop/net/gns_smoke.cpp -- PR-1 verifier for the GameNetworkingSockets
// integration. Replaced by real Session integration in PR-2.
//
// Purpose: force the linker to consume third_party/GameNetworkingSockets
// (static lib) by taking the address of one GNS public symbol. Without
// any reference, /OPT:REF would drop the entire static lib and the link
// test wouldn't actually exercise the integration.
//
// PR-1 does NOT call GameNetworkingSockets_Init() or any other GNS API
// at runtime -- only its address is taken into a never-read global. This
// keeps PR-1's runtime behaviour identical to the pre-GNS DLL while
// still proving the compile + link pipeline works.
//
// Delete this file in PR-2 when coop::net::Session starts genuinely
// calling GNS.

// GNS public headers warn at /W4. Localize the suppression to this file
// so the rest of the project keeps /W4 clean.
#pragma warning(push)
#pragma warning(disable: 4100 4127 4191 4244 4245 4267 4310 4324 4458)
#include <steam/steamnetworkingsockets.h>
#pragma warning(pop)

namespace coop::net::gns_smoke {

// Address-take, not call. Linker must resolve the symbol, but no
// runtime work happens at DllAttach.
[[maybe_unused]] void* const kForceGnsLink =
    reinterpret_cast<void*>(&GameNetworkingSockets_Init);

}  // namespace coop::net::gns_smoke
