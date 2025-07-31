# 🗄️ دليل قاعدة البيانات

## 📋 نظرة عامة

قاعدة البيانات مصممة لتخزين وإدارة بوتات التليجرام والمستخدمين مع تتبع الإحصائيات والأنشطة.

---

## 🏗️ **هيكل قاعدة البيانات:**

### **الجداول الرئيسية:**

#### **1. جدول البوتات (Bots)**
```sql
CREATE TABLE Bots (
    ID INT IDENTITY(1,1) PRIMARY KEY,
    BotToken NVARCHAR(255) NOT NULL UNIQUE,
    BotName NVARCHAR(100) NOT NULL,
    BotUsername NVARCHAR(100) NOT NULL,
    IsActive BIT DEFAULT 1,
    TotalUsers INT DEFAULT 0,
    TotalMessages INT DEFAULT 0,
    CreatedAt DATETIME DEFAULT GETDATE(),
    LastActivity DATETIME DEFAULT GETDATE(),
    Settings NVARCHAR(MAX) -- JSON settings
);
```

#### **2. جدول المستخدمين (Users)**
```sql
CREATE TABLE Users (
    ID INT IDENTITY(1,1) PRIMARY KEY,
    BotToken NVARCHAR(255) NOT NULL,
    UserID BIGINT NOT NULL,
    Username NVARCHAR(100) NOT NULL,
    FirstSeen DATETIME NOT NULL,
    LastSeen DATETIME NOT NULL,
    MessageCount INT DEFAULT 0,
    IsActive BIT DEFAULT 1,
    CreatedAt DATETIME DEFAULT GETDATE(),
    UpdatedAt DATETIME DEFAULT GETDATE(),
    UNIQUE(BotToken, UserID)
);
```

#### **3. جدول الرسائل (Messages)**
```sql
CREATE TABLE Messages (
    ID INT IDENTITY(1,1) PRIMARY KEY,
    BotToken NVARCHAR(255) NOT NULL,
    UserID BIGINT NOT NULL,
    MessageID BIGINT NOT NULL,
    MessageType NVARCHAR(50) NOT NULL,
    MessageText NVARCHAR(MAX),
    MessageDate DATETIME NOT NULL,
    IsProcessed BIT DEFAULT 0,
    CreatedAt DATETIME DEFAULT GETDATE()
);
```

#### **4. جدول الإحصائيات (Statistics)**
```sql
CREATE TABLE Statistics (
    ID INT IDENTITY(1,1) PRIMARY KEY,
    BotToken NVARCHAR(255) NOT NULL,
    StatDate DATE NOT NULL,
    TotalUsers INT DEFAULT 0,
    NewUsers INT DEFAULT 0,
    TotalMessages INT DEFAULT 0,
    ActiveUsers INT DEFAULT 0,
    CreatedAt DATETIME DEFAULT GETDATE(),
    UNIQUE(BotToken, StatDate)
);
```

#### **5. جدول الأحداث (Events)**
```sql
CREATE TABLE Events (
    ID INT IDENTITY(1,1) PRIMARY KEY,
    BotToken NVARCHAR(255) NOT NULL,
    UserID BIGINT,
    EventType NVARCHAR(50) NOT NULL,
    EventData NVARCHAR(MAX),
    EventDate DATETIME DEFAULT GETDATE(),
    Severity NVARCHAR(20) DEFAULT 'INFO'
);
```

---

## 🚀 **البدء السريع:**

### **1. تثبيت المتطلبات:**
```bash
# تثبيت Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# تثبيت Docker Compose
sudo curl -L "https://github.com/docker/compose/releases/download/v2.20.0/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
sudo chmod +x /usr/local/bin/docker-compose
```

### **2. بدء قاعدة البيانات:**
```bash
# بدء قاعدة البيانات
./manage_database.sh start

# إعداد قاعدة البيانات (إنشاء الجداول)
./manage_database.sh setup
```

### **3. التحقق من الحالة:**
```bash
# عرض حالة قاعدة البيانات
./manage_database.sh status

# عرض الإحصائيات
./manage_database.sh stats
```

---

## 🔧 **الإجراءات المخزنة:**

### **1. sp_UpsertUser**
```sql
EXEC sp_UpsertUser 
    @BotToken = 'encrypted_token',
    @UserID = 123456789,
    @Username = 'user_name';
```

### **2. sp_AddMessage**
```sql
EXEC sp_AddMessage 
    @BotToken = 'encrypted_token',
    @UserID = 123456789,
    @MessageID = 1,
    @MessageType = 'text',
    @MessageText = 'Hello Bot!',
    @MessageDate = GETDATE();
```

### **3. sp_UpdateDailyStatistics**
```sql
EXEC sp_UpdateDailyStatistics @BotToken = 'encrypted_token';
```

### **4. sp_GetBotStatistics**
```sql
EXEC sp_GetBotStatistics @BotToken = 'encrypted_token';
```

---

## 📊 **الاستعلامات المفيدة:**

### **1. إحصائيات البوت:**
```sql
SELECT 
    b.BotName,
    b.TotalUsers,
    b.TotalMessages,
    b.LastActivity,
    COUNT(DISTINCT u.UserID) as ActiveUsers
FROM Bots b
LEFT JOIN Users u ON b.BotToken = u.BotToken 
    AND u.LastSeen >= DATEADD(DAY, -1, GETDATE())
WHERE b.BotToken = 'encrypted_token'
GROUP BY b.BotName, b.TotalUsers, b.TotalMessages, b.LastActivity;
```

### **2. المستخدمين الأكثر نشاطاً:**
```sql
SELECT TOP 10
    u.Username,
    u.MessageCount,
    u.LastSeen,
    COUNT(m.ID) as RecentMessages
FROM Users u
LEFT JOIN Messages m ON u.BotToken = m.BotToken 
    AND u.UserID = m.UserID 
    AND m.MessageDate >= DATEADD(DAY, -7, GETDATE())
WHERE u.BotToken = 'encrypted_token'
GROUP BY u.Username, u.MessageCount, u.LastSeen
ORDER BY RecentMessages DESC;
```

### **3. الإحصائيات الشهرية:**
```sql
SELECT 
    YEAR(StatDate) as Year,
    MONTH(StatDate) as Month,
    SUM(TotalUsers) as TotalUsers,
    SUM(NewUsers) as NewUsers,
    SUM(TotalMessages) as TotalMessages,
    AVG(ActiveUsers) as AvgActiveUsers
FROM Statistics
WHERE BotToken = 'encrypted_token'
    AND StatDate >= DATEADD(MONTH, -12, GETDATE())
GROUP BY YEAR(StatDate), MONTH(StatDate)
ORDER BY Year, Month;
```

---

## 🛠️ **إدارة قاعدة البيانات:**

### **الأوامر المتاحة:**

```bash
# بدء قاعدة البيانات
./manage_database.sh start

# إيقاف قاعدة البيانات
./manage_database.sh stop

# إعادة تشغيل قاعدة البيانات
./manage_database.sh restart

# عرض حالة قاعدة البيانات
./manage_database.sh status

# إعداد قاعدة البيانات
./manage_database.sh setup

# إنشاء نسخة احتياطية
./manage_database.sh backup

# استعادة قاعدة البيانات
./manage_database.sh restore backup_file.bak

# تنظيف البيانات القديمة
./manage_database.sh cleanup

# عرض الإحصائيات
./manage_database.sh stats

# عرض المساعدة
./manage_database.sh help
```

---

## 🔐 **الأمان:**

### **1. تشفير البيانات:**
- جميع توكنات البوتات مشفرة
- كلمات المرور محمية
- اتصالات SSL/TLS

### **2. إدارة الصلاحيات:**
```sql
-- إنشاء مستخدم للبوت
CREATE LOGIN BotUser WITH PASSWORD = 'StrongPassword123!';
CREATE USER BotUser FOR LOGIN BotUser;

-- منح الصلاحيات
GRANT SELECT, INSERT, UPDATE ON Users TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Bots TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Messages TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Statistics TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Events TO BotUser;
```

### **3. متغيرات البيئة:**
```bash
# نسخ ملف البيئة
cp .env.database .env

# تعديل الإعدادات حسب الحاجة
nano .env
```

---

## 📈 **المراقبة والأداء:**

### **1. الفهارس المحسنة:**
- فهارس على جميع الأعمدة المهمة
- فهارس مركبة للاستعلامات المعقدة
- فهارس تغطية للاستعلامات السريعة

### **2. النسخ الاحتياطي:**
```bash
# إنشاء نسخة احتياطية يومية
./manage_database.sh backup

# جدولة النسخ الاحتياطية
crontab -e
# إضافة: 0 2 * * * /path/to/manage_database.sh backup
```

### **3. التنظيف التلقائي:**
```bash
# تنظيف البيانات القديمة
./manage_database.sh cleanup

# جدولة التنظيف الأسبوعي
crontab -e
# إضافة: 0 3 * * 0 /path/to/manage_database.sh cleanup
```

---

## 🔧 **استكشاف الأخطاء:**

### **1. مشاكل الاتصال:**
```bash
# التحقق من حالة Docker
docker ps

# التحقق من سجلات MSSQL
docker logs telegram_bots_mssql

# اختبار الاتصال
docker exec telegram_bots_mssql /opt/mssql-tools/bin/sqlcmd \
    -S localhost -U sa -P StrongPassword123! -Q "SELECT 1"
```

### **2. مشاكل الأداء:**
```sql
-- التحقق من الفهارس
SELECT 
    OBJECT_NAME(i.object_id) as TableName,
    i.name as IndexName,
    i.type_desc as IndexType
FROM sys.indexes i
WHERE i.object_id IN (
    OBJECT_ID('Users'),
    OBJECT_ID('Messages'),
    OBJECT_ID('Bots')
);

-- التحقق من حجم الجداول
SELECT 
    t.name as TableName,
    p.rows as RowCount,
    SUM(a.total_pages) * 8 as TotalSpaceKB
FROM sys.tables t
INNER JOIN sys.indexes i ON t.object_id = i.object_id
INNER JOIN sys.partitions p ON i.object_id = p.object_id AND i.index_id = p.index_id
INNER JOIN sys.allocation_units a ON p.partition_id = a.container_id
GROUP BY t.name, p.rows
ORDER BY TotalSpaceKB DESC;
```

---

## 📚 **المراجع:**

### **1. وثائق MSSQL:**
- [Microsoft SQL Server Documentation](https://docs.microsoft.com/en-us/sql/sql-server/)
- [T-SQL Reference](https://docs.microsoft.com/en-us/sql/t-sql/language-reference)

### **2. أدوات إدارة:**
- [SQL Server Management Studio (SSMS)](https://docs.microsoft.com/en-us/sql/ssms/)
- [Azure Data Studio](https://docs.microsoft.com/en-us/sql/azure-data-studio/)

### **3. مراقبة الأداء:**
- [SQL Server Profiler](https://docs.microsoft.com/en-us/sql/tools/sql-server-profiler/)
- [Extended Events](https://docs.microsoft.com/en-us/sql/relational-databases/extended-events/)

---

## 🎯 **الخلاصة:**

### **الميزات الرئيسية:**
1. **✅ هيكل منظم**: 5 جداول رئيسية
2. **✅ علاقات واضحة**: Foreign Keys
3. **✅ فهارس محسنة**: للأداء العالي
4. **✅ إجراءات مخزنة**: للعمليات المعقدة
5. **✅ أمان متقدم**: تشفير وصلاحيات
6. **✅ إحصائيات شاملة**: تتبع الأداء
7. **✅ مرونة**: دعم JSON للإعدادات
8. **✅ قابلية التوسع**: تصميم قابل للنمو

### **الاستخدام:**
- **التطبيق**: يستخدم الإجراءات المخزنة
- **المراقبة**: استعلامات الإحصائيات
- **الأمان**: تشفير البيانات الحساسة
- **الأداء**: فهارس محسنة

**قاعدة البيانات جاهزة للاستخدام! 🚀**