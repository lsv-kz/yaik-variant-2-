#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//======================================================================
int main()
{
    char *method = getenv("REQUEST_METHOD");
    fprintf(stderr, "<%s:%s:%d> *** hello from C script ***\n", __FILE__, __func__, __LINE__);

    printf("Content-Type: text/plain"
            "\r\n\r\n"
            "******************* Hello from C script *******************\n"
            "11111111111111111111111111111111111111111111111111111111111\n"
            "22222222222222222222222222222222222222222222222222222222222\n"
            "33333333333333333333333333333333333333333333333333333333333\n"
            "44444444444444444444444444444444444444444444444444444444444\n"
            "55555555555555555555555555555555555555555555555555555555555\n"
            "66666666666666666666666666666666666666666666666666666666666\n"
        );

    if (method && (!strcmp(method, "POST")))
    {
        char s[256];
        int n = fread(s, 1, sizeof(s), stdin);
        if (n > 0)
        {
            fwrite(s, 1, n, stdout);
            printf("\npost data %d bytes\n", n);
        }
    }

    return 0;
}
