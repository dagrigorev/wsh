# ── wsh_ai ──────────────────────────────────────────────────────────────────
# Local rule-based AI assistant module (C++).
add_library(wsh_ai STATIC
    wsh_ai.cpp
    providers/rule_based_ai_provider.cpp
    providers/ngram_model.cpp
    providers/ngram_ai_provider.cpp
    providers/tiny_llm_provider.cpp
    inference/tiny_reasoning_model.cpp
    commentary/phi4_commentary_provider.cpp
    commentary/phi4_config.cpp
    commentary/phi4_prompt_builder.cpp
    commentary/phi4_runtime.cpp
    commentary/fallback_commentary_provider.cpp
)

target_include_directories(wsh_ai PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/shell
    ${CMAKE_SOURCE_DIR}/src/platform
)
