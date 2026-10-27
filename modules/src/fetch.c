#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <http.h>

static bool show_progress(void *user, const char *stage, size_t received)
{
    const char *host = user;
    if (received)
        fprintf(stderr, "\r\033[90m%s %s: %zu bytes\033[0m", stage, host, received);
    else
        fprintf(stderr, "\r\033[90m%s %s...\033[0m", stage, host);
    return true;
}

int main(int argc, char **argv)
{
    bool headers_only = false, quiet = false;
    const char *url = NULL, *out_path = NULL;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-I") == 0) headers_only = true;
        else if (strcmp(argv[i], "-q") == 0) quiet = true;
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) out_path = argv[++i];
        else url = argv[i];
    }

    if (!url)
    {
        fprintf(stderr, "usage: fetch [-I] [-q] [-o file] <url>\n");
        return 2;
    }

    Url u;
    url_parse(url, &u);
    HttpResponse r;
    bool ok = http_get(url, &r, quiet ? NULL : show_progress, u.host);
    if (!quiet)
        fprintf(stderr, "\r\033[K");

    if (!ok)
    {
        fprintf(stderr, "\033[31mfetch: %s\033[0m\n", r.error);
        return 1;
    }

    if (headers_only || !quiet)
        fprintf(stderr, "\033[32m%d\033[0m %s  \033[90m%s, %zu bytes\033[0m\n", r.status, r.url, r.content_type, r.body_len);

    if (!headers_only)
    {
        if (out_path)
        {
            FILE *f = fopen(out_path, "wb");
            if (!f)
            {
                fprintf(stderr, "fetch: can't write %s\n", out_path);
                return 1;
            }
            fwrite(r.body, 1, r.body_len, f);
            fclose(f);
        }
        else
        {
            fwrite(r.body, 1, r.body_len, stdout);
            if (r.body_len && r.body[r.body_len - 1] != '\n')
                putchar('\n');
            fflush(stdout);
        }
    }

    http_response_free(&r);
    return r.status >= 400;
}
