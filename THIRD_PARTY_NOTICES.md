# Third-party notices

The Windows release contains or downloads the following third-party works.
Their licenses are compatible with this GPL-2.0-or-later distribution, but
continue to apply to those works independently.

| Component | Version / artifact | Use | License |
|---|---|---|---|
| [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | 1.13.4 | Packaged local ASR runtime | Apache License 2.0 (`licenses/Apache-2.0.txt`) |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | 1.27.0 | Packaged inference runtime used by sherpa-onnx | MIT (`licenses/MIT-onnxruntime.txt`) |
| [T-One](https://huggingface.co/t-tech/T-one) | streaming Russian CTC, 2025-09-08 | Downloaded and checksum-verified by the installer; not bundled in the ZIP | Apache License 2.0; copyright 2025 T-Software DC |
| [Nemotron 3.5 ASR Streaming](https://huggingface.co/nvidia/nemotron-3.5-asr-streaming-0.6b) | multilingual streaming ASR, 560 ms int8 export, 2026-06-11 | Downloaded and checksum-verified by the installer; not bundled in the ZIP | NVIDIA Open Model Derivative Work License 1.1 |
| [Geologica](https://github.com/googlefonts/geologica) | variable font | Embedded browser-caption font | SIL Open Font License 1.1 (`licenses/Geologica-OFL.txt`) |

OBS Studio and Qt are build/runtime dependencies supplied by OBS and are not
redistributed in this plugin archive. Their own licenses remain applicable.

Source and binary download locations, versions, and SHA-256 checksums are
pinned in `buildspec.json`, `cmake/common/FindSherpaOnnx.cmake`, and
`installer/local-model.json`.
