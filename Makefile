CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=

# Windows (MSYS2/MinGW make sets OS): name the binary khatm.exe.
# On Windows mingw32-make runs recipes through cmd.exe, where `rm` does
# not exist, so define a platform-appropriate delete command for `clean`.
ifeq ($(OS),Windows_NT)
EXE   := .exe
RMOBJ  = del /q /f $(subst /,\,$(OBJ)) 2>NUL
RMBIN  = del /q /f khatm.exe 2>NUL
else
EXE   :=
RMOBJ  = rm -f $(OBJ)
RMBIN  = rm -f khatm khatm.exe
endif

SRC := src/plat.c src/util.c src/syl.c src/log.c src/plan.c src/pace.c \
       src/ui.c src/cmd.c src/tui.c src/api.c src/main.c
OBJ := $(SRC:.c=.o)

khatm$(EXE): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c src/khatm.h src/plat.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-$(RMBIN)
	-$(RMOBJ)

test: khatm$(EXE)
	bash tests/smoke.sh ./khatm$(EXE)

.PHONY: clean test
