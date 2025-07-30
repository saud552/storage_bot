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
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <functional>
#include <openssl/err.h>
#include <cctype>
#include <algorithm>
#include <openssl/conf.h>
#include <unordered_set>
#include <shared_mutex>
#include <boost/circular_buffer.hpp>
#include <regex>

using namespace std;
using namespace TgBot;
using namespace nanodbc;

// =============== تعريف الهياكل والواجهات الرئيسية ===============

struct BotConfig {
    string token;
    string name;
    string username;
    string encryptedToken;
    atomic<long> storedUsers{0};
    atomic<long> totalUsers{0};
    atomic<bool> isActive{true};
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
    virtual void startBot(const BotConfig& config) = 0;
    virtual void stopBot(const string& encryptedToken) = 0;
    virtual void pauseBot(const string& encryptedToken) = 0;
    virtual void resumeBot(const string& encryptedToken) = 0;
    virtual map<string, BotConfig> getActiveBots() = 0;
    virtual ~IBotManager() = default;
};

// =============== إدارة اتصالات قاعدة البيانات ===============

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
        
        if (availableConnections_.empty()) {
            if (totalConnections_ < maxPoolSize_) {
                return createNewConnection();
            }
            
            connectionCV_.wait(lock, [this] {
                return !availableConnections_.empty() || shutdownFlag_;
            });
        }
        
        if (shutdownFlag_) return nullptr;
        
        auto conn = move(availableConnections_.front());
        availableConnections_.pop();
        return conn;
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
            availableConnections_.push(createNewConnection());
        }
    }

    unique_ptr<connection> createNewConnection() {
        try {
            auto conn = make_unique<connection>(connectionString_);
            execute(*conn, "SET ANSI_NULLS ON; SET QUOTED_IDENTIFIER ON;");
            totalConnections_++;
            return conn;
        } catch (const exception& e) {
            throw runtime_error("Connection creation failed: " + string(e.what()));
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

// =============== خدمة التشفير باستخدام AES-GCM ===============

class EncryptionService : public IEncryptionService {
public:
    EncryptionService() {
        loadEncryptionKey();
    }

    string encrypt(const string& data) override {
        if (data.empty()) throw invalid_argument("Empty data for encryption");
        
        unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(
            EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        if (!ctx) throw runtime_error("Failed to create encryption context");
        
        // توليد IV عشوائي
        vector<unsigned char> iv(IV_LENGTH);
        if (RAND_bytes(iv.data(), IV_LENGTH) != 1) {
            throw runtime_error("IV generation failed");
        }

        // إعداد سياق التشفير
        if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            throw runtime_error("Encryption init failed");
        }

        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, IV_LENGTH, nullptr) != 1) {
            throw runtime_error("Set IV length failed");
        }

        if (EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key_.data(), iv.data()) != 1) {
            throw runtime_error("Encryption init key/iv failed");
        }

        // التشفير
        vector<unsigned char> ciphertext(data.size() + EVP_CIPHER_CTX_block_size(ctx.get()));
        int out_len = 0;
        
        if (EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &out_len, 
                             reinterpret_cast<const unsigned char*>(data.data()), 
                             data.size()) != 1) {
            throw runtime_error("Encryption update failed");
        }
        
        int final_len = 0;
        if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + out_len, &final_len) != 1) {
            throw runtime_error("Encryption final failed");
        }
        
        out_len += final_len;
        ciphertext.resize(out_len);

        // الحصول على علامة المصادقة
        vector<unsigned char> tag(TAG_LENGTH);
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, TAG_LENGTH, tag.data()) != 1) {
            throw runtime_error("Get tag failed");
        }

        // دمج IV + ciphertext + tag
        vector<unsigned char> combined;
        combined.reserve(iv.size() + ciphertext.size() + tag.size());
        combined.insert(combined.end(), iv.begin(), iv.end());
        combined.insert(combined.end(), ciphertext.begin(), ciphertext.end());
        combined.insert(combined.end(), tag.begin(), tag.end());

        // ترميز base64
        return base64Encode(combined);
    }

    string decrypt(const string& encryptedData) override {
        if (encryptedData.empty()) throw invalid_argument("Empty data for decryption");
        
        vector<unsigned char> combined = base64Decode(encryptedData);
        
        if (combined.size() < IV_LENGTH + TAG_LENGTH) {
            throw runtime_error("Invalid encrypted data: too short");
        }

        // فصل IV و ciphertext و tag
        vector<unsigned char> iv(IV_LENGTH);
        vector<unsigned char> tag(TAG_LENGTH);
        vector<unsigned char> ciphertext(combined.size() - IV_LENGTH - TAG_LENGTH);
        
        copy(combined.begin(), combined.begin() + IV_LENGTH, iv.begin());
        copy(combined.begin() + IV_LENGTH, combined.end() - TAG_LENGTH, ciphertext.begin());
        copy(combined.end() - TAG_LENGTH, combined.end(), tag.begin());

        unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(
            EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        if (!ctx) throw runtime_error("Failed to create decryption context");

        // إعداد سياق فك التشفير
        if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            throw runtime_error("Decryption init failed");
        }

        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, IV_LENGTH, nullptr) != 1) {
            throw runtime_error("Set IV length failed");
        }

        if (EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key_.data(), iv.data()) != 1) {
            throw runtime_error("Decryption init key/iv failed");
        }

        // فك التشفير
        vector<unsigned char> plaintext(ciphertext.size() + EVP_CIPHER_CTX_block_size(ctx.get()));
        int out_len = 0;
        
        if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &out_len, 
                             ciphertext.data(), ciphertext.size()) != 1) {
            throw runtime_error("Decryption update failed");
        }
        
        // تعيين علامة المصادقة
        if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, TAG_LENGTH, tag.data()) != 1) {
            throw runtime_error("Set tag failed");
        }
        
        // التحقق من المصادقة
        int final_len = 0;
        int ret = EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + out_len, &final_len);
        if (ret <= 0) {
            throw runtime_error("Decryption failed: Authentication tag mismatch");
        }
        
        out_len += final_len;
        plaintext.resize(out_len);
        
        return string(plaintext.begin(), plaintext.end());
    }

private:
    void loadEncryptionKey() {
        const char* envKey = getenv("ENCRYPTION_KEY");
        if (envKey && strlen(envKey) >= KEY_LENGTH) {
            key_.assign(envKey, envKey + KEY_LENGTH);
        } else {
            key_.resize(KEY_LENGTH);
            if (RAND_bytes(key_.data(), KEY_LENGTH) != 1) {
                throw runtime_error("Key generation failed");
            }
            cerr << "WARNING: Using ephemeral encryption key. Set ENCRYPTION_KEY for persistent encryption.\n";
        }
    }

    string base64Encode(const vector<unsigned char>& data) {
        // استخدام RAII لإدارة BIO
        auto bioDeleter = [](BIO* bio) { BIO_free_all(bio); };
        unique_ptr<BIO, decltype(bioDeleter)> b64(BIO_new(BIO_f_base64()), bioDeleter);
        if (!b64) throw runtime_error("Failed to create BIO for base64 encoding");

        BIO_set_flags(b64.get(), BIO_FLAGS_BASE64_NO_NL);
        BIO* mem = BIO_new(BIO_s_mem());
        if (!mem) throw runtime_error("Failed to create memory BIO");
        BIO_push(b64.get(), mem);

        if (BIO_write(b64.get(), data.data(), data.size()) <= 0) {
            throw runtime_error("BIO_write failed");
        }
        if (BIO_flush(b64.get()) != 1) {
            throw runtime_error("BIO_flush failed");
        }

        BUF_MEM* bptr;
        BIO_get_mem_ptr(b64.get(), &bptr);
        return string(bptr->data, bptr->length);
    }

    vector<unsigned char> base64Decode(const string& encoded) {
        // استخدام RAII لإدارة BIO
        auto bioDeleter = [](BIO* bio) { BIO_free_all(bio); };
        unique_ptr<BIO, decltype(bioDeleter)> b64(BIO_new(BIO_f_base64()), bioDeleter);
        if (!b64) throw runtime_error("Failed to create BIO for base64 decoding");

        BIO_set_flags(b64.get(), BIO_FLAGS_BASE64_NO_NL);
        BIO* mem = BIO_new_mem_buf(encoded.data(), encoded.size());
        if (!mem) throw runtime_error("Failed to create memory BIO");
        BIO_push(b64.get(), mem);

        vector<unsigned char> decoded(encoded.size());
        int len = BIO_read(b64.get(), decoded.data(), encoded.size());
        if (len < 0) {
            throw runtime_error("BIO_read failed");
        }
        decoded.resize(len);
        return decoded;
    }

    static constexpr size_t KEY_LENGTH = 32;  // AES-256
    static constexpr size_t IV_LENGTH = 12;   // GCM recommended IV size
    static constexpr size_t TAG_LENGTH = 16;  // GCM tag length
    
    vector<unsigned char> key_;
};

// =============== إدارة البوتات المركزية ===============

class BotManager : public IBotManager {
public:
    BotManager(shared_ptr<IDatabaseManager> db, shared_ptr<IEncryptionService> encryptor)
        : dbManager_(db), encryptor_(encryptor) {}

    void startBot(const BotConfig& config) override {
        lock_guard<shared_mutex> lock(botsMutex_);
        if (activeBots_.size() >= MAX_ACTIVE_BOTS) {
            throw runtime_error("Maximum active bots reached");
        }
        
        if (activeBots_.find(config.encryptedToken) != activeBots_.end()) {
            throw runtime_error("Bot already active");
        }
        
        activeBots_[config.encryptedToken] = config;
        activeBots_[config.encryptedToken].isActive = true;
        
        thread([this, config]() {
            runBotInstance(config);
        }).detach();
    }

    void stopBot(const string& encryptedToken) override {
        unique_lock<shared_mutex> lock(botsMutex_);
        auto it = activeBots_.find(encryptedToken);
        if (it == activeBots_.end()) return;
        
        it->second.isActive = false;
        lock.unlock();
        
        // انتظار حتى يتوقف البوت بشكل طبيعي
        this_thread::sleep_for(chrono::seconds(2));
        
        lock.lock();
        activeBots_.erase(it);
    }

    void pauseBot(const string& encryptedToken) override {
        unique_lock<shared_mutex> lock(botsMutex_);
        auto it = activeBots_.find(encryptedToken);
        if (it != activeBots_.end()) {
            it->second.isActive = false;
        }
    }

    void resumeBot(const string& encryptedToken) override {
        unique_lock<shared_mutex> lock(botsMutex_);
        auto it = activeBots_.find(encryptedToken);
        if (it != activeBots_.end()) {
            it->second.isActive = true;
        }
    }

    map<string, BotConfig> getActiveBots() override {
        shared_lock<shared_mutex> lock(botsMutex_);
        return activeBots_;
    }

private:
    void runBotInstance(const BotConfig& config) {
        try {
            string token;
            try {
                token = encryptor_->decrypt(config.encryptedToken);
            } catch (const exception& e) {
                cerr << "Decryption failed for bot " << config.name << ": " << e.what() << endl;
                return;
            }

            // التحقق من صحة التوكن باستخدام regex
            if (!regex_match(token, regex("^[0-9]+:[a-zA-Z0-9_-]{35}$"))) {
                cerr << "Invalid token format for bot " << config.name << endl;
                return;
            }

            Bot bot(token);
            
            setupBotHandlers(bot, config);
            
            TgLongPoll longPoll(bot, 50, 10);
            while (isBotActive(config.encryptedToken)) {
                if (isBotPaused(config.encryptedToken)) {
                    this_thread::sleep_for(chrono::milliseconds(500));
                    continue;
                }
                
                try {
                    longPoll.start();
                } catch (const exception& e) {
                    cerr << "Bot error (" << config.name << "): " << e.what() << endl;
                    this_thread::sleep_for(chrono::seconds(2));
                }
            }
        } catch (const exception& e) {
            cerr << "Failed to start bot (" << config.name << "): " << e.what() << endl;
        }
    }

    void setupBotHandlers(Bot& bot, const BotConfig& config) {
        bot.getEvents().onAnyMessage([this, &config](Message::Ptr message) {
            if (!message->from || message->from->username.empty()) return;
            processUserMessage(config.encryptedToken, message->from->id, message->from->username);
        });
    }

    void processUserMessage(const string& encryptedToken, int64_t userId, const string& username) {
        // تجميع الرسائل للمعالجة الدفعية
        lock_guard<mutex> lock(batchMutex_);
        if (messageBatch_.full()) {
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
            
            batch.reserve(messageBatch_.size());
            for (const auto& msg : messageBatch_) {
                batch.push_back(msg);
            }
            messageBatch_.clear();
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
        // استخدام استعلام مجمع للأداء الأمثل
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
        unique_lock<shared_mutex> lock(botsMutex_);
        auto it = activeBots_.find(encryptedToken);
        if (it != activeBots_.end()) {
            it->second.storedUsers += newUsers;
            it->second.totalUsers += newUsers;
        }
    }

    bool isBotActive(const string& encryptedToken) {
        shared_lock<shared_mutex> lock(botsMutex_);
        return activeBots_.find(encryptedToken) != activeBots_.end();
    }

    bool isBotPaused(const string& encryptedToken) {
        shared_lock<shared_mutex> lock(botsMutex_);
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
    mutable shared_mutex botsMutex_;
    map<string, BotConfig> activeBots_;
    
    mutex batchMutex_;
    boost::circular_buffer<MessageData> messageBatch_{BATCH_SIZE};
    chrono::steady_clock::time_point lastBatchTime_ = chrono::steady_clock::now();
};

// =============== واجهة التحكم (بوت المدير) ===============

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
            TgLongPoll longPoll(*managerBot_, 100, 20);
            while (true) {
                longPoll.start();
            }
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
        // ... (معالجة باقي الأحداث)
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
            
            botManager_->startBot(config);
            
            managerBot_->getApi().sendMessage(message->chat->id, 
                "✅ تمت إضافة البوت بنجاح!\n"
                "الاسم: " + config.name + "\n"
                "المعرف: @" + config.username);
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
                stats += "🔄 التخزين: " + string(config.isActive ? "نشط" : "متوقف") + "\n\n";
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

// =============== تهيئة النظام ===============

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
        });
    }

    static shared_ptr<IEncryptionService> createEncryptionService() {
        return make_shared<EncryptionService>();
    }
};

// =============== الدالة الرئيسية ===============

int main() {
    // تهيئة OpenSSL
    OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, nullptr);
    OPENSSL_init_ssl(0, nullptr);
    atexit([]() {
        EVP_cleanup();
        ERR_free_strings();
    });
    
    // الحصول على التوكن من متغير البيئة
    const char* managerToken = getenv("MANAGER_BOT_TOKEN");
    if (!managerToken || strlen(managerToken) < 30) {
        cerr << "Manager bot token is required" << endl;
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
