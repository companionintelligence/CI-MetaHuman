# core-2 build loop

The scripts that actually author, cook, deploy and verify the diorama. They ran for weeks as
hand-maintained copies in `/home/ci/` on core-2 and existed **nowhere else** — which is exactly
the drift trap `HANDOFF.md` Lesson 2 warns about, and a standing risk given how often that box
gets cleaned out (see "Durability" below). They are checked in here as the source of truth; copy
them to `/home/ci/` rather than editing the server copies in place.

| script | what it does | cost |
|---|---|---|
| `run_author_m2.sh` | Authors the map headlessly (`author_diorama.py`) in the content-only project via `dev-5.5` + `-nullrhi` (still 5.5 — see **Engine version**). Mounts the prop GLBs, the Seo FBX/textures and `nia-content`. Writes `stage-out/Content` + `AUTHOR_RESULT.txt`. | ~3 min |
| `m2_full.sh` | The full pipeline: author → purge + refresh `cook-m2/Content` → `docker build Dockerfile.amd --build-arg RELEASE=5.8` → `docker compose up -d project` (the tag it just built) → capture a frame. | ~20 min |
| `gpu_preview.sh` | Renders the **uncooked** authored map on the real GPU via `ci/ue55-editor-preview:mesa`. Skips the cook entirely. See `Dockerfile.editorpreview` for the ICD/`setpriv` gotchas that make this work at all. | ~3-5 min |
| `cap2.mjs` / `cap5.mjs` | Headless Playwright capture of the live WebRTC stream into a PNG + luminance stats. `cap2` writes `m2-canvas.png`, `cap5` writes `ship-view.png` (no mouse input, so it does not rotate the camera). | ~30 s |
| `chat_ps.mjs` | Drives the on-screen chat box **from a browser** over the stream: types with no click, clicks-then-types, presses the Send button, waits for the reply. See "Typing over the stream" below. | ~90 s |

**Always verify with pixels, not logs** (Lesson 1). `LoadMap complete` and `HTTP 200` prove the
stream is up, not that it shows anything. Capture a frame and look at it. `meanLuma ~0.03` is
black, `~117` is a correctly exposed scene. When checking which way a character faces, look for
**eyes** — a yaw error survived weeks of "verified" because nobody looked for a face.

## Engine version

The cook runs on **5.8**. Every git-tracked package under `Content/` is UE5 package version 1018,
UE 5.5.4 reads at most 1013, and package serialisation is forward-only — so a 5.5 cook cannot open
this project's content. It fails at LOAD, never at compile, which is why a compile-only check kept
reporting that 5.5 was fine. `Scripts/BuildNative.ps1`'s `-Engine` notes are the canonical account
of this; read those rather than re-deriving it.

Building 5.5 is still supported on purpose and none of it has been removed: `Dockerfile.amd` keeps
`RELEASE` a build-arg (default 5.8), its pre-5.6 plugin strip stays, `docker-compose.ue55.yml` still
runs a 5.5 image, and `Scripts/RunCompatMatrix.ps1` still builds and tests the plugin against both
engines. What changed is the default — `m2_full.sh` and `m2_clean.sh` now build with
`RELEASE=5.8`, and `m2_full.sh` no longer layers `docker-compose.ue55.yml` on at deploy.

Three 5.5 pins remain in this loop and are **not** fixed by that change:

| where | pin | consequence |
|---|---|---|
| `run_author_m2.sh` | authors in `ghcr.io/epicgames/unreal-engine:dev-5.5` | the authoring engine and the cooking engine are now different versions |
| `Deployment/docker-compose.face.yml` | `image: ci/metahuman-local-llm:5.5-face` | `m2_clean.sh` builds `:5.8-face` and now stops with `COMPOSE_IMAGE_MISMATCH` before deploying, until that file names the same tag |
| `gpu_preview.sh` | renders through `ci/ue55-editor-preview:mesa` | preview only, no cook |

`SIGNALLING_IMAGE` is a different matter and not an engine pin to "fix" here: `.env.example` notes
Epic publishes the signalling server image only up to `:5.5`.

The figures on this page — the `cost` column, the 85 GB base, the 24 GB preview image, the 20
minute cook — all predate this change and were recorded while the loop built 5.5. They stay
attributed to 5.5 rather than being restated for 5.8.

## Typing over the stream

The chat box was proven in a native window through four routes and shipped on that evidence. It had
never been driven from a browser. Those are different tests: a local keystroke comes from the OS, a
streamed one arrives as a `KeyDown`/`KeyPress`/`KeyUp` triple on the WebRTC data channel and is
pushed into Slate by `FPixelStreaming2InputHandler`. `chat_ps.mjs` drives the browser end; the proof
is a pair of lines in the container log, not a screenshot alone:

```
docker run --rm --network host -v /home/ci/shots:/shots -w /shots \
  mcr.microsoft.com/playwright:v1.49.0-noble bash -lc 'TAG=chat node chat_ps.mjs'
docker logs --since <iso8601>Z deployment-project-1 | grep -E 'Chat box submitted|SubmitUserText'
```

To see the streamed input itself, add `-LogCmds="LogPixelStreaming2Input Verbose"` to the app's
command line. That is the only way to tell **"the app never received it"** apart from **"the app
received it and Slate dropped it"**, and it is what settled this: the `KEY_DOWN`/`KEY_PRESSED` lines
were all there, so the keyboard was never the problem.

What the measurements showed (core-2, packaged 5.5 + PixelStreaming2, 720x1280 portrait stream):

- **Typing works with no click at all.** Auto-focus is real: connect, type, press Enter, and the
  turn is submitted, echoed into the on-screen log, and answered. The click is not a prerequisite.
- **A click was what broke it.** The stock player page defaults to the LOCKED-MOUSE control scheme:
  it calls `requestPointerLock` on first click, after which the browser reports `clientX/clientY`
  frozen at the centre of the video and the frontend sends a virtual cursor accumulated from
  movement *deltas*. Clicks at (300,1269) and (657,1269) arrived at the app as **(0,640)** and
  **(720,1280)** — the left edge and the far corner. The first missed the text box; the second was
  aimed at Send and hit nothing. And a click that falls through the HUD onto the game viewport takes
  keyboard focus, which nothing gave back: one stray click made the chat box dead for the session.
- **`HoveringMouse=true` fixes the aim.** The same two clicks then arrive as (299,1269) and
  (657,1269), the box focuses on click, and the **Send button submits**. This is now the default —
  `Deployment/pixelstreaming/player.html` is mounted over the stock page and sets it before
  `player.js` parses. Append `?HoveringMouse=false` to get the stock behaviour back.
- **The app no longer depends on either.** `SCompanionHUD::RestoreInputFocusIfLost`, ticked every
  250 ms by `UCompanionRuntimeSubsystem::RearmChatFocus`, takes the caret back whenever nothing that
  can type is holding it (it deliberately yields to the operator console's own text fields). Send
  and Mic now fire on press (`EButtonClickMethod::MouseDown`) rather than on a captured
  press-and-release, because a streamed tap arrives as `MouseEnter`/`MouseDown`/`MouseUp`/
  `MouseLeave` inside a single frame with no `MouseMove` between them.
- **Not proven: a real phone.** Touch was exercised only under Chromium's mobile emulation, where a
  tap became that same single-frame fake-mouse burst and activated no widget. Treat mobile as
  untested until someone taps Send on actual glass.

One counter-intuitive note for anyone reading a capture: the HUD's `SScrollBox` fills nearly the
whole screen and swallows clicks **without** taking keyboard focus, so clicking the middle of the
scene is harmless. It is the 4px border and the corners — exactly where a drifted virtual cursor
lands — that reach the viewport and steal the caret.

## Service tunnels

core-2 runs the app but none of the models. Cognition and voice both live on beta-1
(`100.124.211.75`), and every one of those ports is bound to **beta-1's loopback** — Ollama by a
running process that predates its own `OLLAMA_HOST=0.0.0.0`, Lemonade and kokoro-align by
container port mappings published to `127.0.0.1`. None of them is reachable across Tailscale as
published, so each is forwarded by a `--user` systemd unit here. Copy to
`~/.config/systemd/user/` and `systemctl --user enable --now <unit>`.

| unit | core-2 port | beta-1 service | used by |
|---|---|---|---|
| `ollama-tunnel.service` | 11435 | Ollama :11434 | `OLLAMA_BASE_URL` — replies |
| `lemonade-tunnel.service` | 13305 | Lemonade Server :13305 | `KOKORO_SERVER_URL` (TTS) + `WHISPER_SERVER_URL` (STT) |
| `kokoro-align-tunnel.service` | 13400 | kokoro-align :13400 | nothing yet — see below |

**One port per unit.** They were briefly merged into a single `ssh -L ... -L ...`; with
`ExitOnForwardFailure=yes` that makes any one port clash take down every service at once.

**A listener is not a tunnel.** The failure that motivated these units looked healthy from `ss`
— `127.0.0.1:13305 LISTEN`, owned by a real `ssh` pid — and returned `HTTP 000` to curl, because
the forward had been established by an `ssh -L` launched with `nohup` from inside an SSH session
and the far end was gone. Verify with a status code, never with a socket:

```
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:13305/api/v1/models   # 200
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:13400/health          # 200
curl -s -o /dev/null -w '%{http_code}\n' http://127.0.0.1:11435/api/tags        # 200
```

And a 200 through the tunnel is not proof the *service* is healthy either — it only proves
something answered. The tunnels can be perfect and still serve `000` when the container behind
them is stopped, which is how this last broke: `lemonade-trial` on beta-1 carries
`RestartPolicy=no`, so once stopped it stays stopped until someone runs `docker start`.

`kokoro-align` has no consumer in the shipped build. `FVisemePlanner::PlanFromAlignment` exists
but nothing calls it over HTTP; the `Re-timed to audio` line comes from `PlanForDuration` against
the audio length Kokoro reports, not from :13400. The tunnel is in place for when the aligner is
wired up — it does not currently affect lip-sync.

## Durability

`docker system prune --volumes --all --force` has been run on core-2 repeatedly (it appears
several times in the `ci` user's shell history), and the box reboots. Each time, every local
image is destroyed: the cooked app, the 24 GB preview image, and the 85 GB `dev-5.5` base.
Filesystem content under `/home/ci/` survives — only Docker state is lost.

Two consequences worth designing around:

- Restoring the stream costs a full engine-image pull plus a cook; the recorded figures, 85 GB
  and 20 minutes, are from the 5.5 loop (see **Engine version**). Pushing the cooked
  image to a registry, or `docker save`-ing it to `/home/ci`, would make it a pull instead.
- Nothing restarts the stream after a reboot; the CI ecosystem containers come back because they
  carry restart policies, and ours do not.
