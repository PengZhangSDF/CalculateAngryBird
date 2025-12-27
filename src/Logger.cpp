// 日志系统实现
#include "Logger.hpp"

#include <iostream>
#include <filesystem>

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& logFilePath) {
    if (initialized_) {
        return; // 已经初始化，直接返回
    }
    
    // 打开日志文件（覆盖模式）
    logFile_.open(logFilePath, std::ios::out | std::ios::trunc);
    if (!logFile_.is_open()) {
        std::cerr << "警告: 无法打开日志文件: " << logFilePath << "\n";
        return;
    }
    
    initialized_ = true;
    info("=== 游戏日志系统初始化 ===");
    info("日志文件: " + logFilePath);
}

void Logger::log(const std::string& level, const std::string& message) {
    if (!initialized_ || !logFile_.is_open()) {
        // 如果日志系统未初始化，输出到控制台
        std::cerr << "[" << level << "] " << message << "\n";
        return;
    }
    
    std::string timeStr = getCurrentTime();
    logFile_ << "[" << timeStr << "] [" << level << "] " << message << std::endl;
    logFile_.flush(); // 立即刷新到文件
    
    // 同时输出到控制台
    std::cerr << "[" << timeStr << "] [" << level << "] " << message << "\n";
}

std::string Logger::getCurrentTime() const {
    auto now = std::time(nullptr);
    auto localTime = *std::localtime(&now);
    
    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void Logger::info(const std::string& message) {
    log("INFO", message);
}

void Logger::warning(const std::string& message) {
    log("WARN", message);
}

void Logger::error(const std::string& message) {
    log("ERROR", message);
}

void Logger::close() {
    if (initialized_ && logFile_.is_open()) {
        info("=== 游戏结束 ===");
        logFile_.close();
        initialized_ = false;
    }
}

Logger::~Logger() {
    close();
}

