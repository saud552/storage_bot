#include <iostream>
#include <tgbot/tgbot.h>

using namespace std;
using namespace TgBot;

int main() {
    cout << "=== تحقق من استخدام tgbot-cpp ===" << endl;
    
    // التحقق من وجود الكلاسات الأساسية
    try {
        // إنشاء كائن Bot (سيتم تجاهل التوكن للاختبار)
        Bot bot("test_token");
        cout << "✅ تم إنشاء كائن Bot بنجاح" << endl;
        
        // التحقق من وجود Webhook
        TgWebhook webhook(bot, "https://test.com/webhook");
        cout << "✅ تم إنشاء Webhook بنجاح" << endl;
        
        // التحقق من وجود LongPoll (للاختبار)
        TgLongPoll longPoll(bot, 50, 10);
        cout << "✅ تم إنشاء LongPoll بنجاح" << endl;
        
        cout << "\n=== معلومات المكتبة ===" << endl;
        cout << "📚 المكتبة: tgbot-cpp" << endl;
        cout << "🔗 Repository: https://github.com/reo7sp/tgbot-cpp" << endl;
        cout << "📦 Package: tgbot-cpp" << endl;
        cout << "✅ الحالة: متوافق ومثبت بشكل صحيح" << endl;
        
        return 0;
    } catch (const exception& e) {
        cerr << "❌ خطأ في التحقق من tgbot-cpp: " << e.what() << endl;
        return 1;
    }
}