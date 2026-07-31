// Measures whether the Pixel Streaming audio track carries ENERGY, not merely whether it exists.
//
// This distinction is the whole point. UE negotiates an audio track and logs
// FStreamer::OnAudioTrackUpdate whether or not any samples ever reach it, so "the track is up"
// proves nothing. A muted app, an uninitialised audio mixer and a working stream are
// indistinguishable from the signalling logs alone -- only the waveform tells them apart.
import { chromium } from 'playwright';
import { writeFileSync } from 'node:fs';

const URL = process.env.STREAM_URL || 'http://localhost:8090/';
const SECONDS = Number(process.env.LISTEN_SECONDS || 25);

const browser = await chromium.launch({
  args: [
    // Chromium refuses to start audio without a user gesture; in a headless capture there is
    // nobody to gesture, so the policy must be lifted or every run reports silence.
    '--autoplay-policy=no-user-gesture-required',
    '--no-sandbox', '--disable-dev-shm-usage',
  ],
});
const page = await browser.newPage();
await page.goto(URL, { waitUntil: 'domcontentloaded' });
await page.waitForTimeout(6000);
// The PS frontend needs a click to begin playback.
await page.mouse.click(640, 360).catch(() => {});
await page.waitForTimeout(6000);

const result = await page.evaluate(async (seconds) => {
  const video = document.querySelector('video');
  if (!video) return { error: 'no video element' };
  const stream = video.srcObject;
  if (!stream) return { error: 'video has no srcObject' };
  const audioTracks = stream.getAudioTracks();
  if (!audioTracks.length) return { error: 'stream carries NO audio track' };

  const ctx = new (window.AudioContext || window.webkitAudioContext)();
  if (ctx.state === 'suspended') await ctx.resume();
  const src = ctx.createMediaStreamSource(new MediaStream([audioTracks[0]]));
  const analyser = ctx.createAnalyser();
  analyser.fftSize = 2048;
  src.connect(analyser);
  const buf = new Float32Array(analyser.fftSize);

  let peak = 0, sum = 0, n = 0;
  const series = [];
  const t0 = performance.now();
  while (performance.now() - t0 < seconds * 1000) {
    analyser.getFloatTimeDomainData(buf);
    let s = 0;
    for (let i = 0; i < buf.length; i++) s += buf[i] * buf[i];
    const rms = Math.sqrt(s / buf.length);
    peak = Math.max(peak, rms);
    sum += rms; n++;
    if (n % 20 === 0) series.push(Number(rms.toFixed(5)));
    await new Promise(r => setTimeout(r, 50));
  }
  return {
    trackLabel: audioTracks[0].label,
    trackEnabled: audioTracks[0].enabled,
    trackMuted: audioTracks[0].muted,
    readyState: audioTracks[0].readyState,
    videoMuted: video.muted,
    peakRms: Number(peak.toFixed(6)),
    meanRms: Number((sum / Math.max(n, 1)).toFixed(6)),
    samples: n,
    series: series.slice(0, 60),
  };
}, SECONDS);

console.log('AUDIO_RESULT ' + JSON.stringify(result));
writeFileSync('/shots/audio_probe.json', JSON.stringify(result, null, 1));
await browser.close();
