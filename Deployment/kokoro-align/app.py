"""Kokoro TTS that also returns the alignment the model already computed.

WHY THIS EXISTS
    Kokoro is StyleTTS2: a duration predictor decides how many frames each phoneme occupies,
    and the length regulator expands the phoneme encodings by exactly those durations before
    the vocoder runs. So the model KNOWS where every phoneme lands in the waveform it just
    produced. That is ground truth for lip-sync, not an estimate.

    We were throwing it away. OpenAI's /v1/audio/speech schema returns audio bytes and nothing
    else, so serving Kokoro through an OpenAI-compatible endpoint (AMD Lemonade, in our case)
    structurally discards the durations at the HTTP boundary. Verified against our own box:
    Content-Type: audio/wav, no timing headers, /audio/speech/timestamps -> 404.

    The consequence downstream was that FVisemePlanner had to GUESS timing from an assumed
    165wpm and then uniformly rescale to the clip length -- which fixes total drift but not
    the distribution, so every phoneme got time proportional to its spelling rather than to
    how long it was actually voiced.

    This service returns both. It is deliberately additive: Lemonade keeps serving chat, STT
    and plain audio, and this answers only the one question Lemonade structurally cannot.

MEASURED (beta-1, CPU only, no ROCm needed for an 82M model):
    "Welcome to Companion Intelligence." -> 34 phonemes, 499ms inference for 3.02s of audio
    (6.1x realtime); durations summed to 3.05s against 3.02s of emitted audio, a 1% match.
"""
import base64, io, os, struct, time
import numpy as np
import onnxruntime as ort
from fastapi import FastAPI
from pydantic import BaseModel
from huggingface_hub import hf_hub_download
import json

REPO = os.environ.get("KOKORO_REPO", "onnx-community/Kokoro-82M-v1.0-ONNX-timestamped")
# model.onnx, NOT model_q8f16.onnx: the q8f16 quantization SEGFAULTS the CPU execution
# provider on load (exit 139, no Python traceback). Full precision is 6x realtime here, so
# the quantized variants buy nothing and cost stability.
MODEL_FILE = os.environ.get("KOKORO_MODEL_FILE", "onnx/model.onnx")
SAMPLE_RATE = 24000
# Kokoro emits 24kHz audio at 40 duration-frames per second, so seconds = frames / 40.
# Confirmed empirically rather than assumed: predicted durations summed to 3.05s against
# 3.02s of actual audio.
FRAME_RATE = 40.0

app = FastAPI(title="kokoro-align")
_sess = None
_vocab = None
_g2p = None
_voices = {}


def _load():
    global _sess, _vocab, _g2p
    if _sess is None:
        _sess = ort.InferenceSession(hf_hub_download(REPO, MODEL_FILE),
                                     providers=["CPUExecutionProvider"])
        tok = json.load(open(hf_hub_download(REPO, "tokenizer.json")))
        _vocab = tok["model"]["vocab"]
        from misaki import en, espeak
        # misaki is Kokoro's OWN phonemizer, so its IPA symbol set matches the model vocab
        # exactly. A generic G2P would emit symbols the vocab lacks, and those tokens would
        # be dropped silently -- shortening the audio with no error anywhere.
        #
        # The espeak fallback is NOT optional. misaki's dictionary is closed-vocabulary, and
        # without a fallback an out-of-vocabulary word is not flagged -- it is mangled or
        # dropped. Caught on our own opening line: "Welcome to Companion Intelligence, I am
        # Eliza. How can I help you?" phonemized to "...ɪntˈɛləʤᵊns, ˌI æm . hˌW kæn..." --
        # the brand name ELIZA vanished entirely, replaced by a bare full stop. Since phonemes
        # drive both the waveform and the alignment, the character silently failed to say her
        # own name and nothing anywhere reported an error. With the fallback: ɪlˈIzə.
        #
        # Note for anyone reading that phoneme string and reaching for a bug report, as I did:
        # misaki writes DIPHTHONGS as capitals, so hˌW is /haʊ/ ("how") and ˌI is /aɪ/ ("I").
        # Those are correct output, not mangled letters.
        try:
            _g2p = en.G2P(trf=False, british=False, fallback=espeak.EspeakFallback(british=False))
        except Exception as exc:  # espeak-ng missing: degrade loudly rather than silently
            print(f"WARNING: espeak fallback unavailable ({exc}); OOV words will be mangled")
            _g2p = en.G2P(trf=False, british=False)
    return _sess, _vocab, _g2p


def _voice(name: str) -> np.ndarray:
    if name not in _voices:
        # Voice packs are indexed BY TOKEN COUNT -- style is length-dependent in StyleTTS2,
        # so the row must be chosen per utterance, not once at load.
        _voices[name] = np.fromfile(hf_hub_download(REPO, f"voices/{name}.bin"),
                                    dtype=np.float32).reshape(-1, 1, 256)
    return _voices[name]


def _wav_bytes(samples: np.ndarray) -> bytes:
    pcm = np.clip(samples, -1.0, 1.0)
    pcm = (pcm * 32767.0).astype("<i2").tobytes()
    hdr = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack(
        "<IHHIIHH", 16, 1, 1, SAMPLE_RATE, SAMPLE_RATE * 2, 2, 16) + b"data" + struct.pack("<I", len(pcm))
    return hdr + pcm


class SpeakRequest(BaseModel):
    text: str
    voice: str = "af_sky"
    speed: float = 1.0


@app.get("/health")
def health():
    return {"ok": True, "model": MODEL_FILE, "frame_rate": FRAME_RATE}


@app.post("/speak")
def speak(req: SpeakRequest):
    sess, vocab, g2p = _load()
    t0 = time.time()

    phonemes, _ = g2p(req.text)
    ids = [vocab[c] for c in phonemes if c in vocab]
    kept = [c for c in phonemes if c in vocab]
    if not ids:
        return {"error": "no pronounceable tokens", "text": req.text}

    pack = _voice(req.voice)
    # Pad token at both ends, mirroring Kokoro's own pipeline.
    inp = np.array([[0, *ids, 0]], dtype=np.int64)
    style = pack[min(inp.shape[1] - 1, pack.shape[0] - 1)]
    wav, dur = sess.run(None, {
        "input_ids": inp,
        "style": style,
        "speed": np.array([req.speed], dtype=np.float32),
    })
    wav = wav.squeeze()
    dur = np.asarray(dur).squeeze().astype(float)

    # Align the duration vector back onto the symbols. inp carries a pad at each end, so the
    # interior maps 1:1 onto the kept phonemes; anything left over is trailing silence, which
    # the client wants explicitly so it can close the mouth at the right moment.
    spans, t = [], 0.0
    for i, frames in enumerate(dur):
        seconds = float(frames) / FRAME_RATE
        sym = kept[i - 1] if 1 <= i <= len(kept) else ""
        spans.append({"symbol": sym, "start": round(t, 4),
                      "duration": round(seconds, 4), "frames": float(frames)})
        t += seconds

    return {
        "audio_wav_b64": base64.b64encode(_wav_bytes(wav)).decode("ascii"),
        "sample_rate": SAMPLE_RATE,
        "audio_seconds": round(len(wav) / SAMPLE_RATE, 4),
        "phonemes": "".join(kept),
        "frame_rate": FRAME_RATE,
        "spans": spans,
        "infer_ms": round((time.time() - t0) * 1000, 1),
    }
