// The WonderWitch's flash, written through FreyaBIOS's bank calls as a
// program written for it writes it. Run on AthenaOS, whose MBM29DL400TC
// driver runs on the hardware, it holds the emulated chip to what that
// driver expects of it. It writes a pattern to a 64 KB sector of its own and
// reads it back, erases the sector and reads it back blank, then writes
// another pattern and reads it back; it programs its verdict into the same
// sector, at $8000, "PASS" or "FAIL" and a byte with a bit for each step
// that held, where tests/machine/TestRomsTest.cpp reads it.

#include <stdbool.h>
#include <stdint.h>
#include <wonderful.h>
#include <sys/bios.h>

// The 64 KB at $10000 of the 512 KB flash: past this program, which sits at
// its start, and a sector of its own.
#define TEST_BANK 0xFFF9
#define PATTERN_SIZE 256

// In the console's RAM: the program's own data is in the SRAM, where the
// flash is shown while the BIOS reads or writes it.
static uint8_t __wf_iram* buffer;

static uint8_t pattern(uint16_t at, uint8_t seed) {
	return (uint8_t) ((at * 7) + seed);
}

static bool write_and_check(uint8_t seed) {
	for (uint16_t i = 0; i < PATTERN_SIZE; i++) buffer[i] = pattern(i, seed);
	bank_write_block(TEST_BANK, 0, buffer, PATTERN_SIZE);
	for (uint16_t i = 0; i < PATTERN_SIZE; i++) buffer[i] = 0;
	bank_read_block(TEST_BANK, 0, buffer, PATTERN_SIZE);
	for (uint16_t i = 0; i < PATTERN_SIZE; i++) {
		if (buffer[i] != pattern(i, seed)) return false;
	}
	return true;
}

static bool blank_at(uint16_t offset) {
	bank_read_block(TEST_BANK, offset, buffer, PATTERN_SIZE);
	for (uint16_t i = 0; i < PATTERN_SIZE; i++) {
		if (buffer[i] != 0xFF) return false;
	}
	return true;
}

void main(int argc, char** argv) {
	buffer = sys_alloc_iram(NULL, PATTERN_SIZE);
	text_screen_init();
	text_put_string(0, 0, "Flash test");

	bool const first = write_and_check(0x5A);
	text_put_string(0, 1, first ? "write: ok" : "write: bad");
	bank_erase_flash(TEST_BANK);
	bool const erased = blank_at(0) && blank_at(0xFF00);
	text_put_string(0, 2, erased ? "erase: ok" : "erase: bad");
	bool const second = write_and_check(0x11);
	text_put_string(0, 3, second ? "rewrite: ok" : "rewrite: bad");

	bool const passed = first && erased && second;
	char const* const verdict = passed ? "PASS" : "FAIL";
	for (uint16_t i = 0; i < 4; i++) buffer[i] = verdict[i];
	buffer[4] = (first ? 1 : 0) | (erased ? 2 : 0) | (second ? 4 : 0);
	bank_write_block(TEST_BANK, 0x8000, buffer, 5);
	text_put_string(0, 5, passed ? "PASS" : "FAIL");

	while (true) key_wait();
}
