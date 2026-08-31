# qMdict

A small, fast, offline reader for MDict dictionaries (`.mdx` / `.mdd`), written in C++ with Qt 6.

Point it at a folder, and every dictionary underneath it — at any depth — becomes searchable
from a single box. Nothing is uploaded, nothing is installed, and a 1 GB dictionary costs a
few tens of megabytes of RAM rather than a gigabyte.

## Features

- **Offline lookup across a whole folder tree.** `.mdx` files are discovered recursively, and
  each one is automatically paired with its `.mdd` resource archives (`foo.mdd`, `foo.1.mdd`, …)
  for images, stylesheets and audio.
- **Dark and light themes on both Windows and Linux.** qMdict applies its own Fusion-based
  palette instead of relying on the desktop, so the two platforms look identical. "Follow System"
  is the default.
- **Pronunciation that just plays.** Speex (`.spx`), MP3 and WAV clips are decoded in-process
  and played directly, with no "open with" prompt and no media framework to install.
- **Adjustable text size.** `Ctrl` and `+`/`-` resize both the article and the word list; the
  setting is remembered. Double-clicking any word in an article looks it up.
- **Low memory and fast startup.** Only the headword index lives in RAM; articles are inflated
  on demand into a small LRU cache. The index is cached on disk after the first open, so
  subsequent launches are effectively instant.
- **Runs out of the box.** The release zips carry their own Qt runtime. Unzip, run, done.

## Getting it

Download the zip for your platform from the
[latest release](../../releases/latest) and unpack it anywhere.

| Platform | Run |
| --- | --- |
| Linux | `./qMdict` |
| Windows | `qMdict.exe`, or `qMdict-portable.cmd` to keep settings inside the folder |

Then use **File → Open Dictionary Folder** and select the folder holding your dictionaries.

In portable mode, settings and the headword index cache are written to the `data/` folder
inside the unpacked directory and nowhere else; delete the folder and no trace remains.

## Using it

| Action | How |
| --- | --- |
| Search | Type in the box; results update as you type |
| Move through results | Up/Down arrows work while the cursor stays in the search box |
| Look up a word you are reading | Double-click it in an article |
| Follow a cross-reference | Click any link in an article |
| Back / Forward | `Alt+Left` / `Alt+Right` |
| Jump to the search box | `Ctrl+L` |
| Larger / smaller text | `Ctrl++` / `Ctrl+-`, reset with `Ctrl+0` (scales the word list too) |
| Hide or show the menu bar | `Ctrl+M` |
| Hear a pronunciation | Click the speaker link in an article |
| Enable or disable dictionaries | **View → Dictionaries…** |
| Switch theme | **View → Theme** |

If a dictionary's own stylesheet clashes with dark mode, turn off
**View → Use Dictionary Styles** to fall back to the theme's colours.

## Format support

| | Supported |
| --- | --- |
| MDX / MDD version | 1.2 and 2.0 |
| Block compression | stored, LZO1X, zlib |
| Text encodings | UTF-8, UTF-16, Latin-1, GBK, GB18030, BIG5, Shift-JIS, EUC-JP, EUC-KR, KOI8-R, Windows code pages |
| Key index obfuscation | Yes (`Encrypted="2"`) |
| Purchased/registered encryption | No — these need a per-user key (`Encrypted="1"`) |
| `@@@LINK=` alias entries | Yes, followed transparently |
| Header `StyleSheet` substitutions | Yes |
| Audio clips | Speex (`.spx`), MP3, WAV — decoded and played in-process |
| Ogg Vorbis / Opus audio | Recognised but not decoded |

Audio format is detected from the file's contents rather than its extension, because
dictionaries are inconsistent about naming. On Linux playback is handed to whichever of
`pw-play`, `paplay`, `aplay`, `ffplay` or `play` is installed; on Windows it goes straight to
the system mixer from memory.

Articles are rendered with `QTextBrowser`, which supports a practical subset of HTML and CSS
but not JavaScript. This is a deliberate trade: a web engine would render a handful of exotic
dictionaries more faithfully at the cost of roughly 150 MB of RAM and 120 MB of download.

## Building from source

The only external dependency is Qt 6.2 or newer (Core, Gui, Widgets). zlib arrives via Qt; the
LZO and RIPEMD-128 codecs MDict needs are implemented in `src/util`; and the two audio decoders
are vendored in `third_party/` (see below), so there is nothing else to install.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build -j$(nproc)
./build/qmdict_tests    # 91 checks over generated MDX/MDD fixtures
./build/qMdict
```

On Debian/Ubuntu the system Qt works too:

```bash
sudo apt install qt6-base-dev cmake ninja-build
```

### Producing the release zips

```bash
# Linux, on Linux
packaging/package-linux.sh /path/to/Qt/6.8.3/gcc_64

# Windows, on Windows (MSVC)
packaging\package-windows.ps1 -QtPrefix C:\Qt\6.8.3\msvc2022_64

# Windows, cross-compiled from Linux (MinGW-w64 + Qt's win64_mingw package)
packaging/package-windows-cross.sh \
    --qt-windows /path/to/Qt/6.8.3/mingw_64 \
    --qt-host    /path/to/Qt/6.8.3/gcc_64
```

Each script builds, bundles the Qt runtime, and zips the result. The native scripts also run
the test suite; the cross script instead walks the import tables of every bundled binary and
fails if any DLL is neither included nor supplied by Windows, since it cannot execute the
result on the build machine.

`.github/workflows/build.yml` builds both platforms natively on every push — including running
the tests on Windows — and attaches the zips to tagged releases.

## How it works

```
src/util/       ripemd128, lzo1x, block codec, legacy text decoding
src/mdict/      mdictfile (container parsing + index), dictionary (mdx+mdd), library (folder scan)
src/audio/      ogg demuxer, speex/mp3/wav decoding, playback
src/ui/         mainwindow, articleview (QTextBrowser + .mdd resources), theme, dark-mode colours
third_party/    speex (decoder only) and minimp3
resources/      generated icon set and the Windows .ico
tools/          make_icon.cpp, which draws that icon set
tests/          fixture generator and checks for all of the above
```

The icon is drawn in code rather than kept as opaque binary artwork, so it can be adjusted
without a graphics editor. After editing `tools/make_icon.cpp`:

```bash
cmake -S . -B build -DQMDICT_BUILD_ICON_TOOL=ON && cmake --build build
./build/qmdict_make_icon resources
```

Opening a dictionary reads its header, key index and record-block table, then stops. Headwords
are stored as one contiguous UTF-8 blob plus a compact offset table, which is roughly 25 bytes
per entry — about 25 MB for a million headwords, versus several hundred megabytes if the
articles were held in memory too. Lookups binary-search a case-folded ordering, seek to the one
compressed block that holds the record, and inflate just that block.

That whole index is then written to a cache file keyed on the dictionary's size and timestamp,
so reopening skips the parse entirely.

The parser is written to be safe against malformed input: every length read from a file is
validated against the real file size before it is used to allocate, and the reader has been
fuzzed with tens of thousands of truncated and bit-flipped dictionaries under
AddressSanitizer and UndefinedBehaviorSanitizer.

### Dictionary styling

A dictionary's stylesheet decides almost everything about how its entries read, so qMdict goes
to some length to honour it.

It is located from the `<link rel="stylesheet">` tag in the article, looking first inside the
`.mdd` archives and then for a plain file beside the `.mdx` — both are common, and dictionaries
that keep it loose on disk would otherwise render as one unbroken paragraph.

That paragraph problem has a second cause worth knowing about. `QTextDocument` decides block
versus inline layout from the element name alone: a `display: block` on a `<span>` is ignored
however it is delivered. Dictionaries such as Oxford build an entire entry out of styled
`<span>`s, so qMdict reads which classes the stylesheet lays out as blocks and rewrites exactly
those spans as `<div>`s before rendering.

Finally, dictionaries assume a white page, so in dark mode their colours are remapped: text is
raised to a readable lightness with its hue intact, and panels meant to be pale are darkened.
Only stylesheet declarations and attribute values are touched, never article prose.

If you would rather have plain theme colours and layout, turn off
**View → Use Dictionary Styles**.

## Third-party code

| Component | Licence | Why |
| --- | --- | --- |
| [Speex](https://www.speex.org/) 1.2.1, decoder only | BSD 3-clause | `.spx` pronunciation clips, which no desktop plays natively |
| [minimp3](https://github.com/lieff/minimp3) | CC0 | `.mp3` pronunciation clips |

Both are compiled straight into the executable and together add about 40 KB. Bundling them was
the cheap alternative to a media framework, which would have added roughly 60 MB to the
download for the same result.

## Licence

qMdict is released under the MIT licence; see [LICENSE](LICENSE). The vendored decoders keep
their own licences as listed above.

## A note on Qt

The release zips include Qt 6 shared libraries, which are used under the LGPL v3. They are
dynamically linked and shipped as separate files in `lib/`, so they can be replaced with a
compatible build of Qt. Qt's source is available from [qt.io](https://www.qt.io/download-open-source).
