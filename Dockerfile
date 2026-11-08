# Default base matches the tested stack (OpenSSL 1.1.1 + BerkeleyDB 5.3).
# Try OpenSSL 3:  docker build --build-arg BASE=debian:bookworm-slim .
# Run the test suite during build:  docker build --build-arg RUN_TESTS=1 .
ARG BASE=debian:bullseye-slim

FROM ${BASE} AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make pkg-config libssl-dev libdb++-dev procps ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY Makefile test.sh ./
COPY src ./src
RUN make -j"$(nproc)"
ARG RUN_TESTS=0
RUN if [ "$RUN_TESTS" = "1" ]; then bash test.sh; fi

FROM ${BASE}
RUN apt-get update && apt-get install -y --no-install-recommends libdb5.3++ openssl \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/bitcoin /usr/local/bin/bitcoin
VOLUME /data
EXPOSE 8333
ENTRYPOINT ["/usr/local/bin/bitcoin", "-datadir=/data"]
