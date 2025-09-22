#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
        int a = atoi(argv[1]);
        int b = atoi(argv[3]);
        char op = argv[2][0]; // 문자열의 문자만 가져옴
        int result = 0;

        if (op == '+') {
                result = a + b;
        } else if(op == '*' || op == 'x') {
                result = a * b;
        } else {
                printf("지원하지 않는 연산자입니다 %c\n",op);
                return 1;
        }

        printf("%d\n",result);
        return 0;
}
