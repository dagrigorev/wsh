# Third-party notices

## Ghostty

Wisp's design follows [Ghostty](https://github.com/ghostty-org/ghostty) closely,
and the project is working through a file-by-file port of Ghostty's modules —
see [`docs/GHOSTTY_PORT_LEDGER.md`](docs/GHOSTTY_PORT_LEDGER.md) for the work
list and current status.

Ghostty is distributed under the MIT License, which permits derivative works
provided the copyright notice and permission notice are retained. Any Wisp
source file that is a port, translation or close derivation of Ghostty source
must carry a header saying so and naming the upstream file, for example:

```c
/* Ported from Ghostty src/terminal/PageList.zig
 * Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors
 * MIT License — see THIRD_PARTY_NOTICES.md
 */
```

**Current status:** as of this commit no Ghostty source has been incorporated
into Wisp. The resemblance so far is architectural — hosting a shell over a
pseudo-terminal, minimal chrome, GPU rendering — which is a design approach,
not copyrightable expression, and needs no license grant. This notice is in
place ahead of the port so the obligation is met from the first ported line
rather than retrofitted afterwards.

Ghostty's upstream `LICENSE` file carries the notice:

> Copyright (c) 2024 Mitchell Hashimoto, Ghostty contributors

The full MIT License text is reproduced in [`LICENSE`](LICENSE) below Wisp's
own notice; the two are the same license, and it covers both.

Wisp is not affiliated with, endorsed by, or an official product of the
Ghostty project. "Ghostty" is the name of that project and is used here only
to describe Wisp's lineage.

## X11 color names (rgb.txt)

`src/terminal/res/x11_rgb_data.inc` embeds `rgb.txt` from the X.Org
project (https://gitlab.freedesktop.org/xorg/app/rgb), by way of Ghostty,
which ships the same file as `src/terminal/res/rgb.txt`. It is used by
`color.hpp` to resolve X11 color names such as "ForestGreen". The file is
distributed under the MIT/X11 license.

## Zig standard library

`src/zigstd/` transliterates parts of the Zig standard library (0.16.0) that
Ghostty depends on for observable behavior: `std.hash.Wyhash` (hash map
placement) and `std.Random` with `Xoshiro256`/`SplitMix64` (the seeded
generators upstream's tests draw from). https://codeberg.org/ziglang/zig

The MIT License (Expat)

Copyright (c) Zig contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
