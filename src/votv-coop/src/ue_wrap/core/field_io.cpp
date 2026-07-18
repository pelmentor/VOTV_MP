// ue_wrap/core/field_io.cpp -- see ue_wrap/core/field_io.h.

#include "ue_wrap/core/field_io.h"

#include "ue_wrap/core/fstring_utils.h"
#include "ue_wrap/core/reflection.h"

#include <cstring>

namespace ue_wrap::field_io {
namespace {

namespace R = reflection;

// Free the engine-allocated buffer behind an FString header slot (no-op on
// null/empty). See the header doctrine note.
void FreeFStringSlot(void* header16) {
    auto* v = reinterpret_cast<FStringView*>(header16);
    if (v->data) R::EngineFree(v->data);
    v->data = nullptr; v->num = 0; v->max = 0;
}

void FreeFStringArraySlot(TArrayView* view) {
    if (view->data) {
        for (int32_t i = 0; i < view->num; ++i)
            FreeFStringSlot(view->data + i * 16);
        R::EngineFree(view->data);
    }
    view->data = nullptr; view->num = 0; view->max = 0;
}

}  // namespace

std::wstring ReadFStringAt(const void* base, int32_t off) {
    if (off < 0) return std::wstring();
    const auto* v = reinterpret_cast<const FStringView*>(
        reinterpret_cast<const uint8_t*>(base) + off);
    if (!v->data || v->num <= 1) return std::wstring();
    return std::wstring(v->data, static_cast<size_t>(v->num - 1));
}

bool WriteFStringField(void* base, int32_t off, const std::wstring& s) {
    if (off < 0) return false;
    uint8_t* slot = reinterpret_cast<uint8_t*>(base) + off;
    const FStringView old = *reinterpret_cast<const FStringView*>(slot);
    if (!fstring_utils::MintFString(s, slot)) return false;  // failure leaves the old value intact
    if (old.data) R::EngineFree(old.data);
    return true;
}

std::vector<std::wstring> ReadFStringArrayField(const void* base, int32_t off) {
    std::vector<std::wstring> out;
    if (off < 0) return out;
    const auto* view = reinterpret_cast<const TArrayView*>(
        reinterpret_cast<const uint8_t*>(base) + off);
    if (!view->data || view->num <= 0 || view->num > 4096) return out;
    for (int32_t i = 0; i < view->num; ++i)
        out.push_back(ReadFStringAt(view->data + i * 16, 0));
    return out;
}

bool WriteFStringArrayField(void* base, int32_t off, const std::vector<std::wstring>& in) {
    if (off < 0) return false;
    auto* view = reinterpret_cast<TArrayView*>(reinterpret_cast<uint8_t*>(base) + off);
    if (in.empty()) {
        FreeFStringArraySlot(view);
        return true;
    }
    const size_t bytes = in.size() * 16;
    uint8_t* buf = static_cast<uint8_t*>(R::EngineAlloc(bytes));
    if (!buf) return false;
    std::memset(buf, 0, bytes);
    for (size_t i = 0; i < in.size(); ++i) {
        if (!fstring_utils::MintFString(in[i], buf + i * 16)) {
            // Roll back the partial mint; the old array stays untouched.
            for (size_t j = 0; j < i; ++j) FreeFStringSlot(buf + j * 16);
            R::EngineFree(buf);
            return false;
        }
    }
    const TArrayView old = *view;
    view->data = buf;
    view->num = static_cast<int32_t>(in.size());
    view->max = static_cast<int32_t>(in.size());
    TArrayView oldCopy = old;
    FreeFStringArraySlot(&oldCopy);
    return true;
}

std::vector<int32_t> ReadInt32ArrayField(const void* base, int32_t off) {
    std::vector<int32_t> out;
    if (off < 0) return out;
    const auto* view = reinterpret_cast<const TArrayView*>(
        reinterpret_cast<const uint8_t*>(base) + off);
    if (!view->data || view->num <= 0 || view->num > 4096) return out;
    const int32_t* ints = reinterpret_cast<const int32_t*>(view->data);
    out.assign(ints, ints + view->num);
    return out;
}

bool WriteInt32ArrayField(void* base, int32_t off, const std::vector<int32_t>& in) {
    if (off < 0) return false;
    auto* view = reinterpret_cast<TArrayView*>(reinterpret_cast<uint8_t*>(base) + off);
    if (in.empty()) {
        if (view->data) R::EngineFree(view->data);
        view->data = nullptr; view->num = 0; view->max = 0;
        return true;
    }
    uint8_t* buf = static_cast<uint8_t*>(R::EngineAlloc(in.size() * 4));
    if (!buf) return false;
    std::memcpy(buf, in.data(), in.size() * 4);
    if (view->data) R::EngineFree(view->data);
    view->data = buf;
    view->num = static_cast<int32_t>(in.size());
    view->max = static_cast<int32_t>(in.size());
    return true;
}

}  // namespace ue_wrap::field_io
