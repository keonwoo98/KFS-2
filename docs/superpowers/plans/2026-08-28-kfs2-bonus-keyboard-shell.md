# KFS-2 Bonus — Polling Keyboard + Debug Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 8042 폴링 키보드 드라이버 위에 디버그용 미니셸(명령 7종)을 올려 `kfs2.pdf` VI장 보너스를 완성하고, 기존 mandatory 12 assertion을 무회귀로 유지한다.

**Architecture:** `keyboard.c`가 포트 `0x64`/`0x60`을 논블로킹으로 폴링해 스캔코드를 ASCII로 바꾸고, `shell.c`가 그 위에서 라인 편집·토크나이즈·명령 디스패치를 한다. 명령 구현은 대부분 mandatory에서 만든 `print_kernel_stack`·`dump_hex`를 부르는 한 줄이다. 검증은 2계층 — 순수 함수(스캔코드 변환·수 파싱·토크나이저)는 커널 안 selftest가, 키보드 하드웨어 경로는 QEMU 모니터 `sendkey`가 덮는다.

**Tech Stack:** C(freestanding) + NASM / `i686-linux-gnu-gcc`·`ld` / QEMU `qemu-system-i386` 모니터(`sendkey`, `xp`) / Docker·Podman 컨테이너 빌드.

**Spec:** `docs/superpowers/specs/2026-08-28-kfs2-bonus-shell-design.md`

## Global Constraints

(모든 Task에 암묵적으로 적용된다. 값은 스펙에서 그대로 옮긴 것이다.)

- **아키텍처**: i386(x86, 32-bit) 필수. 모든 오브젝트는 ELF32.
- **Freestanding**: `-ffreestanding -fno-builtin -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -nostdlib -nodefaultlibs`. 호스트 라이브러리·호스트 `.ld` 금지.
- **경고 0**: `-Wall -Wextra -Werror`. 경고가 하나라도 나면 그 Task는 미완성이다. 특히 명령 함수는 `argc`/`argv`를 안 쓰면 `(void)argc; (void)argv;`로 명시적으로 소비할 것 — `-Wunused-parameter`가 `-Wextra`에 포함된다.
- **빌드는 컨테이너 안에서만**: 호스트에서는 `make <target>`만 친다. 이미지 태그 `kfs2-build`, 플랫폼 `linux/amd64` 고정.
- **산출물 크기**: `kfs.iso` ≤ 10 MiB.
- **부팅 테스트 문자열 규칙**: `tests/boot_test.sh`의 기대 문자열은 **화면 한 행(80칸) 안**에 들어가야 한다. 디코딩된 버퍼는 행 단위로 이어붙으므로 행을 넘어가면 매칭이 실패한다.
- **회귀 금지 (최상위)**: `kfs2.pdf` VI장이 "mandatory가 PERFECT해야 보너스를 평가한다"고 못박는다. 기존 `make test`의 **12 assertion은 모든 Task 종료 시점에 전부 통과**해야 한다. mandatory 소스(`gdt.c`, `gdt_flush.asm`, `stack_dump.c`)와 기존 assertion 문자열은 **수정하지 않는다**.
- **화면 산술** (스펙 §9): VGA는 언제나 마지막 25행을 보여준다. 셸 출력은 항상 최신이라 기존 assertion과 경쟁하지 않는다(여유 13행). 이 계산은 Task 1에서 실측으로 확인한다.
- **커밋 메시지**: 본문 끝에 다음 두 줄을 붙인다.
  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01Fgnb5AiTd3EdViysQoWcfc
  ```
- **커밋 전 확인**: `git status`로 의도한 파일만 스테이징됐는지 확인한다. `*.o`·`*.d`·`isodir/`·`kernel.bin`은 커밋하지 않는다(Task 6의 `kfs.iso`만 예외).

## File Structure

| 파일 | 책임 | 상태 |
|---|---|---|
| `include/keyboard.h` | 키 입력 인터페이스 3개 선언 | 신규 (Task 1) |
| `src/drivers/keyboard.c` | 8042 폴링, 스캔코드 상태 기계, US 변환표 2벌 | 신규 (Task 1) |
| `include/shell.h` | `shell_run` + selftest용 순수 함수 2개 선언 | 신규 (Task 3) |
| `src/shell/shell.c` | 수 파싱·토크나이저·라인 편집·명령 테이블 | 신규 (Task 3~5) |
| `src/drivers/vga.c` | `vga_putchar`에 `'\b'` 분기 추가 | 수정 (Task 4) |
| `src/kernel/kernel.c` | `keyboard_init` + 셸 진입 배선 | 수정 (Task 1, 4) |
| `src/kernel/selftest.c` | `test_keyboard` / `test_shell` 추가 | 수정 (Task 1, 3) |
| `tests/boot_test.sh` | `KEYS` 환경변수로 키 주입 지원 | 수정 (Task 2) |
| `Makefile` | `test-shell` 타깃 추가 | 수정 (Task 2, 5) |
| `README.md` | 셸 사용법 추가 | 수정 (Task 6) |

`Makefile`이 `find src`로 소스를 모으므로 **빌드 규칙 자체는 수정하지 않는다.** `src/shell/`이 자동으로 잡힌다.

**스펙과의 차이 하나**: 스펙 §6.4는 `parse_u32`를 `static`으로 적었으나, §10 계층 1이 selftest에서 이 함수를 호출하므로 `static`일 수 없다. `shell.h`에 `shell_parse_u32`·`shell_tokenize`로 노출하고 "selftest용 공개"임을 주석으로 남긴다.

---

### Task 1: 키보드 드라이버 + 변환 selftest

**Files:**
- Create: `include/keyboard.h`
- Create: `src/drivers/keyboard.c`
- Modify: `src/kernel/selftest.c` (`test_keyboard` 추가, `selftest_run`에서 호출)
- Modify: `src/kernel/kernel.c` (`keyboard_init` + **임시** 에코 루프)

**Interfaces:**
- Consumes: `io.h`의 `inb`, `types.h`의 `uint8_t`/`bool`, `selftest.c`의 `check(int ok, const char *name)`.
- Produces (이후 Task가 그대로 사용):
  - `void keyboard_init(void)` — 8042 출력 버퍼 배출 + 내부 상태 초기화
  - `char keyboard_poll(void)` — 키 없으면 `0`, 있으면 ASCII 한 글자
  - `char kbd_translate(uint8_t sc, bool shift)` — 순수 변환. 하드웨어 접근 없음. selftest 전용으로 노출

**임시 에코에 대하여**: Task 4가 이 루프를 `shell_run()`으로 교체한다. 지금 넣는 이유는 Task 1과 Task 2를 **독립적으로 테스트 가능하게** 만들기 위해서다 — 키가 실제로 들어온다는 것을 보여줄 대상이 없으면 Task 2의 `sendkey` 프로브가 아무것도 증명하지 못한다.

- [ ] **Step 1: `include/keyboard.h` 작성**

```c
#ifndef KEYBOARD_H
# define KEYBOARD_H

# include "types.h"

/* Polling PS/2 keyboard. No IDT, no PIC: the shell asks for a key, it does
 * not get told about one. See the design spec for why IRQ1 is out of scope. */
void keyboard_init(void);   /* drain the 8042 output buffer, reset state */
char keyboard_poll(void);   /* 0 if no key is ready, else one ASCII char */

/* Pure scancode -> ASCII, no hardware access. Exposed so the on-boot
 * selftest can check the translation table without pressing keys. */
char kbd_translate(uint8_t sc, bool shift);

#endif
```

- [ ] **Step 2: `src/drivers/keyboard.c` 작성**

```c
#include "keyboard.h"
#include "io.h"

#define KBD_DATA     0x60
#define KBD_STATUS   0x64
#define KBD_OBF      0x01   /* status bit 0: output buffer full */

#define SC_LSHIFT    0x2A
#define SC_RSHIFT    0x36
#define SC_EXTENDED  0xE0
#define SC_BREAK     0x80   /* release = make code | 0x80 */

/* US QWERTY, scancode set 1. Index = make code; 0 means "no character".
 * Two tables rather than a shift rule: shorter, and nothing to get wrong. */
static const char kbd_us[128] = {
	 0,    0,   '1',  '2',  '3',  '4',  '5',  '6',   /* 0x00-0x07 */
	'7',  '8',  '9',  '0',  '-',  '=', '\b', '\t',   /* 0x08-0x0F */
	'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',   /* 0x10-0x17 */
	'o',  'p',  '[',  ']', '\n',   0,   'a',  's',   /* 0x18-0x1F */
	'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',   /* 0x20-0x27 */
	'\'', '`',   0,  '\\',  'z',  'x',  'c',  'v',   /* 0x28-0x2F */
	'b',  'n',  'm',  ',',  '.',  '/',   0,   '*',   /* 0x30-0x37 */
	 0,   ' ',   0,    0,    0,    0,    0,    0,    /* 0x38-0x3F */
};

static const char kbd_us_shift[128] = {
	 0,    0,   '!',  '@',  '#',  '$',  '%',  '^',   /* 0x00-0x07 */
	'&',  '*',  '(',  ')',  '_',  '+', '\b', '\t',   /* 0x08-0x0F */
	'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',   /* 0x10-0x17 */
	'O',  'P',  '{',  '}', '\n',   0,   'A',  'S',   /* 0x18-0x1F */
	'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',   /* 0x20-0x27 */
	'"',  '~',   0,   '|',  'Z',  'X',  'C',  'V',   /* 0x28-0x2F */
	'B',  'N',  'M',  '<',  '>',  '?',   0,   '*',   /* 0x30-0x37 */
	 0,   ' ',   0,    0,    0,    0,    0,    0,    /* 0x38-0x3F */
};

static bool g_shift;
static bool g_extended;

char kbd_translate(uint8_t sc, bool shift)
{
	if (sc >= 128)
		return 0;
	return shift ? kbd_us_shift[sc] : kbd_us[sc];
}

void keyboard_init(void)
{
	g_shift = false;
	g_extended = false;
	/* GRUB/BIOS may have read a key half-way and left a byte behind; without
	 * this the shell sees a phantom keystroke the moment it starts. Same
	 * reasoning as vga_set_cursor: do not inherit the bootloader's state. */
	while (inb(KBD_STATUS) & KBD_OBF)
		(void)inb(KBD_DATA);
}

char keyboard_poll(void)
{
	uint8_t sc;

	if (!(inb(KBD_STATUS) & KBD_OBF))
		return 0;                       /* nothing waiting: non-blocking */
	sc = inb(KBD_DATA);

	/* 0xE0 announces an extended key (arrows, etc). Its second byte arrives
	 * separately, possibly on a later poll, so the state has to be kept here.
	 * Dropping only the prefix would leak the second byte as a stray char. */
	if (g_extended) {
		g_extended = false;
		return 0;
	}
	if (sc == SC_EXTENDED) {
		g_extended = true;
		return 0;
	}
	if (sc & SC_BREAK) {                /* key release */
		sc = (uint8_t)(sc & 0x7F);
		if (sc == SC_LSHIFT || sc == SC_RSHIFT)
			g_shift = false;
		return 0;
	}
	if (sc == SC_LSHIFT || sc == SC_RSHIFT) {
		g_shift = true;
		return 0;
	}
	return kbd_translate(sc, g_shift);
}
```

- [ ] **Step 3: selftest에 변환 검사 추가 (RED)**

`src/kernel/selftest.c`의 `#include "gdt.h"` 아래에 `#include "keyboard.h"`를 추가하고, `scroll_exercise` 정의 **위에** 다음 함수를 넣는다.

```c
/* Pure table lookup: no hardware, no screen output. Silent on success. */
static void test_keyboard(void)
{
	check(kbd_translate(0x1E, false) == 'a',  "kbd a");
	check(kbd_translate(0x1E, true)  == 'A',  "kbd shift a");
	check(kbd_translate(0x02, false) == '1',  "kbd 1");
	check(kbd_translate(0x02, true)  == '!',  "kbd shift 1");
	check(kbd_translate(0x1C, false) == '\n', "kbd enter");
	check(kbd_translate(0x0E, false) == '\b', "kbd backspace");
	check(kbd_translate(0x39, false) == ' ',  "kbd space");
	check(kbd_translate(0x3B, false) == 0,    "kbd f1 unmapped");
}
```

`selftest_run`의 `test_gdt();` 다음 줄에 `test_keyboard();`를 추가한다.

```c
int selftest_run(void)
{
	g_failed = 0;
	scroll_exercise(); /* first: FAIL lines must survive the scrolling */
	test_mem();
	test_str();
	test_gdt();
	test_keyboard();
	return g_failed;
}
```

- [ ] **Step 4: 테스트가 실패하는지 확인 (RED)**

이 시점에는 `keyboard.c`가 이미 있으므로 검사는 통과한다. **대신 표가 틀렸을 때 잡히는지**를 증명한다: `kbd_us[0x1E]`를 일시적으로 `'z'`로 바꾼다.

Run: `make test`
Expected: **실패해야 정상.** 화면에 `SELFTEST FAIL: kbd a`, 그리고 `FAIL kernel selftest reported failure`. 확인 후 `'a'`로 되돌린다.

- [ ] **Step 5: `kernel_main`에 임시 에코 배선**

`src/kernel/kernel.c`의 `#include "printk.h"` 아래에 `#include "keyboard.h"`를 추가하고, `printk("\n42\n");` 다음에 다음을 넣는다.

```c
	/* TEMPORARY: replaced by shell_run() in Task 4. Present so that Task 2's
	 * sendkey probe has something that visibly reacts to a keystroke. */
	keyboard_init();
	for (;;) {
		char c = keyboard_poll();

		if (c != 0)
			vga_putchar(c);
		__asm__ volatile ("pause");
	}
```

`kernel_main`은 이제 반환하지 않는다.

- [ ] **Step 6: 회귀 + 화면 산술 확인 (GREEN)**

Run: `make test`
Expected: 경고 0, **기존 12 assertion 전부 통과**. 새 selftest 8개는 조용히 통과하므로 화면에 아무것도 추가되지 않는다.

이것이 스펙 §9 화면 산술의 실측 검증이다. `" 42 "`가 여전히 통과하면 계산이 맞은 것이다. **만약 깨지면 셸 쪽에서 대응하고 mandatory는 조정하지 않는다** — 이 Task에서는 에코 루프가 화면에 아무것도 안 찍으므로 깨질 이유가 없다.

- [ ] **Step 7: 손으로 확인 (선택)**

Run: `make run`
Expected: 부팅 화면 아래에서 타이핑하면 글자가 그대로 찍힌다. Shift+a는 `A`. 종료는 2번째 터미널에서 `docker kill kfs2-run`.

- [ ] **Step 8: Commit**

```bash
git add include/keyboard.h src/drivers/keyboard.c src/kernel/selftest.c src/kernel/kernel.c
git commit -F - <<'MSG'
feat: polling PS/2 keyboard driver

The bonus assumes a working keyboard; KFS-1 skipped that bonus, so this is
where it gets built. Polling, not IRQ1: the interrupt path drags in an IDT
and PIC remapping, which is KFS-4's subject, and getting it wrong triple
faults a mandatory part that already passes.

keyboard_poll() returns 0 when nothing is waiting rather than blocking, so
code that is not the shell can ask for a key too, and so the signature
survives being swapped for an interrupt-driven ring buffer later.

Three details that are easy to get wrong and are handled here: the 0xE0
prefix consumes its second byte (dropping only the prefix leaks that byte
as a stray character, and the two can arrive on different polls, so the
state lives in the driver); keyboard_init drains the 8042 so a byte GRUB
left behind does not become a phantom keystroke; and translation is two
128-byte tables rather than a shift rule.

kernel_main gets a temporary echo loop so the next task's sendkey probe has
something that visibly reacts. Task 4 replaces it with the shell.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Fgnb5AiTd3EdViysQoWcfc
MSG
```

---

### Task 2: `sendkey` 실현 가능성 확인 + boot_test 키 주입

**Files:**
- Modify: `tests/boot_test.sh` (11-13행 부근의 `DUMP=$(...)` 파이프라인)
- Modify: `Makefile` (컨테이너 레이어에 `test-shell` 타깃 추가, `.PHONY` 갱신)

**Interfaces:**
- Consumes: Task 1의 에코 루프.
- Produces: `KEYS` 환경변수 — 공백으로 구분된 QEMU `sendkey` 인자 목록. 비어 있으면 기존 동작과 완전히 동일하다. 이후 Task가 `make test-shell`로 셸을 검증한다.

**이 Task는 게이트다.** 스펙 §10·§12가 `sendkey`의 `-display none` 동작을 **미검증 가정**으로 표시했다. Step 3에서 실패하면 Step 6으로 가서 되돌리고 계층 2를 포기한다. mandatory는 어느 쪽이든 영향이 없다.

- [ ] **Step 1: `tests/boot_test.sh`에 키 주입 추가**

`ISO=`/`BOOT_WAIT=` 아래에 `KEYS` 기본값을 추가하고, `DUMP=$(...)` 한 덩어리를 함수로 바꾼다. 기존 파이프라인을 다음으로 **교체**한다.

```sh
ISO=${ISO:-kfs.iso}
BOOT_WAIT=${BOOT_WAIT:-6}
KEYS=${KEYS:-}

# Monitor script: wait for boot, optionally type, then dump the text buffer.
# Each sendkey is one press+release; the small sleep lets the polling kernel
# drain the 8042 between keys.
monitor_script() {
    sleep "$BOOT_WAIT"
    if [ -n "$KEYS" ]; then
        for k in $KEYS; do
            echo "sendkey $k"
            sleep 0.1
        done
        sleep 1
    fi
    echo 'xp /4000bx 0xb8000'
    sleep 1
    echo quit
}

DUMP=$(monitor_script \
    | qemu-system-i386 -cdrom "$ISO" -display none -monitor stdio -no-reboot 2>/dev/null)
```

나머지(`SCREEN=` 파이프라인, assertion 루프, `SELFTEST FAIL` 검사, `exit $status`)는 **손대지 않는다**.

- [ ] **Step 2: 무입력 회귀 확인**

Run: `make test`
Expected: 기존 12 assertion 전부 통과. `KEYS`가 비어 있으므로 동작이 이전과 같아야 한다.

- [ ] **Step 3: `sendkey` 프로브 — 이 Task의 게이트**

Run:
```bash
docker run --rm --platform linux/amd64 -v "$PWD":/kfs -w /kfs kfs2-build \
  sh -c 'KEYS="h i" sh tests/boot_test.sh "hi"'
```
Expected: 디코딩된 화면 마지막에 `hi`가 보이고 `OK   found: hi`.

**실패하면**(키가 안 들어가거나 화면이 안 바뀜) `BOOT_WAIT=12`로 한 번 더 시도한다. 그래도 실패하면 Step 6으로 간다.

- [ ] **Step 4: `Makefile`에 `test-shell` 타깃 추가**

컨테이너 레이어의 `test:` 규칙 **아래**에 다음을 추가한다.

```make
# Types a command into the shell and checks what it printed. Separate target
# so `make test` stays the untouched mandatory regression net.
test-shell: $(ISO)
	KEYS="h i" sh tests/boot_test.sh "hi"
```

같은 파일 컨테이너 레이어의 `.PHONY: all run test` 줄을 `.PHONY: all run test test-shell`로 바꾼다.

호스트 레이어에도 위임 타깃이 필요하다. 호스트 레이어의 `test:` 규칙 아래에 추가한다.

```make
test-shell: image
	$(RUN) $(IMAGE) make test-shell IN_CONTAINER=1
```

호스트 레이어 위쪽의 `.PHONY: all run test shell image clean fclean re`를
`.PHONY: all run test test-shell shell image clean fclean re`로 바꾼다.

- [ ] **Step 5: 두 테스트 모두 확인**

Run: `make test && make test-shell`
Expected: 앞은 12 assertion 통과, 뒤는 `OK   found: hi`.

- [ ] **Step 6: (Step 3 실패 시에만) 되돌리고 계층 2 포기**

```bash
git checkout -- tests/boot_test.sh Makefile
```
스펙 §10의 계층 2 항목과 §12의 미검증 가정을 "실측 결과 불가 — 계층 1 + 수동 시연으로 대체"로 고쳐 커밋한다. 이후 Task들에서 `make test-shell` 관련 스텝은 전부 건너뛰고, 대신 `make run`으로 손으로 확인한다. **나머지 Task의 코드 내용은 바뀌지 않는다.**

- [ ] **Step 7: Commit** (Step 3이 성공한 경우)

```bash
git add tests/boot_test.sh Makefile
git commit -F - <<'MSG'
test: inject keystrokes through the QEMU monitor

The boot test was one-shot — boot, dump the text buffer, quit — so nothing
that needs input could be checked. `sendkey` through the same monitor pipe
fixes that, and it works under -display none because the 8042 is emulated
regardless of whether anything draws the screen (verified, not assumed:
the design doc had this flagged as an open question).

KEYS is empty by default, so `make test` behaves exactly as before and
stays the mandatory regression net. Shell checks go in `make test-shell`,
a separate target, so a failure says which of the two broke.

The 0.1s between keys lets the polling kernel drain the controller; without
interrupts there is no buffering beyond the 8042's own.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Fgnb5AiTd3EdViysQoWcfc
MSG
```

---

### Task 3: 수 파싱 + 토크나이저 (순수 함수)

**Files:**
- Create: `include/shell.h`
- Create: `src/shell/shell.c`
- Modify: `src/kernel/selftest.c` (`test_shell` 추가, `selftest_run`에서 호출)

**Interfaces:**
- Consumes: `types.h`의 `uint32_t`, `libk.h`의 `strcmp`/`memcpy`(selftest에서).
- Produces:
  - `int shell_parse_u32(const char *s, uint32_t *out)` — 성공 `0`, 실패 `-1`. `0x`/`0X` 접두면 16진수, 아니면 10진수.
  - `int shell_tokenize(char *line, char **argv, int max)` — `line`을 제자리에서 자르고 `argv`를 채운다. 반환값은 argc.
  - `void shell_run(void)` — 이 Task에서는 **선언만** 하고 Task 4에서 구현한다.

이 Task는 화면에 아무것도 찍지 않는 순수 로직만 다룬다. 스펙 §10 계층 1의 근거는 화면 공간이 아니라 **계층 분리**다 — 파서 검증에 출력이 섞이면 실패했을 때 파싱이 틀린 건지 출력이 틀린 건지 구분되지 않는다.

- [ ] **Step 1: `include/shell.h` 작성**

```c
#ifndef SHELL_H
# define SHELL_H

# include "types.h"

void shell_run(void);   /* does not return */

/* Exposed (not static) so the on-boot selftest can exercise them without
 * producing any screen output. See the design spec, section 10. */
int shell_parse_u32(const char *s, uint32_t *out);   /* 0 ok, -1 malformed */
int shell_tokenize(char *line, char **argv, int max); /* returns argc */

#endif
```

- [ ] **Step 2: selftest에 검사 추가 (RED)**

`src/kernel/selftest.c`에 `#include "shell.h"`를 추가하고, `test_keyboard` 아래에 넣는다.

```c
/* Pure parsing: no hardware, no screen output. Silent on success. */
static void test_shell(void)
{
	char     buf[32];
	char    *argv[3];
	uint32_t v = 0;

	check(shell_parse_u32("0x1234", &v) == 0 && v == 0x1234, "parse hex");
	check(shell_parse_u32("64", &v) == 0 && v == 64, "parse dec");
	check(shell_parse_u32("0xFFFFFFFF", &v) == 0 && v == 0xFFFFFFFFu,
		"parse hex max");
	check(shell_parse_u32("0x100000000", &v) == -1, "parse overflow");
	check(shell_parse_u32("12g", &v) == -1, "parse junk");
	check(shell_parse_u32("", &v) == -1, "parse empty");
	check(shell_parse_u32("0x", &v) == -1, "parse bare 0x");

	memcpy(buf, "  dump   0x10  20  ", 20);
	check(shell_tokenize(buf, argv, 3) == 3
		&& strcmp(argv[0], "dump") == 0
		&& strcmp(argv[1], "0x10") == 0
		&& strcmp(argv[2], "20") == 0, "tokenize spaces");
	memcpy(buf, "", 1);
	check(shell_tokenize(buf, argv, 3) == 0, "tokenize empty");
}
```

`selftest_run`의 `test_keyboard();` 다음에 `test_shell();`을 추가한다.

- [ ] **Step 3: 링크가 실패하는지 확인 (RED)**

Run: `make test`
Expected: **실패해야 정상.** `undefined reference to 'shell_parse_u32'` 및 `shell_tokenize` 링크 에러. 아직 구현이 없다.

- [ ] **Step 4: `src/shell/shell.c` 작성 (GREEN)**

```c
#include "shell.h"
#include "libk.h"

/* Accumulate with an overflow guard: v * base + d must stay inside 32 bits,
 * which is exactly v <= (UINT32_MAX - d) / base. Checking after the multiply
 * would already have wrapped. */
int shell_parse_u32(const char *s, uint32_t *out)
{
	uint32_t base = 10;
	uint32_t v = 0;
	uint32_t d;
	int      any = 0;

	if (s == NULL || *s == '\0')
		return -1;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
		s += 2;
	}
	while (*s) {
		if (*s >= '0' && *s <= '9')
			d = (uint32_t)(*s - '0');
		else if (base == 16 && *s >= 'a' && *s <= 'f')
			d = (uint32_t)(*s - 'a' + 10);
		else if (base == 16 && *s >= 'A' && *s <= 'F')
			d = (uint32_t)(*s - 'A' + 10);
		else
			return -1;
		if (v > (0xFFFFFFFFu - d) / base)
			return -1;
		v = v * base + d;
		any = 1;
		s++;
	}
	if (!any)          /* "0x" with no digits after it */
		return -1;
	*out = v;
	return 0;
}

/* In-place: spaces become NULs and argv points into the caller's buffer.
 * Stops at max tokens; anything after that is left unparsed. */
int shell_tokenize(char *line, char **argv, int max)
{
	int argc = 0;

	while (*line) {
		while (*line == ' ')
			*line++ = '\0';
		if (*line == '\0')
			break;
		if (argc == max)
			break;
		argv[argc++] = line;
		while (*line && *line != ' ')
			line++;
	}
	return argc;
}
```

- [ ] **Step 5: 통과 확인 (GREEN)**

Run: `make test`
Expected: 경고 0, **기존 12 assertion 전부 통과**. 새 검사 9개는 조용히 통과한다.

`shell_run`은 아직 정의되지 않았지만 아무도 호출하지 않으므로 링크는 성공한다.

- [ ] **Step 6: Commit**

```bash
git add include/shell.h src/shell/shell.c src/kernel/selftest.c
git commit -F - <<'MSG'
feat: shell number parsing and tokenizer

Both are pure — no hardware, no screen — so the on-boot selftest can check
them directly. That is the point of doing them first: a parser bug and an
output bug look the same once they are tangled, and this way a failure can
only be the parser.

Neither is static, which the design doc originally called for, because the
selftest has to reach them. The header says why.

parse_u32 guards the accumulator before the multiply, not after: checking
v * base + d for overflow once it has already wrapped proves nothing. "0x"
with no digits behind it is rejected rather than read as zero.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Fgnb5AiTd3EdViysQoWcfc
MSG
```

---

### Task 4: 셸 골격 — `\b`, 프롬프트, 라인 편집, 디스패치

**Files:**
- Modify: `src/drivers/vga.c` (`vga_putchar`에 `'\b'` 분기)
- Modify: `src/shell/shell.c` (명령 테이블 + `shell_exec` + `shell_run`)
- Modify: `src/kernel/kernel.c` (임시 에코 → `shell_run`)
- Modify: `Makefile` (`test-shell` assertion 교체)

**Interfaces:**
- Consumes: Task 1의 `keyboard_init`/`keyboard_poll`, Task 3의 `shell_parse_u32`/`shell_tokenize`.
- Produces: `void shell_run(void)` — 프롬프트 `kfs> `를 찍고 반환하지 않는다. 이 Task에서는 명령이 `help` 하나뿐이며 Task 5가 나머지 6개를 채운다.

- [ ] **Step 1: `vga_putchar`에 `'\b'` 추가**

`src/drivers/vga.c`의 `vga_putchar`에서 `'\r'` 분기 **다음에** 넣는다.

```c
	} else if (c == '\b') {
		/* Cursor only, like a terminal: erasing is the caller's job
		 * ("\b \b"). Keeps this driver free of an erase policy. */
		if (g_col > 0)
			g_col--;
	} else if (c == '\t') {
```

`g_col`이 줄어들기만 하므로 함수 끝의 `g_col >= VGA_WIDTH` 검사에는 영향이 없다.

- [ ] **Step 2: `src/shell/shell.c`에 골격 추가**

파일 맨 위 `#include "libk.h"` 아래에 헤더를 추가한다.

```c
#include "keyboard.h"
#include "vga.h"
#include "printk.h"
```

그리고 `shell_tokenize` 정의 **아래**에 다음을 추가한다.

```c
#define LINE_MAX 128u
#define ARGV_MAX 3

struct cmd {
	const char *name;
	const char *help;
	void (*fn)(int argc, char **argv);
};

/* Forward-declare the handlers so the table can name them, then define the
 * table, then the handlers. cmd_help walks the table, so the two reference
 * each other; this ordering resolves that without a tentative definition of
 * a const array (which is not valid C). Task 5 extends all three blocks. */
static void cmd_help(int argc, char **argv);

/* One row per command: adding one in KFS-3 is a single line here. */
static const struct cmd g_cmds[] = {
	{ "help", "list commands", cmd_help },
	{ NULL, NULL, NULL }
};

static void cmd_help(int argc, char **argv)
{
	const struct cmd *c;

	(void)argc;
	(void)argv;
	for (c = g_cmds; c->name; c++)
		printk("  %s -- %s\n", c->name, c->help);
}

static void shell_exec(char *line, char **argv)
{
	const struct cmd *c;
	int argc;

	argc = shell_tokenize(line, argv, ARGV_MAX);
	if (argc == 0)
		return;
	for (c = g_cmds; c->name; c++) {
		if (strcmp(c->name, argv[0]) == 0) {
			c->fn(argc, argv);
			return;
		}
	}
	printk("unknown command: %s (try 'help')\n", argv[0]);
}

void shell_run(void)
{
	char   line[LINE_MAX];
	char  *argv[ARGV_MAX];
	size_t len = 0;
	char   c;

	printk("kfs> ");
	for (;;) {
		c = keyboard_poll();
		if (c == 0) {
			/* No hlt here: hlt only wakes on an interrupt, and with
			 * IF=0 and no IDT it would never wake. Polling costs a
			 * busy CPU; pause is the standard spin-loop hint. */
			__asm__ volatile ("pause");
			continue;
		}
		if (c == '\n') {
			vga_putchar('\n');
			line[len] = '\0';
			shell_exec(line, argv);
			len = 0;
			printk("kfs> ");
		} else if (c == '\b') {
			if (len > 0) {
				len--;
				vga_puts("\b \b");
			}
		} else if (len + 1 < LINE_MAX) {
			line[len++] = c;
			vga_putchar(c);
		}
	}
}
```

`static const struct cmd g_cmds[];`의 전방 선언이 필요한 이유: `cmd_help`가 표를 순회하는데 표는 `cmd_help`의 주소를 담으므로 서로를 참조한다.

- [ ] **Step 3: `kernel_main`의 임시 에코를 교체**

`src/kernel/kernel.c`에서 `#include "keyboard.h"` 아래에 `#include "shell.h"`를 추가하고, Task 1 Step 5에서 넣은 임시 블록 전체를 다음으로 **교체**한다.

```c
	keyboard_init();
	shell_run();   /* does not return */
```

- [ ] **Step 4: `test-shell` assertion 교체**

`Makefile`의 `test-shell` 규칙을 다음으로 바꾼다.

```make
test-shell: $(ISO)
	KEYS="h e l p ret" sh tests/boot_test.sh \
	    "help -- list commands" "kfs>"
```

- [ ] **Step 5: 회귀 + 셸 확인**

Run: `make test`
Expected: 경고 0, 기존 12 assertion 전부 통과. 프롬프트 한 줄이 늘지만 스펙 §9대로 밀려나는 것은 SCRL 한 줄뿐이다.

Run: `make test-shell`
Expected: `OK   found: help -- list commands`, `OK   found: kfs>`.

- [ ] **Step 6: 손으로 확인 (선택)**

Run: `make run`
Expected: `kfs> ` 프롬프트. `helo` 입력 후 백스페이스로 `o`를 지우고 `p`를 쳐서 `help` 완성 → Enter → 명령 목록. 없는 명령은 `unknown command: ...`.

- [ ] **Step 7: Commit**

```bash
git add src/drivers/vga.c src/shell/shell.c src/kernel/kernel.c Makefile
git commit -F - <<'MSG'
feat: shell skeleton — prompt, line editing, command dispatch

vga_putchar learns '\b' as a pure cursor move, not an erase. Erasing is
"\b \b" and belongs to the caller; a driver that decides to blank a cell on
backspace has taken a policy decision that the shell should own. At column
zero it does nothing rather than wrapping to the previous row, and the
shell's len > 0 check is the second guard against eating the prompt.

The command table is a name/help/function-pointer array walked by strcmp.
Only `help` is in it here; the next task fills the rest. The shape is the
point: adding a command in KFS-3 should be one row.

The wait loop uses pause, not hlt. hlt only wakes on an interrupt, and with
IF=0 and no IDT it would never wake — the shell would hang on the first
keystroke. A busy loop is what polling actually costs.

kernel_main no longer returns, so boot.asm's .hang stops being the normal
exit path and goes back to being a safety net for the PANIC path and the
`halt` command. The defense notes need that correction.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Fgnb5AiTd3EdViysQoWcfc
MSG
```

---

### Task 5: 명령 6종

**Files:**
- Modify: `src/shell/shell.c` (명령 함수 6개 + 표 6행)
- Modify: `Makefile` (`test-shell` assertion을 `gdt`로 교체)

**Interfaces:**
- Consumes: `kernel.h`의 `print_kernel_stack()`·`dump_hex(const void *, uint32_t)`, `vga.h`의 `vga_clear()`, `io.h`의 `outb`, Task 3의 `shell_parse_u32`.
- Produces: 명령 7종을 갖춘 완성된 셸. 이후 Task는 문서화와 제출물만 다룬다.

- [ ] **Step 1: `test-shell` assertion을 먼저 바꾼다 (RED)**

`Makefile`의 `test-shell` 규칙을 다음으로 교체한다.

```make
test-shell: $(ISO)
	KEYS="g d t ret" sh tests/boot_test.sh \
	    "ff ff 00 00 00 9a cf 00" "kfs>"
```

기대 문자열은 GDT 엔트리 1(커널 코드 디스크립터)의 원시 바이트다. `gdt.c`가 쓴 값이고 `test_gdt`가 이미 `memcmp`로 확인 중이라 결정적이다. `dump_hex`의 한 행은 76칸이므로 80칸 규칙을 만족한다.

- [ ] **Step 2: 테스트가 실패하는지 확인 (RED)**

Run: `make test-shell`
Expected: **실패해야 정상.** `gdt` 명령이 없으므로 화면에는 `unknown command: gdt (try 'help')`만 있고 `FAIL missing: ff ff 00 00 00 9a cf 00`.

- [ ] **Step 3: 명령 함수 6개 구현 (GREEN)**

`src/shell/shell.c` 상단의 include에 다음을 추가한다.

```c
#include "kernel.h"
#include "io.h"
```

`cmd_help`의 전방 선언 **아래**에 나머지 6개의 전방 선언을 추가한다.

```c
static void cmd_stack(int argc, char **argv);
static void cmd_gdt(int argc, char **argv);
static void cmd_dump(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_halt(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);
```

그리고 `cmd_help` **정의 아래**에 구현 6개를 넣는다.

```c
static void cmd_stack(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	print_kernel_stack();
}

static void cmd_gdt(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	/* 7 entries * 8 bytes, at the address the subject mandates. */
	dump_hex((const void *)0x00000800, 56);
}

static void cmd_dump(int argc, char **argv)
{
	uint32_t addr;
	uint32_t len = 64;

	if (argc < 2) {
		printk("usage: dump <addr> [len]\n");
		return;
	}
	if (shell_parse_u32(argv[1], &addr) != 0) {
		printk("dump: bad address\n");
		return;
	}
	if (argc >= 3 && shell_parse_u32(argv[2], &len) != 0) {
		printk("dump: bad length\n");
		return;
	}
	/* No validity check on purpose: showing whatever is at an address is
	 * what a debugger is for. Without paging nothing faults anyway. */
	dump_hex((const void *)(uintptr_t)addr, len);
}

static void cmd_clear(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	vga_clear();
}

static void cmd_halt(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	printk("halted.\n");
	__asm__ volatile ("cli");
	for (;;)
		__asm__ volatile ("hlt");
}

static void cmd_reboot(int argc, char **argv)
{
	uint32_t i;

	(void)argc;
	(void)argv;
	printk("rebooting...\n");
	outb(0x64, 0xFE);           /* pulse the 8042 reset line */
	for (i = 0; i < 10000000u; i++)
		__asm__ volatile ("pause");
	/* Never leave the machine in an undefined state if the pulse did
	 * nothing: say so and stop. */
	printk("reboot failed; halting\n");
	__asm__ volatile ("cli");
	for (;;)
		__asm__ volatile ("hlt");
}
```

`g_cmds` 표를 다음으로 교체한다.

```c
static const struct cmd g_cmds[] = {
	{ "help",   "list commands",                  cmd_help   },
	{ "stack",  "hexdump the kernel stack",       cmd_stack  },
	{ "gdt",    "hexdump the GDT at 0x800",       cmd_gdt    },
	{ "dump",   "dump <addr> [len], len 64",      cmd_dump   },
	{ "clear",  "clear the screen",               cmd_clear  },
	{ "halt",   "stop the CPU",                   cmd_halt   },
	{ "reboot", "reset via the 8042",             cmd_reboot },
	{ NULL, NULL, NULL }
};
```

- [ ] **Step 4: 통과 확인 (GREEN)**

Run: `make test-shell`
Expected: `OK   found: ff ff 00 00 00 9a cf 00`, `OK   found: kfs>`.

- [ ] **Step 5: 회귀 확인**

Run: `make test`
Expected: 경고 0, 기존 12 assertion 전부 통과.

- [ ] **Step 6: 손으로 확인 (선택)**

Run: `make run`
Expected: `help`가 7줄. `clear` 후 `stack`을 치면 깨끗한 화면에 스택 덤프. `dump 0x100000 32`가 커널 `.text` 앞부분을 보여준다. `dump zzz`는 `dump: bad address`. `reboot`은 QEMU가 재시작(또는 `-no-reboot`이면 종료)한다.

**`reboot`은 자동 테스트에 넣지 않는다** — QEMU가 종료해 VGA 덤프를 잃는다.

- [ ] **Step 7: Commit**

```bash
git add src/shell/shell.c Makefile
git commit -F - <<'MSG'
feat: shell commands — stack, gdt, dump, clear, halt, reboot

Six of the seven are one line each on top of what the mandatory part
already built: print_kernel_stack, dump_hex, vga_clear. Only `dump` has new
logic, and only because it takes arguments.

`dump` deliberately does not validate the address. Showing whatever sits at
an address is the entire job of a debugger, and without paging nothing
faults regardless. When KFS-3 turns paging on, a bad address will fault —
and catching that fault is that project's subject, not something to
pre-empt with a range check here.

`reboot` pulses the 8042 reset line and then halts with a message rather
than spinning silently, so a machine where the pulse does nothing does not
look hung. It stays out of the automated test: QEMU exits under -no-reboot
and the screen dump is lost with it.

The test types `gdt` and checks for the kernel code descriptor's raw bytes.
That value is deterministic — gdt.c writes it and test_gdt already memcmps
it — so the assertion cannot pass by accident.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Fgnb5AiTd3EdViysQoWcfc
MSG
```

---

### Task 6: README + 최종 검증 + 턴인 ISO

**Files:**
- Modify: `README.md`
- Modify: `kfs.iso` (재빌드 후 커밋)

**Interfaces:**
- Consumes: Task 1~5 전체.
- Produces: 제출 가능한 상태.

- [ ] **Step 1: README에 셸 절 추가**

`## Layout` 절의 코드 블록에서 `src/lib/printk.c` 줄 **아래**에 두 줄을 추가한다.

```
src/drivers/keyboard.c polling PS/2 keyboard (scancode set 1, no IRQ)
src/shell/shell.c      debug shell: line editing, tokenizer, 7 commands
```

그리고 `## How the boot works` 절 **뒤에** 새 절을 추가한다.

````markdown
## Shell (bonus)

After the boot banners the kernel drops into a polling debug shell:

```
kfs> help
  help -- list commands
  stack -- hexdump the kernel stack
  gdt -- hexdump the GDT at 0x800
  dump -- dump <addr> [len], len 64
  clear -- clear the screen
  halt -- stop the CPU
  reboot -- reset via the 8042
```

`dump` takes `0x`-prefixed hex or plain decimal, so `dump 0x100000 32`
shows the first 32 bytes of the kernel image. There is no IDT yet, so the
keyboard is polled rather than interrupt-driven: `keyboard_poll()` returns
0 when the 8042 has nothing waiting. That costs a busy CPU while the shell
idles — `hlt` cannot be used, because with interrupts masked and no IDT it
would never wake.

`make test` boots without typing anything and checks the mandatory output.
`make test-shell` types a command through the QEMU monitor and checks what
the shell printed.
````

- [ ] **Step 2: 클린 재빌드 + 두 테스트**

Run: `make fclean && make && make test && make test-shell`
Expected: 경고 0, `make test` 12 assertion 통과, `make test-shell` 2 assertion 통과, ISO ≤ 10 MiB.

- [ ] **Step 3: 로드 섹션 확인 (회귀)**

Run:
```bash
python3 -c "
import struct
d=open('kernel.bin','rb').read()
o,=struct.unpack_from('<I',d,0x20); e,n,x=struct.unpack_from('<HHH',d,0x2E)
s,=struct.unpack_from('<I',d,o+x*e+0x10)
for i in range(n):
    b=o+i*e; nm,t,f,a,_,sz=struct.unpack_from('<IIIIII',d,b)
    if f&2: print(d[s+nm:d.index(b'\0',s+nm)].decode(), hex(a), sz)
"
```
Expected: `.text` / `.rodata` / `.bss` **정확히 3개.** 4개가 나오면 `.eh_frame`이 되살아난 것이므로 `linker.ld`의 `/DISCARD/`를 확인한다.

- [ ] **Step 4: 제출물 위생 확인**

Run: `git ls-files | grep -E '\.o$|\.d$|^isodir/|kernel\.bin|\.pdf$|\.env'`
Expected: 출력 없음.

- [ ] **Step 5: 턴인 ISO 재빌드 후 커밋**

셸이 들어가면서 커널이 바뀌었으므로 커밋된 ISO는 더 이상 현재 소스의 산출물이 아니다.

```bash
make re
make test && make test-shell
git add README.md
git add -f kfs.iso
git status   # README.md와 kfs.iso 둘만 스테이징됐는지 확인
git commit -F - <<'MSG'
docs: shell usage; rebuild turn-in image with the bonus

The committed ISO predates the keyboard and shell, so it is no longer the
artifact this source produces — the one thing an evaluator can check with
`make re`. Rebuilt clean and verified with both targets.

README documents why the keyboard is polled and what that costs, since it
is the first question the design invites.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Fgnb5AiTd3EdViysQoWcfc
MSG
```

- [ ] **Step 6: 최종 상태 확인**

Run: `git status --porcelain && make test && make test-shell`
Expected: 워킹 트리 clean, 두 테스트 모두 통과. clean이면 커밋된 ISO가 커밋된 소스의 산출물임이 증명된다.

---

## 부록 A: 과제 요구사항 ↔ Task 매핑

| `kfs2.pdf` VI장 | 구현 위치 |
|---|---|
| 키보드 입력을 받을 것 (전제) | Task 1 `keyboard.c` |
| 미니멀 셸 (POSIX 아님) | Task 4 `shell_run` + Task 5 명령 6종 |
| print-kernel-stack을 셸에 | Task 5 `cmd_stack` → 기존 `print_kernel_stack()` |
| `reboot` | Task 5 `cmd_reboot` (8042 `0xFE`) |
| `halt` | Task 5 `cmd_halt` |
| mandatory PERFECT 유지 | 모든 Task의 `make test` 12 assertion |

## 부록 B: 트러블슈팅

- **`make test`에서 `" 42 "`가 사라짐**: 셸이 화면에 뭔가를 더 찍고 있다. 시작 배너를 넣었는지 확인(스펙 §6.1은 넣지 않기로 함). 대응은 셸 쪽에서 하고 mandatory는 조정하지 않는다.
- **`sendkey` 후 화면이 안 바뀜**: `BOOT_WAIT=12 make test-shell`로 재시도. Apple Silicon(Rosetta+TCG)에서 부팅이 느려 6초 안에 셸에 도달하지 못했을 수 있다.
- **타이핑하면 글자가 두 번 찍힘**: `keyboard_poll`이 break 코드를 make로 처리하고 있다. `sc & 0x80` 검사가 변환표 조회보다 앞에 있는지 확인.
- **화살표를 누르면 엉뚱한 글자**: `0xE0` 다음 바이트를 소비하지 않고 있다. `g_extended` 플래그 경로를 확인.
- **셸이 시작하자마자 글자 하나가 찍힘**: `keyboard_init`의 배출 루프가 빠졌다.
- **`undefined reference to 'shell_run'`**: Task 3에서 선언만 하고 Task 4에서 정의한다. Task 3 시점에는 아무도 호출하지 않아야 한다.
- **`-Werror=unused-parameter`**: 명령 함수에서 `(void)argc; (void)argv;`가 빠졌다.
