# Staging-bundle image (deploy/staging/): rebuilds the simulator with the
# runtime entrypoint layered on top, so the browser UI at :8000 always
# targets whichever station+endpoint STATION_ID and WSS_URL name. Same
# build recipe as the root Dockerfile -- kept separate so the upstream
# image stays clean.

FROM alpine:latest

RUN apk update && \
    apk add --no-cache \
    git \
    cmake \
    openssl-dev \
    build-base

WORKDIR /MicroOcppSimulator

# Bring in the whole repo -- Docker build context is the repo root when
# `docker build -f deploy/staging/Dockerfile .` runs.
COPY . .

RUN git submodule init && git submodule update
RUN cmake -S . -B ./build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
RUN cmake --build ./build -j 16 --target mo_simulator -j 16
RUN chmod +x /MicroOcppSimulator/build/mo_simulator

COPY deploy/staging/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

EXPOSE 8000
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
