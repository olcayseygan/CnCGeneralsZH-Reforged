// eip_sampler - a 1 kHz sampling profiler for a running 32-bit generals.exe.
//
// There is no debugger on this machine, so this is how hotspots get names:
// suspend the target's main thread, read EIP out of its CONTEXT, resume, and
// tally the addresses.  At the end dbghelp turns the tallies into symbols.
//
//   eip_sampler generals.exe 30      sample the main thread for 30 seconds
//   eip_sampler 12345 10 -all        sample every thread of PID 12345
//
// Symbolization runs against the *live* process (SymInitialize with fInvade),
// not against the linker map: the .map's Publics do not list statics, and a
// sampler that cannot name statics lands most of its samples on whatever $R
// symbol happens to precede them.  Keep the PDB next to the exe.

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "winmm.lib")

namespace
{

// Toolhelp is the only way to walk another process' threads without reaching
// into ntdll internals.  The snapshot is a point-in-time list; threads created
// after it is taken are simply not sampled.
struct ThreadRef
{
	DWORD id;
	ULONGLONG created;   // FILETIME as an integer; the earliest one is main()
};

DWORD find_process(const char *spec)
{
	// A pure number is a PID, anything else is an image name.
	char *end = nullptr;
	const unsigned long as_pid = strtoul(spec, &end, 10);
	if (end != spec && *end == 0) {
		return static_cast<DWORD>(as_pid);
	}

	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) {
		return 0;
	}

	PROCESSENTRY32 pe = {sizeof(pe)};
	DWORD found = 0;
	for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe)) {
		if (_stricmp(pe.szExeFile, spec) == 0) {
			found = pe.th32ProcessID;
			break;
		}
	}
	CloseHandle(snap);
	return found;
}

std::vector<ThreadRef> list_threads(DWORD pid)
{
	std::vector<ThreadRef> threads;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE) {
		return threads;
	}

	THREADENTRY32 te = {sizeof(te)};
	for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
		if (te.th32OwnerProcessID != pid) {
			continue;
		}
		HANDLE th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
		if (th == nullptr) {
			continue;
		}
		FILETIME created = {}, exited = {}, kernel = {}, user = {};
		if (GetThreadTimes(th, &created, &exited, &kernel, &user)) {
			ULARGE_INTEGER when;
			when.LowPart = created.dwLowDateTime;
			when.HighPart = created.dwHighDateTime;
			threads.push_back({te.th32ThreadID, when.QuadPart});
		}
		CloseHandle(th);
	}
	CloseHandle(snap);

	std::sort(threads.begin(), threads.end(),
		[](const ThreadRef &a, const ThreadRef &b) { return a.created < b.created; });
	return threads;
}

// Samples are bucketed by symbol rather than by address, so a hot loop spread
// over a hundred instructions reads as one row instead of a hundred.
struct Bucket
{
	std::string name;
	std::string file;
	unsigned line;
	unsigned long long samples;
};

std::string module_of(HANDLE proc, DWORD64 addr)
{
	IMAGEHLP_MODULE64 mi = {sizeof(mi)};
	if (!SymGetModuleInfo64(proc, addr, &mi)) {
		return "?";
	}
	return mi.ModuleName;
}

void report(HANDLE proc, const std::map<DWORD64, unsigned long long> &hits,
	unsigned long long total, const char *title)
{
	std::map<std::string, Bucket> by_symbol;
	for (const auto &hit : hits) {
		const DWORD64 addr = hit.first;

		// SYMBOL_INFO carries its name inline past the end of the struct, so it
		// has to be over-allocated.  MaxNameLen counts characters, not bytes.
		char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
		SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO *>(storage);
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = MAX_SYM_NAME;

		std::string key;
		DWORD64 displacement = 0;
		if (SymFromAddr(proc, addr, &displacement, sym)) {
			key = sym->Name;
		} else {
			char raw[96];
			sprintf_s(raw, "%s!0x%08llx", module_of(proc, addr).c_str(),
				static_cast<unsigned long long>(addr));
			key = raw;
		}

		Bucket &bucket = by_symbol[key];
		if (bucket.samples == 0) {
			bucket.name = key;
			bucket.line = 0;
			IMAGEHLP_LINE64 li = {sizeof(li)};
			DWORD line_displacement = 0;
			if (SymGetLineFromAddr64(proc, addr, &line_displacement, &li)) {
				bucket.file = li.FileName;
				bucket.line = li.LineNumber;
			}
		}
		bucket.samples += hit.second;
	}

	std::vector<Bucket> ranked;
	ranked.reserve(by_symbol.size());
	for (const auto &entry : by_symbol) {
		ranked.push_back(entry.second);
	}
	std::sort(ranked.begin(), ranked.end(),
		[](const Bucket &a, const Bucket &b) { return a.samples > b.samples; });

	printf("\n%s - %llu samples, %zu distinct addresses, %zu symbols\n",
		title, total, hits.size(), by_symbol.size());
	printf("%7s  %6s  %s\n", "samples", "pct", "symbol");
	const size_t shown = ranked.size() < 40 ? ranked.size() : 40;
	for (size_t i = 0; i < shown; ++i) {
		const Bucket &b = ranked[i];
		printf("%7llu  %5.1f%%  %s", b.samples, 100.0 * b.samples / total, b.name.c_str());
		if (b.line != 0) {
			// The path is long and only its tail is useful here.
			const size_t slash = b.file.find_last_of("\\/");
			const char *leaf = slash == std::string::npos
				? b.file.c_str() : b.file.c_str() + slash + 1;
			printf("   [%s:%u]", leaf, b.line);
		}
		printf("\n");
	}
	if (ranked.size() > shown) {
		printf("        ... %zu more symbols\n", ranked.size() - shown);
	}
}

// -callers answers "who is calling this leaf": a leaf like the CRT's printf
// formatter can own a tenth of the frame and say nothing about which of a
// hundred callers is paying for it.  The stack is walked while the thread is
// still suspended, since it is moving the moment the thread resumes.
const size_t CALLER_DEPTH = 6;
const size_t CALLER_ROWS_SHOWN = 25;
using CallerChain = std::array<DWORD64, CALLER_DEPTH>;

// The sampler is built for the game's architecture: an x64 sampler reads an x64 game, an ARM64 one
// an ARM64 game.
#if defined(_M_ARM64)
#define CONTEXT_PC Pc
#define CONTEXT_FRAME Fp
#define CONTEXT_STACK Sp
const DWORD SAMPLED_MACHINE = IMAGE_FILE_MACHINE_ARM64;
#else
#define CONTEXT_PC Rip
#define CONTEXT_FRAME Rbp
#define CONTEXT_STACK Rsp
const DWORD SAMPLED_MACHINE = IMAGE_FILE_MACHINE_AMD64;
#endif

std::string symbol_name(HANDLE proc, DWORD64 addr, std::map<DWORD64, std::string> &cache)
{
	const auto found = cache.find(addr);
	if (found != cache.end()) {
		return found->second;
	}

	char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
	SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO *>(storage);
	sym->SizeOfStruct = sizeof(SYMBOL_INFO);
	sym->MaxNameLen = MAX_SYM_NAME;

	std::string name;
	DWORD64 displacement = 0;
	if (SymFromAddr(proc, addr, &displacement, sym)) {
		name = sym->Name;
	} else {
		char raw[32];
		sprintf_s(raw, "0x%08llx", static_cast<unsigned long long>(addr));
		name = raw;
	}
	cache[addr] = name;
	return name;
}

CallerChain walk_callers(HANDLE proc, HANDLE thread, CONTEXT ctx)
{
	CallerChain chain = {};
	STACKFRAME64 frame = {};
	frame.AddrPC.Offset = ctx.CONTEXT_PC;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.CONTEXT_FRAME;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.CONTEXT_STACK;
	frame.AddrStack.Mode = AddrModeFlat;

	for (size_t depth = 0; depth < CALLER_DEPTH; ++depth) {
		const BOOL walked = StackWalk64(SAMPLED_MACHINE, proc, thread, &frame, &ctx, nullptr,
			SymFunctionTableAccess64, SymGetModuleBase64, nullptr);
		if (!walked || frame.AddrPC.Offset == 0) {
			break;
		}
		chain[depth] = frame.AddrPC.Offset;
	}
	return chain;
}

void report_callers(HANDLE proc, const std::vector<CallerChain> &chains, const char *leaf_filter)
{
	std::map<DWORD64, std::string> names;
	std::map<std::string, unsigned long long> by_chain;
	unsigned long long matched = 0;

	for (const CallerChain &chain : chains) {
		if (chain[0] == 0 || symbol_name(proc, chain[0], names).find(leaf_filter) == std::string::npos) {
			continue;
		}
		++matched;
		std::string key;
		for (size_t depth = 1; depth < CALLER_DEPTH && chain[depth] != 0; ++depth) {
			if (!key.empty()) {
				key += " < ";
			}
			key += symbol_name(proc, chain[depth], names);
		}
		++by_chain[key];
	}

	std::vector<std::pair<std::string, unsigned long long>> ranked(by_chain.begin(), by_chain.end());
	std::sort(ranked.begin(), ranked.end(),
		[](const auto &a, const auto &b) { return a.second > b.second; });

	printf("\ncallers of leaves matching '%s' - %llu of %zu main-thread samples\n",
		leaf_filter, matched, chains.size());
	const size_t shown = ranked.size() < CALLER_ROWS_SHOWN ? ranked.size() : CALLER_ROWS_SHOWN;
	for (size_t i = 0; i < shown; ++i) {
		printf("%7llu  %s\n", ranked[i].second, ranked[i].first.c_str());
	}
}

// -under answers "where does this function's time go": a sample whose stack holds a frame matching
// the filter counts toward it, and every function between the leaf and that frame is ranked by how
// many such samples it was on the stack for, once a sample however often it recurses.  The stack is
// walked deep enough to reach the render loop from a leaf inside the driver.
const size_t UNDER_DEPTH = 48;

std::vector<DWORD64> walk_stack(HANDLE proc, HANDLE thread, CONTEXT ctx)
{
	std::vector<DWORD64> stack;
	stack.push_back(ctx.CONTEXT_PC);
	STACKFRAME64 frame = {};
	frame.AddrPC.Offset = ctx.CONTEXT_PC;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.CONTEXT_FRAME;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.CONTEXT_STACK;
	frame.AddrStack.Mode = AddrModeFlat;
	for (size_t depth = 0; depth < UNDER_DEPTH; ++depth) {
		const BOOL walked = StackWalk64(SAMPLED_MACHINE, proc, thread, &frame, &ctx, nullptr,
			SymFunctionTableAccess64, SymGetModuleBase64, nullptr);
		if (!walked || frame.AddrPC.Offset == 0) {
			break;
		}
		if (depth > 0) {	// the first walked frame is the leaf itself
			stack.push_back(frame.AddrPC.Offset);
		}
	}
	return stack;
}

void report_under(HANDLE proc, const std::vector<std::vector<DWORD64>> &stacks, const char *root_filter)
{
	std::map<DWORD64, std::string> names;
	std::map<std::string, unsigned long long> inclusive;
	std::map<std::string, unsigned long long> leaves;
	unsigned long long matched = 0;

	for (const std::vector<DWORD64> &stack : stacks) {
		size_t root = stack.size();
		for (size_t depth = 0; depth < stack.size(); ++depth) {
			if (symbol_name(proc, stack[depth], names).find(root_filter) != std::string::npos) {
				root = depth;
				break;
			}
		}
		if (root == stack.size()) {
			continue;
		}
		++matched;
		++leaves[symbol_name(proc, stack[0], names)];
		std::map<std::string, bool> seen;
		for (size_t depth = 0; depth <= root; ++depth) {
			const std::string &name = symbol_name(proc, stack[depth], names);
			if (!seen[name]) {
				seen[name] = true;
				++inclusive[name];
			}
		}
	}

	printf("\nunder '%s' - %llu of %zu main-thread samples\n", root_filter, matched, stacks.size());
	if (matched == 0) {
		return;
	}
	const auto print_ranked = [matched](const std::map<std::string, unsigned long long> &counts,
		const char *title) {
		std::vector<std::pair<std::string, unsigned long long>> ranked(counts.begin(), counts.end());
		std::sort(ranked.begin(), ranked.end(),
			[](const auto &a, const auto &b) { return a.second > b.second; });
		printf("%s\n", title);
		const size_t shown = ranked.size() < 45 ? ranked.size() : 45;
		for (size_t i = 0; i < shown; ++i) {
			printf("%7llu  %5.1f%%  %s\n", ranked[i].second, 100.0 * ranked[i].second / matched,
				ranked[i].first.c_str());
		}
	};
	print_ranked(inclusive, "inclusive (on the stack):");
	print_ranked(leaves, "self (the leaf):");
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr,
			"usage: %s <exe-name|pid> <seconds> [-all] [-callers <leaf substring>] [-under <function substring>]\n"
			"       -all samples every thread, not just main\n"
			"       -callers walks the main thread's stack and ranks who calls the matching leaves\n"
			"       -under ranks the functions the main thread spent its time in below the matching one\n"
			"       -tid <thread id> samples that thread in main's place: GameLogic::update runs on a thread of\n"
			"            its own, the busiest one an -all run lists\n", argv[0]);
		return 2;
	}

	const DWORD pid = find_process(argv[1]);
	if (pid == 0) {
		fprintf(stderr, "eip_sampler: no process matching '%s'\n", argv[1]);
		return 1;
	}
	const double seconds = atof(argv[2]);
	bool all_threads = false;
	const char *caller_filter = nullptr;
	const char *under_filter = nullptr;
	DWORD only_tid = 0;
	for (int arg = 3; arg < argc; ++arg) {
		if (_stricmp(argv[arg], "-all") == 0) {
			all_threads = true;
		} else if (_stricmp(argv[arg], "-tid") == 0 && arg + 1 < argc) {
			only_tid = strtoul(argv[++arg], nullptr, 10);
		} else if (_stricmp(argv[arg], "-callers") == 0 && arg + 1 < argc) {
			caller_filter = argv[++arg];
		} else if (_stricmp(argv[arg], "-under") == 0 && arg + 1 < argc) {
			under_filter = argv[++arg];
		}
	}

	HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (proc == nullptr) {
		fprintf(stderr, "eip_sampler: OpenProcess(%lu) failed, error %lu "
			"(run elevated?)\n", pid, GetLastError());
		return 1;
	}

	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
	if (!SymInitialize(proc, nullptr, TRUE)) {
		fprintf(stderr, "eip_sampler: SymInitialize failed, error %lu\n", GetLastError());
		CloseHandle(proc);
		return 1;
	}

	std::vector<ThreadRef> threads = list_threads(pid);
	if (threads.empty()) {
		fprintf(stderr, "eip_sampler: pid %lu has no readable threads\n", pid);
		SymCleanup(proc);
		CloseHandle(proc);
		return 1;
	}
	if (only_tid != 0) {
		threads.assign(1, ThreadRef{only_tid, 0});
	} else if (!all_threads) {
		threads.resize(1);   // earliest creation time == the main thread
	}

	// Open every thread up front: an OpenThread per sample would cost more than
	// the sample it pays for.
	std::vector<HANDLE> handles;
	std::vector<DWORD> ids;
	for (const ThreadRef &t : threads) {
		HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, t.id);
		if (th != nullptr) {
			handles.push_back(th);
			ids.push_back(t.id);
		}
	}
	if (handles.empty()) {
		fprintf(stderr, "eip_sampler: could not open any thread of pid %lu\n", pid);
		SymCleanup(proc);
		CloseHandle(proc);
		return 1;
	}

	printf("eip_sampler: pid %lu, %zu thread(s), %.1fs at ~1 kHz\n",
		pid, handles.size(), seconds);

	// The default 15.6 ms scheduler tick would stretch Sleep(1) to ~16 ms and
	// turn "1 kHz" into 64 Hz.
	timeBeginPeriod(1);

	std::vector<std::map<DWORD64, unsigned long long>> hits(handles.size());
	std::vector<unsigned long long> totals(handles.size(), 0);
	std::vector<CallerChain> chains;
	std::vector<std::vector<DWORD64>> stacks;
	unsigned long long missed = 0;

	LARGE_INTEGER freq = {}, start = {}, now = {};
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&start);
	const LONGLONG deadline = start.QuadPart + static_cast<LONGLONG>(seconds * freq.QuadPart);

	for (;;) {
		QueryPerformanceCounter(&now);
		if (now.QuadPart >= deadline) {
			break;
		}

		for (size_t i = 0; i < handles.size(); ++i) {
			if (SuspendThread(handles[i]) == static_cast<DWORD>(-1)) {
				++missed;
				continue;
			}
			CONTEXT ctx = {};
			ctx.ContextFlags = (caller_filter || under_filter) ? CONTEXT_FULL : CONTEXT_CONTROL;
			if (GetThreadContext(handles[i], &ctx)) {
				++hits[i][ctx.CONTEXT_PC];
				++totals[i];
				if (caller_filter && i == 0) {
					chains.push_back(walk_callers(proc, handles[i], ctx));
				}
				if (under_filter && i == 0) {
					stacks.push_back(walk_stack(proc, handles[i], ctx));
				}
			} else {
				++missed;
			}
			ResumeThread(handles[i]);
		}

		Sleep(1);
	}

	QueryPerformanceCounter(&now);
	const double elapsed = double(now.QuadPart - start.QuadPart) / double(freq.QuadPart);
	timeEndPeriod(1);

	unsigned long long grand_total = 0;
	for (unsigned long long t : totals) {
		grand_total += t;
	}
	printf("done: %llu samples over %.2fs (%.0f Hz per thread), %llu missed\n",
		grand_total, elapsed, grand_total / elapsed / handles.size(), missed);

	for (size_t i = 0; i < handles.size(); ++i) {
		if (totals[i] != 0) {
			char title[64];
			sprintf_s(title, "thread %lu%s", ids[i], i == 0 ? " (main)" : "");
			report(proc, hits[i], totals[i], title);
		}
		CloseHandle(handles[i]);
	}

	if (caller_filter) {
		report_callers(proc, chains, caller_filter);
	}
	if (under_filter) {
		report_under(proc, stacks, under_filter);
	}

	SymCleanup(proc);
	CloseHandle(proc);
	return 0;
}
