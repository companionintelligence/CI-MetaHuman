#!/usr/bin/env python3
"""Patch Epic's pinned Pixel Streaming player defaults and attach inbound audio."""

from pathlib import Path
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


def patch_player(path: Path) -> bool:
    html = path.read_text(encoding="utf-8")
    if MARKER in html:
        return False
    if html.count("</head>") != 1:
        raise ValueError("expected exactly one </head> in {}".format(path))
    if "pixel streaming" not in html.lower():
        raise ValueError("not an Unreal Pixel Streaming player page: {}".format(path))
    path.write_text(html.replace("</head>", SCRIPT + "\n</head>", 1), encoding="utf-8")
    return True


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_pixelstreaming_player.py <player.html>")
    changed = patch_player(Path(sys.argv[1]))
    print("patched" if changed else "already patched")
