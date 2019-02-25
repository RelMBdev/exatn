#ifndef EXATN_NUMERICS_BACKEND_HPP_
#define EXATN_NUMERICS_BACKEND_HPP_

#include "Identifiable.hpp"

#include <string>
#include <vector>
#include <memory>
#include <map>

#include "tensor_method.hpp"

namespace exatn {
namespace numerics {

class Backend : public Identifiable {
protected:

  std::map<std::string, std::shared_ptr<TensorMethod<Identifiable>>> methods;

public:

  virtual void addTensorMethod(std::shared_ptr<TensorMethod<Identifiable>> method) {
      methods.insert({method->name(), method});
  }

  virtual void execute(const std::string& taProl) = 0;

};

}
}

#endif