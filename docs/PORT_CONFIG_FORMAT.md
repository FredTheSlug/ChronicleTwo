# Native configuration assets

The PC port reads every retail configuration (CFG) and map (MAP) script as a native semantic JSON document published by the Rust extractor. A standalone script keeps its path with `.cfg.json` or `.map.json` in place of its suffix (`menu/cfg7/wepdat.cfg.json`, `map/s/s01/s01.map.json`), so a same-stem texture document keeps its `.json` name; a PACK member lives in the PACK's asset directory (`menu/1/shop.pac/shop_com.cfg.json`). The original scripts and PACK members are removed from `/data`. There is no configuration catalog or manifest.

Every document is an object with the schema version, its family's kind, and that family's named fields:

```json
{
  "schema_version": 2,
  "kind": "shop_lists",
  "shops": [{"id": 1, "items": [10, 11, 12]}],
  "prices": [{"item": 10, "buy": 100, "sell": 50}]
}
```

Fields hold the values the game reads, typed as the game reads them: integers are signed 32-bit, numbers read as floats become native floats, and strings are readable UTF-8 that the port encodes as CP932 for the game's fonts and identifiers. A string that UTF-8 cannot represent is `{"text": "...", "game_bytes": "<hexadecimal CP932>"}`. `null` is a string operand the original script left missing. A key that is absent is an optional operand the original script left out; the port then does what the retail callback does without it. Values the game derives at load (radians from degrees, colour scaling, the PAL screen stretch, font conversion, defaults) are not stored. Ordered families keep their order in arrays; nothing is a list of commands. [NATIVE_CONFIGS.md](NATIVE_CONFIGS.md) gives every kind's fields and rules and how the extractor proves each document reproduces its script.

Datapacks replace a document by its relative path; later configured packs take precedence. The port tries candidates from the highest priority down and uses the first whose `schema_version` is 2, whose `kind` is the one its loader reads, and whose fields parse with valid types, counts and string capacities. A candidate that fails prints one `config: <path>: <reason>` diagnostic and the next is used, so a broken overlay, or a schema-1 command document from an older extractor, falls back to the converted original. Paths cannot leave the converted-data root. Loaders skip entries the game's tables cannot hold, where the retail code would have written out of range.

The five `menu/<language>/help.cfg` caption files publish as `kind: "movie_subtitles"` documents with `chapters` of `id` and ordered `entries` (`start_seconds`, `clear_seconds`, `text`, optional `game_bytes`).

Character profiles and the cloth and effect scripts of characters live in the character's `.game.json` companion ([model format](PORT_MODEL_FORMAT.md)); their profiles have typed resource bindings, and their cloth and effect scripts are ordered typed command lists that the port runs through those scripts' tag tables. Text scripts that are not configurations (inventory `.lst` tables, georama `.gpi` files, dungeon floor titles, map effect `.em` scripts and event scripts) keep their own readers.
