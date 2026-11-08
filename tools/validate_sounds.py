"""PlatformIO pre-build: reject unsupported replacement WAVs before flashing."""
from pathlib import Path
import wave

Import('env')
root = Path(env.subst('$PROJECT_DIR'))
for name in ('record-start.wav', 'record-stop.wav'):
    with wave.open(str(root / 'assets/sounds' / name), 'rb') as audio:
        if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getcomptype()) != (1, 2, 16000, 'NONE'):
            raise ValueError(f'{name}: expected uncompressed PCM16 mono 16000 Hz')
        count = audio.getnframes()
        if not 0 < count <= 80000 or len(audio.readframes(count)) != count * 2:
            raise ValueError(f'{name}: expected a complete WAV up to 5 seconds')
