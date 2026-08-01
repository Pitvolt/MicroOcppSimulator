#!/bin/sh
# Runtime OCPP config for the staging bundle. Overwrites mo_store/ws-conn.jsn
# from env vars so ONE image can target any charger / any environment:
#
#   docker run -p 8000:8000 \
#     -e STATION_ID=PVE2-VAJN \
#     -e WSS_URL=wss://staging.ocpp.pitvolt.com \
#     -e AUTH_KEY=<optional basic-auth password> \
#     pitvolt/micro-ocpp-sim:staging
#
# The simulator reads this file exactly once at startup; changing env at
# runtime = restart container. The UI at :8000 lets you drive sessions.

set -eu

: "${STATION_ID:?STATION_ID env required (OCPP charge box id, e.g. PVE2-VAJN)}"
: "${WSS_URL:?WSS_URL env required (e.g. wss://staging.ocpp.pitvolt.com)}"
AUTH_KEY="${AUTH_KEY:-}"

cat > /MicroOcppSimulator/mo_store/ws-conn.jsn <<EOF
{"head":{"content-type":"ocpp_config_file","version":"2.0"},"configurations":[
{"type":"string","key":"Cst_BackendUrl","value":"${WSS_URL}"},
{"type":"string","key":"Cst_ChargeBoxId","value":"${STATION_ID}"},
{"type":"string","key":"AuthorizationKey","value":"${AUTH_KEY}"},
{"type":"int","key":"WebSocketPingInterval","value":5},
{"type":"int","key":"Cst_ReconnectInterval","value":10},
{"type":"int","key":"Cst_StaleTimeout","value":300}]}
EOF

echo "sim: STATION_ID=${STATION_ID} WSS_URL=${WSS_URL} (AUTH_KEY=${AUTH_KEY:+<set>})"
exec /MicroOcppSimulator/build/mo_simulator
