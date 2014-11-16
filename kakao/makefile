OUT = $(shell basename `pwd`)

CFLAGS = -Wall
DFLAGS = -color=on -de -m64 -w
LIBS = libglfw3.a

SRC_D = $(wildcard *.d) $(wildcard */*.d) $(wildcard */*/*.d)
SRC_C = $(wildcard *.c)
OBJ = $(SRC_D:.d=._d_.o) $(SRC_C:.c=._c_.o)

.PHONY: all debug release profile clean

all: debug

debug: DFLAGS += -g -debug -unittest
release: DFLAGS += -O -release -inline -noboundscheck
profile: DFLAGS += -g -O -profile

debug release profile: $(OUT)

$(OUT): $(OBJ)
	@echo $@
	@dmd $(DFLAGS) -of$@ $(OBJ) $(LIBS)

%._c_.o: %.c
	@echo $<
	@cc -c $(CFLAGS) $< -o $@

%._c_.o: %.c
	@echo $<
	@cc -c $(CFLAGS) $< -o $@

%._d_.o: %.d
	@echo $<
	@dmd -c $(DFLAGS) $< -of$@

clean:
	@rm -f $(OBJ) $(OUT) trace.{def,log}
