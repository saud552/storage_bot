#pragma once

#include <string>
#include <memory>
#include <stdexcept>
#include <regex>
#include <optional>
#include <variant>
#include <unordered_map>
#include <atomic>
#include <chrono>

namespace StorageBot {

// =============== Error Handling System ===============
enum class ErrorCode {
    INVALID_CONFIG,
    MISSING_ENV_VAR,
    INVALID_TOKEN_FORMAT,
    DATABASE_CONNECTION_FAILED,
    ENCRYPTION_FAILED,
    BOT_ALREADY_EXISTS,
    BOT_NOT_FOUND,
    INSUFFICIENT_PERMISSIONS,
    RATE_LIMIT_EXCEEDED,
    INTERNAL_ERROR
};

class ConfigException : public std::runtime_error {
public:
    explicit ConfigException(ErrorCode code, const std::string& message)
        : std::runtime_error(message), errorCode_(code) {}
    
    ErrorCode getErrorCode() const { return errorCode_; }
    
private:
    ErrorCode errorCode_;
};

// =============== Configuration Validation ===============
class ConfigValidator {
public:
    static bool isValidToken(const std::string& token) {
        static const std::regex tokenRegex(R"(^[0-9]+:[a-zA-Z0-9_-]{35}$)");
        return std::regex_match(token, tokenRegex);
    }
    
    static bool isValidDatabaseUrl(const std::string& url) {
        return !url.empty() && url.length() > 10;
    }
    
    static bool isValidEncryptionKey(const std::string& key) {
        return key.length() >= 32;
    }
};

// =============== Environment Configuration ===============
class EnvironmentConfig {
public:
    static std::string getRequiredEnvVar(const std::string& name) {
        const char* value = std::getenv(name.c_str());
        if (!value || strlen(value) == 0) {
            throw ConfigException(ErrorCode::MISSING_ENV_VAR, 
                "Required environment variable not set: " + name);
        }
        return std::string(value);
    }
    
    static std::string getOptionalEnvVar(const std::string& name, 
                                        const std::string& defaultValue = "") {
        const char* value = std::getenv(name.c_str());
        return value ? std::string(value) : defaultValue;
    }
    
    static int getOptionalEnvVarAsInt(const std::string& name, int defaultValue = 0) {
        try {
            return std::stoi(getOptionalEnvVar(name, std::to_string(defaultValue)));
        } catch (const std::exception&) {
            return defaultValue;
        }
    }
};

// =============== Database Configuration ===============
struct DatabaseConfig {
    std::string server;
    std::string database;
    std::string username;
    std::string password;
    int port;
    int connectionPoolSize;
    int connectionTimeout;
    bool useSSL;
    
    static DatabaseConfig fromEnvironment() {
        DatabaseConfig config;
        config.server = EnvironmentConfig::getOptionalEnvVar("DB_SERVER", "localhost");
        config.database = EnvironmentConfig::getOptionalEnvVar("DB_NAME", "TelegramBots");
        config.username = EnvironmentConfig::getRequiredEnvVar("DB_USER");
        config.password = EnvironmentConfig::getRequiredEnvVar("DB_PASS");
        config.port = EnvironmentConfig::getOptionalEnvVarAsInt("DB_PORT", 1433);
        config.connectionPoolSize = EnvironmentConfig::getOptionalEnvVarAsInt("DB_POOL_SIZE", 15);
        config.connectionTimeout = EnvironmentConfig::getOptionalEnvVarAsInt("DB_TIMEOUT", 30);
        config.useSSL = EnvironmentConfig::getOptionalEnvVar("DB_SSL", "true") == "true";
        
        return config;
    }
    
    std::string getConnectionString() const {
        std::string connStr = "Driver={ODBC Driver 17 for SQL Server};"
                             "Server=" + server + ":" + std::to_string(port) + ";"
                             "Database=" + database + ";"
                             "UID=" + username + ";"
                             "PWD=" + password + ";";
        
        if (useSSL) {
            connStr += "Encrypt=yes;TrustServerCertificate=yes;";
        }
        
        return connStr;
    }
    
    void validate() const {
        if (!ConfigValidator::isValidDatabaseUrl(server)) {
            throw ConfigException(ErrorCode::INVALID_CONFIG, "Invalid database server");
        }
        if (connectionPoolSize <= 0 || connectionPoolSize > 100) {
            throw ConfigException(ErrorCode::INVALID_CONFIG, "Invalid connection pool size");
        }
    }
};

// =============== Security Configuration ===============
struct SecurityConfig {
    std::string encryptionKey;
    int keyRotationDays;
    bool enableAuditLog;
    std::string allowedAdminUsers;
    
    static SecurityConfig fromEnvironment() {
        SecurityConfig config;
        config.encryptionKey = EnvironmentConfig::getRequiredEnvVar("ENCRYPTION_KEY");
        config.keyRotationDays = EnvironmentConfig::getOptionalEnvVarAsInt("KEY_ROTATION_DAYS", 90);
        config.enableAuditLog = EnvironmentConfig::getOptionalEnvVar("ENABLE_AUDIT_LOG", "true") == "true";
        config.allowedAdminUsers = EnvironmentConfig::getOptionalEnvVar("ALLOWED_ADMIN_USERS", "");
        
        return config;
    }
    
    void validate() const {
        if (!ConfigValidator::isValidEncryptionKey(encryptionKey)) {
            throw ConfigException(ErrorCode::INVALID_CONFIG, "Invalid encryption key length");
        }
        if (keyRotationDays <= 0) {
            throw ConfigException(ErrorCode::INVALID_CONFIG, "Invalid key rotation days");
        }
    }
};

// =============== Bot Configuration ===============
struct BotConfig {
    std::string token;
    std::string name;
    std::string username;
    std::string encryptedToken;
    std::atomic<long> storedUsers{0};
    std::atomic<long> totalUsers{0};
    std::atomic<bool> isActive{true};
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point lastActivity;
    
    BotConfig() : createdAt(std::chrono::system_clock::now()),
                  lastActivity(std::chrono::system_clock::now()) {}
    
    void validate() const {
        if (!ConfigValidator::isValidToken(token)) {
            throw ConfigException(ErrorCode::INVALID_TOKEN_FORMAT, "Invalid bot token format");
        }
        if (name.empty() || username.empty()) {
            throw ConfigException(ErrorCode::INVALID_CONFIG, "Bot name and username required");
        }
    }
    
    void updateActivity() {
        lastActivity = std::chrono::system_clock::now();
    }
};

// =============== Application Configuration ===============
struct AppConfig {
    DatabaseConfig database;
    SecurityConfig security;
    std::string managerToken;
    int maxActiveBots;
    int batchSize;
    int batchTimeoutMs;
    bool enableMetrics;
    std::string logLevel;
    
    static AppConfig fromEnvironment() {
        AppConfig config;
        config.database = DatabaseConfig::fromEnvironment();
        config.security = SecurityConfig::fromEnvironment();
        config.managerToken = EnvironmentConfig::getRequiredEnvVar("MANAGER_BOT_TOKEN");
        config.maxActiveBots = EnvironmentConfig::getOptionalEnvVarAsInt("MAX_ACTIVE_BOTS", 50);
        config.batchSize = EnvironmentConfig::getOptionalEnvVarAsInt("BATCH_SIZE", 100);
        config.batchTimeoutMs = EnvironmentConfig::getOptionalEnvVarAsInt("BATCH_TIMEOUT_MS", 5000);
        config.enableMetrics = EnvironmentConfig::getOptionalEnvVar("ENABLE_METRICS", "true") == "true";
        config.logLevel = EnvironmentConfig::getOptionalEnvVar("LOG_LEVEL", "INFO");
        
        return config;
    }
    
    void validate() const {
        database.validate();
        security.validate();
        
        if (!ConfigValidator::isValidToken(managerToken)) {
            throw ConfigException(ErrorCode::INVALID_TOKEN_FORMAT, "Invalid manager token");
        }
        
        if (maxActiveBots <= 0 || maxActiveBots > 1000) {
            throw ConfigException(ErrorCode::INVALID_CONFIG, "Invalid max active bots");
        }
        
        if (batchSize <= 0 || batchSize > 10000) {
            throw ConfigException(ErrorCode::INVALID_CONFIG, "Invalid batch size");
        }
    }
};

} // namespace StorageBot