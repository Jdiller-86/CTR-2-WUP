# Checked CSTM blocks and seek histories

Scoped #113 / PR #114 under #25/M3 adds a portable, allocation-free reader for
the existing [DSP ADPCM decoder](dsp-adpcm.md). It accepts little-endian CSTM
version `0x02000000`, non-looping DSP ADPCM, one or two channels and sample rates
1–384,000 Hz. Other versions, byte orders, codecs, loops and SEEK layouts return
an explicit unsupported result. This is a file/codec foundation; no guest DSP
service, voice, mixer or native audio path calls it yet.

## Checked layout

`runtime/core/ctr_cstm.h` exposes `ctr_cstm_info` and `ctr_cstm_block`. Each call
checks the actual byte length, three typed INFO/SEEK/DATA section references,
section headers, alignment, non-overlap and all used relative references. Section
order and metadata locations can vary. Optional track references and channel
indices are bounds-checked; track volume/pan is not applied. At most two tracks
are accepted. Bytes are read explicitly as little-endian values, including signed
coefficients and histories, without casting input into native structs.

The subset is bounded to 256 MiB, 65,536 blocks and the decoder's 1,048,576
samples per regular block. Regular blocks contain whole 14-sample/8-byte frames.
The declared total sample count must equal the regular blocks plus final block.
The final block may contain partial frames, with payload bytes between the last
needed nibble and the last complete frame. Its separate padded byte stride must
cover that payload without exceeding the regular block size. All channel payloads,
padding and SEEK entries must fit their own sections.

Block addressing uses channel-interleaved regular blocks. Final channels use the
final padded stride, while returned spans expose only their declared payload.
`first_sample` is an absolute stream position; the returned decoder state starts
at sample zero relative to that block's encoded base. Coefficients are copied to
the returned block. A caller owns its PCM output and passes the returned sample
count to `ctr_dsp_adpcm_decode`.

SEEK records contain numeric signed history1 then history2, ordered by block and
channel. This subset requires four bytes per channel and one record at every
regular block interval. The first record must agree with the channel's INFO
initial histories. Later histories are supplied checkpoints: the reader does not
authenticate them by decoding earlier samples. A seek within a block first decodes
and discards the prefix with the codec; no arbitrary sample seek API is provided.

## Ownership and failures

Input must stay alive and immutable while a returned span is used. Accessors
revalidate bytes rather than trusting caller-editable cached descriptors. All
argument, bounds, format, unsupported and overlap failures preserve the complete
output object. Output cannot alias the input. The reader does not scan encoded
predictors; a late predictor fault is rejected atomically by the existing decoder.
Neither component silently chooses another codec or claims playback success.

## Original evidence and remaining work

`make -C tests/cstm check sanitize` covers literal stereo PCM and history order,
partial payloads with poisoned padding, bad references/sizes/counts, truncation,
address wrap, aliasing and 8,192 deterministic mutations. It is included in
`make check` and Windows portable checks. The shared runtime manifest also
cross-compiles the reader for PowerPC.

```sh
python3 tools/validate-cstm.py --cc gcc --sanitize --output artifacts/cstm-reference
```

The validator constructs 240 original mono/stereo streams with relocated/reordered
sections, one through five blocks, optional track metadata, different coefficients,
nonzero histories and partial/padded endings. Exact rational arithmetic independently
produces 57,980 PCM samples and final histories. The C driver compares block SEEK
decoding with small-chunk continuation of prior decoded histories. CI repeats this
with GCC/Clang sanitizers and Windows and retains only original streams/reports.
No commercial bytes, coefficients, waveforms or decoded audio are fixtures.

The wire layout was checked against the
[3dbrew CSTM format description](https://docs.mikage.app/BCSTM/) and the
[vgmstream CSTM reader](https://github.com/vgmstream/vgmstream/blob/master/src/meta/bcstm.c);
no external implementation was imported. General track mixing, loops, other
SEEK intervals, CSAR selection, guest DSP protocol, synchronization and physical
Wii U audio remain open. The sampled commercial streams have not been decoded
or accepted by this original-fixture validation.
