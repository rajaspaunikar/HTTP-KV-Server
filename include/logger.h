// logger.h
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <iostream>

void log_event(const std::string& event){
    std::cout << "[LOG] " << event << std::endl;
}

#endif // LOGGER_H