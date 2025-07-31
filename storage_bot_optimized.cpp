#include <iostream>
#include <string>
#include <map>
#include <thread>
#include <atomic>
#include <memory>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <stdexcept>
#include <tgbot/tgbot.h>
#include <nanodbc/nanodbc.h>
#include <crypto++/aes.h>
#include <crypto++/gcm.h>
#include <crypto++/base64.h>
#include <cstdlib>
#include <vector>
#include <functional>
#include <cctype>
#include <algorithm>
#include <regex>
#include <future>
#include <optional>
#include <shared_mutex>
#include <semaphore>
#include <barrier>
#include <system_error>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace std;
using namespace TgBot;
using namespace nanodbc;
using namespace CryptoPP;

// =============== إعدادات بيئة التشغيل ===============

namespace EnvironmentConfig {
    // حدود الموارد
    static constexpr size_t MAX_ACTIVE_BOTS = 50;
    static constexpr size_t MAX_CONCURRENT_TASKS = 100;
    static constexpr size_t BATCH_SIZE = 100;
    static constexpr size_t DB_POOL_SIZE = 10;
    static constexpr size_t MAX_MEMORY_USAGE_MB = 512;
    static constexpr size_t MAX_CPU_USAGE_PERCENT = 80;
    
    // إعدادات الشبكة
    static constexpr int WEBHOOK_TIMEOUT_SECONDS = 30;
    static constexpr int DB_CONNECTION_TIMEOUT_SECONDS = 5;
    static constexpr int RETRY_ATTEMPTS = 3;
    
    // إعدادات النظام
    static constexpr bool ENABLE_LOGGING = true;
    static constexpr bool ENABLE_METRICS = true;
    static constexpr bool ENABLE_HEALTH_CHECK = true;
}

// =============== واجهات OOP المحسنة ===============

// واجهة أساسية للكائنات القابلة للتكوين
class IConfigurable {
public:
    virtual void configure(const map<string, string>& config) = 0;
    virtual map<string, string> getConfiguration() const = 0;
    virtual ~IConfigurable() = default;
};

// واجهة أساسية للكائنات القابلة للمراقبة
class IMonitorable {
public:
    virtual map<string, double> getMetrics() const = 0;
    virtual bool isHealthy() const = 0;
    virtual string getStatus() const = 0;
    virtual ~IMonitorable() = default;
};

// واجهة أساسية للكائنات القابلة للإغلاق الآمن
class IShutdownable {
public:
    virtual void shutdown() = 0;
    virtual bool isShutdown() const = 0;
    virtual ~IShutdownable() = default;
};

// =============== هيكل تكوين البوت المحسن ===============

struct BotConfig : public IConfigurable {
    string token;
    string name;
    string username;
    string encryptedToken;
    atomic<long> storedUsers{0};
    atomic<long> totalUsers{0};
    atomic<bool> isActive{true};
    atomic<bool> isRunning{false};
    atomic<bool> isInitialized{false};
    future<void> botThread;
    promise<void> shutdownPromise;
    shared_ptr<barrier<>> initBarrier;
    
    // إعدادات الأداء
    size_t maxConcurrentUsers{1000};
    size_t messageQueueSize{1000};
    chrono::milliseconds processingTimeout{5000};
    
    void configure(const map<string, string>& config) override {
        if (config.count("max_concurrent_users")) {
            maxConcurrentUsers = stoul(config.at("max_concurrent_users"));
        }
        if (config.count("message_queue_size")) {
            messageQueueSize = stoul(config.at("message_queue_size"));
        }
        if (config.count("processing_timeout_ms")) {
            processingTimeout = chrono::milliseconds(stoul(config.at("processing_timeout_ms")));
        }
    }
    
    map<string, string> getConfiguration() const override {
        return {
            {"max_concurrent_users", to_string(maxConcurrentUsers)},
            {"message_queue_size", to_string(messageQueueSize)},
            {"processing_timeout_ms", to_string(processingTimeout.count())}
        };
    }
};

// =============== واجهات الخدمات المحسنة ===============

class IDatabaseManager : public IConfigurable, public IMonitorable, public IShutdownable {
public:
    virtual unique_ptr<connection> getConnection() = 0;
    virtual void releaseConnection(unique_ptr<connection> conn) = 0;
    virtual void executeTransaction(const function<void(connection&)>& func) = 0;
    virtual size_t getPoolSize() const = 0;
    virtual size_t getActiveConnections() const = 0;
    virtual ~IDatabaseManager() = default;
};

class IEncryptionService : public IConfigurable, public IMonitorable {
public:
    virtual string encrypt(const string& data) = 0;
    virtual string decrypt(const string& encryptedData) = 0;
    virtual bool isKeyValid() const = 0;
    virtual ~IEncryptionService() = default;
};

class IBotManager : public IConfigurable, public IMonitorable, public IShutdownable {
public:
    virtual bool startBot(const BotConfig& config) = 0;
    virtual bool stopBot(const string& encryptedToken) = 0;
    virtual bool pauseBot(const string& encryptedToken) = 0;
    virtual bool resumeBot(const string& encryptedToken) = 0;
    virtual map<string, BotConfig> getActiveBots() = 0;
    virtual size_t getTotalBots() const = 0;
    virtual size_t getActiveBotsCount() const = 0;
    virtual ~IBotManager() = default;
};

// =============== مدير قاعدة البيانات المحسن ===============

class DatabaseManager : public IDatabaseManager {
public:
    explicit DatabaseManager(const string& connStr, size_t poolSize = EnvironmentConfig::DB_POOL_SIZE) 
        : connectionString_(connStr), maxPoolSize_(poolSize) {
        initializePool();
    }

    ~DatabaseManager() override {
        shutdown();
    }

    unique_ptr<connection> getConnection() override {
        unique_lock<mutex> lock(poolMutex_);
        
        for (int attempts = 0; attempts < EnvironmentConfig::RETRY_ATTEMPTS; ++attempts) {
            if (availableConnections_.empty()) {
                if (totalConnections_ < maxPoolSize_) {
                    auto conn = createNewConnection();
                    if (conn) return conn;
                }
                
                if (!connectionCV_.wait_for(lock, 
                    chrono::seconds(EnvironmentConfig::DB_CONNECTION_TIMEOUT_SECONDS),
                    [this] { return !availableConnections_.empty() || shutdownFlag_; })) {
                    continue;
                }
            }
            
            if (!availableConnections_.empty()) {
                auto conn = move(availableConnections_.front());
                availableConnections_.pop();
                
                if (isConnectionValid(*conn)) {
                    return conn;
                } else {
                    totalConnections_--;
                }
            }
        }
        
        throw runtime_error("فشل في الحصول على اتصال قاعدة البيانات");
    }

    void releaseConnection(unique_ptr<connection> conn) override {
        if (!conn) return;
        
        lock_guard<mutex> lock(poolMutex_);
        if (!shutdownFlag_ && isConnectionValid(*conn)) {
            availableConnections_.push(move(conn));
            connectionCV_.notify_one();
        } else {
            totalConnections_--;
        }
    }

    void executeTransaction(const function<void(connection&)>& func) override {
        auto conn = getConnection();
        try {
            conn->begin();
            func(*conn);
            conn->commit();
        } catch (...) {
            conn->rollback();
            throw;
        }
        releaseConnection(move(conn));
    }

    void configure(const map<string, string>& config) override {
        lock_guard<mutex> lock(poolMutex_);
        if (config.count("pool_size")) {
            size_t newSize = stoul(config.at("pool_size"));
            if (newSize > maxPoolSize_) {
                maxPoolSize_ = newSize;
            }
        }
    }

    map<string, string> getConfiguration() const override {
        return {
            {"pool_size", to_string(maxPoolSize_)},
            {"connection_string", connectionString_}
        };
    }

    map<string, double> getMetrics() const override {
        lock_guard<mutex> lock(poolMutex_);
        return {
            {"total_connections", static_cast<double>(totalConnections_)},
            {"available_connections", static_cast<double>(availableConnections_.size())},
            {"pool_utilization", static_cast<double>(totalConnections_) / maxPoolSize_}
        };
    }

    bool isHealthy() const override {
        lock_guard<mutex> lock(poolMutex_);
        return !shutdownFlag_ && totalConnections_ > 0;
    }

    string getStatus() const override {
        lock_guard<mutex> lock(poolMutex_);
        if (shutdownFlag_) return "shutdown";
        if (totalConnections_ == 0) return "no_connections";
        return "healthy";
    }

    void shutdown() override {
        lock_guard<mutex> lock(poolMutex_);
        shutdownFlag_ = true;
        connectionCV_.notify_all();
        
        while (!availableConnections_.empty()) {
            availableConnections_.pop();
        }
    }

    bool isShutdown() const override {
        return shutdownFlag_;
    }

    size_t getPoolSize() const override {
        return maxPoolSize_;
    }

    size_t getActiveConnections() const override {
        lock_guard<mutex> lock(poolMutex_);
        return totalConnections_ - availableConnections_.size();
    }

private:
    void initializePool() {
        lock_guard<mutex> lock(poolMutex_);
        for (size_t i = 0; i < min(size_t(5), maxPoolSize_); ++i) {
            auto conn = createNewConnection();
            if (conn) {
                availableConnections_.push(move(conn));
            }
        }
    }

    unique_ptr<connection> createNewConnection() {
        try {
            auto conn = make_unique<connection>(connectionString_);
            if (isConnectionValid(*conn)) {
                totalConnections_++;
                return conn;
            }
        } catch (const exception& e) {
            cerr << "خطأ في إنشاء اتصال قاعدة البيانات: " << e.what() << endl;
        }
        return nullptr;
    }

    bool isConnectionValid(connection& conn) {
        try {
            statement stmt(conn, "SELECT 1");
            stmt.execute();
            return true;
        } catch (...) {
            return false;
        }
    }

    const string connectionString_;
    size_t maxPoolSize_;
    atomic<size_t> totalConnections_{0};
    atomic<bool> shutdownFlag_{false};
    mutable mutex poolMutex_;
    condition_variable connectionCV_;
    queue<unique_ptr<connection>> availableConnections_;
};

// =============== خدمة التشفير المحسنة ===============

class EncryptionService : public IEncryptionService {
public:
    EncryptionService() {
        loadEncryptionKey();
    }

    string encrypt(const string& data) override {
        if (data.empty()) return "";
        
        try {
            string ciphertext;
            SecByteBlock iv(EnvironmentConfig::IV_LENGTH);
            prng_.GenerateBlock(iv, iv.size());

            GCM<AES>::Encryption encryptor;
            encryptor.SetKeyWithIV(key_, key_.size(), iv, iv.size());

            StringSource ss(data, true,
                new AuthenticatedEncryptionFilter(encryptor,
                    new StringSink(ciphertext)
                )
            );

            string result = base64Encode(string(reinterpret_cast<const char*>(iv.data()), iv.size()) + ciphertext);
            encryptionCount_++;
            return result;
        } catch (const exception& e) {
            cerr << "خطأ في التشفير: " << e.what() << endl;
            throw runtime_error("فشل في تشفير البيانات");
        }
    }

    string decrypt(const string& encryptedData) override {
        if (encryptedData.empty()) return "";
        
        try {
            string decoded = base64Decode(encryptedData);
            if (decoded.size() < EnvironmentConfig::IV_LENGTH) {
                throw runtime_error("بيانات مشفرة غير صالحة");
            }

            SecByteBlock iv(reinterpret_cast<const byte*>(decoded.data()), EnvironmentConfig::IV_LENGTH);
            string ciphertext = decoded.substr(EnvironmentConfig::IV_LENGTH);

            GCM<AES>::Decryption decryptor;
            decryptor.SetKeyWithIV(key_, key_.size(), iv, iv.size());

            string plaintext;
            StringSource ss(ciphertext, true,
                new AuthenticatedDecryptionFilter(decryptor,
                    new StringSink(plaintext)
                )
            );

            decryptionCount_++;
            return plaintext;
        } catch (const exception& e) {
            cerr << "خطأ في فك التشفير: " << e.what() << endl;
            throw runtime_error("فشل في فك تشفير البيانات");
        }
    }

    void configure(const map<string, string>& config) override {
        if (config.count("encryption_key")) {
            loadEncryptionKeyFromString(config.at("encryption_key"));
        }
    }

    map<string, string> getConfiguration() const override {
        return {
            {"key_length", to_string(EnvironmentConfig::KEY_LENGTH)},
            {"iv_length", to_string(EnvironmentConfig::IV_LENGTH)}
        };
    }

    map<string, double> getMetrics() const override {
        return {
            {"encryption_count", static_cast<double>(encryptionCount_)},
            {"decryption_count", static_cast<double>(decryptionCount_)},
            {"total_operations", static_cast<double>(encryptionCount_ + decryptionCount_)}
        };
    }

    bool isHealthy() const override {
        return isKeyValid();
    }

    string getStatus() const override {
        return isKeyValid() ? "healthy" : "invalid_key";
    }

    bool isKeyValid() const override {
        return key_.size() == EnvironmentConfig::KEY_LENGTH;
    }

private:
    void loadEncryptionKey() {
        const char* envKey = getenv("ENCRYPTION_KEY");
        if (envKey && strlen(envKey) > 0) {
            loadEncryptionKeyFromString(envKey);
        } else {
            generateNewKey();
        }
    }

    void loadEncryptionKeyFromString(const string& keyStr) {
        try {
            string decoded = base64Decode(keyStr);
            if (decoded.size() == EnvironmentConfig::KEY_LENGTH) {
                key_.Assign(reinterpret_cast<const byte*>(decoded.data()), decoded.size());
            } else {
                generateNewKey();
            }
        } catch (...) {
            generateNewKey();
        }
    }

    void generateNewKey() {
        key_.Resize(EnvironmentConfig::KEY_LENGTH);
        prng_.GenerateBlock(key_, key_.size());
        cerr << "تحذير: تم إنشاء مفتاح تشفير جديد. يرجى تعيين ENCRYPTION_KEY" << endl;
    }

    string base64Encode(const string& data) {
        string encoded;
        StringSource ss(data, true,
            new Base64Encoder(
                new StringSink(encoded),
                false
            )
        );
        return encoded;
    }

    string base64Decode(const string& encoded) {
        string decoded;
        StringSource ss(encoded, true,
            new Base64Decoder(
                new StringSink(decoded)
            )
        );
        return decoded;
    }

    static constexpr size_t KEY_LENGTH = 32;  // AES-256
    static constexpr size_t IV_LENGTH = 12;   // GCM recommended IV size
    
    SecByteBlock key_;
    AutoSeededRandomPool prng_;
    atomic<size_t> encryptionCount_{0};
    atomic<size_t> decryptionCount_{0};
};

// =============== مدير البوتات المحسن ===============

class BotManager : public IBotManager {
public:
    BotManager(shared_ptr<IDatabaseManager> db, shared_ptr<IEncryptionService> encryptor)
        : dbManager_(db), encryptor_(encryptor), 
          taskSemaphore_(EnvironmentConfig::MAX_CONCURRENT_TASKS),
          batchProcessor_(async(launch::async, [this]() { batchProcessorLoop(); })) {}

    ~BotManager() override {
        shutdown();
    }

    bool startBot(const BotConfig& config) override {
        unique_lock<shared_mutex> lock(botsMutex_);
        
        if (activeBots_.size() >= EnvironmentConfig::MAX_ACTIVE_BOTS) {
            cerr << "تم الوصول للحد الأقصى من البوتات النشطة" << endl;
            return false;
        }

        string token;
        try {
            token = encryptor_->decrypt(config.encryptedToken);
        } catch (const exception& e) {
            cerr << "خطأ في فك تشفير التوكن: " << e.what() << endl;
            return false;
        }

        // التحقق من صحة التوكن
        try {
            Bot testBot(token);
            auto me = testBot.getApi().getMe();
            if (!me) {
                cerr << "توكن البوت غير صالح" << endl;
                return false;
            }
        } catch (const exception& e) {
            cerr << "خطأ في التحقق من التوكن: " << e.what() << endl;
            return false;
        }

        BotConfig botConfig = config;
        botConfig.initBarrier = make_shared<barrier<>>(2);
        
        auto future = async(launch::async, [this, botConfig]() {
            runBotInstance(botConfig);
        });
        
        botConfig.botThread = move(future);
        activeBots_[config.encryptedToken] = botConfig;
        
        // انتظار تهيئة البوت
        botConfig.initBarrier->arrive_and_wait();
        
        return true;
    }

    bool stopBot(const string& encryptedToken) override {
        unique_lock<shared_mutex> lock(botsMutex_);
        
        auto it = activeBots_.find(encryptedToken);
        if (it == activeBots_.end()) {
            return false;
        }

        it->second.shutdownPromise.set_value();
        
        if (it->second.botThread.valid()) {
            auto status = it->second.botThread.wait_for(chrono::seconds(10));
            if (status == future_status::timeout) {
                cerr << "تحذير: انتهت مهلة إيقاف البوت" << endl;
            }
        }

        activeBots_.erase(it);
        return true;
    }

    bool pauseBot(const string& encryptedToken) override {
        shared_lock<shared_mutex> lock(botsMutex_);
        
        auto it = activeBots_.find(encryptedToken);
        if (it != activeBots_.end()) {
            it->second.isActive = false;
            return true;
        }
        return false;
    }

    bool resumeBot(const string& encryptedToken) override {
        shared_lock<shared_mutex> lock(botsMutex_);
        
        auto it = activeBots_.find(encryptedToken);
        if (it != activeBots_.end()) {
            it->second.isActive = true;
            return true;
        }
        return false;
    }

    map<string, BotConfig> getActiveBots() override {
        shared_lock<shared_mutex> lock(botsMutex_);
        return activeBots_;
    }

    void configure(const map<string, string>& config) override {
        lock_guard<mutex> lock(configMutex_);
        configuration_ = config;
    }

    map<string, string> getConfiguration() const override {
        lock_guard<mutex> lock(configMutex_);
        return configuration_;
    }

    map<string, double> getMetrics() const override {
        shared_lock<shared_mutex> lock(botsMutex_);
        return {
            {"active_bots", static_cast<double>(activeBots_.size())},
            {"total_bots", static_cast<double>(totalBots_)},
            {"queue_size", static_cast<double>(messageQueue_.size())},
            {"processing_rate", processingRate_}
        };
    }

    bool isHealthy() const override {
        return !shutdownFlag_ && activeBots_.size() <= EnvironmentConfig::MAX_ACTIVE_BOTS;
    }

    string getStatus() const override {
        if (shutdownFlag_) return "shutdown";
        if (activeBots_.size() >= EnvironmentConfig::MAX_ACTIVE_BOTS) return "at_capacity";
        return "healthy";
    }

    void shutdown() override {
        shutdownFlag_ = true;
        messageQueueCV_.notify_all();
        
        if (batchProcessor_.valid()) {
            batchProcessor_.wait();
        }
        
        // إيقاف جميع البوتات
        shared_lock<shared_mutex> lock(botsMutex_);
        for (auto& [token, config] : activeBots_) {
            config.shutdownPromise.set_value();
        }
    }

    bool isShutdown() const override {
        return shutdownFlag_;
    }

    size_t getTotalBots() const override {
        return totalBots_;
    }

    size_t getActiveBotsCount() const override {
        shared_lock<shared_mutex> lock(botsMutex_);
        return activeBots_.size();
    }

private:
    void runBotInstance(const BotConfig& config) {
        try {
            string token = encryptor_->decrypt(config.encryptedToken);
            Bot bot(token);
            
            setupBotHandlers(bot, config);
            
            string webhookUrl = getenv("WEBHOOK_URL") ?: "https://your-domain.com/webhook";
            string webhookPath = "/" + config.encryptedToken;
            
            TgWebhook webhook(bot, webhookUrl + webhookPath);
            
            config.initBarrier->arrive_and_wait();
            webhook.start();
            
        } catch (const exception& e) {
            cerr << "خطأ في تشغيل البوت: " << e.what() << endl;
            config.initBarrier->arrive_and_wait();
        }
    }

    void setupBotHandlers(Bot& bot, const BotConfig& config) {
        bot.getEvents().onAnyMessage([this, &config](Message::Ptr message) {
            if (!config.isActive) return;
            
            string username = message->from->username;
            if (username.empty()) {
                username = "user_" + to_string(message->from->id);
            }
            
            addMessageToQueue(config.encryptedToken, message->from->id, username);
        });

        bot.getEvents().onCommand("start", [this, &config](Message::Ptr message) {
            if (!config.isActive) return;
            
            string username = message->from->username;
            if (username.empty()) {
                username = "user_" + to_string(message->from->id);
            }
            
            addMessageToQueue(config.encryptedToken, message->from->id, username);
        });
    }

    void addMessageToQueue(const string& encryptedToken, int64_t userId, const string& username) {
        taskSemaphore_.acquire();
        
        {
            lock_guard<mutex> lock(messageQueueMutex_);
            messageQueue_.push({encryptedToken, userId, username});
        }
        
        messageQueueCV_.notify_one();
    }

    void batchProcessorLoop() {
        vector<MessageData> batch;
        batch.reserve(EnvironmentConfig::BATCH_SIZE);
        
        while (!shutdownFlag_) {
            {
                unique_lock<mutex> lock(messageQueueMutex_);
                messageQueueCV_.wait_for(lock, chrono::seconds(5), 
                    [this] { return !messageQueue_.empty() || shutdownFlag_; });
                
                while (!messageQueue_.empty() && batch.size() < EnvironmentConfig::BATCH_SIZE) {
                    batch.push_back(messageQueue_.front());
                    messageQueue_.pop();
                }
            }
            
            if (!batch.empty()) {
                processBatch(batch);
                batch.clear();
            }
        }
    }

    void processBatch(const vector<MessageData>& batch) {
        try {
            dbManager_->executeTransaction([this, &batch](connection& conn) {
                processBatchInTransaction(conn, batch);
            });
            
            updateBotStats(batch);
            
        } catch (const exception& e) {
            cerr << "خطأ في معالجة الدفعة: " << e.what() << endl;
        }
    }

    void processBatchInTransaction(connection& conn, const vector<MessageData>& batch) {
        for (const auto& msg : batch) {
            updateUserRecords(conn, msg.encryptedToken, msg.userId, msg.username);
        }
    }

    void updateUserRecords(connection& conn, const string& botToken, 
                          int64_t userId, const string& username) {
        try {
            statement stmt(conn);
            stmt.prepare("MERGE INTO Users AS target "
                        "USING (SELECT ? as BotToken, ? as UserID, ? as Username) AS source "
                        "ON target.BotToken = source.BotToken AND target.UserID = source.UserID "
                        "WHEN MATCHED THEN "
                        "  UPDATE SET Username = source.Username, LastSeen = GETDATE() "
                        "WHEN NOT MATCHED THEN "
                        "  INSERT (BotToken, UserID, Username, FirstSeen, LastSeen) "
                        "  VALUES (source.BotToken, source.UserID, source.Username, GETDATE(), GETDATE())");
            
            stmt.bind(0, botToken.c_str());
            stmt.bind(1, userId);
            stmt.bind(2, username.c_str());
            stmt.execute();
            
        } catch (const exception& e) {
            cerr << "خطأ في تحديث سجل المستخدم: " << e.what() << endl;
        }
    }

    void updateBotStats(const vector<MessageData>& batch) {
        shared_lock<shared_mutex> lock(botsMutex_);
        
        for (const auto& msg : batch) {
            auto it = activeBots_.find(msg.encryptedToken);
            if (it != activeBots_.end()) {
                it->second.totalUsers++;
            }
        }
    }

    struct MessageData {
        string encryptedToken;
        int64_t userId;
        string username;
    };

    shared_ptr<IDatabaseManager> dbManager_;
    shared_ptr<IEncryptionService> encryptor_;
    mutable shared_mutex botsMutex_;
    map<string, BotConfig> activeBots_;
    
    // إدارة الرسائل
    mutex messageQueueMutex_;
    condition_variable messageQueueCV_;
    queue<MessageData> messageQueue_;
    future<void> batchProcessor_;
    
    // إدارة المهام
    counting_semaphore<EnvironmentConfig::MAX_CONCURRENT_TASKS> taskSemaphore_;
    atomic<bool> shutdownFlag_{false};
    
    // الإحصائيات
    atomic<size_t> totalBots_{0};
    atomic<double> processingRate_{0.0};
    map<string, string> configuration_;
    mutable mutex configMutex_;
};

// =============== واجهة التحكم المحسنة ===============

class ControlPanel : public IConfigurable, public IMonitorable, public IShutdownable {
public:
    ControlPanel(shared_ptr<IBotManager> botManager, 
                shared_ptr<IEncryptionService> encryptor,
                const string& managerToken)
        : botManager_(botManager), encryptor_(encryptor),
          managerBot_(make_unique<Bot>(managerToken)) {}

    void start() {
        setupHandlers();
        runEventLoop();
    }

    void configure(const map<string, string>& config) override {
        lock_guard<mutex> lock(configMutex_);
        configuration_ = config;
    }

    map<string, string> getConfiguration() const override {
        lock_guard<mutex> lock(configMutex_);
        return configuration_;
    }

    map<string, double> getMetrics() const override {
        return {
            {"manager_bot_status", managerBot_ ? 1.0 : 0.0},
            {"total_commands_processed", static_cast<double>(commandsProcessed_)}
        };
    }

    bool isHealthy() const override {
        return managerBot_ != nullptr && !shutdownFlag_;
    }

    string getStatus() const override {
        if (shutdownFlag_) return "shutdown";
        return managerBot_ ? "running" : "not_initialized";
    }

    void shutdown() override {
        shutdownFlag_ = true;
    }

    bool isShutdown() const override {
        return shutdownFlag_;
    }

private:
    void setupHandlers() {
        managerBot_->getEvents().onCommand("start", [this](Message::Ptr message) {
            sendMainMenu(message->chat->id);
        });

        managerBot_->getEvents().onCallbackQuery([this](CallbackQuery::Ptr query) {
            handleCallback(query);
        });

        managerBot_->getEvents().onAnyMessage([this](Message::Ptr message) {
            handleTextMessage(message);
        });
    }

    void runEventLoop() {
        try {
            string webhookUrl = getenv("MANAGER_WEBHOOK_URL") ?: "https://your-domain.com/manager";
            TgWebhook webhook(*managerBot_, webhookUrl);
            webhook.start();
        } catch (const exception& e) {
            cerr << "خطأ في بوت المدير: " << e.what() << endl;
        }
    }

    void sendMainMenu(int64_t chatId) {
        auto keyboard = make_shared<InlineKeyboardMarkup>();
        
        vector<InlineKeyboardButton::Ptr> row0;
        auto addBtn = make_shared<InlineKeyboardButton>();
        addBtn->text = "➕ إضافة بوت";
        addBtn->callbackData = "add_bot";
        row0.push_back(addBtn);

        vector<InlineKeyboardButton::Ptr> row1;
        auto listBtn = make_shared<InlineKeyboardButton>();
        listBtn->text = "📋 قائمة البوتات";
        listBtn->callbackData = "list_bots:0";
        row1.push_back(listBtn);

        vector<InlineKeyboardButton::Ptr> row2;
        auto statsBtn = make_shared<InlineKeyboardButton>();
        statsBtn->text = "📊 الإحصائيات";
        statsBtn->callbackData = "stats";
        row2.push_back(statsBtn);

        keyboard->inlineKeyboard = {row0, row1, row2};
        
        managerBot_->getApi().sendMessage(chatId, 
            "مرحبًا بك في نظام إدارة بوتات التخزين\n\n"
            "اختر أحد الخيارات:",
            false, 0, keyboard);
    }

    void handleCallback(CallbackQuery::Ptr query) {
        const string& data = query->data;
        commandsProcessed_++;
        
        if (data == "add_bot") {
            managerBot_->getApi().sendMessage(query->message->chat->id, 
                "أرسل توكن البوت الجديد:");
        } else if (data == "stats") {
            showStats(query);
        }
    }

    void handleTextMessage(Message::Ptr message) {
        commandsProcessed_++;
        
        if (message->text.find("bot") != string::npos) {
            string token = message->text;
            
            try {
                Bot testBot(token);
                auto me = testBot.getApi().getMe();
                if (!me) {
                    managerBot_->getApi().sendMessage(message->chat->id, "❌ توكن البوت غير صالح");
                    return;
                }
                
                BotConfig config;
                config.token = token;
                config.name = me->firstName;
                config.username = me->username;
                config.encryptedToken = encryptor_->encrypt(token);
                
                if (botManager_->startBot(config)) {
                    managerBot_->getApi().sendMessage(message->chat->id, 
                        "✅ تم إضافة البوت بنجاح: " + config.name);
                } else {
                    managerBot_->getApi().sendMessage(message->chat->id, 
                        "❌ فشل في إضافة البوت");
                }
                
            } catch (const exception& e) {
                managerBot_->getApi().sendMessage(message->chat->id, 
                    "❌ خطأ في إضافة البوت: " + string(e.what()));
            }
        }
    }

    void showStats(CallbackQuery::Ptr query) {
        auto metrics = botManager_->getMetrics();
        string stats = "📊 إحصائيات النظام:\n\n";
        stats += "🔢 البوتات النشطة: " + to_string(static_cast<int>(metrics["active_bots"])) + "\n";
        stats += "📈 معدل المعالجة: " + to_string(metrics["processing_rate"]) + "\n";
        stats += "📋 حجم الطابور: " + to_string(static_cast<int>(metrics["queue_size"])) + "\n";
        
        managerBot_->getApi().sendMessage(query->message->chat->id, stats);
    }

    shared_ptr<IBotManager> botManager_;
    shared_ptr<IEncryptionService> encryptor_;
    unique_ptr<Bot> managerBot_;
    atomic<size_t> commandsProcessed_{0};
    map<string, string> configuration_;
    mutable mutex configMutex_;
    atomic<bool> shutdownFlag_{false};
};

// =============== مُهيئ النظام المحسن ===============

class SystemInitializer {
public:
    static void initializeDatabase(IDatabaseManager& db) {
        try {
            db.executeTransaction([](connection& conn) {
                statement stmt(conn);
                
                // إنشاء جدول المستخدمين
                stmt.prepare("IF NOT EXISTS (SELECT * FROM sysobjects WHERE name='Users' AND xtype='U') "
                           "CREATE TABLE Users ("
                           "ID INT IDENTITY(1,1) PRIMARY KEY, "
                           "BotToken NVARCHAR(255) NOT NULL, "
                           "UserID BIGINT NOT NULL, "
                           "Username NVARCHAR(100) NOT NULL, "
                           "FirstSeen DATETIME NOT NULL, "
                           "LastSeen DATETIME NOT NULL, "
                           "UNIQUE(BotToken, UserID))");
                stmt.execute();
                
                // إنشاء فهارس لتحسين الأداء
                stmt.prepare("IF NOT EXISTS (SELECT * FROM sys.indexes WHERE name='IX_Users_BotToken') "
                           "CREATE INDEX IX_Users_BotToken ON Users(BotToken)");
                stmt.execute();
                
                stmt.prepare("IF NOT EXISTS (SELECT * FROM sys.indexes WHERE name='IX_Users_UserID') "
                           "CREATE INDEX IX_Users_UserID ON Users(UserID)");
                stmt.execute();
                
            });
            
            cout << "✅ تم تهيئة قاعدة البيانات بنجاح" << endl;
            
        } catch (const exception& e) {
            cerr << "❌ خطأ في تهيئة قاعدة البيانات: " << e.what() << endl;
            throw;
        }
    }

    static shared_ptr<IEncryptionService> createEncryptionService() {
        return make_shared<EncryptionService>();
    }

    static void checkSystemRequirements() {
        // التحقق من متغيرات البيئة المطلوبة
        const char* requiredEnvVars[] = {
            "MANAGER_BOT_TOKEN",
            "WEBHOOK_URL",
            "MANAGER_WEBHOOK_URL"
        };
        
        for (const auto& var : requiredEnvVars) {
            if (!getenv(var)) {
                cerr << "تحذير: متغير البيئة " << var << " غير محدد" << endl;
            }
        }
        
        // التحقق من مساحة القرص
        try {
            auto space = filesystem::space(".");
            if (space.available < 100 * 1024 * 1024) { // 100 MB
                cerr << "تحذير: مساحة القرص المتاحة منخفضة" << endl;
            }
        } catch (...) {
            cerr << "تحذير: لا يمكن التحقق من مساحة القرص" << endl;
        }
    }
};

// =============== الدالة الرئيسية المحسنة ===============

int main() {
    try {
        cout << "🚀 بدء تشغيل نظام بوتات التخزين..." << endl;
        
        // التحقق من متطلبات النظام
        SystemInitializer::checkSystemRequirements();
        
        // الحصول على متغيرات البيئة
        const char* managerToken = getenv("MANAGER_BOT_TOKEN");
        if (!managerToken) {
            cerr << "❌ خطأ: MANAGER_BOT_TOKEN مطلوب" << endl;
            return 1;
        }
        
        const char* dbServer = getenv("DB_SERVER") ?: "localhost";
        const char* dbName = getenv("DB_NAME") ?: "TelegramBots";
        const char* dbUser = getenv("DB_USER") ?: "sa";
        const char* dbPass = getenv("DB_PASS") ?: "password";
        
        // إنشاء سلسلة الاتصال بقاعدة البيانات
        string connStr = "Driver={ODBC Driver 17 for SQL Server};"
                        "Server=" + string(dbServer) + ";"
                        "Database=" + string(dbName) + ";"
                        "UID=" + string(dbUser) + ";"
                        "PWD=" + string(dbPass) + ";"
                        "TrustServerCertificate=yes;";
        
        // إنشاء الخدمات
        auto dbManager = make_shared<DatabaseManager>(connStr);
        auto encryptor = SystemInitializer::createEncryptionService();
        auto botManager = make_shared<BotManager>(dbManager, encryptor);
        
        // تهيئة قاعدة البيانات
        SystemInitializer::initializeDatabase(*dbManager);
        
        // إنشاء واجهة التحكم
        ControlPanel controlPanel(botManager, encryptor, managerToken);
        
        cout << "✅ تم تهيئة النظام بنجاح" << endl;
        cout << "📊 معلومات النظام:" << endl;
        cout << "  - البوتات النشطة: " << botManager->getActiveBotsCount() << endl;
        cout << "  - حجم تجمع الاتصالات: " << dbManager->getPoolSize() << endl;
        cout << "  - حالة التشفير: " << encryptor->getStatus() << endl;
        
        // بدء تشغيل واجهة التحكم
        controlPanel.start();
        
    } catch (const exception& e) {
        cerr << "❌ خطأ في تشغيل النظام: " << e.what() << endl;
        return 1;
    }
    
    return 0;
}