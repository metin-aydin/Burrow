# Third-Party Notices

Burrow's own code is MIT licensed (see `LICENSE`). It builds on these projects:

## zDesk — Apple Screen Sharing (RFB 003.889) notes

The VNC client's Apple mode (`app/src/vnc/VncSession.cpp`: the 003.889 prelude, the encrypted
record layer, the display layout and Apple's input quirks) follows the wire format measured and
documented by zDesk (`docs/apple-vnc-889.md`, `src/vnc_apple.rs`, `src/vnc_record.rs`), and
`app/src/vnc/AppleAdaptive.cpp` is a C++ port of its Adaptive (0x3f3) decoder
(`frontend/wasm/apple-adaptive/src/decoder.rs`); the decoder tests in `app/tests/tst_vault.cpp` are
ported from the same file.

https://github.com/andrewtheguy/zdesk

```
MIT License

Copyright (c) 2026 Andrew Chen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
