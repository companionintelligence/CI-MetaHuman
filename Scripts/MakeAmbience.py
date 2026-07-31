#!/usr/bin/env python3
"""Synthesizes the machine-room score as a seamless loop. Stdlib only.

Generated rather than sourced, for three reasons: there is no licence question about shipping
it, it can be tuned to this specific room, and a loop that is *computed* can be made to join
perfectly instead of relying on an editor's crossfade.

Two layers, mixed quietly enough to sit under speech without ducking:

  ROOM TONE  - the mainframe itself. Mains hum at 50Hz with its harmonics (a 1970s room is full
               of transformers), plus band-limited noise standing in for fans and tape transports.
  PAD        - a slow minor-9th drone. Sparse and unmoving on purpose: this is a bed, not a cue.
               Anything with a melody would start competing with dialogue within a minute.

Every partial is placed at an EXACT integer number of cycles per loop, which is what makes the
seam inaudible -- each sine ends precisely where it began, so no crossfade is needed and the
join cannot drift or click.

    python Scripts/MakeAmbience.py --seconds 32 --out Deployment/audio/machine_room.wav
"""
import argparse, math, os, random, struct, wave

SAMPLE_RATE = 44100


def _cycles(freq: float, seconds: float) -> float:
    """Nearest frequency to `freq` that completes a whole number of cycles in `seconds`.

    This is the entire loop trick: a partial at a non-integer cycle count is discontinuous at the
    seam and clicks on every repeat.
    """
    n = max(1, round(freq * seconds))
    return n / seconds


def synth(seconds: float, seed: int = 7) -> list:
    rng = random.Random(seed)
    total = int(SAMPLE_RATE * seconds)
    buf = [0.0] * total

    # --- room tone: mains hum + harmonics -------------------------------------------------
    # 50Hz rather than 60: the reference photographs are of European installations, and the
    # lower fundamental sits further from speech and reads as "big machine" rather than "buzz".
    for harmonic, amp in ((1, 0.055), (2, 0.022), (3, 0.011), (4, 0.006), (6, 0.003)):
        f = _cycles(50.0 * harmonic, seconds)
        phase = rng.uniform(0, math.tau)
        for i in range(total):
            buf[i] += amp * math.sin(math.tau * f * i / SAMPLE_RATE + phase)

    # --- fans and tape: band-limited noise -------------------------------------------------
    # A one-pole low-pass over white noise, then a gentle high-pass by subtracting a slower
    # follower. Cheap, but it lands in the right band and avoids a hiss that fights consonants.
    lp, hp_follow = 0.0, 0.0
    noise = [rng.uniform(-1.0, 1.0) for _ in range(total)]
    for i in range(total):
        lp += 0.045 * (noise[i] - lp)            # ~300Hz roll-off
        hp_follow += 0.0009 * (lp - hp_follow)   # remove the very low rumble
        buf[i] += 0.030 * (lp - hp_follow)

    # --- pad: minor-9th drone --------------------------------------------------------------
    # A/C/E/B — minor with the 9th on top. Unresolved, so it never sounds like it is going
    # anywhere, which is what a bed under an open-ended conversation needs.
    for freq, amp in ((110.00, 0.030), (130.81, 0.024), (164.81, 0.020), (246.94, 0.014)):
        f = _cycles(freq, seconds)
        phase = rng.uniform(0, math.tau)
        # Each voice breathes on its own slow cycle, also cycle-locked so the seam stays clean.
        lfo = _cycles(rng.uniform(0.05, 0.11), seconds)
        lfo_phase = rng.uniform(0, math.tau)
        for i in range(total):
            t = i / SAMPLE_RATE
            swell = 0.60 + 0.40 * math.sin(math.tau * lfo * t + lfo_phase)
            buf[i] += amp * swell * math.sin(math.tau * f * t + phase)

    # --- normalize with headroom ------------------------------------------------------------
    # -18dBFS or so: this must sit UNDER Kokoro's speech. Mixing it hot is how ambience turns
    # into an obstacle that then needs ducking to undo.
    peak = max(abs(v) for v in buf) or 1.0
    target = 0.126
    gain = target / peak
    return [v * gain for v in buf]


def write_wav(path: str, samples: list) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SAMPLE_RATE)
        w.writeframes(b"".join(
            struct.pack("<h", max(-32768, min(32767, int(v * 32767)))) for v in samples))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=32.0)
    ap.add_argument("--out", default="Deployment/audio/machine_room.wav")
    args = ap.parse_args()

    samples = synth(args.seconds)
    write_wav(args.out, samples)

    # Report the seam error: the whole point of the cycle-locking above is that this is ~0.
    seam = abs(samples[0] - samples[-1])
    rms = math.sqrt(sum(v * v for v in samples) / len(samples))
    print("%s  %.0fs @ %dHz mono" % (args.out, args.seconds, SAMPLE_RATE))
    print("  rms %.4f (%.1f dBFS)   seam discontinuity %.5f" % (rms, 20 * math.log10(rms), seam))


if __name__ == "__main__":
    main()
