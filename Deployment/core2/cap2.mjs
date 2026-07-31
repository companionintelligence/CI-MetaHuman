import { chromium } from "playwright";
import { writeFileSync } from "node:fs";
const browser = await chromium.launch({ args: [
  "--autoplay-policy=no-user-gesture-required", "--no-sandbox",
  "--use-gl=swiftshader", "--enable-unsafe-swiftshader", "--disable-dev-shm-usage" ]});
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
await page.goto("http://localhost:8090/?AutoConnect=true&AutoPlayVideo=true&StartVideoMuted=true", { waitUntil: "domcontentloaded", timeout: 30000 });
await page.waitForTimeout(6000);
await page.mouse.click(640, 360);
await page.waitForTimeout(6000);
const data = await page.evaluate(() => {
  const v = document.querySelector("video");
  if (!v || !v.videoWidth) return { err: "no video" };
  const c = document.createElement("canvas");
  c.width = v.videoWidth; c.height = v.videoHeight;
  const ctx = c.getContext("2d");
  ctx.drawImage(v, 0, 0);
  // luminance stats to distinguish black-frame vs dark-scene
  const px = ctx.getImageData(0, 0, c.width, c.height).data;
  let sum = 0, max = 0, n = px.length / 4;
  for (let i = 0; i < px.length; i += 4) { const l = 0.2126*px[i] + 0.7152*px[i+1] + 0.0722*px[i+2]; sum += l; if (l > max) max = l; }
  return { w: c.width, h: c.height, t: v.currentTime, meanLuma: +(sum/n).toFixed(2), maxLuma: +max.toFixed(1), png: c.toDataURL("image/png") };
});
if (data.png) { writeFileSync("/shots/m2-canvas.png", Buffer.from(data.png.split(",")[1], "base64")); delete data.png; }
console.log("STATS:", JSON.stringify(data));
await browser.close();
