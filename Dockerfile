# استخدام Ubuntu 22.04 كقاعدة
FROM ubuntu:22.04

# تعيين متغيرات البيئة
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# تحديث النظام وتثبيت الحزم الأساسية
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    git \
    curl \
    wget \
    unzip \
    libssl-dev \
    libcurl4-openssl-dev \
    libboost-all-dev \
    unixodbc-dev \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

# تثبيت ODBC Driver for SQL Server
RUN curl https://packages.microsoft.com/keys/microsoft.asc | apt-key add - \
    && curl https://packages.microsoft.com/config/ubuntu/22.04/prod.list > /etc/apt/sources.list.d/mssql-release.list \
    && apt-get update \
    && ACCEPT_EULA=Y apt-get install -y msodbcsql18 \
    && rm -rf /var/lib/apt/lists/*

# تثبيت Crypto++
RUN wget https://github.com/weidai11/cryptopp/archive/refs/tags/CRYPTOPP_8_7_0.tar.gz \
    && tar -xzf CRYPTOPP_8_7_0.tar.gz \
    && cd cryptopp-CRYPTOPP_8_7_0 \
    && make -j$(nproc) \
    && make install \
    && cd .. \
    && rm -rf cryptopp-CRYPTOPP_8_7_0*

# تثبيت nanodbc
RUN git clone https://github.com/nanodbc/nanodbc.git \
    && cd nanodbc \
    && mkdir build && cd build \
    && cmake .. \
    && make -j$(nproc) \
    && make install \
    && cd ../.. \
    && rm -rf nanodbc

# تثبيت tgbot-cpp
RUN git clone https://github.com/reo7sp/tgbot-cpp.git \
    && cd tgbot-cpp \
    && mkdir build && cd build \
    && cmake .. \
    && make -j$(nproc) \
    && make install \
    && cd ../.. \
    && rm -rf tgbot-cpp

# إنشاء مجلد العمل
WORKDIR /app

# نسخ ملفات المشروع
COPY . .

# بناء المشروع
RUN mkdir build && cd build \
    && cmake .. \
    && make -j$(nproc)

# إنشاء مستخدم غير root
RUN useradd -m -s /bin/bash botuser \
    && chown -R botuser:botuser /app

# التبديل إلى المستخدم الجديد
USER botuser

# تعيين متغيرات البيئة الافتراضية
ENV WEBHOOK_URL=https://your-domain.com/webhook
ENV MANAGER_WEBHOOK_URL=https://your-domain.com/manager
ENV DB_SERVER=localhost
ENV DB_NAME=TelegramBots
ENV DB_USER=sa
ENV DB_PASS=password

# فتح المنافذ المطلوبة
EXPOSE 8443

# إنشاء script التشغيل
RUN echo '#!/bin/bash\n\
if [ -z "$MANAGER_BOT_TOKEN" ]; then\n\
    echo "Error: MANAGER_BOT_TOKEN environment variable is required"\n\
    exit 1\n\
fi\n\
\n\
if [ -z "$WEBHOOK_URL" ]; then\n\
    echo "Error: WEBHOOK_URL environment variable is required"\n\
    exit 1\n\
fi\n\
\n\
if [ -z "$ENCRYPTION_KEY" ]; then\n\
    echo "Warning: ENCRYPTION_KEY not set, using ephemeral key"\n\
fi\n\
\n\
cd /app/build\n\
./storage_bot_improved\n\
' > /app/run.sh && chmod +x /app/run.sh

# نقطة الدخول
ENTRYPOINT ["/app/run.sh"]