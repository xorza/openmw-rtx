FROM ubuntu:26.04@sha256:6a06111c3b0a8600feadb270aec771dc239bf407b25625f93f44ab00e11f4d3c

ENV DEBIAN_FRONTEND="noninteractive" \
    HOME="/home/docs" \
    PATH="/home/docs/venv/bin:$PATH"

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        git \
        libncurses-dev \
        libreadline-dev \
        python3 \
        python3-venv \
        unzip \
        wget \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /home/docs \
    && touch /home/docs/omw_luadoc_docker

WORKDIR /home/docs

# A virtual environment: Ubuntu's Python refuses a package installed beside its own (PEP 668).
COPY requirements.txt ./
RUN python3 -m venv /home/docs/venv && pip install --no-cache-dir -r requirements.txt

COPY source/install_luadocumentor_in_docker.sh ./
RUN bash -e ./install_luadocumentor_in_docker.sh

VOLUME /openmw