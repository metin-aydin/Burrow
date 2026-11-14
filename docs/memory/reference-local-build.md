---
name: reference-local-build
description: Kullanıcının Mac'inde derleme için gereken CMake bayrakları
type: reference
---

Kullanıcının Mac'inde (macOS 27, Xcode SDK 26.2) `/Library/Developer/CommandLineTools` SDK'sı ile
linkleme "tapi error: unknown architecture" veriyor. Xcode SDK'sı ve clang açıkça verilmeli:

```bash
export PATH=/opt/homebrew/bin:$PATH
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
  -DCMAKE_C_COMPILER="$(xcrun -f clang)" -DCMAKE_CXX_COMPILER="$(xcrun -f clang++)" \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix freerdp)"
cmake --build build
```

- `go` PATH'te ServBay alias'ı olarak görünüyor ama çalışmıyor; `/opt/homebrew/bin/go` kullan.
- Homebrew FreeRDP dylib'leri macOS 27.2 için derlenmiş → lokal linkte uyarı verir, zararsız.
  Dağıtım CI'da yapılır.
- openconnect 9.12 bu Mac'te zaten kurulu (`/opt/homebrew/opt/openconnect`), GP ön testi için hazır.
