FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libmongoc-dev \
    libbson-dev \
    libmongocxx-dev \
    wget \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN mkdir -p third_party && \
    wget -q https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h \
    -O third_party/httplib.h

RUN mkdir build && cd build && \
    cmake .. && \
    make -j$(nproc)

ENV PATH="/app/build:${PATH}"

EXPOSE 8080

CMD ["./build/web_server", "--index", "/data/index.bin", "--port", "8080"]