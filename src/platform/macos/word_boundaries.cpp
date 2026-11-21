#include "word_boundaries.h"
#include <CoreFoundation/CoreFoundation.h>

QPair<int, int> macWordRange(const QString &text, int position)
{
    // 汉语没有空格，使用系统词典切词；限制上下文，拖选时不处理整段长历史。
    const int offset = qMax(0, position - 1024);
    const int length = qMin(int(text.size()) - offset, 2048);
    const auto string = CFStringCreateWithCharacters(kCFAllocatorDefault,
        reinterpret_cast<const UniChar *>(text.utf16() + offset), length);
    if (!string) return {-1, -1};
    const auto locale = CFLocaleCopyCurrent();
    const auto tokenizer = CFStringTokenizerCreate(kCFAllocatorDefault, string,
        CFRangeMake(0, length), kCFStringTokenizerUnitWordBoundary, locale);
    QPair<int, int> result{-1, -1};
    if (tokenizer) {
        if (CFStringTokenizerGoToTokenAtIndex(tokenizer, position - offset) != kCFStringTokenizerTokenNone) {
            const auto range = CFStringTokenizerGetCurrentTokenRange(tokenizer);
            if (range.location >= 0 && range.length > 0) result = {int(range.location) + offset, int(range.location + range.length) + offset};
        }
        CFRelease(tokenizer);
    }
    if (locale) CFRelease(locale);
    CFRelease(string);
    return result;
}
