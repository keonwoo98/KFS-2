# KFS_2 보너스 설계 문서 — 폴링 키보드 + 디버그 미니셸

- 프로젝트: 42 Kernel From Scratch, 2편 보너스 (`kfs2.pdf` VI장)
- 작성일: 2026-08-28
- 상태: 설계 확정 대기(사용자 검토 전)
- 기반: mandatory 완료본 (main, `61372e3`) — KFS-1 최종본 뼈대 동기화까지 반영됨

---

## 1. 개요 및 목표

`kfs2.pdf` VI장의 보너스는 한 문단이다.

> Assuming your keyboard work correctly in your Kernel, and you able to catch an
> entry, let's code a Shell! Not a POSIX Shell, just a minimalistic shell with a
> few commands, for debugging purposes. For example, you could implement the
> print-kernel-stack-thing in this shell, and some other things like `reboot`,
> `halt` and such.

"키보드가 동작한다는 전제"가 우리에겐 없다. KFS_1에서 키보드 보너스를 건너뛰었으므로
**폴링 키보드 드라이버부터 만들어야 한다.** 그게 이 작업의 실제 비용이고, 셸 자체는
그 위에 얹히는 얇은 층이다.

**목표**
- 8042 폴링 기반 키보드 드라이버 (IDT/PIC 불요)
- 라인 편집 + 인자 파싱을 갖춘 디버그 셸, 명령 7종
- 기존 mandatory 12 assertion 무회귀
- 셸 로직의 자동 검증(2계층)

**두 가지 목적과 그 우선순위**
1. **KFS_2 점수** — 보너스는 미니셸 하나뿐이고 `100 + round(3.6 × 등급)`. 즉
   100점 → 최대 118점이 이 과제 변별의 전부다. 실측 7건 중 셸 없는 1건만 100점.
2. **KFS_3 개발 도구** — KFS_3 서브젝트가 "the last 'mini-shell' subject"라며 셸의
   존재를 전제한다. 다만 같은 문단이 `will be not graded`라고 못박으므로 **KFS_3
   점수에는 기여하지 않는다.** 난이도 850짜리 페이징·`kmalloc` 디버깅을 대화형으로
   할 수 있다는 개발 편의가 이익의 전부다. 이 구분을 흐리지 말 것.

이 두 목적이 인자 파싱 여부에서 갈렸고, 2번을 근거로 **인자 파싱을 포함**하기로 했다
(§3 결정 로그).

---

## 2. 과제 요구사항 요약

VI장은 스펙을 주지 않는다. 명령 개수도, 이름도, 편집 기능도 정하지 않는다.
예시로 `print-kernel-stack`, `reboot`, `halt`를 든다.

**전제 조건 (빨간 경고 박스, 원문)**

> The bonus part will only be assessed if the mandatory part is PERFECT. Perfect
> means the mandatory part has been integrally done and works without
> malfunctioning. If you have not passed ALL the mandatory requirements, your
> bonus part will not be evaluated at all.

→ **회귀는 곧 보너스 0점이다.** 기존 12 assertion 보존이 이 설계의 최상위 제약이며,
§9의 화면 예산 계산과 §10의 계층 분리가 전부 여기서 나온다.

---

## 3. 결정 로그 (확정 사항)

| 항목 | 결정 | 근거 |
|---|---|---|
| 입력 방식 | **논블로킹 폴링** (`keyboard_poll()`이 키 없으면 0 반환) | 블로킹 대비 비용 차이가 5줄 남짓인데, 셸 아닌 코드도 키를 물어볼 수 있고(KFS_3의 "덤프 중 중단"), 나중에 IRQ1로 갈아끼울 때 시그니처가 유지된다 |
| IRQ1 + IDT | **기각** | IDT·PIC 리매핑·EOI가 통째로 딸려오고 이건 KFS_4(인터럽트)의 주제 자체다. 지금 선점하면 규모가 2배 이상이 되고, 잘못 만지면 triple fault로 이미 안전한 mandatory까지 위험해진다 |
| 인자 파싱 | **포함** (`dump <addr> [len]`) | KFS_3에서 임의 주소를 못 보면 셸이 도구로서 값을 잃는다. 볼 주소가 생길 때마다 커널을 고쳐 재부팅하는 것이 대안인데 그건 도구가 아니다 |
| 수 표기 | `0x` 접두면 16진수, 없으면 10진수 | `dump 0x100000 64` — 주소는 hex, 길이는 dec가 가장 자연스러운 타이핑 |
| CapsLock / Ctrl | **제외** | 명령이 전부 소문자라 실익 0. 토글 상태가 늘면 설명할 거리만 는다 |
| 명령 이름 | `print-kernel-stack` → **`stack`** 으로 축약 | 히스토리도 탭 완성도 없는 셸에서 디펜스 시계를 보며 24자를 치는 것은 실수 유발. `help`가 전부 나열하므로 발견성 문제 없음 |
| `\b` 의미 | vga는 **커서만 뒤로**, 지우는 것은 셸이 `"\b \b"` | 터미널 표준 의미. vga가 "지운다"는 정책을 갖지 않게 한다 (`\r`과 같은 결) |
| selftest 범위 | **순수 함수만** (파서·토크나이저). 명령 실행은 하지 않음 | selftest는 배너보다 먼저 돌므로 여기서 덤프를 찍으면 배너가 밀려 기존 assertion이 깨진다 (§9) |
| 통합 테스트 | 기존 `make test` **불변**, `make test-shell` 신설 | 회귀 방지망과 신규 검증을 분리해야 어느 쪽이 깨졌는지 즉시 갈린다 |
| 셸 위치 | `src/shell/` 신규 디렉토리 | KFS_1 설계 §6이 `src/gdt/`·`src/memory/`를 예고한 것과 같은 결. Makefile은 `find src`라 빌드 규칙 수정 없음 |

---

## 4. 아키텍처 & 부팅 흐름

```
[GRUB] -> [boot.asm: _start] -> [kernel.c: kernel_main]
     vga_init
     magic 검증
     gdt_init
     selftest (libk + scroll + GDT + ★셸 파서/토크나이저)
     배너 · ptest · print_kernel_stack
     "42"
     keyboard_init          <- ★ 신규: 8042 잔여 바이트 배출
     shell_run              <- ★ 신규: 반환하지 않음
```

**의존 방향** (스펙 §7의 사슬 연장, 여전히 단방향):

```
types -> string -> vga -> printk -> {gdt, stack_dump, keyboard} -> shell -> kernel
```

`keyboard`는 `io.h`·`types.h`만 본다 — 셸의 존재를 모른다. `shell`이 keyboard·vga·
printk·libk와 기존 명령 구현(`print_kernel_stack`, `dump_hex`)을 엮는다.

**신규/수정 파일**

```
include/keyboard.h        ★신규
include/shell.h           ★신규
src/drivers/keyboard.c    ★신규
src/shell/shell.c         ★신규
include/vga.h             수정: 없음 (vga_putchar 시그니처 불변)
src/drivers/vga.c         수정: vga_putchar에 '\b' 분기
src/kernel/kernel.c       수정: keyboard_init + shell_run 호출
src/kernel/selftest.c     수정: test_shell_parse 추가
tests/boot_test.sh        수정: 키 주입 옵션
Makefile                  수정: test-shell 타깃
```

---

## 5. 키보드 드라이버 상세

### 5.1 인터페이스

```c
void keyboard_init(void);   /* 8042 출력 버퍼에 남은 잔여 바이트를 배출 */
char keyboard_poll(void);   /* 키 없으면 0, 있으면 ASCII 한 글자 */
```

`keyboard_init`이 필요한 이유는 `vga_set_cursor`와 같다 — **GRUB이 남긴 상태를
물려받지 않기 위해서**다. BIOS·GRUB이 읽다 만 스캔코드가 0x60에 남아 있으면 셸이
뜨자마자 유령 입력이 들어온다. 부팅 시 OBF가 설 동안 읽어 버린다.

반환 `0`이 "키 없음"이므로 ASCII NUL은 절대 반환하지 않는다. 타이핑할 수 없는
글자라 손실이 없다.

### 5.2 포트와 상태 비트

| 포트 | 용도 |
|---|---|
| `0x60` | 데이터 (스캔코드 읽기) |
| `0x64` | 상태 읽기 / 명령 쓰기 |

`inb(0x64)`의 **비트 0 (OBF, Output Buffer Full)** 이 서면 읽을 바이트가 있다.
안 서 있으면 `keyboard_poll`은 즉시 0을 반환한다 — 이것이 논블로킹의 실체다.

(비트 5는 AUX = 마우스 데이터를 뜻하지만, 우리는 PS/2 마우스를 초기화하지 않으므로
설 일이 없다. 검사하지 않는다.)

### 5.3 스캔코드 세트 1 처리

8042는 기본 translation이 켜져 있어 세트 1(XT)을 낸다. 처리할 경우가 넷이다.

| 입력 | 처리 |
|---|---|
| make 코드 (`0x01`~`0x58`) | 변환표에서 ASCII로 |
| break 코드 (`make \| 0x80`) | Shift 해제 추적에만 사용, 그 외 버림 |
| `0x2A`/`0x36` (L/R Shift) | 눌림 플래그 set. `0xAA`/`0xB6`에서 clear |
| `0xE0` 접두 | 확장 키(화살표 등). **접두와 다음 1바이트를 함께 소비하고 무시** |

`0xE0`을 접두만 버리고 다음 바이트를 남기면 화살표를 눌렀을 때 뒤 바이트가 엉뚱한
글자로 새어 나온다. 눈에 잘 안 띄는 함정이라 처음부터 넣는다. 논블로킹 구조에서는
"다음 바이트가 아직 안 왔을" 수 있으므로, **`0xE0`을 본 시점에 플래그를 세우고
다음 `keyboard_poll` 호출에서 소비**한다(드라이버 내부 정적 상태).

### 5.4 변환표

무시프트/시프트 두 벌, 각 128바이트 (`.rodata` 합계 256 B).

```c
static const char kbd_us[128]       = { ... };
static const char kbd_us_shift[128] = { ... };
...
c = shift ? kbd_us_shift[sc] : kbd_us[sc];
```

시프트 규칙을 코드로 표현하려 애쓰는 것보다 짧고 틀릴 여지가 없다.
매핑 없는 자리는 `0`으로 두어 자동으로 무시된다.

**특수 반환값**: Enter(`0x1C`) → `'\n'`, Backspace(`0x0E`) → `'\b'`, Space → `' '`.

---

## 6. 셸 상세

### 6.1 진입점

```c
void shell_run(void);   /* 반환하지 않는다 */
```

동작은 프롬프트 하나를 찍고 루프에 드는 것이 전부다.

```
shell_run:
    프롬프트 출력 ("kfs> ")
    무한 루프:
        c = keyboard_poll()
        c == 0  -> pause, 계속
        c != 0  -> 문자 처리 (§6.2). 개행이면 디스패치 후 프롬프트 재출력
```

**시작 배너를 찍지 않는다.** `kfs shell -- type 'help'` 같은 안내가 자연스러워
보이지만, §9의 화면 예산에 여유가 0이라 한 줄만 늘어도 `" 42 "`가 밀려 mandatory
테스트가 깨진다. 프롬프트 자체가 셸이 떴다는 신호이고, 발견성은 `help`가 담당한다.

### 6.2 라인 편집

버퍼 `char line[128]`. 명령 최장이 `dump 0x00100000 256` 정도(약 20자)라 넉넉하다.

| 입력 | 동작 |
|---|---|
| 인쇄 가능 문자 | 버퍼에 추가 + 에코. 가득 차면 조용히 버림 |
| `'\b'` | `len > 0`일 때만 `len--` + `vga_puts("\b \b")` |
| `'\n'` | 에코 개행 → 토크나이즈 → 디스패치 → 프롬프트 재출력 |

`vga_putchar`의 `'\b'`는 **커서만 한 칸 뒤로** 옮긴다. `col == 0`이면 아무것도 하지
않는다(이전 줄로 넘어가지 않음). 프롬프트를 지우고 들어가는 사고는 셸의 `len > 0`
검사가 막는다 — vga와 셸 양쪽에 방어가 하나씩 있다.

### 6.3 토크나이저

공백을 `\0`으로 바꾸는 in-place 방식. 최대 3토큰(명령 + 인자 2). 앞뒤·연속 공백은
건너뛴다. 빈 줄이면 프롬프트만 다시 찍는다.

### 6.4 수 파싱

```c
static int parse_u32(const char *s, uint32_t *out);  /* 0 성공, -1 실패 */
```

- `0x`/`0X` 접두 → 16진수, 그 외 → 10진수
- 자릿수가 아닌 문자를 만나면 실패
- 누적 중 32비트를 넘으면 실패 (곱하기 전에 검사)
- 빈 문자열 실패

실패 시 셸이 `dump: bad address` 형태로 알리고 명령을 실행하지 않는다.

### 6.5 명령 테이블

```c
struct cmd {
    const char *name;
    const char *help;
    void (*fn)(int argc, char **argv);
};
```

`{ NULL, NULL, NULL }`로 끝나는 정적 배열 + `strcmp` 선형 탐색.
**KFS_3에서 명령 추가가 표 한 줄이 되게 하는 것**이 이 구조의 목적이다.

| 명령 | 동작 | 구현 |
|---|---|---|
| `help` | 테이블 순회 출력 | 신규 (3줄) |
| `stack` | 커널 스택 hexdump | 기존 `print_kernel_stack()` 호출 |
| `gdt` | GDT 7엔트리 덤프 | 기존 `dump_hex((void *)0x800, 56)` |
| `dump <addr> [len]` | 임의 주소 덤프, 기본 len 64 | `parse_u32` + `dump_hex` |
| `clear` | 화면 지우기 | 기존 `vga_clear()` |
| `halt` | CPU 정지 | `cli; hlt` 루프 |
| `reboot` | 재부팅 | `outb(0x64, 0xFE)` 후 폴백으로 halt |

명령 7종 중 **신규 구현이 필요한 것은 `help`와 `dump`의 파싱부뿐**이다. 나머지는
mandatory에서 만든 함수를 부르는 한 줄이다.

`reboot`은 8042 리셋 라인을 펄스한다. 동작하지 않는 환경을 대비해 짧게 스핀한 뒤
halt로 떨어뜨려, 정의되지 않은 상태로 방치하지 않는다.

### 6.6 폴링의 비용 (디펜스 포인트)

대기 루프에 `hlt`를 **넣을 수 없다.** `hlt`는 인터럽트로만 깨는데 우리는 IF=0이고
IDT도 없으므로 영원히 깨어나지 않는다. 따라서 셸은 키를 기다리는 동안 CPU를 100%
쓴다. 이것이 접근 C(IRQ1)가 진짜로 사는 지점이며, 물어보면 그대로 답한다.
루프에 `pause`(`rep nop`) 한 줄은 넣는다 — 스핀 루프 힌트라 비용이 없다.

### 6.7 `.hang` 의미 변화 (디펜스 답변 갱신)

`shell_run`이 반환하지 않으므로 **정상 경로가 더 이상 `boot.asm`의 `.hang`에
도달하지 않는다.** 지금까지 *".hang은 안전장치가 아니라 매 부팅마다 도는 정상
경로"* 가 디펜스 포인트였는데, 셸이 들어오면 **다시 안전장치로 돌아간다** — PANIC
경로(magic 검증 실패)와 `halt` 명령만이 CPU를 멈춘다. 답변이 바뀌는 지점이므로
기존 디펜스 노트를 갱신할 것.

---

## 7. 데이터 흐름

```
사용자 키 누름
  -> 8042가 0x60에 스캔코드 적재, 0x64의 OBF 비트 set
  -> shell_run 루프의 keyboard_poll()
       OBF 미설정 -> 0 반환 (루프 계속)
       OBF 설정   -> inb(0x60)
                     break 코드 / 0xE0 -> 상태만 갱신, 0 반환
                     Shift make/break  -> 플래그 갱신, 0 반환
                     그 외             -> 변환표 -> ASCII 반환
  -> shell이 문자별 처리 (에코 / 백스페이스 / 개행)
  -> 개행이면 토크나이즈 -> 테이블 탐색 -> fn(argc, argv)
  -> 명령이 printk/dump_hex로 출력 -> vga -> 0xB8000
```

---

## 8. 에러 처리

| 상황 | 처리 |
|---|---|
| 알 수 없는 명령 | `unknown command: <name>` + `type 'help'` 안내 |
| `dump` 인자 누락 | `usage: dump <addr> [len]` |
| `dump` 인자 파싱 실패 | `dump: bad address` — 실행하지 않음 |
| 라인 버퍼 초과 | 조용히 버림 (에코도 안 함) |
| 매핑 없는 키 | `keyboard_poll`이 0 반환 — 셸까지 오지 않음 |

**의도적으로 하지 않는 것**: 주소 유효성 검사. `dump 0xFFFFFFFF`는 그냥 그 주소를
읽는다. 페이징이 없어 잘못된 물리 주소도 폴트 없이 쓰레기를 읽을 뿐이고,
"보이는 대로 보여주는" 것이 디버거의 올바른 동작이다. KFS_3에서 페이징이 켜지면
그때 폴트가 나는 것이 정상이며, 그 폴트를 잡는 것이 KFS_3의 주제다.

---

## 9. 회귀 예산 (최상위 제약)

VI장이 "mandatory가 PERFECT해야 보너스를 평가한다"고 못박으므로, 기존 12 assertion
보존이 다른 모든 것에 우선한다.

**현재 화면 사용량** (실측, `make test` 출력 기준)

```
SCRL17 ~ SCRL29          13행
kfs: gdt ok               1
kfs: selftest ok          1
kfs: multiboot ok (...)   1
ptest [...]               1
kernel stack: esp=...     1
덤프 4행                   4
빈 줄 ("\n42\n"의 앞 \n)   1
42                        1
------------------------------
합계                      24행,  커서는 25번째 행(인덱스 24)
```

프롬프트 `kfs> `는 **그 비어 있는 25번째 행에 들어가므로 스크롤이 일어나지 않는다.**
따라서 무입력 상태에서 기존 assertion 12개가 그대로 통과할 것으로 예측한다.

> ⚠️ **여유가 0이다.** 배너가 한 줄만 늘거나 프롬프트 앞에 개행을 넣으면 `" 42 "`가
> 밀려 기존 테스트가 깨진다. 이는 **예측이며 첫 태스크에서 실측으로 확인**한다.
> 어긋나면 프롬프트 앞 개행 제거 → 그래도 부족하면 `scroll_exercise`를 30행에서
> 28행으로 줄이는 순으로 대응한다(SCRL29 assertion을 SCRL27로 함께 조정).

---

## 10. 검증 방법 (2계층)

계층을 나누는 이유는 **깨졌을 때 어디가 문제인지 즉시 갈리게** 하기 위해서다.

### 계층 1 — 순수 로직 selftest (커널 내부)

`selftest.c`에 `test_shell_parse()`를 추가한다. **명령을 실행하지 않는다** —
selftest는 배너보다 먼저 돌므로 여기서 덤프를 찍으면 §9의 예산이 즉시 무너진다.
화면에 아무것도 남기지 않는 순수 함수만 검사한다.

| 검사 | 기대 |
|---|---|
| `parse_u32("0x1234")` | 성공, `0x1234` |
| `parse_u32("64")` | 성공, `64` |
| `parse_u32("0xFFFFFFFF")` | 성공, `0xFFFFFFFF` |
| `parse_u32("0x100000000")` | **실패** (32비트 초과) |
| `parse_u32("12g")` | **실패** |
| `parse_u32("")` | **실패** |
| 토크나이저 `"  dump   0x10  20  "` | `argc == 3`, 토큰 3개 정확 |
| 토크나이저 `""` | `argc == 0` |

기존 `check()` 규약(조용한 성공, 실패 시 빨간 `SELFTEST FAIL:`)에 그대로 얹힌다.
성공하면 화면 사용량이 0이므로 §9 예산에 영향이 없다.

### 계층 2 — `sendkey` 통합 테스트 (커널 외부)

`tests/boot_test.sh`에 키 주입 옵션을 추가하고, **기존 `make test`는 건드리지 않은 채**
`make test-shell`을 신설한다.

```
sendkey g / sendkey d / sendkey t / sendkey ret   ->  셸이 `gdt` 실행
assertion: 화면에 "ff ff 00 00 00 9a cf 00"
```

이 바이트열은 우리가 `gdt.c`에 쓴 커널 코드 디스크립터의 값이고 selftest가 이미
`memcmp`로 확인 중이라 **결정적**이다. `reboot`은 자동 테스트 시퀀스에 넣지 않는다 —
QEMU가 `-no-reboot`으로 종료해 VGA 덤프를 잃는다.

> ⚠️ **미검증 가정**: QEMU 모니터 `sendkey`가 `-display none`에서 의도대로 동작하는지
> 실측한 적이 없다. 키보드 컨트롤러는 디스플레이와 무관하게 에뮬레이트되므로 될 것으로
> 보지만, **구현 플랜의 첫 스텝을 "sendkey 실현 가능성 확인"으로 잡는다.** 실패하면
> 계층 2를 접고 계층 1 + 수동 시연으로 내려간다. mandatory는 어느 쪽이든 영향 없다.

---

## 11. 범위 밖 (Non-goals)

- **커맨드 히스토리, 화살표 키, 탭 완성** — 평가 코멘트가 `tres joli shell` 한 줄인
  것을 보면 정교함이 아니라 동작 여부를 본다. 점수에 안 잡히고 시간만 먹는다
- **CapsLock, Ctrl 조합** — §3
- **파이프·리다이렉션·변수** — POSIX 셸이 아니라고 서브젝트가 명시
- **다중 화면 전환** — KFS_1 보너스 ⑤. `vga.c`의 전역 상태를 구조체로 바꿔야 해서
  이미 되는 것들을 걸고 하나를 얻는 거래
- **IRQ1 / IDT / PIC** — KFS_4 주제 (§3)
- **주소 유효성 검사** — §8
- **`regs` / `cr` 덤프 명령** — KFS_3에서 페이징을 켤 때 CR0.PG·CR3가 실제로 필요해지면
  그때 표에 한 줄 추가한다. 지금은 쓸 데가 없어 YAGNI

---

## 12. 열린 질문 / 리스크

- **`sendkey` 동작 여부** — §10. 플랜 첫 스텝에서 확인, 실패 시 계층 2 철회
- **화면 예산** — §9. 예측이며 실측 필요. 여유가 0이라 가장 먼저 깨질 곳
- **타입매틱(키 반복)** — 키를 오래 누르면 8042가 make 코드를 반복 전송한다. 폴링
  구조에서는 자연스럽게 여러 글자로 처리되며, 이는 실제 터미널과 같은 동작이라
  버그가 아니다. `sendkey`는 press+release를 한 번만 보내므로 테스트에는 무관
- **`0xE0` 상태를 정적 변수로 들고 가는 것** — 논블로킹이라 접두와 본체가 서로 다른
  `keyboard_poll` 호출에 걸쳐 도착할 수 있다. 드라이버 내부 상태로 처리하며,
  이는 IRQ 방식으로 갈아끼울 때도 그대로 유효한 구조다

---

## 부록 A. Defense 예상 질문

| 질문 | 답의 골자 |
|---|---|
| 왜 인터럽트를 안 쓰고 폴링인가? | 이 셸에 필요한 범위에서 폴링으로 충분하고, IDT·PIC 리매핑은 KFS_4의 주제다. 여기서 선점하면 규모가 2배가 되고 잘못 만지면 triple fault로 이미 통과한 mandatory까지 위험해진다 |
| 폴링의 단점은? | 대기 중 CPU를 100% 쓴다. `hlt`로 재울 수 없는데, `hlt`는 인터럽트로만 깨고 우리는 IF=0에 IDT도 없어 영원히 안 깨어나기 때문이다. 이것이 IRQ 방식의 실제 이점이다 |
| `keyboard_init`은 왜 필요한가? | GRUB이 남긴 잔여 스캔코드를 배출한다. `vga_set_cursor`와 같은 논리 — 부트로더가 남긴 상태를 물려받지 않는다 |
| `0xE0`은 무엇이고 왜 두 바이트를 버리나? | 화살표 등 확장 키의 접두 바이트다. 접두만 버리고 본체를 남기면 그 바이트가 엉뚱한 글자로 새어 나온다 |
| 스캔코드와 ASCII는 어떤 관계인가? | 무관하다. 스캔코드는 **키의 물리적 위치** 번호이고 ASCII는 문자 코드다. 그래서 변환표가 필요하고, Shift 여부에 따라 표가 두 벌이다 |
| 셸이 반환하지 않으면 `.hang`은? | 정상 경로에서는 더 이상 도달하지 않는다. PANIC 경로와 `halt` 명령만 CPU를 멈춘다. mandatory 시절과 답이 달라진 지점이다 |
| `dump`에 이상한 주소를 넣으면? | 그대로 읽는다. 페이징이 없어 폴트가 안 나고, 보이는 대로 보여주는 것이 디버거의 올바른 동작이다. KFS_3에서 페이징을 켜면 그때 폴트가 나는 것이 정상이고 그 폴트를 잡는 것이 그 과제의 주제다 |
| 셸을 어떻게 자동 테스트했나? | 두 계층이다. 파서·토크나이저 같은 순수 로직은 커널 안 selftest가 검사하고, 키보드 경로는 QEMU 모니터 `sendkey`로 키를 주입해 화면 결과를 검증한다. 계층을 나눈 이유는 깨졌을 때 어디가 문제인지 즉시 갈리게 하기 위해서다 |
| 왜 selftest에서 명령을 실행하지 않나? | selftest는 배너보다 먼저 돌기 때문에 거기서 덤프를 찍으면 배너가 스크롤에 밀려 기존 테스트가 깨진다. mandatory 시절 "스크롤 유발은 앞에, 남길 것은 뒤에"와 같은 제약이다 |
