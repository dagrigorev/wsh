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
