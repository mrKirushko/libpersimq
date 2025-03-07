# - PERSIMQ (Persistent Simplistic Message Queue) library makefile -
# Full rebuild is set up to happen at each make/make all.
# Run "make" to build everything for your current system.
BUILDROOT_PATH = $(HOME)/BR7M_buildenv

CC=gcc
CFLAGS=-Os -s -Wall -Wno-unused-result -std=gnu17
OUTPUT_DIR=./Output

first: all

dirs:
	[ -d $(OUTPUT_DIR) ] || mkdir -p $(OUTPUT_DIR)

lib:
	$(CC) $(CFLAGS) persimq.c -c -o $(OUTPUT_DIR)/persimq.o
	ar rvs $(OUTPUT_DIR)/libpersimq.a $(OUTPUT_DIR)/persimq.o
	rm $(OUTPUT_DIR)/*.o

persimq_reader:
	$(CC) $(CFLAGS) persimq_reader.c -lpersimq -L$(OUTPUT_DIR) -I. -o $(OUTPUT_DIR)/persimq_reader

examples: lib
	$(CC) $(CFLAGS) ./examples/example.c -lpersimq -L$(OUTPUT_DIR) -I. -o $(OUTPUT_DIR)/example

clean:
	rm -rf $(OUTPUT_DIR)/*

help:
	@echo "Usage: make                compile all the programs"
	@echo "       make dirs           prepare output directories"
	@echo "       make all            same as make"
	@echo "       make help           show this info"
	@echo "       make lib            build the library"
	@echo "       make examples       build the examples"
	@echo "       make persimq_reader build the queue file reader utility"
	@echo "       make clean          remove redundant data"

all: dirs lib examples persimq_reader
