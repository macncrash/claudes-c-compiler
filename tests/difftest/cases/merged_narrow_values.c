/* Narrow values loaded (already extended in their register), merged with
 * constants into a wider value and narrowed back: a signed char or a
 * space, with negative chars in the text; an unsigned char or 0xff; a
 * short or a constant that does not fit a short (which must keep its
 * narrowing); and a char merged with another char's widening. */
#include <stdio.h>
#include <stdint.h>

static const char text[] = "Hello, W\xe9rld! \x80\xff abc XYZ";
static const unsigned char bytes[] = { 1, 200, 255, 0, 77, 128 };
static const short shorts[] = { -3, 32767, -32768, 1000 };

static int __attribute__((noinline)) classify(int n) {
    int lower = 0, upper = 0, other = 0, neg = 0;
    for (int i = 0; i <= n; i++) {
        char c = i < n ? text[i] : ' ';
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c >= 'a' && c <= 'z')
            lower += c;
        else if (c < 0)
            neg += c;
        else
            other += c;
        upper ^= c * (i + 1);
    }
    return lower * 3 + upper + other * 5 + neg * 7;
}

static unsigned __attribute__((noinline)) bytes_or(int n, int k) {
    unsigned s = 0;
    for (int i = 0; i < k; i++) {
        unsigned char b = i < n ? bytes[i] : 0xff;
        s = s * 3 + b + (b > 127);
    }
    return s;
}

static int __attribute__((noinline)) shorts_or(int n, int k, int fill) {
    int s = 0;
    for (int i = 0; i < k; i++) {
        long w = i < n ? shorts[i] : fill;
        short h = (short)w;
        s += h * (i + 1);
    }
    return s;
}

static int __attribute__((noinline)) two_chars(int a, int b) {
    int s = 0;
    for (int i = 0; i < 8; i++) {
        long w = i & 1 ? text[(a + i) % 20] : (signed char)bytes[(b + i) % 6];
        signed char c = (signed char)w;
        s = s * 5 + c;
    }
    return s;
}

int main(void) {
    int n = (int)sizeof text - 1;
    printf("classify %d %d\n", classify(n), classify(5));
    printf("bytes %u %u\n", bytes_or(6, 9), bytes_or(2, 4));
    printf("shorts %d %d %d\n", shorts_or(4, 6, 40000), shorts_or(4, 6, -7), shorts_or(2, 5, 65535));
    printf("two_chars %d %d\n", two_chars(3, 1), two_chars(11, 4));
    return 0;
}
