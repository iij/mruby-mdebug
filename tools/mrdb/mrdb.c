#include <stdio.h>
#include <stdlib.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "mruby.h"
#include "mruby/compile.h"
#include "mruby/irep.h"
#include "mdebug.h"

struct vm_state {
  mrb_state *mrb;
  mrb_irep *irep;
  mrb_code *pc;
  mrb_value *regs;
  int codeidx;
  int lineno;
};

#define CMDSIG(func) static int func(struct vm_state *vm, int argc, char **argv)
CMDSIG(cmd_break);
CMDSIG(cmd_cont);
CMDSIG(cmd_help);
CMDSIG(cmd_list);
CMDSIG(cmd_next);
CMDSIG(cmd_vm);

struct {
  const char *str;
  int (*func)(struct vm_state *, int, char **);
} cmdtab[] = {
  { "b",        cmd_break },
  { "break",    cmd_break },
  { "c",        cmd_cont },
  { "cont",     cmd_cont },
  { "h",        cmd_help },
  { "help",     cmd_help },
  { "l",        cmd_list },
  { "list",     cmd_list },
  { "n",        cmd_next },
  { "next",     cmd_next },
  { "vm",       cmd_vm },
  { NULL,       NULL }
};

const char *prompt = "(mrdb)";

const char **lines;
int linemax, linesize;

#define MAXARGS 10


CMDSIG(cmd_help)
{
  printf("MRuby Debugger help\n");
  printf("  b[reak]      set a breakpoint\n");
  printf("  c[ont]       continue\n");
  printf("  h[elp]       show command reference\n");
  printf("  l[ist]       show source code\n");
  printf("  n[ext]       step program\n");
  printf("  vm           show internal structures of VM\n");
  return 0;
}

CMDSIG(cmd_break)
{
  if (argc != 2) {
    printf("usgae: b <lineno>\n");
    return 0;
  }
  mdebug_set_break_file(vm->mrb, vm->irep->filename, (int)strtol(argv[1], NULL, 10));
  return 0;
}

CMDSIG(cmd_cont)
{
  return 1;
}

CMDSIG(cmd_list)
{
  int b, e, i, l;

  if (argc > 2) {
    printf("usgae: l [<lineno>]\n");
    return 0;
  }

  if (argc == 1)
    l = vm->lineno;
  else
    l = (int)strtol(argv[1], NULL, 10);
  b = l - 5;
  if (b < 1) {
    b = 1;
  }
  e = l + 5;
  if (e > linemax) {
    e = linemax;
  }
  printf("[%d, %d] in %s\n", b, e, vm->irep->filename);
  for (i = b; i < e; i++) {
    const char *marker = "  ";
    if (i == vm->lineno)
      marker = "=>";
    printf("%s %2d  %s\n", marker, i, lines[i]);
  }
  return 0;
}

CMDSIG(cmd_next)
{
  return 1;
}

CMDSIG(cmd_vm)
{
  mdebug_dump_vm(vm->mrb);
  return 0;
}

static void
cmdline_parse(char *line, int *argcp, char **argv)
{
  int argc;
  char *cp, *lp;

  lp = line;
  argc = 0;
  while (argc < MAXARGS) {
    cp = strsep(&lp, " ");
    if (cp == NULL)
      break;
    if (*cp == '\0')
      continue;
    argv[argc++] = cp;
  }
  argv[argc] = NULL;
  *argcp = argc;
}

static void
cmd_readline(struct vm_state *vm)
{
  int argc, i, ret;
  char *line, *argv[MAXARGS+1];
  
  do {
    line = readline(prompt);
    if (line == NULL) {
      printf("\nBye.\n");
      exit(0);
    }
    printf("=> %s\n", line);
    ret = 0;
    cmdline_parse(line, &argc, argv);
    if (argc > 0) {
      for (i = 0; cmdtab[i].str != NULL; i++) {
        if (strcmp(argv[0], cmdtab[i].str) == 0) {
          ret = (*cmdtab[i].func)(vm, argc, argv);
          break;
        }
      }
      if (cmdtab[i].str == NULL) {
        printf("unknown command: %s\n", line);
      }
    }
    free(line);
  } while (ret != 1);
}


static void
mrdb_tracer(mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs, int codeidx, int lineno)
{
  struct vm_state vm;
  const char *line;

  if (lineno > 0 && lineno <= linemax) {
    line = lines[lineno];
  } else {
    line = "(line number points to out of file!)";
  }
  printf("%s:%d:%s\n", irep->filename, lineno, line);
  vm.mrb = mrb;
  vm.irep = irep;
  vm.pc = pc;
  vm.regs = regs;
  vm.codeidx = codeidx;
  vm.lineno = lineno;
  cmd_readline(&vm);
}


static void
readfile(FILE *fp)
{
  int n;
  char buf[1000], *p;

  linesize = 16;
  lines = malloc(linesize * sizeof(char *));
  lines[0] = "";
  for(n = 1; fgets(buf, sizeof(buf), fp) != NULL; n++) {
    if (n == linesize) {
      linesize *= 2;
      lines = realloc(lines, linesize * sizeof(char *));
    }
    if ((p = strchr(buf, '\n')) != NULL) {
      *p = '\0';
    }
    lines[n] = strdup(buf);
  }
  linemax = n;
}

void
usage(const char *name)
{
  printf("Usage: %s [switches] programfile\n", name);
  printf("  switches:\n");
  printf("    (none)\n");
}

int
main(int argc, char **argv)
{
  mrb_state *mrb;
  mrbc_context *c;
  mrb_value mdebug;
  FILE *fp;

  if (argc != 2) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  mrb = mrb_open();
  c = mrbc_context_new(mrb);
  mrbc_filename(mrb, c, argv[1]);
  fp = fopen(argv[1], "r");
  if (fp == NULL) {
    printf("%s: Cannot open program file. (%s)\n", argv[0], argv[1]);
    return EXIT_FAILURE;
  }
  readfile(fp);
  fseek(fp, 0L, SEEK_SET);

  mdebug = mrb_obj_value(mrb_class_get(mrb, "MDebug"));
  mrb_funcall(mrb, mdebug, "on", 0);
  mrb_funcall(mrb, mdebug, "break", 2, mrb_fixnum_value(mrb->irep_len), mrb_fixnum_value(0));
  mdebug_set_tracer(mrdb_tracer);

  printf("mruby debugger\n\n");

  mrb_load_file_cxt(mrb, fp, c);
  mrb_close(mrb);
  return EXIT_SUCCESS;
}
