// 日志系统 - 记录游戏运行日志
#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <ctime>
#include <iomanip>

class Logger {
public:
    static Logger& getInstance();
    
    // 初始化日志系统
    void init(const std::string& logFilePath = "last_run.log");
    
    // 记录信息
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    
    // 关闭日志（程序退出时调用）
    void close();
    
    // 禁止拷贝
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    ~Logger();
    
    void log(const std::string& level, const std::string& message);
    std::string getCurrentTime() const;
    
    std::ofstream logFile_;
    bool initialized_{false};
};

