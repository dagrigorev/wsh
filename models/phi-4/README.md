# Phi-4 Local Model for Wsh AI Commentary

This directory contains the bundled Phi-4 GGUF model used by Wsh's AI
commentary feature.

## Model Format

- File: `model.gguf` (GGUF format)
- Source: Microsoft Phi-4 (14B / miniaturized variant)
- Quantization: Q4_K_M or Q5_K_M recommended for balance of quality and size

## Setup

### Option A: CMake auto-download (recommended)

When configuring the build, enable auto-download:

    cmake -S . -B build -DWSH_DOWNLOAD_PHI4_MODEL=ON

The model file is downloaded to `build/dist/models/phi-4/model.gguf`
during the build. A custom model URL can be set via `-DWSH_PHI4_MODEL_URL=...`.

### Option B: Manual download

Place the GGUF model file here:

    models/phi-4/model.gguf

Default download URL:
<https://huggingface.co/bartowski/phi-4-GGUF/resolve/main/phi-4-Q4_K_M.gguf>

Smaller quantizations (faster download, lower quality):
<https://huggingface.co/bartowski/phi-4-GGUF/resolve/main/phi-4-Q2_K.gguf>
<https://huggingface.co/bartowski/phi-4-GGUF/resolve/main/phi-4-IQ2_M.gguf>

### Detection

Wsh will auto-detect the model at startup. If the model is missing, Wsh will
fall back to deterministic local commentary (if enabled) or silently
disable AI commentary.

## Model Path Resolution

Runtime resolves model path in this order:

1. Config: `ai.phi4.model_path` in Wsh.toml
2. Environment variable: `WSH_PHI4_MODEL_PATH`
3. Relative to executable: `./models/phi-4/model.gguf`
4. Relative to executable: `../models/phi-4/model.gguf`

## Requirements

- Windows 10+
- 8 GB+ RAM (system RAM, not VRAM — Phi-4 runs on CPU via llama.cpp)
- 4 GB+ available disk space for the model file

## Notes

- No cloud APIs required.
- No Ollama, LM Studio, or external servers.
- All inference runs locally on your machine.
