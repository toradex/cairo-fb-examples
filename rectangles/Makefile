SYSROOTS = /build/ags/poky-fido/build/tmp/sysroots
CC = ${SYSROOTS}/x86_64-linux/usr/bin/arm-poky-linux-gnueabi/arm-poky-linux-gnueabi-gcc
INCLUDES = "-I${SYSROOTS}/colibri-vf/usr/include/"
LIB_PATH = "-L${SYSROOTS}/colibri-vf/usr/lib"
LDFLAGS = -lcairo -lts
CFLAGS = -O2 -g -march=armv7-a -mfpu=neon --sysroot=${SYSROOTS}/colibri-vf/

rectangles: rectangles.c
	${CC} ${CFLAGS} ${INCLUDES} ${LIB_PATH} ${LDFLAGS} -o $@ $^

clean:
	rm -rf cairo
