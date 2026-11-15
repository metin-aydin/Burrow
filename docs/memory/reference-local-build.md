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

- cgo'lu Go paketlerini (`internal/gp`) test ederken de Xcode SDK'sı gerekir:
  `SDKROOT=$(xcrun --sdk macosx --show-sdk-path) CC=$(xcrun -f clang) go test ./...`
- `go` PATH'te ServBay alias'ı olarak görünüyor ama çalışmıyor; `/opt/homebrew/bin/go` kullan.
- Homebrew FreeRDP dylib'leri macOS 27.2 için derlenmiş → lokal linkte uyarı verir, zararsız.
  Dağıtım CI'da yapılır.
- openconnect 9.12 bu Mac'te zaten kurulu (`/opt/homebrew/opt/openconnect`), GP ön testi için hazır.

## Linux (Docker ile, bu Mac'te)

- Ubuntu 24.04 konteynerinde `sh tools/linux-deps.sh /opt/burrow-deps` (Qt 6.10.1 + FreeRDP 3.33.1 kurar),
  Go'yu go.dev'den kur, sonra `-DCMAKE_PREFIX_PATH="/opt/burrow-deps/qt;/opt/burrow-deps/freerdp"` ile derle;
  `tools/package-appimage.sh <build> <sürüm> /opt/burrow-deps` AppImage üretir.
- Docker belleği sınırlı: derlemede `-j2` kullan, yoksa `cc1plus` öldürülür.
- Konteynerde FUSE yok: AppImage'ı `--appimage-extract-and-run` ile çalıştır; ekran için `Xvfb :99` + `xwd`.
- `QT_QPA_PLATFORM=offscreen` macOS'ta bu uygulamayı çökertiyor (139); Linux'ta sorunsuz.

## Windows

- Bu Mac'te Windows derlemesi yok (mingw/zig kurulu değil); Windows işi yalnız CI'da (`Windows build`, windows-2025 +
  MSYS2 UCRT64) doğrulanıyor. Yerelde Go tarafı için yalnız `GOOS=windows CGO_ENABLED=0 go vet` denenebilir.
- Windows bilgisayarda: MSYS2 UCRT64 kabuğunda `tools/windows-deps.sh C:/burrow-deps`, sonra
  `PKG_CONFIG_PATH=/c/burrow-deps/lib/pkgconfig cmake -S . -B build -G Ninja`; çalıştırırken `C:/burrow-deps/bin` PATH'te.
- libopenconnect 9.21 kaynaktan, `tools/windows/openconnect-pipe-tun.patch` ile (pipe'a TAP ioctl'ü gönderme).
