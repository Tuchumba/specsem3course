#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <netinet/tcp.h>

#define MAX_THREADS 32
#define BUFFER_SIZE 1024

typedef struct {
    int max_cores;
    int active_threads;
    pthread_mutex_t lock;
    int max_timeout;
} WorkerConfig;

typedef struct {
    char task_id[32];
    char data[BUFFER_SIZE];
    int client_fd;
} Task;

WorkerConfig worker_config;

double numerical_integrate(double a, double b, size_t num_steps) {
    double h = (b - a) / num_steps;
    double sum = 0.0;
    
    for (size_t i = 0; i < num_steps; i++) {
        double x = a + (i + 0.5) * h;
        if ( a < 0 ) {
            fprintf(stderr, "[Worker] Critical error: division by zero\n");
            printf("[Worker] Critical error: division by zero\n");
            return -1;
        }
        double y = x*x;
        sum += y * h;
    }
    return sum;
}

void* compute_task(void* arg) {
    time_t task_start_time = time(NULL);
    Task* task = (Task*)arg;
    double a, b;
    size_t num_steps;
    
    if (sscanf(task->data, "integrate %lf %lf %lu", &a, &b, &num_steps) != 3) {
        fprintf(stderr, "Invalid task format\n");
        free(task);
        return NULL;
    }

    double result = numerical_integrate(a, b, num_steps);
    
    if (result == -1) { 
        send(task->client_fd, "ERROR\n", 6, 0);
    } else {
        char response[128];
        snprintf(response, sizeof(response), "RESULT %s %.6f\n", task->task_id, result);
        send(task->client_fd, response, strlen(response), 0);
    }

    double total_task_time = time(NULL) - task_start_time;
    printf("\nDEBUG: task %s execution time: %.6f seconds\n", task->task_id, total_task_time);

    free(task);
    pthread_mutex_lock(&worker_config.lock);
    worker_config.active_threads--;
    pthread_mutex_unlock(&worker_config.lock);
    return NULL;
}

void worker_init(int max_cores, int max_timeout) {
    worker_config.max_cores = max_cores;
    worker_config.active_threads = 0;
    worker_config.max_timeout = max_timeout;
    pthread_mutex_init(&worker_config.lock, NULL);
}

int connect_to_master(const char* ip, int port) {
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    int enable = 1;
    setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));

    int idle = 2;     // Начать проверки через 2 секунды бездействия
    int interval = 1; // Интервал проверок: 1 секунда
    int count = 3;    // Количество проверок перед разрывом

    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    setsockopt(client_fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));

    if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr))) {
        perror("connect");
        close(client_fd);
        exit(1);
    }

    return client_fd;
}

void handle_client(int client_fd) {
    while (1) {
        char buffer[BUFFER_SIZE];
        int len = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (len <= 0) {
            close(client_fd);
            fprintf(stderr, "Connection with master lost\n");
            return;
        }
        buffer[len] = '\0';
        for (char* msg = strtok(buffer, " \n");msg != NULL; msg = strtok(NULL, " \n")) {
            if (strcmp(msg, "CONFIG") == 0) {
                char response[128];
                snprintf(response, sizeof(response), "CORES %d\n", worker_config.max_cores);
                send(client_fd, response, strlen(response), 0);
                continue;
            }
            
            if (strcmp(msg, "TASK") == 0) {
                char* task_id = strtok(NULL, " ");
                char* data = strtok(NULL, "\n");
                
                printf("taskid=%s\n", task_id);
                printf("data=%s\n", data);
                
                if (!task_id || !data) {
                    fprintf(stderr, "Invalid task format\n");
                    continue;
                }

                pthread_mutex_lock(&worker_config.lock);
                if (worker_config.active_threads >= MAX_THREADS) {
                    pthread_mutex_unlock(&worker_config.lock);
                    fprintf(stderr, "Thread limit reached (%d)\n", worker_config.max_cores);
                    continue;
                }
                worker_config.active_threads++;
                pthread_mutex_unlock(&worker_config.lock);

                Task* task = malloc(sizeof(Task));
                strncpy(task->task_id, task_id, sizeof(task->task_id) - 1);
                strncpy(task->data, data, sizeof(task->data) - 1);
                task->client_fd = client_fd;
                
                pthread_t thread;
                pthread_create(&thread, NULL, compute_task, task);
                pthread_detach(thread);
            }

            if (strcmp(msg, "SHUTDOWN") == 0) {
                printf("[Worker] Received shutdown command. Exiting.\n");
                close(client_fd);
                exit(0);
            }
        }
    
        char tmp;
        if (recv(client_fd, &tmp, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
            printf("[Worker] Master disconnected. Exiting.\n");
            close(client_fd);
            exit(0);
        }
    }
    
}

int main(int argc, char** argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <master_ip> <master_port> <max_cores> <max_timeout>\n", argv[0]);
        return 1;
    }

    worker_init(atoi(argv[3]), atoi(argv[4]));

    alarm(worker_config.max_timeout);


    int client_fd = connect_to_master(argv[1], atoi(argv[2]));


    printf("Connected to master at %s:%d (max_cores=%d)\n", 
           argv[1], atoi(argv[2]), atoi(argv[3]));
    
    handle_client(client_fd);

    return 0;
}