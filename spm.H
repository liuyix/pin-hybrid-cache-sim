#ifndef PIN_SPM_H
#define PIN_SPM_H
#endif

#include <iostream>
#include <sstream>
#include <cstdlib>
#include <time.h>
#include <map>

#include "cache.H"

#define KILO 1024
#define MEGA (KILO*KILO)
#define GIGA (KILO*MEGA)
#define MAX_BLK_CNT 1024
#define RANDOMRANGE 1000

typedef UINT64 SPM_STATS; 




/*!
 *  @brief Computes floor(log2(n))
 *  Works by finding position of MSB set.
 *  @returns -1 if n == 0.
 */
// static inline INT32 FloorLog2(UINT32 n)
// {
//     INT32 p = 0;

//     if (n == 0) return -1;

//     if (n & 0xffff0000) { p += 16; n >>= 16; }
//     if (n & 0x0000ff00)	{ p +=  8; n >>=  8; }
//     if (n & 0x000000f0) { p +=  4; n >>=  4; }
//     if (n & 0x0000000c) { p +=  2; n >>=  2; }
//     if (n & 0x00000002) { p +=  1; }

//     return p;
// }

class Block
{
  public:
    Block(UINT32 size);
    virtual ~Block();
};



// simple spm implementation
class SPM
{
  public:
    enum SPM_Strategy
    {
        PROB_ONLY,
        COUNT_ONLY,
        ALL
    };
    //SPM(UINT32 size,UINT32 blksize,double threshold,UINT32 nthreshold);
    SPM(UINT32 size,UINT32 blksize,double threshold,UINT32 nthreshold,SPM_Strategy s);
    
    bool Access(ADDRINT addr, UINT32 size);
    bool Access2(ADDRINT addr,UINT32 size);//访问计数
    bool Access3(ADDRINT addr,UINT32 size);
    string Stats() const;//最后统计结果输出
  private:
    const UINT32 _spmsize;
    const UINT32 _blocksize;
    const double _threshold;
    const UINT32 _nthreshold;
    const UINT32 _blockcnt;//calcuated
    const UINT32 _addrmask;//calcuated
    const SPM_Strategy _strategy;
    
    UINT32 _curblkcnt;//当前block总数
    SPM_STATS _accessCnt;
    SPM_STATS _hitCnt;
    SPM_STATS _replaceCnt;
    std::map<ADDRINT,UINT32> _blkmap;
    ADDRINT _blocks[MAX_BLK_CNT];
    
  protected:
    ADDRINT GetPageIndex(ADDRINT addr);
    bool InvokeAlloc();//产生随机数决定是否启动spm空间alloc
    VOID Insert(ADDRINT pageidx);
    //VOID Replace(ADDRINT oldidx,ADDRINT newidx);// 使用LRU？
    bool HasSpace() { return _curblkcnt < _blockcnt; };
    bool Find(ADDRINT pageidx);
};

// SPM::SPM(UINT32 size,UINT32 blksize,double threshold,UINT32 nthreshold)          
// {
//     // srand(time(NULL));
//     // //_blkmap = new std::map<ADDRINT,UINT32>();
//     // _accessCnt = _hitCnt = _replaceCnt = 0;
//     // _strategy = ALL;
//     SPM(size,blksize,threshold,nthreshold,ALL);
// }

SPM::SPM(UINT32 size,UINT32 blksize,double threshold,UINT32 nthreshold,SPM_Strategy s)
    : _spmsize(size),
      _blocksize(blksize),
      _threshold(threshold),
      _nthreshold(nthreshold),
      _blockcnt(size/blksize),
      _addrmask(FloorLog2(blksize*KILO)),//计算 log^2(blksize)
      _strategy(s)
{
    srand(time(NULL));
    //_blkmap = new std::map<ADDRINT,UINT32>();
    _accessCnt = _hitCnt = _replaceCnt = 0;
}


ADDRINT SPM::GetPageIndex(ADDRINT addr)
{
    //_addrmask
    return (~0x0 << _addrmask) & addr;
}

bool SPM::InvokeAlloc()//产生随机数决定是否启动spm空间alloc
{
    //given _threshold (0<_threshold<1)
    int boundary = (int)(_threshold * RANDOMRANGE);
    int rand_n = rand()%RANDOMRANGE;
    if (rand_n < boundary)
    {
        return true;
    }
    return false;
}
/*该函数包含了replace操作 */
VOID SPM::Insert(ADDRINT pageidx)
{
    if (HasSpace())
    {
        _blocks[_curblkcnt] = pageidx;
        _curblkcnt++;
    }
    else
    {
        //randomly replace ?
        int rand_idx = rand()%_blockcnt;
        //Replace(rand_idx,)
        _replaceCnt++;
        _blocks[rand_idx] = pageidx;
    }
}
// VOID SPM::Replace(int oldidx,int newidx)// 使用LRU？
// {

// }
bool SPM::Find(ADDRINT pageidx)
{
    UINT32 i=0;
    for (i=0; i<_curblkcnt; ++i) 
    {
        if (_blocks[i] == pageidx)
        {
            return true;
        }
    }
    return false;
}



bool SPM::Access(ADDRINT addr, UINT32 size)
{
    /*
      if page_index(addr) belongs to spm then hit++;
      else
      	miss++
        if gen_random in range then call insert2spm(page_idx)      
     */
    ADDRINT pageidx = GetPageIndex(addr);
    _accessCnt++;
    if (Find(pageidx))
    {
        _hitCnt++;
        return true;
    }
    else
    {
        if (InvokeAlloc())//randomly call spm alloc
        {
            Insert(pageidx);
        }
        return false;
    }
    return false;
}


bool SPM::Access2(ADDRINT addr, UINT32 size)
{
    /*
      if map[addr] not exist then map[addr] = 1;
      else map[addr]++ if map[addr]==16 then insert2spm(addr)
     */
    ADDRINT pageidx = GetPageIndex(addr);
    _accessCnt++;
    if (Find(pageidx))
    {
        _hitCnt++;
        return true;
    }
    else
    {
        if (_blkmap.find(pageidx) != _blkmap.end())
        {
            _blkmap[pageidx]++;
            if (_blkmap[pageidx] == _nthreshold)
            {
                Insert(pageidx);
                _blkmap[pageidx] = 0;
            }
        }//end if find map key
        else
        {
            _blkmap[pageidx] = 1;
        }//end if find key not found
    }
    return false;
    
}

bool SPM::Access3(ADDRINT addr, UINT32 size)
{
    /*
      if found in spm return true
      else
      if should invoke spm call then if blkmap[pageidx] exist then blkmap[pageidx]++ if threashold arrives then Insert2spm else blkmap[pageidx]=1 
     */
    ADDRINT pageidx = GetPageIndex(addr);
    _accessCnt++;
    if (Find(pageidx))
    {
        _hitCnt++;
        return true;
    }
    else
    {
        if (InvokeAlloc())
        {
            if (_blkmap.find(pageidx) != _blkmap.end())
            {
                _blkmap[pageidx]++;
                if (_blkmap[pageidx] == _nthreshold)
                {
                    Insert(pageidx);
                    _blkmap[pageidx] = 0;
                }
            }
            else
            {
                _blkmap[pageidx] = 1;
            }
        }
    }
    return false;
}



string SPM::Stats() const
{
    string statistic = "SPM Summary:\n";
    std::ostringstream oss;
    oss << statistic;
    oss << "SPM size: " << _spmsize 
        << "KB block size: " << _blocksize 
        << "KB Probability: " << _threshold << std::endl; 
    oss << "total:" << _accessCnt 
        << "\thit:" << _hitCnt 
        << "\treplace:" << _replaceCnt << std::endl;
    oss << "hit ratio: " << ((double)_hitCnt / _accessCnt) 
        << "\treplaceCnt: " << ((double)_replaceCnt/_accessCnt) 
        << std::endl;
    string strSummary = oss.str();
    return strSummary;
}
