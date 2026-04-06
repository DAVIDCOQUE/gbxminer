FROM ubuntu:24.04
ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC
RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get dist-upgrade -y && \
    apt-get install libcurl4 libcurl4-openssl-dev \
    libssl-dev \
    libjansson-dev \
    automake \
    autotools-dev \
    build-essential \
    git \
    wget \
    -y
RUN wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404/x86_64/cuda-ubuntu2404.pin
RUN sudo mv cuda-ubuntu2404.pin /etc/apt/preferences.d/cuda-repository-pin-600
RUN wget https://developer.download.nvidia.com/compute/cuda/13.2.0/local_installers/cuda-repo-ubuntu2404-13-2-local_13.2.0-595.45.04-1_amd64.de
RUN dpkg -i cuda-repo-ubuntu2404-13-2-local_13.2.0-595.45.04-1_amd64.deb
RUN apt-key add /var/cuda-repo-ubuntu2404-13-2-local/*.pub
RUN cp /var/cuda-repo-ubuntu2404-13-2-local/cuda-*-keyring.gpg /usr/share/keyrings/
RUN apt-get update
RUN apt-get -y install cuda-toolkit-13-2 cuda-cudart-13-2 libnvidia-compute-520
RUN rm *.deb
RUN ldconfig
RUN git clone https://github.com/d0wn3d/gbxminer
WORKDIR gbxminer
RUN git checkout linux
RUN ./build.sh
RUN strip -s gbxminer
RUN make install
RUN make clean
#RUN gbxminer --version
RUN apt-get remove -y libcurl4-openssl-dev libssl-dev libjansson-dev
#ENTRYPOINT [ "./gbxminer" ]
