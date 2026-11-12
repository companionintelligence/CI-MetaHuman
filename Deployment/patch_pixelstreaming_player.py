#!/usr/bin/env python3
"""Patch Epic's pinned Pixel Streaming player defaults, attach inbound audio, and self-host its font.

Two independent patches, each applied once and recognised by its marker:

- the stream fixes (HoveringMouse and VP8 URL defaults, inbound audio attached to the <video>);
- the font: Epic's page loads Montserrat and Michroma from Google Fonts, so every viewer's
  browser contacted fonts.googleapis.com and fonts.gstatic.com before the stream started. Those
  links are removed and Montserrat SemiBold (the one weight the page asked Google for, SIL Open Font
  License 1.1) is served by the signalling image itself from fonts/ next to the page:
  Dockerfile.signalling copies Deployment/pixelstreaming/fonts/ there. Michroma is not vendored, so
  the UI library's own "'Michroma', sans-serif" falls back to the browser's sans-serif.

A page that still references a Google Fonts host after patching is refused rather than written.
"""

from pathlib import Path
import re
import sys


MARKER = "ci-companion-stream-fixes-v1"
SCRIPT = r"""<script data-ci-companion="ci-companion-stream-fixes-v1">
(function () {
  try {
    var url = new URL(window.location.href);
    if (!url.searchParams.has('HoveringMouse')) url.searchParams.set('HoveringMouse', 'true');
    if (!url.searchParams.has('PreferredCodec')) url.searchParams.set('PreferredCodec', 'VP8');
    window.history.replaceState(null, '', url.toString());
  } catch (e) { /* keep the stock player usable if URL APIs are unavailable */ }
})();
(function () {
  try {
    var OriginalPeerConnection = window.RTCPeerConnection;
    if (!OriginalPeerConnection) return;
    var peers = [];
    window.RTCPeerConnection = function () {
      var peer = new (Function.prototype.bind.apply(
        OriginalPeerConnection, [null].concat([].slice.call(arguments))))();
      peers.push(peer);
      return peer;
    };
    window.RTCPeerConnection.prototype = OriginalPeerConnection.prototype;
    window.setInterval(function () {
      var video = document.querySelector('video');
      if (!video || !video.srcObject) return;
      peers.forEach(function (peer) {
        peer.getReceivers().forEach(function (receiver) {
          var track = receiver.track;
          if (!track || track.kind !== 'audio' ||
              video.srcObject.getAudioTracks().indexOf(track) !== -1) return;
          video.srcObject.addTrack(track);
          video.muted = false;
          var playPromise = video.play();
          if (playPromise && playPromise.catch) playPromise.catch(function () {});
          console.log('[audio-fix] attached inbound audio track');
        });
      });
    }, 1000);
  } catch (e) { /* never let the patch prevent the stock player from loading */ }
})();
</script>
"""

FONT_MARKER = "ci-companion-self-hosted-font-v1"
# fonts/ is relative to the page, like the page's own player.js and images/. An inline <style> is
# left alone by the frontend's html-loader, which only rewrites attributes such as <link href>, so
# the URL reaches the browser exactly as written.
FONT_STYLE = """<style data-ci-companion="ci-companion-self-hosted-font-v1">
/* Montserrat SemiBold, served from this image (fonts/OFL-Montserrat.txt: SIL Open Font License 1.1).
   It replaces the Google Fonts stylesheet Epic's page loads. */
@font-face {
  font-family: 'Montserrat';
  font-style: normal;
  font-weight: 600;
  font-display: swap;
  src: url('fonts/Montserrat-SemiBold.ttf') format('truetype');
}
</style>
"""

GOOGLE_FONT_HOSTS = ("fonts.googleapis.com", "fonts.gstatic.com")
# Any <link> whose href points at a Google Fonts host: the two preconnects and the stylesheet.
GOOGLE_FONT_LINK = re.compile(
    r"[ \t]*<link\b[^>]*\bhref=[\"']https?://fonts\.(?:googleapis|gstatic)\.com[^>]*>[ \t]*\r?\n?",
    re.IGNORECASE)
# The page body names Montserrat with no fallback at all; give it one, so a missing font file
# degrades to a sans-serif instead of the browser's default serif.
BODY_FONT = re.compile(r"font-family:\s*'?Montserrat'?\s*;")
BODY_FONT_STACK = "font-family: 'Montserrat', system-ui, -apple-system, 'Segoe UI', Roboto, Arial, sans-serif;"


def check_page(html: str) -> None:
    """Raises ValueError for a page this script cannot patch safely."""
    if html.count("</head>") != 1:
        raise ValueError("expected exactly one </head>")
    if "pixel streaming" not in html.lower():
        raise ValueError("not an Unreal Pixel Streaming player page")


def patch_fonts(html: str) -> str:
    """The font patch alone: drop the Google Fonts links, serve Montserrat from fonts/."""
    patched = GOOGLE_FONT_LINK.sub("", html)
    if FONT_MARKER not in patched:
        # Before the style block goes in: its @font-face also says font-family: 'Montserrat';.
        patched = BODY_FONT.sub(BODY_FONT_STACK, patched)
        patched = patched.replace("</head>", FONT_STYLE + "</head>", 1)
    left = [h for h in GOOGLE_FONT_HOSTS if h in patched]
    if left:
        raise ValueError("still references {} after patching".format(", ".join(left)))
    return patched


def patch_html(html: str) -> str:
    """Returns the fully patched page; raises ValueError for a page it cannot patch safely."""
    if MARKER in html and FONT_MARKER in html and not any(h in html for h in GOOGLE_FONT_HOSTS):
        return html
    check_page(html)
    patched = html
    if MARKER not in patched:
        patched = patched.replace("</head>", SCRIPT + "\n</head>", 1)
    return patch_fonts(patched)


def patch_player(path: Path) -> bool:
    html = path.read_text(encoding="utf-8")
    try:
        patched = patch_html(html)
    except ValueError as error:
        raise ValueError("{}: {}".format(error, path)) from None
    if patched == html:
        return False
    path.write_text(patched, encoding="utf-8")
    return True


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_pixelstreaming_player.py <player.html>")
    changed = patch_player(Path(sys.argv[1]))
    print("patched" if changed else "already patched")
