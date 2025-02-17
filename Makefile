SRCFILES := $(wildcard src/*.c)
HFILES := $(wildcard src/*.h)
LIBSODIUM_DIR := src/lib/libsodium-1.0.18/build


all: cachesim

cachesim: $(SRCFILES) $(HFILES)
	gcc -Wall -g -o cachesim $(SRCFILES) -lm -I$(LIBSODIUM_DIR)/include -L$(LIBSODIUM_DIR)/lib -lsodium

submission: cachesim
	./bin/makesubmission.sh

grade: cachesim
	./bin/run_grader.py --fast

grade-full: cachesim
	./bin/run_grader.py

clean:
	rm -rfv test_results cachesim *-project1.tar.gz

.PHONY: all submission clean grade grade-full
