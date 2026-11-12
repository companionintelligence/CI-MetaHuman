# MetaHuman landing page

This static explainer is deployed as the Cloudflare Pages project `metahuman` at
`metahuman.pages.dev`, with `metahuman.ci.computer` attached as a custom domain. Its proxied CNAME
now points to the Pages project; allow Cloudflare to finish certificate validation before relying
on the custom hostname. The explainer is separate from the private Pixel Streaming endpoint: only
the explainer is public. The experience link resolves to the Tailscale-only host and is protected
by CI Portal sign-in.

## Private access link

The "Check private access" buttons point at `/access`, a Pages Function (`functions/access.js`),
not at the stream itself: the stream's tailnet host name is deployment configuration and is kept
out of this public repository. The function redirects to the `https://` URL in the Pages
environment variable `METAHUMAN_ACCESS_URL`. Unset (or not `https://`), `/access` answers with a
short "access is by invitation" page instead.

Set it once per environment, in Cloudflare Pages → `metahuman` → Settings → Variables and Secrets,
or from this directory:

```sh
npx wrangler pages secret put METAHUMAN_ACCESS_URL --project-name metahuman
```

For a local preview, pass it on the command line rather than writing it to a file here:

```sh
npx wrangler pages dev . --binding METAHUMAN_ACCESS_URL=https://<stream-host>/
```

## Preview

```sh
npx wrangler pages dev .
```

## Deploy

From this directory, after reviewing the static page changes:

```sh
npx wrangler pages deploy . --project-name metahuman
```

Custom-domain setup is managed in Cloudflare Pages → `metahuman` → Custom domains.
