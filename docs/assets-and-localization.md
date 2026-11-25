# IGDT Assets (`_ovg.bin`) & Multi-Language String Specification

Complete binary format documentation for all UI textures (`opt/gresfiles/`) and localized strings in `opt/hmi`, with extracted catalogs in [gresfiles_catalog_5624.json](decompiled/5624/hmi/gresfiles_catalog_5624.json) / [gresfiles_catalog_5606.json](decompiled/5624/hmi/gresfiles_catalog_5606.json), scene graphs in [resources/scenes/](../resources/scenes), and translation dictionaries in [resources/translations/](../resources/translations).

---

## 1. `_ovg.bin` Texture Descriptor Table

Inside `opt/hmi`, `PT_LOAD[0]` maps file offsets to virtual addresses via `va = file_offset + 0x8000`. The UI asset descriptor table sits in `.rodata`:
- **`5624_vw/opt/hmi`**: `VA 0x69b69c..0x7578ec` (`32,110` widget slots, **`2,806` unique `_ovg.bin` files**).
- **`5606_skoda/opt/hmi`**: `VA 0x6ca2f4` (`39,367` widget slots, **`2,611` unique `_ovg.bin` files**).

Each 24-byte struct (`<BBHhhIHHII`, decoded by `IGDT_LoadOvgTextureEntry` @ [`0x325728`](decompiled/5624/hmi/functions/0x325728.c)) contains:

| Offset | Size | Type | Meaning |
|---:|---:|---|---|
| `+0x00` | 1 | `uint8` | Pixel format (`FUN_00325728` GL format/type): `0x41` (`'A'`, 32,109 entries) / `0x40` (`'@'`, 1 entry): RGBA8888 (4 Bpp, `0x1908`/`0x1401`); `0x03`: RGB565 LE (2 Bpp, `0x1907`/`0x8363`); `0x44` (`'D'`): **RGBA5551 LE** (2 Bpp, `0x1908`/`0x8034`); `0x45` (`'E'`): **RGBA4444 LE** (2 Bpp, `0x1908`/`0x8033`); `0x0b`: A8 (1 Bpp, `0x1906`/`0x1401`) |
| `+0x01` | 1 | `uint8` | Flags (`flags & 0x7f`): `2` = PackBits RLE (inline in `FUN_00325728`), `0` = raw uncompressed pixels |
| `+0x02..+0x07` | 6 | `uint16/int16[2]` | Alignment / sub-rect metadata |
| `+0x08` | 4 | `uint32` | Compressed `_ovg.bin` file size (`bsz`) |
| `+0x0c` | 2 | `uint16` | Width (`w`) |
| `+0x0e` | 2 | `uint16` | Height (`h`) |
| `+0x10` | 4 | `uint32` | Tint ARGB (`0xff000000` in all 32,110 entries of `5624_vw`) |
| `+0x14` | 4 | `uint32` | VA pointer to ASCII path (`"gresfiles/..._ovg.bin"`, opened relative to cwd `/opt`) |

> **Important Deduplication Rule**: Multiple UI widget slots can point to the same `_ovg.bin` file with clipped or sub-panel dimensions (e.g., `10x20` vs `20x20` or `1x1`). Always select the entry with `max(w * h)` per unique path. Applying this rule decodes **100% (`2,806/2,806` in `5624`, `2,611/2,611` in `5606`, `bad=0`)** of all shipped `_ovg.bin` assets.

### PackBits RLE Stream (`flags & 0x7f == 2`, inline in [`0x325728`](decompiled/5624/hmi/functions/0x325728.c))
The `_ovg.bin` file holds **no header** — just raw compressed pixels, decoded inline inside `FUN_00325728` (note: `0x323dfc` is a scene-state updater, not an RLE helper):
- Tracks remaining stream budget `bsz` (decremented by `1 + payload_bytes` per item) and stops as soon as the output buffer (`w * h * bpp`) is full or `bsz == 0`.
- Read control byte `ctrl`:
  - **`ctrl & 0x80` set**: read 1 pixel (`bpp` bytes) and repeat it `(ctrl & 0x7f) + 1` times.
  - **`ctrl & 0x80` clear**: copy `(ctrl + 1) * bpp` literal bytes directly from the stream.
- **Boundary rules (`0x325728`)**: overlong runs/literals are clamped to remaining output capacity; a stream ending cleanly before filling `w * h * bpp` still succeeds (leaving tail pixels zeroed); EOF mid-item fails.

---

## 2. Compressed 5-Language String Block (`FUN_00023650` @ [`0x23650`](decompiled/5624/hmi/functions/0x23650.c))

`opt/hmi` packs all localized UI strings into a dense byte stream inside `.rodata`:
- **`5624_vw`**: file `0x3c3680` (`VA 0x3cb680`), max valid start id `0x20fcb` (`0x23650` rejects `id >= 0x20fcc`; the last 5-language group starts at `0x20fcb` and ends at `0x20fec`, the JSON `block_length`), `multilang_start = 0x02d2` (`\tUSEnglish`). IDs `0..8` are 9 empty chunks (neutral IDs start at `9`), yielding **`222` neutral + `1,976` 5-language entries** (`en_US`, `zh_CN`, `pt_BR`, `ru_RU`, `es_MX`), archived in [strings_5624_vw.json](../resources/translations/strings_5624_vw.json).
- **`5606_skoda`**: file `0x37bba0` (`VA 0x383ba0`), max start id `0x20c2c`, `multilang_start = 0x02cd`, yielding **`218` neutral + `1,960` 5-language entries**, where community translators hex-patched German strings into slot 2 (leaving minor offset drift on 4 entries `0xe05d`, `0xfa29`, `0x10073`, `0x129f7`), archived in [strings_5606_skoda.json](../resources/translations/strings_5606_skoda.json).

### Chunk Byte Format (`FUN_00023650`)
Starting at byte offset `id` inside the string block (for `id >= 0x2d2`, language slot `0..4` selects the `N`-th sequential string starting at `id`):
- `tag = b0 & 0x1f`, `cont = (b0 & 0x80) != 0`
- **If `tag == 0x1f`** (Backreference / Jump):
  - `target = (b1 << 8) | b2` (if `b0 & 0x20` is set, 24-bit `(target << 8) | b3`); decoded **one level deep with language `0`** ([`0x31d1bc`](decompiled/5624/hmi/functions/0x31d1bc.c)) — no backref chains exist in the block.
- **Else** (`tag` chunk length):
  - `mode = (b0 >> 5) & 3`:
    - `mode == 0`: Unicode page `0x00` (ASCII / Latin-1), `cnt = tag` bytes.
    - `mode == 1`: Unicode page `0x01` (Latin Extended-A), `cnt = tag` bytes.
    - `mode == 2`: Unicode page `0x04` (Cyrillic `U+0400..04FF`), `cnt = tag` bytes.
    - `mode == 3`: Arbitrary BMP page (`page = b1`, `cnt = tag - 1` bytes, emitting `chr((page << 8) | b)` — used for CJK `zh_CN`).
- Repeat while `cont` (`b0 & 0x80`) is set.

A second uncompressed **UTF-32LE engineering string table** (`109` entries at file `0x89cc00..0x8a3874`, `VA 0x8a4c00..0x8ab874` in `5624`) holds Engineering Testmode labels (`Audio Framework Service`, `Restore all to factory settings?`, etc.).

> **How Scene Tables Link Strings (`FUN_0031f41c`)**: IGDT scene tables carry **no** multilang string IDs. Kind-4 text nodes interpret `text_layout[eidx] + 0` as a wchar offset directly into the UTF-32 engineering table (`0x8a4c00`), whereas kind-5 text nodes read a runtime wchar buffer (`0x00c62264`, 129-wchar slots) populated at runtime by page code decoding IDs from the `0x23650` block.

---

## 3. Extracted Resource & Translation Archives

- **Per-language JSON dictionaries**:
  - [en_US.json](../resources/translations/en_US.json)
  - [de_DE.json](../resources/translations/de_DE.json)
  - [zh_CN.json](../resources/translations/zh_CN.json)
  - [pt_BR.json](../resources/translations/pt_BR.json)
  - [ru_RU.json](../resources/translations/ru_RU.json)
  - [es_MX.json](../resources/translations/es_MX.json)
- **Full string table dumps**:
  - [strings_5624_vw.json](../resources/translations/strings_5624_vw.json)
  - [strings_5606_skoda.json](../resources/translations/strings_5606_skoda.json)
- **IGDT scene-graph & texture catalogs**:
  - [scenes_5624_vw.json](../resources/scenes/scenes_5624_vw.json) & [scenes_5606_skoda.json](../resources/scenes/scenes_5606_skoda.json)
    - *Known extractor defects in `scenes_5624_vw.json`*: the earlier JSON extractor (1) indexed `branch_init` by `tag & 0xfffff` instead of node `eidx` (affecting screens `0x17` and `0x52`), (2) capped recursion at depth 12 (dropping elements on `0x0a`, `0x0d`, `0x58`, `0x60`, `0x63`, `0xa5`, `0xfe`), and (3) skipped static kind-7 rect nodes. For stock-faithful scene generation, use [src/hmi/tools/gen_igdt_tables.py](../src/hmi/tools/gen_igdt_tables.py) (pinned against these exact exceptions in [tools/tests/test_hmi_tables.py](../tools/tests/test_hmi_tables.py)).
  - [gresfiles_catalog_5624.json](decompiled/5624/hmi/gresfiles_catalog_5624.json) & [gresfiles_catalog_5606.json](decompiled/5624/hmi/gresfiles_catalog_5606.json)
