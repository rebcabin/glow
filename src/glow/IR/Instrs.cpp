// Copyright 2017 Facebook Inc.  All Rights Reserved.

#include "glow/IR/Instrs.h"
#include "glow/IR/IR.h"
#include "glow/Support/Casting.h"
#include "glow/Support/Support.h"

#include <cassert>

using namespace glow;

//===----------------------------------------------------------------------===//
//                      Instruction textual printers
//===----------------------------------------------------------------------===//

std::string ConvolutionInst::getExtraDesc() const {
  return listToString(kernel_, stride_, pad_, depth_);
}

const char *PoolInst::getKindStr() const {
  const char *names[] = {"max", "avg", nullptr};
  return names[static_cast<int>(kind_)];
}

std::string PoolInst::getExtraDesc() const {
  std::string sb = getKindStr();
  return sb += " " + listToString(kernel_, stride_, pad_);
}

std::string FullyConnectedInst::getExtraDesc() const {
  return listToString(depth_);
}

std::string TransposeInst::getExtraDesc() const {
  return arrayRefToString<unsigned>(shuffle_);
}

std::string ReshapeInst::getExtraDesc() const {
  return arrayRefToString<size_t>(dims_);
}

std::string ConcatInst::getExtraDesc() const {
  return "{ " + std::to_string(dim_) + " }";
}

std::string BatchNormalizationInst::getExtraDesc() const {
  return listToString(channelIdx_, epsilon_, momentum_);
}

std::string LocalResponseNormalizationInst::getExtraDesc() const {
  return listToString(halfWindowSize_, alpha_, beta_, k_);
}

const char *ArithmeticInst::getKindStr() const {
  const char *names[] = {"add", "mul", nullptr};
  return names[static_cast<int>(kind_)];
}

std::string ArithmeticInst::getExtraDesc() const { return getKindStr(); }

const char *WeightVar::getInitKindStr(InitKind kind) {
  // extern: No initialization.
  // broadcast: Broadcast a single value to all elements.
  // xavier: Init the tensor with random values using the Xavier method.
  const char *names[] = {"extern", "broadcast", "xavier", nullptr};
  return names[static_cast<int>(kind)];
}

const char *WeightVar::getInitKindStr() const {
  return getInitKindStr(initKind_);
}

std::string WeightVar::getExtraDesc() const {
  auto sp = ", ";
  auto r = std::to_string(*getType());
  if (getInitKind() != InitKind::Extern) {
    r += std::string(sp) + getInitKindStr() + sp + std::to_string(val_);
  }
  return r;
}

std::string AllocActivationInst::getExtraDesc() const {
  return std::to_string(*getType());
}

//===----------------------------------------------------------------------===//
//                       Instruction verification
//===----------------------------------------------------------------------===//

/// Check that the type of the first operand matches the type of the second
/// operand.
static void checkSameType(Instruction::Operand A, Instruction::Operand B) {
  assert(A.first->getType() == B.first->getType() && "Invalid type");
}

void CopyInst::verify() const {
  checkSameType(getOperand(0), getOperand(1));
  auto *op0 = getOperand(0).first;
  auto *op1 = getOperand(1).first;
  (void)op0;
  (void)op1;
  // The operands of the copy instruction must be variables.
  assert(isa<AllocActivationInst>(op0) || isa<WeightVar>(op0));
  assert(isa<AllocActivationInst>(op1) || isa<WeightVar>(op1));
}
void ConvolutionInst::verify() const {
  Value *dest = getOperand(0).first;
  Value *src = getOperand(1).first;
  Value *filter = getOperand(2).first;
  Value *bias = getOperand(3).first;
  (void)filter;
  (void)bias;

  ShapeNHWC idim(src->getType()->dims());
  ShapeNHWC odim(dest->getType()->dims());
  (void)odim;
  assert(idim.w >= kernel_ && idim.h >= kernel_ &&
         "buffer too small for selected stride");

  auto outSz = ConvolutionNode::calculateOutputDims(idim.h, idim.w, pad_,
                                                    kernel_, stride_);
  ShapeNHWC exp(idim.n, outSz.first, outSz.second, depth_);
  (void)exp;
  assert(exp == odim && "Invalid output dimensions");

  llvm::ArrayRef<size_t> filterDims = {depth_, kernel_, kernel_, idim.c};
  assert(filter->getType()->dims() == filterDims && "Invalid filter dims");

  llvm::ArrayRef<size_t> biasDims = {depth_};
  assert(bias->getType()->dims() == biasDims && "Invalid bias dims");
}

void PoolInst::verify() const {
  Value *dest = getOperand(0).first;
  Value *src = getOperand(1).first;
  Value *srcXY = getOperand(2).first;
  (void)srcXY;
  ShapeNHWC idim = ShapeNHWC(src->getType()->dims());
  ShapeNHWC odim = ShapeNHWC(dest->getType()->dims());
  (void)odim;
  assert(idim.w >= kernel_ && idim.h >= kernel_ &&
         "buffer too small for selected stride");

  auto outSz = ConvolutionNode::calculateOutputDims(idim.h, idim.w, pad_,
                                                    kernel_, stride_);
  ShapeNHWC exp(idim.n, outSz.first, outSz.second, idim.c);
  (void)exp;
  assert(exp == odim && "Invalid output dimensions");

  // Allocate cache arrays that store the x and y coordinates of the incoming
  // gradient for each max element.
  if (kind_ == OpKind::Max) {
    llvm::ArrayRef<size_t> exp = {idim.n, outSz.first, outSz.second, idim.c, 2};
    assert(srcXY->getType()->dims() == exp && "Invalid srcXY dims");
  }
}

void FullyConnectedInst::verify() const {
  Value *dest = getOperand(0).first;
  Value *src = getOperand(1).first;
  Value *W = getOperand(2).first;
  Value *B = getOperand(3).first;
  (void)dest;
  (void)W;
  (void)B;
  auto idim = flattenCdr(src->dims());

  llvm::ArrayRef<size_t> exp = {idim.first, depth_};
  assert(dest->dims() == exp && "Invalid output shape");
  (void)exp;

  llvm::ArrayRef<size_t> expW = {depth_, idim.second};
  assert(W->dims() == expW && "Invalid output shape");
  (void)expW;

  llvm::ArrayRef<size_t> expB = {depth_};
  assert(B->dims() == expB && "Invalid output shape");
  (void)expB;
}

void ReluInst::verify() const { checkSameType(getOperand(0), getOperand(1)); }
void SigmoidInst::verify() const {
  checkSameType(getOperand(0), getOperand(1));
}
void TanhInst::verify() const { checkSameType(getOperand(0), getOperand(1)); }
void SoftMaxInst::verify() const {
  checkSameType(getOperand(0), getOperand(1));
}
void RegressionInst::verify() const {
  checkSameType(getOperand(0), getOperand(1));
  checkSameType(getOperand(0), getOperand(2));
}

void ReshapeInst::verify() const {
  assert(getOperand(0).first->getType()->size() ==
             getOperand(1).first->getType()->size() &&
         "Reshape into a different size");
}

void TransposeInst::verify() const {
  auto *dest = getOperand(0).first;
  auto *src = getOperand(1).first;
  (void)dest;
  std::vector<size_t> shape;

  auto dims = src->dims();
  for (size_t i = 0; i < dims.size(); i++) {
    shape.push_back(dims[shuffle_[i]]);
  }

  assert(dest->dims() == llvm::ArrayRef<size_t>(shape) &&
         "Invalid transpose dims");
}

void ConcatInst::verify() const {
  assert(getNumOperands() > 1 && "Invalid number of operands");
  // The dimension of the first input.
  auto inDim = getOperand(1).first->dims();

  for (size_t i = 2, e = this->getNumOperands(); i < e; i++) {
    assert(getOperand(i).first->dims() == inDim && "Invalid input shape");
  }

  std::vector<size_t> shape(inDim.begin(), inDim.end());
  // We are stacking the tensors along a specific dimension. This means that we
  // increase the size of the tensor along this dimension.
  shape[dim_] *= getNumOperands() - 1;

  assert(getOperand(0).first->dims() == llvm::ArrayRef<size_t>(shape) &&
         "Invalid output shape");
}
void BatchNormalizationInst::verify() const {
  checkSameType(getOperand(0), getOperand(1));

  // Figure out how many channels are in the tensor.
  size_t channels = getOperand(0).first->dims()[channelIdx_];

  llvm::ArrayRef<size_t> exp = {channels};
  assert(getOperand(2).first->getType()->dims() == exp && "Invalid bias dim");
  assert(getOperand(3).first->getType()->dims() == exp && "Invalid scale dim");
  assert(getOperand(4).first->getType()->dims() == exp && "Invalid mean dim");
  assert(getOperand(5).first->getType()->dims() == exp && "Invalid var dim");
}
void LocalResponseNormalizationInst::verify() const {
  checkSameType(getOperand(0), getOperand(1));
  checkSameType(getOperand(0), getOperand(2));
}
void ArithmeticInst::verify() const {
  checkSameType(getOperand(0), getOperand(1));
  checkSameType(getOperand(0), getOperand(2));
}

void AllocActivationInst::verify() const {
  unsigned numDealloc = 0;
  for (const Use &U : getUsers()) {
    numDealloc += isa<DeallocActivationInst>(U.get());
  }

  // Make sure that there is exactly one user is a deallocation.
  assert(numDealloc == 1 && "Invalid number of tensor deallocation");
}

void DeallocActivationInst::verify() const {
  // The operand of this instruction needs to be an AllocActivationInst.
  assert(isa<AllocActivationInst>(getOperand(0).first) && "Invalid operand");
}
