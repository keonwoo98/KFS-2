# KFS-2 GDT & Stack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 커널 소유의 7엔트리 GDT를 물리 0x00000800에 설치·lgdt하고, printk 기반 커널 스택 hexdump 도구를 추가한다.

**Architecture:** KFS-1 뼈대(이식 완료, `make test` 통과 기준선) 위에 4개 증분: printk 폭 지정 확장 → GDT 모듈(C 구축 + asm flush) → selftest GDT 검증 → 스택 덤프 도구. 각 증분은 QEMU 부팅 테스트(`make test`)로 검증한다.

**Tech Stack:** C(i686-linux-gnu-gcc, freestanding) + NASM(elf32), 자작 linker.ld, GRUB multiboot v1, QEMU 자동 테스트(컨테이너 내), Docker/Podman.

**Spec:** `docs/superpowers/specs/2026-07-03-kfs2-design.md` (승인됨)

## Global Constraints

- 모든 빌드·테스트는 컨테이너 안: 호스트에서 `make test`만 실행(내부적으로 docker 위임). 클러스터는 `make CONTAINER=podman test`. 호스트 cc/ld 직접 실행 금지.
- 이미지 태그 `kfs2-build`, 플랫폼 `linux/amd64` 고정 (Makefile에 반영됨 — 변경 금지).
- CFLAGS 고정: `-ffreestanding -fno-builtin -fno-stack-protector -fno-pie -nostdlib -nodefaultlibs -Wall -Wextra -Werror -O2 -g -Iinclude -MMD -MP` — warning 0개 필수(-Werror).
- `linker.ld` 수정 금지 (GDT는 런타임 설치 — GRUB이 1MiB 미만 로드를 거부하므로).
- GDT 고정값: base `0x00000800`, null 포함 7엔트리(56바이트), GDTR limit 55(0x37). 셀렉터: KCode 0x08 / KData 0x10 / KStack 0x18 / UCode 0x20 / UData 0x28 / UStack 0x30.
- boot_test 기대 문자열은 화면 한 행(80칸) 안에 들어가야 함. assert 문자열은 selftest의 scroll_exercise **이후** 출력만 사용.
- kernel_main 출력 순서 고정: vga_init → magic 검증 → gdt_init → selftest(scroll → mem/str → gdt) → multiboot 배너 → ptest → 스택 덤프 → "42".
- `*.o`, `*.d`, `kernel.bin`, `isodir/`, `*.iso` 커밋 금지 (.gitignore가 제외; 최종 턴인 시 kfs.iso만 사용자 승인 하에 `git add -f` 예외).
- 커밋은 사용자가 태스크별 커밋을 승인한 경우에만 수행. 커밋 메시지 트레일러: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`. push 금지.
- Makefile은 `find src`로 소스 자동 수집 — 새 .c/.asm 파일은 빌드 규칙 수정 없이 포함된다.
- 소스 파일은 탭 들여쓰기, 블록 선두 변수 선언, 영어 주석(기존 코드 스타일 유지). 헤더는 `#ifndef` 가드 + `# define`/`# include` 스타일(kernel.h 참조).

---

### Task 1: printk 폭 지정 확장 (%08x, %02x)

**Files:**
- Modify: `src/lib/printk.c`
- Modify: `src/kernel/kernel.c:17-18` (ptest 데모 줄)
- Modify: `Makefile:88-90` (test 기대 문자열)
- Test: `make test` (QEMU 부팅 assert)

**Interfaces:**
- Consumes: `vga_putchar/vga_puts` (vga.h), `va_list` (types.h) — 기존 그대로
- Produces: `printk`가 `%[0][폭]d|u|x`를 지원 (시그니처 `void printk(const char *fmt, ...)` 불변). Task 4의 hexdump가 `%08x`/`%02x`에 의존.

- [ ] **Step 1: RED — 데모 줄과 기대 문자열을 먼저 확장**

`src/kernel/kernel.c`의 ptest 줄(17-18행)을 다음으로 교체:

```c
	printk("ptest [%c|%s|%d|%d|%u|%x|%%|%08x|%02x]\n",
		'X', "str", -42, -2147483647 - 1, 4294967295u, 0xdeadbeef,
		0xc0ffee, 0xf);
```

`Makefile`의 컨테이너 계층 test 타깃에서 ptest 기대 문자열을 교체 (탭 들여쓰기 유지):

```make
test: $(ISO)
	sh tests/boot_test.sh " 42 " "selftest ok" "2badb002" \
		"ptest [X|str|-42|-2147483648|4294967295|deadbeef|%|00c0ffee|0f]" \
		"SCRL29" "!SCRL00" "!Booting" "!PANIC"
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `make test`
Expected: 빌드는 성공("%08x"는 유효한 printf 문법이라 -Wformat 경고 없음), 부팅 후
`FAIL missing: ptest [X|str|-42|-2147483648|4294967295|deadbeef|%|00c0ffee|0f]`
가 출력되고 make가 Error 1로 종료. (현재 파서는 `%0`을 미지원 지시자로 처리해 화면에 `%08x`가 리터럴로 남는다.)

- [ ] **Step 3: GREEN — printk.c에 폭 파싱 구현**

`src/lib/printk.c` 전체를 다음으로 교체:

```c
#include "printk.h"
#include "vga.h"
#include "types.h"

/* Width is emitted as leading pad characters, so buf only ever holds
 * the digits of a 32-bit value (max 10 for base 10) — no overflow. */
static void print_unsigned(uint32_t n, uint32_t base, int width, char pad)
{
	static const char digits[] = "0123456789abcdef";
	char   buf[32];
	int    i = 0;

	if (n == 0)
		buf[i++] = '0';
	while (n > 0) {
		buf[i++] = digits[n % base];
		n /= base;
	}
	while (width > i) {
		vga_putchar(pad);
		width--;
	}
	while (i-- > 0)
		vga_putchar(buf[i]);
}

static void print_signed(int32_t v, int width, char pad)
{
	uint32_t u;

	if (v < 0) {
		vga_putchar('-');
		u = -(uint32_t)v; /* unsigned negation: INT_MIN-safe */
		if (width > 0)
			width--;   /* '-' counts toward the field width */
	} else {
		u = (uint32_t)v;
	}
	print_unsigned(u, 10, width, pad);
}

void printk(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	while (*fmt) {
		char pad;
		int  width;

		if (*fmt != '%') {
			vga_putchar(*fmt++);
			continue;
		}
		fmt++; /* skip '%' */
		pad = ' ';
		width = 0;
		if (*fmt == '0') {
			pad = '0';
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (*fmt - '0');
			fmt++;
		}
		if (width > 31)
			width = 31;
		if (*fmt == '\0')
			break;
		if (*fmt == 'c') {
			/* char is promoted to int through varargs; width ignored */
			vga_putchar((char)va_arg(ap, int));
		} else if (*fmt == 's') {
			const char *s = va_arg(ap, const char *);

			vga_puts(s ? s : "(null)"); /* width ignored for %s */
		} else if (*fmt == 'd') {
			print_signed(va_arg(ap, int32_t), width, pad);
		} else if (*fmt == 'u') {
			print_unsigned(va_arg(ap, uint32_t), 10, width, pad);
		} else if (*fmt == 'x') {
			print_unsigned(va_arg(ap, uint32_t), 16, width, pad);
		} else if (*fmt == '%') {
			vga_putchar('%');
		} else {
			/* unsupported directive: echo it (width chars are lost) */
			vga_putchar('%');
			vga_putchar(*fmt);
		}
		fmt++;
	}
	va_end(ap);
}
```

동작 노트: 폭 미지정 시 `width=0`이라 기존 출력과 완전 동일(`%x`의 "2badb002",
`%d`의 "-2147483648" 회귀 없음 — 같은 테스트의 기존 assert가 이를 검증).
`n==0`이 buf 경로로 통합되어 `%02x`에 0을 주면 "00"이 나온다.

- [ ] **Step 4: 테스트 통과 확인**

Run: `make test`
Expected: 모든 assert OK — 특히
`OK   found: ptest [X|str|-42|-2147483648|4294967295|deadbeef|%|00c0ffee|0f]`
와 기존 `OK   found: 2badb002` (무패딩 %x 회귀 없음), `OK   iso size: ...`.

- [ ] **Step 5: Commit** (사용자가 태스크별 커밋을 승인한 경우)

```bash
git add src/lib/printk.c src/kernel/kernel.c Makefile
git commit -m "feat: printk zero-pad width support (%08x, %02x)"
```

---

### Task 2: GDT 모듈 — 0x800 구축 + lgdt + 세그먼트 리로드

**Files:**
- Create: `include/gdt.h`
- Create: `src/gdt/gdt.c`
- Create: `src/gdt/gdt_flush.asm`
- Modify: `src/kernel/kernel.c` (gdt_init 호출)
- Test: `make test` (부팅 생존 = 1차 검증)

**Interfaces:**
- Consumes: `types.h`의 uint8_t/uint16_t/uint32_t
- Produces: `void gdt_init(void)` (gdt.h), 상수 `GDT_BASE`(0x00000800)/`GDT_ENTRIES`(7)/`GDT_SEL_KCODE·KDATA·KSTACK·UCODE·UDATA·USTACK`, `struct gdt_entry`(8B packed)/`struct gdt_ptr`(6B packed) — Task 3의 selftest가 이 상수·구조체를 사용. asm 심볼 `gdt_flush(uint32_t gdt_ptr_addr)`는 gdt.c 내부 전용.

- [ ] **Step 1: include/gdt.h 생성**

```c
#ifndef GDT_H
# define GDT_H

# include "types.h"

/* The subject requires the GDT at this physical address. It sits in the
 * real-mode free area (0x500-0x7BFF), well below the kernel at 1 MiB. */
# define GDT_BASE       0x00000800
# define GDT_ENTRIES    7

/* Selector = index * 8 (TI=0: GDT, RPL=0). */
# define GDT_SEL_KCODE  0x08
# define GDT_SEL_KDATA  0x10
# define GDT_SEL_KSTACK 0x18
# define GDT_SEL_UCODE  0x20
# define GDT_SEL_UDATA  0x28
# define GDT_SEL_USTACK 0x30

/* One 8-byte segment descriptor, exactly as the CPU expects it.
 * base/limit are split for 286->386 backward compatibility. */
struct gdt_entry {
	uint16_t limit_low;    /* limit 15:0  */
	uint16_t base_low;     /* base  15:0  */
	uint8_t  base_middle;  /* base  23:16 */
	uint8_t  access;       /* P | DPL(2) | S | type(4) */
	uint8_t  granularity;  /* G | D | L | AVL | limit 19:16 */
	uint8_t  base_high;    /* base  31:24 */
} __attribute__((packed));

/* Operand of lgdt: size-1 and linear address of the table. */
struct gdt_ptr {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed));

void gdt_init(void);

#endif
```

- [ ] **Step 2: src/gdt/gdt.c 생성**

```c
#include "gdt.h"

/* Build the table directly at GDT_BASE. The linker cannot place it there:
 * GRUB (multiboot) refuses to load ELF segments below 1 MiB, so the table
 * is written at runtime instead. */
static struct gdt_entry *const gdt = (struct gdt_entry *)GDT_BASE;
static struct gdt_ptr gp;

/* gdt_flush.asm: lgdt, reload DS/ES/FS/GS/SS, far-jump to reload CS. */
void gdt_flush(uint32_t gdt_ptr_addr);

static void gdt_set_gate(int idx, uint32_t base, uint32_t limit,
			 uint8_t access, uint8_t gran)
{
	gdt[idx].limit_low   = limit & 0xFFFF;
	gdt[idx].base_low    = base & 0xFFFF;
	gdt[idx].base_middle = (base >> 16) & 0xFF;
	gdt[idx].access      = access;
	/* upper 4 bits (G/D/L/AVL) from gran, lower 4 = limit 19:16 */
	gdt[idx].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
	gdt[idx].base_high   = (base >> 24) & 0xFF;
}

void gdt_init(void)
{
	/* All segments are flat: base 0, limit 0xFFFFF pages (4 GiB).
	 * access: P|DPL|S|type -> 0x9A code, 0x92 data, +0x60 for DPL=3. */
	gdt_set_gate(0, 0, 0x00000, 0x00, 0x00); /* null (CPU requirement) */
	gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xCF); /* kernel code  */
	gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xCF); /* kernel data  */
	gdt_set_gate(3, 0, 0xFFFFF, 0x92, 0xCF); /* kernel stack */
	gdt_set_gate(4, 0, 0xFFFFF, 0xFA, 0xCF); /* user code    */
	gdt_set_gate(5, 0, 0xFFFFF, 0xF2, 0xCF); /* user data    */
	gdt_set_gate(6, 0, 0xFFFFF, 0xF2, 0xCF); /* user stack   */
	gp.limit = sizeof(struct gdt_entry) * GDT_ENTRIES - 1; /* 55 */
	gp.base  = GDT_BASE;
	gdt_flush((uint32_t)&gp);
}
```

- [ ] **Step 3: src/gdt/gdt_flush.asm 생성**

```asm
; Load our GDT and reload every segment register.
; lgdt only fills the GDTR register; each segment register keeps a hidden
; descriptor cache that is refreshed only when the register is reloaded.
; CS cannot be written with mov -- only a far jump reloads it.

bits 32

section .text
global gdt_flush

gdt_flush:                     ; cdecl: gdt_flush(uint32_t gdt_ptr_addr)
	mov  eax, [esp + 4]
	lgdt [eax]                 ; GDTR <- {limit=55, base=0x800}
	mov  ax, 0x10              ; kernel data selector
	mov  ds, ax
	mov  es, ax
	mov  fs, ax
	mov  gs, ax
	mov  ax, 0x18              ; kernel stack selector (base 0: esp stays valid)
	mov  ss, ax
	jmp  0x08:.flush           ; far jump reloads CS with kernel code
.flush:
	ret                        ; stack untouched -> plain return works

section .note.GNU-stack noalloc noexec nowrite progbits
```

- [ ] **Step 4: kernel_main에서 gdt_init 호출**

`src/kernel/kernel.c`에서 include에 `#include "gdt.h"`를 추가하고,
magic 검증 블록 바로 다음(selftest 호출 전)에 한 줄 삽입:

```c
#include "kernel.h"
#include "gdt.h"
#include "vga.h"
#include "printk.h"

void kernel_main(uint32_t magic, uint32_t mb_info_addr)
{
	vga_init();
	if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
		vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
		printk("PANIC: bad multiboot magic: 0x%x\n", magic);
		return;
	}
	gdt_init();
	if (selftest_run() == 0)
		printk("kfs: selftest ok\n");
	/* after selftest: scroll_exercise would push this banner off screen */
	printk("kfs: multiboot ok (magic 0x%x, mbi 0x%x)\n", magic, mb_info_addr);
	printk("ptest [%c|%s|%d|%d|%u|%x|%%|%08x|%02x]\n",
		'X', "str", -42, -2147483647 - 1, 4294967295u, 0xdeadbeef,
		0xc0ffee, 0xf);
	vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
	printk("\n42\n");
}
```

(위 블록은 gdt.h include와 gdt_init() 한 줄 외에는 Task 1 완료 시점과 동일하다.)

- [ ] **Step 5: 테스트 — GDT 로드 후 부팅 생존 확인**

Run: `make test`
Expected: 기존 assert 전부 OK (Task 1과 동일한 통과 목록). 이 태스크의 검증
본질은 "생존"이다: far jump나 셀렉터가 잘못되면 CPU가 triple fault로 즉시
리셋되고(-no-reboot로 QEMU 정지) 화면이 비어 **모든** assert가 FAIL한다.

- [ ] **Step 6: Commit** (사용자가 태스크별 커밋을 승인한 경우)

```bash
git add include/gdt.h src/gdt/gdt.c src/gdt/gdt_flush.asm src/kernel/kernel.c
git commit -m "feat: 7-entry flat GDT built at 0x800, loaded via lgdt"
```

---

### Task 3: selftest에 GDT 검증 추가 ("gdt ok")

**Files:**
- Modify: `src/kernel/selftest.c`
- Modify: `Makefile` (test 기대 문자열에 "gdt ok" 추가)
- Test: `make test`

**Interfaces:**
- Consumes: Task 2의 `gdt.h` (GDT_BASE, GDT_ENTRIES, GDT_SEL_*, struct gdt_ptr, struct gdt_entry), 기존 `check()`/`g_failed` 패턴, libk `memcmp`
- Produces: 부팅 화면에 `kfs: gdt ok` (통과 시) 또는 `SELFTEST FAIL: <name>` (boot_test 내장 규약이 자동 감지)

- [ ] **Step 1: RED — Makefile 기대 문자열에 "gdt ok" 추가**

컨테이너 계층 test 타깃을 다음으로 교체:

```make
test: $(ISO)
	sh tests/boot_test.sh " 42 " "selftest ok" "gdt ok" "2badb002" \
		"ptest [X|str|-42|-2147483648|4294967295|deadbeef|%|00c0ffee|0f]" \
		"SCRL29" "!SCRL00" "!Booting" "!PANIC"
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `make test`
Expected: `FAIL missing: gdt ok` (다른 assert는 OK), make Error 1.

- [ ] **Step 3: GREEN — test_gdt 구현**

`src/kernel/selftest.c`에서 include 블록을 다음으로 교체:

```c
#include "libk.h"
#include "vga.h"
#include "kernel.h"
#include "gdt.h"
```

`test_str()` 함수 뒤에 다음 함수를 추가:

```c
/* Verify the GDT the CPU is actually using: read GDTR back with sgdt,
 * check the live segment selectors, and compare the kernel-code
 * descriptor bytes at 0x808 against the expected encoding. */
static void test_gdt(void)
{
	struct gdt_ptr gdtr;
	uint16_t       sel;
	int            before = g_failed;
	static const uint8_t kcode[8] = {
		0xff, 0xff, 0x00, 0x00, 0x00, 0x9a, 0xcf, 0x00
	};

	__asm__ volatile ("sgdt %0" : "=m"(gdtr));
	check(gdtr.base == GDT_BASE, "gdtr base 0x800");
	/* cast: avoid -Wsign-compare (uint16_t vs size_t) under -Werror */
	check(gdtr.limit == (uint16_t)(sizeof(struct gdt_entry) * GDT_ENTRIES - 1),
		"gdtr limit 55");
	__asm__ volatile ("mov %%cs, %0" : "=r"(sel));
	check(sel == GDT_SEL_KCODE, "cs selector");
	__asm__ volatile ("mov %%ds, %0" : "=r"(sel));
	check(sel == GDT_SEL_KDATA, "ds selector");
	__asm__ volatile ("mov %%ss, %0" : "=r"(sel));
	check(sel == GDT_SEL_KSTACK, "ss selector");
	check(memcmp((const void *)(GDT_BASE + 8), kcode, 8) == 0,
		"kernel code descriptor bytes");
	if (g_failed == before)
		vga_puts("kfs: gdt ok\n");
}
```

`selftest_run()`을 다음으로 교체 (test_gdt 호출 추가):

```c
int selftest_run(void)
{
	g_failed = 0;
	scroll_exercise(); /* first: FAIL lines must survive the scrolling */
	test_mem();
	test_str();
	test_gdt();
	return g_failed;
}
```

- [ ] **Step 4: 테스트 통과 확인**

Run: `make test`
Expected: `OK   found: gdt ok` 포함 전부 OK. 화면 순서: SCRL... → `kfs: gdt ok`
→ `kfs: selftest ok` → 배너 → ptest → 42.

- [ ] **Step 5: Commit** (사용자가 태스크별 커밋을 승인한 경우)

```bash
git add src/kernel/selftest.c Makefile
git commit -m "test: verify GDTR base/limit, live selectors, descriptor bytes"
```

---

### Task 4: 커널 스택 hexdump 도구

**Files:**
- Modify: `src/boot/boot.asm` (stack_top/stack_bottom export)
- Modify: `include/kernel.h` (dump_hex/print_kernel_stack 선언)
- Create: `src/kernel/stack_dump.c`
- Modify: `src/kernel/kernel.c` (canary + print_kernel_stack 호출)
- Modify: `Makefile` (기대 문자열 "kernel stack:", "02 b0 ad 2b" 추가)
- Test: `make test`

**Interfaces:**
- Consumes: Task 1의 printk `%08x`/`%02x`, boot.asm 심볼 `stack_top`
- Produces: `void dump_hex(const void *addr, uint32_t len)`, `void print_kernel_stack(void)` (kernel.h) — 보너스 셸(2차)이 재사용 예정

- [ ] **Step 1: RED — Makefile 기대 문자열 추가**

컨테이너 계층 test 타깃을 다음으로 교체:

```make
test: $(ISO)
	sh tests/boot_test.sh " 42 " "selftest ok" "gdt ok" "2badb002" \
		"ptest [X|str|-42|-2147483648|4294967295|deadbeef|%|00c0ffee|0f]" \
		"kernel stack:" "02 b0 ad 2b" \
		"SCRL29" "!SCRL00" "!Booting" "!PANIC"
```

- [ ] **Step 2: 테스트가 실패하는지 확인**

Run: `make test`
Expected: `FAIL missing: kernel stack:` 와 `FAIL missing: 02 b0 ad 2b` (그 외 OK), make Error 1.

- [ ] **Step 3: boot.asm에서 스택 심볼 export**

`src/boot/boot.asm`의 `.bss` 섹션을 다음으로 교체 (global 두 줄 추가):

```asm
section .bss
align 16
global stack_bottom
global stack_top
stack_bottom:
	resb 16384                       ; 16 KiB kernel stack
stack_top:
```

- [ ] **Step 4: kernel.h에 선언 추가**

`include/kernel.h`의 프로토타입 블록을 다음으로 교체:

```c
void kernel_main(uint32_t magic, uint32_t mb_info_addr);
int  selftest_run(void);
void dump_hex(const void *addr, uint32_t len);
void print_kernel_stack(void);
```

- [ ] **Step 5: src/kernel/stack_dump.c 생성**

```c
#include "kernel.h"
#include "printk.h"
#include "types.h"

/* Top of the 16 KiB kernel stack reserved in boot.asm's .bss.
 * x86 stacks grow downward: esp starts at stack_top and decreases,
 * so [esp, stack_top) is exactly the data currently on the stack. */
extern uint8_t stack_top[];

/* One row: "AAAAAAAA  XX " * 16 "|" ascii "|" = 76 cols (fits 80). */
void dump_hex(const void *addr, uint32_t len)
{
	const uint8_t *p = (const uint8_t *)addr;
	uint32_t       i;
	uint32_t       j;
	uint8_t        c;

	for (i = 0; i < len; i += 16) {
		printk("%08x  ", (uint32_t)(uintptr_t)(p + i));
		for (j = 0; j < 16; j++) {
			if (i + j < len)
				printk("%02x ", p[i + j]);
			else
				printk("   ");
		}
		printk("|");
		for (j = 0; j < 16 && i + j < len; j++) {
			c = p[i + j];
			printk("%c", (c >= 0x20 && c <= 0x7e) ? (char)c : '.');
		}
		printk("|\n");
	}
}

void print_kernel_stack(void)
{
	uint32_t esp;
	uint32_t top;
	uint32_t start;

	__asm__ volatile ("mov %%esp, %0" : "=r"(esp));
	top   = (uint32_t)(uintptr_t)stack_top;
	start = esp & ~(uint32_t)0xf; /* align rows to 16 for readability */
	printk("kernel stack: esp=%08x top=%08x size=%u bytes\n",
		esp, top, top - esp);
	dump_hex((const void *)start, top - start);
}
```

- [ ] **Step 6: kernel_main에서 canary를 심고 덤프 호출**

`src/kernel/kernel.c`의 `kernel_main`을 다음으로 교체 (변경: canary 지역변수,
ptest 줄 뒤 canary 대입 + print_kernel_stack 호출):

```c
void kernel_main(uint32_t magic, uint32_t mb_info_addr)
{
	/* volatile: force a real stack slot so the dump provably contains
	 * a known value (02 b0 ad 2b little-endian) for the boot test. */
	volatile uint32_t stack_canary;

	vga_init();
	if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
		vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
		printk("PANIC: bad multiboot magic: 0x%x\n", magic);
		return;
	}
	gdt_init();
	if (selftest_run() == 0)
		printk("kfs: selftest ok\n");
	/* after selftest: scroll_exercise would push this banner off screen */
	printk("kfs: multiboot ok (magic 0x%x, mbi 0x%x)\n", magic, mb_info_addr);
	printk("ptest [%c|%s|%d|%d|%u|%x|%%|%08x|%02x]\n",
		'X', "str", -42, -2147483647 - 1, 4294967295u, 0xdeadbeef,
		0xc0ffee, 0xf);
	stack_canary = MULTIBOOT_BOOTLOADER_MAGIC;
	(void)stack_canary;
	print_kernel_stack();
	vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
	printk("\n42\n");
}
```

canary가 필요한 이유: `_start`가 push한 magic도 같은 바이트열을 남기지만,
-O2에서 컴파일러가 인자 슬롯을 재사용할 수 있어 보장이 없다. volatile 지역
변수는 반드시 현재 프레임의 실제 스택 슬롯에 기록되므로 덤프에 결정적으로
나타난다.

- [ ] **Step 7: 테스트 통과 확인**

Run: `make test`
Expected: 전부 OK — `OK   found: kernel stack:`, `OK   found: 02 b0 ad 2b` 포함.
덤프는 헤더 1행 + 3~6행 수준이라 SCRL29가 화면 밖으로 밀리지 않는다
(`OK   found: SCRL29` 유지 확인). 만약 부팅이 느려 화면이 비면
`BOOT_WAIT=10 make test`로 재시도.

- [ ] **Step 8: Commit** (사용자가 태스크별 커밋을 승인한 경우)

```bash
git add src/boot/boot.asm include/kernel.h src/kernel/stack_dump.c src/kernel/kernel.c Makefile
git commit -m "feat: human-friendly kernel stack hexdump (esp..stack_top)"
```

---

### Task 5: README 개정 + 클린 최종 검증

**Files:**
- Modify: `README.md` (전면 개정)
- Test: `make fclean && make test` (클린 빌드부터 전체 재검증)

**Interfaces:**
- Consumes: Task 1-4의 최종 산출물 (문서화 대상)
- Produces: 평가자용 README. 코드 인터페이스 없음.

- [ ] **Step 1: README.md 전체를 다음으로 교체**

```markdown
# KFS-2 — GDT & Stack

Freestanding i386 kernel for 42's *Kernel From Scratch 2*: builds on the
KFS-1 base (GRUB multiboot boot, VGA text driver, libk, printk) and adds
a 7-entry flat GDT (null + kernel/user code/data/stack) installed at
physical `0x00000800` via `lgdt`, plus a human-friendly kernel stack
hexdump built on printk.

## Requirements

Any host with Docker **or** rootless Podman. Nothing else — compiler,
GRUB, QEMU all live in the build container (Debian, pinned to
`linux/amd64` because BIOS GRUB modules only exist on x86).

## Usage

| command | effect |
|---|---|
| `make` | build `kernel.bin` + `kfs.iso` in the container |
| `make run` | boot the ISO in QEMU (terminal/curses display) |
| `make test` | headless boot + assert screen contents + ISO size |
| `make shell` | interactive shell inside the build container |
| `make clean` / `fclean` / `re` | the usual |

With Podman: `make CONTAINER=podman <target>` (adds `:Z` volume label and
`--userns=keep-id` automatically).

To leave `make run`: press `ESC` then `2` to reach the QEMU monitor and
type `quit` (if the terminal ends up garbled, run `reset`).

Inside `make shell`, run targets as `make all IN_CONTAINER=1`.

## 42 cluster (Fedora, no sudo)

```sh
sh scripts/cluster_setup.sh        # once: podman storage -> /goinfre
make CONTAINER=podman test
```

## Layout

```
src/boot/boot.asm      multiboot v1 header, 16 KiB stack, jump to kernel_main
src/gdt/gdt.c          7-entry flat GDT built at 0x00000800
src/gdt/gdt_flush.asm  lgdt + segment register reload (far jump for CS)
src/kernel/kernel.c    kernel entry: magic check, gdt_init, banner, "42"
src/kernel/selftest.c  on-boot libk/scroll/GDT self-tests (print-on-failure)
src/kernel/stack_dump.c dump_hex + print_kernel_stack (esp..stack_top)
src/drivers/vga.c      VGA text driver (0xB8000)
src/lib/string.c       memset/memcpy/memmove/memcmp/strlen/strcmp
src/lib/printk.c       %c %s %d %u %x %% with zero-pad width (%08x, %02x)
include/               kernel headers (types.h is the freestanding base)
linker.ld              custom linker script (kernel at 1 MiB)
grub.cfg               GRUB menu entry
tests/boot_test.sh     QEMU-monitor VGA-dump assertions
```

## How the boot works

BIOS → GRUB finds the multiboot header in `kernel.bin` (inside `kfs.iso`),
loads it at 1 MiB, switches to 32-bit protected mode, jumps to `_start`
(eax = `0x2BADB002`, ebx = multiboot info). `boot.asm` sets up a 16 KiB
stack and calls `kernel_main(magic, mb_info_addr)`, which validates the
magic, then builds the kernel's own GDT.

The GDT cannot be placed at `0x00000800` by the linker (GRUB refuses to
load ELF segments below 1 MiB), so `gdt_init` writes the 7 descriptors
there at runtime, loads GDTR with `lgdt`, reloads DS/ES/FS/GS (`0x10`)
and SS (`0x18`), and far-jumps to reload CS (`0x08`). All segments are
flat (base 0, 4 GiB limit), so every pointer — including esp — stays
valid across the switch. Self-tests then read GDTR back with `sgdt` and
verify the live selectors, and `print_kernel_stack` hexdumps the region
esp..stack_top (the stack grows downward from `stack_top`).
```

- [ ] **Step 2: 클린 빌드부터 최종 검증**

Run: `make fclean && make test`
Expected: 소스 전체 재컴파일(-Werror 경고 0), 부팅 테스트 전부 OK,
`OK   iso size: <bytes> (<= 10 MiB)`.

- [ ] **Step 3: 산출물 미추적 확인**

Run: `git status --short`
Expected: 소스/문서 변경만 표시. `*.o`, `*.d`, `kernel.bin`, `isodir/`,
`kfs.iso`는 나타나지 않음(.gitignore). `kfs2.pdf`는 untracked로 남음(정책 미정).

- [ ] **Step 4: Commit** (사용자가 태스크별 커밋을 승인한 경우)

```bash
git add README.md
git commit -m "docs: KFS-2 README (GDT at 0x800, stack hexdump)"
```

- [ ] **Step 5: (제출 직전, 사용자 명시 승인 필요) 턴인 ISO 커밋**

과제 제출물에 "기본 가상 이미지"가 포함되므로 최종 시점에만:

```bash
make            # 최신 kfs.iso 생성
git add -f kfs.iso
git commit -m "chore: ship kfs.iso turn-in image"
```

이 단계는 사용자가 명시적으로 요청할 때까지 실행하지 않는다.

---

## 부록 A. 과제 요구사항 ↔ Task 매핑

| kfs2.pdf 요구사항 | Task |
|---|---|
| GDT 생성·채우기·커널에 링크 | Task 2 |
| 6개 세그먼트 (KCode/KData/KStack/UCode/UData/UStack) | Task 2 (§8.2 값), Task 3 (검증) |
| GDT를 0x00000800에 배치 | Task 2 (런타임 직접 구축), Task 3 (sgdt로 확인) |
| GDT를 BIOS에 선언 (= lgdt) | Task 2 (gdt_flush) |
| 커널 스택을 human-friendly하게 출력 | Task 4 (printk 기반은 Task 1) |
| 작업물 ≤ 10MB | 모든 Task의 make test가 ISO 크기 검사 |
| 코드 + Makefile + 가상 이미지 제출 | Task 5 (ISO 커밋은 사용자 승인 후) |

## 부록 B. 트러블슈팅

- **모든 assert가 한꺼번에 FAIL + 화면 비어 있음**: GDT 로드 후 triple fault
  가능성. gdt_flush의 far jump 셀렉터(0x08), gdt_ptr의 base/limit, packed
  attribute 누락 순으로 의심하라. `make run`으로 직접 부팅해 화면을 보라.
- **`gdtr limit 55` FAIL**: GDT_ENTRIES(7)와 sizeof(struct gdt_entry)(8)
  확인 — packed가 빠지면 sizeof가 8이 아닐 수 있다.
- **`02 b0 ad 2b` FAIL인데 덤프는 보임**: 덤프 행이 화면 폭을 넘어 줄바꿈되면
  바이트열 사이에 개행이 끼어 grep이 실패할 수 있다. 행 폭 76칸 규칙 위반
  여부(dump_hex 포맷 변경)를 확인하라.
- **부팅은 되는데 문자열이 몇 개 빠짐**: BOOT_WAIT 부족(특히 Apple Silicon
  Rosetta). `BOOT_WAIT=10 make test`.
- **컨테이너 안에서 직접 작업 시**: `make shell` 후 `make test IN_CONTAINER=1`.
