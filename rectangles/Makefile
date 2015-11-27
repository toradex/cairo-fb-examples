# Set this to point to your cross compile toolchain
SYSROOTS ?= /build/ags/poky-fido/build/tmp/sysroots
TARGET_SYSROOT ?= ${SYSROOTS}/colibri-vf/
CC ?= ${SYSROOTS}/x86_64-linux/usr/bin/arm-poky-linux-gnueabi/arm-poky-linux-gnueabi-gcc

CPPFLAGS ?= -O2 -g -march=armv7-a -mfpu=neon --sysroot=${TARGET_SYSROOT}

rectangles: rectangles.c
	${CC} ${CPPFLAGS} -lcairo -lts ${LDFLAGS} -o $@ $^

clean:
	rm -rf rectangles
