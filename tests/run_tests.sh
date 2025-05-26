#!/bin/bash

BIN_DIR=./bin
TESTS_DIR=.
PORT=12345
PORT2=8080
MASTER_IP=127.0.0.1
WORKERS=3
TIMEOUT=8

# Функция для завершения всех процессов
cleanup() {
    pkill -f $BIN_DIR/master_integral
    pkill -f $BIN_DIR/worker_integral
    exit 1
}

trap cleanup SIGINT SIGTERM

# Тест 1: Нормальное выполнение
echo "=== Test 1: Normal execution ==="
xterm -hold -e "$BIN_DIR/master_integral $PORT 2 $TIMEOUT 0 1" &
sleep 1
for i in $(seq 1 2); do
    xterm -hold -e "$BIN_DIR/worker_integral  $MASTER_IP $PORT 6 $TIMEOUT" &
done
sleep $TIMEOUT

echo "=== Test 1.2: Normal execution ==="
xterm -hold -e "$BIN_DIR/master_integral $PORT 2 $TIMEOUT 0 1" &
sleep 1
for i in $(seq 1 2); do
    xterm -hold -e "$BIN_DIR/worker_integral  $MASTER_IP $PORT 1 $TIMEOUT" &
done
sleep $TIMEOUT

echo "=== Test 2: Normal execution ==="
xterm -hold -e "$BIN_DIR/master_integral $PORT 4 $TIMEOUT 0 1" &
sleep 1
for i in $(seq 1 4); do
    xterm -hold -e "$BIN_DIR/worker_integral  $MASTER_IP $PORT 1 $TIMEOUT" &
done
sleep $TIMEOUT

echo "=== Test 2.1: Normal execution ==="
xterm -hold -e "$BIN_DIR/master_integral $PORT 4 $TIMEOUT 0 1" &
sleep 1
for i in $(seq 1 4); do
    xterm -hold -e "$BIN_DIR/worker_integral  $MASTER_IP $PORT 3 $TIMEOUT" &
done
sleep $TIMEOUT

sleep 10


# Тест 3: Ошибка в worker (деление на 0)
echo -e "\n=== Test 3: Worker error (division by 0) ==="
xterm -hold -e "$BIN_DIR/master_integral  $PORT 2 $TIMEOUT -1 1" &
sleep 1
for i in $(seq 1 2); do
    xterm -hold -e "$BIN_DIR/worker_integral  $MASTER_IP $PORT 4 $TIMEOUT" &
done
sleep $TIMEOUT


echo -e "\nAll tests completed"
cleanup