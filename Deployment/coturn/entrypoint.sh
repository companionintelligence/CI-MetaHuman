#!/bin/sh
# Entrypoint for the `turnserver` service in docker-compose.yml.
#
# Renders coturn's runtime configuration from turnserver.conf (beside this file) plus the values
# that must not be committed or that differ per host, then runs turnserver on it. Mounted read-only
# and run by /bin/sh, so it needs no executable bit. It runs as the image's unprivileged user
# (nobody:nogroup) and writes only to $TMPDIR inside the container.
#
# The secret goes into a 0600 file rather than onto the command line (`--static-auth-secret=...`)
# because process arguments are readable by every user on the host through `ps`.
set -eu

CONF_DIR="/etc/coturn/ci"
# docker-compose.yml bind-mounts TURN_TLS_CERT_FILE / TURN_TLS_KEY_FILE here, or /dev/null when
# they are unset -- which reads as empty, so "no certificate" and "not configured" are the same test.
TLS_CERT="/etc/coturn/tls/cert.pem"
TLS_KEY="/etc/coturn/tls/key.pem"
RUNTIME_CONF="${TMPDIR:-/tmp}/ci-turnserver.conf"

fail() {
    echo "ci-coturn: $*" >&2
    exit 1
}

secret="${TURN_STATIC_AUTH_SECRET:-}"
unset TURN_STATIC_AUTH_SECRET
# Without the secret coturn could only run open or with static accounts, which is exactly what this
# configuration exists to prevent -- so stop, loudly, rather than start something weaker.
[ -n "$secret" ] || fail "TURN_STATIC_AUTH_SECRET is empty; refusing to start a relay without credentials. Set it in Deployment/.env (openssl rand -hex 32)."
# coturn and the signalling server must HMAC with byte-identical secrets, and a mismatch surfaces
# only as every relay attempt failing with 401. Keep to characters both read back unchanged.
case "$secret" in
    *[!A-Za-z0-9+/=._~-]*) fail "TURN_STATIC_AUTH_SECRET may contain only letters, digits and + / = . _ ~ - (openssl rand -hex 32 is safe)." ;;
esac

# --- Peers the relay may reach despite turnserver.conf's denied-peer-ip ranges --------------------
# PUBLIC_IP is the address this host advertises -- the TURN URL and, on this host network, the
# Unreal streamer's own candidates -- so a browser's relay has to be able to reach it. On a tailnet
# deployment it sits inside the denied 100.64/10, hence the exception. TURN_ALLOWED_PEER_IPS adds
# more, comma-separated, each an address or a first-last range (e.g. a LAN address of this host).
allowed=""
add_allowed() { # <value> <variable name> <strict|lenient>
    # Only address characters reach the config file: no newline, '=', '#' or space can smuggle in
    # another directive.
    case "$1" in
        "") return 0 ;;
        -* | *- | *-*-* | *[!0-9A-Fa-f.:-]*) ;;
        *.* | *:*)
            allowed="${allowed}allowed-peer-ip=$1
"
            return 0 ;;
    esac
    [ "$3" = lenient ] || fail "$2: '$1' is not an IP address or a first-last range."
    echo "ci-coturn: $2 '$1' is not an IP address, so it is not exempt from denied-peer-ip." >&2
}
# PUBLIC_IP may legitimately be a public address or, rarely, a name; neither needs an exemption, so
# a non-address there is a warning. TURN_ALLOWED_PEER_IPS exists only to be an exemption, so a typo
# there stops the container.
add_allowed "${PUBLIC_IP:-}" PUBLIC_IP lenient
set -f # the loop below splits an unquoted variable; do not let it glob too
old_ifs="$IFS"
IFS=','
for peer in ${TURN_ALLOWED_PEER_IPS:-}; do
    # Tolerate "a, b": strip the spaces a hand-edited list picks up.
    add_allowed "$(printf '%s' "$peer" | tr -d ' ')" TURN_ALLOWED_PEER_IPS strict
done
IFS="$old_ifs"
set +f
[ -n "$allowed" ] || echo "ci-coturn: no address is exempt from denied-peer-ip (PUBLIC_IP is unset or not an address); relays reach public addresses only." >&2

# --- TLS listener (5349) ------------------------------------------------------------------------
tls=""
if [ -s "$TLS_CERT" ] || [ -s "$TLS_KEY" ]; then
    # Half a pair is a mistake, not a choice: refuse rather than quietly serve plain TURN only.
    { [ -s "$TLS_CERT" ] && [ -s "$TLS_KEY" ]; } || fail "set both TURN_TLS_CERT_FILE and TURN_TLS_KEY_FILE, or neither."
    [ -r "$TLS_KEY" ] && [ -r "$TLS_CERT" ] || fail "the TLS certificate and key must be readable by uid $(id -u) inside the container (e.g. chgrp $(id -g) key.pem && chmod 640 key.pem on the host)."
    tls="cert=$TLS_CERT
pkey=$TLS_KEY"
else
    tls="no-tls"
    echo "ci-coturn: no TLS certificate configured (TURN_TLS_CERT_FILE / TURN_TLS_KEY_FILE); serving STUN/TURN on 3478 without the TLS listener." >&2
fi

umask 077
{
    # A CRLF checkout would otherwise leave a carriage return on every value.
    tr -d '\r' < "$CONF_DIR/turnserver.conf"
    printf '\n# ---- appended by entrypoint.sh at container start ----\n'
    printf '%s' "$allowed"
    printf '%s\n' "$tls"
    printf 'static-auth-secret=%s\n' "$secret"
} > "$RUNTIME_CONF"

exec turnserver -c "$RUNTIME_CONF" "$@"
