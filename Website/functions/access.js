// Cloudflare Pages Function for /access: the "Check private access" links on index.html.
//
// The private stream's address is a host on the CI tailnet, and this repository is public, so the
// address is not written anywhere in it. It is configuration instead: the Pages environment variable
// METAHUMAN_ACCESS_URL (see README.md), read on each request. When it is set to an https:// URL the
// visitor is redirected there; when it is unset or unusable the visitor gets a short page saying
// that access is by invitation, rather than a link to nowhere.

const NOT_CONFIGURED = `<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <meta name="robots" content="noindex" />
    <title>MetaHuman — private access</title>
    <link rel="stylesheet" href="/styles.css" />
  </head>
  <body>
    <main class="closing">
      <p class="eyebrow">Private preview</p>
      <h2>Access is by invitation.</h2>
      <p class="closing-copy">The live stream runs on a private network. If you have been invited, use the link you were given and sign in with CI Portal.</p>
      <a class="button button-light" href="/">Back to MetaHuman</a>
    </main>
  </body>
</html>
`;

// Only an absolute https:// URL is accepted, so a typo in the setting cannot turn this route into
// an open redirect to some other scheme.
function accessUrl(value) {
  if (typeof value !== "string" || value.trim() === "") {
    return null;
  }
  try {
    const url = new URL(value.trim());
    return url.protocol === "https:" ? url.toString() : null;
  } catch {
    return null;
  }
}

function handle({ env }) {
  const target = accessUrl(env.METAHUMAN_ACCESS_URL);
  if (target) {
    return new Response(null, {
      status: 302,
      headers: {
        Location: target,
        // The target is configuration and may change; never let a cache pin an old one.
        "Cache-Control": "no-store",
      },
    });
  }
  return new Response(NOT_CONFIGURED, {
    status: 503,
    headers: { "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store" },
  });
}

export const onRequestGet = handle;
export const onRequestHead = handle;
