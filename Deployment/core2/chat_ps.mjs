// Does a message TYPED IN A BROWSER reach the companion over Pixel Streaming?
//
// The chat box was proven in a native window long before anyone drove it over the stream, and the
// two are not the same test: a native keystroke comes from the OS, a streamed one arrives as a
// KeyDown/KeyPress/KeyUp triple on the WebRTC data channel and is injected into Slate by
// FPixelStreaming2InputHandler. Everything between those two facts -- input mode, keyboard focus,
// where a click lands -- can be correct locally and broken over the wire.
//
// Run it (from core-2):
//   docker run --rm --network host -v /home/ci/shots:/shots -w /shots \
//     mcr.microsoft.com/playwright:v1.49.0-noble bash -lc 'TAG=chat node chat_ps.mjs'
//
// The PICTURES are only half the evidence; the other half is the container log, which must show
//   LogCompanionRuntime: Chat box submitted (N chars): <text>
//   LogCompanionRuntime: SubmitUserText -> companion: <text>
// for each phase. Grab it with:
//   docker logs --since <iso8601>Z deployment-project-1 | grep -E 'Chat box submitted|SubmitUserText'
// and add -LogCmds="LogPixelStreaming2Input Verbose" to the app's command line to see the
// KEY_DOWN / MOUSE_DOWN lines, which is the only way to tell "the app never got it" apart from
// "the app got it and Slate dropped it".
//
// Four phases, each with its own marker string so the log splits cleanly by phase:
//   A  type with NO click at all      -- proves auto-focus, i.e. a viewer can just start typing
//   B  click into the box, then type  -- the route a viewer takes after clicking anything else
//   C  the Send BUTTON, mouse only    -- the ONLY route a phone has; it has no Enter key
//   D  wait for the reply             -- the full round trip, not just the submit
import { chromium } from "playwright";
import { writeFileSync } from "node:fs";

const OUT = process.env.OUT || "/shots";
const TAG = process.env.TAG || "chat";
// The stream is 720x1280 PORTRAIT (see docker-compose.amd.yml). Matching the viewport to it makes
// page pixels app pixels, so a coordinate read off a screenshot can be clicked directly.
const W = Number(process.env.W || 720), H = Number(process.env.H || 1280);
const URL = process.env.PSURL
  || "http://localhost:8090/?AutoConnect=true&AutoPlayVideo=true&StartVideoMuted=true";
// Coordinates of the two controls, in app pixels, read off a capture.
const BOX = [300, 1269], SEND = [657, 1269];

const browser = await chromium.launch({ args: [
  "--autoplay-policy=no-user-gesture-required", "--no-sandbox",
  "--use-gl=swiftshader", "--enable-unsafe-swiftshader", "--disable-dev-shm-usage" ]});
const page = await browser.newPage({ viewport: { width: W, height: H } });
await page.goto(URL, { waitUntil: "domcontentloaded", timeout: 30000 });
await page.waitForTimeout(14000);

const geom = await page.evaluate(() => {
  const v = document.querySelector("video");
  if (!v) return { err: "no video element" };
  const r = v.getBoundingClientRect();
  return { vw: v.videoWidth, vh: v.videoHeight, rx: r.x, ry: r.y, rw: r.width, rh: r.height,
           playing: !v.paused, t: +v.currentTime.toFixed(1) };
});
console.log("GEOM " + JSON.stringify(geom));
if (geom.err || !geom.vw) { console.log("FAIL no stream"); await browser.close(); process.exit(1); }

// app pixel -> page pixel. With a matched viewport this is the identity, but it is written out so a
// mismatched one is off by a knowable amount rather than silently clicking the wrong thing.
function A2P(px, py) {
  const s = Math.min(geom.rw / geom.vw, geom.rh / geom.vh);
  return [geom.rx + (geom.rw - geom.vw * s) / 2 + px * s,
          geom.ry + (geom.rh - geom.vh * s) / 2 + py * s];
}
const clickApp = async ([px, py]) => { const [x, y] = A2P(px, py); await page.mouse.click(x, y); };

async function grab(name) {
  // The <video> element, not a page screenshot: a page screenshot of a WebRTC surface can come back
  // blank, and the decoded frame is what the viewer actually sees.
  const d = await page.evaluate(() => {
    const v = document.querySelector("video");
    if (!v || !v.videoWidth) return { err: "no video" };
    const c = document.createElement("canvas");
    c.width = v.videoWidth; c.height = v.videoHeight;
    c.getContext("2d").drawImage(v, 0, 0);
    return { t: +v.currentTime.toFixed(1), png: c.toDataURL("image/png") };
  });
  if (d.png) { writeFileSync(`${OUT}/${TAG}_${name}.png`, Buffer.from(d.png.split(",")[1], "base64")); delete d.png; }
  console.log("SHOT " + name + " " + JSON.stringify(d));
}
const type = (t) => page.keyboard.type(t, { delay: 70 });

await grab("00_loaded");

console.log("PHASE A autofocus-noclick");
await type("alpha autofocus no click");
await page.waitForTimeout(500); await grab("01_A_typed");
await page.keyboard.press("Enter");
await page.waitForTimeout(2500); await grab("02_A_after_enter");

console.log("PHASE B click-then-type");
await clickApp(BOX); await page.waitForTimeout(800); await grab("03_B_clicked");
await type("bravo clicked then typed");
await page.waitForTimeout(500); await grab("04_B_typed");
await page.keyboard.press("Enter");
await page.waitForTimeout(2500); await grab("05_B_after_enter");

console.log("PHASE C send-button");
await type("charlie send button");
await page.waitForTimeout(500); await grab("06_C_typed");
await clickApp(SEND);
await page.waitForTimeout(2500); await grab("07_C_after_send");

// The regression that made a single stray click fatal. The HUD's scroll box swallows clicks over
// most of the screen WITHOUT taking the caret, so this has to aim at the one place that does reach
// the game viewport: inside the 4px padding at the very edge. Before the focus guard, everything
// typed after this click was silently discarded for the rest of the session.
console.log("PHASE E stray-click-then-type");
await clickApp([2, 640]); await page.waitForTimeout(1200); await grab("09_E_stray_click");
await type("echo after stray click");
await page.waitForTimeout(500); await grab("10_E_typed");
await page.keyboard.press("Enter");
await page.waitForTimeout(2500); await grab("11_E_after_enter");

console.log("PHASE D reply-wait");
for (let i = 0; i < 6; i++) { await page.waitForTimeout(5000); await grab("12_D_reply_" + i); }

await browser.close();
