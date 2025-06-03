FROM ghcr.io/zephyrproject-rtos/ci-base:v0.28.0

ARG USERNAME=user
ARG ZSDK_VERSION=0.17.0
ENV ZSDK_VERSION=$ZSDK_VERSION
ARG KITWARE_NINJA_VERSION=1.11.1.g95dee.kitware.jobserver-1
ENV KITWARE_NINJA_VERSION=$KITWARE_NINJA_VERSION
ARG DOXYGEN_VERSION=1.9.4
ENV DOXYGEN_VERSION=$DOXYGEN_VERSION
ARG LLVM_VERSION=20
ENV LLVM_VERSION=$LLVM_VERSION
ARG PROTOC_VERSION=21.7
ENV PROTOC_VERSION=$PROTOC_VERSION
ARG WGET_ARGS="-q --show-progress --progress=bar:force:noscroll"

# Install Kitware ninja
# NOTE: Pre-built Kitware ninja binaries are only available for x86_64 host.
RUN if [ "${HOSTTYPE}" = "x86_64" ]; then \
	wget ${WGET_ARGS} https://github.com/Kitware/ninja/releases/download/v${KITWARE_NINJA_VERSION}/ninja-${KITWARE_NINJA_VERSION}_x86_64-linux-gnu.tar.gz && \
	tar xf ninja-${KITWARE_NINJA_VERSION}_x86_64-linux-gnu.tar.gz -C /opt && \
	ln -s /opt/ninja-${KITWARE_NINJA_VERSION}_x86_64-linux-gnu/ninja /usr/local/bin && \
	rm ninja-${KITWARE_NINJA_VERSION}_x86_64-linux-gnu.tar.gz \
	; fi

# Install Doxygen (x86 only)
# NOTE: Pre-built Doxygen binaries are only available for x86_64 host.
RUN if [ "${HOSTTYPE}" = "x86_64" ]; then \
	wget ${WGET_ARGS} "https://github.com/doxygen/doxygen/releases/download/Release_${DOXYGEN_VERSION//./_}/doxygen-${DOXYGEN_VERSION}.linux.bin.tar.gz" && \
	tar xf doxygen-${DOXYGEN_VERSION}.linux.bin.tar.gz -C /opt && \
	ln -s /opt/doxygen-${DOXYGEN_VERSION}/bin/doxygen /usr/local/bin && \
	rm doxygen-${DOXYGEN_VERSION}.linux.bin.tar.gz \
	; fi

# Install LLVM and Clang
RUN wget ${WGET_ARGS} https://apt.llvm.org/llvm.sh && \
	chmod +x llvm.sh && \
	./llvm.sh ${LLVM_VERSION} all && \
	rm -f llvm.sh

# Install protobuf-compiler
RUN mkdir -p /opt/protoc && \
	cd /opt/protoc && \
	PROTOC_HOSTTYPE=$(case $HOSTTYPE in x86_64) echo "x86_64";; aarch64) echo "aarch_64";; esac) && \
	wget ${WGET_ARGS} https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOC_VERSION}/protoc-${PROTOC_VERSION}-linux-${PROTOC_HOSTTYPE}.zip && \
	unzip protoc-${PROTOC_VERSION}-linux-${PROTOC_HOSTTYPE}.zip && \
	ln -s /opt/protoc/bin/protoc /usr/local/bin && \
	rm -f protoc-${PROTOC_VERSION}-linux-${PROTOC_HOSTTYPE}.zip

# Install Zephyr SDK
# We only need arm and x86 toolchains
RUN mkdir -p /opt/toolchains && \
	cd /opt/toolchains && \
	wget ${WGET_ARGS} https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZSDK_VERSION}/zephyr-sdk-${ZSDK_VERSION}_linux-${HOSTTYPE}_minimal.tar.xz && \
	tar xf zephyr-sdk-${ZSDK_VERSION}_linux-${HOSTTYPE}_minimal.tar.xz && \
	zephyr-sdk-${ZSDK_VERSION}/setup.sh -t arm-zephyr-eabi -t x86_64-zephyr-elf -h -c && \
	rm zephyr-sdk-${ZSDK_VERSION}_linux-${HOSTTYPE}_minimal.tar.xz

# Run the Zephyr SDK setup script as 'user' in order to ensure that the
# `Zephyr-sdk` CMake package is located in the package registry under the
# user's home directory.
USER $USERNAME

RUN sudo -E -- bash -c ' \
	/opt/toolchains/zephyr-sdk-${ZSDK_VERSION}/setup.sh -c && \
	chown -R $USERNAME:$USERNAME /home/$USERNAME/.cmake \
	'

USER root

# Set the locale
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-${ZSDK_VERSION}/
ENV PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig

# Install additional packages
RUN apt-get -y update && \
	apt-get -y upgrade && \
	apt-get install --no-install-recommends -y \
		jq

# Install Python dependencies
RUN pip3 install --no-cache-dir \
		-r https://raw.githubusercontent.com/sevenlab-de/thingsboard-sdk/main/scripts/requirements.txt

# Clean up stale packages
RUN apt-get clean -y && \
	apt-get autoremove --purge -y && \
	rm -rf /var/lib/apt/lists/*
