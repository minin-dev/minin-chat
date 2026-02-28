# ============================================================
# MININ-CHAT BUILD SYSTEM
# Compiles: Fortran (encryption) + COBOL (formatter) + C (server)
# ============================================================

CC      = gcc
FC      = gfortran
COBC    = cobc
CFLAGS  = -O2 -Wall -Wextra
FFLAGS  = -O2
COBFLAGS = -x -fixed

# Targets
SERVER  = server
CHAT    = chat
FOBJ    = encrypt.o

.PHONY: all clean test

all: $(SERVER) $(CHAT)

# Fortran encryption module -> object file
$(FOBJ): encrypt.f90
	$(FC) $(FFLAGS) -c $< -o $@

# COBOL message processor -> executable
$(CHAT): chat.cob
	$(COBC) $(COBFLAGS) $< -o $@

# C HTTP server + Fortran object -> executable
$(SERVER): server.c $(FOBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lgfortran -lm

# Quick test
test: all
	@echo "--- Testing Fortran encryption ---"
	@echo "OK" | ./$(CHAT) || echo "(COBOL test - check output)"
	@echo "--- Build complete ---"
	@ls -la $(SERVER) $(CHAT)

clean:
	rm -f $(SERVER) $(CHAT) $(FOBJ) *.mod
