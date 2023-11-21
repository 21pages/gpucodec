#ifndef LOG_H
#define LOG_H

#include <string>

namespace gol {
void error(const std::string &message);
void warning(const std::string &message);
void info(const std::string &message);
void debug(const std::string &message);
void trace(const std::string &message);
} // namespace gol

#endif