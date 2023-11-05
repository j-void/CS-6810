
#include "pin.H"


#include <stdlib.h>

#include "dcache_for_prefetcher.hpp"
#include "pin_profile.H"

class PrefetcherInterface {
public:
  virtual void prefetch(ADDRINT addr, ADDRINT loadPC) = 0;
  virtual void train(ADDRINT addr, ADDRINT loadPC) = 0;
};

PrefetcherInterface *prefetcher;

ofstream outFile;
Cache *cache;
UINT64 loads;
UINT64 stores;
UINT64 hits;
UINT64 accesses, prefetches;
string prefetcherName;
int sets;
int associativity;
int blockSize;
UINT64 checkpoint = 100000000;
UINT64 endpoint = 2000000000;

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<string> KnobPrefetcherName(KNOB_MODE_WRITEONCE, "pintool",
  "pref_type","none", "prefetcher name");
KNOB<UINT32> KnobAggression(KNOB_MODE_WRITEONCE, "pintool",
  "aggr", "2", "the aggression of the prefetcher");
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
  "o", "data", "specify output file name");
KNOB<UINT32> KnobCacheSets(KNOB_MODE_WRITEONCE, "pintool",
  "sets", "64", "cache size in kilobytes");
KNOB<UINT32> KnobLineSize(KNOB_MODE_WRITEONCE, "pintool",
  "b", "4", "cache block size in bytes");
KNOB<UINT32> KnobAssociativity(KNOB_MODE_WRITEONCE, "pintool",
  "a", "2", "cache associativity (1 for direct mapped)");

/* ===================================================================== */

// Print a message explaining all options if invalid options are given
INT32 Usage()
{
  cerr << "This tool represents a cache simulator." << endl;
  cerr << KNOB_BASE::StringKnobSummary() << endl;
  return -1;
}

// Take checkpoints with stats in the output file
void takeCheckPoint()
{
  outFile << "The checkpoint has been reached" << endl;
  outFile << "Accesses: " << accesses << " Loads: "<< loads << " Stores: " << stores <<   endl;
  outFile << "Hits: " << hits << endl;
  outFile << "Hit rate: " << double(hits) / double(accesses) << endl;
  outFile << "Prefetches: " << prefetches << endl;
  outFile << "Successful prefetches: " << cache->getSuccessfulPrefs() << endl;
  if (accesses ==  endpoint) exit(0);
}

/* ===================================================================== */

/* None Prefetcher
    This does not prefetch anything.
*/
class NonePrefetcher : public PrefetcherInterface {
public:
  void prefetch(ADDRINT addr, ADDRINT loadPC) {
    return;
  }

  void train(ADDRINT addr, ADDRINT loadPC) {
    return;
  }
};

/* ===================================================================== */

/* Next Line Prefetcher
    This is an example implementation of the next line Prefetcher.
    The stride prefetcher would also need the program counter of the load instruction.
*/
class NextNLinePrefetcher : public PrefetcherInterface {
public:
  void prefetch(ADDRINT addr, ADDRINT loadPC) {
    for (int i = 1; i <= aggression; i++) {
      UINT64 nextAddr = addr + i * blockSize;
      if (!cache->exists(nextAddr)) {  // Use the member function Cache::exists(UINT64) to query whehter a block exists in the cache w/o triggering any LRU changes (not after a demand access)
          cache->prefetchFillLine(nextAddr); // Use the member function Cache::prefetchFillLine(UINT64) when you fill the cache in the LRU way for prefetch accesses
          prefetches++;
      }
    }
  }

  void train(ADDRINT addr, ADDRINT loadPC) {
    return;
  }
};

class StridePrefetcher : public PrefetcherInterface {
  private:
    // Reference Prediction Table
    UINT64 RPT[64][4] = {0};
    UINT64 numberOfEntries = 0; // number of entries in the RPT - initially no entry added
    int entryIdx = -1; // index in RPT for corresponding PC
    // States -> Initial=0; Transient=1; Steady=2; No prediction=3


  public:
    void prefetch(ADDRINT addr, ADDRINT loadPC) {
      // the index of RPT
      entryIdx = -1;
      for (UINT64 rpt_idx = 0; rpt_idx < numberOfEntries; rpt_idx++) { // loop over the entries of RPT
        if (RPT[rpt_idx][0] == loadPC) { //if entry present in RPT
          entryIdx = rpt_idx; // update the entryIdx as we found PC
          UINT64 pred_addr = RPT[rpt_idx][1] + RPT[rpt_idx][2];
          // begin prefetching only if the state is stable and prediction is correct
          // NOTE check if we need RPT[rpt_idx][3] == 2 or RPT[rpt_idx][3] != 3
          if (pred_addr == addr) { // correct prediction
            if (RPT[rpt_idx][3] == 2) { // correct state
              for (int i = 1; i <= aggression; i++) {
                UINT64 nextAddr = addr + i * blockSize;
                if (!cache->exists(nextAddr)) {  
                    cache->prefetchFillLine(nextAddr); 
                    prefetches++;
                }
              }              
            }
          }
          break; // found the entry so no need to look further
        }
      }
    }

    void train(ADDRINT addr, ADDRINT loadPC) {

      if (entryIdx==-1) { // add the entry in RPT as PC not in RPT
        UINT64 entryToReplace = 0; // which entry to replace
        if (numberOfEntries >= 64) { // if RPT is full the replace randomly
          entryToReplace = rand()%64;
        } else { // else add the entry in RPT
          entryToReplace = numberOfEntries;
          numberOfEntries++;
        }

        RPT[entryToReplace][0] = loadPC; //set load PC
        RPT[entryToReplace][1] = addr; // set the previous addr to current addr
        RPT[entryToReplace][2] = 0; // set the stride to 0
        RPT[entryToReplace][3] = 0; // intial state
      } else {
        // we found the entry in the RPT - PC present can use the same index from prefetch
        UINT64 prev_addr = RPT[entryIdx][1]; // previous addr
        UINT64 pred_addr = prev_addr + RPT[entryIdx][2]; // predicted addr = previous addr + stride
        if (RPT[entryIdx][3] == 0) { // if in initial state
          if (addr == pred_addr) { // if prediction correct
            RPT[entryIdx][3] = 2; // change to steady state
            RPT[entryIdx][1] = addr; // update the previous address
          } else { // if prediction incorrect
            RPT[entryIdx][3] = 1; // change to Transient state
            RPT[entryIdx][2] = addr - prev_addr; // update the stride
            RPT[entryIdx][1] = addr; // update the previous address
          }
        } else if (RPT[entryIdx][3] == 1) { // if in Transient state
          if (addr == pred_addr) { // if prediction correct
            RPT[entryIdx][3] = 2; // change to steady state
            RPT[entryIdx][1] = addr; // update the previous address
          } else { // if prediction incorrect
            RPT[entryIdx][3] = 3; // change to No prediction state
            RPT[entryIdx][2] = addr - prev_addr; // update the stride
            RPT[entryIdx][1] = addr; // update the previous address
          }
        } else if (RPT[entryIdx][3] == 2) { // if in Steady state
          if (addr == pred_addr) { // if prediction correct
            RPT[entryIdx][1] = addr + (aggression * blockSize); // update the previous address to address of last prefetched block
          } else { // if prediction incorrect
            RPT[entryIdx][3] = 0; // change to Initial state
            RPT[entryIdx][1] = addr; // update the previous address
          }
        } else if (RPT[entryIdx][3] == 3) { // if in No prediction state
          if (addr == pred_addr) { // if prediction correct
            RPT[entryIdx][3] = 1; // change to Transient state
            RPT[entryIdx][1] = addr; // update the previous address
          } else { // if prediction incorrect
            RPT[entryIdx][2] = addr - prev_addr; // update the stride
            RPT[entryIdx][1] = addr; // update the previous address
          }
        }
      }

      
      
    }
};


class DistancePrefetcher : public PrefetcherInterface {
  private:
    UINT64 prev_addr = 0; // previous miss address
    INT64 prev_dist = 0; // previous distance
    INT64** RPT = new INT64*[64]; // Reference Prediction Table
    INT64 numberOfEntries = 0; // number of entries in RPT
    bool entryFound; // for every prefetch if the entry is found in RPT

  public:
    void prefetch(ADDRINT addr, ADDRINT loadPC) {

      INT64 new_dist = addr - prev_addr; // get the new distance
      entryFound = FALSE; // initially set to false for every prefetch

      for (INT64 k = 0; k < numberOfEntries; k++) { // loop over RPT
        if (RPT[k][0] == new_dist) { // check if the entry found
          entryFound = TRUE; // in train no need to add the entry
          for (int i = 1; i <= aggression; i++) { // prefetch
            if (RPT[k][i] != 0) { // don't need but helps
              UINT64 nextAddr = addr + RPT[k][i]; // get all the predicted addresses
              if (!cache->exists(nextAddr)) {  
                  cache->prefetchFillLine(nextAddr); 
                  prefetches++;
              }
            } 
          }
        }
      }

    }

    void train(ADDRINT addr, ADDRINT loadPC) {
      INT64 new_dist = addr - prev_addr; // get the new distance

      if (entryFound==FALSE) { // add the entry in RPT corresponding to new dist as it is not present
        INT64 entryToReplace = 0; // which entry to replace
        if (numberOfEntries >= 64) { // if RPT is full the replace randomly
          entryToReplace = rand()%64;
        } else { // else add the entry in RPT
          entryToReplace = numberOfEntries;
          numberOfEntries++;
        }
        RPT[entryToReplace] = new INT64[aggression+1]; // initialize the predicted distances
        for (int i=1 ; i <= aggression ; i++) {
            RPT[entryToReplace][i] = 0;
          }
        RPT[entryToReplace][0] = new_dist; // set distance tag
      }

      // newly observed distance must be added as a predicted distance to the RPT entry that refers to the previous distance
      for (INT64 k = 0; k < numberOfEntries; k++) {
        if (RPT[k][0] == prev_dist) { // check if the prev distance entry is present in RPT
          bool pdFull = TRUE; // flag to check if any of the predicted distance is empty
          for (int i=1 ; i <= aggression ; i++) {
            if (RPT[k][i] == 0) {
              RPT[k][i] = new_dist;
              pdFull = FALSE;
              break;
            }
          }

          if (pdFull == TRUE) { // if predicted distance not empty then replace randomly
            int indexToReplace = rand() % aggression+1; 
            RPT[k][indexToReplace] = new_dist;
          }

        }
      }

      prev_addr = addr;
      prev_dist = new_dist;

    }
};


//---------------------------------------------------------------------
//##############################################
/*
 * Your changes here.
 *
 * Put your prefetcher implementation here
 *
 * Example:
 *
 * class StridePrefetcher : public PrefetcherInterface {
 * private:
 *  // set up private members
 *
 * public:
 *  void prefetch(ADDRINT addr, ADDRINT loadPC) {
 *      // Prefetcher implementation
 *  }
 *
 *  void train(ADDRINT addr, ADDRINT loadPC) {
 *      // Training implementation
 *  }
 * };
 *
 * You can modify the functions Load() and Store() where necessary to implement your functionality
 *
 * DIRECTIONS ON USING THE COMMAND LINE ARGUMENT
 *    The string variable "prefetcherName" indicates the name of the prefetcher that is passed as a command line argument (-pref_type)
 *    The integer variable "aggression" indicates the aggressiveness indicated by the command line argument (-aggr)
 *
 * STATS:
 * ***Note that these exist to help you debug your program and produce your graphs
 *  The member function Cache::getSuccessfulPrefs() returns how many of the prefetched block into the cache were actually used. This applies in  the case where no prefetch buffer is used.
 *  The integer variable "prefetches" should count the number of prefetched blocks
 *  The integer variable "accesses" counts the number of memory accesses performed by the program
 *  The integer variable "hits" counts the number of memory accesses that actually hit in either the data cache or the prefetch buffer such that hits = cacheHits + prefHits
 *
 */
//##############################################
//---------------------------------------------------------------------


/* ===================================================================== */

/* Action taken on a load. Load takes 2 arguments:
    addr: the address of the demanded block (in bytes)
    pc: the program counter of the load instruction
*/
void Load(ADDRINT addr, ADDRINT pc)
{
  accesses++;
  loads++;
  if (cache->probeTag(addr)) { // Use the function Cache::probeTag(UINT64) when you are probing the cache after a demand access
    hits++;
  }
  else {
    cache->fillLine(addr); // Use the member function Cache::fillLine(addr) when you fill in the MRU way for demand accesses
    prefetcher->prefetch(addr, pc);
    prefetcher->train(addr, pc);
  }
  if (accesses % checkpoint == 0)  takeCheckPoint();
}

/* ===================================================================== */

//Action taken on a store
void Store(ADDRINT addr, ADDRINT pc)
{
  accesses++;
  stores++;
  if (cache->probeTag(addr))  hits++;
  else cache->fillLine(addr);
  if (accesses % checkpoint == 0) takeCheckPoint();
}

/* ===================================================================== */

// Receives all instructions and takes action if the instruction is a load or a store
// DO NOT MODIFY THIS FUNCTION
void Instruction(INS ins, void * v)
{
  if (INS_IsMemoryRead(ins) && INS_IsStandardMemop(ins)) {
    INS_InsertPredicatedCall(
        ins, IPOINT_BEFORE, (AFUNPTR) Load,
        (IARG_MEMORYREAD_EA), IARG_INST_PTR, IARG_END);
 }
  if ( INS_IsMemoryWrite(ins) && INS_IsStandardMemop(ins))
  {
    INS_InsertPredicatedCall(
      ins, IPOINT_BEFORE,  (AFUNPTR) Store,
      (IARG_MEMORYWRITE_EA), IARG_INST_PTR, IARG_END);
  }
}

/* ===================================================================== */

// Gets called when the program finishes execution
void Fini(int code, VOID * v)
{
    outFile << "The program has completed execution" << endl;
    takeCheckPoint();
    cout << double(hits) / double(accesses) << endl;
    outFile.close();
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{
    PIN_InitSymbols();
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }

    // Initialize stats
    hits = 0;
    accesses = 0;
    prefetches = 0;
    loads = 0;
    stores = 0;
    aggression = KnobAggression.Value();
    sets = KnobCacheSets.Value();
    associativity = KnobAssociativity.Value();
    blockSize =  KnobLineSize.Value();
    prefetcherName = KnobPrefetcherName;

    if (prefetcherName == "none") {
        prefetcher = new NonePrefetcher();
    } else if (prefetcherName == "next_n_lines") {
        prefetcher = new NextNLinePrefetcher();
    } else if (prefetcherName == "stride") {
        // Uncomment when you implement the stride prefetcher
        prefetcher = new StridePrefetcher();
    } else if (prefetcherName == "distance") {
        // Uncomment when you implement the distance prefetcher
        prefetcher = new DistancePrefetcher();
    } else {
        std::cerr << "Error: No such type of prefetcher. Simulation will be terminated." << std::endl;
        std::exit(EXIT_FAILURE);
    }

    // create a data cache
    cache = new Cache(sets, associativity, blockSize);

    outFile.open(KnobOutputFile.Value());
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns

    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
