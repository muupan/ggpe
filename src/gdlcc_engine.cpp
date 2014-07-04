#include <cassert>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <dlfcn.h>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include "gdlcc_engine.hpp"
#include "ggpe.hpp"

namespace ggpe {

namespace gdlcc {

namespace fs = boost::filesystem;

// Types of function pointers
typedef Tuple StrToTupleFunc(const std::string& str);
typedef std::string TupleToStrFunc(const Tuple& tuple);
typedef int StrToLiteralFunc(const std::string& str);
typedef std::string LiteralToStrFunc(const int literal);
typedef StateSp CreateInitialStateFunc();
//typedef State_p CreateStateFunc(const vector<Tuple> facts);
typedef int GetRoleCountFunc();

namespace {

// Loaded library
void *lib;

// Function pointers
StrToTupleFunc* str_to_tuple_func;
TupleToStrFunc* tuple_to_str_func;
StrToLiteralFunc* str_to_literal_func;
LiteralToStrFunc* literal_to_str_func;
CreateInitialStateFunc* create_initial_state_func;
//CreateStateFunc* create_state_func;
GetRoleCountFunc* get_role_count_func;


void *LoadFuncOrDie(void *lib, const std::string& func_name) {
  void* func = dlsym(lib, func_name.c_str());
  const char* dlsym_error = dlerror();
  if (dlsym_error) {
    std::cerr << "Cannot load symbol create: " << dlsym_error << std::endl;
    dlclose(lib);
    throw std::runtime_error("Failed to load a function from the shared library.");
  }
  assert(func);
  return func;
}

void *LoadLibOrDie(const std::string& path) {
  void *lib = dlopen(path.c_str(), RTLD_NOW);
  if (!lib) {
    std::cerr << "Cannot load library: " << dlerror() << std::endl;
    throw std::runtime_error("Failed to load a shared library.");
  }
  return lib;
}

void Link(const std::string& lib_path) {
  // Link library
  lib = LoadLibOrDie(lib_path);
  // Load pointers to necessary functions
  str_to_tuple_func = reinterpret_cast<StrToTupleFunc*>(LoadFuncOrDie(lib, "StrToTuple"));
  tuple_to_str_func = reinterpret_cast<TupleToStrFunc*>(LoadFuncOrDie(lib, "TupleToStr"));
  str_to_literal_func = reinterpret_cast<StrToLiteralFunc*>(LoadFuncOrDie(lib, "StrToLiteral"));
  literal_to_str_func = reinterpret_cast<LiteralToStrFunc*>(LoadFuncOrDie(lib, "LiteralToStr"));
  create_initial_state_func = reinterpret_cast<CreateInitialStateFunc*>(LoadFuncOrDie(lib, "CreateInitialState"));
//  create_state_func = reinterpret_cast<CreateStateFunc*>(LoadFuncOrDie(lib, "CreateState"));
}

void Delink() {
  dlclose(lib);
  lib = nullptr;
}

Tuple StrToTuple(const std::string& str) {
  return str_to_tuple_func(str);
}

std::string TupleToStr(const Tuple& tuple) {
  return tuple_to_str_func(tuple);
}

int StrToLiteral(const std::string& str) {
  return str_to_literal_func(str);
}

std::string LiteralToStr(const int literal) {
  return literal_to_str_func(literal);
}

//
//// Create a state with given facts
//State_p CreateState(const vector<Tuple> facts) {
//  return create_state_func(facts);
//}

void SaveKifFile(const std::string& kif_filename, const std::string& kif) {
  std::ofstream ofs(kif_filename);
  ofs << kif << std::flush;
}

/**
 * Convert a KIF file into C++ source codes:
 * <name>.kif -> <name>.cpp and <name>.h
 */
void ConvertKifToCpp(const std::string& kif_filename) {
  const auto convert_command = (boost::format("./gdlcc %1%") %
      kif_filename).str();
  std::cout << "Command: " << convert_command << std::endl;
  const auto ret = std::system(convert_command.c_str());
  if (ret != 0) {
//  if (ret != 0 || errno != 0) {
    throw std::runtime_error("Failed to convert KIF into C++.");
  }
}

void CompileCppIntoSharedLibrary(
    const std::string& cpp_filename,
    const std::string& lib_filename) {
  const auto header_include = std::string("-I./src -I.");
#ifdef NDEBUG
  const auto optimization_options = std::string("-O3 -march=native -DNDEBUG");
#else
  const auto optimization_options = std::string("-O0 -g");
#endif
  const auto compile_command =
      (boost::format("$CXX -std=c++11 %1% -I./include -I. %2% -shared -fPIC -o %3%") %
          optimization_options %
          cpp_filename %
          lib_filename).str();
  std::cout << "Command: " << compile_command << std::endl;
  const auto ret = std::system(compile_command.c_str());
//  if (ret != 0 || errno != 0) {
  if (ret != 0) {
    throw std::runtime_error("Failed to compile generated C++.");
  }
}

} // anonymous

void InitializeGDLCCEngine(
    const std::string& kif,
    const std::string& name,
    const bool reuses_existing_lib) {
  if (lib) {
    Delink();
  }
  const auto tmp_dir = std::string("tmp/");
  const auto kif_filename = tmp_dir + name + ".kif";
  const auto cpp_filename = tmp_dir + name + ".cpp";
  const auto lib_filename = tmp_dir + name + ".so";

  // Reuse old shared library if available
  if (reuses_existing_lib &&
      fs::exists(fs::path(kif_filename)) &&
      fs::exists(fs::path(lib_filename))) {
    // Check if descriptions are completely the same
    std::cout << "Old files exist." << std::endl;
    std::ifstream ifs(kif_filename);
    std::string old_kif((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
//    std::cout << "Old KIF file: " << old_kif << std::endl;
    if (kif == old_kif) {
      std::cout << "Reuse old shared library:" << lib_filename << std::endl;
      gdlcc::Link(lib_filename);
      return;
    }
  }
  SaveKifFile(kif_filename, kif);
  ConvertKifToCpp(kif_filename);
  CompileCppIntoSharedLibrary(cpp_filename, lib_filename);
  gdlcc::Link(lib_filename);
}

bool InitializeGDLCCEngineOrFalse(
    const std::string& kif,
    const std::string& name,
    const bool reuses_existing_lib) {
  try {
    InitializeGDLCCEngine(kif, name, true);
    return true;
  } catch (std::exception& e) {
    std::cerr << e.what() << std::endl;
    return false;
  } catch (...) {
    std::cerr << "some exception" << std::endl;
    return false;
  }
}

StateSp CreateInitialState() {
  assert(create_initial_state_func);
  return create_initial_state_func();
}

} // gdlcc
} // ggpe
