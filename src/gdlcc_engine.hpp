#ifndef GDLCC_ENGINE_HPP_
#define GDLCC_ENGINE_HPP_

#include "ggpe.hpp"

namespace ggpe {
namespace gdlcc {

/**
 * Initialize GDLCC Engine:
 * 1) Convert KIF -> C++
 * 2) Compile C++
 * 3) Load as a shared library
 */
void InitializeGDLCCEngine(
    const std::string& kif,
    const std::string& name,
    const bool reuses_existing_lib);

StateSp CreateInitialState();

}
}

#endif /* GDLCC_ENGINE_HPP_ */
