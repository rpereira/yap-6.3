/*************************************************************************
*									 *
*	 YAP Prolog 							 *
*									 *
*	Yap Prolog was developed at NCCUP - Universidade do Porto	 *
*									 *
* Copyright L.Damas, V.S.Costa and Universidade do Porto 1985-1997	 *
*									 *
**************************************************************************
*									 *
* File:		utilpreds.c						 *
* Last rev:	4/03/88							 *
* mods:									 *
* comments:	new utility predicates for YAP				 *
*									 *
*************************************************************************/
#ifdef SCCS
static char     SccsId[] = "@(#)utilpreds.c	1.3";
#endif

#include "Yap.h"
#include "Yatom.h"
#include "Heap.h"
#include "yapio.h"
#include "eval.h"
#include "attvar.h"

typedef struct {
	Term            old_var;
	Term            new_var;
}              *vcell;


STATIC_PROTO(int   copy_complex_term, (CELL *, CELL *, int, int, CELL *, CELL *));
STATIC_PROTO(CELL  vars_in_complex_term, (CELL *, CELL *, Term));
STATIC_PROTO(Int   p_non_singletons_in_term, (void));
STATIC_PROTO(CELL  non_singletons_in_complex_term, (CELL *, CELL *));
STATIC_PROTO(Int   p_variables_in_term, (void));
STATIC_PROTO(Int   ground_complex_term, (CELL *, CELL *));
STATIC_PROTO(Int   p_ground, (void));
STATIC_PROTO(Int   p_copy_term, (void));
STATIC_PROTO(Int   var_in_complex_term, (CELL *, CELL *, Term));

#ifdef DEBUG
STATIC_PROTO(Int  p_force_trail_expansion, (void));
#endif /* DEBUG */

static inline void
clean_tr(tr_fr_ptr TR0) {
  if (TR != TR0) {
    do {
      Term p = TrailTerm(--TR);
      RESET_VARIABLE(p);
    } while (TR != TR0);
  }
}

static inline void
clean_dirty_tr(tr_fr_ptr TR0) {
  if (TR != TR0) {
    tr_fr_ptr pt = TR0;

    do {
      Term p = TrailTerm(pt++);
      if (IsVarTerm(p)) {
	RESET_VARIABLE(p);
      } else {
	/* copy downwards */
	TrailTerm(TR0+1) = TrailTerm(pt);
	TrailTerm(TR0) = TrailTerm(TR0+2) = p;
	pt+=2;
	TR0 += 3;
      }
    } while (pt != TR);
    TR = TR0;
  }
}


static int
copy_complex_term(CELL *pt0, CELL *pt0_end, int share, int newattvs, CELL *ptf, CELL *HLow)
{

  struct cp_frame *to_visit0, *to_visit = (struct cp_frame *)Yap_PreAllocCodeSpace();
  CELL *HB0 = HB;
  tr_fr_ptr TR0 = TR;
  int ground = TRUE;
#ifdef COROUTINING
  CELL *dvars = NULL;
#endif

  HB = HLow;
  to_visit0 = to_visit;
 loop:
  while (pt0 < pt0_end) {
    register CELL d0;
    register CELL *ptd0;
    ++ pt0;
    ptd0 = pt0;
    d0 = *ptd0;
    deref_head(d0, copy_term_unk);
  copy_term_nvar:
    {
      if (IsPairTerm(d0)) {
	CELL *ap2 = RepPair(d0);
	if (ap2 >= HB && ap2 < H) {
	  /* If this is newer than the current term, just reuse */
	  *ptf++ = d0;
	  continue;
	} 
	*ptf = AbsPair(H);
	ptf++;
#ifdef RATIONAL_TREES
	if (to_visit+1 >= (struct cp_frame *)AuxSp) {
	  goto heap_overflow;
	}
	to_visit->start_cp = pt0;
	to_visit->end_cp = pt0_end;
	to_visit->to = ptf;
	to_visit->oldv = *pt0;
	to_visit->ground = ground;
	/* fool the system into thinking we had a variable there */
	*pt0 = AbsPair(H);
	to_visit ++;
#else
	if (pt0 < pt0_end) {
	  if (to_visit+1 >= (struct cp_frame *)AuxSp) {
	    goto heap_overflow;
	  }
	  to_visit->start_cp = pt0;
	  to_visit->end_cp = pt0_end;
	  to_visit->to = ptf;
	  to_visit->ground = ground;
	  to_visit ++;
	}
#endif
	ground = TRUE;
	pt0 = ap2 - 1;
	pt0_end = ap2 + 1;
	ptf = H;
	H += 2;
	if (H > ASP - 2048) {
	  goto overflow;
	}
      } else if (IsApplTerm(d0)) {
	register Functor f;
	register CELL *ap2;
	/* store the terms to visit */
	ap2 = RepAppl(d0);
	if (ap2 >= HB && ap2 <= H) {
	  /* If this is newer than the current term, just reuse */
	  *ptf++ = d0;
	  continue;
	} 
	f = (Functor)(*ap2);

	if (IsExtensionFunctor(f)) {
	    {
	      *ptf++ = d0;  /* you can just copy other extensions. */
	    }
	  continue;
	}
	*ptf = AbsAppl(H);
	ptf++;
	/* store the terms to visit */
#ifdef RATIONAL_TREES
	if (to_visit+1 >= (struct cp_frame *)AuxSp) {
	  goto heap_overflow;
	}
	to_visit->start_cp = pt0;
	to_visit->end_cp = pt0_end;
	to_visit->to = ptf;
	to_visit->oldv = *pt0;
	to_visit->ground = ground;
	/* fool the system into thinking we had a variable there */
	*pt0 = AbsAppl(H);
	to_visit ++;
#else
	if (pt0 < pt0_end) {
	  if (to_visit+1 >= (struct cp_frame *)AuxSp) {
	    goto heap_overflow;
	  }
	  to_visit->start_cp = pt0;
	  to_visit->end_cp = pt0_end;
	  to_visit->to = ptf;
	  to_visit->ground = ground;
	  to_visit ++;
	}
#endif
	ground = (f != FunctorMutable);
	d0 = ArityOfFunctor(f);
	pt0 = ap2;
	pt0_end = ap2 + d0;
	/* store the functor for the new term */
	H[0] = (CELL)f;
	ptf = H+1;
	H += 1+d0;
	if (H > ASP - 2048) {
	  goto overflow;
	}
      } else {
	/* just copy atoms or integers */
	*ptf++ = d0;
      }
      continue;
    }

    derefa_body(d0, ptd0, copy_term_unk, copy_term_nvar);
    ground = FALSE;
    if (ptd0 >= HLow && ptd0 < H) { 
      /* we have already found this cell */
      *ptf++ = (CELL) ptd0;
    } else {
#if COROUTINING
      if (newattvs && IsAttachedTerm((CELL)ptd0)) {
	/* if unbound, call the standard copy term routine */
	struct cp_frame *bp[1];

	if (dvars == NULL) {
	  dvars = (CELL *)DelayTop();
	} 	
	if (ptd0 < dvars) {
	  *ptf++ = (CELL) ptd0;
	} else {
	  tr_fr_ptr CurTR;

	  CurTR = TR;
	  bp[0] = to_visit;
	  HB = HB0;
	  if (!attas[ExtFromCell(ptd0)].copy_term_op(ptd0, bp, ptf)) {
	    goto overflow;
	  }
	  to_visit = bp[0];
	  HB = HLow;
	  ptf++;
	  if (TR > (tr_fr_ptr)Yap_TrailTop - 256) {
	    /* Trail overflow */
	    if (!Yap_growtrail((TR-TR0)*sizeof(tr_fr_ptr *), TRUE)) {
	      goto trail_overflow;
	    }
	  }
	  Bind_Global(ptd0, ptf[-1]);
	}
      } else {
#endif
	/* first time we met this term */
	RESET_VARIABLE(ptf);
	Bind_Global(ptd0, (CELL)ptf);
	ptf++;
#ifdef COROUTINING
      }
#endif
    }
  }
  /* Do we still have compound terms to visit */
  if (to_visit > to_visit0) {
    to_visit --;
    if (ground && share) {
      CELL old = to_visit->oldv;
      CELL *newp = to_visit->to-1;
      CELL new = *newp;

      *newp = old;
      if (IsApplTerm(new))
	H = RepAppl(new);
      else
	H = RepPair(new);
    }
    pt0 = to_visit->start_cp;
    pt0_end = to_visit->end_cp;
    ptf = to_visit->to;
#ifdef RATIONAL_TREES
    *pt0 = to_visit->oldv;
#endif
    ground = (ground && to_visit->ground);
    goto loop;
  }

  /* restore our nice, friendly, term to its original state */
  HB = HB0;
  clean_dirty_tr(TR0);
  return ground;

 overflow:
  /* oops, we're in trouble */
  H = HLow;
  /* we've done it */
  /* restore our nice, friendly, term to its original state */
  HB = HB0;
#ifdef RATIONAL_TREES
  while (to_visit > to_visit0) {
    to_visit --;
    pt0 = to_visit->start_cp;
    pt0_end = to_visit->end_cp;
    ptf = to_visit->to;
    *pt0 = to_visit->oldv;
  }
#endif
  reset_trail(TR0);
  return -1;

trail_overflow:
  /* oops, we're in trouble */
  H = HLow;
  /* we've done it */
  /* restore our nice, friendly, term to its original state */
  HB = HB0;
#ifdef RATIONAL_TREES
  while (to_visit > to_visit0) {
    to_visit --;
    pt0 = to_visit->start_cp;
    pt0_end = to_visit->end_cp;
    ptf = to_visit->to;
    *pt0 = to_visit->oldv;
  }
#endif
  {
    tr_fr_ptr oTR =  TR;
    reset_trail(TR0);
    if (!Yap_growtrail((oTR-TR0)*sizeof(tr_fr_ptr *), FALSE)) {
      return -4;
    }
    return -2;
  }

 heap_overflow:
  /* oops, we're in trouble */
  H = HLow;
  /* we've done it */
  /* restore our nice, friendly, term to its original state */
  HB = HB0;
#ifdef RATIONAL_TREES
  while (to_visit > to_visit0) {
    to_visit --;
    pt0 = to_visit->start_cp;
    pt0_end = to_visit->end_cp;
    ptf = to_visit->to;
    *pt0 = to_visit->oldv;
  }
#endif
  reset_trail(TR0);
  return -3;
}


static Term
handle_cp_overflow(int res, UInt arity, Term t)
{
  XREGS[arity+1] = t;
  switch(res) {
  case -1:
    if (!Yap_gcl((ASP-H)*sizeof(CELL), arity+1, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, Yap_ErrorMessage);
      return 0L;
    }
    return Deref(XREGS[arity+1]);
  case -2:
    return Deref(XREGS[arity+1]);
  case -3:
    if (!Yap_ExpandPreAllocCodeSpace(0,NULL)) {
      Yap_Error(OUT_OF_AUXSPACE_ERROR, TermNil, Yap_ErrorMessage);
      return 0L;
    }
    return Deref(XREGS[arity+1]);
  default:
    return 0L;
  }
}

static Term
CopyTerm(Term inp, UInt arity, int share, int newattvs) {
  Term t = Deref(inp);

  if (IsVarTerm(t)) {
#if COROUTINING
    if (newattvs && IsAttachedTerm(t)) {
      CELL *Hi;
      int res;
    restart_attached:

      *H = t;
      Hi = H+1;
      H += 2;
      if ((res = copy_complex_term(Hi-2, Hi-1, share, newattvs, Hi, Hi)) < 0) {
	H = Hi-1;
	if ((t = handle_cp_overflow(res,arity,t))== 0L)
	  return FALSE;
	goto restart_attached;
      }
      return Hi[0];
    }
#endif
    return MkVarTerm();
  } else if (IsPrimitiveTerm(t)) {
    return t;
  } else if (IsPairTerm(t)) {
    Term tf;
    CELL *ap;
    CELL *Hi;

  restart_list:
    ap = RepPair(t);
    Hi = H;
    tf = AbsPair(H);
    H += 2;
    {
      int res;
      if ((res = copy_complex_term(ap-1, ap+1, share, newattvs, Hi, Hi)) < 0) {
	H = Hi;
	if ((t = handle_cp_overflow(res,arity,t))== 0L)
	  return FALSE;
	goto restart_list;
      } else if (res && share && FunctorOfTerm(t) != FunctorMutable) {
	H = Hi;
	return t;
      }
    }
    return tf;
  } else {
    Functor f = FunctorOfTerm(t);
    Term tf;
    CELL *HB0;
    CELL *ap;

  restart_appl:
    f = FunctorOfTerm(t);
    HB0 = H;
    ap = RepAppl(t);
    tf = AbsAppl(H);
    H[0] = (CELL)f;
    H += 1+ArityOfFunctor(f);
    if (H > ASP-128) {
      H = HB0;
      if ((t = handle_cp_overflow(-1,arity,t))== 0L)
	return FALSE;
      goto restart_appl;
    } else {
      int res;

      if ((res = copy_complex_term(ap, ap+ArityOfFunctor(f), share, newattvs, HB0+1, HB0)) < 0) {
	H = HB0;
	if ((t = handle_cp_overflow(res,arity,t))== 0L)
	  return FALSE;
	goto restart_appl;
      } else if (res && share) {
	H = HB0;
	return t;
      }
    }
    return tf;
  }
}
 
Term
Yap_CopyTerm(Term inp) {
  return CopyTerm(inp, 0, TRUE, TRUE);
}

static Int 
p_copy_term(void)		/* copy term t to a new instance  */
{
  Term t = CopyTerm(ARG1, 2, TRUE, TRUE); 
  if (t == 0L)
    return FALSE;
  /* be careful, there may be a stack shift here */
  return Yap_unify(ARG2,t);
}

static Int 
p_duplicate_term(void)		/* copy term t to a new instance  */
{
  Term t = CopyTerm(ARG1, 2, FALSE, TRUE); 
  if (t == 0L)
    return FALSE;
  /* be careful, there may be a stack shift here */
  return Yap_unify(ARG2,t);
}

static Int 
p_copy_term_no_delays(void)		/* copy term t to a new instance  */
{
  Term t = CopyTerm(ARG1, 2, TRUE, FALSE);
  if (t == 0L) {
    return FALSE;
  }
  /* be careful, there may be a stack shift here */
  return(Yap_unify(ARG2,t));
}


static Term vars_in_complex_term(register CELL *pt0, register CELL *pt0_end, Term inp)
{

  register CELL **to_visit0, **to_visit = (CELL **)Yap_PreAllocCodeSpace();
  register tr_fr_ptr TR0 = TR;
  CELL *InitialH = H;
  CELL output = AbsPair(H);

  to_visit0 = to_visit;
 loop:
  while (pt0 < pt0_end) {
    register CELL d0;
    register CELL *ptd0;
    ++ pt0;
    ptd0 = pt0;
    d0 = *ptd0;
    deref_head(d0, vars_in_term_unk);
  vars_in_term_nvar:
    {
      if (IsPairTerm(d0)) {
#ifdef RATIONAL_TREES
	to_visit[0] = pt0;
	to_visit[1] = pt0_end;
	to_visit[2] = (CELL *)*pt0;
	to_visit += 3;
	*pt0 = TermNil;
#else
	if (pt0 < pt0_end) {
	  to_visit[0] = pt0;
	  to_visit[1] = pt0_end;
	  to_visit += 2;
	}
#endif
	pt0 = RepPair(d0) - 1;
	pt0_end = RepPair(d0) + 1;
      } else if (IsApplTerm(d0)) {
	register Functor f;
	register CELL *ap2;
	/* store the terms to visit */
	ap2 = RepAppl(d0);
	f = (Functor)(*ap2);

	if (IsExtensionFunctor(f)) {
	  continue;
	}
	/* store the terms to visit */
#ifdef RATIONAL_TREES
	to_visit[0] = pt0;
	to_visit[1] = pt0_end;
	to_visit[2] = (CELL *)*pt0;
	to_visit += 3;
	*pt0 = TermNil;
#else
	if (pt0 < pt0_end) {
	  to_visit[0] = pt0;
	  to_visit[1] = pt0_end;
	  to_visit += 2;
	}
#endif
	d0 = ArityOfFunctor(f);
	pt0 = ap2;
	pt0_end = ap2 + d0;
      }
      continue;
    }
    

    derefa_body(d0, ptd0, vars_in_term_unk, vars_in_term_nvar);
    /* do or pt2 are unbound  */
    *ptd0 = TermNil;
    /* leave an empty slot to fill in later */
    if (H+1024 > ASP) {
      goto global_overflow;
    }
    H[1] = AbsPair(H+2);
    H += 2;
    H[-2] = (CELL)ptd0;
    /* next make sure noone will see this as a variable again */ 
    if (TR > (tr_fr_ptr)Yap_TrailTop - 256) {
      /* Trail overflow */
      if (!Yap_growtrail((TR-TR0)*sizeof(tr_fr_ptr *), TRUE)) {
	goto trail_overflow;
      }
    }
    TrailTerm(TR++) = (CELL)ptd0;
  }
  /* Do we still have compound terms to visit */
  if (to_visit > to_visit0) {
#ifdef RATIONAL_TREES
    to_visit -= 3;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    *pt0 = (CELL)to_visit[2];
#else
    to_visit -= 2;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
#endif
    goto loop;
  }

  clean_tr(TR0);
  Yap_ReleasePreAllocCodeSpace((ADDR)to_visit0);
  if (H != InitialH) {
    /* close the list */
    Term t2 = Deref(inp);
    if (IsVarTerm(t2)) {
      RESET_VARIABLE(H-1);
      Yap_unify((CELL)(H-1),ARG2);
    } else {
      H[-1] = t2;		/* don't need to trail */
    }
    return(output);
  } else {
    return(inp);
  }

 trail_overflow:
#ifdef RATIONAL_TREES
  while (to_visit > to_visit0) {
    to_visit -= 3;
    pt0 = to_visit[0];
    *pt0 = (CELL)to_visit[2];
  }
#endif
  Yap_Error_TYPE = OUT_OF_TRAIL_ERROR;
  Yap_Error_Size = (TR-TR0)*sizeof(tr_fr_ptr *);
  clean_tr(TR0);
  Yap_ReleasePreAllocCodeSpace((ADDR)to_visit0);
  H = InitialH;
  return 0L;
  
 global_overflow:
#ifdef RATIONAL_TREES
  while (to_visit > to_visit0) {
    to_visit -= 3;
    pt0 = to_visit[0];
    *pt0 = (CELL)to_visit[2];
  }
#endif
  clean_tr(TR0);
  Yap_ReleasePreAllocCodeSpace((ADDR)to_visit0);
  H = InitialH;
  Yap_Error_TYPE = OUT_OF_STACK_ERROR;
  Yap_Error_Size = (ASP-H)*sizeof(CELL);
  return 0L;
  
}

static int
expand_vts(void)
{
  UInt expand = Yap_Error_Size;
  yap_error_number yap_errno = Yap_Error_TYPE;
  
  Yap_Error_Size = 0;
  Yap_Error_TYPE = YAP_NO_ERROR;
  if (yap_errno == OUT_OF_TRAIL_ERROR) {
    /* Trail overflow */
    if (!Yap_growtrail(expand, FALSE)) {
      return FALSE;
    }
  } else {
    if (!Yap_gcl(expand, 3, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, "in term_variables");
      return FALSE;
    }
  }
  return TRUE;
}
 
static Int 
p_variables_in_term(void)	/* variables in term t		 */
{
  Term out;

  do {
    Term t = Deref(ARG1);
    if (IsVarTerm(t)) {
      out = AbsPair(H);
      H += 2;
      RESET_VARIABLE(H-2);
      RESET_VARIABLE(H-1);
      Yap_unify((CELL)(H-2),ARG1);
      Yap_unify((CELL)(H-1),ARG2);
    }  else if (IsPrimitiveTerm(t)) 
      out = ARG2;
    else if (IsPairTerm(t)) {
      out = vars_in_complex_term(RepPair(t)-1,
				 RepPair(t)+1, ARG2);
    }
    else {
      Functor f = FunctorOfTerm(t);
      out = vars_in_complex_term(RepAppl(t),
				 RepAppl(t)+
				 ArityOfFunctor(f), ARG2);
    }
    if (out == 0L) {
      if (!expand_vts())
	return FALSE;
    }
  } while (out == 0L);
  return(Yap_unify(ARG3,out));
}

static Int 
p_term_variables(void)	/* variables in term t		 */
{
  Term out;

  do {
    Term t = Deref(ARG1);
    if (IsVarTerm(t)) {
      return Yap_unify(MkPairTerm(t,TermNil), ARG2);
    }  else if (IsPrimitiveTerm(t)) {
      return Yap_unify(TermNil, ARG2);
    } else if (IsPairTerm(t)) {
      out = vars_in_complex_term(RepPair(t)-1,
				 RepPair(t)+1, TermNil);
    }
    else {
      Functor f = FunctorOfTerm(t);
      out = vars_in_complex_term(RepAppl(t),
				 RepAppl(t)+
				 ArityOfFunctor(f), TermNil);
    }
    if (out == 0L) {
      if (!expand_vts())
	return FALSE;
    }
  } while (out == 0L);
  return Yap_unify(ARG2,out);
}

static Int 
p_term_variables3(void)	/* variables in term t		 */
{
  Term out;

  do {
    Term t = Deref(ARG1);
    if (IsVarTerm(t)) {
      return Yap_unify(MkPairTerm(t,ARG3), ARG2);
    }  else if (IsPrimitiveTerm(t)) {
      return Yap_unify(ARG2, ARG3);
    } else if (IsPairTerm(t)) {
      out = vars_in_complex_term(RepPair(t)-1,
				 RepPair(t)+1, ARG3);
    }
    else {
      Functor f = FunctorOfTerm(t);
      out = vars_in_complex_term(RepAppl(t),
				 RepAppl(t)+
				 ArityOfFunctor(f), ARG3);
    }
    if (out == 0L) {
      if (!expand_vts())
	return FALSE;
    }
  } while (out == 0L);

  return Yap_unify(ARG2,out);
}

static Term non_singletons_in_complex_term(register CELL *pt0, register CELL *pt0_end)
{

  register CELL **to_visit0, **to_visit = (CELL **)Yap_PreAllocCodeSpace();
  register tr_fr_ptr TR0 = TR;
  CELL *InitialH = H;
  CELL output = AbsPair(H);

  to_visit0 = to_visit;
 loop:
  while (pt0 < pt0_end) {
    register CELL d0;
    register CELL *ptd0;
    ++ pt0;
    ptd0 = pt0;
    d0 = *ptd0;
    deref_head(d0, vars_in_term_unk);
  vars_in_term_nvar:
    {
      if (IsPairTerm(d0)) {
	if (to_visit + 1024 >= (CELL **)AuxSp) {
	  goto aux_overflow;
	}
#ifdef RATIONAL_TREES
	to_visit[0] = pt0;
	to_visit[1] = pt0_end;
	to_visit[2] = (CELL *)*pt0;
	to_visit += 3;
	*pt0 = TermNil;
#else
	if (pt0 < pt0_end) {
	  to_visit[0] = pt0;
	  to_visit[1] = pt0_end;
	  to_visit += 2;
	}
#endif
	pt0 = RepPair(d0) - 1;
	pt0_end = RepPair(d0) + 1;
      } else if (IsApplTerm(d0)) {
	register Functor f;
	register CELL *ap2;
	/* store the terms to visit */
	ap2 = RepAppl(d0);
	f = (Functor)(*ap2);

	if (IsExtensionFunctor(f)) {

	  continue;
	}
	if (to_visit + 1024 >= (CELL **)AuxSp) {
	  goto aux_overflow;
	}
#ifdef RATIONAL_TREES
	to_visit[0] = pt0;
	to_visit[1] = pt0_end;
	to_visit[2] = (CELL *)*pt0;
	to_visit += 3;
	*pt0 = TermNil;
#else
	/* store the terms to visit */
	if (pt0 < pt0_end) {
	  to_visit[0] = pt0;
	  to_visit[1] = pt0_end;
	  to_visit += 2;
	}
#endif
	d0 = ArityOfFunctor(f);
	pt0 = ap2;
	pt0_end = ap2 + d0;
      } else if (d0 == TermFoundVar) {
	CELL *pt2 = pt0;
	while(IsVarTerm(*pt2))
	  pt2 = (CELL *)(*pt2);
	H[1] = AbsPair(H+2);
	H += 2;
	H[-2] = (CELL)pt2;
	*pt2 = TermReFoundVar;
      }
      continue;
    }
    

    derefa_body(d0, ptd0, vars_in_term_unk, vars_in_term_nvar);
    /* do or pt2 are unbound  */
    *ptd0 = TermFoundVar;
    /* next make sure we can recover the variable again */ 
    TrailTerm(TR++) = (CELL)ptd0;
  }
  /* Do we still have compound terms to visit */
  if (to_visit > to_visit0) {
#ifdef RATIONAL_TREES
    to_visit -= 3;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    *pt0 = (CELL)to_visit[2];
#else
    to_visit -= 2;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
#endif
    goto loop;
  }

  clean_tr(TR0);
  if (H != InitialH) {
    /* close the list */
    RESET_VARIABLE(H-1);
    Yap_unify((CELL)(H-1),ARG2);
    return output;
  } else {
    return ARG2;
  }

 aux_overflow:
#ifdef RATIONAL_TREES
  while (to_visit > to_visit0) {
    to_visit -= 3;
    pt0 = to_visit[0];
    *pt0 = (CELL)to_visit[2];
  }
#endif
  clean_tr(TR0);
  if (H != InitialH) {
    /* close the list */
    RESET_VARIABLE(H-1);
  }
  return 0L;
}
 
static Int 
p_non_singletons_in_term(void)	/* non_singletons in term t		 */
{
  Term t;
  Term out;
	
  while (TRUE) {
    t = Deref(ARG1);
    if (IsVarTerm(t)) {
      out = MkPairTerm(t,ARG2);
    }  else if (IsPrimitiveTerm(t)) {
      out = ARG2;
    } else if (IsPairTerm(t)) {
      out = non_singletons_in_complex_term(RepPair(t)-1,
					   RepPair(t)+1);
    } else {
      out = non_singletons_in_complex_term(RepAppl(t),
					   RepAppl(t)+
					   ArityOfFunctor(FunctorOfTerm(t)));
    }
    if (out != 0L) {
      return Yap_unify(ARG3,out);
    } else {
      if (!Yap_ExpandPreAllocCodeSpace(0, NULL)) {
	Yap_Error(OUT_OF_AUXSPACE_ERROR, ARG1, "overflow in singletons");
	return FALSE;
      }
    }
  }  
}

static Int ground_complex_term(register CELL *pt0, register CELL *pt0_end)
{

  register CELL **to_visit0, **to_visit = (CELL **)Yap_PreAllocCodeSpace();

  to_visit0 = to_visit;
 loop:
  while (pt0 < pt0_end) {
    register CELL d0;
    register CELL *ptd0;

    ++pt0;
    ptd0 = pt0;
    d0 = *ptd0;
    deref_head(d0, vars_in_term_unk);
  vars_in_term_nvar:
    {
      if (IsPairTerm(d0)) {
	if (to_visit + 1024 >= (CELL **)AuxSp) {
	  goto aux_overflow;
	}
#ifdef RATIONAL_TREES
	to_visit[0] = pt0;
	to_visit[1] = pt0_end;
	to_visit[2] = (CELL *)*pt0;
	to_visit += 3;
	*pt0 = TermNil;
#else
	if (pt0 < pt0_end) {
	  to_visit[0] = pt0;
	  to_visit[1] = pt0_end;
	  to_visit += 2;
	}
#endif
	pt0 = RepPair(d0) - 1;
	pt0_end = RepPair(d0) + 1;
      } else if (IsApplTerm(d0)) {
	register Functor f;
	register CELL *ap2;
	/* store the terms to visit */
	ap2 = RepAppl(d0);
	f = (Functor)(*ap2);

	if (IsExtensionFunctor(f)) {
	  continue;
	}
	if (to_visit + 1024 >= (CELL **)AuxSp) {
	  goto aux_overflow;
	}
#ifdef RATIONAL_TREES
	to_visit[0] = pt0;
	to_visit[1] = pt0_end;
	to_visit[2] = (CELL *)*pt0;
	to_visit += 3;
	*pt0 = TermNil;
#else
	/* store the terms to visit */
	if (pt0 < pt0_end) {
	  to_visit[0] = pt0;
	  to_visit[1] = pt0_end;
	  to_visit += 2;
	}
#endif
	d0 = ArityOfFunctor(f);
	pt0 = ap2;
	pt0_end = ap2 + d0;
      }
      continue;
    }
    

    derefa_body(d0, ptd0, vars_in_term_unk, vars_in_term_nvar);
#ifdef RATIONAL_TREES
    while (to_visit > to_visit0) {
      to_visit -= 3;
      pt0 = to_visit[0];
      pt0_end = to_visit[1];
      *pt0 = (CELL)to_visit[2];
    }
#endif
    return FALSE;
  }
  /* Do we still have compound terms to visit */
  if (to_visit > to_visit0) {
#ifdef RATIONAL_TREES
    to_visit -= 3;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    *pt0 = (CELL)to_visit[2];
#else
    to_visit -= 2;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
#endif
    goto loop;
  }
  return TRUE;

 aux_overflow:
  /* unwind stack */
#ifdef RATIONAL_TREES
  while (to_visit > to_visit0) {
    to_visit -= 3;
    pt0 = to_visit[0];
    *pt0 = (CELL)to_visit[2];
  }
#endif
  return -1;
}
 
static Int 
p_ground(void)			/* ground(+T)		 */
{
  Term t;

  while (TRUE) {
    Int out;

    t = Deref(ARG1);
    if (IsVarTerm(t)) {
      return FALSE;
    }  else if (IsPrimitiveTerm(t)) {
      return TRUE;
    } else if (IsPairTerm(t)) {
      if ((out =ground_complex_term(RepPair(t)-1,
				    RepPair(t)+1)) >= 0) {
	return out;
      }
    } else {
      Functor fun = FunctorOfTerm(t);
      
      if (IsExtensionFunctor(fun))
	return(TRUE);
      else if ((out = ground_complex_term(RepAppl(t),
					     RepAppl(t)+
					     ArityOfFunctor(fun))) >= 0) {
	     return out;
      }
    }
    if (out < 0) {
      if (!Yap_ExpandPreAllocCodeSpace(0, NULL)) {
	Yap_Error(OUT_OF_AUXSPACE_ERROR, ARG1, "overflow in ground");
	return FALSE;
      }      
    }
  }
}

static Int var_in_complex_term(register CELL *pt0,
			       register CELL *pt0_end,
			       Term v)
{

  register CELL **to_visit0, **to_visit = (CELL **)Yap_PreAllocCodeSpace();
  register tr_fr_ptr TR0 = TR;

  to_visit0 = to_visit;
 loop:
  while (pt0 < pt0_end) {
    register CELL d0;
    register CELL *ptd0;
    ++ pt0;
    ptd0 = pt0;
    d0 = *ptd0;
    deref_head(d0, var_in_term_unk);
  var_in_term_nvar:
    {
      if (IsPairTerm(d0)) {
#ifdef RATIONAL_TREES
	to_visit[0] = pt0;
	to_visit[1] = pt0_end;
	to_visit[2] = (CELL *)*pt0;
	to_visit += 3;
	*pt0 = TermNil;
#else
	if (pt0 < pt0_end) {
	  to_visit[0] = pt0;
	  to_visit[1] = pt0_end;
	  to_visit += 2;
	}
#endif
	pt0 = RepPair(d0) - 1;
	pt0_end = RepPair(d0) + 1;
      } else if (IsApplTerm(d0)) {
	register Functor f;
	register CELL *ap2;
	/* store the terms to visit */
	ap2 = RepAppl(d0);
	f = (Functor)(*ap2);

	if (IsExtensionFunctor(f)) {

	  continue;
	}
#ifdef RATIONAL_TREES
	to_visit[0] = pt0;
	to_visit[1] = pt0_end;
	to_visit[2] = (CELL *)*pt0;
	to_visit += 3;
	*pt0 = TermNil;
#else
	/* store the terms to visit */
	if (pt0 < pt0_end) {
	  to_visit[0] = pt0;
	  to_visit[1] = pt0_end;
	  to_visit += 2;
	}
#endif
	d0 = ArityOfFunctor(f);
	pt0 = ap2;
	pt0_end = ap2 + d0;
      }
      continue;
    }
    

    deref_body(d0, ptd0, var_in_term_unk, var_in_term_nvar);
    if ((CELL)ptd0 == v) { /* we found it */
      clean_tr(TR0);
      return(TRUE);
    }
    /* do or pt2 are unbound  */
    *ptd0 = TermNil;
    /* next make sure noone will see this as a variable again */ 
    TrailTerm(TR++) = (CELL)ptd0;
  }
  /* Do we still have compound terms to visit */
  if (to_visit > to_visit0) {
#ifdef RATIONAL_TREES
    to_visit -= 3;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    *pt0 = (CELL)to_visit[2];
#else
    to_visit -= 2;
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
#endif
    goto loop;
  }
  clean_tr(TR0);
  return FALSE;
}
 
static Int 
var_in_term(Term v, Term t)		/* variables in term t		 */
{

  if (IsVarTerm(t)) {
    return(v == t);
  } else if (IsPrimitiveTerm(t)) {
    return(FALSE);
  } else if (IsPairTerm(t)) {
    return(var_in_complex_term(RepPair(t)-1,
			       RepPair(t)+1, v));
  }
  else return(var_in_complex_term(RepAppl(t),
				  RepAppl(t)+
				  ArityOfFunctor(FunctorOfTerm(t)),v));
}

static Int
p_var_in_term(void)
{
  return(var_in_term(Deref(ARG2), Deref(ARG1)));
}

/* The code for TermHash was originally contributed by Gertjen Van Noor */

/* This code with max_depth == -1 will loop for infinite trees */

#define GvNht ((UInt *)H)

#define HASHADD(T) (GvNht[k]+=(T), k=(k<2 ? k+1 : 0))

static Int TermHash(Term t1, Int depth_lim, Int k)
{
  Int i;
  if (IsVarTerm(t1)) {
    return(-1);
  } else if (IsAtomOrIntTerm(t1)) {
    if (IsAtomTerm(t1)) {
      register char *s = AtomName(AtomOfTerm(t1));
      for (i=0; s[i]; i++)
	HASHADD(s[i]);
      return k;
    } else {
      HASHADD(IntOfTerm(t1));
      return k;
    }
  } else if (IsPairTerm(t1)) {
    HASHADD('.');
    depth_lim--;
    if (depth_lim == 0) return(TRUE);
    k = TermHash(HeadOfTerm(t1),depth_lim,k);
    if (k < 0) return k;
    return TermHash(TailOfTerm(t1),depth_lim,k);
  } else {
    Functor f = FunctorOfTerm(t1);

    if (IsExtensionFunctor(f)) {
      if (f == FunctorDouble) {
	Int *iptr = (Int *)(RepAppl(t1)+1);
	int i;

	for (i = 0; i < sizeof(Float) / sizeof(CELL); i++) {
	  HASHADD(*iptr++);
	}
	return(k);
      } else if (f == FunctorLongInt) {
	HASHADD(LongIntOfTerm(t1));
	return(k);
      } else if (f == FunctorDBRef) {
	HASHADD((Int)DBRefOfTerm(t1));
	return(k);
	/* should never happen */
      } else {
	return(-1);
      }
    } else {
      int ar;
      char *s;

      s = AtomName(NameOfFunctor(f));
      for (i=0; s[i]; i++) 
	HASHADD(s[i]);
      depth_lim--;
      if (depth_lim == 0) return k;
      ar = ArityOfFunctor(f);
      for (i=1; i<=ar; i++) {
	k = TermHash(ArgOfTerm(i,t1),depth_lim,k);
	if (k < 0) return k;
      }
      return(k);
    }
  }
}

static Int
GvNTermHash(void)
{
  unsigned int i1,i2,i3;
  Term t1 = Deref(ARG1);
  Term t2 = Deref(ARG2);
  Term t3 = Deref(ARG3);
  Term result;
  Int size, depth;


  if (IsVarTerm(t2)) {
    Yap_Error(INSTANTIATION_ERROR,t2,"term_hash/4");
    return(FALSE);
  }
  if (!IsIntegerTerm(t2)) {
    Yap_Error(TYPE_ERROR_INTEGER,t2,"term_hash/4");
    return(FALSE);
  }
  depth = IntegerOfTerm(t2);
  if (depth == 0) {
    if (IsVarTerm(t1)) return(TRUE);
    return(Yap_unify(ARG4,MkIntTerm(0)));
  }
  if (IsVarTerm(t3)) {
    Yap_Error(INSTANTIATION_ERROR,t3,"term_hash/4");
    return(FALSE);
  }
  if (!IsIntegerTerm(t3)) {
    Yap_Error(TYPE_ERROR_INTEGER,t3,"term_hash/4");
    return(FALSE);
  }
  size = IntegerOfTerm(t3);
  GvNht[0] = 0;
  GvNht[1] = 0;
  GvNht[2] = 0;

  if (TermHash(t1,depth,0) == -1) return(TRUE);

  i1 = GvNht[0];
  i2 = GvNht[1];
  i3 = GvNht[2];
  i2 ^= i3; i1 ^= i2; i1 = (((i3 << 7) + i2) << 7) + i1;
  result = MkIntegerTerm(i1 % size);
  return Yap_unify(ARG4,result);
}

static int variant_complex(register CELL *pt0, register CELL *pt0_end, register
		   CELL *pt1)
{
  tr_fr_ptr OLDTR = TR;
  register CELL **to_visit = (CELL **)ASP;
  /* make sure that unification always forces trailing */
  HBREG = H;
 

 loop:
  while (pt0 < pt0_end) {
    register CELL d0, d1;
    ++ pt0;
    ++ pt1;
    d0 = Derefa(pt0);
    d1 = Derefa(pt1);
    if (IsVarTerm(d0)) {
      if (IsVarTerm(d1)) {
	CELL *pt0 = VarOfTerm(d0);
	CELL *pt1 = VarOfTerm(d1);
	if (pt0 >= HBREG || pt1 >= HBREG) {
	  /* one of the variables has been found before */
	  if (VarOfTerm(d0)+1 == VarOfTerm(d1)) continue;
	  goto fail;
	} else {
	  /* two new occurrences of the same variable */
	  Term n0 = MkVarTerm(), n1 = MkVarTerm();
	  Bind_Global(VarOfTerm(d0), n0);
	  Bind_Global(VarOfTerm(d1), n1);
	}
	continue;
      } else {
	goto fail;
      }
    } else if (IsVarTerm(d1)) {
      goto fail;
    } else {
      if (d0 == d1) continue;
      else if (IsAtomOrIntTerm(d0)) {
	goto fail;
      } else if (IsPairTerm(d0)) {
	if (!IsPairTerm(d1)) {
	  goto fail;
	}
#ifdef RATIONAL_TREES
	/* now link the two structures so that no one else will */
	/* come here */
	to_visit -= 4;
	if ((CELL *)to_visit < H+1024)
	  goto out_of_stack;
	to_visit[0] = pt0;
	to_visit[1] = pt0_end;
	to_visit[2] = pt1;
	to_visit[3] = (CELL *)*pt0;
	*pt0 = d1;
#else
	/* store the terms to visit */
	if (pt0 < pt0_end) {
	  to_visit -= 3;
	  if ((CELL *)to_visit < H+1024)
	    goto out_of_stack;
	  to_visit[0] = pt0;
	  to_visit[1] = pt0_end;
	  to_visit[2] = pt1;
	}
#endif
	pt0 = RepPair(d0) - 1;
	pt0_end = RepPair(d0) + 1;
	pt1 = RepPair(d1) - 1;
	continue;
      } else if (IsApplTerm(d0)) {
	register Functor f;
	register CELL *ap2, *ap3;
	if (!IsApplTerm(d1)) {
	  goto fail;
	} else {
	  /* store the terms to visit */
	  Functor f2;
	  ap2 = RepAppl(d0);
	  ap3 = RepAppl(d1);
	  f = (Functor)(*ap2);
	  f2 = (Functor)(*ap3);
	  if (f != f2)
	    goto fail;
	  if (IsExtensionFunctor(f)) {
	    if (!unify_extension(f, d0, ap2, d1))
	      goto fail;
	    continue;
	  }
#ifdef RATIONAL_TREES
	/* now link the two structures so that no one else will */
	/* come here */
	to_visit -= 4;
	if ((CELL *)to_visit < H+1024)
	  goto out_of_stack;
	to_visit[0] = pt0;
	to_visit[1] = pt0_end;
	to_visit[2] = pt1;
	to_visit[3] = (CELL *)*pt0;
	*pt0 = d1;
#else
	  /* store the terms to visit */
	  if (pt0 < pt0_end) {
	    to_visit -= 3;
	    if ((CELL *)to_visit < H+1024)
	      goto out_of_stack;
	    to_visit[0] = pt0;
	    to_visit[1] = pt0_end;
	    to_visit[2] = pt1;
	  }
#endif
	  d0 = ArityOfFunctor(f);
	  pt0 = ap2;
	  pt0_end = ap2 + d0;
	  pt1 = ap3;
	  continue;
	}
      }
    }
  }
  /* Do we still have compound terms to visit */
  if (to_visit < (CELL **)ASP) {
#ifdef RATIONAL_TREES
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    pt1 = to_visit[2];
    *pt0 = (CELL)to_visit[3];
    to_visit += 4;
#else
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    pt1 = to_visit[2];
    to_visit += 3;
#endif
    goto loop;
  }

  H = HBREG;
  /* untrail all bindings made by variant */
  while (TR != (tr_fr_ptr)OLDTR) {
    CELL *pt1 = (CELL *) TrailTerm(--TR);
    RESET_VARIABLE(pt1);
  }
  HBREG = B->cp_h;
  return TRUE;

 out_of_stack:
  H = HBREG;
  /* untrail all bindings made by variant */
#ifdef RATIONAL_TREES
  while (to_visit < (CELL **)ASP) {
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    pt1 = to_visit[2];
    *pt0 = (CELL)to_visit[3];
    to_visit += 4;
  }
#endif
  while (TR != (tr_fr_ptr)OLDTR) {
    CELL *pt1 = (CELL *) TrailTerm(--TR);
    RESET_VARIABLE(pt1);
  }
  HBREG = B->cp_h;
  return -1;


 fail:
  /* failure */
  H = HBREG;
#ifdef RATIONAL_TREES
  while (to_visit < (CELL **)ASP) {
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    pt1 = to_visit[2];
    *pt0 = (CELL)to_visit[3];
    to_visit += 4;
  }
#endif
  /* untrail all bindings made by variant */
  while (TR != (tr_fr_ptr)OLDTR) {
    CELL *pt1 = (CELL *) TrailTerm(--TR);
    RESET_VARIABLE(pt1);
  }
  HBREG = B->cp_h;
  return FALSE;
}

static Int 
p_variant(void) /* variant terms t1 and t2	 */
{
  Term t1 = Deref(ARG1);
  Term t2 = Deref(ARG2);
  int out;

  if (t1 == t2)
    return (TRUE);
  if (IsVarTerm(t1)) {
    if (IsVarTerm(t2))
      return(TRUE);
    return(FALSE);
  } else if (IsVarTerm(t2))
    return(FALSE);
  if (IsAtomOrIntTerm(t1)) {
    return(t1 == t2);
  }
  if (IsPairTerm(t1)) {
    if (IsPairTerm(t2)) {
      out = variant_complex(RepPair(t1)-1,
			    RepPair(t1)+1,
			    RepPair(t2)-1);
      if (out < 0) goto error;
      return out;
    }
    else return (FALSE);
  }
  if (!IsApplTerm(t2)) {
    return FALSE;
  } else {
    Functor f1 = FunctorOfTerm(t1);

    if (f1 != FunctorOfTerm(t2)) return(FALSE);
    if (IsExtensionFunctor(f1)) {
      return(unify_extension(f1, t1, RepAppl(t1), t2));
    }
    out = variant_complex(RepAppl(t1),
			  RepAppl(t1)+ArityOfFunctor(f1),
			  RepAppl(t2));
    if (out < 0) goto error;
    return out;
  }
 error:
  if (out == -1) {
    if (!Yap_gcl((ASP-H)*sizeof(CELL), 2, ENV, P)) {
      Yap_Error(OUT_OF_STACK_ERROR, TermNil, "in variant");
      return FALSE;
    }
    return p_variant();
  }
  return FALSE;
}

static int subsumes_complex(register CELL *pt0, register CELL *pt0_end, register
		   CELL *pt1)
{
  register CELL **to_visit = (CELL **)ASP;
  tr_fr_ptr OLDTR = TR, new_tr;
  UInt write_mode = TRUE;


  HBREG = H;
 loop:
  while (pt0 < pt0_end) {
    register CELL d0, d1;
    Int our_write_mode = write_mode;

    ++ pt0;
    ++ pt1;
    /* this is a version of Derefa that checks whether we are trying to
       do something evil */
    {
      CELL *npt0 = pt0;

    restart_d0:
      if (npt0 >= HBREG) {
	our_write_mode = FALSE;
      }
      d0 = *npt0;
      if (IsVarTerm(d0) &&
	  d0 != (CELL)npt0
	  ) {
	npt0 = (CELL *)d0;
	goto restart_d0;
      }
    }
    {
      CELL *npt1 = pt1;

    restart_d1:
      d1 = *npt1;
      if (IsVarTerm(d1)
	  && d1 != (CELL)npt1
	  ) {
	/* never dereference through a variable from the left-side */
	if (npt1 >= HBREG) {
	  goto fail;
	} else {
	  npt1 = (CELL *)d1;
	  goto restart_d1;
	}
      }
    }
    if (IsVarTerm(d0)) {
      if (our_write_mode) {
	/* generate a new binding */
	CELL *pt0 = VarOfTerm(d0);
	Term new = MkVarTerm();
	  
	Bind_Global(pt0, new);
	if (d0 != d1) { /* avoid loops */
	  Bind_Global(VarOfTerm(new), d1);
	}
      } else {
	if (d0 == d1) continue;
	goto fail;
      }
      continue;
    } else if (IsVarTerm(d1)) {
      goto fail;
    } else {
      if (d0 == d1) continue;
      else if (IsAtomOrIntTerm(d0)) {
	goto fail;
      } else if (IsPairTerm(d0)) {
	if (!IsPairTerm(d1)) {
	  goto fail;
	}
#ifdef RATIONAL_TREES
	/* now link the two structures so that no one else will */
	/* come here */
	to_visit -= 5;
	to_visit[0] = pt0;
	to_visit[1] = pt0_end;
	to_visit[2] = pt1;
	to_visit[3] = (CELL *)*pt0;
	to_visit[4] = (CELL *)write_mode;
	*pt0 = d1;
#else
	/* store the terms to visit */
	if (pt0 < pt0_end) {
	  to_visit -= 4;
	  to_visit[0] = pt0;
	  to_visit[1] = pt0_end;
	  to_visit[2] = pt1;
	  to_visit[3] = (CELL *)write_mode;
	}
#endif
	write_mode = our_write_mode;
	pt0 = RepPair(d0) - 1;
	pt0_end = RepPair(d0) + 1;
	pt1 = RepPair(d1) - 1;
	continue;
      } else if (IsApplTerm(d0)) {
	register Functor f;
	register CELL *ap2, *ap3;
	if (!IsApplTerm(d1)) {
	  goto fail;
	} else {
	  /* store the terms to visit */
	  Functor f2;
	  ap2 = RepAppl(d0);
	  ap3 = RepAppl(d1);
	  f = (Functor)(*ap2);
	  f2 = (Functor)(*ap3);
	  if (f != f2)
	    goto fail;
	  if (IsExtensionFunctor(f)) {
	    if (!unify_extension(f, d0, ap2, d1))
	      goto fail;
	    continue;
	  }
#ifdef RATIONAL_TREES
	  /* now link the two structures so that no one else will */
	  /* come here */
	  to_visit -= 5;
	  to_visit[0] = pt0;
	  to_visit[1] = pt0_end;
	  to_visit[2] = pt1;
	  to_visit[3] = (CELL *)*pt0;
	  to_visit[4] = (CELL *)write_mode;
	  *pt0 = d1;
#else
	  /* store the terms to visit */
	  if (pt0 < pt0_end) {
	    to_visit -= 4;
	    to_visit[0] = pt0;
	    to_visit[1] = pt0_end;
	    to_visit[2] = pt1;
	    to_visit[3] = (CELL *)write_mode;
	  }
#endif
	  write_mode = our_write_mode;
	  d0 = ArityOfFunctor(f);
	  pt0 = ap2;
	  pt0_end = ap2 + d0;
	  pt1 = ap3;
	  continue;
	}
      }
    }
  }
  /* Do we still have compound terms to visit */
  if (to_visit < (CELL **)ASP) {
#ifdef RATIONAL_TREES
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    pt1 = to_visit[2];
    *pt0 = (CELL)to_visit[3];
    write_mode = (Int)to_visit[ 4];
    to_visit += 5;
#else
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    pt1 = to_visit[2];
    write_mode = (UInt)to_visit[3];
    to_visit += 4;
#endif
    goto loop;
  }

  H = HBREG;
  /* get rid of intermediate variables  */
  new_tr = TR;
  while (TR != OLDTR) {
    /* cell we bound */
    CELL *pt1 = (CELL *) TrailTerm(--TR);
    /* cell we created */
    CELL *npt1 = (CELL *)*pt1;
    /* shorten the chain */
    if (IsVarTerm(*pt1) && IsUnboundVar(pt1)) {
      RESET_VARIABLE(pt1);
    } else {
      *pt1 = *npt1;
    }
  }
  TR = new_tr;
  HBREG = B->cp_h;
  return TRUE;

 fail:
  H = HBREG;
#ifdef RATIONAL_TREES
  while (to_visit < (CELL **)ASP) {
    pt0 = to_visit[0];
    pt0_end = to_visit[1];
    pt1 = to_visit[2];
    *pt0 = (CELL)to_visit[3];
    to_visit += 5;
  }
#endif
  /* untrail all bindings made by variant */
  while (TR != (tr_fr_ptr)OLDTR) {
    CELL *pt1 = (CELL *) TrailTerm(--TR);
    RESET_VARIABLE(pt1);
  }
  HBREG = B->cp_h;
  return FALSE;
}

static Int 
p_subsumes(void) /* subsumes terms t1 and t2	 */
{
  Term t1 = Deref(ARG1);
  Term t2 = Deref(ARG2);

  if (t1 == t2)
    return (TRUE);
  if (IsVarTerm(t1)) {
    Bind(VarOfTerm(t1), t2);
    return(TRUE);
  } else if (IsVarTerm(t2))
    return(FALSE);
  if (IsAtomOrIntTerm(t1)) {
    return(t1 == t2);
  }
  if (IsPairTerm(t1)) {
    if (IsPairTerm(t2)) {
      return(subsumes_complex(RepPair(t1)-1,
			     RepPair(t1)+1,
			     RepPair(t2)-1));
    }
    else return (FALSE);
  } else {
    Functor f1;

    if (!IsApplTerm(t2)) return(FALSE);
    f1 = FunctorOfTerm(t1);
    if (f1 != FunctorOfTerm(t2))
      return(FALSE);
    if (IsExtensionFunctor(f1)) {
      return(unify_extension(f1, t1, RepAppl(t1), t2));
    }
    return(subsumes_complex(RepAppl(t1),
			   RepAppl(t1)+ArityOfFunctor(f1),
			   RepAppl(t2)));
  }
}

#ifdef DEBUG
static Int
p_force_trail_expansion()
{
  Int i = IntOfTerm(Deref(ARG1))*1024, j = 0;
  tr_fr_ptr OTR = TR;

  for (j = 0; j < i; j++) {
    TrailTerm(TR) = 0;
    TR++;
  }
  TR = OTR;

  return(TRUE);
}

static Int
camacho_dum(void)
{
  Term t1, t2;
  int  max = 3;

  /* build output list */

  t1 = MkAtomTerm(Yap_LookupAtom("[]"));
      t2 = MkPairTerm(MkIntegerTerm(max), t1);

  return(Yap_unify(t2, ARG1));
}



#endif /* DEBUG */

void Yap_InitUtilCPreds(void)
{
  Term cm = CurrentModule;
  Yap_InitCPred("copy_term", 2, p_copy_term, 0);
  Yap_InitCPred("duplicate_term", 2, p_duplicate_term, 0);
  Yap_InitCPred("copy_term_nat", 2, p_copy_term_no_delays, 0);
  Yap_InitCPred("ground", 1, p_ground, SafePredFlag);
  Yap_InitCPred("$variables_in_term", 3, p_variables_in_term, HiddenPredFlag);
  Yap_InitCPred("$non_singletons_in_term", 3, p_non_singletons_in_term, SafePredFlag|HiddenPredFlag);
  CurrentModule = TERMS_MODULE;
  Yap_InitCPred("term_variables", 2, p_term_variables, 0);
  Yap_InitCPred("term_variables", 3, p_term_variables3, 0);
  Yap_InitCPred("variable_in_term", 2, p_var_in_term, SafePredFlag);
  Yap_InitCPred("term_hash", 4, GvNTermHash, SafePredFlag);
  Yap_InitCPred("variant", 2, p_variant, 0);
  Yap_InitCPred("subsumes", 2, p_subsumes, SafePredFlag);
  CurrentModule = cm;
#ifdef DEBUG
  Yap_InitCPred("$force_trail_expansion", 1, p_force_trail_expansion, SafePredFlag|HiddenPredFlag);
  Yap_InitCPred("dum", 1, camacho_dum, SafePredFlag);
#endif
}

