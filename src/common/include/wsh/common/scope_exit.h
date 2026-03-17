#pragma once

#include <utility>

namespace wsh::common
{
template <typename TFn>
class ScopeExit
{
public:
    explicit ScopeExit(TFn fn) : fn_(std::move(fn)) {}
    ~ScopeExit() { fn_(); }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    TFn fn_;
};
} // namespace wsh::common
