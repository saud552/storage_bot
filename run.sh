#!/bin/bash

# ========================================
# نظام إدارة بوتات التخزين - Script التشغيل
# ========================================

set -e

# ألوان للطباعة
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# دالة الطباعة الملونة
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# دالة التحقق من وجود Docker
check_docker() {
    if ! command -v docker &> /dev/null; then
        print_error "Docker غير مثبت. يرجى تثبيت Docker أولاً."
        exit 1
    fi
    
    if ! command -v docker-compose &> /dev/null; then
        print_error "Docker Compose غير مثبت. يرجى تثبيت Docker Compose أولاً."
        exit 1
    fi
    
    print_success "Docker و Docker Compose مثبتان"
}

# دالة التحقق من ملف .env
check_env_file() {
    if [ ! -f ".env" ]; then
        print_warning "ملف .env غير موجود"
        if [ -f ".env.example" ]; then
            print_status "إنشاء ملف .env من .env.example..."
            cp .env.example .env
            print_warning "يرجى تعديل ملف .env وإضافة القيم الصحيحة"
            print_status "أهم المتغيرات المطلوبة:"
            echo "  - MANAGER_BOT_TOKEN"
            echo "  - WEBHOOK_URL"
            echo "  - ENCRYPTION_KEY"
            exit 1
        else
            print_error "ملف .env.example غير موجود"
            exit 1
        fi
    fi
    
    # التحقق من المتغيرات المطلوبة
    source .env
    
    if [ -z "$MANAGER_BOT_TOKEN" ]; then
        print_error "MANAGER_BOT_TOKEN مطلوب في ملف .env"
        exit 1
    fi
    
    if [ -z "$WEBHOOK_URL" ]; then
        print_error "WEBHOOK_URL مطلوب في ملف .env"
        exit 1
    fi
    
    if [ -z "$ENCRYPTION_KEY" ]; then
        print_error "ENCRYPTION_KEY مطلوب في ملف .env"
        exit 1
    fi
    
    # التحقق من طول مفتاح التشفير
    if [ ${#ENCRYPTION_KEY} -ne 32 ]; then
        print_error "ENCRYPTION_KEY يجب أن يكون 32 حرف بالضبط"
        exit 1
    fi
    
    print_success "ملف .env صحيح"
}

# دالة إنشاء المجلدات المطلوبة
create_directories() {
    print_status "إنشاء المجلدات المطلوبة..."
    
    mkdir -p logs
    mkdir -p config
    mkdir -p ssl
    
    print_success "تم إنشاء المجلدات"
}

# دالة إنشاء شهادة SSL (إذا لم تكن موجودة)
create_ssl_certificate() {
    if [ ! -f "ssl/cert.pem" ] || [ ! -f "ssl/key.pem" ]; then
        print_status "إنشاء شهادة SSL ذاتية التوقيع..."
        
        openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
            -keyout ssl/key.pem -out ssl/cert.pem \
            -subj "/C=US/ST=State/L=City/O=Organization/CN=localhost" \
            2>/dev/null || {
            print_warning "فشل في إنشاء شهادة SSL. تأكد من تثبيت OpenSSL"
        }
        
        print_success "تم إنشاء شهادة SSL"
    else
        print_status "شهادة SSL موجودة بالفعل"
    fi
}

# دالة بناء وتشغيل النظام
start_system() {
    print_status "بدء بناء النظام..."
    
    # إيقاف النظام إذا كان يعمل
    docker-compose down 2>/dev/null || true
    
    # بناء وتشغيل النظام
    docker-compose up -d --build
    
    print_success "تم بدء النظام"
}

# دالة عرض حالة النظام
show_status() {
    print_status "حالة النظام:"
    docker-compose ps
    
    print_status "السجلات الأخيرة:"
    docker-compose logs --tail=20 storage_bot
}

# دالة إيقاف النظام
stop_system() {
    print_status "إيقاف النظام..."
    docker-compose down
    print_success "تم إيقاف النظام"
}

# دالة إعادة تشغيل النظام
restart_system() {
    print_status "إعادة تشغيل النظام..."
    docker-compose restart
    print_success "تم إعادة تشغيل النظام"
}

# دالة عرض السجلات
show_logs() {
    print_status "عرض السجلات (اضغط Ctrl+C للخروج):"
    docker-compose logs -f storage_bot
}

# دالة تنظيف النظام
clean_system() {
    print_warning "هذا سيحذف جميع البيانات. هل أنت متأكد؟ (y/N)"
    read -r response
    if [[ "$response" =~ ^[Yy]$ ]]; then
        print_status "تنظيف النظام..."
        docker-compose down -v
        docker system prune -f
        print_success "تم تنظيف النظام"
    else
        print_status "تم إلغاء العملية"
    fi
}

# دالة عرض المساعدة
show_help() {
    echo "استخدام: $0 [COMMAND]"
    echo ""
    echo "الأوامر المتاحة:"
    echo "  start     - بدء النظام"
    echo "  stop      - إيقاف النظام"
    echo "  restart   - إعادة تشغيل النظام"
    echo "  status    - عرض حالة النظام"
    echo "  logs      - عرض السجلات"
    echo "  clean     - تنظيف النظام (حذف جميع البيانات)"
    echo "  help      - عرض هذه المساعدة"
    echo ""
    echo "أمثلة:"
    echo "  $0 start"
    echo "  $0 logs"
    echo "  $0 status"
}

# الدالة الرئيسية
main() {
    case "${1:-start}" in
        "start")
            check_docker
            check_env_file
            create_directories
            create_ssl_certificate
            start_system
            show_status
            ;;
        "stop")
            stop_system
            ;;
        "restart")
            restart_system
            show_status
            ;;
        "status")
            show_status
            ;;
        "logs")
            show_logs
            ;;
        "clean")
            clean_system
            ;;
        "help"|"-h"|"--help")
            show_help
            ;;
        *)
            print_error "أمر غير معروف: $1"
            show_help
            exit 1
            ;;
    esac
}

# تشغيل الدالة الرئيسية
main "$@"