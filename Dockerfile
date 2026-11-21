# syntax=docker/dockerfile:1

# ---------- Stage 1: build (and test) ----------
FROM ubuntu:24.04 AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY tests ./tests

# CMake downloads SQLite and GoogleTest during this step (needs internet).
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build

# The image build fails if any test fails. Skip with: --build-arg RUN_TESTS=false
ARG RUN_TESTS=true
RUN if [ "$RUN_TESTS" = "true" ]; then ctest --test-dir build --output-on-failure; fi

# ---------- Stage 2: small runtime image ----------
FROM ubuntu:24.04 AS runtime

# Run as an unprivileged user; keep the database in /data so it can be a volume.
RUN useradd --system --uid 10001 --no-create-home ev \
 && mkdir /data && chown ev:ev /data

COPY --from=build /src/build/ev_charger /usr/local/bin/ev_charger
COPY --from=build /src/build/ev_charger_server /usr/local/bin/ev_charger_server

USER ev
WORKDIR /data
VOLUME /data
EXPOSE 8080

ENTRYPOINT ["ev_charger"]
CMD ["/data/chargers.db"]

# REST server instead of the demo:
#   docker run --rm -p 127.0.0.1:8080:8080 --entrypoint ev_charger_server IMAGE /data/chargers.db 8080 0.0.0.0
