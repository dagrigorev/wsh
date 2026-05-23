# ── wsh_ai ──────────────────────────────────────────────────────────────────
# Local rule-based AI assistant module (C++).
add_library(wsh_ai STATIC
    wsh_ai.cpp
    providers/rule_based_ai_provider.cpp
    providers/ngram_model.cpp
    providers/ngram_ai_provider.cpp
)

target_include_directories(wsh_ai PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/shell
)
