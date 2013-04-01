#!/bin/env python
#-*- encoding:utf-8 -*-
import os
import subprocess
import cPickle as pickler
#====Basic Config======#
PINBASE = "/home/liuyix/Dev/pin/"
SIMBIN = PINBASE + "/pin"
SPLASH2HOME = "/home/liuyix/Dev/splash2/codes"
KERNELSHOME = SPLASH2HOME + "/kernels/"
APPSHOME = SPLASH2HOME + "/apps/"
gStatDicts = {}
#key - path - binname - parameter
kernels = {'fft':('fft','FFT','-t'),
           'radix':('radix','RADIX','-t'),
           'lu':('lu/contiguous_blocks','LU','-t'),
           'lu-non':('lu/non_contiguous_blocks','LU','-t'),
           'cholesky':('cholesky','CHOLESKY',['-t','./inputs/tk29.O'])
}

apps = {
    'barnes':('barnes','BARNES',['<','./input.p1']),
        'fmm':('fmm','FMM',['<','./input.2048.p1']),
        'ocean':('ocean/contiguous_partitions','OCEAN',' '),
        'ocean-non':('ocean/non_contiguous_partitions','OCEAN',' '),
        'water-nsquared':('water-nsquared','WATER-NSQUARED',['<','./input.2048.p1']),
        'water-spatial':('water-spatial','WATER-SPATIAL',['<',' ./input.p1'])
}

########definations###########

#spm mode
#1. all
#2. prob
#3. count

def spmConfigList(size=512,count=8,prob=0.005,outputName='spm.out',mode='all'):
    """
    
    Arguments:
    - `size`: spm size
    - `count`: 访问计数阈值
    - `prob`: 随机访问频率
    - `outputName`: 输出名字
    - `mode`:
    """
    if mode == 'all':
        n_mode = 1
    elif mode == 'count':
        n_mode = 3
    elif mode == 'prob':
        n_mode = 2
    para = ['-ss',str(size),'-sn',str(count),'-sp',str(prob),'-sw',str(n_mode)];
    simcmd = [SIMBIN,'-t',PINBASE+'/dcache.so'] + para + ['-o',outputName,'--']
    return simcmd
pass

def exeCacheSim(output,appdict,basepath):
    simcmd = [SIMBIN,'-t',PINBASE+'/dcache.so','-sz 0','-o',output,'--']
    #return simcmd
    #simcmdStr = ' '.join(simcmd)
    for k,v in appdict.iteritems():
        executeSingle(basepath,v,simcmd,output)
    
def execute(basepath,appdict,simCmd,outputName,bDebug=False):
    bDebugTmp = bDebug
    for k,v in appdict.iteritems():
        print 'running: ',k
        executeSingle(basepath,v,simCmd,outputName,bDebug=bDebug)

def executeSingle(basepath,prog,simCmd,outputName,bDebug=False,bReadStat=True,strProg=None):
    os.chdir(basepath + prog[0])
    if type(prog[2]) == str:
        fullcmd = simCmd + ['./'+prog[1],prog[2]]
    else:
        fullcmd = simCmd + ['./'+prog[1]] + prog[2]
    fullcmdStr = ' '.join(fullcmd)
    print 'executing: ',fullcmdStr
    if strProg == None:
        strProg = prog[0]
    print 'strProg: ',strProg
    if not bDebug:
        os.system(fullcmdStr)
        #subprocess.call(['/bin/cat',outputName])
    if bReadStat:
        readOutput(strProg,outputName)

    


def executeAll(cmd,outputName,bDebug=True,enableKernels=True,enableApps=True):
    if enableKernels:
        execute(KERNELSHOME,kernels,cmd,outputName,bDebug)
    if enableApps:
        execute(APPSHOME,apps,cmd,outputName,bDebug)
    
def readOutput(strProg,output):
    '''
    read output stats and returns a tuple representing the stats
    '''
    fileOutput = open(output)
    stat = {}
    for strLine in fileOutput:
        print strLine
        strDatalist = strLine.strip().split(' ')
        print strDatalist,'len: ',len(strDatalist)
        stat[strDatalist[0]] = strDatalist[1]
    for k,v in stat.iteritems():
        print k,v
    # 放入全局的容器中
    if strProg not in gStatDicts:
        gStatDicts[strProg] = []
    gStatDicts[strProg].append(stat)

def printStatOutput():
    '''
    print gStatDicts
    '''
    for k in gStatDicts.iterkeys():
        print k
        for element in gStatDicts[k]:
            print element
            
def floatRange(start,stop,step):
    r = start
    while r <= stop:
        yield r
        r += step
        
########################

if __name__ == '__main__':
    

    sizeTuple = (64,128,256,512)
    blksizeTuple = (1,2,4)
    countTuple = (2,4,8,16)
    freqTuple = (0.005,0.05)
    spmsize = 512
    spmcount = 8
    spmprob = 0.005
    outputNameAll = 'both.out'
    cacheOutputName = 'cache.out'
    outputNameProb = 'prob.out'
    outputNameCount = 'count.out'
    for spmsize in sizeTuple:
        for spmcount in blksizeTuple:
            simCmdCountOnly = spmConfigList(spmsize,spmcount,spmprob,outputNameCount,'count')
            executeAll(simCmdCountOnly,outputNameCount,enableApps=True,bDebug=False)
            for prob in floatRange(freqTuple[0],freqTuple[1],0.001):
                simCmdAll = spmConfigList(spmsize,spmcount,prob,outputNameAll,'all')
                executeAll(simCmdAll,outputNameAll,enableApps=True,bDebug=False)
        for spmprob in floatRange(freqTuple[0],freqTuple[1],0.001):
            simCmdProbOnly = spmConfigList(spmsize,spmcount,0.008,outputNameProb,'prob')
            executeAll(simCmdProbOnly,outputNameProb,enableApps=True,bDebug=False)
    # exeCacheSim(cacheOutputName,apps,APPSHOME)
    # exeCacheSim(cacheOutputName,kernels,KERNELSHOME)
    # simCmdAll = spmConfigList(spmsize,spmcount,0.03,outputNameAll,'all')
    # #executeSingle(KERNELSHOME,kernels['fft'],simCmdAll,outputName)
    # executeAll(simCmdAll,outputNameAll,enableApps=False,bDebug=False)
    # #outputNameProb = 'prob.out'
    # simCmdProbOnly = spmConfigList(spmsize,spmcount,0.008,outputNameProb,'prob')
    # executeAll(simCmdProbOnly,outputNameProb,enableApps=False,bDebug=False)
    # #outputNameCount = 'count.out'
    # simCmdCountOnly = spmConfigList(spmsize,16,spmprob,outputNameCount,'count')
    # executeAll(simCmdCountOnly,outputNameCount,enableApps=False,bDebug=False)
    # #readOutput('/home/liuyix/Dev/splash2/codes/kernels/fft/both.out')
    print '====================================='
    printStatOutput()
    statFile = file('overall_stat.out','w')
    pickler.dump(gStatDicts,statFile)
    statFile.close()
    print '====================================='
    
else:
    print 'Running in module!'