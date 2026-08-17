#!/usr/bin/env python3
"""End-to-end conversation test for the virtual-being stack.

Runs N conversation turns through the WHOLE voice loop and checks each stage:

    prompt -> LLM (Ollama) -> reply text -> TTS (Kokoro) -> wav -> STT (Whisper) -> transcript

The round trip is the point. Every previous test in this project checks one component in
isolation with a mocked neighbour, which is exactly why the stack could have 149 green tests and
still not talk: the failures that actually bit us lived BETWEEN components -- a backend selected by
the wrong env var, a reasoning model answering in 16s, an endpoint under /api/v1 instead of /v1.
Speaking a reply and then listening to it back is the cheapest way to exercise all of those joins
at once, and it fails loudly when any single one regresses.

Transcribing our own synthesis is deliberately a WEAK check of speech quality (Whisper hears a
clean signal with no room or microphone) but a STRONG check of plumbing: wrong sample rate, empty
audio, truncated clip, mismatched format or a silently-500ing service all surface as garbled or
missing text.

Usage (run it where the services are reachable -- beta-1 hosts both):
    python3 RunConversationE2E.py
    python3 RunConversationE2E.py --turns 10 --ollama http://127.0.0.1:11434 \
        --lemonade http://127.0.0.1:13305 --model qwen3.8:27b

Exit code is 0 only if every turn passes.
"""

import argparse
import json
import re
import statistics
import sys
import time
import urllib.request
import uuid

# Prompts chosen to vary the SHAPE of the reply, not just its topic, because the failure modes
# differ: a long answer exposes viseme-track drift, a numeric one exposes tokenizer/TTS
# mishandling of digits, and a refusal-ish prompt exposes empty or one-word replies that would
# silently produce a zero-length audio clip.
DEFAULT_PROMPTS = [
    "Greet a visitor to your 1970s computer room in one short sentence.",
    "What does the tape drive behind you do? Answer in two sentences.",
    "Say a single short sentence about punch cards.",
    "Count from one to five, as words, in one sentence.",
    "Describe the sound of this room in one vivid sentence.",
    "A visitor asks if you are lonely here. Answer warmly in two sentences.",
    "Name three machines you can see. One sentence.",
    "Explain what a mainframe is to a child, in two short sentences.",
    "Tell me the year and what computing felt like then, in one sentence.",
    "Say goodbye to a visitor in one short sentence.",
]

SYSTEM_PROMPT = (
    "You are the resident of a 1970s mainframe computer room. "
    "Speak warmly and briefly. Never use lists, markdown, emoji or stage directions -- "
    "your words are spoken aloud."
)


def post_json(url, payload, timeout):
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


def post_bytes(url, payload, timeout):
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode(), headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def post_multipart_wav(url, wav_bytes, fields, timeout):
    """Minimal multipart encoder -- avoids a `requests` dependency on a bare server."""
    boundary = "----e2e" + uuid.uuid4().hex
    parts = []
    for k, v in fields.items():
        parts.append(
            f"--{boundary}\r\nContent-Disposition: form-data; name=\"{k}\"\r\n\r\n{v}\r\n".encode())
    parts.append(
        f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"speech.wav\""
        f"\r\nContent-Type: audio/wav\r\n\r\n".encode() + wav_bytes + b"\r\n")
    parts.append(f"--{boundary}--\r\n".encode())
    body = b"".join(parts)
    req = urllib.request.Request(url, data=body, headers={
        "Content-Type": f"multipart/form-data; boundary={boundary}"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode())


_NUMBER_WORDS = {
    "zero": "0", "one": "1", "two": "2", "three": "3", "four": "4", "five": "5", "six": "6",
    "seven": "7", "eight": "8", "nine": "9", "ten": "10", "eleven": "11", "twelve": "12",
}


def normalize(text):
    """Compare on spoken words only. Whisper restores its own punctuation and casing, and neither
    is something the mouth or the listener cares about.

    Number words are folded to digits because Whisper transcribes spoken "one, two, three" as
    "1, 2, 3". That is CORRECT transcription of correct speech, but a naive word comparison scores
    it 0.0 and fails a turn where the pipeline did everything right -- which is exactly what
    happened on the counting prompt the first time this ran."""
    words = re.sub(r"[^a-z0-9 ]+", " ", text.lower()).split()
    return [_NUMBER_WORDS.get(w, w) for w in words if w]


def similarity(a, b):
    """Order-insensitive word overlap. Deliberately not an exact-match check: a transcript that
    drops a comma or hears 'nineteen seventy' for '1970' is a healthy pipeline, whereas the
    failures worth catching (silence, truncation, wrong clip) destroy most of the words."""
    wa, wb = normalize(a), normalize(b)
    if not wa or not wb:
        return 0.0
    common = 0
    pool = list(wb)
    for w in wa:
        if w in pool:
            pool.remove(w)
            common += 1
    return 2.0 * common / (len(wa) + len(wb))


def wav_duration(b):
    """Parse the RIFF header rather than trusting a reported length: a truncated or empty clip is
    precisely the failure this test exists to catch."""
    try:
        if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
            return 0.0
        i = 12
        rate = channels = bits = 0
        while i + 8 <= len(b):
            cid, size = b[i:i + 4], int.from_bytes(b[i + 4:i + 8], "little")
            if cid == b"fmt ":
                channels = int.from_bytes(b[i + 10:i + 12], "little")
                rate = int.from_bytes(b[i + 12:i + 16], "little")
                bits = int.from_bytes(b[i + 22:i + 24], "little")
            elif cid == b"data" and rate and channels and bits:
                # NEVER trust the declared size: Kokoro streams and writes 0xFFFFFFFF, which
                # reported every clip as 44739s. Measure what actually arrived instead.
                actual = min(size, len(b) - (i + 8))
                return actual / (rate * channels * (bits // 8))
            i += 8 + size + (size & 1)
    except Exception:
        pass
    return 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ollama", default="http://127.0.0.1:11434")
    ap.add_argument("--lemonade", default="http://127.0.0.1:13305")
    ap.add_argument("--model", default="qwen3.8:27b")
    ap.add_argument("--tts-model", default="kokoro-v1")
    ap.add_argument("--stt-model", default="Whisper-Base")
    ap.add_argument("--voice", default="af_sky")
    ap.add_argument("--turns", type=int, default=10)
    ap.add_argument("--min-similarity", type=float, default=0.60)
    ap.add_argument("--timeout", type=float, default=300.0)
    # Exercise ONLY the cognition leg.
    #
    # The TTS and STT legs both go to Lemonade, which on the reference deployment lives on beta-1
    # and reaches core-2 through an SSH tunnel. On any machine without that tunnel -- a developer
    # laptop with a local Ollama, which is the ordinary case -- every turn failed at the TTS step
    # and there was no command line that could make this script pass. That made the single most
    # useful test in the repo (the one that found the WAV segfault) unrunnable exactly where someone
    # is most likely to want it.
    #
    # Turns are still gated: the LLM assertions (non-empty reply, no leaked reasoning) are the ones
    # that hold, and the voice columns simply read "-".
    ap.add_argument("--llm-only", action="store_true",
                    help="skip the TTS/STT legs and assert only the LLM turn (no Lemonade needed)")
    args = ap.parse_args()

    prompts = (DEFAULT_PROMPTS * ((args.turns // len(DEFAULT_PROMPTS)) + 1))[:args.turns]
    rows, failures = [], []
    # History is carried between turns so this exercises a real CONVERSATION, not ten unrelated
    # one-shots -- context growth is itself a latency and correctness risk.
    history = [{"role": "system", "content": SYSTEM_PROMPT}]

    print(f"Conversation E2E: {args.turns} turns" + (" (LLM only)" if args.llm_only else ""))
    print(f"  LLM  {args.model} @ {args.ollama}")
    if args.llm_only:
        print("  TTS  skipped (--llm-only)")
        print("  STT  skipped (--llm-only)\n")
    else:
        print(f"  TTS  {args.tts_model} @ {args.lemonade}")
        print(f"  STT  {args.stt_model} @ {args.lemonade}\n")

    for i, prompt in enumerate(prompts, 1):
        row = {"n": i, "prompt": prompt}
        try:
            history.append({"role": "user", "content": prompt})
            t0 = time.time()
            # think=false: reasoning is ~20x latency for output that is never spoken.
            chat = post_json(f"{args.ollama}/api/chat", {
                "model": args.model, "messages": history, "stream": False, "think": False,
                "keep_alive": "30m"}, args.timeout)
            row["llm_ms"] = int((time.time() - t0) * 1000)
            reply = (chat.get("message", {}) or {}).get("content", "").strip()
            row["reply"] = reply
            history.append({"role": "assistant", "content": reply})
            if not reply:
                raise RuntimeError("empty LLM reply")
            # A reasoning model leaking <think> into content would be spoken aloud verbatim.
            if "<think>" in reply.lower():
                raise RuntimeError("reasoning leaked into spoken content")

            if args.llm_only:
                # The cognition assertions above have already run and are the whole gate here.
                row["ok"] = True
                rows.append(row)
                print(f"[PASS] turn {i:>2}  llm={row['llm_ms']}ms (llm-only)")
                print(f"        said: {reply[:96]}")
                continue

            t0 = time.time()
            wav = post_bytes(f"{args.lemonade}/api/v1/audio/speech", {
                "model": args.tts_model, "input": reply, "voice": args.voice,
                "response_format": "wav"}, args.timeout)
            row["tts_ms"] = int((time.time() - t0) * 1000)
            row["wav_bytes"] = len(wav)
            row["audio_s"] = round(wav_duration(wav), 2)
            if row["audio_s"] <= 0.05:
                raise RuntimeError(f"no decodable audio ({len(wav)} bytes)")

            t0 = time.time()
            stt = post_multipart_wav(f"{args.lemonade}/api/v1/audio/transcriptions", wav,
                                     {"model": args.stt_model}, args.timeout)
            row["stt_ms"] = int((time.time() - t0) * 1000)
            row["transcript"] = (stt.get("text") or "").strip()
            row["similarity"] = round(similarity(reply, row["transcript"]), 3)

            # Rough speaking-rate sanity: the viseme planner assumes ~165 wpm, and a wildly
            # different real rate is what makes an estimated mouth track drift against audio.
            words = len(normalize(reply))
            row["wpm"] = int(words / (row["audio_s"] / 60.0)) if row["audio_s"] > 0 else 0

            row["ok"] = row["similarity"] >= args.min_similarity
            if not row["ok"]:
                raise RuntimeError(f"round-trip similarity {row['similarity']} < {args.min_similarity}")
        except Exception as e:
            row["ok"] = False
            row["error"] = str(e)[:120]
            failures.append((i, row.get("error", "?")))

        rows.append(row)
        mark = "PASS" if row.get("ok") else "FAIL"
        print(f"[{mark}] turn {i:>2}  llm={row.get('llm_ms','-')}ms tts={row.get('tts_ms','-')}ms "
              f"stt={row.get('stt_ms','-')}ms audio={row.get('audio_s','-')}s "
              f"sim={row.get('similarity','-')} wpm={row.get('wpm','-')}")
        print(f"        said: {row.get('reply','')[:96]}")
        if row.get("transcript"):
            print(f"        heard: {row['transcript'][:96]}")
        if row.get("error"):
            print(f"        error: {row['error']}")

    ok = [r for r in rows if r.get("ok")]
    print(f"\n{len(ok)}/{len(rows)} turns passed")
    if ok:
        for k, label in (("llm_ms", "LLM"), ("tts_ms", "TTS"), ("stt_ms", "STT")):
            vals = [r[k] for r in ok if k in r]
            if vals:
                print(f"  {label:<4} median {int(statistics.median(vals)):>6}ms   "
                      f"min {min(vals):>6}ms   max {max(vals):>6}ms")
        sims = [r["similarity"] for r in ok if "similarity" in r]
        if sims:
            print(f"  round-trip similarity: median {statistics.median(sims):.3f}  min {min(sims):.3f}")
    for n, err in failures:
        print(f"  turn {n} failed: {err}")

    with open("conversation_e2e_results.json", "w") as f:
        json.dump(rows, f, indent=1)
    print("  results -> conversation_e2e_results.json")
    return 0 if len(ok) == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
