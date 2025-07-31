-- ========================================
-- إنشاء قاعدة بيانات بوتات التليجرام
-- ========================================

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

GO

USE TelegramBots;
GO

-- ========================================
-- إنشاء الجداول
-- ========================================

-- جدول البوتات
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

-- جدول المستخدمين
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

-- جدول الرسائل
CREATE TABLE Messages (
    ID INT IDENTITY(1,1) PRIMARY KEY,
    BotToken NVARCHAR(255) NOT NULL,
    UserID BIGINT NOT NULL,
    MessageID BIGINT NOT NULL,
    MessageType NVARCHAR(50) NOT NULL, -- text, photo, document, etc.
    MessageText NVARCHAR(MAX),
    MessageDate DATETIME NOT NULL,
    IsProcessed BIT DEFAULT 0,
    CreatedAt DATETIME DEFAULT GETDATE()
);

-- جدول الإحصائيات
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

-- جدول الأحداث
CREATE TABLE Events (
    ID INT IDENTITY(1,1) PRIMARY KEY,
    BotToken NVARCHAR(255) NOT NULL,
    UserID BIGINT,
    EventType NVARCHAR(50) NOT NULL, -- start, stop, pause, resume, error
    EventData NVARCHAR(MAX), -- JSON data
    EventDate DATETIME DEFAULT GETDATE(),
    Severity NVARCHAR(20) DEFAULT 'INFO' -- INFO, WARNING, ERROR
);

GO

-- ========================================
-- إنشاء العلاقات (Foreign Keys)
-- ========================================

-- العلاقة بين Users و Bots
ALTER TABLE Users 
ADD CONSTRAINT FK_Users_Bots 
FOREIGN KEY (BotToken) 
REFERENCES Bots(BotToken);

-- العلاقة بين Messages و Users
ALTER TABLE Messages 
ADD CONSTRAINT FK_Messages_Users 
FOREIGN KEY (BotToken, UserID) 
REFERENCES Users(BotToken, UserID);

-- العلاقة بين Statistics و Bots
ALTER TABLE Statistics 
ADD CONSTRAINT FK_Statistics_Bots 
FOREIGN KEY (BotToken) 
REFERENCES Bots(BotToken);

-- العلاقة بين Events و Bots
ALTER TABLE Events 
ADD CONSTRAINT FK_Events_Bots 
FOREIGN KEY (BotToken) 
REFERENCES Bots(BotToken);

GO

-- ========================================
-- إنشاء الفهارس (Indexes)
-- ========================================

-- فهارس جدول Bots
CREATE INDEX IX_Bots_Token ON Bots(BotToken);
CREATE INDEX IX_Bots_Username ON Bots(BotUsername);
CREATE INDEX IX_Bots_IsActive ON Bots(IsActive);
CREATE INDEX IX_Bots_LastActivity ON Bots(LastActivity);

-- فهارس جدول Users
CREATE INDEX IX_Users_BotToken ON Users(BotToken);
CREATE INDEX IX_Users_UserID ON Users(UserID);
CREATE INDEX IX_Users_LastSeen ON Users(LastSeen);
CREATE INDEX IX_Users_IsActive ON Users(IsActive);

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

GO

-- ========================================
-- إنشاء الإجراءات المخزنة (Stored Procedures)
-- ========================================

-- إجراء إضافة/تحديث مستخدم
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

GO

-- إجراء إضافة رسالة
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

GO

-- إجراء تحديث الإحصائيات اليومية
CREATE PROCEDURE sp_UpdateDailyStatistics
    @BotToken NVARCHAR(255)
AS
BEGIN
    SET NOCOUNT ON;
    
    DECLARE @Today DATE = CAST(GETDATE() AS DATE);
    
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

GO

-- إجراء إضافة حدث
CREATE PROCEDURE sp_AddEvent
    @BotToken NVARCHAR(255),
    @UserID BIGINT = NULL,
    @EventType NVARCHAR(50),
    @EventData NVARCHAR(MAX) = NULL,
    @Severity NVARCHAR(20) = 'INFO'
AS
BEGIN
    SET NOCOUNT ON;
    
    INSERT INTO Events (BotToken, UserID, EventType, EventData, Severity)
    VALUES (@BotToken, @UserID, @EventType, @EventData, @Severity);
END;

GO

-- إجراء إحصائيات البوت
CREATE PROCEDURE sp_GetBotStatistics
    @BotToken NVARCHAR(255)
AS
BEGIN
    SET NOCOUNT ON;
    
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
END;

GO

-- ========================================
-- إنشاء الدوال (Functions)
-- ========================================

-- دالة الحصول على إحصائيات المستخدمين الأكثر نشاطاً
CREATE FUNCTION fn_GetTopActiveUsers
(
    @BotToken NVARCHAR(255),
    @Days INT = 7
)
RETURNS TABLE
AS
RETURN
(
    SELECT TOP 10
        u.Username,
        u.MessageCount,
        u.LastSeen,
        COUNT(m.ID) as RecentMessages
    FROM Users u
    LEFT JOIN Messages m ON u.BotToken = m.BotToken AND u.UserID = m.UserID 
        AND m.MessageDate >= DATEADD(DAY, -@Days, GETDATE())
    WHERE u.BotToken = @BotToken
    GROUP BY u.Username, u.MessageCount, u.LastSeen
);

GO

-- ========================================
-- إنشاء Triggers
-- ========================================

-- Trigger لتحديث إحصائيات البوت عند إضافة مستخدم جديد
CREATE TRIGGER tr_Users_Insert
ON Users
AFTER INSERT
AS
BEGIN
    SET NOCOUNT ON;
    
    UPDATE Bots 
    SET TotalUsers = TotalUsers + 1
    FROM Bots b
    INNER JOIN inserted i ON b.BotToken = i.BotToken;
END;

GO

-- Trigger لتحديث إحصائيات البوت عند إضافة رسالة جديدة
CREATE TRIGGER tr_Messages_Insert
ON Messages
AFTER INSERT
AS
BEGIN
    SET NOCOUNT ON;
    
    UPDATE Bots 
    SET TotalMessages = TotalMessages + 1,
        LastActivity = GETDATE()
    FROM Bots b
    INNER JOIN inserted i ON b.BotToken = i.BotToken;
END;

GO

-- ========================================
-- إنشاء Views
-- ========================================

-- View لإحصائيات البوتات النشطة
CREATE VIEW vw_ActiveBots
AS
SELECT 
    b.BotName,
    b.BotUsername,
    b.TotalUsers,
    b.TotalMessages,
    b.LastActivity,
    COUNT(DISTINCT u.UserID) as ActiveUsers
FROM Bots b
LEFT JOIN Users u ON b.BotToken = u.BotToken AND u.LastSeen >= DATEADD(DAY, -1, GETDATE())
WHERE b.IsActive = 1
GROUP BY b.BotName, b.BotUsername, b.TotalUsers, b.TotalMessages, b.LastActivity;

GO

-- View للإحصائيات اليومية
CREATE VIEW vw_DailyStats
AS
SELECT 
    s.BotToken,
    b.BotName,
    s.StatDate,
    s.TotalUsers,
    s.NewUsers,
    s.TotalMessages,
    s.ActiveUsers
FROM Statistics s
INNER JOIN Bots b ON s.BotToken = b.BotToken
WHERE s.StatDate >= DATEADD(DAY, -30, GETDATE());

GO

-- ========================================
-- إعداد الأمان
-- ========================================

-- إنشاء مستخدم للبوت
CREATE LOGIN BotUser WITH PASSWORD = 'StrongBotPassword123!';
CREATE USER BotUser FOR LOGIN BotUser;

-- منح الصلاحيات
GRANT SELECT, INSERT, UPDATE ON Users TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Bots TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Messages TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Statistics TO BotUser;
GRANT SELECT, INSERT, UPDATE ON Events TO BotUser;

-- منح صلاحيات الإجراءات المخزنة
GRANT EXECUTE ON sp_UpsertUser TO BotUser;
GRANT EXECUTE ON sp_AddMessage TO BotUser;
GRANT EXECUTE ON sp_UpdateDailyStatistics TO BotUser;
GRANT EXECUTE ON sp_AddEvent TO BotUser;
GRANT EXECUTE ON sp_GetBotStatistics TO BotUser;

-- منح صلاحيات Views
GRANT SELECT ON vw_ActiveBots TO BotUser;
GRANT SELECT ON vw_DailyStats TO BotUser;

GO

-- ========================================
-- إدخال بيانات تجريبية
-- ========================================

-- إدخال بوت تجريبي
INSERT INTO Bots (BotToken, BotName, BotUsername, IsActive)
VALUES ('encrypted_bot_token_1', 'Test Bot', 'test_bot', 1);

-- إدخال مستخدم تجريبي
INSERT INTO Users (BotToken, UserID, Username, FirstSeen, LastSeen)
VALUES ('encrypted_bot_token_1', 123456789, 'test_user', GETDATE(), GETDATE());

-- إدخال رسالة تجريبية
INSERT INTO Messages (BotToken, UserID, MessageID, MessageType, MessageText, MessageDate)
VALUES ('encrypted_bot_token_1', 123456789, 1, 'text', 'Hello Bot!', GETDATE());

GO

-- ========================================
-- رسالة نجاح الإنشاء
-- ========================================

PRINT '========================================';
PRINT '✅ تم إنشاء قاعدة البيانات بنجاح!';
PRINT '========================================';
PRINT '';
PRINT '📊 الجداول المنشأة:';
PRINT '  - Bots (البوتات)';
PRINT '  - Users (المستخدمين)';
PRINT '  - Messages (الرسائل)';
PRINT '  - Statistics (الإحصائيات)';
PRINT '  - Events (الأحداث)';
PRINT '';
PRINT '🔧 الإجراءات المخزنة:';
PRINT '  - sp_UpsertUser';
PRINT '  - sp_AddMessage';
PRINT '  - sp_UpdateDailyStatistics';
PRINT '  - sp_AddEvent';
PRINT '  - sp_GetBotStatistics';
PRINT '';
PRINT '👤 المستخدم: BotUser';
PRINT '🔑 كلمة المرور: StrongBotPassword123!';
PRINT '';
PRINT '📝 ملاحظة: يرجى تغيير كلمة المرور في الإنتاج!';
PRINT '========================================';