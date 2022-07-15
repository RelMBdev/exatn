/** ExaTN::Numerics: Tensor contraction sequence optimizer: CuTensorNet heuristics
REVISION: 2022/07/15

Copyright (C) 2018-2022 Dmitry I. Lyakh (Liakh)
Copyright (C) 2018-2022 Oak Ridge National Laboratory (UT-Battelle)
Copyright (C) 2022-2022 NVIDIA Corporation

SPDX-License-Identifier: BSD-3-Clause **/

#ifdef CUQUANTUM

#include <cutensornet.h>
#include <cutensor.h>
#include <cuda_runtime.h>

#ifdef MPI_ENABLED
#include "mpi.h"
#endif

#include <cstdint>
#include <vector>
#include <unordered_map>

#include "timers.hpp"
#include "tensor_network.hpp"
#include "contraction_seq_optimizer_cutnn.hpp"

namespace exatn{

namespace numerics{

#define HANDLE_CTN_ERROR(x) \
{ const auto err = x; \
  if( err != CUTENSORNET_STATUS_SUCCESS ) \
  { printf("Error: %s in line %d\n", cutensornetGetErrorString(err), __LINE__); fflush(stdout); std::abort(); } \
};


struct TensDescr {
 std::vector<int64_t> extents; //tensor dimension extents
 std::vector<int64_t> strides; //tensor dimension strides (optional)
 cudaDataType_t data_type;     //tensor element data type
 std::size_t volume = 0;       //tensor body volume
 std::size_t size = 0;         //tensor body size (bytes)
};


struct TensorNetworkParsed {
 int num_procs = 0; //total number of executing processes
 int proc_id = -1; //id of the current executing process
#ifdef MPI_ENABLED
 MPICommProxy comm; //MPI communicator over executing processes
#endif
 std::unordered_map<numerics::TensorHashType, TensDescr> tensor_descriptors; //tensor descriptors (shape, volume, data type, body)
 std::unordered_map<unsigned int, std::vector<int32_t>> tensor_modes; //indices associated with tensor dimensions (key = original tensor id)
 std::unordered_map<int32_t, int64_t> mode_extents; //extent of each registered tensor mode (mode --> extent)
 int32_t * num_modes_in = nullptr;
 int64_t ** extents_in = nullptr; //non-owning
 int64_t ** strides_in = nullptr;
 int32_t ** modes_in = nullptr; //non-owning
 uint32_t * alignments_in = nullptr;
 int32_t num_modes_out = 0;
 int64_t * extents_out = nullptr; //non-owning
 int64_t * strides_out = nullptr;
 int32_t * modes_out = nullptr; //non-owning
 uint32_t alignment_out;
 cudaDataType_t data_type;
 cutensornetComputeType_t compute_type;
 double prepare_start;
 double prepare_finish;

 ~TensorNetworkParsed() {
  //if(modes_out != nullptr) delete [] modes_out;
  if(strides_out != nullptr) delete [] strides_out;
  //if(extents_out != nullptr) delete [] extents_out;
  if(alignments_in != nullptr) delete [] alignments_in;
  //if(modes_in != nullptr) delete [] modes_in;
  if(strides_in != nullptr) delete [] strides_in;
  //if(extents_in != nullptr) delete [] extents_in;
  if(num_modes_in != nullptr) delete [] num_modes_in;
 }
};


struct InfoCuTensorNet {
 cutensornetHandle_t * cutnn_handle; //non-owning
 cutensornetNetworkDescriptor_t cutnn_network;
 cutensornetContractionOptimizerConfig_t cutnn_config;
 cutensornetContractionOptimizerInfo_t cutnn_info;
 std::size_t workspace_size = 0;
 std::size_t minimum_slices = 1;
 TensorNetworkParsed tn_rep;

 InfoCuTensorNet(cutensornetHandle_t * handle,
                 std::size_t memory_limit,
                 std::size_t min_slices,
                 const TensorNetwork & network);

 ~InfoCuTensorNet();

private:

 static constexpr std::size_t MEM_ALIGNMENT = 256;

 void parseTensorNetwork(const TensorNetwork & network);
};


ContractionSeqOptimizerCutnn::ContractionSeqOptimizerCutnn():
 min_slices_(1)
{
 cutnn_handle_ = static_cast<void*>(new cutensornetHandle_t);
 HANDLE_CTN_ERROR(cutensornetCreate((cutensornetHandle_t*)(cutnn_handle_)));
}


ContractionSeqOptimizerCutnn::~ContractionSeqOptimizerCutnn()
{
 HANDLE_CTN_ERROR(cutensornetDestroy((cutensornetHandle_t)(cutnn_handle_)));
 delete static_cast<cutensornetHandle_t*>(cutnn_handle_);
}


void ContractionSeqOptimizerCutnn::resetMemLimit(std::size_t mem_limit)
{
 make_sure(mem_limit > 0,"#ERROR(exatn::numerics::ContractionSeqOptimizerCutnn): Memory limit must be greater than zero!");
 mem_limit_ = mem_limit;
 return;
}


void ContractionSeqOptimizerCutnn::resetMinSlices(std::size_t min_slices)
{
 make_sure(min_slices > 0,"#ERROR(exatn::numerics::ContractionSeqOptimizerCutnn): Minimal number of slices must be greater than zero!");
 min_slices_ = min_slices;
 return;
}


std::shared_ptr<InfoCuTensorNet> ContractionSeqOptimizerCutnn::determineContractionSequenceWithSlicing(
                                  const TensorNetwork & network,
                                  std::list<ContrTriple> & contr_seq,
                                  std::function<unsigned int ()> intermediate_num_generator)
{
 auto info_cutnn = std::make_shared<InfoCuTensorNet>((cutensornetHandle_t*)(cutnn_handle_),mem_limit_,min_slices_,network);
 return info_cutnn;
}


double ContractionSeqOptimizerCutnn::determineContractionSequence(const TensorNetwork & network,
                                                                  std::list<ContrTriple> & contr_seq,
                                                                  std::function<unsigned int ()> intermediate_num_generator)
{
 auto info_cutnn = determineContractionSequenceWithSlicing(network,contr_seq,intermediate_num_generator);
 double flops = 0.0;
 HANDLE_CTN_ERROR(cutensornetContractionOptimizerInfoGetAttribute(*(info_cutnn->cutnn_handle),
                   info_cutnn->cutnn_info,CUTENSORNET_CONTRACTION_OPTIMIZER_INFO_FLOP_COUNT,&flops,sizeof(flops)));
 return flops;
}


std::unique_ptr<ContractionSeqOptimizer> ContractionSeqOptimizerCutnn::createNew()
{
 return std::unique_ptr<ContractionSeqOptimizer>(new ContractionSeqOptimizerCutnn());
}


InfoCuTensorNet::InfoCuTensorNet(cutensornetHandle_t * handle,
                                 std::size_t memory_limit,
                                 std::size_t min_slices,
                                 const TensorNetwork & network):
 cutnn_handle(handle), workspace_size(memory_limit), minimum_slices(min_slices)
{
 parseTensorNetwork(network);
 HANDLE_CTN_ERROR(cutensornetCreateContractionOptimizerConfig(*cutnn_handle,&cutnn_config));
 HANDLE_CTN_ERROR(cutensornetContractionOptimizerConfigSetAttribute(*cutnn_handle,cutnn_config,
                   CUTENSORNET_CONTRACTION_OPTIMIZER_CONFIG_SLICER_MIN_SLICES,&minimum_slices,sizeof(minimum_slices)));
 HANDLE_CTN_ERROR(cutensornetCreateContractionOptimizerInfo(*cutnn_handle,cutnn_network,&cutnn_info));
 HANDLE_CTN_ERROR(cutensornetContractionOptimize(*cutnn_handle,cutnn_network,cutnn_config,workspace_size,cutnn_info));
}


InfoCuTensorNet::~InfoCuTensorNet()
{
 HANDLE_CTN_ERROR(cutensornetDestroyContractionOptimizerInfo(cutnn_info));
 HANDLE_CTN_ERROR(cutensornetDestroyContractionOptimizerConfig(cutnn_config));
 HANDLE_CTN_ERROR(cutensornetDestroyNetworkDescriptor(cutnn_network));
}


static cudaDataType_t getCudaDataType(const TensorElementType elem_type)
{
 cudaDataType_t cuda_data_type;
 switch(elem_type){
  case TensorElementType::REAL32: cuda_data_type = CUDA_R_32F; break;
  case TensorElementType::REAL64: cuda_data_type = CUDA_R_64F; break;
  case TensorElementType::COMPLEX32: cuda_data_type = CUDA_C_32F; break;
  case TensorElementType::COMPLEX64: cuda_data_type = CUDA_C_64F; break;
  default: assert(false);
 }
 return cuda_data_type;
}


static cutensornetComputeType_t getCutensorComputeType(const TensorElementType elem_type)
{
 cutensornetComputeType_t cutensor_data_type;
 switch(elem_type){
  case TensorElementType::REAL32: cutensor_data_type = CUTENSORNET_COMPUTE_32F; break;
  case TensorElementType::REAL64: cutensor_data_type = CUTENSORNET_COMPUTE_64F; break;
  case TensorElementType::COMPLEX32: cutensor_data_type = CUTENSORNET_COMPUTE_32F; break;
  case TensorElementType::COMPLEX64: cutensor_data_type = CUTENSORNET_COMPUTE_64F; break;
  default: assert(false);
 }
 return cutensor_data_type;
}


void InfoCuTensorNet::parseTensorNetwork(const TensorNetwork & network)
{
 const int32_t num_input_tensors = network.getNumTensors();

 tn_rep.num_modes_in = new int32_t[num_input_tensors];
 tn_rep.extents_in = new int64_t*[num_input_tensors];
 tn_rep.strides_in = new int64_t*[num_input_tensors];
 tn_rep.modes_in = new int32_t*[num_input_tensors];
 tn_rep.alignments_in = new uint32_t[num_input_tensors];

 for(unsigned int i = 0; i < num_input_tensors; ++i) tn_rep.strides_in[i] = NULL;
 for(unsigned int i = 0; i < num_input_tensors; ++i) tn_rep.alignments_in[i] = MEM_ALIGNMENT;
 tn_rep.strides_out = NULL;
 tn_rep.alignment_out = MEM_ALIGNMENT;

 int32_t mode_id = 0, tens_num = 0;
 for(auto iter = network.cbegin(); iter != network.cend(); ++iter){
  const auto tens_id = iter->first;
  const auto & tens = iter->second;
  const auto tens_hash = tens.getTensor()->getTensorHash();
  const auto tens_vol = tens.getTensor()->getVolume();
  const auto tens_rank = tens.getRank();
  const auto tens_type = tens.getElementType();
  if(tens_type == TensorElementType::VOID){
   std::cout << "#ERROR(exatn::numerics::contraction_seq_optimizer_cutnn): Network tensor #" << tens_id
             << " has not been allocated typed storage yet!\n";
   std::abort();
  }
  const auto & tens_legs = tens.getTensorLegs();
  const auto & tens_dims = tens.getDimExtents();

  auto res0 = tn_rep.tensor_descriptors.emplace(std::make_pair(tens_hash,TensDescr{}));
  if(res0.second){
   auto & descr = res0.first->second;
   descr.extents.resize(tens_rank);
   for(unsigned int i = 0; i < tens_rank; ++i) descr.extents[i] = tens_dims[i];
   descr.data_type = getCudaDataType(tens_type);
   descr.volume = tens_vol;
  }

  auto res1 = tn_rep.tensor_modes.emplace(std::make_pair(tens_id,std::vector<int32_t>(tens_rank)));
  assert(res1.second);
  for(unsigned int i = 0; i < tens_rank; ++i){
   const auto other_tens_id = tens_legs[i].getTensorId();
   const auto other_tens_leg_id = tens_legs[i].getDimensionId();
   auto other_tens_iter = tn_rep.tensor_modes.find(other_tens_id);
   if(other_tens_iter == tn_rep.tensor_modes.end()){
    res1.first->second[i] = ++mode_id;
    auto new_mode = tn_rep.mode_extents.emplace(std::make_pair(mode_id,tens_dims[i]));
   }else{
    res1.first->second[i] = other_tens_iter->second[other_tens_leg_id];
   }
  }

  if(tens_id == 0){ //output tensor
   tn_rep.num_modes_out = tens_rank;
   tn_rep.extents_out = res0.first->second.extents.data();
   tn_rep.modes_out = res1.first->second.data();
  }else{ //input tensors
   tn_rep.num_modes_in[tens_num] = tens_rank;
   tn_rep.extents_in[tens_num] = res0.first->second.extents.data();
   tn_rep.modes_in[tens_num] = res1.first->second.data();
   ++tens_num;
  }
 }

 const auto tens_elem_type = network.getTensorElementType();
 tn_rep.data_type = getCudaDataType(tens_elem_type);
 tn_rep.compute_type = getCutensorComputeType(tens_elem_type);

 //Create the cuTensorNet tensor network descriptor (not GPU specific):
 HANDLE_CTN_ERROR(cutensornetCreateNetworkDescriptor(*cutnn_handle,num_input_tensors,
                  tn_rep.num_modes_in,tn_rep.extents_in,tn_rep.strides_in,tn_rep.modes_in,tn_rep.alignments_in,
                  tn_rep.num_modes_out,tn_rep.extents_out,tn_rep.strides_out,tn_rep.modes_out,tn_rep.alignment_out,
                  tn_rep.data_type,tn_rep.compute_type,&cutnn_network));
 return;
}

} //namespace numerics

} //namespace exatn

#endif //CUQUANTUM
