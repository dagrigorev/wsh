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
    commentary/subprocess_phi4_runtime.cpp
    commentary/fallback_commentary_provider.cpp
)

target_include_directories(wsh_ai PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/shell
    ${CMAKE_SOURCE_DIR}/src/platform
)

# Direct in-process Phi-4 runtime (requires vendored llama.cpp)
if(WSH_ENABLE_PHI4 AND llamacpp_POPULATED)
    target_sources(wsh_ai PRIVATE commentary/direct_phi4_runtime.cpp)
    target_compile_definitions(wsh_ai PRIVATE WSH_HAVE_LLAMA)
    target_link_libraries(wsh_ai PRIVATE llama)
    target_include_directories(wsh_ai PRIVATE ${llamacpp_SOURCE_DIR})
endif()
