// Lower arithmetic, never representation, of LLVM x86_fp80 to pointer-only
// SoftFloat ABI.
#include <functional>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/Local.h>
#include <unordered_map>
#include <vector>
using namespace llvm;
static void fail(const Instruction &instruction) {
  errs() << "Unsupported FP80 operation (no precision downgrade): "
         << instruction << "\n";
  exit(1);
}
static bool f80(Type *type) { return type->isX86_FP80Ty(); }
static bool contains80(Type *type) {
  if (f80(type))
    return true;
  if (auto *array = dyn_cast<ArrayType>(type))
    return contains80(array->getElementType());
  if (auto *vector = dyn_cast<VectorType>(type))
    return contains80(vector->getElementType());
  if (auto *structure = dyn_cast<StructType>(type))
    for (auto *element : structure->elements())
      if (contains80(element))
        return true;
  return false;
}
static StringRef callName(Value *value) {
  auto *call = dyn_cast<CallInst>(value);
  return call && call->getCalledFunction()
             ? call->getCalledFunction()->getName()
             : StringRef();
}
struct Lower {
  Module &module;
  LLVMContext &context;
  unsigned count = 0;
  bool remillControl = false;
  uint64_t controlOffset = 0;
  Value *slot(IRBuilder<> &builder, Value *value) {
    IRBuilder<> entry(&*builder.GetInsertBlock()
                            ->getParent()
                            ->getEntryBlock()
                            .getFirstInsertionPt());
    auto *pointer =
        entry.CreateAlloca(ArrayType::get(Type::getInt8Ty(context), 16));
    pointer->setAlignment(Align(16));
    if (value) {
      auto *store = builder.CreateStore(value, pointer);
      store->setAlignment(Align(2));
    }
    return pointer;
  }
  Value *call(IRBuilder<> &builder, StringRef name, Type *result,
              ArrayRef<Value *> args) {
    if (remillControl && name != "dcp_fp80_set_control") {
      auto *function = builder.GetInsertBlock()->getParent();
      if (!function->getName().starts_with("sub_") ||
          function->arg_size() != 3 ||
          !function->getArg(0)->getType()->isPointerTy()) {
        errs() << "Remill control hook requires a three-argument translated "
                  "trace\n";
        exit(1);
      }
      auto *address =
          builder.CreateGEP(builder.getInt8Ty(), function->getArg(0),
                            builder.getInt64(controlOffset));
      auto *control = builder.CreateLoad(builder.getInt16Ty(), address);
      control->setAlignment(Align(2));
      builder.CreateCall(module.getOrInsertFunction(
                             "dcp_fp80_set_control",
                             FunctionType::get(builder.getVoidTy(),
                                               {builder.getInt16Ty()}, false)),
                         {control});
    }
    std::vector<Type *> types;
    for (auto *arg : args)
      types.push_back(arg->getType());
    return builder.CreateCall(
        module.getOrInsertFunction(name,
                                   FunctionType::get(result, types, false)),
        args);
  }
  Value *result(IRBuilder<> &builder, StringRef name,
                ArrayRef<Value *> inputs) {
    auto *out = slot(builder, nullptr);
    std::vector<Value *> args{out};
    args.insert(args.end(), inputs.begin(), inputs.end());
    call(builder, name, Type::getVoidTy(context), args);
    auto *load = builder.CreateLoad(Type::getX86_FP80Ty(context), out);
    load->setAlignment(Align(2));
    return load;
  }
  void replace(Instruction *old, Value *value) {
    old->replaceAllUsesWith(value);
    old->eraseFromParent();
    ++count;
  }
  Value *i32(unsigned value) {
    return ConstantInt::get(Type::getInt32Ty(context), value);
  }
  void scales() {
    std::vector<BinaryOperator *> candidates;
    for (auto &function : module)
      for (auto &instruction : instructions(function))
        if (instruction.getOpcode() == Instruction::FMul &&
            f80(instruction.getType()))
          candidates.push_back(cast<BinaryOperator>(&instruction));
    for (auto *multiply : candidates)
      for (unsigned index = 0; index < 2; ++index) {
        auto *power = dyn_cast<CallInst>(multiply->getOperand(index));
        if (!power ||
            (callName(power) != "exp2l" &&
             callName(power) != "llvm.exp2.f80") ||
            power->arg_size() != 1)
          continue;
        auto *trunc = dyn_cast<CallInst>(power->getArgOperand(0));
        if (!trunc || callName(trunc) != "llvm.trunc.f80")
          continue;
        IRBuilder<> builder(multiply);
        auto *value = result(builder, "dcp_fp80_binary",
                             {slot(builder, multiply->getOperand(1 - index)),
                              slot(builder, trunc->getArgOperand(0)), i32(4)});
        replace(multiply, value);
        if (power->use_empty()) {
          power->eraseFromParent();
          if (trunc->use_empty())
            trunc->eraseFromParent();
        }
        break;
      }
  }
  void operations() {
    for (auto &function : module) {
      std::vector<Instruction *> work;
      for (auto &instruction : instructions(function))
        work.push_back(&instruction);
      for (auto *instruction : work) {
        if (instruction->getType()->isVectorTy() &&
            f80(instruction->getType()->getScalarType()))
          fail(*instruction);
        bool touches = f80(instruction->getType());
        for (auto &operand : instruction->operands()) {
          if (operand->getType()->isVectorTy() &&
              f80(operand->getType()->getScalarType()))
            fail(*instruction);
          touches |= f80(operand->getType());
        }
        if (!touches)
          continue;
        IRBuilder<> builder(instruction);
        Value *value = nullptr;
        switch (instruction->getOpcode()) {
        case Instruction::FAdd:
        case Instruction::FSub:
        case Instruction::FMul:
        case Instruction::FDiv: {
          unsigned op = instruction->getOpcode() == Instruction::FAdd   ? 0
                        : instruction->getOpcode() == Instruction::FSub ? 1
                        : instruction->getOpcode() == Instruction::FMul ? 2
                                                                        : 3;
          value = result(builder, "dcp_fp80_binary",
                         {slot(builder, instruction->getOperand(0)),
                          slot(builder, instruction->getOperand(1)), i32(op)});
          break;
        }
        case Instruction::FNeg:
          value = result(builder, "dcp_fp80_unary",
                         {slot(builder, instruction->getOperand(0)), i32(0)});
          break;
        case Instruction::SIToFP:
        case Instruction::UIToFP: {
          bool sign = instruction->getOpcode() == Instruction::SIToFP;
          if (!instruction->getOperand(0)->getType()->isIntegerTy() ||
              instruction->getOperand(0)->getType()->getIntegerBitWidth() > 64)
            fail(*instruction);
          auto *input =
              sign ? builder.CreateSExtOrTrunc(instruction->getOperand(0),
                                               builder.getInt64Ty())
                   : builder.CreateZExtOrTrunc(instruction->getOperand(0),
                                               builder.getInt64Ty());
          value =
              result(builder, sign ? "dcp_fp80_from_i64" : "dcp_fp80_from_u64",
                     {input});
          break;
        }
        case Instruction::FPExt: {
          auto *input = instruction->getOperand(0);
          bool single = input->getType()->isFloatTy();
          if (!single && !input->getType()->isDoubleTy())
            fail(*instruction);
          value = result(
              builder, single ? "dcp_fp80_from_f32" : "dcp_fp80_from_f64",
              {builder.CreateBitCast(input, single ? builder.getInt32Ty()
                                                   : builder.getInt64Ty())});
          break;
        }
        case Instruction::FPTrunc: {
          bool single = instruction->getType()->isFloatTy();
          if (!single && !instruction->getType()->isDoubleTy())
            fail(*instruction);
          auto *bits =
              call(builder, single ? "dcp_fp80_to_f32" : "dcp_fp80_to_f64",
                   single ? builder.getInt32Ty() : builder.getInt64Ty(),
                   {slot(builder, instruction->getOperand(0))});
          value = builder.CreateBitCast(bits, instruction->getType());
          break;
        }
        case Instruction::FPToSI:
        case Instruction::FPToUI: {
          if (instruction->getType()->getIntegerBitWidth() > 64)
            fail(*instruction);
          auto *integer =
              call(builder, "dcp_fp80_to_int", builder.getInt64Ty(),
                   {slot(builder, instruction->getOperand(0)),
                    i32(instruction->getOpcode() == Instruction::FPToUI)});
          value = builder.CreateTruncOrBitCast(integer, instruction->getType());
          break;
        }
        case Instruction::FCmp: {
          auto *comparison = cast<FCmpInst>(instruction);
          auto *integer =
              call(builder, "dcp_fp80_compare", builder.getInt32Ty(),
                   {slot(builder, comparison->getOperand(0)),
                    slot(builder, comparison->getOperand(1)),
                    i32(comparison->getPredicate())});
          value = builder.CreateICmpNE(integer, builder.getInt32(0));
          break;
        }
        case Instruction::Call: {
          auto name = callName(instruction);
          int operation = -1;
          if (name == "llvm.fabs.f80")
            operation = 1;
          else if (name == "llvm.sqrt.f80" || name == "sqrtl")
            operation = 2;
          else if (name == "llvm.trunc.f80" || name == "truncl")
            operation = 3;
          else if (name == "llvm.floor.f80" || name == "floorl")
            operation = 4;
          else if (name == "llvm.ceil.f80" || name == "ceill")
            operation = 5;
          else if (name == "llvm.roundeven.f80")
            operation = 6;
          else if (name == "llvm.round.f80" || name == "roundl")
            operation = 7;
          else if (name == "llvm.rint.f80" || name == "rintl")
            operation = 8;
          else if (name == "llvm.nearbyint.f80" || name == "nearbyintl")
            operation = 9;
          if (operation < 0 || cast<CallInst>(instruction)->arg_size() != 1)
            fail(*instruction);
          value = result(
              builder, "dcp_fp80_unary",
              {slot(builder, cast<CallInst>(instruction)->getArgOperand(0)),
               i32(operation)});
          break;
        }
        case Instruction::Load:
        case Instruction::Store:
        case Instruction::BitCast:
        case Instruction::PHI:
        case Instruction::Select:
        case Instruction::Freeze:
          continue;
        default:
          fail(*instruction);
        }
        replace(instruction, value);
      }
    }
  }
  void storage() {
    // AArch64 accepts f80 loads/stores but may legalize an unanchored copy
    // as f32. Eliminate every executable f80 type, retaining exact i80 bits.
    auto *bits = Type::getIntNTy(context, 80);
    for (auto &function : module) {
      std::vector<Instruction *> work, erase;
      std::unordered_map<Value *, Value *> mapped;
      for (auto &instruction : instructions(function))
        work.push_back(&instruction);
      for (auto *instruction : work)
        if (auto *phi = dyn_cast<PHINode>(instruction);
            phi && f80(phi->getType())) {
          mapped[phi] = PHINode::Create(bits, phi->getNumIncomingValues(), "",
                                        phi->getIterator());
        }
      std::function<Value *(Value *)> convert = [&](Value *value) -> Value * {
        if (auto found = mapped.find(value); found != mapped.end())
          return found->second;
        if (auto *constant = dyn_cast<ConstantFP>(value))
          return ConstantInt::get(context,
                                  constant->getValueAPF().bitcastToAPInt());
        if (isa<PoisonValue>(value))
          return PoisonValue::get(bits);
        if (isa<UndefValue>(value))
          return UndefValue::get(bits);
        auto *instruction = dyn_cast<Instruction>(value);
        if (!instruction) {
          errs() << "Unsupported FP80 storage value\n";
          exit(1);
        }
        IRBuilder<> builder(instruction);
        Value *converted = nullptr;
        if (auto *load = dyn_cast<LoadInst>(instruction)) {
          auto *replacement =
              builder.CreateLoad(bits, load->getPointerOperand());
          replacement->setAlignment(load->getAlign());
          replacement->setVolatile(load->isVolatile());
          converted = replacement;
          if (load->isAtomic())
            replacement->setAtomic(load->getOrdering(), load->getSyncScopeID());
        } else if (auto *cast = dyn_cast<BitCastInst>(instruction)) {
          auto *input = cast->getOperand(0);
          converted = input->getType() == bits
                          ? input
                          : builder.CreateBitCast(input, bits);
        } else if (auto *select = dyn_cast<SelectInst>(instruction))
          converted = builder.CreateSelect(select->getCondition(),
                                           convert(select->getTrueValue()),
                                           convert(select->getFalseValue()));
        else if (auto *freeze = dyn_cast<FreezeInst>(instruction))
          converted = builder.CreateFreeze(convert(freeze->getOperand(0)));
        else
          fail(*instruction);
        mapped[value] = converted;
        return converted;
      };
      for (auto *instruction : work) {
        if (auto *alloca = dyn_cast<AllocaInst>(instruction);
            alloca && f80(alloca->getAllocatedType()))
          alloca->setAllocatedType(bits);
        if (auto *gep = dyn_cast<GetElementPtrInst>(instruction);
            gep && f80(gep->getSourceElementType()))
          gep->setSourceElementType(bits);
        if (auto *store = dyn_cast<StoreInst>(instruction);
            store && f80(store->getValueOperand()->getType())) {
          IRBuilder<> builder(store);
          auto *replacement = builder.CreateStore(
              convert(store->getValueOperand()), store->getPointerOperand());
          replacement->setAlignment(store->getAlign());
          replacement->setVolatile(store->isVolatile());
          if (store->isAtomic())
            replacement->setAtomic(store->getOrdering(),
                                   store->getSyncScopeID());
          erase.push_back(store);
          continue;
        }
        if (auto *cast = dyn_cast<BitCastInst>(instruction);
            cast && f80(cast->getOperand(0)->getType())) {
          IRBuilder<> builder(cast);
          auto *converted = convert(cast->getOperand(0));
          if (converted->getType() != cast->getType())
            converted = builder.CreateBitCast(converted, cast->getType());
          cast->replaceAllUsesWith(converted);
          erase.push_back(cast);
          continue;
        }
        if (f80(instruction->getType())) {
          if (auto *phi = dyn_cast<PHINode>(instruction)) {
            auto *replacement = cast<PHINode>(mapped.at(phi));
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
              replacement->addIncoming(convert(phi->getIncomingValue(i)),
                                       phi->getIncomingBlock(i));
          } else
            convert(instruction);
          erase.push_back(instruction);
        }
      }
      for (auto *instruction : erase)
        instruction->dropAllReferences();
      for (auto *instruction : erase)
        instruction->eraseFromParent();
    }
    std::vector<Function *> dead;
    for (auto &function : module)
      if (function.isDeclaration() && function.use_empty() &&
          f80(function.getReturnType()))
        dead.push_back(&function);
    for (auto *function : dead)
      function->eraseFromParent();
    for (auto &global : module.globals())
      if (contains80(global.getValueType())) {
        errs() << "Unsupported aggregate/global FP80 storage: " << global
               << "\n";
        exit(1);
      }
    for (auto &function : module)
      for (auto &instruction : instructions(function)) {
        if (auto *alloca = dyn_cast<AllocaInst>(&instruction);
            alloca && contains80(alloca->getAllocatedType()))
          fail(instruction);
        if (auto *gep = dyn_cast<GetElementPtrInst>(&instruction);
            gep && contains80(gep->getSourceElementType()))
          fail(instruction);
        if (contains80(instruction.getType()))
          fail(instruction);
        for (auto &operand : instruction.operands())
          if (contains80(operand->getType()))
            fail(instruction);
      }
  }
};
int main(int argc, char **argv) {
  if (argc < 3 || argc > 4 ||
      (argc == 4 && StringRef(argv[3]) != "--remill-control")) {
    errs() << "Usage: fp80-lower input.ll output.ll [--remill-control]\n";
    return 2;
  }
  LLVMContext context;
  SMDiagnostic diagnostic;
  auto module = parseIRFile(argv[1], diagnostic, context);
  if (!module) {
    diagnostic.print(argv[0], errs());
    return 1;
  }
  for (auto &function : *module)
    if (!function.isDeclaration()) {
      if (f80(function.getReturnType())) {
        errs() << "FP80 function return ABI is unsupported\n";
        return 1;
      }
      for (auto &argument : function.args())
        if (f80(argument.getType())) {
          errs() << "FP80 function argument ABI is unsupported\n";
          return 1;
        }
    }
  Lower lower{*module, context};
  if (argc == 4) {
    // State's FPU union begins with the x87 control word. Derive its byte
    // offset from this IR's declared layout instead of baking in a host offset.
    auto *state = StructType::getTypeByName(context, "struct.State");
    auto *fpu = StructType::getTypeByName(context, "union.FPU");
    bool found = false;
    std::function<void(Type *, uint64_t)> visit = [&](Type *type,
                                                      uint64_t offset) {
      if (type == fpu) {
        if (found) {
          errs() << "Ambiguous Remill FPU layout\n";
          exit(1);
        }
        found = true;
        lower.controlOffset = offset;
        return;
      }
      if (auto *structure = dyn_cast<StructType>(type)) {
        auto *layout = module->getDataLayout().getStructLayout(structure);
        for (unsigned i = 0; i < structure->getNumElements(); ++i)
          visit(structure->getElementType(i),
                offset + layout->getElementOffset(i));
      }
    };
    if (state && fpu)
      visit(state, 0);
    if (!found) {
      errs() << "Remill State/FPU control layout is missing\n";
      return 1;
    }
    lower.remillControl = true;
  }
  lower.scales();
  lower.operations();
  lower.storage();
  if (verifyModule(*module, &errs()))
    return 1;
  std::error_code error;
  raw_fd_ostream output(argv[2], error);
  if (error) {
    errs() << error.message() << "\n";
    return 1;
  }
  module->print(output, nullptr);
  errs() << "Lowered " << lower.count
         << " FP80 operations without changing storage precision\n";
}
