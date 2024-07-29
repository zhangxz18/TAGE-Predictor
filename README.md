# 计算机系统结构 选题2 分支预测

## 实现内容

在本次实验中，我使用了与TAGE_SC_L相似的算法，完成了由TAGE Predictor、Loop Predictor、Corrector Filter等部件组成的分支预测器。在cbp4的、32KB的测试中，MPKI为3.097。

## 目录说明

GsharePredictor.h/cc:提供的baseline

TagePredictor.h/cc: 基本的TAGE预测器

TagePredictorOpt.h/cc:对新分配entry使用use_alt进行优化的TAGE预测器

TagePredictor8Com.h/cc：TAGE有8个component。然而MPKI并没有降低。

LTagePredictor.h/cc：TAGE with Loop table

TAGE_SC_LPredictor.h/cc：TAGE with loop table and Corrector filter

predictor.h/cc：调参后，并且修正了TAGEOpt中对于use_alt的update的错误

如果想要运行某个预测器，用它替换掉cbp4（https://jilp.org/cbp2014/framework.html）的`sim/predictor.h`和`sim/predictor.cc`即可。

## 算法设计

整个算法部件由TAGE、Loop Predictor、Corrector Filter三个主要部件组成。我完成代码的时候是按照以上顺序依次完成三个部件，且三个部件之间比较独立，因此将分开叙述。

![演示文稿1.jpg](https://s2.loli.net/2022/06/22/iHA6eb3VPkOSU2c.jpg)

### TAGE

TAGE是一种ppm-like的分支预测器，该方法的我在实现TAGE时主要参考了参考文献[1]和[2]的实现。其主要思路是通过不同长度的history和PC进行哈希来获得预测结果，对于与不同长度的history有强关联的分支都能取得比较好的效果。

#### 组成

TAGE由一个BaseTable和M个Tagged Table组成。

![2.png](https://s2.loli.net/2022/06/22/blwRtO7Yygcip2e.png)

BaseTable(图中T0）是一个简单的根据PC索引的二位饱和计数器，每个表项对应一个index的饱和计数器。

Tagged Table（图中T1-T4)则是用PC和不同长度的GHR（Global history register）生成索引和标签以获取表项。Tagged Table的表项如下

```c++
struct TageEntry{
  uint16_t tag; // 9 bits
  uint8_t u;    // 2 bits
  uint8_t ctr;  // 3 bits
};
```

其中tag是将（PC, GHR[0:L])进行哈希后得到的标签，用于判断当前entry是否对应到目前的状态；u是useful位，用于判断当前entry是否能提供有用的信息；ctr是3位饱和计数器。

+ provider component： 在TAGE预测部件中负责提供最终结果的那个

+ alternate prediction （altpred）：除了provider component外，hit的那个预测部件的结果（如果没有部件hit，则认为T0是altpred。

#### 预测

+ BaseTable直接根据pc获得对应表项，通过计数器判断结果；
+ Tagged Table将pc与不同len的GHR做hash1，获得表项的index，取出entry。然后判断entry的tag和hash2（PC, GHR[0:L])是否相等，相等则命中。如果命中，entry.ctr就可以给出当前表的预测结果。注意，这里的hash1和hash2不能是同一个hash函数。
+ 选择匹配长度最长的预测结果作为最终结果，其对应的Tagged Table为provider component。匹配长度第二长的作为备选结果，其对应的Table为altpred。

#### 更新

+ 根据实际结果，更新provider component的计数器
+ 如果预测结果正确，不需要分配新的entry。
+ 如果预测结果错误，可能要分配新的entry。
  + 如果provider component Ti，如果i <M,那么分配一个新的entry在Tk（i<k<M)。
  + 两种分配的可能：
    + 如果有uk=0的项，那么Tk就allocate
    + 否则所有的uj,i<j<M，全部-1
  + 如果有两个component都可以被分配，序号小的那个概率是序号大的那个的两倍
  + 刚分配的entry：prediction设为weak correct，u设为0
+ 更新useful位
  + 当altpred和最终预测结果pred不同，如果provider component的预测结果对了，则provider component的u+1，否则-1
  + 每256k个branch后reset一次高位，再256k后reset一次低位

#### 对新分配节点的优化

如果按照PPM-LIKE的逻辑，一个刚分配的entry很可能会被直接投入使用。但是在特定的测例下，新分配的entry有很大概率会出错，我们需要对这种情况做一些优化。

+ 总体思路：用一个4bit的全局计数器，如果provider_component是新分配的entry，且其预测结果出错，但是altpred预测正确，该计数器+1；反过来计数器-1。然后如果这个计数器超过了阈值，在遇到new_entry的时候，我们就使用altpred作为结果。

+ 判断是不是新entry只需要：useful counter == 0 且 weak（taken or not taken）。
+ 优化效果：大约有0.2MPKI的优化(3.27->3.09)

### loop predictor

loop predictor是LTage的组成部分，我的实现主要参考了参考文献[3]。该部件主要是为了处理一些以常数次数循环的循环。其能弥补TAGE的GHR长度有限，对于一些非常长的循环并不能有效预测；或者TAGE对于循环的反应可能不够灵敏。

优化效果：大约有0.04MPKI的优化(3.275->3.237)

#### 构成

loop predictor的主要结构是一个LoopTable，其表项如下。

```c++
struct LoopTableEntry{
  uint16_t past_iter_count; //14 bits，该循环总共迭代次数
  uint16_t now_iter_count;  // 14 bits，当前已经迭代次数
  uint16_t tag;             // 14 bits，匹配判断
  uint8_t confidenc_count;  // 2 bits，该entry可信度
  uint8_t age_count;        // 8 bits，该entry是否过时
};
```

#### 预测

+ 如果`past_iter_count<now_iter_count`，预测为Taken；如果`past_iter_count=now_iter_count`，预测为Not Taken。

+ 如果`confidence_count`为MAX，就认为这个预测结果可以采用；否则预测结果不可以采用。


#### 更新

+ 若loop_tag和entry.tag不同，这说明当前的表项不匹配，就减少entry.age，如果entry.age已经是0，就需要分配新的表项。（这样做的理由是：age实际上标记了一个entry希望被替换的次数，如果一直希望被替换，证明原来的loop可能已经失效了）
+ 若loop_tag和entry.tag相同
  + now_iter_count ++
  + 如果loop predictor提供的预测是正确的，并且预测结果是NotTaken（这证明目前循环到了最后一步），那么重置now_iter_count。然后判断tage_pred提供的结果是否正确，如果tage_pred的结果是错误的，证明loop predictor提供的结果比Tage提供的更可信，confidenc_count和age_count就应该增加。 
  + 如果loop predictor提供的预测是错误的，那么需要判断这个entry是否是刚分配的。
    + 如果是刚分配的entry的前两次循环，（因为第一次循环可能不是在循环开始就分配entry的，统计次数会出错，所以需要前两次），需要把past_iter设成now_iter，然后清空now_iter。
    + 如果不是刚分配的，证明这个entry已经是错误的了，直接清空整个entry即可。
+ allocate entry：allocate时age设置为255，conf设为0，past_iter设为MAX（这样可以在第一次循环中确定past_iter的值）。

### Corrector Filter

corrector filter主要用于纠正一些Tage不能正确预测的场景。比如，一些分支与历史无关，可能就是统计意义上的偏向某个方向，这时候Tage可能不如单纯的基于PC的预测器。Corrector Filter就可以部分纠正这种问题。实现基本参考文献[4]。

优化效果：大约有0.02MPKI的优化(3.24->3.22)

#### 构成

```c++
struct CorrectorFilterEntry{
  uint8_t ctr; // 6 bits的饱和计数器
  uint8_t tag; // 7 bits，匹配判断 
}
```

#### 预测

首先考虑Tage的结果的可信度。如果Tage的可信度是高的（通过ctr来判断，ctr偏离weak足够远就证明可信度高），那么就不需要再使用Corrector filter。如果Tage的可信度低，就使用Corrector filter的结果。直接通过pc和tage 预测结果索引得到表项，然后判断tag是否匹配，而后根据ctr获得结果即可。

#### 更新

+ 如果tage的预测结果是正确的，且tag不匹配，则不需要做任何操作（tage已经能解决问题）
+ 如果tage的预测结果是错的，且tag匹配，则根据分支结果更新饱和计数器（可能需要使用当前的表项）
+ 如果tage的预测结果是错的，且tag不匹配，则根据不同情况，分配表项或者更新饱和计数器。

## 参数设置与存储需求

+ TAGE参数设置

  + component number：1 base table, 4 tagged table

  + Base Table
    +  entry number：$2^{14}$
    + ctr: $2$

  + tagged table(1-4)
    + entry size：14bit(具体的域见上面算法分析部分)
    + entry number: $2^{12}$

+ loop table

  + entry size: 52bit(具体的域见上面算法分析部分)

  + entry number：256

+ Corrector filter

  + entry size：13 bit(具体的域见上面算法分析部分)
  + entry number：236

+ 全局参数/额外budget

  + GHR: 128 bits

  + TAGE重置useful时使用的clock：19 bits

  + use_alt的计数器：4bits

  + use_cf的计数器：4bits

    其余几项只用于在一个分支指令的GetPrediction()和UpdatePrediction()中传递信息，因此实际上不占用空间。（包括provider_component:2bits，altpred_component:2bits，pred:1bit，altpred:1bit，tage_pred:1bit，high_conf:1bit，cf_pred:1bit）

+ 大块使用的空间: $2^{13} * 2 + 4* 2^{12} * 14 + 52 * 256 + 13 * 236 = 262140 < 262144$

+ 零碎使用的空间：$128+19+4+4=155<512$

  可见是符合空间要求的。

## 实验结果

最终测试结果如图所示，MPKI为3.097。

![3.png](https://s2.loli.net/2022/06/22/jzt1bGdrvUBZ29i.png)

## 参考文献

[1] Michaud, Pierre. "A PPM-like, tag-based branch predictor." *The Journal of Instruction-Level Parallelism* 7 (2005): 10.

[2] Seznec, André, and Pierre Michaud. "A case for (partially) TAgged GEometric history length branch prediction." *The Journal of Instruction-Level Parallelism* 8 (2006): 23.

[3]Seznec, André. "A 256 kbits l-tage branch predictor." *Journal of Instruction-Level Parallelism (JILP) Special Issue: The Second Championship Branch Prediction Competition (CBP-2)* 9 (2007): 1-6.

[4]Seznec, André. "Tage-sc-l branch predictors." *JILP-Championship Branch Prediction*. 2014.
