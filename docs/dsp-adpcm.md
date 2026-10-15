# Checked DSP ADPCM decoding reference

Scoped #103 under #25/M3 implements a portable integer codec foundation using
original vectors. Guest DSP IPC, firmware/components, voices, mixing,
CSAR parsing and native audio output remain separate work. The scoped
[CSTM reader](cstm.md) now supplies checked channel blocks and SEEK histories. No commercial
waveform, coefficients or decoded audio is bundled. Earlier native silence/tone
results remain valid for their builds and do not establish guest audio.

## Numeric contract

`runtime/core/ctr_dsp_adpcm.h` accepts eight signed coefficient pairs, explicit
two-sample history and a cursor relative to the original encoded base. A frame
has one header and seven packed bytes (fourteen samples), high nibble first.
The header selects predictor 0–7 and scale exponent 0–15. A signed nibble `n`
produces:

```text
prediction = (coefficient0 * history1 + coefficient1 * history2) / 2048
sample = clamp_to_int16(floor(n * 2^scale + prediction + 1/2))
history2 = history1
history1 = sample
```

Bounded signed 64-bit arithmetic and explicit floor division avoid signed shifts,
floating point, overflow and host byte-order assumptions. Negative ties round
toward positive infinity; clipping precedes history updates. Coefficients and
histories are numeric signed PCM16; container byte-order parsing remains the
caller's responsibility.

## Continuation and atomic failure

Calls advance `state.sample` and return single-channel signed PCM16 into owned
output. Continue against the same encoded base and history. An explicit seek
checkpoint supplies the two preceding samples and sample index; earlier frames
are not re-decoded or authenticated. A frame-aligned block can use a new base
and cursor zero with its supplied history. Only requested bytes/nibbles are
required, allowing a final partial frame. Unused headers/nibbles are not checked.
The [CSTM subset](cstm.md) supplies channel interleaving, final padding and
block-aligned SEEK histories; general intervals and loops remain open.

Each call allocates nothing and is bounded to 1,048,576 samples. Argument,
capacity/cursor/address bounds, write/input overlap and every requested frame's
predictor are checked before any PCM/history/cursor write. A late invalid
predictor preserves the entire prior output/state. State and written output
cannot alias each other or encoded/coefficient input, which stays immutable.
Zero samples is a validated no-op. Errors are explicit `argument`, `bounds`,
`predictor` or `overlap`; no fallback is selected.

## Original validation

`make -C tests/dsp-adpcm check sanitize` covers literal signed nibbles,
positive/negative rounding ties, scale 15, saturation/history, single-sample
continuation, checkpoint seek, final partial bytes, late errors, overlap/address
wrap, zero samples and the exact sample limit. This suite is part of `make check`
and Windows `make check-c-portable`. The shared runtime manifest cross-compiles
the module for PowerPC; no audio voice calls it yet.

```sh
python3 tools/validate-dsp-adpcm.py --cc gcc --sanitize --output artifacts/dsp-reference
```

An independent Python reference uses exact rational numbers and floor division.
6,144 original cases compare every one of 175,770 samples and final histories,
with streamed calls and midpoint checkpoint seek. All predictor/scale/nibble
combinations and seeded varying headers/coefficients/histories/partial frames are
covered. GCC/Clang repeat normal and ASan/UBSan comparisons; Windows runs the
same independent vectors. CI retains original vectors, compiler/source identity,
hashes and reports. These checks establish the stated codec subset only.
The scoped CSTM reader has original container/block/SEEK evidence. Game stream
selection, broader containers, DSP services, mixing/synchronization and Wii U
playback/cleanup still need implementation and physical evidence.
