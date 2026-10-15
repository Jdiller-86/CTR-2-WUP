# Checked audio streams and native playback

Scoped #115 under #25/M3 connects the checked CSTM/ADPCM reader to bounded
voices, fixed-rate stereo mixing and an original native listening diagnostic.
PR #116 implements the v0.26.0 stream-to-output checkpoint. The portable engine
and native adapter have separate ownership boundaries and independently checked
PCM. [Guided listening tests](audio-stream-testing.md) cover actual sound.

## Portable pull engine

`ctr_audio.h` exposes two CSTM voices and fixed 48 kHz interleaved PCM16 output.
Each immutable source may be mono/stereo at 8–96 kHz. Regular/final block layout,
coefficients and checkpoint histories come from the checked CSTM reader. Normal
playback carries decoded history across blocks; explicit seek starts from the
containing block's supplied checkpoint and decodes its prefix. Mono duplicates to
both output channels; stereo retains its channels.

Each output frame uses integer linear interpolation at an exact rational phase.
Interpolation and independent left/right Q15 gains round nearest, with ties toward
positive infinity. The two voices sum in wide integers and saturate once. This
resampler is deterministic but is not bandlimited or a Nintendo DSP fidelity
claim. EOF holds the final source sample only for its fractional tail, then emits
silence. Pause stops source consumption; play does not rewind. Seek resets phase;
seek to EOF also pauses. Engine counters include requested silence; per-voice
counters include consumed output and source decoding work. Counters saturate.

No allocation occurs in the engine. Its storage is 264 bytes on the measured
64-bit host, plus bounded stack caches. One render request contains at most 4,096
frames. A dry simulation validates every used sample before the commit pass, so
late predictor/metadata/bounds/alias errors preserve the entire engine and output.
Caller-provided typed objects must have valid lifetime and alignment; source bytes
remain immutable and alive. This is not protection against invalid host pointers
or concurrent mutation.

## Native ownership and controls

The Audio Streams app renders finite original clips in bounded pulls on the main
thread. The native adapter copies completed interleaved PCM into static aligned
planar buffers, flushes those buffers, and routes two mono AX voices separately
to TV and GamePad left/right. Native SRC is disabled and the ratio is one. AX
never reads guest memory, stack PCM or a buffer being mixed. No audio callback
executes the mixer, storage or logging.

The adapter accepts at most 96,000 frames, supports pause/resume and replay, and
requires both voices to be observed running before reporting completion. Partial
allocation/configuration failure releases acquired voices. Release happens before
AXQuit. Lost AX ownership discards stale pointers. A foreground interruption
invalidates the current guide attempt; reacquisition requires a fresh play action.
Seek regenerates a checked suffix before native playback. These are finite-clip
controls, not a callback streaming or synchronization implementation.

The app records output hashes, rendered/decoded/clipped frames, render time,
voice cursors, ownership/lifecycle failures, session/restart counts and each named
human listening result. Roughly 840 KiB of additional static capacity covers
original fixtures, render storage and native PCM planes. Actual Wii U timing,
pitch, routing, silence and lifecycle still require the guided hardware return.

## Original evidence

The focused engine suite covers 10,844 checks and native AX mocks cover 2,393
checks in normal and ASan/UBSan modes with leak detection. The independent exact
rational model compares 252 cases, 1,884,081 stereo output frames and 9,589 complete
control/state snapshots over nine source rates. It covers two voices, mono/stereo,
partial blocks, clipping, pause, seek, EOF and all seven generated playback scenes.
GCC/Clang sanitizer and Windows jobs retain reports and original reference WAVs.

```sh
python3 tools/validate-audio-engine.py --cc gcc --sanitize --output artifacts/audio-reference
python3 -m host build --target audio-stream-check --output artifacts/audio-stream-check
```

This checkpoint uses finite, owned native clips. It does not implement the
3DS DSP firmware/IPC protocol or synchronized real-time game playback. Real
NDSP sends voice queues through shared DSP memory and needs a component setup;
a custom play opcode would not establish that compatibility. No firmware or
commercial audio is bundled.

The boundary follows primary libctru interfaces for
[DSP services](https://github.com/devkitPro/libctru/blob/master/libctru/source/services/dsp.c),
[NDSP setup](https://github.com/devkitPro/libctru/blob/master/libctru/source/ndsp/ndsp.c)
and [voice records](https://github.com/devkitPro/libctru/blob/master/libctru/source/ndsp/ndsp-internal.h).
No external implementation was imported. Real guest firmware/IPC, CSAR selection,
looping, real-time streaming, game synchronization and physical audio acceptance
remain open. This scoped checkpoint does not accept full M1, M3 or commercial audio.
