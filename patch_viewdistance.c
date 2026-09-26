/* Raise the draw distance in Interstate '82 by editing i82.zfs in place.
 *
 * Standalone port of patch_viewdistance.py for players without Python: put
 * the exe in the game directory and double-click it, or drop i82.zfs (or the
 * game directory) onto it. Same edit, same defaults, same safety net -- the
 * output is byte-identical to the script's for the same factors.
 *
 * Each level carries a World_Data block as plain text inside i82.zfs, with
 * Fog_Max and Clipping_Plane equal by design: the fog hides the edge where
 * the world stops being drawn. Both are scaled, clipping 3x and fog 2x by
 * default. Values sit in fixed-width fields padded with spaces, so each one is
 * rewritten to the same byte length and no offset in the archive moves.
 *
 * Three Instant Action levels are the exception. On the golf course (Country
 * Club, m02.msa), which is dense with palms, the player's own car was not
 * drawn at about one level start in eight at 3x (7 of 60), apparently because
 * the game then has more objects in view than it can draw; at 2x that never
 * happened (0 of 80). Area 49 Surface (m12.msa) and Action Mall (m14.msa)
 * showed the same in play. Their
 * clipping is capped at 2x; every other level takes the factor given.
 *
 * Before the first change an untouched copy is kept as i82.zfs.original, and
 * every run computes from that copy, so factors never compound. Running it
 * again on a patched archive offers to restore the original.
 *
 *     patch_viewdistance.exe                          apply clipping 3x, fog 2x
 *     patch_viewdistance.exe --clip 4 --fog 2.5
 *     patch_viewdistance.exe --dry-run
 *     patch_viewdistance.exe --restore
 *     patch_viewdistance.exe "D:\Games\Interstate 82"
 *
 * Build (MSYS2 MinGW32 or MinGW64 shell):
 *     gcc -O2 -s -o patch_viewdistance.exe patch_viewdistance.c
 */
#define __USE_MINGW_ANSI_STDIO 1
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARCHIVE  "i82.zfs"
#define ORIGINAL "i82.zfs.original"
#define DEFAULT_GAMEDIR "C:\\Program Files (x86)\\GOG Galaxy\\Games\\Interstate 82"

static const struct { const char* key; int is_float; int is_clip; } KEYS[] = {
    { "Clipping_Plane",             0, 1 },
    { "Underground_Clipping_Plane", 0, 1 },
    { "Fog_Max",                    1, 0 },
    { "Underground_Fog_Max",        1, 0 },
};
#define NKEYS (int)(sizeof KEYS / sizeof KEYS[0])

/* Instant Action levels whose clipping is capped (see above) */
static const struct { const char* file; const char* title; } CAPPED[] = {
    { "m02.msa", "Country Club" },
    { "m12.msa", "Area 49 Surface" },
    { "m14.msa", "Action Mall" },
};
#define NCAPPED (int)(sizeof CAPPED / sizeof CAPPED[0])
#define CAP_CLIP_MAX 2.0

static unsigned rd32(const unsigned char* p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
}

/* Where a file lies inside the ZFS3 archive: 1 and [*start,*end) if found. */
static int zfs_find(const unsigned char* d, size_t len, const char* want,
                    size_t* start, size_t* end)
{
    if (len < 0x1C || memcmp(d, "ZFS3", 4)) return 0;
    size_t namelen = rd32(d + 8), per_block = rd32(d + 12), esize = namelen + 20;
    size_t blk = rd32(d + 0x18);
    for (int guard = 0; blk && guard < 10000; guard++) {
        if (namelen == 0 || namelen > 64 || blk + 4 + per_block * esize > len) return 0;
        for (size_t i = 0; i < per_block; i++) {
            const unsigned char* e = d + blk + 4 + i * esize;
            char name[65];
            memcpy(name, e, namelen);
            name[namelen] = 0;
            if (!_stricmp(name, want)) {
                *start = rd32(e + namelen);
                *end = *start + rd32(e + namelen + 8);
                return 1;
            }
        }
        blk = rd32(d + blk);
    }
    return 0;
}

static int g_interactive;   /* started from Explorer: keep the window open */

static void finish(void)
{
    if (g_interactive) {
        printf("\nPress Enter to close.");
        fflush(stdout);
        getchar();
    }
}

static int file_exists(const char* p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int dir_exists(const char* p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int has_archive(const char* dir)
{
    char p[MAX_PATH * 2];
    snprintf(p, sizeof p, "%s\\%s", dir, ARCHIVE);
    return file_exists(p);
}

static unsigned char* read_all(const char* path, size_t* len)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) *len = (size_t)n;
    return buf;
}

/* Write next to the target and swap it in, so an interrupted run never
 * leaves a half-written archive behind. */
static int write_all(const char* path, const unsigned char* buf, size_t len)
{
    char tmp[MAX_PATH * 3 + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (!f) return 0;
    int ok = fwrite(buf, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    if (ok) ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileA(tmp);
    return ok;
}

/* Same match as the script's (?m)^(\t?KEY:\t)([0-9]+(?:\.[0-9]+)?)([ \t]*)(?=[\r\n]):
 * anchored to a line start, so Underground_* never matches as the bare key. */
static int scale(const unsigned char* src, unsigned char* out, size_t len,
                 double clip, double fog, int* skipped_total)
{
    int changed_total = 0;
    *skipped_total = 0;
    size_t cap0[NCAPPED], cap1[NCAPPED];
    int have[NCAPPED];
    for (int c = 0; c < NCAPPED; c++) {
        have[c] = zfs_find(src, len, CAPPED[c].file, &cap0[c], &cap1[c]);
        if (have[c] && clip > CAP_CLIP_MAX)
            printf("    %s (%s): clipping capped at x%g\n", CAPPED[c].file, CAPPED[c].title, CAP_CLIP_MAX);
    }
    for (int k = 0; k < NKEYS; k++) {
        const char* key = KEYS[k].key;
        size_t kl = strlen(key);
        double factor = KEYS[k].is_clip ? clip : fog;
        int changed = 0, skipped = 0;
        struct { char o[32], n[32]; int c; } seen[16];
        int nseen = 0, other = 0;

        for (size_t i = 0; i < len; i++) {
            if (i != 0 && src[i - 1] != '\n') continue;
            size_t p = i;
            if (p < len && src[p] == '\t') p++;
            if (p + kl + 2 > len || memcmp(src + p, key, kl) != 0) continue;
            p += kl;
            if (src[p] != ':' || src[p + 1] != '\t') continue;
            p += 2;

            size_t v0 = p;
            while (p < len && src[p] >= '0' && src[p] <= '9') p++;
            if (p == v0) continue;
            if (p < len && src[p] == '.') {
                size_t d = p + 1;
                while (d < len && src[d] >= '0' && src[d] <= '9') d++;
                if (d > p + 1) p = d;      /* "12." without digits: stop before '.' */
            }
            size_t v1 = p;
            while (p < len && (src[p] == ' ' || src[p] == '\t')) p++;
            if (p >= len || (src[p] != '\r' && src[p] != '\n')) continue;

            size_t field = p - v0;
            char val[32], txt[64];
            if (v1 - v0 >= sizeof val) { skipped++; continue; }
            memcpy(val, src + v0, v1 - v0);
            val[v1 - v0] = 0;
            double f = factor;
            if (KEYS[k].is_clip && f > CAP_CLIP_MAX)
                for (int c = 0; c < NCAPPED; c++)
                    if (have[c] && i >= cap0[c] && i < cap1[c]) f = CAP_CLIP_MAX;
            double nv = strtod(val, NULL) * f;
            if (KEYS[k].is_float)
                snprintf(txt, sizeof txt, "%.2f", nv);
            else
                snprintf(txt, sizeof txt, "%lld", (long long)nearbyint(nv));  /* ties to even, as Python's round() */
            size_t tl = strlen(txt);
            if (tl > field) { skipped++; continue; }

            memcpy(out + v0, txt, tl);
            memset(out + v0 + tl, ' ', field - tl);
            changed++;
            int s = 0;
            while (s < nseen && (strcmp(seen[s].o, val) || strcmp(seen[s].n, txt))) s++;
            if (s == nseen && nseen < 16) {
                snprintf(seen[s].o, sizeof seen[s].o, "%s", val);
                snprintf(seen[s].n, sizeof seen[s].n, "%s", txt);
                seen[s].c = 0;
                nseen++;
            }
            if (s < nseen) seen[s].c++; else other++;
        }
        for (int s = 0; s < nseen; s++)
            printf("    %-28s %8s -> %-8s (%d level%s)\n", key, seen[s].o, seen[s].n,
                   seen[s].c, seen[s].c == 1 ? "" : "s");
        if (other)
            printf("    %-28s %d more value(s)\n", key, other);
        if (skipped)
            printf("    %-28s %d value(s) too wide for their field, left alone\n", key, skipped);
        changed_total += changed;
        *skipped_total += skipped;
    }
    return changed_total;
}

static int ask_yes(const char* q)
{
    printf("%s [y/N] ", q);
    fflush(stdout);
    char line[16];
    if (!fgets(line, sizeof line, stdin)) return 0;
    return line[0] == 'y' || line[0] == 'Y' || line[0] == 'j' || line[0] == 'J';
}

static int restore(const char* archive, const char* original)
{
    if (!file_exists(original)) {
        printf("No %s to restore from -- the archive has never been changed by this tool.\n", ORIGINAL);
        return 2;
    }
    if (!CopyFileA(original, archive, FALSE)) {
        printf("Could not restore %s (error %lu). Is the game still running?\n",
               ARCHIVE, GetLastError());
        return 5;
    }
    printf("Restored %s from %s. The original draw distance is back.\n", ARCHIVE, ORIGINAL);
    return 0;
}

static int run(int argc, char** argv)
{
    double clip = 3.0, fog = 2.0;
    int dry = 0, want_restore = 0, explicit_factors = 0;
    char dir[MAX_PATH * 2] = "";

    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!strcmp(a, "--clip") && i + 1 < argc) { clip = atof(argv[++i]); explicit_factors = 1; }
        else if (!strcmp(a, "--fog") && i + 1 < argc) { fog = atof(argv[++i]); explicit_factors = 1; }
        else if (!strcmp(a, "--dry-run")) dry = 1;
        else if (!strcmp(a, "--restore")) want_restore = 1;
        else if (!strcmp(a, "--game") && i + 1 < argc) snprintf(dir, sizeof dir, "%s", argv[++i]);
        else if (!strcmp(a, "--help") || !strcmp(a, "-h") || !strcmp(a, "/?")) {
            printf("Raise Interstate '82's draw distance by editing i82.zfs in place.\n\n"
                   "  patch_viewdistance.exe [game dir or i82.zfs] [--clip N] [--fog N]\n"
                   "                         [--dry-run] [--restore]\n\n"
                   "Defaults: clipping 3x, fog 2x. Without a path it looks next to the exe,\n"
                   "then in the current directory, then in the default GOG location.\n");
            return 0;
        }
        else if (a[0] == '-') { printf("Unknown option: %s (try --help)\n", a); return 2; }
        else {
            /* a dropped file or directory */
            if (dir_exists(a)) snprintf(dir, sizeof dir, "%s", a);
            else {
                snprintf(dir, sizeof dir, "%s", a);
                char* s = strrchr(dir, '\\');
                if (!s) s = strrchr(dir, '/');
                if (s) *s = 0; else strcpy(dir, ".");
            }
        }
    }
    if (clip <= 0 || fog <= 0) { printf("Factors must be positive.\n"); return 2; }

    if (!dir[0]) {
        char exe[MAX_PATH * 2];
        DWORD n = GetModuleFileNameA(NULL, exe, sizeof exe);
        char* s = n ? strrchr(exe, '\\') : NULL;
        if (s) { *s = 0; if (has_archive(exe)) snprintf(dir, sizeof dir, "%s", exe); }
        if (!dir[0] && has_archive(".")) strcpy(dir, ".");
        if (!dir[0] && has_archive(DEFAULT_GAMEDIR)) strcpy(dir, DEFAULT_GAMEDIR);
    }
    if (!dir[0] || !has_archive(dir)) {
        printf("Could not find %s.\n"
               "Put this program in the Interstate '82 folder and run it there,\n"
               "or drop i82.zfs onto it.\n", ARCHIVE);
        return 2;
    }

    char archive[MAX_PATH * 3], original[MAX_PATH * 3];
    snprintf(archive, sizeof archive, "%s\\%s", dir, ARCHIVE);
    snprintf(original, sizeof original, "%s\\%s", dir, ORIGINAL);
    printf("Game folder: %s\n", dir);

    if (want_restore) return restore(archive, original);

    int have_original = file_exists(original);
    size_t len = 0, cur_len = 0;
    unsigned char* src = read_all(have_original ? original : archive, &len);
    if (!src) { printf("Could not read %s.\n", have_original ? ORIGINAL : ARCHIVE); return 2; }
    unsigned char* out = malloc(len);
    if (!out) { printf("Out of memory.\n"); return 2; }
    memcpy(out, src, len);

    printf("Source: %s, %lu bytes -- clipping x%g, fog x%g\n",
           have_original ? ORIGINAL : ARCHIVE, (unsigned long)len, clip, fog);
    int skipped = 0;
    int changed = scale(src, out, len, clip, fog, &skipped);
    printf("  %d values rewritten, %d skipped, size unchanged\n", changed, skipped);
    if (!changed) {
        printf("  Nothing matched -- is this the Interstate '82 archive?\n");
        return 4;
    }

    /* Already in exactly this state? Then a second double-click means "undo". */
    unsigned char* cur = have_original ? read_all(archive, &cur_len) : NULL;
    int already = cur && cur_len == len && !memcmp(cur, out, len);
    free(cur);
    if (already) {
        printf("\nThe archive already has this draw distance.\n");
        if (dry || explicit_factors || !g_interactive) return 0;
        if (ask_yes("Restore the original draw distance instead?"))
            return restore(archive, original);
        printf("Left unchanged.\n");
        return 0;
    }

    if (dry) { printf("\nDry run -- nothing written.\n"); return 0; }

    if (!have_original) {
        if (!CopyFileA(archive, original, TRUE)) {
            printf("Could not create %s (error %lu). Nothing was changed.\n",
                   ORIGINAL, GetLastError());
            return 5;
        }
        printf("Kept an untouched copy as %s.\n", ORIGINAL);
    }
    if (!write_all(archive, out, len)) {
        printf("Could not write %s (error %lu). Is the game still running?\n"
               "Nothing was changed.\n", ARCHIVE, GetLastError());
        return 5;
    }
    printf("Written. Start a mission to see it -- the menu shows nothing of this.\n"
           "Run this program again, or with --restore, to undo.\n");
    return 0;
}

int main(int argc, char** argv)
{
    DWORD pids[2];
    /* the only process on this console: Explorer started us, the window
     * would vanish before anyone could read it */
    g_interactive = GetConsoleProcessList(pids, 2) == 1;
    int rc = run(argc, argv);
    finish();
    return rc;
}
