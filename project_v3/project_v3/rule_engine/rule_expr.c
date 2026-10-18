// rule_engine/rule_expr.c
#include "rule_engine/rule_expr.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

// ===== Operator helpers ======================================================
static inline int op_prec(token_kind_t k) {
  switch (k) {
    case TK_NOT: return 5;
    case TK_MUL: case TK_DIV: case TK_MOD: return 4;
    case TK_PLUS: case TK_MINUS:           return 3;
    case TK_LT: case TK_LE: case TK_GT: case TK_GE:
    case TK_EQ: case TK_NE:                return 2;
    case TK_AND:                           return 1;
    case TK_OR:                            return 0;
    default: return -1;
  }
}

static inline bool is_right_assoc(token_kind_t k) {
  // only NOT is right-associative here
  return (k == TK_NOT);
}

// ===== Tokenizer for expressions ============================================

typedef struct {
  const char *s;
  size_t      i, n;
} lexer_t;

static void lx_init(lexer_t *L, const char *s) {
  L->s = s;
  L->i = 0;
  L->n = strlen(s);
}

static bool lx_peek(lexer_t *L, char *c) {
  if (L->i >= L->n) return false;
  *c = L->s[L->i];
  return true;
}

static bool lx_get(lexer_t *L, char *c) {
  if (L->i >= L->n) return false;
  *c = L->s[L->i++];
  return true;
}

static void lx_skip_ws(lexer_t *L) {
  char c;
  while (lx_peek(L,&c)) {
    if (c==' '||c=='\t'||c=='\r'||c=='\n') { L->i++; continue; }
    break;
  }
}

static bool is_id_start(char c) {
  return (c=='_' || (c>='A'&&c<='Z') || (c>='a'&&c<='z'));
}
static bool is_id_char(char c) {
  return is_id_start(c) || (c>='0'&&c<='9');
}

typedef struct {
  token_kind_t k;
  double num;
  char   id[16];
  int    op;      // for TK_FUNC we store fkind_t; for ops we store operator as token kind
} tok_t;

static tok_t lx_next(lexer_t *L) {
  tok_t t; t.k=TK_END; t.num=0; t.id[0]=0; t.op=0;
  lx_skip_ws(L);
  char c;
  if (!lx_peek(L,&c)) {
    t.k = TK_END; return t;
  }

  // numbers (simple: allow [0-9.])
  if ((c>='0'&&c<='9') || (c=='.')) {
    char buf[32]; int bi=0;
    while (lx_peek(L,&c) && (bi<(int)sizeof(buf)-1) &&
           ((c>='0'&&c<='9')||c=='.')) {
      buf[bi++]=c; L->i++;
    }
    buf[bi]=0;
    t.k = TK_NUM;
    t.num = atof(buf);
    return t;
  }

  // identifiers / function names
  if (is_id_start(c)) {
    char buf[32]; int bi=0;
    while (lx_peek(L,&c) && bi<(int)sizeof(buf)-1 && is_id_char(c)) {
      buf[bi++]=c; L->i++;
    }
    buf[bi]=0;

    // map to function or identifier
    if      (!strcmp(buf,"u8"))    { t.k=TK_FUNC; t.op=FN_u8; }
    else if (!strcmp(buf,"s8"))    { t.k=TK_FUNC; t.op=FN_s8; }
    else if (!strcmp(buf,"u16le")) { t.k=TK_FUNC; t.op=FN_u16le; }
    else if (!strcmp(buf,"s16le")) { t.k=TK_FUNC; t.op=FN_s16le; }
    else if (!strcmp(buf,"u16be")) { t.k=TK_FUNC; t.op=FN_u16be; }
    else if (!strcmp(buf,"s16be")) { t.k=TK_FUNC; t.op=FN_s16be; }
    else if (!strcmp(buf,"abs"))   { t.k=TK_FUNC; t.op=FN_abs; }
    else if (!strcmp(buf,"sqrt"))  { t.k=TK_FUNC; t.op=FN_sqrt; }
    else if (!strcmp(buf,"mag3"))  { t.k=TK_FUNC; t.op=FN_mag3; }
    else {
      t.k=TK_ID;
      strncpy(t.id, buf, sizeof(t.id)-1);
      t.id[sizeof(t.id)-1]=0;
    }
    return t;
  }

  // operators / punctuation
  lx_get(L,&c);
  switch (c) {
    case '+': t.k=TK_PLUS;  t.op=TK_PLUS;  return t;
    case '-': t.k=TK_MINUS; t.op=TK_MINUS; return t;
    case '*': t.k=TK_MUL;   t.op=TK_MUL;   return t;
    case '/': t.k=TK_DIV;   t.op=TK_DIV;   return t;
    case '%': t.k=TK_MOD;   t.op=TK_MOD;   return t;
    case '!':
      if (lx_peek(L,&c) && c=='=') { L->i++; t.k=TK_NE; t.op=TK_NE; }
      else { t.k=TK_NOT; t.op=TK_NOT; }
      return t;
    case '<':
      if (lx_peek(L,&c) && c=='=') { L->i++; t.k=TK_LE; t.op=TK_LE; }
      else { t.k=TK_LT; t.op=TK_LT; }
      return t;
    case '>':
      if (lx_peek(L,&c) && c=='=') { L->i++; t.k=TK_GE; t.op=TK_GE; }
      else { t.k=TK_GT; t.op=TK_GT; }
      return t;
    case '=':
      if (lx_peek(L,&c) && c=='=') { L->i++; t.k=TK_EQ; t.op=TK_EQ; return t; }
      break;
    case '&':
      if (lx_peek(L,&c) && c=='&') { L->i++; t.k=TK_AND; t.op=TK_AND; return t; }
      break;
    case '|':
      if (lx_peek(L,&c) && c=='|') { L->i++; t.k=TK_OR; t.op=TK_OR; return t; }
      break;
    case '(':
      t.k=TK_LP; return t;
    case ')':
      t.k=TK_RP; return t;
    case ',':
      t.k=TK_COMMA; return t;
  }

  // fallback: end
  t.k = TK_END;
  return t;
}

// ===== Shunting-yard to RPN ==================================================

int to_rpn(const char *expr, rpn_t *out, int out_cap) {
  lexer_t L; lx_init(&L, expr);
  tok_t ops[64]; int sp=0; // operator stack
  int on=0;                // output count

  while (1) {
    tok_t t = lx_next(&L);
    if (t.k == TK_END) break;

    if (t.k == TK_NUM) {
      if (on < out_cap) {
        out[on].k = TK_NUM;
        out[on].num = t.num;
        on++;
      } else return -1;
    } else if (t.k == TK_ID) {
      if (on < out_cap) {
        out[on].k = TK_ID;
        strncpy(out[on].id, t.id, sizeof(out[on].id)-1);
        out[on].id[sizeof(out[on].id)-1]=0;
        on++;
      } else return -1;
    } else if (t.k == TK_FUNC) {
      if (sp < (int)(sizeof(ops)/sizeof(ops[0]))) {
        ops[sp++] = t;
      } else return -1;
    } else if (t.k == TK_COMMA) {
      while (sp>0 && ops[sp-1].k != TK_LP) {
        tok_t z = ops[--sp];
        if (on < out_cap) {
          out[on].k = (z.k==TK_FUNC)?TK_FUNC:z.k;
          out[on].op = z.op;
          on++;
        } else return -1;
      }
    } else if (t.k == TK_LP) {
      if (sp < (int)(sizeof(ops)/sizeof(ops[0]))) {
        ops[sp++] = t;
      } else return -1;
    } else if (t.k == TK_RP) {
      while (sp>0 && ops[sp-1].k != TK_LP) {
        tok_t z = ops[--sp];
        if (on < out_cap) {
          out[on].k = (z.k==TK_FUNC)?TK_FUNC:z.k;
          out[on].op = z.op;
          on++;
        } else return -1;
      }
      if (sp>0 && ops[sp-1].k == TK_LP) sp--; // pop '('
      if (sp>0 && ops[sp-1].k == TK_FUNC) {
        tok_t z = ops[--sp];
        if (on < out_cap) {
          out[on].k = TK_FUNC;
          out[on].op = z.op;
          on++;
        } else return -1;
      }
    } else {
      while (sp>0) {
        tok_t z = ops[sp-1];
        if ((z.k==TK_FUNC) || (z.k==TK_LP)) break;
        int pz = op_prec(z.k), pt = op_prec((token_kind_t)t.k);
        if (pz > pt || (pz == pt && !is_right_assoc((token_kind_t)t.k))) {
          sp--;
          if (on < out_cap) {
            out[on].k = z.k;
            out[on].op = z.op;
            on++;
          } else return -1;
        } else break;
      }
      if (sp < (int)(sizeof(ops)/sizeof(ops[0]))) {
        ops[sp++] = t;
      } else return -1;
    }
  }

  while (sp>0) {
    tok_t z = ops[--sp];
    if (z.k == TK_LP || z.k == TK_RP) return -1; // mismatched parens
    if (on < out_cap) {
      out[on].k = (z.k==TK_FUNC)?TK_FUNC:z.k;
      out[on].op = z.op;
      on++;
    } else return -1;
  }
  return on;
}

// ===== Variable lookup & evaluator ===========================================

static bool lookup_var(eval_ctx_t *e, const char *id, double *out) {
  // exact match against current vars (up to vcnt visible)
  for (int i=0; i<e->vcnt; i++) {
    if (strcmp(id, e->vname[i]) == 0) {
      *out = e->vval[i];
      return true;
    }
  }
  // also allow any already-defined var name (defensive)
  // (caller typically keeps vname[] zeroed for unused slots)
  for (int i=0; i<e->vcnt; i++) {
    if (e->vname[i][0] && strcmp(id, e->vname[i]) == 0) {
      *out = e->vval[i];
      return true;
    }
  }
  // support "<name>_prev"
  if (e->have_prev && e->prev_vval) {
    size_t L = strlen(id);
    if (L > 5 && strcmp(id + (L-5), "_prev") == 0) {
      char base[16];
      size_t bl = L - 5;
      if (bl >= sizeof(base)) bl = sizeof(base)-1;
      memcpy(base, id, bl);
      base[bl] = 0;
      for (int i=0; i<e->vcnt; i++) {
        if (e->vname[i][0] && strcmp(base, e->vname[i]) == 0) {
          *out = e->prev_vval[i];
          return true;
        }
      }
    }
  }
  return false;
}

double eval_rpn(eval_ctx_t *e, const rpn_t *code, int n) {
  double st[64]; int sp=0;

  for (int i=0; i<n; i++) {
    rpn_t t = code[i];
    switch (t.k) {
      case TK_NUM: {
        if (sp<63) { st[sp++] = t.num; }
      } break;

      case TK_ID: {
        double v = 0.0;
        (void)lookup_var(e, t.id, &v);
        if (sp<63) { st[sp++] = v; }
      } break;

      case TK_FUNC: {
        switch ((fkind_t)t.op) {
          case FN_u8: {
            int idx = (int)(sp ? st[--sp] : 0);
            double v = (idx>=0 && idx<e->len) ? e->buf[idx] : 0;
            if (sp<63) st[sp++] = v;
          } break;
          case FN_s8: {
            int idx = (int)(sp ? st[--sp] : 0);
            int8_t v = (idx>=0 && idx<e->len) ? (int8_t)e->buf[idx] : 0;
            if (sp<63) st[sp++] = (double)v;
          } break;
          case FN_u16le: {
            int idx = (int)(sp ? st[--sp] : 0);
            double v = (idx>=0 && idx+1<e->len)
                       ? (double)(e->buf[idx] | (e->buf[idx+1]<<8))
                       : 0;
            if (sp<63) st[sp++] = v;
          } break;
          case FN_s16le: {
            int idx = (int)(sp ? st[--sp] : 0);
            int16_t v = 0;
            if (idx>=0 && idx+1<e->len)
              v = (int16_t)(e->buf[idx] | (e->buf[idx+1]<<8));
            if (sp<63) st[sp++] = (double)v;
          } break;
          case FN_u16be: {
            int idx = (int)(sp ? st[--sp] : 0);
            double v = (idx>=0 && idx+1<e->len)
                       ? (double)((e->buf[idx]<<8) | e->buf[idx+1])
                       : 0;
            if (sp<63) st[sp++] = v;
          } break;
          case FN_s16be: {
            int idx = (int)(sp ? st[--sp] : 0);
            int16_t v = 0;
            if (idx>=0 && idx+1<e->len)
              v = (int16_t)((e->buf[idx]<<8) | e->buf[idx+1]);
            if (sp<63) st[sp++] = (double)v;
          } break;
          case FN_abs: {
            double a = (sp ? st[--sp] : 0);
            if (sp<63) st[sp++] = a<0 ? -a : a;
          } break;
          case FN_sqrt: {
            double a = (sp ? st[--sp] : 0);
            if (sp<63) st[sp++] = sqrt(a);
          } break;
          case FN_mag3: {
            double z = (sp ? st[--sp] : 0);
            double y = (sp ? st[--sp] : 0);
            double x = (sp ? st[--sp] : 0);
            if (sp<63) st[sp++] = sqrt(x*x + y*y + z*z);
          } break;
        }
      } break;

      case TK_PLUS:  {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = a+b;
      } break;
      case TK_MINUS: {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = a-b;
      } break;
      case TK_MUL:   {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = a*b;
      } break;
      case TK_DIV:   {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = (b==0)?0:(a/b);
      } break;
      case TK_MOD:   {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        int ib=(int)b;
        if (sp<63) st[sp++] = ib ? (double)((int)a % ib) : 0;
      } break;

      case TK_NOT: {
        double a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = (!a)?1.0:0.0;
      } break;
      case TK_LT: {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = (a<b)?1:0;
      } break;
      case TK_LE: {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = (a<=b)?1:0;
      } break;
      case TK_GT: {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = (a>b)?1:0;
      } break;
      case TK_GE: {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = (a>=b)?1:0;
      } break;
      case TK_EQ: {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = (a==b)?1:0;
      } break;
      case TK_NE: {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = (a!=b)?1:0;
      } break;
      case TK_AND: {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = (a!=0 && b!=0)?1:0;
      } break;
      case TK_OR: {
        double b=(sp?st[--sp]:0), a=(sp?st[--sp]:0);
        if (sp<63) st[sp++] = (a!=0 || b!=0)?1:0;
      } break;

      case TK_END:
      case TK_LP:
      case TK_RP:
      case TK_COMMA:
      default:
        break;
    }
  }

  return (sp>0)? st[sp-1] : 0.0;
}
