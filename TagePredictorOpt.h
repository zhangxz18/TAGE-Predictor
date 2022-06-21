#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include "utils.h"
#include "tracer.h"

struct TageEntry{
  uint16_t tag;
  uint8_t u;
  uint8_t ctr;
};


class PREDICTOR{
private:
            // global history register
  uint8_t  *base_table;          // base prediction table
  TageEntry **tag_table;
  UINT32  historyLength; // history length
  UINT32  numBaseTableEntries; // entries in pht
  UINT32  *tage_table_len;
  UINT32  numTageTableEntries;

  
  UINT32 *tag;
  UINT32 *tag_table_idx;
  
  UINT32 clock;
  int provider_component;
  int altpred_component;
  bool pred;
  bool altpred;

  
  __uint128_t ghr; 

  uint16_t use_alt;
  bool pred_is_new_entry;

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

