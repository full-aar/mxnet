/*!
 *  Copyright (c) 2015 by Contributors
 * \file operator.h
 * \brief Operator interface of mxnet.
 * \author Naiyan Wang
 */
#ifndef MXNET_OPERATOR_H_
#define MXNET_OPERATOR_H_

#include <dmlc/base.h>
#include <vector>
#include <string>
#include <utility>
#include "./base.h"
#include "./context.h"

namespace mxnet {
/*! \brief operation request type to Forward and Backward */
enum OpReqType {
  /*! \brief no operation, do not write anything */
  kNullOp,
  /*! \brief write gradient to provided space */
  kWriteTo,
  /*!
   * \brief perform an inplace write,
   * Target shares memory with one of input arguments.
   * This option only happen when
   */
  kWriteInplace,
  /*! \brief add to the provided space */
  kAddTo
};

/*!
 * \brief All the possible information needed by Operator.Forward and Backward
 *  This is the superset of RunContext.
 *  We use this data structure to bookkeep everything needed by Forward and Backward.
 * \sa Resource
 */
struct OpContext {
  /*! \brief whether it is training phase */
  int is_train;
  /*! \brief RunContext related resources */
  RunContext run_ctx;
  /*! \brief Resources requested by the operator */
  std::vector<Resource> requested;
  /*!
   * \brief get mshadow stream from Context
   * \return the mshadow stream
   * \tparam xpu the device type of the stream
   */
  template<typename xpu>
  inline mshadow::Stream<xpu>* get_stream() const {
    return static_cast<mshadow::Stream<xpu>*>(run_ctx.stream);
  }
};

/*!
 * \brief Operator interface.
 *  Operator defins basic operation unit of optimized computation graph in mxnet.
 *  This interface relies on pre-allocated memory in TBlob, the caller need to set
 *  the memory region in TBlob correctly before calling Forward and Backward.
 *
 *  Operator is generated by OperatorProperty.
 *  To add new operator(aka. layers of neural nets) to mxnet, developer need to create
 *  a new OperatorProperty and its corresponding Operator.
 *
 * \sa TBlob, TShape, OperatorProperty
 */
class Operator {
 public:
  /*! \brief destructor */
  virtual ~Operator() {}
  /*!
   * \brief perform a forward operation of Operator, save the output to TBlob.
   * \param ctx runtime context available to this call
   * \param in_data array of input data, it is const
   * \param req the request types of saving operation, can only be kWriteTo or kWriteInplace.
   * \param out_data array of output data, pointer is used to indicate that this is holder
   *        the space of TBlob in out_data must be pre-allocated with InferShape
   * \sa OpReqType, OpContext
   */
  virtual void Forward(const OpContext &ctx,
                       const std::vector<TBlob> &in_data,
                       const std::vector<OpReqType> &req,
                       const std::vector<TBlob> &out_data) = 0;
  /*!
   * \brief Perform a Backward Operation, write gradient to the in_grad.
   *
   * Convention:
   *   out_grad.size() == OperatorProperty.NumVisibleReturns()
   *   out_data.size() == OperatorProperty.NumReturns()
   * out_data can contain additional invisible returns that remembers the
   * state carried from the Forward pass. For example mask in the dropout.
   *
   * The gradients are passed from visible returns in this function.
   *
   * \param ctx runtime context available to this call
   * \param out_grad the gradient value we get from of the Operator.
   * \param in_data the array of input data.
   * \param out_data the array of output data.
   * \param req request types of the saving operation, can be all types.
   * \param in_grad the array of gradient we need to write to.
   * \sa OpReqType, OpContext, OperatorProperty
   */
  virtual void Backward(const OpContext &ctx,
                        const std::vector<TBlob> &out_grad,
                        const std::vector<TBlob> &in_data,
                        const std::vector<TBlob> &out_data,
                        const std::vector<OpReqType> &req,
                        const std::vector<TBlob> &in_grad) = 0;
};

#if DMLC_USE_CXX11
// OperatorProperty allows C++11, while Operator do not rely on it.
/*!
 * \brief OperatorProperty is a object that stores all information about Operator.
 * It also contains method to generate context(device) specific operators.
 *
 * It also contains various functions that can be optimally overriden to
 * provide optimization chance for computation engine.
 */
class OperatorProperty {
 public:
  /*!
   * \brief virtual destructor
   */
  virtual ~OperatorProperty() {}
  /*!
   * \brief Get input arguments of the Operator.
   * \return vector of arguments.
   */
  virtual std::vector<std::string> ListArguments() const {
    return {"data"};
  }
  /*!
   * \brief Get name of return values of Operator
   * \return name of return values.
   */
  virtual std::vector<std::string> ListReturns() const {
    return {"output"};
  }
  /*! \return number of real return values of the Operator */
  virtual int NumReturns() const {
    return 1;
  }
  /*!
   * \brief get number of visible return values during Symbol creation.
   *  If NumVisibleReturns() = k, and NumReturns() = n.
   *  The first k returns will be presented in the resulting symbol.
   *
   *  The rest of the returns can be used for auxiliary states for Backward.
   *  For example, Dropout will return [data, mask], with NumVisibleReturns() == 1.
   *  So when user call sym = Dropout(input), only data is presented in sym.
   *  But all the returns will be presented in out_data parameter of Backward if requested.
   *
   * \return number of default return values
   */
  virtual int NumVisibleReturns() const {
    return NumReturns();
  }
  /*!
   *  \brief Set the parameters of the Operator.
   *  \param name parameter name
   *  \param val string for the configuration
   */
  virtual void SetParam(const char *name, const char *val) {}
  /*!
   * \brief infer the shapes of outputs and unknown input arguments
   * \param in_shape the shape of input arguments of the operator
   *     this should be of same length as the vector returned by DescribeArgs
   *     in_shape allows unknown elements, which are checked by shape.ndim() == 0.
   *     For unknown shapes, InferShape will try to fill in the correct Shape in in_shape
   *     For known shapes, InferShape will check shape consistency
   *
   *     common practice: set the shape of data input, and usually weight's shape can be infered
   *
   * \param out_shape the shape of outputs of the operator
   *     InferShape will modify the vector to fill output TShape
   * \return true if the shape inference is successful, false if there is not enough information.
   * \throws dmlc::Error if the known arg_shapes are inconsistent.
   */
  virtual bool InferShape(std::vector<TShape> *in_shape,
                          std::vector<TShape> *out_shape) const = 0;
  /*!
   * \brief Copy this OperatorProperty.
   * \return a pointer to the copied OperatorProperty
   */
  virtual OperatorProperty* Copy() const = 0;
  /*!
   * \brief Create a Operator on specific context
   */
  virtual Operator* CreateOperator(Context ctx) const = 0;
  /*!
   * \brief return the type string of the Operator
   *  subclasses override this function.
   */
  virtual std::string TypeString() const = 0;
  //--------------------------------------------------------
  // All the below functions are optional to override.
  //--------------------------------------------------------
  /*!
   * \brief Declare additional resource required in forward pass.
   *  These additional resources will be presented in OpContext.requested
   *  in the same order of the returned Resource.
   * \return Additional resource request
   */
  virtual std::vector<ResourceRequest> ForwardResource() const {
    return std::vector<ResourceRequest>();
  }
  /*!
   * \brief Decalre additional resource required in backward pass.
   *  These additional resources will be presented in OpContext.requested
   *  in the same order of the returned Resource.
   * \return Additional resource request
   */
  virtual std::vector<ResourceRequest> BackwardResource() const {
    return std::vector<ResourceRequest>();
  }
  /*!
   * \brief Declare the input requirement of Backward pass.
   *
   *  Only the returned list of variables will be used in Backward.
   *  This function is used for memory optimization.
   *  It is adviced to override and only return what is actually needed.
   *  If this function is not overriden, all the variables will be valid in Backward.
   *
   * \code
   *  // The following code declares Backward need out_grad[0], in_data[0],in_data[1]
   *  vector<int> BackwardInputs(const vector<int> &out_grad,
   *                             const vector<int> &in_data,
   *                             const vector<int> &out_data) const {
   *    return {out_grad[0], in_data[0], in_data[1]};
   *  }
   * \endcode
   * \param out_grad gradient of outputs in backward pass.
   * \param in_data the input data in forward pass.
   * \param out_data the output data in forward pass.
   * \return an integer vector indicating the input requirments
   * \sa BackwardInputs
   */
  virtual std::vector<int> DeclareBackwardDependency(
      const std::vector<int> &out_grad,
      const std::vector<int> &in_data,
      const std::vector<int> &out_data) const {
    // By default requires to see all the things.
    // remember to override this function to get a better performance.
    std::vector<int> ret = out_grad;
    ret.insert(ret.end(), in_data.begin(), in_data.end());
    ret.insert(ret.end(), out_data.begin(), out_data.end());
    return ret;
  }
  /*!
   * \brief Get possible forward inplace options.
   *  This function enables optimization to reuse memory of inputs in output.
   *  Only override when necessary, by default in-place is disabled.
   *
   * \code
   *  // The following code says out_data[0] can share data with in_data[0]
   *  vector<pair<int,int> > ForwardInplaceOption(const vector<int> &in_data,
   *                                              const vector<int> &out_data) const {
   *    return {{out_data[0], in_data[0]}};
   *  }
   * \endcode
   * \return list of pair of integers taken from the inputs vector,
   *   indicating possible in place operations.
   */
  virtual std::vector<std::pair<int, int> > ForwardInplaceOption(
      const std::vector<int> &in_data,
      const std::vector<int> &out_data) const {
    return std::vector<std::pair<int, int> >();
  }
  /*!
   * \brief Get possible backward inplace options.
   *  This function enables optimization to reuse memory of inputs in output.
   *  Only override when necessary, by default in-place is disabled.
   *
   * \code
   *  // The following code says in_grad[0] can share data with in_data[0]
   *  vector<pair<int,int> > BackwardInplaceOption(
   *                 const std::vector<int> &out_grad,
   *                 const std::vector<int> &in_data,
   *                 const std::vector<int> &out_data,
   *                 const std::vector<int> &in_grad) const {
   *    return {in_grad[0], in_data[0]}};
   *  }
   * \endcode
   * \return list of pair of integers taken from the inputs vector,
   *   indicating possible in place operations.
   */
  virtual std::vector<std::pair<int, int> > BackwardInplaceOption(
      const std::vector<int> &out_grad,
      const std::vector<int> &in_data,
      const std::vector<int> &out_data,
      const std::vector<int> &in_grad) const {
    return std::vector<std::pair<int, int> >();
  }
  /*!
   * \brief Get Backward Input Dependency for generic types of data.
   *  Normally T can be pointer of Symbol::DataEntry, or NArray.
   *  This function will select the result list of T according to DeclareBackwardDependency.
   *
   * \param in_data the input data in forward pass.
   * \param out_data the output data in forward pass.
   * \param out_grad gradient of outputs in backward pass.
   * \tparam T the generic type parameter.
   * \return vector of inputs the Backward Operation depends on.
   * \sa DeclareBackwardDependency
   */
  template<typename T>
  inline std::vector<T> BackwardInputs(const std::vector<T> &in_data,
                                       const std::vector<T> &out_data,
                                       const std::vector<T> &out_grad) const {
    int cnt = 0;
    std::vector<T> all_vec;
    std::vector<int> in_data_idx, out_data_idx, out_grad_idx;
    for (size_t i = 0; i < in_data.size(); ++i) {
      in_data_idx.push_back(cnt++);
      all_vec.push_back(in_data[i]);
    }
    for (size_t i = 0; i < out_data.size(); ++i) {
      out_data_idx.push_back(cnt++);
      all_vec.push_back(out_data[i]);
    }
    for (size_t i = 0; i < out_grad.size(); ++i) {
      out_grad_idx.push_back(cnt++);
      all_vec.push_back(out_data[i]);
    }
    std::vector<int> ret_idx = this->DeclareBackwardDependency(
        in_data_idx, out_data_idx, out_grad_idx);
    std::vector<T> ret;
    for (size_t i = 0; i < ret_idx.size(); ++i) {
      ret.push_back(all_vec[ret_idx[i]]);
    }
    return ret;
  }
  /*!
   * \brief create OperatorProperty
   * \param type_name the type string of the OperatorProperty
   * \return a new constructed OperatorProperty
   */
  static OperatorProperty *Create(const char* type_name);
};
#endif
}  // namespace mxnet
#endif  // MXNET_OPERATOR_H_
