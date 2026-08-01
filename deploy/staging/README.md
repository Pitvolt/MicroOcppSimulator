# Staging OCPP simulator bundle

A small always-off Fly.io machine running MicroOcppSimulator, pre-pointed at
Pitvolt staging (`wss://staging.ocpp.pitvolt.com`) and the e2e charger
(`PVE2-VAJN`). Auto-starts on first request; idles for free.

Runs the same binary as the upstream repo (built by `deploy/staging/Dockerfile`),
just with a runtime entrypoint that writes `mo_store/ws-conn.jsn` from
`STATION_ID` + `WSS_URL` env vars so one image works for any charger.

## First-time bring-up

Prereq: `brew install flyctl` and `flyctl auth login`.

```bash
cd deploy/staging
# Create the Fly app (uses this fly.toml, does not deploy yet).
flyctl launch --no-deploy --copy-config --name pitvolt-ocpp-sim \
  --org personal --region fra
# Build + deploy from the REPO ROOT (Dockerfile expects the whole repo).
cd ../..
flyctl deploy --config deploy/staging/fly.toml --dockerfile deploy/staging/Dockerfile
```

Once live: <https://pitvolt-ocpp-sim.fly.dev> — the simulator's web UI.
Drive charging sessions from the browser; the running backend on staging
(`stg.api.pitvolt.com`) treats it as a real charger.

## Retarget a different charger

```bash
flyctl secrets set STATION_ID=<new-station-id>
# For a completely different environment:
flyctl secrets set WSS_URL=wss://<other-env>/ocpp
```

Fly restarts the machine automatically.

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
