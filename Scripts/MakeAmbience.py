#!/usr/bin/env python3
"""Synthesizes the set's two ambience beds as seamless loops. Stdlib only.

Generated rather than sourced, for three reasons: there is no licence question about shipping
them, they can be tuned to this specific room, and a loop that is *computed* can be made to join
perfectly instead of relying on an editor's crossfade.

TWO MODES, because the set plays two different things for two different jobs:

  --mode score     the machine-room SCORE. Non-spatialized, placed at the camera, it is the bed
                   the whole scene sits on. 44.1kHz, 32s.
                       python Scripts/MakeAmbience.py --mode score
  --mode roomtone  the machine hall's own NOISE FLOOR. A positioned emitter out in the hall, so
                   it has somewhere to be in the stereo field and gets quieter as the camera
                   pulls away from it. 48kHz to match UE's mixer rate exactly (no resample), 20s.
                       python Scripts/MakeAmbience.py --mode roomtone

Layers, all mixed quietly enough to sit under speech without ducking:

  score:    HUM  - the mainframe itself. Mains at 50Hz with its harmonics (a 1970s room is full
                   of transformers), plus band-limited noise standing in for fans and tape.
            PAD  - a slow minor-9th drone. Sparse and unmoving on purpose: this is a bed, not a
                   cue. Anything with a melody would compete with dialogue within a minute.
  roomtone: HUM  - 60Hz, not 50. The score's 50Hz is the European reference; this one is the
                   ROOM, and the room is the reference photographs of American installations.
                   The two beds therefore beat slowly against each other instead of doubling
                   into one fat tone, which is what stops the pair sounding like a single synth.
            AIR  - blower rumble and duct rush. No pad, no pitch: the moment this layer has a
                   note in it, it stops being a floor and starts being music.

Every partial is placed at an EXACT integer number of cycles per loop, which is what makes the
seam inaudible -- each sine ends precisely where it began, so no crossfade is needed and the
join cannot drift or click.

NOISE cannot be cycle-locked that way, and in the score its seam is simply small enough to hide
under the sines. The room tone has no pad to hide behind, so its noise is filtered CIRCULARLY
instead (see _circular_lp): the filter is run twice around the same periodic input and only the
second lap is kept, so the filter state at sample 0 IS the state it ends on. That makes the
filtered noise exactly periodic rather than approximately so.
"""
import argparse, math, os, random, struct, wave

SAMPLE_RATE = 44100      # the score. 44.1k is fine for a bed that is resampled once at cook time.
ROOM_TONE_RATE = 48000   # the room tone. UE's mixer runs at 48k; matching it avoids a resample.


def _cycles(freq: float, seconds: float) -> float:
    """Nearest frequency to `freq` that completes a whole number of cycles in `seconds`.

    This is the entire loop trick: a partial at a non-integer cycle count is discontinuous at the
    seam and clicks on every repeat.
    """
    n = max(1, round(freq * seconds))
    return n / seconds


def synth(seconds: float, seed: int = 7, rate: int = SAMPLE_RATE) -> list:
    rng = random.Random(seed)
    total = int(rate * seconds)
    buf = [0.0] * total

    # --- room tone: mains hum + harmonics -------------------------------------------------
    # 50Hz rather than 60: the reference photographs are of European installations, and the
    # lower fundamental sits further from speech and reads as "big machine" rather than "buzz".
    for harmonic, amp in ((1, 0.055), (2, 0.022), (3, 0.011), (4, 0.006), (6, 0.003)):
        f = _cycles(50.0 * harmonic, seconds)
        phase = rng.uniform(0, math.tau)
        for i in range(total):
            buf[i] += amp * math.sin(math.tau * f * i / rate + phase)

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
            t = i / rate
            swell = 0.60 + 0.40 * math.sin(math.tau * lfo * t + lfo_phase)
            buf[i] += amp * swell * math.sin(math.tau * f * t + phase)

    # --- normalize with headroom ------------------------------------------------------------
    # -18dBFS or so: this must sit UNDER Kokoro's speech. Mixing it hot is how ambience turns
    # into an obstacle that then needs ducking to undo.
    peak = max(abs(v) for v in buf) or 1.0
    target = 0.126
    gain = target / peak
    return [v * gain for v in buf]


def _circular_lp(x: list, coeff: float, laps: int = 2) -> list:
    """One-pole low-pass run round a PERIODIC input, keeping only the final lap.

    A filter started from silence has a transient at the head that does not appear at the tail,
    so the loop point clicks -- and no amount of tuning the noise itself fixes it, because the
    discontinuity is in the filter STATE, not in the signal. Running the same period through
    first and keeping the second pass means the state entering sample 0 is the state leaving the
    last sample, so the output is exactly periodic. Two laps is enough by a wide margin: the
    one-pole converges in ~1/coeff samples and the shortest loop here is 960,000 of them.
    """
    n = len(x)
    s = 0.0
    for _ in range(laps - 1):
        for i in range(n):
            s += coeff * (x[i] - s)
    out = [0.0] * n
    for i in range(n):
        s += coeff * (x[i] - s)
        out[i] = s
    return out


def synth_roomtone(seconds: float, seed: int = 13, rate: int = ROOM_TONE_RATE) -> list:
    """The hall's noise floor: mains hum and moving air, and deliberately nothing else.

    This plays UNDER the score from a position out in the machine hall, so it carries no pitched
    material at all. Two beds with notes in them would have to agree about key; two beds where
    only one has notes never can disagree.
    """
    rng = random.Random(seed)
    total = int(rate * seconds)
    buf = [0.0] * total

    # --- mains hum: 60Hz and its harmonics -----------------------------------------------------
    # 60 rather than the score's 50 (see the module docstring). Odd harmonics carry more weight
    # than even ones because a saturating transformer core clips symmetrically -- that is the
    # difference between "transformer" and "sine generator".
    for harmonic, amp in ((1, 0.070), (2, 0.020), (3, 0.026), (4, 0.008), (5, 0.011), (6, 0.004)):
        f = _cycles(60.0 * harmonic, seconds)
        phase = rng.uniform(0, math.tau)
        for i in range(total):
            buf[i] += amp * math.sin(math.tau * f * i / rate + phase)

    # --- blowers: low rumble ------------------------------------------------------------------
    # The 729s and the 1401 are all air-cooled, and what a room full of them actually sounds like
    # is a broad low roar with no attack anywhere in it.
    noise = [rng.uniform(-1.0, 1.0) for _ in range(total)]
    rumble = _circular_lp(noise, 0.020)                  # ~150Hz roll-off
    sub = _circular_lp(rumble, 0.0006)                   # the DC/infrasonic part, removed below
    for i in range(total):
        buf[i] += 0.85 * (rumble[i] - sub[i])

    # --- ducts: the air itself ----------------------------------------------------------------
    # A second, brighter noise band. Without it the tone is all rumble and reads as a truck idling
    # outside; the rush is what puts the listener INSIDE the room.
    noise2 = [rng.uniform(-1.0, 1.0) for _ in range(total)]
    rush = _circular_lp(noise2, 0.16)                    # ~1.3kHz
    rush_lo = _circular_lp(rush, 0.012)                  # high-pass by difference
    for i in range(total):
        buf[i] += 0.14 * (rush[i] - rush_lo[i])

    # --- normalize -----------------------------------------------------------------------------
    # Quieter than the score (0.126 there). This is a SECOND bed layered on top of the first, and
    # two ambiences mixed at bed level make one loud ambience -- which is how a room ends up
    # needing ducking against speech that was perfectly audible over either layer alone.
    peak = max(abs(v) for v in buf) or 1.0
    target = 0.085
    gain = target / peak
    return [v * gain for v in buf]


def write_wav(path: str, samples: list, rate: int = SAMPLE_RATE) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(
            struct.pack("<h", max(-32768, min(32767, int(v * 32767)))) for v in samples))


# mode -> (synth fn, default seconds, rate, default output path)
MODES = {
    "score":    (synth,          32.0, SAMPLE_RATE,    "Deployment/audio/machine_room.wav"),
    "roomtone": (synth_roomtone, 20.0, ROOM_TONE_RATE, "Deployment/generated/S_RoomTone.wav"),
}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=sorted(MODES), default="score")
    ap.add_argument("--seconds", type=float, default=None)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    fn, seconds, rate, out = MODES[args.mode]
    seconds = args.seconds if args.seconds is not None else seconds
    out = args.out if args.out is not None else out

    samples = fn(seconds, rate=rate)
    write_wav(out, samples, rate)

    # Report the seam error RELATIVE to the loop's own slew rate, not as an absolute number.
    # A raw |x[0] - x[-1]| is unreadable: the wrap is a normal one-sample step, so a bed with
    # more high-frequency content scores "worse" while looping better. What decides whether the
    # join clicks is whether that step stands out from every other step in the file -- so the
    # useful figure is the ratio, and ~1x means the seam is indistinguishable from the signal.
    steps = [abs(samples[i + 1] - samples[i]) for i in range(0, len(samples) - 1, 7)]
    steps.sort()
    median_step = steps[len(steps) // 2] or 1e-9
    seam = abs(samples[0] - samples[-1])
    rms = math.sqrt(sum(v * v for v in samples) / len(samples))
    print("%s  %.0fs @ %dHz mono" % (out, seconds, rate))
    print("  rms %.4f (%.1f dBFS)   seam step %.2fx the median step (~1x = inaudible join)"
          % (rms, 20 * math.log10(rms), seam / median_step))


if __name__ == "__main__":
    main()
