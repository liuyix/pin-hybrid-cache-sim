/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2012 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
//
// @ORIGINAL_AUTHOR: Artur Klauser
// @EXTENDED: Rodric Rabbah (rodric@gmail.com) 
//

/*! @file
 *  This file contains an ISA-portable cache simulator
 *  data cache hierarchies
 */


#include "pin.H"

#include <iostream>
#include <fstream>
#include <cassert>

#include "cache.H"
#include "pin_profile.H"
#include "spm.H"

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */
//为该pintool添加命令行参数
//格式为 KNOB<DATA_TYPE> KnobVarname(KNOB_MODE_WRITEONCE,"pintool","选项字符","默认值","注释")
//使用变量:string和UINT32使用是 KnobVarname.Value(),BOOL类型直接使用
//EnableKnobFamily("pintool2");

//KNOB_BASE::EnableKnobFamily("foo");
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,    "pintool",
    "o", "dcache.out", "specify dcache file name");
KNOB<BOOL>   KnobTrackLoads(KNOB_MODE_WRITEONCE,    "pintool",
    "tl", "0", "track individual loads -- increases profiling time");
KNOB<BOOL>   KnobTrackStores(KNOB_MODE_WRITEONCE,   "pintool",
   "ts", "0", "track individual stores -- increases profiling time");
KNOB<UINT32> KnobThresholdHit(KNOB_MODE_WRITEONCE , "pintool",
   "rh", "100", "only report memops with hit count above threshold");
KNOB<UINT32> KnobThresholdMiss(KNOB_MODE_WRITEONCE, "pintool",
   "rm","100", "only report memops with miss count above threshold");
KNOB<UINT32> KnobCacheSize(KNOB_MODE_WRITEONCE, "pintool",
    "c","32", "cache size in kilobytes");
KNOB<UINT32> KnobLineSize(KNOB_MODE_WRITEONCE, "pintool",
    "b","32", "cache block size in bytes");
KNOB<UINT32> KnobAssociativity(KNOB_MODE_WRITEONCE, "pintool",
    "a","4", "cache associativity (1 for direct mapped)");


//=================================================//
KNOB<UINT32> KnobSPMSize(KNOB_MODE_WRITEONCE,"pintool",

    "ss","256","spm size(kilo Byte)");

KNOB<UINT32> KnobSPMBlockSize(KNOB_MODE_WRITEONCE,"pintool","sb","1","spm block size(kilo Byte)");

KNOB<double> KnobSPMProbability(KNOB_MODE_WRITEONCE,"pintool","sp","0.01","spm alloc probability");

KNOB<UINT32> KnobSPMNthreshold(KNOB_MODE_WRITEONCE,"pintool",
                               "sn","8","spm visit count threshold");
KNOB<UINT32> KnobSPMStrategy(KNOB_MODE_WRITEONCE,"pintool",
                               "sw","1",
                               "hybrid configruation,1 for two approach hybrid,2 for single probability,3 for single visit count");
KNOB<BOOL> KnobSPMSwitch(KNOB_MODE_WRITEONCE,"pintool","sz","1","turn on spm mode,on default");

/* ===================================================================== */

INT32 Usage()
{
    cerr <<
        "This tool represents a cache simulator.\n"
        "\n";

    cerr << KNOB_BASE::StringKnobSummary();

    cerr << endl;

    return -1;
}

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

// wrap configuation constants into their own name space to avoid name clashes
namespace DL1
{
    const UINT32 max_sets = KILO; // cacheSize / (lineSize * associativity);
    const UINT32 max_associativity = 256; // associativity;
    const CACHE_ALLOC::STORE_ALLOCATION allocation = CACHE_ALLOC::STORE_ALLOCATE;

    typedef CACHE_ROUND_ROBIN(max_sets, max_associativity, allocation) CACHE;
}

DL1::CACHE* dl1 = NULL;
SPM* spm = NULL;

typedef enum
{
    COUNTER_MISS = 0,
    COUNTER_HIT = 1,
    COUNTER_NUM
} COUNTER;


//COUNTER_ARRAY结构在pin_profile.H中定义，没有文档。
typedef  COUNTER_ARRAY<UINT64, COUNTER_NUM> COUNTER_HIT_MISS;

//pin_profile.H中定义的模版类
// holds the counters with misses and hits
// conceptually this is an array indexed by instruction address
COMPRESSOR_COUNTER<ADDRINT, UINT32, COUNTER_HIT_MISS> profile;

//profile在这里用作以instruction address为排序的cache统计使用变量
/* ===================================================================== */
/*
 * 在这里有3个维度的分类，首先是 Load/Store，之后是 Single/Multi，然后是Fast后缀和无fast后缀。前两种含义自明。最后有无Fast使用profile变量进行统计，有fast则直接进行cache的功能模拟。
 调用的是CACHE对象的Accessn方法。
 */
VOID LoadMulti(ADDRINT addr, UINT32 size, UINT32 instId)
{
    // first level D-cache
    const BOOL dl1Hit = dl1->Access(addr, size, CACHE_BASE::ACCESS_TYPE_LOAD);

    const COUNTER counter = dl1Hit ? COUNTER_HIT : COUNTER_MISS;
    profile[instId][counter]++;
}

/* ===================================================================== */

VOID StoreMulti(ADDRINT addr, UINT32 size, UINT32 instId)
{
    // first level D-cache
    const BOOL dl1Hit = dl1->Access(addr, size, CACHE_BASE::ACCESS_TYPE_STORE);

    const COUNTER counter = dl1Hit ? COUNTER_HIT : COUNTER_MISS;
    profile[instId][counter]++;
}

/* ===================================================================== */

VOID LoadSingle(ADDRINT addr, UINT32 instId)
{
    // @todo we may access several cache lines for 
    // first level D-cache
    const BOOL dl1Hit = dl1->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);

    const COUNTER counter = dl1Hit ? COUNTER_HIT : COUNTER_MISS;
    profile[instId][counter]++;
}
/* ===================================================================== */

VOID StoreSingle(ADDRINT addr, UINT32 instId)
{
    // @todo we may access several cache lines for 
    // first level D-cache
    const BOOL dl1Hit = dl1->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_STORE);

    const COUNTER counter = dl1Hit ? COUNTER_HIT : COUNTER_MISS;
    profile[instId][counter]++;
}

/* ===================================================================== */

VOID LoadMultiFast(ADDRINT addr, UINT32 size)
{
    dl1->Access(addr, size, CACHE_BASE::ACCESS_TYPE_LOAD);
}

/* ===================================================================== */

VOID StoreMultiFast(ADDRINT addr, UINT32 size)
{
    dl1->Access(addr, size, CACHE_BASE::ACCESS_TYPE_STORE);
}

/* ===================================================================== */

VOID LoadSingleFast(ADDRINT addr)
{
    dl1->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_LOAD);    
}

/* ===================================================================== */

VOID StoreSingleFast(ADDRINT addr)
{
    dl1->AccessSingleLine(addr, CACHE_BASE::ACCESS_TYPE_STORE);    
}

//==========================


VOID HybridLoad(ADDRINT addr, UINT32 size, UINT32 instId)
{
    if (KnobSPMSwitch && spm->Access(addr,size))
    {
        return;
    }
    else
    {
        if (size <= 4)
        {
            LoadSingleFast(addr);
        }
        else
        {
            LoadMultiFast(addr,size);
        }
    }
    
}

VOID HybridStore(ADDRINT addr, UINT32 size, UINT32 instId)
{
    if (KnobSPMSwitch && spm->Access(addr,size))
    {
        return;
    }
    else
    {
        if (size <=4)
        {
            StoreSingleFast(addr);
        }
        else
        {
            StoreMultiFast(addr,size);
        }
    }
}


/* ===================================================================== */
/*
Instruction函数是Instrument的函数，在该工具中仅在memory operation时被调用，该函数中进行上面各种操作统计函数的调用。
 */
VOID Instruction(INS ins, void * v)
{
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    // Instrument each memory operand. If the operand is both read and written
    // it will be processed twice.
    // Iterating over memory operands ensures that instructions on IA-32 with
    // two read operands (such as SCAS and CMPS) are correctly handled.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        const UINT32 size = INS_MemoryOperandSize(ins, memOp);
        //const BOOL   single = (size <= 4);//size <=4 就是singleline??什么意思？
        const ADDRINT iaddr = INS_Address(ins);
        const UINT32 instId = profile.Map(iaddr);
		///////////////////////////////////////////////////////////////

		/*此处插入SPM模块代码*/
		if (INS_MemoryOperandIsRead(ins,memOp))
		{
            //load operation
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR) HybridLoad,
                IARG_MEMORYOP_EA, memOp,
                IARG_UINT32, size,
                IARG_UINT32, instId,
                IARG_END
            );
		}
		if (INS_MemoryOperandIsWritten(ins,memOp))
        {
            //store operation
            INS_InsertPredicatedCall(
                ins,IPOINT_BEFORE, (AFUNPTR) HybridStore,
                IARG_MEMORYOP_EA, memOp,
                IARG_UINT32, size,
                IARG_UINT32, instId,
                IARG_END
            );
        }
        
		//////////////////////////////////////////////////////////////
        
    //     if (INS_MemoryOperandIsRead(ins, memOp))
    //     {
    //         if( KnobTrackLoads )
    //         {
    //             // map sparse INS addresses to dense IDs
    //             const ADDRINT iaddr = INS_Address(ins);
    //             const UINT32 instId = profile.Map(iaddr);

    //             if( single )
    //             {
    //                 INS_InsertPredicatedCall(
    //                     ins, IPOINT_BEFORE, (AFUNPTR) LoadSingle,
    //                     IARG_MEMORYOP_EA, memOp,
    //                     IARG_UINT32, instId,
    //                     IARG_END);
    //             }
    //             else
    //             {
    //                 INS_InsertPredicatedCall(
    //                     ins, IPOINT_BEFORE,  (AFUNPTR) LoadMulti,
    //                     IARG_MEMORYOP_EA, memOp,
    //                     IARG_UINT32, size,
    //                     IARG_UINT32, instId,
    //                     IARG_END);
    //             }
    //         }
    //         else
    //         {
    //             if( single )
    //             {
    //                 INS_InsertPredicatedCall(
    //                     ins, IPOINT_BEFORE,  (AFUNPTR) LoadSingleFast,
    //                     IARG_MEMORYOP_EA, memOp,
    //                     IARG_END);
                        
    //             }
    //             else
    //             {
    //                 INS_InsertPredicatedCall(
    //                     ins, IPOINT_BEFORE,  (AFUNPTR) LoadMultiFast,
    //                     IARG_MEMORYOP_EA, memOp,
    //                     IARG_UINT32, size,
    //                     IARG_END);
    //             }
    //         }
    //     }
        
    //     if (INS_MemoryOperandIsWritten(ins, memOp))
    //     {
    //         if( KnobTrackStores )
    //         {
    //             const ADDRINT iaddr = INS_Address(ins);
    //             const UINT32 instId = profile.Map(iaddr);

    //             if( single )
    //             {
    //                 INS_InsertPredicatedCall(
    //                     ins, IPOINT_BEFORE,  (AFUNPTR) StoreSingle,
    //                     IARG_MEMORYOP_EA, memOp,
    //                     IARG_UINT32, instId,
    //                     IARG_END);
    //             }
    //             else
    //             {
    //                 INS_InsertPredicatedCall(
    //                     ins, IPOINT_BEFORE,  (AFUNPTR) StoreMulti,
    //                     IARG_MEMORYOP_EA,memOp,
    //                     IARG_UINT32, size,
    //                     IARG_UINT32, instId,
    //                     IARG_END);
    //             }
    //         }
    //         else
    //         {
    //             if( single )
    //             {
    //                 INS_InsertPredicatedCall(
    //                     ins, IPOINT_BEFORE,  (AFUNPTR) StoreSingleFast,
    //                     IARG_MEMORYOP_EA, memOp,
    //                     IARG_END);
                        
    //             }
    //             else
    //             {
    //                 INS_InsertPredicatedCall(
    //                     ins, IPOINT_BEFORE,  (AFUNPTR) StoreMultiFast,
    //                     IARG_MEMORYOP_EA, memOp,
    //                     IARG_UINT32, size,
    //                     IARG_END);
    //             }
    //         }
    //     }
    }
}

/* ===================================================================== */

VOID Fini(int code, VOID * v)
{

    std::ofstream out(KnobOutputFile.Value().c_str(),std::ios::out|std::ios::app);
    // print D-cache profile
    // @todo what does this print
    if(KnobSPMSwitch)
    {
        out << std::endl << spm->Stats() << std::endl;
    }
    else
    {
        // out << "PIN:MEMLATENCIES 1.0. 0x0\n";
        // out <<
        //     "#\n"
        //     "# DCACHE stats\n"
        //     "#\n";
        out << dl1->StatsLong("", CACHE_BASE::CACHE_TYPE_DCACHE);
        // if( KnobTrackLoads || KnobTrackStores ) {
        //     out <<
        //         "#\n"
        //         "# LOAD stats\n"
        //         "#\n";
        //     out << profile.StringLong();
        // }
    }
    out.close();
}

/* ===================================================================== */

int main(int argc, char *argv[])
{
    PIN_InitSymbols();
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }
    if(KnobSPMSwitch)
    {
        SPM::SPM_Strategy strategy = SPM::ALL;
        switch(KnobSPMStrategy.Value())
        {
          case 1:
            strategy = SPM::ALL; break;
          case 2:
            strategy = SPM::PROB_ONLY; break;
          case 3:
            strategy = SPM::COUNT_ONLY; break;
        }
        spm = new SPM(KnobSPMSize.Value(),
                      KnobSPMBlockSize.Value(),
                      KnobSPMProbability.Value(),KnobSPMNthreshold.Value(),
                      strategy);
    }
    dl1 = new DL1::CACHE("L1 Data Cache", 
                         KnobCacheSize.Value() * KILO,
                         KnobLineSize.Value(),
                         KnobAssociativity.Value());
    profile.SetKeyName("iaddr          ");
    profile.SetCounterName("dcache:miss        dcache:hit");
    COUNTER_HIT_MISS threshold;
    threshold[COUNTER_HIT] = KnobThresholdHit.Value();
    threshold[COUNTER_MISS] = KnobThresholdMiss.Value();
    profile.SetThreshold( threshold );
    
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();
    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
