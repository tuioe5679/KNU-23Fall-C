/*
 * 라운드로빈 프로세스 스케줄 시뮬레이터 (부모-자식 구조)
 * 부모가 타이머 시그널로 자식 프로세스 스케줄
 * 자식은 SIGUSR1 (실행 시그널) 받을때마다 CPU burst 1감소
 * burst가 0이 되면 종료하거나 I/O 요청 (랜덤)
 * I/O 완료시 부모가 자식 깨움 + burst를 새롭게 부여
 * READY 큐/ SLEEP 관리, 로그 및 평균 대기시간 계산
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>

#define NPROC 10                   // 자식 프로세스 수
#define SIG_IO_REQUEST (SIGRTMIN+1)// 커스텀 시그널(자식->부모 I/O 요청)

typedef enum { READY, RUNNING, SLEEP, DONE } State;
typedef struct {
        pid_t pid;           // 자식의 PID
        int tq;              // 남은 타임퀀텀
        State state;         // 프로세스 상태
        int wait_time;       // READY 상태 대기 누적 시간
} PCB;

PCB pcb[NPROC];
int baseTQ = 3;          // 기본 타임퀀텀, 명령줄 인수로 조정
int running_idx = -1;    // 현재 실행중인 프로세스 인덱스
int rqueue[NPROC], rq_front = 0, rq_rear = 0, rq_count = 0; // READY 큐 구조
int sleep_idx[NPROC], sleep_wake[NPROC], scount = 0; // SLEEP 큐 구조
volatile sig_atomic_t run_flag = 0, alive = NPROC;   // flag 관련 변수

// READY 큐에 READY 상태 프로세스만 삽입 (중복·비정상 방지)
void rq_push(int idx) {
        if (rq_count < NPROC && pcb[idx].state == READY) {
                // 중복 방지
                for (int i=0;i<rq_count;i++){
                        if (rqueue[(rq_front+i)%NPROC] == idx){
                                return;
                        }
                }
        printf("[Parent] PCB[%d] -> READY 큐 삽입\n", idx);
        rqueue[rq_rear] = idx;
        rq_rear = (rq_rear + 1) % NPROC;
        rq_count++;
        }
}

// READY 큐에서 다음 READY 상태만 반환. 비정상은 자동 SKIP.
int rq_pop() {
        int search = rq_count;
        while (search--) {
                int idx = rqueue[rq_front];
                rqueue[rq_front] = -1;
                rq_front = (rq_front + 1) % NPROC;
                rq_count--;
                if (pcb[idx].state == READY) {
                        printf("[Parent] READY 큐에서 PCB[%d] 꺼냄\n", idx);
                        return idx;
                }
        }
        return -1;
}

// SLEEP 큐 삽입 (자식이 I/O 요청할 때)
void sleep_insert(int idx, int sec) {
        time_t now = time(NULL);
        sleep_idx[scount]=idx; sleep_wake[scount]=now+sec;
        pcb[idx].state=SLEEP; scount++;
        printf("[Parent] PCB[%d] I/O 대기 %d초 등록(wake at %d)\n", idx, sec, sleep_wake[scount-1]);
}

// SLEEP 큐 인출: wakeup_time이 되면 READY 전환 및 new burst 준비
void sleep_update() {
        time_t now = time(NULL);
        for(int i=0;i<scount;i++) {
                if(sleep_wake[i]<=now) {
                        int idx = sleep_idx[i];
                        printf("[Parent] PCB[%d] I/O 완료 -> READY 큐 복귀\n", idx);
                        pcb[idx].tq = baseTQ;
                        pcb[idx].state = READY;
                        rq_push(idx);
                        // SLEEP 큐에서 삭제 및 보정
                        for(int j=i; j<scount-1;j++) {
                                sleep_idx[j]=sleep_idx[j+1]; sleep_wake[j]=sleep_wake[j+1];
                        }
                        scount--; i--;

                        // 자식 깨움 : I/O 종료 후 burst 재초기화
                        kill(pcb[idx].pid, SIGUSR1);
                }
        }
}

// 자식이 SIGUSR1 받을 때마다 burst 1 감소/동작 플래그 설정
void child_run(int sig) {
        run_flag = 1;
}

// 자식→부모 I/O 요청 시그널 핸들러 (자식 burst==0이고 I/O)
void io_request(int sig, siginfo_t *info, void *ctx) {
        int idx = -1;

        for(int i=0;i<NPROC;i++) {
                if(pcb[i].pid==info->si_pid) {
                        idx=i;
                }
        }

        if(idx==-1) {
                return;
        }

        int sec = rand()%5+1;
        printf("[Parent] PCB[%d](PID %d)로부터 IO요청 받음 %d초\n", idx, info->si_pid, sec);
        sleep_insert(idx, sec);

        if(running_idx==idx) {
                printf("[Parent] PCB[%d] RUNNING에서 해제\n", idx);
                running_idx = -1;
        }
}

// 자식 종료(SIGCHLD)시 PCB 상태 갱신 및 큐 동기화
void child_exit(int sig) {
        int status; pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
                for(int i=0;i<NPROC;i++){
                        if(pcb[i].pid==pid) {
                                pcb[i].state = DONE;
                                if(running_idx==i) {
                                        running_idx=-1;
                                }
                                alive--;
                                printf("[Parent] PID %d 종료 -> PCB[%d] DONE (남은:%d)\n", pid, i, alive);
                        }
                }
                if(alive==0) {
                        printf("\n=== 모든 프로세스 종료 ===\n");
                        int tw=0;
                        for(int i=0;i<NPROC;i++) {
                                printf("PID:%d wait:%d\n",pcb[i].pid,pcb[i].wait_time); tw+=pcb[i].wait_time;
                        }
                        printf("평균 대기:%.2f초\n",tw/(double)NPROC);
                        exit(0);
                }
        }
}

// 1초마다 호출되는 부모 타이머 시그널 핸들러 (논리 오류 없이)
void timer_tick(int sig) {
        alarm(1);
        printf("[Parent] TIMER 발생\n");
        sleep_update();

        // READY 상태의 프로세스 대기시간 누적
        for(int i=0;i<NPROC;i++) {
                if(pcb[i].state == READY) {
                        pcb[i].wait_time++;
                }
        }

        // RUNNING 프로세스가 비정상(DONE/SLEEP)이면 해제
        if (running_idx != -1 && (pcb[running_idx].state == DONE || pcb[running_idx].state == SLEEP)) {
                printf("[Parent] 현재 RUNNING 프로세스가 DONE 또는 SLEEP → 실행 중지\n");
                running_idx = -1;
        }

        // RUNNING 없으면 READY 큐에서 선정해서 RUNNING으로 올림 + 타임퀀텀 초기화
        if(running_idx==-1) {
                int candidate = rq_pop();
                if (candidate != -1 && pcb[candidate].state == READY) {
                    running_idx = candidate;
                    pcb[running_idx].state = RUNNING;
                    pcb[running_idx].tq = baseTQ; // 스케줄 시 타임퀀텀 초기화!!
                    printf("[Parent] PCB[%d](PID %d) 실행(RUNNING) tq=%d\n", running_idx, pcb[running_idx].pid, pcb[running_idx].tq);
                }
        }
        // 반복적으로 RUNNING에게 SIGUSR1 보내기 (타임퀀텀 0 될 때까지)
        if (running_idx != -1 && pcb[running_idx].state == RUNNING) {
                kill(pcb[running_idx].pid, SIGUSR1);
                // 타임퀀텀 감소
                printf("[Parent] PCB[%d](PID %d) 타임퀀텀 감소 직전 (현재 TQ:%d)\n",running_idx, pcb[running_idx].pid, pcb[running_idx].tq);
                pcb[running_idx].tq--;
                if(pcb[running_idx].tq < 0) {
                        pcb[running_idx].tq = 0;
                }
                printf("[Parent] PCB[%d](PID %d) 타임퀀텀 감소 후 (남은 TQ:%d)\n",running_idx, pcb[running_idx].pid, pcb[running_idx].tq);


                // 타임퀀텀이 0이면 READY 큐로 이동, 다음 프로세스 선정
                if(pcb[running_idx].tq == 0) {
                        // 모든 READY 프로세스의 타임퀀텀이 0이면 초기화(옵션)
                        int all_zero=1;
                        for(int i=0;i<NPROC;i++)
                                if(pcb[i].state==READY && pcb[i].tq > 0){
                                        all_zero=0;
                                }
                                if(all_zero) {
                                        printf("[Parent] 모든 READY 프로세스 타임퀀텀 0 -> 재초기화\n");
                                for(int i=0;i<NPROC;i++) {
                                        if(pcb[i].state==READY) pcb[i].tq=baseTQ;
                                }
                        }
                        // RUNNING → READY 전이 (DONE/SLEEP은 예외)
                        if (pcb[running_idx].state != DONE && pcb[running_idx].state != SLEEP){
                                pcb[running_idx].state = READY;
                        }
                        rq_push(running_idx);
                        running_idx = -1;
                }
        }
}

// 자식 프로세스: SIGUSR1 받을때마다 burst 감소, 0이되면 종료/IO
void child_main() {
        signal(SIGUSR1, child_run);
        srand(getpid() ^ time(NULL));
        int init_burst = rand()%10+1;
        int burst = init_burst;

        printf("[Child %d] 시작, CPU burst:%d\n", getpid(), burst);

        while(1) {
                pause(); // SIGUSR1 받을때 활성
                if(run_flag) {
                        run_flag=0;
                        printf("%d run_flag=",run_flag);
                        // burst 0이면 더 이상 감소 X (음수 방지)
                        if(burst > 0) {
                                printf("[Child %d] 작업 실행 (burst 감소 전:%d)\n", getpid(), burst);
                                burst--;
                                printf("[Child %d] burst 감소 후:%d\n", getpid(), burst);
                                if(burst==0) {
                                        // 완료시 종료(50%) 혹은 I/O 요청
                                        if(rand()%2==0) {
                                                printf("[Child %d] CPU burst 끝, 종료!\n", getpid());
                                                exit(0);
                                        } else {
                                                kill(getppid(), SIG_IO_REQUEST);
                                                printf("[Child %d] I/O 요청! (BLOCK)\n", getpid());
                                                pause(); // I/O가 끝날때까지 BLOCK
                                                burst = init_burst;
                                                printf("[Child %d] I/O 완료 후 새 burst:%d\n", getpid(), burst);
                                        }
                                }
                        } // burst가 0 이하일 때는 아무 것도 않함
                }
         }
}

int main(int argc, char* argv[]) {

        if(argc>1 && atoi(argv[1])>0) {
                baseTQ=atoi(argv[1]);
        }
        else {
                baseTQ=3;
        }
        printf("=== 시뮬레이터 시작 (타임퀀텀:%d초) ===\n", baseTQ);

        srand(getpid() ^ time(NULL));

        // 핸들러 세팅
        struct sigaction sa_io = {.sa_sigaction=io_request, .sa_flags=SA_SIGINFO};
        sigemptyset(&sa_io.sa_mask);
        sigaction(SIG_IO_REQUEST, &sa_io, NULL);
        signal(SIGCHLD, child_exit);
        signal(SIGALRM, timer_tick);

        // 자식 프로세스 생성
        for(int i=0;i<NPROC;i++){
                pid_t pid = fork();
                if(pid==0) {
                        child_main(i);
                        exit(0); // safety
                } else {
                        pcb[i].pid=pid;
                        pcb[i].state=READY;
                        pcb[i].tq=baseTQ;
                        pcb[i].wait_time=0;
                        rq_push(i); // 초기 READY 큐 세팅
                        printf("[Parent] 자식 PCB[%d] PID:%d 준비완료\n", i, pid);
                }
        }
        alarm(1);
        while(1) {
                pause();
        }
}
