/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#if GOOGLE_CUDA

#define EIGEN_USE_GPU

#include "time_two.h"
#include "tensorflow/core/util/gpu_kernel_helper.h"


#include <cmath>
const float verybig = INFINITY;

namespace tensorflow {
namespace functor {

typedef Eigen::GpuDevice GPUDevice;


// Define the CUDA kernel.
template <typename T, typename S>
__device__ float calcint(S q1,S q2,T f1,T f2){
    T q1f = static_cast<T>(q1);
    T q2f = static_cast<T>(q2);
    return ((f1+q1f*q1f) - (f2+q2f*q2f)) / (2*q1f - 2*q2f);
}


template <typename T, typename S>
__global__ void BasinFinderCudaKernel(const int dim0, const int dim1,
    const int dim2, const T* f, T* out, T* z, S* v, S* basins) {

        const int batchdim = threadIdx.x + blockDim.x*blockIdx.x;
        const int i0 = batchdim/dim2;
        const int i2 = batchdim%dim2;
        // f is a 3-tensor of shape (dim0,dim1,dim2)
        // this thread looks at f[batchdim//shape[2],:,batchdim%shape[2]]

        const int offset1= i0*dim1*dim2+i2;
        const int offset2= i0*(dim1+1)*dim2+i2;

        // initialize v,z
        for(int i1=0; i1<dim1; i1++) {
          v[offset1+i1*dim2]=0;
          z[offset2+i1*dim2]=0;
        }
        z[offset2+dim1*dim2]=0;

        // compute lower parabolas
        int k=0;
        z[offset2+0]=-verybig;
        z[offset2+dim2]=verybig;

        for(int q=1; q<dim1; q++) {
              //printf("%d %d %d :: %d %d \n",i0,i2,q,offset1+k*dim2,offset1+v[offset1+k*dim2]*dim2);
              float s=calcint<T,S>(q,v[offset1+k*dim2],f[offset1+q*dim2],f[offset1+v[offset1+k*dim2]*dim2]);
              //printf("%d %d %d :: %d %d %f \n",i0,i2,q,offset1+k*dim2,offset1+v[offset1+k*dim2]*dim2,s);

              while(s<=z[offset2+k*dim2]){
                  k=k-1;
                  //printf("%d %d %d :: %d %d \n",i0,i2,q,offset1+k*dim2,offset1+v[offset1+k*dim2]*dim2);
                  s=calcint<T,S>(q,v[offset1+k*dim2],f[offset1+q*dim2],f[offset1+v[offset1+k*dim2]*dim2]);
                  //printf("%d %d %d :: %d %d %f \n",i0,i2,q,offset1+k*dim2,offset1+v[offset1+k*dim2]*dim2,s);
              }
              k=k+1;
              v[offset1+k*dim2]=q;
              z[offset2+k*dim2]=s;
              z[offset2+k*dim2+dim2]=verybig;
        }

        // compute basins and out
        k=0;
        for(int q=0; q<dim1; q++) {
          while(z[offset2+(k+1)*dim2]<q) {
            k=k+1;
          }
          int thisv=v[offset1+k*dim2];
          basins[offset1+q*dim2]=thisv;
          out[offset1+q*dim2] = (q-thisv)*(q-thisv) + f[offset1+thisv*dim2];
        }
}

// Define the GPU implementation that launches the CUDA kernel.
template <typename T,typename S>
struct BasinFinderFunctor<GPUDevice, T,S> {
  void operator()(const GPUDevice& d, int dim0, int dim1, int dim2, const T* f, T* out, T* z, S* v, S* basins) {
    // Launch the cuda kernel.
    //
    // See core/util/cuda_kernel_helper.h for example of computing
    // block count and thread_per_block count.
    int block_count = 1+static_cast<int>(dim0*dim2/8);
    int thread_per_block = 8;
    BasinFinderCudaKernel<T,S>
        <<<block_count, thread_per_block, 0, d.stream()>>>(dim0, dim1,dim2, f, out,z,v,basins);
  }
};

// Explicitly instantiate functors for the types of OpKernels registered.
template struct BasinFinderFunctor<GPUDevice, float,int32>;
}  // end namespace functor
}  // end namespace tensorflow

#endif  // GOOGLE_CUDA