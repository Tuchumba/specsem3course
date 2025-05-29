CCG = gcc
CC = clang
CFLAGS = -Wall -Wextra -pthread
SCAN_BUILD = scan-build
SCAN_VIEW = scan-view
BIN_DIR = bin
TERMINAL = xterm -e
A = 0
B = 1
P = 6
PORT = 8080
TIMEOUT = 10000
TESTS_DIR = tests

all: dirs master_integral worker_integral

dirs:
	@mkdir -p $(BIN_DIR)

master_integral: master_integral.c
	@$(CC) $(CFLAGS) $< -o $(BIN_DIR)/$@

worker_integral: worker_integral.c
	@$(CC) $(CFLAGS) $< -o $(BIN_DIR)/$@

run: all
	@echo "Starting master and workers in separate terminals..."
	@xterm -hold -e "./bin/master_integral $(PORT) 2 $(TIMEOUT) $(A) $(B)" &
	@sleep 1
	@xterm -hold -e "./bin/worker_integral 127.0.0.1 $(PORT) $(P) $(TIMEOUT)" &
	@xterm -hold -e "./bin/worker_integral 127.0.0.1 $(PORT) $(P) $(TIMEOUT)" &
	@sleep $(TIMEOUT)

test: all
	@echo "Running test scenarios..."
	@chmod +x $(TESTS_DIR)/run_tests.sh
	@$(TESTS_DIR)/run_tests.sh
	
clean:
	@rm -rf $(BIN_DIR)

.PHONY: all clean dirs run time_run
