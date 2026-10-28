# Audio decoding, streams and native output

The portable core separates DSP ADPCM decoding, checked CSTM blocks, deterministic
mixing and native AX ownership. A bounded guest DSP PCM service is also available. Arbitrary DSP firmware,
CSAR selection and synchronized real-time game playback remain unsupported.
All fixtures are original; no commercial waveform or firmware is bundled.

## DSP ADPCM numeric contract

`ctr_dsp_adpcm.h` accepts eight signed coefficient pairs, two-sample history and
a cursor relative to the original encoded base. Each 8-byte frame contains a
header plus 14 high-nibble-first signed samples. The header selects predictor 0..7
and scale 0..15:

```text
prediction = (coefficient0 * history1 + coefficient1 * history2) / 2048
sample = clamp_to_int16(floor(nibble * 2^scale + prediction + 1/2))
history2 = history1
history1 = sample
```

Explicit signed 64-bit floor division avoids floating point, signed shifts,
overflow and host byte order. Negative ties round toward positive infinity;
clipping precedes history updates. Coefficients/history are numeric PCM16;
container parsing supplies byte order.

Calls advance `state.sample` against the same encoded base. A seek checkpoint
supplies the two preceding samples and index; earlier samples are not authenticated
by re-decoding. Partial final frames need only requested bytes/nibbles. Each
allocation-free call supports at most 1,048,576 samples. Complete bounds, capacity,
predictor and overlap checks precede PCM/history/cursor publication, so a late
bad predictor preserves all output/state. Zero samples is a validated no-op.

## CSTM blocks and seek

`ctr_cstm.h` accepts little-endian version `0x02000000`, non-looping DSP ADPCM,
mono/stereo and sample rates 1–384,000 Hz. Other byte orders/versions/codecs/loops
or SEEK layouts return explicit unsupported results. INFO/SEEK/DATA references,
headers, alignment, non-overlap and used relative offsets are checked against the
actual length. At most two tracks are accepted; track indices are checked but
volume/pan is not applied. Bytes are read explicitly, never cast to native structs.

Limits are 256 MiB, 65,536 blocks and 1,048,576 samples per regular block. Regular
blocks use whole 14-sample/8-byte frames. Declared total samples must match regular
plus final blocks. Final payload may end mid-frame; padded stride must cover it
without exceeding regular block size. All channel payloads/padding/SEEK entries
must fit their own sections. Channels interleave by regular block; final channels
use the final padded stride. Returned spans exclude padding.

SEEK stores signed history1/history2 per block/channel, four bytes per channel,
at every regular block interval. First histories must match INFO. Later histories
are supplied checkpoints; seek decodes/discards the containing block's prefix.
Accessors revalidate caller-editable descriptors. Input stays alive/immutable;
output cannot alias input. Argument/bounds/format/unsupported/overlap failures
preserve output. Encoded predictors are validated by the decoder when used.

## Portable pull engine

`ctr_audio.h` provides two CSTM voices and fixed 48 kHz interleaved PCM16 output.
Sources are mono/stereo at 8–96 kHz; mono duplicates to both channels. Normal
playback carries history; explicit seek starts from a checked block checkpoint.
Each output frame uses integer linear interpolation at an exact rational phase.
Interpolation and independent left/right Q15 gains round nearest with ties toward
positive infinity; voices sum wide and saturate once. The resampler is deterministic,
not bandlimited and not a Nintendo DSP fidelity model.

EOF holds the last sample only for its fractional tail then emits silence.
Pause stops consumption; play does not rewind. Seek resets phase and seek-to-EOF
pauses. Engine counters include requested silence; voice counters track consumed
output and decoding work. Counters saturate. No allocation occurs. A request is
at most 4096 frames. A dry pass validates every used sample before commit; late
metadata/predictor/bounds/alias errors preserve engine and output. Typed host
objects need valid lifetime/alignment; immutable sources cannot be mutated concurrently.

## Guest DSP PCM service

`dsp::DSP` supports the standard load/unload, address conversion, pipe handshake,
interrupt registration, semaphore event and cache-maintenance commands used by
an original ARM probe. Load accepts only the exact original `CTR2WUP-PCM-HLE`
version-1 token with masks `ff/ff`; it selects HLE and executes no DSP firmware.
Other components, commands, descriptors and processing states fail explicitly.
The shared DRAM mapping is `1ff40000..1ff80000`; DSP word addresses select two
`20000`-byte banks. All wire values are explicitly little-endian, including
NDSP's halfword-rotated physical addresses/counts/positions.

The profile supports 24 mono PCM16 voices at unity DSP rate, one current buffer
and four circular queued buffers each, nonzero sequence IDs, pause/resume and
whole-buffer looping. Sources must remain readable in the same live owned linear
allocation; each is at most 65,536 samples. Imported current/queued buffers pin
their allocations until completion, replacement or session teardown; freeing or
reprotecting a pinned allocation is rejected. Stereo mix gains and master volume
are bounded binary32 values converted deterministically to signed Q15. Mixing
rounds ties positive and hard-clips once. Effects, auxiliary routing, ADPCM,
variable source rates and surround are unsupported. This is a deterministic PCM
profile, not a firmware-fidelity claim.

A signaled retained semaphore schedules one 160-sample frame at the exact
`16756991/512` Hz DSP rate. Host polling never completes it early. Both banks,
all channel updates and sources are validated before state, status, capture or
PCM publication. An eight-frame host queue never overwrites unread output;
pressure faults explicitly. Interrupt/semaphore objects retain normal typed
handle ownership through duplication and closure. Closing the owning session
cancels work and releases references. No AX calls run inside a DSP frame.

`ctr_dsp_pcm48k` converts finite DSP output to 48 kHz with exact rational integer
linear interpolation. The guided Guest Audio app checks 300 genuine ARM-produced
blocks through interpreter and bounded AOT, validates every source sample, then
plays the immutable converted clip through the shared AX adapter. It automates
pause/resume and records one listening response. Finite rendering does not
establish continuous guest/AX synchronization or commercial audio compatibility.

## Native ownership

The finite Audio Streams diagnostic renders bounded pulls on the main thread.
The adapter copies completed PCM into static aligned planar buffers, flushes them
and routes two mono AX voices separately to TV/GamePad left/right. Native SRC is
disabled at ratio 1. AX never reads guest memory, stack PCM or a buffer being mixed.
No callback runs storage, mixing or logging.

At most 96,000 frames are accepted. Pause/resume and replay are supported; both
voices must be observed running before completion. Partial allocation/configuration
failure releases voices before AXQuit. Lost ownership discards stale pointers.
Foreground interruption invalidates an attempt; reacquisition requires fresh play.
Seek regenerates a checked suffix before playback. This is finite-clip ownership,
not callback streaming. Diagnostics record output hashes, frame/cursor counters,
render time, lifecycle failures and listening observations separately.

## Validation and references

```sh
make -C tests/guest-dsp check sanitize
make -C tests/dsp-adpcm check sanitize
make -C tests/cstm check sanitize
python3 tools/validate-dsp-adpcm.py --cc gcc --sanitize --output artifacts/adpcm
python3 tools/validate-cstm.py --cc gcc --sanitize --output artifacts/cstm
python3 tools/validate-audio-engine.py --cc gcc --sanitize --output artifacts/audio
```

Exact rational references check PCM, histories, continuation, seek, resampling,
gains and clipping. C tests exercise bounds, padding, late errors, mutations,
rollback and AX ownership mocks. Host vectors do not establish pitch, routing,
listening or physical playback cleanup.

Interface facts follow the [CSTM description](https://docs.mikage.app/BCSTM/),
[vgmstream reader](https://github.com/vgmstream/vgmstream/blob/master/src/meta/bcstm.c)
and libctru [DSP](https://github.com/devkitPro/libctru/blob/master/libctru/source/services/dsp.c)/
[NDSP](https://github.com/devkitPro/libctru/blob/master/libctru/source/ndsp/ndsp.c)
interfaces. The address-conversion formula was cross-checked against
[Azahar](https://github.com/azahar-emu/azahar/blob/1edf59ef592a854ccc2a7405d9aade477b2a0b3d/src/core/hle/service/dsp/dsp_dsp.cpp).
No external implementation was imported.
