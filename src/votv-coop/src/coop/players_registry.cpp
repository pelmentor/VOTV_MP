// coop/players_registry.cpp -- see header for rationale.

#include "coop/players_registry.h"

#include "coop/remote_player.h"
#include "ue_wrap/engine.h"
#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"
#include "ue_wrap/sdk_profile.h"

#include <string>

namespace coop::players {

namespace P = ue_wrap::profile;
namespace R = ue_wrap::reflection;
namespace E = ue_wrap::engine;

Registry& Registry::Get() {
    static Registry s_instance;
    return s_instance;
}

void* Registry::RescanLocal() {
    const int32_t n = R::NumObjects();
    for (int32_t i = 0; i < n; ++i) {
        void* obj = R::ObjectAt(i);
        if (!obj) continue;
        if (R::ClassNameOf(obj) != P::name::MainPlayerClass) continue;
        const std::wstring nm = R::ToString(R::NameOf(obj));
        if (nm.rfind(L"Default__", 0) == 0) continue;  // skip CDO
        if (!R::IsLive(obj)) continue;
        // The discriminator: only the LOCAL player has a non-null Controller.
        // Puppets are explicitly unpossessed (AutoPossess + AI disabled at
        // deferred-spawn) per [[project-coop-enemies-target-both]]. This
        // check is the SINGLE place this discriminator lives -- everywhere
        // else uses Registry::IsLocal(actor) which queries the cache.
        if (!E::GetController(obj)) continue;
        return obj;
    }
    return nullptr;
}

void* Registry::Local() {
    if (localCached_ && R::IsLive(localCached_) && E::GetController(localCached_)) {
        return localCached_;
    }
    localCached_ = RescanLocal();
    return localCached_;
}

uint8_t Registry::LocalPeerId() const {
    return localPeerId_;
}

void Registry::SetLocalPeerId(uint8_t id) {
    localPeerId_ = id;
}

void Registry::InvalidateLocal() {
    localCached_ = nullptr;
}

RemotePlayer* Registry::Puppet(uint8_t peerSessionId) {
    if (peerSessionId >= kMaxPeers) return nullptr;
    return puppetByPeer_[peerSessionId];
}

void Registry::RegisterPuppet(uint8_t peerSessionId, RemotePlayer* puppet) {
    if (peerSessionId >= kMaxPeers) {
        UE_LOGW("players::Registry: peerSessionId %u out of range (max=%u)",
                peerSessionId, static_cast<unsigned>(kMaxPeers));
        return;
    }
    puppetByPeer_[peerSessionId] = puppet;
    UE_LOGI("players::Registry: registered puppet peerId=%u -> %p", peerSessionId, puppet);
}

void Registry::UnregisterPuppet(uint8_t peerSessionId) {
    if (peerSessionId >= kMaxPeers) return;
    puppetByPeer_[peerSessionId] = nullptr;
}

bool Registry::IsLocal(void* actor) {
    if (!actor) return false;
    return actor == Local();
}

bool Registry::IsPuppet(void* actor) {
    if (!actor) return false;
    for (int i = 0; i < kMaxPeers; ++i) {
        if (puppetByPeer_[i] && puppetByPeer_[i]->GetActor() == actor) return true;
    }
    return false;
}

uint8_t Registry::PeerIdOfActor(void* actor) {
    if (!actor) return kPeerIdUnknown;
    if (actor == Local()) return localPeerId_;
    for (int i = 0; i < kMaxPeers; ++i) {
        if (puppetByPeer_[i] && puppetByPeer_[i]->GetActor() == actor) {
            return static_cast<uint8_t>(i);
        }
    }
    return kPeerIdUnknown;
}

}  // namespace coop::players
