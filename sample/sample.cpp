#include <random>
#include <vector>
#include <boost/timer/timer.hpp>
#include <boost/lexical_cast.hpp>
#include <ggpe/ggpe.hpp>
#include <glog/logging.h>

template <class T>
T SelectRandomly(const std::vector<T>& vector) {
  std::mt19937 random_engine(0);
  std::uniform_int_distribution<int> dist(0, vector.size() - 1);
  return vector[dist(random_engine)];
}

void SimulateOnce() {
  auto tmp_state = ggpe::CreateInitialState();
  while (!tmp_state->IsTerminal()) {
    ggpe::JointAction joint_action;
    for (const auto role_idx : ggpe::GetRoleIndices()) {
      joint_action.push_back(SelectRandomly(tmp_state->GetLegalActions()[role_idx]));
    }
    tmp_state = tmp_state->GetNextState(joint_action);
  }
}

std::string EvaluateSimulationSpeed(const int n) {
  std::cout << "Doing " << n << " simulations..." << std::endl;
  boost::timer::cpu_timer timer;
  for (auto i = 0; i < n; ++i) {
    SimulateOnce();
    std::cout << "." << std::flush;
  }
  std::cout << std::endl;
  return timer.format();
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: ./a.out <kif filename> <simulation count>" << std::endl;
    std::cerr << "Sample: ./a.out breakthrough.kif 1000" << std::endl;
    return 0;
  }
  google::InstallFailureSignalHandler();
  const auto kif_file = std::string(argv[1]);
  const auto simulation_count = boost::lexical_cast<int>(argv[2]);
  std::cout << "YAP without tabling:" << std::endl;
  ggpe::InitializeFromFile(kif_file, ggpe::EngineBackend::YAP, false);
  const auto yap_result = EvaluateSimulationSpeed(simulation_count);
  std::cout << "YAP with tabling:" << std::endl;
  ggpe::InitializeFromFile(kif_file, ggpe::EngineBackend::YAP, true);
  const auto yap_tab_result = EvaluateSimulationSpeed(simulation_count);
  std::cout << "GDLCC:" << std::endl;
  ggpe::InitializeFromFile(kif_file, ggpe::EngineBackend::GDLCC);
  if (ggpe::GetEngineBackend() == ggpe::EngineBackend::GDLCC) {
    const auto gdlcc_result = EvaluateSimulationSpeed(simulation_count);
    std::cout << kif_file << std::endl;
    std::cout << "GDLCC: " << gdlcc_result << std::endl;
    std::cout << "YAP without tabling: " << yap_result << std::endl;
    std::cout << "YAP with tabling: " << yap_tab_result << std::endl;
  } else {
    std::cout << kif_file << std::endl;
    std::cout << "YAP without tabling: " << yap_result << std::endl;
    std::cout << "YAP with tabling: " << yap_tab_result << std::endl;
  }
}
