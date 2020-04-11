#include "oneflow/customized/kernels/clip_by_value_kernel.h"
#include "oneflow/core/device/cuda_util.h"

namespace oneflow {

namespace {

template<typename T, typename F>
__global__ void CudaClipForward(F clip_func, int64_t n, const T* x, T* y) {
  CUDA_1D_KERNEL_LOOP(i, n) { y[i] = clip_func(x[i]); }
}

template<typename T, typename F>
__global__ void CudaClipBackward(F clip_func, int64_t n, const T* x, const T* dy, T* dx) {
  CUDA_1D_KERNEL_LOOP(i, n) { dx[i] = clip_func(x[i], dy[i]); }
}

}  // namespace

template<typename T>
struct ClipKernelUtil<DeviceType::kGPU, T> {
  template<typename F>
  static void Forward(DeviceCtx* ctx, F clip_func, const int64_t n, const T* x, T* y) {
    RUN_CUDA_KERNEL((CudaClipForward<T, F>), ctx, n, clip_func, n, x, y);
  }

  template<typename F>
  static void Backward(DeviceCtx* ctx, F clip_func, const int64_t n, const T* x, const T* dy,
                       T* dx) {
    RUN_CUDA_KERNEL((CudaClipBackward<T, F>), ctx, n, clip_func, n, x, dy, dx);
  }
};

#define INITIATE_CLIP_KERNEL_UTIL_GPU(dtype, dtype_v)                                          \
  template struct ClipKernelUtil<DeviceType::kGPU, dtype>;                                     \
  template void ClipKernelUtil<DeviceType::kGPU, dtype>::Forward(                              \
      DeviceCtx*, ClipByMinFunctor<dtype>, const int64_t n, const dtype*, dtype*);             \
  template void ClipKernelUtil<DeviceType::kGPU, dtype>::Forward(                              \
      DeviceCtx*, ClipByMaxFunctor<dtype>, const int64_t n, const dtype*, dtype*);             \
  template void ClipKernelUtil<DeviceType::kGPU, dtype>::Forward(                              \
      DeviceCtx*, ClipByMinMaxFunctor<dtype>, const int64_t n, const dtype*, dtype*);          \
  template void ClipKernelUtil<DeviceType::kGPU, dtype>::Backward(                             \
      DeviceCtx*, ClipByMinGradFunctor<dtype>, const int64_t n, const dtype*, const dtype*,    \
      dtype*);                                                                                 \
  template void ClipKernelUtil<DeviceType::kGPU, dtype>::Backward(                             \
      DeviceCtx*, ClipByMaxGradFunctor<dtype>, const int64_t n, const dtype*, const dtype*,    \
      dtype*);                                                                                 \
  template void ClipKernelUtil<DeviceType::kGPU, dtype>::Backward(                             \
      DeviceCtx*, ClipByMinMaxGradFunctor<dtype>, const int64_t n, const dtype*, const dtype*, \
      dtype*);

OF_PP_FOR_EACH_TUPLE(INITIATE_CLIP_KERNEL_UTIL_GPU, ARITHMETIC_DATA_TYPE_SEQ)

}  // namespace oneflow
