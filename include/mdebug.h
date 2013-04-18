#include "mruby.h"
#include "mruby/irep.h"

struct debug_fileinfo {
  const char *filename;
  unsigned int lineno;
};

typedef void (*mdebug_tracer_func_t)(mrb_state *mrb, mrb_irep *irep, mrb_code *pc, mrb_value *regs, int codeidx, int lineno);

#define DUMP_VM(mrb) \
    do { \
      struct debug_fileinfo info = { __FILE__, __LINE__ }; \
      if ((mrb)->code_fetch_hook) \
        (mrb)->code_fetch_hook(mrb, NULL, (void *)&info, NULL); \
    } while(0)

void mdebug_dump_vm(mrb_state *mrb);
void mdebug_set_break_file(mrb_state *mrb, const char *filename, int lineno);
void mdebug_set_tracer(mdebug_tracer_func_t func);
