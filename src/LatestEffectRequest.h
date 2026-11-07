#pragma once

#include <string>
#include <utility>

// Coalesces UI effect requests so the most recent request wins before output.
class LatestEffectRequest {
public:
    void submit(std::wstring effectId) { pending_ = std::move(effectId); }
    std::wstring take() {
        std::wstring effectId = std::move(pending_);
        pending_.clear();
        return effectId;
    }
    void clear() { pending_.clear(); }

private:
    std::wstring pending_;
};
