import { chromium } from "playwright";
import { writeFileSync } from "node:fs";
// Capture the SHIPPING view: autoplay via URL params only — no mouse input at all.
// (A click/move now rotates the camera, because DefaultPawn input is live again.)
const b = await chromium.launch({ args: ["--autoplay-policy=no-user-gesture-required","--no-sandbox","--use-gl=swiftshader","--enable-unsafe-swiftshader","--disable-dev-shm-usage"] });
const p = await b.newPage({ viewport: { width: 1280, height: 720 } });
await p.goto("http://localhost:8090/?AutoConnect=true&AutoPlayVideo=true&StartVideoMuted=true&HoveringMouse=false", { waitUntil: "domcontentloaded", timeout: 30000 });
await p.waitForTimeout(14000);   // let WebRTC negotiate + frames flow; touch nothing
const d = await p.evaluate(() => {
  const v = document.querySelector("video");
  if (!v || !v.videoWidth) return { err: "no video" };
  const c = document.createElement("canvas");
  c.width = v.videoWidth; c.height = v.videoHeight;
  c.getContext("2d").drawImage(v, 0, 0);
  return { w: c.width, h: c.height, t: v.currentTime, png: c.toDataURL("image/png") };
});
if (d.png) { writeFileSync("/shots/ship-view.png", Buffer.from(d.png.split(",")[1], "base64")); delete d.png; }
console.log("SHIP:", JSON.stringify(d));
await b.close();
