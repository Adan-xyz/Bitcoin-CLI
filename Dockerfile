# Default base: Debian 12 (OpenSSL 3 + BerkeleyDB 5.3), same stack as the Ubuntu CI jobs.
# Run the test suite during build:  docker build --build-arg RUN_TESTS=1 .
# Other bases may use different runtime package names (e.g. libssl3t64, libdb5.3++t64).
ARG BASE=debian:bookworm-slim

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
RUN apt-get update && apt-get install -y --no-install-recommends libdb5.3++ libssl3 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/bitcoin /usr/local/bin/bitcoin
VOLUME /data
EXPOSE 8333
ENTRYPOINT ["/usr/local/bin/bitcoin", "-datadir=/data"]
