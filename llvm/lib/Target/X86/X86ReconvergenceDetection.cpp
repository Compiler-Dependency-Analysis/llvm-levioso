//===- X86 ReconvergenceDetection.cpp: ---------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
//  Author: Ali Hajiabadi <ali.hajiabadi@u.nus.edu> 
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of X86 branch reconvergence 
// detection. This implemeneattion also finds control and data dependent
// instructions of branches.
//
// Note: The immediate post-dominator of a basic block is its reconvergence point
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/CodeGen/MachineScheduler.h" 
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleDFS.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/Analysis/AliasAnalysis.h"

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DebugInfo.h"

#include <set>
#include <queue>
#include <vector>
#include <iostream>
#include <fstream>
#include <stack>
#include <map>
#include <string>
#include <sstream>

using namespace llvm;

using namespace std;

#define DEBUG_TYPE "x86-reconvergence-detection"

#define X86_RECONVERGENCE_DETECTION_PASS_NAME "X86 reconvergence detection pass"

static cl::opt<bool> PrintDependency("print-dep", cl::init(false),
                        cl::Hidden,
                        cl::desc("Enable this option to print detailed dependency information of beanches."));

std::ofstream info("levioso_dependency_info.txt");

namespace
{

class X86ReconvergenceDetection : public MachineFunctionPass 
{
    public:

    static char ID;

    //These are sets for a specific branch (cleared at each iteration, processing diffrent branches)
    std::set<MachineInstr*> control_dependents; //Control dependent instructions
    std::set<MachineInstr*> data_dependents; //Data dependent insttructions
        
    //These are for the entire function
    std::set<MachineInstr*> total_insts;
    std::set<MachineInstr*> total_dependent_insts;
    std::set<MachineInstr*> total_independent_insts;
    std::set<MachineInstr*> total_actual_insts;

    X86ReconvergenceDetection() : MachineFunctionPass(ID)
    {
        initializeX86ReconvergenceDetectionPass(*PassRegistry::getPassRegistry());
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
        AU.addRequired<MachinePostDominatorTree>();
        AU.addRequired<AAResultsWrapperPass>();
        MachineFunctionPass::getAnalysisUsage(AU);
    }

    MachineInstr* getNextInstr(MachineInstr* MI);
    MachineInstr* getPrevInstr(MachineInstr* MI);

    bool runOnMachineFunction(MachineFunction &MF) override;

    StringRef getPassName() const override { return X86_RECONVERGENCE_DETECTION_PASS_NAME; }
};

char X86ReconvergenceDetection::ID = 0;

//This fuction returns the next actual instruction within the same MBB
//      if this is the last actual instruction in the MBB then return nullptr
//      -- Note: getNextNode() throws weird runtime errors for some SPEC apps
MachineInstr* X86ReconvergenceDetection::getNextInstr (MachineInstr* MI){
    MachineInstr* nextInstr = nullptr;

    assert(MI->isActualInstr() && "Please only pass actual instructions.");

    MachineBasicBlock* parentBB = MI->getParent();
    assert(parentBB && "Passing an instruction with invalid MBB.");

    if (parentBB){
        MachineInstr* lastInstr = &(*(parentBB->getLastActualInstr()));

        if (MI->InstructionNumber < lastInstr->InstructionNumber){
            auto inst_it = parentBB->begin(); //inst_it will be the next instruction iterator right after MI
            
            for (auto it = parentBB->begin(), eit = parentBB->end(); it != eit; it++){
                inst_it = it;
                if ((&(*it))->InstructionNumber > MI->InstructionNumber)
                    break;
            }

            while (true){
                if (!(&(*inst_it))->isActualInstr()){
                    inst_it++;
                    continue;
                }

                nextInstr = &(*inst_it);
                break;
            }
        }
    }

    return nextInstr;
}

//This function returns the previous actual instruction
//  the output will be nullptr if MI is the first actual instruction in BB
MachineInstr* X86ReconvergenceDetection::getPrevInstr (MachineInstr* MI){
    MachineInstr*  prev_inst = nullptr;

    assert(MI->isActualInstr() && "Please only pass actual instructions.");

    MachineBasicBlock* parentBB = MI->getParent();
    assert(parentBB && "Passing an instruction with invalid MBB.");

    for (auto it = parentBB->begin(), eit = parentBB->end(); it != eit; it++){
        if (!(&(*it))->isActualInstr()) continue;

        MachineInstr* current_inst = &(*it);

        if (current_inst->InstructionNumber < MI->InstructionNumber){
            prev_inst = current_inst;
            continue;
        }
        else if (current_inst->InstructionNumber == MI->InstructionNumber){
            break;
        }
        else {
            assert("Skipped the input instruction!");
        }
    }

    return prev_inst;
}

bool X86ReconvergenceDetection::runOnMachineFunction (MachineFunction &MF)
{   
    unsigned functionNumber = 10000 + MF.getFunctionNumber();
    if (PrintDependency){
        errs() << "-------------------------------------------------------------------------------------------------------------------\n";
        errs() << "[X86 Reconvergence Detection] Machine Function: " << MF.getName().str() << ", Function Number: " << functionNumber << "\n";
        errs() << "-------------------------------------------------------------------------------------------------------------------\n";
    }

    //Assigning a number to instructions within the function
    //  Note: we embed instruction number and function number within debug info (later used for exctracting PCs)
    unsigned instructionNumber = 0;
    for (MachineBasicBlock &MBB : MF){

        if (PrintDependency){
            errs() << "===================\n";
            errs() << "BB Number: " << MBB.getNumber() << "\n";
            errs() << "===================\n";
        }

        for (MachineInstr &inst : MBB){
            if(inst.isActualInstr()){
                inst.InstructionNumber = instructionNumber;
                total_actual_insts.insert(&inst);
                instructionNumber++;

                if (PrintDependency){
                    errs() << "[InstructionNumber: " << inst.InstructionNumber << "] ";
                    inst.print(errs());
                }

                DebugLoc loc = DebugLoc::get(inst.InstructionNumber, functionNumber, MF.getFunction().getSubprogram());         
                inst.setDebugLoc(loc);
            }
        }
    }

    //post-dominator tree analysis (used for reconvergence detection)
    MachinePostDominatorTree &MPT = getAnalysis<MachinePostDominatorTree>();

    for(MachineBasicBlock &MBB : MF)
    {
        // reconvergence point of a branch (basic block) is the immediate post-dominator in the CFG
        auto reconvergence_point = MPT.getNode(&MBB)->getIDom()->getBlock();

        for (MachineInstr &inst : MBB)
        {
            total_insts.insert(&inst);

            // we only consider conditional branches with a valid reconvergence point
            if (inst.isConditionalBranch() && reconvergence_point != nullptr)
            {
                inst.reconvergence_point = &(*reconvergence_point);

                // finding the dependent instructions of this branch
                // 1. all instructions in the reconvergence window --> control_dependents
                // 2. all instructions data dependent to instructions in the reconvergence window --> data_dependents
                control_dependents.clear();
                data_dependents.clear();

                //First Step: finding control_dependents (Control Dependent Instructions)
                //Solution: We should just trace the successors of the basic block starting from the branch
                //          and reaching the reconvergence point.
                std::set<MachineBasicBlock*> reachable_blocks; //this set will include all basic blocks reachable 
                                                               // between the branch and its reconvergence point
                std::queue<MachineBasicBlock*> bb_queue;
                // initializing bb_queue (working set) with the sucessors of the current BB
                for (auto _succ_ = MBB.succ_begin(), _succ_end_ = MBB.succ_end(); _succ_ != _succ_end_; _succ_++){
                    MachineBasicBlock* _tmp_bb_ = dyn_cast<MachineBasicBlock>(*_succ_);
                    if (_tmp_bb_->getNumber() != reconvergence_point->getNumber())
                        bb_queue.push(_tmp_bb_);
                }

                std::vector<int> visited_bb_numbers; //tracks the numbers of visited basic blocks
                while (!bb_queue.empty())
                {
                    MachineBasicBlock* temp_bb = bb_queue.front();
                    bb_queue.pop();

                    for (auto pit = temp_bb->succ_begin(), pet = temp_bb->succ_end(); pit != pet; ++pit)
                    {
                        auto ptr = find(visited_bb_numbers.begin(), visited_bb_numbers.end(), (*pit)->getNumber());
                        if ((*pit)->getNumber() != reconvergence_point->getNumber()
                            && ptr == visited_bb_numbers.end())
                        {
                            reachable_blocks.insert(dyn_cast<MachineBasicBlock>(*pit));
                            bb_queue.push(dyn_cast<MachineBasicBlock>(*pit));
                            visited_bb_numbers.push_back((*pit)->getNumber());
                        }
                    }
                }

                // adding all instructions of reachable basic blocks to the control_dependents
                for (auto it = reachable_blocks.begin(), eit = reachable_blocks.end(); it != eit; it++)
                {
                    for (auto inst_it = (*it)->begin(), inst_eit = (*it)->end(); inst_it != inst_eit; inst_it++)
                    {
                        control_dependents.insert(&(*inst_it));
                    }
                }

                //Second Step: finding the data_dependents (Data Dependent Instructions)
                //Solution: We use a DFS search to detect all instructions that can be possibly data dependent
                //          on the branch outcome
                std::queue<MachineInstr*> inst_queue;
                std::set<MachineInstr*> processed_insts;

                // adding the branch and its test/compare instruction first and then all control dependent instructions
                inst_queue.push(&inst);
                MachineInstr* prev_compare = getPrevInstr(&inst);
                if (prev_compare) inst_queue.push(prev_compare);
                for (auto it = control_dependents.begin(), eit = control_dependents.end(); it != eit; it++){
                    if (!(*it)->isActualInstr()) continue;
                    inst_queue.push(*it);
                }

                // depth-first search to detect data dependents
                while (!inst_queue.empty()){
                    MachineInstr* Inst = inst_queue.front();
                    inst_queue.pop();

                    auto itr = processed_insts.find(Inst);
                    if (itr != processed_insts.end())
                        continue; //continue if the instruction has been already processed

                    processed_insts.insert(Inst);

                    // adding direct data dependents of the porcessing instruction
                    for (auto dit = Inst->data_dependent.begin(), edit = Inst->data_dependent.end();
                        dit != edit; dit++)
                    {
                        if (!(*dit)->isActualInstr()) continue;

                        if (total_actual_insts.find(*dit) == total_actual_insts.end()) continue;

                        data_dependents.insert(*dit);
                        inst_queue.push(*dit);

                        // we need to separately add branches of compare/test insts in x86

                        MachineInstr *_next_inst = getNextInstr(*dit);
                        if (_next_inst != nullptr){
                            auto found_next = processed_insts.find(_next_inst);
                            if (_next_inst->isConditionalBranch() && found_next == processed_insts.end()){
                                data_dependents.insert(_next_inst);
                                inst_queue.push(_next_inst);
                                // processed_insts.insert(_next_inst);
                            }
                        }
                    }
                }

                //Third Step: put all these instructions in one set
                inst.dependent_insts.clear();
                for (auto it = control_dependents.begin(), eit = control_dependents.end(); it != eit; it++)
                {
                    if (!((*it)->isDebugInstr())){
                        inst.dependent_insts.insert(*it);
                        total_dependent_insts.insert(*it);
                    }
                }     
                for (auto it = data_dependents.begin(), eit = data_dependents.end(); it != eit; it++)
                {
                    if ((*it)->isActualInstr()){
                        inst.dependent_insts.insert(*it);
                        total_dependent_insts.insert(*it);
                    }     
                }
            }
        }
    }

    // Final step: dumping the dependency information in a file
    for (MachineBasicBlock &MBB : MF){
        auto bb = MPT.getNode(&MBB)->getIDom()->getBlock();
        for (MachineInstr &inst : MBB){
            if (inst.isConditionalBranch() && bb != nullptr){
                if (PrintDependency){
                    errs() << "**** RECONVERGENCE INFO ****\n";
                    errs() << "Branch: " << inst.InstructionNumber << " (BB: " << MBB.getNumber() << ", F: " << functionNumber << ")\n";
                    errs() << "Reconvergence Point: " << (inst.reconvergence_point)->getNumber() << "\n";
                    errs() << "\nDependent Instructions:\n";
                }

                if (PrintDependency){
                    for (auto it = inst.dependent_insts.begin(), eit = inst.dependent_insts.end(); it != eit; it++){
                        errs() << "     Inst: " << (*it)->InstructionNumber << ", BB: " << (*it)->getParent()->getNumber() <<
                            ", F: " << ((*it)->getParent()->getParent()->getFunctionNumber() + 10000) << "\n";
                        (*it)->print(errs());
                        errs() << "\n";
                    }
                }

                //this is the target branch:
                info << "* " << inst.InstructionNumber << "  " << functionNumber << "\n";

                // dumping dependents of the target branch
                for (auto it = inst.dependent_insts.begin(), eit = inst.dependent_insts.end(); it != eit; it++){
                    if (!((*it)->isDebugInstr())){
                        info << (*it)->InstructionNumber << " " << functionNumber << "\n";
                    }
                }
                info << "---\n";

                if (PrintDependency){
                    errs() << "****************************\n";
                }
            }
        }
    }

    return false;
}

} // end of anonymouse namespace

INITIALIZE_PASS(X86ReconvergenceDetection, "x86-reconvergence-detection",
    X86_RECONVERGENCE_DETECTION_PASS_NAME,
    true, // is CFG only?
    true  // is analysis?
)

namespace llvm {

FunctionPass *createX86ReconvergenceDetectionPass() { return new X86ReconvergenceDetection(); }

}
