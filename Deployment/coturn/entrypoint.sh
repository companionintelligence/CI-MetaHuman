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
# The peer a relay exists to reach is the Unreal streamer, and it runs on THIS host. coturn and the
# streamer share one network namespace (network_mode: host on both), so the streamer's host
# candidates are this host's interface addresses -- and so is every relay coturn allocates, the
# streamer's own included: with no relay-ip set, a relay is bound to the local address its client
# reached coturn on. This host's own addresses are therefore exempt, read from /proc/net at start
# (host_addresses below). Epic's stock launcher exempts the host too, by one address
# (setup_turn_stun: --allowed-peer-ip=$LOCAL_IP).
#
# PUBLIC_IP alone is not enough: it is an interface address only on a tailnet host or one with a
# public address bound to it. Behind NAT -- a cloud VM with 1:1 NAT, a port-forwarded box, and so
# whatever run.sh fetches from api.ipify.org when PUBLIC_IP is blank -- it is the NAT's address,
# every interface address is private, and coturn would answer the browser's CreatePermission for
# the streamer with 403 Forbidden IP: a relayed viewer would get nothing but an ICE failure.
#
# Each exemption is one address, never a subnet: the rest of the LAN, the containers on Docker's
# bridges and the rest of the tailnet stay denied, and what opens is this host itself, over UDP only
# (no-tcp-relay). Loopback (which coturn refuses before it reads any exemption) and link-local are
# left out. An address the host gains after this container starts is not covered until turnserver
# restarts, which is why PUBLIC_IP is still exempted by value: a tailnet address may come up after
# Docker. TURN_ALLOWED_PEER_IPS adds more, comma-separated, each an address or a first-last range,
# for a peer on ANOTHER machine inside a denied range.
nl='
'
allowed=""
exempt=""
add_allowed() { # <value> <label> <strict|lenient>
    # Only address characters reach the config file: no newline, '=', '#' or space can smuggle in
    # another directive.
    case "$1" in
        "") return 0 ;;
        -* | *- | *-*-* | *[!0-9A-Fa-f.:-]*) ;;
        *.* | *:*)
            case "$allowed" in
                # Listed already: PUBLIC_IP is usually one of this host's addresses too.
                *"allowed-peer-ip=$1$nl"*) ;;
                *)
                    allowed="${allowed}allowed-peer-ip=$1$nl"
                    exempt="${exempt:+$exempt }$1" ;;
            esac
            return 0 ;;
    esac
    [ "$3" = lenient ] || fail "$2: '$1' is not an IP address or a first-last range."
    echo "ci-coturn: $2 '$1' is not an IP address, so it is not exempt from denied-peer-ip." >&2
}
# This host's own addresses, one per line: every unicast address on its interfaces except loopback
# and link-local. /proc/net shows the reader's network namespace, which here is the host's, the one
# the streamer is in. (Under rootless Docker that is RootlessKit's namespace, shared just the same.)
host_addresses() {
    # IPv4: /proc/net/fib_trie prints a local address as a "|-- a.b.c.d" line followed by a
    # "/32 host LOCAL" line, once per routing table that holds it; add_allowed drops repeats.
    if [ -r /proc/net/fib_trie ]; then
        ip4=""
        while IFS= read -r line; do
            case "$line" in
                *"|-- "*) ip4="${line##*"|-- "}" ;;
                *"/32 host LOCAL"*)
                    case "$ip4" in
                        127.* | 169.254.*) ;;
                        *) printf '%s\n' "$ip4" ;;
                    esac ;;
            esac
        done < /proc/net/fib_trie
    fi
    # IPv6: a /proc/net/if_inet6 line is "<32 hex digits> <ifindex> <prefix length> <scope> <flags>
    # <name>". Scope 00 is global, which includes unique-local; 10 is loopback and 20 link-local.
    if [ -r /proc/net/if_inet6 ]; then
        while read -r hex _ _ scope _; do
            [ "$scope" = 00 ] && [ "${#hex}" -eq 32 ] || continue
            ip6=""
            while [ -n "$hex" ]; do # eight groups of four digits, colon-separated
                rest="${hex#????}"
                ip6="${ip6:+$ip6:}${hex%"$rest"}"
                hex="$rest"
            done
            printf '%s\n' "$ip6"
        done < /proc/net/if_inet6
    fi
}
set -f # the loops below split unquoted words; do not let them glob too
for addr in $(host_addresses); do
    add_allowed "$addr" "host address" lenient
done
# PUBLIC_IP may legitimately be a public address or, rarely, a name; neither needs an exemption, so
# a non-address there is a warning. TURN_ALLOWED_PEER_IPS exists only to be an exemption, so a typo
# there stops the container.
add_allowed "${PUBLIC_IP:-}" PUBLIC_IP lenient
old_ifs="$IFS"
IFS=','
for peer in ${TURN_ALLOWED_PEER_IPS:-}; do
    # Tolerate "a, b": strip the spaces a hand-edited list picks up.
    add_allowed "$(printf '%s' "$peer" | tr -d ' ')" TURN_ALLOWED_PEER_IPS strict
done
IFS="$old_ifs"
set +f
if [ -n "$allowed" ]; then
    echo "ci-coturn: exempt from denied-peer-ip: $exempt" >&2
else
    echo "ci-coturn: no address is exempt from denied-peer-ip (none found in /proc/net, and PUBLIC_IP is unset or not an address); relays reach public addresses only." >&2
fi

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
