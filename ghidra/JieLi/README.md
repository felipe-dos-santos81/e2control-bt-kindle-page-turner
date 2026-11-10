> **Local copy.** Taken from [quarkslab/ghidra-jieli](https://github.com/quarkslab/ghidra-jieli) at `e1bd070` (Apache-2.0). Changes to **q32s** only, checked against the BD19 ROM objdump (`ghidra/score.sh`, 9,746/9,746 instruction lengths):
>
> - New encodings: `clz`, 3-operand `lsl`/`lsr`/`asr`/`ror` (immediate and register), compare-with-`weirdimm` branches (`ff2x`), register-mask `pushm`/`popm` (`f37x`), `pop {rets}`, `push`/`pop {psr, rets, reti}` (`q32s_ins_extra.sinc`, `q32s_ins_stack.sinc`). `fdf3`/`fdfb` (`asr`) are inferred from the shift-group order; they are not in the ROM listing.
> - `push`/`pop` register lists now move the registers and `sp`. Before, they had no p-code.
> - Fixed semantics: `jls` was `>` (now unsigned `<=`), `jge` was `s<=` (now `s>=`), `fde5` was named `uxtb` (it is `sxtb`).
> - 23-bit PC-relative branches wrap at 32 MB, so flash code (`0x1E0xxxx`) reaches the ROM (`0x106xxx`) and RAM.

# ghidra-jieli

This is the improved Ghidra processor module for some JieLi's custom CPU architectures, derived from
[Kagaimiq](https://github.com/kagaimiq)'s [original implementation](https://github.com/kagaimiq/jielie/).

- dv10 *(this is Blackfin, not implemented)*
- dv12 *(this is Blackfin, not implemented)*
- pi32 *(not complete, but somewhat usable now.)*
- pi32v2 *(covers most instructions, sone remain unknown)*
- q32s *(very early stage)*
- f59 *(not implemented yet)*
- f95 *(not implemented yet)*

## TODO

- pi32
  - Stack-related instructions (currently only `push {rets}` and `pop {pc}` is handled)
  - Remaining missing instructions
  - rep loops and if-else blocks?
  - Maybe refactor the mnemonics? (like the ones I'm using on q32s and pi32v2? or do it the other way around?)
  - Change flags on instructions that change them (e.g. add, sub, rotc, etc.)
- pi32v2
  - Remaining missing instructions
  - Likely this is far more complicated than pi32, though
  - Mnemonics like in q32s or pi32?
- q32s
  - Do the rest:
    - Group7 instructions of course
    - Flags and stacks too
  - Make decisions about my new mnemonic standard... (or use pi32 one?)
  - (at least it's used on BD19 and BD29 - AC632N and AC630N resp.)
  - (It also seems to be simpler than pi32 or pi32v2!)
- dv10/dv12 (blackfin)
  - Maybe implement it too? Or leave it as a separate project?
