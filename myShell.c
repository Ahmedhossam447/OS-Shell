#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include "vector.h"

#define MAX_INPUT_SIZE 1024
#define MAX_STAGES 16
#define TOKEN_DELIMITERS " \t\r\n"

typedef struct {
    char *input_file;
    char *output_file;
} Redirection;

static void sh_errno(const char *ctx) {
    fprintf(stderr, "sadafa: %s: %s\n", ctx, strerror(errno));
}

static void sh_error(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "sadafa: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static void on_sigint(int sig) {
    (void)sig;
    write(STDOUT_FILENO, "\n", 1);
    write(STDOUT_FILENO, "sadafa> ", 8);
}

static void setup_shell_signals(void) {
    struct sigaction sa_int;
    sa_int.sa_handler = on_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa_int, NULL);

    struct sigaction sa_ign;
    sa_ign.sa_handler = SIG_IGN;
    sigemptyset(&sa_ign.sa_mask);
    sa_ign.sa_flags = 0;
    sigaction(SIGTSTP, &sa_ign, NULL);
}

static void reset_foreground_child_signals(void) {
    struct sigaction sa_dfl;
    sa_dfl.sa_handler = SIG_DFL;
    sigemptyset(&sa_dfl.sa_mask);
    sa_dfl.sa_flags = 0;
    sigaction(SIGINT, &sa_dfl, NULL);
}

static void print_prompt(void) {
    printf("sadafa> ");
    fflush(stdout);
}

static char *read_input(char *buffer, size_t size) {
    if (fgets(buffer, (int)size, stdin) == NULL) {
        return NULL;
    }

    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        return buffer;
    }

    sh_error("input too long (max %zu chars); line discarded", size - 1);
    int c;
    while ((c = getchar()) != '\n' && c != EOF) { }
    buffer[0] = '\0';
    return buffer;
}

static void tokenize(char *input, Vector *tokens) {
    vector_clear(tokens);

    char *token = strtok(input, TOKEN_DELIMITERS);
    while (token != NULL) {
        vector_push(tokens, token);
        token = strtok(NULL, TOKEN_DELIMITERS);
    }
}

static int builtin_cd(char **argv) {
    const char *target;

    if (argv[1] == NULL) {
        target = getenv("HOME");
        if (target == NULL) {
            sh_error("cd: HOME not set");
            return 1;
        }
    } else {
        target = argv[1];
    }

    if (chdir(target) != 0) {
        sh_errno("cd");
        return 1;
    }
    return 0;
}

static int builtin_pwd(void) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        sh_errno("pwd");
        return 1;
    }
    printf("%s\n", cwd);
    return 0;
}

static int builtin_echo(char **argv) {
    for (int i = 1; argv[i] != NULL; i++) {
        if (i > 1) printf(" ");
        printf("%s", argv[i]);
    }
    printf("\n");
    return 0;
}

static int builtin_history(Vector *history) {
    for (int i = 0; i < history->size; i++) {
        printf("%5d  %s\n", i + 1, history->data[i]);
    }
    return 0;
}

typedef int (*BuiltinFn)(char **argv, Vector *history);

typedef struct {
    const char *name;
    BuiltinFn fn;
} BuiltinCommand;

static int builtin_cd_cmd(char **argv, Vector *history) {
    (void)history;
    return builtin_cd(argv);
}

static int builtin_pwd_cmd(char **argv, Vector *history) {
    (void)argv;
    (void)history;
    return builtin_pwd();
}

static int builtin_echo_cmd(char **argv, Vector *history) {
    (void)history;
    return builtin_echo(argv);
}

static int builtin_history_cmd(char **argv, Vector *history) {
    (void)argv;
    return builtin_history(history);
}

static int parse_redirection(Vector *tokens, Redirection *redir) {
    redir->input_file = NULL;
    redir->output_file = NULL;

    int write_idx = 0;
    for (int i = 0; i < tokens->size; i++) {
        char *tok = tokens->data[i];

        if (strcmp(tok, "<") == 0) {
            if (i + 1 >= tokens->size) {
                sh_error("syntax error near '<'");
                return -1;
            }
            redir->input_file = tokens->data[++i];
        } else if (strcmp(tok, ">") == 0) {
            if (i + 1 >= tokens->size) {
                sh_error("syntax error near '>'");
                return -1;
            }
            redir->output_file = tokens->data[++i];
        } else {
            tokens->data[write_idx++] = tok;
        }
    }
    tokens->size = write_idx;
    return 0;
}

static int apply_redirection(Redirection *redir) {
    if (redir->input_file) {
        int fd = open(redir->input_file, O_RDONLY);
        if (fd < 0) {
            sh_errno(redir->input_file);
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            sh_errno("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    if (redir->output_file) {
        int fd = open(redir->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            sh_errno(redir->output_file);
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            sh_errno("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

static int run_builtin_with_redirection(BuiltinFn builtin_fn, char **argv, Vector *history, Redirection *redir) {
    int saved_stdin = -1;
    int saved_stdout = -1;
    int rc;

    if (redir->input_file || redir->output_file) {
        saved_stdin = dup(STDIN_FILENO);
        if (saved_stdin < 0) {
            sh_errno("dup");
            return -1;
        }
        saved_stdout = dup(STDOUT_FILENO);
        if (saved_stdout < 0) {
            sh_errno("dup");
            close(saved_stdin);
            return -1;
        }
        if (apply_redirection(redir) < 0) {
            dup2(saved_stdin, STDIN_FILENO);
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdin);
            close(saved_stdout);
            return -1;
        }
    }

    rc = builtin_fn(argv, history);
    fflush(stdout);

    if (saved_stdin >= 0) {
        if (dup2(saved_stdin, STDIN_FILENO) < 0) {
            sh_errno("dup2");
            rc = -1;
        }
        close(saved_stdin);
    }
    if (saved_stdout >= 0) {
        if (dup2(saved_stdout, STDOUT_FILENO) < 0) {
            sh_errno("dup2");
            rc = -1;
        }
        close(saved_stdout);
    }

    return rc;
}

static char *find_in_path(const char *command) {
    if (strchr(command, '/') != NULL) {
        if (access(command, X_OK) == 0) {
            char *copy = strdup(command);
            if (copy == NULL) sh_errno("strdup");
            return copy;
        }
        return NULL;
    }

    char *path_env = getenv("PATH");
    if (path_env == NULL) {
        return NULL;
    }

    char *path_copy = strdup(path_env);
    if (path_copy == NULL) {
        sh_errno("strdup");
        return NULL;
    }

    char *dir = strtok(path_copy, ":");
    while (dir != NULL) {
        char candidate[1024];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, command);

        if (access(candidate, X_OK) == 0) {
            char *result = strdup(candidate);
            if (result == NULL) sh_errno("strdup");
            free(path_copy);
            return result;
        }
        dir = strtok(NULL, ":");
    }

    free(path_copy);
    return NULL;
}

static int split_pipeline(Vector *tokens, Vector stages[], int max_stages) {
    int count = 0;
    vector_init(&stages[0]);

    for (int i = 0; i < tokens->size; i++) {
        if (strcmp(tokens->data[i], "|") == 0) {
            if (stages[count].size == 0) {
                sh_error("syntax error near '|'");
                for (int j = 0; j <= count; j++) vector_free(&stages[j]);
                return -1;
            }
            count++;
            if (count >= max_stages) {
                sh_error("too many pipe stages (max %d)", max_stages);
                for (int j = 0; j < count; j++) vector_free(&stages[j]);
                return -1;
            }
            vector_init(&stages[count]);
        } else {
            vector_push(&stages[count], tokens->data[i]);
        }
    }

    if (stages[count].size == 0) {
        sh_error("syntax error near '|'");
        for (int j = 0; j <= count; j++) vector_free(&stages[j]);
        return -1;
    }

    return count + 1;
}

static void run_child_command(Vector *stage, Vector *history) {
    char **argv = vector_to_argv(stage);

    if (strcmp(argv[0], "cd") == 0)      exit(0);
    if (strcmp(argv[0], "exit") == 0)    exit(0);
    if (strcmp(argv[0], "pwd") == 0)     exit(builtin_pwd());
    if (strcmp(argv[0], "echo") == 0)    exit(builtin_echo(argv));
    if (strcmp(argv[0], "history") == 0) exit(builtin_history(history));

    char *path = find_in_path(argv[0]);
    if (path == NULL) {
        sh_error("%s: command not found", argv[0]);
        exit(127);
    }
    execv(path, argv);
    sh_errno(argv[0]);
    free(path);
    exit(127);
}

static int execute_pipeline(Vector stages[], int num_stages, Redirection *redir,
                     int background, Vector *history) {
    int prev_read = -1;
    pid_t pids[MAX_STAGES];

    for (int i = 0; i < num_stages; i++) {
        int fds[2] = {-1, -1};
        int is_last = (i == num_stages - 1);

        if (!is_last) {
            if (pipe(fds) < 0) {
                sh_errno("pipe");
                if (prev_read != -1) close(prev_read);
                return -1;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            sh_errno("fork");
            if (prev_read != -1) close(prev_read);
            if (!is_last) { close(fds[0]); close(fds[1]); }
            return -1;
        }

        if (pid == 0) {
            if (!background) {
                reset_foreground_child_signals();
            }

            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }
            if (!is_last) {
                dup2(fds[1], STDOUT_FILENO);
                close(fds[0]);
                close(fds[1]);
            }

            Redirection stage_redir = { NULL, NULL };
            if (i == 0)    stage_redir.input_file  = redir->input_file;
            if (is_last)   stage_redir.output_file = redir->output_file;
            if (apply_redirection(&stage_redir) < 0) {
                exit(1);
            }

            run_child_command(&stages[i], history);
        }

        pids[i] = pid;

        if (prev_read != -1) close(prev_read);
        if (!is_last) {
            close(fds[1]);
            prev_read = fds[0];
        }
    }

    if (background) {
        printf("[bg] %d\n", pids[num_stages - 1]);
        return 0;
    }

    for (int i = 0; i < num_stages; i++) {
        waitpid(pids[i], NULL, 0);
    }
    return 0;
}

static void reap_background_jobs(void) {
    pid_t done;
    int status;
    while ((done = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            printf("[done] %d exit=%d\n", done, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("[done] %d killed by signal %d\n", done, WTERMSIG(status));
        }
    }
}

static void builtin_exit(Vector *tokens, Vector *history) {
    int code = 0;
    if (tokens->size > 1) {
        code = atoi(tokens->data[1]);
    }

    for (int i = 0; i < history->size; i++) {
        free(history->data[i]);
    }
    vector_free(history);
    vector_free(tokens);
    exit(code);
}

static int execute_command(Vector *tokens, Vector *history) {
    static const BuiltinCommand builtins[] = {
        {"cd", builtin_cd_cmd},
        {"pwd", builtin_pwd_cmd},
        {"echo", builtin_echo_cmd},
        {"history", builtin_history_cmd},
        {NULL, NULL}
    };

    if (tokens->size == 0) {
        return 0;
    }

    if (strcmp(tokens->data[0], "exit") == 0) {
        builtin_exit(tokens, history);
    }

    int background = 0;
    if (strcmp(tokens->data[tokens->size - 1], "&") == 0) {
        background = 1;
        tokens->size--;
        if (tokens->size == 0) {
            return 0;
        }
    }

    Redirection redir;
    if (parse_redirection(tokens, &redir) < 0) {
        return -1;
    }

    if (tokens->size == 0) {
        sh_error("syntax error: no command");
        return -1;
    }

    int has_pipe = 0;
    for (int i = 0; i < tokens->size; i++) {
        if (strcmp(tokens->data[i], "|") == 0) {
            has_pipe = 1;
            break;
        }
    }

    if (has_pipe) {
        Vector stages[MAX_STAGES];
        int num_stages = split_pipeline(tokens, stages, MAX_STAGES);
        if (num_stages < 0) {
            return -1;
        }
        int rc = execute_pipeline(stages, num_stages, &redir, background, history);
        for (int i = 0; i < num_stages; i++) vector_free(&stages[i]);
        return rc;
    }

    char **argv = vector_to_argv(tokens);

    for (int i = 0; builtins[i].name != NULL; i++) {
        if (strcmp(argv[0], builtins[i].name) == 0) {
            return run_builtin_with_redirection(builtins[i].fn, argv, history, &redir);
        }
    }

    pid_t pid = fork();

    if (pid < 0) {
        sh_errno("fork");
        return -1;
    }

    if (pid == 0) {
        if (!background) {
            reset_foreground_child_signals();
        }

        if (apply_redirection(&redir) < 0) {
            exit(1);
        }

        char *path = find_in_path(argv[0]);
        if (path == NULL) {
            sh_error("%s: command not found", argv[0]);
            exit(127);
        }

        execv(path, argv);
        sh_errno(argv[0]);
        free(path);
        exit(127);
    }

    if (background) {
        printf("[bg] %d\n", pid);
        return 0;
    }

    int status;
    if (waitpid(pid, &status, 0) < 0) {
        sh_errno("waitpid");
        return -1;
    }

    return 0;
}

int main(void) {
    char input[MAX_INPUT_SIZE];
    Vector tokens;
    Vector history;

    setup_shell_signals();

    vector_init(&tokens);
    vector_init(&history);

    while (1) {
        reap_background_jobs();
        print_prompt();

        if (read_input(input, sizeof(input)) == NULL) {
            printf("\n");
            break;
        }

        if (input[0] == '\0') {
            continue;
        }

        char *history_entry = strdup(input);
        if (history_entry == NULL) {
            sh_errno("strdup");
        } else {
            vector_push(&history, history_entry);
        }

        tokenize(input, &tokens);

        if (tokens.size == 0) {
            continue;
        }

        execute_command(&tokens, &history);
    }

    for (int i = 0; i < history.size; i++) {
        free(history.data[i]);
    }
    vector_free(&history);
    vector_free(&tokens);
    return 0;
}
