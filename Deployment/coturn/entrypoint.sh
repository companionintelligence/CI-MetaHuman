#!/bin/sh
# Entrypoint for the `turnserver` service in docker-compose.yml.
#
# Renders coturn's runtime configuration from turnserver.conf (beside this file) plus the values
# that must not be committed, then runs turnserver on it. Mounted read-only and run by /bin/sh, so
# it needs no executable bit. It runs as the image's unprivileged user and writes only to $TMPDIR
# inside the container.
#
# The secret goes into a 0600 file rather than onto the command line (`--static-auth-secret=...`)
# because process arguments are readable by every user on the host through `ps`.
set -eu

CONF_DIR="/etc/coturn/ci"
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

umask 077
{
    # A CRLF checkout would otherwise leave a carriage return on every value.
    tr -d '\r' < "$CONF_DIR/turnserver.conf"
    printf '\n# ---- appended by entrypoint.sh at container start ----\n'
    printf 'static-auth-secret=%s\n' "$secret"
} > "$RUNTIME_CONF"

exec turnserver -c "$RUNTIME_CONF" "$@"
