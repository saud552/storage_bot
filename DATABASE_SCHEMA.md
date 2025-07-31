# 🗄️ مخطط قاعدة البيانات الكامل

## 📋 ملخص قاعدة البيانات

قاعدة البيانات مصممة لتخزين وإدارة بوتات التليجرام والمستخدمين مع تتبع الإحصائيات والأنشطة.

---

## 🏗️ **هيكل قاعدة البيانات:**

### **1. جدول المستخدمين (Users)**

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

**الوصف:**
- **ID**: معرف فريد تلقائي
- **BotToken**: توكن البوت (مشفر)
- **UserID**: معرف المستخدم في تليجرام
- **Username**: اسم المستخدم
- **FirstSeen**: أول مرة تم رؤية المستخدم
- **LastSeen**: آخر مرة تم رؤية المستخدم
- **MessageCount**: عدد الرسائل المرسلة
- **IsActive**: حالة النشاط
- **CreatedAt**: تاريخ الإنشاء
- **UpdatedAt**: تاريخ التحديث

### **2. جدول البوتات (Bots)**

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

**الوصف:**
- **ID**: معرف فريد تلقائي
- **BotToken**: توكن البوت (مشفر)
- **BotName**: اسم البوت
- **BotUsername**: اسم المستخدم للبوت
- **IsActive**: حالة النشاط
- **TotalUsers**: إجمالي المستخدمين
- **TotalMessages**: إجمالي الرسائل
- **CreatedAt**: تاريخ الإنشاء
- **LastActivity**: آخر نشاط
- **Settings**: إعدادات البوت (JSON)

### **3. جدول الرسائل (Messages)**

```sql
CREATE TABLE Messages (
    ID INT IDENTITY(1,1) PRIMARY KEY,
    BotToken NVARCHAR(255) NOT NULL,
    UserID BIGINT NOT NULL,
    MessageID BIGINT NOT NULL,
    MessageType NVARCHAR(50) NOT NULL, -- text, photo, document, etc.
    MessageText NVARCHAR(MAX),
    MessageDate DATETIME NOT NULL,
    IsProcessed BIT DEFAULT 0,
    CreatedAt DATETIME DEFAULT GETDATE(),
    FOREIGN KEY (BotToken, UserID) REFERENCES Users(BotToken, UserID)
);
```

**الوصف:**
- **ID**: معرف فريد تلقائي
- **BotToken**: توكن البوت
- **UserID**: معرف المستخدم
- **MessageID**: معرف الرسالة في تليجرام
- **MessageType**: نوع الرسالة
- **MessageText**: نص الرسالة
- **MessageDate**: تاريخ الرسالة
- **IsProcessed**: تمت المعالجة أم لا
- **CreatedAt**: تاريخ الإنشاء

### **4. جدول الإحصائيات (Statistics)**

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

**الوصف:**
- **ID**: معرف فريد تلقائي
- **BotToken**: توكن البوت
- **StatDate**: تاريخ الإحصائيات
- **TotalUsers**: إجمالي المستخدمين
- **NewUsers**: المستخدمين الجدد
- **TotalMessages**: إجمالي الرسائل
- **ActiveUsers**: المستخدمين النشطين
- **CreatedAt**: تاريخ الإنشاء

### **5. جدول الأحداث (Events)**

```sql
CREATE TABLE Events (
    ID INT IDENTITY(1,1) PRIMARY KEY,
    BotToken NVARCHAR(255) NOT NULL,
    UserID BIGINT,
    EventType NVARCHAR(50) NOT NULL, -- start, stop, pause, resume, error
    EventData NVARCHAR(MAX), -- JSON data
    EventDate DATETIME DEFAULT GETDATE(),
    Severity NVARCHAR(20) DEFAULT 'INFO' -- INFO, WARNING, ERROR
);
```

**الوصف:**
- **ID**: معرف فريد تلقائي
- **BotToken**: توكن البوت
- **UserID**: معرف المستخدم (اختياري)
- **EventType**: نوع الحدث
- **EventData**: بيانات الحدث (JSON)
- **EventDate**: تاريخ الحدث
- **Severity**: مستوى الخطورة

---

## 🔗 **العلاقات بين الجداول:**

```sql
-- العلاقة بين Users و Messages
ALTER TABLE Messages 
ADD CONSTRAINT FK_Messages_Users 
FOREIGN KEY (BotToken, UserID) 
REFERENCES Users(BotToken, UserID);

-- العلاقة بين Bots و Users
ALTER TABLE Users 
ADD CONSTRAINT FK_Users_Bots 
FOREIGN KEY (BotToken) 
REFERENCES Bots(BotToken);

-- العلاقة بين Bots و Statistics
ALTER TABLE Statistics 
ADD CONSTRAINT FK_Statistics_Bots 
FOREIGN KEY (BotToken) 
REFERENCES Bots(BotToken);

-- العلاقة بين Bots و Events
ALTER TABLE Events 
ADD CONSTRAINT FK_Events_Bots 
FOREIGN KEY (BotToken) 
REFERENCES Bots(BotToken);
```

---

## 📊 **الفهارس (Indexes):**

```sql
-- فهارس جدول Users
CREATE INDEX IX_Users_BotToken ON Users(BotToken);
CREATE INDEX IX_Users_UserID ON Users(UserID);
CREATE INDEX IX_Users_LastSeen ON Users(LastSeen);
CREATE INDEX IX_Users_IsActive ON Users(IsActive);

-- فهارس جدول Bots
CREATE INDEX IX_Bots_Token ON Bots(BotToken);
CREATE INDEX IX_Bots_Username ON Bots(BotUsername);
CREATE INDEX IX_Bots_IsActive ON Bots(IsActive);
CREATE INDEX IX_Bots_LastActivity ON Bots(LastActivity);

-- فهارس جدول Messages
CREATE INDEX IX_Messages_BotToken ON Messages(BotToken);
CREATE INDEX IX_Messages_UserID ON Messages(UserID);
CREATE INDEX IX_Messages_MessageDate ON Messages(MessageDate);
CREATE INDEX IX_Messages_IsProcessed ON Messages(IsProcessed);

-- فهارس جدول Statistics
CREATE INDEX IX_Statistics_BotToken ON Statistics(BotToken);
CREATE INDEX IX_Statistics_Date ON Statistics(StatDate);

-- فهارس جدول Events
CREATE INDEX IX_Events_BotToken ON Events(BotToken);
CREATE INDEX IX_Events_EventDate ON Events(EventDate);
CREATE INDEX IX_Events_Severity ON Events(Severity);
```

---

## 🔧 **الإجراءات المخزنة (Stored Procedures):**

### **1. إجراء إضافة/تحديث مستخدم:**

```sql
CREATE PROCEDURE sp_UpsertUser
    @BotToken NVARCHAR(255),
    @UserID BIGINT,
    @Username NVARCHAR(100)
AS
BEGIN
    SET NOCOUNT ON;
    
    MERGE INTO Users AS target
    USING (SELECT @BotToken as BotToken, @UserID as UserID, @Username as Username) AS source
    ON target.BotToken = source.BotToken AND target.UserID = source.UserID
    WHEN MATCHED THEN
        UPDATE SET 
            Username = source.Username,
            LastSeen = GETDATE(),
            MessageCount = MessageCount + 1,
            UpdatedAt = GETDATE()
    WHEN NOT MATCHED THEN
        INSERT (BotToken, UserID, Username, FirstSeen, LastSeen, MessageCount)
        VALUES (source.BotToken, source.UserID, source.Username, GETDATE(), GETDATE(), 1);
END;
```

### **2. إجراء إضافة رسالة:**

```sql
CREATE PROCEDURE sp_AddMessage
    @BotToken NVARCHAR(255),
    @UserID BIGINT,
    @MessageID BIGINT,
    @MessageType NVARCHAR(50),
    @MessageText NVARCHAR(MAX),
    @MessageDate DATETIME
AS
BEGIN
    SET NOCOUNT ON;
    
    INSERT INTO Messages (BotToken, UserID, MessageID, MessageType, MessageText, MessageDate)
    VALUES (@BotToken, @UserID, @MessageID, @MessageType, @MessageText, @MessageDate);
    
    -- تحديث إحصائيات البوت
    UPDATE Bots 
    SET TotalMessages = TotalMessages + 1,
        LastActivity = GETDATE()
    WHERE BotToken = @BotToken;
END;
```

### **3. إجراء تحديث الإحصائيات اليومية:**

```sql
CREATE PROCEDURE sp_UpdateDailyStatistics
    @BotToken NVARCHAR(255)
AS
BEGIN
    SET NOCOUNT ON;
    
    DECLARE @Today DATE = CAST(GETDATE() AS DATE);
    DECLARE @Yesterday DATE = DATEADD(DAY, -1, @Today);
    
    MERGE INTO Statistics AS target
    USING (
        SELECT 
            @BotToken as BotToken,
            @Today as StatDate,
            COUNT(DISTINCT u.UserID) as TotalUsers,
            COUNT(DISTINCT CASE WHEN u.FirstSeen >= @Today THEN u.UserID END) as NewUsers,
            COUNT(m.ID) as TotalMessages,
            COUNT(DISTINCT CASE WHEN u.LastSeen >= @Today THEN u.UserID END) as ActiveUsers
        FROM Users u
        LEFT JOIN Messages m ON u.BotToken = m.BotToken AND u.UserID = m.UserID 
            AND CAST(m.MessageDate AS DATE) = @Today
        WHERE u.BotToken = @BotToken
    ) AS source
    ON target.BotToken = source.BotToken AND target.StatDate = source.StatDate
    WHEN MATCHED THEN
        UPDATE SET 
            TotalUsers = source.TotalUsers,
            NewUsers = source.NewUsers,
            TotalMessages = source.TotalMessages,
            ActiveUsers = source.ActiveUsers
    WHEN NOT MATCHED THEN
        INSERT (BotToken, StatDate, TotalUsers, NewUsers, TotalMessages, ActiveUsers)
        VALUES (source.BotToken, source.StatDate, source.TotalUsers, source.NewUsers, source.TotalMessages, source.ActiveUsers);
END;
```

---

## 📈 **الاستعلامات المفيدة:**

### **1. إحصائيات البوت:**

```sql
SELECT 
    b.BotName,
    b.TotalUsers,
    b.TotalMessages,
    b.LastActivity,
    COUNT(DISTINCT u.UserID) as ActiveUsers,
    COUNT(m.ID) as TodayMessages
FROM Bots b
LEFT JOIN Users u ON b.BotToken = u.BotToken AND u.LastSeen >= DATEADD(DAY, -1, GETDATE())
LEFT JOIN Messages m ON b.BotToken = m.BotToken AND CAST(m.MessageDate AS DATE) = CAST(GETDATE() AS DATE)
WHERE b.BotToken = @BotToken
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
LEFT JOIN Messages m ON u.BotToken = m.BotToken AND u.UserID = m.UserID 
    AND m.MessageDate >= DATEADD(DAY, -7, GETDATE())
WHERE u.BotToken = @BotToken
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
WHERE BotToken = @BotToken
    AND StatDate >= DATEADD(MONTH, -12, GETDATE())
GROUP BY YEAR(StatDate), MONTH(StatDate)
ORDER BY Year, Month;
```

---

## 🛡️ **الأمان:**

### **1. تشفير البيانات الحساسة:**

```sql
-- تشفير توكن البوت
CREATE MASTER KEY ENCRYPTION BY PASSWORD = 'YourStrongPassword123!';

CREATE CERTIFICATE BotTokenCert
WITH SUBJECT = 'Bot Token Encryption Certificate';

CREATE SYMMETRIC KEY BotTokenKey
WITH ALGORITHM = AES_256
ENCRYPTION BY CERTIFICATE BotTokenCert;

-- دالة تشفير
CREATE FUNCTION fn_EncryptBotToken(@Token NVARCHAR(255))
RETURNS VARBINARY(MAX)
AS
BEGIN
    DECLARE @EncryptedToken VARBINARY(MAX);
    OPEN SYMMETRIC KEY BotTokenKey DECRYPTION BY CERTIFICATE BotTokenCert;
    SET @EncryptedToken = ENCRYPTBYKEY(KEY_GUID('BotTokenKey'), @Token);
    CLOSE SYMMETRIC KEY BotTokenKey;
    RETURN @EncryptedToken;
END;

-- دالة فك التشفير
CREATE FUNCTION fn_DecryptBotToken(@EncryptedToken VARBINARY(MAX))
RETURNS NVARCHAR(255)
AS
BEGIN
    DECLARE @DecryptedToken NVARCHAR(255);
    OPEN SYMMETRIC KEY BotTokenKey DECRYPTION BY CERTIFICATE BotTokenCert;
    SET @DecryptedToken = DECRYPTBYKEY(@EncryptedToken);
    CLOSE SYMMETRIC KEY BotTokenKey;
    RETURN @DecryptedToken;
END;
```

### **2. إدارة الصلاحيات:**

```sql
-- إنشاء مستخدم للبوت
CREATE LOGIN BotUser WITH PASSWORD = 'StrongBotPassword123!';
CREATE USER BotUser FOR LOGIN BotUser;

-- منح الصلاحيات
GRANT SELECT, INSERT, UPDATE ON Users TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Bots TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Messages TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Statistics TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Events TO BotUser;
GRANT EXECUTE ON sp_UpsertUser TO BotUser;
GRANT EXECUTE ON sp_AddMessage TO BotUser;
GRANT EXECUTE ON sp_UpdateDailyStatistics TO BotUser;
```

---

## 📋 **خطة التنفيذ:**

### **المرحلة 1: إنشاء قاعدة البيانات**
```sql
-- إنشاء قاعدة البيانات
CREATE DATABASE TelegramBots
ON PRIMARY (
    NAME = TelegramBots_Data,
    FILENAME = 'C:\Data\TelegramBots.mdf',
    SIZE = 100MB,
    MAXSIZE = UNLIMITED,
    FILEGROWTH = 10MB
)
LOG ON (
    NAME = TelegramBots_Log,
    FILENAME = 'C:\Data\TelegramBots.ldf',
    SIZE = 50MB,
    MAXSIZE = UNLIMITED,
    FILEGROWTH = 5MB
);
```

### **المرحلة 2: إنشاء الجداول**
```sql
USE TelegramBots;

-- إنشاء جميع الجداول
-- (الكود أعلاه)
```

### **المرحلة 3: إنشاء الفهارس**
```sql
-- إنشاء جميع الفهارس
-- (الكود أعلاه)
```

### **المرحلة 4: إنشاء الإجراءات المخزنة**
```sql
-- إنشاء جميع الإجراءات المخزنة
-- (الكود أعلاه)
```

### **المرحلة 5: إعداد الأمان**
```sql
-- إعداد التشفير والصلاحيات
-- (الكود أعلاه)
```

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

**هذا هو التخطيط الكامل لقاعدة البيانات! 🗄️**