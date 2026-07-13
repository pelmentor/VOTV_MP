// coop/creatures/kerfur_form_assembler.h -- the kerfur FORM-FLIP assembler:
// first consumer of the EX_Local* VM-dispatch substrate (ue_wrap/vm_dispatch).
//
// docs/COOP_VM_DISPATCH_PLAN.md S3. The kerfur turn-on / turn-off conversion
// verbs (spawnKerfuro / dropKerfurProp) are BP-internal EX_LocalVirtualFunction
// self-calls -- invisible to both ProcessEvent and Func patches -- which forced
// three probabilistic fragment-stitching crutches (fresh-spawn stamps, destroy-
// edge capture, death-watch poll; takes 8/9/10, take-10 regressed). The substrate
// gives us a DETERMINISTIC bracket at the verb itself; this module will grow into
// the capture + destroy-suppress + deferred-converge assembler that retires those
// crutches (increments 2a-2c).
//
// INCREMENT 1 (this file, observe-only): register the two verbs with the substrate
// and, on each caught dispatch, LOG the Context class + depth. This proves the
// PERMANENT substrate delivers the catch end-to-end through the registration API
// (the same live catch coop/dev/gnatives_probe made throwaway-style at STEP 1.0,
// now through the shipping seam) and turns the inferred kerfur variant family into
// a MEASURED set. NO capture, NO suppress, NO converge, NO authority routing yet --
// those are 2a/2b/2c. The verb bodies run entirely unchanged.
//
// Logging is gated behind [dev] vm_dispatch_log (default off) and capped, so a
// normal session is silent and a hands-on observation run is not spammed.

#pragma once

namespace coop::net { class Session; }

namespace coop::kerfur_form_assembler {

// Register the two conversion verbs with ue_wrap::vm_dispatch and open the session
// gate (SetEnabled(true)). Called from subsystems::Install (world-up, session
// active). Idempotent -- the substrate de-dups the registrations.
void Install(coop::net::Session* session);

// Drive the substrate's deferred GT FName resolution + (dev) the 1/s stats line.
// Called from the game-thread TickGameplay fanout. Cheap no-op once resolved and
// when vm_dispatch_log is off.
void Tick();

// Close the session gate (SetEnabled(false)). Called from the DisconnectAll
// teardown fanout. The swap itself stays (process-lifetime by design).
void OnDisconnect();

}  // namespace coop::kerfur_form_assembler
