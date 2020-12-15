#include "String.h"
#include "Vector.h"
#include "MemoryManager.h"

#include <stdarg.h>

typedef struct String {
    Vector* chars; // vector of characters
    bool isProxy; // normally false. If true, the vector is not touched when deallocating
    uint32_t hashval; // cached hash value. Any time we change the string, this should be set to 0.
} String;

RegisterVectorStatics(V)
RegisterVectorFor(char, V)


inline ArenaPtr FindArena(VectorPtr vec) {
    auto arena = VectorArena(vec);
	if (arena == NULL) arena = MMCurrent();
	return arena;
}

String * StringEmpty() {
    auto vec = VAllocate_char();
    if (!VectorIsValid(vec)) return NULL;

    auto arena = FindArena(vec);
	if (arena == NULL) return NULL;

    auto str = (String*)ArenaAllocateAndClear(arena, sizeof(String));
    str->chars = vec;
    str->hashval = 0;
    str->isProxy = false;

    return str;
}

// Create an empty string in a specific memory arena
String *StringEmptyInArena(Arena* a) {
    if (a == NULL) return NULL;
    auto vec = VAllocateArena_char(a);
    if (!VectorIsValid(vec)) return NULL;

    auto str = (String*)ArenaAllocateAndClear(a, sizeof(String));
    str->chars = vec;
    str->hashval = 0;
    str->isProxy = false;

    return str;
}

String* StringProxy(String* original) {
    if (original == NULL) return NULL;

    auto arena = FindArena(original->chars);
	if (arena == NULL) return NULL;

    auto str = (String*)ArenaAllocateAndClear(arena, sizeof(String)); // put the proxy in the same memory as the original
    str->chars = original->chars;
    str->hashval = 0;
    str->isProxy = true;

    return str;
}

void StringClear(String *str) {
    if (str == NULL) return;
    if (VectorIsValid(str->chars) == false) return;

    str->hashval = 0;
    VClear(str->chars);
}

void StringDeallocate(String *str) {
    if (str == NULL) return;

    auto arena = FindArena(str->chars);
    if (str->isProxy == false && VectorIsValid(str->chars) == true) VectorDeallocate(str->chars);

    ArenaDereference(arena, str);
}

bool StringIsValid(String *str) {
    if (str == NULL) return false;
    return VectorIsValid(str->chars);
}

String* StringNewInArena(const char* str, Arena* a) {
    auto result = (a == NULL) ? StringEmpty() : StringEmptyInArena(a);
    if (result == NULL) return NULL;
    if (VectorIsValid(result->chars) == false) return NULL;

    while (*str != 0) {
        VPush_char(result->chars, *str);
        str++;
    }
    return result;
}

String* StringNew(const char * str) {
    return StringNewInArena(str, NULL);
}

String *StringNew(char c) {
    auto result = StringEmpty();
    if (result == NULL) return NULL;
    if (VectorIsValid(result->chars) == false) return NULL;
    VPush_char(result->chars, c);
    return result;
}

void StringAppendInt32(String *str, int32_t value) {
    if (str == NULL) return;
    if (VectorIsValid(str->chars) == false) return;

    str->hashval = 0;
    bool latch = false; // have we got a sig digit yet?
    int64_t remains = value;
    if (remains < 0) {
        VPush_char(str->chars, '-');
        remains = -remains;
    }
    int64_t scale = 1000000000;// max value of int32 = 2147483647
    int64_t digit = 0;

    while (remains > 0 || scale > 0) {
        digit = remains / scale;

        if (digit > 0 || latch) {
            latch = true;
            VPush_char(str->chars, '0' + digit);
            remains = remains % scale;
        }

        scale /= 10;
    }

    // if exactly zero...
    if (!latch) VPush_char(str->chars, '0');
}

String* StringFromInt32(int32_t i) {
    auto str = StringEmpty();
    StringAppendInt32(str, i);
    return str;
}

void StringAppendInt8Hex(String *str, uint8_t value) {
    if (str == NULL) return;
    if (VectorIsValid(str->chars) == false) return;

    str->hashval = 0;
    uint32_t nybble = 0xF0;
    uint32_t digit = 0;
    for (int i = 4; i >= 0; i-=4) {
        digit = (value & nybble) >> i;
        if (digit <= 9) VPush_char(str->chars, '0' + digit);
        else VPush_char(str->chars, '7' + digit); // line up with capital 'A'
        nybble >>= 4;
    }
}

void StringAppendInt32Hex(String *str, uint32_t value) {
    if (str == NULL) return;
    if (VectorIsValid(str->chars) == false) return;

    str->hashval = 0;
    uint32_t nybble = 0xF0000000;
    uint32_t digit = 0;
    for (int i = 28; i >= 0; i-=4) {
        digit = (value & nybble) >> i;
        if (digit <= 9) VPush_char(str->chars, '0' + digit);
        else VPush_char(str->chars, '7' + digit); // line up with capital 'A'
        nybble >>= 4;
    }
}

void StringAppendInt64Hex(String *str, uint64_t value) {
    uint64_t upper = value >> 32;
    StringAppendInt32Hex(str, upper & 0xFFFFFFFF);
    StringAppendInt32Hex(str, value & 0xFFFFFFFF);
}

void StringAppendDouble(String *str, double value) {
    if (str == NULL) return;
    if (VectorIsValid(str->chars) == false) return;

    str->hashval = 0;

    double uvalue = value;
    if (value < 0) {
        VPush_char(str->chars, '-');
        uvalue = -value;
    }

    // TODO: this whole thing could do with fixing...
    uint32_t intpart = uvalue;
    uint32_t fracpart = (uvalue - intpart) * 100000;

    StringAppendInt32(str, intpart);
    VPush_char(str->chars, '.');

    int64_t digit = 0;

    uint32_t scale = 10000;
    //fracpart = fracpart * scale * 10;
    bool tail = true;
    while (fracpart > 0 && scale > 0) {
        digit = fracpart / scale;
        fracpart = fracpart % scale;

        tail = false;
        VPush_char(str->chars, '0' + digit);

        scale /= 10;
    }

    if (tail) VPush_char(str->chars, '0'); // fractional part is exactly zero
}

void StringAppend(String *first, String *second) {
    if (first == NULL || second == NULL) return;
    unsigned int len = VLength(second->chars);
    for (unsigned int i = 0; i < len; i++) {
        VPush_char(first->chars, *VGet_char(second->chars, i));
    }
    first->hashval = 0;
}

String* StringClone(String *str, Arena* a) {
	if (str == NULL || a == NULL) return NULL;
	auto outp = StringEmptyInArena(a);
	StringAppend(outp, str);
	return outp;
}

String* StringClone(String *str) {
    auto outp = StringEmpty();
    StringAppend(outp, str);
    return outp;
}

void StringAppend(String *first, const char *second) {
    if (first == NULL || second == NULL) return;
    while (*second != 0) {
        VPush_char(first->chars, *second);
        second++;
    }
    first->hashval = 0;
}

void StringAppendChar(String *str, char c) {
    VPush_char(str->chars, c);
    str->hashval = 0;
}

void StringAppendChar(String *str, char c, int count) {
    str->hashval = 0;
    for (int i = 0; i < count; i++) VPush_char(str->chars, c);
}

// internal var-arg appender. `fmt` is taken literally, except for these low ascii chars:
//'\x01'=(String*); '\x02'=int as dec; '\x03'=int as hex; '\x04'=char; '\x05'=C string (const char*); '\x06'=bool
void vStringAppendFormat(String *str, const char* fmt, va_list args) {
    if (str == NULL || fmt == NULL) return;
    
    // NOTE: When expanding this, the low-ascii points \x00, \x0A, \x0D are not to be used (null, lf, cr)
    str->hashval = 0;
    auto chars = str->chars;

    while (*fmt != '\0') {
        if (*fmt == '\x01') {
            String* s = va_arg(args, String*);
            StringAppend(str, s);
        } else if (*fmt == '\x02') {
            int i = va_arg(args, int);
            StringAppendInt32(str, i);
        } else if (*fmt == '\x03') {
            int i = va_arg(args, int);
            StringAppendInt32Hex(str, i);
        } else if (*fmt == '\x04') {
            char c = va_arg(args, char);
            VPush_char(chars, c);
        } else if (*fmt == '\x05') {
            char* s = va_arg(args, char*);
            StringAppend(str, s);
        } else if (*fmt == '\x06') {
            bool s = va_arg(args, bool);
			StringAppend(str, (s) ? "true" : "false");
		} else if (*fmt == '\x07') {
            char i = va_arg(args, char);
            StringAppendInt8Hex(str, i);
        } else {
            StringAppendChar(str, *fmt);
        }
        ++fmt;
    }
}

// Append, somewhat like sprintf. `fmt` is taken literally, except for these low ascii chars: 
//'\x01'=(String*); '\x02'=int as dec; '\x03'=int as hex; '\x04'=char; '\x05'=C string (const char*); '\x06'=bool
void StringAppendFormat(String *str, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vStringAppendFormat(str, fmt, args);
    va_end(args);
}

// Append, somewhat like sprintf. `fmt` is taken literally, except for these low ascii chars: 
//'\x01'=(String*); '\x02'=int as dec; '\x03'=int as hex; '\x04'=char; '\x05'=C string (const char*); '\x06'=bool
String * StringNewFormat(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    auto str = StringEmpty();
    vStringAppendFormat(str, fmt, args);
    va_end(args);
    return str;
}


void StringNL(String *str) {
    VPush_char(str->chars, '\n');
    str->hashval = 0;
}

char StringDequeue(String* str) {
    if (str == NULL) return '\0';
    str->hashval = 0;
    char c;
    if (!VDequeue_char(str->chars, &c)) return '\0';
    return c;
}

// remove and return the last char of the string. If string is empty, returns '\0'
char StringPop(String* str) {
    if (str == NULL) return '\0';
    str->hashval = 0;
    char c;
    if (!VPop_char(str->chars, &c)) return '\0';
    return c;
}

unsigned int StringLength(String * str) {
    if (str == NULL) return 0;
    return VLength(str->chars);
}

char StringCharAtIndex(String *str, int idx) {
    char val = 0;
    if (idx < 0) { // from end
        idx += VLength(str->chars);
    }
    auto ok = VCopy_char(str->chars, idx, &val);
    if (!ok) return 0;
    return val;
}

// Create a new string from a range in an existing string. The existing string is not modified
String *StringSlice(String* str, int startIdx, int length) {
    if (str == NULL) return NULL;
    auto len = StringLength(str);
    if (len < 1) return NULL;

    auto arena = FindArena(str->chars);
	if (arena == NULL) return NULL;

    String *result = StringEmptyInArena(arena);
    while (startIdx < 0) { startIdx += len; }
    if (length < 0) { length += len; length -= startIdx - 1; }

    for (int i = 0; i < length; i++) {
        uint32_t x = (i + startIdx) % len;
        if (!VPush_char(result->chars, *VGet_char(str->chars, x))) {
            // out of memory?
            StringDeallocate(result);
            return NULL;
        }
    }

    return result;
}

// Create a new string from a range in an existing string, and DEALLOCATE the original string
String *StringChop(String* str, int startIdx, int length) {
    auto result = StringSlice(str, startIdx, length);
    StringDeallocate(str);
    return result;
}

char *StringToCStr(String *str, Arena* a) {
    auto len = StringLength(str);
    auto result = (char*)ArenaAllocate(a, 1 + (sizeof(char) * len)); // need extra byte for '\0'
    for (unsigned int i = 0; i < len; i++) {
        result[i] = *VGet_char(str->chars, i);
    }
    result[len] = 0;
    return result;
}

char *SubstringToCStr(String *str, int start, Arena* a) {
    auto len = StringLength(str);
    if (start < 0) { // from end
        start += len;
    }
    if (len > start) { len -= start; }
    if (a == NULL) a = MMCurrent();

    auto result = (char*)ArenaAllocate(a, 1 + (sizeof(char) * len)); // need extra byte for '\0'
    for (unsigned int i = 0; i < len; i++) {
        char *cp = VGet_char(str->chars, i + start);
        if (cp == NULL) {
            break;
        }
        result[i] = *cp;
    }
    result[len] = 0;
    return result;
}

Vector* StringGetByteVector(String* str) {
    if (str == NULL) return NULL;
    return str->chars;
}

uint32_t StringHash(String* str) {
    if (str == NULL) return 0;
    if (str->hashval != 0) return str->hashval;

    uint32_t len = StringLength(str);
    uint32_t hash = len;
    for (uint32_t i = 0; i < len; i++) {
        hash += *VGet_char(str->chars, i);;
        hash ^= hash >> 16;
        hash *= 0x7feb352d;
        hash ^= hash >> 15;
        hash *= 0x846ca68b;
        hash ^= hash >> 16;
    }
    hash ^= len;
    hash ^= hash >> 16;
    hash *= 0x7feb352d;
    hash ^= hash >> 15;
    hash *= 0x846ca68b;
    hash ^= hash >> 16;
    hash += len;

    if (hash == 0) return 0x800800; // never return zero
    str->hashval = hash;
    return hash;
}

void StringToLower(String *str) {
    if (str == NULL) return;
    str->hashval = 0;
    // Simple 7-bit ASCII only at present
    uint32_t len = VLength(str->chars);
    for (uint32_t i = 0; i < len; i++) {
        auto chr = VGet_char(str->chars, i);
        if (*chr >= 'A' && *chr <= 'Z') {
            *chr = *chr + 0x20;
        }
    }
}

void StringToUpper(String *str) {
    if (str == NULL) return;
    str->hashval = 0;
    // Simple 7-bit ASCII only at present
    uint32_t len = VLength(str->chars);
    for (uint32_t i = 0; i < len; i++) {
        auto chr = VGet_char(str->chars, i);
        if (*chr >= 'a' && *chr <= 'z') {
            *chr = *chr - 0x20;
        }
    }
}

bool StringStartsWith(String* haystack, String *needle) {
    if (haystack == NULL) return false;
    if (needle == NULL) return true;
    auto len = StringLength(needle);
    if (len > StringLength(haystack)) return false;
    for (uint32_t i = 0; i < len; i++) {
        auto a = VGet_char(haystack->chars, i);
        auto b = VGet_char(needle->chars, i);
        if (*a != *b) return false;
    }
    return true;
}
bool StringStartsWith(String* haystack, const char* needle) {
    if (haystack == NULL) return false;
    if (needle == NULL) return true;
    auto limit = StringLength(haystack);
    uint32_t i = 0;
    while (needle[i] != 0) {
        if (i >= limit) return false;
        auto chr = VGet_char(haystack->chars, i);
        if (*chr != needle[i]) return false;
        i++;
    }
    return true;
}


bool StringEndsWith(String* haystack, String *needle) {
    if (haystack == NULL) return false;
    if (needle == NULL) return true;
    auto len = StringLength(needle);
    auto off = StringLength(haystack);
    if (len > off) return false;
    off -= len;
    for (uint32_t i = 0; i < len; i++) {
        auto a = VGet_char(haystack->chars, i + off);
        auto b = VGet_char(needle->chars, i);
        if (*a != *b) return false;
    }
    return true;
}
bool StringEndsWith(String* haystack, const char* needle) {
    auto str2 = StringNew(needle);
    bool match = StringEndsWith(haystack, str2);
    StringDeallocate(str2);
    return match;
}

bool StringAreEqual(String* a, String* b) {
    if (a == NULL || a->chars == NULL || !VectorIsValid(a->chars)) return false;
    if (b == NULL || b->chars == NULL || !VectorIsValid(b->chars)) return false;
    uint32_t len = StringLength(a);
    if (len != StringLength(b)) return false;
    for (uint32_t i = 0; i < len; i++) {
        auto ca = VGet_char(a->chars, i);
        auto cb = VGet_char(b->chars, i);
        if (*ca != *cb) return false;
    }
    return true;
}
bool StringAreEqual(String* a, const char* b) {
    if (a == NULL) return false;
    if (b == NULL) return false;
    uint32_t limit = StringLength(a);
    uint32_t i = 0;
    while (b[i] != 0) {
        if (i >= limit) return false;
        auto chr = VGet_char(a->chars, i);
        if (*chr != b[i]) return false;
        i++;
    }
    return i == limit;
}


bool StringFind(String* haystack, String* needle, unsigned int start, unsigned int* outPosition) {
    // get a few special cases out of the way
    if (haystack == NULL) return false;
    if (outPosition != NULL) *outPosition = 0;
    if (needle == NULL) return true; // treating null as empty

    uint32_t hayLen = VLength(haystack->chars);
    uint32_t needleLen = VLength(needle->chars);
    if (needleLen > hayLen) return false;
    if (start < 0) { // from end
        start += hayLen;
    }
    
    auto arena = FindArena(haystack->chars);
	if (arena == NULL) return false;

    // Rabin�Karp method, but using sum rather than hash (more false positives, but much cheaper on average)
    // get a hash of the 'needle', and try to find somewhere in the haystack that matches.
    // double-check if we find one.
    char *matchStr = StringToCStr(needle, arena);
    char *scanStr = SubstringToCStr(haystack, start, arena);

    // make sum of the needle, and an initial sum of the scan hash
    int match = 0;
    int scan = 0;
    uint32_t i = 0;
    for (i = 0; i < needleLen; i++) {
        match += matchStr[i];
        scan += scanStr[i];
    }

    // now roll the hash until until we find a match or run out of string
    for (i = needleLen; i < hayLen; i++) {
        if (match == scan) { // possible match, double check
            uint32_t idx = i - needleLen;
            if (outPosition != NULL) *outPosition = idx + start;
            bool found = true;
            for (uint32_t j = 0; j < needleLen; j++) {
                if (matchStr[j] != scanStr[j + idx]) {
                    found = false;
                    break;
                }
            }
            if (!found) {
                scan -= scanStr[i - needleLen];
                scan += scanStr[i];
                continue;
            }

            // OK, this is a match
            ArenaDereference(arena, matchStr);
            ArenaDereference(arena, scanStr);
            return true;
        }

        scan -= scanStr[i - needleLen];
        scan += scanStr[i];
    }

    // Clean up.
    ArenaDereference(arena, matchStr);
    ArenaDereference(arena, scanStr);
    return false;
}


// Find the position of a substring. If the outPosition is NULL, it is ignored
bool StringFind(String* haystack, const char * needle, unsigned int start, unsigned int* outPosition) {
    auto needl = StringNew(needle);
    bool result = StringFind(haystack, needl, start, outPosition);
    StringDeallocate(needl);
    return result;
}

// Find the next position of a character. If the outPosition is NULL, it is ignored
bool StringFind(String* haystack, char needle, unsigned int start, unsigned int* outPosition) {
    if (haystack == NULL) return false;
    if (outPosition != NULL) *outPosition = 0;
    if (needle == NULL) return true; // treating null as empty

    uint32_t hayLen = VLength(haystack->chars);
    if (start < 0) { // from end
        start += hayLen;
    }
    if (hayLen < start) return false;

    for (int i = start; i < hayLen; i++) {
        char c = StringCharAtIndex(haystack, i);
        if (c == needle) {
            if (outPosition != NULL) *outPosition = i;
            return true;
        }
    }

    return false;
}


// Append part of a source string into the end of the destination
void StringAppendSubstr(String* dest, String* src, int srcStart, int srcLength) {
    if (dest == NULL || src == NULL) return;
    auto slice = StringSlice(src, srcStart, srcLength);
    StringAppend(dest, slice);
    StringDeallocate(slice);
}

// Find any number of instances of a substring. Each one is replaced with a new substring in the output string.
String* StringReplace(String* haystack, String* needle, String* replacement) {
    if (haystack == NULL || needle == NULL) return NULL;

    // use `StringFind` to get to the next occurance.
    // for each occurance, copy across the chars up to that point, then copy across replacement, then skip the occurance
    
	auto arena = FindArena(haystack->chars);
	if (arena == NULL) return NULL;

    String *result = StringEmptyInArena(arena);
    if (result == NULL) return NULL;

    int length = StringLength(haystack);
    int nlen = StringLength(needle);
    uint32_t tail = 0;
    uint32_t next = 0;
    bool found = StringFind(haystack, needle, tail, &next);
    
    while (found) {
        // replacements
        StringAppendSubstr(result, haystack, tail, next - tail);
        StringAppend(result, replacement);

        // next one
        tail = next+nlen;
        found = StringFind(haystack, needle, tail, &next);
        if (next == 0) {
            found = false;
        }
    }

    // final tail
    if (tail < length) {
        StringAppendSubstr(result, haystack, tail, -1);
    }

    return result;
}

bool StringTryParse_int32(String *str, int32_t *dest) {
    if (str == NULL || dest == NULL) return false;

    int len = StringLength(str);
    if (len < 1) return false;
    int32_t result = 0;
    bool invert = false;

    int i = 0;
    if (StringCharAtIndex(str, 0) == '-') {
        if (len == 1) return false; // just a `-` symbol
        invert = true; i++;
    }
    if (StringCharAtIndex(str, 0) == '+') {
        if (len == 1) return false; // just a `+` symbol
        i++;
    }

    for (; i < len; i++) {
        char c = StringCharAtIndex(str, i);
        if (c == '_') continue; // allow (and ignore) underscores. Like 1_000_000

        int d = c - '0';
        if (d > 9 || d < 0) return false;

        result *= 10;
        result += d;
    }

    if (invert) *dest = -result;
    else *dest = result;
    return true;
}


bool StringTryParse_double(String *str, double *dest) {
    if (str == NULL) return false;

    // Plan: parse each side of the '.' as int32, truncate and weld
    uint32_t point = 0;

    int32_t intpart = 0;
    int32_t fracpart = 0;

    String *pt = StringNew(".");
    bool found = StringFind(str, pt, 0, &point);
    StringDeallocate(pt);

    // Integer only
    if (!found) {
        int32_t res;
        bool ok = StringTryParse_int32(str, &res);
        if (ok && dest != NULL) *dest = res;
        return ok;
    }

    // Integer and fraction
    if (point > 0) { // has integer part
        auto intp = StringSlice(str, 0, point);
        bool ok = StringTryParse_int32(intp, &intpart);
        StringDeallocate(intp);

        if (!ok) return false;
    }

    auto fracp = StringSlice(str, point + 1, -1);
    int flen = StringLength(fracp);
    bool ok = StringTryParse_int32(fracp, &fracpart);
    StringDeallocate(fracp);
    if (fracpart < 0) {
        // `1.-2` is not valid!
        return false;
    }
    if (!ok) fracpart = 0;

    // Combine int and frac
    double scale = 1;
    for (int s = 0; s < flen; s++) { scale *= 10; }
    if (dest != NULL) *dest = intpart + ((double)fracpart / scale);
        //fix16_sadd(intpart << 16, fix16_div(fracpart << 16, scale << 16));

    return true;
}