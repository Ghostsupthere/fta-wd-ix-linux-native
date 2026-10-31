#include "log.h"
#include <cstdio>
#include <mutex>
namespace {std::mutex logMutex;FILE* output=nullptr;int logLevel=1;}
void LogOpen(const char* path){std::lock_guard<std::mutex> lock(logMutex);if(output)std::fclose(output);output=std::fopen(path,"w");}
void LogSetLevel(int level){logLevel=level;}
bool LogEnabled(LogLevel level){return int(level)<=logLevel;}
void LogWrite(LogLevel level,const char* format,...){if(!LogEnabled(level))return;std::lock_guard<std::mutex> lock(logMutex);if(!output)return;va_list args;va_start(args,format);std::vfprintf(output,format,args);va_end(args);std::fputc('\n',output);std::fflush(output);}
