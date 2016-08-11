#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Simplifed xv6 shell.

#define MAXARGS 10

// All commands have at least a type. Have looked at the type, the code
// typically casts the *cmd to some specific cmd type.
struct cmd {
  int type; //  ' ' (exec), | (pipe), '<' or '>' for redirection
};

struct execcmd {
  int type;            // ' '
  char *argv[MAXARGS]; // arguments to the command to be exec-ed
};

struct redircmd {
  int type;        // < or >
  struct cmd *cmd; // the command to be run (e.g., an execcmd)
  char *file;      // the input/output file
  int mode;        // the mode to open the file with
  int fd;          // the file descriptor number to use for the file
};

struct pipecmd {
  int type;          // |
  struct cmd *left;  // left side of pipe
  struct cmd *right; // right side of pipe
};

int fork1(void); // Fork but exits on failure.
struct cmd *parsecmd(char *);

// Execute cmd.  Never returns.
void runcmd(struct cmd *cmd) {
  // File descriptor: https://en.wikipedia.org/wiki/File_descriptor
  // man pages:
  // open(2): http://man7.org/linux/man-pages/man2/open.2.html
  // dup(2): http://man7.org/linux/man-pages/man2/dup.2.html
  // pipe(2): http://man7.org/linux/man-pages/man2/pipe.2.html
  int p[2], r;
  struct execcmd *ecmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    exit(0);

  switch (cmd->type) {
  default:
    fprintf(stderr, "unknown runcmd\n");
    exit(-1);

  case ' ':
    ecmd = (struct execcmd *)cmd;
    if (ecmd->argv[0] == 0)
      exit(0);
    // The cmdname at argv[0] itself should be passed into the second parameter
    // of 'execvp()', too.
    execvp(ecmd->argv[0], ecmd->argv);
    break;

  case '>':
  case '<':
    rcmd = (struct redircmd *)cmd;
    // open the file
    // [ToDo] error handling
    int fd = open(rcmd->file, rcmd->mode, S_IRWXU);
    // duplicate the file descriptor to the stdin/stdout so that they can be
    // used interchangeably.
    // dup2 makes the newFd and oldFd points to the same oldFd! Therefore,
    // dup2(a, b) and dup2(b, a) are different in that in the the former case,
    // both a and b point to a; and in the latter a and b point to b!
    dup2(fd, rcmd->fd);
    // run the sub cmd
    runcmd(rcmd->cmd);
    // close the file descriptor
    close(fd);
    break;

  case '|':
    pcmd = (struct pipecmd *)cmd;
    // Your code here ...
    // Create a pair of FDs.
    int pipefd[2];
    pipe(pipefd);

    // File descriptor is process-specific!

    // For a series of cmds conneceted w/ pipe: cmd1 | cmd2 | cmd3 | ... | cmdN
    // The internal cmd_i (i from 2 to N-1) acts both the reader and the writer.
    // However, it shouldn't change its stdout fd to the writing end. This is
    // because of the recursive parsing technique here: cmd_i will be the left
    // cmd and cmd_(i+1) will be the right. Therefore, the stdout of cmd_i will
    // be automatically 'dup'ed to @pipefd[1] in the recursed runcmd() call
    // by this case branch.
    if (fork1() == 0) {
      // Child process is the writer.
      // http://man7.org/linux/man-pages/man2/pipe.2.html
      // Close the unused end of pipe ASAP!
      close(pipefd[0]);
      // So that stdout point to the writing end of pipe now.
      dup2(pipefd[1], STDOUT_FILENO);
      // run the sub cmd
      runcmd(pcmd->left);
      close(pipefd[1]);
    } else {
      // Parent process is the reader.
      close(pipefd[1]);
      // So that stdin point to the reading end of pipe now.
      dup2(pipefd[0], STDIN_FILENO);
      // run the sub cmd
      runcmd(pcmd->right);
      close(pipefd[0]);

      wait(0);
    }

    break;
  }
  exit(0);
}

int getcmd(char *buf, int nbuf) {

  if (isatty(fileno(stdin)))
    fprintf(stdout, "6.828$ ");
  memset(buf, 0, nbuf);
  fgets(buf, nbuf, stdin);
  if (buf[0] == 0) // EOF
    return -1;
  return 0;
}

int main(void) {
  static char buf[100];
  int fd, r;

  // Read and run input commands.
  while (getcmd(buf, sizeof(buf)) >= 0) {
    if (buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' ') {
      // Clumsy but will have to do for now.
      // Chdir has no effect on the parent if run in the child.
      buf[strlen(buf) - 1] = 0; // chop \n
      if (chdir(buf + 3) < 0)
        fprintf(stderr, "cannot cd %s\n", buf + 3);
      continue;
    }
    if (fork1() == 0)
      runcmd(parsecmd(buf));
    wait(&r);
  }
  exit(0);
}

int fork1(void) {
  int pid;

  pid = fork();
  if (pid == -1)
    perror("fork");
  return pid;
}

struct cmd *execcmd(void) {
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = ' ';
  return (struct cmd *)cmd;
}

struct cmd *redircmd(struct cmd *subcmd, char *file, int type) {
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = type;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->mode = (type == '<') ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC;
  cmd->fd = (type == '<') ? 0 : 1;
  return (struct cmd *)cmd;
}

struct cmd *pipecmd(struct cmd *left, struct cmd *right) {
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = '|';
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>";

int gettoken(char **ps, char *es, char **q, char **eq) {
  char *s;
  int ret;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  if (q)
    *q = s;
  ret = *s;
  switch (*s) {
  case 0:
    break;
  case '|':
  case '<':
    s++;
    break;
  case '>':
    s++;
    break;
  default:
    ret = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if (eq)
    *eq = s;

  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int peek(char **ps, char *es, char *toks) {
  char *s;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);

// make a copy of the characters in the input buffer, starting from s through
// es.
// null-terminate the copy to make it a string.
char *mkcopy(char *s, char *es) {
  int n = es - s;
  char *c = malloc(n + 1);
  assert(c);
  strncpy(c, s, n);
  c[n] = 0;
  return c;
}

struct cmd *parsecmd(char *s) {
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if (s != es) {
    fprintf(stderr, "leftovers: %s\n", s);
    exit(-1);
  }
  return cmd;
}

struct cmd *parseline(char **ps, char *es) {
  struct cmd *cmd;
  cmd = parsepipe(ps, es);
  return cmd;
}

struct cmd *parsepipe(char **ps, char *es) {
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if (peek(ps, es, "|")) {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd *parseredirs(struct cmd *cmd, char **ps, char *es) {
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>")) {
    tok = gettoken(ps, es, 0, 0);
    if (gettoken(ps, es, &q, &eq) != 'a') {
      fprintf(stderr, "missing file for redirection\n");
      exit(-1);
    }
    switch (tok) {
    case '<':
      cmd = redircmd(cmd, mkcopy(q, eq), '<');
      break;
    case '>':
      cmd = redircmd(cmd, mkcopy(q, eq), '>');
      break;
    }
  }
  return cmd;
}

struct cmd *parseexec(char **ps, char *es) {
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  ret = execcmd();
  cmd = (struct execcmd *)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while (!peek(ps, es, "|")) {
    if ((tok = gettoken(ps, es, &q, &eq)) == 0)
      break;
    if (tok != 'a') {
      fprintf(stderr, "syntax error\n");
      exit(-1);
    }
    cmd->argv[argc] = mkcopy(q, eq);
    argc++;
    if (argc >= MAXARGS) {
      fprintf(stderr, "too many args\n");
      exit(-1);
    }
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  return ret;
}
