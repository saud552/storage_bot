cmake_minimum_required(VERSION 3.16)
project(TelegramStorageBot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# إعدادات التحسين
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# البحث عن المكتبات المطلوبة
find_package(PkgConfig REQUIRED)

# Telegram Bot API
find_package(PkgConfig REQUIRED)
pkg_check_modules(TGBOT REQUIRED tgbot-cpp)

# Crypto++
find_package(PkgConfig REQUIRED)
pkg_check_modules(CRYPTOPP REQUIRED libcrypto++)

# ODBC
find_package(PkgConfig REQUIRED)
pkg_check_modules(NANODBC REQUIRED nanodbc)

# Boost (إذا كان مطلوباً)
find_package(Boost COMPONENTS system thread REQUIRED)

# إنشاء الملف التنفيذي
add_executable(storage_bot_improved storage_bot_improved.cpp)

# ربط المكتبات
target_link_libraries(storage_bot_improved
    ${TGBOT_LIBRARIES}
    ${CRYPTOPP_LIBRARIES}
    ${NANODBC_LIBRARIES}
    Boost::system
    Boost::thread
    pthread
    ssl
    crypto
)

# إضافة مسارات الرأس
target_include_directories(storage_bot_improved PRIVATE
    ${TGBOT_INCLUDE_DIRS}
    ${CRYPTOPP_INCLUDE_DIRS}
    ${NANODBC_INCLUDE_DIRS}
)

# إضافة flags المترجم
target_compile_options(storage_bot_improved PRIVATE
    ${TGBOT_CFLAGS_OTHER}
    ${CRYPTOPP_CFLAGS_OTHER}
    ${NANODBC_CFLAGS_OTHER}
)

# إعدادات خاصة بـ Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(storage_bot_improved rt)
endif()

# إعدادات خاصة بـ Windows
if(WIN32)
    target_link_libraries(storage_bot_improved ws2_32)
endif()

# إضافة اختبارات الوحدة (اختياري)
enable_testing()
add_test(NAME BasicTest COMMAND storage_bot_improved --test)

# إعدادات التثبيت
install(TARGETS storage_bot_improved
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# إنشاء ملف التكوين
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.env.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.env"
    @ONLY
)

# إنشاء script التشغيل
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/run.sh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/run.sh"
    @ONLY
)