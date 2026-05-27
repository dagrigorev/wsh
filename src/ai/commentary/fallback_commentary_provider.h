#pragma once
#ifndef WSH_FALLBACK_COMMENTARY_PROVIDER_H
#define WSH_FALLBACK_COMMENTARY_PROVIDER_H

#include <string>

namespace wsh {

/* Deterministic fallback commentary provider.
 * Uses simple rule-based matching to generate short playful lines.
 * No model file, no network, no inference — always instant. */
class FallbackCommentaryProvider {
public:
    FallbackCommentaryProvider();
    ~FallbackCommentaryProvider();

    /* Generate commentary for the given command.
     * Returns empty string if no matching rule. */
    std::string Generate(const std::string& command);

    /* Return true if this provider is ready (always true). */
    bool IsAvailable() const { return true; }
};

} /* namespace wsh */

#endif /* WSH_FALLBACK_COMMENTARY_PROVIDER_H */
