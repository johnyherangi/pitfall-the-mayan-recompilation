# Reverse-engineering evidence

What the reconstruction in `src/` was derived from and how it was checked.  Nothing here is
needed to build or run the game.  The original game files (`original/`, the contents of the
Windows CD) and the Genesis ROM (`original_genesis/`) are not part of the repository — supply
your own copies.  The files below are disassembly, decompilation and screenshots of that
copyrighted software: they are in the private repository (`pitfall-the-mayan-decompilation`)
and not in the public one (`pitfall-the-mayan-recompilation`, made by `tools/publish.sh`),
which keeps the scripts, the JSON maps, `drivers/` and `verify/`.

| path | contents |
|------|----------|
| `pitfall_decomp.c` (+ `.data.txt`, `.symbols.txt`) | Ghidra 12.1.4 headless decompile of PITFALL.EXE: the 423 functions Ghidra defined (`tools/ghidra/export_decomp.bat`).  The object handlers are not among them — Ghidra does not see them as functions |
| `pitfall_handlers.c` | the second headless run over the object handlers (`tools/ghidra/export_handlers.bat`): every function failed with a decompiler timeout; kept as the record of that attempt |
| `evidence1/bundle.json` | REA evidence bundle: binary overview, procedures, names, strings, cross-references, a decompile |
| `index.tsv`, `procs.tsv` / `procs.txt`, `strings.tsv` / `strings.txt`, `imports.txt` | symbol tables exported from Ghidra (`index.tsv`: address, name, size) |
| `type_handlers.json` | the object handler table at 0x462350, by behaviour type |
| `sprite_slots*.txt` | sprite slot addresses (0x46ffc8..0x470470) and names; `tools/sprite_slots.json` is the canonical copy |
| `pickup_effects.json`, `sounds.json` | decoded tables |
| `listings/<object>_<type>.txt` | annotated x86 disassembly of every object handler that was ported (`pc_dis.py`) — the reading the C++ in `handlers.cpp` follows |
| `listings/genesis/*.txt` | annotated 68000 disassembly of the matching Genesis handlers (`tools/genesis_dis.py`) |
| `genesis_xref.json`, `genesis_handler_map.json` | ROM <-> PC cross-reference (sprite pointers -> sprite names, subroutines -> PC functions, RAM -> PC globals) and the two handler tables (ROM 0x10621e, PC 0x462350) |
| `pc_dis.py`, `anim_script.py`, `which_func.py` | helper scripts (below) |
| `drivers/` | scripted headless drivers used to verify each handler (see `drivers/README.md`) |
| `verify/` | the original run under Wine against the port: its frames and its memory, tick by tick, with the joypad it read replayed to the port (see `verify/README.md`) |
| `renders/` | frames rendered by the reconstruction during verification (`engine/`: renderer and Harry, `objects/`: the object handlers) |

The per-function decompile files used during the analysis are not included: each one is
contained verbatim in `pitfall_decomp.c`.

## The Genesis ROM

PITFALL.EXE's game logic (0x401000..0x437fff) is a machine translation of the Genesis
version's 68000 code: the 68000 registers live in memory (d0..d7 at 0x460180, a0..a7 at
0x4601a0), each instruction became several x86 ones, and the data tables are the same bytes
(multi-byte values byte-swapped).  The ROM is therefore the more readable copy of the same
logic, and `tools/genesis_dis.py` aligns the two: sprite pointers, subroutine calls and RAM
globals occur in the same order inside each handler, which names the ROM's symbols.  It is a
reading aid only — the PC build is what is reconstructed, its assets and audio included.
Where the two differ, the PC behaviour is kept (for example the skull warrior's bone throw,
`kGenesisThrow` in `handlers.cpp`).

## Scripts

```
python3 re/pc_dis.py 0x412fc2 0x414100     # x86 listing with symbol / register / sprite notes (needs objdump)
python3 tools/genesis_dis.py 66            # 68000 listing of the type-66 handler (needs capstone)
python3 tools/genesis_dis.py --map         # rebuild re/genesis_xref.json
python3 re/anim_script.py 0x45ad86         # decode an animation script (FUN_00423134 format)
python3 re/which_func.py 0x41a51b          # the Ghidra function containing an address
```

`PITFALL_EXE` and `GENESIS_ROM` override the default locations (`original/PITFALL.EXE`,
the first `original_genesis/*.bin`).
