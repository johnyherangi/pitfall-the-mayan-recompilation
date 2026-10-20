# Reverse-engineering notes

How the code in `src/` was read from the original game and checked against it. Nothing here is
needed to build or play the game, and none of it contains game data.

| Path | Contents |
|------|----------|
| `verify/` | runs the original under Wine and compares it with the port: frame by frame, and its memory tick by tick with the original's joypad replayed (see `verify/README.md`) |
| `drivers/` | scripted headless drivers used to check each object handler (see `drivers/README.md`) |
| `type_handlers.json` | the object handler table at 0x462350, by behaviour type |
| `pickup_effects.json`, `sounds.json` | decoded tables |
| `genesis_xref.json`, `genesis_handler_map.json` | links between the Genesis ROM and the PC game: sprite pointers to sprite names, subroutines to functions, RAM to globals, and both handler tables |
| `pc_dis.py`, `anim_script.py`, `which_func.py` | helper scripts (below) |

## The Genesis ROM

`PITFALL.EXE`'s game logic (0x401000..0x437fff) is a machine translation of the Genesis
version's 68000 code: the 68000 registers live in memory (d0..d7 at 0x460180, a0..a7 at
0x4601a0), each instruction became several x86 ones, and the data tables are the same bytes
(byte-swapped). So the ROM is a more readable copy of the same logic, and `tools/genesis_dis.py`
lines the two up. It is a reading aid only: the PC game is what is rebuilt, and where the two
differ the PC behaviour is kept (for example the skull warrior's bone throw, `kGenesisThrow` in
`handlers.cpp`).

## Scripts

```
python3 re/anim_script.py 0x45ad86         # decode an animation script
python3 tools/genesis_dis.py 66            # 68000 listing of the type-66 handler (needs capstone)
python3 tools/genesis_dis.py --map         # rebuild re/genesis_xref.json
python3 re/pc_dis.py 0x412fc2 0x414100     # x86 listing with symbol notes (needs objdump)
python3 re/which_func.py 0x41a51b          # the function containing an address
```

`pc_dis.py` and `which_func.py` need a symbol table, `re/index.tsv` (address, name and size of
each function, tab-separated), exported from Ghidra for your own `PITFALL.EXE`.
`PITFALL_EXE` and `GENESIS_ROM` override the default locations (`original/PITFALL.EXE`, the first
`original_genesis/*.bin`).
