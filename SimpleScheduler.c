#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <time.h>
#include <semaphore.h>
#include <pthread.h>

#define MAX_LINE 100 /* The maximum length command */
#define HISTORY_FILE "history.txt"
void add_process(pid_t pid, char *cmd);
void *scheduler(void *arg);
pthread_mutex_t mutex;
volatile int stop_scheduler = 0;
struct Command
{
    char cmd[MAX_LINE][MAX_LINE];
    pid_t pid;
    struct timeval start, end;
    long duration;
};

#define MAX_PROCESSES 100
typedef struct
{
    pid_t pid;
    char *cmd;
    long duration;
    long execution_time;
    long wait_time;
} Process;

typedef struct
{
    Process processes[MAX_PROCESSES];
    int front;
    int rear;
    int size;
} Queue;

Queue ready_queue;
Queue completed_queue;
int NCPU;
int TSLICE;

void init_queue(Queue *queue)
{
    queue->front = 0;
    queue->rear = -1;
    queue->size = 0;
}

int is_empty(Queue *queue)
{
    return queue->size == 0;
}

int is_full(Queue *queue)
{
    return queue->size == MAX_PROCESSES;
}

void enqueue(Queue *queue, Process process)
{
    if (is_full(queue))
    {
        fprintf(stderr, "Error: Ready queue is full\n");
        return;
    }

    queue->rear = (queue->rear + 1) % MAX_PROCESSES;
    queue->processes[queue->rear] = process;
    queue->size++;
}

Process dequeue(Queue *queue)
{
    if (is_empty(queue))
    {
        fprintf(stderr, "Error: Ready queue is empty\n");
        Process empty_process = {0};
        return empty_process;
    }

    Process process = queue->processes[queue->front];
    queue->front = (queue->front + 1) % MAX_PROCESSES;
    queue->size--;

    return process;
}

struct Command history[MAX_LINE];
int history_count = 0;

void display_history()
{
    for (int i = 0; i < history_count; i++)
    {
        printf("Command: ");
        for (int j = 0; j < MAX_LINE; j++)
        {
            printf("%s ", history[i].cmd[j]);
        }
        printf("\n");
        printf("PID: %d\n", history[i].pid);
        printf("Start Time: %s", asctime(localtime(&history[i].start.tv_sec)));
        printf("Duration: %ld milliseconds\n", history[i].duration);
        printf("\n");
    }
}

void add_to_history(char **cmd, pid_t pid, struct timeval start, long duration)
{
    for (int i = 0; cmd[i] != NULL; i++)
    {
        strcpy(history[history_count].cmd[i], cmd[i]);
    }
    history[history_count].pid = pid;
    history[history_count].start = start;
    history[history_count].duration = duration;
    history_count++;
}

void handle_sigint(int sig)
{
    printf("\nCtrl+C pressed. Displaying history:\n");
    display_history();
    // Print information about all jobs
    for (int i = 0; i < ready_queue.size; i++)
    {
        Process *process = &ready_queue.processes[i];
        printf("Name: %s, PID: %d, Execution Time: %ld, Wait Time: %ld\n",
               process->cmd, process->pid, process->execution_time, process->wait_time);
    }
    stop_scheduler = 1;
    exit(0);
}

void save_history_to_file()
{
    FILE *file = fopen(HISTORY_FILE, "w");
    if (file == NULL)
    {
        perror("Failed to open history file");
        return;
    }

    for (int i = 0; i < history_count; i++)
    {
        for (int j = 0; j < MAX_LINE; j++)
        {
            fprintf(file, "%s ", history[i].cmd[j]);
        }
        fprintf(file, "\n");
        fprintf(file, "PID: %d\n", history[i].pid);
        fprintf(file, "Start Time: %s", asctime(localtime(&history[i].start.tv_sec)));
        fprintf(file, "Duration: %ld milliseconds\n", history[i].duration);
        fprintf(file, "\n");
    }

    fclose(file);
}

void execute_command(char **args)
{
    pid_t pid;
    int status;
    struct timeval start, end;

    if (strcmp(args[0], "cd") == 0)
    {
        if (chdir(args[1]) < 0)
        {
            perror("chdir");
        }
        // else add the cd command in history
        gettimeofday(&start, NULL);
        gettimeofday(&end, NULL);
        long duration = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
        add_to_history(args, pid, start, duration);
        return;
    }

    if (strcmp(args[0], "submit") == 0)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            // Child process
            execvp(args[1], args + 1);
            perror("Execution Error");
            exit(EXIT_FAILURE);
        }
        else if (pid < 0)
        {
            // Fork failed
            perror("fork process failed");
        }
        else
        {
            // Parent process
            // Add the process to the scheduler
            add_process(pid, args[1]);
            kill(pid, SIGSTOP);
        }
        return;
    }

    // execute single command
    gettimeofday(&start, NULL);
    pid = fork();
    if (pid == 0)
    {
        execvp(args[0], args);
        perror("Execution Error");
        exit(EXIT_FAILURE);
    }
    else if (pid < 0)
    {
        perror("fork process failed");
    }
    else
    {
        waitpid(pid, &status, 0);
        gettimeofday(&end, NULL);
        long duration = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
        add_to_history(args, pid, start, duration);
    }
}

void parse_command(char *cmd, char **args)
{
    char *token;
    int i = 0;

    token = strtok(cmd, " \t\n");
    while (token != NULL)
    {
        args[i++] = token;
        token = strtok(NULL, " \t\n");
    }
    args[i] = NULL;
}

//----------------------------------------------------------------------------------------------------
//------------------------------------SCHEDULER-------------------------------------------------------
//----------------------------------------------------------------------------------------------------

void add_process(pid_t pid, char *cmd)
{
    Process process;
    process.pid = pid;
    process.cmd = strdup(cmd);
    process.execution_time = 0;
    process.wait_time = 0;

    enqueue(&ready_queue, process);
}

void *scheduler(void *arg)
{
    while (!stop_scheduler)
    {
        // Acquire the mutex
        pthread_mutex_lock(&mutex);
        // Continue NCPU tasks by sending SIGCONT signal
        for (int i = 0; i < NCPU && i < ready_queue.size; i++)
        {
            Process process = dequeue(&ready_queue);
            // Get the start time
            struct timeval start;
            gettimeofday(&start, NULL);
            kill(process.pid, SIGCONT);
            // Get the end time
            struct timeval end;
            gettimeofday(&end, NULL);
            // Calculate the duration
            long duration = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;

            // Check if the process completed within the time slice
            if (duration <= TSLICE)
            {
                // Update the execution time
                process.execution_time += duration;
                // add_to_history(&process.cmd,process.pid,start,process.execution_time);
                // enqueue(&completed_queue, process);
            }
            else
            {
                // Update the execution time and enqueue the process back to the ready queue
                process.execution_time += TSLICE;
                enqueue(&ready_queue, process);
            }
            //Update the wait time for all processes in the ready queue
            for (int i = 0; i < ready_queue.size; i++)
            {
                Process *p = &ready_queue.processes[i];
                if (p != &process)
                {
                    p->wait_time += duration;
                }
            }        
            enqueue(&ready_queue, process);
        }
        // Release the mutex
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s NCPU TSLICE\n", argv[0]);
        return 1;
    }

    NCPU = atoi(argv[1]);
    TSLICE = atoi(argv[2]);

    pthread_t scheduler_thread;
    if (pthread_mutex_init(&mutex, NULL) != 0)
    {
        perror("Mutex initialization failed");
        return 1;
    }

    init_queue(&ready_queue);
    init_queue(&completed_queue);

    // Create a thread for the scheduler
    if (pthread_create(&scheduler_thread, NULL, scheduler, NULL) != 0)
    {
        perror("Failed to create the scheduler thread");
        return 1;
    }
    
    // Register signal handler for SIGINT
    signal(SIGINT, handle_sigint);
    char cmd[MAX_LINE];
    char *args[MAX_LINE / 2 + 1];
    int should_run = 1;
    do
    {
        printf("SimpleShell> ");
        fgets(cmd, MAX_LINE, stdin);
        cmd[strlen(cmd) - 1] = '\0'; // remove newline at the end

        if (strcmp(cmd, "") == 0)
        {
            continue; // Skip further execution if command is empty
        }

        if (strcmp(cmd, "exit") == 0)
        {
            should_run = 0;
            continue;
        }

        if (strcmp(cmd, "history") == 0)
        {
            for (int i = 0; i < history_count; i++)
            {
                printf("%d  ", i + 1);
                for (int j = 0; j < MAX_LINE; j++)
                {
                    printf("%s ", history[i].cmd[j]);
                }
                printf("\n");
            }
            continue;
        }

        parse_command(cmd, args);

        execute_command(args);

        // Save history to file
        save_history_to_file();

    } while (should_run);

    if (pthread_join(scheduler_thread, NULL) != 0)
    {
        perror("Failed to join the scheduler thread");
        return 1;
    }

    // Print information about all jobs
    for (int i = 0; i < completed_queue.size; i++)
    {
        Process *process = &completed_queue.processes[i];
        printf("Name: %s, PID: %d, Execution Time: %ld, Wait Time: %ld\n",
               process->cmd, process->pid, process->execution_time, process->wait_time);
    }
    return 0;
}