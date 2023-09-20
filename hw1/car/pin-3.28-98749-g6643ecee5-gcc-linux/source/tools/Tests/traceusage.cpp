/*
 * Copyright (C) 2004-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//  This tool collects the dynamic counts of the behavior of the traces
//  that are being generated by pin as the application executes.

#include "pin.H"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <map>
#include <algorithm>
#include <assert.h>
using std::dec;
using std::endl;
using std::hex;
using std::make_pair;
using std::map;
using std::multimap;
using std::ofstream;
using std::pair;
using std::setw;

/* ================================================================== */
/* Global Data Structures                                             */
/* ================================================================== */

/* ================================================================== */
/* Information on every branch in a trace */
typedef struct BBL_INFO_STRUCT
{
    UINT32 ins_cnt;
    UINT32 exec_cnt;
    UINT32 code_size;
    UINT32 accum_code_size;
    UINT32 bbl_exit_cnt;
    BBL_INFO_STRUCT() : ins_cnt(0), exec_cnt(0), code_size(0), accum_code_size(0), bbl_exit_cnt(0) {}
} BBL_INFO;
typedef map< ADDRINT, BBL_INFO > BBL_MAP;

/* ================================================================== */
/* Information on every trace produced by pin */
typedef struct TRACE_INFO_STRUCT
{
    UINT32 exec_cnt;
    UINT32 bbl_cnt;
    UINT32 ins_cnt;
    UINT32 fall_cnt;

    BBL_MAP bbl_info;
    TRACE_INFO_STRUCT() : exec_cnt(0), bbl_cnt(0), ins_cnt(0), fall_cnt(0) {}
} TRACE_INFO;
typedef multimap< ADDRINT, TRACE_INFO > TRACE_MAP;

/* ================================================================== */
TRACE_MAP Trace_Information;
ofstream OutFile("traceusage.trace");

/* ================================================================== */
/* 
 Inc. the counter for a trace when the trace is short and results
 in the program falling off the trace and into a new trace
*/
VOID TraceFall_Info(ADDRINT trace_addr)
{
    TRACE_MAP::iterator tr_it = Trace_Information.find(trace_addr);

    assert(tr_it == Trace_Information.end());

    TRACE_INFO& tr_info = tr_it->second;
    tr_info.fall_cnt++;
}

/* ================================================================== */
/*
 Insert a new bbl record into the trace record
*/
VOID Bbl_Info(ADDRINT bbl_addr, UINT32 ins_cnt, ADDRINT trace_addr, UINT32 code_size, UINT32 accum_code_size)
{
    TRACE_MAP::iterator tr_it = Trace_Information.find(trace_addr);

    assert(tr_it != Trace_Information.end());

    TRACE_INFO& tr_info      = tr_it->second;
    BBL_MAP& Bbl_Information = tr_info.bbl_info;
    BBL_MAP::iterator bbl_it = Bbl_Information.find(bbl_addr);

    if (bbl_it == Bbl_Information.end())
    {
        BBL_INFO bbl_info;
        bbl_info.ins_cnt         = ins_cnt;
        bbl_info.code_size       = code_size;
        bbl_info.accum_code_size = accum_code_size;
        bbl_info.exec_cnt        = 0;
        bbl_info.bbl_exit_cnt    = 0;

        Bbl_Information.insert(make_pair(bbl_addr, bbl_info));
    }
}

/* ================================================================== */
/*
 Insert/Inc. the usage of bbls and the exit status of a bbl
*/
VOID BblExit_Info(ADDRINT bbl_addr, ADDRINT trace_addr)
{
    TRACE_MAP::iterator tr_it = Trace_Information.find(trace_addr);

    assert(tr_it != Trace_Information.end());

    TRACE_INFO& tr_info      = tr_it->second;
    BBL_MAP& Bbl_Information = tr_info.bbl_info;

    BBL_MAP::iterator bbl_it = Bbl_Information.begin();
    for (; bbl_it != Bbl_Information.end(); bbl_it++)
    {
        BBL_INFO& bbl_info = bbl_it->second;

        /* Inc usage of every bbl above the exit bbl for it is utilized */
        bbl_info.exec_cnt++;

        /* The exit bbl itself */
        if (bbl_addr == bbl_it->first)
        {
            bbl_info.bbl_exit_cnt++;
            return;
        }
    }

    assert(bbl_it != Bbl_Information.end());
}

/* ================================================================== */
/*
 Insert/Inc. the usage of a trace
*/
VOID Trace_Info(ADDRINT trace_addr, UINT32 bbl_cnt, UINT32 ins_cnt)
{
    TRACE_MAP::iterator it = Trace_Information.find(trace_addr);

    // First visit
    if (it == Trace_Information.end())
    {
        TRACE_INFO tr_info;
        tr_info.bbl_cnt  = bbl_cnt;
        tr_info.ins_cnt  = ins_cnt;
        tr_info.exec_cnt = 0;

        Trace_Information.insert(make_pair(trace_addr, tr_info));
    }
    else
    {
        it->second.exec_cnt++;
    }
}

/* ================================================================== */
/*
 Instrumentation function;
 Track the trace, the usage of the code in the bbls and the bbl exits
*/
VOID Trace(TRACE trace, VOID* v)
{
    /* Add trace to db */
    Trace_Info(TRACE_Address(trace), TRACE_NumBbl(trace), TRACE_NumIns(trace));

    /* Inc. trace execution count */
    TRACE_InsertCall(trace, IPOINT_BEFORE, (AFUNPTR)Trace_Info, IARG_ADDRINT, TRACE_Address(trace), IARG_UINT32,
                     TRACE_NumBbl(trace), IARG_UINT32, TRACE_NumIns(trace), IARG_END);

    USIZE accum_code_size = 0;
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        accum_code_size += BBL_Size(bbl);

        /* Add bbl to db */
        Bbl_Info(BBL_Address(bbl), BBL_NumIns(bbl), TRACE_Address(trace), BBL_Size(bbl), accum_code_size);

        INS ins = BBL_InsTail(bbl);

        if (INS_IsValidForIpointTakenBranch(ins))
        {
            /* Inc. bbl exit count */
            INS_InsertCall(ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)BblExit_Info, IARG_ADDRINT, BBL_Address(bbl), IARG_ADDRINT,
                           TRACE_Address(trace), IARG_END);
        }
    }

    /* Trace falloff in the case the bbl is too big and does not
       necessarily end with a bbl terminating op                */
    INS ins = BBL_InsTail(TRACE_BblTail(trace));
    if (!INS_IsControlFlow(ins))
    {
        /* Inc. trace falloff exit count */
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)TraceFall_Info, IARG_ADDRINT, TRACE_Address(trace), IARG_END);
    }
}

/* ================================================================== */
/*
 Print bbl exit info
*/
VOID PrintBblExit(pair< const ADDRINT, BBL_INFO > bbl_info)
{
    OutFile << hex << "0x" << bbl_info.first << "\t" << setw(5) << dec << bbl_info.second.exec_cnt << "\t" << setw(5) << dec
            << bbl_info.second.ins_cnt << "\t" << setw(5) << dec << bbl_info.second.bbl_exit_cnt << "\t" << setw(5) << dec
            << bbl_info.second.code_size << "\t" << setw(5) << dec << bbl_info.second.accum_code_size << "\t" << endl;
}

/* ================================================================== */
/*
 Print a trace
*/
VOID PrintTrace(pair< const ADDRINT, TRACE_INFO > trace_info)
{
    /* Trace information */
    OutFile << "==================================================================" << endl;
    OutFile << "Trace:"
            << "\t" << setw(10) << "# Exe"
            << "\t" << setw(5) << "# Bbl"
            << "\t" << setw(5) << "# Ins"
            << "\t" << endl;
    OutFile << "==================================================================" << endl;

    OutFile << hex << "0x" << trace_info.first << "\t" << setw(5) << dec << trace_info.second.exec_cnt << "\t" << setw(5) << dec
            << trace_info.second.bbl_cnt << "\t" << setw(5) << dec << trace_info.second.ins_cnt << "\t" << endl
            << endl;

    /* Bbl information */
    OutFile << "------------------------------------------------------------------" << endl;
    OutFile << "Bbl:"
            << "\t" << setw(10) << "# Exe"
            << "\t" << setw(5) << "# Ins"
            << "\t" << setw(5) << "Exit"
            << "\t" << setw(5) << "Size"
            << "\t" << setw(5) << "ASize"
            << "\t" << endl;
    OutFile << "------------------------------------------------------------------" << endl;

    BBL_MAP& Bbl_Information = trace_info.second.bbl_info;

    /* Bbl exit information */
    for_each(Bbl_Information.begin(), Bbl_Information.end(), PrintBblExit);

    OutFile << endl << endl;
}

/* ================================================================== */
/*
 Output the trace usage to a file
*/
VOID DumpTraceInfo(INT32 code, VOID* v) { for_each(Trace_Information.begin(), Trace_Information.end(), PrintTrace); }

/* ================================================================== */
/*
 Initialize and begin program execution under the control of Pin
*/
int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);

    TRACE_AddInstrumentFunction(Trace, 0);

    PIN_AddFiniFunction(DumpTraceInfo, 0);

    PIN_StartProgram();

    return 0;
}
