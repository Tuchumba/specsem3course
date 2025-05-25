CC = gcc
CFLAGS = -Wall -Wextra -lpthread
BIN_DIR = bin
TERMINAL = xterm -e

all: dirs master_integral worker_integral

dirs:
	@mkdir -p $(BIN_DIR)

master_integral: master_integral.c
	@$(CC) $(CFLAGS) $< -o $(BIN_DIR)/$@

worker_integral: worker_integral.c
	@$(CC) $(CFLAGS) $< -o $(BIN_DIR)/$@

run: all
	@echo "Starting master and workers in separate terminals..."
	@$(TERMINAL) "$(BIN_DIR)/master_integral $(PORT) $(WORKERS) $(A) $(B)" & \
	sleep 0.5 && \
	$(TERMINAL) "$(BIN_DIR)/worker_integral 127.0.0.1 $(PORT) 2" & \
	$(TERMINAL) "$(BIN_DIR)/worker_integral 127.0.0.1 $(PORT) 2" & \
	$(TERMINAL) "$(BIN_DIR)/worker_integral 127.0.0.1 $(PORT) 2" &

time_run: all
	@echo "Starting with time measurement..."
	@time ( \
		$(TERMINAL) "$(BIN_DIR)/master_integral $(PORT) $(WORKERS) $(A) $(B)" & \
		sleep 0.5 && \
		$(TERMINAL) "$(BIN_DIR)/worker_integral 127.0.0.1 $(PORT) 2" & \
		$(TERMINAL) "$(BIN_DIR)/worker_integral 127.0.0.1 $(PORT) 2" & \
		$(TERMINAL) "$(BIN_DIR)/worker_integral 127.0.0.1 $(PORT) 2" & \
		wait \
	)

clean:
	@rm -rf $(BIN_DIR)

.PHONY: all clean dirs run time_run
