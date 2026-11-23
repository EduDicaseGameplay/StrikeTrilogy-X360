// printf-family formatting done on the clang side of the Xbox 360 build.
//
// Console test 14 showed a caller's saved register (r27) coming back as a stack address after snprintf (clang,
// variadic, 64-bit argument) handed its va_list to the XDK's _vsnprintf (MSVC). Rather than trust any va_list
// crossing from clang into the XDK's C library, every printf-family call made by clang code is formatted here:
// snprintf/vsnprintf (also used by OXDK's prebuilt libc++), and, through the renames in x360_crt_extras.h and the
// renamed libc++/runtime copies in link_runtime_xbox.py, _snprintf/_vsnprintf/_vscprintf/fprintf/vfprintf.
// Only plain, non-variadic XDK calls remain (fwrite, pow/log10 returning double in f1).
//
// fprintf/vfprintf to stdout or stderr go to the title log (game:\ps2x_log.txt), where the console can show them.
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void x360_log_write(const char *text, unsigned length); // host/x360_log.cpp

namespace
{
// Round to integer, ties to even (what printf does for an exactly representable tie such as 2.5 or 0.125*10).
uint64_t roundHalfEven(double x)
{
    const double fl = floor(x);
    uint64_t u = static_cast<uint64_t>(fl);
    const double frac = x - fl;
    if (frac > 0.5 || (frac == 0.5 && (u & 1u)))
        ++u;
    return u;
}

// Output sink: counts every character, stores while there is room.
struct Sink
{
    char *buf;
    size_t cap; // bytes available for characters (excluding the terminator slot)
    size_t len; // characters produced so far (may exceed cap)

    void put(char c)
    {
        if (len < cap)
            buf[len] = c;
        ++len;
    }
    void put(const char *s, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            put(s[i]);
    }
    void pad(char c, int n)
    {
        for (int i = 0; i < n; ++i)
            put(c);
    }
};

struct Spec
{
    bool left = false, plus = false, space = false, alt = false, zero = false;
    int width = 0;
    int prec = -1;
};

// Emits [sign][prefix][zeros][body] with width/flags.
void emitNumber(Sink &out, const Spec &sp, const char *sign, const char *prefix, const char *body, size_t bodyLen,
                int minDigits)
{
    const size_t signLen = strlen(sign), prefixLen = strlen(prefix);
    const int zeros = minDigits > static_cast<int>(bodyLen) ? minDigits - static_cast<int>(bodyLen) : 0;
    const int total = static_cast<int>(signLen + prefixLen + bodyLen) + zeros;
    const int padding = sp.width > total ? sp.width - total : 0;
    if (!sp.left && !(sp.zero && sp.prec < 0))
        out.pad(' ', padding);
    out.put(sign, signLen);
    out.put(prefix, prefixLen);
    if (!sp.left && sp.zero && sp.prec < 0)
        out.pad('0', padding);
    out.pad('0', zeros);
    out.put(body, bodyLen);
    if (sp.left)
        out.pad(' ', padding);
}

void formatInteger(Sink &out, const Spec &sp, uint64_t magnitude, bool negative, unsigned base, bool upper, char conv)
{
    char digits[32];
    size_t n = 0;
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (magnitude == 0)
    {
        if (sp.prec != 0)
            digits[n++] = '0';
    }
    while (magnitude)
    {
        digits[n++] = set[magnitude % base];
        magnitude /= base;
    }
    char body[32];
    for (size_t i = 0; i < n; ++i)
        body[i] = digits[n - 1 - i];
    const char *sign = negative ? "-" : (sp.plus ? "+" : (sp.space ? " " : ""));
    if (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o')
        sign = "";
    const char *prefix = "";
    int minDigits = sp.prec >= 0 ? sp.prec : 0;
    if (sp.alt && n && !(n == 1 && body[0] == '0'))
    {
        if (base == 16)
            prefix = upper ? "0X" : "0x";
        else if (base == 8 && minDigits <= static_cast<int>(n))
            minDigits = static_cast<int>(n) + 1;
    }
    emitNumber(out, sp, sign, prefix, body, n, minDigits);
}

// Significant decimal digits of a positive finite value: count digits (<= 17) and its decimal exponent
// (value ~= 0.d1d2... * 10^(exp10+1), i.e. d1 is the 10^exp10 digit). Rounded half up.
void decimalDigits(double v, int count, char *digits, int &exp10)
{
    if (v == 0.0)
    {
        for (int i = 0; i < count; ++i)
            digits[i] = '0';
        exp10 = 0;
        return;
    }
    int e = static_cast<int>(floor(log10(v)));
    double scaled = v / pow(10.0, e);
    if (scaled >= 10.0)
    {
        scaled /= 10.0;
        ++e;
    }
    else if (scaled < 1.0)
    {
        scaled *= 10.0;
        --e;
    }
    const int n = count > 17 ? 17 : count;
    uint64_t d = roundHalfEven(scaled * pow(10.0, n - 1));
    uint64_t limit = 1;
    for (int i = 0; i < n; ++i)
        limit *= 10;
    if (d >= limit)
    {
        d /= 10;
        ++e;
    }
    for (int i = n - 1; i >= 0; --i)
    {
        digits[i] = static_cast<char>('0' + d % 10);
        d /= 10;
    }
    for (int i = n; i < count; ++i)
        digits[i] = '0';
    exp10 = e;
}

// Fixed notation body (no sign) into buf; returns its length.
size_t fixedBody(double v, int prec, bool alt, char *buf, size_t cap)
{
    size_t n = 0;
    // Exact enough with 64-bit integers while v * 10^prec stays below 9e18.
    const double scaledLimit = 9.0e18;
    const double scale = pow(10.0, prec);
    if (v * scale < scaledLimit)
    {
        uint64_t u = roundHalfEven(v * scale);
        char digits[40];
        int k = 0;
        do
        {
            digits[k++] = static_cast<char>('0' + u % 10);
            u /= 10;
        } while (u);
        while (k < prec + 1)
            digits[k++] = '0';
        for (int i = k - 1; i >= 0 && n + 2 < cap; --i)
        {
            buf[n++] = digits[i];
            if (i == prec && (prec > 0 || alt))
                buf[n++] = '.';
        }
        return n;
    }
    // Large values: significant digits, then zeros.
    char digits[400];
    int e10 = 0;
    int count = 0;
    {
        int tmp;
        char probe[2];
        decimalDigits(v, 1, probe, tmp);
        count = tmp + 1 + prec;
        if (count > 390)
            count = 390;
        if (count < 1)
            count = 1;
    }
    decimalDigits(v, count, digits, e10);
    const int intDigits = e10 + 1;
    for (int i = 0; i < intDigits && n + 2 < cap; ++i)
        buf[n++] = i < count ? digits[i] : '0';
    if (prec > 0 || alt)
        buf[n++] = '.';
    for (int i = 0; i < prec && n + 1 < cap; ++i)
    {
        const int idx = intDigits + i;
        buf[n++] = idx < count ? digits[idx] : '0';
    }
    return n;
}

size_t expBody(double v, int prec, bool alt, bool upper, char *buf, size_t cap)
{
    char digits[400];
    int count = prec + 1;
    if (count > 390)
        count = 390;
    int e10 = 0;
    decimalDigits(v, count, digits, e10);
    size_t n = 0;
    buf[n++] = digits[0];
    if (prec > 0 || alt)
        buf[n++] = '.';
    for (int i = 1; i < count && n + 8 < cap; ++i)
        buf[n++] = digits[i];
    buf[n++] = upper ? 'E' : 'e';
    buf[n++] = e10 < 0 ? '-' : '+';
    int ae = e10 < 0 ? -e10 : e10;
    char ed[8];
    int k = 0;
    do
    {
        ed[k++] = static_cast<char>('0' + ae % 10);
        ae /= 10;
    } while (ae);
    if (k < 2)
        ed[k++] = '0';
    while (k)
        buf[n++] = ed[--k];
    return n;
}

void formatFloat(Sink &out, const Spec &spIn, double v, char conv)
{
    Spec sp = spIn;
    const bool upper = conv == 'F' || conv == 'E' || conv == 'G' || conv == 'A';
    bool negative = signbit(v) != 0;
    if (negative)
        v = -v;
    const char *sign = negative ? "-" : (sp.plus ? "+" : (sp.space ? " " : ""));
    if (isnan(v) || isinf(v))
    {
        const char *t = isnan(v) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        sp.zero = false;
        sp.prec = -1;
        emitNumber(out, sp, sign, "", t, 3, 0);
        return;
    }
    char body[512];
    size_t n = 0;
    int prec = sp.prec < 0 ? 6 : sp.prec;
    if (prec > 380)
        prec = 380;
    char c = conv;
    if (c == 'a' || c == 'A')
        c = upper ? 'E' : 'e'; // hexadecimal floats are not used by the runtime: decimal exponent form instead
    if (c == 'f' || c == 'F')
        n = fixedBody(v, prec, sp.alt, body, sizeof(body));
    else if (c == 'e' || c == 'E')
        n = expBody(v, prec, sp.alt, upper, body, sizeof(body));
    else // g / G
    {
        const int p = prec == 0 ? 1 : prec;
        char digits[24];
        int x = 0;
        decimalDigits(v, p > 17 ? 17 : p, digits, x);
        if (v == 0.0)
            x = 0;
        if (p > x && x >= -4)
            n = fixedBody(v, p - 1 - x, sp.alt, body, sizeof(body));
        else
            n = expBody(v, p - 1, sp.alt, upper, body, sizeof(body));
        if (!sp.alt)
        {
            // Strip trailing zeros of the fraction (and a bare point).
            size_t mantEnd = n;
            for (size_t i = 0; i < n; ++i)
                if (body[i] == 'e' || body[i] == 'E')
                {
                    mantEnd = i;
                    break;
                }
            bool hasPoint = false;
            for (size_t i = 0; i < mantEnd; ++i)
                if (body[i] == '.')
                    hasPoint = true;
            if (hasPoint)
            {
                size_t end = mantEnd;
                while (end > 0 && body[end - 1] == '0')
                    --end;
                if (end > 0 && body[end - 1] == '.')
                    --end;
                memmove(body + end, body + mantEnd, n - mantEnd);
                n -= mantEnd - end;
            }
        }
    }
    sp.prec = -1; // the body is complete: no integer-style minimum digits
    emitNumber(out, sp, sign, "", body, n, 0);
}

void formatString(Sink &out, const Spec &sp, const char *s)
{
    if (!s)
        s = "(null)";
    size_t n = 0;
    while (s[n] && (sp.prec < 0 || n < static_cast<size_t>(sp.prec)))
        ++n;
    const int padding = sp.width > static_cast<int>(n) ? sp.width - static_cast<int>(n) : 0;
    if (!sp.left)
        out.pad(' ', padding);
    out.put(s, n);
    if (sp.left)
        out.pad(' ', padding);
}

void formatAll(Sink &out, const char *f, __builtin_va_list ap)
{
    while (*f)
    {
        if (*f != '%')
        {
            out.put(*f++);
            continue;
        }
        ++f;
        if (*f == '%')
        {
            out.put(*f++);
            continue;
        }
        Spec sp;
        for (;; ++f)
        {
            if (*f == '-')
                sp.left = true;
            else if (*f == '+')
                sp.plus = true;
            else if (*f == ' ')
                sp.space = true;
            else if (*f == '#')
                sp.alt = true;
            else if (*f == '0')
                sp.zero = true;
            else
                break;
        }
        if (*f == '*')
        {
            sp.width = __builtin_va_arg(ap, int);
            if (sp.width < 0)
            {
                sp.left = true;
                sp.width = -sp.width;
            }
            ++f;
        }
        else
            while (*f >= '0' && *f <= '9')
                sp.width = sp.width * 10 + (*f++ - '0');
        if (*f == '.')
        {
            ++f;
            sp.prec = 0;
            if (*f == '*')
            {
                sp.prec = __builtin_va_arg(ap, int);
                if (sp.prec < 0)
                    sp.prec = -1;
                ++f;
            }
            else
                while (*f >= '0' && *f <= '9')
                    sp.prec = sp.prec * 10 + (*f++ - '0');
        }
        // Length: 0 int, 1 long (32-bit here), 2 long long, 3 size_t/ptrdiff_t (32), 4 intmax (64), 5 short, 6 char
        int length = 0;
        if (*f == 'h')
        {
            ++f;
            length = 5;
            if (*f == 'h')
            {
                ++f;
                length = 6;
            }
        }
        else if (*f == 'l')
        {
            ++f;
            length = 1;
            if (*f == 'l')
            {
                ++f;
                length = 2;
            }
        }
        else if (*f == 'q' || *f == 'L')
        {
            ++f;
            length = 2; // %Lf: long double is double here (-mlong-double-64); %Ld/%qd: 64-bit integer
        }
        else if (*f == 'z' || *f == 't')
        {
            ++f;
            length = 3;
        }
        else if (*f == 'j')
        {
            ++f;
            length = 4;
        }
        else if (f[0] == 'I' && f[1] == '6' && f[2] == '4')
        {
            f += 3;
            length = 2;
        }
        else if (f[0] == 'I' && f[1] == '3' && f[2] == '2')
            f += 3;
        else if (*f == 'I')
        {
            ++f;
            length = 3;
        }
        const char conv = *f;
        if (!conv)
            break;
        ++f;
        switch (conv)
        {
        case 'd':
        case 'i':
        {
            int64_t v;
            if (length == 2 || length == 4)
                v = __builtin_va_arg(ap, long long);
            else
            {
                int32_t w = __builtin_va_arg(ap, int);
                if (length == 5)
                    w = static_cast<short>(w);
                else if (length == 6)
                    w = static_cast<signed char>(w);
                v = w;
            }
            const bool neg = v < 0;
            formatInteger(out, sp, neg ? 0ull - static_cast<uint64_t>(v) : static_cast<uint64_t>(v), neg, 10, false,
                          conv);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        {
            uint64_t v;
            if (length == 2 || length == 4)
                v = __builtin_va_arg(ap, unsigned long long);
            else
            {
                uint32_t w = __builtin_va_arg(ap, unsigned int);
                if (length == 5)
                    w = static_cast<unsigned short>(w);
                else if (length == 6)
                    w = static_cast<unsigned char>(w);
                v = w;
            }
            const unsigned base = conv == 'u' ? 10 : (conv == 'o' ? 8 : 16);
            formatInteger(out, sp, v, false, base, conv == 'X', conv);
            break;
        }
        case 'p':
        {
            const uintptr_t v = reinterpret_cast<uintptr_t>(__builtin_va_arg(ap, void *));
            Spec ps = sp;
            ps.alt = true;
            if (ps.prec < 0)
                ps.prec = 8;
            formatInteger(out, ps, v, false, 16, false, 'x');
            break;
        }
        case 'c':
        {
            const char ch = static_cast<char>(__builtin_va_arg(ap, int));
            Spec cs = sp;
            cs.prec = 1;
            const char s[2] = {ch, 0};
            if (ch == 0)
            {
                const int padding = cs.width > 1 ? cs.width - 1 : 0;
                if (!cs.left)
                    out.pad(' ', padding);
                out.put('\0');
                if (cs.left)
                    out.pad(' ', padding);
            }
            else
                formatString(out, cs, s);
            break;
        }
        case 's':
        case 'S':
            formatString(out, sp, __builtin_va_arg(ap, const char *));
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            formatFloat(out, sp, __builtin_va_arg(ap, double), conv);
            break;
        case 'n':
        {
            int *p = __builtin_va_arg(ap, int *);
            if (p)
                *p = static_cast<int>(out.len);
            break;
        }
        default:
            out.put('%');
            out.put(conv);
            break;
        }
    }
}

int formatTo(char *buf, size_t n, const char *f, __builtin_va_list ap)
{
    Sink out{buf, n ? n - 1 : 0, 0};
    formatAll(out, f ? f : "", ap);
    if (n)
        buf[out.len < n - 1 ? out.len : n - 1] = 0;
    return out.len > 0x7FFFFFFF ? -1 : static_cast<int>(out.len);
}

int writeFormatted(FILE *file, const char *f, __builtin_va_list ap)
{
    char local[1024];
    __builtin_va_list copy;
    __builtin_va_copy(copy, ap);
    int len = formatTo(local, sizeof(local), f, ap);
    char *text = local;
    char *heap = nullptr;
    if (len >= static_cast<int>(sizeof(local)))
    {
        heap = static_cast<char *>(malloc(static_cast<size_t>(len) + 1));
        if (heap)
        {
            formatTo(heap, static_cast<size_t>(len) + 1, f, copy);
            text = heap;
        }
        else
            len = sizeof(local) - 1;
    }
    __builtin_va_end(copy);
    if (len > 0)
    {
        if (file == stdout || file == stderr)
            x360_log_write(text, static_cast<unsigned>(len));
        else if (file)
            fwrite(text, 1, static_cast<size_t>(len), file);
    }
    free(heap);
    return len;
}
} // namespace

extern "C" {

// C99 semantics: always terminated, returns the length the full output needs.
int x360_vsnprintf_c99(char *buf, size_t n, const char *f, __builtin_va_list ap) { return formatTo(buf, n, f, ap); }

// Microsoft semantics (_vsnprintf/_snprintf): -1 when the output does not fit, no terminator in that case.
int x360_vsnprintf_ms(char *buf, size_t n, const char *f, __builtin_va_list ap)
{
    char *tmp = static_cast<char *>(malloc(n + 1));
    if (!tmp)
        return -1;
    const int len = formatTo(tmp, n + 1, f, ap);
    if (len < 0 || static_cast<size_t>(len) > n)
    {
        memcpy(buf, tmp, n);
        free(tmp);
        return -1;
    }
    memcpy(buf, tmp, static_cast<size_t>(len) + (static_cast<size_t>(len) < n ? 1 : 0));
    free(tmp);
    return len;
}

int x360_snprintf_ms(char *buf, size_t n, const char *f, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, f);
    const int r = x360_vsnprintf_ms(buf, n, f, ap);
    __builtin_va_end(ap);
    return r;
}

int x360_vscprintf(const char *f, __builtin_va_list ap)
{
    char dummy;
    return formatTo(&dummy, 0, f, ap);
}

int x360_vfprintf(FILE *file, const char *f, __builtin_va_list ap) { return writeFormatted(file, f, ap); }

int x360_fprintf(FILE *file, const char *f, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, f);
    const int r = writeFormatted(file, f, ap);
    __builtin_va_end(ap);
    return r;
}

} // extern "C"
