/*************************************************************************
 *									 *
 *	 YAP Prolog 							 *
 *									 *
 *	Yap Prolog was developed at NCCUP - Universidade do Porto	 *
 *									 *
 * Copyright L.Damas, V. Santos Costa and Universidade do Porto 1985--	 *
 *									 *
 **************************************************************************
 *									 *
 * File:		strings.c *
 * comments:	General-conversion of character sequences.		 *
 *									 *
 * Last rev:     $Date: 2008-07-24 16:02:00 $,$Author: vsc $	     	 *
 *									 *
 *************************************************************************/

#include "Yap.h"
#include "YapEval.h"
#include "YapHeap.h"
#include "YapText.h"
#include "Yatom.h"
#include "yapio.h"

#include <YapText.h>
#include <string.h>
#include <wchar.h>

#ifndef HAVE_WCSNLEN
inline static size_t min_size(size_t i, size_t j) { return (i < j ? i : j); }
#define wcsnlen(S, N) min_size(N, wcslen(S))
#endif

#if !defined(HAVE_STPCPY) && !defined(__APPLE__)
inline static void* __stpcpy(void * i, const void * j) { return strcpy(i,j)+strlen(j);}
#define stpcpy __stpcpy
#endif

#ifndef NAN
#define NAN (0.0 / 0.0)
#endif

#define MAX_PATHNAME 2048

struct mblock {
  struct mblock *prev, *next;
  int lvl;
  size_t sz;
};

typedef struct TextBuffer_manager {
  void *buf, *ptr;
  size_t sz;
  struct mblock *first[16];
  struct mblock *last[16];
  int lvl;
} text_buffer_t;

int AllocLevel(void) { return LOCAL_TextBuffer->lvl; }
int push_text_stack__(USES_REGS1) {
  int i = LOCAL_TextBuffer->lvl;
  i++;
  LOCAL_TextBuffer->lvl = i;

  return i;
}

int pop_text_stack__(int i) {
  int lvl = LOCAL_TextBuffer->lvl;
  while (lvl >= i) {
    struct mblock *p = LOCAL_TextBuffer->first[lvl];
    while (p) {
      struct mblock *np = p->next;
      free(p);
      p = np;
    }
    LOCAL_TextBuffer->first[lvl] = NULL;
    LOCAL_TextBuffer->last[lvl] = NULL;
    lvl--;
  }
  LOCAL_TextBuffer->lvl = lvl;
  return lvl;
}

void *pop_output_text_stack__(int i, const void *export) {
  int lvl = LOCAL_TextBuffer->lvl;
  while (lvl >= i) {
    struct mblock *p = LOCAL_TextBuffer->first[lvl];
    while (p) {
      struct mblock *np = p->next;
      if (p + 1 == export) {
        size_t sz = p->sz-sizeof(struct mblock) ;
        memcpy(p, p + 1, sz);
        export = p;
      } else {
        free(p);
      }
      p = np;
    }
    LOCAL_TextBuffer->first[lvl] = NULL;
    LOCAL_TextBuffer->last[lvl] = NULL;
    lvl--;
  }
  LOCAL_TextBuffer->lvl = lvl;
  return (void *)export;
}

//	void pop_text_stack(int i) { LOCAL_TextBuffer->lvl = i; }
void insert_block(struct mblock *o) {
  int lvl = o->lvl;
  o->prev = LOCAL_TextBuffer->last[lvl];
  if (o->prev) {
    o->prev->next = o;
  }
  if (LOCAL_TextBuffer->first[lvl]) {
    LOCAL_TextBuffer->last[lvl] = o;
  } else {
    LOCAL_TextBuffer->first[lvl] = LOCAL_TextBuffer->last[lvl] = o;
  }
  o->next = NULL;
}

void release_block(struct mblock *o) {
  int lvl = o->lvl;
  if (LOCAL_TextBuffer->first[lvl] == o) {
    if (LOCAL_TextBuffer->last[lvl] == o) {
      LOCAL_TextBuffer->first[lvl] = LOCAL_TextBuffer->last[lvl] = NULL;
    }
    LOCAL_TextBuffer->first[lvl] = o->next;
  } else if (LOCAL_TextBuffer->last[lvl] == o) {
    LOCAL_TextBuffer->last[lvl] = o->prev;
  }
  if (o->prev)
    o->prev->next = o->next;
  if (o->next)
    o->next->prev = o->prev;
}

void *Malloc(size_t sz USES_REGS) {
  int lvl = LOCAL_TextBuffer->lvl;
  if (sz == 0)
    sz = 1024;
  sz = ALIGN_BY_TYPE(sz + sizeof(struct mblock), CELL);
  struct mblock *o = malloc(sz);
  if (!o)
    return NULL;
  o->sz = sz;
  o->lvl = lvl;
  o->prev = o->next = 0;
  insert_block(o);
  return o + 1;
}

void *MallocAtLevel(size_t sz, int atL USES_REGS) {
  int lvl = LOCAL_TextBuffer->lvl;
  if (atL > 0 && atL <= lvl) {
    lvl = atL;
  } else if (atL < 0 && lvl - atL >= 0) {
    lvl += atL;
  } else {
    return NULL;
  }
  if (sz == 0)
    sz = 1024;
  sz = ALIGN_BY_TYPE(sz + sizeof(struct mblock), CELL);
  struct mblock *o = malloc(sz);
  if (!o)
    return NULL;
  o->sz = sz;
  o->lvl = lvl;
  o->prev = o->next = 0;
  insert_block(o);
  return o + 1;
}

void *Realloc(void *pt, size_t sz USES_REGS) {
  sz += sizeof(struct mblock);
  struct mblock *old = pt, *o;
  old--;
  release_block(old);
  o = realloc(old, sz);
  o->sz = sz;
  insert_block(o);

  return o + 1;
}


/**
 * Export a local memory object as a RO object to the outside world, that is, recovering as much storage as one can.
 * @param pt pointer to object
 * @return new object
 */
const void *MallocExportAsRO(const void *pt USES_REGS) {
  struct  mblock *old = (void *)pt, *o = old-1;
    if (old == NULL)
        return NULL;
  size_t sz = o->sz;
  release_block(o);
  memcpy((void*)o, pt,sz);
  return realloc((void *)o, sz);
}

void Free(void *pt USES_REGS) {
  struct mblock *o = pt;
  o--;
  release_block(o);
  free(o);
}

void *Yap_InitTextAllocator(void) {
  struct TextBuffer_manager *new = calloc(sizeof(struct TextBuffer_manager), 1);
  return new;
}

static size_t MaxTmp(USES_REGS1) {

  return ((char *)LOCAL_TextBuffer->buf + LOCAL_TextBuffer->sz) -
         (char *)LOCAL_TextBuffer->ptr;
}

static Term Globalize(Term v USES_REGS) {
  if (!IsVarTerm(v = Deref(v))) {
    return v;
  }
  if (VarOfTerm(v) > HR && VarOfTerm(v) < LCL0) {
    Bind_Local(VarOfTerm(v), MkVarTerm());
    v = Deref(v);
  }
  return v;
}

static void *codes2buf(Term t0, void *b0, bool *get_codes USES_REGS) {
  unsigned char *st0, *st, ar[16];
  Term t = t0;
  size_t length = 0;

  if (t == TermNil) {
    st0 = Malloc(4);
    st0[0] = 0;
    return st0;
  }
  if (!IsPairTerm(t))
    return NULL;
  bool codes = IsIntegerTerm(HeadOfTerm(t));
  if (get_codes)
    *get_codes = codes;
  if (codes) {
    while (IsPairTerm(t)) {
      Term hd = HeadOfTerm(t);
      if (IsVarTerm(hd)) {
        Yap_Error(INSTANTIATION_ERROR, t0, "scanning list of codes");
        return NULL;
      }
      if (!IsIntegerTerm(hd)) {
        Yap_Error(TYPE_ERROR_INTEGER, t0, "scanning list of codes");
        return NULL;
      }
      Int code = IntegerOfTerm(hd);
      if (code < 0) {
        Yap_Error(REPRESENTATION_ERROR_CHARACTER_CODE, t0,
                  "scanning list of codes");
        return NULL;
      }
      length += put_utf8(ar, code);
      t = TailOfTerm(t);
    }
  } else {
    while (IsPairTerm(t)) {
      Term hd = HeadOfTerm(t);
      if (!IsAtomTerm(hd)) {
        Yap_Error(TYPE_ERROR_ATOM, t0, "scanning list of atoms");
        return NULL;
      }
      const char *code = RepAtom(AtomOfTerm(hd))->StrOfAE;
      if (code < 0) {
        Yap_Error(REPRESENTATION_ERROR_CHARACTER, t0, "scanning list of atoms");
        return NULL;
      }
      length += strlen(code);
      t = TailOfTerm(t);
    }
  }

  if (!IsVarTerm(t)) {
    if (t != TermNil) {
      Yap_Error(TYPE_ERROR_INTEGER, t0, "scanning list of codes");
      return NULL;
    }
  }

  st0 = st = Malloc(length + 1);
  t = t0;
  if (codes) {
    while (IsPairTerm(t)) {
      Term hd = HeadOfTerm(t);

      Int code = IntegerOfTerm(hd);

      st = st + put_utf8(st, code);
      t = TailOfTerm(t);
    }
  } else {
    while (IsPairTerm(t)) {
      Term hd = HeadOfTerm(t);
      const char *code = RepAtom(AtomOfTerm(hd))->StrOfAE;
      st = (unsigned char *)stpcpy((char *)st, code);
      t = TailOfTerm(t);
    }
  }
  st[0] = '\0';

  return st0;
}

static unsigned char *latin2utf8(seq_tv_t *inp) {
  unsigned char *b0 = inp->val.uc;
  size_t sz = strlen(inp->val.c);
  sz *= 2;
  int ch;
  unsigned char *buf = Malloc(sz + 1), *pt = buf;
  if (!buf)
    return NULL;
  while ((ch = *b0++)) {
    int off = put_utf8(pt, ch);
    if (off < 0) {
      continue;
    }
    pt += off;
  }
  *pt++ = '\0';
  return buf;
}

static unsigned char *wchar2utf8(seq_tv_t *inp) {
  size_t sz = wcslen(inp->val.w) * 4;
  wchar_t *b0 = inp->val.w;
  unsigned char *buf = Malloc(sz + 1), *pt = buf;
  int ch;
  if (!buf)
    return NULL;
  while ((ch = *b0++))
    pt += put_utf8(pt, ch);
  *pt++ = '\0';
  return buf;
}

static void *slice(size_t min, size_t max, const unsigned char *buf USES_REGS);

static unsigned char *Yap_ListOfCodesToBuffer(unsigned char *buf, Term t,
                                              seq_tv_t *inp USES_REGS) {
  bool codes;
  unsigned char *nbuf = codes2buf(t, buf, &codes PASS_REGS);
  if (!codes)
    return NULL;
  return nbuf;
}

static unsigned char *Yap_ListOfAtomsToBuffer(unsigned char *buf, Term t,
                                              seq_tv_t *inp USES_REGS) {
  bool codes;
  unsigned char *nbuf = codes2buf(t, buf, &codes PASS_REGS);
  if (codes)
    return NULL;
  return nbuf;
}

static unsigned char *Yap_ListToBuffer(unsigned char *buf, Term t,
                                       seq_tv_t *inp USES_REGS) {
  unsigned char *nbuf = codes2buf(t, buf, NULL PASS_REGS);
  return nbuf;
}

#if USE_GEN_TYPE_ERROR
static yap_error_number gen_type_error(int flags) {
  if ((flags & (YAP_STRING_STRING | YAP_STRING_ATOM | YAP_STRING_INT |
                YAP_STRING_FLOAT | YAP_STRING_ATOMS_CODES | YAP_STRING_BIG)) ==
      (YAP_STRING_STRING | YAP_STRING_ATOM | YAP_STRING_INT | YAP_STRING_FLOAT |
       YAP_STRING_ATOMS_CODES | YAP_STRING_BIG))
    return TYPE_ERROR_TEXT;
  if ((flags & (YAP_STRING_STRING | YAP_STRING_ATOM | YAP_STRING_INT |
                YAP_STRING_FLOAT | YAP_STRING_BIG)) ==
      (YAP_STRING_STRING | YAP_STRING_ATOM | YAP_STRING_INT | YAP_STRING_FLOAT |
       YAP_STRING_BIG))
    return TYPE_ERROR_ATOMIC;
  if ((flags & (YAP_STRING_INT | YAP_STRING_FLOAT | YAP_STRING_BIG)) ==
      (YAP_STRING_INT | YAP_STRING_FLOAT | YAP_STRING_BIG))
    return TYPE_ERROR_NUMBER;
  if (flags & YAP_STRING_ATOM)
    return TYPE_ERROR_ATOM;
  if (flags & YAP_STRING_STRING)
    return TYPE_ERROR_STRING;
  if (flags & (YAP_STRING_CODES | YAP_STRING_ATOMS))
    return TYPE_ERROR_LIST;
  return TYPE_ERROR_NUMBER;
}
#endif

//  static int cnt;

unsigned char *Yap_readText(seq_tv_t *inp USES_REGS) {

  /* we know what the term is */
  if (!(inp->type & (YAP_STRING_CHARS | YAP_STRING_WCHARS))) {
    if (!(inp->type & YAP_STRING_TERM)) {
      if (IsVarTerm(inp->val.t)) {
        LOCAL_Error_TYPE = INSTANTIATION_ERROR;
      } else if (!IsAtomTerm(inp->val.t) && inp->type == YAP_STRING_ATOM) {
        LOCAL_Error_TYPE = TYPE_ERROR_ATOM;
      } else if (!IsStringTerm(inp->val.t) && inp->type == YAP_STRING_STRING) {
        LOCAL_Error_TYPE = TYPE_ERROR_STRING;
      } else if (!IsPairOrNilTerm(inp->val.t) && !IsStringTerm(inp->val.t) &&
                 inp->type == (YAP_STRING_ATOMS_CODES | YAP_STRING_STRING)) {
        LOCAL_Error_TYPE = TYPE_ERROR_LIST;
      } else if (!IsPairOrNilTerm(inp->val.t) && !IsStringTerm(inp->val.t) &&
                 !IsAtomTerm(inp->val.t) && !(inp->type & YAP_STRING_DATUM)) {
        LOCAL_Error_TYPE = TYPE_ERROR_TEXT;
      }
    }
  }
  if (LOCAL_Error_TYPE != YAP_NO_ERROR)
    return NULL;

  if (IsAtomTerm(inp->val.t) && inp->type & YAP_STRING_ATOM) {
    // this is a term, extract to a buffer, and representation is wide
    // Yap_DebugPlWriteln(inp->val.t);
    Atom at = AtomOfTerm(inp->val.t);
    if (RepAtom(at)->UStrOfAE[0] == 0) {
      unsigned char *o = Malloc(4);
      memset(o, 0, 4);
      return o;
    }
    if (inp->type & YAP_STRING_WITH_BUFFER)
      return at->UStrOfAE;
    size_t sz = strlen(at->StrOfAE);
    inp->type |= YAP_STRING_IN_TMP;
    char *o = BaseMalloc(sz + 1);
    strcpy(o, at->StrOfAE);
    return (unsigned char *)o;
  }
  if (IsStringTerm(inp->val.t) && inp->type & YAP_STRING_STRING) {
    // this is a term, extract to a buffer, and representation is wide
    // Yap_DebugPlWriteln(inp->val.t);
    const char *s = StringOfTerm(inp->val.t);
    if (s[0] == 0) {
      char *o = BaseMalloc(4);
      memset(o, 0, 4);
    }
    if (inp->type & YAP_STRING_WITH_BUFFER)
      return (unsigned char *)UStringOfTerm(inp->val.t);
    inp->type |= YAP_STRING_IN_TMP;
    size_t sz = strlen(s);
    char *o = BaseMalloc(sz + 1);
    strcpy(o, s);
    return (unsigned char *)o;
  }
  if (((inp->type & (YAP_STRING_CODES | YAP_STRING_ATOMS)) ==
       (YAP_STRING_CODES | YAP_STRING_ATOMS)) &&
      IsPairOrNilTerm(inp->val.t)) {
    // Yap_DebugPlWriteln(inp->val.t);
    return Yap_ListToBuffer(NULL, inp->val.t, inp PASS_REGS);
    // this is a term, extract to a sfer, and representation is wide
  }
  if (inp->type & YAP_STRING_CODES && IsPairOrNilTerm(inp->val.t)) {
    // Yap_DebugPlWriteln(inp->val.t);
    return Yap_ListOfCodesToBuffer(NULL, inp->val.t, inp PASS_REGS);
    // this is a term, extract to a sfer, and representation is wide
  }
  if (inp->type & YAP_STRING_ATOMS && IsPairOrNilTerm(inp->val.t)) {
    // Yap_DebugPlWriteln(inp->val.t);
    return Yap_ListOfAtomsToBuffer(NULL, inp->val.t, inp PASS_REGS);
    // this is a term, extract to a buffer, and representation is wide
  }
  if (inp->type & YAP_STRING_INT && IsIntegerTerm(inp->val.t)) {
    // ASCII, so both LATIN1 and UTF-8
    // Yap_DebugPlWriteln(inp->val.t);
    char *s;
    s = BaseMalloc(2 * MaxTmp(PASS_REGS1));
    if (snprintf(s, MaxTmp(PASS_REGS1) - 1, Int_FORMAT,
                 IntegerOfTerm(inp->val.t)) < 0) {
      AUX_ERROR(inp->val.t, 2 * MaxTmp(PASS_REGS1), s, char);
    }
    return (unsigned char *)s;
  }
  if (inp->type & YAP_STRING_FLOAT && IsFloatTerm(inp->val.t)) {
    char *s;
    // Yap_DebugPlWriteln(inp->val.t);
    if (!Yap_FormatFloat(FloatOfTerm(inp->val.t), &s, 1024)) {
      return NULL;
    }
    return (unsigned char *)s;
  }
#if USE_GMP
  if (inp->type & YAP_STRING_BIG && IsBigIntTerm(inp->val.t)) {
    // Yap_DebugPlWriteln(inp->val.t);
    char *s;
    s = BaseMalloc(MaxTmp());
    if (!Yap_mpz_to_string(Yap_BigIntOfTerm(inp->val.t), s, MaxTmp() - 1, 10)) {
      AUX_ERROR(inp->val.t, MaxTmp(PASS_REGS1), s, char);
    }
    return inp->val.uc = (unsigned char *)s;
  }
#endif
  if (inp->type & YAP_STRING_TERM) {
    // Yap_DebugPlWriteln(inp->val.t);
    char *s = (char *) Yap_TermToBuffer(inp->val.t, ENC_ISO_UTF8, 0);
    return inp->val.uc = (unsigned char *)s;
  }
  if (inp->type & YAP_STRING_CHARS) {
    if (inp->enc == ENC_ISO_LATIN1) {
      return latin2utf8(inp);
    } else if (inp->enc == ENC_ISO_ASCII) {
      return inp->val.uc;
    } else { // if (inp->enc == ENC_ISO_UTF8) {
      return inp->val.uc;
    }
  }
  if (inp->type & YAP_STRING_WCHARS) {
    // printf("%S\n",inp->val.w);
    return wchar2utf8(inp);
  }
  return NULL;
}

static Term write_strings(unsigned char *s0, seq_tv_t *out USES_REGS) {
  size_t min = 0, max = strlen((char *)s0);

  if (out->type & (YAP_STRING_NCHARS | YAP_STRING_TRUNC)) {
    if (out->type & YAP_STRING_NCHARS)
      min = out->max;
    if (out->type & YAP_STRING_TRUNC && out->max < max) {
      max = out->max;
      s0[max] = '\0';
    }
  }

  char *s = (char *)s0;
  Term t = init_tstring(PASS_REGS1);
  LOCAL_TERM_ERROR(t, 2 * max);
  unsigned char *buf = buf_from_tstring(HR);
  if (max==0)
    buf[0] = '\0';
  else
    strcpy((char *)buf, s);
  if (max + 1 < min) {
    LOCAL_TERM_ERROR(t, 2 * min);
    memset(buf + min, '\0', max);
    buf += min;
  } else {
    buf += max + 1;
  }
  close_tstring(buf PASS_REGS);
  out->val.t = t;

  return out->val.t;
}

static Term write_atoms(void *s0, seq_tv_t *out USES_REGS) {
  Term t = AbsPair(HR);
  char *s1 = (char *)s0;
  size_t sz = 0;
  size_t max = strlen(s1);
  if (s1[0] == '\0') {
    out->val.t = TermNil;
    return TermNil;
  }
  if (out->type & (YAP_STRING_NCHARS | YAP_STRING_TRUNC)) {
    if (out->type & YAP_STRING_TRUNC && out->max < max)
      max = out->max;
  }

  unsigned char *s = s0, *lim = s + strnlen((char *)s, max);
  unsigned char *cp = s;
  unsigned char w[10];
  int wp = 0;
  LOCAL_TERM_ERROR(t, 2 * (lim - s));
  while (cp < lim && *cp) {
    utf8proc_int32_t chr;
    CELL *cl;
    s += get_utf8(s, -1, &chr);
    if (chr == '\0') {
      w[0] = '\0';
      break;
    }
    wp = put_utf8(w, chr);
    w[wp] = '\0';
    cl = HR;
    HR += 2;
    cl[0] = MkAtomTerm(Yap_ULookupAtom(w));
    cl[1] = AbsPair(HR);
    sz++;
    if (sz == max)
      break;
  }
  if (out->type & YAP_STRING_DIFF) {
    if (sz == 0)
      t = out->dif;
    else
      HR[-1] = Globalize(out->dif PASS_REGS);
  } else {
    if (sz == 0)
      t = TermNil;
    else
      HR[-1] = TermNil;
  }
  out->val.t = t;
  return (t);
}

static Term write_codes(void *s0, seq_tv_t *out USES_REGS) {
  Term t;
  size_t sz = strlen(s0);
  if (sz == 0) {
    if (out->type & YAP_STRING_DIFF) {
      out->val.t = Globalize(out->dif PASS_REGS);
    } else {
      out->val.t = TermNil;
    }
    return out->val.t;
  }
  unsigned char *s = s0, *lim = s + sz;
  unsigned char *cp = s;

  t = AbsPair(HR);
  LOCAL_TERM_ERROR(t, 2 * (lim - s));
  t = AbsPair(HR);
  while (*cp) {
    utf8proc_int32_t chr;
    CELL *cl;
    cp += get_utf8(cp, -1, &chr);
    if (chr == '\0')
      break;
    cl = HR;
    HR += 2;
    cl[0] = MkIntegerTerm(chr);
    cl[1] = AbsPair(HR);
  }
  if (sz == 0) {
    HR[-1] = Globalize(out->dif PASS_REGS);
  } else {
    HR[-1] = TermNil;
  }
  out->val.t = t;
  return (t);
}

static Atom write_atom(void *s0, seq_tv_t *out USES_REGS) {
  unsigned char *s = s0;
  int32_t ch;
  size_t leng = strlen(s0);
  if (leng == 0) {
    return Yap_LookupAtom("");
  }
  if (strlen_utf8(s0) <= leng) {
    return Yap_LookupAtom(s0);
  } else {
    size_t n = get_utf8(s, -1, &ch);
    unsigned char *buf = Malloc(n + 1);
    memcpy(buf, s0, n + 1);
    return Yap_ULookupAtom(buf);
  }
}

void *write_buffer(unsigned char *s0, seq_tv_t *out USES_REGS) {
  size_t leng = strlen((char *)s0);
  size_t min = 0, max = leng;
  if (out->enc == ENC_ISO_UTF8) {
    if ( out->val.uc == NULL) { // this should always be the case
      out->val.uc = BaseMalloc(leng + 1);
      strcpy(out->val.c, (char *)s0);
    } else if (out->val.uc != s0) {
      out->val.c = BaseMalloc(leng + 1);
      strcpy(out->val.c, (char *)s0);
    }
  } else if (out->enc == ENC_ISO_LATIN1) {

    unsigned char *s = s0;
    unsigned char *cp = s;
    unsigned char *buf = out->val.uc;
    if (!buf)
      return NULL;
    while (*cp) {
      utf8proc_int32_t chr;
      int off = get_utf8(cp, -1, &chr);
      if (off <= 0 || chr > 255)
        return NULL;
      if (off == max)
        break;
      cp += off;
      *buf++ = chr;
    }
    if (max >= min)
      *buf++ = '\0';
    else
      while (max < min) {
        utf8proc_int32_t chr;
        max++;
        cp += get_utf8(cp, -1, &chr);
        *buf++ = chr;
      }
  } else if (out->enc == ENC_WCHAR) {
    unsigned char *s = s0, *lim = s + (max = strnlen((char *)s0, max));
    unsigned char *cp = s;
    wchar_t *buf0, *buf;

    buf = buf0 = out->val.w;
    if (!buf)
      return NULL;
    while (*cp && cp < lim) {
      utf8proc_int32_t chr;
      cp += get_utf8(cp, -1, &chr);
      *buf++ = chr;
    }
    if (max >= min)
      *buf++ = '\0';
    else
      while (max < min) {
        utf8proc_int32_t chr;
        max++;
        cp += get_utf8(cp, -1, &chr);
        *buf++ = chr;
      }
    *buf = '\0';
  } else {
    // no other encodings are supported.
    return NULL;
  }
  return out->val.c;
}

static size_t write_length(const unsigned char *s0, seq_tv_t *out USES_REGS) {
  return strlen_utf8(s0);
}

static Term write_number(unsigned char *s, seq_tv_t *out,
                         bool error_on USES_REGS) {
  Term t;
    yap_error_descriptor_t new_error;
    int i = push_text_stack();
    Yap_pushErrorContext(&new_error);
  t = Yap_StringToNumberTerm((char *)s, &out->enc, error_on);
  pop_text_stack(i);
    Yap_popErrorContext(true);
  return t;
}

static Term string_to_term(void *s, seq_tv_t *out USES_REGS) {
  Term o;
    yap_error_descriptor_t new_error;
    Yap_pushErrorContext(&new_error);
    o = out->val.t = Yap_BufferToTerm(s, TermNil);
    Yap_popErrorContext(true);

    return o;
}

bool write_Text(unsigned char *inp, seq_tv_t *out USES_REGS) {
  /* we know what the term is */
  if (out->type == 0) {
    return true;
  }

  if (out->type & (YAP_STRING_INT | YAP_STRING_FLOAT | YAP_STRING_BIG)) {
    if ((out->val.t = write_number(
             inp, out, !(out->type & YAP_STRING_ATOM) PASS_REGS)) != 0L) {
      // Yap_DebugPlWriteln(out->val.t);

      return true;
    }

    if (!(out->type & YAP_STRING_ATOM))
      return false;
  }
  if (out->type & (YAP_STRING_ATOM)) {
    if ((out->val.a = write_atom(inp, out PASS_REGS)) != NIL) {
      Atom at = out->val.a;
      if (at && (out->type & YAP_STRING_OUTPUT_TERM))
        out->val.t = MkAtomTerm(at);
      // Yap_DebugPlWriteln(out->val.t);
      return at != NIL;
    }
  }
  if (out->type & YAP_STRING_DATUM) {
    if ((out->val.t = string_to_term(inp, out PASS_REGS)) != 0L)
      return out->val.t != 0;
  }

  switch (out->type & YAP_TYPE_MASK) {
  case YAP_STRING_CHARS: {
    void *room = write_buffer(inp, out PASS_REGS);
    // printf("%s\n", out->val.c);
    return room != NULL;
  }
  case YAP_STRING_WCHARS: {
    void *room = write_buffer(inp, out PASS_REGS);
    // printf("%S\n", out->val.w);
    return room != NULL;
  }
  case YAP_STRING_STRING:
    out->val.t = write_strings(inp, out PASS_REGS);
    // Yap_DebugPlWriteln(out->val.t);
    return out->val.t != 0;
  case YAP_STRING_ATOMS:
    out->val.t = write_atoms(inp, out PASS_REGS);
    // Yap_DebugPlWriteln(out->val.t);
    return out->val.t != 0;
  case YAP_STRING_CODES:
    out->val.t = write_codes(inp, out PASS_REGS);
    // Yap_DebugPlWriteln(out->val.t);
    return out->val.t != 0;
  case YAP_STRING_LENGTH:
    out->val.l = write_length(inp, out PASS_REGS);
    // printf("s\n",out->val.l);
    return out->val.l != (size_t)(-1);
  case YAP_STRING_ATOM:
    out->val.a = write_atom(inp, out PASS_REGS);
    // Yap_DebugPlWriteln(out->val.t);
    return out->val.a != NULL;
  case YAP_STRING_INT | YAP_STRING_FLOAT | YAP_STRING_BIG:
    out->val.t = write_number(inp, out, true PASS_REGS);
    // Yap_DebugPlWriteln(out->val.t);
    return out->val.t != 0;
  default: { return true; }
  }
  return false;
}

static size_t upcase(void *s0, seq_tv_t *out USES_REGS) {

  unsigned char *s = s0;
  while (*s) {
    // assumes the two code have always the same size;
    utf8proc_int32_t chr;
    get_utf8(s, -1, &chr);
    chr = utf8proc_toupper(chr);
    s += put_utf8(s, chr);
  }
  return true;
}

static size_t downcase(void *s0, seq_tv_t *out USES_REGS) {

  unsigned char *s = s0;
  while (*s) {
    // assumes the two code have always the same size;
    utf8proc_int32_t chr;
    get_utf8(s, -1, &chr);
    chr = utf8proc_tolower(chr);
    s += put_utf8(s, chr);
  }
  return true;
}

bool Yap_CVT_Text(seq_tv_t *inp, seq_tv_t *out USES_REGS) {
  unsigned char *buf;
  bool rc;
  /*
  //printf(stderr, "[ %d ", n++)    ;
  if (inp->type & (YAP_STRING_TERM|YAP_STRING_ATOM|YAP_STRING_ATOMS_CODES
  |YAP_STRING_STRING))
  //Yap_DebugPlWriteln(inp->val.t);
  else if (inp->type & YAP_STRING_WCHARS) fprintf(stderr,"S %S\n", inp->val
  .w);
  else  fprintf(stderr,"s %s\n", inp->val.c);
  */
  //  cnt++;
  int l = push_text_stack();
  buf = Yap_readText(inp PASS_REGS);
  if (!buf) {
    pop_text_stack(l);
    return 0L;
  }
  if (buf[0]) {
  size_t leng = strlen_utf8(buf);
  if (out->type & (YAP_STRING_NCHARS | YAP_STRING_TRUNC)) {
    if (out->max < leng) {
      const unsigned char *ptr = skip_utf8(buf, out->max);
      size_t diff = (ptr - buf);
      char *nbuf = Malloc(diff + 1);
      memcpy(nbuf, buf, diff);
      nbuf[diff] = '\0';
      leng = diff;
    }
    // else if (out->type & YAP_STRING_NCHARS &&
    // const unsigned char *ptr = skip_utf8(buf)
  }

  if (out->type & (YAP_STRING_UPCASE | YAP_STRING_DOWNCASE)) {
    if (out->type & YAP_STRING_UPCASE) {
      if (!upcase(buf, out)) {
        pop_text_stack(l);
        return false;
      }
    }
    if (out->type & YAP_STRING_DOWNCASE) {
      if (!downcase(buf, out)) {
        pop_text_stack(l);
        return false;
      }
    }
  }
  }
  rc = write_Text(buf, out PASS_REGS);
  /*    fprintf(stderr, " -> ");
        if (!rc) fprintf(stderr, "NULL");
        else if (out->type &
        (YAP_STRING_TERM|YAP_STRING_ATOMS_CODES
        |YAP_STRING_STRING)) //Yap_DebugPlWrite(out->val.t);
        else if (out->type &
        YAP_STRING_ATOM) //Yap_DebugPlWriteln(MkAtomTerm(out->val.a));
        else if (out->type & YAP_STRING_WCHARS) fprintf(stderr, "%S",
        out->val.w);
        else
        fprintf(stderr, "%s", out->val.c);
        fprintf(stderr, "\n]\n"); */
  pop_text_stack(l);
  return rc;
}

static unsigned char *concat(int n, void *sv[] USES_REGS) {
  void *buf;
  unsigned char *buf0;
  size_t room = 0;
  int i;

  for (i = 0; i < n; i++) {
    char *s = sv[i];
    if (s[0])
      room += strlen(s);
  }
  buf = Malloc(room + 1);
  buf0 = buf;
  for (i = 0; i < n; i++) {
    char *s = sv[i];
    if (!s[0])
      continue;
#if _WIN32 || defined(__ANDROID__)
    strcpy(buf, s);
    buf = (char *)buf + strlen(buf);
#else
    buf = stpcpy(buf, s);
#endif
  }
  return buf0;
}

static void *slice(size_t min, size_t max, const unsigned char *buf USES_REGS) {
  unsigned char *nbuf = BaseMalloc((max - min) * 4 + 1);
  const unsigned char *ptr = skip_utf8(buf, min);
  unsigned char *nptr = nbuf;
  utf8proc_int32_t chr;

  while (min++ < max) {
    ptr += get_utf8(ptr, -1, &chr);
    nptr += put_utf8(nptr, chr);
  }
  nptr[0] = '\0';
  return nbuf;
}

//
// Out must be an atom or a string
bool Yap_Concat_Text(int tot, seq_tv_t inp[], seq_tv_t *out USES_REGS) {
  void **bufv;
  unsigned char *buf;
  int i, j;

  bufv = Malloc(tot * sizeof(unsigned char *));
  if (!bufv) {
    return NULL;
  }
  for (i = 0, j = 0; i < tot; i++) {
    inp[j].type |= YAP_STRING_WITH_BUFFER;
    unsigned char *nbuf = Yap_readText(inp + i PASS_REGS);

    if (!nbuf) {
      return NULL;
    }
    //      if (!nbuf[0])
    //	continue;
    bufv[j++] = nbuf;
  }
  if (j == 0) {
    buf = Malloc(8);
    memset(buf, 0, 4);
  } else if (j == 1) {
    buf = bufv[0];
  } else {
    buf = concat(tot, bufv PASS_REGS);
  }
  bool rc = write_Text(buf, out PASS_REGS);

  return rc;
}

//
bool Yap_Splice_Text(int n, size_t cuts[], seq_tv_t *inp,
                     seq_tv_t outv[] USES_REGS) {
  const unsigned char *buf;
  size_t b_l, u_l;

  inp->type |= YAP_STRING_IN_TMP;
  buf = Yap_readText(inp PASS_REGS);
  if (!buf) {
    return false;
  }
  b_l = strlen((char *)buf);
  if (b_l == 0) {
    return false;
  }
  u_l = strlen_utf8(buf);
  if (!cuts) {
    if (n == 2) {
      size_t b_l0, b_l1, u_l0, u_l1;
      unsigned char *buf0, *buf1;

      if (outv[0].val.t) {
        buf0 = Yap_readText(outv PASS_REGS);
        if (!buf0) {
          return false;
        }
        b_l0 = strlen((const char *)buf0);
        if (memcmp(buf, buf0, b_l0) != 0) {
          return false;
        }
        u_l0 = strlen_utf8(buf0);
        u_l1 = u_l - u_l0;

        b_l1 = b_l - b_l0;
        buf1 = slice(u_l0, u_l, buf PASS_REGS);
        b_l1 = strlen((const char *)buf1);
        bool rc = write_Text(buf1, outv + 1 PASS_REGS);
        if (!rc) {
          return false;
        }
        return rc;
      } else /* if (outv[1].val.t) */ {
        buf1 = Yap_readText(outv + 1 PASS_REGS);
        if (!buf1) {
          return false;
        }
        b_l1 = strlen((char *)buf1);
        u_l1 = strlen_utf8(buf1);
        b_l0 = b_l - b_l1;
        u_l0 = u_l - u_l1;
        if (memcmp(skip_utf8((const unsigned char *)buf, b_l0), buf1, b_l1) !=
            0) {
          return false;
        }
        buf0 = slice(0, u_l0, buf PASS_REGS);
        bool rc = write_Text(buf0, outv PASS_REGS);
        return rc;
      }
    }
  }
  int i, next;
  for (i = 0; i < n; i++) {
    if (i == 0)
      next = 0;
    else
      next = cuts[i - 1];
    if (i > 0 && cuts[i] == 0)
      break;
    void *bufi = slice(next, cuts[i], buf PASS_REGS);
    if (!write_Text(bufi, outv + i PASS_REGS)) {
      return false;
    }
  }

  return true;
}


/**
 * Convert from a predicate structure to an UTF-8 string of the form
 *
 * module:name/arity.
 *
 * The result is in very volatile memory.
 *
 * @param s        the buffer
 *
 * @return the temporary string
 */
const char *Yap_PredIndicatorToUTF8String(PredEntry *ap) {
  CACHE_REGS
  Atom at;
  arity_t arity = 0;
  Functor f;
  char *s, *smax, *s0;
  s = s0 = malloc(1024);
  smax = s + 1024;
  Term tmod = ap->ModuleOfPred;
  if (tmod) {
    char *sn = Yap_AtomToUTF8Text(AtomOfTerm(tmod));
    stpcpy(s, sn);
    if (smax - s > 1) {
      strcat(s, ":");
    } else {
      return NULL;
    }
    s++;
  } else {
    if (smax - s > strlen("prolog:")) {
      s = strcpy(s, "prolog:");
    } else {
      return NULL;
    }
  }
  // follows the actual functor
  if (ap->ModuleOfPred == IDB_MODULE) {
    if (ap->PredFlags & NumberDBPredFlag) {
      Int key = ap->src.IndxId;
      snprintf(s, smax - s, "%" PRIdPTR, key);
      return LOCAL_FileNameBuf;
    } else if (ap->PredFlags & AtomDBPredFlag) {
      at = (Atom)(ap->FunctorOfPred);
      if (!stpcpy(s, Yap_AtomToUTF8Text(at)))
        return NULL;
    } else {
      f = ap->FunctorOfPred;
      at = NameOfFunctor(f);
      arity = ArityOfFunctor(f);
    }
  } else {
    arity = ap->ArityOfPE;
    if (arity) {
      at = NameOfFunctor(ap->FunctorOfPred);
    } else {
      at = (Atom)(ap->FunctorOfPred);
    }
  }
  if (!stpcpy(s, Yap_AtomToUTF8Text(at))) {
    return NULL;
  }
  s += strlen(s);
  snprintf(s, smax - s, "/%" PRIdPTR, arity);
  return s0;
}

/**
 * Convert from a text buffer (8-bit) to a term that has the same type as
 * _Tguide_
 *
 ≈* @param s        the buffer
 ≈ * @param tguide   the guide
 *
 ≈  * @return the term
*/
Term Yap_MkTextTerm(const char *s, int guide USES_REGS) {
  if (guide == YAP_STRING_ATOM) {
    return MkAtomTerm(Yap_LookupAtom(s));
  } else if (guide == YAP_STRING_STRING) {
    return MkStringTerm(s);
  } else if (guide == YAP_STRING_ATOMS) {
    return Yap_CharsToListOfAtoms(s, ENC_ISO_UTF8 PASS_REGS);
  } else {
    return Yap_CharsToListOfCodes(s, ENC_ISO_UTF8 PASS_REGS);
  }
}
