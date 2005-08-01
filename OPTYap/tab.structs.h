/**********************************************************************
                                                               
                       The OPTYap Prolog system                
  OPTYap extends the Yap Prolog system to support or-parallel tabling
                                                               
  Copyright:   R. Rocha and NCC - University of Porto, Portugal
  File:        tab.structs.h
  version:     $Id: tab.structs.h,v 1.9 2005-08-01 15:40:39 ricroc Exp $   
                                                                     
**********************************************************************/

/* ---------------------------- **
**      Tabling mode flags      **
** ---------------------------- */

#define Mode_SchedulingOn       0x00000001L  /* yap_flags[TABLING_MODE_FLAG] */
#define Mode_CompletedOn        0x00000002L  /* yap_flags[TABLING_MODE_FLAG] */

#define Mode_Local              0x10000000L  /* yap_flags[TABLING_MODE_FLAG] + struct table_entry */
#define Mode_LoadAnswers        0x20000000L  /* yap_flags[TABLING_MODE_FLAG] + struct table_entry */

#define DefaultMode_Local       0x00000001L  /* struct table_entry */
#define DefaultMode_LoadAnswers 0x00000002L  /* struct table_entry */

#define SetMode_SchedulingOn(X)        (X) |= Mode_SchedulingOn
#define SetMode_CompletedOn(X)         (X) |= Mode_CompletedOn
#define IsMode_SchedulingOn(X)         ((X) & Mode_SchedulingOn)
#define IsMode_SchedulingOff(X)        !IsMode_SchedulingOn(X)
#define IsMode_CompletedOn(X)          ((X) & Mode_CompletedOn)
#define IsMode_CompletedOff(X)         !IsMode_CompletedOn(X)

#define SetMode_Local(X)               (X) |= Mode_Local
#define SetMode_Batched(X)             (X) &= ~Mode_Local
#define SetMode_LoadAnswers(X)         (X) |= Mode_LoadAnswers
#define SetMode_ExecAnswers(X)         (X) &= ~Mode_LoadAnswers
#define IsMode_Local(X)                ((X) & Mode_Local)
#define IsMode_LoadAnswers(X)          ((X) & Mode_LoadAnswers)

#define SetDefaultMode_Local(X)        (X) |= DefaultMode_Local
#define SetDefaultMode_Batched(X)      (X) &= ~DefaultMode_Local
#define SetDefaultMode_LoadAnswers(X)  (X) |= DefaultMode_LoadAnswers
#define SetDefaultMode_ExecAnswers(X)  (X) &= ~DefaultMode_LoadAnswers
#define IsDefaultMode_Local(X)         ((X) & DefaultMode_Local)
#define IsDefaultMode_LoadAnswers(X)   ((X) & DefaultMode_LoadAnswers)



/* ---------------------------- **
**      Struct table_entry      **
** ---------------------------- */

typedef struct table_entry {
#ifdef YAPOR
  lockvar lock;
#endif /* YAPOR */
  struct pred_entry *pred_entry;
  int pred_arity;
  int mode_flags;
  struct subgoal_trie_node *subgoal_trie;
  struct subgoal_hash *hash_chain;
  struct table_entry *next;
} *tab_ent_ptr;

#define TabEnt_lock(X)          ((X)->lock)
#define TabEnt_pe(X)            ((X)->pred_entry)
#define TabEnt_arity(X)         ((X)->pred_arity)
#define TabEnt_mode(X)          ((X)->mode_flags)
#define TabEnt_subgoal_trie(X)  ((X)->subgoal_trie)
#define TabEnt_hash_chain(X)    ((X)->hash_chain)
#define TabEnt_next(X)          ((X)->next)



/* -------------------------------------------------------- **
**      Structs subgoal_trie_node and answer_trie_node      **
** -------------------------------------------------------- */

typedef struct subgoal_trie_node {
  Term entry;
#ifdef TABLE_LOCK_AT_NODE_LEVEL
  lockvar lock;
#endif /* TABLE_LOCK_AT_NODE_LEVEL */
  struct subgoal_trie_node *parent;
  struct subgoal_trie_node *child;
  struct subgoal_trie_node *next;
} *sg_node_ptr;

typedef struct answer_trie_node {
  OPCODE trie_instruction;  /* u.opc */
#ifdef YAPOR
  int or_arg;               /* u.ld.or_arg */
#endif /* YAPOR */
  Term entry;
#ifdef TABLE_LOCK_AT_NODE_LEVEL
  lockvar lock;
#endif /* TABLE_LOCK_AT_NODE_LEVEL */
  struct answer_trie_node *parent;
  struct answer_trie_node *child;
  struct answer_trie_node *next;
} *ans_node_ptr;

#define TrNode_instr(X)        ((X)->trie_instruction)
#define TrNode_or_arg(X)       ((X)->or_arg)
#define TrNode_entry(X)        ((X)->entry)
#define TrNode_lock(X)         ((X)->lock)
#define TrNode_parent(X)       ((X)->parent)
#define TrNode_child(X)        ((X)->child)
#define TrNode_sg_fr(X)        ((X)->child)
#define TrNode_next(X)         ((X)->next)



/* ---------------------------------------------- **
**      Structs subgoal_hash and answer_hash      **
** ---------------------------------------------- */

typedef struct subgoal_hash {
  /* the first field is used for compatibility **
  ** with the subgoal_trie_node data structure */
  Term mark;    
  int number_of_buckets;
  struct subgoal_trie_node **buckets;
  int number_of_nodes;
  struct subgoal_hash *next;
} *sg_hash_ptr;

typedef struct answer_hash {
  /* the first field is used for compatibility **
  ** with the answer_trie_node data structure  */
  OPCODE mark;
  int number_of_buckets;
  struct answer_trie_node **buckets;
  int number_of_nodes;
  struct answer_hash *next;
} *ans_hash_ptr;

#define Hash_mark(X)            ((X)->mark)
#define Hash_num_buckets(X)     ((X)->number_of_buckets)
#define Hash_seed(X)            ((X)->number_of_buckets - 1)
#define Hash_buckets(X)         ((X)->buckets)
#define Hash_bucket(X,N)        ((X)->buckets + N)
#define Hash_num_nodes(X)       ((X)->number_of_nodes)
#define Hash_next(X)            ((X)->next)



/* ------------------------------ **
**      Struct subgoal_frame      **
** ------------------------------ */

typedef struct subgoal_frame {
#ifdef YAPOR
  lockvar lock;
  int generator_worker;
  struct or_frame *top_or_frame_on_generator_branch;
#endif /* YAPOR */
  yamop *code_of_subgoal;
  enum {
    start      =  0,
    evaluating =  1,
    complete   =  2,
    compiled   =  3
  } state_flag;
  choiceptr generator_choice_point;
  struct answer_hash *hash_chain;
  struct answer_trie_node *answer_trie;
  struct answer_trie_node *first_answer;
  struct answer_trie_node *last_answer;
#ifdef INCOMPLETE_TABLING
  struct answer_trie_node *try_answer;
#endif /* INCOMPLETE_TABLING */
  struct subgoal_frame *next;
} *sg_fr_ptr;

#define SgFr_lock(X)           ((X)->lock)
#define SgFr_gen_worker(X)     ((X)->generator_worker)
#define SgFr_gen_top_or_fr(X)  ((X)->top_or_frame_on_generator_branch)
#define SgFr_code(X)           ((X)->code_of_subgoal)
#define SgFr_tab_ent(X)        (((X)->code_of_subgoal)->u.ld.te)
#define SgFr_arity(X)          (((X)->code_of_subgoal)->u.ld.s)
#define SgFr_state(X)          ((X)->state_flag)
#define SgFr_gen_cp(X)         ((X)->generator_choice_point)
#define SgFr_hash_chain(X)     ((X)->hash_chain)
#define SgFr_answer_trie(X)    ((X)->answer_trie)
#define SgFr_first_answer(X)   ((X)->first_answer)
#define SgFr_last_answer(X)    ((X)->last_answer)
#define SgFr_try_answer(X)     ((X)->try_answer)
#define SgFr_next(X)           ((X)->next)

/* ------------------------------------------------------------------------------------------- **
   SgFr_lock:          spin-lock to modify the frame fields.
   SgFr_gen_worker:    the id of the worker that had allocated the frame.
   SgFr_gen_top_or_fr: a pointer to the top or-frame in the generator choice point branch. 
                       When the generator choice point is shared the pointer is updated 
                       to its or-frame. It is used to find the direct dependency node for 
                       consumer nodes in other workers branches.
   SgFr_code           initial instruction of the subgoal's compiled code.
   SgFr_tab_ent        a pointer to the correspondent table entry.
   SgFr_arity          the arity of the subgoal.
   SgFr_state:         a flag that indicates the subgoal state.
   SgFr_gen_cp:        a pointer to the correspondent generator choice point.
   SgFr_hash_chain:    a pointer to the first answer_hash struct for the subgoal in hand.
   SgFr_answer_trie:   a pointer to the top answer trie node.
                       It is used to check for/insert new answers.
   SgFr_first_answer:  a pointer to the bottom answer trie node of the first available answer.
   SgFr_last_answer:   a pointer to the bottom answer trie node of the last available answer.
   SgFr_try_answer:    a pointer to the bottom answer trie node of the last tried answer.
                       It is used when a subgoal was not completed during the previous evaluation.
                       Not completed subgoals start by trying the answers already found.
   SgFr_next:          a pointer to chain between subgoal frames.
** ------------------------------------------------------------------------------------------- */



/* --------------------------------- **
**      Struct dependency_frame      **
** --------------------------------- */

typedef struct dependency_frame {
#ifdef YAPOR
  lockvar lock;
  int leader_dependency_is_on_stack;
  struct or_frame *top_or_frame;
#ifdef TIMESTAMP_CHECK
  long timestamp;
#endif /* TIMESTAMP_CHECK */
#endif /* YAPOR */
  choiceptr backchain_choice_point;
  choiceptr leader_choice_point;
  choiceptr consumer_choice_point;
  struct answer_trie_node *last_consumed_answer;
  struct dependency_frame *next;
} *dep_fr_ptr;

#define DepFr_lock(X)                    ((X)->lock)
#define DepFr_leader_dep_is_on_stack(X)  ((X)->leader_dependency_is_on_stack)
#define DepFr_top_or_fr(X)               ((X)->top_or_frame)
#define DepFr_timestamp(X)               ((X)->timestamp)
#define DepFr_backchain_cp(X)            ((X)->backchain_choice_point)
#define DepFr_leader_cp(X)               ((X)->leader_choice_point)
#define DepFr_cons_cp(X)                 ((X)->consumer_choice_point)
#define DepFr_last_answer(X)             ((X)->last_consumed_answer)
#define DepFr_next(X)                    ((X)->next)

/* ---------------------------------------------------------------------------------------------------- **
   DepFr_lock:                   lock variable to modify the frame fields.
   DepFr_leader_dep_is_on_stack: the generator choice point for the correspondent consumer choice point 
                                 is on the worker's stack (FALSE/TRUE).
   DepFr_top_or_fr:              a pointer to the top or-frame in the consumer choice point branch. 
                                 When the consumer choice point is shared the pointer is updated to 
                                 its or-frame. It is used to update the LOCAL_top_or_fr when a worker 
                                 backtracks through answers.
   DepFr_timestamp:              a timestamp used to optimize the search for suspension frames to be 
                                 resumed.
   DepFr_backchain_cp:           a pointer to the nearest choice point with untried alternatives.
                                 It is used to efficiently return (backtrack) to the leader node where 
                                 we perform the last backtracking through answers operation.
   DepFr_leader_cp:              a pointer to the leader choice point.
   DepFr_cons_cp:                a pointer to the correspondent consumer choice point.
   DepFr_last_answer:            a pointer to the last consumed answer.
   DepFr_next:                   a pointer to chain between dependency frames.  
** ---------------------------------------------------------------------------------------------------- */



/* --------------------------------- **
**      Struct suspension_frame      **
** --------------------------------- */

#ifdef YAPOR
typedef struct suspension_frame {
  struct or_frame *top_or_frame_on_stack;
  struct dependency_frame *top_dependency_frame;
  struct subgoal_frame *top_subgoal_frame;
  struct suspended_block {
    void *resume_register;
    void *block_start;
    long block_size;
  } global_block, local_block, trail_block;
  struct suspension_frame *next;
} *susp_fr_ptr;
#endif /* YAPOR */

#define SuspFr_top_or_fr_on_stack(X)  ((X)->top_or_frame_on_stack)
#define SuspFr_top_dep_fr(X)          ((X)->top_dependency_frame)
#define SuspFr_top_sg_fr(X)           ((X)->top_subgoal_frame)
#define SuspFr_global_reg(X)          ((X)->global_block.resume_register)
#define SuspFr_global_start(X)        ((X)->global_block.block_start)
#define SuspFr_global_size(X)         ((X)->global_block.block_size)
#define SuspFr_local_reg(X)           ((X)->local_block.resume_register)
#define SuspFr_local_start(X)         ((X)->local_block.block_start)
#define SuspFr_local_size(X)          ((X)->local_block.block_size)
#define SuspFr_trail_reg(X)           ((X)->trail_block.resume_register)
#define SuspFr_trail_start(X)         ((X)->trail_block.block_start)
#define SuspFr_trail_size(X)          ((X)->trail_block.block_size)
#define SuspFr_next(X)                ((X)->next)



/* --------------------------------------------------------------------------- **
**      Structs generator_choicept, consumer_choicept and loader_choicept      **
** --------------------------------------------------------------------------- */

struct generator_choicept {
  struct choicept cp;
  struct dependency_frame *cp_dep_fr;  /* NULL if batched scheduling */
  struct subgoal_frame *cp_sg_fr;
#ifdef LOW_LEVEL_TRACER
  struct pred_entry *cp_pred_entry;
#endif /* LOW_LEVEL_TRACER */
};

struct consumer_choicept {
  struct choicept cp;
  struct dependency_frame *cp_dep_fr;
#ifdef LOW_LEVEL_TRACER
  struct pred_entry *cp_pred_entry;
#endif /* LOW_LEVEL_TRACER */
};

struct loader_choicept {
  struct choicept cp;
  struct answer_trie_node *cp_last_answer;
#ifdef LOW_LEVEL_TRACER
  struct pred_entry *cp_pred_entry;
#endif /* LOW_LEVEL_TRACER */
};
