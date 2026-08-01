# Cockpit UI Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the MicroOcppSimulator web UI with the "OCPP Simulator Cockpit" design (dark HUD, connector bays, scenario verbs, wire trace), wired to the real simulator REST API, shipped through the existing single-file bundle pipeline.

**Architecture:** Preact app in the `webapp-src/` submodule. A `CockpitStore` (dependency-injected API client, framework-free, unit-tested with `node --test`) polls the simulator every 1s and exposes actions; Preact components render from store state. Webpack inlines fonts as data URLs; `deploy/bundle_all.js` inlines JS+CSS into one `bundle.html`, gzipped to `public/bundle.html.gz` and served by the C++ mongoose server at `/`.

**Tech Stack:** Preact 10 (already a dependency), webpack 5 (existing config), plain CSS with custom properties, `node:test` for unit tests. No new runtime dependencies.

## Global Constraints

- **NO C++ changes.** Nothing under `lib/`, and no edits to `src/*.cpp`, `src/*.h` in the main repo. The API already supports everything needed (partial `POST /connector/{id}/evse` bodies confirmed at `src/api.cpp:87-98`).
- All UI work happens in `/Users/joojodontoh/Documents/PersonalProjects/MicroOcppSimulator/webapp-src/` (its own git repo; `origin` = `Pitvolt/micro-ocpp-dashboard`). Commit there. Main-repo commits only in Task 9.
- Browser-facing API paths are prefixed `/api` (server strips it, `src/net_mongoose.cpp:122-148`). `API_ROOT=/api` in `.env.production`, `http://localhost:8000/api` in `.env.development`. Endpoints used: `GET /connectors`, `GET|POST /connector/{id}/evse`, `GET /connector/{id}/meter`, `GET|POST /connector/{id}/transaction`, `GET /connector/{id}/smartcharging`, `GET|POST /websocket` (full browser path `/api/websocket` — handled before the prefix-strip, same effective URL), `GET|POST /ca_cert` with body `{caCert}`.
- `pingInterval` / `reconnectInterval` are integers in **seconds**.
- `transactionId === -1` means no transaction. Presenting the same `idTag` again via `POST /connector/{id}/transaction` ends the session (server-side toggle).
- There is **no fault-injection endpoint** — the prototype's "INJECT FAULT" button is dropped. `Faulted` status is still rendered if the server reports it.
- Final `public/bundle.html.gz` must be ≤ 150 KB.
- Design reference files (extracted prototype, read-only, delete in Task 9): `webapp-src/design-reference/{ds-components.js, tokens.css, app-shell.jsx, bay.jsx, settings-drawer.jsx, sim-engine.js, michroma-latin.woff2, jetbrains-mono-latin.woff2, ibm-plex-sans-latin.woff2}`.
- **Canonical style source:** the prototype's tokens derive from the Alfred design system at `/Users/joojodontoh/Documents/PersonalProjects/project-alfred/apps/dashboard-web/styles/tokens/{colors,typography,spacing,effects}.css`. Colors and fonts must match it exactly (verified: `design-reference/tokens.css` values are identical). Font stack is non-negotiable: display `Michroma`, body `IBM Plex Sans`, data/readouts `JetBrains Mono` — all three ship inline.
- JSX pragma is `h` / `Fragment` (configured in `webpack_config/webpack.common.js` babel-loader options). Every component file needs `import { h } from "preact"` (plus `Fragment` if used).
- Unit tests run with `node --test tests/` (Node 22 available). Pure logic lives in `.mjs` files with **no JSX and no imports from Preact or DataService** so Node can run them natively.

## File Structure (end state of `webapp-src/src/`)

```
index.js                      entry — renders <App store=.../> into #app
DataService.js                KEEP UNCHANGED (fetch wrapper, API_ROOT prefix)
constants.js                  trimmed to API_ROOT / NODE_ENV exports only
css/tokens.css                design tokens + hud utility classes + @font-face (replaces kube/custom/layout/vars.css)
fonts/michroma-latin.woff2    display font (11.6 KB)
fonts/jetbrains-mono-latin.woff2  mono font (31 KB)
fonts/ibm-plex-sans-latin.woff2   body font (40 KB)
lib/status.mjs                OCPP status → label/tone mapping (pure)
lib/format.mjs                number/time formatting (pure)
store.mjs                     CockpitStore: state, polling, actions, wire trace (pure, DI)
components/ds.js              Button, CornerBracketCard, SectionLabel, TwoPartPrompt, Chip, ConnectionDot, MonoStat, StatusPill, Spark
components/TopStrip.js        sticky header strip
components/Trace.js           wire-trace event log
components/Bay.js             connector bay card (FlowScene, stats, verbs, advanced)
components/SettingsDrawer.js  backend link + CA cert drawer
components/App.js             cockpit assembly + poll timers
components/icons/             KEEP directory (SVGR imports; unused files add no bundle weight)
```

Deleted: `css/kube.css`, `css/custom.css`, `css/layout.css`, `css/vars.css`, `HtmlBuilder.js`, `StyleBuilder.js`, `DateFormatter.js`, `DataService_wasm.js.template` stays (used by wasm build script), all old `components/*.js` except the `icons/` directory, `components/component_styles/`.

---

### Task 1: Workspace reset + design tokens + build plumbing

**Files:**
- Create: `webapp-src/src/css/tokens.css`, `webapp-src/src/fonts/michroma-latin.woff2`, `webapp-src/src/fonts/jetbrains-mono-latin.woff2`, `webapp-src/src/fonts/ibm-plex-sans-latin.woff2`
- Modify: `webapp-src/webpack_config/webpack.common.js`, `webapp-src/public/index.html`, `webapp-src/src/index.js`, `webapp-src/src/constants.js`, `webapp-src/package.json`
- Delete: old css files, old components (see File Structure above)

**Interfaces:**
- Produces: `css/tokens.css` custom properties (`--bg-0`, `--gold`, `--font-mono`, `--sp-1..8`, `--fs-micro/caption/small/body/lead/hero/metric`, `--dur-fast`, `--ease-hud`, `--glow-gold/red/ice`, `--strip-h`, `--bracket`, `--ls-label`, `--fw-medium/semibold`) and classes `.hud-clip`, `.hud-display`, `.hud-decode`, `.hud-grid-bg`, `.flow-bar`, `.live-dot`, `.trace` — all later components rely on these names exactly.

- [ ] **Step 1: Create a work branch in webapp-src**

```bash
cd webapp-src && git checkout -b cockpit-redesign
```

- [ ] **Step 2: Delete old UI files**

```bash
cd webapp-src
git rm src/css/kube.css src/css/custom.css src/css/layout.css src/css/vars.css \
  src/HtmlBuilder.js src/StyleBuilder.js src/DateFormatter.js
git rm -r src/components/component_styles
cd src/components && git rm $(ls *.js) && cd ../..
```
(Keep `src/components/icons/`. Keep `DataService.js`, `DataService_wasm.js.template`.)

- [ ] **Step 3: Create tokens.css**

Copy the entire contents of `webapp-src/design-reference/tokens.css` (the four `:root` blocks and `.hud-*` classes extracted from the prototype) into `webapp-src/src/css/tokens.css`, then **prepend** these `@font-face` rules and **append** the app-level styles:

Prepend:
```css
@font-face {
  font-family: "Michroma";
  font-style: normal; font-weight: 400; font-display: swap;
  src: url("../fonts/michroma-latin.woff2") format("woff2");
}
@font-face {
  font-family: "JetBrains Mono";
  font-style: normal; font-weight: 400 700; font-display: swap;
  src: url("../fonts/jetbrains-mono-latin.woff2") format("woff2");
}
@font-face {
  font-family: "IBM Plex Sans";
  font-style: normal; font-weight: 400 600; font-display: swap;
  src: url("../fonts/ibm-plex-sans-latin.woff2") format("woff2");
}
```

Append:
```css
html, body { height: 100%; }
body { margin: 0; background: var(--bg-0); overflow-x: hidden; }
#app { min-height: 100vh; }
a { color: var(--ice); text-decoration: none; }
a:hover { color: var(--gold); }
@keyframes flow { 0% { opacity: .18 } 45% { opacity: 1 } 100% { opacity: .18 } }
.flow-bar { animation: flow 1.15s var(--ease-hud) infinite; }
@keyframes livepulse { 0%, 100% { opacity: .35 } 50% { opacity: 1 } }
.live-dot { animation: livepulse 1.6s ease-in-out infinite; }
@media (prefers-reduced-motion: reduce) { .flow-bar, .live-dot { animation: none } }
@media (max-width: 1000px) { .strip-min { display: none !important } }
.trace::-webkit-scrollbar { width: 8px; }
.trace::-webkit-scrollbar-thumb { background: var(--hairline-strong); }
button { font: inherit; }
```

Do **not** alter any token values — fonts and colors must stay exactly as in the reference: `--font-display: "Michroma", "Arial Black", sans-serif`, `--font-body: "IBM Plex Sans", system-ui, sans-serif`, `--font-mono: "JetBrains Mono", "SFMono-Regular", monospace`. Remove any `@font-face` blocks that came along in the copy (the reference file should include none — verify).

Fidelity check against the canonical Alfred tokens:
```bash
diff <(grep -o '\-\-[a-z0-9-]*:[^;]*;' src/css/tokens.css | sort -u) \
     <(cat "/Users/joojodontoh/Documents/PersonalProjects/project-alfred/apps/dashboard-web/styles/tokens/"{colors,typography,spacing,effects}.css | grep -o '\-\-[a-z0-9-]*:[^;]*;' | sort -u)
```
Expected: only additions on the Alfred side (extended categorical palette `--coral/--teal/--violet/--magenta/--lime/--steel/--rose`, `--fs-data`, `--hud-panel-h`) and the prototype-local `--bk` helper plus the three `@font-face` families on ours. **No value differences on any shared variable.**

- [ ] **Step 4: Add fonts**

```bash
cd webapp-src && mkdir -p src/fonts
cp design-reference/michroma-latin.woff2 src/fonts/
cp design-reference/jetbrains-mono-latin.woff2 src/fonts/
cp design-reference/ibm-plex-sans-latin.woff2 src/fonts/
```

- [ ] **Step 5: Inline fonts in webpack**

In `webapp-src/webpack_config/webpack.common.js`, add this rule **before** the existing `file-loader` rule (which matches woff but not woff2):

```js
{
    test: /\.woff2$/,
    type: "asset/inline",
},
```

- [ ] **Step 6: Update the HTML shell**

Replace the `<title>` and theme-color lines in `webapp-src/public/index.html`:

```html
<title>MicroOCPP Simulator — Cockpit</title>
<meta name="theme-color" content="#0B0E11">
```
(Remove both old `theme-color` lines; keep `<div id="app"></div>`, the `main.js` script tag, and both `<!--inject_...-->` comments exactly as they are — `bundle_all.js` depends on them.)

- [ ] **Step 7: Stub the entry point**

Replace `webapp-src/src/index.js` with:

```js
import { h, render } from "preact";
import "./css/tokens.css";

render(<div class="hud-display" style="color:var(--gold);padding:24px">COCKPIT BOOT</div>, document.getElementById("app"));
```

Replace `webapp-src/src/constants.js` with:

```js
const API_ROOT = process.env.API_ROOT;
const NODE_ENV = process.env.NODE_ENV;

export { API_ROOT, NODE_ENV };
```

- [ ] **Step 8: Add unit-test script**

In `webapp-src/package.json` scripts, replace `"test": "jest --watch"` with:

```json
"test": "node --test tests/"
```
(jest was never installed; no tests exist yet — `tests/` arrives in Task 2.)

- [ ] **Step 9: Verify production build compiles**

Run: `cd webapp-src && npm install && npm run build && npm run compress`
Expected: webpack exits 0, `dist/bundle.html.gz` produced. Open `dist/bundle.html` and confirm it contains `COCKPIT BOOT` and an inlined `data:font/woff2` URL.

- [ ] **Step 10: Commit**

```bash
cd webapp-src && git add -A && git commit -m "feat: reset UI shell — cockpit tokens, inline fonts, new entry"
```

---

### Task 2: Pure logic — status mapping and formatting

**Files:**
- Create: `webapp-src/src/lib/status.mjs`, `webapp-src/src/lib/format.mjs`
- Test: `webapp-src/tests/status.test.mjs`, `webapp-src/tests/format.test.mjs`

**Interfaces:**
- Produces: `statusLabel(status) -> [text, tone]`, `PILL_TONE` map, `isActiveStatus(status) -> bool`; `kw(watts) -> string`, `kwh(wattHours) -> string`, `amps(a) -> string`, `volts(v) -> string`, `pad2(n) -> string`, `stamp() -> "HH:MM:SS"`. Tones are one of `"text" | "green" | "amber" | "red" | "ice"`.

- [ ] **Step 1: Write failing tests**

`webapp-src/tests/status.test.mjs`:
```js
import { test } from "node:test";
import assert from "node:assert/strict";
import { statusLabel, PILL_TONE, isActiveStatus } from "../src/lib/status.mjs";

test("known OCPP statuses map to human labels and tones", () => {
  assert.deepEqual(statusLabel("Available"), ["AVAILABLE", "text"]);
  assert.deepEqual(statusLabel("Preparing"), ["CAR CONNECTED", "ice"]);
  assert.deepEqual(statusLabel("Charging"), ["CHARGING", "green"]);
  assert.deepEqual(statusLabel("SuspendedEV"), ["PAUSED BY CAR", "amber"]);
  assert.deepEqual(statusLabel("SuspendedEVSE"), ["PAUSED BY CHARGER", "amber"]);
  assert.deepEqual(statusLabel("Finishing"), ["SESSION ENDING", "ice"]);
  assert.deepEqual(statusLabel("Faulted"), ["FAULT", "red"]);
});

test("unknown status falls back to uppercase text tone", () => {
  assert.deepEqual(statusLabel("Reserved"), ["RESERVED", "text"]);
  assert.deepEqual(statusLabel(""), ["UNKNOWN", "text"]);
  assert.deepEqual(statusLabel(undefined), ["UNKNOWN", "text"]);
});

test("tone → StatusPill palette mapping is total", () => {
  for (const tone of ["text", "green", "amber", "red", "ice"]) {
    assert.ok(PILL_TONE[tone], `missing ${tone}`);
  }
});

test("isActiveStatus true only while energy can flow", () => {
  assert.equal(isActiveStatus("Charging"), true);
  assert.equal(isActiveStatus("Available"), false);
  assert.equal(isActiveStatus("Faulted"), false);
});
```

`webapp-src/tests/format.test.mjs`:
```js
import { test } from "node:test";
import assert from "node:assert/strict";
import { kw, kwh, amps, volts, pad2, stamp } from "../src/lib/format.mjs";

test("power formats W → kW with 2 decimals", () => {
  assert.equal(kw(7400), "7.40");
  assert.equal(kw(0), "0.00");
});

test("energy formats Wh → kWh with 3 decimals", () => {
  assert.equal(kwh(1234), "1.234");
});

test("current and voltage format with 1 decimal", () => {
  assert.equal(amps(16.04), "16.0");
  assert.equal(volts(229.96), "230.0");
});

test("pad2 zero-pads", () => {
  assert.equal(pad2(3), "03");
  assert.equal(pad2(12), "12");
});

test("stamp is HH:MM:SS", () => {
  assert.match(stamp(), /^\d{2}:\d{2}:\d{2}$/);
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd webapp-src && npm test`
Expected: FAIL — cannot find `../src/lib/status.mjs`.

- [ ] **Step 3: Implement**

`webapp-src/src/lib/status.mjs`:
```js
export const STATUS_LABEL = {
  Available: ["AVAILABLE", "text"],
  Preparing: ["CAR CONNECTED", "ice"],
  Charging: ["CHARGING", "green"],
  SuspendedEV: ["PAUSED BY CAR", "amber"],
  SuspendedEVSE: ["PAUSED BY CHARGER", "amber"],
  Finishing: ["SESSION ENDING", "ice"],
  Faulted: ["FAULT", "red"],
};

// tone key → StatusPill palette name (ds.js StatusPill TONES)
export const PILL_TONE = { text: "OFFLINE", green: "ONLINE", amber: "PENDING", red: "ERROR", ice: "APPROVED" };

export function statusLabel(status) {
  if (!status) return ["UNKNOWN", "text"];
  return STATUS_LABEL[status] || [String(status).toUpperCase(), "text"];
}

export function isActiveStatus(status) {
  return status === "Charging";
}
```

`webapp-src/src/lib/format.mjs`:
```js
export const kw = (w) => (w / 1000).toFixed(2);
export const kwh = (wh) => (wh / 1000).toFixed(3);
export const amps = (a) => Number(a).toFixed(1);
export const volts = (v) => Number(v).toFixed(1);
export const pad2 = (n) => String(n).padStart(2, "0");

export function stamp() {
  const d = new Date();
  return [d.getHours(), d.getMinutes(), d.getSeconds()].map(pad2).join(":");
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd webapp-src && npm test`
Expected: all tests PASS.

- [ ] **Step 5: Commit**

```bash
cd webapp-src && git add src/lib tests && git commit -m "feat: status/format pure modules with node:test coverage"
```

---

### Task 3: CockpitStore — polling, actions, wire trace

**Files:**
- Create: `webapp-src/src/store.mjs`
- Test: `webapp-src/tests/store.test.mjs`

**Interfaces:**
- Consumes: `stamp` from `lib/format.mjs`. API client shape `{ get(path) -> Promise<json>, post(path, body) -> Promise<json> }` (matches `DataService.js`).
- Produces: `class CockpitStore { constructor(api); state; subscribe(fn); init(); pollFast(); pollSlow(); plugIn(id); unplug(id); tapCard(id, idTag); endSession(id); toggle(id, key); saveBackend(form); loadCert(); saveCert(pem); }`
  - `state = { connectors: [...], backend: {backendUrl, chargeBoxId, authorizationKey, pingInterval, reconnectInterval}, link: "init"|"ok"|"down", log: [{t, cid, line, tone}] }`
  - Connector shape: `{ id: string, evPlugged, evsePlugged, evReady, evseReady: bool, chargePointStatus: string, transactionId: number, idTag: string, energy, power, current, voltage: number, maxPower, maxCurrent: number, history: number[60] }`

- [ ] **Step 1: Write failing tests**

`webapp-src/tests/store.test.mjs`:
```js
import { test } from "node:test";
import assert from "node:assert/strict";
import { CockpitStore } from "../src/store.mjs";

function fakeApi(routes = {}) {
  const calls = [];
  return {
    calls,
    get(path) {
      calls.push(["GET", path]);
      if (path in routes) return Promise.resolve(routes[path]);
      return Promise.reject(new Error("no route " + path));
    },
    post(path, body) {
      calls.push(["POST", path, body]);
      if (path in routes) return Promise.resolve({ ...routes[path], ...body });
      return Promise.reject(new Error("no route " + path));
    },
  };
}

const baseRoutes = () => ({
  "/connectors": ["1", "2"],
  "/websocket": { backendUrl: "ws://x", chargeBoxId: "CB01", authorizationKey: "", pingInterval: 5, reconnectInterval: 30 },
  "/connector/1/evse": { evPlugged: false, evsePlugged: true, evReady: false, evseReady: false, chargePointStatus: "Available" },
  "/connector/2/evse": { evPlugged: false, evsePlugged: true, evReady: false, evseReady: false, chargePointStatus: "Available" },
  "/connector/1/meter": { energy: 100, power: 7400, current: 10.7, voltage: 230.1 },
  "/connector/2/meter": { energy: 0, power: 0, current: 0, voltage: 229.8 },
  "/connector/1/transaction": { idTag: "", transactionId: -1 },
  "/connector/2/transaction": { idTag: "", transactionId: -1 },
  "/connector/1/smartcharging": { maxPower: 11000, maxCurrent: 16 },
  "/connector/2/smartcharging": { maxPower: 11000, maxCurrent: 16 },
});

test("init seeds connectors from /connectors and loads backend config", async () => {
  const store = new CockpitStore(fakeApi(baseRoutes()));
  await store.init();
  assert.equal(store.state.connectors.length, 2);
  assert.equal(store.state.connectors[0].id, "1");
  assert.equal(store.state.backend.chargeBoxId, "CB01");
});

test("pollFast merges evse/meter/transaction, pushes power history, sets link ok", async () => {
  const store = new CockpitStore(fakeApi(baseRoutes()));
  await store.init();
  await store.pollFast();
  const c = store.state.connectors[0];
  assert.equal(c.power, 7400);
  assert.equal(c.chargePointStatus, "Available");
  assert.equal(c.history.length, 60);
  assert.equal(c.history[59], 7400);
  assert.equal(store.state.link, "ok");
});

test("pollFast failure flips link to down and logs once; recovery logs restore", async () => {
  const routes = baseRoutes();
  const api = fakeApi(routes);
  const store = new CockpitStore(api);
  await store.init();
  await store.pollFast();
  const good = routes["/connector/1/evse"];
  delete routes["/connector/1/evse"];
  await store.pollFast();
  assert.equal(store.state.link, "down");
  const downLogs = store.state.log.filter((l) => l.line.includes("POLL FAILED"));
  assert.equal(downLogs.length, 1);
  routes["/connector/1/evse"] = good;
  await store.pollFast();
  assert.equal(store.state.link, "ok");
  assert.ok(store.state.log.some((l) => l.line.includes("LINK RESTORED")));
});

test("plugIn posts partial evse body and merges the response", async () => {
  const api = fakeApi(baseRoutes());
  const store = new CockpitStore(api);
  await store.init();
  await store.plugIn("1");
  const post = api.calls.find(([m, p]) => m === "POST" && p === "/connector/1/evse");
  assert.deepEqual(post[2], { evPlugged: true, evReady: true });
  assert.equal(store.state.connectors[0].evPlugged, true);
});

test("unplug posts all-off body", async () => {
  const api = fakeApi(baseRoutes());
  const store = new CockpitStore(api);
  await store.init();
  await store.unplug("1");
  const post = api.calls.find(([m, p]) => m === "POST" && p === "/connector/1/evse");
  assert.deepEqual(post[2], { evPlugged: false, evReady: false, evseReady: false });
});

test("tapCard posts idTag; endSession re-presents the current session tag", async () => {
  const routes = baseRoutes();
  const api = fakeApi(routes);
  const store = new CockpitStore(api);
  await store.init();
  await store.tapCard("1", "D7A4-0001");
  let post = api.calls.filter(([m, p]) => m === "POST" && p === "/connector/1/transaction");
  assert.deepEqual(post[0][2], { idTag: "D7A4-0001" });
  store.state.connectors[0].idTag = "D7A4-0001";
  store.state.connectors[0].transactionId = 1181;
  await store.endSession("1");
  post = api.calls.filter(([m, p]) => m === "POST" && p === "/connector/1/transaction");
  assert.deepEqual(post[1][2], { idTag: "D7A4-0001" });
});

test("toggle flips a single evse boolean via partial POST", async () => {
  const api = fakeApi(baseRoutes());
  const store = new CockpitStore(api);
  await store.init();
  await store.toggle("1", "evseReady");
  const post = api.calls.find(([m, p]) => m === "POST" && p === "/connector/1/evse");
  assert.deepEqual(post[2], { evseReady: true });
});

test("saveBackend coerces intervals to ints and posts /websocket", async () => {
  const api = fakeApi(baseRoutes());
  const store = new CockpitStore(api);
  await store.init();
  await store.saveBackend({ backendUrl: "ws://y", chargeBoxId: "CB02", authorizationKey: "k", pingInterval: "7", reconnectInterval: "20" });
  const post = api.calls.find(([m, p]) => m === "POST" && p === "/websocket");
  assert.equal(post[2].pingInterval, 7);
  assert.equal(post[2].reconnectInterval, 20);
  assert.equal(store.state.backend.chargeBoxId, "CB02");
});

test("actions append wire-trace entries, capped at 60", async () => {
  const api = fakeApi(baseRoutes());
  const store = new CockpitStore(api);
  await store.init();
  for (let i = 0; i < 70; i++) store.trace("1", "line " + i, "text");
  assert.equal(store.state.log.length, 60);
  await store.plugIn("1");
  assert.ok(store.state.log[0].line.includes("POST /connector/1/evse"));
});
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd webapp-src && npm test`
Expected: FAIL — cannot find `../src/store.mjs`.

- [ ] **Step 3: Implement store.mjs**

`webapp-src/src/store.mjs`:
```js
import { stamp } from "./lib/format.mjs";

const HISTORY_LEN = 60;
const LOG_CAP = 60;

export function makeConnector(id) {
  return {
    id: String(id),
    evPlugged: false, evsePlugged: true, evReady: false, evseReady: false,
    chargePointStatus: "",
    transactionId: -1, idTag: "",
    energy: 0, power: 0, current: 0, voltage: 0,
    maxPower: 11000, maxCurrent: 16,
    history: new Array(HISTORY_LEN).fill(0),
  };
}

export class CockpitStore {
  constructor(api) {
    this.api = api;
    this.state = {
      connectors: [],
      backend: { backendUrl: "", chargeBoxId: "", authorizationKey: "", pingInterval: 0, reconnectInterval: 30 },
      link: "init",
      log: [],
    };
    this.listeners = new Set();
    this.polling = false;
  }

  subscribe(fn) { this.listeners.add(fn); return () => this.listeners.delete(fn); }
  emit() { for (const fn of this.listeners) fn(); }

  trace(cid, line, tone = "text") {
    this.state.log.unshift({ t: stamp(), cid, line, tone });
    if (this.state.log.length > LOG_CAP) this.state.log.length = LOG_CAP;
    this.emit();
  }

  connector(id) { return this.state.connectors.find((c) => c.id === String(id)); }

  markLink(ok) {
    const prev = this.state.link;
    const next = ok ? "ok" : "down";
    if (prev === next) return;
    this.state.link = next;
    if (next === "down") this.trace(0, "POLL FAILED · SIMULATOR UNREACHABLE", "red");
    else if (prev === "down") this.trace(0, "LINK RESTORED · POLLING RESUMED", "green");
    this.emit();
  }

  async init() {
    const ids = await this.api.get("/connectors");
    this.state.connectors = ids.map(makeConnector);
    try { await this.loadBackend(); } catch (e) { /* backend config panel shows blanks */ }
    this.emit();
  }

  async loadBackend() {
    const cfg = await this.api.get("/websocket");
    this.state.backend = { ...this.state.backend, ...cfg };
    this.emit();
  }

  async pollFast() {
    if (this.polling || this.state.connectors.length === 0) return;
    this.polling = true;
    try {
      await Promise.all(this.state.connectors.map(async (c) => {
        const [evse, meter, txn] = await Promise.all([
          this.api.get(`/connector/${c.id}/evse`),
          this.api.get(`/connector/${c.id}/meter`),
          this.api.get(`/connector/${c.id}/transaction`),
        ]);
        Object.assign(c, evse, meter, {
          idTag: txn.idTag || "",
          transactionId: typeof txn.transactionId === "number" ? txn.transactionId : -1,
        });
        c.history.push(c.power || 0);
        if (c.history.length > HISTORY_LEN) c.history.shift();
      }));
      this.markLink(true);
    } catch (e) {
      this.markLink(false);
    } finally {
      this.polling = false;
      this.emit();
    }
  }

  async pollSlow() {
    try {
      await Promise.all(this.state.connectors.map(async (c) => {
        const sc = await this.api.get(`/connector/${c.id}/smartcharging`);
        c.maxPower = sc.maxPower ?? c.maxPower;
        c.maxCurrent = sc.maxCurrent ?? c.maxCurrent;
      }));
      this.emit();
    } catch (e) { /* smartcharging is cosmetic; fast poll owns link state */ }
  }

  async postEvse(id, body) {
    const c = this.connector(id);
    try {
      const resp = await this.api.post(`/connector/${id}/evse`, body);
      Object.assign(c, resp);
      this.trace(id, `POST /connector/${id}/evse ${JSON.stringify(body)} → 200`, "ice");
    } catch (e) {
      this.trace(id, `POST /connector/${id}/evse FAILED`, "red");
    }
    this.emit();
  }

  plugIn(id) { return this.postEvse(id, { evPlugged: true, evReady: true }); }
  unplug(id) { return this.postEvse(id, { evPlugged: false, evReady: false, evseReady: false }); }
  toggle(id, key) {
    const c = this.connector(id);
    return this.postEvse(id, { [key]: !c[key] });
  }

  async tapCard(id, idTag) {
    const c = this.connector(id);
    try {
      const resp = await this.api.post(`/connector/${id}/transaction`, { idTag });
      c.idTag = resp.idTag ?? c.idTag;
      if (typeof resp.transactionId === "number") c.transactionId = resp.transactionId;
      this.trace(id, `POST /connector/${id}/transaction {idTag:"${idTag}"} → 200`, "green");
    } catch (e) {
      this.trace(id, `POST /connector/${id}/transaction FAILED`, "red");
    }
    this.emit();
  }

  endSession(id) {
    const c = this.connector(id);
    return this.tapCard(id, c.idTag);
  }

  async saveBackend(form) {
    const body = {
      backendUrl: form.backendUrl,
      chargeBoxId: form.chargeBoxId,
      authorizationKey: form.authorizationKey,
      pingInterval: parseInt(form.pingInterval, 10) || this.state.backend.pingInterval,
      reconnectInterval: parseInt(form.reconnectInterval, 10) || this.state.backend.reconnectInterval,
    };
    const resp = await this.api.post("/websocket", body);
    this.state.backend = { ...this.state.backend, ...resp };
    this.trace(0, `POST /api/websocket {chargeBoxId:"${body.chargeBoxId}"} → 200 · RECONNECTING`, "gold");
    this.emit();
  }

  async loadCert() {
    const resp = await this.api.get("/ca_cert");
    return resp.caCert || "";
  }

  async saveCert(pem) {
    await this.api.post("/ca_cert", { caCert: pem });
    this.trace(0, "POST /api/ca_cert → 200 · CERTIFICATE UPDATED", "gold");
    this.emit();
  }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd webapp-src && npm test`
Expected: all tests PASS (status, format, store).

- [ ] **Step 5: Commit**

```bash
cd webapp-src && git add src/store.mjs tests/store.test.mjs && git commit -m "feat: CockpitStore polling engine and actions with DI tests"
```

---

### Task 4: Design-system primitives (`ds.js`)

**Files:**
- Create: `webapp-src/src/components/ds.js`

**Interfaces:**
- Consumes: CSS variables/classes from `tokens.css`.
- Produces (named exports, Preact): `Button({variant, size, clip, disabled, onClick, style, children})`, `CornerBracketCard({selected, tone, clip, padding, style, children})`, `SectionLabel({label, action, color, style})`, `TwoPartPrompt({label, action, tone, filled, onClick, disabled, style})`, `Chip({label, tone, style})`, `ConnectionDot({label, status, style})`, `MonoStat({value, label, unit, tone, size, align, style})`, `StatusPill({status, tone, dot, style})`, `Spark({data, max})`, plus shared style objects `LABEL` and `MONO`.

- [ ] **Step 1: Port the primitives**

Port each component in `webapp-src/design-reference/ds-components.js` from `React.createElement` calls to Preact JSX (`import { h, Fragment } from "preact"; import { useState } from "preact/hooks";`). Rules for the port:
- Keep every style object, tone table, and prop default **byte-identical** to the reference — this is a faithful port, not a redesign.
- `className` → keep as `className` (Preact accepts it).
- Drop the `__ds_scope` / try-catch wrappers; use `export function`.
- Add `Spark` and the shared label styles (used by Bay/Trace/TopStrip/SettingsDrawer), source `design-reference/bay.jsx` lines 5-19:

```js
export const LABEL = { fontFamily: "var(--font-mono)", fontSize: "var(--fs-micro)", textTransform: "uppercase", letterSpacing: "var(--ls-label)", color: "var(--text-2)" };
export const MONO = { fontFamily: "var(--font-mono)", fontVariantNumeric: "tabular-nums" };

export function Spark({ data, max }) {
  const w = 100, h2 = 30;
  const peak = Math.max(max * 0.25, ...data) || 1;
  const pts = data.map((v, i) => `${(i / (data.length - 1)) * w},${h2 - (v / peak) * h2}`).join(" ");
  return (
    <svg viewBox={`0 0 ${w} ${h2}`} preserveAspectRatio="none" style={{ width: "100%", height: 44, display: "block" }}>
      <polyline points={`0,${h2} ${pts} ${w},${h2}`} fill="var(--gold-dim)" stroke="none" />
      <polyline points={pts} fill="none" stroke="var(--gold)" strokeWidth="1" vectorEffect="non-scaling-stroke" />
    </svg>
  );
}
```

- [ ] **Step 2: Smoke-render in the entry stub**

Temporarily extend `src/index.js` to render one of each primitive (a `CornerBracketCard` containing `StatusPill status="CHARGING" tone="ONLINE"`, a `TwoPartPrompt label="BAY 1" action="PLUG IN CAR"`, a `MonoStat value="7.40" unit="KW" label="POWER"`, a `Spark data={[0,2,5,3,8]} max={10}`). Run `npm run dev` (dev server opens on a local port) and visually confirm: dark page, gold clipped buttons, bracket corners on `selected`, mono uppercase labels, Michroma renders on `.hud-display`.

- [ ] **Step 3: Verify production build still compiles**

Run: `cd webapp-src && npm run build`
Expected: exit 0.

- [ ] **Step 4: Commit** (leave the smoke-render in place; Task 7 replaces `index.js` anyway)

```bash
cd webapp-src && git add src/components/ds.js src/index.js && git commit -m "feat: port HUD design-system primitives to Preact"
```

---

### Task 5: TopStrip, Trace, Bay

**Files:**
- Create: `webapp-src/src/components/TopStrip.js`, `webapp-src/src/components/Trace.js`, `webapp-src/src/components/Bay.js`

**Interfaces:**
- Consumes: `ds.js` exports; `statusLabel`, `PILL_TONE`, `isActiveStatus` from `../lib/status.mjs`; `kw`, `kwh`, `amps`, `volts`, `pad2`, `stamp` from `../lib/format.mjs`; store instance via props.
- Produces: `TopStrip({ store, onSettings })`, `Trace({ log })`, `Bay({ store, c })` — where `c` is a connector object from `store.state.connectors`.

- [ ] **Step 1: Port TopStrip and Trace**

Port from `design-reference/app-shell.jsx` (functions `TopStrip` and `Trace`) with these changes:
- `S.state.backend` → `store.state.backend`; `down` → `store.state.link === "down"`; `S.stamp()` → `stamp()`.
- The `ri-settings-3-line` icon → import the existing SVGR icon: `import IControls from "./icons/IControls.svg";` rendered at 13px.
- The METER ConnectionDot status: `store.state.link === "ok" ? "ok" : "error"`.
- Brand text: `MICROOCPP SIM` + gold dot; subtitle `OCPP 1.6J CHARGE POINT`.
- Everything else (layout, styles, LIVE · 1S pulse dot, strip-min responsive class) byte-identical.

- [ ] **Step 2: Port Bay**

Port from `design-reference/bay.jsx` with these changes:
1. Props are `{ store, c }`; every `act(() => S.xxx(c))` becomes a direct `store.xxx(c.id)` call (the store emits — no `bump` prop).
2. `const status = c.chargePointStatus;` (server truth, not client derivation) and `const [text, tone] = statusLabel(status); const charging = isActiveStatus(status);`
3. `FlowScene` icons: `import IEvseIcon from "./icons/IEvseIcon.svg";` for the charger, `import IEv from "./icons/IEv.svg";` for the car (render ~34px via width/height props or wrapping span with `display:flex`). Keep the 16 flow bars and wire-color logic identical.
4. Stats row: `<MonoStat value={kw(c.power)} unit="KW" label="POWER" tone={charging ? "gold" : "text"} size="sm" />`, `value={kwh(c.energy)} unit="KWH" label="ENERGY"`, `value={amps(c.current)} unit="A" label="CURRENT"`, `value={volts(c.voltage)} unit="V" label="VOLTAGE"`.
5. Verb logic identical: `!c.evPlugged` → `TwoPartPrompt label={"BAY " + c.id} action="PLUG IN CAR"` calling `store.plugIn(c.id)`; else if `c.transactionId < 0` → `action="TAP RFID CARD"` calling `store.tapCard(c.id, tag)`; else → `label={"TXN " + c.transactionId} action="STOP CHARGING" tone="amber"` calling `store.endSession(c.id)`. `UNPLUG` outline button when plugged.
6. **Remove** the `INJECT FAULT` button (no endpoint). Keep `ADVANCED` toggle.
7. Advanced panel: four `Toggle`s — `EVPLUGGED`, `EVSEPLUGGED`, `EVREADY`, `EVSEREADY` — each calling `store.toggle(c.id, key)`; the idTag input (local `useState`, default `"D7A4-1180"`) + `AUTHORISE` button calling `store.tapCard(c.id, tag)`; readout line shows `CHARGEPOINTSTATUS {status || "—"}`, `TRANSACTIONID {c.transactionId}`, `MAXCURRENT {c.maxCurrent} A`.
8. Header right side: `TYPE 2 · {kw(c.maxPower)} KW LIMIT` (was `.toFixed(1)` of kW — use `(c.maxPower/1000).toFixed(1)` to stay identical to the prototype).
9. `PEAK {kw(Math.max(...c.history))} KW` above `<Spark data={c.history} max={c.maxPower} />`.

- [ ] **Step 3: Compile check**

Run: `cd webapp-src && npm run build`
Expected: exit 0. (Visual verification happens in Task 7 with the full app.)

- [ ] **Step 4: Commit**

```bash
cd webapp-src && git add src/components/TopStrip.js src/components/Trace.js src/components/Bay.js && git commit -m "feat: port TopStrip, Trace, Bay to Preact wired to CockpitStore"
```

---

### Task 6: SettingsDrawer

**Files:**
- Create: `webapp-src/src/components/SettingsDrawer.js`

**Interfaces:**
- Consumes: `SectionLabel`, `Button`, `Chip`, `ConnectionDot`, `LABEL`, `MONO` from `ds.js`; `stamp` from `../lib/format.mjs`; store via props.
- Produces: `SettingsDrawer({ store, open, onClose })`.

- [ ] **Step 1: Port from `design-reference/settings-drawer.jsx`**

Changes from the reference:
1. On `open` becoming true: `setForm({ ...store.state.backend })`, `setSaved(false)`, and `store.loadCert().then(setCert).catch(() => {})` (`useEffect` on `[open]`).
2. `save()` becomes async: `await store.saveBackend(form)`; then `if (certDirty) await store.saveCert(cert);` then `setSaved(true)`. Track `certDirty` with a boolean set in the cert field's onChange. Wrap in try/catch — on failure render `SAVE FAILED` in `var(--red)` instead of the `WRITTEN · {stamp()}` confirmation.
3. **Remove** the DIAGNOSTICS section entirely (`SIMULATE LINK LOSS` was mock-only; link state now comes from real poll failures).
4. `ConnectionDot` in the BACKEND LINK header: `status={store.state.link === "ok" ? "ok" : "error"}`.
5. Field labels/hints identical: `BACKEND SERVER`, `STATION ID`, `AUTH KEY`, `PING (S)`, `RECONNECT (S)`, `CA CERTIFICATE` with hint `PEM, POSTED TO THE SIMULATOR ON SAVE`, chip `REAL` → change chip to `LIVE` tone green.
6. Footer button: `SAVE & RECONNECT` (`Button clip`).

- [ ] **Step 2: Compile check**

Run: `cd webapp-src && npm run build`
Expected: exit 0.

- [ ] **Step 3: Commit**

```bash
cd webapp-src && git add src/components/SettingsDrawer.js && git commit -m "feat: port settings drawer with live websocket + ca_cert round-trip"
```

---

### Task 7: App assembly + end-to-end browser verification

**Files:**
- Create: `webapp-src/src/components/App.js`
- Modify: `webapp-src/src/index.js`

**Interfaces:**
- Consumes: everything above; `DataService` from `../DataService`.
- Produces: `App({ store })` — the complete cockpit.

- [ ] **Step 1: Write App.js**

Port the `Cockpit` function from `design-reference/app-shell.jsx`:

```js
import { h } from "preact";
import { useEffect, useReducer, useState } from "preact/hooks";
import TopStrip from "./TopStrip";
import Trace from "./Trace";
import Bay from "./Bay";
import SettingsDrawer from "./SettingsDrawer";
import { MonoStat, Chip, LABEL, MONO } from "./ds";
import { isActiveStatus } from "../lib/status.mjs";
import { kw, pad2 } from "../lib/format.mjs";
import IForbidden from "./icons/IForbidden.svg";

export default function App({ store }) {
  const [, bump] = useReducer((n) => n + 1, 0);
  const [open, setOpen] = useState(false);

  useEffect(() => {
    const unsub = store.subscribe(bump);
    store.init().then(() => store.pollFast()).catch(() => store.markLink(false));
    const fast = setInterval(() => store.pollFast(), 1000);
    const slow = setInterval(() => store.pollSlow(), 5000);
    return () => { unsub(); clearInterval(fast); clearInterval(slow); };
  }, []);

  const cs = store.state.connectors;
  const down = store.state.link === "down";
  const active = cs.filter((c) => isActiveStatus(c.chargePointStatus)).length;
  const total = cs.reduce((a, c) => a + (c.power || 0), 0);
  const energy = cs.reduce((a, c) => a + (c.energy || 0), 0);

  return (
    <div className="hud-grid-bg" style={{ minHeight: "100vh", background: "var(--bg-0)" }}>
      <TopStrip store={store} onSettings={() => setOpen(true)} />
      {down && (
        <div style={{ display: "flex", alignItems: "center", gap: 10, padding: "8px var(--sp-4)", background: "var(--red-dim)", borderBottom: "1px solid var(--red)" }}>
          <IForbidden style={{ width: 14, height: 14 }} />
          <span style={{ ...MONO, fontSize: 11, color: "var(--red)", letterSpacing: "0.06em" }}>
            SIMULATOR UNREACHABLE · SHOWING LAST KNOWN STATE · RETRYING EVERY 1S
          </span>
        </div>
      )}
      <main style={{ display: "flex", flexDirection: "column", gap: "var(--sp-5)", padding: "var(--sp-5)", maxWidth: 1320, margin: "0 auto" }}>
        <div style={{ display: "flex", alignItems: "flex-end", justifyContent: "space-between", gap: "var(--sp-4)", flexWrap: "wrap" }}>
          <div style={{ display: "flex", alignItems: "baseline", gap: 14 }}>
            <h1 className="hud-display hud-decode" style={{ fontSize: 21, color: "var(--text-1)", margin: 0 }}>CHARGE BAYS</h1>
            <span style={LABEL}>{pad2(cs.length)} SIMULATED CONNECTORS</span>
            {active > 0 && <Chip label="LIVE SESSION" tone="green" />}
          </div>
          <div style={{ display: "flex", gap: "var(--sp-6)" }}>
            <MonoStat value={kw(total)} unit="KW" label="SITE DRAW" tone={active ? "gold" : "text"} size="sm" />
            <MonoStat value={(energy / 1000).toFixed(2)} unit="KWH" label="DELIVERED" tone="ice" size="sm" />
            <MonoStat value={pad2(active)} label="CHARGING" tone={active ? "green" : "text"} size="sm" />
          </div>
        </div>
        <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fit,minmax(min(440px,100%),1fr))", gap: "var(--sp-4)", alignItems: "start" }}>
          {cs.map((c) => <Bay key={c.id} store={store} c={c} />)}
        </div>
        <Trace log={store.state.log} />
      </main>
      <SettingsDrawer store={store} open={open} onClose={() => setOpen(false)} />
    </div>
  );
}
```

- [ ] **Step 2: Rewrite index.js**

```js
import { h, render } from "preact";
import "./css/tokens.css";
import App from "./components/App";
import DataService from "./DataService";
import { CockpitStore } from "./store.mjs";

render(<App store={new CockpitStore(DataService)} />, document.getElementById("app"));
```

- [ ] **Step 3: Build and run the real simulator**

```bash
cd /Users/joojodontoh/Documents/PersonalProjects/MicroOcppSimulator
cmake -S . -B build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j 8 --target mo_simulator
./build/mo_simulator &
```
Verify API up: `curl -s http://localhost:8000/api/connectors` → `["1","2"]`.

- [ ] **Step 4: Golden-path browser verification (dev server)**

Run `cd webapp-src && npm run dev` (proxies to `http://localhost:8000/api` via `.env.development`). In the browser verify, in order:
1. Two bays render, status pills say AVAILABLE, TopStrip shows backend URL + charge box ID fetched from `/api/websocket`, LIVE · 1S pulses.
2. Click **PLUG IN CAR** on bay 1 → pill → CAR CONNECTED (Preparing), wire trace logs the POST.
3. Click **TAP RFID CARD** → within a few seconds pill → CHARGING, power climbs to ~11 kW, sparkline draws, SITE DRAW updates. (OCPP transaction needs the backend link; with the default echo server the simulator still starts transactions locally.)
4. Click **STOP CHARGING** → power ramps to 0, transaction id disappears.
5. Click **UNPLUG** → AVAILABLE.
6. ADVANCED: toggle EVREADY off during a charge → PAUSED BY CAR.
7. SETUP drawer: values round-trip (change STATION ID, save, reopen → persisted; `curl -s http://localhost:8000/api/websocket` confirms).
8. Kill the simulator (`kill %1`) → red SIMULATOR UNREACHABLE banner within ~2s, NO POLL in strip; restart it → banner clears, LINK RESTORED in trace.
9. Narrow the window to ~380px — bays stack, strip-min elements hide.

Fix anything broken before proceeding. This step is the acceptance gate for the whole port.

- [ ] **Step 5: Run unit tests**

Run: `cd webapp-src && npm test`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
cd webapp-src && git add -A && git commit -m "feat: assemble cockpit app — live polling, scenario verbs, settings drawer"
```

---

### Task 8: Production bundle + embedded serving verification

**Files:**
- Modify: `webapp-src/dist/*` (generated), `public/bundle.html.gz` (main repo, generated)

- [ ] **Step 1: Build the production bundle**

```bash
cd /Users/joojodontoh/Documents/PersonalProjects/MicroOcppSimulator
./build-webapp/build_webapp.sh
```
Expected: "Up-to-date version of the web-app bundle was placed in the /public folder!"

- [ ] **Step 2: Size gate**

Run: `ls -la public/bundle.html.gz`
Expected: ≤ 150 KB. The three woff2 files total ~83 KB raw (~110 KB as base64, partially recovered by the final gzip), so expect ~120-140 KB. If over: check `webpack-bundle-analyzer` (uncomment in `webpack.common.js`). Do NOT drop fonts to make budget — they are part of the design contract; raise the concern instead.

- [ ] **Step 3: Serve through the C++ binary**

```bash
./build/mo_simulator &
curl -sI http://localhost:8000/ | grep -i 'content-encoding'
```
Expected: `Content-Encoding: gzip`. Open `http://localhost:8000/` in the browser and repeat golden-path items 1-5 from Task 7 Step 4 against the **bundled** app (this catches prod-only issues: minification, `API_ROOT=/api` same-origin paths, inlined fonts).

- [ ] **Step 4: Commit the bundle source state in webapp-src**

```bash
cd webapp-src && git add -A && git commit -m "chore: production bundle build"
```
(If `dist/` is gitignored in webapp-src, this commit may be empty — skip it then.)

---

### Task 9: Main-repo integration

**Files:**
- Modify: `.gitmodules` (main repo), submodule pointer, `public/bundle.html.gz`
- Delete: `webapp-src/design-reference/`

- [ ] **Step 1: Remove the design reference from webapp-src**

```bash
cd webapp-src && git rm -r design-reference && git commit -m "chore: drop design reference used during port"
```

- [ ] **Step 2: Point .gitmodules at the Pitvolt fork**

In `/Users/joojodontoh/Documents/PersonalProjects/MicroOcppSimulator/.gitmodules`, change the `webapp-src` entry URL from `https://github.com/agruenb/arduino-ocpp-dashboard.git` to `https://github.com/Pitvolt/micro-ocpp-dashboard.git`, then `git submodule sync webapp-src`.

- [ ] **Step 3: Verify tests and builds one final time**

```bash
cd webapp-src && npm test && npm run build && cd ..
ls -la public/bundle.html.gz
```
Expected: tests pass, build clean, bundle present and ≤ 150 KB.

- [ ] **Step 4: Commit in the main repo**

```bash
git add .gitmodules webapp-src public/bundle.html.gz
git commit -m "feat: cockpit UI — redesigned simulator dashboard (webapp-src cockpit-redesign)"
```
**Do not push** either repo — pushing to Pitvolt is the user's call (the submodule commit must be pushed to `Pitvolt/micro-ocpp-dashboard` *before* the main-repo commit is pushed, otherwise clones break; remind the user of this ordering).

---

## Self-Review Notes

- Spec coverage: connector-first bays (T5), scenario verbs (T5), live telemetry + sparkline (T3/T5), persistent connection strip (T5 TopStrip), settings drawer with websocket + CA cert (T6), auto-refresh everywhere with single failure banner (T3/T7), dark HUD look (T1/T4), single-bundle pipeline preserved (T1/T8), no C++ changes (global), fake demo pages dropped (old components deleted in T1), prototype bugs not carried over (store is single source of truth; no stale-state comparison; no missing imports — verified by build + tests).
- Dropped from prototype deliberately: INJECT FAULT (no endpoint), SIMULATE LINK LOSS (mock-only). All three Alfred fonts (Michroma, IBM Plex Sans, JetBrains Mono) ship inline; colors verified identical to `project-alfred/apps/dashboard-web/styles/tokens/colors.css`.
- Type consistency check: `store.state.link` values `"init" | "ok" | "down"` used in T3 tests, T5 TopStrip, T6 drawer, T7 banner. Connector field names match `src/api.cpp` JSON exactly. `PILL_TONE` keys match `statusLabel` tones and `StatusPill` TONES palette names.
