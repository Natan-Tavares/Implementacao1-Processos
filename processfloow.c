#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>
#include <stdarg.h>

#define MAX_TASKS   256
#define MAX_JOBS    256
#define MAX_ARGS    64
#define MAX_LINE    4096
#define MAX_NAME    256
#define MAX_GROUP   64

typedef struct {
    char  name[MAX_NAME];
    char *argv[MAX_ARGS + 1];
    int   argc;
    char  input_file[MAX_NAME];
    char  output_file[MAX_NAME];
    int   has_input;
    int   has_output;
    int   append_output;
    int   used;
} Task;

typedef enum { JOB_RUNNING, JOB_DONE } JobStatus;

typedef struct {
    int       id;
    pid_t     pid;
    char      taskname[MAX_NAME];
    JobStatus status;
    int       exit_code;
    int       signaled;
} Job;

static Task tasks[MAX_TASKS];
static int  num_tasks = 0;

static Job jobs[MAX_JOBS];
static int num_jobs = 0;
static int next_job_id = 1;

static void err_msg(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "processflow: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static Task *find_task(const char *name) {
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].used && strcmp(tasks[i].name, name) == 0) {
            return &tasks[i];
        }
    }
    return NULL;
}

static Task *get_or_create_task_slot(const char *name) {
    Task *t = find_task(name);
    if (t) {
        for (int i = 0; i < t->argc; i++) {
            free(t->argv[i]);
            t->argv[i] = NULL;
        }
        t->argc = 0;
        t->has_input = 0;
        t->has_output = 0;
        t->append_output = 0;
        return t;
    }
    if (num_tasks >= MAX_TASKS) return NULL;
    t = &tasks[num_tasks++];
    memset(t, 0, sizeof(*t));
    t->used = 1;
    strncpy(t->name, name, MAX_NAME - 1);
    return t;
}

static int tokenize(char *line, char *tokens[], int max_tokens) {
    int n = 0;
    char *p = strtok(line, " \t\r\n");
    while (p != NULL && n < max_tokens) {
        tokens[n++] = p;
        p = strtok(NULL, " \t\r\n");
    }
    return n;
}

static void child_run(Task *t, int in_fd, int out_fd) {
    if (in_fd != -1) {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    } else if (t->has_input) {
        int fd = open(t->input_file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "processflow: nao foi possivel abrir o arquivo de entrada '%s' (tarefa '%s'): %s\n",
                    t->input_file, t->name, strerror(errno));
            _exit(1);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (out_fd != -1) {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    } else if (t->has_output) {
        int flags = O_WRONLY | O_CREAT | (t->append_output ? O_APPEND : O_TRUNC);
        int fd = open(t->output_file, flags, 0644);
        if (fd < 0) {
            fprintf(stderr, "processflow: nao foi possivel abrir o arquivo de saida '%s' (tarefa '%s'): %s\n",
                    t->output_file, t->name, strerror(errno));
            _exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    execvp(t->argv[0], t->argv);
    fprintf(stderr, "processflow: nao foi possivel executar '%s' (tarefa '%s'): %s\n",
            t->argv[0], t->name, strerror(errno));
    _exit(127);
}

static void report_status(const char *taskname, int status) {
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code != 0) {
            err_msg("tarefa '%s' terminou com codigo de saida %d", taskname, code);
        }
    } else if (WIFSIGNALED(status)) {
        err_msg("tarefa '%s' foi encerrada pelo sinal %d", taskname, WTERMSIG(status));
    }
}

static pid_t spawn_task(Task *t, int in_fd, int out_fd) {
    pid_t pid = fork();
    if (pid < 0) {
        err_msg("falha ao criar processo para a tarefa '%s': %s", t->name, strerror(errno));
        return -1;
    }
    if (pid == 0) {
        child_run(t, in_fd, out_fd);
        _exit(127);
    }
    return pid;
}

static void run_single(const char *name) {
    Task *t = find_task(name);
    if (!t) {
        err_msg("tarefa '%s' nao encontrada", name);
        return;
    }
    pid_t pid = spawn_task(t, -1, -1);
    if (pid < 0) return;
    int status;
    waitpid(pid, &status, 0);
    report_status(t->name, status);
}

static void run_sequential(char *names[], int n) {
    for (int i = 0; i < n; i++) {
        Task *t = find_task(names[i]);
        if (!t) {
            err_msg("tarefa '%s' nao encontrada", names[i]);
            continue;
        }
        pid_t pid = spawn_task(t, -1, -1);
        if (pid < 0) continue;
        int status;
        waitpid(pid, &status, 0);
        report_status(t->name, status);
    }
}

static void run_parallel(char *names[], int n) {
    pid_t pids[MAX_GROUP];
    char  taskname[MAX_GROUP][MAX_NAME];
    int   count = 0;

    for (int i = 0; i < n; i++) {
        Task *t = find_task(names[i]);
        if (!t) {
            err_msg("tarefa '%s' nao encontrada", names[i]);
            continue;
        }
        pid_t pid = spawn_task(t, -1, -1);
        if (pid < 0) continue;
        pids[count] = pid;
        strncpy(taskname[count], t->name, MAX_NAME - 1);
        taskname[count][MAX_NAME - 1] = '\0';
        count++;
    }

    for (int i = 0; i < count; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        report_status(taskname[i], status);
    }
}

static void run_pipe(char *names[], int n) {
    if (n < 1) {
        err_msg("run pipe requer ao menos uma tarefa");
        return;
    }

    pid_t pids[MAX_GROUP];
    char  taskname[MAX_GROUP][MAX_NAME];
    int   count = 0;
    int   prev_read_fd = -1;

    for (int i = 0; i < n; i++) {
        Task *t = find_task(names[i]);
        if (!t) {
            err_msg("tarefa '%s' nao encontrada; pipe interrompido", names[i]);
            if (prev_read_fd != -1) close(prev_read_fd);
            break;
        }

        int pipefd[2] = {-1, -1};
        int need_pipe = (i < n - 1);
        if (need_pipe && pipe(pipefd) < 0) {
            err_msg("falha ao criar pipe: %s", strerror(errno));
            if (prev_read_fd != -1) close(prev_read_fd);
            break;
        }

        pid_t pid = fork();
        if (pid < 0) {
            err_msg("falha ao criar processo para a tarefa '%s': %s", t->name, strerror(errno));
            if (need_pipe) { close(pipefd[0]); close(pipefd[1]); }
            if (prev_read_fd != -1) close(prev_read_fd);
            break;
        }

        if (pid == 0) {
            int in_fd  = (i == 0) ? -1 : prev_read_fd;
            int out_fd = need_pipe ? pipefd[1] : -1;
            if (need_pipe) close(pipefd[0]);
            child_run(t, in_fd, out_fd);
            _exit(127);
        }

        if (prev_read_fd != -1) close(prev_read_fd);
        if (need_pipe) {
            close(pipefd[1]);
            prev_read_fd = pipefd[0];
        } else {
            prev_read_fd = -1;
        }

        pids[count] = pid;
        strncpy(taskname[count], t->name, MAX_NAME - 1);
        taskname[count][MAX_NAME - 1] = '\0';
        count++;
    }

    for (int i = 0; i < count; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        report_status(taskname[i], status);
    }
}

static void poll_jobs(void) {
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].status == JOB_RUNNING) {
            int status;
            pid_t r = waitpid(jobs[i].pid, &status, WNOHANG);
            if (r == jobs[i].pid) {
                jobs[i].status = JOB_DONE;
                if (WIFEXITED(status)) {
                    jobs[i].exit_code = WEXITSTATUS(status);
                    jobs[i].signaled = 0;
                } else if (WIFSIGNALED(status)) {
                    jobs[i].exit_code = WTERMSIG(status);
                    jobs[i].signaled = 1;
                }
            }
        }
    }
}

static void cmd_start(const char *name) {
    Task *t = find_task(name);
    if (!t) {
        err_msg("tarefa '%s' nao encontrada", name);
        return;
    }
    if (num_jobs >= MAX_JOBS) {
        err_msg("numero maximo de jobs atingido");
        return;
    }
    pid_t pid = spawn_task(t, -1, -1);
    if (pid < 0) return;

    Job *j = &jobs[num_jobs++];
    j->id = next_job_id++;
    j->pid = pid;
    strncpy(j->taskname, t->name, MAX_NAME - 1);
    j->taskname[MAX_NAME - 1] = '\0';
    j->status = JOB_RUNNING;
    j->exit_code = 0;
    j->signaled = 0;

    printf("[%d] %d\n", j->id, (int) pid);
}

static void cmd_jobs(void) {
    poll_jobs();
    for (int i = 0; i < num_jobs; i++) {
        Job *j = &jobs[i];
        if (j->status == JOB_RUNNING) {
            printf("[%d] %d  Executando   %s\n", j->id, (int) j->pid, j->taskname);
        } else if (j->signaled) {
            printf("[%d] %d  Encerrado(sinal %d) %s\n", j->id, (int) j->pid, j->exit_code, j->taskname);
        } else {
            printf("[%d] %d  Concluido(%d) %s\n", j->id, (int) j->pid, j->exit_code, j->taskname);
        }
    }
}

static Job *find_job(int id) {
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].id == id) return &jobs[i];
    }
    return NULL;
}

static void cmd_wait(const char *idstr) {
    char *end;
    long id = strtol(idstr, &end, 10);
    if (*end != '\0') {
        err_msg("job invalido '%s'", idstr);
        return;
    }
    Job *j = find_job((int) id);
    if (!j) {
        err_msg("job %ld nao encontrado", id);
        return;
    }
    if (j->status == JOB_RUNNING) {
        int status;
        waitpid(j->pid, &status, 0);
        j->status = JOB_DONE;
        if (WIFEXITED(status)) {
            j->exit_code = WEXITSTATUS(status);
            j->signaled = 0;
        } else if (WIFSIGNALED(status)) {
            j->exit_code = WTERMSIG(status);
            j->signaled = 1;
        }
    }
    if (j->signaled) {
        printf("[%d] %s encerrado pelo sinal %d\n", j->id, j->taskname, j->exit_code);
    } else {
        printf("[%d] %s concluido (codigo %d)\n", j->id, j->taskname, j->exit_code);
    }
}

static void reap_all_jobs(void) {
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].status == JOB_RUNNING) {
            int status;
            waitpid(jobs[i].pid, &status, 0);
            jobs[i].status = JOB_DONE;
        }
    }
}


static void cmd_task(char *tokens[], int n) {
    if (n < 3) {
        err_msg("uso: task <nome> <programa> [argumentos...]");
        return;
    }
    const char *name = tokens[1];
    Task *t = get_or_create_task_slot(name);
    if (!t) {
        err_msg("numero maximo de tarefas atingido");
        return;
    }
    int argc = 0;
    for (int i = 2; i < n && argc < MAX_ARGS; i++) {
        t->argv[argc++] = strdup(tokens[i]);
    }
    t->argv[argc] = NULL;
    t->argc = argc;
}

static void cmd_input(char *tokens[], int n) {
    if (n != 3) {
        err_msg("uso: input <tarefa> <arquivo>");
        return;
    }
    Task *t = find_task(tokens[1]);
    if (!t) {
        err_msg("tarefa '%s' nao encontrada", tokens[1]);
        return;
    }
    strncpy(t->input_file, tokens[2], MAX_NAME - 1);
    t->has_input = 1;
}

static void cmd_output(char *tokens[], int n) {
    if (n != 3) {
        err_msg("uso: output <tarefa> <arquivo>");
        return;
    }
    Task *t = find_task(tokens[1]);
    if (!t) {
        err_msg("tarefa '%s' nao encontrada", tokens[1]);
        return;
    }
    strncpy(t->output_file, tokens[2], MAX_NAME - 1);
    t->has_output = 1;
    t->append_output = 0;
}

static void cmd_append(char *tokens[], int n) {
    if (n != 3) {
        err_msg("uso: append <tarefa> <arquivo>");
        return;
    }
    Task *t = find_task(tokens[1]);
    if (!t) {
        err_msg("tarefa '%s' nao encontrada", tokens[1]);
        return;
    }
    strncpy(t->output_file, tokens[2], MAX_NAME - 1);
    t->has_output = 1;
    t->append_output = 1;
}

static void cmd_workdir(char *tokens[], int n) {
    if (n != 2) {
        err_msg("uso: workdir <diretorio>");
        return;
    }
    struct stat st;
    if (stat(tokens[1], &st) != 0 || !S_ISDIR(st.st_mode)) {
        err_msg("diretorio '%s' nao encontrado", tokens[1]);
        return;
    }
    if (chdir(tokens[1]) != 0) {
        err_msg("nao foi possivel mudar para o diretorio '%s': %s", tokens[1], strerror(errno));
    }
}

static void cmd_run(char *tokens[], int n) {
    if (n < 2) {
        err_msg("uso: run <tarefa> | run sequential|parallel|pipe <tarefas...>");
        return;
    }
    if (strcmp(tokens[1], "sequential") == 0) {
        if (n < 3) { err_msg("run sequential requer ao menos uma tarefa"); return; }
        run_sequential(&tokens[2], n - 2);
    } else if (strcmp(tokens[1], "parallel") == 0) {
        if (n < 3) { err_msg("run parallel requer ao menos uma tarefa"); return; }
        run_parallel(&tokens[2], n - 2);
    } else if (strcmp(tokens[1], "pipe") == 0) {
        if (n < 3) { err_msg("run pipe requer ao menos uma tarefa"); return; }
        run_pipe(&tokens[2], n - 2);
    } else {
        if (n != 2) {
            err_msg("uso: run <tarefa>");
            return;
        }
        run_single(tokens[1]);
    }
}

static int process_line(char *line) {
    char linecopy[MAX_LINE];
    strncpy(linecopy, line, MAX_LINE - 1);
    linecopy[MAX_LINE - 1] = '\0';

    char *tokens[MAX_ARGS + 8];
    int n = tokenize(linecopy, tokens, MAX_ARGS + 8);

    if (n == 0) {
        return 1;
    }

    poll_jobs();

    if (strcmp(tokens[0], "exit") == 0) {
        return 0;
    } else if (strcmp(tokens[0], "task") == 0) {
        cmd_task(tokens, n);
    } else if (strcmp(tokens[0], "run") == 0) {
        cmd_run(tokens, n);
    } else if (strcmp(tokens[0], "input") == 0) {
        cmd_input(tokens, n);
    } else if (strcmp(tokens[0], "output") == 0) {
        cmd_output(tokens, n);
    } else if (strcmp(tokens[0], "append") == 0) {
        cmd_append(tokens, n);
    } else if (strcmp(tokens[0], "workdir") == 0) {
        cmd_workdir(tokens, n);
    } else if (strcmp(tokens[0], "start") == 0) {
        if (n != 2) {
            err_msg("uso: start <tarefa>");
        } else {
            cmd_start(tokens[1]);
        }
    } else if (strcmp(tokens[0], "jobs") == 0) {
        cmd_jobs();
    } else if (strcmp(tokens[0], "wait") == 0) {
        if (n != 2) {
            err_msg("uso: wait <jobId>");
        } else {
            cmd_wait(tokens[1]);
        }
    } else {
        err_msg("comando desconhecido: '%s'", tokens[0]);
    }

    return 1;
}

static void run_interactive(void) {
    char line[MAX_LINE];
    int is_tty = isatty(STDIN_FILENO);

    while (1) {
        if (is_tty) {
            printf("processflow> ");
            fflush(stdout);
        }
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }
        if (!process_line(line)) break;
    }
}

static void run_workflow(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        err_msg("nao foi possivel abrir o arquivo de workflow '%s': %s", path, strerror(errno));
        exit(1);
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp) != NULL) {
        fputs(line, stdout);
        if (line[strlen(line) - 1] != '\n') printf("\n");
        fflush(stdout);

        if (!process_line(line)) {
            fclose(fp);
            return;
        }
    }
    fclose(fp);
}

int main(int argc, char *argv[]) {
    if (argc > 2) {
        err_msg("numero incorreto de argumentos. Uso: %s [workflowFile]", argv[0]);
        exit(1);
    }

    if (argc == 2) {
        run_workflow(argv[1]);
    } else {
        run_interactive();
    }

    reap_all_jobs();
    return 0;
}
