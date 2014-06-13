#include <random>
#include <vector>
#include <boost/timer/timer.hpp>
#include <ggpe/ggpe.hpp>

template <class T>
T SelectRandomly(const std::vector<T>& vector) {
//  std::mt19937 random_engine(std::random_device());
  std::mt19937 random_engine();
  std::uniform_int_distribution<int> dist(0, vector.size() - 1);
//  return vector[dist(random_engine)];
  return vector[0];
}

void SimulateOnce() {
  auto tmp_state = ggpe::State();
  while (!tmp_state.IsTerminal()) {
    ggpe::JointAction joint_action;
    for (const auto role_idx : ggpe::GetRoleIndices()) {
      joint_action.push_back(SelectRandomly(tmp_state.GetLegalActions()[role_idx]));
    }
    tmp_state = tmp_state.GetNextState(joint_action);
  }
}

void EvaluateSimulationSpeed(const int n) {
  std::cout << "Doing " << n << " simulations..." << std::endl;
  boost::timer::cpu_timer timer;
  for (auto i = 0; i < n; ++i) {
    SimulateOnce();
  }
  std::cout << timer.format() << std::endl;
}

int main() {
  std::vector<std::string> kif_files = {"breakthrough.kif", "skirmish.kif"};
  for (const auto& kif_file : kif_files) {
    ggpe::InitializeFromFile(kif_file);
    EvaluateSimulationSpeed(100);
  }
}
