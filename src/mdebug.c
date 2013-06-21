#include <inttypes.h>
#include <stdlib.h>
#include "mruby.h"
#include "mruby/irep.h"
#include "mruby/proc.h"
#include "mruby/string.h"
#include "mruby/value.h"

#include "mdebug.h"

void show_env(mrb_state *mrb, struct REnv *env);
const char *show_mrb_value(mrb_state *mrb, mrb_value val);
void show_regs(mrb_state *, mrb_code *, mrb_value *);
void db_code_fetch_hook(struct mrb_state* mrb, struct mrb_irep *irep, mrb_code *pc, mrb_value *regs);

static void mdebug_default_tracer(struct mrb_state *mrb, struct mrb_irep *irep, mrb_code *pc, mrb_value *regs, int codeidx, int lineno);


struct breakpoint {
  int valid;
  int irep_idx;
  int irep_codeidx;
  const char *filename;
  uint16_t lineno;
};

static struct breakpoint *breakpoints;
int breakpoints_len;
int breakpoints_max;

mrb_irep **pc2irep = NULL;
static mdebug_tracer_func_t tracer;


static void
bp_init(mrb_state *mrb)
{
  breakpoints_len = 100;
  breakpoints = calloc(breakpoints_len, sizeof(struct breakpoint));
  breakpoints_max = -1;
}

static void
bp_add_file(mrb_state *mrb, const char *filename, mrb_int lineno)
{
  int i;
  for (i = 0; i < breakpoints_len; i++) {
    if (!breakpoints[i].valid) {
      breakpoints[i].valid = 1;
      breakpoints[i].irep_idx = -1;
      breakpoints[i].filename = NULL; // XXX
      breakpoints[i].lineno = lineno;
      if (breakpoints_max < i)
        breakpoints_max = i;
      return;
    }
  }
  mrb_raise(mrb, E_RUNTIME_ERROR, "too many breakpoints!");
}

static void
bp_add_irep(mrb_state *mrb, mrb_int irepidx, mrb_int codeidx)
{
  int i;
  for (i = 0; i < breakpoints_len; i++) {
    if (!breakpoints[i].valid) {
      breakpoints[i].valid = 1;
      breakpoints[i].irep_idx = irepidx;
      breakpoints[i].irep_codeidx = codeidx;
      if (breakpoints_max < i)
        breakpoints_max = i;
      return;
    }
  }
  mrb_raise(mrb, E_RUNTIME_ERROR, "too many breakpoints!");
}

static int
bp_find_file(mrb_state *mrb, const char *filename, uint16_t lineno)
{
  struct breakpoint *bp;
  int i;
  for (i = 0; i <= breakpoints_max; i++) {
    bp = &breakpoints[i];
    if (bp->valid && bp->lineno > 0 && bp->lineno == lineno) {
      return 1;
    }
  }
  return 0;
}

static int
bp_find_irep(mrb_state *mrb, mrb_int irepidx, mrb_int codeidx)
{
  struct breakpoint *bp;
  int i;
  for (i = 0; i <= breakpoints_max; i++) {
    bp = &breakpoints[i];
    if (bp->valid && bp->irep_idx == irepidx && bp->irep_codeidx == codeidx) {
      return 1;
    }
  }
  return 0;
}

int
irep_pc_cmp(const void *ap, const void *bp)
{
  const mrb_irep *a = *(const mrb_irep **)ap;
  const mrb_irep *b = *(const mrb_irep **)bp;
  return (int)(a->iseq - b->iseq);
}

static void
debug_build_pc_table(mrb_state *mrb)
{
  int i;

  pc2irep = calloc(mrb->irep_len, sizeof(mrb_irep *));
  for (i = 0; i < mrb->irep_len; i++)
    pc2irep[i] = mrb->irep[i];
  qsort(pc2irep, mrb->irep_len, sizeof(mrb_irep *), irep_pc_cmp);
if (0) {
  for (i = 0; i < mrb->irep_len; i++) {
    mrb_irep *ir = pc2irep[i];
    printf("%03d : iseq=%p\n", ir->idx, ir->iseq);
  }
}
}

mrb_irep *
debug_pc_to_irep(mrb_state *mrb, mrb_code *pc, uint16_t *linep)
{
  int i, l, n, r;

  if (pc2irep == NULL)
    debug_build_pc_table(mrb);

  l = 0;
  r = mrb->irep_len - 1;
  while (l < r) {
    i = (l + r) / 2;
    n = pc - pc2irep[i]->iseq;
    if (n >= 0 && n < pc2irep[i]->ilen) {
      if (linep)
        *linep = pc2irep[i]->lines[n];
      return pc2irep[i];
    }
    if (n < 0)
      r = i - 1;
    else
      l = i + 1;
  }
  return NULL;
}

static void
mdebug_default_tracer(struct mrb_state *mrb, struct mrb_irep *irep, mrb_code *pc, mrb_value *regs, int codeidx, int lineno)
{
  struct mrb_context *c;
  int i;

  printf("=== Breakpoint: irep %d/%03d %s:%u ===\n",
         irep->idx, codeidx, irep->filename, lineno);

  if (regs != mrb->c->stack) {
    printf("!!! regs != mrb->stack (regs=%p) !!!\n", regs);
  }

  mdebug_dump_vm(mrb);

  c = mrb->c;
  if (c->stack < c->stbase || c->stack >= &c->stbase[c->ci->stackidx + c->ci->nregs]) {
    printf("Stack (alternate):\n");
    for (i = irep->nregs; i >= 0; i--) {
      const char *star = (i > 0) ? " " : "*";
      printf(" %s%03d  %s\n", star, i, show_mrb_value(mrb, c->stack[i]));
    }
  }

  printf("\n");
  fflush(stdout);
}

void
mdebug_set_break_file(mrb_state *mrb, const char *filename, int lineno)
{
  bp_add_file(mrb, filename, lineno);
}

void
mdebug_set_tracer(mdebug_tracer_func_t func)
{
  tracer = func;
}

mrb_value
mrb_mdebug_break(mrb_state *mrb, mrb_value cls)
{
  mrb_value a, b;
  mrb_int irepidx, codeidx;

  b = mrb_nil_value();
  mrb_get_args(mrb, "o|o", &a, &b);
  if (mrb_fixnum_p(a) && mrb_nil_p(b)) {
    bp_add_file(mrb, "", mrb_fixnum(a));
  } else if (mrb_fixnum_p(a) && mrb_fixnum_p(b)) {
    irepidx = mrb_fixnum(a);
    codeidx = mrb_fixnum(b);
    bp_add_irep(mrb, irepidx, codeidx);
  } else if (mrb_string_p(a) && mrb_fixnum_p(b)) {
    bp_add_file(mrb, mrb_str_to_cstr(mrb, a), mrb_fixnum(b));
  } else {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "MDebug.break");
  }
  return mrb_nil_value();
}

mrb_value
mrb_mdebug_on(mrb_state *mrb, mrb_value cls)
{
  mrb->code_fetch_hook = db_code_fetch_hook;
  return mrb_nil_value();
}

void
mrb_mruby_mdebug_gem_init(mrb_state *mrb)
{
  struct RClass *m;

  m = mrb_define_module(mrb, "MDebug");
  mrb_define_class_method(mrb, m, "on", mrb_mdebug_on, ARGS_NONE());
  mrb_define_class_method(mrb, m, "b", mrb_mdebug_break, ARGS_REQ(2));
  mrb_define_class_method(mrb, m, "break", mrb_mdebug_break, ARGS_REQ(2));

  mdebug_set_tracer(mdebug_default_tracer);
  bp_init(mrb);
}

void
mrb_mruby_mdebug_gem_final(mrb_state *mrb)
{
}



static int
stack_offset_in_stbase(mrb_state *mrb, mrb_value *sp)
{
  if (sp >= mrb->c->stbase && sp < mrb->c->stend)
    return (int)(sp - mrb->c->stbase);
  else
    return -1;
}

static const char *
tt2str(enum mrb_vtype tt)
{
  static char buf[4];
  const char *s;

  switch (tt) {
  case MRB_TT_FALSE:     s = "FALSE";     break;
  case MRB_TT_FREE:      s = "FREE";      break;
  case MRB_TT_TRUE:      s = "TRUE";      break;
  case MRB_TT_FIXNUM:    s = "FIXNUM";    break;
  case MRB_TT_SYMBOL:    s = "SYMBOL";    break;
  case MRB_TT_UNDEF:     s = "UNDEF";     break;
  case MRB_TT_FLOAT:     s = "FLOAT";     break;
  case MRB_TT_VOIDP:     s = "VOIDP";     break;
  case MRB_TT_OBJECT:    s = "OBJECT";    break;
  case MRB_TT_CLASS:     s = "CLASS";     break;
  case MRB_TT_MODULE:    s = "MODULE";    break;
  case MRB_TT_ICLASS:    s = "ICLASS";    break;
  case MRB_TT_SCLASS:    s = "SCLASS";    break;
  case MRB_TT_PROC:      s = "PROC";      break;
  case MRB_TT_ARRAY:     s = "ARRAY";     break;
  case MRB_TT_HASH:      s = "HASH";      break;
  case MRB_TT_STRING:    s = "STRING";    break;
  case MRB_TT_RANGE:     s = "RANGE";     break;
  case MRB_TT_EXCEPTION: s = "EXCEPTION"; break;
  case MRB_TT_FILE:      s = "FILE";      break;
  case MRB_TT_ENV:       s = "ENV";       break;
  case MRB_TT_DATA:      s = "DATA";      break;
  case MRB_TT_MAXDEFINE: s = "MAXDEFINE"; break;
  default:
    snprintf(buf, sizeof(buf), "%u", (unsigned)tt);
    s = buf;
    break;
  }
  return s;
}

const char *
show_mrb_value(mrb_state *mrb, mrb_value val)
{
  static char buf[80];
  size_t sz;

  switch (mrb_type(val)) {
  case MRB_TT_FALSE:
    snprintf(buf, sizeof(buf), "false");
    break;
  case MRB_TT_TRUE:
    snprintf(buf, sizeof(buf), "true");
    break;
  case MRB_TT_FIXNUM:
    /* 123 */
    snprintf(buf, sizeof(buf), "%d", mrb_fixnum(val));
    break;
  case MRB_TT_SYMBOL:
    /* :hoge */
    snprintf(buf, sizeof(buf), ":%s",
             mrb_sym2name_len(mrb, mrb_symbol(val), &sz));
    break;
  case MRB_TT_STRING:
    /* "hoge" */
    snprintf(buf, sizeof(buf), "\"%.*s\"", RSTRING_LEN(val), RSTRING_PTR(val));
    break;
  case MRB_TT_OBJECT:
    if (mrb_type(mrb->c->stbase[0]) == MRB_TT_OBJECT &&
        mrb_obj_ptr(val) == mrb_obj_ptr(mrb->c->stbase[0])) {
      return "main";
    }
    /* FALLTHRU */
  default:
    snprintf(buf, sizeof(buf), "(%s:%p)", tt2str(val.tt), mrb_obj_ptr(val));
    break;
  }
  return buf;
}

void
show_env(mrb_state *mrb, struct REnv *env)
{
  int i;

  printf("env %p:\n", env);
  printf("  stack:\n");
  for (i = (int)env->flags - 1; i >= 0; i--) {
    printf("    %03d  %s\n", i, show_mrb_value(mrb, env->stack[i]));
  }
  fflush(stdout);
}

void
show_regs(mrb_state *mrb, mrb_code *pc, mrb_value *regs)
{
  mrb_irep *irep = mrb->c->ci->proc->body.irep;

  printf("* ");
  printf("irep %u, pc=%03ld, stack=%p",
        irep->idx, (long)(pc - irep->iseq), mrb->c->stack);
  if (mrb->c->stack != regs)
    printf(", ***stack != regs***");
  printf("\n");

  printf("regs[0]=%s\n", show_mrb_value(mrb, regs[0]));

  fflush(stdout);
}

static const char *
find_code(mrb_state *mrb, mrb_code *pc)
{
  int i, n;
  static char buf[80];

  for (i = 0; i < mrb->irep_len; i++) {
    mrb_irep *irep = mrb->irep[i];
    n = pc - irep->iseq;
    if (n >= 0 && n < irep->ilen) {
      if (irep->lines) {
        snprintf(buf, sizeof(buf), "irep %d/%03d @%s:%d", 
                 irep->idx, n, irep->filename, irep->lines[n]);
      } else {
        snprintf(buf, sizeof(buf), "irep %d/%03d @%s\n",
                 irep->idx, n, irep->filename);
      }
      return buf;
    }
  }
  snprintf(buf, sizeof(buf), "unknown pc: %p", pc);
  return buf;
}


void
mdebug_dump_vm(mrb_state *mrb)
{
  mrb_callinfo *cibase = mrb->c->cibase;
  size_t sz;
  int ciidx, i;

  printf("Callinfo:\n");
  ciidx = (int)(mrb->c->ci - mrb->c->cibase);
  if (ciidx >= 0) {
    for (i = ciidx; i >= 0; i--) {
      const char *mname = mrb_sym2name_len(mrb, cibase[i].mid, &sz);
      printf("  ci[%d]: nregs=%d, ridx=%d, eidx=%d, stackidx=%d, method=%s",
             i, cibase[i].nregs, cibase[i].ridx, cibase[i].eidx,
             cibase[i].stackidx, mname
             );
      if (MRB_PROC_CFUNC_P(cibase[i].proc))
        printf(", (cfunc)");
      else
        printf(", irep %u", cibase[i].proc->body.irep->idx);
      printf("\n");
    }
  } else {
    printf("  (ci index is negative!)\n");
  }

  printf("Stack (base) [* => mrb->stack]:\n");
  for (i = mrb->c->ci->stackidx + mrb->c->ci->nregs; i >= 0; i--) {
    const char *star = " ";
    if (mrb->c->stack == &mrb->c->stbase[i])
      star = "*";
    printf(" %s%03d  %s\n", star, i, show_mrb_value(mrb, mrb->c->stbase[i]));
  }

  printf("Rescue list:\n");
  if (mrb->c->ci->ridx > 0) {
    for (i = mrb->c->ci->ridx - 1; i >= 0; i--) {
      printf("  %s\n", find_code(mrb, mrb->c->rescue[i]));
    }
  } else {
      printf("  (none)\n");
  }

  printf("Ensure list:\n");
  if (mrb->c->ci->eidx > 0) {
    for (i = mrb->c->ci->eidx - 1; i >= 0; i--) {
      mrb_irep *irep = mrb->c->ensure[i]->body.irep;
      printf("  irep %d @%s:%u\n",
             irep->idx, irep->filename, (irep->lines ? irep->lines[0] : 0));
    }
  } else {
      printf("  (none)\n");
  }

  printf("Environment:\n");
  if (mrb->c->ci->env) {
    struct REnv *env;
    for (env = mrb->c->ci->env; env != NULL; env = (struct REnv *)env->c) {
      const char *mname = mrb_sym2name_len(mrb, env->mid, &sz);
      int n = stack_offset_in_stbase(mrb, env->stack);
      if (n >= 0)
        printf("  stack=base[%d], ", n);
      else
        printf("  stack=%p, ", env->stack);
      printf("cioff=%d, method=%s\n", env->cioff, mname);
    }
  } else {
      printf("  (none)\n");
  }

  fflush(stdout);
}

void
db_code_fetch_hook(struct mrb_state *mrb, struct mrb_irep *irep, mrb_code *pc, mrb_value *regs)
{
  uint16_t lineno;
  int dobreak, codeidx;
  static uint16_t prev_lineno = 0;

if (irep == NULL) {
  struct debug_fileinfo *info = (struct debug_fileinfo *)pc;
  printf("=== DUMP_VM: @%s:%u ===\n", info->filename, info->lineno);
  mdebug_dump_vm(mrb);
  return;
}
  if (irep->filename == NULL)
    return;
  if (irep->lines != NULL && pc >= irep->iseq && pc < irep->iseq + irep->ilen) {
    codeidx = pc - irep->iseq;
    lineno = irep->lines[codeidx];
  } else {
    codeidx = 0;
    lineno = 0;
  }

  // break point
  dobreak = 0;
  if (bp_find_irep(mrb, irep->idx, codeidx)) {
    dobreak = 1;
  } else if (lineno > 0 && bp_find_file(mrb, NULL, lineno)) {
    if (lineno != prev_lineno) {
      dobreak = 1;
      prev_lineno = lineno;
    }
  }

  if (dobreak) {
    (*tracer)(mrb, irep, pc, regs, codeidx, lineno);
  }
}
