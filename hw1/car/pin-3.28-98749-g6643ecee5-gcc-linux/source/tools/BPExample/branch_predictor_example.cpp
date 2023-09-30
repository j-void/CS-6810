#include <iostream>
#include <fstream>
#include <cstdlib>
#include "pin.H"
using std::cerr;
using std::endl;
using std::ios;
using std::ofstream;
using std::string;

// Simulation will stop when this number of instructions have been executed
//
#define STOP_INSTR_NUM 1000000000 // 1b instrs

// Simulator heartbeat rate
//
#define SIMULATOR_HEARTBEAT_INSTR_NUM 100000000 // 100m instrs

/* Base branch predictor class */
// You are highly recommended to follow this design when implementing your branch predictors
//
class BranchPredictorInterface {
public:
  //This function returns a prediction for a branch instruction with address branchPC
  virtual bool getPrediction(ADDRINT branchPC) = 0;
  
  //This function updates branch predictor's history with outcome of branch instruction with address branchPC
  virtual void train(ADDRINT branchPC, bool branchWasTaken) = 0;
};

// This is a class which implements always taken branch predictor
class AlwaysTakenBranchPredictor : public BranchPredictorInterface {
public:
  AlwaysTakenBranchPredictor(UINT64 numberOfEntries) {}; //no entries here: always taken branch predictor is the simplest predictor
	virtual bool getPrediction(ADDRINT branchPC) {
		return true; // predict taken
	}
	virtual void train(ADDRINT branchPC, bool branchWasTaken) {} //nothing to do here: always taken branch predictor does not have history
};

//------------------------------------------------------------------------------
//##############################################################################

class LocalBranchPredictor : public BranchPredictorInterface {

  private:
    UINT64 bp_entries; // branch prediction entries
    UINT64 lhrs[128]; // local history registers
    string* pht_array; // pattern history table
   
  public:
    LocalBranchPredictor(UINT64 numberOfEntries) {
      bp_entries = numberOfEntries;
      // Initialize the local history registers to 0
      for (UINT64 i = 0; i < 128; i++) {
        lhrs[i] = 0;
      }
      // intialize the pattern history table same size as branch preditor entries and set initial value to "11"
      pht_array = new string[numberOfEntries];
      for (UINT64 i = 0; i < numberOfEntries; i++) {
        pht_array[i] = "11";
      }
    };
    virtual bool getPrediction(ADDRINT branchPC) {
      // get the lhr address using last 7 bits or branch program counter
      UINT64 lhr_addr = branchPC % 128;
      // use the value in lhr table to get the address of pht
      UINT64 pht_addr = lhrs[lhr_addr];
      // get the value inside the specific entry of pht table
      string pht_value = pht_array[pht_addr];
      // return the decision based on 2 bit branch predictor logic
      if (pht_value == "11" || pht_value == "10") {
        return true;
      } else {
        return false;
      }
    }
    virtual void train(ADDRINT branchPC, bool branchWasTaken) {
      // get the lhr address using last 7 bits or branch program counter
      UINT64 lhr_addr = branchPC % 128;
      // use the value in lhr table to get the address of pht
      UINT64 pht_addr = lhrs[lhr_addr];
      // get the value inside the specific entry of pht table
      string pht_value = pht_array[pht_addr];

      // update the value of pht table based on whether the branch was actually taken and the predicted value in pht table
      if (pht_value=="11") {
        if (branchWasTaken == false) {
            pht_array[pht_addr] = "10";
        }
      } else if (pht_value=="10") {
        if (branchWasTaken == true) {
          pht_array[pht_addr] = "11";
        } else {
          pht_array[pht_addr] = "01";
        }
      } else if (pht_value=="01") {
        if (branchWasTaken == true) {
          pht_array[pht_addr] = "10";
        } else {
          pht_array[pht_addr] = "00";
        }
      } else if (pht_value=="00") {
        if (branchWasTaken == true) {
          pht_array[pht_addr] = "01";
        } else {
          pht_array[pht_addr] = "00";
        }
      }
      // also update the value in lhr table, update the history of last n runs whether branch was predited or not
      UINT64 pht_addr_new;
      if (branchWasTaken == false) {
          pht_addr_new = pht_addr * 2;
      } else {
          pht_addr_new = (pht_addr * 2) + 1;
      }
      // as pht table as size of bp_entries, check and update it accordigly so it doesn't exceed range
      if (pht_addr_new >= bp_entries) {
          pht_addr_new = pht_addr_new - bp_entries;
      }
      lhrs[lhr_addr] = pht_addr_new;
    }
};

class GshareBranchPredictor : public BranchPredictorInterface {

  private:
    UINT64 bp_entries; // branch prediction entries
    UINT64 ghr; // global history register
    string* pht_array; // pattern history table
   
  public:
    GshareBranchPredictor(UINT64 numberOfEntries) {
      bp_entries = numberOfEntries;
      // initialize the global history register to 0
      ghr = 0;
      // intialize the pattern history table same size as branch preditor entries and set initial value to "11"
      pht_array = new string[numberOfEntries];
      for (UINT64 i = 0; i < numberOfEntries; i++) {
        pht_array[i] = "11";
      }
    };
    virtual bool getPrediction(ADDRINT branchPC) {
      // get last n bits or branch program counter based on branch predictor entries
      UINT64 pc_lsb = branchPC % bp_entries;
      // xor the lase n bits of program counter with global history register to get the address on pht table
      UINT64 pht_addr = pc_lsb ^ ghr;
      // get the value inside the specific entry of pht table
      string pht_value = pht_array[pht_addr];
      // return the decision based on 2 bit branch predictor logic
      if (pht_value == "11" || pht_value == "10") {
        return true;
      } else {
        return false;
      }
    }
    virtual void train(ADDRINT branchPC, bool branchWasTaken) {
      // get last n bits or branch program counter based on branch predictor entries
      UINT64 pc_lsb = branchPC % bp_entries;
      // xor the lase n bits of program counter with global history register to get the address on pht table
      UINT64 pht_addr = pc_lsb ^ ghr;
      // get the value inside the specific entry of pht table
      string pht_value = pht_array[pht_addr];

      // update the value of pht table based on whether the branch was actually taken and the predicted value in pht table
      if (pht_value=="11") {
        if (branchWasTaken == false) {
            pht_array[pht_addr] = "10";
        }
      } else if (pht_value=="10") {
        if (branchWasTaken == true) {
          pht_array[pht_addr] = "11";
        } else {
          pht_array[pht_addr] = "01";
        }
      } else if (pht_value=="01") {
        if (branchWasTaken == true) {
          pht_array[pht_addr] = "10";
        } else {
          pht_array[pht_addr] = "00";
        }
      } else if (pht_value=="00") {
        if (branchWasTaken == true) {
          pht_array[pht_addr] = "01";
        } else {
          pht_array[pht_addr] = "00";
        }
      }
      
      // also update the value in global history regsiter, update the history of last n runs whether branch was predited or not
      UINT64 ghr_new;
      if (branchWasTaken == false) {
          ghr_new = ghr * 2;
      } else {
          ghr_new = (ghr * 2) + 1;
      }
      // as pht table as size of bp_entries, check and update it accordigly so it doesn't exceed range
      if (ghr_new >= bp_entries) {
          ghr_new = ghr_new - bp_entries;
      }
      ghr = ghr_new;
    }
};


class TournamentBranchPredictor : public BranchPredictorInterface {

  private:
    UINT64 bp_entries; // branch prediction entries
    string* pht_array; // pattern history table
    LocalBranchPredictor* lb_predictor; // get the instance on Local Branch Predictor implemented above 
    GshareBranchPredictor* gsb_predictor; // get the instance on Gshare Branch Predictor implemented above 
   
  public:
    TournamentBranchPredictor(UINT64 numberOfEntries) {
      bp_entries = numberOfEntries;
      // initialize the Local Branch Predictor
      lb_predictor = new LocalBranchPredictor(numberOfEntries);
      // initialize the Gshare Branch Predictor
      gsb_predictor = new GshareBranchPredictor(numberOfEntries);
      // intialize the pattern history table same size as branch preditor entries and set initial value to "11"
      pht_array = new string[numberOfEntries];
      for (UINT64 i = 0; i < numberOfEntries; i++) {
        pht_array[i] = "11";
      }
    };
    virtual bool getPrediction(ADDRINT branchPC) {
      // get last n bits or branch program counter based on branch predictor entries
      UINT64 pht_addr = branchPC % bp_entries;
      // get the value inside the specific entry of pht table
      string pht_value = pht_array[pht_addr];
      // based on the value inside the pht table decide whether to use local branch predictor or gshare branch predictor
      if (pht_value == "11" || pht_value == "10") {
        return gsb_predictor->getPrediction(branchPC);
      } else {
        return lb_predictor->getPrediction(branchPC);
      }
    }
    virtual void train(ADDRINT branchPC, bool branchWasTaken) {
      // get last n bits or branch program counter based on branch predictor entries
      UINT64 pht_addr = branchPC % bp_entries;
      // get the value inside the specific entry of pht table
      string pht_value = pht_array[pht_addr];
      // get the output of local branch predictor 
      bool lb_pred = lb_predictor->getPrediction(branchPC);
      // get the output of gshare branch predictor 
      bool gsb_pred = gsb_predictor->getPrediction(branchPC);

      // if (lb_pred != branchWasTaken && gsb_pred != branchWasTaken) { // ignore 
      //   return;
      // }

      // update the pht table based on whether the branch was taken and it corresponds to the correct branch predictor used 
      // see report for more detail and flow chart of logic
      if (pht_value=="11") {
        if (gsb_pred != branchWasTaken && lb_pred == branchWasTaken) {
            pht_array[pht_addr] = "10";
        }
      } else if (pht_value=="10") {
        if (gsb_pred == branchWasTaken) {
          pht_array[pht_addr] = "11";
        } else if (gsb_pred != branchWasTaken && lb_pred == branchWasTaken) {
          pht_array[pht_addr] = "01";
        }
      } else if (pht_value=="01") {
        if (lb_pred == branchWasTaken) {
          pht_array[pht_addr] = "00";
        } else if (lb_pred != branchWasTaken && gsb_pred == branchWasTaken) {
          pht_array[pht_addr] = "10";
        }
      } else if (pht_value=="00") {
        if (branchWasTaken == true) {
          pht_array[pht_addr] = "00";
        } else if (lb_pred != branchWasTaken && gsb_pred == branchWasTaken) {
          pht_array[pht_addr] = "01";
        }
      }
      // also train the local and gshare branch predictor at each train iteration
      lb_predictor->train(branchPC, branchWasTaken);
      gsb_predictor->train(branchPC, branchWasTaken);

    }
};

//  * You also need to create an object of branch predictor class in main()
//  * (i.e. at line 193 in the original unmodified version of this file).

//##############################################################################
//------------------------------------------------------------------------------

ofstream OutFile;
BranchPredictorInterface *branchPredictor;

// Define the command line arguments that Pin should accept for this tool
//
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "BP_stats.out", "specify output file name");
KNOB<UINT64> KnobNumberOfEntriesInBranchPredictor(KNOB_MODE_WRITEONCE, "pintool",
    "num_BP_entries", "1024", "specify number of entries in a branch predictor");
KNOB<string> KnobBranchPredictorType(KNOB_MODE_WRITEONCE, "pintool",
    "BP_type", "always_taken", "specify type of branch predictor to be used");

// The running counts of branches, predictions and instructions are kept here
//
static UINT64 iCount                          = 0;
static UINT64 correctPredictionCount          = 0;
static UINT64 conditionalBranchesCount        = 0;
static UINT64 takenBranchesCount              = 0;
static UINT64 notTakenBranchesCount           = 0;
static UINT64 predictedTakenBranchesCount     = 0;
static UINT64 predictedNotTakenBranchesCount  = 0;

VOID docount() {
  // Update instruction counter
  iCount++;
  // Print this message every SIMULATOR_HEARTBEAT_INSTR_NUM executed
  if (iCount % SIMULATOR_HEARTBEAT_INSTR_NUM == 0) {
    std::cerr << "Executed " << iCount << " instructions." << endl;
  }
  // Release control of application if STOP_INSTR_NUM instructions have been executed
  if (iCount == STOP_INSTR_NUM) {
    PIN_Detach();
  }
}



VOID TerminateSimulationHandler(VOID *v) {
  OutFile.setf(ios::showbase);
  // At the end of a simulation, print counters to a file
  OutFile << "Prediction accuracy:\t"            << (double)correctPredictionCount / (double)conditionalBranchesCount << endl
          << "Number of conditional branches:\t" << conditionalBranchesCount                                      << endl
          << "Number of correct predictions:\t"  << correctPredictionCount                                        << endl
          << "Number of taken branches:\t"       << takenBranchesCount                                            << endl
          << "Number of non-taken branches:\t"   << notTakenBranchesCount                                         << endl
          ;
  OutFile.close();

  std::cerr << endl << "PIN has been detached at iCount = " << STOP_INSTR_NUM << endl;
  std::cerr << endl << "Simulation has reached its target point. Terminate simulation." << endl;
  std::cerr << "Prediction accuracy:\t" << (double)correctPredictionCount / (double)conditionalBranchesCount << endl;
  std::exit(EXIT_SUCCESS);
}

//
VOID Fini(int code, VOID * v)
{
  TerminateSimulationHandler(v);
}

// This function is called before every conditional branch is executed
//
static VOID AtConditionalBranch(ADDRINT branchPC, BOOL branchWasTaken) {
  /*
	 * This is the place where the predictor is queried for a prediction and trained
	 */

  // Step 1: make a prediction for the current branch PC
  //
	bool wasPredictedTaken = branchPredictor->getPrediction(branchPC);
  
  // Step 2: train the predictor by passing it the actual branch outcome
  //
	branchPredictor->train(branchPC, branchWasTaken);

  // Count the number of conditional branches executed
  conditionalBranchesCount++;
  
  // Count the number of conditional branches predicted taken and not-taken
  if (wasPredictedTaken) {
    predictedTakenBranchesCount++;
  } else {
    predictedNotTakenBranchesCount++;
  }

  // Count the number of conditional branches actually taken and not-taken
  if (branchWasTaken) {
    takenBranchesCount++;
  } else {
    notTakenBranchesCount++;
  }

  // Count the number of correct predictions
	if (wasPredictedTaken == branchWasTaken)
    correctPredictionCount++;
}

// Pin calls this function every time a new instruction is encountered
// Its purpose is to instrument the benchmark binary so that when 
// instructions are executed there is a callback to count the number of
// executed instructions, and a callback for every conditional branch
// instruction that calls our branch prediction simulator (with the PC
// value and the branch outcome).
//
VOID Instruction(INS ins, VOID *v) {
  // Insert a call before every instruction that simply counts instructions executed
  INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_END);

  // Insert a call before every conditional branch
  if ( INS_IsBranch(ins) && INS_HasFallThrough(ins) ) {
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)AtConditionalBranch, IARG_INST_PTR, IARG_BRANCH_TAKEN, IARG_END);
  }
}

// Print Help Message
INT32 Usage() {
  cerr << "This tool simulates different types of branch predictors" << endl;
  cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
  return -1;
}

int main(int argc, char * argv[]) {
  // Initialize pin
  if (PIN_Init(argc, argv)) return Usage();

  // Create a branch predictor object of requested type
  if (KnobBranchPredictorType.Value() == "always_taken") {
    std::cerr << "Using always taken BP" << std::endl;
    branchPredictor = new AlwaysTakenBranchPredictor(KnobNumberOfEntriesInBranchPredictor.Value());
  }
//------------------------------------------------------------------------------
//##############################################################################
/*
 * Insert your changes below here...
 *
 * In the following cascading if-statements instantiate branch predictor objects
 * using the classes that you have implemented for each of the three types of
 * predictor.
 *
 * The choice of predictor, and the number of entries in its prediction table
 * can be obtained from the command line arguments of this Pin tool using:
 *
 *  KnobNumberOfEntriesInBranchPredictor.Value() 
 *    returns the integer value specified by tool option "-num_BP_entries".
 *
 *  KnobBranchPredictorType.Value() 
 *    returns the value specified by tool option "-BP_type".
 *    The argument of tool option "-BP_type" must be one of the strings: 
 *        "always_taken",  "local",  "gshare",  "tournament"
 *
 *  Please DO NOT CHANGE these strings - they will be used for testing your code
 */
//##############################################################################
//------------------------------------------------------------------------------
  else if (KnobBranchPredictorType.Value() == "local") {
  	 std::cerr << "Using Local BP." << std::endl;
     branchPredictor = new LocalBranchPredictor(KnobNumberOfEntriesInBranchPredictor.Value());
  }
  else if (KnobBranchPredictorType.Value() == "gshare") {
  	 std::cerr << "Using Gshare BP."<< std::endl;
    branchPredictor = new GshareBranchPredictor(KnobNumberOfEntriesInBranchPredictor.Value());
  }
  else if (KnobBranchPredictorType.Value() == "tournament") {
  	 std::cerr << "Using Tournament BP." << std::endl;
    branchPredictor = new TournamentBranchPredictor(KnobNumberOfEntriesInBranchPredictor.Value());
  }
  else {
    std::cerr << "Error: No such type of branch predictor. Simulation will be terminated." << std::endl;
    std::exit(EXIT_FAILURE);
  }

  std::cerr << "The simulation will run " << STOP_INSTR_NUM << " instructions." << std::endl;

  OutFile.open(KnobOutputFile.Value().c_str());

  // Pin calls Instruction() when encountering each new instruction executed
  INS_AddInstrumentFunction(Instruction, 0);

  // Function to be called if the program finishes before it completes 10b instructions
  PIN_AddFiniFunction(Fini, 0);

  // Callback functions to invoke before Pin releases control of the application
  PIN_AddDetachFunction(TerminateSimulationHandler, 0);

  // Start the benchmark program. This call never returns...
  PIN_StartProgram();

  return 0;
}