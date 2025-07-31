#!/bin/bash

# ========================================
# سكريبت إدارة قاعدة البيانات
# ========================================

set -e

# الألوان للطباعة
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# دالة طباعة الرسائل
print_message() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

# دالة التحقق من وجود Docker
check_docker() {
    if ! command -v docker &> /dev/null; then
        print_error "Docker غير مثبت. يرجى تثبيت Docker أولاً."
        exit 1
    fi
    
    if ! docker info &> /dev/null; then
        print_error "Docker غير قيد التشغيل. يرجى تشغيل Docker أولاً."
        exit 1
    fi
}

# دالة التحقق من وجود Docker Compose
check_docker_compose() {
    if ! command -v docker-compose &> /dev/null; then
        print_error "Docker Compose غير مثبت. يرجى تثبيت Docker Compose أولاً."
        exit 1
    fi
}

# دالة بدء قاعدة البيانات
start_database() {
    print_header "بدء قاعدة البيانات"
    
    check_docker
    check_docker_compose
    
    print_message "بدء تشغيل MSSQL و Redis..."
    
    docker-compose -f docker-compose-db.yml up -d
    
    print_message "انتظار بدء قاعدة البيانات..."
    sleep 30
    
    # التحقق من حالة قاعدة البيانات
    if docker-compose -f docker-compose-db.yml ps | grep -q "Up"; then
        print_message "✅ تم بدء قاعدة البيانات بنجاح!"
        print_message "📊 MSSQL متاح على: localhost:1433"
        print_message "🔴 Redis متاح على: localhost:6379"
    else
        print_error "❌ فشل في بدء قاعدة البيانات"
        exit 1
    fi
}

# دالة إيقاف قاعدة البيانات
stop_database() {
    print_header "إيقاف قاعدة البيانات"
    
    print_message "إيقاف الخدمات..."
    docker-compose -f docker-compose-db.yml down
    
    print_message "✅ تم إيقاف قاعدة البيانات بنجاح!"
}

# دالة إعادة تشغيل قاعدة البيانات
restart_database() {
    print_header "إعادة تشغيل قاعدة البيانات"
    
    stop_database
    sleep 5
    start_database
}

# دالة عرض حالة قاعدة البيانات
status_database() {
    print_header "حالة قاعدة البيانات"
    
    if docker-compose -f docker-compose-db.yml ps | grep -q "Up"; then
        print_message "✅ قاعدة البيانات قيد التشغيل"
        docker-compose -f docker-compose-db.yml ps
    else
        print_warning "⚠️ قاعدة البيانات متوقفة"
    fi
}

# دالة إنشاء قاعدة البيانات
setup_database() {
    print_header "إعداد قاعدة البيانات"
    
    start_database
    
    print_message "انتظار جاهزية MSSQL..."
    sleep 60
    
    # تشغيل سكريبت إعداد قاعدة البيانات
    print_message "تشغيل سكريبت إعداد قاعدة البيانات..."
    
    docker exec telegram_bots_mssql_admin /opt/mssql-tools/bin/sqlcmd \
        -S mssql \
        -U sa \
        -P StrongPassword123! \
        -i /docker-entrypoint-initdb.d/setup.sql
    
    print_message "✅ تم إعداد قاعدة البيانات بنجاح!"
}

# دالة النسخ الاحتياطي
backup_database() {
    print_header "إنشاء نسخة احتياطية"
    
    BACKUP_DIR="./backups"
    BACKUP_FILE="telegram_bots_backup_$(date +%Y%m%d_%H%M%S).bak"
    
    mkdir -p "$BACKUP_DIR"
    
    print_message "إنشاء نسخة احتياطية: $BACKUP_FILE"
    
    docker exec telegram_bots_mssql /opt/mssql-tools/bin/sqlcmd \
        -S localhost \
        -U sa \
        -P StrongPassword123! \
        -Q "BACKUP DATABASE TelegramBots TO DISK = '/var/opt/mssql/backup/$BACKUP_FILE'"
    
    docker cp telegram_bots_mssql:/var/opt/mssql/backup/$BACKUP_FILE "$BACKUP_DIR/"
    
    print_message "✅ تم إنشاء النسخة الاحتياطية بنجاح!"
    print_message "📁 الموقع: $BACKUP_DIR/$BACKUP_FILE"
}

# دالة استعادة قاعدة البيانات
restore_database() {
    print_header "استعادة قاعدة البيانات"
    
    if [ -z "$1" ]; then
        print_error "يرجى تحديد ملف النسخة الاحتياطية"
        echo "الاستخدام: $0 restore <backup_file>"
        exit 1
    fi
    
    BACKUP_FILE="$1"
    
    if [ ! -f "$BACKUP_FILE" ]; then
        print_error "ملف النسخة الاحتياطية غير موجود: $BACKUP_FILE"
        exit 1
    fi
    
    print_message "استعادة من: $BACKUP_FILE"
    
    # نسخ الملف إلى الحاوية
    docker cp "$BACKUP_FILE" telegram_bots_mssql:/var/opt/mssql/backup/
    
    # استعادة قاعدة البيانات
    docker exec telegram_bots_mssql /opt/mssql-tools/bin/sqlcmd \
        -S localhost \
        -U sa \
        -P StrongPassword123! \
        -Q "RESTORE DATABASE TelegramBots FROM DISK = '/var/opt/mssql/backup/$(basename $BACKUP_FILE)' WITH REPLACE"
    
    print_message "✅ تم استعادة قاعدة البيانات بنجاح!"
}

# دالة تنظيف قاعدة البيانات
cleanup_database() {
    print_header "تنظيف قاعدة البيانات"
    
    print_message "حذف الرسائل القديمة (أكثر من 90 يوم)..."
    
    docker exec telegram_bots_mssql /opt/mssql-tools/bin/sqlcmd \
        -S localhost \
        -U sa \
        -P StrongPassword123! \
        -Q "DELETE FROM Messages WHERE MessageDate < DATEADD(DAY, -90, GETDATE())"
    
    print_message "حذف الأحداث القديمة (أكثر من سنة)..."
    
    docker exec telegram_bots_mssql /opt/mssql-tools/bin/sqlcmd \
        -S localhost \
        -U sa \
        -P StrongPassword123! \
        -Q "DELETE FROM Events WHERE EventDate < DATEADD(YEAR, -1, GETDATE())"
    
    print_message "✅ تم تنظيف قاعدة البيانات بنجاح!"
}

# دالة عرض الإحصائيات
show_statistics() {
    print_header "إحصائيات قاعدة البيانات"
    
    print_message "إحصائيات البوتات:"
    docker exec telegram_bots_mssql /opt/mssql-tools/bin/sqlcmd \
        -S localhost \
        -U sa \
        -P StrongPassword123! \
        -Q "SELECT BotName, TotalUsers, TotalMessages, LastActivity FROM Bots"
    
    echo ""
    
    print_message "إحصائيات المستخدمين:"
    docker exec telegram_bots_mssql /opt/mssql-tools/bin/sqlcmd \
        -S localhost \
        -U sa \
        -P StrongPassword123! \
        -Q "SELECT COUNT(*) as TotalUsers, COUNT(CASE WHEN LastSeen >= DATEADD(DAY, -1, GETDATE()) THEN 1 END) as ActiveUsers FROM Users"
    
    echo ""
    
    print_message "إحصائيات الرسائل:"
    docker exec telegram_bots_mssql /opt/mssql-tools/bin/sqlcmd \
        -S localhost \
        -U sa \
        -P StrongPassword123! \
        -Q "SELECT COUNT(*) as TotalMessages, COUNT(CASE WHEN MessageDate >= DATEADD(DAY, -1, GETDATE()) THEN 1 END) as TodayMessages FROM Messages"
}

# دالة عرض المساعدة
show_help() {
    print_header "مساعدة إدارة قاعدة البيانات"
    
    echo "الاستخدام: $0 <command> [options]"
    echo ""
    echo "الأوامر المتاحة:"
    echo "  start       - بدء قاعدة البيانات"
    echo "  stop        - إيقاف قاعدة البيانات"
    echo "  restart     - إعادة تشغيل قاعدة البيانات"
    echo "  status      - عرض حالة قاعدة البيانات"
    echo "  setup       - إعداد قاعدة البيانات (إنشاء الجداول)"
    echo "  backup      - إنشاء نسخة احتياطية"
    echo "  restore     - استعادة قاعدة البيانات"
    echo "  cleanup     - تنظيف البيانات القديمة"
    echo "  stats       - عرض الإحصائيات"
    echo "  help        - عرض هذه المساعدة"
    echo ""
    echo "أمثلة:"
    echo "  $0 start"
    echo "  $0 backup"
    echo "  $0 restore ./backups/telegram_bots_backup_20231201_120000.bak"
    echo "  $0 stats"
}

# التحقق من الأوامر
case "$1" in
    start)
        start_database
        ;;
    stop)
        stop_database
        ;;
    restart)
        restart_database
        ;;
    status)
        status_database
        ;;
    setup)
        setup_database
        ;;
    backup)
        backup_database
        ;;
    restore)
        restore_database "$2"
        ;;
    cleanup)
        cleanup_database
        ;;
    stats)
        show_statistics
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        print_error "أمر غير معروف: $1"
        echo ""
        show_help
        exit 1
        ;;
esac