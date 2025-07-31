# 🤖 نظام إدارة بوتات التخزين المحسن

نظام متقدم لإدارة وتشغيل بوتات تيليجرام متعددة مع تخزين مركزي لبيانات المستخدمين.

## ✨ الميزات الجديدة

### 🔧 التحسينات الرئيسية

1. **استخدام Webhook بدلاً من Polling**
   - أداء أفضل واستجابة أسرع
   - استهلاك موارد أقل
   - دعم HTTPS

2. **إدارة محسنة للبوتات**
   - إدارة آمنة للـ threads
   - التحقق من صحة التوكن قبل التشغيل
   - معالجة أفضل للأخطاء

3. **خدمة تشفير محسنة**
   - استخدام Crypto++ بدلاً من OpenSSL
   - تشفير AES-256-GCM
   - إدارة أفضل للمفاتيح

4. **إدارة اتصالات قاعدة البيانات المحسنة**
   - timeout للاتصالات
   - إعادة المحاولة عند الفشل
   - التحقق من صحة الاتصالات

## 🚀 التثبيت والتشغيل

### المتطلبات

- Docker و Docker Compose
- SQL Server (أو استخدام الحاوية المرفقة)
- توكن بوت تيليجرام للمدير
- عنوان HTTPS للـ webhook

### 1. إعداد متغيرات البيئة

أنشئ ملف `.env`:

```bash
# توكن بوت المدير
MANAGER_BOT_TOKEN=your_manager_bot_token_here

# عناوين Webhook
WEBHOOK_URL=https://your-domain.com/webhook
MANAGER_WEBHOOK_URL=https://your-domain.com/manager

# مفتاح التشفير (32 حرف)
ENCRYPTION_KEY=your_32_character_encryption_key_here

# إعدادات قاعدة البيانات (اختياري إذا كنت تستخدم الحاوية)
DB_SERVER=database
DB_NAME=TelegramBots
DB_USER=sa
DB_PASS=YourStrongPassword123!
```

### 2. تشغيل النظام

```bash
# بناء وتشغيل جميع الخدمات
docker-compose up -d

# عرض السجلات
docker-compose logs -f storage_bot

# إيقاف النظام
docker-compose down
```

### 3. البناء المحلي (بدون Docker)

```bash
# تثبيت المتطلبات
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config \
    libssl-dev libcurl4-openssl-dev libboost-all-dev \
    unixodbc-dev libsqlite3-dev

# تثبيت ODBC Driver for SQL Server
curl https://packages.microsoft.com/keys/microsoft.asc | sudo apt-key add -
curl https://packages.microsoft.com/config/ubuntu/22.04/prod.list | sudo tee /etc/apt/sources.list.d/mssql-release.list
sudo apt-get update
sudo ACCEPT_EULA=Y apt-get install -y msodbcsql18

# بناء المشروع
mkdir build && cd build
cmake ..
make -j$(nproc)

# تشغيل النظام
./storage_bot_optimized
```

## 📊 هيكل قاعدة البيانات

```sql
CREATE TABLE users (
    id INT IDENTITY(1,1) PRIMARY KEY,
    user_id BIGINT NOT NULL,
    username NVARCHAR(255) NOT NULL,
    bot_token NVARCHAR(255) NOT NULL,
    created_at DATETIME DEFAULT GETDATE(),
    updated_at DATETIME DEFAULT GETDATE()
);

-- فهارس للأداء
CREATE INDEX idx_user_bot ON users (user_id, bot_token);
CREATE INDEX idx_bot_token ON users (bot_token);
```

## 🔐 الأمان

### التشفير
- **الخوارزمية**: AES-256-GCM
- **المفتاح**: 32 حرف (256 بت)
- **IV**: عشوائي لكل عملية تشفير
- **المصادقة**: GCM tag للتحقق من سلامة البيانات

### متغيرات البيئة المطلوبة
- `MANAGER_BOT_TOKEN`: توكن بوت المدير
- `WEBHOOK_URL`: عنوان HTTPS للـ webhook
- `ENCRYPTION_KEY`: مفتاح التشفير (32 حرف)

## 🎯 الاستخدام

### 1. بدء النظام
```bash
docker-compose up -d
```

### 2. إضافة بوت جديد
1. افتح بوت المدير في تيليجرام
2. أرسل الأمر `/start`
3. اضغط على "➕ إضافة بوت"
4. أرسل توكن البوت الجديد

### 3. مراقبة الإحصائيات
- استخدم زر "📊 الإحصائيات" في بوت المدير
- عرض عدد المستخدمين لكل بوت
- مراقبة حالة البوتات

## 🔧 التكوين المتقدم

### إعدادات Nginx (اختياري)

أنشئ ملف `nginx.conf`:

```nginx
events {
    worker_connections 1024;
}

http {
    upstream bot_backend {
        server storage_bot:8443;
    }

    server {
        listen 80;
        server_name your-domain.com;
        return 301 https://$server_name$request_uri;
    }

    server {
        listen 443 ssl;
        server_name your-domain.com;

        ssl_certificate /etc/nginx/ssl/cert.pem;
        ssl_certificate_key /etc/nginx/ssl/key.pem;

        location /webhook/ {
            proxy_pass http://bot_backend;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto $scheme;
        }

        location /manager {
            proxy_pass http://bot_backend;
            proxy_set_header Host $host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header X-Forwarded-Proto $scheme;
        }
    }
}
```

### إعدادات SSL

```bash
# إنشاء شهادة SSL ذاتية التوقيع (للاختبار)
mkdir ssl
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
    -keyout ssl/key.pem -out ssl/cert.pem
```

## 📈 الأداء

### التحسينات المطبقة

1. **المعالجة الدفعية**
   - تجميع الرسائل في batches
   - معالجة كل 5 ثوان أو عند امتلاء الـ batch

2. **Connection Pooling**
   - إدارة اتصالات قاعدة البيانات
   - إعادة استخدام الاتصالات
   - timeout وإعادة المحاولة

3. **Webhook بدلاً من Polling**
   - استجابة فورية للرسائل
   - استهلاك موارد أقل
   - قابلية للتوسع

4. **إدارة الذاكرة**
   - استخدام `std::vector` بدلاً من Boost
   - إدارة آمنة للموارد
   - RAII patterns

## 🐛 استكشاف الأخطاء

### مشاكل شائعة

1. **خطأ في الاتصال بقاعدة البيانات**
   ```bash
   # التحقق من حالة قاعدة البيانات
   docker-compose logs database
   
   # اختبار الاتصال
   docker exec -it telegram_bots_db /opt/mssql-tools/bin/sqlcmd \
       -S localhost -U sa -P YourStrongPassword123!
   ```

2. **خطأ في Webhook**
   ```bash
   # التحقق من السجلات
   docker-compose logs storage_bot
   
   # اختبار Webhook
   curl -X POST https://your-domain.com/webhook/test
   ```

3. **مشاكل في التشفير**
   ```bash
   # التحقق من مفتاح التشفير
   echo $ENCRYPTION_KEY | wc -c  # يجب أن يكون 33 (32 + newline)
   ```

### السجلات

```bash
# عرض سجلات البوت
docker-compose logs -f storage_bot

# عرض سجلات قاعدة البيانات
docker-compose logs -f database

# عرض جميع السجلات
docker-compose logs -f
```

## 🔄 التحديثات

### إيقاف النظام
```bash
docker-compose down
```

### تحديث الكود
```bash
git pull origin main
docker-compose build --no-cache
docker-compose up -d
```

## 📝 الترخيص

هذا المشروع مرخص تحت رخصة MIT.

## 🤝 المساهمة

نرحب بالمساهمات! يرجى:

1. Fork المشروع
2. إنشاء branch للميزة الجديدة
3. Commit التغييرات
4. Push إلى Branch
5. إنشاء Pull Request

## 📞 الدعم

للدعم التقني أو الأسئلة:
- افتح issue في GitHub
- راجع الوثائق
- تحقق من السجلات

---

**ملاحظة**: تأكد من تحديث عناوين Webhook وملفات SSL قبل التشغيل في الإنتاج.