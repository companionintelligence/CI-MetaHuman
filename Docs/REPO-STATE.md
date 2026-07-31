# Repository state review

> 2026-07-31. A snapshot of branches, working tree, UI surface and verification status. Facts were
> read from the repo and the running system, not recalled — where something is unverified it says so.

---

## 1. Branches

| Branch | Upstream | Relationship to `main` | State |
|---|---|---|---|
| **`main`** | `origin/main` | — | **Default branch. Current.** 88 commits, 0 unpushed. |
| `feat/companion-face-driver` | `origin/…` | 0 ahead / 9 behind | Merged via **PR #4**. Stale. |
| `feat/companion-stage-viseme-groundwork` | `origin/…` | fully merged | Merged. Stale. |

`git branch --merged main` lists both feature branches, so **no work is stranded** — everything has
landed on `main`. All four PRs (#1–#4) are closed.

The five commits after the PR #4 merge were made **directly to `main`**, so that is the working
pattern now rather than branch-per-change.

### Recommendation

Delete both merged branches locally and on the remote. They are 9 commits behind and keeping them
invites someone to branch from a stale base — which is precisely how the "fix lives where nothing
reads it" failures in `LEARNINGS.md` started.

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
| **Audio reaching a browser** | ❌ **Still unproven** |

That last row has been the oldest open item in the project. The stack synthesizes speech, decodes
it, measures its duration and drives the mouth from that measurement — all verified — but nobody has
confirmed a browser receives audible audio. The negotiated track can carry digital silence forever
and look identical to success unless the track's *energy* is measured.

## 6. Risks

1. **Audio unproven** (above). Several later features assume it.
2. **`docker system prune --all` keeps destroying core-2's images** — five occurrences in shell
   history. Each costs an 85 GB engine re-pull. `Deployment/core2/README.md` documents the archive
   workaround; the prune itself is outside our control.
3. **`fs.inotify.max_user_watches` reverts on reboot.** A cook exhausts the 65 536 default. Needs one
   line in `/etc/sysctl.d/`, which needs the owner's approval — that box runs other services.
4. **Two stale branches** (§1).
5. **The 5.8 editor edits `.uproject` on its own**, adding plugins as *required*. `Dockerfile.amd`'s
   strip-list is the guard; anything 5.8-only that appears must be added or the next 5.5 cook fails.

---

*Snapshot only. `GAP-ANALYSIS.md` is the authority on what works; `LEARNINGS.md` on why things are
shaped the way they are.*
