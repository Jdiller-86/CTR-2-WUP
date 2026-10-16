# Local encrypted-input preparation

CTR2WUP accepts already-decrypted input or supported encrypted containers with
locally supplied material. Preparation validates recovered bytes before the
loader/translator; it does not enable unsupported game profiles.

## Supported inputs

The shared Python engine supplies **prepare-rom** and **import-rom-keys**.
They are developer commands; the Windows GUI and on-console extraction helper
are not implemented. Decryption is local preparation before the existing checked
loader/translator. No platform key values, console dumps or commercial assets
are bundled. Already-decrypted input requires no keys or crypto dependency.

| Input layer | Implemented behavior |
|---|---|
| CIA outer content | AES-CBC with a local plaintext title key, or a local normal common key plus the ticket's wrapped title key; content-index IV and full main-content TMD SHA-256 check |
| NCCH v2 | Exheader/access descriptor, ExeFS header/files/padding and RomFS AES-CTR; primary and secondary key selection, unaligned stream offsets and 1 MiB bounded chunks |
| NCCH crypto methods | Standard KeyX slot 2c, and methods 1 / 0a / 0b using slots 25 / 18 / 1b; seeded secondary KeyY derivation and seed check |
| NCSD cartridge | Bounds/overlap checks and extraction of main partition 0; other partitions are not exported or fully inspected |
| Local key imports | Plaintext AES key database retail NCCH KeyX records; seeddb base-title seeds; explicitly declared decrypted title-key database base-title records |

The prepared private output contains **game.cxi** and **preparation-report.json**.
It preserves the main executable and RomFS bytes; CIA auxiliary contents, tickets
and key files are not copied into that output. The existing inspector validates
exheader/section/file hashes and code decompression before atomic publication.
A small executable-only upload can then be made with prepare-intake. Source files
and existing destinations remain unchanged; failed staging is removed.

Wrong/missing keys or seeds, damaged content, malformed ranges, overlaps and
unsupported modes fail visibly. Integrity checks do not authenticate Nintendo
signatures. RomFS superblock and canonical three-level IVFC verification are implemented.
All declared hash/data blocks, final padding and bounded Level-3 metadata are
checked before preparation is published. Unsupported layouts fail explicitly;
commercial asset/runtime integration remains unfinished. A derived CXI is not
a playable Wii U game or a complete reconstruction of every cartridge/CIA content.

## Key requirements

CIA wrapping and NCCH encryption are different layers. The necessary material
can include platform KeyX components, a CIA common/title key and a title-specific
seed. Some platform keys are shared across consoles. A console-specific SD key
such as movable.sed is not sufficient by itself for these standalone game files.
The key scrambler is an implemented public algorithm, not a bundled platform key.
3DS input keys and Wii U packaging inputs are separate configurations.

Exporting data requires work on a homebrew-enabled 3DS. A normal Windows USB
connection to a stock console does not provide access to its keys. Existing
[GodMode9](https://github.com/d0k3/GodMode9) can decrypt game images on-console and
export support data; its documentation covers support-file creation and bootrom
access. CTR2WUP imports already obtained files. It does not install a console
exploit, dump boot9/OTP/NAND, fetch key databases or currently supply a companion.

The local AES-key importer requires **decrypted aeskeydb-format records** and
imports only standard retail NCCH KeyX slots with empty identifiers. It rejects
encrypted databases, ambiguous duplicates and malformed metadata; unrelated and
devkit-only records are ignored. It does not extract keys from boot9.bin or
aes_keys.txt. seeddb uses little-endian title IDs; decrypted title-key records
store title IDs in the ticket's big-endian byte order. Database plaintext declaration
is not cryptographically authenticated: the recovered CIA must still match its TMD.

A console-bound/personalized ticket needs an already recovered local title key;
EC ticket depersonalization is not implemented. Fixed-key NCCH, v1 counters,
unknown crypto methods, raw encrypted SD/NAND, updates and DLC are unsupported.
Only the documented import formats are accepted.

## Developer workflow

Keep all real dumps, keys, prepared games and derived code under private local
storage. The import output is a key store and must remain on the user's PC;
**do not upload it**. Only file paths are accepted as key configuration; reports,
repr and commands never contain key values. Keys are read in-process and are not
passed to compilers or included in Wii U output. The current private store uses
local files without Windows credential-store integration.

```sh
python -m pip install '.[decryption]'
python -m host import-rom-keys --aeskeydb "private/aeskeydb.bin" \
  --seeddb "private/seeddb.bin" --decrypted-titlekeys "private/decTitleKeys.bin" \
  --output "private/3ds-key-store"
python -m host prepare-rom "private/owned-game.cia" \
  --keys-config "private/3ds-key-store/keys.json" --output "private/prepared-game"
python -m host prepare-intake "private/prepared-game/game.cxi" \
  --output "private/small-intake"
```

Each optional database argument can be omitted. Missing material is named by
role/title rather than silently substituted. Advanced schema-1 JSON supports
ncch_key_x_files, cia_common_key_files, cia_title_key_files and title_seed_files;
map slot/index/title identity to a local **16-byte binary file path**. A decrypted
input can go directly through prepare-rom without --keys-config.

## Verification

Original synthetic fixtures cover all four NCCH methods, seed/no-seed, both CIA
layers, mixed ExeFS keys, padding, compressed code, Unicode paths, chunk boundaries,
wrong inputs and non-overwriting publication. NIST AES CBC/CTR vectors and a
separate bit-string/byte-carry scrambler reference check the crypto primitives.
No user key or game is used in these tests.

The original encrypted execution fixture runs through **decrypt → inspect →
small intake → external AOT → desktop execution → Wii U RPX/WUHB cross-build**.
The original guest completes its frames and exits; this is a pipeline check,
not commercial execution or console acceptance. Run:

```sh
python3 tools/validate-rom-preparation.py --wiiu \
  --output artifacts/reports/my-encrypted-pipeline
```

Format references include GodMode9 commit
f3b9ec7a50b8c8459f62b87bfd7b7214b6f6bda2 (ncch, keydb, ticket, ticketdb and seedsave
sources) and the pinned loader sources. GodMode9 code, key databases and
executables are not dependencies. Synthetic fixture coverage does not establish
compatibility with every exported key set or encrypted dump. PICA, guest audio,
retail saves and commercial WUP conversion are outside this preparation contract.

Use `prepare-assets` on the recovered CXI to prepare a private checked Level-3
blob and manifest. Guest filenames remain virtual; no archive filenames are
extracted onto host paths. See [romfs-assets.md](romfs-assets.md).

Supported decrypted `.cci` (NCSD) enters the same checked preparation pipeline;
no external batch decryptor executable is bundled or executed.
