# MetaHuman landing page

This static explainer is deployed as the Cloudflare Pages project `metahuman` at
`metahuman.pages.dev`, with `metahuman.ci.computer` attached as a custom domain. Its proxied CNAME
now points to the Pages project; allow Cloudflare to finish certificate validation before relying
on the custom hostname. The explainer is separate from the private Pixel Streaming endpoint: only
the explainer is public. The experience link resolves to the Tailscale-only host and is protected
by CI Portal sign-in.

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
