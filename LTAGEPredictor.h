#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include "utils.h"
#include "tracer.h"
#include <bitset>

#define TAGE_TABLE_NUM 4
#define BASE_TABLE_INDEX_WIDTH 13
#define BASE_CTR_WIDTH 2
#define BASE_CTR_INIT 2
#define BASE_CTR_MAX 3
#define TAG_WIDTH 9
#define U_WIDTH 2
#define CTR_WIDTH  3
#define TAGGED_TABLE_INDEX_WIDTH 12 // each tagged table has 2^10 entry
#define TAGGED_CTR_INIT 0
#define TAGGED_CTR_MAX 7
#define TAGGED_WEAK_CORRECT 4
// #define TAGGED_TABLE_HISTORY_WIDTH {5, 14, 37, 100} // each page table compute with history len
#define GHR_LEN 700 // global history register len
#define USE_ALT_MAX 15
#define USE_ALT_INIT 8
#define LOOP_TABLE_ENTRY_NUM 512
#define LOOP_TABLE_INDEX_WIDTH 9
#define LOOP_TAG_WIDTH 14
#define LOOP_CONFIDENC_WIDTH 2
#define LOOP_COUNT_WIDTH 14
#define LOOP_AGE_WIDTH 8

struct TageEntry{
  uint16_t tag; // 9 bits
  uint8_t u;    // 2 bits
  uint8_t ctr;  // 3 bits
};

struct LoopTableEntry{
  uint16_t past_iter_count; //14 bits
  uint16_t now_iter_count;  // 14 bits
  uint16_t tag;             // 14 bits
  uint8_t confidenc_count;  // 2 bits
  uint8_t age_count;        // 8 bits
};

class LoopTable{
  public:
    LoopTableEntry ltable[LOOP_TABLE_ENTRY_NUM];
    bool use_loop;
    bool loop_pred;
    uint32_t loop_idx;
    uint16_t loop_tag;

    LoopTable() = default;

    void init(){
      for(int i = 0; i < LOOP_TABLE_ENTRY_NUM; i++){
        ltable[i].past_iter_count = 0;
        ltable[i].now_iter_count = 0;
        ltable[i].tag = 0;
        ltable[i].confidenc_count = 0;
        ltable[i].age_count = 0;
      }
      use_loop = false;
      loop_pred = false;
      loop_idx = 0;
      loop_tag = 0;
    }

    void get_loop_pred(UINT32 pc){
      use_loop = false;
      loop_pred = false;
      loop_idx = pc & ((1 << LOOP_TABLE_INDEX_WIDTH) - 1);
      loop_tag = (pc >> LOOP_TABLE_INDEX_WIDTH) & ((1 << LOOP_TAG_WIDTH) - 1);
      if(loop_tag == ltable[loop_idx].tag){
        if(ltable[loop_idx].past_iter_count > ltable[loop_idx].now_iter_count){
          loop_pred = TAKEN;
        }
        else if(ltable[loop_idx].past_iter_count == ltable[loop_idx].now_iter_count){
          loop_pred = NOT_TAKEN;
        }
        if(ltable[loop_idx].confidenc_count == (1<<LOOP_CONFIDENC_WIDTH) - 1){
          use_loop = true;
        }
        else{
          use_loop = false;
        }
      }
    }

    void update_loop_pred(UINT32 pc, bool resolveDir, bool tage_pred){
      // tag not match
      if(loop_tag != ltable[loop_idx].tag){
        if(ltable[loop_idx].age_count > 0){
          ltable[loop_idx].age_count = SatDecrement(ltable[loop_idx].age_count);
        }
        // allocate new if age = 0
        else{
          ltable[loop_idx].tag = loop_tag;
          ltable[loop_idx].past_iter_count = (1 << LOOP_COUNT_WIDTH) - 1;
          ltable[loop_idx].now_iter_count = 0;
          ltable[loop_idx].confidenc_count = 0;
          ltable[loop_idx].age_count = (1 << LOOP_AGE_WIDTH) - 1;
        }
      }
      // tag match
      else{
        ltable[loop_idx].now_iter_count++;
        // prediction is correct
        if(loop_pred == resolveDir){
          if(resolveDir == NOT_TAKEN){
            ltable[loop_idx].now_iter_count = 0;
            if(tage_pred != resolveDir){
              ltable[loop_idx].confidenc_count = SatIncrement(ltable[loop_idx].confidenc_count, (1<<LOOP_CONFIDENC_WIDTH) - 1);
              ltable[loop_idx].age_count = SatIncrement(ltable[loop_idx].age_count, (1 << LOOP_AGE_WIDTH) - 1);
            }
          }
        }
        // prediction is incorrect
        else{
          // new allocated entry
          if(ltable[loop_idx].age_count == (1 << LOOP_AGE_WIDTH) - 1 && ltable[loop_idx].confidenc_count <= 1){
            ltable[loop_idx].past_iter_count = ltable[loop_idx].now_iter_count;
            ltable[loop_idx].now_iter_count = 0;
          }
          else{
            ltable[loop_idx].now_iter_count = 0;
            ltable[loop_idx].age_count = 0;
            ltable[loop_idx].confidenc_count = 0;
            ltable[loop_idx].past_iter_count = 0;
            ltable[loop_idx].tag = 0;
          }
        }
      }
    }
};


class PREDICTOR{
private:
  __uint128_t ghr;           // global history register
  uint8_t  *base_table;          // base prediction table
  TageEntry **tag_table;
  UINT32  historyLength; // history length
  UINT32  numBaseTableEntries; // entries in pht
  const UINT32  tage_table_history_width[TAGE_TABLE_NUM] = {5, 14, 37, 100};
  UINT32  numTageTableEntries;

  
  UINT32 tag[TAGE_TABLE_NUM];
  UINT32 tag_table_idx[TAGE_TABLE_NUM];
  
  UINT32 clock;
  int provider_component;
  int altpred_component;
  bool pred;
  bool altpred;

  uint16_t use_alt;
  bool pred_is_new_entry;

  LoopTable ltable;


 public:

  // The interface to the four functions below CAN NOT be changed

  PREDICTOR(void);
  bool    GetPrediction(UINT32 PC);  
  void    UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget);
  void    TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget);

  // Contestants can define their own functions below
  uint16_t get_tag(UINT32 PC, int bank_idx);
  UINT32 get_tagged_idx(UINT32 PC, int bank_idx);

  

};



/***********************************************************/
#endif

