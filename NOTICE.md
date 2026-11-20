# Notice

This is a **modified version** of the source code of Command & Conquer Generals and Command & Conquer Generals
Zero Hour that Electronic Arts Inc. released under the GNU General Public License, version 3, with additional
terms (see [LICENSE.md](LICENSE.md)). It is not the original program, and it is not affiliated with, endorsed by
or sponsored by Electronic Arts Inc.

## Where the code comes from

| Part | Who | Licence |
|:--|:--|:--|
| The released source code (commit `49843ea4`, 2025-02-27) | Electronic Arts Inc. | GPL-3.0-or-later, with EA's additional terms |
| The upstream fork, CnCGeneralsZH-Reforged (2026) | Olcay Seygan | GPL-3.0-or-later, as a modification of EA's release |
| The macOS and Linux port on this branch (2026) | İlyas Akın | GPL-3.0-or-later, as a modification of the above |

The additional terms in LICENSE.md apply to the whole program, including every modification.

## How files are marked

- **A file Electronic Arts released that this port changed** keeps EA's copyright header exactly as released, and
  carries a line directly under it: `Modified 2026 by İlyas Akın for the macOS/Linux port`. The changes
  themselves, with dates, are in the git history.
- **A file this port added** carries `Copyright 2026 İlyas Akın` and the GPL-3.0-or-later notice.
- **A file this port added that holds code taken from an Electronic Arts file** keeps EA's header and names the
  EA file it came from.
- **A file this port added that adapts code from the upstream fork** credits the upstream file and its author.
- A few legacy files that are not UTF-8 spell the name `Ilyas Akin`.
- `GeneralsMD/Code/Tools/license-headers.py --check` verifies all of the above.

Files that upstream added or changed carry upstream's own notices, which this port does not alter.

## Trademarks

"Command & Conquer" and every other Electronic Arts trademark belong to Electronic Arts Inc. The licence grants no
right to use them (LICENSE.md, additional terms). They appear here only to say where the code comes from.

## Third-party components

- **Not in this repository.** Third-party libraries are fetched at pinned versions by `GeneralsMD/Code/Tools/vendor.sh`
  (and `vendor.ps1` on Windows), and each stays under its own licence.
- **In the macOS app.** The app bundle carries those licences in `Contents/Resources/Licenses`.
- **Marked changes.** The port's patches to third-party code are in `GeneralsMD/Code/Libraries/Source/*.patch`, and
  their added lines are marked, and hunks backported from upstream SDL are named in each patch's header.
- **FreeType** is used under the GNU GPL (version 2 or later) option of its dual licence.
- **FFmpeg** is built under LGPL-2.1-or-later only: its build scripts refuse GPL or non-free configurations.
- **mbedTLS**, which carries the online service's TLS, is used under the Apache License 2.0 option of its dual
  licence (Apache-2.0 or GPL-2.0-or-later).

## Game data

No game assets are in this repository. Running the game needs an installed, legitimately obtained copy of
Command & Conquer Generals Zero Hour.
