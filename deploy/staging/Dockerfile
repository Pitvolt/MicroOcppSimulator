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

# Clone fresh from GitHub with submodules recursively -- avoids relying on
# either .git being COPYed (Render strips it) or the host performing a
# recursive checkout. Rebuilds pick up the LATEST main; pin to a tag via
# `--branch <tag>` when a specific release matters.
# NB: the parent's .gitmodules points at Pitvolt/* forks, so `--recursive`
# fetches the exact submodules the staging bundle expects.
ARG SIM_REF=main
RUN git clone --depth 1 --branch "${SIM_REF}" --recurse-submodules \
      --shallow-submodules \
      https://github.com/Pitvolt/MicroOcppSimulator.git /MicroOcppSimulator

WORKDIR /MicroOcppSimulator

RUN cmake -S . -B ./build -DCMAKE_POLICY_VERSION_MINIMUM=3.5
RUN cmake --build ./build -j 16 --target mo_simulator -j 16
RUN chmod +x /MicroOcppSimulator/build/mo_simulator

# The entrypoint is bundled with the freshly-cloned tree, but copy the
# staging-bundle canonical version onto /usr/local/bin so any future edit
# to the file on main takes effect on the next rebuild.
RUN cp /MicroOcppSimulator/deploy/staging/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

EXPOSE 8000
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
