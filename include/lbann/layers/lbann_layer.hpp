////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014-2016, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory.
// Written by the LBANN Research Team (B. Van Essen, et al.) listed in
// the CONTRIBUTORS file. <lbann-dev@llnl.gov>
//
// LLNL-CODE-697807.
// All rights reserved.
//
// This file is part of LBANN: Livermore Big Artificial Neural Network
// Toolkit. For details, see http://software.llnl.gov/LBANN or
// https://github.com/LLNL/LBANN.
//
// Licensed under the Apache License, Version 2.0 (the "Licensee"); you
// may not use this file except in compliance with the License.  You may
// obtain a copy of the License at:
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the license.
//
// lbann_layer .h .cpp - Parent class for all layer types
////////////////////////////////////////////////////////////////////////////////

#ifndef LBANN_LAYER_HPP_INCLUDED
#define LBANN_LAYER_HPP_INCLUDED

#include "lbann/lbann_base.hpp"
#include "lbann/lbann_comm.hpp"
#include "lbann/utils/lbann_summary.hpp"
#include "lbann/optimizers/lbann_optimizer.hpp"
#include "lbann/optimizers/lbann_optimizer_sgd.hpp"
#include "lbann/optimizers/lbann_optimizer_adagrad.hpp"
#include "lbann/optimizers/lbann_optimizer_rmsprop.hpp"
#include "lbann/optimizers/lbann_optimizer_adam.hpp"
#include "lbann/utils/lbann_exception.hpp"
#include "lbann/utils/cudnn_wrapper.hpp"
#include "lbann/utils/lbann_timer.hpp"
#include "lbann/io/lbann_persist.hpp"
#include <string>
#include <vector>

namespace lbann {

// Forward-declare this.
class model;

// @todo: check list of layer types
/**
 * Different layer types.
 */
enum class layer_type {
  fully_connected,
  convolution,
  softmax,
  activation,
  pooling,
  local_response_normalization,
  dropout,
  batch_normalization,
  input_distributed_minibatch,
  input_distributed_minibatch_parallel_io,
  input_partitioned_minibatch_parallel_io,
  target_distributed_minibatch,
  target_distributed_minibatch_parallel_io,
  target_partitioned_minibatch_parallel_io,
  reconstruction,
  INVALID
};

/**
 * Broad categories into which layers are divided.
 */
enum class layer_category {
  io,
  learning,
  activation,
  regularizer,
  transform,
  special,
  invalid
};


static layer_category __attribute__((used)) _layer_type_to_category(layer_type l) {
  switch(l) {
  case layer_type::fully_connected:
  case layer_type::convolution:
    return layer_category::learning;
  case layer_type::softmax:
  case layer_type::activation:
    return layer_category::activation;
  case layer_type::pooling:
    return layer_category::transform;
  case layer_type::local_response_normalization:
  case layer_type::dropout:
  case layer_type::batch_normalization:
    return layer_category::regularizer;
  case layer_type::input_distributed_minibatch:
  case layer_type::input_distributed_minibatch_parallel_io:
  case layer_type::input_partitioned_minibatch_parallel_io:
  case layer_type::target_distributed_minibatch:
  case layer_type::target_distributed_minibatch_parallel_io:
  case layer_type::target_partitioned_minibatch_parallel_io:
    return layer_category::io;
  case layer_type::reconstruction:
    return layer_category::special;
  case layer_type::INVALID:
    return layer_category::invalid;
  default:
    throw(std::string{} + __FILE__ + " " + std::to_string(__LINE__) + " Invalid layer_type specified");
  }
  return layer_category::invalid;
}


class Layer {
 public:
  Layer(const int index, lbann_comm *comm, int mbsize);

  virtual ~Layer(void);

  template <data_layout T_layout>
  void initialize_distributed_matrices();

  virtual void forwardProp(void);
  virtual void backProp(void);
  virtual bool update(void);
  virtual void summarize(lbann_summary& summarizer, int step);
  /**
   * Print information at the end of an epoch.
   * This is always called on the model masters and should synchronize
   * printing if needed.
   */
  virtual void epoch_print(void) const {}
  /**
   * Called on every layer at the end of each epoch to give it the chance to
   * reset/clean up.
   */
  virtual void epoch_reset(void) {}
  virtual DataType checkGradientMB(Layer& PrevLayer, DataType Epsilon=1e-4) {
    return 0.0;
  };

  virtual void setup(int);
  /** Validate that the setup is reasonable. */
  virtual void check_setup(void);

  /** Return the type of this layer. */
  inline layer_type get_type(void) const {
    return m_type;
  }

  /** Return this layer's name */
  inline std::string get_name() const { 
    return m_name;
  }

  /** Set the layer's name **/
  inline void set_name(std::string name) {
    m_name = name;
  }

  /** Return the index of this layer. */
  inline int get_index(void) const {
    return m_index;
  }
  /** Set the index of this layer. */
  inline void set_index(int i) {
    m_index = i;
  }
  /** Return the number of neurons of this layer. */
  inline int get_num_neurons(void) const {
    return m_num_neurons;
  }
  /** Return the execution mode of this layer */
  inline execution_mode get_execution_mode(void) const {
    return m_execution_mode;
  }
  /** Set the execution mode of this layer */
  inline void set_execution_mode(execution_mode mode) {
    m_execution_mode = mode;
  }
  /** Return the data layout of the given layer -- Every concrete
      layer has to overrride this with its T_layout template parameter */
  virtual inline data_layout get_data_layout() { return data_layout::MODEL_PARALLEL; };
  /** Return (a view of) the activations matrix for this layer. */
  virtual ElMat& get_activations(void) {
    return *m_activations;
  }
  /** Reset layer stat counters. */
  virtual void reset_counters(void) {
    fp_time = 0.0;
    bp_time = 0.0;
    update_time = 0.0;
  }

  /** Return the size of mini-batch this layer uses. */
  virtual int get_minibatch_size(void) const {
    return m_mini_batch_size;
  }
  /**
   * Get the "effective" size of a mini-batch.
   * This is for backward propagation, etc. when there are more updates being
   * contributed than the local mini-batch size implies (e.g. when doing
   * inter-model updates).
   */
  virtual int get_effective_minibatch_size(void) const {
    return m_effective_mbsize;
  }
  /** Set the effective size of a mini-batch to size. */
  virtual void set_effective_minibatch_size(int size) {
    m_effective_mbsize = size;
  }

  ElMat *fp_output(void);
  ElMat *bp_output(void);
  void setup_fp_input(ElMat *input);
  void setup_bp_input(ElMat *input);

  void set_prev_layer_type(layer_type type);
  void set_next_layer_type(layer_type type);
  bool using_gpus(void) const;
  void set_prev_layer_using_gpus(bool using_gpus);
  void set_next_layer_using_gpus(bool using_gpus);
#ifdef __LIB_CUDNN
  std::vector<DataType *> *fp_output_d(void);
  std::vector<DataType *> *bp_output_d(void);
  void setup_fp_input_d(std::vector<DataType *> *fp_input_d);
  void setup_bp_input_d(std::vector<DataType *> *bp_input_d);
#endif

  /** Return the neural network model of this layer. */
  inline model* get_neural_network_model(void) const {
    return m_neural_network_model;
  }
  /** Set the neural network model of this layer. */
  inline void set_neural_network_model(model* const m) {
    m_neural_network_model = m;
  }
  virtual El::Matrix<El::Int>* get_sample_indices_per_mb(void) { return nullptr; };

  virtual bool saveToFile(int fd, const char *filename) { return true; };
  virtual bool loadFromFile(int fd, const char *filename) { return true; };

  virtual bool saveToCheckpoint(int fd, const char *filename, size_t *bytes);
  virtual bool loadFromCheckpoint(int fd, const char *filename, size_t *bytes);

  virtual bool saveToCheckpointShared(persist& p);
  virtual bool loadFromCheckpointShared(persist& p);

 protected:
  int m_index;                 ///< Layer index (start with 0)

  std::string m_name;

  lbann_comm *m_comm;

  layer_type m_type;            ///< Type of this layer
  layer_type m_prev_layer_type; ///< Type of previous layer
  layer_type m_next_layer_type; ///< Type of next layer

  int m_num_neurons;           ///< Number of neurons
  int m_num_prev_neurons;      ///< Number of neurons in previous layer

  execution_mode  m_execution_mode;

 public:
  ElMat *m_prev_error_signal;   ///< Local copy of the error signal from "previous" layer ((# neurons) x mini-batch size)

  ElMat *m_activations;         ///< Activations - non-linearity applied to weighted sum ((# neurons) x mini-batch size)

  /// Create a view of each matrix so that it can accomodate partial mini-batches
  ElMat *m_prev_error_signal_v;
  ElMat *m_activations_v;

  ElMat *fp_input;              ///< Pointer to input for the forward propagation - no local storage
  ElMat *bp_input;              ///< Pointer to the input for the backward propagation - no local storage

  model *m_neural_network_model;

 protected:

  ElMat *m_error_signal;        ///< Error signal to "next" layer (i.e. deltas) ((# neurons) x mini-batch size)
  ElMat *m_prev_activations;    ///< Local copy of the activations from the "previous" layer ((# previous layer's neurons) x mini-batch size)

  ElMat *m_error_signal_v;
  ElMat *m_prev_activations_v;


 protected:

  /** Setup views of the matrices for the layer's forward propagation. */
  virtual void fp_set_std_matrix_view();
  /** Setup views of the matrices for the layer's backward propagation. */
  virtual void bp_set_std_matrix_view();
  /** Perform the layers work / main function for forward propagation */
  virtual void fp_compute() {}
  /** Perform the layers work / main function for backward propagation */
  virtual void bp_compute() {}
  /** Perform the layers work / main function for the update step */
  virtual bool update_compute() { return true; }

  /** Current layer is using GPUs. */
  bool m_using_gpus;
  /** Previous layer is using GPUs. */
  bool m_prev_layer_using_gpus;
  /** Next layer is using GPUs. */
  bool m_next_layer_using_gpus;

  /// cuDNN manager
  cudnn::cudnn_manager *m_cudnn;

#ifdef __LIB_CUDNN

  /** Forward propagation input uses pinned memory. */
  bool m_fp_input_pinned;
  /** Forward propagation output uses pinned memory. */
  bool m_fp_output_pinned;
  /** Backward propagation input uses pinned memory. */
  bool m_bp_input_pinned;
  /** Backward propagation output uses pinned memory. */
  bool m_bp_output_pinned;

  /** Number of mini-batch samples per GPU. */
  int m_mini_batch_size_per_gpu;

  /** GPU memory for activations from "previous" layer. */
  std::vector<DataType *> m_prev_activations_d;
  /** GPU memory for activations. */
  std::vector<DataType *> m_activations_d;
  /** GPU memory for error signal from "next" layer. */
  std::vector<DataType *> m_prev_error_signal_d;
  /** GPU memory for error signal. */
  std::vector<DataType *> m_error_signal_d;
  /** GPU memory for forward propagation input. */
  std::vector<DataType *> *fp_input_d;
  /** GPU memory for backward propagation input. */
  std::vector<DataType *> *bp_input_d;

#endif

  /** Size of the local mini-batch. */
  int m_mini_batch_size;
  /** "Effective" mini-batch size for backward propagation, etc.. */
  int m_effective_mbsize;

  /** Time spent in forward propagation. */
  double fp_time;
  /** Time spent in the forward propagation computation. */
  double fp_compute_time;
  /** Time spent in backward propagation. */
  double bp_time;
  /** Time spent in the backward propagation computation. */
  double bp_compute_time;
  /** Time spent in updates. */
  double update_time;
};
}

#endif // LBANN_LAYER_HPP_INCLUDED
