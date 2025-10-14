#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <math.h>
#define _USE_MATH_DEFINES
#define N 4

void sinx_taylor(int num_elements, int terms, double* x, double* result);

// 작업 처리 로그용 구조체
typedef struct {
	int idx; // 순서
	int pid; // 프로세스 PID
	double result; // 계산 값
} result_t;

int main() {

	// 부모-자식 간 통신을 위한 파이프 생성
	// fd[0] 읽기용 fd[1] 쓰기용 (하나씩만 사용 가능)
	int fd[2];
	pipe(fd);

        double x[N] = {0,M_PI/6.,M_PI/3.,0.134};
        double res[N];
	int pids[N];
    	int terms = 6;

	for(int i = 0;i < N;i++) {

		// 자식 프로세스 복제
		int pid = fork();

		// 자식 프로세스 작업 공간
		if(pid == 0) {
			// 읽기는 닫기
			close(fd[0]);

			// 파이프로 보낼 값
			result_t data;
			sinx_taylor(i,terms,x,&data.result);

			data.idx = i;
			data.pid = getpid();

                        printf("%d번 프로세스(PID=%d) 가 처리함\n",(i+1),data.pid);
			write(fd[1],&data,sizeof(result_t));
			close(fd[1]);
			exit(0);
		}
	}

	// 부모 프로세스 작업 공간 (쓰기 닫기)
	close(fd[1]);

        for(int i = 0;i < N;i++) {
                // 자식이 파이프로 보낸 값을 res,pids 배열에 저장
                result_t data;
                read(fd[0],&data,sizeof(result_t));
                res[data.idx] = data.result;
                pids[data.idx] = data.pid;
	}
        // 읽기 닫기
        close(fd[0]);

        for(int i = 0;i < N;i++) {
                int pid = wait(NULL);
                printf("%d 번 자식 프로세스 종료 PID=%d\n",(i+1),pid);
        }
        printf("모든 프로세스 종료 완료\n");

        // 결과 출력
        for(int i = 0;i<N;i++){
                printf("sin(%.2f) by  Talor series = %f\n",x[i],res[i]);
                printf("sin(%.2f) = %f\n",x[i],sin(x[i]));
                printf("%d번 프로세스(PID=%d)가 해당 작업을 처리\n",(i+1),pids[i]);
        }
        return 0;
}

void sinx_taylor(int i, int terms, double* x, double* result) 
{
        double value = x[i];
        double numer = x[i] * x[i] * x[i];
        double denom = 6.; // 3!
        int sign = -1;
	
        for(int j=1; j<=terms; j++) {
                value += (double)sign * numer / denom;
                numer *= x[i] * x[i];
                denom *= (2.*(double)j+2.) * (2.*(double)j+3.);
                sign *= -1;
        }

	// 계산된 결과를 result가 가리키는 메모리에 저장
        *result = value;
}
