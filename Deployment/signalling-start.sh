#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.
#
# PATCHED for ci-metahermit: this is the stock SignallingWebServer start.sh with two changes:
#   - "--https_redirect" removed from SERVER_ARGS (it binds port 80, which is already taken on
#     core-2), and
#   - explicit "--http_port=8090 --streamer_port=8888" added.
# Everything else — the common.sh sourcing, parse_args, setup (npm install/link/build),
# set_public_ip, setup_turn_stun, and start_wilbur (npm start) — is verbatim stock, so the
# build/link/run path is exactly Epic's (my earlier hand-rolled launcher skipped setup's
# workspace linking and failed at runtime). Mounted over the stock path from the host.
SCRIPT_DIR="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"

. ${SCRIPT_DIR}/common.sh

parse_args $@
setup $@
set_public_ip
setup_turn_stun "bg"

SERVER_ARGS+=" --serve --player_port=8090 --streamer_port=8888 --public_ip=${PUBLIC_IP}"
if [[ ! -z "$STUN_SERVER" && ! -z "$TURN_SERVER" ]]; then
    PEER_OPTIONS='{\"iceServers\":[{\"urls\":[\"stun:'${STUN_SERVER}'\",\"turn:'${TURN_SERVER}'\"],\"username\":\"'${TURN_USER}'\",\"credential\":\"'${TURN_PASS}'\"}]}'
elif [[ ! -z "$STUN_SERVER" ]]; then
    PEER_OPTIONS='{\"iceServers\":[{\"urls\":[\"stun:'${STUN_SERVER}'\"]}]}'
elif [[ ! -z "$TURN_SERVER" ]]; then
    PEER_OPTIONS='{\"iceServers\":[{\"urls\":[\"turn:'${TURN_SERVER}'\"],\"username\":\"'${TURN_USER}'\",\"credentials\":\"'${TURN_PASS}'\"}]}'
fi
if [[ ! -z "$PEER_OPTIONS" ]]; then
    SERVER_ARGS+=" --peer_options=\"${PEER_OPTIONS}\""
fi
if [[ ! -z "$FRONTEND_DIR" ]]; then
    SERVER_ARGS+=" --http_root=\"$FRONTEND_DIR\""
fi

print_config
start_wilbur
