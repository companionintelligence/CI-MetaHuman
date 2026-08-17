"""Does the mouth ARTICULATE during the utterance?

Method, and why each piece is there -- every one of them exists because an earlier version of this
measurement produced a confident wrong answer:

  * APERTURE, not frame delta. A frame-difference over the face counts head motion, and the idle
    animation and gaze saccades move the whole head. Aperture (fraction of dark pixels in the lip
    box) responds to the mouth opening and closing and barely to the head translating.

  * REVERSALS, not variance. A mouth that opens once and stays open has high variance. Speech is
    an oscillation, so the metric is direction changes per second in the aperture signal.

  * A CONTROL REGION that cannot articulate. If the cheek/nose control also spikes during the
    speech window, whatever moved was the head, not the lips. Reported as a ratio.

  * The SPEECH WINDOW comes from the run log, not from a sleep. The first attempt at this slept a
    fixed interval and captured 10s AFTER the utterance had ended, then reported lip-sync working.

Usage: python lipcheck.py <shots_dir> [--speech START_MS END_MS]
"""
import csv
import os
import re
import statistics
import sys
from PIL import Image

SHOTS = sys.argv[1]

# Boxes in the CROPPED frame's coordinates. Verified visually by regions.py before trusting any
# number out of this -- a box that is actually chin and background reads as a dead mouth.
MOUTH = (142, 197, 212, 242)
CONTROL = (215, 150, 250, 182)   # cheek under the eye: moves with the head, not driven by visemes
DARK = 95


def aperture(path, box):
    im = Image.open(path).convert("L").crop(box)
    px = list(im.getdata())
    return 100.0 * sum(1 for p in px if p < DARK) / len(px)


def reversals_per_sec(series):
    """Direction changes per second in a lightly smoothed signal.

    Smoothed because single-pixel noise on a 105x70 box produces spurious direction changes at a
    rate that swamps the real one; the 3-tap mean is the smallest window that suppresses it.
    """
    if len(series) < 5:
        return 0.0
    vals = [v for _, v in series]
    sm = [statistics.mean(vals[max(0, i - 1):i + 2]) for i in range(len(vals))]
    turns, prev = 0, 0
    for a, b in zip(sm, sm[1:]):
        d = b - a
        if abs(d) < 0.05:      # deadband: ignore flat runs
            continue
        s = 1 if d > 0 else -1
        if prev and s != prev:
            turns += 1
        prev = s
    span = (series[-1][0] - series[0][0]) / 1000.0
    return turns / span if span > 0 else 0.0


def speech_window_from_log(shots):
    """Start/end of the utterance in elapsed-ms, read off the run log rather than assumed."""
    log = os.path.join(shots, "run.log")
    if not os.path.exists(log):
        return None
    txt = open(log, encoding="utf-8", errors="replace").read()
    # Track duration is stated by the runtime subsystem for every timing source.
    m = re.search(r"Lip-sync source=\S+[^\n]*?(\d+) spans over ([\d.]+)s", txt)
    if not m:
        m = re.search(r"Lip-sync source=\S+[^\n]*?measured ([\d.]+)s", txt)
        if not m:
            return None
    return float(m.group(2) if m.lastindex >= 2 else m.group(1))


rows = [(r["frame"], int(r["elapsed_ms"]))
        for r in csv.DictReader(open(os.path.join(SHOTS, "frames.csv")))]

if "--speech" in sys.argv:
    i = sys.argv.index("--speech")
    S0, S1 = int(sys.argv[i + 1]), int(sys.argv[i + 2])
else:
    print("!! no --speech window given; pass one read off the log")
    sys.exit(2)

# Frames before the level has rendered are black, and a black mouth box reads as 100% aperture --
# a fully "open" mouth that is really a loading screen. The face driver binds ~3.6s in, so 6s is a
# safe floor; without it the silence baseline is polluted by frames with no face in them at all.
FLOOR_MS = 6000
samples = []
for name, ms in rows:
    if ms < FLOOR_MS:
        continue
    p = os.path.join(SHOTS, name)
    if not os.path.exists(p):
        continue
    samples.append((ms, aperture(p, MOUTH), aperture(p, CONTROL)))

speech = [(ms, m, c) for ms, m, c in samples if S0 <= ms <= S1]
# Silence baseline: everything at least 1.5s clear of the window on either side, so the tail of
# the utterance does not leak into the "quiet" number.
quiet = [(ms, m, c) for ms, m, c in samples if ms < S0 - 1500 or ms > S1 + 1500]

if not speech or not quiet:
    print("!! not enough frames in one of the windows (speech=%d quiet=%d)" % (len(speech), len(quiet)))
    sys.exit(2)

mouth_spk = reversals_per_sec([(ms, m) for ms, m, _ in speech])
mouth_sil = reversals_per_sec([(ms, m) for ms, m, _ in quiet])
ctrl_spk = reversals_per_sec([(ms, c) for ms, _, c in speech])
ctrl_sil = reversals_per_sec([(ms, c) for ms, _, c in quiet])

print("frames: %d total, %d in speech window %d-%dms, %d quiet" %
      (len(samples), len(speech), S0, S1, len(quiet)))
print()
print("                     speech      silence     ratio")
print("  MOUTH  rev/s      %7.2f    %7.2f    %6.2fx" %
      (mouth_spk, mouth_sil, mouth_spk / mouth_sil if mouth_sil else float("inf")))
print("  control rev/s     %7.2f    %7.2f    %6.2fx" %
      (ctrl_spk, ctrl_sil, ctrl_spk / ctrl_sil if ctrl_sil else float("inf")))
print()
print("  aperture range in speech: %.2f-%.2f%% (mean %.2f)" % (
    min(m for _, m, _ in speech), max(m for _, m, _ in speech),
    statistics.mean([m for _, m, _ in speech])))
print("  aperture range in quiet : %.2f-%.2f%% (mean %.2f)" % (
    min(m for _, m, _ in quiet), max(m for _, m, _ in quiet),
    statistics.mean([m for _, m, _ in quiet])))
print()
# Two independent conditions, and both have to hold:
#
#   TEMPORAL -- the mouth moves more during the utterance than outside it. On its own this is not
#   enough, because the head also behaves differently while speaking (the gaze policy switches to
#   Speaking mode and saccades more), and an earlier version of this measurement reported exactly
#   that head motion as proof of articulation.
#
#   SPATIAL -- the motion is in the lips and not in the face carrying them. Compared as ABSOLUTE
#   rates rather than as a ratio-of-ratios: the control's silence rate is legitimately 0.00 here,
#   and dividing by it turns the best possible control into a division by zero that reads as a
#   failure.
mr = mouth_spk / mouth_sil if mouth_sil else float("inf")
temporal = mr > 2.0
spatial = mouth_spk > 4.0 * ctrl_spk
print("  temporal: mouth %.2f rev/s speaking vs %.2f quiet = %.2fx  -> %s" % (
    mouth_spk, mouth_sil, mr, "PASS" if temporal else "FAIL"))
print("  spatial : mouth %.2f rev/s vs control %.2f rev/s in the SAME window -> %s" % (
    mouth_spk, ctrl_spk, "PASS" if spatial else "FAIL"))
print()
print("VERDICT: %s" % ("mouth articulates" if temporal and spatial else "NOT DEMONSTRATED"))
