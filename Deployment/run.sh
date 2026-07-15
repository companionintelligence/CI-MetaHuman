#!/usr/bin/env bash
#
# Build (if needed) and launch the containerized MetaHumanLocalLLM Pixel Streaming agent.
#   ./run.sh            # start using the pre-built project image
#   ./run.sh --rebuild  # rebuild the project image first (re-packages the UE app)
#
# Requires: Docker + Compose, the NVIDIA Container Toolkit, a GPU, and a filled-in .env.

set -euo pipefail
cd "$(dirname "$0")"

if [[ ! -f .env ]]; then
  echo "ERROR: .env not found. Copy .env.example to .env and fill it in." >&2
  exit 1
fi

# Load .env so we can derive/validate values before invoking compose.
set -a
# shellcheck disable=SC1091
source .env
set +a

# Auto-detect the host's public IP if not pinned in .env.
if [[ -z "${PUBLIC_IP:-}" ]]; then
  if command -v curl >/dev/null; then
    PUBLIC_IP="$(curl -s https://api.ipify.org)"
  elif command -v wget >/dev/null; then
    PUBLIC_IP="$(wget -qO- https://api.ipify.org)"
  else
    echo "ERROR: PUBLIC_IP unset and neither curl nor wget is available to detect it." >&2
    exit 1
  fi
  export PUBLIC_IP
  echo "Detected PUBLIC_IP=${PUBLIC_IP}"
fi

COMPOSE_FLAGS=""
if [[ "$*" == *"--rebuild"* ]]; then
  COMPOSE_FLAGS="--build"
fi

# Prefer the Compose v2 plugin, fall back to the standalone binary.
COMPOSE="docker compose"
if command -v docker-compose >/dev/null; then
  COMPOSE="docker-compose"
fi

exec $COMPOSE up --force-recreate ${COMPOSE_FLAGS}
