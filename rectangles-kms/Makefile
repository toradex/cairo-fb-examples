CFLAGS += $(shell pkg-config --cflags libdrm)
CFLAGS += $(shell pkg-config --cflags libkms)
CFLAGS += $(shell pkg-config --cflags cairo)
LDFLAGS += $(shell pkg-config --libs libdrm)
LDFLAGS += $(shell pkg-config --libs libkms)
LDFLAGS += $(shell pkg-config --libs cairo)

SOURCES = rectangles-kms.c
OBJECTS = $(SOURCES:.c=.o)

all: $(SOURCES) rectangles-kms

rectangles-kms: $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

.o:
	$(CC) $< -o $@
