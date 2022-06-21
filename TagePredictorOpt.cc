#include "predictor.h"
#include <math.h>



/////////////// STORAGE BUDGET JUSTIFICATION ////////////////
// Total storage budget: 32KB + 17 bits
// Total PHT counters: 2^17 
// Total PHT size = 2^17 * 2 bits/counter = 2^18 bits = 32KB
// GHR size: 17 bits
// Total Size = PHT size + GHR size
/////////////////////////////////////////////////////////////

#define TAGE_TABLE_NUM 4
#define BASE_TABLE_INDEX_WIDTH 14
#define BASE_CTR_WIDTH 2
#define BASE_CTR_INIT 2
#define BASE_CTR_MAX 3
#define TAG_WIDTH 9
#define U_WIDTH 2
#define CTR_WIDTH  3
#define TAGGED_ENTRY_LEN 12 // each tagged table has 2^10 entry
#define TAGGED_CTR_INIT 0
#define TAGGED_CTR_MAX 7
#define TAGGED_WEAK_CORRECT 4
#define L1 5
#define L2 14
#define L3 37
#define L4 100
#define USE_ALT_MAX 15
#define USE_ALT_INIT 7


/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

PREDICTOR::PREDICTOR(void){
  historyLength    = L4;
  ghr = 0;

  numBaseTableEntries = 1 << BASE_TABLE_INDEX_WIDTH;
  base_table = new uint8_t[numBaseTableEntries];
  for(UINT32 ii=0; ii < numBaseTableEntries; ii++){
    base_table[ii] = BASE_CTR_INIT;
  }

  numTageTableEntries = 1 << TAGGED_ENTRY_LEN;
  tage_table_len = new UINT32[4];
  tage_table_len[0] = L1;
  tage_table_len[1] = L2;
  tage_table_len[2] = L3;
  tage_table_len[3] = L4;
  tag_table = new TageEntry*[TAGE_TABLE_NUM];
  for (UINT32 j = 0; j < TAGE_TABLE_NUM; j++){
    tag_table[j] = new TageEntry[numTageTableEntries];
    for(UINT32 ii=0; ii< numTageTableEntries; ii++){
      tag_table[j][ii].tag = 0;
      tag_table[j][ii].u = 0;
      tag_table[j][ii].ctr = TAGGED_CTR_INIT;
    }
  }

  clock = 0;
  
  tag = new UINT32[TAGE_TABLE_NUM];
  tag_table_idx = new UINT32[TAGE_TABLE_NUM];

  use_alt = USE_ALT_INIT;
  pred_is_new_entry = false;
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

bool   PREDICTOR::GetPrediction(UINT32 PC){
  UINT32 base_index   = PC % numBaseTableEntries;
  uint8_t base_counter = base_table[base_index];
  pred = base_counter > BASE_CTR_MAX/2;
  provider_component = -1;
  altpred_component = -1;
  altpred = pred;

  for(int i = 0; i < TAGE_TABLE_NUM; i++){
    tag[i] = get_tag(PC, i);
    tag_table_idx[i] = get_tagged_idx(PC, i);
    if(tag_table[i][tag_table_idx[i]].tag == tag[i]){
      altpred_component = provider_component;
      altpred = pred;
      provider_component = i;
      pred = tag_table[i][tag_table_idx[i]].ctr > TAGGED_CTR_MAX / 2;
    }
  }
  
  if(provider_component != -1 && tag_table[provider_component][tag_table_idx[provider_component]].u == 0 &&
    (tag_table[provider_component][tag_table_idx[provider_component]].ctr == TAGGED_CTR_MAX / 2 ||
    tag_table[provider_component][tag_table_idx[provider_component]].ctr == TAGGED_CTR_MAX / 2 + 1)){
      pred_is_new_entry = true;
    }
    else{
      pred_is_new_entry = false;
    }
  
  if(pred_is_new_entry && use_alt > USE_ALT_MAX / 2 + 1){
    return (int)altpred; 
  }else{
    return (int)pred;
  }
  
}


/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

void  PREDICTOR::UpdatePredictor(UINT32 PC, bool resolveDir, bool predDir, UINT32 branchTarget){

  UINT32 base_index   = PC % numBaseTableEntries;
  uint8_t base_counter = base_table[base_index];

  // update counter of provider component
  if(provider_component == -1){
      if(resolveDir == TAKEN){
        base_table[base_index] = SatIncrement(base_counter, BASE_CTR_MAX);
      }else{
        base_table[base_index] = SatDecrement(base_counter);
      } 
  }
  else{
    uint8_t pred_ctr = tag_table[provider_component][tag_table_idx[provider_component]].ctr;
    if(resolveDir == TAKEN){
      tag_table[provider_component][tag_table_idx[provider_component]].ctr = SatIncrement(pred_ctr, TAGGED_CTR_MAX);
    }
    else{
      tag_table[provider_component][tag_table_idx[provider_component]].ctr = SatDecrement(pred_ctr);
    } 
  }


  // if prediction is incorrect, allocate entry
  // don't need to allocate entry when altpred is false and pred is right, the u tag will do it(otherwise, we will always get the new entry?)
  if(resolveDir != pred && provider_component != TAGE_TABLE_NUM - 1 /*&& !(pred == resolveDir && pred_is_new_entry)*/){
    int unalloc_idx[TAGE_TABLE_NUM] = {-1, -1, -1, -1};
    int count = 0;
    for(int i = provider_component + 1; i < TAGE_TABLE_NUM; i++){
      if(tag_table[i][tag_table_idx[i]].u == 0){
        unalloc_idx[count] = i;
        count ++;
      }
    }
    // if uk > 0 for k in (i, M), then uk = uk-1 for all uk 
    if(count == 0){
      for(int i = provider_component + 1; i < TAGE_TABLE_NUM; i++){
        tag_table[i][tag_table_idx[i]].u = SatDecrement(tag_table[i][tag_table_idx[i]].u);
      }
    }
    else{
      srand(3407);
      // allocate one entry each time
      // if more than one T_i need allocate, for i < j, the probility of allocate entry in T_i = 2 * T_j
      // example:count = 3, rand = {0} for unalloc[2], rand = {1, 2} for unalloc[1], rand = {3,4,5,6} for unalloc[0] 
      int total_pro = (1 << count) - 1;
      int r = rand() % total_pro;
      int choose_idx = -1;
      for (int i = 0; i < count; i++){
        if( r >= (1<<i) - 1 && r < ( 1 << (i + 1) ) - 1 ){
          choose_idx = unalloc_idx[count - 1 - i];
        }
      }
      UINT32 idx_in_tag_table_choose = tag_table_idx[choose_idx];
      tag_table[choose_idx][idx_in_tag_table_choose].tag = tag[choose_idx];
      tag_table[choose_idx][idx_in_tag_table_choose].u = 0;
      if(resolveDir)
        tag_table[choose_idx][idx_in_tag_table_choose].ctr = TAGGED_WEAK_CORRECT;
      else
        tag_table[choose_idx][idx_in_tag_table_choose].ctr = TAGGED_WEAK_CORRECT - 1;
    }
  }

  // update use_alt
  if(altpred != pred && provider_component != -1){
    if(pred != resolveDir){
      use_alt = SatIncrement(use_alt, USE_ALT_MAX);
    }
    else{
      use_alt = SatDecrement(use_alt);
    }
  }

  // update u
  if(altpred != pred && provider_component != -1){
    uint8_t u = tag_table[provider_component][tag_table_idx[provider_component]].u;
    if(pred == resolveDir){
      tag_table[provider_component][tag_table_idx[provider_component]].u = SatIncrement(u, 3);
    }
    else{
      tag_table[provider_component][tag_table_idx[provider_component]].u = SatDecrement(u);
    }
  }

  // After 256k branch, reset u
  clock ++;
  uint8_t mask = 0;
  if(clock == 1 << 18){
    mask = 1;
  }
  else if(clock == 1 << 19){
    mask = 2;
    clock = 0;
  }
  if(mask){
    for(int i = 0; i < TAGE_TABLE_NUM; i++){
      for (UINT32 j = 0; j < numTageTableEntries; j++){
        tag_table[i][j].u = tag_table[i][j].u & mask;
      }
    }
  }

  // update ghr
  ghr = ghr << 1;
  if(resolveDir){
    ghr++;
  }

}

UINT32 PREDICTOR::get_tagged_idx(UINT32 PC, int bank_no){
  __uint128_t temp_ghr = ghr;
  int history_width = tage_table_len[bank_no];
  UINT32 temp_pc = PC & ( (1 << TAGGED_ENTRY_LEN) - 1 );
  
  // folder
  while(history_width > 0){
    int block_width = min(history_width, TAGGED_ENTRY_LEN);
    temp_pc ^= temp_ghr & ( (1 << block_width) - 1 );
    temp_ghr = temp_ghr >> block_width;
    history_width -= block_width;
  }

  return temp_pc & ( (1 << TAGGED_ENTRY_LEN) - 1 ) ;
}

uint16_t PREDICTOR::get_tag(UINT32 PC, int bank_no){
  __uint128_t temp_ghr = ghr & ((1 << tage_table_len[bank_no])-1);
  // TODO 
  return (temp_ghr + PC * 1000000007) & ((1<<TAG_WIDTH) - 1);  
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////

void    PREDICTOR::TrackOtherInst(UINT32 PC, OpType opType, UINT32 branchTarget){

  // This function is called for instructions which are not
  // conditional branches, just in case someone decides to design
  // a predictor that uses information from such instructions.
  // We expect most contestants to leave this function untouched.

  return;
}

/////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////
