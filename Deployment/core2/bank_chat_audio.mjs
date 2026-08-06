// Expanded test bank: chat round-trip + SPEECH audio energy + gaze motion, in one session.
//
// Why one session: the deployment is single-viewer (one UE process, one streamer, one player), so
// running these as separate scripts would serialise into separate connections and lose the thing
// that matters most -- the audio energy DURING a reply, measured against the room-tone floor from
// the same connection. A spike is only meaningful relative to a baseline captured moments earlier
// through the same path.
import { chromium } from 'playwright';
import { writeFileSync } from 'node:fs';

const URL = process.env.STREAM_URL || 'http://localhost:8090/?AutoConnect=true&AutoPlayVideo=true';
const PROMPT = process.env.PROMPT || 'My name is Liam and my favourite colour is orange. Please remember that.';
const out = {};

const browser = await chromium.launch({
  args: ['--autoplay-policy=no-user-gesture-required', '--no-sandbox', '--disable-dev-shm-usage',
         '--use-gl=swiftshader', '--enable-unsafe-swiftshader'],
});
const page = await browser.newPage({ viewport: { width: 720, height: 1280 } });
page.on('console', m => { const t = m.text(); if (t.includes('audio-fix')) out.audioFixLog = t; });
await page.goto(URL, { waitUntil: 'domcontentloaded', timeout: 30000 });
await page.waitForTimeout(15000);   // negotiate + frames + the audio-fix poll (1s cadence)

// --- helper: measure RMS on the attached audio track for N ms ---
async function measure(ms) {
  return page.evaluate(async (dur) => {
    const v = document.querySelector('video');
    if (!v || !v.srcObject) return { error: 'no stream' };
    const a = v.srcObject.getAudioTracks();
    if (!a.length) return { error: 'NO audio track attached' };
    const ctx = new (window.AudioContext || window.webkitAudioContext)();
    if (ctx.state === 'suspended') await ctx.resume();
    const an = ctx.createAnalyser(); an.fftSize = 2048;
    ctx.createMediaStreamSource(new MediaStream([a[0]])).connect(an);
    const buf = new Float32Array(an.fftSize);
    let peak = 0, sum = 0, n = 0;
    const t0 = performance.now();
    while (performance.now() - t0 < dur) {
      an.getFloatTimeDomainData(buf);
      let s = 0; for (let i = 0; i < buf.length; i++) s += buf[i] * buf[i];
      const rms = Math.sqrt(s / buf.length);
      peak = Math.max(peak, rms); sum += rms; n++;
      await new Promise(r => setTimeout(r, 25));
    }
    await ctx.close();
    return { peakRms: +peak.toFixed(6), meanRms: +(sum / n).toFixed(6), samples: n };
  }, ms);
}

async function grab(name) {
  const d = await page.evaluate(() => {
    const v = document.querySelector('video');
    if (!v || !v.videoWidth) return null;
    const c = document.createElement('canvas');
    c.width = v.videoWidth; c.height = v.videoHeight;
    c.getContext('2d').drawImage(v, 0, 0);
    return { w: c.width, h: c.height, png: c.toDataURL('image/png') };
  });
  if (d && d.png) {
    writeFileSync(`/shots/${name}.png`, Buffer.from(d.png.split(',')[1], 'base64'));
    return { w: d.w, h: d.h };
  }
  return null;
}

// 1. BASELINE: room tone only, nobody speaking.
out.streamSize = await grab('bank-01-idle');
out.audioBaseline = await measure(6000);

// 2. GAZE: three frames a second apart. If the eyes are welded forward these are identical in the
//    eye region; the whole point of the feature is that they are not.
const eyeHashes = [];
for (let i = 0; i < 3; i++) {
  const h = await page.evaluate(() => {
    const v = document.querySelector('video');
    const c = document.createElement('canvas');
    c.width = v.videoWidth; c.height = v.videoHeight;
    const g = c.getContext('2d'); g.drawImage(v, 0, 0);
    // Eye band of a portrait framing: horizontal middle, ~30% down.
    const x = Math.floor(c.width * 0.35), y = Math.floor(c.height * 0.26);
    const w = Math.floor(c.width * 0.30), hh = Math.floor(c.height * 0.10);
    const d = g.getImageData(x, y, w, hh).data;
    let sum = 0; for (let i = 0; i < d.length; i += 4) sum += d[i] * 3 + d[i + 1] * 5 + d[i + 2];
    return sum;
  });
  eyeHashes.push(h);
  await page.waitForTimeout(1000);
}
out.eyeRegionSums = eyeHashes;
out.eyeRegionMoved = new Set(eyeHashes).size > 1;

// 3. CHAT ROUND TRIP. Type with no click at all -- auto-focus is the documented behaviour and a
//    click is what historically broke the caret.
await page.keyboard.type(PROMPT, { delay: 45 });
await page.waitForTimeout(400);
await grab('bank-02-typed');
await page.keyboard.press('Enter');

// 4. SPEECH AUDIO, measured while the reply is being spoken. Starts immediately so the window
//    covers the TTS clip rather than the silence after it.
out.audioDuringReply = await measure(22000);
out.replyFrame = await grab('bank-03-reply');

await page.waitForTimeout(2000);
await grab('bank-04-after');

out.verdict = {
  audioAttached: !out.audioBaseline?.error,
  speechLouderThanRoomTone:
    out.audioDuringReply?.peakRms && out.audioBaseline?.peakRms
      ? +(out.audioDuringReply.peakRms / out.audioBaseline.peakRms).toFixed(2)
      : null,
};
console.log('BANK_RESULT ' + JSON.stringify(out));
await browser.close();
