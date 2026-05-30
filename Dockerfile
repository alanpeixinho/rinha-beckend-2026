FROM alpine:3.21 AS builder

RUN apk add --no-cache cmake g++ git make

WORKDIR /app
COPY CMakeLists.txt .
COPY src/ src/

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc)

FROM alpine:3.21

RUN apk add --no-cache libgcc libstdc++

COPY --from=builder /app/build/rinha-2026 /usr/local/bin/rinha-2026

RUN mkdir -p /sockets

EXPOSE 9999
CMD ["rinha-2026"]
