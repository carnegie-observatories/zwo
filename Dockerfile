FROM ubuntu:24.04
RUN mkdir /app
RUN apt-get update

# Install dependencies
RUN apt-get install -y wget make gcc-aarch64-linux-gnu bzip2 telnet
RUN apt-get install -y libx11-dev

# Copy files
COPY . /app/zwo

## Install dependencies
RUN apt-get install -y libusb-1.0-0-dev
## ASI SDK
RUN cd /app/zwo/docs/ZWO/Setup/ && tar jxvf ASI_linux_mac_SDK_V1.20.2.tar.bz2 && cd ASI_linux_mac_SDK_V1.20.2/lib 
RUN cp /app/zwo/docs/ZWO/Setup/ASI_linux_mac_SDK_V1.20.2/lib/armv8/libASICamera2.so.1.20.2 /usr/local/lib
RUN cd /app/zwo/docs/ZWO/Setup/ASI_linux_mac_SDK_V1.20.2/lib/ && install asi.rules /lib/udev/rules.d
RUN cd /usr/local/lib && ln -s libASICamera2.so.1.20.2 libASICamera2.so
## EFW SDK
RUN cd /app/zwo/docs/ZWO/Setup/ && tar jxvf EFW_linux_mac_SDK_V1.7.tar.bz2 && cd EFW_linux_mac_SDK_V1.7/lib
RUN cp /app/zwo/docs/ZWO/Setup/EFW_linux_mac_SDK_V1.7/lib/armv8/libEFWFilter.so.1.7 /usr/local/lib
RUN cd /app/zwo/docs/ZWO/Setup/EFW_linux_mac_SDK_V1.7/lib/ && install efw.rules /lib/udev/rules.d
RUN cd /usr/local/lib && ln -s libEFWFilter.so.1.7 libEFWFilter.so
## build
RUN cd /app/zwo/src/server && make clean && make
## install
RUN cp /app/zwo/src/server/zwoserver /usr/local/bin

# copy service files
RUN mkdir -p /etc/systemd/system/ && cp /app/zwo/etc/systemd/system/zwo.service /etc/systemd/system/

# make tarball
RUN tar jcvf /app/zwo.tar.bz2 /usr/local/lib/ /usr/local/bin/ /lib/udev/rules.d/asi.rules /lib/udev/rules.d/efw.rules /etc/systemd/system/zwo.service

# Run
EXPOSE 52311
ENV LD_LIBRARY_PATH=/usr/local/lib/
CMD ["/app/zwo/src/server/zwoserver"]
