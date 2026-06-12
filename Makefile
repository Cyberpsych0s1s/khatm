CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

# Windows (MSYS2/MinGW make sets OS): name the binary khatm.exe.
# Two sub-cases for the commands in clean/install:
#  - MSYS2 / Git Bash shells set MSYSTEM, run recipes through sh, and put
#    $(PREFIX)/bin on PATH   the POSIX commands are the right ones.
#  - native mingw32-make runs recipes through cmd.exe (`rm`/`install` do
#    not exist); install goes to the per-user app dir, no admin needed.
ifeq ($(OS),Windows_NT)
EXE   := .exe
ifeq ($(MSYSTEM),)
RMOBJ      = del /q /f $(subst /,\,$(OBJ)) 2>NUL
RMBIN      = del /q /f khatm.exe 2>NUL
INSTALLDIR = $(LOCALAPPDATA)\Programs\khatm
INST_MKDIR = if not exist "$(INSTALLDIR)" mkdir "$(INSTALLDIR)"
INST_COPY  = copy /y khatm.exe "$(INSTALLDIR)" >NUL
INST_RM    = del /q /f "$(INSTALLDIR)\khatm.exe" 2>NUL
INST_NOTE  = @echo if khatm is not found, add $(INSTALLDIR) to your PATH
else
RMOBJ      = rm -f $(OBJ)
RMBIN      = rm -f khatm khatm.exe
INSTALLDIR = $(DESTDIR)$(BINDIR)
INST_MKDIR = mkdir -p "$(INSTALLDIR)"
INST_COPY  = install -m 0755 khatm.exe "$(INSTALLDIR)/khatm.exe"
INST_RM    = rm -f "$(INSTALLDIR)/khatm.exe"
INST_NOTE  = @:
endif
else
EXE   :=
RMOBJ      = rm -f $(OBJ)
RMBIN      = rm -f khatm khatm.exe
INSTALLDIR = $(DESTDIR)$(BINDIR)
INST_MKDIR = install -d "$(INSTALLDIR)"
INST_COPY  = install -m 0755 khatm "$(INSTALLDIR)/khatm"
INST_RM    = rm -f "$(INSTALLDIR)/khatm"
INST_NOTE  = @:
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

install: khatm$(EXE)
	$(INST_MKDIR)
	$(INST_COPY)
	@echo installed: $(INSTALLDIR)
	$(INST_NOTE)

uninstall:
	-$(INST_RM)

.PHONY: clean test install uninstall
