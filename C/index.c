/*************************************************************************
*									 *
*	 Yap Prolog 							 *
*									 *
*	Yap Prolog was developed at NCCUP - Universidade do Porto	 *
*									 *
* Copyright L.Damas, V.S.Costa and Universidade do Porto 1985-1997	 *
*									 *
**************************************************************************
*									 *
* File:		index.c							 *
* comments:	Indexing a Prolog predicate				 *
*									 *
* Last rev:     $Date: 2004-03-25 02:19:10 $,$Author: pmoura $						 *
* $Log: not supported by cvs2svn $
* Revision 1.80  2004/03/19 11:35:42  vsc
* trim_trail for default machine
* be more aggressive about try-retry-trust chains.
*    - handle cases where block starts with a wait
*    - don't use _killed instructions, just let the thing rot by itself.
*                                                                  *
*									 *
*************************************************************************/
#ifdef SCCS
static char     SccsId[] = "%W% %G%";
#endif

/*
 * This file compiles and removes the indexation code for the prolog compiler 
 *
 * Some remarks: *try_me always point to inside the code;
 * try always points to outside 
 *

 Algorithm:

 - fetch info on all clauses
 - if #clauses =1  return
 - compute groups:
    seq of variable only clauses
    seq: of one or more type instructions
         bound clauses
 - sort group
 - select constant
          --> type instructions
          --> count constants
          --> switch
	       for all arguments:
	       select new argument 

 */

#include "absmi.h"
#include "compile.h"
#include "index.h"
#ifdef DEBUG
#include "yapio.h"
#endif
#ifndef NULL
#define NULL (void *)0
#endif
#if HAVE_STRING_H
#include <string.h>
#endif

UInt STATIC_PROTO(do_index, (ClauseDef *,ClauseDef *,struct intermediates *,UInt,UInt,int,int,CELL *));
UInt STATIC_PROTO(do_compound_index, (ClauseDef *,ClauseDef *,Term *t,struct intermediates *,UInt,UInt,UInt,UInt,int,int,int,CELL *,int));
UInt STATIC_PROTO(do_dbref_index, (ClauseDef *,ClauseDef *,Term,struct intermediates *,UInt,UInt,int,int,CELL *));
UInt STATIC_PROTO(do_blob_index, (ClauseDef *,ClauseDef *,Term,struct intermediates *,UInt,UInt,int,int,CELL *));

static UInt labelno;

static inline int
smaller(Term t1, Term t2)
{
  CELL tg1 = TagOf(t1), tg2 = TagOf(t2);
  if (tg1 == tg2) {
    return t1 < t2;
  } else
    return tg1 < tg2;
}

static inline int
smaller_or_eq(Term t1, Term t2)
{
  CELL tg1 = TagOf(t1), tg2 = TagOf(t2);
  if (tg1 == tg2) {
    return t1 <= t2;
  } else
    return tg1 < tg2;
}

static inline void
clcpy(ClauseDef *d, ClauseDef *s)
{
  memcpy((void *)d, (void *)s, sizeof(ClauseDef));
}

static void
insort(ClauseDef base[], CELL *p, CELL *q, int my_p)
{
  CELL *j;

  if (my_p) {
    p[1] = p[0];
    for (j = p+2; j < q; j += 2) {
      Term key;
      Int off = *j;
      CELL *i;

      key = base[off].Tag;
      i = j+1;
    
      /* we are at offset 1 */
      while (i > p+1 && smaller(key,base[i[-2]].Tag)) {
	i[0] = i[-2];
	i -= 2;
      }
      i[0] = off;
    }
  } else {
    for (j = p+2; j < q; j += 2) {
      Term key;
      Int off = *j;
      CELL *i;

      key = base[off].Tag;
      i = j;
    
      /* we are at offset 1 */
      while (i > p && smaller(key,base[i[-2]].Tag)) {
	i[0] = i[-2];
	i -= 2;
      }
      i[0] = off;
    }
  }
}


/* copy to a new list of terms */
static
void msort(ClauseDef *base, Int *pt, Int size, int my_p)
{

  if (size > 2) {
    Int half_size = size / 2;
    Int *pt_left, *pt_right, *end_pt, *end_pt_left;
    int left_p, right_p;

    if (size < 50) {
       insort(base, pt, pt+2*size, my_p);
       return;
    }
    pt_right = pt + half_size*2;
    left_p = my_p^1;
    right_p = my_p;
    msort(base, pt, half_size, left_p);
    msort(base, pt_right, size-half_size, right_p);
    /* now implement a simple merge routine */
    
    /* pointer to after the end of the list */
    end_pt = pt + 2*size;
    /* pointer to the element after the last element to the left */
    end_pt_left = pt+half_size*2;
    /* where is left list */
    pt_left = pt+left_p;
    /* where is right list */
    pt_right += right_p;
    /* where is new list */
    pt += my_p;
    /* while there are elements in the left or right vector do compares */
    while (pt_left < end_pt_left && pt_right < end_pt) {
      /* if the element to the left is larger than the one to the right */
      if (smaller_or_eq(base[pt_left[0]].Tag, base[pt_right[0]].Tag)) {
	/* copy the one to the left */
	pt[0] = pt_left[0];
	/* and avance the two pointers */
	pt += 2;
	pt_left += 2;
      } else {
	/* otherwise, copy the one to the right */
	pt[0] = pt_right[0];
	pt += 2;
	pt_right += 2;
      }
    }
    /* if any elements were left in the left vector just copy them */
    while (pt_left < end_pt_left) {
      pt[0] = pt_left[0];
      pt += 2;
      pt_left += 2;
    }
    /* if any elements were left in the right vector
       and they are in the wrong place, just copy them */
    if (my_p != right_p) {
      while(pt_right < end_pt) {
	pt[0] = pt_right[0];
	pt += 2;
	pt_right += 2;
      }
    }
  } else {
    if (size > 1 && smaller(base[pt[2]].Tag,base[pt[0]].Tag)) {
      CELL t = pt[2];
      pt[2+my_p] = pt[0];
      pt[my_p] = t;
    } else if (my_p) {
      pt[1] = pt[0];
      if (size > 1)
	pt[3] = pt[2];
    }
  }
}

static void
copy_back(ClauseDef *dest, CELL *pt, int max) {
  /* first need to say that we had no need to make a copy */
  int i;
  CELL *tmp = pt;
  for (i=0; i < max; i++) {
    if (*tmp != i) {
      ClauseDef cl;
      int j = i;
      CELL *pnt = tmp;

      /* found a chain */
      /* make a backup copy */
      clcpy(&cl, dest+i);
      do {
	/* follow the chain */
	int k = *pnt;

	*pnt = j;
	/*	printf("i=%d, k = %d, j = %d\n",i,j,k); */
	if (k == i) {
	  clcpy(dest+j, &cl);
	  break;
	} else {
	  clcpy(dest+j, dest+k);
	}
	pnt = pt+2*k;
	j = k;
      } while (TRUE);
    }
    /* we don't need to do swap */
    tmp += 2;
  }
}

/* sort a group of clauses by using their tags */
static void
sort_group(GroupDef *grp, CELL *top, struct intermediates *cint)
{
  int max = (grp->LastClause-grp->FirstClause)+1, i;
  CELL *pt = top;

  while (top+2*max > (CELL *)Yap_TrailTop) {
#if USE_SYSTEM_MALLOC
    Yap_Error_Size = 2*max*sizeof(CELL);
    /* grow stack */
    longjmp(cint->CompilerBotch,4);
#else
    if (!Yap_growtrail(2*max*CellSize)) {
      Yap_Error(SYSTEM_ERROR,TermNil,"YAP failed to reserve %ld in growtrail",
		2*max*CellSize);
      return;
    }
#endif
  }
  /* initialise vector */
  for (i=0; i < max; i++) {
    *pt = i;
    pt += 2;
  }
#define M_EVEN  0
  msort(grp->FirstClause, top, max, M_EVEN);
  copy_back(grp->FirstClause, top, max);
}

/* add copy to register stack for original reg */
static int
add_regcopy(wamreg regs[MAX_REG_COPIES], int regs_count, wamreg copy)
{
  if (regs_count == MAX_REG_COPIES) {
    regs[0] = copy;
  }
  regs[regs_count] = copy;
  return regs_count+1;
}

/* add copy to register stack for original reg */
static int
init_regcopy(wamreg regs[MAX_REG_COPIES], wamreg copy)
{
  regs[0] = copy;
  return 1;
}

/* add copy to register stack for original reg */
static int
delete_regcopy(wamreg regs[MAX_REG_COPIES], int regs_count, wamreg copy)
{
  int i = 0;
  while (i < regs_count) {
    if (regs[i] == copy) {
      /* we found it */
      regs[i] = regs[MAX_REG_COPIES-1];
      return regs_count-1;
    }
    i++;
  }
  /* this copy had overflowed, or it just was not there */
  return regs_count;
}

/* add copy to register stack for original reg */
inline static int
regcopy_in(wamreg regs[MAX_REG_COPIES], int regs_count, wamreg copy)
{
  int i = 0;
  do {
    if (regs[i] == copy) {
      return TRUE;
    }
    i++;
  } while (i < regs_count);
  /* this copy could not be found */
  return FALSE;
}

/* Restores a prolog clause, in its compiled form */
#if YAPOR
static int 
has_cut(yamop *pc)
/*
 * Cl points to the start of the code, IsolFlag tells if we have a single
 * clause for this predicate or not 
 */
{
  do {
    op_numbers op = Yap_op_from_opcode(pc->opc);
    switch (op) {
    case _Ystop:
    case _Nstop:
      return FALSE;
      /* instructions type ld */
    case _cut:
    case _cut_t:
    case _cut_e:
    case _p_cut_by_y:
    case _p_cut_by_x:
    case _commit_b_y:
    case _commit_b_x:
      return TRUE;
    case _try_me:
    case _retry_me:
    case _trust_me:
    case _profiled_retry_me:
    case _profiled_trust_me:
    case _count_retry_me:
    case _count_trust_me:
    case _try_me0:
    case _retry_me0:
    case _trust_me0:
    case _try_me1:
    case _retry_me1:
    case _trust_me1:
    case _try_me2:
    case _retry_me2:
    case _trust_me2:
    case _try_me3:
    case _retry_me3:
    case _trust_me3:
    case _try_me4:
    case _retry_me4:
    case _trust_me4:
    case _spy_or_trymark:
    case _try_and_mark:
    case _profiled_retry_and_mark:
    case _count_retry_and_mark:
    case _retry_and_mark:
    case _try_clause:
    case _retry:
    case _trust:
#ifdef YAPOR
    case _getwork:
    case _getwork_seq:
    case _sync:
#endif
#ifdef TABLING
    case _table_try_me_single:
    case _table_try_me:
    case _table_retry_me:
    case _table_trust_me:
    case _table_answer_resolution:
    case _table_completion:
#endif
      pc = NEXTOP(pc,ld);
      break;
      /* instructions type Ill */
    case _enter_lu_pred:
    case _stale_lu_index:
      pc = pc->u.Ill.l1;
      break;
      /* instructions type l */
    case _enter_profiling:
    case _count_call:
    case _retry_profiled:
    case _count_retry:
    case _trust_logical_pred:
    case _execute:
    case _dexecute:
    case _jump:
    case _move_back:
    case _skip:
    case _jump_if_var:
    case _try_in:
      pc = NEXTOP(pc,l);
      break;
    case _jump_if_nonvar:
      pc = NEXTOP(pc,xl);
      break;
      /* instructions type EC */
    case _alloc_for_logical_pred:
      pc = NEXTOP(pc,EC);
      break;
      /* instructions type e */
    case _trust_fail:
    case _op_fail:
    case _procceed:
    case _allocate:
    case _deallocate:
    case _write_void:
    case _write_list:
    case _write_l_list:
#if !defined(YAPOR)
    case _or_last:
#endif
    case _pop:
    case _index_pred:
#if THREADS
    case _thread_local:
#endif
    case _expand_index:
    case _undef_p:
    case _spy_pred:
    case _p_equal:
    case _p_dif:
    case _p_eq:
    case _p_functor:
    case _p_execute_tail:
    case _enter_a_profiling:
    case _count_a_call:
    case _index_dbref:
    case _index_blob:
#ifdef YAPOR
    case _getwork_first_time:
#endif
#ifdef TABLING
    case _trie_do_var:
    case _trie_trust_var:
    case _trie_try_var:
    case _trie_retry_var:
    case _trie_do_val:
    case _trie_trust_val:
    case _trie_try_val:
    case _trie_retry_val:
    case _trie_do_atom:
    case _trie_trust_atom:
    case _trie_try_atom:
    case _trie_retry_atom:
    case _trie_do_list:
    case _trie_trust_list:
    case _trie_try_list:
    case _trie_retry_list:
    case _trie_do_struct:
    case _trie_trust_struct:
    case _trie_try_struct:
    case _trie_retry_struct:
#endif
      pc = NEXTOP(pc,e);
      break;
      /* instructions type x */
    case _save_b_x:
    case _get_list:
    case _put_list:
    case _write_x_var:
    case _write_x_val:
    case _write_x_loc:
      pc = NEXTOP(pc,x);
      break;
      /* instructions type xF */
    case _p_atom_x:
    case _p_atomic_x:
    case _p_integer_x:
    case _p_nonvar_x:
    case _p_number_x:
    case _p_var_x:
    case _p_db_ref_x:
    case _p_primitive_x:
    case _p_compound_x:
    case _p_float_x:
    case _p_cut_by_x:
      pc = NEXTOP(pc,xF);
      break;
      /* instructions type y */
    case _save_b_y:
    case _write_y_var:
    case _write_y_val: 
    case _write_y_loc:
      pc = NEXTOP(pc,y);
      break;
      /* instructions type yF */
    case _p_atom_y:
    case _p_atomic_y:
    case _p_integer_y:
    case _p_nonvar_y:
    case _p_number_y:
    case _p_var_y:
    case _p_db_ref_y:
    case _p_primitive_y:
    case _p_compound_y:
    case _p_float_y:
    case _p_cut_by_y:
      pc = NEXTOP(pc,yF);
      break;
      /* instructions type sla */
    case _p_execute:
    case _fcall:
    case _call:
#ifdef YAPOR
    case _or_last:
#endif
      pc = NEXTOP(pc,sla);
      break;
      /* instructions type sla, but for disjunctions */
    case _either:
    case _or_else:
      pc = NEXTOP(pc,sla);
      break;
      /* instructions type sla, but for functions */
    case _call_cpred:
    case _call_usercpred:
      pc = NEXTOP(pc,sla);
      break;
      /* instructions type xx */
    case _get_x_var:
    case _get_x_val:
    case _glist_valx:
    case _gl_void_varx:
    case _gl_void_valx:
    case _put_x_var:
    case _put_x_val:
      pc = NEXTOP(pc,xx);
      break;
      /* instructions type yx */
    case _get_y_var:
    case _get_y_val:
    case _put_y_var:
    case _put_y_val:
    case _put_unsafe:
      pc = NEXTOP(pc,yx);
      break;
      /* instructions type xc */
    case _get_atom:
    case _put_atom:
    case _get_float:
    case _get_longint:
    case _get_bigint:
      pc = NEXTOP(pc,xc);
      break;
      /* instructions type xf */
    case _get_struct:
    case _put_struct:
      pc = NEXTOP(pc,xf);
      break;
      /* instructions type xy */
    case _glist_valy:
    case _gl_void_vary:
    case _gl_void_valy:
      pc = NEXTOP(pc,xy);
      break;
      /* instructions type ox */
    case _unify_x_var:
    case _unify_x_var_write:
    case _unify_l_x_var:
    case _unify_l_x_var_write:
    case _unify_x_val_write:
    case _unify_x_val:
    case _unify_l_x_val_write:
    case _unify_l_x_val:
    case _unify_x_loc_write:
    case _unify_x_loc:
    case _unify_l_x_loc_write:
    case _unify_l_x_loc:
    case _save_pair_x_write:
    case _save_pair_x:
    case _save_appl_x_write:
    case _save_appl_x:
      pc = NEXTOP(pc,ox);
      break;
      /* instructions type oxx */
    case _unify_x_var2:
    case _unify_x_var2_write:
    case _unify_l_x_var2:
    case _unify_l_x_var2_write:
      pc = NEXTOP(pc,oxx);
      break;
      /* instructions type oy */
    case _unify_y_var:
    case _unify_y_var_write:
    case _unify_l_y_var:
    case _unify_l_y_var_write:
    case _unify_y_val_write:
    case _unify_y_val:
    case _unify_l_y_val_write:
    case _unify_l_y_val:
    case _unify_y_loc_write:
    case _unify_y_loc:
    case _unify_l_y_loc_write:
    case _unify_l_y_loc:
    case _save_pair_y_write:
    case _save_pair_y:
    case _save_appl_y_write:
    case _save_appl_y:
      pc = NEXTOP(pc,oy);
      break;
      /* instructions type o */
    case _unify_void_write:
    case _unify_void:
    case _unify_l_void_write:
    case _unify_l_void:
    case _unify_list_write:
    case _unify_list:
    case _unify_l_list_write:
    case _unify_l_list:
      pc = NEXTOP(pc,o);
      break;
      /* instructions type os */
    case _unify_n_voids_write:
    case _unify_n_voids:
    case _unify_l_n_voids_write:
    case _unify_l_n_voids:
      pc = NEXTOP(pc,os);
      break;
      /* instructions type oc */
    case _unify_atom_write:
    case _unify_atom:
    case _unify_l_atom_write:
    case _unify_l_atom:
    case _unify_float:
    case _unify_l_float:
    case _unify_longint:
    case _unify_l_longint:
    case _unify_bigint:
    case _unify_l_bigint:
      pc = NEXTOP(pc,oc);
      break;
      /* instructions type osc */
    case _unify_n_atoms_write:
    case _unify_n_atoms:
      pc = NEXTOP(pc,osc);
      break;
      /* instructions type of */
    case _unify_struct_write:
    case _unify_struct:
    case _unify_l_struc_write:
    case _unify_l_struc:
      pc = NEXTOP(pc,of);
      break;
      /* instructions type s */
    case _write_n_voids:
    case _pop_n:
#ifdef TABLING
    case _table_new_answer:
#endif
      pc = NEXTOP(pc,s);
      break;
      /* instructions type ps */
   case _write_atom:
      pc = NEXTOP(pc,c);
      break;
      /* instructions type sc */
   case _write_n_atoms:
      pc = NEXTOP(pc,sc);
      break;
      /* instructions type f */
   case _write_struct:
   case _write_l_struc:
      pc = NEXTOP(pc,f);
      break;
      /* instructions type sdl */
    case _call_c_wfail:
      pc = NEXTOP(pc,sdl);
      break;
      /* instructions type lds */
    case _try_c:
    case _try_userc:
      pc = NEXTOP(pc,lds);
      break;
    case _retry_c:
    case _retry_userc:
      pc = NEXTOP(pc,lds);
      break;
      /* instructions type llll */
    case _switch_on_type:
      return FALSE;
      break;
    case _switch_list_nl:
      return FALSE;
      break;
    case _switch_on_arg_type:
      return FALSE;
      break;
    case _switch_on_sub_arg_type:
      return FALSE;
      /* instructions type lll */
      /* instructions type cll */
    case _if_not_then:
      return FALSE;
      /* instructions type sl */
    case _switch_on_func:
    case _switch_on_cons:
    case _go_on_func:
    case _go_on_cons:
    case _if_func:
    case _if_cons:
      return FALSE;
      /* instructions type xxx */
    case _p_plus_vv:
    case _p_minus_vv:
    case _p_times_vv:
    case _p_div_vv:
    case _p_and_vv:
    case _p_or_vv:
    case _p_sll_vv:
    case _p_slr_vv:
    case _p_arg_vv:
    case _p_func2s_vv:
    case _p_func2f_xx:
      pc = NEXTOP(pc,xxx);
      break;
      /* instructions type xxc */
    case _p_plus_vc:
    case _p_minus_cv:
    case _p_times_vc:
    case _p_div_cv:
    case _p_and_vc:
    case _p_or_vc:
    case _p_sll_vc:
    case _p_slr_vc:
    case _p_func2s_vc:
      pc = NEXTOP(pc,xxc);
      break;
    case _p_div_vc:
    case _p_sll_cv:
    case _p_slr_cv:
    case _p_arg_cv:
      pc = NEXTOP(pc,xcx);
      break;
    case _p_func2s_cv:
      pc = NEXTOP(pc,xcx);
      break;
      /* instructions type xyx */
    case _p_func2f_xy:
      pc = NEXTOP(pc,xyx);
      break;
      /* instructions type yxx */
    case _p_plus_y_vv:
    case _p_minus_y_vv:
    case _p_times_y_vv:
    case _p_div_y_vv:
    case _p_and_y_vv:
    case _p_or_y_vv:
    case _p_sll_y_vv:
    case _p_slr_y_vv:
    case _p_arg_y_vv:
    case _p_func2s_y_vv:
    case _p_func2f_yx:
      pc = NEXTOP(pc,yxx);
      break;
      /* instructions type yyx */
    case _p_func2f_yy:
      pc = NEXTOP(pc,yyx);
      break;
      /* instructions type yxc */
    case _p_plus_y_vc:
    case _p_minus_y_cv:
    case _p_times_y_vc:
    case _p_div_y_vc:
    case _p_div_y_cv:
    case _p_and_y_vc:
    case _p_or_y_vc:
    case _p_sll_y_vc:
    case _p_slr_y_vc:
    case _p_func2s_y_vc:
      pc = NEXTOP(pc,yxc);
      break;
      /* instructions type ycx */
    case _p_sll_y_cv:
    case _p_slr_y_cv:
    case _p_arg_y_cv:
      pc = NEXTOP(pc,ycx);
      break;
      /* instructions type ycx */
    case _p_func2s_y_cv:
      pc = NEXTOP(pc,ycx);
      break;
      /* instructions type llxx */
    case _call_bfunc_xx:
      pc = NEXTOP(pc,llxx);
      break;
      /* instructions type llxy */
    case _call_bfunc_yx:
    case _call_bfunc_xy:
      pc = NEXTOP(pc,llxy);
      break;
    case _call_bfunc_yy:
      pc = NEXTOP(pc,llyy);
      break;
    }
  } while (TRUE);
}
#else
#define has_cut(pc) 0
#endif /* YAPOR */

static void 
add_info(ClauseDef *clause, UInt regno)
{
  wamreg myregs[MAX_REG_COPIES];
  int nofregs;
  yslot ycopy = 0;
  yamop *cl;
  
  nofregs = init_regcopy(myregs, Yap_regnotoreg(regno));
  cl = clause->CurrentCode;
  while (TRUE) {
    op_numbers op = Yap_op_from_opcode(cl->opc);
    switch (op) {
    case _alloc_for_logical_pred:
      cl = NEXTOP(cl,EC);
      break;
    case _cut:
    case _cut_t:
    case _cut_e:
    case _allocate:
    case _deallocate:
    case _write_void:
    case _write_list:
    case _write_l_list:
    case _enter_a_profiling:
    case _count_a_call:
      cl = NEXTOP(cl,e);
      break;
    case _save_b_x:
    case _commit_b_x:
    case _write_x_val:
    case _write_x_loc:
    case _write_x_var:
    case _put_list:
      if (regcopy_in(myregs, nofregs, cl->u.x.x)) {
	clause->Tag = (CELL)NULL;
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,x);
      break;
    case _p_nonvar_x:
      if (regcopy_in(myregs, nofregs, cl->u.xF.x)) {
	clause->Tag = (CELL)NULL;
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xF);
      break;
    case _p_number_x:
      if (regcopy_in(myregs, nofregs, cl->u.xF.x)) {
	clause->Tag = (_number+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xF);
      break;
    case _p_atomic_x:
      if (regcopy_in(myregs, nofregs, cl->u.xF.x)) {
	clause->Tag = (_atomic+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xF);
      break;
    case _p_integer_x:
      if (regcopy_in(myregs, nofregs, cl->u.xF.x)) {
	clause->Tag = (_integer+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xF);
      break;
    case _p_primitive_x:
      if (regcopy_in(myregs, nofregs, cl->u.xF.x)) {
	clause->Tag = (_primitive+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xF);
      break;
    case _p_compound_x:
      if (regcopy_in(myregs, nofregs, cl->u.xF.x)) {
	clause->Tag = (_compound+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xF);
      break;
    case _p_var_x:
      if (regcopy_in(myregs, nofregs, cl->u.xF.x)) {
	clause->Tag = (_var+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xF);
      break;
    case _p_db_ref_x:
      if (regcopy_in(myregs, nofregs, cl->u.xF.x)) {
	clause->Tag = AbsAppl((CELL *)FunctorDBRef);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xF);
      break;
    case _p_float_x:
      if (regcopy_in(myregs, nofregs, cl->u.xF.x)) {
	clause->Tag = AbsAppl((CELL *)FunctorDouble);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xF);
      break;
    case _p_atom_x:
      if (regcopy_in(myregs, nofregs, cl->u.xF.x)) {
	clause->Tag = (_atom+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xF);
      break;
    case _get_list:
      if (regcopy_in(myregs, nofregs, cl->u.x.x)) {
	clause->Tag = AbsPair(NULL);
	clause->u.WorkPC = NEXTOP(cl,x);
	return;
      }
      cl = NEXTOP(cl,x);
      break;
    case _p_cut_by_x:
      cl = NEXTOP(cl,xF);
      break;
    case _save_b_y:
    case _commit_b_y:
    case _write_y_var:
    case _write_y_val: 
    case _write_y_loc:
      if (cl->u.y.y == ycopy) {
	clause->Tag = (CELL)NULL;
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,y);
      break;
    case _p_nonvar_y:
      if (cl->u.yF.y == ycopy) {
	clause->Tag = (CELL)NULL;
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yF);
      break;
    case _p_atomic_y:
      if (ycopy == cl->u.yF.y) {
	clause->Tag = (_atomic+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yF);
      break;
    case _p_integer_y:
      if (ycopy == cl->u.yF.y) {
	clause->Tag = (_integer+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yF);
      break;
    case _p_number_y:
      if (ycopy == cl->u.yF.y) {
	clause->Tag = (_number+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yF);
      break;
    case _p_primitive_y:
      if (ycopy == cl->u.yF.y) {
	clause->Tag = (_primitive+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yF);
      break;
    case _p_compound_y:
      if (ycopy == cl->u.yF.y) {
	clause->Tag = (_compound+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yF);
      break;
    case _p_db_ref_y:
      if (ycopy == cl->u.yF.y) {
	clause->Tag = AbsAppl((CELL *)FunctorDBRef);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yF);
      break;
    case _p_float_y:
      if (ycopy == cl->u.yF.y) {
	clause->Tag = AbsAppl((CELL *)FunctorDouble);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yF);
      break;
    case _p_atom_y:
      if (cl->u.yF.y == ycopy) {
	clause->Tag = (_atom+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yF);
      break;
    case _p_var_y:
      if (cl->u.yF.y == ycopy) {
	clause->Tag = (_var+1)*sizeof(CELL);
	clause->u.t_ptr = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yF);
      break;
    case _p_cut_by_y:
      cl = NEXTOP(cl,yF);
      break;
    case _p_execute:
    case _fcall:
    case _call:
#ifdef YAPOR
    case _or_last:
#endif
    case _either:
    case _or_else:
    case _call_cpred:
    case _call_usercpred:
      clause->Tag = (CELL)NULL;
      return;
    case _get_x_var:
      if (regcopy_in(myregs, nofregs, cl->u.xx.xr)) {
	nofregs = add_regcopy(myregs, nofregs, cl->u.xx.xl);
	cl = NEXTOP(cl,xx);
	break;
      }
    case _put_x_var:
      /* if the last slot I am using, get out */
      if (regcopy_in(myregs, nofregs, cl->u.xx.xl) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.xx.xl)) == 0 &&
	  !ycopy) {
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xx);
      break;
    case _get_x_val:
      /* alias two registers */
      if (regcopy_in(myregs, nofregs, cl->u.xx.xl)) {
	nofregs = add_regcopy(myregs, nofregs, cl->u.xx.xr);
      } else if (regcopy_in(myregs, nofregs, cl->u.xx.xr)) {
	nofregs = add_regcopy(myregs, nofregs, cl->u.xx.xl);
      } 
      cl = NEXTOP(cl,xx);
      break;
    case _put_x_val:
      if (regcopy_in(myregs, nofregs, cl->u.xx.xl)) {
	nofregs = add_regcopy(myregs, nofregs, cl->u.xx.xr);
      } else if (regcopy_in(myregs, nofregs, cl->u.xx.xr) &&
		 (nofregs = delete_regcopy(myregs, nofregs, cl->u.xx.xr)) == 0 &&
		 !ycopy) {
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xx);
      break;
    case _glist_valx:
    case _gl_void_varx:
    case _gl_void_valx:
      if (regcopy_in(myregs, nofregs, cl->u.xx.xl)) {
	clause->u.WorkPC = cl;
	clause->Tag = AbsPair(NULL);
	return;
      }
      cl = NEXTOP(cl,xx);
      break;
    case _get_y_var:
      if (regcopy_in(myregs, nofregs, cl->u.yx.x)) {
	ycopy = cl->u.yx.y;
      }
    case _put_y_var:
      cl = NEXTOP(cl,yx);
      break;
    case _put_y_val:
    case _put_unsafe:
      if (ycopy == cl->u.yx.y) {
	nofregs = add_regcopy(myregs, nofregs, cl->u.yx.x);
      } else {
	nofregs = delete_regcopy(myregs, nofregs, cl->u.yx.x);
      }
      cl = NEXTOP(cl,yx);
      break;      
    case _get_y_val:
      if (regcopy_in(myregs, nofregs, cl->u.yx.x)) {
	ycopy = cl->u.yx.y;
      } else if (ycopy == cl->u.yx.y) {
	nofregs = add_regcopy(myregs, nofregs, cl->u.yx.x);
      }
      cl = NEXTOP(cl,yx);
      break;
    case _get_atom:
      if (regcopy_in(myregs, nofregs, cl->u.xc.x)) {
	clause->Tag = cl->u.xc.c;
	return;
      } else {
	cl = NEXTOP(cl,xc);
      }
      break;
    case _get_float:
      if (regcopy_in(myregs, nofregs, cl->u.xc.x)) {
	clause->u.t_ptr = cl->u.xc.c;
	clause->Tag = AbsAppl((CELL *)FunctorDouble);
	return;
      } else {
	cl = NEXTOP(cl,xc);
      }
      break;
    case _get_longint:
      if (regcopy_in(myregs, nofregs, cl->u.xc.x)) {
	clause->u.t_ptr = cl->u.xc.c;
	clause->Tag = AbsAppl((CELL *)FunctorLongInt);
	return;
      } else {
	cl = NEXTOP(cl,xc);
      }
      break;
   case _get_bigint:
      if (regcopy_in(myregs, nofregs, cl->u.xc.x)) {
	clause->u.t_ptr = cl->u.xc.c;
#ifdef USE_GMP
	clause->Tag = AbsAppl((CELL *)FunctorBigInt);
#else
	clause->Tag = AbsAppl((CELL *)FunctorLongInt);	
#endif
	return;
      } else {
	cl = NEXTOP(cl,xc);
      }
      break;
    case _copy_idb_term:
    case _unify_idb_term:
      if (regno == 2) {
	LogUpdClause *lcl = ClauseCodeToLogUpdClause(cl);
	Term t = lcl->ClSource->Entry;
	if (IsVarTerm(t)) {
	  clause->Tag = (CELL)NULL;
	} else if (IsApplTerm(t)) {
	  CELL *pt = RepAppl(t);

	  clause->Tag = AbsAppl((CELL *)pt[0]);
	  clause->u.c_sreg = pt;
	} else if (IsPairTerm(t)) {
	  CELL *pt = RepPair(t);

	  clause->Tag = AbsPair(NULL);
	  clause->u.c_sreg = pt-1;
	} else {
	  clause->Tag = t;
	}
      } else {
	clause->Tag = (CELL)NULL;
      }
      return;
    case _put_atom:
      if (regcopy_in(myregs, nofregs, cl->u.xc.x) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.xc.x)) == 0 &&
	  !ycopy) {
	clause->Tag = (CELL)NULL;
	return;
      } else {
	cl = NEXTOP(cl,xc);
      }
      break;
    case _get_struct:
      if (regcopy_in(myregs, nofregs, cl->u.xf.x)) {
	clause->u.WorkPC = NEXTOP(cl,xf);
	clause->Tag = AbsAppl((CELL *)cl->u.xf.f);
	return;
      } else {
	cl = NEXTOP(cl,xf);
      }
      break;
    case _put_struct:
      if (regcopy_in(myregs, nofregs, cl->u.xf.x) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.xf.x)) == 0 &&
	  !ycopy) {
	clause->Tag = (CELL)NULL;
	return;
      } else {
	cl = NEXTOP(cl,xf);
      }
      break;
    case _glist_valy:
    case _gl_void_vary:
    case _gl_void_valy:
      if (regcopy_in(myregs, nofregs, cl->u.xy.x)) {
	clause->u.WorkPC = cl;
	clause->Tag = AbsPair(NULL);
	return;
      }
      cl = NEXTOP(cl,xy);
      break;
    case _unify_x_var:
    case _unify_x_var_write:
    case _unify_l_x_var:
    case _unify_l_x_var_write:
      if (regcopy_in(myregs, nofregs, cl->u.ox.x) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.ox.x)) == 0 &&
	  !ycopy) {
	/* we just initialised the argument, so nothing can happen now */
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,ox);
      break;
    case _unify_x_val_write:
    case _unify_x_val:
    case _unify_l_x_val_write:
    case _unify_l_x_val:
    case _unify_x_loc_write:
    case _unify_x_loc:
    case _unify_l_x_loc_write:
    case _unify_l_x_loc:
      /* we're just done with the head of a list, but there
	 is nothing inside.
       */
      cl = NEXTOP(cl,ox);
      break;
    case _save_pair_x_write:
    case _save_pair_x:
    case _save_appl_x_write:
    case _save_appl_x:
      if (regcopy_in(myregs, nofregs, cl->u.ox.x) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.ox.x)) == 0 &&
	  !ycopy) {
	/* we just initialised the argument, so nothing can happen now */
	clause->Tag = (CELL)NULL;
	return;	
      }
      cl = NEXTOP(cl,ox);
      break;
    case _unify_x_var2:
    case _unify_x_var2_write:
    case _unify_l_x_var2:
    case _unify_l_x_var2_write:
      if (regcopy_in(myregs, nofregs, cl->u.oxx.xl) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.oxx.xl)) == 0 &&
	  !ycopy) {
	/* we just initialised the argument, so nothing can happen now */
	clause->Tag = (CELL)NULL;
	return;
      }
      if (regcopy_in(myregs, nofregs, cl->u.oxx.xr) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.oxx.xr)) == 0 &&
	  !ycopy) {
	/* we just initialised the argument, so nothing can happen now */
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,oxx);
      break;
    case _unify_y_var:
    case _unify_y_var_write:
    case _unify_l_y_var:
    case _unify_l_y_var_write:
      /* we're just done with the head of a list, but there
	 is nothing inside.
       */
      if (cl->u.oy.y == ycopy) {
	ycopy = 0;	/* weird stuff, let's just reset ycopy */
	if (nofregs == 0) {
	  clause->Tag = (CELL)NULL;
	  return;
	}
      }
      cl = NEXTOP(cl,oy);
      break;
    case _unify_y_val_write:
    case _unify_y_val:
    case _unify_l_y_val_write:
    case _unify_l_y_val:
    case _unify_y_loc_write:
    case _unify_y_loc:
    case _unify_l_y_loc_write:
    case _unify_l_y_loc:
      /* we're just done with the head of a list, but there
	 is nothing inside.
       */
      cl = NEXTOP(cl,oy);
      break;
    case _save_pair_y_write:
    case _save_pair_y:
    case _save_appl_y_write:
    case _save_appl_y:
      if (cl->u.oy.y == ycopy) {
	ycopy = 0;	/* weird stuff, let's just reset ycopy */
	if (nofregs == 0) {
	  clause->Tag = (CELL)NULL;
	  return;
	}
      }
      cl = NEXTOP(cl,oy);
      break;
    case _unify_void_write:
    case _unify_void:
    case _unify_l_void_write:
    case _unify_l_void:
      /* we're just done with the head of a list, but there
	 is nothing inside.
       */
      cl = NEXTOP(cl,o);
      break;
    case _unify_list_write:
    case _unify_list:
    case _unify_l_list_write:
    case _unify_l_list:
      cl = NEXTOP(cl,o);
      break;      
    case _unify_n_voids_write:
    case _unify_n_voids:
    case _unify_l_n_voids_write:
    case _unify_l_n_voids:
      cl = NEXTOP(cl,os);
      break;      
    case _unify_atom_write:
    case _unify_atom:
    case _unify_l_atom_write:
    case _unify_l_atom:
      cl = NEXTOP(cl,oc);
      break;      
    case _unify_float:
    case _unify_l_float:
      cl = NEXTOP(cl,oc);
      break;      
    case _unify_longint:
    case _unify_l_longint:
      cl = NEXTOP(cl,oc);
      break;      
    case _unify_bigint:
    case _unify_l_bigint:
      cl = NEXTOP(cl,oc);
      break;      
    case _unify_n_atoms_write:
    case _unify_n_atoms:
      cl = NEXTOP(cl,osc);
      break;      
    case _unify_struct_write:
    case _unify_struct:
    case _unify_l_struc_write:
    case _unify_l_struc:
      cl = NEXTOP(cl,of);
      break;      
    case _write_n_voids:
    case _pop_n:
      cl = NEXTOP(cl,s);
      break;      
    case _write_atom:
      cl = NEXTOP(cl,c);
      break;
    case _write_n_atoms:
      cl = NEXTOP(cl,sc);
      break;
   case _write_struct:
   case _write_l_struc:
      cl = NEXTOP(cl,f);
      break;
    case _call_c_wfail:
    case _try_c:
    case _try_userc:
    case _retry_c:
    case _retry_userc:
    case _switch_on_type:
    case _switch_list_nl:
    case _switch_on_arg_type:
    case _switch_on_sub_arg_type:
    case _if_not_then:
    case _switch_on_func:
    case _switch_on_cons:
    case _go_on_func:
    case _go_on_cons:
    case _if_func:
    case _if_cons:
      clause->Tag = (CELL)NULL;
      return;
    case _p_plus_vv:
    case _p_minus_vv:
    case _p_times_vv:
    case _p_div_vv:
    case _p_and_vv:
    case _p_or_vv:
    case _p_sll_vv:
    case _p_slr_vv:
    case _p_arg_vv:
    case _p_func2s_vv:
    case _p_func2f_xx:
      if (regcopy_in(myregs, nofregs, cl->u.xxx.x) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.xxx.x)) == 0 &&
	  !ycopy) {
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xxx);
      break;
    case _p_plus_vc:
    case _p_minus_cv:
    case _p_times_vc:
    case _p_div_cv:
    case _p_and_vc:
    case _p_or_vc:
    case _p_sll_vc:
    case _p_slr_vc:
    case _p_func2s_vc:
      if (regcopy_in(myregs, nofregs, cl->u.xxc.x) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.xxc.x)) == 0 &&
	  !ycopy) {
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xxc);
      break;
    case _p_div_vc:
    case _p_sll_cv:
    case _p_slr_cv:
    case _p_arg_cv:
    case _p_func2s_cv:
      if (regcopy_in(myregs, nofregs, cl->u.xcx.x) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.xcx.x)) == 0 &&
	  !ycopy) {
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xcx);
      break;
    case _p_func2f_xy:
      if (regcopy_in(myregs, nofregs, cl->u.xyx.x) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.xyx.x)) == 0 &&
	  !ycopy) {
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xyx);
      break;
    case _p_plus_y_vv:
    case _p_minus_y_vv:
    case _p_times_y_vv:
    case _p_div_y_vv:
    case _p_and_y_vv:
    case _p_or_y_vv:
    case _p_sll_y_vv:
    case _p_slr_y_vv:
    case _p_arg_y_vv:
    case _p_func2s_y_vv:
    case _p_func2f_yx:
      if (cl->u.yxx.y == ycopy) {
	ycopy = 0;	/* weird stuff, let's just reset ycopy */
	if (nofregs == 0) {
	  clause->Tag = (CELL)NULL;
	  return;
	}
      }
      cl = NEXTOP(cl,yxx);
      break;
    case _p_func2f_yy:
      if (regcopy_in(myregs, nofregs, cl->u.yyx.x) &&
	  (nofregs = delete_regcopy(myregs, nofregs, cl->u.yyx.x)) == 0 &&
	  !ycopy) {
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yyx);
      break;
    case _p_plus_y_vc:
    case _p_minus_y_cv:
    case _p_times_y_vc:
    case _p_div_y_vc:
    case _p_div_y_cv:
    case _p_and_y_vc:
    case _p_or_y_vc:
    case _p_sll_y_vc:
    case _p_slr_y_vc:
    case _p_func2s_y_vc:
      if (cl->u.yxc.y == ycopy) {
	ycopy = 0;	/* weird stuff, let's just reset ycopy */
	if (nofregs == 0) {
	  clause->Tag = (CELL)NULL;
	  return;
	}
      }
      cl = NEXTOP(cl,yxc);
      break;
    case _p_sll_y_cv:
    case _p_slr_y_cv:
    case _p_arg_y_cv:
    case _p_func2s_y_cv:
      if (cl->u.ycx.y == ycopy) {
	ycopy = 0;	/* weird stuff, let's just reset ycopy */
	if (nofregs == 0) {
	  clause->Tag = (CELL)NULL;
	  return;
	}
      }
      cl = NEXTOP(cl,ycx);
      break;
    case _call_bfunc_xx:
      cl = NEXTOP(cl,llxx);
      break;
    case _call_bfunc_yx:
    case _call_bfunc_xy:
      cl = NEXTOP(cl,llxy);
      break;
    case _call_bfunc_yy:
      cl = NEXTOP(cl,llyy);
      break;
    case _Ystop:
    case _Nstop:
    case _try_me:
    case _retry_me:
    case _trust_me:
    case _profiled_retry_me:
    case _profiled_trust_me:
    case _count_retry_me:
    case _count_trust_me:
    case _try_me0:
    case _retry_me0:
    case _trust_me0:
    case _try_me1:
    case _retry_me1:
    case _trust_me1:
    case _try_me2:
    case _retry_me2:
    case _trust_me2:
    case _try_me3:
    case _retry_me3:
    case _trust_me3:
    case _try_me4:
    case _retry_me4:
    case _trust_me4:
    case _spy_or_trymark:
    case _try_and_mark:
    case _profiled_retry_and_mark:
    case _count_retry_and_mark:
    case _retry_and_mark:
    case _try_clause:
    case _retry:
    case _trust:
    case _enter_lu_pred:
    case _stale_lu_index:
#ifdef YAPOR
    case _getwork:
    case _getwork_seq:
    case _sync:
#endif
#ifdef TABLING
    case _table_try_single:
    case _table_try_me:
    case _table_retry_me:
    case _table_trust_me:
    case _table_try:
    case _table_retry:
    case _table_trust:
    case _table_answer_resolution:
    case _table_completion:
#endif
    case _enter_profiling:
    case _count_call:
    case _retry_profiled:
    case _count_retry:
    case _trust_logical_pred:
    case _execute:
    case _dexecute:
    case _jump:
    case _move_back:
    case _skip:
    case _jump_if_var:
    case _try_in:
    case _lock_lu:
    case _unlock_lu:
      clause->Tag = (CELL)NULL;
      return;
    case _jump_if_nonvar:
      clause->Tag = (CELL)NULL;
      return;
      /* instructions type e */
    case _trust_fail:
    case _op_fail:
    case _procceed:
#if !defined(YAPOR)
    case _or_last:
#endif
    case _pop:
    case _index_pred:
#if THREADS
    case _thread_local:
#endif
    case _expand_index:
    case _undef_p:
    case _spy_pred:
    case _p_equal:
    case _p_dif:
    case _p_eq:
    case _p_functor:
    case _p_execute_tail:
    case _index_dbref:
    case _index_blob:
#ifdef YAPOR
    case _getwork_first_time:
#endif
#ifdef TABLING
    case _table_new_answer:
    case _trie_do_var:
    case _trie_trust_var:
    case _trie_try_var:
    case _trie_retry_var:
    case _trie_do_val:
    case _trie_trust_val:
    case _trie_try_val:
    case _trie_retry_val:
    case _trie_do_atom:
    case _trie_trust_atom:
    case _trie_try_atom:
    case _trie_retry_atom:
    case _trie_do_list:
    case _trie_trust_list:
    case _trie_try_list:
    case _trie_retry_list:
    case _trie_do_struct:
    case _trie_trust_struct:
    case _trie_try_struct:
    case _trie_retry_struct:
#endif
      clause->Tag = (CELL)NULL;
      return;
    }
  }
}

static void 
add_head_info(ClauseDef *clause, UInt regno)
{
  wamreg iarg = Yap_regnotoreg(regno);

  yamop *cl = clause->CurrentCode;
  while (TRUE) {
    op_numbers op = Yap_op_from_opcode(cl->opc);
    switch (op) {
    case _get_list:
      if (cl->u.x.x == iarg) {
	clause->Tag = AbsPair(NULL);
	clause->u.WorkPC = NEXTOP(cl,x);
	return;
      }
      cl = NEXTOP(cl,x);
      break;
    case _get_x_var:
      if (cl->u.xx.xl == iarg) {
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xx);
      break;
    case _get_x_val:
      if (cl->u.xx.xl == iarg ||
	  cl->u.xx.xr == iarg) {
	clause->Tag = (CELL)NULL;
	return;
      } 
      cl = NEXTOP(cl,xx);
      break;
    case _glist_valx:
    case _gl_void_varx:
    case _gl_void_valx:
      if (cl->u.xx.xl == iarg) {
	clause->u.WorkPC = cl;
	clause->Tag = AbsPair(NULL);
	return;
      }
      if (cl->u.xx.xr == iarg) {
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,xx);
      break;
    case _get_y_val:
    case _get_y_var:
      if (cl->u.xx.xr == iarg) {
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,yx);
      break;
    case _get_atom:
      if (cl->u.xc.x == iarg) {
	clause->Tag = cl->u.xc.c;
	return;
      } else {
	cl = NEXTOP(cl,xc);
      }
      break;
    case _get_float:
      if (cl->u.xc.x == iarg) {
	clause->u.t_ptr = cl->u.xc.c;
	clause->Tag = AbsAppl((CELL *)FunctorDouble);
	return;
      } else {
	cl = NEXTOP(cl,xc);
      }
      break;
    case _get_longint:
      if (cl->u.xc.x == iarg) {
	clause->u.t_ptr = cl->u.xc.c;
	clause->Tag = AbsAppl((CELL *)FunctorLongInt);
	return;
      } else {
	cl = NEXTOP(cl,xc);
      }
      break;
   case _get_bigint:
      if (cl->u.xc.x == iarg) {
	clause->u.t_ptr = cl->u.xc.c;
#ifdef USE_GMP
	clause->Tag = AbsAppl((CELL *)FunctorBigInt);
#else
	clause->Tag = AbsAppl((CELL *)FunctorLongInt);	
#endif
	return;
      } else {
	cl = NEXTOP(cl,xc);
      }
      break;
    case _get_struct:
      if (cl->u.xf.x == iarg) {
	clause->u.WorkPC = NEXTOP(cl,xf);
	clause->Tag = AbsAppl((CELL *)cl->u.xf.f);
	return;
      } else {
	cl = NEXTOP(cl,xf);
      }
      break;
    case _glist_valy:
    case _gl_void_vary:
    case _gl_void_valy:
      if (cl->u.xy.x == iarg) {
	clause->u.WorkPC = cl;
	clause->Tag = AbsPair(NULL);
	return;
      }
      cl = NEXTOP(cl,xy);
      break;
    case _unify_x_var:
    case _unify_x_var_write:
    case _unify_l_x_var:
    case _unify_l_x_var_write:
    case _unify_x_val_write:
    case _unify_x_val:
    case _unify_l_x_val_write:
    case _unify_l_x_val:
    case _unify_x_loc_write:
    case _unify_x_loc:
    case _unify_l_x_loc_write:
    case _unify_l_x_loc:
    case _save_pair_x_write:
    case _save_pair_x:
    case _save_appl_x_write:
    case _save_appl_x:
      if (cl->u.ox.x == iarg) {
	/* we just initialised the argument, so nothing can happen now */
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,ox);
      break;
    case _unify_x_var2:
    case _unify_x_var2_write:
    case _unify_l_x_var2:
    case _unify_l_x_var2_write:
      if (cl->u.oxx.xl == iarg ||
	  cl->u.oxx.xr == iarg) {
	/* we just initialised the argument, so nothing can happen now */
	clause->Tag = (CELL)NULL;
	return;
      }
      cl = NEXTOP(cl,oxx);
      break;
    case _unify_y_var:
    case _unify_y_var_write:
    case _unify_l_y_var:
    case _unify_l_y_var_write:
    case _unify_y_val_write:
    case _unify_y_val:
    case _unify_l_y_val_write:
    case _unify_l_y_val:
    case _unify_y_loc_write:
    case _unify_y_loc:
    case _unify_l_y_loc_write:
    case _unify_l_y_loc:
    case _save_pair_y_write:
    case _save_pair_y:
    case _save_appl_y_write:
    case _save_appl_y:
      /* we're just done with the head of a list, but there
	 is nothing inside.
       */
      cl = NEXTOP(cl,oy);
      break;
    case _unify_void_write:
    case _unify_void:
    case _unify_l_void_write:
    case _unify_l_void:
      /* we're just done with the head of a list, but there
	 is nothing inside.
       */
      cl = NEXTOP(cl,o);
      break;
    case _unify_list_write:
    case _unify_list:
    case _unify_l_list_write:
    case _unify_l_list:
      cl = NEXTOP(cl,o);
      break;      
    case _unify_n_voids_write:
    case _unify_n_voids:
    case _unify_l_n_voids_write:
    case _unify_l_n_voids:
      cl = NEXTOP(cl,os);
      break;      
    case _unify_atom_write:
    case _unify_atom:
    case _unify_l_atom_write:
    case _unify_l_atom:
      cl = NEXTOP(cl,oc);
      break;      
    case _unify_float:
    case _unify_l_float:
      cl = NEXTOP(cl,oc);
      break;      
    case _unify_longint:
    case _unify_l_longint:
      cl = NEXTOP(cl,oc);
      break;      
    case _unify_bigint:
    case _unify_l_bigint:
      cl = NEXTOP(cl,oc);
      break;      
    case _unify_n_atoms_write:
    case _unify_n_atoms:
      cl = NEXTOP(cl,osc);
      break;      
    case _unify_struct_write:
    case _unify_struct:
    case _unify_l_struc_write:
    case _unify_l_struc:
      cl = NEXTOP(cl,of);
      break;      
    case _unify_idb_term:
    case _copy_idb_term:
      if (regno == 2) {
	LogUpdClause *lcl = ClauseCodeToLogUpdClause(cl);
	Term t = lcl->ClSource->Entry;

	if (IsVarTerm(t)) {
	  clause->Tag = (CELL)NULL;
	} else if (IsApplTerm(t)) {
	  CELL *pt = RepAppl(t);

	  clause->Tag = AbsAppl((CELL *)pt[0]);
	  if (IsExtensionFunctor(FunctorOfTerm(t))) {
	    clause->u.t_ptr = t;
	  } else {
	    clause->u.c_sreg = pt;
	  }
	} else if (IsPairTerm(t)) {
	  CELL *pt = RepPair(t);

	  clause->Tag = AbsPair(NULL);
	  clause->u.c_sreg = pt-1;
	} else {
	  clause->Tag = t;
	}
      } else {
	clause->Tag = (CELL)NULL;
      }
      return;
    default:
      clause->Tag = (CELL)NULL;
      return;
    }
  }
}

static void 
move_next(ClauseDef *clause, UInt regno)
{
  yamop *cl = clause->CurrentCode;
  wamreg wreg = Yap_regnotoreg(regno);
  op_numbers op = Yap_op_from_opcode(cl->opc);

  switch (op) {
  case _p_db_ref_x:
  case _p_float_x:
    if (wreg == cl->u.x.x) {
      clause->CurrentCode = NEXTOP(cl,x);
    }	
    return;
  case _get_list:
    if (wreg == cl->u.x.x) {
      clause->CurrentCode = NEXTOP(cl,x);
    }	
    return;
  case _glist_valx:
  case _gl_void_vary:
  case _gl_void_valy:
  case _gl_void_varx:
  case _gl_void_valx:
  case _glist_valy:
    return;
  case _get_atom:
  case _get_float:
  case _get_longint:
  case _get_bigint:
    if (wreg == cl->u.xc.x) {
      clause->CurrentCode = NEXTOP(cl,xc);
    }	
    return;
  case _get_struct:
    if (wreg == cl->u.xf.x) {
      clause->CurrentCode = NEXTOP(cl,xf);
    }	
  default:
    clause->CurrentCode = clause->Code;
    return;
  }
}

static void
add_arg_info(ClauseDef *clause, PredEntry *ap, UInt argno)
{
  yamop *cl;
  if (ap->ModuleOfPred == IDB_MODULE) {
    cl = clause->Code;
  } else {
    cl = clause->u.WorkPC;
  }
  while (TRUE) {
    op_numbers op = Yap_op_from_opcode(cl->opc);
    switch (op) {
    case _glist_valx:
      if (argno == 1) {
	clause->Tag = (CELL)NULL;
	return;
      }
      argno--;
      cl = NEXTOP(cl,xx);
      break;
    case _gl_void_vary:
    case _gl_void_valy:
    case _gl_void_varx:
    case _gl_void_valx:
      clause->Tag = (CELL)NULL;
      return;
    case _glist_valy:
      if (argno == 1) {
	clause->Tag = (CELL)NULL;
	return;
      }
      argno--;
      cl = NEXTOP(cl,xy);
      break;
    case _unify_l_x_var:
    case _unify_l_x_val:
    case _unify_l_x_loc:
    case _unify_x_var:
    case _unify_x_val:
    case _unify_x_loc:
      if (argno == 1) {
	clause->Tag = (CELL)NULL;
	return;
      }
      argno--;
    case _unify_l_x_var_write:
    case _unify_l_x_val_write:
    case _unify_l_x_loc_write:
    case _unify_x_var_write:
    case _unify_x_val_write:
    case _unify_x_loc_write:
      cl = NEXTOP(cl,ox);
      break;
    case _save_pair_x_write:
    case _save_pair_x:
    case _save_appl_x_write:
    case _save_appl_x:
      cl = NEXTOP(cl,ox);
      break;
    case _unify_l_x_var2:
    case _unify_x_var2:
      if (argno == 1 || argno == 2) {
	clause->Tag = (CELL)NULL;
	return;
      }
      argno -= 2;
    case _unify_l_x_var2_write:
    case _unify_x_var2_write:
      cl = NEXTOP(cl,oxx);
      break;
    case _unify_y_var:
    case _unify_y_val:
    case _unify_y_loc:
    case _unify_l_y_var:
    case _unify_l_y_val:
    case _unify_l_y_loc:
      /* we're just done with the head of a list, but there
	 is nothing inside.
       */
      if (argno == 1) {
	clause->Tag = (CELL)NULL;
	return;
      }
      argno--;
    case _unify_y_var_write:
    case _unify_y_val_write:
    case _unify_y_loc_write:
    case _unify_l_y_var_write:
    case _unify_l_y_val_write:
    case _unify_l_y_loc_write:
      cl = NEXTOP(cl,oy);
      break;
    case _save_pair_y_write:
    case _save_pair_y:
    case _save_appl_y_write:
    case _save_appl_y:
      cl = NEXTOP(cl,oy);
      break;
    case _unify_l_void:
    case _unify_void:
      if (argno == 1) {
	clause->Tag = (CELL)NULL;
	return;
      }
      argno--;
    case _unify_l_void_write:
    case _unify_void_write:
      cl = NEXTOP(cl,o);
      break;
    case _unify_list:
    case _unify_l_list:
      if (argno == 1) {
	clause->Tag = AbsPair(NULL);
	clause->u.WorkPC = NEXTOP(cl,o);
	return;
      }
      argno += 1; /* 2-1: have two extra arguments to skip */
    case _unify_list_write:
    case _unify_l_list_write:
      cl = NEXTOP(cl,o);
      break;
    case _unify_n_voids:
    case _unify_l_n_voids:
      if (argno <= cl->u.os.s) {
	clause->Tag = (CELL)NULL;
	return;
      }
      argno -= cl->u.os.s;
    case _unify_n_voids_write:
    case _unify_l_n_voids_write:
      cl = NEXTOP(cl,os);
      break;      
    case _unify_atom:
    case _unify_l_atom:
      if (argno == 1) {
	clause->Tag = cl->u.oc.c;
	return;
      }
      argno--;
    case _unify_atom_write:
    case _unify_l_atom_write:
      cl = NEXTOP(cl,oc);
      break;      
    case _unify_float:
    case _unify_l_float:
      if (argno == 1) {
	clause->Tag = AbsAppl((CELL *)FunctorDouble);
	clause->u.t_ptr = cl->u.oc.c;
	return;
      }
      cl = NEXTOP(cl,oc);
      argno--;
      break;
    case _unify_longint:
    case _unify_l_longint:
      if (argno == 1) {
	clause->Tag = AbsAppl((CELL *)FunctorLongInt);
	clause->u.t_ptr = cl->u.oc.c;
	return;
      }
      argno--;
      cl = NEXTOP(cl,oc);
      break;
    case _unify_bigint:
    case _unify_l_bigint:
      if (argno == 1) {
#ifdef USE_GMP
	clause->Tag = AbsAppl((CELL *)FunctorBigInt);
#else
	clause->Tag = AbsAppl((CELL *)FunctorLongInt);	
#endif
	clause->u.t_ptr = cl->u.oc.c;
	return;
      }
      argno--;
      break;
    case _unify_n_atoms:
      if (argno <= cl->u.osc.s) {
	clause->Tag = cl->u.osc.c;
	return;
      }
      argno -= cl->u.osc.s;
    case _unify_n_atoms_write:
      cl = NEXTOP(cl,osc);
      break;      
    case _unify_struct:
    case _unify_l_struc:
      if (argno == 1) {
	clause->Tag = AbsAppl((CELL *)cl->u.of.f);
	clause->u.WorkPC = NEXTOP(cl,of);
	return;
      }
      argno--;
    case _unify_l_struc_write:
    case _unify_struct_write:
      cl = NEXTOP(cl,of);
      break;      
    case _pop:
      cl = NEXTOP(cl,e);
      break;            
    case _pop_n:
      cl = NEXTOP(cl,s);
      break;      
    case _unify_idb_term:
    case _copy_idb_term:
      {
	Term t = clause->u.c_sreg[argno];

	if (IsVarTerm(t)) {
	  clause->Tag = (CELL)NULL;
	} else if (IsApplTerm(t)) {
	  CELL *pt = RepAppl(t);

	  clause->Tag = AbsAppl((CELL *)pt[0]);
	  if (IsExtensionFunctor(FunctorOfTerm(t))) {
	    clause->u.t_ptr = t;
	  } else {
	    clause->u.c_sreg = pt;
	  }
	} else if (IsPairTerm(t)) {
	  CELL *pt = RepPair(t);

	  clause->Tag = AbsPair(NULL);
	  clause->u.c_sreg = pt-1;
	} else {
	  clause->Tag = t;
	}
      }
      return;
    default:
      return;
    }
  }
}

static void
skip_to_arg(ClauseDef *clause, PredEntry *ap, UInt argno, int at_point)
{
  yamop *cl;
  int done = FALSE;
  if (ap->ModuleOfPred == IDB_MODULE) {
    return;
  } else {
    cl = clause->CurrentCode;
  }

  if (!at_point) {
    clause->CurrentCode = clause->Code;
    return;
  }

  while (!done) {
    op_numbers op = Yap_op_from_opcode(cl->opc);
    switch (op) {
    case _unify_void:
      if (argno == 1) {
	clause->CurrentCode = clause->Code;
	return;
      } else {
	argno--;
      }
    case _unify_void_write:
      cl = NEXTOP(cl,o);
      break;
    case _unify_list:
    case _unify_l_list:
    case _unify_atom:
    case _unify_l_atom:
    case _unify_longint:
    case _unify_l_longint:
    case _unify_bigint:
    case _unify_l_bigint:
    case _unify_l_float:
    case _unify_struct:
    case _unify_l_struc:
      if (cl == clause->u.WorkPC) {
	clause->CurrentCode = cl;
      } else {
	clause->CurrentCode = clause->Code;
      }
      return;
    case _unify_list_write:
    case _unify_l_list_write:
      cl = NEXTOP(cl,o);
      break;
    case _unify_n_voids:
    case _unify_l_n_voids:
      if (argno <= cl->u.os.s) {
	clause->CurrentCode = clause->Code;
	return;
      } else {
	argno -= cl->u.os.s;
      }
    case _unify_n_voids_write:
    case _unify_l_n_voids_write:
      cl = NEXTOP(cl,os);
      break;      
    case _unify_atom_write:
    case _unify_l_atom_write:
      cl = NEXTOP(cl,oc);
      break;      
    case _unify_l_struc_write:
    case _unify_struct_write:
      cl = NEXTOP(cl,of);
      break;      
    case _pop:
      cl = NEXTOP(cl,e);
      break;            
    case _pop_n:
      cl = NEXTOP(cl,s);
      break;      
    default:
      clause->CurrentCode = clause->Code;
      return;      
    }
  }
}

static UInt
groups_in(ClauseDef *min, ClauseDef *max, GroupDef *grp)
{
  UInt groups = 0;

  while(min <= max) {
    grp->FirstClause = min;
    grp->AtomClauses = 0;
    grp->PairClauses = 0;
    grp->StructClauses = 0;
    grp->TestClauses = 0;
    if (min->Tag == (_var+1)*sizeof(CELL)) {
      min++;
      continue;
    }
    /* only do this for the first clauses in a group */
    if (IsVarTerm(min->Tag)) {
      ClauseDef *clp = min+1;

      grp->VarClauses = 1;
      do {
	if (clp > max ||
	    !IsVarTerm(clp->Tag)) {
	  grp->LastClause = (min = clp)-1;
	  break;
	}
	clp++;
	if (clp->Tag != (_var+1)*sizeof(CELL))
	  grp->VarClauses++;
      } while (TRUE);
    } else {
      grp->VarClauses = 0;
      do {
      restart_loop:
	if (IsAtomTerm(min->Tag) || IsIntTerm(min->Tag)) {
	  grp->AtomClauses++;
	} else if (IsPairTerm(min->Tag)) {
	  grp->PairClauses++;
	} else if (IsApplTerm(min->Tag)) {
	  grp->StructClauses++;
	} else {
	  grp->TestClauses++;
	}
	min++;
      } while (min <= max &&
	       (!IsVarTerm(min->Tag)));
      if (min <= max && min->Tag == (_var+1)*sizeof(CELL)) {
	min++;
	goto restart_loop;
      }
      grp->LastClause = min-1;
    }
    groups++;
    grp++;
  }
  return groups;
}

static UInt
new_label(void)
{
  UInt lbl = labelno;
  labelno += 2;
  return lbl;
}

static void
emit_trust(ClauseDef *cl, struct intermediates *cint, UInt nxtlbl, int clauses)
{
  PredEntry *ap = cint->CurrentPred;

  if (ap->PredFlags & ProfiledPredFlag) {
    Yap_emit(retry_profiled_op, Unsigned(ap), Zero, cint);
  }
  if (ap->PredFlags & CountPredFlag) {
    Yap_emit(count_retry_op, Unsigned(ap), Zero, cint);
  }
  if (clauses == 0) {
    Yap_emit(trust_op, (CELL)(cl->Code), has_cut(cl->CurrentCode) , cint);
  } else {
    Yap_emit(retry_op, (CELL)(cl->Code), (clauses << 1) | has_cut(cl->CurrentCode) , cint);
    Yap_emit(jumpi_op, nxtlbl, Zero, cint);
  }
}

static void
emit_retry(ClauseDef *cl, struct intermediates *cint, int clauses)
{
  PredEntry *ap = cint->CurrentPred;

  if (ap->PredFlags & ProfiledPredFlag) {
    Yap_emit(retry_profiled_op, Unsigned(ap), Zero, cint);
  }
  if (ap->PredFlags & CountPredFlag) {
    Yap_emit(count_retry_op, Unsigned(ap), Zero, cint);
  }
  Yap_emit(retry_op, (CELL)(cl->Code), (clauses << 1) | has_cut(cl->CurrentCode), cint);
}

static compiler_vm_op
emit_optry(int var_group, int first, int clauses, int clleft, PredEntry *ap)
{
  /* var group */
  if (var_group || clauses == 0) {
    if (first) {
      return try_op;
    } else if (clleft+clauses) {
      return retry_op;
    } else {
      return trust_op;
    }
  } else if (clleft == 0) {
#ifdef TABLING
    if (ap->PredFlags & TabledPredFlag && !first) {
      /* we never actually get to remove the last choice-point in this case */
      return retry_op;
    } else
#endif
    {
      /* last group */
      return try_op;
    }
  } else {
    /* nonvar group */
    return try_in_op;
  }
}


static void
emit_try(ClauseDef *cl, struct intermediates *cint, int var_group, int first, int clauses, int clleft, UInt nxtlbl)
{
  compiler_vm_op comp_op = emit_optry(var_group, first, clauses, clleft, cint->CurrentPred);
  Yap_emit(comp_op, (CELL)(cl->CurrentCode), ((clauses+clleft) << 1) | has_cut(cl->CurrentCode), cint);
}

static TypeSwitch *
emit_type_switch(compiler_vm_op op, struct intermediates *cint)
{
 return (TypeSwitch *)Yap_emit_extra_size(op, 0, sizeof(TypeSwitch), cint);
}


static yamop *
emit_switch_space(UInt n, UInt item_size, struct intermediates *cint)
{
  PredEntry *ap = cint->CurrentPred;

  if (ap->PredFlags & LogUpdatePredFlag) {
    UInt sz = sizeof(LogUpdIndex)+n*item_size;
    LogUpdIndex *cl = (LogUpdIndex *)Yap_AllocCodeSpace(sz);
    if (cl == NULL) {
      Yap_Error_Size = sizeof(LogUpdIndex)+n*item_size;
      /* grow stack */
      longjmp(cint->CompilerBotch,2);
    }
    cl->ClFlags = SwitchTableMask|LogUpdMask;
    cl->ClSize = sz;
    /* insert into code chain */
    return cl->ClCode;
  } else {
    UInt sz = sizeof(StaticIndex)+n*item_size;
    StaticIndex *cl = (StaticIndex *)Yap_AllocCodeSpace(sz);
    if (cl == NULL) {
      Yap_Error_Size = sizeof(LogUpdIndex)+n*item_size;
      /* grow stack */
      longjmp(cint->CompilerBotch,2);
    }
    cl->ClFlags = SwitchTableMask;
    cl->ClSize = sz;
    return cl->ClCode;
    /* insert into code chain */
  }  
}

static AtomSwiEntry *
emit_cswitch(int n, UInt fail_l, struct intermediates *cint)
{
  compiler_vm_op op;
  AtomSwiEntry *target;

  if (n > MIN_HASH_ENTRIES) {
    int cases = MIN_HASH_ENTRIES, i;
    n += 1+n/4;
    while (cases < n) cases *= 2;
    n = cases;
    op = switch_c_op;
    target = (AtomSwiEntry *)emit_switch_space(n, sizeof(AtomSwiEntry), cint);
    for (i=0; i<n; i++) {
      target[i].Tag = Zero;
      target[i].Label = fail_l;
    }
    Yap_emit(op, Unsigned(n), (CELL)target, cint);
  } else {
    op = if_c_op;
    target = (AtomSwiEntry *)emit_switch_space(n+1, sizeof(AtomSwiEntry), cint);
    target[n].Tag = Zero;
    target[n].Label = fail_l;
    Yap_emit(op, Unsigned(n), (CELL)target, cint);
  }
  return target;
}

static AtomSwiEntry *
lookup_c_hash(Term t, yamop *tab, COUNT entries)
{
  AtomSwiEntry *cebase = (AtomSwiEntry *)tab;
  int hash, d;
  AtomSwiEntry *centry;

  hash = (t >> HASH_SHIFT) & (entries-1);
  centry = cebase + hash;
  d = (entries-1) & (t|1);
  while (centry->Tag != t) {
    if (centry->Tag == 0L)
      return centry;
    hash = (hash + d) & (entries-1);
    centry = cebase + hash;
  }
  return centry;
}

static AtomSwiEntry *
fetch_centry(AtomSwiEntry *cebase, Term wt, int i, int n)
{
  if (n > MIN_HASH_ENTRIES) {
    int cases = MIN_HASH_ENTRIES;

    n += 1+n/4;
    while (cases < n) cases *= 2;
    return lookup_c_hash(wt, (yamop *)cebase, cases);
  } else {
    return cebase + i;
  }
}

static FuncSwiEntry *
emit_fswitch(int n, UInt fail_l, struct intermediates *cint)
{
  compiler_vm_op op;
  FuncSwiEntry *target;

  if (n > MIN_HASH_ENTRIES) {
    int cases = MIN_HASH_ENTRIES, i;
    n += 1+n/4;
    while (cases < n) cases *= 2;
    n = cases;
    op = switch_f_op;
    target = (FuncSwiEntry *)emit_switch_space(n, sizeof(FuncSwiEntry), cint);
    for (i=0; i<n; i++) {
      target[i].Tag = NULL;
      target[i].Label = fail_l;
    }
    Yap_emit(op, Unsigned(n), (CELL)target, cint);
  } else {
    op = if_f_op;
    target = (FuncSwiEntry *)emit_switch_space(n+1, sizeof(FuncSwiEntry), cint);
    target[n].Tag = NULL;
    target[n].Label = fail_l;
    Yap_emit(op, Unsigned(n), (CELL)target, cint);
  }
  return target;
}

static FuncSwiEntry *
lookup_f_hash(Functor f, yamop *tab, COUNT entries)
{
  FuncSwiEntry *febase = (FuncSwiEntry *)tab;
  int hash, d;
  FuncSwiEntry *fentry;
  Term wt = (Term)f;

  hash = (wt >> HASH_SHIFT) & (entries-1);
  fentry = febase + hash;
  d = (entries-1) & (wt|1);
  while (fentry->Tag != f) {
    if (fentry->Tag == NULL)
      return fentry;
    hash = (hash + d) & (entries-1);
    fentry = febase + hash;
  }
  return fentry;
}

static FuncSwiEntry *
fetch_fentry(FuncSwiEntry *febase, Functor ft, int i, int n)
{
  if (n > MIN_HASH_ENTRIES) {
    int cases = MIN_HASH_ENTRIES;

    n += 1+n/4;
    while (cases < n) cases *= 2;
    return lookup_f_hash(ft, (yamop *)febase, cases);
  } else {
    return febase + i;
  }
}

/* we assume there is at least one clause, that is, c0 < cf */
static UInt
do_var_clauses(ClauseDef *c0, ClauseDef *cf, int var_group, struct intermediates *cint, int first, int clleft, UInt nxtlbl, UInt argno0) {
  UInt labl;
  UInt labl_dyn0 = 0, labl_dynf = 0;

  labl = new_label();
  Yap_emit(label_op, labl, Zero, cint);
  /*
    add expand_node if var_group == TRUE (jump on var) ||
		       var_group == FALSE (leaf node)
   */
  if (first &&
      cint->CurrentPred->PredFlags & LogUpdatePredFlag) {
    UInt ncls;
    labl_dyn0 = new_label();
    if (clleft)
      labl_dynf = labl_dyn0;
    else
      labl_dynf = new_label();
    if (clleft == 0) /* trust*/
      ncls = (cf-c0)+1;
    else
      ncls = 0;
    Yap_emit_3ops(enter_lu_op, labl_dyn0, labl_dynf, ncls, cint);
    Yap_emit(label_op, labl_dyn0, Zero, cint); 
  }
  if (c0 == cf) {
    emit_try(c0, cint, var_group, first, 0, clleft, nxtlbl);
  } else {

    if (c0 < cf) {
      emit_try(c0, cint, var_group, first, cf-c0, clleft, nxtlbl);
    }
    c0++;
    while (c0 < cf) {
      emit_retry(c0, cint, clleft+(cf-c0));
      c0++;
    }
    if (c0 == cf) {
      emit_trust(c0, cint, nxtlbl, clleft);
      if (!clleft && 
	  cint->CurrentPred->PredFlags & LogUpdatePredFlag) {
	Yap_emit(label_op, labl_dynf, Zero, cint); 
      }
    }
  }
  return labl;
}

static UInt
do_var_group(GroupDef *grp, struct intermediates *cint, int var_group, int first, int clleft, UInt nxtlbl, UInt argno0) {
  return do_var_clauses(grp->FirstClause, grp->LastClause, var_group, cint, first, clleft, nxtlbl, argno0);
}


/* count the number of different constants */
static UInt
count_consts(GroupDef *grp)
{
  Term current = MkAtomTerm(AtomFoundVar);
  UInt i = 0;
  ClauseDef *cl = grp->FirstClause;
    
  while (IsAtomTerm(cl->Tag) || IsIntTerm(cl->Tag)) {
    if (current != cl->Tag) {
      i++;
      current = cl->Tag;
    }
    if (cl == grp->LastClause) {
      return i;
    }
    cl++;
  }
  return i;
}

static UInt
count_blobs(GroupDef *grp)
{
  Term current = MkAtomTerm(AtomFoundVar);
  UInt i = 0;
  ClauseDef *cl = grp->FirstClause;
    
  while (TRUE) {
    if (current != cl->Tag) {
      i++;
      current = cl->Tag;
    }
    if (cl == grp->LastClause) {
      return i;
    }
    cl++;
  }
  return i;
}

/* count the number of different constants */
static UInt
count_funcs(GroupDef *grp)
{
  Term current = MkAtomTerm(AtomFoundVar);
  UInt i = 0;
  ClauseDef *cl = grp->FirstClause;
    
  while (IsApplTerm(cl->Tag)) {
    if (current != cl->Tag) {
      i++;
      current = cl->Tag;
    }
    if (cl == grp->LastClause) {
      return i;
    }
    cl++;
  }
  return i;
}

static UInt
emit_single_switch_case(ClauseDef *min, struct intermediates *cint, int first, int clleft, UInt nxtlbl)
{
#ifdef TABLING
  if (ap->PredFlags & TabledPredFlag) {
    /* we have two differences with tabling:
       1. we cannot allow straight jumps to clauses, otherwise thetabled
         would never get to be created.
       2. we don't clean trust at the very end of computation.
    */
    if (clleft == 0) {
      UInt lbl = new_label();
      Yap_emit(label_op, lbl, Zero, cint);
      if (first) {
	Yap_emit(table_try_single_op, (UInt)(min->CurrentCode), has_cut(cl->CurrentCode), cint);
      } else {
	Yap_emit(trust_op, (UInt)(min->CurrentCode), has_cut(cl->CurrentCode), cint);
      }
      return lbl;
    }
  }
#endif
  return (UInt)(min->CurrentCode);
}

static UInt
suspend_indexing(ClauseDef *min, ClauseDef *max, PredEntry *ap)
{
  return (UInt)&(ap->cs.p_code.ExpandCode);
}


static UInt
do_var_entries(GroupDef *grp, Term t, struct intermediates *cint, UInt argno, int first, int clleft, UInt nxtlbl){
  PredEntry *ap = cint->CurrentPred;

  if (!IsVarTerm(t) || t != 0L) {
    return suspend_indexing(grp->FirstClause, grp->LastClause, ap);
  }
  return do_var_group(grp, cint, FALSE, first, clleft, nxtlbl, ap->ArityOfPE+1);
}

static UInt
do_consts(GroupDef *grp, Term t, struct intermediates *cint, int compound_term, CELL *sreg, UInt arity, int last_arg, UInt argno, int first, UInt nxtlbl, int clleft, CELL *top)
{
  UInt n;
  ClauseDef *min = grp->FirstClause;
  UInt i;
  UInt lbl;
  /* generate a switch */
  AtomSwiEntry *cs;
  PredEntry *ap = cint->CurrentPred;

  if (!IsAtomTerm(min->Tag) && !IsIntTerm(min->Tag)) {
    /* no clauses, just skip */
    return nxtlbl;
  }
  n = count_consts(grp);
  lbl = new_label();
  Yap_emit(label_op, lbl, Zero, cint);
  cs = emit_cswitch(n, (UInt)FAILCODE, cint);
  for (i = 0; i < n; i++) {
    AtomSwiEntry *ics;
    ClauseDef *max = min;

    ics = fetch_centry(cs, min->Tag, i, n);
    ics->Tag = min->Tag;
    while ((max+1)->Tag == min->Tag &&
	   max != grp->LastClause) max++;
    if (min != max) {
      if (sreg != NULL) {
	if (ap->PredFlags & LogUpdatePredFlag && max > min)
	  ics->Label = suspend_indexing(min, max, ap);
	else
	  ics->Label = do_compound_index(min, max, sreg, cint, compound_term, arity, argno+1, nxtlbl, first, last_arg, clleft, top, TRUE);
      } else if (ap->PredFlags & LogUpdatePredFlag) {
	ics->Label = suspend_indexing(min, max, cint->CurrentPred);
      } else {
	ics->Label = do_index(min, max, cint, argno+1, nxtlbl, first, clleft, top);
      }
    } else {
      ics->Label = do_index(min, max, cint, argno+1, nxtlbl, first, clleft, top);
    }
    grp->FirstClause = min = max+1;
  }
  return lbl;
}

static void
do_blobs(GroupDef *grp, Term t, struct intermediates *cint, UInt argno, int first, UInt nxtlbl, int clleft, CELL *top)
{
  UInt n;
  ClauseDef *min = grp->FirstClause;
  UInt i;
  /* generate a switch */
  AtomSwiEntry *cs;
  PredEntry *ap = cint->CurrentPred;

  n = count_blobs(grp);
  cs = emit_cswitch(n, nxtlbl, cint);
  for (i = 0; i < n; i++) {
    AtomSwiEntry *ics;
    ClauseDef *max = min;

    ics = fetch_centry(cs, min->Tag, i, n);
    ics->Tag = min->Tag;
    while ((max+1)->Tag == min->Tag &&
	   max != grp->LastClause) max++;
    if (min != max &&
	(ap->PredFlags & LogUpdatePredFlag)) {
      ics->Label = suspend_indexing(min, max, ap);
    } else {
      ics->Label = do_index(min, max, cint, argno+1, nxtlbl, first, clleft, top);
    }
    grp->FirstClause = min = max+1;
  }
}

static UInt
do_funcs(GroupDef *grp, Term t, struct intermediates *cint, UInt argno, int first, int last_arg, UInt nxtlbl, int clleft, CELL *top)
{
  UInt n = count_funcs(grp);
  ClauseDef *min = grp->FirstClause;
  UInt i;
  FuncSwiEntry *fs;
  UInt lbl;

  if (min > grp->LastClause || n == 0) {
    /* no clauses, just skip */
    return nxtlbl;
  }
  lbl = new_label();
  Yap_emit(label_op, lbl, Zero, cint);
  /* generate a switch */
  fs = emit_fswitch(n, (UInt)FAILCODE, cint);
  for (i = 0; i < n ; i++) {
    Functor f = (Functor)RepAppl(min->Tag);
    FuncSwiEntry *ifs;
    ClauseDef *max = min;

    ifs = fetch_fentry(fs, f, i, n);
    ifs->Tag = f;
    while ((max+1)->Tag == min->Tag &&
	   max != grp->LastClause) max++; 
    /* delay non-trivial indexing  
   if (min != max &&
	!IsExtensionFunctor(f)) {
      ifs->Label = suspend_indexing(min, max, ap);
    } else 
    */
    if (IsExtensionFunctor(f)) {
      if (f == FunctorDBRef) 
	ifs->Label = do_dbref_index(min, max, t, cint, argno, nxtlbl, first, clleft, top);
      else
	ifs->Label = do_blob_index(min, max, t, cint, argno, nxtlbl, first, clleft, top);
	
    } else {
      CELL *sreg;

      if (!IsVarTerm(t) && IsApplTerm(t) && FunctorOfTerm(t) == f) {
	sreg = RepAppl(t)+1;
      } else {
	sreg = NULL;
      }
      ifs->Label = do_compound_index(min, max, sreg, cint, 0, ArityOfFunctor(f), argno+1, nxtlbl, first, last_arg, clleft, top, TRUE);
    }
    grp->FirstClause = min = max+1;
  }
  return lbl;
}

static UInt
do_pair(GroupDef *grp, Term t, struct intermediates *cint, UInt argno, int first, int last_arg, UInt nxtlbl, int clleft, CELL *top)
{
  ClauseDef *min = grp->FirstClause;
  ClauseDef *max = grp->FirstClause;

  while (IsPairTerm(max->Tag) && max != grp->LastClause) {
    max++;
  }
  if (min > grp->LastClause) {
    /* no clauses, just skip */
    return nxtlbl;
  }
  grp->FirstClause = max+1;
  if (min == max) {
    /* single clause, no need to do indexing, but we do know it is a list */ 
    return (UInt)(min->CurrentCode);
  }
  if (min != max && !IsPairTerm(t)) {
    return suspend_indexing(min, max, cint->CurrentPred);
  }
  return do_compound_index(min, max, (IsPairTerm(t) ? RepPair(t) : NULL), cint, 0, 2, argno+1, nxtlbl, first, last_arg, clleft, top, TRUE);
}

static void
group_prologue(int compound_term, UInt argno, int first, struct intermediates *cint)
{
  if (compound_term) {
    Yap_emit(cache_sub_arg_op, compound_term-1, compound_term-1, cint);
  } else {
    if (!first || argno != 1) {
      Yap_emit(cache_arg_op, argno, argno, cint);
    }
  }
}

/* make sure that we can handle failure correctly */
static void
emit_protection_choicepoint(int first, int clleft, UInt nxtlbl, struct intermediates *cint)
{

  if (first) {
    if (clleft) {
      if (cint->CurrentPred->PredFlags & LogUpdatePredFlag) {
	UInt labl = new_label();

	Yap_emit_3ops(enter_lu_op, labl, labl, 0, cint);
	Yap_emit(label_op, labl, Zero, cint);
      }
      Yap_emit(tryme_op, nxtlbl, (clleft << 1), cint);
    }
  } else {
    /* !first */
    if (clleft) {
      Yap_emit(retryme_op, nxtlbl, (clleft << 1), cint);
#ifdef TABLING
    } else if ((cint->CurrentPred->PredFlags & TabledPredFlag)) {
      /*
	we cannot get rid of the choice-point for tabled predicates, all
	kinds of hell would follow, so we just keep it around: not nice,
	but should work.
      */
      Yap_emit(retryme_op, (CELL)TRUSTFAILCODE, 0, cint);
#endif
    } else {
      Yap_emit(trustme_op, 0, 0, cint);
    }
  }
}


static ClauseDef *
cls_move(ClauseDef *min, PredEntry *ap, ClauseDef *max, int compound_term, UInt argno, int last_arg)
{
  ClauseDef *cl=min;

  cl = min;
  if (compound_term) {
    while (cl <= max) {
      skip_to_arg(cl, ap, compound_term, last_arg );
      cl++;
    }
  } else {
    while (cl <= max) {
      if (cl->Tag == (_var+1)*sizeof(CELL)) {
	ClauseDef *cli = cl;
	while (cli < max) {
	  clcpy(cli,cli+1);
	  cli++;
	}
	max--;
      } else {
	move_next(cl, argno);
      }
      cl++;
    }
  }
  return max;
}

static void
purge_pvar(GroupDef *group) {
  ClauseDef *max = group->LastClause;
  ClauseDef *cl = group->FirstClause;

  while (cl <= max) {
    if (cl->Tag == (_var+1)*sizeof(CELL)) {
      ClauseDef *cli = cl;
      while (cli < max) {
	clcpy(cli,cli+1);
	cli++;
      }
      group->VarClauses--;
      max--;
    }
    cl++;
  }
  group->LastClause = max;
}


static UInt *
do_nonvar_group(GroupDef *grp, Term t, int compound_term, CELL *sreg, UInt arity, UInt labl, struct intermediates *cint, UInt argno, int first, int last_arg, UInt nxtlbl, int clleft, CELL *top) {
  TypeSwitch *type_sw;
  PredEntry *ap = cint->CurrentPred;

  /* move cl pointer */
  if (grp->AtomClauses + grp->PairClauses + grp->StructClauses > 1) {
    Yap_emit(label_op, labl, Zero, cint);
    if (argno == 1 && !compound_term) {
      emit_protection_choicepoint(first, clleft, nxtlbl, cint);
    }
    group_prologue(compound_term, argno, first, cint);
    if (grp->LastClause < grp->FirstClause) { /* only tests */
      return NULL;
    }
    type_sw = emit_type_switch(switch_on_type_op, cint);
    type_sw->VarEntry = do_var_entries(grp, t, cint, argno, first, clleft, nxtlbl);
    grp->LastClause = cls_move(grp->FirstClause, ap, grp->LastClause, compound_term, argno, last_arg);
    sort_group(grp,top,cint);
    type_sw->ConstEntry = 
      type_sw->FuncEntry = 
      type_sw->PairEntry = 
      nxtlbl;
    while (grp->FirstClause <= grp->LastClause) {
      if (IsAtomOrIntTerm(grp->FirstClause->Tag)) {
	type_sw->ConstEntry = do_consts(grp, t, cint, compound_term, sreg, arity, last_arg, argno, first, nxtlbl, clleft, top);
      } else if (IsApplTerm(grp->FirstClause->Tag)) {
	type_sw->FuncEntry = do_funcs(grp, t, cint, argno, first, last_arg, nxtlbl, clleft, top);
      } else {
	type_sw->PairEntry = do_pair(grp, t, cint, argno, first, last_arg, nxtlbl, clleft, top);
      }
    }
    return &(type_sw->VarEntry);
  } else {
    Yap_emit(label_op,labl,Zero, cint);
    do_var_group(grp, cint, TRUE, first, clleft, nxtlbl, ap->ArityOfPE+1);
    return NULL;
  }
}

static UInt
do_optims(GroupDef *group, int ngroups, UInt fail_l, ClauseDef *min, struct intermediates *cint)
{
  if (ngroups==2 && group[0].FirstClause ==  group[0].LastClause &&
      group[0].AtomClauses == 1 && group[1].VarClauses == 1) {
    CELL *sp;
    UInt labl;

    labl = new_label();
    sp = Yap_emit_extra_size(if_not_op, Zero, 4*CellSize, cint);
    sp[0] = (CELL)(group[0].FirstClause->Tag);
    sp[1] = (CELL)(group[1].FirstClause->Code);
    sp[2] = do_var_clauses(group[0].FirstClause, group[1].LastClause, FALSE, cint, TRUE, 0, (CELL)FAILCODE, cint->CurrentPred->ArityOfPE+1);      
    sp[3] = do_var_clauses(min, group[1].LastClause, FALSE, cint, TRUE, 0, (CELL)FAILCODE, cint->CurrentPred->ArityOfPE+1);
    return labl;
  }
  return fail_l;
}

static int
cls_info(ClauseDef *min, ClauseDef *max, UInt argno)
{
  ClauseDef *cl=min;
  int found_pvar = FALSE;

  while (cl <= max) {
    add_info(cl, argno);
    if (cl->Tag == (_var+1)*sizeof(CELL)) {
      found_pvar = TRUE;
    }
    /*    if (IsVarTerm(cl->Tag)) cl->Tag = (CELL)NULL; */
    cl++;
  }
  return found_pvar;
}

static int
cls_head_info(ClauseDef *min, ClauseDef *max, UInt argno)
{
  ClauseDef *cl=min;

  while (cl <= max) {
    add_head_info(cl, argno);
    /*    if (IsVarTerm(cl->Tag)) cl->Tag = (CELL)NULL; */
    cl++;
  }
  return FALSE;
}

static UInt
do_index(ClauseDef *min, ClauseDef* max, struct intermediates *cint, UInt argno, UInt fail_l, int first, int clleft, CELL *top)
{
  UInt ngroups, found_pvar = FALSE;
  UInt i = 0;
  GroupDef *group = (GroupDef *)top;
  UInt labl, labl0, lablx;
  Term t;
  /* remember how we entered here */
  UInt argno0 = argno;
  PredEntry *ap = cint->CurrentPred;

  if (min == max) {
    /* base case, just commit to the current code */
    return emit_single_switch_case(min, cint, first, clleft, fail_l);
  }
  if ((argno > 1 && yap_flags[INDEXING_MODE_FLAG] == INDEX_MODE_SINGLE) ||
      ap->ArityOfPE < argno) {
    return do_var_clauses(min, max, FALSE, cint, first, clleft, fail_l, ap->ArityOfPE+1);
  }
  t = Deref(XREGS[argno]);
  if (ap->PredFlags & LogUpdatePredFlag) {
    found_pvar = cls_head_info(min, max, argno);
  } else {
    found_pvar = cls_info(min, max, argno);
  }
  ngroups = groups_in(min, max, group);
  if (IsVarTerm(t) &&
      max - min > 2 &&
      ap->ModuleOfPred != IDB_MODULE) {
    lablx = new_label();
    Yap_emit(label_op, lablx, Zero, cint);
    while (IsVarTerm(t)) {
      Yap_emit(jump_nv_op, (CELL)(&(ap->cs.p_code.ExpandCode)), argno, cint);
      if (argno == ap->ArityOfPE) {
	do_var_clauses(min, max, FALSE, cint, first, clleft, fail_l, argno0);
	return lablx;
      }
      argno++;
      t = Deref(XREGS[argno]);
      if (ap->PredFlags & LogUpdatePredFlag) {
	found_pvar = cls_head_info(min, max, argno);
      } else {
	found_pvar = cls_info(min, max, argno);
      }
      ngroups = groups_in(min, max, group);
    } 
    labl0 = labl = new_label();
  } else {
    lablx = labl0 = labl = new_label();
  }
  top = (CELL *)(group+ngroups);
  if (argno > 1) {
    /* don't try being smart for other arguments than the first */
    if (ngroups > 1 || group->VarClauses != 0 || found_pvar) {
      if (ap->ArityOfPE == argno) {
	return do_var_clauses(min, max, FALSE, cint, first, clleft, fail_l, ap->ArityOfPE+1);
      } else {
	return do_index(min, max, cint, argno+1, fail_l, first, clleft, top);
      }
    } else {
      ClauseDef *cl = min;
      /*
	need to reset the code pointer, otherwise I could be in
	the middle of a compound term.
       */
      while (cl <= max) {
	cl->CurrentCode = cl->Code;
	cl++;
      }    
    }
  } else {
    UInt special_options;
    if ((ap->PredFlags & LogUpdatePredFlag) && ngroups > 1) {
      /* make sure we only expand at a single point */
      if (group[0].VarClauses && ngroups > 3) {
	int ncls = group[ngroups-1].LastClause-group[2].FirstClause;
	group[2].VarClauses += ncls;
	group[2].LastClause = group[ngroups-1].LastClause;
	ngroups = 3;
      } else if (!group[0].VarClauses && ngroups > 2) {
	int ncls = group[ngroups-1].LastClause-group[1].FirstClause;
	group[1].VarClauses += ncls;
	group[1].LastClause = group[ngroups-1].LastClause;
	ngroups = 2;
      }
    } else if ((special_options = do_optims(group, ngroups, fail_l, min, cint)) != fail_l) {
      return special_options;
    }
    if (ngroups == 1 && group->VarClauses && !found_pvar) {
      return do_index(min, max, cint, argno+1, fail_l, first, clleft, top);
    } else if (found_pvar) {
      Yap_emit(label_op, labl0, Zero, cint);
      labl = new_label();
      Yap_emit(jump_v_op, suspend_indexing(min, max, ap), Zero, cint);
    }
  }
  for (i=0; i < ngroups; i++) {
    UInt nextlbl;
    int left_clauses = clleft+(max-group->LastClause);
    /* a group may end up not having clauses*/

    if (i < ngroups-1) {
      nextlbl = new_label();
    } else {
      nextlbl = fail_l;
    }
    if (found_pvar && argno == 1) {
      purge_pvar(group);
    }
    if (group->FirstClause==group->LastClause && first && left_clauses == 0) {
      Yap_emit(jumpi_op, (CELL)(group->FirstClause->Code), Zero, cint);
    } else {
      if (group->VarClauses) {
	Yap_emit(label_op,labl,Zero, cint);
	do_var_group(group, cint, argno == 1, first, left_clauses, nextlbl, ap->ArityOfPE+1);
      } else {
	do_nonvar_group(group, t, 0, NULL, 0, labl, cint, argno, first, TRUE, nextlbl, left_clauses, top);
      }
    }
    first = FALSE;
    group++;
    labl = nextlbl;
  }
  return lablx;
}

static ClauseDef *
copy_clauses(ClauseDef *max0, ClauseDef *min0, CELL *top, struct intermediates *cint)
{
  UInt sz = ((max0+1)-min0)*sizeof(ClauseDef);
  while ((char *)top + sz > Yap_TrailTop) {
#if USE_SYSTEM_MALLOC
    Yap_Error_Size = sz;
    /* grow stack */
    longjmp(cint->CompilerBotch,4);
#else
    if(!Yap_growtrail (sz)) {
      longjmp(cint->CompilerBotch,3);
    }
#endif
  }
  memcpy((void *)top, (void *)min0, sz);
  return (ClauseDef *)top;
}


/* execute an index inside a structure */
static UInt
do_compound_index(ClauseDef *min0, ClauseDef* max0, Term* sreg, struct intermediates *cint, UInt i, UInt arity, UInt argno, UInt fail_l, int first, int last_arg, int clleft, CELL *top, int done_work)
{
  int ret_lab = 0, *newlabp;
  CELL *top0 = top;
  ClauseDef *min, *max;
  PredEntry *ap = cint->CurrentPred;
  int found_index = FALSE, lu_pred = ap->PredFlags & LogUpdatePredFlag;

  newlabp = & ret_lab;
  if (min0 == max0) {
    /* base case, just commit to the current code */
    return emit_single_switch_case(min0, cint, first, clleft, fail_l);
  }
  if (yap_flags[INDEXING_MODE_FLAG] == INDEX_MODE_SINGLE) {
    *newlabp = 
      do_var_clauses(min0, max0, FALSE, cint, first, clleft, fail_l, ap->ArityOfPE+1);
    return ret_lab;
  }
  if (sreg == NULL) {
    return suspend_indexing(min0, max0, ap);
  }
  while (i < arity && !found_index) { 
    ClauseDef *cl;
    GroupDef *group;
    UInt ngroups;
    int isvt = IsVarTerm(Deref(sreg[i]));

    min = copy_clauses(max0, min0, top, cint);
    max = min+(max0-min0);
    top = (CELL *)(max+1);
    cl = min;
    /* search for a subargument */
    while (cl <= max) {
      add_arg_info(cl, ap, i+1);
      cl++;
    }
    group = (GroupDef *)top;
    ngroups = groups_in(min, max, group);
    if (ngroups == 1 && group->VarClauses == 0) {
      /* ok, we are doing a sub-argument */
      /* process groups */
      *newlabp = new_label();
      top = (CELL *)(group+1);
      newlabp = do_nonvar_group(group, (sreg == NULL ? 0L : Deref(sreg[i])), i+1, (isvt ? NULL : sreg), arity, *newlabp, cint, argno, argno == 1, (last_arg && i+1 == arity), fail_l, clleft, top);
      if (newlabp == NULL) {
	found_index = TRUE;
	top = top0;
	break;
      }
      if (sreg == NULL || !isvt) {
	found_index = TRUE;
      } else {
	done_work |= TRUE;
      }
    }
    top = top0;
    i++;
  }
  if (!found_index) {
    if (!lu_pred || !done_work)
      *newlabp = do_index(min0, max0, cint, argno+1, fail_l, first, clleft, top);
    else
      *newlabp = suspend_indexing(min0, max0, ap);
  }
  return ret_lab;
}

static UInt
do_dbref_index(ClauseDef *min, ClauseDef* max, Term t, struct intermediates *cint, UInt argno, UInt fail_l, int first, int clleft, CELL *top)
{
  UInt ngroups;
  GroupDef *group;
  ClauseDef *cl = min;

  group = (GroupDef *)top;
  cl = min;
  
  while (cl <= max) {
    cl->Tag = cl->u.t_ptr;
    cl++;
  }
  ngroups = groups_in(min, max, group);
  if (ngroups > 1 || group->VarClauses) {
    return do_index(min, max, cint, argno+1, fail_l, first, clleft, top);
  } else {
    int labl = new_label();

    Yap_emit(label_op, labl, Zero, cint);
    Yap_emit(index_dbref_op, Zero, Zero, cint);
    sort_group(group,(CELL *)(group+1),cint);
    do_blobs(group, t, cint, argno, first, fail_l, clleft, (CELL *)group+1);
    return labl;
  }
}

static UInt
do_blob_index(ClauseDef *min, ClauseDef* max, Term t, struct intermediates *cint, UInt argno, UInt fail_l, int first, int clleft, CELL *top)
{
  UInt ngroups;
  GroupDef *group;
  ClauseDef *cl = min;

  group = (GroupDef *)top;
  cl = min;
  
  while (cl <= max) {
    if (cl->u.t_ptr == (CELL)NULL) { /* check whether it is a builtin */
      cl->Tag = Zero;
    } else {
      cl->Tag = MkIntTerm(RepAppl(cl->u.t_ptr)[1]);
    }
    cl++;
  }
  ngroups = groups_in(min, max, group);
  if (ngroups > 1 || group->VarClauses) {
    return do_index(min, max, cint, argno+1, fail_l, first, clleft, top);
  } else {
    int labl = new_label();

    Yap_emit(label_op, labl, Zero, cint);
    Yap_emit(index_blob_op, Zero, Zero, cint);
    sort_group(group,(CELL *)(group+1),cint);
    do_blobs(group, t, cint, argno, first, fail_l, clleft, (CELL *)group+1);
    return labl;
  }
}

static void
init_clauses(ClauseDef *cl, PredEntry *ap)
{
  StaticClause *scl = ClauseCodeToStaticClause(ap->cs.p_code.FirstClause);

  do {
    cl->Code = cl->CurrentCode = scl->ClCode;
    cl++;
    if (scl->ClCode == ap->cs.p_code.LastClause)
      return;
    scl = scl->ClNext;
  } while (TRUE);
}

static void
init_log_upd_clauses(ClauseDef *cl, PredEntry *ap)
{
  LogUpdClause *lcl = ClauseCodeToLogUpdClause(ap->cs.p_code.FirstClause);

  do {
    cl->Code = cl->CurrentCode = lcl->ClCode;
    cl++;
    lcl = lcl->ClNext;
  } while (lcl != NULL);
}

static UInt
compile_index(struct intermediates *cint)
{
  PredEntry *ap = cint->CurrentPred;
  int NClauses = ap->cs.p_code.NOfClauses;
  ClauseDef *cls = (ClauseDef *)H;
  CELL *top = (CELL *) TR;

  /* only global variable I use directly */
  labelno = 1;

  Yap_Error_Size = NClauses*sizeof(ClauseDef);
  /* reserve double the space for compiler */
  if (cls+2*NClauses > (ClauseDef *)(ASP-4096)) {
    /* tell how much space we need */
    Yap_Error_Size += NClauses*sizeof(ClauseDef);
    /* grow stack */
    longjmp(cint->CompilerBotch,3);
  }
  cint->freep = (char *)(cls+NClauses);
  if (ap->PredFlags & LogUpdatePredFlag) {
    /* throw away a label */
    new_label();
    init_log_upd_clauses(cls,ap);
  } else {
    /* prepare basic data structures */ 
    init_clauses(cls,ap);
  }
  return do_index(cls, cls+(NClauses-1), cint, 1, (UInt)FAILCODE, TRUE, 0, top);
}


static int  vsc_index;

yamop *
Yap_PredIsIndexable(PredEntry *ap)
{
  yamop *indx_out;
  int setjres;
  struct intermediates cint;

  vsc_index++;
  cint.CurrentPred = ap;
  Yap_Error_Size = 0;
  if ((setjres = setjmp(cint.CompilerBotch)) == 3) {
    restore_machine_regs();
    Yap_gcl(Yap_Error_Size, ap->ArityOfPE, ENV, CP);
  } else if (setjres == 2) {
    restore_machine_regs();
    if (!Yap_growheap(FALSE, Yap_Error_Size, NULL)) {
      Yap_Error(OUT_OF_HEAP_ERROR, TermNil, Yap_ErrorMessage);
      return FAILCODE;
    }
  } else if (setjres == 4) {
    restore_machine_regs();
    if (!Yap_growtrail(Yap_Error_Size)) {
      Yap_Error(OUT_OF_TRAIL_ERROR, TermNil, Yap_ErrorMessage);
      return FAILCODE;
    }
  } else if (setjres != 0) {
    if (!Yap_growheap(FALSE, Yap_Error_Size, NULL)) {
      Yap_Error(SYSTEM_ERROR, TermNil, Yap_ErrorMessage);
      return FAILCODE;
    }
  }
 restart_index:
  cint.CodeStart = cint.BlobsStart = cint.cpc = cint.icpc = NIL;
  Yap_ErrorMessage = NULL;
  if (compile_index(&cint) == (UInt)FAILCODE) {
    return FAILCODE;
  }
#ifdef DEBUG
  if (Yap_Option['i' - 'a' + 1]) {
    Yap_ShowCode(&cint);
  }
#endif
  /* globals for assembler */
  IPredArity = ap->ArityOfPE;
  if (cint.CodeStart) {
    if ((indx_out = Yap_assemble(ASSEMBLING_INDEX, TermNil, ap, FALSE, &cint)) == NULL) {
      if (!Yap_growheap(FALSE, Yap_Error_Size, NULL)) {
	Yap_Error(SYSTEM_ERROR, TermNil, Yap_ErrorMessage);
	return NULL;
      }
      goto restart_index;
    }
  } else {
    return NULL;
  }
#ifdef LOW_PROF
  if (ProfilerOn) {
    Yap_inform_profiler_of_clause(indx_out, ProfEnd, ap,1); 
  }
#endif /* LOW_PROF */
  if (ap->PredFlags & LogUpdatePredFlag) {
    LogUpdIndex *cl = ClauseCodeToLogUpdIndex(indx_out);
    cl->ClFlags |= SwitchRootMask;
  }
  return(indx_out);
}

static istack_entry *
reset_stack(istack_entry *sp0)
{
  sp0->pos = 0;
  return sp0;
}

static istack_entry *
push_stack(istack_entry *sp, Int arg, Term Tag, Term extra)
{
  sp->pos = arg;
  sp->val = Tag;
  sp->extra = extra;
  sp++;
  sp->pos = 0;
  return sp;
}

static istack_entry *
install_clause(ClauseDef *cls, PredEntry *ap, istack_entry *stack)
{
  int last_arg = TRUE;

  istack_entry *sp = stack;
  last_arg = TRUE;
  while (sp->pos) {
    if ((Int)(sp->pos) > 0) {
      add_info(cls, sp->pos);
    } else if (sp->pos) {
      UInt argno = -sp->pos;
      add_arg_info(cls, ap, argno);
    }
    /* if we are not talking about a variable */
    if (cls->Tag != sp->val) {
      if (sp->val == 0L) {
	sp++;
      }
      break;
    } else {
      if (IsApplTerm(cls->Tag)) {
	Functor f = (Functor)RepAppl(cls->Tag);
	if (IsExtensionFunctor(f)) {
	  if (f == FunctorDBRef) {
	    if (cls->u.t_ptr != sp->extra) break;
	  } else {
	    Term t = MkIntTerm(RepAppl(sp->extra)[1]),
	      t1 = MkIntTerm(RepAppl(cls->u.t_ptr)[1]);
	      if (t != t1) break;
	  }
	}
      }
      if ((Int)(sp->pos) > 0) {
	move_next(cls, sp->pos);
      } else if (sp->pos) {
	UInt argno = -sp->pos;
	skip_to_arg(cls, ap, argno, FALSE);
	if (ArityOfFunctor((Functor)RepAppl(sp[-1].val))
	    != argno+1) {
	  last_arg = FALSE;
	}
      }
    }
    sp++;
  }
  return sp;
}

static ClauseDef *
install_clauses(ClauseDef *cls, PredEntry *ap, istack_entry *stack, yamop *beg, yamop *end)
{
  istack_entry *sp = stack;
  StaticClause *cl = ClauseCodeToStaticClause(beg);

  if (stack[0].pos == 0) {
    while (TRUE) {
      cls->Code =  cls->CurrentCode = cl->ClCode;
      cls->Tag =  0;
      cls++;
      if (cl->ClCode == end) {
	return cls-1;
      }
      cl = cl->ClNext;
    }
  }
  while (TRUE) {
    cls->Code =  cls->CurrentCode = cl->ClCode;
    sp = install_clause(cls, ap, stack);
    /* we reached a matching clause */
    if (!sp->pos && (sp[-1].val == 0L || cls->Tag == sp[-1].val)) {
      cls++;
    }
    if (cl->ClCode == end || cl->ClCode == NULL) {
      return cls-1;
    }
    cl = cl->ClNext;
  }
}

static void
reinstall_clauses(ClauseDef *cls, ClauseDef *end, PredEntry *ap, istack_entry *stack)
{
  do {
    cls->CurrentCode = cls->Code;
    install_clause(cls, ap, stack);
  } while (cls++ != end);
}

static istack_entry *
install_log_upd_clause(ClauseDef *cls, PredEntry *ap, istack_entry *stack)
{
  int last_arg = TRUE;

  istack_entry *sp = stack;
  last_arg = TRUE;
  while (sp->pos) {
    if ((Int)(sp->pos) > 0) {
      add_head_info(cls, sp->pos);
    } else if (sp->pos) {
      UInt argno = -sp->pos;
      add_arg_info(cls, ap, argno);
    }
    /* if we are not talking about a variable */
    if (cls->Tag != sp->val) {
      if (sp->val == 0L) {
	sp++;
      }
      break;
    } else {
      if (IsApplTerm(cls->Tag)) {
	Functor f = (Functor)RepAppl(cls->Tag);
	if (IsExtensionFunctor(f)) {
	  if (f == FunctorDBRef) {
	    if (cls->u.t_ptr != sp->extra) break;
	  } else {
	    Term t = MkIntTerm(RepAppl(sp->extra)[1]),
	      t1 = MkIntTerm(RepAppl(cls->u.t_ptr)[1]);
	      if (t != t1) break;
	  }
	}
      }
      if ((Int)(sp->pos) > 0) {
	move_next(cls, sp->pos);
      } else if (sp->pos) {
	UInt argno = -sp->pos;
	skip_to_arg(cls, ap, argno, FALSE);
	if (ArityOfFunctor((Functor)RepAppl(sp[-1].val))
	    != argno+1) {
	  last_arg = FALSE;
	}
      }
    }
    sp++;
  }
  return sp;
}

static ClauseDef *
install_log_upd_clauses(ClauseDef *cls, PredEntry *ap, istack_entry *stack, yamop *beg, yamop *end)
{
  istack_entry *sp = stack;

  if (stack[0].pos == 0) {
    while (TRUE) {
      cls->Code =  cls->CurrentCode = beg;
      cls->Tag =  0;
      cls++;
      if (beg == end || beg == NULL) {
	return cls-1;
      }
      beg = ClauseCodeToLogUpdClause(beg)->ClNext->ClCode;
    }
  }
  while (TRUE) {
    cls->Code =  cls->CurrentCode = beg;
    sp = install_log_upd_clause(cls, ap, stack);
    /* we reached a matching clause */
    if (!sp->pos && (sp[-1].val == 0L || cls->Tag == sp[-1].val)) {
      cls++;
    }
    if (beg == end || beg == NULL) {
      return cls-1;
    }
    beg = ClauseCodeToLogUpdClause(beg)->ClNext->ClCode;
  }
}

static void
reinstall_log_upd_clauses(ClauseDef *cls, ClauseDef *end, PredEntry *ap, istack_entry *stack)
{
  do {
    cls->CurrentCode = cls->Code;
    install_log_upd_clause(cls, ap, stack);
  } while (cls++ != end);
}

#if PRECOMPUTE_REGADDRESS

#define arg_from_x(I)		(((CELL *)(I))-XREGS)

#else

#define arg_from_x(I)		(I)

#endif /* ALIGN_LONGS */

static AtomSwiEntry *
lookup_c(Term t, yamop *tab, COUNT entries)
{
  AtomSwiEntry *cebase = (AtomSwiEntry *)tab;

  while (cebase->Tag != t) {
    entries--;
    cebase++;
    if (entries == 0)
      return cebase;
  }
  return cebase;
}

static FuncSwiEntry *
lookup_f(Functor f, yamop *tab, COUNT entries)
{
  FuncSwiEntry *febase = (FuncSwiEntry *)tab;
                                                
  while (febase->Tag != f) {
    entries--;
    febase++;
    if (entries == 0)
      return febase;
  }
  return febase;
}

static COUNT
count_clauses_left(yamop *cl, PredEntry *ap)
{
  if (ap->PredFlags & LogUpdatePredFlag) {
    LogUpdClause *c = ClauseCodeToLogUpdClause(cl);
    COUNT i = 0;
 
    while (c != NULL) {
      i++;
      c = c->ClNext;
    }
    return i;
  } else {
    yamop *last = ap->cs.p_code.LastClause;
    StaticClause *c = ClauseCodeToStaticClause(cl);
    COUNT i = 1;

    while (c->ClCode != last) {
      i++;
      c = c->ClNext;
    }
    return i;
  }
}

static yamop **
expand_index(struct intermediates *cint) {
  /* first clause */
  PredEntry *ap = cint->CurrentPred;
  yamop *first, *last = NULL, *alt = NULL;
  istack_entry *stack, *sp;
  ClauseDef *cls = (ClauseDef *)H, *max;
  int NClauses;
  /* last clause to experiment with */
  yamop *ipc;
  /* labp should point at the beginning of the sequence */
  yamop **labp = NULL;
  Term t = TermNil, *s_reg = NULL;
  int is_last_arg = TRUE;
  int argno = 1;
  int isfirstcl = TRUE;
  /* this is will be used as a new PC */
  CELL *top = (CELL *) TR;
  UInt arity = 0;
  UInt lab, fail_l, clleft, i = 0;

  ipc = ap->cs.p_code.TrueCodeOfPred;
  first = ap->cs.p_code.FirstClause;
  NClauses = ap->cs.p_code.NOfClauses;
  sp = stack = (istack_entry *)top;
  labelno = 1;
  stack[0].pos = 0;
  /* try to refine the interval using the indexing code */

  while (ipc != NULL) {
    op_numbers op;

    op = Yap_op_from_opcode(ipc->opc);
    switch(op) {
    case _try_clause:
    case _retry:
      /* this clause had no indexing */
      if (ap->PredFlags & LogUpdatePredFlag) {
	first = ClauseCodeToLogUpdClause(ipc->u.ld.d)->ClNext->ClCode;
      } else {
	first = ClauseCodeToStaticClause(ipc->u.ld.d)->ClNext->ClCode;
      }
      isfirstcl = FALSE;
      ipc = NEXTOP(ipc,ld);
      break;
    case _try_in:
      if (ap->PredFlags & LogUpdatePredFlag) {
	first = ClauseCodeToLogUpdClause(ipc->u.l.l)->ClNext->ClCode;
      } else {
	first = ClauseCodeToStaticClause(ipc->u.l.l)->ClNext->ClCode;
      }
      isfirstcl = FALSE;
      ipc = NEXTOP(ipc,l);
      break;
    case _retry_me:
    case _retry_me1:
    case _retry_me2:
    case _retry_me3:
    case _retry_me4:
      isfirstcl = FALSE;
    case _try_me:
    case _try_me1:
    case _try_me2:
    case _try_me3:
    case _try_me4:
      /* ok, we found the start for an indexing block,
	 but we don't if we are going to operate here or not */
      /* if we are to commit here, alt will tell us where */
      alt = ipc->u.ld.d;
      ipc = NEXTOP(ipc,ld);
      /* start of a group, reset stack */
      sp = stack;
      stack[0].pos = 0;
      break;
    case _profiled_trust_me:
    case _trust_me:
    case _count_trust_me:
    case _trust_me1:
    case _trust_me2:
    case _trust_me3:
    case _trust_me4:
      /* we will commit to this group for sure */
      ipc = NEXTOP(ipc,ld);
      alt = NULL;
      /* start of a group, reset stack */
      sp = stack;
      stack[0].pos = 0;
      break;
    case _trust:
      /* we should never be here */
      Yap_Error(SYSTEM_ERROR, TermNil, "New indexing code");
      labp =  NULL;
      ipc = NULL;
      break;
    case _stale_lu_index:
    case _enter_lu_pred:
      /* no useful info */
      ipc = ipc->u.Ill.l1;
      break;
    case _trust_logical_pred:
      /* no useful info */
      ipc = NEXTOP(ipc,l);
      break;
    case _retry_profiled:
    case _count_retry:
      /* no useful info */
      ipc = NEXTOP(ipc,l);
      break;
    case _jump:
      /* just skip for now, but should worry about memory management */
      ipc = ipc->u.l.l;
      break;
    case _lock_lu:
      ipc = NEXTOP(ipc,p);
      break;
    case _unlock_lu:
      ipc = NEXTOP(ipc,e);
      break;
    case _jump_if_var:
      if (IsVarTerm(Deref(ARG1))) {
	labp = &(ipc->u.l.l);
	ipc = ipc->u.l.l;
      } else {
	ipc = NEXTOP(ipc,l);
      }
      break;
    case _jump_if_nonvar:
      argno = arg_from_x(ipc->u.xl.x);
      t = Deref(XREGS[argno]);
      i = 0;
      /* expand_index expects to find the new argument */
      if (!IsVarTerm(t)) {
	argno--;
	labp = &(ipc->u.xl.l);
	ipc = ipc->u.xl.l;
      } else {
	ipc = NEXTOP(ipc,xl);
      }
      break;
      /* instructions type EC */
      /* instructions type e */
    case _index_dbref:
      t = AbsAppl(s_reg-1);
      sp[-1].extra = t;
      s_reg = NULL;
      ipc = NEXTOP(ipc,e);
      break;
    case _index_blob:
      t = MkIntTerm(s_reg[0]);
      sp[-1].extra = AbsAppl(s_reg-1);
      s_reg = NULL;
      ipc = NEXTOP(ipc,e);
      break;
      /* instructions type e */
    case _switch_on_type:
      t = Deref(ARG1);
      argno = 1;
      i = 0;
      if (IsVarTerm(t)) {
	labp = &(ipc->u.llll.l4);
	ipc = ipc->u.llll.l4;
      } else if (IsPairTerm(t)) {
	sp = push_stack(sp, 1, AbsPair(NULL), TermNil);
	s_reg = RepPair(t);
	labp = &(ipc->u.llll.l1);
	ipc = ipc->u.llll.l1;	
      } else if (IsApplTerm(t)) {
	sp = push_stack(sp, 1, AbsAppl((CELL *)FunctorOfTerm(t)), TermNil);
	ipc = ipc->u.llll.l3;	
      } else {
	sp = push_stack(sp, argno, t, TermNil);
	ipc = ipc->u.llll.l2;	
      }
      break;
    case _switch_list_nl:
      t = Deref(ARG1);
      argno = 1;
      i = 0;
      if (IsVarTerm(t)) {	
	labp = &(ipc->u.ollll.l4);
	ipc = ipc->u.ollll.l4;
      } else if (IsPairTerm(t)) {
	s_reg = RepPair(t);
	labp = &(ipc->u.ollll.l1);
	sp = push_stack(sp, 1, AbsPair(NULL), TermNil);
	ipc = ipc->u.ollll.l1;	
      } else if (IsApplTerm(t)) {
	sp = push_stack(sp, 1, AbsAppl((CELL *)FunctorOfTerm(t)), TermNil);
	ipc = ipc->u.ollll.l3;	
      } else {
	sp = push_stack(sp, argno, t, TermNil);
	ipc = ipc->u.ollll.l2;	
      }
      break;
    case _switch_on_arg_type:
      argno = arg_from_x(ipc->u.xllll.x);
      i = 0;
      t = Deref(XREGS[argno]);
      if (IsVarTerm(t)) {
	labp = &(ipc->u.xllll.l4);
	ipc = ipc->u.xllll.l4;
      } else if (IsPairTerm(t)) {
	s_reg = RepPair(t);
	sp = push_stack(sp, argno, AbsPair(NULL), TermNil);
	labp = &(ipc->u.xllll.l1);
	ipc = ipc->u.xllll.l1;	
      } else if (IsApplTerm(t)) {
	sp = push_stack(sp, argno, AbsAppl((CELL *)FunctorOfTerm(t)), TermNil);
	ipc = ipc->u.xllll.l3;	
      } else {
	sp = push_stack(sp, argno, t, TermNil);
	ipc = ipc->u.xllll.l2;	
      }
      break;
    case _switch_on_sub_arg_type:
      i = ipc->u.sllll.s;
      t = Deref(s_reg[i]);
      if (i != arity-1) is_last_arg = FALSE;
      t = Deref(s_reg[i]);
      if (IsVarTerm(t)) {
	labp = &(ipc->u.sllll.l4);
	ipc = ipc->u.sllll.l4;
	i++;
      } else if (IsPairTerm(t)) {
	s_reg = RepPair(t);
	sp = push_stack(sp, -i-1, AbsPair(NULL), TermNil);
	labp = &(ipc->u.sllll.l1);
	ipc = ipc->u.sllll.l1;
	i = 0;
      } else if (IsApplTerm(t)) {
	sp = push_stack(sp, -i-1, AbsAppl((CELL *)FunctorOfTerm(t)), TermNil);
	ipc = ipc->u.sllll.l3;
	i = 0;
      } else {
	/* We don't push stack here, instead we go over to next argument
	   sp = push_stack(sp, -i-1, t);
	*/
	sp = push_stack(sp, -i-1, t, TermNil);
	ipc = ipc->u.sllll.l2;	
	i++;
      }
      break;
    case _if_not_then:
      labp = NULL;
      ipc = NULL;
      break;
      /* instructions type ollll */
    case _switch_on_func:
    case _if_func:
    case _go_on_func:
      {
	FuncSwiEntry *fe;
	yamop *newpc;
	Functor f;

	s_reg = RepAppl(t);
	f = (Functor)(*s_reg++);
	if (op == _switch_on_func) {
	  fe = lookup_f_hash(f,ipc->u.sl.l,ipc->u.sl.s);
	} else {
	  fe = lookup_f(f,ipc->u.sl.l,ipc->u.sl.s);
	}
	newpc = (yamop *)(fe->Label);

	if (newpc == (yamop *)&(ap->cs.p_code.ExpandCode)) {
	  /* we found it */
	  labp = (yamop **)(&(fe->Label));
	  ipc = NULL;
	} else {
	  ipc = newpc;
	}
      }
      break;
    case _switch_on_cons:
    case _if_cons:
    case _go_on_cons:
      {
	AtomSwiEntry *ae;

	if (op == _switch_on_cons) {
	  ae = lookup_c_hash(t,ipc->u.sl.l,ipc->u.sl.s);
	} else {
	  ae = lookup_c(t,ipc->u.sl.l,ipc->u.sl.s);
	}

	if (ae->Label == (CELL)&(ap->cs.p_code.ExpandCode)) {
	  /* we found it */
	  labp = (yamop **)(&(ae->Label));
	  ipc = NULL;
	} else {
	  ipc = (yamop *)(ae->Label);
	}
      }
      break;
    case _expand_index:
      if (alt != NULL && ap->PredFlags & LogUpdatePredFlag) {
	op_numbers fop = Yap_op_from_opcode(alt->opc);
	if (fop == _enter_lu_pred) 
	  alt = alt->u.Ill.l1;
	if (fop == _trust_logical_pred) 
	  alt = NEXTOP(alt,l);
      }
      ipc = NULL;
      break;
    case _op_fail:
      ipc = alt;
      alt = NULL;
      break;
    default:
      if (alt == NULL) {
	Yap_Error(SYSTEM_ERROR,t,"Bug in Indexing Code");
	labp = NULL;
	ipc = NULL;
      } else {
	/* backtrack */
	first = alt->u.ld.d;
	ipc = alt;
	alt = NULL;
      }
    }
  }

  /* if there was an overflow while generating the code, make sure
     S is still correct */
  if (s_reg != NULL)
    S = s_reg;
  if (alt == NULL) {
    /* oops, we are at last clause */
    fail_l = (UInt)FAILCODE;
    clleft = 0;
    last = ap->cs.p_code.LastClause;
  } else {
    if (ap->PredFlags & LogUpdatePredFlag) {
      op_numbers op = Yap_op_from_opcode(alt->opc);
      if (op == _trust_logical_pred) {
	last = NEXTOP(alt,l)->u.ld.d;
      } else {
	last = alt->u.ld.d;
      }
    } else {
      op_numbers op = Yap_op_from_opcode(alt->opc);
      if (op == _retry ||
	  op == _trust) {
	last = alt->u.ld.d;
      }
    }
    fail_l = (UInt)alt;
    clleft = count_clauses_left(last,ap);
  }

  if (cls+2*NClauses > (ClauseDef *)(ASP-4096)) {
    /* tell how much space we need (worst case) */
    Yap_Error_Size += NClauses*sizeof(ClauseDef);
    /* grow stack */
    longjmp(cint->CompilerBotch,3);
  }
  if (ap->PredFlags & LogUpdatePredFlag) {
    max = install_log_upd_clauses(cls, ap, stack, first, last);
  } else {
    max = install_clauses(cls, ap, stack, first, last);
  }
  /* don't count last clause if you don't have to */
  if (alt && max->Code == last) max--;
  if (max < cls && labp != NULL) {
      *labp = FAILCODE;
    return labp;
  }
  cint->freep = (char *)(max+1);
  cint->CodeStart = cint->BlobsStart = cint->cpc = cint->icpc = NULL;
  
  if (!IsVarTerm(sp[-1].val)  && sp > stack) {
    if (IsAtomOrIntTerm(sp[-1].val)) {
      if (s_reg == NULL) { /* we have not yet looked into terms */
	lab = do_index(cls, max, cint, argno+1, fail_l, isfirstcl, clleft, top);
      } else {
	UInt arity = 0;

	if (ap->PredFlags & LogUpdatePredFlag) {
	  reinstall_log_upd_clauses(cls, max, ap, stack);
	} else {
	  reinstall_clauses(cls, max, ap, stack);
	}
	sp--;
	while (sp > stack) {
	  Term t = sp[-1].val;
	  if (IsApplTerm(t)) {
	    Functor f = (Functor)RepAppl(t);
	    if (!IsExtensionFunctor(f)) {
	      arity = ArityOfFunctor(f);
	      break;
	    } else {
	      sp--;
	    }
	  } else if (IsPairTerm(t)) {
	    arity = 2;
	    break;
	  } else {
	    sp--;
	  }
	}
	lab = do_compound_index(cls, max, s_reg, cint, i, arity, argno, fail_l, isfirstcl, is_last_arg, clleft, top, FALSE);
      }
    } else if (IsPairTerm(sp[-1].val) && sp > stack) {
      lab = do_compound_index(cls, max, s_reg, cint, i, 2, argno, fail_l, isfirstcl, is_last_arg, clleft, top, FALSE);
    } else {
      Functor f = (Functor)RepAppl(sp[-1].val);
      /* we are continuing within a compound term */
      if (IsExtensionFunctor(f)) {
	lab = do_index(cls, max, cint, argno+1, fail_l, isfirstcl, clleft, top);
      } else {
	lab = do_compound_index(cls, max, s_reg, cint, i, ArityOfFunctor(f), argno, fail_l, isfirstcl, is_last_arg, clleft, top, FALSE);
      }
    }
  } else {
    if (argno == ap->ArityOfPE) {
      lab = 
	do_var_clauses(cls, max, FALSE, cint, isfirstcl, clleft, fail_l, ap->ArityOfPE+1);
    } else {
      lab = do_index(cls, max, cint, argno+1, fail_l, isfirstcl, clleft, top);
    }
  }
  if (labp && !(lab & 1))
    *labp = (yamop *)lab; /* in case we have a single clause */
  return labp;
}


static yamop *
ExpandIndex(PredEntry *ap) {
  yamop *indx_out;
  yamop **labp;
  int cb;
  struct intermediates cint;

  if ((cb = setjmp(cint.CompilerBotch)) == 3) {
    restore_machine_regs();
    Yap_gcl(Yap_Error_Size, ap->ArityOfPE, ENV, CP);
  } else if (cb == 2) {
    restore_machine_regs();
    if (!Yap_growheap(FALSE, Yap_Error_Size, NULL)) {
      save_machine_regs();
      if (ap->PredFlags & LogUpdatePredFlag) {
	Yap_kill_iblock((ClauseUnion *)ClauseCodeToLogUpdIndex(ap->cs.p_code.TrueCodeOfPred),NULL, ap);
      } else {
	StaticIndex *cl;

	cl = ClauseCodeToStaticIndex(ap->cs.p_code.TrueCodeOfPred);
	Yap_kill_iblock((ClauseUnion *)ClauseCodeToStaticIndex(ap->cs.p_code.TrueCodeOfPred),NULL, ap);
      }
      return NULL;
    }
  } else if (cb == 4) {
    restore_machine_regs();
    if (!Yap_growtrail(Yap_Error_Size)) {
      save_machine_regs();
      if (ap->PredFlags & LogUpdatePredFlag) {
	Yap_kill_iblock((ClauseUnion *)ClauseCodeToLogUpdIndex(ap->cs.p_code.TrueCodeOfPred),NULL, ap);
      } else {
	StaticIndex *cl;

	cl = ClauseCodeToStaticIndex(ap->cs.p_code.TrueCodeOfPred);
	Yap_kill_iblock((ClauseUnion *)cl, NULL, ap);
      }
      return FAILCODE;
    }
  }
 restart_index:
  cint.CodeStart = cint.cpc = cint.BlobsStart = cint.icpc = NIL;
  cint.CurrentPred = ap;
  Yap_ErrorMessage = NULL;
  Yap_Error_Size = 0;
#ifdef DEBUG
  if (Yap_Option['i' - 'a' + 1]) {
    Term tmod = ap->ModuleOfPred;
    if (!tmod) tmod = TermProlog;
    Yap_DebugPutc(Yap_c_error_stream,'>');
    Yap_DebugPutc(Yap_c_error_stream,'\t');
    Yap_plwrite(tmod, Yap_DebugPutc, 0);
    Yap_DebugPutc(Yap_c_error_stream,':');
    if (ap->ModuleOfPred == IDB_MODULE) {
      Term t = Deref(ARG1);
      if (IsAtomTerm(t)) {
	Yap_plwrite(t, Yap_DebugPutc, 0);
      } else if (IsIntegerTerm(t)) {
	Yap_plwrite(t, Yap_DebugPutc, 0);
      } else {
	Functor f = FunctorOfTerm(t);
	Atom At = NameOfFunctor(f);
	Yap_plwrite(MkAtomTerm(At), Yap_DebugPutc, 0);
	Yap_DebugPutc(Yap_c_error_stream,'/');
	Yap_plwrite(MkIntTerm(ArityOfFunctor(f)), Yap_DebugPutc, 0);
      }
    } else {
      if (ap->ArityOfPE == 0) {
	Atom At = (Atom)ap->FunctorOfPred;
	Yap_plwrite(MkAtomTerm(At), Yap_DebugPutc, 0);
      } else {
	Functor f = ap->FunctorOfPred;
	Atom At = NameOfFunctor(f);
	Yap_plwrite(MkAtomTerm(At), Yap_DebugPutc, 0);
	Yap_DebugPutc(Yap_c_error_stream,'/');
	Yap_plwrite(MkIntTerm(ArityOfFunctor(f)), Yap_DebugPutc, 0);
      }
    }
    Yap_DebugPutc(Yap_c_error_stream,'\n');
  }
#endif
  if ((labp = expand_index(&cint)) == NULL) {
    return FAILCODE;
  }
  if (*labp == FAILCODE) {
    return FAILCODE;
  }
#ifdef DEBUG
  if (Yap_Option['i' - 'a' + 1]) {
    Yap_ShowCode(&cint);
  }
#endif
  /* globals for assembler */
  IPredArity = ap->ArityOfPE;
  if (cint.CodeStart) {
    if ((indx_out = Yap_assemble(ASSEMBLING_EINDEX, TermNil, ap, FALSE, &cint)) == NULL) {
      if (!Yap_growheap(FALSE, Yap_Error_Size, NULL)) {
	Yap_Error(SYSTEM_ERROR, TermNil, Yap_ErrorMessage);
	return FAILCODE;
      }
      goto restart_index;
    }
  } else {
    /* single case */
    return *labp;
  }
#ifdef LOW_PROF
  if (ProfilerOn) {
    Yap_inform_profiler_of_clause(indx_out, ProfEnd, ap,1); 
  }
#endif /* LOW_PROF */
  if (indx_out == NULL) {
    return FAILCODE;
  }
  *labp = indx_out;
  if (ap->PredFlags & LogUpdatePredFlag) {
    /* add to head of current code children */
    LogUpdIndex *ic = (LogUpdIndex *)Yap_find_owner_index((yamop *)labp, ap),
      *nic = ClauseCodeToLogUpdIndex(indx_out);
    /* insert myself in the indexing code chain */ 
    nic->SiblingIndex = ic->ChildIndex;
    nic->u.ParentIndex = ic;
    nic->ClFlags &= ~SwitchRootMask;
    ic->ChildIndex = nic;
    ic->ClRefCount++;
  } else {
    /* add to head of current code children */
    StaticIndex *ic = (StaticIndex *)Yap_find_owner_index((yamop *)labp, ap),
      *nic = ClauseCodeToStaticIndex(indx_out);
    /* insert myself in the indexing code chain */ 
    nic->SiblingIndex = ic->ChildIndex;
    ic->ChildIndex = nic;
  }
  return indx_out;
}

yamop *
Yap_ExpandIndex(PredEntry *ap) {
  return ExpandIndex(ap);
}

static path_stack_entry *
push_path(path_stack_entry *sp, yamop **pipc, ClauseDef *clp)
{
  sp->flag = pc_entry;
  sp->u.pce.pi_pc = pipc;
  sp->u.pce.code = clp->Code;
  sp->u.pce.current_code = clp->CurrentCode;
  sp->u.pce.work_pc = clp->u.WorkPC;
  sp->u.pce.tag = clp->Tag;
  return sp+1;
}
		 
static path_stack_entry *
fetch_new_block(path_stack_entry *sp, yamop **pipc, PredEntry *ap)
{
  /* add current position */
  sp->flag = block_entry;
  sp->u.cle.entry_code = pipc;
  if (ap->PredFlags & LogUpdatePredFlag) {
    sp->u.cle.block = (ClauseUnion *)ClauseCodeToLogUpdIndex(*pipc);
  } else {
    sp->u.cle.block = (ClauseUnion *)ClauseCodeToStaticIndex(*pipc);
  }
  return sp+1;
}
		 
static path_stack_entry *
init_block_stack(path_stack_entry *sp, yamop *ipc, PredEntry *ap)
{
  /* add current position */
  
  sp->flag = block_entry;
  sp->u.cle.entry_code = NULL;
  if (ap->PredFlags & LogUpdatePredFlag) {
    sp->u.cle.block = (ClauseUnion *)ClauseCodeToLogUpdIndex(ipc);
  } else {
    sp->u.cle.block = (ClauseUnion *)ClauseCodeToStaticIndex(ipc);
  }
  return sp+1;
}

static path_stack_entry  *
cross_block(path_stack_entry *sp, yamop **pipc, PredEntry *ap)
{
  yamop *ipc = *pipc;
  path_stack_entry *tsp = sp;
  ClauseUnion *block;

  do {
    UInt bsize;
    while ((--tsp)->flag != block_entry);
    block = tsp->u.cle.block;
    if (block->lui.ClFlags & LogUpdMask)
      bsize = block->lui.ClSize;
    else
      bsize = block->si.ClSize;
    if (ipc > (yamop *)block &&
	ipc <= (yamop *)((CODEADDR)block + bsize)) {
      path_stack_entry *nsp = tsp+1;
      for (;tsp<sp;tsp++) {
	if (tsp->flag == pc_entry) {
	  if (nsp != tsp) {
	    nsp->flag = pc_entry;
	    nsp->u.pce.pi_pc = tsp->u.pce.pi_pc;
	    nsp->u.pce.code = tsp->u.pce.code;
	    nsp->u.pce.current_code = tsp->u.pce.current_code;
	    nsp->u.pce.work_pc = tsp->u.pce.work_pc;
	    nsp->u.pce.tag = tsp->u.pce.tag;
	  }
	  nsp++;
	}
      }
      return nsp;
    }
  } while (tsp->u.cle.entry_code != NULL);
  /* moved to a new block */
  return fetch_new_block(sp, pipc, ap);
}


static yamop *
pop_path(path_stack_entry **spp, ClauseDef *clp, PredEntry *ap)
{
  path_stack_entry *sp = *spp;
  yamop *nipc;

  while ((--sp)->flag != pc_entry);
  *spp = sp;
  clp->Code = sp->u.pce.code;
  clp->CurrentCode = sp->u.pce.current_code;
  clp->u.WorkPC = sp->u.pce.work_pc;
  clp->Tag = sp->u.pce.tag;
  if (sp->u.pce.pi_pc == NULL) {
    *spp = sp;
    return NULL;
  }
  nipc = *(sp->u.pce.pi_pc);
  *spp = cross_block(sp, sp->u.pce.pi_pc, ap);
  return nipc;
}

static int
table_fe_overflow(yamop *pc, Functor f)
{
  if (pc->u.sl.s <= MIN_HASH_ENTRIES) {
    /* we cannot expand otherwise */
    COUNT i;
    FuncSwiEntry *csw = (FuncSwiEntry *)pc->u.sl.l;

    for (i=0; i < pc->u.sl.s; i++,csw++) {
      if (csw->Tag == f) return FALSE;
    }
    return TRUE;
  } else {
    COUNT i, free = 0, used;
    FuncSwiEntry *fsw = (FuncSwiEntry *)pc->u.sl.l;
    for (i=0; i<pc->u.sl.s; i++,fsw++) {
      if (fsw->Tag == NULL) free++;
    }
    used = pc->u.sl.s-free;
    used += 1+used/4;
    return (!free || pc->u.sl.s/free > 4);
  }
}

static int
table_ae_overflow(yamop *pc, Term at)
{
  if (pc->u.sl.s <= MIN_HASH_ENTRIES) {
    /* check if we are already there */
    COUNT i;
    AtomSwiEntry *csw = (AtomSwiEntry *)pc->u.sl.l;

    for (i=0; i < pc->u.sl.s; i++,csw++) {
      if (csw->Tag == at) return FALSE;
    }
    return TRUE;
  } else {
    COUNT i, free = 0, used;
    AtomSwiEntry *csw = (AtomSwiEntry *)pc->u.sl.l;
    for (i=0; i<pc->u.sl.s; i++,csw++) {
      if (csw->Tag == 0L) free++;
    }
    used = pc->u.sl.s-free;
    used += 1+used/4;
    return (!free || used >= pc->u.sl.s);
  }
}

static void
replace_index_block(ClauseUnion *parent_block, yamop *cod, yamop *ncod, PredEntry *ap)
{
  if (ap->PredFlags & LogUpdatePredFlag) {
    LogUpdIndex
      *cl = ClauseCodeToLogUpdIndex(cod),
      *ncl = ClauseCodeToLogUpdIndex(ncod),
      *c = parent_block->lui.ChildIndex;
    ncl->SiblingIndex = cl->SiblingIndex;
    ncl->ClRefCount = cl->ClRefCount;
    ncl->ChildIndex = cl->ChildIndex;
    ncl->u.ParentIndex = cl->u.ParentIndex;
    INIT_LOCK(ncl->ClLock);
    if (c == cl) {
      parent_block->lui.ChildIndex = ncl;
    } else {
      while (c->SiblingIndex != cl) {
	c = c->SiblingIndex;
      }
      c->SiblingIndex = ncl;
    }
    c = cl->ChildIndex;
    while (c != NULL) {
      c->u.ParentIndex = ncl;
      c = c->SiblingIndex;
    }
    Yap_FreeCodeSpace((CODEADDR)cl);
  } else {
    StaticIndex
      *cl = ClauseCodeToStaticIndex(cod),
      *ncl = ClauseCodeToStaticIndex(ncod),
      *c = parent_block->si.ChildIndex;
    ncl->SiblingIndex = cl->SiblingIndex;
    if (c == cl) {
      parent_block->si.ChildIndex = ncl;
    } else {
      while (c->SiblingIndex != cl) {
	c = c->SiblingIndex;
      }
      c->SiblingIndex = ncl;
    }
    Yap_FreeCodeSpace((CODEADDR)cl);
  }
}
		 
static AtomSwiEntry *
expand_ctable(yamop *pc, ClauseUnion *blk, struct intermediates *cint, Term at)
{
  PredEntry *ap = cint->CurrentPred;
  int n = pc->u.sl.s, i, i0 = n;
  UInt fail_l = Zero;
  AtomSwiEntry *old_ae = (AtomSwiEntry *)(pc->u.sl.l), *target;

  if (n > MIN_HASH_ENTRIES) {
    AtomSwiEntry *tmp = old_ae;
    int i;
    
    n = 1;
    for (i = 0; i < pc->u.sl.s; i++,tmp++) {
      if (tmp->Tag != Zero) n++;
      else fail_l = tmp->Label;
    }
  } else {
    fail_l = old_ae[n].Label;
    n++;
  }
  if (n > MIN_HASH_ENTRIES) {
    int cases = MIN_HASH_ENTRIES, i, n0;
    n0 = n+1+n/4;
    while (cases < n0) cases *= 2;
    if (cases == pc->u.sl.s) {
      return fetch_centry(old_ae, at, n-1, n);
    }
    /* initialise */
    target = (AtomSwiEntry *)emit_switch_space(cases, sizeof(AtomSwiEntry), cint);
    pc->opc = Yap_opcode(_switch_on_cons);
    pc->u.sl.s = cases;
    for (i=0; i<cases; i++) {
      target[i].Tag = Zero;
      target[i].Label = fail_l;
    }
  } else {
    pc->opc = Yap_opcode(_if_cons);
    pc->u.sl.s = n;
    target = (AtomSwiEntry *)emit_switch_space(n+1, sizeof(AtomSwiEntry), cint);
    target[n].Tag = Zero;
    target[n].Label = fail_l;
  }
  for (i = 0; i < i0; i++,old_ae++) {
    Term tag = old_ae->Tag;

    if (tag != Zero) {
      AtomSwiEntry *ics = fetch_centry(target, tag, i, n);
      ics->Tag = tag;
      ics->Label = old_ae->Label;    
    }
  }
  /* support for threads */
  if (blk)
    replace_index_block(blk, pc->u.sl.l, (yamop *)target, ap);
  pc->u.sl.l = (yamop *)target;
  return fetch_centry(target, at, n-1, n);
}

static FuncSwiEntry *
expand_ftable(yamop *pc, ClauseUnion *blk, struct intermediates *cint, Functor f)
{
  PredEntry *ap = cint->CurrentPred;
  int n = pc->u.sl.s, i, i0 = n;
  UInt fail_l =  Zero;
  FuncSwiEntry *old_fe = (FuncSwiEntry *)(pc->u.sl.l), *target;

  if (n > MIN_HASH_ENTRIES) {
    FuncSwiEntry *tmp = old_fe;
    int i;
    
    n = 1;
    for (i = 0; i < pc->u.sl.s; i++,tmp++) {
      if (tmp->Tag != Zero) n++;
      else fail_l = tmp->Label;
    }
  } else {
    fail_l = old_fe[n].Label;
    n++;
  }
  if (n > MIN_HASH_ENTRIES) {
    int cases = MIN_HASH_ENTRIES, i, n0;
    n0 = n+1+n/4;
    while (cases < n0) cases *= 2;

    if (cases == pc->u.sl.s) {
      return fetch_fentry(old_fe, f, n-1, n);
    }
    pc->opc = Yap_opcode(_switch_on_func);
    pc->u.sl.s = cases;
    /* initialise */
    target = (FuncSwiEntry *)emit_switch_space(cases, sizeof(FuncSwiEntry), cint);
    for (i=0; i<cases; i++) {
      target[i].Tag = NULL;
      target[i].Label = fail_l;
    }
  } else {
    pc->opc = Yap_opcode(_if_func);
    pc->u.sl.s = n;
    target = (FuncSwiEntry *)emit_switch_space(n+1, sizeof(FuncSwiEntry), cint);
    target[n].Tag = Zero;
    target[n].Label = fail_l;
  }
  for (i = 0; i < i0; i++,old_fe++) {
    Functor f = old_fe->Tag;

    if (f != NULL) {
      FuncSwiEntry *ifs = fetch_fentry(target, f, i, n);
      ifs->Tag = old_fe->Tag;
      ifs->Label = old_fe->Label;    
    }
  }
  replace_index_block(blk, pc->u.sl.l, (yamop *)target, ap);
  pc->u.sl.l = (yamop *)target;
  return fetch_fentry(target, f, n-1, n);
}

static ClauseUnion *
current_block(path_stack_entry *sp)
{
  while ((--sp)->flag != block_entry);
  return sp->u.cle.block;
}

static path_stack_entry *
kill_block(path_stack_entry *sp, PredEntry *ap)
{
  while ((--sp)->flag != block_entry);
  if (sp->u.cle.entry_code == NULL) {
    Yap_kill_iblock(sp->u.cle.block, NULL, ap);
  } else {
    path_stack_entry *nsp = sp;
    
    while ((--nsp)->flag != block_entry);
    Yap_kill_iblock(sp->u.cle.block, nsp->u.cle.block, ap);
    *sp->u.cle.entry_code = (yamop *)&(ap->cs.p_code.ExpandCode);
  }
  return sp;
}

static path_stack_entry *
kill_clause(yamop *ipc, yamop *bg, yamop *lt, path_stack_entry *sp, PredEntry *ap)
{
  LogUpdIndex *blk;
  yamop *start;
  op_numbers op0;

  while ((--sp)->flag != block_entry);
  blk = (LogUpdIndex *)(sp->u.cle.block);
  start = blk->ClCode;
  op0 = Yap_op_from_opcode(start->opc);
  while (op0 == _jump_if_nonvar) {
    start = NEXTOP(start, xl);
    op0 = Yap_op_from_opcode(start->opc);
  }
  if ((op0 != _enter_lu_pred && op0 != _stale_lu_index)
      || !start->u.Ill.s /* weird block */) {
    return kill_block(sp+1, ap);
  } else {
    /* decrease number of clauses */
    start->u.Ill.s--;
    if (start->u.Ill.s == 1) {
      yamop *codep = start->u.Ill.l1;

      /* search for the one clause that has been left */
      while (TRUE) {
	op_numbers op = Yap_op_from_opcode(codep->opc);
	switch (op) {
	case _trust:
	case _retry:
	case _try_clause:
	  /* kill block and replace by this single clause */
	  if (codep->u.ld.d != FAILCODE) {
	    path_stack_entry *nsp;
	    LogUpdClause *tgl = ClauseCodeToLogUpdClause(codep->u.ld.d);
    
	    if (tgl->ClFlags & ErasedMask ||
		IN_BETWEEN(bg, codep->u.ld.d, lt)) {
	      codep = NEXTOP(codep,ld);
	      break;
	    }
	    nsp = sp;
	    while ((--nsp)->flag != block_entry);
	    *sp->u.cle.entry_code = codep->u.ld.d;
	    Yap_kill_iblock(sp->u.cle.block, nsp->u.cle.block, ap);
	    return sp;
	  } else {
	    codep = NEXTOP(codep,ld);
	  }
	  break;
	case _trust_logical_pred:
	  codep = NEXTOP(codep, l);
	  break;
	case _retry_profiled:
	case _count_call:
	  codep = NEXTOP(codep, p);
	  break;
	default:
	  Yap_Error(FATAL_ERROR, TermNil, "Invalid Opcode");
	  return sp;
	}
      }
    }
    /* just mark the clause as dead and the code as unreachable, but 
       don't do anything else
    */
    if (IN_BETWEEN(bg, start->u.Ill.l1->u.ld.d, lt)) {
      start->u.Ill.l1->u.ld.d = FAILCODE;
    }
    start->opc = Yap_opcode(_stale_lu_index);
    return sp;
  }
}

static yamop *
copy_ld(yamop *codep, yamop *ocodep, PredEntry *ap, yamop *code, int has_cut)
{
  codep->u.ld.s = ap->ArityOfPE;
  codep->u.ld.p = ap; 
  codep->u.ld.d = code;
#ifdef YAPOR
  /* FIX ME */
  codep->u.ld.or_arg = ocodep->u.ld.or_arg;
#endif /* YAPOR */
#ifdef TABLING
  codep->u.ld.te = ocodep->u.ld.te;
#endif /* TABLING */
  return NEXTOP(codep, ld);
}

static yamop *
gen_lui_retry(yamop *codep, yamop *ocodep, int profiled, int count_call, PredEntry *ap)
{
    if (profiled) {
      codep->opc = Yap_opcode(_retry_profiled);
      codep->u.p.p = ap;
      codep = NEXTOP(codep,p);
    }
    if (count_call) {
      codep->opc = Yap_opcode(_count_retry);
      codep->u.p.p = ap;
      codep = NEXTOP(codep,p);
    }
    codep->opc = Yap_opcode(_retry);
    return copy_ld(codep, ocodep, ap, ocodep->u.ld.d, FALSE);
}

static yamop *
gen_lui_trust(yamop *codep, yamop *ocodep, int profiled, int count_call, PredEntry *ap, yamop *code, int has_cut, LogUpdIndex *blk)
{
    if (profiled) {
      codep->opc = Yap_opcode(_retry_profiled);
      codep->u.p.p = ap;
      codep = NEXTOP(codep,p);
    }
    if (count_call) {
      codep->opc = Yap_opcode(_count_call);
      codep->u.p.p = ap;
      codep = NEXTOP(codep,p);
    }
    codep->opc = Yap_opcode(_trust_logical_pred);
    codep->u.l.l = (yamop *)blk;
    codep = NEXTOP(codep,l);
    codep->opc = Yap_opcode(_trust);
    return copy_ld(codep, ocodep, ap, code, has_cut);
}

static void
clean_ref_to_clause(LogUpdClause *tgl, op_numbers op, int compact_mode)
{
  if (op == _try_clause || !compact_mode)
    return;
  LOCK(tgl->ClLock);
  tgl->ClRefCount--;
  if ((tgl->ClFlags & ErasedMask) &&
      !(tgl->ClRefCount) &&
      !(tgl->ClFlags & InUseMask)) {
    /* last ref to the clause */
    UNLOCK(tgl->ClLock);
    Yap_ErLogUpdCl(tgl);
  } else {
    UNLOCK(tgl->ClLock);
  }
}

static yamop *
cp_lu_trychain(yamop *codep, yamop *ocodep, yamop *ostart, int flag, PredEntry *ap, yamop *code, int has_cut, LogUpdIndex *nblk, UInt ncls, UInt i)
{
  int count_reds = ap->PredFlags & CountPredFlag;
  int profiled = ap->PredFlags & ProfiledPredFlag;
  int compact_mode = (codep == ocodep);

  while (ocodep != NULL &&
	 ocodep < ostart->u.Ill.l2) {
    op_numbers op = Yap_op_from_opcode(ocodep->opc);
    switch (op) {
    case _try_clause:
      if (ocodep->u.ld.d == FAILCODE) {
	ocodep = NEXTOP(ocodep, ld);
	break;
      }
    case _retry:
    case _trust:
      {
	/* set up a try_clause */
	LogUpdClause *tgl = ClauseCodeToLogUpdClause(ocodep->u.ld.d);

	if (tgl->ClFlags & ErasedMask) {
	  clean_ref_to_clause(tgl, op, compact_mode);
	  ocodep = NEXTOP(ocodep, ld);
	  break;
	} else if (i == 0) {
	  codep->opc = Yap_opcode(_try_clause);
	  codep = copy_ld(codep, ocodep, ap, ocodep->u.ld.d, FALSE);
	} else if (i == ncls-1) {
	  if (!compact_mode) {
	    tgl->ClRefCount++;
	  } else {
	    Yap_cleanup_dangling_indices(NEXTOP(ocodep,ld),ostart->u.Ill.l1,ostart->u.Ill.l2,(yamop *)&(ap->cs.p_code.ExpandCode));
	  }
	  codep = gen_lui_trust(codep, ocodep, profiled, count_reds, ap, ocodep->u.ld.d, TRUE, nblk);
	  ocodep = NULL;
	  break;
	} else {
	  if (op == _try_clause || !compact_mode) {
	    tgl->ClRefCount++;
	  }
	  codep = gen_lui_retry(codep, ocodep, profiled, count_reds, ap);
	}
      }
      i++;
      ocodep = NEXTOP(ocodep, ld);
      break;
    case _trust_logical_pred:
      ocodep = NEXTOP(ocodep, l);
      break;
    case _retry_profiled:
    case _count_call:
      ocodep = NEXTOP(ocodep, p);
      break;
    default:
      Yap_Error(FATAL_ERROR, TermNil, "Invalid Opcode");
    }
  }
  if (flag == RECORDZ) {
    codep = gen_lui_trust(codep, ocodep, profiled, count_reds, ap, code, has_cut, nblk);    
  }
  return codep;
}

static yamop *
replace_lu_block(LogUpdIndex *blk, int flag, PredEntry *ap, yamop *code, int has_cut)
{
  yamop *begin = blk->ClCode, *codep, *start, *ocodep;
  yamop *nbegin;
  UInt ncls, xcls, jnvs = 0;
  UInt sz, i;
  LogUpdIndex *ncl, *pcl;
  int count_reds = ap->PredFlags & CountPredFlag;
  int profiled = ap->PredFlags & ProfiledPredFlag;
  op_numbers op = Yap_op_from_opcode(begin->opc);

  
  while (op == _jump_if_nonvar) {
    jnvs++;
    begin = NEXTOP(begin, xl);
    op = Yap_op_from_opcode(begin->opc);
  }
  /* add half the current space plus 1, and also the extra clause */
  if (flag == RECORDA || flag == RECORDZ) {
    /* we are still introducing a clause */
    ncls = ++(begin->u.Ill.s);
    xcls = ncls+ncls/2+2;
  } else {
    ncls = begin->u.Ill.s;
    xcls = ncls;
  }
  sz = sizeof(LogUpdIndex)+
    xcls*((UInt)NEXTOP((yamop *)NULL,ld))+
    jnvs*((UInt)NEXTOP((yamop *)NULL,l))+
    (UInt)NEXTOP((yamop *)NULL,Ill)+
    (UInt)NEXTOP((yamop *)NULL,p);
  if (count_reds) sz += xcls*((UInt)NEXTOP((yamop *)NULL,p));
  if (profiled) sz += xcls*((UInt)NEXTOP((yamop *)NULL,p));
  ncl = (LogUpdIndex *)Yap_AllocCodeSpace(sz);
  if (ncl == NULL) {
    Yap_Error_Size = sz;
    Yap_ErrorMessage = "while at indexing code";
    return NULL;
  }
  ncl->ClFlags = LogUpdMask|IndexedPredFlag|IndexMask;
  if (blk->ClFlags & SwitchRootMask) {
      ncl->ClFlags |= SwitchRootMask;
      ncl->u.pred = blk->u.pred;
  } else {
    ncl->u.ParentIndex = blk->u.ParentIndex;
  }
  ncl->ClRefCount = 0;
  {
    LogUpdIndex *idx = ncl->ChildIndex = blk->ChildIndex;
    while (idx) {
      blk->ClRefCount--;
      idx = idx->SiblingIndex;
    }
  }
  blk->ChildIndex = NULL;
  ncl->ClSize = sz;
  INIT_LOCK(ncl->ClLock);
  INIT_CLREF_COUNT(ncl);
  nbegin = ncl->ClCode;
  begin = blk->ClCode;
  while (jnvs--) {
    nbegin->opc = begin->opc;
    begin = NEXTOP(begin, xl);
    nbegin = NEXTOP(nbegin, xl);
  }
  codep = start = nbegin;
  /* ok, we've allocated and set up things, now let's finish */
  codep->opc = Yap_opcode(_enter_lu_pred);
  codep->u.Ill.s = begin->u.Ill.s;
  codep->u.Ill.I = ncl;
  codep = NEXTOP(codep,Ill);
  ocodep = begin->u.Ill.l1;
  if (flag == RECORDA) {
    int j;

    for (j=0; j < ncls/2; j++) {
      codep = NEXTOP(codep, ld);
      if (profiled) codep = NEXTOP(codep, p);
      if (count_reds) codep = NEXTOP(codep, p);
    }
    start->u.Ill.l1 = codep;
    i = 1;
    codep->opc = Yap_opcode(_try_clause);
    codep = copy_ld(codep, ocodep, ap, code, has_cut);
  } else if (flag == RECORDZ) {
    LogUpdClause *tgl = ClauseCodeToLogUpdClause(code);

    tgl->ClRefCount++;
    start->u.Ill.l1 = codep;
    i = 0;
  } else {
    start->u.Ill.l1 = codep;    
    i = 0;
  }
  codep = cp_lu_trychain(codep, ocodep, begin, flag, ap, code, has_cut, ncl, ncls, i);
  /* the copying has been done */
  start->u.Ill.l2 = codep;
  /* insert ourselves into chain */
  if (blk->ClFlags & SwitchRootMask) {
    Yap_kill_iblock((ClauseUnion *)blk, NULL, ap);
  } else {
    pcl = blk->u.ParentIndex;
    ncl->SiblingIndex = pcl->ChildIndex;
    pcl->ChildIndex = ncl;
    /* we have a new pointer to our clause */
    pcl->ClRefCount++;
    if (!(blk->ClFlags & ErasedMask)) {
      Yap_kill_iblock((ClauseUnion *)blk, (ClauseUnion *)pcl, ap);
    }
  }
  return start;
}

static yamop *
clean_up_index(LogUpdIndex *blk, yamop **jlbl, PredEntry *ap)
{
  yamop *codep = blk->ClCode;
  UInt ncls = codep->u.Ill.s;

  if (blk->ClFlags & InUseMask) {
    /* make a new block */
    yamop *new = replace_lu_block(blk, REFRESH, ap, NULL, FALSE);
    if (jlbl)
      *jlbl = new;
    return new;
  } else {
    /* work on the current block */
    op_numbers op = Yap_op_from_opcode(codep->opc);

    while (op == _jump_if_nonvar) {
      codep = NEXTOP(codep, xl);
      op = Yap_op_from_opcode(codep->opc);
    }
    codep->u.Ill.l2 = cp_lu_trychain(codep->u.Ill.l1, codep->u.Ill.l1, codep, REFRESH, ap, NULL, FALSE, blk, ncls, 0);
    return codep->u.Ill.l1;
  }
}

static yamop *
insertz_in_lu_block(LogUpdIndex *blk, PredEntry *ap, yamop *code)
{
  op_numbers op = Yap_op_from_opcode(blk->ClCode->opc);
  yamop *end, *last, *where, *next;
  UInt bsize;
  yamop *begin = blk->ClCode;

  /* make sure this is something I can work with */
  while (op == _jump_if_nonvar) {
    begin = NEXTOP(begin, xl);
    op = Yap_op_from_opcode(begin->opc);
  }
  if (op != _enter_lu_pred && op != _stale_lu_index) {
    if (blk->ClFlags & SwitchRootMask) {
      Yap_kill_iblock((ClauseUnion *)blk, NULL, ap);
    } else {
      Yap_kill_iblock((ClauseUnion *)blk, (ClauseUnion *)blk->u.ParentIndex, ap);
    }
    return (yamop *)&(ap->cs.p_code.ExpandCode);
  }
  /* ok, we are in a sequence of try-retry-trust instructions, or something
     similar */
  bsize = blk->ClSize;
  end = (yamop *)((CODEADDR)blk+bsize);
  where = last = begin->u.Ill.l2;
  next = NEXTOP(NEXTOP(where, ld),p);  /* trust logical followed by trust */
  last = PREVOP(last, ld);
  /* follow profiling and counting instructions */ 
  if (ap->PredFlags & ProfiledPredFlag) {
    next = NEXTOP(next, p);
  }
  if (ap->PredFlags & CountPredFlag) {
    next = NEXTOP(next, p);
  }
  if (next <= end) {
    /* we got space to put something in */
    LogUpdClause *tgl = ClauseCodeToLogUpdClause(code);

    if (begin->opc != Yap_opcode(_stale_lu_index)) {
      if (blk->ClFlags & InUseMask) {
	begin->opc = Yap_opcode(_stale_lu_index);
      } else {
	/* we need to rebuild the code */
	/* first, shift the last retry down, getting rid of the trust logical pred */
	yamop *nlast = PREVOP(last, l);
	  
	memmove((void *)nlast, (void *)last, (CELL)NEXTOP((yamop *)NULL,ld));
	nlast->opc = Yap_opcode(_retry);
	where = NEXTOP(nlast,ld);
	if (ap->PredFlags & ProfiledPredFlag) {
	  where->opc = Yap_opcode(_retry_profiled);
	  where->u.p.p = ap;
	  where = NEXTOP(where, p);
	}
	if (ap->PredFlags & CountPredFlag) {
	  where->opc = Yap_opcode(_count_retry);
	  where->u.p.p = ap;
	  where = NEXTOP(where, p);
	}
	where->opc = Yap_opcode(_trust_logical_pred);
	where->u.l.l = (yamop *)blk;
	where = NEXTOP(where, l);
      }
    }
    where->opc = Yap_opcode(_trust);
    where->u.ld.s = ap->ArityOfPE;
    where->u.ld.p = ap; 
    where->u.ld.d = code;
#ifdef YAPOR
    /* FIX ME */
    where->u.ld.or_arg = last->u.ld.or_arg;
#endif /* YAPOR */
#ifdef TABLING
    where->u.ld.te = last->u.ld.te;
#endif /* TABLING */
    begin->u.Ill.l2 = NEXTOP(where,ld);
    begin->u.Ill.s++;
    tgl->ClRefCount++;
    return begin;
  } else {
    return replace_lu_block(blk, RECORDZ, ap, code, has_cut(code));
  }
}

static yamop *
inserta_in_lu_block(LogUpdIndex *blk, PredEntry *ap, yamop *code)
{
  op_numbers op = Yap_op_from_opcode(blk->ClCode->opc);
  yamop *start, *next, *here;
  yamop *begin = blk->ClCode;

  /* make sure this is something I can work with */
  while (op == _jump_if_nonvar) {
    begin = NEXTOP(begin, xl);
    op = Yap_op_from_opcode(begin->opc);
  }
  if (op != _enter_lu_pred && op != _stale_lu_index) {
    if (blk->ClFlags & SwitchRootMask) {
      Yap_kill_iblock((ClauseUnion *)blk, NULL, ap);
    } else {
      Yap_kill_iblock((ClauseUnion *)blk, (ClauseUnion *)blk->u.ParentIndex, ap);
    }
    return (yamop *)&(ap->cs.p_code.ExpandCode);
  }
  /* ok, we are in a sequence of try-retry-trust instructions, or something
     similar */
  here = next = begin->u.Ill.l1;
  if (here->opc == Yap_opcode(_try_clause) && here->u.ld.d == FAILCODE) {
    begin->u.Ill.s++;
    here->u.ld.d = code;
    return begin;
  }
  start = NEXTOP(begin,Ill);
  here = PREVOP(here, ld);
  /* follow profiling and counting instructions */ 
  if (ap->PredFlags & ProfiledPredFlag) {
    next = NEXTOP(next, p);
    here = PREVOP(here, p);
  }
  if (ap->PredFlags & CountPredFlag) {
    next = NEXTOP(next, p);
    here = PREVOP(here, p);
  }
  if (here >= start) {
    /* we got space to put something in */
    LogUpdClause *tgl = ClauseCodeToLogUpdClause(next->u.ld.d);
    next->opc = Yap_opcode(_retry);
    tgl->ClRefCount++;
    begin->u.Ill.l1 = here;
    begin->u.Ill.s++;
    here->opc = Yap_opcode(_try_clause);
    here->u.ld.s = next->u.ld.s;
    here->u.ld.p = ap; 
    here->u.ld.d = code;
#ifdef YAPOR
    /* FIX ME */
    here->u.ld.or_arg = next->u.ld.or_arg;
#endif /* YAPOR */
#ifdef TABLING
    here->u.ld.te = next->u.ld.te;
#endif /* TABLING */
    here = NEXTOP(here,ld);
    if (ap->PredFlags & ProfiledPredFlag) {
      here->opc = Yap_opcode(_retry_profiled);
      here->u.p.p = ap;
      here = NEXTOP(here, p);
    }
    if (ap->PredFlags & ProfiledPredFlag) {
      here->opc = Yap_opcode(_retry_profiled);
      here->u.p.p = ap;
    }
    return begin;
  } else {
    return replace_lu_block(blk, RECORDA, ap, code, has_cut(code));
  }
}

static path_stack_entry *
expanda_block(path_stack_entry *sp, PredEntry *ap, ClauseDef *cls, int group1, yamop *alt, struct intermediates *cint)
{
  while ((--sp)->flag != block_entry);
  if (sp->u.cle.entry_code == NULL) {
    Yap_kill_iblock(sp->u.cle.block, NULL, ap);
  } else if (ap->PredFlags & LogUpdatePredFlag &&
	     group1 && alt == NULL) {
    yamop *new_code = 
      inserta_in_lu_block((LogUpdIndex *)sp->u.cle.block, ap, cls->Code);
    if (new_code == NULL) 
      longjmp(cint->CompilerBotch,2);
    *sp->u.cle.entry_code = new_code;
  } else {
    path_stack_entry *nsp = sp;
    
    while ((--nsp)->flag != block_entry);
    Yap_kill_iblock(sp->u.cle.block, nsp->u.cle.block, ap);
    *sp->u.cle.entry_code = (yamop *)&(ap->cs.p_code.ExpandCode);
  }
  return sp;
}

static path_stack_entry *
expandz_block(path_stack_entry *sp, PredEntry *ap, ClauseDef *cls, int group1, yamop *alt, struct intermediates *cint)
{
  while ((--sp)->flag != block_entry);
  if (sp->u.cle.entry_code == NULL) {
    Yap_kill_iblock(sp->u.cle.block, NULL, ap);
  } else if (ap->PredFlags & LogUpdatePredFlag &&
	     group1 && alt == NULL) {
    yamop *new_code = 
      insertz_in_lu_block((LogUpdIndex *)sp->u.cle.block, ap, cls->Code);
    if (new_code == NULL) 
      longjmp(cint->CompilerBotch,2);
    *sp->u.cle.entry_code = 
      new_code;
  } else {
    path_stack_entry *nsp = sp;
    
    while ((--nsp)->flag != block_entry);
    Yap_kill_iblock(sp->u.cle.block, nsp->u.cle.block, ap);
    *sp->u.cle.entry_code = (yamop *)&(ap->cs.p_code.ExpandCode);
  }
  return sp;
}

/* this code should be called when we jumped to clauses */
static path_stack_entry *
kill_unsafe_block(path_stack_entry *sp, op_numbers op, PredEntry *ap)
{
  yamop *ipc;
  while ((--sp)->flag != block_entry);
  if (sp->u.cle.entry_code == NULL)
    return sp;
  ipc = *sp->u.cle.entry_code;
  if (Yap_op_from_opcode(ipc->opc) == op) {
    /* the new block was the current clause */
    *sp->u.cle.entry_code = (yamop *)&(ap->cs.p_code.ExpandCode);
    return sp;
  }
  /* we didn't have protection, should kill now */
  return kill_block(sp+1, ap);
}


static void
add_to_index(struct intermediates *cint, int first, path_stack_entry *sp, ClauseDef *cls) {
  /* last clause to experiment with */
  PredEntry *ap = cint->CurrentPred;
  yamop *ipc = ap->cs.p_code.TrueCodeOfPred;
  int group1 = TRUE;
  yamop *alt = NULL;
  UInt current_arity = 0;
  int last_arg = TRUE;

  sp = init_block_stack(sp, ipc, ap);
  /* try to refine the interval using the indexing code */
  while (ipc != NULL) {
    op_numbers op = Yap_op_from_opcode(ipc->opc);

    switch(op) {
    case _try_clause:
      /* I cannot expand a predicate that starts on a variable,
         have to expand the index.
      */
      if (first) {
	sp = expanda_block(sp, ap, cls, group1, alt, cint);
	ipc = pop_path(&sp, cls, ap);
      } else {
	/* just go to next instruction */
	ipc = NEXTOP(ipc,ld);
      }
      break;
    case _stale_lu_index:
    case _enter_lu_pred:
      if (first) {
	sp = expanda_block(sp, ap, cls, group1, alt, cint);
      } else {
	sp = expandz_block(sp, ap, cls, group1, alt, cint);
      }
      ipc = pop_path(&sp, cls, ap);
      break;
    case _retry:
      /* this clause had no indexing */
      ipc = NEXTOP(ipc,ld);
      break;
      /* instructions type l */
    case _retry_me:
    case _retry_me1:
    case _retry_me2:
    case _retry_me3:
    case _retry_me4:
      /* should never be reached both for asserta */
      group1 = FALSE;
      ipc = ipc->u.ld.d;
      break;
    case _try_me:
    case _try_me1:
    case _try_me2:
    case _try_me3:
    case _try_me4:
      if (first) {
	ipc = NEXTOP(ipc,ld);
	alt = ipc->u.ld.d;
      } else {
	ipc = ipc->u.ld.d;
	group1 = FALSE;
      }
      break;
    case _retry_profiled:
    case _count_retry:
      ipc = NEXTOP(ipc, ld);
      break;
    case _profiled_trust_me:
    case _trust_me:
    case _count_trust_me:
    case _trust_me1:
    case _trust_me2:
    case _trust_me3:
    case _trust_me4:
      group1 = FALSE;
      ipc = NEXTOP(ipc, ld);
      break;
    case _trust_logical_pred:
      ipc = NEXTOP(ipc, l);
      break;
    case _trust:
      sp = expandz_block(sp, ap, cls, group1, alt, cint);
      ipc = pop_path(&sp, cls, ap);
      break;
    case _jump:
      sp = cross_block(sp, &ipc->u.l.l, ap);
      /* just skip for now, but should worry about memory management */
      ipc = ipc->u.l.l;
      break;
    case _jump_if_var:
      sp = push_path(sp, &(ipc->u.l.l), cls);
      ipc = NEXTOP(ipc,l);
      break;
    case _jump_if_nonvar:
      sp = push_path(sp, &(ipc->u.xl.l), cls);
      ipc = NEXTOP(ipc,xl);
      break;
      /* instructions type EC */
    case _try_in:
      /* we are done */
      if (first) {
	sp = kill_block(sp, ap);
	ipc = pop_path(&sp, cls, ap);
      } else {
	ipc = NEXTOP(ipc,l);
      }
      break;
      /* instructions type e */
    case _switch_on_type:
      sp = push_path(sp, &(ipc->u.llll.l4), cls);
      if (ap->PredFlags & LogUpdatePredFlag) {
	add_head_info(cls, 1);
      } else {
	add_info(cls, 1);
      }
      if (IsPairTerm(cls->Tag)) {
	yamop *nipc = ipc->u.llll.l1;

	current_arity = 2;
	move_next(cls, 1);
	if (nipc == FAILCODE) {
	  /* jump straight to clause */
	  ipc->u.llll.l1 = cls->CurrentCode;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* go on */
	  sp = cross_block(sp, &ipc->u.llll.l1, ap);
	  ipc = nipc;	
	}
      } else if (IsAtomOrIntTerm(cls->Tag)) {
	yamop *nipc = ipc->u.llll.l2;
	move_next(cls, 1);
	if (nipc == FAILCODE) {
	  /* need to expand the block */
	  sp = kill_block(sp, ap);
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else if (IsApplTerm(cls->Tag)) {
	yamop *nipc = ipc->u.llll.l3;
	if (nipc == FAILCODE) {
	  /* need to expand the block */
	  sp = kill_block(sp, ap);
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else {
	/* we can't separate into four groups,
	   need to restart.
	*/
	sp = kill_block(sp, ap);
	ipc = pop_path(&sp, cls, ap);
      }
      break;
    case _switch_list_nl:
      sp = push_path(sp, &(ipc->u.ollll.l4), cls);
      if (ap->PredFlags & LogUpdatePredFlag) {
	add_head_info(cls, 1);
      } else {
	add_info(cls, 1);
      }
      if (IsPairTerm(cls->Tag)) {
	yamop *nipc = ipc->u.ollll.l1;

	current_arity = 2;
	move_next(cls, 1);
	if (nipc == FAILCODE) {
	  /* jump straight to clause */
	  ipc->u.ollll.l1 = cls->CurrentCode;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* go on */
	  sp = cross_block(sp, &ipc->u.ollll.l1, ap);
	  ipc = nipc;	
	}
      } else if (IsAtomOrIntTerm(cls->Tag)) {
	yamop *nipc = ipc->u.ollll.l2;
	move_next(cls, 1);
	if (nipc == FAILCODE) {
	  /* need to expand the block */
	  sp = kill_block(sp, ap);
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else if (IsApplTerm(cls->Tag)) {
	yamop *nipc = ipc->u.ollll.l3;
	if (nipc == FAILCODE) {
	  /* need to expand the block */
	  sp = kill_block(sp, ap);
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else {
	/* we can't separate into four groups,
	   need to restart.
	*/
	sp = kill_block(sp, ap);
	ipc = pop_path(&sp, cls, ap);
      }
      break;
    case _switch_on_arg_type:
      sp = push_path(sp, &(ipc->u.xllll.l4), cls);
      if (ap->PredFlags & LogUpdatePredFlag) {
	add_head_info(cls, Yap_regtoregno(ipc->u.xllll.x));
      } else {
	add_info(cls, Yap_regtoregno(ipc->u.xllll.x));
      }
      if (IsPairTerm(cls->Tag)) {
	yamop *nipc = ipc->u.xllll.l1;

	current_arity = 2;
	move_next(cls, Yap_regtoregno(ipc->u.xllll.x));
	if (nipc == FAILCODE) {
	  /* jump straight to clause */
	  ipc->u.xllll.l1 = cls->CurrentCode;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* go on */
	  sp = cross_block(sp, &ipc->u.xllll.l1, ap);
	  ipc = nipc;	
	}
      } else if (IsAtomOrIntTerm(cls->Tag)) {
	yamop *nipc = ipc->u.xllll.l2;
	move_next(cls, Yap_regtoregno(ipc->u.xllll.x));
	if (nipc == FAILCODE) {
	  /* need to expand the block */
	  sp = kill_block(sp, ap);
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else if (IsApplTerm(cls->Tag)) {
	yamop *nipc = ipc->u.xllll.l3;
	move_next(cls, Yap_regtoregno(ipc->u.xllll.x));
	if (nipc == FAILCODE) {
	  /* need to expand the block */
	  sp = kill_block(sp, ap);
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else {
	/* we can't separate into four groups,
	   need to restart.
	*/
	sp = kill_block(sp, ap);
	ipc = pop_path(&sp, cls, ap);
      }
      break;
    case _switch_on_sub_arg_type:
      sp = push_path(sp, &(ipc->u.sllll.l4), cls);
      add_arg_info(cls, ap, ipc->u.sllll.s+1);
      if (IsPairTerm(cls->Tag)) {
	yamop *nipc = ipc->u.sllll.l1;
	current_arity = 2;
	skip_to_arg(cls, ap, ipc->u.sllll.s, current_arity);
	if (current_arity != ipc->u.sllll.s+1) {
	  last_arg = FALSE;
	}
	if (nipc == FAILCODE) {
	  /* jump straight to clause */
	  ipc->u.sllll.l1 = cls->CurrentCode;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* go on */
	  sp = cross_block(sp, &ipc->u.sllll.l1, ap);
	  ipc = nipc;	
	}
      } else if (IsAtomOrIntTerm(cls->Tag)) {
	yamop *nipc = ipc->u.sllll.l2;
	skip_to_arg(cls, ap, ipc->u.sllll.s, current_arity);
	if (current_arity != ipc->u.sllll.s+1) {
	  last_arg = FALSE;
	}
	if (nipc == FAILCODE) {
	  /* need to expand the block */
	  sp = kill_block(sp, ap);
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else if (IsApplTerm(cls->Tag)) {
	yamop *nipc = ipc->u.sllll.l3;
	skip_to_arg(cls, ap, ipc->u.sllll.s, current_arity);
	if (current_arity != ipc->u.sllll.s+1) {
	  last_arg = FALSE;
	}
	if (nipc == FAILCODE) {
	  /* need to expand the block */
	  sp = kill_block(sp, ap);
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else {
	/* we can't separate into four groups,
	   need to restart.
	*/
	sp = kill_block(sp, ap);
	ipc = pop_path(&sp, cls, ap);
      }
      break;
    case _if_not_then:
      ipc = pop_path(&sp, cls, ap);
      break;
      /* instructions type ollll */
    case _switch_on_func:
    case _if_func:
    case _go_on_func:
      {
	FuncSwiEntry *fe;
	yamop *newpc;
	Functor f = (Functor)RepAppl(cls->Tag);
	
	if (op == _switch_on_func) {
	  fe = lookup_f_hash(f, ipc->u.sl.l, ipc->u.sl.s);
	} else {
	  fe = lookup_f(f, ipc->u.sl.l, ipc->u.sl.s);
	}
	if (!IsExtensionFunctor(f)) {
	  current_arity = ArityOfFunctor(f);
	}
	newpc = (yamop *)(fe->Label);

	if (newpc == (yamop *)&(ap->cs.p_code.ExpandCode)) {
	  /* we found it */
	  ipc = pop_path(&sp, cls, ap);
	} else if (newpc == FAILCODE) {
	  /* oops, nothing there */
	  if (fe->Tag != f) {
	    if (IsExtensionFunctor(f)) {
	      sp = kill_unsafe_block(sp, op, ap);
	      ipc = pop_path(&sp, cls, ap);
	      break;
	    }
	    if (table_fe_overflow(ipc, f)) {
	      fe = expand_ftable(ipc, current_block(sp), cint, f);
	    }
	    fe->Tag = f;
	  }
	  fe->Label = (UInt)cls->CurrentCode;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  yamop *newpc = (yamop *)(fe->Label);
	  sp = fetch_new_block(sp, &(ipc->u.sl.l), ap);
	  sp = cross_block(sp, (yamop **)&(fe->Label), ap);
	  ipc = newpc;
	}
      }
      break;
    case _index_dbref:
      cls->Tag = cls->u.t_ptr;
      ipc = NEXTOP(ipc,e);
      break;
    case _index_blob:
      cls->Tag = MkIntTerm(RepAppl(cls->u.t_ptr)[1]);
      ipc = NEXTOP(ipc,e);
      break;
    case _switch_on_cons:
    case _if_cons:
    case _go_on_cons:
      {
	AtomSwiEntry *ae;
	yamop *newpc;
	Term at = cls->Tag;
	
	if (op == _switch_on_cons) {
	  ae = lookup_c_hash(at,ipc->u.sl.l,ipc->u.sl.s);
	} else {
	  ae = lookup_c(at, ipc->u.sl.l, ipc->u.sl.s);
	}
	newpc = (yamop *)(ae->Label);

	if (newpc == (yamop *)&(ap->cs.p_code.ExpandCode)) {
	  /* nothing more to do */
	  ipc = pop_path(&sp, cls, ap);
	} else if (newpc == FAILCODE) {
	  /* oops, nothing there */
	  if (ae->Tag != at) {
	    if (table_ae_overflow(ipc, at)) {
	      ae = expand_ctable(ipc, current_block(sp), cint, at);
	    }
	    ae->Tag = at;
	  }
	  ae->Label = (UInt)cls->CurrentCode;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  yamop *newpc = (yamop *)(ae->Label);
	  sp = fetch_new_block(sp, &(ipc->u.sl.l), ap);
	  sp = cross_block(sp, (yamop **)&(ae->Label), ap);
	  ipc = newpc;
	}
      }
      break;
    case _expand_index:
      ipc = pop_path(&sp, cls, ap);
      break;
    case _lock_lu:
      ipc = NEXTOP(ipc,p);
      break;
    case _unlock_lu:
      ipc = NEXTOP(ipc,e);
      break;
    default:
      sp = kill_unsafe_block(sp, op, ap);
      ipc = pop_path(&sp, cls, ap);
    }
  }
}


void
Yap_AddClauseToIndex(PredEntry *ap, yamop *beg, int first) {
  ClauseDef cl;
  /* first clause */
  path_stack_entry *stack, *sp;
  int cb;
  struct intermediates cint;

  if (!(ap->PredFlags & LogUpdatePredFlag)) {
    if (ap->PredFlags & IndexedPredFlag)
      Yap_RemoveIndexation(ap);
    return;
  }
  cint.CurrentPred = ap;
  if ((cb = setjmp(cint.CompilerBotch)) == 3) {
    restore_machine_regs();
    Yap_gcl(Yap_Error_Size, ap->ArityOfPE, ENV, CP);
  } else if (cb == 2) {
    restore_machine_regs();
    if (!Yap_growheap(FALSE, Yap_Error_Size, NULL)) {
      save_machine_regs();
      if (ap->PredFlags & LogUpdatePredFlag) {
	Yap_kill_iblock((ClauseUnion *)ClauseCodeToLogUpdIndex(ap->cs.p_code.TrueCodeOfPred),NULL, ap);
      } else {
	StaticIndex *cl;

	cl = ClauseCodeToStaticIndex(ap->cs.p_code.TrueCodeOfPred);
	Yap_kill_iblock((ClauseUnion *)cl, NULL, ap);
      }
      return;
    }
  } else if (cb == 4) {
    restore_machine_regs();
    if (!Yap_growtrail(Yap_Error_Size)) {
      save_machine_regs();
      if (ap->PredFlags & LogUpdatePredFlag) {
	Yap_kill_iblock((ClauseUnion *)ClauseCodeToLogUpdIndex(ap->cs.p_code.TrueCodeOfPred),NULL, ap);
      } else {
	StaticIndex *cl;

	cl = ClauseCodeToStaticIndex(ap->cs.p_code.TrueCodeOfPred);
	Yap_kill_iblock((ClauseUnion *)cl, NULL, ap);
      }
      return;
    }
    Yap_Error_Size = 0;
  }
  Yap_ErrorMessage = NULL;
#ifdef DEBUG
  if (Yap_Option['i' - 'a' + 1]) {
    Term tmod = ap->ModuleOfPred;
    if (!tmod) tmod = TermProlog;
    Yap_DebugPutc(Yap_c_error_stream,'+');
    Yap_DebugPutc(Yap_c_error_stream,'\t');
    Yap_plwrite(tmod, Yap_DebugPutc, 0);
    Yap_DebugPutc(Yap_c_error_stream,':');
    if (ap->ModuleOfPred == IDB_MODULE) {
      Term t = Deref(ARG1);
      if (IsAtomTerm(t)) {
	Yap_plwrite(t, Yap_DebugPutc, 0);
      } else if (IsIntegerTerm(t)) {
	Yap_plwrite(t, Yap_DebugPutc, 0);
      } else {
	Functor f = FunctorOfTerm(t);
	Atom At = NameOfFunctor(f);
	Yap_plwrite(MkAtomTerm(At), Yap_DebugPutc, 0);
	Yap_DebugPutc(Yap_c_error_stream,'/');
	Yap_plwrite(MkIntTerm(ArityOfFunctor(f)), Yap_DebugPutc, 0);
      }
    } else {
      if (ap->ArityOfPE == 0) {
	Atom At = (Atom)ap->FunctorOfPred;
	Yap_plwrite(MkAtomTerm(At), Yap_DebugPutc, 0);
      } else {
	Functor f = ap->FunctorOfPred;
	Atom At = NameOfFunctor(f);
	Yap_plwrite(MkAtomTerm(At), Yap_DebugPutc, 0);
	Yap_DebugPutc(Yap_c_error_stream,'/');
	Yap_plwrite(MkIntTerm(ArityOfFunctor(f)), Yap_DebugPutc, 0);
      }
    }
    Yap_DebugPutc(Yap_c_error_stream,'\n');
  }
#endif
  stack = (path_stack_entry *)TR;
  cl.Code =  cl.CurrentCode = beg;
  sp = push_path(stack, NULL, &cl);
  add_to_index(&cint, first, sp, &cl); 
}
		 

static void
contract_ftable(yamop *ipc, ClauseUnion *blk, PredEntry *ap, Functor f) {
  int n = ipc->u.sl.s;
  FuncSwiEntry *fep;

  if (n > MIN_HASH_ENTRIES) {
    fep = lookup_f_hash(f, ipc->u.sl.l, n);
  } else {
    fep = (FuncSwiEntry *)(ipc->u.sl.l);
    while (fep->Tag != f) fep++;
  }
  fep->Label = (CELL)FAILCODE;
}

static void
contract_ctable(yamop *ipc, ClauseUnion *blk, PredEntry *ap, Term at) {
  int n = ipc->u.sl.s;
  AtomSwiEntry *cep;

  if (n > MIN_HASH_ENTRIES) {
    cep = lookup_c_hash(at, ipc->u.sl.l, n);
  } else {
    cep = (AtomSwiEntry *)(ipc->u.sl.l);
    while (cep->Tag != at) cep++;
  }
  cep->Label = (CELL)FAILCODE;
}

static void
remove_from_index(PredEntry *ap, path_stack_entry *sp, ClauseDef *cls, yamop *bg, yamop *lt) {
  /* last clause to experiment with */
  yamop *ipc = ap->cs.p_code.TrueCodeOfPred;
  UInt current_arity = 0;

  sp = init_block_stack(sp, ipc, ap);
  if (ap->cs.p_code.NOfClauses == 1) {
    if (ap->PredFlags & IndexedPredFlag) {
      Yap_RemoveIndexation(ap);
      return;
    }
    ap->cs.p_code.TrueCodeOfPred = ap->cs.p_code.FirstClause;
    if (ap->PredFlags & SpiedPredFlag) {
      ap->OpcodeOfPred = Yap_opcode(_spy_pred);
      ap->CodeOfPred = (yamop *)(&(ap->OpcodeOfPred)); 
    } else {
      ap->OpcodeOfPred = ap->cs.p_code.FirstClause->opc;
      ap->CodeOfPred = ap->cs.p_code.TrueCodeOfPred;
    }
    return;
  }
  /* try to refine the interval using the indexing code */
  while (ipc != NULL) {
    op_numbers op = Yap_op_from_opcode(ipc->opc);

    switch(op) {
    case _retry_profiled:
    case _count_retry:
      ipc = NEXTOP(ipc, p);
      break;
    case _try_in:
      /* I cannot expand a predicate that starts on a variable,
         have to expand the index.
      */
      if (IN_BETWEEN(bg,ipc->u.l.l,lt)) {
	sp = kill_clause(ipc, bg, lt, sp, ap);
	ipc = pop_path(&sp, cls, ap);
      } else {
	/* just go to next instruction */
	ipc = NEXTOP(ipc,l);
      }
      break;
    case _try_clause:
    case _retry:
      /* I cannot expand a predicate that starts on a variable,
         have to expand the index.
      */
      if (IN_BETWEEN(bg,ipc->u.ld.d,lt)) {
	sp = kill_clause(ipc, bg, lt, sp, ap);
	ipc = pop_path(&sp, cls, ap);
      } else {
	/* just go to next instruction */
	ipc = NEXTOP(ipc,ld);
      }
      break;
    case _trust_logical_pred:
      ipc = NEXTOP(ipc,l);
      break;
    case _trust:
      if (IN_BETWEEN(bg,ipc->u.ld.d,lt)) {
	sp = kill_clause(ipc, bg, lt, sp, ap);
      }
      ipc = pop_path(&sp, cls, ap);
      break;
    case _stale_lu_index:
    case _enter_lu_pred:
      sp = kill_clause(ipc, bg, lt, sp, ap);
      ipc = pop_path(&sp, cls, ap);
      break;
      /* instructions type l */
    case _try_me:
    case _try_me1:
    case _try_me2:
    case _try_me3:
    case _try_me4:
    case _retry_me:
    case _retry_me1:
    case _retry_me2:
    case _retry_me3:
    case _retry_me4:
      sp = push_path(sp, &(ipc->u.ld.d), cls);
      ipc = NEXTOP(ipc,ld);
      break;
    case _profiled_trust_me:
    case _trust_me:
    case _count_trust_me:
    case _trust_me1:
    case _trust_me2:
    case _trust_me3:
    case _trust_me4:
      ipc = NEXTOP(ipc,ld);
      break;
    case _jump:
      sp = cross_block(sp, &ipc->u.l.l, ap);
      /* just skip for now, but should worry about memory management */
      ipc = ipc->u.l.l;
      break;
    case _jump_if_var:
      sp = push_path(sp, &(ipc->u.l.l), cls);
      ipc = NEXTOP(ipc,l);
      break;
    case _jump_if_nonvar:
      sp = push_path(sp, &(ipc->u.xl.l), cls);
      ipc = NEXTOP(ipc,xl);
      break;
      /* instructions type e */
    case _switch_on_type:
      sp = push_path(sp, &(ipc->u.llll.l4), cls);
      if (ap->PredFlags & LogUpdatePredFlag) {
	add_head_info(cls, 1);
      } else {
	add_info(cls, 1);
      }
      if (IsPairTerm(cls->Tag)) {
	yamop *nipc = ipc->u.llll.l1;
	current_arity = 2;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.llll.l1 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* go on */
	  sp = cross_block(sp, &ipc->u.llll.l1, ap);
	  ipc = nipc;	
	}
      } else if (IsAtomOrIntTerm(cls->Tag)) {
	yamop *nipc = ipc->u.llll.l2;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.llll.l2 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else if (IsApplTerm(cls->Tag)) {
	yamop *nipc = ipc->u.llll.l3;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.llll.l3 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else {
	/* we can't separate into four groups,
	   need to restart.
	*/
	sp = kill_block(sp, ap);
	ipc = pop_path(&sp, cls, ap);
      }
      break;
    case _switch_list_nl:
      sp = push_path(sp, &(ipc->u.ollll.l4), cls);
      if (ap->PredFlags & LogUpdatePredFlag) {
	add_head_info(cls, 1);
      } else {
	add_info(cls, 1);
      }
      if (IsPairTerm(cls->Tag)) {
	yamop *nipc = ipc->u.ollll.l1;
	current_arity = 2;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.ollll.l1 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* go on */
	  sp = cross_block(sp, &ipc->u.ollll.l1, ap);
	  ipc = nipc;	
	}
      } else if (IsAtomOrIntTerm(cls->Tag)) {
	yamop *nipc = ipc->u.ollll.l2;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.ollll.l2 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else if (IsApplTerm(cls->Tag)) {
	yamop *nipc = ipc->u.ollll.l3;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.ollll.l3 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else {
	/* we can't separate into four groups,
	   need to restart.
	*/
	sp = kill_block(sp, ap);
	ipc = pop_path(&sp, cls, ap);
      }
      break;
    case _switch_on_arg_type:
      sp = push_path(sp, &(ipc->u.xllll.l4), cls);
      current_arity = 2;
      if (ap->PredFlags & LogUpdatePredFlag) {
	add_head_info(cls, Yap_regtoregno(ipc->u.xllll.x));
      } else {
	add_info(cls, Yap_regtoregno(ipc->u.xllll.x));
      }
      if (IsPairTerm(cls->Tag)) {
	yamop *nipc = ipc->u.xllll.l1;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.xllll.l1 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* go on */
	  sp = cross_block(sp, &ipc->u.xllll.l1, ap);
	  ipc = nipc;	
	}
      } else if (IsAtomOrIntTerm(cls->Tag)) {
	yamop *nipc = ipc->u.xllll.l2;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.xllll.l2 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else if (IsApplTerm(cls->Tag)) {
	yamop *nipc = ipc->u.xllll.l3;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.xllll.l3 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else {
	/* we can't separate into four groups,
	   need to restart.
	*/
	sp = kill_block(sp, ap);
	ipc = pop_path(&sp, cls, ap);
      }
      break;
    case _switch_on_sub_arg_type:
      sp = push_path(sp, &(ipc->u.sllll.l4), cls);
      current_arity = 2;
      add_arg_info(cls, ap, ipc->u.sllll.s+1);
      if (IsPairTerm(cls->Tag)) {
	yamop *nipc = ipc->u.sllll.l1;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.sllll.l1 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* go on */
	  sp = cross_block(sp, &ipc->u.sllll.l1, ap);
	  ipc = nipc;	
	}
      } else if (IsAtomOrIntTerm(cls->Tag)) {
	yamop *nipc = ipc->u.sllll.l2;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.sllll.l2 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else if (IsApplTerm(cls->Tag)) {
	yamop *nipc = ipc->u.sllll.l3;
	if (IN_BETWEEN(bg,nipc,lt)) {
	  /* jump straight to clause */
	  ipc->u.sllll.l3 = FAILCODE;
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  /* I do not have to worry about crossing a block here */
	  ipc = nipc;	
	}
      } else {
	/* we can't separate into four groups,
	   need to restart.
	*/
	sp = kill_block(sp, ap);
	ipc = pop_path(&sp, cls, ap);
      }
      break;
    case _if_not_then:
      ipc = pop_path(&sp, cls, ap);
      break;
      /* instructions type ollll */
    case _switch_on_func:
    case _if_func:
    case _go_on_func:
      {
	FuncSwiEntry *fe;
	yamop *newpc;
	Functor f = (Functor)RepAppl(cls->Tag);
	
	if (op == _switch_on_func) {
	  fe = lookup_f_hash(f, ipc->u.sl.l, ipc->u.sl.s);
	} else {
	  fe = lookup_f(f, ipc->u.sl.l, ipc->u.sl.s);
	}
	if (!IsExtensionFunctor(f)) {
	  current_arity = ArityOfFunctor(f);
	}
	newpc = (yamop *)(fe->Label);

	if (newpc == (yamop *)&(ap->cs.p_code.ExpandCode)) {
	  /* we found it */
	  ipc = pop_path(&sp, cls, ap);
	} else if (newpc == FAILCODE) {
	  ipc = pop_path(&sp, cls, ap);
	} else if (IN_BETWEEN(bg,fe->Label,lt)) {
	  /* oops, nothing there */
	  contract_ftable(ipc, current_block(sp), ap, f);
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  yamop *newpc = (yamop *)(fe->Label);
	  sp = fetch_new_block(sp, &(ipc->u.sl.l), ap);
	  sp = cross_block(sp, (yamop **)&(fe->Label), ap);
	  ipc = newpc;
	}
      }
      break;
    case _index_dbref:
      cls->Tag = cls->u.t_ptr;
      ipc = NEXTOP(ipc,e);
      break;
    case _index_blob:
      cls->Tag = MkIntTerm(RepAppl(cls->u.t_ptr)[1]);
      ipc = NEXTOP(ipc,e);
      break;
    case _switch_on_cons:
    case _if_cons:
    case _go_on_cons:
      {
	AtomSwiEntry *ae;
	yamop *newpc;
	Term at = cls->Tag;
	
	if (op == _switch_on_cons) {
	  ae = lookup_c_hash(at,ipc->u.sl.l,ipc->u.sl.s);
	} else {
	  ae = lookup_c(at, ipc->u.sl.l, ipc->u.sl.s);
	}
	newpc = (yamop *)(ae->Label);

	if (newpc == (yamop *)&(ap->cs.p_code.ExpandCode)) {
	  /* we found it */
	  ipc = pop_path(&sp, cls, ap);
	} else if (newpc == FAILCODE) {
	  ipc = pop_path(&sp, cls, ap);
	} else if (IN_BETWEEN(bg,ae->Label,lt)) {
	  /* oops, nothing there */
	  contract_ctable(ipc, current_block(sp), ap, at);
	  ipc = pop_path(&sp, cls, ap);
	} else {
	  yamop *newpc = (yamop *)(ae->Label);

	  sp = fetch_new_block(sp, &(ipc->u.sl.l), ap);
	  sp = cross_block(sp, (yamop **)&(ae->Label), ap);
	  ipc = newpc;
	}
      }
      break;
    case _expand_index:
      ipc = pop_path(&sp, cls, ap);
      break;
    case _lock_lu:
      ipc = NEXTOP(ipc,p);
      break;
    case _unlock_lu:
      ipc = NEXTOP(ipc,e);
      break;
    default:
      if (IN_BETWEEN(bg,ipc,lt)) {
	sp = kill_unsafe_block(sp, op, ap);
      }
      ipc = pop_path(&sp, cls, ap);
    }
  }
}


/* clause is locked */
void
Yap_RemoveClauseFromIndex(PredEntry *ap, yamop *beg) {
  ClauseDef cl;
  /* first clause */
  path_stack_entry *stack, *sp;
  int cb;
  yamop *last;
  struct intermediates cint; 
 

  if ((cb = setjmp(cint.CompilerBotch)) == 3) {
    restore_machine_regs();
    Yap_gcl(Yap_Error_Size, ap->ArityOfPE, ENV, CP);
  } else if (cb == 2) {
    restore_machine_regs();
    if (!Yap_growheap(FALSE, Yap_Error_Size, NULL)) {
      save_machine_regs();
      if (ap->PredFlags & LogUpdatePredFlag) {
	Yap_kill_iblock((ClauseUnion *)ClauseCodeToLogUpdIndex(ap->cs.p_code.TrueCodeOfPred),NULL, ap);
      } else {
	StaticIndex *cl;

	cl = ClauseCodeToStaticIndex(ap->cs.p_code.TrueCodeOfPred);
	Yap_kill_iblock((ClauseUnion *)cl, NULL, ap);
      }
      return;
    }
    Yap_Error_Size = 0;
  } else if (cb == 4) {
    restore_machine_regs();
    if (!Yap_growtrail(Yap_Error_Size)) {
      save_machine_regs();
      if (ap->PredFlags & LogUpdatePredFlag) {
	Yap_kill_iblock((ClauseUnion *)ClauseCodeToLogUpdIndex(ap->cs.p_code.TrueCodeOfPred),NULL, ap);
      } else {
	StaticIndex *cl;

	cl = ClauseCodeToStaticIndex(ap->cs.p_code.TrueCodeOfPred);
	Yap_kill_iblock((ClauseUnion *)cl, NULL, ap);
      }
      return;
    }
    Yap_Error_Size = 0;
  }
  Yap_ErrorMessage = NULL;
#ifdef DEBUG
  if (Yap_Option['i' - 'a' + 1]) {
    Term tmod = ap->ModuleOfPred;

    if (!tmod) tmod = TermProlog;
    Yap_DebugPutc(Yap_c_error_stream,'-');
    Yap_DebugPutc(Yap_c_error_stream,'\t');
    Yap_plwrite(tmod, Yap_DebugPutc, 0);
    Yap_DebugPutc(Yap_c_error_stream,':');
    if (ap->ModuleOfPred != IDB_MODULE) {
      if (ap->ArityOfPE == 0) {
	Atom At = (Atom)ap->FunctorOfPred;
	Yap_plwrite(MkAtomTerm(At), Yap_DebugPutc, 0);
      } else {
	Functor f = ap->FunctorOfPred;
	Atom At = NameOfFunctor(f);
	Yap_plwrite(MkAtomTerm(At), Yap_DebugPutc, 0);
	Yap_DebugPutc(Yap_c_error_stream,'/');
	Yap_plwrite(MkIntTerm(ArityOfFunctor(f)), Yap_DebugPutc, 0);
      }
    } else {
      if (ap->PredFlags & NumberDBPredFlag) {
	Int id = ap->src.IndxId;
	Yap_plwrite(MkIntegerTerm(id), Yap_DebugPutc, 0);
      } else if (ap->PredFlags & AtomDBPredFlag) {
	Atom At = (Atom)ap->FunctorOfPred;
	Yap_plwrite(MkAtomTerm(At), Yap_DebugPutc, 0);
      } else {
	Functor f = ap->FunctorOfPred;
	Atom At = NameOfFunctor(f);
	Yap_plwrite(MkAtomTerm(At), Yap_DebugPutc, 0);
	Yap_DebugPutc(Yap_c_error_stream,'/');
	Yap_plwrite(MkIntTerm(ArityOfFunctor(f)), Yap_DebugPutc, 0);
      }
    }
    Yap_DebugPutc(Yap_c_error_stream,'\n');
  }
#endif
  stack = (path_stack_entry *)TR;
  if (ap->PredFlags & LogUpdatePredFlag) {
    LogUpdClause *c = ClauseCodeToLogUpdClause(beg);
    cl.Code =  cl.CurrentCode = beg;
    last = (yamop *)((CODEADDR)c+c->ClSize);
  } else {
    StaticClause *c = ClauseCodeToStaticClause(beg);
    cl.Code =  cl.CurrentCode = beg;
    last = (yamop *)((CODEADDR)c+c->ClSize);
  }
  sp = push_path(stack, NULL, &cl);
  if (ap->cs.p_code.NOfClauses == 0) {
    /* there was no indexing code */
    ap->CodeOfPred = ap->cs.p_code.TrueCodeOfPred = FAILCODE;
    ap->OpcodeOfPred = Yap_opcode(_op_fail);
  } else {
    remove_from_index(ap, sp, &cl, beg, last); 
  }
}
	     

static LogUpdClause *
lu_clause(yamop *ipc)
{
  LogUpdClause *c;
  CELL *p = (CELL *)ipc;

  if (ipc == FAILCODE)
    return NULL;
  while ((c = ClauseCodeToLogUpdClause(p))->Id != FunctorDBRef ||
	 !(c->ClFlags & LogUpdMask) ||
	 (c->ClFlags & (IndexMask|DynamicMask|SwitchTableMask|SwitchRootMask))) {
    p--;
  }
  return c;
}

static LogUpdClause *
static_clause(yamop *ipc)
{
  StaticClause *c;
  CELL *p = (CELL *)ipc;

  if (ipc == FAILCODE)
    return NULL;
  while ((c = ClauseCodeToStaticClause(p))->Id != FunctorDBRef ||
	 (c->ClFlags & (LogUpdMask|IndexMask|DynamicMask|SwitchTableMask|SwitchRootMask))) {
    p--;
  }
  return (LogUpdClause *)c;
}

static void
store_clause_choice_point(Term t1, Term tb, Term tr, yamop *ipc, PredEntry *pe, yamop *ap_pc, yamop *cp_pc)
{
  Term tpc = MkIntegerTerm((Int)ipc);
  Term tpe = MkIntegerTerm((Int)pe);
  CELL *tsp = ASP-5;
  choiceptr bptr = ((choiceptr)tsp)-1;

  tsp[0] = tpe;
  tsp[1] = tpc;
  tsp[2] = t1;
  tsp[3] = tb;
  tsp[4] = tr;
  bptr->cp_tr = TR;
  HB = bptr->cp_h = H;
#ifdef DEPTH_LIMIT
  bptr->cp_depth = DEPTH;
#endif
  bptr->cp_b = B;
  bptr->cp_cp = cp_pc;
  bptr->cp_ap = ap_pc;
  bptr->cp_env = ENV;
  /* now, install the new YREG =*/
  ASP = (CELL *)bptr;
  ASP[E_CB] = (CELL)bptr;
  B = bptr;
#ifdef YAPOR
  SCH_set_load(B);
#endif	/* YAPOR */
  SET_BB(bptr);
}

static void
update_clause_choice_point(yamop *ipc, yamop *ap_pc)
{
  Term tpc = MkIntegerTerm((Int)ipc);
  B->cp_args[1] = tpc;
  B->cp_h = H;
  B->cp_ap = ap_pc;
}

static LogUpdClause *
to_clause(yamop *ipc, PredEntry *ap)
{
  if (ap->PredFlags & LogUpdatePredFlag)
    return lu_clause(ipc);
  else
    return static_clause(ipc);
}

LogUpdClause *
Yap_FollowIndexingCode(PredEntry *ap, yamop *ipc, Term t1, Term tb, Term tr, yamop *ap_pc, yamop *cp_pc)
{
  CELL *s_reg = NULL;
  Term t = TermNil;
  yamop *start_pc = ipc;
  choiceptr b0 = NULL;
  yamop **jlbl = NULL;
  int lu_pred = ap->PredFlags & LogUpdatePredFlag;

  if (ap->ModuleOfPred != IDB_MODULE) {
    if (ap->ArityOfPE) {
      CELL *tar = RepAppl(t1);
      UInt i;

      for (i = 1; i <= ap->ArityOfPE; i++) {
	XREGS[i] = tar[i];
      }
    }
  }
  /* try to refine the interval using the indexing code */
  while (ipc != NULL) {
    op_numbers op = Yap_op_from_opcode(ipc->opc);

    switch(op) {
    case _try_in:
      update_clause_choice_point(NEXTOP(ipc,l), ap_pc);
      if (lu_pred)
	return lu_clause(ipc->u.l.l);
      else
	return static_clause(ipc->u.l.l);
      break;
    case _try_clause:
      if (b0 == NULL)
	store_clause_choice_point(t1, tb, tr, NEXTOP(ipc,ld), ap, ap_pc, cp_pc);
      else
	update_clause_choice_point(NEXTOP(ipc,ld), ap_pc);
      if (lu_pred)
	return lu_clause(ipc->u.ld.d);
      else
	return static_clause(ipc->u.ld.d);
    case _try_me:
    case _try_me1:
    case _try_me2:
    case _try_me3:
    case _try_me4:
      if (b0 == NULL)
	store_clause_choice_point(t1, tb, tr, ipc->u.ld.d, ap, ap_pc, cp_pc);
      else
	update_clause_choice_point(ipc->u.ld.d, ap_pc);
      ipc = NEXTOP(ipc,ld);
      break;
    case _retry_profiled:
    case _count_retry:
      ipc = NEXTOP(ipc,p);
      break;
    case _retry:
      update_clause_choice_point(NEXTOP(ipc,ld),ap_pc);
      if (lu_pred)
	return lu_clause(ipc->u.ld.d);
      else
	return static_clause(ipc->u.ld.d);
    case _retry_me:
    case _retry_me1:
    case _retry_me2:
    case _retry_me3:
    case _retry_me4:
      update_clause_choice_point(ipc->u.ld.d,ap_pc);
      ipc = NEXTOP(ipc,ld);
      break;
    case _trust:
#ifdef YAPOR
      CUT_prune_to(B->cp_b);
#else
      B = B->cp_b;
#endif /* YAPOR */
#ifdef TABLING
      abolish_incomplete_subgoals(B);
#endif /* TABLING */
      b0 = B;
      if (lu_pred)
	return lu_clause(ipc->u.ld.d);
      else
	return static_clause(ipc->u.ld.d);
    case _profiled_trust_me:
    case _trust_me:
    case _count_trust_me:
    case _trust_me1:
    case _trust_me2:
    case _trust_me3:
    case _trust_me4:
      b0 = B;
      ipc = NEXTOP(ipc,ld);
      break;
    case _trust_logical_pred:
      {
	LogUpdIndex *cl = (LogUpdIndex *)ipc->u.l.l;
	/* check if we are the ones using this code */
#if defined(YAPOR) || defined(THREADS)
	LOCK(cl->ClLock);
	DEC_CLREF_COUNT(cl);
	/* clear the entry from the trail */
	TR = --(B->cp_tr);
	/* actually get rid of the code */
	if (cl->ClRefCount == 0 && cl->ClFlags & ErasedMask) {
	  UNLOCK(cl->ClLock);
	  /* I am the last one using this clause, hence I don't need a lock
	     to dispose of it 
	  */
	  Yap_ErLogUpdIndex(cl);
	} else {
	  UNLOCK(cl->ClLock);
	}
#else
	if (TrailTerm(B->cp_tr-1) == CLREF_TO_TRENTRY(cl) &&
	    B->cp_tr > B->cp_b->cp_tr) {
	  cl->ClFlags &= ~InUseMask;
	  /* clear the entry from the trail */
	  TR = --(B->cp_tr);
	  /* next, recover space for the indexing code if it was erased */
	  if (cl->ClFlags & ErasedMask) {
	    Yap_ErLogUpdIndex(cl);
	  }
	}
#endif
      }
      ipc = NEXTOP(ipc,l);
      break;
    case _stale_lu_index:
#if defined(YAPOR) || defined(THREADS)
      LOCK(ap->PELock);
      if (*jlbl != ipc) {
	ipc = *jlbl;
	UNLOCK(ap->PELock);
	break;
      }
#endif
      while (TRUE) {
	yamop *nipc = clean_up_index(ipc->u.Ill.I, jlbl, ap);
	if (nipc == NULL) {
	  /* not enough space */
	  H[0] = t1;
	  H[1] = tb;
	  H[2] = tr;
	  H += 3;
	  if (!Yap_growheap(FALSE, Yap_Error_Size, NULL)) {
	    UNLOCK(ap->PELock);
	    Yap_Error(OUT_OF_HEAP_ERROR, TermNil, Yap_ErrorMessage);
	    return NULL;
	  }
	  H -= 3;
	  t1 = H[0];
	  tb = H[1];
	  tr = H[2];
	} else {
	  UNLOCK(ap->PELock);
	  ipc = nipc;
	  break;
	}
      }
      break;
    case _enter_lu_pred:
      {
	LogUpdIndex *cl = ipc->u.Ill.I;
	LOCK(cl->ClLock);
	/* indicate the indexing code is being used */
#if defined(YAPOR) || defined(THREADS)
	/* just store a reference */
	INC_CLREF_COUNT(cl);
	TRAIL_CLREF(cl);
#else
	if (!(cl->ClFlags & InUseMask)) {
	  cl->ClFlags |= InUseMask;
	  TRAIL_CLREF(cl);
	}	
#endif
	UNLOCK(cl->ClLock);
      }
      ipc = ipc->u.Ill.l1;
      break;
    case _jump:
      ipc = ipc->u.l.l;
      break;
    case _jump_if_var:
      {
	Term t = Deref(ARG1);
	if (IsVarTerm(t)) {
	  jlbl = &(ipc->u.l.l);
	  ipc = ipc->u.l.l;
	} else {
	  ipc = NEXTOP(ipc,l);
	}
      }
      break;
    case _jump_if_nonvar:
      {
	Term t = Deref(XREGS[arg_from_x(ipc->u.xllll.x)]);
	if (!IsVarTerm(t)) {
	  jlbl = &(ipc->u.xl.l);
	  ipc = ipc->u.xl.l;
	} else {
	  ipc = NEXTOP(ipc,xl);
	}
      }
      break;
      /* instructions type e */
    case _switch_on_type:
      t = Deref(ARG1);
      if (IsVarTerm(t)) {
	jlbl = &(ipc->u.llll.l4);
	ipc = ipc->u.llll.l4;
      } else if (IsPairTerm(t)) {
	jlbl = &(ipc->u.llll.l1);
	ipc = ipc->u.llll.l1;
	s_reg = RepPair(t);
      } else if (IsAtomOrIntTerm(t)) {
	jlbl = &(ipc->u.llll.l2);
	ipc = ipc->u.llll.l2;
      } else {
	jlbl = &(ipc->u.llll.l3);
	ipc = ipc->u.llll.l3;
      }
      break;
    case _switch_list_nl:
      t = Deref(ARG1);
      if (IsVarTerm(t)) {
	jlbl = &(ipc->u.ollll.l4);
	ipc = ipc->u.ollll.l4;
      } else if (IsPairTerm(t)) {
	jlbl = &(ipc->u.ollll.l1);
	ipc = ipc->u.ollll.l1;
	s_reg = RepPair(t);
      } else if (IsAtomOrIntTerm(t)) {
	jlbl = &(ipc->u.ollll.l2);
	ipc = ipc->u.ollll.l2;
      } else {
	jlbl = &(ipc->u.ollll.l3);
	ipc = ipc->u.ollll.l3;
      }
      break;
    case _switch_on_arg_type:
      t = Deref(XREGS[arg_from_x(ipc->u.xllll.x)]);
      if (IsVarTerm(t)) {
	jlbl = &(ipc->u.xllll.l4);
	ipc = ipc->u.xllll.l4;
      } else if (IsPairTerm(t)) {
	jlbl = &(ipc->u.xllll.l1);
	ipc = ipc->u.xllll.l1;
	s_reg = RepPair(t);
      } else if (IsAtomOrIntTerm(t)) {
	jlbl = &(ipc->u.xllll.l1);
	ipc = ipc->u.xllll.l2;
      } else {
	jlbl = &(ipc->u.xllll.l3);
	ipc = ipc->u.xllll.l3;
      }
      break;
    case _switch_on_sub_arg_type:
      t = Deref(s_reg[ipc->u.sllll.s]);
      if (IsVarTerm(t)) {
	jlbl = &(ipc->u.sllll.l4);
	ipc = ipc->u.sllll.l4;
      } else if (IsPairTerm(t)) {
	jlbl = &(ipc->u.sllll.l1);
	ipc = ipc->u.sllll.l1;
	s_reg = RepPair(t);
      } else if (IsAtomOrIntTerm(t)) {
	jlbl = &(ipc->u.sllll.l2);
	ipc = ipc->u.sllll.l2;
      } else {
	jlbl = &(ipc->u.sllll.l3);
	ipc = ipc->u.sllll.l3;
      }
      break;
    case _if_not_then:
      t = Deref(ARG1);
      if (IsVarTerm(t)) {
	jlbl = &(ipc->u.clll.l3);
	ipc = ipc->u.clll.l3;
      } else if (!IsVarTerm(t) && t != ipc->u.clll.c) {
	jlbl = &(ipc->u.clll.l2);
	ipc = ipc->u.clll.l2;
      } else {
	jlbl = &(ipc->u.clll.l1);
	ipc = ipc->u.clll.l1;
      }
      break;
      /* instructions type ollll */
    case _switch_on_func:
    case _if_func:
    case _go_on_func:
      {
	FuncSwiEntry *fe;
	Functor f;
	
	s_reg = RepAppl(t);
	f = (Functor)s_reg[0];
	s_reg++;
	if (op == _switch_on_func) {
	  fe = lookup_f_hash(f, ipc->u.sl.l, ipc->u.sl.s);
	} else {
	  fe = lookup_f(f, ipc->u.sl.l, ipc->u.sl.s);
	}
	jlbl = (yamop **)(&fe->Label);
	ipc = (yamop *)(fe->Label);
      }
      break;
    case _index_dbref:
      t = AbsAppl(s_reg-1);
      ipc = NEXTOP(ipc,e);
      break;
    case _index_blob:
      t = MkIntTerm(s_reg[0]);
      ipc = NEXTOP(ipc,e);
      break;
    case _switch_on_cons:
    case _if_cons:
    case _go_on_cons:
      {
	AtomSwiEntry *ae;
	
	if (op == _switch_on_cons) {
	  ae = lookup_c_hash(t, ipc->u.sl.l, ipc->u.sl.s);
	} else {
	  ae = lookup_c(t, ipc->u.sl.l, ipc->u.sl.s);
	}
	jlbl = (yamop **)(&ae->Label);
	ipc = (yamop *)(ae->Label);
      }
      break;
    case _expand_index:
#if defined(YAPOR) || defined(THREADS)
      LOCK(ap->PELock);
      if (*jlbl != ipc) {
	ipc = *jlbl;
	UNLOCK(ap->PELock);
	break;
      }
#endif
      ipc = ExpandIndex(ap);
      UNLOCK(ap->PELock);
      break;
    case _op_fail:
      /*
	ipc = (yamop *)IntegerOfTerm(B->cp_args[1]);
	break;
      */
    case _undef_p:
      return NULL;
    case _lock_lu:
      ipc = NEXTOP(ipc,p);
      break;
    case _unlock_lu:
      ipc = NEXTOP(ipc,e);
      break;
#if THREADS
    case _thread_local:
      ap = Yap_GetThreadPred(ap);
      ipc = ap->CodeOfPred;
      break;
#endif
    case _index_pred:
    case _spy_pred:
      Yap_IPred(ap);
      start_pc = ipc = ap->cs.p_code.TrueCodeOfPred;
      break;
    default:
      if (b0) {
#ifdef YAPOR
	CUT_prune_to(B->cp_b);
#else
	B = B->cp_b;
#endif /* YAPOR */
#ifdef TABLING
	abolish_incomplete_subgoals(B);
#endif /* TABLING */
	/* I did a trust */
      }
      if (lu_pred)
	return lu_clause(ipc);
      else
	return static_clause(ipc);
    }
  }
  if (b0) {
    /* I did a trust */
#ifdef YAPOR
    CUT_prune_to(B->cp_b);
#else
    B = B->cp_b;
#endif /* YAPOR */
#ifdef TABLING
    abolish_incomplete_subgoals(B);
#endif /* TABLING */
  }
  return NULL;
}

LogUpdClause *
Yap_NthClause(PredEntry *ap, Int ncls)
{
  yamop
    *ipc = ap->cs.p_code.TrueCodeOfPred,
    *alt = NULL;
  yamop **jlbl = NULL;

  /* search every clause */
  if (ncls == 1)
    return to_clause(ap->cs.p_code.FirstClause,ap);
  else if (ncls == ap->cs.p_code.NOfClauses)
    return to_clause(ap->cs.p_code.LastClause,ap);
  else if (ncls > ap->cs.p_code.NOfClauses)
    return NULL;
  else if (ncls < 0)
    return NULL;
  
  if (ap->ModuleOfPred != IDB_MODULE) {
    if (ap->ArityOfPE) {
      UInt i;

      for (i = 1; i <= ap->ArityOfPE; i++) {
	XREGS[i] = MkVarTerm();
      }
    }
  } else {
    ARG2 = MkVarTerm();
  }
  while (TRUE) {
    op_numbers op = Yap_op_from_opcode(ipc->opc);

    switch(op) {
    case _try_in:
      if (ncls == 1)
	return to_clause(ipc->u.l.l, ap);
      ncls--;
      ipc = NEXTOP(ipc,l);
      break;
    case _retry_profiled:
    case _count_retry:
      ipc = NEXTOP(ipc,p);
    case _try_clause:
    case _retry:
      if (ncls == 1)
	return to_clause(ipc->u.ld.d, ap);
      else if (alt == NULL) {
	ncls -= 2;
	/* get there in a fell swoop */
	if (ap->PredFlags & ProfiledPredFlag) {
	  if (ap->PredFlags & CountPredFlag) {
	    ipc = (yamop *)((char *)ipc+ncls*(UInt)NEXTOP(NEXTOP(NEXTOP((yamop *)NULL,ld),p),p));
	  } else {
	    ipc = (yamop *)((char *)ipc+ncls*(UInt)NEXTOP(NEXTOP((yamop *)NULL,ld),p));
	  }
	} else if (ap->PredFlags & CountPredFlag) {
	  ipc = (yamop *)((char *)ipc+ncls*(UInt)NEXTOP(NEXTOP((yamop *)NULL,ld),p));
	} else {
	  ipc = (yamop *)((char *)ipc+ncls*(UInt)NEXTOP((yamop *)NULL,ld));
	}
	ncls = 1;
      } else {
	ncls--;
      }
      ipc = NEXTOP(ipc,ld);
      break;
    case _trust:
      if (ncls == 1)
	return to_clause(ipc->u.l.l,ap);
      ncls--;
      ipc = alt;
      break;
    case _try_me:
    case _try_me1:
    case _try_me2:
    case _try_me3:
    case _try_me4:
    case _retry_me:
    case _retry_me1:
    case _retry_me2:
    case _retry_me3:
    case _retry_me4:
      alt = ipc->u.ld.d;
      ipc = NEXTOP(ipc,ld);
      break;
    case _profiled_trust_me:
    case _trust_me:
    case _count_trust_me:
    case _trust_me1:
    case _trust_me2:
    case _trust_me3:
    case _trust_me4:
      alt = NULL;
      ipc = NEXTOP(ipc,ld);
      break;
    case _trust_logical_pred:
      ipc = NEXTOP(ipc,l);
    case _stale_lu_index:
#if defined(YAPOR) || defined(THREADS)
      LOCK(ap->PELock);
      if (*jlbl != ipc) {
	ipc = *jlbl;
	UNLOCK(ap->PELock);
	break;
      }
#endif
      while (TRUE) {
	yamop *nipc = clean_up_index(ipc->u.Ill.I, jlbl, ap);
	if (nipc == NULL) {
	  /* not enough space */
	  if (!Yap_growheap(FALSE, Yap_Error_Size, NULL)) {
	    UNLOCK(ap->PELock);
	    Yap_Error(OUT_OF_HEAP_ERROR, TermNil, Yap_ErrorMessage);
	    return NULL;
	  }
	} else {
	  UNLOCK(ap->PELock);
	  ipc = nipc;
	  break;
	}
      }
      break;
    case _enter_lu_pred:
      ipc = ipc->u.Ill.l1;
      break;
    case _lock_lu:
      ipc = NEXTOP(ipc,p);
      break;
    case _jump:
      jlbl = &(ipc->u.l.l);
      ipc = ipc->u.l.l;
      break;
    case _jump_if_var:
      jlbl = &(ipc->u.l.l);
      ipc = ipc->u.l.l;
      break;
    case _jump_if_nonvar:
      ipc = NEXTOP(ipc,xl);
      break;
      /* instructions type e */
    case _switch_on_type:
      jlbl = &(ipc->u.llll.l4);
      ipc = ipc->u.llll.l4;
      break;
    case _switch_list_nl:
      jlbl = &(ipc->u.ollll.l4);
      ipc = ipc->u.ollll.l4;
      break;
    case _switch_on_arg_type:
      jlbl = &(ipc->u.xllll.l4);
      ipc = ipc->u.xllll.l4;
      break;
    case _switch_on_sub_arg_type:
      jlbl = &(ipc->u.sllll.l4);
      ipc = ipc->u.sllll.l4;
      break;
    case _if_not_then:
      jlbl = &(ipc->u.clll.l3);
      ipc = ipc->u.clll.l3;
      break;
    case _expand_index:
#if defined(YAPOR) || defined(THREADS)
      LOCK(ap->PELock);
      if (*jlbl != (yamop *)&(ap->cs.p_code.ExpandCode)) {
	ipc = *jlbl;
	UNLOCK(ap->PELock);
	break;
      }
#endif
      ipc = ExpandIndex(ap);
      UNLOCK(ap->PELock);
      break;
    case _op_fail:
      ipc = alt;
      break;
    case _index_pred:
    case _spy_pred:
      Yap_IPred(ap);
      ipc = ap->cs.p_code.TrueCodeOfPred;
      break;
    case _undef_p:
    default:
      return NULL;
    }
  }
}

static yamop **
find_caller(PredEntry *ap, yamop *code) {
  /* first clause */
  yamop *alt = NULL;
  istack_entry *stack, *sp;
  /* last clause to experiment with */
  yamop *ipc = ap->cs.p_code.TrueCodeOfPred;
  /* labp should point at the beginning of the sequence */
  yamop **labp = NULL;
  Term t = TermNil, *s_reg = NULL;
  int is_last_arg = TRUE;
  int argno = 1;
  /* this is will be used as a new PC */
  CELL *top = (CELL *) TR;
  UInt arity = 0;
  sp = stack = (istack_entry *)top;

  labelno = 1;
  stack[0].pos = 0;

  /* try to refine the interval using the indexing code */
  while (ipc != NULL) {
    op_numbers op;

    op = Yap_op_from_opcode(ipc->opc);
    switch(op) {
    case _try_me:
    case _try_me1:
    case _try_me2:
    case _try_me3:
    case _try_me4:
    case _retry_me:
    case _retry_me1:
    case _retry_me2:
    case _retry_me3:
    case _retry_me4:
    case _trust_me:
    case _trust_me1:
    case _trust_me2:
    case _trust_me3:
    case _trust_me4:
    case _profiled_trust_me:
    case _count_trust_me:
      alt = ipc->u.ld.d;
      ipc = NEXTOP(ipc,ld);
      break;
    case _jump:
      ipc = ipc->u.l.l;
      break;
    case _jump_if_var:
      if (IsVarTerm(Deref(ARG1))) {
	ipc = ipc->u.l.l;
      } else {
	ipc = NEXTOP(ipc,l);
      }
      break;
    case _jump_if_nonvar:
      if (!IsVarTerm(XREGS[arg_from_x(ipc->u.xllll.x)])) {
	ipc = ipc->u.xl.l;
      } else {
	ipc = NEXTOP(ipc,xl);
      }
      break;
      /* instructions type EC */
      /* instructions type e */
    case _index_dbref:
      t = AbsAppl(s_reg-1);
      sp[-1].val = t;
      s_reg = NULL;
      ipc = NEXTOP(ipc,e);
      break;
    case _index_blob:
      t = MkIntTerm(s_reg[0]);
      sp[-1].val = t;
      s_reg = NULL;
      ipc = NEXTOP(ipc,e);
      break;
      /* instructions type e */
    case _switch_on_type:
      t = Deref(ARG1);
      argno = 1;
      sp = reset_stack(stack);
      if (IsVarTerm(t)) {
	if (ipc->u.llll.l4 == code) return &(ipc->u.llll.l4);
	ipc = ipc->u.llll.l4;
      } else if (IsPairTerm(t)) {
	sp = push_stack(sp, 1, AbsPair(NULL), TermNil);
	s_reg = RepPair(t);
	labp = &(ipc->u.llll.l1);
	if (ipc->u.llll.l1 == code) return &(ipc->u.llll.l1);
	ipc = ipc->u.llll.l1;	
      } else if (IsApplTerm(t)) {
	sp = push_stack(sp, 1, AbsAppl((CELL *)FunctorOfTerm(t)), TermNil);
	ipc = ipc->u.llll.l3;	
      } else {
	sp = push_stack(sp, 1, t, TermNil);
	ipc = ipc->u.llll.l2;	
      }
      break;
    case _switch_list_nl:
      t = Deref(ARG1);
      sp = reset_stack(stack);
      argno = 1;
      if (IsVarTerm(t)) {	
	if (ipc->u.ollll.l4 == code) return &(ipc->u.ollll.l4);
	ipc = ipc->u.ollll.l4;
      } else if (IsPairTerm(t)) {
	s_reg = RepPair(t);
	sp = push_stack(sp, 1, AbsPair(NULL), TermNil);
	if (ipc->u.ollll.l1 == code) 
	  return &(ipc->u.ollll.l1);
	ipc = ipc->u.ollll.l1;	
      } else if (IsApplTerm(t)) {
	sp = push_stack(sp, 1, AbsAppl((CELL *)FunctorOfTerm(t)), TermNil);
	ipc = ipc->u.ollll.l3;	
      } else {
	sp = push_stack(sp, 1, t, TermNil);
	ipc = ipc->u.ollll.l2;	
      }
      break;
    case _switch_on_arg_type:
      argno = arg_from_x(ipc->u.xllll.x);
      t = Deref(XREGS[argno]);
      if (IsVarTerm(t)) {
	if (ipc->u.xllll.l4 == code) return &(ipc->u.xllll.l4);
	ipc = ipc->u.xllll.l4;
      } else if (IsPairTerm(t)) {
	s_reg = RepPair(t);
	sp = push_stack(sp, argno, AbsPair(NULL), TermNil);
	if (ipc->u.xllll.l1 == code) return &(ipc->u.xllll.l1);
	ipc = ipc->u.xllll.l1;	
      } else if (IsApplTerm(t)) {
	sp = push_stack(sp, argno, AbsAppl((CELL *)FunctorOfTerm(t)), TermNil);
	ipc = ipc->u.xllll.l3;	
      } else {
	sp = push_stack(sp, argno, t, TermNil);
	ipc = ipc->u.xllll.l2;	
      }
      break;
    case _switch_on_sub_arg_type:
      {
	COUNT argno = ipc->u.sllll.s;

	t = Deref(s_reg[ipc->u.sllll.s]);
	if (argno != arity-1) is_last_arg = FALSE;
	t = Deref(s_reg[argno]);
	if (IsVarTerm(t)) {
	  if (ipc->u.sllll.l4 == code) return &(ipc->u.sllll.l4);
	  ipc = ipc->u.sllll.l4;
	} else if (IsPairTerm(t)) {
	  s_reg = RepPair(t);
	  sp = push_stack(sp, -argno-1, AbsPair(NULL), TermNil);
	  if (ipc->u.sllll.l1 == code) return &(ipc->u.sllll.l1);
	  ipc = ipc->u.sllll.l1;
	} else if (IsApplTerm(t)) {
	  sp = push_stack(sp, -argno-1, AbsAppl((CELL *)FunctorOfTerm(t)), TermNil);
	  ipc = ipc->u.sllll.l3;	
	} else {
	  sp = push_stack(sp, -argno-1, t, TermNil);
	  ipc = ipc->u.sllll.l2;	
	}
      }
      break;
    case _if_not_then:
      ipc = NULL;
      break;
      /* instructions type ollll */
    case _switch_on_func:
    case _if_func:
    case _go_on_func:
      {
	FuncSwiEntry *fe;
	yamop *newpc;
	Functor f;

	s_reg = RepAppl(t);
	f = (Functor)(*s_reg++);
	if (op == _switch_on_func) {
	  fe = lookup_f_hash(f,ipc->u.sl.l,ipc->u.sl.s);
	} else {
	  fe = lookup_f(f,ipc->u.sl.l,ipc->u.sl.s);
	}
	newpc = (yamop *)(fe->Label);

	if (newpc == code) {
	  /* we found it */
	  return (yamop **)(&(fe->Label));
	} else if (newpc == FAILCODE) {
	  ipc = alt;
	} else {
	  ipc = newpc;
	}
      }
      break;
    case _switch_on_cons:
    case _if_cons:
    case _go_on_cons:
      {
	AtomSwiEntry *ae;
	yamop *newpc;

	if (op == _switch_on_cons) {
	  ae = lookup_c_hash(t,ipc->u.sl.l,ipc->u.sl.s);
	} else {
	  ae = lookup_c(t,ipc->u.sl.l,ipc->u.sl.s);
	}

	newpc = (yamop *)(ae->Label);
	if (newpc == code) {
	  /* we found it */
	  return (yamop **)(&(ae->Label));
	  ipc = NULL;
	} else if (newpc == FAILCODE) {
	  /* oops, things went wrong */
	  ipc = alt;
	} else {
	  ipc = newpc;
	}
      }
      break;
    case _expand_index:
      ipc = alt;
      alt = NULL;
      break;
    case _op_fail:
      if (alt == NULL) {
	return NULL;
      } else {
	ipc = alt;
	alt = NULL;
      }
      break;
    case _stale_lu_index:
      /* found myself */
      return NULL;
    default:
      if (alt == NULL) {
	Yap_Error(SYSTEM_ERROR,t,"Bug in Indexing Code");
	return NULL;
      } else {
	ipc = alt;
      }
    }
  }
  return NULL;
}

yamop *
Yap_CleanUpIndex(LogUpdIndex *blk)
{
  PredEntry *ap;
  LogUpdIndex *pblk = blk, *tblk;

  /* first, go up until findin'your pred */
  tblk = pblk;
  while (!(tblk->ClFlags & SwitchRootMask))
    tblk = tblk->u.ParentIndex;
  ap = tblk->u.pred;

  if (blk->ClFlags & InUseMask) {
    /* I have to kill this block */
    yamop **caller, *new;
    caller = find_caller(ap, blk->ClCode);
    while (TRUE) {
      new = replace_lu_block(blk, REFRESH, ap, NULL, FALSE);
      /* will be null, if we are in the middle of the current block */
      if (new == NULL) {
	if (!Yap_growheap(FALSE, Yap_Error_Size, NULL)) {
	  Yap_Error(OUT_OF_HEAP_ERROR, TermNil, Yap_ErrorMessage);
	  return FAILCODE;
	}
      }
      if (caller) {
	*caller = new;
      }
      return new;
    }
  } else {
    /* just compact the code */
    yamop *start = blk->ClCode, *codep;
    op_numbers op = Yap_op_from_opcode(start->opc);

    while (op == _jump_if_nonvar) {
      start = NEXTOP(start, xl);
      op = Yap_op_from_opcode(start->opc);
    }
    codep = start->u.Ill.l1;
    start->opc = Yap_opcode(_enter_lu_pred);
    start->u.Ill.l2 = cp_lu_trychain(codep, codep, start, REFRESH, ap, NULL, FALSE, blk, start->u.Ill.s, 0);
    return start;
  }
}

