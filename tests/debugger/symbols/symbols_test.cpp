/*
	Test driver for the symbol table of the debugger (libswirl/debugger/dbg_symbols.cpp).

	symbols_test table      the .symtab walk on a fixture ELF, lookup by address, the text form of an address
	symbols_test damaged    images that are cut, that are no ELF and that name offsets outside of themselves

	The fixture is built here, byte for byte, so that a test can say what every field holds and can break one.
	An image is handed over in memory that ends where the image ends: on hosts with mmap the page behind it
	cannot be read, so a read past the size of an image ends the test.

	Exit code 0 when every check held.
*/
#include "dbg_symbols.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#define HAVE_GUARD_PAGE 1
#endif

using namespace dbg;

namespace
{
	int failures;
	int checks;

	bool Check(bool ok, const char* fmt, ...)
	{
		checks++;

		if (!ok)
		{
			char text[512];
			va_list args;
			va_start(args, fmt);
			vsnprintf(text, sizeof(text), fmt, args);
			va_end(args);

			if (++failures <= 60)
				fprintf(stderr, "FAIL %s\n", text);
		}

		return ok;
	}

	/*
		An image in memory that ends exactly where the image ends
	*/
	class Guarded
	{
	public:
		explicit Guarded(const std::vector<uint8_t>& bytes) : size_(bytes.size())
		{
#if defined(HAVE_GUARD_PAGE)
			const size_t page = (size_t)sysconf(_SC_PAGESIZE);
			const size_t pages = (size_ + page - 1) / page;

			mapped_ = (pages + 1) * page;
			base_ = (uint8_t*)mmap(nullptr, mapped_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

			if (base_ == MAP_FAILED)
			{
				perror("mmap");
				exit(2);
			}

			// the page behind the image
			if (mprotect(base_ + pages * page, page, PROT_NONE) != 0)
			{
				perror("mprotect");
				exit(2);
			}

			data_ = base_ + pages * page - size_;
#else
			data_ = (uint8_t*)malloc(size_ != 0 ? size_ : 1);
#endif
			if (size_ != 0)
				memcpy(data_, bytes.data(), size_);
		}

		~Guarded()
		{
#if defined(HAVE_GUARD_PAGE)
			munmap(base_, mapped_);
#else
			free(data_);
#endif
		}

		Guarded(const Guarded&) = delete;
		Guarded& operator=(const Guarded&) = delete;

		const void* data() const { return data_; }
		size_t size() const { return size_; }

	private:
		size_t size_;
		uint8_t* data_;
#if defined(HAVE_GUARD_PAGE)
		uint8_t* base_;
		size_t mapped_;
#endif
	};

	void Load(const std::vector<uint8_t>& image)
	{
		Guarded guarded(image);

		symbols::OnElfLoaded(guarded.data(), guarded.size());
	}

	/*
		The fixture
	*/
	const unsigned kNotype = 0, kObject = 1, kFunc = 2, kSection = 3, kFile = 4;
	const unsigned kLocal = 0, kGlobal = 1;
	const uint16_t kUndef = 0, kText = 1, kAbs = 0xFFF1;

	struct Sym {
		std::string name;
		uint32_t value;
		uint32_t size;
		unsigned type;
		unsigned bind;
		uint16_t shndx;
	};

	void Put16(std::vector<uint8_t>& out, size_t at, uint32_t v)
	{
		out[at] = (uint8_t)v;
		out[at + 1] = (uint8_t)(v >> 8);
	}

	void Put32(std::vector<uint8_t>& out, size_t at, uint32_t v)
	{
		Put16(out, at, v & 0xFFFF);
		Put16(out, at + 2, v >> 16);
	}

	// Where the builder put things, for the tests that break them
	struct Layout {
		size_t symtab, symtabSize;		// the symbol entries
		size_t strtab, strtabSize;		// their names
		size_t shdrs;					// the section headers: null, .text, .symtab, .strtab
		size_t firstName;				// offset of the first symbol's name inside the string table
	};

	const size_t kEhdr = 52, kShdr = 40, kSymEntry = 16;
	const uint32_t kSecSymtab = 2, kSecStrtab = 3;

	// offsets inside a section header
	const size_t kShType = 4, kShOffset = 16, kShSize = 20, kShLink = 24, kShEntsize = 36;

	std::vector<uint8_t> Build(const std::vector<Sym>& syms, Layout* layout = nullptr, bool terminate_names = true)
	{
		std::vector<uint8_t> strtab(1, 0);
		std::vector<uint32_t> name_at;

		for (const Sym& s : syms)
		{
			if (s.name.empty())
			{
				name_at.push_back(0);
				continue;
			}

			name_at.push_back((uint32_t)strtab.size());
			strtab.insert(strtab.end(), s.name.begin(), s.name.end());
			strtab.push_back(0);
		}

		// the last name runs to the end of the table
		if (!terminate_names && strtab.size() > 1)
			strtab.pop_back();

		const size_t text = kEhdr, text_size = 16;
		const size_t symtab = text + text_size;
		const size_t symtab_size = (syms.size() + 1) * kSymEntry;
		const size_t strtab_at = symtab + symtab_size;
		const size_t shdrs = strtab_at + strtab.size();

		std::vector<uint8_t> out(shdrs + 4 * kShdr, 0);

		memcpy(out.data(), "\x7F" "ELF", 4);
		out[4] = 1;							// 32 bits
		out[5] = 1;							// little-endian
		out[6] = 1;
		Put16(out, 16, 2);					// an executable
		Put16(out, 18, 42);					// SuperH
		Put32(out, 20, 1);
		Put32(out, 24, 0x8C010000);			// the entry
		Put32(out, 32, (uint32_t)shdrs);
		Put16(out, 40, (uint32_t)kEhdr);
		Put16(out, 46, (uint32_t)kShdr);
		Put16(out, 48, 4);
		Put16(out, 50, 0);

		// entry 0 of a .symtab is empty
		for (size_t i = 0; i < syms.size(); i++)
		{
			const size_t at = symtab + (i + 1) * kSymEntry;

			Put32(out, at, name_at[i]);
			Put32(out, at + 4, syms[i].value);
			Put32(out, at + 8, syms[i].size);
			out[at + 12] = (uint8_t)((syms[i].bind << 4) | syms[i].type);
			Put16(out, at + 14, syms[i].shndx);
		}

		memcpy(out.data() + strtab_at, strtab.data(), strtab.size());

		// .text
		Put32(out, shdrs + 1 * kShdr + kShType, 1);
		Put32(out, shdrs + 1 * kShdr + kShOffset, (uint32_t)text);
		Put32(out, shdrs + 1 * kShdr + kShSize, (uint32_t)text_size);

		// .symtab, with its names in section 3
		Put32(out, shdrs + 2 * kShdr + kShType, kSecSymtab);
		Put32(out, shdrs + 2 * kShdr + kShOffset, (uint32_t)symtab);
		Put32(out, shdrs + 2 * kShdr + kShSize, (uint32_t)symtab_size);
		Put32(out, shdrs + 2 * kShdr + kShLink, 3);
		Put32(out, shdrs + 2 * kShdr + kShEntsize, (uint32_t)kSymEntry);

		// .strtab
		Put32(out, shdrs + 3 * kShdr + kShType, kSecStrtab);
		Put32(out, shdrs + 3 * kShdr + kShOffset, (uint32_t)strtab_at);
		Put32(out, shdrs + 3 * kShdr + kShSize, (uint32_t)strtab.size());

		if (layout != nullptr)
		{
			layout->symtab = symtab;
			layout->symtabSize = symtab_size;
			layout->strtab = strtab_at;
			layout->strtabSize = strtab.size();
			layout->shdrs = shdrs;
			layout->firstName = name_at.empty() ? 0 : name_at[0];
		}

		return out;
	}

	// A name with bytes above 0x7F and with control characters, as a guest image can have them
	const char kOddName[] = "caf\xC3\xA9_\xFF\x01\ttab";

	std::vector<Sym> Fixture()
	{
		return {
			{ "_start",      0x8C010000, 0x40,  kFunc,    kGlobal, kText },
			{ "_main",       0x8C010040, 0x100, kFunc,    kGlobal, kText },
			{ "label_a",     0x8C010200, 0,     kNotype,  kLocal,  kText },		// reaches to label_b
			{ "label_b",     0x8C010280, 0,     kNotype,  kLocal,  kText },		// reaches to _table
			{ "_table",      0x8C010300, 0x20,  kObject,  kGlobal, kText },
			{ kOddName,      0x8C010400, 0x10,  kFunc,    kGlobal, kText },
			{ "_far",        0x8C100000, 0,     kNotype,  kGlobal, kText },		// the last one: kMaxSymbolReach
			{ "_stack_top",  0x8D000000, 0,     kNotype,  kGlobal, kAbs  },
			// several names of one address: the function names it
			{ "alias_label", 0x8C020000, 0,     kNotype,  kLocal,  kText },
			{ "alias_obj",   0x8C020000, 8,     kObject,  kGlobal, kText },
			{ "alias_func",  0x8C020000, 8,     kFunc,    kLocal,  kText },
			{ "alias_gfunc", 0x8C020000, 8,     kFunc,    kGlobal, kText },
			// absolute symbols and symbols of a section at one address, the same in type and binding: the
			// first one of a section names it. (A KallistiOS program has __executable_start, absolute, in
			// front of _start.)
			{ "abs_first",   0x8C040000, 0,     kNotype,  kGlobal, kAbs  },
			{ "sect_second", 0x8C040000, 0,     kNotype,  kGlobal, kText },
			{ "abs_third",   0x8C040000, 0,     kNotype,  kGlobal, kAbs  },
			{ "sect_fourth", 0x8C040000, 0,     kNotype,  kGlobal, kText },
			// type and binding come first
			{ "sect_label",  0x8C040100, 0,     kNotype,  kGlobal, kText },
			{ "abs_func",    0x8C040100, 4,     kFunc,    kGlobal, kAbs  },
			{ "sect_local",  0x8C040200, 0,     kNotype,  kLocal,  kText },
			{ "abs_global",  0x8C040200, 0,     kNotype,  kGlobal, kAbs  },
			// what the table does not take
			{ "",            0x8C010000, 0,     kSection, kLocal,  kText },
			{ ".text",       0x8C010000, 0,     kSection, kLocal,  kText },
			{ "file.c",      0,          0,     kFile,    kLocal,  kAbs  },
			{ "_undefined",  0,          0,     kFunc,    kGlobal, kUndef },
			{ "",            0x8C010008, 4,     kFunc,    kLocal,  kText },		// no name
			{ "tls_thing",   0x8C010500, 4,     6,        kGlobal, kText },		// a thread-local object
			// a name that is there twice: the first definition is found
			{ "twice",       0x8C030000, 4,     kFunc,    kLocal,  kText },
			{ "twice",       0x8C030100, 4,     kFunc,    kLocal,  kText },
		};
	}

	const size_t kFixtureSymbols = 22;

	struct Found {
		bool ok;
		std::string name;
		uint32_t offset;
	};

	Found Lookup(uint32_t addr)
	{
		const char* name = "(untouched)";
		uint32_t offset = 0xDEADBEEF;
		Found f;

		f.ok = symbols::Lookup(addr, &name, &offset);
		f.name = name;
		f.offset = offset;

		return f;
	}

	std::string Text(uint32_t addr, bool* ok = nullptr)
	{
		char text[128];
		bool rv = symbols::Format(addr, text, sizeof(text));

		if (ok != nullptr)
			*ok = rv;

		return text;
	}

	void CheckIn(uint32_t addr, const char* name, uint32_t offset, const char* what)
	{
		Found f = Lookup(addr);

		Check(f.ok && f.name == name && f.offset == offset, "%s: %08X is %s+0x%x (found %d, %s+0x%x)", what, addr, name, offset, f.ok,
			f.name.c_str(), f.offset);
	}

	void CheckNone(uint32_t addr, const char* what)
	{
		Found f = Lookup(addr);
		bool ok;
		std::string text = Text(addr, &ok);

		Check(!f.ok && f.name == "(untouched)" && f.offset == 0xDEADBEEF, "%s: %08X lies in no symbol, and the outputs are left alone (found %d, %s)", what,
			addr, f.ok, f.name.c_str());
		Check(!ok && text.empty(), "%s: %08X has no text form (\"%s\")", what, addr, text.c_str());
	}

	void TestTable()
	{
		Check(symbols::Count() == 0, "no symbols before an ELF is loaded (%zu)", symbols::Count());
		CheckNone(0x8C010000, "empty table");

		uint32_t addr = 1;

		Check(!symbols::Find("_start", &addr) && addr == 1, "Find() on an empty table finds nothing and leaves the address alone");

		Load(Build(Fixture()));

		Check(symbols::Count() == kFixtureSymbols, "the fixture has %zu named, defined functions, objects and labels (%zu)", kFixtureSymbols, symbols::Count());

		// by name
		Check(symbols::Find("_start", &addr) && addr == 0x8C010000, "_start is at 8C010000 (%08X)", addr);
		Check(symbols::Find("_main", &addr) && addr == 0x8C010040, "_main is at 8C010040 (%08X)", addr);
		Check(symbols::Find("_stack_top", &addr) && addr == 0x8D000000, "an absolute symbol is defined: _stack_top is at 8D000000 (%08X)", addr);
		Check(symbols::Find(kOddName, &addr) && addr == 0x8C010400, "the name with bytes above 7F and control characters is found as the image has it (%08X)", addr);
		Check(symbols::Find("twice", &addr) && addr == 0x8C030000, "a name that is defined twice is its first definition (%08X)", addr);

		addr = 2;
		Check(!symbols::Find("_undefined", &addr) && addr == 2, "an undefined symbol is not in the table");
		Check(!symbols::Find(".text", &addr), "a section symbol is not in the table");
		Check(!symbols::Find("file.c", &addr), "a file symbol is not in the table");
		Check(!symbols::Find("tls_thing", &addr), "a symbol of another type is not in the table");
		Check(!symbols::Find("_star", &addr) && !symbols::Find("_start2", &addr) && !symbols::Find("", &addr), "a name is matched whole");

		// by address
		CheckIn(0x8C010000, "_start", 0, "the start of a symbol");
		CheckIn(0x8C01003F, "_start", 0x3F, "the last byte of a symbol with a size");
		CheckIn(0x8C010040, "_main", 0, "the next symbol");
		CheckIn(0x8C01013F, "_main", 0xFF, "inside");
		CheckNone(0x8C010140, "behind the end of a symbol with a size");
		CheckNone(0x8C0101FF, "in front of the next symbol");
		CheckNone(0x8C00FFFF, "in front of the first symbol");

		// the low 29 bits are the address
		CheckIn(0x0C010044, "_main", 4, "the same code at 0C..");
		CheckIn(0xAC010044, "_main", 4, "the same code at AC..");
		CheckIn(0x2C010044, "_main", 4, "the same code at 2C..");

		// without a size
		CheckIn(0x8C010200, "label_a", 0, "a symbol without a size");
		CheckIn(0x8C01027F, "label_a", 0x7F, "a symbol without a size reaches to the next symbol");
		CheckIn(0x8C010280, "label_b", 0, "the next symbol");
		CheckIn(0x8C0102FF, "label_b", 0x7F, "a symbol without a size reaches to the next symbol");
		CheckIn(0x8C100000, "_far", 0, "a symbol without a size and with none close behind it");
		CheckIn(0x8C100000 + symbols::kMaxSymbolReach - 1, "_far", symbols::kMaxSymbolReach - 1, "it reaches kMaxSymbolReach bytes");
		CheckNone(0x8C100000 + symbols::kMaxSymbolReach, "and no further");
		CheckIn(0x8D00FFFF, "_stack_top", 0xFFFF, "the last symbol of the table reaches kMaxSymbolReach bytes");
		CheckNone(0x8D010000, "and no further");

		// several names
		CheckIn(0x8C020000, "alias_gfunc", 0, "of several symbols at one address the global function names it");
		CheckIn(0x8C020007, "alias_gfunc", 7, "and what lies in it");
		CheckIn(0x8C020008, "alias_label", 8, "behind the sized ones, the label of that address still reaches to the next symbol");

		// a symbol of a section and an absolute one
		CheckIn(0x8C040000, "sect_second", 0, "of absolute symbols and symbols of a section at one address, the same in type and binding, the first one of a section names it");
		CheckIn(0x8C040080, "sect_second", 0x80, "and what lies behind it");
		Check(symbols::Find("abs_first", &addr) && addr == 0x8C040000 && symbols::Find("abs_third", &addr) && addr == 0x8C040000,
			"the absolute symbols of that address are found by their names (%08X)", addr);
		CheckIn(0x8C040100, "abs_func", 0, "the type comes first: an absolute function names its address before a label of a section");
		CheckIn(0x8C040104, "sect_label", 4, "which names what lies behind the function");
		CheckIn(0x8C040200, "abs_global", 0, "and so does the binding: a global absolute symbol names its address before a local symbol of a section");

		// the text form
		Check(Text(0x8C010040) == "_main", "the text form at the start of a symbol is its name (\"%s\")", Text(0x8C010040).c_str());
		Check(Text(0x8C01004A) == "_main+0xa", "inside it is name+0x<offset>, lower-case hex (\"%s\")", Text(0x8C01004A).c_str());
		Check(Text(0x8C0100FC) == "_main+0xbc", "... (\"%s\")", Text(0x8C0100FC).c_str());
		Check(Text(0x8D00ABCD) == "_stack_top+0xabcd", "... (\"%s\")", Text(0x8D00ABCD).c_str());
		Check(Text(0x8C010400) == kOddName, "the table keeps the bytes of a name: above 7F and control characters");
		Check(Text(0x8C010404) == std::string(kOddName) + "+0x4", "and with an offset");

		{
			char small[6];
			bool ok;

			memset(small, 'x', sizeof(small));
			ok = symbols::Format(0x8C01004A, small, 5);
			Check(ok && strcmp(small, "_mai") == 0 && small[5] == 'x', "the text is cut to n bytes, terminator included (\"%.5s\")", small);

			memset(small, 'x', sizeof(small));
			ok = symbols::Format(0x8C01004A, small, 1);
			Check(ok && small[0] == 0 && small[1] == 'x', "n = 1 leaves room for the terminator only");

			memset(small, 'x', sizeof(small));
			ok = symbols::Format(0x8C01004A, small, 0);
			Check(ok && small[0] == 'x', "n = 0 writes nothing");

			ok = symbols::Format(0x8C0101FF, small, 0);
			Check(!ok && small[0] == 'x', "n = 0 writes nothing for an address without a symbol either");

			ok = symbols::Format(0x8C0101FF, small, sizeof(small));
			Check(!ok && small[0] == 0, "an address without a symbol gives the empty text");
		}

		// a second image replaces the table
		Load(Build({ { "_other", 0x8C050000, 4, kFunc, kGlobal, kText } }));
		Check(symbols::Count() == 1 && !symbols::Find("_main", &addr) && symbols::Find("_other", &addr) && addr == 0x8C050000,
			"a second ELF replaces the table (%zu symbols)", symbols::Count());
		CheckNone(0x8C010040, "the table of the ELF before");

		// a boot that loads no ELF
		symbols::OnElfLoaded(nullptr, 0);
		Check(symbols::Count() == 0, "a boot without an ELF empties the table (%zu)", symbols::Count());

		Load(Build(Fixture()));
		symbols::Clear();
		Check(symbols::Count() == 0 && !symbols::Find("_main", &addr), "Clear() empties the table (%zu)", symbols::Count());
		CheckNone(0x8C010040, "a cleared table");

		// an image without symbols is an ELF all the same
		Load(Build({ }));
		Check(symbols::Count() == 0, "an ELF whose .symtab has only its empty entry has no symbols (%zu)", symbols::Count());
	}

	/*
		Damaged images
	*/
	uint32_t rng_state = 0x2545F491;

	uint32_t Rng()
	{
		rng_state ^= rng_state << 13;
		rng_state ^= rng_state >> 17;
		rng_state ^= rng_state << 5;
		return rng_state;
	}

	// Loads an image that must give no symbol
	void CheckEmpty(const std::vector<uint8_t>& image, const char* what)
	{
		Load(Build(Fixture()));
		Load(image);

		Check(symbols::Count() == 0, "%s: no symbols, and none left of the ELF before (%zu)", what, symbols::Count());
	}

	void TestDamaged()
	{
		Layout at;
		const std::vector<uint8_t> good = Build(Fixture(), &at);
		uint32_t addr;

		Load(good);
		Check(symbols::Count() == kFixtureSymbols, "the fixture loads (%zu symbols)", symbols::Count());

		// what is no 32-bit little-endian ELF
		{
			std::vector<uint8_t> image = good;
			image[0] = 0x7E;
			CheckEmpty(image, "no ELF magic");

			image = good;
			image[4] = 2;
			CheckEmpty(image, "a 64-bit ELF");

			image = good;
			image[5] = 2;
			CheckEmpty(image, "a big-endian ELF");

			CheckEmpty({ }, "an empty image");
			CheckEmpty({ 0x7F, 'E', 'L', 'F' }, "four bytes");
			CheckEmpty(std::vector<uint8_t>(4096, 0), "4096 zero bytes");
			CheckEmpty(std::vector<uint8_t>(4096, 0xFF), "4096 bytes FF");

			Load(good);
			symbols::OnElfLoaded(good.data(), 0);
			Check(symbols::Count() == 0, "an image of size 0 empties the table whatever the pointer is (%zu)", symbols::Count());

			Load(good);
			symbols::OnElfLoaded(nullptr, 0);
			Check(symbols::Count() == 0, "a null image empties the table (%zu)", symbols::Count());
		}

		// cut at every length: nothing is read behind the end, and what is found is of the fixture
		{
			size_t loaded = 0, partial = 0;

			for (size_t n = 0; n < good.size(); n++)
			{
				std::vector<uint8_t> cut(good.begin(), good.begin() + (std::ptrdiff_t)n);

				Load(cut);

				if (symbols::Count() != 0)
				{
					partial++;
					if (symbols::Count() > kFixtureSymbols)
						loaded++;
				}
			}

			Check(loaded == 0, "the image cut at each of its %zu lengths never gives more symbols than it has", good.size());
			Check(partial == 0, "and gives none at all: its section headers are at its end (%zu cuts gave some)", partial);
		}

		// the section headers
		{
			std::vector<uint8_t> image = good;
			Put32(image, 32, (uint32_t)good.size() - 8);
			CheckEmpty(image, "section headers that start 8 bytes in front of the end");

			image = good;
			Put32(image, 32, 0xFFFFFFF0);
			CheckEmpty(image, "section headers at FFFFFFF0");

			image = good;
			Put32(image, 32, 0);
			CheckEmpty(image, "no section headers");

			image = good;
			Put16(image, 48, 0);
			CheckEmpty(image, "a section count of 0");

			image = good;
			Put16(image, 46, 39);
			CheckEmpty(image, "section headers of 39 bytes");

			image = good;
			Put16(image, 46, 0xFFFF);
			CheckEmpty(image, "section headers of 65535 bytes");

			image = good;
			Put16(image, 48, 0xFFFF);
			Load(image);
			Check(symbols::Count() == 0 || symbols::Count() == kFixtureSymbols, "a section count of 65535: the headers behind the end are not read (%zu symbols)",
				symbols::Count());
		}

		// the .symtab
		{
			const size_t symtab_hdr = at.shdrs + 2 * kShdr;
			const size_t strtab_hdr = at.shdrs + 3 * kShdr;

			std::vector<uint8_t> image = good;
			Put32(image, symtab_hdr + kShOffset, (uint32_t)good.size() - 8);
			CheckEmpty(image, "a .symtab that starts 8 bytes in front of the end");

			image = good;
			Put32(image, symtab_hdr + kShOffset, 0xFFFFFFF8);
			CheckEmpty(image, "a .symtab at FFFFFFF8");

			image = good;
			Put32(image, symtab_hdr + kShSize, 0xFFFFFFF0);
			CheckEmpty(image, "a .symtab of FFFFFFF0 bytes");

			image = good;
			Put32(image, symtab_hdr + kShSize, (uint32_t)(good.size() - at.symtab + 1));
			CheckEmpty(image, "a .symtab that ends one byte behind the end");

			image = good;
			Put32(image, symtab_hdr + kShLink, 4);
			CheckEmpty(image, "a .symtab whose string table is section 4 of 4");

			// ... also when the bytes behind the section headers look like the header of one
			image = good;
			image.resize(good.size() + kShdr, 0);
			Put32(image, at.shdrs + 4 * kShdr + kShType, kSecStrtab);
			Put32(image, at.shdrs + 4 * kShdr + kShOffset, (uint32_t)at.strtab);
			Put32(image, at.shdrs + 4 * kShdr + kShSize, (uint32_t)at.strtabSize);
			Put32(image, symtab_hdr + kShLink, 4);
			CheckEmpty(image, "a .symtab whose string table is section 4 of 4, with what could be its header behind the headers");

			image = good;
			Put32(image, symtab_hdr + kShLink, 0xFFFFFFFF);
			CheckEmpty(image, "a .symtab whose string table is section FFFFFFFF");

			image = good;
			Put32(image, symtab_hdr + kShLink, 1);
			CheckEmpty(image, "a .symtab whose string table is no string table");

			image = good;
			Put32(image, symtab_hdr + kShEntsize, 8);
			CheckEmpty(image, "symbol entries of 8 bytes");

			image = good;
			Put32(image, symtab_hdr + kShEntsize, 0xFFFFFFFF);
			CheckEmpty(image, "symbol entries of FFFFFFFF bytes");

			image = good;
			Put32(image, symtab_hdr + kShEntsize, 0);
			Load(image);
			Check(symbols::Count() == kFixtureSymbols, "an entry size of 0 is the 16 bytes of ELF32 (%zu symbols)", symbols::Count());

			image = good;
			Put32(image, symtab_hdr + kShSize, (uint32_t)at.symtabSize - 1);
			Load(image);
			Check(symbols::Count() == kFixtureSymbols - 1, "a .symtab cut inside its last entry leaves that entry out (%zu symbols)",
				symbols::Count());
			Check(symbols::Find("_main", &addr) && addr == 0x8C010040, "and keeps the ones in front of it");

			// the string table
			image = good;
			Put32(image, strtab_hdr + kShOffset, (uint32_t)good.size() - 2);
			CheckEmpty(image, "a string table that starts 2 bytes in front of the end");

			image = good;
			Put32(image, strtab_hdr + kShSize, 0xFFFFFFFF);
			CheckEmpty(image, "a string table of FFFFFFFF bytes");

			image = good;
			Put32(image, strtab_hdr + kShOffset, 0xFFFFFFFF);
			CheckEmpty(image, "a string table at FFFFFFFF");

			// names
			image = good;
			Put32(image, at.symtab + 1 * kSymEntry, (uint32_t)at.strtabSize);
			Load(image);
			Check(symbols::Count() == kFixtureSymbols - 1 && !symbols::Find("_start", &addr), "a name at the end of the string table is no name (%zu symbols)",
				symbols::Count());

			image = good;
			Put32(image, at.symtab + 1 * kSymEntry, 0xFFFFFFFF);
			Load(image);
			Check(symbols::Count() == kFixtureSymbols - 1 && !symbols::Find("_start", &addr), "a name at FFFFFFFF is no name (%zu symbols)", symbols::Count());

			image = good;
			Put32(image, at.symtab + 1 * kSymEntry, (uint32_t)at.firstName + 1);
			Load(image);
			Check(symbols::Find("start", &addr) && addr == 0x8C010000, "a name can start anywhere in the string table");
		}

		// a string table that does not end with a NUL: the last name ends with the table
		{
			Layout cut_at;
			std::vector<uint8_t> image = Build({ { "_first", 0x8C010000, 4, kFunc, kGlobal, kText }, { "_unterminated", 0x8C010010, 4, kFunc, kGlobal, kText } },
				&cut_at, false);

			// the string table is the last thing in front of the section headers: make what follows it no NUL
			Check(cut_at.strtab + cut_at.strtabSize == cut_at.shdrs, "(the fixture has the section headers behind the string table)");
			image[cut_at.shdrs] = 'X';

			Load(image);
			Check(symbols::Count() == 2 && symbols::Find("_unterminated", &addr) && addr == 0x8C010010,
				"a name without a NUL ends where the string table ends (%zu symbols, text \"%s\")", symbols::Count(), Text(0x8C010010).c_str());

			// and such a table at the very end of the image: the string table is moved behind the section headers
			std::vector<uint8_t> moved = image;
			std::vector<uint8_t> names(image.begin() + (std::ptrdiff_t)cut_at.strtab, image.begin() + (std::ptrdiff_t)(cut_at.strtab + cut_at.strtabSize));

			Put32(moved, cut_at.shdrs + 3 * kShdr + kShOffset, (uint32_t)moved.size());
			moved.insert(moved.end(), names.begin(), names.end());

			Load(moved);
			Check(symbols::Count() == 2 && symbols::Find("_unterminated", &addr), "the same with the string table as the last bytes of the image (%zu symbols)",
				symbols::Count());
		}

		// values at the top of the address space: no sum wraps
		{
			Load(Build({
				{ "top_sized",  0xFFFFFFF0, 0x100,      kObject, kGlobal, kAbs },
				{ "huge",       0x8C000000, 0xFFFFFFFF, kObject, kGlobal, kText },
				{ "top_label",  0x1FFFFFFE, 0,          kNotype, kGlobal, kAbs },
			}));

			CheckIn(0x8C000000, "huge", 0, "a symbol whose size runs over the end of the address space");
			CheckIn(0x1FFFFFEF, "huge", 0x13FFFFEF, "reaches to the end of the 29 bits");
			CheckIn(0xFFFFFFF0, "top_sized", 0, "a symbol at FFFFFFF0");
			CheckIn(0xFFFFFFFD, "top_sized", 0xD, "...");
			CheckIn(0xFFFFFFFF, "top_label", 1, "a label two bytes in front of the end");
			CheckNone(0x0BFFFFFF, "in front of all of them");
		}

		// random damage: any bytes of the header, the section headers and the entries
		{
			size_t runs = 0;

			for (int round = 0; round < 20000; round++)
			{
				std::vector<uint8_t> image = good;
				const int hits = 1 + (int)(Rng() % 4);

				for (int h = 0; h < hits; h++)
				{
					size_t where;

					switch (Rng() % 4)
					{
					case 0:  where = Rng() % kEhdr; break;
					case 1:  where = at.shdrs + Rng() % (4 * kShdr); break;
					case 2:  where = at.symtab + Rng() % at.symtabSize; break;
					default: where = Rng() % image.size(); break;
					}

					image[where] = (uint8_t)(Rng() % 3 == 0 ? 0xFF : Rng());
				}

				// and sometimes the image is cut as well
				if (Rng() % 8 == 0)
					image.resize(Rng() % (image.size() + 1));

				Load(image);

				// every address has an answer
				for (uint32_t probe = 0x8C00FFF0; probe < 0x8C010500; probe += 0x37)
				{
					const char* name;
					uint32_t offset;

					if (symbols::Lookup(probe, &name, &offset))
						(void)strlen(name);
				}

				runs++;
			}

			Check(runs == 20000, "20000 images with random damage were loaded and looked into");
		}

		symbols::Clear();
	}
}

int main(int argc, char** argv)
{
	std::string mode = argc > 1 ? argv[1] : "";

	if (mode == "table" && argc == 2)
		TestTable();
	else if (mode == "damaged" && argc == 2)
		TestDamaged();
	else
	{
		fprintf(stderr, "usage: symbols_test table | damaged\n");
		return 2;
	}

	printf("%s: %d checks, %d failed\n", mode.c_str(), checks, failures);
	return failures == 0 && checks != 0 ? 0 : 1;
}
