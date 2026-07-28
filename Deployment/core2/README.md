# core-2 build loop

The scripts that actually author, cook, deploy and verify the diorama. They ran for weeks as
hand-maintained copies in `/home/ci/` on core-2 and existed **nowhere else** — which is exactly
the drift trap `HANDOFF.md` Lesson 2 warns about, and a standing risk given how often that box
gets cleaned out (see "Durability" below). They are checked in here as the source of truth; copy
them to `/home/ci/` rather than editing the server copies in place.

| script | what it does | cost |
|---|---|---|
| `run_author_m2.sh` | Authors the map headlessly (`author_diorama.py`) in the content-only project via `dev-5.5` + `-nullrhi`. Mounts the prop GLBs, the Seo FBX/textures and `nia-content`. Writes `stage-out/Content` + `AUTHOR_RESULT.txt`. | ~3 min |
| `m2_full.sh` | The full pipeline: author → purge + refresh `cook-m2/Content` → `docker build Dockerfile.amd --build-arg RELEASE=5.5` → `docker compose up -d project` → capture a frame. | ~20 min |
| `gpu_preview.sh` | Renders the **uncooked** authored map on the real GPU via `ci/ue55-editor-preview:mesa`. Skips the cook entirely. See `Dockerfile.editorpreview` for the ICD/`setpriv` gotchas that make this work at all. | ~3-5 min |
| `cap2.mjs` / `cap5.mjs` | Headless Playwright capture of the live WebRTC stream into a PNG + luminance stats. `cap2` writes `m2-canvas.png`, `cap5` writes `ship-view.png` (no mouse input, so it does not rotate the camera). | ~30 s |

**Always verify with pixels, not logs** (Lesson 1). `LoadMap complete` and `HTTP 200` prove the
stream is up, not that it shows anything. Capture a frame and look at it. `meanLuma ~0.03` is
black, `~117` is a correctly exposed scene. When checking which way a character faces, look for
**eyes** — a yaw error survived weeks of "verified" because nobody looked for a face.

## Durability

`docker system prune --volumes --all --force` has been run on core-2 repeatedly (it appears
several times in the `ci` user's shell history), and the box reboots. Each time, every local
image is destroyed: the cooked app, the 24 GB preview image, and the 85 GB `dev-5.5` base.
Filesystem content under `/home/ci/` survives — only Docker state is lost.

Two consequences worth designing around:

- Restoring the stream currently costs an 85 GB pull plus a 20 minute cook. Pushing the cooked
  image to a registry, or `docker save`-ing it to `/home/ci`, would make it a pull instead.
- Nothing restarts the stream after a reboot; the CI ecosystem containers come back because they
  carry restart policies, and ours do not.
