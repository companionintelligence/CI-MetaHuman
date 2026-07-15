#!/bin/bash
# Custom launcher for the Epic PixelStreaming 5.5 SignallingWebServer image.
#
# The image's stock start.sh hardcodes `--https_redirect`, which binds port 80 — already
# taken on core-2 by the CI stack. This bypasses it and runs the server directly on an
# explicit HTTP port with the correct underscore-style flags (the stock image's docs use
# camelCase in some places, but the wilbur server wants --http_port / --streamer_port).
set -e

# The release image ships source, not a build — compile once. Must link the sibling
# workspace packages first (this is what start.sh's start_wilbur does); without the links
# the build fails with "Cannot find module '@epicgames-ps/lib-pixelstreamingsignalling-...'".
if [ ! -f /SignallingWebServer/build/index.js ]; then
  echo "[signalling-start] building (linking /Common and /Signalling workspaces)..."
  cd /Signalling && npm link ../Common && npm run build
  cd /SignallingWebServer && npm link ../Signalling && npm run build
fi
cd /SignallingWebServer

echo "[signalling-start] starting wilbur on http_port=8090 streamer_port=8888 public_ip=${PUBLIC_IP:-127.0.0.1}"
exec npm start -- \
  --serve \
  --http_port=8090 \
  --streamer_port=8888 \
  --public_ip="${PUBLIC_IP:-127.0.0.1}"
