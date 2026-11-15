/* Strong definitions that replace w1.c's weak ones. */
int wv = 9;
const int wc = 30;
static const char *const names2[] = { "uno", "dos", "tres", "quatro" };
int wsw(int k) {
    switch (k) {
    case 0: return 200; case 1: return 201; case 2: return 202; case 3: return 203;
    case 4: return 204; case 5: return 205; case 6: return 206; default: return -9;
    }
}
const char *wname(int k) { return names2[k % 4]; }
int other(int k) { return k * 3; }
