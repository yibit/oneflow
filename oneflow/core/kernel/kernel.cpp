#include "oneflow/core/kernel/kernel.h"
#include "oneflow/core/common/gdb.h"

namespace oneflow {

namespace {

void CheckSameDim0ValidNum(const PbRpf<std::string>& bns,
                           const std::function<Blob*(const std::string&)>& BnInOp2Blob) {
  const void* mem_ptr = BnInOp2Blob(bns.Get(0))->dim0_valid_num_ptr();
  size_t len = BnInOp2Blob(bns.Get(0))->ByteSizeOfDim0ValidNumField();
  FOR_RANGE(int, i, 1, bns.size()) {
    CHECK_EQ(std::memcmp(BnInOp2Blob(bns.Get(i))->dim0_valid_num_ptr(), mem_ptr, len), 0);
  }
}

void ClearBlobDim0ValidNumIfNeed(const PbRpf<std::string>& bns,
                                 const std::function<Blob*(const std::string&)>& BnInOp2Blob) {
  for (const auto& bn : bns) {
    Blob* blob = BnInOp2Blob(bn);
    if (blob != nullptr && blob->has_dim0_valid_num_field()) {
      std::memset(blob->mut_dim0_valid_num_ptr(), 0, blob->ByteSizeOfDim0ValidNumField());
    }
  }
}

}  // namespace

void Kernel::Init(const ParallelContext* parallel_ctx, const KernelConf& kernel_conf,
                  DeviceCtx* device_ctx) {
  kernel_conf_ = kernel_conf;
  VirtualKernelInit(parallel_ctx, device_ctx);
}

void Kernel::InitModelAndConstBuf(const KernelCtx& ctx, const ParallelContext* parallel_ctx,
                                  const Snapshot* snapshot,
                                  std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  InitConstBufBlobs(ctx.device_ctx, BnInOp2Blob);
  std::string model_load_dir = "";
  if (snapshot) {
    std::string snapshot_load_path = snapshot->GetDirFromOpName(op_conf().name());
    if (SnapshotFS()->IsDirectory(snapshot_load_path)) { model_load_dir = snapshot_load_path; }
  } else {
    model_load_dir = op_conf().model_load_dir();
  }
  if (model_load_dir == "") {
    std::mt19937* random_seed_gen = static_cast<std::mt19937*>(ctx.other);
    InitModelBlobsWithRandomSeed(ctx.device_ctx, random_seed_gen, BnInOp2Blob);
  } else {
    int32_t part_id = -1;
    int32_t part_num = -1;
    std::tie(part_id, part_num) = GetPartIdAndPartNumFromParallelCtx(parallel_ctx);
    InitModelBlobsWithDir(ctx.device_ctx, part_id, part_num, model_load_dir, BnInOp2Blob);
  }
}

void Kernel::Launch(const KernelCtx& ctx,
                    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  if (kernel_conf_.is_forward()) {
    gdb::ForwardEnterBreakPoint(op_attribute(), BnInOp2Blob);
    Forward(ctx, BnInOp2Blob);
    gdb::ForwardLeaveBreakPoint(op_attribute(), BnInOp2Blob);
  } else {
    gdb::BackwardEnterBreakPoint(op_attribute(), BnInOp2Blob);
    Backward(ctx, BnInOp2Blob);
    gdb::BackwardLeaveBreakPoint(op_attribute(), BnInOp2Blob);
  }
}

const LogicalBlobId& Kernel::BnInOp2Lbi(const std::string& bn_in_op) const {
  return op_attribute().bn_in_op2lbi().at(bn_in_op);
}

bool Kernel::HasEmptyShapeBlob(const PbRpf<std::string>& bns,
                               const std::function<Blob*(const std::string&)>& BnInOp2Blob) const {
  for (const auto& bn : bns) {
    Blob* blob = BnInOp2Blob(bn);
    if (blob && blob->IsShapeEmpty()) { return true; }
  }
  return false;
}

void Kernel::Forward(const KernelCtx& ctx,
                     std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  if (kernel_conf_.need_do_dim0_valid_num()) {
    CHECK(!kernel_conf_.need_do_opaque_header());
    ForwardDim0ValidNum(ctx, BnInOp2Blob);
  }
  if (HasEmptyShapeBlob(op_attribute().input_bns(), BnInOp2Blob) && !NeedForwardIfBlobEmpty()) {
    ClearBlobDim0ValidNumIfNeed(op_attribute().output_bns(), BnInOp2Blob);
    return;
  }
  if (kernel_conf_.need_do_dim1_valid_num()) {
    CHECK(!kernel_conf_.need_do_opaque_header());
    ForwardDim1ValidNum(ctx, BnInOp2Blob);
  }
  if (kernel_conf_.need_do_dim2_valid_num()) {
    CHECK(!kernel_conf_.need_do_opaque_header());
    ForwardDim2ValidNum(ctx, BnInOp2Blob);
  }
  if (kernel_conf_.need_do_record_id_in_device_piece()) {
    CHECK(!kernel_conf_.need_do_opaque_header());
    ForwardRecordIdInDevicePiece(ctx, BnInOp2Blob);
  }
  ForwardDataContent(ctx, BnInOp2Blob);
  if (GetActivationType() != ActivationType::kNone) {
    const PbRpf<std::string> obns = this->op_attribute().output_bns();
    CHECK_EQ(obns.size(), 1);

    Blob* out_blob = BnInOp2Blob(obns[0]);
    ForwardActivation(ctx, out_blob);
  }
  if (kernel_conf_.need_do_opaque_header()) {
    ForwardPackedHeader(ctx, BnInOp2Blob);
  } else {
    if (kernel_conf_.need_do_data_id()) { ForwardDataId(ctx, BnInOp2Blob); }
    if (kernel_conf_.need_do_col_num()) { ForwardColNum(ctx, BnInOp2Blob); }
  }
}

void Kernel::Backward(const KernelCtx& ctx,
                      std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  if (op_attribute().model_diff_bns().size() > 0) {
    BackwardModelDiffDim0ValidNum(ctx, BnInOp2Blob);
  }
  if (kernel_conf_.need_do_dim0_valid_num() && op_attribute().input_diff_bns_size() > 0) {
    CHECK(!kernel_conf_.need_do_opaque_header());
    BackwardInDiffDim0ValidNum(ctx, BnInOp2Blob);
  }
  if (HasEmptyShapeBlob(op_attribute().output_diff_bns(), BnInOp2Blob)
      && !NeedBackwardIfBlobEmpty()) {
    ClearBlobDim0ValidNumIfNeed(op_attribute().input_diff_bns(), BnInOp2Blob);
    return;
  }
  CHECK_EQ(false, HasEmptyShapeBlob(op_attribute().model_diff_bns(), BnInOp2Blob));
  ActivationType activation = GetActivationType();
  if (activation != ActivationType::kNone) {
    const PbRpf<std::string> obns = this->op_attribute().output_bns();
    const PbRpf<std::string> odbns = this->op_attribute().output_diff_bns();
    CHECK_EQ(obns.size(), 1);
    CHECK_EQ(odbns.size(), 1);

    const Blob* out_blob = BnInOp2Blob(obns[0]);
    const Blob* out_diff_blob = BnInOp2Blob(odbns[0]);
    Blob* bw_activation_blob = BnInOp2Blob("bw_activation");
    CHECK(bw_activation_blob != nullptr);
    BackwardActivation(ctx, out_blob, out_diff_blob, bw_activation_blob);
    BackwardDataContent(ctx, [&](const std::string& bn) -> Blob* {
      if (bn == odbns[0]) {
        return bw_activation_blob;
      } else {
        return BnInOp2Blob(bn);
      }
    });
  } else {
    BackwardDataContent(ctx, BnInOp2Blob);
  }
  if (kernel_conf_.need_do_data_id()) { BackwardDataId(ctx, BnInOp2Blob); }
  if (kernel_conf_.need_do_col_num()) { BackwardColNum(ctx, BnInOp2Blob); }
  if (this->op_attribute().model_diff_bns().size() > 0) {
    SetTotalInstanceNumDiffBlob(ctx, BnInOp2Blob);
  }
}

bool Kernel::HasModelBns() const { return op_attribute().model_bns().size() > 0; }

template<DeviceType device_type>
void KernelIf<device_type>::ForwardDataId(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  ForwardField(ctx.device_ctx, BnInOp2Blob, &Blob::CopyDataIdFrom);
}

template<DeviceType device_type>
void KernelIf<device_type>::ForwardColNum(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  ForwardField(ctx.device_ctx, BnInOp2Blob, &Blob::CopyColNumFrom);
}

template<DeviceType device_type>
void KernelIf<device_type>::ForwardField(DeviceCtx* ctx,
                                         std::function<Blob*(const std::string&)> BnInOp2Blob,
                                         void (Blob::*Copy)(DeviceCtx*, const Blob*)) const {
  const PbRpf<std::string>* input_bn = &op_attribute().input_bns();
  CopyField(ctx, BnInOp2Blob, *input_bn, op_attribute().output_bns(), Copy);
}

template<DeviceType device_type>
void KernelIf<device_type>::ForwardDim0ValidNum(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  CHECK(kernel_conf().can_naive_do_dim0_valid_num());
  CheckSameDim0ValidNum(op_attribute().input_bns(), BnInOp2Blob);
  CopyField(ctx.device_ctx, BnInOp2Blob, BnInOp2Blob(op_attribute().input_bns(0)),
            op_attribute().output_bns(), &Blob::CopyDim0ValidNumFrom);
}

template<DeviceType device_type>
void KernelIf<device_type>::ForwardRecordIdInDevicePiece(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  CHECK(kernel_conf().can_naive_do_record_id_in_device_piece());
  CheckSameRecordIdInDevicePiece(op_attribute().input_bns(), BnInOp2Blob);
  CopyField(ctx.device_ctx, BnInOp2Blob, BnInOp2Blob(op_attribute().input_bns(0)),
            op_attribute().output_bns(), &Blob::CopyRecordIdInDevicePieceFrom);
}

template<DeviceType device_type>
void KernelIf<device_type>::BackwardModelDiffDim0ValidNum(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  bool is_out_diff_empty = HasEmptyShapeBlob(op_attribute().output_diff_bns(), BnInOp2Blob);
  for (const std::string& bn : op_attribute().model_diff_bns()) {
    Blob* blob = BnInOp2Blob(bn);
    CHECK(blob);
    if (blob->has_dim0_valid_num_field()) {
      CHECK(blob->has_dim0_inner_shape());
      CHECK_EQ(1, blob->dim0_inner_shape().At(0));
      blob->set_dim0_valid_num(0, is_out_diff_empty ? 0 : blob->static_shape().At(0));
    }
  }
}

template<DeviceType device_type>
void KernelIf<device_type>::BackwardInDiffDim0ValidNum(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  CHECK(kernel_conf().can_naive_do_dim0_valid_num());
  CheckSameDim0ValidNum(op_attribute().output_diff_bns(), BnInOp2Blob);
  PbRpf<std::string> input_diff_bns;
  for (const auto& bn : op_attribute().input_diff_bns()) {
    if (BnInOp2Blob(bn) != nullptr) { *input_diff_bns.Add() = bn; }
  }
  if (input_diff_bns.empty()) { return; }
  CopyField(ctx.device_ctx, BnInOp2Blob, BnInOp2Blob(op_attribute().output_diff_bns(0)),
            input_diff_bns, &Blob::CopyDim0ValidNumFrom);
}

template<DeviceType device_type>
void KernelIf<device_type>::ForwardPackedHeader(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  CopyField(ctx.device_ctx, BnInOp2Blob, op_attribute().input_bns(), op_attribute().output_bns(),
            &Blob::CopyHeaderFrom);
}

template<DeviceType device_type>
void KernelIf<device_type>::BackwardDataId(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  // do nothing
}

template<DeviceType device_type>
void KernelIf<device_type>::BackwardColNum(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  CopyField(ctx.device_ctx, BnInOp2Blob, op_attribute().output_diff_bns(),
            op_attribute().input_diff_bns(), &Blob::CopyColNumFrom);
}

template<DeviceType device_type, typename T>
int32_t KernelIfWithModel<device_type, T>::CalcInstanceNumSum(
    const int32_t index, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  CHECK_LT(index, this->op_attribute().output_diff_bns_size());
  int32_t instance_num_sum = 0;
  Blob* out_diff_blob = BnInOp2Blob(this->op_attribute().output_diff_bns().Get(index));
  if (out_diff_blob->has_dim0_valid_num_field()) {
    FOR_RANGE(int32_t, i, 0, out_diff_blob->dim0_inner_shape().At(0)) {
      instance_num_sum += out_diff_blob->dim0_valid_num(i);
    }
  } else {
    instance_num_sum = out_diff_blob->static_shape().At(0);
  }
  return instance_num_sum;
}
template<DeviceType device_type, typename T>
void KernelIfWithModel<device_type, T>::SetTotalInstanceNumDiffBlob(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  CHECK_GE(this->op_attribute().model_bns().size(), 2);
  int32_t instance_num_sum = CalcInstanceNumSum(0, BnInOp2Blob);
  FOR_RANGE(int32_t, i, 1, this->op_attribute().output_diff_bns_size()) {
    CHECK_EQ(instance_num_sum, CalcInstanceNumSum(i, BnInOp2Blob));
  }
  Blob* total_instance_num_diff_blob = BnInOp2Blob("total_instance_num_diff");
  KernelUtil<device_type, T>::Set(ctx.device_ctx, static_cast<T>(instance_num_sum),
                                  total_instance_num_diff_blob->mut_dptr<T>());
}

template<DeviceType device_type>
void KernelIf<device_type>::CopyField(DeviceCtx* ctx,
                                      std::function<Blob*(const std::string&)> BnInOp2Blob,
                                      const Blob* from_blob, const PbRpf<std::string>& to_bns,
                                      void (Blob::*Copy)(DeviceCtx*, const Blob*)) const {
  for (const std::string& to_bn : to_bns) { (BnInOp2Blob(to_bn)->*Copy)(ctx, from_blob); }
}

template<DeviceType device_type>
void KernelIf<device_type>::CopyField(DeviceCtx* ctx,
                                      std::function<Blob*(const std::string&)> BnInOp2Blob,
                                      const PbRpf<std::string>& from_bns,
                                      const PbRpf<std::string>& to_bns,
                                      void (Blob::*Copy)(DeviceCtx*, const Blob*)) const {
  if (from_bns.size() == 1) {
    const Blob* in_blob = BnInOp2Blob(from_bns[0]);
    CopyField(ctx, BnInOp2Blob, in_blob, to_bns, Copy);
  } else if (to_bns.size() == 1) {
    Blob* in_blob = BnInOp2Blob(from_bns[0]);
    Blob* out_blob = BnInOp2Blob(to_bns[0]);
    (out_blob->*Copy)(ctx, in_blob);
  } else {
    CHECK(from_bns.size() == 0 || to_bns.size() == 0);
  }
}

std::unique_ptr<const Kernel> ConstructKernel(const ParallelContext* parallel_ctx,
                                              const KernelConf& conf, DeviceCtx* device_ctx) {
  Kernel* rptr = NewObj<Kernel>(conf.op_attribute().op_conf().op_type_case(), conf);
  rptr->Init(parallel_ctx, conf, device_ctx);
  return std::unique_ptr<const Kernel>(rptr);
}

template<DeviceType device_type, typename T>
ActivationType KernelIfWithActivation<device_type, T>::GetActivationType() const {
  return static_cast<ActivationType>(this->GetEnumFromCustomizedOpConf("activation"));
}

template<DeviceType device_type, typename T>
void KernelIfWithActivation<device_type, T>::ForwardActivation(const KernelCtx& ctx,
                                                               Blob* out_blob) const {
  T* out_dptr = out_blob->mut_dptr<T>();
  int64_t elem_cnt = out_blob->shape().elem_cnt();

  switch (GetActivationType()) {
#define DEFINE_ONE_CASE(activation_type)                                                       \
  case ActivationType::k##activation_type:                                                     \
    KernelUtil<device_type, T>::activation_type(ctx.device_ctx, elem_cnt, out_dptr, out_dptr); \
    break;
    DEFINE_ONE_CASE(TanH)
    DEFINE_ONE_CASE(Sigmoid)
    DEFINE_ONE_CASE(Relu)
#undef DEFINE_ONE_CASE
    default: UNIMPLEMENTED();
  }
}

template<DeviceType device_type, typename T>
void KernelIfWithActivation<device_type, T>::BackwardActivation(const KernelCtx& ctx,
                                                                const Blob* out_blob,
                                                                const Blob* out_diff_blob,
                                                                Blob* bw_activation_blob) const {
  int64_t elem_cnt = out_blob->shape().elem_cnt();
  switch (GetActivationType()) {
#define DEFINE_ONE_CASE(activation_type)                                    \
  case ActivationType::k##activation_type:                                  \
    KernelUtil<device_type, T>::activation_type##Backward(                  \
        ctx.device_ctx, elem_cnt, out_blob->dptr<T>(), out_blob->dptr<T>(), \
        out_diff_blob->dptr<T>(), bw_activation_blob->mut_dptr<T>());       \
    break
    DEFINE_ONE_CASE(TanH);
    DEFINE_ONE_CASE(Sigmoid);
    DEFINE_ONE_CASE(Relu);
#undef DEFINE_ONE_CASE
    default: UNIMPLEMENTED();
  }
}

#define INSTANTIATE_KERNEL_IF(device_type) template class KernelIf<device_type>;

OF_PP_FOR_EACH_TUPLE(INSTANTIATE_KERNEL_IF, DEVICE_TYPE_SEQ);

#define INSTANTIATE_KERNEL_IF_SUBCLASS(device_type, data_type_pair)                \
  template class KernelIfWithModel<device_type, OF_PP_PAIR_FIRST(data_type_pair)>; \
  template class KernelIfWithActivation<device_type, OF_PP_PAIR_FIRST(data_type_pair)>;

OF_PP_SEQ_PRODUCT_FOR_EACH_TUPLE(INSTANTIATE_KERNEL_IF_SUBCLASS, DEVICE_TYPE_SEQ,
                                 FLOATING_DATA_TYPE_SEQ);

}  // namespace oneflow
