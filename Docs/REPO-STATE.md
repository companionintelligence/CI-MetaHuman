# Repository state review

> 2026-07-31. A snapshot of branches, working tree, UI surface and verification status. Facts were
> read from the repo and the running system, not recalled — where something is unverified it says so.

---

## 1. Branches

| Branch | Upstream | Relationship to `main` | State |
|---|---|---|---|
| **`main`** | `origin/main` | — | **Default branch. The only branch, local or remote.** |
| ~~`feat/companion-face-driver`~~ | — | 0 unique commits | **Deleted.** |
| ~~`feat/companion-stage-viseme-groundwork`~~ | — | 0 unique commits | **Deleted.** |

`git ls-remote` shows origin now carries **only `main`** — and in fact carried only `main` before
this cleanup too, so the earlier claim that these existed "on the remote" was wrong: GitHub had
already removed them on merge. What remained were local branches tracking upstreams that no longer
existed, which is why `git branch -d` refused one of them: it could not verify against a deleted
upstream and defaulted to treating it as unmerged. `git merge-base --is-ancestor` confirmed both
tips were fully contained in `main` before forcing.

`git branch --merged main` lists both feature branches, so **no work is stranded** — everything has
landed on `main`. All four PRs (#1–#4) are closed.

The five commits after the PR #4 merge were made **directly to `main`**, so that is the working
pattern now rather than branch-per-change.

Both deleted. Verified as ancestors of `main` first — `--merged` alone was not trustworthy here,
since it disagreed with `-d` on the same branch.

## 2. Working tree — uncommitted

13 modified files plus 3 new ones, all one feature: **a Kokoro phoneme aligner**.

```
new   Public/Voice/KokoroPhonemeAligner.h          (72 lines)
new   Private/Voice/KokoroPhonemeAligner.cpp      (193 lines)
new   Private/Tests/KokoroPhonemeAlignerSpec.cpp
mod   Voice/VisemePlanner.{h,cpp}                  PlanFromAlignment + VisemeForIpa
mod   Avatar/CompanionRuntimeSubsystem.{h,cpp}     route replies through the aligner
mod   Settings/MetaHumanLLMSettings.h              KokoroAlignerUrl
mod   Settings/MetaHumanLLMEnvConfig.cpp           KOKORO_ALIGNER_URL
mod   Deployment/docker-compose.face.yml           aligner URL + emotion/console/free-look flags
mod   Deployment/core2/stage.sh
```

**What it is for.** `KOKORO_SERVER_URL` already returns the audio and its true *length*, which fixes
the total duration — but the track underneath is still estimated from spelling and then stretched to
fit, so a long word and a short one get time in proportion to their *letters* rather than to how
long they were actually voiced. The sidecar (`Deployment/kokoro-align/`) returns the per-phoneme
durations Kokoro's own duration predictor computed for that waveform — the same tensor its length
regulator used to build it — which the OpenAI `/audio/speech` schema discards at the HTTP boundary.

It degrades safely: unset the URL and the mouth silently falls back to the rescaled estimate, and
the log line names which of the two it used on every reply.

## 3. UI surface

Two Slate widgets, both added as **viewport content** so they render into the same back buffer Pixel
Streaming captures. That is what makes one implementation serve both targets — a native window draws
them directly, and the streamed client sees them because they are part of the frame, with keystrokes
returning over the existing input channel. A browser-side DOM overlay would have served the stream
and left the native build with no way to type.

### `SCompanionHUD` — 352 lines
Chat log, input box, Send and Mic buttons, status line. `/console` toggles the settings panel.

### `SCompanionSettingsPanel` — 340 lines
7 checkboxes, 4 text boxes, 2 buttons, 1 slider, in five sections:

| Section | Controls |
|---|---|
| **Avatar** | `id` text; rig `CC` / `ARKit` |
| **Voice / speech** | 10 Kokoro voices; `speak` and `listen` toggles; TTS + STT model fields |
| **Music** | on/off; volume slider (0.55 default) |
| **Chat / branding** | chat visibility; wordmark visibility |
| **Debug** | viseme self-test; diagnostics; LLM model field |

### `FCompanionSettingsModel` — the backing state
A plain struct (17 fields) with `Validate()`, `ToCompactString()` and the voice/profile lists. Being
a struct rather than widget state is what makes it unit-testable without a Slate harness — the panel
exposes `Simulate*` entry points that drive the same code paths the real widgets call.

`bSttEnabled` defaults **off** deliberately: there is no capture path, and a UI that implies one
would be lying about the system.

### Known UI trap, already paid for
The panel's `SBorder` was originally the whole `ChildSlot`, so it stretched to the viewport and its
0.86-alpha plate dimmed the entire scene — character-region luma **17 vs 110**. Every control
rendered correctly; only the *extent* was wrong, which no unit test could catch. It is constrained
to a 430 px box now. This is the case for verifying UI with screenshots rather than assertions.

## 4. Documentation

21 documents in `Docs/`. The load-bearing ones:

| Doc | Purpose |
|---|---|
| `ARCHITECTURE.md` | The spine — how the subsystems fit |
| `GDD-WEB-EXPERIENCE.md` | The experience, with an explicit "what is NOT built" |
| `GAP-ANALYSIS.md` | What is real vs aspirational |
| `PLAN-MULTI-AVATAR.md` | Casting inventory + phased plan |
| `LEARNINGS.md` | The traps, each traceable to a real failure |
| `BUILD-AND-DEPLOY.md`, `CONFIGURATION.md`, `TESTING.md` | Operational |

## 5. Verification status

| Claim | Evidence |
|---|---|
| Editor target compiles on Linux 5.5 | ✅ Clean |
| Backend test suite | ✅ **223 passing, 0 failing** |
| Cook + deploy | ✅ `BUILD_OK`, zero `AssetBaseClassLoaded` ensures |
| Face rig binds | ✅ 42/42 curves resolved on `SK_Nia` |
| Opening line | ✅ Verbatim in chat + spoken |
| Console renders | ✅ Screenshot; scene luma 110 |
| LLM round trip | ✅ ~814 ms warm (`qwen3.6:27b` on beta-1) |
| **Audio reaching a browser** | ✅ **Proven 2026-08-06** — speech `peakRms` 0.0769 vs 0.0211 room-tone floor |

That last row was the oldest open item in the project, and it is now closed — but the cause was not
where anyone had been looking. UE was transmitting audio the whole time. The stock Pixel Streaming
frontend builds the `<video>` element's MediaStream from the **video track only**, so the audio
transceiver was negotiated, the receiver was live, the packets arrived, and the element that was
supposed to play them was never handed the track.

That is invisible from every direction the question had been asked from: signalling shows a
negotiated audio track, UE logs `OnAudioTrackUpdate`, WebRTC internals show bytes received. It shows
up only in `video.srcObject.getAudioTracks()`, which was empty. Measuring exactly that is what
settled it.

Fixed in `Deployment/pixelstreaming/player.html` (the page already mounted over the stock one).
Measured with `Deployment/core2/bank_chat_audio.mjs`, same container, before and after:

```
before   {"error":"stream carries NO audio track"}
after    peakRms 0.021611  meanRms 0.017165   <- room tone only, nobody speaking
speech   peakRms 0.076937  meanRms 0.020096   <- 3.65x the room-tone floor
```

The ~0.017 floor is the machine-room tone looping. The 3.65x excursion is her actually talking.

**A caution this cost half an hour:** the first speech measurement came back at exactly 1.00x
baseline and looked like a flat failure. It was a *timing* artefact — a cold 27B model took 31s to
first token and the 22s listening window had already closed before Kokoro synthesized anything. Warm
the model before measuring, or listen longer than the cold-load time.

## 6. Risks

1. **⏳ The Hana acquisition deadline — 2026-11-05, 97 days.** The only item here with an external
   clock. Unactioned, and it needs an Epic sign-in inside a 5.5 editor (see §9).
2. **Audio unproven** (above). Several later features assume it.
3. ~~**`docker system prune --all` keeps destroying core-2's images**~~ — **mitigated 2026-08-06.**
   It struck again mid-session, taking the engine image, the cooked app image and the signalling
   image, and costing an 85 GB re-pull in the middle of a verification run. The archive workaround
   the old note pointed at had itself been deleted, because nothing kept *it* alive either.

   `Deployment/core2/pin-engine-image.sh` is a hard interlock instead of a remedy: `prune --all`
   removes images "not used by at least one container" and never removes a RUNNING container, so
   one idle `--restart=always` container built from the engine image makes that image unprunable by
   any invocation short of an explicit `docker rmi -f`. Costs a sleeping process; needs no operator
   to remember anything, which is the part every previous mitigation got wrong.

   The prune itself is still outside our control — it comes from unrelated CI housekeeping on that
   box, and unrelated images (traefik, rabbitmq, cloudflared) now appear alongside ours.
4. **`fs.inotify.max_user_watches` reverts on reboot.** A cook exhausts the 65 536 default. Needs one
   line in `/etc/sysctl.d/`, which needs the owner's approval — that box runs other services.
5. ~~Two stale branches~~ — deleted.
6. **The 5.8 editor edits `.uproject` on its own**, adding plugins as *required*. `Dockerfile.amd`'s
   strip-list is the guard; anything 5.8-only that appears must be added or the next 5.5 cook fails.

---

*Snapshot only. `GAP-ANALYSIS.md` is the authority on what works; `LEARNINGS.md` on why things are
shaped the way they are.*

---

# Addendum — deeper audit

Second pass, looking for problems rather than restating structure.

## 7. Clean

| Check | Result |
|---|---|
| Duplicate keys in `docker-compose.face.yml` | None (one was found and fixed earlier) |
| `TODO` / `FIXME` / `HACK` in plugin source | **0** |
| Hard-coded secrets | None — all credentials come from env; `CI_HUB_TOKEN` is the `__NEEDS__` placeholder |
| Build junk tracked (`Intermediate`/`Binaries`/`Saved`) | **0 files** |
| Git pack size | 2.85 MiB — the binaries live in LFS |

## 8. Dead code

`UCompanionHUDWidget` (`Public/UI/CompanionHUDWidget.h` + `.cpp`, ~124 lines) is **unreferenced**.
The only mentions outside its own files are UHT-generated. It is a UMG widget superseded by the
Slate `SCompanionHUD`, which is what actually renders — the Slate one is added as viewport content
so it reaches the Pixel Streaming back buffer, which a UMG widget nothing constructs never could.

It still compiles into the module and shows up in searches as though it were the HUD, which is
actively misleading: `GAP-ANALYSIS.md` described the HUD as "code only, nothing creates it" long
after the Slate HUD went live, because the dead class kept answering the question.

**Deleted.** Both files removed and the two comments that named it updated; zero references remain.

## 9. 262 MB of LFS the cook throws away — **DO NOT DELETE**

28 tracked LFS files, all matched by `Dockerfile.amd`'s strip list for pre-5.6 targets:

```
Content/MetaHumans/DefaultCompanion.uasset      48 MB
Content/OA_KhaosMDF492.uasset                  108 MB
Content/KhaosMDF492/**                          ~100 MB
```

Every 5.5 cook deletes these before building, nothing in code or config references them, and
`PLAN-MULTI-AVATAR.md` recommends against casting further MetaHumans. On that evidence I was about
to recommend removing them.

**That would have been a serious mistake.** `GDD.md` records a hard deadline:

> UE 5.5 acquisition via Bridge/Importer ends **2026-11-05**, with a 90-day retrieval window after.

Once that window closes these assets are **not re-acquirable**. "Recoverable from git history" is
true and irrelevant — the risk is not losing the file, it is losing the *ability to obtain it again*
if the history is ever rewritten or the repo re-created. An asset that cannot be re-downloaded is
not a cleanup candidate however much disk it uses.

**Keep them.** The 262 MB is the cheapest insurance available.

### ⏳ And the deadline is live and unactioned

The same GDD section flags acquiring **Hana** (the free 5.5-native preset the shipping character is
meant to be built from) as *time-critical*, dated 2026-07-15. Checked today:

- **No Hana asset exists** anywhere in the repo or `Content/`.
- **97 days remain** (14 weeks) to 2026-11-05, plus the 90-day retrieval window.

This is the only item in the project with an external clock on it. Everything else can slip; this
cannot. It needs an Epic sign-in inside a 5.5 editor, so it is a person-task, not an agent-task.

## 10. Test coverage gaps

223 tests, 0 failing — but six classes have no spec at all:

| Class | Assessment |
|---|---|
| `NullSpeechToText`, `NullTextToSpeech` | Null objects. Fine. |
| `TapeReelSpinner` | Scene prop rotation. Low risk. |
| `CompanionHUDWidget` | Dead (§8). |
| **`CompanionFaceSubsystem`** | **Binds the face to the resident.** Untested. |
| **`CompanionRuntimeSubsystem`** | **Spawns the companion, routes every reply.** Untested. |

The two subsystems are the load-bearing glue and the least covered, because both need a `UWorld` —
which is exactly why the *logic* was pushed into testable pieces (`FVisemePlanner`,
`FCompanionSettingsModel`, `FFaceProfile`) and those are well covered.

**Addressed.** `CompanionSubsystemSpec.cpp` builds a transient `UWorld` per case and covers the
selection and guard logic: tag match, no false positive on a different tag, empty world, the tag
literal matching what `author_diorama.py` stamps, and — the condition that actually shipped — a
runtime subsystem with no companion behind it reporting failure rather than silently doing nothing.

`FindResident` moved from private to public to allow this. It is a const query with no side
effects, so nothing is weakened by exposing the seam where the worst bug lived.

## 11. Documentation drift — found and fixed

Three claims had gone stale. Corrected in this pass rather than merely listed:

| Doc | Was | Now |
|---|---|---|
| `GAP-ANALYSIS.md` | "User input ❌ Missing — nobody can address her" | ✅ Text input works and is verified; **voice** input is what is missing |
| `GAP-ANALYSIS.md` | "HUD ⚠️ Code only — nothing creates it" | ✅ Slate HUD live; the UMG class it referred to is dead code |
| `GDD-WEB-EXPERIENCE.md` | §3 opening flythrough marked ✅ | ⚠️ **opt-in** — `MHLLM_OPENING_SHOT` now defaults to **0** |

That last one matters most: the GDD opens by describing the camera move as *the* experience, while
the default deployment has a static camera. Anyone reading the GDD and then running the stack would
conclude the feature was broken.

**The pattern worth noting:** every one of these drifted the same way — behaviour improved, the doc
kept describing the old state, and the stale text read as a defect report rather than as an
out-of-date sentence. Docs asserting ❌ need re-checking whenever the thing they describe changes.
