#include "ue_wrap/call.h"

#include "ue_wrap/log.h"
#include "ue_wrap/reflection.h"

#include <cstring>

namespace ue_wrap {

ParamFrame::ParamFrame(void* function) : fn_(function) {
    if (!fn_) return;
    const int32_t frameSize = reflection::FunctionFrameSize(fn_);
    if (frameSize < 0) {  // genuinely malformed UFunction; refuse
        UE_LOGE("ParamFrame: function %p has negative frame size %d", fn_, frameSize);
        fn_ = nullptr;
        return;
    }
    // frameSize == 0 is VALID: a no-param/no-return UFunction (e.g. K2_DestroyActor).
    // ProcessEvent is then invoked with a null params buffer (buf_ stays empty).
    // Previously this was treated as an error and the call became a silent no-op --
    // so DestroyActor never destroyed anything (freecam cams / nameplates / puppets
    // leaked). Keep fn_ so the call goes through.
    if (frameSize > 0) {
        buf_.assign(static_cast<size_t>(frameSize), 0);
        // Cache param name->offset once (the FProperty chain doesn't change).
        for (const auto& p : reflection::FunctionParams(fn_)) {
            offsets_.emplace_back(p.name, p.offset);
        }
    }
}

int32_t ParamFrame::OffsetOf(const wchar_t* name) const {
    for (const auto& o : offsets_) {
        if (o.first == name) return o.second;
    }
    return -1;
}

bool ParamFrame::SetRaw(const wchar_t* name, const void* src, int32_t size) {
    if (fn_ == nullptr || buf_.empty()) return false;  // empty => zero-param frame; nothing to set
    const int32_t off = OffsetOf(name);
    if (off < 0) {
        UE_LOGE("ParamFrame::Set: unknown param '%ls'", name);
        return false;
    }
    if (off + size > static_cast<int32_t>(buf_.size())) {
        UE_LOGE("ParamFrame::Set: param '%ls' off=%d size=%d overflows frame %zu",
                name, off, size, buf_.size());
        return false;
    }
    std::memcpy(buf_.data() + off, src, static_cast<size_t>(size));
    return true;
}

bool ParamFrame::GetRaw(const wchar_t* name, void* dst, int32_t size) const {
    if (fn_ == nullptr || buf_.empty()) return false;
    const int32_t off = OffsetOf(name);
    if (off < 0) {
        UE_LOGE("ParamFrame::Get: unknown param '%ls'", name);
        return false;
    }
    if (off + size > static_cast<int32_t>(buf_.size())) {
        UE_LOGE("ParamFrame::Get: param '%ls' off=%d size=%d overflows frame %zu",
                name, off, size, buf_.size());
        return false;
    }
    std::memcpy(dst, buf_.data() + off, static_cast<size_t>(size));
    return true;
}

bool Call(void* object, ParamFrame& frame) {
    if (!frame.valid()) return false;
    return reflection::CallFunction(object, frame.function(), frame.data());
}

}  // namespace ue_wrap
