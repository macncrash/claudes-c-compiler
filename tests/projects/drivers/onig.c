/* Oniguruma driver: compiles patterns in several syntaxes and encodings
 * (backreferences, named groups, lookaround, lazy and possessive
 * quantifiers, Unicode properties, case folding, Shift_JIS, ISO-8859-1) and
 * prints every match position and capture group over a set of subjects. */
#include <stdio.h>
#include <string.h>
#include "oniguruma.h"

static int bad;

static void run(OnigEncoding enc, OnigSyntaxType *syn, OnigOptionType opt, const char *pat, const char *const *subjects) {
    regex_t *re;
    OnigErrorInfo einfo;
    const UChar *p = (const UChar *)pat;
    int r = onig_new(&re, p, p + strlen(pat), opt, enc, syn, &einfo);
    if (r != ONIG_NORMAL) {
        UChar msg[ONIG_MAX_ERROR_MESSAGE_LEN];
        onig_error_code_to_str(msg, r, &einfo);
        printf("/%s/ error %d: %s\n", pat, r, msg);
        return;
    }
    OnigRegion *region = onig_region_new();
    for (int s = 0; subjects[s]; s++) {
        const UChar *str = (const UChar *)subjects[s];
        const UChar *end = str + strlen(subjects[s]);
        const UChar *start = str;
        printf("/%s/ %d:", pat, s);
        int n = 0;
        while (start <= end && n < 8) {
            r = onig_search(re, str, end, start, end, region, ONIG_OPTION_NONE);
            if (r < 0) {
                if (r != ONIG_MISMATCH) { printf(" err%d", r); bad++; }
                break;
            }
            printf(" @%d", r);
            for (int g = 0; g < region->num_regs; g++) printf(" %d-%d", region->beg[g], region->end[g]);
            n++;
            /* Next search after this match (one further on an empty match). */
            int next = region->end[0] > region->beg[0] ? region->end[0] : region->end[0] + 1;
            start = str + next;
        }
        printf("\n");
        onig_region_clear(region);
    }
    onig_region_free(region, 1);
    onig_free(re);
}

int main(void) {
    OnigEncoding encs[] = {ONIG_ENCODING_UTF8, ONIG_ENCODING_SJIS, ONIG_ENCODING_ISO_8859_1, ONIG_ENCODING_ASCII};
    onig_initialize(encs, 4);
    static const char *const text[] = {
        "The quick brown fox jumps over the lazy dog 12 times, 345 ways.",
        "aaa bbb aaa abab abcabc xyzzy 2026-11-14 10:20:30",
        "foo@example.com, bar.baz@test.org; not-an-email@, x@y.z",
        "", "aaaaaaaaaaaaaaaaaaaaaaaaaaaa!",
        "Grüße aus München — ÄÖÜ äöü ß ﬃ Ω ω Σσς 日本語テキスト",
        NULL,
    };
    static const char *const pats[] = {
        "\\b\\w+\\b", "o", "(\\w)(\\w)\\2\\1", "(a+)\\s+b+\\s+\\1", "\\d+", "(\\d{4})-(\\d\\d)-(\\d\\d)",
        "(?<user>[\\w.]+)@(?<host>\\w+(?:\\.\\w+)+)", "(?<=\\s)a\\w*", "\\w+(?=,)", "(?<!a)b+", "a*?b", "(?:ab)++",
        "x(y|z)+?z", "(?i)THE", "^.*$", "\\A\\w+", "\\w+\\z", ".{3}(?=!)", "(a|aa)*!", "(a*)*b",
        "\\p{Lu}\\p{Ll}+", "\\p{Greek}+", "[[:alpha:]]+", "[^\\x00-\\x7F]+", "(?i)grüsse|münchen", "ß|SS",
        "\\X", "(?i)σ", "[a-c&&b-d]+", "(abc){2}", "\\h+", "(?m)a.b", "\\R", "\\G\\w", "(?<n>a)\\k<n>",
        "(?<x>\\w)(?:(?!\\k<x>)\\w)*\\k<x>", "\\Qa.b\\E", "[\\w&&[^\\d]]+", "(?~abc)", "a{2,}+",
        NULL,
    };
    for (int k = 0; pats[k]; k++) run(ONIG_ENCODING_UTF8, ONIG_SYNTAX_ONIGURUMA, ONIG_OPTION_NONE, pats[k], text);
    static const char *const perl[] = {"(\\w+)\\s+\\1", "(?i)quick", "\\bt\\w*", "(?<y>\\d\\d)", NULL};
    for (int k = 0; perl[k]; k++) run(ONIG_ENCODING_UTF8, ONIG_SYNTAX_PERL, ONIG_OPTION_NONE, perl[k], text);
    static const char *const posix[] = {"[[:digit:]]+", "a\\{2\\}", "\\(ab\\)*c", NULL};
    for (int k = 0; posix[k]; k++) run(ONIG_ENCODING_ASCII, ONIG_SYNTAX_POSIX_BASIC, ONIG_OPTION_NONE, posix[k], text);
    run(ONIG_ENCODING_UTF8, ONIG_SYNTAX_ONIGURUMA, ONIG_OPTION_IGNORECASE, "straße|ǅ", text);
    run(ONIG_ENCODING_UTF8, ONIG_SYNTAX_ONIGURUMA, ONIG_OPTION_FIND_LONGEST, "a+|a+b|\\w+", text);
    /* Shift_JIS: "日本語" and "テスト" in Shift_JIS bytes. */
    static const char *const sjis[] = {"\x93\xfa\x96\x7b\x8c\xea abc \x83\x65\x83\x58\x83\x67", NULL};
    run(ONIG_ENCODING_SJIS, ONIG_SYNTAX_ONIGURUMA, ONIG_OPTION_NONE, "\x83\x65.", sjis);
    run(ONIG_ENCODING_SJIS, ONIG_SYNTAX_ONIGURUMA, ONIG_OPTION_NONE, ".", sjis);
    /* ISO-8859-1: "Grüße" with ü = 0xfc, ß = 0xdf. */
    static const char *const latin1[] = {"Gr\xfc\xdf" "e GR\xdc\xdf" "E", NULL};
    run(ONIG_ENCODING_ISO_8859_1, ONIG_SYNTAX_ONIGURUMA, ONIG_OPTION_IGNORECASE, "gr\xfc\xdf" "e", latin1);
    run(ONIG_ENCODING_ISO_8859_1, ONIG_SYNTAX_ONIGURUMA, ONIG_OPTION_NONE, "[[:upper:]]+", latin1);
    printf("oniguruma %s, %d errors\n", onig_version(), bad);
    onig_end();
    return bad != 0;
}
