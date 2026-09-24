# c64-git

A proof-of-concept Git client for the Commodore 64, built with
[cc65](https://cc65.github.io/). It reads a git object store from a
1541-friendly container file, inflates zlib-compressed loose objects,
parses commits and trees, verifies SHA-1, and prints a `git log` /
`ls-tree` / `show`-style report on the C64 screen — all on stock
64 KB of RAM.

The same report code runs identically on the host and on the C64;
the test suite diffs the two outputs byte-for-byte under VICE.

## What works today

- `git64tool mkpak <gitdir> <out.pak>` — walk a real `.git/objects`
  loose-object store, SHA-1-verify every object against its filename,
  and pack them into `GITREPO.PAK` (sorted index + raw zlib streams).
- `git64tool report <pak>` — produce the "golden" report on the host.
- `git64tool cat <pak> <hexsha>` — dump a single object.
- `GIT64.PRG` — on the C64, opens `GITREPO.PAK` on device 8 as a SEQ
  file, then prints:
  - HEAD ref + short SHA, object count
  - `git log` — up to 4 commits: sha, author line, subject
  - `git ls-tree` of the HEAD tree (mode, name, sha)
  - `git show` — a text preview of the first blob
  - `SHA1-OK n FAIL n` — every visited object re-hashed on the 6502
- Real zlib inflate (Mark Adler's `puff.c`, vendored) and a SHA-1
  implementation, both running on the 6502.

## Build

Requirements: `cc65` (`cl65`), a C compiler + zlib dev headers for the
host tool; `vice` (`x64sc`, `c1541`) for the emulator test.

```sh
make            # builds build/git64tool and build/GIT64.PRG
make test-host  # host-only validation (no emulator needed)
make test       # full PoC: fixture repo -> pak -> host report ->
                # d64 -> x64sc run -> golden diff
```

Debian/Ubuntu: `apt install cc65 vice zlib1g-dev`. Note Debian's VICE
package ships **no ROMs** — copy `C64/` and `DRIVES/` ROM data from a
VICE source tarball into `~/.local/share/vice/` or the emulator hangs
at `SEARCHING FOR *`.

## Run it on a C64 / emulator

```sh
./build/git64tool mkpak /path/to/repo/.git gitrepo.pak

c1541 -format c64git,01 d64 c64git.d64
c1541 -attach c64git.d64 -write build/GIT64.PRG git64
c1541 -attach c64git.d64 -write gitrepo.pak "gitrepo.pak,s"

x64sc -autostart c64git.d64        # or on hardware: LOAD"GIT64",8 + RUN
```

On a 1541 the pak travels as one sequential file — exactly what the
PoC reads. A real drive reads it at ~400 bytes/s; larger repos are a
patience exercise.

## How it fits in 64 KB

- The pak index (up to 64 objects) is loaded once into a static
  ~2 KB table; objects are inflated into two static 12 KB buffers
  (compressed + decompressed). Stack is tiny on the 6502, so hot
  functions use `static` locals instead.
- Sequential reads only: `read_at(offset)` rewinds by closing and
  reopening the file, then skips forward — the access pattern the
  1541 actually supports. (On real hardware you'd use U1 block reads
  against a BAM of file extents instead — see Next steps.)
- `puff.c` (zlib's reference inflate) was minimally altered for cc65:
  large locals moved to `static` storage. SHA-1 is a byte-at-a-time
  one-shot implementation; no 64-bit types.

## GITREPO.PAK format

```
offset  size  field
0       8     magic "C64GITPK"
8       1     version (1)
9       1     flags (reserved, 0)
10      2     object count (<= 64)
12      20    HEAD sha1 (binary)
32      24    HEAD ref name, zero-padded (e.g. refs/heads/master)
56      4     index offset (absolute)
-- index, 32 bytes per entry, sorted by sha --
+0      20    object sha1 (binary)
+20     4     data offset (absolute)
+24     4     compressed length
+28     2     inflated length
+30     1     type: 1=blob 2=tree 3=commit 4=tag
+31     1     pad
-- raw zlib streams follow --
```

Why a container at all: a real `.git/objects` tree is thousands of
2-letter directories + files — hostile to a drive that lists the
directory sector-by-sector and has no seek. One file + one index is
the smallest thing that stays honest about the transport problem.

## What is simulated vs real

**Real, running on a 6502:** zlib inflate, SHA-1, commit/tree parsing,
the whole report flow, cbm (KERNAL) file I/O against a d64 image under
true drive emulation (VICE `x64sc` with drive ROMs, TDE enabled).

**Simulated / host-side:** `mkpak` (object discovery + packing) runs
on the host — it needs a filesystem walk and real zlib; that's the
"fetch/pack service" role a server or Ultimate-64-style coprocessor
would play. VICE is an emulator, not silicon: timing and IEC edge
cases differ on real hardware.

## Constraints encountered (worth knowing)

- **cc65 charset**: cbm targets compile C literals to PETSCII — which
  silently breaks byte-exact compares against on-disk data (pak magic,
  `"commit "` headers, filenames — CBM DOS does no case folding) and
  makes emitted text non-ASCII. `src/ascii_charmap.h` identity-maps
  the charset; emitted bytes are then ASCII, which the C64's
  lower-case charset displays correctly.
- **cc65 stack/limits**: only a handful of locals per function
  (`static` everywhere), no compound literals, and ~2 KB of stack —
  hence the big static buffers.
- **16-char filenames**: `GITREPO.PAK` fits; git object names do not —
  another reason for the container.
- **VICE remote monitor**: connecting halts the CPU, `x` resumes, and
  replies lag while it runs — `tests/vicemon.py` holds one persistent
  connection and searches the cumulative stream for the `$9FFE` done
  marker before dumping the `$8000` capture buffer.

## Transport paths this PoC side-steps (and how they'd slot in)

The pak is deliberately transport-agnostic — "how objects reach the
disk" is the open design space:

- **Serial/UserPort**: a PC pushes the pak over a userport↔RS-232 or
  IEC-aware link (e.g. ZoomFloppy in reverse / a simple X-modem-ish
  protocol); the client just `OPEN`s it.
- **Ethernet carts (RR-Net / TFE / 64NIC+)**: HTTP fetch of a pak from
  a LAN git bridge straight to disk — the natural way to do `clone`.
- **Ultimate 64 / 1541 Ultimate**: its REST API or USB storage makes
  the pak appear on a mounted disk image — no cable protocol needed.
- **WiFi modems (WiC64 etc.)**: same story — fetch the container, then
  everything else in this repo is unchanged.

`clone/fetch` is one download of a pak; `push` would need the host
bridge to rebuild objects — a protocol decision, not a 6502 problem.

## Limits (current PoC)

- Max 64 objects / 12 KB per object (compressed and inflated).
- History walk capped at 4 commits; trees at 16 entries.
- Loose objects only — no packfiles, refs beyond HEAD, or index.
- No clone/fetch/push — the pak arrives by disk-swap magic today.
- Sequential-read I/O is O(file) per rewind; fine for a PoC, not for
  a big repo.

## Next steps

1. U1/direct-sector reads with an extent table instead of
   rewind-by-reopen; raise object count to a few hundred.
2. Packfile (`.pack`/`.idx`) reading — the real git wire format —
   using the same read_at abstraction plus delta resolution.
3. A serial or RR-Net fetch path so `clone` works without a host
   sidecar; then `fetch` as "pak diff from bridge".
4. Interactive UI: pick a commit, browse trees, page blob text.
5. Real-hardware pass on a 1541/Ultimate-64.

## Layout

```
src/pak.[ch]        pak container format + reader (read_at abstraction)
src/gitobj.[ch]     git object header / commit / tree parsing
src/sha1.[ch]       SHA-1 (one-shot, 6502-safe)
src/puff.[ch]       vendored zlib inflate (altered for cc65)
src/report.[ch]     the report flow shared by host tool and C64 app
src/ascii_charmap.h cc65 pragma: identity charmap (ASCII literals)
src/host/git64tool.c  host CLI: mkpak / report / cat
src/c64/git64.c     the C64 program (cbm I/O, screen + RAM capture)
tests/run_tests.sh  fixture -> pak -> host golden -> VICE diff
tests/vicemon.py    VICE remote-monitor driver for the C64 test
```

`puff.c` is from zlib contrib (`puff.c` by Mark Adler, zlib license);
see the header comment for the cc65 alterations.
