// Refined: my first probe only inspected video.srcObject. The PS frontend may attach the
// inbound audio to a SEPARATE element, so "no audio track on the video" does not prove
// "no audio transmitted". This checks every media element AND patches RTCPeerConnection to
// observe what the connection actually negotiated.
import { chromium } from 'playwright';
const URL = process.env.STREAM_URL || 'http://localhost:8090/';
const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required','--no-sandbox','--disable-dev-shm-usage'] });
const page = await browser.newPage();
// Patch BEFORE any page script runs, so we see every connection the app makes.
await page.addInitScript(() => {
  window.__pcs = [];
  const Orig = window.RTCPeerConnection;
  window.RTCPeerConnection = function (...a) { const pc = new Orig(...a); window.__pcs.push(pc); return pc; };
  window.RTCPeerConnection.prototype = Orig.prototype;
});
await page.goto(URL, { waitUntil: 'domcontentloaded' });
await page.waitForTimeout(6000);
await page.mouse.click(640, 360).catch(() => {});
await page.waitForTimeout(12000);
const r = await page.evaluate(async () => {
  const out = { elements: [], receivers: [], inboundAudio: [] };
  for (const el of document.querySelectorAll('video,audio')) {
    const s = el.srcObject;
    out.elements.push({ tag: el.tagName, muted: el.muted, paused: el.paused,
      audioTracks: s ? s.getAudioTracks().length : -1, videoTracks: s ? s.getVideoTracks().length : -1 });
  }
  for (const pc of (window.__pcs || [])) {
    for (const rcv of pc.getReceivers()) {
      out.receivers.push({ kind: rcv.track?.kind, id: rcv.track?.id, muted: rcv.track?.muted, state: rcv.track?.readyState });
    }
    const stats = await pc.getStats();
    stats.forEach(s => {
      if (s.type === 'inbound-rtp' && s.kind === 'audio') {
        out.inboundAudio.push({ bytes: s.bytesReceived, packets: s.packetsReceived,
          level: s.audioLevel, energy: s.totalAudioEnergy, dur: s.totalSamplesDuration });
      }
    });
  }
  return out;
});
console.log('AUDIO2 ' + JSON.stringify(r));
await browser.close();
