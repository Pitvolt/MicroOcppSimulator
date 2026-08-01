# Staging OCPP simulator bundle

A small always-off Fly.io machine running MicroOcppSimulator, pre-pointed at
Pitvolt staging (`wss://staging.ocpp.pitvolt.com`) and the e2e charger
(`PVE2-VAJN`). Auto-starts on first request; idles for free.

Runs the same binary as the upstream repo (built by `deploy/staging/Dockerfile`),
just with a runtime entrypoint that writes `mo_store/ws-conn.jsn` from
`STATION_ID` + `WSS_URL` env vars so one image works for any charger.

## First-time bring-up (Render.com — no card required)

Free web-service tier, 750 h/month, sleeps after 15 min idle, wakes in ~30s.

1. Log in at <https://dashboard.render.com> (GitHub SSO — no card asked).
2. **New → Blueprint → Connect** the `Pitvolt/MicroOcppSimulator` repo.
3. Render reads `deploy/staging/render.yaml`, creates the service, and
   deploys from `deploy/staging/Dockerfile`. First build takes ~5 min.
4. Public URL: `https://pitvolt-ocpp-sim.onrender.com` — the simulator's
   web UI. Drive charging sessions from the browser; staging
   (`stg.api.pitvolt.com`) treats it as a real charger.

Change the targeted charger from the Render dashboard → Environment →
edit `STATION_ID` → save (Render redeploys automatically).

## Alternative: Fly.io bring-up (requires a card)

```bash
brew install flyctl && flyctl auth login
flyctl launch --no-deploy --copy-config --name pitvolt-ocpp-sim \
  --org personal --region fra --config deploy/staging/fly.toml
flyctl deploy --config deploy/staging/fly.toml \
  --dockerfile deploy/staging/Dockerfile
# URL: https://pitvolt-ocpp-sim.fly.dev
```

## Retarget a different charger

- **Render**: Dashboard → Environment → edit `STATION_ID` (or `WSS_URL`) →
  save. Redeploys automatically.
- **Fly**: `flyctl secrets set STATION_ID=<new-station-id>`.

## Local dry-run without Fly

```bash
docker build -f deploy/staging/Dockerfile -t pitvolt/micro-ocpp-sim:staging .
docker run -p 8000:8000 \
  -e STATION_ID=PVE2-VAJN \
  -e WSS_URL=wss://staging.ocpp.pitvolt.com \
  pitvolt/micro-ocpp-sim:staging
# UI at http://localhost:8000
```

## Cost + safety

- Auto-stops when idle; a warm start is ~2s. Effectively free on Fly's
  hobby tier.
- Publicly reachable (no auth). Fine because it's a staging tool that can
  only drive whatever charger `STATION_ID` names. **Never** point it at
  production without adding a Fly access policy or basic auth in front.
