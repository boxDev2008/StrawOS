// Turns libhubbub's element-type.gperf into a sorted table with a binary-search
// lookup, standing in for gperf. usage: make-element-types <element-type.gperf> <out.c>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ENTRIES 512

typedef struct
{
    char name[64];
    char kind[64];
}
Entry;

static Entry entries[MAX_ENTRIES];
static int   entry_count;

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static int entry_compare(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    int c = strcmp(x->name, y->name);
    return c ? c : strcmp(x->kind, y->kind);
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: make-element-types <element-type.gperf> <out.c>\n");
        return 1;
    }

    FILE *in = fopen(argv[1], "r");
    if (!in)
    {
        perror(argv[1]);
        return 1;
    }

    // keywords sit between the first and second "%%" lines
    char line[512];
    int section = 0;
    while (fgets(line, sizeof(line), in))
    {
        if (strncmp(line, "%%", 2) == 0)
        {
            if (++section == 2)
                break;
            continue;
        }
        char *comma = strchr(line, ',');
        if (section != 1 || !comma)
            continue;
        *comma = '\0';
        char *kind = comma + 1;
        char *next = strchr(kind, ',');
        if (next)
            *next = '\0';
        if (entry_count == MAX_ENTRIES)
        {
            fprintf(stderr, "make-element-types: too many entries\n");
            return 1;
        }
        snprintf(entries[entry_count].name, sizeof(entries[0].name), "%s", trim(line));
        snprintf(entries[entry_count].kind, sizeof(entries[0].kind), "%s", trim(kind));
        entry_count++;
    }
    fclose(in);

    qsort(entries, entry_count, sizeof(Entry), entry_compare);

    FILE *out = fopen(argv[2], "w");
    if (!out)
    {
        perror(argv[2]);
        return 1;
    }

    fputs("#include <string.h>\n#include <strings.h>\n#include \"treebuilder/element-type.h\"\n\n", out);
    fputs("static const struct element_type_map wordlist[] = {\n", out);
    for (int i = 0; i < entry_count; i++)
        fprintf(out, "\t{ \"%s\", %s },\n", entries[i].name, entries[i].kind);
    fputs("};\n\n", out);
    fputs("static const struct element_type_map *hubbub_element_type_generated_lookup(const char *str, size_t len)\n{\n"
          "\tsize_t lo = 0, hi = sizeof(wordlist) / sizeof(wordlist[0]);\n"
          "\twhile (lo < hi) {\n\t\tsize_t mid = (lo + hi) / 2;\n"
          "\t\tint c = strncasecmp(str, wordlist[mid].name, len);\n"
          "\t\tif (c == 0 && wordlist[mid].name[len] != '\\0')\n\t\t\tc = -1;\n"
          "\t\tif (c == 0)\n\t\t\treturn &wordlist[mid];\n"
          "\t\tif (c < 0)\n\t\t\thi = mid;\n\t\telse\n\t\t\tlo = mid + 1;\n\t}\n\treturn NULL;\n}\n", out);
    fclose(out);
    return 0;
}
