/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_COMPILER_UTILS_ARM64_ASSEMBLER_ARM64_H_
#define ART_COMPILER_UTILS_ARM64_ASSEMBLER_ARM64_H_

#include <stdint.h>
#include <memory>
#include <vector>

#include <android-base/logging.h>

#include "base/bit_utils_iterator.h"
#include "base/macros.h"
#include "dwarf/register.h"
#include "offsets.h"
#include "utils/arm64/managed_register_arm64.h"
#include "utils/assembler.h"

// TODO(VIXL): Make VIXL compile cleanly with -Wshadow, -Wdeprecated-declarations.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "aarch64/disasm-aarch64.h"
#include "aarch64/macro-assembler-aarch64.h"
#pragma GCC diagnostic pop

namespace art HIDDEN {

class Arm64InstructionSetFeatures;

namespace arm64 {

static inline dwarf::Reg DWARFReg(vixl::aarch64::CPURegister reg) {
  if (reg.IsFPRegister()) {
    return dwarf::Reg::Arm64Fp(reg.GetCode());
  } else {
    DCHECK_LT(reg.GetCode(), 31u);  // X0 - X30.
    return dwarf::Reg::Arm64Core(reg.GetCode());
  }
}

#define MEM_OP(...)      vixl::aarch64::MemOperand(__VA_ARGS__)

enum LoadOperandType {
  kLoadSignedByte,
  kLoadUnsignedByte,
  kLoadSignedHalfword,
  kLoadUnsignedHalfword,
  kLoadWord,
  kLoadCoreWord,
  kLoadSWord,
  kLoadDWord
};

enum StoreOperandType {
  kStoreByte,
  kStoreHalfword,
  kStoreWord,
  kStoreCoreWord,
  kStoreSWord,
  kStoreDWord
};

class Arm64Assembler final : public Assembler {
 public:
  explicit Arm64Assembler(
      ArenaAllocator* allocator, const Arm64InstructionSetFeatures* features = nullptr);

  virtual ~Arm64Assembler() {}

  vixl::aarch64::MacroAssembler* GetVIXLAssembler() { return &vixl_masm_; }

  // Finalize the code.
  void FinalizeCode() override;

  // Size of generated code.
  size_t CodeSize() const override;
  const uint8_t* CodeBufferBaseAddress() const override;

  // Copy instructions out of assembly buffer into the given region of memory.
  void CopyInstructions(const MemoryRegion& region) override;

  void LoadRawPtr(ManagedRegister dest, ManagedRegister base, Offset offs);

  void SpillRegisters(vixl::aarch64::CPURegList registers, int offset);
  void UnspillRegisters(vixl::aarch64::CPURegList registers, int offset);

  // A helper to save/restore a list of ZRegisters to a specified stack offset location.
  template <bool is_save>
  void SaveRestoreZRegisterList(uint32_t vreg_bit_vector, int64_t stack_offset) {
    if (vreg_bit_vector == 0) {
      return;
    }
    vixl::aarch64::UseScratchRegisterScope temps(GetVIXLAssembler());
    vixl::aarch64::Register temp = temps.AcquireX();
    vixl_masm_.Add(temp, vixl::aarch64::sp, stack_offset);
    size_t slot_no = 0;
    for (uint32_t i : LowToHighBits(vreg_bit_vector)) {
      if (is_save) {
        vixl_masm_.Str(vixl::aarch64::ZRegister(i),
                       vixl::aarch64::SVEMemOperand(temp, slot_no, vixl::aarch64::SVE_MUL_VL));
      } else {
        vixl_masm_.Ldr(vixl::aarch64::ZRegister(i),
                       vixl::aarch64::SVEMemOperand(temp, slot_no, vixl::aarch64::SVE_MUL_VL));
      }
      slot_no++;
    }
  }

  // Jump to address (not setting link register)
  void JumpTo(ManagedRegister m_base, Offset offs, ManagedRegister m_scratch);

  //
  // Heap poisoning.
  //

  // Poison a heap reference contained in `reg`.
  void PoisonHeapReference(vixl::aarch64::Register reg);
  // Unpoison a heap reference contained in `reg`.
  void UnpoisonHeapReference(vixl::aarch64::Register reg);
  // Poison a heap reference contained in `reg` if heap poisoning is enabled.
  void MaybePoisonHeapReference(vixl::aarch64::Register reg);
  // Unpoison a heap reference contained in `reg` if heap poisoning is enabled.
  void MaybeUnpoisonHeapReference(vixl::aarch64::Register reg);

  // Emit code checking the status of the Marking Register, and aborting
  // the program if MR does not match the value stored in the art::Thread
  // object.
  //
  // Argument `temp` is used as a temporary register to generate code.
  // Argument `code` is used to identify the different occurrences of
  // MaybeGenerateMarkingRegisterCheck and is passed to the BRK instruction.
  void GenerateMarkingRegisterCheck(vixl::aarch64::Register temp, int code = 0);

  void Bind([[maybe_unused]] Label* label) override {
    UNIMPLEMENTED(FATAL) << "Do not use Bind(Label*) for ARM64";
  }
  void Jump([[maybe_unused]] Label* label) override {
    UNIMPLEMENTED(FATAL) << "Do not use Jump(Label*) for ARM64";
  }

  void Bind(vixl::aarch64::Label* label) {
    vixl_masm_.Bind(label);
  }
  void Jump(vixl::aarch64::Label* label) {
    vixl_masm_.B(label);
  }

  static vixl::aarch64::Register reg_x(int code) {
    CHECK(code < kNumberOfXRegisters) << code;
    if (code == SP) {
      return vixl::aarch64::sp;
    } else if (code == XZR) {
      return vixl::aarch64::xzr;
    }
    return vixl::aarch64::XRegister(code);
  }

  static vixl::aarch64::Register reg_w(int code) {
    CHECK(code < kNumberOfWRegisters) << code;
    if (code == WSP) {
      return vixl::aarch64::wsp;
    } else if (code == WZR) {
      return vixl::aarch64::wzr;
    }
    return vixl::aarch64::WRegister(code);
  }

  static vixl::aarch64::VRegister reg_d(int code) {
    return vixl::aarch64::DRegister(code);
  }

  static vixl::aarch64::VRegister reg_s(int code) {
    return vixl::aarch64::SRegister(code);
  }

 private:
  // VIXL assembler.
  vixl::aarch64::MacroAssembler vixl_masm_;

  // Used for testing.
  friend class Arm64ManagedRegister_VixlRegisters_Test;
};

}  // namespace arm64
}  // namespace art

#endif  // ART_COMPILER_UTILS_ARM64_ASSEMBLER_ARM64_H_
