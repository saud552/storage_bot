#!/bin/bash

# ========================================
# Script للتحقق من تثبيت tgbot-cpp
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

echo "========================================"
echo "🔍 التحقق من تثبيت tgbot-cpp"
echo "========================================"

# التحقق من وجود pkg-config
print_status "التحقق من وجود pkg-config..."
if command -v pkg-config &> /dev/null; then
    print_success "pkg-config مثبت"
else
    print_error "pkg-config غير مثبت. يرجى تثبيته أولاً."
    exit 1
fi

# التحقق من وجود tgbot-cpp
print_status "التحقق من وجود tgbot-cpp..."
if pkg-config --exists tgbot-cpp; then
    print_success "tgbot-cpp مثبت"
    
    # عرض معلومات المكتبة
    echo ""
    print_status "معلومات tgbot-cpp:"
    echo "  الإصدار: $(pkg-config --modversion tgbot-cpp)"
    echo "  مسارات الرأس: $(pkg-config --cflags tgbot-cpp)"
    echo "  المكتبات: $(pkg-config --libs tgbot-cpp)"
else
    print_error "tgbot-cpp غير مثبت"
    echo ""
    print_status "طرق التثبيت:"
    echo ""
    echo "1. تثبيت من الحزم:"
    echo "   Ubuntu/Debian: sudo apt-get install libtgbot-cpp-dev"
    echo "   CentOS/RHEL: sudo yum install tgbot-cpp-devel"
    echo "   Arch Linux: sudo pacman -S tgbot-cpp"
    echo ""
    echo "2. تثبيت من المصدر:"
    echo "   git clone https://github.com/reo7sp/tgbot-cpp.git"
    echo "   cd tgbot-cpp"
    echo "   mkdir build && cd build"
    echo "   cmake .."
    echo "   make -j$(nproc)"
    echo "   sudo make install"
    echo ""
    exit 1
fi

# التحقق من وجود ملفات الرأس
print_status "التحقق من ملفات الرأس..."
HEADER_DIR=$(pkg-config --variable=includedir tgbot-cpp 2>/dev/null || echo "/usr/local/include")
if [ -f "$HEADER_DIR/tgbot/tgbot.h" ]; then
    print_success "ملف الرأس موجود: $HEADER_DIR/tgbot/tgbot.h"
else
    print_warning "ملف الرأس غير موجود في $HEADER_DIR/tgbot/tgbot.h"
    
    # البحث في مواقع أخرى
    for dir in "/usr/include" "/usr/local/include" "/opt/local/include"; do
        if [ -f "$dir/tgbot/tgbot.h" ]; then
            print_success "ملف الرأس موجود في: $dir/tgbot/tgbot.h"
            break
        fi
    done
fi

# التحقق من وجود المكتبة
print_status "التحقق من المكتبة..."
LIB_DIR=$(pkg-config --variable=libdir tgbot-cpp 2>/dev/null || echo "/usr/local/lib")
if [ -f "$LIB_DIR/libtgbot.so" ] || [ -f "$LIB_DIR/libtgbot.a" ]; then
    print_success "المكتبة موجودة في: $LIB_DIR"
else
    print_warning "المكتبة غير موجودة في $LIB_DIR"
    
    # البحث في مواقع أخرى
    for dir in "/usr/lib" "/usr/local/lib" "/opt/local/lib"; do
        if [ -f "$dir/libtgbot.so" ] || [ -f "$dir/libtgbot.a" ]; then
            print_success "المكتبة موجودة في: $dir"
            break
        fi
    done
fi

# اختبار البناء البسيط
print_status "اختبار البناء البسيط..."
cat > test_tgbot.cpp << 'EOF'
#include <iostream>
#include <tgbot/tgbot.h>

using namespace std;
using namespace TgBot;

int main() {
    try {
        cout << "✅ tgbot-cpp يعمل بشكل صحيح" << endl;
        cout << "📚 الإصدار: " << TGBOT_VERSION << endl;
        return 0;
    } catch (const exception& e) {
        cerr << "❌ خطأ: " << e.what() << endl;
        return 1;
    }
}
EOF

# محاولة البناء
if g++ -std=c++17 test_tgbot.cpp -o test_tgbot $(pkg-config --cflags --libs tgbot-cpp) 2>/dev/null; then
    print_success "تم بناء الاختبار بنجاح"
    
    # تشغيل الاختبار
    if ./test_tgbot; then
        print_success "✅ tgbot-cpp يعمل بشكل صحيح!"
    else
        print_error "❌ فشل في تشغيل الاختبار"
    fi
    
    # تنظيف
    rm -f test_tgbot.cpp test_tgbot
else
    print_error "❌ فشل في بناء الاختبار"
    print_status "أسباب محتملة:"
    echo "  - المكتبة غير مثبتة بشكل صحيح"
    echo "  - مسارات الرأس غير صحيحة"
    echo "  - المكتبات المطلوبة مفقودة"
    echo ""
    print_status "حلول مقترحة:"
    echo "  1. إعادة تثبيت tgbot-cpp"
    echo "  2. التحقق من متغيرات البيئة"
    echo "  3. تحديث ldconfig"
    
    # تنظيف
    rm -f test_tgbot.cpp test_tgbot
    exit 1
fi

# التحقق من المكتبات المطلوبة
print_status "التحقق من المكتبات المطلوبة..."
REQUIRED_LIBS=("ssl" "crypto" "curl" "pthread")

for lib in "${REQUIRED_LIBS[@]}"; do
    if ldconfig -p | grep -q "lib$lib.so"; then
        print_success "المكتبة $lib موجودة"
    else
        print_warning "المكتبة $lib غير موجودة"
    fi
done

echo ""
echo "========================================"
print_success "✅ تم الانتهاء من التحقق بنجاح!"
echo "========================================"
echo ""
print_status "معلومات إضافية:"
echo "  📚 Repository: https://github.com/reo7sp/tgbot-cpp"
echo "  📖 التوثيق: https://github.com/reo7sp/tgbot-cpp/wiki"
echo "  🐛 المسائل: https://github.com/reo7sp/tgbot-cpp/issues"
echo ""
print_status "للبناء:"
echo "  mkdir build && cd build"
echo "  cmake .."
echo "  make -j$(nproc)"
echo ""