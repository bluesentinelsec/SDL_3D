#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 3 || strcmp(argv[1], "--output") != 0)
        return 2;

    FILE *output = fopen(argv[2], "wb");
    if (output == NULL)
        return 3;

    for (int i = 3; i < argc; ++i)
    {
        if (fprintf(output, "%s\n", argv[i]) < 0)
        {
            fclose(output);
            return 4;
        }
    }
    return fclose(output) == 0 ? 0 : 5;
}
