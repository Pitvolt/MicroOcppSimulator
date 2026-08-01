# Prompt: Redesign the MicroOcppSimulator Web UI

Copy everything below this line into a fresh Claude session opened at the repo root.

---

## Mission

Redesign the web UI of this OCPP charge-point simulator from scratch. The current UI is a dated, form-heavy "debug dashboard" (Kube CSS, fieldsets, manual Fetch buttons, raw protocol booleans). I want a modern, intuitive, human-centred simulator UI that makes an EV-charging session feel tangible — plug in a car, tap a card, watch it charge — while the C++ simulator underneath stays exactly as it is.

**Hard rule: NO CORE LOGIC CHANGES.** Everything in `lib/` (MicroOcpp, MicroOcppMongoose, mongoose, mbedtls, ArduinoJson) and the simulation logic in `src/evse.cpp`, `src/main.cpp`, `src/net_mongoose.cpp` is untouchable. The only C++ file you may lightly touch is `src/api.cpp` / `src/api.h` (the HTTP glue layer) — and only to rename/alias endpoints or JSON field names so the API reads more human, or to add a trivial aggregate GET. Never change simulation behaviour, OCPP behaviour, timing, or state machines.

## Architecture you must preserve

- The C++ binary (mongoose web server) serves **one single gzipped HTML file** — `public/bundle.html.gz` — at `/`, with `Content-Encoding: gzip`. All CSS and JS must be inlined into that one file. No external assets, no CDN, no separate JS/CSS files at runtime.
- The webapp source lives in the `webapp-src/` git submodule (Preact + webpack). Build pipeline:
  1. `npm run build` (webpack prod → `dist/main.js` + `dist/main.css`)
  2. `npm run compress` (`deploy/bundle_all.js` inlines CSS+JS into `dist/bundle.html`, gzips it)
  3. `build-webapp/build_webapp.sh` moves `bundle.html.gz` → `../public/`
  4. Requires a `.env.production` file in `webapp-src/` defining `API_ROOT` (empty string = same origin).
- Keep the stack lightweight: Preact is fine (3KB), plain CSS or a tiny utility layer is fine. **No React, no Tailwind-via-CDN, no heavy component libraries.** Target: final `bundle.html.gz` stays under ~150KB (it's currently ~36KB).
- The UI talks to the backend via **HTTP polling only** (no browser websocket, no SSE). Current UI polls at 1s intervals on the Status page; that cadence is fine.
- CORS is wide open on the server; optional HTTP Basic Auth may be configured but the UI doesn't handle it — ignore auth.

## The API contract (current, verified from `src/api.cpp`)

Legacy JSON API (what the UI uses today):

| Endpoint | Methods | Body / Response |
|---|---|---|
| `/connectors` | GET | `["1","2"]` (2 simulated connectors, IDs are 1-indexed strings) |
| `/connector/{id}/evse` | GET, POST | `{evPlugged, evsePlugged, evReady, evseReady: bool, chargePointStatus: string}` — status is an OCPP value: Available, Preparing, Charging, SuspendedEV, SuspendedEVSE, Finishing, Faulted, … |
| `/connector/{id}/meter` | GET | `{energy: Wh int, power: W int, current: A float, voltage: V float}` |
| `/connector/{id}/transaction` | GET, POST | GET → `{idTag: string, transactionId: int (-1 = none), authorizationStatus}`; POST `{idTag}` simulates presenting an RFID card |
| `/connector/{id}/smartcharging` | GET | `{maxPower: W, maxCurrent: A}` |
| `/api/websocket` | GET, POST | `{backendUrl, chargeBoxId, authorizationKey, pingInterval, reconnectInterval}` — configures the simulator's OCPP connection to a CSMS |

Newer query-param API (plain-text responses, exists but unused by UI): `POST /api/plugin|plugout|end|state|authorize?evse_id=N&...`. You may use these or ignore them.

Semantics cheat-sheet (J1772):
- `evPlugged` = cable plugged into the car; `evReady` = car requests charge (State C); `evseReady` = charger contactor closed. Charging happens when all three true + transaction authorized.
- Typical happy path: plug in → present idTag (POST transaction) → status Preparing → Charging → present same idTag again or unplug to stop.

**Allowed API tweaks** (optional, in `src/api.cpp` only): add friendlier alias routes or field names (e.g. `carPlugged`, `carReady`, `chargerReady`, or a combined `GET /connector/{id}/summary` returning evse+meter+transaction in one response to reduce polling). If you rename anything, keep the old routes/fields working — other tools may depend on them.

## What the current UI is (so you know what you're replacing)

Six sidebar pages (`webapp-src/src/components/`):
1. **Status** — live cards per connector (1s polling), gradient background by status, three toggle chips (Plugged / EV Ready / EVSE Ready), raw meter numbers.
2. **OCPP 1.6 Connection** — form for backendUrl / chargeBoxId / authorizationKey with manual Fetch/Send buttons.
3. **Station** — station-level config forms.
4. **Connectors** — tabbed per-connector debug panels (EVSE booleans, meter, transaction idTag entry, smart charging), each section with its own Fetch button and success/error alert + timestamp.
5. **Network** — **hardcoded fake demo content** (fake WiFi list). 
6. **Security** — CA-cert textarea (real, posts to backend) + **hardcoded fake cipher-suite table**.

Known problems to NOT carry over:
- Everything is manual-fetch forms; only Status is live. Feels like Postman with CSS.
- Protocol jargon leaks everywhere ("evPlugged", "idTag", "OCPP 1.6 Websocket").
- Fake demo pages (Network, most of Security) mislead users — drop them or clearly mark real vs. mock, keep only the real CA-cert feature somewhere in a settings area.
- Existing bugs (don't replicate): `Gui.EvseLiveDisplay.js` uses `DateFormatter` without importing it (crashes on POST success path), and the POST omits `evsePlugged`, and compares the response against stale pre-update state.
- 5100-line kube.css for a handful of controls.

## Design brief

Build a **single-page, real-time simulator cockpit**. Suggested shape (you have creative freedom, but hit these notes):

1. **Connector-first layout.** One card/panel per connector showing a visual EV↔charger scene: a car, a cable, a charger. State is communicated visually (cable connected/disconnected, energy-flow animation while Charging, colour-coded status pill using human words: "Available", "Car connected", "Charging", "Paused by car", "Paused by charger", "Fault").
2. **Actions as scenario verbs, not booleans.** Primary buttons like "Plug in car", "Tap RFID card", "Stop charging", "Unplug" that map onto the boolean POSTs / transaction POST under the hood. Keep an "Advanced" disclosure per connector exposing the three raw toggles and idTag field for power users.
3. **Live telemetry.** Poll the meter and render power as a small live sparkline/chart (rolling ~60s window, canvas or inline SVG — no chart library), energy counter, current, voltage. Numbers formatted for humans (kW / kWh with sensible precision).
4. **Connection status always visible.** A persistent header strip showing the OCPP backend connection (backend URL, charge box ID) with a settings drawer/modal to edit the `/api/websocket` fields and the CA cert. Use plain labels: "Backend server", "Station ID", "Password/Auth key".
5. **Everything auto-refreshes.** No Fetch buttons anywhere. Poll continuously (1s for active connector state, slower for config), show a subtle "live" indicator, and surface network failures as one unobtrusive banner, not per-form alerts.
6. **Look and feel:** modern, clean, dark-mode-friendly (dark default is fine for a tool like this), CSS custom properties for theming, good typography, subtle motion for state transitions. Responsive down to a phone. All icons as inline SVG.

## Process requirements

- Work only in `webapp-src/` (and optionally `src/api.cpp`/`api.h` per the rules above). Keep the webpack + `bundle_all.js` pipeline working — you may simplify it, but the output must remain a single `bundle.html.gz` that `build-webapp/build_webapp.sh` places into `public/`.
- You may delete/replace all existing components, CSS, and the kube.css framework entirely.
- After building, verify end-to-end: build the C++ sim (`cmake -S . -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && cmake --build build --target mo_simulator`), run it, open `http://localhost:8000`, and exercise the golden path in a browser: plug in → tap card → observe Charging + live meter → stop → unplug. Also verify both connectors, the settings drawer round-trips `/api/websocket` values, and the UI degrades gracefully when the backend is unreachable.
- If you change `src/api.cpp`, prove old routes still respond identically (curl before/after).

Start by reading `src/api.cpp`, `webapp-src/src/index.js`, and `webapp-src/deploy/bundle_all.js`, then propose the screen layout before writing code.
