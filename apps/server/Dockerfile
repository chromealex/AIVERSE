# Game server image. Build from the repository root:
#   docker build -f apps/server/Dockerfile -t aiverse-server .
#   docker run -p 27015:27015/udp -p 27016:27016 -v rts-data:/srv/aiverse/data aiverse-server
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake ninja-build && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF -DAIVERSE_BUILD_HEADLESS=OFF -DAIVERSE_BUILD_TESTS=OFF \
 && cmake --build build --target aiverse_server

FROM debian:bookworm-slim
RUN useradd --system --home /srv/aiverse aiverse && mkdir -p /srv/aiverse/data && chown aiverse /srv/aiverse/data
COPY --from=build /src/build/aiverse_server /usr/local/bin/aiverse_server
COPY apps/server/server.json /etc/aiverse/server.json
# Game data is optional for the server; it only makes saved replays point at real files.
COPY templates /srv/aiverse/templates
WORKDIR /srv/aiverse
USER aiverse
EXPOSE 27015/udp 27016/tcp
CMD ["aiverse_server", "--config", "/etc/aiverse/server.json", "--replays", "data/replays", "--ratings", "data/ratings.txt"]
