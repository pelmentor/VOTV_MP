// coop/dev/add_points.cpp -- see coop/dev/add_points.h.

#include "coop/dev/add_points.h"

#include "coop/balance_sync.h"

namespace coop::dev::add_points {

void GivePoints(int amount) {
    // Route through the shared-balance feature so the credit lands on the HOST's
    // canonical balance and both peers stay mirrored: on the host (or solo) it applies
    // locally via AddPoints (the host poll then broadcasts the new total); on a client
    // it sends a BalanceDelta request to the host. CreditRouted is render-thread safe.
    coop::balance_sync::CreditRouted(amount);
}

}  // namespace coop::dev::add_points
