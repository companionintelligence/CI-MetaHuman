// Films the stream: ONE browser session, many frames. The per-frame container spawn used by
// cap2.mjs costs ~12s, which cannot sample an 11-second camera move at all -- by the second frame
// the shot is over. Connecting once and pulling frames on an interval is the only way to see
// motion rather than two disconnected stills.
import { chromium } from "playwright";
import { writeFileSync } from "node:fs";

const COUNT    = Number(process.env.BURST_COUNT ?? 48);
const INTERVAL = Number(process.env.BURST_MS ?? 300);
const PREFIX   = process.env.BURST_PREFIX ?? "/shots/burst";
// Reload puts the app back at level start so the opening shot replays from frame one.
const RELOAD   = (process.env.BURST_RELOAD ?? "1") === "1";

const browser = await chromium.launch({ args: [
  "--autoplay-policy=no-user-gesture-required", "--no-sandbox",
  "--use-gl=swiftshader", "--enable-unsafe-swiftshader", "--disable-dev-shm-usage" ]});
const page = await browser.newPage({ viewport: { width: 720, height: 1280 } });
await page.goto("http://localhost:8090/?AutoConnect=true&AutoPlayVideo=true&StartVideoMuted=true",
  { waitUntil: "domcontentloaded", timeout: 30000 });
await page.waitForTimeout(6000);
await page.mouse.click(360, 640);
await page.waitForTimeout(RELOAD ? 5000 : 6000);

const grab = () => page.evaluate(() => {
  const v = document.querySelector("video");
  if (!v || !v.videoWidth) return null;
  const c = document.createElement("canvas");
  c.width = v.videoWidth; c.height = v.videoHeight;
  c.getContext("2d").drawImage(v, 0, 0);
  return { t: v.currentTime, png: c.toDataURL("image/png") };
});

let saved = 0;
const t0 = Date.now();
for (let i = 0; i < COUNT; i++) {
  const d = await grab();
  if (d && d.png) {
    writeFileSync(`${PREFIX}_${String(i).padStart(3, "0")}.png`,
      Buffer.from(d.png.split(",")[1], "base64"));
    saved++;
  }
  const due = t0 + (i + 1) * INTERVAL;
  const wait = due - Date.now();
  if (wait > 0) await page.waitForTimeout(wait);
}
console.log(`BURST: saved ${saved}/${COUNT} frames over ${((Date.now()-t0)/1000).toFixed(1)}s`);
await browser.close();
