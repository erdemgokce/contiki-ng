CONTIKI_PROJECT = udp-client udp-server
PROJECT_SOURCEFILES += ota-metadata.c
all: $(CONTIKI_PROJECT)

CONTIKI=../..
MODULES += os/storage/cfs
include $(CONTIKI)/Makefile.include
