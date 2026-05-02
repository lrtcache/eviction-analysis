#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

struct TraceEntry
{
	std::uint8_t mode{};
	uint64_t page_id{};
};

static_assert(sizeof(TraceEntry) == 16);

struct WindowTracker
{
	uint64_t window_size;
	uint64_t window_index{};
	uint64_t window_start{};
	unordered_set<uint64_t> pages{};
	ofstream out{};

	WindowTracker(uint64_t size, const fs::path& output_path)
	    : window_size{size}, window_index{}, window_start{}, pages{}, out{output_path}
	{
		out << "window_size,window_index,start_access,end_access,unique_pages\n";
	}

	void flush(uint64_t current_access)
	{
		if (pages.empty() && current_access == window_start) {
			return;
		}
		out << format("{},{},{},{},{}\n", window_size, window_index, window_start,
		              current_access, pages.size());
		pages.clear();
		window_index++;
		window_start = current_access;
	}

	void observe(uint64_t access_index, uint64_t page_id)
	{
		while (access_index >= window_start + window_size) {
			flush(window_start + window_size);
		}
		pages.insert(page_id);
	}

	void finish(uint64_t total_accesses) { flush(total_accesses); }
};

uint64_t quantile_from_counts(const map<uint64_t, uint64_t>& counts, uint64_t total,
                              double q)
{
	if (total == 0 || counts.empty()) {
		return 0;
	}
	const uint64_t rank = static_cast<uint64_t>(q * total + 0.999999999);
	uint64_t seen = 0;
	for (const auto& [value, freq] : counts) {
		seen += freq;
		if (seen >= max<uint64_t>(rank, 1)) {
			return value;
		}
	}
	return counts.rbegin()->first;
}

vector<uint64_t> parse_window_sizes(int argc, char** argv, int& argi)
{
	vector<uint64_t> sizes;
	while (argi < argc) {
		const string_view arg = argv[argi];
		if (arg.empty() || arg[0] == '-') {
			break;
		}
		if (!ranges::all_of(arg, [](unsigned char ch) { return isdigit(ch) != 0; })) {
			break;
		}
		sizes.push_back(stoull(argv[argi]));
		argi++;
	}
	return sizes;
}

vector<fs::path> expand_inputs(const vector<string>& inputs)
{
	vector<fs::path> res;
	for (const auto& raw : inputs) {
		fs::path path{raw};
		if (fs::is_directory(path)) {
			for (const auto& entry : fs::directory_iterator(path)) {
				if (entry.path().extension() == ".parsed") {
					res.push_back(entry.path());
				}
			}
			continue;
		}
		if (fs::exists(path)) {
			res.push_back(path);
		}
	}
	sort(res.begin(), res.end());
	res.erase(unique(res.begin(), res.end()), res.end());
	return res;
}

string output_stem(const fs::path& path)
{
	string name = path.filename().string();
	if (name.ends_with(".parsed")) {
		name.resize(name.size() - string(".parsed").size());
	}
	return name;
}

int main(int argc, char** argv)
{
	fs::path output_dir;
	bool has_output_dir = false;
	vector<uint64_t> window_sizes{1000, 10000, 100000};
	vector<string> inputs;

	for (int i = 1; i < argc; ++i) {
		const string arg = argv[i];
		if (arg == "--output-dir") {
			if (++i >= argc) {
				cerr << "--output-dir requires a value\n";
				return 1;
			}
			output_dir = argv[i];
			has_output_dir = true;
		} else if (arg == "--window-sizes") {
			i++;
			auto parsed = parse_window_sizes(argc, argv, i);
			if (parsed.empty()) {
				cerr << "--window-sizes requires at least one integer\n";
				return 1;
			}
			window_sizes = std::move(parsed);
			i--;
		} else {
			inputs.push_back(arg);
		}
	}

	if (inputs.empty()) {
		cerr << "usage: analyze_locality [--output-dir DIR] [--window-sizes N... ] "
		        "<parsed traces...>\n";
		return 1;
	}

	if (!has_output_dir) {
		output_dir = "outputs/locality-analysis";
	}
	fs::create_directories(output_dir);

	const auto paths = expand_inputs(inputs);
	if (paths.empty()) {
		cerr << "no parsed trace files found\n";
		return 1;
	}

	constexpr size_t ENTRY_BATCH = 1 << 16;
	vector<TraceEntry> buffer(ENTRY_BATCH);

	for (const auto& path : paths) {
		const auto filesize = fs::file_size(path);
		if (filesize % sizeof(TraceEntry) != 0) {
			cerr << "trace file size is not a multiple of entry size: "
			     << path.string() << '\n';
			return 1;
		}

		FILE* f = fopen(path.c_str(), "rb");
		if (f == nullptr) {
			perror("fopen");
			return 1;
		}
		setvbuf(f, nullptr, _IOFBF, 1 << 20);

		const auto stem = output_stem(path);
		ofstream reuse_out(output_dir / format("{}-reuse-gap-dist.csv", stem));
		ofstream summary_out(output_dir / format("{}-summary.csv", stem));
		reuse_out << "gap,count,fraction,cdf\n";
		summary_out
		    << "accesses,unique_pages,reuses,reuse_rate,p50_gap,p90_gap,p99_gap\n";

		vector<WindowTracker> windows;
		windows.reserve(window_sizes.size());
		for (const auto size : window_sizes) {
			windows.emplace_back(
			    size, output_dir / format("{}-working-set-{}.csv", stem, size));
		}

		unordered_map<uint64_t, uint64_t> last_seen;
		map<uint64_t, uint64_t> reuse_gap_counts;
		uint64_t accesses = 0;
		uint64_t reuses = 0;

		while (true) {
			const auto entries_read =
			    fread(buffer.data(), sizeof(TraceEntry), buffer.size(), f);
			if (entries_read == 0) {
				break;
			}
			for (size_t i = 0; i < entries_read; ++i) {
				const auto& entry = buffer[i];
				if (auto it = last_seen.find(entry.page_id); it != last_seen.end()) {
					const uint64_t gap = accesses - it->second - 1;
					reuse_gap_counts[gap]++;
					it->second = accesses;
					reuses++;
				} else {
					last_seen.emplace(entry.page_id, accesses);
				}

				for (auto& window : windows) {
					window.observe(accesses, entry.page_id);
				}
				accesses++;
			}
		}

		if (ferror(f)) {
			perror("fread");
			fclose(f);
			return 1;
		}
		fclose(f);

		for (auto& window : windows) {
			window.finish(accesses);
		}

		uint64_t seen = 0;
		for (const auto& [gap, count] : reuse_gap_counts) {
			seen += count;
			const double fraction =
			    reuses > 0 ? static_cast<double>(count) / static_cast<double>(reuses)
			               : 0.0;
			const double cdf =
			    reuses > 0 ? static_cast<double>(seen) / static_cast<double>(reuses)
			               : 0.0;
			reuse_out << format("{},{},{:.12f},{:.12f}\n", gap, count, fraction, cdf);
		}

		const uint64_t unique_pages = last_seen.size();
		const double reuse_rate =
		    accesses > 0 ? static_cast<double>(reuses) / static_cast<double>(accesses)
		                 : 0.0;
		summary_out << format("{},{},{},{:.6f},{},{},{}\n", accesses, unique_pages,
		                      reuses, reuse_rate,
		                      quantile_from_counts(reuse_gap_counts, reuses, 0.50),
		                      quantile_from_counts(reuse_gap_counts, reuses, 0.90),
		                      quantile_from_counts(reuse_gap_counts, reuses, 0.99));

		cout << path.string() << '\n';
		cout << format("  accesses={} unique_pages={} reuses={} reuse_rate={:.3f}\n",
		               accesses, unique_pages, reuses, reuse_rate);
		cout << format("  reuse gaps: p50={} p90={} p99={}\n",
		               quantile_from_counts(reuse_gap_counts, reuses, 0.50),
		               quantile_from_counts(reuse_gap_counts, reuses, 0.90),
		               quantile_from_counts(reuse_gap_counts, reuses, 0.99));
	}

	return 0;
}
