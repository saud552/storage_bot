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

using namespace std;
using namespace TgBot;
using namespace nanodbc;
using namespace CryptoPP;

// =============== تعريف الهياكل والواجهات الرئيسية ===============

struct BotConfig {
    string token;
    string name;
    string username;
    string encryptedToken;
    atomic<long> storedUsers{0};
    atomic<long> totalUsers{0};
    atomic<bool> isActive{true};
    atomic<bool> isRunning{false};
    future<void> botThread;
};

class IDatabaseManager {
public:
    virtual unique_ptr<connection> getConnection() = 0;
    virtual void releaseConnection(unique_ptr<connection> conn) = 0;
    virtual void executeTransaction(const function<void(connection&)>& func) = 0;
    virtual ~IDatabaseManager() = default;
};

class IEncryptionService {
public:
    virtual string encrypt(const string& data) = 0;
    virtual string decrypt(const string& encryptedData) = 0;
    virtual ~IEncryptionService() = default;
};

class IBotManager {
public:
    virtual bool startBot(const BotConfig& config) = 0;
    virtual bool stopBot(const string& encryptedToken) = 0;
    virtual bool pauseBot(const string& encryptedToken) = 0;
    virtual bool resumeBot(const string& encryptedToken) = 0;
    virtual map<string, BotConfig> getActiveBots() = 0;
    virtual ~IBotManager() = default;
};

// =============== إدارة اتصالات قاعدة البيانات المحسنة ===============

class DatabaseManager : public IDatabaseManager {
public:
    DatabaseManager(const string& connStr, size_t poolSize = 10) 
        : connectionString_(connStr), maxPoolSize_(poolSize) {
        initializePool();
    }

    ~DatabaseManager() {
        shutdown();
    }

    unique_ptr<connection> getConnection() override {
        unique_lock<mutex> lock(poolMutex_);
        
        // محاولة الحصول على اتصال متاح
        for (int attempts = 0; attempts < 3; ++attempts) {
            if (availableConnections_.empty()) {
                if (totalConnections_ < maxPoolSize_) {
                    auto conn = createNewConnection();
                    if (conn) return conn;
                }
                
                // انتظار مع timeout
                if (!connectionCV_.wait_for(lock, chrono::seconds(5), [this] {
                    return !availableConnections_.empty() || shutdownFlag_;
                })) {
                    continue;
                }
            }
            
            if (shutdownFlag_) return nullptr;
            
            // التحقق من صحة الاتصال قبل إرجاعه
            auto conn = move(availableConnections_.front());
            availableConnections_.pop();
            
            if (isConnectionValid(*conn)) {
                return conn;
            } else {
                // إزالة الاتصال الفاسد
                conn->disconnect();
                if (totalConnections_ > 0) totalConnections_--;
            }
        }
        
        throw runtime_error("Failed to get valid database connection after 3 attempts");
    }

    void releaseConnection(unique_ptr<connection> conn) override {
        if (!conn) return;
        
        lock_guard<mutex> lock(poolMutex_);
        if (shutdownFlag_ || !isConnectionValid(*conn)) {
            conn->disconnect();
            if (!shutdownFlag_ && totalConnections_ > 0) {
                totalConnections_--;
            }
            return;
        }
        
        availableConnections_.push(move(conn));
        connectionCV_.notify_one();
    }

    void executeTransaction(const function<void(connection&)>& func) override {
        auto conn = getConnection();
        if (!conn) throw runtime_error("Database connection unavailable");
        
        try {
            execute(*conn, "BEGIN TRANSACTION");
            func(*conn);
            execute(*conn, "COMMIT TRANSACTION");
        } catch (...) {
            try { execute(*conn, "ROLLBACK TRANSACTION"); } catch (...) {}
            throw;
        }
        releaseConnection(move(conn));
    }

    void shutdown() {
        {
            lock_guard<mutex> lock(poolMutex_);
            shutdownFlag_ = true;
        }
        connectionCV_.notify_all();
        
        lock_guard<mutex> lock(poolMutex_);
        while (!availableConnections_.empty()) {
            auto conn = move(availableConnections_.front());
            if (conn && conn->connected()) conn->disconnect();
            availableConnections_.pop();
        }
        totalConnections_ = 0;
    }

private:
    void initializePool() {
        lock_guard<mutex> lock(poolMutex_);
        for (size_t i = 0; i < min(5, maxPoolSize_); ++i) {
            auto conn = createNewConnection();
            if (conn) {
                availableConnections_.push(move(conn));
            }
        }
    }

    unique_ptr<connection> createNewConnection() {
        try {
            auto conn = make_unique<connection>(connectionString_);
            execute(*conn, "SET ANSI_NULLS ON; SET QUOTED_IDENTIFIER ON;");
            totalConnections_++;
            return conn;
        } catch (const exception& e) {
            cerr << "Connection creation failed: " << e.what() << endl;
            return nullptr;
        }
    }

    bool isConnectionValid(connection& conn) {
        try {
            if (!conn.connected()) return false;
            execute(conn, "SELECT 1;");
            return true;
        } catch (...) {
            return false;
        }
    }

    const string connectionString_;
    const size_t maxPoolSize_;
    atomic<size_t> totalConnections_{0};
    atomic<bool> shutdownFlag_{false};
    mutex poolMutex_;
    condition_variable connectionCV_;
    queue<unique_ptr<connection>> availableConnections_;
};

// =============== خدمة التشفير المحسنة باستخدام Crypto++ ===============

class EncryptionService : public IEncryptionService {
public:
    EncryptionService() {
        loadEncryptionKey();
    }

    string encrypt(const string& data) override {
        if (data.empty()) throw invalid_argument("Empty data for encryption");
        
        try {
            // توليد IV عشوائي
            SecByteBlock iv(IV_LENGTH);
            prng_.GenerateBlock(iv, iv.size());
            
            // إعداد التشفير
            GCM<AES>::Encryption encryptor;
            encryptor.SetKeyWithIV(key_, key_.size(), iv, iv.size());
            
            // التشفير
            string ciphertext;
            StringSource ss(data, true,
                new AuthenticatedEncryptionFilter(encryptor,
                    new StringSink(ciphertext)
                )
            );
            
            // دمج IV + ciphertext
            string combined;
            combined.reserve(iv.size() + ciphertext.size());
            combined.append(reinterpret_cast<const char*>(iv.data()), iv.size());
            combined.append(ciphertext);
            
            // ترميز base64
            return base64Encode(combined);
        } catch (const exception& e) {
            throw runtime_error("Encryption failed: " + string(e.what()));
        }
    }

    string decrypt(const string& encryptedData) override {
        if (encryptedData.empty()) throw invalid_argument("Empty data for decryption");
        
        try {
            string combined = base64Decode(encryptedData);
            
            if (combined.size() < IV_LENGTH) {
                throw runtime_error("Invalid encrypted data: too short");
            }
            
            // فصل IV و ciphertext
            SecByteBlock iv(reinterpret_cast<const byte*>(combined.data()), IV_LENGTH);
            string ciphertext(combined.begin() + IV_LENGTH, combined.end());
            
            // إعداد فك التشفير
            GCM<AES>::Decryption decryptor;
            decryptor.SetKeyWithIV(key_, key_.size(), iv, iv.size());
            
            // فك التشفير
            string plaintext;
            StringSource ss(ciphertext, true,
                new AuthenticatedDecryptionFilter(decryptor,
                    new StringSink(plaintext)
                )
            );
            
            return plaintext;
        } catch (const exception& e) {
            throw runtime_error("Decryption failed: " + string(e.what()));
        }
    }

private:
    void loadEncryptionKey() {
        const char* envKey = getenv("ENCRYPTION_KEY");
        if (envKey && strlen(envKey) >= KEY_LENGTH) {
            key_.assign(reinterpret_cast<const byte*>(envKey), KEY_LENGTH);
        } else {
            key_.resize(KEY_LENGTH);
            prng_.GenerateBlock(key_, key_.size());
            cerr << "WARNING: Using ephemeral encryption key. Set ENCRYPTION_KEY for persistent encryption.\n";
        }
    }

    string base64Encode(const string& data) {
        string encoded;
        StringSource ss(data, true,
            new Base64Encoder(
                new StringSink(encoded),
                false // لا إضافة فواصل أسطر
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
};

// =============== إدارة البوتات المحسنة ===============

class BotManager : public IBotManager {
public:
    BotManager(shared_ptr<IDatabaseManager> db, shared_ptr<IEncryptionService> encryptor)
        : dbManager_(db), encryptor_(encryptor) {}

    bool startBot(const BotConfig& config) override {
        lock_guard<mutex> lock(botsMutex_);
        
        if (activeBots_.size() >= MAX_ACTIVE_BOTS) {
            throw runtime_error("Maximum active bots reached");
        }
        
        if (activeBots_.find(config.encryptedToken) != activeBots_.end()) {
            throw runtime_error("Bot already active");
        }
        
        // التحقق من صحة التوكن أولاً
        string token;
        try {
            token = encryptor_->decrypt(config.encryptedToken);
            if (!regex_match(token, regex("^[0-9]+:[a-zA-Z0-9_-]{35}$"))) {
                throw runtime_error("Invalid token format");
            }
        } catch (const exception& e) {
            throw runtime_error("Invalid encrypted token: " + string(e.what()));
        }
        
        // إنشاء نسخة من التكوين
        BotConfig botConfig = config;
        botConfig.isRunning = true;
        
        // بدء البوت في thread منفصل
        botConfig.botThread = async(launch::async, [this, botConfig]() {
            runBotInstance(botConfig);
        });
        
        // إضافة البوت للقائمة فقط بعد نجاح بدء التشغيل
        activeBots_[config.encryptedToken] = botConfig;
        
        return true;
    }

    bool stopBot(const string& encryptedToken) override {
        unique_lock<mutex> lock(botsMutex_);
        auto it = activeBots_.find(encryptedToken);
        if (it == activeBots_.end()) return false;
        
        it->second.isRunning = false;
        it->second.isActive = false;
        
        // انتظار انتهاء thread البوت
        if (it->second.botThread.valid()) {
            lock.unlock();
            try {
                it->second.botThread.wait_for(chrono::seconds(5));
            } catch (...) {
                // تجاهل الأخطاء في حالة timeout
            }
            lock.lock();
        }
        
        activeBots_.erase(it);
        return true;
    }

    bool pauseBot(const string& encryptedToken) override {
        lock_guard<mutex> lock(botsMutex_);
        auto it = activeBots_.find(encryptedToken);
        if (it != activeBots_.end()) {
            it->second.isActive = false;
            return true;
        }
        return false;
    }

    bool resumeBot(const string& encryptedToken) override {
        lock_guard<mutex> lock(botsMutex_);
        auto it = activeBots_.find(encryptedToken);
        if (it != activeBots_.end()) {
            it->second.isActive = true;
            return true;
        }
        return false;
    }

    map<string, BotConfig> getActiveBots() override {
        lock_guard<mutex> lock(botsMutex_);
        return activeBots_;
    }

private:
    void runBotInstance(const BotConfig& config) {
        try {
            string token = encryptor_->decrypt(config.encryptedToken);
            
            Bot bot(token);
            setupBotHandlers(bot, config);
            
            // استخدام Webhook بدلاً من Polling
            string webhookUrl = getenv("WEBHOOK_URL") ?: "https://your-domain.com/webhook";
            string webhookPath = "/webhook/" + config.encryptedToken;
            
            TgWebhook webhook(bot, webhookUrl + webhookPath);
            
            while (isBotRunning(config.encryptedToken)) {
                if (isBotPaused(config.encryptedToken)) {
                    this_thread::sleep_for(chrono::milliseconds(500));
                    continue;
                }
                
                try {
                    webhook.start();
                } catch (const exception& e) {
                    cerr << "Bot error (" << config.name << "): " << e.what() << endl;
                    this_thread::sleep_for(chrono::seconds(2));
                }
            }
            
            webhook.stop();
        } catch (const exception& e) {
            cerr << "Failed to start bot (" << config.name << "): " << e.what() << endl;
        }
    }

    void setupBotHandlers(Bot& bot, const BotConfig& config) {
        // معالجة جميع أنواع الرسائل
        bot.getEvents().onAnyMessage([this, &config](Message::Ptr message) {
            if (!message->from) return;
            
            // معالجة الرسائل حتى لو لم يكن هناك username
            string username = message->from->username.empty() ? 
                "user_" + to_string(message->from->id) : message->from->username;
            
            processUserMessage(config.encryptedToken, message->from->id, username);
        });
        
        // معالجة الأوامر
        bot.getEvents().onCommand("start", [this, &config](Message::Ptr message) {
            if (message->from) {
                string username = message->from->username.empty() ? 
                    "user_" + to_string(message->from->id) : message->from->username;
                processUserMessage(config.encryptedToken, message->from->id, username);
            }
        });
    }

    void processUserMessage(const string& encryptedToken, int64_t userId, const string& username) {
        lock_guard<mutex> lock(batchMutex_);
        
        if (messageBatch_.size() >= BATCH_SIZE) {
            processBatch();
        }
        
        messageBatch_.push_back({encryptedToken, userId, username});
        
        if (lastBatchTime_ + chrono::seconds(5) < chrono::steady_clock::now()) {
            processBatch();
        }
    }

    void processBatch() {
        vector<MessageData> batch;
        {
            lock_guard<mutex> lock(batchMutex_);
            if (messageBatch_.empty()) return;
            
            batch.swap(messageBatch_);
            lastBatchTime_ = chrono::steady_clock::now();
        }
        
        try {
            dbManager_->executeTransaction([&](connection& conn) {
                processBatchInTransaction(conn, batch);
            });
        } catch (const exception& e) {
            cerr << "Batch processing failed: " << e.what() << endl;
        }
    }

    void processBatchInTransaction(connection& conn, const vector<MessageData>& batch) {
        unordered_map<string, vector<pair<int64_t, string>>> botUsers;
        
        // تجميع البيانات حسب البوت
        for (const auto& msg : batch) {
            botUsers[msg.encryptedToken].emplace_back(msg.userId, msg.username);
        }
        
        // معالجة كل بوت على حدة
        for (const auto& [botToken, users] : botUsers) {
            updateUserRecords(conn, botToken, users);
            updateBotStats(botToken, users.size());
        }
    }

    void updateUserRecords(connection& conn, const string& botToken, 
                          const vector<pair<int64_t, string>>& users) {
        const string upsertQuery = R"(
            MERGE INTO users AS target
            USING (VALUES (?, ?, ?)) AS source (user_id, username, bot_token)
            ON target.user_id = source.user_id AND target.bot_token = source.bot_token
            WHEN MATCHED THEN 
                UPDATE SET username = source.username, updated_at = GETDATE()
            WHEN NOT MATCHED THEN 
                INSERT (user_id, username, bot_token, created_at, updated_at)
                VALUES (source.user_id, source.username, source.bot_token, GETDATE(), GETDATE());
        )";
        
        statement stmt(conn);
        prepare(stmt, upsertQuery);
        
        for (const auto& [userId, username] : users) {
            stmt.bind(0, static_cast<long long>(userId));
            stmt.bind(1, username.c_str());
            stmt.bind(2, botToken.c_str());
            execute(stmt);
            stmt.reset();
        }
    }

    void updateBotStats(const string& encryptedToken, size_t newUsers) {
        lock_guard<mutex> lock(botsMutex_);
        auto it = activeBots_.find(encryptedToken);
        if (it != activeBots_.end()) {
            it->second.storedUsers += newUsers;
            it->second.totalUsers += newUsers;
        }
    }

    bool isBotRunning(const string& encryptedToken) {
        lock_guard<mutex> lock(botsMutex_);
        auto it = activeBots_.find(encryptedToken);
        return it != activeBots_.end() && it->second.isRunning;
    }

    bool isBotPaused(const string& encryptedToken) {
        lock_guard<mutex> lock(botsMutex_);
        auto it = activeBots_.find(encryptedToken);
        return it != activeBots_.end() && !it->second.isActive;
    }

    struct MessageData {
        string encryptedToken;
        int64_t userId;
        string username;
    };

    static constexpr size_t MAX_ACTIVE_BOTS = 50;
    static constexpr size_t BATCH_SIZE = 100;
    
    shared_ptr<IDatabaseManager> dbManager_;
    shared_ptr<IEncryptionService> encryptor_;
    mutable mutex botsMutex_;
    map<string, BotConfig> activeBots_;
    
    mutex batchMutex_;
    vector<MessageData> messageBatch_;
    chrono::steady_clock::time_point lastBatchTime_ = chrono::steady_clock::now();
};

// =============== واجهة التحكم المحسنة ===============

class ControlPanel {
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
            // استخدام Webhook للبوت المدير أيضاً
            string webhookUrl = getenv("MANAGER_WEBHOOK_URL") ?: "https://your-domain.com/manager";
            TgWebhook webhook(*managerBot_, webhookUrl);
            webhook.start();
        } catch (const exception& e) {
            cerr << "Manager bot error: " << e.what() << endl;
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
        
        if (data == "add_bot") {
            managerBot_->getApi().sendMessage(query->message->chat->id, 
                "أرسل توكن البوت الجديد:");
        }
        else if (data == "stats") {
            showStats(query);
        }
        // يمكن إضافة المزيد من المعالجات هنا
    }

    void handleTextMessage(Message::Ptr message) {
        if (message->text.empty() || message->text[0] == '/') return;
        
        try {
            string token = message->text;
            token.erase(remove_if(token.begin(), token.end(), ::isspace), token.end());
            
            // التحقق من صيغة التوكن
            if (!regex_match(token, regex("^[0-9]+:[a-zA-Z0-9_-]{35}$"))) {
                throw invalid_argument("Invalid token format");
            }
            
            // التحقق من صحة التوكن
            Bot testBot(token);
            auto me = testBot.getApi().getMe();
            
            // إضافة البوت
            BotConfig config;
            config.token = token;
            config.name = me->firstName;
            config.username = me->username;
            config.encryptedToken = encryptor_->encrypt(token);
            
            if (botManager_->startBot(config)) {
                managerBot_->getApi().sendMessage(message->chat->id, 
                    "✅ تمت إضافة البوت بنجاح!\n"
                    "الاسم: " + config.name + "\n"
                    "المعرف: @" + config.username);
            } else {
                throw runtime_error("Failed to start bot");
            }
        } catch (const exception& e) {
            managerBot_->getApi().sendMessage(message->chat->id, 
                "❌ فشل إضافة البوت: " + string(e.what()));
        }
    }

    void showStats(CallbackQuery::Ptr query) {
        try {
            auto bots = botManager_->getActiveBots();
            string stats = "📊 إحصائيات البوتات:\n\n";
            
            for (const auto& [token, config] : bots) {
                stats += "🤖 " + config.name + " (@" + config.username + ")\n";
                stats += "👥 المستخدمون: " + to_string(config.totalUsers.load()) + "\n";
                stats += "🔄 الحالة: " + string(config.isActive ? "نشط" : "متوقف") + "\n\n";
            }
            
            managerBot_->getApi().editMessageText(stats,
                query->message->chat->id,
                query->message->messageId);
        } catch (const exception& e) {
            managerBot_->getApi().answerCallbackQuery(query->id, 
                "❌ فشل جلب الإحصائيات: " + string(e.what()));
        }
    }

    shared_ptr<IBotManager> botManager_;
    shared_ptr<IEncryptionService> encryptor_;
    unique_ptr<Bot> managerBot_;
};

// =============== تهيئة النظام المحسنة ===============

class SystemInitializer {
public:
    static void initializeDatabase(IDatabaseManager& db) {
        db.executeTransaction([](connection& conn) {
            // إنشاء جدول المستخدمين
            execute(conn, R"(
                IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'users')
                CREATE TABLE users (
                    id INT IDENTITY(1,1) PRIMARY KEY,
                    user_id BIGINT NOT NULL,
                    username NVARCHAR(255) NOT NULL,
                    bot_token NVARCHAR(255) NOT NULL,
                    created_at DATETIME DEFAULT GETDATE(),
                    updated_at DATETIME DEFAULT GETDATE()
                )
            )");
            
            // إنشاء فهارس
            execute(conn, R"(
                IF NOT EXISTS (SELECT * FROM sys.indexes WHERE name = 'idx_user_bot')
                CREATE INDEX idx_user_bot ON users (user_id, bot_token)
            )");
            
            // إنشاء فهرس إضافي للأداء
            execute(conn, R"(
                IF NOT EXISTS (SELECT * FROM sys.indexes WHERE name = 'idx_bot_token')
                CREATE INDEX idx_bot_token ON users (bot_token)
            )");
        });
    }

    static shared_ptr<IEncryptionService> createEncryptionService() {
        return make_shared<EncryptionService>();
    }
};

// =============== الدالة الرئيسية المحسنة ===============

int main() {
    // التحقق من متغيرات البيئة المطلوبة
    const char* managerToken = getenv("MANAGER_BOT_TOKEN");
    if (!managerToken || strlen(managerToken) < 30) {
        cerr << "Manager bot token is required" << endl;
        return 1;
    }
    
    const char* webhookUrl = getenv("WEBHOOK_URL");
    if (!webhookUrl) {
        cerr << "WEBHOOK_URL environment variable is required" << endl;
        return 1;
    }
    
    // تكوين قاعدة البيانات
    string dbConnStr = "Driver={ODBC Driver 17 for SQL Server};"
                       "Server=" + string(getenv("DB_SERVER") ?: "localhost") + ";"
                       "Database=" + string(getenv("DB_NAME") ?: "TelegramBots") + ";"
                       "UID=" + string(getenv("DB_USER") ?: "sa") + ";"
                       "PWD=" + string(getenv("DB_PASS") ?: "password") + ";";
    
    try {
        // إنشاء وإعداد المكونات
        auto dbManager = make_shared<DatabaseManager>(dbConnStr, 15);
        SystemInitializer::initializeDatabase(*dbManager);
        
        auto encryptor = SystemInitializer::createEncryptionService();
        auto botManager = make_shared<BotManager>(dbManager, encryptor);
        
        // بدء نظام التحكم
        ControlPanel controlPanel(botManager, encryptor, managerToken);
        controlPanel.start();
    } catch (const exception& e) {
        cerr << "System initialization failed: " << e.what() << endl;
        return 1;
    }
    
    return 0;
}