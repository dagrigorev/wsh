#pragma once

#include <optional>
#include <utility>

#include "wsh/common/error.h"

namespace wsh::common
{
template <typename T>
class Result
{
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error error) : error_(std::move(error)) {}

    [[nodiscard]] bool HasValue() const noexcept { return value_.has_value(); }
    [[nodiscard]] const T& Value() const { return *value_; }
    [[nodiscard]] T& Value() { return *value_; }
    [[nodiscard]] const Error& GetError() const { return *error_; }

private:
    std::optional<T> value_;
    std::optional<Error> error_;
};

template <>
class Result<void>
{
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}

    [[nodiscard]] bool HasValue() const noexcept { return !error_.has_value(); }
    [[nodiscard]] const Error& GetError() const { return *error_; }

private:
    std::optional<Error> error_;
};
} // namespace wsh::common
