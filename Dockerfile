FROM alpine:3.21 AS builder

RUN apk add --no-cache cmake g++ gcc git make

WORKDIR /app
COPY CMakeLists.txt .
COPY src/ src/
COPY include/ include/

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc)

FROM alpine:3.21

RUN apk add --no-cache libgcc libstdc++

COPY --from=builder /app/build/rinha-2026 /usr/local/bin/rinha-2026

COPY resources/dataset_f16.dat /data/dataset.dat
COPY resources/kdtree_f32.dat /data/kdtree.dat

RUN mkdir -p /sockets

EXPOSE 9999
CMD ["rinha-2026", "/data/dataset.dat", "/data/kdtree.dat"]
