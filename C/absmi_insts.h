/*****************************************************************
 *                         INSTRUCTIONS                           *
 *****************************************************************/

#ifdef INDENT_CODE
{
  {
    {
#endif /* INDENT_CODE */

      
BOp(Ystop, l);
      SET_ASP(YREG, E_CB*sizeof(CELL));
      /* make sure ASP is initialised */
      saveregs();

#if PUSH_REGS
      restore_absmi_regs(old_regs);
#endif
#if BP_FREE
      P1REG = PCBACKUP;
#endif
      return 1;
      ENDBOp();

      BOp(Nstop, e);
      SET_ASP(YREG, E_CB*sizeof(CELL));
      saveregs();
#if PUSH_REGS
      restore_absmi_regs(old_regs);
#endif
#if BP_FREE
      P1REG = PCBACKUP;
#endif
      return 0;
      ENDBOp();



        /************************************************************************\
         *      Native Code Execution                                            *
\************************************************************************/

#if YAP_JIT
        /* native_me  */
        BOp(jit_handler, J);
        if (!PREG->y_u.J.jh->fi.bcst.c) PREG->y_u.J.jh->mf.isground = IsGround(PREG);
        PREG->y_u.J.jh->fi.bcst.c++;

        /* Did PREG reach threshold value to become critical? */
        if (PREG->y_u.J.jh->fi.bcst.c == (COUNT)(ExpEnv.config_struc.frequency_bound*(ExpEnv.config_struc.profiling_startp)) && !PREG->y_u.J.jh->mf.isground) {
#if YAP_DBG_PREDS
          if (ExpEnv.debug_struc.pprint_me.criticals != 0 && ExpEnv.debug_struc.pprint_me.criticals != 0x1) {
            fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
            fprintf(stderr, "%s", (char*)ExpEnv.debug_struc.pprint_me.criticals);
          }
#endif
          goto critical_lbl;
        }
#if YAP_DBG_PREDS
        print_main_when_head(PREG, ON_INTERPRETER);
#endif
        PREG = NEXTOP(PREG, J);
        JMPNext();
        ENDBOp();
#endif

#include "cp_absmi_insts.c"
#include "lu_absmi_insts.c"     
#include "fail_absmi_insts.c"
#include "control_absmi_insts.c"
#include "unify_absmi_insts.c"
#include "fli_absmi_insts.c"
#include "or_absmi_insts.c"     
#include "index_absmi_insts.c"     
#include "prim_absmi_insts.c"     
#include "meta_absmi_insts.c"     
