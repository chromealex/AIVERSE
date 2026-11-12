#pragma once
#include <cstddef>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::lib::strategy {
enum class PuzzleOp:uint8_t {Literal,Copy,Add,Subtract,Multiply,Divide,Load,Store,Jump,JumpZero,Read,Write,Yield,Halt};
struct PuzzleInstruction {PuzzleOp op=PuzzleOp::Halt;uint8_t dst=0,src=0;int32_t immediate=0;};
struct PuzzleProgram {std::vector<PuzzleInstruction> code;uint32_t memoryWords=64;};
enum class MachineFault:uint8_t {None,Arithmetic,Address,ProgramCounter,StepLimit};
struct MachineState {
    std::array<int32_t,8> registers{};
    std::vector<int32_t> memory,input,output;
    uint32_t pc=0,inputCursor=0;
    uint64_t steps=0;
    bool halted=false;
    MachineFault fault=MachineFault::None;
    bool operator==(const MachineState&)const=default;
};
enum class MachineStop {BudgetExceeded,WaitingInput,OutputFull,Yielded,Halted,Fault,Invalid};
inline bool valid(const PuzzleProgram& p) {
    if(p.code.empty()||p.code.size()>4096||!p.memoryWords||p.memoryWords>4096)return false;
    for(const auto& i:p.code){
        if(i.op>PuzzleOp::Halt||i.dst>=8||i.src>=8)return false;
        if((i.op==PuzzleOp::Jump||i.op==PuzzleOp::JumpZero)&&(i.immediate<0||size_t(i.immediate)>=p.code.size()))return false;
    }
    return true;
}
inline bool valid(const MachineState& s) {
    return !s.memory.empty()&&s.memory.size()<=4096&&s.input.size()<=4096&&s.output.size()<=4096&&s.inputCursor<=s.input.size()&&s.pc<=4096&&s.steps<=1000000000&&s.fault<=MachineFault::StepLimit&&!(s.halted&&s.fault!=MachineFault::None);
}
inline bool compatible(const PuzzleProgram& p,const MachineState& s){return valid(p)&&valid(s)&&p.memoryWords==s.memory.size()&&s.pc<=p.code.size();}
inline MachineState beginMachine(const PuzzleProgram& p){MachineState s;if(valid(p))s.memory.resize(p.memoryWords);return s;}
inline bool machineInput(MachineState& s,const std::vector<int32_t>& input) {
    if(!valid(s)||s.halted||s.fault!=MachineFault::None||input.size()>4096-(s.input.size()-s.inputCursor))return false;
    // Retain unread values only; no input may be overwritten while awaiting it.
    s.input.erase(s.input.begin(),s.input.begin()+s.inputCursor);s.inputCursor=0;s.input.insert(s.input.end(),input.begin(),input.end());return true;
}
// A deterministic instruction budget, never wall-clock time. Wait/output-full do not
// consume an instruction. Arithmetic/address faults retain operands and offending PC.
inline MachineStop runMachine(const PuzzleProgram& p,MachineState& s,uint32_t budget) {
    if(!compatible(p,s)||budget==0||budget>1000000)return MachineStop::Invalid;
    if(s.halted)return MachineStop::Halted;
    if(s.fault!=MachineFault::None)return MachineStop::Fault;
    for(uint32_t count=0;count<budget;++count){
        if(s.pc==p.code.size()){s.fault=MachineFault::ProgramCounter;return MachineStop::Fault;}
        if(s.steps==1000000000){s.fault=MachineFault::StepLimit;return MachineStop::Fault;}
        const auto i=p.code[s.pc];auto& dst=s.registers[i.dst];auto src=s.registers[i.src];
        if(i.op==PuzzleOp::Read&&s.inputCursor==s.input.size())return MachineStop::WaitingInput;
        if(i.op==PuzzleOp::Write&&s.output.size()==4096)return MachineStop::OutputFull;
        int64_t value=dst;uint32_t next=s.pc+1;bool write=false;
        switch(i.op){
        case PuzzleOp::Literal:value=i.immediate;write=true;break;
        case PuzzleOp::Copy:value=src;write=true;break;
        case PuzzleOp::Add:value=int64_t(dst)+src;write=true;break;
        case PuzzleOp::Subtract:value=int64_t(dst)-src;write=true;break;
        case PuzzleOp::Multiply:value=int64_t(dst)*src;write=true;break;
        case PuzzleOp::Divide:
            if(!src||(dst==INT32_MIN&&src==-1)){s.fault=MachineFault::Arithmetic;return MachineStop::Fault;}
            value=dst/src;write=true;break;
        case PuzzleOp::Load:
            if(src<0||size_t(src)>=s.memory.size()){s.fault=MachineFault::Address;return MachineStop::Fault;}
            value=s.memory[size_t(src)];write=true;break;
        case PuzzleOp::Store:
            if(dst<0||size_t(dst)>=s.memory.size()){s.fault=MachineFault::Address;return MachineStop::Fault;}
            s.memory[size_t(dst)]=src;break;
        case PuzzleOp::Jump:next=uint32_t(i.immediate);break;
        case PuzzleOp::JumpZero:if(!dst)next=uint32_t(i.immediate);break;
        case PuzzleOp::Read:value=s.input[s.inputCursor++];write=true;break;
        case PuzzleOp::Write:s.output.push_back(dst);break;
        case PuzzleOp::Yield:break;
        case PuzzleOp::Halt:s.halted=true;break;
        }
        if(write){
            if(value<INT32_MIN||value>INT32_MAX){s.fault=MachineFault::Arithmetic;return MachineStop::Fault;}
            dst=int32_t(value);
        }
        s.pc=next;++s.steps;
        if(s.halted)return MachineStop::Halted;
        if(i.op==PuzzleOp::Yield)return MachineStop::Yielded;
    }
    return MachineStop::BudgetExceeded;
}
}
