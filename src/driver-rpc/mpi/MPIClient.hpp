#ifndef EXATN_MPICLIENT_HPP_
#define EXATN_MPICLIENT_HPP_

#include "DriverClient.hpp"
#include "DriverServer.hpp"
#include "mpi.h"
#include <algorithm>
#include <functional>


namespace exatn {
namespace rpc {
namespace mpi {

class MPIClient : public DriverClient {

private:
  std::function<char()> randChar = []() -> char {
    const char charset[] = "0123456789"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[rand() % max_index];
  };

  const std::string generateRandomString(const int length = 10) {
    std::string str(length, 0);
    std::generate_n(str.begin(), length, randChar);
    return str;
  }

protected:
  MPI_Comm serverComm;

  std::map<std::string, MPI_Request> requests;

  std::map<std::string, int> jobId2NResults;
  
  std::vector<std::complex<double>> results;

public:
  MPIClient();

  // Send TaProl string, get a jobId string,
  // so this is an asynchronous call
  const std::string sendTAProL(const std::string taProlStr) override;

  // Retrieve result of job with given jobId.
  // Returns a scalar type double?
  const std::vector<std::complex<double>> retrieveResult(const std::string jobId) override;

  void shutdown() override;

  const std::string name() const override { return "mpi"; }

  const std::string description() const override {
    return "This DriverClient uses MPI to communicate with ExaTN Driver.";
  }
};

} // namespace mpi
} // namespace rpc
} // namespace exatn
#endif