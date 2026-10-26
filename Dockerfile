# Derleme + test ortamı (CI'daki ubuntu-24.04 ile aynı paketler)
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build pkg-config git ca-certificates \
      qt6-base-dev qt6-declarative-dev qt6-serialport-dev qt6-serialbus-dev \
      qt6-charts-dev qml6-module-qtcharts \
      qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
      qml6-module-qtquick-window qml6-module-qtquick-templates qml6-module-qtquick-dialogs \
      qml6-module-qtqml-workerscript qml6-module-qtqml \
      libgtest-dev \
      python3 iproute2 can-utils xvfb xauth fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
