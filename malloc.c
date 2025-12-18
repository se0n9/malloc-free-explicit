#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 기본 상수 정의 */
#define WSIZE       8           // Word size (Bytes) - 헤더/푸터 크기
#define DSIZE       16          // Double word size (Bytes) - 정렬 기준
#define MEM_SIZE    800         // 문제에서 지정한 메모리 풀 크기

/* 비트 조작을 위한 마스크 */
#define ALLOC_MASK      0x1     // 현재 블록 할당 여부 (0:Free, 1:Alloc)
#define PREV_ALLOC_MASK 0x2     // 이전 블록 할당 여부 (0:Free, 1:Alloc)
#define SIZE_MASK       (~0xF)  // 하위 4비트를 지우고 사이즈만 추출

/* 매크로 함수 (안전한 포인터 연산을 위해 괄호 필수) */
// 크기, 현재 할당, 이전 할당 정보를 합쳐서 헤더 값을 만듦
#define PACK(size, alloc, prev_alloc) ((size) | (alloc) | (prev_alloc))

// 주소 p에서 값 읽기/쓰기 (64비트 기준 unsigned long 사용)
#define GET(p)          (*(unsigned long *)(p))
#define PUT(p, val)     (*(unsigned long *)(p) = (val))

// 주소 p에서 사이즈, 할당여부, 이전할당여부 읽기
#define GET_SIZE(p)         (GET(p) & SIZE_MASK)
#define GET_ALLOC(p)        (GET(p) & ALLOC_MASK)
#define GET_PREV_ALLOC(p)   (GET(p) & PREV_ALLOC_MASK)

// 블록 포인터(bp)를 주면 헤더와 푸터의 주소를 반환
#define HDRP(bp)    ((char *)(bp) - WSIZE)
// 주의: FTRP는 현재 블록이 Free 상태일 때만 유효함 (Alloc은 푸터 없음)
#define FTRP(bp)    ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

// 다음 블록과 이전 블록의 포인터(bp) 반환
#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE(HDRP(bp)))
// 주의: PREV_BLKP는 이전 블록이 Free 상태일 때만 유효함 (Footer를 통해 이동)
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* 전약 변수 설정*/
static char mem_pool[MEM_SIZE]; // 800 바이트 크기의 정적 메모리 배열
static char *heap_listp; // 힙의 시작점을 가리킬 포인터

typedef int data_t;

static void *find_fit(size_t asize);//함수 프로토타입
static void place(void *bp, size_t asize);
int round_up(int n, int m);
char *coalesce(char *p);

/* 함수 구현 */
void init_mem(){
    heap_listp = mem_pool;
    *(unsigned long*)(heap_listp) = 0; // 0-7바이트: 패딩 채우기
    *(unsigned long*)(heap_listp + WSIZE) = PACK(DSIZE, 1, 2); // 8-15바이트: Prologue Block 헤더 채우기
    *(unsigned long*)(heap_listp + (2*WSIZE)) = PACK(DSIZE, 1, 2); // 16-23바이트: Prologue Block 푸터 채우기

    heap_listp += (2*WSIZE); // heap_listp는 프롤로그 푸터를 가리킴

    size_t init_size = MEM_SIZE - (4*WSIZE);
    *(unsigned long*)(heap_listp + (WSIZE)) = PACK(init_size, 0, 2); // initial Free Block 생성
    
    char *footer_pos = (heap_listp + (WSIZE)) + init_size - WSIZE; // 792-799 번지 앞
    *(unsigned long*)(footer_pos) = PACK(init_size, 0, 2);

    *(unsigned long*)(footer_pos + (WSIZE)) = PACK(0, 1, 0); // Epilogue Header: footer + 8
}

char *mm_alloc(size_t size){//구현완
    size_t asize; // 조정된 블록 사이즈(헤더 + 정렬 패딩)
    char *bp; // 현재 블록의 payload 시작점

    // 1. size == 0 인 경우 NULL 처리
    if (size == 0) return NULL;

    // 2. 사이즈 조정 (Align)
    asize = round_up(size + WSIZE, DSIZE);
    
    // 3. 가용 리스트 탐색 (Find Fit)
    bp = find_fit(asize);

    if(bp == NULL){
        return NULL;
    }

    // 4. 찾았을 경우 배치(Place)
    place(bp, asize);

    return bp;
}

void mm_free(char *p){

    if(p == NULL){
        return;
    }
    // 현재 헤더의 alloc 업데이트, 푸터 쓰기 -> 새로운 헤더로 덮어씌우기
    size_t size = GET_SIZE(HDRP(p)); 
    int prev_alloc = GET_PREV_ALLOC(HDRP(p));
    PUT(HDRP(p), PACK(size, 0, prev_alloc)); // 헤더 갱신
    PUT(FTRP(p), PACK(size, 0, prev_alloc)); // 푸터 생성

    // 다음 블록의 prev_alloc을 0으로 갱신
    void *next_bp = NEXT_BLKP(p);
    char *next_hdrp = HDRP(next_bp);
    size_t next_size = GET_SIZE(next_hdrp);
    int current_alloc_next = GET_ALLOC(next_hdrp);
    PUT(next_hdrp, PACK(next_size, current_alloc_next, 0));

    coalesce(p);
}

char *coalesce(char *p){
    char *next_bp = NEXT_BLKP(p);
    char *current_hdrp = HDRP(p);

    size_t size = GET_SIZE(HDRP(p));
    int prev_alloc = 0;
    // Case 1(Default): 앞 할당O, 뒤는 free된 경우
    if(GET_PREV_ALLOC(current_hdrp) == 1 && GET_ALLOC(HDRP(next_bp)) == 1){
        return p;
    } 
    // Case 2: 앞 블록은 할당O, 뒤 블록은 free된 경우
    else if(GET_PREV_ALLOC(current_hdrp) == 1 && GET_ALLOC(HDRP(next_bp)) == 0){
        size += GET_SIZE(HDRP(next_bp)); // size += next_size
        prev_alloc = GET_PREV_ALLOC(HDRP(p));
        PUT(current_hdrp, PACK(size, 0, prev_alloc)); // 현재 헤더 위치에 크기 갱신
        PUT(FTRP(next_bp), PACK(size, 0, prev_alloc));
        return p;
    }
    // Case 3: 앞 블록은 free, 뒤는 할당된 상태인 경우
    else if(GET_PREV_ALLOC(current_hdrp) == 0 && GET_ALLOC(HDRP(next_bp)) == 1){
        char *prev_bp = PREV_BLKP(p);
        char *prev_footer = (p - WSIZE); // 이전 블록의 푸터 포인터
        size += GET_SIZE(prev_footer);
        prev_alloc = GET_PREV_ALLOC(HDRP(prev_bp)); // 앞 블록의 prev_alloc 비트
        PUT(HDRP(prev_bp), PACK(size, 0, prev_alloc)); //이전 블록의 헤더 갱신
        PUT(FTRP(p), PACK(size, 0, prev_alloc)); // 현재 블록의 푸터 갱신
        p = (HDRP(prev_bp) + WSIZE); // p를 이전 블록의 페이로드를 가리키도록 갱신
        return p;
    }
    //Case 4: 앞, 뒤 블록 모두 free되어 있는 경우
    else {
        char *prev_bp = PREV_BLKP(p);
        size = (GET_SIZE(HDRP(prev_bp)) + GET_SIZE(current_hdrp) + GET_SIZE(HDRP(next_bp))); // 이전+현재+다음 블록 크기 다 더하기
        prev_alloc = GET_PREV_ALLOC(HDRP(prev_bp)); // 이전 블록이 갖고있던 prev_alloc 정보 저장
        PUT(HDRP(prev_bp), PACK(size, 0, prev_alloc)); // 이전 블록 헤더 갱신
        PUT(FTRP(next_bp), PACK(size, 0, prev_alloc)); // 다음 블록의 푸터에 전체 크기 쓰기 
        p = (HDRP(prev_bp) + WSIZE);
        return p; 
    }
}

void show_mm(){//출력하는것


}

static void *find_fit(size_t asize){//구현완
    void* bp;//block pointer 선언

    for(bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
        if(GET_ALLOC(HDRP(bp))==0 && GET_SIZE(HDRP(bp))>=asize){
            return bp;
        }
    }
    return NULL;
    //헤더정보를가지고 할당한 사이즈정보, 하위 첫번째 비트(할당여부)를 확인하여 진행
}

static void place(void *bp, size_t asize){
    size_t csize = GET_SIZE(HDRP(bp)); // current block size를 담을 변수
    void* next_bp = NEXT_BLKP(bp); //블록 포인터를 기존 블록 사이즈 기준의 다음 블록의 헤더로 이동
    int isPrevAlloc = GET_PREV_ALLOC(HDRP(bp));
    
    if(csize - asize<16){
        PUT(HDRP(bp), PACK(csize, 1, isPrevAlloc)); //할당되었다고 업데이트하기
        PUT(HDRP(next_bp), PACK(GET_SIZE(HDRP(next_bp)), GET_ALLOC(HDRP(next_bp)), 2));//다음 블록의 헤더도 이전 블록이 할당되었다고 업데이트하기
    }
    else{
        PUT(HDRP(bp),PACK(asize, 1, isPrevAlloc));//split해서 필요한 만큼만 공간 할당하기
        void* nextbp = NEXT_BLKP(bp);//할당되지 않은 다음 공간으로 이동하기
        PUT(HDRP(nextbp), PACK(csize-asize, 0, 2));//할당한 정보 헤더에 업데이트
        PUT(FTRP(nextbp), PACK(csize-asize, 0, 2));//할당한 정보 푸터에도 업데이트
        PUT(HDRP(next_bp), PACK(GET_SIZE(HDRP(next_bp)), GET_ALLOC(HDRP(next_bp)), 0));//나누기 전의 블록의 다음 블록의 헤더의 previous block을 할당x(0) 상태로 변경 
    }
}

int round_up(int n, int m){//구현완
    return ((n+m-1)/m)*m;
}

#define DO_SHOW(cmd) printf("<%s>\n", #cmd); (cmd); show_mm()
int main(){
    char *p[10];
    DO_SHOW(init_mem());
    DO_SHOW(p[0] = mm_alloc(100));
    DO_SHOW(p[1] = mm_alloc(300));
    DO_SHOW(p[2] = mm_alloc(70));
    DO_SHOW(p[3] = mm_alloc(180));
    DO_SHOW(mm_free(p[2]));
    DO_SHOW(mm_free(p[3]));
    DO_SHOW(p[4]= mm_alloc(50));
    DO_SHOW(mm_free(p[0]));
    DO_SHOW(mm_free(p[1]));
    DO_SHOW(p[5] = mm_alloc(120));
    DO_SHOW(mm_free(p[4]));
    DO_SHOW(mm_free(p[5]));


    return 0;
}