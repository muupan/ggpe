/*
 * file_utils.hpp
 *
 *  Created on: 2014/06/26
 *      Author: User
 */

#ifndef FILE_UTILS_HPP_
#define FILE_UTILS_HPP_

#include <fstream>
#include <stdexcept>

namespace file_utils {

inline std::string LoadStringFromFile(const std::string& filename) {
  std::ifstream ifs(filename);
  if (!ifs) {
    throw std::runtime_error("File '" + filename + "' does not exists.");
  }
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

}

#endif /* FILE_UTILS_HPP_ */
