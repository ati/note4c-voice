#!/usr/bin/env python3
"""Run real portable firmware code with ASan/UBSan; optionally package a test WAV.

No network access. Output images are made from the actual 2-bpp framebuffer.
"""
import argparse
import email
import email.policy
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import wave
import zlib

ROOT = Path(__file__).resolve().parents[1]
BOUNDARY = 'Note4CVoice7dc6a680019c4321'


def png(raw, path):
    assert len(raw) == 30000
    rows = bytearray()
    for y in range(300):
        rows.append(0)
        for x in range(400):
            color = raw[y * 100 + x // 4] >> (6 - (x % 4) * 2) & 3
            assert color in (0, 2)
            # Screen RGB only approximates the panel's physical yellow.
            rows.extend((255, 225, 0) if color else (0, 0, 0))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 400, 300, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wav', type=Path, help='Optional mono PCM16 16kHz WAV to package with firmware code')
    parser.add_argument('--output', type=Path, default=ROOT / 'test-output')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / 'vendor/SOURCES.json').read_text())
    for path, entry in manifest.items():
        assert hashlib.sha256((ROOT / path).read_bytes()).hexdigest() == entry['sha256'], path
    with tempfile.TemporaryDirectory(prefix='note4c-voice-test-') as directory:
        build = Path(directory)
        executable = build / 'voice-test'
        subprocess.run(['c++', '-std=c++11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I', str(ROOT / 'include'), str(ROOT / 'test/voice_test.cpp'),
                        *[str(ROOT / 'src' / name) for name in ['voice_logic.cpp', 'canvas.cpp', 'fonts.cpp']],
                        '-o', str(executable)], check=True)
        command = [str(executable), str(output)]
        if args.wav:
            with wave.open(str(args.wav.resolve()), 'rb') as source:
                assert (source.getnchannels(), source.getsampwidth(), source.getframerate()) == (1, 2, 16000)
                assert 0 < source.getnframes() <= 480000
                pcm = source.readframes(source.getnframes())
            (build / 'speech.pcm').write_bytes(pcm)
            command += [str(build / 'speech.pcm'), str(output / 'request.multipart')]
        subprocess.run(command, check=True)
        sound_test = build / 'sound-test'
        subprocess.run(['c++', '-std=c++11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I', str(ROOT / 'include'), str(ROOT / 'test/sound_test.cpp'),
                        str(ROOT / 'src/sound.cpp'), str(ROOT / 'src/voice_logic.cpp'),
                        '-o', str(sound_test)], check=True)
        subprocess.run([str(sound_test), str(ROOT / 'assets/sounds/record-start.wav'),
                        str(ROOT / 'assets/sounds/record-stop.wav')], check=True)
        notebook_test = build / 'notebook-test'
        subprocess.run(['c++', '-std=c++11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I', str(ROOT / 'include'), str(ROOT / 'test/notebook_test.cpp'),
                        *[str(ROOT / 'src' / name) for name in
                          ['voice_logic.cpp', 'canvas.cpp', 'fonts.cpp', 'notebook.cpp', 'note_state.cpp', 'panel.cpp']],
                        '-o', str(notebook_test)], check=True)
        subprocess.run([str(notebook_test), str(output)], check=True)
        if args.wav:
            body = (output / 'request.multipart').read_bytes()
            message = email.message_from_bytes(
                f'Content-Type: multipart/form-data; boundary={BOUNDARY}\r\nMIME-Version: 1.0\r\n\r\n'.encode() + body,
                policy=email.policy.default)
            fields = {part.get_param('name', header='content-disposition'): part for part in message.iter_parts()}
            assert set(fields) == {'file', 'model', 'language', 'response_format'}
            for key, value in {'model': b'whisper-1', 'language': b'ru', 'response_format': b'json'}.items():
                assert fields[key].get_payload(decode=True) == value
            assert fields['file'].get_filename() == 'speech.wav'
            assert fields['file'].get_content_type() == 'audio/wav'
            wav_bytes = fields['file'].get_payload(decode=True)
            with wave.open(io.BytesIO(wav_bytes), 'rb') as wav:
                assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 16000)
                assert wav.readframes(wav.getnframes()) == pcm
            print(f'PASS: independent multipart/WAV parser, {len(body)} request bytes, PCM unchanged')
    for raw in sorted(output.glob('screen-*.raw')):
        png(raw.read_bytes(), raw.with_suffix('.png'))
    print('PASS: vendor/font SHA256; previews:', output)


if __name__ == '__main__':
    main()
