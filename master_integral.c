 #include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#define MAX_WORKERS 10
#define BUFFER_SIZE 1024
#define TASK_TIMEOUT 5

typedef struct {
    int fd;
    int busy;
    char task_id[32];
    time_t task_start_time;
    double partial_result;  // Для хранения частичного результата
    int result_received;   // Флаг получения результата
} Worker;

typedef struct {
    Worker workers[MAX_WORKERS];
    int num_workers;
    int max_workers;
    int max_timeout;
    struct pollfd fds[MAX_WORKERS + 1];
    double total_result;   // Суммарный результат
    int tasks_completed;   // Число завершенных задач
} MasterNode;

MasterNode master;
struct timespec program_start, program_end;

void master_init(int max_workers, int max_timeout) {
    master.max_workers = max_workers;
    master.max_timeout = max_timeout;
    master.num_workers = 0;
    master.total_result = 0.0;
    master.tasks_completed = 0;
    memset(master.workers, 0, sizeof(master.workers));
}

void send_task(int worker_idx, const char* task_id, const char* task_data) {
    Worker* worker = &master.workers[worker_idx];
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "TASK %s %s %d\n", task_id, task_data, TASK_TIMEOUT);
    send(worker->fd, buffer, strlen(buffer), 0);
    worker->busy = 1;
    worker->result_received = 0;
    strncpy(worker->task_id, task_id, sizeof(worker->task_id) - 1);
    worker->task_start_time = time(NULL);
    printf("[Master] Sent task %s to worker %d\n", task_id, worker_idx);
}

void handle_result(int worker_idx, const char* result_str) {
    Worker* worker = &master.workers[worker_idx];
    double result;
    if (sscanf(result_str, "%lf", &result) == 1) {
        worker->partial_result = result;
        worker->result_received = 1;
        master.total_result += result;
        master.tasks_completed++;
        printf("[Master] Received result from worker %d: %.6f\n", worker_idx, result);
    } else {
        fprintf(stderr, "[Master] Invalid result format from worker %d\n", worker_idx);
    }
    worker->busy = 0;
    if (master.tasks_completed >= master.max_workers) {
    printf("[Master] All tasks completed. Shutting down workers...\n");
    for (int i = 0; i < master.num_workers; i++) {
        send(master.workers[i].fd, "SHUTDOWN\n", 9, 0);
    }
}
}

void check_timeouts() {
    time_t now = time(NULL);
    for (int i = 0; i < master.num_workers; i++) {
        if (master.workers[i].busy && !master.workers[i].result_received && 
            (now - master.workers[i].task_start_time) > TASK_TIMEOUT) {
            printf("[Master] Worker %d timed out on task %s\n", i, master.workers[i].task_id);
            master.workers[i].busy = 0;
        }
    }
}

void run_master(int port, double a, double b) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr))) {
        perror("bind");
        close(listen_fd);
        exit(1);
    }

    if (listen(listen_fd, 5)) {
        perror("listen");
        close(listen_fd);
        exit(1);
    }

    master.fds[0].fd = listen_fd;
    master.fds[0].events = POLLIN;
    int nfds = 1;

    printf("[Master] Started on port %d. Waiting for %d workers...\n", port, master.max_workers);

    // Ждем подключения всех рабочих узлов
    while (master.num_workers < master.max_workers) {
        int ready = poll(master.fds, nfds, 1000);
        if (ready < 0) {
            perror("poll");
            break;
        }

        if (master.fds[0].revents & POLLIN) {
            int worker_fd = accept(listen_fd, NULL, NULL);
            if (worker_fd < 0) {
                perror("accept");
                continue;
            }

            master.workers[master.num_workers].fd = worker_fd;
            master.fds[nfds].fd = worker_fd;
            master.fds[nfds].events = POLLIN;
            printf("[Master] New worker connected (fd=%d), total: %d/%d\n", 
                  worker_fd, master.num_workers+1, master.max_workers);
            master.num_workers++;
            nfds++;
        }
    }

    // Раздаем задачи
    clock_gettime(CLOCK_MONOTONIC, &program_start);
    
    double step = (b - a) / master.max_workers;
    for (int i = 0; i < master.max_workers; i++) {
        double start = a + i * step;
        double end = start + step;
        char task_data[128];
        snprintf(task_data, sizeof(task_data), "integrate %lf %lf", start, end);
        
        char task_id[32];
        snprintf(task_id, sizeof(task_id), "task%d", i);
        
        send_task(i % master.num_workers, task_id, task_data);
    }

    // Собираем результаты
    while (master.tasks_completed < master.max_workers) {
        check_timeouts();
        
        int ready = poll(master.fds, nfds, 1000);
        if (ready < 0) {
            perror("poll");
            break;
        }

        for (int i = 1; i < nfds; i++) {
            if (master.fds[i].revents & POLLIN) {
                char buffer[BUFFER_SIZE];
                int len = recv(master.fds[i].fd, buffer, sizeof(buffer) - 1, 0);
                if (len <= 0) {
                    printf("[Master] Worker %d disconnected\n", i-1);
                    close(master.fds[i].fd);
                    master.fds[i] = master.fds[nfds-1];
                    nfds--;
                    i--;
                    continue;
                }

                buffer[len] = '\0';
                if (strncmp(buffer, "RESULT ", 7) == 0) {
                    char* task_id = strtok(buffer + 7, " ");
                    char* result_str = strtok(NULL, "\n");
                    if (task_id && result_str) {
                        handle_result(i - 1, result_str);
                    }
                }
            }
        }
    }

    printf("\n[Master] Final result: ∫f(x)dx from %.2f to %.2f = %.6f\n", a, b, master.total_result);
     clock_gettime(CLOCK_MONOTONIC, &program_end);
    double total_time = (program_end.tv_sec - program_start.tv_sec) + 
                   (program_end.tv_nsec - program_start.tv_nsec) / 1000000000.0;
    printf("\nTotal program execution time: %.6f seconds\n", total_time);
printf("[Master] Calculation completed. Shutting down...\n");
	fflush(stdout);
    close(listen_fd);
    
}

int main(int argc, char** argv) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <port> <max_workers> <max_timeout> <a> <b>\n", argv[0]);
        return 1;
    }
    

    int port = atoi(argv[1]);
    int max_workers = atoi(argv[2]);
    int max_timeout = atoi(argv[3]);
    double a = atof(argv[4]);
    double b = atof(argv[5]);

    master_init(max_workers, max_timeout);
    run_master(port, a, b);
   
    return 0;
}