#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.
#
# PATCHED for ci-metahermit: this is the stock SignallingWebServer start.sh with these changes:
#   - "--https_redirect" removed from SERVER_ARGS (it binds port 80, which is already taken on
#     core-2),
#   - explicit "--player_port=8090 --streamer_port=8888" added, and
#   - the ICE servers are built here instead of by setup_turn_stun (see "ICE servers" below).
# Everything else — the common.sh sourcing, parse_args, setup (npm install/link/build),
# set_public_ip, and start_wilbur (npm start) — is verbatim stock, so the build/link/run path is
# exactly Epic's (my earlier hand-rolled launcher skipped setup's workspace linking and failed at
# runtime). Mounted over the stock path from the host.
SCRIPT_DIR="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"

. ${SCRIPT_DIR}/common.sh

parse_args $@
setup $@
# Compose pins this to the tailnet address for a private deployment. Keep the stock public-IP
# discovery only for standalone deployments that did not set PUBLIC_IP.
if [[ -z "${PUBLIC_IP:-}" ]]; then
    set_public_ip
fi

# --- ICE servers ---------------------------------------------------------------------------------
# setup_turn_stun is deliberately NOT called. Without --start-turn all it does is fill in defaults,
# and those are a public Google STUN server and Epic's well-known static TURN account -- which the
# stock script then wrote, password included, into the peer options every browser is sent.
#
# Here the TURN entry carries NO credentials. When TURN_STATIC_AUTH_SECRET is set, the server's
# --turn_secret_file option mints a username/credential pair for each peer as it connects (TURN
# REST API: the username carries an expiry, the credential is an HMAC of it under the secret coturn
# also holds), valid for TURN_CREDENTIAL_TTL seconds for a player. Without the secret TURN is left
# out entirely: a TURN entry nobody can authenticate against only fails later, at ICE, and looks
# like a network problem.
#
# The secret reaches the server through a file in a root-only directory rather than --turn_secret,
# because SERVER_ARGS is printed by print_config and every argument is visible in `ps`.
RUNTIME_DIR="/tmp/ci-signalling"
PEER_OPTIONS_FILE="${RUNTIME_DIR}/peer_options.json"
TURN_SECRET_FILE="${RUNTIME_DIR}/turn_secret"
mkdir -p "${RUNTIME_DIR}" && chmod 700 "${RUNTIME_DIR}" && rm -f "${TURN_SECRET_FILE}" || exit 1

# Compose writes these as "${PUBLIC_IP}:3478", which arrives as ":3478" when PUBLIC_IP was blank
# there; complete it with the address set_public_ip resolved above.
complete_host() {
    if [[ "$1" == :* ]]; then echo "${PUBLIC_IP}$1"; else echo "$1"; fi
}
# Each value becomes part of an ICE URL inside a JSON string, so refuse anything that could break
# either: host names, IPv4, bracketed IPv6 and a port only.
HOST_PORT_RE='^[]A-Za-z0-9._:[-]+$'
ICE_SERVERS=()
add_ice_server() { # <scheme> <host:port> [suffix]
    if [[ "$2" =~ $HOST_PORT_RE ]]; then
        ICE_SERVERS+=("{\"urls\":[\"$1:$2${3:-}\"]}")
    else
        echo "WARNING: ignoring $1 server '$2': expected host:port" >&2
    fi
}

STUN_SERVER="$(complete_host "${STUN_SERVER:-}")"
TURN_SERVER="$(complete_host "${TURN_SERVER:-}")"

if [[ -n "${STUN_SERVER}" ]]; then
    add_ice_server "stun" "${STUN_SERVER}"
fi

TURN_ARGS=""
if [[ -n "${TURN_SERVER}" && -n "${TURN_STATIC_AUTH_SECRET:-}" ]]; then
    TURN_TTL="${TURN_CREDENTIAL_TTL:-3600}"
    if [[ ! "${TURN_TTL}" =~ ^[1-9][0-9]*$ ]]; then
        echo "WARNING: TURN_CREDENTIAL_TTL '${TURN_TTL}' is not a positive number of seconds; using 3600" >&2
        TURN_TTL=3600
    fi
    (umask 077 && printf '%s' "${TURN_STATIC_AUTH_SECRET}" > "${TURN_SECRET_FILE}") || exit 1
    add_ice_server "turn" "${TURN_SERVER}"
    # coturn's TLS listener, when it has a certificate. The server fills in credentials for turns:
    # entries exactly as for turn: ones. The browser checks the certificate against this name.
    if [[ -n "${TURN_TLS_SERVER:-}" ]]; then
        add_ice_server "turns" "${TURN_TLS_SERVER}" "?transport=tcp"
    fi
    TURN_ARGS=" --turn_secret_file=${TURN_SECRET_FILE} --turn_ttl=${TURN_TTL}"
elif [[ -n "${TURN_SERVER}" ]]; then
    echo "WARNING: TURN_STATIC_AUTH_SECRET is not set, so no TURN credentials can be issued." \
         "Offering STUN only; peers that cannot connect directly will fail." >&2
fi
unset TURN_STATIC_AUTH_SECRET

(IFS=,; printf '{"iceServers":[%s]}\n' "${ICE_SERVERS[*]}") > "${PEER_OPTIONS_FILE}" || exit 1

SERVER_ARGS+=" --serve --player_port=8090 --streamer_port=8888 --public_ip=${PUBLIC_IP}"
SERVER_ARGS+=" --peer_options_file=${PEER_OPTIONS_FILE}${TURN_ARGS}"
if [[ ! -z "$FRONTEND_DIR" ]]; then
    SERVER_ARGS+=" --http_root=\"$FRONTEND_DIR\""
fi

print_config
start_wilbur
