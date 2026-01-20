#pragma once
// ===================================================================================
// Logger.h - Simple file-based logger with level filtering and auto-rotation
// ===================================================================================

#include <fstream>
#include <mutex>
#include <string>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

// Undefine ERROR if defined by Windows headers to avoid conflict
#ifdef ERROR
#undef ERROR
#endif

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERR = 3, NONE = 4 };

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    // Initialize logger with level string (e.g., "DEBUG", "INFO", "WARN", "ERROR")
    // Logs at the specified level and above
    void init(const std::string& levelArg) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Parse level
        std::string level = levelArg;
        std::transform(level.begin(), level.end(), level.begin(),
                       [](unsigned char c) { return std::toupper(c); });

        if (level == "DEBUG") {
            m_minLevel = LogLevel::DEBUG;
        } else if (level == "INFO") {
            m_minLevel = LogLevel::INFO;
        } else if (level == "WARN" || level == "WARNING") {
            m_minLevel = LogLevel::WARN;
        } else if (level == "ERROR" || level == "ERR") {
            m_minLevel = LogLevel::ERR;
        } else {
            // Invalid level, disable logging
            m_minLevel = LogLevel::NONE;
            return;
        }

        // Create log file with timestamp
        openNewLogFile();
        m_initialized = true;

        // Log startup
        logInternal(LogLevel::INFO, "Logger initialized at level: " + levelArg);
    }

    void log(LogLevel level, const std::string& message) {
        if (!m_initialized || level < m_minLevel || level == LogLevel::NONE) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        // Check if we need to rotate (3 minutes = 180 seconds)
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_fileStartTime).count();
        if (elapsed >= 180) {
            logInternal(LogLevel::INFO, "Log rotation: creating new file after 3 minutes");
            m_file.close();
            openNewLogFile();
        }

        logInternal(level, message);
    }

    bool isEnabled() const {
        return m_initialized && m_minLevel != LogLevel::NONE;
    }

    bool isLevelEnabled(LogLevel level) const {
        return m_initialized && level >= m_minLevel && level != LogLevel::NONE;
    }

    // Shutdown logger cleanly
    void shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_initialized && m_file.is_open()) {
            logInternal(LogLevel::INFO, "Logger shutting down");
            m_file.close();
        }
        m_initialized = false;
    }

    ~Logger() {
        shutdown();
    }

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void openNewLogFile() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
#ifdef _WIN32
        localtime_s(&tm_now, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_now);
#endif

        std::ostringstream filename;
        filename << "joystickmidi_"
                 << std::put_time(&tm_now, "%Y-%m-%d_%H-%M-%S")
                 << ".log";

        m_file.open(filename.str(), std::ios::out | std::ios::app);
        m_fileStartTime = std::chrono::steady_clock::now();
        m_currentFilename = filename.str();
    }

    void logInternal(LogLevel level, const std::string& message) {
        if (!m_file.is_open()) return;

        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm_now;
#ifdef _WIN32
        localtime_s(&tm_now, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_now);
#endif

        m_file << "[" << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S")
               << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
               << "[" << std::setw(5) << std::left << levelToString(level) << "] "
               << message << std::endl;

        m_file.flush();
    }

    static const char* levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO";
            case LogLevel::WARN:  return "WARN";
            case LogLevel::ERR:   return "ERROR";
            default:              return "NONE";
        }
    }

    std::ofstream m_file;
    std::mutex m_mutex;
    LogLevel m_minLevel = LogLevel::NONE;
    bool m_initialized = false;
    std::chrono::steady_clock::time_point m_fileStartTime;
    std::string m_currentFilename;
};

// ===================================================================================
// Logging Macros - Use these throughout the codebase
// ===================================================================================

// Simple string logging
#define LOG_DEBUG(msg) Logger::instance().log(LogLevel::DEBUG, msg)
#define LOG_INFO(msg)  Logger::instance().log(LogLevel::INFO, msg)
#define LOG_WARN(msg)  Logger::instance().log(LogLevel::WARN, msg)
#define LOG_ERROR(msg) Logger::instance().log(LogLevel::ERR, msg)

// Stream-style logging helper
#define LOG_STREAM(level, expr) do { \
    if (Logger::instance().isLevelEnabled(level)) { \
        std::ostringstream _log_ss; \
        _log_ss << expr; \
        Logger::instance().log(level, _log_ss.str()); \
    } \
} while(0)

#define LOG_DEBUG_S(expr) LOG_STREAM(LogLevel::DEBUG, expr)
#define LOG_INFO_S(expr)  LOG_STREAM(LogLevel::INFO, expr)
#define LOG_WARN_S(expr)  LOG_STREAM(LogLevel::WARN, expr)
#define LOG_ERROR_S(expr) LOG_STREAM(LogLevel::ERR, expr)
