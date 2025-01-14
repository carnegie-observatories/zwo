FROM --platform=linux/amd64 ubuntu:24.04
RUN mkdir /app
RUN apt-get update

# Install dependencies
RUN apt-get install -y wget make gcc bzip2 telnet

# Install CXT
RUN apt-get install -y libx11-dev git
ADD https://github.com/carnegie-observatories/cxt.git /app/cxt
RUN ln -s /app/cxt/src/ /app/CXT && cd /app/CXT/ && make clean && make

# Install ZWO
COPY . /app/zwo
# Build ZWO - GCAM
RUN cd /app/zwo/ && ln -s /app/CXT/ CXT
RUN cd /app/zwo/src/gcam && make clean && make
# Build ZWO - server
## Install dependencies
RUN apt-get install -y libusb-1.0-0-dev
## ASI SDK
RUN cd /app/zwo/docs/ZWO/Setup/ && tar jxvf ASI_linux_mac_SDK_V1.20.2.tar.bz2 && cd ASI_linux_mac_SDK_V1.20.2/lib 
RUN cp /app/zwo/docs/ZWO/Setup/ASI_linux_mac_SDK_V1.20.2/lib/x64/libASICamera2.so.1.20.2 /usr/local/lib
RUN cd /app/zwo/docs/ZWO/Setup/ASI_linux_mac_SDK_V1.20.2/lib/ && install asi.rules /lib/udev/rules.d
RUN cd /usr/local/lib && ln -s libASICamera2.so.1.20.2 libASICamera2.so
## EFW SDK
RUN cd /app/zwo/docs/ZWO/Setup/ && tar jxvf EFW_linux_mac_SDK_V1.7.tar.bz2 && cd EFW_linux_mac_SDK_V1.7/lib
RUN cp /app/zwo/docs/ZWO/Setup/EFW_linux_mac_SDK_V1.7/lib/x64/libEFWFilter.so.1.7 /usr/local/lib
RUN cd /app/zwo/docs/ZWO/Setup/EFW_linux_mac_SDK_V1.7/lib/ && install efw.rules /lib/udev/rules.d
RUN cd /usr/local/lib && ln -s libEFWFilter.so.1.7 libEFWFilter.so
## build
RUN cd /app/zwo/src/server && make clean && make
