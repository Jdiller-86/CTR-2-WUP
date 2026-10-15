#!/usr/bin/env python3
"""Compare original CSTM pull/resample/mix/control cases with rational PCM."""
import argparse
from fractions import Fraction
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import wave

ROOT = Path(__file__).resolve().parents[1]
RATES = [8000, 11025, 22050, 32000, 44100, 48000, 64000, 88200, 96000]
RATE = 48000
ONE = 32768


def floor(value):
    return value.numerator//value.denominator


def nearest(value):
    return floor(value+Fraction(1, 2))


def clamp(value):
    return max(-32768, min(32767, value))


def stream(data):
    """Independent field reads plus full rational decode, not the C parser."""
    u16 = lambda at: struct.unpack_from('<H', data, at)[0]
    u32 = lambda at: struct.unpack_from('<I', data, at)[0]
    sections = {u16(20+i*12): u32(24+i*12) for i in range(3)}
    info, seek, audio = [sections[k] for k in (0x4000, 0x4001, 0x4002)]
    meta = info+8+u32(info+12)
    channels, rate, count = data[meta+2], u32(meta+4), u32(meta+12)
    blocks, full_bytes, full_samples, last_bytes, last_samples, stride = struct.unpack_from('<6I', data, meta+16)
    table = info+8+u32(info+28)
    start = audio+8+u32(meta+52)
    pcm = []
    for channel in range(channels):
        record = table+u32(table+8+channel*8)
        adpcm = record+u32(record+4)
        coefficients = struct.unpack_from('<16h', data, adpcm)
        h1, h2 = struct.unpack_from('<2h', data, adpcm+34)
        output = []
        for block in range(blocks):
            checkpoint = struct.unpack_from('<2h', data, seek+8+(block*channels+channel)*4)
            if checkpoint != (h1, h2):
                raise ValueError('Original stream SEEK histories differ from full rational decoding')
            samples = last_samples if block == blocks-1 else full_samples
            payload_bytes = last_bytes if block == blocks-1 else full_bytes
            at = start+block*full_bytes*channels+channel*(stride if block == blocks-1 else full_bytes)
            payload = data[at:at+payload_bytes]
            for n in range(samples):
                frame, lane = divmod(n, 14)
                predictor, scale = divmod(payload[frame*8], 16)
                packed = payload[frame*8+1+lane//2]
                nibble = (packed >> (0 if lane%2 else 4)) & 15
                if nibble >= 8:
                    nibble -= 16
                a, b = coefficients[predictor*2:predictor*2+2]
                value = clamp(nearest(Fraction(nibble*2**scale)+Fraction(a*h1+b*h2, 2048)))
                output.append(value)
                h2, h1 = h1, value
        if len(output) != count:
            raise ValueError('Original stream sample count differs')
        pcm.append(output)
    return dict(pcm=pcm, rate=rate, samples=count, channels=channels, blocks=blocks)


def at_rate(data, rate):
    data = bytearray(data)
    sections = {struct.unpack_from('<H', data, 20+i*12)[0]: struct.unpack_from('<I', data, 24+i*12)[0] for i in range(3)}
    info = sections[0x4000]
    meta = info+8+struct.unpack_from('<I', data, info+12)[0]
    struct.pack_into('<I', data, meta+4, rate)
    return bytes(data)


class Reference:
    def __init__(self, sources):
        self.voices = [dict(source=stream(data), position=0, phase=0, playing=False,
                            gains=[ONE, ONE], rendered=0, clipped=0) for data in sources]
        self.rendered = self.clipped = 0

    def snapshot(self):
        output = bytearray(struct.pack('<QQ', self.rendered, self.clipped))
        for v in self.voices:
            output += struct.pack('<5I2Q', 1+2*int(v['playing']), v['position'], v['phase'],
                                  *v['gains'], v['rendered'], v['clipped'])
        return output

    def command(self, kind, voice, a, b):
        v = self.voices[voice] if voice < 2 else None
        output = bytearray(struct.pack('<I', 0))
        if kind == 1:
            v['gains'] = [a, b]
        elif kind == 2:
            v['playing'] = bool(a) and v['position'] < v['source']['samples']
        elif kind == 3:
            v['position'], v['phase'] = a, 0
            if a == v['source']['samples']:
                v['playing'] = False
        elif kind == 4:
            for _ in range(a):
                contributions = [[0, 0], [0, 0]]
                for i, v in enumerate(self.voices):
                    if not v['playing']:
                        continue
                    s, pos, fraction = v['source'], v['position'], Fraction(v['phase'], RATE)
                    for channel in range(2):
                        samples = s['pcm'][channel if s['channels'] == 2 else 0]
                        first, second = samples[pos], samples[min(pos+1, s['samples']-1)]
                        interpolated = nearest(first+(second-first)*fraction)
                        contributions[i][channel] = nearest(Fraction(interpolated*v['gains'][channel], ONE))
                    whole, v['phase'] = divmod(v['phase']+s['rate'], RATE)
                    v['position'] = min(s['samples'], pos+whole)
                    v['playing'] = v['position'] < s['samples']
                    v['rendered'] += 1
                for channel in range(2):
                    total = sum(c[channel] for c in contributions)
                    clipped = total < -32768 or total > 32767
                    if clipped:
                        self.clipped += 1
                        for i, v in enumerate(self.voices):
                            if (total > 0 and contributions[i][channel] > 0) or (total < 0 and contributions[i][channel] < 0):
                                v['clipped'] += 1
                    output += struct.pack('<h', clamp(total))
            self.rendered += a
        else:
            raise ValueError('Unknown original command')
        return output+self.snapshot()


def commands(index, sources, rng):
    a, b = [stream(data) for data in sources]
    gains = [0, 1, 8192, 16384, 24576, 32768]
    rows = [(1, 0, gains[index%6], gains[(index+3)%6]),
            (1, 1, gains[(index+4)%6], gains[(index+1)%6]),
            (2, 0, 1, 0), (2, 1, index%3 != 0, 0),
            (4, 0, 17, 0), (4, 0, 13, 0), (2, 0, 0, 0), (4, 0, 11, 0),
            (3, 0, a['samples']//2, 0), (4, 0, 7, 0), (2, 0, 1, 0)]
    for _ in range(8):
        voice = rng.randrange(2)
        kind = rng.randrange(4)
        if kind == 0:
            rows.append((1, voice, rng.choice(gains), rng.choice(gains)))
        elif kind == 1:
            rows.append((2, voice, rng.randrange(2), 0))
        elif kind == 2:
            rows.append((3, voice, rng.randrange([a, b][voice]['samples']+1), 0))
        rows.append((4, 0, rng.choice([0, 1, 2, 5, 14, 31, 79, 160, 257]), 0))
    rows += [(1, 0, ONE, ONE), (1, 1, ONE, ONE),
             (3, 0, 0, 0), (3, 1, 0, 0), (2, 0, 1, 0), (2, 1, 1, 0),
             (4, 0, 4096, 0), (4, 0, 7, 0),
             (3, 0, a['samples']-1, 0), (2, 0, 1, 0), (4, 0, 9, 0),
             (3, 0, a['samples'], 0), (2, 0, 1, 0), (4, 0, 2, 0)]
    return [tuple(map(int, row)) for row in rows]


def fixture_pcm(data):
    s = stream(data)
    frames = (s['samples']*RATE+s['rate']-1)//s['rate']
    r = Reference([data, data])
    r.command(2, 0, 1, 0)
    output = bytearray()
    for begin in range(0, frames, 4096):
        count = min(4096, frames-begin)
        block = r.command(4, 0, count, 0)
        output += block[4:4+count*4]
    return bytes(output), frames


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte)*16777619) & 0xffffffff
    return f'{value:08x}'


def write_wav(path, pcm):
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(2);wav.setsampwidth(2);wav.setframerate(RATE);wav.writeframes(pcm)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    output = args.output.resolve()
    modes = ['normal', 'sanitized'] if args.sanitize else ['normal']
    binaries = {}
    sources = ['runtime/core/ctr_audio.c', 'runtime/core/ctr_cstm.c', 'runtime/core/ctr_dsp_adpcm.c',
               'runtime/core/ctr_audio_fixture.c', 'tests/audio-engine/reference_driver.c']
    for mode in modes:
        binary = output/('reference-'+mode+('.exe' if os.name == 'nt' else ''))
        command = [args.cc, '-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Iruntime/core', *sources, '-o', str(binary)]
        if mode == 'sanitized':
            command += ['-O1', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
        subprocess.run(command, cwd=ROOT, check=True)
        binaries[mode] = binary
    subprocess.run([str(binaries['normal']), '--fixtures', str(output)], check=True)
    spec = importlib.util.spec_from_file_location('original_cstm_vectors', ROOT/'tools/validate-cstm.py')
    original = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(original)
    rng = random.Random(0x41554d58)
    rows = []
    for index in range(240):
        first = at_rate(original.fixture(index, rng)[0], RATES[index%len(RATES)])
        second = at_rate(original.fixture((index*7+3)%240, rng)[0], RATES[(index*5+2)%len(RATES)])
        pair = [first, second]
        rows.append((pair, commands(index, pair, rng)))
    diagnostics = []
    for name in ['mono32k.cstm', 'stereo32k.cstm', 'stereo48k.cstm', 'mono32k-falling.cstm', 'mono48k.cstm']:
        data = (output/name).read_bytes()
        decoded = stream(data)
        # Literal waveform reconstruction also checks the native fixture builder.
        for c, channel in enumerate(decoded['pcm']):
            for n, sample in enumerate(channel):
                phrase, within = divmod(n, 16000)
                value = 0
                if within < 12800:
                    motif = 1 if name == 'mono32k-falling.cstm' else c
                    phase = within*[[220, 330, 440], [660, 550, 440]][motif][phrase]%32000
                    triangle = phase*24//32000-6 if phase < 16000 else 18-phase*24//32000
                    envelope = min(within, 12800-within, 128)
                    value = (abs(triangle*envelope)//128)*(1 if triangle >= 0 else -1)*1024
                if sample != value:
                    raise ValueError('Native original motif differs from independent integer definition')
        pcm, frames = fixture_pcm(data)
        wav_name = name.replace('.cstm', '-48k.wav')
        write_wav(output/wav_name, pcm)
        pair = [data, data]
        control = [(2, 0, 1, 0)]
        control += [(4, 0, min(4096, frames-begin), 0) for begin in range(0, frames, 4096)]
        control += [(4, 0, 7, 0)]
        rows.append((pair, control))
        diagnostics.append(dict(file=name, channels=decoded['channels'], source_rate=decoded['rate'],
                                source_frames=decoded['samples'], output_frames=frames,
                                cstm_sha256=hashlib.sha256(data).hexdigest(), pcm_sha256=hashlib.sha256(pcm).hexdigest(),
                                wav_file=wav_name, wav_sha256=hashlib.sha256((output/wav_name).read_bytes()).hexdigest()))
    scenes = []
    configurations = [
        ('left', ['mono32k.cstm', 'mono32k-falling.cstm'], [(ONE, 0), (ONE, ONE)], False, 0, 72000),
        ('right', ['mono32k-falling.cstm', 'mono32k.cstm'], [(0, ONE), (ONE, ONE)], False, 0, 72000),
        ('mix', ['mono32k.cstm', 'mono32k-falling.cstm'], [(ONE//2, ONE//2)]*2, True, 0, 72000),
        ('rate', ['mono48k.cstm', 'mono32k-falling.cstm'], [(ONE, 0), (ONE, ONE)], False, 0, 48000),
        ('pause', ['mono32k.cstm', 'mono32k-falling.cstm'], [(ONE, ONE)]*2, False, 0, 72000),
        ('seek', ['mono32k.cstm', 'mono32k-falling.cstm'], [(ONE, 0), (ONE, ONE)], False, 16000, 48000),
        ('repeat', ['mono32k.cstm', 'mono32k-falling.cstm'], [(ONE//2, ONE//2)]*2, True, 0, 72000)]
    for name, files, gains, both, seek, frames in configurations:
        pair = [(output/file).read_bytes() for file in files]
        control = [(1, v, *gain) for v, gain in enumerate(gains)]
        if seek:
            control.append((3, 0, seek, 0))
        control.append((2, 0, 1, 0))
        if both:
            control.append((2, 1, 1, 0))
        control += [(4, 0, min(4096, frames-begin), 0) for begin in range(0, frames, 4096)]
        model, pcm = Reference(pair), bytearray()
        for command in control:
            result = model.command(*command)
            if command[0] == 4:
                pcm += result[4:4+command[2]*4]
        rows.append((pair, control))
        wav_name = 'scene-'+name+'-48k.wav'
        write_wav(output/wav_name, pcm)
        scenes.append(dict(id=name, source_files=files, gains_q15=gains, seek_source_sample=seek,
                           playing_voices=2 if both else 1, output_frames=frames,
                           pcm_fnv1a32=fnv1a(pcm), pcm_sha256=hashlib.sha256(pcm).hexdigest(),
                           wav_file=wav_name, wav_sha256=hashlib.sha256((output/wav_name).read_bytes()).hexdigest(),
                           clipped_samples=model.clipped,
                           voice_rendered_frames=[v['rendered'] for v in model.voices],
                           voice_positions=[v['position'] for v in model.voices],
                           native_pause_or_console_verified=False))
    identity = dict(source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                    source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)))
    (output/'guided-scenes.json').write_text(json.dumps(dict(version='0.27.0', output_rate=RATE,
                                                           original_inputs_only=True, independent_reference=True,
                                                           scenes=scenes, **identity), indent=2)+'\n')
    vector = bytearray(b'AEM1'+struct.pack('<I', len(rows)))
    expected = bytearray(struct.pack('<I', len(rows)))
    frames = state_checks = clipping_cases = clipped_samples = 0
    for pair, control in rows:
        reference = Reference(pair)
        for data in pair:
            vector += struct.pack('<I', len(data))+data
        vector += struct.pack('<I', len(control))
        expected += struct.pack('<I', len(control))
        for command in control:
            vector += struct.pack('<4I', *command)
            expected += reference.command(*command)
            if command[0] == 4:
                frames += command[2]
        state_checks += len(control)
        clipping_cases += reference.clipped > 0
        clipped_samples += reference.clipped
    vectors = output/'original-vectors.bin';vectors.write_bytes(vector)
    env = {**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1', 'UBSAN_OPTIONS': 'halt_on_error=1:print_stacktrace=1'}
    for mode in modes:
        result = output/(mode+'.pcm-results')
        subprocess.run([str(binaries[mode]), str(vectors), str(result)], check=True, env=env)
        actual = result.read_bytes()
        if actual != expected:
            mismatch = next((i for i, (a, b) in enumerate(zip(actual, expected)) if a != b), min(len(actual), len(expected)))
            raise ValueError(f'PCM/control/counter differs from independent rational reference at byte {mismatch}; got {actual[mismatch:mismatch+16].hex()}, expected {expected[mismatch:mismatch+16].hex()}')
    report = dict(ok=True, version='0.27.0', original_inputs_only=True, independent_reference=True,
                  exact_rational_adpcm=True, exact_linear_resample_q15_mix=True,
                  exact_voice_position_phase_controls_counters=True, cases=len(rows), output_frames=frames,
                  pcm_samples=frames*2, state_checks=state_checks,
                  clipping_cases=clipping_cases, clipped_output_samples=clipped_samples,
                  counters_compared=['engine.rendered_frames', 'engine.clipped_samples',
                                     'voice.rendered_frames', 'voice.clipped_samples'],
                  sample_rates=RATES, modes=modes, diagnostic_fixtures=diagnostics, guided_scenes=scenes,
                  **identity,
                  compiler=subprocess.check_output([args.cc, '--version'], text=True).splitlines()[0],
                  original_vectors_sha256=hashlib.sha256(vector).hexdigest(), pcm_results_sha256=hashlib.sha256(expected).hexdigest(),
                  guest_dsp_or_console_acceptance=False)
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, sort_keys=True))


if __name__ == '__main__':
    main()
